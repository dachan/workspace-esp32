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

        if let stopped = closeOpenMenu(focus: focus, pulse: pulse) { return stopped }
        guard let control = findModelControl(in: root) else {
            return .failed("ChatGPT model control unavailable")
        }
        guard AXQuery.click(control, pulse: pulse), Keys.wait(Keys.pickerTiming, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not open the ChatGPT model control")
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }

        guard let selectModel = AXQuery.find(in: root, where: {
            AXQuery.description($0) == "Select model"
        }) else {
            _ = closeOpenMenu(focus: focus, pulse: pulse)
            return .failed("ChatGPT Select model menu unavailable")
        }
        guard AXQuery.click(selectModel, pulse: pulse), Keys.wait(Keys.pickerTiming, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not open the ChatGPT model list")
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }

        guard let choice = AXQuery.find(in: root, where: {
            AXQuery.role($0) == "AXMenuItem" && AXQuery.title($0).caseInsensitiveCompare(name) == .orderedSame
        }) else {
            _ = closeOpenMenu(focus: focus, pulse: pulse)
            return .failed("ChatGPT model \(name) is unavailable")
        }
        guard AXQuery.click(choice, pulse: pulse), Keys.wait(Keys.pickerTiming, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not select \(name)")
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }
        return .applied(path: "Accessibility Select model \(name)")
    }

    private static func findModelControl(in root: AXUIElement) -> AXUIElement? {
        AXQuery.find(in: root, where: { element in
            guard AXQuery.role(element) == "AXPopUpButton" else { return false }
            let current = AXQuery.title(element)
            return Catalog.models.contains { current.hasPrefix($0) }
        })
    }

    /// Closes a model menu an earlier, interrupted apply left open, one Escape
    /// per visible level. Escape is sent only while a menu row is visible, so a
    /// closed menu never passes Escape through to the composer.
    static func closeOpenMenu(focus: FocusOperation, pulse: @escaping () -> Bool) -> Switcher.Result? {
        let root = AXUIElementCreateApplication(focus.pid)
        AXUIElementSetMessagingTimeout(root, 0.4)
        for _ in 0..<2 {
            guard menuOpen(root) else { return nil }
            guard Keys.key(Keys.escape, pulse: pulse),
                  Keys.wait(Keys.keystrokeDelay, pulse: pulse) else { return .interrupted }
        }
        return nil
    }

    private static func menuOpen(_ root: AXUIElement) -> Bool {
        AXQuery.find(in: root, where: { element in
            guard AXQuery.role(element) == "AXMenuItem" else { return false }
            if AXQuery.description(element) == "Select model" { return true }
            let title = AXQuery.title(element)
            return Catalog.models.contains { title.caseInsensitiveCompare($0) == .orderedSame }
        }) != nil
    }
}
#endif
