"""Control a native title's opt-in localhost TCP input/screenshot endpoint."""
import argparse
import base64
import json
from pathlib import Path
import socket
import struct
import zlib


def request(port, command, timeout=5):
    with socket.create_connection(("127.0.0.1", port), timeout) as connection:
        connection.sendall((command + "\n").encode("ascii"))
        with connection.makefile("rb") as response:
            line = response.readline(96 * 1024 * 1024)
    if not line.endswith(b"\n"):
        raise ValueError("incomplete debug response")
    result = json.loads(line)
    if not result.get("ok"):
        raise ValueError(result.get("error", "debug command failed"))
    return result


def gamepad(buttons="", lx=0, ly=0, rx=0, ry=0):
    digital = {"up": 1, "down": 2, "left": 4, "right": 8, "start": 16,
               "back": 32, "lstick": 64, "rstick": 128}
    analog = {"a": 2, "b": 3, "x": 4, "y": 5, "black": 6, "white": 7,
              "lt": 8, "rt": 9}
    pad = bytearray(18)
    for name in filter(None, buttons.lower().split(",")):
        if name in digital:
            pad[0] |= digital[name]
        elif name in analog:
            pad[analog[name]] = 255
        else:
            raise ValueError("unknown Xbox button: " + name)
    if any(type(v) is not int or not -32768 <= v <= 32767 for v in (lx, ly, rx, ry)):
        raise ValueError("stick values must be signed 16-bit integers")
    struct.pack_into("<hhhh", pad, 10, lx, ly, rx, ry)
    return pad


def save_png(result, path):
    width, height = result["width"], result["height"]
    if result.get("format") != "rgba8" or not 0 < width * height <= 16 * 1024 * 1024:
        raise ValueError("unsupported screenshot shape/format")
    rgba = base64.b64decode(result["data"], validate=True)
    if len(rgba) != width * height * 4:
        raise ValueError("incomplete screenshot pixels")
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    rows = b"".join(b"\0" + rgba[y*width*4:(y+1)*width*4] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    Path(path).write_bytes(png + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
    return {k: v for k, v in result.items() if k != "data"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("status")
    commands.add_parser("clear_input")
    connect=commands.add_parser('connect_input',help='Connect opt-in virtual controllers; bit0 is Player1')
    connect.add_argument('mask',type=lambda value:int(value,0))
    resize = commands.add_parser('resize')
    resize.add_argument('width', type=int)
    resize.add_argument('height', type=int)
    press = commands.add_parser("press")
    press.add_argument("buttons", help="Comma-separated Xbox names, e.g. a,down")
    press.add_argument("--ms", type=int, default=150)
    press.add_argument('--player',type=int,choices=range(1,5),default=1)
    for name in ("lx", "ly", "rx", "ry"):
        press.add_argument("--"+name, type=int, default=0)
    shot = commands.add_parser("screenshot")
    shot.add_argument("path", type=Path)
    args = parser.parse_args()
    if args.command == "press":
        if not 1 <= args.ms <= 5000:
            parser.error("--ms must be in 1..5000")
        pad = gamepad(args.buttons, args.lx, args.ly, args.rx, args.ry)
        result = request(args.port, f"set_input {pad.hex()} {args.ms}" if args.player==1 else f"set_input_port {args.player-1} {pad.hex()} {args.ms}")
    elif args.command=='connect_input':
        result=request(args.port,'connect_input '+str(args.mask))
    elif args.command == 'resize':
        result = request(args.port, f'resize {args.width} {args.height}')
    else:
        result = request(args.port, args.command)
        if args.command == "screenshot":
            result = save_png(result, args.path)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
