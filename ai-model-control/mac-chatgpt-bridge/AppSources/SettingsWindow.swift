#if os(macOS)
import Combine
import SwiftUI
import AppKit

struct CursorModelSetting: Identifiable {
    let index: Int
    let name: String
    let provider: String

    var id: Int { index }
}

struct CursorModelGroup: Identifiable {
    let provider: String
    let models: [CursorModelSetting]

    var id: String { provider }
}

final class BridgePreferences: ObservableObject {
    static let chatGPTEfforts = ["Light", "Medium", "High", "Extra High", "Max", "Ultra"]
    static let chatGPTModels = ["GPT-6 Astra", "GPT-5.6 Sol", "GPT-5.6 Terra", "GPT-5.6 Luna", "GPT-5.5"]
    static let openCodeModels = ["GPT-6 Astra", "GPT-5.6 Terra", "GPT-5.6 Sol", "GPT-5.6 Luna"]
    static let cursorModels = [
        "Auto", "Cursor Grok 4.6", "Composer 2.5", "Claude Opus 5", "GPT-5.6 Sol",
        "Claude Fable 5", "GPT-5.6 Terra", "GPT-5.6 Luna", "Claude Opus 4.8", "GPT-5.5",
        "Claude Fable 5.1", "Cursor Grok 4.5", "Gemini 3.8 Flash", "Gemini 3.7 Flash",
        "Claude Sonnet 5", "Claude Sonnet 4.6", "Codex 5.3", "Claude Opus 4.7", "GPT-5.4",
        "Claude Opus 4.6", "Claude Opus 4.5", "GPT-5.2", "Gemini 3.6 Flash", "Gemini 3.1 Pro",
        "GPT-5.4 Mini", "GPT-5.4 Nano", "Claude Haiku 4.5", "Claude Sonnet 4.5", "GPT-5.1",
        "Gemini 3 Flash", "Gemini 3.5 Flash", "Claude Sonnet 4", "GPT-5 Mini", "Gemini 2.5 Flash",
        "Kimi K3", "Kimi K2.7 Code", "GLM 5.2",
    ]

