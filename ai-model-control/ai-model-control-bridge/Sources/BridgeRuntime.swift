#if os(macOS)
import AppKit
import Foundation

private let timestampFormatter = ISO8601DateFormatter()
private func stamp() -> String { timestampFormatter.string(from: Date()) }
private var uptime: TimeInterval { ProcessInfo.processInfo.systemUptime }

func printFront(preferred: String?) {
    let app = DeskFront.frontmost()
    print(DeskFront.label(preferred: preferred))
    print("frontmost: \(app?.localizedName ?? "?") (\(app?.bundleIdentifier ?? "?"))")
}

/// Keyboard-transaction lifecycle. Only an explicit APPLY/PUSH arms it, and
/// every outcome (applied, superseded, interrupted, focus loss, final failure)
/// returns to idle, so receiving STATE alone can never post keys.
private enum ApplyPhase {
    case idle
    case armed(ApplyArm)

    var arm: ApplyArm? {
        if case .armed(let arm) = self { return arm }
        return nil
    }
}

private struct ApplyArm {
    /// Distinguishes a re-arm that arrived while a transaction was running.
    let id: UInt64
    let focus: FocusOperation
    /// Bridge settle after the intent, then retry backoff after a failure.
    var runAt: TimeInterval
    var force: Bool
    var failures = 0
    var lastFailure: String?
}

/// Who owns the Rig model/effort shown on the panel. Rig owns it unless a
/// dial intent recently claimed it.
private enum RigOwner {
    case rig
    case dial(until: TimeInterval)
}

private struct RigPanel: Equatable {
    let model: String
    let thinking: String
    let mask: UInt64
}

final class BridgeRuntime {
    private let options: Options
    private let session: SerialSession?
    // The complete latest ESP32 target survives partial application and focus loss;
    // there is no detent queue.
    private var desired: [SettingKind: String] = [:]
    private var generation: UInt64 = 0
    private var phase = ApplyPhase.idle
    private var armCount: UInt64 = 0
    private var receivedRevisions: [SettingKind: UInt64] = [:]
    private var lastFront: Bool?
    private var lastFrontPID: Int32?
    private var receivedModel: String?
    private var receivedThinking: String?
    private var lastApplied: [Int32: [SettingKind: String]] = [:]
    private var bootID: UInt64?
    private var connectionGeneration: UInt64 = 0
    private var rigOwner = RigOwner.rig
    private var rigShown: RigPanel?
    private var rigNextPollAt: TimeInterval = 0
    private var lastRigError: String?
    private var axPrompted = false

    private lazy var chatGPTCatalog = CatalogPoller<CodexModelList.Entry>(
        name: "Codex", load: { CodexModelList.load() }
    ) { [unowned self] entries in
        Catalog.setChatGPTModels(entries.map(\.name))
        self.session?.setChatGPTCatalog(entries)
        print("\(stamp()) ChatGPT model catalog: \(entries.map(\.name).joined(separator: ", "))")
        fflush(stdout)
    }

    private lazy var cursorCatalog = CatalogPoller<CursorModelList.Entry>(
        name: "Cursor", load: { CursorModelList.load() }
    ) { [unowned self] entries in
        Catalog.setCursorCatalog(entries)
        self.session?.setCursorCatalog(entries)
        print("\(stamp()) Cursor model catalog: \(entries.map(\.name).joined(separator: ", "))")
        fflush(stdout)
    }

    init(options: Options) {
        self.options = options
        Catalog.setChatGPTThinkingMask(options.chatGPTThinkingMask)
        Catalog.setCursorEnabledMask(options.cursorModelMask)
        session = options.port.map {
            SerialSession(
                port: $0,
                baud: options.baud,
                chatGPTThinkingMask: options.chatGPTThinkingMask,
                cursorModelMask: options.cursorModelMask,
                showOlderModels: options.showOlderModels,
                dialSwap: options.dialSwap,
                resetOnFirstOpen: options.resetPanel
            )
        }
    }

    // MARK: Panel state

