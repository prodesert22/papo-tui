"""A stand-in for the GitHub releases API, served over TLS on the loopback."""

from contextlib import contextmanager
from pathlib import Path
import base64
import hashlib
import struct
import ssl
import subprocess
import tempfile

from mock_server import MockServer


def release(tag, prerelease=None, draft=False, assets=()):
    """One release object with the fields the client reads."""
    return {
        "tag_name": tag,
        "draft": draft,
        "prerelease": ("-" in tag) if prerelease is None else prerelease,
        "assets": list(assets),
    }


def asset(name, data, digest=None, size=None):
    """One asset entry as GitHub lists it; digest and size default to the data's."""
    return {
        "name": name,
        "size": len(data) if size is None else size,
        "digest": "sha256:" + hashlib.sha256(data).hexdigest() if digest is None else digest,
        "browser_download_url": "https://example.invalid/unused/" + name,
    }


class SigningKey:
    """An RSA key made by openssl, to sign what the stand-in publishes.

    Nobody but the project can sign with the real release key, so the program
    is told to trust this one instead (``public`` is its DER file).
    """

    def __init__(self, directory, name="release-key"):
        self.private = Path(directory) / (name + ".pem")
        self.public = Path(directory) / (name + ".der")
        subprocess.run(
            ["openssl", "genrsa", "-out", str(self.private), "2048"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        subprocess.run(
            ["openssl", "rsa", "-in", str(self.private), "-pubout", "-outform", "DER", "-out"]
            + [str(self.public)],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    def sign(self, data):
        """An armored detached OpenPGP signature of data, as gpg writes one.

        A version 4 signature packet over a binary document, RSA with SHA-512,
        with the creation time as its only hashed subpacket.
        """
        created = b"\x05\x02" + struct.pack(">I", 1794784485)
        head = bytes([4, 0x00, 1, 10]) + struct.pack(">H", len(created)) + created
        covered = data + head + b"\x04\xff" + struct.pack(">I", len(head))
        raw = subprocess.run(
            ["openssl", "dgst", "-sha512", "-sign", str(self.private)],
            input=covered,
            capture_output=True,
            check=True,
        ).stdout
        number = int.from_bytes(raw, "big")
        digits = number.to_bytes((number.bit_length() + 7) // 8, "big")
        body = (
            head
            + struct.pack(">H", 0)
            + hashlib.sha512(covered).digest()[:2]
            + struct.pack(">H", number.bit_length())
            + digits
        )
        packet = b"\x89" + struct.pack(">H", len(body)) + body
        crc = 0xB704CE
        for byte in packet:
            crc ^= byte << 16
            for _ in range(8):
                crc <<= 1
                if crc & 0x1000000:
                    crc ^= 0x1864CFB
        radix = base64.b64encode(packet).decode()
        lines = [radix[i : i + 64] for i in range(0, len(radix), 64)]
        checksum = base64.b64encode((crc & 0xFFFFFF).to_bytes(3, "big")).decode()
        return (
            "-----BEGIN PGP SIGNATURE-----\n\n"
            + "\n".join(lines)
            + "\n="
            + checksum
            + "\n-----END PGP SIGNATURE-----\n"
        ).encode()


def certificate(directory):
    """Writes a self-signed certificate for 127.0.0.1 and returns its files."""
    cert, key = Path(directory) / "cert.pem", Path(directory) / "key.pem"
    subprocess.run(
        [
            "openssl",
            "req",
            "-x509",
            "-newkey",
            "rsa:2048",
            "-nodes",
            "-days",
            "1",
            "-keyout",
            str(key),
            "-out",
            str(cert),
            "-subj",
            "/CN=127.0.0.1",
            "-addext",
            "subjectAltName=IP:127.0.0.1",
        ],
        check=True,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return cert, key


class UpdateMock:
    """Answers the release requests; ``answer`` is what the next ones receive.

    ``answer`` is a list of releases, newest first as GitHub lists them, or a
    tuple in the form the mock server's ``respond`` takes (status, body, cookie,
    retry_after, content_type). ``environment`` points a client at this server
    and makes it trust the certificate.
    """

    def __init__(self, server, cert, key, stranger):
        self.server = server
        self.answer = []
        # Each release publishes SHA256SUMS, listing the digest of every asset
        # in ``answer``, and SHA256SUMS.asc, its signature by ``signer``.
        # ``signed`` False publishes neither; ``manifest`` replaces the listed
        # sums with a dict of name to hexadecimal sum; ``stranger`` is a key
        # the program does not trust, to sign with instead.
        self.key = key
        self.stranger = stranger
        self.signer = key
        self.signed = True
        self.manifest = None
        # What each download path answers: the bytes of a file, or a tuple in
        # the form ``respond`` takes (a redirect is (302, None, None, None,
        # None, location)). Paths are "v<version>/<name>".
        self.files = {}
        # Terminals of the test suites start with the automatic check off.
        self.environment = {
            "PAPO_UPDATE_API_URL": server.url + "/github",
            "PAPO_UPDATE_CHECK": "1",
            "PAPO_UPDATE_KEY": str(key.public),
            "CURL_CA_BUNDLE": str(cert),
        }
        server.api_handler = self.route

    def sums(self, tag):
        """The text of SHA256SUMS for the release with that tag, or None."""
        listed = self.manifest
        if listed is None:
            found = [r for r in self.answer if isinstance(r, dict) and r["tag_name"] == tag]
            if not found:
                return None
            listed = {
                a["name"]: a["digest"][7:]
                for a in found[0]["assets"]
                if a["digest"].startswith("sha256:")
            }
        return "".join(f"{value}  {name}\n" for name, value in sorted(listed.items())).encode()

    def route(self, method, path, _body):
        if method == "GET" and "/github/download/" in path:
            relative = path.split("/github/download/", 1)[1]
            served = self.files.get(relative)
            tag, _, name = relative.partition("/")
            if served is None and self.signed and name in ("SHA256SUMS", "SHA256SUMS.asc"):
                served = self.sums(tag)
                if served is not None and name.endswith(".asc"):
                    served = self.signer.sign(served)
            if isinstance(served, tuple):
                return served
            if served is None:
                return 404, None
            return 200, served, None, None, "application/octet-stream"
        if method != "GET" or "/github/releases" not in path:
            return 404, None
        if isinstance(self.answer, tuple):
            return self.answer
        if path.endswith("/releases/latest") and isinstance(self.answer, list):
            # GitHub answers this one with the newest published final release.
            finals = [r for r in self.answer if not r["draft"] and not r["prerelease"]]
            return (200, finals[0]) if finals else (404, {"message": "Not Found"})
        return 200, self.answer

    @property
    def calls(self):
        """The release requests: the paths asked for, and the cookies sent with them."""
        return [(r[1], r[2]) for r in self.server.requests if "/github/releases" in r[1]]

    @property
    def proofs(self):
        """The requests for a release's manifest or its signature, with their cookies."""
        return [
            (r[1], r[2])
            for r in self.server.requests
            if r[1].endswith(("/SHA256SUMS", "/SHA256SUMS.asc"))
        ]

    @property
    def downloads(self):
        """The package requests: the paths asked for, and the cookies sent with them."""
        return [
            (r[1], r[2])
            for r in self.server.requests
            if "/github/releases" not in r[1]
            and not r[1].endswith(("/SHA256SUMS", "/SHA256SUMS.asc"))
        ]

    def url(self, path):
        """The address of a download path on this server."""
        return self.server.url + "/github/download/" + path


@contextmanager
def update_mock():
    with tempfile.TemporaryDirectory(prefix="papo-update-tls-") as directory:
        cert, key = certificate(directory)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        release_key = SigningKey(directory)
        stranger = SigningKey(directory, "stranger-key")
        with MockServer(tls_context=context) as server:
            yield UpdateMock(server, cert, release_key, stranger)
