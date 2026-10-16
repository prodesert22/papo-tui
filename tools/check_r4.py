"""Exercise R4 with the C client and an isolated copy of the real backend.

Creates synthetic accounts only in a temporary database, never in the source.
Credentials and cookies stay in memory; the report contains boolean evidence.
"""

import argparse
import json
import os
from pathlib import Path
import secrets
import select
import subprocess
import sys
import tempfile
import time

from tools.check_backend import private_write
from tools.check_r3 import ROOT, checked_run, isolated_backend, stop


def environment(username, password, server_password="", ca=None):
    result = dict(
        os.environ,
        LC_ALL="C.UTF-8",
        NO_PROXY="*",
        PAPO_PROBE_USERNAME=username,
        PAPO_PROBE_PASSWORD=password,
        PAPO_PROBE_SERVER_PASSWORD=server_password,
    )
    for key in (
        "ALL_PROXY",
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "http_proxy",
        "https_proxy",
        "CURL_CA_BUNDLE",
    ):
        result.pop(key, None)
    if ca:
        result["CURL_CA_BUNDLE"] = str(ca)
    return result


def probe(binary, base, mode, env):
    result = json.loads(checked_run([str(binary), base, mode], env=env, timeout=25))
    time.sleep(0.8)
    return result


def controlled(binary, base, env, change):
    process = subprocess.Popen(
        [str(binary), base, "controlled-refresh"],
        env=env,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
    )
    try:
        if not select.select([process.stdout], [], [], 10)[0]:
            raise RuntimeError("Control probe timed out")
        if json.loads(process.stdout.readline()) != {"awaiting_control": True}:
            raise RuntimeError("Control probe did not authenticate")
        change()
        output, _ = process.communicate("continue\n", timeout=20)
        if process.returncode:
            raise RuntimeError("Control probe failed")
        result = json.loads(output)
        time.sleep(0.8)
        return result
    finally:
        stop(process)
        for stream in (process.stdin, process.stdout):
            if stream and not stream.closed:
                stream.close()


