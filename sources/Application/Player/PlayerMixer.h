
#ifndef _APPLICATION_MIXER_H_
#define _APPLICATION_MIXER_H_

#include "Foundation/T_Singleton.h"
#include "Application/Model/Project.h"
#include "Application/Views/ViewData.h"
#include "Application/Utils/fixed.h"
#include "Application/Audio/AudioFileStreamer.h"
#include "PlayerChannel.h"
#include "Foundation/Observable.h"
#include "Services/Audio/AudioOut.h"

// Dedicated bus for the audio file streamer (import preview), placed just past
// the track buses so it never collides with a real track (was 8, which was a
// real track after the 8->16 change). Must stay < MAX_BUS_COUNT.
#define STREAM_MIX_BUS SONG_CHANNEL_COUNT

class PlayerMixer: public T_Singleton<PlayerMixer>,public Observable,public I_Observer {
public:
	PlayerMixer() ;
	virtual ~PlayerMixer() {} ;

	bool Start() ;
	void Stop() ;
	bool Init(Project *project) ;
	void Close() ;

	void OnPlayerStart() ;
	void OnPlayerStop() ;

	void StartInstrument(int channel,I_Instrument *instrument,unsigned char note,bool newInstrument) ;
	void StopInstrument(int channel) ;

	int GetChannelNote(int Channel) ;

	I_Instrument *GetInstrument(int channel) ;

	I_Instrument *GetLastInstrument(int channel) ;
	
	void StartChannel(int channel) ;
	void StopChannel(int channel) ;

	bool IsChannelPlaying(int channel) ;
	
	void StartStreaming(const Path &) ;
	void StopStreaming()  ;

	bool Clipped() ;

	void Update(Observable &o,I_ObservableData *d) ;
	int GetPlayedBufferPercentage() ;   

	void SetChannelMute(int channel,bool mute) ;
	bool IsChannelMuted(int channel) ;

	char *GetPlayedNote(int channel) ;
	char *GetPlayedOctive(int channel) ;
	
	AudioOut *GetAudioOut() ;

	void Lock() ;
	void Unlock() ;

private:

	Project *project_ ;
	bool clipped_ ;
	
    I_Instrument *lastInstrument_[SONG_CHANNEL_COUNT] ;
	bool isChannelPlaying_[SONG_CHANNEL_COUNT] ;

	AudioFileStreamer fileStreamer_ ;
	PlayerChannel *channel_[SONG_CHANNEL_COUNT] ;

	// store trigger notes, 0xFF = none
	
    unsigned char notes_[SONG_CHANNEL_COUNT] ;
} ;

#endif
