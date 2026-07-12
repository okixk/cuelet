#include "platform/PlatformInfo.h"

#include <QtGlobal>

QString PlatformInfo::virtualMicrophoneNote()
{
#if defined(Q_OS_LINUX)
    return QStringLiteral("Virtual microphone routing is not built in yet. A future Linux backend can integrate with PipeWire or PulseAudio routing.");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Virtual microphone routing is not built in yet. macOS usually needs a virtual device such as BlackHole.");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Virtual microphone routing is not built in yet. Windows usually needs a virtual cable device such as VB-Cable.");
#else
    return QStringLiteral("Virtual microphone routing is not built in yet and depends on platform audio routing support.");
#endif
}
