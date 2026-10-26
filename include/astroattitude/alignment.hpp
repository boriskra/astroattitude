#pragma once
#include "astroattitude.hpp"
namespace astroattitude {
// Experimental fixed-pose diagnostics. Seconds are in one caller-declared
// monotonic clock; rotations map column vectors from the named source frame.
struct AlignmentFrame {
  std::string frame_id, camera_id, reference_id, optical_provenance;
  double start_s, end_s;
  Rotation3 R_reference_from_camera, R_camera_from_accelerometer_nominal;
};
struct AlignmentImu {
  double start_s, end_s;
  Vec3 down_accelerometer;
  bool stationary;
};
struct AlignmentRequest {
  std::string reference_id, clock_id;
  std::vector<std::string> cameras;
  std::vector<AlignmentFrame> frames;
  std::optional<AlignmentImu> imu;
  std::optional<Vec3> down_reference;
  uint32_t minimum_frames = 6;
  bool motion_detected = false;
};
enum class AlignmentStatus {
  estimated,
  tilt_only,
  gravity_reference_unavailable,
  motion_evidence_unavailable,
  motion_detected,
  insufficient_frames,
  optical_configuration_changed,
  inconsistent_frames,
  rotation_estimated,
  pose_diversity_insufficient,
  ambiguous_alignment,
  fit_inconsistent
};
struct AlignmentPair {
  std::string from_sensor, to_sensor;
  AlignmentStatus status = AlignmentStatus::insufficient_frames;
  uint32_t from_frames = 0, to_frames = 0, pose_count = 0;
  std::optional<Vec3> misalignment_vector_rad;
  std::optional<double> maximum_gravity_residual_rad, geometry_ratio;
  std::optional<Rotation3> R_to_from;
  std::optional<Vec3> rotation_vector_rad;
  std::optional<double> gravity_mismatch_rad, maximum_repeatability_rad;
  std::vector<std::string> limitations;
};
struct AlignmentReport {
  std::vector<AlignmentPair> pairs;
  bool physical_accuracy_qualified = false;
};
Outcome<AlignmentReport> measure_alignment(const AlignmentRequest &,
                                           Cancellation = {nullptr});
// Multiple stationary poses; motion is allowed only between pose intervals.
struct RigAlignmentRequest {
  std::vector<AlignmentRequest> poses;
};
Outcome<AlignmentReport> calibrate_rig_alignment(const RigAlignmentRequest &,
                                                 Cancellation = {nullptr});
struct TargetCorner {
  uint64_t id;
  Vec2 target_m, pixel;
};
struct PlanarPoseRequest {
  // Intrinsics describe these exact pixels. Distortion must already be removed
  // or explicitly accepted as an optical approximation by the caller.
  uint32_t width, height;
  double fx_px, fy_px, cx_px, cy_px;
  std::vector<TargetCorner> corners;
  double maximum_rms_px = 2, ambiguity_tolerance_px = .25;
};
struct PlanarPose {
  Rotation3 R_camera_from_target;
  Vec3 translation_camera_m;
  double rms_px;
  uint32_t corners;
};
Outcome<PlanarPose> solve_planar_target(const PlanarPoseRequest &,
                                        Cancellation = {nullptr});
} // namespace astroattitude
