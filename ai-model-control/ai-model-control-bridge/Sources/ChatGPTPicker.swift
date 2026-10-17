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
        guard AXQuery.click(control, pulse: pulse), Keys.wait(Keys.pickerTiming, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not open the ChatGPT model control")
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }

        guard let selectModel = AXQuery.find(in: root, where: {
            AXQuery.description($0) == "Select model"
        }) else {
            closeMenus(pulse: pulse)
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
            closeMenus(pulse: pulse)
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

    private static func closeMenus(pulse: @escaping () -> Bool) {
        _ = Keys.key(Keys.escape, pulse: pulse)
        _ = Keys.wait(Keys.keystrokeDelay, pulse: pulse)
        _ = Keys.key(Keys.escape, pulse: pulse)
    }
}
#endif
