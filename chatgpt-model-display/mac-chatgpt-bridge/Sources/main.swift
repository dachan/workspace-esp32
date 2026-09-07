#if os(macOS)
import AppKit
import Foundation

struct Options {
    var json = false
    var dumpAX = false
    var listCandidates = false
    var listPorts = false
    var sendSerial = false
    var hidInfo = false
    var checkAX = false
    var help = false
    var watch = false
    var setModel: String?
    var setThinking: String?
    var listen = false
    var maxDepth = 32
    var maxNodes = 8_000
    var intervalSeconds = 5.0
    var port: String?
    var baud = SerialBridge.defaultBaud
    var bundleID: String?
}

func parseOptions(_ args: [String]) -> Options? {
    var options = Options()
    var index = 0
    let argv = Array(args.dropFirst())
    while index < argv.count {
        let arg = argv[index]
        func takeValue() -> String? {
            index += 1
            return index < argv.count ? argv[index] : nil
        }
        switch arg {
        case "-h", "--help":
            options.help = true
        case "--json":
            options.json = true
        case "--dump-ax":
            options.dumpAX = true
        case "--list-candidates":
            options.listCandidates = true
        case "--list-ports":
            options.listPorts = true
        case "--send-serial":
            options.sendSerial = true
        case "--hid-info":
            options.hidInfo = true
        case "--check-ax":
            options.checkAX = true
        case "--max-depth":
            guard let value = takeValue(), let parsed = Int(value), parsed > 0 else {
                fputs("chatgpt-bridge: --max-depth needs a positive integer\n", stderr)
                return nil
            }
            options.maxDepth = parsed
        case "--max-nodes":
            guard let value = takeValue(), let parsed = Int(value), parsed > 0 else {
                fputs("chatgpt-bridge: --max-nodes needs a positive integer\n", stderr)
                return nil
            }
            options.maxNodes = parsed
        case "--port":
            guard let value = takeValue() else {
                fputs("chatgpt-bridge: --port needs a device path\n", stderr)
                return nil
            }
            options.port = value
        case "--baud":
            guard let value = takeValue(), let parsed = Int(value), parsed > 0 else {
                fputs("chatgpt-bridge: --baud needs a positive integer\n", stderr)
                return nil
            }
            options.baud = parsed
        case "--bundle-id":
            guard let value = takeValue() else {
                fputs("chatgpt-bridge: --bundle-id needs an identifier\n", stderr)
                return nil
            }
            options.bundleID = value
        case "--watch":
            options.watch = true
        case "--set-model":
            guard let value = takeValue() else {
                fputs("chatgpt-bridge: --set-model needs a name\n", stderr)
                return nil
            }
            options.setModel = value
        case "--set-thinking":
            guard let value = takeValue() else {
                fputs("chatgpt-bridge: --set-thinking needs a level\n", stderr)
                return nil
            }
            options.setThinking = value
        case "--listen":
            options.listen = true
        case "--interval":
            guard let value = takeValue(), let parsed = Double(value), parsed > 0 else {
                fputs("chatgpt-bridge: --interval needs a positive number of seconds\n", stderr)
                return nil
            }
            options.intervalSeconds = parsed
        default:
            fputs("chatgpt-bridge: unknown option \(arg)\n", stderr)
            return nil
        }
        index += 1
    }
    return options
}

func usage() -> String {
    """
    Usage: chatgpt-bridge [options]

    Read the selected model from the ChatGPT macOS app via Accessibility.

    Options:
      --json              Print one JSON object
      --dump-ax           Dump the ChatGPT AX tree
      --max-depth N       AX walk depth (default 32)
      --max-nodes N       AX walk cap (default 8000)
      --list-candidates   Print scored model-like nodes
      --list-ports        List likely USB serial devices
      --send-serial       Write MODEL <name> (dry-run without --port)
      --port PATH         USB serial device
      --baud N            Serial baud (default 115200)
      --bundle-id ID      Force com.openai.chat or com.openai.codex
      --hid-info          Describe the keyboard control path
      --check-ax          Check Accessibility permission and exit
      --watch             Poll the selected model until interrupted
      --set-model NAME    One-shot: choose NAME from the model picker
      --set-thinking LVL  One-shot: set the thinking level
      --listen            Read SET MODEL / SET THINKING from --port and apply
                          them in ChatGPT (implied by --watch --port)
      --interval SEC      Watch poll interval in seconds (default 5)
      -h, --help
    """
}

