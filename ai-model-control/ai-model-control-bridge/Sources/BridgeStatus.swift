import Foundation

/// Machine-readable stdout lines the Model Dial menu app maps to its status
/// label, so the menu never depends on the wording of human log lines.
enum BridgeStatus: String {
    case serialConnected = "SERIAL_CONNECTED"
    case serialUnavailable = "SERIAL_UNAVAILABLE"
    case dialState = "DIAL_STATE"
    case applying = "APPLYING"
    case applyDropped = "APPLY_DROPPED"
    case posted = "POSTED"
    case applyFailed = "APPLY_FAILED"
    case inputGuardUnavailable = "INPUT_GUARD_UNAVAILABLE"

    func emit() {
        print("BRIDGE_STATUS \(rawValue)")
        fflush(stdout)
    }
}
