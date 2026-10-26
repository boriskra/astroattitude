from dataclasses import dataclass
from typing import List, Optional

from enum import Enum

class AlignmentStatus(str, Enum):
    estimated = "estimated"
    tilt_only = "tilt_only"
    gravity_reference_unavailable = "gravity_reference_unavailable"
    motion_evidence_unavailable = "motion_evidence_unavailable"
    motion_detected = "motion_detected"
    insufficient_frames = "insufficient_frames"
    optical_configuration_changed = "optical_configuration_changed"
    inconsistent_frames = "inconsistent_frames"
    rotation_estimated = "rotation_estimated"
    pose_diversity_insufficient = "pose_diversity_insufficient"
    ambiguous_alignment = "ambiguous_alignment"
    fit_inconsistent = "fit_inconsistent"

class ErrorCode(str, Enum):
    InvalidInput = "InvalidInput"
    InvalidResource = "InvalidResource"
    UnsupportedVersion = "UnsupportedVersion"
    CalibrationMismatch = "CalibrationMismatch"
    OutsideCalibrationDomain = "OutsideCalibrationDomain"
    UnsupportedObservation = "UnsupportedObservation"
    InsufficientObservations = "InsufficientObservations"
    AstrometryDataUnavailable = "AstrometryDataUnavailable"
    NoMatch = "NoMatch"
    AmbiguousMatch = "AmbiguousMatch"
    UnobservableGeometry = "UnobservableGeometry"
    ResidualLimitExceeded = "ResidualLimitExceeded"
    UncertaintyUnavailable = "UncertaintyUnavailable"
    ConditionalPrecisionNotMet = "ConditionalPrecisionNotMet"
    ResourceLimitExceeded = "ResourceLimitExceeded"
    Cancelled = "Cancelled"
    InternalError = "InternalError"

class TemporalModel(str, Enum):
    stationary_single_exposure = "stationary_single_exposure"
    aligned_effective_exposure = "aligned_effective_exposure"

class EopPolicy(str, Enum):
    observed_only = "observed_only"
    allow_predicted = "allow_predicted"

class RefractionKind(str, Enum):
    disabled = "disabled"
    atmosphere = "atmosphere"

class SampleLayout(str, Enum):
    mono = "mono"
    rggb = "rggb"
    bggr = "bggr"
    grbg = "grbg"
    gbrg = "gbrg"

class CaptureRole(str, Enum):
    training = "training"
    model_selection = "model_selection"
    reserve = "reserve"
    validation = "validation"

class CalibrationStage(str, Enum):
    mapping = "mapping"
    model_selection = "model_selection"
    reserve = "reserve"
    frozen = "frozen"
    validation = "validation"
    complete = "complete"
    paused = "paused"
    ended = "ended"

@dataclass(frozen=True)
class Vec2:
    x: float
    y: float

@dataclass(frozen=True)
class Vec3:
    x: float
    y: float
    z: float

@dataclass(frozen=True)
class Mat2:
    row_major: List[float]

@dataclass(frozen=True)
class Mat3:
    row_major: List[float]

@dataclass(frozen=True)
class Rotation3:
    matrix: Mat3

@dataclass(frozen=True)
class UtcInstant:
    unix_seconds: int
    nanoseconds: int

@dataclass(frozen=True)
class ExposureTime:
    midpoint: UtcInstant
    duration_s: float
    midpoint_sigma_s: Optional[float]

@dataclass(frozen=True)
class ObserverPosition:
    latitude_rad: float
    longitude_rad: float
    ellipsoid_height_m: float

@dataclass(frozen=True)
class ObservationIdentity:
    observation_id: str
    camera_configuration_id: str
    calibration_revision: str
    exposure: ExposureTime
    temporal_model: TemporalModel
    timing_provenance_id: str

@dataclass(frozen=True)
class PixelStar:
    id: int
    pixel: Vec2
    centroid_covariance_px2: Optional[Mat2]
    flux: Optional[float]

@dataclass(frozen=True)
class RayStar:
    id: int
    direction_camera: Vec3
    direction_covariance_rad2: Optional[Mat3]

@dataclass(frozen=True)
class PixelObservation:
    identity: ObservationIdentity
    width: int
    height: int
    stars: List[PixelStar]

@dataclass(frozen=True)
class RayObservation:
    identity: ObservationIdentity
    stars: List[RayStar]

@dataclass(frozen=True)
class PinholeBrownModel:
    fx_px: float
    fy_px: float
    cx_px: float
    cy_px: float
    k1: float
    k2: float
    k3: float
    p1: float
    p2: float

@dataclass(frozen=True)
class PixelBounds:
    min_x: float
    min_y: float
    max_x: float
    max_y: float

@dataclass(frozen=True)
class CameraCalibration:
    revision: str
    camera_configuration_id: str
    width: int
    height: int
    model: PinholeBrownModel
    valid_region: PixelBounds

