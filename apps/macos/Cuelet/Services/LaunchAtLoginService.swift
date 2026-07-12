import ServiceManagement

struct LaunchAtLoginService {
    func setEnabled(_ isEnabled: Bool) throws {
        if isEnabled {
            if SMAppService.mainApp.status != .enabled {
                try SMAppService.mainApp.register()
            }
        } else if SMAppService.mainApp.status == .enabled {
            try SMAppService.mainApp.unregister()
        }
    }

    var isEnabled: Bool {
        SMAppService.mainApp.status == .enabled
    }
}
