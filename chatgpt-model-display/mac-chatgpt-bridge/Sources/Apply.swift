#if os(macOS)
import Foundation

enum Switcher {
    private static var interruptedPickers: Set<Int32> = []

    enum Result {
        case applied(path: String)
        case interrupted
        case failed(String)
    }

    /// Ctrl+Shift+M opens the picker on Astra; Down N to the dial index; Return.
    /// `pulse` may drain serial / check focus; return true to abort as superseded.
    static func model(
        _ raw: String,
        preferredBundleID: String?,
        pulse: (() -> Bool)? = nil
    ) -> Result {
        guard let focus = FocusOperation(preferred: preferredBundleID) else {
            return .failed("ChatGPT is not focused")
        }
        let upstream = pulse
        let pulse: () -> Bool = {
            let superseded = upstream?() ?? false
            return !focus.isCurrent || superseded
        }
        guard let index = Catalog.modelIndex(raw), let name = Catalog.modelName(raw) else {
            return .failed("unknown model \(raw)")
        }
        if pulse() {
            return .interrupted
        }

        fputs(
            "chatgpt-bridge: open model picker via Ctrl+Shift+M, Down \(index) to \(name)\n",
            stderr
        )
        // Only dismiss a picker this helper may have left open in this process.
        if interruptedPickers.contains(focus.pid) {
            guard Keys.key(Keys.escape, pulse: pulse), Keys.wait(0.1, pulse: pulse) else {
                return .interrupted
            }
            interruptedPickers.remove(focus.pid)
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
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return .failed("ChatGPT is not focused")
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
            guard DeskFront.isForeground(preferred: preferredBundleID) else {
                return .failed("ChatGPT is not focused")
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

    /// Always absolute: clamp to Light, then climb. Avoids relative desync when
    /// ChatGPT's effort was changed outside the bridge (no AX readback).
    static func thinking(
        _ raw: String,
        preferredBundleID: String?,
        pulse: (() -> Bool)? = nil
    ) -> Result {
        guard let focus = FocusOperation(preferred: preferredBundleID) else {
            return .failed("ChatGPT is not focused")
        }
        let upstream = pulse
        let pulse: () -> Bool = {
            let superseded = upstream?() ?? false
            return !focus.isCurrent || superseded
        }
        guard let target = Catalog.thinkingIndex(raw), let name = Catalog.thinkingName(raw) else {
            return .failed("unknown thinking \(raw)")
        }
        if pulse() {
            return .interrupted
        }

        fputs(
            "chatgpt-bridge: reasoning absolute set via Ctrl+Shift+, then up to \(name)\n",
            stderr
        )
        // Clamp from the highest supported level before climbing.
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
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return .failed("ChatGPT is not focused")
        }
        return .applied(path: "Ctrl+Shift+,/. absolute \(name)")
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
