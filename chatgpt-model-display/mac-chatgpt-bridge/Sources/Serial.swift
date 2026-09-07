import Foundation
#if canImport(Darwin)
import Darwin
#endif

enum SerialLine {
    case setModel(String)
    case setThinking(String)
    case ignored
}

enum SerialBridge {
    static let defaultBaud = 115_200

    static func parseInbound(_ raw: String) -> SerialLine {
        let line = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !line.isEmpty else { return .ignored }
        if line.hasPrefix("SET MODEL ") || line.hasPrefix("SET MODEL\t") {
            let value = String(line.dropFirst(10)).trimmingCharacters(in: .whitespacesAndNewlines)
            return value.isEmpty ? .ignored : .setModel(value)
        }
        if line.hasPrefix("SET THINKING ") || line.hasPrefix("SET THINKING\t") {
            let value = String(line.dropFirst(13)).trimmingCharacters(in: .whitespacesAndNewlines)
            return value.isEmpty ? .ignored : .setThinking(value)
        }
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

    func open() throws {
        guard let port, !port.isEmpty else { return }
        #if os(macOS)
        if fd >= 0 { return }
        let opened = Darwin.open(port, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard opened >= 0 else {
            throw SerialError.io("could not open \(port) (errno \(errno))")
        }
        fd = opened
        applyBaud(fd, baud: baud)
        #else
        throw SerialError.unsupported("USB serial is macOS-only")
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
            let line = String(pending[..<range.lowerBound])
                .trimmingCharacters(in: CharacterSet(charactersIn: "\r"))
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

enum SerialError: Error, CustomStringConvertible {
    case io(String)
    case unsupported(String)

    var description: String {
        switch self {
        case .io(let message), .unsupported(let message):
            return message
        }
    }
}
