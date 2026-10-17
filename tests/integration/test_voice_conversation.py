"""Voice-channel text conversations through the TUI; synthetic HTTP/WS only."""

import time
import unittest
from urllib.parse import urlsplit

from history_mock import HistoryMock, VOICE, uuid
from realtime_mock import RealtimeMock
from r8_mock import R8Mock
from test_terminal import Terminal


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
        return t

    def reads(self, server):
        return [r for r in server.requests if r[0] == "GET" and "/messages" in r[1]]

    def voice(self, t, packet="\x1bOS"):
        t.send("\x1bOB\x1bOF" + packet)  # tabs -> list -> last channel
        self.wait(t, "♪ voz · Histórico")

    def test_enter_click_and_right_preserve_conversation_shift_enter_opens(self):
        with HistoryMock(count=1) as server:
            t = self.login(server)
            t.send("\x1bOB\x1bOB\n")
            self.wait(t, "Mensagem 0001")
            t.send("Rascunho do geral")
            t.click(9, 4)
            self.wait(t, "Foco: Canais")
            self.wait(t, "F4")
            before = self.reads(server)
            for action in ("\n", "\r", "\x1b[13u", "\x1bOM", "\x1bOC"):
                t.send(action)
                self.wait(t, "Foco: Canais")
                self.assertIn("# geral · Histórico", self.screen(t))
                self.assertIn("Rascunho do geral", self.screen(t))
                self.assertEqual(self.reads(server), before)
            t.click(9, 4)
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
            t.send("\x1bOS")
            self.wait(t, "Foco: Histórico")
            self.assertEqual(self.reads(server), before)
            self.assertIn("Rascunho de voz", self.screen(t))

    def test_history_shortcut_encodings_with_lock_modifiers_and_narrow_screen(self):
        with HistoryMock(count=0) as server:
            t = self.login(server)
            # F4 on a category must not request any history.
            t.send("\x1bOB\x1bOS")
            self.assertEqual(self.reads(server), [])
            for locks in (0, 64, 128, 192):
                for packet in (
                    f"\x1b[1;{1 + locks}S",
                    f"\x1b[14;{1 + locks}~",
                    f"\x1b[13;{2 + locks}u",
                    f"\x1b[27;{2 + locks};13~",
                    f"\x1b[57414;{2 + locks}u",
                ):
                    with self.subTest(packet=packet):
                        t.click(7, 4)
                        self.wait(t, "# geral · Histórico")
                        t.send("\x1bOD\x1bOF" + packet)
                        self.wait(t, "♪ voz · Histórico")
                        self.wait(t, "Nenhuma mensagem neste canal")
            t.send("\x1bOD")
            t.resize(14, 44)
            self.wait(t, "F4")
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
            t.send("Rascunho da voz\x1b\x1bOD\x1bOH\x1bOB\n")
            self.wait(t, "# geral · Histórico")
            t.send("Rascunho do texto\x1b\x1bOD\x1bOF\x1bOS")
            self.wait(t, "♪ voz · Histórico")
            self.wait(t, "Rascunho da voz")
            t.send("\n\x1bOS")
            t.read(0.2)
            self.wait(t, "Emojis")
            self.assertEqual(len(server.sent_messages), 1)  # F4 in the editor opens emojis.
            t.send("\x1b")
            t.send("\x1b\x1bOD\x1bOH\x1bOB\n")
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
                    t.send("\x1bOB\x1bOF\n")
                    self.assertEqual(self.reads(server), [])
                    t.send("\x1bOS")
                    self.wait(t, expected)
                    self.assertEqual(len(self.reads(server)), 1)
                    server.case = "normal"
                    t.read(1.1 if case == "limited" else 0.1)
                    t.send("\x12")
                    self.wait(t, "Conversa de voz 0001")
                    self.assertTrue(all(VOICE in r[1] for r in self.reads(server)))
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
            self.assertTrue(all(VOICE in r[1] for r in self.reads(server)))
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
            t.send("\x1b[20~")
            self.wait(t, "Aviso 0001")
            t.send("\n")
            self.wait(t, "Detalhes da notificação")
            self.assertEqual(self.reads(server), [])
            t.send("\n")
            self.wait(t, "♪ voz · Histórico")
            self.wait(t, "Conversa de voz 0001")
            self.wait(t, "↑↓ mensagens")
            self.assertEqual(len(self.reads(server)), 1)
            self.assertIn(VOICE, self.reads(server)[0][1])


if __name__ == "__main__":
    unittest.main()
