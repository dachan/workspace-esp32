#if os(macOS)
import Darwin
import Foundation

/// JSON-RPC client for Rig's `models.active` / `agent.focus` / `agent.setFocus`.
enum RigClient {
    struct Error: LocalizedError {
        let errorDescription: String?
    }

    struct Model: Equatable {
        let slug: String
        let name: String
        let efforts: [String]
    }

    struct ActiveModels {
        let main: String
        let models: [Model]
    }

    struct Focus {
        let main: String
        let effort: String
        let name: String?
        let efforts: [String]
    }

    static func isAvailable() -> Bool { socketPath() != nil }

    private static var lastActive: ActiveModels?

    static func modelsActive() throws -> ActiveModels {
        do {
            let next = try parseActive(call("models.active"))
            lastActive = next
            return next
        } catch {
            if let lastActive { return lastActive }
            throw error
        }
    }

    static func focus() throws -> Focus {
        let raw = try call("agent.focus")
        guard let object = raw as? [String: Any], let main = object["main"] as? String else {
            throw Error(errorDescription: "agent.focus returned empty")
        }
        return Focus(
            main: main,
            effort: object["effort"] as? String ?? "auto",
            name: object["name"] as? String,
            efforts: object["efforts"] as? [String] ?? []
        )
    }

    static func setFocus(main: String?, effort: String?) throws -> Focus {
        var patch: [String: String] = [:]
        if let main { patch["main"] = main }
        if let effort { patch["effort"] = effort }
        let raw = try call("agent.setFocus", params: [patch])
        guard let object = raw as? [String: Any], let nextMain = object["main"] as? String else {
            throw Error(errorDescription: "agent.setFocus returned empty")
        }
        return Focus(
            main: nextMain,
            effort: object["effort"] as? String ?? effort ?? "auto",
            name: object["name"] as? String,
            efforts: object["efforts"] as? [String] ?? []
        )
    }

    private static func parseActive(_ raw: Any?) throws -> ActiveModels {
        if raw == nil || raw is NSNull {
            throw Error(errorDescription: "Open a project folder in Rig")
        }
        guard let object = raw as? [String: Any] else {
            throw Error(errorDescription: "models.active returned empty")
        }
        let main = object["main"] as? String ?? ""
        let models = (object["models"] as? [[String: Any]] ?? []).compactMap { row -> Model? in
            guard let slug = row["slug"] as? String else { return nil }
            return Model(
                slug: slug,
                name: row["name"] as? String ?? slug,
                efforts: row["efforts"] as? [String] ?? []
            )
        }
        return ActiveModels(main: main, models: models)
    }

    private static func socketPath() -> String? {
        let home = NSHomeDirectory()
        let paths = [
            home + "/Library/Application Support/Rig/rig.sock",
            home + "/Library/Application Support/Electron/rig.sock",
        ]
        return paths.first { FileManager.default.fileExists(atPath: $0) }
    }

