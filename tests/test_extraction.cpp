#include "astroattitude/extraction.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace astroattitude;
void check(bool v, const char *s) {
  if (!v)
    throw std::runtime_error(s);
}
ExtractionResult run(const LinearImage &im, const ExtractionOptions &o = {}) {
  auto v = StarExtractor::extract(im, o);
  if (auto e = std::get_if<Error>(&v))
    throw std::runtime_error(e->detail);
  return std::get<ExtractionResult>(v);
}
int main() {
  try {
    constexpr int w = 96, h = 96;
    std::vector<float> a(w * h);
    LinearImage im{w, h, w, a};
    for (auto layout :
         {SampleLayout::Mono, SampleLayout::RGGB, SampleLayout::BGGR,
          SampleLayout::GRBG, SampleLayout::GBRG})
      for (double phase : {.0, .25, .5, .75}) {
        double cx = 45 + phase, cy = 48 + .7 * phase;
        im.layout = layout;
        for (int y = 0; y < h; ++y)
          for (int x = 0; x < w; ++x) {
            double s = 0;
            for (int i = 0; i < 16; ++i)
              for (int j = 0; j < 16; ++j) {
                double dx = (x + (i + .5) / 16 - .5 - cx) / 1.3,
                       dy = (y + (j + .5) / 16 - .5 - cy) / 1.8;
                s += 500 * std::exp(-.5 * (dx * dx + dy * dy)) / 256;
              }
            a[y * w + x] =
                100 + .03 * x + .02 * y + s + .03 * std::sin(x * 7 + y * 11);
          }
        auto r = run(im);
        if (r.stars.size() != 1) {
          std::cerr << "layout " << int(layout) << " phase " << phase
                    << " stars " << r.stars.size() << "\n";
          for (auto &d : r.sources)
            std::cerr << d.reason << " " << d.pixel.x << "," << d.pixel.y
                      << " widths " << d.sigma_major_px << ","
                      << d.sigma_minor_px << " residual " << d.residual_dn
                      << "\n";
        }
        check(r.stars.size() == 1, "single source recovery");
        check(std::hypot(r.stars[0].pixel.x - cx, r.stars[0].pixel.y - cy) <
                  .01,
              "pixel-phase centroid bias");
        check(!r.stars[0].centroid_covariance_px2, "unknown covariance");
        check(!r.physical_accuracy_qualified, "qualification");
        auto r2 = run(im);
        check(r2.policy_id == r.policy_id &&
                  r2.stars[0].pixel.x == r.stars[0].pixel.x,
              "determinism");
      }
    im.white_levels = {200, 200, 200, 200};
    auto clipped = run(im);
    check(clipped.stars.empty(), "clipping rejection");
    bool found = false;
    for (auto &s : clipped.sources)
      found |= s.reason == "Clipped";
    check(found, "clipping reason");
    im.white_levels = {65535, 65535, 65535, 65535};
    std::vector<uint8_t> mask(w * h, 1);
    im.excluded = mask;
    check(run(im).stars.empty(), "caller mask");
    im.excluded = {};
    ExtractionOptions o;
    o.maximum_workspace_bytes = 1;
    check(std::get<Error>(StarExtractor::extract(im, o)).code ==
              ErrorCode::ResourceLimitExceeded,
          "memory bound");
    o = {};
    o.maximum_work_units = 1;
    check(std::get<Error>(StarExtractor::extract(im, o)).code ==
              ErrorCode::ResourceLimitExceeded,
          "work bound");
    std::atomic_bool stop{true};
    check(std::get<Error>(StarExtractor::extract(im, {}, Cancellation{&stop}))
                  .code == ErrorCode::Cancelled,
          "cancel");
    // Rotated trails and broad diffuse peaks must not become accepted compact
    // sources.
    im.layout = SampleLayout::Mono;
    for (auto widths :
         std::vector<std::pair<double, double>>{{5., .8}, {7., 7.}}) {
      for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
          double dx = x - 48., dy = y - 48., u = (dx + dy) / std::sqrt(2.),
                 v = (dy - dx) / std::sqrt(2.);
          a[y * w + x] =
              100 +
              500 * std::exp(-.5 * (u * u / (widths.first * widths.first) +
                                    v * v / (widths.second * widths.second)));
        }
      auto r = run(im);
      check(r.stars.empty(), "trail/glare rejected");
    }
    // Unsupported edge profiles retain a reason; a fully masked field stays
    // empty.
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x)
        a[y * w + x] =
            100 +
            500 * std::exp(-.5 * ((x - 5.) * (x - 5.) + (y - 48.) * (y - 48.)) /
                           2.);
    auto edge = run(im);
    check(edge.stars.empty(), "border source rejected");
    bool border = false;
    for (auto &d : edge.sources)
      border |= d.reason == "Border";
    check(border, "border reason retained");
    a.assign(w * h, 100);
    check(run(im).stars.empty(), "constant sky");
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        a[y * w + x] = 100;
        for (double cx : {45., 51.})
          a[y * w + x] +=
              500 *
              std::exp(-.5 * ((x - cx) * (x - cx) + (y - 48.) * (y - 48.)) /
                       1.44);
      }
    auto blended = run(im);
    check(blended.stars.empty(), "blend rejected");
    bool blend = false;
    for (auto &d : blended.sources)
      blend |= d.reason == "Blend";
    check(blend, "blend reason");
    a[0] = NAN;
    check(std::get<Error>(StarExtractor::extract(im)).code ==
              ErrorCode::InvalidInput,
          "nonfinite");
    std::cout << "Extraction controls passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
