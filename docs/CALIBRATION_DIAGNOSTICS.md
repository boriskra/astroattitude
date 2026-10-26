# Calibration observability diagnostics

Status: implemented experimental diagnostics, not production calibration or
physical qualification. `diagnostics/calibration.py` uses Python 3.9+ and only
the standard library. It is not installed with the native library or adapters.
The production solver continues to consume caller-qualified calibration.

The [long-span calibration requirements](VALIDATION.md) specify
field coverage, centroid/optical-state checks and independent validation needed
to pursue sub-arcminute consistency without treating repeatability as accuracy.

The [multi-camera and mount-motion investigation](RIG_FUSION.md) describes how
shared thermal mount drift and camera-specific changes can confound long-track
calibration. A longer capture does not establish constant attitude by itself.

## Contract

```python
assess_linearized_calibration(
    jacobian, *, parameter_names, parameter_scales,
    residual_sigma=None, max_information_condition=1e8,
) -> CalibrationAssessment

rotation_observability(
    directions, *, tangent_sigma_rad=None, max_information_condition=1e8,
) -> CalibrationAssessment

split_time_blocks(block_ids, times_s, *, heldout_blocks=1)
    -> (training_row_indices, heldout_row_indices)
```

Inputs use finite built-in Python numbers. Jacobians have 1–12 columns and at
most 20,000 rows; unit-direction collections have at most 6,000 entries. Invalid,
nonfinite, unrepresentable or nonconvergent numerical inputs are rejected.

The Jacobian is the derivative of each residual with respect to each candidate
parameter, in explicitly declared units. Positive parameter scales define useful
changes in those units. A condition number mixing radians, pixels and focal
length without declared scales is not interpretable. Include nuisance parameters
(such as focal scale or center offsets) jointly when assessing attitude. Treating
them as fixed can hide an attitude/calibration ambiguity.

The frozen result records parameter names/scales, sorted scaled-information
eigenvalues, a weakest scaled mode, effective rank, information condition,
optional covariance in the original parameter units, and a covariance status.
`physical_accuracy_qualified` is always false. The weakest mode has arbitrary
sign; repeated eigenvalues do not define a unique eigenvector. Physical parameter
increments corresponding to the mode are `scale[j] * mode[j]`.

For supplied row standard deviations, form the scaled, whitened Jacobian
`A[i,j] = J[i,j] * scale[j] / sigma[i]`, and information `H = Aᵀ A`. Without
noise, this is only an unweighted local sensitivity assessment. Eigenvalues below
or equal to `largest / max_information_condition` fail the sensitivity gate.
The default threshold is a diagnostic policy, not a derived accuracy requirement.
The condition cap is limited to 1e10 because this small normal-matrix assessment
is not intended to resolve extreme numerical condition numbers.

Covariance is emitted only when all parameters pass the gate and measurement
noise is supplied. It is `diag(scales) H⁻¹ diag(scales)`. There is no residual-based
variance estimate, pseudoinverse covariance, regularization prior or invented
noise. If the model is weak, covariance is withheld. Noisy residuals, robust-loss
weights, PSF-model differences and an assumed calibration error are not
interchangeable with measured independent noise.

Rows must have independent supplied errors, or be prewhitened using an externally
justified covariance (then pass unit standard deviations). The diagnostic cannot
discover unknown correlation. Camera calibration, clock, ephemeris, PSF/flare,
OIS and thermal effects shared by many frames must be modeled or explicitly left
outside the conditional covariance. Even full local rank does not prove a unique
global solution, a correct forward model or physically valid calibration.

## Rotation and accelerometer geometry

For a unit direction `u` in one declared frame, infinitesimal rotation contributes
`I - u uᵀ` to the equal tangent-noise information. `rotation_observability` sums
this information, using supplied isotropic tangent standard deviations in radians
when available. Its covariance, if admitted, describes infinitesimal rotation
components in that same frame. The cross-product factorization uses three rows
but only two tangent degrees of freedom; they are not three independent measured
Cartesian components.

```python
from diagnostics.calibration import rotation_observability

# Synthetic directions with an EXPLICIT assumed noise model, not phone evidence.
result = rotation_observability(
    [(1, 0, 0), (0, 1, 0), (0, 0, 1)],
    tangent_sigma_rad=[0.001, 0.001, 0.001],
)
assert result.effective_rank == 3
assert not result.physical_accuracy_qualified

# Repeated gravity measurements at one fixed pose cannot calibrate full alignment.
gravity = rotation_observability([(0, 0, 1)] * 100)
assert gravity.effective_rank == 2
assert gravity.covariance_parameter_units is None
```

For real camera–IMU alignment, the camera-side gravity direction must come from
an independently informative optical attitude; it cannot be created from an
unconstrained short Sun track and used as alignment truth. Distinct poses with
known optical orientation can constrain the missing rotation. Accelerometer
bias/scale and dynamic acceleration require additional modeling. Longer repeats
at the same pose do not supply pose diversity.

## Validation without frame leakage

