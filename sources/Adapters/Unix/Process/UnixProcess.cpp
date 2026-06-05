
#include "UnixProcess.h"
#include "System/Console/Trace.h"
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>

void *_UnixStartThread(void *p) {
	SysThread *play=(SysThread *)p ;
	play->startExecution() ;
	return NULL ;
}

bool UnixProcessFactory::BeginThread(SysThread& thread) {
pthread_t pthread ;

	pthread_create(&pthread,0,_UnixStartThread,&thread) ;
	return true ;
}

SysSemaphore *UnixProcessFactory::CreateNewSemaphore(int initialcount, int maxcount) {
	return new UnixSysSemaphore(initialcount,maxcount) ;
} ;

UnixSysSemaphore::UnixSysSemaphore(int initialcount,int maxcount) {
	// CRITICAL FIX: this used to be
	//     sem_=sem_open("n0ssemaphore",O_CREAT,S_IRUSR|S_IWUSR,0) ;
	// i.e. a NAMED POSIX semaphore with a SINGLE hardcoded name. sem_open()
	// without O_EXCL returns the SAME kernel object for an existing name, so
	// EVERY SysSemaphore in the process aliased ONE counter: the audio buffering
	// wakeup semaphore_, AND the multicore worker's go_ AND done_. The SDL audio
	// callback Posts that counter on every consume/underrun (Notify), so its
	// tokens leaked into the worker's go_/done_ rendezvous -> the worker woke an
	// extra time, rendered the NEXT generation early into the shared childScratch_
	// while the audio thread was still summing the current one -> buses 8-16
	// (tracks 9-16 + the preview stream) got double-advanced and summed from the
	// wrong block = "play the attack then cut". (It also ignored initialcount and
	// the dtor sem_unlink()'d the shared name out from under live handles.)
	// A private, unnamed, per-instance semaphore gives go_/done_/semaphore_ three
	// independent counters and honors initialcount.
	(void)maxcount ;
	sem_init(&sem_, 0 /*pshared=0: shared between threads of this process only*/, (unsigned)initialcount) ;
} ;

UnixSysSemaphore::~UnixSysSemaphore() {
  sem_destroy(&sem_) ;
} ;

SysSemaphoreResult UnixSysSemaphore::Wait() {
	while (sem_wait(&sem_)!=0 && errno==EINTR) {} // restart if interrupted by a signal
	return SSR_NO_ERROR ;
} ;

SysSemaphoreResult UnixSysSemaphore::TryWait() {
	return (sem_trywait(&sem_)==0) ? SSR_NO_ERROR : SSR_BUSY ;
}

SysSemaphoreResult UnixSysSemaphore::WaitTimeout(unsigned long timeout) {
	return SSR_INVALID ;
} ;

SysSemaphoreResult UnixSysSemaphore::Post() {
	sem_post(&sem_) ;
	return SSR_NO_ERROR ;
} ;
