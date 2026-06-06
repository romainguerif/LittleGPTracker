#include "MidiOutScheduler.h"
#include "ALSAMidi.h"
#include "Services/Midi/MidiClock.h"
#include <time.h>

MidiOutScheduler::MidiOutScheduler(ALSARawMidiOutDevice *dev)
 : dev_(dev), wpos_(0), rpos_(0), offsetFrames_(0), started_(false) {}

void MidiOutScheduler::StartScheduler() {
	if (started_) return;
	started_ = true;
	Start();
}

void MidiOutScheduler::StopScheduler() {
	if (!started_) return;
	RequestTermination();
	SysProcessFactory::GetInstance()->JoinThread(*this);
	started_ = false;
}

void MidiOutScheduler::Push(unsigned long long stamp, unsigned char s, unsigned char d1, unsigned char d2) {
	// Serialize producers (multiple threads call this). Consumer is lock-free.
	SysMutexLocker lock(pushLock_);
	unsigned w = wpos_;
	if (w - rpos_ >= RING) return; // ring full -> drop (won't happen at MIDI rates)
	Ent &e = ring_[w & MASK];
	e.stamp = stamp; e.s = s; e.d1 = d1; e.d2 = d2;
	__sync_synchronize(); // publish the entry before advancing the write index
	wpos_ = w + 1;
}

void MidiOutScheduler::Flush() {
	rpos_ = wpos_; // consumer-side drop of everything pending
}

bool MidiOutScheduler::Execute() {
	while (!shouldTerminate()) {
		unsigned long long played = MidiClock::GetInstance()->InterpolatedPlayed();
		while (rpos_ != wpos_) {
			Ent &e = ring_[rpos_ & MASK];
			__sync_synchronize(); // see the entry's fields after the index
			long long due = (long long)e.stamp - (long long)offsetFrames_;
			if (due > (long long)played) break; // front not due yet; stamps are
			                                     // non-decreasing -> none are
			dev_->WriteRaw(e.s, e.d1, e.d2);
			__sync_synchronize();
			rpos_ = rpos_ + 1;
		}
		// Short precise poll: ~250us -> sub-millisecond emission jitter, locked to
		// the interpolated audio play position. Negligible CPU on the A53.
		struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = 250000;
		nanosleep(&ts, 0);
	}
	return false;
}
