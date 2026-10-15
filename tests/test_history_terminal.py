"""R5 through ncurses in a pseudoterminal, using synthetic HTTP data."""
import threading
import time
import unittest
from history_mock import HistoryMock, CHANNEL, OTHER, uuid
from test_terminal import Terminal, ObservedScreen, TerminalStream
from wcwidth import wcswidth


class SidebarObserver(ObservedScreen):
    """Catch transient sidebar changes, even if a later write restores it."""
    def __init__(self, columns, lines):
        super().__init__(columns, lines)
        self.watch = False
        self.sidebar_changes = []

    def sidebar(self):
        return tuple(tuple(self.buffer[y][x] for x in range(1, 25)) for y in range(5, self.lines - 3))

    def observe(self, method, *args, **kwargs):
        before = self.sidebar() if self.watch else None
        result = method(*args, **kwargs)
        if self.watch and self.sidebar() != before:
            self.sidebar_changes.append(method.__name__)
        return result

    def draw(self, *args, **kwargs): return self.observe(super().draw, *args, **kwargs)
    def erase_in_line(self, *args, **kwargs): return self.observe(super().erase_in_line, *args, **kwargs)
    def erase_in_display(self, *args, **kwargs): return self.observe(super().erase_in_display, *args, **kwargs)
    def insert_lines(self, *args, **kwargs): return self.observe(super().insert_lines, *args, **kwargs)
    def delete_lines(self, *args, **kwargs): return self.observe(super().delete_lines, *args, **kwargs)
    def index(self, *args, **kwargs): return self.observe(super().index, *args, **kwargs)
    def reverse_index(self, *args, **kwargs): return self.observe(super().reverse_index, *args, **kwargs)
    def scroll_up(self, *args, **kwargs): return self.observe(super().scroll_up, *args, **kwargs)
    def scroll_down(self, *args, **kwargs): return self.observe(super().scroll_down, *args, **kwargs)

class ConversationObserver(SidebarObserver):
    def sidebar(self):
        return tuple(tuple(self.buffer[y][x] for x in range(26, self.columns - 1))
                     for y in range(5, self.lines - 3))

