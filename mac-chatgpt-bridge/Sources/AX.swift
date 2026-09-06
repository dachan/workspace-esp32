#if os(macOS)
import ApplicationServices
import AppKit
import Foundation

enum AXTrust {
    static func isTrusted(prompt: Bool) -> Bool {
        let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: prompt] as CFDictionary
        return AXIsProcessTrustedWithOptions(options)
    }

    static func require(prompt: Bool) -> Bool {
        if isTrusted(prompt: prompt) {
            return true
        }
        let process = ProcessInfo.processInfo.processName
        fputs(
            """
            Accessibility is not granted for this process (\(process)).
            Grant it to the app that launches chatgpt-bridge (Terminal, iTerm, Cursor, …):
              System Settings → Privacy & Security → Accessibility
            Then re-run this command.

            """,
            stderr
        )
        return false
    }
}

enum AXNode {
    static func copy(_ element: AXUIElement, _ attribute: String) -> AnyObject? {
        var value: CFTypeRef?
        guard AXUIElementCopyAttributeValue(element, attribute as CFString, &value) == .success else {
            return nil
        }
        return value
    }

    static func string(_ element: AXUIElement, _ attribute: String) -> String? {
        if let value = copy(element, attribute) as? String {
            let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
            return trimmed.isEmpty ? nil : trimmed
        }
        if let number = copy(element, attribute) as? NSNumber {
            return number.stringValue
        }
        return nil
    }

    static func bool(_ element: AXUIElement, _ attribute: String) -> Bool? {
        copy(element, attribute) as? Bool
    }

    static func children(_ element: AXUIElement) -> [AXUIElement] {
        if let kids = copy(element, kAXChildrenAttribute as String) as? [AXUIElement] {
            return kids
        }
        if let kids = copy(element, "AXChildrenInNavigationOrder") as? [AXUIElement] {
            return kids
        }
        return []
    }

    static func point(_ element: AXUIElement) -> CGPoint? {
        guard let raw = copy(element, kAXPositionAttribute as String) else {
            return nil
        }
        var point = CGPoint.zero
        let ok = AXValueGetValue(raw as! AXValue, .cgPoint, &point)
        return ok ? point : nil
    }

    static func role(_ element: AXUIElement) -> String {
        string(element, kAXRoleAttribute as String) ?? "AXUnknown"
    }

    static func texts(_ element: AXUIElement) -> [String] {
        [
            string(element, kAXTitleAttribute as String),
            string(element, kAXDescriptionAttribute as String),
            string(element, kAXValueAttribute as String),
            string(element, kAXHelpAttribute as String),
            string(element, kAXIdentifierAttribute as String),
        ].compactMap { $0 }
    }
}

struct AXSnapshot {
    var path: String
    var role: String
    var subrole: String?
    var title: String?
    var description: String?
    var value: String?
    var identifier: String?
    var help: String?
    var mark: String?
    var position: CGPoint?
    var inList: Bool
    var inMenuBar: Bool
}

enum AXWalk {
    static func snapshots(
        of root: AXUIElement,
        prefix: String,
        maxDepth: Int,
        maxNodes: Int,
        inList: Bool = false,
        inMenuBar: Bool = false
    ) -> [AXSnapshot] {
        var out: [AXSnapshot] = []
        walk(
            root,
            path: prefix,
            depth: 0,
            maxDepth: maxDepth,
            maxNodes: maxNodes,
            inList: inList,
            inMenuBar: inMenuBar,
            into: &out
        )
        return out
    }

