"""Own password change (Ctrl+S → Conta) and password reset by link through the real TUI."""

from test_context import main as test_main

import time
import unittest

from password_mock import TOKEN, PasswordMock
from terminal_support import Terminal
import terminal_interactions as interactions

DOWN, LEFT, RIGHT = "\x1bOB", "\x1bOD", "\x1bOC"
NEW = "Renewed!456"


class PasswordTerminal(unittest.TestCase):
    screen = interactions.MessageInteractions.screen
    wait = interactions.MessageInteractions.wait
    settled = interactions.MessageInteractions.settled

    def terminal(self):
        terminal = Terminal()
        self.addCleanup(terminal.close)
        return terminal

    def button(self, terminal, label):
        for y, line in enumerate(terminal.screen.display):
            if label in line:
                terminal.click(y, line.index(label) + 2)
                return
        self.fail("Button absent: " + label)

    def calls(self, server, count, seconds=6):
        deadline = time.monotonic() + seconds
        while len(server.password_calls) < count and time.monotonic() < deadline:
            time.sleep(0.03)
        self.assertEqual(len(server.password_calls), count, server.password_calls)
        return server.password_calls

    # Reset by link, without a session.

    def reset_form(self, server, terminal):
        terminal.send(server.url + "\tjosé")
        self.wait(terminal, "[Redefinir senha]")
        self.button(terminal, "[Redefinir senha]")
        self.wait(terminal, "Link de redefinição")
        self.wait(terminal, "Redefina sua senha com o link recebido")
        # The username waits aside: the link field starts empty.
        self.assertNotIn("josé", self.screen(terminal))

    def test_reset_link_sets_a_new_password_and_returns_to_login(self):
        with PasswordMock() as server:
            t = self.terminal()
            self.reset_form(server, t)
            link = server.url + "/passwordchange/" + TOKEN
            t.send(link + "\t" + NEW + "\t" + NEW + "\n")
            self.wait(t, "Senha redefinida")
            self.assertEqual(self.calls(server, 1), [("link", {"token": TOKEN, "password": NEW})])
            # Back on login with the username restored; no sign-in was attempted.
            self.wait(t, "Usuário (username)")
            self.wait(t, "josé")
            self.assertIn("[ Entrar ]", self.screen(t))
            self.assertNotIn("Link de redefinição", self.screen(t))
            self.assertFalse(any(r[1].endswith("/auth/login") for r in server.requests))
            self.assertNotIn(NEW.encode(), t.output)

    def test_reset_link_checks_input_and_host_before_sending(self):
        with PasswordMock() as server:
            t = self.terminal()
            self.reset_form(server, t)
            t.send("https://papo.example/outra\t" + NEW + "\t" + NEW + "\n")
            self.wait(t, "Cole o link de redefinição completo")
            t.send("\x15https://papo.example/passwordchange/" + TOKEN + "\t\x15fraca\t\x15fraca\n")
            self.wait(t, "8 caracteres")
            t.send("\x15" + NEW + "\t\x15" + "Other!456\n")
            self.wait(t, "não coincidem")
            t.send("\x15" + NEW + "\n")
            # A link from another host than the server URL needs a second Enter.
            self.wait(t, "O link é de papo.example")
            t.read(0.3)
            self.assertEqual(server.password_calls, [])
            t.send("\n")
            self.assertEqual(self.calls(server, 1), [("link", {"token": TOKEN, "password": NEW})])
            self.wait(t, "Senha redefinida")

    def test_used_link_is_reported_once_and_the_form_is_kept(self):
        with PasswordMock() as server:
            server.tokens = set()
            t = self.terminal()
            self.reset_form(server, t)
            t.send(TOKEN + "\t" + NEW + "\t" + NEW + "\n")
            self.wait(t, "Link inválido, expirado ou já usado")
            self.assertEqual(len(self.calls(server, 1)), 1)
            self.assertIn("Link de redefinição", self.screen(t))
            # [Login] leaves the reset form and brings the username back.
            self.button(t, "[Login]")
            self.wait(t, "Usuário (username)")
            self.wait(t, "josé")
            t.read(0.3)
            self.assertEqual(len(server.password_calls), 1)

    # Own password change, with a session.

    def open(self, server):
        t = self.terminal()
        t.resize(30, 100)
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "Conversar em #geral")
        t.send("\x13")
        self.wait(t, "Configurações")
        t.send(RIGHT + "\t" + DOWN * 2 + "\n\t\n")
        self.settled(t, lambda: "Configurações" not in self.screen(t))
        self.wait(t, "Conversar em #geral")
        return t

    def refused_input(self, t, label):
        """Row of the label's input and its vertical sides, once the cursor is in it."""
        row = next(i for i, line in enumerate(t.screen.display) if label in line)
        self.settled(t, lambda: t.screen.cursor.y == row)
        return row, t.screen.display[row].count("x")

    def conta(self, t):
        t.send("\x13")
        self.wait(t, "Seu perfil em")
        # Conta is the last tab: Left from Perfil reaches it.
        t.send(LEFT)
        self.wait(t, "Sua conta em")
        self.wait(t, "@josé")
        t.send(DOWN)

    def test_conta_changes_the_password_after_confirmation_and_ends_the_session(self):
        with PasswordMock() as server:
            t = self.open(server)
            self.conta(t)
            t.send(NEW + DOWN + NEW + "\n")
            self.wait(t, "Todas as sessões da conta serão encerradas")
            self.wait(t, "Confirmar")
            t.read(0.3)
            self.assertEqual(server.password_calls, [])
            t.send("\n")
            calls = self.calls(server, 2)
            self.assertEqual(calls, [("reset", None), ("password", {"password": NEW})])
            # Back to login: the server revoked this session too.
            self.wait(t, "Senha alterada")
            self.wait(t, "[ Entrar ]")
            self.assertNotIn("Conversar em #geral", self.screen(t))
            t.read(0.5)
            self.assertEqual(len(server.password_calls), 2)
            self.assertNotIn(NEW.encode(), t.output)

    def test_conta_refusals_keep_the_session_and_are_never_repeated(self):
        with PasswordMock() as server:
            t = self.open(server)
            self.conta(t)
            # Local checks send nothing.
            t.send("curta\n")
            self.wait(t, "8 caracteres")
            row, sides = self.refused_input(t, "Nova senha:")
            t.send("y")
            self.settled(t, lambda: t.screen.display[row].count("x") < sides)
            t.send("\x15" + NEW + DOWN + "Other!456\n")
            self.wait(t, "não coincidem")
            row, sides = self.refused_input(t, "Confirmar senha:")
            t.send("y")
            self.settled(t, lambda: t.screen.display[row].count("x") < sides)
            t.send("\x15Other!456")
            t.read(0.3)
            self.assertEqual(server.password_calls, [])
            server.password_status = 400
            t.send("\x15" + NEW + "\n\n")
            self.wait(t, "O servidor recusou a nova senha: Senha recusada")
            self.assertNotIn("\x1b", self.screen(t))
            self.assertEqual(
                self.calls(server, 2), [("reset", None), ("password", {"password": NEW})]
            )
            # The session stays; nothing is resent on its own.
            t.read(1)
            self.assertEqual(len(server.password_calls), 2)
            self.assertIn("Sua conta em", self.screen(t))
            t.send("\x1b")
            self.settled(t, lambda: "Configurações" not in self.screen(t))
            self.wait(t, "Conversar em #geral")

    def test_conta_unconfirmed_change_is_reported_without_repeating(self):
        with PasswordMock() as server:
            server.password_status = "drop"
            t = self.open(server)
            self.conta(t)
            t.send(NEW + DOWN + NEW + "\n\n")
            self.assertEqual(len(self.calls(server, 2)), 2)
            # The change took effect: the session check that follows the
            # unconfirmed outcome finds this session revoked.
            self.wait(t, "Sua sessão expirou ou foi revogada")
            self.wait(t, "[ Entrar ]")
            t.read(0.5)
            self.assertEqual(len(server.password_calls), 2)


if __name__ == "__main__":
    test_main(1)