    static let cursorModelGroups: [CursorModelGroup] = [
        CursorModelGroup(provider: "Automatic", models: [CursorModelSetting(index: 0, name: "Auto", provider: "Automatic")]),
        CursorModelGroup(provider: "Anthropic", models: [
            CursorModelSetting(index: 5, name: "Claude Fable 5", provider: "Anthropic"),
            CursorModelSetting(index: 10, name: "Claude Fable 5.1", provider: "Anthropic"),
            CursorModelSetting(index: 26, name: "Claude Haiku 4.5", provider: "Anthropic"),
            CursorModelSetting(index: 20, name: "Claude Opus 4.5", provider: "Anthropic"),
            CursorModelSetting(index: 19, name: "Claude Opus 4.6", provider: "Anthropic"),
            CursorModelSetting(index: 17, name: "Claude Opus 4.7", provider: "Anthropic"),
            CursorModelSetting(index: 8, name: "Claude Opus 4.8", provider: "Anthropic"),
            CursorModelSetting(index: 3, name: "Claude Opus 5", provider: "Anthropic"),
            CursorModelSetting(index: 31, name: "Claude Sonnet 4", provider: "Anthropic"),
            CursorModelSetting(index: 27, name: "Claude Sonnet 4.5", provider: "Anthropic"),
            CursorModelSetting(index: 15, name: "Claude Sonnet 4.6", provider: "Anthropic"),
            CursorModelSetting(index: 14, name: "Claude Sonnet 5", provider: "Anthropic"),
        ]),
        CursorModelGroup(provider: "Cursor", models: [
            CursorModelSetting(index: 11, name: "Cursor Grok 4.5", provider: "Cursor"),
            CursorModelSetting(index: 1, name: "Cursor Grok 4.6", provider: "Cursor"),
        ]),
        CursorModelGroup(provider: "Google", models: [
            CursorModelSetting(index: 33, name: "Gemini 2.5 Flash", provider: "Google"),
            CursorModelSetting(index: 29, name: "Gemini 3 Flash", provider: "Google"),
            CursorModelSetting(index: 23, name: "Gemini 3.1 Pro", provider: "Google"),
            CursorModelSetting(index: 30, name: "Gemini 3.5 Flash", provider: "Google"),
            CursorModelSetting(index: 22, name: "Gemini 3.6 Flash", provider: "Google"),
            CursorModelSetting(index: 13, name: "Gemini 3.7 Flash", provider: "Google"),
            CursorModelSetting(index: 12, name: "Gemini 3.8 Flash", provider: "Google"),
        ]),
        CursorModelGroup(provider: "Kimi", models: [
            CursorModelSetting(index: 35, name: "Kimi K2.7 Code", provider: "Kimi"),
            CursorModelSetting(index: 34, name: "Kimi K3", provider: "Kimi"),
        ]),
        CursorModelGroup(provider: "OpenAI", models: [
            CursorModelSetting(index: 16, name: "Codex 5.3", provider: "OpenAI"),
            CursorModelSetting(index: 32, name: "GPT-5 Mini", provider: "OpenAI"),
            CursorModelSetting(index: 28, name: "GPT-5.1", provider: "OpenAI"),
            CursorModelSetting(index: 21, name: "GPT-5.2", provider: "OpenAI"),
            CursorModelSetting(index: 18, name: "GPT-5.4", provider: "OpenAI"),
            CursorModelSetting(index: 24, name: "GPT-5.4 Mini", provider: "OpenAI"),
            CursorModelSetting(index: 25, name: "GPT-5.4 Nano", provider: "OpenAI"),
            CursorModelSetting(index: 9, name: "GPT-5.5", provider: "OpenAI"),
            CursorModelSetting(index: 7, name: "GPT-5.6 Luna", provider: "OpenAI"),
            CursorModelSetting(index: 4, name: "GPT-5.6 Sol", provider: "OpenAI"),
            CursorModelSetting(index: 6, name: "GPT-5.6 Terra", provider: "OpenAI"),
        ]),
        CursorModelGroup(provider: "Other", models: [
            CursorModelSetting(index: 2, name: "Composer 2.5", provider: "Other"),
            CursorModelSetting(index: 36, name: "GLM 5.2", provider: "Other"),
        ]),
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
        chatGPTThinkingMask = Self.readMask(defaults: defaults, key: chatGPTKey, fallback: Self.defaultChatGPTThinkingMask)
        cursorModelMask = Self.readMask(defaults: defaults, key: cursorKey, fallback: Self.defaultCursorModelMask)
        chatGPTThinkingMask = Self.normalizedEffortMask(chatGPTThinkingMask)
        cursorModelMask = Self.normalizedCursorMask(cursorModelMask)
    }

    func isEffortEnabled(_ index: Int) -> Bool {
        index >= 0 && index < Self.chatGPTEfforts.count && chatGPTThinkingMask & (UInt64(1) << UInt64(index)) != 0
    }

    func setEffortEnabled(_ enabled: Bool, index: Int) {
        guard index >= 0, index < Self.chatGPTEfforts.count else { return }
        var mask = chatGPTThinkingMask
        let bit = UInt64(1) << UInt64(index)
        if enabled { mask |= bit } else { mask &= ~bit }
        updateChatGPTMask(mask)
    }

    func effortBinding(index: Int) -> Binding<Bool> {
        Binding(get: { self.isEffortEnabled(index) }, set: { self.setEffortEnabled($0, index: index) })
    }

    func isCursorModelEnabled(_ index: Int) -> Bool {
        index == 0 || (index > 0 && index < Self.cursorModels.count && cursorModelMask & (UInt64(1) << UInt64(index)) != 0)
    }

    func setCursorModelEnabled(_ enabled: Bool, index: Int) {
        guard index > 0, index < Self.cursorModels.count else { return }
        var mask = cursorModelMask
        let bit = UInt64(1) << UInt64(index)
        if enabled { mask |= bit } else { mask &= ~bit }
        updateCursorMask(mask)
    }

    func cursorModelBinding(index: Int) -> Binding<Bool> {
        Binding(get: { self.isCursorModelEnabled(index) }, set: { self.setCursorModelEnabled($0, index: index) })
    }

