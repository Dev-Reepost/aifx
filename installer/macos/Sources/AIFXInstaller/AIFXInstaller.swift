// SPDX-License-Identifier: BSD-3-Clause
// Copyright AIFX contributors.
//
// AIFX macOS installer: a small SwiftUI wizard that takes the user through
//   1. welcome / version banner
//   2. install location (per-user vs system-wide)
//   3. site configuration (ComfyUI server URL + the two mount-path views)
//   4. install (rewrite each bundle's defaults.json, copy into place,
//      clear macOS quarantine)
//   5. done / open the OFX directory in Finder
//
// The seven .ofx.bundle payloads are embedded by tools/release-macos-installer.sh
// into Contents/Resources/Bundles/ before code-signing. At runtime they live at
// Bundle.main.resourcePath/Bundles/<Target>.ofx.bundle and are streamed to the
// chosen install directory by InstallerEngine.

import SwiftUI
import AppKit

// =============================================================================
// MARK: - Site config model
//
// The wizard collects these values from the user and writes them back into each
// embedded bundle's Contents/Resources/config/defaults.json before the bundle
// lands in the OFX plugin directory. The JSON schema is shared with the build-
// time merge at tools/merge-defaults.py — keep the keys aligned.
// =============================================================================

struct SiteConfig {
    /// ComfyUI server hostname or IP (e.g. "192.168.1.211" or "comfyui.local").
    var serverAddress: String = "127.0.0.1"
    /// ComfyUI HTTP port (default 8188 for stock ComfyUI; we ship 8388 for the
    /// Reepost studio convention).
    var serverPort: Int = 8188

    /// The shared-folder path **as this operator's Mac sees it** —
    /// typically `/Volumes/<share>/<project-root>`. Written into
    /// defaults.json under `server.macMountPath`.
    var clientMountPath: String = "/Volumes/silo/AIFX"

    /// The shared-folder path **as the ComfyUI server's filesystem sees it**.
    /// In our setup the server is a Windows box and this is a UNC path like
    /// `\\\\server-host\\share\\AIFX`. Written under `server.winMountPath`
    /// (the schema still uses that key for v0.1.x — see RELEASE_SPEC notes
    /// on the planned schema rename to `serverMountPath` in v0.2).
    var serverMountPath: String = #"\\COMFYUI-HOST\silo\AIFX"#

    /// ComfyUI per-job timeout in seconds. 600 matches the studio default;
    /// dial higher for slow-VRAM SeedVR2 / DepthCrafter / MaMa Matting.
    var timeoutSeconds: Int = 600

    /// Whether plugins should be active out-of-the-box. We default to false
    /// so the host doesn't unexpectedly hit a misconfigured ComfyUI on the
    /// first render — operators flip the toggle in the host UI once they've
    /// verified server reachability.
    var enableProcessingByDefault: Bool = false

    /// If true, the installer will leave each bundle's stock defaults.json
    /// untouched and only copy the bundles. Useful for operators who'll
    /// configure everything in the host UI on first use.
    var keepBundledDefaults: Bool = false
}

// =============================================================================
// MARK: - Install location

enum InstallScope: String, CaseIterable, Identifiable {
    case perUser
    case systemWide
    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .perUser:    return "Per-user (~/Library/OFX/Plugins/)"
        case .systemWide: return "System-wide (/Library/OFX/Plugins/)"
        }
    }

    var targetURL: URL {
        switch self {
        case .perUser:
            let home = FileManager.default.homeDirectoryForCurrentUser
            return home.appendingPathComponent("Library/OFX/Plugins", isDirectory: true)
        case .systemWide:
            return URL(fileURLWithPath: "/Library/OFX/Plugins", isDirectory: true)
        }
    }

    /// True iff installing here requires admin (which we explicitly do NOT
    /// support in v0.1.x — installer hard-fails with a clear message).
    var requiresAdmin: Bool { self == .systemWide }
}

// =============================================================================
// MARK: - Installer engine

/// Performs the actual file operations. UI calls `install(...)` once on the
/// confirm step and observes `progress` and `log` to render feedback.
@MainActor
final class InstallerEngine: ObservableObject {

    @Published var progress: Double = 0
    @Published var log: [String] = []
    @Published var failed: Bool = false
    @Published var finished: Bool = false

    /// Hardcoded list of bundles the installer expects to find in its own
    /// Resources/Bundles/ directory. If any are missing the build is broken.
    static let expectedBundles: [String] = [
        "DepthAnything3.ofx.bundle",
        "DepthCrafter.ofx.bundle",
        "NormalCrafter.ofx.bundle",
        "SegmentationSAM3.ofx.bundle",
        "MatteMaMa.ofx.bundle",
        "MatteMA2.ofx.bundle",
        "UpscaleSeedVR2.ofx.bundle",
    ]

