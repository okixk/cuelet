#include "CueletVirtualAudioCore.h"
#include "CueletVirtualAudioDiagnosticClient.h"
#include "CueletVirtualAudioDiagnostics.h"

#include <CoreFoundation/CFPlugInCOM.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef void* (*CueletFactoryFunction)(CFAllocatorRef, CFUUIDRef);

#ifndef CUELET_BUNDLE_SMOKE_EXPECT_DIAGNOSTICS
#define CUELET_BUNDLE_SMOKE_EXPECT_DIAGNOSTICS 1
#endif

#if CUELET_BUNDLE_SMOKE_EXPECT_DIAGNOSTICS
typedef struct CueletPropertyDispatchProbe {
    Boolean hasProperty;
    OSStatus settableStatus;
    Boolean settable;
    OSStatus sizeStatus;
    UInt32 reportedSize;
    OSStatus getStatus;
    UInt32 returnedSize;
    CFPropertyListRef value;
} CueletPropertyDispatchProbe;

static CueletPropertyDispatchProbe probeReadOnlyProperty(
    AudioServerPlugInDriverRef driver,
    AudioObjectPropertySelector selector)
{
    const AudioObjectPropertyAddress address =
        CueletDiagnosticPropertyAddress(selector);
    CueletPropertyDispatchProbe probe = {0};
    probe.hasProperty = (*driver)->HasProperty(
        driver, kCueletObjectDevice, 0, &address);
    probe.settable = true;
    probe.settableStatus = (*driver)->IsPropertySettable(
        driver, kCueletObjectDevice, 0, &address, &probe.settable);
    probe.sizeStatus = (*driver)->GetPropertyDataSize(
        driver,
        kCueletObjectDevice,
        0,
        &address,
        0,
        NULL,
        &probe.reportedSize);
    probe.returnedSize = sizeof(probe.value);
    probe.getStatus = (*driver)->GetPropertyData(
        driver,
        kCueletObjectDevice,
        0,
        &address,
        0,
        NULL,
        sizeof(probe.value),
        &probe.returnedSize,
        &probe.value);
    return probe;
}

static bool isReadOnlyCFDataProbeValid(
    const CueletPropertyDispatchProbe* probe)
{
    return probe->hasProperty &&
        probe->settableStatus == noErr && !probe->settable &&
        probe->sizeStatus == noErr &&
        probe->reportedSize == sizeof(CFPropertyListRef) &&
        probe->getStatus == noErr &&
        probe->returnedSize == sizeof(CFPropertyListRef) &&
        probe->value != NULL &&
        CFGetTypeID(probe->value) == CFDataGetTypeID();
}
#endif

int main(int argc, char* argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <driver-executable>\n", argv[0]);
        return 64;
    }

    void* handle = dlopen(argv[1], RTLD_LOCAL | RTLD_NOW);
    if (handle == NULL) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }

    dlerror();
    CueletFactoryFunction factory = (CueletFactoryFunction)dlsym(
        handle,
        "CueletVirtualAudio_Create");
    const char* symbolError = dlerror();
    if (symbolError != NULL || factory == NULL) {
        fprintf(stderr, "dlsym failed: %s\n", symbolError);
        dlclose(handle);
        return 1;
    }

    AudioServerPlugInDriverRef driver = factory(
        NULL,
        kAudioServerPlugInTypeUUID);
    if (driver == NULL || *driver == NULL) {
        fprintf(stderr, "factory returned no driver interface\n");
        dlclose(handle);
        return 1;
    }

    LPVOID queriedInterface = NULL;
    const HRESULT queryResult = (*driver)->QueryInterface(
        driver,
        CFUUIDGetUUIDBytes(kAudioServerPlugInDriverInterfaceUUID),
        &queriedInterface);
    if (queryResult != S_OK || queriedInterface != driver) {
        fprintf(stderr, "driver interface query failed: %d\n", queryResult);
        dlclose(handle);
        return 1;
    }

