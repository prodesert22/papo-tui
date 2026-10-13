"""Demonstrate R3 with the C client and an actual backend, without account login.

An optional existing backend binary runs against a temporary PostgreSQL database.
Only existing account/server rows are copied; the source database is read only.
All temporary database, process, configuration and TLS resources are removed.
"""
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import secrets
import signal
import socket
import subprocess
import sys
import tempfile
import time
from urllib.parse import quote, urlsplit

import requests

from tools.check_backend import private_write

ROOT = Path(__file__).resolve().parents[1]


def checked_run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if result.returncode: raise RuntimeError("Development operation failed; no diagnostic credentials were recorded")
    return result.stdout


def stop(process):
    if process is not None and process.poll() is None:
        process.send_signal(signal.SIGINT)
        try: process.wait(timeout=12)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)


@contextmanager
def isolated_backend(binary, container, work):
    details = json.loads(checked_run(["docker", "inspect", container], timeout=10))[0]
    settings = dict(item.split("=", 1) for item in details["Config"]["Env"] if "=" in item)
    source = settings.get("POSTGRES_DB", settings["POSTGRES_USER"])
    database = "papo_r3_" + secrets.token_hex(8)
    ports = details["NetworkSettings"]["Ports"].get("5432/tcp")
    if not ports: raise RuntimeError("PostgreSQL needs a published local development port")

    def sql(statement, selected_database=database):
        return checked_run(["docker", "exec", "-i", container, "sh", "-c",
                            'exec psql -X -q -v ON_ERROR_STOP=1 -U "$POSTGRES_USER" -d "$1" -At',
                            "papo-r3", selected_database], input=statement, timeout=30)

    created, process = False, None
    try:
        sql("CREATE DATABASE " + database + ";", "postgres")
        created = True
        schema = checked_run(["docker", "exec", container, "sh", "-c",
                              'exec pg_dump -U "$POSTGRES_USER" -d "$POSTGRES_DB" --schema-only --no-owner --no-privileges'], timeout=30)
        sql(schema)
        with tempfile.TemporaryDirectory(prefix="backend-", dir=work) as directory:
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", 0))
                port = listener.getsockname()[1]
            base = f"http://127.0.0.1:{port}"
            dsn = ("postgres://" + quote(settings["POSTGRES_USER"], safe="") + ":" +
                   quote(settings.get("POSTGRES_PASSWORD", ""), safe="") +
                   f"@127.0.0.1:{ports[0]['HostPort']}/{database}?sslmode=disable")
            environment = dict(os.environ, SERVER_PORT=str(port), DATABASE_URL=dsn,
                               JWT_SECRET=secrets.token_hex(32), HMAC_SECRET=secrets.token_hex(32),
                               BASE_URL=base, THUMBNAIL_ENABLED="false", LINK_PREVIEW_ENABLED="false",
                               MODERATION_ENABLED="false", USE_FCM_RELAY="false", VOICE_ICE_UDP_PORT="0",
                               TURN_URLS="", STUN_URLS="", CLOUDFLARE_PROXY="false", TRUSTED_PROXY_CIDRS="",
                               RATE_LIMIT="10", RATE_BURST="25")
            process = subprocess.Popen([str(binary)], env=environment, cwd=directory,
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            session = requests.Session()
            session.trust_env = False
            try:
                for _ in range(80):
                    if process.poll() is not None: raise RuntimeError("Isolated backend exited")
                    try:
                        if session.get(base + "/health", timeout=1, allow_redirects=False).status_code == 200: break
                    except requests.RequestException: pass
                    time.sleep(0.25)
                else: raise RuntimeError("Isolated backend did not become ready")
            finally: session.close()

            def seed():
                queries = {
                    "users": "SELECT (jsonb_populate_record(NULL::users,to_jsonb(u)||'{\"avatar_media\":null,\"banner_media\":null}'::jsonb)).* FROM users u WHERE u.id IN (SELECT owner_id FROM servers)",
                    "user_settings": "SELECT s.* FROM user_settings s WHERE s.user_id IN (SELECT owner_id FROM servers)",
                    "servers": "SELECT (jsonb_populate_record(NULL::servers,to_jsonb(s)||'{\"icon_media\":null}'::jsonb)).* FROM servers s",
                }
                for table, query in queries.items():
                    rows = sql("COPY (" + query + ") TO STDOUT;", source)
                    sql("COPY " + table + " FROM STDIN;\n" + rows + "\\.\n")
                sql("UPDATE servers SET public_server=true, password_hash=NULL;")
                if sql("SELECT count(*) FROM servers;").strip() != "1":
                    raise RuntimeError("The existing development server was not found")

            def visibility(public):
                sql("UPDATE servers SET public_server=" + ("true, password_hash=NULL" if public else
                    "false, password_hash=(SELECT password_hash FROM users LIMIT 1)") + ";")

            yield base, seed, visibility
            stop(process)
            process = None
    finally:
        stop(process)
        if created: sql("DROP DATABASE " + database + " WITH (FORCE);", "postgres")


def probe(binary, base, *, ca=None):
    environment = dict(os.environ, NO_PROXY="*")
    for key in ("ALL_PROXY", "HTTP_PROXY", "HTTPS_PROXY", "http_proxy", "https_proxy", "CURL_CA_BUNDLE"):
        environment.pop(key, None)
    if ca: environment["CURL_CA_BUNDLE"] = str(ca)
    command = [str(binary), "discover", base]
    if urlsplit(base).scheme == "http": command.append("allow")
    result = json.loads(checked_run(command, env=environment, timeout=25))
    time.sleep(0.25)
    return result


def demonstrate_tui(binary, base, public, work):
    # Reuse the existing PTY observer; it consumes the executable argument.
    previous_arguments = sys.argv[:]
    sys.path.insert(0, str(ROOT / "tests"))
    sys.argv = [sys.argv[0], str(binary)]
    try:
        from test_terminal import Terminal
    finally: sys.argv = previous_arguments
    terminal = None
    checks = {}

    def screen(): return "\n".join(terminal.screen.display)

    def wait(text, absent=False):
        for _ in range(150):
            if (text in screen()) != absent: return
            terminal.read(0.03)
        raise RuntimeError("R3 terminal demonstration did not reach the expected screen")

    with tempfile.TemporaryDirectory(prefix="profiles-", dir=work) as directory:
        environment = dict(XDG_CONFIG_HOME=directory, NO_PROXY="*", ALL_PROXY="", HTTP_PROXY="", HTTPS_PROXY="")
        path = Path(directory) / "papo-tui" / "config.json"
        try:
            terminal = Terminal(("--profiles",), environment=environment)
            terminal.send("\x0eInstância real\t" + base + "\n\n")
            wait("Pública" if public else "Privada")
            first_id = json.loads(path.read_text())["profiles"][0]["id"]
            terminal.send("\x0eInválida\thttp://127.0.0.1:9\n\n")
            wait("Conexão recusada")
            old_id = json.loads(path.read_text())["profiles"][1]["id"]
            terminal.send("\x05\x15Instância corrigida\t\x15" + base + "\n\n")
            wait("Pública" if public else "Privada")
            saved = json.loads(path.read_text())
            checks["rename_preserves_id"] = saved["profiles"][1]["id"] == old_id
            checks["invalid_url_corrected_without_restart"] = saved["profiles"][1]["url"] == base
            checks["first_profile_preserved"] = saved["profiles"][0]["id"] == first_id
            private_write(work / "backend-tui-wide.json", {"screen": terminal.screen.display})
            terminal.resize(14, 44)
            wait("Instância corrigida")
            private_write(work / "backend-tui-narrow.json", {"screen": terminal.screen.display})
            terminal.resize(24, 80)
            terminal.send("\n\n")
            wait("URL do servidor")
            checks["selected_profile_populates_login"] = base in screen()
            checks["terminal_restored_first_run"] = terminal.close()
            terminal = None
            terminal = Terminal(environment=environment)
            wait("URL do servidor")
            checks["restart_opens_login"] = "Selecione um perfil" not in screen()
            terminal.send("\x10")
            wait("Selecione um perfil")
            checks["profiles_reappear_after_restart"] = "Instância corrigida" in screen() and "Instância real" in screen()
            terminal.send("\n")
            wait("URL do servidor")
            checks["last_selection_survives_restart"] = "Instância corrigida" in screen()
            checks["only_public_configuration_persisted"] = (
                set(saved) == {"version", "profiles", "selected_profile", "requests_per_second", "request_burst"} and
                all(set(item) == {"id", "name", "url"} for item in saved["profiles"]))
            checks["terminal_restored_second_run"] = terminal.close()
            terminal = None
        finally:
            if terminal is not None: terminal.close(exit_key="\x03")
    return checks


def tls_checks(proxy_binary, base, binary, work):
    process = None
    with tempfile.TemporaryDirectory(prefix="tls-", dir=work) as directory:
        cert, key = Path(directory) / "cert.pem", Path(directory) / "key.pem"
        checked_run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                     "-keyout", str(key), "-out", str(cert), "-subj", "/CN=127.0.0.1",
                     "-addext", "subjectAltName=IP:127.0.0.1"], timeout=20)
        key.chmod(0o600)
        try:
            process = subprocess.Popen([str(proxy_binary), "-upstream", base, "-cert", str(cert), "-key", str(key)],
                                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
            secure_base = process.stdout.readline().strip()
            if not secure_base.startswith("https://127.0.0.1:"): raise RuntimeError("TLS proxy did not become ready")
            trusted = probe(binary, secure_base, ca=cert)
            untrusted = probe(binary, secure_base)
            wrong_host = probe(binary, secure_base.replace("127.0.0.1", "localhost"), ca=cert)
            return {"real_backend_https_discovery": trusted["identified"],
                    "untrusted_ca_rejected": not untrusted["identified"] and "segura" in untrusted["message"],
                    "wrong_hostname_rejected": not wrong_host["identified"] and "segura" in wrong_host["message"]}
        finally:
            stop(process)
            if process and process.stdout: process.stdout.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", default="http://127.0.0.1:8080")
    parser.add_argument("--probe", type=Path, default=ROOT / "build/profiles-probe")
    parser.add_argument("--binary", type=Path, default=ROOT / "build/papo-tui")
    parser.add_argument("--backend-binary", type=Path)
    parser.add_argument("--postgres-container", default="papo_postgres")
    parser.add_argument("--tls-proxy", type=Path)
    parser.add_argument("--report", type=Path, default=ROOT / "build/r3/backend.json")
    args = parser.parse_args()
    work = args.report.resolve().parent
    work.mkdir(parents=True, exist_ok=True, mode=0o700)
    report = {"observed_at": datetime.now(timezone.utc).isoformat(), "checks": {},
              "backend": "isolated real executable/database" if args.backend_binary else "provided development instance",
              "accounts_created": 0, "account_logins": 0, "source_database_mutations": 0}

    def verify(base):
        metadata = probe(args.probe.resolve(), base)
        report["checks"]["real_backend_discovered"] = metadata["identified"]
        if not metadata["identified"]: raise RuntimeError("Real backend discovery failed")
        report["checks"].update(demonstrate_tui(args.binary.resolve(), base, metadata["public"], work))
        if args.tls_proxy:
            report["checks"].update(tls_checks(args.tls_proxy.resolve(), base, args.probe.resolve(), work))

    try:
        if args.backend_binary:
            report["backend_binary_sha256"] = hashlib.sha256(args.backend_binary.read_bytes()).hexdigest()
            with isolated_backend(args.backend_binary.resolve(), args.postgres_container, work) as (base, seed, visibility):
                missing = probe(args.probe.resolve(), base)
                report["checks"]["real_uninitialized_backend_detected"] = not missing["identified"] and "não inicializada" in missing["message"]
                seed()
                verify(base)
                visibility(False)
                private = probe(args.probe.resolve(), base)
                report["checks"]["real_private_backend_identified"] = private["identified"] and not private["public"]
                visibility(True)
            report["checks"]["temporary_database_and_process_removed"] = True
        else: verify(args.url)
        report["client_binary_sha256"] = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    except (OSError, RuntimeError, subprocess.SubprocessError, ValueError, KeyError):
        report["checks"]["completed"] = False
    private_write(args.report, report)
    passed = bool(report["checks"]) and all(report["checks"].values())
    print("R3 backend and terminal demonstration passed" if passed else "R3 demonstration failed; inspect the outcome report")
    return 0 if passed else 2


if __name__ == "__main__":
    raise SystemExit(main())
