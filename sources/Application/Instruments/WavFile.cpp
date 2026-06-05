
#include "WavFile.h"
#include "System/Console/Trace.h"
#include "Foundation/Types/Types.h"
#include "Services/Time/TimeService.h"
#include "Application/Model/Config.h"
#include <stdlib.h>
#include <math.h>

int WavFile::bufferChunkSize_=-1 ;
bool WavFile::initChunkSize_=true ;

short Swap16 (short from)
{
#ifdef __ppc__
	short result;
	((char*)&result)[0] = ((char*)&from)[1];
	((char*)&result)[1] = ((char*)&from)[0];
	return  result;
#else
	return from;
#endif	
}

int Swap32 (int from)
{
#ifdef __ppc__
	int result;
	((char*)&result)[0] = ((char*)&from)[3];
	((char*)&result)[1] = ((char*)&from)[2];
	((char*)&result)[2] = ((char*)&from)[1];
	((char*)&result)[3] = ((char*)&from)[0];			 
	return  result;
#else
	return from;
#endif 	
}


WavFile::WavFile(I_File *file) {
	if (initChunkSize_) {
		const char *size=Config::GetInstance()->GetValue("SAMPLELOADCHUNKSIZE") ;
		if (size) {
			bufferChunkSize_=atoi(size) ;
		}
		initChunkSize_=false;
	}
	samples_=0 ;
	size_=0 ;
	readBuffer_=0 ;
	readBufferSize_=0 ;
	sampleBufferSize_=0 ;
	bytePerSample_=2 ;
	isFloat_=false ;
	filePos_=-1 ;
	loadedFrames_=0 ;
	streaming_=false ;
	xfBackup_=0 ;
	xfBackupFrame_=0 ;
	xfBackupFrames_=0 ;
	bakedLoopStart_=-1 ;
	bakedLoopEnd_=-1 ;
	file_=file ;
} ;

WavFile::~WavFile() {
	if (file_) {
		file_->Close() ;
		delete file_ ;
	}
	SAFE_FREE(samples_) ;
	SAFE_FREE(readBuffer_) ;
	SAFE_FREE(xfBackup_) ;
} ;

