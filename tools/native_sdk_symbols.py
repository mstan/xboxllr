"""Conservative, private SDK attribution from complete image-bound x86 bodies."""
from collections import Counter, defaultdict
import argparse
import hashlib
import json
from pathlib import Path
import struct

import capstone as cs
from capstone.x86_const import X86_OP_IMM, X86_OP_MEM, X86_OP_REG
from aot_inventory import parse_xbe
from native_guest_symbols import SCHEMA, Symbols
from native_guest_types import TypeDecoder
from native_image_reads import section_window
from native_symbol_discovery import QualifiedFunctions

ATLAS_SCHEMA = 'xboxllr.sdk-signatures.v1'
MAX_BODY = 8192


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def family(section):
    name = section.upper()
    for prefix, library in (('D3DX','D3DX8'), ('D3D','D3D8'), ('DSOUND','DSOUND'),
                            ('XGRPH','XGRAPHC'), ('XGRAPHC','XGRAPHC'), ('XPP','XAPILIB'),
                            ('XNET','XNET')):
        if name.startswith(prefix):
            return library
    return None


class Unknown(ValueError):
    pass


class Image:
    def __init__(self, blob, functions):
        self.blob = blob
        self.info = parse_xbe(blob)
        self.sha = self.info['sha256']
        self.sections = self.info['sections']
        # Labels never enter target matching or dependency fingerprints.
        self.functions = {int(row['start'],16):int(row['end'],16)-int(row['start'],16)
                          if isinstance(row.get('end'),str) else row['end']-int(row['start'],16)
                          for row in functions if 'start' in row}
        self.functions.update({row['address']:row['size'] for row in functions if 'address' in row})
        count, table = struct.unpack_from('<II',blob,0x160)
        grouped = defaultdict(list)
        for index, library in enumerate(self.info['libraries']):
            grouped[library['name']].append(dict(library, flags=struct.unpack_from('<H',blob,table-self.info['base']+index*16+14)[0]))
        self.libraries = {name:rows[0] for name,rows in grouped.items() if len(rows)==1}
        self.imports = {}
        encoded = struct.unpack_from('<I',blob,0x158)[0]
        key = 0x5B6D40B6 if self.info['encoding']=='retail' else 0xEFB1F152
        if encoded not in (0,key):
            address = encoded ^ key
            for index in range(4096):
                raw = self.read(address+index*4,4)
                if raw is None or len(raw)!=4:
                    raise ValueError('kernel import table outside the loaded image')
                value = int.from_bytes(raw,'little')
                if value==0:
                    break
                if value & 0xffff0000 != 0x80000000 or not value & 0xffff:
                    raise ValueError('unqualified kernel import ordinal')
                self.imports[address+index*4] = value & 0xffff
            else:
                raise ValueError('unterminated kernel import table')
        self.decoder = cs.Cs(cs.CS_ARCH_X86,cs.CS_MODE_32)
        self.decoder.detail = True
        self.cache = {}
        self.rejections = Counter()

    def section(self, address):
        return next((s for s in self.sections if s['address']<=address<s['address']+s['size']),None)

    def read(self, address, count):
        section = self.section(address)
        if section:
            raw = section_window(self.blob,(section['address'],section['size'],section['raw_offset'],section['raw_size']),address,count)
            return raw if raw is not None and len(raw)==count else None
        base = self.info['base']
        headers = struct.unpack_from('<I',self.blob,0x108)[0]
        if base<=address and address+count<=base+headers:
            return self.blob[address-base:address-base+count]
        return None

    def context(self, address):
        section = self.section(address)
        library = family(section['name']) if section else None
        version = self.libraries.get(library)
        if not version:
            raise Unknown('SDK library context unavailable')
        return library, version

    def fingerprint(self, address, depth=2, active=frozenset()):
        key = address,depth
        if key in self.cache:
            return self.cache[key]
        try:
            result = self._fingerprint(address,depth,active)
        except Unknown as error:
            result = dict(status='unknown',reason=str(error))
        self.cache[key] = result
        return result

    def _fingerprint(self, address, depth, active):
        if address in active:
            raise Unknown('recursive callee needs additional proof')
        library, version = self.context(address)
        size = self.functions.get(address)
        if type(size) is not int or not 0<size<=MAX_BODY:
            raise Unknown('function extent unavailable or exceeds bound')
        section = self.section(address)
        if not section['address']<=address<section['address']+section['raw_size']:
            raise Unknown('entry is not file backed')
        raw = self.read(address,size)
        if raw is None:
            raise Unknown('function exceeds its loaded section')
        insns = {i.address:i for i in self.decoder.disasm(raw,address)}
        if not insns or len(insns)>2048:
            raise Unknown('body is undecodable or exceeds instruction bound')
        visited, pending, tokens, constants, imports, callees = set(),[address],{},[],set(),set()
        while pending:
            pc = pending.pop()
            if pc in visited:
                continue
            insn = insns.get(pc)
            if insn is None:
                raise Unknown('CFG edge is not an instruction boundary')
            visited.add(pc)
            if insn.mnemonic in ('int','int1','int3','ud2','hlt','sysenter','sysexit'):
                raise Unknown('exception or system transfer needs separate proof')
            call,jump,ret = (insn.group(kind) for kind in (cs.CS_GRP_CALL,cs.CS_GRP_JUMP,cs.CS_GRP_RET))
            operands = []
            for index, op in enumerate(insn.operands):
                if op.type==X86_OP_REG:
                    operands.append(('reg',insn.reg_name(op.reg),op.size))
                elif op.type==X86_OP_IMM:
                    value = op.imm & 0xffffffff
                    if call or jump:
                        if address<=value<address+size:
                            operands.append(('local',value-address));pending.append(value)
                        else:
                            if depth<=0:
                                raise Unknown('callee depth limit')
                            callee = self.fingerprint(value,depth-1,active|{address})
                            if callee['status']!='qualified':
                                raise Unknown('external callee is unqualified')
                            operands.append(('callee',callee['signature']));callees.add(callee['signature'])
                    elif self.section(value):
                        raise Unknown('image-address immediate needs independent object proof')
                    else:
                        operands.append(('imm',value,op.size))
                elif op.type==X86_OP_MEM:
                    mem = op.mem
                    base,index_reg,segment = (insn.reg_name(v) if v else '' for v in (mem.base,mem.index,mem.segment))
                    disp = mem.disp & 0xffffffff
                    if (not base and not index_reg and not segment and op.size==4 and
                            disp in self.imports and (call or insn.mnemonic=='jmp')):
                        ordinal = self.imports[disp]
                        operands.append(('kernel',ordinal));imports.add(ordinal)
                    elif self.section(disp):
                        target = self.section(disp)
                        if (base or index_reg or segment or insn.mnemonic=='lea' or
                                op.access!=cs.CS_AC_READ or not 0<op.size<=16 or target['flags']&1 or
                                disp+op.size>target['address']+target['raw_size']):
                            raise Unknown('addressed mutable/indexed data needs independent proof')
                        value = self.read(disp,op.size)
                        operands.append(('constant',op.size,value.hex(),disp%min(op.size,16)))
                        constants.append(dict(offset=pc-address,operand=index,address=disp,size=op.size,sha256=hashlib.sha256(value).hexdigest()))
                    elif not base and not index_reg and not segment:
                        raise Unknown('unmapped absolute data access')
                    else:
                        operands.append(('mem',segment,base,index_reg,mem.scale,mem.disp,op.size))
                else:
                    raise Unknown('unqualified operand form')
            if (call or jump) and (not insn.operands or insn.operands[0].type!=X86_OP_IMM):
                if not (len(operands)==1 and operands[0][0]=='kernel'):
                    raise Unknown('unresolved indirect transfer')
            tokens[pc-address] = (insn.mnemonic,insn.size,operands)
            if ret or (jump and insn.mnemonic in ('jmp','ljmp')):
                continue
            following = pc+insn.size
            if following>=address+size:
                raise Unknown('CFG falls beyond the body')
            pending.append(following)
        covered = {at for pc in visited for at in range(pc,pc+insns[pc].size)}
        if any(address+offset not in covered and byte not in (0x90,0xcc) for offset,byte in enumerate(raw)):
            raise Unknown('unrepresented body bytes or embedded data')
        meaningful = [insns[pc] for pc in visited if insns[pc].mnemonic!='nop']
        signature = digest(dict(architecture='ia32-le',library=library,api=(version['major'],version['minor']),body=sorted(tokens.items())))
        return dict(status='qualified',signature=signature,size=size,body_bytes=sum(i.size for i in meaningful),instructions=len(meaningful),
                    library=library,version=version,constants=constants,imports=sorted(imports),callees=sorted(callees))


