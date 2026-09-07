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
    private var pending = Data()
    private var reconnectAt = Date.distantPast
    private var lastConnectionError: String?

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
        pending.removeAll()
    }

    func readLines() -> [String] {
        #if os(macOS)
        if fd < 0 {
            guard Date() >= reconnectAt else { return [] }
            do {
                try open()
                lastConnectionError = nil
                fputs("chatgpt-bridge: serial reconnected; listening for new SET lines\n", stderr)
            } catch {
                connectionFailed(String(describing: error))
                return []
            }
        }
        var readFailure: String?
        var chunk = [UInt8](repeating: 0, count: 256)
        while true {
            let n = Darwin.read(fd, &chunk, chunk.count)
            if n < 0 {
                let code = errno
                if code == EINTR { continue }
                if code != EAGAIN && code != EWOULDBLOCK {
                    readFailure = "serial read failed (errno \(code))"
                }
                break
            }
            if n == 0 {
                readFailure = "serial disconnected"
                break
            }
            pending.append(contentsOf: chunk[0..<n])
        }
        var lines: [String] = []
        while let newline = pending.firstIndex(of: 10) {
            let line = String(decoding: pending[..<newline], as: UTF8.self)
                .trimmingCharacters(in: CharacterSet(charactersIn: "\r"))
            pending.removeSubrange(...newline)
            if !line.isEmpty { lines.append(line) }
        }
        if let readFailure { connectionFailed(readFailure) }
        return lines
        #else
        return []
        #endif
    }

    private func connectionFailed(_ message: String) {
        close()
        reconnectAt = Date().addingTimeInterval(2)
        if message != lastConnectionError {
            fputs("chatgpt-bridge: \(message); retrying configured port every 2 seconds\n", stderr)
            lastConnectionError = message
        }
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
        withUnsafeMutableBytes(of: &term.c_cc) { controls in
            controls[Int(VMIN)] = 1
            controls[Int(VTIME)] = 0
        }
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
