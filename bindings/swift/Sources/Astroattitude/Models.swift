import Foundation

public enum AlignmentStatus: String, Codable, Sendable {
    case estimated
    case tilt_only
    case gravity_reference_unavailable
    case motion_evidence_unavailable
    case motion_detected
    case insufficient_frames
    case optical_configuration_changed
    case inconsistent_frames
    case rotation_estimated
    case pose_diversity_insufficient
    case ambiguous_alignment
    case fit_inconsistent
}

public enum ErrorCode: String, Codable, Sendable {
    case InvalidInput
    case InvalidResource
    case UnsupportedVersion
    case CalibrationMismatch
    case OutsideCalibrationDomain
    case UnsupportedObservation
    case InsufficientObservations
    case AstrometryDataUnavailable
    case NoMatch
    case AmbiguousMatch
    case UnobservableGeometry
    case ResidualLimitExceeded
    case UncertaintyUnavailable
    case ConditionalPrecisionNotMet
    case ResourceLimitExceeded
    case Cancelled
    case InternalError
}

public enum TemporalModel: String, Codable, Sendable {
    case stationary_single_exposure
    case aligned_effective_exposure
}

public enum EopPolicy: String, Codable, Sendable {
    case observed_only
    case allow_predicted
}

public enum RefractionKind: String, Codable, Sendable {
    case disabled
    case atmosphere
}

public enum SampleLayout: String, Codable, Sendable {
    case mono
    case rggb
    case bggr
    case grbg
    case gbrg
}

public enum CaptureRole: String, Codable, Sendable {
    case training
    case model_selection
    case reserve
    case validation
}

public enum CalibrationStage: String, Codable, Sendable {
    case mapping
    case model_selection
    case reserve
    case frozen
    case validation
    case complete
    case paused
    case ended
}

public struct Vec2: Codable, Sendable {
    public var x: Double
    public var y: Double
    public init(x: Double, y: Double) {
        self.x = x
        self.y = y
    }
}

public struct Vec3: Codable, Sendable {
    public var x: Double
    public var y: Double
    public var z: Double
    public init(x: Double, y: Double, z: Double) {
        self.x = x
        self.y = y
        self.z = z
    }
}

public struct Mat2: Codable, Sendable {
    public var row_major: [Double]
    public init(row_major: [Double]) {
        self.row_major = row_major
    }
}

public struct Mat3: Codable, Sendable {
    public var row_major: [Double]
    public init(row_major: [Double]) {
        self.row_major = row_major
    }
}

public struct Rotation3: Codable, Sendable {
    public var matrix: Mat3
    public init(matrix: Mat3) {
        self.matrix = matrix
    }
}

public struct UtcInstant: Codable, Sendable {
    public var unix_seconds: WireInt64
    public var nanoseconds: UInt32
    public init(unix_seconds: WireInt64, nanoseconds: UInt32) {
        self.unix_seconds = unix_seconds
        self.nanoseconds = nanoseconds
    }
}

public struct ExposureTime: Codable, Sendable {
    public var midpoint: UtcInstant
    public var duration_s: Double
    public var midpoint_sigma_s: Double?
    public init(midpoint: UtcInstant, duration_s: Double, midpoint_sigma_s: Double?) {
        self.midpoint = midpoint
        self.duration_s = duration_s
        self.midpoint_sigma_s = midpoint_sigma_s
    }
}

public struct ObserverPosition: Codable, Sendable {
    public var latitude_rad: Double
    public var longitude_rad: Double
    public var ellipsoid_height_m: Double
    public init(latitude_rad: Double, longitude_rad: Double, ellipsoid_height_m: Double) {
        self.latitude_rad = latitude_rad
        self.longitude_rad = longitude_rad
        self.ellipsoid_height_m = ellipsoid_height_m
    }
}

public struct ObservationIdentity: Codable, Sendable {
    public var observation_id: String
    public var camera_configuration_id: String
    public var calibration_revision: String
    public var exposure: ExposureTime
    public var temporal_model: TemporalModel
    public var timing_provenance_id: String
    public init(observation_id: String, camera_configuration_id: String, calibration_revision: String, exposure: ExposureTime, temporal_model: TemporalModel, timing_provenance_id: String) {
        self.observation_id = observation_id
        self.camera_configuration_id = camera_configuration_id
        self.calibration_revision = calibration_revision
        self.exposure = exposure
        self.temporal_model = temporal_model
        self.timing_provenance_id = timing_provenance_id
    }
}

