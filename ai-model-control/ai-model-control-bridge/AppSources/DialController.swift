#if os(macOS)
import AppKit
import ApplicationServices
import Foundation
import ServiceManagement

@MainActor
final class DialController {
    /// Bridge child lifecycle. `wantsBridge` is the user's intent; this is what
    /// is actually running. A child is owned until its terminationHandler runs,
    /// so two helpers never compete for the port's TIOCEXCL claim.
    private enum Child {
        case none(retryAt: TimeInterval)
        case running(Process)
        case stopping(Process)

        var process: Process? {
            switch self {
            case .none: return nil
            case .running(let process), .stopping(let process): return process
            }
        }
    }

    var status = DialStatus.starting { didSet { onChange?() } }
    var onChange: (() -> Void)?
    var port: String?
    var message: String? { didSet { onChange?() } }
    private(set) var isSyncing = false { didSet { onChange?() } }
    var startsAtLogin: Bool
    var bridgeEnabled: Bool { wantsBridge }
    let preferences = BridgePreferences()

    private let bridgeLogURL = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Logs/Model Dial/bridge.log")
    private var child = Child.none(retryAt: 0)
    private var monitor: Timer?
    private var cursorSignature: String?
    private var lastCursorRead: TimeInterval = 0
    private var wantsBridge = true
    /// The panel can sit on a powered hub and keep running across a Mac restart
    /// with its USB link stalled, so each Model Dial launch resets it once.
    private var panelResetPending = true
    private(set) var selectedPort = UserDefaults.standard.string(forKey: "selectedSerialPort")

