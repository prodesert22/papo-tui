"""R1 development integration: the C client, real backend and verified TLS.

Credentials come from the environment. Reports contain only protocol outcomes;
temporary TLS keys are removed, and each probe logs out its own test session.
"""
import argparse
from datetime import datetime, timezone
import os
from pathlib import Path
import select
import subprocess
import tempfile
import time
from urllib.parse import urlsplit

from tools.check_backend import normalize, private_write


def run_probe(binary, base, environment, mode=""):
    process = subprocess.run([str(binary), base, mode], env=environment,
                             capture_output=True, text=True, timeout=30)
    allowed = {"rest_ready", "ws_open", "ws_error", "ws_closed", "ws_status", "ack",
               "presence", "fragment", "events", "revalidated_failure", "logout"}
    result = {"exit_code": process.returncode}
    for line in process.stdout.splitlines():
        key, separator, value = line.partition('=')
        if separator and key in allowed and value.isdecimal():
            result[key] = int(value)
    return result


def connected(result):
    return (result.get('exit_code') == 0 and result.get('rest_ready') == 1 and
            result.get('ws_open') == 1 and result.get('ws_status') == 101 and
            result.get('ack') == 1 and result.get('logout') == 1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://127.0.0.1:8080')
    parser.add_argument('--username', default=os.environ.get('PAPO_TEST_USERNAME'))
    parser.add_argument('--password-env', default='PAPO_TEST_PASSWORD')
    parser.add_argument('--probe', type=Path, default=Path('build/ws-probe'))
    parser.add_argument('--tls-proxy', type=Path, default=Path('build/integration/tls-proxy'))
    parser.add_argument('--report', type=Path, default=Path('build/integration/r1-backend.json'))
    arguments = parser.parse_args()
    base = normalize(arguments.url)
    parsed = urlsplit(base)
    if parsed.scheme != 'http' or parsed.hostname != '127.0.0.1':
        parser.error('This development probe requires an HTTP 127.0.0.1 upstream')
    password = os.environ.get(arguments.password_env)
    if not arguments.username or not password:
        parser.error('Set the account name and password in the environment')
    environment = dict(os.environ, PAPO_PROBE_USERNAME=arguments.username,
                       PAPO_PROBE_PASSWORD=password)
    report = {'observed_at': datetime.now(timezone.utc).isoformat(), 'base_url': base, 'checks': {}}
    try:
        report['http_ws'] = run_probe(arguments.probe.resolve(), base, environment)
        report['checks']['authenticated_http_ws'] = connected(report['http_ws'])
        time.sleep(0.25)
        arguments.report.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        with tempfile.TemporaryDirectory(prefix='r1-tls-', dir=arguments.report.parent) as directory:
            cert, key = Path(directory) / 'cert.pem', Path(directory) / 'key.pem'
            subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                            '-keyout', str(key), '-out', str(cert), '-subj', '/CN=127.0.0.1',
                            '-addext', 'subjectAltName=IP:127.0.0.1'], check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            proxy = subprocess.Popen([str(arguments.tls_proxy.resolve()), '-upstream', base,
                                      '-cert', str(cert), '-key', str(key)],
                                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
            try:
                if not select.select([proxy.stdout], [], [], 5)[0]:
                    raise RuntimeError('TLS proxy did not become ready')
                secure_base = proxy.stdout.readline().strip()
                secure = urlsplit(secure_base)
                if secure.scheme != 'https' or secure.hostname != '127.0.0.1' or not secure.port:
                    raise RuntimeError('TLS proxy did not provide a loopback address')
                trusted = dict(environment, CURL_CA_BUNDLE=str(cert))
                report['https_wss'] = run_probe(arguments.probe.resolve(), secure_base, trusted)
                report['checks']['authenticated_https_wss'] = connected(report['https_wss'])
                time.sleep(0.25)
                untrusted = dict(environment)
                untrusted.pop('CURL_CA_BUNDLE', None)
                report['untrusted_certificate'] = run_probe(arguments.probe.resolve(), secure_base, untrusted, 'transport')
                report['wrong_hostname'] = run_probe(arguments.probe.resolve(), secure_base.replace('127.0.0.1', 'localhost'), trusted, 'transport')
                for name in ('untrusted_certificate', 'wrong_hostname'):
                    report['checks'][name + '_rejected'] = (report[name].get('exit_code') == 2 and
                                                           report[name].get('ws_error') == 1 and
                                                           report[name].get('ws_status') == 0)
            finally:
                proxy.terminate()
                try:
                    proxy.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proxy.kill()
                    proxy.wait(timeout=5)
                proxy.stdout.close()
    except (OSError, RuntimeError, subprocess.SubprocessError):
        report['checks']['completed'] = False
    private_write(arguments.report, report)
    passed = bool(report['checks']) and all(report['checks'].values())
    print('R1 backend checks passed' if passed else 'R1 backend checks failed; inspect the outcome report')
    return 0 if passed else 2


if __name__ == '__main__':
    raise SystemExit(main())