func printJSON(_ value: some Encodable) {
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.sortedKeys]
    guard let data = try? encoder.encode(value), let line = String(data: data, encoding: .utf8) else {
        fputs("chatgpt-bridge: failed to encode JSON\n", stderr)
        return
    }
    print(line)
}

func printHuman(_ readback: ModelReadback) {
    if readback.ok, let model = readback.model {
        print("ChatGPT model: \(model)")
        if let source = readback.source {
            print("source: \(source)")
        }
    } else {
        fputs("chatgpt-bridge: \(readback.error ?? "model not found")\n", stderr)
    }
    let app = readback.appName ?? "ChatGPT"
    let bundle = readback.bundleID ?? "?"
    let pid = readback.pid.map(String.init) ?? "?"
    print("app: \(app) (\(bundle), pid \(pid))")
}

func run() -> Int32 {
    guard let options = parseOptions(CommandLine.arguments) else {
        return 2
    }
    if options.help {
        print(usage())
        return 0
    }
    if options.hidInfo {
        HIDBridge.printInfo()
        return 0
    }
    if options.listPorts {
        let ports = SerialBridge.candidatePorts()
        if ports.isEmpty {
            print("no USB serial devices matched")
        } else {
            ports.forEach { print($0) }
        }
        return 0
    }
    if options.checkAX {
        let trusted = AXTrust.isTrusted(prompt: true)
        if options.json {
            printJSON(["ok": trusted, "accessibility": trusted])
        } else {
            print(trusted ? "accessibility: granted" : "accessibility: missing")
        }
        return trusted ? 0 : 2
    }

    guard AXTrust.require(prompt: true) else {
        return 2
    }

    if let name = options.setModel {
        let app = ChatGPTProcess.find(preferredBundleID: options.bundleID)
        let result = ChatGPTApply.model(name, app: app, maxDepth: options.maxDepth, maxNodes: options.maxNodes)
        if result.ok {
            print("applied model \(name) via \(result.path)")
            return 0
        }
        fputs("chatgpt-bridge: \(result.error ?? "set-model failed")\n", stderr)
        return 1
    }
    if let level = options.setThinking {
        let app = ChatGPTProcess.find(preferredBundleID: options.bundleID)
        let result = ChatGPTApply.thinking(level, app: app, maxDepth: options.maxDepth, maxNodes: options.maxNodes)
        if result.ok {
            print("applied thinking \(level) via \(result.path)")
            return 0
        }
        fputs("chatgpt-bridge: \(result.error ?? "set-thinking failed")\n", stderr)
        return 1
    }

    let app = ChatGPTProcess.find(preferredBundleID: options.bundleID)
    if app == nil {
        let hint = options.bundleID ?? ChatGPTProcess.knownBundleIDs.joined(separator: " or ")
        fputs("chatgpt-bridge: ChatGPT is not running (looked for \(hint))\n", stderr)
        // Still allow cache → serial for watch/send continuity.
        if options.dumpAX || options.listCandidates {
            return 1
        }
        if options.watch {
            return runWatch(options: options, initialApp: nil)
        }
        if options.listen {
            return runListen(options: options, initialApp: nil)
        }
        return emitOnce(options: options, app: nil)
    }

    let runningApp = app!

    if options.dumpAX {
        ModelReader.dump(app: runningApp, maxDepth: options.maxDepth, maxNodes: options.maxNodes)
        return 0
    }

    if options.listCandidates {
        let readback = ModelReader.read(app: runningApp, maxDepth: options.maxDepth, maxNodes: options.maxNodes)
        if options.json {
            printJSON(readback)
        } else {
            if readback.candidates.isEmpty {
                print("no model-like AX nodes")
            } else {
                for candidate in readback.candidates {
                    print("\(candidate.score)\t\(candidate.model)\t\(candidate.source)\t\(candidate.path)")
                }
            }
        }
        return readback.ok ? 0 : 1
    }

    if options.watch {
        return runWatch(options: options, initialApp: runningApp)
    }
    if options.listen {
        return runListen(options: options, initialApp: runningApp)
    }

    return emitOnce(options: options, app: runningApp)
}