    var statusSummary: String {
        guard wantsBridge else { return "Bridge off" }
        if !AXIsProcessTrusted() { return "Accessibility permission needed" }
        return status.title
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
            Task { @MainActor in
                self?.checkPort()
                self?.checkCursorModels()
            }
        }
        startBridge()
        checkCursorModels()
    }

    func shutdown() {
        wantsBridge = false
        monitor?.invalidate()
        monitor = nil
        stopBridge()
        // The app's run loop is about to stop, so its delayed shutdown fallback
        // cannot be relied on to release the child and serial port.
        if let process = child.process, process.isRunning { kill(process.processIdentifier, SIGKILL) }
    }

    func setBridgeEnabled(_ enabled: Bool) {
        guard enabled != wantsBridge else { return }
        wantsBridge = enabled
        if enabled {
            if case .none = child { child = .none(retryAt: 0) }
            status = .starting
            recordBridgeEvent("Bridge enabled")
            startBridge()
        } else {
            stopBridge()
            port = nil
            status = .off
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
        status = .restarting
        recordBridgeEvent("Bridge restart requested")
        switch child {
        case .running: stopBridge()  // terminationHandler schedules the replacement
        case .stopping: break        // already exiting; same outcome
        case .none: scheduleStart(after: 0.2)
        }
    }

    /// none(retryAt) and a start attempt once it passes; the 2 s port check
    /// is the fallback if this attempt finds no port.
    private func scheduleStart(after delay: TimeInterval) {
        child = .none(retryAt: ProcessInfo.processInfo.systemUptime + delay)
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            self?.startBridge()
        }
    }

    private func checkCursorModels() {
        guard wantsBridge, !isSyncing,
              let signature = CursorModelSync.fileSignature,
              signature != cursorSignature,
              ProcessInfo.processInfo.systemUptime - lastCursorRead >= 10 else { return }
        lastCursorRead = ProcessInfo.processInfo.systemUptime
        Task { await syncCursorModels(signature: signature) }
    }

    private func syncCursorModels(signature: String) async {
        guard wantsBridge, !isSyncing else { return }
        isSyncing = true
        let originalMask = preferences.cursorModelMask
        let result = await Task.detached(priority: .utility) { CursorModelSync.readMask() }.value
        defer { isSyncing = false }
        guard wantsBridge else { return }
        switch result {
        case .success(let mask):
            // If Cursor changed while the read ran, let the next poll get its final state.
            guard CursorModelSync.fileSignature == signature else { return }
            cursorSignature = signature
            // A preference edit made while reading Cursor takes precedence.
            if preferences.cursorModelMask == originalMask, preferences.syncCursorModels(mask) {
                recordBridgeEvent("Cursor models updated from Cursor")
                restartBridge()
            }
        case .failure(let error):
            recordBridgeEvent("Cursor sync: \(error.localizedDescription); saved selection retained")
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
            status = .waitingForSelectedPanel
            stopBridge()
        }
        startBridge()
    }

    private func startBridge() {
        guard wantsBridge, case .none(let retryAt) = child,
              ProcessInfo.processInfo.systemUptime >= retryAt else { return }
        let available = ports()
        if selectedPort == nil, available.count == 1 {
            selectedPort = available[0]
            UserDefaults.standard.set(available[0], forKey: "selectedSerialPort")
        }
        guard let candidate = selectedPort else {
            port = nil
            status = available.isEmpty ? .waitingForPanel : .chooseDevice
            return
        }
        guard available.contains(candidate) else {
            port = nil
            status = .waitingForSelectedPanel
            return
        }
        guard let executable = bridgeExecutable() else {
            status = .unavailable
            message = "The bundled ai-model-control-bridge executable is missing."
            recordBridgeEvent(message!)
            return
        }
        let arguments = [
            "--watch", "--send-serial", "--port", candidate,
            "--chatgpt-effort-mask", maskArgument(preferences.chatGPTThinkingMask),
            "--cursor-model-mask", maskArgument(preferences.cursorModelMask),
            "--show-older-models", preferences.showOlderModels ? "1" : "0",
            "--dial-swap", preferences.swapDials ? "1" : "0",
        ] + (panelResetPending ? ["--reset-panel"] : [])
        let process = Process()
        process.executableURL = executable
        process.arguments = arguments
        capture(process) { [weak self, weak process] text in
            guard let self, let process, case .running(let current) = self.child, current === process else { return }
            self.recordBridgeOutput(text)
        }
        process.terminationHandler = { [weak self] completed in
            Task { @MainActor in
                guard let self, self.child.process === completed else { return }
                let requested: Bool
                if case .stopping = self.child { requested = true } else { requested = false }
                self.port = nil
                guard self.wantsBridge else {
                    self.child = .none(retryAt: 0)
                    return
                }
                if requested {
                    self.scheduleStart(after: 0.2)
                } else {
                    self.status = .restarting
                    self.recordBridgeEvent("Bridge exited (status \(completed.terminationStatus)); retrying")
                    self.scheduleStart(after: 2)
                }
            }
        }
        do {
            child = .running(process)
            try process.run()
            panelResetPending = false
            port = candidate
            status = .connecting
            message = "Bridge started on \(candidate)"
            recordBridgeEvent(message!)
        } catch {
            child = .none(retryAt: ProcessInfo.processInfo.systemUptime + 2)
            port = nil
            status = .unavailable
            message = error.localizedDescription
            recordBridgeEvent("Bridge could not start: \(message!)")
        }
    }

    private func stopBridge() {
        guard case .running(let process) = child else { return }
        child = .stopping(process)
        if process.isRunning { process.terminate() }
        DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [weak self, weak process] in
            guard let self, let process, case .stopping(let current) = self.child,
                  current === process, process.isRunning else { return }
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
        if let next = DialStatus(bridgeLine: text) {
            status = next
        } else {
            message = lastLine(text)
        }
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
        let bundled = Bundle.main.bundleURL.appendingPathComponent("Contents/MacOS/ai-model-control-bridge")
        if FileManager.default.isExecutableFile(atPath: bundled.path) { return bundled }
        let sibling = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent()
            .appendingPathComponent("ai-model-control-bridge")
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
