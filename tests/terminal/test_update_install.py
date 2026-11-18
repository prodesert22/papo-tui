"""Replacing an AppImage or an unpacked archive with the newer version, through the real TUI."""

from test_context import main as test_main

import io
import os
import platform
import stat
import tarfile
import tempfile
import time

from mock_server import ChunkedBody
from settings_mock import SettingsMock
from update_mock import asset, release, update_mock
from update_terminal import UpdateSuite

ARCH = {"x86_64": "x86_64", "amd64": "x86_64", "aarch64": "aarch64", "arm64": "aarch64"}.get(
    platform.machine().lower(), platform.machine().lower()
)
IMAGE = f"PapoTUI-99.0.0-linux-{ARCH}.AppImage"
ARCHIVE = f"papo-tui-99.0.0-linux-{ARCH}.tar.gz"
READY = "Versão 99.0.0 pronta: Instalar e sair a coloca no lugar desta."
UPDATED = "Papo TUI atualizado para a versão 99.0.0. Abra o programa novamente."
MARKER = "share/papo-tui/install-channel"


def archive(top="papo-tui-99.0.0", marker="tarball\n", program=b"new program", extra=()):
    """The bytes of a portable archive; extra adds (name, target) links under top."""
    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode="w:gz") as tar:

        def add(name, content, mode=0o644):
            info = tarfile.TarInfo(top + "/" + name)
            info.size = len(content)
            info.mode = mode
            tar.addfile(info, io.BytesIO(content))

        add("bin/papo-tui", program, 0o755)
        add("lib/papo-tui/libnew.so", b"library")
        if marker is not None:
            add(MARKER, marker.encode())
        for name, target in extra:
            info = tarfile.TarInfo(top + "/" + name)
            info.type = tarfile.SYMTYPE
            info.linkname = target
            tar.addfile(info)
    return data.getvalue()


