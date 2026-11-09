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
    private var chatGPTCatalog = CatalogQueue()
    private var cursorCatalog = CatalogQueue()
    private var rigCatalog = CatalogQueue()
    private var claudeCatalog = CatalogQueue()
    /// Port lifecycle: closed (reopen once `retryAt` passes) ↔ open.
    private enum Link {
        case closed(retryAt: TimeInterval)
        case open(Int32)
    }

    /// Per-connection handshake. SYNC goes first so the panel switches from
    /// legacy SET to acknowledged STATE before a FRONT can change its fields,
    /// then the fixed CONFIG lines, then catalogs and host state.
    private enum Handshake: Equatable {
        case needSync
        case config(Int)
        case ready
    }

    private var link = Link.closed(retryAt: 0)
    private var fd: Int32? {
        if case .open(let fd) = link { return fd }
        return nil
    }
    private(set) var connectionGeneration: UInt64 = 0
    var isConnected: Bool { fd != nil }
    private var pending: [UInt8] = []
    private var discardLine = false
    private var lastConnectionError: String?
    private var handshake = Handshake.needSync
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
    /// One hardware reset on the first successful open, then normal operation.
    private var resetPending: Bool

    init(
        port: String,
        baud: Int,
        chatGPTThinkingMask: UInt64 = Catalog.defaultChatGPTThinkingMask,
        cursorModelMask: UInt64 = 0xFF,
        rigModelMask: UInt64 = 0xFFF,
        showOlderModels: Bool = false,
        dialSwap: Bool = false,
        resetOnFirstOpen: Bool = false
    ) {
        resetPending = resetOnFirstOpen
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
        if resetPending {
            resetPending = false
            pulseReset(opened)
            Darwin.close(opened)
            // The chip drops off USB while it reboots; reopen once it is back.
            link = .closed(retryAt: ProcessInfo.processInfo.systemUptime + 1.5)
            fputs("ai-model-control-bridge: reset the panel; reconnecting after it boots\n", stderr)
            return
        }
        link = .open(opened)
        connectionGeneration &+= 1
        requestSync()
        lastConnectionError = nil
        fputs("ai-model-control-bridge: serial connected; requesting current dial state\n", stderr)
        BridgeStatus.serialConnected.emit()
    }

    /// open → closed(retryAt). Everything sent on the old link is resent after reopen.
    func close(retryAt: TimeInterval = 0) {
        if let fd {
            Darwin.close(fd)
            connectionGeneration &+= 1
        }
        link = .closed(retryAt: retryAt)
        pending.removeAll(keepingCapacity: true)
        discardLine = false
        outgoing.removeAll(keepingCapacity: true)
        outgoingOffset = 0
        acknowledgements.removeAll()
        requestSync()
    }

    func readLines() -> [String] {
        if case .closed(let retryAt) = link {
            guard ProcessInfo.processInfo.systemUptime >= retryAt else { return [] }
            do {
                try open()
            } catch {
                connectionFailed(String(describing: error))
                return []
            }
        }
        guard let fd else { return [] }
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

    /// Any state → needSync: the whole configuration is resent after SYNC.
    func requestSync() {
        handshake = .needSync
        panelFrontSent = nil
        nextTimeAt = 0
        rigModelMaskSent = nil
        rigEffortMasksSent = nil
        chatGPTCatalog.restart()
        cursorCatalog.restart()
        rigCatalog.restart()
        claudeCatalog.restart()
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

    func setChatGPTCatalog(_ entries: [CodexModelList.Entry]) {
        var lines = ["CONFIG CHATGPT_CLEAR"]
        for entry in entries {
            lines.append("CONFIG CHATGPT_ADD \(String(format: "%02x", entry.effortMask)) \(entry.name)")
        }
        lines.append("CONFIG CHATGPT_END")
        chatGPTCatalog.replace(lines)
    }

    func setCursorCatalog(_ entries: [CursorModelList.Entry]) {
        var lines = ["CONFIG CURSOR_CLEAR"]
        for entry in entries {
            lines.append("CONFIG CURSOR_ADD \(String(format: "%02x", entry.effortMask)) \(entry.name)")
        }
        lines.append("CONFIG CURSOR_END")
        cursorCatalog.replace(lines)
    }

    /// Claude model rows read from its own menu; names only (efforts are uniform).
    func setClaudeCatalog(_ names: [String]) {
        let entries = names.map { String($0.prefix(31)) }.filter { !$0.isEmpty }
        guard !entries.isEmpty else { return }
        claudeCatalog.replace(
            ["CONFIG CLAUDE_CLEAR"] + entries.map { "CONFIG CLAUDE_ADD \($0)" } + ["CONFIG CLAUDE_END"]
        )
    }

    func setRigCatalog(_ entries: [(name: String, mask: UInt8)]) {
        var lines = ["CONFIG RIG_CLEAR"]
        for entry in entries {
            let name = String(entry.name.trimmingCharacters(in: .whitespacesAndNewlines).prefix(79))
            guard !name.isEmpty else { continue }
            lines.append("CONFIG RIG_ADD \(String(format: "%02x", entry.mask)) \(name)")
        }
        lines.append("CONFIG RIG_END")
        rigCatalog.replace(lines)
    }

    func clearHostPanel() {
        hostModelWanted = nil
        hostThinkingWanted = nil
        hostEffortMaskWanted = nil
        hostModelSent = nil
        hostThinkingSent = nil
        hostEffortMaskSent = nil
    }

    func setHostPanel(model: String, thinking: String, effortMask: UInt8? = nil) {
        hostModelWanted = model
        hostThinkingWanted = thinking
        if hostEffortMaskWanted != effortMask {
            hostEffortMaskWanted = effortMask
            hostEffortMaskSent = nil
        }
    }

    private var configLines: [String] {
        [
            "CONFIG CHATGPT_EFFORTS \(String(format: "%016llx", chatGPTThinkingMask))",
            "CONFIG CURSOR_MODELS \(String(format: "%016llx", cursorModelMask))",
            "CONFIG DIAL_SWAP \(dialSwap ? 1 : 0)\nCONFIG CHATGPT_OLDER \(showOlderModels ? 1 : 0)",
        ]
    }

    /// The FRONT line to send now, if any, recording it as sent.
    private func nextFront() -> String? {
        guard handshake != .needSync, let wanted = panelFrontWanted, wanted != panelFrontSent else { return nil }
        // The panel clamps Cursor's saved model against its catalog, so send that first.
        guard wanted != "Cursor" || cursorCatalog.isSent else { return nil }
        panelFrontSent = wanted
        hostModelSent = nil
        hostThinkingSent = nil
        hostEffortMaskSent = nil
        if wanted != "Rig" {
            clearHostPanel()
        }
        return wanted
    }

    /// One line per pass, highest priority first: ACK, SYNC, FRONT, handshake
    /// CONFIG, catalogs, Rig masks, Rig host state, then the TIME refresh.
    private func nextLine() -> String? {
        if let kind = SettingKind.allCases.first(where: { acknowledgements[$0] != nil }),
           let revision = acknowledgements.removeValue(forKey: kind) {
            return "ACK \(String(format: "%016llx", revision)) \(kind.rawValue)"
        }
        if handshake == .needSync {
            // READY retries recover a reset without periodic state polling.
            handshake = .config(0)
            return "SYNC"
        }
        if let front = nextFront() {
            return "FRONT \(front)"
        }
        if case .config(let index) = handshake {
            let lines = configLines
            handshake = index + 1 < lines.count ? .config(index + 1) : .ready
            return lines[index]
        }
        if let next = chatGPTCatalog.next() ?? cursorCatalog.next() ?? claudeCatalog.next() ?? rigCatalog.next() {
            return next
        }
        // A Rig catalog, once received, carries enablement itself and supersedes the masks.
        if rigCatalog.isEmpty, rigModelMaskSent != rigModelMask {
            rigModelMaskSent = rigModelMask
            return "CONFIG RIG_MODELS \(String(format: "%016llx", rigModelMask))"
        }
        if rigCatalog.isEmpty, !rigEffortMasks.isEmpty, rigEffortMasksSent != rigEffortMasks {
            rigEffortMasksSent = rigEffortMasks
            return "CONFIG RIG_EFFORTS \(rigEffortMasks.map { String(format: "%02x", $0) }.joined())"
        }
        if panelFrontSent == "Rig" {
            if let model = hostModelWanted, model != hostModelSent {
                hostModelSent = model
                hostEffortMaskSent = nil
                return "MODEL \(model)"
            }
            if let mask = hostEffortMaskWanted, mask != hostEffortMaskSent {
                hostEffortMaskSent = mask
                return "CONFIG RIG_HOST_EFFORTS \(String(format: "%02x", mask)) \(hostModelWanted ?? "")"
            }
            if let thinking = hostThinkingWanted, thinking != hostThinkingSent {
                hostThinkingSent = thinking
                return "THINKING \(thinking)"
            }
        }
        if ProcessInfo.processInfo.systemUptime >= nextTimeAt {
            nextTimeAt = ProcessInfo.processInfo.systemUptime + 30
            let unix = Int64(Date().timeIntervalSince1970)
            let tzMin = TimeZone.current.secondsFromGMT() / 60
            return "TIME \(unix) \(tzMin)"
        }
        return nil
    }

    func flushWrites() {
        guard let fd else { return }
        for _ in 0..<4 {
            if outgoing.isEmpty {
                guard let line = nextLine() else { break }
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
        close(retryAt: ProcessInfo.processInfo.systemUptime + 2)
        if message != lastConnectionError {
            fputs("ai-model-control-bridge: \(message); retrying configured port every 2 seconds\n", stderr)
            lastConnectionError = message
            BridgeStatus.serialUnavailable.emit()
        }
    }

    /// esptool's hard reset: RTS asserted with DTR released holds EN low, on
    /// USB Serial/JTAG and on auto-reset USB-UART boards alike. DTR is released
    /// first because DTR held during release would boot download mode instead.
    private func pulseReset(_ fd: Int32) {
        var dtr = TIOCM_DTR
        var rts = TIOCM_RTS
        _ = ioctl(fd, TIOCMBIC, &dtr)
        _ = ioctl(fd, TIOCMBIS, &rts)
        usleep(200_000)
        _ = ioctl(fd, TIOCMBIC, &rts)
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
/// One ordered CONFIG transaction, resent whole after every reconnect or SYNC.
struct CatalogQueue {
    private var wanted: [String] = []
    private var index = 0
    private(set) var isSent = true

    var isEmpty: Bool { wanted.isEmpty }

    mutating func replace(_ lines: [String]) {
        guard lines != wanted else { return }
        wanted = lines
        index = 0
        isSent = false
    }

    mutating func restart() {
        index = 0
        isSent = wanted.isEmpty
    }

    mutating func next() -> String? {
        guard !isSent, index < wanted.count else { return nil }
        let line = wanted[index]
        index += 1
        if index >= wanted.count { isSent = true }
        return line
    }
}
#endif
