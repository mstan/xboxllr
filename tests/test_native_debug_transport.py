"""Real TCP/mailbox fixture: inputs, lease expiry, partial lines and large shots."""
import base64
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from native_debug import gamepad, request


def main():
    with socket.socket() as temporary:
        temporary.bind(("127.0.0.1", 0))
        port = temporary.getsockname()[1]
    child = subprocess.Popen([sys.argv[1], str(port)], stdin=subprocess.PIPE,
                             stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        deadline = time.monotonic() + 5
        while True:
            try:
                assert request(port, "status")["input_sequence"] == 0
                break
            except OSError:
                if time.monotonic() > deadline or child.poll() is not None:
                    raise
                time.sleep(.02)
        with socket.socket() as duplicate:
            duplicate.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            try:
                duplicate.bind(("127.0.0.1", port))
                raise AssertionError("another process could take the debug endpoint")
            except OSError:
                pass
        with socket.create_connection(("127.0.0.1", port), 5) as connection:
            connection.sendall(b"sta")
            connection.sendall(b"tus\nping\nstatus\n")
            with connection.makefile("rb") as response:
                assert json.loads(response.readline())["ok"]
                assert response.readline() == b"pong\n"
                assert json.loads(response.readline())["ok"]
        for command in ("set_input 00 100", "set_input " + "00"*18 + " 5001",
                        "set_input " + "0001" + "00"*16 + " 100", "screenshot extra",
                        'resize 0 720', 'resize 8192 8192', 'resize 1680 -1', 'resize 1680 720 extra'):
            try:
                request(port, command)
                raise AssertionError("invalid command accepted")
            except ValueError:
                pass
        pad = gamepad("up,a", -12345, 12345)
        assert request(port, 'resize 1680 720')['queued']
        assert pad == struct.pack("<H8Bhhhh", 1, 255, 0, 0, 0, 0, 0, 0, 0, -12345, 12345, 0, 0)
        assert request(port, f"set_input {pad.hex()} 250")["sequence"] == 1
        deadline = time.monotonic() + 1
        while request(port, "status")["delivered_sequence"] != 1:
            assert time.monotonic() < deadline
            time.sleep(.005)
        time.sleep(.27)
        assert not request(port, "status")["input_active"]
        shot = request(port, "screenshot")
        pixels = base64.b64decode(shot["data"], validate=True)
        assert shot["width"] == 640 and shot["height"] == 480 and len(pixels) == 640*480*4
        for x, y in ((0, 0), (639, 479), (255, 256)):
            assert pixels[(y*640+x)*4:(y*640+x)*4+4] == bytes((x&255, y&255, 0x65, 255))
        request(port, "clear_input")
        # Keep an idle client connected during destruction: server stop must
        # join promptly instead of hanging in a blocking receive.
        with socket.create_connection(("127.0.0.1", port), 5):
            _, errors = child.communicate(b"quit\n", timeout=5)
        assert child.returncode == 0, errors.decode(errors="replace")
        print("PASS native TCP: disabled startup, fragmented/batched commands, real signed-stick/button delivery, invalid-input guards, expiry, 1.6MB screenshot response, idle-client shutdown")
    finally:
        if child.poll() is None:
            child.kill()
            child.communicate()


if __name__ == "__main__":
    main()
