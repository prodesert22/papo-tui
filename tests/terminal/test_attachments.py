"""Attachments through the real TUI; clipboard helpers and HTTP are synthetic."""

import os
from pathlib import Path
import tempfile
import unittest
from wcwidth import wcswidth
from history_mock import HistoryMock
import test_send_product as product
from test_terminal import editor_region


class Attachments(unittest.TestCase):
    screen = product.SendProduct.screen
    wait = product.SendProduct.wait
    click = product.SendProduct.click
    open = product.SendProduct.open
    editor_text = product.SendProduct.editor_text

    def add_file(self, terminal, path):
        terminal.send("\x1b[15~")
        self.wait(terminal, "Anexar arquivos")
        terminal.send("\x03" + str(path) + "\n")
        self.wait(terminal, path.name + " 🗙]")

    def test_picker_file_only_send_and_binary_multipart(self):
        with tempfile.TemporaryDirectory() as folder, HistoryMock(count=0) as server:
            first, second = Path(folder, "résumé.bin"), Path(folder, "data.txt")
            first.write_bytes(b"\x00\xffbinary")
            second.write_text("second")
            terminal = self.open(server)
            try:
                self.add_file(terminal, first)
                self.add_file(terminal, second)
                terminal.send("\n")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(
                    [part["data"] for part in server.uploads[0]], [b"\x00\xffbinary", b"second"]
                )
                self.assertEqual(server.sent_messages[0]["content"], "")
                self.assertNotIn(" 🗙]", self.editor_text(terminal))
            finally:
                self.assertTrue(terminal.close())

    def test_tab_visual_order_removal_confirmation_and_resize(self):
        with tempfile.TemporaryDirectory() as folder, HistoryMock(count=0) as server:
            first, second = Path(folder, "one.txt"), Path(folder, "two.txt")
            first.write_text("one")
            second.write_text("two")
            terminal = self.open(server)
            try:
                self.add_file(terminal, first)
                self.add_file(terminal, second)
                for label in ("Anexos", "Histórico"):
                    terminal.send("\x1b[Z")
                    self.wait(terminal, "Foco: " + label)
                for label in ("Anexos", "Mensagem", "Anexar", "Emojis"):
                    terminal.send("\t")
                    self.wait(terminal, "Foco: " + label)
                terminal.send("\x1b[Z\x1b[Z\x1b[Z\x1bOC\n")
                self.wait(terminal, "Remover anexo?")
                terminal.send("\n")  # Cancel is the default.
                self.wait(terminal, "two.txt 🗙]")
                terminal.send("\n\x1bOC\n")
                self.wait(terminal, "one.txt 🗙]")
                self.assertNotIn("two.txt 🗙]", self.screen(terminal))
                terminal.resize(14, 44)
                self.wait(terminal, "one.txt 🗙]")
                terminal.send("\n\x1bOC\n")
                self.wait(terminal, "Foco: Mensagem")
                self.assertNotIn(" 🗙]", self.screen(terminal))
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(terminal.close())

    def test_attachment_block_stays_between_history_and_editor(self):
        with tempfile.TemporaryDirectory() as folder, HistoryMock(count=4) as server:
            source = Path(folder, "separate.txt")
            source.write_text("separate block")
            terminal = self.open(server)
            try:
                self.add_file(terminal, source)
                for height, width in ((24, 100), (14, 44), (32, 120)):
                    terminal.resize(height, width)
                    self.wait(terminal, "separate.txt 🗙]")
                    display = terminal.screen.display
                    row = next(i for i, line in enumerate(display) if "separate.txt 🗙]" in line)
                    editor = editor_region(terminal)[0]
                    column = 26 if width >= 80 else 1
                    # The editor's upper border follows the attachment block.
                    self.assertTrue(
                        any(
                            terminal.screen.buffer[y][column].data in ("q", "─")
                            for y in range(row + 1, editor)
                        ),
                        display,
                    )
                    self.assertNotIn("separate.txt", self.editor_text(terminal))
                    if height > 14:
                        self.assertIn("Anexos", display[row - 1])
                    self.assertIn("Histórico", "\n".join(display[:row]))
                terminal.send("first\x1b[13;2usecond\x1b[13;2uthird")
                self.wait(terminal, "third")
                self.assertNotIn("separate.txt", self.editor_text(terminal))
                terminal.send("\n")
                self.wait(terminal, "Mensagem confirmada")
                self.assertNotIn("separate.txt 🗙]", self.screen(terminal))
                self.assertEqual(server.sent_messages[0]["content"], "first\nsecond\nthird")
            finally:
                self.assertTrue(terminal.close())

    def test_mouse_close_glyph_opens_confirmation_and_keeps_original_file(self):
        with tempfile.TemporaryDirectory() as folder, HistoryMock(count=0) as server:
            source = Path(folder, "mouse.txt")
            source.write_text("original")
            terminal = self.open(server)
            try:
                row = editor_region(terminal)[0]
                clip_left, emoji_left = terminal.screen.columns - 16, terminal.screen.columns - 9
                line = terminal.screen.display[row]
                self.assertIn("📎", line[clip_left:emoji_left])
                self.assertNotIn("Anexar", line)
                self.assertNotIn("[", line[emoji_left:])
                self.assertNotIn("]", line[emoji_left:])
                terminal.click(row, 29)  # Place focus in the editor before tabbing.
                self.wait(terminal, "Foco: Mensagem")
                normal_bg = terminal.screen.buffer[row][clip_left + 1].bg
                self.assertEqual(normal_bg, terminal.screen.buffer[row][emoji_left + 1].bg)
                self.assertNotEqual(normal_bg, terminal.screen.buffer[row][clip_left - 2].bg)
                terminal.send("\t")
                self.wait(terminal, "Foco: Anexar")
                focus_bg = terminal.screen.buffer[row][clip_left + 1].bg
                self.assertNotEqual(normal_bg, focus_bg)
                terminal.send("\t")
                self.wait(terminal, "Foco: Emojis")
                self.assertEqual(focus_bg, terminal.screen.buffer[row][emoji_left + 1].bg)
                # Both directions wrap between the two buttons without editing text.
                for key, label in (
                    ("\x1bOD", "Anexar"),
                    ("\x1bOD", "Emojis"),
                    ("\x1bOC", "Anexar"),
                    ("\x1bOC", "Emojis"),
                ):
                    terminal.send(key)
                    self.wait(terminal, "Foco: " + label)
                for left in (clip_left, emoji_left):
                    self.assertEqual(terminal.screen.buffer[row][left].data, " ")
                    self.assertEqual(terminal.screen.buffer[row - 1][left].data, " ")
                self.assertEqual(server.message_count, 0)
                terminal.send("\n")
                self.wait(terminal, "Emojis · Esc voltar")
                terminal.send("\x1b")
                self.wait(terminal, "Foco: Emojis")
                terminal.click(row - 1, clip_left)  # The background padding is clickable too.
                self.wait(terminal, "Anexar arquivos")
                terminal.send("\x03" + str(source) + "\n")
                self.wait(terminal, "mouse.txt 🗙]")
                row = next(
                    i for i, line in enumerate(terminal.screen.display) if "mouse.txt 🗙]" in line
                )
                line = terminal.screen.display[row]
                terminal.click(row, wcswidth(line[: line.index("🗙")]))
                self.wait(terminal, "Remover anexo?")
                self.click(terminal, "Cancelar")
                self.assertNotIn("Remover anexo?", self.screen(terminal))
                self.assertIn("mouse.txt 🗙]", self.screen(terminal))
                self.assertEqual(source.read_text(), "original")
            finally:
                self.assertTrue(terminal.close())

    def test_exit_removes_private_draft_copies(self):
        with tempfile.TemporaryDirectory() as folder, HistoryMock(count=0) as server:
            source = Path(folder, "draft.txt")
            source.write_text("private staged draft")
            temporary = Path(folder, "temporary")
            temporary.mkdir()
            terminal = self.open(server, environment={"TMPDIR": str(temporary)})
            try:
                self.add_file(terminal, source)
                self.assertTrue(list(temporary.glob("papo-attachment-*/data")))
            finally:
                self.assertTrue(terminal.close())
            self.assertEqual(list(temporary.glob("papo-attachment-*")), [])
            self.assertEqual(source.read_text(), "private staged draft")

    def test_attachments_stay_with_their_conversation_draft(self):
        with tempfile.TemporaryDirectory() as folder, HistoryMock(count=0) as server:
            path = Path(folder, "draft.txt")
            path.write_text("draft file")
            terminal = self.open(server)
            try:
                self.add_file(terminal, path)
                terminal.send("draft text\x1b\x1b\x1bOB\n")
                self.wait(terminal, "# outro · Histórico")
                self.assertNotIn("draft.txt 🗙]", self.screen(terminal))
                terminal.send("other draft\x1b\x1b\x1bOA\n")
                self.wait(terminal, "# geral · Histórico")
                self.wait(terminal, "draft.txt 🗙]")
                self.assertIn("draft text", self.editor_text(terminal))
                self.assertEqual(server.message_count, 0)
            finally:
                self.assertTrue(terminal.close())

    @unittest.skipIf(os.name == "nt", "Synthetic Unix clipboard helper")
    def test_slow_clipboard_can_be_cancelled_without_sending_an_incomplete_message(self):
        with tempfile.TemporaryDirectory() as folder, HistoryMock(count=0) as server:
            helper = Path(folder, "wl-paste")
            helper.write_text(
                "#!/usr/bin/python3\nimport sys,time\n"
                "if '--list-types' in sys.argv: print('text/uri-list')\n"
                "else: time.sleep(10)\n"
            )
            helper.chmod(0o700)
            terminal = self.open(
                server,
                environment={
                    "WAYLAND_DISPLAY": "synthetic",
                    "PATH": folder + os.pathsep + os.environ.get("PATH", ""),
                },
            )
            try:
                terminal.send("\x16typed while reading\n")
                self.assertIn("typed while reading", self.editor_text(terminal))
                self.assertEqual(server.message_count, 0)
                terminal.send("\x1b\n")
                self.wait(terminal, "Mensagem confirmada")
                self.assertEqual(server.sent_messages[0]["content"], "typed while reading")
                self.assertEqual(server.uploads[0], [])
            finally:
                self.assertTrue(terminal.close())

    @unittest.skipIf(
        os.name == "nt", "Unix helper fixtures; native Windows clipboard requires Windows"
    )
    def test_ctrl_v_files_image_and_text_use_one_representation(self):
        for kind, key in (
            ("files", "\x16"),
            ("image", "\x16"),
            ("image", "\x1b[118;5u"),
            ("text", "\x16"),
        ):
            with (
                self.subTest(kind=kind, key=repr(key)),
                tempfile.TemporaryDirectory() as folder,
                HistoryMock(count=0) as server,
            ):
                root = Path(folder)
                file = root / "copied.txt"
                file.write_text("copied file")
                payload = root / "payload"
                # PNG signature plus synthetic bytes; upload does not decode it.
                payload.write_bytes(b"\x89PNG\r\n\x1a\nfixture")
                helper = root / "wl-paste"
                types = {
                    "files": "text/uri-list\\nimage/png\\ntext/plain",
                    "image": "image/png\\ntext/plain",
                    "text": "text/plain",
                }[kind]
                helper.write_text(
                    "#!/usr/bin/python3\nimport sys\nfrom pathlib import Path\n"
                    f"kind={kind!r}\n"
                    f"if '--list-types' in sys.argv: print('{types}')\n"
                    f"elif kind=='files': print({file.as_uri()!r})\n"
                    f"elif kind=='image': sys.stdout.buffer.write(Path({str(payload)!r}).read_bytes())\n"
                    "else: print('Pasted text', end='')\n"
                )
                helper.chmod(0o700)
                terminal = self.open(
                    server,
                    environment={
                        "WAYLAND_DISPLAY": "synthetic",
                        "PATH": folder + os.pathsep + os.environ.get("PATH", ""),
                    },
                )
                try:
                    terminal.send(key)
                    self.wait(
                        terminal,
                        {
                            "files": "copied.txt 🗙]",
                            "image": "clipboard.png 🗙]",
                            "text": "Pasted text",
                        }[kind],
                    )
                    self.assertEqual(server.message_count, 0)
                    terminal.send("\n")
                    self.wait(terminal, "Mensagem confirmada")
                    self.assertEqual(len(server.uploads[0]), 0 if kind == "text" else 1)
                    if kind == "image":
                        self.assertEqual(server.uploads[0][0]["data"], payload.read_bytes())
                    if kind == "files":
                        self.assertEqual(server.uploads[0][0]["data"], b"copied file")
                finally:
                    self.assertTrue(terminal.close())


if __name__ == "__main__":
    unittest.main()
