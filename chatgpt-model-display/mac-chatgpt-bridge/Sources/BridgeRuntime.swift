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
    // The complete latest target survives partial application; no detent queue.
    private var desired: [SettingKind: String] = [:]
    private var generation: UInt64 = 0
    private var targetFocus: FocusOperation?
    private var receivedRevisions: [SettingKind: UInt64] = [:]
    private var lastFront: Bool?
    private var lastFrontPID: Int32?
    private var retryAt: TimeInterval = 0
    private var settleAt: TimeInterval = 0
    private var lastFailure: String?
    private var receivedModel: String?
    private var receivedThinking: String?
    private var lastApplied: [Int32: [SettingKind: String]] = [:]
    private var forceApply = false

    init(options: Options) {
        self.options = options
        session = options.port.map { SerialSession(port: $0, baud: options.baud) }
    }

    private func receive(_ update: SerialUpdate) {
        guard let value = update.kind.canonicalName(update.value) else {
            fputs("chatgpt-bridge: ignore unknown \(update.kind.rawValue) \(update.value)\n", stderr)
            return
        }
        // ACK means received, never confirmation of application UI state.
        // A repeated snapshot/ACK retry must not replay an already accepted setting.
        session?.acknowledge(update)
        if let revision = update.revision {
            if receivedRevisions[update.kind] == revision { return }
            receivedRevisions[update.kind] = revision
        }
        if update.kind == .model {
            receivedModel = value
        } else {
            receivedThinking = value
        }
        // Changes are never deferred: without focus the dial state lives on the panel only.
        guard DeskFront.isForeground(preferred: options.bundleID) else {
            forceApply = false
            discardPending("ChatGPT/Cursor is not focused")
            print("\(stamp()) ignored \(update.kind.rawValue) \(value) (ChatGPT/Cursor is not focused)")
            fflush(stdout)
            return
        }
        acceptTarget()
        // Batch paired knob turns: apply 1 s after the last received change.
        // PUSH (sync button) skips the 1 s batch window.
        settleAt = ProcessInfo.processInfo.systemUptime + (forceApply ? 0.4 : 1.0)
        retryAt = 0
        lastFailure = nil
        print("\(stamp()) rx \(update.kind.rawValue) \(value)")
        fflush(stdout)
    }

    private func acceptTarget() {
        if targetFocus?.isCurrent != true {
            discardPending("target app changed")
            targetFocus = FocusOperation(preferred: options.bundleID)
        }
        desired = [:]
        if let model = receivedModel { desired[.model] = model }
        if let thinking = receivedThinking { desired[.thinking] = thinking }
        generation &+= 1
    }

    private func discardPending(_ reason: String) {
        if !desired.isEmpty {
            print("\(stamp()) dropped target (\(reason))")
            fflush(stdout)
        }
        desired.removeAll()
        targetFocus = nil
        generation &+= 1
        retryAt = 0
        lastFailure = nil
        forceApply = false
    }

    private func drainSerial() {
        guard let session else { return }
        for raw in session.readLines() {
            if raw == "PUSH" {
                guard DeskFront.isForeground(preferred: options.bundleID) else {
                    discardPending("SYNC received without focus")
                    continue
                }
                acceptTarget()
                forceApply = true
                settleAt = ProcessInfo.processInfo.systemUptime + 0.4
                retryAt = 0
                lastFailure = nil
                print("\(stamp()) rx PUSH")
                fflush(stdout)
                continue
            }
            if let update = SerialBridge.parseInbound(raw) { receive(update) }
        }
        session.setPanelFront(DeskFront.panelTitle(preferred: options.bundleID))
        session.flushWrites()
    }

    private func applyPending() {
        guard !desired.isEmpty, let focus = targetFocus else { return }
        guard focus.isCurrent else {
            discardPending("target app left the foreground")
            return
        }
        let now = ProcessInfo.processInfo.systemUptime
        guard now >= retryAt, now >= settleAt else { return }
        let snapshot = desired
        let revision = generation
        let pid = focus.pid
        if forceApply {
            lastApplied.removeValue(forKey: pid)
            forceApply = false
        }
        let pulse: () -> Bool = {
            self.drainSerial()
            return !focus.isCurrent || self.generation != revision
        }

        // Finish one field at a time, always model before its dependent effort.
        // Invalidate before posting: even a failed/interrupted sequence may mutate UI.
        applyFields: for kind in SettingKind.allCases {
            guard let value = snapshot[kind] else { continue }
            if focus.kind == .cursor, kind == .model,
               Catalog.cursorModelIndex(value) == nil { continue }
            if lastApplied[pid]?[kind] == value { continue }
            if pulse() { break }
            lastApplied[pid, default: [:]].removeValue(forKey: kind)
            if kind == .model {
                lastApplied[pid, default: [:]].removeValue(forKey: .thinking)
            }
            let result: Switcher.Result
            switch kind {
            case .model:
                result = Switcher.model(value, preferredBundleID: options.bundleID, pulse: pulse)
            case .thinking:
                result = Switcher.thinking(
                    value, model: lastApplied[pid]?[.model],
                    preferredBundleID: options.bundleID, pulse: pulse
                )
            }
            // A completed key sequence is usable only for the current target.
            guard !pulse() else { break }
            switch result {
            case .applied(let path):
                lastApplied[pid, default: [:]][kind] = value
                lastFailure = nil
                retryAt = 0
                print("\(stamp()) posted \(kind.rawValue) \(value) via \(path)")
                if kind == .model, focus.kind == .cursor,
                   !Keys.wait(0.4, pulse: pulse) { break applyFields }
            case .interrupted:
                return
            case .failed(let message):
                if message != lastFailure {
                    fputs("chatgpt-bridge: \(message); retrying while focused\n", stderr)
                }
                lastFailure = message
                retryAt = ProcessInfo.processInfo.systemUptime + 2
                return
            }
        }
        if !focus.isCurrent, targetFocus === focus {
            discardPending("target app left the foreground")
        }
        fflush(stdout)
    }

    private func noteFront() {
        let app = DeskFront.frontmost()
        let front = app.map { DeskFront.isTarget($0, preferred: options.bundleID) } ?? false
        let pid = app?.processIdentifier
        if front != lastFront || (front && pid != lastFrontPID) {
            lastFront = front
            lastFrontPID = pid
            print("\(stamp()) \(DeskFront.label(preferred: options.bundleID))")
            fflush(stdout)
        }
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
