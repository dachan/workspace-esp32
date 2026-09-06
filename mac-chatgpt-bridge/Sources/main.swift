#if os(macOS)
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
      --hid-info          Document the Ctrl+Shift+M HID path
      --check-ax          Check Accessibility permission and exit
      --watch             Poll the selected model until interrupted
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
    guard let app = ChatGPTProcess.find(preferredBundleID: options.bundleID) else {
        let hint = options.bundleID ?? ChatGPTProcess.knownBundleIDs.joined(separator: " or ")
        fputs("chatgpt-bridge: ChatGPT is not running (looked for \(hint))\n", stderr)
        return 1
    }

    if options.dumpAX {
        ModelReader.dump(app: app, maxDepth: options.maxDepth, maxNodes: options.maxNodes)
        return 0
    }

    if options.listCandidates {
        let readback = ModelReader.read(app: app, maxDepth: options.maxDepth, maxNodes: options.maxNodes)
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
        return runWatch(options: options, initialApp: app)
    }

    return emitOnce(options: options, app: app)
}

func emitOnce(options: Options, app: NSRunningApplication) -> Int32 {
    let readback = ModelReader.read(app: app, maxDepth: options.maxDepth, maxNodes: options.maxNodes)
    if options.json {
        printJSON(readback)
    } else {
        printHuman(readback)
    }

    if options.sendSerial {
        guard let model = readback.model, readback.ok else {
            fputs("chatgpt-bridge: not sending serial; no confident model\n", stderr)
            return 1
        }
        do {
            try SerialBridge.send(
                model: model,
                port: options.port,
                baud: options.baud,
                echoLine: !options.json
            )
        } catch {
            fputs("chatgpt-bridge: \(error)\n", stderr)
            return 1
        }
    }
    return readback.ok ? 0 : 1
}

@discardableResult
func emitChange(
    options: Options,
    readback: ModelReadback,
    previousModel: inout String?
) -> Bool {
    let model = readback.ok ? readback.model : nil
    guard model != previousModel else {
        return false
    }
    previousModel = model

    if options.json {
        printJSON(readback)
    } else if let model {
        let stamp = ISO8601DateFormatter().string(from: Date())
        print("\(stamp) model: \(model)")
        if let source = readback.source {
            print("source: \(source)")
        }
    } else {
        let stamp = ISO8601DateFormatter().string(from: Date())
        fputs("\(stamp) chatgpt-bridge: \(readback.error ?? "model not found")\n", stderr)
    }
    fflush(stdout)
    fflush(stderr)

    if options.sendSerial, let model, readback.ok {
        do {
            try SerialBridge.send(
                model: model,
                port: options.port,
                baud: options.baud,
                echoLine: !options.json
            )
        } catch {
            fputs("chatgpt-bridge: \(error)\n", stderr)
        }
    }
    return true
}

func runWatch(options: Options, initialApp: NSRunningApplication) -> Int32 {
    var app = initialApp
    var previousModel: String?
    if !options.json {
        print(
            "watching every \(options.intervalSeconds)s (Ctrl+C to stop); prints on change"
        )
        fflush(stdout)
    }

    while true {
        if ChatGPTProcess.find(preferredBundleID: options.bundleID) == nil {
            // keep last known app handle; find may still fail briefly
        }
        if let found = ChatGPTProcess.find(preferredBundleID: options.bundleID) {
            app = found
        }

        let readback = ModelReader.read(
            app: app,
            maxDepth: options.maxDepth,
            maxNodes: options.maxNodes
        )
        _ = emitChange(options: options, readback: readback, previousModel: &previousModel)
        Thread.sleep(forTimeInterval: options.intervalSeconds)
    }
}

exit(run())
#else
import Foundation

fputs("chatgpt-bridge is macOS-only.\n", stderr)
exit(1)
#endif
