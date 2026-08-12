import Foundation

struct PlaybackState: Equatable {
    var playingClipIDs: Set<SoundClip.ID> = []
    var pausedClipIDs: Set<SoundClip.ID> = []
    var playbackStartDatesByClipID: [SoundClip.ID: Date] = [:]
    var lastPlayedClipID: SoundClip.ID?

    var isPlaying: Bool {
        !playingClipIDs.isEmpty
    }

    mutating func markPlaying(_ clipID: SoundClip.ID, startedAt: Date = Date()) {
        playingClipIDs.insert(clipID)
        pausedClipIDs.remove(clipID)
        playbackStartDatesByClipID[clipID] = startedAt
        lastPlayedClipID = clipID
    }

    mutating func stop(_ clipID: SoundClip.ID) {
        playingClipIDs.remove(clipID)
        pausedClipIDs.remove(clipID)
        playbackStartDatesByClipID[clipID] = nil

        if lastPlayedClipID == clipID {
            lastPlayedClipID = mostRecentPlayingClipID
        }
    }

    mutating func stopAll() {
        playingClipIDs.removeAll()
        pausedClipIDs.removeAll()
        playbackStartDatesByClipID.removeAll()
        lastPlayedClipID = nil
    }

    mutating func pause(_ clipID: SoundClip.ID) {
        guard playingClipIDs.contains(clipID) else { return }
        pausedClipIDs.insert(clipID)
    }

    mutating func resume(_ clipID: SoundClip.ID) {
        pausedClipIDs.remove(clipID)
    }

    func isPaused(_ clipID: SoundClip.ID) -> Bool {
        pausedClipIDs.contains(clipID)
    }

    var mostRecentPlayingClipID: SoundClip.ID? {
        playingClipIDs.max { lhs, rhs in
            let lhsDate = playbackStartDatesByClipID[lhs] ?? .distantPast
            let rhsDate = playbackStartDatesByClipID[rhs] ?? .distantPast
            if lhsDate == rhsDate {
                return lhs.uuidString < rhs.uuidString
            }
            return lhsDate < rhsDate
        }
    }
}
