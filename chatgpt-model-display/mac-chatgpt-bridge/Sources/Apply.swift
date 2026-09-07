#if os(macOS)
import AppKit
import ApplicationServices
import Foundation

enum ChatGPTApply {
    struct Result {
        var ok: Bool
        var path: String
        var error: String?
    }

    static func model(
        _ name: String,
        app: NSRunningApplication?,
        maxDepth: Int,
        maxNodes: Int
    ) -> Result {
        let target = name.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !target.isEmpty else {
            return Result(ok: false, path: "none", error: "empty model")
        }
        guard let app else {
            return Result(ok: false, path: "none", error: "ChatGPT not running")
        }
        app.activate(options: [])
        Thread.sleep(forTimeInterval: 0.35)
        dismissMenus()

        let base = ThinkingText.stripSuffix(target)
        let model = base.isEmpty ? target : base
        if let chip = ModelChip.find(app: app, maxDepth: maxDepth, maxNodes: maxNodes),
           ModelText.matches(chip.model, model) {
            return Result(ok: true, path: "noop", error: nil)
        }

        if let opener = openModelPicker(app: app, maxDepth: maxDepth, maxNodes: maxNodes) {
            if let path = ModelPicker.select(
                model,
                app: app,
                maxDepth: maxDepth,
                maxNodes: maxNodes
            ), ModelChip.modelMatches(
                app: app,
                expect: model,
                maxDepth: maxDepth,
                maxNodes: maxNodes
            ) {
                return Result(ok: true, path: "\(opener) -> picker \(path)", error: nil)
            }
            dismissMenus()
        }
        return Result(
            ok: false,
            path: "model picker",
            error: "model picker did not switch to \(model); retry when ChatGPT is idle"
        )
    }

    private static func openModelPicker(
        app: NSRunningApplication,
        maxDepth: Int,
        maxNodes: Int
    ) -> String? {
        if let chip = ModelChip.find(app: app, maxDepth: maxDepth, maxNodes: maxNodes),
           AXAction.press(chip.element) {
            Thread.sleep(forTimeInterval: 0.65)
            return "model control \(chip.path)"
        }
        if HIDBridge.openModelPicker() {
            Thread.sleep(forTimeInterval: 0.8)
            return "model shortcut"
        }
        return nil
    }

    static func thinking(
        _ level: String,
        app: NSRunningApplication?,
        maxDepth: Int,
        maxNodes: Int
    ) -> Result {
        let think = ThinkingText.canonical(level)
            ?? level.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !think.isEmpty else {
            return Result(ok: false, path: "none", error: "empty thinking")
        }
        guard let app else {
            return Result(ok: false, path: "none", error: "ChatGPT not running")
        }
        app.activate(options: [])
        Thread.sleep(forTimeInterval: 0.35)
        dismissMenus()

        let current = ThinkingControl.current(
            app: app,
            maxDepth: maxDepth,
            maxNodes: maxNodes
        )
        if current == think {
            return Result(ok: true, path: "noop", error: nil)
        }

        if let path = ThinkingControl.select(
            think,
            app: app,
            maxDepth: maxDepth,
            maxNodes: maxNodes
        ), ThinkingControl.matches(
            app: app,
            expect: think,
            maxDepth: maxDepth,
            maxNodes: maxNodes
        ) {
            return Result(ok: true, path: path, error: nil)
        }

        dismissMenus()
        if let current,
           let delta = ThinkingText.delta(from: current, to: think),
           HIDBridge.bumpReasoning(delta: delta),
           ThinkingControl.matches(
               app: app,
               expect: think,
               maxDepth: maxDepth,
               maxNodes: maxNodes
           ) {
            return Result(ok: true, path: "reasoning shortcut", error: nil)
        }
        let now = ThinkingControl.current(
            app: app,
            maxDepth: maxDepth,
            maxNodes: maxNodes
        ) ?? "?"
        return Result(
            ok: false,
            path: "thinking control",
            error: "could not set reasoning to \(think); current level is \(now)"
        )
    }

    static func dismissMenus() {
        for _ in 0..<2 {
            _ = HIDBridge.postKeyRaw(HIDBridge.keyEscape, flags: [], down: true)
            _ = HIDBridge.postKeyRaw(HIDBridge.keyEscape, flags: [], down: false)
            Thread.sleep(forTimeInterval: 0.05)
        }
    }
}

