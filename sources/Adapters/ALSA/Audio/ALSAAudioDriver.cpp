#include "ALSAAudioDriver.h"
#include "Services/Midi/MidiClock.h"
#include "Services/Midi/MidiService.h"
#include "Application/Model/Config.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>

// Write a sysfs value (raw; LGPT globally redefines fopen). No-op if absent.
static void writeSys(const char *path, const char *val) {
    int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        ssize_t w = write(fd, val, strlen(val));
        (void)w;
        close(fd);
    }
}

// ---- thread trampolines ----------------------------------------------------

bool ALSAThread::Execute() {
    if (role_ == FEED)
        driver_->feedBody(this);
    else
        driver_->outBody(this);
    return true;
}

void ALSAThread::RequestTermination() {
    SysThread::RequestTermination();
    if (role_ == FEED)
        driver_->notifyFeed(); // unblock the feed thread's wait
}

// ---- device selection ------------------------------------------------------

// Same /proc/asound/cards scan as the input probe: prefer the USB card so LGPT's
// whole mix plays THROUGH the external gear (e.g. an Elektron Model:Samples).
static int findUsbCard() {
    int fd = open("/proc/asound/cards", O_RDONLY);
    if (fd < 0)
        return -1;
    char buf[4096];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0)
        return -1;
    buf[n] = 0;
    int found = -1;
    char *line = buf;
    while (line && *line) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = 0;
        int idx;
        if (sscanf(line, " %d [", &idx) == 1) {
            if ((strstr(line, "USB") || strstr(line, "usb")) &&
                !strstr(line, "audiocodec")) {
                found = idx;
                break;
            }
        }
        line = nl ? nl + 1 : 0;
    }
    return found;
}

// ---- driver ----------------------------------------------------------------

ALSAAudioDriver::ALSAAudioDriver(AudioSettings &settings)
    : AudioDriver(settings), pcm_(0), written_(0), feed_(0), out_(0),
      feedSem_(0), startNs_(0) {}

ALSAAudioDriver::~ALSAAudioDriver() {}

bool ALSAAudioDriver::openPcm() {
    char dev[96];
    const char *cfg = Config::GetInstance()->GetValue("AUDIOOUTDEV");
    if (cfg && cfg[0]) {
        strncpy(dev, cfg, sizeof(dev) - 1);
        dev[sizeof(dev) - 1] = 0;
    } else {
        int card = findUsbCard();
        if (card >= 0)
            snprintf(dev, sizeof(dev), "plughw:%d,0", card); // -> the USB gear
        else
            strcpy(dev, "default"); // no USB device: internal codec
    }

    // sunxi USB anti-glitch (same recipe that stabilised capture in M8Tape):
    // smaller/more frequent isochronous transfers + no USB autosuspend, and force
    // the sound card's power on. Cuts the periodic USB-audio crackle.
    writeSys("/sys/module/snd_usb_audio/parameters/nrpacks", "1");
    writeSys("/sys/module/usbcore/parameters/autosuspend", "-1");
    for (int c = 0; c < 4; c++) {
        char pc[96];
        snprintf(pc, sizeof(pc), "/sys/class/sound/card%d/device/../power/control", c);
        writeSys(pc, "on");
    }

    int err = snd_pcm_open(&pcm_, dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        Trace::Error("ALSAAudio: open(%s) failed: %s", dev, snd_strerror(err));
        pcm_ = 0;
        return false;
    }

    // Target buffering. Bigger = more headroom against render spikes / USB
    // hiccups (fewer crackles), at the cost of play-along latency (the MIDI sync
    // stays exact regardless, via snd_pcm_delay). Tunable: AUDIOOUTLATENCY (ms).
    unsigned int latency_us = 120000; // ~120 ms default
    const char *lat = Config::GetInstance()->GetValue("AUDIOOUTLATENCY");
    if (lat && lat[0]) {
        int ms = atoi(lat);
        if (ms >= 20 && ms <= 500)
            latency_us = (unsigned int)ms * 1000;
    }
    // soft_resample=1 lets the device run at its native rate while we feed 44100
    // (so the engine's fixed 44100 clock holds).
    err = snd_pcm_set_params(pcm_, SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED, 2, 44100,
                             1 /*soft resample*/, latency_us);
    if (err < 0) {
        Trace::Error("ALSAAudio: set_params failed: %s", snd_strerror(err));
        snd_pcm_close(pcm_);
        pcm_ = 0;
        return false;
    }

    // Probe (HTTP-readable) so the chosen output device is diagnosable.
    int pfd =
        open("/mnt/SDCARD/audioout_probe.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (pfd >= 0) {
        char msg[160];
        int l = snprintf(msg, sizeof(msg),
                         "backend: ALSA direct (snd_pcm)\noutput device: %s\n"
                         "44100 Hz S16_LE stereo, target %u us\n",
                         dev, latency_us);
        if (l > 0) {
            ssize_t w = write(pfd, msg, l);
            (void)w;
        }
        close(pfd);
    }
    Trace::Log("ALSAAudio", "output: %s (target %u us)", dev, latency_us);
    return true;
}

