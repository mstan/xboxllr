"""Shared title-independent plan resolution and bounds."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import native_visual_mods as v


class VisualResolverTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.plan=self.root/'mods.plan'
        self.data=dict(title=dict(xbe_sha256='a'*64),mods=dict(registry='visuals.c'),launcher=dict(frame_modes=['interpolated']))

    def test_all_quality_controls_and_exclusive_frame_operation(self):
        result=v.resolve(self.root/'title.toml',self.data,self.plan,height=2160,supersample=2,
                         antialiasing='fxaa',anisotropy=16,frame_mode='interpolated',frame_rate=120)
        self.assertEqual(result['internal_canvas'],[5760,4320])
        self.assertEqual(self.plan.read_text().count('operation frame-rate'),1)
        self.assertIn('operation frame-rate 1 1 120',self.plan.read_text())
        self.assertEqual(len(result['catalog_sha256']),2)

    def test_invalid_selection_cannot_replace_prior_plan(self):
        self.plan.write_text('retained prior selection')
        for options in [dict(frame_mode='actual'),dict(height=True),dict(anisotropy=3),dict(aspect='64:9'),
                        dict(aspect='21:9'),dict(frame_rate=144)]:
            with self.subTest(options=options),self.assertRaises(ValueError):
                v.resolve(self.root/'title.toml',self.data,self.plan,**options)
            self.assertEqual(self.plan.read_text(),'retained prior selection')

    def test_original_is_empty_policy(self):
        result=v.resolve(self.root/'title.toml',self.data,self.plan)
        self.assertEqual(result['operations'],[])
        self.assertEqual(result['catalog_sha256'],{})
        self.assertEqual(self.plan.read_text(),'xlle-mod-plan 2\nidentity '+'a'*64+'\nend\n')

    def test_title_aspect_identity_and_wide_supersampling_limit(self):
        pid,plugin='authored.enhancement.widescreen','authored.widescreen'
        self.data['title']['game_id']='1234ABCD'
        self.data['mods'].update(widescreen_manifest='wide.toml',widescreen_package=pid,widescreen_plugin=plugin)
        (self.root/'wide.toml').write_text('''format_version=1
id="authored.enhancement.widescreen"
version="1.0.0"
[[target]]
game_id="1234ABCD"
xbe_sha256="'''+('a'*64)+'''"
[[feature]]
id="widescreen"
default_enabled=false
[[plugin]]
feature="widescreen"
id="authored.widescreen"
''')
        r=v.resolve(self.root/'title.toml',self.data,self.plan,aspect='adaptive',height=1080)
        self.assertEqual(r['internal_canvas'],[1920,1080])
        self.assertIn('operation aspect 1 authored.widescreen 16 9 1',self.plan.read_text())
        retained=self.plan.read_bytes()
        with self.assertRaisesRegex(ValueError,'texture limit'):
            v.resolve(self.root/'title.toml',self.data,self.plan,aspect='32:9',height=2160,supersample=3)
        self.assertEqual(self.plan.read_bytes(),retained)
        self.data['title']['game_id']='99999999'
        with self.assertRaisesRegex(ValueError,'catalog'):
            v.resolve(self.root/'title.toml',self.data,self.plan,aspect='21:9')
        self.assertEqual(self.plan.read_bytes(),retained)


if __name__=='__main__':unittest.main()
