"""Private SDK layout transfer with independent ABI and target-address witnesses."""
import argparse
import hashlib
import json
from pathlib import Path
import re

from aot_inventory import parse_xbe
from native_guest_types import TypeDecoder
from native_guest_symbols import codeview_identities
from native_symbol_discovery import QualifiedFunctions

SCHEMA = 'xboxllr.sdk-layouts.v1'


class Unavailable(ValueError):
    pass


def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()


def leaf(types, type_id, seen=()):
    if type_id in seen or len(seen)>=32:
        raise Unavailable('cyclic or excessive field type chain')
    row=types.get(str(type_id))
    if row is None:
        raise Unavailable('field type is missing')
    tag,size=row['tag'],row.get('length')
    if tag==17:
        return leaf(types,row.get('type'),seen+(type_id,))
    if tag==14 and size==4:
        return dict(kind='pointer',bytes=4)
    if tag==16 and type(size) is int and 1<=size<=16:
        basic=row.get('basic')
        if basic in (6,7,10,13,14,31):
            result=dict(kind='integer',bytes=size,signed=basic in (6,13,31))
            if basic==10:result['boolean']=True
            return result
        if basic==8 and size in (4,8):
            return dict(kind='float',bytes=size)
    if tag==15 and type(row.get('count')) is int and 0<row['count']<=4096:
        element=leaf(types,row['type'],seen+(type_id,))
        if size!=row['count']*element['bytes']:
            raise Unavailable('array size and count disagree')
        return dict(kind='array',bytes=size,count=row['count'],element=element)
    raise Unavailable('field needs an independently qualified nested/enum layout')


def shape(document, name, root_ids=None):
    types=document['types'];candidates=[]
    for root in types.values():
        if root_ids is not None and root.get('id') not in root_ids:
            continue
        if root.get('tag')!=11 or root.get('name')!=name or not root.get('length'):
            continue
        if type(root['length']) is not int or not 0<root['length']<=65536:
            raise Unavailable('record size exceeds the qualified bound')
        rows=[m for m in types.values() if m.get('parent')==root['id'] and m.get('tag')==7 and m.get('data_kind')==7]
        if not rows:  # Incomplete/qualified forward records are not layouts.
            continue
        if len(rows)>256:
            raise Unavailable('record has excessive members')
        if any(m.get('parent')==root['id'] and m.get('tag')==18 for m in types.values()):
            raise Unavailable('base-class layout needs additional proof')
        members=[];occupied=set()
        for member in rows:
            value=leaf(types,member['type']);offset=member.get('field_offset')
            if type(offset) is not int or offset<0 or offset+value['bytes']>root['length']:
                raise Unavailable('member exceeds the declared object')
            field=dict(name=member['name'],offset=offset,type=value)
            if member.get('location')==6:
                bit,width,storage=(member.get(k) for k in ('bit_position','bit_width','storage_offset'))
                if (value['kind']!='integer' or storage!=offset or type(bit) is not int or type(width) is not int or
                        bit<0 or width<=0 or bit+width>value['bytes']*8):
                    raise Unavailable('bitfield storage proof is incomplete')
                field.update(bit_position=bit,bit_width=width)
                bits=set(range(offset*8+bit,offset*8+bit+width))
            else:
                bits=set(range(offset*8,(offset+value['bytes'])*8))
            if occupied&bits:
                raise Unavailable('overlapping or union members are ambiguous')
            occupied|=bits;members.append(field)
        candidates.append(dict(bytes=root['length'],members=sorted(members,key=lambda m:(m['offset'],m['name']))))
    unique={digest(row):row for row in candidates}
    if len(unique)!=1:
        raise Unavailable('type name is missing or has incompatible complete layouts')
    return next(iter(unique.values()))


