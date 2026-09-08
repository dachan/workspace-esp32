#if os(macOS)
import AppKit
import Foundation

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
    if options.setModel != nil || options.setThinking != nil {
        guard AXTrust.require(prompt: true) else { return 2 }
        guard let focus = FocusOperation(preferred: options.bundleID) else { return 1 }
        let result = InputGuard.protect(focus: focus) { inputGuard in
            if let name = options.setModel {
                let result = Switcher.model(name, preferredBundleID: options.bundleID)
                if case .applied = result {} else { return result }
            }
            guard inputGuard.isValid else { return .interrupted }
            if let level = options.setThinking {
                return Switcher.thinking(level, model: options.setModel, preferredBundleID: options.bundleID)
            }
            return .applied(path: "guarded model selection")
        }
        let status = report(result)
        if status != 0 { return status }
        if !options.watch && !options.listen { return 0 }
    }
    if options.watch || options.listen {
        return runWatch(options: options)
    }

    printFront(preferred: options.bundleID)
    return 0
}

func report(_ result: Switcher.Result) -> Int32 {
    switch result {
    case .applied(let path):
        print("applied via \(path)")
        return 0
    case .interrupted:
        fputs("chatgpt-bridge: operation interrupted by a focus change\n", stderr)
    case .failed(let message):
        fputs("chatgpt-bridge: \(message)\n", stderr)
    }
    return 1
}

exit(run())
#else
import Foundation

fputs("chatgpt-bridge is macOS-only.\n", stderr)
exit(1)
#endif
