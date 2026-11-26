# Troubleshooting

## Installation, sign-in and connection

- **Command not found:** check that the application is installed and its folder
  is on PATH. For a portable package, run the executable from the extracted
  folder; see the [installation guide](../installation.md).
- **Cannot connect:** check the final URL, instance availability and credentials.
  Private servers also require the server password.
- **A tab shows (offline):** the server is not answering. Wait for it to come
  back; the application reconnects by itself, or open the tab to try at once.
- **Disconnected:** the application reconnects by itself; press **F5** in the
  conversation to try at once.
- **Server reports an IP block:** contact the instance administrator. A block
  during private server authorization happens before your account is checked;
  an already open session in another client does not confirm that a new login
  is allowed. Account or IP blocks reported during login are shown separately
  from a refusal to validate an existing session.
- **Untrusted certificate:** install the instance's CA or run
  `CURL_CA_BUNDLE=/path/to/ca.pem papo-tui`. In PowerShell, set
  `$env:CURL_CA_BUNDLE='C:\path\to\ca.pem'` before running the application.
  TLS verification remains enabled.
- **Local HTTP server:** run `papo-tui --allow-local-http`. This option allows
  HTTP only on `localhost`, `127.0.0.1` and `[::1]`.
- **Request limit:** wait for the indicated period and try again.
  Sends are not retried automatically.
- **Forgot your password:** ask the server administration for a reset link and
  use **[Redefinir senha]** on the sign-in screen. A link works only once and
  only on the server that created it.
- **The status bar shows Sessão reutilizada · troque a senha** (Session reused ·
  change the password): the server saw an old session of your account being
  used again, so it ended your other sessions. This can happen when another
  device or a copy of your saved session (for example a synced or restored
  folder) uses it, or when someone else has it. You stay signed in. If it was
  not you, change the password in **Ctrl+S → Conta** (Account); the warning
  disappears after the change, when you sign in with the new password.
- **Session is not remembered:** unlock the system credential store. Without an
  available store, you must sign in at the next start; cookies are not saved as
  plain text. A second window of the application does not restore or save
  sessions while the first one is open.

## Terminal and keyboard

- **Small screen or intercepted keys:** enlarge the window and check the
  terminal's reserved shortcuts. Use the mouse if a shortcut does not reach
  the application.
- **Shift+Enter sends instead of adding a line:** use a terminal that supports
  reporting modified Enter, such as Kitty. If the terminal cannot distinguish
  it from Enter, use Ctrl+N to insert a new line, or Ctrl+O in the channel list to
  open a voice channel's messages.
- **Ctrl+Backspace deletes only one character:** some terminals send the same
  code for Backspace and Ctrl+Backspace. Use Ctrl+W to delete the word before
  the cursor, or a terminal that reports the modified key, such as GNOME
  Terminal.
- **Alt+arrows switch to another console:** on the Linux console these keys
  belong to the system. Use Tab and Shift+Tab to move the focus; press Alt+→
  or Alt+← to return to the console the application runs on.
- **Linux terminal settings were disrupted:** run `reset`, even if the
  characters do not appear as you type.
- **The on-screen keyboard does not open on Android:** in Termux, a tap only
  opens the soft keyboard while the terminal is not reporting mouse events.
  The application pauses the mouse reports as soon as the tap that focuses a
  field ends, so the keyboard opens a moment later. If it stays closed, show it
  with the keyboard button in the Termux drawer.

## Interface and settings

- **A field got a red border:** the program refused what you typed in that field,
  for example an empty required name, a password that does not match, an invalid
  date or a message with nothing in it. The field keeps its own background and
  is surrounded by a red border (only its two sides where there is no room for
  the lines above and below), the cursor is already in it and the message at the
  bottom stays until your next action, saying what to fix and naming the field
  when it was left empty, such as **Preencha o campo Nome.** (Fill in the Name
  field). The border goes away with the first character you type or paste
  there. This applies to the sign-in form, the **Configurações do servidor**
  forms, **Perfil** and **Conta** in Settings, the server profile dialogs, the
  save dialog and the message box.
- **A Server settings tab is missing:** your account lacks the permission that
  tab needs; ask the server owner. If the panel shows **Atualizando
  permissões…**, wait a moment or press Ctrl+R in the conversation to refresh
  permissions, then reopen the panel.
- **The System theme stays dark on a light terminal:** the terminal did not
  report its background color. Choose **Claro** (Light) in Ctrl+S → Aparência.
- **Text is too small or too large:** the font size comes from the terminal;
  change it there. The server's font size setting does not apply in a terminal.
