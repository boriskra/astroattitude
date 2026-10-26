"""Importer controls: sealed roles, failed blocks, media hashes, and exclusive outputs."""
import hashlib
import importlib.util
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
import unittest

spec = importlib.util.spec_from_file_location('replay', Path(__file__).resolve().parents[1] / 'tools/replay_calibration.py')
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)

class Importer(unittest.TestCase):
    def test_masks_have_no_capture_defaults_and_validate_bounds(self):
        self.assertEqual(replay.explicit_mask_rectangles({},(32,48)),[])
        boxes=[[0,0,3,2],[40,20,48,32]]
        self.assertEqual(replay.explicit_mask_rectangles({'extraction_mask_rectangles':boxes},(32,48)),boxes)
        for boxes in (None, [[0,0,49,1]], [[-1,0,3,3]], [[1,1,1,2]], [[True,0,2,2]], [[0,0,1.5,2]], [[0,1,2]]):
            with self.assertRaises(ValueError):replay.explicit_mask_rectangles({'extraction_mask_rectangles':boxes},(32,48))

    def test_evidence_inventory(self):
        with TemporaryDirectory() as temporary:
            base = Path(temporary)
            source = base / 'session'
            source.mkdir()
            def write(path, value):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(json.dumps(value))
            (source / 'SESSION.md').write_text('Synthetic session')
            write(source / 'control.json', {'stop': True})
            write(source / 'fixed-view-plan.json', {'training_minutes': 90})
            labels = ['fixed-a', 'fixed-b', 'fixed-c']
            (source / 'block-journal.jsonl').write_text(''.join(json.dumps({'label': x, 'role': 'scientific', 'count': 12}) + '\n' for x in labels))
            for index, role in enumerate(['training', 'reserve_pending_freeze']):
                block = source / labels[index]
                raw = block / 'raw'
                raw.mkdir(parents=True)
                (raw / 'photo.dng').write_bytes(b'synthetic-placeholder')
                manifest = {'start_utc': f'2000-01-01T0{index + 1}:00:00Z', 'stop_utc': f'2000-01-01T0{index + 1}:01:00Z',
                    'plan': {'frame_count_limit': 12, 'focus_positions': [.5], 'requested_exposure_seconds': 1, 'requested_iso': 100},
                    'camera_configuration': {'camera_unique_id': 'synthetic-camera', 'physical_lens': 'wide', 'raw_kind': 'bayer', 'raw_pixel_format': 'synthetic'},
                    'photos': [{'file': 'photo.dng', 'bytes': 21, 'cameraUniqueID':'synthetic-camera', 'lens':'wide','rawKind':'bayer','rawPixelFormat':'synthetic','lensPosition':.5,'exposureDurationSeconds':1,'iso':100,'width':64,'height':64,'pre_capture_camera_state':{'zoom_factor':1},'settings_readback_qualified':True,'focus_capture_clock_guard_qualified':True,'unique_exposure_spacing_qualified':True}]}
                write(raw / 'night_photos.json', manifest)
                mh = replay.digest(raw / 'night_photos.json')
                write(block / 'receipt.json', {'path': str(raw.relative_to(source)), 'validation_returncode': 0})
                write(block / 'validation.json', {'manifest_sha256': mh, 'status': 'PASS', 'frames': [{'ordinal': 1, 'dng_sha256': replay.digest(raw / 'photo.dng'), 'bayer_sha256': 'unavailable-synthetic-placeholder'}]})
                write(block / 'scientific-role.json', {'role': role, 'pose_id': 'synthetic-P00', 'manifest_sha256': mh, 'plan_sha256': replay.digest(source / 'fixed-view-plan.json')})
            (source / 'fixed-b/raw/photo.dng').write_bytes(b'tampered')
            native = base / 'synthetic.dylib'
            native.write_bytes(b'placeholder-not-loaded')
            args = SimpleNamespace(session=source, output=base/'experiment', native=native, catalog=None, astrometry=None, context=None, blocks=None, visual_masks=True)
            replay.prepare(args)
            protocol = json.loads((args.output / 'protocol.json').read_text())
            self.assertEqual(len(protocol['blocks']), 3)
            self.assertEqual(sum(b['planned_count'] for b in protocol['blocks']), 36)
            self.assertEqual(protocol['blocks'][0]['allocation'], 'development')
            self.assertEqual(protocol['blocks'][1]['role'], 'reserve_pending_freeze')
            self.assertEqual(protocol['blocks'][1]['allocation'], 'evaluation')
            self.assertEqual(protocol['blocks'][1]['start_gap_seconds'], 3600)
            self.assertEqual(protocol['blocks'][1]['frames'][0]['status'], 'verification_failed')
            self.assertTrue(protocol['blocks'][2]['failures'])
            self.assertIn('missing_scheduled_frames', protocol['blocks'][0]['failures'])
            self.assertEqual(protocol['variants'], ['automatic', 'historical_visual_mask'])
            with self.assertRaises(FileExistsError): replay.prepare(args)
            with self.assertRaises(ValueError): replay.child(source, '../outside')

if __name__ == '__main__': unittest.main()
