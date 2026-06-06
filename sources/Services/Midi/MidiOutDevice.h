#ifndef _MIDIOUT_DEVICE_H_
#define _MIDIOUT_DEVICE_H_

#include <string>
#include "Foundation/Types/Types.h"
#include "Foundation/T_SimpleList.h"
#include "MidiMessage.h"

class MidiOutDevice {
public:
	MidiOutDevice(const char *name) ;
	virtual ~MidiOutDevice() ;

	const char *GetName() ;
	void SetName(const char *name);

	virtual bool Init()=0 ;
	virtual void Close()=0 ;
	virtual bool Start()=0 ;
	virtual void Stop()=0 ;

	/*! Sends a whole queue of messages - default implementation
		is to send every message one after the other using sendmessage
	*/

	virtual void SendQueue(T_SimpleList<MidiMessage> &queue)  ;
	virtual void SendMessage(MidiMessage &m)=0 ;

	// Precise-output timing offset in audio frames: +ve sends earlier (lead),
	// -ve later. Only devices with a frame-accurate scheduler honor it (default
	// no-op). Used to fine-tune alignment vs external gear.
	virtual void SetTimingOffset(int frames) {}

	// True if this device has a frame-accurate scheduler (MidiService then hands
	// it timestamped messages directly instead of using the per-buffer flush).
	virtual bool IsPrecise() { return false; }

private:
	std::string name_ ;
} ;
#endif
