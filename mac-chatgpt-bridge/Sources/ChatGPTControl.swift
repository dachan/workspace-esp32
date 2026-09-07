#if os(macOS)
import AppKit
import ApplicationServices
import CoreGraphics
import Foundation

enum ChatGPTControl {
    @discardableResult
    static func setModel(_ name: String, app: NSRunningApplication, maxDepth: Int, maxNodes: Int) -> Bool {
        activate(app)
        Thread.sleep(forTimeInterval: 0.15)
        HIDBridge.postChordControlShiftM()
        Thread.sleep(forTimeInterval: 0.4)
        if clickMatchingText(app: app, want: name, maxDepth: maxDepth, maxNodes: maxNodes) {
            ModelCache.save(name)
            return true
        }
        HIDBridge.typeText(name)
        Thread.sleep(forTimeInterval: 0.12)
        HIDBridge.postKey(keyCode: 36) // Return
        ModelCache.save(name)
        return true
    }

    @discardableResult
    static func setThinking(_ name: String, app: NSRunningApplication, maxDepth: Int, maxNodes: Int) -> Bool {
        activate(app)
        Thread.sleep(forTimeInterval: 0.1)
        return clickMatchingText(app: app, want: name, maxDepth: maxDepth, maxNodes: maxNodes)
    }

    private static func activate(_ app: NSRunningApplication) {
        app.activate(options: [.activateIgnoringOtherApps])
    }

    private static func clickMatchingText(
        app: NSRunningApplication,
        want: String,
        maxDepth: Int,
        maxNodes: Int
    ) -> Bool {
        let axApp = AXUIElementCreateApplication(app.processIdentifier)
        var snaps: [AXSnapshot] = []
        if let menuBar = AXNode.element(axApp, AXAttr.menuBar) {
            snaps += AXWalk.snapshots(of: menuBar, prefix: "menu", maxDepth: maxDepth, maxNodes: maxNodes, inMenuBar: true)
        }
        snaps += AXWalk.snapshots(of: axApp, prefix: "app", maxDepth: maxDepth, maxNodes: maxNodes)
        for (index, window) in ChatGPTProcess.windows(for: axApp).enumerated() {
            snaps += AXWalk.snapshots(of: window, prefix: "win\(index)", maxDepth: maxDepth, maxNodes: maxNodes)
        }

        let wantFold = want.lowercased()
        var bestPoint: CGPoint?
        var bestScore = -1
        for snap in snaps {
            for text in [snap.title, snap.value, snap.description, snap.help].compactMap({ $0 }) {
                let fold = text.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
                guard !fold.isEmpty else { continue }
                var score = 0
                if fold == wantFold { score = 100 }
                else if fold.contains(wantFold) { score = 80 }
                else if wantFold.contains(fold), fold.count >= 3 { score = 40 }
                if score > bestScore, let point = snap.position {
                    bestScore = score
                    bestPoint = point
                }
            }
        }
        guard let point = bestPoint, bestScore >= 40 else { return false }
        return clickScreen(point)
    }

    private static func clickScreen(_ point: CGPoint) -> Bool {
        guard let move = CGEvent(mouseEventSource: nil, mouseType: .mouseMoved, mouseCursorPosition: point, mouseButton: .left),
              let down = CGEvent(mouseEventSource: nil, mouseType: .leftMouseDown, mouseCursorPosition: point, mouseButton: .left),
              let up = CGEvent(mouseEventSource: nil, mouseType: .leftMouseUp, mouseCursorPosition: point, mouseButton: .left)
        else { return false }
        move.post(tap: .cghidEventTap)
        down.post(tap: .cghidEventTap)
        up.post(tap: .cghidEventTap)
        return true
    }
}
#endif
