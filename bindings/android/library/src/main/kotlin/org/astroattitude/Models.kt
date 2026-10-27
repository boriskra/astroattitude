package org.astroattitude

import kotlinx.serialization.Serializable

@Serializable
enum class AlignmentStatus { estimated, tilt_only, gravity_reference_unavailable, motion_evidence_unavailable, motion_detected, insufficient_frames, optical_configuration_changed, inconsistent_frames, rotation_estimated, pose_diversity_insufficient, ambiguous_alignment, fit_inconsistent }

@Serializable
enum class ErrorCode { InvalidInput, InvalidResource, UnsupportedVersion, CalibrationMismatch, OutsideCalibrationDomain, UnsupportedObservation, InsufficientObservations, AstrometryDataUnavailable, NoMatch, AmbiguousMatch, UnobservableGeometry, ResidualLimitExceeded, UncertaintyUnavailable, ConditionalPrecisionNotMet, ResourceLimitExceeded, Cancelled, InternalError }

@Serializable
enum class TemporalModel { stationary_single_exposure, aligned_effective_exposure }

@Serializable
enum class EopPolicy { observed_only, allow_predicted }

@Serializable
enum class RefractionKind { disabled, atmosphere }

@Serializable
enum class SampleLayout { mono, rggb, bggr, grbg, gbrg }

@Serializable
enum class CaptureRole { training, model_selection, reserve, validation }

@Serializable
enum class CalibrationStage { mapping, model_selection, reserve, frozen, validation, complete, paused, ended }

@Serializable
data class Vec2(
    val x: Double,
    val y: Double
)

@Serializable
data class Vec3(
    val x: Double,
    val y: Double,
    val z: Double
)

@Serializable
data class Mat2(
    val row_major: List<Double>
)

@Serializable
data class Mat3(
    val row_major: List<Double>
)

@Serializable
data class Rotation3(
    val matrix: Mat3
)

@Serializable
data class UtcInstant(
    val unix_seconds: WireInt64,
    val nanoseconds: UInt
)

@Serializable
data class ExposureTime(
    val midpoint: UtcInstant,
    val duration_s: Double,
    val midpoint_sigma_s: Double?
)

@Serializable
data class ObserverPosition(
    val latitude_rad: Double,
    val longitude_rad: Double,
    val ellipsoid_height_m: Double
)

@Serializable
data class ObservationIdentity(
    val observation_id: String,
    val camera_configuration_id: String,
    val calibration_revision: String,
    val exposure: ExposureTime,
    val temporal_model: TemporalModel,
    val timing_provenance_id: String
)

@Serializable
data class PixelStar(
    val id: WireUInt64,
    val pixel: Vec2,
    val centroid_covariance_px2: Mat2?,
    val flux: Double?
)

@Serializable
data class RayStar(
    val id: WireUInt64,
    val direction_camera: Vec3,
    val direction_covariance_rad2: Mat3?
)

@Serializable
data class PixelObservation(
    val identity: ObservationIdentity,
    val width: UInt,
    val height: UInt,
    val stars: List<PixelStar>
)

@Serializable
data class RayObservation(
    val identity: ObservationIdentity,
    val stars: List<RayStar>
)

@Serializable
data class PinholeBrownModel(
    val fx_px: Double,
    val fy_px: Double,
    val cx_px: Double,
    val cy_px: Double,
    val k1: Double,
    val k2: Double,
    val k3: Double,
    val p1: Double,
    val p2: Double
)

@Serializable
data class PixelBounds(
    val min_x: Double,
    val min_y: Double,
    val max_x: Double,
    val max_y: Double
)

@Serializable
data class CameraCalibration(
    val revision: String,
    val camera_configuration_id: String,
    val width: UInt,
    val height: UInt,
    val model: PinholeBrownModel,
    val valid_region: PixelBounds
)

@Serializable
data class Atmosphere(
    val pressure_pa: Double,
    val temperature_kelvin: Double,
    val relative_humidity: Double,
    val effective_wavelength_m: Double
)

@Serializable
data class Refraction(
    val kind: RefractionKind,
    val parameters: Atmosphere? = null
)

@Serializable
data class EarthContext(
    val observer: ObserverPosition,
    val refraction: Refraction,
    val eop_policy: EopPolicy
)

@Serializable
data class AcceptanceCriteria(
    val minimum_inliers: UInt,
    val maximum_match_error_rad: Double,
    val maximum_fit_rms_rad: Double,
    val maximum_conditional_sigma_rad: Double?
)

@Serializable
data class SolveLimits(
    val maximum_hypotheses: UInt,
    val maximum_candidate_comparisons: ULong,
    val maximum_workspace_bytes: ULong
)

@Serializable
data class SolveOptions(
    val acceptance: AcceptanceCriteria,
    val limits: SolveLimits
)

