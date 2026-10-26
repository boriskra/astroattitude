// swift-tools-version: 5.9
import PackageDescription
let package = Package(name: "AlignmentAdapter", platforms: [.iOS(.v16), .macOS(.v13)], products: [.library(name: "AlignmentAdapter", targets: ["AlignmentAdapter"])], targets: [.binaryTarget(name: "AlignmentAdapter", path: "Artifacts/AlignmentAdapter.xcframework")])
