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
private final class StatusItemDelegate: NSObject, NSApplicationDelegate, NSMenuDelegate {
    private let controller = DialController()
    private let statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let menu = NSMenu()
    private var bridgeToggleRow: NSMenuItem!
    private var bridgeSwitch: NSSwitch!
    private var loginRow: NSMenuItem!

    func applicationDidFinishLaunching(_ notification: Notification) {
        statusItem.button?.image = NSImage(systemSymbolName: "dial.medium", accessibilityDescription: "Model Dial")
        statusItem.button?.imagePosition = .imageLeading
        statusItem.button?.title = ""
        menu.delegate = self
        bridgeToggleRow = NSMenuItem()
        let bridgeLabel = NSTextField(labelWithString: "Bridge")
        bridgeLabel.frame = NSRect(x: 12, y: 6, width: 54, height: 18)
        bridgeSwitch = NSSwitch(frame: NSRect(x: 78, y: 4, width: 44, height: 22))
        bridgeSwitch.target = self
        bridgeSwitch.action = #selector(toggleBridge)
        let bridgeView = NSView(frame: NSRect(x: 0, y: 0, width: 134, height: 30))
        bridgeView.addSubview(bridgeLabel)
        bridgeView.addSubview(bridgeSwitch)
        bridgeToggleRow.view = bridgeView
        menu.addItem(bridgeToggleRow)
        menu.addItem(.separator())
        menu.addItem(withTitle: "Open bridge log in Terminal", action: #selector(openBridgeLog), keyEquivalent: "")
        menu.addItem(.separator())
        loginRow = menu.addItem(withTitle: "Open at login", action: #selector(toggleLogin), keyEquivalent: "")
        menu.addItem(.separator())
        menu.addItem(withTitle: "Quit Model Dial", action: #selector(quit), keyEquivalent: "")
        for item in menu.items { item.target = self }
        statusItem.menu = menu
        controller.start()
        refreshMenu()
    }

    func menuWillOpen(_ menu: NSMenu) { refreshMenu() }

    @objc private func toggleBridge(_ sender: NSSwitch) {
        controller.setBridgeEnabled(sender.state == .on)
        refreshMenu()
    }
    @objc private func openBridgeLog() { controller.openBridgeLog() }
    @objc private func toggleLogin() { controller.setStartsAtLogin(!controller.startsAtLogin); refreshMenu() }
    @objc private func quit() { NSApplication.shared.terminate(nil) }

    private func refreshMenu() {
        bridgeSwitch.state = controller.bridgeEnabled ? .on : .off
        loginRow.state = controller.startsAtLogin ? .on : .off
    }
}

@MainActor
private final class DialController {
    var status = "Starting"
    var port: String?
    var message: String?
    var startsAtLogin: Bool
    var bridgeEnabled: Bool { wantsBridge }

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

    func openBridgeLog() {
        do {
            try ensureLogFile()
            let command = "tail -n 100 -F \(shellQuote(bridgeLogURL.path))"
            let source = """
            tell application "Terminal"
                activate
                do script \(appleScriptLiteral(command))
            end tell
            """
            var error: NSDictionary?
            NSAppleScript(source: source)?.executeAndReturnError(&error)
            if let error { message = "Could not open Terminal: \(error.description)" }
        } catch {
            message = "Could not create bridge log: \(error.localizedDescription)"
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
        process.arguments = ["--watch", "--send-serial", "--port", candidate]
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

    private func appleScriptLiteral(_ value: String) -> String {
        "\"\(value.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\""))\""
    }

    private func bridgeExecutable() -> URL? {
        let bundled = Bundle.main.bundleURL.appendingPathComponent("Contents/MacOS/chatgpt-bridge")
        if FileManager.default.isExecutableFile(atPath: bundled.path) { return bundled }
        let sibling = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent()
            .appendingPathComponent("chatgpt-bridge")
        return FileManager.default.isExecutableFile(atPath: sibling.path) ? sibling : nil
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
