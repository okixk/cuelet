/* Privacy-safe inspector for Cuelet diagnostic driver custom properties. */

#include "CueletVirtualAudioCore.h"
#include "CueletVirtualAudioDiagnosticClient.h"
#include "CueletVirtualAudioDiagnostics.h"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>
#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static AudioDeviceID findDevice(void)
{
    const AudioObjectPropertyAddress devicesAddress = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(
            kAudioObjectSystemObject,
            &devicesAddress,
            0,
            NULL,
            &size) != noErr || size == 0) {
        return kAudioObjectUnknown;
    }
    AudioDeviceID* devices = calloc(1, size);
    if (devices == NULL) {
        return kAudioObjectUnknown;
    }
    if (AudioObjectGetPropertyData(
            kAudioObjectSystemObject,
            &devicesAddress,
            0,
            NULL,
            &size,
            devices) != noErr) {
        free(devices);
        return kAudioObjectUnknown;
    }
    CFStringRef wanted = CFSTR(CUELET_DRIVER_DEVICE_UID);
    AudioDeviceID result = kAudioObjectUnknown;
    for (UInt32 index = 0; index < size / sizeof(AudioDeviceID); ++index) {
        const AudioObjectPropertyAddress uidAddress = {
            kAudioDevicePropertyDeviceUID,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain,
        };
        CFStringRef uid = NULL;
        UInt32 uidSize = sizeof(uid);
        if (AudioObjectGetPropertyData(
                devices[index],
                &uidAddress,
                0,
                NULL,
                &uidSize,
                &uid) == noErr && uid != NULL &&
            CFEqual(uid, wanted)) {
            result = devices[index];
            break;
        }
    }
    free(devices);
    return result;
}

static void fourCC(UInt32 value, char output[5])
{
    output[0] = isprint((int)((value >> 24) & 0xffU))
        ? (char)((value >> 24) & 0xffU) : '.';
    output[1] = isprint((int)((value >> 16) & 0xffU))
        ? (char)((value >> 16) & 0xffU) : '.';
    output[2] = isprint((int)((value >> 8) & 0xffU))
        ? (char)((value >> 8) & 0xffU) : '.';
    output[3] = isprint((int)(value & 0xffU)) ? (char)(value & 0xffU) : '.';
    output[4] = '\0';
}

static void statusText(OSStatus status, char output[64])
{
    if (status == noErr) {
        snprintf(output, 64, "noErr");
        return;
    }
    char code[5];
    fourCC((UInt32)status, code);
    snprintf(output, 64, "%d('%s')", (int)status, code);
}

static bool supportedPropertyListSize(UInt32 size)
{
    return size == sizeof(CFPropertyListRef);
}

static OSStatus getPropertyList(
    AudioDeviceID device,
    UInt32 selector,
    bool required,
    CFPropertyListRef* valueOut)
{
    const AudioObjectPropertyAddress property =
        CueletDiagnosticPropertyAddress(selector);
    const Boolean hasProperty = AudioObjectHasProperty(device, &property);
    UInt32 size = 0;
    const OSStatus sizeStatus = AudioObjectGetPropertyDataSize(
        device, &property, 0, NULL, &size);
    if (!hasProperty || sizeStatus != noErr ||
        !supportedPropertyListSize(size)) {
        char selectorCode[5];
        char scopeCode[5];
        char sizeStatusDescription[64];
        fourCC(selector, selectorCode);
        fourCC(property.mScope, scopeCode);
        statusText(sizeStatus, sizeStatusDescription);
        fprintf(stderr,
            "diagnostic property access failed: device=%u uid=%s "
            "selector=%s(0x%08x) scope=%s(0x%08x) element=%u "
            "hasProperty=%u sizeStatus=%s size=%u requirement=%s; "
            "expected Cuelet device/global/main with CFPropertyList data\n",
            device, CUELET_DRIVER_DEVICE_UID, selectorCode, selector,
            scopeCode, property.mScope, property.mElement, hasProperty,
            sizeStatusDescription, size, required ? "required" : "optional");
        return sizeStatus != noErr
            ? sizeStatus : kAudioHardwareUnknownPropertyError;
    }
    CFPropertyListRef value = NULL;
    UInt32 used = sizeof(value);
    const OSStatus getStatus = AudioObjectGetPropertyData(
        device, &property, 0, NULL, &used, &value);
    if (getStatus != noErr || used != sizeof(value) || value == NULL) {
        char selectorCode[5];
        char getStatusDescription[64];
        fourCC(selector, selectorCode);
        statusText(getStatus, getStatusDescription);
        fprintf(stderr,
            "diagnostic property read failed: device=%u uid=%s "
            "selector=%s(0x%08x) scope=global element=main "
            "getStatus=%s returnedSize=%u requirement=%s\n",
            device, CUELET_DRIVER_DEVICE_UID, selectorCode, selector,
            getStatusDescription, used, required ? "required" : "optional");
        if (value != NULL) CFRelease(value);
        return getStatus != noErr
            ? getStatus : kAudioHardwareBadPropertySizeError;
    }
    *valueOut = value;
    return noErr;
}

static OSStatus getDataProperty(
    AudioDeviceID device,
    UInt32 selector,
    bool required,
    void* valueOut,
    size_t expectedSize)
{
    CFPropertyListRef value = NULL;
    const OSStatus status = getPropertyList(
        device, selector, required, &value);
    if (status != noErr) {
        return status;
    }
    OSStatus result = noErr;
    if (CFGetTypeID(value) != CFDataGetTypeID() ||
        CFDataGetLength((CFDataRef)value) != (CFIndex)expectedSize) {
        char selectorCode[5];
        fourCC(selector, selectorCode);
        fprintf(stderr,
            "diagnostic property payload invalid: device=%u selector=%s "
            "expected=CFData/%zu bytes requirement=%s\n",
            device, selectorCode, expectedSize,
            required ? "required" : "optional");
        result = kAudioHardwareBadPropertySizeError;
    } else {
        memcpy(valueOut, CFDataGetBytePtr((CFDataRef)value), expectedSize);
    }
    CFRelease(value);
    return result;
}

static OSStatus getDataPropertyPrefix(
    AudioDeviceID device,
    UInt32 selector,
    bool required,
    void* valueOut,
    size_t minimumSize,
    size_t maximumSize)
{
    CFPropertyListRef value = NULL;
    const OSStatus status = getPropertyList(
        device, selector, required, &value);
    if (status != noErr) return status;
    OSStatus result = noErr;
    if (CFGetTypeID(value) != CFDataGetTypeID()) {
        result = kAudioHardwareIllegalOperationError;
    } else {
        const CFIndex length = CFDataGetLength((CFDataRef)value);
        if (length < 0 || (size_t)length < minimumSize ||
            (size_t)length > maximumSize) {
            result = kAudioHardwareBadPropertySizeError;
        } else {
            memset(valueOut, 0, maximumSize);
            memcpy(
                valueOut,
                CFDataGetBytePtr((CFDataRef)value),
                (size_t)length);
        }
    }
    CFRelease(value);
    return result;
}

static OSStatus verifyCustomPropertyInfo(AudioDeviceID device)
{
    const AudioObjectPropertyAddress address = CueletDiagnosticPropertyAddress(
        kAudioObjectPropertyCustomPropertyInfoList);
    const Boolean hasProperty = AudioObjectHasProperty(device, &address);
    UInt32 size = 0;
    const OSStatus sizeStatus = AudioObjectGetPropertyDataSize(
        device, &address, 0, NULL, &size);
    const AudioObjectPropertySelector expectedSelectors[] = {
        kCueletDiagnosticPropertySchema,
        kCueletDiagnosticPropertyCounters,
        kCueletDiagnosticPropertyEvents,
        kCueletDiagnosticPropertyEventCount,
        kCueletDiagnosticPropertyClear,
        kCueletDiagnosticPropertyBuild,
        kCueletDiagnosticPropertyEnabled,
    };
    const UInt32 expectedSize = sizeof(expectedSelectors) /
        sizeof(expectedSelectors[0]) *
        (UInt32)sizeof(AudioServerPlugInCustomPropertyInfo);
    if (!hasProperty || sizeStatus != noErr || size != expectedSize) {
        char statusDescription[64];
        statusText(sizeStatus, statusDescription);
        fprintf(stderr,
            "diagnostic metadata access failed: device=%u uid=%s "
            "selector=cust(0x%08x) scope=glob(0x%08x) element=0 "
            "hasProperty=%u sizeStatus=%s size=%u expectedSize=%u "
            "requirement=required\n",
            device, CUELET_DRIVER_DEVICE_UID,
            kAudioObjectPropertyCustomPropertyInfoList,
            kAudioObjectPropertyScopeGlobal,
            hasProperty, statusDescription, size, expectedSize);
        return sizeStatus != noErr
            ? sizeStatus : kAudioHardwareBadPropertySizeError;
    }
    AudioServerPlugInCustomPropertyInfo properties[7] = {0};
    UInt32 used = sizeof(properties);
    const OSStatus getStatus = AudioObjectGetPropertyData(
        device, &address, 0, NULL, &used, properties);
    if (getStatus != noErr || used != sizeof(properties)) {
        return getStatus != noErr
            ? getStatus : kAudioHardwareBadPropertySizeError;
    }
    for (size_t index = 0;
         index < sizeof(expectedSelectors) / sizeof(expectedSelectors[0]);
         ++index) {
        const AudioServerPlugInCustomPropertyDataType expectedQualifier =
            kAudioServerPlugInCustomPropertyDataTypeNone;
        if (properties[index].mSelector != expectedSelectors[index] ||
            properties[index].mPropertyDataType !=
                kAudioServerPlugInCustomPropertyDataTypeCFPropertyList ||
            properties[index].mQualifierDataType != expectedQualifier) {
            fprintf(stderr,
                "diagnostic metadata entry invalid: device=%u index=%zu\n",
                device, index);
            return kAudioHardwareIllegalOperationError;
        }
    }
    return noErr;
}

