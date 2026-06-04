
#include "AudioDriver.h"
#include "System/System/System.h"
#include "System/Console/Trace.h"
#include "System/Console/n_assert.h"

AudioDriver::AudioDriver(AudioSettings &settings) {
	settings_=settings ;
}

AudioDriver::~AudioDriver() {
}

bool AudioDriver::Init() {

  // Clear all buffers
	
   for (int i=0;i<SOUND_BUFFER_COUNT;i++) {
     pool_[i].buffer_=0 ;
     pool_[i].size_=0 ;
   } ;
   isPlaying_=false;	 

   return InitDriver() ;
}

void AudioDriver::Close() {
	CloseDriver() ;
};

bool AudioDriver::Start() {

    isPlaying_=true ; 

    for (int i=0;i<SOUND_BUFFER_COUNT;i++) {
  	  SAFE_FREE(pool_[i].buffer_) ;
    } ;
	 
    poolQueuePosition_=0 ;
    poolPlayPosition_=0 ;
	hasData_=false ;

    return StartDriver() ;
};

void AudioDriver::Stop() {
     isPlaying_=false ;
	hasData_=false ;
     StopDriver() ;
}

void AudioDriver::AddBuffer(short *buffer,int samplecount) {
  
  int len=samplecount*2*sizeof(short) ;

  if (!isPlaying_) return ;

  if (len>SOUND_BUFFER_MAX) {
      Trace::Error("Alert: buffer size exceeded") ;
  }

  if (pool_[poolQueuePosition_].buffer_!=0) {
  NInvalid ;
  Trace::Error("Audio overrun, please report") ;
  SAFE_FREE(pool_[poolQueuePosition_].buffer_) ;
  return ;
  }	

  pool_[poolQueuePosition_].buffer_=(char*) ((short *)SYS_MALLOC(len)) ;

  SYS_MEMCPY(pool_[poolQueuePosition_].buffer_,(char *)buffer,len) ;
  pool_[poolQueuePosition_].size_=len ;
  poolQueuePosition_=(poolQueuePosition_+1)%SOUND_BUFFER_COUNT ;
	hasData_=true ;
}

// True while the pool holds fewer than the target prebuffer depth. The producer
// thread polls this to fill the pool up to the target after any dip and then
// stop -- a classic bounded producer/consumer with the target as the high
// watermark. This both BOUNDS latency (never more than the target buffered, so
// no runaway growth toward SOUND_BUFFER_COUNT ~= 2s) and REBUILDS the cushion
// after a dip (so the pool can't get stuck near empty -> constant underruns).
bool AudioDriver::needsBuffering() {
  int fill = (poolQueuePosition_ - poolPlayPosition_ + SOUND_BUFFER_COUNT) % SOUND_BUFFER_COUNT ;
  int target = settings_.preBufferCount_ ;
  if (target < 2) target = 2 ;
  if (target > SOUND_BUFFER_COUNT - 1) target = SOUND_BUFFER_COUNT - 1 ;
  return fill < target ;
}

void AudioDriver::OnNewBufferNeeded() {
  // Produce exactly one buffer. The producer thread loops on needsBuffering()
  // to decide HOW MANY to produce (fill to target), so this stays single-purpose.
  SetChanged() ;
  Event event(Event::ADET_BUFFERNEEDED);
  NotifyObservers(&event) ;
} ;

void AudioDriver::onAudioBufferTick()
{
  SetChanged() ;
  Event event(Event::ADET_DRIVERTICK);
  NotifyObservers(&event) ;
}

bool AudioDriver::hasData() {
	return hasData_ ;
}  ;

AudioSettings AudioDriver::GetAudioSettings() {
	return settings_ ;
} ;
