# Accessibility

Press **Ctrl+S** and go to **Acessibilidade** (Accessibility). Its choices
belong to this device: they apply to every server and are saved in
`config.json`, never on a server.

- **Contraste** (Contrast): **Padrão** (Default), **Alto** (High) or **Sem
  cores** (No colors). High keeps the account's dark or light theme with a pure
  black or white background, brighter text and secondary text, stronger
  separators and a focus fill that stands apart from the background. No colors
  draws the interface in the terminal's own foreground and background and shows
  the focused item in reverse video; images, avatars and videos keep their
  colors. When the `NO_COLOR` environment variable is set and not empty, the
  application starts without colors for that run, whatever this choice says.
- **Cores para daltonismo** (Color-blind safe colors): errors and the unread
  dot turn orange, and success, online presence, the speaking member and the
  microphone level bars turn blue, instead of red and green.
- **Indicadores além da cor** (Cues beyond color): the focused item of lists,
  menus, tabs and buttons is also bold and underlined, so focus never depends on
  color alone.
- **Cores dos cargos** (Role colors): **Originais** (Original),
  **Contraste ajustado** (Adjusted contrast), which lightens or darkens each
  role color until it reads at 4.5:1 on the background behind it, or
  **Desativadas** (Off), which shows every name in the text color. Presence and
  speaking colors keep their meaning in every option.
- **Modo leitor de tela** (Screen reader mode): whenever no text field has
  focus, the terminal cursor rests on the focused item: the server tab, the
  channel or DM, the selected message, the menu item, the settings row, the tab
  or the button. Screen readers that follow the terminal cursor, such as Orca or
  NVDA, then read it. The loading animation stands still and interface icons are
  shown as text. The server tabs and the **Canais**/**DMs** tabs are written
  between brackets again, such as **[Name]**, **[+ Adicionar]**, **[Canais]**
  and **[DMs]**, instead of being marked only by their background, which a
  screen reader does not read.
- **Símbolos da interface** (Interface symbols): **Ícones** (Icons) or
  **Texto** (Text). Text replaces icons such as 🔍, 🖈, 👤, 📎, 😄, ⚙ and 🎤 with
  labels such as **[/]**, **[Fix]**, **[@]**, **[+]**, **[:)]**, **[*]** and
  **[Mic]**, as on the [Linux console](linux-console.md).
- **Emojis nas mensagens** (Emoji in messages): **Imagens** (Images) or
  **Nomes** (Names). Names shows each emoji, in messages and in the message box,
  as its name between colons, such as **:grinning:** for 😀, and the server's own
  emoji as **:name:**, instead of pictures.
- **Bordas** (Borders): **Linhas** (Lines) or **ASCII**. ASCII draws boxes and
  lines with +, - and |.
- **Alerta visual** (Visual alert): **Desligado** (Off), **Destaque**
  (Highlight) or **Piscar** (Flash). Highlight shows each new notification in the
  footer, such as **Nova mensagem de Ana · Comunidade** (New message from Ana ·
  Comunidade) or **Nova menção de …** (New mention from …), even when sounds and
  system notifications are off. It follows the same rules as the sounds: channels
  set to none or muted and the conversation you are reading do not alert. Flash
  also flashes the screen once, at most every three seconds.
- **Duração dos avisos** (Notice duration): **2 segundos** (2 seconds),
  **6 segundos** (6 seconds) or **Até uma tecla** (Until a key), for the notices
  that go away by themselves: the visual alert and the message that confirms a
  sent message.
- **Mouse**: **Ativado** (On) or **Desativado** (Off). Off leaves the mouse to
  the terminal, so you can select and copy text as in any other program; every
  control stays reachable from the keyboard.
- **Confirmar ações que encerram** (Confirm actions that end things): quitting
  (Ctrl+Q, or Ctrl+C outside a field), signing out (Ctrl+L) and closing a DM
  (Ctrl+W) show **Ctrl+Q de novo sai** (Ctrl+Q again quits) or a similar notice
  and wait for the same key again, with no time limit. Any other key cancels and
  does nothing else.

Use ↑/↓ to select an item and ←/→, Space or Enter to change it, or click it;
the selected item explains itself below the list. **Salvar e aplicar** (Save
and apply) applies the choices at once. The font size is the terminal's: change
it in the terminal's own settings, usually with Ctrl and + or −. History
density stays in [Theme and history display](settings.md#theme-and-history-display).
