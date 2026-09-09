#if os(macOS)
import Foundation

/// OpenCode's encoder order is reversed from its native Luna-first picker.
enum OpenCodeApply {
    private static var openPickers: Set<Int32> = []

    static func model(_ raw: String, focus: FocusOperation, pulse: @escaping () -> Bool) -> Switcher.Result {
        guard let name = Catalog.openCodeModelName(raw),
              let pickerIndex = Catalog.openCodePickerIndex(raw) else {
            return .failed("unknown OpenCode model \(raw)")
        }
        if let result = prepare(focus, pulse: pulse) { return result }
        openPickers.insert(focus.pid)
        guard Keys.command(Keys.apostrophe, pulse: pulse), Keys.wait(0.45, pulse: pulse) else {
            return .interrupted
        }
        // Luna is already highlighted when the native menu opens. Down is
        // therefore zero-based: the first Down advances to Sol.
        if pickerIndex > 0 {
            for _ in 0..<pickerIndex {
                guard Keys.key(Keys.down, pulse: pulse), Keys.wait(0.05, pulse: pulse) else {
                    return .interrupted
                }
            }
        }
        guard Keys.key(Keys.return, pulse: pulse), Keys.wait(0.25, pulse: pulse) else {
            return .interrupted
        }
        openPickers.remove(focus.pid)
        return .applied(path: "OpenCode Command-' Down \(pickerIndex) Return \(name)")
    }

    private static func prepare(_ focus: FocusOperation, pulse: @escaping () -> Bool) -> Switcher.Result? {
        guard !pulse() else { return .interrupted }
        if openPickers.contains(focus.pid) {
            guard Keys.key(Keys.escape, pulse: pulse), Keys.wait(0.15, pulse: pulse) else {
                return .interrupted
            }
            openPickers.remove(focus.pid)
        }
        guard PromptFocus.ensure(pid: focus.pid, kind: .openCode) == .focused else {
            return .failed("OpenCode prompt unavailable")
        }
        return pulse() ? .interrupted : nil
    }

}
#endif
