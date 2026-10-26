# Validation and limitations

astroattitude is experimental. All published fixtures and evidence are independently
synthetic. No physical pointing or absolute-accuracy qualification is claimed.

## What the tests cover

The native suite exercises resource parsing and validity, forward/inverse geometry,
association, conditional uncertainty, budgets, cancellation and allocation failures.
Extraction controls cover pixel phase, CFA geometry, background, clipping, blends,
trails, borders and centroid bias against generated truth. Calibration controls use
known forward models with independent block rotations, weak geometry, holdouts,
missing evidence, immutable freezes and journal restoration.

C, Python, Swift and Kotlin controls share the native synthetic fixtures. Diagnostic
tests independently generate rational MakerNotes, sensor windows, source candidates
and rotation controls. Their generator is `tests/synthetic_sky.py`, seed 1729; no
recording or captured numeric array is an input. Replay tests construct temporary
synthetic journals and intentionally corrupted files.

## Reproduce checks

Follow [BUILD.md](BUILD.md) for native and platform tests. Run
`python3 tools/check_publication.py` and `python3 tests/test_publication.py` before
publishing. Configure a separate native build with `-DAA_SANITIZE=ON` for address and
undefined-behavior sanitizer checks. Generated fixture and test output directories
are ignored and are not distribution assets.

## Interpret results correctly

- **Consistency** measures variation across admitted observations and a declared span.
- **Conditional precision** depends on supplied measurement noise and fixed nuisance
  assumptions. Unknown noise remains absent; fit residuals do not supply it.
- **Absolute accuracy** needs independent physical truth and a supported systematic
  error budget. It remains unqualified.

Successful synthetic tests do not establish catalog identification performance on real
images. Optical PSF mismatch, pixel sampling, source ambiguity, timing, refraction,
Earth-orientation prediction, stabilization, focus and camera–IMU alignment remain
material concerns.

The calibration workflow requires a frozen candidate, panel, policy and future
schedule before validation. Its default four-source panel needs at least six complete
frames per block across 70 minutes, with maximum north change and maximum pairwise
separation both below 60 arcseconds. Missing scheduled blocks prevent a pass. These
are consistency criteria, not an absolute-accuracy guarantee. See the
[workflow guide](CALIBRATION_WORKFLOW.md) for admission and export rules.
