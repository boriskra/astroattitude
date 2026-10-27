import AlignmentAdapter
import Astroattitude
import Foundation
import simd

enum SceneMode: String, CaseIterable, Identifiable {
  case stars = "Stars"
  case target = "Printed target"
  var id: String { rawValue }
}
struct CameraMeasurement: Sendable {
  let rotation: Rotation3
  let provenance: String
}
final class MeasurementProcessor: @unchecked Sendable {
  let engine: Engine?
  let observer: ObserverPosition?
  let clockAnchor: (uptime: Double, utc: Double)
  init(resources: SkyResources?, observer: ObserverPosition?) throws {
    self.engine = try resources.map { try Engine(catalog: $0.catalog, astrometry: $0.astrometry) }
    self.observer = observer
    clockAnchor = (ProcessInfo.processInfo.systemUptime, Date().timeIntervalSince1970)
  }
  deinit { engine?.close() }
  static let nominal = Rotation3(matrix: Mat3(row_major: [1, 0, 0, 0, -1, 0, 0, 0, -1]))
  func process(
    _ frame: CapturedFrame, mode: SceneMode, cameraID: String, fov: Double,
    cancel: CancellationToken
  ) throws -> CameraMeasurement {
    if mode == .target { return try target(frame, cancel: cancel) }
    guard let engine, let observer else {
      throw DemoError.message("Sky references or location are missing.")
    }
    let metadata = ExtractionMetadata(
      width: UInt32(frame.width), height: UInt32(frame.height), row_stride: UInt32(frame.width),
      layout: frame.layout, black_levels: frame.black, white_levels: frame.white,
      options: ExtractionOptions(
        detection_sigma: 6, maximum_axis_ratio: 2.5, maximum_candidates: 128, maximum_stars: 64,
        maximum_fit_iterations: 20, maximum_pixels: 16_000_000,
        maximum_workspace_bytes: 536_870_912, maximum_work_units: 2_000_000_000))
    let extracted = try StarExtractor.extract(
      pixels: frame.pixels, metadata: metadata, cancel: cancel)
    guard extracted.stars.count >= 6 else {
      throw DemoError.message("Too few compact stars. Use a clearer, darker field.")
    }
    let portrait = try Self.portrait(extracted.stars, frame: frame)
    let timestamp = clockAnchor.utc + frame.midpoint - clockAnchor.uptime
    guard
      abs(
        (Date().timeIntervalSince1970 - ProcessInfo.processInfo.systemUptime)
          - (clockAnchor.utc - clockAnchor.uptime)) < 0.1
    else { throw DemoError.message("System clock changed during capture.") }
    let whole = floor(timestamp)
    let exposure = ExposureTime(
      midpoint: UtcInstant(
        unix_seconds: WireInt64(Int64(whole)), nanoseconds: UInt32((timestamp - whole) * 1e9)),
      duration_s: frame.duration, midpoint_sigma_s: nil)
    let k = frame.intrinsics
    let focal = Double(max(frame.width, frame.height)) / (2 * tan(fov / 2))
    let optical =
      k == nil ? "FOV-seed-approximation" : "delivered-intrinsics-distortion-unqualified"
    let model = PinholeBrownModel(
      fx_px: k.map { Double($0[0][0]) } ?? focal, fy_px: k.map { Double($0[1][1]) } ?? focal,
      cx_px: k.map { Double($0[2][0]) } ?? Double(portrait.width - 1) / 2,
      cy_px: k.map { Double($0[2][1]) } ?? Double(portrait.height - 1) / 2, k1: 0, k2: 0, k3: 0,
      p1: 0, p2: 0)
    let calibration = CameraCalibration(
      revision: "metadata-seed", camera_configuration_id: cameraID, width: UInt32(portrait.width),
      height: UInt32(portrait.height), model: model,
      valid_region: PixelBounds(
        min_x: 0, min_y: 0, max_x: Double(portrait.width - 1), max_y: Double(portrait.height - 1)))
    let identity = ObservationIdentity(
      observation_id: UUID().uuidString, camera_configuration_id: cameraID,
      calibration_revision: calibration.revision, exposure: exposure,
      temporal_model: .stationary_single_exposure,
      timing_provenance_id: "camera-host-uptime-wallclock-uncertainty-unknown")
    let context = EarthContext(
      observer: observer, refraction: Refraction(kind: .disabled, parameters: nil),
      eop_policy: .allow_predicted)
    let options = SolveOptions(
      acceptance: AcceptanceCriteria(
        minimum_inliers: 6, maximum_match_error_rad: 0.002, maximum_fit_rms_rad: 0.001,
        maximum_conditional_sigma_rad: nil),
      limits: SolveLimits(
        maximum_hypotheses: 50000, maximum_candidate_comparisons: 2_000_000,
        maximum_workspace_bytes: 67_108_864))
    switch try engine.solve(
      PixelObservation(
        identity: identity, width: calibration.width, height: calibration.height,
        stars: portrait.stars), calibration: calibration, context: context, options: options,
      cancel: cancel)
    {
    case .failure(let e):
      throw DemoError.message("Stars could not be associated: \(e.code.rawValue).")
    case .success(let s):
      return CameraMeasurement(
        rotation: s.R_enu_from_camera,
        provenance: optical + ";" + frame.provenance + ";EOP-" + s.models.earth_orientation_status
          + ";refraction-disabled")
    }
  }
  static func portrait(_ stars: [PixelStar], frame: CapturedFrame) throws -> (
    stars: [PixelStar], width: Int, height: Int
  ) {
    guard [1, 3, 6, 8].contains(frame.orientation) else {
      throw DemoError.message("Unsupported RAW orientation.")
    }
    let swap = frame.orientation == 6 || frame.orientation == 8
    let out = stars.map { s -> PixelStar in
      var s = s
      let x = s.pixel.x
      let y = s.pixel.y
      switch frame.orientation {
      case 3: s.pixel = Vec2(x: Double(frame.width - 1) - x, y: Double(frame.height - 1) - y)
      case 6: s.pixel = Vec2(x: Double(frame.height - 1) - y, y: x)
      case 8: s.pixel = Vec2(x: y, y: Double(frame.width - 1) - x)
      default: break
      }
      return s
    }
    return (out, swap ? frame.height : frame.width, swap ? frame.width : frame.height)
  }
  private func target(_ frame: CapturedFrame, cancel: CancellationToken) throws -> CameraMeasurement
  {
    guard frame.orientation == 1, let k = frame.intrinsics else {
      throw DemoError.message("This camera did not deliver intrinsics for target measurement.")
    }
    var ids = [Int32](repeating: 0, count: 9)
    var coordinates = [Double](repeating: 0, count: 72)
    let count = aa_demo_tags(
      frame.luma, Int32(frame.width), Int32(frame.height), Int32(frame.width), &ids, &coordinates, 9
    )
    guard count >= 2 else {
      throw DemoError.message("Show at least two target markers with space between them.")
    }
    var corners = [TargetCorner]()
    // AprilTag corner order: bottom-left, bottom-right, top-right, top-left.
    let offsets = [
      SIMD2<Double>(0, 1), SIMD2<Double>(1, 1), SIMD2<Double>(1, 0), SIMD2<Double>(0, 0),
    ]
    for i in 0..<Int(count) {
      let id = Int(ids[i])
      let origin = SIMD2(Double(id % 3) * 0.065, Double(id / 3) * 0.065)
      for j in 0..<4 {
        let p = origin + offsets[j] * 0.05
        corners.append(
          TargetCorner(
            id: WireUInt64(UInt64(id * 4 + j)), target_m: Vec2(x: p.x, y: p.y),
            pixel: Vec2(x: coordinates[i * 8 + j * 2], y: coordinates[i * 8 + j * 2 + 1])))
      }
    }
    let request = PlanarPoseRequest(
      width: UInt32(frame.width), height: UInt32(frame.height), fx_px: Double(k[0][0]),
      fy_px: Double(k[1][1]), cx_px: Double(k[2][0]), cy_px: Double(k[2][1]), corners: corners,
      maximum_rms_px: 2, ambiguity_tolerance_px: 0.25)
    let pose = try Alignment.targetPose(request, cancel: cancel)
    let a = pose.R_camera_from_target.matrix.row_major
    let transposed = [a[0], a[3], a[6], a[1], a[4], a[7], a[2], a[5], a[8]]
    return CameraMeasurement(
      rotation: Rotation3(matrix: Mat3(row_major: transposed)),
      provenance: "delivered-intrinsics;distortion-unqualified;planar-target-no-gravity")
  }
}
