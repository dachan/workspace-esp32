#if os(macOS)
import AppKit
import Foundation

/// ChatGPT/Codex and Cursor share the desk dial; each app has its own
/// shortcuts. Keys fire only while one of these is already frontmost.
enum DeskKind {
    case chatGPT
    case cursor
}

enum DeskFront {
    static let chatGPTIDs: Set<String> = [
        "com.openai.chat",
        "com.openai.codex",
    ]
    static let cursorID = "com.todesktop.230313mzl4w4u92"
    static let bundleIDs: Set<String> = chatGPTIDs.union([cursorID])

    static func kind(of app: NSRunningApplication) -> DeskKind? {
        guard let id = app.bundleIdentifier else { return nil }
        if chatGPTIDs.contains(id) { return .chatGPT }
        if id == cursorID { return .cursor }
        return nil
    }

    static func displayName(for app: NSRunningApplication) -> String {
        switch app.bundleIdentifier {
        case "com.openai.chat": return "ChatGPT"
        case "com.openai.codex": return "Codex"
        case cursorID: return "Cursor"
        default: return app.localizedName ?? "?"
        }
    }

    static func isTarget(_ app: NSRunningApplication, preferred: String?) -> Bool {
        guard let id = app.bundleIdentifier, bundleIDs.contains(id) else { return false }
        return preferred == nil || preferred == id
    }

    static func frontmost() -> NSRunningApplication? {
        NSWorkspace.shared.frontmostApplication
    }

    static func isForeground(preferred: String? = nil) -> Bool {
        guard let front = frontmost() else { return false }
        return isTarget(front, preferred: preferred)
    }

    static func focusedApp(preferred: String? = nil) -> NSRunningApplication? {
        guard let app = frontmost(), isTarget(app, preferred: preferred) else { return nil }
        return app
    }

    static func panelTitle(preferred: String? = nil) -> String {
        guard let app = focusedApp(preferred: preferred) else {
            return "None"
        }
        return kind(of: app) == .cursor ? "Cursor" : "ChatGPT"
    }

    static func label(preferred: String? = nil) -> String {
        let app = frontmost()
        let name = app?.localizedName ?? "?"
        if let app, isTarget(app, preferred: preferred) {
            return "\(displayName(for: app)) foreground"
        }
        return "ChatGPT/Cursor background (\(name))"
    }
}

/// Latches any activation away from the original process, even if focus returns
/// before the next key delay finishes. Also guards one-shot commands.
final class FocusOperation {
    let pid: Int32
    let kind: DeskKind
    let displayName: String
    private var interrupted = false
    private var observer: NSObjectProtocol?

    init?(preferred: String?) {
        guard let app = DeskFront.focusedApp(preferred: preferred),
              let kind = DeskFront.kind(of: app)
        else {
            return nil
        }
        pid = app.processIdentifier
        self.kind = kind
        displayName = DeskFront.displayName(for: app)
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
