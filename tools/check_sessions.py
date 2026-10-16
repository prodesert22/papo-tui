"""Validate persistent sessions against a Go backend and an isolated test wallet.

Run under dbus-run-session --config-file=tests/support/keyring-bus.conf. Accounts,
database, TLS keys and desktop storage are disposable; credentials stay in RAM.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile
import time
from uuid import UUID

import requests
from tools.check_backend import private_write
from tools.check_r3 import checked_run, isolated_backend, stop
from tools.check_r5 import source_fingerprint


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend-binary", type=Path, required=True)
    parser.add_argument("--tls-proxy", type=Path, required=True)
    parser.add_argument("--tui", type=Path, default=Path("build/papo-tui"))
    parser.add_argument("--keyring", type=Path, default=Path("build/fake-keyring"))
    parser.add_argument("--postgres-container", default="papo_postgres")
    parser.add_argument("--work-dir", type=Path, default=Path("build/session-validation"))
    args = parser.parse_args()
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True, mode=0o700)
    checks = {}
    proxy = provider = terminal = None
    previous = sys.argv[:]
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
    sys.argv = [sys.argv[0], str(args.tui.resolve())]
    try:
        from test_terminal import Terminal
    finally:
        sys.argv = previous
    try:
        access = []
        with isolated_backend(
            args.backend_binary.resolve(),
            args.postgres_container,
            work,
            on_ready=access.append,
            label="r6",
        ) as (base, seed, _visibility):
            seed()
            sql = access[0]
            sql("INSERT INTO channels(name,type,position) VALUES('sessao-real','text',1);")
            with tempfile.TemporaryDirectory(prefix="session-", dir=work) as directory:
                root = Path(directory)
                cert, key = root / "cert.pem", root / "key.pem"
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
                secure = proxy.stdout.readline().strip()
                if not secure.startswith("https://127.0.0.1:"):
                    raise RuntimeError("TLS proxy unavailable")
                environment = dict(
                    HOME=str(root),
                    XDG_CONFIG_HOME=str(root / "config"),
                    XDG_STATE_HOME=str(root / "state"),
                    CURL_CA_BUNDLE=str(cert),
                    NO_PROXY="*",
                    ALL_PROXY="",
                    HTTP_PROXY="",
                    HTTPS_PROXY="",
                    DBUS_SESSION_BUS_ADDRESS=os.environ["DBUS_SESSION_BUS_ADDRESS"],
                )
                provider = subprocess.Popen(
                    [str(args.keyring.resolve()), "wallet6"],
                    env=dict(os.environ, **environment),
                    stdout=subprocess.PIPE,
                    stderr=subprocess.DEVNULL,
                )
                if provider.stdout.readline() != b"ready\n":
                    raise RuntimeError("Private wallet unavailable")
                username = "session_" + secrets.token_hex(4)
                password = "Synthetic!" + secrets.token_hex(12) + "A"
                with requests.Session() as http:
                    http.trust_env = False
                    http.verify = str(cert)
                    response = http.post(
                        secure + "/auth/register",
                        json={"username": username, "password": password},
                        timeout=10,
                    )
                    if response.status_code != 201:
                        raise RuntimeError("Synthetic registration failed")
                    identity = str(UUID(response.json()["id"]))
                time.sleep(0.2)
                query = "SELECT count(*) FROM user_connections WHERE user_id='" + identity + "';"

                def count():
                    return int(sql(query).strip())

                def state():
                    files = list((root / "state/papo-tui").glob("session-*.json"))
                    return json.loads(files[0].read_text()) if files else {}

                def wait(predicate, seconds=12):
                    deadline = time.monotonic() + seconds
                    while not predicate() and time.monotonic() < deadline:
                        terminal.read(0.03)
                    if not predicate():
                        raise RuntimeError("Session validation did not reach expected state")

                def screen():
                    return "\n".join(terminal.screen.display)

                def open_tui():
                    return Terminal(environment=environment, allow_local_http=False)

                def close_tui():
                    nonlocal terminal
                    checks["terminal_restored"] = terminal.close() and checks.get(
                        "terminal_restored", True
                    )
                    terminal = None

                terminal = open_tui()
                terminal.send(secure + "\t" + username + "\t" + password + "\n")
                wait(lambda: "sessao-real" in screen() and state().get("restore_allowed"))
                checks["real_https_login_and_secure_storage"] = count() == 1
                checks["no_history_prefetch"] = (
                    sql("SELECT count(*) FROM user_channel_state;").strip() == "0"
                )
                checks["password_absent_from_terminal"] = password.encode() not in terminal.output
                close_tui()
                terminal = open_tui()
                wait(lambda: "sessao-real" in screen() and state().get("restore_allowed"))
                checks["restart_rest_validates_without_new_login"] = count() == 1
                # The first scheduled rotation after authentication is automatic.
                wait(lambda: count() > 1, seconds=38)
                wait(lambda: state().get("restore_allowed"))
                rotated = count()
                checks["automatic_real_rotation_persisted"] = rotated > 1
                close_tui()
                terminal = open_tui()
                wait(lambda: "sessao-real" in screen() and state().get("restore_allowed"))
                checks["restart_uses_rotated_cookie_without_login"] = count() == rotated
                close_tui()
                sql("DELETE FROM user_connections WHERE user_id='" + identity + "';")
                terminal = open_tui()
                wait(lambda: "Entre novamente" in screen())
                wait(lambda: not state().get("restore_allowed"))
                checks["revoked_real_connection_returns_to_login"] = (
                    "URL do servidor" in screen() and "sessao-real" not in screen()
                )
                checks["public_state_has_no_credentials"] = all(
                    password.encode() not in p.read_bytes() and b"Auth=" not in p.read_bytes()
                    for p in list((root / "config").rglob("*")) + list((root / "state").rglob("*"))
                    if p.is_file()
                )
                close_tui()
                stop(proxy)
                proxy.stdout.close()
                proxy = None
                provider.terminate()
                provider.wait(timeout=5)
                provider.stdout.close()
                provider = None
        report = dict(
            kind="actual-go-backend-isolated-database-and-simulated-private-wallet",
            checks=checks,
            passed=bool(checks) and all(checks.values()),
            source_database_mutated=False,
            temporary_resources_removed=True,
            client_source_sha256=source_fingerprint(),
        )
        for name in ("backend_binary", "tls_proxy", "tui", "keyring"):
            report[name + "_sha256"] = hashlib.sha256(getattr(args, name).read_bytes()).hexdigest()
        private_write(work / "backend.json", report)
        print(json.dumps(report))
        return 0 if report["passed"] else 1
    except (
        RuntimeError,
        OSError,
        ValueError,
        KeyError,
        subprocess.TimeoutExpired,
        requests.RequestException,
    ):
        private_write(
            work / "backend.json",
            dict(
                passed=False, checks=checks, error="Session validation failed; credentials omitted"
            ),
        )
        print("Session validation failed; credentials omitted", file=sys.stderr)
        return 1
    finally:
        if terminal and terminal.process.poll() is None:
            terminal.close()
        stop(proxy)
        if provider:
            provider.terminate()
            provider.wait(timeout=5)


if __name__ == "__main__":
    raise SystemExit(main())
