import CoreAudio
import Foundation

struct LiveAudioOutputDevice: Equatable {
    var device: AudioDevice
    var audioDeviceID: AudioDeviceID
}

@MainActor
protocol AudioDeviceProviding: AnyObject {
    func outputDeviceSnapshots() -> [LiveAudioOutputDevice]
    func inputDevices() -> [AudioDevice]
    func systemOutputDevice() -> LiveAudioOutputDevice?
    func outputDevice(forPersistentID persistentID: String) -> LiveAudioOutputDevice?
    func startObserving(_ handler: @escaping () -> Void)
    func stopObserving()
}

@MainActor
final class AudioDeviceService: AudioDeviceProviding {
    private let systemObject = AudioObjectID(kAudioObjectSystemObject)
    private let listenerQueue = DispatchQueue.main
    private var listenerBlock: AudioObjectPropertyListenerBlock?
    private var observedAliveDeviceIDs: Set<AudioDeviceID> = []
    private var changeHandler: (() -> Void)?

    isolated deinit {
        stopObserving()
    }

    func outputDeviceSnapshots() -> [LiveAudioOutputDevice] {
        let defaultID = defaultDeviceID(selector: kAudioHardwarePropertyDefaultOutputDevice)
        let candidates = allDeviceIDs()
            .compactMap { deviceID -> LiveAudioOutputDevice? in
                guard deviceHasStreams(deviceID, scope: kAudioDevicePropertyScopeOutput),
                      deviceIsAlive(deviceID),
                      let device = descriptor(
                        for: deviceID,
                        scope: kAudioDevicePropertyScopeOutput,
                        isDefault: deviceID == defaultID
                      ) else { return nil }
                return LiveAudioOutputDevice(device: device, audioDeviceID: deviceID)
            }
        return Self.normalizedOutputDevices(candidates)
    }

    static func normalizedOutputDevices(_ candidates: [LiveAudioOutputDevice]) -> [LiveAudioOutputDevice] {
        var seenUIDs: Set<String> = []
        return candidates
            .filter { candidate in
                guard candidate.device.kind == .output,
                      candidate.device.isAlive,
                      let uid = candidate.device.coreAudioUID else { return false }
                return seenUIDs.insert(uid).inserted
            }
            .sorted { lhs, rhs in
                lhs.device.name.localizedStandardCompare(rhs.device.name) == .orderedAscending
            }
    }

    func inputDevices() -> [AudioDevice] {
        let defaultID = defaultDeviceID(selector: kAudioHardwarePropertyDefaultInputDevice)
        var seenUIDs: Set<String> = []
        return allDeviceIDs()
            .compactMap { deviceID -> AudioDevice? in
                guard deviceHasStreams(deviceID, scope: kAudioDevicePropertyScopeInput),
                      deviceIsAlive(deviceID),
                      let device = descriptor(
                        for: deviceID,
                        scope: kAudioDevicePropertyScopeInput,
                        isDefault: deviceID == defaultID
                      ),
                      let uid = device.coreAudioUID,
                      seenUIDs.insert(uid).inserted else { return nil }
                return device
            }
            .sorted { lhs, rhs in
                lhs.name.localizedStandardCompare(rhs.name) == .orderedAscending
            }
    }

    func systemOutputDevice() -> LiveAudioOutputDevice? {
        guard let deviceID = defaultDeviceID(selector: kAudioHardwarePropertyDefaultOutputDevice),
              deviceHasStreams(deviceID, scope: kAudioDevicePropertyScopeOutput),
              deviceIsAlive(deviceID),
              let device = descriptor(
                for: deviceID,
                scope: kAudioDevicePropertyScopeOutput,
                isDefault: true
              ) else { return nil }
        return LiveAudioOutputDevice(device: device, audioDeviceID: deviceID)
    }

    func outputDevice(forPersistentID persistentID: String) -> LiveAudioOutputDevice? {
        guard let uid = AudioDevice(
            id: persistentID,
            name: "",
            kind: .output,
            isDefault: false,
            isVirtual: false
        ).coreAudioUID else { return nil }
        return outputDeviceSnapshots().first { $0.device.coreAudioUID == uid }
    }

