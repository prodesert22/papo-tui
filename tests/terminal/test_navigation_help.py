"""Global and focused help tables through a real PTY, with synthetic HTTP/WS."""

from test_context import main as test_main
from channel_preferences import ChannelPreferencesMock
from terminal_history import HistoryTerminal
from terminal_support import Terminal, wait_for_screen, editor_region


class NavigationHelp(HistoryTerminal):
    def terminal(self, arguments=()):
        t = Terminal(arguments=arguments)
        self.addCleanup(lambda: t.close() if t.process.poll() is None else None)
        t.resize(40, 140)
        return t

    def open(self, server):
        t = self.terminal()
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "Conversar em #geral")
        t.click(t.screen.lines - 7, 32)
        self.wait(t, "Foco: Mensagem")
        return t

    def screen(self, t):
        return "\n".join(t.screen.display)

    def help(self, t, area, packet="\x07"):
        t.send(packet)
        self.wait(t, "Ajuda do contexto")
        self.wait(t, area)
        self.wait(t, "Combinação de teclas")
        self.wait(t, "Função")

    def assert_table_grid(self, t, entries=True):
        header = next(
            i for i, line in enumerate(t.screen.display) if "Combinação de teclas" in line
        )
        left = t.screen.display[header].index("Combinação de teclas") - 1
        middle = t.screen.display[header].index("Função") - 2
        top = t.screen.display[header - 1]
        self.assertIn(top[left], ("l", "┌"))
        self.assertIn(top[middle], ("w", "┬"))
        right = next(i for i in range(middle + 1, len(top)) if top[i] in ("k", "┐"))
        wait_for_screen(
            t,
            lambda: any(
                line[left] in ("m", "└") and line[right] in ("j", "┘")
                for line in t.screen.display[header + 2 :]
            ),
            "Help table bottom border did not arrive",
        )
        bottom = next(
            i
            for i in range(header + 2, t.screen.lines)
            if t.screen.display[i][left] in ("m", "└") and t.screen.display[i][right] in ("j", "┘")
        )
        self.assertIn(t.screen.display[bottom][middle], ("v", "┴"))
        dividers = 0
        for y in range(header, bottom):
            # Screen columns include the second cell of wide emoji glyphs.
            line = [t.screen.buffer[y][x].data for x in range(t.screen.columns)]
            self.assertIn(line[left], ("x", "│", "t", "├"))
            self.assertIn(line[right], ("x", "│", "u", "┤"))
            self.assertIn(line[middle], ("x", "│", "n", "┼"))
            if line[left] in ("t", "├"):
                dividers += 1
                self.assertIn(line[middle], ("n", "┼"))
                self.assertIn(line[right], ("u", "┤"))
                self.assertTrue(all(ch in ("q", "─") for ch in line[left + 1 : middle]))
                self.assertTrue(all(ch in ("q", "─") for ch in line[middle + 1 : right]))
        self.assertGreaterEqual(dividers, 2 if entries and t.screen.lines >= 24 else 1)

    def test_context_filters_editor_history_channels_and_members(self):
        with ChannelPreferencesMock() as server:
            t = self.open(server)
            t.send("Preserved help draft")
            self.help(t, "Editor de mensagens")
            self.assertIn("Ctrl+N", self.screen(t))
            self.assertNotIn("Ctrl+S", self.screen(t))
            t.send("\x1b")
            self.wait(t, "Preserved help draft")
            t.send("\x1b")
            self.wait(t, "Foco: Histórico")
            self.help(t, "Histórico da conversa")
            self.assertIn("PgUp / PgDn", self.screen(t))
            self.assertNotIn("Ctrl+N", self.screen(t))
            t.send("\x1b\x0b")
            self.wait(t, "Foco: Seleção")
            self.help(t, "Seleção de mensagens")
            self.assertIn("r / e / E", self.screen(t))
            self.assertNotIn("PgUp / PgDn", self.screen(t))
            t.send("\x1b\x0b\x1b")
            self.wait(t, "Foco: Canais")
            self.help(t, "Lista de canais")
            self.assertIn("Ctrl+O", self.screen(t))
            t.send("\x1b\x15")
            self.wait(t, "Membros (")
            self.help(t, "Lista de membros")
            self.assertNotIn("Ctrl+N", self.screen(t))
            t.send("\x1b/")
            self.help(t, "Busca de membros")
            self.assertIn("Ctrl+W", self.screen(t))
            t.send("\x1b\x15")
            self.wait(t, "Preserved help draft")
            self.assertEqual(server.sent_messages, [])

    def test_global_table_switching_scroll_resize_and_keyboard_buttons(self):
        with ChannelPreferencesMock() as server:
            t = self.open(server)
            self.help(t, "Editor de mensagens")
            self.assert_table_grid(t)
            t.send("\t\n")
            self.wait(t, "Ajuda global")
            self.assert_table_grid(t)
            self.assertIn("Ctrl+S", self.screen(t))
            self.assertIn("Ctrl+F", self.screen(t))
            t.send("\t\x1bOF")
            self.wait(t, "Colar texto")
            t.resize(14, 44)
            self.wait(t, "Combinação de teclas")
            self.wait(t, "Função")
            self.assert_table_grid(t)
            t.resize(40, 140)
            self.wait(t, "Colar texto")
            self.assert_table_grid(t)
            t.send("\x1bOH")
            self.wait(t, "Ctrl+G / F1")
            t.send("\x07")
            self.wait(t, "Ajuda do contexto")
            self.assertNotIn("Ctrl+S", self.screen(t))
            t.send("\x1b")
            self.wait(t, "Conversar em #geral")

    def test_global_search_filters_keys_functions_and_preserves_the_draft(self):
        with ChannelPreferencesMock() as server:
            t = self.open(server)
            t.send("Draft behind search\x1f")
            self.wait(t, "Ajuda global")
            self.wait(t, "Buscar:")
            t.send("CtRl+Y")
            self.wait(t, "Identificar novamente")
            self.assertNotIn("Ctrl+S", self.screen(t))
            self.assertIn("Tentar novamente", self.screen(t))
            self.assert_table_grid(t)
            t.send("\x15NOTIFICAÇÕES")
            self.wait(t, "Escolher uma notificação")
            self.assertNotIn("Ctrl+Y", self.screen(t))
            t.send("\x03zzzzzzmissing")
            self.wait(t, "Nenhum atalho encontrado.")
            self.assert_table_grid(t, entries=False)
            t.send("\n\x06\x03\x1b[200~CtRl+Y\x1b[201~")
            self.wait(t, "Identificar novamente")
            t.send("\t")
            row = next(i for i, line in enumerate(t.screen.display) if "Buscar:" in line)
            t.click(row, t.screen.display[row].index("Buscar:") + 9)
            t.send("\x15CtRl+Y")
            self.wait(t, "Identificar novamente")
            t.resize(14, 44)
            self.wait(t, "CtRl+Y")
            self.assert_table_grid(t)
            t.resize(40, 140)
            self.wait(t, "Identificar novamente")
            t.send("\x07")
            self.wait(t, "Editor de mensagens")
            self.assertNotIn("Buscar:", self.screen(t))
            t.send("\x1f")
            self.wait(t, "Identificar novamente")
            t.send("\x1b")
            self.wait(t, "Draft behind search")
            t.send("\x1f")
            self.wait(t, "Ctrl+G / F1")
            self.assertNotIn("CtRl+Y", self.screen(t))
            t.send("\x1b")
            self.wait(t, "Draft behind search")
            self.assertEqual(server.sent_messages, [])
            self.assertEqual(server.setting_calls, [])

    def test_help_blocks_background_commands_and_bracketed_paste(self):
        with ChannelPreferencesMock() as server:
            t = self.open(server)
            t.send("Protected draft")
            self.help(t, "Editor de mensagens")
            t.send("\x13\x06\x02\x15\x04\x14\x0f\x05\x19\x0b")
            self.wait(t, "Ajuda do contexto")
            t.send("\x1b[200~ignored\x07\x14\n\x1b[201~")
            self.wait(t, "Ajuda do contexto")
            t.send("\x1b")
            self.wait(t, "Protected draft")
            row, _last = editor_region(t)
            self.assertNotIn("ignored", "\n".join(t.screen.display[row:]))
            self.assertIn("Foco: Mensagem", self.screen(t))
            self.assertEqual(server.sent_messages, [])
            self.assertEqual(server.setting_calls, [])
            self.assertEqual(server.marked, [])

    def test_nested_emoji_and_channel_menus_resume_after_both_help_dialogs(self):
        with ChannelPreferencesMock() as server:
            t = self.open(server)
            t.send("\x05")
            self.wait(t, "Emojis · Esc voltar")
            self.help(t, "Seletor de emojis")
            t.send("\x1f")
            self.wait(t, "Ajuda global")
            t.send("\x1f")
            self.wait(t, "Emojis · Esc voltar")
            self.assertNotIn("Ajuda global", self.screen(t))
            t.send("\x1b")
            self.wait(t, "Conversar em #geral")
            t.send("\x1b[<2;5;8M\x1b[<2;5;8m")
            self.wait(t, "Silenciar canal…")
            self.help(t, "Menu de contexto")
            t.send("\x1b")
            self.wait(t, "Silenciar canal…")
            t.send("\x1b")
            t.send("\x00")
            self.wait(t, "Silenciar canal…")
            t.send("\x00")
            self.wait(t, "Conversar em #geral")
            self.assertEqual(server.setting_calls, [])

    def test_login_profiles_and_csi_u_control_shortcuts(self):
        t = self.terminal()
        t.send("https://help.example.test")
        for locks in (0, 64, 128, 192):
            self.help(t, "Login e cadastro", f"\x1b[103;{5 + locks}u")
            t.send(f"\x1b[47;{5 + locks}u")
            self.wait(t, "Ajuda global")
            if locks == 0:
                t.send("\x1b[200~Ctrl+Y\x1b[201~")
                self.wait(t, "Identificar novamente")
            t.send("\x1b")
            self.wait(t, "https://help.example.test")
        t.send("\x1bOP")
        self.wait(t, "Ajuda do contexto")
        t.send("\x1b\x10")
        self.wait(t, "Selecione um perfil")
        self.help(t, "Perfis de servidores")
        t.send("\x1f")
        self.wait(t, "Ajuda global")
        t.send("\x1b[200~zzzzmissing\x1b[201~")
        self.wait(t, "Nenhum atalho encontrado.")
        t.send("\x11")
        wait_for_screen(t, lambda: t.process.poll() is not None, "Quit was blocked by help")
        self.assertEqual(t.process.returncode, 0)

    def test_obsolete_function_keys_are_inert_and_footer_opens_context_help(self):
        with ChannelPreferencesMock() as server:
            t = self.open(server)
            t.send("Unchanged draft")
            for packet in (
                "\x1bOQ",
                "\x1bOR",
                "\x1bOS",
                "\x1b[15~",
                "\x1b[17~",
                "\x1b[18~",
                "\x1b[19~",
                "\x1b[20~",
                "\x1b[21~",
            ):
                t.send(packet)
                self.wait(t, "Unchanged draft")
                self.assertIn("Foco: Mensagem", self.screen(t))
            footer = t.screen.display[-2]
            self.assertIn("Ctrl+G: ajuda", footer)
            self.assertNotIn("Enter enviar", footer)
            t.click(t.screen.lines - 2, footer.index("Ctrl+G") + 2)
            self.wait(t, "Ajuda do contexto")
            self.wait(t, "Editor de mensagens")


if __name__ == "__main__":
    test_main(1)
