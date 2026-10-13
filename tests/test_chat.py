"""R2 acceptance scenarios on a real PTY, using only the local demonstration."""
import signal
import time
import unittest

from test_terminal import Terminal

UP, DOWN, LEFT, RIGHT = "\x1bOA", "\x1bOB", "\x1bOD", "\x1bOC"
HOME, END, F1, F2 = "\x1bOH", "\x1bOF", "\x1bOP", "\x1bOQ"
PGUP, PGDOWN, DELETE = "\x1b[5~", "\x1b[6~", "\x1b[3~"


class ChatTests(unittest.TestCase):
    def terminal(self, **kwargs):
        terminal = Terminal(("--demo",), **kwargs)
        self.addCleanup(self.cleanup_terminal, terminal)
        return terminal

    def cleanup_terminal(self, terminal):
        if not getattr(terminal, "closed", False):
            self.assertTrue(terminal.close(exit_key="\x03"))
            terminal.closed = True
            self.assertEqual(terminal.process.returncode, 0)

    def screen(self, terminal):
        return "\n".join(terminal.screen.display)

    def wait(self, terminal, text, *, absent=False):
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            if (text in self.screen(terminal)) != absent:
                return
            terminal.read(0.03)
        self.fail(f"screen expectation failed: {text!r}, absent={absent}\n{self.screen(terminal)}")

    def focus(self, terminal, name):
        self.wait(terminal, "Foco: " + name)

    def navigate(self, terminal, label):
        terminal.send("\x0b" + label + "\n")
        self.focus(terminal, "Histórico")

    def test_layout_contextual_arrows_and_scrolling(self):
        terminal = self.terminal()
        self.assertIn("[Comunidade]", terminal.screen.display[1])
        self.assertIn("Canais / DMs", terminal.screen.display[3][:25])
        self.assertIn("Histórico", terminal.screen.display[3][26:])
        self.assertIn("Editor", terminal.screen.display[16])
        terminal.send(RIGHT)
        self.focus(terminal, "Abas")
        self.assertIn("[Trabalho]", terminal.screen.display[1])
        terminal.send(DOWN)
        self.focus(terminal, "Canais")
        self.assertIn("> # equipe", self.screen(terminal))
        terminal.send(DOWN + RIGHT)
        self.focus(terminal, "Histórico")
        self.assertIn("# planejamento", terminal.screen.display[3])
        terminal.send(HOME)
        self.assertIn("10:32 Ana", terminal.screen.display[6])
        terminal.send(DOWN)
        self.focus(terminal, "Histórico")
        self.assertIn("Olá!", terminal.screen.display[6])
        terminal.send(LEFT)
        self.focus(terminal, "Canais")
        terminal.send(UP + UP)
        self.focus(terminal, "Abas")

    def test_mouse_typing_caret_and_release_do_not_repeat(self):
        terminal = self.terminal()
        terminal.click(6, 8)  # Second channel: projetos.
        self.focus(terminal, "Histórico")
        self.assertIn("# projetos", terminal.screen.display[3])
        terminal.send("abc")
        self.focus(terminal, "Editor")
        self.assertIn("abc", terminal.screen.display[17])
        terminal.click(17, 30)
        terminal.send("X")
        self.assertIn("aXbc", terminal.screen.display[17])
        terminal.click(6, 40)
        self.focus(terminal, "Histórico")
        terminal.click(23, 0)  # Outside controls.
        self.focus(terminal, "Histórico")
        terminal.send("Z")
        self.assertIn("aXZbc", terminal.screen.display[17])

    def test_multiline_paste_never_sends_and_preserves_utf8(self):
        terminal = self.terminal()
        terminal.send(DOWN + RIGHT)
        terminal.send("\x1b[200~ação界語\r\nsegunda\rterceira\tlinha\x1b[201~")
        self.focus(terminal, "Editor")
        self.assertIn("ação界語", terminal.screen.display[17])
        self.assertIn("segunda", terminal.screen.display[18])
        self.assertIn("terceira linha", terminal.screen.display[19])
        self.assertNotIn("Agora · Você", self.screen(terminal))
        terminal.send("\n")
        self.assertIn("Mensagem adicionada", self.screen(terminal))
        self.assertIn("segunda", self.screen(terminal))
        self.assertIn("terceira linha", self.screen(terminal))
        self.assertNotIn("terceira linha", terminal.screen.display[19])

    def test_drafts_are_isolated_by_server_and_channel(self):
        terminal = self.terminal()
        self.navigate(terminal, "Comunidade / # projetos")
        terminal.send("primeiro")
        self.navigate(terminal, "Trabalho / # equipe")
        self.assertNotIn("primeiro", terminal.screen.display[17])
        terminal.send("segundo")
        self.navigate(terminal, "Comunidade / # projetos")
        self.assertIn("primeiro", terminal.screen.display[17])
        self.navigate(terminal, "Comunidade / # geral")
        self.assertNotIn("primeiro", terminal.screen.display[17])
        self.navigate(terminal, "Trabalho / # equipe")
        self.assertIn("segundo", terminal.screen.display[17])

    def test_split_paste_controls_mouse_and_resize_do_not_trigger_actions(self):
        terminal = self.terminal()
        terminal.send(DOWN + RIGHT + "\x1b[200~um\r")
        terminal.click(6, 8)
        terminal.resize(30, 100)
        terminal.send("\ny\tz\x00\x03\x11" + F2 + "\x1b[201~")
        self.focus(terminal, "Editor")
        self.assertIn("# geral", self.screen(terminal))
        self.assertIn("um", self.screen(terminal))
        self.assertIn("y z", self.screen(terminal))
        self.assertNotIn("Esta mensagem acabou de chegar", self.screen(terminal))
        self.assertNotIn("Sair da demonstração", self.screen(terminal))
        terminal.send(HOME + UP + END)
        self.assertEqual(terminal.screen.cursor.y, 23)

    def test_narrow_panels_and_resize_preserve_draft(self):
        terminal = self.terminal()
        terminal.send(DOWN + RIGHT + "ação界語\x0esegunda")
        terminal.resize(18, 60)
        self.focus(terminal, "Editor")
        self.assertIn("ação界語", self.screen(terminal))
        terminal.send("\t")
        self.focus(terminal, "Canais")
        self.assertNotIn("Editor", self.screen(terminal))
        terminal.send("\t")
        self.focus(terminal, "Histórico")
        self.assertIn("segunda", self.screen(terminal))
        terminal.send(LEFT + LEFT)
        self.focus(terminal, "Abas")
        terminal.send(DOWN + RIGHT)
        self.focus(terminal, "Histórico")
        terminal.resize(2, 18)
        self.assertIn("Amplie", terminal.screen.display[0])
        self.assertEqual(terminal.screen.display[1].rstrip(), "Ctrl+Q: sair")
        terminal.resize(14, 44)
        self.assertIn("segunda", self.screen(terminal))
        terminal.resize(30, 100)
        self.assertIn("ação界語", self.screen(terminal))
        terminal.send("X")
        self.assertIn("segundaX", self.screen(terminal))

    def test_help_filter_empty_results_and_modals_keep_editor(self):
        terminal = self.terminal()
        terminal.send(DOWN + RIGHT + "texto" + LEFT)
        # LEFT is an editor movement, not a server switch.
        terminal.send(F1)
        self.assertIn("Ajuda", self.screen(terminal))
        terminal.send(PGDOWN)
        self.assertIn("HISTÓRICO", self.screen(terminal))
        terminal.resize(18, 60)
        self.assertIn("Ajuda", self.screen(terminal))
        terminal.send("\x1b")
        self.focus(terminal, "Editor")
        terminal.send("X")
        self.assertIn("textXo", self.screen(terminal))
        terminal.send("\x0binexistente")
        self.assertIn("Nenhum resultado", self.screen(terminal))
        terminal.send("\x15ESTUDOS / # DÚVIDAS\n")
        self.focus(terminal, "Histórico")
        self.assertIn("[Estudos]", self.screen(terminal))
        self.assertIn("Nenhuma mensagem", self.screen(terminal))

    def test_history_arrivals_and_resize_keep_reading_anchor(self):
        terminal = self.terminal()
        terminal.send(DOWN + RIGHT + HOME + DOWN)
        before = terminal.screen.display[6][28:].rstrip()
        self.assertIn("Olá", before)
        terminal.send(F2)
        self.assertEqual(terminal.screen.display[6][28:].rstrip(), before)
        self.assertIn("1 novas", terminal.screen.display[3])
        terminal.resize(18, 60)
        self.assertIn("Olá", terminal.screen.display[6])
        terminal.resize(24, 80)
        self.assertEqual(terminal.screen.display[6][28:].rstrip(), before)
        terminal.send(END)
        self.assertNotIn("1 novas", self.screen(terminal))
        self.assertIn("Esta mensagem acabou", self.screen(terminal))

    def test_empty_loading_and_recoverable_error(self):
        terminal = self.terminal()
        self.navigate(terminal, "Comunidade / # ajuda")
        self.assertIn("Nenhuma mensagem", self.screen(terminal))
        self.navigate(terminal, "Comunidade / # ideias")
        self.wait(terminal, "Carregando")
        self.wait(terminal, "Carregando", absent=True)
        self.navigate(terminal, "Comunidade / # eventos")
        self.assertIn("Erro simulado", self.screen(terminal))
        terminal.send("r")
        self.focus(terminal, "Editor")
        self.assertIn("r", terminal.screen.display[17])
        terminal.send("\x1b\x12")
        self.wait(terminal, "Carregando", absent=True)
        self.assertNotIn("Erro simulado", self.screen(terminal))
        self.assertIn("r", terminal.screen.display[17])

    def test_tabs_overflow_and_narrow_mouse_panels(self):
        terminal = self.terminal()
        terminal.click(1, 77)
        self.assertIn("Laboratório", terminal.screen.display[1])
        row = terminal.screen.display[1]
        terminal.click(1, row.index("Laboratório") + 2)
        self.focus(terminal, "Canais")
        terminal.resize(18, 60)
        row = terminal.screen.display[2]
        terminal.click(2, row.index("[Conversa]") + 3)
        self.focus(terminal, "Histórico")
        row = terminal.screen.display[2]
        terminal.click(2, row.index("[Canais]") + 3)
        self.focus(terminal, "Canais")
        terminal.resize(14, 44)
        terminal.send(DOWN * 7)
        self.assertIn("@ Marco", self.screen(terminal))

    def test_wide_caret_combining_marks_and_backspace(self):
        terminal = self.terminal()
        terminal.send(DOWN + RIGHT + "ação界語")
        self.assertEqual((terminal.screen.cursor.y, terminal.screen.cursor.x), (17, 37))
        terminal.send("\x7f")
        self.assertIn("ação界", terminal.screen.display[17])
        self.assertEqual(terminal.screen.cursor.x, 35)
        terminal.send("\x15cafe\u0301")
        terminal.send("\x7f")
        self.assertIn("caf", terminal.screen.display[17])
        self.assertNotIn("café", terminal.screen.display[17])
        terminal.send("\x15👨‍👩‍👧‍👦")
        self.assertEqual(terminal.screen.cursor.x, 30)
        terminal.send("\x7f")
        self.assertEqual(terminal.screen.cursor.x, 29)

    def test_field_and_message_limits_keep_unsent_draft(self):
        terminal = self.terminal()
        terminal.send(DOWN + RIGHT + "\x1b[200~" + "A" * 2100 + "\x1b[201~")
        self.wait(terminal, "Colagem limitada")
        self.assertNotIn("Agora · Você", self.screen(terminal))
        terminal.send("\x15")
        terminal.send(F2 * 32)
        terminal.send("fica\n")
        self.wait(terminal, "Limite de 32 mensagens")
        self.assertIn("fica", terminal.screen.display[17])

    def test_monochrome_focus_and_errors_have_text_labels(self):
        terminal = self.terminal(terminal_type="vt100")
        self.focus(terminal, "Abas")
        terminal.send(DOWN + RIGHT)
        self.focus(terminal, "Histórico")
        self.navigate(terminal, "Comunidade / # eventos")
        self.assertIn("Erro simulado", self.screen(terminal))

    def test_konsole_palette_and_subtle_focus_background(self):
        terminal = self.terminal(environment={"KONSOLE_VERSION": "260802"})
        self.assertNotIn(b"\x1b]4;", terminal.output)
        self.assertIn(b"38;5;215", terminal.output)
        before = terminal.screen.buffer[3][2]
        terminal.send(DOWN)
        self.focus(terminal, "Canais")
        after = terminal.screen.buffer[3][2]
        self.assertEqual(before.fg, after.fg)
        self.assertNotEqual(before.bg, after.bg)

    def test_add_notice_and_mouse_wheel_in_history_and_help(self):
        terminal = self.terminal()
        terminal.send(RIGHT * 6 + "\n")
        self.assertIn("Adicionar servidor", self.screen(terminal))
        terminal.send("\x1b" + DOWN + RIGHT + HOME)
        self.focus(terminal, "Histórico")
        before = terminal.screen.display[6]
        terminal.send("\x1b[<65;41;8M")
        self.assertNotEqual(before, terminal.screen.display[6])
        terminal.send(F1)
        before = self.screen(terminal)
        terminal.send("\x1b[<65;41;8M")
        self.assertNotEqual(before, self.screen(terminal))
        terminal.send("\x1b")
        self.focus(terminal, "Histórico")

    def test_exit_confirmation_preserves_draft_and_restores_terminal(self):
        terminal = self.terminal()
        terminal.send(DOWN + RIGHT + "rascunho\x11")
        self.assertIn("Sair da demonstração", self.screen(terminal))
        terminal.send("\n")  # Default is Voltar.
        self.focus(terminal, "Editor")
        self.assertIn("rascunho", terminal.screen.display[17])
        terminal.send("\x11\x11")
        terminal.process.wait(timeout=4)
        self.assertTrue(terminal.close())
        terminal.closed = True
        self.assertEqual(terminal.process.returncode, 0)
        self.assertIn(b"\x1b[?2004l", terminal.output)
        self.assertIn(b"\x1b[?1049l", terminal.output)
        self.assertFalse(terminal.screen.cursor.hidden)

    def test_exit_keys_and_signals_restore_terminal(self):
        for exit_key, signal_number in (("\x11", None), ("\x03", None), (None, signal.SIGINT), (None, signal.SIGTERM)):
            with self.subTest(exit_key=exit_key, signal_number=signal_number):
                terminal = Terminal(("--demo",))
                try:
                    self.focus(terminal, "Abas")
                finally:
                    restored = terminal.close(signal_number=signal_number, exit_key=exit_key)
                self.assertTrue(restored)
                self.assertEqual(terminal.process.returncode, 0)
                self.assertFalse(terminal.screen.cursor.hidden)


if __name__ == "__main__":
    unittest.main()
