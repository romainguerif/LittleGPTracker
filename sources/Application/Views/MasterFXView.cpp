#include "MasterFXView.h"
#include "BaseClasses/UIIntVarField.h"
#include "Foundation/Variables/Variable.h"
#include "Services/Audio/Delay.h"
#include "System/System/System.h"
#include <stdio.h>

MasterFXView::MasterFXView(GUIWindow &w, ViewData *data)
    : FieldView(w, data) {
	project_ = data->project_ ;

	GUIPoint position = GetAnchor() ;
	UIIntVarField *field ;
	Delay *dly = Delay::GetInstance() ;

	// --- DELAY (dub) ---  header drawn in DrawView, leave a row for it
	position._y += 1 ;
	field = new UIIntVarField(position, *dly->onVar_, "delay:    %s", 0, 1, 1, 1) ;
	T_SimpleList<UIField>::Insert(field) ;

	position._y += 1 ;
	field = new UIIntVarField(position, *dly->timeVar_, "time:     %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(field) ;

	position._y += 1 ;
	field = new UIIntVarField(position, *dly->feedbackVar_, "feedback: %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(field) ;

	position._y += 1 ;
	field = new UIIntVarField(position, *dly->toneVar_, "tone:     %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(field) ;

	position._y += 1 ;
	field = new UIIntVarField(position, *dly->wetVar_, "return:   %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(field) ;

	position._y += 1 ;
	field = new UIIntVarField(position, *dly->pingpongVar_, "pingpong: %s", 0, 1, 1, 1) ;
	T_SimpleList<UIField>::Insert(field) ;
}

MasterFXView::~MasterFXView() {}

void MasterFXView::OnFocus() {
	UIField *first = T_SimpleList<UIField>::GetFirst() ;
	if (first) {
		SetFocus(first) ;
	}
}

void MasterFXView::DrawView() {

	Clear() ;
	View::EnableNotification() ;

	GUITextProperties props ;
	GUIPoint pos = GetTitlePosition() ;

	SetColor(CD_NORMAL) ;
	DrawString(pos._x, pos._y, "Master FX", props) ;

	// Section header
	GUIPoint a = GetAnchor() ;
	SetColor(CD_HILITE2) ;
	DrawString(a._x, a._y, "DELAY (dub)", props) ;
	SetColor(CD_NORMAL) ;

	// Hint: instruments feed this via their "delay send" (page FX).
	GUIPoint hint = a ;
	hint._y += 9 ;
	SetColor(CD_HILITE1) ;
	DrawString(hint._x, hint._y, "send per instr: FX page", props) ;
	SetColor(CD_NORMAL) ;

	FieldView::Redraw() ;
	drawMap() ;
}

void MasterFXView::ProcessButtonMask(unsigned short mask, bool pressed) {

	if (!pressed) {
		return ;
	}

	FieldView::ProcessButtonMask(mask) ;

	// R + UP : back up to Song (Master FX sits below Song)
	if (mask & EPBM_R) {
		if (mask & EPBM_UP) {
			ViewType vt = VT_SONG ;
			ViewEvent ve(VET_SWITCH_VIEW, &vt) ;
			SetChanged() ;
			NotifyObservers(&ve) ;
		}
	}
}
