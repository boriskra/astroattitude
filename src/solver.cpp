#include "internal.hpp"
#include <queue>

namespace astroattitude {
#ifdef AA_TEST_HOOKS
namespace testing { std::atomic_bool *candidate_seen=nullptr; std::atomic_bool *release_candidate=nullptr; }
#endif
namespace {
struct Budget {
  SolveLimits limits;
  Cancellation cancel;
  uint64_t comparisons = 0, node_visits = 0;
  void visit() {
    if (++node_visits > 20000000)
      fail(ErrorCode::ResourceLimitExceeded,
           "Search traversal budget exhausted");
    if ((node_visits & 1023) == 0)
      check();
  }
  uint32_t hypotheses = 0;
  void check() {
    if (cancel.requested && cancel.requested->load(std::memory_order_relaxed))
      fail(ErrorCode::Cancelled, "Solve cancelled");
  }
  void compare() {
    if (++comparisons > limits.maximum_candidate_comparisons)
      fail(ErrorCode::ResourceLimitExceeded,
           "Candidate comparison budget exhausted after " +
               std::to_string(hypotheses) + " hypotheses");
    if ((comparisons & 1023) == 0)
      check();
  }
  void hypothesis() {
    check();
    if (++hypotheses > limits.maximum_hypotheses)
      fail(ErrorCode::ResourceLimitExceeded, "Hypothesis budget exhausted");
  }
};
struct Feature {
  std::array<double, 3> sides;
  std::array<uint32_t, 3> ids;
};
Feature feature(std::array<uint32_t, 3> ids, const std::vector<V> &r) {
  std::array<std::pair<double, uint32_t>, 3> a{
      {{angle(r[ids[1]], r[ids[2]]), ids[0]},
       {angle(r[ids[0]], r[ids[2]]), ids[1]},
       {angle(r[ids[0]], r[ids[1]]), ids[2]}}};
  std::sort(a.begin(), a.end());
  Feature f;
  for (int k = 0; k < 3; k++) {
    f.sides[k] = a[k].first;
    f.ids[k] = a[k].second;
  }
  return f;
}
M initial_rotation(const std::array<uint32_t, 3> &a,
                   const std::array<uint32_t, 3> &b, const std::vector<V> &obs,
                   const std::vector<V> &refs) {
  M h = M::Zero();
  for (int k = 0; k < 3; k++)
    h += refs[b[k]] * obs[a[k]].transpose();
  Eigen::JacobiSVD<M> svd(h, Eigen::ComputeFullU | Eigen::ComputeFullV);
  M d = M::Identity();
  d(2, 2) =
      (svd.matrixU() * svd.matrixV().transpose()).determinant() < 0 ? -1 : 1;
  return svd.matrixU() * d * svd.matrixV().transpose();
}
std::vector<std::pair<uint32_t, uint32_t>>
assign(const M &r, const std::vector<V> &obs, const References &refs,
       const SpatialIndex &tree, double gate, Budget &budget,
       const std::array<std::pair<uint32_t, uint32_t>, 3> &seed) {
  struct Edge {
    double residual;
    uint32_t a, b;
  };
  std::vector<Edge>
      edges; // At most 128*catalog; bound separately before inserting.
  double chord = 2 * sin(gate / 2);
  for (uint32_t a = 0; a < obs.size(); a++) {
    V v = r * obs[a];
    tree.radius(
        tree.root, v, chord * chord,
        [&](uint32_t b, double dist) {
          budget.compare();
          if (refs.visible[b] && dist <= chord * chord) {
            require(edges.size() < 8192, "Assignment edge budget exhausted",
                    ErrorCode::ResourceLimitExceeded);
            edges.push_back({angle(v, refs.rays[b]), a, b});
          }
        },
        [&] { budget.visit(); });
  }
  std::sort(edges.begin(), edges.end(), [](auto &a, auto &b) {
    return std::tie(a.residual, a.a, a.b) < std::tie(b.residual, b.a, b.b);
  });
  std::set<uint32_t> used_a, used_b;
  std::vector<std::pair<uint32_t, uint32_t>> map;
  for (auto [a, b] : seed) {
    if (angle(r * obs[a], refs.rays[b]) > gate)
      return {};
    used_a.insert(a);
    used_b.insert(b);
    map.emplace_back(a, b);
  }
  for (auto &e : edges)
    if (!used_a.count(e.a) && !used_b.count(e.b)) {
      used_a.insert(e.a);
      used_b.insert(e.b);
      map.emplace_back(e.a, e.b);
    }
  std::sort(map.begin(), map.end());
  return map;
}
M information(const M &r, const std::vector<V> &obs, const std::vector<M> &cov,
              const References &refs,
              const std::vector<std::pair<uint32_t, uint32_t>> &map,
              V *gradient) {
  M h = M::Zero();
  if (gradient)
    *gradient = V::Zero();
  for (auto [a, b] : map) {
    V v = r * obs[a];
    M arot = -skew(v), w = M::Identity();
    if (!cov.empty()) {
      M c = r * cov[a] * r.transpose();
      Eigen::Matrix<double, 3, 2> tangent;
      tangent.col(0) = v.unitOrthogonal();
      tangent.col(1) = v.cross(tangent.col(0));
      Eigen::Matrix2d projected = tangent.transpose() * c * tangent;
      w = tangent * projected.inverse() * tangent.transpose();
    }
    require(w.allFinite(), "Unstable covariance",
            ErrorCode::UnobservableGeometry);
    h += arot.transpose() * w * arot;
    if (gradient)
      *gradient += arot.transpose() * w * (v - refs.rays[b]);
  }
  return (h + h.transpose()) / 2;
}
bool observable(const M &h) {
  Eigen::SelfAdjointEigenSolver<M> e(h);
  return e.info() == Eigen::Success && e.eigenvalues().allFinite() &&
         e.eigenvalues()[2] > 0 &&
         e.eigenvalues()[0] / e.eigenvalues()[2] >= 1e-10;
}
M refine(M r, const std::vector<V> &obs, const std::vector<M> &cov,
         const References &refs,
         const std::vector<std::pair<uint32_t, uint32_t>> &map) {
  M cross = M::Zero();
  for (auto [a, b] : map)
    cross += refs.rays[b] * obs[a].transpose();
  Eigen::JacobiSVD<M> svd(cross, Eigen::ComputeFullU | Eigen::ComputeFullV);
  M sign = M::Identity();
  sign(2, 2) =
      (svd.matrixU() * svd.matrixV().transpose()).determinant() < 0 ? -1 : 1;
  r = svd.matrixU() * sign * svd.matrixV().transpose();
  for (int k = 0; k < 20; k++) {
    V g;
    M h = information(r, obs, cov, refs, map, &g);
    if (!observable(h))
      return r;
    V step = -h.ldlt().solve(g);
    if (!step.allFinite())
      return r;
    double n = step.norm();
    if (n < 1e-13)
      return r;
    if (n > .1)
      step *= .1 / n;
    r = Eigen::AngleAxisd(step.norm(), step.normalized()).toRotationMatrix() *
        r;
  }
  return r;
}
void covariance_check(M &c, const V &v) {
  require(c.allFinite() && c.norm() > 0, "Invalid covariance");
  double norm = c.norm();
  require((c - c.transpose()).norm() <= 1e-10 * norm, "Asymmetric covariance");
  c = ((c + c.transpose()) / 2).eval();
  M p = M::Identity() - v * v.transpose();
  require((p * c * p - c).norm() <= 1e-10 * norm,
          "Ray covariance is not tangent");
  c = p * c * p;
  Eigen::SelfAdjointEigenSolver<M> e(c);
  auto d = e.eigenvalues();
  require(e.info() == Eigen::Success && d[2] > 0 &&
              d[0] >= -1e-10 * std::max(std::abs(d[0]), d[2]) &&
              d[1] / d[2] >= 1e-12,
          "Invalid tangent covariance");
}
} // namespace
Outcome<Engine> Engine::create(Catalog cat, AstrometryData data) {
  try {
    require(bool(cat.impl_) && bool(data.impl_), "Empty resources",
            ErrorCode::InvalidResource);
    auto p = std::make_shared<Impl>(Impl{std::move(cat), std::move(data), {}});
    auto &s = p->catalog.impl_->stars;
    std::vector<V> points;
    for (auto &v : s)
      points.push_back(v.icrs);
    SpatialIndex tree(points);
    uint64_t work = 0;
    std::set<std::array<uint32_t, 3>> unique;
    for (uint32_t i = 0; i < s.size(); i++) {
      std::priority_queue<std::pair<double, uint32_t>> nearest;
      auto visit = [&](auto &&self, int n) -> void {
        if (n < 0)
          return;
        require(++work <= 20000000, "Construction work limit",
                ErrorCode::ResourceLimitExceeded);
        auto node = tree.nodes[n];
        double d = (points[i] - points[node.id]).squaredNorm();
        if (node.id != i) {
          nearest.push({d, node.id});
          if (nearest.size() > 16)
            nearest.pop();
        }
        double delta = points[i][node.axis] - points[node.id][node.axis];
        self(self, delta < 0 ? node.left : node.right);
        if (nearest.size() < 16 || delta * delta <= nearest.top().first)
          self(self, delta < 0 ? node.right : node.left);
      };
      visit(visit, tree.root);
      std::vector<uint32_t> ids;
      while (!nearest.empty()) {
        ids.push_back(nearest.top().second);
        nearest.pop();
      }
      for (size_t a = 0; a < ids.size(); a++)
        for (size_t b = a + 1; b < ids.size(); b++) {
          require(++work <= 20000000, "Construction work limit",
                  ErrorCode::ResourceLimitExceeded);
          std::array<uint32_t, 3> t{i, ids[a], ids[b]};
          std::sort(t.begin(), t.end());
          unique.insert(t);
          require(unique.size() * 80 + p->catalog.impl_->bytes +
                          p->data.impl_->bytes <
                      268435456,
                  "Engine construction memory limit",
                  ErrorCode::ResourceLimitExceeded);
        }
    }
    p->triangles.reserve(unique.size());
    for (auto &t : unique)
      p->triangles.push_back({t});
    return Engine(p);
  } catch (const Failure &f) {
    return f.error;
  } catch (const std::bad_alloc &) {
    return Error{
        ErrorCode::ResourceLimitExceeded, "Engine allocation failed", {}};
  } catch (const std::exception &) {
    return Error{ErrorCode::InternalError, "Engine construction failed", {}};
  }
}
Outcome<AttitudeSolution> Engine::solve(const RayObservation &o,
                                        const EarthContext &ctx,
                                        const SolveOptions &options,
                                        Cancellation cancel) const {
  try {
    require(bool(impl_), "Empty engine", ErrorCode::InvalidResource);
    validate_identity(o.identity);
    require(o.stars.size() >= 6, "Need at least six detections",
            ErrorCode::InsufficientObservations);
    require(o.stars.size() <= 128, "Too many detections");
    auto a = options.acceptance;
    auto l = options.limits;
    require(a.minimum_inliers >= 6 && a.minimum_inliers <= o.stars.size() &&
                finite(a.maximum_match_error_rad) &&
                a.maximum_match_error_rad > 0 &&
                a.maximum_match_error_rad <= .01 &&
                finite(a.maximum_fit_rms_rad) && a.maximum_fit_rms_rad > 0 &&
                a.maximum_fit_rms_rad <= a.maximum_match_error_rad,
            "Invalid acceptance criteria");
    if (a.maximum_conditional_sigma_rad)
      require(finite(*a.maximum_conditional_sigma_rad) &&
                  *a.maximum_conditional_sigma_rad > 0,
              "Invalid conditional precision");
    require(l.maximum_hypotheses > 0 && l.maximum_hypotheses <= 50000 &&
                l.maximum_candidate_comparisons > 0 &&
                l.maximum_candidate_comparisons <= 2000000 &&
                l.maximum_workspace_bytes > 0 &&
                l.maximum_workspace_bytes <= 67108864,
            "Invalid solve limits");
    std::vector<RayStar> stars(o.stars.begin(), o.stars.end());
    std::sort(stars.begin(), stars.end(),
              [](auto &a, auto &b) { return a.id < b.id; });
    std::vector<V> obs;
    std::vector<M> cov;
    bool has = stars[0].direction_covariance_rad2.has_value();
    for (size_t k = 0; k < stars.size(); k++) {
      auto &s = stars[k];
      require(k == 0 || stars[k - 1].id != s.id, "Duplicate detection ID");
      V v = vec(s.direction_camera);
      require(v.allFinite() && std::abs(v.norm() - 1) <= 1e-10,
              "Invalid unit ray");
      v.normalize();
      obs.push_back(v);
      require(s.direction_covariance_rad2.has_value() == has,
              "Mixed covariance availability");
      if (has) {
        M c = matrix(*s.direction_covariance_rad2);
        covariance_check(c, v);
        cov.push_back(c);
      }
    }
    M observation_geometry = M::Zero();
    for (auto &v : obs)
      observation_geometry += M::Identity() - v * v.transpose();
    require(observable(observation_geometry),
            "Detection directions do not constrain a rotation",
            ErrorCode::UnobservableGeometry);
    size_t workspace = impl_->triangles.size() * sizeof(Feature) +
                       impl_->catalog.impl_->stars.size() * 160 +
                       stars.size() * 2048 + 1024 * 1024;
    require(workspace <= l.maximum_workspace_bytes,
            "Workspace budget too small", ErrorCode::ResourceLimitExceeded);
    auto refs = references(*impl_->catalog.impl_, *impl_->data.impl_,
                           o.identity, ctx, cancel);
    Budget budget{l, cancel};
    budget.check();
    SpatialIndex tree(refs.rays);
    std::vector<Feature> index;
    index.reserve(impl_->triangles.size());
    size_t triangle_count = 0;
    for (auto &t : impl_->triangles) {
      if (refs.visible[t.ids[0]] && refs.visible[t.ids[1]] &&
          refs.visible[t.ids[2]])
        index.push_back(feature(t.ids, refs.rays));
      if ((++triangle_count & 1023) == 0)
        budget.check();
    }
    // In-place 3D descriptor tree avoids scanning broad shortest-side bands.
    auto build_features = [&](auto &&self, size_t lo, size_t hi,
                              int depth) -> void {
      budget.check();
      if (lo >= hi)
        return;
      size_t mid = (lo + hi) / 2;
      int axis = depth % 3;
      std::nth_element(
          index.begin() + lo, index.begin() + mid, index.begin() + hi,
          [&](const Feature &a, const Feature &b) {
            return a.sides[axis] < b.sides[axis] ||
                   (a.sides[axis] == b.sides[axis] && a.ids < b.ids);
          });
      self(self, lo, mid, depth + 1);
      self(self, mid + 1, hi, depth + 1);
    };
    build_features(build_features, 0, index.size(), 0);
    bool support = false, geometry = false, winner_isolated = false;
    std::optional<M> winner;
    std::vector<std::pair<uint32_t, uint32_t>> winning;
    double winning_rms = 0, winning_max = 0;
    for (uint32_t i = 0; i < obs.size(); i++)
      for (uint32_t j = i + 1; j < obs.size(); j++)
        for (uint32_t k = j + 1; k < obs.size(); k++) {
          budget.check();
          auto f = feature({i, j, k}, obs);
          if (f.sides[0] < 1e-8)
            continue;
          double margin = 2 * a.maximum_match_error_rad + 1e-12;
          std::vector<const Feature *> candidates;
          auto query_features = [&](auto &&self, size_t lo, size_t hi,
                                    int depth) -> void {
            if (lo >= hi)
              return;
            size_t mid = (lo + hi) / 2;
            auto &item = index[mid];
            int axis = depth % 3;
            budget.visit();
            if (std::abs(item.sides[0] - f.sides[0]) <= margin &&
                std::abs(item.sides[1] - f.sides[1]) <= margin &&
                std::abs(item.sides[2] - f.sides[2]) <= margin) {
              require(candidates.size() < 8192,
                      "Descriptor candidate storage limit",
                      ErrorCode::ResourceLimitExceeded);
              candidates.push_back(&item);
            }
            if (f.sides[axis] - margin <= item.sides[axis])
              self(self, lo, mid, depth + 1);
            if (f.sides[axis] + margin >= item.sides[axis])
              self(self, mid + 1, hi, depth + 1);
          };
          query_features(query_features, 0, index.size(), 0);
          std::sort(candidates.begin(), candidates.end(),
                    [](auto a, auto b) { return a->ids < b->ids; });
          for (auto it : candidates) {
            auto ids = it->ids;
            std::sort(ids.begin(), ids.end());
            do {
              // Edge checks reject permutations before allocating a hypothesis.
              bool fits = true;
              for (int b = 0; b < 3; b++) {
                budget.compare();
                if (std::abs(angle(obs[f.ids[b]], obs[f.ids[(b + 1) % 3]]) -
                             angle(refs.rays[ids[b]],
                                   refs.rays[ids[(b + 1) % 3]])) > margin) {
                  fits = false;
                  break;
                }
              }
              if (!fits)
                continue;
              std::array<std::pair<uint32_t, uint32_t>, 3> seed;
              for (int q = 0; q < 3; q++)
                seed[q] = {f.ids[q], ids[q]};
              budget.hypothesis();
              M r = initial_rotation(f.ids, ids, obs, refs.rays);
              // Reuse a canonical fit only when this branch provably has the
              // identical complete one-to-one assignment: every matched catalog
              // ray is isolated by >2*gate and all initial rays enter its gate.
              if (winner && winner_isolated && winning.size() == obs.size()) {
                bool same = true;
                for (auto [x, y] : seed)
                  if (winning[x].second != y)
                    same = false;
                if (same)
                  for (auto [x, y] : winning) {
                    budget.compare();
                    if (angle(r * obs[x], refs.rays[y]) >
                        a.maximum_match_error_rad) {
                      same = false;
                      break;
                    }
                  }
                if (same) {
                  budget.check();
                  continue;
                }
              }
              auto map = assign(r, obs, refs, tree, a.maximum_match_error_rad,
                                budget, seed);
              if (map.size() < a.minimum_inliers)
                continue;
              for (int iteration = 0; iteration < 5; iteration++) {
                M next_rotation = refine(r, obs, cov, refs, map);
                if ((next_rotation - r).norm() < 1e-13) {
                  r = next_rotation;
                  break;
                }
                r = next_rotation;
                auto next = assign(r, obs, refs, tree,
                                   a.maximum_match_error_rad, budget, seed);
                if (next == map)
                  break;
                map = std::move(next);
                if (map.size() < a.minimum_inliers)
                  break;
              }
              if (map.size() < a.minimum_inliers)
                continue;
              int outside = 0;
              for (auto [x, y] : map)
                if (x != f.ids[0] && x != f.ids[1] && x != f.ids[2])
                  outside++;
              if (outside < 3)
                continue;
              support = true;
              M h = information(r, obs, cov, refs, map, nullptr);
              if (!observable(h))
                continue;
              geometry = true;
              double sum = 0, max = 0;
              for (auto [x, y] : map) {
                budget.compare();
                double v = angle(r * obs[x], refs.rays[y]);
                sum += v * v;
                max = std::max(max, v);
              }
              double rms = sqrt(sum / map.size());
              if (max > a.maximum_match_error_rad ||
                  rms > a.maximum_fit_rms_rad)
                continue;
              if (winner) {
                M delta = r * winner->transpose();
                double distance = Eigen::AngleAxisd(delta).angle();
                if (map != winning || distance > 1e-8)
                  fail(ErrorCode::AmbiguousMatch,
                       "Competing accepted correspondence/attitude");
              } else {
                winner = r;
#ifdef AA_TEST_HOOKS
                if(testing::candidate_seen){testing::candidate_seen->store(true);while(!testing::release_candidate->load()){};}
#endif
                winning = map;
                winning_rms = rms;
                winning_max = max;
                winner_isolated = winning.size() == obs.size();
                double isolation = 2 * sin(a.maximum_match_error_rad);
                if (winner_isolated)
                  for (auto [x, y] : winning) {
                    tree.radius(
                        tree.root, refs.rays[y], isolation * isolation,
                        [&](uint32_t other, double distance) {
                          budget.compare();
                          if (other != y && distance <= isolation * isolation)
                            winner_isolated = false;
                        },
                        [&] { budget.visit(); });
                  }
              }
              budget.check();
            } while (std::next_permutation(ids.begin(), ids.end()));
          }
        }
    budget.check();
    if (!winner)
      fail(!support    ? ErrorCode::NoMatch
           : !geometry ? ErrorCode::UnobservableGeometry
                       : ErrorCode::ResidualLimitExceeded,
           "Completed search has no accepted attitude");
    M r = *winner;
    require((r.transpose() * r - M::Identity()).norm() <= 1e-10 &&
                std::abs(r.determinant() - 1) <= 1e-10,
            "Invalid fitted rotation", ErrorCode::InternalError);
    AttitudeSolution result;
    result.identity = o.identity;
    result.R_enu_from_camera = {mat(r)};
    result.observer_used = ctx.observer;
    result.fit_rms_rad = winning_rms;
    result.fit_max_residual_rad = winning_max;
    result.catalog_content_id = impl_->catalog.impl_->id;
    result.astrometry_content_id = impl_->data.impl_->id;
    result.implementation_version = "0.1.0";
    result.search = {"knn16-triangle/1", budget.hypotheses, budget.comparisons};
    result.models = {
        "erfa-2.0.1-iau2006a-tt/1",
        refs.predicted ? "predicted" : "observed",
        std::holds_alternative<Atmosphere>(ctx.refraction) ? "modeled"
                                                           : "disabled",
        {"total_accuracy_not_evaluated", "nuisance_parameters_fixed",
         "catalog_uncertainty_excluded", "tt_approximates_tdb",
         "celestial_pole_offsets_omitted", "eop_subdaily_corrections_omitted",
         "temporal_model_caller_asserted"}};
    auto &flags = result.models.limitations;
    if (!has)
      flags.push_back("measurement_noise_unavailable");
    if (!o.identity.exposure.midpoint_sigma_s)
      flags.push_back("timing_uncertainty_unavailable");
    flags.push_back(std::holds_alternative<Atmosphere>(ctx.refraction)
                        ? "refraction_model_error"
                        : "refraction_disabled");
    if (refs.predicted)
      flags.push_back("predicted_eop");
    std::set<uint32_t> used;
    for (auto [x, y] : winning) {
      auto &s = impl_->catalog.impl_->stars[y];
      result.matches.push_back({stars[x].id,
                                impl_->catalog.impl_->ns + ":" + s.id,
                                angle(r * obs[x], refs.rays[y])});
      used.insert(x);
      if (s.motion)
        flags.push_back("assumed_catalog_motion");
      if (s.parallax)
        flags.push_back("assumed_catalog_parallax");
      if (s.radial)
        flags.push_back("assumed_catalog_radial_velocity");
    }
    for (uint32_t n = 0; n < stars.size(); n++)
      if (!used.count(n))
        result.rejected_detections.push_back(stars[n].id);
    std::sort(flags.begin(), flags.end());
    flags.erase(std::unique(flags.begin(), flags.end()), flags.end());
    if (has) {
      M h = information(r, obs, cov, refs, winning, nullptr);
      M c = h.inverse();
      require(c.allFinite(), "Invalid information inverse",
              ErrorCode::UnobservableGeometry);
      result.conditional_uncertainty = ConditionalUncertainty{mat(c)};
      if (a.maximum_conditional_sigma_rad) {
        Eigen::SelfAdjointEigenSolver<M> e(c);
        require(sqrt(e.eigenvalues()[2]) <= *a.maximum_conditional_sigma_rad,
                "Conditional precision threshold not met",
                ErrorCode::ConditionalPrecisionNotMet);
      }
    } else if (a.maximum_conditional_sigma_rad)
      fail(ErrorCode::UncertaintyUnavailable, "Measurement noise unavailable");
    budget.check();
    return result;
  } catch (const Failure &f) {
    return f.error;
  } catch (const std::bad_alloc &) {
    return Error{
        ErrorCode::ResourceLimitExceeded, "Solve allocation failed", {}};
  } catch (const std::exception &) {
    return Error{ErrorCode::InternalError, "Unexpected solve failure", {}};
  }
}
Outcome<AttitudeSolution> Engine::solve(const PixelObservation &o,
                                        const CameraCalibration &c,
                                        const EarthContext &ctx,
                                        const SolveOptions &options,
                                        Cancellation cancel) const {
  try {
    validate_identity(o.identity);
    identifier(c.revision);
    identifier(c.camera_configuration_id);
    require(
        o.identity.calibration_revision == c.revision &&
            o.identity.camera_configuration_id == c.camera_configuration_id &&
            o.width == c.width && o.height == c.height,
        "Calibration/configuration mismatch", ErrorCode::CalibrationMismatch);
    require(c.width > 0 && c.width <= 32768 && c.height > 0 &&
                c.height <= 32768,
            "Invalid raster");
    require(o.stars.size() >= 6, "Need six detections",
            ErrorCode::InsufficientObservations);
    require(o.stars.size() <= 128, "Too many detections");
    auto m = c.model;
    for (double x : {m.fx_px, m.fy_px, m.cx_px, m.cy_px, m.k1, m.k2, m.k3, m.p1,
                     m.p2, c.valid_region.min_x, c.valid_region.min_y,
                     c.valid_region.max_x, c.valid_region.max_y})
      require(finite(x), "Nonfinite calibration");
    require(m.fx_px > 0 && m.fy_px > 0 &&
                c.valid_region.min_x <= c.valid_region.max_x &&
                c.valid_region.min_y <= c.valid_region.max_y,
            "Invalid calibration bounds");
    auto distort = [&](Eigen::Vector2d v, double t, Eigen::Matrix2d *jac) {
      double x = v.x(), y = v.y(), r = x * x + y * y,
             a = 1 + t * (m.k1 * r + m.k2 * r * r + m.k3 * r * r * r),
             d = t * (2 * m.k1 + 4 * m.k2 * r + 6 * m.k3 * r * r);
      if (jac)
        *jac << a + x * x * d + 2 * t * m.p1 * y + 6 * t * m.p2 * x,
            x * y * d + 2 * t * m.p1 * x + 2 * t * m.p2 * y,
            x * y * d + 2 * t * m.p1 * x + 2 * t * m.p2 * y,
            a + y * y * d + 6 * t * m.p1 * y + 2 * t * m.p2 * x;
      return Eigen::Vector2d(
          x * a + t * (2 * m.p1 * x * y + m.p2 * (r + 2 * x * x)),
          y * a + t * (m.p1 * (r + 2 * y * y) + 2 * m.p2 * x * y));
    };
    std::vector<RayStar> rays;
    for (auto &s : o.stars) {
      if (cancel.requested && cancel.requested->load(std::memory_order_relaxed))
        fail(ErrorCode::Cancelled, "Pixel solve cancelled");
      require(finite(s.pixel.x) && finite(s.pixel.y) && s.pixel.x >= 0 &&
                  s.pixel.y >= 0 && s.pixel.x <= c.width - 1 &&
                  s.pixel.y <= c.height - 1 &&
                  s.pixel.x >= c.valid_region.min_x &&
                  s.pixel.x <= c.valid_region.max_x &&
                  s.pixel.y >= c.valid_region.min_y &&
                  s.pixel.y <= c.valid_region.max_y,
              "Pixel outside calibration", ErrorCode::OutsideCalibrationDomain);
      if (s.flux)
        require(finite(*s.flux) && *s.flux > 0, "Invalid flux");
      Eigen::Vector2d target((s.pixel.x - m.cx_px) / m.fx_px,
                             (s.pixel.y - m.cy_px) / m.fy_px),
          v = target;
      // A conservative certificate for the central homotopy branch: on the
      // radius-R disk, ||J-I||_infinity < 1/2. Brown's Jacobian is symmetric,
      // hence positive definite throughout that convex disk for every stage.
      // R >= 2*|target| also keeps the unique root inside the disk.
      double radius=std::max(2*target.norm(),1e-9),r2=radius*radius;
      double radial_bound=std::abs(m.k1)*r2+std::abs(m.k2)*r2*r2+std::abs(m.k3)*r2*r2*r2;
      double derivative_bound=2*std::abs(m.k1)+4*std::abs(m.k2)*r2+6*std::abs(m.k3)*r2*r2;
      double bound=radial_bound+2*r2*derivative_bound+8*radius*(std::abs(m.p1)+std::abs(m.p2));
      require(finite(bound)&&bound<.5,"Central Brown inverse cannot be certified",ErrorCode::OutsideCalibrationDomain);
      Eigen::Matrix2d jac;
      for (int stage = 1; stage <= 32; stage++) {
        bool ok = false;
        for (int k = 0; k < 30; k++) {
          Eigen::Vector2d delta = distort(v, stage / 32., &jac) - target;
          require(jac.allFinite() && jac.determinant() > 0,
                  "Unsupported inverse branch",
                  ErrorCode::OutsideCalibrationDomain);
          if (delta.norm() < 1e-13) {
            ok = true;
            break;
          }
          v -= jac.inverse() * delta;
          require(v.allFinite() && v.norm() <= radius, "Unprojection diverged",
                  ErrorCode::OutsideCalibrationDomain);
        }
        require(ok, "Unprojection did not converge",
                ErrorCode::OutsideCalibrationDomain);
      }
      Eigen::Vector2d residual = distort(v, 1, &jac) - target;
      require(
          std::hypot(residual.x() * m.fx_px, residual.y() * m.fy_px) <= 1e-6,
          "Unprojection forward error", ErrorCode::OutsideCalibrationDomain);
      V raw(v.x(), v.y(), 1), unit = raw.normalized();
      RayStar r{s.id, {unit.x(), unit.y(), unit.z()}, std::nullopt};
      if (s.centroid_covariance_px2) {
        auto a = s.centroid_covariance_px2->row_major;
        Eigen::Matrix2d cov;
        cov << a[0], a[1], a[2], a[3];
        require(cov.allFinite() && cov.norm() > 0 &&
                    (cov - cov.transpose()).norm() <= 1e-10 * cov.norm(),
                "Invalid pixel covariance");
        cov = ((cov + cov.transpose()) / 2).eval();
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> eig(cov);
        require(eig.eigenvalues()[0] > 0 &&
                    eig.eigenvalues()[0] / eig.eigenvalues()[1] >= 1e-12,
                "Pixel covariance not positive definite");
        Eigen::Matrix<double, 3, 2> base;
        base << 1, 0, 0, 1, 0, 0;
        Eigen::Matrix2d scale;
        scale << 1 / m.fx_px, 0, 0, 1 / m.fy_px;
        Eigen::Matrix<double, 3, 2> j =
            (M::Identity() - unit * unit.transpose()) / raw.norm() * base *
            jac.inverse() * scale;
        r.direction_covariance_rad2 = mat(j * cov * j.transpose());
      }
      rays.push_back(r);
    }
    return solve(RayObservation{o.identity, rays}, ctx, options, cancel);
  } catch (const Failure &f) {
    return f.error;
  } catch (const std::bad_alloc &) {
    return Error{
        ErrorCode::ResourceLimitExceeded, "Pixel allocation failed", {}};
  } catch (const std::exception &) {
    return Error{ErrorCode::InternalError, "Pixel conversion failed", {}};
  }
}
} // namespace astroattitude
