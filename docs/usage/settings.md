# Settings

## Open Settings

Press **Ctrl+S**, or click the gear below your name in the box at the bottom of
the channel list, to open **Configurações** (Settings). Settings opens only
from a server you are signed in to. Use ←/→ on the section selector to browse
its sections:

| Section | What it holds |
| --- | --- |
| **Perfil** (Profile) | Your nickname, status, description, avatar and banner on this server; see [Edit your own profile](profile-and-account.md#edit-your-own-profile) |
| **Aparência** (Appearance) | The [application mode](application-modes.md), the [interface language](#interface-language) and the [theme and history display](#theme-and-history-display) |
| **Notificações** (Notifications) | How the account is notified; see [Account notifications](notifications.md#account-notifications) |
| **Áudio** (Audio) | Devices, volumes, microphone test and push-to-talk; see [Voice calls and audio settings](voice-calls.md) |
| **Acessibilidade** (Accessibility) | Contrast, colors, symbols and other [accessibility](accessibility.md) choices |
| **Atalhos** (Shortcuts) | The keys of commands; see [Customize shortcuts](shortcuts.md) |
| **Atualizações** (Updates) | The update check and the download of a newer version; see [Updates](updates.md) |
| **Sistema** (System) | [Where this device's settings are saved](#where-this-devices-settings-are-saved) |
| **Conta** (Account) | Your password on this server; see [Change your password](profile-and-account.md#change-your-password) |
| **Sobre** (About) | The version and license; see [About Papo TUI](about.md) |

## Account and device settings

Your account on each server keeps its own appearance and notification
settings. They are saved on that server, so another device signed in to the same
account uses them too, and each server tab can have different ones. The
appearance follows the server tab you are looking at; the notifications of each
server follow that server's account.

## Interface language

Press **Ctrl+S**, go to **Aparência** (Appearance) and change **Idioma**
(Language) below the [application modes](application-modes.md): **Sistema**
(System), **Português** or **English**. System, the default, uses Portuguese
when it comes before English in your system's language settings (`LANGUAGE`,
`LC_ALL`, `LC_MESSAGES` or `LANG` on Linux and Termux, the display language on
Windows) and English otherwise. To choose for one run without changing the
setting, start the application with `PAPO_LANGUAGE=pt` or `PAPO_LANGUAGE=en`
while the setting is System.

The language belongs to this device and applies as soon as you select
**Salvar e aplicar** (Save and apply). It changes the application's own text;
messages, names, topics and other text written by people stay as they are.
The option words of the message box commands stay as they are, so
`/banner remover` works in both languages, and `/status` accepts `away` and
`busy` as well as `ausente` and `ocupado`. This guide names buttons and notices
as they appear in Portuguese, followed by their meaning in English, which is
also what the English interface shows. On Windows, the titles of the native
file dialogs stay in Portuguese.

## Theme and history display

Press **Ctrl+S** and go to **Aparência** (Appearance). Below the
[application modes](application-modes.md) and the interface language, which
belong to this device, are the account's settings:

- **Tema** (Theme): **Escuro** (Dark), **Claro** (Light) or **Sistema**
  (System). System uses the light theme when the terminal reports a light
  background, and the dark theme otherwise or when the terminal does not say.
- **Densidade** (Density): **Compacta** (Compact) removes the blank line above
  each author's messages, **Normal** keeps it, and **Confortável**
  (Comfortable) also leaves a blank line between messages from the same author.
- **Mostrar horários** (Show times): the time beside each author's name.
- **Mostrar avatares** (Show avatars): the picture, or the initials in text
  mode, to the left of each author's messages. Without avatars, messages start
  at the left edge.

Use ↑/↓ to select an item and ←/→, Space or Enter to change it, or click it:
the left half of a choice goes back and the right half goes forward. The
selected item explains what it does below the list. Select
**Salvar e aplicar** (Save and apply). The mode applies at once; the account
part is sent to the server, and the window stays open until the server answers
with **Configurações da conta salvas.** (Account settings saved). The new
theme and display then apply. The font size is the terminal's: change it in
the terminal's own settings.

## Where this device's settings are saved

The application mode, the interface language, the audio settings, the
[shortcuts](shortcuts.md), the [accessibility](accessibility.md) choices,
the switches of [this device](notifications.md#this-device), the player volume, the request limits and the saved server
profiles belong to this computer. They are kept in one file, `config.json`:

- Linux and Termux: `~/.config/papo-tui/config.json`, or
  `$XDG_CONFIG_HOME/papo-tui/config.json` when that variable holds an absolute
  path.
- Windows: `%LOCALAPPDATA%\papo-tui\config.json`.

To see the exact path on your computer, press **Ctrl+S** and open **Sistema**
(System), the tab before **Conta**.
Under **Arquivo de configuração:** (Configuration file) the tab shows the whole
path, on up to three lines, and then the state of the file:

- **Em uso** (In use): changes made on this computer are saved in it.
- **Ainda não existe** (It does not exist yet): nothing was saved on this
  computer so far; the first saved change creates the file.
- **Bloqueado** (Locked): the file is kept as it is and nothing is saved over
  it. The text says why: the file was saved by a newer version of Papo TUI,
  with its format number and the newest one this version reads; the file is
  damaged or is not a valid configuration; or it could not be read as a private
  file of yours.
- **Indisponível** (Unavailable): the configuration folder could not be opened.

**Copiar caminho** (Copy path) copies the path to the clipboard and shows
**Cópia solicitada.** (Copy requested), so you can open the file in an editor.
The tab changes no setting; **Fechar** (Close) or Esc leaves it.

Passwords and session cookies are never written to this file. If the
application reports that the configuration is locked, see
[Troubleshooting](troubleshooting.md).
