#if os(macOS)
import Combine
import SwiftUI
import AppKit

final class BridgePreferences: ObservableObject {
    static let chatGPTEfforts = ["Light", "Medium", "High", "Extra High", "Max", "Ultra"]
    static let cursorModels = [
        "Auto",
        "Cursor Grok 4.6",
        "Composer 2.5",
        "Claude Opus 5",
        "GPT-5.6 Sol",
        "Claude Fable 5",
        "GPT-5.6 Terra",
        "GPT-5.6 Luna",
        "Claude Opus 4.8",
        "GPT-5.5",
        "Claude Fable 5.1",
        "Cursor Grok 4.5",
        "Gemini 3.8 Flash",
        "Gemini 3.7 Flash",
        "Claude Sonnet 5",
        "Claude Sonnet 4.6",
        "Codex 5.3",
        "Claude Opus 4.7",
        "GPT-5.4",
        "Claude Opus 4.6",
        "Claude Opus 4.5",
        "GPT-5.2",
        "Gemini 3.6 Flash",
        "Gemini 3.1 Pro",
        "GPT-5.4 Mini",
        "GPT-5.4 Nano",
        "Claude Haiku 4.5",
        "Claude Sonnet 4.5",
        "GPT-5.1",
        "Gemini 3 Flash",
        "Gemini 3.5 Flash",
        "Claude Sonnet 4",
        "GPT-5 Mini",
        "Gemini 2.5 Flash",
        "Kimi K3",
        "Kimi K2.7 Code",
        "GLM 5.2",
    ]

    static let defaultChatGPTThinkingMask: UInt64 = 0x0F
    static let defaultCursorModelMask: UInt64 = 0xFF

    @Published private(set) var chatGPTThinkingMask: UInt64
    @Published private(set) var cursorModelMask: UInt64
    var onChange: (() -> Void)?

    private let defaults = UserDefaults.standard
    private let chatGPTKey = "chatGPTThinkingMask"
    private let cursorKey = "cursorModelMask"

    init() {
        chatGPTThinkingMask = Self.readMask(
            defaults: defaults,
            key: chatGPTKey,
            fallback: Self.defaultChatGPTThinkingMask
        )
        cursorModelMask = Self.readMask(
            defaults: defaults,
            key: cursorKey,
            fallback: Self.defaultCursorModelMask
        )
        chatGPTThinkingMask = Self.normalizedEffortMask(chatGPTThinkingMask)
        cursorModelMask = Self.normalizedCursorMask(cursorModelMask)
    }

    func isEffortEnabled(_ index: Int) -> Bool {
        index >= 0 && index < Self.chatGPTEfforts.count
            && chatGPTThinkingMask & (UInt64(1) << UInt64(index)) != 0
    }

    func setEffortEnabled(_ enabled: Bool, index: Int) {
        guard index >= 0, index < Self.chatGPTEfforts.count else { return }
        var mask = chatGPTThinkingMask
        let bit = UInt64(1) << UInt64(index)
        if enabled { mask |= bit } else { mask &= ~bit }
        updateChatGPTMask(mask)
    }

    func effortBinding(index: Int) -> Binding<Bool> {
        Binding(
            get: { self.isEffortEnabled(index) },
            set: { self.setEffortEnabled($0, index: index) }
        )
    }

    func isCursorModelEnabled(_ index: Int) -> Bool {
        index == 0 || (index > 0 && index < Self.cursorModels.count
                       && cursorModelMask & (UInt64(1) << UInt64(index)) != 0)
    }

    func setCursorModelEnabled(_ enabled: Bool, index: Int) {
        guard index > 0, index < Self.cursorModels.count else { return }
        var mask = cursorModelMask
        let bit = UInt64(1) << UInt64(index)
        if enabled { mask |= bit } else { mask &= ~bit }
        updateCursorMask(mask)
    }

    func cursorModelBinding(index: Int) -> Binding<Bool> {
        Binding(
            get: { self.isCursorModelEnabled(index) },
            set: { self.setCursorModelEnabled($0, index: index) }
        )
    }

    func reset() {
        updateChatGPTMask(Self.defaultChatGPTThinkingMask)
        updateCursorMask(Self.defaultCursorModelMask)
    }

    private func updateChatGPTMask(_ raw: UInt64) {
        let next = Self.normalizedEffortMask(raw)
        guard next != chatGPTThinkingMask else { return }
        chatGPTThinkingMask = next
        persist(chatGPTKey, mask: next)
    }

    private func updateCursorMask(_ raw: UInt64) {
        let next = Self.normalizedCursorMask(raw)
        guard next != cursorModelMask else { return }
        cursorModelMask = next
        persist(cursorKey, mask: next)
    }

    private func persist(_ key: String, mask: UInt64) {
        defaults.set(String(format: "%016llx", mask), forKey: key)
        onChange?()
    }

    private static func readMask(defaults: UserDefaults, key: String, fallback: UInt64) -> UInt64 {
        guard let raw = defaults.string(forKey: key), let mask = UInt64(raw, radix: 16) else {
            return fallback
        }
        return mask
    }

    private static func normalizedEffortMask(_ raw: UInt64) -> UInt64 {
        let limit = (UInt64(1) << UInt64(chatGPTEfforts.count)) - 1
        let mask = raw & limit
        return mask == 0 ? 1 : mask
    }

    private static func normalizedCursorMask(_ raw: UInt64) -> UInt64 {
        let limit = (UInt64(1) << UInt64(cursorModels.count)) - 1
        return (raw & limit) | 1
    }
}

struct SettingsView: View {
    @ObservedObject var preferences: BridgePreferences

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                settingsSection(
                    title: "ChatGPT Efforts",
                    detail: "Enabled levels are available on the ESP32 effort dial."
                ) {
                    ForEach(Array(BridgePreferences.chatGPTEfforts.enumerated()), id: \.offset) { index, effort in
                        Toggle(effort, isOn: preferences.effortBinding(index: index))
                    }
                }

                Divider()

                settingsSection(
                    title: "Cursor Models",
                    detail: "Enabled models are available on the ESP32 model dial."
                ) {
                    ForEach(Array(BridgePreferences.cursorModels.enumerated()), id: \.offset) { index, model in
                        Toggle(model, isOn: preferences.cursorModelBinding(index: index))
                            .disabled(index == 0)
                    }
                }

                Divider()

                HStack {
                    Spacer()
                    Button("Reset Defaults") {
                        preferences.reset()
                    }
                    Spacer()
                }
            }
            .padding(22)
        }
        .frame(minWidth: 420, minHeight: 620)
    }

    @ViewBuilder
    private func settingsSection<Content: View>(
        title: String,
        detail: String,
        @ViewBuilder content: () -> Content
    ) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title)
                .font(.headline)
            Text(detail)
                .font(.subheadline)
                .foregroundStyle(.secondary)
            content()
        }
    }
}

final class SettingsWindowController: NSWindowController {
    init(preferences: BridgePreferences, onChange: @escaping () -> Void) {
        let hosting = NSHostingView(rootView: SettingsView(preferences: preferences))
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 460, height: 680),
            styleMask: [.titled, .closable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "Model Dial Settings"
        window.contentView = hosting
        window.contentMinSize = NSSize(width: 420, height: 560)
        window.isReleasedWhenClosed = false
        preferences.onChange = onChange
        super.init(window: window)
    }

    required init?(coder: NSCoder) {
        super.init(coder: coder)
    }

    func showWindow() {
        window?.center()
        showWindow(nil)
        NSApp.activate(ignoringOtherApps: true)
    }
}
#endif
