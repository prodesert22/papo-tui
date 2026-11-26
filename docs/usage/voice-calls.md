# Voice calls and audio settings

## Join a call

Select a voice channel and press **Enter**, or click it, to join its call.
Your current text conversation and draft stay open. You can keep talking while
reading other channels or switching servers. Only one call is active at a time;
joining another first leaves the previous call. **Sair da chamada** (Leave call)
below the channel list disconnects or cancels a connection in progress.
Connection or session loss ends the call; join again after reconnecting.

## Members in a call

Connected members appear below their voice channel. Names are white while idle
and green while that person speaks. Your own entry in the member list also turns
green from your local microphone level while you are transmitting, because
servers may leave the publisher out of their speaker ranking. A **🎤** marker at
the right of the name means the member's microphone is open; **🎤×** in red means
the server reports that member muted. Light, Ultralight and text terminals use
**●** and **●×** instead. No marker is shown until the server reports that
member's microphone state, so a freshly listed member may stay without one
until their first state update. Recent servers include membership in their
initial presence snapshot. Older servers may only provide the member list after
you join. A server's speaker ranking can limit how many simultaneous speakers
are highlighted.

## Call audio and microphone

The **🔊** and **🎤** buttons beside the gear toggle playback and microphone
independently. Disabled controls show a cross or a muted speaker. Light,
Ultralight and text terminals use **♪** for playback and **●** for microphone.
You can reach the buttons with Tab and activate them with Enter or Space.
While one of them is focused, ← and → move between playback, microphone and
the gear, in the order they appear on screen.
The microphone starts enabled; push-to-talk starts disabled.

## Audio settings

Open **Ctrl+S → Áudio** (Audio) to choose input/output devices, change microphone
and call volumes, or configure push-to-talk. Use Tab to focus the options,
↑/↓ to choose one and ←/→ or Enter to adjust it. Device selections follow the
audio devices reported by your system. **Padrão do sistema** uses its default
device. **Atualizar dispositivos** refreshes the list. An unavailable explicitly
selected device stops audio rather than silently selecting another microphone.

**Iniciar teste do microfone** starts a local microphone test. The bar moves
from green through yellow to red as input grows louder, with a peak reading in
dB. The test also plays your microphone back locally, so headphones are useful.
Your microphone is not sent to the call during the test. Stop the test with
**Parar teste do microfone**; leaving the Audio tab or closing settings also
stops it. **Salvar** saves settings; **Cancelar** or Esc restores previous values.

## Push-to-talk

When **Pressionar para falar** (Push-to-talk) is enabled, use the configured
key while talking; the default binding is **Ctrl+Y**. To change it, select
**Tecla para falar** and press Enter, then press the desired combination.
Combinations with Ctrl or Alt are accepted, as are function keys, arrows,
Home/End/Page/Insert/Delete keys on their own, Shift+Tab, and the mouse wheel
buttons **4** and **5**. Letters, numbers, space and symbols need Ctrl or Alt
so ordinary typing is never captured; Shift is folded into the resulting
character. A lone Ctrl, Alt or Shift is ignored while the capture waits for the
rest of the combination. Press Esc to cancel the capture. While a call is
active and push-to-talk is enabled, the configured key is reserved for
push-to-talk; the default Ctrl+Y then does not retry a failed send. When the
captured key also belongs to another conversation command, a warning below the
list names it; the keys of quitting, help and Settings are refused. The same
key can also be changed in **Ctrl+S → Atalhos** (see
[Customize shortcuts](shortcuts.md)).

In terminals that report key presses and releases, hold the key to talk and
release it to stop. Terminals that do not report releases still send the legacy
control byte for Ctrl+letter combinations: there, each press of Ctrl+Y turns
transmission on or off, and the key repeat while it is held is ignored. In those
terminals Ctrl+number combinations cannot be told apart from typing the digit,
so choose a Ctrl+letter combination, use the **Segure para falar** button, or
bind mouse button 4/5. A wheel click toggles transmission on and off because
terminals do not send a wheel release. Releasing the key where the terminal
reports releases, pressing the key again on a terminal without them, pressing
the wheel again, losing terminal focus or opening a dialog stops transmission.
Muting the microphone overrides push-to-talk. This control works only while the
application has focus.

## If a call does not connect

If negotiation fails, check audio permissions, device availability and your
server's STUN/TURN configuration. **Fluxos desta instância** is an advanced
setting: ask the administrator for `VOICE_AUDIO_SLOTS` (default 8). It applies
when next joining that server. The backend currently does not advertise this
value automatically. For a source build, see [BUILD.md](../../BUILD.md) for required
GStreamer components. No camera or screen sharing is provided.

## Builds without voice

Builds without the GStreamer WebRTC stack have no voice calls: the call buttons
are absent, the **Áudio** panel reports that the build has no WebRTC support,
and opening a voice channel reports that voice is unavailable.
