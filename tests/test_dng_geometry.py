"""Synthetic controls only; no private media or captured coefficients."""
import dataclasses
import math
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from diagnostics.dng_geometry import radial_warp_from_opcode_list, RadialWarp


def envelope(coefficients=None, reciprocal=1, **kwargs):
    coefficients = coefficients or [1.1, 0., -.1] + [0.]*12
    values = (coefficients + kwargs.get('tangential', [0., 0.])
              + kwargs.get('radius', [0., 1.]) + kwargs.get('center', [.5, .5]))
    payload = struct.pack('>I21dI', 1, *values, reciprocal)
    return struct.pack('>5I', 1, 14, 0x01060000, 0, len(payload)) + payload


class DngGeometryControls(unittest.TestCase):
    def test_independent_forward_and_inverse(self):
        warp = radial_warp_from_opcode_list(envelope())
        # Center (400,300), farthest corner radius 500. Ideal radius 0.5.
        factor = 1.1 - .1 * .5**2
        measured = (400 + 250/factor, 300)
        self.assertEqual(warp.ideal_to_source((650,300), (0,0,800,600)), measured)
        self.assertAlmostEqual(warp.source_to_ideal(measured,(0,0,800,600))[0],650,places=10)
        # Applying the sampling map to measured centroids is the WRONG direction.
        self.assertGreater(abs(warp.ideal_to_source(measured,(0,0,800,600))[0]-650), 20)

    def test_nonreciprocal_and_odd_terms(self):
        warp = radial_warp_from_opcode_list(envelope([1,.2]+[0.]*13,0))
        self.assertEqual(warp.ideal_to_source((650,300),(0,0,800,600)),(675,300))
        self.assertAlmostEqual(warp.source_to_ideal((675,300),(0,0,800,600))[0],650,places=10)

    def test_offcenter_crop_translation_and_identity(self):
        warp=radial_warp_from_opcode_list(envelope([1]+[0.]*14,center=[.3,.7]))
        bounds=(96,30,896,630)
        for p in [(96,30),(800,550),(336,450)]:
            for method in (warp.source_to_ideal,warp.ideal_to_source):
                q=method(p,bounds)
                self.assertLess(math.dist(p,q),1e-10)
        warp=radial_warp_from_opcode_list(envelope())
        q=warp.source_to_ideal((650,300),(0,0,800,600))
        shifted=warp.source_to_ideal((746,330),bounds)
        self.assertLess(math.dist(shifted,(q[0]+96,q[1]+30)),1e-10)

    def test_bayer_coordinate_offsets_and_rescaling(self):
        warp=radial_warp_from_opcode_list(envelope())
        # Half-plane point -> full active raster, not a new warp domain.
        half=(350.25,200.75);offset=(.5,.5)
        full=tuple(2*x+o for x,o in zip(half,offset))
        correct=warp.source_to_ideal(full,(0,0,1000,800))
        small=warp.source_to_ideal(tuple(x/2 for x in full),(0,0,500,400))
        self.assertLess(math.dist(correct,tuple(2*x for x in small)),1e-10)

    def test_dense_roundtrip(self):
        warp=radial_warp_from_opcode_list(envelope())
        for x in range(0,801,40):
            for y in range(0,601,30):
                source=warp.ideal_to_source((x,y),(0,0,800,600))
                self.assertLess(math.dist((x,y),warp.source_to_ideal(source,(0,0,800,600))),1e-8)

    def test_reject_fold_and_nonfinite(self):
        for coefficients in ([1,0,-1]+[0.]*12,[-1]+[0.]*14,[math.nan]+[0.]*14):
            with self.assertRaises(ValueError):radial_warp_from_opcode_list(envelope(coefficients,0))
        # Reciprocal r/(1+2r^2) folds despite positive denominator.
        with self.assertRaises(ValueError):radial_warp_from_opcode_list(envelope([1,0,2]+[0.]*12))

    def test_reject_truncation_trailing_unsupported_and_budgets(self):
        data=envelope()
        for size in range(len(data)):
            with self.assertRaises(ValueError):radial_warp_from_opcode_list(data[:size])
        for bad in (data+b'!',struct.pack('>I',101),b'0'*1048577,
                    data[:4]+struct.pack('>I',1)+data[8:],
                    envelope(tangential=[.1,0]),envelope(radius=[.1,1]),envelope(reciprocal=2)):
            with self.assertRaises(ValueError):radial_warp_from_opcode_list(bad)

    def test_missing_and_radiometric_only_are_not_calibration(self):
        self.assertIsNone(radial_warp_from_opcode_list(struct.pack('>I',0)))
        data=struct.pack('>5I7d',1,3,0x01030000,0,56,0,0,0,0,0,.5,.5)
        self.assertIsNone(radial_warp_from_opcode_list(data))

    def test_identity_and_domain_are_immutable_and_explicit(self):
        warp=radial_warp_from_opcode_list(envelope())
        self.assertEqual(len(warp.payload_sha256),64)
        with self.assertRaises(dataclasses.FrozenInstanceError):warp.reciprocal=False
        for point,bounds in (((10000,0),(0,0,800,600)),((math.nan,0),(0,0,800,600)),((0,0),(0,0,0,1))):
            with self.assertRaises(ValueError):warp.source_to_ideal(point,bounds)


if __name__=='__main__':unittest.main()
