#!/usr/bin/env python3
"""End-to-end test: start the real server on a free port and play it over
real HTTP and WebSocket connections. Standard library only.

usage: server_e2e.py <mob-survivor-server> <web-dir>
"""

import base64
import hashlib
import json
import os
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"


class WebSocket:
    """Just enough of an RFC 6455 client for the test."""

    def __init__(self, port, headers=None):
        self.sock = socket.create_connection(("127.0.0.1", port), timeout=5)
        key = base64.b64encode(os.urandom(16)).decode()
        extra = "".join(f"{k}: {v}\r\n" for k, v in (headers or {}).items())
        self.sock.sendall(
            (
                f"GET /ws HTTP/1.1\r\nHost: 127.0.0.1:{port}\r\nUpgrade: websocket\r\n"
                f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                f"Sec-WebSocket-Version: 13\r\n{extra}\r\n"
            ).encode()
        )
        self.buffer = b""
        while b"\r\n\r\n" not in self.buffer:
            chunk = self.sock.recv(4096)
            assert chunk, "server closed during the handshake"
            self.buffer += chunk
        head, self.buffer = self.buffer.split(b"\r\n\r\n", 1)
        assert head.startswith(b"HTTP/1.1 101"), head
        expected = base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode()
        assert f"Sec-WebSocket-Accept: {expected}".encode() in head, head

    def send(self, message):
        payload = json.dumps(message).encode()
        mask = os.urandom(4)
        header = bytes([0x81])
        if len(payload) < 126:
            header += bytes([0x80 | len(payload)])
        else:
            header += bytes([0x80 | 126]) + struct.pack(">H", len(payload))
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.sock.sendall(header + mask + masked)

    def _read(self, n):
        while len(self.buffer) < n:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise EOFError("server closed the connection")
            self.buffer += chunk
        data, self.buffer = self.buffer[:n], self.buffer[n:]
        return data

    def receive(self):
        """Returns (opcode, payload) for the next data frame; answers pings."""
        while True:
            b0, b1 = self._read(2)
            length = b1 & 0x7F
            if length == 126:
                (length,) = struct.unpack(">H", self._read(2))
            elif length == 127:
                (length,) = struct.unpack(">Q", self._read(8))
            payload = self._read(length)
            opcode = b0 & 0x0F
            if opcode == 0x9:
                continue
            return opcode, payload

    def wait_for(self, kind, timeout=10.0):
        """The next text message of type `kind` (others are skipped)."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            opcode, payload = self.receive()
            if opcode == 0x1:
                message = json.loads(payload)
                if message.get("t") == kind:
                    return message
        raise AssertionError(f"no {kind!r} message within {timeout}s")

    def next_snapshot(self, timeout=5.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            opcode, payload = self.receive()
            if opcode == 0x2:
                return parse_snapshot(payload)
        raise AssertionError("no snapshot")

    def close(self):
        self.sock.close()


def parse_snapshot(data):
    """Decodes the binary snapshot (layout: docs/protocol.md)."""
    at = 0

    def take(fmt):
        nonlocal at
        values = struct.unpack_from("<" + fmt, data, at)
        at += struct.calcsize("<" + fmt)
        return values

    kind, phase, level, tick, left, elapsed, frenzy, outcome = take("BBHIffBB")
    assert kind == 1
    bases = [take("ii") for _ in range(2)]
    cannons = [take("BBhBBBB") for _ in range(take("B")[0])]
    gates = [take("h")[0] for _ in range(take("B")[0])]
    saws = [take("h")[0] for _ in range(take("B")[0])]
    bombs = [take("BhhhhhB") for _ in range(take("B")[0])]
    powerups = [take("IhhHH") for _ in range(take("B")[0])]
    events = [take("BBhhhi") for _ in range(take("H")[0])]
    mobs = [take("IhhBH") for _ in range(take("H")[0])]
    assert at == len(data), (at, len(data))
    return {
        "phase": phase,
        "level": level,
        "tick": tick,
        "bases": bases,
        "cannons": cannons,
        "gates": gates,
        "saws": saws,
        "bombs": bombs,
        "powerups": powerups,
        "events": events,
        "mobs": mobs,
    }


def http_get(port, path):
    try:
        with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=5) as response:
            return response.status, response.read(), response.headers
    except urllib.error.HTTPError as error:
        return error.code, error.read(), error.headers


def main():
    server_path, web_dir = sys.argv[1], sys.argv[2]
    data_dir = tempfile.mkdtemp(prefix="mob-survivor-e2e-")
    server = subprocess.Popen(
        [server_path, "--bind", "127.0.0.1", "--port", "0", "--web", web_dir, "--data", data_dir],
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        port = None
        deadline = time.time() + 10
        while port is None and time.time() < deadline:
            line = server.stderr.readline()
            if not line:
                break
            event = json.loads(line)
            if event.get("event") == "listening":
                port = event["port"]
        assert port, "server did not start"

        status, body, headers = http_get(port, "/")
        assert status == 200 and b"<title>" in body, status
        assert headers["Content-Type"].startswith("text/html")
        status, body, _ = http_get(port, "/healthz")
        assert status == 200 and body == b"ok\n"
        # The container healthcheck mode: 0 against the live server, 1 when
        # nothing answers (port 1 on loopback is closed).
        probe = [server_path, "--healthcheck", "--bind", "127.0.0.1", "--port"]
        assert subprocess.run(probe + [str(port)], timeout=10).returncode == 0
        assert subprocess.run(probe + ["1"], timeout=10, stderr=subprocess.DEVNULL).returncode == 1
        status, body, _ = http_get(port, "/api/status")
        assert status == 200 and json.loads(body)["ok"] is True
        assert http_get(port, "/nope.js")[0] == 404
        assert http_get(port, "/../CMakeLists.txt")[0] == 404
        assert http_get(port, "/ws")[0] == 426

        # Two friends: a campaign room, joined by code.
        ada = WebSocket(port, {"Tailscale-User-Name": "Ada"})
        welcome = ada.wait_for("welcome")
        assert welcome["name"] == "Ada" and welcome["v"] == 2, welcome
        ada.send({"t": "hello", "name": "Ada"})
        ada.send({"t": "create", "mode": "campaign"})
        room = ada.wait_for("room")
        code = room["code"]

        bo = WebSocket(port)
        bo.wait_for("welcome")
        bo.send({"t": "hello", "name": "Bo"})
        bo.send({"t": "join", "code": code})
        joined = bo.wait_for("room")
        assert joined["code"] == code and len(joined["members"]) == 2, joined

        ada.send({"t": "start"})
        level = bo.wait_for("level")
        assert level["level"] == 1 and level["gates"], level
        snapshot = bo.next_snapshot()
        assert len(snapshot["cannons"]) == 2 and len(snapshot["gates"]) == len(level["gates"])

        # After the countdown Ada fires; her mobs show up in Bo's snapshots.
        time.sleep(3.2)
        for _ in range(10):
            ada.send({"t": "in", "x": 12, "f": True})
            time.sleep(0.1)
        # Bo's socket holds a backlog from the sleep: read until play shows.
        blue_mobs = 0
        for _ in range(200):
            snapshot = bo.next_snapshot()
            if snapshot["phase"] == 2:
                blue_mobs = max(blue_mobs, sum(1 for mob in snapshot["mobs"] if mob[3] & 1 == 0))
            if blue_mobs > 0:
                break
        assert blue_mobs > 0, "no blue mobs after firing"

        # A versus room against a bot runs alongside.
        cy = WebSocket(port)
        cy.wait_for("welcome")
        cy.send({"t": "create", "mode": "versus", "bots": True, "start": True})
        versus = cy.wait_for("level")
        assert versus["mode"] == "versus", versus
        assert len(cy.next_snapshot()["cannons"]) == 2

        status, body, _ = http_get(port, "/api/status")
        report = json.loads(body)
        assert report["rooms"] == 2 and report["clients"] == 3, report

        for client in (ada, bo, cy):
            client.close()
    finally:
        server.send_signal(signal.SIGTERM)
        try:
            code = server.wait(timeout=5)
        except subprocess.TimeoutExpired:
            server.kill()
            raise
    assert code == 0, f"server exited {code}"
    print("server_e2e: ok")


if __name__ == "__main__":
    main()
