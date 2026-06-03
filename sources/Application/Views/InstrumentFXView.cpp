#include "InstrumentFXView.h"
#include "Application/Instruments/InstrumentBank.h"
#include "Application/Instruments/SampleInstrument.h"
#include "BaseClasses/UIIntVarField.h"
#include "Foundation/Variables/Variable.h"
#include "System/System/System.h"
#include <stdio.h>

InstrumentFXView::InstrumentFXView(GUIWindow &w, ViewData *data)
    : FieldView(w, data) {
	project_ = data->project_ ;
	lastFocusID_ = 0 ;
	current_ = 0 ;
	onInstrumentChange() ;
}

InstrumentFXView::~InstrumentFXView() {}

void InstrumentFXView::onInstrumentChange() {

	ClearFocus() ;

	I_Instrument *old = current_ ;

	int i = viewData_->currentInstrument_ ;
	InstrumentBank *bank = viewData_->project_->GetInstrumentBank() ;
	current_ = bank->GetInstrument(i) ;

	if (current_ != old) {
		current_->RemoveObserver(*this) ;
	}
	T_SimpleList<UIField>::Empty() ;

	fillFields() ;

	// MIDI instruments have no FX fields -> nothing to focus.
	UIField *first = T_SimpleList<UIField>::GetFirst() ;
	if (first) {
		SetFocus(first) ;
		IteratorPtr<UIField> it2(T_SimpleList<UIField>::GetIterator()) ;
		for (it2->Begin(); !it2->IsDone(); it2->Next()) {
			UIIntVarField &field = (UIIntVarField &)it2->CurrentItem() ;
			if (field.GetVariableID() == lastFocusID_) {
				SetFocus(&field) ;
				break ;
			}
		}
	}
	if (current_ != old) {
		current_->AddObserver(*this) ;
	}
}

void InstrumentFXView::fillFields() {

	int i = viewData_->currentInstrument_ ;
	InstrumentBank *bank = viewData_->project_->GetInstrumentBank() ;
	I_Instrument *instr = bank->GetInstrument(i) ;

	// FX live on SampleInstrument only.
	if (instr->GetType() != IT_SAMPLE) {
		return ;
	}
	SampleInstrument *instrument = (SampleInstrument *)instr ;

	GUIPoint position = GetAnchor() ;
	position._y += 1 ; // leave a row for the COMPRESSOR header

	Variable *v ;
	UIIntVarField *f ;

	v = instrument->FindVariable(SIP_COMP_ON) ;
	f = new UIIntVarField(position, *v, "comp: %s", 0, 1, 1, 1) ;
	T_SimpleList<UIField>::Insert(f) ;
	f->SetFocus() ;

	position._y += 1 ;
	v = instrument->FindVariable(SIP_COMP_THRESH) ;
	f = new UIIntVarField(position, *v, "threshold: %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(f) ;

	position._y += 1 ;
	v = instrument->FindVariable(SIP_COMP_RATIO) ;
	f = new UIIntVarField(position, *v, "ratio:     %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(f) ;

	position._y += 1 ;
	v = instrument->FindVariable(SIP_COMP_ATTACK) ;
	f = new UIIntVarField(position, *v, "attack:    %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(f) ;

	position._y += 1 ;
	v = instrument->FindVariable(SIP_COMP_RELEASE) ;
	f = new UIIntVarField(position, *v, "release:   %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(f) ;

	position._y += 1 ;
	v = instrument->FindVariable(SIP_COMP_MAKEUP) ;
	f = new UIIntVarField(position, *v, "makeup:    %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(f) ;

	// EQ section (header drawn in DrawView). Center 0x80 = flat (0 dB).
	position._y += 3 ;
	v = instrument->FindVariable(SIP_EQ_ON) ;
	f = new UIIntVarField(position, *v, "eq: %s", 0, 1, 1, 1) ;
	T_SimpleList<UIField>::Insert(f) ;

	position._y += 1 ;
	v = instrument->FindVariable(SIP_EQ_LOW) ;
	f = new UIIntVarField(position, *v, "low:       %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(f) ;

	position._y += 1 ;
	v = instrument->FindVariable(SIP_EQ_MID) ;
	f = new UIIntVarField(position, *v, "mid:       %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(f) ;

	position._y += 1 ;
	v = instrument->FindVariable(SIP_EQ_HIGH) ;
	f = new UIIntVarField(position, *v, "high:      %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(f) ;

	// LFO section (header drawn in DrawView)
	position._y += 3 ;
	v = instrument->FindVariable(SIP_LFO_ON) ;
	f = new UIIntVarField(position, *v, "lfo: %s", 0, 1, 1, 1) ;
	T_SimpleList<UIField>::Insert(f) ;

	position._y += 1 ;
	v = instrument->FindVariable(SIP_LFO_TARGET) ;
	f = new UIIntVarField(position, *v, "target: %s", 0, 3, 1, 1) ;
	T_SimpleList<UIField>::Insert(f) ;

	position._y += 1 ;
	v = instrument->FindVariable(SIP_LFO_RATE) ;
	f = new UIIntVarField(position, *v, "rate:      %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(f) ;

	position._y += 1 ;
	v = instrument->FindVariable(SIP_LFO_DEPTH) ;
	f = new UIIntVarField(position, *v, "depth:     %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(f) ;

	// Delay send (feeds the global dub delay configured on the Project page)
	position._y += 3 ;
	v = instrument->FindVariable(SIP_DELAY_SEND) ;
	f = new UIIntVarField(position, *v, "delay send: %2.2X", 0, 255, 1, 0x10) ;
	T_SimpleList<UIField>::Insert(f) ;
}

void InstrumentFXView::DrawView() {

	Clear() ;
	View::EnableNotification() ;

	GUITextProperties props ;
	GUIPoint pos = GetTitlePosition() ;

	char title[24] ;
	SetColor(CD_NORMAL) ;
	sprintf(title, "Instr FX %2.2X", viewData_->currentInstrument_) ;
	DrawString(pos._x, pos._y, title, props) ;

	// Section headers
	GUIPoint a = GetAnchor() ;
	SetColor(CD_HILITE2) ;
	DrawString(a._x, a._y, "COMPRESSOR", props) ;
	GUIPoint eqHdr = a ;
	eqHdr._y += 8 ;
	DrawString(eqHdr._x, eqHdr._y, "EQ (low/mid/high)", props) ;
	GUIPoint lfoHdr = a ;
	lfoHdr._y += 14 ;
	DrawString(lfoHdr._x, lfoHdr._y, "LFO (hypnotic mod)", props) ;
	SetColor(CD_NORMAL) ;

	FieldView::Redraw() ;
	drawMap() ;
}

void InstrumentFXView::OnFocus() { onInstrumentChange() ; }

void InstrumentFXView::Update(Observable &o, I_ObservableData *d) {
	onInstrumentChange() ;
}

void InstrumentFXView::ProcessButtonMask(unsigned short mask, bool pressed) {

	if (!pressed) {
		return ;
	}

	FieldView::ProcessButtonMask(mask) ;

	// R + DOWN : back to the instrument page (mirrors instrument -> FX via R+UP)
	if (mask & EPBM_R) {
		if (mask & EPBM_DOWN) {
			ViewType vt = VT_INSTRUMENT ;
			ViewEvent ve(VET_SWITCH_VIEW, &vt) ;
			SetChanged() ;
			NotifyObservers(&ve) ;
		}
	}
}
