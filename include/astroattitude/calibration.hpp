#pragma once
#include "astroattitude.hpp"
namespace astroattitude {
enum class CalibrationStage {
  Mapping,
  ModelSelection,
  Reserve,
  Frozen,
  Validation,
  Complete,
  Paused,
  Ended
};
enum class CaptureRole { Training, ModelSelection, Reserve, Validation };
struct CalibrationPreparation {
  std::string session_id, camera_configuration_id, physical_camera_id;
  std::string pixel_convention_id, optical_state_id;
  uint32_t width = 0, height = 0;
  double horizontal_fov_rad =
      0; // Caller camera metadata, explicitly unqualified.
  uint32_t remaining_manual_moves = 0;
};
struct CalibrationConfiguration {
  std::string session_id, camera_configuration_id, physical_camera_id;
  std::string pixel_convention_id, optical_state_id;
  uint32_t width = 0, height = 0;
  PinholeBrownModel unqualified_seed{};
  SolveOptions association{{6, .002, .001, std::nullopt},
                           {50000, 2000000, 67108864}};
  uint32_t maximum_frames = 2000, maximum_blocks = 256,
           maximum_fit_iterations = 60;
  uint64_t maximum_work_units = 100000000, maximum_memory_bytes = 268435456;
  uint32_t remaining_manual_moves = 0;
};
struct CalibrationFrame {
  std::string id, pose_id, block_id, camera_configuration_id;
  std::string evidence_digest, quality_provenance_id, timing_provenance_id;
  CaptureRole role = CaptureRole::Training;
  ExposureTime exposure{};
  EarthContext earth{};
  std::vector<PixelStar> stars;
};
struct IngestReceipt {
  std::string frame_id, evidence_digest, reason;
  bool admitted = false;
  uint32_t matched_sources = 0;
  std::optional<ErrorCode> association_error;
  std::vector<StarMatch> matches;
  std::vector<std::string> limitations;
};
struct BlockCompletion {
  std::string id, pose_id;
  CaptureRole role = CaptureRole::Training;
  UtcInstant start{}, end{};
  std::vector<std::string> planned_frame_ids;
  // Missing IDs must be listed explicitly, with one nonempty reason per ID.
  std::vector<std::string> missing_frame_ids, missing_reasons;
};
struct BlockReceipt {
  std::string block_id, status;
  uint32_t planned = 0, attempted = 0, admitted = 0;
};
struct CalibrationProgress {
  CalibrationStage stage;
  uint32_t frames = 0, blocks = 0, admitted_frames = 0;
  bool ready_to_fit = false;
  std::vector<uint32_t> occupied_cells;
  std::vector<std::string> limitations;
};
struct CalibrationAction {
  std::string kind, reason;
  uint32_t requested_frames = 0;
};
struct OpticalModelDiagnostic {
  std::string family, status;
  PinholeBrownModel model{};
  std::optional<double> training_rms_rad, selection_prediction_rms_rad,
      profile_condition;
  uint32_t profile_rank = 0;
  std::vector<double> profile_eigenvalues;
  std::vector<double> weakest_mode_scaled;
  std::vector<std::string> parameter_order;
};
struct CandidateData {
  std::string id, session_id, catalog_content_id, astrometry_content_id,
      data_digest;
  CameraCalibration calibration;
  std::vector<OpticalModelDiagnostic> models;
  std::vector<std::string> limitations;
  bool physical_accuracy_qualified = false;
};
class CalibrationCandidate {
  std::shared_ptr<const CandidateData> data_;
  explicit CalibrationCandidate(CandidateData d)
      : data_(std::make_shared<const CandidateData>(std::move(d))) {}
  friend class CalibrationSession;

public:
  const CandidateData &data() const { return *data_; }
};
struct ValidationSlot {
  std::string block_id;
  UtcInstant start{}, end{};
};
struct ValidationPlan {
  std::string id, pose_id, admission_policy_id;
  UtcInstant freeze_time{};
  std::vector<std::string> source_panel;
  std::vector<ValidationSlot> slots;
  uint32_t minimum_frames_per_block = 6;
  double minimum_span_s = 4200;
  double maximum_north_change_rad = 0.0002908882086657216;
  double maximum_pairwise_change_rad = 0.0002908882086657216;
};
struct ValidationRequest {
  std::string id, pose_id, admission_policy_id;
  UtcInstant freeze_time{}, first_start{};
};
struct ValidationBlockResult {
  std::string block_id, status;
  uint32_t planned_frames = 0, complete_panel_frames = 0;
  std::optional<Vec3> mean_north_camera;
  std::optional<double> scatter_rad, maximum_pair_error_rad;
};
struct ValidationReport {
  std::string candidate_id, plan_id, status;
  std::vector<ValidationBlockResult> blocks;
  std::optional<double> span_s, max_north_change_rad, max_pairwise_change_rad;
  std::vector<std::string> limitations;
  bool physical_accuracy_qualified = false;
};
struct CalibrationPackage {
  CameraCalibration calibration;
  std::string candidate_id, plan_id, catalog_content_id, astrometry_content_id;
  std::string qualification = "empirical_consistency_only";
  bool physical_accuracy_qualified = false;
};
// Serialized access is required for mutable sessions. Immutable values may be
// shared.
class CalibrationSession {
  struct Impl;
  std::shared_ptr<Impl> impl_;
  explicit CalibrationSession(std::shared_ptr<Impl> p) : impl_(std::move(p)) {}

public:
  static Outcome<CalibrationSession> prepare(Catalog, AstrometryData,
                                             const CalibrationPreparation &);
  static Outcome<CalibrationSession> create(Catalog, AstrometryData,
                                            const CalibrationConfiguration &);
  Outcome<IngestReceipt> add_frame(const CalibrationFrame &,
                                   Cancellation = {nullptr});
  Outcome<BlockReceipt> finish_block(const BlockCompletion &);
  CalibrationProgress progress() const;
  CalibrationAction next_action() const;
  Outcome<CalibrationCandidate> fit(Cancellation = {nullptr});
  std::vector<OpticalModelDiagnostic> model_diagnostics() const;
  Outcome<ValidationPlan> propose_validation(const ValidationRequest &,
                                             Cancellation = {nullptr}) const;
  Outcome<std::string> freeze(const CalibrationCandidate &,
                              const ValidationPlan &);
  Outcome<ValidationReport> validate(Cancellation = {nullptr});
  Outcome<CalibrationPackage> export_validated(Cancellation = {nullptr});
  Outcome<CalibrationStage> pause();
  Outcome<CalibrationStage> resume();
  Outcome<CalibrationStage> end();
  // Journals preserve inputs and actions; restore rechecks resources and
  // recomputes outcomes.
  Outcome<std::string> journal() const;
  static Outcome<CalibrationSession> restore(Catalog, AstrometryData,
                                             std::string_view,
                                             Cancellation = {nullptr});
};
} // namespace astroattitude
