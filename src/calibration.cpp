#include "astroattitude/calibration.hpp"
#include "internal.hpp"
#include "workflow_json.hpp"
#include <map>
#include <unordered_set>
namespace astroattitude {
namespace {
template <class T, class F> Outcome<T> guarded(F f) {
  try {
    return f();
  } catch (const Failure &e) {
    return e.error;
  } catch (const std::bad_alloc &) {
    return Error{
        ErrorCode::ResourceLimitExceeded, "Calibration allocation failed", {}};
  } catch (const std::exception &) {
    return Error{
        ErrorCode::InvalidInput, "Malformed calibration operation", {}};
  }
}
template <class T> T unwrap(Outcome<T> v) {
  if (auto e = std::get_if<Error>(&v))
    throw Failure{*e};
  return std::get<T>(std::move(v));
}
std::string hash_json(const J &j) {
  auto s = j.dump();
  return digest({reinterpret_cast<const std::byte *>(s.data()), s.size()});
}
std::string candidate_digest(const CandidateData &d) {
  return hash_json(J{{"algorithm", "forward-brown-session/1"},
                     {"data", d.data_digest},
                     {"models", d.models},
                     {"catalog", d.catalog_content_id},
                     {"astrometry", d.astrometry_content_id},
                     {"session", d.session_id},
                     {"model", d.calibration.model},
                     {"region", d.calibration.valid_region},
                     {"configuration", d.calibration.camera_configuration_id},
                     {"width", d.calibration.width},
                     {"height", d.calibration.height}});
}
void verify_restored_candidate(const CandidateData &saved,
                               const CandidateData &computed) {
  require(!saved.physical_accuracy_qualified &&
              saved.id == candidate_digest(saved) &&
              saved.calibration.revision == saved.id,
          "Candidate integrity failure", ErrorCode::InvalidResource);
  require(saved.session_id == computed.session_id &&
              saved.data_digest == computed.data_digest &&
              saved.catalog_content_id == computed.catalog_content_id &&
              saved.astrometry_content_id == computed.astrometry_content_id,
          "Candidate evidence mismatch", ErrorCode::InvalidResource);
  require(saved.calibration.camera_configuration_id ==
                  computed.calibration.camera_configuration_id &&
              saved.calibration.width == computed.calibration.width &&
              saved.calibration.height == computed.calibration.height &&
              J(saved.calibration.valid_region) ==
                  J(computed.calibration.valid_region),
          "Candidate support mismatch", ErrorCode::InvalidResource);
  auto a = J(saved.calibration.model), b = J(computed.calibration.model);
  for (auto it = a.begin(); it != a.end(); ++it) {
    double x = it.value().get<double>(), y = b.at(it.key()).get<double>();
    require(finite(x) && finite(y) &&
                std::abs(x - y) <= 1e-9 * std::max(1., std::abs(y)),
            "Recomputed optics differ beyond portability tolerance",
            ErrorCode::InvalidResource);
  }
  require(saved.models.size() == computed.models.size(),
          "Candidate family mismatch", ErrorCode::InvalidResource);
  for (size_t i = 0; i < saved.models.size(); ++i)
    require(saved.models[i].family == computed.models[i].family &&
                saved.models[i].status == computed.models[i].status &&
                saved.models[i].profile_rank == computed.models[i].profile_rank,
            "Candidate observability mismatch", ErrorCode::InvalidResource);
}
void instant(UtcInstant t) {
  require(t.nanoseconds < 1000000000 && t.unix_seconds >= 0 &&
              t.unix_seconds < 7258118400LL,
          "Invalid UTC instant");
}
double difference(UtcInstant a, UtcInstant b) {
  return double(a.unix_seconds - b.unix_seconds) +
         (double(a.nanoseconds) - double(b.nanoseconds)) * 1e-9;
}
void cancelled(Cancellation c) {
  if (c.requested && c.requested->load(std::memory_order_relaxed))
    fail(ErrorCode::Cancelled, "Calibration cancelled");
}
struct Work {
  uint64_t used = 0, limit;
  Cancellation cancel;
  void tick(uint64_t n = 1) {
    cancelled(cancel);
    require(n <= limit - used, "Calibration work budget",
            ErrorCode::ResourceLimitExceeded);
    used += n;
  }
};
V ray(Vec2 p, const PinholeBrownModel &m) {
  double x = (p.x - m.cx_px) / m.fx_px, y = (p.y - m.cy_px) / m.fy_px,
         radius = std::max(2 * std::hypot(x, y), 1e-9), r2 = radius * radius;
  double bound = std::abs(m.k1) * r2 + std::abs(m.k2) * r2 * r2 +
                 2 * r2 * (2 * std::abs(m.k1) + 4 * std::abs(m.k2) * r2);
  require(finite(bound) && bound < .5,
          "Central Brown inverse cannot be certified",
          ErrorCode::OutsideCalibrationDomain);
  Eigen::Vector2d t(x, y), v = t;
  for (int i = 0; i < 40; ++i) {
    double rr = v.squaredNorm(), a = 1 + m.k1 * rr + m.k2 * rr * rr,
           d = 2 * m.k1 + 4 * m.k2 * rr;
    Eigen::Matrix2d j = Eigen::Matrix2d::Identity() * a + d * v * v.transpose();
    Eigen::Vector2d e = v * a - t;
    if (e.norm() < 1e-13)
      break;
    v -= j.ldlt().solve(e);
    require(v.allFinite() && v.norm() <= radius, "Brown inverse failed",
            ErrorCode::OutsideCalibrationDomain);
  }
  double rr = v.squaredNorm();
  require((v * (1 + m.k1 * rr + m.k2 * rr * rr) - t).norm() *
                  std::max(m.fx_px, m.fy_px) <=
              1e-6,
          "Brown forward check failed", ErrorCode::OutsideCalibrationDomain);
  return V(v.x(), v.y(), 1).normalized();
}
void model_check(const PinholeBrownModel &m, uint32_t w, uint32_t h) {
  for (double v :
       {m.fx_px, m.fy_px, m.cx_px, m.cy_px, m.k1, m.k2, m.k3, m.p1, m.p2})
    require(finite(v), "Nonfinite optics");
  require(m.fx_px > 0 && m.fx_px == m.fy_px && m.k3 == 0 && m.p1 == 0 &&
              m.p2 == 0,
          "Only square-pixel focal/r2/r4 forward Brown supported");
  for (Vec2 p : std::vector<Vec2>{{0, 0},
                                  {double(w - 1), 0},
                                  {0, double(h - 1)},
                                  {double(w - 1), double(h - 1)}})
    ray(p, m);
}
M rotation(const std::vector<V> &cam, const std::vector<V> &ref) {
  require(cam.size() == ref.size() && cam.size() >= 3,
          "Insufficient known directions", ErrorCode::InsufficientObservations);
  M a = M::Zero();
  for (size_t i = 0; i < cam.size(); ++i)
    a += ref[i] * cam[i].transpose();
  Eigen::JacobiSVD<M> svd(a, Eigen::ComputeFullU | Eigen::ComputeFullV);
  require(svd.singularValues()[1] > 1e-8 * svd.singularValues()[0],
          "Unobservable direction geometry", ErrorCode::UnobservableGeometry);
  M d = M::Identity();
  d(2, 2) =
      (svd.matrixU() * svd.matrixV().transpose()).determinant() < 0 ? -1 : 1;
  return svd.matrixU() * d * svd.matrixV().transpose();
}
struct Pair {
  Vec2 pixel;
  V reference;
  std::string source;
};
struct Stored {
  CalibrationFrame frame;
  IngestReceipt receipt;
  std::vector<Pair> pairs;
  bool predicted = false;
  std::vector<std::string> flags;
};
std::vector<std::string> limits() {
  return {"Experimental calibration; not absolute accuracy",
          "Unknown centroid and shared systematic uncertainty",
          "Clock/refraction/catalog/EOP assumptions retained",
          "OIS, focus, mount/thermal/vibration and camera-IMU alignment "
          "unqualified",
          "No actual lens/mount temperature supplied"};
}
struct FitResult {
  PinholeBrownModel model;
  double cost;
  Eigen::MatrixXd jacobian;
  bool converged = false;
};
} // namespace
struct CalibrationSession::Impl {
  Catalog catalog;
  AstrometryData astrometry;
  Engine engine;
  CalibrationConfiguration config;
  CalibrationStage stage = CalibrationStage::Mapping,
                   prior = CalibrationStage::Mapping;
  std::vector<Stored> frames;
  std::vector<BlockCompletion> blocks;
  std::optional<CalibrationCandidate> candidate;
  std::optional<ValidationPlan> plan;
  J actions = J::array();
  std::vector<OpticalModelDiagnostic> last_models;
  void action_budget() const {
    require(actions.size() < 50000, "Journal action budget",
            ErrorCode::ResourceLimitExceeded);
  }
  Impl(Catalog c, AstrometryData a, Engine e, CalibrationConfiguration cfg)
      : catalog(std::move(c)), astrometry(std::move(a)), engine(std::move(e)),
        config(std::move(cfg)) {}
  std::vector<std::string> evidence_limits() const {
    auto out = limits();
    std::set<std::string> flags;
    for (auto &f : frames) {
      if (f.predicted)
        flags.insert("predicted_EOP");
      flags.insert(f.flags.begin(), f.flags.end());
    }
    out.insert(out.end(), flags.begin(), flags.end());
    return out;
  }
  std::string data_hash() const {
    J rows = J::array();
    for (auto &f : frames)
      if (f.frame.role == CaptureRole::Training ||
          f.frame.role == CaptureRole::ModelSelection)
        rows.push_back(f.frame);
    J bs = J::array();
    for (auto &b : blocks)
      if (b.role == CaptureRole::Training ||
          b.role == CaptureRole::ModelSelection)
        bs.push_back(b);
    return hash_json(J{{"frames", rows}, {"blocks", bs}, {"config", config}});
  }
  std::vector<std::vector<Pair>> groups(CaptureRole role) const {
    std::vector<std::vector<Pair>> result;
    for (auto &b : blocks)
      if (b.role == role) {
        std::vector<Pair> g;
        for (auto &f : frames)
          if (f.frame.block_id == b.id && f.receipt.admitted)
            g.insert(g.end(), f.pairs.begin(), f.pairs.end());
        if (g.size() >= 6)
          result.push_back(std::move(g));
      }
    return result;
  }
  Eigen::VectorXd residual(const PinholeBrownModel &m,
                           const std::vector<std::vector<Pair>> &groups,
                           Work &work) const {
    size_t n = 0;
    for (auto &g : groups)
      n += g.size();
    work.tick(n);
    Eigen::VectorXd out(n * 3);
    size_t index = 0;
    for (auto &g : groups) {
      std::vector<V> c, r;
      for (auto &p : g) {
        c.push_back(ray(p.pixel, m));
        r.push_back(p.reference);
      }
      auto rot = rotation(c, r);
      double weight = 1 / std::sqrt(double(g.size()));
      for (size_t i = 0; i < g.size(); ++i) {
        out.segment<3>(index) = (rot * c[i] - r[i]) * weight;
        index += 3;
      }
    }
    return out;
  }
  FitResult optimize(int family, const std::vector<std::vector<Pair>> &groups,
                     Work &work) const {
    int n = family == 0 ? 1 : family == 1 ? 3 : 5;
    auto seed = config.unqualified_seed;
    Eigen::VectorXd q(n), scales(n);
    q[0] = seed.fx_px;
    scales[0] = seed.fx_px * .01;
    if (n >= 3) {
      q[1] = 0;
      q[2] = 0;
      scales[1] = scales[2] = .01;
    }
    if (n == 5) {
      q[3] = seed.cx_px;
      q[4] = seed.cy_px;
      scales[3] = scales[4] = 10;
    }
    auto model = [&](const Eigen::VectorXd &x) {
      auto m = seed;
      m.fx_px = m.fy_px = x[0];
      m.k1 = n >= 3 ? x[1] : 0;
      m.k2 = n >= 3 ? x[2] : 0;
      m.k3 = m.p1 = m.p2 = 0;
      if (n == 5) {
        m.cx_px = x[3];
        m.cy_px = x[4];
      }
      return m;
    };
    auto valid = [&](const Eigen::VectorXd &x) {
      return x.allFinite() && x[0] >= .7 * seed.fx_px &&
             x[0] <= 1.3 * seed.fx_px &&
             (n < 3 || (std::abs(x[1]) <= .2 && std::abs(x[2]) <= .2)) &&
             (n < 5 || (std::abs(x[3] - seed.cx_px) <= .15 * config.width &&
                        std::abs(x[4] - seed.cy_px) <= .15 * config.height));
    };
    auto r = residual(model(q), groups, work);
    double cost = r.squaredNorm(), lambda = 1e-3;
    bool converged = false;
    Eigen::MatrixXd j(r.size(), n);
    for (uint32_t it = 0; it < config.maximum_fit_iterations; ++it) {
      for (int k = 0; k < n; ++k) {
        auto x = q;
        x[k] += scales[k] * 1e-4;
        j.col(k) = (residual(model(x), groups, work) - r) / 1e-4;
      }
      Eigen::MatrixXd h = j.transpose() * j;
      h.diagonal().array() += lambda * std::max(1e-12, h.diagonal().maxCoeff());
      Eigen::VectorXd step = h.ldlt().solve(-j.transpose() * r),
                      next = q + scales.cwiseProduct(step);
      if (!valid(next)) {
        lambda *= 10;
        continue;
      }
      try {
        auto nr = residual(model(next), groups, work);
        double nc = nr.squaredNorm();
        if (nc <= cost) {
          double change = cost - nc;
          q = next;
          r = nr;
          cost = nc;
          lambda = std::max(1e-12, lambda / 3);
          if (step.norm() < 1e-6 || change < 1e-16) {
            converged = true;
            break;
          }
        } else
          lambda *= 10;
      } catch (const Failure &f) {
        if (f.error.code != ErrorCode::OutsideCalibrationDomain)
          throw;
        lambda *= 10;
      }
      if (lambda > 1e12)
        break;
    }
    for (int k = 0; k < n; ++k) {
      auto x = q;
      x[k] += scales[k] * 1e-4;
      j.col(k) = (residual(model(x), groups, work) - r) / 1e-4;
    }
    return {model(q), cost, std::move(j), converged};
  }
  double predict(const PinholeBrownModel &m, Work &work) const {
    std::vector<double> block_scores;
    for (auto &b : blocks)
      if (b.role == CaptureRole::ModelSelection) {
        double sum = 0;
        size_t count = 0;
        for (auto &f : frames)
          if (f.frame.block_id == b.id && f.receipt.admitted &&
              f.pairs.size() >= 6) {
            std::vector<V> c, r;
            for (auto &p : f.pairs) {
              c.push_back(ray(p.pixel, m));
              r.push_back(p.reference);
            }
            for (size_t k = 0; k < c.size(); ++k) {
              work.tick(c.size());
              auto cc = c, rr = r;
              cc.erase(cc.begin() + k);
              rr.erase(rr.begin() + k);
              double e = angle(rotation(cc, rr) * c[k], r[k]);
              sum += e * e;
              ++count;
            }
          }
        require(count > 0, "Model-selection block has no prediction evidence",
                ErrorCode::InsufficientObservations);
        block_scores.push_back(sum / count);
      }
    require(!block_scores.empty(), "Missing model-selection blocks",
            ErrorCode::InsufficientObservations);
    return std::sqrt(
        std::accumulate(block_scores.begin(), block_scores.end(), 0.) /
        block_scores.size());
  }
};
Outcome<CalibrationSession>
CalibrationSession::prepare(Catalog catalog, AstrometryData astrometry,
                            const CalibrationPreparation &p) {
  return guarded<CalibrationSession>([&] {
    require(finite(p.horizontal_fov_rad) && p.horizontal_fov_rad > .05 &&
                p.horizontal_fov_rad < 2.8,
            "Unsupported camera field of view");
    CalibrationConfiguration c;
    c.session_id = p.session_id;
    c.camera_configuration_id = p.camera_configuration_id;
    c.physical_camera_id = p.physical_camera_id;
    c.pixel_convention_id = p.pixel_convention_id;
    c.optical_state_id = p.optical_state_id;
    c.width = p.width;
    c.height = p.height;
    c.remaining_manual_moves = p.remaining_manual_moves;
    double f = p.width / (2 * std::tan(p.horizontal_fov_rad / 2));
    c.unqualified_seed = {
        f, f, (double(p.width) - 1) / 2, (double(p.height) - 1) / 2, 0, 0, 0,
        0, 0};
    return unwrap(create(std::move(catalog), std::move(astrometry), c));
  });
}
Outcome<CalibrationSession>
CalibrationSession::create(Catalog cat, AstrometryData ast,
                           const CalibrationConfiguration &c) {
  return guarded<CalibrationSession>([&] {
    for (auto s :
         {c.session_id, c.camera_configuration_id, c.physical_camera_id,
          c.pixel_convention_id, c.optical_state_id})
      identifier(s);
    require(c.width >= 24 && c.height >= 24 && c.width <= 32768 &&
                c.height <= 32768,
            "Invalid calibration raster");
    model_check(c.unqualified_seed, c.width, c.height);
    require(c.maximum_frames > 0 && c.maximum_frames <= 20000 &&
                c.maximum_blocks > 0 && c.maximum_blocks <= 2048 &&
                c.maximum_fit_iterations > 0 &&
                c.maximum_fit_iterations <= 250 && c.maximum_work_units > 0,
            "Invalid calibration limits");
    require(uint64_t(c.maximum_frames) * 50000 +
                    uint64_t(c.maximum_blocks) * 8192 + 1048576 <=
                c.maximum_memory_bytes,
            "Calibration memory budget", ErrorCode::ResourceLimitExceeded);
    require(c.association.acceptance.minimum_inliers >= 6 &&
                !c.association.acceptance.maximum_conditional_sigma_rad,
            "Calibration association requires six sources; seed uncertainty is "
            "unknown");
    auto e = unwrap(Engine::create(cat, ast));
    return CalibrationSession(std::make_shared<Impl>(
        std::move(cat), std::move(ast), std::move(e), c));
  });
}
Outcome<IngestReceipt> CalibrationSession::add_frame(const CalibrationFrame &f,
                                                     Cancellation cancel) {
  return guarded<IngestReceipt>([&] {
    auto &s = *impl_;
    s.action_budget();
    cancelled(cancel);
    require(s.stage != CalibrationStage::Paused &&
                s.stage != CalibrationStage::Ended &&
                s.stage != CalibrationStage::Complete,
            "Session does not accept frames");
    for (auto x :
         {f.id, f.pose_id, f.block_id, f.camera_configuration_id,
          f.evidence_digest, f.quality_provenance_id, f.timing_provenance_id})
      identifier(x);
    require(f.camera_configuration_id == s.config.camera_configuration_id,
            "ConfigurationChanged", ErrorCode::CalibrationMismatch);
    require(f.stars.size() <= 128, "Source budget");
    require(s.frames.size() < s.config.maximum_frames, "Frame budget",
            ErrorCode::ResourceLimitExceeded);
    instant(f.exposure.midpoint);
    require(int(f.role) >= 0 && int(f.role) <= 3, "Invalid role");
    for (auto &b : s.blocks) {
      require(b.id != f.block_id, "Block already closed");
      require(std::find(b.planned_frame_ids.begin(), b.planned_frame_ids.end(),
                        f.id) == b.planned_frame_ids.end(),
              "Frame ID already accounted for");
    }
    if (!s.blocks.empty()) {
      require(int(f.role) >= int(s.blocks.back().role),
              "Role cannot move backwards");
      require(difference(f.exposure.midpoint, s.blocks.back().end) > 0,
              "Exposure precedes closed block");
    }
    for (auto &old : s.frames) {
      require(old.frame.id != f.id, "Duplicate frame ID");
      if (old.frame.block_id == f.block_id)
        require(old.frame.pose_id == f.pose_id && old.frame.role == f.role,
                "Mixed block identity");
    }
    if (!s.frames.empty())
      require(difference(f.exposure.midpoint,
                         s.frames.back().frame.exposure.midpoint) > 0,
              "Nonchronological exposure");
    if (s.plan) {
      require(f.role == CaptureRole::Validation,
              "Frozen session accepts only validation");
      require(f.pose_id == s.plan->pose_id, "Validation pose changed");
      require(f.quality_provenance_id == s.plan->admission_policy_id,
              "Validation admission policy changed");
      bool slot = false;
      for (auto &b : s.plan->slots)
        if (b.block_id == f.block_id) {
          slot = true;
          require(difference(f.exposure.midpoint, b.start) >= 0 &&
                      difference(b.end, f.exposure.midpoint) >= 0,
                  "Exposure outside validation slot");
        }
      require(slot && difference(f.exposure.midpoint, s.plan->freeze_time) > 0,
              "Not preregistered future exposure");
    } else {
      require(f.role != CaptureRole::Validation, "No frozen validation plan");
      if (!s.frames.empty())
        require(int(f.role) >= int(s.frames.back().frame.role),
                "Role cannot move backwards");
    }
    Stored stored{f, {f.id, f.evidence_digest, "", false, 0}, {}, false};
    ObservationIdentity id{f.id,
                           f.camera_configuration_id,
                           "unqualified-optical-seed",
                           f.exposure,
                           TemporalModel::StationarySingleExposure,
                           f.timing_provenance_id};
    validate_identity(id);
    // Resource validity is checked even for failed/empty or repeated images.
    auto refs =
        references(NativeAccess::get(s.catalog),
                   NativeAccess::get(s.astrometry), id, f.earth, cancel);
    stored.predicted = refs.predicted;
    if (refs.predicted)
      stored.receipt.limitations.push_back("predicted_EOP");
    bool duplicate = false;
    for (auto &old : s.frames)
      duplicate |= old.frame.evidence_digest == f.evidence_digest;
    if (duplicate)
      stored.receipt.reason = "RepeatedPayload";
    else if (f.stars.size() < 6)
      stored.receipt.reason = "InsufficientObservations";
    else {
      auto model = s.plan ? s.candidate->data().calibration
                          : CameraCalibration{"unqualified-optical-seed",
                                              s.config.camera_configuration_id,
                                              s.config.width,
                                              s.config.height,
                                              s.config.unqualified_seed,
                                              {0, 0, double(s.config.width - 1),
                                               double(s.config.height - 1)}};
      id.calibration_revision = model.revision;
      auto solved = s.engine.solve(
          PixelObservation{id, s.config.width, s.config.height, f.stars}, model,
          f.earth, s.config.association, cancel);
      if (auto e = std::get_if<Error>(&solved)) {
        if (e->code == ErrorCode::Cancelled)
          throw Failure{*e};
        stored.receipt.reason = "AssociationRejected";
        stored.receipt.association_error = e->code;
      } else {
        auto &sol = std::get<AttitudeSolution>(solved);
        stored.flags = sol.models.limitations;
        stored.receipt.limitations = stored.flags;
        stored.receipt.matches = sol.matches;
        auto &cat = NativeAccess::get(s.catalog);
        for (auto &match : sol.matches) {
          auto pixel =
              std::find_if(f.stars.begin(), f.stars.end(),
                           [&](auto &p) { return p.id == match.detection_id; });
          auto source =
              std::find_if(cat.stars.begin(), cat.stars.end(), [&](auto &st) {
                return cat.ns + ":" + st.id == match.catalog_star_id;
              });
          require(pixel != f.stars.end() && source != cat.stars.end(),
                  "Association provenance mismatch");
          stored.pairs.push_back({pixel->pixel,
                                  refs.rays[source - cat.stars.begin()],
                                  match.catalog_star_id});
        }
        stored.receipt.admitted = true;
        stored.receipt.reason = "AdmittedNotQualified";
        stored.receipt.matched_sources = stored.pairs.size();
      }
    }
    auto receipt = stored.receipt;
    s.frames.push_back(std::move(stored));
    s.actions.push_back({{"op", "frame"}, {"value", f}});
    if (s.plan)
      s.stage = CalibrationStage::Validation;
    else
      s.stage = f.role == CaptureRole::Training ? CalibrationStage::Mapping
                : f.role == CaptureRole::ModelSelection
                    ? CalibrationStage::ModelSelection
                    : CalibrationStage::Reserve;
    return receipt;
  });
}
Outcome<BlockReceipt>
CalibrationSession::finish_block(const BlockCompletion &b) {
  return guarded<BlockReceipt>([&] {
    auto &s = *impl_;
    s.action_budget();
    require(s.stage != CalibrationStage::Paused &&
                s.stage != CalibrationStage::Ended &&
                s.stage != CalibrationStage::Complete,
            "Session not accepting blocks");
    identifier(b.id);
    identifier(b.pose_id);
    require(int(b.role) >= 0 && int(b.role) <= 3, "Invalid block role");
    instant(b.start);
    instant(b.end);
    require(difference(b.end, b.start) >= 0, "Invalid block interval");
    require(s.blocks.size() < s.config.maximum_blocks, "Block budget",
            ErrorCode::ResourceLimitExceeded);
    for (auto &old : s.blocks)
      require(old.id != b.id, "Duplicate block");
    if (!s.blocks.empty())
      require(difference(b.start, s.blocks.back().end) > 0,
              "Overlapping/nonchronological blocks");
    require(!b.planned_frame_ids.empty() &&
                b.planned_frame_ids.size() <= s.config.maximum_frames &&
                b.missing_frame_ids.size() == b.missing_reasons.size(),
            "Invalid planned/missing frames");
    std::set<std::string> planned, missing, attempted;
    for (auto &id : b.planned_frame_ids) {
      identifier(id);
      require(planned.insert(id).second, "Duplicate planned frame");
      for (auto &old : s.blocks)
        require(std::find(old.planned_frame_ids.begin(),
                          old.planned_frame_ids.end(),
                          id) == old.planned_frame_ids.end(),
                "Planned frame already accounted for");
    }
    for (size_t i = 0; i < b.missing_frame_ids.size(); ++i) {
      identifier(b.missing_reasons[i]);
      require(planned.count(b.missing_frame_ids[i]) &&
                  missing.insert(b.missing_frame_ids[i]).second,
              "Invalid missing frame");
    }
    BlockReceipt result{b.id, "complete", uint32_t(planned.size()), 0, 0};
    for (auto &f : s.frames)
      if (f.frame.block_id == b.id) {
        require(f.frame.pose_id == b.pose_id && f.frame.role == b.role &&
                    planned.count(f.frame.id) && !missing.count(f.frame.id),
                "Block/frame mismatch");
        require(difference(f.frame.exposure.midpoint, b.start) >= 0 &&
                    difference(b.end, f.frame.exposure.midpoint) >= 0,
                "Frame outside block");
        attempted.insert(f.frame.id);
        ++result.attempted;
        result.admitted += f.receipt.admitted;
      }
    require(planned.size() == missing.size() + attempted.size(),
            "Unaccounted planned frames");
    if (result.admitted < 6)
      result.status = "insufficient";
    if (s.plan) {
      require(b.role == CaptureRole::Validation && b.pose_id == s.plan->pose_id,
              "Validation block identity");
      auto slot = std::find_if(s.plan->slots.begin(), s.plan->slots.end(),
                               [&](auto &x) { return x.block_id == b.id; });
      require(slot != s.plan->slots.end() &&
                  difference(b.start, slot->start) >= 0 &&
                  difference(slot->end, b.end) >= 0,
              "Unscheduled block");
    } else {
      require(b.role != CaptureRole::Validation, "Validation before freeze");
      if (!s.blocks.empty())
        require(int(b.role) >= int(s.blocks.back().role),
                "Role cannot move backwards");
    }
    s.blocks.push_back(b);
    s.actions.push_back({{"op", "block"}, {"value", b}});
    s.stage = b.role == CaptureRole::Training ? CalibrationStage::Mapping
              : b.role == CaptureRole::ModelSelection
                  ? CalibrationStage::ModelSelection
              : b.role == CaptureRole::Reserve ? CalibrationStage::Reserve
                                               : CalibrationStage::Validation;
    return result;
  });
}
CalibrationProgress CalibrationSession::progress() const {
  auto &s = *impl_;
  CalibrationProgress p{s.stage,
                        uint32_t(s.frames.size()),
                        uint32_t(s.blocks.size()),
                        0,
                        false,
                        std::vector<uint32_t>(12),
                        limits()};
  for (auto &f : s.frames)
    if (f.receipt.admitted) {
      ++p.admitted_frames;
      for (auto &pair : f.pairs) {
        int x = std::clamp(int(pair.pixel.x * 4 / s.config.width), 0, 3),
            y = std::clamp(int(pair.pixel.y * 3 / s.config.height), 0, 2);
        ++p.occupied_cells[y * 4 + x];
      }
    }
  p.ready_to_fit = !s.plan && s.stage != CalibrationStage::Ended &&
                   s.stage != CalibrationStage::Paused &&
                   s.groups(CaptureRole::Training).size() >= 2 &&
                   s.groups(CaptureRole::ModelSelection).size() >= 1;
  return p;
}
CalibrationAction CalibrationSession::next_action() const {
  auto p = progress();
  if (p.stage == CalibrationStage::Ended)
    return {"ReviewFailure", "SessionEnded", 0};
  if (p.stage == CalibrationStage::Paused)
    return {"ReviewFailure", "SessionPaused", 0};
  if (p.stage == CalibrationStage::Complete) {
    std::vector<uint32_t> cells(12);
    for (auto &f : impl_->frames)
      if (f.frame.role == CaptureRole::Training && f.receipt.admitted)
        for (auto &pair : f.pairs) {
          int x = std::clamp(int(pair.pixel.x * 4 / impl_->config.width), 0, 3);
          int y =
              std::clamp(int(pair.pixel.y * 3 / impl_->config.height), 0, 2);
          ++cells[y * 4 + x];
        }
    if (!std::all_of(cells.begin(), cells.end(), [](auto n) { return n >= 6; }))
      return {"ReviewFailure", "UnsupportedFieldRegion", 0};
    return {"SaveCalibration", "ConsistencyOnly", 0};
  }
  if (p.stage == CalibrationStage::Frozen ||
      p.stage == CalibrationStage::Validation)
    return {"CaptureBurst", "FollowFrozenSchedule", 12};
  if (!impl_->last_models.empty() && !impl_->candidate)
    return {"ReviewFailure", "OpticsNotObservableOrUnsupported", 0};
  if (!p.ready_to_fit &&
      std::any_of(impl_->blocks.begin(), impl_->blocks.end(), [](auto &b) {
        return b.role == CaptureRole::ModelSelection;
      }))
    return {"ReviewFailure", "InsufficientCatalogEvidence", 0};
  if (impl_->candidate &&
      impl_->candidate->data().data_digest == impl_->data_hash())
    return {"PrepareValidation", "CandidateNeedsFreshPlan", 0};
  if (p.ready_to_fit)
    return {"FitCandidate", "TrainingAndSelectionAvailable", 0};
  return {"CaptureBurst",
          impl_->config.remaining_manual_moves == 0
              ? "FixedViewNeedsMoreEvidence"
              : "MappingNeedsCoverage",
          12};
}
Outcome<CalibrationCandidate> CalibrationSession::fit(Cancellation cancel) {
  return guarded<CalibrationCandidate>([&] {
    auto &s = *impl_;
    s.action_budget();
    require(progress().ready_to_fit,
            "Insufficient training/selection or wrong state",
            ErrorCode::InsufficientObservations);
    for (auto &f : s.frames)
      require(std::any_of(s.blocks.begin(), s.blocks.end(),
                          [&](auto &b) { return b.id == f.frame.block_id; }),
              "Unclosed block");
    Work work{0, s.config.maximum_work_units, cancel};
    auto groups = s.groups(CaptureRole::Training);
    CandidateData data;
    data.session_id = s.config.session_id;
    data.catalog_content_id = std::string(s.catalog.content_id());
    data.astrometry_content_id = std::string(s.astrometry.content_id());
    data.data_digest = s.data_hash();
    data.limitations = s.evidence_limits();
    double best = INFINITY;
    std::optional<PinholeBrownModel> chosen;
    for (int family = 0; family < 3; ++family) {
      OpticalModelDiagnostic d;
      d.family = family == 0   ? "focal"
                 : family == 1 ? "focal_radial2"
                               : "focal_center_radial2";
      try {
        auto fit = s.optimize(family, groups, work);
        d.model = fit.model;
        d.training_rms_rad = std::sqrt(fit.cost / groups.size());
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> e(
            fit.jacobian.transpose() * fit.jacobian);
        auto vals = e.eigenvalues();
        d.profile_eigenvalues.assign(vals.data(), vals.data() + vals.size());
        auto weak = e.eigenvectors().col(0).eval();
        d.weakest_mode_scaled.assign(weak.data(), weak.data() + weak.size());
        d.parameter_order = {"focal_scale_1percent_seed"};
        if (family >= 1) {
          d.parameter_order.push_back("forward_k1_scale_0.01");
          d.parameter_order.push_back("forward_k2_scale_0.01");
        }
        if (family == 2) {
          d.parameter_order.push_back("center_x_scale_10px");
          d.parameter_order.push_back("center_y_scale_10px");
        }
        double max = vals[vals.size() - 1];
        for (double v : vals)
          if (v > max / 1e8)
            ++d.profile_rank;
        if (vals[0] > 0)
          d.profile_condition = max / vals[0];
        d.status = fit.converged ? "converged" : "not_converged";
        d.selection_prediction_rms_rad = s.predict(fit.model, work);
        if (d.profile_rank != uint32_t(vals.size()))
          d.status = "weak_geometry";
        if (d.status == "converged" &&
            *d.selection_prediction_rms_rad < best - 1e-12) {
          best = *d.selection_prediction_rms_rad;
          chosen = fit.model;
        }
      } catch (const Failure &f) {
        if (f.error.code == ErrorCode::Cancelled ||
            f.error.code == ErrorCode::ResourceLimitExceeded)
          throw;
        d.status = "unsupported_or_insufficient";
      }
      data.models.push_back(d);
    }
    s.last_models = data.models;
    require(chosen.has_value(), "No observable converged supported model",
            ErrorCode::UnobservableGeometry);
    double minx = s.config.width, miny = s.config.height, maxx = 0, maxy = 0;
    for (auto &f : s.frames)
      if (f.frame.role == CaptureRole::Training && f.receipt.admitted)
        for (auto &p : f.pairs) {
          minx = std::min(minx, p.pixel.x);
          maxx = std::max(maxx, p.pixel.x);
          miny = std::min(miny, p.pixel.y);
          maxy = std::max(maxy, p.pixel.y);
        }
    data.calibration = {"candidate",    s.config.camera_configuration_id,
                        s.config.width, s.config.height,
                        *chosen,        {minx, miny, maxx, maxy}};
    data.limitations.push_back("Valid-region bounding box does not certify "
                               "unsampled interior coverage");
    data.id = candidate_digest(data);
    data.calibration.revision = data.id;
    CalibrationCandidate candidate(std::move(data));
    s.candidate = candidate;
    s.actions.push_back({{"op", "fit"},
                         {"candidate_id", candidate.data().id},
                         {"candidate", candidate.data()}});
    return candidate;
  });
}
std::vector<OpticalModelDiagnostic>
CalibrationSession::model_diagnostics() const {
  return impl_->last_models;
}
Outcome<ValidationPlan>
CalibrationSession::propose_validation(const ValidationRequest &request,
                                       Cancellation cancel) const {
  return guarded<ValidationPlan>([&] {
    auto &s = *impl_;
    require(s.candidate && !s.plan &&
                s.candidate->data().data_digest == s.data_hash(),
            "Fit a current candidate before planning validation");
    identifier(request.id);
    identifier(request.pose_id);
    identifier(request.admission_policy_id);
    instant(request.freeze_time);
    instant(request.first_start);
    require(difference(request.first_start, request.freeze_time) > 0,
            "Validation starts after freeze");
    Work work{0, s.config.maximum_work_units, cancel};
    // Availability only, never selection residuals. Bounded pool and
    // lexicographic tie-breaking.
    std::map<std::string, uint32_t> counts;
    for (auto &f : s.frames)
      if (f.frame.role == CaptureRole::ModelSelection && f.receipt.admitted &&
          f.frame.quality_provenance_id == request.admission_policy_id)
        for (auto &p : f.pairs)
          ++counts[p.source];
    std::vector<std::string> pool;
    for (auto &entry : counts)
      pool.push_back(entry.first);
    std::stable_sort(pool.begin(), pool.end(),
                     [&](auto &a, auto &b) { return counts[a] > counts[b]; });
    if (pool.size() > 12)
      pool.resize(12);
    std::vector<std::string> best;
    uint32_t best_min = 0;
    for (size_t a = 0; a < pool.size(); ++a)
      for (size_t b = a + 1; b < pool.size(); ++b)
        for (size_t c = b + 1; c < pool.size(); ++c)
          for (size_t d = c + 1; d < pool.size(); ++d) {
            std::vector<std::string> panel{pool[a], pool[b], pool[c], pool[d]};
            uint32_t minimum = UINT32_MAX;
            for (auto &block : s.blocks)
              if (block.role == CaptureRole::ModelSelection) {
                uint32_t n = 0;
                for (auto &f : s.frames)
                  if (f.frame.block_id == block.id && f.receipt.admitted &&
                      f.frame.quality_provenance_id ==
                          request.admission_policy_id) {
                    work.tick(f.pairs.size());
                    bool full = true;
                    for (auto &id : panel)
                      full &=
                          std::any_of(f.pairs.begin(), f.pairs.end(),
                                      [&](auto &p) { return p.source == id; });
                    n += full;
                  }
                minimum = std::min(minimum, n);
              }
            if (minimum != UINT32_MAX && minimum > best_min) {
              best_min = minimum;
              best = panel;
            }
          }
    require(best_min >= 6,
            "No four-source panel supports six frames in every selection "
            "block; wait for a suitable field without requesting another move",
            ErrorCode::InsufficientObservations);
    ValidationPlan p;
    p.id = request.id;
    p.pose_id = request.pose_id;
    p.admission_policy_id = request.admission_policy_id;
    p.freeze_time = request.freeze_time;
    p.source_panel = best;
    for (int i = 0; i < 15; ++i) {
      auto start = request.first_start;
      start.unix_seconds += 300 * i;
      auto end = start;
      end.unix_seconds += 150;
      p.slots.push_back({request.id + "-" + std::to_string(i + 1), start, end});
    }
    return p;
  });
}
Outcome<std::string> CalibrationSession::freeze(const CalibrationCandidate &c,
                                                const ValidationPlan &p) {
  return guarded<std::string>([&] {
    auto &s = *impl_;
    s.action_budget();
    require(!s.plan && s.stage != CalibrationStage::Paused &&
                s.stage != CalibrationStage::Ended && s.candidate &&
                s.candidate->data().id == c.data().id &&
                c.data().data_digest == s.data_hash(),
            "Candidate/session mismatch");
    identifier(p.id);
    identifier(p.pose_id);
    identifier(p.admission_policy_id);
    instant(p.freeze_time);
    require(
        p.source_panel.size() == 4 &&
            std::set<std::string>(p.source_panel.begin(), p.source_panel.end())
                    .size() == 4,
        "Four unique fixed panel sources required");
    for (auto &id : p.source_panel) {
      identifier(id);
      require(std::any_of(NativeAccess::get(s.catalog).stars.begin(),
                          NativeAccess::get(s.catalog).stars.end(),
                          [&](auto &st) {
                            return NativeAccess::get(s.catalog).ns + ":" +
                                       st.id ==
                                   id;
                          }),
              "Unknown panel source");
    }
    for (auto &block : s.blocks)
      if (block.role == CaptureRole::ModelSelection) {
        uint32_t complete = 0;
        for (auto &f : s.frames)
          if (f.frame.block_id == block.id && f.receipt.admitted &&
              f.frame.quality_provenance_id == p.admission_policy_id) {
            bool full = true;
            for (auto &id : p.source_panel)
              full &=
                  std::any_of(f.pairs.begin(), f.pairs.end(),
                              [&](auto &pair) { return pair.source == id; });
            complete += full;
          }
        require(complete >= p.minimum_frames_per_block,
                "Fixed panel unavailable in model-selection block",
                ErrorCode::InsufficientObservations);
      }
    require(p.slots.size() >= 2 && p.slots.size() <= s.config.maximum_blocks &&
                p.minimum_frames_per_block >= 6 &&
                p.minimum_frames_per_block <= s.config.maximum_frames &&
                finite(p.minimum_span_s) && p.minimum_span_s >= 4200 &&
                finite(p.maximum_north_change_rad) &&
                p.maximum_north_change_rad > 0 &&
                p.maximum_north_change_rad <= pi &&
                finite(p.maximum_pairwise_change_rad) &&
                p.maximum_pairwise_change_rad > 0 &&
                p.maximum_pairwise_change_rad <= pi,
            "Invalid validation policy");
    if (!s.frames.empty())
      require(difference(p.freeze_time,
                         s.frames.back().frame.exposure.midpoint) > 0,
              "Freeze must follow prior data");
    if (!s.blocks.empty())
      require(difference(p.freeze_time, s.blocks.back().end) > 0,
              "Freeze must follow prior blocks");
    std::set<std::string> ids;
    UtcInstant last = p.freeze_time;
    for (auto &slot : p.slots) {
      identifier(slot.block_id);
      instant(slot.start);
      instant(slot.end);
      require(ids.insert(slot.block_id).second &&
                  difference(slot.start, last) > 0 &&
                  difference(slot.end, slot.start) > 0,
              "Invalid/overlapping future schedule");
      for (auto &b : s.blocks)
        require(b.id != slot.block_id, "Historical validation block");
      last = slot.end;
    }
    require(difference(p.slots.back().end, p.slots.front().start) >=
                p.minimum_span_s,
            "Schedule too short");
    s.plan = p;
    s.stage = CalibrationStage::Frozen;
    s.actions.push_back({{"op", "freeze"}, {"value", p}});
    return hash_json(J{{"candidate", c.data().id}, {"plan", p}});
  });
}
Outcome<ValidationReport> CalibrationSession::validate(Cancellation cancel) {
  return guarded<ValidationReport>([&] {
    auto &s = *impl_;
    s.action_budget();
    require(s.plan && s.candidate, "No frozen candidate");
    cancelled(cancel);
    ValidationReport report;
    report.candidate_id = s.candidate->data().id;
    report.plan_id = hash_json(*s.plan);
    report.status = "incomplete";
    report.limitations = s.evidence_limits();
    std::vector<V> means;
    std::optional<UtcInstant> first, last;
    bool all = true;
    Work work{0, s.config.maximum_work_units, cancel};
    for (auto &slot : s.plan->slots) {
      ValidationBlockResult block;
      block.block_id = slot.block_id;
      block.status = "missing_block";
      auto completion =
          std::find_if(s.blocks.begin(), s.blocks.end(),
                       [&](auto &b) { return b.id == slot.block_id; });
      std::vector<V> north;
      double pairmax = 0;
      if (completion != s.blocks.end()) {
        block.planned_frames = completion->planned_frame_ids.size();
        block.status = "insufficient_panel";
        for (auto &f : s.frames)
          if (f.frame.block_id == slot.block_id && f.receipt.admitted) {
            work.tick(f.pairs.size());
            std::vector<V> c, r;
            for (auto &id : s.plan->source_panel) {
              auto pair = std::find_if(f.pairs.begin(), f.pairs.end(),
                                       [&](auto &x) { return x.source == id; });
              if (pair != f.pairs.end()) {
                c.push_back(
                    ray(pair->pixel, s.candidate->data().calibration.model));
                r.push_back(pair->reference);
              }
            }
            if (c.size() != s.plan->source_panel.size())
              continue;
            auto rot = rotation(c, r);
            north.push_back(rot.transpose() * V(0, 1, 0));
            for (size_t i = 0; i < c.size(); ++i)
              for (size_t j = 0; j < i; ++j)
                pairmax = std::max(
                    pairmax, std::abs(angle(c[i], c[j]) - angle(r[i], r[j])));
            if (!first)
              first = f.frame.exposure.midpoint;
            last = f.frame.exposure.midpoint;
          }
        block.complete_panel_frames = north.size();
        if (north.size() >= s.plan->minimum_frames_per_block) {
          V sum = V::Zero();
          for (auto &n : north)
            sum += n;
          if (sum.norm() > 1e-8 * north.size()) {
            V mean = sum.normalized();
            double scatter = 0;
            for (auto &n : north) {
              double a = angle(n, mean);
              scatter += a * a;
            }
            block.mean_north_camera = Vec3{mean.x(), mean.y(), mean.z()};
            block.scatter_rad = std::sqrt(scatter / north.size());
            block.maximum_pair_error_rad = pairmax;
            block.status = "complete";
            means.push_back(mean);
          } else
            block.status = "ambiguous_mean";
        }
      }
      all &= block.status == "complete";
      report.blocks.push_back(block);
    }
    if (first && last)
      report.span_s = difference(*last, *first);
    if (means.size() >= 2) {
      double change = 0, pair = 0;
      for (size_t i = 0; i < means.size(); ++i) {
        change = std::max(change, angle(means[0], means[i]));
        for (size_t j = 0; j < i; ++j)
          pair = std::max(pair, angle(means[i], means[j]));
      }
      report.max_north_change_rad = change;
      report.max_pairwise_change_rad = pair;
    }
    if (all && report.span_s && *report.span_s >= s.plan->minimum_span_s &&
        report.max_north_change_rad && report.max_pairwise_change_rad) {
      report.status =
          *report.max_north_change_rad < s.plan->maximum_north_change_rad &&
                  *report.max_pairwise_change_rad <
                      s.plan->maximum_pairwise_change_rad
              ? "pass"
              : "target_missed";
    }
    if (report.status == "pass" && s.stage != CalibrationStage::Paused &&
        s.stage != CalibrationStage::Ended)
      s.stage = CalibrationStage::Complete;
    s.actions.push_back({{"op", "validate"}});
    return report;
  });
}
Outcome<CalibrationPackage>
CalibrationSession::export_validated(Cancellation cancel) {
  return guarded<CalibrationPackage>([&] {
    auto r = unwrap(validate(cancel));
    require(r.status == "pass", "InsufficientValidation",
            ErrorCode::InsufficientObservations);
    auto &s = *impl_;
    auto &d = s.candidate->data();
    model_check(d.calibration.model, d.calibration.width, d.calibration.height);
    std::vector<uint32_t> cells(12);
    for (auto &f : s.frames)
      if (f.frame.role == CaptureRole::Training && f.receipt.admitted)
        for (auto &p : f.pairs) {
          int x = std::clamp(int(p.pixel.x * 4 / s.config.width), 0, 3),
              y = std::clamp(int(p.pixel.y * 3 / s.config.height), 0, 2);
          ++cells[y * 4 + x];
        }
    require(
        std::all_of(cells.begin(), cells.end(), [](auto n) { return n >= 6; }),
        "UnsupportedFieldRegion", ErrorCode::OutsideCalibrationDomain);
    return CalibrationPackage{d.calibration,
                              d.id,
                              r.plan_id,
                              d.catalog_content_id,
                              d.astrometry_content_id,
                              "empirical_consistency_only",
                              false};
  });
}
Outcome<CalibrationStage> CalibrationSession::pause() {
  return guarded<CalibrationStage>([&] {
    auto &s = *impl_;
    s.action_budget();
    require(s.stage != CalibrationStage::Ended &&
                s.stage != CalibrationStage::Paused,
            "Cannot pause");
    s.prior = s.stage;
    s.stage = CalibrationStage::Paused;
    s.actions.push_back({{"op", "pause"}});
    return s.stage;
  });
}
Outcome<CalibrationStage> CalibrationSession::resume() {
  return guarded<CalibrationStage>([&] {
    auto &s = *impl_;
    s.action_budget();
    require(s.stage == CalibrationStage::Paused, "Not paused");
    s.stage = s.prior;
    s.actions.push_back({{"op", "resume"}});
    return s.stage;
  });
}
Outcome<CalibrationStage> CalibrationSession::end() {
  return guarded<CalibrationStage>([&] {
    auto &s = *impl_;
    s.action_budget();
    require(s.stage != CalibrationStage::Ended, "Session already ended");
    s.stage = CalibrationStage::Ended;
    s.actions.push_back({{"op", "end"}});
    return s.stage;
  });
}
Outcome<std::string> CalibrationSession::journal() const {
  return guarded<std::string>([&] {
    auto &s = *impl_;
    J j = {{"schema", "astroattitude.calibration-journal/1"},
           {"catalog", s.catalog.content_id()},
           {"astrometry", s.astrometry.content_id()},
           {"configuration", s.config},
           {"actions", s.actions}};
    auto str = j.dump();
    require(str.size() <= s.config.maximum_memory_bytes / 2,
            "Journal byte budget", ErrorCode::ResourceLimitExceeded);
    return str;
  });
}
Outcome<CalibrationSession> CalibrationSession::restore(Catalog c,
                                                        AstrometryData a,
                                                        std::string_view text,
                                                        Cancellation cancel) {
  return guarded<CalibrationSession>([&] {
    auto j = parse_json(
        {reinterpret_cast<const std::byte *>(text.data()), text.size()},
        134217728, 32, 4000000);
    keys(j, {"schema", "catalog", "astrometry", "configuration", "actions"});
    workflow_validate(j);
    require(j.at("schema") == "astroattitude.calibration-journal/1" &&
                j.at("catalog") == c.content_id() &&
                j.at("astrometry") == a.content_id(),
            "Journal resource/schema mismatch", ErrorCode::InvalidResource);
    auto s = unwrap(
        create(c, a, j.at("configuration").get<CalibrationConfiguration>()));
    auto actions = j.at("actions");
    require(actions.is_array() && actions.size() <= 50000,
            "Journal action limit");
    for (auto &action : actions) {
      cancelled(cancel);
      auto op = action.at("op").get<std::string>();
      if (op == "frame")
        unwrap(s.add_frame(action.at("value").get<CalibrationFrame>(), cancel));
      else if (op == "block")
        unwrap(s.finish_block(action.at("value").get<BlockCompletion>()));
      else if (op == "fit") {
        auto candidate = unwrap(s.fit(cancel));
        if (action.contains("candidate")) {
          auto saved = action.at("candidate").get<CandidateData>();
          require(saved.id == action.at("candidate_id").get<std::string>(),
                  "Candidate identity mismatch", ErrorCode::InvalidResource);
          verify_restored_candidate(saved, candidate.data());
          s.impl_->candidate = CalibrationCandidate(saved);
          s.impl_->last_models = saved.models;
          s.impl_->actions.back() = action;
        } else
          require(candidate.data().id ==
                      action.at("candidate_id").get<std::string>(),
                  "Legacy journal requires exact recomputation",
                  ErrorCode::InvalidResource);
      } else if (op == "freeze") {
        require(s.impl_->candidate.has_value(), "Missing candidate");
        unwrap(s.freeze(*s.impl_->candidate,
                        action.at("value").get<ValidationPlan>()));
      } else if (op == "validate")
        unwrap(s.validate(cancel));
      else if (op == "pause")
        unwrap(s.pause());
      else if (op == "resume")
        unwrap(s.resume());
      else if (op == "end")
        unwrap(s.end());
      else
        fail(ErrorCode::InvalidInput, "Unknown journal action");
    }
    return s;
  });
}
} // namespace astroattitude
