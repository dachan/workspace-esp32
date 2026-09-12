#if os(macOS)
import AppKit
import ApplicationServices
import Foundation
import ServiceManagement

@MainActor
final class DialController {
    var status = "Starting" { didSet { onChange?() } }
    var onChange: (() -> Void)?
    var port: String?
    var message: String? { didSet { onChange?() } }
    private(set) var isSyncing = false { didSet { onChange?() } }
    var startsAtLogin: Bool
    var bridgeEnabled: Bool { wantsBridge }
    let preferences = BridgePreferences()

    private let bridgeLogURL = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Logs/Model Dial/bridge.log")
    private var bridge: Process?
    private var monitor: Timer?
    private var wantsBridge = true
    private var stopping = false
    private var restartAt: TimeInterval = 0
    private(set) var selectedPort = UserDefaults.standard.string(forKey: "selectedSerialPort")

    var statusSummary: String {
        guard wantsBridge else { return "Bridge off" }
        if !AXIsProcessTrusted() { return "Accessibility permission needed" }
        return status
    }

    var availablePorts: [String] { ports() }

    func selectPort(_ path: String) {
        guard ports().contains(path), path != selectedPort else { return }
        selectedPort = path
        UserDefaults.standard.set(path, forKey: "selectedSerialPort")
        restartBridge()
    }

    func openAccessibilitySettings() {
        guard let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility") else { return }
        NSWorkspace.shared.open(url)
    }

    init() {
        startsAtLogin = SMAppService.mainApp.status == .enabled
    }

