# Customize shortcuts

Press **Ctrl+S** and go to **Atalhos** (Shortcuts) for every key of the
application. Commands are grouped by area: **Aplicativo** (Application),
**Conversas** (Conversations), **Listas e seleção** (Lists and selection),
**Membros** (Members), **Mídia e player** (Media and player), **Arquivos**
(Files) and **Login e perfis** (Sign-in and profiles). Below them come
**Pressionar para falar** (Push-to-talk), when the build supports voice, and
**Teclas fixas** (Fixed keys): the arrows, Enter, Esc, Tab, Space, Delete and
the text editing keys. Fixed keys are listed for reference and never change, so
a new key can never lock you out of a screen.

Type in **Buscar** (Search) to filter by action, key or area; ↓ moves to the
list, and typing in the list goes back to the search. Press Enter on a command,
or click it, to open its menu:

- **Definir tecla** (Set key): press the new combination; it replaces the
  command's keys.
- **Adicionar outra tecla** (Add another key): keep the current keys and add
  one. A command has up to three keys.
- **Remover as teclas** (Remove the keys): leave the command without a key.
- **Restaurar o padrão** (Restore the default).

While **Pressione a nova tecla** (Press the new key) is shown, press the
combination; a lone Ctrl, Alt or Shift is ignored and Esc cancels. A refused key
keeps the capture waiting and shows why below the list:

- the fixed keys, Ctrl+H, Ctrl+I, Ctrl+M and Ctrl+[ (the terminal sends them as
  Backspace, Tab, Enter and Esc), Ctrl+A, Ctrl+C and Ctrl+V;
- Ctrl+U, Ctrl+W and Ctrl+N for commands of the areas where they edit text;
- a key without Ctrl or Alt for commands that also act while you type, such as
  **Ir para um canal ou DM** (Go to a channel or DM, Ctrl+K), since it would be
  typed into the message box. Commands of lists, message selection and the
  player, such as **r** (reply) or **m** (mute), may use a single key;
- mouse buttons, which only push-to-talk uses.

When the key already belongs to another command that listens in the same area,
the window shows **… já é usada por "…"** (… is already used by "…"): Enter
moves the key to the command you are editing, leaving the other one without it,
and Esc keeps both as they were. **Sair do aplicativo** (Quit), the help and
**Configurações** (Settings) always keep at least one key. Commands that never
listen together may share a key, as Ctrl+E opens the emoji picker in a
conversation and edits a profile in the profile manager.

A changed command shows **•**. **[Restaurar todos os atalhos]** (Restore all
shortcuts), at the end of the unfiltered list, asks for a second Enter and
returns every command to its default keys. **Salvar e aplicar** (Save and apply)
applies the new keys at once: the help dialogs, the footer hint and notices that
name a key, such as **Ctrl+R: tentar novamente** (Ctrl+R: try again), show the
key in effect. **Cancelar** (Cancel) or Esc discards the changes.

Shortcuts belong to this device: they are saved in its `config.json` and are
never sent to a server. Terminals that do not report modified keys cannot tell
every combination apart. There, Ctrl+Shift+letter arrives as Ctrl+letter, and
Ctrl+number or function keys with Ctrl, Alt or Shift may not arrive at all; use
Ctrl+letter, Alt+letter or a plain function key, or a terminal with the
extended keyboard protocol, such as Kitty, foot, WezTerm or Ghostty.
