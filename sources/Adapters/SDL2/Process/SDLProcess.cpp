
#include "SDLProcess.h"
#include <SDL2/SDL_thread.h>

int _SDLStartThread(void *argp) {
	SysThread *play=(SysThread *)argp ;
	play->startExecution() ;
	return 0 ;
}

bool SDLProcessFactory::BeginThread(SysThread& thread) {
	// Keep the handle so the thread can be joined (SDL_WaitThread) at teardown
	// instead of being leaked / guessed at with a fixed delay. (Signature kept
	// exactly as the original call to match this build's SDL2 headers.)
	SDL_Thread *t = SDL_CreateThread(_SDLStartThread,&thread);
	thread.SetOSHandle(t) ;
	return (t!=0) ;
}

SysSemaphore *SDLProcessFactory::CreateNewSemaphore(int initialcount, int maxcount) {
	return new SDLSysSemaphore(initialcount,maxcount) ;
} ;

SDLSysSemaphore::SDLSysSemaphore(int initialcount,int maxcount) {
	handle_=SDL_CreateSemaphore(initialcount) ;
} ;

SDLSysSemaphore::~SDLSysSemaphore() {
	if (handle_) {
		SDL_DestroySemaphore(handle_) ;
		handle_=0 ;
	}
} ;

SysSemaphoreResult SDLSysSemaphore::Wait() {
	if (!handle_) {
		return SSR_INVALID ;
	} ;
	return (SysSemaphoreResult)SDL_SemWait(handle_) ;
} ;

SysSemaphoreResult SDLSysSemaphore::TryWait() {
	if (!handle_) {
		return SSR_INVALID ;
	} ;
	return (SysSemaphoreResult)0;
}

SysSemaphoreResult SDLSysSemaphore::WaitTimeout(unsigned long timeout) {
	if (!handle_) {
		return SSR_INVALID ;
	} ;
	return (SysSemaphoreResult)0;
} ;

SysSemaphoreResult SDLSysSemaphore::Post() {
	if (!handle_) {
		return SSR_INVALID ;
	} ;
	return (SysSemaphoreResult)SDL_SemPost(handle_) ;
} ;
