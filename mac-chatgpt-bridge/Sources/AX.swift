#if os(macOS)
import ApplicationServices
import AppKit
import Foundation

enum AXTrust {
    static func isTrusted(prompt: Bool) -> Bool {
        let options: NSDictionary = [
            kAXTrustedCheckOptionPrompt.takeUnretainedValue(): prompt,
        ]
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

enum AXRoleName {
    static let popUpButton = "AXPopUpButton"
    static let comboBox = "AXComboBox"
    static let menuButton = "AXMenuButton"
    static let menuItem = "AXMenuItem"
    static let button = "AXButton"
    static let staticText = "AXStaticText"
    static let list = "AXList"
    static let menuBar = "AXMenuBar"
    static let menu = "AXMenu"
}

enum AXNode {
    static func copy(_ element: AXUIElement, _ attribute: CFString) -> CFTypeRef? {
        var value: CFTypeRef?
        guard AXUIElementCopyAttributeValue(element, attribute, &value) == .success else {
            return nil
        }
        return value
    }

    static func element(_ parent: AXUIElement, _ attribute: CFString) -> AXUIElement? {
        guard let value = copy(parent, attribute) else { return nil }
        return axElement(value)
    }

    static func elements(_ parent: AXUIElement, _ attribute: CFString) -> [AXUIElement] {
        guard let value = copy(parent, attribute) else { return [] }
        return axElements(value)
    }

    static func string(_ element: AXUIElement, _ attribute: CFString) -> String? {
        guard let value = copy(element, attribute) else { return nil }
        let typeID = CFGetTypeID(value)
        if typeID == CFStringGetTypeID() {
            let text = String(unsafeBitCast(value, to: CFString.self))
            let trimmed = text.trimmingCharacters(in: .whitespacesAndNewlines)
            return trimmed.isEmpty ? nil : trimmed
        }
        if typeID == CFNumberGetTypeID() {
            var number: Int64 = 0
            let cfNumber = unsafeBitCast(value, to: CFNumber.self)
            guard CFNumberGetValue(cfNumber, .sInt64Type, &number) else { return nil }
            return String(number)
        }
        return nil
    }

    static func children(_ element: AXUIElement) -> [AXUIElement] {
        if let value = copy(element, kAXChildrenAttribute) {
            return axElements(value)
        }
        if let value = copy(element, "AXChildrenInNavigationOrder" as CFString) {
            return axElements(value)
        }
        return []
    }

    static func point(_ element: AXUIElement) -> CGPoint? {
        guard let raw = copy(element, kAXPositionAttribute),
              CFGetTypeID(raw) == AXValueGetTypeID() else {
            return nil
        }
        let axValue = unsafeBitCast(raw, to: AXValue.self)
        var point = CGPoint.zero
        return AXValueGetValue(axValue, .cgPoint, &point) ? point : nil
    }

    static func role(_ element: AXUIElement) -> String {
        string(element, kAXRoleAttribute) ?? "AXUnknown"
    }

    private static func axElement(_ value: CFTypeRef) -> AXUIElement? {
        guard CFGetTypeID(value) == AXUIElementGetTypeID() else { return nil }
        return unsafeBitCast(value, to: AXUIElement.self)
    }

    private static func axElements(_ value: CFTypeRef) -> [AXUIElement] {
        guard CFGetTypeID(value) == CFArrayGetTypeID() else { return [] }
        let cfArray = unsafeBitCast(value, to: CFArray.self)
        let count = CFArrayGetCount(cfArray)
        var result: [AXUIElement] = []
        result.reserveCapacity(count)
        for index in 0..<count {
            guard let pointer = CFArrayGetValueAtIndex(cfArray, index) else { continue }
            let item = Unmanaged<AnyObject>.fromOpaque(pointer).takeUnretainedValue()
            guard CFGetTypeID(item) == AXUIElementGetTypeID() else { continue }
            result.append(unsafeBitCast(item, to: AXUIElement.self))
        }
        return result
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
        let listed = inList || role == AXRoleName.list
        let menu = inMenuBar || role == AXRoleName.menuBar || role == AXRoleName.menu
        out.append(
            AXSnapshot(
                path: path,
                role: role,
                subrole: AXNode.string(element, kAXSubroleAttribute),
                title: AXNode.string(element, kAXTitleAttribute),
                description: AXNode.string(element, kAXDescriptionAttribute),
                value: AXNode.string(element, kAXValueAttribute),
                identifier: AXNode.string(element, kAXIdentifierAttribute),
                help: AXNode.string(element, kAXHelpAttribute),
                mark: AXNode.string(element, kAXMenuItemMarkCharAttribute),
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
        add(AXNode.element(app, kAXFocusedWindowAttribute))
        add(AXNode.element(app, kAXMainWindowAttribute))
        AXNode.elements(app, kAXWindowsAttribute).forEach { add($0) }
        return out
    }
}
#endif
