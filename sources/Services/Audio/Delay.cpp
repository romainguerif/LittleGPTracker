#include "Delay.h"
#include "Foundation/Variables/Variable.h"
#include "Foundation/Types/Types.h"
#include "System/System/System.h"
#include "Services/Audio/AudioOut.h" // MIX_BUFFER_SIZE
#include "Application/Model/Song.h"   // SONG_CHANNEL_COUNT
#include <math.h>

#define DELAY_MAX_FRAMES 88200 // 2 seconds @ 44100

Delay::Delay() {
	ring_ = 0 ;
	sendBuf_ = 0 ;
	sendMix_ = 0 ;
	ringFrames_ = DELAY_MAX_FRAMES ;
	sendFrames_ = 0 ;
	sendStride_ = 0 ;
	writePos_ = 0 ;
	lpL_ = lpR_ = 0.0f ;
	onVar_       = new Variable("delay", MAKE_FOURCC('D','L','O','N'), false) ;
	timeVar_     = new Variable("delaytime", MAKE_FOURCC('D','L','T','M'), 0x60) ;
	feedbackVar_ = new Variable("delayfeedback", MAKE_FOURCC('D','L','F','B'), 0x90) ;
	toneVar_     = new Variable("delaytone", MAKE_FOURCC('D','L','T','O'), 0x70) ;
	wetVar_      = new Variable("delaywet", MAKE_FOURCC('D','L','W','T'), 0x80) ;
	pingpongVar_ = new Variable("delaypingpong", MAKE_FOURCC('D','L','P','P'), false) ;
}

Delay::~Delay() {
	Close() ;
}

void Delay::Init() {
	if (!ring_) {
		ring_ = (fixed *)SYS_MALLOC(ringFrames_ * 2 * sizeof(fixed)) ;
		for (int i = 0; i < ringFrames_ * 2; i++) ring_[i] = 0 ;
	}
	if (!sendBuf_) {
		sendFrames_ = MIX_BUFFER_SIZE / 4 ; // MIX_BUFFER_SIZE bytes -> stereo fixed frames
		sendStride_ = sendFrames_ * 2 ;     // fixeds per channel region
		sendBuf_ = (fixed *)SYS_MALLOC(SONG_CHANNEL_COUNT * sendStride_ * sizeof(fixed)) ;
		for (int i = 0; i < SONG_CHANNEL_COUNT * sendStride_; i++) sendBuf_[i] = 0 ;
		sendMix_ = (fixed *)SYS_MALLOC(sendStride_ * sizeof(fixed)) ;
		for (int i = 0; i < sendStride_; i++) sendMix_[i] = 0 ;
	}
}

void Delay::Close() {
	SAFE_FREE(ring_) ;
	SAFE_FREE(sendBuf_) ;
	SAFE_FREE(sendMix_) ;
}

fixed *Delay::sendBuffer(int channel) {
	if (channel < 0 || channel >= SONG_CHANNEL_COUNT) channel = 0 ;
	return sendBuf_ + channel * sendStride_ ;
}

bool Delay::active() {
	return ring_ && sendBuf_ && onVar_->GetInt() != 0 ;
}

void Delay::clearSend(int frames) {
	if (!sendBuf_) return ;
	if (frames > sendFrames_) frames = sendFrames_ ;
	for (int ch = 0; ch < SONG_CHANNEL_COUNT; ch++) {
		SYS_MEMSET(sendBuf_ + ch * sendStride_, 0, frames * 2 * sizeof(fixed)) ;
	}
}

// Read the accumulated per-instrument sends, run them through the delay line
// with filtered ping-pong feedback, and ADD the wet echoes onto the master.
// The dry signal is already in the master (normal channel mix), so we only add
// the wet here -> this is a proper send bus, not a master insert.
bool Delay::processSend(fixed *master, int frames) {
	if (!active()) return false ;
	if (frames > sendFrames_) frames = sendFrames_ ;

	// Sum every channel's send into sendMix_ (the parallel render filled each
	// channel's own region; here, single-threaded, we merge them).
	int n = frames * 2 ;
	for (int i = 0; i < n; i++) sendMix_[i] = 0 ;
	for (int ch = 0; ch < SONG_CHANNEL_COUNT; ch++) {
		fixed *s = sendBuf_ + ch * sendStride_ ;
		for (int i = 0; i < n; i++) sendMix_[i] += s[i] ;
	}

	// Free-running time: ~22 ms .. 2 s (tempo sync is a future refinement).
	int delayFrames = 1000 + (int)((timeVar_->GetInt() / 255.0f) * (ringFrames_ - 1001)) ;
	if (delayFrames < 1) delayFrames = 1 ;
	if (delayFrames >= ringFrames_) delayFrames = ringFrames_ - 1 ;

	float fb     = (feedbackVar_->GetInt() / 255.0f) * 0.98f ;
	float wet    = wetVar_->GetInt() / 255.0f ;
	float lpCoef = 0.04f + (toneVar_->GetInt() / 255.0f) * 0.93f ; // higher tone = brighter repeats
	bool pingpong = pingpongVar_->GetInt() != 0 ;

	const float hi = (float)i2fp(32767) ;
	const float lo = (float)i2fp(-32768) ;

	for (int i = 0; i < frames; i++) {
		float inL = (float)sendMix_[i*2] ;
		float inR = (float)sendMix_[i*2+1] ;

		int rp = writePos_ - delayFrames ;
		if (rp < 0) rp += ringFrames_ ;
		float dL = (float)ring_[rp*2] ;
		float dR = (float)ring_[rp*2+1] ;

		// 1-pole low-pass in the feedback path darkens each repeat (dub grain)
		lpL_ += (dL - lpL_) * lpCoef ;
		lpR_ += (dR - lpR_) * lpCoef ;
		float fL = lpL_ * fb ;
		float fR = lpR_ * fb ;
		float wfL = pingpong ? fR : fL ;
		float wfR = pingpong ? fL : fR ;

		// write send input + feedback into the line (clamped to avoid overflow)
		float rL = inL + wfL ;
		float rR = inR + wfR ;
		if (rL > hi) rL = hi ; else if (rL < lo) rL = lo ;
		if (rR > hi) rR = hi ; else if (rR < lo) rR = lo ;
		ring_[writePos_*2]   = (fixed)rL ;
		ring_[writePos_*2+1] = (fixed)rR ;
		writePos_++ ;
		if (writePos_ >= ringFrames_) writePos_ = 0 ;

		// add the wet echoes onto the master (clamped)
		float outL = (float)master[i*2]   + wet * dL ;
		float outR = (float)master[i*2+1] + wet * dR ;
		if (outL > hi) outL = hi ; else if (outL < lo) outL = lo ;
		if (outR > hi) outR = hi ; else if (outR < lo) outR = lo ;
		master[i*2]   = (fixed)outL ;
		master[i*2+1] = (fixed)outR ;
	}
	return true ;
}
