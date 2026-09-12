#if os(macOS)
import Foundation

/// Read only the model catalog and overrides, off the menu's main thread.
enum CursorModelSync {
    enum ReadError: LocalizedError {
        case unavailable
        var errorDescription: String? { "Cursor model selection unavailable or read timed out" }
    }

    static func readMask() -> Result<UInt64, Error> {
        let database = NSHomeDirectory() + "/Library/Application Support/Cursor/User/globalStorage/state.vscdb"
        guard FileManager.default.fileExists(atPath: database) else { return .failure(ReadError.unavailable) }
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
            .appendingPathComponent("model-dial-catalog-\(UUID().uuidString).json")
        guard FileManager.default.createFile(atPath: outputURL.path, contents: nil,
                                            attributes: [.posixPermissions: 0o600]) else {
            return .failure(ReadError.unavailable)
        }
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
            guard process.terminationStatus == 0 else { throw ReadError.unavailable }
            let attributes = try FileManager.default.attributesOfItem(atPath: outputURL.path)
            guard let size = attributes[.size] as? NSNumber, size.intValue <= 2 * 1024 * 1024,
                  let object = try JSONSerialization.jsonObject(with: Data(contentsOf: outputURL)) as? [String: Any],
                  let catalog = object["catalog"] as? [[String: Any]],
                  let enabled = object["enabled"] as? [String],
                  let disabled = object["disabled"] as? [String] else { throw ReadError.unavailable }
            let defaultIDs = catalog.compactMap { model -> String? in
                model["defaultOn"] as? Bool == true ? model["name"] as? String : nil
            }
            let enabledIDs = Set(defaultIDs).union(enabled).subtracting(disabled)
            var mask: UInt64 = 1
            for (index, name) in BridgePreferences.cursorModels.enumerated() where index > 0 {
                let model = catalog.first { $0["clientDisplayName"] as? String == name }
                let id = model?["name"] as? String ?? modelID(name)
                if enabledIDs.contains(id) { mask |= UInt64(1) << UInt64(index) }
            }
            return .success(mask)
        } catch {
            return .failure(error)
        }
    }

    private static func modelID(_ name: String) -> String {
        if name == "Codex 5.3" { return "gpt-5.3-codex" }
        if name.hasPrefix("Cursor Grok ") {
            return name.replacingOccurrences(of: "Cursor ", with: "").lowercased()
        }
        return name.lowercased().replacingOccurrences(of: " ", with: "-")
    }
}
#endif
