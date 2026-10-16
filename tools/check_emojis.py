"""Exercise emoji/avatar UI against the Go backend in a disposable database.

All uploads, accounts and messages are synthetic and isolated. Passwords and
cookies remain in memory; the report contains fixed stages and boolean checks.
"""

import argparse
import base64
from contextlib import ExitStack
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
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--backend-binary", type=Path, required=True)
    p.add_argument("--tls-proxy", type=Path, required=True)
    p.add_argument("--postgres-container", default="papo_postgres")
    p.add_argument("--binary", type=Path, default=ROOT / "build/papo-tui")
    p.add_argument("--output", type=Path, default=ROOT / "build/emojis-validation")
    args = p.parse_args()
    work = args.output.resolve()
    work.mkdir(parents=True, exist_ok=True, mode=0o700)
    checks = {}
    stage = "setup"
    last_status = None
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
            seed()
            sql = access[0]
            channel = str(
                UUID(
                    sql(
                        "INSERT INTO channels(name,type,position) VALUES('emoji-fixture','text',1) RETURNING id;"
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
                nonlocal next_request, last_status
                delay = next_request - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                response = session.request(
                    method, secure + path, timeout=20, allow_redirects=False, **kwargs
                )
                next_request = time.monotonic() + 0.2
                last_status = response.status_code
                if response.status_code != status:
                    raise RuntimeError("Unexpected backend status")
                return response.json() if response.content else None

            stage = "synthetic account"
            username = "emoji_" + secrets.token_hex(4)
            password = "Emoji!" + secrets.token_hex(12)
            call("POST", "/auth/register", 201, json={"username": username, "password": password})
            call("POST", "/auth/login", json={"username": username, "password": password})
            identity = call("GET", "/auth/whoami")
            user = str(UUID(identity["id"]))
            sql("UPDATE servers SET owner_id='" + user + "';")
            stage = "avatar and emoji fixtures"
            gif = base64.b64encode(media.MediaTerminal.animated_gif).decode()
            call("PUT", "/users/" + user + "/avatar", json={"avatar": gif, "avatar_format": "GIF"})
            emoji = call(
                "POST",
                "/emojis",
                201,
                json={"name": "papo_gato", "format": "GIF", "image_blob": gif},
            )
            profiles = call("POST", "/users/profile_batch", json={"ids": [user]})
            checks["profile_batch_contains_base64_avatar"] = bool(
                profiles["profiles"][0]["avatar_blob"]
            )
            listed = call("GET", "/emojis?order=asc")
            checks["emoji_catalogue_contains_base64"] = any(
                e["id"] == emoji["id"] and e["image_blob"] for e in listed["emojis"]
            )
            call(
                "POST",
                "/messages",
                201,
                files={"channel_id": (None, channel), "content": (None, "Emojis 😄 :papo_gato:")},
            )
            stage = "real TUI through TLS and Kitty protocol"
            terminal = Terminal(
                environment={"CURL_CA_BUNDLE": str(cert), "NO_PROXY": "*"}, allow_local_http=False
            )
            try:
                terminal.stream = media.GraphicsStream(terminal.screen)
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
                        raise RuntimeError("Product state absent")

                def screen():
                    return "\n".join(terminal.screen.display)

                wait(lambda: "emoji-fixture" in screen())
                terminal.resize(30, 100)
                terminal.send("\x1bOB\n")
                wait(lambda: "Emojis" in screen() and "Histórico" in screen())
                wait(lambda: len(terminal.stream.kitty_frames) > 5)
                variants = {}
                for header, pixels in terminal.stream.kitty_frames:
                    variants.setdefault((header[b"i"], header[b"s"], header[b"v"]), set()).add(
                        pixels
                    )
                checks["actual_avatar_is_animated"] = any(
                    w == b"48" and h == b"48" and len(pixels) > 1
                    for (_, w, h), pixels in variants.items()
                )
                checks["actual_custom_emoji_is_animated"] = any(
                    w == b"16" and h == b"16" and len(pixels) > 1
                    for (_, w, h), pixels in variants.items()
                )
                terminal.send("\x1bOS\t\x1bOC")
                wait(lambda: "1 emojis do servidor" in screen())
                checks["server_picker_loads_actual_catalogue"] = "papo_gato" in screen()
                terminal.send("\t\n")
                wait(lambda: "Emojis · Esc voltar" not in screen())
                before = call("GET", "/channels/" + channel + "/messages")
                checks["choosing_emoji_does_not_send"] = len(before["messages"]) == 1
                terminal.send("\n")
                wait(lambda: "Mensagem confirmada" in screen())
                messages = call("GET", "/channels/" + channel + "/messages")
                checks["custom_token_round_trips"] = any(
                    m["content"] == ":papo_gato:" for m in messages["messages"]
                )
                stage = "live avatar update"
                start = len(terminal.stream.kitty_frames)
                png = base64.b64encode(media.MediaTerminal.images["png"]).decode()
                call(
                    "PUT",
                    "/users/" + user + "/avatar",
                    json={"avatar": png, "avatar_format": "PNG"},
                )
                wait(
                    lambda: any(
                        h.get(b"s") == b"48" and h.get(b"v") == b"48"
                        for h, _ in terminal.stream.kitty_frames[start:]
                    )
                )
                checks["avatar_update_refreshes_visible_picture"] = True
                checks["password_not_in_output"] = password.encode() not in terminal.output
            finally:
                checks["terminal_restored"] = terminal.close()
        report = {
            "passed": all(checks.values()),
            "kind": "actual-go-backend-isolated-database",
            "checks": checks,
            "source_database_mutated": False,
            "temporary_resources_removed": True,
            "terminal_pixel_evidence": "PTY protocol responder; not native emulator pixel composition",
        }
    except Exception:
        report = {
            "passed": False,
            "stage": stage,
            "http_status": last_status,
            "checks": checks,
            "error": "Validation failed; credentials omitted",
        }
    private_write(work / "summary.json", report)
    print(json.dumps(report, ensure_ascii=False))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
