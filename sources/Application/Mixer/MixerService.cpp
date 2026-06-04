#include "MixerService.h"
#include "Application/Audio/DummyAudioOut.h"
#include "Application/Model/Config.h"
#include "Application/Model/Mixer.h"
#include "Application/Model/Project.h"
#include "Services/Audio/Audio.h"
#include "Services/Audio/AudioDriver.h"
#include "Services/Midi/MidiService.h"
#include "System/Console/Trace.h"
#include <time.h>
#include <string.h>

MixerService::MixerService() : out_(0), ownsOut_(false), sync_(0), isRendering_(false) {
    mode_ = MSRM_PLAYBACK;
};

MixerService::~MixerService(){};

/*
 * initializes the mixer service, config changes depending if we're in sequencer or render mode
 */
bool MixerService::Init() {
    // create the output depending on rendering mode
    out_ = 0;
    ownsOut_ = false;
	switch (mode_) {
    case MSRM_STEREO:
    case MSRM_STEMS:
        out_ = new DummyAudioOut();
        ownsOut_ = true; // we own this one and must delete it in Close()
        break;
    default:
        Audio *audio = Audio::GetInstance();
        out_ = audio->GetFirst();
        break;
	}

	for (int i=0;i<MAX_BUS_COUNT;i++) {
		master_.Insert(bus_[i]);
	}

	// Render the master's channel buses across CPU cores (the Brick has 4).
	// Default on; set AUDIOMULTICORE=NO in config.xml to force single-core.
	const char *mc = Config::GetInstance()->GetValue("AUDIOMULTICORE") ;
	bool useMulticore = (!mc) || (strcmp(mc,"NO")!=0 && strcmp(mc,"no")!=0) ;
	master_.SetParallel(useMulticore) ;
	Trace::Log("MixerService", useMulticore ? "multicore render ON" : "multicore render OFF") ;

	bool result = false;
	if (out_) {
		result = out_->Init();
		if (result) {
			out_->Insert(master_);
		}

        initRendering(mode_);
        out_->AddObserver(*MidiService::GetInstance());
	}

	sync_=SDL_CreateMutex();
	NAssert(sync_);

	if (result) {
		Trace::Log("MixerService", "output initialized");
	} else {
		Trace::Log("MixerService", "failed to initialize output");
	}
	return (result);
};

void MixerService::initRendering(MixerServiceRenderMode mode) {
    // Build a timestamp so each bounce gets a unique filename instead of
    // overwriting the previous render. Falls back gracefully if the clock
    // is unavailable.
    char ts[32];
    time_t now = time(0);
    struct tm *lt = localtime(&now);
    if (lt) {
        strftime(ts, sizeof(ts), "%y%m%d-%H%M%S", lt);
    } else {
        strcpy(ts, "render");
    }

    char buffer[1024];
    switch(mode) {
    case MSRM_PLAYBACK:
        break;
    case MSRM_STEREO:
        sprintf(buffer, "project:mixdown_%s.wav", ts);
        out_->SetFileRenderer(buffer);
        break;
    case MSRM_STEMS:
        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            sprintf(buffer, "project:channel%d_%s.wav", i, ts);
            bus_[i].SetFileRenderer(buffer);
        }
        break;
    }
}

void MixerService::Close() {
	if (out_) {
    out_->RemoveObserver(*MidiService::GetInstance());
		out_->Close() ;
		out_->Empty() ;
		master_.Empty() ;

		switch(mode_) {
        case MSRM_STEMS:
        case MSRM_STEREO:
            break;
        default:
            break;
        }
    }
   for (int i=0;i<MAX_BUS_COUNT;i++) {
	   bus_[i].Empty() ;
   }
	// Delete the output only if we created it (DummyAudioOut for stereo/stem
	// render); the playback out_ is owned by Audio and must not be deleted.
	if (ownsOut_) {
		delete out_ ;
		ownsOut_=false ;
	}
	out_=0 ;
	SDL_DestroyMutex(sync_) ;
	sync_=0 ;
} ;

void MixerService::SetRenderMode(int mode) {
    mode_ = MixerServiceRenderMode(mode);
}

bool MixerService::IsRendering() { return isRendering_; }

bool MixerService::Start() {
    MidiService::GetInstance()->Start();
    if (out_) {
        out_->AddObserver(*this);
        out_->Start();
     }
	return true ;
} ;

void MixerService::Stop() {
	MidiService::GetInstance()->Stop() ;
     if (out_) {
      out_->Stop() ;
      out_->RemoveObserver(*this) ;
     }
}

MixBus *MixerService::GetMixBus(int i) {
	return &(bus_[i]) ;
} ;

void MixerService::Update(Observable &o,I_ObservableData *d)  {

  AudioDriver::Event *event=(AudioDriver::Event *)d;
  if (event->type_ == AudioDriver::Event::ADET_BUFFERNEEDED)
  {  
    Lock() ;
    SetChanged() ;
    NotifyObservers() ;

    out_->Trigger();
    Unlock();
  }
}

bool MixerService::Clipped() {
     return out_->Clipped() ;
} ;

void MixerService::SetPregain(int vol) {
    Mixer *mixer = Mixer::GetInstance();

    fixed masterVolume = fp_mul(i2fp(vol), fl2fp(0.01f));

    for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
        bus_[i].SetVolume(masterVolume);
  }
};

void MixerService::SetSoftclip(int clip, int gain) {
    out_->SetSoftclip(clip, gain);
}

void MixerService::SetMasterVolume(int attn) { out_->SetMasterVolume(attn); }

int MixerService::GetPlayedBufferPercentage() {
	return out_->GetPlayedBufferPercentage() ;
}

void MixerService::toggleRendering(bool enable) {
    isRendering_ = enable;
    switch (mode_) {
    case MSRM_PLAYBACK:
        break;
    case MSRM_STEREO:
        // Generate a fresh timestamped filename only when starting a render.
        if (enable) initRendering(MSRM_STEREO);
        out_->EnableRendering(enable);
        break;
    case MSRM_STEMS:
        if (enable) initRendering(MSRM_STEMS);
        for (int i = 0; i < SONG_CHANNEL_COUNT; i++) {
            bus_[i].EnableRendering(enable);
        };
        break;
    }
}

void MixerService::OnPlayerStart() {
	toggleRendering(true) ;
} ;

void MixerService::OnPlayerStop() {
	toggleRendering(false) ;
} ;

void MixerService::Execute(FourCC id,float value) {
     if (value>0.5) {
        Audio *audio=Audio::GetInstance() ;
        int volume=audio->GetMixerVolume() ;
        switch(id) {
           case TRIG_VOLUME_INCREASE:
                if (volume<100) volume+=1 ;
                break ;
           case TRIG_VOLUME_DECREASE:
                if (volume>0) volume-=1 ;
                break ;                       
        } ;
        audio->SetMixerVolume(volume) ;
     } ;
}

AudioOut *MixerService::GetAudioOut() {
	return out_ ;
} ;


void MixerService::Lock() {
	if (sync_) SDL_LockMutex(sync_) ;
}

void MixerService::Unlock() {
	if (sync_) SDL_UnlockMutex(sync_) ;
}
