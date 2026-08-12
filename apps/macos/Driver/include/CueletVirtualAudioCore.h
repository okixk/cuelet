#ifndef CUELET_VIRTUAL_AUDIO_CORE_H
#define CUELET_VIRTUAL_AUDIO_CORE_H

#include <CoreAudio/AudioServerPlugIn.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CUELET_DRIVER_NAME "Cuelet Virtual Microphone"
#define CUELET_DRIVER_MANUFACTURER "Cuelet"
#define CUELET_DRIVER_BUNDLE_ID "ch.oki.cuelet.virtual-microphone.driver"
#define CUELET_DRIVER_DEVICE_UID "ch.oki.cuelet.virtual-microphone"
#define CUELET_DRIVER_MODEL_UID "ch.oki.cuelet.virtual-microphone.model"
#define CUELET_DRIVER_VERSION "0.1.11"
#define CUELET_DRIVER_BUILD_VERSION "12"

#define CUELET_AUDIO_CHANNEL_COUNT 2U
#define CUELET_AUDIO_BYTES_PER_FRAME 8U
#define CUELET_RING_CAPACITY_FRAMES 16384U
#define CUELET_MAX_CLIENTS 64U

enum {
    kCueletObjectPlugIn = kAudioObjectPlugInObject,
    kCueletObjectDevice = 2,
    kCueletObjectInputStream = 3,
    kCueletObjectOutputStream = 4,
    kCueletObjectInputVolume = 5,
    kCueletObjectInputMute = 6,
    kCueletObjectOutputVolume = 7,
    kCueletObjectOutputMute = 8,
};

typedef enum CueletObjectKind {
    kCueletObjectKindUnknown = 0,
    kCueletObjectKindPlugIn,
    kCueletObjectKindDevice,
    kCueletObjectKindInputStream,
    kCueletObjectKindOutputStream,
    kCueletObjectKindVolumeControl,
    kCueletObjectKindMuteControl,
} CueletObjectKind;

typedef struct CueletTimelineFrame {
    /*
     * A slot is valid only when both the absolute frame and generation match
     * the requested range. The payload is one atomic 64-bit stereo value so
     * a reader can never observe a mixed left/right pair during publication.
     */
    _Atomic uint64_t frame;
    _Atomic uint64_t generation;
    _Atomic uint64_t sampleBits;
} CueletTimelineFrame;

typedef struct CueletRingBuffer {
    CueletTimelineFrame frames[CUELET_RING_CAPACITY_FRAMES];
    _Atomic uint64_t resetGeneration;
    _Atomic uint64_t underrunCount;
    _Atomic uint64_t overrunCount;
    _Atomic uint64_t rejectedWriteCount;
    _Atomic uint64_t lastWriteStart;
    _Atomic uint64_t lastWriteEnd;
} CueletRingBuffer;

typedef struct CueletRingReader {
    _Atomic uint64_t generation;
    _Atomic bool initialized;
    _Atomic uint64_t lastRequestedStart;
    _Atomic uint64_t lastRequestedEnd;
} CueletRingReader;

typedef enum CueletRingReadStatus {
    kCueletRingReadOK = 0,
    kCueletRingReadNotYetWritten,
    kCueletRingReadOverwritten,
    kCueletRingReadGenerationMismatch,
    kCueletRingReadAbsoluteFrameMismatch,
    kCueletRingReadPartialRange,
    kCueletRingReadUnpublished,
    kCueletRingReadSampleRateReset,
    kCueletRingReadTimelineUninitialized,
    kCueletRingReadMappingInvalid,
    kCueletRingReadStreamInactive,
    kCueletRingReadClientReaderUnavailable,
    kCueletRingReadInvalidArgument,
    kCueletRingReadStatusCount,
} CueletRingReadStatus;

typedef enum CueletRingWriteStatus {
    kCueletRingWriteOK = 0,
    kCueletRingWriteGenerationMismatch,
    kCueletRingWriteTimelineUninitialized,
    kCueletRingWriteInvalidSampleTime,
    kCueletRingWriteInvalidArgument,
} CueletRingWriteStatus;

