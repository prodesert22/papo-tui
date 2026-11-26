# Members and profiles

## Conversation header

The conversation header has **🔍** (Search), **📌** (Pinned messages) and **👤**
(Members). Text rendering uses **🖈** for pinned messages. At the top of the
history, press ↑ to focus these controls, then ←/→ to move between them and
Enter to open one. ↓ returns to the history; on 👤, it enters an open member
drawer. ↑ goes to server tabs. From the channel/DM selector buttons, → after
DMs also reaches Search. Tab and Shift+Tab include all three controls.

## Member drawer

Press **Ctrl+U** or click **👤** beside the pinned-message button to open the member
drawer on the right. It reserves space beside the conversation when the terminal is wide
enough and overlays the right side on narrower screens. Closing or resizing it
preserves your draft and reading position. Use contextual help for the member controls; the footer keeps only **Ctrl+G: ajuda**
(Open help) beside operational notices.

Each member shows an avatar (or their centered first initial), display name and
presence dot: green for online, yellow for away, red for busy and gray for offline. A text label also identifies
the state. **Desconhecido** (Unknown) means presence has not been confirmed;
a saved away/busy preference alone does not establish that someone is online.
Ultralight shows text and an avatar placeholder without loading images.

**Agrupar: Cargo** (Group by role) is the default. Members appear once, under
the first role supplied by the server; this order does not imply a role
hierarchy. People without roles appear under **Sem cargo** (No role). Select
the grouping control to switch to **Agrupar: Status** (Group by status). The
choice is saved locally for future runs across instances.

Press **/** or click **Buscar** (Search) to filter by nickname or @username,
then press Enter to return to the list. Search ignores case and operates on
loaded members; loading and partial-list notices identify incomplete results.
Use Tab/Shift+Tab to move between search, grouping and results. Use ↑/↓,
Home/End or PageUp/PageDown to select people, or use the mouse. Press **Ctrl+R**
to refresh the list and **Esc** or **Ctrl+U** to close the drawer.

Reopening the drawer within a minute shows the members already loaded without
fetching them again; names, roles and status keep updating live meanwhile.
After that, or with **Ctrl+R**, the list is checked again in the background
while it stays on screen, and people no longer on the server leave it.

## Member menu

Press **Ctrl+Space** or **m** on the selected member, or **right-click a member**, to open their
context menu. It sits to the left of the drawer, or above/below the selected row
on narrow screens, keeping the member's name visible. Navigate with ↑/↓ and
Enter, or click an option:

- **Ver perfil** (View profile) opens their profile. Enter on a member also
  opens it directly. The modal shows their banner, avatar, nickname, @username,
  roles, description, presence and status message when available. Avatars and
  banners retain their full image, with padding when their shape differs from
  the available space. Use ↑/↓ or
  the mouse wheel to scroll, Ctrl+R to refresh and Esc to return.
- **Mensagem** (Message) opens or reopens a DM and focuses its editor. The
  server may refuse a DM when either participant has blocked the other.
- **Bloquear / Desbloquear** (Block / Unblock) shows only the applicable action.
  Blocking prevents DMs in both directions and does not hide public-channel
  messages. Unblocking does not automatically reopen a closed DM.
- **Banir / Desbanir** (Ban / Unban) requires the instance owner or
  `manage_server` permission and asks for confirmation. A ban applies to the
  entire instance and revokes the target's sessions. Unbanning requires them
  to sign in again.
- **Gerar link de reset de senha** (Generate password-reset link) has the same
  administrative requirement and asks for confirmation. The result shows a
  single-use link and its expiration. Choose **Copiar** (Copy) or press Enter
  to copy it. A new link invalidates previous links for that account. Closing
  the modal discards the displayed link. The member uses it with **[Redefinir
  senha]** on the sign-in screen; see
  [Reset your password with a link](getting-started.md#reset-your-password-with-a-link).

Your own entry offers only the profile. Administrative actions follow confirmed
permissions and the server's response. Menus reuse your account's existing
permission state; opening them does not request permissions again. Block state
loads when the drawer opens, and ban state comes from the member list. If a menu
opens before this finishes, it shows a loading placeholder until its options are
ready. Ctrl+R refreshes state; failures offer an explicit retry. Failed
mutations are never repeated automatically. After an uncertain result, refresh
state before deciding whether to repeat the action. Profile/banner changes made
elsewhere may require reopening the profile or pressing Ctrl+R.
