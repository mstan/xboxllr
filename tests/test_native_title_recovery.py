"""Transactional native recovery around authored source and a compiler seam."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_title as n
from native_function_extent import apply_extents


class NativeRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.output=self.root/'build/generated';(self.output/'gen').mkdir(parents=True)
        self.xbe=self.root/'authored.xbe';self.xbe.write_bytes(b'\xc3')
        self.image={'sha256':n.digest(self.xbe),'sections':[{'address':0x10000,'raw_offset':0,'raw_size':1,'flags':4,'name':'.text'}]}
        profile=self.root/'profile.json';profile.write_text(json.dumps({'schema':'native-title-profile.v1','xbe_sha256':self.image['sha256']}))
        self.source=self.root/'title.toml'
        self.source.write_text('schema="xboxllr.native-title.v1"\n[title]\nid="authored"\nname="Authored"\nexecutable="Authored"\nxbe_sha256="'+self.image['sha256']+'"\n[generation]\nprofile="profile.json"\nbindings=["cpu"]\n[build]\n')
        _,self.data=n.definition(self.source)
        for name in ('recomp_0000.c','recomp_dispatch.c'):(self.output/'gen'/name).write_text('/* authored retained '+name+' */\n')
        (self.output/'disasm').mkdir();(self.output/'disasm/functions.json').write_text('[]')
        self.manifest=self.output/'generation.aot-private.json'
        self.manifest.write_text(json.dumps({'sha256':self.image['sha256'],'cpu_binding':{'authored':'preserved'},
            'files':{p.name:n.digest(p) for p in (self.output/'gen').iterdir()}}))
        n.write_runtime_profile(self.output,self.data)
        (self.output/'native-title-preparation.json').write_text(json.dumps(n.preparation_identity(self.source,self.data,self.output)))
        self.paths={'xbe':self.xbe,'cpu_donor':self.root/'donor','xdk_symbols':self.root/'symbols.json'}
        self.paths['cpu_donor'].mkdir();self.paths['xdk_symbols'].write_text('[]')
        self.record={'start':0x10000,'original_end':0x10000,'end':0x10001,'sha256':hashlib.sha256(b'\xc3').hexdigest(),'reason':'authored indirect root','new_entry':True}
        self.plan=self.root/'recovery.json';self.write_plan()

    def write_plan(self):
        self.plan.write_text(json.dumps({'schema':'xboxllr.native-function-recovery.v1','sha256':self.image['sha256'],'extents':[self.record]}))

    def inventory(self):
        return {str(p.relative_to(self.output)):p.read_bytes() for p in self.output.rglob('*') if p.is_file()}

    def lower(self,args):
        self.assertNotEqual(args.output,self.output)
        value=json.loads((args.output/'generation.aot-private.json').read_text())
        entry=args.output/'gen/recomp_entry_00010000.c';entry.write_text('/* authored compiled return */\n')
        (args.output/'gen/recomp_dispatch.c').write_text('/* authored dispatch plus reviewed entry */\n')
        (args.output/'disasm/functions.json').write_text(json.dumps(apply_extents(b'\xc3',self.image,[],[self.record])))
        (args.output/'function-extents.aot-private.json').write_text(json.dumps({'sha256':self.image['sha256'],'extents':[self.record]}))
        value['function_extents']=[self.record]
        value['files']={p.name:n.digest(p) for p in (args.output/'gen').iterdir()}
        (args.output/'generation.aot-private.json').write_text(json.dumps(value))
        return dict(self.record)

    def run_recovery(self,lower=None,binder=None):
        original_import=n.importlib.import_module;module=Mock(bind=Mock(side_effect=binder))
        def selected_import(name,package=None):
            return module if name in n.BINDINGS.values() else original_import(name,package)
        with patch('aot_inventory.parse_xbe',return_value=self.image),patch('native_function_extent.recover',side_effect=lower or self.lower),\
             patch.object(n.importlib,'import_module',side_effect=selected_import):
            return n.recover_prepared(self.source,self.data,self.paths,self.output,self.plan)

    def test_publishes_validated_entry_preserves_other_units_and_is_idempotent(self):
        old=(self.output/'gen/recomp_0000.c').read_bytes();previous=self.manifest.read_bytes()
        result=self.run_recovery()
        self.assertEqual(result['recovered'],1)
        self.assertEqual((self.output/'gen/recomp_0000.c').read_bytes(),old)
        self.assertEqual((Path(result['history'])/'before/generation.aot-private.json').read_bytes(),previous)
        n.identity(self.source,self.data,self.output)
        before=self.inventory();self.assertTrue(self.run_recovery()['idempotent']);self.assertEqual(before,self.inventory())

    def test_wrong_original_hash_rejects_before_compilation_or_live_mutation(self):
        self.record['sha256']='0'*64;self.write_plan();before=self.inventory()
        compiler=Mock(side_effect=AssertionError('compiler must not run'))
        with self.assertRaisesRegex(ValueError,'bytes changed'):self.run_recovery(compiler)
        compiler.assert_not_called();self.assertEqual(before,self.inventory())

    def test_explicit_default_plan_fields_remain_canonical_and_idempotent(self):
        plan=json.loads(self.plan.read_text())
        plan['extents'][0].update(terminal='return',contained_entries=[])
        self.plan.write_text(json.dumps(plan))
        self.assertEqual(self.run_recovery()['recovered'],1)
        before=self.inventory()
        self.assertTrue(self.run_recovery()['idempotent'])
        self.assertEqual(before,self.inventory())

    def test_rebinding_line_endings_cannot_invalidate_an_unchanged_cpu_unit(self):
        source=self.output/'gen/recomp_0000.c';source.write_bytes(b'/* authored retained CPU */\r\n')
        value=json.loads(self.manifest.read_text());value['files'][source.name]=n.digest(source)
        self.manifest.write_text(json.dumps(value))
        (self.output/'native-title-preparation.json').write_text(json.dumps(n.preparation_identity(self.source,self.data,self.output)))
        original=source.read_bytes()
        def normalize(xbe,output,symbols,**kwargs):
            path=output/'gen/recomp_0000.c';path.write_bytes(path.read_bytes().replace(b'\r\n',b'\n'))
            value=json.loads((output/'generation.aot-private.json').read_text());value['files'][path.name]=n.digest(path)
            (output/'generation.aot-private.json').write_text(json.dumps(value))
        result=self.run_recovery(binder=normalize)
        self.assertEqual(source.read_bytes(),original)
        self.assertNotIn('gen/'+source.name,result['changed_files'])

    def test_compiler_failure_cannot_change_live_preparation(self):
        before=self.inventory();stages=[]
        def broken(args):
            stages.append(args.output);self.lower(args);raise RuntimeError('authored lowering failure')
        with self.assertRaisesRegex(RuntimeError,'lowering failure'):self.run_recovery(broken)
        self.assertEqual(before,self.inventory());self.assertFalse(stages[0].exists())
        n.identity(self.source,self.data,self.output)

    def test_changed_service_admission_rejects_staged_output(self):
        before=self.inventory()
        def drift(xbe,output,symbols,**kwargs):
            value=json.loads((output/'generation.aot-private.json').read_text());value['cpu_binding']={'authored':'changed'}
            (output/'generation.aot-private.json').write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError,'service admissions'):self.run_recovery(binder=drift)
        self.assertEqual(before,self.inventory())

    def test_modified_live_generation_is_not_implicitly_adopted(self):
        value=json.loads(self.manifest.read_text());value['cpu_binding']['authored']='changed'
        self.manifest.write_text(json.dumps(value))
        compiler=Mock(side_effect=AssertionError('compiler must not run'))
        with self.assertRaisesRegex(ValueError,'identity changed'):self.run_recovery(compiler)
        compiler.assert_not_called()

    def test_interrupted_publication_keeps_explicit_launch_blocker_and_backup(self):
        replace=n.os.replace;calls=[]
        def interrupted(source,target):
            calls.append(target)
            if len(calls)==2:raise OSError('authored publish interruption')
            replace(source,target)
        with patch.object(n.os,'replace',side_effect=interrupted),self.assertRaisesRegex(OSError,'publish interruption'):
            self.run_recovery()
        marker=json.loads((self.output/n.INCOMPLETE_RECOVERY).read_text())
        self.assertTrue((Path(marker['history'])/'before/generation.aot-private.json').exists())
        with self.assertRaisesRegex(ValueError,'publication is incomplete'):
            n.preparation_identity(self.source,self.data,self.output)


if __name__=='__main__':unittest.main()