def compare(document, other, name):
    """Explain an incompatible size before unsupported nested fields obscure it."""
    sizes=lambda d:{t['length'] for t in d['types'].values() if t.get('tag')==11 and t.get('name')==name and t.get('length')}
    left,right=sizes(document),sizes(other)
    if not left or not right or len(left)!=1 or len(right)!=1:
        return dict(status='unavailable',reason='type size is missing or ambiguous')
    if left!=right:
        return dict(status='incompatible',reason='declared object sizes differ')
    try:
        equal=shape(document,name)==shape(other,name)
        return dict(status='compatible' if equal else 'incompatible',reason=None if equal else 'member layouts differ')
    except Unavailable as error:
        return dict(status='unavailable',reason=str(error))


def context(blob, symbols, document, reference):
    QualifiedFunctions(symbols,blob)
    image=parse_xbe(blob);TypeDecoder(document,image['sha256'])
    if document.get('source_sha256')!=symbols['source_sha256']:
        raise ValueError('layout and function witnesses have different PDB sources')
    if reference.get('abi')!=dict(machine='ia32',pointer_bytes=4,endianness='little',compiler_family='msvc'):
        raise Unavailable('reference ABI does not match the Xbox IA-32 model')
    library=reference['library']
    records=[row for row in image['libraries'] if row['name']==library]
    if len(records)!=1 or [records[0]['major'],records[0]['minor']]!=reference['api']:
        raise Unavailable('SDK library/version context is missing or incompatible')
    sections={'XAPILIB':('XPP',),'XAPILIBD':('XPP',),'D3DX8':('D3DX',),'D3D8':('D3D',),'DSOUND':('DSOUND',)}.get(library,())
    owners={owner for f in symbols['functions'] if any(s['name'] in sections and s['address']<=f['address']<s['address']+s['size']
            for s in image['sections']) for owner in f.get('compiland_ids',[])}
    compilers=[row for owner in owners for row in symbols.get('compilers',{}).get(str(owner),[]) if row.get('language') in (0,1)]
    if (not compilers or any(not row.get('compiler_name','').startswith('Microsoft') or 'Optimizing Compiler' not in row.get('compiler_name','') or
                            not 12<=row.get('backend_major',0)<=19 or row.get('platform') not in (3,4,5,6,7) for row in compilers)):
        raise Unavailable('SDK compiler ABI context is unavailable')
    compilers=list({digest(row):row for row in compilers}.values())
    return image,dict(library=records[0],compilers=compilers,machine='ia32',pointer_bytes=4,endianness='little')


def qualified_shape(document, name, profile, *, abi_proof=None, abi=None, blob=None):
    result=shape(document,name)
    if result!=profile['shape']:
        raise Unavailable('layout differs from independent declaration/compiler facts')
    # A fixed C object has a size divisible by its power-of-two alignment.
    # For these profiles an odd size proves alignment one, independently of
    # DIA's packed flag (which can be false for actually packed structures).
    if result['bytes']%2==1 and profile.get('alignment')==1 and profile.get('effective_pack')==1:
        return result,'odd-size-and-compiled-declaration'
    if abi_proof is None:
        raise Unavailable('packing/alignment needs additional independent proof')
    if (abi_proof.get('schema')!='xboxllr.sdk-abi-proof.v1' or
            abi_proof.get('xbe_sha256')!=document['xbe_sha256'] or
            abi_proof.get('pdb_sha256')!=document['source_sha256']):
        raise Unavailable('compiled ABI proof belongs to another image/PDB')
    if (abi_proof.get('compiler_record_sha256') not in {digest(row) for row in abi['compilers']} or
            any(not re.fullmatch('[0-9a-f]{64}',abi_proof.get(key,'')) for key in ('artifact_sha256','declaration_sha256'))):
        raise Unavailable('compiled ABI proof lacks matching compiler/artifact provenance')
    try:
        artifact=Path(abi_proof['artifact_path']);declaration=Path(abi_proof['declaration_path'])
        if artifact.stat().st_size>128*1024*1024 or declaration.stat().st_size>2*1024*1024:
            raise Unavailable('compiled ABI evidence exceeds its read bound')
        compiled=artifact.read_bytes()
        if (hashlib.sha256(compiled).hexdigest()!=abi_proof['artifact_sha256'] or
                hashlib.sha256(declaration.read_bytes()).hexdigest()!=abi_proof['declaration_sha256']):
            raise Unavailable('compiled ABI artifact/declaration hash differs')
        identities=codeview_identities(blob)
        if len(identities)!=1 or codeview_identities(compiled)!=identities:
            raise Unavailable('compiled ABI artifact has a different CodeView identity')
    except (OSError,KeyError,TypeError) as error:
        raise Unavailable('compiled ABI evidence is missing or unreadable') from error
    measured=abi_proof.get('types',{}).get(name,{})
    alignment=profile.get('alignment');packing=profile.get('effective_pack')
    if (type(alignment) is not int or alignment<=0 or alignment>8192 or alignment&(alignment-1) or result['bytes']%alignment or
            type(packing) is not int or packing not in (1,2,4,8,16) or
            measured!={'shape_sha256':digest(result),'alignment':alignment,'effective_pack':packing}):
        raise Unavailable('compiled packing/alignment facts differ or are incomplete')
    return result,'image-bound-compiler-measurement'


