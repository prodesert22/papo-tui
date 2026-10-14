"""Synthetic realtime fixtures, independent of real backend observations."""
import copy
import json
import socket
import threading
import time
from urllib.parse import urlsplit, parse_qs
import websocket
from history_mock import HistoryMock, CHANNEL, STAMP


class RealtimeMock(HistoryMock):
    def __init__(self, count=3):
        super().__init__(count=count)
        self.websocket_hook = self.connected
        self.peers = []
        self.lock = threading.RLock()
        self.delay = 0
        self.offline = False
        self.revoked = False
        self.recovery_status = None
        self.snapshot_started = threading.Event()

    def connected(self, connection, send):
        with self.lock: self.peers.append((connection, send))

    def emit(self, event, fragmented=False):
        data = json.dumps(event, ensure_ascii=False).encode()
        with self.lock:
            for connection, send in self.peers:
                if not connection.connected: continue
                try:
                    if fragmented:
                        split = data.index('á'.encode()) + 1
                        send(data[:split], fin=0)
                        send(b'r7-ping', websocket.ABNF.OPCODE_PING)
                        send(data[split:], websocket.ABNF.OPCODE_CONT)
                    else: send(data)
                except (OSError, websocket.WebSocketException): pass

    def disconnect(self):
        with self.lock:
            for connection, _send in self.peers:
                try: connection.sock.shutdown(socket.SHUT_RDWR)
                except OSError: pass
            self.peers.clear()

    def route(self, method, path, body):
        url = urlsplit(path)
        if self.offline and method == 'GET' and not url.path.endswith('/ws'): return 503, None
        if self.revoked and url.path.endswith('/auth/whoami'): return 401, None
        if '/channels/' in url.path and url.path.endswith('/messages'):
            if self.recovery_status: return self.recovery_status, None
            query = parse_qs(url.query)
            cursor = query.get('last_id', [None])[0]
            ascending = query.get('order') == ['asc']
            with self.lock:
                messages = copy.deepcopy(self.messages + self.sent_messages)
            messages.sort(key=lambda m: (m['created_at'], m['id']))
            if cursor:
                stamp = query['since'][0]
                messages = [m for m in messages if ((m['created_at'], m['id']) > (stamp, cursor) if ascending
                                                  else (m['created_at'], m['id']) < (stamp, cursor))]
            if not ascending: messages.reverse()
            page = messages[:100]
            self.snapshot_started.set()
            if self.delay: time.sleep(self.delay)
            return 200, {'channel_id': CHANNEL, 'messages': page, 'has_more': len(messages) > 100}
        response = super().route(method, path, body)
        return copy.deepcopy(response)