    private func append(_ line: String) { log.append(line) }

    /// Returns the directory containing the embedded .ofx.bundle payloads.
    /// In a real installer build this is Contents/Resources/Bundles/. When
    /// running `swift run` during development, the Resources dir doesn't
    /// exist and we look one level up to find a dev override.
    private func bundlesSourceURL() -> URL? {
        if let res = Bundle.main.resourceURL {
            let candidate = res.appendingPathComponent("Bundles", isDirectory: true)
            if FileManager.default.fileExists(atPath: candidate.path) {
                return candidate
            }
        }
        // Dev fallback: AIFX/installer/macos/Resources/Bundles/
        let exe = Bundle.main.executableURL?.deletingLastPathComponent()
        if let dev = exe?
            .deletingLastPathComponent()
            .deletingLastPathComponent()
            .appendingPathComponent("Resources/Bundles", isDirectory: true),
           FileManager.default.fileExists(atPath: dev.path) {
            return dev
        }
        return nil
    }

    /// Rewrite the given defaults.json on disk with the user's site config.
    /// Conservative: read → patch the known keys → write back, preserving any
    /// keys we don't touch (notably the per-plugin `project` block).
    private func patchDefaults(at url: URL, with config: SiteConfig) throws {
        let data = try Data(contentsOf: url)
        guard var root = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw NSError(domain: "AIFXInstaller", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "Malformed defaults.json at \(url.path)"])
        }

        var server  = root["server"]   as? [String: Any] ?? [:]
        var controls = root["controls"] as? [String: Any] ?? [:]

        server["serverAddress"] = config.serverAddress
        server["serverPort"]    = config.serverPort
        server["macMountPath"]  = config.clientMountPath
        // Keep the v0.1.x JSON key name (winMountPath) — semantically it's the
        // ComfyUI server's view of the shared folder. The rename to
        // serverMountPath is planned for v0.2 with a one-time migration.
        server["winMountPath"]  = config.serverMountPath
        // linuxMountPath: leave whatever was bundled. Not relevant on macOS
        // operator machines and the plugin doesn't fetch it at runtime today.

        controls["timeout"]          = config.timeoutSeconds
        controls["enableProcessing"] = config.enableProcessingByDefault

        root["server"]   = server
        root["controls"] = controls

        let out = try JSONSerialization.data(withJSONObject: root,
                                             options: [.prettyPrinted, .sortedKeys])
        try out.write(to: url, options: .atomic)
    }

    /// Copy `src` → `dst`, replacing any existing item at `dst`.
    private func replace(_ src: URL, with dst: URL) throws {
        let fm = FileManager.default
        if fm.fileExists(atPath: dst.path) {
            try fm.removeItem(at: dst)
        }
        try fm.copyItem(at: src, to: dst)
    }

    /// Strip the com.apple.quarantine xattr from `url` recursively. Best-effort;
    /// failure is logged but does not abort the install.
    private func clearQuarantine(at url: URL) {
        let task = Process()
        task.launchPath = "/usr/bin/xattr"
        task.arguments = ["-dr", "com.apple.quarantine", url.path]
        let pipe = Pipe()
        task.standardOutput = pipe
        task.standardError  = pipe
        do {
            try task.run()
            task.waitUntilExit()
        } catch {
            append("    (could not clear quarantine on \(url.lastPathComponent): \(error.localizedDescription))")
        }
    }

    /// Run the full install. Updates `progress` and `log` as it goes.
    func install(scope: InstallScope, config: SiteConfig) async {
        progress = 0
        log = []
        failed = false
        finished = false

        let fm = FileManager.default
        let target = scope.targetURL

        append("AIFX Installer starting…")
        append("  Destination: \(target.path)")
        append(config.keepBundledDefaults
               ? "  Defaults:    keeping each plugin's bundled defaults (no site override)"
               : "  Defaults:    rewriting each plugin's defaults.json with site config")

        // 1. Resolve the embedded source bundles.
        guard let src = bundlesSourceURL() else {
            append("✗ Cannot locate embedded .ofx.bundle payloads inside the installer.")
            append("  Expected at: Contents/Resources/Bundles/")
            failed = true
            return
        }
        append("  Source:      \(src.path)")

        // 2. Make sure all 7 are actually present (the installer is broken if not).
        for name in Self.expectedBundles {
            let path = src.appendingPathComponent(name)
            if !fm.fileExists(atPath: path.path) {
                append("✗ Missing embedded bundle: \(name)")
                failed = true
                return
            }
        }

        // 3. Ensure the destination directory exists. For the per-user case
        //    we can create it ourselves; for system-wide we can't (no admin
        //    privilege in this process) — abort with a clean message.
        if !fm.fileExists(atPath: target.path) {
            if scope.requiresAdmin {
                append("✗ \(target.path) does not exist.")
                append("  System-wide install in v0.1.x requires the directory to already exist.")
                append("  Run: sudo mkdir -p \(target.path) && sudo chmod 755 \(target.path)")
                append("  Or re-run the installer and pick the per-user option.")
                failed = true
                return
            }
            do {
                try fm.createDirectory(at: target, withIntermediateDirectories: true)
                append("  Created destination directory.")
            } catch {
                append("✗ Could not create \(target.path): \(error.localizedDescription)")
                failed = true
                return
            }
        }

        // 4. For each bundle: copy → patch defaults.json → clear quarantine.
        let total = Double(Self.expectedBundles.count)
        for (i, name) in Self.expectedBundles.enumerated() {
            let srcBundle = src.appendingPathComponent(name)
            let dstBundle = target.appendingPathComponent(name)
            append("[\(i+1)/\(Self.expectedBundles.count)] \(name)")

            do {
                try replace(srcBundle, with: dstBundle)
            } catch {
                append("    ✗ copy failed: \(error.localizedDescription)")
                if (error as NSError).code == NSFileWriteNoPermissionError && scope.requiresAdmin {
                    append("      System-wide install needs admin rights; this version of the installer")
                    append("      does not elevate. Re-run with the per-user option.")
                }
                failed = true
                return
            }

            if !config.keepBundledDefaults {
                let defaults = dstBundle
                    .appendingPathComponent("Contents/Resources/config/defaults.json")
                if fm.fileExists(atPath: defaults.path) {
                    do {
                        try patchDefaults(at: defaults, with: config)
                        append("    site config written")
                    } catch {
                        append("    ✗ could not patch defaults.json: \(error.localizedDescription)")
                        failed = true
                        return
                    }
                } else {
                    append("    (no defaults.json in this bundle — skipping site override)")
                }
            }

            clearQuarantine(at: dstBundle)
            progress = Double(i + 1) / total
        }

        append("")
        append("✓ Done. All 7 plugins installed at \(target.path).")
        append("  Restart your OFX host. Plugins appear under the AIFX category.")
        finished = true
    }
}

