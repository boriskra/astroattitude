"""Independent synthetic exposure controls; no private images or coefficients."""
import math
from pathlib import Path
import random
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from diagnostics.exposure_fusion import ExposureSample as S, fuse_samples


class ExposureFusionControls(unittest.TestCase):
    def test_dynamic_range_and_clipped_long_exposure(self):
        result = fuse_samples([S(1000, 1, 4, 990), S(10, .01, .04, 990)])
        self.assertEqual(result.status, "single_exposure")
        self.assertEqual(result.value, 1000)
        self.assertEqual(result.rejected, (0,))
        self.assertAlmostEqual(result.conditional_variance, 400)

    def test_weighted_solution_and_exposure_scale(self):
        # Scene estimates 10 and 12, variances 1 and 4: analytic answer 10.4,0.8.
        result = fuse_samples([S(20, 2, 4, 100), S(120, 10, 400, 200)])
        self.assertEqual(result.status, "fused")
        self.assertAlmostEqual(result.value, 10.4)
        self.assertAlmostEqual(result.conditional_variance, .8)
        self.assertAlmostEqual(result.consistency_chi_square, .8)

    def test_negative_noise_not_clipped_or_brightened(self):
        result = fuse_samples([S(-2, 1, 4, 100), S(2, 1, 4, 100)])
        self.assertEqual(result.value, 0)
        self.assertEqual(result.conditional_variance, 2)

    def test_registration_or_response_inconsistency_fails_closed(self):
        result = fuse_samples([S(10, 1, 1, 1000), S(100, 1, 1, 1000)])
        self.assertEqual(result.status, "inconsistent")
        self.assertIsNone(result.value)
        self.assertIsNone(result.conditional_variance)

    def test_masks_and_all_clipped(self):
        result = fuse_samples([S(10, 1, 1, 100, False), S(100, 1, 1, 100)])
        self.assertEqual(result.status, "no_usable_samples")
        self.assertIsNone(result.value)
        self.assertEqual(result.rejected, (0, 1))

    def test_independent_noise_trials(self):
        rng = random.Random(20261025)
        estimates = []
        for _ in range(4000):
            result = fuse_samples([S(50 + rng.gauss(0, 2), 1, 4, 1000),
                                   S(200 + rng.gauss(0, 4), 4, 16, 1000)],
                                  max_reduced_chi_square=100)
            estimates.append(result.value)
        mean = sum(estimates) / len(estimates)
        variance = sum((x - mean) ** 2 for x in estimates) / (len(estimates) - 1)
        self.assertLess(abs(mean - 50), .05)
        self.assertLess(abs(variance - .8), .06)

    def test_invalid_and_bounded_inputs(self):
        for samples in [[], [S(1, 1, 1, 100)] * 33,
                        [S(math.nan, 1, 1, 100)], [S(True, 1, 1, 100)],
                        [S(1<<4096, 1, 1, 100)], [S('1', 1, 1, 100)],
                        [S(1, 0, 1, 100)],
                        [S(1, 1, 0, 100)], [S(1, 1, 1, -1)],
                        [S(1, 1, 1, 100, 1)], [S(1, 1e-300, 1, 100)]]:
            with self.assertRaises(ValueError):
                fuse_samples(samples)
        for threshold in [0, math.nan, math.inf, True, '1', 1<<4096]:
            with self.assertRaises(ValueError):
                fuse_samples([S(1, 1, 1, 100)], max_reduced_chi_square=threshold)

    def test_overflowing_inconsistency_never_passes_large_gate(self):
        r = fuse_samples([S(1e200, 1, 1, 1e300), S(-1e200, 1, 1, 1e300),
                          S(0, 1, 1, 1e300)], max_reduced_chi_square=1e308)
        self.assertEqual(r.status, 'inconsistent')
        self.assertIsNone(r.value)
        self.assertIsNone(r.conditional_variance)


if __name__ == '__main__':
    unittest.main()