public struct PixelStar: Codable, Sendable {
    public var id: WireUInt64
    public var pixel: Vec2
    public var centroid_covariance_px2: Mat2?
    public var flux: Double?
    public init(id: WireUInt64, pixel: Vec2, centroid_covariance_px2: Mat2?, flux: Double?) {
        self.id = id
        self.pixel = pixel
        self.centroid_covariance_px2 = centroid_covariance_px2
        self.flux = flux
    }
}

public struct RayStar: Codable, Sendable {
    public var id: WireUInt64
    public var direction_camera: Vec3
    public var direction_covariance_rad2: Mat3?
    public init(id: WireUInt64, direction_camera: Vec3, direction_covariance_rad2: Mat3?) {
        self.id = id
        self.direction_camera = direction_camera
        self.direction_covariance_rad2 = direction_covariance_rad2
    }
}

public struct PixelObservation: Codable, Sendable {
    public var identity: ObservationIdentity
    public var width: UInt32
    public var height: UInt32
    public var stars: [PixelStar]
    public init(identity: ObservationIdentity, width: UInt32, height: UInt32, stars: [PixelStar]) {
        self.identity = identity
        self.width = width
        self.height = height
        self.stars = stars
    }
}

public struct RayObservation: Codable, Sendable {
    public var identity: ObservationIdentity
    public var stars: [RayStar]
    public init(identity: ObservationIdentity, stars: [RayStar]) {
        self.identity = identity
        self.stars = stars
    }
}

public struct PinholeBrownModel: Codable, Sendable {
    public var fx_px: Double
    public var fy_px: Double
    public var cx_px: Double
    public var cy_px: Double
    public var k1: Double
    public var k2: Double
    public var k3: Double
    public var p1: Double
    public var p2: Double
    public init(fx_px: Double, fy_px: Double, cx_px: Double, cy_px: Double, k1: Double, k2: Double, k3: Double, p1: Double, p2: Double) {
        self.fx_px = fx_px
        self.fy_px = fy_px
        self.cx_px = cx_px
        self.cy_px = cy_px
        self.k1 = k1
        self.k2 = k2
        self.k3 = k3
        self.p1 = p1
        self.p2 = p2
    }
}

public struct PixelBounds: Codable, Sendable {
    public var min_x: Double
    public var min_y: Double
    public var max_x: Double
    public var max_y: Double
    public init(min_x: Double, min_y: Double, max_x: Double, max_y: Double) {
        self.min_x = min_x
        self.min_y = min_y
        self.max_x = max_x
        self.max_y = max_y
    }
}

public struct CameraCalibration: Codable, Sendable {
    public var revision: String
    public var camera_configuration_id: String
    public var width: UInt32
    public var height: UInt32
    public var model: PinholeBrownModel
    public var valid_region: PixelBounds
    public init(revision: String, camera_configuration_id: String, width: UInt32, height: UInt32, model: PinholeBrownModel, valid_region: PixelBounds) {
        self.revision = revision
        self.camera_configuration_id = camera_configuration_id
        self.width = width
        self.height = height
        self.model = model
        self.valid_region = valid_region
    }
}

public struct Atmosphere: Codable, Sendable {
    public var pressure_pa: Double
    public var temperature_kelvin: Double
    public var relative_humidity: Double
    public var effective_wavelength_m: Double
    public init(pressure_pa: Double, temperature_kelvin: Double, relative_humidity: Double, effective_wavelength_m: Double) {
        self.pressure_pa = pressure_pa
        self.temperature_kelvin = temperature_kelvin
        self.relative_humidity = relative_humidity
        self.effective_wavelength_m = effective_wavelength_m
    }
}

public struct Refraction: Codable, Sendable {
    public var kind: RefractionKind
    public var parameters: Atmosphere?
    public init(kind: RefractionKind, parameters: Atmosphere?) {
        self.kind = kind
        self.parameters = parameters
    }
}

public struct EarthContext: Codable, Sendable {
    public var observer: ObserverPosition
    public var refraction: Refraction
    public var eop_policy: EopPolicy
    public init(observer: ObserverPosition, refraction: Refraction, eop_policy: EopPolicy) {
        self.observer = observer
        self.refraction = refraction
        self.eop_policy = eop_policy
    }
}

public struct AcceptanceCriteria: Codable, Sendable {
    public var minimum_inliers: UInt32
    public var maximum_match_error_rad: Double
    public var maximum_fit_rms_rad: Double
    public var maximum_conditional_sigma_rad: Double?
    public init(minimum_inliers: UInt32, maximum_match_error_rad: Double, maximum_fit_rms_rad: Double, maximum_conditional_sigma_rad: Double?) {
        self.minimum_inliers = minimum_inliers
        self.maximum_match_error_rad = maximum_match_error_rad
        self.maximum_fit_rms_rad = maximum_fit_rms_rad
        self.maximum_conditional_sigma_rad = maximum_conditional_sigma_rad
    }
}

