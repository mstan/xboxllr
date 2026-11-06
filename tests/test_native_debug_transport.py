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
                        'resize 0 720', 'resize 8192 8192', 'resize 1680 -1', 'resize 1680 720 extra',
                        'connect_input 0','connect_input 2','connect_input 16','connect_input -1',
                        'set_input_port 4 '+'00'*18+' 100','set_input_port -1 '+'00'*18+' 100'):
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
        assert request(port,'status')['connected_mask']==1
        request(port,'connect_input 3')
        assert request(port,'status')['connected_mask']==3
        request(port,'set_input_port 1 '+gamepad('b').hex()+' 200')
        deadline=time.monotonic()+1
        while request(port,'status')['delivered_sequence']!=2:
            assert time.monotonic()<deadline
            time.sleep(.005)
        request(port,'connect_input 1')
        assert request(port,'status')['connected_mask']==1
        assert not request(port,'status')['input_active']
        shot = request(port, "screenshot")
        pixels = base64.b64decode(shot["data"], validate=True)
        assert shot["width"] == 640 and shot["height"] == 480 and len(pixels) == 640*480*4
        for x, y in ((0, 0), (639, 479), (255, 256)):
            assert pixels[(y*640+x)*4:(y*640+x)*4+4] == bytes((x&255, y&255, 0x65, 255))
        request(port, "clear_input")
        request(port,'pause 69632')
        snapshot=request(port,'guest_snapshot');epoch=snapshot['snapshot']
        assert snapshot['pc']==0x11000 and snapshot['registers']['eax']==42
        assert snapshot['xbe_sha256']=='a'*64 and snapshot['entry_sp']==0x20000
        assert snapshot['xmm'][0].startswith('0000803e')
        before=request(port,'status')['frames']
        assert request(port,f'read_guest {epoch} 73728 4')['data']=='2a000000'
        assert request(port,'guest_snapshot')==snapshot
        assert request(port,'status')['frames']==before
        for command in (f'read_guest {epoch} 73728 4097',f'read_guest {epoch} 4294967295 4',
                        f'read_guest {epoch} 4244635648 4',f'read_guest {epoch+1} 73728 4','pause 70000'):
            try:
                request(port,command);raise AssertionError('invalid/unsafe guest read accepted')
            except ValueError:pass
        request(port,'resume')
        try:
            request(port,f'read_guest {epoch} 73728 4');raise AssertionError('expired snapshot accepted')
        except ValueError:pass
        # A second pause immediately after resume must establish a new epoch,
        # rather than stranding the owner in the previous pause wait.
        request(port,'pause');assert request(port,'guest_snapshot')['snapshot']!=epoch
        request(port,'resume')
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
    with socket.socket() as temporary:
        temporary.bind(('127.0.0.1',0));port=temporary.getsockname()[1]
    child=subprocess.Popen([sys.argv[1],str(port),'incoherent'],stdin=subprocess.PIPE,
                           stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        deadline=time.monotonic()+5
        while True:
            try:request(port,'status');break
            except OSError:
                if time.monotonic()>deadline or child.poll() is not None:raise
                time.sleep(.02)
        try:
            request(port,'pause');raise AssertionError('uncertified runtime snapshot accepted')
        except ValueError as error:assert 'coherent' in str(error)
        _,errors=child.communicate(b'quit\n',timeout=5)
        assert child.returncode==0,errors.decode(errors='replace')
        print('PASS guest inspection transport: coherent frozen epoch, bounded reads, stale-token/MMIO rejection, resume/re-pause and unsafe-profile refusal')
    finally:
        if child.poll() is None:child.kill();child.communicate()


if __name__ == "__main__":
    main()
