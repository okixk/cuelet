import Foundation

struct ProfileService {
    func defaultProfiles() -> [SoundProfile] {
        [
            SoundProfile(name: "Default"),
            SoundProfile(name: "Streaming"),
            SoundProfile(name: "Tabletop")
        ]
    }
}
