#if os(macOS)
import CoreGraphics
import Foundation

/// A short, process-scoped filter. All ownership changes run on the apply thread.
final class InputGuard {
    static let eventTag: Int64 = 0x41494D4F44454C
    private static var current: InputGuard?
    private let focus: FocusOperation
    private var tap: CFMachPort?
    private var source: CFRunLoopSource?
    private var watchdog: DispatchWorkItem?
    private var cancelled = false
    private let deadline = ProcessInfo.processInfo.systemUptime + 5

    private init(focus: FocusOperation) { self.focus = focus }

    var isValid: Bool {
        !cancelled && focus.isCurrent && ProcessInfo.processInfo.systemUptime < deadline
            && tap.map { CGEvent.tapIsEnabled(tap: $0) } == true
    }

    static func protect(
        focus: FocusOperation, body: (InputGuard) -> Switcher.Result
    ) -> Switcher.Result {
        if let current {
            guard current.focus.pid == focus.pid, current.isValid else { return .interrupted }
            return body(current)
        }
        // Consult physical HID state only. The bridge's Control-Shift shortcuts may
        // leave modifier flags in the combined session table after their key-up event.
        // Private event sources avoid changing this hardware-only snapshot.
        // Starting with a physical press would hide its release from the target app.
        if let held = heldInput() { return .failed("input guard waiting for \(held) to release") }
        let guardInput = InputGuard(focus: focus)
        guard guardInput.start() else {
            guardInput.stop()
            return .failed("input guard unavailable; check Accessibility and Input Monitoring permissions")
        }
        current = guardInput
        defer {
            guardInput.stop()
            current = nil
        }
        return body(guardInput)
    }

    private static func heldInput() -> String? {
        let state = CGEventSourceStateID.hidSystemState
        if let key = (0..<128).first(where: {
            CGEventSource.keyState(state, key: CGKeyCode($0))
        }) {
            return "key code \(key)"
        }
        let buttonNames = ["left mouse button", "right mouse button", "other mouse button"]
        for (rawValue, name) in buttonNames.enumerated() {
            let button = CGMouseButton(rawValue: UInt32(rawValue))!
            if CGEventSource.buttonState(state, button: button) { return name }
        }
        return nil
    }

    private func start() -> Bool {
        let types: [CGEventType] = [
            .keyDown, .keyUp, .flagsChanged, .leftMouseDown, .leftMouseUp,
            .rightMouseDown, .rightMouseUp, .otherMouseDown, .otherMouseUp,
            .mouseMoved, .leftMouseDragged, .rightMouseDragged, .otherMouseDragged, .scrollWheel
        ]
        let mask = types.reduce(CGEventMask(0)) { $0 | (CGEventMask(1) << $1.rawValue) }
        guard let tap = CGEvent.tapCreateForPid(
            pid: focus.pid, place: .headInsertEventTap, options: .defaultTap,
            eventsOfInterest: mask,
            callback: { _, type, event, context in
                guard let context else { return Unmanaged.passUnretained(event) }
                let owner = Unmanaged<InputGuard>.fromOpaque(context).takeUnretainedValue()
                if type == .tapDisabledByTimeout || type == .tapDisabledByUserInput {
                    owner.cancelled = true
                    return Unmanaged.passUnretained(event)
                }
                if event.getIntegerValueField(.eventSourceUserData) == InputGuard.eventTag {
                    return Unmanaged.passUnretained(event)
                }
                guard owner.isValid else { return Unmanaged.passUnretained(event) }
                if type == .keyDown && event.getIntegerValueField(.keyboardEventKeycode) == Int64(Keys.escape) {
                    owner.cancelled = true
                }
                return nil
            }, userInfo: Unmanaged.passUnretained(self).toOpaque()
        ) else { return false }
        self.tap = tap
        guard let source = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, tap, 0) else {
            stop()
            return false
        }
        self.source = source
        CFRunLoopAddSource(CFRunLoopGetCurrent(), source, .commonModes)
        CGEvent.tapEnable(tap: tap, enable: true)
        // Independent of the apply run loop: a stalled AX call cannot leave input locked.
        let watchdog = DispatchWorkItem { CGEvent.tapEnable(tap: tap, enable: false) }
        self.watchdog = watchdog
        DispatchQueue.global().asyncAfter(deadline: .now() + 5, execute: watchdog)
        return isValid
    }

    private func stop() {
        watchdog?.cancel()
        if let tap {
            CGEvent.tapEnable(tap: tap, enable: false)
            CFMachPortInvalidate(tap)
        }
        if let source { CFRunLoopRemoveSource(CFRunLoopGetCurrent(), source, .commonModes) }
        source = nil
        tap = nil
    }
}
#endif