def terminal_checks(binary, base, username, password, ca, work, session_count):
    previous_arguments = sys.argv[:]
    sys.path.insert(0, str(ROOT / "tests"))
    sys.argv = [sys.argv[0], str(binary)]
    try:
        from test_terminal import Terminal
    finally:
        sys.argv = previous_arguments
    checks, terminal = {}, None
    with tempfile.TemporaryDirectory(prefix="terminal-", dir=work) as directory:
        env = dict(environment(username, password, ca=ca), XDG_CONFIG_HOME=directory)

        def wait(text):
            deadline = time.monotonic() + 8
            while text not in "\n".join(terminal.screen.display) and time.monotonic() < deadline:
                terminal.read(0.03)
            if text not in "\n".join(terminal.screen.display):
                raise RuntimeError("R4 terminal did not reach the expected screen")

        try:
            terminal = Terminal(environment=env)
            new_username = "r4c_" + secrets.token_hex(4)
            terminal.send("\x0e" + base + "\t" + new_username + "\t" + password + "\tWrong!123\n")
            wait("não coincidem")
            checks["tui_confirmation_blocks_mismatch"] = True
            terminal.send("\x15" + password + "\n")
            wait("Conta criada")
            checks["tui_real_registration_requires_login"] = "Confirmar senha" not in "\n".join(
                terminal.screen.display
            )
            terminal.send(password + "\n")
            terminal.return_to_account()
            wait("Login realizado")
            wait("Tempo real conectado")
            checks["tui_new_account_explicit_login"] = True
            terminal.send("\n")
            wait("Você saiu da conta")
            terminal.send("\x1bOA\x15" + username + "\t" + password + "\n")
            terminal.return_to_account()
            wait("Login realizado")
            wait("Tempo real conectado")
            checks["tui_real_rest_and_wss"] = True
            before = session_count()
            terminal.send("\x12")
            deadline = time.monotonic() + 5
            while session_count() <= before and time.monotonic() < deadline:
                terminal.read(0.1)
            wait("Login realizado")
            checks["tui_manual_real_renewal"] = session_count() > before
            checks["tui_password_masked"] = password.encode() not in terminal.output
            checks["tui_first_terminal_restored"] = terminal.close()
            terminal = None
            terminal = Terminal(environment=env)
            wait("URL do servidor")
            checks["restart_requires_login"] = (
                "[ Entrar ]" in "\n".join(terminal.screen.display)
                and not terminal.screen.cursor.hidden
            )
            checks["no_local_session_file"] = not any(
                path.is_file() for path in Path(directory).rglob("*")
            )
            checks["tui_second_terminal_restored"] = terminal.close()
            terminal = None
        finally:
            if terminal:
                terminal.close(exit_key="\x03")
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--backend-binary", type=Path, required=True)
    parser.add_argument("--tls-proxy", type=Path, required=True)
    parser.add_argument("--probe", type=Path, default=ROOT / "build/session-probe")
    parser.add_argument("--tui", type=Path, default=ROOT / "build/papo-tui")
    parser.add_argument("--postgres-container", default="papo_postgres")
    parser.add_argument("--output", type=Path, default=ROOT / "build/r4/validation")
    args = parser.parse_args()
    work = args.output.resolve()
    work.mkdir(mode=0o700, parents=True, exist_ok=True)
    checks, access, proxy = {}, [], None
    username_a, username_b = "r4a_" + secrets.token_hex(4), "r4b_" + secrets.token_hex(4)
    password = "R4!" + secrets.token_hex(12)
    try:
        with isolated_backend(
            args.backend_binary.resolve(),
            args.postgres_container,
            work,
            on_ready=access.append,
            label="r4",
        ) as (base, seed, visibility):
            seed()
            sql = access[0]
            with tempfile.TemporaryDirectory(prefix="tls-", dir=work) as directory:
                cert, key = Path(directory) / "cert.pem", Path(directory) / "key.pem"
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
                    raise RuntimeError("TLS proxy did not become ready")
                env_a = environment(username_a, password, ca=cert)
                registered = probe(args.probe, secure, "register", env_a)
                checks["public_registration_without_auto_login"] = (
                    registered["registered"] and not registered["session"]
                )
                result = probe(args.probe, secure, "ws-refresh", env_a)
                checks["public_login_identity_and_wss"] = result["initial"] and result["ws_open"]
                checks["real_cookie_rotation_and_whoami"] = (
                    result["session"] and result["concurrent_blocked"]
                )
                identity_a = result["identity"]
                checks["untrusted_tls_rejected"] = not probe(
                    args.probe, secure, "login", environment(username_a, password)
                )["session"]
                checks["wrong_tls_hostname_rejected"] = not probe(
                    args.probe, secure.replace("127.0.0.1", "localhost"), "login", env_a
                )["session"]
                checks["real_authenticated_get"] = (
                    probe(args.probe, secure, "whoami", env_a)["status"] == 200
                )
                result = probe(args.probe, secure, "logout", env_a)
                checks["real_logout_clears_identity"] = (
                    result["logout_started"] and not result["session"] and not result["identity"]
                )
                result = probe(
                    args.probe, secure, "login", environment(username_a, "Invalid!123", ca=cert)
                )
                checks["invalid_account_password"] = (
                    not result["session"] and result["input_error"] == 3
                )
                sql(
                    "UPDATE servers SET public_server=false,password_hash=(SELECT password_hash FROM users WHERE username='"
                    + username_a
                    + "');"
                )
                env_b = environment(username_b, password, password, cert)
                result = probe(args.probe, secure, "register-login", env_b)
                checks["private_registration_and_explicit_login"] = (
                    result["registered"] and result["session"]
                )
                checks["accounts_do_not_share_identity"] = result[
                    "identity"
                ] != identity_a and bool(result["identity"])
                result = probe(
                    args.probe,
                    secure,
                    "login",
                    environment(username_b, password, "Wrong!123", cert),
                )
                checks["invalid_private_password_focus"] = (
                    not result["session"] and result["input_error"] == 4
                )
                visibility(True)
                result = controlled(
                    args.probe,
                    secure,
                    env_a,
                    lambda: sql(
                        "UPDATE user_connections SET replaced_at=now()-interval '61 seconds' WHERE user_id='"
                        + identity_a
                        + "';"
                    ),
                )
                checks["real_revoked_session_returns_login"] = (
                    result["initial"] and not result["session"] and not result["identity"]
                )
                result = controlled(
                    args.probe,
                    secure,
                    env_a,
                    lambda: sql(
                        "UPDATE users SET connection_violation=true WHERE id='" + identity_a + "';"
                    ),
                )
                checks["real_connection_violation_returns_login"] = (
                    result["initial"] and not result["session"]
                )
                sql("UPDATE users SET connection_violation=false WHERE id='" + identity_a + "';")
                result = controlled(
                    args.probe,
                    secure,
                    env_a,
                    lambda: sql("UPDATE users SET banned=true WHERE id='" + identity_a + "';"),
                )
                checks["real_ban_blocks_renewal"] = result["initial"] and not result["session"]
                sql("UPDATE users SET banned=false WHERE id='" + identity_a + "';")
                checks.update(
                    terminal_checks(
                        args.tui,
                        secure,
                        username_a,
                        password,
                        cert,
                        work,
                        lambda: int(
                            sql(
                                "SELECT count(*) FROM user_connections WHERE user_id='"
                                + identity_a
                                + "';"
                            ).strip()
                        ),
                    )
                )
                stop(proxy)
                proxy.stdout.close()
                proxy = None
        report = {
            "kind": "actual-backend-isolated-database",
            "checks": checks,
            "passed": all(checks.values()),
            "source_database_mutated": False,
            "temporary_accounts_database_and_tls_removed": True,
        }
        private_write(work / "summary.json", report)
        print(json.dumps(report, ensure_ascii=False))
        return 0 if report["passed"] else 1
    except (RuntimeError, OSError, ValueError, subprocess.TimeoutExpired):
        private_write(
            work / "summary.json",
            {
                "passed": False,
                "checks": checks,
                "error": "R4 validation failed; credentials omitted",
            },
        )
        print("R4 validation failed; credentials omitted", file=sys.stderr)
        return 1
    finally:
        stop(proxy)
        if proxy and proxy.stdout:
            proxy.stdout.close()


if __name__ == "__main__":
    raise SystemExit(main())
