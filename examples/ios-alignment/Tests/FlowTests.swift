import XCTest

final class FlowTests: XCTestCase {
  func testStarsCountdownAndResults() {
    let app = XCUIApplication()
    app.launchArguments = ["--synthetic"]
    app.launch()
    app.switches["Full alignment with guided tilts"].tap()
    app.buttons["Prepare"].tap()
    app.buttons["Start"].tap()
    XCTAssertTrue(app.staticTexts["countdown"].waitForExistence(timeout: 2))
    XCTAssertTrue(app.staticTexts["results"].waitForExistence(timeout: 30))
    XCTAssertTrue(app.buttons["Export result…"].exists)
  }
  func testTargetAndCancellation() {
    let app = XCUIApplication()
    app.launchArguments = ["--synthetic"]
    app.launch()
    app.switches["Full alignment with guided tilts"].tap()
    app.buttons["Printed target"].tap()
    app.buttons["Prepare"].tap()
    app.buttons["Start"].tap()
    app.buttons["Cancel"].tap()
    XCTAssertFalse(app.buttons["Cancel"].exists)
    app.buttons["Start"].tap()
    XCTAssertTrue(app.staticTexts["results"].waitForExistence(timeout: 30))
    XCTAssertTrue(
      app.staticTexts["Unavailable: the target has no gravity reference in one pose."].firstMatch
        .exists)
  }
  func testGuidedTilts() {
    let app = XCUIApplication()
    app.launchArguments = ["--synthetic"]
    app.launch()
    app.buttons["Prepare"].tap()
    app.buttons["Start"].tap()
    for _ in 0..<2 {
      XCTAssertTrue(app.buttons["Ready for next pose"].waitForExistence(timeout: 50))
      app.buttons["Ready for next pose"].tap()
    }
    XCTAssertTrue(app.staticTexts["rotation estimated"].firstMatch.waitForExistence(timeout: 30))
  }
  func testGuidedTargetTilts() {
    let app = XCUIApplication()
    app.launchArguments = ["--synthetic"]
    app.launch()
    app.buttons["Printed target"].tap()
    app.buttons["Prepare"].tap()
    app.buttons["Start"].tap()
    for _ in 0..<3 {
      XCTAssertTrue(app.buttons["Ready for next pose"].waitForExistence(timeout: 50))
      app.buttons["Ready for next pose"].tap()
    }
    XCTAssertTrue(app.staticTexts["rotation estimated"].firstMatch.waitForExistence(timeout: 30))
  }
}
