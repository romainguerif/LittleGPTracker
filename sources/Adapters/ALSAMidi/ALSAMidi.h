#ifndef _ALSA_MIDI_H_
#define _ALSA_MIDI_H_

#include "Services/Midi/MidiService.h"
#include "Services/Midi/MidiOutDevice.h"
#include <string>

// Direct ALSA rawmidi output for minimal handheld Linux (Trimui/Allwinner etc.).
// Writes MIDI bytes straight to /dev/snd/midiCxDy with plain open()/write() --
// NO libasound, NO snd-seq dependency (RtMidi needs the ALSA sequencer, often
// absent on these kernels; the USB-MIDI rawmidi char devices usually exist).
// This is the robust, dependency-free path to drive external gear from the device.
class MidiOutScheduler;

class ALSARawMidiOutDevice : public MidiOutDevice {
public:
	ALSARawMidiOutDevice(const char *devPath, const char *name);
	virtual ~ALSARawMidiOutDevice();
	virtual bool Init();
	virtual void Close();
	virtual bool Start();
	virtual void Stop();
	// Called by the base flush: DEFERS the message to the precise scheduler, timed
	// by its frame stamp (jitter-free, audio-locked). Does not write immediately.
	virtual void SendMessage(MidiMessage &m);
	// Actual byte write to the rawmidi fd. Called ONLY by the scheduler thread at
	// the exact moment the message is due.
	void WriteRaw(unsigned char status, unsigned char d1, unsigned char d2);
	virtual void SetTimingOffset(int frames);
	virtual bool IsPrecise() { return true; }
private:
	std::string devPath_;
	int fd_;
	MidiOutScheduler *scheduler_;
};

class ALSARawMidiService : public MidiService {
public:
	ALSARawMidiService();
	virtual ~ALSARawMidiService();
protected:
	virtual void buildDriverList();
};

#endif