public struct SolveLimits: Codable, Sendable {
    public var maximum_hypotheses: UInt32
    public var maximum_candidate_comparisons: UInt64
    public var maximum_workspace_bytes: UInt64
    public init(maximum_hypotheses: UInt32, maximum_candidate_comparisons: UInt64, maximum_workspace_bytes: UInt64) {
        self.maximum_hypotheses = maximum_hypotheses
        self.maximum_candidate_comparisons = maximum_candidate_comparisons
        self.maximum_workspace_bytes = maximum_workspace_bytes
    }
}

public struct SolveOptions: Codable, Sendable {
    public var acceptance: AcceptanceCriteria
    public var limits: SolveLimits
    public init(acceptance: AcceptanceCriteria, limits: SolveLimits) {
        self.acceptance = acceptance
        self.limits = limits
    }
}

public struct StarMatch: Codable, Sendable {
    public var detection_id: WireUInt64
    public var catalog_star_id: String
    public var angular_residual_rad: Double
    public init(detection_id: WireUInt64, catalog_star_id: String, angular_residual_rad: Double) {
        self.detection_id = detection_id
        self.catalog_star_id = catalog_star_id
        self.angular_residual_rad = angular_residual_rad
    }
}

public struct ConditionalUncertainty: Codable, Sendable {
    public var covariance_enu_rad2: Mat3
    public init(covariance_enu_rad2: Mat3) {
        self.covariance_enu_rad2 = covariance_enu_rad2
    }
}

public struct SearchReport: Codable, Sendable {
    public var algorithm_version: String
    public var hypotheses_evaluated: UInt32
    public var candidate_comparisons: UInt64
    public init(algorithm_version: String, hypotheses_evaluated: UInt32, candidate_comparisons: UInt64) {
        self.algorithm_version = algorithm_version
        self.hypotheses_evaluated = hypotheses_evaluated
        self.candidate_comparisons = candidate_comparisons
    }
}

public struct ModelReport: Codable, Sendable {
    public var model_id: String
    public var earth_orientation_status: String
    public var refraction_status: String
    public var limitations: [String]
    public init(model_id: String, earth_orientation_status: String, refraction_status: String, limitations: [String]) {
        self.model_id = model_id
        self.earth_orientation_status = earth_orientation_status
        self.refraction_status = refraction_status
        self.limitations = limitations
    }
}

public struct AttitudeSolution: Codable, Sendable {
    public var identity: ObservationIdentity
    public var R_enu_from_camera: Rotation3
    public var observer_used: ObserverPosition
    public var conditional_uncertainty: ConditionalUncertainty?
    public var fit_rms_rad: Double
    public var fit_max_residual_rad: Double
    public var matches: [StarMatch]
    public var rejected_detections: [WireUInt64]
    public var catalog_content_id: String
    public var astrometry_content_id: String
    public var implementation_version: String
    public var search: SearchReport
    public var models: ModelReport
    public init(identity: ObservationIdentity, R_enu_from_camera: Rotation3, observer_used: ObserverPosition, conditional_uncertainty: ConditionalUncertainty?, fit_rms_rad: Double, fit_max_residual_rad: Double, matches: [StarMatch], rejected_detections: [WireUInt64], catalog_content_id: String, astrometry_content_id: String, implementation_version: String, search: SearchReport, models: ModelReport) {
        self.identity = identity
        self.R_enu_from_camera = R_enu_from_camera
        self.observer_used = observer_used
        self.conditional_uncertainty = conditional_uncertainty
        self.fit_rms_rad = fit_rms_rad
        self.fit_max_residual_rad = fit_max_residual_rad
        self.matches = matches
        self.rejected_detections = rejected_detections
        self.catalog_content_id = catalog_content_id
        self.astrometry_content_id = astrometry_content_id
        self.implementation_version = implementation_version
        self.search = search
        self.models = models
    }
}

public struct SolverError: Codable, Sendable {
    public var code: ErrorCode
    public var detail: String
    public var affected_detections: [WireUInt64]
    public init(code: ErrorCode, detail: String, affected_detections: [WireUInt64]) {
        self.code = code
        self.detail = detail
        self.affected_detections = affected_detections
    }
}

