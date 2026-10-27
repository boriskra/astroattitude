"""Generate value-only adapter types. No numerical algorithms live in adapters."""
import json
from pathlib import Path
root=Path(__file__).resolve().parents[1]
models={
'Vec2':'x:double y:double','Vec3':'x:double y:double z:double','Mat2':'row_major:double[]','Mat3':'row_major:double[]','Rotation3':'matrix:Mat3',
'UtcInstant':'unix_seconds:i64 nanoseconds:u32','ExposureTime':'midpoint:UtcInstant duration_s:double midpoint_sigma_s:double?',
'ObserverPosition':'latitude_rad:double longitude_rad:double ellipsoid_height_m:double',
'ObservationIdentity':'observation_id:string camera_configuration_id:string calibration_revision:string exposure:ExposureTime temporal_model:TemporalModel timing_provenance_id:string',
'PixelStar':'id:u64 pixel:Vec2 centroid_covariance_px2:Mat2? flux:double?',
'RayStar':'id:u64 direction_camera:Vec3 direction_covariance_rad2:Mat3?',
'PixelObservation':'identity:ObservationIdentity width:u32 height:u32 stars:PixelStar[]',
'RayObservation':'identity:ObservationIdentity stars:RayStar[]',
'PinholeBrownModel':'fx_px:double fy_px:double cx_px:double cy_px:double k1:double k2:double k3:double p1:double p2:double',
'PixelBounds':'min_x:double min_y:double max_x:double max_y:double',
'CameraCalibration':'revision:string camera_configuration_id:string width:u32 height:u32 model:PinholeBrownModel valid_region:PixelBounds',
'Atmosphere':'pressure_pa:double temperature_kelvin:double relative_humidity:double effective_wavelength_m:double',
'Refraction':'kind:RefractionKind parameters:Atmosphere?',
'EarthContext':'observer:ObserverPosition refraction:Refraction eop_policy:EopPolicy',
'AcceptanceCriteria':'minimum_inliers:u32 maximum_match_error_rad:double maximum_fit_rms_rad:double maximum_conditional_sigma_rad:double?',
'SolveLimits':'maximum_hypotheses:u32 maximum_candidate_comparisons:count maximum_workspace_bytes:count',
'SolveOptions':'acceptance:AcceptanceCriteria limits:SolveLimits',
'StarMatch':'detection_id:u64 catalog_star_id:string angular_residual_rad:double',
'ConditionalUncertainty':'covariance_enu_rad2:Mat3',
'SearchReport':'algorithm_version:string hypotheses_evaluated:u32 candidate_comparisons:count',
'ModelReport':'model_id:string earth_orientation_status:string refraction_status:string limitations:string[]',
'AttitudeSolution':'identity:ObservationIdentity R_enu_from_camera:Rotation3 observer_used:ObserverPosition conditional_uncertainty:ConditionalUncertainty? fit_rms_rad:double fit_max_residual_rad:double matches:StarMatch[] rejected_detections:u64[] catalog_content_id:string astrometry_content_id:string implementation_version:string search:SearchReport models:ModelReport',
'SolverError':'code:ErrorCode detail:string affected_detections:u64[]'}
models.update({'CalibrationPreparation':'session_id:string camera_configuration_id:string physical_camera_id:string pixel_convention_id:string optical_state_id:string width:u32 height:u32 horizontal_fov_rad:double remaining_manual_moves:u32','ExtractionOptions': 'detection_sigma:double maximum_axis_ratio:double maximum_candidates:u32 maximum_stars:u32 maximum_fit_iterations:u32 maximum_pixels:count maximum_workspace_bytes:count maximum_work_units:count', 'ExtractionMetadata': 'width:u32 height:u32 row_stride:u32 layout:SampleLayout black_levels:double[] white_levels:double[] options:ExtractionOptions', 'SourceDiagnostic': 'id:u64 pixel:Vec2 reason:string sigma_major_px:double sigma_minor_px:double amplitude:double residual_dn:double local_noise_dn:double', 'ExtractionRegion': 'bounds:PixelBounds reason:string', 'ExtractionResult': 'policy_id:string stars:PixelStar[] sources:SourceDiagnostic[] regions:ExtractionRegion[] work_units:count candidate_capacity_reached:bool physical_accuracy_qualified:bool', 'CalibrationConfiguration': 'session_id:string camera_configuration_id:string physical_camera_id:string pixel_convention_id:string optical_state_id:string width:u32 height:u32 unqualified_seed:PinholeBrownModel association:SolveOptions maximum_frames:u32 maximum_blocks:u32 maximum_fit_iterations:u32 maximum_work_units:count maximum_memory_bytes:count remaining_manual_moves:u32', 'CalibrationFrame': 'id:string pose_id:string block_id:string camera_configuration_id:string evidence_digest:string quality_provenance_id:string timing_provenance_id:string role:CaptureRole exposure:ExposureTime earth:EarthContext stars:PixelStar[]', 'IngestReceipt': 'frame_id:string evidence_digest:string reason:string admitted:bool matched_sources:u32 association_error:ErrorCode? matches:StarMatch[] limitations:string[]', 'BlockCompletion': 'id:string pose_id:string role:CaptureRole start:UtcInstant end:UtcInstant planned_frame_ids:string[] missing_frame_ids:string[] missing_reasons:string[]', 'BlockReceipt': 'block_id:string status:string planned:u32 attempted:u32 admitted:u32', 'CalibrationProgress': 'stage:CalibrationStage frames:u32 blocks:u32 admitted_frames:u32 ready_to_fit:bool occupied_cells:u32[] limitations:string[]', 'CalibrationAction': 'kind:string reason:string requested_frames:u32', 'OpticalModelDiagnostic': 'family:string status:string model:PinholeBrownModel training_rms_rad:double? selection_prediction_rms_rad:double? profile_condition:double? profile_rank:u32 profile_eigenvalues:double[] weakest_mode_scaled:double[] parameter_order:string[]', 'CandidateData': 'id:string session_id:string catalog_content_id:string astrometry_content_id:string data_digest:string calibration:CameraCalibration models:OpticalModelDiagnostic[] limitations:string[] physical_accuracy_qualified:bool', 'ValidationRequest': 'id:string pose_id:string admission_policy_id:string freeze_time:UtcInstant first_start:UtcInstant',
'ValidationSlot': 'block_id:string start:UtcInstant end:UtcInstant', 'ValidationPlan': 'id:string pose_id:string admission_policy_id:string freeze_time:UtcInstant source_panel:string[] slots:ValidationSlot[] minimum_frames_per_block:u32 minimum_span_s:double maximum_north_change_rad:double maximum_pairwise_change_rad:double', 'ValidationBlockResult': 'block_id:string status:string planned_frames:u32 complete_panel_frames:u32 mean_north_camera:Vec3? scatter_rad:double? maximum_pair_error_rad:double?', 'ValidationReport': 'candidate_id:string plan_id:string status:string blocks:ValidationBlockResult[] span_s:double? max_north_change_rad:double? max_pairwise_change_rad:double? limitations:string[] physical_accuracy_qualified:bool', 'CalibrationPackage': 'calibration:CameraCalibration candidate_id:string plan_id:string catalog_content_id:string astrometry_content_id:string qualification:string physical_accuracy_qualified:bool'})
models.update({
'AlignmentFrame':'frame_id:string camera_id:string reference_id:string optical_provenance:string start_s:double end_s:double R_reference_from_camera:Rotation3 R_camera_from_accelerometer_nominal:Rotation3',
'AlignmentImu':'start_s:double end_s:double down_accelerometer:Vec3 stationary:bool',
'AlignmentRequest':'reference_id:string clock_id:string cameras:string[] frames:AlignmentFrame[] imu:AlignmentImu? down_reference:Vec3? minimum_frames:u32 motion_detected:bool',
'AlignmentPair':'from_sensor:string to_sensor:string status:AlignmentStatus from_frames:u32 to_frames:u32 pose_count:u32 misalignment_vector_rad:Vec3? maximum_gravity_residual_rad:double? geometry_ratio:double? R_to_from:Rotation3? rotation_vector_rad:Vec3? gravity_mismatch_rad:double? maximum_repeatability_rad:double? limitations:string[]',
'RigAlignmentRequest':'poses:AlignmentRequest[]',
'AlignmentReport':'pairs:AlignmentPair[] physical_accuracy_qualified:bool',
'TargetCorner':'id:u64 target_m:Vec2 pixel:Vec2',
'PlanarPoseRequest':'width:u32 height:u32 fx_px:double fy_px:double cx_px:double cy_px:double corners:TargetCorner[] maximum_rms_px:double ambiguity_tolerance_px:double',
'PlanarPose':'R_camera_from_target:Rotation3 translation_camera_m:Vec3 rms_px:double corners:u32'
})
(root/'bindings/schema/models.json').write_text(json.dumps(models,indent=2)+'\n')
def typ(t,lang):
 if t.endswith('?'):
  x=typ(t[:-1],lang);return f'Optional[{x}]' if lang=='py' else x+'?'
 if t.endswith('[]'):
  x=typ(t[:-2],lang);return f'List[{x}]' if lang=='py' else f'[{x}]' if lang=='swift' else f'List<{x}>'
 return {'py':{'bool':'bool','double':'float','string':'str','i64':'int','u64':'int','u32':'int','count':'int'},'swift':{'bool':'Bool','double':'Double','string':'String','i64':'WireInt64','u64':'WireUInt64','u32':'UInt32','count':'UInt64'},'kt':{'bool':'Boolean','double':'Double','string':'String','i64':'WireInt64','u64':'WireUInt64','u32':'UInt','count':'ULong'}}[lang].get(t,t)