final class BridgeRuntime {
    var previousModel: String?
    var previousThinking: String?
    var suppressSerialUntil = Date.distantPast
    var ignoreModel: String?
    var ignoreThinking: String?
    var pendingSerialSync = false
    var session: SerialSession?
}

func thinkingFrom(_ model: String) -> String? {
    ThinkingText.canonical(model)
}

/// Strip a trailing current or legacy thinking token from a Codex chip title.
func modelBaseName(_ model: String) -> String {
    let base = ThinkingText.stripSuffix(model)
    return base.isEmpty ? model : base
}

func sendSerialModel(
    model: String,
    thinking: String?,
    options: Options,
    session: SerialSession?
) -> Bool {
    do {
        let base = modelBaseName(model)
        let think = thinking ?? thinkingFrom(model)
        try SerialBridge.send(
            model: base,
            thinking: think,
            port: options.port,
            baud: options.baud,
            echoLine: !options.json,
            session: session
        )
        return true
    } catch {
        fputs("chatgpt-bridge: \(error)\n", stderr)
        return false
    }
}

func emitOnce(options: Options, app: NSRunningApplication?) -> Int32 {
    let readback: ModelReadback
    if let app {
        readback = ModelReader.read(app: app, maxDepth: options.maxDepth, maxNodes: options.maxNodes)
    } else {
        readback = ModelReadback(
            ok: false,
            model: nil,
            source: nil,
            bundleID: options.bundleID,
            pid: nil,
            appName: nil,
            candidates: [],
            error: "ChatGPT is not running"
        )
    }

    if readback.ok, let model = readback.model {
        let base = modelBaseName(model)
        let thinking = thinkingFrom(model)
        ModelCache.save(base)
        if let thinking {
            ModelCache.saveThinking(thinking)
        }
        if options.json {
            printJSON(readback)
        } else {
            printHuman(readback)
        }
        if options.sendSerial {
            return sendSerialModel(
                model: base,
                thinking: thinking,
                options: options,
                session: nil
            ) ? 0 : 1
        }
        return 0
    }

    // Live AX failed — fall back to disk cache when present.
    if let cached = ModelCache.load() {
        fputs("chatgpt-bridge: using cached model: \(cached)\n", stderr)
        if options.json {
            var cachedReadback = readback
            cachedReadback.ok = true
            cachedReadback.model = cached
            cachedReadback.source = "disk-cache"
            printJSON(cachedReadback)
        } else {
            print("ChatGPT model: \(cached)")
            print("source: disk-cache")
            let app = readback.appName ?? "ChatGPT"
            let bundle = readback.bundleID ?? "?"
            let pid = readback.pid.map(String.init) ?? "?"
            print("app: \(app) (\(bundle), pid \(pid))")
        }
        if options.sendSerial {
            return sendSerialModel(
                model: cached,
                thinking: thinkingFrom(cached) ?? ModelCache.loadThinking(),
                options: options,
                session: nil
            ) ? 0 : 1
        }
        return 0
    }

    if options.json {
        printJSON(readback)
    } else {
        printHuman(readback)
    }
    if options.sendSerial {
        fputs("chatgpt-bridge: not sending serial; no confident model and no cache\n", stderr)
    }
    return 1
}

