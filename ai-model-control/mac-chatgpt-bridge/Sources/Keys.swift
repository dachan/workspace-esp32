#if os(macOS)
import CoreGraphics
import Foundation

enum Keys {
    /// Delay between repeated navigation keys.
    static let navigationDelay = 0.025

    static let apostrophe: UInt16 = 0x27
    static let comma: UInt16 = 0x2B
    static let period: UInt16 = 0x2F
    static let m: UInt16 = 0x2E
    static let l: UInt16 = 0x25
    static let slash: UInt16 = 0x2C
    static let escape: UInt16 = 0x35
    static let `return`: UInt16 = 0x24
    static let down: UInt16 = 0x7D
    static let up: UInt16 = 0x7E
    static let right: UInt16 = 0x7C
    static let left: UInt16 = 0x7B
    static let home: UInt16 = 0x73

    static let hidInfo = """
    Keyboard path (Mac helper)
      Foreground: NSWorkspace.frontmostApplication (ChatGPT, Codex, Cursor, or OpenCode).
      The helper never activates those apps; keys fire only while one is focused.
      Before shortcuts it focuses the prompt: Cursor Command-L only if the
      Agents panel is not already open (Cmd+L toggles it closed otherwise),
      otherwise AX-focus aislash-editor-input; ChatGPT via the message box.
      ChatGPT / Codex
        Model: Control-Shift-M (picker opens on Astra), Down to dial index, Return.
        Reasoning: absolute Light clamp then Control-Shift-. up to target.
        Bind those shortcuts in ChatGPT if they are Unassigned.
      Cursor
        Command-/ opens the model list on Search. First Down is Auto,
        then catalog order (Settings toggle order). Return selects.
        Models that are toggled off in Cursor are skipped by the picker.
        Effort: Command-/, Left, Up, Right directly into Reasoning,
        then Down to the level and Return once, then Escape twice to close the menus. Chat/agent input must be focused.
        Fast/Slow is not set from the dial.
        MODELS (Cursor only) opens the on-device enable list; Auto stays on.
        Tap SYNC on the panel to reapply the current model and thinking.
      OpenCode
        Command-apostrophe opens models; exact accessible names select choices.
        The variant menu selects absolute effort (Light maps to Low, Extra High to Xhigh).
      Serial is drained during key delays so a newer SET supersedes in-flight apply.
    """

    /// Run-loop wait so serial drain / focus checks can run during key delays.
    /// `pulse` returning true aborts the wait early (caller should stop applying).
    @discardableResult
    static func wait(_ seconds: Double, pulse: (() -> Bool)? = nil) -> Bool {
        if seconds <= 0 {
            return !(pulse?() ?? false)
        }
        let end = ProcessInfo.processInfo.systemUptime + seconds
        while ProcessInfo.processInfo.systemUptime < end {
            if pulse?() == true {
                return false
            }
            let slice = min(0.05, end - ProcessInfo.processInfo.systemUptime)
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
    static let commandFlags: CGEventFlags = [.maskCommand]
    static let commandShiftFlags: CGEventFlags = [.maskCommand, .maskShift]

    @discardableResult
    static func chord(_ code: UInt16, _ flags: CGEventFlags, pulse: (() -> Bool)? = nil) -> Bool {
        key(code, flags: flags, pulse: pulse)
    }

    @discardableResult
    static func controlShift(_ code: UInt16, pulse: (() -> Bool)? = nil) -> Bool {
        chord(code, controlShiftFlags, pulse: pulse)
    }

    @discardableResult
    static func command(_ code: UInt16, pulse: (() -> Bool)? = nil) -> Bool {
        chord(code, commandFlags, pulse: pulse)
    }

    @discardableResult
    static func commandShift(_ code: UInt16, pulse: (() -> Bool)? = nil) -> Bool {
        chord(code, commandShiftFlags, pulse: pulse)
    }

    private static func post(_ code: UInt16, flags: CGEventFlags, down: Bool) -> Bool {
        guard let source = CGEventSource(stateID: .hidSystemState),
              let event = CGEvent(keyboardEventSource: source, virtualKey: code, keyDown: down)
        else {
            return false
        }
        event.flags = flags
        event.setIntegerValueField(.eventSourceUserData, value: InputGuard.eventTag)
        event.post(tap: .cghidEventTap)
        return true
    }
}
#endif