py='from dataclasses import dataclass\nfrom typing import List, Optional\n\n'
swift='import Foundation\n\n'
kt='package org.astroattitude\n\nimport kotlinx.serialization.Serializable\n\n'
enums = {
    'AlignmentStatus': 'estimated tilt_only gravity_reference_unavailable motion_evidence_unavailable motion_detected insufficient_frames optical_configuration_changed inconsistent_frames rotation_estimated pose_diversity_insufficient ambiguous_alignment fit_inconsistent',
    'ErrorCode': 'InvalidInput InvalidResource UnsupportedVersion CalibrationMismatch OutsideCalibrationDomain UnsupportedObservation InsufficientObservations AstrometryDataUnavailable NoMatch AmbiguousMatch UnobservableGeometry ResidualLimitExceeded UncertaintyUnavailable ConditionalPrecisionNotMet ResourceLimitExceeded Cancelled InternalError',
    'TemporalModel': 'stationary_single_exposure aligned_effective_exposure',
    'EopPolicy': 'observed_only allow_predicted',
    'RefractionKind': 'disabled atmosphere',
    'SampleLayout': 'mono rggb bggr grbg gbrg',
    'CaptureRole': 'training model_selection reserve validation',
    'CalibrationStage': 'mapping model_selection reserve frozen validation complete paused ended'
}
py += 'from enum import Enum\n\n'
for name, values in enums.items():
    py += f'class {name}(str, Enum):\n' + ''.join(f'    {v} = "{v}"\n' for v in values.split()) + '\n'
    swift += f'public enum {name}: String, Codable, Sendable {{\n' + ''.join(f'    case {v}\n' for v in values.split()) + '}\n\n'
    kt += f'@Serializable\nenum class {name} {{ ' + ', '.join(values.split()) + ' }\n\n'
for name,fields in models.items():
 fs=[x.split(':') for x in fields.split()]
 py+='@dataclass(frozen=True)\nclass '+name+':\n'+''.join(f'    {n}: {typ(t,"py")}\n' for n,t in fs)+'\n'
 swift+=f'public struct {name}: Codable, Sendable {{\n'+''.join(f'    public var {n}: {typ(t,"swift")}\n' for n,t in fs)+f'    public init('+', '.join(f'{n}: {typ(t,"swift")}' for n,t in fs)+') {\n'+''.join(f'        self.{n} = {n}\n' for n,t in fs)+'    }\n}\n\n'
 kt+=f'@Serializable\ndata class {name}(\n'+',\n'.join(f'    val {n}: {typ(t,"kt")}' + (' = null' if name == 'Refraction' and n == 'parameters' else '') for n,t in fs)+'\n)\n\n'
(root/'bindings/python/astroattitude/models.py').write_text(py.rstrip()+"\n")
(root/'bindings/swift/Sources/Astroattitude/Models.swift').write_text(swift.rstrip()+"\n")
(root/'bindings/android/library/src/main/kotlin/org/astroattitude/Models.kt').write_text(kt.rstrip()+"\n")
