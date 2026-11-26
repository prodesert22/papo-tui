# Unread messages and mentions

## Channels

A channel with new messages shows its name in bold and a small red dot at the
right of its row. A red badge with a count takes the dot's place when the
channel has unread mentions of you or `@everyone`. The **Canais** and
**DMs** tabs show the same dot, after their name, when one of their
conversations has new messages.

## Direct messages

The **DMs** list shows only each person's name. A conversation with new
messages shows the red badge with how many messages are unread;
nothing appears beside a read conversation. The server counts unread messages
when it lists your DMs; messages arriving afterwards add to that count until
you read the conversation, and a dot appears while the count is not known.

## Server tabs

Red dots beside server names indicate unread messages in the channels and DMs
listed for that server, or unread notifications, and the count shows
known unread mentions of you or `@everyone`. In graphics terminals, the
badge is centered vertically in the row, with a white count inside the red
circle; larger counts use a wider rounded badge. The number is centered inside
the badge. Text channel indicators use circled digits or parentheses.
Server badges sit inside the tab, separated from the name by one
space, for example `Server ❶`. With text rendering (Chafa, Light or Ultralight),
the badge is a plain red mention counter, such as `Server 1`; zero indicates
unread activity without a confirmed mention. On terminals with fewer than 256
colors, and with the screen reader mode, the badge stays inside the brackets of
the selected server, as in `[Server 1]`.
Counts cover loaded previews, which can be truncated, rather than a server-wide
total. Opening a notification clears its mention badge after confirmation.
The server dot also covers notifications with missing mention metadata.

## When a conversation counts as read

A conversation counts as read when its latest message is visible at the bottom
of the history while the terminal has focus; only then is the read sent to the
server. In a terminal that does not report focus, a message counts as seen
when it appears within two seconds of your last key press or click. Opening a conversation or scrolling through older messages does not mark
it read. Notifications for messages up to the point where you read a
conversation, in this client or another one, are marked read with it and leave
the notification count. Delayed copies of already-read messages do not restore the dot.
The unread-message dot remains until the conversation is read. Your own messages
never light it, and deleting the last message you read does not light it again.

## The New messages rule

When you open a conversation with new messages, a red **Novas mensagens** (New
messages) rule appears above the first message someone else sent after the
point where you stopped reading, here or on another device. It stays where it
is while the conversation remains open, even after the conversation is read, so
you can scroll back to it; messages that arrive while you are in the
conversation do not add one. The next time you open the conversation it shows
only what is new by then. The rule appears only when the messages on both sides
of it are already loaded; the client never loads extra history to place it.

## Terminal window title

In terminals that support window titles, the title shows **channel | server**
(or **DM com name | server** for a direct message). A **●** at the start, as in
**● geral | Comunidade**, means one of the connected servers has news: it
appears whenever a server tab shows its red dot, for unread messages or
notifications in channels and DMs that are not muted. A muted channel does not
light it, even when new messages arrive there. The dot disappears once you read
that news or mute those conversations. A count such as **(3)**
shows the total known unread mentions across connected instances, including
`@everyone`; ordinary unread messages do not add to it. The count disappears
when it reaches zero. Switching conversations updates the title, and returning
to login clears the conversation name. Long names are shortened to fit.
A terminal setting that forces a fixed tab title may override this display.
