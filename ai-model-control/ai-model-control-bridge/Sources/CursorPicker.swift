#if os(macOS)
import ApplicationServices
import CoreGraphics
import Foundation

/// Selects Cursor models and reasoning levels by their visible accessibility
/// labels. This avoids assumptions about enabled-model order or current focus.
enum CursorPicker {
    static func matches(_ name: String, focus: FocusOperation, effort: Bool) -> Bool {
        guard let label = selectedControlLabel(focus: focus) else { return false }
        if effort {
            guard let model = modelName(in: label) else { return false }
            return label.caseInsensitiveCompare(model + " " + name) == .orderedSame
        }
        return modelName(in: label)?.caseInsensitiveCompare(name) == .orderedSame
    }

    static func selectedModel(focus: FocusOperation) -> String? {
        guard let label = selectedControlLabel(focus: focus) else { return nil }
        return modelName(in: label)
    }

    // Cursor may expose a model control in the editor and another in Agents.
    // The active chat composer is the last one in the focused window's AX tree.
    private static func selectedControlLabel(focus: FocusOperation) -> String? {
        guard let root = root(for: focus) else { return nil }
        var selected: String?
        _ = find(in: root, where: {
            if let label = modelControlLabel($0) { selected = label }
            return false
        })
        return selected
    }

    /// Cursor exposes the popup's visible model name on a child text element
    /// in some layouts, leaving the popup's own title and description empty.
    static func modelControlLabel(_ element: AXUIElement) -> String? {
        guard role(element) == "AXPopUpButton" else { return nil }
        var label: String?
        _ = find(in: element, where: {
            for attribute in [kAXTitleAttribute as String,
                              kAXDescriptionAttribute as String,
                              kAXValueAttribute as String] {
                let value = string($0, attribute)
                if modelName(in: value) != nil {
                    label = value
                    return true
                }
            }
            return false
        })
        return label
    }

