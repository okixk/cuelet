import SwiftUI

struct ShortcutCaptureSheet: View {
    @EnvironmentObject private var appState: AppState
    @StateObject private var captureService = ShortcutCaptureService()

    let request: ShortcutCaptureRequest

    @State private var pendingShortcut: SoundShortcut?
    @State private var scope = HotkeyScope.local
    @State private var replacesExisting = false
    @State private var statusMessage: String?
    @State private var didLoadCurrentValue = false

    private var clip: SoundClip? {
        appState.clip(withID: request.clipID)
    }

    private var normalizedPendingShortcut: SoundShortcut? {
        pendingShortcut?.normalized(scope: scope)
    }

    private var conflictingClip: SoundClip? {
        guard let shortcut = normalizedPendingShortcut, let clip else { return nil }
        return appState.conflictingClip(for: shortcut, excluding: clip.id)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            VStack(alignment: .leading, spacing: 5) {
                Text(clip.map { "Shortcut for “\($0.displayName)”" } ?? "Shortcut")
                    .font(.headline)
                Text("Press a key combination, then save the assignment.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
            }

            Picker("Scope", selection: $scope) {
                ForEach(HotkeyScope.allCases) { scope in
                    Text(scope.title).tag(scope)
                }
            }
            .pickerStyle(.segmented)

            ShortcutRecorderView(
                captureService: captureService,
                statusMessage: statusMessage ?? availabilityMessage,
                fallbackLabel: normalizedPendingShortcut?.displayLabel ?? "Press a key combination"
            )

            if let conflictingClip {
                VStack(alignment: .leading, spacing: 8) {
                    Label(
                        "\(normalizedPendingShortcut?.displayLabel ?? "This shortcut") is already assigned to “\(conflictingClip.displayName)”.",
                        systemImage: "exclamationmark.triangle"
                    )
                    .font(.callout)
                    Toggle("Replace existing assignment", isOn: $replacesExisting)
                }
            }

            HStack {
                Button("Clear") {
                    pendingShortcut = nil
                    replacesExisting = false
                    statusMessage = "Shortcut will be cleared when saved."
                }
                .disabled(pendingShortcut == nil && clip?.shortcut == nil)

                Spacer()

                Button("Cancel") {
                    appState.dismissShortcutCapture()
                }
                .keyboardShortcut(.cancelAction)

                Button("Save") {
                    save()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(conflictingClip != nil && !replacesExisting)
            }
        }
        .padding(22)
        .frame(width: 420)
        .fixedSize(horizontal: false, vertical: true)
        .onAppear {
            guard !didLoadCurrentValue else { return }
            didLoadCurrentValue = true
            pendingShortcut = clip?.shortcut
            scope = clip?.shortcut?.scope ?? .local
            captureService.start(
                onValidShortcut: { shortcut in
                    pendingShortcut = shortcut.normalized(scope: scope)
                    replacesExisting = false
                    statusMessage = nil
                },
                onCancel: {
                    appState.dismissShortcutCapture()
                },
                onClear: {
                    pendingShortcut = nil
                    replacesExisting = false
                    statusMessage = "Shortcut will be cleared when saved."
                }
            )
        }
        .onDisappear {
            captureService.stop()
        }
        .onChange(of: scope) { _, newScope in
            pendingShortcut = pendingShortcut?.normalized(scope: newScope)
            replacesExisting = false
            statusMessage = nil
        }
    }

    private var availabilityMessage: String {
        guard let shortcut = normalizedPendingShortcut else { return "No shortcut assigned" }
        if conflictingClip != nil { return "Already assigned" }
        if ShortcutCaptureService.isReservedSystemShortcut(shortcut) { return "Reserved by macOS" }
        if shortcut == clip?.shortcut { return "Current shortcut" }
        return scope == .global ? "Availability is checked when saved" : "Available"
    }

    private func save() {
        guard let clip else { return }
        let result = appState.assignShortcutTransactional(
            normalizedPendingShortcut,
            to: clip.id,
            replacingConflicts: replacesExisting
        )

        switch result {
        case .assigned:
            appState.dismissShortcutCapture()
        case .conflict(let conflict):
            statusMessage = "Already assigned to “\(conflict.displayName)”."
        case .invalid(let message), .registrationFailed(let message), .persistenceFailed(let message):
            statusMessage = message
        case .notFound:
            statusMessage = "This sound is no longer available."
        }
    }
}
