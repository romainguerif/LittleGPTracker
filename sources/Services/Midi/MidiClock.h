#ifndef _MIDI_CLOCK_H_
#define _MIDI_CLOCK_H_

#include "Foundation/T_Singleton.h"

// Shared audio<->MIDI frame clock for jitter-free, audio-locked MIDI output.
//
// Two frame counters live in the same "frames since playback start" space:
//   - produced_ : advanced by the render thread per rendered block (the music the
//     sequencer has GENERATED so far). MIDI messages are stamped with this value
//     at generation time = the frame at which that musical instant will be heard.
//   - played_   : advanced by the audio output callback as audio is actually sent
//     out (the music the listener HEARS so far). Published with a monotonic
//     timestamp so a consumer can interpolate the exact current played-frame
//     between callback updates (which are coarse, ~one audio buffer apart).
//
// A precise MIDI scheduler thread then sends each stamped message the instant
// InterpolatedPlayed() reaches its stamp -> MIDI lands in sync with the audio
// actually playing, with sub-millisecond jitter, regardless of buffer sizes.
//
// Threads: produced_ is written/read on the render(buffering) thread only.
// played_/anchor are written by the audio callback, read by the scheduler via a
// seqlock. clock_gettime(CLOCK_MONOTONIC) only -> portable (Linux + macOS).
class MidiClock : public T_Singleton<MidiClock> {
public:
	MidiClock();

	// Call at playback start: zero both counters, set the rate.
	void Reset(int sampleRate);

	// --- produced side (render / buffering thread) ---
	unsigned long long CurrentProduced() const { return produced_; }
	void AdvanceProduced(int frames) { produced_ += (unsigned long long)frames; }

	// --- played side (audio callback thread) ---
	void AdvancePlayed(int frames);   // played_ += frames; republish anchor (now)

	// --- scheduler thread ---
	// Best estimate of the current played-frame, interpolated from the last anchor.
	unsigned long long InterpolatedPlayed() const;

	int SampleRate() const { return sampleRate_; }
	static unsigned long long NowNs(); // CLOCK_MONOTONIC nanoseconds

private:
	int sampleRate_;
	unsigned long long produced_;
	// seqlock-protected anchor (played frame + the monotonic ns it was published)
	volatile unsigned gen_;
	unsigned long long anchorPlayed_;
	unsigned long long anchorNs_;
};

#endif
