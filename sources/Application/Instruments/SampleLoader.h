#ifndef _SAMPLE_LOADER_H_
#define _SAMPLE_LOADER_H_

#include "System/Process/Process.h"
#include "System/Process/SysMutex.h"

class WavFile ;

// Background thread that fills WavFile sample buffers from the SD card, so a big
// sample no longer freezes the UI while it loads. The pool Open()s + PrepareStream
// Load()s a WavFile then Enqueue()s it here; the loader decodes it in chunks,
// publishing the watermark (WavFile::SetLoadedFrames) after each, then Close()s the
// file. Playback gates on WavFile::IsReady() until the fill completes -- the hot
// render loop is NOT touched.
//
// Lifecycle / anti-UAF: ALL SamplePool mutations run on the UI thread. Before the
// pool frees ANY WavFile it calls WaitIdle() (or StopLoader() at shutdown), which
// blocks until the loader holds no file in flight. After that the loader touches
// nothing until the next Enqueue -- and Enqueue can't run concurrently (same UI
// thread) -- so the pool can safely delete WavFiles it owns. No use-after-free.
#define SAMPLE_LOADER_QUEUE 256

class SampleLoader : public SysThread {
public:
	SampleLoader() ;
	virtual ~SampleLoader() ;
	void StartLoader() ;       // spin up the worker thread (idempotent)
	void StopLoader() ;        // terminate + join (call before freeing anything)
	void Enqueue(WavFile *w) ; // queue a prepared WavFile to fill (UI thread)
	void WaitIdle() ;          // block until nothing is in flight (UI thread)
	virtual bool Execute() ;   // thread body
private:
	SysMutex lock_ ;
	WavFile *queue_[SAMPLE_LOADER_QUEUE] ;
	int qHead_, qTail_, qCount_ ;
	WavFile *current_ ;        // file being filled right now (guarded by lock_)
	bool started_ ;
} ;
#endif