    /// Revisioned STATE is panel state only, never an intent. Legacy SET (firmware
    /// that never negotiated SYNC) stays an intent for compatibility; SYNC is sent
    /// before FRONT, so current firmware never emits SET while connected.
    private func receive(_ update: SerialUpdate) {
        guard let value = update.kind.canonicalName(update.value) else {
            fputs("ai-model-control-bridge: ignore unknown \(update.kind.rawValue) \(update.value)\n", stderr)
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
        print("\(stamp()) rx \(update.kind.rawValue) \(value)")
        BridgeStatus.dialState.emit()
        if update.revision == nil {
            requestApply(force: false, source: "legacy SET")
        }
    }

    private func replaceDesiredWithPanelState() {
        desired = [:]
        if let model = receivedModel { desired[.model] = model }
        if let thinking = receivedThinking { desired[.thinking] = thinking }
        generation &+= 1
    }

    private func forgetPanel() {
        receivedRevisions.removeAll()
        receivedModel = nil
        receivedThinking = nil
        lastApplied.removeAll()
    }

    // MARK: Apply phase transitions

    private func requestApply(force: Bool, source: String) {
        guard DeskFront.isForeground(preferred: options.bundleID) else {
            cancelApply("\(source) received without focus")
            print("\(stamp()) dropped \(source) (no supported app is focused)")
            BridgeStatus.applyDropped.emit()
            return
        }
        arm(force: force)
        if source == "APPLY", phase.arm?.focus.kind == .rig {
            // Firmware ignores host display updates for 8.4 s after rotation.
            // Wait out that window so a later Rig selection is actually received.
            holdRigReadback(for: 8.4)
        }
        print("\(stamp()) rx \(source)")
        BridgeStatus.applying.emit()
    }

    /// idle/armed → armed. Explicit APPLY/PUSH intents arrive after their complete
    /// state frames; the short settle lets a newer physical action supersede this one.
    private func arm(force: Bool) {
        guard !desired.isEmpty else { return }
        let previous = phase.arm
        let focus: FocusOperation
        if let previous, previous.focus.isCurrent {
            focus = previous.focus
        } else if let next = FocusOperation(preferred: options.bundleID) {
            focus = next
        } else {
            return
        }
        if focus.kind == .rig {
            session?.clearHostPanel()
        }
        armCount &+= 1
        phase = .armed(ApplyArm(
            id: armCount,
            focus: focus,
            runAt: uptime + Keys.bridgeSettle,
            force: force || previous?.force == true
        ))
    }

    /// armed → idle, keeping panel state; a new APPLY/PUSH is required.
    private func cancelApply(_ reason: String) {
        if phase.arm != nil && !desired.isEmpty {
            print("\(stamp()) deferred target (\(reason))")
            fflush(stdout)
        }
        phase = .idle
        generation &+= 1
    }

    private func cancelAfterFocusLoss(_ reason: String) {
        cancelApply(reason)
        // Focus may leave and return during one apply; keep the transition
        // visible to noteFront() without restarting keyboard automation.
        lastFront = false
        lastFrontPID = nil
    }

    /// Panel identity changed: drop both the intent and the state it referred to.
    private func discardPending(_ reason: String) {
        if !desired.isEmpty {
            print("\(stamp()) dropped target (\(reason))")
            fflush(stdout)
        }
        desired.removeAll()
        phase = .idle
        generation &+= 1
    }

    // MARK: Serial

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
                    forgetPanel()
                    print("\(stamp()) rx READY \(hex)")
                    fflush(stdout)
                }
                // Respond to duplicates too: an earlier SYNC may have been lost.
                session.requestSync()
                continue
            }
            if raw == "APPLY" || raw == "PUSH" {
                requestApply(force: raw == "PUSH", source: raw)
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
        if let names = Catalog.claudeLiveModels { session.setClaudeCatalog(names) }
        session.flushWrites()
        resetConnectionIfNeeded(session)
    }

    private func resetConnectionIfNeeded(_ session: SerialSession) {
        guard connectionGeneration != session.connectionGeneration else { return }
        connectionGeneration = session.connectionGeneration
        discardPending("serial connection changed")
        forgetPanel()
        bootID = nil
    }

    // MARK: Applying

    private func applyPending() {
        guard var arm = phase.arm, !desired.isEmpty else { return }
        guard arm.focus.isCurrent else {
            cancelAfterFocusLoss("target app left the foreground")
            return
        }
        guard uptime >= arm.runAt else { return }
        if arm.focus.kind != .rig, !accessibilityReady() { return }
        if arm.force {
            lastApplied.removeValue(forKey: arm.focus.pid)
            arm.force = false
            phase = .armed(arm)
        }
        let snapshot = desired
        guard needsApply(snapshot, focus: arm.focus) else {
            phase = .idle
            return
        }
        let revision = generation
        let result = arm.focus.kind == .rig
            ? applyRig(snapshot, focus: arm.focus)
            : applyKeys(snapshot, focus: arm.focus, revision: revision)
        finish(arm, revision: revision, result: result)
        fflush(stdout)
    }

    /// armed → idle, or armed (retry) after a failure while still current.
    private func finish(_ arm: ApplyArm, revision: UInt64, result: Switcher.Result) {
        // A newer APPLY/PUSH re-armed during the transaction; it owns the phase now.
        guard var current = phase.arm, current.id == arm.id else { return }
        guard arm.focus.isCurrent else {
            cancelAfterFocusLoss("target app left the foreground")
            return
        }
        guard generation == revision else {
            cancelApply("newer panel state arrived during apply")
            return
        }
        switch result {
        case .applied:
            phase = .idle
        case .interrupted:
            cancelApply("input guard or apply interrupted")
        case .failed(let message):
            current.failures += 1
            if current.failures >= 3 {
                fputs("ai-model-control-bridge: \(message); stopped until the next APPLY or PUSH\n", stderr)
                BridgeStatus.applyFailed.emit()
                cancelApply("\(arm.focus.displayName) apply failed")
                return
            }
            if message != current.lastFailure {
                fputs("ai-model-control-bridge: \(message); retrying while focused\n", stderr)
            }
            current.lastFailure = message
            current.runAt = uptime + 2
            phase = .armed(current)
        }
    }

    private func accessibilityReady() -> Bool {
        if AXTrust.isTrusted(prompt: false) { return true }
        if !axPrompted {
            axPrompted = true
            _ = AXTrust.isTrusted(prompt: true)
            let message = "Accessibility is not granted; ChatGPT / Cursor / OpenCode keys are skipped. Enable Model Dial in System Settings → Privacy & Security → Accessibility."
            fputs("ai-model-control-bridge: \(message)\n", stderr)
            print("\(stamp()) \(message)")
            fflush(stdout)
        }
        return AXTrust.isTrusted(prompt: false)
    }

    private func needsApply(_ snapshot: [SettingKind: String], focus: FocusOperation) -> Bool {
        snapshot.contains { kind, value in
            if skipsField(kind, value, focus: focus) { return false }
            return lastApplied[focus.pid]?[kind] != value
        }
    }

    /// Cursor models outside its picker cannot be selected; leave them alone.
    private func skipsField(_ kind: SettingKind, _ value: String, focus: FocusOperation) -> Bool {
        focus.kind == .cursor && kind == .model && Catalog.cursorPickerIndex(value) == nil
    }

    private func applyKeys(
        _ snapshot: [SettingKind: String], focus: FocusOperation, revision: UInt64
    ) -> Switcher.Result {
        let pid = focus.pid
        let serialPulse: () -> Bool = {
            self.drainSerial()
            return !focus.isCurrent || self.generation != revision
        }
        return InputGuard.protect(focus: focus) { inputGuard in
            let pulse = { !inputGuard.isValid || serialPulse() }
            // Finish one field at a time, always model before its dependent effort.
            // Invalidate before posting: even a failed/interrupted sequence may mutate UI.
            fields: for kind in SettingKind.allCases {
                guard let value = snapshot[kind], !skipsField(kind, value, focus: focus),
                      lastApplied[pid]?[kind] != value else { continue }
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
                    print("\(stamp()) posted \(kind.rawValue) \(value) via \(path)")
                    BridgeStatus.posted.emit()
                    if kind == .model, focus.kind == .cursor,
                       !Keys.wait(Keys.pickerTiming, pulse: pulse) { break fields }
                case .interrupted, .failed:
                    return result
                }
            }
            return inputGuard.isValid ? .applied(path: "guarded transaction") : .interrupted
        }
    }

    private func applyRig(_ snapshot: [SettingKind: String], focus: FocusOperation) -> Switcher.Result {
        let result = RigApply.apply(model: snapshot[.model], thinking: snapshot[.thinking])
        guard case .applied(let path) = result else { return result }
        lastApplied[focus.pid] = snapshot
        rigShown = RigPanel(
            model: snapshot[.model] ?? rigShown?.model ?? "",
            thinking: snapshot[.thinking] ?? rigShown?.thinking ?? "",
            mask: rigShown?.mask ?? 0
        )
        holdRigReadback(for: 1.5)
        if let model = snapshot[.model] {
            print("\(stamp()) posted model \(model) via \(path)")
        }
        if let thinking = snapshot[.thinking] {
            print("\(stamp()) posted thinking \(thinking) via \(path)")
        }
        BridgeStatus.posted.emit()
        return result
    }

    // MARK: Focus and Rig readback

    private func noteFront() {
        let app = DeskFront.frontmost()
        let front = app.map { DeskFront.isTarget($0, preferred: options.bundleID) } ?? false
        let pid = app?.processIdentifier
        if front != lastFront || (front && pid != lastFrontPID) {
            lastFront = front
            lastFrontPID = pid
            print("\(stamp()) \(DeskFront.label(preferred: options.bundleID))")
            fflush(stdout)
            if phase.arm != nil {
                cancelApply(front ? "focused app changed" : "no supported app is focused")
            }
            rigShown = nil
            rigNextPollAt = 0
        }
        pushRigToPanel(app: app)
    }

    /// rig → dial(until), extending but never shortening an existing hold.
    private func holdRigReadback(for seconds: TimeInterval) {
        let until = uptime + seconds
        if case .dial(let current) = rigOwner, current >= until { return }
        rigOwner = .dial(until: until)
    }

    /// Saved STATE frames are snapshots; only explicit dial intent holds Rig
    /// readback. A mismatch alone would block the first sync forever.
    private func rigReadbackHeld() -> Bool {
        if phase.arm?.focus.kind == .rig { return true }
        guard case .dial(let until) = rigOwner else { return false }
        if uptime < until { return true }
        rigOwner = .rig
        return false
    }

    private func pushRigToPanel(app: NSRunningApplication?) {
        guard let session else { return }
        guard let app, DeskFront.kind(of: app) == .rig else {
            rigShown = nil
            return
        }
        // Poll Rig's socket at the panel refresh rate, not on every 50 ms loop:
        // each poll is two blocking round trips.
        guard uptime >= rigNextPollAt else { return }
        rigNextPollAt = uptime + 0.4
        do {
            let active = try RigClient.modelsActive(timeout: 1)
            let focus = try RigClient.focus(timeout: 1)
            let mask = Catalog.rigEnabledMask(from: active.models)
            let model = Catalog.rigPanelModel(from: focus, active: active.models)
            let thinking = Catalog.rigPanelThinking(from: focus.effort)
            session.setRigCatalog(Catalog.rigCatalogEntries(from: active.models))
            session.setRigModelMask(mask)
            session.setRigEffortMasks(Catalog.rigEffortMasks(from: active.models))
            lastRigError = nil
            if rigReadbackHeld() { return }
            session.setHostPanel(
                model: model, thinking: thinking,
                effortMask: Catalog.rigEffortMask(from: focus.efforts)
            )
            let shown = RigPanel(model: model, thinking: thinking, mask: mask)
            if shown != rigShown {
                lastApplied[app.processIdentifier] = [.model: model, .thinking: thinking]
                receivedModel = model
                receivedThinking = thinking
                replaceDesiredWithPanelState()
                print("\(stamp()) panel Rig \(model) \(thinking)")
                fflush(stdout)
            }
            rigShown = shown
        } catch {
            let message = error.localizedDescription
            if message != lastRigError {
                fputs("ai-model-control-bridge: could not read Rig focus (\(message))\n", stderr)
                lastRigError = message
            }
        }
    }

    func run() -> Never {
        // Load once before FRONT Cursor so a saved model is not clamped against
        // the firmware fallback catalog during the initial USB handshake.
        cursorCatalog.loadNow()
        print("watching ChatGPT / Cursor / OpenCode / Rig / Claude foreground\(session.map { "; listening on \($0.port)" } ?? "") (Ctrl+C to stop)")
        fflush(stdout)
        while true {
            chatGPTCatalog.poll()
            cursorCatalog.poll()
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
        fputs("ai-model-control-bridge: --listen needs --port\n", stderr)
        return 2
    }
    // Watching the frontmost app and forwarding FRONT state only use NSWorkspace.
    // Keyboard control is gated at the point keys could be posted.
    BridgeRuntime(options: options).run()
}
#endif
