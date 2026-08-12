/* Cuelet-only Core Audio object hierarchy and custom-property visibility probe. */

#include "CueletVirtualAudioCore.h"
#include "CueletVirtualAudioDiagnosticClient.h"
#include "CueletVirtualAudioDiagnostics.h"

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ProbeObject {
    AudioObjectID objectID;
    const char* role;
} ProbeObject;

typedef struct ProbeSelector {
    AudioObjectPropertySelector selector;
    const char* name;
} ProbeSelector;

typedef struct ProbeScope {
    AudioObjectPropertyScope scope;
    const char* name;
} ProbeScope;

typedef struct ProbeElement {
    AudioObjectPropertyElement element;
    const char* name;
} ProbeElement;

static const ProbeSelector kSelectors[] = {
    { kAudioObjectPropertyCustomPropertyInfoList, "cust" },
    { kCueletDiagnosticPropertySchema, "cdsv" },
    { kCueletDiagnosticPropertyCounters, "cdct" },
    { kCueletDiagnosticPropertyEvents, "cqev" },
    { kCueletDiagnosticPropertyEventCount, "cdec" },
    { kCueletDiagnosticPropertyClear, "cdcl" },
    { kCueletDiagnosticPropertyBuild, "cdbv" },
    { kCueletDiagnosticPropertyEnabled, "cden" },
};

static const ProbeScope kScopes[] = {
    { kAudioObjectPropertyScopeGlobal, "global" },
    { kAudioObjectPropertyScopeInput, "input" },
    { kAudioObjectPropertyScopeOutput, "output" },
    { kAudioObjectPropertyScopeWildcard, "wildcard" },
};

/* Main and the legacy master element are both numeric zero in current SDKs. */
static const ProbeElement kElements[] = {
    { kAudioObjectPropertyElementMain, "main" },
    { 0, "master_alias" },
    { kAudioObjectPropertyElementWildcard, "wildcard" },
};

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
    if (code[0] != '.' || code[1] != '.' || code[2] != '.' || code[3] != '.') {
        snprintf(output, 64, "%d('%s')", (int)status, code);
    } else {
        snprintf(output, 64, "%d", (int)status);
    }
}

static AudioDeviceID findDevice(void)
{
    const AudioObjectPropertyAddress address = {
        kAudioHardwarePropertyDevices,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(
            kAudioObjectSystemObject, &address, 0, NULL, &size) != noErr ||
        size == 0) {
        return kAudioObjectUnknown;
    }
    AudioDeviceID* devices = calloc(1, size);
    if (devices == NULL) {
        return kAudioObjectUnknown;
    }
    if (AudioObjectGetPropertyData(
            kAudioObjectSystemObject, &address, 0, NULL, &size, devices) !=
        noErr) {
        free(devices);
        return kAudioObjectUnknown;
    }
    AudioDeviceID result = kAudioObjectUnknown;
    const UInt32 count = size / sizeof(*devices);
    for (UInt32 index = 0; index < count; ++index) {
        const AudioObjectPropertyAddress uidAddress = {
            kAudioDevicePropertyDeviceUID,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain,
        };
        CFStringRef uid = NULL;
        UInt32 uidSize = sizeof(uid);
        if (AudioObjectGetPropertyData(
                devices[index], &uidAddress, 0, NULL, &uidSize, &uid) == noErr &&
            uid != NULL && CFEqual(uid, CFSTR(CUELET_DRIVER_DEVICE_UID))) {
            result = devices[index];
            break;
        }
    }
    free(devices);
    return result;
}