WavFile *WavFile::Open(const char *path) {

    // open file

	FileSystem *fs=FileSystem::GetInstance() ;
	I_File *file=fs->Open(path,"r") ;
	
	if (!file) return 0 ;

	WavFile *wav=new WavFile(file) ;

        
        // Get data
        
/*        file->Seek(0,SEEK_SET) ;
        file->Read(fileBuffer,filesize,1) ;
        uchar *ptr=fileBuffer ;*/
        
//Trace::Dump("Loading sample from %s",path) ;

	long position=0 ;

	// Read 'RIFF'

	unsigned int chunk ;

	position+=wav->readBlock(position,4) ;
	memcpy(&chunk,wav->readBuffer_,4) ;
	chunk = Swap32(chunk);
		
	if (chunk!=0x46464952) {
		Trace::Error("Bad RIFF format %x",chunk) ;
		delete(wav) ;
		return 0 ;
	}


	// Read size

	unsigned int size ;
	position+=wav->readBlock(position,4) ;
	memcpy(&size,wav->readBuffer_,4) ;
	size = Swap32(size);

	// Read WAVE

	position+=wav->readBlock(position,4) ;
	memcpy(&chunk,wav->readBuffer_,4) ;
	chunk = Swap32(chunk);

	if (chunk!=0x45564157) {
		Trace::Error("Bad WAV format") ;
		delete wav ;
		return 0 ;
	}

    // Find the 'fmt ' chunk, skipping ANY other chunk before it (JUNK, LIST,
    // bext, fact, ...). Many real-world WAVs put metadata before fmt -- the old
    // code only skipped JUNK and rejected everything else ("Bad WAV/fmt format").

    position += wav->readBlock(position, 4);
    memcpy(&chunk,wav->readBuffer_,4) ;
	chunk = Swap32(chunk);

    int chunkGuard = 0 ;
    while (chunk != 0x20746D66 && chunkGuard++ < 64) { // 'fmt '
        position += wav->readBlock(position, 4) ;
        memcpy(&size, wav->readBuffer_, 4) ;
        size = Swap32(size) ;
        position += size ;
        if (size & 1) position += 1 ; // RIFF chunks are word-aligned (pad byte)
        position += wav->readBlock(position, 4) ;
        memcpy(&chunk, wav->readBuffer_, 4) ;
        chunk = Swap32(chunk) ;
    }

    if (chunk!=0x20746D66) {
		Trace::Error("Bad WAV/fmt format") ;
		delete wav ;
		return 0 ;
	}

	// Read subchunk size

	position+=wav->readBlock(position,4) ;
	memcpy(&size,wav->readBuffer_,4) ;
	size = Swap32(size);

	if (size<16) {
		Trace::Error("Bad fmt size format") ;
		delete wav ;
		return 0 ;
	}
	int offset=size-16 ;

	// Read compression

	unsigned short comp ;
	position+=wav->readBlock(position,2) ;
	memcpy(&comp,wav->readBuffer_,2) ;
	comp = Swap16(comp);

	// (compression validated below -- after resolving WAVE_FORMAT_EXTENSIBLE)

	// Read NumChannels (mono/Stereo)

	unsigned short nChannels ;
	position+=wav->readBlock(position,2) ;
	memcpy(&nChannels,wav->readBuffer_,2) ;
	nChannels = Swap16(nChannels);

	// Read Sample rate 

	unsigned int sampleRate ;

	position+=wav->readBlock(position,4) ;
	memcpy(&sampleRate,wav->readBuffer_,4) ;
	sampleRate = Swap32(sampleRate);

	// Skip byteRate & blockalign

	position+=6 ;

	short bitPerSample ;
	position+=wav->readBlock(position,2) ;
	memcpy(&bitPerSample,wav->readBuffer_,2) ;
	bitPerSample = Swap16(bitPerSample);
		
	// Resolve the real format. For WAVE_FORMAT_EXTENSIBLE (0xFFFE) the actual
	// code lives in the first 2 bytes of the SubFormat GUID inside the fmt
	// extension (cbSize[2] validBits[2] channelMask[4] SubFormat[16]).
	unsigned short effectiveComp = comp ;
	if (comp == 0xFFFE && offset >= 10) {
		position += wav->readBlock(position, 2) ; // cbSize
		position += wav->readBlock(position, 2) ; // validBitsPerSample
		position += wav->readBlock(position, 4) ; // channelMask
		position += wav->readBlock(position, 2) ; // SubFormat[0..1] = format code
		memcpy(&effectiveComp, wav->readBuffer_, 2) ;
		effectiveComp = Swap16(effectiveComp) ;
		if (offset > 10) position += (offset - 10) ; // rest of the GUID
	} else if (offset) {
		position += offset ; // skip any extra fmt bytes
	}

	bool isFloat = (effectiveComp == 3) ; // 1 = PCM, 3 = IEEE float
	if (effectiveComp != 1 && effectiveComp != 3) {
		Trace::Error("Unsupported compression %d", (int)effectiveComp) ;
		delete wav ;
		return 0 ;
	}
	if ((bitPerSample!=8)&&(bitPerSample!=16)&&(bitPerSample!=24)&&(bitPerSample!=32)) {
		Trace::Error("Only 8/16/24/32 bit supported") ;
		delete wav ;
		return 0 ;
	}
	if (isFloat && bitPerSample!=32) {
		Trace::Error("Float WAV must be 32-bit") ;
		delete wav ;
		return 0 ;
	}
	wav->isFloat_=isFloat ;
	bitPerSample/=8 ;
	wav->bytePerSample_=bitPerSample ;

	// read data subchunk header
	//Trace::Dump("data subch") ;

	position+=wav->readBlock(position,4) ;
	memcpy(&chunk,wav->readBuffer_,4) ;
	chunk = Swap32(chunk);
	

	int dataGuard = 0 ;
	while (chunk!=0x61746164 && dataGuard++ < 64) {
		position+=wav->readBlock(position,4) ;
		memcpy(&size,wav->readBuffer_,4) ;
		size = Swap32(size);

		position+=size ;
		if (size & 1) position += 1 ; // word-align (pad byte on odd-size chunks)
		position+=wav->readBlock(position,4) ;
		memcpy(&chunk,wav->readBuffer_,4) ;
		chunk = Swap32(chunk);
	}

	if (chunk!=0x61746164) { // never found 'data' -> truncated/malformed file
		Trace::Error("Bad WAV: no data chunk") ;
		delete wav ;
		return 0 ;
	}

        wav->sampleRate_=sampleRate ;
       	wav->channelCount_=nChannels ;

	// Read data size in byte

	position+=wav->readBlock(position,4) ;
	memcpy(&size,wav->readBuffer_,4) ;
	size = Swap32(size);

	wav->size_=size/nChannels/bitPerSample ; // Size in samples (stereo/16bits)

	wav->dataPosition_=position ;

	return wav ;
} ; 

