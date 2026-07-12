// swift-tools-version: 5.10
import PackageDescription

let package = Package(
    name: "CueletMacOS",
    platforms: [
        .macOS(.v14)
    ],
    products: [
        .executable(name: "Cuelet", targets: ["Cuelet"])
    ],
    targets: [
        .executableTarget(
            name: "Cuelet",
            path: "Cuelet",
            exclude: [
                "Resources/IconExportREADME.md"
            ],
            resources: [
                .process("Resources")
            ]
        ),
        .testTarget(
            name: "CueletTests",
            dependencies: ["Cuelet"],
            path: "CueletTests"
        )
    ]
)
