from pathlib import Path
import json,sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from native_mod_launcher import Catalog
from native_title_switches import declarations,manifests,environment


class TitleSwitchTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.source=self.root/'title.toml';self.state=self.root/'selection.json'
        self.switch=dict(manifest='mods/preloaded/packages/fixture.enhancement.unlock/1.0.0/manifest.toml',
                         package='fixture.enhancement.unlock',feature='early-unlock',key='EARLY_UNLOCK')
        self.data=dict(title=dict(game_id='12345678',xbe_sha256='a'*64),mods=dict(switches=[self.switch]))
        path=self.root/self.switch['manifest'];path.parent.mkdir(parents=True)
        path.write_text('''format_version=1
id="fixture.enhancement.unlock"
name="Fixture"
version="1.0.0"
description="Reviewed static title mod"
[[target]]
game_id="12345678"
xbe_sha256="'''+('a'*64)+'''"
[[feature]]
id="early-unlock"
name="Early unlock"
default_enabled=false
''')

    def test_title_default_on_and_saved_off(self):
        defaults={'fixture.enhancement.unlock/early-unlock':dict(enabled=True)}
        catalog=Catalog(manifests(self.source,self.data),self.state,defaults=defaults)
        self.assertEqual(environment(catalog,self.data),dict(XLLE_PILOT_TITLE_MOD_EARLY_UNLOCK='1'))
        catalog.features[0]['enabled']=False
        self.state.write_text(json.dumps(catalog.selection()))
        restored=Catalog(manifests(self.source,self.data),self.state,defaults=defaults)
        self.assertEqual(environment(restored,self.data),dict(XLLE_PILOT_TITLE_MOD_EARLY_UNLOCK='0'))

    def test_wrong_title_is_rejected_and_other_titles_get_nothing(self):
        self.data['title']['game_id']='87654321'
        with self.assertRaisesRegex(ValueError,'Unsupported bundled'):manifests(self.source,self.data)
        self.assertEqual(manifests(self.source,dict(mods={})),[])
        self.assertEqual(environment(Catalog([],self.state),dict(mods={})),{})

    def test_bad_keys_paths_duplicates_and_missing_selection_are_rejected(self):
        self.switch['key']='PATH'
        self.assertEqual(declarations(self.data)[0]['key'],'PATH') # Always receives the fixed prefix.
        self.switch['key']='EARLY_UNLOCK;COMMAND'
        with self.assertRaises(ValueError):declarations(self.data)
        self.switch['key']='EARLY_UNLOCK';self.switch['manifest']='../outside.toml'
        with self.assertRaises(ValueError):declarations(self.data)
        self.switch['manifest']='mods/preloaded/packages/fixture.enhancement.unlock/1.0.0/manifest.toml'
        self.data['mods']['switches'].append(dict(self.switch))
        with self.assertRaises(ValueError):declarations(self.data)
        self.data['mods']['switches'].pop()
        with self.assertRaisesRegex(ValueError,'missing'):environment(Catalog([],self.state),self.data)


if __name__=='__main__':unittest.main()
