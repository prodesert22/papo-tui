"""Own profile editing through the real TUI and synthetic REST."""

from test_context import main as test_main

import base64
import os
import struct
import tempfile
import time
import unittest
import zlib
from pathlib import Path

from profile_mock import ME, ProfileMock
from terminal_support import VERTICAL, Terminal
import terminal_interactions as interactions

DOWN, RIGHT = "\x1bOB", "\x1bOC"


def png(width, height):
    """A plain RGB PNG with varied pixels, so it does not compress to nothing."""

    def chunk(kind, data):
        return (
            struct.pack(">I", len(data))
            + kind
            + data
            + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
        )

    rows = b"".join(
        b"\x00" + bytes((x * 7 + y * 3) & 255 for x in range(width) for _ in range(3))
        for y in range(height)
    )
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(rows))
        + chunk(b"IEND", b"")
    )


def png_size(data):
    assert data[:8] == b"\x89PNG\r\n\x1a\n" and data[12:16] == b"IHDR"
    return struct.unpack(">II", data[16:24])


class ProfileEdit(unittest.TestCase):
    screen = interactions.MessageInteractions.screen
    wait = interactions.MessageInteractions.wait
    settled = interactions.MessageInteractions.settled

    def open(self, server, *, mode="ultralight"):
        terminal = Terminal()
        self.addCleanup(terminal.close)
        terminal.resize(30, 100)
        terminal.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(terminal, "Conversar em #geral")
        terminal.send("\x13")
        self.wait(terminal, "Configurações")
        terminal.send(RIGHT + "\t" + DOWN * (2 if mode == "ultralight" else 1) + "\n\t\n")
        self.settled(terminal, lambda: "Configurações" not in self.screen(terminal))
        self.wait(terminal, "Conversar em #geral")
        return terminal

    def editor(self, terminal):
        def focus():
            line = next((row for row in terminal.screen.display if "Foco:" in row), "")
            return line[line.find("Foco:") :].split("●")[0].strip()

        for _ in range(8):
            if "Foco: Mensagem" in self.screen(terminal):
                return
            # One Tab at a time: a second one before the first is drawn would skip past.
            before = focus()
            terminal.send("\t")
            self.settled(terminal, lambda: focus() != before)
        self.wait(terminal, "Foco: Mensagem")

    def perfil(self, terminal):
        terminal.send("\x13")
        self.wait(terminal, "Configurações")
        # Settings open on Perfil, the first tab.
        self.wait(terminal, "Seu perfil em")
        self.wait(terminal, "Apelido:")

    def puts(self, server, count, seconds=6):
        deadline = time.monotonic() + seconds
        while len(server.profile_puts) < count and time.monotonic() < deadline:
            time.sleep(0.03)
        self.assertEqual(len(server.profile_puts), count, server.profile_puts)
        return server.profile_puts

    def test_perfil_tab_saves_the_text_fields_together_and_the_status(self):
        with ProfileMock() as server:
            t = self.open(server)
            self.perfil(t)
            self.wait(t, "Você")
            self.wait(t, "Frase de status")
            t.send(DOWN + "\x15Novo apelido")
            t.send(DOWN + "\x15Em reunião")
            t.send(DOWN * 3 + RIGHT)
            self.wait(t, "< Ausente >")
            t.send("\t\n")
            self.wait(t, "Status: ausente")
            puts = self.puts(server, 2)
            # The required fields go together, completed from the loaded
            # profile; the typing phrase is left as it was.
            self.assertEqual(
                puts[0],
                (
                    "",
                    {
                        "nickname": "Novo apelido",
                        "status": "Em reunião",
                        "description": "Descrição de perfil sintético.",
                    },
                ),
            )
            self.assertEqual(puts[1], ("status", {"status": "away"}))
            self.assertIn("Novo apelido", self.screen(t))
            # Nothing else to save: no request is sent.
            t.send("\n")
            self.wait(t, "Nenhuma alteração para salvar.")
            t.read(0.5)
            self.assertEqual(len(server.profile_puts), 2)

    def window_rows(self, terminal):
        """Rows of the settings window, from its top border to its bottom one.

        Raises StopIteration while the window is not all on the screen."""
        lines = terminal.screen.display
        top = next(i for i, line in enumerate(lines) if "Configurações" in line) - 1
        bottom = next(i for i, line in enumerate(lines) if "Fechar" in line and i > top) + 1
        return lines[top : bottom + 1]

    def settings_rows(self, terminal):
        """Rows of the settings window, once its last rows are on the screen.

        The window is drawn from its top, so its fields can show before the row that
        closes it."""

        def whole():
            try:
                self.window_rows(terminal)
            except StopIteration:
                return False
            return True

        self.settled(terminal, whole)
        return self.window_rows(terminal)

    def test_perfil_previews_the_avatar_and_banner_like_the_profile_card(self):
        with ProfileMock() as server:
            t = self.open(server, mode="light")
            self.perfil(t)
            self.wait(t, "@josé")
            # The loaded profile is drawn from the preview down to the banner's row, its last.
            self.settled(
                t, lambda: any("Banner:" in row and "sem banner" in row for row in t.screen.display)
            )
            window = self.settings_rows(t)
            text = "\n".join(window)
            # The preview shows the saved name, @username and status, a letter
            # for the avatar that is not loaded and the empty banner band.
            self.assertIn("Você", text)
            self.assertIn("Online", text)
            self.assertEqual(text.count("sem banner"), 2, text)
            # The fields stay reachable below it.
            for label in ("Apelido:", "Frase de status:", "Status:", "Avatar:", "Banner:"):
                self.assertIn(label, text)
            preview = next(i for i, row in enumerate(window) if "@josé" in row)
            field = next(i for i, row in enumerate(window) if "Apelido:" in row)
            self.assertLess(preview, field)
            # The window is taller than the usual 20 rows to make room for it.
            self.assertGreater(len(window), 20)
            t.send("\x1b")
            self.settled(t, lambda: "Configurações" not in self.screen(t))

    def test_perfil_has_no_preview_in_ultralight_or_when_the_terminal_is_short(self):
        with ProfileMock() as server:
            t = self.open(server)
            self.perfil(t)
            self.assertEqual(len(self.settings_rows(t)), 20)
            self.assertNotIn("@josé", "\n".join(self.settings_rows(t)))
            t.send("\x1b")
            self.settled(t, lambda: "Configurações" not in self.screen(t))
        with ProfileMock() as server:
            t = self.open(server, mode="light")
            t.resize(24, 100)
            self.perfil(t)

            def short_window():
                # When the terminal shrinks, its old rows can show until the redraw.
                try:
                    return len(self.window_rows(t)) <= 20
                except StopIteration:
                    return False

            self.settled(t, short_window)
            self.assertNotIn("@josé", "\n".join(self.settings_rows(t)))
            self.assertIn("Apelido:", self.screen(t))

    def test_a_refused_perfil_field_takes_the_cursor_back_and_turns_red(self):
        with ProfileMock() as server:
            t = self.open(server)
            self.perfil(t)
            self.wait(t, "Frase de status")
            t.send(DOWN + "\x15" + "a" * 33)
            # Moving on and saving: the refusal brings the cursor back to the nickname.
            t.send(DOWN + "\n")
            self.wait(t, "Apelido com mais de 32 caracteres.")
            row = next(i for i, line in enumerate(t.screen.display) if "Apelido:" in line)
            self.settled(t, lambda: t.screen.cursor.y == row)
            sides = t.screen.display[row].count(VERTICAL)
            t.send("\x7f")
            self.settled(t, lambda: t.screen.display[row].count(VERTICAL) < sides)
            self.assertEqual(len(server.profile_puts), 0)

    def test_rejected_and_unconfirmed_changes_are_never_repeated(self):
        with ProfileMock() as server:
            t = self.open(server)
            server.put_status[""] = 400
            self.perfil(t)
            self.wait(t, "Você")
            t.send(DOWN + "\x15Recusado\n")
            self.wait(t, "O servidor recusou a alteração: Campo rejeitado")
            self.assertNotIn("\x1b", self.screen(t))
            server.put_status[""] = "drop"
            loads = sum(1 for r in server.requests if r[1].endswith(f"/users/{ME}/profile"))
            t.send("\x15Sem resposta\n")
            self.wait(t, "Alteração não confirmada")
            # The profile is fetched again to show what the server kept.
            self.settled(
                t,
                lambda: (
                    sum(1 for r in server.requests if r[1].endswith(f"/users/{ME}/profile")) > loads
                ),
            )
            t.read(1)
            self.assertEqual(
                [body["nickname"] for _, body in server.profile_puts], ["Recusado", "Sem resposta"]
            )

    def test_editor_commands_change_the_profile_and_other_text_is_sent(self):
        with ProfileMock() as server:
            t = self.open(server)
            self.editor(t)
            t.send("/nick Apelidão\n")
            self.wait(t, "Perfil salvo")
            t.send("/status ocupado\n")
            self.wait(t, "Status: ocupado")
            t.send("/bio Uma descrição nova\n")
            self.puts(server, 3)
            # Enter on the bare name only completes it; a second Enter sends.
            t.send("/nick\n")
            self.wait(t, "+1 opção")
            t.read(0.3)
            self.assertEqual(len(server.profile_puts), 3)
            t.send("\n")
            self.puts(server, 4)
            t.send("/banner remover\n")
            self.wait(t, "Banner removido")
            puts = self.puts(server, 5)
            self.assertEqual(puts[0][1]["nickname"], "Apelidão")
            self.assertEqual(puts[0][1]["status"], "Frase de status")
            self.assertEqual(puts[1], ("status", {"status": "busy"}))
            self.assertEqual(puts[2][1]["description"], "Uma descrição nova")
            self.assertEqual(puts[2][1]["nickname"], "Apelidão")
            self.assertEqual(puts[3][1]["nickname"], "")
            self.assertEqual(puts[4], ("banner", {"banner": "", "banner_format": ""}))
            # A wrong argument keeps the text and sends nothing.
            t.send("/status talvez\n")
            self.wait(t, "Uso: /status online, ausente ou ocupado")
            self.assertIn("/status talvez", self.screen(t))
            # The composer shows the refusal in red until the text changes.
            row = next(i for i, line in enumerate(t.screen.display) if "> /status talvez" in line)
            prompt = t.screen.display[row].index("> /status talvez")
            red = t.screen.buffer[row][prompt].fg
            t.send("x")
            self.settled(t, lambda: t.screen.buffer[row][prompt].fg != red)
            t.send("\x7f\x03")
            # An empty message is refused the same way.
            t.send("   \n")
            self.wait(t, "Escreva uma mensagem antes de enviar.")
            red = t.screen.buffer[row][prompt].fg
            t.send("x")
            self.settled(t, lambda: t.screen.buffer[row][prompt].fg != red)
            t.send("\x7f\x03")
            # "//" sends one slash; other names are ordinary messages.
            t.send("//comando literal\n")
            t.send("/nickname não é comando\n")
            self.settled(t, lambda: len(server.sent_messages) == 2)
            self.assertEqual(
                [m["content"] for m in server.sent_messages],
                ["/comando literal", "/nickname não é comando"],
            )
            self.assertEqual(len(server.profile_puts), 5)

    def test_typing_a_slash_lists_commands_and_then_describes_the_typed_one(self):
        with ProfileMock() as server:
            t = self.open(server)
            self.editor(t)
            t.send("/")
            self.wait(t, "Comandos")
            # The list is drawn after its title; wait for each entry.
            for name in ("/nick", "/status", "/bio", "/avatar", "/banner"):
                self.wait(t, name)
            self.assertIn("Alterar seu apelido neste servidor.", self.screen(t))
            t.send("ni")
            self.settled(t, lambda: "/status " not in self.screen(t))
            # Tab completes the name; the chooser then describes its option and
            # the editor shows how many options remain.
            t.send("\t")
            self.wait(t, "novo_apelido")
            self.wait(t, "+1 opção")
            self.assertIn("Opções", self.screen(t))
            self.assertIn("> /nick", self.screen(t))
            t.send("Ana")
            self.settled(t, lambda: "+1 opção" not in self.screen(t))
            # Esc hides the chooser and keeps the draft.
            t.send("\x1b")
            self.settled(t, lambda: "novo_apelido" not in self.screen(t))
            self.assertIn("/nick Ana", self.screen(t))
            # Choices narrow as the option is typed; Tab completes one.
            t.send("\x03/status o")
            # Online and Ocupado show before the "o" arrives too: the filter is done once
            # Ausente leaves.
            self.settled(t, lambda: "Ausente" not in self.screen(t))
            self.wait(t, "online")
            self.wait(t, "ocupado")
            t.send("\x1bOB\t")
            self.wait(t, "/status ocupado")
            t.send("\n")
            self.wait(t, "Status: ocupado")
            self.assertEqual(self.puts(server, 1), [("status", {"status": "busy"})])
            # Other text with a slash closes the chooser and is sent.
            t.send("/x")
            self.settled(t, lambda: "Comandos" not in self.screen(t))
            t.send("\n")
            self.settled(t, lambda: len(server.sent_messages) == 1)
            self.assertEqual(server.sent_messages[0]["content"], "/x")

    @unittest.skipIf(os.name == "nt", "Native Windows file dialogs cannot be driven through ConPTY")
    def test_images_within_limits_are_sent_unchanged_and_larger_ones_reduced(self):
        with tempfile.TemporaryDirectory() as folder, ProfileMock() as server:
            large, small = Path(folder, "grande.png"), Path(folder, "pequena.png")
            large.write_bytes(png(1200, 600))
            small.write_bytes(png(64, 64))
            t = self.open(server, mode="light")
            self.editor(t)
            t.send("/avatar\n")
            self.wait(t, "Escolher imagem")
            t.send("\x03" + str(large) + "\n")
            self.wait(t, "Avatar atualizado")
            endpoint, body = self.puts(server, 1)[0]
            self.assertEqual((endpoint, body["avatar_format"]), ("avatar", "PNG"))
            self.assertEqual(png_size(base64.b64decode(body["avatar"])), (512, 256))
            # The first Enter completes the name; with no option typed, the
            # second one runs it, since removing the banner is optional.
            t.send("/banner\n")
            self.wait(t, "+1 opção")
            t.send("\n")
            self.wait(t, "Escolher imagem")
            t.send("\x03" + str(small) + "\n")
            self.wait(t, "Banner atualizado")
            endpoint, body = self.puts(server, 2)[1]
            self.assertEqual((endpoint, body["banner_format"]), ("banner", "PNG"))
            self.assertEqual(base64.b64decode(body["banner"]), small.read_bytes())


if __name__ == "__main__":
    test_main(1)