enum ModelPicker {
    static func select(
        _ model: String,
        app: NSRunningApplication,
        maxDepth: Int,
        maxNodes: Int
    ) -> String? {
        if let path = selectVisible(model, app: app, maxDepth: maxDepth, maxNodes: maxNodes) {
            return path
        }
        if let section = openMoreModels(app: app, maxDepth: maxDepth, maxNodes: maxNodes) {
            Thread.sleep(forTimeInterval: 0.45)
            if let path = selectVisible(model, app: app, maxDepth: maxDepth, maxNodes: maxNodes) {
                return "\(section) -> \(path)"
            }
        }
        fputs("chatgpt-bridge: no pressable model-picker match for \(model)\n", stderr)
        return nil
    }

    private static func selectVisible(
        _ model: String,
        app: NSRunningApplication,
        maxDepth: Int,
        maxNodes: Int
    ) -> String? {
        // The picker overlay is appended late in the Electron AX tree.
        let pickerMaxDepth = max(maxDepth, 64)
        let pickerNodeCap = max(maxNodes, 16_000)
        let axApp = AXUIElementCreateApplication(app.processIdentifier)
        let matches = AXWalk.hits(
            of: axApp,
            prefix: "app",
            maxDepth: pickerMaxDepth,
            maxNodes: pickerNodeCap
        )
            .compactMap { hit -> (AXHit, Int)? in
                guard AXAction.isPressableRole(hit.snap.role) else { return nil }
                let score = hit.snap.labels.map {
                    ModelText.pickerScore($0, target: model)
                }.max() ?? 0
                return score > 0 ? (hit, score) : nil
            }
        for (hit, _) in matches.sorted(by: { $0.1 > $1.1 }) {
            if AXAction.press(hit.element) {
                return hit.snap.path
            }
        }
        return nil
    }

    private static func openMoreModels(
        app: NSRunningApplication,
        maxDepth: Int,
        maxNodes: Int
    ) -> String? {
        let axApp = AXUIElementCreateApplication(app.processIdentifier)
        let labels = ["legacy models", "more models", "other models"]
        let hit = AXWalk.hits(
            of: axApp,
            prefix: "app",
            maxDepth: max(maxDepth, 64),
            maxNodes: max(maxNodes, 16_000)
        ).first { hit in
            guard AXAction.isPressableRole(hit.snap.role) else { return false }
            let haystack = hit.snap.labels.joined(separator: " ").lowercased()
            return labels.contains(where: haystack.contains)
        }
        guard let hit, AXAction.press(hit.element) else { return nil }
        return hit.snap.path
    }
}

enum ModelText {
    static func matches(_ candidate: String, _ target: String) -> Bool {
        let lhs = canonical(candidate)
        let rhs = canonical(target)
        return lhs.caseInsensitiveCompare(rhs) == .orderedSame
    }

    static func pickerScore(_ candidate: String, target: String) -> Int {
        let label = normalized(candidate)
        let wanted = normalized(target)
        if label == wanted { return 12 }
        if label.hasPrefix("\(wanted) ") || label.hasSuffix(" \(wanted)") { return 10 }
        if matches(candidate, target) { return 9 }

        let targetCanonical = canonical(target)
        let distinctive = targetCanonical.split(separator: " ").last.map(String.init) ?? ""
        if distinctive.count >= 3, normalized(candidate).split(separator: " ").contains(Substring(distinctive.lowercased())) {
            return 7
        }
        return 0
    }

    private static func canonical(_ raw: String) -> String {
        let stripped = ThinkingText.stripSuffix(raw)
        return ModelReader.normalizeModel(stripped) ?? stripped
    }

    private static func normalized(_ raw: String) -> String {
        raw.lowercased()
            .components(separatedBy: CharacterSet.alphanumerics.inverted)
            .filter { !$0.isEmpty }
            .joined(separator: " ")
    }
}

enum ModelChip {
    struct Hit {
        var element: AXUIElement
        var path: String
        var model: String
    }

