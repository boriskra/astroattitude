import Astroattitude
import CoreMotion
import simd

final class MotionRecorder: @unchecked Sendable {
  private let manager = CMMotionManager(), queue = OperationQueue(), lock = NSLock()
  private var samples: [(Double, SIMD3<Double>)] = []
  private var rotationSamples: [(Double, Double)] = []
  private var moved = false
  private var anchor: SIMD3<Double>?
  func start() throws {
    guard manager.isAccelerometerAvailable, manager.isGyroAvailable else {
      throw DemoError.message("Accelerometer and gyro are required.")
    }
    queue.maxConcurrentOperationCount = 1
    manager.accelerometerUpdateInterval = 0.02
    manager.gyroUpdateInterval = 0.02
    lock.lock()
    samples = []
    rotationSamples = []
    anchor = nil
    moved = false
    lock.unlock()
    manager.startAccelerometerUpdates(to: queue) { [weak self] data, _ in
      guard let self, let d = data else { return }
      let g = SIMD3(d.acceleration.x, d.acceleration.y, d.acceleration.z)
      self.lock.lock()
      defer { self.lock.unlock() }
      self.samples.append((d.timestamp, g))
      if self.samples.count > 13000 { self.samples.removeFirst() }
    }
    manager.startGyroUpdates(to: queue) { [weak self] data, _ in
      guard let self, let d = data else { return }
      let rate = simd_length(SIMD3(d.rotationRate.x, d.rotationRate.y, d.rotationRate.z))
      self.lock.lock()
      self.rotationSamples.append((d.timestamp, rate))
      if self.rotationSamples.count > 13000 { self.rotationSamples.removeFirst() }
      self.lock.unlock()
    }
  }
  func beginMeasurement(after time: Double) {
    lock.lock()
    samples.removeAll { $0.0 < time }
    rotationSamples.removeAll { $0.0 < time }
    anchor = nil
    moved = false
    lock.unlock()
  }
  func summary(from start: Double, to end: Double) -> AlignmentImu? {
    lock.lock()
    defer { lock.unlock() }
    let a = samples.filter { $0.0 >= start && $0.0 <= end }
    let r = rotationSamples.filter { $0.0 >= start && $0.0 <= end }
    guard let result = Self.evaluate(acceleration: a, gyro: r) else { return nil }
    let mean = SIMD3(
      result.down_accelerometer.x, result.down_accelerometer.y, result.down_accelerometer.z)
    let stationary = result.stationary
    if let anchor, simd_length(simd_normalize(mean) - anchor) > 0.005 { moved = true }
    if !stationary { moved = true }
    if anchor == nil && stationary { anchor = simd_normalize(mean) }
    return result
  }
  static func evaluate(acceleration a: [(Double, SIMD3<Double>)], gyro r: [(Double, Double)])
    -> AlignmentImu?
  {
    guard a.count >= 25, r.count >= 25, let first = a.first, let last = a.last,
      last.0 - first.0 >= 0.45
    else { return nil }
    let mean = a.reduce(SIMD3<Double>.zero) { $0 + $1.1 } / Double(a.count)
    let std = sqrt(a.reduce(0) { $0 + simd_length_squared($1.1 - mean) } / Double(a.count))
    let continuous =
      zip(a, a.dropFirst()).allSatisfy { $1.0 - $0.0 < 0.15 }
      && zip(r, r.dropFirst()).allSatisfy { $1.0 - $0.0 < 0.15 }
      && abs(a.first!.0 - r.first!.0) < 0.15 && abs(a.last!.0 - r.last!.0) < 0.15
    let stationary =
      continuous && abs(simd_length(mean) - 1) < 0.08 && std < 0.015
      && r.allSatisfy { $0.1 < 0.025 }
    return AlignmentImu(
      start_s: first.0, end_s: last.0, down_accelerometer: Vec3(x: mean.x, y: mean.y, z: mean.z),
      stationary: stationary)
  }
  func hasMoved(since start: Double) -> Bool {
    lock.lock()
    defer { lock.unlock() }
    // Integrated rotation catches slow movement about gravity, which the
    // accelerometer alone cannot observe. No magnetometer/fused attitude.
    let r = rotationSamples.filter { $0.0 >= start }
    var angle = 0.0
    for (a, b) in zip(r, r.dropFirst()) { angle += max(0, b.1 - 0.003) * max(0, b.0 - a.0) }
    return moved || angle > 0.01 || r.contains { $0.1 > 0.025 }
  }
  func stop() {
    manager.stopAccelerometerUpdates()
    manager.stopGyroUpdates()
  }
}