#if CUELET_BUNDLE_SMOKE_EXPECT_DIAGNOSTICS
    const AudioObjectPropertyAddress customInfoAddress =
        CueletDiagnosticPropertyAddress(
            kAudioObjectPropertyCustomPropertyInfoList);
    if (!(*driver)->HasProperty(
            driver,
            kCueletObjectDevice,
            0,
            &customInfoAddress)) {
        fprintf(stderr, "diagnostic custom-property metadata is unavailable\n");
        (*driver)->Release(driver);
        dlclose(handle);
        return 1;
    }
    UInt32 customInfoSize = 0;
    if ((*driver)->GetPropertyDataSize(
            driver,
            kCueletObjectDevice,
            0,
            &customInfoAddress,
            0,
            NULL,
            &customInfoSize) != noErr ||
        customInfoSize != 7 * sizeof(AudioServerPlugInCustomPropertyInfo)) {
        fprintf(stderr, "diagnostic custom-property metadata size is invalid\n");
        (*driver)->Release(driver);
        dlclose(handle);
        return 1;
    }
    AudioServerPlugInCustomPropertyInfo customInfo[7] = {0};
    UInt32 customInfoUsed = sizeof(customInfo);
    if ((*driver)->GetPropertyData(
            driver,
            kCueletObjectDevice,
            0,
            &customInfoAddress,
            0,
            NULL,
            sizeof(customInfo),
            &customInfoUsed,
            customInfo) != noErr ||
        customInfoUsed != sizeof(customInfo) ||
        customInfo[2].mSelector != kCueletDiagnosticPropertyEvents ||
        customInfo[2].mPropertyDataType !=
            kAudioServerPlugInCustomPropertyDataTypeCFPropertyList ||
        customInfo[2].mQualifierDataType !=
            kAudioServerPlugInCustomPropertyDataTypeNone) {
        fprintf(stderr, "diagnostic event property metadata is invalid\n");
        (*driver)->Release(driver);
        dlclose(handle);
        return 1;
    }

    const AudioObjectPropertyAddress diagnosticBuildAddress =
        CueletDiagnosticPropertyAddress(kCueletDiagnosticPropertyBuild);
    if (!(*driver)->HasProperty(
            driver,
            kCueletObjectDevice,
            0,
            &diagnosticBuildAddress)) {
        fprintf(stderr, "diagnostic build property is unavailable\n");
        (*driver)->Release(driver);
        dlclose(handle);
        return 1;
    }
    CFPropertyListRef buildValue = NULL;
    UInt32 buildSize = sizeof(buildValue);
    const OSStatus buildStatus = (*driver)->GetPropertyData(
        driver,
        kCueletObjectDevice,
        0,
        &diagnosticBuildAddress,
        0,
        NULL,
        sizeof(buildValue),
        &buildSize,
        &buildValue);
    CueletDiagnosticBuildInfo build = {0};
    const bool buildDataValid = buildValue != NULL &&
        CFGetTypeID(buildValue) == CFDataGetTypeID() &&
        CFDataGetLength((CFDataRef)buildValue) == (CFIndex)sizeof(build);
    if (buildDataValid) {
        memcpy(
            &build,
            CFDataGetBytePtr((CFDataRef)buildValue),
            sizeof(build));
    }
    if (buildStatus != noErr || buildSize != sizeof(buildValue) ||
        !buildDataValid ||
        build.versionMajor != 0 || build.versionMinor != 1 ||
        build.versionPatch != 11 || build.buildNumber != 12 ||
        build.diagnosticEnabled != 1) {
        fprintf(stderr, "diagnostic build identity is invalid\n");
        if (buildValue != NULL) CFRelease(buildValue);
        (*driver)->Release(driver);
        dlclose(handle);
        return 1;
    }
    CFRelease(buildValue);

    CueletPropertyDispatchProbe countersProbe = probeReadOnlyProperty(
        driver, kCueletDiagnosticPropertyCounters);
    CueletPropertyDispatchProbe eventProbe = probeReadOnlyProperty(
        driver, kCueletDiagnosticPropertyEvents);
    CFPropertyListRef eventValue = eventProbe.value;
    const bool eventTypeValid = eventValue != NULL &&
        CFGetTypeID(eventValue) == CFDataGetTypeID() &&
        CFDataGetLength((CFDataRef)eventValue) >=
            (CFIndex)sizeof(CueletDiagnosticEventExportHeader);
    const CueletDiagnosticEventExportHeader* eventHeader = eventTypeValid
        ? (const CueletDiagnosticEventExportHeader*)
            CFDataGetBytePtr((CFDataRef)eventValue)
        : NULL;
    const bool eventHeaderValid = eventHeader != NULL &&
        eventHeader->schemaVersion == CUELET_DIAGNOSTIC_SCHEMA_VERSION &&
        eventHeader->eventRecordSize == sizeof(CueletDiagnosticSnapshot) &&
        eventHeader->snapshotCapacity ==
            CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY &&
        eventHeader->returnedEventCount <=
            CUELET_DIAGNOSTIC_EVENT_SNAPSHOT_CAPACITY &&
        CFDataGetLength((CFDataRef)eventValue) ==
            (CFIndex)(sizeof(*eventHeader) +
                eventHeader->returnedEventCount *
                    sizeof(CueletDiagnosticSnapshot));
    if (!isReadOnlyCFDataProbeValid(&countersProbe) ||
        !isReadOnlyCFDataProbeValid(&eventProbe) ||
        !eventHeaderValid) {
        fprintf(stderr, "diagnostic read-only CFData dispatch is invalid\n");
        if (countersProbe.value != NULL) CFRelease(countersProbe.value);
        if (eventValue != NULL) CFRelease(eventValue);
        (*driver)->Release(driver);
        dlclose(handle);
        return 1;
    }
    printf(
        "direct selector=cdct has=%u settableStatus=%d settable=%u "
        "sizeStatus=%d size=%u getStatus=%d returned=%u type=%lu\n",
        countersProbe.hasProperty,
        (int)countersProbe.settableStatus,
        countersProbe.settable,
        (int)countersProbe.sizeStatus,
        countersProbe.reportedSize,
        (int)countersProbe.getStatus,
        countersProbe.returnedSize,
        (unsigned long)CFGetTypeID(countersProbe.value));
    printf(
        "direct selector=cqev has=%u settableStatus=%d settable=%u "
        "sizeStatus=%d size=%u getStatus=%d returned=%u type=%lu "
        "schema=%u events=%u\n",
        eventProbe.hasProperty,
        (int)eventProbe.settableStatus,
        eventProbe.settable,
        (int)eventProbe.sizeStatus,
        eventProbe.reportedSize,
        (int)eventProbe.getStatus,
        eventProbe.returnedSize,
        (unsigned long)CFGetTypeID(eventProbe.value),
        eventHeader->schemaVersion,
        eventHeader->returnedEventCount);
    CFRelease(countersProbe.value);
    CFRelease(eventValue);
#else
    const AudioObjectPropertyAddress customInfoAddress =
        CueletDiagnosticPropertyAddress(
            kAudioObjectPropertyCustomPropertyInfoList);
    if ((*driver)->HasProperty(
            driver,
            kCueletObjectDevice,
            0,
            &customInfoAddress)) {
        fprintf(stderr, "Release driver unexpectedly exposes diagnostics\n");
        (*driver)->Release(driver);
        dlclose(handle);
        return 1;
    }
#endif

    (*driver)->Release(driver);
    dlclose(handle);
#if CUELET_BUNDLE_SMOKE_EXPECT_DIAGNOSTICS
    printf(
        "Cuelet diagnostic custom properties: object=%u scope=global "
        "element=main cust=7 type=CFPropertyList build=0.1.11/12\n",
        kCueletObjectDevice);
    printf("Cuelet virtual audio diagnostic bundle smoke test: PASS\n");
#else
    printf("Cuelet virtual audio production bundle smoke test: PASS\n");
#endif
    return 0;
}
