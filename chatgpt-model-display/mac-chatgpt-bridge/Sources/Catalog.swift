import Foundation

enum Catalog {
    static let models = [
        "GPT-6 Astra",
        "GPT-5.6 Sol",
        "GPT-5.6 Terra",
        "GPT-5.6 Luna",
        "GPT-5.5",
    ]

    private struct CursorModel {
        let name: String
        let efforts: [String]
    }

    // Auto, then Cursor Settings toggle order. Keep these aligned with firmware catalog.c.
    // Empty efforts means the effort knob is ignored (Unsupported).
    private static let cursorCatalog: [CursorModel] = [
        .init(name: "Auto", efforts: []),
        .init(name: "Cursor Grok 4.6", efforts: ["Low", "Medium", "High", "Extra High"]),
        .init(name: "Composer 2.5", efforts: []),
        .init(name: "Claude Opus 5", efforts: ["Low", "Medium", "High", "Extra High", "Max"]),
        .init(name: "GPT-5.6 Sol", efforts: ["None", "Low", "Medium", "High", "Extra High", "Max"]),
        .init(name: "Claude Fable 5", efforts: ["Low", "Medium", "High", "Extra High", "Max"]),
        .init(name: "GPT-5.6 Terra", efforts: ["None", "Low", "Medium", "High", "Extra High", "Max"]),
        .init(name: "GPT-5.6 Luna", efforts: ["None", "Low", "Medium", "High", "Extra High", "Max"]),
        .init(name: "Claude Opus 4.8", efforts: ["Low", "Medium", "High", "Extra High", "Max"]),
        .init(name: "GPT-5.5", efforts: ["None", "Low", "Medium", "High", "Extra High"]),
        .init(name: "Claude Fable 5.1", efforts: ["Low", "Medium", "High", "Extra High", "Max"]),
        .init(name: "Cursor Grok 4.5", efforts: ["Low", "Medium", "High"]),
        .init(name: "Gemini 3.8 Flash", efforts: ["Low", "Medium", "High"]),
        .init(name: "Gemini 3.7 Flash", efforts: ["Low", "Medium", "High"]),
        .init(name: "Claude Sonnet 5", efforts: ["Low", "Medium", "High", "Extra High", "Max"]),
        .init(name: "Claude Sonnet 4.6", efforts: ["Low", "Medium", "High", "Max"]),
        .init(name: "Codex 5.3", efforts: ["Low", "Medium", "High", "Extra High"]),
        .init(name: "Claude Opus 4.7", efforts: ["Low", "Medium", "High", "Extra High", "Max"]),
        .init(name: "GPT-5.4", efforts: ["None", "Low", "Medium", "High", "Extra High"]),
        .init(name: "Claude Opus 4.6", efforts: ["Low", "Medium", "High", "Max"]),
        .init(name: "Claude Opus 4.5", efforts: []),
        .init(name: "GPT-5.2", efforts: ["Low", "Medium", "High", "Extra High"]),
        .init(name: "Gemini 3.6 Flash", efforts: ["Minimal", "Low", "Medium", "High"]),
        .init(name: "Gemini 3.1 Pro", efforts: []),
        .init(name: "GPT-5.4 Mini", efforts: ["None", "Low", "Medium", "High", "Extra High"]),
        .init(name: "GPT-5.4 Nano", efforts: ["None", "Low", "Medium", "High", "Extra High"]),
        .init(name: "Claude Haiku 4.5", efforts: []),
        .init(name: "Claude Sonnet 4.5", efforts: []),
        .init(name: "GPT-5.1", efforts: ["Low", "Medium", "High"]),
        .init(name: "Gemini 3 Flash", efforts: []),
        .init(name: "Gemini 3.5 Flash", efforts: []),
        .init(name: "Claude Sonnet 4", efforts: []),
        .init(name: "GPT-5 Mini", efforts: []),
        .init(name: "Gemini 2.5 Flash", efforts: []),
        .init(name: "Kimi K3", efforts: ["Low", "High", "Max"]),
        .init(name: "Kimi K2.7 Code", efforts: []),
        .init(name: "GLM 5.2", efforts: ["High", "Max"]),
    ]

    static var cursorModels: [String] { cursorCatalog.map(\.name) }

    static let thinking = [
        "Light",
        "Medium",
        "High",
        "Extra High",
    ]

    static let cursorThinking = [
        "Low",
        "Medium",
        "High",
        "Extra High",
        "Max",
        "None",
        "Minimal",
    ]

