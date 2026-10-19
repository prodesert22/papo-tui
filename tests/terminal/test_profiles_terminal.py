"""R3 acceptance on a real PTY, with isolated public configuration and fixtures."""

from test_context import main as test_main

import json
from pathlib import Path
import tempfile
import time
import unittest

from mock_server import MockServer
from terminal_support import Terminal

UP, DOWN, LEFT, RIGHT = "\x1bOA", "\x1bOB", "\x1bOD", "\x1bOC"
HOME, END, PGUP, PGDOWN, F5 = "\x1bOH", "\x1bOF", "\x1b[5~", "\x1b[6~", "\x1b[15~"


class ProfileTerminalTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="papo-profiles-pty-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.path = self.root / "papo-tui" / "config.json"

    def seed(self, urls=(), rate=10, burst=25):
        self.path.parent.mkdir(parents=True, mode=0o700, exist_ok=True)
        profiles = [
            {"id": f"{i + 1:032x}", "name": f"Perfil {i:02}", "url": url}
            for i, url in enumerate(urls)
        ]
        config = {
            "version": 2,
            "profiles": profiles,
            "selected_profile": None,
            "requests_per_second": rate,
            "request_burst": burst,
        }
        self.path.write_text(json.dumps(config))
        self.path.chmod(0o600)
        return config

    def terminal(self, *, manage=True, terminal_type="xterm-256color"):
        environment = dict(
            XDG_CONFIG_HOME=str(self.root),
            NO_PROXY="*",
            ALL_PROXY="",
            HTTP_PROXY="",
            HTTPS_PROXY="",
        )
        terminal = Terminal(
            ("--profiles",) if manage else (), environment=environment, terminal_type=terminal_type
        )
        self.addCleanup(self.close, terminal)
        return terminal

    def close(self, terminal):
        if not getattr(terminal, "closed", False):
            self.assertTrue(terminal.close(exit_key="\x11"))
            terminal.closed = True
            self.assertEqual(terminal.process.returncode, 0)
            self.assertNotIn(b"AddressSanitizer", terminal.output)
            self.assertNotIn(b"runtime error:", terminal.output)

    @staticmethod
    def screen(terminal):
        return "\n".join(terminal.screen.display)

    def wait(self, terminal, text, *, absent=False):
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if (text in self.screen(terminal)) != absent:
                return
            terminal.read(0.03)
        self.fail(f"screen expectation failed: {text!r}, absent={absent}\n{self.screen(terminal)}")

    def position(self, terminal, text):
        for row, line in enumerate(terminal.screen.display):
            column = line.find(text)
            if column >= 0:
                return row, column
        self.fail(f"Control absent: {text!r}\n{self.screen(terminal)}")

    def click(self, terminal, text):
        row, column = self.position(terminal, text)
        terminal.click(row, column + 1)

    def glyph(self, terminal, text):
        row, column = self.position(terminal, text)
        return terminal.screen.buffer[row][column]

    def config(self):
        return json.loads(self.path.read_text(encoding="utf-8"))

    def test_startup_login_button_modal_selection_and_cancel(self):
        self.seed(["http://127.0.0.1:9/papo", "http://127.0.0.1:10/other"])
        terminal = self.terminal(manage=False)
        self.wait(terminal, "URL do servidor")
        self.assertNotIn("Selecione um perfil", self.screen(terminal))
        self.click(terminal, "[Selecionar perfil]")
        self.wait(terminal, "Selecione um perfil")
        terminal.send("\x1b")
        self.wait(terminal, "URL do servidor")
        terminal.send("\x10" + DOWN + "\n")
        self.wait(terminal, "URL do servidor")
        self.assertIn("http://127.0.0.1:10/other", self.screen(terminal))
        self.assertEqual(terminal.screen.cursor.y, 9)
        self.assertEqual(self.config()["selected_profile"], f"{2:032x}")
        terminal.send(UP * 3)
        self.assertEqual(terminal.screen.cursor.y, 9)  # The locked URL is skipped.

    def test_add_rename_remove_confirmation_and_restart(self):
        terminal = self.terminal()
        self.click(terminal, "[+ Adicionar]")
        self.wait(terminal, "Adicionar perfil")
        terminal.send("Comunidade 界\thttp://127.0.0.1:9/papo\n\n")
        self.wait(terminal, "> Comunidade 界")
        first = self.config()["profiles"][0]["id"]
        terminal.send("\x05\x15Renomeado\n\n\n")
        self.wait(terminal, "> Renomeado")
        self.assertEqual(self.config()["profiles"][0]["id"], first)
        terminal.send("\x04\n")
        self.wait(terminal, "Remover perfil", absent=True)
        self.assertEqual(len(self.config()["profiles"]), 1)
        self.close(terminal)
        terminal = self.terminal()
        self.wait(terminal, "> Renomeado")
        terminal.send("\x04" + RIGHT + "\n")
        self.wait(terminal, "Nenhum perfil salvo")
        self.assertEqual(self.config()["profiles"], [])

    def test_list_scroll_resize_and_clamped_navigation(self):
        self.seed([f"http://127.0.0.1:9/{i}" for i in range(30)])
        terminal = self.terminal()
        terminal.send(END)
        self.wait(terminal, "> Perfil 29")
        self.assertNotIn("Perfil 00", self.screen(terminal))
        terminal.send(PGUP)
        self.wait(terminal, "> Perfil 18")
        terminal.resize(14, 44)
        self.wait(terminal, "> Perfil 18")
        terminal.send(END + DOWN)
        self.wait(terminal, "> Perfil 29")
        terminal.resize(30, 100)
        terminal.send(HOME + LEFT)
        self.wait(terminal, "↑↓: perfis")
        terminal.send(UP + LEFT + LEFT)
        self.wait(terminal, "←→: opções")
        self.assertIn("Perfil 00", self.screen(terminal))
        terminal.send(RIGHT + RIGHT)
        self.wait(terminal, "Adicionar uma instância")
        terminal.send(LEFT + DOWN + RIGHT + LEFT + LEFT)
        self.wait(terminal, "←→: ações")
        first = self.glyph(terminal, "[Selecionar]")
        terminal.send(RIGHT * 4 + RIGHT)
        self.assertEqual(self.glyph(terminal, "[Identificar]").fg, first.fg)
        self.assertEqual(self.glyph(terminal, "[Identificar]").bg, first.bg)
        terminal.send(UP + DOWN + "\n")
        self.wait(terminal, "←→: ações")
        self.assertIn("> Perfil 01", self.screen(terminal))

    def test_cancel_add_keeps_footer_and_shortcuts_disabled(self):
        self.seed(["http://127.0.0.1:9/papo"])
        terminal = self.terminal()
        self.click(terminal, "[+ Adicionar]")
        terminal.send("\x1b" + DOWN + "\t\x1b[Z\x05\x04\x0c")
        self.wait(terminal, "Selecione um perfil para habilitar")
        self.assertNotIn("[Selecionar]", self.screen(terminal))
        terminal.click(18, 3)
        self.assertNotIn("Editar perfil", self.screen(terminal))
        self.assertEqual(len(self.config()["profiles"]), 1)
        terminal.send(LEFT + DOWN)
        self.wait(terminal, "> Perfil 00")
        self.assertIn("[Selecionar]", self.screen(terminal))

    def test_rate_edit_invalid_input_default_restore_and_restart(self):
        self.seed(["http://127.0.0.1:9/papo"])
        terminal = self.terminal()
        terminal.send("\x0c\x152.5\t\x153\n\n")
        self.wait(terminal, "Limites de requisições", absent=True)
        self.assertEqual(
            (self.config()["requests_per_second"], self.config()["request_burst"]), (2.5, 3)
        )
        terminal.send("\x0c\x150\t\x154\n\n")
        self.wait(terminal, "taxa positiva")
        self.assertEqual(
            (self.config()["requests_per_second"], self.config()["request_burst"]), (2.5, 3)
        )
        terminal.send("\x1b\x0c\t\t" + RIGHT + "\n")
        self.wait(terminal, "Padrão preenchido")
        terminal.send("\t\t\n")
        self.wait(terminal, "Limites de requisições", absent=True)
        self.assertEqual(
            (self.config()["requests_per_second"], self.config()["request_burst"]), (10, 25)
        )
        self.close(terminal)
        terminal = self.terminal()
        self.wait(terminal, "10 req/s · burst 25")

    def test_mouse_actions_and_selected_text_colors(self):
        self.seed(["http://127.0.0.1:9/a", "http://127.0.0.1:9/b"])
        terminal = self.terminal()
        selected = self.glyph(terminal, "Perfil 00")
        self.assertNotEqual(selected.fg, self.glyph(terminal, "Perfil 01").fg)
        self.assertNotEqual(selected.bg, self.glyph(terminal, "Perfil 01").bg)
        self.click(terminal, "Perfil 01")
        terminal.send(RIGHT)
        self.assertEqual(
            self.glyph(terminal, "Perfil 01").fg, self.glyph(terminal, "[Selecionar]").fg
        )
        self.assertNotEqual(
            self.glyph(terminal, "Perfil 01").bg, self.glyph(terminal, "[Selecionar]").bg
        )
        self.assertNotEqual(
            self.glyph(terminal, "[Selecionar]").fg, self.glyph(terminal, "[Editar]").fg
        )
        self.click(terminal, "[Editar]")
        self.wait(terminal, "Editar perfil")
        self.assertEqual(self.glyph(terminal, "[Salvar]").fg, self.glyph(terminal, "[Cancelar]").fg)
        self.click(terminal, "[Cancelar]")
        self.wait(terminal, "Editar perfil", absent=True)
        self.assertEqual(self.config()["profiles"][1]["name"], "Perfil 01")

    def test_failure_correct_url_and_retry_without_restart(self):
        with MockServer("uninitialized") as server:
            self.seed([server.url])
            terminal = self.terminal()
            self.wait(terminal, "não inicializada")
            before = self.path.read_bytes()
            terminal.send("\x05\t\x15bad\n\n")
            self.wait(terminal, "URL válida")
            self.assertEqual(self.path.read_bytes(), before)
            terminal.send("\x1b")
            server.scenario = "public"
            terminal.send(F5)
            self.wait(terminal, "Pública")
            terminal.send("\x05\t\x15" + server.url + "/new-prefix\n\n")
            self.wait(terminal, "Editar perfil", absent=True)
            self.wait(terminal, "Pública")
            self.assertEqual(self.config()["profiles"][0]["url"], server.url + "/new-prefix")
            self.assertTrue(any(r[1] == "/papo/new-prefix/health" for r in server.requests))

    def test_late_discovery_and_remote_terminal_controls(self):
        with MockServer("slow-health") as source, MockServer() as target:
            source.name = "Stale response"
            target.name = "Current\x1b[2J\x07instance"
            self.seed([source.url, target.url])
            terminal = self.terminal()
            terminal.send(DOWN)
            self.wait(terminal, "Pública")
            terminal.read(0.7)
            self.assertNotIn("Stale response", self.screen(terminal))
            self.assertNotIn(b"Current\x1b[2J", terminal.output)
            self.assertIn("> Perfil 01", self.screen(terminal))
            self.assertEqual([r[1] for r in source.requests], ["/papo/health"])

    def test_switch_authenticated_profiles_clears_fields_cookies_and_socket(self):
        with MockServer() as source, MockServer() as target:
            self.seed([source.url, target.url])
            terminal = self.terminal(manage=False)
            terminal.send("\x10\njosé\tSynthetic!123\n")
            terminal.return_to_account()
            self.wait(terminal, "Tempo real conectado")
            terminal.send("\x10" + DOWN + "\n")
            self.wait(terminal, "URL do servidor")
            self.assertIn(target.url, self.screen(terminal))
            self.assertNotIn("josé", self.screen(terminal))
            terminal.send("josé\tSynthetic!123\n")
            terminal.return_to_account()
            self.wait(terminal, "Tempo real conectado")
            self.assertTrue(any(r[1].endswith("/ws") for r in source.requests))
            self.assertFalse(source.ws_connected.is_set())
            initial = next(r for r in target.requests if r[1].endswith("/auth/login"))
            self.assertIsNone(initial[2])
            for secret in (b"Synthetic!123", b"user-fixture", b"temporary-fixture"):
                self.assertNotIn(secret, terminal.output)
                self.assertNotIn(secret, self.path.read_bytes())

    def test_private_password_border_focus_mask_and_retry(self):
        with MockServer("private") as server:
            terminal = self.terminal(manage=False)
            terminal.send(server.url + "\tjosé\tSynthetic!123\n")
            self.wait(terminal, "Preencha a senha do servidor")
            self.assertEqual(terminal.screen.cursor.y, 15)
            self.assertIn("Senha do servidor *", self.screen(terminal))
            terminal.send("Wrong!123\n")
            self.wait(terminal, "Senha do servidor incorreta")
            self.assertEqual(terminal.screen.cursor.y, 15)
            terminal.send("Server!123\n")
            terminal.return_to_account()
            self.wait(terminal, "Login realizado")
            self.assertNotIn("Senha do servidor *", self.screen(terminal))
            for secret in (b"Synthetic!123", b"Wrong!123", b"Server!123"):
                self.assertNotIn(secret, terminal.output)

    def test_rate_queue_keeps_modal_resize_and_exit_responsive(self):
        with MockServer() as server:
            self.seed([server.url], rate=0.01, burst=1)
            terminal = self.terminal()
            self.wait(terminal, "Identificando")
            started = time.monotonic()
            terminal.send("\x05")
            self.wait(terminal, "Editar perfil")
            terminal.resize(14, 44)
            terminal.resize(24, 80)
            terminal.send("\x1b")
            self.close(terminal)
            self.assertLess(time.monotonic() - started, 2)
            self.assertEqual([r[1] for r in server.requests], ["/papo/health"])

    def test_invalid_config_can_be_reloaded_without_overwriting_it(self):
        self.seed()
        self.path.write_text("{broken")
        terminal = self.terminal()
        self.wait(terminal, "Configuração inválida")
        self.click(terminal, "[+ Adicionar]")
        terminal.send("New\thttp://127.0.0.1:9/papo\n\n")
        self.wait(terminal, "Configuração bloqueada")
        self.assertEqual(self.path.read_text(encoding="utf-8"), "{broken")
        terminal.send("\x1b")
        self.seed(["http://127.0.0.1:9/papo"])
        terminal.send("\x12" + LEFT + DOWN)
        self.wait(terminal, "> Perfil 00")

    def test_monochrome_navigation_and_terminal_restoration(self):
        self.seed(["http://127.0.0.1:9/papo", "http://127.0.0.1:9/other"])
        terminal = self.terminal(terminal_type="vt100")
        terminal.send(DOWN + RIGHT + RIGHT)
        self.wait(terminal, "←→: ações")
        self.assertTrue(self.glyph(terminal, "[Editar]").reverse)
        self.assertFalse(self.glyph(terminal, "[Selecionar]").reverse)
        terminal.send(UP)
        self.wait(terminal, "> Perfil 01")
        self.assertTrue(self.glyph(terminal, "Perfil 01").reverse)
        self.close(terminal)


if __name__ == "__main__":
    test_main(
        1,
    )
