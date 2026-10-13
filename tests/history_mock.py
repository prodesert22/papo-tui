"""Synthetic R5 data; never observations of a development backend."""
from urllib.parse import urlsplit, parse_qs
import threading
import time
from mock_server import MockServer

def uuid(n): return f'00000000-0000-4000-8000-{n:012x}'
STAMP = '2026-10-12T12:00:00.123456Z'
CHANNEL = uuid(10)
OTHER = uuid(11)

class HistoryMock(MockServer):
    def __init__(self, case='normal', count=250):
        super().__init__()
        self.case, self.count = case, count
        self.history_started = threading.Event()
        self.older_calls = 0
        self.summary_calls = 0
        self.api_handler = self.route
        self.channels = [
            {'id': OTHER, 'name': 'outro', 'position': 3, 'topic': 'Outra conversa', 'type': 'text'},
            {'id': uuid(12), 'name': 'voz', 'position': 4, 'topic': None, 'type': 'voice'},
            {'id': uuid(9), 'name': 'Categoria', 'position': 1, 'topic': None, 'type': 'category'},
            {'id': CHANNEL, 'name': 'geral', 'position': 2, 'topic': 'Tópico remoto\x1b]52;c;INJECT\x07', 'type': 'text',
             'last_message': {'id': uuid(100 + count)}, 'last_read_message': None},
        ]
        self.messages = [self.message(n) for n in range(1, count + 1)]

    def message(self, n):
        return {'id': uuid(100 + n), 'channel_id': CHANNEL, 'author_id': uuid(20 + n % 3),
                'content': f'Mensagem {n:04d}', 'created_at': STAMP, 'edited_at': None, 'reply_to': None,
                'attachments': [], 'embeds': None, 'reactions': None}

    def route(self, method, path, body):
        url = urlsplit(path)
        if url.path.endswith('/channels') and method == 'GET':
            if self.case == 'channels-invalid': return 200, {'channels': [{'id': '../bad'}]}
            if self.case == 'channels-empty': return 200, {'channels': []}
            return 200, {'channels': self.channels}
        if url.path.endswith('/messages') and '/channels/' in url.path:
            self.history_started.set()
            channel = url.path.split('/')[-2]
            if channel == OTHER:
                messages = [dict(self.message(1), channel_id=OTHER, content='Conteúdo do outro canal')]
                messages += [m for m in self.sent_messages if m['channel_id'] == OTHER]
                return 200, {'channel_id': OTHER, 'messages': list(reversed(messages)), 'has_more': False}
            if self.case == 'slow': time.sleep(0.4)
            if self.case in ('forbidden', 'removed', 'limited', 'failure'):
                return {'forbidden': 403, 'removed': 404, 'limited': 429, 'failure': 500}[self.case], None
            query = parse_qs(url.query)
            assert query.get('order') == ['desc']
            cursor = query.get('last_id', [None])[0]
            upper = self.count
            if cursor:
                self.older_calls += 1
                assert query['since'] == [STAMP]
                upper = int(cursor.split('-')[-1], 16) - 100 - 1
                if self.case == 'overlap': upper += 1
                if self.case == 'older-error' and self.older_calls == 1: return 503, None
            page = list(reversed(self.messages[max(0, upper - 100):upper]))
            more = upper > 100
            if not cursor and self.sent_messages:
                messages = self.messages + [m for m in self.sent_messages if m['channel_id'] == CHANNEL]
                page = list(reversed(messages[-100:])); more = len(messages) > 100
            if self.case == 'empty-page' and cursor: page, more = [], True
            if self.case == 'empty': page, more = [], False
            if self.case == 'wrong-channel': page = [dict(page[0], channel_id=OTHER)]
            if self.case == 'invalid-date': page = [dict(page[0], created_at='2026-02-30T00:00:00Z')]
            if self.case == 'null-item': page = [None]
            if self.case == 'too-many': page += [self.message(1)]
            if self.case == 'repeat' and cursor: page, more = list(reversed(self.messages[-100:])), True
            return 200, {'channel_id': CHANNEL, 'messages': page, 'has_more': more}
        if url.path.endswith('/users/user_summary_batch') and method == 'POST':
            self.summary_calls += 1
            assert 1 <= len(body['ids']) <= 1000
            assert len(body['ids']) == len(set(body['ids']))
            if self.case == 'authors-error': return 503, None
            if self.case == 'authors-401' and self.summary_calls == 1: return 401, None
            if self.case == 'authors-twice': return 401, None
            return 200, [{'id': id, 'username': self.username if id == uuid(1) else 'Autor ' + id[-1], 'nickname': 'Apelido' if id == uuid(21) else None}
                         for id in body['ids'] if id != uuid(22)]
        return None
