"""Exercise the C R5 reader against a real Go backend in a temporary database.

Source schema and owner/server rows are copied read-only. Synthetic accounts and
250 messages exist only in the isolated database. Passwords/cookies stay in RAM.
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
from tools.check_r4 import environment


def fingerprint(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_fingerprint():
    root = Path(__file__).resolve().parents[1]
    files = [root / 'CMakeLists.txt', *root.glob('src/**/*.c'), *root.glob('src/**/*.h')]
    digest = hashlib.sha256()
    for path in sorted(files):
        digest.update(str(path.relative_to(root)).encode() + b'\0' + path.read_bytes())
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend-binary', type=Path, required=True)
    parser.add_argument('--tls-proxy', type=Path, required=True)
    parser.add_argument('--probe', type=Path, default=Path('build/history-probe'))
    parser.add_argument('--tui', type=Path, default=Path('build/papo-tui'))
    parser.add_argument('--postgres-container', default='papo_postgres')
    parser.add_argument('--work-dir', type=Path, default=Path('build/r5/validation'))
    args = parser.parse_args()
    work = args.work_dir.resolve(); work.mkdir(parents=True, exist_ok=True, mode=0o700)
    proxy = None; checks = {}; sql = None; operation = 'isolated-backend'; last_status = None
    try:
        def ready(query):
            nonlocal sql
            sql = query
        with isolated_backend(args.backend_binary.resolve(), args.postgres_container, work, on_ready=ready, label='r5') as (base, seed, _visibility):
            seed()
            with tempfile.TemporaryDirectory(prefix='tls-', dir=work) as directory:
                cert, key = Path(directory) / 'cert.pem', Path(directory) / 'key.pem'
                checked_run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                             '-keyout', str(key), '-out', str(cert), '-subj', '/CN=127.0.0.1',
                             '-addext', 'subjectAltName=IP:127.0.0.1'], timeout=20)
                key.chmod(0o600)
                proxy = subprocess.Popen([str(args.tls_proxy.resolve()), '-upstream', base, '-cert', str(cert), '-key', str(key), '-delay-history-ms', '600'],
                                         stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
                secure = proxy.stdout.readline().strip()
                if not secure.startswith('https://127.0.0.1:'): raise RuntimeError('TLS proxy unavailable')
                username, password = 'r5_' + secrets.token_hex(4), 'Synthetic!' + secrets.token_hex(12) + 'A'
                with requests.Session() as session:
                    session.trust_env = False; session.verify = str(cert)
                    next_request = 0
                    def call(method, path, status=200, **kwargs):
                        nonlocal next_request, operation, last_status
                        operation = method + ' ' + ('/channels/.../messages' if '/channels/' in path else path)
                        delay = next_request - time.monotonic()
                        if delay > 0: time.sleep(delay)
                        response = session.request(method, secure + path, timeout=10, allow_redirects=False, **kwargs)
                        next_request = time.monotonic() + 0.15
                        last_status = response.status_code
                        if response.status_code != status: raise RuntimeError('Unexpected backend status')
                        return response.json() if response.content else None
                    registered = call('POST', '/auth/register', 201, json={'username': username, 'password': password})
                    identity = str(UUID(registered['id']))
                    sql("UPDATE servers SET owner_id='" + identity + "';")
                    call('POST', '/auth/login', json={'username': username, 'password': password})
                    call('GET', '/auth/whoami')
                    category = call('POST', '/channels', 201, json={'name': 'R5 categoria', 'type': 'category'})
                    channel = call('POST', '/channels', 201, json={'name': 'R5 histórico', 'type': 'text', 'topic': 'Histórico isolado'})
                    other = call('POST', '/channels', 201, json={'name': 'R5 vazio', 'type': 'text'})
                    voice = call('POST', '/channels', 201, json={'name': 'R5 voz', 'type': 'voice'})
                    channel_id = str(UUID(channel['id'])); other_id = str(UUID(other['id']))
                    expected = []
                    for n in range(1, 251):
                        m = call('POST', '/messages', 201, files={'channel_id': (None, channel_id), 'content': (None, f'Mensagem real {n:04d}')})
                        expected.append(str(UUID(m['id'])))
                    # Exercise exact composite cursor ties, only in test data.
                    sql("UPDATE messages SET created_at='2026-10-12T12:00:00.123456Z' WHERE channel_id='" + channel_id + "';")
                    expected.sort()
                    env = environment(username, password, ca=cert)
                    def probe(mode, selected=channel_id):
                        output = checked_run([str(args.probe.resolve()), secure, mode, selected], env=env, timeout=30)
                        time.sleep(1.25)
                        return json.loads(output)
                    result = probe('recent')
                    checks['https_sorted_channels_and_types'] = [c['type'] for c in result['channels']] == ['category', 'text', 'text', 'voice']
                    checks['no_history_prefetch'] = result['initial']['count'] == 0
                    checks['three_pages_100_200_250'] = [p['count'] for p in result['pages']] == [100, 200, 250]
                    checks['250_unique_ids_equal_timestamp_exact_cursor'] = result['pages'][-1]['ids'] == expected
                    checks['authors_resolved_by_real_summary_post'] = set(result['pages'][-1]['authors']) == {username}
                    checks['return_to_recent_100'] = result['recent']['ids'] == expected[-100:]
                    latest = sql("SELECT last_read_message_id FROM user_channel_state WHERE user_id='" + identity + "' AND channel_id='" + channel_id + "';").strip()
                    checks['history_marks_read_without_cursor_regression'] = latest == expected[-1]
                    empty = probe('single', other_id)
                    checks['empty_real_channel'] = empty['pages'][0]['count'] == 0 and not empty['pages'][0]['more']
                    # A regular account lacks the owner's permission bypass.
                    username_b = 'r5b_' + secrets.token_hex(4)
                    call('POST', '/auth/register', 201, json={'username': username_b, 'password': password})
                    role = call('POST', '/roles', 201, json={'name': 'R5 restrito', 'permissions': {}})
                    role_id = str(UUID(role['id']))
                    call('PUT', '/channels/' + channel_id + '/permissions/' + role_id,
                         json={'read_channel': False, 'send_messages': False})
                    denied_env = environment(username_b, password, ca=cert)
                    denied = json.loads(checked_run([str(args.probe.resolve()), secure, 'single', channel_id], env=denied_env, timeout=30))
                    time.sleep(1.25)
                    checks['real_permission_failure_readable'] = denied['pages'][0]['count'] == 0 and 'Sem permissão' in denied['pages'][0]['status']
                    operation = 'terminal-read'
                    terminal(args.tui.resolve(), secure, username, password, env, checks, work,
                        lambda: int(sql("SELECT count(*) FROM user_connections WHERE user_id='" + identity + "';").strip()))
                stop(proxy); proxy.stdout.close(); proxy = None
        report = {'kind': 'actual-go-backend-isolated-database', 'backend_sha256': fingerprint(args.backend_binary),
                  'probe_sha256': fingerprint(args.probe), 'tui_sha256': fingerprint(args.tui),
                  'proxy_sha256': fingerprint(args.tls_proxy), 'harness_sha256': fingerprint(Path(__file__)),
                  'client_source_sha256': source_fingerprint(),
                  'client_head': checked_run(['git', 'rev-parse', 'HEAD']).strip(), 'controlled_history_response_delay_ms': 600, 'checks': checks,
                  'passed': bool(checks) and all(checks.values()), 'source_database_mutated': False,
                  'temporary_accounts_messages_database_and_tls_removed': True}
        private_write(work / 'backend.json', report)
        print(json.dumps(report, ensure_ascii=False))
        return 0 if report['passed'] else 1
    except (RuntimeError, OSError, ValueError, KeyError, subprocess.TimeoutExpired, requests.RequestException) as failure:
        private_write(work / 'backend.json', {'passed': False, 'checks': checks, 'operation': operation,
                      'http_status': last_status, 'failure_class': type(failure).__name__, 'error': 'R5 validation failed; credentials omitted'})
        print('R5 validation failed; credentials omitted', file=sys.stderr); return 1
    finally:
        stop(proxy)
        if proxy and proxy.stdout: proxy.stdout.close()


def terminal(binary, base, username, password, env, checks, work, session_count):
    previous = sys.argv[:]
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests'))
    sys.argv = [sys.argv[0], str(binary)]
    try: from test_terminal import Terminal
    finally: sys.argv = previous
    t = Terminal(environment=dict(env, TERM='xterm-256color'))
    def wait(text):
        deadline = time.monotonic() + 10
        while text not in '\n'.join(t.screen.display) and time.monotonic() < deadline: t.read(0.03)
        if text not in '\n'.join(t.screen.display):
            private_write(work / 'terminal-failure.json', {'expected': text,
                'screen': [row.replace(password, '[redacted]') for row in t.screen.display]})
            raise RuntimeError('R5 TUI did not reach expected screen')
    try:
        t.send(base + '\t' + username + '\t' + password + '\n'); wait('R5 histórico')
        checks['tui_real_login'] = True
        checks['tui_automatically_opens_channels'] = 'URL do servidor' not in '\n'.join(t.screen.display)
        checks['tui_real_channels'] = True
        t.send('\x1bOB\x1bOB\n'); wait('Somente leitura')
        wait('Mensagem real')
        checks['tui_real_recent_page'] = True
        t.read(0.3)
        t.send('\x1bOH'); wait('Mensagem real')
        before = t.screen.display[8]
        t.send('\x1b[5~'); t.read(1.1)
        checks['tui_real_page_prepend_preserves_anchor'] = t.screen.display[8] == before
        private_write(work / 'tui-history.json', {'kind': 'actual-backend-pty-with-controlled-delay',
            'history_top_line_before': before, 'history_top_line_after': t.screen.display[8],
            'screen': [row.replace(password, '[redacted]') for row in t.screen.display]})
        t.send('\x1bOH\x1b[5~'); t.read(1.1)
        t.send('\x1bOH'); wait('Início do histórico alcançado')
        checks['tui_real_three_pages'] = True
        t.send('\x1bOF'); wait('PageUp no início')
        checks['tui_returns_recent'] = True
        # End starts a fresh GET whose real response is delayed by the proxy.
        # Select the empty channel while that response is still in flight.
        t.send('\x1bOF'); t.click(8, 4); wait('Nenhuma mensagem neste canal')
        checks['tui_channel_switch_during_delayed_real_response'] = 'Mensagem real' not in '\n'.join(t.screen.display)
        checks['tui_demo_layout_real_connection'] = '[+ Adicionar]' in t.screen.display[1] and 'Editor' in '\n'.join(t.screen.display)
        before_add = session_count()
        def click_text(label):
            for row, line in enumerate(t.screen.display):
                if label in line:
                    from wcwidth import wcswidth
                    t.click(row, wcswidth(line[:line.index(label)]) + 2)
                    return
            raise RuntimeError('R5 TUI control absent')
        click_text('[+ Adicionar]'); wait('URL do servidor')
        checks['tui_add_opens_clean_login'] = base not in '\n'.join(t.screen.display) and username not in '\n'.join(t.screen.display)
        click_text('[Cadastro]'); wait('Confirmar senha')
        checks['tui_add_offers_registration'] = True
        click_text('[Login]'); wait('URL do servidor')
        t.send('\x1b'); wait('R5 histórico')
        checks['tui_cancel_add_preserves_real_session'] = before_add == session_count() and '[+ Adicionar]' in t.screen.display[1]
        t.send('\x1b[17~'); wait('[Canais / F6]')
        wait('[Renovar]')
        before_count = session_count()
        t.send('\x12'); wait('[Renovar]')
        checks['tui_real_renewal_keeps_account_view'] = session_count() > before_count and '[+ Adicionar]' not in '\n'.join(t.screen.display)
        checks['password_not_written_to_terminal'] = password.encode() not in t.output
    finally: checks['terminal_restored'] = t.close()
    return checks

if __name__ == '__main__': raise SystemExit(main())
