// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "ada-recorder",
    platforms: [.macOS(.v12)],  // Required for ScreenCaptureKit
    products: [
        .executable(name: "ada-recorder", targets: ["ada-recorder"])
    ],
    targets: [
        .executableTarget(
            name: "ada-recorder",
            path: "Sources/ada-recorder",
            swiftSettings: [
                .unsafeFlags(["-parse-as-library"])
            ],
            linkerSettings: [
                .linkedFramework("AVFoundation"),
                .linkedFramework("ScreenCaptureKit"),
                .linkedFramework("CoreMedia"),
                .linkedFramework("CoreVideo"),
                .linkedFramework("VideoToolbox")
            ]
        )
    ]
)
