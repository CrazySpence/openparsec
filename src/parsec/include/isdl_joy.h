/*
 * PARSEC HEADER: isdl_joy.h
 */

#ifndef _ISDL_JOY_H_
#define _ISDL_JOY_H_


// binding table entry — one row in the joystick config UI -------------------
//
struct joybinding_s {
	const char* display_name;  // e.g. "Fire Gun"
	int*        var;           // pointer to the binding variable
	int         sdl_default;   // SDL_CONTROLLER_BUTTON_* or axis idx; -1 = none
	bool        is_axis;       // true for axis entries (use ←→ to remap)
};


// external variables ---------------------------------------------------------
//
extern byte isdl_NumButtons;  // number of buttons on the connected joystick


// external functions ---------------------------------------------------------
//
void	ISDL_JoyInitHandler();
void	ISDL_JoyKillHandler();
void	ISDL_JoyCollect();

joybinding_s*	ISDL_GetBindingTable();
int				ISDL_GetBindingCount();
void			ISDL_SetBinding( int index, int value );
void			ISDL_ResetBindings();
int				ISDL_FindButtonOwner( int button );

// Controls-config helpers — work regardless of QueryJoystick / Op_Joystick
int		ISDL_JoyAvailable();          // true if a joystick handle is open
int		ISDL_JoyScanButtons();        // returns first pressed button idx (incl. virtual), or -1
void	ISDL_SnapshotButtons( byte* snap, int size ); // fill snap[0..size-1] with current button states

// Per-axis deadzone helpers --------------------------------------------------
int		ISDL_GetDeadzone( int axis );             // returns deadzone magnitude (0..30000)
void	ISDL_SetDeadzone( int axis, int value );  // clamps 0..30000, sets Min=-value, Max=+value
int		ISDL_GetRawAxis( int axis );              // returns raw SDL axis value (-32767..32767), or 0 if unavailable
#define ISDL_JOY_DEADZONE_DEFAULT 3000           // match the C array defaults above


// device/platform dependend constraints --------------------------------------
//
#define JOY_DEVICE_MIN				-32767
#define JOY_DEVICE_MAX			 	 32767
#define JOY_DEVICE_RANGE		 	 65535

#define JOY_X_DIV				 	 1024 	//JOY_DEVICE_RANGE / JOY_X_RANGE;
#define JOY_Y_DIV					 1024	//JOY_DEVICE_RANGE / JOY_Y_RANGE;
#define JOY_THROTTLE_DIV			 256 	//JOY_DEVICE_RANGE / JOY_THROTTLE_RANGE
#define JOY_THROTTLE_OFF			 128	//JOY_THROTTLE_RANGE / 2
#define JOY_RUDDER_DIV				 1024 	//JOY_DEVICE_RANGE / JOY_RUDDER_RANGE

#endif // _ISDL_JOY_H_


