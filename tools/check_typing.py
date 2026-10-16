"""Validate typing with the C TUI and a real Go backend in an isolated database.

Synthetic account credentials stay in RAM. Reports contain only checks/hashes.
The development database is copied read-only and temporary resources removed.
"""

import argparse
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path
import secrets
import ssl
import subprocess
import sys
import tempfile
import time
from uuid import UUID

import requests
import websocket
from tools.check_backend import private_write
from tools.check_r3 import ROOT, checked_run, isolated_backend, stop
from tools.check_r5 import fingerprint, source_fingerprint


def database_hash(container):
    dump = checked_run(
        [
            "docker",
            "exec",
            container,
            "sh",
            "-c",
            'exec pg_dump -U "$POSTGRES_USER" -d "$POSTGRES_DB" --no-owner --no-privileges',
        ],
        timeout=30,
    )
    # Recent pg_dump versions generate a random restriction token per dump.
    stable = "\n".join(
        line for line in dump.splitlines() if not line.startswith(("\\restrict ", "\\unrestrict "))
    )
    return hashlib.sha256(stable.encode()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend-binary", type=Path, required=True)
    parser.add_argument("--tls-proxy", type=Path, required=True)
    parser.add_argument("--tui", type=Path, default=ROOT / "build/papo-tui")
    parser.add_argument("--postgres-container", default="papo_postgres")
    parser.add_argument("--output", type=Path, default=ROOT / "build/typing-backend")
    args = parser.parse_args()
    work = args.output.resolve()
    work.mkdir(mode=0o700, parents=True, exist_ok=True)
    checks, access, phase, expected_state = {}, [], "setup", None
    try:
        before = database_hash(args.postgres_container)
        with ExitStack() as stack:
            base, seed, _visibility = stack.enter_context(
                isolated_backend(
                    args.backend_binary.resolve(),
                    args.postgres_container,
                    work,
                    on_ready=access.append,
                    label="r7",
                )
            )
            seed()
            sql = access[0]
            channel = str(
                UUID(
                    sql(
                        "INSERT INTO channels(name,type,position) VALUES('typing-geral','text',1) RETURNING id;"
                    ).strip()
                )
            )
            other = str(
                UUID(
                    sql(
                        "INSERT INTO channels(name,type,position) VALUES('typing-outro','text',2) RETURNING id;"
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
            proxy = subprocess.Popen(
                [
                    str(args.tls_proxy.resolve()),
                    "-upstream",
                    base,
                    "-cert",
                    str(cert),
                    "-key",
                    str(key),
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.DEVNULL,
                text=True,
            )
            stack.callback(proxy.stdout.close)
            stack.callback(stop, proxy)
            secure = proxy.stdout.readline().strip()
            if not secure.startswith("https://127.0.0.1:"):
                raise RuntimeError("TLS proxy unavailable")
            sessions = [stack.enter_context(requests.Session()) for _ in range(6)]
            password, usernames, identities = "Typing!" + secrets.token_hex(12) + "A", [], []
            next_request = 0

            def call(session, method, path, expected=200, **kwargs):
                nonlocal next_request
                delay = next_request - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
                response = session.request(
                    method, secure + path, timeout=10, allow_redirects=False, **kwargs
                )
                next_request = time.monotonic() + 0.2
                if response.status_code != expected:
                    raise RuntimeError("Unexpected backend status")
                return response.json() if response.content else None

            phase = "synthetic accounts"
            for n, session in enumerate(sessions):
                session.trust_env = False
                session.verify = str(cert)
                username = "typing_" + secrets.token_hex(4)
                usernames.append(username)
                registered = call(
                    session,
                    "POST",
                    "/auth/register",
                    201,
                    json=dict(username=username, password=password),
                )
                identities.append(str(UUID(registered["id"])))
                call(
                    session, "POST", "/auth/login", json=dict(username=username, password=password)
                )
                call(
                    session,
                    "PUT",
                    "/users/" + identities[-1],
                    json=dict(
                        nickname=f"Pessoa{n + 1}",
                        status="",
                        description="",
                        typing="está preparando uma resposta" if n == 0 else "",
                    ),
                )
            sockets = []
            for session in sessions[:5]:
                connection = websocket.create_connection(
                    secure.replace("https:", "wss:") + "/ws",
                    timeout=2,
                    cookie="Auth=" + session.cookies.get("Auth"),
                    suppress_origin=True,
                    sslopt=dict(ca_certs=str(cert), cert_reqs=ssl.CERT_REQUIRED),
                )
                stack.callback(connection.close)
                sockets.append(connection)
            sys.path.insert(0, str(ROOT / "tests"))
            previous = sys.argv[:]
            sys.argv = [sys.argv[0], str(args.tui.resolve())]
            try:
                import test_terminal
            finally:
                sys.argv = previous
            test_terminal.BINARY = str(args.tui.resolve())
            terminal = test_terminal.Terminal(
                environment=dict(
                    TERM="xterm-256color", LC_ALL="C.UTF-8", NO_PROXY="*", CURL_CA_BUNDLE=str(cert)
                ),
                allow_local_http=False,
            )
            closed = False

            def close_terminal():
                nonlocal closed
                if closed:
                    return True
                closed = True
                return terminal.close()

            stack.callback(close_terminal)

            def wait(text, *, absent=False, seconds=10):
                nonlocal expected_state
                expected_state = text
                deadline = time.monotonic() + seconds
                while (
                    text in "\n".join(terminal.screen.display)
                ) == absent and time.monotonic() < deadline:
                    terminal.read(0.03)
                visible = "\n".join(terminal.screen.display)
                if (text in visible) == absent:
                    if password not in visible and all(
                        session.cookies.get("Auth") not in visible for session in sessions
                    ):
                        private_write(
                            work / "failure-screen.json", dict(screen=terminal.screen.display)
                        )
                    raise RuntimeError("Expected typing state absent")

            def typing(n=0, selected=channel):
                sockets[n].send(json.dumps(dict(type="typing", channel_id=selected)))

            phase = "real typing delivery"
            terminal.send(secure + "\t" + usernames[-1] + "\t" + password + "\n")
            wait("typing-geral")
            terminal.resize(24, 180)
            terminal.send("\x1bOB\n")
            wait("# typing-geral · Histórico")
            wait("Tempo real conectado")
            typing(selected=other)
            terminal.read(0.3)
            checks["inactive_channel_ignored"] = "digitando" not in "\n".join(
                terminal.screen.display
            )
            typing()
            wait("Pessoa1 está preparando uma resposta")
            checks["rest_custom_phrase_for_existing_online_user"] = True
            for n in range(1, 4):
                typing(n)
            # Independent WS clients can be dispatched in any arrival order.
            wait("estão digitando…")
            for n in range(4):
                wait(f"Pessoa{n + 1}")
            checks["four_named_typers"] = True
            typing(4)
            wait("Várias pessoas estão digitando…")
            checks["five_generic_typers"] = True
            for connection in sockets[1:]:
                connection.close()
            wait("Pessoa1 está preparando uma resposta")
            checks["offline_removes_typers"] = True
            call(
                sessions[0],
                "PUT",
                "/users/" + identities[0],
                json=dict(nickname="Álice", status="", description="", typing="escreve com calma"),
            )
            wait("Álice escreve com calma")
            checks["live_custom_phrase_and_nickname"] = True
            typing()
            wait("Álice escreve com calma")
            wait("escreve com calma", absent=True, seconds=8)
            checks["silence_expires_indicator"] = True
            terminal.send("rascunho")
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                event = json.loads(sockets[0].recv())
                if event.get("type") == "typing" and event.get("user_id") == identities[-1]:
                    break
            else:
                raise RuntimeError("Outbound typing absent")
            checks["tui_edits_send_authenticated_typing"] = (
                event.get("channel_id") == channel and event.get("is_typing") is True
            )
            checks["self_typing_hidden"] = "Pessoa6" not in "\n".join(terminal.screen.display)
            typing()
            wait("Álice escreve com calma")
            call(
                sessions[0],
                "POST",
                "/messages",
                201,
                files={"channel_id": (None, channel), "content": (None, "Resposta pronta")},
            )
            wait("Resposta pronta")
            wait("escreve com calma", absent=True)
            checks["message_clears_typing_and_preserves_draft"] = "rascunho" in "\n".join(
                terminal.screen.display
            )
            checks["terminal_restored"] = close_terminal()
        checks["source_database_unchanged"] = before == database_hash(args.postgres_container)
        report = dict(
            passed=all(checks.values()),
            kind="actual-go-backend-isolated-database",
            checks=checks,
            temporary_resources_removed=True,
            tui_sha256=fingerprint(args.tui.resolve()),
            client_source_sha256=source_fingerprint(),
            backend_sha256=fingerprint(args.backend_binary.resolve()),
            harness_sha256=fingerprint(Path(__file__)),
        )
    except (
        RuntimeError,
        OSError,
        ValueError,
        subprocess.TimeoutExpired,
        requests.RequestException,
        websocket.WebSocketException,
    ) as failure:
        report = dict(
            passed=False,
            phase=phase,
            checks=checks,
            failure_class=type(failure).__name__,
            expected_state=expected_state,
        )
    private_write(work / "summary.json", report)
    print(
        json.dumps(
            dict(passed=report["passed"], checks=len(checks), report=str(work / "summary.json"))
        )
    )
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
