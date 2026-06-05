
#ifndef _WAV_FILE_H_
#define _WAV_FILE_H_

#include "System/FileSystem/FileSystem.h"
#include "SoundSource.h"

class WavFile:public SoundSource {

protected: // Factory - see Load method
	WavFile(I_File *file) ;
public:
	virtual ~WavFile() ;
	static WavFile *Open(const char *) ;
	virtual void *GetSampleBuffer(int note) ;
	virtual int GetSize(int note) ;
	virtual int GetSampleRate(int note) ;
	virtual int GetChannelCount(int note) ;
	virtual int GetRootNote(int note) ;
	bool GetBuffer(long start,long sampleCount) ; // values in smples
	void Close() ;
	virtual bool IsMulti() {return false ; } ;
	virtual bool IsReady() ; // false while a background stream-in is still filling

	// ---- background "stream-in" loading -------------------------------------
	// Same end result as GetBuffer(0,GetSize()) -- the whole sample ends up in
	// samples_ -- but filled INCREMENTALLY so a big file does not freeze the UI.
	// A background loader calls PrepareStreamLoad() once, then DecodeRange() in
	// chunks; after each chunk it publishes the watermark via SetLoadedFrames().
	// Playback reads up to GetLoadedFrames() and treats the rest as not-yet-there.
	// The existing synchronous GetBuffer() path is untouched.
	bool PrepareStreamLoad() ;                 // alloc samples_ full size, loadedFrames_=0
	bool DecodeRange(long frameStart,long frameCount) ; // decode [start,+count) into samples_
	long GetLoadedFrames() ;                   // acquire-load of the watermark
	void SetLoadedFrames(long n) ;             // release-store of the watermark

protected:
	long readBlock(long position,long count) ;
private:
	I_File *file_ ;  // File
	void *readBuffer_ ; // Temp read buffer
	int readBufferSize_; // Read buffer size
	short *samples_ ; // sample buffer size (16 bits)
	int sampleBufferSize_ ;
	int size_ ; // number of samples
	int sampleRate_ ; // sample rate
	int channelCount_ ; // mono / stereo
	int bytePerSample_ ; // original file: 1/2/3/4 bytes (8/16/24/32 bit)
	bool isFloat_ ; // true if the data is 32-bit IEEE float (else signed int)
	int dataPosition_ ; // offset in file to get to data
	long filePos_ ; // current file read position, to skip redundant seeks
	volatile long loadedFrames_ ; // stream-in watermark: frames decoded & visible to playback
	bool streaming_ ; // true if this file is being filled by the background loader

	static int bufferChunkSize_ ;
	static bool initChunkSize_ ;
} ;
#endif
