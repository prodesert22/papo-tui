"""Typing through the real TUI with synthetic, contract-shaped WS/REST peers."""
import time
import unittest
from urllib.parse import urlsplit
from history_mock import CHANNEL, OTHER, uuid, assert_history_reads
from realtime_mock import RealtimeMock
from test_terminal import Terminal


class TypingServer(RealtimeMock):
    def __init__(self):
        super().__init__(count=0)
        self.users = {uuid(1000 + n): dict(id=uuid(1000 + n), username=f'user{n + 1}',
                                         nickname=f'Pessoa{n + 1}', typing=None) for n in range(5)}
        self.summary_delay = 0
        self.summary_failures = 0

    def route(self, method, path, body):
        if method == 'POST' and urlsplit(path).path.endswith('/users/user_summary_batch'):
            self.summary_calls += 1
            if self.summary_failures:
                self.summary_failures -= 1
                return 503, None
            users = [dict(self.users[id]) for id in body['ids'] if id in self.users]
            if self.summary_delay: time.sleep(self.summary_delay)
            return 200, users
        return super().route(method, path, body)

    def typing(self, n=0, *, active=True, channel=CHANNEL):
        self.emit(dict(type='typing', channel_id=channel, user_id=uuid(1000 + n), is_typing=active))


class TypingTerminal(unittest.TestCase):
    def screen(self, terminal): return '\n'.join(terminal.screen.display)

    def wait(self, terminal, text, *, absent=False, seconds=8):
        deadline = time.monotonic() + seconds
        while (text in self.screen(terminal)) == absent and time.monotonic() < deadline:
            terminal.read(.03)
        if absent: self.assertNotIn(text, self.screen(terminal))
        else: self.assertIn(text, self.screen(terminal))

    def open(self, server):
        terminal = Terminal(); self.addCleanup(lambda: self.assertTrue(terminal.close()))
        terminal.send(server.url + '\tjosé\tSynthetic!123\n'); self.wait(terminal, 'geral')
        terminal.resize(24, 160); terminal.send('\x1bOB\x1bOB\n')
        self.wait(terminal, 'Nenhuma mensagem neste canal')
        self.wait(terminal, 'Tempo real conectado')
        return terminal

    def line(self, terminal):
        return terminal.screen.display[-4]

    def test_custom_single_plural_four_and_generic_five_deduplicate(self):
        with TypingServer() as server:
            server.users[uuid(1000)]['typing'] = 'está preparando uma resposta'
            terminal = self.open(server)
            server.typing(); self.wait(terminal, 'Pessoa1 está preparando uma resposta')
            requests = server.summary_calls
            for _ in range(3): server.typing()
            terminal.read(.2)
            self.assertEqual(server.summary_calls, requests)
            for n in range(1, 4): server.typing(n)
            self.wait(terminal, 'Pessoa1, Pessoa2, Pessoa3, Pessoa4 estão digitando…')
            self.assertNotIn('preparando', self.line(terminal))
            server.typing(4); self.wait(terminal, 'Várias pessoas estão digitando…')
            self.assertNotIn('Pessoa1', self.line(terminal))
            server.typing(4, active=False)
            self.wait(terminal, 'Pessoa1, Pessoa2, Pessoa3, Pessoa4 estão digitando…')
            for n in range(1, 4): server.typing(n, active=False)
            self.wait(terminal, 'Pessoa1 está preparando uma resposta')
            server.typing(active=False); self.wait(terminal, 'preparando', absent=True)
            self.assertNotIn('Evento ignorado', self.screen(terminal))
            self.assertEqual(sum('/messages' in r[1] for r in server.requests if r[0] == 'GET'), 1)

    def test_presence_overrides_pending_summary_and_empty_phrase_restores_default(self):
        with TypingServer() as server:
            server.summary_delay = .35
            server.users[uuid(1000)]['typing'] = 'frase antiga'
            terminal = self.open(server)
            server.typing(); terminal.read(.1)
            server.emit(dict(type='presence_update', user_id=uuid(1000), status='online',
                             nickname='Álice', typing='está escrevendo com calma'))
            self.wait(terminal, 'Álice está escrevendo com calma'); terminal.read(.5)
            self.assertIn('Álice está escrevendo com calma', self.line(terminal))
            self.assertNotIn('frase antiga', self.screen(terminal))
            server.emit(dict(type='presence_update', user_id=uuid(1000), status='online', nickname='Álice', typing=''))
            self.wait(terminal, 'Álice está digitando…')
            server.emit(dict(type='presence_update', user_id=uuid(1000), status='online', nickname='Álice'))
            terminal.read(.1); self.assertIn('Álice está digitando…', self.line(terminal))

    def test_inactive_self_invalid_stop_message_and_offline(self):
        with TypingServer() as server:
            terminal = self.open(server)
            server.typing(channel=OTHER)
            server.emit(dict(type='typing', channel_id=CHANNEL, user_id=uuid(1), is_typing=True))
            terminal.read(.2); self.assertNotIn('digitando', self.line(terminal))
            self.assertEqual(server.summary_calls, 0)
            server.emit(dict(type='typing', channel_id=CHANNEL, user_id=uuid(1000), is_typing='true'))
            self.wait(terminal, 'inválido ignorado'); self.assertNotIn('digitando', self.line(terminal))
            server.typing(); self.wait(terminal, 'Pessoa1 está digitando…')
            server.emit(dict(server.message(1), type='message', author_id=uuid(1000), content='Resposta pronta'))
            self.wait(terminal, 'Resposta pronta'); self.assertNotIn('digitando', self.line(terminal))
            server.typing(); self.wait(terminal, 'Pessoa1 está digitando…')
            server.emit(dict(type='presence_update', user_id=uuid(1000), status='offline'))
            self.wait(terminal, 'digitando', absent=True)
            server.typing(); self.wait(terminal, 'Pessoa1 está digitando…')
            terminal.click(8, 4); self.wait(terminal, '# outro · Histórico')
            self.assertNotIn('digitando', self.line(terminal))
            assert_history_reads(self, server, {CHANNEL: (1, 2), OTHER: (1, 1)})

    def test_expiry_is_refreshed_and_disconnect_clears_indicator(self):
        with TypingServer() as server:
            terminal = self.open(server)
            server.typing(); self.wait(terminal, 'Pessoa1 está digitando…')
            terminal.read(3.3); server.typing(); terminal.read(3.3)
            self.assertIn('Pessoa1 está digitando…', self.line(terminal))
            self.wait(terminal, 'digitando', absent=True, seconds=4)
            server.typing(); self.wait(terminal, 'Pessoa1 está digitando…')
            server.disconnect(); self.wait(terminal, 'digitando', absent=True)

    def test_actual_edits_emit_throttled_events_and_navigation_does_not(self):
        with TypingServer() as server:
            terminal = self.open(server)
            terminal.send('\n\x1bOC\x1bOD'); terminal.read(.1)
            self.assertFalse([e for e in server.ws_messages if e['type'] == 'typing'])
            terminal.send('rascunho'); terminal.read(.2)
            events = [e for e in server.ws_messages if e['type'] == 'typing']
            self.assertEqual(events, [dict(type='typing', channel_id=CHANNEL)])
            terminal.read(2.1); terminal.send('\x1bOD\x1bOC'); terminal.read(.1)
            self.assertEqual(len([e for e in server.ws_messages if e['type'] == 'typing']), 1)
            terminal.send(' novo'); terminal.read(.1)
            self.assertEqual(len([e for e in server.ws_messages if e['type'] == 'typing']), 2)
            terminal.send('\n'); self.wait(terminal, 'Mensagem confirmada')
            self.assertEqual(server.sent_messages[0]['content'], 'rascunho novo')

    def test_remote_controls_unicode_resize_and_missing_user_are_safe(self):
        with TypingServer() as server:
            server.users[uuid(1000)].update(nickname='Álice\x1b]52;c;INJECT\x07', typing='escreve 界\x1b[2J')
            terminal = self.open(server)
            server.typing(); self.wait(terminal, 'escreve 界')
            self.assertNotIn(b'\x1b]52;c;INJECT', terminal.output)
            terminal.resize(14, 44)
            self.assertEqual(len(terminal.screen.display), 14)
            self.assertEqual(terminal.screen.columns, 44)
            self.assertTrue(all(len(row) <= 44 for row in terminal.screen.display))
            self.assertIn('Álice', self.line(terminal))
            terminal.resize(24, 160); self.wait(terminal, 'escreve 界')
            server.typing(active=False); server.typing(99)
            self.wait(terminal, 'Usuário indisponível está digitando…')
            terminal.read(.3)  # Wait for the omitted summary to be negatively cached.
            requests = server.summary_calls
            server.typing(99); terminal.read(.2)
            self.assertEqual(server.summary_calls, requests)

    def test_summary_failure_preserves_draft_and_later_typing_can_resolve_name(self):
        with TypingServer() as server:
            server.summary_failures = 1
            terminal = self.open(server); terminal.send('Rascunho preservado')
            server.typing(); self.wait(terminal, 'Autores indisponíveis')
            self.assertIn('Usuário indisponível está digitando…', self.line(terminal))
            self.assertIn('Rascunho preservado', self.screen(terminal))
            server.typing(); self.wait(terminal, 'Pessoa1 está digitando…')
            self.assertEqual(server.summary_calls, 2)
            self.assertFalse(server.sent_messages)
            self.assertEqual(sum('/messages' in r[1] for r in server.requests if r[0] == 'GET'), 1)

    def test_presence_without_nickname_does_not_prevent_mention_name_lookup(self):
        with TypingServer() as server:
            terminal = self.open(server)
            server.emit(dict(type='presence_update', user_id=uuid(1000), status='online'))
            server.emit(dict(server.message(1), type='message', content=f'Olá @mention(<@{uuid(1000)}>)'))
            self.wait(terminal, 'Olá @user1')
            self.assertNotIn('@mention(', self.screen(terminal))


if __name__ == '__main__': unittest.main()
