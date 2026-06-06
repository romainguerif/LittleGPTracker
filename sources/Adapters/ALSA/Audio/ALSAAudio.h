#ifndef _ALSA_AUDIO_H_
#define _ALSA_AUDIO_H_

#include "Services/Audio/Audio.h"

// Audio backend that outputs through ALSA directly (see ALSAAudioDriver) so the
// MIDI clock can lock to the real, measured output latency (snd_pcm_delay).
class ALSAAudio : public Audio {
  public:
    ALSAAudio(AudioSettings &hints);
    ~ALSAAudio();
    virtual void Init();
    virtual void Close();
    virtual int GetMixerVolume();
    virtual void SetMixerVolume(int volume);

  private:
    AudioSettings hints_;
};
#endif
