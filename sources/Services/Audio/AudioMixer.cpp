#include "AudioMixer.h"
#include "System/System/System.h"
#include "System/Process/Process.h"
#include <math.h>
#include <string.h>

#define MAX_POSITIVE_FIXED i2fp(32767)
#define MAX_NEGATIVE_FIXED i2fp(-32768)

// ---- multicore child-render helper thread -----------------------------------
// One worker that renders an assigned slice of the children (the rest are
// rendered on the calling/audio thread), so the master mix uses 2 cores. The
// children are independent (per-channel state, per-channel delay send), so no
// locking is needed -- just a go/done handshake around each block.
class MixRenderWorker : public SysThread {
public:
	MixRenderWorker():children_(0),scratch_(0),gotData_(0),first_(0),last_(0),n_(0) {
		go_=SysSemaphore::Create(0,1) ;
		done_=SysSemaphore::Create(0,1) ;
	}
	virtual bool Execute() {
		while (!shouldTerminate()) {
			go_->Wait() ;
			if (shouldTerminate()) break ;
			for (int i=first_;i<last_;i++) {
				gotData_[i]=children_[i]->Render(scratch_[i],n_) ;
			}
			done_->Post() ;
		}
		return false ;
	}
	AudioModule **children_ ;
	fixed **scratch_ ;
	bool *gotData_ ;
	int first_,last_,n_ ;
	SysSemaphore *go_,*done_ ;
} ;

AudioMixer::AudioMixer(const char *name):
	T_SimpleList<AudioModule>(false),
	enableRendering_(0),
	writer_(0),
	name_(name)
{
	volume_=(i2fp(1)) ;
    softclip_ = -1;
    softclipGain_ = 0 ;
	masterVolume_ = 100 ;
	clipped_ = false ;
	mixBuffer_ = 0 ;
	mixBufferSamples_ = 0 ;
	parallel_ = false ;
	worker_ = 0 ;
	childList_ = 0 ;
	childScratch_ = 0 ;
	childGotData_ = 0 ;
	childCapacity_ = 0 ;
	childScratchSamples_ = 0 ;

	// Precalculate constant values for softclipping algorithm
	softClipData_[0].alpha = 1.45f; // -1.5db (approx.)
	softClipData_[1].alpha = 1.07f; // -3db (approx.)
	softClipData_[2].alpha = 0.75f; // -6db (approx.)
	softClipData_[3].alpha = 0.53f; // -9db (approx.)

	for (int i = 0; i < 4; i++) {
		softClipData_[i].alpha23 = softClipData_[i].alpha * (2.0f / 3.0f);
		softClipData_[i].alphaInv = 1.0f / softClipData_[i].alpha;

		if (softClipData_[i].alpha > 1.0f) {
			/* calculates gain compensation differently for
			 * modes with alpha > 1, so there's no drop in loudness
			 * and we can still drive the hard clipper when the input
			 * goes over 1.0
			 */
			softClipData_[i].gainCmp = 1.0f / (1.0f - (pow(softClipData_[i].alphaInv, 2.0f) / 3.0f));
		} else {
			softClipData_[i].gainCmp = 1.0f / softClipData_[i].alpha23;
		}
	}
} ;

AudioMixer::~AudioMixer() {
	SAFE_FREE(mixBuffer_) ;
	SetParallel(false) ; // tears down the worker thread + scratch buffers
}

// Enable/disable multicore child rendering. On enable, spin up the helper thread
// and allocate the child snapshot arrays; on disable, stop the thread and free.
void AudioMixer::SetParallel(bool enable) {
	if (enable==parallel_) return ;
	if (enable) {
		childCapacity_=32 ; // plenty for the master's ~17 buses
		childList_=(AudioModule **)malloc(childCapacity_*sizeof(AudioModule *)) ;
		childScratch_=(fixed **)malloc(childCapacity_*sizeof(fixed *)) ;
		childGotData_=(bool *)malloc(childCapacity_*sizeof(bool)) ;
		for (int i=0;i<childCapacity_;i++) childScratch_[i]=0 ;
		worker_=new MixRenderWorker() ;
		worker_->Start() ;
		parallel_=true ;
	} else {
		parallel_=false ;
		if (worker_) {
			worker_->RequestTermination() ;
			worker_->go_->Post() ; // wake it so it can see the termination flag
			while (!worker_->IsFinished()) { /* brief */ }
			delete worker_ ;
			worker_=0 ;
		}
		if (childScratch_) {
			for (int i=0;i<childCapacity_;i++) SAFE_FREE(childScratch_[i]) ;
			free(childScratch_) ; childScratch_=0 ;
		}
		if (childList_) { free(childList_) ; childList_=0 ; }
		if (childGotData_) { free(childGotData_) ; childGotData_=0 ; }
		childCapacity_=0 ;
		childScratchSamples_=0 ;
	}
}

void AudioMixer::ensureChildScratch(int count,int samplecount) {
	if (samplecount>childScratchSamples_) {
		for (int i=0;i<childCapacity_;i++) {
			SAFE_FREE(childScratch_[i]) ;
			childScratch_[i]=(fixed *)malloc(samplecount*2*sizeof(fixed)) ;
		}
		childScratchSamples_=samplecount ;
	}
}

void AudioMixer::SetFileRenderer(const char *path) {
	renderPath_=path ;
} ;

void AudioMixer::EnableRendering(bool enable) {

	if (enable==enableRendering_) {
		return ;
	}

	if (enable) {
		writer_=new WavFileWriter(renderPath_.c_str()) ;
	} 

	enableRendering_=enable ;
	if (!enable) {
		writer_->Close() ;
		SAFE_DELETE(writer_) ;
	}
} ;

