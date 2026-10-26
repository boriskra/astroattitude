"""Portable tests of independently generated image/accelerometer controls.

No original images, GPS, real catalog/EOP or image dependencies are needed here.
The native tests deliberately use an unrelated synthetic catalog and test-only
optics/time/context. They are rejection/ABI regressions, not physical sky solves.
"""
import copy
import json
import math
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT/'bindings/python'))
from diagnostics.sky_inputs import (apple_acceleration, acceleration_summary,
                                    interpolate_acceleration, display_pixel)

from synthetic_sky import corpus
FIXTURES, ORACLE = corpus()
NATIVE = sys.argv.pop(1) if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else None
GENERATED = Path(sys.argv.pop(1)) if NATIVE else None


class SkyInputs(unittest.TestCase):
    def test_corpus_integrity_and_scientific_limits(self):
        frames = FIXTURES['frames']
        self.assertEqual(sum(f['scene'] == 'synthetic_note' for f in frames), 8)
        self.assertEqual(sum(f['scene'] == 'synthetic_sensor' for f in frames), 8)
        self.assertEqual(len({f['image']['sha256'] for f in frames}), 16)
        for f in frames:
            with self.subTest(frame=f['id']):
                image = f['image']
                self.assertEqual(len(image['sha256']), 64)
                self.assertIsNone(f['physical_attitude_truth'])
                self.assertIsNone(f['acceleration']['summary']['heading_rad'])
                self.assertIsNone(f['acceleration']['summary']['camera_attitude'])
                self.assertFalse(f['acceleration']['summary']['exposure_alignment_qualified'])
                self.assertLessEqual(len(image['candidates']), 16)
                self.assertEqual(len({p['id'] for p in image['candidates']}), len(image['candidates']))
                for p in image['candidates']:
                    self.assertTrue(0 <= p['pixel']['x'] < image['width'])
                    self.assertTrue(0 <= p['pixel']['y'] < image['height'])
                    self.assertIsNone(p['centroid_covariance_px2'])
                # No per-image GPS/UTC, identifiers, thumbnails or whole MakerNotes.
                self.assertFalse(any(k in json.dumps(f).lower() for k in
                                      ('latitude', 'longitude', 'callback_utc', 'device_uuid')))

    def test_synthetic_apple_acceleration_matches_chosen_rational_vectors(self):
        for f in FIXTURES['frames']:
            if f['scene'] != 'synthetic_note':
                continue
            with self.subTest(frame=f['id']):
                values = apple_acceleration(bytes.fromhex(f['acceleration']['tag_only_note_hex']))
                for actual, expected in zip(values, ORACLE['vectors'][f['id']]):
                    self.assertAlmostEqual(actual, expected, delta=1e-9)
                self.assertTrue(acceleration_summary(values)['near_one_g'])

    def test_makernote_owns_endianness(self):
        note = bytes.fromhex(FIXTURES['frames'][0]['acceleration']['tag_only_note_hex'])
        # Independent TIFF construction of equivalent little-endian signed rationals.
        pairs = struct.unpack_from('>iiiiii', note, 32)
        little = (b'Apple iOS\0\0\1II' + struct.pack('<HHHII', 1, 8, 10, 3, 32)
                  + b'\0'*4 + struct.pack('<iiiiii', *pairs))
        self.assertEqual(apple_acceleration(note), apple_acceleration(little))
        wrong = note[:12] + b'II' + note[14:]
        with self.assertRaises(ValueError): apple_acceleration(wrong)

    def test_corrupt_notes_do_not_manufacture_gravity(self):
        good = bytes.fromhex(FIXTURES['frames'][0]['acceleration']['tag_only_note_hex'])
        cases = [b'', good[:15], good[:-1], b'X'+good[1:], good+b'x'*65536]
        for offset, data in [(14, b'\xff\xff'), (16, b'\0\x09'), (18, b'\0\x05'),
                             (20, struct.pack('>I', 2)), (24, struct.pack('>I', 0)),
                             (24, struct.pack('>I', 2**32-1)), (36, b'\0'*4)]:
            cases.append(good[:offset]+data+good[offset+len(data):])
        duplicate = (good[:14]+struct.pack('>H', 2)
                     +struct.pack('>HHII', 8, 10, 3, 44)*2+b'\0'*4+good[32:])
        cases.append(duplicate)
        for note in cases:
            with self.subTest(size=len(note)):
                with self.assertRaises(ValueError): apple_acceleration(note)

    def test_day_image_sensor_pairing(self):
        for f in FIXTURES['frames']:
            if f['scene'] != 'synthetic_sensor': continue
            with self.subTest(frame=f['id']):
                a = f['acceleration']; samples = a['samples']
                self.assertEqual(interpolate_acceleration(samples), a['vector_g'])
                self.assertLess(samples[0]['relative_sensor_time_s'], 0)
                self.assertGreater(samples[-1]['relative_sensor_time_s'], 0)
                self.assertTrue(acceleration_summary(a['vector_g'])['near_one_g'])
                # Callback latency is recorded, not silently interpreted as exposure.
                self.assertGreater(f['callback_minus_pts_s'], .02)
                self.assertIn('unqualified_exposure', a['sample_timing'])
                self.assertEqual(len({s['source_line'] for s in samples}), len(samples))

    def test_pairing_rejects_missing_stale_and_nonmonotonic_samples(self):
        samples = next(f for f in FIXTURES['frames'] if f['scene'] == 'synthetic_sensor')['acceleration']['samples']
        for bad in [[], samples[:1], samples[::-1], [samples[0], samples[0]], samples[:2]]:
            with self.assertRaises(ValueError): interpolate_acceleration(bad)
        with self.assertRaises(ValueError): interpolate_acceleration(samples, maximum_gap_s=.001)
        with self.assertRaises(ValueError): interpolate_acceleration(samples, time_s=100)
        with self.assertRaises(ValueError): interpolate_acceleration(samples, time_s=math.nan)
        with self.assertRaises(ValueError): interpolate_acceleration(samples, maximum_gap_s=-1)
        exact = samples[2]
        self.assertEqual(interpolate_acceleration(samples, exact['relative_sensor_time_s']),
                         exact['acceleration_g'])

    def test_acceleration_does_not_observe_heading_or_camera_pose(self):
        # Same measured vector permits arbitrary yaw. No fabricated heading/extrinsic.
        for f in FIXTURES['frames']:
            summary = acceleration_summary(f['acceleration']['vector_g'])
            self.assertIsNone(summary['heading_rad'])
            self.assertIsNone(summary['camera_attitude'])
            self.assertAlmostEqual(sum(x*x for x in summary['unit_vector']), 1)
        # Norm near g does not certify stationarity; an arbitrary unit vector also passes.
        self.assertTrue(acceleration_summary([.6, .8, 0])['near_one_g'])
        self.assertFalse(acceleration_summary([2, 0, 0])['near_one_g'])
        for bad in [[0, 0, 0], [math.nan, 0, 1], [0, math.inf, 0], [1, 2], [True, 0, 1]]:
            with self.assertRaises(ValueError): acceleration_summary(bad)

    def test_display_orientation_controls(self):
        # Hand-derived nonsquare image control, all eight rotations/reflections.
        expected = [(2, 3), (8, 3), (8, 3), (2, 3), (3, 2), (3, 2), (3, 8), (3, 8)]
        for orientation in range(1, 9):
            self.assertEqual(display_pixel(2, 3, 11, 7, orientation), expected[orientation-1])
        # Off-axis control distinguishes otherwise equal y=height/2 reflections.
        self.assertEqual(display_pixel(2, 1, 11, 7, 6), (5, 2))
        self.assertEqual(display_pixel(2, 1, 11, 7, 8), (1, 8))
        for f in FIXTURES['frames']:
            im = f['image']
            for p in im['candidates']:
                x,y = display_pixel(p['pixel']['x'],p['pixel']['y'],im['width'],im['height'],im['orientation'])
                w,h = (im['height'],im['width']) if im['orientation'] >= 5 else (im['width'],im['height'])
                self.assertTrue(0 <= x < w and 0 <= y < h)
        with self.assertRaises(ValueError): display_pixel(10.5, 0, 11, 7, 1)
        with self.assertRaises(ValueError): display_pixel(0, 0, 11, 7, 0)


