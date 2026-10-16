# Using Papo TUI

## Run the application

After [installing](instalacao.md), open a UTF-8 terminal with at least
44 columns and 22 rows and run:

```sh
papo-tui
```

For the Windows ZIP, open PowerShell in the extracted folder and run:

```powershell
.\papo-tui.exe
```

For Flatpak, run `flatpak run io.github.prodesert22.PapoTUI`. To run a local
Linux build from the project folder, use `./build/papo-tui`.

The application attempts to restore the last session when the system credential
store is available.

Use `papo-tui --help` to list command-line options and `papo-tui --version`
to check the installed version.

## Connect and chat

Enter the full instance URL, such as `https://chat.example.com`, your username
and your password. Enter a server password only for private instances.
Select **Entrar** (Sign in). To create an account, select **Cadastro** (Register)
and then sign in.

**Selecionar perfil** (Select profile) lets you save server names and URLs.
To open the profile manager directly, run `papo-tui --profiles`.
Passwords are not stored in the configuration.

Choose a channel with the arrow keys and Enter, or with the mouse. Type in the
editor and press **Enter** to send; **Shift+Enter** inserts a new line. **Ctrl+N**
also inserts a new line. Pasting text does not send the message automatically.
Switching conversations preserves drafts during the current run.

Scroll with the arrow keys, PageUp/PageDown or the mouse wheel over the history.
The next page loads as you approach either edge. A loading indicator appears
if you reach the edge before the messages arrive.

Closing the application discards drafts. Signing out also disables session
recovery. If a send remains unconfirmed, check the history before resending:
the message may already have reached the server.

Voice calls are not available yet.

## Keyboard and mouse

| Control | Action |
| --- | --- |
| Tab / Shift+Tab | Move focus |
| Esc | Close a modal, go back or cancel a connection |
| Ctrl+Q | Quit |
| Ctrl+C | Clear the field; quit when outside a field |
| ↑ / ↓ or mouse wheel over history | Scroll the conversation |
| PageUp / PageDown in history | Scroll by a page; nearby pages load automatically |
| Home / End in history | Go to the start of loaded history / recent messages |
| Ctrl+R | Refresh or retry a request/connection |
| Ctrl+T | Retry the last failed send |
| F6 | Open the account screen |
| Ctrl+L in a conversation | Sign out of the current account |
| F10 | Start a DM |
| F9 | Open the notification center and preferences |
| F4 in the editor | Choose an emoji |
| F7 in history | Open media |
| Shift+Enter in the editor | Insert a new line |

Type `@` in the editor to choose a person to mention; confirm with Tab or Enter.
After closing the chooser, Enter sends the message.

Type `:` to open emoji suggestions, then keep typing to filter Unicode and
server emojis by name or keyword. Use ↑/↓ to choose, Tab or Enter to insert,
and Esc to dismiss. Confirming a suggestion inserts it without sending the
message. Suggestions place your most frequently inserted emojis first.
Counts are saved per account and instance in the user configuration directory,
under `papo-tui/emoji-usage/`; if it cannot be written, ranking lasts for the
current run.

F4 opens the full emoji picker. Each tile shows a 32×32 preview with its name
below. Use ↓ to move from search to the Unicode/Servidor buttons, then ↓ again
to enter the grid. From its first row, ↑ returns to the buttons; ↑ again returns
to search. Use ←/→ to switch emoji sources while the buttons have focus.
Tab and Shift+Tab also move between these areas.

## Images and video

Press F7 with the history focused, or click a media preview, to open it.
If several items are visible, choose one with ↑/↓ and Enter or click it.
Videos finish downloading before playback starts. Press Esc or click outside
the media window to close it or cancel loading.

| Player control | Action |
| --- | --- |
| Space | Pause or resume |
| ← / → | Seek backward / forward by 5 seconds |
| `,` or `<` / `.` or `>` | Seek backward / forward by 1 second |
| ↑ / ↓ | Increase / decrease the volume |
| `m` | Mute or unmute |
| Esc | Close the player or cancel loading |

On the control bar, click the left button to pause or resume, the timeline to
seek, and the right bar to adjust the volume from 0 to 100%. The volume gradient
is green up to 60%, yellow up to 85% and red above that.
The button shows `[▶]` to resume and `[⏸]` to pause.

Videos scale up or down to fit the player, preserving their aspect ratio without
cropping. Their size adjusts when you resize the terminal.

Click **Info**, or press `i` or F8, to show or hide the media properties.
Use PageUp/PageDown or the mouse wheel over the properties panel to scroll it.

## Troubleshooting

- **Command not found:** check that the application is installed and its folder
  is on PATH. For a portable package, run the executable from the extracted
  folder; see the [installation guide](instalacao.md).
- **Cannot connect:** check the final URL, instance availability and credentials.
  Private servers also require the server password.
- **Untrusted certificate:** install the instance's CA or run
  `CURL_CA_BUNDLE=/path/to/ca.pem papo-tui`. In PowerShell, set
  `$env:CURL_CA_BUNDLE='C:\path\to\ca.pem'` before running the application.
  TLS verification remains enabled.
- **Local HTTP server:** run `papo-tui --allow-local-http`. This option allows
  HTTP only on `localhost`, `127.0.0.1` and `[::1]`.
- **Request limit:** wait for the indicated period and try again.
  Sends are not retried automatically.
- **Images do not appear:** check that the terminal supports Kitty graphics,
  Sixel or iTerm2 images. Without a
  supported protocol, chat remains text-only.
- **Emoji pictures are missing:** install the Noto Color Emoji font and restart
  the application.
- **Video is still loading:** playback starts after the download finishes.
  Check the connection if it does not progress. Press Esc to cancel; if an
  error appears, press Enter in the media window to retry.
- **Video plays without sound:** press `m` to unmute, raise the volume in the
  player and check the system's audio output. If no audio device is available,
  playback can continue without sound.
- **Missing media module:** reinstall the complete package. For the ZIP, keep
  the DLLs and the `papo-backends` folder alongside the program.
- **Session is not remembered:** unlock the system credential store. Without an
  available store, you must sign in at the next start; cookies are not saved as
  plain text.
- **Small screen or intercepted keys:** enlarge the window and check the
  terminal's reserved shortcuts. Use the mouse if a shortcut does not reach
  the application.
- **Shift+Enter sends instead of adding a line:** use a terminal that supports
  reporting modified Enter, such as Kitty. If the terminal cannot distinguish
  it from Enter, use Ctrl+N to insert a new line.
- **Linux terminal settings were disrupted:** run `reset`, even if the
  characters do not appear as you type.
