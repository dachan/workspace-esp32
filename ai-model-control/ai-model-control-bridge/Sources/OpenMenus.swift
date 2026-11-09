#if os(macOS)
import Foundation

/// Escapes owed to an app whose picker an interrupted apply may have left
/// open: clean → owes(n) on opening, back to clean once the picker closes or
/// the debt is paid. Used for Cursor and OpenCode, whose open popover cannot
/// be observed reliably; ChatGPT and Claude check their visible menu instead.
enum OpenMenus {
    private static var owed: [Int32: Int] = [:]

    static func opening(_ pid: Int32, levels: Int = 1) {
        owed[pid] = levels
    }

    static func closed(_ pid: Int32) {
        owed.removeValue(forKey: pid)
    }

    /// Pays any owed Escapes before a new picker opens.
    static func dismiss(_ pid: Int32, pulse: @escaping () -> Bool) -> Switcher.Result? {
        while let remaining = owed[pid], remaining > 0 {
            guard Keys.key(Keys.escape, pulse: pulse) else { return .interrupted }
            owed[pid] = remaining - 1
            guard Keys.wait(Keys.keystrokeDelay, pulse: pulse) else { return .interrupted }
        }
        owed.removeValue(forKey: pid)
        return nil
    }
}
#endif