static const char* eventName(uint32_t kind)
{
    switch ((CueletDiagnosticEventKind)kind) {
    case kCueletDiagnosticDriverInitialize: return "driver_initialize";
    case kCueletDiagnosticStartIO: return "start_io";
    case kCueletDiagnosticStopIO: return "stop_io";
    case kCueletDiagnosticGetZeroTimeStamp: return "zero_timestamp";
    case kCueletDiagnosticWillDoIOOperation: return "will_do_io";
    case kCueletDiagnosticBeginIOOperation: return "begin_io";
    case kCueletDiagnosticDoIOOperation: return "do_io";
    case kCueletDiagnosticEndIOOperation: return "end_io";
    case kCueletDiagnosticWriteMix: return "write_mix";
    case kCueletDiagnosticReadInput: return "read_input";
    case kCueletDiagnosticRingReset: return "ring_reset";
    case kCueletDiagnosticRingWrite: return "ring_write";
    case kCueletDiagnosticRingRead: return "ring_read";
    case kCueletDiagnosticProducerRejected: return "producer_rejected";
    case kCueletDiagnosticSampleRateChange: return "sample_rate_change";
    case kCueletDiagnosticStreamActivationChange: return "stream_activation";
    case kCueletDiagnosticPropertySnapshot: return "property_snapshot";
    case kCueletDiagnosticEventKindCount: break;
    }
    return "unknown";
}

static const char* writeResultName(uint32_t status)
{
    switch ((CueletRingWriteStatus)status) {
    case kCueletRingWriteOK: return "WRITE_OK";
    case kCueletRingWriteGenerationMismatch: return "WRITE_GENERATION_MISMATCH";
    case kCueletRingWriteTimelineUninitialized: return "WRITE_TIMELINE_UNINITIALIZED";
    case kCueletRingWriteInvalidSampleTime: return "WRITE_INVALID_SAMPLE_TIME";
    case kCueletRingWriteInvalidArgument: return "WRITE_INVALID_ARGUMENT";
    }
    return "WRITE_UNKNOWN";
}

static const char* readResultName(uint32_t status)
{
    switch ((CueletRingReadStatus)status) {
    case kCueletRingReadOK: return "READ_OK";
    case kCueletRingReadNotYetWritten: return "READ_NOT_YET_WRITTEN";
    case kCueletRingReadOverwritten: return "READ_OVERWRITTEN";
    case kCueletRingReadGenerationMismatch: return "READ_GENERATION_MISMATCH";
    case kCueletRingReadAbsoluteFrameMismatch: return "READ_ABSOLUTE_FRAME_MISMATCH";
    case kCueletRingReadPartialRange: return "READ_PARTIAL_RANGE";
    case kCueletRingReadUnpublished: return "READ_UNPUBLISHED";
    case kCueletRingReadSampleRateReset: return "READ_SAMPLE_RATE_RESET";
    case kCueletRingReadTimelineUninitialized: return "READ_TIMELINE_UNINITIALIZED";
    case kCueletRingReadMappingInvalid: return "READ_MAPPING_INVALID";
    case kCueletRingReadStreamInactive: return "READ_STREAM_INACTIVE";
    case kCueletRingReadClientReaderUnavailable:
        return "READ_CLIENT_READER_UNAVAILABLE";
    case kCueletRingReadInvalidArgument: return "READ_INVALID_ARGUMENT";
    case kCueletRingReadStatusCount: break;
    }
    return "READ_UNKNOWN";
}

static const char* readFailureName(uint32_t code)
{
    switch ((CueletDiagnosticReadFailureCode)code) {
    case kCueletDiagnosticReadFailureNone: return "READ_OK";
    case kCueletDiagnosticReadFailureNotYetWritten:
        return "READ_NOT_YET_WRITTEN";
    case kCueletDiagnosticReadFailureOverwritten: return "READ_OVERWRITTEN";
    case kCueletDiagnosticReadFailureGenerationMismatch:
        return "READ_GENERATION_MISMATCH";
    case kCueletDiagnosticReadFailureAbsoluteTagMismatch:
        return "READ_ABSOLUTE_TAG_MISMATCH";
    case kCueletDiagnosticReadFailureUnpublished: return "READ_UNPUBLISHED";
    case kCueletDiagnosticReadFailureTimelineUninitialized:
        return "READ_TIMELINE_UNINITIALIZED";
    case kCueletDiagnosticReadFailureStreamInactive:
        return "READ_STREAM_INACTIVE";
    case kCueletDiagnosticReadFailureClientReaderUnavailable:
        return "READ_CLIENT_READER_UNAVAILABLE";
    case kCueletDiagnosticReadFailureMappingInvalid:
        return "READ_MAPPING_INVALID";
    case kCueletDiagnosticReadFailureInvalidArgument:
        return "READ_INVALID_ARGUMENT";
    case kCueletDiagnosticReadFailureSampleRateReset:
        return "READ_SAMPLE_RATE_RESET";
    case kCueletDiagnosticReadFailureCodeCount: break;
    }
    return "READ_FAILURE_UNKNOWN";
}

static const char* timelineResultName(uint32_t status)
{
    switch ((CueletTimelineStatus)status) {
    case kCueletTimelineOK: return "TIMELINE_OK";
    case kCueletTimelineInvalidArgument: return "TIMELINE_INVALID_ARGUMENT";
    case kCueletTimelineInputTimestampInvalid: return "TIMELINE_INPUT_INVALID";
    case kCueletTimelineOutputTimestampInvalid: return "TIMELINE_OUTPUT_INVALID";
    case kCueletTimelineNegativeSampleTime: return "TIMELINE_NEGATIVE_SAMPLE_TIME";
    case kCueletTimelineFractionalSampleTime: return "TIMELINE_FRACTIONAL_SAMPLE_TIME";
    case kCueletTimelineSampleTimeOverflow: return "TIMELINE_SAMPLE_TIME_OVERFLOW";
    case kCueletTimelineUninitialized: return "TIMELINE_UNINITIALIZED";
    case kCueletTimelineNegativeSourceRange: return "TIMELINE_NEGATIVE_SOURCE_RANGE";
    }
    return "TIMELINE_UNKNOWN";
}

static const char* dispositionName(uint32_t value)
{
    switch ((CueletDiagnosticOperationDisposition)value) {
    case kCueletDiagnosticOperationNormal: return "normal";
    case kCueletDiagnosticOperationMainBufferMissing: return "main_buffer_missing";
    case kCueletDiagnosticOperationWrongStream: return "wrong_stream";
    case kCueletDiagnosticOperationStreamInactive: return "stream_inactive";
    case kCueletDiagnosticOperationClientReaderMissing: return "client_reader_missing";
    case kCueletDiagnosticOperationUnsupported: return "unsupported_operation";
    }
    return "unknown";
}

