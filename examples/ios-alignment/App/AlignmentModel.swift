import AVFoundation
import Astroattitude
import CoreLocation
import SwiftUI

@MainActor
final class AlignmentModel: NSObject, ObservableObject, @preconcurrency CLLocationManagerDelegate {
  @Published var mode = SceneMode.stars
  @Published var status = "Choose a scene, then prepare the measurement."
  @Published var ready = false
  @Published var running = false
  @Published var preparing = false
  @Published var countdown: Int?
  @Published var guidedRepositioning = true
  @Published var waitingForPose = false
  @Published var poseNumber = 1
  @Published var previewReady = false
  private var poses = [AlignmentRequest]()
  private var poseActive = false
  private var fullReportReady = false
  private var plannedPoses: Int { guidedRepositioning ? (mode == .stars ? 3 : 4) : 1 }
  @Published var report: AlignmentReport?
  @Published var reasons: [String: String] = [:]
  @Published var exportURL: URL?
  let synthetic: Bool
  let rig = CaptureRig()
  private let motion = MotionRecorder(), tone = Tone(), location = CLLocationManager()
  private var cameras: [AVCaptureDevice] = [], resources: SkyResources?,
    position: ObserverPosition?, processor: MeasurementProcessor?
  private var frames = [AlignmentFrame](), token: CancellationToken?, task: Task<Void, Never>?,
    watchdog: Task<Void, Never>?
  private var idleWasDisabled = false, measurementStart = 0.0, policy: SessionPolicy?,
    cameraIDs = [String](), sessionID = UUID()
  private var observers = [NSObjectProtocol]()
  override init() {
    #if targetEnvironment(simulator)
      synthetic = true
    #else
      synthetic = ProcessInfo.processInfo.arguments.contains("--synthetic")
    #endif
    super.init()
    location.delegate = self
    observers.append(
      NotificationCenter.default.addObserver(
        forName: UIApplication.didEnterBackgroundNotification, object: nil, queue: .main
      ) { [weak self] _ in
        Task { @MainActor in self?.finish("Stopped because the app entered the background.") }
      })
    observers.append(
      NotificationCenter.default.addObserver(
        forName: ProcessInfo.thermalStateDidChangeNotification, object: nil, queue: .main
      ) { [weak self] _ in
        Task { @MainActor in
          if ProcessInfo.processInfo.thermalState == .serious
            || ProcessInfo.processInfo.thermalState == .critical
          {
            self?.finish("Stopped because the phone is too warm.")
          }
        }
      })
    observers.append(
      NotificationCenter.default.addObserver(
        forName: .AVCaptureSessionWasInterrupted, object: nil, queue: .main
      ) { [weak self] _ in
        Task { @MainActor in self?.finish("Camera interrupted. Results are partial.") }
      })
  }
  var cameraNames: [String] {
    synthetic ? ["Wide", "Telephoto"] : cameras.map { $0.localizedName }
  }
  func prepare() async {
    guard !preparing && !running else { return }
    preparing = true
    ready = false
    defer { preparing = false }
    do {
      if synthetic {
        cameraIDs = ["wide", "tele"]
        ready = true
        status = "Synthetic simulator demonstration. No physical measurements."
        return
      }
      guard await AVCaptureDevice.requestAccess(for: .video) else {
        throw DemoError.message("Allow camera access in Settings.")
      }
      cameras = CaptureRig.cameras()
      cameraIDs = cameras.map { $0.uniqueID }
      guard !cameras.isEmpty else {
        throw DemoError.message("No physical rear cameras are available.")
      }
      if mode == .stars {
        if location.authorizationStatus == .notDetermined {
          location.requestWhenInUseAuthorization()
          status = "Allow location access, then prepare again."
          return
        }
        guard [.authorizedAlways, .authorizedWhenInUse].contains(location.authorizationStatus)
        else {
          throw DemoError.message(
            "Stars need your local observer position. Enable location access or use Printed target."
          )
        }
        location.desiredAccuracy = kCLLocationAccuracyHundredMeters
        location.requestLocation()
        for _ in 0..<30 {
          if position != nil { break }
          try await Task.sleep(nanoseconds: 100_000_000)
        }
        guard position != nil else {
          throw DemoError.message("Waiting for a current location. Prepare again shortly.")
        }
        do { resources = try await Task.detached { try ReferenceData.cached() }.value } catch {
          throw DemoError.message("Download current sky references, then prepare again.")
        }
      }
      let selectedResources = mode == .stars ? resources : nil
      let selectedPosition = position
      processor = try await Task.detached {
        try MeasurementProcessor(resources: selectedResources, observer: selectedPosition)
      }.value
      let sensorStart = ProcessInfo.processInfo.systemUptime
      try motion.start()
      try await Task.sleep(nanoseconds: 800_000_000)
      let motionReady =
        motion.summary(from: sensorStart, to: ProcessInfo.processInfo.systemUptime) != nil
      motion.stop()
      guard motionReady else {
        throw DemoError.message(
          "Motion samples are unavailable. Check motion access and prepare again.")
      }
      guard UIApplication.shared.applicationState == .active,
        ProcessInfo.processInfo.thermalState != .serious,
        ProcessInfo.processInfo.thermalState != .critical
      else { throw DemoError.message("Return to the app and let the phone cool before preparing.") }
      rig.beginSession()
      try await rig.configure(cameras[0], stars: mode == .stars)
      previewReady = true
      ready = true
      status =
        mode == .stars
        ? "Point the rear lenses at a clear star field. Keep the phone fixed after the countdown."
        : "Show the flat printed target to every lens. Tilt the target slightly to avoid a frontal ambiguity. No leveling is needed."
    } catch { status = DemoError.describe(error) }
  }
  func download() async {
    guard !preparing && !running else { return }
    preparing = true
    status = "Downloading public sky references. No measurements are sent."
    defer { preparing = false }
    do {
      resources = try await Task.detached { try await ReferenceData.download() }.value
      status = "References ready. Press Prepare."
    } catch { status = DemoError.describe(error) }
  }
  func locationManager(_ manager: CLLocationManager, didUpdateLocations locations: [CLLocation]) {
    if let p = locations.last, p.horizontalAccuracy >= 0, p.horizontalAccuracy <= 1000,
      p.verticalAccuracy >= 0,
      abs(p.timestamp.timeIntervalSinceNow) < 120
    {
      position = ObserverPosition(
        latitude_rad: p.coordinate.latitude * .pi / 180,
        longitude_rad: p.coordinate.longitude * .pi / 180, ellipsoid_height_m: p.ellipsoidalAltitude
      )
    }
  }
  func locationManager(_ manager: CLLocationManager, didFailWithError error: Error) {
    status = "Location unavailable. Try Prepare again with a clear view of the sky."
  }
  func resetPreparation() {
    rig.stop()
    previewReady = false
    position = nil
    ready = false
    report = nil
  }
  func start() {
    guard ready && !running else { return }
    guard
      ProcessInfo.processInfo.thermalState != .serious
        && ProcessInfo.processInfo.thermalState != .critical
    else {
      status = "Let the phone cool before starting."
      return
    }
    previewReady = !synthetic
    running = true
    report = nil
    reasons = [:]
    frames = []
    poses = []
    poseActive = false
    fullReportReady = false
    waitingForPose = false
    poseNumber = 1
    rig.beginSession()
    measurementStart = ProcessInfo.processInfo.systemUptime
    exportURL = nil
    sessionID = UUID()
    let id = sessionID
    let start = ProcessInfo.processInfo.systemUptime
    policy = SessionPolicy(start: start)
    idleWasDisabled = UIApplication.shared.isIdleTimerDisabled
    UIApplication.shared.isIdleTimerDisabled = true
    do {
      token = try CancellationToken()
      if !synthetic { try motion.start() }
    } catch {
      finish(DemoError.describe(error))
      return
    }
    watchdog = Task {
      try? await Task.sleep(
        nanoseconds: UInt64(
          max(0, start + SessionPolicy.hard - ProcessInfo.processInfo.systemUptime) * 1e9))
      if !Task.isCancelled && self.sessionID == id {
        self.finish("Time limit reached. Results are partial.")
      }
    }
    task = Task {
      do {
        while let p = policy, p.countdownRemaining(at: ProcessInfo.processInfo.systemUptime) > 0 {
          countdown = p.countdownRemaining(at: ProcessInfo.processInfo.systemUptime)
          status = "Position the phone on its fixed mount."
          try await Task.sleep(nanoseconds: 100_000_000)
        }
        guard running, sessionID == id else { return }
        countdown = nil
        for pose in 0..<plannedPoses {
          poseNumber = pose + 1
          if pose > 0 {
            guard let policy,
              ProcessInfo.processInfo.systemUptime < policy.start + SessionPolicy.normal - 10
            else { break }
            waitingForPose = true
            let prompts = [
              "", "Tilt the phone about 20° to the left from its starting position.",
              "Return near the starting position, then tilt about 20° upward.",
              "Keep the target fixed. Turn the phone clockwise about 25° and tilt slightly downward.",
            ]
            status =
              prompts[pose]
              + " Keep the scene visible in every lens. Secure the mount, then press Ready."
            tone.play(done: true)
            UIAccessibility.post(notification: .announcement, argument: status)
            while waitingForPose && running { try await Task.sleep(nanoseconds: 100_000_000) }
            try Task.checkCancellation()
            for second in stride(from: 5, through: 1, by: -1) {
              countdown = second
              try await Task.sleep(nanoseconds: 1_000_000_000)
            }
            countdown = nil
          }
          guard running, sessionID == id else { return }
          tone.play(done: false)
          UIAccessibility.post(
            notification: .announcement,
            argument: "Measuring pose \(pose+1). Do not move the phone.")
          try await Task.sleep(nanoseconds: 1_000_000_000)
          measurementStart = ProcessInfo.processInfo.systemUptime
          motion.beginMeasurement(after: measurementStart)
          frames = []
          poseActive = true
          if synthetic {
            try await syntheticPose(index: pose)
          } else {
            try await captureRun(id: id)
          }
          completePose()
        }
        if guidedRepositioning, let token, !poses.isEmpty, running {
          status = "Calculating full alignment from the stationary poses."
          let request = RigAlignmentRequest(poses: poses)
          let result = try await Task.detached {
            try Alignment.calibrateRig(request, cancel: token)
          }.value
          if running, sessionID == id {
            report = result
            fullReportReady = true
          }
        }
        if running, sessionID == id { finish("Measurement complete.") }
      } catch {
        if running, sessionID == id {
          finish(error is CancellationError ? "Measurement cancelled." : DemoError.describe(error))
        }
      }
    }
  }
  private func captureRun(id: UUID) async throws {
    guard let processor, let token, let policy else {
      throw DemoError.message("Prepare the app first.")
    }
    for (index, camera) in cameras.enumerated() {
      try Task.checkCancellation()
      guard running, sessionID == id else { return }
      let deadline = policy.cameraDeadline(
        at: ProcessInfo.processInfo.systemUptime,
        remaining: (plannedPoses - poseNumber) * cameras.count + cameras.count - index,
        reserve: Double(plannedPoses - poseNumber) * 7)
      if deadline <= ProcessInfo.processInfo.systemUptime {
        reasons[camera.uniqueID] = "Session budget exhausted."
        continue
      }
      status = "Measuring \(camera.localizedName) (\(index+1)/\(cameras.count))"
      do {
        try await rig.configure(camera, stars: mode == .stars)
        try await Task.sleep(nanoseconds: 1_500_000_000)
        try await rig.freeze(stars: mode == .stars)
        try await Task.sleep(nanoseconds: 1_000_000_000)
        var attempts = 0
        var accepted = 0
        var lastTime = -Double.infinity
        while running && sessionID == id
          && policy.mayAttempt(
            at: ProcessInfo.processInfo.systemUptime, deadline: deadline, attempts: attempts,
            accepted: accepted, targetFrames: guidedRepositioning ? 2 : 6)
        {
          attempts += 1
          do {
            let f = try await rig.frame(stars: mode == .stars)
            guard f.midpoint > lastTime else {
              try await Task.sleep(nanoseconds: 200_000_000)
              continue
            }
            lastTime = f.midpoint
            try await Task.sleep(nanoseconds: 600_000_000)
            guard
              let window = motion.summary(
                from: f.midpoint - f.duration / 2 - 0.3, to: f.midpoint + f.duration / 2 + 0.5),
              window.stationary
            else { throw DemoError.message("The phone is moving or motion samples are missing.") }
            let selectedMode = mode
            let fov = Double(camera.activeFormat.videoFieldOfView) * Double.pi / 180
            let cameraID = camera.uniqueID
            let result = try await Task.detached {
              try processor.process(
                f, mode: selectedMode, cameraID: cameraID, fov: fov, cancel: token)
            }.value
            guard running, sessionID == id,
              !policy.expired(at: ProcessInfo.processInfo.systemUptime)
            else { return }
            frames.append(
              AlignmentFrame(
                frame_id: UUID().uuidString, camera_id: cameraID,
                reference_id: selectedMode == .stars ? "ENU" : "target-grid",
                optical_provenance: result.provenance, start_s: f.midpoint - f.duration / 2,
                end_s: f.midpoint + f.duration / 2, R_reference_from_camera: result.rotation,
                R_camera_from_accelerometer_nominal: MeasurementProcessor.nominal))
            accepted += 1
            reasons[cameraID] =
              "Pose \(poseNumber): \(accepted)/\(guidedRepositioning ? 2:6) usable frames"
          } catch { reasons[camera.uniqueID] = DemoError.describe(error) }
        }
      } catch { reasons[camera.uniqueID] = DemoError.describe(error) }
    }
  }
  private func syntheticPose(index: Int) async throws {
    guard let token else { return }
    if index == 0 {
      if mode == .target {
        _ = try await Task.detached { try SyntheticScenes.target(cancel: token) }.value
      } else {
        _ = try await Task.detached { try SyntheticScenes.stars(cancel: token) }.value
      }
    }
    if guidedRepositioning {
      let request = try JSONDecoder().decode(
        RigAlignmentRequest.self, from: SyntheticScenes.data(mode == .stars ? "rig" : "rig-target"))
      poses.append(request.poses[index])
      poseActive = false
      report = try Alignment.measure(request.poses[index], cancel: token)
    } else {
      var input = try JSONDecoder().decode(
        AlignmentRequest.self, from: SyntheticScenes.data("alignment"))
      if mode == .target { input.down_reference = nil }
      report = try Alignment.measure(input, cancel: token)
      poseActive = false
    }
  }
  private func completePose() {
    guard poseActive else { return }
    poseActive = false
    let imu = motion.summary(from: measurementStart, to: ProcessInfo.processInfo.systemUptime)
    let covered =
      imu.map { sample in frames.filter { $0.start_s >= sample.start_s && $0.end_s <= sample.end_s }
      } ?? frames
    let request = AlignmentRequest(
      reference_id: mode == .stars ? "ENU" : "target-grid", clock_id: "host-uptime",
      cameras: cameraIDs, frames: covered, imu: imu,
      down_reference: mode == .stars ? Vec3(x: 0, y: 0, z: -1) : nil,
      minimum_frames: guidedRepositioning ? 2 : 6,
      motion_detected: motion.hasMoved(since: measurementStart))
    poses.append(request)
    if !fullReportReady {
      do { report = try Alignment.measure(request) } catch {
        reasons["session"] = DemoError.describe(error)
      }
    }
  }
  func continuePose() { waitingForPose = false }
  func finish(_ message: String) {
    guard running else {
      rig.stop()
      previewReady = false
      return
    }
    running = false
    task?.cancel()
    watchdog?.cancel()
    try? token?.request()
    rig.stop()
    previewReady = false
    motion.stop()
    countdown = nil
    UIApplication.shared.isIdleTimerDisabled = idleWasDisabled
    waitingForPose = false
    if !synthetic { completePose() }
    status = message
    tone.play(done: true)
    UIAccessibility.post(notification: .announcement, argument: message)
  }
  func saveResult() {
    guard let report else { return }
    do {
      let data = try JSONEncoder().encode(report)
      let file = FileManager.default.temporaryDirectory.appendingPathComponent(
        "alignment-result.json")
      try data.write(to: file, options: [.atomic, .completeFileProtection])
      exportURL = file
    } catch { status = "Could not prepare the local result file." }
  }
  func displayName(_ id: String) -> String {
    if id == "accelerometer" { return "Accelerometer" }
    return cameras.first { $0.uniqueID == id }?.localizedName ?? id.capitalized
  }
}