@unittest.skipUnless(NATIVE, 'native boundary checks require library and generated fixture paths')
class SkyNativeRejections(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from astroattitude import NativeLibrary, Engine
        cls.engine = Engine(NativeLibrary(NATIVE), (GENERATED/'catalog.json').read_bytes(),
                            (GENERATED/'astrometry.json').read_bytes())
        cls.base = json.loads((GENERATED/'pixels.json').read_text())

    @classmethod
    def tearDownClass(cls): cls.engine.close()

    def request(self, frame):
        q = copy.deepcopy(self.base); image = frame['image']; w,h=image['width'],image['height']
        q['observation'].update(width=w, height=h, stars=copy.deepcopy(image['candidates']))
        q['observation']['identity']['observation_id'] = frame['id']
        c = q['calibration']; c.update(width=w, height=h)
        c['model'] = dict(fx_px=w,fy_px=w,cx_px=(w-1)/2,cy_px=(h-1)/2,k1=0,k2=0,k3=0,p1=0,p2=0)
        c['valid_region'] = dict(min_x=0,min_y=0,max_x=w-1,max_y=h-1)
        return q

    def solve(self, q):
        from astroattitude import decode, PixelObservation, CameraCalibration, EarthContext, SolveOptions
        return self.engine.solve_pixels(decode(PixelObservation,q['observation']),
                                        decode(CameraCalibration,q['calibration']),
                                        decode(EarthContext,q['context']),decode(SolveOptions,q['options']))

    def test_all_generated_pixel_sets_reject_unrelated_synthetic_catalog(self):
        from astroattitude import SolverError, ErrorCode
        counts = {}
        for frame in FIXTURES['frames']:
            with self.subTest(frame=frame['id']):
                q = self.request(frame); result = self.solve(q)
                self.assertIsInstance(result, SolverError)
                expected = ErrorCode.InsufficientObservations if len(q['observation']['stars']) < 6 else ErrorCode.NoMatch
                self.assertEqual(result.code, expected)
                # Reordering generated candidates preserves the semantic result.
                q['observation']['stars'].reverse()
                self.assertEqual(self.solve(q).code, expected)
                counts[expected.value] = counts.get(expected.value,0)+1
        print('Generated-pixel unrelated-catalog rejection counts:', counts)

    def test_generated_pixel_calibration_and_identity_failures(self):
        from astroattitude import ErrorCode
        for frame in FIXTURES['frames']:
            if len(frame['image']['candidates']) < 6: continue
            with self.subTest(frame=frame['id']):
                q = self.request(frame);q['observation']['identity']['calibration_revision']='wrong-revision'
                self.assertEqual(self.solve(q).code,ErrorCode.CalibrationMismatch)
                q = self.request(frame);q['observation']['stars'][1]['id']=q['observation']['stars'][0]['id']
                self.assertEqual(self.solve(q).code,ErrorCode.InvalidInput)
                q = self.request(frame);q['observation']['width'],q['observation']['height']=q['observation']['height'],q['observation']['width']
                self.assertEqual(self.solve(q).code,ErrorCode.CalibrationMismatch)

    def test_acceleration_is_not_an_implicit_solver_prior(self):
        # v1 accepts measured pixels/rays, not accelerometer-derived fake stars.
        # Supplying a new field must fail strict request parsing, even for a real vector.
        import ctypes as C
        from astroattitude import ErrorCode, SolverError, decode
        f = FIXTURES['frames'][-1];q=self.request(f)
        q['observation']['acceleration_g']=f['acceleration']['vector_g']
        raw=json.dumps(q).encode();message=C.c_void_p();native=self.engine._native
        with self.engine._lease() as handle:
            status=native.lib.aa_solve_v1(handle,raw,len(raw),None,C.byref(message))
        self.assertEqual(status,1)
        result=decode(SolverError,native._message(message)['error'])
        self.assertEqual(result.code,ErrorCode.InvalidInput)


if __name__ == '__main__': unittest.main()
