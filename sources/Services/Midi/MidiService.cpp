#include "MidiService.h"
#include "Application/Model/Config.h"
#include "Application/Player/SyncMaster.h"
#include "Services/Audio/AudioDriver.h"
#include "Services/Audio/Audio.h"
#include "Services/Midi/MidiClock.h"
#include "System/Console/Trace.h"
#include "System/Timer/Timer.h"

#ifdef SendMessage
#undef SendMessage
#endif

MidiService::MidiService()
    : T_SimpleList<MidiOutDevice>(true), inList_(true), device_(0),
      sendSync_(true), playing_(false) {
    for (int i = 0; i < MIDI_MAX_BUFFERS; i++) {
        queues_[i] = new T_SimpleList<MidiMessage>(true);
    }
    const char *delay = Config::GetInstance()->GetValue("MIDIDELAY");
    midiDelay_ = delay ? atoi(delay) : 1;

    const char *sendSync = Config::GetInstance()->GetValue("MIDISENDSYNC");
    if (sendSync) {
        sendSync_ = (strcmp(sendSync, "YES") == 0);
    }

    // Default MIDI-out timing offset (ms) from config; the project field can also
    // tweak it live. +ve = send MIDI earlier (lead) vs LGPT's own audio.
    midiOutOffsetFrames_ = 0;
    const char *moff = Config::GetInstance()->GetValue("MIDIOUTOFFSET");
    if (moff) SetMidiOutOffsetMs(atoi(moff));
};

void MidiService::SetMidiOutOffsetMs(int ms) {
    int rate = Audio::GetInstance()->GetSampleRate();
    if (rate <= 0) rate = 44100;
    midiOutOffsetFrames_ = (int)((long)ms * rate / 1000);
    if (device_) device_->SetTimingOffset(midiOutOffsetFrames_);
};

MidiService::~MidiService() { Close(); };

bool MidiService::Init() {
    Empty();
    inList_.Empty();
    buildDriverList();
    // Add a merger for the input
    merger_ = new MidiInMerger();
    IteratorPtr<MidiInDevice> it(inList_.GetIterator());
    for (it->Begin(); !it->IsDone(); it->Next()) {
        MidiInDevice &current = it->CurrentItem();
        merger_->Insert(current);
    }

    return true;
};

void MidiService::Close() { Stop(); };

I_Iterator<MidiInDevice> *MidiService::GetInIterator() {
    return inList_.GetIterator();
};

void MidiService::SelectDevice(const std::string &name) { deviceName_ = name; };

bool MidiService::Start() {
    currentPlayQueue_ = 0;
    currentOutQueue_ = 0;
    return true;
};

void MidiService::Stop() { stopDevice(); };

void MidiService::QueueMessage(MidiMessage &m) {
    if (!device_) return;

    // Transport realtime (start/continue/stop) must go out PROMPTLY and reliably:
    // they're not melodic, and a frame-stamp would get STUCK at stop (audio pauses
    // -> the played-frame freezes -> a frame-timed message never fires). Stamp 0 =
    // "send ASAP". Clock (0xF8) and notes are stamped with the produced frame so
    // the scheduler emits them locked to the audio that's actually playing.
    bool transport = (m.status_ == 0xFA || m.status_ == 0xFB || m.status_ == 0xFC);
    unsigned long long stamp = transport ? 0ULL : MidiClock::GetInstance()->CurrentProduced();

    if (device_->IsPrecise()) {
        // Hand straight to the device's precise scheduler (lock-free); bypass the
        // per-audio-buffer flush, which would otherwise stall around stop.
        MidiMessage ms(m.status_, m.data1_, m.data2_);
        ms.stamp_ = stamp;
        device_->SendMessage(ms);
        return;
    }

    // Non-precise device (e.g. RtMidi on desktop): keep the original queued path.
#ifdef _FEAT_MIDI_LOCK
    SysMutexLocker locker(queueMutex_);
#endif
    T_SimpleList<MidiMessage> *queue = queues_[currentPlayQueue_];
    MidiMessage *ms = new MidiMessage(m.status_, m.data1_, m.data2_);
    ms->stamp_ = stamp;
    queue->Insert(ms);
};

