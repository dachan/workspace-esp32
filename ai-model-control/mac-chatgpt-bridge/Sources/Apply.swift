#if os(macOS)
import Foundation

enum Switcher {
    private static var interruptedPickers: [Int32: Int] = [:]

    enum Result {
        case applied(path: String)
        case interrupted
        case failed(String)
    }

    /// `pulse` may drain serial / check focus; return true to abort as superseded.
    static func model(
        _ raw: String,
        preferredBundleID: String?,
        pulse: (() -> Bool)? = nil
    ) -> Result {
        guard let focus = FocusOperation(preferred: preferredBundleID) else {
            return .failed("ChatGPT, Cursor, or OpenCode is not focused")
        }
        return InputGuard.protect(focus: focus) { inputGuard in
            let upstream = wrappedPulse(focus: focus, upstream: pulse)
            let pulse = { !inputGuard.isValid || upstream() }
            switch focus.kind {
            case .openCode:
                return OpenCodeApply.model(raw, focus: focus, pulse: pulse)
            case .chatGPT:
                guard let name = Catalog.chatgptModelName(raw) else {
                    return .failed("unknown model \(raw)")
                }
                if pulse() {
                    return .interrupted
                }
                return chatGPTModel(name: name, focus: focus, preferred: preferredBundleID, pulse: pulse)
            case .cursor:
                guard let index = Catalog.cursorPickerIndex(raw), let name = Catalog.cursorModelName(raw) else {
                    return .failed("unknown model \(raw)")
                }
                if pulse() {
                    return .interrupted
                }
                return cursorModel(index: index, name: name, focus: focus, preferred: preferredBundleID, pulse: pulse)
            }
        }
    }

    static func thinking(
        _ raw: String,
        model: String? = nil,
        preferredBundleID: String?,
        pulse: (() -> Bool)? = nil
    ) -> Result {
        guard let focus = FocusOperation(preferred: preferredBundleID) else {
            return .failed("ChatGPT, Cursor, or OpenCode is not focused")
        }
        if focus.kind == .openCode {
            return .applied(path: "OpenCode effort sync unsupported; skipped")
        }
        return InputGuard.protect(focus: focus) { inputGuard in
            let upstream = wrappedPulse(focus: focus, upstream: pulse)
            let pulse = { !inputGuard.isValid || upstream() }
            switch focus.kind {
            case .openCode:
                return .applied(path: "OpenCode effort sync unsupported; skipped")
            case .chatGPT:
                guard let target = Catalog.chatgptThinkingIndex(raw), let name = Catalog.chatgptThinkingName(raw) else {
                    return .failed("unknown thinking \(raw)")
                }
                if pulse() {
                    return .interrupted
                }
                return chatGPTThinking(target: target, name: name, focus: focus, preferred: preferredBundleID, pulse: pulse)
            case .cursor:
                guard let model, let levels = Catalog.cursorEfforts(for: model) else {
                    return .failed("Cursor effort needs a known model; select a model with the dial first")
                }
                if levels.isEmpty { return .applied(path: "effort unsupported for \(model); skipped") }
                guard let effort = Catalog.cursorEffort(raw, model: model) else {
                    return .failed("unknown thinking \(raw)")
                }
                let (target, name) = effort
                if pulse() { return .interrupted }
                // Effort-only: open the popover and navigate directly to Reasoning.
                return cursorSelectEffort(
                    target: target, name: name, focus: focus, preferred: preferredBundleID, pulse: pulse
                )
            }
        }
    }

