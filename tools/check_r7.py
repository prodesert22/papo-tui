"""R7 actual Go backend validation; synthetic accounts in a temporary database.

Credentials remain in memory. The source database is only read, and reports
contain checks and hashes, never cookies, passwords or remote error bodies.
"""
import argparse
from contextlib import ExitStack
import json
from pathlib import Path
import secrets
import subprocess
import sys
import tempfile
import time
from uuid import UUID

import requests
import websocket
from tools.check_backend import private_write
from tools.check_r3 import ROOT, checked_run, isolated_backend, stop
from tools.check_r4 import environment
from tools.check_r5 import fingerprint, source_fingerprint


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--backend-binary', type=Path, required=True)
    parser.add_argument('--tls-proxy', type=Path, required=True)
    parser.add_argument('--tui', type=Path, default=ROOT / 'build/papo-tui')
    parser.add_argument('--postgres-container', default='papo_postgres')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/r7/validation')
    args = parser.parse_args()
    work = args.output.resolve(); work.mkdir(mode=0o700, parents=True, exist_ok=True)
    checks, access, observations = {}, [], {}
    phase = 'setup'
    last_status = None
    password = 'R7!' + secrets.token_hex(12)
    users = ['r7a_' + secrets.token_hex(4), 'r7b_' + secrets.token_hex(4)]
    try:
        with ExitStack() as stack:
            base, seed, _visibility = stack.enter_context(isolated_backend(args.backend_binary.resolve(), args.postgres_container,
                                                                          work, on_ready=access.append, label='r7'))
            seed(); sql = access[0]
            channel = str(UUID(sql("INSERT INTO channels(name,type,position,topic) VALUES('r7-geral','text',1,'R7 isolado') RETURNING id;").strip()))
            sql("INSERT INTO channels(name,type,position) VALUES('r7-outro','text',2);")
            directory = Path(stack.enter_context(tempfile.TemporaryDirectory(prefix='tls-', dir=work)))
            cert, key = directory / 'cert.pem', directory / 'key.pem'
            checked_run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-days', '1',
                         '-keyout', str(key), '-out', str(cert), '-subj', '/CN=127.0.0.1',
                         '-addext', 'subjectAltName=IP:127.0.0.1'], timeout=20)
            key.chmod(0o600)
            proxy = subprocess.Popen([str(args.tls_proxy.resolve()), '-upstream', base, '-cert', str(cert), '-key', str(key),
                                      '-r7-control', '-message-delay', '400ms', '-delay-history-ms', '300'],
                                     stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
            stack.callback(proxy.stdout.close); stack.callback(stop, proxy)
            secure = proxy.stdout.readline().strip()
            if not secure.startswith('https://127.0.0.1:'): raise RuntimeError('TLS proxy unavailable')
            sessions = [stack.enter_context(requests.Session()) for _ in users]
            next_request = 0

            def call(session, method, path, status=200, *, direct=False, **kwargs):
                nonlocal next_request, last_status
                delay = next_request - time.monotonic()
                if delay > 0: time.sleep(delay)
                if direct:
                    # Same isolated backend through its loopback address. Keep
                    # the original Secure flag in the session's cookie jar.
                    cookie = session.cookies.get('Auth')
                    if cookie: kwargs['headers'] = {'Cookie': 'Auth=' + cookie}
                response = session.request(method, (base if direct else secure) + path, timeout=10,
                                           allow_redirects=False, **kwargs)
                last_status = response.status_code
                next_request = time.monotonic() + .2
                if response.status_code != status: raise RuntimeError('Unexpected backend status: ' + str(response.status_code))
                return response.json() if response.content else None

            identities=[]
            for session, user in zip(sessions, users):
                session.trust_env = False; session.verify = str(cert)
                registered=call(session, 'POST', '/auth/register', 201, json={'username': user, 'password': password})
                identities.append(str(UUID(registered['id'])))
                call(session, 'POST', '/auth/login', json={'username': user, 'password': password})
            checks['two_accounts_in_isolated_database'] = True
            phase = 'product conversation'
            # Reuse the PTY driver without importing its unittest command line.
            sys.path.insert(0, str(ROOT / 'tests'))
            previous = sys.argv[:]; sys.argv = [sys.argv[0], str(args.tui.resolve())]
            try: import test_terminal
            finally: sys.argv = previous
            test_terminal.BINARY = str(args.tui.resolve())
            terminals = []

            def close_terminal(t):
                if getattr(t, 'r7_closed', False): return True
                t.r7_closed = True
                return t.close()

            def screen(t): return '\n'.join(t.screen.display)

            def pump(seconds=.1):
                deadline = time.monotonic() + seconds
                while time.monotonic() < deadline:
                    for terminal in terminals: terminal.read(.015)

            def wait(t, text, seconds=12):
                deadline = time.monotonic() + seconds
                while text not in screen(t) and time.monotonic() < deadline: pump()
                if text not in screen(t): raise RuntimeError('Expected product state absent: ' + text)

            def editor(t):
                rows = t.screen.display
                border = next(i for i, row in enumerate(rows) if i >= 5 and ' Editor ' in row)
                return '\n'.join(rows[border + 1:len(rows) - (4 if len(rows) >= 18 else 3)])

            for user in users:
                terminal = test_terminal.Terminal(environment=dict(environment(user, password, ca=cert), TERM='xterm-256color'),
                                                 allow_local_http=False)
                terminals.append(terminal); stack.callback(close_terminal, terminal)
                terminal.send(secure + '\t' + user + '\t' + password + '\n')
                wait(terminal, 'r7-geral'); terminal.send('\x1bOB\n'); wait(terminal, '# r7-geral · Histórico')
            a, b = terminals
            a.send('R7 conversa A para B\n')
            wait(a, 'Mensagem confirmada'); wait(b, 'R7 conversa A para B')
            b.send('R7 resposta B para A\n')
            wait(b, 'Mensagem confirmada'); wait(a, 'R7 resposta B para A')
            checks['two_accounts_receive_without_manual_get'] = True
            pump(.6)
            checks['ws_and_delayed_rest_do_not_duplicate'] = screen(a).count('R7 conversa A para B') == 1 and screen(b).count('R7 resposta B para A') == 1
            phase = 'channel navigation with active websocket'
            a.send('\x1b\x1bOD\x1bOB')  # browse the other channel without opening it
            b.send('R7 WS durante navegação\n')
            wait(a, 'R7 WS durante navegação')
            checks['channel_navigation_preserves_active_history_and_ws'] = (
                'Foco: Canais' in screen(a) and '# r7-geral · Histórico' in screen(a))
            output = len(a.output)
            a.send('\x1bOA\x1bOC'); a.click(6, 4); pump(.5)
            checks['reselecting_current_channel_preserves_live_message'] = (
                'R7 WS durante navegação' in screen(a) and b'Carregando hist' not in a.output[output:])
            phase = 'split mouse click after keyboard channel selection'
            a.send('\x1bOD\x1bOB\n')
            wait(a, '# r7-outro · Histórico')
            a.send('\x1b[<0;5;7M')
            wait(a, '# r7-geral · Histórico'); pump(.08)
            a.send('\x1b[<0;5;7m')
            wait(a, 'R7 WS durante navegação')
            checks['split_mouse_release_does_not_block_history_response'] = True
            phase = 'mention selection and delivery'
            a.send('R7 menção @'+users[1]);wait(a,'Menções');wait(a,'@'+users[1])
            a.send('\t');pump(.2)
            checks['mention_tab_selects_without_sending'] = 'Menções' not in screen(a) and '@'+users[1] in editor(a)
            a.send('\n');wait(a,'Mensagem confirmada');wait(b,'@'+users[1]);pump(.2)
            expected='R7 menção @mention(<@'+identities[1]+'>) '
            stored=sql("SELECT content FROM messages WHERE author_id='"+identities[0]+"' ORDER BY created_at DESC,id DESC LIMIT 1;").rstrip('\n')
            checks['mention_wire_contains_canonical_uuid'] = stored==expected
            checks['mention_live_display_is_readable'] = 'R7 menção' in screen(b) and '@mention(' not in screen(b)
            checks['mention_recipient_has_vertical_bar'] = any(b.screen.buffer[y][26].data in ('│','x') for y in range(8,16))
            phase = 'live embed updates'
            observer = websocket.create_connection(secure.replace('https://', 'wss://', 1) + '/ws',
                cookie='Auth=' + sessions[0].cookies.get('Auth'), sslopt={'ca_certs': str(cert)}, timeout=2,
                suppress_origin=True)
            stack.callback(observer.close)
            embedded = call(sessions[0], 'POST', '/messages', 201, files={
                'channel_id': (None, channel), 'content': (None, ''),
                'embeds': (None, json.dumps([{'title': 'R7 embed ao vivo', 'description': 'Conteúdo recebido via WS'}]))})
            wait(a, 'R7 embed ao vivo'); wait(b, 'R7 embed ao vivo')
            checks['embed_only_message_renders_without_reopening_channel'] = all('Conteúdo recebido via WS' in screen(t) for t in terminals)
            call(sessions[0], 'PUT', '/messages/' + str(UUID(embedded['id'])), json={
                'content': '', 'embeds': [{'title': 'R7 embed substituído', 'description': 'Atualização ao vivo'}]})
            wait(a, 'R7 embed substituído'); wait(b, 'R7 embed substituído')
            checks['embed_replacement_updates_both_accounts_live'] = all('R7 embed ao vivo' not in screen(t) for t in terminals)
            call(sessions[0], 'PUT', '/messages/' + str(UUID(embedded['id'])), json={'content': 'R7 embed removido', 'embeds': []})
            snapshots = []
            deadline = time.monotonic() + 3
            while len(snapshots) < 3 and time.monotonic() < deadline:
                event = json.loads(observer.recv())
                if event.get('type') == 'message_embeds_update' and event.get('message_id') == embedded['id']:
                    snapshots.append(event.get('embeds'))
            checks['backend_delivers_embed_update_events'] = len(snapshots) == 3
            observations['empty_embed_update_shape'] = 'null' if snapshots and snapshots[-1] is None else 'array'
            observer.close()
            wait(a, 'R7 embed removido'); wait(b, 'R7 embed removido'); pump(.3)
            checks['empty_embed_update_removes_previous_metadata'] = all('R7 embed substituído' not in screen(t) for t in terminals)
            a.send('Rascunho R7 preservado')
            original = call(sessions[0], 'POST', '/messages', 201, files={'channel_id': (None, channel), 'content': (None, 'R7 original para editar')})
            deleted = call(sessions[0], 'POST', '/messages', 201, files={'channel_id': (None, channel), 'content': (None, 'R7 excluir enquanto offline')})
            wait(b, 'R7 excluir enquanto offline')
            phase = 'offline recovery'
            call(sessions[0], 'POST', '/__papo_test/offline', 204)
            wait(a, 'Reconectando'); a.send(' durante queda')
            a.resize(22, 60)
            checks['draft_and_resize_during_reconnection'] = 'Rascunho R7' in editor(a)
            call(sessions[0], 'PUT', '/messages/' + str(UUID(original['id'])), direct=True, json={'content': 'R7 edição offline recuperada'})
            call(sessions[0], 'DELETE', '/messages/' + str(UUID(deleted['id'])), 204, direct=True)
            call(sessions[1], 'POST', '/messages', 201, direct=True, files={'channel_id': (None, channel), 'content': (None, 'R7 nova mensagem offline')})
            call(sessions[0], 'POST', '/__papo_test/online', 204)
            wait(a, 'R7 nova mensagem offline'); wait(b, 'R7 nova mensagem offline')
            wait(a, 'R7 edição offline recuperada'); wait(b, 'R7 edição offline recuperada')
            checks['offline_new_messages_edits_deletions_recovered'] = all('R7 excluir enquanto offline' not in screen(t) for t in terminals)
            checks['draft_preserved_after_recovery'] = 'durante queda' in editor(a)
            phase = 'revocation'
            a.resize(24, 80)
            call(sessions[0], 'POST', '/auth/drop_connection', json={'connection_id': 'ALL'})
            # Force a reconnect/REST check without waiting for the backend's ping.
            call(sessions[1], 'POST', '/__papo_test/offline', 204)
            call(sessions[1], 'POST', '/__papo_test/online', 204)
            wait(a, '[ Entrar ]', seconds=20)
            checks['revoked_connection_returns_to_login'] = 'Senha (password)' in screen(a) and 'Entre novamente' in screen(a)
            checks['terminal_restored_a'] = close_terminal(a)
            checks['terminal_restored_b'] = close_terminal(b)
            terminals.clear()
        report = {'passed': all(checks.values()), 'kind': 'actual-go-backend-isolated-database', 'checks': checks,
                  'observations': observations,
                  'source_database_mutated': False, 'temporary_resources_removed': True,
                  'tui_sha256': fingerprint(args.tui.resolve()), 'client_source_sha256': source_fingerprint(),
                  'backend_sha256': fingerprint(args.backend_binary.resolve()), 'harness_sha256': fingerprint(Path(__file__))}
        private_write(work / 'summary.json', report)
        print(json.dumps({'passed': report['passed'], 'checks': len(checks), 'report': str(work / 'summary.json')}))
        return 0 if report['passed'] else 1
    except (RuntimeError, OSError, ValueError, subprocess.TimeoutExpired, requests.RequestException,
            websocket.WebSocketException) as failure:
        private_write(work / 'summary.json', {'passed': False, 'phase': phase, 'checks': checks,
                                            'kind': 'actual-go-backend-isolated-database', 'temporary_resources_removed': True,
                                            'failure_class': type(failure).__name__, 'last_status': last_status})
        print(json.dumps({'passed': False, 'phase': phase, 'checks': checks, 'last_status': last_status}))
        return 1


if __name__ == '__main__': raise SystemExit(main())
