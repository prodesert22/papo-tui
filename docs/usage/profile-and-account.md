# Your profile and password

## Edit your own profile

Your profile belongs to each server separately: changes apply to the server tab
that is open. Press **Ctrl+S**: settings open on **Perfil** (Profile), the
first tab. The tab shows **Seu perfil em** (Your profile on)
and the server name, then:

- **Apelido** (Nickname), up to 32 characters. Leave it empty to show only your
  @username.
- **Frase de status** (Status phrase), up to 64 characters.
- **Descrição** (Description), up to 512 characters. Shift+Enter starts a new
  line.
- **Frase de digitação** (Typing phrase), up to 64 characters, shown to others
  while you type.
- **Status**: ←/→ or Enter chooses **Online**, **Ausente** (Away) or
  **Ocupado** (Busy).
- **Avatar** and **Banner**: **Alterar…** (Change…) opens the file choice;
  **Remover** (Remove) deletes the banner.

Above the fields, a preview shows your saved banner, avatar, name, @username and
status as they appear in **Ver perfil** (View profile); the banner fills its
whole band, which has the shape of a Discord banner (17:6, 680x240 pixels), so a
banner of another shape is cropped at the center; **sem banner** (no
banner) shows while you have none, and your initial stands in for an avatar that
has not loaded. It follows the server, so a new image replaces it as soon as the
server accepts it, and text you have not saved is not shown in it. The preview
needs a terminal of at least 28 rows, where the window grows from 20 to up to 37
rows (the taller the terminal, the less the banner is cropped), and is not shown in Ultralight mode; on shorter terminals the window keeps
its usual size and only the fields are shown. In Light mode it is drawn with colored
characters.

Each text field shows how many characters it uses. Use ↑/↓ to move between the
fields. Enter in a text field, or **Salvar perfil** (Save profile), saves the
text fields and the status together; **Fechar** (Close) or Esc closes the window,
and unsaved text is discarded. The footer of the window shows **Salvando…**
(Saving), then **Perfil salvo** (Profile saved) or the server's reason when it
refuses a change. An image is sent as soon as you choose it.

## Profile commands

You can also type these commands in the message box and press Enter. They
change the profile on the current server and are never sent as messages.
Typing `/` at the start of the message box opens **Comandos** (Commands) above
it, listing each command and what it does; keep typing to narrow the list,
use ↑/↓ to choose and Tab or Enter to complete the name. Once a command is
typed, the box above shows its description and **Opções** (Options), and the
message box shows **+1 opção** (+1 option) until you type the option. For
`/status` and `/banner`, the accepted values are listed and narrow as you
type; Tab completes the chosen one. Esc hides the box and keeps your text.
Enter on a bare command name that takes an option only completes it, so
`/nick` alone removes the nickname after a second Enter.

| Command | Effect |
| --- | --- |
| `/nick name` | Changes your nickname; `/nick` alone removes it. |
| `/status online`, `/status ausente`, `/status ocupado` | Changes your status (`away` and `busy` also work). |
| `/bio text` | Changes your description; `/bio` alone clears it. |
| `/avatar` | Opens the file choice for your avatar. |
| `/banner` | Opens the file choice for your banner; `/banner remover` removes it. |

The footer shows the result. A command with a wrong argument, such as
`/status talvez`, shows how to use it and keeps the text in the box. Other text
that starts with `/` is sent as an ordinary message; start it with `//` to send
a message beginning with `/`, for example `//nick` sends `/nick`.

## Avatar and banner images

Avatars and banners can be PNG, JPEG, GIF or WEBP files. The server accepts up
to 2 MB, 512 pixels per side for an avatar and 2048 pixels for a banner. A
larger still image is reduced to fit and sent as PNG; a larger animated GIF,
WEBP or PNG is refused, so reduce the file first. Ultralight has no image
processing, so there only files already within the limits are accepted.

A change that the server refuses or does not confirm is never repeated
automatically. When a change is not confirmed (**Alteração não confirmada**),
the profile is loaded again so you can see what the server kept before trying
again.

## Change your password

Your password belongs to your account on each server. To change it on the
server tab that is open, press **Ctrl+S** and choose **Conta** (Account), the
tab before **Sobre**; pressing ← twice from **Perfil** reaches it. The tab shows
**Sua conta em** (Your account on) with the server name and your @username,
then **Nova senha** (New password) and **Confirmar senha** (Confirm password).
Both fields hide what you type. The password needs 8 or more characters, an
uppercase letter and a symbol.

Press Enter or select **Alterar senha** (Change password). The first press only
checks the fields and asks for confirmation: the button becomes **Confirmar**
(Confirm), because the server ends every session of the account, on every
device, including this one. Select **Confirmar** to send the change. Editing a
field or closing the window cancels the confirmation.

When the server accepts it, the application returns to the sign-in screen with
**Senha alterada** (Password changed). Sign in again with the new password; the
saved session of that server is deleted. If the server refuses the password, the
reason appears at the bottom of the tab and you stay signed in. A change that is
not confirmed (**Troca de senha não confirmada**) may still have taken effect: if
the session ends shortly after, sign in with the new password. A change is never
repeated automatically.
