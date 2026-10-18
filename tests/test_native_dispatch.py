"""Compile transformed C: exact targets, holes, outliers and allocation failure."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from native_dispatch import compact, refresh_inventory_count, MAX_BYTES


def fixture(addresses):
    definitions = '\n'.join(f'void f{i}(void) {{ sink={i+1}; }}' for i in range(len(addresses)))
    rows = '\n'.join(f'    {{ 0x{a:08X}u, f{i} }},' for i, a in enumerate(addresses))
    return f'''#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
typedef void (*recomp_func_t)(void);
volatile unsigned sink;
{definitions}
typedef struct {{ uint32_t xbox_va; recomp_func_t func; }} recomp_entry_t;
static const recomp_entry_t g_recomp_table[] = {{
{rows}
}};
static const size_t g_recomp_table_size = {len(addresses)};
/* ----------------------------------------------------------------
 * Flat, directly-indexed dispatch.
 */
/* Get the number of registered functions */
'''


class DispatchTests(unittest.TestCase):
    def test_shape_idempotency_and_bounds(self):
        source, info = compact(fixture([0,0x100000,0x100004,0x100100,0x031c0000]))
        self.assertEqual((info['base'],info['span'],info['flat_entries']), (0x100000,0x101,3))
        self.assertEqual(compact(source), (source,info))
        _, tie = compact(fixture([1,2,0x200000,0x200001]))
        self.assertEqual(tie['base'],1)
        self.assertEqual(info['max_bytes'],MAX_BYTES)

    def test_invalid_inventory_fails(self):
        for addresses in ([],[2,1],[1,1]):
            with self.assertRaises(ValueError): compact(fixture(addresses))
        with self.assertRaises(ValueError):
            compact(fixture([1]).replace('g_recomp_table_size = 1','g_recomp_table_size = 2'))

    def test_recovered_inventory_refreshes_old_bound(self):
        stale = fixture([0x100000,0x100004,0x100100,0x031c0000]).replace(
            'g_recomp_table_size = 4', 'g_recomp_table_size = 2')
        fixed, info = compact(refresh_inventory_count(stale))
        self.assertEqual(info['entries'], 4)
        self.assertIn('sizeof(g_recomp_table) / sizeof(g_recomp_table[0])', fixed)
        self.assertEqual(compact(fixed), (fixed, info))

    def compile_run(self, addresses, fail=False, expect_flat=True):
        compiler = os.environ.get('CC')
        if not compiler:
            compiler = next((x for x in ('C:/msys64/mingw64/bin/gcc.exe',shutil.which('gcc'),shutil.which('clang')) if x and Path(x).is_file()),None)
        if not compiler: self.skipTest('C compiler unavailable; set CC')
        source, info = compact(fixture(addresses))
        if fail:
            source=source.replace('typedef void (*recomp_func_t)', 'static void *no_alloc(size_t n,size_t s) { (void)n;(void)s;return NULL; }\n#define calloc no_alloc\ntypedef void (*recomp_func_t)')
        expected = '\n'.join(f'if (recomp_lookup(0x{a:X}u)!=f{i}) return {i+10};' for i,a in enumerate(addresses))
        candidates=sorted({0,0xffffffff,*[x for a in addresses for x in (a-1,a+1) if 0<=x<=0xffffffff]}-set(addresses))
        absent='\n'.join(f'if (recomp_lookup(0x{a:X}u)) return 2;' for a in candidates)
        checks=expected+'\n'+absent
        source+=f'''
int main(void) {{
    if(recomp_dispatch_flat_bytes()) return 1;
    {checks}
    if(recomp_dispatch_init()!={(1 if expect_flat else 0)}) return 3;
    if(recomp_dispatch_flat_bytes()!={(info['span'] if expect_flat else 0)}ull*sizeof(recomp_func_t)) return 4;
    {checks}
    if(recomp_dispatch_init()!={(1 if expect_flat else 0)}) return 5;
    {checks}
    for(size_t i=0;i<g_recomp_table_size;i++) {{
        recomp_lookup(g_recomp_table[i].xbox_va)();
        if(sink!=i+1) return 6;
    }}
    /* Check every byte/unaligned hole in the small main cluster. */
    for(uint32_t a=0x100000;a<0x100102;a++) {{
        recomp_func_t expected=NULL;
        for(size_t i=0;i<g_recomp_table_size;i++) if(g_recomp_table[i].xbox_va==a) expected=g_recomp_table[i].func;
        if(recomp_lookup(a)!=expected) return 7;
    }}
    return 0;
}}
'''
        with tempfile.TemporaryDirectory() as td:
            directory=Path(td); c=directory/'dispatch.c'; exe=directory/'dispatch.exe'
            c.write_text(source)
            subprocess.run([compiler,'-std=c11','-O2','-Wall','-Werror',str(c),'-o',str(exe)],check=True,capture_output=True)
            subprocess.run([str(exe)],check=True,capture_output=True)

    def test_compiled_exact_lookup(self):
        self.compile_run([0,0x100000,0x100004,0x100100,0x031c0000])

    def test_compiled_allocation_failure_keeps_targets(self):
        self.compile_run([0,0x100000,0x100004,0x100100,0x031c0000],fail=True,expect_flat=False)

    def test_compiled_excessive_span_keeps_targets(self):
        self.compile_run([0x100000+i*0xfffff for i in range(10)],expect_flat=False)


if __name__ == '__main__': unittest.main()
