#pragma once

#include "Foundation/Types/Types.h"
#include "Foundation/Observable.h"
#include "MidiMessage.h"

struct MidiMessage:public I_ObservableData
{
  enum Type
  {
    MIDI_NOTE_OFF = 0x80,
    MIDI_NOTE_ON = 0x90,
    MIDI_AFTERTOUCH = 0xA0,
    MIDI_CONTROLLER = 0xB0,
    MIDI_PROGRAM_CHANGE = 0xC0,
    MIDI_CHANNEL_AFTERTOUCH = 0xD0,
    MIDI_PITCH_BEND = 0xE0,
    MIDI_MIDI_CLOCK = 0xF0,
  };
  
 static const unsigned char UNUSED_BYTE = 255;
  
  MidiMessage(
    unsigned char status=UNUSED_BYTE,
    unsigned char data1=UNUSED_BYTE,
    unsigned char data2=UNUSED_BYTE)
  : status_(status),
    data1_(data1),
    data2_(data2),
    stamp_(0)
  {
  };
  
  //----------------------------------------------------------------------------
  
  inline MidiMessage::Type GetType()
  {
    return (MidiMessage::Type)(status_&0xF0);
  }
  
  //----------------------------------------------------------------------------
  

	unsigned char status_ ;
	unsigned char data1_ ;
	unsigned char data2_ ;
	// Audio-frame at which this message should be HEARD (set at generation time
	// from MidiClock::CurrentProduced). Used by the precise MIDI scheduler to send
	// it the instant that frame is played. 0 = no timestamp (send immediately).
	unsigned long long stamp_ ;
};