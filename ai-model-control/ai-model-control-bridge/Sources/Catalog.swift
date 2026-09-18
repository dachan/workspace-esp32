import Foundation

enum Catalog {
    static let models = [
        "GPT-6 Astra",
        "GPT-5.6 Sol",
        "GPT-5.6 Terra",
        "GPT-5.6 Luna",
        "GPT-5.5",
    ]

    // OpenCode encoder order. Its native picker remains Luna-first.
    static let openCodeModels = [
        "GPT-6 Astra",
        "GPT-5.6 Terra",
        "GPT-5.6 Sol",
        "GPT-5.6 Luna",
    ]
    // OpenRouter Latest aliases after Rig `modelDisplayName` (live catalog).
    static let rigModels = [
        "Grok Latest",
        "GPT Astra Latest",
        "GPT Sol Latest",
        "GPT Terra Latest",
        "GPT Luna Latest",
        "Claude Sonnet Latest",
        "Claude Opus Latest",
        "Claude Fable Latest",
        "Flash Latest",
        "Gemini Flash Latest",
        "Gemini Pro Latest",
        "Kimi Latest",
    ]
    static let rigThinking = [
        "Auto", "None", "Light", "Medium", "High", "Extra High", "Max",
    ]

    private struct CursorModel {
        let name: String
        let efforts: [String]
    }

    // Auto, then Model Dial Settings toggle order. Keep these aligned with firmware catalog.c.
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
        "Max",
        "Ultra",
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

    static let defaultChatGPTThinkingMask: UInt64 = 0x0F
    static var chatGPTThinkingMask: UInt64 = defaultChatGPTThinkingMask

    static var chatGPTThinkingEnabled: [String] {
        thinking.enumerated().compactMap { index, name in
            chatGPTThinkingMask & (1 << index) != 0 ? name : nil
        }
    }

    static func setChatGPTThinkingMask(_ mask: UInt64) {
        let limit = (UInt64(1) << UInt64(thinking.count)) - 1
        var next = mask & limit
        if next == 0 {
            next = 1
        }
        chatGPTThinkingMask = next
    }

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

    static func openCodeModelIndex(_ raw: String) -> Int? {
        index(raw, in: openCodeModels, aliases: [
            "astra": 0, "terra": 1, "sol": 2, "luna": 3,
        ])
    }

    static func rigModelIndex(_ raw: String) -> Int? {
        let stripped = rigDisplayName(raw)
        if let exact = rigModels.firstIndex(where: {
            $0.caseInsensitiveCompare(raw) == .orderedSame
                || $0.caseInsensitiveCompare(stripped) == .orderedSame
        }) {
            return exact
        }
        return rigPanelName(slug: raw, name: raw).flatMap { name in
            rigModels.firstIndex { $0.caseInsensitiveCompare(name) == .orderedSame }
        }
    }

    static func rigCanonicalSlug(_ panelName: String) -> String? {
        rigSlots.first { $0.name.caseInsensitiveCompare(panelName) == .orderedSame }?.slug
    }

    /// Command-apostrophe's native list is the reverse of the encoder order.
    static func openCodePickerIndex(_ raw: String) -> Int? {
        openCodeModelIndex(raw).map { openCodeModels.count - 1 - $0 }
    }

    static func cursorModelIndex(_ raw: String) -> Int? {
        index(raw, in: cursorModels, aliases: [
            "auto": 0, "grok": 1, "composer": 2, "opus": 3,
            "sol": 4, "fable": 5, "terra": 6, "luna": 7,
        ])
    }

