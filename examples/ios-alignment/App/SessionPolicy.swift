import Foundation

/// Pure scheduling policy so countdown/deadline behavior is tested without cameras.
struct SessionPolicy {
  let start: Double
  static let countdown = 15.0, normal = 120.0, hard = 240.0
  func countdownRemaining(at time: Double) -> Int {
    max(0, Int(ceil(start + Self.countdown - time)))
  }
  func expired(at time: Double) -> Bool { time >= start + Self.hard }
  func cameraDeadline(at time: Double, remaining: Int, reserve: Double = 0) -> Double {
    min(
      start + Self.normal - 5,
      time + max(0, (start + Self.normal - 5 - reserve - time) / Double(max(1, remaining))))
  }
  func mayAttempt(
    at time: Double, deadline: Double, attempts: Int, accepted: Int, targetFrames: Int = 6
  ) -> Bool {
    time < deadline && !expired(at: time) && attempts < 12 && accepted < targetFrames
  }
}
