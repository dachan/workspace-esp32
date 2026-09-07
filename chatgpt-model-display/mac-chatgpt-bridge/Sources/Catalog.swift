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

    static func thinkingIndex(_ raw: String) -> Int? {
        let buf = raw.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        if buf.contains("extra high") || buf.contains("xhigh") || buf == "max"
            || buf == "ultra" || buf.contains("heavy") {
            return 3
        }
        if buf.contains("high") || buf == "advanced" || buf == "thinking" {
            return 2
        }
        if buf.contains("medium") || buf.contains("standard") || buf == "auto" {
            return 1
        }
        if buf.contains("light") || buf == "minimal" || buf.contains("instant")
            || buf.contains("fast") || buf == "low" {
            return 0
        }
        return thinking.firstIndex(where: { $0.caseInsensitiveCompare(raw) == .orderedSame })
    }

    static func modelName(_ raw: String) -> String? {
        modelIndex(raw).map { models[$0] }
    }

    static func thinkingName(_ raw: String) -> String? {
        thinkingIndex(raw).map { thinking[$0] }
    }
}
