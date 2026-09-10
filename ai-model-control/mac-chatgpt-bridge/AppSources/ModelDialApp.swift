#if os(macOS)
import AppKit
import Combine
import CryptoKit
import Foundation
import ServiceManagement
import SwiftUI

@main
struct ModelDialApp: App {
    @StateObject private var controller = DialController()

    var body: some Scene {
        MenuBarExtra("Model Dial", systemImage: controller.symbol) {
            VStack(alignment: .leading, spacing: 12) {
                Text("Model Dial").font(.headline)
                LabeledContent("Bridge", value: controller.status)
                LabeledContent("Panel", value: controller.port ?? "Not connected")
                if let message = controller.message {
                    Text(message).font(.caption).foregroundStyle(.secondary).lineLimit(3)
                }
                Divider()
                Toggle("Open at login", isOn: Binding(
                    get: { controller.startsAtLogin },
                    set: { controller.setStartsAtLogin($0) }
                ))
                Button("Reconnect now") { controller.reconnect() }
                Divider()
                Text("Firmware update").font(.headline)
                LabeledContent("Image", value: controller.firmwareName ?? "Choose a firmware image")
                LabeledContent("Flasher", value: controller.flasherName ?? "Choose esptool")
                if let hash = controller.firmwareHash {
                    Text("SHA-256 \(hash)").font(.caption2.monospaced()).foregroundStyle(.secondary).lineLimit(1)
                }
                HStack {
                    Button("Choose image…") { controller.chooseFirmware() }
                    Button("Choose flasher…") { controller.chooseFlasher() }
                }
                Button(controller.flashing ? "Installing…" : "Install selected firmware") {
                    controller.installFirmware()
                }
                .disabled(!controller.canInstall)
                Divider()
                Button("Quit Model Dial") { NSApplication.shared.terminate(nil) }
            }
            .padding(14)
            .frame(width: 360)
            .task { controller.start() }
        }
        .menuBarExtraStyle(.window)
    }
}

@MainActor
private final class DialController: ObservableObject {
    @Published var status = "Starting"
    @Published var port: String?
    @Published var message: String?
    @Published var firmwareHash: String?
    @Published var flashing = false
    @Published var startsAtLogin: Bool
    private var bridge: Process?
    private var flasher: Process?
    private var monitor: Timer?
    private var flashOutput = ""
    private var firmware: URL?
    private var flasherURL: URL?
    private var wantsBridge = true

    init() {
        startsAtLogin = SMAppService.mainApp.status == .enabled
        firmware = savedURL("firmwarePath") ?? Bundle.main.url(
            forResource: "ai-model-control-v0.90", withExtension: "bin", subdirectory: "Firmware"
        )
        flasherURL = savedURL("flasherPath")
        refreshHash()
    }

    var symbol: String { flashing ? "arrow.triangle.2.circlepath" : bridge == nil ? "exclamationmark.triangle" : "dial.medium" }
    var firmwareName: String? { firmware?.lastPathComponent }
    var flasherName: String? { flasherURL?.lastPathComponent }
    var canInstall: Bool { !flashing && port != nil && firmware != nil && flasherURL != nil }