// =============================================================================
// MARK: - Wizard state

enum WizardStep: Int, CaseIterable {
    case welcome, location, siteConfig, confirm, installing, done
}

@MainActor
final class WizardState: ObservableObject {
    @Published var step: WizardStep = .welcome
    @Published var scope: InstallScope = .perUser
    @Published var config: SiteConfig = .init()
    @Published var engine = InstallerEngine()

    /// Version string baked in by the release script via Info.plist.
    var version: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev"
    }

    func go(_ next: WizardStep) { step = next }
    func back() {
        if let prev = WizardStep(rawValue: max(step.rawValue - 1, 0)) { step = prev }
    }

    /// Light validation before we let the user advance past site config.
    var siteConfigValid: Bool {
        if config.keepBundledDefaults { return true }
        return !config.serverAddress.trimmingCharacters(in: .whitespaces).isEmpty
            && config.serverPort > 0 && config.serverPort < 65536
            && !config.clientMountPath.trimmingCharacters(in: .whitespaces).isEmpty
            && !config.serverMountPath.trimmingCharacters(in: .whitespaces).isEmpty
    }
}

// =============================================================================
// MARK: - Views

struct WelcomeView: View {
    @EnvironmentObject var state: WizardState
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("AIFX \(state.version)")
                .font(.system(size: 28, weight: .bold))
            Text("Seven OpenFX plugins bridging your DCC to a ComfyUI model server.")
                .font(.title3)
                .foregroundStyle(.secondary)
            Divider().padding(.vertical, 4)
            Text("This installer will:")
                .font(.headline)
            VStack(alignment: .leading, spacing: 6) {
                Label("Copy the seven .ofx.bundle directories to your OFX plugin folder.", systemImage: "folder.badge.plus")
                Label("Bake your site's ComfyUI server URL and shared-folder paths into each bundle's defaults.", systemImage: "gearshape")
                Label("Clear macOS quarantine so your host loads them without warnings.", systemImage: "lock.open")
            }
            .font(.system(.body))
            Spacer()
            Text("Plugins are functional but pre-release. Configure for your network on the next steps.")
                .font(.callout)
                .foregroundStyle(.tertiary)
        }
    }
}

