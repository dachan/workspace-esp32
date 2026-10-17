#if os(macOS)
import ApplicationServices
import CoreGraphics
import Foundation

/// Bounded Accessibility lookups and clicks shared by label-driven pickers.
enum AXQuery {
    static func find(
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

    /// Posts a tagged mouse click at the element's validated center.
    static func click(_ element: AXUIElement, pulse: @escaping () -> Bool) -> Bool {
        guard !pulse(),
              let positionRaw = copy(element, kAXPositionAttribute as String),
              let sizeRaw = copy(element, kAXSizeAttribute as String)
        else { return false }
        guard CFGetTypeID(positionRaw) == AXValueGetTypeID(),
              CFGetTypeID(sizeRaw) == AXValueGetTypeID() else { return false }
        let positionValue = positionRaw as! AXValue
        let sizeValue = sizeRaw as! AXValue
        guard AXValueGetType(positionValue) == .cgPoint,
              AXValueGetType(sizeValue) == .cgSize else { return false }
        var position = CGPoint.zero
        var size = CGSize.zero
        guard AXValueGetValue(positionValue, .cgPoint, &position),
              AXValueGetValue(sizeValue, .cgSize, &size),
              position.x.isFinite, position.y.isFinite,
              size.width.isFinite, size.height.isFinite,
              size.width > 0, size.height > 0 else { return false }
        return Keys.click(
            CGPoint(x: position.x + size.width / 2, y: position.y + size.height / 2),
            pulse: pulse
        )
    }

    static func copy(_ element: AXUIElement, _ attribute: String) -> AnyObject? {
        var value: AnyObject?
        return AXUIElementCopyAttributeValue(element, attribute as CFString, &value) == .success ? value : nil
    }

    static func string(_ element: AXUIElement, _ attribute: String) -> String {
        copy(element, attribute) as? String ?? ""
    }

    static func role(_ element: AXUIElement) -> String {
        string(element, kAXRoleAttribute as String)
    }

    static func title(_ element: AXUIElement) -> String {
        string(element, kAXTitleAttribute as String)
    }

    static func description(_ element: AXUIElement) -> String {
        string(element, kAXDescriptionAttribute as String)
    }
}
#endif
