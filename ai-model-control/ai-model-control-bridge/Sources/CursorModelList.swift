#if os(macOS)
import Foundation

/// Cursor's picker data is private local state. Keep the last good catalog when it is unavailable.
enum CursorModelList {
    struct Entry: Equatable {
        let name: String
        let effortMask: UInt8
    }

    private static let effortNames = ["None", "Minimal", "Low", "Medium", "High", "Extra High", "Max"]
    private static let database = NSHomeDirectory() + "/Library/Application Support/Cursor/User/globalStorage/state.vscdb"

    static func load() -> [Entry]? {
        guard FileManager.default.fileExists(atPath: database) else { return nil }
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/sqlite3")
        process.arguments = ["-readonly", database, """
            SELECT json_object(
              'catalog', json_extract(value, '$.availableDefaultModels2'),
              'enabled', json_extract(value, '$.aiSettings.modelOverrideEnabled'),
              'disabled', json_extract(value, '$.aiSettings.modelOverrideDisabled'))
            FROM ItemTable
            WHERE key='src.vs.platform.reactivestorage.browser.reactiveStorageServiceImpl.persistentStorage.applicationUser';
            """]
        let outputURL = FileManager.default.temporaryDirectory
            .appendingPathComponent("model-dial-cursor-\(UUID().uuidString).json")
        guard FileManager.default.createFile(atPath: outputURL.path, contents: nil,
                                            attributes: [.posixPermissions: 0o600]) else { return nil }
        defer { try? FileManager.default.removeItem(at: outputURL) }
        do {
            let output = try FileHandle(forWritingTo: outputURL)
            defer { try? output.close() }
            process.standardOutput = output
            process.standardError = FileHandle.nullDevice
            try process.run()
            let timeout = DispatchWorkItem {
                if process.isRunning { kill(process.processIdentifier, SIGKILL) }
            }
            DispatchQueue.global().asyncAfter(deadline: .now() + 3, execute: timeout)
            defer { timeout.cancel() }
            process.waitUntilExit()
            guard process.terminationStatus == 0 else { return nil }
            let attributes = try FileManager.default.attributesOfItem(atPath: outputURL.path)
            guard let size = attributes[.size] as? NSNumber, size.intValue <= 2 * 1024 * 1024,
                  let object = try JSONSerialization.jsonObject(with: Data(contentsOf: outputURL)) as? [String: Any],
                  let catalog = object["catalog"] as? [[String: Any]],
                  let enabled = object["enabled"] as? [String],
                  let disabled = object["disabled"] as? [String] else { return nil }
            let defaults = catalog.compactMap { model -> String? in
                model["defaultOn"] as? Bool == true ? model["name"] as? String : nil
            }
            let enabledIDs = Set(defaults).union(enabled).subtracting(disabled)
            var entries: [Entry] = []
            var names = Set<String>()
            for model in catalog where entries.count < 48 {
                guard let id = model["name"] as? String, enabledIDs.contains(id),
                      let raw = model["clientDisplayName"] as? String else { continue }
                let name = raw.trimmingCharacters(in: .whitespacesAndNewlines)
                guard !name.isEmpty, name.utf8.count < 64,
                      name.rangeOfCharacter(from: .controlCharacters) == nil,
                      names.insert(name.lowercased()).inserted else { continue }
                entries.append(Entry(name: name, effortMask: effortMask(model)))
            }
            return entries.first?.name == "Auto" ? entries : nil
        } catch {
            return nil
        }
    }

    private static func effortMask(_ model: [String: Any]) -> UInt8 {
        guard let definitions = model["parameterDefinitions"] as? [[String: Any]],
              let effort = definitions.first(where: {
                  guard let id = $0["id"] as? String else { return false }
                  return ["effort", "reasoning", "reasoning_effort"].contains(id)
              }),
              let parameter = effort["parameterType"] as? [String: Any],
              let choice = parameter["enumParameter"] as? [String: Any],
              let values = choice["values"] as? [[String: Any]] else { return 0 }
        var mask: UInt8 = 0
        for value in values {
            guard let label = value["displayName"] as? String,
                  let index = effortNames.firstIndex(where: { $0.caseInsensitiveCompare(label) == .orderedSame }) else { continue }
            mask |= 1 << index
        }
        return mask
    }
}
#endif
