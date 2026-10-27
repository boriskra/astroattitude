#include "astroattitude/astroattitude.h"
#include "internal.hpp"
#include "alignment_json.hpp"
#include <charconv>
using namespace astroattitude;
struct aa_calibration_session { CalibrationSession session; };
struct aa_calibration_artifact { const std::variant<CalibrationCandidate,ValidationReport> value; };
struct aa_engine {
  Engine engine;
};
struct aa_message {
  std::string bytes;
};
struct aa_cancel {
  std::atomic_bool value{false};
};
namespace {
const char *codes[] = {"InvalidInput",
                       "InvalidResource",
                       "UnsupportedVersion",
                       "CalibrationMismatch",
                       "OutsideCalibrationDomain",
                       "UnsupportedObservation",
                       "InsufficientObservations",
                       "AstrometryDataUnavailable",
                       "NoMatch",
                       "AmbiguousMatch",
                       "UnobservableGeometry",
                       "ResidualLimitExceeded",
                       "UncertaintyUnavailable",
                       "ConditionalPrecisionNotMet",
                       "ResourceLimitExceeded",
                       "Cancelled",
                       "InternalError"};
std::span<const std::byte> bytes(const uint8_t *p, uint64_t n) {
  return {reinterpret_cast<const std::byte *>(p), size_t(n)};
}
template <class T> T decimal(const J &j) {
  auto s = string(j, 21);
  require(s == "0" || (s[0] != '0' && s != "-0" && s[0] != '+' &&
                       !(s.size() > 1 && s[0] == '-' && s[1] == '0')),
          "Noncanonical integer");
  T v{};
  auto r = std::from_chars(s.data(), s.data() + s.size(), v);
  require(r.ec == std::errc{} && r.ptr == s.data() + s.size(),
          "Invalid decimal integer");
  return v;
}
uint32_t u32(const J &j) {
  auto n = integer(j);
  require(n >= 0 && n <= UINT32_MAX, "Integer outside uint32");
  return uint32_t(n);
}
uint64_t u64(const J &j) {
  require(j.is_number_unsigned() ||
              (j.is_number_integer() && j.get<int64_t>() >= 0),
          "Expected unsigned integer");
  return j.get<uint64_t>();
}
template <class T, class F> std::optional<T> optional(const J &j, F f) {
  if (j.is_null())
    return std::nullopt;
  return f(j);
}
Vec2 v2(const J &j) {
  keys(j, {"x", "y"});
  return {number(j.at("x")), number(j.at("y"))};
}
Vec3 v3(const J &j) {
  keys(j, {"x", "y", "z"});
  return {number(j.at("x")), number(j.at("y")), number(j.at("z"))};
}
template <size_t N> std::array<double, N> arr(const J &j) {
  keys(j, {"row_major"});
  auto &a = j.at("row_major");
  require(a.is_array() && a.size() == N, "Matrix shape mismatch");
  std::array<double, N> r;
  for (size_t n = 0; n < N; n++)
    r[n] = number(a[n]);
  return r;
}
Mat2 m2(const J &j) { return {arr<4>(j)}; }
Mat3 m3(const J &j) { return {arr<9>(j)}; }
ObservationIdentity identity(const J &j) {
  keys(j, {"observation_id", "camera_configuration_id", "calibration_revision",
           "exposure", "temporal_model", "timing_provenance_id"});
  auto &e = j.at("exposure");
  keys(e, {"midpoint", "duration_s", "midpoint_sigma_s"});
  auto &t = e.at("midpoint");
  keys(t, {"unix_seconds", "nanoseconds"});
  auto model = string(j.at("temporal_model"));
  require(model == "stationary_single_exposure" ||
              model == "aligned_effective_exposure",
          "Invalid temporal model");
  return {string(j.at("observation_id")),
          string(j.at("camera_configuration_id")),
          string(j.at("calibration_revision")),
          {{decimal<int64_t>(t.at("unix_seconds")), u32(t.at("nanoseconds"))},
           number(e.at("duration_s")),
           optional<double>(e.at("midpoint_sigma_s"), number)},
          model == "stationary_single_exposure"
              ? TemporalModel::StationarySingleExposure
              : TemporalModel::AlignedEffectiveExposure,
          string(j.at("timing_provenance_id"))};
}
EarthContext context(const J &j) {
  keys(j, {"observer", "refraction", "eop_policy"});
  auto &p = j.at("observer");
  keys(p, {"latitude_rad", "longitude_rad", "ellipsoid_height_m"});
  EarthContext c{{number(p.at("latitude_rad")), number(p.at("longitude_rad")),
                  number(p.at("ellipsoid_height_m"))},
                 NoRefraction{},
                 EopPolicy::ObservedOnly};
  auto policy = string(j.at("eop_policy"));
  require(policy == "observed_only" || policy == "allow_predicted",
          "Invalid EOP policy");
  c.eop_policy = policy == "observed_only" ? EopPolicy::ObservedOnly
                                           : EopPolicy::AllowPredicted;
  auto &r = j.at("refraction");
  require(r.is_object() && r.contains("kind"), "Invalid refraction");
  if (string(r.at("kind")) == "disabled")
    keys(r, {"kind"});
  else {
    keys(r, {"kind", "parameters"});
    require(string(r.at("kind")) == "atmosphere", "Invalid refraction kind");
    auto &a = r.at("parameters");
    keys(a, {"pressure_pa", "temperature_kelvin", "relative_humidity",
             "effective_wavelength_m"});
    c.refraction = Atmosphere{number(a.at("pressure_pa")),
                              number(a.at("temperature_kelvin")),
                              number(a.at("relative_humidity")),
                              number(a.at("effective_wavelength_m"))};
  }
  return c;
}
SolveOptions options(const J &j) {
  keys(j, {"acceptance", "limits"});
  auto &a = j.at("acceptance");
  keys(a, {"minimum_inliers", "maximum_match_error_rad", "maximum_fit_rms_rad",
           "maximum_conditional_sigma_rad"});
  auto &l = j.at("limits");
  keys(l, {"maximum_hypotheses", "maximum_candidate_comparisons",
           "maximum_workspace_bytes"});
  return {{u32(a.at("minimum_inliers")),
           number(a.at("maximum_match_error_rad")),
           number(a.at("maximum_fit_rms_rad")),
           optional<double>(a.at("maximum_conditional_sigma_rad"), number)},
          {u32(l.at("maximum_hypotheses")),
           u64(l.at("maximum_candidate_comparisons")),
           u64(l.at("maximum_workspace_bytes"))}};
}
CameraCalibration calibration(const J &j) {
  keys(j, {"revision", "camera_configuration_id", "width", "height", "model",
           "valid_region"});
  auto &m = j.at("model");
  keys(m, {"fx_px", "fy_px", "cx_px", "cy_px", "k1", "k2", "k3", "p1", "p2"});
  auto &b = j.at("valid_region");
  keys(b, {"min_x", "min_y", "max_x", "max_y"});
  return {string(j.at("revision")),
          string(j.at("camera_configuration_id")),
          u32(j.at("width")),
          u32(j.at("height")),
          {number(m.at("fx_px")), number(m.at("fy_px")), number(m.at("cx_px")),
           number(m.at("cy_px")), number(m.at("k1")), number(m.at("k2")),
           number(m.at("k3")), number(m.at("p1")), number(m.at("p2"))},
          {number(b.at("min_x")), number(b.at("min_y")), number(b.at("max_x")),
           number(b.at("max_y"))}};
}
J ids(const std::vector<DetectionId> &a) {
  J r = J::array();
  for (auto n : a)
    r.push_back(std::to_string(n));
  return r;
}
J error(const Error &e) {
  return {{"schema", "astroattitude.response/1"},
          {"outcome", "error"},
          {"error",
           {{"code", codes[int(e.code)]},
            {"detail", e.detail.substr(0, 1024)},
            {"affected_detections", ids(e.affected_detections)}}}};
}
J encoded_identity(const ObservationIdentity &i) {
  J sigma = nullptr;
  if (i.exposure.midpoint_sigma_s)
    sigma = *i.exposure.midpoint_sigma_s;
  return {
      {"observation_id", i.observation_id},
      {"camera_configuration_id", i.camera_configuration_id},
      {"calibration_revision", i.calibration_revision},
      {"timing_provenance_id", i.timing_provenance_id},
      {"temporal_model",
       i.temporal_model == TemporalModel::StationarySingleExposure
           ? "stationary_single_exposure"
           : "aligned_effective_exposure"},
      {"exposure",
       {{"midpoint",
         {{"unix_seconds", std::to_string(i.exposure.midpoint.unix_seconds)},
          {"nanoseconds", i.exposure.midpoint.nanoseconds}}},
        {"duration_s", i.exposure.duration_s},
        {"midpoint_sigma_s", sigma}}}};
}
J encoded(const AttitudeSolution &s) {
  J matches = J::array();
  for (auto &m : s.matches)
    matches.push_back({{"detection_id", std::to_string(m.detection_id)},
                       {"catalog_star_id", m.catalog_star_id},
                       {"angular_residual_rad", m.angular_residual_rad}});
  J cov = nullptr;
  if (s.conditional_uncertainty)
    cov = {{"covariance_enu_rad2",
            {{"row_major",
              s.conditional_uncertainty->covariance_enu_rad2.row_major}}}};
  J v = {{"identity", encoded_identity(s.identity)},
         {"R_enu_from_camera",
          {{"matrix", {{"row_major", s.R_enu_from_camera.matrix.row_major}}}}},
         {"observer_used",
          {{"latitude_rad", s.observer_used.latitude_rad},
           {"longitude_rad", s.observer_used.longitude_rad},
           {"ellipsoid_height_m", s.observer_used.ellipsoid_height_m}}},
         {"conditional_uncertainty", cov},
         {"fit_rms_rad", s.fit_rms_rad},
         {"fit_max_residual_rad", s.fit_max_residual_rad},
         {"matches", matches},
         {"rejected_detections", ids(s.rejected_detections)},
         {"catalog_content_id", s.catalog_content_id},
         {"astrometry_content_id", s.astrometry_content_id},
         {"implementation_version", s.implementation_version},
         {"search",
          {{"algorithm_version", s.search.algorithm_version},
           {"hypotheses_evaluated", s.search.hypotheses_evaluated},
           {"candidate_comparisons", s.search.candidate_comparisons}}},
         {"models",
          {{"model_id", s.models.model_id},
           {"earth_orientation_status", s.models.earth_orientation_status},
           {"refraction_status", s.models.refraction_status},
           {"limitations", s.models.limitations}}}};
  return {{"schema", "astroattitude.response/1"},
          {"outcome", "success"},
          {"solution", v}};
}
void output(aa_message **p, const J &j) {
  auto text = j.dump();
  require(text.size() <= 262144, "Response too large",
          ErrorCode::ResourceLimitExceeded);
  *p = new aa_message{std::move(text)};
}
} // namespace
extern "C" {
uint32_t aa_abi_version() { return 1; }
int32_t aa_engine_create_v1(const uint8_t *c, uint64_t cn, const uint8_t *d,
                            uint64_t dn, aa_engine **out, aa_message **err) {
  if (out)
    *out = nullptr;
  if (err)
    *err = nullptr;
  if (!out || !err || (!c && cn) || (!d && dn))
    return 2;
  try {
    auto cat = Catalog::load(bytes(c, cn));
    if (auto e = std::get_if<Error>(&cat)) {
      output(err, error(*e));
      return 1;
    }
    auto data = AstrometryData::load(bytes(d, dn));
    if (auto e = std::get_if<Error>(&data)) {
      output(err, error(*e));
      return 1;
    }
    auto engine =
        Engine::create(std::get<Catalog>(cat), std::get<AstrometryData>(data));
    if (auto e = std::get_if<Error>(&engine)) {
      output(err, error(*e));
      return 1;
    }
    *out = new aa_engine{std::get<Engine>(engine)};
    return 0;
  } catch (const std::bad_alloc &) {
    return 3;
  } catch (...) {
    return 4;
  }
}
void aa_engine_destroy_v1(aa_engine *p) { delete p; }
int32_t aa_solve_v1(const aa_engine *e, const uint8_t *p, uint64_t n,
                    const aa_cancel *c, aa_message **out) {
  if (out)
    *out = nullptr;
  if (!e || !out || (!p && n))
    return 2;
  try {
    try {
      auto j = parse_json(bytes(p, n), 262144, 12, 20000);
      keys(j, {"schema", "kind", "observation", "calibration", "context",
               "options"});
      require(string(j.at("schema")) == "astroattitude.solve/1",
              "Unsupported solve schema", ErrorCode::UnsupportedVersion);
      auto kind = string(j.at("kind"));
      auto &o = j.at("observation");
      auto ctx = context(j.at("context"));
      auto opt = options(j.at("options"));
      auto identity_value = identity(o.at("identity"));
      auto &a = o.at("stars");
      require(a.is_array() && a.size() <= 128, "Invalid detections");
      Cancellation cancel{c ? &c->value : nullptr};
      Outcome<AttitudeSolution> result =
          Error{ErrorCode::InternalError, "Uninitialized solve", {}};
      if (kind == "rays") {
        keys(o, {"identity", "stars"});
        require(j.at("calibration").is_null(), "Ray calibration must be null");
        std::vector<RayStar> stars;
        for (auto &s : a) {
          keys(s, {"id", "direction_camera", "direction_covariance_rad2"});
          stars.push_back(
              {decimal<uint64_t>(s.at("id")), v3(s.at("direction_camera")),
               optional<Mat3>(s.at("direction_covariance_rad2"), m3)});
        }
        result = e->engine.solve(RayObservation{identity_value, stars}, ctx,
                                 opt, cancel);
      } else {
        require(kind == "pixels", "Unknown observation kind");
        keys(o, {"identity", "width", "height", "stars"});
        std::vector<PixelStar> stars;
        for (auto &s : a) {
          keys(s, {"id", "pixel", "centroid_covariance_px2", "flux"});
          stars.push_back({decimal<uint64_t>(s.at("id")), v2(s.at("pixel")),
                           optional<Mat2>(s.at("centroid_covariance_px2"), m2),
                           optional<double>(s.at("flux"), number)});
        }
        result =
            e->engine.solve(PixelObservation{identity_value, u32(o.at("width")),
                                             u32(o.at("height")), stars},
                            calibration(j.at("calibration")), ctx, opt, cancel);
      }
      if (auto error_value = std::get_if<Error>(&result)) {
        output(out, error(*error_value));
        return 1;
      }
      output(out, encoded(std::get<AttitudeSolution>(result)));
      return 0;
    } catch (const Failure &f) {
      output(out, error(f.error));
      return 1;
    } catch (const J::exception &) {
      output(out, error({ErrorCode::InvalidInput, "Malformed request", {}}));
      return 1;
    }
  } catch (const std::bad_alloc &) {
    return 3;
  } catch (...) {
    return 4;
  }
}
const uint8_t *aa_message_bytes_v1(const aa_message *p, uint64_t *n) {
  if (!n)
    return nullptr;
  *n = p ? p->bytes.size() : 0;
  return p ? reinterpret_cast<const uint8_t *>(p->bytes.data()) : nullptr;
}
void aa_message_destroy_v1(aa_message *p) { delete p; }
int32_t aa_cancel_create_v1(aa_cancel **p) {
  if (!p)
    return 2;
  *p = nullptr;
  try {
    *p = new aa_cancel;
    return 0;
  } catch (const std::bad_alloc &) {
    return 3;
  } catch (...) {
    return 4;
  }
}
void aa_cancel_request_v1(aa_cancel *p) {
  if (p)
    p->value.store(true, std::memory_order_relaxed);
}
void aa_cancel_destroy_v1(aa_cancel *p) { delete p; }
}
namespace {
template <class T> T workflow_value(Outcome<T> v) {
  if (auto e = std::get_if<Error>(&v))
    throw Failure{*e};
  return std::get<T>(std::move(v));
}
void workflow_output(aa_message **out, const J &value) {
  auto text = J{
      {"schema", "astroattitude.workflow-response/1"},
      {"outcome", "success"},
      {"value",
       value}}.dump();
  require(text.size() <= 134217728, "Workflow response budget",
          ErrorCode::ResourceLimitExceeded);
  *out = new aa_message{std::move(text)};
}
template <class F> int32_t workflow_call(aa_message **out, F f) {
  if (!out)
    return 2;
  *out = nullptr;
  try {
    try {
      f();
      return 0;
    } catch (const Failure &e) {
      output(out, error(e.error));
      return 1;
    } catch (const std::exception &e) {
      if (dynamic_cast<const std::bad_alloc *>(&e))
        throw;
      output(
          out,
          error({ErrorCode::InvalidInput, "Malformed workflow request", {}}));
      return 1;
    }
  } catch (const std::bad_alloc &) {
    return 3;
  } catch (...) {
    return 4;
  }
}
} // namespace
extern "C" {
int32_t aa_alignment_measure_v1(const uint8_t *p, uint64_t n,
                                const aa_cancel *cancel, aa_message **out) {
  if (!p && n) {
    if (out)
      *out = nullptr;
    return 2;
  }
  return workflow_call(out, [&] {
    auto j = parse_json(bytes(p, n), 1048576, 16, 50000);
    keys(j, {"schema", "value"});
    workflow_validate(j);
    require(j.at("schema") == "astroattitude.alignment/1", "Alignment schema",
            ErrorCode::UnsupportedVersion);
    workflow_output(out, workflow_value(measure_alignment(
                             j.at("value").get<AlignmentRequest>(),
                             {cancel ? &cancel->value : nullptr})));
  });
}
int32_t aa_rig_alignment_v1(const uint8_t *p, uint64_t n,
                            const aa_cancel *cancel, aa_message **out) {
  if (!p && n) {
    if (out)
      *out = nullptr;
    return 2;
  }
  return workflow_call(out, [&] {
    auto j = parse_json(bytes(p, n), 6 * 1048576, 20, 300000);
    keys(j, {"schema", "value"});
    workflow_validate(j);
    require(j.at("schema") == "astroattitude.rig-alignment/1",
            "Rig alignment schema", ErrorCode::UnsupportedVersion);
    workflow_output(out, workflow_value(calibrate_rig_alignment(
                             j.at("value").get<RigAlignmentRequest>(),
                             {cancel ? &cancel->value : nullptr})));
  });
}
int32_t aa_planar_pose_v1(const uint8_t *p, uint64_t n, const aa_cancel *cancel,
                          aa_message **out) {
  if (!p && n) {
    if (out)
      *out = nullptr;
    return 2;
  }
  return workflow_call(out, [&] {
    auto j = parse_json(bytes(p, n), 65536, 12, 5000);
    keys(j, {"schema", "value"});
    workflow_validate(j);
    require(j.at("schema") == "astroattitude.planar-pose/1", "Target schema",
            ErrorCode::UnsupportedVersion);
    workflow_output(out, workflow_value(solve_planar_target(
                             j.at("value").get<PlanarPoseRequest>(),
                             {cancel ? &cancel->value : nullptr})));
  });
}
int32_t aa_extract_v1(const float *pixels, uint64_t count, const uint8_t *mask,
                      uint64_t masks, const uint8_t *metadata, uint64_t length,
                      const aa_cancel *cancel, aa_message **out) {
  if ((!pixels && count) || (!mask && masks) || (!metadata && length)) {
    if (out)
      *out = nullptr;
    return 2;
  }
  return workflow_call(out, [&] {
    auto j = parse_json(bytes(metadata, length), 16384, 8, 1024);
    keys(j, {"schema", "width", "height", "row_stride", "layout",
             "black_levels", "white_levels", "options"});
    workflow_validate(j);
    require(j.at("black_levels").size() == 4 &&
                j.at("white_levels").size() == 4,
            "Four parity levels required");
    require(j.at("schema") == "astroattitude.extract/1",
            "Unsupported extraction schema", ErrorCode::UnsupportedVersion);
    require(count <= 24000000 && masks <= 24000000, "Input buffer limit",
            ErrorCode::ResourceLimitExceeded);
    LinearImage im{u32(j.at("width")),
                   u32(j.at("height")),
                   u32(j.at("row_stride")),
                   {pixels, size_t(count)},
                   j.at("layout").get<SampleLayout>(),
                   j.at("black_levels").get<std::array<double, 4>>(),
                   j.at("white_levels").get<std::array<double, 4>>(),
                   {mask, size_t(masks)}};
    auto r = workflow_value(StarExtractor::extract(
        im, j.at("options").get<ExtractionOptions>(),
        Cancellation{cancel ? &cancel->value : nullptr}));
    workflow_output(out, J(r));
  });
}
int32_t aa_calibration_create_v1(const uint8_t *c, uint64_t cn,
                                 const uint8_t *a, uint64_t an,
                                 const uint8_t *p, uint64_t pn,
                                 aa_calibration_session **session,
                                 aa_message **out) {
  if (session)
    *session = nullptr;
  if (!session || (!c && cn) || (!a && an) || (!p && pn)) {
    if (out)
      *out = nullptr;
    return 2;
  }
  return workflow_call(out, [&] {
    auto catalog = workflow_value(Catalog::load(bytes(c, cn)));
    auto astrometry = workflow_value(AstrometryData::load(bytes(a, an)));
    auto j = parse_json(bytes(p, pn), 16384, 12, 2048);
    workflow_validate(j);
    auto s = workflow_value([&]() -> Outcome<CalibrationSession> {
      if (j.at("schema") == "astroattitude.calibration-prepare/1") {
        keys(j, {"schema", "preparation"});
        return CalibrationSession::prepare(
            catalog, astrometry,
            j.at("preparation").get<CalibrationPreparation>());
      }
      keys(j, {"schema", "configuration"});
      require(j.at("schema") == "astroattitude.calibration-create/1",
              "Unsupported workflow schema", ErrorCode::UnsupportedVersion);
      return CalibrationSession::create(
          catalog, astrometry,
          j.at("configuration").get<CalibrationConfiguration>());
    }());
    *session = new aa_calibration_session{std::move(s)};
  });
}
int32_t aa_calibration_restore_v1(const uint8_t *c, uint64_t cn,
                                  const uint8_t *a, uint64_t an,
                                  const uint8_t *p, uint64_t pn,
                                  const aa_cancel *cancel,
                                  aa_calibration_session **session,
                                  aa_message **out) {
  if (session)
    *session = nullptr;
  if (!session || (!c && cn) || (!a && an) || (!p && pn)) {
    if (out)
      *out = nullptr;
    return 2;
  }
  return workflow_call(out, [&] {
    require(pn <= 134217728, "Journal size", ErrorCode::ResourceLimitExceeded);
    auto catalog = workflow_value(Catalog::load(bytes(c, cn)));
    auto astrometry = workflow_value(AstrometryData::load(bytes(a, an)));
    auto s = workflow_value(CalibrationSession::restore(
        catalog, astrometry, {reinterpret_cast<const char *>(p), size_t(pn)},
        Cancellation{cancel ? &cancel->value : nullptr}));
    *session = new aa_calibration_session{std::move(s)};
  });
}
int32_t aa_calibration_call_v1(aa_calibration_session *session,
                               const uint8_t *p, uint64_t n,
                               const aa_calibration_artifact *candidate,
                               const aa_cancel *cancel,
                               aa_calibration_artifact **artifact,
                               aa_message **out) {
  if (artifact)
    *artifact = nullptr;
  if (!session || !artifact || (!p && n)) {
    if (out)
      *out = nullptr;
    return 2;
  }
  return workflow_call(out, [&] {
    auto j = parse_json(bytes(p, n), 4194304, 16, 100000);
    keys(j, {"schema", "operation", "value"});
    workflow_validate(j);
    require(j.at("schema") == "astroattitude.calibration-call/1",
            "Unsupported workflow schema", ErrorCode::UnsupportedVersion);
    auto op = string(j.at("operation"));
    auto &value = j.at("value");
    auto &s = session->session;
    Cancellation token{cancel ? &cancel->value : nullptr};
    J result;
    std::unique_ptr<aa_calibration_artifact> created;
    if (op == "add_frame")
      result =
          workflow_value(s.add_frame(value.get<CalibrationFrame>(), token));
    else if (op == "finish_block")
      result = workflow_value(s.finish_block(value.get<BlockCompletion>()));
    else if (op == "progress")
      result = s.progress();
    else if (op == "model_diagnostics")
      result = s.model_diagnostics();
    else if (op == "next_action")
      result = s.next_action();
    else if (op == "fit") {
      auto c = workflow_value(s.fit(token));
      result = c.data();
      created =
          std::make_unique<aa_calibration_artifact>(aa_calibration_artifact{c});
    } else if (op == "propose_validation")
      result = workflow_value(
          s.propose_validation(value.get<ValidationRequest>(), token));
    else if (op == "freeze") {
      require(candidate && std::holds_alternative<CalibrationCandidate>(
                               candidate->value),
              "Candidate handle required");
      result = workflow_value(
          s.freeze(std::get<CalibrationCandidate>(candidate->value),
                   value.get<ValidationPlan>()));
    } else if (op == "validate") {
      auto r = workflow_value(s.validate(token));
      result = r;
      created =
          std::make_unique<aa_calibration_artifact>(aa_calibration_artifact{r});
    } else if (op == "export_validated")
      result = workflow_value(s.export_validated(token));
    else if (op == "journal")
      result = workflow_value(s.journal());
    else if (op == "pause")
      result = workflow_value(s.pause());
    else if (op == "resume")
      result = workflow_value(s.resume());
    else if (op == "end")
      result = workflow_value(s.end());
    else
      fail(ErrorCode::InvalidInput, "Unknown workflow operation");
    workflow_output(out, result);
    *artifact = created.release();
  });
}
void aa_calibration_destroy_v1(aa_calibration_session *p) { delete p; }
void aa_calibration_artifact_destroy_v1(aa_calibration_artifact *p) {
  delete p;
}
}
