"""Authored generated-C fixtures; no owned title code is required."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_unit_budget as b

PREFIX = b'''/**
 * Authored - Recompiled code chunk 0
 * Functions: 3 (0x00001000 - 0x00001020)
 */

#define RECOMP_GENERATED_CODE
#include "recomp_funcs.h"
#include <math.h>

'''


def function(address, symbol=None, body=b'    return;\n'):
    symbol=symbol or f'sub_{address:08X}'
    return (f'/**\n * sub_{address:08X}\n * Original: 0x{address:08X} - 0x{address+4:08X} (4 bytes, 2 insns)\n * CC: cdecl, 0 params, returns int_or_void\n */\nvoid {symbol}(void)\n{{\n'.encode()+body+b'}\n\n')


class UnitBudgetTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory=Path(self.temp.name)

    def unit(self,name,parts):
        path=self.directory/name
        path.write_bytes(PREFIX+b''.join(parts))
        return path

    def test_lossless_prefix_function_identity_and_new_names(self):
        sections=[function(0x1000),function(0x1010,'pilot_d3d_original_sub_00001010'),
                  function(0x1020,'pilot_original_audio_Release')]
        original=self.unit('recomp_0002.c',sections).read_bytes()
        untouched=self.unit('recomp_0017.c',[function(0x2000)])
        stamp=untouched.stat().st_mtime_ns
        (self.directory/'recomp_dispatch.c').write_bytes(b'untouched dispatcher')
        receipt=b.apply(self.directory,max_functions=1,max_bytes=10000)
        first=next(r for r in receipt['inputs'] if r['name']=='recomp_0002.c')
        self.assertEqual(first['outputs'],['recomp_0002.c','recomp_0018.c','recomp_0019.c'])
        gathered=[]
        for name in first['outputs']:
            prefix,functions=b.parse_unit((self.directory/name).read_bytes())
            self.assertEqual(prefix,PREFIX)
            gathered.extend(f.section for f in functions)
        self.assertEqual(PREFIX+b''.join(gathered),original)
        self.assertEqual(untouched.stat().st_mtime_ns,stamp)
        self.assertEqual((self.directory/'recomp_dispatch.c').read_bytes(),b'untouched dispatcher')
        all_functions=[f for p in self.directory.glob('recomp_0*.c') for f in b.parse_unit(p.read_bytes())[1]]
        self.assertEqual(len({f.address for f in all_functions}),4)
        self.assertEqual(len({f.symbol for f in all_functions}),4)
        self.assertEqual(len(all_functions),4)

    def test_byte_budget_includes_preamble_and_indivisible_record(self):
        small=function(0x1000)
        huge=function(0x1010,body=b'    /* '+b'x'*1000+b' */\n')
        self.unit('recomp_0000.c',[small,huge,function(0x1020)])
        limit=len(PREFIX)+len(small)+1
        receipt=b.apply(self.directory,max_functions=64,max_bytes=limit)
        self.assertEqual([r['functions'] for r in receipt['outputs']],[1,1,1])
        oversize=[r for r in receipt['outputs'] if r['bytes']>limit]
        self.assertEqual(len(oversize),1)
        self.assertEqual(oversize[0]['indivisible'],['sub_00001010'])

    def test_idempotent_unchanged_bytes_and_mtimes(self):
        self.unit('recomp_0000.c',[function(0x1000+i*16) for i in range(5)])
        b.apply(self.directory,max_functions=2,max_bytes=10000)
        before={p.name:(p.read_bytes(),p.stat().st_mtime_ns) for p in self.directory.iterdir()}
        receipt=b.apply(self.directory,max_functions=2,max_bytes=10000)
        self.assertFalse(receipt['changed'])
        self.assertEqual(before,{p.name:(p.read_bytes(),p.stat().st_mtime_ns) for p in self.directory.iterdir()})

    def test_literals_comments_nested_scopes_and_crlf_preserved(self):
        body=b'''    const char *s="{ \\\" }";
    char q='}'; /* } { */
    // }
    { if (q) { s="/*"; } }
'''
        raw=(PREFIX+function(0x1000,body=body)).replace(b'\n',b'\r\n')
        prefix,functions=b.parse_unit(raw)
        self.assertEqual(prefix+b''.join(f.section for f in functions),raw)
        self.assertIn(b'\r\n',prefix)

    def test_function_local_fp_macros_preserved_and_must_not_escape(self):
        body=b'    #define fp_push(v) do { \\\n        use(v); } while(0)\n    #define fp_st1() fp_st(1)\n    fp_push(1);\n    #undef fp_push\n    #undef fp_st1\n'
        raw=PREFIX+function(0x1000,body=body)
        prefix,functions=b.parse_unit(raw)
        self.assertEqual(prefix+functions[0].section,raw)
        with self.assertRaises(ValueError):
            b.parse_unit(raw.replace(b'    #undef fp_push\n',b''))

    def test_entry_annotation_supported(self):
        entry=function(0x1000).replace(b'sub_00001000',b'xbe_entry_point')
        prefix,functions=b.parse_unit(PREFIX+entry)
        self.assertEqual(functions[0].symbol,'xbe_entry_point')
        self.assertEqual(functions[0].address,'00001000')
        self.assertEqual(prefix+functions[0].section,PREFIX+entry)

    def test_unknown_top_level_or_incomplete_input_rejects_before_mutation(self):
        malformed=[b'static int unknown;\n'+function(0x1000),
                   function(0x1000)+b'void extra(void) {}\n',
                   function(0x1000)[:-3],
                   function(0x1000).replace(b'0x00001000 -',b'0x00001001 -'),
                   function(0x1000,body=b'#define LEAK 1\n'),
                   function(0x1000).replace(b' * CC:',b' * premature */ int hidden; /* CC:'),
                   function(0x1000,body=b'/* unterminated\n'),
                   function(0x1000,body=b'"unterminated\n')]
        good=self.unit('recomp_0000.c',[function(0x2000),function(0x2010)])
        for bad in malformed:
            with self.subTest(bad=bad):
                path=self.directory/'recomp_0001.c'
                path.write_bytes(PREFIX+bad)
                before={p.name:p.read_bytes() for p in self.directory.iterdir()}
                with self.assertRaises(ValueError):
                    b.apply(self.directory,max_functions=1)
                self.assertEqual(before,{p.name:p.read_bytes() for p in self.directory.iterdir()})

    def test_duplicate_and_ambiguous_numeric_units_rejected(self):
        self.unit('recomp_0000.c',[function(0x1000)])
        path=self.unit('recomp_0001.c',[function(0x1000)])
        with self.assertRaises(ValueError):b.partition(self.directory)
        path.unlink()
        self.unit('recomp_0.c',[function(0x2000)])
        with self.assertRaises(ValueError):b.partition(self.directory)

    def test_invalid_budget_empty_directory_and_unknown_preamble(self):
        for count,size in ((0,10),(1,0),(-1,10),(True,10),(1,1.5)):
            with self.assertRaises(ValueError):b.partition(self.directory,count,size)
        with self.assertRaises(ValueError):b.partition(self.directory)
        (self.directory/'recomp_0000.c').write_bytes(b'#include "recomp_funcs.h"\n'+function(0x1000))
        with self.assertRaises(ValueError):b.partition(self.directory)


if __name__=='__main__':unittest.main()
