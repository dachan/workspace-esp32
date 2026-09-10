// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "chatgpt-bridge",
    platforms: [
        .macOS(.v13),
    ],
    products: [
        .executable(name: "chatgpt-bridge", targets: ["chatgpt-bridge"]),
        .executable(name: "model-dial", targets: ["model-dial"]),
    ],
    targets: [
        .executableTarget(
            name: "chatgpt-bridge",
            path: "Sources"
        ),
        .executableTarget(
            name: "model-dial",
            path: "AppSources"
        ),
    ]
)