@discardableResult
func emitChange(
    options: Options,
    readback: ModelReadback,
    runtime: BridgeRuntime,
    fromCache: Bool = false
) -> Bool {
    var model: String?
    var thinking: String?
    let sourceOverride: String?
    if readback.ok, let live = readback.model {
        model = live
        thinking = thinkingFrom(live)
        sourceOverride = nil
    } else if let cached = ModelCache.load() {
        if !fromCache {
            fputs("chatgpt-bridge: using cached model: \(cached)\n", stderr)
        }
        model = cached
        thinking = thinkingFrom(cached) ?? ModelCache.loadThinking()
        sourceOverride = "disk-cache"
    } else {
        model = nil
        thinking = nil
        sourceOverride = nil
    }

    model = model.map(modelBaseName)
    if let ignore = runtime.ignoreModel, model == ignore {
        fputs("chatgpt-bridge: ignore stale AX model after encoder SET\n", stderr)
        model = runtime.previousModel
    } else if model == runtime.previousModel {
        runtime.ignoreModel = nil
    }
    if let ignore = runtime.ignoreThinking, thinking == ignore {
        thinking = runtime.previousThinking
    } else if thinking == runtime.previousThinking {
        runtime.ignoreThinking = nil
    }
    if let model {
        ModelCache.save(model)
    }
    if let thinking {
        ModelCache.saveThinking(thinking)
    }
    let modelChanged = model != runtime.previousModel
    let thinkingChanged = thinking != runtime.previousThinking
    let stateChanged = modelChanged || thinkingChanged
    let now = Date()
    let canFlushPending = options.sendSerial
        && runtime.pendingSerialSync
        && now >= runtime.suppressSerialUntil
        && model != nil
    guard stateChanged || canFlushPending else {
        return false
    }

    if stateChanged {
        runtime.previousModel = model
        runtime.previousThinking = thinking

        if options.json {
            if let model, let sourceOverride {
                var cachedReadback = readback
                cachedReadback.ok = true
                cachedReadback.model = model
                cachedReadback.source = sourceOverride
                printJSON(cachedReadback)
            } else {
                printJSON(readback)
            }
        } else if let model {
            let stamp = ISO8601DateFormatter().string(from: now)
            print("\(stamp) model: \(model)")
            if let thinking {
                print("thinking: \(thinking)")
            }
            if let sourceOverride {
                print("source: \(sourceOverride)")
            } else if let source = readback.source {
                print("source: \(source)")
            }
        } else {
            let stamp = ISO8601DateFormatter().string(from: now)
            fputs("\(stamp) chatgpt-bridge: \(readback.error ?? "model not found")\n", stderr)
        }
        fflush(stdout)
        fflush(stderr)
    }

    if options.sendSerial, let model {
        if now < runtime.suppressSerialUntil {
            runtime.pendingSerialSync = true
            fputs("serial hold after encoder SET; skip MODEL/THINKING echo\n", stderr)
        } else {
            runtime.pendingSerialSync = !sendSerialModel(
                model: model,
                thinking: thinking,
                options: options,
                session: runtime.session
            )
        }
    }
    return stateChanged || canFlushPending
}

