# Getting started

## Run the application

After [installing](../installation.md), open a UTF-8 terminal with at least
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

`papo-tui --help` describes the command-line options, the environment
variables the application reads and where it keeps its files, in the system
language or the one set with `PAPO_LANGUAGE`; `papo-tui --version` prints the
version. In a terminal the help uses colors for its headings, options and
links; it is plain text when redirected to a file or another program, when
`NO_COLOR` is set or when `TERM` is `dumb`. `NO_COLOR` also starts the
interface without colors (see [Accessibility](accessibility.md)). Keyboard controls are listed in
the in-app help and in [Keyboard and mouse](keyboard-and-mouse.md).

## Sign in

Enter the instance URL, such as `chat.example.com`, your username and your
password. An address without `https://` or `http://` uses HTTPS. Enter a
server password only for private instances.
Select **Entrar** (Sign in). To create an account, select **Cadastro** (Register)
and then sign in. Use **Tab** to move between fields, **Enter** to confirm and
**Ctrl+Q** to quit.

After signing in, [Servers and connection](servers.md) explains the server tabs
and [Conversations](conversations.md) how to read and send messages.

## Reset your password with a link

If you cannot sign in, ask the server administration for a password-reset link.
It looks like `https://…/passwordchange/…`, works once and expires after a
while. On the sign-in screen, enter the server URL and select **[Redefinir
senha]** (Reset password) below the buttons, or reach it with Tab or ↓. The form
changes to **Link de redefinição** (Reset link), **Nova senha** (New password)
and **Confirmar nova senha** (Confirm new password). Paste the whole link, or
only its last part, type the new password twice and select **[Redefinir]**
(Reset). The password needs 8 or more characters, an uppercase letter and a
symbol; the server may require more and says so when it refuses one.

If the link names another address than the server URL in the form, the footer
shows both and nothing is sent. Check the URL, or select **[Redefinir]** again to
send the link to the server in the form anyway: the link only works on the
server that created it.

**Senha redefinida** (Password reset) means the new password is in effect: the
form returns to signing in, with the username you had typed. Sessions of that
account on other devices are ended. **Link inválido, expirado ou já usado**
(Invalid, expired or used link) means the link no longer works; ask for a new
one. If the reset is not confirmed (**Redefinição não confirmada**), try signing
in with the new password before asking for another link. **[Login]** returns to
the sign-in form without sending anything; the link is not kept.

## Saved server profiles

**Selecionar perfil** (Select profile) lets you save server names and URLs.
To open the profile manager directly, run `papo-tui --profiles`.
Passwords are not stored in the configuration.