public struct CalibrationPreparation: Codable, Sendable {
    public var session_id: String
    public var camera_configuration_id: String
    public var physical_camera_id: String
    public var pixel_convention_id: String
    public var optical_state_id: String
    public var width: UInt32
    public var height: UInt32
    public var horizontal_fov_rad: Double
    public var remaining_manual_moves: UInt32
    public init(session_id: String, camera_configuration_id: String, physical_camera_id: String, pixel_convention_id: String, optical_state_id: String, width: UInt32, height: UInt32, horizontal_fov_rad: Double, remaining_manual_moves: UInt32) {
        self.session_id = session_id
        self.camera_configuration_id = camera_configuration_id
        self.physical_camera_id = physical_camera_id
        self.pixel_convention_id = pixel_convention_id
        self.optical_state_id = optical_state_id
        self.width = width
        self.height = height
        self.horizontal_fov_rad = horizontal_fov_rad
        self.remaining_manual_moves = remaining_manual_moves
    }
}

public struct ExtractionOptions: Codable, Sendable {
    public var detection_sigma: Double
    public var maximum_axis_ratio: Double
    public var maximum_candidates: UInt32
    public var maximum_stars: UInt32
    public var maximum_fit_iterations: UInt32
    public var maximum_pixels: UInt64
    public var maximum_workspace_bytes: UInt64
    public var maximum_work_units: UInt64
    public init(detection_sigma: Double, maximum_axis_ratio: Double, maximum_candidates: UInt32, maximum_stars: UInt32, maximum_fit_iterations: UInt32, maximum_pixels: UInt64, maximum_workspace_bytes: UInt64, maximum_work_units: UInt64) {
        self.detection_sigma = detection_sigma
        self.maximum_axis_ratio = maximum_axis_ratio
        self.maximum_candidates = maximum_candidates
        self.maximum_stars = maximum_stars
        self.maximum_fit_iterations = maximum_fit_iterations
        self.maximum_pixels = maximum_pixels
        self.maximum_workspace_bytes = maximum_workspace_bytes
        self.maximum_work_units = maximum_work_units
    }
}

public struct ExtractionMetadata: Codable, Sendable {
    public var width: UInt32
    public var height: UInt32
    public var row_stride: UInt32
    public var layout: SampleLayout
    public var black_levels: [Double]
    public var white_levels: [Double]
    public var options: ExtractionOptions
    public init(width: UInt32, height: UInt32, row_stride: UInt32, layout: SampleLayout, black_levels: [Double], white_levels: [Double], options: ExtractionOptions) {
        self.width = width
        self.height = height
        self.row_stride = row_stride
        self.layout = layout
        self.black_levels = black_levels
        self.white_levels = white_levels
        self.options = options
    }
}

public struct SourceDiagnostic: Codable, Sendable {
    public var id: WireUInt64
    public var pixel: Vec2
    public var reason: String
    public var sigma_major_px: Double
    public var sigma_minor_px: Double
    public var amplitude: Double
    public var residual_dn: Double
    public var local_noise_dn: Double
    public init(id: WireUInt64, pixel: Vec2, reason: String, sigma_major_px: Double, sigma_minor_px: Double, amplitude: Double, residual_dn: Double, local_noise_dn: Double) {
        self.id = id
        self.pixel = pixel
        self.reason = reason
        self.sigma_major_px = sigma_major_px
        self.sigma_minor_px = sigma_minor_px
        self.amplitude = amplitude
        self.residual_dn = residual_dn
        self.local_noise_dn = local_noise_dn
    }
}

public struct ExtractionRegion: Codable, Sendable {
    public var bounds: PixelBounds
    public var reason: String
    public init(bounds: PixelBounds, reason: String) {
        self.bounds = bounds
        self.reason = reason
    }
}

public struct ExtractionResult: Codable, Sendable {
    public var policy_id: String
    public var stars: [PixelStar]
    public var sources: [SourceDiagnostic]
    public var regions: [ExtractionRegion]
    public var work_units: UInt64
    public var candidate_capacity_reached: Bool
    public var physical_accuracy_qualified: Bool
    public init(policy_id: String, stars: [PixelStar], sources: [SourceDiagnostic], regions: [ExtractionRegion], work_units: UInt64, candidate_capacity_reached: Bool, physical_accuracy_qualified: Bool) {
        self.policy_id = policy_id
        self.stars = stars
        self.sources = sources
        self.regions = regions
        self.work_units = work_units
        self.candidate_capacity_reached = candidate_capacity_reached
        self.physical_accuracy_qualified = physical_accuracy_qualified
    }
}

