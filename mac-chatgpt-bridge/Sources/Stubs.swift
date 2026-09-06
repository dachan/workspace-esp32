import Foundation
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
    HID path (firmware later, not this helper)
      ESP32-S3 presents as a USB keyboard and sends \(chord)
      to open ChatGPT's model picker. This CLI never injects keys.

    Notes
      - Phase 1 only reads the current model via Accessibility.
      - Confirm on device whether ChatGPT honors Control-Shift-M
        versus Command-Shift-M; firmware can remap if needed.
      - Keep the ChatGPT window focused when the HID chord fires.
    """

    static func printInfo() {
        print(summary)
    }
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