    /// Nil means the model is unknown; an empty list means effort is unsupported.
    static func cursorEfforts(for model: String) -> [String]? {
        guard let index = cursorModelIndex(model) else { return nil }
        return cursorCatalog[index].efforts
    }

    static func cursorEffort(_ raw: String, model: String) -> (index: Int, name: String)? {
        guard let levels = cursorEfforts(for: model), !levels.isEmpty,
              let canonical = cursorThinkingName(raw) else { return nil }
        let name: String
        if levels.contains(canonical) { name = canonical }
        else if canonical == "None" || canonical == "Minimal" { name = levels[0] }
        else { name = levels[levels.count - 1] }
        return levels.firstIndex(of: name).map { ($0, name) }
    }

    static func chatgptModelIndex(_ raw: String) -> Int? {
        index(raw, in: models, aliases: [
            "astra": 0, "sol": 1, "terra": 2, "luna": 3,
        ])
    }

    static func cursorModelIndex(_ raw: String) -> Int? {
        index(raw, in: cursorModels, aliases: [
            "auto": 0, "grok": 1, "composer": 2, "opus": 3,
            "sol": 4, "fable": 5, "terra": 6, "luna": 7,
        ])
    }

    /// Auto plus the seven Cursor Settings toggles that ship enabled.
    static var cursorEnabledMask: UInt64 = 0xFF

    static func setCursorEnabledMask(_ mask: UInt64) {
        cursorEnabledMask = mask | 1
    }

    static func cursorNameEnabled(_ raw: String) -> Bool {
        guard let full = cursorModelIndex(raw) else { return false }
        if full == 0 { return true }
        guard full < 64 else { return false }
        return cursorEnabledMask & (1 << full) != 0
    }

    /// Command-/ Down index among Auto + models enabled on the panel.
    static func cursorPickerIndex(_ raw: String) -> Int? {
        guard let full = cursorModelIndex(raw), cursorNameEnabled(raw) else { return nil }
        var n = 0
        for i in 0..<full {
            if i == 0 || (i < 64 && cursorEnabledMask & (1 << i) != 0) { n += 1 }
        }
        return n
    }

    static func modelIndex(_ raw: String) -> Int? {
        chatgptModelIndex(raw) ?? cursorModelIndex(raw)
    }

    // Keep these small protocol tables aligned with firmware catalog.c.
    static let thinkingAliases = [
        "minimal": 0, "instant": 0, "fast": 0, "low": 0, "light": 0,
        "standard": 1, "auto": 1,
        "advanced": 2, "thinking": 2,
        "xhigh": 3, "ultra": 3, "heavy": 3,
        "max": 4,
    ]

    static func chatgptThinkingIndex(_ raw: String) -> Int? {
        let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if let index = thinking.firstIndex(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return index
        }
        if name.lowercased() == "none" { return 0 }
        if name.lowercased() == "max" {
            return thinking.count - 1
        }
        return thinkingAliases[name.lowercased()].map { min($0, thinking.count - 1) }
    }

    static func cursorThinkingIndex(_ raw: String) -> Int? {
        let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if let index = cursorThinking.firstIndex(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return index
        }
        return thinkingAliases[name.lowercased()].map { min($0, cursorThinking.count - 1) }
    }

    static func thinkingIndex(_ raw: String) -> Int? {
        chatgptThinkingIndex(raw) ?? cursorThinkingIndex(raw)
    }

    static func chatgptModelName(_ raw: String) -> String? {
        chatgptModelIndex(raw).map { models[$0] }
    }

    static func cursorModelName(_ raw: String) -> String? {
        cursorModelIndex(raw).map { cursorModels[$0] }
    }

    static func modelName(_ raw: String) -> String? {
        chatgptModelName(raw) ?? cursorModelName(raw)
    }

    static func chatgptThinkingName(_ raw: String) -> String? {
        chatgptThinkingIndex(raw).map { thinking[$0] }
    }

    static func cursorThinkingName(_ raw: String) -> String? {
        cursorThinkingIndex(raw).map { cursorThinking[$0] }
    }

    static func thinkingName(_ raw: String) -> String? {
        let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if cursorThinking.contains(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return cursorThinkingName(raw)
        }
        return chatgptThinkingName(raw) ?? cursorThinkingName(raw)
    }

    private static func index(_ raw: String, in names: [String], aliases: [String: Int]) -> Int? {
        let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if let index = names.firstIndex(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return index
        }
        return aliases[name.lowercased()]
    }
}
