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
    private let chatGPTThinkingMask: UInt64
    private let cursorModelMask: UInt64
    private let showOlderModels: Bool
    private let dialSwap: Bool
    private var rigModelMask: UInt64
    private var rigModelMaskSent: UInt64?
    private var rigEffortMasks: [UInt8] = []
    private var rigEffortMasksSent: [UInt8]?
    private var rigCatalogWanted: [String] = []
    private var rigCatalogIndex = 0
    private var rigCatalogSent = true
    private var fd: Int32 = -1
    private(set) var connectionGeneration: UInt64 = 0
    var isConnected: Bool { fd >= 0 }
    private var pending: [UInt8] = []
    private var discardLine = false
    private var reconnectAt: TimeInterval = 0
    private var lastConnectionError: String?
    private var syncPending = true
    private var nextTimeAt: TimeInterval = 0
    private var panelFrontWanted: String?
    private var panelFrontSent: String?
    private var hostModelWanted: String?
    private var hostThinkingWanted: String?
    private var hostEffortMaskWanted: UInt8?
    private var hostModelSent: String?
    private var hostThinkingSent: String?
    private var hostEffortMaskSent: UInt8?
    private var acknowledgements: [SettingKind: UInt64] = [:]
    private var outgoing: [UInt8] = []
    private var outgoingOffset = 0
    private var configurationStep = 0

    init(
        port: String,
        baud: Int,
        chatGPTThinkingMask: UInt64 = Catalog.defaultChatGPTThinkingMask,
        cursorModelMask: UInt64 = 0xFF,
        rigModelMask: UInt64 = 0xFFF,
        showOlderModels: Bool = false,
        dialSwap: Bool = false
    ) {
        self.port = port
        self.baud = baud
        self.chatGPTThinkingMask = chatGPTThinkingMask
        self.cursorModelMask = cursorModelMask
        self.rigModelMask = rigModelMask
        self.showOlderModels = showOlderModels
        self.dialSwap = dialSwap
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
            guard tcflush(opened, TCIFLUSH) == 0 else {
                throw SerialError.io("could not clear queued input from \(port) (errno \(errno))")
            }
        } catch {
            Darwin.close(opened)
            throw error
        }
        fd = opened
        connectionGeneration &+= 1
        requestSync()
        nextTimeAt = 0
        panelFrontSent = nil
        lastConnectionError = nil
        fputs("ai-model-control-bridge: serial connected; requesting current dial state\n", stderr)
        print("BRIDGE_STATUS SERIAL_CONNECTED")
        fflush(stdout)
    }

    func close() {
        if fd >= 0 {
            Darwin.close(fd)
            fd = -1
            connectionGeneration &+= 1
        }
        pending.removeAll(keepingCapacity: true)
        discardLine = false
        outgoing.removeAll(keepingCapacity: true)
        outgoingOffset = 0
        acknowledgements.removeAll()
        configurationStep = 0
        rigModelMaskSent = nil
        rigEffortMasksSent = nil
        rigCatalogIndex = 0
        rigCatalogSent = rigCatalogWanted.isEmpty
        hostModelSent = nil
        hostThinkingSent = nil
        hostEffortMaskSent = nil
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
                        fputs("ai-model-control-bridge: discard malformed or oversized serial line\n", stderr)
                    } else {
                        pending.append(byte)
                    }
                }
            }
        }
        // A disconnect invalidates even complete frames collected earlier in this poll.
        return isConnected ? lines : []
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
        configurationStep = 0
        rigModelMaskSent = nil
        rigEffortMasksSent = nil
        rigCatalogIndex = 0
        rigCatalogSent = rigCatalogWanted.isEmpty
        hostModelSent = nil
        hostThinkingSent = nil
        hostEffortMaskSent = nil
    }

    func setPanelFront(_ title: String) {
        panelFrontWanted = title
    }

    func setRigModelMask(_ mask: UInt64) {
        let next = mask == 0 ? 1 : mask
        if next != rigModelMask {
            rigModelMask = next
            rigModelMaskSent = nil
        }
    }

    func setRigEffortMasks(_ masks: [UInt8]) {
        if masks != rigEffortMasks {
            rigEffortMasks = masks
            rigEffortMasksSent = nil
        }
    }

    func setRigCatalog(_ entries: [(name: String, mask: UInt8)]) {
        var lines = ["CONFIG RIG_CLEAR"]
        for entry in entries {
            let name = String(entry.name.trimmingCharacters(in: .whitespacesAndNewlines).prefix(79))
            guard !name.isEmpty else { continue }
            lines.append("CONFIG RIG_ADD \(String(format: "%02x", entry.mask)) \(name)")
        }
        lines.append("CONFIG RIG_END")
        if lines != rigCatalogWanted {
            rigCatalogWanted = lines
            rigCatalogIndex = 0
            rigCatalogSent = false
        }
    }

    func setHostPanel(model: String, thinking: String, effortMask: UInt8? = nil) {
        hostModelWanted = model
        hostThinkingWanted = thinking
        if hostEffortMaskWanted != effortMask {
            hostEffortMaskWanted = effortMask
            hostEffortMaskSent = nil
        }
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
                    hostModelSent = nil
                    hostThinkingSent = nil
                    hostEffortMaskSent = nil
                    if wanted != "Rig" {
                        hostModelWanted = nil
                        hostThinkingWanted = nil
                        hostEffortMaskWanted = nil
                    }
                } else if syncPending {
                    // READY retries recover a reset without periodic state polling.
                    line = "SYNC"
                    syncPending = false
                    panelFrontSent = nil
                } else if configurationStep == 0 {
                    line = "CONFIG CHATGPT_EFFORTS \(String(format: "%016llx", chatGPTThinkingMask))"
                    configurationStep = 1
                } else if configurationStep == 1 {
                    line = "CONFIG CURSOR_MODELS \(String(format: "%016llx", cursorModelMask))"
                    configurationStep = 2
                } else if configurationStep == 2 {
                    line = "CONFIG DIAL_SWAP \(dialSwap ? 1 : 0)\nCONFIG CHATGPT_OLDER \(showOlderModels ? 1 : 0)"
                    configurationStep = 3
                } else if !rigCatalogSent, rigCatalogIndex < rigCatalogWanted.count {
                    line = rigCatalogWanted[rigCatalogIndex]
                    rigCatalogIndex += 1
                    if rigCatalogIndex >= rigCatalogWanted.count {
                        rigCatalogSent = true
                    }
                    configurationStep = 4
                } else if rigCatalogWanted.isEmpty, configurationStep == 3 || rigModelMaskSent != rigModelMask {
                    line = "CONFIG RIG_MODELS \(String(format: "%016llx", rigModelMask))"
                    configurationStep = 4
                    rigModelMaskSent = rigModelMask
                } else if rigCatalogWanted.isEmpty, !rigEffortMasks.isEmpty, configurationStep == 4 || rigEffortMasksSent != rigEffortMasks {
                    line = "CONFIG RIG_EFFORTS \(rigEffortMasks.map { String(format: "%02x", $0) }.joined())"
                    configurationStep = 5
                    rigEffortMasksSent = rigEffortMasks
                } else if let model = hostModelWanted, model != hostModelSent, panelFrontSent == "Rig" {
                    line = "MODEL \(model)"
                    hostModelSent = model
                    hostEffortMaskSent = nil
                } else if let mask = hostEffortMaskWanted, mask != hostEffortMaskSent, panelFrontSent == "Rig" {
                    line = "CONFIG RIG_HOST_EFFORTS \(String(format: "%02x", mask)) \(hostModelWanted ?? "")"
                    hostEffortMaskSent = mask
                } else if let thinking = hostThinkingWanted, thinking != hostThinkingSent, panelFrontSent == "Rig" {
                    line = "THINKING \(thinking)"
                    hostThinkingSent = thinking
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
            fputs("ai-model-control-bridge: \(message); retrying configured port every 2 seconds\n", stderr)
            lastConnectionError = message
            print("BRIDGE_STATUS SERIAL_UNAVAILABLE")
            fflush(stdout)
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
