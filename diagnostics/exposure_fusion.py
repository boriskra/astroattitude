"""Experimental linear exposure fusion, separate from production attitude solving.

Samples must describe the SAME scene element and channel, after black subtraction,
geometric registration and exposure integration. Response scales and measurement
variances are caller inputs, not inferred from ISO labels or fit residuals. This
module performs no alignment, demosaic, sharpening, tone mapping or deghosting.
"""
from dataclasses import dataclass
import math
from typing import Optional, Sequence, Tuple


def _finite_number(value):
    try:
        return type(value) in (int, float) and math.isfinite(value)
    except OverflowError:
        return False


@dataclass(frozen=True)
class ExposureSample:
    signal_dn: float
    response_scale: float
    variance_dn2: float
    saturation_dn: float
    usable: bool = True


@dataclass(frozen=True)
class FusedSample:
    status: str
    value: Optional[float]
    conditional_variance: Optional[float]
    contributors: Tuple[int, ...]
    rejected: Tuple[int, ...]
    consistency_chi_square: Optional[float]
    physical_accuracy_qualified: bool = False


def fuse_samples(samples: Sequence[ExposureSample], *,
                 max_reduced_chi_square: float = 9.0) -> FusedSample:
    """Inverse-variance fusion of 1..32 registered linear samples.

    response_scale converts a common scene value into expected DN. Include actual
    exposure AND calibrated gain/throughput; equal ISO ratios alone do not prove
    equal response. saturation_dn is a conservative black-subtracted clipping
    threshold, including caller's headroom. Negative noise samples are retained.

    Inconsistent samples fail closed rather than silently choosing an exposure.
    The consistency threshold is a diagnostic gate, not a calibrated confidence
    level. Variance assumes independent supplied noise and exact registration,
    response scales and masks; it excludes their systematic uncertainties.
    """
    if not 1 <= len(samples) <= 32:
        raise ValueError("Expected 1..32 exposure samples")
    if (not _finite_number(max_reduced_chi_square)
            or max_reduced_chi_square <= 0):
        raise ValueError("Invalid consistency threshold")
    kept, rejected, values, variances = [], [], [], []
    for i, sample in enumerate(samples):
        if not isinstance(sample, ExposureSample) or type(sample.usable) is not bool:
            raise ValueError("Invalid exposure sample")
        numbers = (sample.signal_dn, sample.response_scale,
                   sample.variance_dn2, sample.saturation_dn)
        if (not all(_finite_number(x) for x in numbers)
                or min(numbers[1:]) <= 0):
            raise ValueError("Nonfinite or nonpositive exposure metadata")
        if not sample.usable or sample.signal_dn >= sample.saturation_dn:
            rejected.append(i)
            continue
        value = sample.signal_dn / sample.response_scale
        variance = sample.variance_dn2 / sample.response_scale / sample.response_scale
        if not math.isfinite(value) or not math.isfinite(variance) or variance <= 0:
            raise ValueError("Unrepresentable normalized sample")
        kept.append(i)
        values.append(value)
        variances.append(variance)
    if not kept:
        return FusedSample("no_usable_samples", None, None, (), tuple(rejected), None)
    # Relative weights avoid overflowing reciprocals of small valid variances.
    floor = min(variances)
    weights = [floor / variance for variance in variances]
    total = math.fsum(weights)
    value = math.fsum((weight / total) * x for weight, x in zip(weights, values))
    variance = floor / total
    try:
        chi_square = math.fsum(((x - value) / math.sqrt(v)) ** 2
                              for x, v in zip(values, variances))
    except OverflowError:
        chi_square = math.inf
    if not math.isfinite(value) or variance <= 0:
        raise ValueError("Unrepresentable fused sample")
    if len(kept) > 1 and (not math.isfinite(chi_square)
                         or chi_square / (len(kept) - 1) > max_reduced_chi_square):
        return FusedSample("inconsistent", None, None, tuple(kept),
                           tuple(rejected), chi_square)
    return FusedSample("fused" if len(kept) > 1 else "single_exposure",
                       value, variance, tuple(kept), tuple(rejected), chi_square)