    static func model(
        name: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Switcher.Result {
        guard let root = root(for: focus),
              let menu = find(in: root, where: {
                  role($0) == "AXMenu" && description($0) == "Model selection"
              }) else { return .failed("Cursor model menu unavailable") }
        // Read the live row order so Cursor settings can reorder enabled models.
        let rows = menuItems(in: menu)
        guard let index = rows.firstIndex(where: { modelName(in: title($0)) == name }) else {
            closeMenus(pulse: pulse)
            return .failed("Cursor model \(name) is unavailable")
        }
        // Command-/ focuses the empty search field; first Down highlights Auto.
        guard step(Keys.down, count: index + 1, pulse: pulse),
              Keys.key(Keys.return, pulse: pulse),
              Keys.wait(Keys.pickerTiming, pulse: pulse) else { return .interrupted }
        guard selectedModel(focus: focus)?.caseInsensitiveCompare(name) == .orderedSame else {
            return .failed("Cursor did not select model \(name)")
        }
        return .applied(path: "keyboard model \(name)")
    }

    static func effort(
        name: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Switcher.Result {
        guard step(Keys.left, count: 1, pulse: pulse),
              step(Keys.up, count: 1, pulse: pulse),
              step(Keys.right, count: 1, pulse: pulse),
              Keys.wait(Keys.pickerTiming, pulse: pulse) else { return .interrupted }
        guard let root = root(for: focus),
              let menu = find(in: root, where: {
                  role($0) == "AXMenu" &&
                      ["effort options", "reasoning options", "reasoning effort options"]
                          .contains(description($0).lowercased())
              }) else {
            closeMenus(pulse: pulse)
            return .failed("Cursor effort menu unavailable")
        }
        let rows = menuItems(in: menu)
        guard let index = rows.firstIndex(where: {
            title($0).caseInsensitiveCompare(name) == .orderedSame
        }) else {
            closeMenus(pulse: pulse)
            return .failed("Cursor effort \(name) is unavailable")
        }
        // Right enters at the first level, independently of the saved effort.
        guard step(Keys.down, count: index, pulse: pulse),
              Keys.key(Keys.return, pulse: pulse),
              Keys.wait(Keys.pickerTiming, pulse: pulse) else { return .interrupted }
        // Effort can leave both submenu and parent open. Let each Escape
        // settle before restoring composer focus, or the second can be lost.
        for _ in 0..<2 {
            guard Keys.key(Keys.escape, pulse: pulse),
                  Keys.wait(Keys.pickerTiming, pulse: pulse) else { return .interrupted }
        }
        if let currentRoot = Self.root(for: focus), find(in: currentRoot, where: {
            guard role($0) == "AXMenu" else { return false }
            let label = description($0).lowercased()
            return label == "model selection" || label.hasSuffix(" parameters")
                || label == "effort options" || label == "reasoning options"
        }) != nil {
            return .failed("Cursor effort selected but picker did not close")
        }
        guard !pulse() else { return .interrupted }
        _ = PromptFocus.ensure(pid: focus.pid, kind: .cursor)
        return .applied(path: "keyboard effort \(name)")
    }

    private static func step(_ key: UInt16, count: Int, pulse: @escaping () -> Bool) -> Bool {
        for _ in 0..<count {
            guard Keys.key(key, pulse: pulse),
                  Keys.wait(Keys.keystrokeDelay, pulse: pulse) else { return false }
        }
        return !pulse()
    }

    private static func menuItems(in root: AXUIElement) -> [AXUIElement] {
        var rows: [AXUIElement] = []
        _ = find(in: root, where: {
            if role($0) == "AXMenuItem" { rows.append($0) }
            return false
        })
        return rows
    }

    // Model rows can append descriptive text. Resolve the longest catalog name
    // first so GPT-5.4 Mini cannot be selected for GPT-5.4.
    private static func modelName(in label: String) -> String? {
        Catalog.cursorModels.sorted { $0.count > $1.count }.first {
            label.caseInsensitiveCompare($0) == .orderedSame ||
                label.lowercased().hasPrefix($0.lowercased() + " ") ||
                label.lowercased().hasPrefix($0.lowercased() + "\n")
        }
    }

    private static func root(for focus: FocusOperation) -> AXUIElement? {
        let app = AXUIElementCreateApplication(focus.pid)
        AXUIElementSetMessagingTimeout(app, 0.4)
        guard let window = copy(app, kAXFocusedWindowAttribute as String),
              CFGetTypeID(window) == AXUIElementGetTypeID() else { return nil }
        return (window as! AXUIElement)
    }

    private static func find(
        in root: AXUIElement,
        where matches: (AXUIElement) -> Bool
    ) -> AXUIElement? {
        var visited = 0
        func walk(_ element: AXUIElement) -> AXUIElement? {
            guard visited < 5000 else { return nil }
            visited += 1
            if matches(element) { return element }
            guard let children = copy(element, kAXChildrenAttribute as String) as? [AXUIElement] else {
                return nil
            }
            for child in children {
                if let found = walk(child) { return found }
            }
            return nil
        }
        return walk(root)
    }

    private static func closeMenus(pulse: @escaping () -> Bool) {
        _ = Keys.key(Keys.escape, pulse: pulse)
        _ = Keys.wait(Keys.keystrokeDelay, pulse: pulse)
        _ = Keys.key(Keys.escape, pulse: pulse)
    }

    private static func copy(_ element: AXUIElement, _ attribute: String) -> AnyObject? {
        var value: AnyObject?
        return AXUIElementCopyAttributeValue(element, attribute as CFString, &value) == .success ? value : nil
    }

    private static func string(_ element: AXUIElement, _ attribute: String) -> String {
        copy(element, attribute) as? String ?? ""
    }

    private static func role(_ element: AXUIElement) -> String {
        string(element, kAXRoleAttribute as String)
    }

    private static func title(_ element: AXUIElement) -> String {
        string(element, kAXTitleAttribute as String)
    }

    private static func description(_ element: AXUIElement) -> String {
        string(element, kAXDescriptionAttribute as String)
    }
}
#endif