    static func find(app: NSRunningApplication, maxDepth: Int, maxNodes: Int) -> Hit? {
        let hits = ChatGPTProcess.hits(for: app, maxDepth: maxDepth, maxNodes: maxNodes)
        let scored = hits.compactMap { hit -> (Hit, Int)? in
            guard AXAction.isPressableRole(hit.snap.role) else { return nil }
            let labels = hit.snap.labels
            guard let title = labels.first(where: { ModelReader.normalizeModel($0) != nil }),
                  let normalized = ModelReader.normalizeModel(title) else { return nil }
            let model = ThinkingText.stripSuffix(normalized)
            var score = 7
            let hay = labels.joined(separator: " ").lowercased()
            let namedModel = model.lowercased().contains("gpt")
                || ["astra", "sol", "terra", "luna"].contains(model.lowercased())
            guard namedModel || hay.contains("model") else { return nil }
            if hit.snap.role == AXRoleName.popUpButton || hit.snap.role == AXRoleName.comboBox {
                score += 4
            }
            if hay.contains("model") { score += 5 }
            if hay.contains("terra") || hay.contains("luna") { score += 2 }
            if hit.snap.inList || hit.snap.inMenuBar { score -= 6 }
            if let y = hit.snap.position?.y, y >= 0, y < 180, !hit.snap.inMenuBar {
                score += 2
            }
            return (
                Hit(element: hit.element, path: hit.snap.path, model: model),
                score
            )
        }
        return scored.max(by: { $0.1 < $1.1 })?.0
    }

    static func modelMatches(app: NSRunningApplication, expect: String, maxDepth: Int, maxNodes: Int) -> Bool {
        Thread.sleep(forTimeInterval: 0.55)
        guard let chip = find(app: app, maxDepth: maxDepth, maxNodes: maxNodes) else { return false }
        return ModelText.matches(chip.model, expect)
    }
}

enum ThinkingControl {
    struct Hit {
        var element: AXUIElement
        var path: String
        var level: String
        var score: Int
    }

    static func current(app: NSRunningApplication, maxDepth: Int, maxNodes: Int) -> String? {
        find(app: app, maxDepth: maxDepth, maxNodes: maxNodes)?.level
    }

    static func find(app: NSRunningApplication, maxDepth: Int, maxNodes: Int) -> Hit? {
        let hits = ChatGPTProcess.hits(for: app, maxDepth: maxDepth, maxNodes: maxNodes)
        return hits.compactMap(score).filter { $0.score >= 7 }.max { $0.score < $1.score }
    }

    static func select(
        _ target: String,
        app: NSRunningApplication,
        maxDepth: Int,
        maxNodes: Int
    ) -> String? {
        if let control = find(app: app, maxDepth: maxDepth, maxNodes: maxNodes),
           AXAction.press(control.element) {
            Thread.sleep(forTimeInterval: 0.6)
            if let selected = ThinkingPicker.selectFromOpenMenu(
                target,
                app: app,
                maxDepth: maxDepth,
                maxNodes: maxNodes
            ) {
                return "thinking control \(control.path) -> \(selected)"
            }
            ChatGPTApply.dismissMenus()
        }

        if let model = ModelChip.find(app: app, maxDepth: maxDepth, maxNodes: maxNodes),
           AXAction.press(model.element) {
            Thread.sleep(forTimeInterval: 0.6)
            if let selected = ThinkingPicker.selectFromOpenMenu(
                target,
                app: app,
                maxDepth: maxDepth,
                maxNodes: maxNodes
            ) {
                return "model control \(model.path) -> \(selected)"
            }
            ChatGPTApply.dismissMenus()
        }
        return nil
    }

    static func matches(
        app: NSRunningApplication,
        expect: String,
        maxDepth: Int,
        maxNodes: Int
    ) -> Bool {
        Thread.sleep(forTimeInterval: 0.55)
        return current(app: app, maxDepth: maxDepth, maxNodes: maxNodes)?
            .caseInsensitiveCompare(expect) == .orderedSame
    }

    private static func score(_ hit: AXHit) -> Hit? {
        guard AXAction.isPressableRole(hit.snap.role) else { return nil }
        guard let scored = ThinkingMatch.score(hit.snap) else { return nil }
        return Hit(
            element: hit.element,
            path: hit.snap.path,
            level: scored.level,
            score: scored.score
        )
    }
}

enum ThinkingPicker {
    static func selectFromOpenMenu(
        _ target: String,
        app: NSRunningApplication,
        maxDepth: Int,
        maxNodes: Int
    ) -> String? {
        if let path = selectVisible(target, app: app, maxDepth: maxDepth, maxNodes: maxNodes) {
            return path
        }
        if let selector = openSelector(app: app, maxDepth: maxDepth, maxNodes: maxNodes) {
            Thread.sleep(forTimeInterval: 0.45)
            if let path = selectVisible(target, app: app, maxDepth: maxDepth, maxNodes: maxNodes) {
                return "\(selector) -> \(path)"
            }
        }
        return nil
    }

