#if os(macOS)
import AppKit
import ApplicationServices
import Foundation

/// Claude desktop: the composer's "Model: Opus 5.5 Medium" pop-up opens one menu
/// with model rows and an Effort submenu. Its documented Command-Shift-I/E
/// shortcuts do not respond to posted keys, so the bridge presses that control
/// by Accessibility, then presses rows by visible label (never by digit).
enum ClaudePicker {
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
        return withMenu(
            target: name, focus: focus, preferred: preferred, pulse: pulse,
            act: { root, status in
                // The menu is open anyway: refresh the dial's live model list.
                Catalog.setClaudeModels(modelRows(in: root, excluding: status))
                return pressChoice(name, in: root, excluding: status, pulse: pulse)
            },
            confirmed: { status in status.hasPrefix(name + " ") }
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
        return withMenu(
            target: label, focus: focus, preferred: preferred, pulse: pulse,
            act: { root, status in
                let visible = { findChoice(label, in: root, excluding: status) != nil }
                if !visible() {
                    guard let effort = findChoice("Effort", in: root, excluding: status) else {
                        return .failed("Claude Effort row unavailable")
                    }
                    // Pressing the row highlights it but does not open the submenu; Right
                    // opens it and does nothing if it is already open.
                    guard press(effort, pulse: pulse), Keys.key(Keys.right, pulse: pulse) else {
                        return .interrupted
                    }
                    _ = poll(pulse: pulse, until: visible)
                }
                return pressChoice(label, in: root, excluding: status, pulse: pulse)
            },
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
        print("status control: \(status.map { "\(AXQuery.role($0)) \"\(statusText($0))\"" } ?? "not found")")
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
        print("elements mentioning a model or effort:")
        dumpCandidates(root) { print("  \($0)") }
        let live = modelRows(in: root, excluding: status)
        print("model rows read: \(live.isEmpty ? "none (open the model menu first)" : live.joined(separator: ", "))")
        print("dial fallback: \(Catalog.claudeFallbackModels.joined(separator: ", "))")
        return 0
    }

    /// Any-role elements whose title, description or value mentions a Claude
    /// model family or effort word, with every label source shown separately.
    private static func dumpCandidates(_ root: AXUIElement, _ emit: (String) -> Void) {
        let families = Set(Catalog.claudeModels.compactMap { $0.split(separator: " ").first.map(String.init) })
            .union(["Opus", "Sonnet", "Haiku", "Fable"])
        let words = families.union(Catalog.claudeEffortLabels).union(["Effort"])
        var visited = 0
        var lines = 0
        _ = AXQuery.find(in: root, where: { element in
            visited += 1
            let title = AXQuery.title(element)
            let description = AXQuery.description(element)
            let value = AXQuery.string(element, kAXValueAttribute as String)
            let text = "\(title) \(description) \(value)"
            // Menu rows are short; long text is chat transcript, not controls.
            guard lines < 60, text.count < 90,
                  !["AXTextArea", "AXTextField", "AXWebArea"].contains(AXQuery.role(element)),
                  words.contains(where: { text.contains($0) }) else { return false }
            lines += 1
            emit("\(AXQuery.role(element)) title=\"\(title)\" desc=\"\(description)\" value=\"\(value.prefix(60))\"")
            return false
        })
        emit("scanned \(visited) elements, \(lines) matched")
    }

