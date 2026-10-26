import Foundation
import Astroattitude
// Offline guide. Acquisition and fresh validation must be provided by an adapter.
guard CommandLine.arguments.count == 4 else {
    print("Usage: calibration-guide catalog.json astrometry.json recorded-journal.json")
    exit(2)
}
func input(_ index: Int) throws -> Data { try Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[index])) }
let session = try CalibrationSession.restore(catalog: input(1), astrometry: input(2), journal: String(decoding: input(3), as: UTF8.self))
defer { session.close() }
let progress = try session.progress()
print("Stage: \(progress.stage). \(progress.admitted_frames)/\(progress.frames) frames admitted in \(progress.blocks) blocks.")
let gaps = progress.occupied_cells.filter { $0 < 6 }.count
print("Coverage: \(gaps) of 12 image regions have fewer than six source observations.")
let action = try session.nextAction()
switch action.kind {
case "PrepareValidation": print("Next: plan and freeze a fresh validation series before taking any validation exposures.")
case "FitCandidate": print("Next: fit a diagnostic candidate from the completed training and selection blocks.")
case "CaptureBurst": print("Next: collect the scheduled fixed-view block when acquisition is authorized.")
case "SaveCalibration": print("Next: request validated export; the native domain and coverage gates still apply.")
default: print("Next: review the evidence limitation: \(action.reason).")
}
print("This recorded-data example performs no acquisition. Absolute accuracy is unqualified.")
