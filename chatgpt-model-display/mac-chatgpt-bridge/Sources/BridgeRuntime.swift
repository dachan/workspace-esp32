#if os(macOS)
import AppKit
import Foundation

private let timestampFormatter = ISO8601DateFormatter()
private func stamp() -> String { timestampFormatter.string(from: Date()) }

func printFront(preferred: String?) {
    let app = DeskFront.frontmost()
    print(DeskFront.label(preferred: preferred))
    print("frontmost: \(app?.localizedName ?? "?") (\(app?.bundleIdentifier ?? "?"))")
}

final class BridgeRuntime {
    private let options: Options
    private let session: SerialSession?
    private var pending: [SettingKind: String] = [:]
    private var receivedRevisions: [SettingKind: UInt64] = [:]
    private var lastFront: Bool?
    private var lastFrontPID: Int32?
    private var retryAt: TimeInterval = 0
    private var lastFailure: String?

    init(options: Options) {
        self.options = options
        session = options.port.map { SerialSession(port: $0, baud: options.baud) }
    }

    private func queue(_ update: SerialUpdate) {
        guard let value = update.kind.canonicalName(update.value) else {
            fputs("chatgpt-bridge: ignore unknown \(update.kind.rawValue) \(update.value)\n", stderr)
            return
        }
        // ACK means received and queued, never confirmation of application UI state.
        // A repeated snapshot/ACK retry must not replay an already accepted setting.
        session?.acknowledge(update)
        if let revision = update.revision {
            if receivedRevisions[update.kind] == revision { return }
            receivedRevisions[update.kind] = revision
        }
        pending[update.kind] = value
        retryAt = 0
        lastFailure = nil
        let focused = DeskFront.isForeground(preferred: options.bundleID)
        print("\(stamp()) \(focused ? "rx" : "queued") \(update.kind.rawValue) \(value)")
        fflush(stdout)
    }

    private func drainSerial() {
        guard let session else { return }
        for raw in session.readLines() {
            if let update = SerialBridge.parseInbound(raw) { queue(update) }
        }
        session.flushWrites()
    }

    private func applyPending() {
        // Bound immediate retries while letting the latest model take priority.
        for _ in 0..<8 {
            guard DeskFront.isForeground(preferred: options.bundleID),
                  ProcessInfo.processInfo.systemUptime >= retryAt,
                  let kind = SettingKind.allCases.first(where: { pending[$0] != nil }),
                  let value = pending[kind] else { return }
            let pulse: () -> Bool = {
                self.drainSerial()
                return self.pending[kind] != value
                    || (kind == .thinking && self.pending[.model] != nil)
            }
            let result: Switcher.Result
            switch kind {
            case .model:
                result = Switcher.model(value, preferredBundleID: options.bundleID, pulse: pulse)
            case .thinking:
                result = Switcher.thinking(value, preferredBundleID: options.bundleID, pulse: pulse)
            }
            switch result {
            case .applied(let path):
                if pending[kind] == value { pending.removeValue(forKey: kind) }
                lastFailure = nil
                retryAt = 0
                print("\(stamp()) applied \(kind.rawValue) \(value) via \(path)")
            case .interrupted:
                print("\(stamp()) interrupted \(kind.rawValue) \(value); setting remains queued")
            case .failed(let message):
                if message != lastFailure {
                    fputs("chatgpt-bridge: \(message); setting remains queued\n", stderr)
                }
                lastFailure = message
                retryAt = ProcessInfo.processInfo.systemUptime + 2
                return
            }
            fflush(stdout)
        }
    }

    private func noteFront() {
        let app = DeskFront.frontmost()
        let front = app.map { DeskFront.isTarget($0, preferred: options.bundleID) } ?? false
        let pid = app?.processIdentifier
        if front != lastFront || (front && pid != lastFrontPID) {
            lastFront = front
            lastFrontPID = pid
            print("\(stamp()) \(front ? "ChatGPT foreground" : "ChatGPT background")")
            if front && !pending.isEmpty {
                retryAt = 0
                lastFailure = nil
                print("\(stamp()) flushing queued dial state into ChatGPT")
            }
            fflush(stdout)
        }
    }

    func run() -> Never {
        print("watching ChatGPT foreground\(session.map { "; listening on \($0.port)" } ?? "") (Ctrl+C to stop)")
        fflush(stdout)
        while true {
            drainSerial()
            noteFront()
            applyPending()
            RunLoop.current.run(until: Date().addingTimeInterval(0.05))
        }
    }
}

func runWatch(options: Options) -> Int32 {
    if options.listen && options.port == nil {
        fputs("chatgpt-bridge: --listen needs --port\n", stderr)
        return 2
    }
    if options.port != nil {
        guard AXTrust.require(prompt: true) else { return 2 }
    }
    BridgeRuntime(options: options).run()
}
#endif