class LayoutAtlas:
    def __init__(self, reference=None, document=None):
        producer=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        self.document=document or dict(schema=SCHEMA,producer_sha256=producer,reference=reference,sources=[],types={})
        if self.document.get('schema')!=SCHEMA or self.document.get('producer_sha256')!=producer:
            raise ValueError('layout atlas schema/producer does not match')
        if self.document.get('reference',{}).get('schema')!='xboxllr.sdk-layout-reference.v1':
            raise ValueError('independent layout reference is missing')

    def add(self, blob, symbols, layouts, abi_proof=None):
        reference=self.document['reference'];image,abi=context(blob,symbols,layouts,reference)
        if image['sha256'] in self.document['sources']:
            raise ValueError('duplicate layout source')
        self.document['sources'].append(image['sha256']);decisions=[]
        for name,profile in reference['types'].items():
            try:
                result,method=qualified_shape(layouts,name,profile,abi_proof=abi_proof,abi=abi,blob=blob)
                group=self.document['types'].setdefault(name,dict(shape=result,sources=[]))
                group['sources'].append(dict(image=image['sha256'],title_id=image['title_id'],pdb_sha256=symbols['source_sha256'],
                    context=abi,alignment_proof=method,abi_proof_sha256=digest(abi_proof) if abi_proof else None))
                decisions.append(dict(name=name,status='qualified'))
            except Unavailable as error:
                decisions.append(dict(name=name,status='unavailable',reason=str(error)))
        return decisions

    def transfer(self, blob, symbols, layouts, abi_proof=None):
        reference=self.document['reference'];image,abi=context(blob,symbols,layouts,reference)
        if image['sha256'] in self.document['sources']:
            raise ValueError('target was used as a layout source')
        output=dict(schema='xboxllr.guest-types.v1',xbe_sha256=image['sha256'],identity='qualified-sdk-layouts',
                    source_sha256=digest(self.document),types={},objects=[],scopes={},variables={},frames=[],decisions=[],context=abi)
        next_id=1;bindings={}
        def put(row):
            nonlocal next_id
            key=next_id;next_id+=1;output['types'][str(key)]=dict(row,id=key);return key
        def field_type(value):
            if value['kind']=='array':
                return put(dict(tag=15,length=value['bytes'],count=value['count'],type=field_type(value['element'])))
            if value['kind']=='pointer':return put(dict(tag=14,length=4,type=0))
            return put(dict(tag=16,length=value['bytes'],basic=8 if value['kind']=='float' else 10 if value.get('boolean') else 6 if value['signed'] else 7))
        for name,profile in reference['types'].items():
            try:
                result,method=qualified_shape(layouts,name,profile,abi_proof=abi_proof,abi=abi,blob=blob);group=self.document['types'].get(name,{})
                sources=group.get('sources',[])
                if len({row['title_id'] for row in sources})<2 or group.get('shape')!=result:
                    raise Unavailable('needs agreeing independent source titles')
                # Export the agreed source layout after the independent target
                # witness has qualified it, without carrying either PDB's IDs.
                result=group['shape']
                root=put(dict(tag=11,name=name,length=result['bytes'],alignment=profile['alignment'],effective_pack=profile['effective_pack']))
                for member in result['members']:
                    row=dict(tag=7,parent=root,name=member['name'],field_offset=member['offset'],data_kind=7,
                             location=6 if 'bit_width' in member else 4,type=field_type(member['type']))
                    if 'bit_width' in member:
                        row.update(storage_offset=member['offset'],bit_position=member['bit_position'],bit_width=member['bit_width'])
                    put(row)
                bindings[name]=(root,result['bytes'])
                output['decisions'].append(dict(name=name,status='qualified',sources=sources,alignment_proof=method,
                                               abi_proof_sha256=digest(abi_proof) if abi_proof else None))
            except Unavailable as error:
                output['decisions'].append(dict(name=name,status='unavailable',reason=str(error)))
        relations={}
        for row in layouts['types'].values():
            target=layouts['types'].get(str(row.get('unmodified_type')), {})
            if (row.get('tag')==11 and target.get('tag')==11 and
                    all(row.get(key)==target.get(key) for key in ('name','length'))):
                relations.setdefault(row['id'],set()).add(target['id'])
                relations.setdefault(target['id'],set()).add(row['id'])
        for obj in layouts.get('objects',[]):
            row=layouts['types'].get(str(obj['type']),{});seen=set()
            while row.get('tag')==17 and row.get('id') not in seen and len(seen)<32:
                seen.add(row['id']);row=layouts['types'].get(str(row.get('type')), {})
            binding=bindings.get(row.get('name')) if row.get('tag')==11 else None
            if binding:
                root,size=binding;address=obj['address']
                related={row['id']};pending=[row['id']]
                while pending and len(related)<=32:
                    for other in relations.get(pending.pop(),set())-related:
                        related.add(other);pending.append(other)
                if len(related)>32:
                    continue
                try:
                    if shape(layouts,row['name'],related)!=reference['types'][row['name']]['shape']:
                        continue
                except Unavailable:
                    continue
                if (obj.get('mapped_bytes',0)>=size and any(s['address']<=address<address+size<=s['address']+s['size'] for s in image['sections'])):
                    output['objects'].append(dict(name=obj['name'],address=address,type=root,mapped_bytes=size,
                        address_proof='matching-target-PDB',target_pdb_sha256=symbols['source_sha256']))
        TypeDecoder(output,image['sha256'])
        return output


