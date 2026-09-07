#if os(macOS)
import Foundation

enum Switcher {
    struct Result {
        var ok: Bool
        var path: String
        var error: String?
    }

    /// Ctrl+Shift+M → Down to the ESP dial index → Return.
    /// ChatGPT's picker always opens with GPT-6 Astra highlighted; list order
    /// matches Catalog.models. No Escape first — while the composer is focused,
    /// Escape steals key focus and live dial applies miss (queued→focus still
    /// works because ChatGPT was just brought front).
    static func model(_ raw: String, preferredBundleID: String?) -> Result {
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT is not focused")
        }
        guard let index = Catalog.modelIndex(raw), let name = Catalog.modelName(raw) else {
            return Result(ok: false, path: "none", error: "unknown model \(raw)")
        }

        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT is not focused")
        }
        fputs(
            "chatgpt-bridge: open model picker via Ctrl+Shift+M, Down \(index) to \(name)\n",
            stderr
        )
        guard Keys.controlShift(Keys.m) else {
            return Result(ok: false, path: "shortcut", error: "could not post Ctrl+Shift+M")
        }
        Keys.wait(0.45)
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT is not focused")
        }

        for _ in 0..<index {
            guard Keys.key(Keys.down) else {
                return Result(ok: false, path: "picker", error: "could not move to \(name)")
            }
            Keys.wait(0.25)
        }

        guard Keys.key(Keys.return) else {
            return Result(ok: false, path: "picker", error: "could not confirm \(name)")
        }
        Keys.wait(0.15)
        return Result(ok: true, path: "Ctrl+Shift+M Down \(index) \(name)", error: nil)
    }

    /// Ctrl+Shift-, / Ctrl+Shift-. bumps. ChatGPT must already be focused.
    /// No Escape here — while the composer is focused, Escape steals key
    /// focus and the reasoning chords silently miss.
    static func thinking(_ raw: String, preferredBundleID: String?, from current: String?) -> Result {
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT is not focused")
        }
        guard let target = Catalog.thinkingIndex(raw), let name = Catalog.thinkingName(raw) else {
            return Result(ok: false, path: "none", error: "unknown thinking \(raw)")
        }

        let delta: Int
        if let current, let from = Catalog.thinkingIndex(current) {
            delta = target - from
        } else {
            delta = Int.min
        }
        if delta == 0 {
            return Result(ok: true, path: "noop", error: nil)
        }

        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT is not focused")
        }

        if delta == Int.min {
            // Clamp to Light (3× decrease covers Extra High → Light), then climb.
            fputs("chatgpt-bridge: reasoning absolute set via Ctrl+Shift+, then up to \(name)\n", stderr)
            guard bump(delta: -3), bump(delta: target) else {
                return Result(ok: false, path: "shortcut", error: "could not set reasoning to \(name)")
            }
            Keys.wait(0.1)
        } else {
            let label = delta > 0 ? "." : ","
            fputs("chatgpt-bridge: reasoning bump \(delta) via Ctrl+Shift+\(label)\n", stderr)
            guard bump(delta: delta) else {
                return Result(ok: false, path: "shortcut", error: "could not set reasoning to \(name)")
            }
        }
        return Result(ok: true, path: "Ctrl+Shift+,/. \(name)", error: nil)
    }

    private static func bump(delta: Int) -> Bool {
        guard delta != 0 else { return true }
        let code: UInt16 = delta > 0 ? Keys.period : Keys.comma
        for _ in 0..<abs(delta) {
            guard Keys.controlShift(code) else { return false }
            Keys.wait(0.15)
        }
        return true
    }
}
#endif
