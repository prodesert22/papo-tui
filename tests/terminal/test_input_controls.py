"""Real input controls in PTYs; clipboard helpers are synthetic or OSC-only."""

from test_context import main as test_main

import base64
import json
import os
from pathlib import Path
import re
import signal
import tempfile
import time
import unittest
from history_mock import HistoryMock
from realtime_mock import RealtimeMock
from mock_server import MockServer
from terminal_support import Terminal, editor_region, editor_column, wait_for_screen
from wcwidth import wcswidth


class InputControls(unittest.TestCase):
    def screen(self, t):
        return "\n".join(t.screen.display)

    def wait(self, t, text, *, absent=False, seconds=8):
        deadline = time.monotonic() + seconds
        while (text in self.screen(t)) == absent and time.monotonic() < deadline:
            t.read(0.03)
        if absent:
            self.assertNotIn(text, self.screen(t))
        else:
            self.assertIn(text, self.screen(t))

    def terminal(self, *args, **kwargs):
        t = Terminal(*args, **kwargs)
        self.addCleanup(lambda: self.assertTrue(t.close()))
        arguments = kwargs.get("arguments", args[0] if args else ())
        self.wait(t, "papo · Servidores salvos" if "--profiles" in arguments else "URL do servidor")
        return t

    def open(self, server, **kwargs):
        t = self.terminal(**kwargs)
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "geral")
        t.resize(24, 160)
        wait_for_screen(
            t,
            lambda: t.screen.buffer[23][159].data in ("j", "┘"),
            "Resized conversation frame did not arrive",
        )
        t.send("\x1bOB\n")
        self.wait(t, "Conversar em #geral")
        return t

    def field(self, t):
        first, end = editor_region(t)
        return "\n".join(t.screen.display[first:end])

    def wait_field(self, t, text, seconds=3):
        wait_for_screen(t, lambda: text in self.field(t), f"Missing editor text: {text}", seconds)

    def wait_cursor(self, t, row, column, seconds=3):
        expected = (row, column)
        wait_for_screen(
            t,
            lambda: (t.screen.cursor.y, t.screen.cursor.x) == expected,
            f"Cursor did not reach {expected}",
            seconds,
        )

    def mouse(self, t, row, column, code, release=False):
        t.send(f"\x1b[<{code};{column + 1};{row + 1}{'m' if release else 'M'}")

    def wait_copied(self, t, expected):
        previous = getattr(t, "observed_copies", 0)

        def received():
            matches = re.findall(rb"\x1b\]52;c;([A-Za-z0-9+/=]*)\x07", t.output)
            return len(matches) > previous and base64.b64decode(matches[-1]).decode() == expected

        wait_for_screen(t, received, "Expected clipboard snapshot did not arrive")
        t.observed_copies = len(re.findall(rb"\x1b\]52;c;([A-Za-z0-9+/=]*)\x07", t.output))

    def wait_selected(self, t, cells, ordinary):
        wait_for_screen(
            t,
            lambda: all(
                (t.screen.buffer[row][column].fg, t.screen.buffer[row][column].bg)
                != (ordinary.fg, ordinary.bg)
                for row, column in cells
            ),
            "Selection highlight was not rendered",
        )

    def test_placeholder_growth_five_rows_wheel_and_clear(self):
        with HistoryMock(count=0) as server:
            t = self.open(server)
            self.assertNotIn(" Editor ", self.screen(t))
            first, end = editor_region(t)
            self.assertEqual(end - first, 1)
            for n in range(1, 7):
                t.send(("\x0e" if n > 1 else "") + f"linha{n}")
                self.wait_field(t, f"linha{n}")
                first, end = editor_region(t)
                self.assertEqual(end - first, min(n, 5))
            self.assertNotIn("linha1", self.field(t))
            self.wait_field(t, "linha6")
            self.mouse(t, first, 35, 64)
            self.wait_field(t, "linha1")
            wait_for_screen(t, lambda: t.screen.cursor.hidden, "Scrolled editor cursor is visible")
            self.wait_field(t, "linha1")
            self.assertTrue(t.screen.cursor.hidden)
            self.mouse(t, first, 35, 65)
            self.wait_field(t, "linha6")
            t.send("\x1bOA\x1bOHX")
            self.wait_field(t, "Xlinha5")
            t.send("\x03")
            self.wait(t, "Conversar em #geral")
            self.assertIsNone(t.process.poll())
            first, end = editor_region(t)
            self.assertEqual(end - first, 1)
            t.resize(14, 44)
            t.send("primeira\x0esegunda")
            self.wait_field(t, "segunda")
            first, end = editor_region(t)
            self.assertEqual(end - first, 1)
            self.wait_field(t, "segunda")
            t.send("\x03")
            self.wait(t, "Conversar em #geral")

    def test_shift_enter_inserts_a_line_without_sending(self):
        with RealtimeMock(count=0) as server:
            t = self.open(server)
            self.assertIn(b"\x1b[?u", t.output)
            self.assertNotIn(b"\x1b[>1u", t.output)
            t.send("\x1b[?0u")
            wait_for_screen(t, lambda: b"\x1b[>1u" in t.output, "Keyboard mode was not negotiated")
            self.assertIn(b"\x1b[>1u", t.output)
            t.send("primeira linha\x1b[13;2usegunda linha\x1b[27;2;13~terceira linha")
            self.wait_field(t, "primeira linha")
            self.wait_field(t, "segunda linha")
            self.wait_field(t, "terceira linha")
            self.assertFalse(server.sent_messages)
            t.send("\n")
            self.wait(t, "Mensagem confirmada")
            self.assertEqual(
                server.sent_messages[-1]["content"],
                "primeira linha\nsegunda linha\nterceira linha",
            )

    def test_disambiguated_keypad_keeps_editing_and_enter_shortcuts(self):
        with RealtimeMock(count=0) as server:
            t = self.open(server)
            t.send("\x1b[?0uoriginal\x1b[57423u\x1b[57424;2uNovo")
            self.wait_field(t, "Novo")
            self.assertNotIn("original", self.field(t))
            t.send("\x1b[57414;2usegunda")
            self.wait_field(t, "segunda")
            self.assertFalse(server.sent_messages)
            t.send("\x1b[57414u")
            self.wait(t, "Mensagem confirmada")
            self.assertEqual(server.sent_messages[-1]["content"], "Novo\nsegunda")

    def test_shift_enter_with_lock_modifiers_never_submits(self):
        with RealtimeMock(count=0) as server:
            t = self.open(server)
            t.send("\x1b[?0ulinha0")
            lines = ["linha0"]
            for locks in (0, 64, 128, 192):
                modifier = 2 + locks
                for packet in (
                    f"\x1b[13;{modifier}u",
                    f"\x1b[27;{modifier};13~",
                    f"\x1b[57414;{modifier}u",
                ):
                    with self.subTest(locks=locks, packet=packet):
                        lines.append(f"linha{len(lines)}")
                        t.send(packet + lines[-1])
                        self.wait_field(t, lines[-1])
                        self.assertFalse(server.sent_messages)
            t.send("\n")
            self.wait(t, "Mensagem confirmada")
            self.assertEqual(len(server.sent_messages), 1)
            self.assertEqual(server.sent_messages[0]["content"], "\n".join(lines))

    def test_shift_enter_does_not_submit_the_login_form(self):
        with MockServer() as server:
            t = self.terminal()
            t.send("\x1b[?0u" + server.url + "\tjosé\tSynthetic!123")
            password_row = next(
                row + 1 for row, line in enumerate(t.screen.display) if "Senha (password)" in line
            )
            self.wait_cursor(t, password_row, 12 + len("Synthetic!123"))
            for packet in ("\x1b[13;2u", "\x1b[27;130;13~", "\x1b[57414;194u"):
                # An extra character proves the preceding packet was handled
                # before asserting that no request was submitted.
                t.send(packet + "X")
                self.wait_cursor(t, password_row, 12 + len("Synthetic!123X"))
                self.assertFalse(server.requests)
                self.assertIn("Senha (password)", self.screen(t))
                t.send("\x7f")
                self.wait_cursor(t, password_row, 12 + len("Synthetic!123"))
            t.send("\n")
            self.wait(t, "[Canais]")

    @unittest.skipIf(os.name == "nt", "POSIX signal shutdown")
    def test_negotiated_keyboard_mode_is_popped_once_on_sigterm(self):
        t = self.terminal()
        t.send("\x1b[?17u\x1b[?31u")
        wait_for_screen(t, lambda: b"\x1b[>1u" in t.output, "Keyboard mode was not negotiated")
        self.assertEqual(t.output.count(b"\x1b[>1u"), 1)
        t.process.send_signal(signal.SIGTERM)
        t.process.wait(timeout=5)
        t.read()
        self.assertEqual(t.process.returncode, 0)
        self.assertEqual(t.output.count(b"\x1b[<u"), 1)
        self.assertLess(bytes(t.output).index(b"\x1b[<u"), bytes(t.output).rindex(b"\x1b[?1049l"))

    def test_shift_selection_colors_graphemes_and_word_movement(self):
        with HistoryMock(count=0) as server:
            t = self.open(server)
            t.send("Ação界")
            self.wait_field(t, "Ação界")
            row, _ = editor_region(t)
            end = editor_column(t) + wcswidth("Ação界")
            self.wait_cursor(t, row, end)
            ordinary = t.screen.buffer[row][end - 1]
            t.send("\x1b[1;2D\x1b[1;2D")
            self.wait_selected(t, ((row, end - 1),), ordinary)
            selected = t.screen.buffer[row][end - 1]
            self.assertNotEqual((selected.fg, selected.bg), (ordinary.fg, ordinary.bg))
            t.send("X\n")
            self.wait(t, "Mensagem confirmada")
            self.assertEqual(server.sent_messages[-1]["content"], "AçãX")
            t.send("um dois três\x1b[1;5D\x1b[1;6Dnovo ")
            self.wait_field(t, "um novo três")
            t.send("\x1b[1;5H\x1b[1;5C\x1b[1;6Ffim\n")
            self.wait(t, "Mensagem confirmada")
            deadline = time.monotonic() + 5
            while len(server.sent_messages) < 2 and time.monotonic() < deadline:
                t.read(0.03)
            self.assertEqual(server.sent_messages[-1]["content"], "umfim")

    def test_word_arrows_stop_at_word_edges_in_every_input(self):
        with HistoryMock(count=0) as server:
            t = self.open(server)
            t.send("ação  duas\x0epalavras\x1b[1;5H")
            self.wait_field(t, "palavras")
            first, _ = editor_region(t)
            self.wait_cursor(t, first, editor_column(t))
            row, column = t.screen.cursor.y, t.screen.cursor.x
            t.send("\x1b[1;5C")
            self.wait_cursor(t, row, column + 4)
            t.send("\x1b[1;5C")
            self.wait_cursor(t, row, column + 10)
            t.send("\x1b[1;5C")
            self.wait_cursor(t, row + 1, column + 8)
            t.send("\x1b[1;5D")
            self.wait_cursor(t, row + 1, column)
            t.send("\x1b[1;5D")
            self.wait_cursor(t, row, column + 6)
            t.send("\x1b[1;5D")
            self.wait_cursor(t, row, column)
        for arguments, opening in (((), "\t"), (("--profiles",), "\x0e")):
            f = self.terminal(arguments=arguments)
            f.send(opening + "uma palavra\x1bOH")
            self.wait(f, "uma palavra")
            field_row = next(i for i, line in enumerate(f.screen.display) if "uma palavra" in line)
            field_column = f.screen.display[field_row].index("uma palavra")
            self.wait_cursor(f, field_row, field_column)
            row, column = f.screen.cursor.y, f.screen.cursor.x
            f.send("\x1b[1;5C")
            self.wait_cursor(f, row, column + 3)
            f.send("\x1b[1;5C")
            self.wait_cursor(f, row, column + 11)
            f.send("\x1b[1;5D")
            self.wait_cursor(f, row, column + 4)
            f.send("\x1b[1;5D")
            self.wait_cursor(f, row, column)

    def test_kitty_copy_marker_lifecycle_and_forwarded_shortcut(self):
        t = self.terminal(environment=dict(KITTY_WINDOW_ID="papo-synthetic-window"))
        self.assertIn(b"\x1b]1337;SetUserVar=papo_tui=MQ==\x07", t.output)
        self.assertIn(b"\x1b[?u", t.output)
        t.send("\x1b[?0u")
        wait_for_screen(t, lambda: b"\x1b[>1u" in t.output, "Keyboard mode was not negotiated")
        self.assertEqual(t.output.count(b"\x1b[>1u"), 1)
        t.send("\x1b[?1u")
        self.assertEqual(t.output.count(b"\x1b[>1u"), 1)
        t.send("\ttexto selecionado\x1bOH\x1b[1;2F\x1b[99;6u")
        self.wait(t, "Texto copiado")
        self.wait_copied(t, "texto selecionado")
        self.assertIn("texto selecionado", self.screen(t))
        t.send("\x11")
        t.process.wait(timeout=5)
        t.read()
        self.assertIn(b"\x1b]1337;SetUserVar=papo_tui\x07", t.output)
        self.assertIn(b"\x1b[<u", t.output)

    def test_drag_batch_copy_and_expiring_independent_notice(self):
        with RealtimeMock(count=0) as server:
            t = self.open(server)
            t.send("Mensagem enviada\n")
            self.wait(t, "Mensagem confirmada")
            t.send("alpha beta")
            self.wait_field(t, "alpha beta")
            first, _ = editor_region(t)
            self.wait_cursor(t, first, editor_column(t) + len("alpha beta"))
            row = t.screen.cursor.y
            start = t.screen.cursor.x - 10
            t.send(
                f"\x1b[<0;{start + 1};{row + 1}M\x1b[<32;{start + 6};{row + 1}M\x1b[<0;{start + 6};{row + 1}m"
            )
            self.mouse(t, row, start + 2, 2)
            self.mouse(t, row, start + 2, 2, release=True)
            self.wait(t, "Texto copiado")
            self.wait_copied(t, "alpha")
            footer = t.screen.display[-2]
            self.assertIn("Tempo real conectado", footer)
            self.assertIn("Mensagem confirmada", footer)
            t.send("X")
            self.wait_field(t, "X beta")
            t.read(3.2)
            self.assertNotIn("Texto copiado", t.screen.display[-2])
            self.assertIn("Tempo real conectado", t.screen.display[-2])
            self.assertIn("Mensagem confirmada", t.screen.display[-2])

    def test_fragmented_paste_is_atomic_keeps_ws_and_never_submits(self):
        with RealtimeMock(count=0) as server:
            t = self.open(server)
            t.send("texto antigo\x1bOH\x1b[1;2F")
            t.send("\x1b[200~ação\r")
            t.read(0.15)
            self.wait_field(t, "texto antigo")
            self.assertNotIn("ação", self.field(t))
            server.emit(
                dict(server.message(1), type="message", content="Mensagem durante a colagem")
            )
            self.wait(t, "Mensagem durante a colagem")
            t.send("\nsegunda linha\x03\x11")
            self.assertFalse(server.sent_messages)
            self.wait_field(t, "texto antigo")
            t.send("\x1b[201~")
            self.wait_field(t, "ação")
            self.wait_field(t, "segunda linha")
            self.assertNotIn("texto antigo", self.field(t))
            self.assertIsNone(t.process.poll())
            t.send("\n")
            self.wait(t, "Mensagem confirmada")
            self.assertEqual(server.sent_messages[-1]["content"], "ação\nsegunda linha")

    def test_oversized_paste_preserves_selection_and_original_draft(self):
        with HistoryMock(count=0) as server:
            t = self.open(server)
            t.send("texto intacto\x1bOH\x1b[1;2F")
            t.send("\x1b[200~" + "界" * 2050 + "\x1b[201~")
            self.wait(t, "Colagem excede o limite")
            self.wait_field(t, "texto intacto")
            self.assertFalse(server.sent_messages)
            t.send("Novo\n")
            self.wait(t, "Mensagem confirmada")
            self.assertEqual(server.sent_messages[-1]["content"], "Novo")

    def test_login_shared_selection_clear_paste_and_private_password_copy(self):
        with MockServer() as server:
            t = self.terminal()
            t.send(server.url + "\tusuário errado\x1bOH\x1b[1;2Fjosé\t")
            password_row = next(
                row + 1 for row, line in enumerate(t.screen.display) if "Senha (password)" in line
            )
            self.wait_cursor(t, password_row, 12)
            row, column = t.screen.cursor.y, t.screen.cursor.x
            t.send("SenhaErrada")
            self.wait_cursor(t, row, column + len("SenhaErrada"))
            t.send("\x03")
            self.wait_cursor(t, row, column)
            self.assertIsNone(t.process.poll())
            t.send("\x1b[200~Synthetic!123")
            t.read(0.1)
            self.assertNotIn("***", t.screen.display[row])
            t.send("\x1b[201~\x1bOH\x1b[1;2F")
            self.assertNotIn(b"Synthetic!123", t.output)
            t.send("\x1b[99;6u")
            self.wait(t, "Texto copiado")
            self.wait_copied(t, "Synthetic!123")
            self.mouse(t, row, column + 2, 2)
            self.mouse(t, row, column + 2, 2, release=True)
            self.wait(t, "Texto copiado")
            self.wait_copied(t, "Synthetic!123")
            t.send("\n")
            self.wait(t, "[Comunidade]")
            login = next(
                r for r in server.requests if r[0] == "POST" and r[1].endswith("/auth/login")
            )
            self.assertEqual(login[3]["username"], "josé")
            self.assertEqual(login[3]["password"], "Synthetic!123")

    def test_profile_fields_use_words_selection_clear_and_copy(self):
        with MockServer() as server:
            t = self.terminal(arguments=("--profiles",))
            t.send("\x0e")
            self.wait(t, "Nome local")
            row, column = t.screen.cursor.y, t.screen.cursor.x
            t.send("Lugar teste\x1b[1;5D\x1b[1;2F")
            t.send("\x1b[27;6;67~")
            self.wait(t, "Texto copiado")
            self.wait_copied(t, "teste")
            self.mouse(t, row, column + 7, 2)
            self.mouse(t, row, column + 7, 2, release=True)
            self.wait(t, "Texto copiado")
            self.wait_copied(t, "teste")
            t.send("amigos\tURL errada\x03\x1b[200~" + server.url + "\x1b[201~\t\n")
            self.wait(t, "Lugar amigos")
            self.assertIsNone(t.process.poll())
            config = json.loads(
                (Path(t.configuration.name) / "papo-tui/config.json").read_text(encoding="utf-8")
            )
            self.assertEqual(config["profiles"][0]["name"], "Lugar amigos")
            self.assertEqual(config["profiles"][0]["url"], server.url)

    def test_native_clipboard_is_polled_and_owns_the_selected_snapshot(self):
        with (
            tempfile.TemporaryDirectory(prefix="papo-clipboard-fixture-") as folder,
            HistoryMock(count=0) as server,
        ):
            root = Path(folder)
            captured = root / "copied.txt"
            command = root / "wl-copy"
            command.write_text(
                "#!/usr/bin/python3\nimport sys,time\nfrom pathlib import Path\n"
                "data=sys.stdin.buffer.read()\ntime.sleep(.7)\n"
                f"Path({str(captured)!r}).write_bytes(data)\n"
            )
            command.chmod(0o700)
            t = self.open(
                server,
                environment=dict(
                    WAYLAND_DISPLAY="papo-synthetic-display",
                    PATH=folder + os.pathsep + os.environ.get("PATH", ""),
                ),
            )
            t.send("conteúdo antigo\x1bOH\x1b[1;2F")
            self.wait_field(t, "conteúdo antigo")
            first, _ = editor_region(t)
            self.wait_cursor(t, first, editor_column(t) + wcswidth("conteúdo antigo"))
            row, column = t.screen.cursor.y, t.screen.cursor.x
            self.mouse(t, row, column + 2, 2)
            self.mouse(t, row, column + 2, 2, release=True)
            t.send("Mensagem enquanto copia\n")
            self.wait(t, "Mensagem confirmada")
            self.wait(t, "Texto copiado")
            self.assertEqual(captured.read_text(encoding="utf-8"), "conteúdo antigo")
            self.assertEqual(server.sent_messages[-1]["content"], "Mensagem enquanto copia")
            self.assertNotIn(b"\x1b]52;c;", t.output)

    def test_native_clipboard_timeout_falls_back_without_blocking_input(self):
        with (
            tempfile.TemporaryDirectory(prefix="papo-clipboard-timeout-") as folder,
            HistoryMock(count=0) as server,
        ):
            command = Path(folder) / "wl-copy"
            command.write_text(
                "#!/usr/bin/python3\nimport sys,time\nsys.stdin.buffer.read()\ntime.sleep(10)\n"
            )
            command.chmod(0o700)
            t = self.open(
                server,
                environment=dict(
                    WAYLAND_DISPLAY="papo-synthetic-display",
                    PATH=folder + os.pathsep + os.environ.get("PATH", ""),
                ),
            )
            t.send("snapshot\x1bOH\x1b[1;2F")
            self.wait_field(t, "snapshot")
            first, _ = editor_region(t)
            self.wait_cursor(t, first, editor_column(t) + len("snapshot"))
            row, column = t.screen.cursor.y, t.screen.cursor.x
            self.mouse(t, row, column + 2, 2)
            self.mouse(t, row, column + 2, 2, release=True)
            t.send("nova mensagem\n")
            self.wait(t, "Mensagem confirmada")
            self.wait(t, "Texto copiado")
            self.wait_copied(t, "snapshot")

    def test_selection_survives_pending_confirmation_and_channel_drafts(self):
        with HistoryMock(count=0) as server:
            server.scenario = "send-delayed"
            t = self.open(server)
            t.send("prefixo enviado\n continuação selecionada\x1bOH\x1b[1;2F")
            self.wait(t, "Mensagem confirmada")
            t.send("Novo\n")
            deadline = time.monotonic() + 5
            while len(server.sent_messages) < 2 and time.monotonic() < deadline:
                t.read(0.03)
            self.assertEqual(
                [m["content"] for m in server.sent_messages], ["prefixo enviado", "Novo"]
            )

    def test_drag_multiline_and_visual_home_end_do_not_switch_focus(self):
        with HistoryMock(count=0) as server:
            t = self.open(server)
            t.send("primeira\x0esegunda\x0eterceira")
            self.wait_field(t, "terceira")
            first, end = editor_region(t)
            start = editor_column(t)
            ordinary = t.screen.buffer[first][start + 3]
            self.mouse(t, first, start + 3, 0)
            self.mouse(t, first + 1, start + 4, 32)
            self.wait_selected(
                t, ((first, start + 3), (first, start + 6), (first + 1, start + 1)), ordinary
            )
            for row, column in ((first, start + 3), (first, start + 6), (first + 1, start + 1)):
                selected = t.screen.buffer[row][column]
                self.assertNotEqual((selected.fg, selected.bg), (ordinary.fg, ordinary.bg))
            unselected = t.screen.buffer[first + 1][start + 5]
            self.assertEqual((unselected.fg, unselected.bg), (ordinary.fg, ordinary.bg))
            self.mouse(t, first + 1, start + 4, 0, release=True)
            t.send("X")
            self.wait_field(t, "priXnda")
            self.wait_field(t, "terceira")
            t.send("\x1bOHY\x1bOFZ\x1bOA\x1bOB\n")
            self.wait(t, "Mensagem confirmada")
            self.assertEqual(server.sent_messages[-1]["content"], "YpriXndaZ\nterceira")

    def test_copy_shortcut_preserves_selection_and_clears_only_on_ctrl_c(self):
        with HistoryMock(count=0) as server:
            t = self.open(server)
            t.send("primeira\x0esegunda\x1b[1;5H\x1b[1;6F")
            for sequence in ("\x1b[99;6u", "\x1b[67;6u", "\x1b[27;6;67~"):
                t.send(sequence)
                self.wait(t, "Texto copiado")
                self.wait_copied(t, "primeira\nsegunda")
                self.wait_field(t, "primeira")
                self.wait_field(t, "segunda")
            t.send("Novo")
            self.wait_field(t, "Novo")
            self.assertNotIn("primeira", self.field(t))
            t.send("\x1b[49;5u")
            self.assertIsNone(t.process.poll())
            self.wait_field(t, "Novo")
            t.send("\x1b[99;5u")
            self.wait(t, "Conversar em #geral")

    def test_terminal_drag_mode_without_keyboard_protocol_support(self):
        with HistoryMock(count=0) as server:
            t = self.open(server)
            self.assertNotIn(b"\x1b[>1u", t.output)
            # PTYs can inject motion even when the terminal was never asked to
            # emit it. Check the actual mode request as well as live highlight.
            last_mouse_enable = bytes(t.output).rfind(b"\x1b[?1006;1000h")
            self.assertIn(b"\x1b[?1002h", t.output[last_mouse_enable:])
            t.send("\x1b[17;129~")
            self.wait(t, "[Canais / F6]")
            self.assertIn(b"\x1b[?1002h", t.output[bytes(t.output).rfind(b"\x1b[?1006;1000h") :])
            t.send("\x1b[113;5u")
            t.process.wait(timeout=5)
            t.read()
            self.assertNotIn(b"\x1b[<u", t.output)
            self.assertIn(b"\x1b[?1002l", t.output)

    def test_ctrl_v_with_lock_modifiers_does_not_corrupt_input_or_commands(self):
        with HistoryMock(count=0) as server:
            t = self.open(server)
            for locks in (0, 64, 128, 192):
                ctrl, shift_ctrl = 5 + locks, 6 + locks
                t.send("original")
                self.wait_field(t, "original")
                first, _ = editor_region(t)
                self.wait_cursor(t, first, editor_column(t) + len("original"))
                position = (t.screen.cursor.y, t.screen.cursor.x)
                t.send(f"\x1b[118;{ctrl}uX")
                self.wait_field(t, "originalX")
                t.send("\x7f")
                self.wait_cursor(t, *position)
                self.wait(t, "Foco: Mensagem")
                self.wait_field(t, "original")
                self.assertEqual((t.screen.cursor.y, t.screen.cursor.x), position)
                self.assertNotIn(f"{ctrl}u", self.field(t))
                t.send("\x1b[200~ colagem\r\nsegunda\x1b[201~")
                self.wait_field(t, "original colagem")
                self.wait_field(t, "segunda")
                t.send(f"\x1b[1;{ctrl}H\x1b[1;{shift_ctrl}F\x1b[99;{shift_ctrl}u")
                self.wait_copied(t, "original colagem\nsegunda")
                t.send(f"\x1b[99;{ctrl}u")
                self.wait(t, "Conversar em #geral")
            t.send("\x1b[113;133u")
            t.process.wait(timeout=5)

    def test_unknown_keyboard_packets_never_become_text_or_escape_focus(self):
        with HistoryMock(count=0) as server:
            t = self.open(server)
            t.send("preservado")
            self.wait_field(t, "preservado")
            expected = "preservado"
            for packet in ("\x1b[57361;133u", "\x1b[99;133:3u", "\x1b[?1u"):
                expected += "X"
                t.send(packet + "X")
                self.wait_field(t, expected)
                self.wait(t, "Foco: Mensagem")
                self.wait_field(t, "preservado")
                self.assertNotIn("133", self.field(t))
            t.send("\x03")
            self.wait(t, "Conversar em #geral")

    def test_locked_login_and_profile_controls_preserve_paste_and_focus(self):
        t = self.terminal()
        t.send("\x1b[9;129uusuário\x1b[118;133u")
        self.wait(t, "usuário")
        self.assertIn("usuário", t.screen.display[t.screen.cursor.y])
        self.assertNotIn("133u", self.screen(t))
        t.send("\x1b[117;133u\x1b[200~novo usuário\x1b[201~")
        self.wait(t, "novo usuário")
        self.assertNotIn("usuárionovo", self.screen(t))
        p = self.terminal(arguments=("--profiles",))
        p.send("\x0e")
        self.wait(p, "Nome local")
        p.send("perfil\x1b[118;133u\x1b[57361;133u")
        self.assertNotIn("133", self.screen(p))
        self.wait(p, "perfil")
        p.send("\x1b[99;133u\x1b[200~novo perfil\x1b[201~")
        self.wait(p, "novo perfil")
        self.assertNotIn("perfilnovo", self.screen(p))

    def test_live_drag_across_soft_wrap_and_upward_across_lines(self):
        with HistoryMock(count=0) as server:
            t = self.open(server)
            start = editor_column(t)
            wrap = t.screen.columns - start - 18  # Reserve both toolbar buttons.
            t.send("a" * wrap + "bravo")
            self.wait_field(t, "bravo")
            first, end = editor_region(t)
            self.assertEqual(end - first, 2)
            ordinary = t.screen.buffer[first][start]
            self.mouse(t, first, start + wrap - 4, 0)
            self.mouse(t, first + 1, start + 3, 32)
            self.wait_selected(t, ((first, start + wrap - 3), (first + 1, start + 1)), ordinary)
            for row, column in ((first, start + wrap - 3), (first + 1, start + 1)):
                selected = t.screen.buffer[row][column]
                self.assertNotEqual((selected.fg, selected.bg), (ordinary.fg, ordinary.bg))
            self.mouse(t, first + 1, start + 3, 0, release=True)
            t.send("\x1b[99;6u")
            self.wait_copied(t, "aaaabra")
            t.send("X")
            self.wait_field(t, "Xvo")
            t.send("\x03um\x0edois\x0etrês")
            self.wait_field(t, "três")
            first, _ = editor_region(t)
            self.mouse(t, first + 2, start + 3, 0)
            self.mouse(t, first, start, 32)
            t.send("\x1b[99;6u")
            self.wait_copied(t, "um\ndois\ntrê")
            self.mouse(t, first, start, 0, release=True)

    def test_repainting_never_shows_cursor_in_history(self):
        with RealtimeMock(count=5) as server:
            t = self.open(server)
            t.send("rascunho")
            self.wait_field(t, "rascunho")
            observed = len(t.screen.visible_cursor_rows)
            t.send("X")
            self.wait_field(t, "rascunhoX")
            self.assertTrue(
                all(row == editor_region(t)[0] for row in t.screen.visible_cursor_rows[observed:]),
                t.screen.visible_cursor_rows[observed:],
            )
            t.send("\x1b")
            wait_for_screen(t, lambda: t.screen.cursor.hidden, "History cursor is visible")
            self.assertTrue(t.screen.cursor.hidden)
            observed = len(t.screen.visible_cursor_rows)
            server.emit(
                dict(server.message(6), type="message", content="Mensagem nova no histórico")
            )
            self.wait(t, "Mensagem nova no histórico")
            self.assertFalse(t.screen.visible_cursor_rows[observed:])

    def test_selection_remains_visible_without_terminal_colors(self):
        with HistoryMock(count=0) as server:
            t = self.open(server, terminal_type="vt100")
            t.send("seleção")
            self.wait_field(t, "seleção")
            row, _ = editor_region(t)
            self.wait_cursor(t, row, editor_column(t) + wcswidth("seleção"))
            t.send("\x1b[1;5H\x1b[1;6F")
            wait_for_screen(
                t,
                lambda: t.screen.buffer[row][editor_column(t)].reverse,
                "Monochrome selection was not rendered",
            )
            self.wait_field(t, "seleção")
            self.assertTrue(t.screen.buffer[row][editor_column(t)].reverse)
            t.send("Novo\n")
            self.wait(t, "Mensagem confirmada")
            self.assertEqual(server.sent_messages[-1]["content"], "Novo")


if __name__ == "__main__":
    test_main(
        1,
    )
