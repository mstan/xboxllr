"""Declared preowned frame storage; producer inputs never come from readback.

The allocation map is fixture configuration. It does not qualify original
allocation, GPU caching/feedback ordering, startup or resource lifetimes.
"""
from native_frame_inputs import require
from native_frame_target_reference import layout,placement


def target_layouts(scene):
    result=[]
    for role,name in enumerate(('color_header','depth_header')):
        w,h,fmt=scene['before'][role]['shape']
        result.append(layout(dict(width=w,height=h,format=fmt,role=role,resource=scene[name])))
    return result


def validate(plan,scene):
    require(set(plan)=={'target_ids','target_headers','target_addresses','texture_addresses','texture_producers'},
            'Incomplete frame resource placement')
    for name in ('target_ids','target_headers','target_addresses'):
        require(len(plan[name])==2 and all(type(v) is int and v>0 for v in plan[name]),'Incomplete frame target placement')
    require(plan['target_ids'][0]!=plan['target_ids'][1] and
            plan['target_headers'][0]!=plan['target_headers'][1] and
            plan['target_addresses'][0]!=plan['target_addresses'][1],'Overlapping target identities')
    require(all(h%4==0 and 0x2200000<=h<0x2206000 for h in plan['target_headers']),
            'Target header outside preowned descriptor storage')
    require(len(plan['texture_addresses'])==len(plan['texture_producers'])==4,'Incomplete frame texture placement')
    ranges=[]
    for n,address in enumerate(plan['target_addresses']):
        _,height,pitch=target_layouts(scene)[n];size=height*pitch
        require(address%64==0 and address>=0x600000 and address+size<=0x2200000,'Target outside preowned frame storage')
        ranges.append((address,address+size))
    require(ranges[0][1]<=ranges[1][0] or ranges[1][1]<=ranges[0][0],'Target storage overlaps')
    for texture,address,producer in zip(scene['textures'],plan['texture_addresses'],plan['texture_producers']):
        require(type(producer) is int and producer>=0,'Invalid texture producer')
        if texture is None:
            require(address is None and producer==0,'Inactive texture has storage');continue
        require(type(address) is int and address%64==0 and address>=0x600000 and
                address+len(texture['cpu'])<=0x4000000,'Texture outside preowned RAM')
        links={m.get('producer_target',0) for m in texture['mips']}
        require(links=={producer},'Producer placement differs from captured linkage')
        if producer:
            require(len(texture['mips'])==1 and ((texture['resource'][3]>>8)&255) in (0x12,0x1e),
                    'Producer needs an explicit single-level linear sampling header')


def draw_plan(targets,draw,scene):
    headers,storage=placement(targets);pair=[draw['target'],draw['depth']]
    addresses=[];producers=[];cursor=0x3000000
    for texture in scene['textures']:
        if texture is None:addresses.append(None);producers.append(0);continue
        links={m.get('producer_target',0) for m in texture['mips']}
        require(len(links)==1,'Mixed producer/CPU mip storage needs a separate fixture')
        producer=next(iter(links))
        if producer:
            require(producer in targets,'Unknown producer target')
            target=targets[producer];w,h,pitch=layout(target);size=texture['resource'][4]
            require(len(texture['mips'])==1 and [(size&4095)+1,((size>>12)&4095)+1,((size>>24)+1)*64]==[w,h,pitch]
                    and len(texture['cpu'])==pitch*h,'Producer layout differs from target storage')
            addresses.append(storage[producer])
        else:
            addresses.append(cursor);cursor+=(len(texture['cpu'])+0xffff)&~0xffff
        producers.append(producer)
    plan=dict(target_ids=pair,target_headers=[headers[n] for n in pair],target_addresses=[storage[n] for n in pair],
              texture_addresses=addresses,texture_producers=producers)
    validate(plan,scene);return plan