@Serializable
data class StarMatch(
    val detection_id: WireUInt64,
    val catalog_star_id: String,
    val angular_residual_rad: Double
)

@Serializable
data class ConditionalUncertainty(
    val covariance_enu_rad2: Mat3
)

@Serializable
data class SearchReport(
    val algorithm_version: String,
    val hypotheses_evaluated: UInt,
    val candidate_comparisons: ULong
)

@Serializable
data class ModelReport(
    val model_id: String,
    val earth_orientation_status: String,
    val refraction_status: String,
    val limitations: List<String>
)

@Serializable
data class AttitudeSolution(
    val identity: ObservationIdentity,
    val R_enu_from_camera: Rotation3,
    val observer_used: ObserverPosition,
    val conditional_uncertainty: ConditionalUncertainty?,
    val fit_rms_rad: Double,
    val fit_max_residual_rad: Double,
    val matches: List<StarMatch>,
    val rejected_detections: List<WireUInt64>,
    val catalog_content_id: String,
    val astrometry_content_id: String,
    val implementation_version: String,
    val search: SearchReport,
    val models: ModelReport
)

@Serializable
data class SolverError(
    val code: ErrorCode,
    val detail: String,
    val affected_detections: List<WireUInt64>
)

@Serializable
data class CalibrationPreparation(
    val session_id: String,
    val camera_configuration_id: String,
    val physical_camera_id: String,
    val pixel_convention_id: String,
    val optical_state_id: String,
    val width: UInt,
    val height: UInt,
    val horizontal_fov_rad: Double,
    val remaining_manual_moves: UInt
)

@Serializable
data class ExtractionOptions(
    val detection_sigma: Double,
    val maximum_axis_ratio: Double,
    val maximum_candidates: UInt,
    val maximum_stars: UInt,
    val maximum_fit_iterations: UInt,
    val maximum_pixels: ULong,
    val maximum_workspace_bytes: ULong,
    val maximum_work_units: ULong
)

@Serializable
data class ExtractionMetadata(
    val width: UInt,
    val height: UInt,
    val row_stride: UInt,
    val layout: SampleLayout,
    val black_levels: List<Double>,
    val white_levels: List<Double>,
    val options: ExtractionOptions
)

@Serializable
data class SourceDiagnostic(
    val id: WireUInt64,
    val pixel: Vec2,
    val reason: String,
    val sigma_major_px: Double,
    val sigma_minor_px: Double,
    val amplitude: Double,
    val residual_dn: Double,
    val local_noise_dn: Double
)

@Serializable
data class ExtractionRegion(
    val bounds: PixelBounds,
    val reason: String
)

@Serializable
data class ExtractionResult(
    val policy_id: String,
    val stars: List<PixelStar>,
    val sources: List<SourceDiagnostic>,
    val regions: List<ExtractionRegion>,
    val work_units: ULong,
    val candidate_capacity_reached: Boolean,
    val physical_accuracy_qualified: Boolean
)

@Serializable
data class CalibrationConfiguration(
    val session_id: String,
    val camera_configuration_id: String,
    val physical_camera_id: String,
    val pixel_convention_id: String,
    val optical_state_id: String,
    val width: UInt,
    val height: UInt,
    val unqualified_seed: PinholeBrownModel,
    val association: SolveOptions,
    val maximum_frames: UInt,
    val maximum_blocks: UInt,
    val maximum_fit_iterations: UInt,
    val maximum_work_units: ULong,
    val maximum_memory_bytes: ULong,
    val remaining_manual_moves: UInt
)

@Serializable
data class CalibrationFrame(
    val id: String,
    val pose_id: String,
    val block_id: String,
    val camera_configuration_id: String,
    val evidence_digest: String,
    val quality_provenance_id: String,
    val timing_provenance_id: String,
    val role: CaptureRole,
    val exposure: ExposureTime,
    val earth: EarthContext,
    val stars: List<PixelStar>
)

@Serializable
data class IngestReceipt(
    val frame_id: String,
    val evidence_digest: String,
    val reason: String,
    val admitted: Boolean,
    val matched_sources: UInt,
    val association_error: ErrorCode?,
    val matches: List<StarMatch>,
    val limitations: List<String>
)

@Serializable
data class BlockCompletion(
    val id: String,
    val pose_id: String,
    val role: CaptureRole,
    val start: UtcInstant,
    val end: UtcInstant,
    val planned_frame_ids: List<String>,
    val missing_frame_ids: List<String>,
    val missing_reasons: List<String>
)

@Serializable
data class BlockReceipt(
    val block_id: String,
    val status: String,
    val planned: UInt,
    val attempted: UInt,
    val admitted: UInt
)

@Serializable
data class CalibrationProgress(
    val stage: CalibrationStage,
    val frames: UInt,
    val blocks: UInt,
    val admitted_frames: UInt,
    val ready_to_fit: Boolean,
    val occupied_cells: List<UInt>,
    val limitations: List<String>
)