    private static func call(_ method: String, params: [Any] = []) throws -> Any? {
        guard let path = socketPath() else { throw Error(errorDescription: "Rig is not running") }
        let fd = Darwin.socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { throw Error(errorDescription: "Could not open the Rig socket") }
        defer { Darwin.close(fd) }
        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        let pathOk = path.withCString { cstr -> Bool in
            let count = strlen(cstr) + 1
            guard count <= MemoryLayout.size(ofValue: addr.sun_path) else { return false }
            _ = withUnsafeMutablePointer(to: &addr.sun_path) { dest in
                dest.withMemoryRebound(to: CChar.self, capacity: count) { ptr in
                    strncpy(ptr, cstr, count)
                }
            }
            return true
        }
        guard pathOk else { throw Error(errorDescription: "Rig socket path is too long") }
        let connected = withUnsafePointer(to: &addr) { pointer in
            pointer.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                Darwin.connect(fd, $0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        guard connected == 0 else { throw Error(errorDescription: "Could not connect to Rig") }
        var timeout = timeval(tv_sec: 5, tv_usec: 0)
        _ = setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
        _ = setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
        let payload: [String: Any] = ["jsonrpc": "2.0", "id": 1, "method": method, "params": params]
        let body = try JSONSerialization.data(withJSONObject: payload)
        var line = body
        line.append(contentsOf: [0x0A])
        guard line.withUnsafeBytes({ Darwin.write(fd, $0.baseAddress, line.count) }) == line.count else {
            throw Error(errorDescription: "Could not write to Rig")
        }
        var collected = Data()
        var chunk = [UInt8](repeating: 0, count: 4096)
        while true {
            let n = Darwin.read(fd, &chunk, chunk.count)
            if n <= 0 { break }
            collected.append(contentsOf: chunk.prefix(n))
            if collected.contains(0x0A) { break }
        }
        guard let text = String(data: collected, encoding: .utf8),
              let jsonLine = text.split(separator: "\n", omittingEmptySubsequences: true).first,
              let decoded = try JSONSerialization.jsonObject(with: Data(jsonLine.utf8)) as? [String: Any]
        else {
            throw Error(errorDescription: "Rig returned an empty reply")
        }
        if let error = decoded["error"] as? [String: Any] {
            throw Error(errorDescription: error["message"] as? String ?? "Rig request failed")
        }
        return decoded["result"]
    }
}

enum RigApply {
    private static let effortOrder = ["auto", "none", "low", "medium", "high", "xhigh", "max"]

    static func apply(model: String?, thinking: String?) -> Switcher.Result {
        do {
            let active = try RigClient.modelsActive()
            var main: String?
            if let model {
                guard let slug = matchSlug(model, in: active.models) else {
                    return .failed("unknown Rig model \(model)")
                }
                main = slug
            }
            var effort: String?
            if let thinking {
                let allowed = main.flatMap { slug in active.models.first { $0.slug == slug }?.efforts } ?? []
                effort = matchEffort(thinking, allowed: allowed)
            }
            let next = try RigClient.setFocus(main: main, effort: effort)
            let path = "agent.setFocus main \(next.main) effort \(next.effort)"
            return .applied(path: path)
        } catch {
            return .failed(error.localizedDescription)
        }
    }

    static func matchSlug(_ raw: String, in models: [RigClient.Model]) -> String? {
        let needle = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        if needle.isEmpty { return nil }
        if let exact = models.first(where: { $0.slug.caseInsensitiveCompare(needle) == .orderedSame }) {
            return exact.slug
        }
        if let named = models.first(where: { $0.name.caseInsensitiveCompare(needle) == .orderedSame }) {
            return named.slug
        }
        if let mapped = Catalog.enabledSlug(forPanel: needle, in: models) {
            return mapped
        }
        if let panel = Catalog.rigPanelName(slug: needle, name: needle) {
            return Catalog.enabledSlug(forPanel: panel, in: models)
        }
        let folded = needle.lowercased()
        let hits = models.filter {
            $0.name.lowercased().contains(folded) || $0.slug.lowercased().contains(folded)
        }
        if hits.count == 1 { return hits[0].slug }
        return nil
    }

    static func matchEffort(_ raw: String, allowed: [String]) -> String? {
        let mapped = mapPanelEffort(raw)
        let choices = allowed.isEmpty ? ["auto"] : allowed
        if choices.contains(mapped) { return mapped }
        guard let want = effortOrder.firstIndex(of: mapped) else { return choices.contains("auto") ? "auto" : choices[0] }
        return choices.min { lhs, rhs in
            let left = abs((effortOrder.firstIndex(of: lhs) ?? 0) - want)
            let right = abs((effortOrder.firstIndex(of: rhs) ?? 0) - want)
            return left == right ? (effortOrder.firstIndex(of: lhs) ?? 0) < (effortOrder.firstIndex(of: rhs) ?? 0) : left < right
        }
    }

    private static func mapPanelEffort(_ raw: String) -> String {
        switch raw.trimmingCharacters(in: .whitespacesAndNewlines).lowercased() {
        case "auto": return "auto"
        case "none", "minimal": return "none"
        case "light", "low", "fast", "instant": return "low"
        case "medium", "standard": return "medium"
        case "high", "advanced", "thinking": return "high"
        case "extra high", "xhigh", "x-high", "ultra", "heavy": return "xhigh"
        case "max": return "max"
        default: return "auto"
        }
    }
}
#endif
