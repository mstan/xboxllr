"""Build an authored 4x fixture and verify native GPU replacement/fallback."""
import argparse,os,subprocess,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_texture_replacements import prepare

parser=argparse.ArgumentParser();parser.add_argument('--binary',required=True);args=parser.parse_args()
raw=bytes([255]*8+[0,248,0,0,0,0,0,0])
rgba=b''.join(bytes((0,255 if x&1 else 0,0 if x&1 else 255,255)) for y in range(16) for x in range(16))
with tempfile.TemporaryDirectory(prefix='xboxllr-hd-gpu-') as temporary:
    wide=b''.join(bytes((0,255 if x&1 else 0,0 if x&1 else 255,255)) for y in range(16) for x in range(128))
    index=prepare(temporary,[dict(format=0xe,width=4,height=4,source=raw,hd_width=16,hd_height=16,rgba=rgba),
        dict(format=0x11,width=32,height=4,source=b'\0\xf8'*128,hd_width=128,hd_height=16,rgba=wide),
        dict(format=0x12,width=4,height=4,source=b''.join(bytes((i*7,i*11,i*13,255)) for i in range(16)),hd_width=16,hd_height=16,rgba=rgba)])
    env=dict(os.environ,XLLE_PILOT_TEXTURE_REPLACEMENTS=str(index),XLLE_PILOT_VISIBLE='0')
    subprocess.run([args.binary,'--hires'],env=env,check=True)
