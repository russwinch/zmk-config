// swift-tools-version:5.9
import PackageDescription

// FlasherCore is plain Foundation (it also builds and tests on Linux);
// the SwiftUI app on top of it is macOS only.
var targets: [Target] = [
    .target(name: "FlasherCore"),
    .testTarget(name: "FlasherCoreTests", dependencies: ["FlasherCore"]),
]
#if os(macOS)
targets.append(.executableTarget(name: "Flasher", dependencies: ["FlasherCore"]))
#endif

let package = Package(
    name: "Flasher",
    platforms: [.macOS(.v13)],
    targets: targets
)