bool AudioMixer::Render(fixed *buffer,int samplecount) {
    clipped_ = false;
    bool gotData = parallel_ ? sumChildrenParallel(buffer,samplecount)
                             : sumChildrenSequential(buffer,samplecount) ;
    if (gotData) {
        applyVolumeAndClip(buffer,samplecount) ;
    }
    if (enableRendering_&&writer_) {
        if (!gotData) {
            memset(buffer,0,samplecount*2*sizeof(fixed)) ;
        }
        writer_->AddBuffer(buffer,samplecount) ;
    }
    return gotData ;
} ;

// Render all children one after another on the calling thread (the original
// behaviour; used by the per-channel buses, and by the master when multicore
// is off).
bool AudioMixer::sumChildrenSequential(fixed *buffer,int samplecount) {
    bool gotData = false;
    IteratorPtr<AudioModule> it(GetIterator());
    for (it->Begin(); !it->IsDone(); it->Next()) {
        AudioModule &current = it->CurrentItem();
        if (!gotData) {
            gotData=current.Render(buffer,samplecount) ;
        } else {
            if (mixBufferSamples_ < samplecount) {
               SAFE_FREE(mixBuffer_) ;
               mixBuffer_=(fixed *)malloc(samplecount*2*sizeof(fixed)) ;
               mixBufferSamples_=samplecount ;
            }
            if (current.Render(mixBuffer_,samplecount)) {
               fixed *dst=buffer ;
               fixed *src=mixBuffer_ ;
               int count=samplecount*2 ;
               while (count--) {
                 *dst+=*src ;
                 dst++ ;
                 src++ ;
               }
            }
        }
    }
    return gotData ;
}

// Render the children across two cores: the helper thread takes the second half,
// this (audio) thread takes the first half, then we sum. Each child writes into
// its own scratch buffer, so there is no shared write -> no locking needed.
bool AudioMixer::sumChildrenParallel(fixed *buffer,int samplecount) {
    int nc=0 ;
    IteratorPtr<AudioModule> it(GetIterator());
    for (it->Begin(); !it->IsDone(); it->Next()) {
        if (nc>=childCapacity_) break ;
        childList_[nc++]=&it->CurrentItem() ;
    }
    if (nc==0) return false ;
    ensureChildScratch(nc,samplecount) ;

    int half=nc/2 ;
    worker_->children_=childList_ ;
    worker_->scratch_=childScratch_ ;
    worker_->gotData_=childGotData_ ;
    worker_->first_=half ;
    worker_->last_=nc ;
    worker_->n_=samplecount ;
    worker_->go_->Post() ;                 // kick the helper

    for (int i=0;i<half;i++) {             // render our share in parallel
        childGotData_[i]=childList_[i]->Render(childScratch_[i],samplecount) ;
    }

    worker_->done_->Wait() ;               // barrier: wait for the helper

    int total=samplecount*2 ;
    memset(buffer,0,total*sizeof(fixed)) ;
    bool gotData=false ;
    for (int i=0;i<nc;i++) {
        if (childGotData_[i]) {
            fixed *src=childScratch_[i] ;
            for (int k=0;k<total;k++) buffer[k]+=src[k] ;
            gotData=true ;
        }
    }
    return gotData ;
}

void AudioMixer::applyVolumeAndClip(fixed *buffer,int samplecount) {
    fixed *c = buffer;
    float damp = pow((float)masterVolume_ / 100, 4.0f);
    if (volume_ != i2fp(1)) {
        for (int i = 0; i < samplecount * 2; i++) {
            fixed v = fp_mul(*c, volume_);
            *c++ = v;
        }
    }
    c = buffer;
    for (int i = 0; i < samplecount * 2; i++) {
        fixed sample = *c;
        sample = fl2fp(damp * fp2fl(hardClip(softClip(sample))));
        *c++ = sample;
    }
}

void AudioMixer::SetVolume(fixed volume) { volume_ = volume; }

void AudioMixer::SetSoftclip(int clip, int gain) {
    softclip_ = clip - 1;
	softclipGain_ = gain;
}

void AudioMixer::SetMasterVolume(int volume) {
	masterVolume_ = volume;
}

bool AudioMixer::Clipped() { return clipped_; }

fixed AudioMixer::hardClip(fixed sample) {
    if (sample > MAX_POSITIVE_FIXED || sample < MAX_NEGATIVE_FIXED) {
        clipped_ = true;
		return sample > 0 ? MAX_POSITIVE_FIXED : MAX_NEGATIVE_FIXED;
    }
    return sample;
}

/* Implements standard cubic algorithm
 * https://wiki.analog.com/resources/tools-software/sigmastudio/toolbox/nonlinearprocessors/standardcubic
 */
fixed AudioMixer::softClip(fixed sample) {
    if (softclip_ == -1 || sample == 0)
        return sample;

    float x;
    float sampleFloat = fp2fl(sample);
	float maxFloat = fp2fl(sampleFloat > 0 ? MAX_POSITIVE_FIXED : MAX_NEGATIVE_FIXED);
	SoftClipData* data = &softClipData_[softclip_];

    x = data->alphaInv * (sampleFloat / maxFloat);
    if (x > -1.0f && x < 1.0f) {
        sampleFloat = maxFloat * (data->alpha * (x - (pow(x, 3.0f) / 3.0f)));
    } else {
        sampleFloat = maxFloat * data->alpha23;
    }

    if (softclipGain_) {
        sampleFloat = sampleFloat * data->gainCmp;
    }

    return fl2fp(sampleFloat);
}
