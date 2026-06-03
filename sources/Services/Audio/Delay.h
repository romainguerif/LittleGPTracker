#ifndef _DELAY_H_
#define _DELAY_H_

#include "Foundation/T_Singleton.h"
#include "Application/Utils/fixed.h"

class Variable ;

// Global stereo dub delay fed by PER-INSTRUMENT sends (not the whole master).
// Each voice writes (output * send) into sendBuf_; the delay processes that
// with filtered/ping-pong feedback and adds the wet result back to the master.
class Delay : public T_Singleton<Delay> {
public:
	Delay() ;
	~Delay() ;

	void Init() ;   // allocate the delay line + send buffer (once)
	void Close() ;

	bool active() ; // delay enabled?
	fixed *sendBuffer() { return sendBuf_ ; } // voices accumulate sends here
	void clearSend(int frames) ;              // clear the send accumulator
	bool processSend(fixed *master, int frames) ; // add the delay wet to master

	// Bus settings (persisted via the Project save/load bridge).
	Variable *onVar_ ;
	Variable *timeVar_ ;
	Variable *feedbackVar_ ;
	Variable *toneVar_ ;
	Variable *wetVar_ ;
	Variable *pingpongVar_ ;

private:
	fixed *ring_ ;
	fixed *sendBuf_ ;
	int ringFrames_ ;
	int sendFrames_ ;
	int writePos_ ;
	float lpL_, lpR_ ;
} ;
#endif
