"""Authored IA-32 -> pinned donor + shared x87 extension -> native C execution.

No game input. Optional XLLE_CPU_DONOR, CC and XLLE_X87_TEST_DIR select local
dependencies/output, as in the continuation fixture.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from native_pilot import DONOR_PIN
from native_x87_lifting import install
from test_native_continuation import fixture_compiler

class NativeX87(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        donor=Path(os.environ.get('XLLE_CPU_DONOR',ROOT.parent/'refs/xboxrecomp-phobos665-review-20261006'))
        if not donor.is_dir():raise unittest.SkipTest('set XLLE_CPU_DONOR to the pinned CPU tools')
        git='C:/Program Files/Git/cmd/git.exe' if sys.platform=='win32' else 'git'
        if subprocess.check_output([git,'-C',str(donor),'rev-parse','HEAD'],text=True).strip()!=DONOR_PIN:
            raise RuntimeError('CPU donor pin differs')
        sys.path.insert(0,str(donor))
        from tools.recomp import lifter,config
        from tools.recomp.translator import FunctionTranslator
        from tools.recomp.disasm import Disassembler,BasicBlock
        install(lifter);cls.lifter=lifter;cls.disassembler=Disassembler;cls.block=BasicBlock
        cls.parts={};cls.predicates=[]
        # DA/DB conditional-move encodings, source ST1..ST7. CMP sets CPU
        # flags; a following SETE/SETB/JNP proves the move preserves them.
        for byte,base in ((0xda,0xc0),(0xda,0xc8),(0xda,0xd0),(0xda,0xd8),
                          (0xdb,0xc0),(0xdb,0xc8),(0xdb,0xd0),(0xdb,0xd8)):
            for source in range(1,8):
                address=0x10000+len(cls.parts)*64
                cls.parts[address]=bytes.fromhex('39d8')+bytes((byte,base+source))+bytes.fromhex('0f94c20f92c67b0681ca00000100c3')
                cls.predicates.append(address)
        cls.minimum=0x10000+len(cls.parts)*64
        cls.parts[cls.minimum]=bytes.fromhex('d8d1dfe0f6c401dac9d9c9ddd8c3')
        cls.maximum=0x10000+len(cls.parts)*64
        cls.parts[cls.maximum]=bytes.fromhex('d8d1dfe0f6c401dbc9d9c9ddd8c3')
        cls.joined=0x10000+len(cls.parts)*64
        cls.parts[cls.joined]=bytes.fromhex('39d8eb00daca0f94c2c3')
        cls.cleared=0x10000+len(cls.parts)*64
        cls.parts[cls.cleared]=bytes.fromhex('39d8dbe20f94c20f92c67b0681ca00000100c3')
        cls.stored=0x10000+len(cls.parts)*64
        cls.parts[cls.stored]=bytes.fromhex('39d80fae1d001001000f94c20f92c67b0681ca00000100c3')
        cls.indexed=0x10000+len(cls.parts)*64
        cls.parts[cls.indexed]=bytes.fromhex('39d80fae5e040f94c20f92c67b0681ca00000100c3')
        cls.freed=[]
        for index in range(8):
            address=0x10000+len(cls.parts)*64
            cls.parts[address]=bytes.fromhex('39d8')+bytes((0xdd,0xc0+index))+bytes.fromhex('0f94c20f92c67b0681ca00000100c3')
            cls.freed.append(address)
        image=bytearray(8192)
        for address,raw in cls.parts.items():image[address-0x10000:address-0x10000+len(raw)]=raw
        config._install([config.Section('.text',0x10000,len(image),0,len(image),True)],entry_point=0x10000,kernel_thunk_addr=0,origin='authored-x87-conditional-moves')
        db={a:dict(_addr=a,start=hex(a),end=a+len(raw),size=len(raw)) for a,raw in cls.parts.items()}
        translator=FunctionTranslator(bytes(image),db)
        cls.code='#define RECOMP_GENERATED_CODE\n#include "recomp_funcs.h"\n'+'\n'.join(translator.translate_function(a,db[a]) for a in db)
        if '/* FPU:' in cls.code or 'unqualified x87' in cls.code:raise RuntimeError('authored qualified fixture guarded or dropped')
        cls.out=Path(os.environ.get('XLLE_X87_TEST_DIR',ROOT/'build/native-x87-diagnostic'));cls.out.mkdir(parents=True,exist_ok=True)
        (cls.out/'lifted.c').write_text(cls.code)
        shutil.copyfile(donor/'templates/runtime/recomp_types.h',cls.out/'recomp_types.h')
        (cls.out/'recomp_funcs.h').write_text('#include "recomp_types.h"\n'+'\n'.join(f'void sub_{a:08X}(void);' for a in db)+'\n')
        declarations='\n'.join(f'sub_{a:08X},' for a in cls.predicates)
        (cls.out/'cases.h').write_text('static recomp_func_t cases[]={'+declarations+'};\n'+
            'static recomp_func_t free_cases[]={'+','.join(f'sub_{a:08X}' for a in cls.freed)+'};\n'+
            f'#define MINIMUM sub_{cls.minimum:08X}\n#define MAXIMUM sub_{cls.maximum:08X}\n#define JOINED sub_{cls.joined:08X}\n#define CLEARED sub_{cls.cleared:08X}\n#define STORED sub_{cls.stored:08X}\n#define INDEXED sub_{cls.indexed:08X}\n')
        exe=cls.out/('native-x87.exe' if sys.platform=='win32' else 'native-x87')
        cc=fixture_compiler(os.environ.get('CC'))
        command=[cc,'-std=c11','-O2','-I'+str(cls.out),'-I'+str(ROOT/'pilots/native-title'),str(cls.out/'lifted.c'),str(ROOT/'tests/fixtures/native_x87_host.c'),'-lm','-o',str(exe)]
        result=subprocess.run(command,capture_output=True,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        if result.returncode:raise RuntimeError(result.stdout+result.stderr)
        cls.output=result.stdout
        (cls.out/'evidence.json').write_text(json.dumps(dict(donor=DONOR_PIN,fixture_sha256=hashlib.sha256(image).hexdigest(),compiler=cc,output=result.stdout),indent=2)+'\n')
    def test_native_flags_stack_conditions_and_minmax(self):
        self.assertIn('PASS 8960 conditional moves',self.output)
        self.assertIn('PASS 128 min/max',self.output)
        self.assertIn('PASS 40 SSE control stores',self.output)
        self.assertIn('PASS 320 register frees',self.output)
    def test_missing_flags_and_unknown_x87_are_explicit_guards(self):
        decoder=self.disassembler()
        for raw,reason in ((bytes.fromhex('dac9'),'conditional move flags/operands'),(bytes.fromhex('d9f4'),'x87 instruction fxtract'),(bytes.fromhex('0fae1500100100'),'SSE control write')):
            insns=decoder.disassemble_function(raw,0x20000,0x20000+len(raw))
            code,_=self.lifter.lift_basic_block(self.lifter.Lifter(),self.block(0x20000,insns))
            self.assertIn('RECOMP_UNIMPL','\n'.join(code));self.assertIn(reason,'\n'.join(code))
    def test_hardener_rejects_old_silent_fpu_fallback(self):
        from native_pilot import harden_generated
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory)/'recomp_0000.c').write_text('void example(void) { /* FPU: fcmove st(0), st(1) */ }\n')
            with self.assertRaisesRegex(ValueError,'unhandled donor lowering'):harden_generated(Path(directory))

if __name__=='__main__':unittest.main()
