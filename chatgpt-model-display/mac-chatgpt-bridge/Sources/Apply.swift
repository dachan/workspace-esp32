#if os(macOS)
import Foundation

enum Switcher {
    private static var interruptedPickers: Set<Int32> = []

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
            return .failed("ChatGPT or Cursor is not focused")
        }
        let pulse = wrappedPulse(focus: focus, upstream: pulse)
        switch focus.kind {
        case .chatGPT:
            guard let index = Catalog.chatgptModelIndex(raw), let name = Catalog.chatgptModelName(raw) else {
                return .failed("unknown model \(raw)")
            }
            if pulse() {
                return .interrupted
            }
            return chatGPTModel(index: index, name: name, focus: focus, preferred: preferredBundleID, pulse: pulse)
        case .cursor:
            guard let index = Catalog.cursorModelIndex(raw), let name = Catalog.cursorModelName(raw) else {
                return .failed("unknown model \(raw)")
            }
            if pulse() {
                return .interrupted
            }
            return cursorModel(index: index, name: name, focus: focus, preferred: preferredBundleID, pulse: pulse)
        }
    }

    static func thinking(
        _ raw: String,
        model: String? = nil,
        preferredBundleID: String?,
        pulse: (() -> Bool)? = nil
    ) -> Result {
        guard let focus = FocusOperation(preferred: preferredBundleID) else {
            return .failed("ChatGPT or Cursor is not focused")
        }
        let pulse = wrappedPulse(focus: focus, upstream: pulse)
        switch focus.kind {
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

    /// Ctrl+Shift+M opens the picker on Astra; Down N to the dial index; Return.
    private static func chatGPTModel(
        index: Int,
        name: String,
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Result {
        fputs(
            "chatgpt-bridge: open model picker via Ctrl+Shift+M, Down \(index) to \(name)\n",
            stderr
        )
        if let stopped = dismissInterruptedPicker(pid: focus.pid, pulse: pulse) {
            return stopped
        }
        interruptedPickers.insert(focus.pid)
        guard Keys.controlShift(Keys.m, pulse: pulse) else {
            return pulse()
                ? .interrupted
                : .failed("could not post Ctrl+Shift+M")
        }
        guard Keys.wait(0.45, pulse: pulse) else {
            return .interrupted
        }
        guard DeskFront.isForeground(preferred: preferred) else {
            return .failed("\(focus.displayName) is not focused")
        }

        for _ in 0..<index {
            guard Keys.key(Keys.down, pulse: pulse) else {
                return pulse()
                    ? .interrupted
                    : .failed("could not move to \(name)")
            }
            guard Keys.wait(0.05, pulse: pulse) else {
                return .interrupted
            }
            guard DeskFront.isForeground(preferred: preferred) else {
                return .failed("\(focus.displayName) is not focused")
            }
        }

        guard Keys.key(Keys.return, pulse: pulse) else {
            return pulse()
                ? .interrupted
                : .failed("could not confirm \(name)")
        }
        interruptedPickers.remove(focus.pid)
        guard Keys.wait(0.15, pulse: pulse) else {
            return .interrupted
        }
        return .applied(path: "Ctrl+Shift+M Down \(index) \(name)")
    }

    /// Select the Cursor model, close the picker, then reopen it for effort.
    static func cursorModelThenThinking(
        _ modelRaw: String,
        thinking thinkingRaw: String,
        preferredBundleID: String?,
        pulse: (() -> Bool)? = nil
    ) -> Result {
        guard let focus = FocusOperation(preferred: preferredBundleID) else {
            return .failed("ChatGPT or Cursor is not focused")
        }
        let pulse = wrappedPulse(focus: focus, upstream: pulse)
        guard let modelIndex = Catalog.cursorModelIndex(modelRaw),
              let modelName = Catalog.cursorModelName(modelRaw) else {
            return .failed("unknown model \(modelRaw)")
        }
        guard let levels = Catalog.cursorEfforts(for: modelRaw) else {
            return .failed("unknown model \(modelRaw)")
        }
        if pulse() {
            return .interrupted
        }
        if let stopped = cursorSelectModel(
            index: modelIndex, name: modelName, focus: focus, preferred: preferredBundleID, pulse: pulse
        ) {
            return stopped
        }
        if levels.isEmpty { return .applied(path: "model \(modelName); effort unsupported, skipped") }
        guard let (target, thinkingName) = Catalog.cursorEffort(thinkingRaw, model: modelRaw) else {
            return .failed("unknown thinking \(thinkingRaw)")
        }
        guard Keys.wait(0.4, pulse: pulse) else {
            return .interrupted
        }
        return cursorSelectEffort(
            target: target, name: thinkingName, focus: focus, preferred: preferredBundleID, pulse: pulse
        )
    }

    /// Command-/ focuses Search; first Down is Auto, then picker order.
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
        guard Keys.wait(0.1, pulse: pulse) else {
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
            Keys.down, times: index + 1, gap: 0.05, pulse: pulse,
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
        interruptedPickers.remove(focus.pid)
        guard Keys.wait(0.15, pulse: pulse) else {
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
        guard Keys.wait(0.08, pulse: pulse) else {
            return .interrupted
        }
        guard Keys.key(Keys.up, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not move to Cursor Thinking")
        }
        guard Keys.wait(0.08, pulse: pulse) else {
            return .interrupted
        }
        guard Keys.key(Keys.right, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not open Cursor Thinking menu")
        }
        guard Keys.wait(0.25, pulse: pulse) else {
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

    private static func openCursorPopover(
        focus: FocusOperation,
        preferred: String?,
        pulse: @escaping () -> Bool
    ) -> Result? {
        if let stopped = dismissInterruptedPicker(pid: focus.pid, pulse: pulse) {
            return stopped
        }
        interruptedPickers.insert(focus.pid)
        guard Keys.command(Keys.slash, pulse: pulse) else {
            return pulse() ? .interrupted : .failed("could not post Command-/")
        }
        guard Keys.wait(0.45, pulse: pulse) else {
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
            Keys.down, times: index, gap: 0.05, pulse: pulse,
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
        guard Keys.wait(0.15, pulse: pulse) else {
            return .interrupted
        }
        interruptedPickers.remove(focus.pid)
        guard Keys.wait(0.15, pulse: pulse) else {
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
        guard interruptedPickers.contains(pid) else { return nil }
        guard Keys.key(Keys.escape, pulse: pulse), Keys.wait(0.1, pulse: pulse) else {
            return .interrupted
        }
        interruptedPickers.remove(pid)
        return nil
    }

    private static func bump(delta: Int, pulse: (() -> Bool)?) -> Bool {
        guard delta != 0 else { return true }
        let code: UInt16 = delta > 0 ? Keys.period : Keys.comma
        for _ in 0..<abs(delta) {
            guard Keys.controlShift(code, pulse: pulse) else { return false }
            guard Keys.wait(0.15, pulse: pulse) else { return false }
            if pulse?() == true {
                return false
            }
        }
        return true
    }
}
#endif
