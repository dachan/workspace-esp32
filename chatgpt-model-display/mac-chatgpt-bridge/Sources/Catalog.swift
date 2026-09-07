import Foundation

enum Catalog {
    static let models = [
        "GPT-6 Astra",
        "GPT-5.6 Sol",
        "GPT-5.6 Terra",
        "GPT-5.6 Luna",
        "GPT-5.5",
    ]

    static let cursorModels = [
        "Auto",
        "Cursor Grok 4.6",
        "Composer 2.5",
        "Claude Opus 5",
        "GPT-5.6 Sol",
        "Claude Fable 5",
        "GPT-5.6 Terra",
        "GPT-5.6 Luna",
    ]

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
    ]

    /// Nil means the model is unknown; an empty list means effort is unsupported.
    static func cursorEfforts(for model: String) -> [String]? {
        guard let index = cursorModelIndex(model) else { return nil }
        switch index {
        case 0, 2: return []
        case 1: return Array(cursorThinking.prefix(4))
        case 3, 5: return Array(cursorThinking.prefix(5))
        default: return ["None"] + cursorThinking.prefix(5)
        }
    }

    static func cursorEffort(_ raw: String, model: String) -> (index: Int, name: String)? {
        guard let levels = cursorEfforts(for: model), !levels.isEmpty,
              let canonical = cursorThinkingName(raw) else { return nil }
        let name: String
        if levels.contains(canonical) { name = canonical }
        else if canonical == "None" { name = levels[0] }
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