bool ALSAAudioDriver::InitDriver() { return openPcm(); }

void ALSAAudioDriver::CloseDriver() {
    if (pcm_) {
        snd_pcm_close(pcm_);
        pcm_ = 0;
    }
}

bool ALSAAudioDriver::StartDriver() {
    if (!pcm_)
        return false;
    written_ = 0;
    startNs_ = MidiClock::NowNs();
    snd_pcm_prepare(pcm_);

    feedSem_ = SysSemaphore::Create(0, 1);

    // Prebuffer: render the pool up to target before we start draining it.
    for (int i = 0; i < SOUND_BUFFER_COUNT && needsBuffering(); i++)
        OnNewBufferNeeded();

    out_ = new ALSAThread(this, ALSAThread::OUT);
    feed_ = new ALSAThread(this, ALSAThread::FEED);
    out_->Start();
    feed_->Start();
    return true;
}

void ALSAAudioDriver::StopDriver() {
    if (feed_) {
        feed_->RequestTermination(); // notifyFeed() wakes it
        SysProcessFactory::GetInstance()->JoinThread(*feed_);
        delete feed_;
        feed_ = 0;
    }
    if (out_) {
        out_->RequestTermination();
        if (pcm_)
            snd_pcm_drop(pcm_); // unblock a snd_pcm_writei() in progress
        SysProcessFactory::GetInstance()->JoinThread(*out_);
        delete out_;
        out_ = 0;
    }
    if (feedSem_) {
        delete feedSem_;
        feedSem_ = 0;
    }
}

void ALSAAudioDriver::notifyFeed() {
    if (feedSem_)
        feedSem_->Post();
}

// Producer: render ahead into the pool whenever the consumer signals room.
void ALSAAudioDriver::feedBody(ALSAThread *self) {
    // Elevated priority so render keeps the pool filled ahead of the out thread
    // (a starved pool = ALSA underrun = crackle). A notch below the out thread.
    struct sched_param sp;
    sp.sched_priority = 60;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);

    while (!self->ShouldStop()) {
        feedSem_->Wait();
        while (needsBuffering() && !self->ShouldStop())
            OnNewBufferNeeded();
    }
}

// Consumer: write each rendered block to ALSA, then set MidiClock's played-frame
// to (frames_written - snd_pcm_delay) = exactly what is audible right now.
void ALSAAudioDriver::outBody(ALSAThread *self) {
    // Run the output thread at real-time priority (SDL drove its audio callback
    // on an RT thread; our plain thread would otherwise be preempted mid-block,
    // draining the ALSA buffer -> crackle). Best-effort: ignore if not permitted.
    struct sched_param sp;
    sp.sched_priority = 70;
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);

    while (!self->ShouldStop()) {
        if (pool_[poolPlayPosition_].buffer_ == 0) {
            // Pool empty (producer behind): ask for more and wait briefly. We do
            // NOT write silence -- that would drift written_ vs produced_ and so
            // desync MIDI permanently. A real underrun just xruns + recovers.
            notifyFeed();
            usleep(1000);
            continue;
        }

        short *buf = (short *)pool_[poolPlayPosition_].buffer_;
        int frames = pool_[poolPlayPosition_].size_ / 4; // 4 bytes / stereo frame

        int off = 0;
        while (off < frames && !self->ShouldStop()) {
            snd_pcm_sframes_t w = snd_pcm_writei(pcm_, buf + off * 2, frames - off);
            if (w < 0) {
                // xrun / suspend: recover and re-try this block.
                int r = snd_pcm_recover(pcm_, (int)w, 1);
                if (r < 0) {
                    Trace::Error("ALSAAudio: recover failed: %s", snd_strerror(r));
                    break;
                }
                continue;
            }
            off += (int)w;
        }
        written_ += (unsigned long long)frames;

        // Exact output latency: frames still queued before the just-written audio
        // is heard. played = written - delay = the frame audible now.
        snd_pcm_sframes_t delay = 0;
        if (snd_pcm_delay(pcm_, &delay) == 0 && delay >= 0) {
            unsigned long long heard =
                (written_ > (unsigned long long)delay) ? written_ - delay : 0;
            MidiClock::GetInstance()->SetPlayed(heard);
        }

        SYS_FREE(pool_[poolPlayPosition_].buffer_);
        pool_[poolPlayPosition_].buffer_ = 0;
        poolPlayPosition_ = (poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;

        notifyFeed();
        onAudioBufferTick();
        MidiService::GetInstance()->Flush();
    }
}

int ALSAAudioDriver::GetPlayedBufferPercentage() {
    int fill = (poolQueuePosition_ - poolPlayPosition_ + SOUND_BUFFER_COUNT) %
               SOUND_BUFFER_COUNT;
    int target = settings_.preBufferCount_ > 0 ? settings_.preBufferCount_ : 8;
    int pct = 100 - (fill * 100 / target);
    if (pct < 0)
        pct = 0;
    if (pct > 100)
        pct = 100;
    return pct;
}

double ALSAAudioDriver::GetStreamTime() {
    return (double)written_ / 44100.0;
}
