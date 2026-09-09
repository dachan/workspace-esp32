#if os(macOS)
import ApplicationServices
import Foundation

/// OpenCode's encoder order is reversed from its native Luna-first picker.
/// Effort uses the absolute variant menu.
enum OpenCodeApply {
    private static var openPickers: Set<Int32> = []

    static func model(_ raw: String, focus: FocusOperation, pulse: @escaping () -> Bool) -> Switcher.Result {
        guard let name = Catalog.openCodeModelName(raw),
              let pickerIndex = Catalog.openCodePickerIndex(raw) else {
            return .failed("unknown OpenCode model \(raw)")
        }
        if let result = prepare(focus, pulse: pulse) { return result }
        openPickers.insert(focus.pid)
        guard Keys.command(Keys.apostrophe, pulse: pulse), Keys.wait(0.45, pulse: pulse) else {
            return .interrupted
        }
        // The native menu starts before Luna, so the first Down selects Luna.
        for _ in 0...pickerIndex {
            guard Keys.key(Keys.down, pulse: pulse), Keys.wait(0.05, pulse: pulse) else {
                return .interrupted
            }
        }
        guard Keys.key(Keys.return, pulse: pulse), Keys.wait(0.25, pulse: pulse) else {
            return .interrupted
        }
        openPickers.remove(focus.pid)
        return .applied(path: "OpenCode Command-' Down \(pickerIndex + 1) Return \(name)")
    }

    static func thinking(_ raw: String, focus: FocusOperation, pulse: @escaping () -> Bool) -> Switcher.Result {
        guard let index = Catalog.chatgptThinkingIndex(raw) else {
            return .failed("unknown OpenCode effort \(raw)")
        }
        let name = ["Low", "Medium", "High", "Xhigh"][index]
        if let result = prepare(focus, pulse: pulse) { return result }
        guard let button = find(focus.pid, name: "Choose model variant", roles: ["AXPopUpButton"]) else {
            return .failed("OpenCode variant control unavailable")
        }
        guard !pulse() else { return .interrupted }
        openPickers.insert(focus.pid)
        guard AXUIElementPerformAction(button, kAXPressAction as CFString) == .success else {
            return .failed("could not open OpenCode variants")
        }
        guard Keys.wait(0.25, pulse: pulse) else { return .interrupted }
        return select(name, roles: ["AXMenuItem"], focus: focus, pulse: pulse)
    }

    private static func prepare(_ focus: FocusOperation, pulse: @escaping () -> Bool) -> Switcher.Result? {
        guard !pulse() else { return .interrupted }
        if openPickers.contains(focus.pid) {
            guard Keys.key(Keys.escape, pulse: pulse), Keys.wait(0.15, pulse: pulse) else {
                return .interrupted
            }
            openPickers.remove(focus.pid)
        }
        guard PromptFocus.ensure(pid: focus.pid, kind: .openCode) == .focused else {
            return .failed("OpenCode prompt unavailable")
        }
        return pulse() ? .interrupted : nil
    }

    private static func select(
        _ name: String, roles: Set<String>, focus: FocusOperation, pulse: @escaping () -> Bool
    ) -> Switcher.Result {
        guard let item = find(focus.pid, name: name, roles: roles) else {
            if Keys.key(Keys.escape, pulse: pulse) { openPickers.remove(focus.pid) }
            return pulse() ? .interrupted : .failed("OpenCode has no unique available \(name) choice")
        }
        guard !pulse() else { return .interrupted }
        guard AXUIElementPerformAction(item, kAXPressAction as CFString) == .success else {
            return .failed("could not select OpenCode \(name)")
        }
        guard Keys.wait(0.25, pulse: pulse) else { return .interrupted }
        openPickers.remove(focus.pid)
        return .applied(path: "OpenCode accessible choice \(name)")
    }

    private static func find(_ pid: Int32, name: String, roles: Set<String>) -> AXUIElement? {
        let root = AXUIElementCreateApplication(pid)
        AXUIElementSetMessagingTimeout(root, 0.4)
        var matches: [AXUIElement] = []
        var visited = 0
        func attribute(_ el: AXUIElement, _ key: String) -> AnyObject? {
            var value: AnyObject?
            return AXUIElementCopyAttributeValue(el, key as CFString, &value) == .success ? value : nil
        }
        func walk(_ el: AXUIElement) {
            guard visited < 4000 else { return }
            visited += 1
            let role = attribute(el, kAXRoleAttribute as String) as? String ?? ""
            let labels = [kAXTitleAttribute, kAXDescriptionAttribute].compactMap {
                attribute(el, $0 as String) as? String
            }
            if roles.contains(role), labels.contains(where: { $0.caseInsensitiveCompare(name) == .orderedSame }),
               (attribute(el, kAXEnabledAttribute as String) as? Bool) != false {
                matches.append(el)
            }
            for child in attribute(el, kAXChildrenAttribute as String) as? [AXUIElement] ?? [] { walk(child) }
        }
        walk(root)
        // Multiple providers can expose the same model name; never guess.
        return matches.count == 1 ? matches[0] : nil
    }
}
#endif
