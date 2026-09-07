import Foundation
#if canImport(Darwin)
import Darwin
#endif
#if canImport(CoreGraphics)
import CoreGraphics
#endif

enum SerialLine {
    case setModel(String)
    case setThinking(String)
    case ignored
}

enum SerialBridge {
    static let defaultBaud = 115_200

    static func protocolLine(model: String) -> String {
        "MODEL \(model)\n"
    }

    static func thinkingLine(_ level: String) -> String {
        "THINKING \(level)\n"
    }

    static func parseInbound(_ raw: String) -> SerialLine {
        var line = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !line.isEmpty else { return .ignored }
        if line.hasPrefix("SET MODEL ") || line.hasPrefix("SET MODEL\t") {
            let value = String(line.dropFirst(10)).trimmingCharacters(in: .whitespacesAndNewlines)
            return value.isEmpty ? .ignored : .setModel(value)
        }
        if line.hasPrefix("SET THINKING ") || line.hasPrefix("SET THINKING\t") {
            let value = String(line.dropFirst(13)).trimmingCharacters(in: .whitespacesAndNewlines)
            return value.isEmpty ? .ignored : .setThinking(value)
        }
        // MODEL / THINKING are Mac → ESP. Never treat them as inbound commands.
        return .ignored
    }

    static func candidatePorts() -> [String] {
        let dir = "/dev"
        guard let names = try? FileManager.default.contentsOfDirectory(atPath: dir) else {
            return []
        }
        let prefixes = ["cu.usbmodem", "cu.usbserial", "cu.wchusbserial", "cu.SLAB_USBtoUART"]
        return names
            .filter { name in prefixes.contains(where: { name.hasPrefix($0) }) }
            .map { "\(dir)/\($0)" }
            .sorted()
    }

    static func send(
        model: String,
        thinking: String? = nil,
        port: String?,
        baud: Int,
        echoLine: Bool,
        session: SerialSession? = nil
    ) throws {
        if let session {
            try session.writeModel(model, thinking: thinking, echoLine: echoLine)
            return
        }
        let session = SerialSession(port: port, baud: baud)
        try session.open()
        defer { session.close() }
        try session.writeModel(model, thinking: thinking, echoLine: echoLine)
    }
}

final class SerialSession {
    let port: String?
    let baud: Int
    private var fd: Int32 = -1
    private var pending = ""

    init(port: String?, baud: Int) {
        self.port = port
        self.baud = baud
    }

    var isOpen: Bool { fd >= 0 }

    func open() throws {
        guard let port, !port.isEmpty else { return }
        #if os(macOS)
        if fd >= 0 { return }
        let opened = Darwin.open(port, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard opened >= 0 else {
            throw StubError.io("could not open \(port) (errno \(errno))")
        }
        fd = opened
        applyBaud(fd, baud: baud)
        #else
        throw StubError.unsupported("USB serial is macOS-only")
        #endif
    }

    func close() {
        #if os(macOS)
        if fd >= 0 {
            Darwin.close(fd)
            fd = -1
        }
        #endif
        pending = ""
    }

    func writeModel(_ model: String, thinking: String?, echoLine: Bool) throws {
        try write(line: SerialBridge.protocolLine(model: model), echoLine: echoLine)
        if let thinking, !thinking.isEmpty {
            try write(line: SerialBridge.thinkingLine(thinking), echoLine: echoLine)
        }
    }

    func write(line: String, echoLine: Bool) throws {
        let text = line.hasSuffix("\n") ? line : line + "\n"
        guard let port, !port.isEmpty else {
            fputs("serial dry-run (no --port)\n", stderr)
            if echoLine {
                print(text, terminator: "")
            }
            return
        }
        #if os(macOS)
        if fd < 0 {
            try open()
        }
        let bytes = Array(text.utf8)
        let written = bytes.withUnsafeBufferPointer { buffer in
            Darwin.write(fd, buffer.baseAddress, buffer.count)
        }
        guard written == bytes.count else {
            throw StubError.io("short write to \(port)")
        }
        fputs("serial wrote \(text.trimmingCharacters(in: .newlines)) to \(port)\n", stderr)
        #else
        throw StubError.unsupported("USB serial write is macOS-only")
        #endif
    }

    func readLines() -> [String] {
        #if os(macOS)
        guard fd >= 0 else { return [] }
        var chunk = [UInt8](repeating: 0, count: 256)
        while true {
            let n = Darwin.read(fd, &chunk, chunk.count)
            if n <= 0 { break }
            if let piece = String(bytes: chunk[0..<n], encoding: .utf8) {
                pending += piece
            }
        }
        var lines: [String] = []
        while let range = pending.range(of: "\n") {
            let line = String(pending[..<range.lowerBound]).trimmingCharacters(in: CharacterSet(charactersIn: "\r"))
            pending = String(pending[range.upperBound...])
            if !line.isEmpty {
                lines.append(line)
            }
        }
        return lines
        #else
        return []
        #endif
    }

    #if os(macOS)
    private func applyBaud(_ fd: Int32, baud: Int) {
        var term = termios()
        guard tcgetattr(fd, &term) == 0 else { return }
        let speed: speed_t
        switch baud {
        case 9600: speed = speed_t(B9600)
        case 57600: speed = speed_t(B57600)
        case 115200: speed = speed_t(B115200)
        case 230400: speed = speed_t(B230400)
        default: speed = speed_t(B115200)
        }
        cfmakeraw(&term)
        cfsetispeed(&term, speed)
        cfsetospeed(&term, speed)
        term.c_cflag |= tcflag_t(CLOCAL | CREAD)
        _ = tcsetattr(fd, TCSANOW, &term)
    }
    #endif
}

enum HIDBridge {
    static let chord = "Ctrl+Shift+M"
    private static let keyM: UInt16 = 0x2E
    private static let keyReturn: UInt16 = 0x24
    private static let keyEscape: UInt16 = 0x35

