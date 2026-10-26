# Camera and accelerometer alignment

The experimental alignment API compares orientations measured in one common
reference frame. It does not calibrate accelerometer bias or scale, estimate a
location, or qualify a lens model. The [iOS example](../examples/ios-alignment/README.md)
provides guided stationary poses, an optional one-pose check and a synthetic simulator mode.

## What can be measured

From one stationary pose:

| Reference | Camera to camera | Accelerometer to camera |
| --- | --- | --- |
| Stars, observer position and time | Full relative rotation | Gravity mismatch; twist about gravity remains unknown |
| Unlevelled planar target | Full relative rotation | Unavailable: no gravity reference |

One static gravity vector constrains only two rotational degrees of freedom.
`measure_alignment` therefore returns no accelerometer-to-camera matrix. More
samples of that same vector do not resolve the missing degree of freedom.

`calibrate_rig_alignment` accepts distinct stationary poses and estimates a full
rotation. Stars require at least three poses with independently known reference
gravity. An unlevelled, **fixed** target requires at least four poses; its unknown
gravity direction is fitted jointly with the rotation. Accelerometer bias and
scale remain unqualified in both cases. The target result is conditional on the
fixed board and the uncalibrated accelerometer model, not an independent absolute
gravity measurement. Motion is allowed between poses, never within one pose.

## Coordinate and timing contract

`AlignmentFrame.R_reference_from_camera` maps camera column vectors into the
named reference frame. Camera axes are right, down, forward. All frame intervals
and the IMU summary use the same caller-declared monotonic `clock_id` in seconds.
`reference_id` must remain identical. The application is responsible for proving
stationarity over **and between** the sequential camera blocks.

For cameras A and B, the result maps A vectors to B:

```
R_B_from_A = transpose(R_reference_from_B) * R_reference_from_A
```

The rotation vector is axis times angle in radians, not three Euler angles.
The iOS table converts it to degrees. Within-camera orientations are equally
weighted and projected onto SO(3). `maximum_repeatability_rad` is the maximum
pairwise change within either camera's admitted frames; it is not a confidence
interval or an accuracy bound. The admission cap is 0.02 radians within a camera.

For gravity, supply a physical **down** vector in the reference frame, e.g.
`(0,0,-1)` in East–North–Up, and a raw stationary accelerometer down vector. The
observable mismatch is the angle between independently derived camera-frame down
and `R_camera_from_accelerometer_nominal * down_accelerometer`. In the example's
portrait raster convention the nominal mapping is `diag(1,-1,-1)`. No fused
camera/IMU attitude or magnetometer supplies the reference.

## Native and binding entry points

- C++: `measure_alignment`, `calibrate_rig_alignment` and `solve_planar_target` in
  [alignment.hpp](../include/astroattitude/alignment.hpp).
- C: `aa_alignment_measure_v1` (`astroattitude.alignment/1`) and
  `aa_planar_pose_v1` (`astroattitude.planar-pose/1`). Requests contain `schema` and
  `value`; successful responses contain `value`. Existing owned messages and
  cancellation rules apply. No mutable alignment handle is needed.
  The multi-pose endpoint is `aa_rig_alignment_v1`, schema
  `astroattitude.rig-alignment/1`, with a 6 MiB request limit.
- Python: `astroattitude.workflow.measure_alignment(native, request, cancel=None)`
  `solve_planar_target`, and `calibrate_rig_alignment`.
- Swift / Kotlin: `Alignment.measure`, `Alignment.targetPose` and `Alignment.calibrateRig`.

The additive endpoints do not alter the existing solve ABI. JSON request limits
are 1 MiB for alignment and 64 KiB for target pose. Corner IDs remain canonical
uint64 decimal strings on the wire. A cancelled call returns `Cancelled` through
the existing binding error path. Close a cancellation token explicitly after its
active call finishes; the bindings retain the existing active-call protection.

Alignment accepts up to eight cameras, 256 frames and a requested minimum of
2–32 frames per camera (default six). `AlignmentStatus` distinguishes `estimated`,
`tilt_only`, `gravity_reference_unavailable`, `motion_evidence_unavailable`,
`motion_detected`, `insufficient_frames`, `optical_configuration_changed` and
`inconsistent_frames`. An absent IMU summary yields unavailable comparisons.
Bad rotations, conflicting identities or invalid/unsupported timing fail the
request. Missing camera rows are retained. Supply an optical provenance identity
that changes when the optical configuration changes.

## Full rotation from distinct poses

`RigAlignmentRequest.poses` holds 1–6 ordinary `AlignmentRequest` values. Their
camera sets, reference and clock identities must match, their stationary intervals
must not overlap, and frame IDs cannot be reused. Per-camera optical provenance
and the nominal axis map must remain unchanged. Two usable frames per camera per
pose are sufficient for admission; the example collects three or four poses.

The fit gives equal weight to each admitted pose, using its mean camera rotation
and normalized raw accelerometer down vector. It minimizes the difference between
`R_reference_from_camera * R_camera_from_accelerometer * down_accelerometer` and
the reference down vector. For the target, that unit reference vector is another
fit variable. Independent camera translations remain in the planar pose stage.

The solver uses seven starting rotations and at most 100 iterations per start.
It rejects a normal-matrix eigenvalue ratio below 1e-4, competing solutions,
maximum gravity disagreement above 0.02 radians, or corrections more than 0.35
radians from the nominal map. These are conservative admission rules for small
mounting misalignments, not accuracy guarantees. A phone turned only around a
single axis can fail the geometry test. No drift or residual trend is removed.

A successful `rotation_estimated` row includes the full `R_to_from` and
`misalignment_vector_rad`, the axis–angle vector of
`R_to_from * transpose(R_nominal)`. Other statuses include
`pose_diversity_insufficient`, `ambiguous_alignment` and `fit_inconsistent`.
`pose_count`, `maximum_gravity_residual_rad` and `geometry_ratio` are diagnostics;
none is a covariance or confidence interval. Camera pairs average independently
measured within-pose relative rotations and report their between-pose spread.

## Planar target adapter

The core receives identified 2D board corners in metres and image corners in
pixels, plus explicit pinhole intrinsics for that raster. Detection is outside
the core. The example alone links AprilTag; the library adds no image dependency.

Each pose has its own translation. The solver uses normalized homography
initialization, a reflected-normal alternative and bounded multistart refinement.
It rejects competing poses, nearly collinear geometry, a target covering less
than 1% of the raster bounding area, or a view within 0.08 radians of frontal.
These conservative checks can return unavailable on an otherwise recognizable
board. They are not a proof that every possible ambiguous image is detected.
There are 8–128 corner inputs, six starts and at most 40 iterations per start.
Cancellation is checked between refinement iterations. Intrinsics are bounded
between 1 and 1,000,000 pixels. Distortion must have been removed, or its omission
must be carried as an explicit optical limitation by the caller.

## Evidence and limits

Synthetic controls check transform direction, gravity signs, independent camera
translations, rejection paths, uint64 handling and cross-language parity. The
example renders independent star and target images to exercise extraction and
marker detection. No captured measurements are bundled.

Physical speed, optical accuracy, timing offsets, rolling shutter, OIS, focus,
mount motion, accelerometer bias and temperature dependence remain unqualified.
App thermal state is not lens temperature. The short example cannot replace the
[optical validation workflow](CALIBRATION_WORKFLOW.md), and it makes no claim of
sub-arcminute accuracy.
