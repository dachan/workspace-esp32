#if os(macOS)
import AppKit
import Foundation
import ServiceManagement
import SwiftUI

@main
struct ModelDialApp: App {
    @NSApplicationDelegateAdaptor(StatusItemDelegate.self) private var delegate

    var body: some Scene {
        Settings { EmptyView() }
    }
}


@MainActor
private final class MenuToggle: NSControl {
    var isOn = false { didSet { needsDisplay = true } }

    override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
    }

    override func draw(_ dirtyRect: NSRect) {
        let track = bounds.insetBy(dx: 1, dy: 1)
        let trackPath = NSBezierPath(roundedRect: track, xRadius: track.height / 2, yRadius: track.height / 2)
        (isOn ? NSColor.systemGreen : NSColor.quaternaryLabelColor).setFill()
        trackPath.fill()

        let knobSize = track.height - 4
        let knobX = isOn ? track.maxX - knobSize - 2 : track.minX + 2
        let knob = NSRect(x: knobX, y: track.midY - knobSize / 2, width: knobSize, height: knobSize)
        NSColor.white.setFill()
        NSBezierPath(ovalIn: knob).fill()
    }

    override func mouseUp(with event: NSEvent) {
        guard isEnabled else { return }
        isOn.toggle()
        sendAction(action, to: target)
    }
}

