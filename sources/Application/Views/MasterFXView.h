#ifndef _MASTER_FX_VIEW_H_
#define _MASTER_FX_VIEW_H_

#include "BaseClasses/FieldView.h"
#include "Foundation/Observable.h"
#include "ViewData.h"

// Master (global) FX page, sitting below Song in the navigation.
// Currently hosts the global dub delay; more master effects get added here.
class MasterFXView : public FieldView, public I_Observer {
public:
	MasterFXView(GUIWindow &w, ViewData *data) ;
	virtual ~MasterFXView() ;

	virtual void ProcessButtonMask(unsigned short mask, bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType, unsigned int) {} ;
	virtual void OnFocus() ;

protected:
	void Update(Observable &o, I_ObservableData *d) {} ;

private:
	Project *project_ ;
} ;
#endif
