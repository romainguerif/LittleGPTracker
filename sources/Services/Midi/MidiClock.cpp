#include "MidiClock.h"
#include <time.h>

MidiClock::MidiClock()
 : sampleRate_(44100), produced_(0), gen_(0), anchorPlayed_(0), anchorNs_(0) {}

unsigned long long MidiClock::NowNs() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (unsigned long long)ts.tv_sec * 1000000000ULL + (unsigned long long)ts.tv_nsec;
}

void MidiClock::Reset(int sampleRate) {
	if (sampleRate > 0) sampleRate_ = sampleRate;
	produced_ = 0;
	// publish a fresh zero anchor (seqlock write)
	gen_ = gen_ + 1; __sync_synchronize();
	anchorPlayed_ = 0;
	anchorNs_ = NowNs();
	__sync_synchronize(); gen_ = gen_ + 1;
}

void MidiClock::AdvancePlayed(int frames) {
	if (frames <= 0) return;
	unsigned long long p = anchorPlayed_ + (unsigned long long)frames;
	unsigned long long now = NowNs();
	gen_ = gen_ + 1; __sync_synchronize();   // -> odd: write in progress
	anchorPlayed_ = p;
	anchorNs_ = now;
	__sync_synchronize(); gen_ = gen_ + 1;   // -> even: write done
}

void MidiClock::SetPlayed(unsigned long long played) {
	// Monotonic guard: snd_pcm_delay() jitter must never move played backwards.
	if (played < anchorPlayed_) played = anchorPlayed_;
	unsigned long long now = NowNs();
	gen_ = gen_ + 1; __sync_synchronize();   // -> odd: write in progress
	anchorPlayed_ = played;
	anchorNs_ = now;
	__sync_synchronize(); gen_ = gen_ + 1;   // -> even: write done
}

unsigned long long MidiClock::InterpolatedPlayed() const {
	unsigned long long played, ns; unsigned g0, g1;
	int tries = 0;
	do {
		g0 = gen_; __sync_synchronize();
		played = anchorPlayed_;
		ns = anchorNs_;
		__sync_synchronize(); g1 = gen_;
	} while ((g0 != g1 || (g0 & 1)) && ++tries < 100);
	unsigned long long now = NowNs();
	if (now <= ns) return played;
	// played frames since the anchor = elapsed seconds * sample rate
	unsigned long long dNs = now - ns;
	unsigned long long dFrames = (dNs * (unsigned long long)sampleRate_) / 1000000000ULL;
	return played + dFrames;
}
