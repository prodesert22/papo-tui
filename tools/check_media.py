"""Media integration with the real Go backend, synthetic data in a temporary DB.

No source database writes. Cookies and credentials stay in memory. The PTY
responder is synthetic; its protocol evidence is separate from an emulator.
"""

import argparse
import hashlib
from contextlib import ExitStack
import json
import os
from pathlib import Path
import secrets
import sys
import tempfile
import time
from uuid import UUID
import requests
from tools.check_backend import private_write
from tools.check_r3 import ROOT, checked_run, isolated_backend
from tools.check_r6 import proxy


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--backend-binary", type=Path, required=True)
    p.add_argument("--tls-proxy", type=Path, required=True)
    p.add_argument("--postgres-container", default="papo_postgres")
    p.add_argument("--binary", type=Path, default=ROOT / "build/papo-tui")
    p.add_argument("--probe", type=Path, default=ROOT / "build/media-probe")
    p.add_argument("--output", type=Path, default=ROOT / "build/media/validation")
    args = p.parse_args()
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=True, mode=0o700)
    checks = {}
    stage = "setup"
    next_request = 0
    last_status = None
    try:
        previous = sys.argv[:]
        sys.path.insert(0, str(ROOT / "tests"))
        sys.argv = [sys.argv[0], str(args.binary.resolve()), "ffmpeg"]
        try:
            from test_media_terminal import GraphicsStream, MediaTerminal
            from test_terminal import Terminal
        finally:
            sys.argv = previous
        MediaTerminal.setUpClass()
        with ExitStack() as stack:
            stack.callback(MediaTerminal.tearDownClass)
            access = []
            base, seed, _ = stack.enter_context(
                isolated_backend(
                    args.backend_binary.resolve(),
                    args.postgres_container,
                    work,
                    on_ready=access.append,
                    label="media",
                )
            )
            seed()
            sql = access[0]
            channel = sql(
                "INSERT INTO channels(name,type,position,topic) VALUES('media-fixture','text',1,'Synthetic media') RETURNING id;"
            ).strip()
            directory = Path(
                stack.enter_context(tempfile.TemporaryDirectory(prefix="tls-", dir=work))
            )
            cert, key = directory / "cert.pem", directory / "key.pem"
            checked_run(
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
                timeout=20,
            )
            key.chmod(0o600)
            secure = stack.enter_context(
                proxy(args.tls_proxy.resolve(), base, cert, key, delay="0s")
            )
            session = stack.enter_context(requests.Session())
            session.trust_env = False
            session.verify = str(cert)

            def call(method, path, status=200, **kwargs):
                nonlocal next_request, stage, last_status
                delay = next_request - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                response = session.request(
                    method, secure + path, timeout=20, allow_redirects=False, **kwargs
                )
                next_request = time.monotonic() + 0.2
                last_status = response.status_code
                if response.status_code != status:
                    raise RuntimeError("Unexpected status at " + stage)
                return response

            username = "media_" + secrets.token_hex(4)
            password = "Media!" + secrets.token_hex(12)
            stage = "registration"
            call("POST", "/auth/register", 201, json={"username": username, "password": password})
            call("POST", "/auth/login", json={"username": username, "password": password})
            identity = call("GET", "/auth/whoami").json()
            sql("UPDATE servers SET owner_id='" + str(UUID(identity["id"])) + "';")
            attachments = []
            for ext, mime in [
                ("png", "image/png"),
                ("jpg", "image/jpeg"),
                ("webp", "image/webp"),
                ("gif", "image/gif"),
                ("mp4", "video/mp4"),
            ]:
                stage = "upload " + ext
                content = MediaTerminal.video if ext == "mp4" else MediaTerminal.images[ext]
                message = call(
                    "POST",
                    "/messages",
                    201,
                    data={"channel_id": channel, "content": "Media " + ext},
                    files={"attachments": ("fixture." + ext, content, mime)},
                ).json()
                attachment = message["attachments"][0]
                attachments.append((attachment, content, ext))
            stage = "custom embed"
            embed_input = {
                "title": "Media card",
                "description": "Actual backend fixture",
                "color": "#ffb347",
                "author": {"name": "Synthetic author"},
                "fields": [{"name": "First", "value": "Field value", "inline": True}],
                "footer": {"text": "Media footer"},
            }
            message = call(
                "POST",
                "/messages",
                201,
                files={
                    "channel_id": (None, channel),
                    "content": (None, ""),
                    "embeds": (None, json.dumps([embed_input])),
                },
            ).json()
            embed = message["embeds"][0]
            checks["backend_accepts_supported_embed_attributes"] = (
                embed["title"] == "Media card" and embed["fields"][0]["inline"]
            )
            stage = "downloads"
            for attachment, content, ext in attachments:
                downloaded = call("GET", "/attachments/" + attachment["id"])
                checks["real_" + ext + "_bytes"] = downloaded.content == content
                environment = dict(
                    os.environ,
                    NO_PROXY="*",
                    CURL_CA_BUNDLE=str(cert),
                    PAPO_PROBE_USERNAME=username,
                    PAPO_PROBE_PASSWORD=password,
                    PAPO_MEDIA_ID=attachment["id"],
                )
                result = checked_run(
                    [
                        str(args.probe.resolve()),
                        secure,
                        "attachment-video" if ext == "mp4" else "image",
                    ],
                    env=environment,
                    timeout=30,
                )
                checks["c_authenticated_" + ext + "_download"] = all(
                    value in result for value in ("ready=1", "private=1", "independent=1")
                )
                time.sleep(0.3)
            details = call("GET", "/embeds/" + embed["id"]).json()
            checks["embed_without_image_is_accessible"] = (
                details["image_data"] is None and details["author_image_data"] is None
            )
            checks["missing_attachment_is_404"] = (
                call("GET", "/attachments/00000000-0000-4000-8000-000000000999", 404).status_code
                == 404
            )
            stage = "product PTY"
            with tempfile.TemporaryDirectory(prefix="client-", dir=work) as client_directory:
                terminal = Terminal(
                    environment={"CURL_CA_BUNDLE": str(cert), "TMPDIR": client_directory}
                )
                try:
                    terminal.stream = GraphicsStream(terminal.screen)
                    terminal.send("\x1b[?62;4;22c\x1b[6;16;8t")
                    terminal.send(secure + "\t" + username + "\t" + password + "\n")

                    def wait(value):
                        deadline = time.monotonic() + 10
                        while (
                            value not in "\n".join(terminal.screen.display)
                            and time.monotonic() < deadline
                        ):
                            terminal.read(0.03)
                        if value not in "\n".join(terminal.screen.display):
                            raise RuntimeError("Product state absent")

                    wait("media-fixture")
                    terminal.resize(40, 100)
                    terminal.send("\x1bOB\n")
                    wait("Media card")
                    checks["actual_backend_cards_in_product"] = "Field value" in "\n".join(
                        terminal.screen.display
                    )
                    terminal.send("\x1bOH")
                    deadline = time.monotonic() + 10
                    while not terminal.stream.frames and time.monotonic() < deadline:
                        terminal.read(0.03)
                    checks["actual_backend_image_through_c_decoder_and_sixel"] = bool(
                        terminal.stream.frames
                    )
                    checks["product_hides_synthetic_password"] = (
                        password.encode() not in terminal.output
                    )
                finally:
                    checks["product_restores_terminal"] = terminal.close()
                checks["product_removes_temporary_media"] = not list(
                    Path(client_directory).iterdir()
                )
            stage = "logout"
            call("POST", "/auth/logout", 204)
        report = {
            "passed": all(checks.values()),
            "kind": "actual-go-backend-isolated-database",
            "checks": checks,
            "source_database_mutated": False,
            "temporary_resources_removed": True,
            "terminal_pixel_evidence": "PTY protocol responder; see separate emulator evidence",
        }
        report["binary_sha256"] = hashlib.sha256(args.binary.read_bytes()).hexdigest()
        report["backend_sha256"] = hashlib.sha256(args.backend_binary.read_bytes()).hexdigest()
        private_write(work / "summary.json", report)
        print(json.dumps(report, ensure_ascii=False))
        return 0 if report["passed"] else 1
    except Exception:
        report = {
            "passed": False,
            "checks": checks,
            "stage": stage,
            "http_status": last_status,
            "error": "Validation failed; credentials omitted",
        }
        private_write(work / "summary.json", report)
        print(json.dumps(report, ensure_ascii=False))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
