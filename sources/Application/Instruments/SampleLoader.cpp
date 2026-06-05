#include "SampleLoader.h"
#include "WavFile.h"
#include "Services/Time/TimeService.h"
#include "System/Console/Trace.h"

SampleLoader::SampleLoader()
 : qHead_(0), qTail_(0), qCount_(0), current_(0), started_(false) {
	for (int i=0;i<SAMPLE_LOADER_QUEUE;i++) queue_[i]=0 ;
}

SampleLoader::~SampleLoader() { StopLoader() ; }

void SampleLoader::StartLoader() {
	if (started_) return ;
	started_=true ;
	Start() ;
}

void SampleLoader::StopLoader() {
	if (!started_) return ;
	RequestTermination() ;
	// Real join (SDL_WaitThread under the hood): the worker has returned and is no
	// longer touching any WavFile before we come back -- safe to free them after.
	SysProcessFactory::GetInstance()->JoinThread(*this) ;
	started_=false ;
}

void SampleLoader::Enqueue(WavFile *w) {
	if (!w) return ;
	lock_.Lock() ;
	if (qCount_<SAMPLE_LOADER_QUEUE) {
		queue_[qTail_]=w ;
		qTail_=(qTail_+1)%SAMPLE_LOADER_QUEUE ;
		qCount_++ ;
	}
	lock_.Unlock() ;
}

void SampleLoader::WaitIdle() {
	if (!started_) return ;
	for (;;) {
		lock_.Lock() ;
		bool idle=(qCount_==0 && current_==0) ;
		lock_.Unlock() ;
		if (idle) return ;
		TimeService::GetInstance()->Sleep(1) ;
	}
}

bool SampleLoader::Execute() {
	while (!shouldTerminate()) {
		WavFile *w=0 ;
		lock_.Lock() ;
		if (qCount_>0) {
			w=queue_[qHead_] ;
			qHead_=(qHead_+1)%SAMPLE_LOADER_QUEUE ;
			qCount_-- ;
			current_=w ;       // published as "in flight" until we clear it below
		}
		lock_.Unlock() ;
		if (!w) { TimeService::GetInstance()->Sleep(5) ; continue ; }

		// Decode the whole sample into its (already allocated) buffer in chunks,
		// advancing the watermark after each so playback can become ready ASAP.
		long total=w->GetSize(-1) ;
		long done=0 ;
		const long CHUNK=8192 ; // frames per step
		while (done<total && !shouldTerminate()) {
			long c=total-done ; if (c>CHUNK) c=CHUNK ;
			if (!w->DecodeRange(done,c)) break ; // read/alloc error: stop, leave rest
			done+=c ;
			w->SetLoadedFrames(done) ;
		}
		w->Close() ; // release the file handle (keeps samples_); done or aborted
		lock_.Lock() ; current_=0 ; lock_.Unlock() ;
	}
	return false ;
}
