import AppKit
import SwiftUI

struct MainWindowTrackerView: NSViewRepresentable {
    @EnvironmentObject private var appState: AppState

    func makeNSView(context: Context) -> TrackingView {
        let view = TrackingView()
        view.focusRingType = .none
        view.onWindowChange = { [weak appState] window in
            DispatchQueue.main.async {
                appState?.registerMainWindow(window)
            }
        }
        return view
    }

    func updateNSView(_ nsView: TrackingView, context: Context) {
        nsView.onWindowChange = { [weak appState] window in
            DispatchQueue.main.async {
                appState?.registerMainWindow(window)
            }
        }
    }

    final class TrackingView: NSView {
        var onWindowChange: ((NSWindow?) -> Void)?

        override var acceptsFirstResponder: Bool {
            false
        }

        override func viewDidMoveToWindow() {
            super.viewDidMoveToWindow()
            reportCurrentWindow()
        }

        func reportCurrentWindow() {
            onWindowChange?(window)
        }
    }
}
