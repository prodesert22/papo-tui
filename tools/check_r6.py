"""R6 validation against a temporary database and the actual Go backend.

All users, channels and messages are synthetic and exist only in the isolated
copy. Credentials stay in memory; reports contain boolean checks only.
"""
import argparse
from contextlib import contextmanager
import json
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile
import time

from tools.check_backend import private_write
from tools.check_r3 import ROOT, checked_run, isolated_backend, stop
from tools.check_r4 import environment
from tools.check_r5 import fingerprint, source_fingerprint


@contextmanager
def proxy(binary, base, cert, key, *, drop=False, delay="300ms"):
    command = [str(binary), '-upstream', base, '-cert', str(cert), '-key', str(key), '-message-delay', delay]
    if drop: command.append('-drop-first-message-response')
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    try:
        secure = process.stdout.readline().strip()
        if not secure.startswith('https://127.0.0.1:'): raise RuntimeError('TLS proxy did not become ready')
        yield secure
    finally:
        stop(process)
        if process.stdout: process.stdout.close()


def probe(binary, base, mode, env):
    result = json.loads(checked_run([str(binary), base, mode], env=env, timeout=25))
    time.sleep(0.8)
    return result


def observe_editor(binary, base, env):
    previous_arguments = sys.argv[:]
    sys.path.insert(0, str(ROOT / 'tests'))
    sys.argv = [sys.argv[0], str(binary)]
    try:
        from test_terminal import Terminal
    finally: sys.argv = previous_arguments
    terminal = Terminal((base,), environment=env)
    checks = {}
    def wait(text):
        deadline = time.monotonic() + 8
        while text not in '\n'.join(terminal.screen.display) and time.monotonic() < deadline: terminal.read(0.03)
        if text not in '\n'.join(terminal.screen.display): raise RuntimeError('R6 editor did not reach the expected state')
    try:
        wait('Canal A')
        terminal.send('R6 TUI: ação\x0esegunda linha\n')
        terminal.send(' continuação')
        wait('Mensagem confirmada')
        checks['real_editor_preserves_typing_during_post'] = 'continuação' in terminal.screen.display[9] and 'R6 TUI' not in terminal.screen.display[9]
        terminal.send('\tRascunho B\t')
        wait('Canal A')
        checks['real_editor_preserves_drafts_across_channels'] = 'continuação' in terminal.screen.display[9]
        terminal.resize(22, 44)
        checks['real_editor_preserves_draft_after_resize'] = 'continuação' in terminal.screen.display[9]
        checks['real_editor_never_displays_account_password'] = env['PAPO_PROBE_PASSWORD'].encode() not in terminal.output
    finally:
        checks['real_editor_restores_terminal'] = terminal.close()
    return checks


