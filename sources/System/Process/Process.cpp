
#include "Process.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"

bool SysThread::Start() {
	return SysProcessFactory::GetInstance()->BeginThread(*this) ;
} ;

bool SysThread::startExecution() {
	isFinished_=false ;
	shouldTerminate_=false ;
	bool result=false ;
	try {
			result=Execute() ;
	} catch (...) {
		NInvalid ;
	}
	isFinished_=true ;
	return result ;
}

bool SysThread::IsFinished() {
	return isFinished_ ;
}

bool SysThread::shouldTerminate() {
	return shouldTerminate_ ;
}

void SysThread::RequestTermination() {
	shouldTerminate_=true ;
}


// Default join for platforms without a real OS join: spin until the thread flags
// completion. The memory barrier forces the flag to be re-read each iteration
// (otherwise -O3 could hoist the load out of the loop and hang). SDL overrides
// this with a proper SDL_WaitThread (no spin).
void SysProcessFactory::JoinThread(SysThread &t) {
	while (!t.IsFinished()) { __sync_synchronize() ; }
}

SysSemaphore *SysSemaphore::Create(int initialcount,int maxcount) {
	return SysProcessFactory::GetInstance()->CreateNewSemaphore(initialcount,maxcount) ;
} ;

