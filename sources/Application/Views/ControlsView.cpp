#include "ControlsView.h"
#include "Application/Player/Player.h"

// Notation: S = bouton bas (action/edit), E = bouton droite, L/R = gachettes,
// up/down/left/right = croix directionnelle. N (haut/X) et O (gauche/Y) sont
// libres. La liste defile avec up/down ; R+croix revient au projet.
static const char *kLines[] = {
	"Boutons: S=bas  E=droite",
	"L R=gachettes  croix=up/dn/lf/rt",
	"(N=haut O=gauche : libres)",
	"",
	"-- PAGES (R + croix) --",
	"R+up      Song -> Project",
	"R+down    Song -> Master FX",
	"R+right   Song -> Chain",
	"R+left    Chain -> Song",
	"R+right   Chain -> Phrase",
	"R+left    Phrase -> Chain",
	"R+right   Phrase -> Instrument",
	"R+up      Phrase -> Groove",
	"R+down    Phrase -> Table",
	"R+up      Instr -> Instr FX",
	"R+down    Instr -> Table",
	"R+down    Instr FX -> Instr",
	"R+down    Project -> Song",
	"R+up      Master FX -> Song",
	"Sel+L/R   Song: pistes 1-8 / 9-16",
	"",
	"-- LECTURE --",
	"Start     play / audition",
	"R+Start   stop",
	"L+Start   play ligne courante",
	"L+up/dn   saut de section",
	"L+lf/rt   nudge tempo (+/-)",
	"",
	"-- EDITION (Song/Chain/Phr/Tab) --",
	"S+left/rt valeur  -1 / +1",
	"S+up/down valeur +16 / -16",
	"S         repose derniere valeur",
	"S+L       coller (pousse vers bas)",
	"E+S       couper (remonte le reste)",
	"E+L       mode clone",
	"E+R       mute on/off",
	"E+lf/rt   warp piste voisine",
	"E+up/down warp colonne",
	"",
	"-- SELECTION --",
	"E         copier",
	"E+L       etendre la selection",
	"S+L       couper la selection",
	"E+R       Phrase/Tab: interpoler",
	"",
	"-- INSTRUMENT --",
	"E+lf/rt   instrument  -1 / +1",
	"E+up/down instrument -16 / +16",
	"E+S       vider (sample / table)",
	"S         (champ table) nouveau",
	"",
	"-- PHRASE: colonne commande --",
	"S+up/down ouvre le selecteur de cmd",
	"",
	"(la description de chaque commande",
	" s'affiche en haut de la track)",
} ;

#define CONTROLS_LINE_COUNT ((int)(sizeof(kLines)/sizeof(kLines[0])))
#define CONTROLS_VISIBLE 24
#define CONTROLS_TOP_Y 4

ControlsView::ControlsView(GUIWindow &w, ViewData *viewData)
    : View(w, viewData) {
	scrollOffset_ = 0 ;
}

ControlsView::~ControlsView() {}

void ControlsView::OnFocus() { scrollOffset_ = 0 ; }

void ControlsView::OnPlayerUpdate(PlayerEventType, unsigned int) {}

void ControlsView::DrawView() {

	Clear() ;
	GUITextProperties props ;

	SetColor(CD_NORMAL) ;
	DrawString(1, 1, "CONTROLS", props) ;

	int maxScroll = CONTROLS_LINE_COUNT - CONTROLS_VISIBLE ;
	if (maxScroll < 0) maxScroll = 0 ;
	if (scrollOffset_ > maxScroll) scrollOffset_ = maxScroll ;
	if (scrollOffset_ < 0) scrollOffset_ = 0 ;

	for (int i = 0; i < CONTROLS_VISIBLE; i++) {
		int idx = scrollOffset_ + i ;
		if (idx >= CONTROLS_LINE_COUNT) break ;
		const char *line = kLines[idx] ;
		// section headers (start with "--") get the highlight colour
		if (line[0] == '-' && line[1] == '-') {
			SetColor(CD_HILITE2) ;
		} else {
			SetColor(CD_NORMAL) ;
		}
		DrawString(1, CONTROLS_TOP_Y + i, line, props) ;
	}

	// scroll hint
	SetColor(CD_HILITE1) ;
	if (scrollOffset_ < maxScroll) {
		DrawString(1, CONTROLS_TOP_Y + CONTROLS_VISIBLE, "up/down: defiler   R+croix: retour", props) ;
	} else {
		DrawString(1, CONTROLS_TOP_Y + CONTROLS_VISIBLE, "R+croix: retour projet", props) ;
	}
	SetColor(CD_NORMAL) ;
}

void ControlsView::ProcessButtonMask(unsigned short mask, bool pressed) {

	if (!pressed) {
		return ;
	}

	// R + any direction -> back to the Project page
	if (mask & EPBM_R) {
		if (mask & (EPBM_UP | EPBM_DOWN | EPBM_LEFT | EPBM_RIGHT)) {
			ViewType vt = VT_PROJECT ;
			ViewEvent ve(VET_SWITCH_VIEW, &vt) ;
			SetChanged() ;
			NotifyObservers(&ve) ;
		}
		return ;
	}

	// plain up/down scrolls the list
	if (mask & EPBM_DOWN) {
		scrollOffset_++ ;
		isDirty_ = true ;
	}
	if (mask & EPBM_UP) {
		scrollOffset_-- ;
		if (scrollOffset_ < 0) scrollOffset_ = 0 ;
		isDirty_ = true ;
	}
}
