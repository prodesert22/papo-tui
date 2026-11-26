# Permissions and access

The channel list hides channels you cannot read. Message menus show only the
actions your account can perform; keyboard shortcuts follow the same rules.
In a read-only channel, you can keep a draft, edit or delete your own messages,
view reactions and remove your own reaction. Sending, replying and adding a
reaction require permission to send messages. Administrative roles do not let
you edit another person's messages. Deleting another person's message requires
`delete_messages` in that channel; this does not apply to other people's DMs.

Assigned or removed roles take effect as soon as their WebSocket event arrives,
including while a menu or confirmation is open. Other permission changes are
checked every 30 seconds in the active server tab. Ctrl+R also refreshes
permissions. These checks can take longer while the connection is unavailable
or the server limits requests. Permissions that cannot be confirmed remain
unavailable; the client shows a loading or retry notice.

If you lose access to an open channel, its history closes and your draft stays
with that conversation. Once access returns, select the channel to resume it.
An action still waiting to be sent is canceled if its permission is revoked;
an action already transmitted can still finish. Failed mutations require an
explicit retry. Roles, channels and the server itself are managed in
[Server settings](server-settings.md).
