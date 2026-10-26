#include "astroattitude/extraction.hpp"
#include "internal.hpp"
#include <tuple>

namespace astroattitude {
namespace {
struct Budget {
  uint64_t used = 0, limit;
  Cancellation cancel;
  void tick(uint64_t n = 1) {
    if (cancel.requested && cancel.requested->load(std::memory_order_relaxed))
      fail(ErrorCode::Cancelled, "Extraction cancelled");
    require(n <= limit - used, "Extraction work budget",
            ErrorCode::ResourceLimitExceeded);
    used += n;
  }
};
double median(std::vector<double> &a) {
  if (a.empty())
    return 0;
  auto k = a.begin() + a.size() / 2;
  std::nth_element(a.begin(), k, a.end());
  return *k;
}
bool green(SampleLayout l, int x, int y) {
  if (l == SampleLayout::Mono)
    return true;
  bool diagonal = l == SampleLayout::GRBG || l == SampleLayout::GBRG;
  return (((x + y) & 1) == 0) == diagonal;
}
// Five-point Gauss-Legendre integration over the area of a native pixel.
double profile(const Eigen::Matrix<double, 9, 1> &q, double x, double y) {
  constexpr double z[] = {-.453089922969332, -.269234655052842, 0,
                          .269234655052842, .453089922969332};
  constexpr double w[] = {.118463442528095, .239314335249683, .284444444444444,
                          .239314335249683, .118463442528095};
  double sum = 0, c = std::cos(q[5]), s = std::sin(q[5]);
  for (int i = 0; i < 5; ++i)
    for (int j = 0; j < 5; ++j) {
      double dx = x + z[i] - q[0], dy = y + z[j] - q[1];
      double u = (c * dx + s * dy) / q[3], v = (-s * dx + c * dy) / q[4];
      sum += w[i] * w[j] * std::exp(-.5 * (u * u + v * v));
    }
  return q[6] + q[7] * x + q[8] * y + q[2] * sum;
}
struct Sample {
  double x, y, value;
};
SourceDiagnostic refine(const LinearImage &im, int x, int y, DetectionId id,
                        double noise, const ExtractionOptions &opt,
                        Budget &budget) {
  SourceDiagnostic d{id, {double(x), double(y)}, "FitFailed"};
  d.local_noise_dn = noise;
  constexpr int rad = 10;
  if (x < rad || y < rad || x + rad >= int(im.width) ||
      y + rad >= int(im.height)) {
    d.reason = "Border";
    return d;
  }
  std::vector<Sample> samples;
  std::vector<double> edge;
  bool clipped = false, excluded = false;
  for (int dy = -rad; dy <= rad; ++dy)
    for (int dx = -rad; dx <= rad; ++dx) {
      int xx = x + dx, yy = y + dy;
      size_t k = size_t(yy) * im.width + xx;
      if (!im.excluded.empty() && im.excluded[k]) {
        excluded = true;
        continue;
      }
      int parity = (yy & 1) * 2 + (xx & 1);
      double v =
          im.pixels[size_t(yy) * im.row_stride + xx] - im.black_levels[parity];
      if (dx * dx + dy * dy <= 25 &&
          v >= .98 * (im.white_levels[parity] - im.black_levels[parity]))
        clipped = true;
      if (!green(im.layout, xx, yy))
        continue;
      samples.push_back({double(dx), double(dy), v});
      if (dx * dx + dy * dy > 64)
        edge.push_back(v);
    }
  if (clipped) {
    d.reason = "Clipped";
    return d;
  }
  if (excluded) {
    d.reason = "Excluded";
    return d;
  }
  double bg = median(edge), amp = 0;
  for (auto p : samples)
    amp = std::max(amp, p.value - bg);
  Eigen::Matrix<double, 9, 1> q;
  q << 0, 0, amp, 1.3, 1.8, 0, bg, 0, 0;
  Eigen::Matrix2d moments = Eigen::Matrix2d::Zero();
  double weight = 0;
  for (auto p : samples)
    if (p.x * p.x + p.y * p.y <= 36) {
      double v = std::max(0., p.value - bg);
      Eigen::Vector2d xy(p.x, p.y);
      moments += v * xy * xy.transpose();
      weight += v;
    }
  if (weight > 0) {
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> e(moments / weight);
    q[3] = std::clamp(std::sqrt(std::max(.25, e.eigenvalues()[1] - 1. / 12)),
                      .7, 3.);
    q[4] = std::clamp(std::sqrt(std::max(.25, e.eigenvalues()[0] - 1. / 12)),
                      .7, 3.);
    q[5] = std::atan2(e.eigenvectors()(1, 1), e.eigenvectors()(0, 1));
    if (std::abs(q[3] - q[4]) < .05)
      q[3] += .05;
  }
  auto residual = [&](const auto &a) {
    budget.tick(samples.size() * 25);
    Eigen::VectorXd r(samples.size());
    for (size_t i = 0; i < samples.size(); ++i)
      r[i] = profile(a, samples[i].x, samples[i].y) - samples[i].value;
    return r;
  };
  auto r = residual(q);
  double cost = r.squaredNorm(), lambda = 1e-3;
  bool converged = false;
  for (uint32_t it = 0; it < opt.maximum_fit_iterations; ++it) {
    Eigen::MatrixXd j(samples.size(), 9);
    for (int k = 0; k < 9; ++k) {
      auto qq = q;
      double step = 1e-4 * std::max(1., std::abs(q[k]));
      qq[k] += step;
      j.col(k) = (residual(qq) - r) / step;
    }
    Eigen::Matrix<double, 9, 9> h = j.transpose() * j;
    Eigen::Matrix<double, 9, 1> scale =
        h.diagonal().cwiseMax(1e-10).cwiseSqrt();
    Eigen::Matrix<double, 9, 9> hn = scale.cwiseInverse().asDiagonal() * h *
                                     scale.cwiseInverse().asDiagonal();
    hn.diagonal().array() += lambda;
    Eigen::Matrix<double, 9, 1> delta =
        scale.cwiseInverse().asDiagonal() *
        hn.ldlt().solve(
            -(scale.cwiseInverse().asDiagonal() * j.transpose() * r));
    if (!delta.allFinite())
      break;
    auto next = (q + delta).eval();
    if (std::abs(next[0]) > 4 || std::abs(next[1]) > 4 || next[2] <= 0 ||
        next[3] < .35 || next[4] < .35 || next[3] > 8 || next[4] > 8) {
      lambda *= 10;
      continue;
    }
    auto nr = residual(next);
    double nc = nr.squaredNorm();
    if (nc <= cost) {
      double change = cost - nc;
      q = next;
      r = nr;
      cost = nc;
      lambda = std::max(1e-10, lambda / 3);
      if (delta.norm() < 1e-6 * (1 + q.norm()) || change < 1e-9 * (1 + cost)) {
        converged = true;
        break;
      }
    } else
      lambda *= 10;
    if (lambda > 1e12)
      break;
  }
  d.pixel = {x + q[0], y + q[1]};
  d.amplitude = q[2];
  d.sigma_major_px = std::max(q[3], q[4]);
  d.sigma_minor_px = std::min(q[3], q[4]);
  d.residual_dn = std::sqrt(cost / samples.size());
  // Two distinct native-sample maxima are a blend even when a broad ellipse
  // approximates their sum. This test uses only pixels and the local plane.
  for (const auto &sample : samples) {
    if (std::hypot(sample.x - q[0], sample.y - q[1]) <= 2.5)
      continue;
    double signal = sample.value - q[6] - q[7] * sample.x - q[8] * sample.y;
    if (signal <= std::max(.25 * q[2], 8 * noise))
      continue;
    bool peak = true;
    for (const auto &other : samples) {
      double dx = other.x - sample.x, dy = other.y - sample.y;
      if ((dx == 0 && dy == 0) || std::abs(dx) > 2 || std::abs(dy) > 2)
        continue;
      double v = other.value - q[6] - q[7] * other.x - q[8] * other.y;
      if (v > signal || (v == signal && (dy < 0 || (dy == 0 && dx < 0)))) {
        peak = false;
        break;
      }
    }
    if (peak) {
      d.reason = "Blend";
      return d;
    }
  }
  if (!converged)
    return d;
  if (d.sigma_major_px > 5.5) {
    d.reason = "ExtendedGlare";
    return d;
  }
  if (d.sigma_major_px / d.sigma_minor_px > opt.maximum_axis_ratio) {
    d.reason = "Elongated";
    return d;
  }
  if (q[2] < opt.detection_sigma * std::max(noise, d.residual_dn)) {
    d.reason = "LowContrast";
    return d;
  }
  // A second compact positive residual is a blend warning, not a new star.
  for (size_t i = 0; i < samples.size(); ++i)
    if (std::hypot(samples[i].x - q[0], samples[i].y - q[1]) > 2.5 &&
        -r[i] > std::max(.3 * q[2], 8 * noise)) {
      d.reason = "Blend";
      return d;
    }
  d.reason = "Accepted";
  return d;
}
} // namespace
Outcome<ExtractionResult> StarExtractor::extract(const LinearImage &im,
                                                 const ExtractionOptions &o,
                                                 Cancellation cancel) {
  try {
    require(im.width >= 24 && im.height >= 24 && im.width <= 32768 &&
                im.height <= 32768 && im.row_stride >= im.width,
            "Invalid image dimensions");
    uint64_t n = uint64_t(im.width) * im.height;
    require(n <= o.maximum_pixels && o.maximum_pixels <= 24000000,
            "Image pixel budget", ErrorCode::ResourceLimitExceeded);
    require(im.pixels.size() >=
                uint64_t(im.height - 1) * im.row_stride + im.width,
            "Short pixel buffer");
    require(im.excluded.empty() || im.excluded.size() == n,
            "Mask size mismatch");
    require(int(im.layout) >= 0 && int(im.layout) <= 4, "Unsupported CFA");
    require(o.maximum_candidates >= 1 && o.maximum_candidates <= 2048 &&
                o.maximum_stars >= 1 && o.maximum_stars <= 128 &&
                o.maximum_stars <= o.maximum_candidates,
            "Invalid source limits");
    require(o.maximum_fit_iterations >= 1 && o.maximum_fit_iterations <= 250 &&
                finite(o.detection_sigma) && o.detection_sigma >= 3 &&
                o.detection_sigma <= 20 && finite(o.maximum_axis_ratio) &&
                o.maximum_axis_ratio >= 1 && o.maximum_axis_ratio <= 10,
            "Invalid extraction policy");
    require(n * 32 + uint64_t(o.maximum_candidates) * 8192 + 1048576 <=
                o.maximum_workspace_bytes,
            "Extraction memory budget", ErrorCode::ResourceLimitExceeded);
    for (int k = 0; k < 4; ++k)
      require(finite(im.black_levels[k]) && finite(im.white_levels[k]) &&
                  im.white_levels[k] > im.black_levels[k],
              "Invalid sample levels");
    Budget budget{0, o.maximum_work_units, cancel};
    budget.tick();
    ExtractionResult out;
    std::string policy = J{{"algorithm", "native-linear-2"},
                           {"sigma", o.detection_sigma},
                           {"axis_ratio", o.maximum_axis_ratio},
                           {"candidates", o.maximum_candidates},
                           {"stars", o.maximum_stars},
                           {"iterations", o.maximum_fit_iterations}}
                             .dump();
    out.policy_id = digest(
        {reinterpret_cast<const std::byte *>(policy.data()), policy.size()});
    int w = im.width, h = im.height, tw = (w + 63) / 64, th = (h + 63) / 64;
    std::vector<float> a(n), bg(tw * th), noise(tw * th), signal(n);
    std::vector<uint8_t> usable(n);
    for (int y = 0; y < h; ++y) {
      budget.tick(w);
      for (int x = 0; x < w; ++x) {
        size_t k = size_t(y) * w + x;
        double v = im.pixels[size_t(y) * im.row_stride + x];
        require(finite(v), "Nonfinite pixel");
        int p = (y & 1) * 2 + (x & 1);
        require(finite(v - im.black_levels[p]) &&
                    std::abs(v - im.black_levels[p]) < 1e30,
                "Sample dynamic range exceeded");
        a[k] = v - im.black_levels[p];
        usable[k] =
            green(im.layout, x, y) && (im.excluded.empty() || !im.excluded[k]);
      }
    }
    for (int ty = 0; ty < th; ++ty)
      for (int tx = 0; tx < tw; ++tx) {
        std::vector<double> values;
        for (int y = ty * 64; y < std::min(h, ty * 64 + 64); ++y)
          for (int x = tx * 64; x < std::min(w, tx * 64 + 64); ++x)
            if (usable[size_t(y) * w + x])
              values.push_back(a[size_t(y) * w + x]);
        budget.tick(values.size() * 16);
        double b = median(values);
        for (auto &v : values)
          v = std::abs(v - b);
        double s = std::max(1e-6, 1.4826 * median(values));
        bg[ty * tw + tx] = b;
        noise[ty * tw + tx] = s;
        if (values.size() < 32)
          out.regions.push_back({{double(tx * 64), double(ty * 64),
                                  double(std::min(w - 1, tx * 64 + 63)),
                                  double(std::min(h - 1, ty * 64 + 63))},
                                 "InsufficientSamples"});
      }
    // Local multi-scale averages on native green samples. No CFA interpolation
    // is used for fitting.
    for (int y = 2; y < h - 2; ++y) {
      budget.tick(uint64_t(w) * 34);
      for (int x = 2; x < w - 2; ++x) {
        size_t k = size_t(y) * w + x;
        if (!im.excluded.empty() && im.excluded[k])
          continue;
        double s3 = 0, s5 = 0;
        int c3 = 0, c5 = 0;
        for (int dy = -2; dy <= 2; ++dy)
          for (int dx = -2; dx <= 2; ++dx) {
            size_t q = size_t(y + dy) * w + x + dx;
            if (!usable[q])
              continue;
            s5 += a[q];
            ++c5;
            if (std::abs(dx) <= 1 && std::abs(dy) <= 1) {
              s3 += a[q];
              ++c3;
            }
          }
        int t = (y / 64) * tw + x / 64;
        if (c3 >= 2 && c5 >= 8)
          signal[k] = std::max((s3 / c3 - bg[t]) * std::sqrt(double(c3)),
                               (s5 / c5 - bg[t]) * std::sqrt(double(c5))) /
                      noise[t];
      }
    }
    struct Peak {
      int x, y;
      float score;
    };
    std::vector<Peak> peaks;
    // Bounded per-spatial-cell lists prevent bright glare from consuming the
    // whole candidate budget.
    std::array<std::vector<Peak>, 12> cells;
    size_t per = (o.maximum_candidates + 11) / 12;
    for (int y = 2; y < h - 2; ++y) {
      budget.tick(uint64_t(w) * 26);
      for (int x = 2; x < w - 2; ++x) {
        float s = signal[size_t(y) * w + x];
        if (s < o.detection_sigma)
          continue;
        bool peak = true;
        for (int dy = -2; dy <= 2 && peak; ++dy)
          for (int dx = -2; dx <= 2; ++dx) {
            if (!dx && !dy)
              continue;
            float v = signal[size_t(y + dy) * w + x + dx];
            if (v > s || (v == s && (dy < 0 || (dy == 0 && dx < 0)))) {
              peak = false;
              break;
            }
          }
        if (!peak)
          continue;
        auto &cell = cells[std::min(2, y * 3 / h) * 4 + std::min(3, x * 4 / w)];
        cell.push_back({x, y, s});
        std::stable_sort(cell.begin(), cell.end(),
                         [](auto a, auto b) { return a.score > b.score; });
        if (cell.size() > per) {
          cell.pop_back();
          out.candidate_capacity_reached = true;
        }
      }
    }
    for (size_t rank = 0; rank < per; ++rank)
      for (auto &cell : cells)
        if (rank < cell.size() && peaks.size() < o.maximum_candidates)
          peaks.push_back(cell[rank]);
    DetectionId id = 0;
    for (auto p : peaks) {
      auto d = refine(im, p.x, p.y, ++id, noise[(p.y / 64) * tw + p.x / 64], o,
                      budget);
      if (d.reason == "Accepted") {
        bool near = false;
        for (auto &s : out.stars)
          if (std::hypot(s.pixel.x - d.pixel.x, s.pixel.y - d.pixel.y) < 3)
            near = true;
        if (near)
          d.reason = "Blend";
        else if (out.stars.size() == o.maximum_stars)
          d.reason = "Capacity";
        else
          out.stars.push_back(
              {id, d.pixel, std::nullopt,
               2 * pi * d.amplitude * d.sigma_major_px * d.sigma_minor_px});
      }
      if (d.reason == "Clipped" || d.reason == "ExtendedGlare")
        out.regions.push_back(
            {{double(std::max(0, p.x - 10)), double(std::max(0, p.y - 10)),
              double(std::min(w - 1, p.x + 10)),
              double(std::min(h - 1, p.y + 10))},
             d.reason});
      out.sources.push_back(std::move(d));
    }
    out.work_units = budget.used;
    return out;
  } catch (const Failure &f) {
    return f.error;
  } catch (const std::bad_alloc &) {
    return Error{
        ErrorCode::ResourceLimitExceeded, "Extraction allocation failed", {}};
  } catch (const std::exception &) {
    return Error{ErrorCode::InternalError, "Extraction failed", {}};
  }
}
} // namespace astroattitude