@dataclass(frozen=True)
class Atmosphere:
    pressure_pa: float
    temperature_kelvin: float
    relative_humidity: float
    effective_wavelength_m: float

@dataclass(frozen=True)
class Refraction:
    kind: RefractionKind
    parameters: Optional[Atmosphere]

@dataclass(frozen=True)
class EarthContext:
    observer: ObserverPosition
    refraction: Refraction
    eop_policy: EopPolicy

@dataclass(frozen=True)
class AcceptanceCriteria:
    minimum_inliers: int
    maximum_match_error_rad: float
    maximum_fit_rms_rad: float
    maximum_conditional_sigma_rad: Optional[float]

@dataclass(frozen=True)
class SolveLimits:
    maximum_hypotheses: int
    maximum_candidate_comparisons: int
    maximum_workspace_bytes: int

@dataclass(frozen=True)
class SolveOptions:
    acceptance: AcceptanceCriteria
    limits: SolveLimits

@dataclass(frozen=True)
class StarMatch:
    detection_id: int
    catalog_star_id: str
    angular_residual_rad: float

@dataclass(frozen=True)
class ConditionalUncertainty:
    covariance_enu_rad2: Mat3

@dataclass(frozen=True)
class SearchReport:
    algorithm_version: str
    hypotheses_evaluated: int
    candidate_comparisons: int

@dataclass(frozen=True)
class ModelReport:
    model_id: str
    earth_orientation_status: str
    refraction_status: str
    limitations: List[str]

@dataclass(frozen=True)
class AttitudeSolution:
    identity: ObservationIdentity
    R_enu_from_camera: Rotation3
    observer_used: ObserverPosition
    conditional_uncertainty: Optional[ConditionalUncertainty]
    fit_rms_rad: float
    fit_max_residual_rad: float
    matches: List[StarMatch]
    rejected_detections: List[int]
    catalog_content_id: str
    astrometry_content_id: str
    implementation_version: str
    search: SearchReport
    models: ModelReport

@dataclass(frozen=True)
class SolverError:
    code: ErrorCode
    detail: str
    affected_detections: List[int]

@dataclass(frozen=True)
class CalibrationPreparation:
    session_id: str
    camera_configuration_id: str
    physical_camera_id: str
    pixel_convention_id: str
    optical_state_id: str
    width: int
    height: int
    horizontal_fov_rad: float
    remaining_manual_moves: int

@dataclass(frozen=True)
class ExtractionOptions:
    detection_sigma: float
    maximum_axis_ratio: float
    maximum_candidates: int
    maximum_stars: int
    maximum_fit_iterations: int
    maximum_pixels: int
    maximum_workspace_bytes: int
    maximum_work_units: int

@dataclass(frozen=True)
class ExtractionMetadata:
    width: int
    height: int
    row_stride: int
    layout: SampleLayout
    black_levels: List[float]
    white_levels: List[float]
    options: ExtractionOptions

@dataclass(frozen=True)
class SourceDiagnostic:
    id: int
    pixel: Vec2
    reason: str
    sigma_major_px: float
    sigma_minor_px: float
    amplitude: float
    residual_dn: float
    local_noise_dn: float

@dataclass(frozen=True)
class ExtractionRegion:
    bounds: PixelBounds
    reason: str

@dataclass(frozen=True)
class ExtractionResult:
    policy_id: str
    stars: List[PixelStar]
    sources: List[SourceDiagnostic]
    regions: List[ExtractionRegion]
    work_units: int
    candidate_capacity_reached: bool
    physical_accuracy_qualified: bool

@dataclass(frozen=True)
class CalibrationConfiguration:
    session_id: str
    camera_configuration_id: str
    physical_camera_id: str
    pixel_convention_id: str
    optical_state_id: str
    width: int
    height: int
    unqualified_seed: PinholeBrownModel
    association: SolveOptions
    maximum_frames: int
    maximum_blocks: int
    maximum_fit_iterations: int
    maximum_work_units: int
    maximum_memory_bytes: int
    remaining_manual_moves: int

@dataclass(frozen=True)
class CalibrationFrame:
    id: str
    pose_id: str
    block_id: str
    camera_configuration_id: str
    evidence_digest: str
    quality_provenance_id: str
    timing_provenance_id: str
    role: CaptureRole
    exposure: ExposureTime
    earth: EarthContext
    stars: List[PixelStar]

@dataclass(frozen=True)
class IngestReceipt:
    frame_id: str
    evidence_digest: str
    reason: str
    admitted: bool
    matched_sources: int
    association_error: Optional[ErrorCode]
    matches: List[StarMatch]
    limitations: List[str]

@dataclass(frozen=True)
class BlockCompletion:
    id: str
    pose_id: str
    role: CaptureRole
    start: UtcInstant
    end: UtcInstant
    planned_frame_ids: List[str]
    missing_frame_ids: List[str]
    missing_reasons: List[str]

