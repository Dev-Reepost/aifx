// swift-tools-version: 5.9
// SPDX-License-Identifier: BSD-3-Clause
// Copyright AIFX contributors.
//
// SPM manifest for the AIFX macOS installer. Produces a single executable
// (AIFXInstaller) that tools/release-macos-installer.sh wraps into an
// AIFX Installer.app bundle alongside the seven embedded .ofx.bundle
// payloads.
import PackageDescription

let package = Package(
    name: "AIFXInstaller",
    platforms: [
        // The plugins themselves work down to macOS 10.13; the installer can
        // afford to require something newer because nothing else loads it.
        .macOS(.v13)
    ],
    targets: [
        .executableTarget(
            name: "AIFXInstaller",
            path: "Sources/AIFXInstaller"
        )
    ]
)
