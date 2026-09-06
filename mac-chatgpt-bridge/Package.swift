// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "chatgpt-bridge",
    platforms: [
        .macOS(.v13),
    ],
    products: [
        .executable(name: "chatgpt-bridge", targets: ["chatgpt-bridge"]),
    ],
    targets: [
        .executableTarget(
            name: "chatgpt-bridge",
            path: "Sources"
        ),
    ]
)