def observe_product(binary, base, env_a, env_b, *, uncertain=False, count=None):
    # The component observer may have imported this module with a different
    # binary. Set the product binary only while constructing each terminal.
    sys.path.insert(0, str(ROOT / 'tests'))
    previous_arguments = sys.argv[:]
    sys.argv = [sys.argv[0], str(binary)]
    try:
        import test_terminal
    finally:
        sys.argv = previous_arguments
    checks = {}
    text = 'R6 produto: ação\nsegunda linha' if not uncertain else 'R6 produto: confirmação perdida'

    def screen(terminal): return '\n'.join(terminal.screen.display)

    def wait(terminal, value):
        deadline = time.monotonic() + 8
        while value not in screen(terminal) and time.monotonic() < deadline: terminal.read(0.03)
        if value not in screen(terminal): raise RuntimeError('R6 product did not reach the expected state')

    def editor(terminal):
        rows = terminal.screen.display
        border = next(i for i, row in enumerate(rows) if i >= 5 and ' Editor ' in row)
        return '\n'.join(rows[border + 1:len(rows) - (4 if len(rows) >= 18 else 3)])

    def open_account(env):
        old_binary = test_terminal.BINARY
        test_terminal.BINARY = str(binary)
        try:
            terminal = test_terminal.Terminal(environment=env, allow_local_http=False)
        finally:
            test_terminal.BINARY = old_binary
        try:
            terminal.send(base + '\t' + env['PAPO_PROBE_USERNAME'] + '\t' + env['PAPO_PROBE_PASSWORD'] + '\n')
            wait(terminal, 'r6-a')
            terminal.send('\x1bOB\n')
            wait(terminal, '# r6-a · Histórico')
            return terminal
        except Exception:
            terminal.close()
            raise

    terminal = open_account(env_a)
    try:
        before = count() if uncertain else None
        terminal.send(text.replace('\n', '\x0e') + '\n')
        if uncertain:
            wait(terminal, 'Envio sem confirmação')
            checks['product_lost_confirmation_preserves_draft'] = text in editor(terminal) and count() == before + 1
            terminal.send('\n')
            wait(terminal, 'Reenviar mensagem?')
            terminal.send('\n')  # Default Cancelar.
            checks['product_retry_default_cancel_never_replays'] = count() == before + 1
            terminal.send('\x14')
            wait(terminal, 'Reenviar mensagem?')
            terminal.send('\x1bOC\n')
            wait(terminal, 'Mensagem confirmada')
            checks['product_explicit_retry_can_duplicate'] = count() == before + 2 and text not in editor(terminal)
        else:
            terminal.send(' continuação')
            wait(terminal, 'Mensagem confirmada')
            checks['product_confirmation_merges_visible_history'] = 'R6 produto: ação' in screen(terminal) and 'R6 produto' not in editor(terminal)
            checks['product_keeps_edits_after_post'] = 'continuação' in editor(terminal)
            terminal.send('\x1b\x1b\x1bOB\nRascunho produto B')
            wait(terminal, '# r6-b · Histórico')
            terminal.send('\x1b\x1b\x1bOA\n')
            wait(terminal, '# r6-a · Histórico')
            checks['product_channel_drafts_are_independent'] = 'continuação' in editor(terminal) and 'Rascunho produto B' not in editor(terminal)
            terminal.resize(14, 44)
            checks['product_resize_keeps_draft'] = 'continuação' in editor(terminal)
            terminal.resize(24, 80)
        checks['product_password_not_displayed'] = env_a['PAPO_PROBE_PASSWORD'].encode() not in terminal.output
    finally:
        checks['product_restores_terminal'] = terminal.close()
    if not uncertain:
        second = open_account(env_b)
        try:
            wait(second, 'R6 produto: ação')
            checks['product_account_b_reads_account_a'] = 'segunda linha' in screen(second)
            second.send('R6 produto: resposta B\n')
            wait(second, 'Mensagem confirmada')
            checks['product_account_b_can_reply'] = 'R6 produto: resposta B' in screen(second)
        finally:
            checks['product_account_b_restores_terminal'] = second.close()
        first = open_account(env_a)
        try:
            wait(first, 'R6 produto: resposta B')
            checks['product_account_a_reads_reply_b'] = True
        finally:
            checks['product_account_a_restores_terminal'] = first.close()
    return checks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend-binary', type=Path, required=True)
    parser.add_argument('--tls-proxy', type=Path, required=True)
    parser.add_argument('--probe', type=Path, default=ROOT / 'build/send-probe')
    parser.add_argument('--auth-probe', type=Path, default=ROOT / 'build/session-probe')
    parser.add_argument('--terminal-probe', type=Path, default=ROOT / 'build/send-terminal-probe')
    parser.add_argument('--tui', type=Path, default=ROOT / 'build/papo-tui')
    parser.add_argument('--postgres-container', default='papo_postgres')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/r6/validation')
    args = parser.parse_args()
    work = args.output.resolve(); work.mkdir(mode=0o700, parents=True, exist_ok=True)
    access, checks = [], {}
    password = 'R6!' + secrets.token_hex(12)
    username_a, username_b = 'r6a_' + secrets.token_hex(4), 'r6b_' + secrets.token_hex(4)
    text = 'R6: Olá, ação!\nSegunda linha com café.'
    try:
        with isolated_backend(args.backend_binary.resolve(), args.postgres_container, work,
                              on_ready=access.append, label='r6') as (base, seed, _visibility):
            seed(); sql = access[0]
            first = sql("INSERT INTO channels(name,type,position,topic) VALUES('r6-a','text',1,'R6 fixture') RETURNING id;").strip()
            second = sql("INSERT INTO channels(name,type,position,topic) VALUES('r6-b','text',2,'R6 fixture') RETURNING id;").strip()
            count = lambda: int(sql("SELECT count(*) FROM messages;").strip())
            with tempfile.TemporaryDirectory(prefix='tls-', dir=work) as directory:
                cert, key = Path(directory) / 'cert.pem', Path(directory) / 'key.pem'
                checked_run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                             '-keyout', str(key), '-out', str(cert), '-subj', '/CN=127.0.0.1',
                             '-addext', 'subjectAltName=IP:127.0.0.1'], timeout=20)
                key.chmod(0o600)
                with proxy(args.tls_proxy.resolve(), base, cert, key) as secure:
                    env_a, env_b = environment(username_a, password, ca=cert), environment(username_b, password, ca=cert)
                    for env in (env_a, env_b):
                        env.update(PAPO_SEND_CHANNEL=first, PAPO_SEND_SECOND_CHANNEL=second, PAPO_SEND_TEXT=text)
                        result = probe(args.auth_probe, secure, 'register', env)
                        if not result['registered']: raise RuntimeError('Synthetic account registration failed')
                    checks['two_synthetic_accounts_created_only_in_temporary_database'] = True
                    sent = probe(args.probe, secure, 'append', env_a)
                    checks['real_multipart_utf8_and_multiline_confirmed'] = sent['state'] == 2 and sent['message']['content'] == text
                    checks['real_post_keeps_later_edits'] = sent['draft'] == ' continuação'
                    received = probe(args.probe, secure, 'read', env_b)
                    messages = received.get('history', {}).get('messages', [])
                    checks['account_b_reads_account_a_message'] = received.get('read_status') == 200 and any(
                        item['id'] == sent['message']['id'] and item['author_id'] == sent['message']['author_id'] and item['content'] == text for item in messages)
                    reverse = probe(args.probe, secure, 'send', env_b)
                    checks['account_b_can_reply_with_its_own_identity'] = reverse['state'] == 2 and reverse['message']['author_id'] != sent['message']['author_id']
                    received = probe(args.probe, secure, 'read', env_a)
                    checks['account_a_reads_account_b_reply'] = any(item['id'] == reverse['message']['id'] for item in received.get('history', {}).get('messages', []))
                    replaced = probe(args.probe, secure, 'replace', env_a)
                    checks['real_post_preserves_rewritten_draft'] = replaced['state'] == 2 and replaced['draft'] == 'Outro rascunho'
                    before = count()
                    empty = probe(args.probe, secure, 'empty', env_a)
                    checks['empty_text_does_not_create_message'] = empty['submit'] == 1 and count() == before
                    queued = probe(args.probe, secure, 'channels', env_a)
                    checks['real_channel_operations_are_independent'] = queued['confirmed_count'] == 2 and queued['second_id'] > queued['id']
                    checks.update(observe_editor(args.terminal_probe, secure, env_a))
                    history = probe(args.probe, secure, 'read', env_b)
                    checks['account_b_reads_text_from_interactive_editor'] = any(item.get('content') == 'R6 TUI: ação\nsegunda linha' for item in history.get('history', {}).get('messages', []))
                    checks.update(observe_product(args.tui.resolve(), secure, env_a, env_b))
                with proxy(args.tls_proxy.resolve(), base, cert, key, drop=True) as secure:
                    before = count()
                    lost = probe(args.probe, secure, 'send', env_a)
                    checks['lost_confirmation_after_real_creation_is_uncertain'] = lost['state'] == 4 and lost['draft'] == text
                    checks['lost_confirmation_does_not_automatically_duplicate'] = count() == before + 1
                with proxy(args.tls_proxy.resolve(), base, cert, key, delay='16s') as secure:
                    before = count()
                    timeout = probe(args.probe, secure, 'send', env_a)
                    checks['real_timeout_after_creation_keeps_draft_without_replay'] = timeout['state'] == 4 and timeout['draft'] == text and count() == before + 1
                with proxy(args.tls_proxy.resolve(), base, cert, key, drop=True) as secure:
                    before = count()
                    retry = probe(args.probe, secure, 'retry', env_a)
                    checks['manual_retry_requires_duplication_acknowledgement'] = retry['need_confirmation'] and retry['second_id'] > retry['id']
                    checks['acknowledged_manual_retry_has_independent_confirmation'] = retry['second_state'] == 2 and retry['draft'] == '' and count() == before + 2
                with proxy(args.tls_proxy.resolve(), base, cert, key, drop=True) as secure:
                    checks.update(observe_product(args.tui.resolve(), secure, env_a, env_b, uncertain=True, count=count))
        report = {'passed': all(checks.values()), 'kind': 'actual-backend-isolated-database', 'checks': checks,
                  'source_database_mutated': False, 'temporary_users_channels_messages_database_and_tls_removed': True,
                  'product_screen_integration': 'validated', 'tui_binary_sha256': fingerprint(args.tui.resolve()),
                  'source_sha256': source_fingerprint()}
        private_write(work / 'summary.json', report)
        print(json.dumps(report, ensure_ascii=False))
        return 0 if report['passed'] else 1
    except (OSError, ValueError, RuntimeError, KeyError, subprocess.TimeoutExpired):
        private_write(work / 'summary.json', {'passed': False, 'checks': checks, 'error': 'R6 validation failed; credentials omitted'})
        print('R6 validation failed; credentials omitted', file=sys.stderr)
        return 1

if __name__ == '__main__': raise SystemExit(main())