    /// ChatGPT/Codex uses visible accessibility labels, not a fixed picker order.
    private static func chatGPTModel(
        name: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Result {
        fputs("chatgpt-bridge: ChatGPT model via accessibility Select model \(name)\n", stderr)
        if let stopped = dismissInterruptedPicker(pid: focus.pid, pulse: pulse) {
            return stopped
        }
        if let stopped = primePrompt(focus: focus, preferred: preferred, pulse: pulse) {
            return stopped
        }
        return ChatGPTPicker.select(name: name, focus: focus, preferred: preferred, pulse: pulse)
    }

    /// Command-/ focuses Search; first Down is Auto, then enabled catalog order.
    private static func cursorModel(
        index: Int,
        name: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Result {
        fputs(
            "chatgpt-bridge: Cursor model via Command-/, Down \(index + 1) to \(name)\n",
            stderr
        )
        if let stopped = cursorSelectModel(
            index: index, name: name, focus: focus, preferred: preferred, pulse: pulse
        ) {
            return stopped
        }
        return .applied(path: "Command-/ Down \(index + 1) \(name)")
    }

    /// Always absolute: clamp to Light, then climb. Avoids relative desync when
    /// ChatGPT's effort was changed outside the bridge (no AX readback).
    private static func chatGPTThinking(
        target: Int,
        name: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Result {
        fputs(
            "chatgpt-bridge: reasoning absolute set via Ctrl+Shift+, then up to \(name)\n",
            stderr
        )
        if let stopped = dismissInterruptedPicker(pid: focus.pid, pulse: pulse) {
            return stopped
        }
        if let stopped = primePrompt(focus: focus, preferred: preferred, pulse: pulse) {
            return stopped
        }
        guard bump(delta: -(Catalog.thinking.count - 1), pulse: pulse) else {
            return pulse()
                ? .interrupted
                : .failed("could not clamp reasoning to Light")
        }
        guard bump(delta: target, pulse: pulse) else {
            return pulse()
                ? .interrupted
                : .failed("could not set reasoning to \(name)")
        }
        guard Keys.wait(Keys.keystrokeDelay, pulse: pulse) else {
            return .interrupted
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }
        return .applied(path: "Ctrl+Shift+,/. absolute \(name)")
    }

    private static func cursorSelectModel(
        index: Int,
        name: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Result? {
        if let stopped = openCursorPopover(focus: focus, preferred: preferred, pulse: pulse) {
            return stopped
        }
        if let stopped = repeatKey(
            Keys.down, times: index + 1, gap: Keys.keystrokeDelay, pulse: pulse,
            fail: "could not move to \(name)"
        ) {
            return stopped
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }
        guard Keys.key(Keys.return, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not confirm \(name)")
        }
        interruptedPickers.removeValue(forKey: focus.pid)
        guard Keys.wait(Keys.modelTiming, pulse: pulse) else {
            return .interrupted
        }
        return nil
    }

    private static func cursorSelectEffort(
        target: Int,
        name: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Result {
        fputs(
            "chatgpt-bridge: Cursor effort via Command-/ Left Up Right, Effort Down \(target) to \(name)\n",
            stderr
        )
        if let stopped = openCursorPopover(focus: focus, preferred: preferred, pulse: pulse) {
            return stopped
        }
        guard Keys.key(Keys.left, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not leave Cursor model list")
        }
        guard Keys.wait(Keys.keystrokeDelay, pulse: pulse) else {
            return .interrupted
        }
        guard Keys.key(Keys.up, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not move to Cursor Thinking")
        }
        guard Keys.wait(Keys.keystrokeDelay, pulse: pulse) else {
            return .interrupted
        }
        guard Keys.key(Keys.right, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not open Cursor Thinking menu")
        }
        interruptedPickers[focus.pid] = 2
        guard Keys.wait(Keys.modelTiming, pulse: pulse) else {
            return .interrupted
        }
        return pickCursorSubmenuIndex(
            target, name: name, focus: focus, preferred: preferred, pulse: pulse,
            path: "Command-/ Left Up Right Reasoning \(name)"
        )
    }

    private static func wrappedPulse(focus: FocusOperation, upstream: (() -> Bool)?) -> () -> Bool {
        {
            let superseded = upstream?() ?? false
            return !focus.isCurrent || superseded
        }
    }

    private static func primePrompt(
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Result? {
        switch focus.kind {
        case .cursor:
            // Cmd+L is Toggle Sidepanel. Sending it while Agents is already
            // open closes the right panel. Only use it to open a missing panel.
            if PromptFocus.ensure(pid: focus.pid, kind: .cursor) == .missing {
                guard Keys.command(Keys.l, pulse: pulse) else {
                    return pulse() ? .interrupted : .failed("could not post Command-L")
                }
            }
        case .chatGPT, .openCode:
            if PromptFocus.ensure(pid: focus.pid, kind: focus.kind) == .missing {
                return nil
            }
        }
        guard Keys.wait(Keys.keystrokeDelay, pulse: pulse) else { return .interrupted }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }
        return nil
    }

    private static func openCursorPopover(
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Result? {
        if let stopped = dismissInterruptedPicker(pid: focus.pid, pulse: pulse) {
            return stopped
        }
        if let stopped = primePrompt(focus: focus, preferred: preferred, pulse: pulse) {
            return stopped
        }
        interruptedPickers[focus.pid] = 1
        guard Keys.command(Keys.slash, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not post Command-/")
        }
        guard Keys.wait(Keys.modelTiming, pulse: pulse) else {
            return .interrupted
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }
        return nil
    }

    private static func pickCursorSubmenuIndex(
        _ index: Int,
        name: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool,
        path: String
    ) -> Result {
        // Right highlights the first supported entry (Low or None).
        if let stopped = repeatKey(
            Keys.down, times: index, gap: Keys.keystrokeDelay, pulse: pulse,
            fail: "could not move to \(name)"
        ) {
            return stopped
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }
        guard Keys.key(Keys.return, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not confirm \(name)")
        }
        guard Keys.wait(Keys.keystrokeDelay, pulse: pulse) else {
            return .interrupted
        }
        // Track each closed layer so superseding input cannot strand a submenu.
        if let stopped = dismissInterruptedPicker(pid: focus.pid, pulse: pulse) {
            return stopped
        }
        guard Keys.wait(Keys.keystrokeDelay, pulse: pulse) else {
            return .interrupted
        }
        return .applied(path: path)
    }

    private static func repeatKey(
        _ code: UInt16,
        times: Int,
        gap: Double,
        pulse: @escaping () -> Bool,
        fail: String
    ) -> Result? {
        for _ in 0..<times {
            guard Keys.key(code, pulse: pulse) else {
                return pulse() ? .interrupted : .failed(fail)
            }
            guard Keys.wait(gap, pulse: pulse) else {
                return .interrupted
            }
        }
        return nil
    }

    private static func dismissInterruptedPicker(pid: Int32, pulse: @escaping () -> Bool) -> Result? {
        while let remaining = interruptedPickers[pid], remaining > 0 {
            guard Keys.key(Keys.escape, pulse: pulse) else { return .interrupted }
            interruptedPickers[pid] = remaining - 1
            guard Keys.wait(Keys.keystrokeDelay, pulse: pulse) else { return .interrupted }
        }
        interruptedPickers.removeValue(forKey: pid)
        return nil
    }

    private static func bump(delta: Int, pulse: (() -> Bool)?) -> Bool {
        guard delta != 0 else { return true }
        let code: UInt16 = delta > 0 ? Keys.period : Keys.comma
        for _ in 0..<abs(delta) {
            guard Keys.controlShift(code, pulse: pulse) else { return false }
            guard Keys.wait(Keys.keystrokeDelay, pulse: pulse) else { return false }
            if pulse?() == true {
                return false
            }
        }
        return true
    }
}
#endif
