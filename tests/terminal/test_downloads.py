"""Attachment downloads through the real TUI; the backend and files are synthetic."""

from test_context import main as test_main

from pathlib import Path
import tempfile
import time
import unittest
from wcwidth import wcswidth
from history_mock import HistoryMock, uuid
import terminal_interactions as support

CONTENT = bytes(range(256)) * 10  # 2.50 KB of non-text bytes
NAME = "mouse-scroll-fix-1.0.0.jar"


class Downloads(support.MessageInteractions):
    def serve(self, *, attachments=None):
        server = HistoryMock(count=1)
        parent = server.route
        files = attachments or [(NAME, CONTENT)]
        server.messages[0]["attachments"] = [
            {
                "id": uuid(900 + i),
                "mime_type": "application/java-archive",
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
                return 200, files[index][1], None, None, "application/java-archive"
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
                self.assertEqual(server.downloads, [])  # Showing a card never downloads.
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