void MidiService::Trigger() {
    AdvancePlayQueue();
    // Only emit MIDI clock while actually PLAYING. Player::Update calls Trigger()
    // every audio block regardless of run state, and MidiSlice() is always true,
    // so without this gate the clock floods continuously once stopped -> the slave
    // (Model:Sample) never really stops. (Was masked before: device was DummyMidi.)
    if (device_ && sendSync_ && playing_) {
        SyncMaster *sm = SyncMaster::GetInstance();
        if (sm->MidiSlice()) {
            MidiMessage msg;
            msg.status_ = 0xF8;
            QueueMessage(msg);
        }
    }
}

void MidiService::AdvancePlayQueue() {
#ifdef _FEAT_MIDI_LOCK
    SysMutexLocker locker(queueMutex_);
#endif
    int next = (currentPlayQueue_ + 1) % MIDI_MAX_BUFFERS;
    if (queueMutex_.TryLock()) {
        // Only clear AFTER successful lock — avoids data loss
        queues_[next]->Empty();
        queueMutex_.Unlock();
        currentPlayQueue_ = next;
    }
}

void MidiService::Update(Observable &o, I_ObservableData *d) {
    AudioDriver::Event *event = (AudioDriver::Event *)d;
    if (event->type_ == AudioDriver::Event::ADET_DRIVERTICK) {
        onAudioTick();
    }
};

void MidiService::onAudioTick() {
    if (tickToFlush_ > 0) {
        if (--tickToFlush_ == 0) {
            flushOutQueue();
        }
    }
}

void MidiService::Flush() {
    tickToFlush_ = midiDelay_;
    if (tickToFlush_ == 0) {
        flushOutQueue();
    }
};

void MidiService::flushOutQueue() {
#ifdef _FEAT_MIDI_LOCK
    SysMutexLocker locker(queueMutex_);
#endif
    int next = (currentOutQueue_ + 1) % MIDI_MAX_BUFFERS;

    if (queueMutex_.TryLock()) {
        T_SimpleList<MidiMessage> *flushQueue = queues_[next];

        if (device_) {
            device_->SendQueue(*flushQueue);
        }

        flushQueue->Empty();
        currentOutQueue_ = next; // Advance only after safe flush
        queueMutex_.Unlock();
    }
}

/*
 * starts midi device
 */
void MidiService::startDevice() {
    IteratorPtr<MidiOutDevice> it(GetIterator());

    for (it->Begin(); !it->IsDone(); it->Next()) {
        MidiOutDevice &current = it->CurrentItem();
        if (!strcmp(deviceName_.c_str(), current.GetName())) {
            if (current.Init()) {
                if (current.Start()) {
                    Trace::Log("MidiService", "midi device %s started",
                               deviceName_.c_str());
                    device_ = &current;
                    device_->SetTimingOffset(midiOutOffsetFrames_);
                } else {
                    Trace::Log("MidiService", "midi device %s failed to start",
                               deviceName_.c_str());
                    current.Close();
                }
            }
            break;
        }
    }
};

/*
 * closes midi device
 */
void MidiService::stopDevice() {
    if (device_) {
        device_->Stop();
        device_->Close();
    }
    device_ = 0;
};

/*
 * starts midi device when playback starts
 */
void MidiService::OnPlayerStart() {
    if (deviceName_.size() != 0) {
        stopDevice();
        startDevice();
        deviceName_ = "";
    } else {
        startDevice();
    }

    playing_ = true; // gate clock generation ON
    if (sendSync_) {
        MidiMessage msg;
        msg.status_ = 0xFA;
        QueueMessage(msg);
    }
};

/*
 * queues midi stop message when player stops
 */
void MidiService::OnPlayerStop() {
    playing_ = false; // gate clock generation OFF immediately -> no clock after stop
    // Panic before stopping: All-Notes-Off (CC 123) on every channel, sent ASAP.
    // A note still held when the transport stops would otherwise get stuck on the
    // external gear, because its note-off can be scheduled past the point where the
    // played-frame freezes. Sent immediately (stamp 0) so it always reaches the wire.
    if (device_) {
        for (int ch = 0; ch < 16; ch++) {
            MidiMessage off((unsigned char)(0xB0 | ch), 123, 0);
            off.stamp_ = 0;
            if (device_->IsPrecise()) device_->SendMessage(off);
            else QueueMessage(off);
        }
    }
    if (sendSync_) {
        MidiMessage msg;
        msg.status_ = 0xFC;
        QueueMessage(msg);
    }
};