static AudioObjectID findPlugIn(void)
{
    const AudioObjectPropertyAddress address = {
        kAudioHardwarePropertyTranslateBundleIDToPlugIn,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    const CFStringRef bundleID = CFSTR(CUELET_DRIVER_BUNDLE_ID);
    AudioObjectID plugIn = kAudioObjectUnknown;
    UInt32 size = sizeof(plugIn);
    return AudioObjectGetPropertyData(
               kAudioObjectSystemObject,
               &address,
               sizeof(bundleID),
               &bundleID,
               &size,
               &plugIn) == noErr
        ? plugIn : kAudioObjectUnknown;
}

static bool containsObject(
    const ProbeObject* objects,
    size_t count,
    AudioObjectID objectID)
{
    for (size_t index = 0; index < count; ++index) {
        if (objects[index].objectID == objectID) {
            return true;
        }
    }
    return false;
}

static void addObject(
    ProbeObject* objects,
    size_t capacity,
    size_t* count,
    AudioObjectID objectID,
    const char* role)
{
    if (objectID == kAudioObjectUnknown || *count >= capacity ||
        containsObject(objects, *count, objectID)) {
        return;
    }
    objects[*count] = (ProbeObject){ objectID, role };
    ++*count;
}

static AudioObjectID getOwner(AudioObjectID objectID)
{
    const AudioObjectPropertyAddress address = {
        kAudioObjectPropertyOwner,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    AudioObjectID owner = kAudioObjectUnknown;
    UInt32 size = sizeof(owner);
    return AudioObjectGetPropertyData(
               objectID, &address, 0, NULL, &size, &owner) == noErr
        ? owner : kAudioObjectUnknown;
}

static void addObjectList(
    ProbeObject* objects,
    size_t capacity,
    size_t* count,
    AudioObjectID objectID,
    AudioObjectPropertySelector selector,
    AudioObjectPropertyScope scope,
    const char* role)
{
    const AudioObjectPropertyAddress address = {
        selector, scope, kAudioObjectPropertyElementMain,
    };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(objectID, &address, 0, NULL, &size) !=
            noErr ||
        size == 0) {
        return;
    }
    AudioObjectID* list = calloc(1, size);
    if (list == NULL) {
        return;
    }
    if (AudioObjectGetPropertyData(
            objectID, &address, 0, NULL, &size, list) == noErr) {
        const UInt32 listCount = size / sizeof(*list);
        for (UInt32 index = 0; index < listCount; ++index) {
            addObject(objects, capacity, count, list[index], role);
        }
    }
    free(list);
}

static UInt32 getUInt32Property(
    AudioObjectID objectID,
    AudioObjectPropertySelector selector)
{
    const AudioObjectPropertyAddress address = {
        selector,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    UInt32 value = 0;
    UInt32 size = sizeof(value);
    return AudioObjectGetPropertyData(
               objectID, &address, 0, NULL, &size, &value) == noErr
        ? value : 0;
}

static void getStringProperty(
    AudioObjectID objectID,
    AudioObjectPropertySelector selector,
    char output[256])
{
    output[0] = '\0';
    const AudioObjectPropertyAddress address = {
        selector,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    CFStringRef value = NULL;
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(
            objectID, &address, 0, NULL, &size, &value) == noErr &&
        value != NULL) {
        (void)CFStringGetCString(
            value, output, 256, kCFStringEncodingUTF8);
    }
}

static void printHierarchy(const ProbeObject* objects, size_t count)
{
    puts("object_id,role,class,class_hex,owner,name,uid");
    for (size_t index = 0; index < count; ++index) {
        const UInt32 objectClass = getUInt32Property(
            objects[index].objectID, kAudioObjectPropertyClass);
        const AudioObjectID owner = getOwner(objects[index].objectID);
        char classCode[5];
        fourCC(objectClass, classCode);
        char name[256];
        char uid[256];
        getStringProperty(
            objects[index].objectID, kAudioObjectPropertyName, name);
        getStringProperty(
            objects[index].objectID, kAudioDevicePropertyDeviceUID, uid);
        printf("%u,%s,%s,0x%08x,%u,\"%s\",\"%s\"\n",
            objects[index].objectID, objects[index].role, classCode,
            objectClass, owner, name, uid);
    }
}

static void printPropertyMatrix(const ProbeObject* objects, size_t count)
{
    puts("object_id,role,selector,selector_hex,selector_decimal,scope,scope_hex,"
         "element,element_value,has_property,settable_status,settable_value,"
         "size_status,size,get_status,get_size");
    for (size_t objectIndex = 0; objectIndex < count; ++objectIndex) {
        for (size_t selectorIndex = 0;
             selectorIndex < sizeof(kSelectors) / sizeof(kSelectors[0]);
             ++selectorIndex) {
            for (size_t scopeIndex = 0;
                 scopeIndex < sizeof(kScopes) / sizeof(kScopes[0]);
                 ++scopeIndex) {
                for (size_t elementIndex = 0;
                     elementIndex < sizeof(kElements) / sizeof(kElements[0]);
                     ++elementIndex) {
                    const AudioObjectPropertyAddress address = {
                        kSelectors[selectorIndex].selector,
                        kScopes[scopeIndex].scope,
                        kElements[elementIndex].element,
                    };
                    const Boolean has = AudioObjectHasProperty(
                        objects[objectIndex].objectID, &address);
                    Boolean settable = false;
                    const OSStatus settableStatus =
                        AudioObjectIsPropertySettable(
                            objects[objectIndex].objectID,
                            &address,
                            &settable);
                    UInt32 size = 0;
                    const UInt32 qualifierSize = 0;
                    const void* qualifierData = NULL;
                    const OSStatus sizeStatus = AudioObjectGetPropertyDataSize(
                        objects[objectIndex].objectID,
                        &address,
                        qualifierSize,
                        qualifierData,
                        &size);
                    OSStatus getStatus = sizeStatus;
                    UInt32 getSize = size;
                    void* allocation = NULL;
                    if (sizeStatus == noErr && size > 0 && size <= 64U * 1024U * 1024U) {
                        allocation = calloc(1, size);
                        if (allocation != NULL) {
                            getStatus = AudioObjectGetPropertyData(
                                objects[objectIndex].objectID,
                                &address,
                                qualifierSize,
                                qualifierData,
                                &getSize,
                                allocation);
                        }
                    }
                    char settableText[64];
                    char sizeText[64];
                    char getText[64];
                    statusText(settableStatus, settableText);
                    statusText(sizeStatus, sizeText);
                    statusText(getStatus, getText);
                    printf("%u,%s,%s,0x%08x,%u,%s,0x%08x,%s,%u,%u,"
                           "\"%s\",%u,\"%s\",%u,\"%s\",%u\n",
                        objects[objectIndex].objectID,
                        objects[objectIndex].role,
                        kSelectors[selectorIndex].name,
                        kSelectors[selectorIndex].selector,
                        kSelectors[selectorIndex].selector,
                        kScopes[scopeIndex].name,
                        kScopes[scopeIndex].scope,
                        kElements[elementIndex].name,
                        kElements[elementIndex].element,
                        has,
                        settableText,
                        settable,
                        sizeText,
                        size,
                        getText,
                        getSize);
                    if (getStatus == noErr &&
                        kSelectors[selectorIndex].selector !=
                            kAudioObjectPropertyCustomPropertyInfoList &&
                        getSize == sizeof(CFPropertyListRef) &&
                        allocation != NULL) {
                        const CFPropertyListRef value =
                            *(const CFPropertyListRef*)allocation;
                        if (value != NULL) CFRelease(value);
                    }
                    free(allocation);
                }
            }
        }
    }
}

static void printCustomPropertyContract(AudioDeviceID device)
{
    const AudioObjectPropertyAddress address =
        CueletDiagnosticPropertyAddress(
            kAudioObjectPropertyCustomPropertyInfoList);
    UInt32 size = 0;
    const OSStatus sizeStatus = AudioObjectGetPropertyDataSize(
        device, &address, 0, NULL, &size);
    char sizeStatusText[64];
    statusText(sizeStatus, sizeStatusText);
    printf("size_status,size,get_status,get_size,struct_size,array_index,"
           "selector,selector_hex,selector_decimal,property_type,"
           "property_type_hex,property_type_decimal,qualifier_type,"
           "qualifier_type_hex,qualifier_type_decimal\n");
    if (sizeStatus != noErr || size == 0 ||
        size % sizeof(AudioServerPlugInCustomPropertyInfo) != 0) {
        printf("\"%s\",%u,not-attempted,0,%zu,,,,,,,,,,,\n",
            sizeStatusText, size,
            sizeof(AudioServerPlugInCustomPropertyInfo));
        return;
    }
    AudioServerPlugInCustomPropertyInfo* properties = calloc(1, size);
    if (properties == NULL) {
        printf("\"%s\",%u,allocation-failed,0,%zu,,,,,,,,,,,\n",
            sizeStatusText, size,
            sizeof(AudioServerPlugInCustomPropertyInfo));
        return;
    }
    UInt32 used = size;
    const OSStatus getStatus = AudioObjectGetPropertyData(
        device, &address, 0, NULL, &used, properties);
    char getStatusText[64];
    statusText(getStatus, getStatusText);
    const UInt32 count = used /
        (UInt32)sizeof(AudioServerPlugInCustomPropertyInfo);
    for (UInt32 index = 0; index < count; ++index) {
        char selector[5];
        char propertyType[5];
        char qualifierType[5];
        fourCC(properties[index].mSelector, selector);
        fourCC(properties[index].mPropertyDataType, propertyType);
        fourCC(properties[index].mQualifierDataType, qualifierType);
        printf("\"%s\",%u,\"%s\",%u,%zu,%u,%s,0x%08x,%u,%s,"
               "0x%08x,%u,%s,0x%08x,%u\n",
            sizeStatusText, size, getStatusText, used,
            sizeof(AudioServerPlugInCustomPropertyInfo), index,
            selector, properties[index].mSelector,
            properties[index].mSelector,
            propertyType, properties[index].mPropertyDataType,
            properties[index].mPropertyDataType,
            qualifierType, properties[index].mQualifierDataType,
            properties[index].mQualifierDataType);
    }
    free(properties);
}

static void printReadOnlyPropertyComparison(AudioDeviceID device)
{
    const ProbeSelector selectors[] = {
        { kCueletDiagnosticPropertyCounters, "cdct" },
        { kCueletDiagnosticPropertyEvents, "cqev" },
    };
    puts("selector,has_property,settable_status,settable_value,size_status,"
         "reported_size,get_status,returned_size,value_type,value_bytes");
    for (size_t index = 0;
         index < sizeof(selectors) / sizeof(selectors[0]); ++index) {
        const AudioObjectPropertyAddress address =
            CueletDiagnosticPropertyAddress(selectors[index].selector);
        const Boolean hasProperty = AudioObjectHasProperty(device, &address);
        Boolean settable = true;
        const OSStatus settableStatus = AudioObjectIsPropertySettable(
            device, &address, &settable);
        UInt32 size = 0;
        const OSStatus sizeStatus = AudioObjectGetPropertyDataSize(
            device, &address, 0, NULL, &size);
        CFPropertyListRef value = NULL;
        UInt32 used = sizeof(value);
        const OSStatus getStatus = AudioObjectGetPropertyData(
            device, &address, 0, NULL, &used, &value);
        const char* valueType = "none";
        long valueBytes = -1;
        if (value != NULL && CFGetTypeID(value) == CFDataGetTypeID()) {
            valueType = "CFData";
            valueBytes = (long)CFDataGetLength((CFDataRef)value);
        } else if (value != NULL) {
            valueType = "other";
        }
        char settableText[64];
        char sizeText[64];
        char getText[64];
        statusText(settableStatus, settableText);
        statusText(sizeStatus, sizeText);
        statusText(getStatus, getText);
        printf("%s,%u,\"%s\",%u,\"%s\",%u,\"%s\",%u,%s,%ld\n",
            selectors[index].name, hasProperty, settableText, settable,
            sizeText, size, getText, used, valueType, valueBytes);
        if (value != NULL) CFRelease(value);
    }
}

static void printEventAccessMatrix(AudioDeviceID device)
{
    const AudioObjectPropertyAddress address =
        CueletDiagnosticPropertyAddress(kCueletDiagnosticPropertyEvents);
    const UInt8 qualifierByte = 1;
    const CFDataRef qualifier = CFDataCreate(
        kCFAllocatorDefault,
        &qualifierByte,
        (CFIndex)sizeof(qualifierByte));
    const UInt32 outputSizes[] = {
        sizeof(UInt32),
        sizeof(CFPropertyListRef),
        sizeof(CFPropertyListRef) + sizeof(UInt32),
        2U * sizeof(CFPropertyListRef),
    };
    puts("mode,qualifier_size,size_status,reported_size,output_size,"
         "get_status,returned_size,value_present,value_type,value_bytes");
    for (unsigned mode = 0; mode < 2; ++mode) {
        const bool qualified = mode != 0;
        const UInt32 qualifierSize = qualified && qualifier != NULL
            ? (UInt32)sizeof(qualifier) : 0;
        const void* qualifierData = qualifierSize == 0
            ? NULL : &qualifier;
        UInt32 reportedSize = 0;
        const OSStatus sizeStatus = AudioObjectGetPropertyDataSize(
            device,
            &address,
            qualifierSize,
            qualifierData,
            &reportedSize);
        for (size_t outputIndex = 0;
             outputIndex < sizeof(outputSizes) / sizeof(outputSizes[0]);
             ++outputIndex) {
            union {
                max_align_t alignment;
                unsigned char bytes[2 * sizeof(CFPropertyListRef)];
            } storage = {0};
            UInt32 used = outputSizes[outputIndex];
            const OSStatus getStatus = AudioObjectGetPropertyData(
                device,
                &address,
                qualifierSize,
                qualifierData,
                &used,
                storage.bytes);
            CFPropertyListRef value = NULL;
            if (getStatus == noErr && used == sizeof(value)) {
                memcpy(&value, storage.bytes, sizeof(value));
            }
            const bool valuePresent = value != NULL;
            const char* valueType = "none";
            long valueBytes = -1;
            if (valuePresent && CFGetTypeID(value) == CFDataGetTypeID()) {
                valueType = "CFData";
                valueBytes = (long)CFDataGetLength((CFDataRef)value);
            } else if (valuePresent) {
                valueType = "other";
            }
            char sizeStatusText[64];
            char getStatusText[64];
            statusText(sizeStatus, sizeStatusText);
            statusText(getStatus, getStatusText);
            printf("%s,%u,\"%s\",%u,%u,\"%s\",%u,%u,%s,%ld\n",
                qualified ? "qualified" : "unqualified",
                qualifierSize,
                sizeStatusText,
                reportedSize,
                outputSizes[outputIndex],
                getStatusText,
                used,
                valuePresent,
                valueType,
                valueBytes);
            if (value != NULL) CFRelease(value);
        }
    }
    const CFPropertyListRef setValue = kCFBooleanTrue;
    const OSStatus setStatus = AudioObjectSetPropertyData(
        device,
        &address,
        0,
        NULL,
        sizeof(setValue),
        &setValue);
    char setStatusText[64];
    statusText(setStatus, setStatusText);
    printf("set_attempt,0,not-attempted,0,%zu,\"%s\",0,0,none,-1\n",
        sizeof(setValue), setStatusText);
    if (qualifier != NULL) CFRelease(qualifier);
}

int CueletDriverProbeProperties(void)
{
    AudioDeviceID device = findDevice();
    if (device == kAudioObjectUnknown) {
        fprintf(stderr, "Cuelet device not found: %s\n", CUELET_DRIVER_DEVICE_UID);
        return 2;
    }

    ProbeObject objects[32] = {0};
    size_t objectCount = 0;
    addObject(objects, 32, &objectCount, kAudioObjectSystemObject, "system");
    addObject(objects, 32, &objectCount, findPlugIn(), "plugin");
    addObject(objects, 32, &objectCount, device, "device");

    AudioObjectID owner = getOwner(device);
    while (owner != kAudioObjectUnknown && owner != kAudioObjectSystemObject) {
        addObject(objects, 32, &objectCount, owner, "owner");
        const AudioObjectID nextOwner = getOwner(owner);
        if (nextOwner == owner) {
            break;
        }
        owner = nextOwner;
    }

    addObjectList(objects, 32, &objectCount, device,
        kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput,
        "input_stream");
    addObjectList(objects, 32, &objectCount, device,
        kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
        "output_stream");
    addObjectList(objects, 32, &objectCount, device,
        kAudioObjectPropertyOwnedObjects, kAudioObjectPropertyScopeGlobal,
        "device_owned");

    puts("# hierarchy");
    printHierarchy(objects, objectCount);
    puts("# property_matrix");
    printPropertyMatrix(objects, objectCount);
    puts("# custom_property_contract");
    printCustomPropertyContract(device);
    puts("# read_only_cfpropertylist_comparison");
    printReadOnlyPropertyComparison(device);
    puts("# event_access_matrix");
    printEventAccessMatrix(device);
    return 0;
}

#ifndef CUELET_PROPERTY_PROBE_NO_MAIN
int main(void)
{
    return CueletDriverProbeProperties();
}
#endif
