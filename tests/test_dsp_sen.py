"""Closed SEN contract, source seams and actual single/block CPU terminal guards."""
import json,os,subprocess,sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parents[1]/'tools'))
from native_dsp_donor import guarded
from native_dsp_sen import FILES
ROOT=Path(__file__).parents[1]
class SenTests(unittest.TestCase):
    def test_pinned_source_mutation_rejected(self):
        core=(ROOT.parent/'refs/dsp56300-mborgerson-review-20261007/crates/emu/src/core.rs').read_text(encoding='utf-8')
        with self.assertRaises(ValueError):guarded('crates/emu/src/core.rs',core.replace('    pub fn stack_push(&mut self, ssh_val: u32, ssl_val: u32) {','    pub fn changed_stack_push() {'))
        for name in FILES:guarded(name,(ROOT.parent/'refs/dsp56300-mborgerson-review-20261007'/name).read_text(encoding='utf-8'))
    def checker(self,key):
        value=os.environ.get(key)
        if not value:self.skipTest('set '+key)
        return value
    def test_actual_single_and_block_unsupported_forms_stop(self):
        checker=self.checker('DSP_SEN_CHECK')
        modes={'sp':'direct SP','sc':'task/configuration','sz':'task/configuration','ep':'task/configuration','ssh-write':'explicit','ssl-write':'explicit','ssh-read':'explicit','ssl-read':'explicit','underflow':'underflow/refill','enddo':'explicit/early-abort','spill':'spill boundary','disable-occupied':'occupied mode transition','sz-overflow':'SZ overflow','enable-unset-sz':'unset SZ','status-history':'status/history'}
        for mode,message in modes.items():
            for execution in ('single','block'):
                with self.subTest(mode=mode,execution=execution):
                    r=subprocess.run([checker,mode,execution],capture_output=True,text=True,timeout=10)
                    self.assertNotEqual(r.returncode,0);self.assertIn('Unqualified SEN',r.stderr);self.assertIn(message,r.stderr)
    def test_empty_mode_transition_single_and_block(self):
        checker=self.checker('DSP_SEN_CHECK')
        for execution in ('single','block'):
            r=subprocess.run([checker,'empty-disable',execution],capture_output=True,text=True,timeout=10)
            self.assertEqual(r.returncode,0,r.stdout+r.stderr);self.assertIn('Empty SEN enable/disable preserved',r.stdout)
    def test_original_prefix_depth_workspace_and_real_c5_boundary(self):
        checker=self.checker('DSP_SEN_CHECK')
        image=ROOT/'build/blinx/audio-ep-startup-context-audit-20261010/ep-mapped.aot-private.bin'
        r=subprocess.run([checker,'--original',str(image)],capture_output=True,text=True,timeout=10)
        self.assertEqual(r.returncode,2,r.stdout+r.stderr)
        self.assertEqual(json.loads(r.stdout),dict(boundary='original_EP_C5_FFF_attempt_before_yield',pc=0x2ef,sp=2,sc=2,maximum_depth=3,workspace_P_words_cleared=30,EP=0xbe0,SZ=32,extension_memory_unchanged=True,acknowledgement_supplied=False))
    def test_production_owner_exact_c5_boundary(self):
        checker=self.checker('DSP_SEN_OWNER_CHECK')
        image=ROOT/'build/blinx/audio-ep-startup-context-audit-20261010/ep-mapped.aot-private.bin'
        r=subprocess.run([checker,str(image)],capture_output=True,text=True,timeout=10)
        self.assertEqual(r.returncode,2,r.stdout+r.stderr);self.assertIn('SEN resident prefix reached C5=FFF terminal guard',r.stdout)
if __name__=='__main__':unittest.main()
