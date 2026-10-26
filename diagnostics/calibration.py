"""Bounded local calibration assessment; not a fitter or calibration approval.

Standard library only. The caller supplies a Jacobian at a candidate solution,
with declared parameter scales. Noise is never estimated from fit residuals.
An informative local Jacobian does not prove a global or physically valid model.
"""
from dataclasses import dataclass
import math
from typing import Optional, Tuple


@dataclass(frozen=True)
class CalibrationAssessment:
    parameter_names: Tuple[str, ...]
    parameter_scales: Tuple[float, ...]
    information_eigenvalues_scaled: Tuple[float, ...]
    weakest_mode_scaled: Tuple[float, ...]
    effective_rank: int
    information_condition: Optional[float]
    covariance_parameter_units: Optional[Tuple[Tuple[float, ...], ...]]
    covariance_status: str
    physical_accuracy_qualified: bool = False


def _finite(value):
    try:
        return type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        return False


def _symmetric_eigen(matrix):
    """Small symmetric Jacobi eigensystem, with bounded work and convergence check."""
    n = len(matrix)
    a = [list(row) for row in matrix]
    v = [[float(i == j) for j in range(n)] for i in range(n)]
    for _ in range(100 * n * n):
        if n == 1:
            break
        p, q = max(((i, j) for i in range(n) for j in range(i + 1, n)),
                   key=lambda ij: abs(a[ij[0]][ij[1]]))
        if abs(a[p][q]) <= 1e-14 * max(1.0, max(abs(a[i][i]) for i in range(n))):
            break
        tau = (a[q][q] - a[p][p]) / (2 * a[p][q])
        t = math.copysign(1 / (abs(tau) + math.hypot(1, tau)), tau)
        c = 1 / math.sqrt(1 + t*t)
        s = t*c
        apq = a[p][q]
        a[p][p] -= t*apq
        a[q][q] += t*apq
        a[p][q] = a[q][p] = 0.0
        for k in range(n):
            if k not in (p, q):
                kp, kq = a[k][p], a[k][q]
                a[k][p] = a[p][k] = c*kp - s*kq
                a[k][q] = a[q][k] = s*kp + c*kq
            vp, vq = v[k][p], v[k][q]
            v[k][p], v[k][q] = c*vp - s*vq, s*vp + c*vq
    else:
        raise ValueError('Information eigensystem did not converge')
    order = sorted(range(n), key=lambda i: a[i][i])
    return [a[i][i] for i in order], [[v[k][i] for i in order] for k in range(n)]


def assess_linearized_calibration(jacobian, *, parameter_names, parameter_scales,
                                 residual_sigma=None, max_information_condition=1e8):
    """Assess local identifiability and, only if supplied, independent noise.

    J[i,j] = d residual_i / d parameter_j in caller-declared units. Scales are
    positive changes in those parameter units: J_scaled = J * diag(scales).
    residual_sigma, when supplied, is one positive standard deviation per row,
    in that row's residual units. Rows must be independent, or have already been
    whitened by the caller (then supply unit sigma). Never pass robust-loss
    weights as measurement noise. Unknown correlations/systematics are omitted.

    Effective rank uses a relative eigenvalue cutoff 1/max_information_condition.
    This is a user-selected sensitivity gate, not a physical qualification test.
    All parameters, including nuisance parameters, must be jointly present;
    covariance sub-blocks then include their local coupling. Singular/weak modes
    cause covariance to be withheld, rather than hidden by a pseudoinverse.
    """
    n = len(parameter_names)
    if (not 1 <= n <= 12 or len(parameter_scales) != n
            or any(not isinstance(x, str) or not x or len(x) > 128 for x in parameter_names)
            or len(set(parameter_names)) != n
            or any(not _finite(s) or s <= 0 for s in parameter_scales)
            or not _finite(max_information_condition)
            or not 1 < max_information_condition <= 1e10):
        raise ValueError('Invalid parameters, scales or condition gate')
    if not 1 <= len(jacobian) <= 20000:
        raise ValueError('Jacobian row budget exceeded or empty')
    if residual_sigma is not None and (len(residual_sigma) != len(jacobian)
            or any(not _finite(s) or s <= 0 for s in residual_sigma)):
        raise ValueError('Noise must be positive finite standard deviations per row')
    rows = []
    for i, row in enumerate(jacobian):
        if len(row) != n or any(not _finite(x) for x in row):
            raise ValueError('Invalid Jacobian')
        sigma = residual_sigma[i] if residual_sigma is not None else 1.0
        scaled = [x * s / sigma for x, s in zip(row, parameter_scales)]
        if any(not math.isfinite(x) for x in scaled):
            raise ValueError('Scaled Jacobian overflow')
        rows.append(scaled)
    # Normalize before forming J^T J; the relative condition does not change.
    scale = max(abs(x) for row in rows for x in row)
    multiplier = scale*scale
    if not math.isfinite(multiplier) or (scale != 0 and multiplier == 0):
        raise ValueError('Information outside representable range')
    normalized = [[x / scale for x in row] for row in rows] if scale else rows
    h = [[math.fsum(row[i]*row[j] for row in normalized)
          for j in range(n)] for i in range(n)]
    ev, vectors = _symmetric_eigen(h)
    # Roundoff in a PSD matrix may produce a tiny negative eigenvalue.
    if ev[0] < -1e-12 * max(1.0, ev[-1]):
        raise ValueError('Invalid negative information eigenvalue')
    ev = [max(0.0, x) for x in ev]
    largest = ev[-1]
    rank = sum(x > largest / max_information_condition for x in ev) if largest else 0
    eigenvalues = tuple(x * multiplier for x in ev)
    if not all(math.isfinite(x) for x in eigenvalues):
        raise ValueError('Information overflow')
    condition = largest / ev[0] if ev[0] > 0 else None
    if condition is not None and not math.isfinite(condition):
        condition = None
    covariance = None
    status = 'measurement_noise_not_supplied'
    if rank < n:
        status = 'weak_or_unobservable_parameters'
    elif residual_sigma is not None:
        covariance = tuple(tuple(parameter_scales[i]*parameter_scales[j] *
                           math.fsum(vectors[i][k]*vectors[j][k] / ev[k]
                                     for k in range(n)) / multiplier
                           for j in range(n)) for i in range(n))
        if not all(math.isfinite(x) for row in covariance for x in row):
            raise ValueError('Covariance overflow')
        status = 'conditional_on_model_and_independent_supplied_noise'
    return CalibrationAssessment(tuple(parameter_names), tuple(parameter_scales),
                                 eigenvalues, tuple(row[0] for row in vectors),
                                 rank, condition, covariance, status)


