#pragma once

// Debug candidates use a private WPP provider for deterministic startup and
// lifecycle checkpoints. Release builds compile every call away.
#if DBG

#include <evntrace.h>

#define WPP_CHECK_FOR_NULL_STRING

// {1819CEB3-B714-493F-8B5F-771AFFB0DC63}
#define WPP_CONTROL_GUIDS                                                \
    WPP_DEFINE_CONTROL_GUID(                                             \
        CueletVirtualAudioTraceGuid,                                     \
        (1819ceb3,b714,493f,8b5f,771affb0dc63),                          \
        WPP_DEFINE_BIT(CUELET_TRACE_STARTUP)                             \
        WPP_DEFINE_BIT(CUELET_TRACE_STREAM))

#define WPP_LEVEL_FLAGS_LOGGER(level, flags) WPP_LEVEL_LOGGER(flags)
#define WPP_LEVEL_FLAGS_ENABLED(level, flags)                            \
    (WPP_LEVEL_ENABLED(flags) &&                                         \
     WPP_CONTROL(WPP_BIT_ ## flags).Level >= level)

extern "C"
void
CueletTraceCheckpoint(
    _In_z_ PCSTR checkpoint,
    _In_ NTSTATUS status,
    _In_ ULONGLONG value0,
    _In_ ULONGLONG value1,
    _In_ ULONGLONG value2,
    _In_ ULONGLONG value3,
    _In_ ULONGLONG value4,
    _In_ ULONGLONG value5,
    _In_ ULONGLONG value6,
    _In_ ULONGLONG value7);

#define CUELET_TRACE_CHECKPOINT(                                         \
    checkpoint, status, value0, value1, value2, value3,                 \
    value4, value5, value6, value7)                                     \
    CueletTraceCheckpoint(                                               \
        checkpoint, status,                                              \
        static_cast<ULONGLONG>(value0),                                  \
        static_cast<ULONGLONG>(value1),                                  \
        static_cast<ULONGLONG>(value2),                                  \
        static_cast<ULONGLONG>(value3),                                  \
        static_cast<ULONGLONG>(value4),                                  \
        static_cast<ULONGLONG>(value5),                                  \
        static_cast<ULONGLONG>(value6),                                  \
        static_cast<ULONGLONG>(value7))

#else

#define CUELET_TRACE_CHECKPOINT(                                         \
    checkpoint, status, value0, value1, value2, value3,                 \
    value4, value5, value6, value7)                                     \
    ((void)0)

#endif
