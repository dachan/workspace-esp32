#if os(macOS)
import Foundation

struct Options {
    var help = false
    var listPorts = false
    var hidInfo = false
    var checkAX = false
    var checkInputGuard = false
    var front = false
    var watch = false
    var listen = false
    var applyOnConnect = false
    var setModel: String?
    var setThinking: String?
    var port: String?
    var baud = SerialBridge.defaultBaud
    var bundleID: String?
    var chatGPTThinkingMask = Catalog.defaultChatGPTThinkingMask
    var cursorModelMask: UInt64 = 0xFF
}

private func parseMask(_ raw: String) -> UInt64? {
    let value = raw.lowercased().hasPrefix("0x") ? String(raw.dropFirst(2)) : raw
    return UInt64(value, radix: 16) ?? UInt64(raw)
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
        case "--check-input-guard":
            options.checkInputGuard = true
        case "--check-ax":
            options.checkAX = true
        case "--front":
            options.front = true
        case "--watch":
            options.watch = true
        case "--listen":
            options.listen = true
        case "--apply-on-connect":
            options.applyOnConnect = true
        case "--send-serial":
            break // Compatibility with existing launch commands.
        case "--set-model":
            guard let raw = takeValue(), let value = Catalog.modelName(raw) else {
                fputs("ai-model-control-bridge: --set-model needs a supported model name\n", stderr)
                return nil
            }
            options.setModel = value
        case "--set-thinking":
            guard let raw = takeValue(), let value = Catalog.thinkingName(raw) else {
                fputs("ai-model-control-bridge: --set-thinking needs a supported level\n", stderr)
                return nil
            }
            options.setThinking = value
        case "--port":
            guard let value = takeValue(), !value.isEmpty, !value.hasPrefix("--") else {
                fputs("ai-model-control-bridge: --port needs a device path\n", stderr)
                return nil
            }
            options.port = value
        case "--baud":
            guard let value = takeValue(), let parsed = Int(value), SerialSession.supportedBauds.contains(parsed) else {
                fputs("ai-model-control-bridge: --baud must be 9600, 57600, 115200, or 230400\n", stderr)
                return nil
            }
            options.baud = parsed
        case "--bundle-id":
            guard let value = takeValue(), DeskFront.bundleIDs.contains(value) else {
                fputs(
                    "ai-model-control-bridge: --bundle-id must be com.openai.chat, com.openai.codex, com.todesktop.230313mzl4w4u92, or dev.rig.desktop\n",
                    stderr
                )
                return nil
            }
            options.bundleID = value
        case "--chatgpt-effort-mask":
            guard let value = takeValue(), let mask = parseMask(value) else {
                fputs("ai-model-control-bridge: --chatgpt-effort-mask needs a hexadecimal mask\n", stderr)
                return nil
            }
            options.chatGPTThinkingMask = mask
        case "--cursor-model-mask":
            guard let value = takeValue(), let mask = parseMask(value) else {
                fputs("ai-model-control-bridge: --cursor-model-mask needs a hexadecimal mask\n", stderr)
                return nil
            }
            options.cursorModelMask = mask
        case "--json", "--dump-ax", "--list-candidates", "--interval",
             "--max-depth", "--max-nodes":
            fputs("ai-model-control-bridge: \(arg) was removed in the keyboard-only rewrite\n", stderr)
            return nil
        default:
            fputs("ai-model-control-bridge: unknown option \(arg)\n", stderr)
            return nil
        }
        index += 1
    }
    return options
}

func usage() -> String {
    """
    Usage: ai-model-control-bridge [options]

    Apply ESP32 encoder SET MODEL / SET THINKING with keyboard
    shortcuts, only while ChatGPT, Cursor, OpenCode, or Rig is already the foreground app.
    ChatGPT: accessibility model control → Select model → exact label; thinking Ctrl+Shift+, / .
    Cursor: Command-/ model first, then Command-/ again for Effort.
    Rig: models.active / agent.focus / agent.setFocus on the local Rig socket.

    Options:
      --front             Print whether ChatGPT, Cursor, OpenCode, or Rig is foreground and exit
      --watch             Follow foreground + optional serial SET lines
      --listen            Read SET MODEL / SET THINKING from --port
                          (implied by --watch --port)
      --apply-on-connect  Apply the first complete panel snapshot if an app is focused
      --port PATH         USB serial device
      --baud N            Serial baud (default 115200)
      --list-ports        List likely USB serial devices
      --set-model NAME    One-shot: select NAME if ChatGPT, Cursor, OpenCode, or Rig is focused
      --set-thinking LVL  One-shot: set reasoning if ChatGPT, Cursor, OpenCode, or Rig is focused
      --bundle-id ID      Force ChatGPT, Codex, Cursor, OpenCode, or Rig
      --chatgpt-effort-mask HEX  Enabled ChatGPT effort levels (default 0x0f)
      --cursor-model-mask HEX   Enabled Cursor model entries (Auto is always on)
      --hid-info          Describe the keyboard control path
      --check-ax          Check Accessibility permission and exit
      --check-input-guard Check input filter availability for the focused app; no keys posted
      --send-serial       Accepted for the old watch command; unused
      -h, --help
    """
}

#endif