struct LocationView: View {
    @EnvironmentObject var state: WizardState
    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("Install location").font(.title2.bold())
            Text("Where should the plugin bundles be installed?")
                .foregroundStyle(.secondary)
            Picker("", selection: $state.scope) {
                ForEach(InstallScope.allCases) { s in Text(s.displayName).tag(s) }
            }
            .pickerStyle(.radioGroup)
            .labelsHidden()

            GroupBox {
                VStack(alignment: .leading, spacing: 6) {
                    Text("Target directory:")
                        .font(.subheadline)
                        .foregroundStyle(.secondary)
                    Text(state.scope.targetURL.path)
                        .font(.system(.body, design: .monospaced))
                        .textSelection(.enabled)
                    if state.scope.requiresAdmin {
                        Text("System-wide installs require the destination directory to already exist and be writable. Create it first with:")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .padding(.top, 6)
                        Text("sudo mkdir -p \(state.scope.targetURL.path) && sudo chmod 755 \(state.scope.targetURL.path)")
                            .font(.system(.caption, design: .monospaced))
                            .textSelection(.enabled)
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(6)
            }
            Spacer()
        }
    }
}

struct SiteConfigView: View {
    @EnvironmentObject var state: WizardState
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Site configuration").font(.title2.bold())
            Text("These values are written into each plugin's defaults.json. You can change them later in your host UI on a per-plugin basis.")
                .foregroundStyle(.secondary)

            Toggle("Skip — keep each plugin's bundled defaults (configure later in the host UI)",
                   isOn: $state.config.keepBundledDefaults)
                .padding(.bottom, 4)

            Form {
                Section("ComfyUI server") {
                    TextField("Address (hostname or IP)", text: $state.config.serverAddress)
                        .disabled(state.config.keepBundledDefaults)
                    TextField("Port", value: $state.config.serverPort, format: .number)
                        .disabled(state.config.keepBundledDefaults)
                }
                Section("Shared folder paths") {
                    LabeledContent("This Mac's view") {
                        TextField("/Volumes/silo/AIFX", text: $state.config.clientMountPath)
                            .disabled(state.config.keepBundledDefaults)
                    }
                    LabeledContent("ComfyUI server's view") {
                        TextField(#"\\COMFYUI-HOST\silo\AIFX"#, text: $state.config.serverMountPath)
                            .disabled(state.config.keepBundledDefaults)
                    }
                    Text("The plugin writes EXR frames using the Mac path, then rewrites the path into the server's view before submitting to ComfyUI. The server's view is typically a Windows UNC path.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Section("Defaults") {
                    Stepper(value: $state.config.timeoutSeconds, in: 60...3600, step: 30) {
                        Text("Job timeout: \(state.config.timeoutSeconds) s")
                    }
                    .disabled(state.config.keepBundledDefaults)
                    Toggle("Enable processing by default (otherwise operators flip the toggle per clip in the host)",
                           isOn: $state.config.enableProcessingByDefault)
                        .disabled(state.config.keepBundledDefaults)
                }
            }
            .formStyle(.grouped)
            Spacer()
        }
    }
}

struct ConfirmView: View {
    @EnvironmentObject var state: WizardState
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Ready to install").font(.title2.bold())
            GroupBox("Summary") {
                VStack(alignment: .leading, spacing: 6) {
                    row("Plugins",     "7 .ofx.bundle directories")
                    row("Destination", state.scope.targetURL.path)
                    if state.config.keepBundledDefaults {
                        row("Defaults", "Keep each plugin's bundled defaults")
                    } else {
                        row("Server",       "\(state.config.serverAddress):\(state.config.serverPort)")
                        row("Mac path",     state.config.clientMountPath)
                        row("Server path",  state.config.serverMountPath)
                        row("Timeout",      "\(state.config.timeoutSeconds) s")
                        row("Processing",   state.config.enableProcessingByDefault ? "Enabled by default" : "Disabled by default")
                    }
                }
                .frame(maxWidth: .infinity, alignment: .leading)
                .padding(6)
            }
            Spacer()
            Text("Clicking Install will copy the bundles, write the configuration above, and clear macOS quarantine.")
                .font(.callout)
                .foregroundStyle(.secondary)
        }
    }

    @ViewBuilder
    private func row(_ label: String, _ value: String) -> some View {
        HStack(alignment: .firstTextBaseline, spacing: 12) {
            Text(label)
                .font(.caption)
                .foregroundStyle(.secondary)
                .frame(width: 110, alignment: .trailing)
            Text(value)
                .font(.system(.body, design: .monospaced))
                .textSelection(.enabled)
        }
    }
}