    private static func selectVisible(
        _ target: String,
        app: NSRunningApplication,
        maxDepth: Int,
        maxNodes: Int
    ) -> String? {
        let wanted = ThinkingText.canonical(target) ?? target
        let axApp = AXUIElementCreateApplication(app.processIdentifier)
        let matches = AXWalk.hits(
            of: axApp,
            prefix: "app",
            maxDepth: max(maxDepth, 64),
            maxNodes: max(maxNodes, 16_000)
        ).compactMap { hit -> (AXHit, Int)? in
            guard AXAction.isPressableRole(hit.snap.role) else { return nil }
            var score = 0
            for label in hit.snap.labels where ThinkingText.isLevelLabel(label) {
                if ThinkingText.canonical(label) == wanted {
                    score = max(score, 10)
                }
            }
            if hit.snap.mark != nil { score += 2 }
            return score > 0 ? (hit, score) : nil
        }
        for (hit, _) in matches.sorted(by: { $0.1 > $1.1 }) {
            if AXAction.press(hit.element) { return hit.snap.path }
        }
        return nil
    }

    private static func openSelector(
        app: NSRunningApplication,
        maxDepth: Int,
        maxNodes: Int
    ) -> String? {
        let axApp = AXUIElementCreateApplication(app.processIdentifier)
        let hit = AXWalk.hits(
            of: axApp,
            prefix: "app",
            maxDepth: max(maxDepth, 64),
            maxNodes: max(maxNodes, 16_000)
        ).first { hit in
            guard AXAction.isPressableRole(hit.snap.role) else { return false }
            let haystack = hit.snap.labels.joined(separator: " ").lowercased()
            return haystack.contains("reasoning") || haystack.contains("thinking effort")
                || haystack.contains("thinking level")
        }
        guard let hit, AXAction.press(hit.element) else { return nil }
        return hit.snap.path
    }
}

enum ThinkingText {
    static let currentNames = ["Light", "Medium", "High", "Extra High"]
    private static let suffixes: [(text: String, canonical: String)] = [
        ("Extra High", "Extra High"),
        ("ExtraHigh", "Extra High"),
        ("XHigh", "Extra High"),
        ("Ultra", "Extra High"),
        ("Max", "Extra High"),
        ("Advanced", "High"),
        ("Standard", "Medium"),
        ("Thinking", "High"),
        ("Instant", "Light"),
        ("Minimal", "Light"),
        ("Medium", "Medium"),
        ("Heavy", "Extra High"),
        ("Light", "Light"),
        ("High", "High"),
        ("Fast", "Light"),
        ("Auto", "Medium"),
        ("Low", "Light"),
    ]

    static func canonical(_ raw: String) -> String? {
        let buf = raw.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        if let suffix = suffix(in: raw) { return suffix }
        if buf.contains("extra high") || buf.contains("xhigh") || buf == "max"
            || buf == "ultra" { return "Extra High" }
        if buf.contains("heavy") { return "Extra High" }
        if buf == "thinking" || buf == "high" || buf.contains("advanced") { return "High" }
        if buf.contains("high"), !buf.contains("extra") { return "High" }
        if buf.contains("medium") || buf.contains("standard") || buf == "auto" { return "Medium" }
        if buf.contains("light") || buf == "minimal" { return "Light" }
        if buf.contains("instant") || buf.contains("fast") || buf == "low" { return "Light" }
        return nil
    }

    static func isLevelLabel(_ raw: String) -> Bool {
        let trimmed = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard canonical(trimmed) != nil else { return false }
        return stripSuffix(trimmed).isEmpty
    }

    static func delta(from current: String, to target: String) -> Int? {
        if let currentIndex = currentNames.firstIndex(of: current),
           let targetIndex = currentNames.firstIndex(of: target) {
            return targetIndex - currentIndex
        }
        return nil
    }

    static func suffix(in title: String) -> String? {
        let t = title.trimmingCharacters(in: .whitespacesAndNewlines)
        for suffix in suffixes {
            if t.lowercased().hasSuffix(suffix.text.lowercased()) {
                return suffix.canonical
            }
        }
        return nil
    }

    static func stripSuffix(_ title: String) -> String {
        var t = title.trimmingCharacters(in: .whitespacesAndNewlines)
        for suffix in suffixes {
            if t.lowercased().hasSuffix(suffix.text.lowercased()) {
                t = String(t.dropLast(suffix.text.count))
                    .trimmingCharacters(in: .whitespacesAndNewlines)
                break
            }
        }
        return t
    }
}
#endif