public struct CalibrationConfiguration: Codable, Sendable {
    public var session_id: String
    public var camera_configuration_id: String
    public var physical_camera_id: String
    public var pixel_convention_id: String
    public var optical_state_id: String
    public var width: UInt32
    public var height: UInt32
    public var unqualified_seed: PinholeBrownModel
    public var association: SolveOptions
    public var maximum_frames: UInt32
    public var maximum_blocks: UInt32
    public var maximum_fit_iterations: UInt32
    public var maximum_work_units: UInt64
    public var maximum_memory_bytes: UInt64
    public var remaining_manual_moves: UInt32
    public init(session_id: String, camera_configuration_id: String, physical_camera_id: String, pixel_convention_id: String, optical_state_id: String, width: UInt32, height: UInt32, unqualified_seed: PinholeBrownModel, association: SolveOptions, maximum_frames: UInt32, maximum_blocks: UInt32, maximum_fit_iterations: UInt32, maximum_work_units: UInt64, maximum_memory_bytes: UInt64, remaining_manual_moves: UInt32) {
        self.session_id = session_id
        self.camera_configuration_id = camera_configuration_id
        self.physical_camera_id = physical_camera_id
        self.pixel_convention_id = pixel_convention_id
        self.optical_state_id = optical_state_id
        self.width = width
        self.height = height
        self.unqualified_seed = unqualified_seed
        self.association = association
        self.maximum_frames = maximum_frames
        self.maximum_blocks = maximum_blocks
        self.maximum_fit_iterations = maximum_fit_iterations
        self.maximum_work_units = maximum_work_units
        self.maximum_memory_bytes = maximum_memory_bytes
        self.remaining_manual_moves = remaining_manual_moves
    }
}

public struct CalibrationFrame: Codable, Sendable {
    public var id: String
    public var pose_id: String
    public var block_id: String
    public var camera_configuration_id: String
    public var evidence_digest: String
    public var quality_provenance_id: String
    public var timing_provenance_id: String
    public var role: CaptureRole
    public var exposure: ExposureTime
    public var earth: EarthContext
    public var stars: [PixelStar]
    public init(id: String, pose_id: String, block_id: String, camera_configuration_id: String, evidence_digest: String, quality_provenance_id: String, timing_provenance_id: String, role: CaptureRole, exposure: ExposureTime, earth: EarthContext, stars: [PixelStar]) {
        self.id = id
        self.pose_id = pose_id
        self.block_id = block_id
        self.camera_configuration_id = camera_configuration_id
        self.evidence_digest = evidence_digest
        self.quality_provenance_id = quality_provenance_id
        self.timing_provenance_id = timing_provenance_id
        self.role = role
        self.exposure = exposure
        self.earth = earth
        self.stars = stars
    }
}

public struct IngestReceipt: Codable, Sendable {
    public var frame_id: String
    public var evidence_digest: String
    public var reason: String
    public var admitted: Bool
    public var matched_sources: UInt32
    public var association_error: ErrorCode?
    public var matches: [StarMatch]
    public var limitations: [String]
    public init(frame_id: String, evidence_digest: String, reason: String, admitted: Bool, matched_sources: UInt32, association_error: ErrorCode?, matches: [StarMatch], limitations: [String]) {
        self.frame_id = frame_id
        self.evidence_digest = evidence_digest
        self.reason = reason
        self.admitted = admitted
        self.matched_sources = matched_sources
        self.association_error = association_error
        self.matches = matches
        self.limitations = limitations
    }
}

public struct BlockCompletion: Codable, Sendable {
    public var id: String
    public var pose_id: String
    public var role: CaptureRole
    public var start: UtcInstant
    public var end: UtcInstant
    public var planned_frame_ids: [String]
    public var missing_frame_ids: [String]
    public var missing_reasons: [String]
    public init(id: String, pose_id: String, role: CaptureRole, start: UtcInstant, end: UtcInstant, planned_frame_ids: [String], missing_frame_ids: [String], missing_reasons: [String]) {
        self.id = id
        self.pose_id = pose_id
        self.role = role
        self.start = start
        self.end = end
        self.planned_frame_ids = planned_frame_ids
        self.missing_frame_ids = missing_frame_ids
        self.missing_reasons = missing_reasons
    }
}

public struct BlockReceipt: Codable, Sendable {
    public var block_id: String
    public var status: String
    public var planned: UInt32
    public var attempted: UInt32
    public var admitted: UInt32
    public init(block_id: String, status: String, planned: UInt32, attempted: UInt32, admitted: UInt32) {
        self.block_id = block_id
        self.status = status
        self.planned = planned
        self.attempted = attempted
        self.admitted = admitted
    }
}

