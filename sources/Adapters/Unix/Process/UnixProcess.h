#ifndef _Unix_PROCESS_H_
#define _Unix_PROCESS_H_

#include "System/Process/Process.h"
#include <pthread.h>
#include <semaphore.h>

class UnixProcessFactory:public SysProcessFactory {
	bool BeginThread(SysThread &) ;
	virtual SysSemaphore *CreateNewSemaphore(int initialcount = 0, int maxcount = 0) ;
} ;

class UnixSysSemaphore:public SysSemaphore {
public:
	UnixSysSemaphore(int initialcount = 0, int maxcount = 0) ;
	virtual ~UnixSysSemaphore() ;
	virtual SysSemaphoreResult Wait() ;
	virtual SysSemaphoreResult TryWait() ;
	virtual SysSemaphoreResult WaitTimeout(unsigned long) ;
	virtual SysSemaphoreResult Post() ;
private:
	// Private, unnamed, per-instance POSIX semaphore. (Was sem_t* opened by NAME
	// with a single hardcoded string -> every SysSemaphore aliased ONE kernel
	// counter, see the .cpp.)
	sem_t sem_ ;
} ;
#endif