    func resetChatGPTEfforts() { updateChatGPTMask(Self.defaultChatGPTThinkingMask) }
    func resetCursorModels() { updateCursorMask(Self.defaultCursorModelMask) }

    func syncCursorModels(_ mask: UInt64) { updateCursorMask(mask) }

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
        guard let raw = defaults.string(forKey: key), let mask = UInt64(raw, radix: 16) else { return fallback }
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
        TabView {
            chatGPTTab.tabItem { Text("ChatGPT") }
            cursorTab.tabItem { Text("Cursor") }
            openCodeTab.tabItem { Text("OpenCode") }
        }
        .frame(minWidth: 600, minHeight: 620)
    }

    private var chatGPTTab: some View {
        settingsScroll {
            settingsSection(title: "Models", detail: "ChatGPT model availability is fixed and cannot be changed here.") {
                twoColumnGrid {
                    ForEach(BridgePreferences.chatGPTModels, id: \.self) { model in
                        Toggle(model, isOn: .constant(true)).disabled(true)
                    }
                }
            }
            Divider()
            settingsSection(title: "Efforts", detail: "Enabled levels are available on the ESP32 effort dial.") {
                twoColumnGrid {
                    ForEach(Array(BridgePreferences.chatGPTEfforts.enumerated()), id: \.offset) { index, effort in
                        Toggle(effort, isOn: preferences.effortBinding(index: index))
                    }
                }
            }
            resetButton("Reset ChatGPT Defaults") { preferences.resetChatGPTEfforts() }
        }
    }

    private var cursorTab: some View {
        settingsScroll {
            settingsSection(title: "Models", detail: "Enabled models are available on the ESP32 model dial.") {
                ForEach(BridgePreferences.cursorModelGroups) { group in
                    Text(group.provider)
                        .font(.subheadline.weight(.semibold))
                        .padding(.top, group.provider == "Automatic" ? 0 : 8)
                    twoColumnGrid {
                        ForEach(group.models) { model in
                            Toggle(model.name, isOn: preferences.cursorModelBinding(index: model.index))
                                .disabled(model.index == 0)
                        }
                    }
                }
            }
            resetButton("Reset Cursor Defaults") { preferences.resetCursorModels() }
        }
    }

    private var openCodeTab: some View {
        settingsScroll {
            settingsSection(title: "Models", detail: "OpenCode's supported model list is fixed in the bridge.") {
                twoColumnGrid {
                    ForEach(BridgePreferences.openCodeModels, id: \.self) { model in
                        Toggle(model, isOn: .constant(true)).disabled(true)
                    }
                }
            }
        }
    }

    private func settingsScroll<Content: View>(@ViewBuilder content: () -> Content) -> some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 18) {
                content()
            }
            .padding(22)
        }
    }

    private func twoColumnGrid<Content: View>(@ViewBuilder content: () -> Content) -> some View {
        LazyVGrid(
            columns: [GridItem(.flexible(), spacing: 18), GridItem(.flexible(), spacing: 18)],
            alignment: .leading,
            spacing: 8,
            content: content
        )
    }

    @ViewBuilder
    private func settingsSection<Content: View>(title: String, detail: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 10) {
            Text(title).font(.headline)
            Text(detail).font(.subheadline).foregroundStyle(.secondary)
            content()
        }
    }

    private func resetButton(_ title: String, action: @escaping () -> Void) -> some View {
        HStack {
            Spacer()
            Button(title, action: action)
            Spacer()
        }
    }
}

final class SettingsWindowController: NSWindowController {
    init(preferences: BridgePreferences, onChange: @escaping () -> Void) {
        let hosting = NSHostingView(rootView: SettingsView(preferences: preferences))
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 680, height: 720),
            styleMask: [.titled, .closable, .resizable],
            backing: .buffered,
            defer: false
        )
        window.title = "Model Dial Settings"
        window.contentView = hosting
        window.contentMinSize = NSSize(width: 600, height: 620)
        window.isReleasedWhenClosed = false
        preferences.onChange = onChange
        super.init(window: window)
    }

    required init?(coder: NSCoder) { super.init(coder: coder) }

    func showWindow() {
        window?.center()
        showWindow(nil)
        NSApp.activate(ignoringOtherApps: true)
    }
}
#endif
