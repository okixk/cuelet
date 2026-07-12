#include "services/LinuxAudioService.h"

#include <cassert>
#include <iostream>

int main(int argc, char** argv)
{
    gst_init(&argc, &argv);

    LinuxAudioService audio;
    assert(!audio.playbackProgress("missing"));

    cuelet::SoundClip missing;
    missing.relativePath = "missing";
    missing.missing = true;
    assert(!audio.play(missing));
    assert(!audio.playbackProgress("missing"));

    std::cout << "cuelet audio service tests passed\n";
    return 0;
}
