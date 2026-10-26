# Experimental native calibration workflow

Status: implemented, unreleased. Synthetic controls exercise the native core and
C/Python/Swift/Kotlin adapters. Existing solve behavior is unchanged. Real-image
replay is retrospective; fresh physical validation and absolute accuracy remain
unqualified. See [extraction](STAR_EXTRACTION.md), [replay](RECORDED_REPLAY.md), and
[validation and limitations](VALIDATION.md).

## Guided use

1. **Prepare.** Supply camera metadata, configuration/physical-camera/optical-state
   identities, raster convention, approximate horizontal field of view and the
   number of available manual moves. `CalibrationPreparation` creates an explicitly
   unqualified square-pixel seed with zero distortion. Users need not enter lens
   coefficients. Zero moves selects fixed-view guidance.
2. **Collect and ingest.** The application schedules whole training and
   model-selection blocks before acquisition, decodes linear pixels, runs
   `StarExtractor`, and passes exposure/context/evidence to `add_frame`. Record every
   attempted frame and close every block with `finish_block`, including missing
   frame IDs and their reasons. A receipt contains source identities, detection
   IDs, association failures and model limitations. Covariance is not invented.
3. **Review progress.** `progress` reports frame/block/admission counts and a 4×3
   source-coverage grid. `next_action` returns one action and one reason. Failed
   selection or optical observability produces a review action. The library does
   not repeatedly request manual repositioning or control a camera. Coverage counts are
   diagnostics, not proof that the entire field is calibrated.
4. **Fit.** `fit` returns an immutable diagnostic candidate or a typed failure.
   `model_diagnostics` retains attempted family results, even when no candidate is
   supported. Save diagnostic evidence separately from an exportable calibration.
5. **Plan and freeze.** `propose_validation` chooses a four-source panel from
   model-selection availability alone and proposes 15 blocks spaced five minutes
   apart, covering at least 70 minutes. Its search is bounded to the 12 most
   frequently available sources; an unsuccessful bounded search is a limitation,
   not proof that no other panel exists. Each selection block must already contain
   six complete panel frames. Availability does not predict future weather.
   The adapter persists the proposed plan; `freeze` binds its exact panel, pose,
   admission-policy identity, schedule and thresholds to the candidate. Validation
   frames must carry that exact identity in `quality_provenance_id`; matching
   selection evidence must use it too. The adapter hashes its extraction and
   admission rules into this identity.
6. **Validate.** Only fresh exposures after freeze in the scheduled slots may enter
   validation. The source panel cannot change. All scheduled blocks need at least
   six complete panel frames, the actual exposure span must reach 4,200 seconds,
   and both maximum north change from the first block and maximum pairwise block
   separation must be strictly below the declared limits (default 60 arcseconds).
7. **Save or report the limitation.** `export_validated` requires a passing report,
   certified forward Brown inversion over the raster, and training coverage in
   every grid cell. Unsupported regions are rejected. Export is deliberately
   conservative: the current implementation does not certify holes inside a
   bounding box or synthesize a more permissive partial-field export.

The application owns camera access, health checks, scheduling, backups, display
transforms, location/time acquisition and any physical actions. The library owns
pixel extraction, association, fitting, candidate/plan identity and validation.
Neither layer should present a fitted residual as heading accuracy.

## States and evidence rules

`mapping → model_selection → reserve → frozen → validation → complete`, with
explicit `paused` and terminal `ended` states. Reserve is optional. `pause`/`resume`
retain the prior state. Ended sessions reject new capture evidence. Offline replay
uses a separate experimental session rather than reopening an ended outdoor pose.

Roles are caller-predeclared and monotone. Completion requires each planned frame
to be accounted for as attempted or missing. Empty failed blocks remain visible.
Inputs have strictly increasing exposure times; completed blocks do not overlap.
A changed camera configuration, reused frame ID or inconsistent block identity
is rejected. Repeated payload digests are retained as rejected frames. Digests are
caller evidence claims; the local importer verifies the actual bytes.

Training and model-selection data alone enter the optical fit. Reserve cannot be
relabelled as validation. Validation frames still use the existing minimum-six
catalog association gate. Rotation from the known four-source panel is a separate
operation after association; this does not lower the blind matching minimum.
No drift subtraction, missing-block deletion, panel substitution or residual-based
mask selection is performed.

## Numerical method and domain

Three bounded **forward Brown** families are evaluated:

- shared focal scale, with center fixed;
- shared focal scale plus `k1`, `k2`, with center fixed;
- shared focal scale, effective center, `k1`, `k2`.

`fx == fy`; `k3 == p1 == p2 == 0`. Focal bounds are 0.7–1.3 times the metadata seed,
radial coefficients ±0.2, and center movement ±15% of the raster dimension.
These are solver domains, not physical priors or uncertainty bounds. Every trial
must support certified central Brown inversion at its sampled pixels; validated
export also checks the raster corners. Inverse-polynomial coefficients
are not forward Brown coefficients and are never imported as candidates.

Each training block has an independent Wahba rotation nuisance variable. Angular
vector residuals give each block equal total weight. There is no stationary-mount
constraint. Model-selection score is leave-one-source-out prediction with fixed
optics, equal block weighting and separately estimated frame rotation. Empty
selection blocks prevent selection. The simplest family wins score ties within
1e-12 radians. This is an experimental selection policy, not a calibrated test.

The finite-difference Jacobian re-estimates each block rotation. Reports expose
its scaled eigenvalues, weakest mode, rank and condition; a condition exceeding
1e8 is treated as weak geometry. These diagnostics are not covariance, confidence
intervals or absolute pointing uncertainty. Conditional precision is unestimated
without an independently justified noise model. Mount motion, OIS, source/PSF
sampling, focus, temperature, camera–IMU alignment, catalog, clock, refraction and
predicted EOP remain separate limitations. Actual lens/mount temperature is absent.

## Resources, journals and ownership

Association uses the native engine, caller-provided validated catalog/astrometry,
explicit observer/refraction/EOP policy and an unqualified optical seed. Resource
validity, ambiguity and computational limits remain enforced. Quality exclusions
from catalog conversion remain in its provenance manifest. Retained native model
flags accompany receipts, candidates and validation reports.

`journal` serializes configuration, input evidence and state-changing successful
operations. Caller logs retain rejected API calls and failed fits; these are not
silently turned into admitted frames. `restore` rechecks resource identities and
recomputes association and fitting. New fit records retain the exact candidate;
restoration checks its digest, support and evidence, model statuses/ranks and
recomputed optical parameters within 1e-9 relative (absolute floor 1e-9). This
accommodates platform floating-point differences without changing frozen optics.
Legacy records without the candidate require exact recomputation. Journals are
not signatures, independent truth, or authorization to acquire new images.

Mutable sessions require serialized access in C++. C has opaque session and
immutable artifact handles. Language adapters serialize session calls, retain
active-call leases, support cancellation, and provide explicit `close`/`use`.
No worker pool, decoder, device service or numerical reimplementation is hidden
inside an adapter. See [C ABI](C_ABI.md) for versioned messages and limits.

## Examples

- C++: `examples/cpp_calibration.cpp`
- C: `examples/c_calibration.c`
- Python: `examples/python_calibration.py` and `tools/replay_calibration.py`
- Swift: `bindings/swift/Sources/CalibrationGuide/main.swift` (the synthetic runner also
  exercises extraction and the full lifecycle)
- Kotlin: `bindings/android/example/src/main/kotlin/org/astroattitude/example/CalibrationGuide.kt`

The executable examples use synthetic resources and replay journals. They do not
capture images, move a mount, install an application, or qualify a handset.
