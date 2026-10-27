import Astroattitude
import Foundation
import simd

/// Deterministic controls for simulator use; never touches capture or location.
enum SyntheticScenes {
  static func data(_ name: String) throws -> Data {
    guard
      let url = Bundle.main.url(
        forResource: name, withExtension: "json", subdirectory: "SyntheticFixtures")
    else { throw DemoError.message("Build the synthetic fixtures first.") }
    return try Data(contentsOf: url)
  }
  struct Target: Decodable {
    let width: Int, height: Int, focal: Double, luma: [UInt8]
    let expected_reference_from_camera: [Double]
  }
  static func target(cancel: CancellationToken) throws -> CameraMeasurement {
    let s = try JSONDecoder().decode(Target.self, from: data("target-scene"))
    let k = simd_float3x3(
      columns: (
        SIMD3(Float(s.focal), 0, 0), SIMD3(0, Float(s.focal), 0),
        SIMD3(Float(s.width) / 2, Float(s.height) / 2, 1)
      ))
    let frame = CapturedFrame(
      pixels: [], luma: s.luma, width: s.width, height: s.height, orientation: 1, layout: .mono,
      black: [0, 0, 0, 0], white: [1, 1, 1, 1], midpoint: 10, duration: 0.01, intrinsics: k,
      provenance: "independent-synthetic-target")
    let result = try MeasurementProcessor(resources: nil, observer: nil).process(
      frame, mode: .target, cameraID: "synthetic", fov: 1, cancel: cancel)
    guard
      zip(result.rotation.matrix.row_major, s.expected_reference_from_camera).allSatisfy({
        abs($0 - $1) < 0.02
      })
    else { throw DemoError.message("Synthetic target rotation check failed.") }
    return result
  }
  private struct Pixels: Decodable {
    let observation: PixelObservation
    let calibration: CameraCalibration
    let context: EarthContext
    let options: SolveOptions
  }
  static func stars(cancel: CancellationToken) throws -> Rotation3 {
    let s = try JSONDecoder().decode(Pixels.self, from: data("pixels"))
    let width = Int(s.observation.width)
    let height = Int(s.observation.height)
    var pixels = [Float](repeating: 100, count: width * height)
    // Independent pixel-integrated compact sources, no stored sensor image.
    for star in s.observation.stars {
      let px = star.pixel.x
      let py = star.pixel.y
      for y in max(0, Int(py) - 10)...min(height - 1, Int(py) + 10) {
        for x in max(0, Int(px) - 10)...min(width - 1, Int(px) + 10) {
          let a =
            (erf((Double(x) + 0.5 - px) / 2.828427) - erf((Double(x) - 0.5 - px) / 2.828427)) / 2
          let b =
            (erf((Double(y) + 0.5 - py) / 2.828427) - erf((Double(y) - 0.5 - py) / 2.828427)) / 2
          pixels[y * width + x] += Float(20000 * a * b)
        }
      }
    }
    let metadata = ExtractionMetadata(
      width: UInt32(width), height: UInt32(height), row_stride: UInt32(width), layout: .mono,
      black_levels: [0, 0, 0, 0], white_levels: [4095, 4095, 4095, 4095],
      options: ExtractionOptions(
        detection_sigma: 6, maximum_axis_ratio: 2.5, maximum_candidates: 128, maximum_stars: 64,
        maximum_fit_iterations: 30, maximum_pixels: 5_000_000, maximum_workspace_bytes: 268_435_456,
        maximum_work_units: 2_000_000_000))
    let extracted = try StarExtractor.extract(pixels: pixels, metadata: metadata, cancel: cancel)
    let engine = try Engine(catalog: data("catalog"), astrometry: data("astrometry"))
    defer { engine.close() }
    var observation = s.observation
    observation.stars = extracted.stars
    var options = s.options
    options.acceptance.maximum_fit_rms_rad = 0.0001
    options.acceptance.maximum_match_error_rad = 0.0002
    options.acceptance.maximum_conditional_sigma_rad = nil
    switch try engine.solve(
      observation, calibration: s.calibration, context: s.context, options: options, cancel: cancel)
    {
    case .success(let solution): return solution.R_enu_from_camera
    case .failure(let error): throw DemoError.message("Synthetic stars: \(error.detail)")
    }
  }
}