    func startObserving(_ handler: @escaping () -> Void) {
        stopObserving()
        changeHandler = handler
        let block: AudioObjectPropertyListenerBlock = { [weak self] _, _ in
            Task { @MainActor [weak self] in
                guard let self else { return }
                self.rebuildAliveObservers()
                self.changeHandler?()
            }
        }
        listenerBlock = block

        addSystemListener(selector: kAudioHardwarePropertyDevices, block: block)
        addSystemListener(selector: kAudioHardwarePropertyDefaultOutputDevice, block: block)
        rebuildAliveObservers()
    }

    func stopObserving() {
        guard let block = listenerBlock else {
            changeHandler = nil
            return
        }
        removeSystemListener(selector: kAudioHardwarePropertyDevices, block: block)
        removeSystemListener(selector: kAudioHardwarePropertyDefaultOutputDevice, block: block)
        for deviceID in observedAliveDeviceIDs {
            removeDeviceListeners(deviceID: deviceID, block: block)
        }
        observedAliveDeviceIDs.removeAll()
        listenerBlock = nil
        changeHandler = nil
    }

    private func rebuildAliveObservers() {
        guard let block = listenerBlock else { return }
        let currentIDs = Set(allDeviceIDs())
        for removedID in observedAliveDeviceIDs.subtracting(currentIDs) {
            removeDeviceListeners(deviceID: removedID, block: block)
        }
        for addedID in currentIDs.subtracting(observedAliveDeviceIDs) {
            addDeviceListeners(deviceID: addedID, block: block)
        }
        observedAliveDeviceIDs = currentIDs
    }

