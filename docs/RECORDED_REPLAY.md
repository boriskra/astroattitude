# Local recorded-data replay

`tools/replay_calibration.py` is an optional Python adapter for the documented journal/manifest
layout. It uses caller-installed `rawpy` and NumPy to decode; neither is a
production dependency. It performs no phone operations or downloads.

```sh
python tools/replay_calibration.py prepare --session /private/session \
  --output /private/fresh-experiment --native /path/libastroattitude_c.dylib \
  --catalog /private/catalog.json --astrometry /private/astrometry.json \
  --context /private/context.json
python tools/replay_calibration.py run --output /private/fresh-experiment \
  --native /path/libastroattitude_c.dylib --catalog /private/catalog.json \
  --astrometry /private/astrometry.json --context /private/context.json
```

Preparation exclusively creates the experiment, archives the runner/native binary,
records extraction/association policies and comparison metrics, fixes whole-block
allocation, and inventories journals, manifests, reviews, prior outputs and RAW
hashes. Historical training blocks are development; remaining whole blocks are
evaluation. **All outcomes are retrospective**, including evaluation outcomes,
because recorded data may already have informed development. `--blocks` is an explicit
preprocessing allocation, not a post-fit source filter; unprocessed frames remain
in the report. Never call historical reserve data prospective validation.

Run verifies immutable inputs and DNG/decoded-Bayer hashes, preserves every planned
frame, marks missing/failed/duplicate evidence, and reports actual start gaps.
Pose and camera/optical settings, including actual per-block readbacks, define
separate sessions. Changed within-block settings or unqualified setting/clock
guards remain failures; requested values do not overwrite actual readbacks. Each original baseline
and existing visual-mask trial remains untouched. `--visual-masks` adds a separately
labelled native run using caller-supplied pixel boxes; it never derives masks from
fit residuals. Historical baseline/trial counts are still reported when this option
is absent. New source patches require independent inspection before claiming newly
recovered catalog stars.

Reported comparisons include accepted compact-source count, stable rejection
reasons, original usable sources within three native pixels, 4×3 coverage and
native association outcomes. Source receipts preserve namespace-qualified IDs and
flag agreement or contradiction with earlier IDs at the same source position.
Neither historical agreement nor an automatic match certifies identity. Proximity to an old centroid is a recovery diagnostic,
not independent truth. Current association input is predeclared as the 64 highest
flux detections with ID tie-breaking; extraction still retains all candidates and
rejections. Resource or association failures remain explicit.

Optional catalog/astrometry/context arguments must match the predeclared hashes.
Without them, extraction proceeds with an explicit `not_run` association status.
With them, native resource checks remain enforced. The adapter records the capture
clock bridge plus half-exposure midpoint assumption; it does not certify clock
semantics, GPS height datum, refraction or absolute north. Unaccounted scheduled blocks or failed native block completion prevent fitting.
The native fit sees only training/model-selection roles. A diagnostic candidate may be saved; this runner
never freezes historical validation or exports a qualified calibration.

No source recordings, site coordinates, handset coefficients, third-party resource
packages or private journals belong in Git. Publish independent synthetic fixtures only; keep all capture-derived findings local. Exclusive result paths protect earlier experiments.

## Explicit masks

A per-block `visual-review.json` may provide `extraction_mask_rectangles`: a list of
`[x0, y0, x1, y1]` integer rectangles in decoded native-pixel coordinates, with
exclusive upper bounds. No border or obstruction mask is assumed. Invalid rectangles
fail rather than silently clipping. Masks must come from pixels or caller review,
never optical-fit residuals or predicted catalog positions.
