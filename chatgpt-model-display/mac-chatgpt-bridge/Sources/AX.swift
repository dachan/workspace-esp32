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
    static let radioButton = "AXRadioButton"
    static let checkBox = "AXCheckBox"
    static let list = "AXList"
    static let menuBar = "AXMenuBar"
    static let menu = "AXMenu"
}

enum AXAction {
    static let press = kAXPressAction as CFString

    static func isPressableRole(_ role: String) -> Bool {
        switch role {
        case AXRoleName.popUpButton, AXRoleName.comboBox, AXRoleName.menuButton,
             AXRoleName.menuItem, AXRoleName.button, AXRoleName.radioButton:
            return true
        default:
            return false
        }
    }

    @discardableResult
    static func press(_ element: AXUIElement) -> Bool {
        AXUIElementPerformAction(element, press) == .success
    }
}

enum AXAttr {
    static let size = kAXSizeAttribute as CFString
    static let children = kAXChildrenAttribute as CFString
    static let childrenInNavOrder = "AXChildrenInNavigationOrder" as CFString
    static let description = kAXDescriptionAttribute as CFString
    static let focusedWindow = kAXFocusedWindowAttribute as CFString
    static let help = kAXHelpAttribute as CFString
    static let identifier = kAXIdentifierAttribute as CFString
    static let mainWindow = kAXMainWindowAttribute as CFString
    static let mark = kAXMenuItemMarkCharAttribute as CFString
    static let menuBar = kAXMenuBarAttribute as CFString
    static let position = kAXPositionAttribute as CFString
    static let role = kAXRoleAttribute as CFString
    static let subrole = kAXSubroleAttribute as CFString
    static let title = kAXTitleAttribute as CFString
    static let value = kAXValueAttribute as CFString
    static let windows = kAXWindowsAttribute as CFString
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
        if let value = copy(element, AXAttr.children) {
            return axElements(value)
        }
        if let value = copy(element, AXAttr.childrenInNavOrder) {
            return axElements(value)
        }
        return []
    }

    static func point(_ element: AXUIElement) -> CGPoint? {
        guard let raw = copy(element, AXAttr.position),
              CFGetTypeID(raw) == AXValueGetTypeID() else {
            return nil
        }
        let axValue = unsafeBitCast(raw, to: AXValue.self)
        var point = CGPoint.zero
        return AXValueGetValue(axValue, .cgPoint, &point) ? point : nil
    }

    static func size(_ element: AXUIElement) -> CGSize? {
        guard let raw = copy(element, AXAttr.size),
              CFGetTypeID(raw) == AXValueGetTypeID() else { return nil }
        var size = CGSize.zero
        guard AXValueGetValue(raw as! AXValue, .cgSize, &size) else { return nil }
        return size
    }

    static func role(_ element: AXUIElement) -> String {
        string(element, AXAttr.role) ?? "AXUnknown"
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

    var labels: [String] {
        [title, description, value, help, identifier].compactMap { $0 }
    }
}

struct AXHit {
    var element: AXUIElement
    var snap: AXSnapshot
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
        hits(
            of: root,
            prefix: prefix,
            maxDepth: maxDepth,
            maxNodes: maxNodes,
            inList: inList,
            inMenuBar: inMenuBar
        ).map(\.snap)
    }

    static func hits(
        of root: AXUIElement,
        prefix: String,
        maxDepth: Int,
        maxNodes: Int,
        inList: Bool = false,
        inMenuBar: Bool = false
    ) -> [AXHit] {
        var out: [AXHit] = []
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
        into out: inout [AXHit]
    ) {
        guard out.count < maxNodes else { return }
        let role = AXNode.role(element)
        let listed = inList || role == AXRoleName.list
        let menu = inMenuBar || role == AXRoleName.menuBar || role == AXRoleName.menu
        let snap = AXSnapshot(
            path: path,
            role: role,
            subrole: AXNode.string(element, AXAttr.subrole),
            title: AXNode.string(element, AXAttr.title),
            description: AXNode.string(element, AXAttr.description),
            value: AXNode.string(element, AXAttr.value),
            identifier: AXNode.string(element, AXAttr.identifier),
            help: AXNode.string(element, AXAttr.help),
            mark: AXNode.string(element, AXAttr.mark),
            position: AXNode.point(element),
            inList: listed,
            inMenuBar: menu
        )
        out.append(AXHit(element: element, snap: snap))
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

    static func hits(
        for app: NSRunningApplication,
        maxDepth: Int,
        maxNodes: Int
    ) -> [AXHit] {
        let axApp = AXUIElementCreateApplication(app.processIdentifier)
        var out: [AXHit] = []
        if let menuBar = AXNode.element(axApp, AXAttr.menuBar) {
            out += AXWalk.hits(
                of: menuBar,
                prefix: "menu",
                maxDepth: maxDepth,
                maxNodes: maxNodes,
                inMenuBar: true
            )
        }
        let windows = windows(for: axApp)
        if windows.isEmpty {
            out += AXWalk.hits(of: axApp, prefix: "app", maxDepth: maxDepth, maxNodes: maxNodes)
        } else {
            for (index, window) in windows.enumerated() {
                out += AXWalk.hits(
                    of: window,
                    prefix: "win\(index)",
                    maxDepth: maxDepth,
                    maxNodes: maxNodes
                )
            }
        }
        return out
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
        add(AXNode.element(app, AXAttr.focusedWindow))
        add(AXNode.element(app, AXAttr.mainWindow))
        AXNode.elements(app, AXAttr.windows).forEach { add($0) }
        return out
    }
}
#endif