def rotation_observability(directions, *, tangent_sigma_rad=None,
                           max_information_condition=1e8):
    """Local rotation information from unit directions in ONE declared frame.

    This is geometry assessment, not attitude estimation. For isotropic tangent
    noise sigma per direction, H=sum((I-u*u^T)/sigma^2). The three Cartesian
    cross-product rows below are a factorization of this tangent information;
    they are NOT three independent measured components. Covariance, if admitted,
    describes infinitesimal rotation components in the same declared frame.
    Repeated gravity at one pose leaves rotation about gravity unobservable.
    """
    if not 1 <= len(directions) <= 6000:
        raise ValueError('Direction budget exceeded or empty')
    if tangent_sigma_rad is not None and (len(tangent_sigma_rad) != len(directions)
            or any(not _finite(s) or s <= 0 for s in tangent_sigma_rad)):
        raise ValueError('Invalid tangent noise')
    rows = []
    for u in directions:
        if (len(u) != 3 or any(not _finite(x) for x in u)
                or abs(math.hypot(*u) - 1) > 1e-8):
            raise ValueError('Directions must be finite unit vectors')
        x, y, z = u
        rows.extend(((0, -z, y), (z, 0, -x), (-y, x, 0)))
    sigma = None if tangent_sigma_rad is None else [s for s in tangent_sigma_rad for _ in range(3)]
    return assess_linearized_calibration(rows, parameter_names=('rx_rad', 'ry_rad', 'rz_rad'),
        parameter_scales=(1., 1., 1.), residual_sigma=sigma,
        max_information_condition=max_information_condition)


def split_time_blocks(block_ids, times_s, *, heldout_blocks=1):
    """Return training/test row indices; latest WHOLE blocks are held out.

    At least two training blocks and one held-out block are required. Frames
    must be time ordered; block IDs must be contiguous and never recur. Caller
    must fit only training rows and freeze model selection before scoring test
    rows. A split alone cannot enforce independence or prevent tuning leakage.
    """
    if (not 1 <= len(block_ids) <= 20000 or len(times_s) != len(block_ids)
            or type(heldout_blocks) is not int or heldout_blocks < 1
            or any(not isinstance(x, str) or not x or len(x) > 128 for x in block_ids)
            or any(not _finite(t) for t in times_s)
            or any(b <= a for a, b in zip(times_s, times_s[1:]))):
        raise ValueError('Invalid ordered block observations')
    blocks, seen = [], set()
    for name in block_ids:
        if not blocks or name != blocks[-1]:
            if name in seen:
                raise ValueError('Interleaved or repeated time block')
            blocks.append(name)
            seen.add(name)
    if len(blocks) < heldout_blocks + 2:
        raise ValueError('Need at least two training blocks plus held-out blocks')
    test = set(blocks[-heldout_blocks:])
    return (tuple(i for i, b in enumerate(block_ids) if b not in test),
            tuple(i for i, b in enumerate(block_ids) if b in test))