struct InstallingView: View {
    @EnvironmentObject var state: WizardState
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text(state.engine.finished ? "Installed"
                 : state.engine.failed ? "Install failed"
                 : "Installing…")
                .font(.title2.bold())
            ProgressView(value: state.engine.progress, total: 1)
                .progressViewStyle(.linear)
            ScrollViewReader { scroll in
                ScrollView {
                    VStack(alignment: .leading, spacing: 2) {
                        ForEach(Array(state.engine.log.enumerated()), id: \.offset) { i, line in
                            Text(line)
                                .font(.system(.caption, design: .monospaced))
                                .textSelection(.enabled)
                                .id(i)
                        }
                    }
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(8)
                }
                .background(Color(NSColor.textBackgroundColor))
                .clipShape(RoundedRectangle(cornerRadius: 6))
                .onChange(of: state.engine.log.count) { _ in
                    withAnimation { scroll.scrollTo(state.engine.log.count - 1, anchor: .bottom) }
                }
            }
        }
    }
}

struct DoneView: View {
    @EnvironmentObject var state: WizardState
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Installed").font(.title2.bold())
            Text("AIFX \(state.version) is installed at \(state.scope.targetURL.path).")
                .foregroundStyle(.secondary)
            Text("Restart your OFX host. Plugins appear under the **AIFX** category.")
            Spacer()
            HStack {
                Button("Reveal in Finder") {
                    NSWorkspace.shared.activateFileViewerSelecting([state.scope.targetURL])
                }
                Button("Open documentation") {
                    if let u = URL(string: "https://dev-reepost.github.io/aifx/") {
                        NSWorkspace.shared.open(u)
                    }
                }
            }
        }
    }
}

// =============================================================================
// MARK: - Wizard shell

struct WizardView: View {
    @EnvironmentObject var state: WizardState
    var body: some View {
        VStack(spacing: 0) {
            // Header
            HStack {
                Text("AIFX Installer")
                    .font(.system(size: 14, weight: .semibold))
                Spacer()
                Text(stepLabel(state.step))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }
            .padding(.horizontal, 20)
            .padding(.top, 14)
            .padding(.bottom, 10)

            // Body
            Group {
                switch state.step {
                case .welcome:    WelcomeView()
                case .location:   LocationView()
                case .siteConfig: SiteConfigView()
                case .confirm:    ConfirmView()
                case .installing: InstallingView()
                case .done:       DoneView()
                }
            }
            .padding(.horizontal, 20)

            Spacer(minLength: 0)

            // Footer buttons
            Divider()
            HStack {
                Button("Back") { state.back() }
                    .disabled(state.step == .welcome || state.step == .installing || state.step == .done)
                Spacer()
                primaryButton
            }
            .padding(.horizontal, 20)
            .padding(.vertical, 12)
        }
        .frame(minWidth: 620, minHeight: 460)
    }

    private func stepLabel(_ s: WizardStep) -> String {
        switch s {
        case .welcome:    return "1 / 5 — Welcome"
        case .location:   return "2 / 5 — Install location"
        case .siteConfig: return "3 / 5 — Site configuration"
        case .confirm:    return "4 / 5 — Confirm"
        case .installing: return "5 / 5 — Installing"
        case .done:       return "Done"
        }
    }

    @ViewBuilder
    private var primaryButton: some View {
        switch state.step {
        case .welcome:
            Button("Continue") { state.go(.location) }
                .keyboardShortcut(.defaultAction)
        case .location:
            Button("Continue") { state.go(.siteConfig) }
                .keyboardShortcut(.defaultAction)
        case .siteConfig:
            Button("Continue") { state.go(.confirm) }
                .keyboardShortcut(.defaultAction)
                .disabled(!state.siteConfigValid)
        case .confirm:
            Button("Install") {
                state.go(.installing)
                Task { await state.engine.install(scope: state.scope, config: state.config) }
            }
            .keyboardShortcut(.defaultAction)
        case .installing:
            if state.engine.failed {
                Button("Close") { NSApp.terminate(nil) }
            } else if state.engine.finished {
                Button("Continue") { state.go(.done) }
                    .keyboardShortcut(.defaultAction)
            } else {
                Button("Continue") {}.disabled(true)
            }
        case .done:
            Button("Close") { NSApp.terminate(nil) }
                .keyboardShortcut(.defaultAction)
        }
    }
}

// =============================================================================
// MARK: - App entry

@main
struct AIFXInstallerApp: App {
    @StateObject private var state = WizardState()
    var body: some Scene {
        WindowGroup("AIFX Installer") {
            WizardView()
                .environmentObject(state)
        }
        .windowResizability(.contentSize)
    }
}
