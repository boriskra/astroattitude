#pragma once
#include "astroattitude/calibration.hpp"
#include "astroattitude/extraction.hpp"
#include <charconv>
#include <cmath>
#include <nlohmann/json.hpp>
#include <set>
namespace nlohmann {
template <class T> struct adl_serializer<std::optional<T>> {
  static void to_json(json &j, const std::optional<T> &v) {
    if (v)
      j = *v;
    else
      j = nullptr;
  }
  static void from_json(const json &j, std::optional<T> &v) {
    if (j.is_null())
      v.reset();
    else
      v = j.get<T>();
  }
};
} // namespace nlohmann
namespace astroattitude {
using WorkflowJson = nlohmann::json;
inline void workflow_validate(const WorkflowJson &j) {
  if (j.is_object())
    for (auto it = j.begin(); it != j.end(); ++it) {
      auto &k = it.key();
      auto &v = it.value();
      static const std::set<std::string> u32 = {"width",
                                                "height",
                                                "row_stride",
                                                "nanoseconds",
                                                "maximum_candidates",
                                                "maximum_stars",
                                                "maximum_fit_iterations",
                                                "maximum_frames",
                                                "maximum_blocks",
                                                "remaining_manual_moves",
                                                "minimum_inliers",
                                                "minimum_frames_per_block"};
      static const std::set<std::string> counts = {
          "maximum_pixels",     "maximum_workspace_bytes",
          "maximum_work_units", "maximum_memory_bytes",
          "maximum_hypotheses", "maximum_candidate_comparisons"};
      if (u32.count(k) || counts.count(k)) {
        if (!v.is_number_unsigned() &&
            (!v.is_number_integer() || v.get<int64_t>() < 0))
          throw std::invalid_argument("Unsigned integer required");
        if (u32.count(k) && v.get<uint64_t>() > UINT32_MAX)
          throw std::invalid_argument("uint32 overflow");
      }
      workflow_validate(v);
    }
  else if (j.is_array())
    for (auto &v : j)
      workflow_validate(v);
  else if (j.is_number_float() && !std::isfinite(j.get<double>()))
    throw std::invalid_argument("Nonfinite value");
}
template <class T> inline T wire_integer(const WorkflowJson &j) {
  auto s = j.get<std::string>();
  T v{};
  auto r = std::from_chars(s.data(), s.data() + s.size(), v);
  if (r.ec != std::errc{} || r.ptr != s.data() + s.size() ||
      std::to_string(v) != s)
    throw std::invalid_argument("Noncanonical integer");
  return v;
}
#define AA_ENUM_JSON(T, ...)                                                   \
  inline void to_json(WorkflowJson &j, T v) {                                  \
    static const std::vector<std::string> names = {__VA_ARGS__};               \
    if (size_t(v) >= names.size())                                             \
      throw std::invalid_argument("Invalid enum");                             \
    j = names[size_t(v)];                                                      \
  }                                                                            \
  inline void from_json(const WorkflowJson &j, T &v) {                         \
    static const std::vector<std::string> names = {__VA_ARGS__};               \
    auto s = j.get<std::string>();                                             \
    auto p = std::find(names.begin(), names.end(), s);                         \
    if (p == names.end())                                                      \
      throw std::invalid_argument("Unknown enum");                             \
    v = T(p - names.begin());                                                  \
  }
AA_ENUM_JSON(SampleLayout, "mono", "rggb", "bggr", "grbg", "gbrg")
AA_ENUM_JSON(CaptureRole, "training", "model_selection", "reserve",
             "validation")
AA_ENUM_JSON(CalibrationStage, "mapping", "model_selection", "reserve",
             "frozen", "validation", "complete", "paused", "ended")
AA_ENUM_JSON(EopPolicy, "observed_only", "allow_predicted")
AA_ENUM_JSON(ErrorCode, "InvalidInput", "InvalidResource", "UnsupportedVersion",
             "CalibrationMismatch", "OutsideCalibrationDomain",
             "UnsupportedObservation", "InsufficientObservations",
             "AstrometryDataUnavailable", "NoMatch", "AmbiguousMatch",
             "UnobservableGeometry", "ResidualLimitExceeded",
             "UncertaintyUnavailable", "ConditionalPrecisionNotMet",
             "ResourceLimitExceeded", "Cancelled", "InternalError")
#undef AA_ENUM_JSON
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec2, x, y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Vec3, x, y, z)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Mat2, row_major)
inline void to_json(WorkflowJson &j, const UtcInstant &t) {
  j = {{"unix_seconds", std::to_string(t.unix_seconds)},
       {"nanoseconds", t.nanoseconds}};
}
inline void from_json(const WorkflowJson &j, UtcInstant &t) {
  t = {wire_integer<int64_t>(j.at("unix_seconds")),
       j.at("nanoseconds").get<uint32_t>()};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ExposureTime, midpoint, duration_s,
                                   midpoint_sigma_s)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ObserverPosition, latitude_rad,
                                   longitude_rad, ellipsoid_height_m)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Atmosphere, pressure_pa, temperature_kelvin,
                                   relative_humidity, effective_wavelength_m)
