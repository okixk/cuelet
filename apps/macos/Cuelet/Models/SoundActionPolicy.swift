import Foundation

struct SoundActionPolicy: Equatable {
    let canPlay: Bool
    let canReveal: Bool
    let canRemoveFromLibrary: Bool
    let canDeleteManagedFile: Bool
    let canLocateOrRelink: Bool
    let renameChangesDisplayNameOnly: Bool

    init(clip: SoundClip, fileExists: Bool? = nil) {
        let exists = fileExists ?? clip.fileURL.map { FileManager.default.fileExists(atPath: $0.path) } ?? false
        canPlay = clip.isPlayable && exists
        canReveal = exists
        canRemoveFromLibrary = true
        canDeleteManagedFile = clip.storageMode == .managed && !clip.isMissing && exists
        canLocateOrRelink = clip.isMissing || clip.isBookmarkStale || !exists
        renameChangesDisplayNameOnly = true
    }
}