public struct CalibrationProgress: Codable, Sendable {
    public var stage: CalibrationStage
    public var frames: UInt32
    public var blocks: UInt32
    public var admitted_frames: UInt32
    public var ready_to_fit: Bool
    public var occupied_cells: [UInt32]
    public var limitations: [String]
    public init(stage: CalibrationStage, frames: UInt32, blocks: UInt32, admitted_frames: UInt32, ready_to_fit: Bool, occupied_cells: [UInt32], limitations: [String]) {
        self.stage = stage
        self.frames = frames
        self.blocks = blocks
        self.admitted_frames = admitted_frames
        self.ready_to_fit = ready_to_fit
        self.occupied_cells = occupied_cells
        self.limitations = limitations
    }
}

public struct CalibrationAction: Codable, Sendable {
    public var kind: String
    public var reason: String
    public var requested_frames: UInt32
    public init(kind: String, reason: String, requested_frames: UInt32) {
        self.kind = kind
        self.reason = reason
        self.requested_frames = requested_frames
    }
}

public struct OpticalModelDiagnostic: Codable, Sendable {
    public var family: String
    public var status: String
    public var model: PinholeBrownModel
    public var training_rms_rad: Double?
    public var selection_prediction_rms_rad: Double?
    public var profile_condition: Double?
    public var profile_rank: UInt32
    public var profile_eigenvalues: [Double]
    public var weakest_mode_scaled: [Double]
    public var parameter_order: [String]
    public init(family: String, status: String, model: PinholeBrownModel, training_rms_rad: Double?, selection_prediction_rms_rad: Double?, profile_condition: Double?, profile_rank: UInt32, profile_eigenvalues: [Double], weakest_mode_scaled: [Double], parameter_order: [String]) {
        self.family = family
        self.status = status
        self.model = model
        self.training_rms_rad = training_rms_rad
        self.selection_prediction_rms_rad = selection_prediction_rms_rad
        self.profile_condition = profile_condition
        self.profile_rank = profile_rank
        self.profile_eigenvalues = profile_eigenvalues
        self.weakest_mode_scaled = weakest_mode_scaled
        self.parameter_order = parameter_order
    }
}

public struct CandidateData: Codable, Sendable {
    public var id: String
    public var session_id: String
    public var catalog_content_id: String
    public var astrometry_content_id: String
    public var data_digest: String
    public var calibration: CameraCalibration
    public var models: [OpticalModelDiagnostic]
    public var limitations: [String]
    public var physical_accuracy_qualified: Bool
    public init(id: String, session_id: String, catalog_content_id: String, astrometry_content_id: String, data_digest: String, calibration: CameraCalibration, models: [OpticalModelDiagnostic], limitations: [String], physical_accuracy_qualified: Bool) {
        self.id = id
        self.session_id = session_id
        self.catalog_content_id = catalog_content_id
        self.astrometry_content_id = astrometry_content_id
        self.data_digest = data_digest
        self.calibration = calibration
        self.models = models
        self.limitations = limitations
        self.physical_accuracy_qualified = physical_accuracy_qualified
    }
}

public struct ValidationRequest: Codable, Sendable {
    public var id: String
    public var pose_id: String
    public var admission_policy_id: String
    public var freeze_time: UtcInstant
    public var first_start: UtcInstant
    public init(id: String, pose_id: String, admission_policy_id: String, freeze_time: UtcInstant, first_start: UtcInstant) {
        self.id = id
        self.pose_id = pose_id
        self.admission_policy_id = admission_policy_id
        self.freeze_time = freeze_time
        self.first_start = first_start
    }
}

public struct ValidationSlot: Codable, Sendable {
    public var block_id: String
    public var start: UtcInstant
    public var end: UtcInstant
    public init(block_id: String, start: UtcInstant, end: UtcInstant) {
        self.block_id = block_id
        self.start = start
        self.end = end
    }
}

public struct ValidationPlan: Codable, Sendable {
    public var id: String
    public var pose_id: String
    public var admission_policy_id: String
    public var freeze_time: UtcInstant
    public var source_panel: [String]
    public var slots: [ValidationSlot]
    public var minimum_frames_per_block: UInt32
    public var minimum_span_s: Double
    public var maximum_north_change_rad: Double
    public var maximum_pairwise_change_rad: Double
    public init(id: String, pose_id: String, admission_policy_id: String, freeze_time: UtcInstant, source_panel: [String], slots: [ValidationSlot], minimum_frames_per_block: UInt32, minimum_span_s: Double, maximum_north_change_rad: Double, maximum_pairwise_change_rad: Double) {
        self.id = id
        self.pose_id = pose_id
        self.admission_policy_id = admission_policy_id
        self.freeze_time = freeze_time
        self.source_panel = source_panel
        self.slots = slots
        self.minimum_frames_per_block = minimum_frames_per_block
        self.minimum_span_s = minimum_span_s
        self.maximum_north_change_rad = maximum_north_change_rad
        self.maximum_pairwise_change_rad = maximum_pairwise_change_rad
    }
}

