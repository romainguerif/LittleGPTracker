#ifndef _SAMPLE_INSTRUMENT_H_
#define _SAMPLE_INSTRUMENT_H_

#include "I_Instrument.h"
#include "SampleRenderingParams.h"
#include "SRPUpdaters.h"

#include "SoundSource.h"
#include "Application/Model/Song.h" 
#include "Foundation/Observable.h"
#include "Foundation/Types/Types.h"
#include "Foundation/Variables/WatchedVariable.h"

enum SampleInstrumentLoopMode {
    SILM_ONESHOT = 0,
    SILM_LOOP,
    SILM_LOOP_PINGPONG,
    SILM_OSC,
    //	SILM_OSCFINE,
    SILM_LOOPSYNC,
    SILM_LAST
};

#define NO_SAMPLE (-1)
#define SIP_VOLUME    		MAKE_FOURCC('V','O','L','M')
#define SIP_CRUSH 	  		MAKE_FOURCC('C','R','S','H')
#define SIP_CRUSHVOL 	  	MAKE_FOURCC('C','R','S','V')
#define SIP_DOWNSMPL 	  	MAKE_FOURCC('D','S','P','L')
#define SIP_ROOTNOTE  		MAKE_FOURCC('R','O','O','T')
#define SIP_FINETUNE  		MAKE_FOURCC('F','N','T','N')
#define SIP_PAN 	  		MAKE_FOURCC('P','A','N','_')
#define SIP_START       	MAKE_FOURCC('S','T','R','T')
#define SIP_END       		MAKE_FOURCC('E','N','D','_')
#define SIP_LOOPMODE  		MAKE_FOURCC('L','M','O','D')
#define SIP_LOOPSTART 		MAKE_FOURCC('L','S','T','A')
#define SIP_LOOPLEN 		MAKE_FOURCC('L','L','E','N')
#define SIP_INTERPOLATION   MAKE_FOURCC('I','N','T','P')
#define SIP_SAMPLE 		    MAKE_FOURCC('S','M','P','L')
#define SIP_SLICES 		    MAKE_FOURCC('S','L','C','S')
#define SIP_FILTMODE		MAKE_FOURCC('F','I','M','O') 
#define SIP_ATTENUATE		MAKE_FOURCC('F','I','A','T')
#define SIP_FILTMIX			MAKE_FOURCC('F','M','I','X')
#define SIP_FILTCUTOFF		MAKE_FOURCC('F','C','U','T')
#define SIP_FILTRESO		MAKE_FOURCC('F','R','E','S')
#define SIP_TABLE			MAKE_FOURCC('T','A','B','L')
#define SIP_TABLEAUTO		MAKE_FOURCC('T','B','L','A')
#define SIP_FBTUNE			MAKE_FOURCC('F','B','T','U')
#define SIP_FBMIX			MAKE_FOURCC('F','B','M','X')
#define SIP_PRINTFX MAKE_FOURCC('P', 'R', 'F', 'X')
#define SIP_IR_PAD MAKE_FOURCC('I', 'R', 'P', 'D')
#define SIP_IR_WET MAKE_FOURCC('I', 'R', 'W', 'T')
// Per-instrument compressor (applied per voice, after the filter)
#define SIP_COMP_ON      MAKE_FOURCC('C','P','O','N')
#define SIP_COMP_THRESH  MAKE_FOURCC('C','P','T','H')
#define SIP_COMP_RATIO   MAKE_FOURCC('C','P','R','A')
#define SIP_COMP_ATTACK  MAKE_FOURCC('C','P','A','T')
#define SIP_COMP_RELEASE MAKE_FOURCC('C','P','R','E')
#define SIP_COMP_MAKEUP  MAKE_FOURCC('C','P','M','K')
// Per-instrument EQ (3-band: low shelf / mid bell / high shelf)
#define SIP_EQ_ON   MAKE_FOURCC('E','Q','O','N')
#define SIP_EQ_LOW  MAKE_FOURCC('E','Q','L','O')
#define SIP_EQ_MID  MAKE_FOURCC('E','Q','M','D')
#define SIP_EQ_HIGH MAKE_FOURCC('E','Q','H','I')
// Per-instrument LFO (modulates cutoff / volume / pitch) — hypnotic movement
#define SIP_LFO_ON     MAKE_FOURCC('L','F','O','N')
#define SIP_LFO_TARGET MAKE_FOURCC('L','F','T','G')
#define SIP_LFO_RATE   MAKE_FOURCC('L','F','R','T')
#define SIP_LFO_DEPTH  MAKE_FOURCC('L','F','D','P')

