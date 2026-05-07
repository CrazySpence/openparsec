/*
 * PARSEC HEADER: m_ctrlcfg.h
 *
 * Controls Configuration Screen — keyboard / mouse / joystick tabs.
 */

#ifndef _M_CTRLCFG_H_
#define _M_CTRLCFG_H_


// tab identifiers ------------------------------------------------------------
//
enum {
	CTRLTAB_KEYBOARD  = 0,
	CTRLTAB_MOUSE     = 1,
	CTRLTAB_JOYSTICK  = 2,
	NUM_CTRLTABS
};


// external functions — lifecycle ---------------------------------------------
//
void	InitCtrlConfig();
void	DrawCtrlConfigScreen();


// external functions — navigation --------------------------------------------
//
void	CtrlCfgCursorUp();
void	CtrlCfgCursorDown();
void	CtrlCfgLeft();
void	CtrlCfgRight();
void	CtrlCfgSelect();         // Enter: start remap for selected row
void	CtrlCfgSelectLayer2();   // Shift+Enter: remap layer 2 (keyboard tab only)
void	CtrlCfgClearBinding();   // Del: clear selected binding
void	CtrlCfgResetDefaults();  // R: reset all bindings on current tab
void	CtrlCfgExit();           // Esc: trigger transition back to options
void	CtrlCfgHandleSpecialKeys(); // R/Del/Shift+Enter — call each frame when visible


// external functions — per-frame input capture -------------------------------
//
bool	CtrlCfgWaitingForInput();
void	CtrlCfgPollInput();                         // call each frame while visible
void	CtrlCfgHandleKeyPress( dword mkc_code );    // keyboard-tab remap
void	CtrlCfgHandleButtonPress( int button_idx ); // joystick-tab remap


// external functions — mouse support ----------------------------------------
//
int		MouseOverCtrlCfg( int mousex, int mousey );
void	CtrlCfgMouseClick( int mousex, int mousey );


// external functions — slide animation (same interface as m_option) ----------
//
void	MoveInCtrlConfig();
void	MoveOutCtrlConfig();
void	SlideInCtrlConfig();
void	SlideOutCtrlConfig();
int		SlideFinishedCtrlConfig();


#endif // _M_CTRLCFG_H_