public struct ValidationBlockResult: Codable, Sendable {
    public var block_id: String
    public var status: String
    public var planned_frames: UInt32
    public var complete_panel_frames: UInt32
    public var mean_north_camera: Vec3?
    public var scatter_rad: Double?
    public var maximum_pair_error_rad: Double?
    public init(block_id: String, status: String, planned_frames: UInt32, complete_panel_frames: UInt32, mean_north_camera: Vec3?, scatter_rad: Double?, maximum_pair_error_rad: Double?) {
        self.block_id = block_id
        self.status = status
        self.planned_frames = planned_frames
        self.complete_panel_frames = complete_panel_frames
        self.mean_north_camera = mean_north_camera
        self.scatter_rad = scatter_rad
        self.maximum_pair_error_rad = maximum_pair_error_rad
    }
}

public struct ValidationReport: Codable, Sendable {
    public var candidate_id: String
    public var plan_id: String
    public var status: String
    public var blocks: [ValidationBlockResult]
    public var span_s: Double?
    public var max_north_change_rad: Double?
    public var max_pairwise_change_rad: Double?
    public var limitations: [String]
    public var physical_accuracy_qualified: Bool
    public init(candidate_id: String, plan_id: String, status: String, blocks: [ValidationBlockResult], span_s: Double?, max_north_change_rad: Double?, max_pairwise_change_rad: Double?, limitations: [String], physical_accuracy_qualified: Bool) {
        self.candidate_id = candidate_id
        self.plan_id = plan_id
        self.status = status
        self.blocks = blocks
        self.span_s = span_s
        self.max_north_change_rad = max_north_change_rad
        self.max_pairwise_change_rad = max_pairwise_change_rad
        self.limitations = limitations
        self.physical_accuracy_qualified = physical_accuracy_qualified
    }
}

public struct CalibrationPackage: Codable, Sendable {
    public var calibration: CameraCalibration
    public var candidate_id: String
    public var plan_id: String
    public var catalog_content_id: String
    public var astrometry_content_id: String
    public var qualification: String
    public var physical_accuracy_qualified: Bool
    public init(calibration: CameraCalibration, candidate_id: String, plan_id: String, catalog_content_id: String, astrometry_content_id: String, qualification: String, physical_accuracy_qualified: Bool) {
        self.calibration = calibration
        self.candidate_id = candidate_id
        self.plan_id = plan_id
        self.catalog_content_id = catalog_content_id
        self.astrometry_content_id = astrometry_content_id
        self.qualification = qualification
        self.physical_accuracy_qualified = physical_accuracy_qualified
    }
}

public struct AlignmentFrame: Codable, Sendable {
    public var frame_id: String
    public var camera_id: String
    public var reference_id: String
    public var optical_provenance: String
    public var start_s: Double
    public var end_s: Double
    public var R_reference_from_camera: Rotation3
    public var R_camera_from_accelerometer_nominal: Rotation3
    public init(frame_id: String, camera_id: String, reference_id: String, optical_provenance: String, start_s: Double, end_s: Double, R_reference_from_camera: Rotation3, R_camera_from_accelerometer_nominal: Rotation3) {
        self.frame_id = frame_id
        self.camera_id = camera_id
        self.reference_id = reference_id
        self.optical_provenance = optical_provenance
        self.start_s = start_s
        self.end_s = end_s
        self.R_reference_from_camera = R_reference_from_camera
        self.R_camera_from_accelerometer_nominal = R_camera_from_accelerometer_nominal
    }
}

public struct AlignmentImu: Codable, Sendable {
    public var start_s: Double
    public var end_s: Double
    public var down_accelerometer: Vec3
    public var stationary: Bool
    public init(start_s: Double, end_s: Double, down_accelerometer: Vec3, stationary: Bool) {
        self.start_s = start_s
        self.end_s = end_s
        self.down_accelerometer = down_accelerometer
        self.stationary = stationary
    }
}

