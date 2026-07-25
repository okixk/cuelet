#pragma once

#include <ntddk.h>
#include <ks.h>
#include <ksmedia.h>

// The SysVAD sample generates a tone for capture. Cuelet replaces that sample
// behavior with this bounded, nonpaged render-to-capture ring. Every capture
// stream has its own cursor so several voice/chat clients can read the mix.
//
// The endpoint format tables are constrained to matching PCM formats by the
// source-preparation script. A format mismatch fails closed to silence.

void CueletAudioBridgeInitialize();

void CueletAudioBridgePrepareForDeviceStart();

void CueletAudioBridgeBeginTeardown();

void CueletAudioBridgePublish(
    _In_reads_bytes_(byteCount) const BYTE* source,
    _In_ ULONG byteCount,
    _In_opt_ const WAVEFORMATEX* format);

void CueletAudioBridgeRead(
    _In_ const void* readerKey,
    _Out_writes_bytes_(byteCount) BYTE* destination,
    _In_ ULONG byteCount,
    _In_opt_ const WAVEFORMATEX* format);

void CueletAudioBridgeReleaseReader(_In_ const void* readerKey);
