"""Fake sway and Hyprland IPC sockets that record what a client asks of them."""

import json
import os
import socket
import struct
import threading


class _Server:
    def __init__(self, path):
        self.path = path
        self.commands = []
        self._socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self._socket.bind(path)
        self._socket.listen(8)
        self._socket.settimeout(0.2)
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, daemon=True)

    def __enter__(self):
        self._thread.start()
        return self

    def __exit__(self, *error):
        self._stop.set()
        self._thread.join(timeout=5)
        self._socket.close()
        os.unlink(self.path)

    def _serve(self):
        while not self._stop.is_set():
            try:
                connection, _ = self._socket.accept()
            except socket.timeout:
                continue
            with connection:
                connection.settimeout(2)
                try:
                    self.handle(connection)
                except (OSError, ValueError):
                    pass


def _receive(connection, size):
    data = b""
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise ValueError("closed")
        data += chunk
    return data


class Sway(_Server):
    """i3 IPC: GET_TREE answers with the given windows, RUN_COMMAND is recorded."""

    def __init__(self, path, windows):
        super().__init__(path)
        self.tree = {
            "id": 1,
            "type": "root",
            "nodes": [{"id": 2, "type": "workspace", "nodes": [], "floating_nodes": windows}],
        }

    def handle(self, connection):
        while True:
            header = _receive(connection, 14)
            if header[:6] != b"i3-ipc":
                return
            length, kind = struct.unpack("=II", header[6:])
            payload = _receive(connection, length).decode()
            if kind == 4:
                reply = self.tree
            else:
                self.commands.append(payload)
                reply = [{"success": True}]
            body = json.dumps(reply).encode()
            connection.sendall(b"i3-ipc" + struct.pack("=II", len(body), kind) + body)


class Hyprland(_Server):
    """One request per connection: j/clients lists the windows, dispatches are recorded."""

    def __init__(self, path, clients):
        super().__init__(path)
        self.clients = clients

    def handle(self, connection):
        request = connection.recv(4096).decode()
        if request == "j/clients":
            connection.sendall(json.dumps(self.clients).encode())
        else:
            self.commands.append(request)
            connection.sendall(b"ok")
