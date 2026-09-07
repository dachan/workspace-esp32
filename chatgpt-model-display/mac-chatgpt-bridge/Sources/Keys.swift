#if os(macOS)
import CoreGraphics
import Foundation

enum Keys {
    static let comma: UInt16 = 0x2B
    static let period: UInt16 = 0x2F
    static let m: UInt16 = 0x2E
    static let escape: UInt16 = 0x35
    static let `return`: UInt16 = 0x24
    static let up: UInt16 = 0x7E
    static let down: UInt16 = 0x7D

    static let hidInfo = """
    Keyboard path (Mac helper)
      Foreground: NSWorkspace.frontmostApplication (ChatGPT / Codex only).
      Model: Control-Shift-M (picker opens on Astra), Down to dial index, Return.
      Reasoning: absolute Light clamp then Control-Shift-. up to target.
      Bind those shortcuts in ChatGPT if they are Unassigned.
      The helper never activates ChatGPT; keys fire only while it is focused.
      Serial is drained during key delays so a newer SET supersedes in-flight apply.
    """

    /// Run-loop wait so serial drain / focus checks can run during key delays.
    /// `pulse` returning true aborts the wait early (caller should stop applying).
    @discardableResult
    static func wait(_ seconds: Double, pulse: (() -> Bool)? = nil) -> Bool {
        if seconds <= 0 {
            return !(pulse?() ?? false)
        }
        let end = Date().addingTimeInterval(seconds)
        while Date() < end {
            if pulse?() == true {
                return false
            }
            let slice = min(0.05, end.timeIntervalSinceNow)
            if slice <= 0 {
                break
            }
            RunLoop.current.run(until: Date().addingTimeInterval(slice))
        }
        return !(pulse?() ?? false)
    }

    @discardableResult
    static func key(_ code: UInt16, flags: CGEventFlags = [], pulse: (() -> Bool)? = nil) -> Bool {
        if pulse?() == true {
            return false
        }
        return post(code, flags: flags, down: true) && post(code, flags: flags, down: false)
    }

    static let controlShiftFlags: CGEventFlags = [.maskControl, .maskShift]

    @discardableResult
    static func chord(_ code: UInt16, _ flags: CGEventFlags, pulse: (() -> Bool)? = nil) -> Bool {
        key(code, flags: flags, pulse: pulse)
    }

    @discardableResult
    static func controlShift(_ code: UInt16, pulse: (() -> Bool)? = nil) -> Bool {
        chord(code, controlShiftFlags, pulse: pulse)
    }

    private static func post(_ code: UInt16, flags: CGEventFlags, down: Bool) -> Bool {
        guard let source = CGEventSource(stateID: .hidSystemState),
              let event = CGEvent(keyboardEventSource: source, virtualKey: code, keyDown: down)
        else {
            return false
        }
        event.flags = flags
        event.post(tap: .cghidEventTap)
        return true
    }
}
#endif
