"""A stand-in for the GitHub releases API, served over TLS on the loopback."""

from contextlib import contextmanager
from pathlib import Path
import hashlib
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

    def __init__(self, server, cert):
        self.server = server
        self.answer = []
        # What each download path answers: the bytes of a file, or a tuple in
        # the form ``respond`` takes (a redirect is (302, None, None, None,
        # None, location)). Paths are "v<version>/<name>".
        self.files = {}
        # Terminals of the test suites start with the automatic check off.
        self.environment = {
            "PAPO_UPDATE_API_URL": server.url + "/github",
            "PAPO_UPDATE_CHECK": "1",
            "CURL_CA_BUNDLE": str(cert),
        }
        server.api_handler = self.route

    def route(self, method, path, _body):
        if method == "GET" and "/github/download/" in path:
            served = self.files.get(path.split("/github/download/", 1)[1])
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
    def downloads(self):
        """The file requests: the paths asked for, and the cookies sent with them."""
        return [(r[1], r[2]) for r in self.server.requests if "/github/releases" not in r[1]]

    def url(self, path):
        """The address of a download path on this server."""
        return self.server.url + "/github/download/" + path


@contextmanager
def update_mock():
    with tempfile.TemporaryDirectory(prefix="papo-update-tls-") as directory:
        cert, key = certificate(directory)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        with MockServer(tls_context=context) as server:
            yield UpdateMock(server, cert)
