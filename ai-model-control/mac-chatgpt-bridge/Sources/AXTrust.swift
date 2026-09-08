#if os(macOS)
import ApplicationServices
import Foundation

enum AXTrust {
    static func isTrusted(prompt: Bool) -> Bool {
        let options: NSDictionary = [
            kAXTrustedCheckOptionPrompt.takeUnretainedValue(): prompt,
        ]
        return AXIsProcessTrustedWithOptions(options)
    }

    static func require(prompt: Bool) -> Bool {
        if isTrusted(prompt: prompt) {
            return true
        }
        let process = ProcessInfo.processInfo.processName
        fputs(
            """
            Accessibility is not granted for this process (\(process)).
            Key posting needs it on the app that launches chatgpt-bridge
            (Terminal, iTerm, Cursor, …):
              System Settings → Privacy & Security → Accessibility
            Foreground detection does not need this permission.

            """,
            stderr
        )
        return false
    }
}
#endif
