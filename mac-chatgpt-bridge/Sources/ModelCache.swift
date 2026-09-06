import Foundation

/// Persist last successful AX model string for serial/watch fallback.
enum ModelCache {
    private static let fileName = "last-model.txt"
    private static let folderName = "chatgpt-bridge"

    static var fileURL: URL {
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
        return base.appendingPathComponent(fileName, isDirectory: false)
    }

    static func load() -> String? {
        guard let raw = try? String(contentsOf: fileURL, encoding: .utf8) else {
            return nil
        }
        let line = raw
            .trimmingCharacters(in: .whitespacesAndNewlines)
        return line.isEmpty ? nil : line
    }

    static func save(_ model: String) {
        let trimmed = model.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        do {
            try trimmed.write(to: fileURL, atomically: true, encoding: .utf8)
        } catch {
            fputs("chatgpt-bridge: cache write failed: \(error)\n", stderr)
        }
    }
}