@Serializable
data class CalibrationAction(
    val kind: String,
    val reason: String,
    val requested_frames: UInt
)

@Serializable
data class OpticalModelDiagnostic(
    val family: String,
    val status: String,
    val model: PinholeBrownModel,
    val training_rms_rad: Double?,
    val selection_prediction_rms_rad: Double?,
    val profile_condition: Double?,
    val profile_rank: UInt,
    val profile_eigenvalues: List<Double>,
    val weakest_mode_scaled: List<Double>,
    val parameter_order: List<String>
)

@Serializable
data class CandidateData(
    val id: String,
    val session_id: String,
    val catalog_content_id: String,
    val astrometry_content_id: String,
    val data_digest: String,
    val calibration: CameraCalibration,
    val models: List<OpticalModelDiagnostic>,
    val limitations: List<String>,
    val physical_accuracy_qualified: Boolean
)

@Serializable
data class ValidationRequest(
    val id: String,
    val pose_id: String,
    val admission_policy_id: String,
    val freeze_time: UtcInstant,
    val first_start: UtcInstant
)

@Serializable
data class ValidationSlot(
    val block_id: String,
    val start: UtcInstant,
    val end: UtcInstant
)

@Serializable
data class ValidationPlan(
    val id: String,
    val pose_id: String,
    val admission_policy_id: String,
    val freeze_time: UtcInstant,
    val source_panel: List<String>,
    val slots: List<ValidationSlot>,
    val minimum_frames_per_block: UInt,
    val minimum_span_s: Double,
    val maximum_north_change_rad: Double,
    val maximum_pairwise_change_rad: Double
)

@Serializable
data class ValidationBlockResult(
    val block_id: String,
    val status: String,
    val planned_frames: UInt,
    val complete_panel_frames: UInt,
    val mean_north_camera: Vec3?,
    val scatter_rad: Double?,
    val maximum_pair_error_rad: Double?
)

@Serializable
data class ValidationReport(
    val candidate_id: String,
    val plan_id: String,
    val status: String,
    val blocks: List<ValidationBlockResult>,
    val span_s: Double?,
    val max_north_change_rad: Double?,
    val max_pairwise_change_rad: Double?,
    val limitations: List<String>,
    val physical_accuracy_qualified: Boolean
)

@Serializable
data class CalibrationPackage(
    val calibration: CameraCalibration,
    val candidate_id: String,
    val plan_id: String,
    val catalog_content_id: String,
    val astrometry_content_id: String,
    val qualification: String,
    val physical_accuracy_qualified: Boolean
)

@Serializable
data class AlignmentFrame(
    val frame_id: String,
    val camera_id: String,
    val reference_id: String,
    val optical_provenance: String,
    val start_s: Double,
    val end_s: Double,
    val R_reference_from_camera: Rotation3,
    val R_camera_from_accelerometer_nominal: Rotation3
)

@Serializable
data class AlignmentImu(
    val start_s: Double,
    val end_s: Double,
    val down_accelerometer: Vec3,
    val stationary: Boolean
)

@Serializable
data class AlignmentRequest(
    val reference_id: String,
    val clock_id: String,
    val cameras: List<String>,
    val frames: List<AlignmentFrame>,
    val imu: AlignmentImu?,
    val down_reference: Vec3?,
    val minimum_frames: UInt,
    val motion_detected: Boolean
)

@Serializable
data class AlignmentPair(
    val from_sensor: String,
    val to_sensor: String,
    val status: AlignmentStatus,
    val from_frames: UInt,
    val to_frames: UInt,
    val pose_count: UInt,
    val misalignment_vector_rad: Vec3?,
    val maximum_gravity_residual_rad: Double?,
    val geometry_ratio: Double?,
    val R_to_from: Rotation3?,
    val rotation_vector_rad: Vec3?,
    val gravity_mismatch_rad: Double?,
    val maximum_repeatability_rad: Double?,
    val limitations: List<String>
)

@Serializable
data class RigAlignmentRequest(
    val poses: List<AlignmentRequest>
)

@Serializable
data class AlignmentReport(
    val pairs: List<AlignmentPair>,
    val physical_accuracy_qualified: Boolean
)

@Serializable
data class TargetCorner(
    val id: WireUInt64,
    val target_m: Vec2,
    val pixel: Vec2
)

@Serializable
data class PlanarPoseRequest(
    val width: UInt,
    val height: UInt,
    val fx_px: Double,
    val fy_px: Double,
    val cx_px: Double,
    val cy_px: Double,
    val corners: List<TargetCorner>,
    val maximum_rms_px: Double,
    val ambiguity_tolerance_px: Double
)

@Serializable
data class PlanarPose(
    val R_camera_from_target: Rotation3,
    val translation_camera_m: Vec3,
    val rms_px: Double,
    val corners: UInt
)
