#ifndef _MIXER_SERVICE_H_
#define _MIXER_SERVICE_H_

#ifdef SDL2
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#include "Application/Commands/CommandDispatcher.h" // Would be better done externally and call an API here
#include "Foundation/Observable.h"
#include "Foundation/T_Singleton.h"
#include "Services/Audio/AudioMixer.h"
#include "Services/Audio/AudioOut.h"
#include "MixBus.h"
#include "Application/Model/Song.h" // SONG_CHANNEL_COUNT

enum MixerServiceRenderMode {
    MSRM_PLAYBACK,
    MSRM_STEREO,
    MSRM_STEMS,
};

// One bus per track, plus one extra dedicated bus for the file streamer /
// import preview (STREAM_MIX_BUS). Tying these to SONG_CHANNEL_COUNT keeps the
// streamer bus out of the track range after the 8->16 track change.
#define MAX_BUS_COUNT (SONG_CHANNEL_COUNT + 1)

class MixerService: 
      public T_Singleton<MixerService>,
      public Observable,
      public I_Observer,
      public CommandExecuter      
{

public:
	MixerService() ;
	virtual ~MixerService() ;

	bool Init() ;
	void Close() ;

	bool Start() ;
	void Stop() ;

	MixBus *GetMixBus(int i) ;	

	virtual void Update(Observable &o,I_ObservableData *d) ;	

	void OnPlayerStart() ;
	void OnPlayerStop() ;

	bool Clipped() ;
    void SetPregain(int);
    void SetSoftclip(int, int);
    void SetMasterVolume(int);
    void SetRenderMode(int);
    bool IsRendering();
    int GetPlayedBufferPercentage() ;
	
	virtual void Execute(FourCC id,float value) ;

	AudioOut *GetAudioOut() ;

	void Lock() ;
	void Unlock() ;

protected:
	void toggleRendering(bool enable) ;
private:
  void initRendering(MixerServiceRenderMode);
  AudioOut *out_;
  bool ownsOut_; // true when out_ is our own DummyAudioOut (must be deleted)
  MixBus master_;
  MixBus bus_[MAX_BUS_COUNT];
  MixerServiceRenderMode mode_;
  SDL_mutex *sync_;
  bool isRendering_;
} ;
#endif
