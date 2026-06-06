#include "ALSAAudio.h"
#include "ALSAAudioDriver.h"
#include "Services/Audio/AudioOutDriver.h"

ALSAAudio::ALSAAudio(AudioSettings &hints) : Audio(hints) { hints_ = hints; }

ALSAAudio::~ALSAAudio() {}

void ALSAAudio::Init() {
    AudioSettings settings;
    settings.audioAPI_ = GetAudioAPI();
    settings.bufferSize_ = GetAudioBufferSize();
    settings.preBufferCount_ = GetAudioPreBufferCount();

    ALSAAudioDriver *drv = new ALSAAudioDriver(settings);
    AudioOut *out = new AudioOutDriver(*drv);
    Insert(out);
}

void ALSAAudio::Close() {
    IteratorPtr<AudioOut> it(GetIterator());
    for (it->Begin(); !it->IsDone(); it->Next()) {
        AudioOut &current = it->CurrentItem();
        current.Close();
    }
}

int ALSAAudio::GetMixerVolume() { return 100; }

void ALSAAudio::SetMixerVolume(int volume) {}
