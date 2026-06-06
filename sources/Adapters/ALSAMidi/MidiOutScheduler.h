#ifndef _MIDI_OUT_SCHEDULER_H_
#define _MIDI_OUT_SCHEDULER_H_

#include "System/Process/Process.h"
#include "System/Process/SysMutex.h"

class ALSARawMidiOutDevice;

// Precise, audio-locked MIDI output thread (jitter-free clock for external gear).
//
// The render side pushes each MIDI message stamped with the audio-frame at which
// it should be HEARD (MidiClock produced-frame). This thread emits the message
// the instant MidiClock::InterpolatedPlayed() reaches that frame -> the MIDI lands
// in sync with the audio that's actually playing, with sub-millisecond jitter,
// no matter how the audio is buffered. Replaces the old per-audio-buffer flush
// (which bursted clock pulses every ~one buffer => audible tempo wobble).
//
// SPSC: Push() runs on the render/buffering thread (single producer); Execute()
// is the single consumer. The actual byte write goes through the device's blocking
// fd (off the audio thread), so a momentarily-full kernel buffer can't stall audio.
class MidiOutScheduler : public SysThread {
public:
	MidiOutScheduler(ALSARawMidiOutDevice *dev);
	void StartScheduler();
	void StopScheduler();
	// from the render/buffering thread:
	void Push(unsigned long long stamp, unsigned char s, unsigned char d1, unsigned char d2);
	void Flush(); // drop everything pending (on stop)
	void SetOffsetFrames(long off) { offsetFrames_ = off; } // +lead / -lag vs audio
	virtual bool Execute();
private:
	ALSARawMidiOutDevice *dev_;
	enum { RING = 4096, MASK = RING - 1 };
	struct Ent { unsigned long long stamp; unsigned char s, d1, d2; };
	Ent ring_[RING];
	volatile unsigned wpos_;
	volatile unsigned rpos_;
	long offsetFrames_;
	bool started_;
	// Push() is called from MORE THAN ONE thread (the render thread for clock/notes,
	// and the player-control thread for transport / all-notes-off), so the producer
	// side must be serialized. The consumer (Execute) stays lock-free.
	SysMutex pushLock_;
};

#endif
