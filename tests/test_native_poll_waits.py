"""Authored byte evidence and real C verify the optional whole-poll seam."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
try:
    from native_poll_waits import apply, transform, validate
    HAVE_CAPSTONE = True
except ModuleNotFoundError as error:
    if error.name != 'capstone':
        raise
    HAVE_CAPSTONE = False
from native_thread_checkpoints import instrument


def fixture():
    blob = bytearray(128)
    blob[0:8] = bytes.fromhex('3905002000007cf8')
    blob[16:32] = bytes.fromhex('a10020000040a300200000e900000000')
    blob[40:50] = bytes.fromhex('6810100000e81e000000')
    blob[80:83] = bytes.fromhex('c20400')
    image = dict(sha256=hashlib.sha256(blob).hexdigest(), sections=[dict(address=0x1000, raw_offset=0, raw_size=128, flags=4)])
    def span(start, length):
        return dict(start=hex(start), end=hex(start+length), sha256=hashlib.sha256(blob[start-0x1000:start-0x1000+length]).hexdigest())
    contract = dict(version=1, xbe_sha256=image['sha256'], counter='0x2000', publisher=span(0x1010,16),
                    registration=span(0x1028,10), export=span(0x1050,3), loops=[dict(function='0x1000',**span(0x1000,8))])
    source = '''#include <math.h>
#include <stdint.h>
#include <stdio.h>
static uint32_t counter,eax=2,esp=1024;
static unsigned checkpoints,waits;
static void checkpoint(void) { if(++checkpoints==4 || checkpoints==8)counter++; }
#define PILOT_THREAD_CHECKPOINT() checkpoint()
#define MEM32(address) counter
#define CMP_L(a,b) ((a)<(b))
void sub_00001000(void)
{
    uint32_t _fa=0,_fb=0;
    int32_t _fas=0,_fbs=0;
loc_00001000: ;
    PILOT_THREAD_CHECKPOINT();
    _fa = (uint32_t)(MEM32(0x2000)) & 0xFFFFFFFFu; _fb = (uint32_t)(eax) & 0xFFFFFFFFu;
    _fas = (int32_t)(int32_t)(_fa); _fbs = (int32_t)(int32_t)(_fb); /* cmp MEM32(0x2000), eax (32-bit) */
    if (CMP_L(_fas, _fbs)) goto loc_00001000; /* jl: less (signed <) */
    if(_fa!=2 || _fb!=2 || _fas!=2 || _fbs!=2 || esp!=1024)counter=99;
}
void pilot_native_poll_wait(void) { waits++; }
int main(void) {
    sub_00001000();
    printf("%u %u %u %u\\n",counter,eax,esp,checkpoints);
#ifdef PILOT_NATIVE_POLL_WAIT
    return waits!=7;
#else
    return waits!=0;
#endif
}
'''
    return bytes(blob), image, contract, source


@unittest.skipUnless(HAVE_CAPSTONE, 'optional native generation requires Capstone')
class PollWaits(unittest.TestCase):
    def test_shape_scope_and_recovery_replay(self):
        blob,image,contract,source=fixture()
        validate(blob,image,contract)
        result=transform(source,contract['loops'],0x2000)
        self.assertEqual(transform(result,contract['loops'],0x2000),result)
        self.assertIn('PILOT_THREAD_CHECKPOINT();',result)
        self.assertIn('if (CMP_L(_fas, _fbs)) {',result)
        self.assertEqual(instrument(result)[0],result)
        self.assertEqual(result.count('void pilot_native_poll_wait(void);'),1)
        with tempfile.TemporaryDirectory() as td:
            directory=Path(td);path=directory/'recomp_0000.c';path.write_text(source)
            record=apply(blob,image,directory,contract)
            self.assertEqual(record['units'],['recomp_0000.c'])
            self.assertEqual(apply(blob,image,directory,contract),record)
            path.write_text(source.replace('#include <math.h>','#include <math.h>\n/* recovered owner */'))
            self.assertEqual(apply(blob,image,directory,contract),record)
            self.assertEqual(json.loads((directory/'pilot_poll_wait_units.json').read_text()),record['units'])

    def test_fail_closed_before_any_source_write(self):
        blob,image,contract,source=fixture()
        with tempfile.TemporaryDirectory() as td:
            directory=Path(td);path=directory/'recomp_0000.c';path.write_text(source)
            for bad in [dict(contract,version=2),dict(contract,xbe_sha256='0'*64),dict(contract,counter='0xFD000000'),dict(contract,loops=contract['loops']*2)]:
                with self.assertRaises(ValueError):apply(blob,image,directory,bad)
                self.assertEqual(path.read_text(),source)
            bad=json.loads(json.dumps(contract));bad['loops'].append(dict(contract['loops'][0],function='0x1100',start='0x1100'))
            with self.assertRaises(ValueError):apply(blob,image,directory,bad)
            self.assertEqual(path.read_text(),source)
            path.write_text(source.replace('    PILOT_THREAD_CHECKPOINT();',''))
            with self.assertRaisesRegex(ValueError,'shape changed'):apply(blob,image,directory,contract)
            self.assertFalse((directory/'pilot_poll_wait_units.json').exists())

    def test_signed_predicate_counter_and_registration_are_bound(self):
        blob,image,contract,source=fixture()
        for offset,replacement in [(6,0x72),(2,0x04),(41,0x20),(81,0x08),(16,0x90)]:
            changed=bytearray(blob);changed[offset]=replacement
            with self.assertRaisesRegex(ValueError,'bytes changed'):validate(bytes(changed),image,contract)
        for text in [source.replace('CMP_L(_fas, _fbs)','CMP_L(_fbs, _fas)'),source.replace('MEM32(0x2000)','MEM32(0x2004)')]:
            with self.assertRaisesRegex(ValueError,'shape changed'):transform(text,contract['loops'],0x2000)

    def test_rehashed_unsafe_code_still_rejected(self):
        blob,image,contract,source=fixture()
        for offset,value in [(6,0x72),(7,0xf0),(1,0x15),(21,0x48),(41,0x20),(81,0x08)]:
            changed=bytearray(blob);changed[offset]=value
            altered=json.loads(json.dumps(contract));new_image=dict(image,sha256=hashlib.sha256(changed).hexdigest())
            altered['xbe_sha256']=new_image['sha256']
            for span in [altered['publisher'],altered['registration'],altered['export'],*altered['loops']]:
                start,end=int(span['start'],0)-0x1000,int(span['end'],0)-0x1000
                span['sha256']=hashlib.sha256(changed[start:end]).hexdigest()
            with self.assertRaises(ValueError):validate(bytes(changed),new_image,altered)

    def test_compiled_original_condition_flags_stack_and_producer(self):
        compiler=os.environ.get('CC') or next((p for p in ['C:/msys64/mingw64/bin/gcc.exe',shutil.which('gcc'),shutil.which('clang')] if p and Path(p).is_file()),None)
        if not compiler:self.skipTest('GCC/Clang missing; set CC')
        blob,image,contract,source=fixture();source=transform(source,contract['loops'],0x2000)
        with tempfile.TemporaryDirectory() as td:
            directory=Path(td);path=directory/'poll.c';path.write_text(source)
            for optimize in ['-O0','-O2']:
                for enabled in [False,True]:
                    exe=directory/('poll'+optimize+str(enabled)+'.exe')
                    command=[compiler,'-std=c11',optimize,'-Wall','-Werror',str(path),'-o',str(exe)]
                    if enabled:command.append('-DPILOT_NATIVE_POLL_WAIT')
                    built=subprocess.run(command,capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stderr)
                    run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=5)
                    self.assertEqual(run.returncode,0,run.stderr)
                    self.assertEqual(run.stdout,'2 2 1024 8\n')


if __name__=='__main__':unittest.main()
