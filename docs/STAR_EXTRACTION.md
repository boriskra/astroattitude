# Portable linear-pixel star extraction

Status: experimental native implementation in `extraction.hpp`. No decoder,
camera service or display transform is linked into production.

`LinearImage` borrows float samples for the synchronous call. Width, height and
row stride describe the caller-decoded, unrotated raster; pixel centers have
integer coordinates. Mono and RGGB/BGGR/GRBG/GBRG are supported. CFA phase is relative
to this raster's top-left: the adapter must update it after cropping. Black/white
levels are four **raster parity** entries, not camera channel indices. Optional
nonzero exclusion bytes cover exactly width×height native pixels.

The algorithm estimates median/MAD backgrounds in 64×64 tiles, detects compact
multiscale peaks on native green samples, distributes candidate capacity over a
4×3 grid, and refines each candidate using original native samples. Bayer fits use
green only; mono uses all samples. The pixel-integrated elliptical Gaussian uses
5×5 quadrature and a planar background. Its widths, amplitude, local noise and fit
residual are diagnostics. Centroid covariance is always absent in this release.
No attitude residual or predicted catalog location affects detection or masks.

Stable source reasons: `Accepted`, `Border`, `Clipped`, `Excluded`, `FitFailed`,
`ExtendedGlare`, `Elongated`, `LowContrast`, `Blend`, `Capacity`. Diagnostic regions
mark insufficient tile support, clipping and extended features. They are reviewable
boxes, not reliable cloud segmentation. An accepted compact feature is not yet a
catalog-associated star. In particular, a single image cannot establish whether
faint structure is cloud contamination. Optional caller masks and subsequent
unambiguous association remain important.

The extraction policy digest covers algorithm version and detection/refinement
settings. Replays separately bind raster/levels, caller mask bytes, native library,
source image and protocol. Do not compare policy identity alone as an image hash.

Defaults: 16,777,216 pixels, 512 candidates, 128 returned detections, 60 fit
iterations, 512 MiB estimated workspace, 2 billion work units. Hard limits include
24 million pixels, 2,048 candidates, 128 accepted sources and 250 iterations.
Input buffers can include row padding but the C boundary caps samples at 24 million.
Budget checks precede allocation, run through image rows and optimizer evaluations,
and poll caller cancellation. Work units bound explicit algorithmic work; they
are not a wall-clock deadline. No hidden thread pool is used.

Tests cover pixel-phase/CFA parity, integrated truth centroids, gradients, clipping,
excluded pixels, trails, broad glare, blends, unsupported borders, constant fields,
nonfinite samples, deterministic ordering, work/memory limits and cancellation.
Synthetic bias tolerance is a control threshold, not a real-camera accuracy claim.