func applyEncoderSet(
    line: SerialLine,
    options: Options,
    app: NSRunningApplication?,
    runtime: BridgeRuntime
) {
    switch line {
    case .ignored:
        return
    case .setModel(let name):
        runtime.suppressSerialUntil = Date().addingTimeInterval(8)
        runtime.pendingSerialSync = true
        runtime.ignoreModel = runtime.previousModel
        let result = ChatGPTApply.model(
            name,
            app: app,
            maxDepth: options.maxDepth,
            maxNodes: options.maxNodes
        )
        if result.ok {
            runtime.previousModel = modelBaseName(name)
            ModelCache.save(runtime.previousModel ?? name)
            let stamp = ISO8601DateFormatter().string(from: Date())
            print("\(stamp) applied SET MODEL \(name) via \(result.path)")
        } else {
            runtime.ignoreModel = nil
            fputs("chatgpt-bridge: \(result.error ?? "SET MODEL failed")\n", stderr)
        }
        fflush(stdout)
        fflush(stderr)
    case .setThinking(let level):
        runtime.suppressSerialUntil = Date().addingTimeInterval(8)
        runtime.pendingSerialSync = true
        runtime.ignoreThinking = runtime.previousThinking
        let result = ChatGPTApply.thinking(
            level,
            app: app,
            maxDepth: options.maxDepth,
            maxNodes: options.maxNodes
        )
        if result.ok {
            runtime.previousThinking = ThinkingText.canonical(level) ?? level
            ModelCache.saveThinking(runtime.previousThinking ?? level)
            let stamp = ISO8601DateFormatter().string(from: Date())
            print("\(stamp) applied SET THINKING \(level) via \(result.path)")
        } else {
            runtime.ignoreThinking = nil
            fputs("chatgpt-bridge: \(result.error ?? "SET THINKING failed")\n", stderr)
        }
        fflush(stdout)
        fflush(stderr)
    }
}

func drainSerial(options: Options, app: NSRunningApplication?, runtime: BridgeRuntime) {
    guard let session = runtime.session else { return }
    for raw in session.readLines() {
        applyEncoderSet(
            line: SerialBridge.parseInbound(raw),
            options: options,
            app: app,
            runtime: runtime
        )
    }
}

func runWatch(options: Options, initialApp: NSRunningApplication?) -> Int32 {
    var options = options
    if options.port != nil {
        options.listen = true
    }
    if options.listen, options.port == nil, options.sendSerial {
        fputs("chatgpt-bridge: --listen needs --port to read SET lines\n", stderr)
    }

    let runtime = BridgeRuntime()
    if options.port != nil || options.sendSerial || options.listen {
        let session = SerialSession(port: options.port, baud: options.baud)
        do {
            try session.open()
            runtime.session = session
        } catch {
            fputs("chatgpt-bridge: \(error)\n", stderr)
            if options.listen, options.port != nil {
                return 1
            }
        }
    }
    defer { runtime.session?.close() }

    var app = initialApp
    if !options.json {
        var parts = ["watching every \(options.intervalSeconds)s"]
        if options.listen, options.port != nil {
            parts.append("listening for SET MODEL / SET THINKING on \(options.port!)")
        }
        print("\(parts.joined(separator: "; ")) (Ctrl+C to stop)")
        fflush(stdout)
    }

    var lastAX = Date.distantPast
    while true {
        if let found = ChatGPTProcess.find(preferredBundleID: options.bundleID) {
            app = found
        }
        drainSerial(options: options, app: app, runtime: runtime)

        let now = Date()
        if now.timeIntervalSince(lastAX) >= options.intervalSeconds {
            lastAX = now
            let readback: ModelReadback
            if let app {
                readback = ModelReader.read(
                    app: app,
                    maxDepth: options.maxDepth,
                    maxNodes: options.maxNodes
                )
            } else {
                readback = ModelReadback(
                    ok: false,
                    model: nil,
                    source: nil,
                    bundleID: options.bundleID,
                    pid: nil,
                    appName: nil,
                    candidates: [],
                    error: "ChatGPT is not running"
                )
            }
            let usingCache = !(readback.ok && readback.model != nil)
            _ = emitChange(
                options: options,
                readback: readback,
                runtime: runtime,
                fromCache: usingCache && runtime.previousModel != nil
            )
        }
        Thread.sleep(forTimeInterval: 0.05)
    }
}

func runListen(options: Options, initialApp: NSRunningApplication?) -> Int32 {
    guard options.port != nil else {
        fputs("chatgpt-bridge: --listen needs --port\n", stderr)
        return 2
    }
    var watchOptions = options
    watchOptions.watch = true
    watchOptions.listen = true
    watchOptions.sendSerial = options.sendSerial
    return runWatch(options: watchOptions, initialApp: initialApp)
}

exit(run())
#else
import Foundation

fputs("chatgpt-bridge is macOS-only.\n", stderr)
exit(1)
#endif