static double doubleFromBits(uint64_t bits)
{
    double value = 0.0;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint64_t doubleBits(double value)
{
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static bool validateEventExportBytes(
    const void* bytes,
    size_t length)
{
    if (bytes == NULL || length < sizeof(CueletDiagnosticEventExportHeader)) {
        return false;
    }
    const CueletDiagnosticEventExportHeader* header = bytes;
    const uint64_t expectedLength = sizeof(*header) +
        (uint64_t)header->returnedEventCount *
            sizeof(CueletDiagnosticSnapshot);
    if (header->schemaVersion != CUELET_DIAGNOSTIC_SCHEMA_VERSION ||
        header->eventRecordSize != sizeof(CueletDiagnosticSnapshot) ||
        header->snapshotCapacity !=
            CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY ||
        header->returnedEventCount > header->snapshotCapacity ||
        header->availableEventCount > CUELET_DIAGNOSTIC_EVENT_CAPACITY ||
        expectedLength != length) {
        return false;
    }
    if (header->availableEventCount == 0) {
        return header->returnedEventCount == 0 &&
            header->firstReturnedSequence == 0 &&
            header->lastReturnedSequence == 0;
    }
    if (header->newestAvailableSequence <
            header->oldestAvailableSequence ||
        header->newestAvailableSequence -
                header->oldestAvailableSequence + 1U !=
            header->availableEventCount ||
        header->returnedEventCount == 0 ||
        header->firstReturnedSequence <
            header->oldestAvailableSequence ||
        header->lastReturnedSequence >
            header->newestAvailableSequence ||
        header->lastReturnedSequence -
                header->firstReturnedSequence + 1U !=
            header->returnedEventCount) {
        return false;
    }
    const CueletDiagnosticSnapshot* events =
        (const CueletDiagnosticSnapshot*)(header + 1);
    if (events[0].sequence != header->firstReturnedSequence ||
        events[header->returnedEventCount - 1U].sequence !=
            header->lastReturnedSequence) {
        return false;
    }
    for (uint32_t index = 1; index < header->returnedEventCount; ++index) {
        if (events[index].sequence != events[index - 1U].sequence + 1U) {
            return false;
        }
    }
    return true;
}

static int getEventSnapshot(
    AudioDeviceID device,
    void** allocationOut,
    CueletDiagnosticEventExportHeader** headerOut)
{
    const AudioObjectPropertyAddress property =
        CueletDiagnosticPropertyAddress(kCueletDiagnosticPropertyEvents);
    Boolean isSettable = true;
    const OSStatus settableStatus = AudioObjectIsPropertySettable(
        device, &property, &isSettable);
    if (settableStatus != noErr || isSettable) {
        char statusDescription[64];
        statusText(settableStatus, statusDescription);
        fprintf(stderr,
            "diagnostic event property is not read-only: device=%u "
            "selector=cqev settableStatus=%s settable=%u\n",
            device, statusDescription, isSettable);
        return 1;
    }
    CFPropertyListRef value = NULL;
    const OSStatus status = getPropertyList(
        device,
        kCueletDiagnosticPropertyEvents,
        true,
        &value);
    if (status != noErr) {
        return 1;
    }
    if (CFGetTypeID(value) != CFDataGetTypeID() ||
        CFDataGetLength((CFDataRef)value) <
            (CFIndex)sizeof(CueletDiagnosticEventExportHeader)) {
        fprintf(stderr, "diagnostic event property is not a valid CFData export\n");
        CFRelease(value);
        return 1;
    }
    const CFIndex length = CFDataGetLength((CFDataRef)value);
    const CueletDiagnosticEventExportHeader* sourceHeader =
        (const CueletDiagnosticEventExportHeader*)
            CFDataGetBytePtr((CFDataRef)value);
    const uint64_t expectedLength = sizeof(*sourceHeader) +
        (uint64_t)sourceHeader->returnedEventCount *
            sizeof(CueletDiagnosticSnapshot);
    if (!validateEventExportBytes(sourceHeader, (size_t)length)) {
        fprintf(stderr,
            "diagnostic event snapshot failed schema/length validation: "
            "schema=%u eventRecordSize=%u count=%u first=%" PRIu64
            " last=%" PRIu64 " bytes=%ld expected=%" PRIu64
            "\n",
            sourceHeader->schemaVersion, sourceHeader->eventRecordSize,
            sourceHeader->returnedEventCount,
            sourceHeader->firstReturnedSequence,
            sourceHeader->lastReturnedSequence,
            (long)length, expectedLength);
        CFRelease(value);
        return 1;
    }
    void* allocation = calloc(1, (size_t)length);
    if (allocation == NULL) {
        fprintf(stderr, "diagnostic event allocation failed\n");
        CFRelease(value);
        return 1;
    }
    memcpy(allocation, CFDataGetBytePtr((CFDataRef)value), (size_t)length);
    CFRelease(value);
    *allocationOut = allocation;
    *headerOut = allocation;
    return 0;
}

static void printCountersJSON(
    FILE* output,
    AudioDeviceID device,
    const CueletDiagnosticBuildInfo* build,
    const CueletDiagnosticCounters* counters)
{
    const uint64_t inspectorHostTime = mach_absolute_time();
    fprintf(output,
        "{\"type\":\"counters\",\"device\":%u,"
        "\"inspectorHostTime\":%" PRIu64 ","
        "\"version\":\"%u.%u.%u\",\"build\":%u,"
        "\"diagnosticEnabled\":%u,\"stateToken\":\"%016" PRIx64 "\","
        "\"ringToken\":\"%016" PRIx64 "\",\"nextSequence\":%" PRIu64 ","
        "\"eventCount\":%" PRIu64 ",\"droppedEvents\":%" PRIu64 ","
        "\"writeMixCalls\":%" PRIu64 ",\"writeNonzeroCalls\":%" PRIu64 ","
        "\"writeZeroCalls\":%" PRIu64 ",\"writeRequestedFrames\":%" PRIu64 ","
        "\"writeAcceptedFrames\":%" PRIu64 ",\"writeRejectedFrames\":%" PRIu64 ","
        "\"readInputCalls\":%" PRIu64 ",\"readRequestedFrames\":%" PRIu64 ","
        "\"readValidFrames\":%" PRIu64 ",\"readZeroFilledFrames\":%" PRIu64 ","
        "\"ringResets\":%" PRIu64 ",\"generationChanges\":%" PRIu64 ","
        "\"startIO\":%" PRIu64 ",\"stopIO\":%" PRIu64 ","
        "\"writeInputFrames\":%" PRIu64 ","
        "\"writeValidatedFrames\":%" PRIu64 ","
        "\"writeStoredPayloadFrames\":%" PRIu64 ","
        "\"writePublishedTagFrames\":%" PRIu64 ","
        "\"writePublicationFailures\":%" PRIu64 ","
        "\"readOKFrames\":%" PRIu64 ","
        "\"readNotYetWrittenFrames\":%" PRIu64 ","
        "\"readOverwrittenFrames\":%" PRIu64 ","
        "\"readGenerationMismatchFrames\":%" PRIu64 ","
        "\"readAbsoluteTagMismatchFrames\":%" PRIu64 ","
        "\"readUnpublishedFrames\":%" PRIu64 ","
        "\"readTimelineUninitializedFrames\":%" PRIu64 ","
        "\"readStreamInactiveFrames\":%" PRIu64 ","
        "\"readClientReaderUnavailableFrames\":%" PRIu64 ","
        "\"readMappingInvalidFrames\":%" PRIu64 ","
        "\"readInvalidArgumentFrames\":%" PRIu64 ","
        "\"readSampleRateResetFrames\":%" PRIu64 ","
        "\"readPartialValidFrames\":%" PRIu64 ","
        "\"readOKCalls\":%" PRIu64 ","
        "\"readPartialCalls\":%" PRIu64 ","
        "\"readAllUnavailableCalls\":%" PRIu64 ","
        "\"readMappingInvalidCalls\":%" PRIu64 ","
        "\"readMappedCalls\":%" PRIu64 ","
        "\"readGenerationResolvedCalls\":%" PRIu64 ","
        "\"readPreRingAcceptedCalls\":%" PRIu64 ","
        "\"readRingLookupCalls\":%" PRIu64 ","
        "\"readRingLookupFrames\":%" PRIu64 ","
        "\"readMappedButNoGenerationCalls\":%" PRIu64 ","
        "\"readGenerationButNoRingCalls\":%" PRIu64 ","
        "\"readRingLookupUnavailableCalls\":%" PRIu64,
        device, inspectorHostTime,
        build->versionMajor, build->versionMinor, build->versionPatch,
        build->buildNumber, build->diagnosticEnabled,
        counters->stateToken, counters->ringToken, counters->nextSequence,
        counters->availableEventCount, counters->droppedEventCount,
        counters->writeMixCallCount, counters->writeMixNonzeroCallCount,
        counters->writeMixZeroCallCount, counters->writeRequestedFrames,
        counters->writeAcceptedFrames, counters->writeRejectedFrames,
        counters->readInputCallCount, counters->readRequestedFrames,
        counters->readValidFrames, counters->readZeroFilledFrames,
        counters->ringResetCount, counters->generationChangeCount,
        counters->startIOCount, counters->stopIOCount,
        counters->writeInputFrames, counters->writeValidatedFrames,
        counters->writeStoredPayloadFrames, counters->writePublishedTagFrames,
        counters->writePublicationFailures, counters->readOKFrames,
        counters->readNotYetWrittenFrames, counters->readOverwrittenFrames,
        counters->readGenerationMismatchFrames,
        counters->readAbsoluteTagMismatchFrames,
        counters->readUnpublishedFrames,
        counters->readTimelineUninitializedFrames,
        counters->readStreamInactiveFrames,
        counters->readClientReaderUnavailableFrames,
        counters->readMappingInvalidFrames,
        counters->readInvalidArgumentFrames,
        counters->readSampleRateResetFrames,
        counters->readPartialValidFrames, counters->readOKCalls,
        counters->readPartialCalls, counters->readAllUnavailableCalls,
        counters->readMappingInvalidCalls,
        counters->readMappedCalls,
        counters->readGenerationResolvedCalls,
        counters->readPreRingAcceptedCalls,
        counters->readRingLookupCalls,
        counters->readRingLookupFrames,
        counters->readMappedButNoGenerationCalls,
        counters->readGenerationButNoRingCalls,
        counters->readRingLookupUnavailableCalls);
    fprintf(output, ",\"writeResultCalls\":{");
    for (uint32_t index = 0;
         index <= kCueletRingWriteInvalidArgument; ++index) {
        fprintf(output, "%s\"%s\":%" PRIu64,
            index == 0 ? "" : ",", writeResultName(index),
            counters->writeResultCounts[index]);
    }
    fprintf(output, "},\"readResultCalls\":{");
    for (uint32_t index = 0;
         index < kCueletRingReadStatusCount; ++index) {
        fprintf(output, "%s\"%s\":%" PRIu64,
            index == 0 ? "" : ",", readResultName(index),
            counters->readResultCounts[index]);
    }
    fprintf(output, "},\"timelineResultCalls\":{");
    for (uint32_t index = 0;
         index <= kCueletTimelineNegativeSourceRange; ++index) {
        fprintf(output, "%s\"%s\":%" PRIu64,
            index == 0 ? "" : ",", timelineResultName(index),
            counters->timelineResultCounts[index]);
    }
    const CueletDiagnosticReadFailureSummary* first =
        &counters->firstReadFailure;
    const CueletDiagnosticReadFailureSummary* last =
        &counters->lastReadFailure;
    fprintf(output,
        "},\"firstReadFailure\":{\"sequence\":%" PRIu64 ","
        "\"code\":\"%s\",\"inputStart\":%" PRIu64 ","
        "\"sourceStart\":%" PRIu64 ",\"frames\":%u,"
        "\"expectedGeneration\":%" PRIu64 ","
        "\"observedGeneration\":%" PRIu64 ","
        "\"expectedTag\":%" PRIu64 ",\"observedTag\":%" PRIu64 ","
        "\"slot\":%" PRIu64 ",\"timelineStatus\":\"%s\","
        "\"inputFlags\":%u,\"outputFlags\":%u,\"client\":%u},"
        "\"lastReadFailure\":{\"sequence\":%" PRIu64 ","
        "\"code\":\"%s\",\"inputStart\":%" PRIu64 ","
        "\"sourceStart\":%" PRIu64 ",\"frames\":%u,"
        "\"expectedGeneration\":%" PRIu64 ","
        "\"observedGeneration\":%" PRIu64 ","
        "\"expectedTag\":%" PRIu64 ",\"observedTag\":%" PRIu64 ","
        "\"slot\":%" PRIu64 ",\"timelineStatus\":\"%s\","
        "\"inputFlags\":%u,\"outputFlags\":%u,\"client\":%u},",
        first->sequence, readFailureName(first->code), first->inputStart,
        first->sourceStart, first->frameCount, first->expectedGeneration,
        first->observedGeneration, first->expectedTag, first->observedTag,
        first->slot, timelineResultName(first->timelineStatus),
        first->inputTimeFlags, first->outputTimeFlags, first->clientID,
        last->sequence, readFailureName(last->code), last->inputStart,
        last->sourceStart, last->frameCount, last->expectedGeneration,
        last->observedGeneration, last->expectedTag, last->observedTag,
        last->slot, timelineResultName(last->timelineStatus),
        last->inputTimeFlags, last->outputTimeFlags, last->clientID);
    fprintf(output,
        "\"lastAcceptedWrite\":{\"sequence\":%" PRIu64 ","
        "\"start\":%" PRIu64 ",\"frames\":%u,\"generation\":%" PRIu64 ","
        "\"firstTag\":%" PRIu64 ",\"finalTag\":%" PRIu64 "},"
        "\"lastPublishedWrite\":{\"sequence\":%" PRIu64 ","
        "\"start\":%" PRIu64 ",\"frames\":%u,\"generation\":%" PRIu64 ","
        "\"firstTag\":%" PRIu64 ",\"finalTag\":%" PRIu64 "},"
        "\"lastRead\":{\"sequence\":%" PRIu64 ","
        "\"inputStart\":%" PRIu64 ",\"sourceStart\":%" PRIu64 ","
        "\"frames\":%u,\"result\":\"%s\","
        "\"expectedGeneration\":%" PRIu64 ","
        "\"observedGeneration\":%" PRIu64 ","
        "\"expectedTag\":%" PRIu64 ",\"observedTag\":%" PRIu64 ","
        "\"timelineStatus\":\"%s\",\"inputFlags\":%u,"
        "\"outputFlags\":%u,\"client\":%u,\"mapped\":%u,"
        "\"generationResolved\":%u,\"preRingAccepted\":%u,"
        "\"ringLookupReached\":%u,\"readerInitiallyInitialized\":%u,"
        "\"readerGenerationAdopted\":%u},"
        "\"criticalWriteCount\":%u,\"criticalReadCount\":%u,"
        "\"criticalEventCount\":%u,\"criticalEvents\":[",
        counters->lastAcceptedWrite.sequence,
        counters->lastAcceptedWrite.start,
        counters->lastAcceptedWrite.frameCount,
        counters->lastAcceptedWrite.generation,
        counters->lastAcceptedWrite.firstTag,
        counters->lastAcceptedWrite.finalTag,
        counters->lastPublishedWrite.sequence,
        counters->lastPublishedWrite.start,
        counters->lastPublishedWrite.frameCount,
        counters->lastPublishedWrite.generation,
        counters->lastPublishedWrite.firstTag,
        counters->lastPublishedWrite.finalTag,
        counters->lastRead.sequence, counters->lastRead.inputStart,
        counters->lastRead.sourceStart, counters->lastRead.frameCount,
        readFailureName(counters->lastRead.resultCode),
        counters->lastRead.expectedGeneration,
        counters->lastRead.observedGeneration,
        counters->lastRead.expectedTag, counters->lastRead.observedTag,
        timelineResultName(counters->lastRead.timelineStatus),
        counters->lastRead.inputTimeFlags, counters->lastRead.outputTimeFlags,
        counters->lastRead.clientID, counters->lastRead.readMapped,
        counters->lastRead.readGenerationResolved,
        counters->lastRead.readPreRingAccepted,
        counters->lastRead.readRingLookupReached,
        counters->lastRead.readerInitiallyInitialized,
        counters->lastRead.readerGenerationAdopted,
        counters->criticalWriteCount, counters->criticalReadCount,
        counters->criticalEventCount);
    for (uint32_t index = 0; index < counters->criticalEventCount; ++index) {
        const CueletDiagnosticCriticalEvent* event =
            &counters->criticalEvents[index];
        fprintf(output,
            "%s{\"sequence\":%" PRIu64 ",\"kind\":%u,"
            "\"start\":%" PRIu64 ",\"frames\":%u,"
            "\"generation\":%" PRIu64 ",\"expectedTag\":%" PRIu64 ","
            "\"observedTag\":%" PRIu64 ",\"result\":\"%s\","
            "\"validFrames\":%u,\"zeroFilledFrames\":%u,"
            "\"timelineStatus\":\"%s\",\"inputFlags\":%u,"
            "\"outputFlags\":%u,\"client\":%u,\"mapped\":%u,"
            "\"generationResolved\":%u,\"preRingAccepted\":%u,"
            "\"ringLookupReached\":%u,\"readerInitiallyInitialized\":%u,"
            "\"readerGenerationAdopted\":%u,"
            "\"checksum\":\"%016" PRIx64 "\"}",
            index == 0 ? "" : ",", event->sequence, event->kind,
            event->absoluteStart, event->frameCount, event->generation,
            event->expectedTag, event->observedTag,
            event->kind == kCueletDiagnosticCriticalNonzeroWrite
                ? writeResultName(event->resultCode)
                : readFailureName(event->resultCode),
            event->validFrames, event->zeroFilledFrames,
            timelineResultName(event->timelineStatus),
            event->inputTimeFlags, event->outputTimeFlags, event->clientID,
            event->readMapped, event->readGenerationResolved,
            event->readPreRingAccepted, event->readRingLookupReached,
            event->readerInitiallyInitialized,
            event->readerGenerationAdopted,
            event->payloadChecksum);
    }
    fprintf(output, "]}\n");
}

static void printEventJSON(FILE* output, const CueletDiagnosticSnapshot* event)
{
    const CueletDiagnosticRecordData* data = &event->data;
    fprintf(output,
        "{\"type\":\"event\",\"sequence\":%" PRIu64 ","
        "\"event\":\"%s\",\"stateToken\":\"%016" PRIx64 "\","
        "\"ringToken\":\"%016" PRIx64 "\",\"device\":%u,\"stream\":%u,"
        "\"client\":%u,\"operation\":%u,\"cycle\":%" PRIu64 ","
        "\"frames\":%u,\"sampleRate\":%.3f,\"generation\":%" PRIu64 ","
        "\"currentFlags\":%u,\"currentSampleBits\":\"%016" PRIx64 "\","
        "\"currentSample\":%.9f,\"currentFrame\":%" PRIu64 ",\"currentStatus\":%u,"
        "\"inputFlags\":%u,\"inputSampleBits\":\"%016" PRIx64 "\","
        "\"inputSample\":%.9f,\"inputFrame\":%" PRIu64 ",\"inputStatus\":%u,"
        "\"outputFlags\":%u,\"outputSampleBits\":\"%016" PRIx64 "\","
        "\"outputSample\":%.9f,\"outputFrame\":%" PRIu64 ",\"outputStatus\":%u,"
        "\"mainBuffer\":%u,\"secondaryBuffer\":%u,\"selectedBuffer\":%u,"
        "\"bufferStatus\":%u,\"disposition\":\"%s\","
        "\"checksum\":\"%016" PRIx64 "\","
        "\"peakLeft\":%.9g,\"peakRight\":%.9g,\"rmsLeft\":%.9g,"
        "\"rmsRight\":%.9g,\"zeroFrames\":%u,\"nonzeroFrames\":%u,"
        "\"publishedChecksum\":\"%016" PRIx64 "\","
        "\"publishedPeakLeft\":%.9g,\"publishedPeakRight\":%.9g,"
        "\"publishedRMSLeft\":%.9g,\"publishedRMSRight\":%.9g,"
        "\"publishedZeroFrames\":%u,\"publishedNonzeroFrames\":%u,"
        "\"inputStart\":%" PRIu64 ",\"outputStart\":%" PRIu64 ","
        "\"sourceStart\":%" PRIu64 ",\"offset\":%" PRId64 ",\"delay\":%u,"
        "\"timelineStatus\":\"%s\",\"writeStatus\":\"%s\","
        "\"writeAccepted\":%u,\"readStatus\":\"%s\","
        "\"validFrames\":%u,\"zeroFilledFrames\":%u,"
        "\"firstRejection\":\"%s\",\"firstRejectedFrame\":%" PRIu64 ","
        "\"expectedGeneration\":%" PRIu64 ",\"observedGeneration\":%" PRIu64 ","
        "\"expectedTag\":%" PRIu64 ",\"observedTag\":%" PRIu64 ","
        "\"firstSlot\":%" PRIu64 ",\"finalSlot\":%" PRIu64 ","
        "\"firstPublishedTag\":%" PRIu64 ",\"finalPublishedTag\":%" PRIu64 ","
        "\"publishedGeneration\":%" PRIu64 ",\"runningBefore\":%" PRIu64 ","
        "\"runningAfter\":%" PRIu64 ",\"mapped\":%u,"
        "\"generationResolved\":%u,\"preRingAccepted\":%u,"
        "\"ringLookupReached\":%u,\"ringLookupFrames\":%u,"
        "\"readerInitiallyInitialized\":%u,"
        "\"readerGenerationAdopted\":%u,",
        event->sequence, eventName(event->eventKind), data->stateToken,
        data->ringToken, data->deviceObjectID, data->streamObjectID,
        data->clientID, data->operationID, data->cycleCounter, data->frameCount,
        data->sampleRate, data->resetGeneration,
        data->currentTimeFlags, data->currentSampleTimeBits,
        doubleFromBits(data->currentSampleTimeBits), data->currentSampleFrame,
        data->currentFrameConversionStatus,
        data->inputTimeFlags, data->inputSampleTimeBits,
        doubleFromBits(data->inputSampleTimeBits), data->inputSampleFrame,
        data->inputFrameConversionStatus,
        data->outputTimeFlags, data->outputSampleTimeBits,
        doubleFromBits(data->outputSampleTimeBits), data->outputSampleFrame,
        data->outputFrameConversionStatus,
        data->mainBufferPresent, data->secondaryBufferPresent,
        data->selectedBuffer, data->bufferSelectionStatus,
        dispositionName(data->operationDisposition),
        data->payloadChecksum, data->payloadPeakLeft, data->payloadPeakRight,
        data->payloadRMSLeft, data->payloadRMSRight,
        data->payloadZeroFrameCount, data->payloadNonzeroFrameCount,
        data->publishedPayloadChecksum,
        data->publishedPayloadPeakLeft, data->publishedPayloadPeakRight,
        data->publishedPayloadRMSLeft, data->publishedPayloadRMSRight,
        data->publishedPayloadZeroFrameCount,
        data->publishedPayloadNonzeroFrameCount,
        data->inputStartFrame, data->outputStartFrame, data->sourceStartFrame,
        data->observedTimelineOffsetFrames, data->loopbackDelayFrames,
        timelineResultName(data->timelineStatus),
        writeResultName(data->ringWriteStatus), data->ringWriteAcceptedFrames,
        readResultName(data->ringReadStatus), data->validFrameCount,
        data->zeroFilledFrameCount,
        readResultName(data->ringReadFirstRejection),
        data->ringReadFirstRejectedFrame, data->expectedGeneration,
        data->observedGeneration, data->expectedAbsoluteTag,
        data->observedAbsoluteTag, data->firstRingSlot, data->finalRingSlot,
        data->firstPublishedAbsoluteTag, data->finalPublishedAbsoluteTag,
        data->publishedGeneration, data->runningClientCountBefore,
        data->runningClientCountAfter, data->readMapped,
        data->readGenerationResolved, data->readPreRingAccepted,
        data->readRingLookupReached, data->readRingLookupFrames,
        data->readerInitiallyInitialized, data->readerGenerationAdopted);
    fprintf(output,
        "\"writeInputFrames\":%u,\"writeValidatedFrames\":%u,"
        "\"writeStoredPayloadFrames\":%u,\"writePublishedTagFrames\":%u,"
        "\"writePublicationFailures\":%u,\"readFailureFrames\":{",
        data->writeInputFrames, data->writeValidatedFrames,
        data->writeStoredPayloadFrames, data->writePublishedTagFrames,
        data->writePublicationFailures);
    for (uint32_t code = 0;
         code < kCueletDiagnosticReadFailureCodeCount; ++code) {
        fprintf(output, "%s\"%s\":%u",
            code == 0 ? "" : ",", readFailureName(code),
            data->readFailureFrameCounts[code]);
    }
    fprintf(output, "}}\n");
}

typedef struct CueletInspectorEventCursor {
    bool initialized;
    bool hasLastConsumedSequence;
    uint64_t lastConsumedSequence;
    uint64_t nextExpectedSequence;
} CueletInspectorEventCursor;

typedef struct CueletInspectorConsumeResult {
    uint64_t emittedEvents;
    uint64_t missingEvents;
    uint64_t firstMissingSequence;
    bool hasGap;
} CueletInspectorConsumeResult;

static void recordEventGap(
    FILE* output,
    CueletInspectorConsumeResult* result,
    uint64_t firstMissing,
    uint64_t count)
{
    if (count == 0) return;
    if (!result->hasGap) {
        result->hasGap = true;
        result->firstMissingSequence = firstMissing;
    }
    result->missingEvents += count;
    if (output != NULL) {
        fprintf(output,
            "{\"type\":\"event_gap\",\"firstMissing\":%" PRIu64
            ",\"count\":%" PRIu64 "}\n",
            firstMissing, count);
    }
}

static bool consumeEventSnapshot(
    const CueletDiagnosticEventExportHeader* header,
    size_t length,
    CueletInspectorEventCursor* cursor,
    FILE* output,
    CueletInspectorConsumeResult* resultOut)
{
    if (cursor == NULL || resultOut == NULL ||
        !validateEventExportBytes(header, length)) {
        return false;
    }
    CueletInspectorConsumeResult result = {0};
    if (!cursor->initialized) {
        cursor->initialized = true;
        cursor->nextExpectedSequence = header->oldestAvailableSequence;
    }
    if (header->oldestAvailableSequence > cursor->nextExpectedSequence) {
        recordEventGap(
            output,
            &result,
            cursor->nextExpectedSequence,
            header->oldestAvailableSequence - cursor->nextExpectedSequence);
        cursor->nextExpectedSequence = header->oldestAvailableSequence;
    }
    const CueletDiagnosticSnapshot* events =
        (const CueletDiagnosticSnapshot*)(header + 1);
    for (uint32_t index = 0; index < header->returnedEventCount; ++index) {
        const uint64_t sequence = events[index].sequence;
        if ((cursor->hasLastConsumedSequence &&
             sequence <= cursor->lastConsumedSequence) ||
            sequence < cursor->nextExpectedSequence) {
            continue;
        }
        if (sequence > cursor->nextExpectedSequence) {
            recordEventGap(
                output,
                &result,
                cursor->nextExpectedSequence,
                sequence - cursor->nextExpectedSequence);
        }
        if (output != NULL) printEventJSON(output, &events[index]);
        ++result.emittedEvents;
        cursor->hasLastConsumedSequence = true;
        cursor->lastConsumedSequence = sequence;
        cursor->nextExpectedSequence = sequence + 1U;
    }
    *resultOut = result;
    return true;
}

static unsigned eventWatchIterationCount(
    double seconds,
    unsigned intervalMilliseconds)
{
    return (unsigned)(seconds * 1000.0 / intervalMilliseconds) + 1U;
}

static int readStatus(
    AudioDeviceID device,
    CueletDiagnosticSchema* schema,
    CueletDiagnosticBuildInfo* build,
    CueletDiagnosticCounters* counters)
{
    int failures = 0;
    failures += verifyCustomPropertyInfo(device) != noErr;
    failures += getDataProperty(
        device, kCueletDiagnosticPropertySchema, true,
        schema, sizeof(*schema)) != noErr;
    failures += getDataProperty(
        device, kCueletDiagnosticPropertyBuild, true,
        build, sizeof(*build)) != noErr;
    failures += getDataPropertyPrefix(
        device, kCueletDiagnosticPropertyCounters, true,
        counters,
        offsetof(CueletDiagnosticCounters, writeInputFrames),
        sizeof(*counters)) != noErr;

    CFPropertyListRef enabled = NULL;
    const OSStatus enabledStatus = getPropertyList(
        device, kCueletDiagnosticPropertyEnabled, false, &enabled);
    if (enabledStatus == noErr) {
        if (CFGetTypeID(enabled) != CFBooleanGetTypeID() ||
            !CFBooleanGetValue((CFBooleanRef)enabled)) {
            fprintf(stderr,
                "optional diagnostic-enabled property does not report true\n");
        }
        CFRelease(enabled);
    }
    return failures == 0 ? 0 : 1;
}

static int commandStatus(AudioDeviceID device)
{
    CueletDiagnosticSchema schema = {0};
    CueletDiagnosticBuildInfo build = {0};
    CueletDiagnosticCounters counters = {0};
    if (readStatus(device, &schema, &build, &counters) != 0) {
        return 1;
    }
    printCountersJSON(stdout, device, &build, &counters);
    fprintf(stderr,
        "Cuelet diagnostics: schema=%u capacity=%u eventSize=%u maxAnalyzed=%u\n",
        schema.schemaVersion, schema.eventCapacity, schema.eventSize,
        schema.maximumAnalyzedFrames);
    return 0;
}

static int commandClear(AudioDeviceID device)
{
    const AudioObjectPropertyAddress property = CueletDiagnosticPropertyAddress(
        kCueletDiagnosticPropertyClear);
    const CFPropertyListRef clear = kCFBooleanTrue;
    const OSStatus status = AudioObjectSetPropertyData(
        device, &property, 0, NULL, sizeof(clear), &clear);
    if (status != noErr) {
        char statusDescription[64];
        statusText(status, statusDescription);
        fprintf(stderr,
            "diagnostic clear failed: device=%u uid=%s selector=cdcl "
            "scope=global element=main status=%s\n",
            device, CUELET_DRIVER_DEVICE_UID, statusDescription);
        return 1;
    }
    printf("{\"type\":\"clear\",\"status\":\"ok\"}\n");
    return 0;
}

static int commandSnapshot(AudioDeviceID device, const char* outputPath)
{
    CueletDiagnosticSchema schema = {0};
    CueletDiagnosticBuildInfo build = {0};
    CueletDiagnosticCounters counters = {0};
    if (readStatus(device, &schema, &build, &counters) != 0) return 1;
    FILE* output = stdout;
    const bool closeOutput = outputPath != NULL;
    if (outputPath != NULL) {
        output = fopen(outputPath, "w");
        if (output == NULL) {
            fprintf(stderr, "cannot open %s: %s\n", outputPath, strerror(errno));
            return 1;
        }
    }
    void* allocation = NULL;
    CueletDiagnosticEventExportHeader* header = NULL;
    if (getEventSnapshot(device, &allocation, &header) != 0) {
        if (closeOutput) fclose(output);
        return 1;
    }
    const uint64_t omitted = header->availableEventCount >=
            header->returnedEventCount
        ? header->availableEventCount - header->returnedEventCount : 0;
    fprintf(output,
        "{\"type\":\"snapshot\",\"schema\":%u,"
        "\"eventRecordSize\":%u,\"snapshotCapacity\":%u,"
        "\"oldestAvailableSequence\":%" PRIu64 ","
        "\"newestAvailableSequence\":%" PRIu64 ","
        "\"firstReturnedSequence\":%" PRIu64 ","
        "\"lastReturnedSequence\":%" PRIu64 ","
        "\"returnedEventCount\":%u,\"availableEventCount\":%" PRIu64 ","
        "\"omittedOlderEvents\":%" PRIu64 ",\"droppedEvents\":%" PRIu64
        "}\n",
        header->schemaVersion, header->eventRecordSize,
        header->snapshotCapacity, header->oldestAvailableSequence,
        header->newestAvailableSequence, header->firstReturnedSequence,
        header->lastReturnedSequence, header->returnedEventCount,
        header->availableEventCount, omitted, header->droppedEventCount);
    const CueletDiagnosticSnapshot* events =
        (const CueletDiagnosticSnapshot*)(header + 1);
    for (uint32_t index = 0; index < header->returnedEventCount; ++index) {
        printEventJSON(output, &events[index]);
    }
    if (closeOutput) {
        fclose(output);
    }
    fprintf(stderr,
        "Cuelet diagnostic snapshot: events=%u omitted=%" PRIu64
        " dropped=%" PRIu64 "%s%s\n",
        header->returnedEventCount, omitted, header->droppedEventCount,
        outputPath == NULL ? "" : " output=",
        outputPath == NULL ? "" : outputPath);
    free(allocation);
    return 0;
}

static int commandProbeEventPage(AudioDeviceID device)
{
    CueletDiagnosticSchema schema = {0};
    if (getDataProperty(
            device,
            kCueletDiagnosticPropertySchema,
            true,
            &schema,
            sizeof(schema)) != noErr) {
        return 1;
    }
    if (schema.schemaVersion != CUELET_DIAGNOSTIC_SCHEMA_VERSION) return 1;
    void* allocation = NULL;
    CueletDiagnosticEventExportHeader* header = NULL;
    if (getEventSnapshot(device, &allocation, &header) != 0) return 1;
    printf(
        "{\"type\":\"event-snapshot-probe\",\"status\":\"ok\","
        "\"schema\":%u,\"eventRecordSize\":%u,\"eventCount\":%u,"
        "\"bytes\":%ld}\n",
        header->schemaVersion,
        header->eventRecordSize,
        header->returnedEventCount,
        (long)(sizeof(*header) +
            header->returnedEventCount * sizeof(CueletDiagnosticSnapshot)));
    free(allocation);
    return 0;
}

static int commandSummarize(AudioDeviceID device)
{
    CueletDiagnosticSchema schema = {0};
    CueletDiagnosticBuildInfo build = {0};
    CueletDiagnosticCounters counters = {0};
    if (readStatus(device, &schema, &build, &counters) != 0) {
        return 1;
    }
    printf("diagnostic_version=%u.%u.%u build=%u\n",
        build.versionMajor, build.versionMinor, build.versionPatch,
        build.buildNumber);
    printf("write_mix_called=%s count=%" PRIu64 " nonzero=%" PRIu64
           " input_frames=%" PRIu64 " stored_frames=%" PRIu64
           " published_tag_frames=%" PRIu64
           " publication_failures=%" PRIu64 "\n",
        counters.writeMixCallCount > 0 ? "yes" : "no",
        counters.writeMixCallCount, counters.writeMixNonzeroCallCount,
        counters.writeInputFrames, counters.writeStoredPayloadFrames,
        counters.writePublishedTagFrames,
        counters.writePublicationFailures);
    printf("read_input_called=%s count=%" PRIu64 " valid_frames=%" PRIu64
           " zero_filled_frames=%" PRIu64 " ok_calls=%" PRIu64
           " partial_calls=%" PRIu64 " unavailable_calls=%" PRIu64 "\n",
        counters.readInputCallCount > 0 ? "yes" : "no",
        counters.readInputCallCount, counters.readValidFrames,
        counters.readZeroFilledFrames, counters.readOKCalls,
        counters.readPartialCalls, counters.readAllUnavailableCalls);
    printf("state_token=%016" PRIx64 " ring_token=%016" PRIx64
           " resets=%" PRIu64 " generation_changes=%" PRIu64
           " dropped_events=%" PRIu64 "\n",
        counters.stateToken, counters.ringToken,
        counters.ringResetCount, counters.generationChangeCount,
        counters.droppedEventCount);
    for (uint32_t index = 0; index < CUELET_DIAGNOSTIC_WRITE_RESULT_COUNT; ++index) {
        if (counters.writeResultCounts[index] > 0) {
            printf("write_result[%s]=%" PRIu64 "\n",
                writeResultName(index), counters.writeResultCounts[index]);
        }
    }
    for (uint32_t index = 0; index < CUELET_DIAGNOSTIC_READ_RESULT_COUNT; ++index) {
        if (counters.readResultCounts[index] > 0) {
            printf("read_result[%s]=%" PRIu64 "\n",
                readResultName(index), counters.readResultCounts[index]);
        }
    }
    printf("read_rejection[READ_NOT_YET_WRITTEN]=%" PRIu64 "\n",
        counters.readNotYetWrittenFrames);
    printf("read_rejection[READ_OVERWRITTEN]=%" PRIu64 "\n",
        counters.readOverwrittenFrames);
    printf("read_rejection[READ_GENERATION_MISMATCH]=%" PRIu64 "\n",
        counters.readGenerationMismatchFrames);
    printf("read_rejection[READ_ABSOLUTE_TAG_MISMATCH]=%" PRIu64 "\n",
        counters.readAbsoluteTagMismatchFrames);
    printf("read_rejection[READ_UNPUBLISHED]=%" PRIu64 "\n",
        counters.readUnpublishedFrames);
    printf("read_rejection[READ_TIMELINE_UNINITIALIZED]=%" PRIu64 "\n",
        counters.readTimelineUninitializedFrames);
    printf("read_rejection[READ_STREAM_INACTIVE]=%" PRIu64 "\n",
        counters.readStreamInactiveFrames);
    printf("read_rejection[READ_CLIENT_READER_UNAVAILABLE]=%" PRIu64 "\n",
        counters.readClientReaderUnavailableFrames);
    printf("read_rejection[READ_MAPPING_INVALID]=%" PRIu64 "\n",
        counters.readMappingInvalidFrames);
    printf("read_rejection[READ_INVALID_ARGUMENT]=%" PRIu64 "\n",
        counters.readInvalidArgumentFrames);
    printf("read_progress mapped=%" PRIu64 " generation_resolved=%" PRIu64
           " pre_ring_accepted=%" PRIu64 " ring_lookup=%" PRIu64
           " ring_lookup_frames=%" PRIu64
           " mapped_no_generation=%" PRIu64
           " generation_no_ring=%" PRIu64
           " ring_lookup_unavailable=%" PRIu64 "\n",
        counters.readMappedCalls, counters.readGenerationResolvedCalls,
        counters.readPreRingAcceptedCalls, counters.readRingLookupCalls,
        counters.readRingLookupFrames,
        counters.readMappedButNoGenerationCalls,
        counters.readGenerationButNoRingCalls,
        counters.readRingLookupUnavailableCalls);
    printf("first_read_failure=%s timeline=%s input_flags=%u output_flags=%u "
           "input=%" PRIu64 " source=%" PRIu64
           " expected_generation=%" PRIu64 " observed_generation=%" PRIu64
           " expected_tag=%" PRIu64 " observed_tag=%" PRIu64 "\n",
        readFailureName(counters.firstReadFailure.code),
        timelineResultName(counters.firstReadFailure.timelineStatus),
        counters.firstReadFailure.inputTimeFlags,
        counters.firstReadFailure.outputTimeFlags,
        counters.firstReadFailure.inputStart,
        counters.firstReadFailure.sourceStart,
        counters.firstReadFailure.expectedGeneration,
        counters.firstReadFailure.observedGeneration,
        counters.firstReadFailure.expectedTag,
        counters.firstReadFailure.observedTag);
    printf("last_read_failure=%s timeline=%s input_flags=%u output_flags=%u "
           "input=%" PRIu64 " source=%" PRIu64
           " expected_generation=%" PRIu64 " observed_generation=%" PRIu64
           " expected_tag=%" PRIu64 " observed_tag=%" PRIu64 "\n",
        readFailureName(counters.lastReadFailure.code),
        timelineResultName(counters.lastReadFailure.timelineStatus),
        counters.lastReadFailure.inputTimeFlags,
        counters.lastReadFailure.outputTimeFlags,
        counters.lastReadFailure.inputStart,
        counters.lastReadFailure.sourceStart,
        counters.lastReadFailure.expectedGeneration,
        counters.lastReadFailure.observedGeneration,
        counters.lastReadFailure.expectedTag,
        counters.lastReadFailure.observedTag);
    printf("last_accepted_write=[%" PRIu64 ",%" PRIu64
           ") generation=%" PRIu64 "\n",
        counters.lastAcceptedWrite.start,
        counters.lastAcceptedWrite.start +
            counters.lastAcceptedWrite.frameCount,
        counters.lastAcceptedWrite.generation);
    printf("last_published_write=[%" PRIu64 ",%" PRIu64
           ") generation=%" PRIu64 " last_resolved_read=[%" PRIu64
           ",%" PRIu64 ") expected_generation=%" PRIu64
           " timeline=%s input_flags=%u output_flags=%u\n",
        counters.lastPublishedWrite.start,
        counters.lastPublishedWrite.start +
            counters.lastPublishedWrite.frameCount,
        counters.lastPublishedWrite.generation,
        counters.lastRead.sourceStart,
        counters.lastRead.sourceStart + counters.lastRead.frameCount,
        counters.lastRead.expectedGeneration,
        timelineResultName(counters.lastRead.timelineStatus),
        counters.lastRead.inputTimeFlags,
        counters.lastRead.outputTimeFlags);
    return 0;
}

static int commandWatch(
    AudioDeviceID device,
    double seconds,
    unsigned intervalMilliseconds,
    const char* outputPath)
{
    const bool closeOutput = outputPath != NULL;
    FILE* output = closeOutput ? fopen(outputPath, "w") : stdout;
    if (output == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", outputPath, strerror(errno));
        return 1;
    }
    const unsigned iterations = eventWatchIterationCount(
        seconds, intervalMilliseconds);
    const struct timespec delay = {
        .tv_sec = intervalMilliseconds / 1000U,
        .tv_nsec = (long)(intervalMilliseconds % 1000U) * 1000000L,
    };
    for (unsigned index = 0; index < iterations; ++index) {
        CueletDiagnosticSchema schema = {0};
        CueletDiagnosticBuildInfo build = {0};
        CueletDiagnosticCounters counters = {0};
        if (readStatus(device, &schema, &build, &counters) != 0) {
            if (closeOutput) fclose(output);
            return 1;
        }
        printCountersJSON(output, device, &build, &counters);
        fflush(output);
        if (index + 1 < iterations) {
            nanosleep(&delay, NULL);
        }
    }
    if (closeOutput) {
        fclose(output);
    }
    return 0;
}

static int commandWatchEvents(
    AudioDeviceID device,
    double seconds,
    unsigned intervalMilliseconds,
    const char* outputPath)
{
    FILE* output = fopen(outputPath, "w");
    if (output == NULL) {
        fprintf(stderr, "cannot open %s: %s\n", outputPath, strerror(errno));
        return 1;
    }
    const unsigned iterations = eventWatchIterationCount(
        seconds, intervalMilliseconds);
    const struct timespec delay = {
        .tv_sec = intervalMilliseconds / 1000U,
        .tv_nsec = (long)(intervalMilliseconds % 1000U) * 1000000L,
    };
    CueletDiagnosticSchema schema = {0};
    CueletDiagnosticBuildInfo build = {0};
    CueletDiagnosticCounters counters = {0};
    if (readStatus(device, &schema, &build, &counters) != 0) {
        fclose(output);
        return 1;
    }
    uint64_t copiedEvents = 0;
    uint64_t missingEvents = 0;
    bool wroteHeader = false;
    CueletInspectorEventCursor cursor = {0};
    for (unsigned iteration = 0; iteration < iterations; ++iteration) {
        void* allocation = NULL;
        CueletDiagnosticEventExportHeader* header = NULL;
        if (getEventSnapshot(device, &allocation, &header) != 0) {
            fclose(output);
            return 1;
        }
        if (!wroteHeader) {
            fprintf(output,
                "{\"type\":\"event_watch\",\"schema\":%u,"
                "\"intervalMilliseconds\":%u,\"snapshotCapacity\":%u}\n",
                header->schemaVersion, intervalMilliseconds,
                header->snapshotCapacity);
            wroteHeader = true;
        }
        const size_t length = sizeof(*header) +
            (size_t)header->returnedEventCount *
                sizeof(CueletDiagnosticSnapshot);
        CueletInspectorConsumeResult consumed = {0};
        if (!consumeEventSnapshot(
                header, length, &cursor, output, &consumed)) {
            free(allocation);
            fclose(output);
            return 1;
        }
        copiedEvents += consumed.emittedEvents;
        missingEvents += consumed.missingEvents;
        free(allocation);
        fflush(output);
        if (iteration + 1 < iterations) {
            nanosleep(&delay, NULL);
        }
    }
    fclose(output);
    fprintf(stderr,
        "Cuelet event watch: copied=%" PRIu64 " missing=%" PRIu64
        " output=%s\n",
        copiedEvents, missingEvents, outputPath);
    return missingEvents == 0 ? 0 : 2;
}

static int commandSelftest(void)
{
    if (!supportedPropertyListSize(sizeof(CFPropertyListRef)) ||
        supportedPropertyListSize(sizeof(UInt32)) ||
        supportedPropertyListSize(0) ||
        supportedPropertyListSize(sizeof(UInt64) + sizeof(UInt32))) {
        fprintf(stderr, "CFPropertyList size proxy validation failed\n");
        return 1;
    }
    struct {
        CueletDiagnosticEventExportHeader header;
        CueletDiagnosticSnapshot event;
    } exportFixture = {
        .header = {
            .schemaVersion = CUELET_DIAGNOSTIC_SCHEMA_VERSION,
            .eventRecordSize = sizeof(CueletDiagnosticSnapshot),
            .snapshotCapacity = CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY,
            .returnedEventCount = 1,
            .oldestAvailableSequence = 19,
            .newestAvailableSequence = 19,
            .firstReturnedSequence = 19,
            .lastReturnedSequence = 19,
            .availableEventCount = 1,
        },
        .event = {.sequence = 19},
    };
    if (!validateEventExportBytes(&exportFixture, sizeof(exportFixture))) {
        fprintf(stderr, "event export decoder rejected a valid snapshot\n");
        return 1;
    }
    exportFixture.header.returnedEventCount = 2;
    if (validateEventExportBytes(&exportFixture, sizeof(exportFixture))) {
        fprintf(stderr, "event export decoder accepted an invalid snapshot\n");
        return 1;
    }
    exportFixture.header.returnedEventCount = 1;

    struct {
        CueletDiagnosticEventExportHeader header;
        CueletDiagnosticSnapshot events[4];
    } cursorFixture = {
        .header = {
            .schemaVersion = CUELET_DIAGNOSTIC_SCHEMA_VERSION,
            .eventRecordSize = sizeof(CueletDiagnosticSnapshot),
            .snapshotCapacity = CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY,
            .returnedEventCount = 3,
            .oldestAvailableSequence = 10,
            .newestAvailableSequence = 12,
            .firstReturnedSequence = 10,
            .lastReturnedSequence = 12,
            .availableEventCount = 3,
        },
        .events = {
            {.sequence = 10}, {.sequence = 11}, {.sequence = 12},
        },
    };
    CueletInspectorEventCursor cursor = {0};
    CueletInspectorConsumeResult consumed = {0};
    size_t cursorLength = sizeof(cursorFixture.header) +
        3U * sizeof(CueletDiagnosticSnapshot);
    if (!consumeEventSnapshot(
            &cursorFixture.header, cursorLength, &cursor, NULL, &consumed) ||
        consumed.emittedEvents != 3 || consumed.missingEvents != 0 ||
        !cursor.hasLastConsumedSequence || cursor.lastConsumedSequence != 12) {
        fprintf(stderr, "event cursor rejected first poll\n");
        return 1;
    }
    consumed = (CueletInspectorConsumeResult){0};
    if (!consumeEventSnapshot(
            &cursorFixture.header, cursorLength, &cursor, NULL, &consumed) ||
        consumed.emittedEvents != 0 || consumed.missingEvents != 0 ||
        cursor.lastConsumedSequence != 12) {
        fprintf(stderr, "event cursor failed repeated-poll deduplication\n");
        return 1;
    }
    cursorFixture.header.returnedEventCount = 4;
    cursorFixture.header.newestAvailableSequence = 14;
    cursorFixture.header.firstReturnedSequence = 11;
    cursorFixture.header.lastReturnedSequence = 14;
    cursorFixture.header.availableEventCount = 5;
    cursorFixture.events[0].sequence = 11;
    cursorFixture.events[1].sequence = 12;
    cursorFixture.events[2].sequence = 13;
    cursorFixture.events[3].sequence = 14;
    cursorLength = sizeof(cursorFixture);
    consumed = (CueletInspectorConsumeResult){0};
    if (!consumeEventSnapshot(
            &cursorFixture.header, cursorLength, &cursor, NULL, &consumed) ||
        consumed.emittedEvents != 2 || consumed.missingEvents != 0 ||
        cursor.lastConsumedSequence != 14) {
        fprintf(stderr, "event cursor failed overlapping-window progression\n");
        return 1;
    }
    cursorFixture.header.returnedEventCount = 2;
    cursorFixture.header.oldestAvailableSequence = 20;
    cursorFixture.header.newestAvailableSequence = 21;
    cursorFixture.header.firstReturnedSequence = 20;
    cursorFixture.header.lastReturnedSequence = 21;
    cursorFixture.header.availableEventCount = 2;
    cursorFixture.header.droppedEventCount = 5;
    cursorFixture.events[0].sequence = 20;
    cursorFixture.events[1].sequence = 21;
    cursorLength = sizeof(cursorFixture.header) +
        2U * sizeof(CueletDiagnosticSnapshot);
    consumed = (CueletInspectorConsumeResult){0};
    if (!consumeEventSnapshot(
            &cursorFixture.header, cursorLength, &cursor, NULL, &consumed) ||
        consumed.emittedEvents != 2 || consumed.missingEvents != 5 ||
        !consumed.hasGap || consumed.firstMissingSequence != 15 ||
        cursor.lastConsumedSequence != 21) {
        fprintf(stderr, "event cursor failed sequence-gap detection\n");
        return 1;
    }
    CueletInspectorEventCursor wrappedCursor = {0};
    cursorFixture.header.returnedEventCount = 3;
    cursorFixture.header.oldestAvailableSequence = 100;
    cursorFixture.header.newestAvailableSequence = 102;
    cursorFixture.header.firstReturnedSequence = 100;
    cursorFixture.header.lastReturnedSequence = 102;
    cursorFixture.header.availableEventCount = 3;
    cursorFixture.header.droppedEventCount = 500;
    cursorFixture.events[0].sequence = 100;
    cursorFixture.events[1].sequence = 101;
    cursorFixture.events[2].sequence = 102;
    cursorLength = sizeof(cursorFixture.header) +
        3U * sizeof(CueletDiagnosticSnapshot);
    consumed = (CueletInspectorConsumeResult){0};
    if (!consumeEventSnapshot(
            &cursorFixture.header, cursorLength, &wrappedCursor,
            NULL, &consumed) ||
        consumed.emittedEvents != 3 || consumed.missingEvents != 0 ||
        wrappedCursor.lastConsumedSequence != 102) {
        fprintf(stderr, "event cursor failed wrapped-store snapshot\n");
        return 1;
    }
    cursorFixture.events[1].sequence = 22;
    if (validateEventExportBytes(&cursorFixture, cursorLength)) {
        fprintf(stderr, "event decoder accepted malformed ordering\n");
        return 1;
    }
    cursorFixture.events[1].sequence = 101;
    cursorFixture.header.schemaVersion += 1U;
    if (validateEventExportBytes(&cursorFixture, cursorLength)) {
        fprintf(stderr, "event decoder accepted malformed schema\n");
        return 1;
    }
    cursorFixture.header.schemaVersion = CUELET_DIAGNOSTIC_SCHEMA_VERSION;
    if (eventWatchIterationCount(1.0, 50) != 21U) {
        fprintf(stderr, "event watch termination count is invalid\n");
        return 1;
    }
    CueletDiagnosticSnapshot write = {
        .sequence = 1,
        .eventKind = kCueletDiagnosticWriteMix,
        .data = {
            .stateToken = UINT64_C(0x0102030405060708),
            .ringToken = UINT64_C(0x1112131415161718),
            .deviceObjectID = kCueletObjectDevice,
            .streamObjectID = kCueletObjectOutputStream,
            .clientID = 42,
            .operationID = kAudioServerPlugInIOOperationWriteMix,
            .frameCount = 512,
            .sampleRate = 48000.0,
            .outputTimeFlags = kAudioTimeStampSampleTimeValid,
            .outputSampleTimeBits = 0,
            .outputSampleFrame = 123064,
            .outputFrameConversionStatus = kCueletTimelineOK,
            .mainBufferPresent = 1,
            .selectedBuffer = kCueletDiagnosticBufferMain,
            .operationDisposition = kCueletDiagnosticOperationNormal,
            .payloadChecksum = UINT64_C(16609893262586320761),
            .payloadPeakLeft = 0.25F,
            .payloadPeakRight = 0.25F,
            .payloadRMSLeft = 0.1766F,
            .payloadRMSRight = 0.1768F,
            .payloadNonzeroFrameCount = 512,
            .publishedPayloadChecksum = UINT64_C(16609893262586320761),
            .publishedPayloadPeakLeft = 0.25F,
            .publishedPayloadPeakRight = 0.25F,
            .publishedPayloadRMSLeft = 0.1766F,
            .publishedPayloadRMSRight = 0.1768F,
            .publishedPayloadNonzeroFrameCount = 512,
            .outputStartFrame = 123064,
            .timelineStatus = kCueletTimelineOK,
            .ringWriteStatus = kCueletRingWriteOK,
            .ringWriteAcceptedFrames = 512,
            .resetGeneration = 1,
            .publishedGeneration = 1,
            .firstRingSlot = 8680,
            .finalRingSlot = 9191,
            .firstPublishedAbsoluteTag = 123064,
            .finalPublishedAbsoluteTag = 123575,
        },
    };
    write.data.outputSampleTimeBits = doubleBits(123064.0);
    CueletDiagnosticSnapshot read = {
        .sequence = 2,
        .eventKind = kCueletDiagnosticReadInput,
        .data = {
            .stateToken = UINT64_C(0x0102030405060708),
            .ringToken = UINT64_C(0x1112131415161718),
            .deviceObjectID = kCueletObjectDevice,
            .streamObjectID = kCueletObjectInputStream,
            .clientID = 41,
            .operationID = kAudioServerPlugInIOOperationReadInput,
            .frameCount = 512,
            .sampleRate = 48000.0,
            .mainBufferPresent = 1,
            .selectedBuffer = kCueletDiagnosticBufferMain,
            .operationDisposition = kCueletDiagnosticOperationNormal,
            .payloadChecksum = UINT64_C(16609893262586320761),
            .payloadPeakLeft = 0.25F,
            .payloadPeakRight = 0.25F,
            .payloadRMSLeft = 0.1766F,
            .payloadRMSRight = 0.1768F,
            .payloadNonzeroFrameCount = 512,
            .inputStartFrame = 122880,
            .outputStartFrame = 123064,
            .sourceStartFrame = 122552,
            .observedTimelineOffsetFrames = 184,
            .loopbackDelayFrames = 512,
            .mappingValid = 1,
            .timelineStatus = kCueletTimelineOK,
            .ringReadStatus = kCueletRingReadOK,
            .validFrameCount = 512,
            .expectedGeneration = 1,
            .resetGeneration = 1,
            .firstRingSlot = 8168,
            .finalRingSlot = 8679,
        },
    };
    printf("{\"type\":\"selftest\",\"synthetic\":true,"
           "\"schema\":%u,\"eventDecoder\":\"ok\","
           "\"status\":\"ok\"}\n",
        CUELET_DIAGNOSTIC_SCHEMA_VERSION);
    printEventJSON(stdout, &write);
    printEventJSON(stdout, &read);
    return 0;
}

static void usage(const char* command)
{
    fprintf(stderr,
        "usage:\n"
        "  %s probe-properties\n"
        "  %s status\n"
        "  %s clear\n"
        "  %s snapshot [jsonl-path]\n"
        "  %s probe-event-page\n"
        "  %s summarize\n"
        "  %s watch <seconds> <interval-ms> [jsonl-path]\n"
        "  %s watch-events <seconds> <interval-ms> <jsonl-path>\n"
        "  %s selftest\n",
        command, command, command, command, command, command, command, command,
        command);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        usage(argv[0]);
        return 64;
    }
    if (strcmp(argv[1], "selftest") == 0 && argc == 2) {
        return commandSelftest();
    }
    if (strcmp(argv[1], "probe-properties") == 0 && argc == 2) {
        return CueletDriverProbeProperties();
    }
    const AudioDeviceID device = findDevice();
    if (device == kAudioObjectUnknown) {
        fprintf(stderr, "Cuelet device not found: %s\n", CUELET_DRIVER_DEVICE_UID);
        return 1;
    }
    if (strcmp(argv[1], "status") == 0 && argc == 2) {
        return commandStatus(device);
    }
    if (strcmp(argv[1], "clear") == 0 && argc == 2) {
        return commandClear(device);
    }
    if (strcmp(argv[1], "snapshot") == 0 && argc <= 3) {
        return commandSnapshot(device, argc == 3 ? argv[2] : NULL);
    }
    if (strcmp(argv[1], "probe-event-page") == 0 && argc == 2) {
        return commandProbeEventPage(device);
    }
    if (strcmp(argv[1], "summarize") == 0 && argc == 2) {
        return commandSummarize(device);
    }
    if (strcmp(argv[1], "watch") == 0 && (argc == 4 || argc == 5)) {
        const double seconds = strtod(argv[2], NULL);
        const unsigned interval = (unsigned)strtoul(argv[3], NULL, 10);
        if (seconds <= 0.0 || interval == 0) {
            usage(argv[0]);
            return 64;
        }
        return commandWatch(
            device, seconds, interval, argc == 5 ? argv[4] : NULL);
    }
    if (strcmp(argv[1], "watch-events") == 0 && argc == 5) {
        const double seconds = strtod(argv[2], NULL);
        const unsigned interval = (unsigned)strtoul(argv[3], NULL, 10);
        if (seconds <= 0.0 || interval == 0) {
            usage(argv[0]);
            return 64;
        }
        return commandWatchEvents(device, seconds, interval, argv[4]);
    }
    usage(argv[0]);
    return 64;
}
