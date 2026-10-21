"""R5 through ncurses in a pseudoterminal, using synthetic HTTP data."""

from test_context import main as test_main
from terminal_history import SidebarObserver, ConversationObserver

import resource
import threading
import time
from history_mock import HistoryMock, CHANNEL, OTHER, VOICE, uuid
from terminal_support import Terminal, TerminalStream, wait_for_screen


import terminal_history as support


class HistoryTerminal(support.HistoryTerminal):
    def test_history_resize_and_unicode_send_with_one_megabyte_stack(self):
        # Reproduce the Windows executable's default stack limit on Linux.
        with HistoryMock() as server:
            terminal = Terminal()
            try:
                _, hard = resource.prlimit(terminal.process.pid, resource.RLIMIT_STACK)
                resource.prlimit(terminal.process.pid, resource.RLIMIT_STACK, (1024 * 1024, hard))
                terminal.send(server.url + "\tjosé\tSynthetic!123\r")
                self.wait(terminal, "geral")
                terminal.send("\x1bOB\r")
                self.wait(terminal, "Mensagem 0250")
                terminal.resize(28, 100)
                terminal.send("Olá 😀\r")
                deadline = time.monotonic() + 8
                while not server.sent_messages and time.monotonic() < deadline:
                    terminal.read(0.03)
                self.assertTrue(server.sent_messages)
                self.assertEqual(server.sent_messages[-1]["content"], "Olá 😀")
            finally:
                self.assertTrue(terminal.close())
            self.assertEqual(terminal.process.returncode, 0)

    def test_server_automatically_opens_first_channel_without_selection_markers(self):
        with HistoryMock(count=1) as server:
            terminal = Terminal()
            try:
                terminal.send(server.url + "\tjosé\tSynthetic!123\n")
                self.wait(terminal, "Mensagem 0001")
                self.wait(terminal, "Conversar em #geral")
                reads = [r[1] for r in server.requests if "/messages" in r[1]]
                self.assertEqual(len(reads), 1)
                self.assertIn(CHANNEL, reads[0])
                self.assertEqual(terminal.screen.buffer[7][2].data, "#")
                for row in range(6, 10):
                    self.assertNotIn(">", terminal.screen.display[row][1:24])
                self.assertIn("Categoria", terminal.screen.display[6])
            finally:
                self.assertTrue(terminal.close())

    def test_automatic_channel_follows_list_order_including_voice_history(self):
        for first, content in ((OTHER, "Conteúdo do outro canal"), (VOICE, "Conversa de voz 0001")):
            with self.subTest(first=first), HistoryMock(count=1) as server:
                for channel in server.channels:
                    if channel["id"] == first:
                        channel["position"] = 0
                terminal = Terminal()
                try:
                    terminal.send(server.url + "\tjosé\tSynthetic!123\n")
                    self.wait(terminal, content)
                    reads = [r[1] for r in server.requests if "/messages" in r[1]]
                    self.assertEqual(len(reads), 1)
                    self.assertIn(first, reads[0])
                finally:
                    self.assertTrue(terminal.close())

    def test_categories_are_skipped_by_navigation_and_ignore_mouse_clicks(self):
        with HistoryMock(count=1) as server:
            for channel in server.channels:
                channel["position"] *= 10
            for number, position in ((30, 21), (31, 22), (32, 50)):
                server.channels.append(
                    dict(
                        id=uuid(number),
                        name=f"Separador {number}",
                        position=position,
                        type="category",
                        topic=None,
                    )
                )
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0001")
                terminal.click(6, 4)
                self.assertIn("Foco: Histórico", terminal.screen.display[2])
                terminal.send("\x1bOD")  # history -> channels
                self.wait(terminal, "Foco: Canais")
                wait_for_screen(
                    terminal,
                    lambda: terminal.screen.buffer[7][2].fg != terminal.screen.buffer[10][2].fg,
                    "Focused channel highlight did not arrive",
                )
                selected = terminal.screen.buffer[7][2].fg

                def highlighted(row):
                    self.assertEqual(terminal.screen.buffer[row][2].fg, selected)
                    for category in (6, 8, 9, 12):
                        self.assertNotEqual(terminal.screen.buffer[category][2].fg, selected)

                highlighted(7)
                for key, row in (
                    ("\x1bOB", 10),
                    ("\x1bOA", 7),
                    ("\x1bOF", 11),
                    ("\x1b[6~", 11),
                    ("\x1bOH", 7),
                    ("\x1b[5~", 7),
                ):
                    terminal.send(key)
                    highlighted(row)
                terminal.send("\x1bOA")
                self.wait(terminal, "Foco: Canais/DMs")
                terminal.send("\x1bOB\x1bOB")
                highlighted(10)
                reads = [r for r in server.requests if "/messages" in r[1]]
                for category in (6, 8, 9, 12):
                    terminal.click(category, 4)
                    highlighted(10)
                    self.assertIn("Foco: Canais", terminal.screen.display[2])
                self.assertEqual([r for r in server.requests if "/messages" in r[1]], reads)
                terminal.send("\n")
                self.wait(terminal, "Conteúdo do outro canal")
            finally:
                self.assertTrue(terminal.close())

    def test_empty_and_category_only_servers_do_not_open_history(self):
        for categories in (False, True):
            with self.subTest(categories=categories), HistoryMock() as server:
                server.channels = [
                    c for c in server.channels if categories and c["type"] == "category"
                ]
                terminal = Terminal()
                try:
                    terminal.send(server.url + "\tjosé\tSynthetic!123\n")
                    self.wait(terminal, "Categoria" if categories else "Nenhum canal disponível")
                    terminal.send("\x1bOB\x1bOH\x1bOF\x1b[5~\x1b[6~\n\x1bOC\x05")
                    terminal.click(6, 4)
                    terminal.read(0.2)
                    self.assertFalse(any("/messages" in r[1] for r in server.requests))
                    self.assertIsNone(terminal.process.poll())
                finally:
                    self.assertTrue(terminal.close())

    def test_channel_navigation_keeps_chat_and_reopening_current_does_not_reload(self):
        with HistoryMock("slow") as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0250")
                terminal.read(0.2)
                terminal.send("Rascunho preservado\x1b\x1bOH\x1bOD")
                self.wait(terminal, "Mensagem 0151")
                self.wait(terminal, "Foco: Canais")
                # Finish asynchronous paints before recording navigation colors.
                # Author metadata can arrive after the page. Finish that
                # asynchronous paint before observing navigation-only changes.
                self.wait(terminal, "Apelido")
                deadline = time.monotonic() + 5
                stable_since = time.monotonic()
                previous = tuple(terminal.screen.display)
                while time.monotonic() < deadline:
                    terminal.read(0.03)
                    current = tuple(terminal.screen.display)
                    if current != previous:
                        stable_since = time.monotonic()
                        previous = current
                    if time.monotonic() - stable_since >= 0.3:
                        break
                before = terminal.screen.display[8][26:]
                requests = [r for r in server.requests if "/messages" in r[1]]
                screen = ConversationObserver(80, 24)
                stream = TerminalStream(screen)
                stream.feed(bytes(terminal.output))
                terminal.screen, terminal.stream = screen, stream
                conversation = screen.sidebar()
                screen.watch = True
                for key in ("\x1bOB", "\x1bOA", "\x1bOH", "\x1bOF", "\x1b[5~", "\x1b[6~"):
                    terminal.send(key)
                    self.assertEqual(screen.sidebar(), conversation)
                    self.assertEqual(
                        screen.sidebar_changes,
                        [],
                        f"Chat flashed during channel navigation: {key!r}",
                    )
                screen.watch = False
                # Return the highlight to the open channel and confirm it.
                terminal.send("\x1bOH\n")
                terminal.read(0.5)
                self.assertEqual(terminal.screen.display[8][26:], before)
                self.assertIn("Rascunho preservado", "\n".join(terminal.screen.display))
                for action in (lambda: terminal.click(7, 4), lambda: terminal.send("\x1bOD\x1bOC")):
                    output = len(terminal.output)
                    action()
                    terminal.read(0.5)
                    self.assertNotIn(b"Carregando hist", terminal.output[output:])
                    self.assertEqual(terminal.screen.display[8][26:], before)
                self.assertEqual([r for r in server.requests if "/messages" in r[1]], requests)
                # Clicking the sidebar heading focuses navigation, not a read.
                terminal.click(5, 4)
                terminal.send("\x1bOB")
                self.assertIn("Foco: Canais", terminal.screen.display[2])
                self.assertEqual(terminal.screen.display[8][26:], before)
                self.assertEqual([r for r in server.requests if "/messages" in r[1]], requests)
                # A different channel must still make exactly one new read.
                terminal.click(8, 4)
                self.wait(terminal, "Conteúdo do outro canal")
                self.assertEqual(
                    len([r for r in server.requests if "/messages" in r[1]]), len(requests) + 1
                )
            finally:
                self.assertTrue(terminal.close())

    def test_reopening_empty_current_channel_preserves_empty_state(self):
        with HistoryMock("empty") as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, "Nenhuma mensagem neste canal")
                requests = [r for r in server.requests if "/messages" in r[1]]
                output = len(terminal.output)
                terminal.click(7, 4)
                terminal.send("\x1bOD\x1bOC")
                terminal.read(0.2)
                self.assertIn("Nenhuma mensagem neste canal", "\n".join(terminal.screen.display))
                self.assertNotIn(b"Carregando hist", terminal.output[output:])
                self.assertEqual([r for r in server.requests if "/messages" in r[1]], requests)
            finally:
                self.assertTrue(terminal.close())

    def test_keyboard_channel_change_then_single_click_opens_history(self):
        with HistoryMock(count=1) as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0001")
                for _ in range(4):
                    terminal.send("\x1bOD\x1bOB\n")
                    self.wait(terminal, "Conteúdo do outro canal")
                    terminal.click(7, 4)
                    self.wait(terminal, "# geral · Histórico")
                    self.wait(terminal, "Mensagem 0001")
                    self.assertNotIn("Conteúdo do outro canal", "\n".join(terminal.screen.display))
                self.assertEqual(sum("/messages" in r[1] for r in server.requests), 9)
            finally:
                self.assertTrue(terminal.close())

    def test_single_click_after_keyboard_change_with_pending_response(self):
        # Mouse initialization enables SGR for Kitty too, including terminfo
        # entries that used X10 before the player's wide-terminal support.
        for term in ("xterm-256color", "xterm-kitty"):
            with self.subTest(term=term), HistoryMock("slow", count=1) as server:
                parent = server.api_handler
                pending = threading.Event()

                def route(method, path, body):
                    if "/channels/" + OTHER + "/messages" in path:
                        pending.set()
                        server.stopping.wait(0.5)
                    return parent(method, path, body)

                server.api_handler = route
                terminal = self.open(server, terminal_type=term)
                try:
                    self.wait(terminal, "Mensagem 0001")
                    terminal.send("\x1bOD\x1bOB\n")
                    self.assertTrue(pending.wait(2))
                    # Real clicks deliver a press and a later release, with a
                    # frame/network poll between them. No subsequent input may
                    # be needed to complete the new channel's HTTP response.
                    terminal.send("\x1b[<0;5;8M")
                    self.wait(terminal, "# geral · Histórico")
                    terminal.send("\x1b[<0;5;8m")
                    self.wait(terminal, "Mensagem 0001")
                    self.assertNotIn("Conteúdo do outro canal", "\n".join(terminal.screen.display))
                finally:
                    self.assertTrue(terminal.close())

    def test_selecting_current_channel_does_not_restart_pending_history(self):
        with HistoryMock("slow") as server:
            terminal = self.open(server)
            try:
                self.assertTrue(server.history_started.wait(1))
                terminal.click(7, 4)
                terminal.send("\x1bOD\x1bOC")
                self.wait(terminal, "Mensagem 0250")
                terminal.read(0.5)
                self.assertEqual(len([r for r in server.requests if "/messages" in r[1]]), 1)
            finally:
                self.assertTrue(terminal.close())

    def test_history_scroll_never_clears_or_moves_sidebar_between_frames(self):
        with HistoryMock(count=250) as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, "Mensagem 0250")
                terminal.read(0.2)
                screen = SidebarObserver(80, 24)
                stream = TerminalStream(screen)
                stream.feed(bytes(terminal.output))
                terminal.screen, terminal.stream = screen, stream
                sidebar = screen.sidebar()
                screen.watch = True
                # Arrow keys, page scrolling, wheel, and a click in the already
                # focused history must leave the channel cells untouched.
                for key in (
                    "\x1bOA",
                    "\x1bOB",
                    "\x1b[5~",
                    "\x1b[6~",
                    "\x1b[<64;40;12M",
                    "\x1b[<65;40;12M",
                ):
                    terminal.send(key)
                    self.assertEqual(screen.sidebar(), sidebar)
                    self.assertEqual(screen.sidebar_changes, [], f"Sidebar flashed for {key!r}")
                terminal.click(10, 40)
                self.assertEqual(
                    screen.sidebar_changes, [], "Clicking focused history cleared the sidebar"
                )
                terminal.send("\x1bOH")
                self.wait(terminal, "Mensagem 0151")
                terminal.send("\x1b[5~")
                deadline = time.monotonic() + 4
                while not server.older_calls and time.monotonic() < deadline:
                    terminal.read(0.03)
                terminal.read(0.2)
                self.assertEqual(server.older_calls, 1)
                self.assertEqual(
                    screen.sidebar_changes, [], "Loading an older page flashed the sidebar"
                )
                # Wheel scrolling while editing must also preserve the sidebar
                # and the editor's draft/cursor across subsequent focus changes.
                screen.watch = False
                terminal.send("Rascunho sem flash")
                self.wait(terminal, "Rascunho sem flash")
                terminal.send("\x1bOD")
                screen.sidebar_changes.clear()
                screen.watch = True
                cursor = (screen.cursor.y, screen.cursor.x)
                terminal.send("\x1b[<64;40;12M")
                self.assertEqual(screen.sidebar_changes, [])
                self.assertEqual((screen.cursor.y, screen.cursor.x), cursor)
                self.assertIn("Rascunho sem flash", "\n".join(screen.display))
            finally:
                self.assertTrue(terminal.close())

    def test_login_opens_channels_once_and_renewal_keeps_account_view(self):
        with HistoryMock() as server:
            t = Terminal()
            try:
                t.send(server.url + "\tjosé\tSynthetic!123\n")
                self.wait(t, "geral")
                self.wait(t, "Mensagem 0250")
                self.assertEqual(sum("/messages" in r[1] for r in server.requests), 1)
                t.send("\x14")
                self.wait(t, "[Renovar]")
                t.send("\x12")
                self.wait(t, "[Renovar]")
                t.read(0.3)
                self.assertEqual(server.refresh_count, 1)
                self.assertIn("[Canais]", "\n".join(t.screen.display))
                self.assertNotIn("[+ Adicionar]", "\n".join(t.screen.display))
                t.send("\n")
                self.wait(t, "Você saiu da conta")
                t.send("Synthetic!123\n")
                self.wait(t, "geral")
                self.assertNotIn("URL do servidor", "\n".join(t.screen.display))
            finally:
                self.assertTrue(t.close())

    def test_three_pages_anchor_resize_and_recent(self):
        with HistoryMock() as server:
            t = self.open(server)
            try:
                self.wait(t, "Mensagem 0250")
                t.send("\x1bOH")
                self.wait(t, "Mensagem 0151")
                # Name summaries can arrive after the message page. Compare
                # stable content once the visible author has been resolved.
                self.wait(t, "Apelido")
                # Capture the top visible message and line before prepend.
                before = t.screen.display[8]
                t.send("\x1b[5~")
                deadline = time.monotonic() + 5
                while server.older_calls < 1 and time.monotonic() < deadline:
                    t.read(0.03)
                t.read(0.3)
                self.assertEqual(t.screen.display[8], before)
                t.resize(24, 100)
                self.assertIn("Mensagem 0151", "\n".join(t.screen.display))
                t.send("\x1bOH")
                self.wait(t, "Mensagem 0051")
                t.send("\x1b[5~")
                t.read(0.4)
                self.assertIn("Mensagem 0051", "\n".join(t.screen.display))
                t.send("\x1bOH")
                self.wait(t, "Mensagem 0001")
                t.send("\x1bOF")
                self.wait(t, "Mensagem 0250")
                self.assertEqual(server.older_calls, 2)
                self.assertNotIn(b"\x1b]52;c;INJECT", t.output)
                self.assertNotIn(b"Synthetic!123", t.output)
            finally:
                self.assertTrue(t.close())

    def test_wheel_loads_older_pages_without_moving_reading_anchor(self):
        with HistoryMock() as server:
            t = self.open(server)
            try:
                self.wait(t, "Mensagem 0250")
                for calls, message in ((1, "Mensagem 0151"), (2, "Mensagem 0051")):
                    t.send("\x1bOH")
                    self.wait(t, message)
                    self.wait(t, "Apelido")
                    before = t.screen.display[8]
                    t.send("\x1b[<64;40;12M")
                    deadline = time.monotonic() + 3
                    while server.older_calls < calls and time.monotonic() < deadline:
                        t.read(0.03)
                    t.read(0.2)
                    self.assertEqual(server.older_calls, calls)
                    self.assertEqual(t.screen.display[8], before)
                t.send("\x1bOH")
                self.wait(t, "Mensagem 0001")
                t.send("\x1b[<64;40;12M" * 10)
                t.read(0.2)
                self.assertEqual(server.older_calls, 2)
            finally:
                self.assertTrue(t.close())

    def test_prefetch_starts_before_edge_and_loading_animates_only_at_edge(self):
        with HistoryMock() as server:
            started, release = threading.Event(), threading.Event()
            route = server.api_handler

            def delayed_page(method, path, body):
                if "/messages?" in path and "last_id=" in path:
                    started.set()
                    release.wait()
                return route(method, path, body)

            server.api_handler = delayed_page
            t = self.open(server)
            t.resize(24, 120)
            try:
                self.wait(t, "Mensagem 0250")
                t.send("\x1bOH\x1b[6~\x1b[6~\x1b[6~")
                self.assertFalse(started.is_set())
                for _ in range(15):
                    t.send("\x1b[<64;40;12M")
                    if started.is_set():
                        break
                self.assertTrue(started.is_set(), "No page prefetched near the beginning")
                self.assertNotIn("Mensagem 0151", "\n".join(t.screen.display))
                self.assertNotIn("Carregando próximas mensagens", t.screen.display[-2])
                t.send("\x1bOH")
                self.wait(t, "Carregando próximas mensagens")
                before = t.screen.display[8]
                footer = t.screen.display[-2]
                sidebar = [line[1:25] for line in t.screen.display[5:-3]]
                frames = set()
                for _ in range(4):
                    t.read(0.13)
                    line = t.screen.display[-2]
                    self.assertIn("Carregando próximas mensagens", line)
                    frames.add(line)
                    self.assertEqual(t.screen.display[8], before)
                    self.assertEqual([line[1:25] for line in t.screen.display[5:-3]], sidebar)
                self.assertGreater(
                    len(frames | {footer}), 1, "Loading did not animate without input"
                )
                release.set()
                deadline = time.monotonic() + 3
                while (
                    "Carregando próximas mensagens" in t.screen.display[-2]
                    and time.monotonic() < deadline
                ):
                    t.read(0.03)
                self.assertNotIn("Carregando próximas mensagens", t.screen.display[-2])
                self.assertEqual(t.screen.display[8], before)
                self.assertEqual(server.older_calls, 1)
                t.send("\x1bOH")
                self.wait(t, "Mensagem 0051")
            finally:
                release.set()
                self.assertTrue(t.close())

    def test_scroll_pagination_error_stops_loading_until_manual_retry(self):
        with HistoryMock("older-error") as server:
            t = self.open(server)
            try:
                self.wait(t, "Mensagem 0250")
                t.send("\x1bOH\x1b[<64;40;12M")
                self.wait(t, "Falha ao carregar")
                self.assertNotIn("Carregando próximas mensagens", t.screen.display[-2])
                self.assertIn("Mensagem 0151", "\n".join(t.screen.display))
                t.send("\x1b[<64;40;12M" * 8)
                self.assertEqual(server.older_calls, 1)
                t.send("\x12")
                deadline = time.monotonic() + 3
                while server.older_calls < 2 and time.monotonic() < deadline:
                    t.read(0.03)
                t.read(0.2)
                t.send("\x1bOH")
                self.wait(t, "Mensagem 0051")
            finally:
                self.assertTrue(t.close())

    def test_evicted_recent_pages_prefetch_and_animate_when_scrolling_down(self):
        with HistoryMock(count=1250) as server:
            for message in server.messages:
                message["author_id"] = uuid(21)
            started, release = threading.Event(), threading.Event()
            route = server.api_handler

            def delayed_forward(method, path, body):
                if "/messages?order=asc" in path:
                    started.set()
                    release.wait(8)
                return route(method, path, body)

            server.api_handler = delayed_forward
            t = self.open(server)
            try:
                self.wait(t, "Mensagem 1250")
                for oldest in range(1151, 50, -100):
                    t.send("\x1bOH")
                    self.wait(t, f"Mensagem {oldest:04d}")
                    t.send("\x1b[<64;40;12M")
                    # Home only sees the prepended page after its response.
                    expected = max(1, oldest - 100)
                    deadline = time.monotonic() + 3
                    while time.monotonic() < deadline:
                        t.send("\x1bOH")
                        if f"Mensagem {expected:04d}" in "\n".join(t.screen.display):
                            break
                    self.assertIn(f"Mensagem {expected:04d}", "\n".join(t.screen.display))
                t.resize(60, 100)
                for _ in range(30):
                    t.send("\x1b[6~")
                    if started.is_set():
                        break
                self.assertTrue(started.is_set(), "No forward prefetch after sliding the window")
                self.assertNotIn("Carregando próximas mensagens", t.screen.display[-2])
                t.send("\x1b[6~" * 4)
                self.wait(t, "Carregando próximas mensagens")
                self.assertIn("Mensagem 1000", "\n".join(t.screen.display))
                before = t.screen.display[8]
                release.set()
                deadline = time.monotonic() + 3
                while (
                    "Carregando próximas mensagens" in t.screen.display[-2]
                    and time.monotonic() < deadline
                ):
                    t.read(0.03)
                self.assertNotIn("Carregando próximas mensagens", t.screen.display[-2])
                self.assertEqual(t.screen.display[8], before)
                self.assertNotIn("Mensagem 1100", "\n".join(t.screen.display))
                self.assertEqual(server.newer_calls, 1)
            finally:
                release.set()
                self.assertTrue(t.close())

    def test_delayed_selection_and_mouse(self):
        with HistoryMock("slow") as server:
            t = self.open(server)
            try:
                self.assertTrue(server.history_started.wait(1))
                # click another channel before the delayed history completes
                t.click(8, 4)
                self.wait(t, "Conteúdo do outro canal")
                self.assertNotIn("Mensagem 0250", "\n".join(t.screen.display))
                self.assertTrue(any(OTHER in r[1] for r in server.requests))
                t.send("\x1b[200~\x0c\x11\x1b[201~")
                self.assertIsNone(t.process.poll())
                self.assertFalse(any(r[1].endswith("/auth/logout") for r in server.requests))
                t.send("\x0c")
                self.wait(t, "Você saiu da conta")
            finally:
                self.assertTrue(t.close())

    def test_null_content_media_reply_and_long_text_are_sanitized(self):
        with HistoryMock(count=1) as server:
            m = server.messages[0]
            m.update(
                content=None,
                edited_at="2026-10-12T12:01:00Z",
                reply_to=uuid(999),
                attachments=[
                    {
                        "original_file_name": "relatório.txt\x1b[31m",
                        "mime_type": "text/plain",
                        "size_bytes": 123,
                        "moderation_status": "clean",
                    }
                ],
                embeds=[
                    {
                        "title": "Prévia",
                        "description": "Descrição",
                        "url": "https://example.invalid",
                    }
                ],
            )
            t = self.open(server)
            try:
                t.send("\x1bOH")
                self.wait(t, "mensagem não disponível")
                self.assertNotIn("Sem conteúdo textual", "\n".join(t.screen.display))
                self.wait(t, "relatório.txt")
                self.wait(t, "editado")
                self.assertNotIn(b"\x1b[31m", t.output)
                t.send("\x1bOF")
                self.wait(t, "Prévia")
                m["content"] = "á" * 8182 + "FIMDOREMOTO"
                m["attachments"] = []
                m["embeds"] = []
                t.send("\x1bOF")
                self.wait(t, "EMOTO")
                self.assertIn("FIMDOR", "\n".join(t.screen.display))
            finally:
                self.assertTrue(t.close())

    def test_message_filling_the_width_keeps_one_separator_line(self):
        with HistoryMock(count=2) as server:
            # At 212 columns the content area fits 175 cells; a trailing space
            # at that boundary used to wrap into an empty line below the text.
            server.messages[0]["content"] = "WRAP" + "A" * 170 + " "
            t = self.open(server)
            try:
                t.resize(30, 212)
                self.wait(t, "Mensagem 0002")
                self.wait(t, "WRAP")

                def row_of(text):
                    return next(
                        (row for row, line in enumerate(t.screen.display) if text in line), None
                    )

                first, second = row_of("WRAP"), row_of("Mensagem 0002")
                self.assertIsNotNone(first)
                self.assertIsNotNone(second)
                self.assertEqual(second - first, 3)
            finally:
                self.assertTrue(t.close())

    def test_trailing_space_at_the_width_boundary_does_not_add_an_empty_line(self):
        with HistoryMock(count=2) as server:
            # At 211 columns the content area fits 174 cells, so the trailing
            # space used to wrap alone into an empty line below the text.
            server.messages[0]["content"] = "WRAP" + "A" * 170 + " "
            t = self.open(server)
            try:
                t.resize(30, 211)
                self.wait(t, "Mensagem 0002")
                self.wait(t, "WRAP")

                def row_of(text):
                    return next(
                        (row for row, line in enumerate(t.screen.display) if text in line), None
                    )

                first, second = row_of("WRAP"), row_of("Mensagem 0002")
                self.assertIsNotNone(first)
                self.assertIsNotNone(second)
                self.assertEqual(second - first, 3)
            finally:
                self.assertTrue(t.close())

    def test_narrow_navigation_errors_and_return_account(self):
        with HistoryMock("forbidden") as server:
            t = self.open(server)
            try:
                self.wait(t, "Sem permissão")
                t.resize(14, 44)
                self.wait(t, "Sem permissão")
                t.send("\x1b[Z\x1b[Z")
                self.wait(t, "Categoria")
                t.send("\x1bOB\n")
                self.wait(t, "Conteúdo do outro canal")
                t.resize(24, 80)
                t.send("\x14")
                self.wait(t, "[Canais]")
                t.send("\x14")
                self.wait(t, "geral")
                t.send("\x10")
                self.wait(t, "Selecione um perfil")
            finally:
                self.assertTrue(t.close())

    def test_demo_frame_and_add_login_registration_then_switch_isolated_sessions(self):
        with HistoryMock() as first, HistoryMock() as second:
            second.name = "Trabalho"
            for m in second.messages:
                m["content"] = "Segunda instância " + m["content"]
            route = second.route

            def second_login(method, path, body):
                if method == "POST" and path.endswith("/auth/login"):
                    second.session_cookie = "second-fixture"
                    return 200, {"user": {"id": uuid(1), "username": "josé"}}, "second-fixture"
                return route(method, path, body)

            second.api_handler = second_login
            t = self.open(first)
            try:
                self.wait(t, "Mensagem 0250")
                self.assertIn("[Comunidade]", t.screen.display[1])
                self.assertIn("[+ Adicionar]", t.screen.display[1])
                self.assertIn("Conversar em #", "\n".join(t.screen.display))
                self.assertNotEqual(t.screen.display[0].strip(), "")  # outer frame
                t.send("rascunho sem enviar")
                self.assertFalse(
                    any(r[1].endswith("/messages") and r[0] == "POST" for r in first.requests)
                )
                self.click_text(t, "[+ Adicionar]")
                self.wait(t, "URL do servidor")
                self.assertNotIn(first.url, "\n".join(t.screen.display))
                self.assertNotIn("josé", "\n".join(t.screen.display))
                self.assertFalse(any(r[1].endswith("/auth/logout") for r in first.requests))
                self.click_text(t, "[Cadastro]")
                self.wait(t, "Confirmar senha")
                self.click_text(t, "[Login]")
                t.send(second.url + "\tjosé\tSynthetic!123\n")
                self.wait(t, "[Trabalho]")
                self.wait(t, "geral")
                self.assertIn("Comunidade", t.screen.display[1])
                t.send("\x1bOB\n")
                self.wait(t, "Segunda instância")
                self.click_text(t, "Comunidade")
                self.wait(t, "[Comunidade]")
                self.wait(t, "geral")
                self.assertNotIn("Segunda instância", "\n".join(t.screen.display))
                t.send("\x1bOB\n")  # tabs -> first channel -> history
                self.wait(t, "Mensagem 0250")
                self.assertEqual(sum(r[1].endswith("/auth/login") for r in first.requests), 1)
                self.assertEqual(sum(r[1].endswith("/auth/login") for r in second.requests), 1)
                self.assertTrue(
                    all(
                        r[2] == "Auth=second-fixture"
                        for r in second.requests
                        if "/channels" in r[1] or "summary_batch" in r[1]
                    )
                )
                self.assertTrue(
                    all(
                        r[2] == "Auth=user-fixture"
                        for r in first.requests
                        if "/channels" in r[1] or "summary_batch" in r[1]
                    )
                )
                self.assertNotIn(b"Synthetic!123", t.output)
            finally:
                self.assertTrue(t.close())

    def test_add_by_keyboard_and_cancel_preserves_server_and_paste_is_inert(self):
        with HistoryMock() as server:
            t = self.open(server)
            try:
                t.send(
                    "\x1bOD\x1bOH\x1bOA\x1bOA\x1bOC"
                )  # history -> first channel -> buttons -> tabs -> add
                self.assertIn("Foco: Abas", "\n".join(t.screen.display))
                self.assertIn(">+ Adicionar<", t.screen.display[1])
                t.send("\x1bOD")
                self.assertIn("[+ Adicionar]", t.screen.display[1])
                t.send("\x1bOC")
                self.assertIn(">+ Adicionar<", t.screen.display[1])
                t.send("\n")
                self.wait(t, "URL do servidor")
                t.send("\x1b")
                self.wait(t, "[Comunidade]")
                self.assertEqual(sum(r[1].endswith("/auth/login") for r in server.requests), 1)
                t.send("\x1b[C")  # CSI arrows emitted by some terminals
                self.assertIn(">+ Adicionar<", t.screen.display[1])
                t.send("\x1b[D")
                self.assertIn("[+ Adicionar]", t.screen.display[1])
                t.resize(22, 44)
                self.assertIn("[+ Adicionar]", t.screen.display[1])
                self.assertIn("[Canais]", "\n".join(t.screen.display))
                t.send("\x1b[200~\x1bOC\n\x1b[201~")
                self.assertNotIn("URL do servidor", "\n".join(t.screen.display))
                self.click_text(t, "[+ Adicionar]")
                self.wait(t, "URL do servidor")
                t.resize(24, 80)
                t.send("\x1b")
                self.wait(t, "[Comunidade]")
            finally:
                self.assertTrue(t.close())

    def test_failed_add_preserves_original_and_same_url_replaces_tab(self):
        with HistoryMock() as server, HistoryMock() as rejected:
            rejected.scenario = "unauthorized"
            t = self.open(server)
            try:
                self.click_text(t, "[+ Adicionar]")
                self.wait(t, "URL do servidor")
                t.send(rejected.url + "\tjosé\tWrong!123\n")
                self.wait(t, "Usuário ou senha")
                t.send("\x1b")
                self.wait(t, "[Comunidade]")
                self.assertEqual(t.screen.display[1].count("Comunidade"), 1)
                self.assertFalse(any(r[1].endswith("/auth/logout") for r in server.requests))
                self.click_text(t, "[+ Adicionar]")
                self.wait(t, "URL do servidor")
                t.send(server.url + "\tjosé\tSynthetic!123\n")
                self.wait(t, "[Comunidade]")
                self.wait(t, "geral")
                self.assertEqual(t.screen.display[1].count("Comunidade"), 1)
                self.assertEqual(sum(r[1].endswith("/auth/login") for r in server.requests), 2)
                t.send("\x0c")
                self.wait(t, "Você saiu da conta")
                self.assertNotIn("[+ Adicionar]", "\n".join(t.screen.display))
            finally:
                self.assertTrue(t.close())

    def test_long_remote_server_names_overflow_tabs_and_narrow_mouse_switch(self):
        with HistoryMock() as first, HistoryMock() as second:
            first.name = "Primeira界" * 8 + "\x1b]52;c;REMOTE\x07"
            second.name = "Segunda界" * 8
            t = self.open(first)
            try:
                self.click_text(t, "[+ Adicionar]")
                self.wait(t, "URL do servidor")
                t.send(second.url + "\tjosé\tSynthetic!123\n")
                self.wait(t, "Segunda界")
                self.wait(t, "geral")
                self.assertIn("[+ Adicionar]", t.screen.display[1])
                self.assertIn("‹", t.screen.display[1])
                self.click_text(t, "‹")
                self.wait(t, "Primeira界")
                self.click_text(t, "Primeira界")
                self.wait(t, "[Primeira界")
                t.resize(22, 44)
                self.wait(t, "[Primeira界")
                self.assertIn("[+ Adicionar]", t.screen.display[1])
                self.assertIn("›", t.screen.display[1])
                self.click_text(t, "›")
                self.wait(t, "Segunda界")
                self.click_text(t, "Segunda界")
                self.wait(t, "[Segunda界")
                self.assertEqual(sum(r[1].endswith("/auth/login") for r in first.requests), 1)
                self.assertNotIn(b"\x1b]52;c;REMOTE", t.output)
            finally:
                self.assertTrue(t.close())


if __name__ == "__main__":
    test_main(1, verbosity=2)