    /// Opens the composer model menu, runs `act`, closes what is left open, and
    /// confirms the result from the control's own label.
    private static func withMenu(
        target: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool,
        act: (AXUIElement, AXUIElement) -> Switcher.Result,
        confirmed: (String) -> Bool
    ) -> Switcher.Result {
        let root = applicationElement(pid: focus.pid)
        guard let control = statusControl(in: root) else {
            dumpCandidates(root) { fputs("ai-model-control-bridge: Claude AX \($0)\n", stderr) }
            return .failed("Claude model control unavailable")
        }
        let before = statusText(control)
        // The control already names the model and effort; skip a no-op menu trip.
        if confirmed(before) { return .applied(path: "Claude already \(before)") }
        fputs("ai-model-control-bridge: Claude \(before) -> \(target)\n", stderr)
        guard press(control, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not open the Claude model menu")
        }
        // The menu renders asynchronously; a fixed wait raced it.
        let opened = poll(pulse: pulse) {
            findChoice("Effort", in: root, excluding: control) != nil
                || !modelRows(in: root, excluding: control).isEmpty
        }
        guard !pulse() else { return .interrupted }
        guard opened else { return .failed("Claude model menu did not open") }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }
        let result = act(root, control)
        guard case .applied = result else {
            if case .failed = result {
                // Record what the open menu exposes so a label/role mismatch is diagnosable.
                dumpCandidates(root) { fputs("ai-model-control-bridge: Claude AX \($0)\n", stderr) }
            }
            closeIfOpen(root, pulse: pulse)
            return result
        }
        guard Keys.wait(Keys.pickerTiming, pulse: pulse) else { return .interrupted }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }
        closeIfOpen(root, pulse: pulse)

        guard let current = statusControl(in: root).map(statusText) else {
            return .applied(path: "Claude \(target) (no status readback)")
        }
        guard confirmed(current) else {
            return .failed("Claude shows \(current) after selecting \(target)")
        }
        return .applied(path: "Claude \(target), confirmed \(current)")
    }

    /// Polls `condition` every posted-key gap for up to a second.
    private static func poll(pulse: @escaping () -> Bool, until condition: () -> Bool) -> Bool {
        let deadline = ProcessInfo.processInfo.systemUptime + 1.0
        while ProcessInfo.processInfo.systemUptime < deadline {
            if condition() { return true }
            guard Keys.wait(Keys.keystrokeDelay, pulse: pulse) else { return false }
        }
        return condition()
    }

    private static func pressChoice(
        _ label: String, in root: AXUIElement, excluding control: AXUIElement, pulse: @escaping () -> Bool
    ) -> Switcher.Result {
        guard let item = findChoice(label, in: root, excluding: control) else {
            return .failed("Claude menu choice \(label) unavailable")
        }
        return press(item, pulse: pulse) ? .applied(path: label) : .interrupted
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
    /// Rows are searched inside open AXMenu containers when Claude exposes them,
    /// so a sidebar or transcript control can never be pressed.
    private static func findChoice(
        _ label: String, in root: AXUIElement, excluding control: AXUIElement?
    ) -> AXUIElement? {
        var menus: [AXUIElement] = []
        _ = AXQuery.find(in: root, where: { element in
            if AXQuery.role(element) == "AXMenu" { menus.append(element) }
            return false
        })
        for scope in menus.isEmpty ? [root] : menus {
            for role in choiceRoles {
                let found = AXQuery.find(in: scope, where: { element in
                    if let control, CFEqual(element, control) { return false }
                    return AXQuery.role(element) == role && matches(statusLabel(element), label: label)
                })
                if let found { return found }
            }
        }
        return nil
    }

    /// Label followed by nothing or a non-word separator: "High" matches
    /// "High" and "High, selected" but not "Higher".
    private static func matches(_ name: String, label: String) -> Bool {
        let lowered = name.lowercased()
        guard lowered.hasPrefix(label.lowercased()) else { return false }
        guard let next = lowered.dropFirst(label.count).first else { return true }
        return !(next.isLetter || next.isNumber || next == ".")
    }

    private static func statusControl(in root: AXUIElement) -> AXUIElement? {
        AXQuery.find(in: root, where: { element in
            let role = AXQuery.role(element)
            guard role == "AXButton" || role == "AXPopUpButton" else { return false }
            let words = statusText(element).split(separator: " ")
            // Model plus effort, so a bare menu row such as "Opus 5.5" never matches.
            guard words.count == 3, let model = modelRowName(words.prefix(2).joined(separator: " ")) else {
                return false
            }
            let effort = String(words[2])
            return !model.isEmpty
                && (Catalog.claudeEffortLabels.contains(effort) || effort == "Max" || effort == "Ultracode")
        })
    }

    /// The composer control's label without its "Model:" prefix: "Opus 5.5 Medium".
    private static func statusText(_ element: AXUIElement) -> String {
        let label = statusLabel(element)
        let prefix = "Model: "
        return label.hasPrefix(prefix) ? String(label.dropFirst(prefix.count)) : label
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
