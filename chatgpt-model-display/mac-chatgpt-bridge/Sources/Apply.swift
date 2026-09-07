#if os(macOS)
import Foundation

enum Switcher {
    struct Result {
        var ok: Bool
        var path: String
        var error: String?
    }

    static func model(_ raw: String, preferredBundleID: String?) -> Result {
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT/Cursor is not focused")
        }
        guard let name = Catalog.modelName(raw) else {
            return Result(ok: false, path: "none", error: "unknown model \(raw)")
        }
        let query = Catalog.pickerQuery(for: name)

        Keys.key(Keys.escape)
        Keys.wait(0.08)
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT/Cursor is not focused")
        }
        fputs("chatgpt-bridge: open model picker via Ctrl+Shift+M, type \(query)\n", stderr)
        guard Keys.controlShift(Keys.m) else {
            return Result(ok: false, path: "shortcut", error: "could not post Ctrl+Shift+M")
        }
        Keys.wait(0.4)
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT/Cursor is not focused")
        }
        guard Keys.type(query) else {
            return Result(ok: false, path: "picker", error: "could not type \(query)")
        }
        Keys.wait(0.12)
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT/Cursor is not focused")
        }
        guard Keys.key(Keys.return) else {
            return Result(ok: false, path: "picker", error: "could not confirm \(name)")
        }
        Keys.wait(0.12)
        return Result(ok: true, path: "picker \(name)", error: nil)
    }

    static func thinking(_ raw: String, preferredBundleID: String?, from current: String?) -> Result {
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT/Cursor is not focused")
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

        Keys.key(Keys.escape)
        Keys.wait(0.05)
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT/Cursor is not focused")
        }

        if delta == Int.min {
            fputs("chatgpt-bridge: reasoning reset via Ctrl+Shift+, then up to \(name)\n", stderr)
            guard bump(delta: -3), bump(delta: target) else {
                return Result(ok: false, path: "shortcut", error: "could not set reasoning to \(name)")
            }
        } else {
            let label = delta > 0 ? "." : ","
            fputs("chatgpt-bridge: reasoning bump \(delta) via Ctrl+Shift+\(label)\n", stderr)
            guard bump(delta: delta) else {
                return Result(ok: false, path: "shortcut", error: "could not set reasoning to \(name)")
            }
        }
        return Result(ok: true, path: "reasoning shortcut \(name)", error: nil)
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