void *WavFile::GetSampleBuffer(int note) {
	return samples_ ;
} ;

int WavFile::GetSize(int note) {
	return size_ ;
} ;

int WavFile::GetChannelCount(int note) {
    return channelCount_ ;
} ;

int WavFile::GetSampleRate(int note) {
    return sampleRate_ ;
} ;

long WavFile::readBlock(long start,long size) {
	if (size>readBufferSize_) {
		SAFE_FREE(readBuffer_) ;
		readBuffer_=SYS_MALLOC(size) ;
		readBufferSize_=size ;
	}
  if (!readBuffer_)
  {
    Trace::Error("Failed to allocate read buffer of size %d",size);
  }
  else
  {
    // Only seek when we're not already at the right spot. Sample data is read in
    // sequential chunks, so skipping the redundant seek lets stdio read-ahead do
    // its job -- a real win on a slow SD card (the old code seeked before every
    // ~4KB read, defeating buffering).
    if (start!=filePos_) file_->Seek(start,SEEK_SET) ;
    file_->Read(readBuffer_,size,1) ;
    filePos_=start+size ;
  }
	return size ;
} ;


bool WavFile::GetBuffer(long start,long size) {

	// compute the sample buffer size we need,
	// allocate if needed

	int sampleBufferSize=2*channelCount_*size ;
	if (sampleBufferSize>sampleBufferSize_) {
		SAFE_FREE(samples_) ;
		samples_=(short *)SYS_MALLOC(sampleBufferSize) ;
		sampleBufferSize_=sampleBufferSize ;
	}

  if (!samples_)
  {
    // Out of memory (e.g. a very long sample on a 1GB device): fail cleanly so the
    // caller can reject the sample, instead of dereferencing a null buffer below
    // (crash) or playing garbage. sampleBufferSize_ was reset; clear it.
    Trace::Error("Failed to allocate %d samples",sampleBufferSize);
    sampleBufferSize_=0 ;
    return false ;
  }

	int rawStart=dataPosition_+start*channelCount_*bytePerSample_ ;

	if (bytePerSample_<=2 && !isFloat_) {
		// ---- 8 / 16-bit: original path (read raw into samples_, then expand) ----
		int bufferSize=size*channelCount_*bytePerSample_ ;
		int bufferStart=rawStart ;
		int count=bufferSize ;
		int offset=0 ;
		char *ptr=(char *)samples_ ;
		int readSize =
	   (bufferChunkSize_>0)
	   ? bufferChunkSize_
	   : count>4096?4096:count;

		while (count>0) {
			readSize=(count>readSize)?readSize:count ;
			readBlock(bufferStart,readSize) ;
			memcpy(ptr+offset,readBuffer_,readSize) ;
			bufferStart+=readSize ;
			count-=readSize ;
			offset+=readSize ;
			// (No per-chunk Sleep: it throttled loading 10-100x when
			// SAMPLELOADCHUNKSIZE was set, for no benefit on this target.)
		}

		// expand 8 bit data if needed
		unsigned char *src=(unsigned char *)samples_ ;
		short *dst=samples_ ;
		for (int i=size-1;i>=0;i--) {
			if (bytePerSample_==1) {
				dst[i]=(src[i]-128)*256 ;
			} else {
				*dst=Swap16(*dst) ;
				dst++ ;
				if (channelCount_>1) {
					*dst=Swap16(*dst) ;
					dst++ ;
				}
			}
		}
		return true ;
	}

	// ---- 24 / 32-bit int or 32-bit float -> down-convert to signed 16-bit ----
	// (little-endian; all LGPT targets are LE). Read the raw data in SMALL chunks
	// (like the 8/16-bit path), converting each chunk straight into samples_. This
	// avoids ever allocating one huge readBuffer_ / doing one giant read for the
	// whole file -- which fails on the device's pool allocator for long samples
	// (the file then played only its start, then silence). readBuffer_ stays ~4KB.
	long total=(long)size*channelCount_ ;     // 16-bit samples to produce
	int bps=bytePerSample_ ;                   // 3 (24-bit) or 4 (32-bit/float)
	long chunkSamples=4096/bps ;               // whole samples per ~4KB raw chunk
	if (chunkSamples<1) chunkSamples=1 ;
	short *out=samples_ ;
	long done=0 ;
	long rawPos=rawStart ;
	while (done<total) {
		long n=total-done ;
		if (n>chunkSamples) n=chunkSamples ;
		long rawBytes=n*bps ;
		readBlock(rawPos,rawBytes) ;          // into readBuffer_ (small, reused)
		unsigned char *raw=(unsigned char *)readBuffer_ ;
		if (!raw) break ;                     // alloc failed: leave the rest silent
		if (isFloat_) {
			for (long i=0;i<n;i++) {
				float f ; memcpy(&f,raw+i*4,4) ;
				int v=(int)(f*32767.0f) ;
				if (v>32767) v=32767 ; else if (v<-32768) v=-32768 ;
				out[done+i]=(short)v ;
			}
		} else if (bps==3) {
			for (long i=0;i<n;i++) {
				out[done+i]=(short)((raw[i*3+2]<<8)|raw[i*3+1]) ;
			}
		} else { // 32-bit signed int
			for (long i=0;i<n;i++) {
				out[done+i]=(short)((raw[i*4+3]<<8)|raw[i*4+2]) ;
			}
		}
		done+=n ;
		rawPos+=rawBytes ;
	}
	return true ;
} ;

