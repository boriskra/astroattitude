import Foundation
import Astroattitude
struct Request: Decodable { let observation: RayObservation; let context: EarthContext; let options: SolveOptions }
let path = URL(fileURLWithPath: CommandLine.arguments[1])
func data(_ name: String) throws -> Data { try Data(contentsOf: path.appendingPathComponent(name)) }
let request = try JSONDecoder().decode(Request.self, from: data("rays.json"))
let engine = try Engine(catalog: data("catalog.json"), astrometry: data("astrometry.json"))
defer { engine.close() }
switch try engine.solve(request.observation, context: request.context, options: request.options) {
case .success(let solution):
    precondition(solution.matches.last!.detection_id.value == UInt64.max)
    precondition(solution.fit_rms_rad < 1e-10)
    print("Swift synthetic solve:", solution.matches.count, "matches", solution.models.limitations)
case .failure(let error): fatalError(error.detail)
}
let token = try CancellationToken(); try token.request(); defer { token.close() }
switch try engine.solve(request.observation, context: request.context, options: request.options, cancel: token) {
case .failure(let error): precondition(error.code == .Cancelled)
case .success: fatalError("Cancellation ignored")
}
engine.close(); engine.close()
do { _ = try engine.solve(request.observation, context: request.context, options: request.options); fatalError("Closed engine accepted") } catch BridgeError.closed {}
struct PixelRequest: Decodable { let observation: PixelObservation; let calibration: CameraCalibration; let context: EarthContext; let options: SolveOptions }
let pixelRequest = try JSONDecoder().decode(PixelRequest.self, from: data("pixels.json"))
let pixelEngine = try Engine(catalog: data("catalog.json"), astrometry: data("astrometry.json"))
defer { pixelEngine.close() }
switch try pixelEngine.solve(pixelRequest.observation, calibration: pixelRequest.calibration, context: pixelRequest.context, options: pixelRequest.options) {
case .success(let solution): precondition(solution.matches.count == 8 && solution.fit_rms_rad < 1e-10); print("Swift pixel solve passed")
case .failure(let error): fatalError(error.detail)
}
struct ExtractionFixture: Decodable { let pixels:[Float]; let metadata:ExtractionMetadata; let expected:ExtractionResult }
let extraction = try JSONDecoder().decode(ExtractionFixture.self, from:data("extraction.json"))
let found = try StarExtractor.extract(pixels:extraction.pixels,metadata:extraction.metadata)
precondition(found.policy_id == extraction.expected.policy_id && found.stars.count == 1)
precondition(abs(found.stars[0].pixel.x - extraction.expected.stars[0].pixel.x) < 1e-10)
do { _ = try StarExtractor.extract(pixels:extraction.pixels,metadata:extraction.metadata,cancel:token); fatalError("Extractor ignored cancel") } catch BridgeError.resource(let e) { precondition(e.code == .Cancelled) }
let workflow = try CalibrationSession.restore(catalog:data("catalog.json"),astrometry:data("astrometry.json"),journal:String(decoding:data("workflow-training.json"),as:UTF8.self))
defer { workflow.close() }
let workflowProgress = try workflow.progress(); precondition(workflowProgress.admitted_frames == 30)
let candidate = try workflow.fit(); defer { candidate.close() }
precondition(abs(candidate.data.calibration.model.fx_px-1000)<0.01)
let plan = try workflow.proposeValidation(ValidationRequest(id:"guided",pose_id:"fixed",admission_policy_id:"synthetic-integrated",freeze_time:UtcInstant(unix_seconds:1577837000,nanoseconds:0),first_start:UtcInstant(unix_seconds:1577837010,nanoseconds:0)))
_ = try workflow.freeze(candidate,plan:plan)
let incomplete = try workflow.validate(); defer { incomplete.close() }; precondition(incomplete.data.status == "incomplete")
let finished = try CalibrationSession.restore(catalog:data("catalog.json"),astrometry:data("astrometry.json"),journal:String(decoding:data("workflow-complete.json"),as:UTF8.self))
defer { finished.close() }
let report = try finished.validate(); defer { report.close() }
let expectedReport = try JSONDecoder().decode(ValidationReport.self,from:data("workflow-report.json"))
precondition(report.data.status == "pass" && report.data.candidate_id == expectedReport.candidate_id)
finished.close(); finished.close()
do { _ = try finished.progress(); fatalError("Closed workflow accepted") } catch BridgeError.closed {}
print("Swift native workflow and extraction parity passed; absolute accuracy unqualified")