inline void to_json(WorkflowJson &j, const EarthContext &e) {
  WorkflowJson r = {{"kind", "disabled"}};
  if (auto a = std::get_if<Atmosphere>(&e.refraction))
    r = {{"kind", "atmosphere"}, {"parameters", *a}};
  j = {{"observer", e.observer},
       {"refraction", r},
       {"eop_policy", e.eop_policy}};
}
inline void from_json(const WorkflowJson &j, EarthContext &e) {
  e.observer = j.at("observer").get<ObserverPosition>();
  e.eop_policy = j.at("eop_policy").get<EopPolicy>();
  auto r = j.at("refraction");
  if (r.at("kind") == "disabled")
    e.refraction = NoRefraction{};
  else if (r.at("kind") == "atmosphere")
    e.refraction = r.at("parameters").get<Atmosphere>();
  else
    throw std::invalid_argument("Unknown refraction");
}
inline void to_json(WorkflowJson &j, const PixelStar &s) {
  j = {{"id", std::to_string(s.id)},
       {"pixel", s.pixel},
       {"centroid_covariance_px2", s.centroid_covariance_px2},
       {"flux", s.flux}};
}
inline void from_json(const WorkflowJson &j, PixelStar &s) {
  s = {wire_integer<uint64_t>(j.at("id")), j.at("pixel").get<Vec2>(),
       j.at("centroid_covariance_px2").get<std::optional<Mat2>>(),
       j.at("flux").get<std::optional<double>>()};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PinholeBrownModel, fx_px, fy_px, cx_px,
                                   cy_px, k1, k2, k3, p1, p2)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PixelBounds, min_x, min_y, max_x, max_y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CameraCalibration, revision,
                                   camera_configuration_id, width, height,
                                   model, valid_region)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AcceptanceCriteria, minimum_inliers,
                                   maximum_match_error_rad, maximum_fit_rms_rad,
                                   maximum_conditional_sigma_rad)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SolveLimits, maximum_hypotheses,
                                   maximum_candidate_comparisons,
                                   maximum_workspace_bytes)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SolveOptions, acceptance, limits)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ExtractionOptions, detection_sigma,
                                   maximum_axis_ratio, maximum_candidates,
                                   maximum_stars, maximum_fit_iterations,
                                   maximum_pixels, maximum_workspace_bytes,
                                   maximum_work_units)
inline void to_json(WorkflowJson &j, const SourceDiagnostic &s) {
  j = {{"id", std::to_string(s.id)},
       {"pixel", s.pixel},
       {"reason", s.reason},
       {"sigma_major_px", s.sigma_major_px},
       {"sigma_minor_px", s.sigma_minor_px},
       {"amplitude", s.amplitude},
       {"residual_dn", s.residual_dn},
       {"local_noise_dn", s.local_noise_dn}};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ExtractionRegion, bounds, reason)
inline void to_json(WorkflowJson &j, const ExtractionResult &r) {
  j = {{"policy_id", r.policy_id},
       {"stars", r.stars},
       {"sources", r.sources},
       {"regions", r.regions},
       {"work_units", r.work_units},
       {"candidate_capacity_reached", r.candidate_capacity_reached},
       {"physical_accuracy_qualified", false}};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CalibrationPreparation, session_id,
                                   camera_configuration_id, physical_camera_id,
                                   pixel_convention_id, optical_state_id, width,
                                   height, horizontal_fov_rad,
                                   remaining_manual_moves)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CalibrationConfiguration, session_id,
                                   camera_configuration_id, physical_camera_id,
                                   pixel_convention_id, optical_state_id, width,
                                   height, unqualified_seed, association,
                                   maximum_frames, maximum_blocks,
                                   maximum_fit_iterations, maximum_work_units,
                                   maximum_memory_bytes, remaining_manual_moves)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CalibrationFrame, id, pose_id, block_id,
                                   camera_configuration_id, evidence_digest,
                                   quality_provenance_id, timing_provenance_id,
                                   role, exposure, earth, stars)
inline void to_json(WorkflowJson &j, const StarMatch &s) {
  j = {{"detection_id", std::to_string(s.detection_id)},
       {"catalog_star_id", s.catalog_star_id},
       {"angular_residual_rad", s.angular_residual_rad}};
}
inline void from_json(const WorkflowJson &j, StarMatch &s) {
  s = {wire_integer<uint64_t>(j.at("detection_id")),
       j.at("catalog_star_id").get<std::string>(),
       j.at("angular_residual_rad").get<double>()};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(IngestReceipt, frame_id, evidence_digest,
                                   reason, admitted, matched_sources,
                                   association_error, matches, limitations)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BlockCompletion, id, pose_id, role, start,
                                   end, planned_frame_ids, missing_frame_ids,
                                   missing_reasons)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BlockReceipt, block_id, status, planned,
                                   attempted, admitted)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CalibrationProgress, stage, frames, blocks,
                                   admitted_frames, ready_to_fit,
                                   occupied_cells, limitations)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CalibrationAction, kind, reason,
                                   requested_frames)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(OpticalModelDiagnostic, family, status,
                                   model, training_rms_rad,
                                   selection_prediction_rms_rad,
                                   profile_condition, profile_rank,
                                   profile_eigenvalues, weakest_mode_scaled,
                                   parameter_order)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CandidateData, id, session_id,
                                   catalog_content_id, astrometry_content_id,
                                   data_digest, calibration, models,
                                   limitations, physical_accuracy_qualified)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ValidationRequest, id, pose_id,
                                   admission_policy_id, freeze_time,
                                   first_start)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ValidationSlot, block_id, start, end)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ValidationPlan, id, pose_id,
                                   admission_policy_id, freeze_time,
                                   source_panel, slots,
                                   minimum_frames_per_block, minimum_span_s,
                                   maximum_north_change_rad,
                                   maximum_pairwise_change_rad)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ValidationBlockResult, block_id, status,
                                   planned_frames, complete_panel_frames,
                                   mean_north_camera, scatter_rad,
                                   maximum_pair_error_rad)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ValidationReport, candidate_id, plan_id,
                                   status, blocks, span_s, max_north_change_rad,
                                   max_pairwise_change_rad, limitations,
                                   physical_accuracy_qualified)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CalibrationPackage, calibration,
                                   candidate_id, plan_id, catalog_content_id,
                                   astrometry_content_id, qualification,
                                   physical_accuracy_qualified)
} // namespace astroattitude
