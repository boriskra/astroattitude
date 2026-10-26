"""Experimental point geometry, NOT a DNG renderer or a calibrated camera model.

Independent implementation of the radial subset of Adobe DNG 1.7.1 chapter 7.
No vendor SDK code or handset coefficients are bundled. Coordinates are supplied
in the unrotated active-image frame with explicit corner-pixel bounds. Callers own
ActiveArea/crop/CFA transforms, processing history, K, and calibration admission.
"""
from dataclasses import dataclass
import hashlib
import math
import struct
from typing import Optional, Tuple


def _poly(coefficients, x):
    result = 0.0
    for coefficient in reversed(coefficients):
        result = result * x + coefficient
    return result


def _positive_on_unit_interval(power):
    """Conservative Bernstein certificate; uncertain maps are refused."""
    degree = len(power) - 1
    bernstein = [sum(power[i] * math.comb(k, i) / math.comb(degree, i)
                     for i in range(k + 1)) for k in range(degree + 1)]
    pending = [(bernstein, 0)]
    work = 0
    while pending:
        values, depth = pending.pop()
        work += 1
        if work > 4096 or not all(math.isfinite(v) for v in values):
            return False
        if min(values) > 1e-12:
            continue
        if max(values) <= 1e-12 or depth >= 20:
            return False
        left, right = [values[0]], [values[-1]]
        while len(values) > 1:
            values = [(a + b) / 2 for a, b in zip(values, values[1:])]
            left.append(values[0]); right.append(values[-1])
        pending.extend([(left, depth + 1), (right[::-1], depth + 1)])
    return True


@dataclass(frozen=True)
class RadialWarp:
    coefficients: Tuple[float, ...]
    normalized_center: Tuple[float, float]
    reciprocal: bool
    payload_sha256: str
    declared_minimum_version: int

    def __post_init__(self):
        if (not isinstance(self.coefficients, tuple) or len(self.coefficients) != 15
                or not isinstance(self.normalized_center, tuple)
                or len(self.normalized_center) != 2
                or not all(math.isfinite(x) for x in self.coefficients + self.normalized_center)
                or not all(0 <= x <= 1 for x in self.normalized_center)
                or type(self.reciprocal) is not bool):
            raise ValueError("Invalid radial model")
        # dr_source/dr_ideal = (f-r*f')/f^2 or f+r*f'.
        derivative = tuple(c * (1 - i if self.reciprocal else 1 + i)
                           for i, c in enumerate(self.coefficients))
        if not (_positive_on_unit_interval(self.coefficients)
                and _positive_on_unit_interval(derivative)):
            raise ValueError("Positive invertible radial branch cannot be certified")

    def _geometry(self, point, bounds):
        if len(point) != 2 or len(bounds) != 4 or not all(map(math.isfinite, (*point, *bounds))):
            raise ValueError("Nonfinite or malformed coordinates")
        x0, y0, x1, y1 = bounds
        if not x0 < x1 or not y0 < y1:
            raise ValueError("Invalid corner-pixel bounds")
        cx = x0 + self.normalized_center[0] * (x1 - x0)
        cy = y0 + self.normalized_center[1] * (y1 - y0)
        radius = math.hypot(max(cx-x0, x1-cx), max(cy-y0, y1-cy))
        dx, dy = point[0]-cx, point[1]-cy
        return cx, cy, dx, dy, math.hypot(dx, dy)/radius

    def _source_radius(self, ideal):
        factor = _poly(self.coefficients, ideal)
        return ideal / factor if self.reciprocal else ideal * factor

    def ideal_to_source(self, point, bounds):
        """DNG's output-to-input sampling map; no extrapolation outside r<=1."""
        cx, cy, dx, dy, radius = self._geometry(point, bounds)
        if radius > 1 + 1e-14:
            raise ValueError("Outside supported ideal radius")
        factor = _poly(self.coefficients, min(radius, 1))
        scale = 1/factor if self.reciprocal else factor
        return cx + dx*scale, cy + dy*scale

    def source_to_ideal(self, point, bounds):
        """Invert a measured source point, rather than apply the forward warp twice."""
        cx, cy, dx, dy, source = self._geometry(point, bounds)
        if source > self._source_radius(1) + 1e-14:
            raise ValueError("Outside supported source radius")
        if source == 0:
            return cx, cy
        low, high = 0.0, 1.0
        for _ in range(56):
            mid = (low + high)/2
            if self._source_radius(mid) < source:
                low = mid
            else:
                high = mid
        scale = (low + high)/2/source
        result = cx + dx*scale, cy + dy*scale
        check = self.ideal_to_source(result, bounds)
        if math.hypot(check[0]-point[0], check[1]-point[1]) > 1e-6:
            raise ValueError("Inverse forward check failed")
        return result


def radial_warp_from_opcode_list(data: bytes) -> Optional[RadialWarp]:
    """Read a bounded opcode list containing one radial opcode 14 and/or vignette.

    Refuse unsupported geometry, per-channel warps, tangential terms, restricted
    radius ranges and opcode chains. None means no supplied warp, NOT zero optics.
    This deliberately supports less than a general DNG reader.
    """
    if not isinstance(data, bytes) or not 4 <= len(data) <= 1048576:
        raise ValueError("Invalid opcode buffer")
    count = struct.unpack_from(">I", data)[0]
    if count > 100:
        raise ValueError("Opcode count exceeds limit")
    offset, warp = 4, None
    for _ in range(count):
        if offset + 16 > len(data):
            raise ValueError("Truncated opcode header")
        opcode, version, flags, size = struct.unpack_from(">4I", data, offset)
        offset += 16
        if size > len(data)-offset or flags & ~3:
            raise ValueError("Invalid opcode envelope")
        payload = data[offset:offset+size]; offset += size
        if opcode == 3:
            if size != 56 or not all(map(math.isfinite, struct.unpack(">7d", payload))):
                raise ValueError("Invalid vignette payload")
            continue  # Radiometric only: not applied to point coordinates.
        if opcode != 14 or warp is not None:
            raise ValueError("Unsupported opcode or warp chain")
        if size != 176 or struct.unpack_from(">I", payload)[0] != 1:
            raise ValueError("Only single-plane radial layout is supported")
        values = struct.unpack_from(">21d", payload, 4)
        reciprocal = struct.unpack_from(">I", payload, 172)[0]
        if (not all(map(math.isfinite, values)) or values[15:17] != (0., 0.)
                or values[17:19] != (0., 1.) or reciprocal not in (0, 1)):
            raise ValueError("Unsupported or invalid radial parameters")
        warp = RadialWarp(values[:15], values[19:21], bool(reciprocal),
                          hashlib.sha256(payload).hexdigest(), version)
    if offset != len(data):
        raise ValueError("Trailing opcode data")
    return warp
