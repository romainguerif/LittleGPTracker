#include "WavFileWriter.h"
#include "System/Console/Trace.h"
#include "Services/Time/TimeService.h"
#include <stdlib.h>

// Ring capacity per stem, in shorts (power of two). 1<<17 = 131072 shorts =
// ~1.5 s of stereo audio = 256 KB. With 16 stems that's ~4 MB of RAM, and it
// absorbs any realistic SD-card write stall without ever blocking the render.
#define WAV_RING_SHORTS (1u << 17)

// ---- background drain thread ------------------------------------------------

bool WavWriterThread::Execute() {
	while (!shouldTerminate()) {
		writer_->drainToDisk() ;
		TimeService::GetInstance()->Sleep(5) ; // poll ~every 5 ms
	}
	writer_->drainToDisk() ; // final flush of whatever is still queued
	return false ;
}

// ---- writer -----------------------------------------------------------------

WavFileWriter::WavFileWriter(const char *path):
	sampleCount_(0),
	file_(0),
	ring_(0),
	ringCapacity_(WAV_RING_SHORTS),
	ringMask_(WAV_RING_SHORTS - 1),
	writePos_(0),
	readPos_(0),
	dropped_(0),
	thread_(0)
{
	Path filePath(path) ;
	file_=FileSystem::GetInstance()->Open(filePath.GetPath().c_str(),"wb") ;
	if (file_) {

		// RIFF chunk

		unsigned int chunk ;
		chunk=Swap32(0x46464952) ;
		file_->Write(&chunk,1,4);
		unsigned int size ;
		size=0 ; // to be filled later
		file_->Write(&size,1,4);

		// WAVE chunk

		chunk=Swap32(0x45564157) ;
		file_->Write(&chunk,1,4);
		chunk=Swap32(0x20746D66) ;
		file_->Write(&chunk,1,4);
		size=Swap32(16) ;
		file_->Write(&size,1,4);

		unsigned short ushort ;
		ushort=Swap16(1) ; // compression
		file_->Write(&ushort,1,2);
		ushort=Swap16(2) ; // nChannels
		file_->Write(&ushort,1,2);
		unsigned int sampleRate=Swap32(44100) ;
		file_->Write(&sampleRate,1,4);

		unsigned int byteRate=Swap32(4*44100) ;
		file_->Write(&byteRate,1,4);

		ushort=Swap16(4) ; //  blockalign
		file_->Write(&ushort,1,2);

		ushort=Swap16(16) ; // bitPerSample
		file_->Write(&ushort,1,2);

		// data subchunk

		chunk = Swap32(0x61746164);
		file_->Write(&chunk,1,4);

		size=0 ;  // to be updated later
		file_->Write(&chunk,1,4);

		// allocate the ring and spin up the background drain thread
		ring_=(short *)malloc(ringCapacity_*sizeof(short)) ;
		if (ring_) {
			thread_=new WavWriterThread(this) ;
			thread_->Start() ;
		}
	} ;
} ;

WavFileWriter::~WavFileWriter() {
	Close();
}

void WavFileWriter::AddBuffer(fixed *bufferIn,int size) {

	if (!file_ || !ring_) return ;

	unsigned int shorts=(unsigned int)(size*2) ; // stereo
	unsigned int w=writePos_ ;
	unsigned int r=readPos_ ;
	unsigned int freeSlots=ringCapacity_-(w-r) ;
	if (shorts>freeSlots) {
		// Writer thread can't keep up: drop this chunk (a gap in the RECORDING)
		// rather than block the realtime audio thread.
		dropped_+=size ;
		return ;
	}

	fixed *p=bufferIn ;
	fixed f_32767=i2fp(32767) ;
	fixed f_m32768=i2fp(-32768) ;
	for (unsigned int i=0;i<shorts;i++) {
		fixed v=*p++ ;
		if (v>f_32767) {
			v=f_32767 ;
		} else if (v<f_m32768) {
			v=f_m32768 ;
		}
		ring_[(w+i)&ringMask_]=short(fp2i(v)) ;
	}
	__sync_synchronize() ; // publish the data before advancing the write pointer
	writePos_=w+shorts ;
}

void WavFileWriter::drainToDisk() {

	if (!file_ || !ring_) return ;

	unsigned int w=writePos_ ;
	__sync_synchronize() ; // read the data only after seeing the write pointer
	unsigned int r=readPos_ ;
	unsigned int avail=w-r ;
	if (avail==0) return ;

	// write in up to two contiguous chunks (the ring may wrap)
	unsigned int idx=r&ringMask_ ;
	unsigned int firstChunk=ringCapacity_-idx ;
	if (firstChunk>avail) firstChunk=avail ;
	file_->Write(ring_+idx,sizeof(short),firstChunk) ;
	if (avail>firstChunk) {
		file_->Write(ring_,sizeof(short),avail-firstChunk) ;
	}

	sampleCount_+=avail/2 ; // shorts -> stereo frames
	__sync_synchronize() ;
	readPos_=r+avail ;
}

void WavFileWriter::Close() {

	if (!file_) return ;

	// stop the drain thread (it does a final flush on its way out)
	if (thread_) {
		thread_->RequestTermination() ;
		while (!thread_->IsFinished()) {
			TimeService::GetInstance()->Sleep(1) ;
		}
		delete thread_ ;
		thread_=0 ;
	}
	drainToDisk() ; // safety: flush anything left, now single-threaded

	if (dropped_>0) {
		Trace::Error("WavFileWriter: dropped %d samples (disk too slow)",dropped_) ;
	}

	size_t len=file_->Tell() ;
	len=Swap32(len-8) ;
	file_->Seek(4,SEEK_SET) ;
	file_->Write(&len,4,1) ;

	file_->Seek(40,SEEK_SET) ;
	unsigned int dataSize=Swap32(sampleCount_*4) ;
	file_->Write(&dataSize,4,1) ;

	file_->Seek(0,SEEK_END) ;

	file_->Close() ;
	SAFE_DELETE(file_) ;
	if (ring_) {
		free(ring_) ;
		ring_=0 ;
	}
} ;
