#pragma once
#include "astroattitude/alignment.hpp"
#include "workflow_json.hpp"
namespace astroattitude {
inline const std::vector<std::string> alignment_status_names = {
    "estimated",
    "tilt_only",
    "gravity_reference_unavailable",
    "motion_evidence_unavailable",
    "motion_detected",
    "insufficient_frames",
    "optical_configuration_changed",
    "inconsistent_frames",
    "rotation_estimated",
    "pose_diversity_insufficient",
    "ambiguous_alignment",
    "fit_inconsistent"};
inline void to_json(WorkflowJson &j, AlignmentStatus v) {
  j = alignment_status_names.at(size_t(v));
}
inline void from_json(const WorkflowJson &j, AlignmentStatus &v) {
  auto s = j.get<std::string>();
  auto i = std::find(alignment_status_names.begin(),
                     alignment_status_names.end(), s);
  if (i == alignment_status_names.end())
    throw std::invalid_argument("Unknown alignment status");
  v = AlignmentStatus(i - alignment_status_names.begin());
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Mat3, row_major)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Rotation3, matrix)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AlignmentFrame, frame_id, camera_id,
                                   reference_id, optical_provenance, start_s,
                                   end_s, R_reference_from_camera,
                                   R_camera_from_accelerometer_nominal)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AlignmentImu, start_s, end_s,
                                   down_accelerometer, stationary)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AlignmentRequest, reference_id, clock_id,
                                   cameras, frames, imu, down_reference,
                                   minimum_frames, motion_detected)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AlignmentPair, from_sensor, to_sensor,
                                   status, from_frames, to_frames, pose_count,
                                   misalignment_vector_rad,
                                   maximum_gravity_residual_rad, geometry_ratio,
                                   R_to_from, rotation_vector_rad,
                                   gravity_mismatch_rad,
                                   maximum_repeatability_rad, limitations)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(RigAlignmentRequest, poses)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AlignmentReport, pairs,
                                   physical_accuracy_qualified)
inline void to_json(WorkflowJson &j, const TargetCorner &v) {
  j = {{"id", std::to_string(v.id)},
       {"target_m", v.target_m},
       {"pixel", v.pixel}};
}
inline void from_json(const WorkflowJson &j, TargetCorner &v) {
  v = {wire_integer<uint64_t>(j.at("id")), j.at("target_m").get<Vec2>(),
       j.at("pixel").get<Vec2>()};
}
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PlanarPoseRequest, width, height, fx_px,
                                   fy_px, cx_px, cy_px, corners, maximum_rms_px,
                                   ambiguity_tolerance_px)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PlanarPose, R_camera_from_target,
                                   translation_camera_m, rms_px, corners)
} // namespace astroattitude
