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
    private var settleAt: TimeInterval = 0
    private var lastFailure: String?
    private var receivedModel: String?
    private var receivedThinking: String?
    private var lastApplied: [DeskKind: [SettingKind: String]] = [:]

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
        if update.kind == .model {
            receivedModel = value
            // Model selection can restore a different per-model effort in Cursor.
            if let thinking = receivedThinking { pending[.thinking] = thinking }
        } else {
            receivedThinking = value
        }
        pending[update.kind] = value
        // Batch paired knob turns: apply 1 s after the last received change.
        settleAt = ProcessInfo.processInfo.systemUptime + 1.0
        retryAt = 0
        lastFailure = nil
        let focused = DeskFront.isForeground(preferred: options.bundleID)
        print("\(stamp()) \(focused ? "rx" : "queued") \(update.kind.rawValue) \(value)")
        publishQueueStatus()
        fflush(stdout)
    }

    private func cancelQueue() {
        guard !pending.isEmpty else { return }
        pending.removeAll()
        retryAt = 0
        lastFailure = nil
        print("\(stamp()) cancelled queued dial state; panel restores last known")
        publishQueueStatus()
        fflush(stdout)
    }

    private func drainSerial() {
        guard let session else { return }
        for raw in session.readLines() {
            if raw == "CANCEL" {
                cancelQueue()
                continue
            }
            if let update = SerialBridge.parseInbound(raw) { queue(update) }
        }
        publishQueueStatus()
        session.flushWrites()
    }

    private func applyPending() {
        // Bound immediate retries while letting the latest model take priority.
        for _ in 0..<8 {
            let now = ProcessInfo.processInfo.systemUptime
            guard DeskFront.isForeground(preferred: options.bundleID),
                  now >= retryAt, now >= settleAt,
                  let kind = SettingKind.allCases.first(where: { pending[$0] != nil }),
                  let value = pending[kind] else { return }
            let pulse: () -> Bool = {
                self.drainSerial()
                return self.pending[kind] != value
                    || (kind == .thinking && self.pending[.model] != nil)
            }
            guard let app = DeskFront.focusedApp(preferred: options.bundleID),
                  let appKind = DeskFront.kind(of: app) else { return }
            if appKind == .cursor,
               let model = pending[.model],
               Catalog.cursorModelIndex(model) == nil {
                pending.removeValue(forKey: .model)
                print("\(stamp()) skip MODEL \(model) (not in Cursor picker)")
                fflush(stdout)
                publishQueueStatus()
                continue
            }
            // Re-apply only fields that changed since the last apply to this app.
            var droppedUnchanged = false
            for settled in SettingKind.allCases {
                if appKind == .cursor && settled == .thinking,
                   let model = pending[.model], lastApplied[appKind]?[.model] != model { continue }
                guard let queued = pending[settled],
                      lastApplied[appKind]?[settled] == queued else { continue }
                pending.removeValue(forKey: settled)
                print("\(stamp()) skip \(settled.rawValue) \(queued) (unchanged)")
                droppedUnchanged = true
            }
            if droppedUnchanged {
                fflush(stdout)
                publishQueueStatus()
                continue
            }
            let result: Switcher.Result
            if appKind == .cursor,
               let model = pending[.model],
               let thinking = pending[.thinking] {
                let bothPulse: () -> Bool = {
                    self.drainSerial()
                    return self.pending[.model] != model || self.pending[.thinking] != thinking
                }
                result = Switcher.cursorModelThenThinking(
                    model, thinking: thinking,
                    preferredBundleID: options.bundleID, pulse: bothPulse
                )
                switch result {
                case .applied(let path):
                    if pending[.model] == model { pending.removeValue(forKey: .model) }
                    if pending[.thinking] == thinking { pending.removeValue(forKey: .thinking) }
                    lastApplied[appKind, default: [:]][.model] = model
                    lastApplied[appKind, default: [:]][.thinking] = thinking
                    lastFailure = nil
                    retryAt = 0
                    print("\(stamp()) applied MODEL \(model) THINKING \(thinking) via \(path)")
                    publishQueueStatus()
                    fflush(stdout)
                    continue
                case .interrupted:
                    print("\(stamp()) interrupted Cursor model/effort; settings remain queued")
                    fflush(stdout)
                    continue
                case .failed(let message):
                    if message != lastFailure {
                        fputs("chatgpt-bridge: \(message); setting remains queued\n", stderr)
                    }
                    lastFailure = message
                    retryAt = ProcessInfo.processInfo.systemUptime + 2
                    return
                }
            }
            switch kind {
            case .model:
                result = Switcher.model(value, preferredBundleID: options.bundleID, pulse: pulse)
            case .thinking:
                result = Switcher.thinking(value, model: lastApplied[.cursor]?[.model] ?? receivedModel, preferredBundleID: options.bundleID, pulse: pulse)
            }
            switch result {
            case .applied(let path):
                if pending[kind] == value { pending.removeValue(forKey: kind) }
                lastApplied[appKind, default: [:]][kind] = value
                lastFailure = nil
                retryAt = 0
                print("\(stamp()) applied \(kind.rawValue) \(value) via \(path)")
                publishQueueStatus()
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
            print("\(stamp()) \(DeskFront.label(preferred: options.bundleID))")
            if front && !pending.isEmpty {
                retryAt = 0
                lastFailure = nil
                let target = app.map { DeskFront.displayName(for: $0) } ?? "ChatGPT/Cursor"
                print("\(stamp()) flushing queued dial state into \(target)")
            }
            fflush(stdout)
        }
    }

    private func publishQueueStatus() {
        let queued = !pending.isEmpty && !DeskFront.isForeground(preferred: options.bundleID)
        session?.setPanelQueued(queued)
        session?.setPanelFront(DeskFront.panelTitle(preferred: options.bundleID))
    }

    func run() -> Never {
        print("watching ChatGPT / Cursor foreground\(session.map { "; listening on \($0.port)" } ?? "") (Ctrl+C to stop)")
        fflush(stdout)
        while true {
            drainSerial()
            noteFront()
            applyPending()
            drainSerial()
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
