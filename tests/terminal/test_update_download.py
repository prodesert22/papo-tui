"""Downloading the package of a newer version from a stand-in for GitHub, through the real TUI."""

from test_context import main as test_main

import json
import os
import platform
import tempfile
import time

from mock_server import ChunkedBody
from settings_mock import SettingsMock
from update_mock import asset, release, update_mock
from update_terminal import UpdateSuite

ARCH = {"x86_64": "x86_64", "amd64": "x86_64", "aarch64": "aarch64", "arm64": "aarch64"}.get(
    platform.machine().lower(), platform.machine().lower()
)
DEB = f"papo-tui-99.0.0-ubuntu-{ARCH}.deb"
RELEASE_PAGE = "https://github.com/prodesert22/papo-tui/releases/tag/v"
MISMATCH = "O arquivo baixado não confere com o publicado e foi descartado."
REFUSED = "O GitHub não entregou o arquivo. Tente mais tarde."


class UpdateDownload(UpdateSuite):
    def setUp(self):
        # The program's temporary folder is its own here, so leftovers show.
        # Short names: Sobre shows the saved path on one line of 72 cells.
        self.root = tempfile.TemporaryDirectory(prefix="pu-")
        self.addCleanup(self.root.cleanup)
        self.temporary = os.path.join(self.root.name, "t")
        self.saved = os.path.join(self.root.name, "s")
        os.mkdir(self.temporary)
        os.mkdir(self.saved)

    def start(self, server, hub, channel="deb-ubuntu"):
        """Signs in as an installation of that kind and opens Sobre once it knows the release."""
        environment = dict(hub.environment, PAPO_UPDATE_CHANNEL=channel, TMPDIR=self.temporary)
        t = self.login(server, environment=environment)
        self.wait(t, "Mensagem 0001")
        self.calls(hub, 1)
        self.open_sobre(t)
        self.wait(t, "Versão 99.0.0 disponível.")
        return t

    def press(self, t, first=False):
        """Enter on the section's button; the first time it is two Tabs away."""
        t.send("\t\t\n" if first else "\n")

    def save_to(self, t, target):
        self.wait(t, "Salvar atualização")
        self.wait(t, "Salvar em")
        t.send("\x03" + str(target) + "\n")

    def staged(self):
        """The update folders in the program's temporary directory (media has its own)."""
        return [name for name in os.listdir(self.temporary) if name.startswith("papo-update-")]

    def leftovers(self):
        """Update folders still there a moment after they should be gone."""
        end = time.monotonic() + 3
        while self.staged() and time.monotonic() < end:
            time.sleep(0.05)
        return self.staged()

    def offer(self, hub, name, data, **listed):
        hub.answer = [release("v99.0.0", assets=[asset(name, data, **listed)])]

    def test_package_follows_a_redirect_is_checked_saved_and_explained(self):
        data = os.urandom(200_000)
        target = os.path.join(self.saved, "novo.deb")
        with SettingsMock() as server, update_mock() as hub:
            self.offer(hub, DEB, data)
            # GitHub answers the release address with a redirect to its storage.
            hub.files["v99.0.0/" + DEB] = (302, None, None, None, None, hub.url("store/" + DEB))
            hub.files["store/" + DEB] = data
            t = self.start(server, hub)
            self.wait(t, "Baixar atualização")
            # The release page stays as the way to get it by hand.
            self.wait(t, "Versão 99.0.0 disponível. Baixe com o botão ou manualmente em:")
            self.assertEqual(hub.downloads, [])
            self.press(t, first=True)
            self.save_to(t, target)
            self.wait(t, "Pacote conferido e salvo em " + target)
            self.wait(t, "Instale com:")
            command = "sudo apt install '" + target + "'"
            self.wait(t, command)
            self.dump(t, "update-saved")
            with open(target, "rb") as f:
                self.assertEqual(f.read(), data)
            # Two requests, each to the stand-in, neither with a cookie.
            self.assertEqual(
                hub.downloads,
                [
                    ("/papo/github/download/v99.0.0/" + DEB, None),
                    ("/papo/github/download/store/" + DEB, None),
                ],
            )
            self.assertEqual(self.leftovers(), [])
            # The button now copies the command; so does Enter on its line.
            self.wait(t, "Copiar comando")
            self.assertEqual(self.copies(t), [])
            self.press(t)
            self.wait(t, "Cópia solicitada.")
            self.assertEqual(self.copies(t), [command])
            # The stored answer keeps what the download needs after a restart.
            with open(self.state_file(t), encoding="utf-8") as f:
                stored = json.load(f)["contexts"][0]
            self.assertEqual(stored["latest"], "99.0.0")
            self.assertEqual(stored["asset"]["name"], DEB)
            self.assertEqual(stored["asset"]["size"], len(data))
            t.send("\x1b")
            self.wait(t, "Arquivo salvo em")
            self.paths(hub)

    def test_file_that_is_not_the_published_one_is_discarded(self):
        data = os.urandom(120_000)
        with SettingsMock() as server, update_mock() as hub:
            self.offer(hub, DEB, data)
            t = self.start(server, hub)
            cases = (
                ("other bytes of the same size", os.urandom(len(data))),
                ("more bytes than published", data + b"extra"),
                ("fewer bytes than published", data[:-7]),
            )
            for index, (case, served) in enumerate(cases):
                with self.subTest(case=case):
                    target = os.path.join(self.saved, f"wrong-{index}.deb")
                    hub.files["v99.0.0/" + DEB] = served
                    self.wait(t, "Baixar atualização")
                    self.press(t, first=index == 0)
                    self.save_to(t, target)
                    self.wait(t, MISMATCH)
                    self.assertFalse(os.path.exists(target))
                    self.assertEqual(self.leftovers(), [])
                    # Nothing was saved: no command, and the download is offered again.
                    self.assertNotIn("Instale com:", self.text(t))
            self.dump(t, "update-mismatch")
            self.assertEqual(os.listdir(self.saved), [])
            # The published file, once served, is accepted after the failures.
            target = os.path.join(self.saved, "right.deb")
            hub.files["v99.0.0/" + DEB] = data
            self.press(t)
            self.save_to(t, target)
            self.wait(t, "Pacote conferido e salvo em " + target)
            with open(target, "rb") as f:
                self.assertEqual(f.read(), data)

    def test_redirects_elsewhere_and_missing_files_deliver_nothing(self):
        data = os.urandom(50_000)
        with SettingsMock() as server, update_mock() as hub:
            self.offer(hub, DEB, data)
            t = self.start(server, hub)

            def redirect(location):
                return (302, None, None, None, None, location)

            chain = {f"hop{n}/" + DEB: redirect(hub.url(f"hop{n + 1}/" + DEB)) for n in range(1, 5)}
            cases = (
                # (case, files, requests the stand-in must see)
                ("another host", {"v99.0.0/" + DEB: redirect("https://example.invalid/" + DEB)}, 1),
                (
                    "the same host without TLS",
                    {"v99.0.0/" + DEB: redirect(hub.url(DEB).replace("https://", "http://"))},
                    1,
                ),
                ("no file", {}, 1),
                # The first request and three redirects are sent; the fourth is not.
                (
                    "too many",
                    dict(chain, **{"v99.0.0/" + DEB: redirect(hub.url("hop1/" + DEB))}),
                    4,
                ),
            )
            for index, (case, files, requests) in enumerate(cases):
                with self.subTest(case=case):
                    target = os.path.join(self.saved, f"refused-{index}.deb")
                    hub.files.clear()
                    hub.files.update(files)
                    before = len(hub.downloads)
                    self.wait(t, "Baixar atualização")
                    self.press(t, first=index == 0)
                    self.save_to(t, target)
                    self.wait(t, REFUSED)
                    self.assertEqual(len(hub.downloads) - before, requests, hub.downloads)
                    self.assertFalse(os.path.exists(target))
                    self.assertEqual(self.leftovers(), [])
            self.assertEqual(os.listdir(self.saved), [])
            self.assertTrue(all(cookie is None for _, cookie in hub.downloads))

    def test_cancelling_the_dialog_or_the_download_leaves_nothing_behind(self):
        data = os.urandom(400_000)
        target = os.path.join(self.saved, "cancelled.deb")
        with SettingsMock() as server, update_mock() as hub:
            self.offer(hub, DEB, data)
            # Arrives in pieces a third of a second apart: about six seconds in all.
            hub.files["v99.0.0/" + DEB] = (
                200,
                ChunkedBody(data, pieces=20, delay=0.3, content_length=True),
            )
            t = self.start(server, hub)
            self.wait(t, "Baixar atualização")
            self.press(t, first=True)
            self.wait(t, "Salvar atualização")
            t.send("\x1b")
            # The footer has room for the start of the notice at 80 columns.
            self.wait(t, "Download da atualização ca")
            self.assertEqual(hub.downloads, [])
            self.wait(t, "Baixar atualização")
            self.press(t)
            self.save_to(t, target)
            self.wait(t, "Baixando " + DEB)
            self.wait(t, "Cancelar download")
            # The file is being written to the program's private temporary folder.
            self.assertEqual(len(self.staged()), 1)
            self.dump(t, "update-downloading")
            self.press(t)
            self.wait(t, "Baixar atualização")
            self.assertNotIn("Baixando " + DEB, self.text(t))
            self.assertEqual(self.leftovers(), [])
            self.assertFalse(os.path.exists(target))
            self.assertEqual(len(hub.downloads), 1)

    def test_command_follows_the_kind_of_installation(self):
        data = os.urandom(30_000)
        cases = (
            ("arch", f"papo-tui-99.0.0-arch-{ARCH}.pkg.tar.zst", "Instale com:", "sudo pacman -U "),
            (
                "rpm-fedora",
                f"papo-tui-99.0.0-fedora-{ARCH}.rpm",
                "Instale com:",
                "sudo dnf install ",
            ),
            (
                "appimage",
                f"PapoTUI-99.0.0-linux-{ARCH}.AppImage",
                "Torne o arquivo executável e abra-o no lugar do atual:",
                "chmod +x ",
            ),
        )
        for channel, name, label, verb in cases:
            with self.subTest(channel=channel), SettingsMock() as server, update_mock() as hub:
                target = os.path.join(self.saved, "p-" + channel)
                # The release has files for other installations too.
                hub.answer = [
                    release("v99.0.0", assets=[asset(DEB, b"deb"), asset(name, data)]),
                ]
                hub.files["v99.0.0/" + name] = data
                hub.files["v99.0.0/" + DEB] = b"deb"
                t = self.start(server, hub, channel=channel)
                self.wait(t, "Baixar atualização")
                self.press(t, first=True)
                self.save_to(t, target)
                self.wait(t, label)
                self.wait(t, verb + "'" + target + "'")
                self.assertEqual(hub.downloads, [("/papo/github/download/v99.0.0/" + name, None)])
                with open(target, "rb") as f:
                    self.assertEqual(f.read(), data)
                self.assertTrue(t.close())

    def test_no_download_is_offered_without_a_file_for_this_installation(self):
        with SettingsMock() as server, update_mock() as hub:
            # Listed, but for a Debian package; and one whose digest cannot be checked.
            fedora = f"papo-tui-99.0.0-fedora-{ARCH}.rpm"
            hub.answer = [
                release(
                    "v99.0.0",
                    assets=[asset(DEB, b"deb"), asset(fedora, b"rpm", digest="sha1:abc")],
                )
            ]
            for channel in ("deb-debian", "rpm-fedora", "not-a-channel"):
                with self.subTest(channel=channel):
                    t = self.start(server, hub, channel=channel)
                    # The version and its page are shown, to be fetched by hand; the
                    # button still only checks.
                    self.wait(t, "Versão 99.0.0 disponível. Baixe manualmente em:")
                    self.wait(t, RELEASE_PAGE + "99.0.0")
                    self.wait(t, "Verificar agora")
                    self.assertNotIn("Baixar atualização", self.text(t))
                    self.assertEqual(hub.downloads, [])
                    self.assertTrue(t.close())
                    hub.server.requests.clear()


if __name__ == "__main__":
    test_main()