    func start() {
        guard monitor == nil else { return }
        monitor = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.checkPort() }
        }
        startBridge()
    }

    func shutdown() {
        wantsBridge = false
        monitor?.invalidate()
        monitor = nil
        stopBridge()
        // The app's run loop is about to stop, so its delayed shutdown fallback
        // cannot be relied on to release the child and serial port.
        if let bridge, bridge.isRunning { kill(bridge.processIdentifier, SIGKILL) }
    }

    func setBridgeEnabled(_ enabled: Bool) {
        guard enabled != wantsBridge else { return }
        wantsBridge = enabled
        if enabled {
            restartAt = 0
            status = "Starting"
            recordBridgeEvent("Bridge enabled")
            startBridge()
        } else {
            stopBridge()
            port = nil
            status = "Off"
            recordBridgeEvent("Bridge disabled")
        }
    }

    func setStartsAtLogin(_ enabled: Bool) {
        do {
            if enabled { try SMAppService.mainApp.register() }
            else { try SMAppService.mainApp.unregister() }
            startsAtLogin = SMAppService.mainApp.status == .enabled
            if SMAppService.mainApp.status == .requiresApproval {
                message = "Approve Model Dial in System Settings → General → Login Items."
                SMAppService.openSystemSettingsLoginItems()
            }
        } catch {
            startsAtLogin = SMAppService.mainApp.status == .enabled
            message = "Could not change login setting: \(error.localizedDescription)"
            let alert = NSAlert()
            alert.messageText = "Could not change Open at Login"
            alert.informativeText = error.localizedDescription
            alert.runModal()
        }
    }

    func restartBridge() {
        guard wantsBridge else { return }
        restartAt = ProcessInfo.processInfo.systemUptime + 0.2
        status = "Restarting"
        recordBridgeEvent("Bridge restart requested")
        stopBridge()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { [weak self] in
            self?.startBridge()
        }
    }

    func syncApps() {
        guard wantsBridge, !isSyncing else { return }
        isSyncing = true
        let originalMask = preferences.cursorModelMask
        Task {
            let result = await Task.detached(priority: .utility) { CursorModelSync.readMask() }.value
            defer { isSyncing = false }
            guard wantsBridge else { return }
            switch result {
            case .success(let mask):
                // A preference edit made while reading Cursor takes precedence.
                if preferences.cursorModelMask == originalMask, preferences.syncCursorModels(mask) {
                    recordBridgeEvent("Cursor models updated from Cursor")
                }
            case .failure(let error):
                recordBridgeEvent("Cursor sync: \(error.localizedDescription); saved selection retained")
            }
            recordBridgeEvent("Sync requested for the focused app")
            restartBridge()
        }
    }

    func openBridgeLog() {
        do {
            try ensureLogFile()
            let scriptURL = try bridgeLogCommandURL()
            try "#!/bin/zsh\nexec /usr/bin/tail -n 100 -F \(shellQuote(bridgeLogURL.path))\n"
                .write(to: scriptURL, atomically: true, encoding: .utf8)
            try FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: scriptURL.path)
            NSWorkspace.shared.open(scriptURL)
        } catch {
            message = "Could not open bridge log: \(error.localizedDescription)"
        }
    }

    private func checkPort() {
        onChange?()
        guard wantsBridge else { return }
        if let port, !ports().contains(port) {
            status = "Waiting for selected panel"
            stopBridge()
        }
        startBridge()
    }

    private func startBridge() {
        guard wantsBridge, bridge == nil,
              ProcessInfo.processInfo.systemUptime >= restartAt else { return }
        let available = ports()
        if selectedPort == nil, available.count == 1 {
            selectedPort = available[0]
            UserDefaults.standard.set(available[0], forKey: "selectedSerialPort")
        }
        guard let candidate = selectedPort else {
            port = nil
            status = available.isEmpty ? "Waiting for panel" : "Choose a device in Device menu"
            return
        }
        guard available.contains(candidate) else {
            port = nil
            status = "Waiting for selected panel"
            return
        }
        guard let executable = bridgeExecutable() else {
            status = "Unavailable"
            message = "The bundled chatgpt-bridge executable is missing."
            recordBridgeEvent(message!)
            return
        }
        let process = Process()
        process.executableURL = executable
        process.arguments = [
            "--watch", "--send-serial", "--port", candidate,
            "--chatgpt-effort-mask", maskArgument(preferences.chatGPTThinkingMask),
            "--cursor-model-mask", maskArgument(preferences.cursorModelMask),
        ]
        capture(process) { [weak self, weak process] text in
            guard let self, let process, self.bridge === process, !self.stopping else { return }
            self.recordBridgeOutput(text)
        }
        process.terminationHandler = { [weak self] completed in
            Task { @MainActor in
                guard let self, self.bridge === completed else { return }
                let wasStopping = self.stopping
                self.bridge = nil
                self.stopping = false
                self.port = nil
                guard self.wantsBridge else { return }
                if !wasStopping {
                    self.restartAt = ProcessInfo.processInfo.systemUptime + 2
                    self.status = "Restarting"
                    self.recordBridgeEvent("Bridge exited (status \(completed.terminationStatus)); retrying")
                }
                self.startBridge()
            }
        }
        do {
            bridge = process
            try process.run()
            port = candidate
            status = "Connecting to panel"
            message = "Bridge started on \(candidate)"
            recordBridgeEvent(message!)
        } catch {
            bridge = nil
            port = nil
            restartAt = ProcessInfo.processInfo.systemUptime + 2
            status = "Unavailable"
            message = error.localizedDescription
            recordBridgeEvent("Bridge could not start: \(message!)")
        }
    }

    private func stopBridge() {
        guard let process = bridge, !stopping else { return }
        stopping = true
        // Retain ownership until terminationHandler confirms exit. A settings
        // change must never overlap two helpers competing for TIOCEXCL.
        if process.isRunning { process.terminate() }
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self, weak process] in
            guard let self, let process, self.bridge === process,
                  self.stopping, process.isRunning else { return }
            kill(process.processIdentifier, SIGKILL)
        }
    }

    private func capture(_ process: Process, received: @escaping @MainActor @Sendable (String) -> Void) {
        for isError in [false, true] {
            let pipe = Pipe()
            let lines = BridgeOutputLines(received: received)
            pipe.fileHandleForReading.readabilityHandler = { handle in
                let data = handle.availableData
                if data.isEmpty { handle.readabilityHandler = nil }
                Task { @MainActor in lines.receive(data) }
            }
            if isError { process.standardError = pipe }
            else { process.standardOutput = pipe }
        }
    }

    private func recordBridgeOutput(_ text: String) {
        message = lastLine(text)
        if text.contains("serial connected;") { status = "Waiting for panel response" }
        if text.contains(" rx MODEL ") || text.contains(" rx THINKING ")
            || text.contains(" ignored MODEL ") || text.contains(" ignored THINKING ") {
            status = "Panel responding"
        }
        if text.contains("retrying configured port") { status = "Serial unavailable — retrying" }
        if text.contains("input guard unavailable") { status = "Input permission needed — see log" }
        if text.contains("stopped after 3 attempts") { status = "Apply failed — turn dial or Sync" }
        if text.contains(" posted ") { status = "Panel responding" }
        appendLogLines(text.split(whereSeparator: \.isNewline).map(String.init))
    }

    private func recordBridgeEvent(_ text: String) { appendLogLines(["Model Dial: \(text)"]) }

    private func appendLogLines(_ newLines: [String]) {
        let timestamp = ISO8601DateFormatter().string(from: Date())
        let records = newLines.filter { !$0.isEmpty }.map { "\(timestamp) \($0)" }
        guard !records.isEmpty else { return }
        do {
            try ensureLogFile()
            let attributes = try FileManager.default.attributesOfItem(atPath: bridgeLogURL.path)
            if let size = attributes[.size] as? NSNumber, size.intValue >= 2 * 1024 * 1024 {
                let previous = bridgeLogURL.appendingPathExtension("1")
                if FileManager.default.fileExists(atPath: previous.path) {
                    try FileManager.default.removeItem(at: previous)
                }
                try FileManager.default.moveItem(at: bridgeLogURL, to: previous)
                try ensureLogFile()
            }
            let handle = try FileHandle(forWritingTo: bridgeLogURL)
            defer { try? handle.close() }
            try handle.seekToEnd()
            try handle.write(contentsOf: Data((records.joined(separator: "\n") + "\n").utf8))
        } catch {
            message = "Could not write bridge log: \(error.localizedDescription)"
        }
    }

    private func ensureLogFile() throws {
        let directory = bridgeLogURL.deletingLastPathComponent()
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        if !FileManager.default.fileExists(atPath: bridgeLogURL.path) {
            FileManager.default.createFile(atPath: bridgeLogURL.path, contents: nil)
        }
    }

    private func shellQuote(_ value: String) -> String {
        "'\(value.replacingOccurrences(of: "'", with: "'\\''"))'"
    }

    private func bridgeLogCommandURL() throws -> URL {
        let directory = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/Model Dial", isDirectory: true)
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        return directory.appendingPathComponent("open-bridge-log.command")
    }

    private func bridgeExecutable() -> URL? {
        let bundled = Bundle.main.bundleURL.appendingPathComponent("Contents/MacOS/chatgpt-bridge")
        if FileManager.default.isExecutableFile(atPath: bundled.path) { return bundled }
        let sibling = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent()
            .appendingPathComponent("chatgpt-bridge")
        return FileManager.default.isExecutableFile(atPath: sibling.path) ? sibling : nil
    }

    private func maskArgument(_ mask: UInt64) -> String {
        String(format: "%016llx", mask)
    }

    private func lastLine(_ text: String) -> String {
        text.split(whereSeparator: \.isNewline).last.map(String.init) ?? "No output"
    }

    private func ports() -> [String] {
        guard let names = try? FileManager.default.contentsOfDirectory(atPath: "/dev") else { return [] }
        let prefixes = ["cu.usbmodem", "cu.usbserial", "cu.wchusbserial", "cu.SLAB_USBtoUART"]
        return names.filter { name in prefixes.contains(where: { name.hasPrefix($0) }) }
            .map { "/dev/\($0)" }.sorted()
    }
}
#endif
