"""Bounded native-launcher image handoff and additive save publication.

Pixels are decoded by recomp-ui's private stb_image. No image package or
extra Python runtime dependency is needed in portable game releases.
"""
import json
import os
from pathlib import Path
import re
import struct
import uuid


def service(folder, handler):
    request=folder/'request'
    if not request.exists():return False
    claimed=folder/'request.processing'
    request.rename(claimed)
    try:
        if claimed.stat().st_size>32768:raise ValueError("Oversized image request")
        lines=claimed.read_text(encoding='ascii').splitlines()
        if not lines or lines[0]!='xboxllr-image-import 1' or lines[-1]!='end':raise ValueError("Invalid image request")
        fields={}
        for line in lines[1:-1]:
            k,v=line.split(' ',1)
            if k in fields:raise ValueError("Duplicate image request field")
            fields[k]=v
        action=fields.get('action')
        expected=({'action','game'} if action=='inspect' else {'action','game','slot','sha'} if action in ('load','delete') else {'action','game','source','size','fit','width','height'})
        if action=='install' and 'density' in fields:expected.add('density')
        if action not in ('inspect','install','load','delete') or set(fields)!=expected:raise ValueError("Incomplete image request")
        for k in ('game','source'):
            if k not in fields:continue
            fields[k]=bytes.fromhex(fields[k]).decode('utf-8')
            if not fields[k] or '\0' in fields[k] or len(fields[k])>4095:raise ValueError("Invalid image filename")
        if action in ('load','delete'):
            fields['slot']=int(fields['slot'])
            if not 0<=fields['slot']<28 or not re.fullmatch('[0-9a-f]{64}',fields['sha']):raise ValueError('Invalid saved design selection')
        fields['_bridge']=folder
        if action=='install':
            fields['density']=int(fields.get('density','1'))
            if fields['density'] not in (1,2,4):raise ValueError('Invalid image detail')
            for k in ('size','fit','width','height'):fields[k]=int(fields[k])
            w,h=fields['width'],fields['height']
            if not 1<=w<=8192 or not 1<=h<=8192 or w*h>16777216 or not 0<=fields['size']<5 or not 0<=fields['fit']<3:raise ValueError("Invalid image dimensions or size")
            pixels=folder/'pixels.rgba'
            if pixels.is_symlink() or pixels.stat().st_size!=w*h*4:raise ValueError("Incomplete image pixels")
            result=handler(fields,pixels.read_bytes())
        else:result=handler(fields,None)
    except (OSError,ValueError,KeyError,RuntimeError) as error:
        result=dict(status='Graffiti operation failed: '+str(error),capacity='',slots=-1,locked=False)
    finally:
        claimed.unlink(missing_ok=True)
        (folder/'pixels.rgba').unlink(missing_ok=True)
    temporary=folder/'response.tmp'
    lines=['xboxllr-image-result 1','status '+result['status'].encode('utf-8').hex(),
           'capacity '+(result['capacity'].encode('utf-8').hex() or '-'),
           'slots '+str(result['slots']),'locked '+str(int(result['locked'])),
           'gallery '+str(int(result.get('gallery',False))),'preview '+str(result.get('preview',-1)),'end']
    temporary.write_text('\n'.join(lines)+'\n',encoding='ascii',newline='\n')
    temporary.replace(folder/'response')
    return True


def _ordinary(path):
    if path.is_symlink() or getattr(path,'is_junction',lambda:False)():
        raise ValueError("Save location contains a link. Existing saves were preserved.")


def _save_name(name):
    if not isinstance(name,str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,42}',name):raise ValueError('Invalid save directory name')


def _save_file(name):
    if not isinstance(name,str) or not re.fullmatch(r'[A-Za-z0-9_-][A-Za-z0-9_.-]{0,41}',name):raise ValueError('Invalid save filename')


def _bridge_file(folder,name,data):
    temporary=folder/(name+'.tmp')
    with temporary.open('wb') as out:out.write(data);out.flush();os.fsync(out.fileno())
    temporary.replace(folder/name)


