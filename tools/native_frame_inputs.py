#!/usr/bin/env python3
"""Verify compact native-frame input coverage and target dependency linkage.

Assets, supplied initial targets and later observations remain distinct. This
checks capture integrity and complete input coverage, never correct pixels.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct

from native_frame_audit import audit as ledger_audit, DRAW_NAMES, TARGET_NAMES, word

MAGIC=0x49464c58
MAX_BYTES=1024*1024*1024
MAX_ASSET_BYTES=64*1024*1024
FORMAT_BYTES={0x12:4,0x22:8,0x32:12,0x42:16,0x40:4,0x16:4,
              0x15:2,0x25:4,0x45:8,0x11:2,0x21:4,0x41:8,0x72:12}


def require(condition,message):
    if not condition:raise ValueError(message)


def fvf_extent(value):
    textures=(value>>8)&15
    require(value&15==4 and not value&0xf030 and textures<=4,'Unsupported captured FVF layout')
    require(not ((value>>16)>>(textures*2)),'FVF coordinate field has no coordinate')
    return 16+4*bool(value&0x40)+4*bool(value&0x80)+sum(
        ((((value>>(16+i*2))+1)&3)+1)*4 for i in range(textures))


class Reader:
    def __init__(self,path):
        self.path=Path(path);self.assets={};self.small={};self.targets={};self.draws=[]
        self.events=[];self.root=None;self.footer=None;self.current=[0,0];self.guests=[0,0]
        self.producers=0;self.last_call=0

    def asset(self,number,size=None):
        require(number in self.assets,'Missing/forward asset reference')
        result=self.assets[number]
        if size is not None:require(result['bytes']==size,'Asset extent disagrees with consumer')
        return result

    def words(self,number,size):
        self.asset(number,size)
        require(number in self.small and size%4==0,'Metadata asset is not bounded DWORDs')
        return struct.unpack('<'+'I'*(size//4),self.small[number])

    def target(self,number,role):
        if not number:return None
        require(number in self.targets and self.targets[number]['role']==role,'Unknown/wrong target role')
        return self.targets[number]

    def image(self,asset,target):
        self.asset(asset,target['width']*target['height']*4)

    def texture(self,number):
        if not number:return
        extent=self.asset(number)['bytes']
        require(extent>=108 and extent<=540 and extent%4==0,'Texture metadata extent')
        w=self.words(number,extent)
        require(w[0] and 0<w[9]<=4096 and 0<w[10]<=4096 and 0<w[11]<=13 and w[6] in (0,1),
                'Invalid texture ownership/shape')
        require(extent==(18+w[11]*9)*4 and w[16]+w[11]<=13 and w[17]==0,'Incomplete texture mip records')
        self.asset(w[12],20)
        if w[6]:
            require(w[13]==0 and w[14]==0 and w[16]==0 and w[11]==1 and w[8]==0x12 and w[7]==1,
                    'Invalid GPU storage alias')
            require(w[2] in (1,2) and w[1] and (w[2]!=1 or w[3]<2),'Unknown GPU alias source')
            key=(0xfffffffd if w[3]==0 else 0xfffffffc) if w[2]==1 else w[3]
            require(w[15]==key,'GPU alias key names another storage')
        else:
            self.asset(w[13])
            require(w[14]==0 and w[16]==0 and w[1]==w[15] and not any(w[2:6]),
                    'Unqualified sampled view or altered texture storage identity')
        for level in range(w[11]):
            m=w[18+level*9:27+level*9]
            width,height=max(1,w[9]>>level),max(1,w[10]>>level)
            block=8 if w[8]==12 else 16 if w[8] in (14,15) else 0
            yuv=w[8] in (0x24,0x25)
            pitch=((width+3)//4)*block if block else width*(4 if yuv or w[8]>5 else 2)
            rows=(height+3)//4 if block else height
            require(m[:7]==(level,width,height,pitch,rows,pitch*rows,int(yuv)),'Mip shape/encoding mismatch')
            self.asset(m[7],m[5])
            known=next((t['id'] for t in self.targets.values() if
                        (t['key'],t['level'])==(w[15],w[16]+level)),0)
            require(m[8]==known,'Missing/changed texture producer link')
            if m[8]:
                require(m[8] in self.targets,'Texture producer has no prior target root')
                target=self.targets[m[8]]
                require((target['key'],target['level'],target['width'],target['height'])==
                        (w[15],w[16]+level,width,height),'Texture producer names another subresource')
                self.producers+=1

    def draw(self,w):
        require(len(w)==130 and w[0]==len(self.draws)+1,'Missing/reused draw input')
        number,call,kind,primitive,first,count,shader,up,stride,color,depth=w[:11]
        require(self.root is not None and call>0 and kind<4 and kind==(0 if shader&1 else 2)+bool(up),
                'Wrong draw kind or missing frame root')
        require(primitive in (5,6,8) and 3<=count<=65536 and first+count<=0xffffffff,'Invalid draw range')
        require((primitive!=5 or count%3==0) and (primitive!=8 or count%4==0),'Invalid primitive count')
        require([color,depth]==self.current,'Draw breaks target binding continuity')
        require(not up or first==0 and stride>0,'Invalid user-pointer range')
        require(up or stride==0,'Bound submission has user stride')
        self.target(color,0);self.target(depth,1)
        require(w[13] and w[14],'Empty viewport')
        for index,size in ((25,664),(26,512),(29,64)):self.asset(w[index],size)
        require(bool(w[27])==bool(w[28]),'Missing pixel program')
        if w[28]:self.asset(w[28],240)
        expected=[0]*16
        if shader&1:
            require(0<w[20]<=136 and w[24]==0,'Invalid program/descriptor combination')
            self.asset(w[21],w[20]*16);self.asset(w[22],3072)
            elements=self.words(w[23],192)
            for i in range(16):
                fmt,stream,offset=elements[i*3:i*3+3]
                if fmt==2:continue
                require(fmt in FORMAT_BYTES and stream<16,'Unknown captured declaration layout')
                expected[stream]=max(expected[stream],offset+FORMAT_BYTES[fmt])
        else:
            require(not any(w[20:24]),'FVF submission invents a programmable input')
            self.asset(w[24],0x114);expected[0]=fvf_extent(shader)
        if up:require(not any(expected[1:]) and stride>=expected[0],'UP requires exactly one sufficient stream')
        for i,extent in enumerate(expected):
            s=w[30+i*6:36+i*6]
            if not extent:require(not any(s),'Extra unconsumed stream');continue
            require(s[5]==extent and s[0]<=2048 and s[1]==(count-1)*s[0]+extent,'Incomplete consumed stream span')
            self.asset(s[2],s[1])
            require(s[4] and s[4]+s[1]<=0x100000000,'Invalid guest stream range')
            if up:require(s[0]==stride and s[3]==0 and s[4]==up,'UP stream aliases another source')
            else:require(s[3],'Bound stream has no resource')
        for texture in w[126:130]:self.texture(texture)
        self.draws.append(dict(input=number,call=call,kind=kind,primitive=primitive,first=first,count=count,
            shader=shader,up_data=up,up_stride=stride,target=color,depth=depth,guest_targets=list(self.guests),
            program_instructions=w[20],code_asset=w[21],constant_asset=w[22],element_asset=w[23],
            fvf_asset=w[24],render_asset=w[25],texture_state_asset=w[26],pixel_handle=w[27],
            pixel_asset=w[28],packed_asset=w[29],viewport=list(w[11:17]),screen=list(w[17:19]),depth_scale=w[19],
            streams=[list(w[30+i*6:36+i*6]) for i in range(16)],textures=list(w[126:130])))
        self.events.append(dict(type='draw',call=call,input=number))

    def read(self):
        require(32<=self.path.stat().st_size<=MAX_BYTES,'Container outside byte bound')
        digest=hashlib.sha256();records=0
        with self.path.open('rb') as file:
            header=file.read(32);digest.update(header);self.header=struct.unpack('<8I',header)
            require(self.header[:2]==(MAGIC,2) and 0<self.header[3]<=4096 and 0<self.header[4]<=4096
                    and self.header[7]==0,'Invalid input container header')
            while raw:=file.read(8):
                require(len(raw)==8 and self.footer is None,'Truncated record or data after footer')
                digest.update(raw);kind,size=struct.unpack('<2I',raw)
                require(kind in range(1,9) and 0<size<=MAX_ASSET_BYTES+40,'Invalid record type/extent')
                offset=file.tell();data=file.read(size);digest.update(data);records+=1
                require(len(data)==size and records<=200000,'Truncated/bounded record')
                if kind==1:
                    require(size>40,'Empty asset record');number,length=struct.unpack_from('<2I',data)
                    require(number==len(self.assets)+1 and number<=32768 and length==size-40,'Asset numbering/extent')
                    sha=hashlib.sha256(data[40:]).hexdigest()
                    require(data[8:40].hex()==sha,'Asset checksum mismatch')
                    self.assets[number]=dict(bytes=length,sha256=sha,offset=offset+40)
                    if length<=4096:self.small[number]=data[40:]
                    continue
                require(size%4==0,'Unaligned metadata record');w=struct.unpack('<'+'I'*(size//4),data)
                if kind==2:
                    require(len(w)==15 and w[0]==len(self.targets)+1 and w[0]<=64 and w[5]<2,'Target numbering/role')
                    require((w[1]==0)==(self.root is None),'Target root outside initialization/bind')
                    require(w[2] and w[3] and 0<w[6]<=4096 and 0<w[7]<=4096 and w[4]<13,'Invalid target subresource')
                    require(w[8] in ((44,45) if w[5] else (28,87)),'Unknown target encoding')
                    require(not any((t['key'],t['level'])==(w[3],w[4]) for t in self.targets.values()),'Target root supplied twice')
                    t=dict(id=w[0],call=w[1],header=w[2],key=w[3],level=w[4],role=w[5],width=w[6],height=w[7],
                           format=w[8],initial_asset=w[9],resource=list(w[10:]))
                    self.image(w[9],t);self.targets[w[0]]=t;call=w[1]
                    self.events.append(dict(type='target_root',call=call,target=w[0]))
                elif kind==3:
                    require(len(w)==3 and w[0] in self.targets and w[1]>0,'Invalid target observation')
                    t=self.targets[w[0]];self.image(w[2],t)
                    require(w[0]==self.current[t['role']],'Observation belongs to unbound target')
                    call=w[1];self.events.append(dict(type='target_observation',call=call,target=w[0],asset=w[2]))
                elif kind==4:self.draw(w);call=w[1]
                elif kind==5:
                    require(len(w)==9 and self.root is not None and list(w[7:])==self.current,'Clear breaks target continuity')
                    require(w[1:3]==(0,0) and not w[3]&~0xf3 and (w[3]&0xf0) in (0,0xf0),'Unsupported clear inputs')
                    call=w[0];self.events.append(dict(type='clear',call=call,args=list(w[1:7]),targets=list(w[7:])))
                elif kind==6:
                    require(len(w)==7 and self.root is not None,'Incomplete target bind')
                    for role in (0,1):
                        t=self.target(w[1+role],role)
                        require(bool(t)==bool(w[3+role])==bool(w[5+role]),'Target binding lacks resource identity')
                        if t:self.asset(w[5+role],20)
                    self.current=list(w[1:3]);self.guests=list(w[3:5]);call=w[0]
                    self.events.append(dict(type='bind',call=call,targets=self.current[:],guests=self.guests[:]))
                elif kind==7:
                    require(len(w)==2 and self.root is None,'Missing/duplicate frame root')
                    self.current=list(w);self.root=list(w)
                    for role in (0,1):
                        t=self.target(w[role],role);self.guests[role]=t['header'] if t else 0
                    require(self.guests==list(self.header[5:7]),'Root belongs to another frame target')
                    require(sorted(self.targets)==sorted(t for t in w if t),'Extra initial target roots')
                    call=0;self.events.append(dict(type='root',call=0,targets=list(w)))
                else:
                    require(len(w)==4 and self.root is not None and w[1:]==(len(self.draws),len(self.assets),len(self.targets)),
                            'Footer counts disagree with inputs')
                    self.footer=w;call=w[0]+1
                require(call>=self.last_call,'Out-of-order input events');self.last_call=call
        require(self.footer is not None,'Missing complete input footer');self.sha256=digest.hexdigest()
        return self


def audit(ledger,inputs=None):
    ledger=Path(ledger);accounting=ledger_audit(ledger)
    rows=[json.loads(line) for line in ledger.read_bytes().splitlines()];begin,end=rows[0],rows[-1]
    reader=Reader(inputs or ledger.with_name('frame-inputs.bin')).read()
    require(reader.header[2:7]==(begin['swap'],begin['width'],begin['height'],begin['target'],begin['depth']),
            'Container belongs to another frame')
    require(reader.footer[0]==end['calls'] and reader.footer[1]==end['draws']==word(end.get('inputs')),
            'Ledger and input footer disagree')
    calls={r['sequence']:r for r in rows[1:-1]};seen=set();draws={d['call']:d for d in reader.draws}
    for call,row in calls.items():
        number=word(row.get('input_sequence'))
        if row['name'] in DRAW_NAMES:
            require(call in draws,'Missing returned draw input');d=draws[call];up=row['name'].endswith('UP')
            args=[d['primitive'],d['count'],d['up_data'],d['up_stride']] if up else [d['primitive'],d['first'],d['count']]
            require(number==d['input'] and row['args']==args and row['shader']==d['shader'] and
                    [row['target'],row['depth']]==d['guest_targets'] and up==bool(d['kind']&1),
                    'Draw input belongs to another returned submission')
            require(number not in seen,'Reused input number');seen.add(number)
        else:require(number==0,'Input number attached to a non-draw')
    events_by_call={}
    for event in reader.events:
        call=event['call'];kind=event['type']
        if call:events_by_call.setdefault(call,[]).append(event)
        if kind in ('root','target_root'):
            require(call==0 or call in calls and calls[call]['name'] in TARGET_NAMES,'Target root not introduced at a bind')
        elif kind=='target_observation':
            require(call==end['calls']+1 or call in calls and calls[call]['name'] in TARGET_NAMES,'Observation is not before bind/swap')
        elif kind=='clear':require(call in calls and calls[call]['name']=='D3DDevice_Clear' and event['args']==calls[call]['args'],
                                   'Clear record belongs to another operation')
        elif kind=='bind':require(call in calls and calls[call]['name'] in TARGET_NAMES and
                                  event['guests']==[calls[call]['target'],calls[call]['depth']], 'Bind belongs to another operation')
        elif kind=='draw':require(call in draws,'Orphan draw event')
    bound=list(reader.root);guests=list(begin[k] for k in ('target','depth'))
    for call,row in calls.items():
        events=events_by_call.get(call,[])
        name=row['name']
        require(not any(text in name for text in ('CreateTexture','CreateCubeTexture','CreateVolumeTexture',
                'Surface_Lock','Texture_Lock')) and name not in
                ('Direct3D_CreateDevice','D3DDevice_Reset','D3DDevice_Release'),
                'Unqualified storage lifecycle/CPU target mutation inside frame')
        if row['name']=='D3DDevice_Clear':require(sum(e['type']=='clear' for e in events)==1,'Missing/duplicate clear input')
        if row['name'] in TARGET_NAMES:
            require(sum(e['type']=='bind' for e in events)==1,'Missing/duplicate target binding')
            observations=[e['target'] for e in events if e['type']=='target_observation']
            require(sorted(observations)==sorted(t for t in bound if t),'Missing/extra pre-bind observations')
            binding=next(e for e in events if e['type']=='bind')
            require(all(e['target'] in binding['targets'] for e in events if e['type']=='target_root'),
                    'Introduced target root is not a returned binding')
            bound=binding['targets'];guests=binding['guests']
        require([row['target'],row['depth']]==guests,'Returned call changes targets without a binding')
    final=[e['target'] for e in events_by_call.get(end['calls']+1,[]) if e['type']=='target_observation']
    require(sorted(final)==sorted(t for t in reader.current if t),'Missing/extra final target observations')
    return dict(schema=2,ledger_sha256=accounting['ledger_sha256'],inputs_sha256=reader.sha256,
        frame_boundary_complete=True,complete_draw_input_coverage=True,complete_pixel_qualification=False,
        start_swap=begin['swap'],end_swap=end['swap'],calls=end['calls'],draws=len(reader.draws),
        vertices=sum(d['count'] for d in reader.draws),draw_kinds=accounting['draw_kinds'],assets=len(reader.assets),
        asset_bytes=sum(a['bytes'] for a in reader.assets.values()),container_bytes=reader.path.stat().st_size,
        targets=list(reader.targets.values()),texture_producer_references=reader.producers,
        asset_directory=reader.assets,draws_detail=reader.draws,events=reader.events,
        limitations=['Integrity/coverage are not shader/state/pixel qualification',
                     'Initial target bytes are supplied roots; later target images are observations',
                     'Texture producer links require expected prior target output, not observed image substitution',
                     'Storage creation/reset and CPU target lock mutations are outside this capture scope',
                     'Original startup/resource allocation, enabled-noise and exact silicon remain separate'])


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ledger',type=Path);parser.add_argument('--inputs',type=Path)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    try:result=audit(args.ledger,args.inputs)
    except (OSError,ValueError,KeyError,TypeError,struct.error) as error:
        args.output.write_text(json.dumps(dict(complete_draw_input_coverage=False,complete_pixel_qualification=False,error=str(error)),indent=2)+'\n')
        print('Frame inputs rejected: '+str(error));raise SystemExit(1)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('draws','vertices','draw_kinds','assets','container_bytes','targets','texture_producer_references','complete_draw_input_coverage','complete_pixel_qualification')}))
