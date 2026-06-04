#ifndef _AUDIO_MIXER_H_
#define _AUDIO_MIXER_H_

#include "AudioModule.h"
#include "Foundation/T_SimpleList.h"
#include "Application/Instruments/WavFileWriter.h"
#include <string>

struct SoftClipData {
    float alpha;
	float alpha23;
	float alphaInv;
	float gainCmp;
};

class MixRenderWorker ; // background render thread (multicore), defined in the .cpp

class AudioMixer: public AudioModule,public T_SimpleList<AudioModule> {
public:
	AudioMixer(const char *name) ;
	virtual ~AudioMixer() ;
	virtual bool Render(fixed *buffer,int samplecount) ;
	void SetFileRenderer(const char *path) ;
	void EnableRendering(bool enable) ;
	void SetVolume(fixed volume) ;
    virtual void SetSoftclip(int clip, int gain);
    virtual void SetMasterVolume(int volume) ;
	virtual bool Clipped() ;
	// Render this mixer's children across CPU cores (only worth it for the master,
	// which has the 16 channel buses). Children are independent so this is safe.
	void SetParallel(bool enable) ;

private:
	// child rendering split out of Render() so it can be done sequentially or in
	// parallel; the volume/softclip post-processing is shared.
	bool sumChildrenSequential(fixed *buffer,int samplecount) ;
	bool sumChildrenParallel(fixed *buffer,int samplecount) ;
	void applyVolumeAndClip(fixed *buffer,int samplecount) ;
	void ensureChildScratch(int count,int samplecount) ;
  fixed hardClip(fixed sample);
  fixed softClip(fixed sample);
  bool enableRendering_;
  std::string renderPath_;
  WavFileWriter *writer_;
  fixed volume_;
  std::string name_;
  SoftClipData softClipData_[4];
  int softclip_;
  int softclipGain_;
  int masterVolume_;
  bool clipped_;
  // Reusable scratch mix buffer, grown on demand and freed only at
  // destruction. Avoids a malloc/free on every Render() in the real-time
  // audio path (a source of jitter / underruns on weak devices).
  fixed *mixBuffer_;
  int mixBufferSamples_;
  // Parallel (multicore) child-render state. Set up only via SetParallel() (i.e.
  // on the master mixer, whose children are the 16 independent channel buses).
  bool parallel_;
  MixRenderWorker *worker_;   // one helper thread (main + worker = 2 cores)
  AudioModule **childList_;   // children snapshot for the current render
  fixed **childScratch_;      // per-child output buffer
  bool *childGotData_;        // per-child: did it produce sound?
  int childCapacity_;         // number of child slots allocated
  int childScratchSamples_;   // size (in samples) of each scratch buffer
} ;
#endif
