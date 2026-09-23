#if os(macOS)
import AppKit
import Foundation

enum CodexModelList {
    struct Entry: Equatable {
        let name: String
        let effortMask: UInt8
    }

    static func load() -> [Entry]? {
        let installed = NSWorkspace.shared.urlForApplication(withBundleIdentifier: "com.openai.codex")
        let bundled = installed?.appendingPathComponent("Contents/Resources/codex")
        let fallback = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(".local/bin/codex")
        guard let executable = [bundled, fallback].compactMap({ $0 }).first(where: {
            FileManager.default.isExecutableFile(atPath: $0.path)
        }) else { return nil }

        let process = Process()
        process.executableURL = executable
        process.arguments = ["app-server"]
        process.standardError = FileHandle.nullDevice
        let input = Pipe()
        let output = Pipe()
        process.standardInput = input
        process.standardOutput = output
        let ready = DispatchSemaphore(value: 0)
        var buffer = Data()
        var response: Data?
        output.fileHandleForReading.readabilityHandler = { handle in
            let chunk = handle.availableData
            guard !chunk.isEmpty else { return }
            buffer.append(chunk)
            while let newline = buffer.firstIndex(of: 10) {
                let line = buffer.subdata(in: 0..<newline)
                buffer.removeSubrange(0...newline)
                if let object = (try? JSONSerialization.jsonObject(with: line)) as? [String: Any],
                   object["id"] as? Int == 1 {
                    response = line
                    ready.signal()
                    return
                }
            }
        }
        do {
            try process.run()
            for message in [
                #"{"method":"initialize","id":0,"params":{"clientInfo":{"name":"model_dial","title":"Model Dial","version":"0.90"}}}"#,
                #"{"method":"initialized","params":{}}"#,
                #"{"method":"model/list","id":1,"params":{"limit":100,"includeHidden":false}}"#,
            ] {
                try input.fileHandleForWriting.write(contentsOf: Data((message + "\n").utf8))
            }
            _ = ready.wait(timeout: .now() + 5)
        } catch {
            output.fileHandleForReading.readabilityHandler = nil
            if process.isRunning { process.terminate(); process.waitUntilExit() }
            return nil
        }
        output.fileHandleForReading.readabilityHandler = nil
        if process.isRunning { process.terminate(); process.waitUntilExit() }
        guard let response,
              let payload = (try? JSONSerialization.jsonObject(with: response)) as? [String: Any],
              let result = payload["result"] as? [String: Any],
              let data = result["data"] as? [[String: Any]] else { return nil }

        var entries: [Entry] = []
        for model in data where model["hidden"] as? Bool != true {
            guard model["model"] is String,
                  let raw = model["displayName"] as? String else { continue }
            let name = raw.replacingOccurrences(
                of: #"^(GPT-[0-9]+(?:\.[0-9]+)?)-"#, with: "$1 ", options: .regularExpression
            )
            guard name.utf8.count < 64,
                  name.unicodeScalars.allSatisfy({ $0.value >= 32 && $0.value < 127 }),
                  !entries.contains(where: { $0.name.caseInsensitiveCompare(name) == .orderedSame }) else { continue }
            let levels = model["supportedReasoningEfforts"] as? [[String: Any]] ?? []
            let bits: [String: UInt8] = ["low": 1, "medium": 2, "high": 4,
                                        "xhigh": 8, "max": 16, "ultra": 32]
            let mask = levels.reduce(UInt8(0)) { value, level in
                value | (bits[level["reasoningEffort"] as? String ?? ""] ?? 0)
            }
            // Older catalogs may omit the effort list; keep the existing dial range then.
            entries.append(Entry(name: name, effortMask: levels.isEmpty ? 0x3f : mask))
            if entries.count == 32 { break }
        }
        return entries.isEmpty ? nil : entries
    }
}
#endif
