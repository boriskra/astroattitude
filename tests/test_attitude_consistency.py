"""Independent analytic controls, with synthetic directions only."""
from dataclasses import FrozenInstanceError
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from diagnostics.attitude_consistency import compare_pair_geometry, summarize_direction_series


def ray(angle):
    return (math.sin(angle), 0., math.cos(angle))


class ConsistencyControls(unittest.TestCase):
    def test_rotation_preserves_pair_geometry(self):
        refs = [ray(.1), ray(.5), ray(-.2), (0., 1., 0.)]
        # Independent proper axis permutation (x,y,z) -> (z,x,y).
        measured = [(z, x, y) for x, y, z in refs]
        r = compare_pair_geometry([2**64-1, 0, 7, 3], measured, refs)
        self.assertEqual(len(r.pairs), 6)
        self.assertEqual(r.pairs[0].source_ids, (0, 3))
        self.assertLess(r.max_abs_error_rad, 1e-15)
        self.assertFalse(r.physical_accuracy_qualified)
        with self.assertRaises(FrozenInstanceError):r.rms_error_rad = 1
        order = [2, 0, 3, 1]
        self.assertEqual(r, compare_pair_geometry([[2**64-1, 0, 7, 3][i] for i in order],
                         [measured[i] for i in order], [refs[i] for i in order]))

    def test_field_error_cannot_be_hidden_by_attitude_rotation(self):
        # Known planar angular bias of .02 rad on just one source.
        reference = [ray(0), ray(.3), ray(.7)]
        measured = [ray(.4), ray(.7), ray(1.12)]
        r = compare_pair_geometry([1, 2, 3], measured, reference)
        for p, expected in zip(r.pairs, [0, .02, .02]):
            self.assertAlmostEqual(p.error_rad, expected, places=14)
        self.assertAlmostEqual(r.rms_error_rad, .02*math.sqrt(2/3), places=14)

    def test_tiny_angle_and_antipodal_controls(self):
        r = compare_pair_geometry([0, 1], [ray(0), ray(1e-12)], [ray(0), ray(0)])
        self.assertAlmostEqual(r.pairs[0].error_rad / 1e-12, 1, places=12)
        r = compare_pair_geometry([0, 1], [ray(0), (0, 0, -1)], [ray(0), ray(0)])
        self.assertEqual(r.pairs[0].error_rad, math.pi)

    def test_analytic_scatter_and_long_span_bias(self):
        # ±a scatter around first direction, same scatter around a biased second.
        a, bias = .001, .02
        dirs = [ray(-a), ray(a)]*3 + [ray(bias-a), ray(bias+a)]*3
        r = summarize_direction_series(['a']*6+['b']*6, list(range(12)), dirs)
        for block in r.blocks:
            self.assertAlmostEqual(block.rms_scatter_rad, a, places=14)
            self.assertAlmostEqual(block.alternating_half_separation_rad, 2*a, places=14)
        self.assertAlmostEqual(r.max_shift_from_first_rad, bias, places=14)
        self.assertFalse(r.physical_accuracy_qualified)
        # Duplicating correlated observations must not shrink empirical scatter.
        dup = summarize_direction_series(['a']*12, list(range(12)), dirs[:6]*2)
        self.assertAlmostEqual(dup.blocks[0].rms_scatter_rad, a, places=14)
        self.assertIsNone(dup.max_shift_from_first_rad)

    def test_north_wrap_has_no_azimuth_discontinuity(self):
        a = 1e-4
        dirs = [(math.cos(a), math.sin(a), 0), (math.cos(a), -math.sin(a), 0)]*3
        r = summarize_direction_series(['x']*6, list(range(6)), dirs)
        self.assertAlmostEqual(r.blocks[0].rms_scatter_rad, a, places=14)
        self.assertEqual(r.blocks[0].mean_direction, (1, 0, 0))

    def test_failures_preserved_and_halves_use_original_rows(self):
        dirs = [ray(0), None, ray(.002), ray(.004), ray(.006)]
        r = summarize_direction_series(['a']*5, list(range(5)), dirs, minimum_block_samples=4)
        block = r.blocks[0]
        self.assertEqual((block.attempted_count, block.accepted_count), (5, 4))
        self.assertEqual(block.missing_row_indices, (1,))
        # First half is original rows 0,2,4, not compacted accepted rows 0,3.
        mean_even = math.atan2(sum(math.sin(x) for x in [0,.002,.006]),
                               sum(math.cos(x) for x in [0,.002,.006]))
        self.assertAlmostEqual(block.alternating_half_separation_rad, .004-mean_even, places=14)
        r = summarize_direction_series(['a']*5, list(range(5)), dirs)
        self.assertEqual(r.blocks[0].status, 'insufficient_samples')
        self.assertIsNone(r.blocks[0].mean_direction)
        self.assertIsNone(r.reference_block_id)

    def test_all_missing_ambiguous_and_absent_half(self):
        for dirs, status in [([None]*6, 'insufficient_samples'),
                             ([(0,0,1),(0,0,-1)]*3, 'ambiguous_mean')]:
            r = summarize_direction_series(['a']*6, list(range(6)), dirs)
            self.assertEqual(r.blocks[0].status, status)
            self.assertIsNone(r.blocks[0].rms_scatter_rad)
            self.assertIsNone(r.max_shift_from_first_rad)
        dirs = [(0,0,1), None]*6
        r = summarize_direction_series(['a']*12, list(range(12)), dirs)
        self.assertIsNone(r.blocks[0].alternating_half_separation_rad)
        self.assertEqual(r.blocks[0].rms_scatter_rad, 0)

    def test_insufficient_first_block_does_not_create_zero_reference(self):
        r = summarize_direction_series(['a']*2+['b']*6+['c']*6, list(range(14)),
                                       [None]*2+[ray(.1)]*6+[ray(.3)]*6)
        self.assertEqual(r.reference_block_id, 'b')
        self.assertIsNone(r.blocks[0].shift_from_first_admitted_rad)
        self.assertAlmostEqual(r.max_shift_from_first_rad, .2)

    def test_invalid_pair_inputs_and_budget(self):
        for ids in [[], [1], [1,1], [True,2], [-1,2], [0,2**64], list(range(65))]:
            with self.assertRaises(ValueError):
                compare_pair_geometry(ids, [(1,0,0)]*len(ids), [(1,0,0)]*len(ids))
        for u in [(0,0,0),(1,1,0),(True,0,0),(math.nan,0,0),(1<<4096,0,0),(1,0)]:
            with self.assertRaises(ValueError):compare_pair_geometry([0,1], [u,(1,0,0)], [(1,0,0)]*2)
        with self.assertRaises(ValueError):compare_pair_geometry([0,1], [(1,0,0)], [(1,0,0)]*2)

    def test_invalid_series_and_budget(self):
        for ids, times, dirs in [([],[],[]), (['a','b','a'],[0,1,2],[ray(0)]*3),
                (['a']*2,[0,0],[ray(0)]*2), (['a']*2,[1,0],[ray(0)]*2),
                ([''],[0],[None]), (['a'],[math.inf],[None]), (['a'],[True],[None]),
                (['a'],[1<<4096],[None]), (['a'],[0],[(0,0,0)]),
                (['a']*20001,list(range(20001)),[None]*20001), (['a'],[],[None])]:
            with self.assertRaises(ValueError):summarize_direction_series(ids,times,dirs)
        for minimum in [True,1,0,20001,6.0]:
            with self.assertRaises(ValueError):summarize_direction_series(['a'],[0],[None],minimum_block_samples=minimum)


if __name__ == '__main__':unittest.main()
