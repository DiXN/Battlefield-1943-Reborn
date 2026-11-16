"""A stand in for the Discord client's local IPC, for testing the game's Rich Presence (bf/src/discord_presence.cpp).

It listens where the game looks (a named pipe on Windows, a Unix socket on Linux), answers the handshake with a
READY dispatch and each command as Discord does, and writes every frame the game sends to a JSON lines file, one
object a frame: {"t": seconds since the start, "op": opcode, "json": the payload as parsed}.

A dev build finds it with BF_DEV_DISCORD_IPC=<the same name> (a running Discord holds discord-ipc-0):
    python tools/fake_discord_ipc.py --name bf-fake-discord --log runs/discord1/frames.jsonl
    BF_DEV_DISCORD_IPC=bf-fake-discord  (in the game's environment)

--refuse answers the handshake with Discord's close for an unknown application (code 4000).
--restart-after S closes the connection S seconds after each handshake and does not listen for --away seconds (a
Discord restart): the game should come back by itself, quietly.
Standard library only (ctypes for the Windows pipe).
"""

import argparse
import json
import os
import socket
import struct
import sys
import threading
import time

START = time.monotonic()


def now():
    return round(time.monotonic() - START, 3)


class Recorder:
    def __init__(self, path):
        self.path = path
        self.lock = threading.Lock()
        if path:
            os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
            open(path, "w").close()

    def note(self, **entry):
        entry = {"t": now(), **entry}
        line = json.dumps(entry)
        print(line, flush=True)
        if self.path:
            with self.lock, open(self.path, "a", encoding="utf-8") as f:
                f.write(line + "\n")


# ---- the transports: read(n) gives exactly n bytes or raises EOFError; write(b); close()

if sys.platform == "win32":
    import ctypes
    from ctypes import wintypes

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateNamedPipeW.restype = wintypes.HANDLE
    k32.CreateNamedPipeW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD,
                                     wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID]
    k32.ConnectNamedPipe.argtypes = [wintypes.HANDLE, wintypes.LPVOID]
    k32.ReadFile.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
    k32.WriteFile.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
    k32.PeekNamedPipe.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.DWORD, wintypes.LPVOID,
                                  ctypes.POINTER(wintypes.DWORD), wintypes.LPVOID]
    k32.DisconnectNamedPipe.argtypes = [wintypes.HANDLE]
    k32.CloseHandle.argtypes = [wintypes.HANDLE]
    INVALID = wintypes.HANDLE(-1).value

    class Pipe:
        def __init__(self, handle):
            self.h = handle
            self.deadline = None  # a restart: reads end here (polled, so nothing blocks past it)

        def read(self, n):
            out = b""
            while len(out) < n:
                avail = wintypes.DWORD(0)
                if not k32.PeekNamedPipe(self.h, None, 0, None, ctypes.byref(avail), None):
                    raise EOFError
                if avail.value == 0:
                    if self.deadline is not None and time.monotonic() >= self.deadline:
                        raise EOFError
                    time.sleep(0.02)
                    continue
                buf = ctypes.create_string_buffer(n - len(out))
                got = wintypes.DWORD(0)
                if not k32.ReadFile(self.h, buf, n - len(out), ctypes.byref(got), None) or got.value == 0:
                    raise EOFError
                out += buf.raw[: got.value]
            return out

        def write(self, b):
            done = wintypes.DWORD(0)
            if not k32.WriteFile(self.h, b, len(b), ctypes.byref(done), None):
                raise EOFError

        def close(self):
            k32.DisconnectNamedPipe(self.h)
            k32.CloseHandle(self.h)

    def accept(name):
        h = k32.CreateNamedPipeW("\\\\.\\pipe\\" + name, 3, 0, 1, 65536, 65536, 0, None)  # duplex, byte mode, waits
        if h == INVALID or h is None:
            raise OSError(f"CreateNamedPipe failed: {ctypes.get_last_error()}")
        if not k32.ConnectNamedPipe(h, None) and ctypes.get_last_error() != 535:  # ERROR_PIPE_CONNECTED
            k32.CloseHandle(h)
            raise OSError(f"ConnectNamedPipe failed: {ctypes.get_last_error()}")
        return Pipe(h)

    def listen(name):
        return name