def main():
    parser=argparse.ArgumentParser(description=__doc__);commands=parser.add_subparsers(dest='action',required=True)
    build=commands.add_parser('atlas');build.add_argument('--reference',type=Path,required=True);build.add_argument('--sources',type=Path,required=True)
    transfer=commands.add_parser('transfer');transfer.add_argument('--atlas',type=Path,required=True)
    transfer.add_argument('--xbe',type=Path,required=True);transfer.add_argument('--symbols',type=Path,required=True);transfer.add_argument('--types',type=Path,required=True)
    transfer.add_argument('--abi-proof',type=Path)
    for command in (build,transfer):command.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if not args.output.name.endswith('.aot-private.json'):raise ValueError('layout datasets must stay private')
    read=lambda path:json.loads(Path(path).read_text())
    if args.action=='atlas':
        atlas=LayoutAtlas(read(args.reference))
        for source in read(args.sources):
            print(json.dumps(atlas.add(Path(source['xbe']).read_bytes(),read(source['symbols']),read(source['types']),
                                      read(source['abi_proof']) if source.get('abi_proof') else None)),flush=True)
        result=atlas.document
    else:
        result=LayoutAtlas(document=read(args.atlas)).transfer(args.xbe.read_bytes(),read(args.symbols),read(args.types),
                                                            read(args.abi_proof) if args.abi_proof else None)
        print(json.dumps(dict(qualified=sum(r['status']=='qualified' for r in result['decisions']),objects=len(result['objects']))))
    args.output.write_text(json.dumps(result,separators=(',',':'))+'\n')


if __name__=='__main__':main()
