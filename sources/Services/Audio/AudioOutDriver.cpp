
#include "AudioOutDriver.h"
#include "Application/Model/Project.h"
#include "Application/Player/SyncMaster.h" // Should be installable
#include "AudioDriver.h"
#include "Delay.h"
#include "Services/Time/TimeService.h"
#include "System/Console/Trace.h"
#include "System/System/System.h"
#include <math.h>

AudioOutDriver::AudioOutDriver(AudioDriver &driver) {
    driver_ = &driver;
    driver.AddObserver(*this) ;
    primarySoundBuffer_=0 ;
    mixBuffer_ = 0;
    SetOwnership(false);
}

AudioOutDriver::~AudioOutDriver() {
    driver_->RemoveObserver(*this);
    delete driver_ ;
};

bool AudioOutDriver::Init() {
	primarySoundBuffer_=(fixed *)SYS_MALLOC(MIX_BUFFER_SIZE*sizeof(fixed)/2) ;
	mixBuffer_=(short *)SYS_MALLOC(MIX_BUFFER_SIZE) ;
	Delay::GetInstance()->Init() ;
    return driver_->Init();
} ;

void AudioOutDriver::Close() {
    driver_->Close();
    SAFE_FREE(primarySoundBuffer_) ;
	SAFE_FREE(mixBuffer_) ;     
}

bool AudioOutDriver::Start() {
    sampleCount_=0 ;
	return driver_->Start() ;
}

void AudioOutDriver::Stop() {
	driver_->Stop() ;
}

void AudioOutDriver::Trigger() {

#if defined(__aarch64__)
	// Flush denormals to zero on the audio thread. Decaying float state in the
	// EQ / LFO / delay feedback paths drifts into the denormal range, which is
	// extremely slow on Cortex-A53 and shows up as a CPU creep that worsens over
	// a session. Setting FPCR.FZ (bit 24) makes denormals flush to 0.
	{
		unsigned long fpcr ;
		__asm__ __volatile__("mrs %0, fpcr" : "=r"(fpcr)) ;
		if (!(fpcr & (1UL<<24))) {
			fpcr |= (1UL<<24) ;
			__asm__ __volatile__("msr fpcr, %0" :: "r"(fpcr)) ;
		}
	}
#endif

	TimeService *ts=TimeService::GetInstance() ;

    prepareMixBuffers();
    // Clear the delay send accumulator before the voices render into it.
    Delay::GetInstance()->clearSend(sampleCount_) ;
    hasSound_=AudioMixer::Render(primarySoundBuffer_,sampleCount_) ;
    // Feed silence to the master when the mix is empty so the delay tail rings out.
    if (!hasSound_) {
        SYS_MEMSET(primarySoundBuffer_,0,sampleCount_*2*sizeof(fixed)) ;
    }
    // Add the per-instrument delay sends (wet) onto the master.
    if (Delay::GetInstance()->processSend(primarySoundBuffer_,sampleCount_)) {
        hasSound_=true ;
    }
    clipToMix();
    driver_->AddBuffer(mixBuffer_,sampleCount_) ;
}

void AudioOutDriver::Update(Observable &o,I_ObservableData *d) 
{
    SetChanged();
    NotifyObservers(d) ;
}

void AudioOutDriver::prepareMixBuffers() {
    sampleCount_ = getPlaySampleCount();
    // Safety net on the audio thread: the fixed mix buffers hold MIX_BUFFER_SIZE/4
    // stereo frames. sampleCount_ is derived from the tempo, so a tiny/zero tempo
    // (corrupt project, pathological tap) would otherwise make us render past the
    // end of primarySoundBuffer_/mixBuffer_ -> heap corruption + crash. Clamp it.
    const int maxFrames = MIX_BUFFER_SIZE / 4 ;
    if (sampleCount_ > maxFrames) sampleCount_ = maxFrames ;
    if (sampleCount_ < 0) sampleCount_ = 0 ;
} ;

void AudioOutDriver::SetMasterVolume(int volume) {
    AudioMixer::SetMasterVolume(volume);
}

void AudioOutDriver::clipToMix() {

    bool interlaced = driver_->Interlaced();

    if (!hasSound_) {
        SYS_MEMSET(mixBuffer_, 0, sampleCount_ * 2 * sizeof(short));
    } else {
        short *s1 = mixBuffer_;
		short *s2 = (interlaced) ? s1 + 1 : s1 + sampleCount_;
		int offset = (interlaced) ? 2 : 1;

        fixed *p = primarySoundBuffer_;

        for (int i = 0; i < sampleCount_; i++) {

            fixed leftSample = *p++;
            fixed rightSample = *p++;

            *s1 = short(fp2i(leftSample));
            s1 += offset;
			*s2 = short(fp2i(rightSample));
			s2 += offset;
        };
    }
} ;

int AudioOutDriver::GetPlayedBufferPercentage() {
	return driver_->GetPlayedBufferPercentage() ;
} ;

AudioDriver *AudioOutDriver::GetDriver() { return driver_; };

std::string AudioOutDriver::GetAudioAPI() {
	AudioSettings as=driver_->GetAudioSettings() ;
	return as.audioAPI_ ;
} ;

std::string AudioOutDriver::GetAudioDevice() {
	AudioSettings as=driver_->GetAudioSettings() ;
	return as.audioDevice_ ;
} ;

int AudioOutDriver::GetAudioBufferSize() {
	AudioSettings as=driver_->GetAudioSettings() ;
	return as.bufferSize_ ;
} ;

int AudioOutDriver::GetAudioRequestedBufferSize() {
	AudioSettings as=driver_->GetAudioSettings() ;
	return as.bufferSize_ ;
}

int AudioOutDriver::GetAudioPreBufferCount() {
	AudioSettings as=driver_->GetAudioSettings() ;
	return as.preBufferCount_ ;
} ;
double AudioOutDriver::GetStreamTime() { return driver_->GetStreamTime(); };
