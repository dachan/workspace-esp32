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
    // OpenRouter Latest aliases. Panel labels keep the slug provider and omit Latest.
    static let rigModels = [
        "xAI Grok", "OpenAI Astra", "OpenAI Sol", "OpenAI Terra", "OpenAI Luna",
        "Anthropic Sonnet", "Anthropic Opus", "Anthropic Fable", "DeepSeek Flash",
        "Google Gemini Flash", "Google Pro", "Moonshot Kimi",
    ]
    static let rigThinking = [
        "Auto", "None", "Low", "Medium", "High", "X-High", "Max",
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
        .init(name: "Grok 4.7", efforts: ["Low", "Medium", "High", "Extra High"]),
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
        if let exact = rigModels.firstIndex(where: { panelNamesMatch($0, raw) }) {
            return exact
        }
        return rigPanelName(slug: raw, name: raw).flatMap { name in
            rigModels.firstIndex { panelNamesMatch($0, name) }
        }
    }

    static func rigCanonicalSlug(_ panelName: String) -> String? {
        rigSlots.first { panelNamesMatch($0.name, panelName) }?.slug
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
        let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if trimmed.isEmpty { return nil }
        return chatgptModelName(trimmed)
            ?? cursorModelName(trimmed)
            ?? openCodeModelName(trimmed)
            ?? rigModelName(trimmed)
            ?? trimmed
    }

    static func chatgptThinkingName(_ raw: String) -> String? {
        chatgptThinkingIndex(raw).map { chatGPTThinkingEnabled[$0] }
    }

    static func cursorThinkingName(_ raw: String) -> String? {
        cursorThinkingIndex(raw).map { cursorThinking[$0] }
    }

    static func thinkingName(_ raw: String) -> String? {
        let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if name.isEmpty { return nil }
        if let exact = rigThinking.first(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return exact
        }
        if cursorThinking.contains(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return cursorThinkingName(raw)
        }
        return chatgptThinkingName(raw) ?? cursorThinkingName(raw) ?? name
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

    static func rigEffortMask(from efforts: [String]) -> UInt8 {
        // Rig composer lists Auto–Max whenever the model has reasoning.
        efforts.isEmpty ? 0 : 0x7F
    }

    static func rigEffortMasks(from models: [RigClient.Model]) -> [UInt8] {
        rigSlots.map { slot in
            guard let model = models.first(where: { normalizeSlug($0.slug) == normalizeSlug(slot.slug) }) else {
                return 0
            }
            return model.efforts.isEmpty ? 0 : rigEffortMask(from: model.efforts)
        }
    }

    static func rigPanelModel(from focus: RigClient.Focus, active: [RigClient.Model]) -> String {
        let rows = rigCatalogRows(from: active)
        if let hit = rows.first(where: { normalizeSlug($0.slug) == normalizeSlug(focus.main) }) {
            return hit.name
        }
        return rows.first?.name ?? rigModels[0]
    }

    static func rigPanelThinking(from effort: String) -> String {
        switch effort.trimmingCharacters(in: .whitespacesAndNewlines).lowercased() {
        case "auto": return "Auto"
        case "none", "minimal": return "None"
        case "low", "light": return "Low"
        case "medium": return "Medium"
        case "high": return "High"
        case "xhigh", "x-high", "extra high", "extra-high": return "X-High"
        case "max": return "Max"
        default: return "Auto"
        }
    }

    static func rigPanelName(slug: String, name: String?, among: [String] = []) -> String? {
        let shown = rigDisplayName(name ?? "", slug: slug, among: among)
        return shown.isEmpty ? nil : shown
    }

    static func rigCatalogEntries(from models: [RigClient.Model]) -> [(name: String, mask: UInt8)] {
        rigCatalogRows(from: models).map { (name: $0.name, mask: $0.mask) }
    }

    static func rigCatalogRows(from models: [RigClient.Model]) -> [(slug: String, name: String, mask: UInt8)] {
        let peers = models.map(\.slug)
        var rows = models.compactMap { model -> (slug: String, name: String, mask: UInt8)? in
            let name = collapsed(stripLatest(displayNameFromSlug(model.slug, among: peers)))
            guard !name.isEmpty else { return nil }
            let mask: UInt8 = model.efforts.isEmpty ? 0 : 0x7F
            return (model.slug, name, mask)
        }
        let unique = uniquifyRigLabels(rows.map { (slug: $0.slug, name: $0.name) })
        for i in rows.indices {
            rows[i].name = withProviderPrefix(unique[i], slug: rows[i].slug)
        }
        return rows
            .sorted { $0.name.localizedCaseInsensitiveCompare($1.name) == .orderedDescending }
    }

    static func enabledSlug(forPanel name: String, in models: [RigClient.Model]) -> String? {
        if let exact = models.first(where: { $0.slug.caseInsensitiveCompare(name) == .orderedSame }) {
            return exact.slug
        }
        if let row = rigCatalogRows(from: models).first(where: { panelNamesMatch($0.name, name) }) {
            return row.slug
        }
        if let named = models.first(where: {
            panelNamesMatch($0.name, name)
                || panelNamesMatch(rigDisplayName($0.name, slug: $0.slug), name)
        }) {
            return named.slug
        }
        if let mapped = models.first(where: {
            guard let panel = rigPanelName(slug: $0.slug, name: $0.name) else { return false }
            return panelNamesMatch(panel, name)
        }) {
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
        .init(name: "xAI Grok", slug: "~x-ai/grok-latest"),
        .init(name: "OpenAI Terra", slug: "~openai/gpt-terra-latest"),
        .init(name: "OpenAI Sol", slug: "~openai/gpt-sol-latest"),
        .init(name: "OpenAI Luna", slug: "~openai/gpt-luna-latest"),
        .init(name: "OpenAI Astra", slug: "~openai/gpt-astra-latest"),
        .init(name: "Moonshot Kimi", slug: "~moonshotai/kimi-latest"),
        .init(name: "Google Pro", slug: "~google/gemini-pro-latest"),
        .init(name: "Google Gemini Flash", slug: "~google/gemini-flash-latest"),
        .init(name: "DeepSeek Flash", slug: "~deepseek/deepseek-flash-latest"),
        .init(name: "Anthropic Sonnet", slug: "~anthropic/claude-sonnet-latest"),
        .init(name: "Anthropic Opus", slug: "~anthropic/claude-opus-latest"),
        .init(name: "Anthropic Fable", slug: "~anthropic/claude-fable-latest"),
    ]

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

    private static func index(_ raw: String, in names: [String], aliases: [String: Int]) -> Int? {
        let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if let index = names.firstIndex(where: { $0.caseInsensitiveCompare(name) == .orderedSame }) {
            return index
        }
        return aliases[name.lowercased()]
    }

    /// Panel label: slug provider plus short name, no colon, no Latest.
    static func rigDisplayName(_ raw: String, slug: String? = nil, among: [String] = []) -> String {
        if let slug, slug.contains("/") {
            let peers = among.isEmpty ? rigSlots.map(\.slug) : among
            return withProviderPrefix(displayNameFromSlug(slug, among: peers), slug: slug)
        }
        return collapsed(stripLatest(normalizeColonProvider(raw)))
    }

    static func rigLatestName(forSlug slug: String) -> String? {
        let id = normalizeSlug(slug)
        guard rigSlots.contains(where: { normalizeSlug($0.slug) == id }) else { return nil }
        return withProviderPrefix(displayNameFromSlug(slug, among: rigSlots.map(\.slug)), slug: slug)
    }

    private static func providerLabel(for slug: String) -> String {
        let key = openRouterProvider(slug).lowercased()
        return providerLabels[key] ?? titlePathToken(key)
    }

    private static func withProviderPrefix(_ name: String, slug: String) -> String {
        let product = collapsed(stripLatest(name))
        let label = providerLabel(for: slug)
        guard !product.isEmpty else { return label }
        guard !label.isEmpty else { return product }
        if product.caseInsensitiveCompare(label) == .orderedSame { return product }
        if product.lowercased().hasPrefix(label.lowercased() + " ") { return product }
        return collapsed(label + " " + product)
    }

    private static func normalizeColonProvider(_ raw: String) -> String {
        let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if let colon = trimmed.range(of: ":"),
           trimmed.distance(from: trimmed.startIndex, to: colon.lowerBound) <= 40,
           colon.upperBound < trimmed.endIndex,
           trimmed[colon.upperBound] == " "
        {
            let provider = String(trimmed[..<colon.lowerBound])
            let rest = String(trimmed[trimmed.index(after: colon.upperBound)...])
                .trimmingCharacters(in: .whitespaces)
            return collapsed(provider + " " + rest)
        }
        return trimmed
    }

    static func stripProviderPrefix(_ raw: String) -> String {
        let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if let colon = trimmed.range(of: ":"),
           trimmed.distance(from: trimmed.startIndex, to: colon.lowerBound) <= 40,
           colon.upperBound < trimmed.endIndex,
           trimmed[colon.upperBound] == " "
        {
            return String(trimmed[trimmed.index(after: colon.upperBound)...]).trimmingCharacters(in: .whitespaces)
        }
        for label in Set(providerLabels.values) {
            let prefix = label + " "
            if trimmed.count > prefix.count, trimmed.lowercased().hasPrefix(prefix.lowercased()) {
                return String(trimmed.dropFirst(prefix.count)).trimmingCharacters(in: .whitespaces)
            }
        }
        return trimmed
    }

    static func panelNamesMatch(_ a: String, _ b: String) -> Bool {
        let left = stripLatest(a.trimmingCharacters(in: .whitespacesAndNewlines))
        let right = stripLatest(b.trimmingCharacters(in: .whitespacesAndNewlines))
        if left.caseInsensitiveCompare(right) == .orderedSame { return true }
        let ls = stripLatest(stripProviderPrefix(left))
        let rs = stripLatest(stripProviderPrefix(right))
        return ls.caseInsensitiveCompare(right) == .orderedSame
            || left.caseInsensitiveCompare(rs) == .orderedSame
            || ls.caseInsensitiveCompare(rs) == .orderedSame
    }

    static func stripLatest(_ raw: String) -> String {
        let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        let suffix = " latest"
        if trimmed.lowercased().hasSuffix(suffix), trimmed.count > suffix.count {
            return String(trimmed.dropLast(suffix.count)).trimmingCharacters(in: .whitespaces)
        }
        return trimmed
    }

    static func collapsed(_ raw: String) -> String {
        raw.split { $0.isWhitespace }.joined(separator: " ")
    }

    static func uniquifyRigLabels(_ rows: [(slug: String, name: String)]) -> [String] {
        var names = rows.map { collapsed($0.name) }
        func key(_ value: String) -> String { value.lowercased() }
        func dupes() -> Set<String> {
            var counts: [String: Int] = [:]
            for name in names { counts[key(name), default: 0] += 1 }
            return Set(counts.compactMap { $0.value > 1 ? $0.key : nil })
        }
        var colliding = dupes()
        if !colliding.isEmpty {
            let peers = rows.map(\.slug)
            for i in names.indices where colliding.contains(key(names[i])) {
                let slug = rows[i].slug
                if pathParts(slug).last.map(isLatestToken) == true {
                    names[i] = collapsed(displayNameFromSlug(slug, among: peers))
                } else {
                    names[i] = collapsed(fullNameFromSlug(slug))
                }
            }
        }
        colliding = dupes()
        if !colliding.isEmpty {
            for i in names.indices where colliding.contains(key(names[i])) {
                names[i] = collapsed(fullNameFromSlug(rows[i].slug))
            }
        }
        return names
    }

    private static func normalizeSlug(_ slug: String) -> String {
        slug.trimmingCharacters(in: CharacterSet(charactersIn: "~")).lowercased()
    }

    private static func openRouterProvider(_ slug: String) -> String {
        let normalized = normalizeSlug(slug)
        let raw = normalized.split(separator: "/", maxSplits: 1).first.map(String.init) ?? normalized
        let trimmed = String(raw.drop { !$0.isLetter && !$0.isNumber })
        return trimmed.isEmpty ? raw : trimmed
    }

    private static func slugModelId(_ slug: String) -> String {
        let bare = normalizeSlug(slug).split(separator: ":").first.map(String.init) ?? normalizeSlug(slug)
        if let slash = bare.firstIndex(of: "/") {
            return String(bare[bare.index(after: slash)...]).lowercased()
        }
        return bare.lowercased()
    }

    private static func pathParts(_ slug: String) -> [String] {
        slugModelId(slug).split { $0 == "-" || $0 == "_" }.map(String.init).filter { !$0.isEmpty }
    }

    private static func isLatestToken(_ part: String) -> Bool {
        part.caseInsensitiveCompare("latest") == .orderedSame
    }

    private static func isProductWord(_ part: String) -> Bool {
        if isLatestToken(part) { return false }
        if part.first?.isNumber == true { return false }
        return part.filter(\.isLetter).count >= 2
    }

    private static func titlePathToken(_ part: String) -> String {
        let lower = part.lowercased()
        if lower == "gpt" { return "GPT" }
        if lower == "glm" { return "GLM" }
        if part.first?.isNumber == true { return part }
        return part.prefix(1).uppercased() + part.dropFirst().lowercased()
    }

    private static func shortNameFromSlug(_ slug: String, keepLatest: Bool = false) -> (label: String, droppedFamily: Bool) {
        let parts = pathParts(slug)
        guard !parts.isEmpty else { return (slug, false) }
        let latest = isLatestToken(parts.last ?? "")
        let core = latest ? Array(parts.dropLast()) : parts
        let family = core.first ?? ""
        var product = core
        var droppedFamily = false
        if core.count > 1, core.dropFirst().contains(where: isProductWord) {
            product = Array(core.dropFirst())
            droppedFamily = true
        }
        var words = product.map(titlePathToken)
        if keepLatest && latest { words.append("Latest") }
        let label = words.joined(separator: " ")
        return (label.isEmpty ? titlePathToken(family) : label, droppedFamily)
    }

    private static func fullNameFromSlug(_ slug: String, keepLatest: Bool = false) -> String {
        let parts = pathParts(slug)
        let latest = parts.last.map(isLatestToken) ?? false
        let core = latest ? Array(parts.dropLast()) : parts
        var words = core.map(titlePathToken)
        if keepLatest && latest { words.append("Latest") }
        return words.joined(separator: " ")
    }

    static func displayNameFromSlug(_ slug: String, among: [String] = [], keepLatest: Bool = false) -> String {
        let selfName = shortNameFromSlug(slug, keepLatest: keepLatest)
        if !selfName.droppedFamily || among.isEmpty { return selfName.label }
        let id = slugModelId(slug)
        let provider = openRouterProvider(slug).lowercased()
        let repeatsProvider = id == provider || id.hasPrefix(provider + "-")
        let collide = among.contains { other in
            guard normalizeSlug(other) != normalizeSlug(slug) else { return false }
            return shortNameFromSlug(other, keepLatest: keepLatest).label.caseInsensitiveCompare(selfName.label) == .orderedSame
        }
        if !collide || repeatsProvider { return selfName.label }
        return fullNameFromSlug(slug, keepLatest: keepLatest)
    }
}
