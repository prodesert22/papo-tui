# Pinned messages and server search

## Pinned messages

Click **Fixadas** (Pinned) in the conversation header, or press **Ctrl+B** outside
of the media player, to list that conversation's pinned messages. Type in
**Buscar** (Search) to filter their text, author name or username. Filtering
is local and ignores letter case. Use arrow keys, PageUp/PageDown, Home/End
or the mouse wheel to browse the list. **Ctrl+R** refreshes it.

The message context menu offers **Fixar mensagem** (Pin message) or
**Desafixar mensagem** (Unpin message), once its pin state has loaded.
If it cannot load, keep the menu open and use Ctrl+R to retry.
Pinning and unpinning require `pin_message` and permission to read the
conversation. Viewing the list requires read access without pin permission.
The server remains authoritative; a conversation can have at most 100 pins.
Pinning and unpinning are not repeated automatically after
an error or an uncertain response. Check the refreshed list before retrying.

## Search messages on the server

Click **Buscar** (Search) in the conversation header, or press **Ctrl+F**, to
search messages on the current instance. Type message content and press
**Enter**, or click **Buscar**, to query the server. Typing alone does not
send requests. The initial scope is the whole server, with newest results
first. Results and text matching follow the server's search behavior.
The whole-server endpoint may omit DMs; select a specific DM to search it.

From the search text field, press Down to reach **Buscar** and **Filtros**.
Left/Right selects between these buttons; Enter activates the selected button.
Down again reaches the results. Up from the first result returns to the buttons,
and Up again returns to the text field. Tab/Shift+Tab also moves between areas.

## Search filters

**Filtros** (Filters) opens a separate screen in the same panel:

- **Autor** (Author), **Conversa** (Conversation) and **Menciona** (Mentions)
  open searchable name selectors. Choose **Qualquer / limpar filtro**
  (Any / clear filter) to remove that restriction.
- **Contém link** (Contains a link) requires a URL in the message content.
- **Data inicial / Data final** (Start / end date) accept `YYYY-MM-DD` and
  include both boundary dates.
- **Anexos** (Attachments) cycles through **Qualquer**, **Com anexos** and
  **Sem anexos** (Any, With attachments and Without attachments).
- **Ordem** (Order) switches between newest and oldest messages first.

Use Tab/Shift+Tab or up/down to reach each filter; the panel scrolls on small
screens. **Aplicar** (Apply), **Cancelar** (Cancel) and **Limpar filtros**
(Clear filters) share one row: ←/→ move between them. **Aplicar** saves the
filters and returns to results without sending a request. **Cancelar** or Esc
discards filter edits. **Limpar filtros** clears the filter draft.

Below them, **Filtros recentes** (Recent filters) lists the filters of your last
five searches on this server, newest first, such as
`Autor: Bia · # geral · Contém link`. Enter or a click fills the filters with
one of them, ready for **Aplicar**. The search text is not part of them. The
list lasts while the application is open and is cleared when you sign out.
Execute **Buscar**
to apply your text and filters to a new server query. Text may be empty when
at least one filter is set; ordering alone is not a search criterion.

## More results

Use **Carregar mais** (Load more) for the next page. Loaded results belong to
the last submitted query; **Alterações pendentes** (Pending changes) means
you must execute Buscar before loading more with edited criteria. Ctrl+R
repeats the current text and filters. A local limit of 1,000 results or 4 MiB
shows a partial-list notice; refine the query when that happens.

## Open a result

In either list, Enter or a click opens message details. **Ir para mensagem**
(Go to message) opens its conversation, locates older messages when needed,
and highlights the target using the same navigation as
[notifications](notifications.md#open-a-notification). The
pinned-message detail also has **Desafixar** (Unpin); the initial selection
is Go to message. Use Left/Right or Tab to select an action, then Enter to
activate it. Up/Down scrolls the preview. Other message actions are available in
the conversation's context menu. Esc returns to the list with its query and
position preserved. Go to message uses the same animated **Localizando mensagem…**
view as notifications, keeping the modal open until the destination is ready.
Esc cancels and returns to details; errors remain in the modal for explicit retry.
Searching or viewing these previews does not open inactive histories or mark
notifications read. Opening the actual conversation has its normal read behavior.
