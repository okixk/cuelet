import SwiftUI

struct SettingsView: View {
    @EnvironmentObject private var appState: AppState

    var body: some View {
        TabView {
            Form {
                Section("Library") {
                    LabeledContent("Library", value: appState.settings.libraryPath.isEmpty ? "Not selected" : appState.settings.libraryPath)
                    Button("Choose Library…") {
                        appState.chooseLibrary()
                    }
                    Toggle("Scan subfolders", isOn: Binding(
                        get: { appState.settings.scansSubfolders },
                        set: { appState.setScansSubfolders($0) }
                    ))
                    Toggle("Stop playback when changing library", isOn: $appState.settings.stopOnLibraryChange)
                }

                Section("Import Behavior") {
                    Toggle("Prefer Copy in the import dialog", isOn: $appState.settings.copiesImportedFiles)
                    Text("Cuelet always asks before importing. Copy creates a managed file; Link keeps the external file in place.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }

            }
            .padding(20)
            .tabItem { Label("Library", systemImage: "folder") }

            Form {
                Section("Playback") {
                    LabeledContent(
                        "Output",
                        value: appState.audioRouteStatus.activeName
                            ?? "\(appState.audioRouteStatus.selectedName) (ready)"
                    )
                    Slider(value: $appState.settings.soundboardVolume, in: 0...1) {
                        Text("Soundboard Volume")
                    }
                    Toggle("Allow simultaneous playback", isOn: $appState.settings.allowsSimultaneousPlayback)
                }

                Section("Shortcuts") {
                    LabeledContent("Global Shortcuts", value: appState.globalShortcutStatusMessage)
                    Text("Assign Local or Global shortcuts from a sound's context menu.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(20)
            .tabItem { Label("Playback", systemImage: "speaker.wave.2") }

            AudioRoutingSettingsView(appState: appState)
                .padding(20)
                .tabItem { Label("Audio & Microphone", systemImage: "mic") }

            Form {
                Section("Background") {
                    Toggle("Keep Cuelet running after closing its main window", isOn: $appState.settings.keepsRunningAfterWindowClose)
                    Toggle("Show Cuelet in the menu bar", isOn: $appState.settings.showsMenuBarItem)
                    Toggle("Start hidden", isOn: $appState.settings.startsHidden)
                        .disabled(!appState.settings.keepsRunningAfterWindowClose)
                }

                Section("Login") {
                    Toggle("Launch at login", isOn: Binding(
                        get: { appState.settings.launchesAtLogin },
                        set: { appState.setLaunchesAtLogin($0) }
                    ))
                    Text("Launch at Login requires the Finder-launchable Cuelet app bundle.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(20)
            .tabItem { Label("Background", systemImage: "menubar.rectangle") }

            Form {
                Section("Appearance") {
                    Picker("Appearance", selection: $appState.settings.appearanceMode) {
                        Text("System").tag("System")
                        Text("Light").tag("Light")
                        Text("Dark").tag("Dark")
                    }
                }
            }
            .padding(20)
            .tabItem { Label("Display", systemImage: "rectangle.on.rectangle") }

#if DEBUG
            Form {
                Section("Developer") {
                    LabeledContent("Settings domain", value: "ch.oki.cuelet")
                    Text("Detailed driver and routing information is available in Debug builds only.")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            }
            .padding(20)
            .tabItem { Label("Developer", systemImage: "hammer") }
#endif
        }
        .frame(width: 780, height: 680)
        .task {
            appState.refreshAudioRouting()
        }
    }
}

private struct AudioRoutingSettingsView: View {
    @ObservedObject var appState: AppState
#if DEBUG
    @State private var showsDriverTechnicalDetails = false
#endif

    var body: some View {
        Form {
            Section("Cuelet Output") {
                Picker("Output Device", selection: Binding(
                    get: { appState.settings.outputDeviceID },
                    set: { _ = appState.selectOutputDevice(id: $0) }
                )) {
                    ForEach(appState.outputDevices) { device in
                        Label {
                            Text(devicePickerTitle(device))
                        } icon: {
                            Image(systemName: device.isVirtual ? "waveform.badge.plus" : "speaker.wave.2")
                        }
                        .tag(device.id)
                    }
                    if selectedDeviceIsUnavailable {
                        Label("\(appState.settings.outputDeviceName) — Unavailable", systemImage: "exclamationmark.triangle")
                            .tag(appState.settings.outputDeviceID)
                    }
                }
                .accessibilityLabel("Cuelet output device")
                .accessibilityValue(outputAccessibilityValue)

                LabeledContent("Selected", value: selectedOutputLabel)

                LabeledContent("Active", value: activeOutputLabel)
                    .accessibilityLabel("Active Cuelet output")
                    .accessibilityValue(activeOutputLabel)

                Label(appState.audioRouteStatus.message, systemImage: routeStatusIcon)
                    .font(.callout)
                    .foregroundStyle(routeStatusColor)
                    .lineLimit(nil)
                    .fixedSize(horizontal: false, vertical: true)
                    .frame(maxWidth: 500, alignment: .leading)
                    .accessibilityLabel("Output routing status")
                    .accessibilityValue(appState.audioRouteStatus.message)

#if DEBUG
                if let details = appState.audioRouteStatus.technicalDetails {
                    Text(details)
                        .font(.caption.monospaced())
                        .textSelection(.enabled)
                        .foregroundStyle(.secondary)
                        .accessibilityLabel("Output routing technical error")
                }
#endif

                Picker("On Device Loss", selection: Binding(
                    get: { appState.settings.outputFallbackPolicy },
                    set: { appState.setOutputFallbackPolicy($0) }
                )) {
                    ForEach(AudioOutputFallbackPolicy.allCases) { policy in
                        Text(policy.title).tag(policy)
                    }
                }
                .disabled(appState.settings.outputDeviceID == AudioDevice.systemOutput.id)
                .accessibilityHint(appState.settings.outputFallbackPolicy.explanation)

                Text(appState.settings.outputFallbackPolicy.explanation)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .lineLimit(nil)
                    .fixedSize(horizontal: false, vertical: true)
                    .frame(maxWidth: 500, alignment: .leading)

                HStack {
                    Button("Refresh Outputs") {
                        appState.refreshAudioRouting()
                    }
                    Spacer()
                    Text("Cuelet never changes the macOS system default.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Section("Available Outputs") {
                ForEach(appState.outputDevices) { device in
                    HStack(spacing: 12) {
                        Text(device.name)
                            .lineLimit(1)
                            .truncationMode(.middle)
                        Spacer()
                        Text(deviceDescription(device))
                            .lineLimit(1)
                            .foregroundStyle(.secondary)
                    }
                }
            }

            Section("Microphone Input") {
                LabeledContent("Metering Input", value: "macOS Default Microphone")

                ForEach(appState.inputDevices) { device in
                    LabeledContent(device.name, value: device.isVirtual ? "Virtual" : "Detected")
                }

                LabeledContent("Permission", value: appState.microphonePermissionState.title)

                HStack {
                    Button("Request Microphone Access") {
                        Task { await appState.requestMicrophonePermission() }
                    }
                    .disabled(appState.microphonePermissionState == .authorized)

                    Button("Refresh All Devices") {
                        appState.refreshAudioRouting()
                    }
                }

                Toggle("Monitor input level", isOn: Binding(
                    get: { appState.settings.isInputMonitoringEnabled },
                    set: { appState.setInputMonitoringEnabled($0) }
                ))

                VStack(alignment: .leading, spacing: 6) {
                    ProgressView(value: appState.inputLevelState.averagePower)
                    Text(inputLevelLabel)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
            }

            Section("Cuelet Virtual Microphone") {
                LabeledContent {
                    Label(
                        appState.virtualAudioDriverStatus.title,
                        systemImage: driverStatusIcon
                    )
                    .foregroundStyle(driverStatusColor)
                } label: {
                    Text("Status")
                }
                .accessibilityLabel("Cuelet Virtual Microphone status")
                .accessibilityValue(appState.virtualAudioDriverStatus.title)

                if let installedVersion = appState.virtualAudioDriverStatus.installedVersion {
#if DEBUG
                    let installedBuild = appState.virtualAudioDriverStatus.installedBuildVersion
                    LabeledContent(
                        "Installed Version",
                        value: installedBuild.map { "\(installedVersion) (build \($0))" }
                            ?? installedVersion
                    )
#else
                    LabeledContent("Installed Version", value: installedVersion)
#endif
                }
                if let preparedVersion = appState.virtualAudioDriverStatus.preparedVersion {
#if DEBUG
                    let preparedBuild = appState.virtualAudioDriverStatus.preparedBuildVersion
                    LabeledContent(
                        "Bundled Version",
                        value: preparedBuild.map { "\(preparedVersion) (build \($0))" }
                            ?? preparedVersion
                    )
#else
                    LabeledContent("Bundled Version", value: preparedVersion)
#endif
                }

                Text(appState.virtualAudioDriverStatus.message)
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .lineLimit(nil)
                    .fixedSize(horizontal: false, vertical: true)

                if appState.virtualAudioDriverStatus.isDeviceReady,
                   appState.settings.outputDeviceID != AudioDevice.persistentID(
                    forCoreAudioUID: CueletVirtualAudioDriverStatus.deviceUID
                   ) {
                    Button("Use as Cuelet Output") {
                        _ = appState.selectOutputDevice(
                            id: AudioDevice.persistentID(
                                forCoreAudioUID: CueletVirtualAudioDriverStatus.deviceUID
                            )
                        )
                    }
                }

                Text("Virtual-microphone routing sends Cuelet playback to the selected receiving app. Physical-microphone mixing and simultaneous speaker output are not available on macOS.")
                    .font(.callout)
                    .foregroundStyle(.secondary)
                    .lineLimit(nil)
                    .fixedSize(horizontal: false, vertical: true)

#if DEBUG
                Button {
                    showsDriverTechnicalDetails.toggle()
                } label: {
                    Label(
                        "Technical details",
                        systemImage: showsDriverTechnicalDetails
                            ? "chevron.down"
                            : "chevron.right"
                    )
                    .frame(maxWidth: .infinity, alignment: .leading)
                }
                .buttonStyle(.plain)
                .accessibilityValue(showsDriverTechnicalDetails ? "Expanded" : "Collapsed")
                if showsDriverTechnicalDetails {
                    Text(appState.virtualAudioDriverStatus.technicalDetails)
                        .font(.caption.monospaced())
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(.top, 4)
                }
#endif

                if !appState.audioStatusMessage.isEmpty {
                    Text(appState.audioStatusMessage)
                        .font(.callout)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .formStyle(.grouped)
    }

    private var inputLevelLabel: String {
        guard appState.inputLevelState.isMonitoring else { return "Input monitoring is off." }
        let level = Int(appState.inputLevelState.averagePower * 100)
        return "Input level: \(level)%"
    }

    private var selectedDeviceIsUnavailable: Bool {
        appState.settings.outputDeviceID != AudioDevice.systemOutput.id
            && !appState.outputDevices.contains { $0.id == appState.settings.outputDeviceID }
    }

    private var selectedOutputLabel: String {
        selectedDeviceIsUnavailable
            ? "\(appState.settings.outputDeviceName) — Unavailable"
            : appState.audioRouteStatus.selectedName
    }

    private var activeOutputLabel: String {
        guard appState.audioRouteStatus.isConfirmedActive,
              let activeName = appState.audioRouteStatus.activeName else {
            return appState.audioRouteStatus.kind == .ready ? "Not playing — route ready" : "Not active"
        }
        return activeName
    }

    private var outputAccessibilityValue: String {
        "Selected \(selectedOutputLabel). \(appState.audioRouteStatus.message)"
    }

    private var routeStatusIcon: String {
        switch appState.audioRouteStatus.kind {
        case .applying, .reconnecting: "arrow.triangle.2.circlepath"
        case .ready, .systemOutput, .explicitDevice: "checkmark.circle"
        case .fallbackSystemOutput: "arrow.uturn.forward.circle"
        case .unavailable, .failed: "exclamationmark.triangle"
        }
    }

    private var routeStatusColor: Color {
        switch appState.audioRouteStatus.kind {
        case .unavailable, .failed: .orange
        default: .secondary
        }
    }

    private var driverStatusIcon: String {
        switch appState.virtualAudioDriverStatus.kind {
        case .ready, .selected: "checkmark.circle"
        case .preparedForInstallation, .updateAvailable: "shippingbox"
        case .restartRequired: "restart.circle"
        case .notInstalled: "externaldrive.badge.questionmark"
        case .unavailable, .versionMismatch, .installationError, .routingError:
            "exclamationmark.triangle"
        }
    }

    private var driverStatusColor: Color {
        switch appState.virtualAudioDriverStatus.kind {
        case .ready, .selected: .green
        case .preparedForInstallation, .restartRequired, .updateAvailable: .secondary
        case .notInstalled, .unavailable, .versionMismatch, .installationError, .routingError:
            .orange
        }
    }

    private func devicePickerTitle(_ device: AudioDevice) -> String {
        device.isVirtual ? "\(device.name) — Virtual" : device.name
    }

    private func deviceDescription(_ device: AudioDevice) -> String {
        if device.id == AudioDevice.systemOutput.id { return "Follows macOS" }
        var components: [String] = []
        if device.isVirtual { components.append("Virtual") }
        if let transport = device.transportName { components.append(transport) }
        if let manufacturer = device.manufacturer,
           manufacturer.localizedCaseInsensitiveCompare("Apple Inc.") != .orderedSame {
            components.append(manufacturer)
        }
        if device.id == appState.settings.outputDeviceID { components.append("Selected") }
        return components.isEmpty ? "Available" : components.joined(separator: " · ")
    }
}
