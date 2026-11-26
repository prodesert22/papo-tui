# Servers and connection

## Server tabs

When the system credential store is available, the application restores the
session of every server you were signed in to. Each server opens as a tab in
the top bar. The server you used last is selected, on the channel or direct
message you had open there; each other tab also returns to its own last
conversation. Without that record, for example on the first start after an
update, the most recently added server is selected. Use the **+ Adicionar** (Add)
tab at the right end of that bar to sign in to another server.

The selected server is the tab with a highlighted background. A server name too
long for its tab is not cut with an ellipsis: its last characters fade into the
tab. Terminals
with fewer than 256 colors, such as the Linux console, cannot show the
background, so the selected server is written as **[Name]** and the add tab as
**[+ Adicionar]**. The
[screen reader mode](accessibility.md) writes them the same way on any terminal.

The sign-in screen keeps the server tabs at the top while another server is
open, for example when the selected server is unreachable at startup or you are
adding a server. Click a tab to open that server, or press ↑ from the first field
(or Shift+Tab), choose a tab with ←/→ and press Enter.

## Offline servers and reconnection

A server that cannot be reached keeps its tab, marked **(offline)**, and its
saved session. The application keeps trying it in the background, a few
seconds apart at first and then every 30 seconds, and the tab becomes available
again by itself once the server answers; you do not need your password. Opening
the tab tries it again right away.

If the server you are using stops answering, its tab is marked **(offline)** and
the history shows **Servidor offline** (Server offline) with **Reconectando
automaticamente…** (Reconnecting automatically) instead of loading or
permission errors. Your session is kept, and the conversation comes back when
the server answers again.

When the connection drops, the footer shows **Desconectado · F5: reconectar**
(Disconnected · F5: reconnect), or **Servidor offline · F5: reconectar** when
the server stops answering. The application keeps retrying by itself, waiting
longer after each failure, up to 30 seconds. Press **F5** to try again right
away instead of waiting: your session is checked first, then the realtime
connection opens again. Messages already sent are never sent again.

## Remove a server

To remove a server from the list, right-click its tab, or select the tab and
press **m** or Ctrl+Space, to open the tab's menu below it. Choose **Remover
servidor** (Remove server), then confirm **Remover** (Remove). On the server
you are using, the same menu also offers **Configurações do servidor** (Server
settings); see [Server settings](server-settings.md). Esc or a click
outside closes the menu. Pressing Delete on a selected tab goes straight to the
confirmation. Removing a server closes the tab and deletes
the session saved in the system credential store; the server does not come back
when you reopen the application. It does not sign you out on the server; use
**Sair da conta** (Sign out) first if you also want to end that session there.
