# Server settings

Open the active server's tab menu (right-click its tab, or select it and press
**m** or Ctrl+Space) and choose **Configurações do servidor** (Server
settings). The panel shows one tab per category. Tabs your account cannot use
are hidden:

| Tab | Shown when your account |
|---|---|
| **Visão geral** (Overview) | owns the server or has **Gerenciar servidor** (Manage server) |
| **Cargos** (Roles) | has **Gerenciar cargos** (Manage roles) |
| **Canais** (Channels) | has **Gerenciar canais** (Manage channels) |
| **Emojis** | always |
| **Webhooks** | owns the server or has **Gerenciar webhooks** (Manage webhooks) |
| **Membros** (Members) | has **Gerenciar cargos** or **Gerenciar servidor** |
| **Auditoria** (Audit log) | owns the server or has **Gerenciar servidor** |

The server owner sees every tab. While the client is still confirming your
permissions, the bottom of the panel shows **Atualizando permissões…**
(Updating permissions) and tabs that depend on them appear once confirmed. If
you lose a permission while the panel is open, its tab closes, unsaved changes
there are discarded and **Permissão removida; a aba foi fechada.** (Permission
removed; the tab was closed) appears.

## Move around the panel

Use ←/→ on the tab row, or click a tab, to switch categories. Tab and
Shift+Tab move between the tabs, the list on the left, the form on the right
and **Fechar** (Close). In the list, ↑/↓, PageUp/PageDown and Home/End choose an
item and Enter opens it in the form. In the form, ↑/↓ move between fields,
Enter in a text field saves, Space or Enter toggles a box or presses a button,
and ←/→ change an option shown as **< … >**. Buttons that delete, revoke, remove an avatar or ban ask
for a second press: the first one changes the label to **Confirmar: …**
(Confirm). Esc closes the panel without saving the open form.

## Tabs

- **Visão geral**: rename the server (up to 32 characters), change or remove
  its icon, and make it public or private. **Alterar ícone…** (Change icon)
  opens the image picker; images larger than 512 pixels are reduced and large
  animations are refused. Making a public server private asks for the server
  password people will use to join; on a private server, **Nova senha** (New
  password) is optional and left empty keeps the current one. The password is
  masked and is not stored by the application. **Salvar alterações** (Save
  changes) sends only what you changed. The owner, creation date and counts are
  shown for reference.
- **Cargos**: choose **+ Novo cargo** (New role) or a role. Edit its name, its
  color as `#RRGGBB` and its eight permissions, including **Gerenciar webhooks**, then press **Criar cargo**
  (Create role) or **Salvar cargo** (Save role). Leaving the color empty sends
  no color. **Excluir cargo** (Delete role) removes the role from the server.
- **Canais**: choose **+ Novo canal** (New channel) to create a text, voice or
  category channel, or choose a channel to rename it, change its topic (up to
  512 characters; categories have none), move it up or down, or delete it.
  Under **Permissões por cargo** (Permissions per role), each box is saved as
  soon as you change it. **Novo vínculo** (New link) and **Vincular cargo**
  (Link role) add a role to the channel, starting with reading and sending
  allowed; **Remover vínculo** (Remove link) takes the role off the channel
  without deleting it.
- **Emojis**: choose an emoji to see its picture, format, date and author on the
  right; Ultralight shows the details without the picture. Choose
  **+ Adicionar emoji** (Add emoji), type a name and press
  **Escolher imagem e enviar…** (Choose image and send) to pick a GIF, JPEG,
  PNG or WEBP file. You can delete emojis you added; deleting other people's
  emojis needs **Gerenciar servidor**. **Carregar mais…** (Load more) at the
  end of the list fetches the next page.
- **Webhooks**: a webhook lets another program post messages in one channel. The
  list shows each webhook by its nickname (or name) and its channel. Choose
  **+ Novo webhook** (New webhook), pick a text or voice channel with ←/→ on
  **Canal** (Channel), type a name of up to 80 characters and press **Criar
  webhook** (Create webhook). The new webhook opens with **Token de acesso**
  (Access token), its **URL de envio** (sending address) and **Copiar URL** and
  **Copiar token** (Copy URL, Copy token): the token is shown **only now**. It is
  kept in memory while this form stays open and is dropped when you choose
  another webhook, close the panel or reopen it; to get a new one, revoke the
  webhook and create another. Anyone with the token can post in that channel, so
  keep it private. In an existing webhook, **Apelido** (Nickname, up to 100
  characters, empty to use the name) with **Salvar apelido** (Save nickname)
  changes the name shown on its messages, **Escolher avatar e enviar…** (Choose
  avatar and send) picks a GIF, JPEG, PNG or WEBP image, and **Remover avatar**
  (Remove avatar) takes it off; both ask the server, which refuses avatars above
  2 MB or 512 pixels, and the new picture can take a while to replace the one
  already loaded in the history. **Revogar webhook** (Revoke webhook) ends the
  token's access; the messages it already posted stay. The **Auditoria** filter
  **Entidade** also lists **Webhooks**.
- **Membros**: type while the list is focused to filter by nickname or
  @username; Backspace erases the filter. With **Gerenciar cargos**, the boxes
  under **Cargos** assign or remove each role at once. With **Gerenciar
  servidor**, **Banir** (Ban) ends the member's sessions and blocks new
  sign-ins, **Desbanir** (Unban) allows them again, and **Gerar link de
  redefinição de senha** (Generate password reset link) shows a single-use
  link and its expiry; a new link replaces earlier ones. You cannot ban
  yourself or the owner.
- **Auditoria**: lists recorded events, newest first. Choose an event to see
  who did it, when, the member affected and its details. **Carregar mais…**
  fetches older events. The first row, **▾ Filtros** (Filters), opens the
  filters and shows how many are in use:
  - **Entidade** (Entity): one kind of item, such as **Cargos** or
    **Usuários** (Users).
  - **Ação** (Action): one action name, such as `role.create`. The options are
    the actions seen in the events loaded since the panel opened.
  - **Feito por** (Done by): type part of a nickname or @username and choose
    the person below the field; Enter picks the only match. **Qualquer
    pessoa** (Anyone) removes this filter.
  - **De** (From) and **Até** (Until): dates as DD/MM/YYYY in this computer's
    time zone; both include the whole day.

  **Aplicar filtros** (Apply filters) reloads the log; **Limpar filtros**
  (Clear filters), on the same row, shows every event again. From an event, **Só a ação …**
  (Only action …) and **Só as ações de …** (Only actions by …) narrow the
  filters in use to that event's action or author. The server cannot filter
  by the member affected.

## Results of a change

The result of each change appears at the bottom of the panel. **Aguarde a
alteração anterior terminar** (Wait for the previous change to finish) means
another change is still being sent. A change refused by the server, such as
**Já existe um cargo com esse nome** (A role with that name already exists),
keeps your text so you can correct it. **Alteração não confirmada; confira
antes de repetir** (Change not confirmed; check before repeating) means the
connection ended before the server answered: the panel reloads what the server
kept, and the change is never sent again automatically. The server does not
announce changes made by other administrators to roles, emojis, webhooks,
channel permissions or the server itself. They appear when you return to that tab or
reopen the panel; roles and channels also refresh about every 30 seconds.
