"""Adversarial publication controls use disposable synthetic file trees only."""
import importlib.util,json,pathlib,subprocess,tempfile,unittest
spec=importlib.util.spec_from_file_location('gate',pathlib.Path(__file__).resolve().parents[1]/'tools/check_publication.py')
gate=importlib.util.module_from_spec(spec);spec.loader.exec_module(gate)
class Publication(unittest.TestCase):
    def test_inventory_links_credentials_and_symlinks(self):
        with tempfile.TemporaryDirectory() as d:
            r=pathlib.Path(d);subprocess.run(['git','init','-q',str(r)],check=True)
            (r/'README.md').write_text('# Synthetic\n')
            def inventory(names):
                (r/'publication-files.json').write_text(json.dumps({'files':{n:{'purpose':'synthetic test'} for n in names},'synthetic_provenance':{'captured_inputs':False,'generators':[]}}))
            inventory(['README.md']);self.assertEqual(gate.check(r),[])
            (r/'surprise.txt').write_text('ordinary');self.assertTrue(gate.check(r));(r/'surprise.txt').unlink()
            (r/'README.md').write_text('[bad](../missing.md)');self.assertTrue(gate.check(r))
            (r/'README.md').write_text('gh'+'p_'+'X'*36);self.assertTrue(gate.check(r))
            (r/'README.md').write_text('# Synthetic\n[bad](#absent)');self.assertTrue(gate.check(r))
            (r/'README.md').write_text('# Synthetic\n[good](#synthetic)');self.assertEqual(gate.check(r),[])
            (r/'link').symlink_to('/tmp');inventory(['README.md','link']);self.assertTrue(gate.check(r))
    def test_binary_hash_and_media(self):
        with tempfile.TemporaryDirectory() as d:
            r=pathlib.Path(d);subprocess.run(['git','init','-q',str(r)],check=True)
            (r/'photo.png').write_bytes(b'not a photograph')
            (r/'publication-files.json').write_text(json.dumps({'files':{'photo.png':{'purpose':'test'}},'synthetic_provenance':{'captured_inputs':False}}))
            self.assertTrue(gate.check(r))
            (r/'photo.png').unlink()
            jar=r/'bindings/android/gradle/wrapper/gradle-wrapper.jar';jar.parent.mkdir(parents=True);jar.write_bytes(b'synthetic binary\x00')
            import hashlib
            inventory={'files':{str(jar.relative_to(r)):{'purpose':'synthetic gate control','binary_sha256':hashlib.sha256(jar.read_bytes()).hexdigest()}},'synthetic_provenance':{'captured_inputs':False}}
            (r/'publication-files.json').write_text(json.dumps(inventory));self.assertEqual(gate.check(r),[])
            jar.write_bytes(b'changed');self.assertTrue(gate.check(r))
if __name__=='__main__':unittest.main()
