"""Voice-channel text conversations through the TUI; synthetic HTTP/WS only."""

from test_context import main as test_main

import json
import time
import unittest
from pathlib import Path
from urllib.parse import urlsplit

from history_mock import HistoryMock, CHANNEL, VOICE, uuid
from realtime_mock import RealtimeMock
from r8_mock import R8Mock
from terminal_support import Terminal, voice_supported


class VoiceRealtimeMock(RealtimeMock):
    def __init__(self):
        super().__init__(count=3)
        self.messages = [
            dict(m, id=uuid(500 + n), channel_id=VOICE, content=f"Conversa de voz {n:04d}")
            for n, m in enumerate(self.messages, 1)
        ]

    def route(self, method, path, body):
        response = super().route(method, path, body)
        if urlsplit(path).path.endswith(f"/channels/{VOICE}/messages") and response[0] == 200:
            response[1]["channel_id"] = VOICE
        return response


class VoiceConversation(unittest.TestCase):
    def require_voice(self, t):
        """Skip when the program was built without the WebRTC voice support."""
        if not voice_supported(t):
            self.skipTest("Build without the GStreamer WebRTC stack.")

    def test_live_members_and_speakers_stay_below_voice_channel(self):
        with RealtimeMock() as server:
            t = self.login(server)
            self.require_voice(t)
            self.wait(t, "Tempo real conectado")
            t.send("\x1bOB\n")
            self.wait(t, "Mensagem 0003")
            t.send("rascunho preservado")
            server.emit(
                dict(
                    type="presence_sync",
                    members=[
                        dict(
                            user_id=uuid(21), status="online", status_message="", user_voice=[VOICE]
                        )
                    ],
                )
            )
            deadline = time.monotonic() + 8

            def participant():
                for y, line in enumerate(t.screen.display):
                    if "Apelido" in line[:25] and "voz" in t.screen.display[y - 1][:25]:
                        return y, line.index("Apelido")
                return None

            while not participant() and time.monotonic() < deadline:
                t.read(0.03)
            self.assertIsNotNone(participant(), self.screen(t))

            def participant_line():
                found = participant()
                return t.screen.display[found[0]] if found else ""

            def wait_line(text):
                deadline = time.monotonic() + 5
                while text not in participant_line() and time.monotonic() < deadline:
                    t.read(0.03)
                self.assertIn(text, participant_line(), self.screen(t))

            server.emit(
                dict(
                    type="voice_state_update",
                    channel_id=VOICE,
                    user_id=uuid(21),
                    muted=False,
                    camera_on=False,
                    screen_sharing=False,
                )
            )
            wait_line("[●]")
            y, x = participant()
            idle = t.screen.buffer[y][x].fg
            server.emit(dict(type="active_speaker_update", channel_id=VOICE, user_ids=[uuid(21)]))
            deadline = time.monotonic() + 5
            while t.screen.buffer[y][x].fg == idle and time.monotonic() < deadline:
                t.read(0.03)
            self.assertIn(t.screen.buffer[y][x].fg, (46, "00ff00"), self.screen(t))
            server.emit(dict(type="active_speaker_update", channel_id=VOICE, user_ids=[]))
            deadline = time.monotonic() + 5
            while t.screen.buffer[y][x].fg != idle and time.monotonic() < deadline:
                t.read(0.03)
            self.assertEqual(t.screen.buffer[y][x].fg, idle)
            server.emit(
                dict(
                    type="voice_state_update",
                    channel_id=VOICE,
                    user_id=uuid(21),
                    muted=True,
                    camera_on=False,
                    screen_sharing=False,
                )
            )
            wait_line("[●×]")
            self.assertNotIn(t.screen.buffer[y][x].fg, (46, "00ff00"), self.screen(t))
            server.emit(dict(type="voice_leave", channel_id=VOICE, user_id=uuid(21)))
            deadline = time.monotonic() + 5
            while participant() and time.monotonic() < deadline:
                t.read(0.03)
            self.assertIsNone(participant())
            self.assertIn("rascunho preservado", self.screen(t))
            self.assertFalse(
                any(e.get("type", "").startswith("voice_") for e in server.ws_messages)
            )

    def test_own_name_turns_green_only_in_member_list(self):
        with RealtimeMock() as server:
            t = self.login(server)
            self.require_voice(t)
            self.assertTrue(self.join_call(t, server), (server.ws_messages, self.screen(t)))
            server.emit(
                dict(
                    type="voice_joined",
                    channel_id=VOICE,
                    members=[
                        dict(
                            user_id=uuid(1),
                            muted=False,
                            camera_on=False,
                            screen_sharing=False,
                        )
                    ],
                    active_speakers=[],
                )
            )

            def positions():
                voice_row = next(
                    (y for y, line in enumerate(t.screen.display) if "♪ voz" in line[:25]), None
                )
                if voice_row is None or "josé" not in t.screen.display[voice_row + 1]:
                    return None
                member = (voice_row + 1, t.screen.display[voice_row + 1].index("josé"))
                top = next(
                    (
                        (y, t.screen.display[y].index("josé"))
                        for y in range(len(t.screen.display) - 1, -1, -1)
                        if "josé" in t.screen.display[y]
                    ),
                    None,
                )
                if top is None or top[0] <= member[0]:
                    return None
                return member, top

            def colors():
                found = positions()
                return None if found is None else [t.screen.buffer[y][x].fg for y, x in found]

            deadline = time.monotonic() + 8
            while colors() is None and time.monotonic() < deadline:
                t.read(0.03)
            self.assertIsNotNone(colors(), self.screen(t))
            idle = colors()
            self.assertFalse(idle[0] in (46, "00ff00"))
            server.emit(dict(type="active_speaker_update", channel_id=VOICE, user_ids=[uuid(1)]))
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                current = colors()
                if current and current[0] in (46, "00ff00") and current[1] == idle[1]:
                    break
                t.read(0.03)
            else:
                self.fail(self.screen(t))
            server.emit(dict(type="active_speaker_update", channel_id=VOICE, user_ids=[]))
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                if colors() == idle:
                    break
                t.read(0.03)
            else:
                self.fail(self.screen(t))

    def test_audio_and_microphone_buttons_toggle_independently(self):
        with RealtimeMock() as server:
            t = self.login(server)
            self.require_voice(t)
            t.send("\x1bOB\n")
            self.wait(t, "Mensagem 0003")
            t.send("rascunho preservado")
            row = next(y for y, line in enumerate(t.screen.display) if "⚙" in line)
            t.click(row, t.screen.display[row].index("♪"))
            self.wait(t, "[♪×]")
            t.click(row, t.screen.display[row].index("●"))
            self.wait(t, "[●×]")
            t.click(row, t.screen.display[row].index("♪"))
            self.wait(t, "[♪]")
            self.assertIn("●×", t.screen.display[row])
            t.click(row, t.screen.display[row].index("●"))
            self.wait(t, "[●]")
            self.assertIn("rascunho preservado", self.screen(t))
            self.assertFalse(
                any(e.get("type", "").startswith("voice_") for e in server.ws_messages)
            )

    def test_configured_key_and_mouse_push_to_talk_control_transmission(self):
        with RealtimeMock() as server:
            t = self.login(server)
            self.require_voice(t)
            # The synthetic terminal answers the keyboard-capability query so
            # press/release events use the same CSI-u path as a real terminal.
            t.send("\x1b[?1u")
            self.configure_ptt(t, "\x1b[109;6u", "Ctrl+Shift+M", "ctrl+shift+m")
            self.assertTrue(self.join_call(t, server), (server.ws_messages, self.screen(t)))
            self.wait(t, "Chamada de voz")
            server.emit(dict(type="voice_joined", channel_id=VOICE, members=[], active_speakers=[]))
            self.wait(t, "Segure para falar")
            t.send("\x1b[109;6:1u")
            self.wait(t, "[Transmitindo]")
            t.send("\x1b[109;6:3u")
            self.wait(t, "Segure para falar")
            self.configure_ptt(t, "\x1b[<64;10;5M", "Mouse 4", "mouse4", enable=False)
            t.send("\x1b[<64;10;5M")
            self.wait(t, "[Transmitindo]")
            t.send("\x1b[<64;10;5M")
            self.wait(t, "Segure para falar")
            self.cancel_call(t)
            self.assertTrue(self.wait_event(t, server, "voice_leave"), server.ws_messages)

    def test_legacy_ctrl_letter_push_to_talk_toggles_transmission(self):
        with RealtimeMock() as server:
            t = self.login(server)
            self.require_voice(t)
            # This terminal does not answer the keyboard-capability query, so
            # Ctrl+Y arrives as the legacy 0x19 byte and has no release event.
            self.configure_ptt(t, "\x19", "Ctrl+Y", "ctrl+y")
            self.assertTrue(self.join_call(t, server), (server.ws_messages, self.screen(t)))
            self.wait(t, "Chamada de voz")
            server.emit(dict(type="voice_joined", channel_id=VOICE, members=[], active_speakers=[]))
            self.wait(t, "Segure para falar")
            t.send("\x19")
            self.wait(t, "[Transmitindo]")
            # The second press must not be mistaken for a key repeat.
            deadline = time.monotonic() + 0.75
            while time.monotonic() < deadline:
                t.read(0.03)
            t.send("\x19")
            self.wait(t, "Segure para falar")

    def settings_tab_row(self, t):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            for row, line in enumerate(t.screen.display):
                if "Perfil" in line and "Áudio" in line:
                    return row
            t.read(0.03)
        self.fail(self.screen(t))

    def configure_ptt(self, t, sequence, expected, saved_key, *, enable=True):
        t.send("\x13")
        self.wait(t, "Configurações")
        row = self.settings_tab_row(t)
        t.click(row, t.screen.display[row].index("Áudio"))
        self.wait(t, "Tecla para falar:")
        if enable:
            t.send("\t" + "\x1bOB" * 5 + "\n")
            self.wait(t, "Pressionar para falar: < Ativado >")
            t.send("\x1bOB\n")
        else:
            t.send("\t" + "\x1bOB" * 6 + "\n")
        self.wait(t, "pressione uma combinação")
        t.send(sequence)
        self.wait(t, f"Tecla para falar: < {expected} >")
        t.send("\t\n")
        path = Path(t.configuration.name, "papo-tui", "config.json")
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if path.exists():
                audio = json.loads(path.read_text()).get("audio", {})
                if audio.get("push_to_talk", {}).get("key") == saved_key:
                    self.assertTrue(audio["push_to_talk"]["enabled"])
                    return
            t.read(0.03)
        self.fail(self.screen(t))

    def test_join_reads_ice_servers_negotiates_and_leaves_the_call(self):
        with RealtimeMock() as server:
            t = self.login(server)
            self.require_voice(t)
            self.assertTrue(self.join_call(t, server), (server.ws_messages, self.screen(t)))
            self.assertTrue(
                any(r[1].endswith("/voice/ice-servers") for r in server.requests), server.requests
            )
            joins = [e for e in server.ws_messages if e.get("type") == "voice_join"]
            self.assertEqual(len(joins), 1)
            self.assertEqual(joins[0]["channel_id"], VOICE)
            self.wait(t, "Chamada de voz")
            server.emit(dict(type="voice_joined", channel_id=VOICE, members=[], active_speakers=[]))
            # The engine answers with an offer when a device is reachable and
            # gives up otherwise; both prove the join was processed.
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline and not any(
                e.get("type") in ("voice_offer", "voice_leave") for e in server.ws_messages
            ):
                t.read(0.05)
            self.assertTrue(
                any(e.get("type") in ("voice_offer", "voice_leave") for e in server.ws_messages),
                server.ws_messages,
            )
            self.cancel_call(t)
            self.assertTrue(self.wait_event(t, server, "voice_leave"), server.ws_messages)

    def test_open_voice_channel_allows_a_member_who_is_not_the_owner(self):
        with HistoryMock(count=1) as server:
            server.owner_id = "00000000-0000-4000-8000-000000000099"
            t = self.login(server)
            self.require_voice(t)
            self.assertTrue(self.join_call(t, server), (server.ws_messages, self.screen(t)))
            self.assertNotIn("sem permissão", self.screen(t))
            self.cancel_call(t)
            self.assertTrue(self.wait_event(t, server, "voice_leave"), server.ws_messages)

    def test_denied_voice_channel_reports_the_notice_once(self):
        with HistoryMock(count=1) as server:
            server.owner_id = "00000000-0000-4000-8000-000000000099"
            role = uuid(30)
            server.user_roles = [dict(id=role, color="#ffffff")]
            server.roles = [dict(id=role, name="Membro", color="#ffffff", permissions={})]
            for channel in server.channels:
                if channel["type"] == "voice":
                    channel["permissions"] = [
                        dict(
                            role_id=role,
                            permissions=dict(
                                read_channel=True,
                                send_messages=True,
                                delete_messages=False,
                                connect_voice=False,
                            ),
                        )
                    ]
            t = self.login(server)
            self.require_voice(t)
            t.resize(30, 200)
            self.wait(t, "♪ voz")
            row = next(y for y, line in enumerate(t.screen.display) if "♪ voz" in line[:40])
            t.click(row, 5)
            self.wait(t, "Voz indisponível ou sem permissão de conexão.")
            self.assertEqual(self.screen(t).count("Voz indisponível ou sem permissão"), 1)
            self.assertFalse(
                any(e.get("type") == "voice_join" for e in server.ws_messages), server.ws_messages
            )

    def join_call(self, t, server):
        """Click the voice channel once and wait for the join request."""
        row = next((y for y, line in enumerate(t.screen.display) if "♪ voz" in line[:25]), None)
        self.assertIsNotNone(row, self.screen(t))
        t.click(row, 5)
        return self.wait_event(t, server, "voice_join", 15)

    def cancel_call(self, t):
        """Leave the call with the focused button."""
        for _ in range(20):
            t.send("\t")
            if "Foco: Sair da chamada" in self.screen(t):
                break
        self.wait(t, "Foco: Sair da chamada")
        t.send("\n")

    def wait_event(self, t, server, kind, seconds=10):
        """Wait until the client sends an event of the given kind."""
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if any(e.get("type") == kind for e in server.ws_messages):
                return True
            t.read(0.05)
        return False

    def screen(self, t):
        return "\n".join(t.screen.display)

    def wait(self, t, text):
        deadline = time.monotonic() + 8
        while text not in self.screen(t) and time.monotonic() < deadline:
            t.read(0.03)
        self.assertIn(text, self.screen(t))

    def login(self, server, *, cleanup=True):
        t = Terminal()
        if cleanup:
            self.addCleanup(lambda: self.assertTrue(t.close()))
        t.send(server.url + "\tjosé\tSynthetic!123\n")
        self.wait(t, "Categoria")
        self.wait(t, "Conversar em #geral")
        return t

    def reads(self, server):
        return [r for r in server.requests if r[0] == "GET" and "/messages" in r[1]]

    def voice(self, t, packet="\x0f"):
        t.send("\x1bOB\x1bOF" + packet)  # tabs -> list -> last channel
        self.wait(t, "♪ voz · Histórico")

    def test_enter_click_and_right_preserve_conversation_shift_enter_opens(self):
        with HistoryMock(count=1) as server:
            t = self.login(server)
            t.send("\x1bOB\n")
            self.wait(t, "Mensagem 0001")
            t.send("Rascunho do geral")
            t.click(6, 4)
            self.wait(t, "Foco: Canais")
            self.wait(t, "Ctrl+G: ajuda")
            before = self.reads(server)
            for action in ("\n", "\r", "\x1b[13u", "\x1bOM", "\x1bOC"):
                t.send(action)
                self.wait(t, "Foco: Canais")
                self.assertIn("# geral · Histórico", self.screen(t))
                self.assertIn("Rascunho do geral", self.screen(t))
                self.assertEqual(self.reads(server), before)
            t.click(6, 4)
            self.assertEqual(self.reads(server), before)
            self.assertIn("# geral · Histórico", self.screen(t))
            t.send("\x1b[13;2u")
            self.wait(t, "Conversa de voz 0001")
            self.assertIn("♪ voz · Histórico", self.screen(t))
            self.assertEqual(len(self.reads(server)), len(before) + 1)
            t.send("Rascunho de voz\x1b\x1bOD\n")
            self.wait(t, "Foco: Canais")
            self.assertIn("Rascunho de voz", self.screen(t))
            before = self.reads(server)
            t.send("\x0f")
            self.wait(t, "Foco: Histórico")
            self.assertEqual(self.reads(server), before)
            self.assertIn("Rascunho de voz", self.screen(t))

    def test_history_shortcut_encodings_with_lock_modifiers_and_narrow_screen(self):
        with HistoryMock(count=0) as server:
            t = self.login(server)
            # Category clicks cannot change the channel or trigger a new read.
            self.wait(t, "Nenhuma mensagem neste canal")
            before = self.reads(server)
            t.click(3, 4)
            self.assertEqual(self.reads(server), before)
            for locks in (0, 64, 128, 192):
                for packet in (
                    f"\x1b[111;{5 + locks}u",
                    f"\x1b[27;{5 + locks};111~",
                    f"\x1b[13;{2 + locks}u",
                    f"\x1b[27;{2 + locks};13~",
                    f"\x1b[57414;{2 + locks}u",
                ):
                    with self.subTest(packet=packet):
                        t.click(4, 4)
                        self.wait(t, "# geral · Histórico")
                        t.send("\x1bOD\x1bOF" + packet)
                        self.wait(t, "♪ voz · Histórico")
                        self.wait(t, "Nenhuma mensagem neste canal")
            t.send("\x1bOD")
            t.resize(14, 44)
            self.wait(t, "Ctrl+G: ajuda")
            before = self.reads(server)
            t.send("\n")
            self.assertEqual(self.reads(server), before)
            t.send("\x1b[13;2u")
            self.wait(t, "♪ voz · Histórico")

    def test_pagination_send_and_independent_drafts(self):
        with HistoryMock(count=150) as server:
            t = self.login(server)
            self.voice(t)
            self.wait(t, "Conversa de voz 0150")
            self.wait(t, "Autor 4")
            t.read(0.3)
            t.send("\x1bOH")
            self.wait(t, "Conversa de voz 0051")
            t.send("\x1b[5~")
            deadline = time.monotonic() + 5
            while not server.older_calls and time.monotonic() < deadline:
                t.read(0.03)
            self.assertEqual(server.older_calls, 1)
            t.send("\x1bOH")
            self.wait(t, "Conversa de voz 0001")
            t.send("\x1bOFTexto enviado no canal de voz\n")
            self.wait(t, "Mensagem confirmada")
            self.assertEqual(server.sent_messages[-1]["channel_id"], VOICE)
            self.assertEqual(server.sent_messages[-1]["content"], "Texto enviado no canal de voz")
            self.assertIn("Texto enviado no canal de voz", self.screen(t))
            t.send("Rascunho da voz\x1b\x1bOD\x1bOH\n")
            self.wait(t, "# geral · Histórico")
            t.send("Rascunho do texto\x1b\x1bOD\x1bOF\x0f")
            self.wait(t, "♪ voz · Histórico")
            self.wait(t, "Rascunho da voz")
            t.send("\n\x05")
            t.read(0.2)
            self.wait(t, "Emojis")
            self.assertEqual(len(server.sent_messages), 1)  # Ctrl+E in the editor opens emojis.
            t.send("\x1b")
            t.send("\x1b\x1bOD\x1bOH\n")
            self.wait(t, "Rascunho do texto")

    def test_history_errors_can_be_retried_without_enter_starting_voice(self):
        for case, expected in (
            ("forbidden", "Sem permissão"),
            ("removed", "removido"),
            ("limited", "Limite"),
            ("failure", "Falha"),
        ):
            with self.subTest(case=case), HistoryMock(case, count=1) as server:
                t = self.login(server, cleanup=False)
                try:
                    self.wait(t, expected)
                    before = self.reads(server)
                    t.send("\x1bOB\x1bOF\n")
                    self.assertEqual(self.reads(server), before)
                    t.send("\x0f")
                    self.wait(t, expected)
                    deadline = time.monotonic() + 5
                    while len(self.reads(server)) == len(before) and time.monotonic() < deadline:
                        t.read(0.03)
                    self.assertEqual(len(self.reads(server)), len(before) + 1)
                    self.wait(t, expected)
                    server.case = "normal"
                    t.read(1.1 if case == "limited" else 0.1)
                    t.send("\x12")
                    self.wait(t, "Conversa de voz 0001")
                    self.assertTrue(all(VOICE in r[1] for r in self.reads(server)[len(before) :]))
                finally:
                    self.assertTrue(t.close())

    def test_realtime_and_recovery_keep_voice_chat_and_draft(self):
        with VoiceRealtimeMock() as server:
            t = self.login(server)
            self.voice(t)
            self.wait(t, "Conversa de voz 0003")
            self.wait(t, "Tempo real conectado")
            server.emit(dict(type="typing", channel_id=VOICE, user_id=uuid(21), is_typing=True))
            self.wait(t, "Apelido está digitando")
            server.emit(dict(type="typing", channel_id=VOICE, user_id=uuid(21), is_typing=False))
            t.send("Rascunho durante reconexão")
            self.assertTrue(
                any(
                    event.get("type") == "typing" and event.get("channel_id") == VOICE
                    for event in server.ws_messages
                )
            )
            message = dict(
                server.message(4), id=uuid(504), channel_id=VOICE, content="Texto ao vivo na voz"
            )
            server.messages.append(message)
            server.emit(dict(message, type="message"))
            self.wait(t, "Texto ao vivo na voz")
            server.offline = True
            server.disconnect()
            self.wait(t, "Reconectando")
            server.messages[-1]["content"] = "Texto recuperado na voz"
            server.offline = False
            self.wait(t, "Texto recuperado na voz")
            self.wait(t, "Rascunho durante reconexão")
            self.assertTrue(all(CHANNEL in r[1] or VOICE in r[1] for r in self.reads(server)))
            self.assertFalse(
                any(event.get("type", "").startswith("voice_") for event in server.ws_messages)
            )

    def test_notification_destination_opens_voice_conversation(self):
        with R8Mock(count=1) as server:
            server.notifications[0].update(channel_id=VOICE, message_id=uuid(501))
            original = server.api_handler

            def route(method, path, body):
                if urlsplit(path).path.endswith(f"/channels/{VOICE}/messages"):
                    return HistoryMock.route(server, method, path, body)
                return original(method, path, body)

            server.api_handler = route
            t = self.login(server)
            t.send("\x04")
            self.wait(t, "Aviso 0001")
            t.send("\n")
            self.wait(t, "Detalhes da notificação")
            self.assertEqual(len(self.reads(server)), 1)
            self.assertIn(CHANNEL, self.reads(server)[0][1])
            t.send("\n")
            self.wait(t, "♪ voz · Histórico")
            self.wait(t, "Conversa de voz 0001")
            self.wait(t, "Foco: Seleção")
            self.assertEqual(len(self.reads(server)), 2)
            self.assertIn(VOICE, self.reads(server)[1][1])


if __name__ == "__main__":
    test_main(
        1,
    )