    static let summary = """
    HID path (Mac helper)
      chatgpt-bridge injects \(chord) to open ChatGPT's model picker,
      then types the target name and presses Return.
      Used when Accessibility cannot press a real control.

    Notes
      - Accessibility must be granted to the launching terminal.
      - Keep the ChatGPT window focused; the helper activates it first.
      - Confirm on device whether ChatGPT honors Control-Shift-M
        versus Command-Shift-M if the picker does not open.
    """

    static func printInfo() {
        print(summary)
    }

    static func openPickerAndChoose(_ text: String) -> Bool {
        #if os(macOS) && canImport(CoreGraphics)
        let flags: CGEventFlags = [.maskControl, .maskShift]
        guard postKey(keyM, flags: flags, down: true),
              postKey(keyM, flags: flags, down: false)
        else {
            return false
        }
        Thread.sleep(forTimeInterval: 0.35)
        guard typeText(text) else { return false }
        Thread.sleep(forTimeInterval: 0.12)
        guard postKey(keyReturn, flags: [], down: true),
              postKey(keyReturn, flags: [], down: false)
        else {
            return false
        }
        Thread.sleep(forTimeInterval: 0.2)
        _ = postKey(keyEscape, flags: [], down: true)
        _ = postKey(keyEscape, flags: [], down: false)
        return true
        #else
        fputs("chatgpt-bridge: HID inject is macOS-only\n", stderr)
        return false
        #endif
    }

    #if os(macOS) && canImport(CoreGraphics)
    private static func postKey(_ code: UInt16, flags: CGEventFlags, down: Bool) -> Bool {
        guard let event = CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: down) else {
            return false
        }
        event.flags = flags
        event.post(tap: .cghidEventTap)
        return true
    }

    private static func typeText(_ text: String) -> Bool {
        for scalar in text.unicodeScalars {
            var utf16 = Array(String(scalar).utf16)
            guard let down = CGEvent(keyboardEventSource: nil, virtualKey: 0, keyDown: true),
                  let up = CGEvent(keyboardEventSource: nil, virtualKey: 0, keyDown: false)
            else {
                return false
            }
            down.keyboardSetUnicodeString(stringLength: utf16.count, unicodeString: &utf16)
            up.keyboardSetUnicodeString(stringLength: utf16.count, unicodeString: &utf16)
            down.post(tap: .cghidEventTap)
            up.post(tap: .cghidEventTap)
        }
        return !text.isEmpty
    }
    #endif
}

enum StubError: Error, CustomStringConvertible {
    case io(String)
    case unsupported(String)

    var description: String {
        switch self {
        case .io(let message), .unsupported(let message):
            return message
        }
    }
}