    func start() {
        guard monitor == nil else { return }
        monitor = Timer.scheduledTimer(withTimeInterval: 2, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.checkPort() }
        }
        startBridge()
    }

    func setStartsAtLogin(_ enabled: Bool) {
        do {
            if enabled { try SMAppService.mainApp.register() }
            else { try SMAppService.mainApp.unregister() }
            startsAtLogin = SMAppService.mainApp.status == .enabled
        } catch {
            message = "Could not change login setting: \(error.localizedDescription)"
            startsAtLogin = SMAppService.mainApp.status == .enabled
        }
    }

    func reconnect() { stopBridge(); wantsBridge = true; startBridge() }

    func chooseFirmware() {
        chooseFile(title: "Choose ESP32 firmware") { url in
            self.firmware = url
            UserDefaults.standard.set(url.path, forKey: "firmwarePath")
            self.refreshHash()
        }
    }

    func chooseFlasher() {
        chooseFile(title: "Choose esptool") { url in
            self.flasherURL = url
            UserDefaults.standard.set(url.path, forKey: "flasherPath")
        }
    }

    func installFirmware() {
        guard let image = firmware, let tool = flasherURL, let port,
              FileManager.default.isExecutableFile(atPath: tool.path) else {
            message = "Choose a firmware image, an executable esptool, and a connected panel."
            return
        }
        wantsBridge = false
        stopBridge()
        flashing = true
        status = "Installing firmware"
        flashOutput = ""
        let process = Process()
        process.executableURL = tool
        process.arguments = [
            "--chip", "esp32s3", "--port", port, "--baud", "460800",
            "--before", "default-reset", "--after", "hard-reset", "write-flash",
            "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "16MB",
            "0x10000", image.path,
        ]
        capture(process) { [weak self] text in self?.flashOutput += text }
        process.terminationHandler = { [weak self] completed in
            Task { @MainActor in self?.finishFlash(completed) }
        }
        do {
            flasher = process
            try process.run()
            message = "Flashing \(image.lastPathComponent) to \(port)"
        } catch {
            flasher = nil
            flashing = false
            wantsBridge = true
            status = "Flash could not start"
            message = error.localizedDescription
            startBridge()
        }
    }

    private func finishFlash(_ process: Process) {
        guard flasher === process else { return }
        let verified = process.terminationStatus == 0 && flashOutput.contains("Hash of data verified")
        flasher = nil
        flashing = false
        wantsBridge = true
        status = verified ? "Firmware verified; reconnecting" : "Firmware update failed"
        message = verified ? "Firmware hash verified. Reconnecting to the panel." : lastLine(flashOutput)
        DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in self?.startBridge() }
    }

    private func checkPort() {
        guard wantsBridge, !flashing else { return }
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
            status = "Bridge unavailable"
            message = "The bundled chatgpt-bridge executable is missing."
            return
        }
        let process = Process()
        process.executableURL = executable
        process.arguments = ["--watch", "--send-serial", "--port", candidate]
        capture(process) { [weak self] text in self?.message = self?.lastLine(text) }
        process.terminationHandler = { [weak self] completed in
            Task { @MainActor in
                guard let self, self.bridge === completed else { return }
                self.bridge = nil
                guard self.wantsBridge, !self.flashing else { return }
                self.status = "Bridge restarting"
                DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in self?.startBridge() }
            }
        }
        do {
            bridge = process
            try process.run()
            port = candidate
            status = "Connected"
            message = "Bridge started on \(candidate)"
        } catch {
            bridge = nil
            port = nil
            status = "Bridge unavailable"
            message = error.localizedDescription
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

    private func bridgeExecutable() -> URL? {
        let bundled = Bundle.main.bundleURL.appendingPathComponent("Contents/MacOS/chatgpt-bridge")
        if FileManager.default.isExecutableFile(atPath: bundled.path) { return bundled }
        let sibling = URL(fileURLWithPath: CommandLine.arguments[0]).deletingLastPathComponent()
            .appendingPathComponent("chatgpt-bridge")
        return FileManager.default.isExecutableFile(atPath: sibling.path) ? sibling : nil
    }

    private func chooseFile(title: String, selected: (URL) -> Void) {
        let panel = NSOpenPanel()
        panel.title = title
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        if panel.runModal() == .OK, let url = panel.url { selected(url) }
    }

    private func refreshHash() {
        guard let firmware else { firmwareHash = nil; return }
        guard let data = try? Data(contentsOf: firmware) else {
            firmwareHash = nil
            message = "Could not read firmware image."
            return
        }
        firmwareHash = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
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

    private func savedURL(_ key: String) -> URL? {
        guard let path = UserDefaults.standard.string(forKey: key), !path.isEmpty else { return nil }
        return URL(fileURLWithPath: path)
    }
}
#endif
