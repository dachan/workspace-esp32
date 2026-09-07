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
      Foreground: NSWorkspace.frontmostApplication (ChatGPT or Cursor).
      Model: Control-Shift-M opens the picker; type the model token, Return.
      Reasoning: Control-Shift-, decreases; Control-Shift-. increases.
      Bind those shortcuts in ChatGPT/Cursor if they are Unassigned.
      The helper never activates an app; keys fire only while it is focused.
    """

    static func wait(_ seconds: Double) {
        Thread.sleep(forTimeInterval: seconds)
    }

    @discardableResult
    static func key(_ code: UInt16, flags: CGEventFlags = []) -> Bool {
        post(code, flags: flags, down: true) && post(code, flags: flags, down: false)
    }

    static let controlShiftFlags: CGEventFlags = [.maskControl, .maskShift]

    @discardableResult
    static func chord(_ code: UInt16, _ flags: CGEventFlags) -> Bool {
        key(code, flags: flags)
    }

    @discardableResult
    static func controlShift(_ code: UInt16) -> Bool {
        chord(code, controlShiftFlags)
    }

    @discardableResult
    static func type(_ text: String) -> Bool {
        for scalar in text.unicodeScalars {
            var utf16 = Array(String(scalar).utf16)
            guard let source = CGEventSource(stateID: .hidSystemState),
                  let down = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: true),
                  let up = CGEvent(keyboardEventSource: source, virtualKey: 0, keyDown: false)
            else {
                return false
            }
            down.keyboardSetUnicodeString(stringLength: utf16.count, unicodeString: &utf16)
            up.keyboardSetUnicodeString(stringLength: utf16.count, unicodeString: &utf16)
            down.post(tap: .cghidEventTap)
            up.post(tap: .cghidEventTap)
            wait(0.03)
        }
        return true
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
