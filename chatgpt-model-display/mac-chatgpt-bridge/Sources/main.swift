#if os(macOS)
import AppKit
import Foundation

struct Options {
    var help = false
    var listPorts = false
    var hidInfo = false
    var checkAX = false
    var front = false
    var watch = false
    var listen = false
    var sendSerial = false
    var setModel: String?
    var setThinking: String?
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
        case "--list-ports":
            options.listPorts = true
        case "--hid-info":
            options.hidInfo = true
        case "--check-ax":
            options.checkAX = true
        case "--front":
            options.front = true
        case "--watch":
            options.watch = true
        case "--listen":
            options.listen = true
        case "--send-serial":
            options.sendSerial = true
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
        case "--json", "--dump-ax", "--list-candidates", "--interval",
             "--max-depth", "--max-nodes":
            fputs("chatgpt-bridge: \(arg) was removed in the keyboard-only rewrite\n", stderr)
            return nil
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

    Apply ESP32 encoder SET MODEL / SET THINKING with keyboard
    shortcuts, only while ChatGPT is already the foreground app.
    Model: Ctrl+Shift+M, Down to dial index. Thinking: Ctrl+Shift+, / .

    Options:
      --front             Print whether ChatGPT is foreground and exit
      --watch             Follow foreground + optional serial SET lines
      --listen            Read SET MODEL / SET THINKING from --port
                          (implied by --watch --port)
      --port PATH         USB serial device
      --baud N            Serial baud (default 115200)
      --list-ports        List likely USB serial devices
      --set-model NAME    One-shot: select NAME if ChatGPT is focused
      --set-thinking LVL  One-shot: set reasoning if ChatGPT is focused
      --bundle-id ID      Force com.openai.chat or com.openai.codex
      --hid-info          Describe the keyboard control path
      --check-ax          Check Accessibility permission and exit
      --send-serial       Accepted for the old watch command; unused
      -h, --help
    """
}

final class BridgeRuntime {
    var pendingModel: String?
    var pendingThinking: String?
    var lastFront: Bool?
    var lastFrontPID: Int32?
    var retryAt = Date.distantPast
    var lastFailure: String?
    var session: SerialSession?
}

func stamp() -> String {
    ISO8601DateFormatter().string(from: Date())
}

func printFront(preferred: String?) {
    let app = NSWorkspace.shared.frontmostApplication
    let name = app?.localizedName ?? "?"
    let bundle = app?.bundleIdentifier ?? "?"
    print(DeskFront.label(preferred: preferred))
    print("frontmost: \(name) (\(bundle))")
}

func queue(line: SerialLine, runtime: BridgeRuntime, focused: Bool) {
    switch line {
    case .ignored:
        return
    case .setModel(let name):
        guard let model = Catalog.modelName(name) else {
            fputs("chatgpt-bridge: ignore unknown MODEL \(name)\n", stderr)
            return
        }
        runtime.pendingModel = model
        runtime.retryAt = .distantPast
        runtime.lastFailure = nil
        // Always log RX so missing SETs are visible even while ChatGPT is focused.
        if focused {
            print("\(stamp()) rx MODEL \(model)")
        } else {
            print("\(stamp()) queued MODEL \(model) until ChatGPT is focused")
        }
    case .setThinking(let level):
        guard let think = Catalog.thinkingName(level) else {
            fputs("chatgpt-bridge: ignore unknown THINKING \(level)\n", stderr)
            return
        }
        runtime.pendingThinking = think
        runtime.retryAt = .distantPast
        runtime.lastFailure = nil
        if focused {
            print("\(stamp()) rx THINKING \(think)")
        } else {
            print("\(stamp()) queued THINKING \(think) until ChatGPT is focused")
        }
    }
    fflush(stdout)
}

func applyPending(options: Options, runtime: BridgeRuntime) {
    guard DeskFront.isForeground(preferred: options.bundleID) else { return }
    guard runtime.pendingModel != nil || runtime.pendingThinking != nil else { return }
    guard Date() >= runtime.retryAt else { return }

    // Re-enter after supersede so a newer SET wins without 2s backoff.
    for _ in 0..<8 {
        guard DeskFront.isForeground(preferred: options.bundleID) else { return }
        guard Date() >= runtime.retryAt else { return }

        if let name = runtime.pendingModel {
            let applying = name
            let pulse: () -> Bool = {
                drainSerial(options: options, runtime: runtime)
                if let pending = runtime.pendingModel, pending != applying {
                    return true
                }
                return false
            }
            let result = Switcher.model(
                name,
                preferredBundleID: options.bundleID,
                pulse: pulse
            )
            if result.error == "superseded" {
                print("\(stamp()) interrupted MODEL \(applying); queued → \(runtime.pendingModel ?? "?")")
                fflush(stdout)
                continue
            }
            if result.ok {
                if runtime.pendingModel == applying {
                    runtime.pendingModel = nil
                }
                print("\(stamp()) applied MODEL \(name) via \(result.path)")
                fflush(stdout)
                continue
            }
            fail(result.error ?? "SET MODEL failed", runtime: runtime)
            return
        }

        if let level = runtime.pendingThinking {
            let applying = level
            let pulse: () -> Bool = {
                drainSerial(options: options, runtime: runtime)
                // Prefer a fresh model SET over finishing an in-flight thinking apply.
                if runtime.pendingModel != nil {
                    return true
                }
                if let pending = runtime.pendingThinking, pending != applying {
                    return true
                }
                return false
            }
            let result = Switcher.thinking(
                level,
                preferredBundleID: options.bundleID,
                pulse: pulse
            )
            if result.error == "superseded" {
                print("\(stamp()) interrupted THINKING \(applying); setting remains queued")
                fflush(stdout)
                continue
            }
            if result.ok {
                if runtime.pendingThinking == applying {
                    runtime.pendingThinking = nil
                }
                print("\(stamp()) applied THINKING \(level) via \(result.path)")
                fflush(stdout)
                continue
            }
            fail(result.error ?? "SET THINKING failed", runtime: runtime)
            return
        }

        break
    }

    runtime.lastFailure = nil
    runtime.retryAt = .distantPast
    fflush(stdout)
    fflush(stderr)
}

func fail(_ message: String, runtime: BridgeRuntime) {
    if message != runtime.lastFailure {
        fputs("chatgpt-bridge: \(message); setting remains queued\n", stderr)
    }
    runtime.lastFailure = message
    runtime.retryAt = Date().addingTimeInterval(2)
    fflush(stderr)
}

func drainSerial(options: Options, runtime: BridgeRuntime) {
    guard let session = runtime.session else { return }
    let focused = DeskFront.isForeground(preferred: options.bundleID)
    for raw in session.readLines() {
        queue(line: SerialBridge.parseInbound(raw), runtime: runtime, focused: focused)
    }
}

func noteFront(options: Options, runtime: BridgeRuntime) {
    let front = DeskFront.isForeground(preferred: options.bundleID)
    let pid = DeskFront.frontmost()?.processIdentifier
    if front != runtime.lastFront || (front && pid != runtime.lastFrontPID) {
        let becameFocused = front && (runtime.lastFront != true || pid != runtime.lastFrontPID)
        runtime.lastFront = front
        runtime.lastFrontPID = pid
        print("\(stamp()) \(DeskFront.label(preferred: options.bundleID))")
        if becameFocused, runtime.pendingModel != nil || runtime.pendingThinking != nil {
            // ChatGPT just took focus — flush queued dial state with shortcuts.
            runtime.retryAt = .distantPast
            runtime.lastFailure = nil
            print("\(stamp()) flushing queued dial state into ChatGPT")
        }
        fflush(stdout)
    }
}

func runWatch(options: Options) -> Int32 {
    var options = options
    if options.port != nil {
        options.listen = true
    }
    if options.listen, options.port == nil {
        fputs("chatgpt-bridge: --listen needs --port\n", stderr)
        return 2
    }
    if options.listen || options.setModel != nil || options.setThinking != nil {
        guard AXTrust.require(prompt: true) else { return 2 }
    }

    let runtime = BridgeRuntime()
    if options.listen {
        let session = SerialSession(port: options.port, baud: options.baud)
        do {
            try session.open()
            runtime.session = session
        } catch {
            fputs("chatgpt-bridge: \(error)\n", stderr)
            return 1
        }
    }
    defer { runtime.session?.close() }

    var parts = ["watching ChatGPT foreground via NSWorkspace"]
    if options.listen, let port = options.port {
        parts.append("listening for SET MODEL / SET THINKING on \(port)")
    }
    print("\(parts.joined(separator: "; ")) (Ctrl+C to stop)")
    fflush(stdout)

    while true {
        drainSerial(options: options, runtime: runtime)
        noteFront(options: options, runtime: runtime)
        applyPending(options: options, runtime: runtime)
        RunLoop.current.run(until: Date().addingTimeInterval(0.05))
    }
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
        print(Keys.hidInfo)
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
        print(trusted ? "accessibility: granted" : "accessibility: missing")
        return trusted ? 0 : 2
    }
    if options.front {
        printFront(preferred: options.bundleID)
        return DeskFront.isForeground(preferred: options.bundleID) ? 0 : 1
    }
    if let name = options.setModel {
        guard AXTrust.require(prompt: true) else { return 2 }
        let result = Switcher.model(name, preferredBundleID: options.bundleID)
        if result.ok {
            print("applied model \(name) via \(result.path)")
            return 0
        }
        fputs("chatgpt-bridge: \(result.error ?? "set-model failed")\n", stderr)
        return 1
    }
    if let level = options.setThinking {
        guard AXTrust.require(prompt: true) else { return 2 }
        let result = Switcher.thinking(level, preferredBundleID: options.bundleID)
        if result.ok {
            print("applied thinking \(level) via \(result.path)")
            return 0
        }
        fputs("chatgpt-bridge: \(result.error ?? "set-thinking failed")\n", stderr)
        return 1
    }
    if options.watch || options.listen {
        return runWatch(options: options)
    }

    printFront(preferred: options.bundleID)
    return 0
}

exit(run())
#else
import Foundation

fputs("chatgpt-bridge is macOS-only.\n", stderr)
exit(1)
#endif
