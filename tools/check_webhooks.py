"""Validate webhook attribution against an isolated copy of the Go backend.

All accounts, webhook tokens and messages are synthetic. The development
database is read only; migrations and mutations apply only to its disposable
copy. Reports contain fixed check names and booleans, never credentials.
"""

import argparse
from contextlib import ExitStack
import base64
import json
from pathlib import Path
import re
import secrets
import sys
import tempfile
import time
from uuid import UUID
import requests
from tools.check_backend import private_write
from tools.check_r3 import ROOT, isolated_backend, checked_run
from tools.check_r6 import proxy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend-binary", type=Path, required=True)
    parser.add_argument("--backend-source", type=Path, required=True)
    parser.add_argument("--tls-proxy", type=Path, required=True)
    parser.add_argument("--postgres-container", default="papo_postgres")
    parser.add_argument("--binary", type=Path, default=ROOT / "build/papo-tui")
    parser.add_argument("--output", type=Path, default=ROOT / "build/webhooks-validation")
    args = parser.parse_args()
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=True, mode=0o700)
    checks = {}
    stage = "setup"
    try:
        previous = sys.argv[:]
        sys.path.insert(0, str(ROOT / "tests"))
        sys.argv = [sys.argv[0], str(args.binary.resolve()), "ffmpeg"]
        try:
            import test_media_terminal as media
            from test_terminal import Terminal
        finally:
            sys.argv = previous
        media.MediaTerminal.setUpClass()
        with ExitStack() as stack:
            stack.callback(media.MediaTerminal.tearDownClass)
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
            sql = access[0]
            # Older development schemas may predate PR #26. Only this temporary
            # database receives the confirmed backend migrations.
            if not sql("SELECT to_regclass('public.webhooks');").strip():
                sql(
                    (args.backend_source / "migrations/014_external_message_attribution.sql")
                    .read_text()
                    .split("-- +goose Down")[0]
                )
            if (
                sql(
                    "SELECT count(*) FROM information_schema.columns WHERE table_name='webhooks' AND column_name='avatar_media';"
                ).strip()
                == "0"
            ):
                sql(
                    (args.backend_source / "migrations/015_webhook_identity.sql")
                    .read_text()
                    .split("-- +goose Down")[0]
                )
            seed()
            channel = str(
                UUID(
                    sql(
                        "INSERT INTO channels(name,type,position) VALUES('webhook-fixture','text',1) RETURNING id;"
                    ).strip()
                )
            )
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
            next_request = 0

            def call(method, path, status=200, **kwargs):
                nonlocal next_request
                delay = next_request - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                response = session.request(
                    method, secure + path, timeout=20, allow_redirects=False, **kwargs
                )
                next_request = time.monotonic() + 0.2
                if response.status_code != status:
                    raise RuntimeError("Unexpected backend status")
                return response.json() if response.content else None

            stage = "synthetic account and webhook"
            username, password = (
                "webhook_" + secrets.token_hex(4),
                "Webhook!" + secrets.token_hex(12),
            )
            call("POST", "/auth/register", 201, json={"username": username, "password": password})
            call("POST", "/auth/login", json={"username": username, "password": password})
            user = str(UUID(call("GET", "/auth/whoami")["id"]))
            sql("UPDATE servers SET owner_id='" + user + "';")
            webhook = call(
                "POST", "/webhooks", 201, json={"channel_id": channel, "name": "Build App"}
            )
            wid = str(UUID(webhook["id"]))
            token = webhook["token"]
            call(
                "PATCH",
                "/webhooks/" + wid,
                json={
                    "display_name": "Release App",
                    "avatar": base64.b64encode(media.MediaTerminal.animated_gif).decode(),
                    "avatar_format": "GIF",
                },
            )
            headers = {"Authorization": "Bearer " + token}
            call(
                "POST",
                "/webhooks/" + wid + "/messages",
                201,
                headers=headers,
                files={"content": (None, "Webhook history fixture")},
            )
            page = call("GET", "/channels/" + channel + "/messages?order=desc")
            message = page["messages"][0]
            checks["rest_nullable_user_and_webhook_id"] = (
                message["author_id"] is None and message["webhook_id"] == wid
            )
            checks["rest_app_name_and_avatar_path"] = (
                message["external_author"]["name"] == "Release App"
                and message["external_author"]["avatar_url"] == "/webhooks/" + wid + "/avatar"
            )
            stage = "TUI REST header and animated app avatar"
            terminal = Terminal(
                environment={"CURL_CA_BUNDLE": str(cert), "NO_PROXY": "*"}, allow_local_http=False
            )
            try:
                terminal.stream = media.GraphicsStream(terminal.screen, include_icons=True)
                query = re.search(rb"\x1b_Gi=(\d+),s=1,v=1,a=q", terminal.output)
                if not query:
                    raise RuntimeError("No graphics probe")
                terminal.send("\x1b_Gi=" + query[1].decode() + ";OK\x1b\\\x1b[?62;22c\x1b[6;16;8t")
                terminal.send(secure + "\t" + username + "\t" + password + "\n")

                def wait(predicate):
                    deadline = time.monotonic() + 12
                    while not predicate() and time.monotonic() < deadline:
                        terminal.read(0.03)
                    if not predicate():
                        raise RuntimeError("Expected product state absent")

                def screen():
                    return "\n".join(terminal.screen.display)

                wait(lambda: "webhook-fixture" in screen())
                terminal.resize(30, 100)
                terminal.send("\x1bOB\n")
                wait(
                    lambda: (
                        "Release App [APP]" in screen() and "Webhook history fixture" in screen()
                    )
                )
                checks["rest_header_name_and_app_badge"] = "Autor indisponível" not in screen()

                def animated():
                    frames = {}
                    for h, p in terminal.stream.kitty_frames:
                        if h.get(b"s") == b"48" and h.get(b"v") == b"48":
                            frames.setdefault(h[b"i"], set()).add(p)
                    return any(len(p) > 1 for p in frames.values())

                wait(animated)
                checks["app_avatar_animates"] = True
                wait(lambda: "Tempo real conectado" in screen())
                stage = "TUI WebSocket attribution"
                call(
                    "POST",
                    "/webhooks/" + wid + "/messages",
                    201,
                    headers=headers,
                    files={"content": (None, "Webhook live fixture")},
                )
                wait(lambda: "Webhook live fixture" in screen())
                checks["websocket_name_badge_and_content"] = (
                    "Release App [APP]" in screen() and "Autor indisponível" not in screen()
                )
                stage = "missing avatar fallback"
                call("PATCH", "/webhooks/" + wid, json={"avatar": ""})
                call(
                    "POST",
                    "/webhooks/" + wid + "/messages",
                    201,
                    headers=headers,
                    files={"content": (None, "Webhook without avatar")},
                )
                wait(lambda: "Webhook without avatar" in screen())
                checks["missing_avatar_keeps_name_and_badge"] = "Release App [APP]" in screen()
                checks["no_secrets_in_terminal"] = all(
                    value.encode() not in terminal.output
                    for value in (password, token, session.cookies.get("Auth"))
                    if value
                )
            finally:
                checks["terminal_restored"] = terminal.close()
        checks["isolated_cleanup_completed"] = True
        passed = bool(checks) and all(checks.values())
        private_write(work / "summary.json", {"passed": passed, "checks": checks})
        print(json.dumps({"passed": passed, "checks": len(checks)}))
        return 0 if passed else 1
    except Exception as error:
        private_write(
            work / "summary.json",
            {"passed": False, "stage": stage, "error_type": type(error).__name__, "checks": checks},
        )
        print(json.dumps({"passed": False, "stage": stage, "error_type": type(error).__name__}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