// ---- background "stream-in" loading -----------------------------------------
// Allocate the full 16-bit sample buffer up front (so playback can index into it
// as it fills), reset the watermark to 0. Header is already parsed by Open().
bool WavFile::PrepareStreamLoad() {
	long frames=size_ ;
	if (frames<=0) return false ;
	int needed=2*channelCount_*(int)frames ; // 16-bit output bytes
	if (needed>sampleBufferSize_) {
		SAFE_FREE(samples_) ;
		samples_=(short *)SYS_MALLOC(needed) ;
		sampleBufferSize_=needed ;
	}
	if (!samples_) { sampleBufferSize_=0 ; return false ; }
	SetLoadedFrames(0) ;
	streaming_=true ; // playback gate (IsReady) is active until the fill completes
	return true ;
}

// Ready unless a background stream-in is still in progress. Synchronous loads
// (streaming_ stays false) are ALWAYS ready -> existing behaviour unchanged.
bool WavFile::IsReady() {
	if (!streaming_) return true ;
	return GetLoadedFrames()>=(long)size_ ;
}

// Loop de-click via crossfade, baked into the sample buffer (the hot render loop
// stays untouched -- it just plays a buffer whose loop seam is already smooth).
// Blend the X frames ending at loopEnd with the X frames ending at loopStart, so
// that as playback nears loopEnd it morphs toward the audio just BEFORE loopStart;
// the wrap loopEnd->loopStart is then continuous (orig[loopStart-1] -> orig[loopStart]).
// Idempotent per loop config; a previous bake is restored before a new one. Called
// at note trigger on the sequencer thread (under the mixer lock, before the block's
// render), so there is no concurrent read of samples_.
#define WAV_XFADE_MAX 512 // frames (~12 ms @ 44.1k)
void WavFile::ApplyLoopCrossfade(int loopStart,int loopEnd) {
	if (!samples_) return ;
	if (loopStart==bakedLoopStart_ && loopEnd==bakedLoopEnd_) return ; // already baked
	int ch=channelCount_ ;
	// restore any previous (different) bake so we blend from original data
	if (xfBackup_) {
		for (int i=0;i<xfBackupFrames_*ch;i++) samples_[xfBackupFrame_*ch+i]=xfBackup_[i] ;
		SAFE_FREE(xfBackup_) ;
		xfBackupFrames_=0 ; bakedLoopStart_=bakedLoopEnd_=-1 ;
	}
	if (loopStart<0 || loopEnd>size_ || loopEnd<=loopStart) return ;
	int loopLen=loopEnd-loopStart ;
	int X=WAV_XFADE_MAX ;
	if (X>loopStart) X=loopStart ;   // need X frames of audio before loopStart
	if (X>loopLen/2)  X=loopLen/2 ;  // and at most half the loop
	if (X<8) return ;                // too short to be worth it / smooth
	int base=loopEnd-X ;
	xfBackup_=(short *)SYS_MALLOC(X*ch*sizeof(short)) ;
	if (!xfBackup_) return ;
	for (int i=0;i<X*ch;i++) xfBackup_[i]=samples_[base*ch+i] ; // save original tail
	xfBackupFrame_=base ; xfBackupFrames_=X ;
	for (int i=0;i<X;i++) {
		float w=0.5f-0.5f*cosf((float)M_PI*(float)i/(float)(X-1)) ; // 0 -> 1 raised cosine
		for (int c=0;c<ch;c++) {
			int tail=xfBackup_[i*ch+c] ;                  // original [loopEnd-X+i]
			int pre =samples_[(loopStart-X+i)*ch+c] ;     // original [loopStart-X+i] (untouched)
			int v=(int)lrintf(tail*(1.0f-w)+pre*w) ;
			if (v>32767) v=32767 ; else if (v<-32768) v=-32768 ;
			samples_[base*ch+i*ch+c]=(short)v ;
		}
	}
	bakedLoopStart_=loopStart ; bakedLoopEnd_=loopEnd ;
}