`split_time_blocks` accepts strictly increasing times and contiguous block IDs.
It reserves the latest whole blocks, with at least two earlier training blocks.
Interleaved blocks, repeated timestamps and insufficient blocks are rejected.
Fit and choose the candidate model on training data, then freeze it before
scoring held-out blocks. If results are used to choose a new model, reserve new
future blocks for an independent check. A row split cannot enforce this discipline
or make correlated observations independent.


## Empirical consistency and rotation-invariant checks

`diagnostics/attitude_consistency.py` adds two bounded, standard-library host
diagnostics. They are not installed with production packages and do not alter
the solver, calibration schema or C ABI. All reported angles are **radians**.

```python
from diagnostics.attitude_consistency import (
    compare_pair_geometry, summarize_direction_series,
)

geometry = compare_pair_geometry(
    source_ids=[11, 22, 33],
    measured_rays=[(1, 0, 0), (0, 1, 0), (0, 0, 1)],
    reference_directions=[(0, 1, 0), (0, 0, 1), (1, 0, 0)],
)
assert geometry.rms_error_rad == 0  # independent frame rotations are allowed

series = summarize_direction_series(
    block_ids=['first'] * 6 + ['later'] * 6,
    times_s=list(range(12)),
    directions=[(0, 1, 0)] * 6 + [None] * 6,
)
assert series.blocks[1].status == 'insufficient_samples'
assert series.max_shift_from_first_rad is None  # only one admitted block
assert not series.physical_accuracy_qualified
```

`compare_pair_geometry(source_ids, measured_rays, reference_directions)` accepts
2–64 unique unsigned 64-bit IDs and corresponding finite unit vectors. Each
collection uses one orthonormal frame; the two frames need not coincide.
The caller must supply context-corrected reference directions for each exposure,
including the chosen observer, time, EOP and refraction convention. The frozen
result contains every unique pair sorted by ID, measured/reference separations,
their signed difference (measured minus reference), RMS and maximum absolute
error. At most 2,016 pairs are evaluated. Shared-source pairs are correlated;
their number is not an independent sample count.

Rigid rotation preserves pairwise separation. A changing separation error across
a fixed-ID track therefore cannot be explained by rigid camera rotation alone.
It can still arise from optical model error, centroids/PSFs, source mismatches,
refraction or incorrect references. The helper neither identifies a unique cause
nor fits calibration. Compare the same pairs across times and retain missing
pairs explicitly; changing the source set can change a summary by itself.

`summarize_direction_series(block_ids, times_s, directions,
minimum_block_samples=6)` accepts 1–20,000 rows. Times must be finite and strictly
increasing, and nonempty block IDs (at most 128 characters) must be contiguous and
never recur. Directions must represent the same quantity in one declared frame,
for example `R_camera_to_ENU.T @ (0,1,0)` for true north in camera coordinates.
Do not combine different poses, configurations or optical models in one series.
The caller owns attitude conversion and fixed within-block source selection.
Missing estimates are `None`; malformed/nonunit values are rejected, not dropped.

Each block retains attempted/accepted counts, global missing-row indices and
first/last attempted times. With fewer than the declared minimum (2–20,000,
default 6), its status is `insufficient_samples` and all statistics are absent.
Otherwise the normalized vector mean defines empirical angular RMS scatter.
A mean resultant length at or below 1e-8 yields `ambiguous_mean`, not a fabricated
direction. Alternating-half separation uses original row parity within the block,
including missing rows; it is absent if either half has no defined mean. The
reference is the first admitted block; maximum change is absent until at least
two blocks are admitted. This measures change from that reference, not the
maximum separation between any two blocks.

These immutable results always mark physical accuracy unqualified. North-vector
scatter is a three-dimensional angular statistic, not azimuth standard deviation,
geographic-position precision or an absolute error bound. No covariance, standard
error, confidence interval or square-root-of-sample-count scaling is emitted.
Alternating halves share systematics. A common calibration bias can give zero
scatter, and slow optical or mount change can produce long-span inconsistency.
Accelerometer gravity comparisons constrain tilt only; camera–IMU alignment,
bias and dynamic acceleration still require separate treatment.

Freeze calibration and admission criteria before evaluating future whole bursts.
These helpers do not enforce that experiment history or certify real catalog
identity. Preserve failed frames and insufficient blocks; do not lower admission
thresholds after seeing outcomes. Inverse radial diagnostic fits cannot be copied
into the production forward Brown model as if they used the same convention.

### Synthetic validation

Ten controls in `tests/test_attitude_consistency.py` cover independent rigid-axis
rotation, an analytic nonrigid source bias, tiny/antipodal angles, known burst
scatter and long-span bias, angular wrap, correlated duplication, original-row
half membership with missing estimates, absent/ambiguous means, reference
selection, immutable results, uint64 IDs, invalid inputs and work budgets.
They use no handset coefficients, private recordings or site coordinates.

Run the current synthetic suites using the build guide. Test results do not qualify physical accuracy.
