import Foundation

struct SoundProfile: Identifiable, Hashable, Codable {
    let id: UUID
    var name: String
    var clipIDs: [SoundClip.ID]

    init(id: UUID = UUID(), name: String, clipIDs: [SoundClip.ID] = []) {
        self.id = id
        self.name = name
        self.clipIDs = clipIDs
    }
}
