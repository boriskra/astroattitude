"""Empirical direction repeatability and rotation-invariant optical checks.

Standard-library host diagnostics only. No calibration fitting, covariance,
confidence interval, ephemeris conversion, or physical-accuracy qualification.
"""
from dataclasses import dataclass, field
import math
from typing import Optional, Tuple


Vector = Tuple[float, float, float]


def _finite(x):
    try:
        return type(x) in (int, float) and math.isfinite(x)
    except OverflowError:
        return False


def _unit(u):
    if not isinstance(u, (list, tuple)) or len(u) != 3 or any(not _finite(x) for x in u):
        raise ValueError('Expected a finite three-component unit direction')
    n = math.hypot(*u)
    if abs(n - 1) > 1e-8:
        raise ValueError('Direction is not unit length')
    return tuple(x / n for x in u)


def _angle(a, b):
    # Unlike acos(dot), this retains tiny-angle sensitivity near dot == 1.
    cross = (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
    return math.atan2(math.hypot(*cross), math.fsum(x*y for x, y in zip(a, b)))


def _mean(vectors):
    if not vectors:
        return None
    v = tuple(math.fsum(u[i] for u in vectors) / len(vectors) for i in range(3))
    n = math.hypot(*v)
    return None if n <= 1e-8 else tuple(x/n for x in v)


@dataclass(frozen=True)
class PairSeparation:
    source_ids: Tuple[int, int]
    measured_rad: float
    reference_rad: float
    error_rad: float


@dataclass(frozen=True)
class PairGeometry:
    pairs: Tuple[PairSeparation, ...]
    rms_error_rad: float
    max_abs_error_rad: float
    physical_accuracy_qualified: bool = field(default=False, init=False)


def compare_pair_geometry(source_ids, measured_rays, reference_directions) -> PairGeometry:
    """Compare every unique matched-source separation, without fitting attitude.

    Each ray collection must be in a single orthonormal frame; the two frames
    may differ by arbitrary rotation. References must already be corrected to
    the exposure epoch/observer/refraction convention. IDs are unique uint64.
    Pair order is ascending ID, independent of input order. Shared-source pair
    errors are correlated: the pair count is NOT independent sample count.
    """
    n = len(source_ids)
    if not 2 <= n <= 64 or len(measured_rays) != n or len(reference_directions) != n:
        raise ValueError('Expected 2–64 corresponding source directions')
    if any(type(i) is not int or not 0 <= i < 2**64 for i in source_ids) or len(set(source_ids)) != n:
        raise ValueError('Source IDs must be unique uint64 integers')
    rows = sorted((i, _unit(m), _unit(r)) for i, m, r in
                  zip(source_ids, measured_rays, reference_directions))
    pairs = []
    for j, (a, m, r) in enumerate(rows):
        for b, other_m, other_r in rows[j+1:]:
            measured, reference = _angle(m, other_m), _angle(r, other_r)
            pairs.append(PairSeparation((a, b), measured, reference, measured-reference))
    errors = [p.error_rad for p in pairs]
    return PairGeometry(tuple(pairs), math.sqrt(math.fsum(e*e for e in errors)/len(errors)),
                        max(abs(e) for e in errors))


@dataclass(frozen=True)
class DirectionBlock:
    block_id: str
    first_time_s: float
    last_time_s: float
    attempted_count: int
    accepted_count: int
    missing_row_indices: Tuple[int, ...]
    status: str
    mean_direction: Optional[Vector]
    rms_scatter_rad: Optional[float]
    alternating_half_separation_rad: Optional[float]
    shift_from_first_admitted_rad: Optional[float]


@dataclass(frozen=True)
class DirectionSeries:
    blocks: Tuple[DirectionBlock, ...]
    minimum_block_samples: int
    reference_block_id: Optional[str]
    max_shift_from_first_rad: Optional[float]
    physical_accuracy_qualified: bool = field(default=False, init=False)


def summarize_direction_series(block_ids, times_s, directions, *, minimum_block_samples=6) -> DirectionSeries:
    """Summarize repeated estimates of ONE direction in ONE declared frame.

    E.g. true north expressed in camera axes for a fixed camera configuration.
    Caller owns frame transforms, fixed within-block source sets, model freeze,
    admission and pose boundaries. Supply None for unavailable estimates; never
    substitute identity or zero. Times are finite strictly increasing seconds
    on one clock; IDs form contiguous blocks. All rows, including failures, set
    alternating-half membership. No standard error or covariance is inferred.
    """
    n = len(block_ids)
    if (not 1 <= n <= 20000 or len(times_s) != n or len(directions) != n
            or type(minimum_block_samples) is not int or not 2 <= minimum_block_samples <= 20000):
        raise ValueError('Invalid row budget, shape or minimum sample count')
    groups, seen = [], set()
    for i, (label, t, u) in enumerate(zip(block_ids, times_s, directions)):
        if not isinstance(label, str) or not label or len(label) > 128 or not _finite(t):
            raise ValueError('Invalid block ID or time')
        if i and t <= times_s[i-1]:
            raise ValueError('Times must strictly increase, including missing rows')
        if not groups or label != groups[-1][0]:
            if label in seen:
                raise ValueError('Block IDs must be contiguous and never recur')
            seen.add(label)
            groups.append((label, []))
        groups[-1][1].append((i, float(t), None if u is None else _unit(u)))
    blocks, reference, reference_id = [], None, None
    for label, rows in groups:
        valid = [u for _, _, u in rows if u is not None]
        missing = tuple(i for i, _, u in rows if u is None)
        mean = rms = halves = shift = None
        status = 'insufficient_samples'
        if len(valid) >= minimum_block_samples:
            mean = _mean(valid)
            status = 'ambiguous_mean' if mean is None else 'empirical_only'
            if mean is not None:
                rms = math.sqrt(math.fsum(_angle(u, mean)**2 for u in valid)/len(valid))
                a = _mean([r[2] for r in rows[::2] if r[2] is not None])
                b = _mean([r[2] for r in rows[1::2] if r[2] is not None])
                if a is not None and b is not None:
                    halves = _angle(a, b)
                if reference is None:
                    reference, reference_id = mean, label
                shift = _angle(reference, mean)
        blocks.append(DirectionBlock(label, rows[0][1], rows[-1][1], len(rows), len(valid),
                                     missing, status, mean, rms, halves, shift))
    shifts = [b.shift_from_first_admitted_rad for b in blocks if b.shift_from_first_admitted_rad is not None]
    # One admitted block has a reference, but cannot establish long-span change.
    return DirectionSeries(tuple(blocks), minimum_block_samples, reference_id,
                           max(shifts) if len(shifts) >= 2 else None)
