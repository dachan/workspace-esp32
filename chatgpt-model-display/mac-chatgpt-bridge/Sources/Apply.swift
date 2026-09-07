#if os(macOS)
import Foundation

enum Switcher {
    struct Result {
        var ok: Bool
        var path: String
        var error: String?

        static let superseded = Result(ok: false, path: "superseded", error: "superseded")
    }

    /// Ctrl+Shift+M opens the picker on Astra; Down N to the dial index; Return.
    /// `pulse` may drain serial / check focus; return true to abort as superseded.
    static func model(
        _ raw: String,
        preferredBundleID: String?,
        pulse: (() -> Bool)? = nil
    ) -> Result {
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT is not focused")
        }
        guard let index = Catalog.modelIndex(raw), let name = Catalog.modelName(raw) else {
            return Result(ok: false, path: "none", error: "unknown model \(raw)")
        }
        if pulse?() == true {
            return .superseded
        }

        fputs(
            "chatgpt-bridge: open model picker via Ctrl+Shift+M, Down \(index) to \(name)\n",
            stderr
        )
        guard Keys.controlShift(Keys.m, pulse: pulse) else {
            return pulse?() == true
                ? .superseded
                : Result(ok: false, path: "shortcut", error: "could not post Ctrl+Shift+M")
        }
        guard Keys.wait(0.45, pulse: pulse) else {
            return .superseded
        }
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT is not focused")
        }

        for _ in 0..<index {
            guard Keys.key(Keys.down, pulse: pulse) else {
                return pulse?() == true
                    ? .superseded
                    : Result(ok: false, path: "picker", error: "could not move to \(name)")
            }
            guard Keys.wait(0.25, pulse: pulse) else {
                return .superseded
            }
            guard DeskFront.isForeground(preferred: preferredBundleID) else {
                return Result(ok: false, path: "deferred", error: "ChatGPT is not focused")
            }
        }

        guard Keys.key(Keys.return, pulse: pulse) else {
            return pulse?() == true
                ? .superseded
                : Result(ok: false, path: "picker", error: "could not confirm \(name)")
        }
        guard Keys.wait(0.15, pulse: pulse) else {
            return .superseded
        }
        return Result(ok: true, path: "Ctrl+Shift+M Down \(index) \(name)", error: nil)
    }

    /// Always absolute: clamp to Light, then climb. Avoids relative desync when
    /// ChatGPT's effort was changed outside the bridge (no AX readback).
    static func thinking(
        _ raw: String,
        preferredBundleID: String?,
        pulse: (() -> Bool)? = nil
    ) -> Result {
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT is not focused")
        }
        guard let target = Catalog.thinkingIndex(raw), let name = Catalog.thinkingName(raw) else {
            return Result(ok: false, path: "none", error: "unknown thinking \(raw)")
        }
        if pulse?() == true {
            return .superseded
        }

        fputs(
            "chatgpt-bridge: reasoning absolute set via Ctrl+Shift+, then up to \(name)\n",
            stderr
        )
        // 3× decrease covers Extra High → Light.
        guard bump(delta: -3, pulse: pulse) else {
            return pulse?() == true
                ? .superseded
                : Result(ok: false, path: "shortcut", error: "could not clamp reasoning to Light")
        }
        guard bump(delta: target, pulse: pulse) else {
            return pulse?() == true
                ? .superseded
                : Result(ok: false, path: "shortcut", error: "could not set reasoning to \(name)")
        }
        guard Keys.wait(0.1, pulse: pulse) else {
            return .superseded
        }
        guard DeskFront.isForeground(preferred: preferredBundleID) else {
            return Result(ok: false, path: "deferred", error: "ChatGPT is not focused")
        }
        return Result(ok: true, path: "Ctrl+Shift+,/. absolute \(name)", error: nil)
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
