"""Independent synthetic calibration/observability controls; no field data."""
import math
from pathlib import Path
import random
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from diagnostics.calibration import (assess_linearized_calibration,
                                    rotation_observability, split_time_blocks)


class CalibrationControls(unittest.TestCase):
    def test_three_axes_analytic_information_and_noise(self):
        result = rotation_observability([(1,0,0),(0,1,0),(0,0,1)],
                                        tangent_sigma_rad=[.01]*3)
        self.assertEqual(result.effective_rank, 3)
        for x in result.information_eigenvalues_scaled:
            self.assertAlmostEqual(x, 20000.)
        for i in range(3):
            for j in range(3):
                self.assertAlmostEqual(result.covariance_parameter_units[i][j],
                                       .00005 if i == j else 0.)
        self.assertFalse(result.physical_accuracy_qualified)

    def test_single_pose_gravity_never_becomes_full_alignment(self):
        for count in (1, 100):
            r = rotation_observability([(0,0,1)]*count, tangent_sigma_rad=[.001]*count)
            self.assertEqual(r.effective_rank, 2)
            self.assertEqual(r.information_eigenvalues_scaled[0], 0)
            self.assertIsNone(r.covariance_parameter_units)
            self.assertAlmostEqual(abs(r.weakest_mode_scaled[2]), 1)

    def test_long_arc_improves_weak_axis_not_systematic_qualification(self):
        def arc(angle):
            return [(math.sin(angle*i/12), 0, math.cos(angle*i/12)) for i in range(13)]
        short = rotation_observability(arc(math.radians(.01)))
        long = rotation_observability(arc(math.radians(15)))
        self.assertLess(short.effective_rank, 3)
        self.assertEqual(long.effective_rank, 3)
        self.assertGreater(long.information_eigenvalues_scaled[0],
                           100000*short.information_eigenvalues_scaled[0])
        self.assertIsNone(long.covariance_parameter_units)  # no invented noise
        self.assertEqual(long.covariance_status, 'measurement_noise_not_supplied')

    def test_nuisance_coupling_not_hidden_by_pseudoinverse(self):
        args = dict(parameter_names=('angle','offset'), parameter_scales=(1,1))
        r = assess_linearized_calibration([[1,1],[2,2]], residual_sigma=[1,1], **args)
        self.assertEqual(r.effective_rank, 1)
        self.assertIsNone(r.covariance_parameter_units)
        # Independent analytic inverse of [[2,1],[1,1]] is [[1,-1],[-1,2]].
        r = assess_linearized_calibration([[1,0],[1,1]], residual_sigma=[1,1], **args)
        for got, expected in zip(r.covariance_parameter_units, ((1,-1),(-1,2))):
            for x, y in zip(got, expected):self.assertAlmostEqual(x,y)

    def test_parameter_unit_conversion_preserves_information(self):
        a = assess_linearized_calibration([[1,0],[1,1]], parameter_names=('a','b'),
                parameter_scales=(1,1), residual_sigma=(2,2))
        b = assess_linearized_calibration([[.001,0],[.001,1]], parameter_names=('a_milli','b'),
                parameter_scales=(1000,1), residual_sigma=(2,2))
        self.assertEqual(a.information_eigenvalues_scaled, b.information_eigenvalues_scaled)
        self.assertAlmostEqual(b.covariance_parameter_units[0][0]/1e6,
                               a.covariance_parameter_units[0][0])
        self.assertAlmostEqual(b.covariance_parameter_units[0][1]/1000,
                               a.covariance_parameter_units[0][1])

    def test_analytic_rotated_eigensystem_and_random_inverse(self):
        # Construct J as independently rotated, non-uniform diagonal information.
        rng = random.Random(127)
        for _ in range(20):
            angle = rng.uniform(-3,3); c,s = math.cos(angle),math.sin(angle)
            j = [[c,-s,0],[2*s,2*c,0],[0,0,3]]
            r = assess_linearized_calibration(j, parameter_names=('a','b','c'),
                    parameter_scales=(1,1,1), residual_sigma=(1,1,1))
            for x,y in zip(r.information_eigenvalues_scaled,(1,4,9)):
                self.assertAlmostEqual(x,y,places=10)
            h = [[sum(row[i]*row[k] for row in j) for k in range(3)] for i in range(3)]
            for i in range(3):
                for k in range(3):
                    self.assertAlmostEqual(sum(h[i][a]*r.covariance_parameter_units[a][k]
                                               for a in range(3)),float(i==k),places=10)

    def test_noise_scaling_without_residual_rescaling(self):
        kw = dict(parameter_names=('a',), parameter_scales=(1,))
        a = assess_linearized_calibration([[1],[1]], residual_sigma=[1,1], **kw)
        b = assess_linearized_calibration([[1],[1]], residual_sigma=[3,3], **kw)
        self.assertAlmostEqual(b.covariance_parameter_units[0][0],
                               9*a.covariance_parameter_units[0][0])
        z = assess_linearized_calibration([[0]], residual_sigma=[1], **kw)
        self.assertEqual(z.effective_rank, 0)
        self.assertIsNone(z.covariance_parameter_units)

    def test_four_coupled_parameters_analytic_eigensystem(self):
        basis = [[1,1,1,1],[1,-1,1,-1],[1,1,-1,-1],[1,-1,-1,1]]
        j = [[x*(i+1)/2 for x in row] for i,row in enumerate(basis)]
        r = assess_linearized_calibration(j, parameter_names=('a','b','c','d'),
                parameter_scales=(1,1,1,1), residual_sigma=(1,1,1,1))
        for got,expected in zip(r.information_eigenvalues_scaled,(1,4,9,16)):
            self.assertAlmostEqual(got,expected,places=10)
        for i in range(4):
            for k in range(4):
                expected = sum(basis[a][i]*basis[a][k]/(4*(a+1)**2) for a in range(4))
                self.assertAlmostEqual(r.covariance_parameter_units[i][k],expected,places=10)

    def test_time_block_holdout_and_leakage_rejections(self):
        train,test = split_time_blocks(['a','a','b','b','c','c'],list(range(6)))
        self.assertEqual(train,(0,1,2,3));self.assertEqual(test,(4,5))
        for ids,t in [(['a','b','a','c'],range(4)),(['a','b'],range(2)),
                      (['a','b','c'],[0,0,1]),(['a','b','c'],[0,2,1])]:
            with self.assertRaises(ValueError):split_time_blocks(ids,t)
        with self.assertRaises(ValueError):split_time_blocks(['a','b','c'],range(3),heldout_blocks=2)

    def test_invalid_inputs_and_budgets(self):
        kw = dict(parameter_names=('a',),parameter_scales=(1,))
        for j in ([],[[math.nan]],[[math.inf]],[[True]],[[1<<4096]],[[1,2]],[[1]]*20001):
            with self.assertRaises(ValueError):assess_linearized_calibration(j,**kw)
        for sigma in ([],[0],[-1],[math.inf],[True],[1e-300]):
            with self.assertRaises(ValueError):assess_linearized_calibration([[1]],residual_sigma=sigma,**kw)
        for d in ([],[(0,0,0)],[(1,1,0)],[(1,0,math.nan)],[(1,0,0)]*6001):
            with self.assertRaises(ValueError):rotation_observability(d)
        with self.assertRaises(ValueError):assess_linearized_calibration([[1,2]],
            parameter_names=('same','same'),parameter_scales=(1,1))
        with self.assertRaises(ValueError):assess_linearized_calibration([[1]],
            max_information_condition=1e14,**kw)


if __name__ == '__main__':unittest.main()
