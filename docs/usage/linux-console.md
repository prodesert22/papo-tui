# The Linux console (TTY)

The application runs on the Linux virtual console (`TERM=linux`), in Light or
Ultralight mode. The console's fonts have no emoji and only a few symbols, so
the interface uses symbols those fonts can draw:

| Button or mark | Elsewhere | On the console |
| --- | --- | --- |
| Search, pinned messages, members | 🔍 📌 👤 | **[/]** **[Fix]** (**[Pin]** in English) **[@]** |
| Attach files, emoji picker | 📎 😄 | **[+]** **[:)]** |
| Settings | **[⚙]** | **[*]** |
| Call audio, muted | 🔊 🔇 | **[◀)]** **[◀x]** |
| Microphone, muted | 🎤 | **[Mic]** **[M x]** |
| Voice channel | **♪ voz** | **◀ voz** |
| Pause, close | ⏸ ✕ | **\|** **x** |
| Loading | turning dots | turning bar `\| / - \` |

Emojis in messages, reactions and the editor appear as their names between
colons, such as **:joy:** for 😂. The message keeps the emoji itself: what you
send is unchanged. The emoji picker (Ctrl+E or **[:)]**) lists emojis by name,
one per row; search and choose them as usual.

The focused button, tab or row has an orange fill with black text. Alt+← and
Alt+→ switch virtual consoles there, so they never reach the application: move
the focus with Tab and Shift+Tab, as the footer shows (**Tab / Shift+Tab: mudar
foco**, Tab / Shift+Tab: move focus).
