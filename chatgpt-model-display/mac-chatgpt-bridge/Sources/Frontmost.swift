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

    static func isTarget(_ app: NSRunningApplication, preferred: String?) -> Bool {
        guard let id = app.bundleIdentifier, bundleIDs.contains(id) else { return false }
        return preferred == nil || preferred == id
    }

    static func frontmost() -> NSRunningApplication? {
        NSWorkspace.shared.frontmostApplication
    }

    static func isForeground(preferred: String? = nil) -> Bool {
        guard let front = frontmost() else {
            return false
        }
        return isTarget(front, preferred: preferred)
    }

    static func label(preferred: String? = nil) -> String {
        let app = frontmost()
        let name = app?.localizedName ?? "?"
        if let app, isTarget(app, preferred: preferred) {
            return "ChatGPT foreground"
        }
        return "ChatGPT background (\(name))"
    }
}
/// Latches any activation away from the original process, even if focus returns
/// before the next key delay finishes. Also guards one-shot commands.
final class FocusOperation {
    let pid: Int32
    private var interrupted = false
    private var observer: NSObjectProtocol?

    init?(preferred: String?) {
        guard let app = DeskFront.frontmost(), DeskFront.isTarget(app, preferred: preferred) else {
            return nil
        }
        pid = app.processIdentifier
        observer = NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didActivateApplicationNotification, object: nil, queue: .main
        ) { [weak self] notification in
            guard let self,
                  let app = notification.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication
            else { return }
            if app.processIdentifier != self.pid { self.interrupted = true }
        }
    }

    var isCurrent: Bool {
        if DeskFront.frontmost()?.processIdentifier != pid { interrupted = true }
        return !interrupted
    }

    deinit {
        if let observer { NSWorkspace.shared.notificationCenter.removeObserver(observer) }
    }
}
#endif