@MainActor
private final class StatusItemDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private let controller = DialController()
    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let menu = NSMenu()
    private var bridgeToggleRow: NSMenuItem!
    private var bridgeSwitch: MenuToggle!
    private var loginRow: NSMenuItem!
    private var loginSwitch: MenuToggle!
    private var settingsWindow: SettingsWindowController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        statusItem.button?.image = NSImage(systemSymbolName: "dial.medium", accessibilityDescription: "Model Dial")
        statusItem.button?.imagePosition = .imageLeading
        statusItem.button?.title = ""
        menu.delegate = self
        (bridgeToggleRow, bridgeSwitch) = makeToggleRow(title: "Bridge", action: #selector(toggleBridge))
        menu.addItem(bridgeToggleRow)
        (loginRow, loginSwitch) = makeToggleRow(title: "Open At Login", action: #selector(toggleLogin))
        menu.addItem(loginRow)
        menu.addItem(.separator())
        menu.addItem(withTitle: "Sync", action: #selector(syncApps), keyEquivalent: "")
        menu.addItem(withTitle: "Settings…", action: #selector(openSettings), keyEquivalent: "")
        menu.addItem(withTitle: "Open Log", action: #selector(openBridgeLog), keyEquivalent: "")
        menu.addItem(.separator())
        menu.addItem(withTitle: "Quit Model Dial", action: #selector(quit), keyEquivalent: "")
        for item in menu.items { item.target = self }
        statusItem.menu = menu
        controller.start()
        refreshMenu()
    }

    func applicationWillTerminate(_ notification: Notification) {
        controller.shutdown()
    }

    func menuWillOpen(_ menu: NSMenu) { refreshMenu() }

    @objc private func toggleBridge(_ sender: MenuToggle) {
        controller.setBridgeEnabled(sender.isOn)
        refreshMenu()
    }
    @objc private func toggleLogin(_ sender: MenuToggle) {
        controller.setStartsAtLogin(sender.isOn)
        refreshMenu()
    }
    @objc private func openBridgeLog() { controller.openBridgeLog() }
    @objc private func syncApps() { controller.syncApps() }
    @objc private func openSettings() {
        if settingsWindow == nil {
            settingsWindow = SettingsWindowController(
                preferences: controller.preferences,
                onChange: { [weak controller] in controller?.restartBridge() }
            )
        }
        settingsWindow?.showWindow()
    }
    @objc private func quit() { NSApplication.shared.terminate(nil) }

    private func refreshMenu() {
        bridgeSwitch.isOn = controller.bridgeEnabled
        loginSwitch.isOn = controller.startsAtLogin
    }

    private func makeToggleRow(title: String, action: Selector) -> (NSMenuItem, MenuToggle) {
        let item = NSMenuItem()
        let label = NSTextField(labelWithString: title)
        label.frame = NSRect(x: 12, y: 6, width: 92, height: 18)
        let toggle = MenuToggle(frame: NSRect(x: 134, y: 5, width: 36, height: 24))
        toggle.target = self
        toggle.action = action
        let view = NSView(frame: NSRect(x: 0, y: 0, width: 194, height: 34))
        view.addSubview(label)
        view.addSubview(toggle)
        item.view = view
        return (item, toggle)
    }
}

@MainActor
private final class DialController {
    var status = "Starting"
    var port: String?
    var message: String?
    var startsAtLogin: Bool
    var bridgeEnabled: Bool { wantsBridge }
    let preferences = BridgePreferences()

    private let bridgeLogURL = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent("Library/Logs/Model Dial/bridge.log")
    private var bridge: Process?
    private var monitor: Timer?
    private var wantsBridge = true

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
    }

    func setBridgeEnabled(_ enabled: Bool) {
        guard enabled != wantsBridge else { return }
        wantsBridge = enabled
        if enabled {
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
        } catch {
            message = "Could not change login setting: \(error.localizedDescription)"
        }
    }

    func restartBridge() {
        guard wantsBridge else { return }
        stopBridge()
        port = nil
        status = "Starting"
        recordBridgeEvent("Bridge restarting after settings change")
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) { [weak self] in
            self?.startBridge()
        }
    }

    func syncApps() {
        guard wantsBridge else { return }
        if syncCursorModelsFromApp() {
            recordBridgeEvent("Cursor models updated from Cursor")
        }
        recordBridgeEvent("Synced ChatGPT efforts: \(preferences.enabledChatGPTEffortNames().joined(separator: ", "))")
        recordBridgeEvent("Synced Cursor models: \(preferences.enabledCursorModelNames().joined(separator: ", "))")
        recordBridgeEvent("Synced OpenCode models: \(BridgePreferences.openCodeModels.joined(separator: ", "))")
        recordBridgeEvent("Sync requested for ChatGPT, Cursor, and OpenCode")
        restartBridge()
    }

    private func syncCursorModelsFromApp() -> Bool {
        let database = NSHomeDirectory() + "/Library/Application Support/Cursor/User/globalStorage/state.vscdb"
        guard FileManager.default.fileExists(atPath: database) else { return false }

        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/sqlite3")
        process.arguments = ["-readonly", database, "SELECT CAST(value AS TEXT) FROM ItemTable WHERE key='src.vs.platform.reactivestorage.browser.reactiveStorageServiceImpl.persistentStorage.applicationUser';"]
        let outputURL = URL(fileURLWithPath: NSTemporaryDirectory(), isDirectory: true)
            .appendingPathComponent("model-dial-cursor-catalog-\(UUID().uuidString).json")
        FileManager.default.createFile(atPath: outputURL.path, contents: nil)
        defer { try? FileManager.default.removeItem(at: outputURL) }

        do {
            let output = try FileHandle(forWritingTo: outputURL)
            process.standardOutput = output
            try process.run()
            process.waitUntilExit()
            try output.close()
            guard process.terminationStatus == 0,
                  let object = try JSONSerialization.jsonObject(with: Data(contentsOf: outputURL)) as? [String: Any],
                  let catalog = object["availableDefaultModels2"] as? [[String: Any]],
                  let settings = object["aiSettings"] as? [String: Any],
                  let enabled = settings["modelOverrideEnabled"] as? [String],
                  let disabled = settings["modelOverrideDisabled"] as? [String] else {
                recordBridgeEvent("Cursor sync failed: model catalog or overrides unavailable; saved selection retained")
                return false
            }

            let defaultIDs = catalog.compactMap { model -> String? in
                model["defaultOn"] as? Bool == true ? model["name"] as? String : nil
            }
            let enabledIDs = Set(defaultIDs).union(enabled).subtracting(disabled)
            var mask: UInt64 = 1
            for (index, name) in BridgePreferences.cursorModels.enumerated() where index > 0 {
                let model = catalog.first { $0["clientDisplayName"] as? String == name }
                let modelID = model?["name"] as? String ?? cursorModelID(name)
                if enabledIDs.contains(modelID) {
                    mask |= UInt64(1) << UInt64(index)
                }
            }
            return preferences.syncCursorModels(mask)
        } catch {
            recordBridgeEvent("Could not read Cursor model selection: \(error.localizedDescription)")
            return false
        }
    }

    private func findValue(_ key: String, in value: Any) -> Any? {
        if let dictionary = value as? [String: Any] {
            if let found = dictionary[key] { return found }
            for child in dictionary.values {
                if let found = findValue(key, in: child) { return found }
            }
        } else if let values = value as? [Any] {
            for child in values {
                if let found = findValue(key, in: child) { return found }
            }
        }
        return nil
    }

    private func cursorModelID(_ name: String) -> String {
        if name == "Codex 5.3" { return "gpt-5.3-codex" }
        if name.hasPrefix("Cursor Grok ") {
            return name.replacingOccurrences(of: "Cursor ", with: "").lowercased()
        }
        return name.lowercased().replacingOccurrences(of: " ", with: "-")
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
        guard wantsBridge else { return }
        if ports().first != port { stopBridge() }
        startBridge()
    }

    private func startBridge() {
        guard wantsBridge, bridge == nil else { return }
        guard let candidate = ports().first else {
            port = nil
            status = "Waiting for panel"
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
        capture(process) { [weak self] text in self?.recordBridgeOutput(text) }
        process.terminationHandler = { [weak self] completed in
            Task { @MainActor in
                guard let self, self.bridge === completed else { return }
                self.bridge = nil
                guard self.wantsBridge else { return }
                self.status = "Restarting"
                self.recordBridgeEvent("Bridge stopped; retrying")
                DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in self?.startBridge() }
            }
        }
        do {
            bridge = process
            try process.run()
            port = candidate
            status = "Connected"
            message = "Bridge started on \(candidate)"
            recordBridgeEvent(message!)
        } catch {
            bridge = nil
            port = nil
            status = "Unavailable"
            message = error.localizedDescription
            recordBridgeEvent("Bridge could not start: \(message!)")
        }
    }

    private func stopBridge() {
        guard let process = bridge else { return }
        bridge = nil
        process.terminate()
    }

    private func capture(_ process: Process, received: @escaping @MainActor @Sendable (String) -> Void) {
        let stdout = Pipe()
        let stderr = Pipe()
        let read: @Sendable (FileHandle) -> Void = { handle in
            let data = handle.availableData
            guard let text = String(data: data, encoding: .utf8), !text.isEmpty else { return }
            Task { @MainActor in received(text) }
        }
        stdout.fileHandleForReading.readabilityHandler = read
        stderr.fileHandleForReading.readabilityHandler = read
        process.standardOutput = stdout
        process.standardError = stderr
    }

    private func recordBridgeOutput(_ text: String) {
        message = lastLine(text)
        appendLogLines(text.split(whereSeparator: \.isNewline).map(String.init))
    }

    private func recordBridgeEvent(_ text: String) { appendLogLines(["Model Dial: \(text)"]) }

    private func appendLogLines(_ newLines: [String]) {
        let timestamp = ISO8601DateFormatter().string(from: Date())
        let records = newLines.filter { !$0.isEmpty }.map { "\(timestamp) \($0)" }
        guard !records.isEmpty else { return }
        do {
            try ensureLogFile()
            let handle = try FileHandle(forWritingTo: bridgeLogURL)
            try handle.seekToEnd()
            try handle.write(contentsOf: Data((records.joined(separator: "\n") + "\n").utf8))
            try handle.close()
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
        "'\(value.replacingOccurrences(of: "'", with: "'\\\\''"))'"
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
