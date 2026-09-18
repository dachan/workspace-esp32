// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "ai-model-control-bridge",
    platforms: [
        .macOS(.v13),
    ],
    products: [
        .executable(name: "ai-model-control-bridge", targets: ["ai-model-control-bridge"]),
        .executable(name: "model-dial", targets: ["model-dial"]),
    ],
    targets: [
        .executableTarget(
            name: "ai-model-control-bridge",
            path: "Sources"
        ),
        .executableTarget(
            name: "model-dial",
            path: "AppSources"
        ),
    ]
)
