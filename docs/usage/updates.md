# Updates

Press **Ctrl+S** and choose **Atualizações** (Updates), the section between
**Atalhos** and **Sistema** of **Configurações** (Settings). It has two
switches, **Verificar atualizações automaticamente** (Check for updates
automatically) and **Aceitar versões beta** (Accept beta versions), the result
of the last check and, at the bottom, a button that changes with what there is
to do: **Verificar agora** (Check now), **Baixar atualização** (Download
update), **Baixar e instalar** (Download and install), **Cancelar download**
(Cancel download), **Copiar comando** (Copy command) or **Instalar e sair**
(Install and quit). Tab or ↓ moves from the section selector to the lines;
Space, Enter or a click changes a switch.

## Check for a new version

Once a day the program asks GitHub whether a newer release exists: about three
seconds after it starts, when the last answer is at least a day old, and then
every 24 hours while it stays open. The request goes to `api.github.com`,
outside any chat server: it carries no cookie and nothing about your accounts,
only the program's version, and GitHub sees your network address as any site
you visit does. Only final versions are offered, unless you turn on
[**Aceitar versões beta**](#accept-beta-versions).

When there is a newer version:

- the sign-in screen adds **Versão X disponível** (Version X available) to its
  message line;
- the conversation screen shows **Versão X disponível · Ctrl+S → Atualizações** in the
  bottom bar, or **Atualização disponível** (Update available) when the bar is
  short. Click it to open **Atualizações**. It leaves the bar once you have opened
  **Atualizações**, until the program starts again;
- **Atualizações** shows **Versão X disponível. Baixe manualmente em:** (Version X
  available. Download it by hand at:) and the address of that release's page.
  Press Enter on the address, or click it, to copy it, open it in a browser and
  download the package for your system. When the program can also fetch that
  package for you, the line reads **Baixe com o botão ou manualmente em:**
  (Download it with the button or by hand at:).

## Turn the automatic check on or off

**[x] Verificar atualizações automaticamente** (Check for updates
automatically) turns the daily check on or off; the change is saved at once
for this device. With it off,
nothing is asked until you select **Verificar agora** (Check now), which asks
right away. Two requests are at least a minute apart: sooner than that the
button answers **Aguarde um minuto antes de verificar de novo.** (Wait a minute
before checking again). Below the switches, **Você está na versão mais
recente.** (You are on the latest version) reports the last answer.

## Accept beta versions

**[ ] Aceitar versões beta** (Accept beta versions), below that switch, is off
when the program is installed: you are told about final versions only, even
when the copy you run is itself a beta. Turn it on, with Space, Enter or a
click, to be offered pre-releases too: alpha and beta versions and release
candidates, which arrive earlier and are less tested. The newest version of
any kind is then the one offered, so a final version still replaces the
pre-releases that led to it.

The change is saved at once for this device, and the program asks GitHub again
a moment later, because the answer it had was for the other choice; a download
in progress is cancelled. Turning the switch off never takes you back to an
older version: when the pre-release you run is newer than the latest final
version, **Atualizações** says you are on the latest version until a newer final one
is published.

To keep a run from asking by itself whatever the saved switch says, for example
before you ever sign in, start the program with `PAPO_UPDATE_CHECK=0`. A copy
built from source that is not a release build never asks by itself either.

## Download the new version

When the newer release has a package for the way your copy was installed, the
button in **Atualizações** becomes **Baixar atualização** (Download update). The
program picks the file that matches your installation and processor: the DEB
for Ubuntu or for Debian, the RPM for Fedora, the Arch package, the AppImage,
the Flatpak bundle, the Termux package, or the Windows installer or ZIP.

1. Select **Baixar atualização**. **Salvar atualização** (Save update) asks
   where to save the file; Esc gives up before anything is downloaded.
2. **Atualizações** shows **Conferindo a assinatura da versão…** (Checking the
   version's signature) while the program makes sure the release is the
   project's; see [How a download is trusted](#how-a-download-is-trusted). The
   file is then downloaded from GitHub into a private temporary folder while
   **Atualizações** shows **Baixando … 42%**. You can close Settings and keep
   chatting. **Cancelar download** (Cancel download) stops it and removes what
   had arrived.
3. The program compares the file's size and SHA-256 with the ones published
   for it. Only a file that matches is moved to the place you chose; **O
   arquivo baixado não confere com o publicado e foi descartado.** (The
   downloaded file does not match the published one and was discarded) means it
   did not, and nothing was saved.
4. **Atualizações** then shows **Pacote conferido e salvo em** (Package checked and
   saved to) with the path, and how to install it: for example **Instale com:**
   (Install with:) and `sudo apt install '/home/you/Downloads/papo-tui-….deb'`.
   **Copiar comando** (Copy command), or Enter on that line, copies it. Run it
   in a terminal after closing the program.

For these packages the program does not install anything or replace its own
files: you run the command, with your password where the package manager asks
for one.

## How a download is trusted

Every release is signed with the project's key. Before it asks for a single
byte of the package, the program fetches the release's list of checksums
(`SHA256SUMS`) and its signature (`SHA256SUMS.asc`), and continues only when:

- the signature of that list was made with the project's release key, which is
  built into the program (OpenPGP key `AA09 D90E 1547 7426 DAFD F098 30F8 5714
  E332 3D6B`, the one each release publishes as `papo-tui-signing-key.asc`);
- the list names the package for your installation, with the same SHA-256
  GitHub reports for it.

The package is then downloaded and its own SHA-256 compared with that signed
value. So a file is accepted only when the holder of the project's key vouched
for exactly those bytes: someone who could change what GitHub serves, or the
connection to it, still could not make the program accept another file.

**Esta versão não publica uma assinatura; nada foi baixado.** (This version
publishes no signature; nothing was downloaded) means the release has no such
list or signature. **A assinatura desta versão não confere; nada foi baixado.**
(This version's signature does not match; nothing was downloaded) means the
signature is not the project's, or the list does not name the package with
that SHA-256. In both cases nothing of the package was requested; use the
address of the release page and check the signatures by hand, as the release
notes describe, before installing anything.

**Baixar atualização** does not appear, and the address of the release page is
all **Atualizações** offers, when the release has no package for your installation,
when the program cannot tell how it was installed (a copy built from source, or
unpacked from a portable archive older than this feature), or when GitHub lists
the file without a SHA-256.

## Let the program install the new version

Two kinds of copy belong to you and not to a package manager, so the program
can replace them itself: an **AppImage** file, and a folder unpacked from the
**portable archive** (`papo-tui-…-linux-….tar.gz`). For these the button is
**Baixar e instalar** (Download and install), when you can write to the folder
that holds the file or the program's folder.

1. Select **Baixar e instalar**. Nothing asks where to save: the new version is
   prepared in a hidden folder beside the one being replaced, and checked
   against the size and SHA-256 GitHub publishes. The portable archive is also
   unpacked and inspected there: it must hold exactly that version of the
   program. **Cancelar download** stops and removes it.
2. **Atualizações** shows **Versão X pronta** (Version X ready) and the button becomes
   **Instalar e sair** (Install and quit). Until you select it, the copy you are
   running is untouched.
3. Select **Instalar e sair**. The program closes and, once nothing of it is in
   use, puts the new version in place, then prints **Papo TUI atualizado para a
   versão X. Abra o programa novamente.** (Papo TUI was updated to version X.
   Open the program again). Start it again the way you always do: the AppImage
   keeps its name and permissions, and the folder keeps its name.

Leaving the program any other way discards what was prepared; nothing is
installed. After a portable folder is replaced, the old one stays beside it as
`<folder>.previous` until the new version has started once, and is then
removed.

A portable folder is replaced whole, so it must hold only the program. If you
unpacked the archive over a folder that has other things, for example
`~/.local`, the program finds names the new version does not have and replaces
nothing.

If the new version cannot be put in place, the one you have stays, and the
program says where it left the new file, already checked. On Windows, and for
every packaged installation, use **Baixar atualização** as described above.
