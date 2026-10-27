import Astroattitude
import XCTest

@testable import AlignmentDemo

final class SessionTests: XCTestCase {
  func testTimeBudget() {
    let p = SessionPolicy(start: 100)
    XCTAssertEqual(p.countdownRemaining(at: 100), 15)
    XCTAssertEqual(p.countdownRemaining(at: 114.9), 1)
    XCTAssertEqual(p.countdownRemaining(at: 115), 0)
    XCTAssertFalse(p.expired(at: 339.9))
    XCTAssertTrue(p.expired(at: 340))
    XCTAssertFalse(p.mayAttempt(at: 200, deadline: 220, attempts: 12, accepted: 0))
    XCTAssertFalse(p.mayAttempt(at: 200, deadline: 220, attempts: 0, accepted: 6))
    XCTAssertEqual(p.cameraDeadline(at: 116, remaining: 3), 149, accuracy: 1e-9)
  }
  func testMalformedReferencesFail() {
    XCTAssertThrowsError(try ReferenceData.convert([], now: Date()))
    XCTAssertThrowsError(
      try ReferenceData.convert(Array(repeating: Data("unsupported".utf8), count: 4), now: Date()))
  }
  func testUntrustedReferenceIntegerBounds() {
    let leap = Data("#@ -9223372036854775808\n2208988800 37\n".utf8)
    let announcement = Data("NO leap second will be introduced at the end of December 2030".utf8)
    XCTAssertThrowsError(
      try ReferenceData.convert([Data(), Data(), leap, announcement], now: Date()))
    let year = Data("NO leap second will be introduced at the end of December 9999".utf8)
    XCTAssertThrowsError(try ReferenceData.convert([Data(), Data(), Data(), year], now: Date()))
  }
  func testPortraitGeometry() throws {
    let frame = CapturedFrame(
      pixels: [], luma: [], width: 100, height: 50, orientation: 6, layout: .rggb, black: [0],
      white: [1], midpoint: 0, duration: 1, intrinsics: nil, provenance: "synthetic")
    let stars = [
      PixelStar(id: 1, pixel: Vec2(x: 20, y: 10), centroid_covariance_px2: nil, flux: nil)
    ]
    let r = try MeasurementProcessor.portrait(stars, frame: frame)
    XCTAssertEqual(r.width, 50)
    XCTAssertEqual(r.height, 100)
    XCTAssertEqual(r.stars[0].pixel.x, 39)
    XCTAssertEqual(r.stars[0].pixel.y, 20)
  }

  func testSyntheticImageAdapters() throws {
    let token = try CancellationToken()
    defer { token.close() }
    _ = try SyntheticScenes.target(cancel: token)
    _ = try SyntheticScenes.stars(cancel: token)
  }
  func testMissingIntrinsicsAndFeatures() throws {
    let token = try CancellationToken()
    defer { token.close() }
    let frame = CapturedFrame(
      pixels: [], luma: [], width: 100, height: 100, orientation: 1, layout: .mono, black: [0],
      white: [1], midpoint: 1, duration: 0.01, intrinsics: nil, provenance: "synthetic")
    XCTAssertThrowsError(
      try MeasurementProcessor(resources: nil, observer: nil).process(
        frame, mode: .target, cameraID: "test", fov: 1, cancel: token))
  }
  func testStationarityAndGaps() {
    let a = (0..<50).map { (Double($0) * 0.02, SIMD3<Double>(0, 0, -1)) }
    let r = (0..<50).map { (Double($0) * 0.02, 0.0) }
    XCTAssertTrue(MotionRecorder.evaluate(acceleration: a, gyro: r)!.stationary)
    XCTAssertNil(MotionRecorder.evaluate(acceleration: Array(a.prefix(4)), gyro: r))
    XCTAssertFalse(
      MotionRecorder.evaluate(acceleration: a, gyro: r.map { ($0.0, 0.04) })!.stationary)
    XCTAssertFalse(
      MotionRecorder.evaluate(acceleration: a, gyro: r.filter { !($0.0 > 0.3 && $0.0 < 0.7) })!
        .stationary)
  }
  func testExpiredAnnouncementsFailBeforeCatalog() throws {
    let announcement = Data("NO leap second will be introduced at the end of December 2019".utf8)
    let leap = Data("#@ 4000000000\n2208988800 37\n".utf8)
    XCTAssertThrowsError(
      try ReferenceData.convert(
        [Data(), Data(), leap, announcement], now: Date(timeIntervalSince1970: 1_578_009_600)))
  }
  func testReferenceReceiptRejectsModifiedPackages() throws {
    let catalog = Data("synthetic-catalog".utf8)
    let astrometry = Data("synthetic-time".utf8)
    let manifest = Data("synthetic-provenance".utf8)
    let receipt = try ReferenceData.receipt(
      catalog: catalog, astrometry: astrometry, manifest: manifest)
    XCTAssertNoThrow(
      try ReferenceData.verifyReceipt(
        receipt, catalog: catalog, astrometry: astrometry, manifest: manifest))
    XCTAssertThrowsError(
      try ReferenceData.verifyReceipt(
        receipt, catalog: catalog + Data([0]), astrometry: astrometry, manifest: manifest))
    XCTAssertThrowsError(
      try ReferenceData.verifyReceipt(
        receipt, catalog: catalog, astrometry: astrometry, manifest: Data()))
    for address in ReferenceData.urls {
      let url = try XCTUnwrap(URL(string: address))
      XCTAssertEqual(url.scheme, "https")
      XCTAssertNil(url.query)
      XCTAssertNil(url.user)
    }
  }
}
