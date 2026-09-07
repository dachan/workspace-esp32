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
           ThinkingText.stripSuffix(chip.title).caseInsensitiveCompare(model) == .orderedSame {
            return Result(ok: true, path: "noop", error: nil)
        }

        if HIDBridge.openModelPicker() {
            Thread.sleep(forTimeInterval: 0.8)
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
                return Result(ok: true, path: "picker \(path)", error: nil)
            }
            dismissMenus()
        }
        return Result(
            ok: false,
            path: "model picker",
            error: "model picker did not switch to \(model); retry when ChatGPT is idle"
        )
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

        let currentTitle = ModelChip.find(app: app, maxDepth: maxDepth, maxNodes: maxNodes)?.title ?? ""
        if ThinkingText.suffix(in: currentTitle) == think {
            return Result(ok: true, path: "noop", error: nil)
        }

        if let current = ThinkingText.suffix(in: currentTitle),
           let delta = ThinkingText.delta(from: current, to: think),
           HIDBridge.bumpReasoning(delta: delta),
           ModelChip.thinkingMatches(
               app: app,
               expect: think,
               maxDepth: maxDepth,
               maxNodes: maxNodes
           ) {
            return Result(ok: true, path: "reasoning shortcut", error: nil)
        }
        let now = ModelChip.find(app: app, maxDepth: maxDepth, maxNodes: maxNodes)?.title ?? "?"
        return Result(
            ok: false,
            path: "reasoning shortcut",
            error: "could not set reasoning to \(think); model chip is \(now). Configure the Increase/Decrease Reasoning shortcuts in ChatGPT Settings"
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
        let wanted = model.lowercased()
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
                guard hit.snap.role == AXRoleName.button else { return nil }
                let labels = hit.snap.labels.map {
                    $0.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
                }
                var score = 0
                for label in labels {
                    if label == wanted {
                        score = max(score, 10)
                    } else if label.hasPrefix("\(wanted) ") {
                        score = max(score, 8)
                    }
                }
                return score > 0 ? (hit, score) : nil
            }
        for (hit, _) in matches.sorted(by: { $0.1 > $1.1 }) {
            if AXAction.press(hit.element) {
                return hit.snap.path
            }
        }
        fputs("chatgpt-bridge: no pressable model-picker match for \(model)\n", stderr)
        return nil
    }
}

enum ModelChip {
    struct Hit {
        var element: AXUIElement
        var path: String
        var title: String
    }

    static func find(app: NSRunningApplication, maxDepth: Int, maxNodes: Int) -> Hit? {
        let hits = ChatGPTProcess.hits(for: app, maxDepth: maxDepth, maxNodes: maxNodes)
        let scored = hits.compactMap { hit -> (Hit, Int)? in
            guard hit.snap.role == AXRoleName.popUpButton else { return nil }
            let title = hit.snap.title ?? hit.snap.value ?? ""
            let desc = hit.snap.description ?? ""
            var score = 0
            let hay = "\(title) \(desc)".lowercased()
            if hay.contains("gpt") || hay.contains("o3") || hay.contains("o4") || hay.contains("codex") {
                score += 5
            }
            if ModelReader.normalizeModel(title) != nil { score += 5 }
            if hay.contains("terra") || hay.contains("luna") { score += 2 }
            guard score >= 5 else { return nil }
            return (
                Hit(element: hit.element, path: hit.snap.path, title: title),
                score
            )
        }
        return scored.max(by: { $0.1 < $1.1 })?.0
    }

    static func modelMatches(app: NSRunningApplication, expect: String, maxDepth: Int, maxNodes: Int) -> Bool {
        Thread.sleep(forTimeInterval: 0.55)
        guard let chip = find(app: app, maxDepth: maxDepth, maxNodes: maxNodes) else { return false }
        return ThinkingText.stripSuffix(chip.title).caseInsensitiveCompare(expect) == .orderedSame
    }

    static func thinkingMatches(app: NSRunningApplication, expect: String, maxDepth: Int, maxNodes: Int) -> Bool {
        Thread.sleep(forTimeInterval: 0.55)
        guard let chip = find(app: app, maxDepth: maxDepth, maxNodes: maxNodes) else { return false }
        return ThinkingText.suffix(in: chip.title)?.lowercased() == expect.lowercased()
    }
}

enum ThinkingText {
    static let currentNames = ["Light", "Medium", "High", "Extra High"]
    static let legacyNames = ["Instant", "Medium", "High", "Extra High"]
    private static let suffixes: [(text: String, canonical: String)] = [
        ("Extra High", "Extra High"),
        ("Advanced", "High"),
        ("Standard", "Medium"),
        ("Thinking", "High"),
        ("Instant", "Instant"),
        ("Medium", "Medium"),
        ("Heavy", "Extra High"),
        ("Light", "Light"),
        ("High", "High"),
        ("Fast", "Instant"),
        ("Auto", "Medium"),
        ("Max", "Extra High"),
        ("Low", "Instant"),
    ]

    static func canonical(_ raw: String) -> String? {
        let buf = raw.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        if let suffix = suffix(in: raw) { return suffix }
        if buf.contains("extra high") || buf == "max" { return "Extra High" }
        if buf.contains("heavy") { return "Extra High" }
        if buf == "thinking" || buf == "high" || buf.contains("advanced") { return "High" }
        if buf.contains("high"), !buf.contains("extra") { return "High" }
        if buf.contains("medium") || buf.contains("standard") || buf == "auto" { return "Medium" }
        if buf.contains("light") { return "Light" }
        if buf.contains("instant") || buf.contains("fast") || buf == "low" { return "Instant" }
        return nil
    }

    static func delta(from current: String, to target: String) -> Int? {
        for order in [currentNames, legacyNames] {
            if let currentIndex = order.firstIndex(of: current),
               let targetIndex = order.firstIndex(of: target) {
                return targetIndex - currentIndex
            }
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
