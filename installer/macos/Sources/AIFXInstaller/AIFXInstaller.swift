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
    /// ComfyUI server hostname or IP (e.g. "comfyui.example.local" or "127.0.0.1").
    var serverAddress: String = "127.0.0.1"
    /// ComfyUI HTTP port (8188 is the stock ComfyUI default).
    var serverPort: Int = 8188

    /// The shared-folder path **as this operator's Mac sees it** —
    /// typically `/Volumes/<share>/<project-root>`. Written into
    /// defaults.json under `storage.localMountPath.macos` — the key the
    /// plugin actually reads at runtime (comfyui_base_plugin.cpp).
    var clientMountPath: String = "/Volumes/silo/AIFX"

    /// The shared-folder path **as the ComfyUI server's filesystem sees it**.
    /// In our setup the server is a Windows box and this is a UNC path like
    /// `\\\\server-host\\share\\AIFX`. Written under `storage.serverMountPath`.
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
        case .systemWide: return "System-wide (/Library/OFX/Plugins/) — required for Flame / Flare"
        }
    }

    /// One-line explanation shown under the picker.
    var rationale: String {
        switch self {
        case .perUser:
            return "No admin password needed. Works with Nuke, Resolve, Fusion — but "
                 + "Autodesk Flame and Flare do NOT scan this directory and will not "
                 + "see the plugins."
        case .systemWide:
            return "Every OFX host scans this directory, including Flame and Flare. "
                 + "macOS will ask for an administrator password once."
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

    /// True iff writing here needs elevated privileges. The installer now
    /// handles this itself (one authorisation prompt, see installPrivileged),
    /// rather than refusing and telling the operator to run mkdir by hand.
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
    /// `nonisolated static` so stageOne() can call it off the main actor.
    nonisolated static func patchDefaultsFile(at url: URL, with config: SiteConfig) throws {
        let data = try Data(contentsOf: url)
        guard var root = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
            throw NSError(domain: "AIFXInstaller", code: 1,
                          userInfo: [NSLocalizedDescriptionKey: "Malformed defaults.json at \(url.path)"])
        }

        var server   = root["server"]   as? [String: Any] ?? [:]
        var storage  = root["storage"]  as? [String: Any] ?? [:]
        var controls = root["controls"] as? [String: Any] ?? [:]

        server["serverAddress"] = config.serverAddress
        server["serverPort"]    = config.serverPort

        // Mount paths live under "storage" — these are the keys the plugin
        // reads at runtime (comfyui_base_plugin.cpp reads
        // storage.localMountPath.<os> and storage.serverMountPath). An earlier
        // build of this installer wrote server.macMountPath / server.winMountPath,
        // which the plugin ignores; do not resurrect those keys.
        var localMountPath = storage["localMountPath"] as? [String: Any] ?? [:]
        localMountPath["macos"] = config.clientMountPath   // this Mac's view
        storage["localMountPath"]  = localMountPath
        storage["serverMountPath"] = config.serverMountPath // ComfyUI server's view

        controls["timeout"]          = config.timeoutSeconds
        controls["enableProcessing"] = config.enableProcessingByDefault

        root["server"]   = server
        root["storage"]  = storage
        root["controls"] = controls

        let out = try JSONSerialization.data(withJSONObject: root,
                                             options: [.prettyPrinted, .sortedKeys])
        try out.write(to: url, options: .atomic)
    }

    /// Run the full install. Updates `progress` and `log` as it goes.
    ///
    /// Two-phase by design:
    ///
    ///   1. **Stage** every bundle into a temp directory — copy, rewrite
    ///      defaults.json, strip quarantine. All unprivileged, all off the main
    ///      actor.
    ///   2. **Install** the staged set into the target in one move. Per-user is
    ///      a plain copy; system-wide goes through a single authorisation
    ///      prompt (see installPrivileged).
    ///
    /// Staging first means a failure part-way through patching cannot leave a
    /// half-updated plugin directory behind, and it reduces the privileged step
    /// to one auditable command instead of one prompt per bundle.
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

        // App Translocation: macOS runs a still-quarantined .app from a random
        // read-only mount under /private/var/folders/.../AppTranslocation/,
        // with its own confined temp directory. Nothing here fails because of
        // it any more, but it means the operator launched straight from the DMG
        // and the app is not where they think it is -- worth saying out loud,
        // because it makes every path in this log look wrong.
        if src.path.contains("/AppTranslocation/") {
            append("  Note:        running translocated (launched from the DMG, still quarantined).")
            append("               To run it from a normal path, copy it out first:")
            append("                 cp -R '/Volumes/AIFX <version>/AIFX Installer.app' /Applications/")
            append("                 find '/Applications/AIFX Installer.app' -exec xattr -d com.apple.quarantine {} \\; 2>/dev/null")
        }

        // 2. Make sure all 7 are actually present (the installer is broken if not).
        for name in Self.expectedBundles {
            let path = src.appendingPathComponent(name)
            if !fm.fileExists(atPath: path.path) {
                append("✗ Missing embedded bundle: \(name)")
                failed = true
                return
            }
        }

        // 3. Stage into /tmp -- NOT the app's own temp directory.
        //
        // This used to use FileManager's .itemReplacementDirectory, which lands
        // in $TMPDIR/TemporaryItems/NSIRD_<app>_<random>/: the per-user, per-app
        // confined temp directory. Staging runs as the operator and wrote there
        // happily; the install then runs as root, through AppleScript's `with
        // administrator privileges`, in a different security context -- and
        // reading back out of that directory is refused:
        //
        //   ditto: .../NSIRD_AIFX Installer_Ttv8NB/AIFX-staging/
        //          DepthAnything3.ofx.bundle: Operation not permitted
        //
        // EPERM, not EACCES: this is the sandbox refusing the path, not file
        // modes, so being root does not help. App Translocation compounds it --
        // a quarantined .app launched from the DMG runs from a random read-only
        // mount with its own confined $TMPDIR, which is what the operator's log
        // showed.
        //
        // /tmp is world-traversable, outside every app container and every
        // translocation mount, and readable by root whichever context created
        // it. The directory is a fresh UUID created with
        // withIntermediateDirectories:false, so an existing path (or a planted
        // symlink) makes this fail rather than being silently reused.
        let stagingRoot = URL(fileURLWithPath: "/tmp", isDirectory: true)
            .appendingPathComponent("aifx-install-\(UUID().uuidString)", isDirectory: true)
        let staging = stagingRoot.appendingPathComponent("AIFX-staging", isDirectory: true)
        do {
            try fm.createDirectory(at: stagingRoot, withIntermediateDirectories: false,
                                   attributes: [.posixPermissions: 0o755])
            try fm.createDirectory(at: staging, withIntermediateDirectories: false,
                                   attributes: [.posixPermissions: 0o755])
        } catch {
            append("✗ Could not create a staging directory: \(error.localizedDescription)")
            failed = true
            return
        }
        defer { try? FileManager.default.removeItem(at: stagingRoot) }

        let bundles = Self.expectedBundles
        let total = Double(bundles.count)

        for (i, name) in bundles.enumerated() {
            append("[\(i+1)/\(bundles.count)] \(name)")

            // Off the main actor: awaiting a detached task suspends this one, so
            // SwiftUI gets to redraw between bundles. Doing the file work inline
            // on @MainActor blocked every UI update until the whole install
            // finished, which is why the progress bar sat at 0% throughout.
            let lines: [String]
            do {
                let s = src.appendingPathComponent(name)
                let d = staging.appendingPathComponent(name)
                lines = try await Task.detached(priority: .userInitiated) {
                    try Self.stageOne(from: s, to: d, config: config)
                }.value
            } catch {
                append("    ✗ \(error.localizedDescription)")
                failed = true
                return
            }
            lines.forEach(append)

            // Staging is 80% of the visible work; the copy into place is the rest.
            progress = 0.8 * Double(i + 1) / total
        }

        // 4. Move the staged set into the target.
        append("")
        append("Installing into \(target.path)…")
        if scope.requiresAdmin {
            append("  macOS will ask for an administrator password.")
        }

        do {
            let staged = staging
            let dest = target
            let elevate = scope.requiresAdmin
            let names = bundles
            try await Task.detached(priority: .userInitiated) {
                try Self.installStaged(from: staged, to: dest, names: names, elevated: elevate)
            }.value
        } catch {
            append("✗ Install failed: \(error.localizedDescription)")
            if scope.requiresAdmin {
                append("  If you cancelled the password prompt, re-run the installer.")
                append("  Otherwise copy the bundles yourself, from a terminal:")
                append("    sudo mkdir -p \(target.path)")
                append("    sudo ditto <this app>/Contents/Resources/Bundles/ \(target.path)/")
            }
            failed = true
            return
        }

        progress = 1
        append("")
        append("✓ Done. All \(bundles.count) plugins installed at \(target.path).")
        append("  Restart your OFX host. Plugins appear under the AIFX category.")
        finished = true
    }

    // -------------------------------------------------------------------------
    // MARK: Off-main-actor workers
    //
    // `nonisolated static` so they can run inside Task.detached without touching
    // any @Published state. They return their log lines instead of appending, so
    // the caller can hop back to the main actor to publish them.
    // -------------------------------------------------------------------------

    /// Copy one bundle into the staging dir, rewrite its defaults.json, and
    /// strip quarantine. Throws on anything that would produce a broken plugin.
    nonisolated static func stageOne(from src: URL, to dst: URL, config: SiteConfig) throws -> [String] {
        var lines: [String] = []
        let fm = FileManager.default

        if fm.fileExists(atPath: dst.path) { try fm.removeItem(at: dst) }
        try fm.copyItem(at: src, to: dst)

        if !config.keepBundledDefaults {
            let defaults = dst.appendingPathComponent("Contents/Resources/config/defaults.json")
            if fm.fileExists(atPath: defaults.path) {
                try patchDefaultsFile(at: defaults, with: config)
                lines.append("    site config written")
            } else {
                lines.append("    (no defaults.json in this bundle — skipping site override)")
            }
        }

        // Best-effort: a bundle that keeps the quarantine flag still loads in
        // every OFX host we target, so this must not abort the install.
        //
        // NOT `xattr -dr`: macOS 26 removed the -r flag, so that invocation
        // exits 64 having cleared nothing — and because this call is
        // best-effort, it failed silently. find(1) supplies the recursion.
        _ = try? run("/usr/bin/find", [dst.path, "-exec", "/usr/bin/xattr",
                                       "-d", "com.apple.quarantine", "{}", ";"])
        return lines
    }

    /// Put the staged bundles into their final home, replacing any earlier
    /// install of the same plugin.
    ///
    /// Elevated installs go through one `osascript` invocation: AppleScript's
    /// `with administrator privileges` is the supported way for an unsigned,
    /// non-sandboxed app to obtain the standard macOS authorisation dialog
    /// without shipping a privileged helper tool. The command is built from a
    /// fixed template with only the paths interpolated, and every path is
    /// single-quoted, so a space or an apostrophe in a path cannot break out.
    nonisolated static func installStaged(from staging: URL, to target: URL,
                                          names: [String], elevated: Bool) throws {
        let fm = FileManager.default

        if !elevated {
            if !fm.fileExists(atPath: target.path) {
                try fm.createDirectory(at: target, withIntermediateDirectories: true)
            }
            for name in names {
                let dst = target.appendingPathComponent(name)
                if fm.fileExists(atPath: dst.path) { try fm.removeItem(at: dst) }
                try fm.copyItem(at: staging.appendingPathComponent(name), to: dst)
            }
            return
        }

        var cmd = "/bin/mkdir -p \(shellQuote(target.path)) && /bin/chmod 755 \(shellQuote(target.path))"
        for name in names {
            let dst = target.appendingPathComponent(name).path
            let stg = staging.appendingPathComponent(name).path
            cmd += " && /bin/rm -rf \(shellQuote(dst))"
            cmd += " && /usr/bin/ditto \(shellQuote(stg)) \(shellQuote(dst))"
        }
        // Leave the bundles readable by everyone; the host runs as the operator,
        // not as root.
        cmd += " && /bin/chmod -R go+rX \(shellQuote(target.path))"

        try runPrivileged(cmd)
    }

    /// POSIX single-quote escaping: wrap in single quotes and replace any
    /// embedded quote with the '\'' idiom.
    nonisolated static func shellQuote(_ s: String) -> String {
        "'" + s.replacingOccurrences(of: "'", with: "'\\''") + "'"
    }

    /// Run a shell command as root via the standard macOS authorisation dialog.
    nonisolated static func runPrivileged(_ command: String) throws {
        // AppleScript string literal: backslashes and double quotes must be escaped.
        let escaped = command
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "\"", with: "\\\"")
        let script = "do shell script \"\(escaped)\" with administrator privileges"

        var error: NSDictionary?
        guard let appleScript = NSAppleScript(source: script) else {
            throw InstallError.message("Could not build the authorisation script.")
        }
        appleScript.executeAndReturnError(&error)
        if let error {
            // -128 is userCancelledErr: the operator dismissed the password dialog.
            let code = (error[NSAppleScript.errorNumber] as? Int) ?? 0
            if code == -128 {
                throw InstallError.message("Authorisation cancelled.")
            }
            let msg = (error[NSAppleScript.errorMessage] as? String) ?? "unknown error"
            throw InstallError.message("Privileged install failed: \(msg)")
        }
    }

    /// Minimal process runner for the staging phase.
    @discardableResult
    nonisolated static func run(_ launchPath: String, _ arguments: [String]) throws -> Int32 {
        let task = Process()
        task.executableURL = URL(fileURLWithPath: launchPath)
        task.arguments = arguments
        let pipe = Pipe()
        task.standardOutput = pipe
        task.standardError  = pipe
        try task.run()
        task.waitUntilExit()
        return task.terminationStatus
    }
}

enum InstallError: LocalizedError {
    case message(String)
    var errorDescription: String? {
        switch self { case .message(let m): return m }
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
    // System-wide by default: Autodesk Flame and Flare only scan
    // /Library/OFX/Plugins, so a per-user install leaves them invisible -- an
    // operator following the defaults got a "successful" install and no plugins
    // in the host. Per-user remains available for hosts that read it and for
    // machines where the operator has no admin rights.
    @Published var scope: InstallScope = .systemWide
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
                    Text(state.scope.rationale)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                        .fixedSize(horizontal: false, vertical: true)
                        .padding(.top, 6)
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
