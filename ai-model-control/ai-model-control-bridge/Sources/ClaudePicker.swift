#if os(macOS)
import AppKit
import ApplicationServices
import Foundation

/// Claude desktop: Command-Shift-I opens the model menu and Command-Shift-E the
/// effort menu. Choices are pressed by visible label; the effort menu shows no
/// digit badges and the model digits shift with the account's model list.
enum ClaudePicker {
    private static let keyI: UInt16 = 0x22
    private static let keyE: UInt16 = 0x0E
    /// Most specific first: a sidebar button named "Opus 5.5 notes" must never
    /// win over a real menu row.
    private static let choiceRoles = ["AXMenuItem", "AXRadioButton", "AXButton"]

    static func model(
        _ raw: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Switcher.Result {
        guard let name = Catalog.claudeModelName(raw) else {
            return .failed("unknown Claude model \(raw)")
        }
        return choose(
            label: name, shortcut: keyI, focus: focus, preferred: preferred, pulse: pulse,
            whileOpen: { root, status in
                // The menu is open anyway: refresh the dial's live model list.
                Catalog.setClaudeModels(modelRows(in: root, excluding: status))
            },
            confirmed: { status in status.hasPrefix(name) }
        )
    }

    static func thinking(
        _ raw: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Switcher.Result {
        guard let label = Catalog.claudeEffortLabel(raw) else {
            return .failed("unknown Claude effort \(raw)")
        }
        return choose(
            label: label, shortcut: keyE, focus: focus, preferred: preferred, pulse: pulse,
            whileOpen: { _, _ in },
            confirmed: { status in status.hasSuffix(" " + label) }
        )
    }

    /// `--check-claude`: prints what the bridge would match without posting keys
    /// or clicks. Open a Claude menu by hand first to see its rows.
    static func diagnose() -> Int32 {
        print("Accessibility trusted: \(AXIsProcessTrusted())")
        guard let app = NSRunningApplication
            .runningApplications(withBundleIdentifier: DeskFront.claudeID).first else {
            print("Claude (\(DeskFront.claudeID)) is not running")
            return 1
        }
        let root = applicationElement(pid: app.processIdentifier)
        let status = statusControl(in: root)
        print("status control: \(status.map { "\(AXQuery.role($0)) \"\(statusLabel($0))\"" } ?? "not found")")
        var rows = 0
        _ = AXQuery.find(in: root, where: { element in
            let role = AXQuery.role(element)
            let name = statusLabel(element)
            if choiceRoles.contains(role), !name.isEmpty, rows < 80 {
                rows += 1
                var notes: [String] = []
                if let status, CFEqual(element, status) { notes.append("status") }
                if let model = modelRowName(name) { notes.append("model=\(model)") }
                if let effort = Catalog.claudeEffortLabels.first(where: { matches(name, label: $0) }) {
                    notes.append("effort=\(effort)")
                }
                print("  \(role) \"\(name)\"\(notes.isEmpty ? "" : "  [\(notes.joined(separator: ", "))]")")
            }
            return false
        })
        let live = modelRows(in: root, excluding: status)
        print("model rows read: \(live.isEmpty ? "none (open the model menu first)" : live.joined(separator: ", "))")
        print("dial fallback: \(Catalog.claudeFallbackModels.joined(separator: ", "))")
        return 0
    }

    private static func choose(
        label: String,
        shortcut: UInt16,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool,
        whileOpen: (AXUIElement, AXUIElement?) -> Void,
        confirmed: (String) -> Bool
    ) -> Switcher.Result {
        let root = applicationElement(pid: focus.pid)
        // The composer status ("Opus 5.5 Medium") also starts with a model name.
        let status = statusControl(in: root)

        let letter = shortcut == keyI ? "I" : "E"
        fputs("ai-model-control-bridge: Claude Command-Shift-\(letter) \(label)\n", stderr)
        guard Keys.commandShift(shortcut, pulse: pulse), Keys.wait(Keys.pickerTiming, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not open the Claude menu")
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }
        whileOpen(root, status)

        guard let item = findChoice(label: label, in: root, excluding: status) else {
            closeIfOpen(root, pulse: pulse)
            return .failed("Claude menu choice \(label) unavailable")
        }
        guard press(item, pulse: pulse), Keys.wait(Keys.pickerTiming, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not select Claude \(label)")
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }
        closeIfOpen(root, pulse: pulse)

        let path = "Claude Command-Shift-\(letter) \(label)"
        guard let current = statusControl(in: root).map(statusLabel) else {
            return .applied(path: "\(path) (no status readback)")
        }
        guard confirmed(current) else {
            return .failed("Claude shows \(current) after selecting \(label)")
        }
        return .applied(path: "\(path), confirmed \(current)")
    }

    private static func applicationElement(pid: pid_t) -> AXUIElement {
        let root = AXUIElementCreateApplication(pid)
        AXUIElementSetMessagingTimeout(root, 0.4)
        // Chromium exposes its web accessibility tree only to assistive clients.
        AXUIElementSetAttributeValue(root, "AXManualAccessibility" as CFString, kCFBooleanTrue)
        return root
    }

    /// AXPress does not depend on screen geometry or pointer position; fall
    /// back to a tagged click only when the row refuses the action.
    private static func press(_ element: AXUIElement, pulse: @escaping () -> Bool) -> Bool {
        guard !pulse() else { return false }
        if AXUIElementPerformAction(element, kAXPressAction as CFString) == .success { return true }
        return AXQuery.click(element, pulse: pulse)
    }

    /// Visible model menu rows in order, e.g. "Sonnet 5.5 Default 2" → "Sonnet 5.5".
    /// Only AXMenuItem rows count, so session titles can never enter the dial.
    private static func modelRows(in root: AXUIElement, excluding status: AXUIElement?) -> [String] {
        var names: [String] = []
        _ = AXQuery.find(in: root, where: { element in
            if let status, CFEqual(element, status) { return false }
            guard AXQuery.role(element) == "AXMenuItem",
                  let name = modelRowName(statusLabel(element)),
                  !names.contains(name) else { return false }
            names.append(name)
            return false
        })
        return names
    }

    /// A model row starts with a capitalized family and a version number.
    /// Usage-credit rows stay off the dial.
    private static func modelRowName(_ label: String) -> String? {
        guard !label.localizedCaseInsensitiveContains("usage credits") else { return nil }
        let words = label.split(separator: " ")
        guard words.count >= 2,
              let first = words[0].first, first.isUppercase, words[0].allSatisfy(\.isLetter),
              words[1].first?.isNumber == true,
              words[1].allSatisfy({ $0.isNumber || $0 == "." }) else { return nil }
        return "\(words[0]) \(words[1])"
    }

    /// A menu row's accessible name may append badges ("Sonnet 5.5 Default 2").
    private static func findChoice(
        label: String, in root: AXUIElement, excluding status: AXUIElement?
    ) -> AXUIElement? {
        for role in choiceRoles {
            let found = AXQuery.find(in: root, where: { element in
                if let status, CFEqual(element, status) { return false }
                return AXQuery.role(element) == role && matches(statusLabel(element), label: label)
            })
            if let found { return found }
        }
        return nil
    }

    private static func matches(_ name: String, label: String) -> Bool {
        name.caseInsensitiveCompare(label) == .orderedSame
            || name.lowercased().hasPrefix(label.lowercased() + " ")
    }

    private static func statusControl(in root: AXUIElement) -> AXUIElement? {
        AXQuery.find(in: root, where: { element in
            let role = AXQuery.role(element)
            guard role == "AXButton" || role == "AXPopUpButton" else { return false }
            let words = statusLabel(element).split(separator: " ")
            // Model plus effort, so a bare menu row such as "Opus 5.5" never matches.
            guard words.count == 3, let model = modelRowName(words.prefix(2).joined(separator: " ")) else {
                return false
            }
            let effort = String(words[2])
            return !model.isEmpty
                && (Catalog.claudeEffortLabels.contains(effort) || effort == "Max" || effort == "Ultracode")
        })
    }

    private static func statusLabel(_ element: AXUIElement) -> String {
        let title = AXQuery.title(element)
        let name = title.isEmpty ? AXQuery.description(element) : title
        return name.split(whereSeparator: \.isWhitespace).joined(separator: " ")
    }

    /// Escape only while a model or effort row is visible: with no menu open,
    /// Escape in Claude's Code tab stops the running response. Effort can be a
    /// submenu of the model menu, so allow one Escape per level.
    private static func closeIfOpen(_ root: AXUIElement, pulse: @escaping () -> Bool) {
        for _ in 0..<2 {
            let open = AXQuery.find(in: root, where: { element in
                guard AXQuery.role(element) == "AXMenuItem" else { return false }
                let name = statusLabel(element)
                return modelRowName(name) != nil
                    || Catalog.claudeEffortLabels.contains { matches(name, label: $0) }
            }) != nil
            guard open, Keys.key(Keys.escape, pulse: pulse),
                  Keys.wait(Keys.keystrokeDelay, pulse: pulse) else { return }
        }
    }
}
#endif
