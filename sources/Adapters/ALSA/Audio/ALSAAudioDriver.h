#ifndef _ALSA_AUDIO_DRIVER_H_
#define _ALSA_AUDIO_DRIVER_H_

#include "Services/Audio/AudioDriver.h"
#include "System/Process/Process.h"
#include <alsa/asoundlib.h>

// Direct-ALSA audio output, used so the MIDI clock can lock to what is ACTUALLY
// heard. The output thread writes each rendered block with snd_pcm_writei (which
// paces at real time) and then reads snd_pcm_delay() — the exact number of frames
// still in the host + USB buffer before the just-written audio is audible. From
// that, MidiClock's played-frame is set to (frames_written - delay) = the frame
// the listener is hearing right now. The MIDI scheduler emits each message when
// the interpolated played-frame reaches its stamp, so MIDI to external gear lands
// in sync with LGPT's audio as it leaves the DAC/USB device — no manual offset.
//
// Two threads (mirrors the SDL backend's split): a feed thread renders ahead into
// the pool (producer), an out thread drains the pool to ALSA (consumer).

class ALSAAudioDriver;

class ALSAThread : public SysThread {
  public:
    enum Role { FEED, OUT };
    ALSAThread(ALSAAudioDriver *d, Role role) : driver_(d), role_(role) {}
    virtual bool Execute();
    virtual void RequestTermination();
    bool ShouldStop() { return shouldTerminate(); } // public view for the driver

  private:
    ALSAAudioDriver *driver_;
    Role role_;
};

class ALSAAudioDriver : public AudioDriver {
  public:
    ALSAAudioDriver(AudioSettings &settings);
    virtual ~ALSAAudioDriver();

    virtual bool InitDriver();
    virtual void CloseDriver();
    virtual bool StartDriver();
    virtual void StopDriver();
    virtual int GetPlayedBufferPercentage();
    virtual int GetSampleRate() { return 44100; }
    virtual bool Interlaced() { return true; }
    virtual double GetStreamTime();

    // thread bodies (called from ALSAThread::Execute)
    void feedBody(ALSAThread *self); // producer: render ahead into the pool
    void outBody(ALSAThread *self);  // consumer: write to ALSA + sync MidiClock
    void notifyFeed();

  private:
    bool openPcm();

    snd_pcm_t *pcm_;
    unsigned long long written_; // total REAL frames submitted to ALSA
    ALSAThread *feed_;
    ALSAThread *out_;
    SysSemaphore *feedSem_; // out thread posts -> feed thread renders more
    unsigned long long startNs_;
};
#endif
