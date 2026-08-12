#ifndef CUELET_VIRTUAL_AUDIO_DIAGNOSTIC_CLIENT_H
#define CUELET_VIRTUAL_AUDIO_DIAGNOSTIC_CLIENT_H

#include <CoreAudio/CoreAudio.h>
#include <stdio.h>

static inline AudioObjectPropertyAddress CueletDiagnosticPropertyAddress(
    AudioObjectPropertySelector selector)
{
    const AudioObjectPropertyAddress address = {
        selector,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain,
    };
    return address;
}

int CueletDriverProbeProperties(void);

#endif
