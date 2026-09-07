import Foundation

enum SettingKind: String, CaseIterable {
    case model = "MODEL"
    case thinking = "THINKING"

    func canonicalName(_ raw: String) -> String? {
        switch self {
        case .model: return Catalog.modelName(raw)
        case .thinking: return Catalog.thinkingName(raw)
        }
    }
}

struct SerialUpdate {
    let kind: SettingKind
    let value: String
    let revision: UInt64?
}

enum SerialBridge {
    static let defaultBaud = 115_200
    static let maxLineBytes = 191

    static func parseInbound(_ raw: String) -> SerialUpdate? {
        let parts = raw.split(maxSplits: 3, whereSeparator: { $0 == " " || $0 == "\t" })
        guard parts.count >= 3 else { return nil }
        if parts[0] == "STATE", parts.count == 4,
           parts[1].count == 16, let revision = UInt64(parts[1], radix: 16),
           let kind = SettingKind(rawValue: String(parts[2])) {
            return SerialUpdate(kind: kind, value: String(parts[3]), revision: revision)
        }
        if parts[0] == "SET", let kind = SettingKind(rawValue: String(parts[1])) {
            let value = parts.dropFirst(2).joined(separator: " ")
            return SerialUpdate(kind: kind, value: value, revision: nil)
        }
        return nil
    }

    static func candidatePorts() -> [String] {
        guard let names = try? FileManager.default.contentsOfDirectory(atPath: "/dev") else {
            return []
        }
        let prefixes = ["cu.usbmodem", "cu.usbserial", "cu.wchusbserial", "cu.SLAB_USBtoUART"]
        return names.filter { name in prefixes.contains(where: { name.hasPrefix($0) }) }
            .map { "/dev/\($0)" }.sorted()
    }
}
