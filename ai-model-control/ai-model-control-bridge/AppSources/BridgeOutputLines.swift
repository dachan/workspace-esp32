#if os(macOS)
import Foundation

/// Pipe reads can split UTF-8 and lines. Only complete records reach the log/UI.
@MainActor
final class BridgeOutputLines {
    private var pending = Data()
    private var discarding = false
    private let received: @MainActor @Sendable (String) -> Void

    init(received: @escaping @MainActor @Sendable (String) -> Void) {
        self.received = received
    }

    func receive(_ data: Data) {
        if data.isEmpty {
            if !pending.isEmpty { emit() }
            return
        }
        for byte in data {
            if byte == 10 {
                if !discarding { emit() }
                pending.removeAll(keepingCapacity: true)
                discarding = false
            } else if !discarding {
                if pending.count >= 8192 {
                    pending.removeAll(keepingCapacity: true)
                    discarding = true
                    received("Bridge output line exceeded 8192 bytes; discarded")
                } else {
                    pending.append(byte)
                }
            }
        }
    }

    private func emit() {
        let text = String(decoding: pending, as: UTF8.self)
        pending.removeAll(keepingCapacity: true)
        if !text.isEmpty { received(text) }
    }
}
#endif