public struct AlignmentRequest: Codable, Sendable {
    public var reference_id: String
    public var clock_id: String
    public var cameras: [String]
    public var frames: [AlignmentFrame]
    public var imu: AlignmentImu?
    public var down_reference: Vec3?
    public var minimum_frames: UInt32
    public var motion_detected: Bool
    public init(reference_id: String, clock_id: String, cameras: [String], frames: [AlignmentFrame], imu: AlignmentImu?, down_reference: Vec3?, minimum_frames: UInt32, motion_detected: Bool) {
        self.reference_id = reference_id
        self.clock_id = clock_id
        self.cameras = cameras
        self.frames = frames
        self.imu = imu
        self.down_reference = down_reference
        self.minimum_frames = minimum_frames
        self.motion_detected = motion_detected
    }
}

public struct AlignmentPair: Codable, Sendable {
    public var from_sensor: String
    public var to_sensor: String
    public var status: AlignmentStatus
    public var from_frames: UInt32
    public var to_frames: UInt32
    public var pose_count: UInt32
    public var misalignment_vector_rad: Vec3?
    public var maximum_gravity_residual_rad: Double?
    public var geometry_ratio: Double?
    public var R_to_from: Rotation3?
    public var rotation_vector_rad: Vec3?
    public var gravity_mismatch_rad: Double?
    public var maximum_repeatability_rad: Double?
    public var limitations: [String]
    public init(from_sensor: String, to_sensor: String, status: AlignmentStatus, from_frames: UInt32, to_frames: UInt32, pose_count: UInt32, misalignment_vector_rad: Vec3?, maximum_gravity_residual_rad: Double?, geometry_ratio: Double?, R_to_from: Rotation3?, rotation_vector_rad: Vec3?, gravity_mismatch_rad: Double?, maximum_repeatability_rad: Double?, limitations: [String]) {
        self.from_sensor = from_sensor
        self.to_sensor = to_sensor
        self.status = status
        self.from_frames = from_frames
        self.to_frames = to_frames
        self.pose_count = pose_count
        self.misalignment_vector_rad = misalignment_vector_rad
        self.maximum_gravity_residual_rad = maximum_gravity_residual_rad
        self.geometry_ratio = geometry_ratio
        self.R_to_from = R_to_from
        self.rotation_vector_rad = rotation_vector_rad
        self.gravity_mismatch_rad = gravity_mismatch_rad
        self.maximum_repeatability_rad = maximum_repeatability_rad
        self.limitations = limitations
    }
}

public struct RigAlignmentRequest: Codable, Sendable {
    public var poses: [AlignmentRequest]
    public init(poses: [AlignmentRequest]) {
        self.poses = poses
    }
}

public struct AlignmentReport: Codable, Sendable {
    public var pairs: [AlignmentPair]
    public var physical_accuracy_qualified: Bool
    public init(pairs: [AlignmentPair], physical_accuracy_qualified: Bool) {
        self.pairs = pairs
        self.physical_accuracy_qualified = physical_accuracy_qualified
    }
}

public struct TargetCorner: Codable, Sendable {
    public var id: WireUInt64
    public var target_m: Vec2
    public var pixel: Vec2
    public init(id: WireUInt64, target_m: Vec2, pixel: Vec2) {
        self.id = id
        self.target_m = target_m
        self.pixel = pixel
    }
}

public struct PlanarPoseRequest: Codable, Sendable {
    public var width: UInt32
    public var height: UInt32
    public var fx_px: Double
    public var fy_px: Double
    public var cx_px: Double
    public var cy_px: Double
    public var corners: [TargetCorner]
    public var maximum_rms_px: Double
    public var ambiguity_tolerance_px: Double
    public init(width: UInt32, height: UInt32, fx_px: Double, fy_px: Double, cx_px: Double, cy_px: Double, corners: [TargetCorner], maximum_rms_px: Double, ambiguity_tolerance_px: Double) {
        self.width = width
        self.height = height
        self.fx_px = fx_px
        self.fy_px = fy_px
        self.cx_px = cx_px
        self.cy_px = cy_px
        self.corners = corners
        self.maximum_rms_px = maximum_rms_px
        self.ambiguity_tolerance_px = ambiguity_tolerance_px
    }
}

public struct PlanarPose: Codable, Sendable {
    public var R_camera_from_target: Rotation3
    public var translation_camera_m: Vec3
    public var rms_px: Double
    public var corners: UInt32
    public init(R_camera_from_target: Rotation3, translation_camera_m: Vec3, rms_px: Double, corners: UInt32) {
        self.R_camera_from_target = R_camera_from_target
        self.translation_camera_m = translation_camera_m
        self.rms_px = rms_px
        self.corners = corners
    }
}
