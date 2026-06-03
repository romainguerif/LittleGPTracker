#ifndef _DELAY_H_
#define _DELAY_H_

#include "Foundation/T_Singleton.h"
#include "Application/Utils/fixed.h"

class Variable ;

// Global stereo dub delay applied to the final master mix.
// Filtered (darkening) feedback + optional ping-pong = the Basic Channel grain.
class Delay : public T_Singleton<Delay> {
public:
	Delay() ;
	~Delay() ;

	void Init() ;  // allocate the delay line (once)
	void Close() ;
	bool process(fixed *buffer, int frames) ; // in-place; returns true if active

	// Exposed for the UI (ProjectView). Owned here (not yet persisted).
	Variable *onVar_ ;
	Variable *timeVar_ ;
	Variable *feedbackVar_ ;
	Variable *toneVar_ ;
	Variable *wetVar_ ;
	Variable *pingpongVar_ ;

private:
	fixed *ring_ ;
	int ringFrames_ ;
	int writePos_ ;
	float lpL_, lpR_ ; // feedback low-pass state
} ;
#endif
