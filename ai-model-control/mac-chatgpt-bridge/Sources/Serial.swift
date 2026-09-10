import Foundation
#if os(macOS)
import Darwin

final class SerialSession {
    private static let baudRates: [Int: speed_t] = [
        9600: speed_t(B9600), 57600: speed_t(B57600),
        115200: speed_t(B115200), 230400: speed_t(B230400),
    ]
    static var supportedBauds: [Int] { baudRates.keys.sorted() }

    let port: String
    let baud: Int
    private var fd: Int32 = -1
    private var pending: [UInt8] = []
    private var discardLine = false
    private var reconnectAt: TimeInterval = 0
    private var lastConnectionError: String?
    private var syncPending = true
    private var nextTimeAt: TimeInterval = 0
    private var panelFrontWanted: String?
    private var panelFrontSent: String?
    private var acknowledgements: [SettingKind: UInt64] = [:]
    private var outgoing: [UInt8] = []
    private var outgoingOffset = 0

    init(port: String, baud: Int) {
        self.port = port
        self.baud = baud
    }

    private func open() throws {
        let opened = Darwin.open(port, O_RDWR | O_NOCTTY | O_NONBLOCK)
        guard opened >= 0 else {
            throw SerialError.io("could not open \(port) (errno \(errno))")
        }
        do {
            guard ioctl(opened, TIOCEXCL) == 0 else {
                throw SerialError.io("could not exclusively claim \(port) (errno \(errno))")
            }
            try configure(opened)
        } catch {
            Darwin.close(opened)
            throw error
        }
        fd = opened
        requestSync()
        nextTimeAt = 0
        panelFrontSent = nil
        lastConnectionError = nil
        fputs("chatgpt-bridge: serial connected; requesting current dial state\n", stderr)
    }

    func close() {
        if fd >= 0 {
            Darwin.close(fd)
            fd = -1
        }
        pending.removeAll(keepingCapacity: true)
        discardLine = false
        outgoing.removeAll(keepingCapacity: true)
        outgoingOffset = 0
        acknowledgements.removeAll()
    }

    func readLines() -> [String] {
        if fd < 0 {
            guard ProcessInfo.processInfo.systemUptime >= reconnectAt else { return [] }
            do {
                try open()
            } catch {
                connectionFailed(String(describing: error))
                return []
            }
        }
        var lines: [String] = []
        var chunk = [UInt8](repeating: 0, count: 256)
        // Bound memory and work per poll; continuous input must not starve focus checks.
        for _ in 0..<16 {
            let n = Darwin.read(fd, &chunk, chunk.count)
            if n < 0 {
                if errno == EINTR { continue }
                if errno != EAGAIN && errno != EWOULDBLOCK {
                    connectionFailed("serial read failed (errno \(errno))")
                }
                break
            }
            if n == 0 {
                connectionFailed("serial disconnected")
                break
            }
            for byte in chunk.prefix(n) {
                if byte == 10 {
                    if !discardLine, !pending.isEmpty, let line = String(bytes: pending, encoding: .utf8) {
                        lines.append(line)
                    }
                    pending.removeAll(keepingCapacity: true)
                    discardLine = false
                } else if byte != 13 && !discardLine {
                    if byte == 0 || pending.count >= SerialBridge.maxLineBytes {
                        pending.removeAll(keepingCapacity: true)
                        discardLine = true
                        fputs("chatgpt-bridge: discard malformed or oversized serial line\n", stderr)
                    } else {
                        pending.append(byte)
                    }
                }
            }
        }
        return lines
    }

    // Called only after the value was validated and accepted into the runtime queue.
    func acknowledge(_ update: SerialUpdate) {
        if let revision = update.revision {
            acknowledgements[update.kind] = revision
        }
    }

    func requestSync() {
        syncPending = true
        panelFrontSent = nil
        nextTimeAt = 0
    }

    func setPanelFront(_ title: String) {
        panelFrontWanted = title
    }

    func flushWrites() {
        guard fd >= 0 else { return }
        for _ in 0..<4 {
            if outgoing.isEmpty {
                let line: String
                if let kind = SettingKind.allCases.first(where: { acknowledgements[$0] != nil }),
                   let revision = acknowledgements.removeValue(forKey: kind) {
                    line = "ACK \(String(format: "%016llx", revision)) \(kind.rawValue)"
                } else if let wanted = panelFrontWanted, wanted != panelFrontSent {
                    line = "FRONT \(wanted)"
                    panelFrontSent = wanted
                } else if syncPending {
                    // READY retries recover a reset without periodic state polling.
                    line = "SYNC"
                    syncPending = false
                    panelFrontSent = nil
                } else if ProcessInfo.processInfo.systemUptime >= nextTimeAt {
                    let unix = Int64(Date().timeIntervalSince1970)
                    let tzMin = TimeZone.current.secondsFromGMT() / 60
                    line = "TIME \(unix) \(tzMin)"
                    nextTimeAt = ProcessInfo.processInfo.systemUptime + 30
                } else {
                    break
                }
                outgoing = Array("\n\(line)\n".utf8)
                outgoingOffset = 0
            }
            let n = outgoing.withUnsafeBytes { bytes in
                Darwin.write(fd, bytes.baseAddress!.advanced(by: outgoingOffset), bytes.count - outgoingOffset)
            }
            if n < 0 {
                if errno == EINTR { continue }
                if errno != EAGAIN && errno != EWOULDBLOCK {
                    connectionFailed("serial write failed (errno \(errno))")
                }
                break
            }
            if n == 0 { break }
            outgoingOffset += n
            if outgoingOffset == outgoing.count {
                outgoing.removeAll(keepingCapacity: true)
            }
        }
    }

    private func connectionFailed(_ message: String) {
        close()
        reconnectAt = ProcessInfo.processInfo.systemUptime + 2
        if message != lastConnectionError {
            fputs("chatgpt-bridge: \(message); retrying configured port every 2 seconds\n", stderr)
            lastConnectionError = message
        }
    }

    private func configure(_ fd: Int32) throws {
        guard let speed = Self.baudRates[baud] else {
            throw SerialError.io("unsupported baud \(baud)")
        }
        var term = termios()
        guard tcgetattr(fd, &term) == 0 else {
            throw SerialError.io("could not read serial configuration (errno \(errno))")
        }
        cfmakeraw(&term)
        withUnsafeMutableBytes(of: &term.c_cc) { controls in
            controls[Int(VMIN)] = 1
            controls[Int(VTIME)] = 0
        }
        term.c_cflag |= tcflag_t(CLOCAL | CREAD)
        guard cfsetispeed(&term, speed) == 0, cfsetospeed(&term, speed) == 0,
              tcsetattr(fd, TCSANOW, &term) == 0 else {
            throw SerialError.io("could not configure serial port (errno \(errno))")
        }
    }

    deinit { close() }
}

enum SerialError: Error, CustomStringConvertible {
    case io(String)
    var description: String {
        switch self { case .io(let message): return message }
    }
}
#endif
