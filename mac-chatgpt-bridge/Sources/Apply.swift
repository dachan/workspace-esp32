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
        if let app {
            _ = app.activate(options: [.activateIgnoringOtherApps])
            Thread.sleep(forTimeInterval: 0.15)
            if let hit = pressMatching(app: app, target: target, kind: .model, maxDepth: maxDepth, maxNodes: maxNodes) {
                return Result(ok: true, path: "ax \(hit)", error: nil)
            }
        }
        if HIDBridge.openPickerAndChoose(target) {
            return Result(ok: true, path: "hid \(HIDBridge.chord)", error: nil)
        }
        return Result(ok: false, path: "hid", error: "could not apply model \(target)")
    }

    static func thinking(
        _ level: String,
        app: NSRunningApplication?,
        maxDepth: Int,
        maxNodes: Int
    ) -> Result {
        let target = ThinkingText.canonical(level) ?? level.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !target.isEmpty else {
            return Result(ok: false, path: "none", error: "empty thinking")
        }
        if let app {
            _ = app.activate(options: [.activateIgnoringOtherApps])
            Thread.sleep(forTimeInterval: 0.15)
            if let hit = pressMatching(app: app, target: target, kind: .thinking, maxDepth: maxDepth, maxNodes: maxNodes) {
                return Result(ok: true, path: "ax \(hit)", error: nil)
            }
        }
        if HIDBridge.openPickerAndChoose(target) {
            return Result(ok: true, path: "hid \(HIDBridge.chord)", error: nil)
        }
        return Result(ok: false, path: "hid", error: "could not apply thinking \(target)")
    }

    private enum Kind { case model, thinking }

    private static func pressMatching(
        app: NSRunningApplication,
        target: String,
        kind: Kind,
        maxDepth: Int,
        maxNodes: Int
    ) -> String? {
        let first = ChatGPTProcess.hits(for: app, maxDepth: maxDepth, maxNodes: maxNodes)
        if let direct = bestPressable(in: first, target: target, kind: kind) {
            if AXAction.press(direct.element) {
                return direct.snap.path
            }
        }
        if kind == .model, let picker = bestPicker(in: first) {
            _ = AXAction.press(picker.element)
            Thread.sleep(forTimeInterval: 0.25)
            let opened = ChatGPTProcess.hits(for: app, maxDepth: maxDepth, maxNodes: maxNodes)
            if let item = bestPressable(in: opened, target: target, kind: kind), AXAction.press(item.element) {
                return item.snap.path
            }
        }
        return nil
    }

    private static func bestPicker(in hits: [AXHit]) -> AXHit? {
        let roles: Set<String> = [
            AXRoleName.popUpButton, AXRoleName.comboBox, AXRoleName.menuButton, AXRoleName.button,
        ]
        return hits.first { hit in
            guard roles.contains(hit.snap.role) else { return false }
            let hay = hit.snap.labels.joined(separator: " ").lowercased()
            return hay.contains("model") || ModelReader.normalizeModel(hit.snap.title ?? "") != nil
                || ModelReader.normalizeModel(hit.snap.value ?? "") != nil
        }
    }

    private static func bestPressable(in hits: [AXHit], target: String, kind: Kind) -> AXHit? {
        let want = target.lowercased()
        let pressable: Set<String> = [
            AXRoleName.menuItem, AXRoleName.button, AXRoleName.radioButton,
            AXRoleName.checkBox, AXRoleName.popUpButton, AXRoleName.menuButton,
        ]
        var best: (AXHit, Int)?
        for hit in hits {
            guard pressable.contains(hit.snap.role) else { continue }
            let labels = hit.snap.labels
            guard !labels.isEmpty else { continue }
            var score = 0
            for label in labels {
                let got = label.lowercased()
                if got == want {
                    score = max(score, 10)
                } else if got.hasPrefix(want) || want.hasPrefix(got) {
                    score = max(score, 8)
                } else if got.contains(want) || want.contains(got) {
                    score = max(score, 5)
                } else if kind == .thinking, ThinkingText.same(got, want) {
                    score = max(score, 9)
                }
            }
            if kind == .thinking {
                let hay = labels.joined(separator: " ").lowercased()
                if hay.contains("reason") || hay.contains("think") {
                    score += 1
                }
            }
            if score >= 5, best == nil || score > best!.1 {
                best = (hit, score)
            }
        }
        return best?.0
    }
}

enum ThinkingText {
    static let names = ["Instant", "Medium", "High", "Extra High"]

    static func canonical(_ raw: String) -> String? {
        let buf = raw.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        if buf.contains("extra high") || buf.contains("max") { return "Extra High" }
        if buf.contains("high") || buf == "thinking" || buf.contains("advanced") { return "High" }
        if buf.contains("medium") || buf.contains("standard") || buf.contains("auto") { return "Medium" }
        if buf.contains("instant") || buf.contains("fast") || buf.contains("low") { return "Instant" }
        return nil
    }

    static func same(_ a: String, _ b: String) -> Bool {
        canonical(a) != nil && canonical(a) == canonical(b)
    }
}
#endif
