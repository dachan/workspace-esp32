import Foundation

/// Persist last successful model/thinking for serial/watch fallback.
enum ModelCache {
    private static let folderName = "chatgpt-bridge"

    static var folderURL: URL {
        let fm = FileManager.default
        let base: URL
        if let appSupport = try? fm.url(
            for: .applicationSupportDirectory,
            in: .userDomainMask,
            appropriateFor: nil,
            create: true
        ) {
            base = appSupport.appendingPathComponent(folderName, isDirectory: true)
        } else {
            base = fm.homeDirectoryForCurrentUser
                .appendingPathComponent(".cache", isDirectory: true)
                .appendingPathComponent(folderName, isDirectory: true)
        }
        try? fm.createDirectory(at: base, withIntermediateDirectories: true)
        return base
    }

    static var fileURL: URL {
        folderURL.appendingPathComponent("last-model.txt", isDirectory: false)
    }

    static var thinkingURL: URL {
        folderURL.appendingPathComponent("last-thinking.txt", isDirectory: false)
    }

    static func load() -> String? { read(fileURL) }

    static func loadThinking() -> String? { read(thinkingURL) }

    static func save(_ model: String) {
        write(model, to: fileURL)
    }

    static func saveThinking(_ level: String) {
        write(level, to: thinkingURL)
    }

    private static func read(_ url: URL) -> String? {
        guard let raw = try? String(contentsOf: url, encoding: .utf8) else {
            return nil
        }
        let line = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        return line.isEmpty ? nil : line
    }

    private static func write(_ value: String, to url: URL) {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        do {
            try trimmed.write(to: url, atomically: true, encoding: .utf8)
        } catch {
            fputs("chatgpt-bridge: cache write failed: \(error)\n", stderr)
        }
    }
}
