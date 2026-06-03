#ifndef _CONTROLS_VIEW_H_
#define _CONTROLS_VIEW_H_

#include "BaseClasses/View.h"
#include "ViewData.h"

// Read-only, scrollable cheat-sheet of all the controls, reached from the
// Project page. Notation: face buttons N/S/E/O (only S=bottom and E=right are
// mapped), d-pad up/down/left/right, shoulders L/R.
class ControlsView : public View {
public:
	ControlsView(GUIWindow &w, ViewData *viewData) ;
	~ControlsView() ;
	virtual void ProcessButtonMask(unsigned short mask, bool pressed) ;
	virtual void DrawView() ;
	virtual void OnPlayerUpdate(PlayerEventType, unsigned int tick = 0) ;
	virtual void OnFocus() ;
private:
	int scrollOffset_ ;
} ;
#endif
