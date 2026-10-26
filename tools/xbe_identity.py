"""Bounded public XBE certificate metadata, independent of disc/section parsing."""
import io
from pathlib import Path
import struct


def _read(source):
    source.seek(0, 2)
    length = source.tell()
    source.seek(0)
    header = source.read(0x178)
    if len(header) != 0x178 or header[:4] != b'XBEH':
        raise ValueError('Not an XBE image')
    base, headers = struct.unpack_from('<II', header, 0x104)
    cert = struct.unpack_from('<I', header, 0x118)[0] - base
    if not 0x178 <= headers <= length or cert < 0x178 or cert + 0x5c > headers:
        raise ValueError('XBE certificate identity is outside headers')
    source.seek(cert)
    public = source.read(0x5c)
    if len(public) != 0x5c:
        raise ValueError('XBE certificate identity is truncated')
    size, _, title_id = struct.unpack_from('<III', public)
    if size < 0x5c or cert + size > headers:
        raise ValueError('XBE certificate extent is invalid')
    units = struct.unpack_from('<40H', public, 0x0c)
    end = next((i for i, unit in enumerate(units) if unit == 0), 40)
    name = public[0x0c:0x0c + end * 2].decode('utf-16le', errors='replace')
    # Certificate text is display data. Keep controls out of logs and messages.
    def display(char):
        cp = ord(char)
        hidden = (cp < 32 or 0x7f <= cp <= 0x9f or cp in (0xad, 0x61c, 0x180e, 0xfeff) or
                  0x200b <= cp <= 0x200f or 0x2028 <= cp <= 0x202e or 0x2060 <= cp <= 0x206f or
                  0xfff9 <= cp <= 0xfffb or 0xe0000 <= cp <= 0xe007f)
        return ' ' if hidden else char
    name = ''.join(map(display, name)).strip(' ')
    return dict(title_id=title_id, title_name=name)


def parse_identity(data):
    return _read(io.BytesIO(data))


def read_identity(path):
    with Path(path).open('rb') as source:
        return _read(source)


def title_id_label(title_id):
    publisher = bytes((title_id >> 24, (title_id >> 16) & 255))
    if all(65 <= char <= 90 for char in publisher):
        return f'{publisher.decode("ascii")}-{title_id & 0xffff:03d} (0x{title_id:08X})'
    return f'0x{title_id:08X}'


def unsupported_image_message(path, title):
    try:
        identity = read_identity(path)
    except (OSError, ValueError) as error:
        return f'This Xbox executable is not a supported original revision. Cannot read its title identity: {error}.'
    return unsupported_identity_message(identity, title)


def unsupported_identity_message(identity, title):
    label = title_id_label(identity['title_id'])
    selected = f'{identity["title_name"] or "Unnamed title"} ({label})'
    expected = title.get('game_id', '')
    if expected and identity['title_id'] != int(expected, 16):
        return f'Wrong game: selected {selected}. Expected {title.get("name", "title")} ({title_id_label(int(expected, 16))}).'
    return f'This Xbox executable is not a supported original revision. Selected {selected}; choose a supported original revision.'
