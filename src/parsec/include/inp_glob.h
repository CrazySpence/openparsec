/*
 * PARSEC HEADER: inp_glob.h
 */

#ifndef _INP_GLOB_H_
#define _INP_GLOB_H_


// ----------------------------------------------------------------------------
// INPUT SUBSYSTEM (INP) related global declarations and definitions          -
// ----------------------------------------------------------------------------

// current joystick sensitivity (timing)

extern float	joy_yaw_corr_refframe;
extern float	joy_pitch_corr_refframe;
extern float	joy_roll_corr_refframe;
extern float	joy_acc_corr_refframe;

extern int 		inp_mouse_invert_yaxis;
extern int 		inp_mouse_sensitivity;
extern int 		inp_mouse_drift;
extern int		inp_mouse_gun_button;           // which button fires gun      (default: MOUSE_BUTTON_LEFT)
extern int		inp_mouse_missile_button;       // which button fires missile  (default: MOUSE_BUTTON_RIGHT)
extern int		inp_mouse_target_button;        // which button cycles targets (default: -1 = unbound)
extern int		inp_mouse_nextgun_button;       // which button cycles guns    (default: -1 = unbound)
extern int		inp_mouse_nextmissile_button;   // which button cycles missiles(default: -1 = unbound)
extern int		inp_mouse_accel_button;         // which button accelerates    (default: -1 = unbound)
extern int		inp_mouse_decel_button;         // which button decelerates    (default: -1 = unbound)
extern int		inp_mouse_scroll_y;             // scroll-wheel accumulator (cleared each frame by INPs_Collect)

#endif // _INP_GLOB_H_


