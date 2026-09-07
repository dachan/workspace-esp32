#if os(macOS)
import AppKit
import Foundation

/// One `frontmostApplication` read. Keys fire only for ChatGPT / Codex —
/// never Cursor. Cursor is where the bridge often runs; treating it as a
/// target burned the queue by posting ChatGPT shortcuts into the wrong app.
enum DeskFront {
    static let bundleIDs: Set<String> = [
        "com.openai.chat",
        "com.openai.codex",
    ]

    static func allowedIDs(preferred: String?) -> Set<String> {
        if let preferred, !preferred.isEmpty {
            return [preferred]
        }
        return bundleIDs
    }

    static func isTarget(_ app: NSRunningApplication, preferred: String?) -> Bool {
        if let id = app.bundleIdentifier, allowedIDs(preferred: preferred).contains(id) {
            return true
        }
        guard preferred == nil || preferred?.isEmpty == true else {
            return false
        }
        let name = (app.localizedName ?? "").lowercased()
        return name == "chatgpt" || name.hasPrefix("chatgpt ")
    }

    static func frontmost(preferred: String? = nil) -> NSRunningApplication? {
        NSWorkspace.shared.frontmostApplication
    }

    static func isForeground(preferred: String? = nil) -> Bool {
        guard let front = frontmost(preferred: preferred) else {
            return false
        }
        return isTarget(front, preferred: preferred)
    }

    static func label(preferred: String? = nil) -> String {
        let app = frontmost(preferred: preferred)
        let name = app?.localizedName ?? "?"
        if let app, isTarget(app, preferred: preferred) {
            return "ChatGPT foreground"
        }
        return "ChatGPT background (\(name))"
    }
}
#endif