    /// Auto plus the seven Model Dial Settings entries that ship enabled.
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
        chatgptModelIndex(raw) ?? cursorModelIndex(raw) ?? openCodeModelIndex(raw) ?? rigModelIndex(raw)
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
        let allIndex: Int?
        if let index = thinking.firstIndex(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            allIndex = index
        } else if name.lowercased() == "none" {
            allIndex = 0
        } else if name.lowercased() == "max" {
            allIndex = thinking.count - 2
        } else {
            allIndex = thinkingAliases[name.lowercased()].map { min($0, thinking.count - 1) }
        }
        guard let allIndex else {
            return nil
        }
        let enabledIndices = thinking.indices.filter { chatGPTThinkingMask & (1 << $0) != 0 }
        guard !enabledIndices.isEmpty else { return 0 }
        if let exact = enabledIndices.firstIndex(of: allIndex) {
            return exact
        }
        let nearest = enabledIndices.min { lhs, rhs in
            let leftDistance = abs(lhs - allIndex)
            let rightDistance = abs(rhs - allIndex)
            return leftDistance == rightDistance ? lhs < rhs : leftDistance < rightDistance
        }
        return nearest.flatMap { enabledIndices.firstIndex(of: $0) }
    }

    static func cursorThinkingIndex(_ raw: String) -> Int? {
        let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if let index = cursorThinking.firstIndex(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return index
        }
        return thinkingAliases[name.lowercased()].map { min($0, cursorThinking.count - 1) }
    }

    static func thinkingIndex(_ raw: String) -> Int? {
        let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if let index = rigThinking.firstIndex(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return index
        }
        return chatgptThinkingIndex(raw) ?? cursorThinkingIndex(raw)
    }

    static func chatgptModelName(_ raw: String) -> String? {
        chatgptModelIndex(raw).map { models[$0] }
    }

    static func cursorModelName(_ raw: String) -> String? {
        cursorModelIndex(raw).map { cursorModels[$0] }
    }

    static func openCodeModelName(_ raw: String) -> String? {
        openCodeModelIndex(raw).map { openCodeModels[$0] }
    }

    static func rigModelName(_ raw: String) -> String? {
        rigModelIndex(raw).map { rigModels[$0] }
    }

    static func modelName(_ raw: String) -> String? {
        chatgptModelName(raw) ?? cursorModelName(raw) ?? openCodeModelName(raw) ?? rigModelName(raw)
    }

    static func chatgptThinkingName(_ raw: String) -> String? {
        chatgptThinkingIndex(raw).map { chatGPTThinkingEnabled[$0] }
    }

    static func cursorThinkingName(_ raw: String) -> String? {
        cursorThinkingIndex(raw).map { cursorThinking[$0] }
    }

    static func thinkingName(_ raw: String) -> String? {
        let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if let exact = rigThinking.first(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return exact
        }
        if cursorThinking.contains(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return cursorThinkingName(raw)
        }
        return chatgptThinkingName(raw) ?? cursorThinkingName(raw)
    }

    static func rigEnabledMask<T: Collection>(from models: T) -> UInt64
    where T.Element == RigClient.Model {
        var mask: UInt64 = 0
        for (index, slot) in rigSlots.enumerated() where index < 64 {
            let id = normalizeSlug(slot.slug)
            if models.contains(where: { normalizeSlug($0.slug) == id }) {
                mask |= 1 << UInt64(index)
            }
        }
        return mask == 0 ? 1 : mask
    }

    static func rigPanelModel(from focus: RigClient.Focus, active: [RigClient.Model]) -> String {
        if let latest = rigLatestName(forSlug: focus.main) { return latest }
        let stripped = rigDisplayName(focus.name ?? "", slug: focus.main)
        if !stripped.isEmpty { return stripped }
        if let option = active.first(where: { normalizeSlug($0.slug) == normalizeSlug(focus.main) }) {
            if let latest = rigLatestName(forSlug: option.slug) { return latest }
            let named = rigDisplayName(option.name, slug: option.slug)
            if !named.isEmpty { return named }
        }
        return rigModels[0]
    }

    static func rigPanelThinking(from effort: String) -> String {
        switch effort.trimmingCharacters(in: .whitespacesAndNewlines).lowercased() {
        case "auto": return "Auto"
        case "none": return "None"
        case "low", "light", "minimal": return "Light"
        case "medium": return "Medium"
        case "high": return "High"
        case "xhigh", "x-high": return "Extra High"
        case "max": return "Max"
        default: return "Auto"
        }
    }

    static func rigPanelName(slug: String, name: String?) -> String? {
        if let latest = rigLatestName(forSlug: slug) { return latest }
        let shown = rigDisplayName(name ?? "", slug: slug)
        return shown.isEmpty ? nil : shown
    }

    static func enabledSlug(forPanel name: String, in models: [RigClient.Model]) -> String? {
        if let exact = models.first(where: { $0.slug.caseInsensitiveCompare(name) == .orderedSame }) {
            return exact.slug
        }
        if let named = models.first(where: {
            $0.name.caseInsensitiveCompare(name) == .orderedSame
                || rigDisplayName($0.name, slug: $0.slug)
                    .caseInsensitiveCompare(rigDisplayName(name)) == .orderedSame
        }) {
            return named.slug
        }
        if let mapped = models.first(where: { rigPanelName(slug: $0.slug, name: $0.name) == name }) {
            return mapped.slug
        }
        if let canonical = rigCanonicalSlug(name),
           models.contains(where: { $0.slug.caseInsensitiveCompare(canonical) == .orderedSame })
        {
            return canonical
        }
        return nil
    }

    private struct RigSlot {
        let name: String
        let slug: String
    }

    // Keep aligned with firmware `rig_models` and Rig Latest aliases.
    private static let rigSlots: [RigSlot] = [
        .init(name: "Grok Latest", slug: "~x-ai/grok-latest"),
        .init(name: "GPT Astra Latest", slug: "~openai/gpt-astra-latest"),
        .init(name: "GPT Sol Latest", slug: "~openai/gpt-sol-latest"),
        .init(name: "GPT Terra Latest", slug: "~openai/gpt-terra-latest"),
        .init(name: "GPT Luna Latest", slug: "~openai/gpt-luna-latest"),
        .init(name: "Claude Sonnet Latest", slug: "~anthropic/claude-sonnet-latest"),
        .init(name: "Claude Opus Latest", slug: "~anthropic/claude-opus-latest"),
        .init(name: "Claude Fable Latest", slug: "~anthropic/claude-fable-latest"),
        .init(name: "Flash Latest", slug: "~deepseek/deepseek-flash-latest"),
        .init(name: "Gemini Flash Latest", slug: "~google/gemini-flash-latest"),
        .init(name: "Gemini Pro Latest", slug: "~google/gemini-pro-latest"),
        .init(name: "Kimi Latest", slug: "~moonshotai/kimi-latest"),
    ]

    private static func index(_ raw: String, in names: [String], aliases: [String: Int]) -> Int? {
        let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if let index = names.firstIndex(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return index
        }
        return aliases[name.lowercased()]
    }

    /// Same rules as Rig `modelDisplayName` on the OpenRouter catalog.
    static func rigDisplayName(_ raw: String, slug: String? = nil) -> String {
        let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return trimmed }
        var provider: String?
        var model = trimmed
        if let colon = trimmed.range(of: ":"),
           trimmed.distance(from: trimmed.startIndex, to: colon.lowerBound) <= 40,
           colon.upperBound < trimmed.endIndex,
           trimmed[colon.upperBound] == " "
        {
            provider = String(trimmed[..<colon.lowerBound]).trimmingCharacters(in: .whitespaces)
            model = String(trimmed[trimmed.index(after: colon.upperBound)...]).trimmingCharacters(in: .whitespaces)
        }
        model = stripLeadingProvider(model, provider)
        if let slug {
            let key = openRouterProvider(slug)
            model = stripLeadingProvider(model, providerLabels[key] ?? key)
        }
        if !model.isEmpty { return model }
        if let slug { return slug.split(separator: "/").last.map(String.init) ?? trimmed }
        return trimmed
    }

    static func rigLatestName(forSlug slug: String) -> String? {
        let id = normalizeSlug(slug)
        return rigSlots.first { normalizeSlug($0.slug) == id }?.name
    }

    private static func normalizeSlug(_ slug: String) -> String {
        slug.trimmingCharacters(in: CharacterSet(charactersIn: "~")).lowercased()
    }

    private static func openRouterProvider(_ slug: String) -> String {
        let normalized = normalizeSlug(slug)
        let raw = normalized.split(separator: "/", maxSplits: 1).first.map(String.init) ?? normalized
        return raw.drop { !$0.isLetter && !$0.isNumber }.isEmpty ? raw : String(raw.drop { !$0.isLetter && !$0.isNumber })
    }

    private static let providerLabels: [String: String] = [
        "01-ai": "01.AI",
        "ai21": "AI21",
        "amazon": "Amazon",
        "anthropic": "Anthropic",
        "arcee-ai": "Arcee",
        "cognitivecomputations": "Cognitive Computations",
        "cohere": "Cohere",
        "deepseek": "DeepSeek",
        "google": "Google",
        "groq": "Groq",
        "huggingface": "Hugging Face",
        "ibm": "IBM",
        "inception": "Inception",
        "meta": "Meta",
        "meta-llama": "Meta",
        "microsoft": "Microsoft",
        "minimax": "MiniMax",
        "mistralai": "Mistral",
        "moonshotai": "Moonshot",
        "morph": "Morph",
        "nvidia": "NVIDIA",
        "openai": "OpenAI",
        "openrouter": "OpenRouter",
        "perplexity": "Perplexity",
        "qwen": "Qwen",
        "x-ai": "xAI",
        "z-ai": "Z.ai",
    ]

    private static func stripLeadingProvider(_ model: String, _ provider: String?) -> String {
        let label = provider?.trimmingCharacters(in: .whitespaces) ?? ""
        guard !label.isEmpty else { return model }
        let prefix = label + " "
        if model.count > prefix.count, model.lowercased().hasPrefix(prefix.lowercased()) {
            return String(model.dropFirst(prefix.count)).trimmingCharacters(in: .whitespaces)
        }
        return model
    }
}
