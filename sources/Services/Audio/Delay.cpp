#include "Delay.h"
#include "Foundation/Variables/Variable.h"
#include "Foundation/Types/Types.h"
#include "System/System/System.h"
#include <math.h>

#define DELAY_MAX_FRAMES 88200 // 2 seconds @ 44100

Delay::Delay() {
	ring_ = 0 ;
	ringFrames_ = DELAY_MAX_FRAMES ;
	writePos_ = 0 ;
	lpL_ = lpR_ = 0.0f ;
	onVar_       = new Variable("delay", MAKE_FOURCC('D','L','O','N'), false) ;
	timeVar_     = new Variable("delay time", MAKE_FOURCC('D','L','T','M'), 0x60) ;
	feedbackVar_ = new Variable("delay feedback", MAKE_FOURCC('D','L','F','B'), 0x90) ;
	toneVar_     = new Variable("delay tone", MAKE_FOURCC('D','L','T','O'), 0x70) ;
	wetVar_      = new Variable("delay wet", MAKE_FOURCC('D','L','W','T'), 0x60) ;
	pingpongVar_ = new Variable("delay pingpong", MAKE_FOURCC('D','L','P','P'), false) ;
}

Delay::~Delay() {
	Close() ;
}

void Delay::Init() {
	if (!ring_) {
		ring_ = (fixed *)SYS_MALLOC(ringFrames_ * 2 * sizeof(fixed)) ;
		for (int i = 0; i < ringFrames_ * 2; i++) ring_[i] = 0 ;
	}
}

void Delay::Close() {
	SAFE_FREE(ring_) ;
}

bool Delay::process(fixed *buf, int frames) {
	if (!ring_ || onVar_->GetInt() == 0) return false ;

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
		fixed inL = buf[i*2] ;
		fixed inR = buf[i*2+1] ;

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

		// write input + feedback into the line (clamped to avoid overflow)
		float rL = (float)inL + wfL ;
		float rR = (float)inR + wfR ;
		if (rL > hi) rL = hi ; else if (rL < lo) rL = lo ;
		if (rR > hi) rR = hi ; else if (rR < lo) rR = lo ;
		ring_[writePos_*2]   = (fixed)rL ;
		ring_[writePos_*2+1] = (fixed)rR ;
		writePos_++ ;
		if (writePos_ >= ringFrames_) writePos_ = 0 ;

		// dry + wet out (clamped)
		float outL = (float)inL + wet * dL ;
		float outR = (float)inR + wet * dR ;
		if (outL > hi) outL = hi ; else if (outL < lo) outL = lo ;
		if (outR > hi) outR = hi ; else if (outR < lo) outR = lo ;
		buf[i*2]   = (fixed)outL ;
		buf[i*2+1] = (fixed)outR ;
	}
	return true ;
}
