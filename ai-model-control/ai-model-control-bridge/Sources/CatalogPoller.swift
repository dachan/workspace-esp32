#if os(macOS)
import Foundation

/// Reloads one app's model list off the main thread at a fixed interval.
/// waiting(until) → loading → waiting; `onChange` runs only for a new list,
/// and an unavailable source is reported once until it recovers.
final class CatalogPoller<Entry: Equatable> {
    private enum State {
        case waiting(until: TimeInterval)
        case loading
    }

    private let name: String
    private let interval: TimeInterval
    private let load: () -> [Entry]?
    private let onChange: ([Entry]) -> Void
    private var state = State.waiting(until: 0)
    private var current: [Entry]?
    private var reportedUnavailable = false

    init(
        name: String,
        interval: TimeInterval = 10,
        load: @escaping () -> [Entry]?,
        onChange: @escaping ([Entry]) -> Void
    ) {
        self.name = name
        self.interval = interval
        self.load = load
        self.onChange = onChange
    }

    /// Blocking first read, so the panel handshake starts with the live list.
    func loadNow() {
        if let entries = load() { accept(entries) }
        state = .waiting(until: ProcessInfo.processInfo.systemUptime + interval)
    }

    func poll() {
        guard case .waiting(let until) = state,
              ProcessInfo.processInfo.systemUptime >= until else { return }
        state = .loading
        let load = self.load
        DispatchQueue.global(qos: .utility).async { [weak self] in
            let entries = load()
            DispatchQueue.main.async {
                self?.finish(entries)
            }
        }
    }

    private func finish(_ entries: [Entry]?) {
        state = .waiting(until: ProcessInfo.processInfo.systemUptime + interval)
        guard let entries else {
            if !reportedUnavailable {
                fputs("ai-model-control-bridge: \(name) model list unavailable; retaining panel catalog\n", stderr)
            }
            reportedUnavailable = true
            return
        }
        reportedUnavailable = false
        accept(entries)
    }

    private func accept(_ entries: [Entry]) {
        guard entries != current else { return }
        current = entries
        onChange(entries)
    }
}
#endif
