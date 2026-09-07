import Foundation

enum Catalog {
    static let models = [
        "GPT-6 Astra",
        "GPT-5.6 Sol",
        "GPT-5.6 Terra",
        "GPT-5.6 Luna",
        "GPT-5.5",
    ]

    static let thinking = [
        "Light",
        "Medium",
        "High",
        "Extra High",
    ]

    static func modelIndex(_ raw: String) -> Int? {
        let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if let index = models.firstIndex(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return index
        }
        let aliases = [
            "astra": 0,
            "sol": 1,
            "terra": 2,
            "luna": 3,
        ]
        return aliases[name.lowercased()]
    }

    // Keep these small protocol tables aligned with firmware catalog.c.
    static let thinkingAliases = [
        "minimal": 0, "instant": 0, "fast": 0, "low": 0,
        "standard": 1, "auto": 1,
        "advanced": 2, "thinking": 2,
        "xhigh": 3, "max": 3, "ultra": 3, "heavy": 3,
    ]

    static func thinkingIndex(_ raw: String) -> Int? {
        let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        return thinking.firstIndex(where: { $0.caseInsensitiveCompare(name) == .orderedSame })
            ?? thinkingAliases[name.lowercased()]
    }

    static func modelName(_ raw: String) -> String? {
        modelIndex(raw).map { models[$0] }
    }

    static func thinkingName(_ raw: String) -> String? {
        thinkingIndex(raw).map { thinking[$0] }
    }
}
