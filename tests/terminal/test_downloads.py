"""Attachment downloads through the real TUI; the backend and files are synthetic."""

from test_context import main as test_main

from pathlib import Path
import tempfile
import time
import unittest
from wcwidth import wcswidth
from history_mock import CHANNEL, HistoryMock, uuid
from realtime_mock import RealtimeMock
import terminal_interactions as support

CONTENT = bytes(range(256)) * 10  # 2.50 KB of non-text bytes
NAME = "mouse-scroll-fix-1.0.0.jar"


class Downloads(support.MessageInteractions):
    def serve(self, *, attachments=None, mime="application/java-archive", mock=HistoryMock):
        server = mock(count=1)
        parent = server.route
        files = attachments or [(NAME, CONTENT)]
        server.messages[0]["attachments"] = [
            {
                "id": uuid(900 + i),
                "mime_type": mime,
                "original_file_name": name,
                "size_bytes": len(data),
                "moderation_status": "clean",
            }
            for i, (name, data) in enumerate(files)
        ]
        server.downloads = []

        def route(method, path, body):
            if "/attachments/" in path:
                server.downloads.append(path)
                index = int(path.rsplit("/", 1)[1][-12:], 16) - 900
                return 200, files[index][1], None, None, mime
            return parent(method, path, body)

        server.api_handler = route
        return server

    def button(self, terminal):
        self.wait(terminal, "[ ↓ ]")
        for row, line in enumerate(terminal.screen.display):
            if "[ ↓ ]" in line:
                return row, wcswidth(line[: line.index("[ ↓ ]")]) + 2
        self.fail("Missing download button")

    def save_dialog(self, terminal, target):
        self.wait(terminal, "Salvar anexo")
        self.wait(terminal, "Salvar em")
        terminal.send("\x03" + str(target) + "\n")

    def test_an_empty_destination_is_refused_in_red_with_the_cursor_in_the_path(self):
        with self.serve() as server:
            terminal = self.open(server)
            try:
                row, column = self.button(terminal)
                terminal.click(row, column)
                self.wait(terminal, "Salvar em")
                path_row = next(
                    i for i, line in enumerate(terminal.screen.display) if "Salvar em" in line
                ) + 1
                # From the Salvar button, with nothing typed.
                terminal.send("\x03\t\t\t\n")
                self.wait(terminal, "Informe o caminho do arquivo.")
                red = terminal.screen.buffer[path_row][5].bg
                # Typing now goes to the path, and the red goes away with the first character.
                terminal.send("a")
                self.settled(terminal, lambda: "a" in terminal.screen.display[path_row])
                self.assertNotEqual(terminal.screen.buffer[path_row][5].bg, red)
                self.assertEqual(server.downloads, [])
            finally:
                self.assertTrue(terminal.close())

    def test_file_card_shows_name_size_and_button_on_the_right(self):
        with self.serve() as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, NAME)
                self.wait(terminal, "2.50 KB")
                self.assertNotIn("Anexo:", self.screen(terminal))
                row, column = self.button(terminal)
                line = terminal.screen.display[row]
                self.assertLess(wcswidth(line[: line.index(NAME)]), column)
                # The button is filled with its own color, unlike the card text.
                button_bg = terminal.screen.buffer[row][column].bg
                self.assertNotEqual(button_bg, terminal.screen.buffer[row][column - 12].bg)
                self.assertEqual(server.downloads, [])  # Showing a card never downloads.
            finally:
                self.assertTrue(terminal.close())

    def test_moderation_state_from_the_server_appears_on_the_card_at_once(self):
        with self.serve(mock=RealtimeMock) as server:
            terminal = self.open(server)
            try:
                self.wait(terminal, NAME)
                self.wait(terminal, "Tempo real conectado")
                self.assertNotIn("Moderação", self.screen(terminal))
                requests = sum("/messages" in r[1] for r in server.requests)
                server.emit(
                    {
                        "type": "attachment_moderation_update",
                        "channel_id": CHANNEL,
                        "message_id": server.messages[0]["id"],
                        "attachment_id": uuid(900),
                        "status": "sensitive",
                    }
                )
                self.wait(terminal, "Moderação: sensitive")
                # Nothing is reloaded and the card keeps working.
                self.assertEqual(sum("/messages" in r[1] for r in server.requests), requests)
                self.assertEqual(server.downloads, [])
                self.button(terminal)
            finally:
                self.assertTrue(terminal.close())

    def test_button_asks_where_to_save_then_writes_the_file(self):
        with tempfile.TemporaryDirectory() as folder, self.serve() as server:
            target = Path(folder, "saved.jar")
            terminal = self.open(server)
            try:
                row, column = self.button(terminal)
                self.assertEqual(server.downloads, [])
                terminal.click(row, column)
                self.save_dialog(terminal, target)
                self.wait(terminal, "Arquivo salvo em")
                self.assertEqual(target.read_bytes(), CONTENT)
                self.assertEqual(len(server.downloads), 1)
                self.assertNotIn("Salvar anexo", self.screen(terminal))
                self.assertEqual(list(Path(folder).iterdir()), [target])
            finally:
                self.assertTrue(terminal.close())

    def test_context_menu_download_uses_the_same_dialog(self):
        with tempfile.TemporaryDirectory() as folder, self.serve() as server:
            target = Path(folder, "menu.jar")
            terminal = self.open(server)
            try:
                self.right_click(terminal, NAME)
                self.wait(terminal, "Baixar anexo")
                self.click(terminal, "Baixar anexo")
                self.save_dialog(terminal, target)
                self.wait(terminal, "Arquivo salvo em")
                self.assertEqual(target.read_bytes(), CONTENT)
            finally:
                self.assertTrue(terminal.close())

    def test_context_menu_downloads_images_videos_and_audio_too(self):
        for mime, name in (
            ("image/png", "picture.png"),
            ("video/mp4", "clip.mp4"),
            ("audio/mpeg", "song.mp3"),
        ):
            with (
                self.subTest(mime=mime),
                tempfile.TemporaryDirectory() as folder,
                self.serve(attachments=[(name, CONTENT)], mime=mime) as server,
            ):
                target = Path(folder, "saved-" + name)
                terminal = self.open(server)
                try:
                    self.right_click(terminal, "Mensagem 0001")
                    self.click(terminal, "Baixar anexo")
                    self.save_dialog(terminal, target)
                    self.wait(terminal, "Arquivo salvo em")
                    self.assertEqual(target.read_bytes(), CONTENT)
                finally:
                    self.assertTrue(terminal.close())

    def test_more_than_two_attachments_open_a_chooser_for_the_menu_download(self):
        files = [("one.bin", b"first"), ("two.bin", b"second"), ("three.bin", b"third")]
        with tempfile.TemporaryDirectory() as folder, self.serve(attachments=files) as server:
            terminal = self.open(server)
            try:
                self.resize(terminal, 40, 100)
                self.right_click(terminal, "Mensagem 0001")
                self.click(terminal, "Baixar anexos")
                self.wait(terminal, "Baixar anexos ")
                self.wait(terminal, "Escolha os anexos")
                screen = self.screen(terminal)
                for label in ("one.bin · 5 bytes", "two.bin · 6 bytes", "three.bin · 5 bytes"):
                    self.assertIn(label, screen)
                self.assertEqual(server.downloads, [])
                # Nothing is marked: confirming asks for at least one.
                terminal.send("\t\t\n")
                self.wait(terminal, "Marque ao menos um anexo")
                terminal.send("\x1b[Z\x1b[Z")  # Back to the list, first item.
                terminal.send(" \x1bOB\x1bOB \t\t\n")  # Mark one.bin and three.bin.
                self.save_dialog(terminal, Path(folder, "a.bin"))
                self.wait(terminal, "three.bin")
                self.save_dialog(terminal, Path(folder, "b.bin"))
                deadline = time.monotonic() + 8
                while not Path(folder, "b.bin").exists() and time.monotonic() < deadline:
                    terminal.read(0.03)
                self.assertEqual(Path(folder, "a.bin").read_bytes(), b"first")
                self.assertEqual(Path(folder, "b.bin").read_bytes(), b"third")
                self.assertEqual(len(server.downloads), 2)  # two.bin was never requested.
            finally:
                self.assertTrue(terminal.close())

    def test_chooser_cancel_and_select_all_and_two_attachments_skip_it(self):
        files = [("one.bin", b"1"), ("two.bin", b"2"), ("three.bin", b"3")]
        with tempfile.TemporaryDirectory() as folder, self.serve(attachments=files) as server:
            terminal = self.open(server)
            try:
                self.resize(terminal, 40, 100)
                self.right_click(terminal, "Mensagem 0001")
                self.click(terminal, "Baixar anexos")
                self.wait(terminal, "Escolha os anexos")
                terminal.send("a")
                self.wait(terminal, "3 marcado(s)")
                terminal.send("a")
                self.wait(terminal, "0 marcado(s)")
                terminal.send("\x1b")
                self.wait(terminal, "Download cancelado.")
                self.assertNotIn("Escolha os anexos", self.screen(terminal))
                time.sleep(0.3)
                self.assertEqual(server.downloads, [])
            finally:
                self.assertTrue(terminal.close())
        files = [("one.bin", b"1"), ("two.bin", b"2")]
        with tempfile.TemporaryDirectory() as folder, self.serve(attachments=files) as server:
            terminal = self.open(server)
            try:
                self.resize(terminal, 40, 100)
                self.right_click(terminal, "Mensagem 0001")
                self.click(terminal, "Baixar anexos")
                self.wait(terminal, "Salvar anexo")  # Straight to the save dialog.
                self.assertNotIn("Escolha os anexos", self.screen(terminal))
                terminal.send("\x1b")
                self.wait(terminal, "Download cancelado.")
            finally:
                self.assertTrue(terminal.close())

    def test_cancelling_the_dialog_downloads_nothing(self):
        with tempfile.TemporaryDirectory() as folder, self.serve() as server:
            terminal = self.open(server)
            try:
                row, column = self.button(terminal)
                terminal.click(row, column)
                self.wait(terminal, "Salvar anexo")
                terminal.send("\x1b")
                self.wait(terminal, "Download cancelado.")
                self.assertNotIn("Salvar anexo", self.screen(terminal))
                time.sleep(0.3)
                self.assertEqual(server.downloads, [])
                self.assertEqual(list(Path(folder).iterdir()), [])
            finally:
                self.assertTrue(terminal.close())

    def test_existing_file_needs_a_second_confirmation_and_missing_folder_is_refused(self):
        with tempfile.TemporaryDirectory() as folder, self.serve() as server:
            target = Path(folder, "existing.jar")
            target.write_bytes(b"old")
            terminal = self.open(server)
            try:
                row, column = self.button(terminal)
                terminal.click(row, column)
                self.wait(terminal, "Salvar anexo")
                terminal.send("\x03" + str(Path(folder, "missing", "x.jar")) + "\n")
                self.wait(terminal, "A pasta do destino não existe.")
                terminal.send("\x03" + str(target) + "\n")
                self.wait(terminal, "O arquivo já existe")
                self.assertEqual(target.read_bytes(), b"old")
                self.assertEqual(server.downloads, [])
                terminal.send("\n")
                self.wait(terminal, "Arquivo salvo em")
                self.assertEqual(target.read_bytes(), CONTENT)
            finally:
                self.assertTrue(terminal.close())

    def test_dialog_suggests_a_safe_name_in_the_chosen_folder(self):
        hostile = "../evil\x1b[2J:name.txt"
        with (
            tempfile.TemporaryDirectory() as folder,
            self.serve(attachments=[(hostile, b"x")]) as server,
        ):
            terminal = self.open(server)
            try:
                row, column = self.button(terminal)
                terminal.click(row, column)
                self.wait(terminal, "Salvar anexo")
                self.assertNotIn(
                    "\x1b[2J", terminal.output.decode("utf-8", "replace").split("Salvar anexo")[-1]
                )
                self.assertIn("evil_[2J_name.txt", self.screen(terminal))
                terminal.send("\x1b")
                self.wait(terminal, "Download cancelado.")
            finally:
                self.assertTrue(terminal.close())

    def test_several_attachments_ask_for_each_destination(self):
        files = [("one.bin", b"first"), ("two.bin", b"second")]
        with tempfile.TemporaryDirectory() as folder, self.serve(attachments=files) as server:
            terminal = self.open(server)
            try:
                self.right_click(terminal, "one.bin")
                self.wait(terminal, "Baixar anexos")
                self.click(terminal, "Baixar anexos")
                self.save_dialog(terminal, Path(folder, "a.bin"))
                # The second file asks again, with its own suggested name.
                self.wait(terminal, "two.bin")
                self.save_dialog(terminal, Path(folder, "b.bin"))
                deadline = time.monotonic() + 8
                while not Path(folder, "b.bin").exists() and time.monotonic() < deadline:
                    terminal.read(0.03)
                self.assertEqual(Path(folder, "a.bin").read_bytes(), b"first")
                self.assertEqual(Path(folder, "b.bin").read_bytes(), b"second")
            finally:
                self.assertTrue(terminal.close())

    def test_failed_download_reports_without_writing(self):
        with tempfile.TemporaryDirectory() as folder, self.serve() as server:
            parent = server.api_handler
            server.api_handler = lambda method, path, body: (
                (404, {"error": "missing"})
                if "/attachments/" in path
                else parent(method, path, body)
            )
            terminal = self.open(server)
            try:
                row, column = self.button(terminal)
                terminal.click(row, column)
                self.save_dialog(terminal, Path(folder, "never.jar"))
                self.wait(terminal, "Falha ao baixar")
                self.assertEqual(list(Path(folder).iterdir()), [])
            finally:
                self.assertTrue(terminal.close())


if __name__ == "__main__":
    test_main()
