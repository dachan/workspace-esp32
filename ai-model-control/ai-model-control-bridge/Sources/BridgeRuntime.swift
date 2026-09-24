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
    private var nextChatGPTRefreshAt: TimeInterval = 0
    private var chatGPTRefreshInFlight = false
    private var lastChatGPTCatalogError = false
    private var lastChatGPTCatalog: [CodexModelList.Entry]?
    private var lastRigPushAt: TimeInterval = 0
    private var lastRigPanel: (model: String, thinking: String, mask: UInt64)?
    private var lastRigError: String?
    /// Skip Rig→panel MODEL/THINKING while a dial apply is settling or just posted.
    private var suppressHostPushUntil: TimeInterval = 0
    private var axPrompted = false

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
                dialSwap: options.dialSwap
            )
        }
    }

    private func receive(_ update: SerialUpdate) {
        let legacyIntent = update.revision == nil
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
        fflush(stdout)
        if legacyIntent {
            requestApply(force: false, source: "legacy SET")
        }
    }

    private func requestApply(force: Bool, source: String) {
        guard DeskFront.isForeground(preferred: options.bundleID) else {
            suspendPending("\(source) received without focus")
            print("\(stamp()) dropped \(source) (no supported app is focused)")
            fflush(stdout)
            return
        }
        beginApplyingToFocusedApp(force: force)
        print("\(stamp()) rx \(source)")
        fflush(stdout)
    }

    private func replaceDesiredWithPanelState() {
        desired = [:]
        if let model = receivedModel { desired[.model] = model }
        if let thinking = receivedThinking { desired[.thinking] = thinking }
        generation &+= 1
    }

    /// The dial is authoritative, but state receipt alone never posts keys.
    /// A complete snapshot is applied only after explicit APPLY/PUSH intent
    /// while a supported app is already foreground.
    private func beginApplyingToFocusedApp(force: Bool = false) {
        guard !desired.isEmpty, DeskFront.isForeground(preferred: options.bundleID) else { return }
        let continuingTarget = targetFocus?.isCurrent == true
        if !continuingTarget {
            targetFocus = FocusOperation(preferred: options.bundleID)
        }
        guard targetFocus != nil else { return }
        applyFailures = 0
        forceApply = forceApply || force
        // Explicit APPLY/PUSH intents arrive after their complete state frames.
        // Keep the short settle so a newer physical action can supersede this one.
        settleAt = ProcessInfo.processInfo.systemUptime + Keys.bridgeSettle
        retryAt = 0
        lastFailure = nil
    }

    /// Interrupting an apply retains panel state, but a new APPLY/PUSH intent
    /// is required before another keyboard transaction starts.
    private func suspendPending(_ reason: String) {
        if targetFocus != nil && !desired.isEmpty {
            print("\(stamp()) deferred target (\(reason))")
            fflush(stdout)
        }
        targetFocus = nil
        generation &+= 1
        retryAt = 0
        lastFailure = nil
        forceApply = false
    }

    private func suspendAfterFocusLoss(_ reason: String) {
        suspendPending(reason)
        // Focus may leave and return during one apply; keep the transition
        // visible to noteFront() without restarting keyboard automation.
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
        if focus.kind == .rig {
            applyRig(focus: focus)
            return
        }
        if !AXTrust.isTrusted(prompt: false) {
            if !axPrompted {
                axPrompted = true
                _ = AXTrust.isTrusted(prompt: true)
                let message = "Accessibility is not granted; ChatGPT / Cursor / OpenCode keys are skipped. Enable Model Dial in System Settings → Privacy & Security → Accessibility."
                fputs("ai-model-control-bridge: \(message)\n", stderr)
                print("\(stamp()) \(message)")
                fflush(stdout)
            }
            if !AXTrust.isTrusted(prompt: false) { return }
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
                fputs("ai-model-control-bridge: \(message); stopped until the next supported-app focus or dial update\n", stderr)
                suspendPending("\(focus.displayName) apply failed")
            } else {
                if message != lastFailure {
                    fputs("ai-model-control-bridge: \(message); retrying while focused\n", stderr)
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

    private func applyRig(focus: FocusOperation) {
        guard focus.isCurrent else {
            suspendAfterFocusLoss("target app left the foreground")
            return
        }
        let now = ProcessInfo.processInfo.systemUptime
        guard now >= retryAt, now >= settleAt else { return }
        let snapshot = desired
        let pid = focus.pid
        if forceApply {
            lastApplied.removeValue(forKey: pid)
            forceApply = false
        }
        let needsApply = snapshot.contains { kind, value in lastApplied[pid]?[kind] != value }
        guard needsApply else { return }
        let result = RigApply.apply(model: snapshot[.model], thinking: snapshot[.thinking])
        switch result {
        case .applied(let path):
            lastApplied[pid] = snapshot
            lastFailure = nil
            retryAt = 0
            applyFailures = 0
            lastRigPanel = (
                snapshot[.model] ?? lastRigPanel?.model ?? "",
                snapshot[.thinking] ?? lastRigPanel?.thinking ?? "",
                lastRigPanel?.mask ?? 0
            )
            suppressHostPushUntil = ProcessInfo.processInfo.systemUptime + 1.5
            if let model = snapshot[.model] {
                print("\(stamp()) posted model \(model) via \(path)")
            }
            if let thinking = snapshot[.thinking] {
                print("\(stamp()) posted thinking \(thinking) via \(path)")
            }
        case .interrupted:
            suspendPending("Rig apply interrupted")
        case .failed(let message):
            applyFailures += 1
            if applyFailures >= 3 {
                fputs("ai-model-control-bridge: \(message); stopped until the next supported-app focus or dial update\n", stderr)
                suspendPending("Rig apply failed")
            } else {
                if message != lastFailure {
                    fputs("ai-model-control-bridge: \(message); retrying while focused\n", stderr)
                }
                lastFailure = message
                retryAt = ProcessInfo.processInfo.systemUptime + 2
            }
        }
        if !focus.isCurrent, targetFocus === focus {
            suspendAfterFocusLoss("target app left the foreground")
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
            fflush(stdout)
            if targetFocus != nil {
                suspendPending(front ? "focused app changed" : "no supported app is focused")
            }
            lastRigPanel = nil
            lastRigPushAt = 0
        }
        pushRigToPanel(app: app)
    }

    private func pushRigToPanel(app: NSRunningApplication?) {
        guard let session else { return }
        guard let app, DeskFront.kind(of: app) == .rig else {
            lastRigPanel = nil
            return
        }
        let now = ProcessInfo.processInfo.systemUptime
        let dialAhead =
            (desired[.model].map { $0 != lastRigPanel?.model } ?? false)
            || (desired[.thinking].map { $0 != lastRigPanel?.thinking } ?? false)
        let holdHost = now < suppressHostPushUntil
            || (targetFocus != nil && now < settleAt + 1.25)
            || dialAhead
        do {
            let active = try RigClient.modelsActive()
            let focus = try RigClient.focus()
            let mask = Catalog.rigEnabledMask(from: active.models)
            let model = Catalog.rigPanelModel(from: focus, active: active.models)
            let masks = Catalog.rigEffortMasks(from: active.models)
            let effortMask = Catalog.rigEffortMask(from: focus.efforts)
            let thinking = Catalog.rigPanelThinking(from: focus.effort)
            let catalog = Catalog.rigCatalogEntries(from: active.models)
            session.setRigCatalog(catalog)
            session.setRigModelMask(mask)
            session.setRigEffortMasks(masks)
            if holdHost { return }
            if lastRigPanel != nil, now - lastRigPushAt < 0.4 {
                return
            }
            lastRigPushAt = now
            session.setHostPanel(model: model, thinking: thinking, effortMask: effortMask)
            if lastRigPanel?.model != model || lastRigPanel?.thinking != thinking || lastRigPanel?.mask != mask {
                lastApplied[app.processIdentifier] = [.model: model, .thinking: thinking]
                receivedModel = model
                receivedThinking = thinking
                print("\(stamp()) panel Rig \(model) \(thinking)")
                fflush(stdout)
            }
            lastRigPanel = (model, thinking, mask)
            lastRigError = nil
        } catch {
            let message = error.localizedDescription
            if message != lastRigError {
                fputs("ai-model-control-bridge: could not read Rig focus (\(message))\n", stderr)
                lastRigError = message
            }
        }
    }

    private func refreshChatGPTCatalog() {
        let now = ProcessInfo.processInfo.systemUptime
        guard !chatGPTRefreshInFlight, now >= nextChatGPTRefreshAt else { return }
        chatGPTRefreshInFlight = true
        nextChatGPTRefreshAt = now + 10
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let entries = CodexModelList.load()
            DispatchQueue.main.async {
                guard let self else { return }
                self.chatGPTRefreshInFlight = false
                guard let entries else {
                    if !self.lastChatGPTCatalogError {
                        fputs("ai-model-control-bridge: Codex model list unavailable; retaining panel catalog\n", stderr)
                    }
                    self.lastChatGPTCatalogError = true
                    return
                }
                self.lastChatGPTCatalogError = false
                guard entries != self.lastChatGPTCatalog else { return }
                self.lastChatGPTCatalog = entries
                Catalog.setChatGPTModels(entries.map(\.name))
                self.session?.setChatGPTCatalog(entries)
                print("\(stamp()) ChatGPT model catalog: \(entries.map(\.name).joined(separator: ", "))")
                fflush(stdout)
            }
        }
    }

    func run() -> Never {
        print("watching ChatGPT / Cursor / OpenCode / Rig foreground\(session.map { "; listening on \($0.port)" } ?? "") (Ctrl+C to stop)")
        fflush(stdout)
        while true {
            refreshChatGPTCatalog()
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