class UpdateInstall(UpdateSuite):
    def setUp(self):
        self.root = tempfile.TemporaryDirectory(prefix="pi-")
        self.addCleanup(self.root.cleanup)
        self.place = os.path.join(self.root.name, "apps")
        os.mkdir(self.place)

    def start(self, server, hub, channel, target):
        """Signs in as that kind of installation at target and opens Sobre."""
        environment = dict(hub.environment, PAPO_UPDATE_CHANNEL=channel, PAPO_UPDATE_TARGET=target)
        t = self.login(server, environment=environment)
        self.wait(t, "Mensagem 0001")
        self.calls(hub, 1)
        self.open_sobre(t)
        self.wait(t, "Versão 99.0.0 disponível. Baixe com o botão ou manualmente em:")
        return t

    def press(self, t, first=False):
        t.send("\t\t\n" if first else "\n")

    def staged(self):
        """The folders prepared beside the installation."""
        return [name for name in os.listdir(self.place) if name.startswith(".papo-tui-update-")]

    def leftovers(self):
        end = time.monotonic() + 3
        while self.staged() and time.monotonic() < end:
            time.sleep(0.05)
        return self.staged()

    def ended(self, t, seconds=10):
        """Waits for the program to end by itself and returns what it printed last."""
        end = time.monotonic() + seconds
        while t.process.poll() is None and time.monotonic() < end:
            t.read(0.05)
        self.assertIsNotNone(t.process.poll(), "the program is still running")
        t.read(0.2)
        return t.output.decode("utf-8", "replace")

    def image(self, mode=0o750):
        target = os.path.join(self.place, "Papo.AppImage")
        with open(target, "wb") as f:
            f.write(b"old image")
        os.chmod(target, mode)
        return target

    def folder(self):
        """An installation unpacked from the portable archive, with a library of its own."""
        target = os.path.join(self.place, "papo-tui-1.0.0")
        for name, content, mode in (
            ("bin/papo-tui", b"old program", 0o755),
            (MARKER, b"tarball\n", 0o644),
            ("lib/papo-tui/libold.so", b"old", 0o644),
        ):
            path = os.path.join(target, name)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(content)
            os.chmod(path, mode)
        return target

    def read(self, *parts):
        with open(os.path.join(*parts), "rb") as f:
            return f.read()

    def test_appimage_is_replaced_when_the_program_leaves_to_install(self):
        data = os.urandom(150_000)
        target = self.image()
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = [release("v99.0.0", assets=[asset(IMAGE, data)])]
            hub.files["v99.0.0/" + IMAGE] = (302, None, None, None, None, hub.url("s/" + IMAGE))
            hub.files["s/" + IMAGE] = data
            t = self.start(server, hub, "appimage", target)
            # A copy that can replace itself asks for no place to save.
            self.wait(t, "Baixar e instalar")
            self.press(t, first=True)
            self.wait(t, READY)
            self.wait(t, "Instalar e sair")
            self.assertNotIn("Salvar atualização", self.text(t))
            self.dump(t, "update-staged")
            # Prepared beside the file, which is untouched while the program runs.
            self.assertEqual(len(self.staged()), 1)
            self.assertEqual(self.read(target), b"old image")
            self.press(t)
            output = self.ended(t)
            self.assertIn(UPDATED, output)
            self.assertEqual(t.process.poll(), 0)
            self.assertEqual(self.read(target), data)
            # The permissions are the ones the old file had.
            self.assertEqual(stat.S_IMODE(os.stat(target).st_mode), 0o750)
            self.assertEqual(self.staged(), [])
            self.assertEqual(os.listdir(self.place), ["Papo.AppImage"])
            self.assertTrue(all(cookie is None for _, cookie in hub.downloads))

    def test_unpacked_archive_is_swapped_and_the_old_copy_goes_on_the_next_start(self):
        data = archive(extra=(("bin/alias", "papo-tui"),))
        target = self.folder()
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = [release("v99.0.0", assets=[asset(ARCHIVE, data)])]
            hub.files["v99.0.0/" + ARCHIVE] = data
            t = self.start(server, hub, "tarball", target)
            self.wait(t, "Baixar e instalar")
            self.press(t, first=True)
            self.wait(t, READY)
            self.assertEqual(self.read(target, "bin/papo-tui"), b"old program")
            self.press(t)
            self.assertIn(UPDATED, self.ended(t))
            # The folder keeps its name and now holds the newer tree, whole.
            self.assertEqual(self.read(target, "bin/papo-tui"), b"new program")
            self.assertTrue(os.access(os.path.join(target, "bin/papo-tui"), os.X_OK))
            self.assertEqual(self.read(target, "bin/alias"), b"new program")
            self.assertEqual(self.read(target, "lib/papo-tui/libnew.so"), b"library")
            self.assertFalse(os.path.exists(os.path.join(target, "lib/papo-tui/libold.so")))
            # The old copy waits beside it until the new one has started once.
            previous = target + ".previous"
            self.assertEqual(self.read(previous, "lib/papo-tui/libold.so"), b"old")
            self.assertEqual(self.staged(), [])
            hub.answer = [release("v0.5.0")]
            t = self.login(
                server,
                environment=dict(
                    hub.environment, PAPO_UPDATE_CHANNEL="tarball", PAPO_UPDATE_TARGET=target
                ),
            )
            self.wait(t, "Mensagem 0001")
            self.assertFalse(os.path.exists(previous))
            self.assertEqual(self.read(target, "bin/papo-tui"), b"new program")

    def test_leaving_another_way_or_cancelling_puts_nothing_in_place(self):
        data = os.urandom(300_000)
        target = self.image()
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = [release("v99.0.0", assets=[asset(IMAGE, data)])]
            # Arrives in pieces a third of a second apart.
            hub.files["v99.0.0/" + IMAGE] = (
                200,
                ChunkedBody(data, pieces=20, delay=0.3, content_length=True),
            )
            t = self.start(server, hub, "appimage", target)
            self.wait(t, "Baixar e instalar")
            self.press(t, first=True)
            self.wait(t, "Baixando " + IMAGE)
            self.wait(t, "Cancelar download")
            self.assertEqual(len(self.staged()), 1)
            self.press(t)
            self.wait(t, "Baixar e instalar")
            self.assertEqual(self.leftovers(), [])
            # Prepared this time, but the program is left with its usual key.
            hub.files["v99.0.0/" + IMAGE] = data
            self.press(t)
            self.wait(t, READY)
            self.assertEqual(len(self.staged()), 1)
            t.send("\x1b")
            self.wait(t, "Mensagem 0001")
            t.send("\x11")
            output = self.ended(t)
            self.assertNotIn("atualizado", output)
            self.assertEqual(self.read(target), b"old image")
            self.assertEqual(self.staged(), [])

    def test_archive_that_is_not_the_program_is_discarded(self):
        target = self.folder()
        cases = (
            ("another version inside", archive(top="papo-tui-98.0.0")),
            ("no marker", archive(marker=None)),
            ("a link out of the tree", archive(extra=(("share/secret", "/etc/passwd"),))),
            ("not an archive", b"this is not a gzip archive at all"),
        )
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = [release("v99.0.0", assets=[asset(ARCHIVE, cases[0][1])])]
            t = self.start(server, hub, "tarball", target)
            for index, (case, data) in enumerate(cases):
                with self.subTest(case=case):
                    # GitHub publishes this very file: only its content is wrong.
                    hub.answer = [release("v99.0.0", assets=[asset(ARCHIVE, data)])]
                    hub.files["v99.0.0/" + ARCHIVE] = data
                    if index:
                        # A new answer is needed for the new size and digest.
                        t.close()
                        hub.server.requests.clear()
                        t = self.start(server, hub, "tarball", target)
                    self.wait(t, "Baixar e instalar")
                    self.press(t, first=True)
                    self.wait(t, "O arquivo baixado não contém a versão esperada")
                    self.assertEqual(self.leftovers(), [])
                    self.assertNotIn("Instalar e sair", self.text(t))
                    self.assertEqual(self.read(target, "bin/papo-tui"), b"old program")
                    self.assertFalse(os.path.exists(target + ".previous"))
            self.dump(t, "update-unusable")

    def test_place_that_cannot_be_replaced_keeps_this_version_and_the_new_file(self):
        data = os.urandom(40_000)
        target = self.image()
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = [release("v99.0.0", assets=[asset(IMAGE, data)])]
            hub.files["v99.0.0/" + IMAGE] = data
            t = self.start(server, hub, "appimage", target)
            self.wait(t, "Baixar e instalar")
            self.press(t, first=True)
            self.wait(t, READY)
            staged = os.path.join(self.place, self.staged()[0], IMAGE)
            # Something else took the file's name while the program was open.
            os.remove(target)
            os.mkdir(target)
            self.press(t)
            output = self.ended(t)
            self.assertNotIn("atualizado", output)
            self.assertIn("Não foi possível instalar a versão 99.0.0", output)
            self.assertIn(staged, output)
            self.assertTrue(os.path.isdir(target))
            # The checked file is left where the message says.
            self.assertEqual(self.read(staged), data)

    def test_folder_shared_with_something_else_is_never_renamed_away(self):
        data = archive()
        target = self.folder()
        # As when the archive was unpacked over a folder other programs use.
        os.makedirs(os.path.join(target, "state", "papo-tui"))
        with open(os.path.join(target, "bin", "other-tool"), "wb") as f:
            f.write(b"theirs")
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = [release("v99.0.0", assets=[asset(ARCHIVE, data)])]
            hub.files["v99.0.0/" + ARCHIVE] = data
            t = self.start(server, hub, "tarball", target)
            self.wait(t, "Baixar e instalar")
            self.press(t, first=True)
            self.wait(t, READY)
            self.press(t)
            output = self.ended(t)
            self.assertNotIn("atualizado", output)
            self.assertIn("Não foi possível instalar a versão 99.0.0", output)
            self.assertEqual(self.read(target, "bin/papo-tui"), b"old program")
            self.assertEqual(self.read(target, "bin/other-tool"), b"theirs")
            self.assertTrue(os.path.isdir(os.path.join(target, "state", "papo-tui")))
            self.assertFalse(os.path.exists(target + ".previous"))
            # The new tree, checked, is left where the message says.
            staged = os.path.join(self.place, self.staged()[0], "tree", "papo-tui-99.0.0")
            self.assertIn(staged, output)
            self.assertEqual(self.read(staged, "bin/papo-tui"), b"new program")

    def test_folder_without_room_for_the_new_version_reports_it(self):
        data = os.urandom(40_000)
        target = self.image()
        os.chmod(self.place, 0o555)
        self.addCleanup(os.chmod, self.place, 0o755)
        if os.access(self.place, os.W_OK):
            self.skipTest("This user writes to read-only folders.")
        with SettingsMock() as server, update_mock() as hub:
            hub.answer = [release("v99.0.0", assets=[asset(IMAGE, data)])]
            hub.files["v99.0.0/" + IMAGE] = data
            t = self.start(server, hub, "appimage", target)
            self.wait(t, "Baixar e instalar")
            self.press(t, first=True)
            self.wait(t, "Não foi possível gravar o arquivo temporário.")
            self.assertEqual(hub.downloads, [])
            self.assertEqual(self.read(target), b"old image")


if __name__ == "__main__":
    test_main()