@dataclass(frozen=True)
class BlockReceipt:
    block_id: str
    status: str
    planned: int
    attempted: int
    admitted: int

@dataclass(frozen=True)
class CalibrationProgress:
    stage: CalibrationStage
    frames: int
    blocks: int
    admitted_frames: int
    ready_to_fit: bool
    occupied_cells: List[int]
    limitations: List[str]

@dataclass(frozen=True)
class CalibrationAction:
    kind: str
    reason: str
    requested_frames: int

@dataclass(frozen=True)
class OpticalModelDiagnostic:
    family: str
    status: str
    model: PinholeBrownModel
    training_rms_rad: Optional[float]
    selection_prediction_rms_rad: Optional[float]
    profile_condition: Optional[float]
    profile_rank: int
    profile_eigenvalues: List[float]
    weakest_mode_scaled: List[float]
    parameter_order: List[str]

@dataclass(frozen=True)
class CandidateData:
    id: str
    session_id: str
    catalog_content_id: str
    astrometry_content_id: str
    data_digest: str
    calibration: CameraCalibration
    models: List[OpticalModelDiagnostic]
    limitations: List[str]
    physical_accuracy_qualified: bool

@dataclass(frozen=True)
class ValidationRequest:
    id: str
    pose_id: str
    admission_policy_id: str
    freeze_time: UtcInstant
    first_start: UtcInstant

@dataclass(frozen=True)
class ValidationSlot:
    block_id: str
    start: UtcInstant
    end: UtcInstant

@dataclass(frozen=True)
class ValidationPlan:
    id: str
    pose_id: str
    admission_policy_id: str
    freeze_time: UtcInstant
    source_panel: List[str]
    slots: List[ValidationSlot]
    minimum_frames_per_block: int
    minimum_span_s: float
    maximum_north_change_rad: float
    maximum_pairwise_change_rad: float

@dataclass(frozen=True)
class ValidationBlockResult:
    block_id: str
    status: str
    planned_frames: int
    complete_panel_frames: int
    mean_north_camera: Optional[Vec3]
    scatter_rad: Optional[float]
    maximum_pair_error_rad: Optional[float]

@dataclass(frozen=True)
class ValidationReport:
    candidate_id: str
    plan_id: str
    status: str
    blocks: List[ValidationBlockResult]
    span_s: Optional[float]
    max_north_change_rad: Optional[float]
    max_pairwise_change_rad: Optional[float]
    limitations: List[str]
    physical_accuracy_qualified: bool

@dataclass(frozen=True)
class CalibrationPackage:
    calibration: CameraCalibration
    candidate_id: str
    plan_id: str
    catalog_content_id: str
    astrometry_content_id: str
    qualification: str
    physical_accuracy_qualified: bool

@dataclass(frozen=True)
class AlignmentFrame:
    frame_id: str
    camera_id: str
    reference_id: str
    optical_provenance: str
    start_s: float
    end_s: float
    R_reference_from_camera: Rotation3
    R_camera_from_accelerometer_nominal: Rotation3

@dataclass(frozen=True)
class AlignmentImu:
    start_s: float
    end_s: float
    down_accelerometer: Vec3
    stationary: bool

@dataclass(frozen=True)
class AlignmentRequest:
    reference_id: str
    clock_id: str
    cameras: List[str]
    frames: List[AlignmentFrame]
    imu: Optional[AlignmentImu]
    down_reference: Optional[Vec3]
    minimum_frames: int
    motion_detected: bool

@dataclass(frozen=True)
class AlignmentPair:
    from_sensor: str
    to_sensor: str
    status: AlignmentStatus
    from_frames: int
    to_frames: int
    pose_count: int
    misalignment_vector_rad: Optional[Vec3]
    maximum_gravity_residual_rad: Optional[float]
    geometry_ratio: Optional[float]
    R_to_from: Optional[Rotation3]
    rotation_vector_rad: Optional[Vec3]
    gravity_mismatch_rad: Optional[float]
    maximum_repeatability_rad: Optional[float]
    limitations: List[str]

@dataclass(frozen=True)
class RigAlignmentRequest:
    poses: List[AlignmentRequest]

@dataclass(frozen=True)
class AlignmentReport:
    pairs: List[AlignmentPair]
    physical_accuracy_qualified: bool

@dataclass(frozen=True)
class TargetCorner:
    id: int
    target_m: Vec2
    pixel: Vec2

@dataclass(frozen=True)
class PlanarPoseRequest:
    width: int
    height: int
    fx_px: float
    fy_px: float
    cx_px: float
    cy_px: float
    corners: List[TargetCorner]
    maximum_rms_px: float
    ambiguity_tolerance_px: float

@dataclass(frozen=True)
class PlanarPose:
    R_camera_from_target: Rotation3
    translation_camera_m: Vec3
    rms_px: float
    corners: int
