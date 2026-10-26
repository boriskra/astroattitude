# Exposure diagnostics

These optional Python diagnostics operate on caller-supplied linear samples. They are
separate from the native attitude API and contain no captured measurements.

## Capture contract

A stellar exposure can saturate the Moon and foreground lights. Retain separate
short, intermediate and long RAW exposures at the same explicitly selected physical
lens and measured focus. Preserve requested settings, actual EXIF exposure/ISO,
camera readbacks, exposure/focus settling evidence, capture clocks, sensor streams,
safety stops and finalized manifests for every frame. A sequential bracket is not
simultaneous: clouds, lunar motion, Earth rotation, OIS and mount drift matter.

Use histogram clipping and native point/limb quality to select exposure tiers.
Increasing ISO does not collect more photons. Keep frames with different settings
out of a fixed-configuration repeatability series. Do not infer camera infinity
from actuator position 1.0, or infer defocus from cloud/glare alone. Never silently
switch cameras or pool poses.

For isolated lunar surface detail, admit unsaturated RAW samples and verify that
surface contrast repeats across independent frames. A small disk can reveal broad
markings without resolving small craters. Cropping, interpolation and display
enlargement do not improve optical angular resolution. Do not generate texture,
apply learned enhancement, or substitute a reference lunar image as evidence.

## Diagnostic API

`diagnostics.exposure_fusion` has no third-party dependencies:

```python
from diagnostics.exposure_fusion import ExposureSample, fuse_samples

result = fuse_samples([
    ExposureSample(signal_dn=20, response_scale=2,
                   variance_dn2=4, saturation_dn=100),
    ExposureSample(signal_dn=120, response_scale=10,
                   variance_dn2=400, saturation_dn=200),
])
assert result.status == "fused"
# Conditional weighted value 10.4; supplied-noise variance 0.8.
```

Each call combines 1–32 measurements of the **same registered scene element and
channel**. Inputs are linear, black-subtracted DN; negative noise samples are
retained. `response_scale` converts the common scene value into expected DN and
must include actual exposure and calibrated gain/throughput. ISO ratios alone are
not a calibration. `variance_dn2` is explicit measurement variance in DN squared.
`saturation_dn` is a conservative black-subtracted clipping threshold with caller
headroom; `usable=False` masks motion, ghosts, bad pixels or other invalid samples.

The result retains contributing and rejected indices. Saturated/masked inputs are
excluded. All-invalid inputs yield `no_usable_samples`; one remaining exposure
yields `single_exposure`. An inconsistent weighted fit yields `inconsistent` with
no value or variance, rather than concealing a registration or response mismatch.
The configurable reduced-chi-square gate is a diagnostic threshold, not a claimed
false-alarm probability. Invalid/nonfinite metadata and excessive sample counts
raise `ValueError`.

Conditional variance assumes independent supplied noise and exact registration,
response and masks. It excludes those systematic uncertainties and is not an
attitude covariance. Estimated noise or empirical brightness normalization may be
used in a clearly labeled display experiment, but does not become calibrated
measurement noise. Brightness variation between exposure groups can reflect
cloud/throughput, timing, processing or response changes; do not automatically
attribute it to sensor gain.

## Registration and display boundaries

Register a short lunar crop using independently verified lunar features/limb and
retain interpolation and coverage provenance. A local translation does not qualify
full-field sky registration. Stars, the Moon and stationary foreground have
different motion; a single long-span translation cannot align all three. Prefer
per-exposure spherical sky registration with admitted optics and timestamps; mask
foreground/ghosts and treat the Moon's own motion separately.

Exposure normalization, tone mapping and contrast enhancement belong to display
outputs. Preserve linear inputs, clipped/invalid masks, exposure identities and
any single-reference fallback map. Never silently fill rejected pixels or call an
empirically normalized display a calibrated radiance product. Production attitude
estimation should use qualified centroids/rays from original unsaturated samples,
not the tone-mapped composite.

The lunar center supplies a bearing, not a complete attitude: rotation about that
bearing remains free. Stars, calibrated camera-to-IMU gravity, or verified lunar
landmark orientation can supply further constraints. Lunar landmarks require
phase/libration/topocentric projection and optical modeling; a visually plausible
small disk is not sufficient qualification. Unknown camera–IMU misalignment,
accelerometer bias, EOP/refraction/time, optical/OIS and thermal mount errors remain.

## Validation

`python tests/test_exposure_fusion.py` runs eight synthetic controls covering
dynamic-range recovery when a long exposure clips, independent weighted-solution
and variance calculations, negative-noise preservation, inconsistent registration
or response rejection, masking/all-clipped inputs, seeded noise trials and bounded
invalid inputs, including overflow-safe inconsistency rejection. CTest registers
`exposure_fusion_controls` alongside the other
optional host diagnostics. No production binary links this Python diagnostic.

Physical bracket validation, calibrated radiometric response, lunar limb bias,
surface-landmark attitude, full-field WDR registration and absolute pointing
accuracy remain unqualified. A good-looking image is not their acceptance test.