else:
    class Sock:
        def __init__(self, s):
            self.s = s
            self.deadline = None

        def read(self, n):
            out = b""
            while len(out) < n:
                if self.deadline is not None:
                    left = self.deadline - time.monotonic()
                    if left <= 0:
                        raise EOFError
                    self.s.settimeout(left)
                try:
                    b = self.s.recv(n - len(out))
                except socket.timeout:
                    raise EOFError
                if not b:
                    raise EOFError
                out += b
            return out

        def write(self, b):
            self.s.sendall(b)

        def close(self):
            self.s.close()

    def listen(name):
        if os.path.exists(name):
            os.unlink(name)
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(name)
        srv.listen(1)
        return srv

    def accept(srv):
        c, _ = srv.accept()
        return Sock(c)


def frame(op, obj):
    data = json.dumps(obj).encode()
    return struct.pack("<II", op, len(data)) + data


def serve(conn, rec, args):
    opened = time.monotonic()
    while True:
        try:
            op, n = struct.unpack("<II", conn.read(8))
            payload = conn.read(n)
        except (EOFError, OSError):
            if conn.deadline is not None and time.monotonic() >= conn.deadline:
                rec.note(event="restart: closing")
            else:
                rec.note(event="disconnected")
            break
        try:
            obj = json.loads(payload)
        except ValueError:
            obj = {"raw": payload.decode(errors="replace")}
        rec.note(op=op, json=obj)
        if op == 0:
            if args.refuse:
                conn.write(frame(2, {"code": 4000, "message": "Invalid Client ID"}))
                rec.note(event="refused")
                break
            conn.write(frame(1, {"cmd": "DISPATCH", "evt": "READY", "data": {"v": 1, "user": {"id": "0", "username": "fake"}}}))
            if args.restart_after > 0:
                conn.deadline = time.monotonic() + args.restart_after
        elif op == 1:
            if args.reject_activity:
                conn.write(frame(1, {"cmd": obj.get("cmd"), "evt": "ERROR", "nonce": obj.get("nonce"),
                                     "data": {"code": 4000, "message": "fake error"}}))
            else:
                conn.write(frame(1, {"cmd": obj.get("cmd"), "data": obj.get("args", {}).get("activity"),
                                     "evt": None, "nonce": obj.get("nonce")}))
        elif op == 3:
            conn.write(frame(4, obj))
        elif op == 2:
            break
    try:
        conn.close()
    except OSError:
        pass
    return time.monotonic() - opened


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", default="bf-fake-discord", help="pipe name (Windows) or socket path (Linux)")
    ap.add_argument("--log", default="")
    ap.add_argument("--refuse", action="store_true")
    ap.add_argument("--reject-activity", action="store_true")
    ap.add_argument("--restart-after", type=float, default=0)
    ap.add_argument("--away", type=float, default=20)
    ap.add_argument("--quit-after", type=float, default=0, help="stop after this many seconds (0: never)")
    args = ap.parse_args()
    rec = Recorder(args.log)
    if args.quit_after > 0:
        threading.Timer(args.quit_after, lambda: os._exit(0)).start()
    srv = listen(args.name)
    rec.note(event="listening", name=args.name)
    while True:
        conn = accept(srv)
        rec.note(event="connected")
        serve(conn, rec, args)
        if args.restart_after > 0:
            rec.note(event=f"away for {args.away} s")
            if sys.platform != "win32":
                srv.close()
                os.unlink(args.name)
            time.sleep(args.away)
            if sys.platform != "win32":
                srv = listen(args.name)
            rec.note(event="back")


if __name__ == "__main__":
    main()