def gallery(folder,codec,store,key,used):
    """Bounded, decoded thumbnails of actual native saves for the UI bridge."""
    rows=[]
    for slot in sorted(used):
        try:design=codec.saved_design(store,slot,key)
        except (OSError,ValueError):continue
        if design is None:continue
        w,h=design['width'],design['height'];tw=th=0;pixels=b''
        if design['rgba'] is not None:
            scale=min(64/w,64/h);tw=max(1,round(w*scale));th=max(1,round(h*scale))
            pixels=codec.resize(design['rgba'],w,h,tw,th)
        rows.append(struct.pack('<8I',slot,design['size'],design['density'],w,h,tw,th,int(design.get('deletable',False)))+
                    bytes.fromhex(design['sha'])+pixels)
    _bridge_file(folder,'gallery.bin',b'XLLEGA1\0'+struct.pack('<I',len(rows))+b''.join(rows))


def preview(folder,design):
    if design['rgba'] is None:raise ValueError('This design could not be decoded with the selected console identity')
    _bridge_file(folder,'preview.bin',b'XLLEGP1\0'+struct.pack('<3I',design['slot'],design['width'],design['height'])+
                 bytes.fromhex(design['sha'])+design['rgba'])


class FolderSaves:
    def __init__(self,state,title_id):
        self.state=Path(state).resolve()
        if not re.fullmatch(r'[0-9a-fA-F]{8}',title_id):raise ValueError('Invalid save title ID')
        self.root=self.state/'Partition1/UDATA'/title_id.lower()
        current=self.state
        _ordinary(current)
        for part in ('Partition1','UDATA',title_id.lower()):
            current=current/part;_ordinary(current)
            if current.exists() and not current.is_dir():raise ValueError("Invalid native save folder")
        self.root.mkdir(parents=True,exist_ok=True)

    def available_bytes(self):
        import shutil
        return shutil.disk_usage(self.state).free

    def names(self):
        entries=list(self.root.iterdir())
        if len(entries)>4096:raise ValueError("Too many save entries to inspect safely")
        for entry in entries:_ordinary(entry)
        return [p.name for p in entries if p.is_dir()]

    def read(self,name,file,limit):
        _save_name(name);_save_file(file)
        path=self.root/name/file;_ordinary(self.root/name);_ordinary(path)
        if not path.exists():return None
        if not path.is_file() or path.stat().st_size>limit:raise ValueError("Unrecognized existing graffiti save; nothing was replaced")
        return path.read_bytes()

    def entries(self,name):
        _save_name(name)
        directory=self.root/name;_ordinary(directory)
        rows=list(directory.iterdir())
        if len(rows)>16:raise ValueError('Unexpected extra files in this design; nothing was deleted')
        for path in rows:
            _ordinary(path)
            if not path.is_file():raise ValueError('Unexpected directory in this design; nothing was deleted')
        return [p.name for p in rows]

    def delete(self,name,expected_sha):
        import hashlib
        _save_name(name)
        if not re.fullmatch('[0-9a-f]{64}',expected_sha):raise ValueError('Invalid saved design digest')
        allowed={'save.dat','SaveMeta.xbx','SaveImage.xbx','xboxllr-hd.bin'}
        files=self.entries(name)
        if 'save.dat' not in files or not set(files)<=allowed or hashlib.sha256(self.read(name,'save.dat',2*1024*1024) or b'').hexdigest()!=expected_sha:
            raise ValueError('The saved design changed or contains extra files. Refresh before deleting.')
        source=self.root/name;trash=self.state/('graffiti-delete-'+uuid.uuid4().hex)
        source.rename(trash)
        # The selected directory is first unpublished atomically. If storage
        # fails during removal, retain its clearly named recovery directory.
        for file in files:(trash/file).unlink()
        trash.rmdir()

    def publish(self,files,destination_name=None):
        # Staging is outside UDATA, so enumeration can never see a half save.
        destination_name=destination_name or uuid.uuid4().hex[:12].upper()
        if not re.fullmatch(r'[A-Za-z0-9_-]{1,42}',destination_name):raise ValueError('Invalid save directory name')
        temporary=self.state/('graffiti-import-'+uuid.uuid4().hex)
        temporary.mkdir();created=[]
        try:
            for name,content in files.items():
                if name not in ('save.dat','SaveMeta.xbx','SaveImage.xbx','xboxllr-hd.bin'):raise ValueError("Invalid native save filename")
                path=temporary/name
                with path.open('xb') as out:
                    created.append(path);out.write(content);out.flush();os.fsync(out.fileno())
                if path.read_bytes()!=content:raise OSError("Native graffiti readback failed")
            destination=self.root/destination_name
            if destination.exists():raise FileExistsError("Native save destination already exists")
            temporary.rename(destination)
            return destination.name
        finally:
            if temporary.exists():
                for path in created:path.unlink()
                temporary.rmdir()
