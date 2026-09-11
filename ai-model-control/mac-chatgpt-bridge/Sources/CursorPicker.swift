#if os(macOS)
import ApplicationServices
import CoreGraphics
import Foundation

/// Selects Cursor models and reasoning levels by their visible accessibility
/// labels. This avoids assumptions about enabled-model order or current focus.
enum CursorPicker {
    static func matches(_ name: String, focus: FocusOperation, effort: Bool) -> Bool {
        control(name, focus: focus, effort: effort) != nil
    }

    private static func control(_ name: String, focus: FocusOperation, effort: Bool) -> AXUIElement? {
        guard let root = root(for: focus) else { return nil }
        return find(in: root, where: {
            guard role($0) == "AXPopUpButton" else { return false }
            let label = title($0).isEmpty ? description($0) : title($0)
            if label.caseInsensitiveCompare(name) == .orderedSame { return true }
            if effort {
                return Catalog.cursorModels.contains {
                    label.caseInsensitiveCompare($0 + " " + name) == .orderedSame
                }
            }
            return Catalog.cursorEfforts(for: name)?.contains {
                label.caseInsensitiveCompare(name + " " + $0) == .orderedSame
            } == true
        })
    }

    static func openEffort(focus: FocusOperation, pulse: @escaping () -> Bool) -> Bool {
        for name in Catalog.cursorThinking {
            if let button = control(name, focus: focus, effort: true) {
                return click(button, pulse: pulse) && Keys.wait(Keys.modelTiming, pulse: pulse)
            }
        }
        return false
    }

    static func model(
        name: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Switcher.Result {
        guard let root = root(for: focus) else {
            return .failed("Cursor focused window unavailable")
        }
        guard let menu = find(in: root, where: {
            role($0) == "AXMenu" && description($0).caseInsensitiveCompare("Model selection") == .orderedSame
        }) else {
            closeMenus(pulse: pulse)
            return .failed("Cursor model menu unavailable")
        }
        guard let choice = find(in: menu, where: {
            role($0) == "AXMenuItem" && modelName(in: title($0)) == name
        }) else {
            closeMenus(pulse: pulse)
            return .failed("Cursor model \(name) is unavailable")
        }
        guard click(choice, pulse: pulse), Keys.wait(Keys.modelTiming, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not select Cursor model \(name)")
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }
        for _ in 0..<3 {
            guard !pulse() else { return .interrupted }
            if matches(name, focus: focus, effort: false) {
                return .applied(path: "Accessibility verified model \(name)")
            }
            guard Keys.wait(Keys.modelTiming, pulse: pulse) else { return .interrupted }
        }
        closeMenus(pulse: pulse)
        return .failed("Cursor model could not be verified")
    }

    static func effort(
        name: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Switcher.Result {
        guard let root = root(for: focus) else {
            return .failed("Cursor focused window unavailable")
        }
        if find(in: root, where: {
            role($0) == "AXMenu" && description($0).caseInsensitiveCompare("Reasoning options") == .orderedSame
        }) == nil {
        guard let parameters = find(in: root, where: {
            role($0) == "AXMenu" && description($0).lowercased().hasSuffix(" parameters")
        }), let reasoning = find(in: parameters, where: {
            role($0) == "AXMenuItem" && title($0).hasPrefix("Reasoning ")
        }) else {
            closeMenus(pulse: pulse)
            return .failed("Cursor reasoning menu unavailable")
        }
        guard click(reasoning, pulse: pulse), Keys.wait(Keys.modelTiming, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not open Cursor reasoning menu")
        }
        }
        guard let menu = find(in: root, where: {
            role($0) == "AXMenu" && description($0).caseInsensitiveCompare("Reasoning options") == .orderedSame
        }), let choice = find(in: menu, where: {
            role($0) == "AXMenuItem" && title($0).caseInsensitiveCompare(name) == .orderedSame
        }) else {
            closeMenus(pulse: pulse)
            return .failed("Cursor effort \(name) is unavailable")
        }
        guard click(choice, pulse: pulse), Keys.wait(Keys.modelTiming, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not select Cursor effort \(name)")
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }
        // Cursor can retain the parent parameters menu after choosing an effort.
        // Only dismiss menus belonging to this picker, and stop if focus changes.
        for _ in 0..<3 {
            guard !pulse() else { return .interrupted }
            guard find(in: root, where: {
                role($0) == "AXMenu" &&
                    (description($0).lowercased().hasSuffix(" parameters") ||
                     description($0).caseInsensitiveCompare("Reasoning options") == .orderedSame)
            }) != nil else {
                _ = PromptFocus.ensure(pid: focus.pid, kind: .cursor)
                for _ in 0..<3 {
                    guard !pulse() else { return .interrupted }
                    if matches(name, focus: focus, effort: true) {
                        return .applied(path: "Accessibility verified effort \(name)")
                    }
                    guard Keys.wait(Keys.modelTiming, pulse: pulse) else { return .interrupted }
                }
                return .failed("Cursor effort could not be verified")
            }
            guard Keys.key(Keys.escape, pulse: pulse),
                  Keys.wait(Keys.modelTiming, pulse: pulse) else { return .interrupted }
        }
        return .failed("Cursor effort selected but picker did not close")
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

    private static func click(_ element: AXUIElement, pulse: @escaping () -> Bool) -> Bool {
        guard !pulse(),
              let positionRaw = copy(element, kAXPositionAttribute as String),
              let sizeRaw = copy(element, kAXSizeAttribute as String)
        else { return false }
        let positionValue = positionRaw as! AXValue
        let sizeValue = sizeRaw as! AXValue
        var position = CGPoint.zero
        var size = CGSize.zero
        AXValueGetValue(positionValue, .cgPoint, &position)
        AXValueGetValue(sizeValue, .cgSize, &size)
        return Keys.click(
            CGPoint(x: position.x + size.width / 2, y: position.y + size.height / 2),
            pulse: pulse
        )
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