class HistoryTerminal(unittest.TestCase):
    def wait(self, terminal, text):
        deadline = time.monotonic() + 8
        while text not in '\n'.join(terminal.screen.display) and time.monotonic() < deadline:
            terminal.read(0.03)
        self.assertIn(text, '\n'.join(terminal.screen.display))

    def open(self, server, terminal_type='xterm-256color'):
        t = Terminal(terminal_type=terminal_type)
        t.send(server.url + '\tjosé\tSynthetic!123\n')
        self.wait(t, 'geral')
        self.assertNotIn('URL do servidor', '\n'.join(t.screen.display))
        self.assertFalse(any('/messages' in r[1] for r in server.requests))
        t.send('\x1bOB\x1bOB\n')  # tabs -> category -> first text channel -> open
        self.wait(t, 'Conversar em #geral')
        return t

    def test_channel_navigation_keeps_chat_and_reopening_current_does_not_reload(self):
        with HistoryMock('slow') as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, 'Mensagem 0250')
                terminal.read(.2)
                terminal.send('Rascunho preservado\x1b\x1bOH\x1bOD')
                self.wait(terminal, 'Mensagem 0151')
                self.wait(terminal, 'Foco: Canais')
                # ncurses emits the footer after the conversation cells. Wait
                # for the complete focus transition before recording colors.
                self.wait(terminal, '↑↓ canais')
                before = terminal.screen.display[8][26:]
                requests = [r for r in server.requests if '/messages' in r[1]]
                screen = ConversationObserver(80, 24)
                stream = TerminalStream(screen); stream.feed(bytes(terminal.output))
                terminal.screen, terminal.stream = screen, stream
                conversation = screen.sidebar(); screen.watch = True
                for key in ('\x1bOB', '\x1bOA', '\x1bOH', '\x1bOF', '\x1b[5~', '\x1b[6~'):
                    terminal.send(key)
                    self.assertEqual(screen.sidebar(), conversation)
                    self.assertEqual(screen.sidebar_changes, [], f'Chat flashed during channel navigation: {key!r}')
                screen.watch = False
                # Return the highlight to the open channel and confirm it.
                terminal.send('\x1bOH\x1bOB\n')
                terminal.read(.5)
                self.assertEqual(terminal.screen.display[8][26:], before)
                self.assertIn('Rascunho preservado', '\n'.join(terminal.screen.display))
                for action in (lambda: terminal.click(7, 4), lambda: terminal.send('\x1bOD\x1bOC')):
                    output = len(terminal.output); action(); terminal.read(.5)
                    self.assertNotIn(b'Carregando hist', terminal.output[output:])
                    self.assertEqual(terminal.screen.display[8][26:], before)
                self.assertEqual([r for r in server.requests if '/messages' in r[1]], requests)
                # Clicking the sidebar heading focuses navigation, not a read.
                terminal.click(5, 4); terminal.send('\x1bOB')
                self.assertIn('Foco: Canais', terminal.screen.display[2])
                self.assertEqual(terminal.screen.display[8][26:], before)
                self.assertEqual([r for r in server.requests if '/messages' in r[1]], requests)
                # A different channel must still make exactly one new read.
                terminal.click(8, 4); self.wait(terminal, 'Conteúdo do outro canal')
                self.assertEqual(len([r for r in server.requests if '/messages' in r[1]]), len(requests) + 1)
            finally: self.assertTrue(terminal.close())

    def test_reopening_empty_current_channel_preserves_empty_state(self):
        with HistoryMock('empty') as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, 'Nenhuma mensagem neste canal')
                requests = [r for r in server.requests if '/messages' in r[1]]
                output = len(terminal.output)
                terminal.click(7, 4); terminal.send('\x1bOD\x1bOC'); terminal.read(.2)
                self.assertIn('Nenhuma mensagem neste canal', '\n'.join(terminal.screen.display))
                self.assertNotIn(b'Carregando hist', terminal.output[output:])
                self.assertEqual([r for r in server.requests if '/messages' in r[1]], requests)
            finally: self.assertTrue(terminal.close())

    def test_keyboard_channel_change_then_single_click_opens_history(self):
        with HistoryMock(count=1) as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, 'Mensagem 0001')
                for _ in range(4):
                    terminal.send('\x1bOD\x1bOB\n')
                    self.wait(terminal, 'Conteúdo do outro canal')
                    terminal.click(7, 4)
                    self.wait(terminal, '# geral · Histórico')
                    self.wait(terminal, 'Mensagem 0001')
                    self.assertNotIn('Conteúdo do outro canal', '\n'.join(terminal.screen.display))
                self.assertEqual(sum('/messages' in r[1] for r in server.requests), 9)
            finally: self.assertTrue(terminal.close())

    def test_single_click_after_keyboard_change_with_pending_response(self):
        for term, press, release in (
            ('xterm-256color', '\x1b[<0;5;8M', '\x1b[<0;5;8m'),
            ('xterm-kitty', '\x1b[M %(', '\x1b[M#%('),
        ):
            with self.subTest(term=term), HistoryMock('slow', count=1) as server:
                parent = server.api_handler
                pending = threading.Event()
                def route(method, path, body):
                    if '/channels/' + OTHER + '/messages' in path:
                        pending.set()
                        server.stopping.wait(.5)
                    return parent(method, path, body)
                server.api_handler = route
                terminal = self.open(server, terminal_type=term)
                try:
                    self.wait(terminal, 'Mensagem 0001')
                    terminal.send('\x1bOD\x1bOB\n')
                    self.assertTrue(pending.wait(2))
                    # Real clicks deliver a press and a later release, with a
                    # frame/network poll between them. No subsequent input may
                    # be needed to complete the new channel's HTTP response.
                    terminal.send(press)
                    self.wait(terminal, '# geral · Histórico')
                    terminal.send(release)
                    self.wait(terminal, 'Mensagem 0001')
                    self.assertNotIn('Conteúdo do outro canal', '\n'.join(terminal.screen.display))
                finally: self.assertTrue(terminal.close())

    def test_selecting_current_channel_does_not_restart_pending_history(self):
        with HistoryMock('slow') as server:
            terminal = self.open(server)
            try:
                self.assertTrue(server.history_started.wait(1))
                terminal.click(7, 4)
                terminal.send('\x1bOD\x1bOC')
                self.wait(terminal, 'Mensagem 0250')
                terminal.read(.5)
                self.assertEqual(len([r for r in server.requests if '/messages' in r[1]]), 1)
            finally: self.assertTrue(terminal.close())

    def test_history_scroll_never_clears_or_moves_sidebar_between_frames(self):
        with HistoryMock(count=250) as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, 'Mensagem 0250')
                terminal.read(.2)
                screen = SidebarObserver(80, 24)
                stream = TerminalStream(screen)
                stream.feed(bytes(terminal.output))
                terminal.screen, terminal.stream = screen, stream
                sidebar = screen.sidebar()
                screen.watch = True
                # Arrow keys, page scrolling, wheel, and a click in the already
                # focused history must leave the channel cells untouched.
                for key in ('\x1bOA', '\x1bOB', '\x1b[5~', '\x1b[6~', '\x1b[<64;40;12M', '\x1b[<65;40;12M'):
                    terminal.send(key)
                    self.assertEqual(screen.sidebar(), sidebar)
                    self.assertEqual(screen.sidebar_changes, [], f'Sidebar flashed for {key!r}')
                terminal.click(10, 40)
                self.assertEqual(screen.sidebar_changes, [], 'Clicking focused history cleared the sidebar')
                terminal.send('\x1bOH')
                self.wait(terminal, 'Mensagem 0151')
                terminal.send('\x1b[5~')
                deadline = time.monotonic() + 4
                while not server.older_calls and time.monotonic() < deadline: terminal.read(.03)
                terminal.read(.2)
                self.assertEqual(server.older_calls, 1)
                self.assertEqual(screen.sidebar_changes, [], 'Loading an older page flashed the sidebar')
                # Wheel scrolling while editing must also preserve the sidebar
                # and the editor's draft/cursor across subsequent focus changes.
                screen.watch = False
                terminal.send('Rascunho sem flash')
                self.wait(terminal, 'Rascunho sem flash')
                terminal.send('\x1bOD')
                screen.sidebar_changes.clear(); screen.watch = True
                cursor = (screen.cursor.y, screen.cursor.x)
                terminal.send('\x1b[<64;40;12M')
                self.assertEqual(screen.sidebar_changes, [])
                self.assertEqual((screen.cursor.y, screen.cursor.x), cursor)
                self.assertIn('Rascunho sem flash', '\n'.join(screen.display))
            finally: self.assertTrue(terminal.close())

    def test_login_opens_channels_once_and_renewal_keeps_account_view(self):
        with HistoryMock() as server:
            t = Terminal()
            try:
                t.send(server.url + '\tjosé\tSynthetic!123\n')
                self.wait(t, 'geral')
                self.assertFalse(any('/messages' in r[1] for r in server.requests))
                t.send('\x1b[17~')
                self.wait(t, '[Renovar]')
                t.send('\x12')
                self.wait(t, '[Renovar]')
                t.read(0.3)
                self.assertEqual(server.refresh_count, 1)
                self.assertIn('[Canais / F6]', '\n'.join(t.screen.display))
                self.assertNotIn('[+ Adicionar]', '\n'.join(t.screen.display))
                t.send('\n')
                self.wait(t, 'Você saiu da conta')
                t.send('Synthetic!123\n')
                self.wait(t, 'geral')
                self.assertNotIn('URL do servidor', '\n'.join(t.screen.display))
            finally: self.assertTrue(t.close())

    def test_three_pages_anchor_resize_and_recent(self):
        with HistoryMock() as server:
            t = self.open(server)
            try:
                self.wait(t, 'Mensagem 0250')
                t.send('\x1bOH')
                self.wait(t, 'Mensagem 0151')
                # Name summaries can arrive after the message page. Compare
                # stable content once the visible author has been resolved.
                self.wait(t, 'Apelido')
                # Capture the top visible message and line before prepend.
                before = t.screen.display[8]
                t.send('\x1b[5~')
                deadline = time.monotonic() + 5
                while server.older_calls < 1 and time.monotonic() < deadline: t.read(0.03)
                t.read(0.3)
                self.assertEqual(t.screen.display[8], before)
                t.resize(24, 100)
                self.assertIn('Mensagem 0151', '\n'.join(t.screen.display))
                t.send('\x1bOH')
                self.wait(t, 'Mensagem 0051')
                t.send('\x1b[5~')
                t.read(0.4)
                self.assertIn('Mensagem 0051', '\n'.join(t.screen.display))
                t.send('\x1bOH')
                self.wait(t, 'Mensagem 0001')
                t.send('\x1bOF')
                self.wait(t, 'Mensagem 0250')
                self.assertEqual(server.older_calls, 2)
                self.assertNotIn(b'\x1b]52;c;INJECT', t.output)
                self.assertNotIn(b'Synthetic!123', t.output)
            finally: self.assertTrue(t.close())

    def test_delayed_selection_and_mouse(self):
        with HistoryMock('slow') as server:
            t = self.open(server)
            try:
                self.assertTrue(server.history_started.wait(1))
                # click another channel before the delayed history completes
                t.click(8, 4)
                self.wait(t, 'Conteúdo do outro canal')
                self.assertNotIn('Mensagem 0250', '\n'.join(t.screen.display))
                self.assertTrue(any(OTHER in r[1] for r in server.requests))
                t.send('\x1b[200~\x0c\x11\x1b[201~')
                self.assertIsNone(t.process.poll())
                self.assertFalse(any(r[1].endswith('/auth/logout') for r in server.requests))
                t.send('\x0c')
                self.wait(t, 'Você saiu da conta')
            finally: self.assertTrue(t.close())

    def test_null_content_media_reply_and_long_text_are_sanitized(self):
        with HistoryMock(count=1) as server:
            m = server.messages[0]
            m.update(content=None, edited_at='2026-10-12T12:01:00Z', reply_to=uuid(999),
                     attachments=[{'original_file_name': 'relatório.txt\x1b[31m', 'mime_type': 'text/plain',
                                   'size_bytes': 123, 'moderation_status': 'clean'}],
                     embeds=[{'title': 'Prévia', 'description': 'Descrição', 'url': 'https://example.invalid'}])
            t = self.open(server)
            try:
                t.send('\x1bOH')
                self.wait(t, 'mensagem não disponível')
                self.assertNotIn('Sem conteúdo textual', '\n'.join(t.screen.display))
                self.wait(t, 'relatório.txt')
                self.wait(t, 'editado')
                self.assertNotIn(b'\x1b[31m', t.output)
                t.send('\x1bOF')
                self.wait(t, 'Prévia')
                m['content'] = 'á' * 8182 + 'FIMDOREMOTO'
                m['attachments'] = []; m['embeds'] = []
                t.send('\x1bOF')
                self.wait(t, 'EMOTO')
                self.assertIn('FIMDOR', '\n'.join(t.screen.display))
            finally: self.assertTrue(t.close())

    def test_narrow_navigation_errors_and_return_account(self):
        with HistoryMock('forbidden') as server:
            t = self.open(server)
            try:
                self.wait(t, 'Sem permissão')
                t.resize(14, 44)
                self.wait(t, 'Sem permissão')
                t.send('\x1b[Z')
                self.wait(t, 'Categoria')
                t.send('\x1bOB\n')
                self.wait(t, 'Conteúdo do outro canal')
                t.resize(24, 80)
                t.send('\x1b[17~')
                self.wait(t, '[Canais / F6]')
                t.send('\x1b[17~')
                self.wait(t, 'geral')
                t.send('\x10')
                self.wait(t, 'Selecione um perfil')
            finally: self.assertTrue(t.close())

    def click_text(self, terminal, text):
        for row, line in enumerate(terminal.screen.display):
            if text in line:
                terminal.click(row, wcswidth(line[:line.index(text)]) + min(2, wcswidth(text) - 1))
                return
        self.fail('Control absent: ' + text)

    def test_demo_frame_and_add_login_registration_then_switch_isolated_sessions(self):
        with HistoryMock() as first, HistoryMock() as second:
            second.name = 'Trabalho'
            for m in second.messages: m['content'] = 'Segunda instância ' + m['content']
            route = second.route
            def second_login(method, path, body):
                if method == 'POST' and path.endswith('/auth/login'):
                    second.session_cookie = 'second-fixture'
                    return 200, {'user': {'id': uuid(1), 'username': 'josé'}}, 'second-fixture'
                return route(method, path, body)
            second.api_handler = second_login
            t = self.open(first)
            try:
                self.wait(t, 'Mensagem 0250')
                self.assertIn('[Comunidade]', t.screen.display[1])
                self.assertIn('[+ Adicionar]', t.screen.display[1])
                self.assertIn('Conversar em #', '\n'.join(t.screen.display))
                self.assertNotEqual(t.screen.display[0].strip(), '')  # outer frame
                t.send('rascunho sem enviar')
                self.assertFalse(any(r[1].endswith('/messages') and r[0] == 'POST' for r in first.requests))
                self.click_text(t, '[+ Adicionar]')
                self.wait(t, 'URL do servidor')
                self.assertNotIn(first.url, '\n'.join(t.screen.display))
                self.assertNotIn('josé', '\n'.join(t.screen.display))
                self.assertFalse(any(r[1].endswith('/auth/logout') for r in first.requests))
                self.click_text(t, '[Cadastro]')
                self.wait(t, 'Confirmar senha')
                self.click_text(t, '[Login]')
                t.send(second.url + '\tjosé\tSynthetic!123\n')
                self.wait(t, '[Trabalho]')
                self.wait(t, 'geral')
                self.assertIn('Comunidade', t.screen.display[1])
                t.send('\x1bOB\x1bOB\n')
                self.wait(t, 'Segunda instância')
                self.click_text(t, 'Comunidade')
                self.wait(t, '[Comunidade]')
                self.wait(t, 'geral')
                self.assertNotIn('Segunda instância', '\n'.join(t.screen.display))
                t.send('\x1bOB\x1bOB\n')  # tabs -> list -> text -> history
                self.wait(t, 'Mensagem 0250')
                self.assertEqual(sum(r[1].endswith('/auth/login') for r in first.requests), 1)
                self.assertEqual(sum(r[1].endswith('/auth/login') for r in second.requests), 1)
                self.assertTrue(all(r[2] == 'Auth=second-fixture' for r in second.requests if '/channels' in r[1] or 'summary_batch' in r[1]))
                self.assertTrue(all(r[2] == 'Auth=user-fixture' for r in first.requests if '/channels' in r[1] or 'summary_batch' in r[1]))
                self.assertNotIn(b'Synthetic!123', t.output)
            finally: self.assertTrue(t.close())

    def test_add_by_keyboard_and_cancel_preserves_server_and_paste_is_inert(self):
        with HistoryMock() as server:
            t = self.open(server)
            try:
                t.send('\x1bOD\x1bOH\x1bOA\x1bOA\x1bOC')  # history -> first channel -> buttons -> tabs -> add
                self.assertIn('Foco: Abas', '\n'.join(t.screen.display))
                self.assertIn('>+ Adicionar<', t.screen.display[1])
                t.send('\x1bOD')
                self.assertIn('[+ Adicionar]', t.screen.display[1])
                t.send('\x1bOC')
                self.assertIn('>+ Adicionar<', t.screen.display[1])
                t.send('\n')
                self.wait(t, 'URL do servidor')
                t.send('\x1b')
                self.wait(t, '[Comunidade]')
                self.assertEqual(sum(r[1].endswith('/auth/login') for r in server.requests), 1)
                t.send('\x1b[C')  # CSI arrows emitted by some terminals
                self.assertIn('>+ Adicionar<', t.screen.display[1])
                t.send('\x1b[D')
                self.assertIn('[+ Adicionar]', t.screen.display[1])
                t.resize(22, 44)
                self.assertIn('[+ Adicionar]', t.screen.display[1])
                self.assertIn('[Canais]', '\n'.join(t.screen.display))
                t.send('\x1b[200~\x1bOC\n\x1b[201~')
                self.assertNotIn('URL do servidor', '\n'.join(t.screen.display))
                self.click_text(t, '[+ Adicionar]')
                self.wait(t, 'URL do servidor')
                t.resize(24, 80)
                t.send('\x1b')
                self.wait(t, '[Comunidade]')
            finally: self.assertTrue(t.close())

    def test_failed_add_preserves_original_and_same_url_replaces_tab(self):
        with HistoryMock() as server, HistoryMock() as rejected:
            rejected.scenario = 'unauthorized'
            t = self.open(server)
            try:
                self.click_text(t, '[+ Adicionar]')
                self.wait(t, 'URL do servidor')
                t.send(rejected.url + '\tjosé\tWrong!123\n')
                self.wait(t, 'Usuário ou senha')
                t.send('\x1b')
                self.wait(t, '[Comunidade]')
                self.assertEqual(t.screen.display[1].count('Comunidade'), 1)
                self.assertFalse(any(r[1].endswith('/auth/logout') for r in server.requests))
                self.click_text(t, '[+ Adicionar]')
                self.wait(t, 'URL do servidor')
                t.send(server.url + '\tjosé\tSynthetic!123\n')
                self.wait(t, '[Comunidade]')
                self.wait(t, 'geral')
                self.assertEqual(t.screen.display[1].count('Comunidade'), 1)
                self.assertEqual(sum(r[1].endswith('/auth/login') for r in server.requests), 2)
                t.send('\x0c')
                self.wait(t, 'Você saiu da conta')
                self.assertNotIn('[+ Adicionar]', '\n'.join(t.screen.display))
            finally: self.assertTrue(t.close())

    def test_long_remote_server_names_overflow_tabs_and_narrow_mouse_switch(self):
        with HistoryMock() as first, HistoryMock() as second:
            first.name = 'Primeira界' * 8 + '\x1b]52;c;REMOTE\x07'
            second.name = 'Segunda界' * 8
            t = self.open(first)
            try:
                self.click_text(t, '[+ Adicionar]')
                self.wait(t, 'URL do servidor')
                t.send(second.url + '\tjosé\tSynthetic!123\n')
                self.wait(t, 'Segunda界')
                self.wait(t, 'geral')
                self.assertIn('[+ Adicionar]', t.screen.display[1])
                self.assertIn('‹', t.screen.display[1])
                self.click_text(t, '‹')
                self.wait(t, 'Primeira界')
                self.click_text(t, 'Primeira界')
                self.wait(t, '[Primeira界')
                t.resize(22, 44)
                self.wait(t, '[Primeira界')
                self.assertIn('[+ Adicionar]', t.screen.display[1])
                self.assertIn('›', t.screen.display[1])
                self.click_text(t, '›')
                self.wait(t, 'Segunda界')
                self.click_text(t, 'Segunda界')
                self.wait(t, '[Segunda界')
                self.assertEqual(sum(r[1].endswith('/auth/login') for r in first.requests), 1)
                self.assertNotIn(b'\x1b]52;c;REMOTE', t.output)
            finally: self.assertTrue(t.close())

if __name__ == '__main__': unittest.main()
