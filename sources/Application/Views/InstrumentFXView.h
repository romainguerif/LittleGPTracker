#ifndef _INSTRUMENT_FX_VIEW_H_
#define _INSTRUMENT_FX_VIEW_H_

#include "BaseClasses/FieldView.h"
#include "Foundation/Observable.h"
#include "ViewData.h"

// Per-instrument FX page (option C: extensible). Currently hosts the
// compressor; the EQ and future effects get added here.
class InstrumentFXView: public FieldView, public I_Observer {
public:
	InstrumentFXView(GUIWindow &w, ViewData *data) ;
	virtual ~InstrumentFXView() ;

	virtual void ProcessButtonMask(unsigned short mask, bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType, unsigned int) {} ;
	virtual void OnFocus() ;

protected:
	void onInstrumentChange() ;
	void fillFields() ;
	void Update(Observable &o, I_ObservableData *d) ;

private:
	Project *project_ ;
	FourCC lastFocusID_ ;
	I_Instrument *current_ ;
} ;
#endif
