"""Unit controls for synthetic catalog-adapter behavior; they do not certify physical images."""
import math
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from diagnostics.night_catalog import exif_exposure, pixel_request, acceleration_comparison, relative_gravity_comparison


class NightCatalogControls(unittest.TestCase):
    def test_offset_and_subsecond_are_not_rounded_or_double_applied(self):
        value = {'DateTimeOriginal': '2000:01:01 00:00:00', 'OffsetTimeOriginal': '-07:00',
                 'SubsecTimeOriginal': '100', 'ExposureTime': (1, 15)}
        actual = exif_exposure(value)
        self.assertEqual(actual['midpoint'], {'unix_seconds': '946710000', 'nanoseconds': 100000000})
        self.assertAlmostEqual(actual['duration_s'], 1/15)
        self.assertIsNone(actual['midpoint_sigma_s'])
        value['OffsetTimeOriginal'] = '+00:00'
        self.assertEqual(int(exif_exposure(value)['midpoint']['unix_seconds']), 946710000-7*3600)

    def test_missing_or_bad_time_never_uses_sidecar_or_system_clock(self):
        value = {'DateTimeOriginal': '2000:01:01 00:00:00', 'OffsetTimeOriginal': '-07:00',
                 'SubsecTimeOriginal': '100', 'ExposureTime': (1, 15)}
        for key in ('DateTimeOriginal', 'OffsetTimeOriginal', 'ExposureTime'):
            bad = dict(value); del bad[key]
            with self.assertRaises(KeyError): exif_exposure(bad)
        for subsec in ('1.5', '1234567890', '', '١'):
            with self.assertRaises(ValueError): exif_exposure(dict(value, SubsecTimeOriginal=subsec))
        for exposure in (0, -1, math.nan, math.inf):
            with self.assertRaises(ValueError): exif_exposure(dict(value, ExposureTime=exposure))

    def test_caller_request_has_no_synthetic_identity_noise_or_imu_prior(self):
        exp = {'midpoint': {'unix_seconds': '946710000', 'nanoseconds': 100000000},
               'duration_s': 1, 'midpoint_sigma_s': None}
        observer = {'latitude_rad': .2, 'longitude_rad': .3, 'ellipsoid_height_m': 40}
        q = pixel_request('synthetic-input', [[12, 23], [45, 56]], 640, 480, exp,
                          {'fx': 500, 'fy': 500, 'cx': 320, 'cy': 240}, observer)
        self.assertEqual(q['context']['observer'], observer)
        self.assertEqual(q['observation']['identity']['exposure'], exp)
        self.assertIn('unqualified', q['calibration']['revision'])
        self.assertEqual(q['calibration']['model']['k1'], 0)  # declared assumption
        self.assertEqual(q['observation']['stars'][1]['pixel'], {'x': 45, 'y': 56})
        self.assertIsNone(q['observation']['stars'][0]['centroid_covariance_px2'])
        self.assertNotIn('gravity', q['context'])
        self.assertNotIn('acceleration', q['context'])

    def test_no_tilt_or_yaw_error_without_alignment_even_after_a_solve(self):
        for accepted in (False, True):
            result = acceleration_comparison([.6, 0, .8], accepted)
            self.assertEqual(result['norm_g'], 1)
            self.assertIsNone(result['tilt_residual_deg'])
            self.assertIsNone(result['yaw_residual_deg'])
            self.assertFalse(result['qualified'])
            self.assertEqual('no_accepted_star_solution' in result['reasons'], not accepted)

    def test_independent_rotation_and_gravity_control(self):
        identity = [[1,0,0],[0,1,0],[0,0,1]]
        theta = math.radians(10)
        rotation = [[math.cos(theta),-math.sin(theta),0], [math.sin(theta),math.cos(theta),0], [0,0,1]]
        actual = relative_gravity_comparison([identity, rotation], [[1,0,0], [0,1,0]], 0)
        self.assertAlmostEqual(actual['optical_rotation_deg'], 10)
        self.assertAlmostEqual(actual['acceleration_direction_change_deg'], 90)
        self.assertAlmostEqual(actual['conditional_minimum_discrepancy_deg'], 80)
        self.assertFalse(actual['qualified'])
        self.assertIsNone(actual['absolute_tilt_error_deg'])

    def test_unknown_fixed_mounting_misalignment_preserves_relative_comparison(self):
        identity = [[1,0,0],[0,1,0],[0,0,1]]
        vectors = [[.1,.2,.9],[-.15,.27,.93]]
        angle = math.radians(37)
        c, s = math.cos(angle), math.sin(angle)
        tilted = [[x,c*y-s*z,s*y+c*z] for x,y,z in vectors]
        before = relative_gravity_comparison([identity,identity], vectors, .173)
        after = relative_gravity_comparison([identity,identity], tilted, .173)
        self.assertAlmostEqual(before['acceleration_direction_change_deg'],
                               after['acceleration_direction_change_deg'], places=10)
        self.assertAlmostEqual(before['conditional_minimum_discrepancy_deg'],
                               after['conditional_minimum_discrepancy_deg'], places=10)
        self.assertIsNone(after['absolute_tilt_error_deg'])
        self.assertFalse(after['qualified'])

    def test_relative_gravity_bound_is_axis_invariant_and_rejects_reflection(self):
        identity = [[1,0,0],[0,1,0],[0,0,1]]
        rz90 = [[0,-1,0],[1,0,0],[0,0,1]]
        for vectors in ([[1,0,0],[0,1,0]], [[-1,0,0],[0,-1,0]], [[0,0,1],[1,0,0]]):
            result = relative_gravity_comparison([identity, rz90], vectors, 0)
            self.assertAlmostEqual(result['optical_rotation_deg'], 90)
            self.assertAlmostEqual(result['acceleration_direction_change_deg'], 90)
            self.assertEqual(result['conditional_minimum_discrepancy_deg'], 0)
            self.assertFalse(result['qualified'])
        result = relative_gravity_comparison([identity, identity], [[1,0,0],[0,1,0]], 0)
        self.assertEqual(result['conditional_minimum_discrepancy_deg'], 90)
        for bad in ([[1,0,0],[0,1,0],[0,0,-1]], [[2,0,0],[0,1,0],[0,0,1]]):
            with self.assertRaises(ValueError):
                relative_gravity_comparison([identity,bad], [[1,0,0],[1,0,0]], 0)


if __name__ == '__main__': unittest.main()