- **Settings shows Configuração bloqueada** (Configuration locked) **when you
  save:** the application could not use `config.json` when it started, so it
  left the file untouched and refuses to save over it. Changes to the
  application mode, the interface language, the audio settings, the shortcuts
  and the accessibility choices are not kept until this is solved. **Ctrl+S → Sistema** shows the path of the file and the
  reason; see
  [Where this device's settings are saved](settings.md#where-this-devices-settings-are-saved).
  - **Configuração bloqueada: arquivo de uma versão mais nova do Papo TUI.**
    (Configuration locked: file from a newer version of Papo TUI): the file is
    sound, but a newer version of the application saved it in a format this
    one does not read. Use that newer version again, or update this
    installation. To keep using the older version, move the file elsewhere;
    it then starts with the default preferences and no saved servers.
  - **Configuração bloqueada; corrija o arquivo e use Ctrl+R.** (Configuration
    locked; fix the file and use Ctrl+R): the file is damaged, is not a valid
    configuration, or is not a private file of your user. Correct it, or move
    it elsewhere to start with the default preferences and no saved servers.

  Then restart the application, or press Ctrl+R in the profile manager
  (`papo-tui --profiles`), which reads the file again.
- **An account setting was not saved:** the message under the options says
  whether the server refused it or the result is unknown. Nothing is repeated
  automatically; reopen the window to see the server's current values before
  trying again.

## Links

- **Falha ao abrir o navegador.** (Could not open the browser) after choosing
  **Abrir no navegador**: on Linux the program found no `xdg-open` to hand the
  link to. Install xdg-utils (in Termux, termux-tools provides it), then choose
  the option again. If the footer says **Link aberto no navegador.** and no
  window appears, the link was handed over: check the default browser set in
  your desktop, or run `xdg-open https://example.com` in a terminal to see what
  it reports.
- **Ctrl+click opens only part of a long link:** your terminal found the
  address by itself, one row at a time, instead of using the program's
  hyperlink. Check that your terminal supports hyperlinks and has them enabled
  (in kitty, `allow_hyperlinks`; with tmux, a version and terminal that pass
  them on), or use **Abrir no navegador** from the message's context menu when
  the message is only the link. See
  [Open links from the terminal](formatting-and-links.md#open-links-from-the-terminal).

## Images, video and sound

- **Images do not appear:** check the application mode in Ctrl+S. Ultralight shows
  attachment descriptions only. Normal falls back to Chafa when the terminal
  has no supported graphics protocol; Light always uses Chafa.
- **Emoji pictures are missing:** install the Noto Color Emoji font and restart
  the application.
- **Video is still loading:** the player status line shows the transfer
  progress. Videos prepared for streaming start before the download ends;
  files whose index is at the end start only when it finishes. If progress
  stops, check the connection. Press Esc to cancel; if an error appears,
  press Enter in the media window to retry.
- **Video plays without sound:** press `m` to unmute, raise the volume in the
  player and check the system's audio output. If no audio device is available,
  playback can continue without sound.
- **Audio plays without sound:** the same checks apply; the status line shows
  **Sem dispositivo de áudio** (No audio device) when the system offers no
  output. If the player shows **Áudio inválido ou codec indisponível.** (Invalid
  audio or unavailable codec), the file is damaged or its format is not supported.
- **Ctrl+X does not reopen the audio:** the message may have been deleted or be
  out of reach; the footer shows **Mensagem do áudio não está disponível.**
  (The audio's message is not available). The mini-player keeps working.
- **Missing media helper or module:** reinstall the complete package. For the
  ZIP, keep the DLLs and the `papo-backends` folder alongside the program.
- **No notification or call sounds:** check **Sons** in Ctrl+S → Notificações
  for the server's account, **Sons neste aparelho** in Ctrl+D → Preferências,
  that the call output is not silenced next to ⚙, and the system's audio
  output. Sounds need the `papo-backends` folder of the package.

## Updates

- **The update check failed:** **Atualizações** in Settings says why.
  **Não foi possível consultar o GitHub. Verifique a conexão.** (Could not reach
  GitHub) means `api.github.com` did not answer: check the connection and any
  proxy, then select **Verificar agora** (Check now) after a minute.
  **O GitHub limitou as consultas deste endereço.** (GitHub limited the requests
  from this address) appears when many requests left the same network address
  within the hour; the program waits at least an hour before asking again and
  your conversations are not slowed. **O certificado do GitHub não é
  confiável** (GitHub's certificate is not trusted) usually means
  `CURL_CA_BUNDLE` names a file without the public certificate authorities;
  nothing is asked over an untrusted connection. A failed check is tried again
  twice within a few seconds and then on the next day. None of this affects
  the chat servers.
- **The update download failed:** **Atualizações** says why and offers
  **Baixar atualização** (Download update) again. **O GitHub não entregou o
  arquivo.** (GitHub did not deliver the file) means the answer was not the
  file, or sent the download somewhere other than GitHub: try later. **O
  arquivo baixado não confere com o publicado** means its size or SHA-256 was
  not the published one; it was removed and nothing was saved. **Não foi
  possível gravar o arquivo temporário.** (Could not write the temporary file)
  points to a full or read-only temporary folder; set `TMPDIR` to one with
  room. **Não foi possível gravar em …** names the place you chose: pick a
  folder you can write to.
- **The update's signature was refused:** see
  [How a download is trusted](updates.md#how-a-download-is-trusted). Nothing is
  downloaded from a release the project's key did not sign; trying again does
  not change that. If the release page looks right, tell the project.
- **Baixar e instalar could not finish:** **O arquivo baixado não contém a
  versão esperada do programa e foi descartado.** (The downloaded file does not
  hold the expected version of the program) means the portable archive was not
  the tree of that version; nothing changed. **Não foi possível gravar o
  arquivo temporário.** means the folder that holds your AppImage or program
  folder has no room or cannot be written. **Não foi possível instalar a versão
  X; esta continua no lugar.** (Could not install version X; this one stays in
  place), printed after the program closed, is followed by the path of the new
  version: replace the old file or folder with it by hand. For a portable
  folder this happens when the folder also holds files that are not the
  program's, or when an older `<folder>.previous` that is not a copy of the
  program is in the way.
