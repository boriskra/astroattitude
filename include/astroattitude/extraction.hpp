#pragma once
#include "astroattitude.hpp"

namespace astroattitude {
// Pixel centers are integer coordinates in the unrotated input raster.
enum class SampleLayout { Mono, RGGB, BGGR, GRBG, GBRG };
struct LinearImage {
  uint32_t width, height, row_stride;
  std::span<const float> pixels;
  SampleLayout layout = SampleLayout::Mono;
  // Levels follow the four parity positions of this raster, after crop/CFA
  // shift.
  std::array<double, 4> black_levels{0, 0, 0, 0};
  std::array<double, 4> white_levels{65535, 65535, 65535, 65535};
  std::span<const uint8_t>
      excluded; // empty or width*height; nonzero = excluded
};
struct ExtractionOptions {
  double detection_sigma = 5;
  double maximum_axis_ratio = 3;
  uint32_t maximum_candidates = 512;
  uint32_t maximum_stars = 128;
  uint32_t maximum_fit_iterations = 60;
  uint64_t maximum_pixels = 16777216;
  uint64_t maximum_workspace_bytes = 536870912;
  uint64_t maximum_work_units = 2000000000;
};
struct SourceDiagnostic {
  DetectionId id;
  Vec2 pixel;
  std::string reason; // Accepted, Excluded, Clipped, ExtendedGlare, Border,
                      // Blend, Elongated, FitFailed, LowContrast, Capacity
  double sigma_major_px = 0, sigma_minor_px = 0;
  double amplitude = 0, residual_dn = 0, local_noise_dn = 0;
};
struct ExtractionRegion {
  PixelBounds bounds;
  std::string reason;
};
struct ExtractionResult {
  std::string policy_id;
  std::vector<PixelStar> stars;
  std::vector<SourceDiagnostic> sources;
  std::vector<ExtractionRegion> regions;
  uint64_t work_units = 0;
  bool candidate_capacity_reached = false;
  bool physical_accuracy_qualified = false;
};
class StarExtractor {
public:
  static Outcome<ExtractionResult> extract(const LinearImage &,
                                           const ExtractionOptions & = {},
                                           Cancellation = {nullptr});
};
} // namespace astroattitude