#define FB_BUFFER_LENGTH 3500 // (in samples)

class SampleInstrument: public I_Instrument,I_Observer {

public:
       SampleInstrument() ;
       virtual ~SampleInstrument() ;
       // I_Instrument implementation
	   virtual bool Init() ;
       virtual bool Start(int channel,unsigned char note,bool trigger=true) ;
       virtual void Stop(int channel) ;
       virtual bool Render(int channel,fixed *buffer,int size,bool updateTick) ;
       virtual bool IsInitialized() ;
	   virtual bool IsEmpty() ;

	   virtual InstrumentType GetType() { return IT_SAMPLE ; } ;
  	   virtual void ProcessCommand(int channel,FourCC cc,ushort value) ;
	   virtual void Purge() ;
	   virtual int GetTable() ;
	   virtual bool GetTableAutomation();
	   virtual void GetTableState(TableSaveState &state) ;	 
	   virtual void SetTableState(TableSaveState &state) ;	 

	   bool IsMulti() ;

	  // Engine playback  start callback

	  virtual void OnStart() ;

	   // I_Observer
       virtual void Update(Observable &o,I_ObservableData *d);
       // Additional
       void AssignSample(int i) ;
	   int GetSampleIndex() ;
	   int GetVolume() ;
	   void SetVolume(int) ;
	   int GetSampleSize(int channel=-1) ;
       int GetLoopEnd();
       virtual const char *GetName();
       virtual const char *GetFileName();
 
  static void EnableDownsamplingLegacy();

protected:
		void updateInstrumentData(bool search) ;
		void doTickUpdate(int channel) ;
		void doKRateUpdate(int channel) ;
		void updateFeedback(renderParams *rp) ;

private:
       SoundSource *source_ ;
       struct renderParams renderParams_[SONG_CHANNEL_COUNT] ;
       bool running_ ;
       bool dirty_ ;
	   TableSaveState tableState_ ;
	   
	   static int lastMidiNote_[SONG_CHANNEL_COUNT] ;
	   static fixed lastSample_[SONG_CHANNEL_COUNT][2] ;
	   static fixed feedback_[SONG_CHANNEL_COUNT][FB_BUFFER_LENGTH*2] ;
	   static float compGain_[SONG_CHANNEL_COUNT] ; // smoothed compressor gain per channel
	   // EQ biquad state: [channel][L/R][band 0..2][z1,z2]
	   static float eqZ_[SONG_CHANNEL_COUNT][2][3][2] ;
	   static float lfoPhase_[SONG_CHANNEL_COUNT] ; // LFO phase 0..1 per channel

	   Variable *volume_ ;
	   Variable *crush_ ;
	   Variable *cutoff_ ;
	   Variable *reso_ ;
	   Variable *table_ ;
	   Variable *tableAuto_ ;
	   Variable *downsample_ ;
	   Variable *rootNote_ ;
	   Variable *fineTune_ ;
	   Variable *drive_ ;
	   Variable *fbMix_ ;
	   Variable *fbTune_ ;
	   WatchedVariable *start_ ;
	   WatchedVariable *loopStart_ ;
	   WatchedVariable *loopEnd_ ;
	   Variable *filterMix_ ;
	   Variable *filterMode_ ;
	   Variable *attenuate_ ;
	   Variable *pan_ ;
	   Variable *loopMode_ ;
	   Variable *slices_ ;
	   Variable *interpolation_ ;
       Variable *printFx_;
       Variable *irPad_;
       Variable *irWet_;
       // Per-instrument compressor (applied per voice, after the filter)
       Variable *compOn_;
       Variable *compThresh_;
       Variable *compRatio_;
       Variable *compAttack_;
       Variable *compRelease_;
       Variable *compMakeup_;
       // Per-instrument 3-band EQ
       Variable *eqOn_;
       Variable *eqLow_;
       Variable *eqMid_;
       Variable *eqHigh_;
       // Per-instrument LFO
       Variable *lfoOn_;
       Variable *lfoTarget_;
       Variable *lfoRate_;
       Variable *lfoDepth_;

       static bool useDirtyDownsampling_;
       char *fxPresets[4];
} ;
#endif
