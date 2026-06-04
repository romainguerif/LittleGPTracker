#ifndef _WAV_FILE_WRITER_H_
#define _WAV_FILE_WRITER_H_

#include "System/FileSystem/FileSystem.h"
#include "System/Process/Process.h"
#include "Application/Utils/fixed.h"

class WavFileWriter ;

// Background thread that drains one writer's ring buffer to disk. Keeps the
// SD-card write latency OFF the realtime audio thread, so recording (esp. 16
// stems at once) never stalls the render -> no crackle.
class WavWriterThread : public SysThread {
public:
	WavWriterThread(WavFileWriter *w) : writer_(w) {} ;
	virtual bool Execute() ;
private:
	WavFileWriter *writer_ ;
} ;

class WavFileWriter {
public:
	WavFileWriter(const char *path) ;
	~WavFileWriter() ;
	// Called from the AUDIO thread: converts to 16-bit and enqueues into the ring
	// (no disk I/O). size is in samples (stereo frames).
	void AddBuffer(fixed *,int size) ;
	void Close() ;
	// Called from the WRITER thread: flush whatever is queued to disk.
	void drainToDisk() ;
private:
	int sampleCount_ ;            // frames written (for the WAV header)
	I_File *file_ ;
	// Lock-free single-producer/single-consumer ring of interleaved 16-bit
	// stereo samples. writePos_/readPos_ are free-running counters (their
	// difference is the fill); indexing is masked to the (power-of-two) capacity.
	short *ring_ ;
	unsigned int ringCapacity_ ; // in shorts
	unsigned int ringMask_ ;
	volatile unsigned int writePos_ ; // audio thread only
	volatile unsigned int readPos_ ;  // writer thread only
	unsigned int dropped_ ;           // samples dropped when the ring is full
	WavWriterThread *thread_ ;
} ;
#endif