    private static func walk(
        _ element: AXUIElement,
        path: String,
        depth: Int,
        maxDepth: Int,
        maxNodes: Int,
        inList: Bool,
        inMenuBar: Bool,
        into out: inout [AXSnapshot]
    ) {
        guard out.count < maxNodes else { return }
        let role = AXNode.role(element)
        let listed = inList || role == (kAXListRole as String)
        let menu = inMenuBar || role == (kAXMenuBarRole as String) || role == (kAXMenuRole as String)
        out.append(
            AXSnapshot(
                path: path,
                role: role,
                subrole: AXNode.string(element, kAXSubroleAttribute as String),
                title: AXNode.string(element, kAXTitleAttribute as String),
                description: AXNode.string(element, kAXDescriptionAttribute as String),
                value: AXNode.string(element, kAXValueAttribute as String),
                identifier: AXNode.string(element, kAXIdentifierAttribute as String),
                help: AXNode.string(element, kAXHelpAttribute as String),
                mark: AXNode.string(element, kAXMenuItemMarkCharAttribute as String),
                position: AXNode.point(element),
                inList: listed,
                inMenuBar: menu
            )
        )
        guard depth < maxDepth else { return }
        let kids = AXNode.children(element)
        for (index, child) in kids.enumerated() {
            walk(
                child,
                path: "\(path).\(index)",
                depth: depth + 1,
                maxDepth: maxDepth,
                maxNodes: maxNodes,
                inList: listed,
                inMenuBar: menu,
                into: &out
            )
        }
    }

    static func dumpLine(_ snap: AXSnapshot) -> String {
        var parts = ["\(snap.path)  \(snap.role)"]
        func add(_ key: String, _ value: String?) {
            guard let value, !value.isEmpty else { return }
            parts.append("\(key)=\(quote(value))")
        }
        add("subrole", snap.subrole)
        add("title", snap.title)
        add("desc", snap.description)
        add("value", snap.value)
        add("id", snap.identifier)
        add("help", snap.help)
        add("mark", snap.mark)
        if let position = snap.position {
            parts.append("pos=\(Int(position.x)),\(Int(position.y))")
        }
        return parts.joined(separator: "  ")
    }

    private static func quote(_ value: String) -> String {
        let escaped = value
            .replacingOccurrences(of: "\\", with: "\\\\")
            .replacingOccurrences(of: "\"", with: "\\\"")
            .replacingOccurrences(of: "\n", with: "\\n")
        return "\"\(escaped)\""
    }
}

enum ChatGPTProcess {
    static let knownBundleIDs = [
        "com.openai.chat",
        "com.openai.codex",
    ]

    static func find(preferredBundleID: String?) -> NSRunningApplication? {
        if let preferredBundleID, !preferredBundleID.isEmpty {
            return firstRunning(bundleID: preferredBundleID)
        }
        let running = NSWorkspace.shared.runningApplications
        let matches = running.filter(isChatGPTFamily)
        if let front = matches.first(where: { $0.isActive }) {
            return front
        }
        for bundleID in knownBundleIDs {
            if let app = matches.first(where: { $0.bundleIdentifier == bundleID }) {
                return app
            }
        }
        return matches.first
    }

    static func isChatGPTFamily(_ app: NSRunningApplication) -> Bool {
        if let bundleID = app.bundleIdentifier, knownBundleIDs.contains(bundleID) {
            return true
        }
        let name = (app.localizedName ?? app.bundleURL?.deletingPathExtension().lastPathComponent ?? "")
            .lowercased()
        return name == "chatgpt" || name.hasPrefix("chatgpt ")
    }

    static func firstRunning(bundleID: String) -> NSRunningApplication? {
        NSRunningApplication.runningApplications(withBundleIdentifier: bundleID).first
    }

    static func windows(for app: AXUIElement) -> [AXUIElement] {
        var seen = Set<String>()
        var out: [AXUIElement] = []
        func add(_ element: AXUIElement?) {
            guard let element else { return }
            let key = String(describing: element)
            guard seen.insert(key).inserted else { return }
            out.append(element)
        }
        add(AXNode.copy(app, kAXFocusedWindowAttribute as String) as? AXUIElement)
        add(AXNode.copy(app, kAXMainWindowAttribute as String) as? AXUIElement)
        if let windows = AXNode.copy(app, kAXWindowsAttribute as String) as? [AXUIElement] {
            windows.forEach { add($0) }
        }
        return out
    }
}
#endif
