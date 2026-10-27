# Multi-camera and inertial fusion

The [experimental alignment API](ALIGNMENT.md) implements fixed-pose camera
comparisons and observable gravity mismatch. Navigation/filter fusion remains
application-design guidance.
The library returns timestamped camera orientation. The application owns camera-to-rig
alignment, clock relationships, motion models and admission into a navigation filter.

## What a rig needs

Each lens needs its own validated optical model and an independently measured
camera-to-rig rotation. Use a shared reference frame and explicit transform direction.
Switching lenses does not preserve optical calibration. Accelerometer gravity alone
cannot determine rotation about gravity or camera yaw.

## Time and motion

Sequential frames are not simultaneous measurements. Preserve exposure midpoint and
its uncertainty, rolling-shutter assumptions and the clock conversion provenance.
Motion between exposures requires a supported temporal model; do not force a rigid
stationary fit to hide it. Repeated frames are often correlated.

## Validation

Use independently generated controls to test transform direction, timing, missing
measurements and correlated inputs. Physical validation requires independent alignment
and timing evidence. Report thermal, focus, stabilization, mount and vibration limits.
A categorical thermal state is not lens or mount temperature. Do not infer absolute
heading accuracy from fit residuals or agreement between dependent estimates.

See [calibration diagnostics](CALIBRATION_DIAGNOSTICS.md) and
[validation](VALIDATION.md) for the implemented diagnostic boundaries.
