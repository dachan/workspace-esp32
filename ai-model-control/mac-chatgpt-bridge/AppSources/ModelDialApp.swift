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
    private var loginRow: NSMenuItem!
    private let statusRow = NSMenuItem(title: "Starting", action: nil, keyEquivalent: "")
    private let deviceMenu = NSMenu(title: "Device")
    private var syncRow: NSMenuItem!
    private var settingsWindow: SettingsWindowController?

    func applicationDidFinishLaunching(_ notification: Notification) {
        statusItem.button?.image = NSImage(systemSymbolName: "dial.medium", accessibilityDescription: "Model Dial")
        statusItem.button?.imagePosition = .imageLeading
        statusItem.button?.title = ""
        menu.delegate = self
        menu.autoenablesItems = false
        statusRow.isEnabled = false
        menu.addItem(statusRow)
        menu.addItem(.separator())
        bridgeToggleRow = menu.addItem(withTitle: "Bridge Enabled", action: #selector(toggleBridge), keyEquivalent: "")
        loginRow = menu.addItem(withTitle: "Open at Login", action: #selector(toggleLogin), keyEquivalent: "")
        menu.addItem(.separator())
        let deviceRow = menu.addItem(withTitle: "Device", action: nil, keyEquivalent: "")
        deviceRow.submenu = deviceMenu
        syncRow = menu.addItem(withTitle: "Sync", action: #selector(syncApps), keyEquivalent: "")
        menu.addItem(withTitle: "Accessibility Settings…", action: #selector(openAccessibility), keyEquivalent: "")
        menu.addItem(withTitle: "Settings…", action: #selector(openSettings), keyEquivalent: "")
        menu.addItem(withTitle: "Open Log", action: #selector(openBridgeLog), keyEquivalent: "")
        menu.addItem(.separator())
        menu.addItem(withTitle: "Quit Model Dial", action: #selector(quit), keyEquivalent: "")
        for item in menu.items { item.target = self }
        statusItem.menu = menu
        controller.onChange = { [weak self] in self?.refreshMenu() }
        controller.start()
        refreshMenu()
    }

    func applicationWillTerminate(_ notification: Notification) {
        controller.shutdown()
    }

    func menuWillOpen(_ menu: NSMenu) {
        refreshMenu()
        refreshDevices()
    }

    @objc private func toggleBridge() {
        controller.setBridgeEnabled(!controller.bridgeEnabled)
        refreshMenu()
    }
    @objc private func toggleLogin() {
        controller.setStartsAtLogin(!controller.startsAtLogin)
        refreshMenu()
    }
    @objc private func openAccessibility() { controller.openAccessibilitySettings() }
    @objc private func selectDevice(_ sender: NSMenuItem) {
        if let path = sender.representedObject as? String { controller.selectPort(path) }
        refreshDevices()
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
        bridgeToggleRow.state = controller.bridgeEnabled ? .on : .off
        loginRow.state = controller.startsAtLogin ? .on : .off
        statusRow.title = controller.statusSummary
        statusRow.toolTip = controller.message
        statusItem.button?.toolTip = "Model Dial: \(controller.statusSummary)"
        syncRow.isEnabled = controller.bridgeEnabled && controller.port != nil && !controller.isSyncing
    }

    private func refreshDevices() {
        deviceMenu.removeAllItems()
        deviceMenu.autoenablesItems = false
        let available = controller.availablePorts
        var choices = available
        if let selected = controller.selectedPort, !choices.contains(selected) { choices.append(selected) }
        for path in choices.sorted() {
            let present = available.contains(path)
            let title = URL(fileURLWithPath: path).lastPathComponent + (present ? "" : " (disconnected)")
            let row = deviceMenu.addItem(withTitle: title, action: #selector(selectDevice), keyEquivalent: "")
            row.target = self
            row.representedObject = path
            row.state = path == controller.selectedPort ? .on : .off
            row.isEnabled = present
        }
        if choices.isEmpty {
            let row = deviceMenu.addItem(withTitle: "No serial devices found", action: nil, keyEquivalent: "")
            row.isEnabled = false
        }
    }
}

#endif
