#if os(macOS)
import ApplicationServices
import CoreGraphics
import Foundation

/// Selects a ChatGPT/Codex model by its visible accessibility label. The app's
/// menu order may change, so this intentionally does not use arrow offsets.
enum ChatGPTPicker {
    static func select(
        name: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Switcher.Result {
        let root = AXUIElementCreateApplication(focus.pid)
        AXUIElementSetMessagingTimeout(root, 0.4)

        guard let control = findModelControl(in: root) else {
            return .failed("ChatGPT model control unavailable")
        }
        guard click(control, pulse: pulse), Keys.wait(Keys.modelTiming, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not open the ChatGPT model control")
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }

        guard let selectModel = find(in: root, where: {
            description($0) == "Select model"
        }) else {
            closeMenus(pulse: pulse)
            return .failed("ChatGPT Select model menu unavailable")
        }
        guard click(selectModel, pulse: pulse), Keys.wait(Keys.modelTiming, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not open the ChatGPT model list")
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }

        guard let choice = find(in: root, where: {
            role($0) == "AXMenuItem" && title($0).caseInsensitiveCompare(name) == .orderedSame
        }) else {
            closeMenus(pulse: pulse)
            return .failed("ChatGPT model \(name) is unavailable")
        }
        guard click(choice, pulse: pulse), Keys.wait(Keys.modelTiming, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not select \(name)")
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }
        return .applied(path: "Accessibility Select model \(name)")
    }

    private static func findModelControl(in root: AXUIElement) -> AXUIElement? {
        find(in: root, where: { element in
            guard role(element) == "AXPopUpButton" else { return false }
            let current = title(element)
            return Catalog.models.contains { current.hasPrefix($0) }
        })
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