    private func addSystemListener(selector: AudioObjectPropertySelector, block: @escaping AudioObjectPropertyListenerBlock) {
        var address = AudioObjectPropertyAddress(
            mSelector: selector,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        AudioObjectAddPropertyListenerBlock(systemObject, &address, listenerQueue, block)
    }

    private func removeSystemListener(selector: AudioObjectPropertySelector, block: @escaping AudioObjectPropertyListenerBlock) {
        var address = AudioObjectPropertyAddress(
            mSelector: selector,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        AudioObjectRemovePropertyListenerBlock(systemObject, &address, listenerQueue, block)
    }

    private func addDeviceListeners(deviceID: AudioDeviceID, block: @escaping AudioObjectPropertyListenerBlock) {
        for var address in observedDeviceAddresses {
            AudioObjectAddPropertyListenerBlock(deviceID, &address, listenerQueue, block)
        }
    }

    private func removeDeviceListeners(deviceID: AudioDeviceID, block: @escaping AudioObjectPropertyListenerBlock) {
        for var address in observedDeviceAddresses {
            AudioObjectRemovePropertyListenerBlock(deviceID, &address, listenerQueue, block)
        }
    }

    private var observedDeviceAddresses: [AudioObjectPropertyAddress] {
        [
            AudioObjectPropertyAddress(
                mSelector: kAudioDevicePropertyDeviceIsAlive,
                mScope: kAudioObjectPropertyScopeGlobal,
                mElement: kAudioObjectPropertyElementMain
            ),
            AudioObjectPropertyAddress(
                mSelector: kAudioObjectPropertyName,
                mScope: kAudioObjectPropertyScopeGlobal,
                mElement: kAudioObjectPropertyElementMain
            ),
            AudioObjectPropertyAddress(
                mSelector: kAudioDevicePropertyStreams,
                mScope: kAudioDevicePropertyScopeOutput,
                mElement: kAudioObjectPropertyElementMain
            )
        ]
    }

    private func allDeviceIDs() -> [AudioDeviceID] {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioHardwarePropertyDevices,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        var dataSize: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(systemObject, &address, 0, nil, &dataSize) == noErr else {
            return []
        }
        var deviceIDs = Array(
            repeating: AudioDeviceID(kAudioObjectUnknown),
            count: Int(dataSize) / MemoryLayout<AudioDeviceID>.size
        )
        guard AudioObjectGetPropertyData(systemObject, &address, 0, nil, &dataSize, &deviceIDs) == noErr else {
            return []
        }
        return deviceIDs.filter { $0 != kAudioObjectUnknown }
    }

    private func defaultDeviceID(selector: AudioObjectPropertySelector) -> AudioDeviceID? {
        var address = AudioObjectPropertyAddress(
            mSelector: selector,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        var deviceID = AudioDeviceID(kAudioObjectUnknown)
        var dataSize = UInt32(MemoryLayout<AudioDeviceID>.size)
        guard AudioObjectGetPropertyData(systemObject, &address, 0, nil, &dataSize, &deviceID) == noErr,
              deviceID != kAudioObjectUnknown else { return nil }
        return deviceID
    }

    private func descriptor(
        for deviceID: AudioDeviceID,
        scope: AudioObjectPropertyScope,
        isDefault: Bool
    ) -> AudioDevice? {
        guard let uid = stringProperty(deviceID, selector: kAudioDevicePropertyDeviceUID),
              !uid.isEmpty,
              let name = stringProperty(deviceID, selector: kAudioObjectPropertyName),
              !name.isEmpty else { return nil }
        let transport = uint32Property(deviceID, selector: kAudioDevicePropertyTransportType)
        return AudioDevice(
            id: AudioDevice.persistentID(forCoreAudioUID: uid),
            name: name,
            kind: scope == kAudioDevicePropertyScopeInput ? .input : .output,
            isDefault: isDefault,
            isVirtual: Self.isVirtualTransport(transport),
            manufacturer: stringProperty(deviceID, selector: kAudioObjectPropertyManufacturer),
            transportName: transportName(transport),
            isAlive: true
        )
    }

    private func deviceHasStreams(_ deviceID: AudioDeviceID, scope: AudioObjectPropertyScope) -> Bool {
        var address = AudioObjectPropertyAddress(
            mSelector: kAudioDevicePropertyStreams,
            mScope: scope,
            mElement: kAudioObjectPropertyElementMain
        )
        var dataSize: UInt32 = 0
        return AudioObjectGetPropertyDataSize(deviceID, &address, 0, nil, &dataSize) == noErr
            && dataSize >= UInt32(MemoryLayout<AudioStreamID>.size)
    }

    private func deviceIsAlive(_ deviceID: AudioDeviceID) -> Bool {
        uint32Property(deviceID, selector: kAudioDevicePropertyDeviceIsAlive) == 1
    }

    private func stringProperty(_ objectID: AudioObjectID, selector: AudioObjectPropertySelector) -> String? {
        var address = AudioObjectPropertyAddress(
            mSelector: selector,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        var value: CFString = "" as CFString
        var dataSize = UInt32(MemoryLayout<CFString>.size)
        let status = withUnsafeMutablePointer(to: &value) { pointer in
            AudioObjectGetPropertyData(objectID, &address, 0, nil, &dataSize, pointer)
        }
        guard status == noErr else { return nil }
        let string = value as String
        return string.isEmpty ? nil : string
    }

    private func uint32Property(_ objectID: AudioObjectID, selector: AudioObjectPropertySelector) -> UInt32? {
        var address = AudioObjectPropertyAddress(
            mSelector: selector,
            mScope: kAudioObjectPropertyScopeGlobal,
            mElement: kAudioObjectPropertyElementMain
        )
        var value: UInt32 = 0
        var dataSize = UInt32(MemoryLayout<UInt32>.size)
        guard AudioObjectGetPropertyData(objectID, &address, 0, nil, &dataSize, &value) == noErr else {
            return nil
        }
        return value
    }

    private func transportName(_ transport: UInt32?) -> String? {
        switch transport {
        case kAudioDeviceTransportTypeBuiltIn: "Built-in"
        case kAudioDeviceTransportTypeAggregate: "Aggregate"
        case kAudioDeviceTransportTypeVirtual: "Virtual"
        case kAudioDeviceTransportTypePCI: "PCI"
        case kAudioDeviceTransportTypeUSB: "USB"
        case kAudioDeviceTransportTypeFireWire: "FireWire"
        case kAudioDeviceTransportTypeBluetooth: "Bluetooth"
        case kAudioDeviceTransportTypeBluetoothLE: "Bluetooth LE"
        case kAudioDeviceTransportTypeHDMI: "HDMI"
        case kAudioDeviceTransportTypeDisplayPort: "DisplayPort"
        case kAudioDeviceTransportTypeAirPlay: "AirPlay"
        case kAudioDeviceTransportTypeAVB: "AVB"
        case kAudioDeviceTransportTypeThunderbolt: "Thunderbolt"
        case kAudioDeviceTransportTypeContinuityCaptureWired: "Continuity (wired)"
        case kAudioDeviceTransportTypeContinuityCaptureWireless: "Continuity (wireless)"
        case .some: "Other"
        case .none: nil
        }
    }

    static func isVirtualTransport(_ transport: UInt32?) -> Bool {
        transport == kAudioDeviceTransportTypeVirtual
    }
}