// Decode frames [frameStart, frameStart+frameCount) from the file into samples_
// at the matching offset, down-converting to 16-bit. Per-chunk for ALL formats
// (the synchronous GetBuffer's 8/16-bit path expands in place at the end, which
// can't be watermarked mid-fill -- this path converts each chunk straight into
// place so the loader can publish progress after every chunk). Does NOT touch the
// watermark; the caller advances it via SetLoadedFrames() once a chunk is in.
bool WavFile::DecodeRange(long frameStart,long frameCount) {
	if (!samples_ || !file_ || frameCount<=0) return false ;
	int bps=bytePerSample_ ;
	long outBase=frameStart*channelCount_ ;          // 16-bit sample index
	long totalSamples=frameCount*channelCount_ ;     // 16-bit samples to produce
	long rawPos=(long)dataPosition_+frameStart*(long)channelCount_*bps ;
	long maxPerChunk=65536/bps ; if (maxPerChunk<1) maxPerChunk=1 ;
	long done=0 ;
	while (done<totalSamples) {
		long n=totalSamples-done ; if (n>maxPerChunk) n=maxPerChunk ;
		long rawBytes=n*bps ;
		readBlock(rawPos,rawBytes) ;
		unsigned char *raw=(unsigned char *)readBuffer_ ;
		if (!raw) return false ;
		short *out=samples_+outBase+done ;
		if (bps==2 && !isFloat_) {            // 16-bit LE: raw IS the sample data
			short *s16=(short *)raw ;
			for (long i=0;i<n;i++) out[i]=Swap16(s16[i]) ;
		} else if (bps==1 && !isFloat_) {     // 8-bit unsigned -> signed 16-bit
			for (long i=0;i<n;i++) out[i]=(short)((raw[i]-128)*256) ;
		} else if (isFloat_) {                // 32-bit float -> 16-bit
			for (long i=0;i<n;i++) {
				float f ; memcpy(&f,raw+i*4,4) ;
				int v=(int)(f*32767.0f) ;
				if (v>32767) v=32767 ; else if (v<-32768) v=-32768 ;
				out[i]=(short)v ;
			}
		} else if (bps==3) {                  // 24-bit int -> top 16 bits
			for (long i=0;i<n;i++) out[i]=(short)((raw[i*3+2]<<8)|raw[i*3+1]) ;
		} else {                              // 32-bit int -> top 16 bits
			for (long i=0;i<n;i++) out[i]=(short)((raw[i*4+3]<<8)|raw[i*4+2]) ;
		}
		done+=n ; rawPos+=rawBytes ;
	}
	return true ;
}

// Watermark accessors with a full fence each, so the producer's sample writes are
// visible to the consumer once it observes the advanced watermark (A53 = weak
// memory; same release/acquire discipline as the stem-writer SPSC ring).
long WavFile::GetLoadedFrames() { long n=loadedFrames_ ; __sync_synchronize() ; return n ; }
void WavFile::SetLoadedFrames(long n) { __sync_synchronize() ; loadedFrames_=n ; }

void WavFile::Close() {
	file_->Close() ;
	SAFE_DELETE(file_) ;
	SAFE_FREE(readBuffer_) ;
	readBufferSize_=0 ;
} ;

int WavFile::GetRootNote(int note) {
	return 60 ;
} 
