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
    // The complete latest ESP32 target survives partial application and focus loss;
    // there is no detent queue.
    private var desired: [SettingKind: String] = [:]
    private var generation: UInt64 = 0
    private var targetFocus: FocusOperation?
    private var receivedRevisions: [SettingKind: UInt64] = [:]
    private var lastFront: Bool?
    private var lastFrontPID: Int32?
    private var retryAt: TimeInterval = 0
    private var settleAt: TimeInterval = 0
    private var lastFailure: String?
    private var applyFailures = 0
    private var receivedModel: String?
    private var receivedThinking: String?
    private var lastApplied: [Int32: [SettingKind: String]] = [:]
    private var forceApply = false
    private var bootID: UInt64?
    private var connectionGeneration: UInt64 = 0

    init(options: Options) {
        self.options = options
        Catalog.setChatGPTThinkingMask(options.chatGPTThinkingMask)
        Catalog.setCursorEnabledMask(options.cursorModelMask)
        session = options.port.map {
            SerialSession(
                port: $0,
                baud: options.baud,
                chatGPTThinkingMask: options.chatGPTThinkingMask,
                cursorModelMask: options.cursorModelMask
            )
        }
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
        replaceDesiredWithPanelState()
        guard DeskFront.isForeground(preferred: options.bundleID) else {
            suspendPending("no supported app is focused")
            print("\(stamp()) deferred \(update.kind.rawValue) \(value) (no supported app is focused)")
            fflush(stdout)
            return
        }
        beginApplyingToFocusedApp()
        print("\(stamp()) rx \(update.kind.rawValue) \(value)")
        fflush(stdout)
    }

    private func replaceDesiredWithPanelState() {
        desired = [:]
        if let model = receivedModel { desired[.model] = model }
        if let thinking = receivedThinking { desired[.thinking] = thinking }
        generation &+= 1
    }

    /// The dial is authoritative. Keep its most recent complete state while
    /// unsupported apps are frontmost, then reconcile it only once a supported
    /// app is confirmed foreground.
    private func beginApplyingToFocusedApp(force: Bool = false, settle: Bool = true) {
        guard !desired.isEmpty, DeskFront.isForeground(preferred: options.bundleID) else { return }
        let continuingTarget = targetFocus?.isCurrent == true
        if !continuingTarget {
            targetFocus = FocusOperation(preferred: options.bundleID)
        }
        guard targetFocus != nil else { return }
        applyFailures = 0
        forceApply = forceApply || force || !continuingTarget
        // Batch paired dial updates. Focus-triggered reconciliation starts
        // immediately so it does not collide with the user's first keystrokes.
        settleAt = settle ? ProcessInfo.processInfo.systemUptime + Keys.bridgeSettle : 0
        retryAt = 0
        lastFailure = nil
    }

    /// Interrupting an apply must never forget the dial's desired state. A
    /// later supported-app focus creates a fresh FocusOperation and retries it.
    private func suspendPending(_ reason: String) {
        if targetFocus != nil && !desired.isEmpty {
            print("\(stamp()) deferred target (\(reason))")
            fflush(stdout)
        }
        targetFocus = nil
        generation &+= 1
        retryAt = 0
        lastFailure = nil
        forceApply = true
    }

    private func suspendAfterFocusLoss(_ reason: String) {
        suspendPending(reason)
        // Focus may leave and return during one apply; make that return visible
        // to noteFront() so it starts a fresh, foreground-bound operation.
        lastFront = false
        lastFrontPID = nil
    }

    private func discardPending(_ reason: String) {
        if !desired.isEmpty {
            print("\(stamp()) dropped target (\(reason))")
            fflush(stdout)
        }
        applyFailures = 0
        desired.removeAll()
        targetFocus = nil
        generation &+= 1
        retryAt = 0
        lastFailure = nil
        forceApply = false
    }

    private func drainSerial() {
        guard let session else { return }
        let lines = session.readLines()
        resetConnectionIfNeeded(session)
        for raw in lines {
            if raw.hasPrefix("READY ") {
                let hex = raw.dropFirst("READY ".count)
                guard hex.count == 16, let id = UInt64(hex, radix: 16) else { continue }
                if bootID != id {
                    bootID = id
                    discardPending("panel restarted")
                    receivedRevisions.removeAll()
                    receivedModel = nil
                    receivedThinking = nil
                    lastApplied.removeAll()
                    print("\(stamp()) rx READY \(hex)")
                    fflush(stdout)
                }
                // Respond to duplicates too: an earlier SYNC may have been lost.
                session.requestSync()
                continue
            }
            if raw == "PUSH" {
                guard DeskFront.isForeground(preferred: options.bundleID) else {
                    forceApply = true
                    suspendPending("SYNC received without focus")
                    print("\(stamp()) deferred PUSH (no supported app is focused)")
                    fflush(stdout)
                    continue
                }
                forceApply = true
                beginApplyingToFocusedApp()
                print("\(stamp()) rx PUSH")
                fflush(stdout)
                continue
            }
            if raw.hasPrefix("ENABLED ") {
                let hex = raw.dropFirst("ENABLED ".count).trimmingCharacters(in: .whitespaces)
                if let mask = UInt64(hex, radix: 16) {
                    print("\(stamp()) rx ENABLED \(String(format: "%016llx", mask)) (configured mask retained)")
                    fflush(stdout)
                }
                continue
            }
            if let update = SerialBridge.parseInbound(raw) { receive(update) }
        }
        session.setPanelFront(DeskFront.panelTitle(preferred: options.bundleID))
        session.flushWrites()
        resetConnectionIfNeeded(session)
    }

    private func resetConnectionIfNeeded(_ session: SerialSession) {
        guard connectionGeneration != session.connectionGeneration else { return }
        connectionGeneration = session.connectionGeneration
        discardPending("serial connection changed")
        receivedRevisions.removeAll()
        receivedModel = nil
        receivedThinking = nil
        bootID = nil
        lastApplied.removeAll()
    }

    private func applyPending() {
        guard !desired.isEmpty, let focus = targetFocus else { return }
        guard AXTrust.isTrusted(prompt: false) else {
            return
        }
        guard focus.isCurrent else {
            suspendAfterFocusLoss("target app left the foreground")
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

        let needsApply = snapshot.contains { kind, value in
            if focus.kind == .cursor, kind == .model, Catalog.cursorPickerIndex(value) == nil { return false }
            return lastApplied[pid]?[kind] != value
        }
        guard needsApply else { return }

        var guardInterrupted = false
        let guardedResult = InputGuard.protect(focus: focus) { inputGuard in
            defer { guardInterrupted = !inputGuard.isValid }
            let serialPulse = pulse
            let pulse = { !inputGuard.isValid || serialPulse() }
            // Finish one field at a time, always model before its dependent effort.
            // Invalidate before posting: even a failed/interrupted sequence may mutate UI.
            applyFields: for kind in SettingKind.allCases {
                guard let value = snapshot[kind] else { continue }
                if focus.kind == .cursor, kind == .model,
                   Catalog.cursorPickerIndex(value) == nil { continue }
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
                       !Keys.wait(Keys.pickerTiming, pulse: pulse) { break applyFields }
                case .interrupted:
                    return .interrupted
                case .failed(let message):
                    return .failed(message)
                }
            }
            return inputGuard.isValid ? .applied(path: "guarded transaction") : .interrupted
        }
        if case .failed(let message) = guardedResult, generation == revision {
            applyFailures += 1
            if applyFailures >= 3 {
                fputs("chatgpt-bridge: \(message); stopped until the next supported-app focus or dial update\n", stderr)
                suspendPending("\(focus.displayName) apply failed")
            } else {
                if message != lastFailure {
                    fputs("chatgpt-bridge: \(message); retrying while focused\n", stderr)
                }
                lastFailure = message
                retryAt = ProcessInfo.processInfo.systemUptime + 2
            }
        }
        if guardInterrupted, targetFocus === focus {
            suspendPending("input guard or apply interrupted")
        }
        if !focus.isCurrent, targetFocus === focus {
            suspendAfterFocusLoss("target app left the foreground")
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
            if front {
                // Reapply even if this process was previously cached: the user
                // may have changed the app directly while it was unfocused.
                beginApplyingToFocusedApp(force: true, settle: false)
            } else {
                suspendPending("no supported app is focused")
            }
        }
    }

    func run() -> Never {
        print("watching ChatGPT / Cursor / OpenCode foreground\(session.map { "; listening on \($0.port)" } ?? "") (Ctrl+C to stop)")
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
    // Watching the frontmost app and forwarding FRONT state only use NSWorkspace.
    // Keyboard control is gated at the point keys could be posted.
    BridgeRuntime(options: options).run()
}
#endif
