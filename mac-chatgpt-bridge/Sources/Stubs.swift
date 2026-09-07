import Foundation
#if canImport(CoreGraphics)
import CoreGraphics
#endif
#if canImport(Darwin)
import Darwin
#endif

enum SerialBridge {
    static let defaultBaud = 115_200

    static func protocolLine(model: String) -> String {
        "MODEL \(model)\n"
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

    static func send(model: String, port: String?, baud: Int, echoLine: Bool) throws {
        let line = protocolLine(model: model)
        guard let port, !port.isEmpty else {
            fputs("serial dry-run (no --port); baud=\(baud)\n", stderr)
            if echoLine {
                print(line, terminator: "")
            }
            return
        }
        #if os(macOS)
        try write(port: port, line: line, baud: baud)
        fputs("serial wrote \(line.trimmingCharacters(in: .newlines)) to \(port)\n", stderr)
        #else
        throw StubError.unsupported("USB serial write is macOS-only")
        #endif
    }

    #if os(macOS)
    private static func write(port: String, line: String, baud: Int) throws {
        let fd = open(port, O_WRONLY | O_NOCTTY)
        guard fd >= 0 else {
            throw StubError.io("could not open \(port) (errno \(errno))")
        }
        defer { close(fd) }
        applyBaud(fd, baud: baud)
        let bytes = Array(line.utf8)
        let written = bytes.withUnsafeBufferPointer { buffer in
            Darwin.write(fd, buffer.baseAddress, buffer.count)
        }
        guard written == bytes.count else {
            throw StubError.io("short write to \(port)")
        }
    }


    /// Open serial RDWR for watch+listen. Caller owns the fd (close when done).
    static func openPort(_ port: String, baud: Int) throws -> Int32 {
        let fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard fd >= 0 else {
            throw StubError.io("could not open \(port) (errno \(errno))")
        }
        applyBaud(fd, baud: baud)
        // Drop NONBLOCK after configure for simpler read loops (poll with select).
        let flags = fcntl(fd, F_GETFL)
        if flags >= 0 {
            _ = fcntl(fd, F_SETFL, flags & ~O_NONBLOCK)
        }
        return fd
    }

    /// Non-blocking-ish read of available bytes; returns empty if none.
    static func readAvailable(_ fd: Int32, max: Int = 512) -> String {
        var buf = [UInt8](repeating: 0, count: max)
        let n = read(fd, &buf, buf.count)
        guard n > 0 else { return "" }
        return String(bytes: buf[0..<n], encoding: .utf8) ?? ""
    }

    private static func applyBaud(_ fd: Int32, baud: Int) {
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
        _ = tcsetattr(fd, TCSANOW, &term)
    }
    #endif
}

enum HIDBridge {
    static let chord = "Ctrl+Shift+M"

    static let summary = """
    HID / CGEvent path (macOS bridge)
      Bridge posts \(chord) to open ChatGPT's model picker, then types
      or clicks the target model / thinking label from encoder SET lines.

    Notes
      - Requires Accessibility (and Input Monitoring on newer macOS) for the
        Terminal / process that launches chatgpt-bridge.
      - Keep ChatGPT focused; desk watch mode listens for SET MODEL / SET THINKING.
    """

    static func printInfo() {
        print(summary)
    }

#if os(macOS)
    static func postChordControlShiftM() {
        postKey(keyCode: 46, flags: [.maskControl, .maskShift]) // M
    }

    static func postKey(keyCode: CGKeyCode, flags: CGEventFlags = []) {
        guard let src = CGEventSource(stateID: .hidSystemState),
              let down = CGEvent(keyboardEventSource: src, virtualKey: keyCode, keyDown: true),
              let up = CGEvent(keyboardEventSource: src, virtualKey: keyCode, keyDown: false)
        else { return }
        down.flags = flags
        up.flags = flags
        down.post(tap: .cghidEventTap)
        up.post(tap: .cghidEventTap)
    }

    static func typeText(_ text: String) {
        guard let src = CGEventSource(stateID: .hidSystemState) else { return }
        for ch in text.utf16 {
            var chars = [UniChar(ch)]
            guard let down = CGEvent(keyboardEventSource: src, virtualKey: 0, keyDown: true),
                  let up = CGEvent(keyboardEventSource: src, virtualKey: 0, keyDown: false)
            else { continue }
            down.keyboardSetUnicodeString(stringLength: 1, unicodeString: &chars)
            up.keyboardSetUnicodeString(stringLength: 1, unicodeString: &chars)
            down.post(tap: .cghidEventTap)
            up.post(tap: .cghidEventTap)
        }
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