class Atlas:
    def __init__(self, document=None):
        producer = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
        self.document = document or dict(schema=ATLAS_SCHEMA,producer_sha256=producer,sources=[],groups={})
        if self.document.get('schema')!=ATLAS_SCHEMA:
            raise ValueError('unsupported SDK atlas')
        if self.document.get('producer_sha256')!=producer:
            raise ValueError('SDK atlas was produced by a different matcher')

    def add(self, image, symbols, layouts=None):
        QualifiedFunctions(symbols,image.blob)
        if image.sha in self.document['sources']:
            raise ValueError('duplicate atlas source image')
        objects = defaultdict(set)
        if layouts is not None:
            decoder = TypeDecoder(layouts,image.sha)
            if layouts.get('source_sha256')!=symbols['source_sha256']:
                raise ValueError('object and function metadata have different sources')
            for obj in layouts.get('objects',[]):
                # A whole scalar object, not a guessed containing global or UDT.
                for size in (1,2,4,8,16):
                    raw = image.read(obj['address'],size)
                    if (raw is not None and obj.get('mapped_bytes',0)>=size and
                            decoder.scalar(obj['type'],raw).get('kind') in ('integer','enum','float') and
                            obj.get('name')):
                        objects[(obj['address'],size)].add(obj['name'])
        self.document['sources'].append(image.sha)
        stats = Counter()
        for row in symbols['functions']:
            result = image.fingerprint(row['address'])
            if result['status']!='qualified' or result['body_bytes']<32 or result['instructions']<8:
                stats[result.get('reason','tiny or trivial body')] += 1
                continue
            group = self.document['groups'].setdefault(result['signature'],[])
            constants = [dict(use,names=sorted(objects.get((use['address'],use['size']),())))
                         for use in result['constants']]
            compilers = [compiler for owner in row.get('compiland_ids',[])
                         for compiler in symbols.get('compilers',{}).get(str(owner),[])]
            group.append(dict(image=image.sha,title_id=image.info['title_id'],address=row['address'],
                              names=row['names'],version=result['version'],library=result['library'],
                              constants=constants,source_sha256=symbols['source_sha256'],
                              compiler_context=dict(status='known' if compilers else 'unavailable',records=compilers)))
            stats['indexed'] += 1
        return dict(stats)

    def match(self, image):
        if image.sha in self.document['sources']:
            raise ValueError('target image was used to select atlas labels')
        functions, decisions, object_uses, object_decisions = [],[],defaultdict(list),[]
        for address in sorted(image.functions):
            result = image.fingerprint(address)
            reason = result.get('reason')
            group = self.document['groups'].get(result.get('signature'),[])
            if result['status']=='qualified':
                name_sets = {tuple(sorted(row['names'])) for row in group}
                if result['body_bytes']<32 or result['instructions']<8:
                    reason='tiny or trivial body'
                elif len(name_sets)!=1:
                    reason='unknown or ambiguous SDK signature'
                elif len({row['title_id'] for row in group})<2:
                    reason='needs independent source titles'
                elif not any(row['version']==result['version'] for row in group) and len({row['version']['build'] for row in group})<2:
                    reason='SDK revision transfer is unqualified'
                elif any((row['version']['flags']^result['version']['flags'])&0x8000 for row in group):
                    reason='debug/retail library context differs'
                else:
                    functions.append(dict(address=address,size=result['size'],names=list(next(iter(name_sets))),
                         extent_kind='qualified-sdk-body',signature=result['signature'],library=result['library'],
                         target_version=result['version'],sources=group,constant_uses=result['constants'],
                         imports=result['imports'],callees=result['callees'],
                         compiler_context=dict(status='unavailable',proof='complete decoded body; no compiler or layout inference')))
                    for use in result['constants']:
                        references = [dict(row=source,use=constant) for source in group
                            for constant in source['constants']
                            if (constant['offset'],constant['operand'],constant['size'],constant['sha256']) ==
                               (use['offset'],use['operand'],use['size'],use['sha256'])]
                        labels = {tuple(ref['use'].get('names',[])) for ref in references}
                        if (len(labels)==1 and next(iter(labels)) and
                                len({ref['row']['title_id'] for ref in references})>=2):
                            object_uses[use['address']].append(dict(use,names=list(next(iter(labels))),
                                function=address,signature=result['signature'],
                                sources=[dict(image=ref['row']['image'],function=ref['row']['address'],
                                              address=ref['use']['address']) for ref in references]))
                        else:
                            object_decisions.append(dict(address=use['address'],function=address,
                                offset=use['offset'],operand=use['operand'],status='unknown',
                                reason='needs unanimous named scalar proof from independent source titles'))
                    reason=None
            decisions.append(dict(address=address,status='qualified' if reason is None else 'unknown',reason=reason))
        objects = []
        for address,uses in sorted(object_uses.items()):
            keys = {(use['size'],use['sha256'],tuple(use['names'])) for use in uses}
            if len(keys)==1:
                objects.append(dict(address=address,size=uses[0]['size'],names=uses[0]['names'],
                                    sha256=uses[0]['sha256'],proof='decoded-readonly-scalar-xrefs',uses=uses))
                object_decisions.append(dict(address=address,status='qualified',reason=None))
            else:
                object_decisions.append(dict(address=address,status='unknown',reason='conflicting addressed-object proofs'))
        document = dict(schema=SCHEMA,xbe_sha256=image.sha,identity='qualified-sdk-signatures',
                        source_sha256=digest(self.document),functions=functions,objects=objects,
                        decisions=decisions,object_decisions=object_decisions)
        Symbols(document,image.sha)
        return document


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    commands=parser.add_subparsers(dest='action',required=True)
    build=commands.add_parser('atlas');build.add_argument('--sources',type=Path,required=True)
    match=commands.add_parser('match');match.add_argument('--atlas',type=Path,required=True)
    match.add_argument('--xbe',type=Path,required=True);match.add_argument('--functions',type=Path,required=True)
    for command in (build,match):command.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.action=='atlas':
        atlas=Atlas()
        for source in json.loads(args.sources.read_text()):
            symbols=json.loads(Path(source['symbols']).read_text())
            image=Image(Path(source['xbe']).read_bytes(),symbols['functions'])
            layouts=json.loads(Path(source['types']).read_text()) if source.get('types') else None
            print(json.dumps(dict(image=image.sha,stats=atlas.add(image,symbols,layouts))),flush=True)
        document=atlas.document
    else:
        atlas=Atlas(json.loads(args.atlas.read_text()))
        rows=json.loads(args.functions.read_text());rows=rows.get('functions',[]) if isinstance(rows,dict) else rows
        document=atlas.match(Image(args.xbe.read_bytes(),rows))
        print(json.dumps(dict(qualified=len(document['functions']),objects=len(document['objects']),
                              unknown=len(document['decisions'])-len(document['functions']))))
    if not args.output.name.endswith('.aot-private.json'):
        raise ValueError('SDK-derived datasets must use an aot-private.json filename')
    args.output.write_text(json.dumps(document,separators=(',',':'))+'\n')


if __name__=='__main__':main()
