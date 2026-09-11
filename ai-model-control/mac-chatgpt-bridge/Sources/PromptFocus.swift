#if os(macOS)
import ApplicationServices
import Foundation

/// Give the composer keyboard focus by Accessibility identity, not by click.
/// AXPress / HID clicks use Chromium's hit-test and land on the title-bar
/// settings gear. Setting AXFocused + the caret range targets the node itself.
enum PromptFocus {
    enum Outcome {
        case focused
        case missing
    }

    static func ensure(pid: pid_t, kind: DeskKind) -> Outcome {
        let app = AXUIElementCreateApplication(pid)
        AXUIElementSetMessagingTimeout(app, 0.4)
        let root: AXUIElement
        if kind == .cursor {
            guard let window = copy(app, kAXFocusedWindowAttribute as String),
                  CFGetTypeID(window) == AXUIElementGetTypeID() else { return .missing }
            root = window as! AXUIElement
        } else {
            root = app
        }
        guard let field = findField(in: root, kind: kind) else {
            return .missing
        }
        _ = AXUIElementSetAttributeValue(
            field, kAXFocusedAttribute as CFString, kCFBooleanTrue
        )
        placeCaret(in: field)
        return .focused
    }

    private static func findField(in root: AXUIElement, kind: DeskKind) -> AXUIElement? {
        var best: (score: Int, el: AXUIElement)?
        var visited = 0

        func walk(_ el: AXUIElement) {
            if visited >= 4000 { return }
            visited += 1
            let score = matchScore(el, kind: kind)
            if score > (best?.score ?? 0) {
                best = (score, el)
            }
            if let children = copy(el, kAXChildrenAttribute as String) as? [AXUIElement] {
                for child in children {
                    walk(child)
                }
            }
        }
        walk(root)
        return best?.el
    }

    private static func matchScore(_ el: AXUIElement, kind: DeskKind) -> Int {
        let classes = classList(el)
        let role = string(el, kAXRoleAttribute as String)
        let desc = string(el, kAXDescriptionAttribute as String).lowercased()
        let placeholder = string(el, "AXPlaceholderValue").lowercased()
        switch kind {
        case .openCode:
            return (role == "AXTextArea" || role == "AXTextField") && desc == "prompt" ? 100 : 0
        case .cursor:
            if classes.contains("aislash-editor-input") { return 100 }
            if desc.contains("follow-up") || placeholder.contains("follow-up") { return 80 }
            return 0
        case .chatGPT:
            if classes.contains("ProseMirror"), role == "AXTextArea" || role == "AXTextField" {
                return 90
            }
            if desc.contains("do anything") || placeholder.contains("do anything") { return 80 }
            return 0
        }
    }

    /// Move the insertion point into the field so it becomes first responder
    /// without selecting (or replacing) existing text.
    private static func placeCaret(in el: AXUIElement) {
        let length: Int
        if let number = copy(el, kAXNumberOfCharactersAttribute as String) as? NSNumber {
            length = number.intValue
        } else {
            length = 0
        }
        var range = CFRange(location: length, length: 0)
        guard let value = AXValueCreate(.cfRange, &range) else { return }
        _ = AXUIElementSetAttributeValue(
            el, kAXSelectedTextRangeAttribute as CFString, value
        )
    }

    private static func copy(_ el: AXUIElement, _ attr: String) -> AnyObject? {
        var value: AnyObject?
        return AXUIElementCopyAttributeValue(el, attr as CFString, &value) == .success ? value : nil
    }

    private static func string(_ el: AXUIElement, _ attr: String) -> String {
        copy(el, attr) as? String ?? ""
    }

    private static func classList(_ el: AXUIElement) -> [String] {
        copy(el, "AXDOMClassList") as? [String] ?? []
    }
}
#endif
