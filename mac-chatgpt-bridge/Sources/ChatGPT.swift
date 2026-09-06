#if os(macOS)
import ApplicationServices
import AppKit
import Foundation

struct ModelCandidate: Codable {
    var model: String
    var score: Int
    var source: String
    var path: String
    var role: String
}

struct ModelReadback: Codable {
    var ok: Bool
    var model: String?
    var source: String?
    var bundleID: String?
    var pid: Int32?
    var appName: String?
    var candidates: [ModelCandidate]
    var error: String?
}

enum ModelReader {
    private static let chrome: Set<String> = [
        "new chat", "new conversation", "send", "search", "voice", "transcribe",
        "settings", "options", "share", "copy", "edit", "delete", "archive",
        "stop generating", "temporary chat", "chatgpt said", "you said",
        "ask anything", "message chatgpt", "attach", "dictation", "sidebar",
        "close", "minimize", "zoom", "file", "view", "window", "help",
        "chatgpt", "codex", "work", "projects",
    ]

    static func read(
        app: NSRunningApplication,
        maxDepth: Int,
        maxNodes: Int
    ) -> ModelReadback {
        let axApp = AXUIElementCreateApplication(app.processIdentifier)
        var snaps: [AXSnapshot] = []
        if let menuBar = AXNode.element(axApp, kAXMenuBarAttribute) {
            snaps += AXWalk.snapshots(
                of: menuBar,
                prefix: "menu",
                maxDepth: maxDepth,
                maxNodes: maxNodes,
                inMenuBar: true
            )
        }
        let windows = ChatGPTProcess.windows(for: axApp)
        if windows.isEmpty {
            snaps += AXWalk.snapshots(of: axApp, prefix: "app", maxDepth: maxDepth, maxNodes: maxNodes)
        } else {
            for (index, window) in windows.enumerated() {
                snaps += AXWalk.snapshots(
                    of: window,
                    prefix: "win\(index)",
                    maxDepth: maxDepth,
                    maxNodes: maxNodes
                )
            }
        }

        let candidates = snaps.compactMap(score).sorted { lhs, rhs in
            if lhs.score != rhs.score { return lhs.score > rhs.score }
            return lhs.path < rhs.path
        }

        var result = ModelReadback(
            ok: false,
            bundleID: app.bundleIdentifier,
            pid: app.processIdentifier,
            appName: app.localizedName,
            candidates: Array(candidates.prefix(12))
        )
        if let best = candidates.first, best.score >= 6 {
            result.ok = true
            result.model = best.model
            result.source = best.source
        } else {
            result.error = candidates.isEmpty
                ? "no model-like AX nodes; run --dump-ax"
                : "low-confidence model match; inspect --list-candidates or --dump-ax"
        }
        return result
    }

    static func dump(app: NSRunningApplication, maxDepth: Int, maxNodes: Int) {
        let axApp = AXUIElementCreateApplication(app.processIdentifier)
        print(
            "# ChatGPT AX dump  pid=\(app.processIdentifier)  bundle=\(app.bundleIdentifier ?? "?")  name=\(app.localizedName ?? "?")"
        )
        if let menuBar = AXNode.element(axApp, kAXMenuBarAttribute) {
            print("# menu bar")
            AXWalk.snapshots(
                of: menuBar,
                prefix: "menu",
                maxDepth: maxDepth,
                maxNodes: maxNodes,
                inMenuBar: true
            ).forEach { print(AXWalk.dumpLine($0)) }
        }
        let windows = ChatGPTProcess.windows(for: axApp)
        if windows.isEmpty {
            print("# app (no AXWindow list)")
            AXWalk.snapshots(of: axApp, prefix: "app", maxDepth: maxDepth, maxNodes: maxNodes)
                .forEach { print(AXWalk.dumpLine($0)) }
            return
        }
        for (index, window) in windows.enumerated() {
            print("# window \(index)")
            AXWalk.snapshots(of: window, prefix: "win\(index)", maxDepth: maxDepth, maxNodes: maxNodes)
                .forEach { print(AXWalk.dumpLine($0)) }
        }
    }

    private static func score(_ snap: AXSnapshot) -> ModelCandidate? {
        let fields = [snap.title, snap.description, snap.value, snap.help].compactMap { $0 }
        let models = fields.compactMap(normalizeModel)
        guard let model = models.first else { return nil }

        var score = 4
        var source = "\(snap.role)"
        switch snap.role {
        case AXRoleName.popUpButton, AXRoleName.comboBox, AXRoleName.menuButton:
            score += 4
            source = "toolbar-picker \(snap.role)"
        case AXRoleName.menuItem:
            score += snap.mark == nil ? 1 : 5
            source = snap.mark == nil ? "menu-item" : "checked-menu-item"
        case AXRoleName.button:
            score += 2
            source = "button"
        case AXRoleName.staticText:
            score += 1
            source = "static-text"
        default:
            break
        }

        let haystack = ([snap.identifier, snap.help, snap.description].compactMap { $0 }.joined(separator: " ")).lowercased()
        if haystack.contains("model") {
            score += 5
            source += "+id"
        }
        if snap.inMenuBar { score += 2 }
        if snap.inList { score -= 4 }
        if let y = snap.position?.y, y >= 0, y < 160, !snap.inMenuBar {
            score += 2
            source += "+top"
        }
        if model.lowercased() == "chatgpt" {
            score -= 3
        }

        return ModelCandidate(model: model, score: score, source: source, path: snap.path, role: snap.role)
    }

    static func normalizeModel(_ raw: String) -> String? {
        let text = raw
            .replacingOccurrences(of: "\u{FFFC}", with: "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        guard (2...80).contains(text.count) else { return nil }
        let lower = text.lowercased()
        if chrome.contains(lower) { return nil }

        let exactModes = ["auto", "instant", "thinking", "standard", "pro"]
        if exactModes.contains(lower) { return text }

        let patterns: [NSRegularExpression] = [
            try! NSRegularExpression(pattern: #"^chatgpt\s+\d+(\.\d+)?(\s+\w+)*$"#, options: .caseInsensitive),
            try! NSRegularExpression(pattern: #"^gpt-\d+(\.\d+)?([-\s]\w+)*$"#, options: .caseInsensitive),
            try! NSRegularExpression(pattern: #"^o\d+([-\s]\w+)*$"#, options: .caseInsensitive),
            try! NSRegularExpression(pattern: #"^(gpt-)?5(\.\d+)?(\s+(instant|thinking|pro|auto))?$"#, options: .caseInsensitive),
            try! NSRegularExpression(pattern: #"^(instant|thinking|auto|pro)(\s+mode)?$"#, options: .caseInsensitive),
        ]
        let range = NSRange(text.startIndex..<text.endIndex, in: text)
        if patterns.contains(where: { $0.firstMatch(in: text, range: range) != nil }) {
            return text
        }
        return nil
    }
}
#endif