typedef enum CueletTimelineStatus {
    kCueletTimelineOK = 0,
    kCueletTimelineInvalidArgument,
    kCueletTimelineInputTimestampInvalid,
    kCueletTimelineOutputTimestampInvalid,
    kCueletTimelineNegativeSampleTime,
    kCueletTimelineFractionalSampleTime,
    kCueletTimelineSampleTimeOverflow,
    kCueletTimelineUninitialized,
    kCueletTimelineNegativeSourceRange,
} CueletTimelineStatus;

typedef struct CueletRingWriteResult {
    CueletRingWriteStatus status;
    uint32_t acceptedFrames;
    uint64_t generation;
    uint64_t firstSlot;
    uint64_t finalSlot;
    uint64_t firstPublishedFrame;
    uint64_t finalPublishedFrame;
} CueletRingWriteResult;

typedef struct CueletRingReadResult {
    uint32_t validFrames;
    uint32_t unavailableFrames;
    uint32_t staleFrames;
    CueletRingReadStatus status;
    CueletRingReadStatus firstRejectionReason;
    uint64_t firstRejectedFrame;
    uint64_t expectedGeneration;
    uint64_t observedGeneration;
    uint64_t expectedFrame;
    uint64_t observedFrame;
    uint64_t firstSlot;
    uint64_t finalSlot;
    uint32_t readerInitiallyInitialized;
    uint32_t readerGenerationAdopted;
    uint32_t generationResolved;
    uint32_t preRingAccepted;
    uint32_t ringLookupReached;
    uint32_t ringLookupFrames;
    /* Each unavailable frame is assigned exactly one detailed ring reason. */
    uint32_t rejectionFrameCounts[kCueletRingReadStatusCount];
} CueletRingReadResult;

typedef struct CueletIOState {
    CueletRingBuffer ring;
    _Atomic uint64_t runningClientCount;
    _Atomic bool inputStreamActive;
    _Atomic bool outputStreamActive;
} CueletIOState;

CueletObjectKind CueletObjectKindForID(AudioObjectID objectID);
AudioObjectID CueletObjectOwner(AudioObjectID objectID);
bool CueletObjectSupportsProperty(
    AudioObjectID objectID,
    const AudioObjectPropertyAddress* address);

bool CueletIsSupportedSampleRate(Float64 sampleRate);
AudioStreamBasicDescription CueletMakeStreamFormat(Float64 sampleRate);
OSStatus CueletValidateStreamFormat(
    const AudioStreamBasicDescription* format);
CueletTimelineStatus CueletSampleFrameFromTimestamp(
    const AudioTimeStamp* timestamp,
    CueletTimelineStatus invalidTimestampStatus,
    uint64_t* frameOut);

void CueletRingInitialize(CueletRingBuffer* ring);
void CueletRingReset(CueletRingBuffer* ring);
uint64_t CueletRingGeneration(const CueletRingBuffer* ring);
uint64_t CueletRingLastWriteEnd(const CueletRingBuffer* ring);
void CueletRingReaderReset(
    CueletRingReader* reader,
    const CueletRingBuffer* ring);
bool CueletRingWriteAt(
    CueletRingBuffer* ring,
    uint64_t generation,
    uint64_t startFrame,
    const Float32* interleavedStereo,
    uint32_t frameCount);
CueletRingWriteResult CueletRingWriteAtDetailed(
    CueletRingBuffer* ring,
    uint64_t generation,
    uint64_t startFrame,
    const Float32* interleavedStereo,
    uint32_t frameCount);
CueletRingReadResult CueletRingReadAt(
    CueletRingBuffer* ring,
    CueletRingReader* reader,
    uint64_t generation,
    uint64_t startFrame,
    Float32* interleavedStereo,
    uint32_t frameCount);

void CueletIOStateInitialize(CueletIOState* state);
OSStatus CueletIOStateStart(CueletIOState* state);
OSStatus CueletIOStateStop(CueletIOState* state);
bool CueletIOStateSetStreamActive(
    CueletIOState* state,
    bool isInput,
    bool isActive);

#ifdef CUELET_AUDIO_TESTING
void CueletRingForceGenerationForTesting(
    CueletRingBuffer* ring,
    uint64_t generation);
#endif

#ifdef __cplusplus
}
#endif

#endif
