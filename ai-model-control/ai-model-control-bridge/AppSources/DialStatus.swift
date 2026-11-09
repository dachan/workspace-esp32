#if os(macOS)
import Foundation

/// The menu's status row. Supervisor events set the first group; the bridge's
/// `BRIDGE_STATUS <TOKEN>` lines set the second.
enum DialStatus: Equatable {
    case starting
    case restarting
    case off
    case waitingForPanel
    case chooseDevice
    case waitingForSelectedPanel
    case unavailable
    case connecting
    case panelConnected
    case serialUnavailable
    case dialStateReceived
    case applying
    case applyDropped
    case commandSent
    case applyFailed
    case inputPermissionNeeded

    /// Parses one bridge stdout line; nil for ordinary log lines.
    init?(bridgeLine line: String) {
        let prefix = "BRIDGE_STATUS "
        guard line.hasPrefix(prefix) else { return nil }
        switch line.dropFirst(prefix.count) {
        case "SERIAL_CONNECTED": self = .panelConnected
        case "SERIAL_UNAVAILABLE": self = .serialUnavailable
        case "DIAL_STATE": self = .dialStateReceived
        case "APPLYING": self = .applying
        case "APPLY_DROPPED": self = .applyDropped
        case "POSTED": self = .commandSent
        case "APPLY_FAILED": self = .applyFailed
        case "INPUT_GUARD_UNAVAILABLE": self = .inputPermissionNeeded
        default: return nil
        }
    }

    var title: String {
        switch self {
        case .starting: return "Starting"
        case .restarting: return "Restarting"
        case .off: return "Off"
        case .waitingForPanel: return "Waiting for panel"
        case .chooseDevice: return "Choose a device in Device menu"
        case .waitingForSelectedPanel: return "Waiting for selected panel"
        case .unavailable: return "Unavailable"
        case .connecting: return "Connecting to panel"
        case .panelConnected: return "Panel connected — waiting for dial state"
        case .serialUnavailable: return "Serial unavailable — retrying"
        case .dialStateReceived: return "Dial state received"
        case .applying: return "Applying dial to focused app"
        case .applyDropped: return "Apply dropped — focus a supported app"
        case .commandSent: return "Command sent — verify app setting"
        case .applyFailed: return "Apply failed — change dial or apply again"
        case .inputPermissionNeeded: return "Input permission needed — see log"
        }
    }
}
#endif
