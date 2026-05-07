/*
 * PARSEC - Controls Configuration Screen
 *
 * Three-tab graphical UI for remapping keyboard, mouse, and joystick bindings.
 * Accessed from the Options menu via "configure controls...".
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

// C library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// compilation flags/debug support — must come before SDL so SYSTEM_TARGET_LINUX is defined
#include "config.h"

// SDL — for per-frame keyboard state scan during remap-wait
#ifdef SYSTEM_TARGET_LINUX
	#include <SDL2/SDL.h>
#else
	#include <SDL.h>
#endif
#include "debug.h"

// general definitions
#include "general.h"
#include "objstruc.h"

// global externals
#include "globals.h"

#include "con_com.h"

// subsystem headers
#include "aud_defs.h"
#include "inp_defs.h"
#include "net_defs.h"
#include "sys_defs.h"
#include "vid_defs.h"

// drawing subsystem
#include "d_font.h"

// rendering subsystem
#include "r_patch.h"
#include "r_supp.h"

// local module header
#include "m_ctrlcfg.h"

// proprietary module headers
#include "con_aux.h"
#include "con_ext.h"
#include "con_kmap.h"
#include "con_main.h"
#include "e_color.h"
#include "e_draw.h"
#include "e_global.h"
#include "e_supp.h"
#include "inp_glob.h"
#include "isdl_joy.h"
#include "keycodes.h"
#include "m_main.h"


// ---------------------------------------------------------------------------
// constants
// ---------------------------------------------------------------------------

#define CTRLCFG_SLIDE_SPEED     12
#define CTRLCFG_MAX_TEXTLEN     255
#define CTRLCFG_MAX_VISIBLE     14      // max rows visible at once in lists
#define CTRLCFG_CAPTION_STR     "CONTROLS"

// position constants — mirror m_option.cpp
#define CTRLCFG_POS_RIGHT       0
#define CTRLCFG_POS_LEFT        ( m_sintab_size - 1 )

// mouse tab row ids — button-assignment rows come first (< MROW_INVERT),
// settings rows follow (MROW_INVERT..MROW_COUNT-1).
enum {
	MROW_LBUTTON = 0,
	MROW_RBUTTON,
	MROW_TARGET_BTN,
	MROW_NEXTGUN_BTN,
	MROW_NEXTMISSILE_BTN,
	MROW_ACCEL_BTN,
	MROW_DECEL_BTN,
	MROW_INVERT,        // first settings row
	MROW_SENSITIVITY,
	MROW_DRIFT,
	MROW_COUNT
};


// ---------------------------------------------------------------------------
// external symbols from m_main.cpp
// ---------------------------------------------------------------------------

extern int   m_sintab[];
extern int   m_sintab_size;
extern void  EnterCtrlConfig();


// ---------------------------------------------------------------------------
// state variables
// ---------------------------------------------------------------------------

static int          ctrlcfg_tab         = CTRLTAB_KEYBOARD;
static int          ctrlcfg_selected    = 0;
static int          ctrlcfg_scroll      = 0;    // first visible row (keyboard tab)
static bool         ctrlcfg_waiting     = false;
static bool         ctrlcfg_layer2      = false; // keyboard: capturing layer 2?

// Input-capture snapshots (must be declared before any function that touches them)
#define CC_KEY_SNAP_MAX 512
static Uint8  cc_key_snap[ CC_KEY_SNAP_MAX ];
static int    cc_key_snap_valid   = 0;
static Uint32 cc_mouse_snap       = 0;
static int    cc_mouse_snap_valid = 0;
static byte   cc_joy_snap[ 40 ];   // snapshot of JoyState.Buttons[] at wait-start
static int    cc_joy_snap_valid   = 0;

// number of per-axis deadzone rows appended after the binding list
#define NUM_JOY_DEADZONE_ROWS 4

static int          ctrlcfg_slidepos    = CTRLCFG_POS_RIGHT;
static int          ctrlcfg_slidetarget = CTRLCFG_POS_RIGHT;
static refframe_t   ctrlcfg_lastref     = REFFRAME_INVALID;

// cached metrics (computed each frame)
static int  cc_text_x      = 0;
static int  cc_text_y_base = 0;
static int  cc_width       = 0;
static int  cc_height      = 0;
static int  cc_chwidth     = 0;
static int  cc_chheight    = 0;
static int  cc_panel_x     = 0;
static int  cc_panel_y     = 0;
static int  cc_panel_w     = 0;
static int  cc_panel_h     = 0;
static bool cc_metrics_valid = false;

// paste buffer
#define CC_PASTE_LEN 511
static char cc_paste[ CC_PASTE_LEN + 1 ];


// (joystick diagram removed — binding list uses the full panel width)


// ---------------------------------------------------------------------------
// slide animation — mirrors DoOptionsSliding() in m_option.cpp exactly
// ---------------------------------------------------------------------------

static void DoCtrlCfgSliding()
{
    if ( ctrlcfg_slidepos == ctrlcfg_slidetarget ) {
        ctrlcfg_lastref = REFFRAME_INVALID;
        return;
    }

    refframe_t rfcount = SYSs_GetRefFrameCount();

    if ( ctrlcfg_slidepos < ctrlcfg_slidetarget ) {
        if ( ctrlcfg_lastref == REFFRAME_INVALID ) {
            ctrlcfg_lastref = rfcount;
        } else {
            refframe_t delta = rfcount - ctrlcfg_lastref;
            for ( ; delta >= CTRLCFG_SLIDE_SPEED; delta -= CTRLCFG_SLIDE_SPEED ) {
                ctrlcfg_slidepos++;
                if ( ctrlcfg_slidepos >= ctrlcfg_slidetarget ) {
                    ctrlcfg_slidepos = ctrlcfg_slidetarget;
                    ctrlcfg_lastref  = REFFRAME_INVALID;
                    break;
                }
                ctrlcfg_lastref += CTRLCFG_SLIDE_SPEED;
            }
        }
    } else {
        if ( ctrlcfg_lastref == REFFRAME_INVALID ) {
            ctrlcfg_lastref = rfcount;
        } else {
            refframe_t delta = rfcount - ctrlcfg_lastref;
            for ( ; delta >= CTRLCFG_SLIDE_SPEED; delta -= CTRLCFG_SLIDE_SPEED ) {
                ctrlcfg_slidepos--;
                if ( ctrlcfg_slidepos <= ctrlcfg_slidetarget ) {
                    ctrlcfg_slidepos = ctrlcfg_slidetarget;
                    ctrlcfg_lastref  = REFFRAME_INVALID;
                    break;
                }
                ctrlcfg_lastref += CTRLCFG_SLIDE_SPEED;
            }
        }
    }
}


// ---------------------------------------------------------------------------
// public slide interface
// ---------------------------------------------------------------------------

void MoveInCtrlConfig()
{
    ctrlcfg_slidepos    = CTRLCFG_POS_LEFT;
    ctrlcfg_slidetarget = CTRLCFG_POS_LEFT;
}

void MoveOutCtrlConfig()
{
    ctrlcfg_slidepos    = CTRLCFG_POS_RIGHT;
    ctrlcfg_slidetarget = CTRLCFG_POS_RIGHT;
}

void SlideInCtrlConfig()
{
    ctrlcfg_slidetarget = CTRLCFG_POS_LEFT;
}

void SlideOutCtrlConfig()
{
    ctrlcfg_slidetarget = CTRLCFG_POS_RIGHT;
}

int SlideFinishedCtrlConfig()
{
    return ( ctrlcfg_slidepos == ctrlcfg_slidetarget );
}


// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------

void InitCtrlConfig()
{
    ctrlcfg_tab      = CTRLTAB_KEYBOARD;
    ctrlcfg_selected = 0;
    ctrlcfg_scroll   = 0;
    ctrlcfg_waiting  = false;
    ctrlcfg_layer2   = false;
    cc_metrics_valid = false;
}


// ---------------------------------------------------------------------------
// helper: draw a thin divider line
// ---------------------------------------------------------------------------

static void CC_DrawDivider( WSFP wstrfp, int lx, int ly, int len )
{
    int dpos;
    for ( dpos = 0; dpos < len && dpos < CC_PASTE_LEN; dpos++ )
        cc_paste[ dpos ] = '-';
    cc_paste[ dpos ] = 0;
    wstrfp( cc_paste, lx, ly, TRTAB_PANELTEXT );
}


// ---------------------------------------------------------------------------
// helper: draw a solid colored rectangle
// ---------------------------------------------------------------------------

static void CC_DrawRect( int x, int y, int w, int h, int r, int g, int b, int a )
{
    colscreenrect_s rect;
    rect.x          = x;
    rect.y          = y;
    rect.w          = w;
    rect.h          = h;
    rect.itertype   = iter_rgba | iter_specularadd;
    rect.color.R    = (byte)r;
    rect.color.G    = (byte)g;
    rect.color.B    = (byte)b;
    rect.color.A    = (byte)a;
    DRAW_ColoredScreenRect( &rect );
}


// ---------------------------------------------------------------------------
// helper: compute and cache screen metrics for the panel
// ---------------------------------------------------------------------------

static void CalcCtrlCfgMetrics()
{
    extern int hud_line_dist;

    int chwidth  = CharsetInfo[ HUD_CHARSETNO ].width;
    int chheight = hud_line_dist;

    D_SetWStrContext( CharsetInfo[ HUD_CHARSETNO ].charsetpointer,
                     CharsetInfo[ HUD_CHARSETNO ].geompointer,
                     NULL,
                     CharsetInfo[ HUD_CHARSETNO ].width,
                     CharsetInfo[ HUD_CHARSETNO ].height );

    // Panel covers ~70% of screen width and most of screen height
    int panel_w = (int)( Screen_Width  * 0.72f );
    int panel_h = (int)( Screen_Height * 0.80f );

    // Slide offset: panel starts off-screen to the right, slides left
    int slide_offset = 0;
    if ( ctrlcfg_slidepos != CTRLCFG_POS_LEFT )
        slide_offset = m_sintab[ ctrlcfg_slidepos ];

    int panel_x = Screen_Width - panel_w - 20 + slide_offset;
    int panel_y = (int)( Screen_Height * 0.08f );

    cc_chwidth     = chwidth;
    cc_chheight    = chheight;
    cc_panel_x     = panel_x;
    cc_panel_y     = panel_y;
    cc_panel_w     = panel_w;
    cc_panel_h     = panel_h;
    cc_text_x      = panel_x + 2 * chwidth;
    cc_text_y_base = panel_y + 2 * chheight;
    cc_width       = panel_w / chwidth - 4;   // content width in chars
    cc_height      = panel_h / chheight - 4;  // content height in rows
    cc_metrics_valid = true;
}


// ---------------------------------------------------------------------------
// helper: clamp-write a string without overflow
// ---------------------------------------------------------------------------

static void CC_WriteStr( WSFP wstrfp, const char *s, int x, int y )
{
    strncpy( cc_paste, s, CC_PASTE_LEN );
    cc_paste[ CC_PASTE_LEN ] = 0;
    wstrfp( cc_paste, x, y, TRTAB_PANELTEXT );
}


// ---------------------------------------------------------------------------
// helper: highlight-write a string (write twice = brighter)
// ---------------------------------------------------------------------------

static void CC_WriteStrHL( WSFP wstrfp, const char *s, int x, int y )
{
    strncpy( cc_paste, s, CC_PASTE_LEN );
    cc_paste[ CC_PASTE_LEN ] = 0;
    wstrfp( cc_paste, x, y, TRTAB_PANELTEXT );
    wstrfp( cc_paste, x, y, TRTAB_PANELTEXT );
}


// ---------------------------------------------------------------------------
// draw tab bar at the top of the panel
// ---------------------------------------------------------------------------

static void DrawCtrlConfigTabs( WSFP wstrfp )
{
    static const char *tab_labels[] = { "[ KEYBOARD ]", "[ MOUSE ]", "[ JOYSTICK ]" };

    int x = cc_text_x;
    int y = cc_text_y_base;

    // When tab bar itself is focused (selected==-1), draw a highlight bar
    // behind all the tab labels so the user sees focus has moved up.
    if ( ctrlcfg_selected == -1 ) {
        int total_w = 0;
        for ( int t = 0; t < NUM_CTRLTABS; t++ )
            total_w += (int)( strlen( tab_labels[ t ] ) + 2 ) * cc_chwidth;
        CC_DrawRect( x - cc_chwidth, y - 1, total_w + cc_chwidth, cc_chheight,
                     0, 60, 60, 60 );
    }

    for ( int t = 0; t < NUM_CTRLTABS; t++ ) {
        bool is_active = ( t == ctrlcfg_tab );
        if ( is_active )
            CC_WriteStrHL( wstrfp, tab_labels[ t ], x, y );
        else
            CC_WriteStr( wstrfp, tab_labels[ t ], x, y );
        x += (int)( strlen( tab_labels[ t ] ) + 2 ) * cc_chwidth;
    }
}


// ---------------------------------------------------------------------------
// keyboard tab
// ---------------------------------------------------------------------------

// Returns displayable name for a game function index (0-based)
static const char *GameFuncName( int idx )
{
    // functional_keys[0] is "func_" signature; actual names start at index 1
    extern const char *const functional_keys[];
    return functional_keys[ idx + 1 ];
}

// Number of keyboard game functions
static int NumGameFuncKeys()
{
    return (int)NUM_GAMEFUNC_KEYS;
}

// Layer 1 key code for function idx
static dword KeyLayer1( int idx )
{
    return ((dword *)KeyAssignments)[ idx ];
}

// Layer 2 key code for function idx
static dword KeyLayer2( int idx )
{
    return ((dword *)&KeyAssignments[ 1 ])[ idx ];
}

static void DrawKeyboardTab( WSFP wstrfp )
{
    int content_x  = cc_text_x;
    int content_y  = cc_text_y_base + cc_chheight * 3; // below tabs + divider
    int col2_x     = content_x + cc_chwidth * 20;
    int col3_x     = content_x + cc_chwidth * 36;
    int nfunc      = NumGameFuncKeys();
    int visible    = cc_height - 5;
    if ( visible < 4 ) visible = 4;

    // Clamp scroll
    if ( ctrlcfg_scroll > nfunc - visible )
        ctrlcfg_scroll = nfunc - visible;
    if ( ctrlcfg_scroll < 0 )
        ctrlcfg_scroll = 0;

    // Column headers
    CC_WriteStr( wstrfp, "FUNCTION", content_x, content_y );
    CC_WriteStr( wstrfp, "LAYER 1",  col2_x,    content_y );
    CC_WriteStr( wstrfp, "LAYER 2",  col3_x,    content_y );
    content_y += cc_chheight;
    CC_DrawDivider( wstrfp, content_x, content_y, cc_width );
    content_y += cc_chheight;

    for ( int i = ctrlcfg_scroll; i < nfunc && i < ctrlcfg_scroll + visible; i++ ) {
        bool selected = ( ctrlcfg_selected == i );
        bool waiting  = ( ctrlcfg_waiting && selected );

        const char *fname = GameFuncName( i );
        const char *k1    = GetMKCNameForCode( KeyLayer1( i ) );
        const char *k2    = GetMKCNameForCode( KeyLayer2( i ) );

        // Highlight bar behind selected row
        if ( selected ) {
            CC_DrawRect( content_x - cc_chwidth,
                         content_y - 1,
                         cc_width * cc_chwidth + cc_chwidth,
                         cc_chheight,
                         0, 80, 80, 80 );
        }

        if ( waiting ) {
            CC_WriteStrHL( wstrfp, fname,   content_x, content_y );
            CC_WriteStrHL( wstrfp, ctrlcfg_layer2 ? "PRESS KEY (L2)..." : "PRESS KEY...", col2_x, content_y );
        } else if ( selected ) {
            CC_WriteStrHL( wstrfp, fname, content_x, content_y );
            CC_WriteStrHL( wstrfp, k1,    col2_x,    content_y );
            CC_WriteStrHL( wstrfp, k2,    col3_x,    content_y );
        } else {
            CC_WriteStr( wstrfp, fname, content_x, content_y );
            CC_WriteStr( wstrfp, k1,    col2_x,    content_y );
            CC_WriteStr( wstrfp, k2,    col3_x,    content_y );
        }

        content_y += cc_chheight;
    }

    // Scroll indicators
    if ( ctrlcfg_scroll > 0 )
        CC_WriteStr( wstrfp, "^ more ^", cc_text_x, cc_text_y_base + 3 * cc_chheight );
    if ( ctrlcfg_scroll + visible < nfunc ) {
        int ind_y = cc_panel_y + cc_panel_h - 3 * cc_chheight;
        CC_WriteStr( wstrfp, "v more v", cc_text_x, ind_y );
    }
}


// ---------------------------------------------------------------------------
// mouse tab
// ---------------------------------------------------------------------------

static const char *MouseButtonName( int btn )
{
    switch ( btn ) {
        case MOUSE_BUTTON_LEFT:        return "Left";
        case MOUSE_BUTTON_MIDDLE:      return "Middle";
        case MOUSE_BUTTON_RIGHT:       return "Right";
        case MOUSE_BUTTON_X1:          return "X1";
        case MOUSE_BUTTON_X2:          return "X2";
        case MOUSE_BUTTON_SCROLL_UP:   return "Scroll+";
        case MOUSE_BUTTON_SCROLL_DOWN: return "Scroll-";
        default:                       return "---";
    }
}

static const char *MouseRowLabel( int row )
{
    switch ( row ) {
        case MROW_LBUTTON:          return "  Fire Gun:";
        case MROW_RBUTTON:          return "  Fire Missile:";
        case MROW_TARGET_BTN:       return "  Target:";
        case MROW_NEXTGUN_BTN:      return "  Next Gun:";
        case MROW_NEXTMISSILE_BTN:  return "  Next Missile:";
        case MROW_ACCEL_BTN:        return "  Accelerate:";
        case MROW_DECEL_BTN:        return "  Decelerate:";
        case MROW_INVERT:           return "  Invert Y-Axis:";
        case MROW_SENSITIVITY:      return "  Sensitivity:";
        case MROW_DRIFT:            return "  Centering Speed:";
        default:                    return "";
    }
}

static void MouseRowValue( int row, char *out )
{
    switch ( row ) {
        case MROW_LBUTTON:          strcpy( out, MouseButtonName( inp_mouse_gun_button ) );         break;
        case MROW_RBUTTON:          strcpy( out, MouseButtonName( inp_mouse_missile_button ) );     break;
        case MROW_TARGET_BTN:       strcpy( out, MouseButtonName( inp_mouse_target_button ) );      break;
        case MROW_NEXTGUN_BTN:      strcpy( out, MouseButtonName( inp_mouse_nextgun_button ) );     break;
        case MROW_NEXTMISSILE_BTN:  strcpy( out, MouseButtonName( inp_mouse_nextmissile_button ) ); break;
        case MROW_ACCEL_BTN:        strcpy( out, MouseButtonName( inp_mouse_accel_button ) );       break;
        case MROW_DECEL_BTN:        strcpy( out, MouseButtonName( inp_mouse_decel_button ) );       break;
        case MROW_INVERT:           strcpy( out, inp_mouse_invert_yaxis ? "ON " : "OFF" );          break;
        case MROW_SENSITIVITY:      sprintf( out, "%d", inp_mouse_sensitivity );                    break;
        case MROW_DRIFT:            sprintf( out, "%d", inp_mouse_drift );                          break;
        default:                    strcpy( out, "" );                                              break;
    }
}

static void DrawMouseTab( WSFP wstrfp )
{
    int col1_x = cc_text_x;
    int col2_x = cc_text_x + cc_chwidth * 22;
    int y      = cc_text_y_base + cc_chheight * 3;

    // ---- Button assignment rows (remappable, MROW_LBUTTON..MROW_DECEL_BTN) ----
    CC_WriteStr( wstrfp, "BUTTON ASSIGNMENTS", col1_x, y );
    y += cc_chheight;
    CC_DrawDivider( wstrfp, col1_x, y, cc_width );
    y += cc_chheight;

    for ( int r = MROW_LBUTTON; r < MROW_INVERT; r++ ) {
        bool sel     = ( ctrlcfg_selected == r );
        bool waiting = ( ctrlcfg_waiting && sel );
        char valbuf[ 64 ];
        MouseRowValue( r, valbuf );

        if ( sel ) {
            CC_DrawRect( col1_x - cc_chwidth, y - 1,
                         cc_width * cc_chwidth + cc_chwidth, cc_chheight,
                         0, 80, 80, 80 );
            CC_WriteStrHL( wstrfp, MouseRowLabel( r ), col1_x, y );
            if ( waiting )
                CC_WriteStrHL( wstrfp, "CLICK A BUTTON...", col2_x, y );
            else
                CC_WriteStrHL( wstrfp, valbuf, col2_x, y );
        } else {
            CC_WriteStr( wstrfp, MouseRowLabel( r ), col1_x, y );
            CC_WriteStr( wstrfp, valbuf,             col2_x, y );
        }
        y += cc_chheight;
    }

    // ---- Settings rows (adjust with ←→, MROW_INVERT..MROW_COUNT-1) ----
    y += cc_chheight;
    CC_WriteStr( wstrfp, "SETTINGS", col1_x, y );
    y += cc_chheight;
    CC_DrawDivider( wstrfp, col1_x, y, cc_width );
    y += cc_chheight;

    for ( int r = MROW_INVERT; r < MROW_COUNT; r++ ) {
        bool sel = ( ctrlcfg_selected == r );
        char valbuf[ 64 ];
        MouseRowValue( r, valbuf );

        if ( sel ) {
            CC_DrawRect( col1_x - cc_chwidth, y - 1,
                         cc_width * cc_chwidth + cc_chwidth, cc_chheight,
                         0, 80, 80, 80 );
            CC_WriteStrHL( wstrfp, MouseRowLabel( r ), col1_x, y );
            snprintf( cc_paste, CC_PASTE_LEN, "< %s >", valbuf );
            CC_WriteStrHL( wstrfp, cc_paste, col2_x, y );
        } else {
            CC_WriteStr( wstrfp, MouseRowLabel( r ), col1_x, y );
            CC_WriteStr( wstrfp, valbuf,             col2_x, y );
        }
        y += cc_chheight;
    }
}


// ---------------------------------------------------------------------------
// joystick action list
// ---------------------------------------------------------------------------

static void DrawJoystickList( WSFP wstrfp, int list_x, int list_y, int list_w )
{
    joybinding_s *bindings = ISDL_GetBindingTable();
    int nbind    = ISDL_GetBindingCount();
    int totalrows = nbind + NUM_JOY_DEADZONE_ROWS;
    int visible  = cc_height - 5;
    if ( visible < 4 ) visible = 4;

    // Scroll so selected row is visible (only when a list row is focused)
    if ( ctrlcfg_selected >= 0 ) {
        if ( ctrlcfg_selected < ctrlcfg_scroll )
            ctrlcfg_scroll = ctrlcfg_selected;
        if ( ctrlcfg_selected >= ctrlcfg_scroll + visible )
            ctrlcfg_scroll = ctrlcfg_selected - visible + 1;
    }
    if ( ctrlcfg_scroll < 0 )
        ctrlcfg_scroll = 0;
    if ( ctrlcfg_scroll > totalrows - visible )
        ctrlcfg_scroll = totalrows - visible;
    if ( ctrlcfg_scroll < 0 )
        ctrlcfg_scroll = 0;

    int col2_x = list_x + (int)( list_w * 0.55f );
    int y      = list_y;

    // Headers
    CC_WriteStr( wstrfp, "ACTION",  list_x, y );
    CC_WriteStr( wstrfp, "BUTTON",  col2_x, y );
    y += cc_chheight;
    CC_DrawDivider( wstrfp, list_x, y, list_w / cc_chwidth );
    y += cc_chheight;

    // ---- Binding rows ----
    for ( int i = ctrlcfg_scroll; i < nbind && i < ctrlcfg_scroll + visible; i++ ) {
        bool sel     = ( ctrlcfg_selected == i );
        bool waiting = ( ctrlcfg_waiting && sel );

        char valbuf[ 32 ];
        if ( bindings[ i ].is_axis ) {
            snprintf( valbuf, sizeof(valbuf), "AXIS %d", *bindings[ i ].var );
        } else if ( *bindings[ i ].var < 0 ) {
            strcpy( valbuf, "---" );
        } else if ( *bindings[ i ].var >= JOYBTN_AXIS_OFFSET ) {
            snprintf( valbuf, sizeof(valbuf), "AX %d+", *bindings[ i ].var - JOYBTN_AXIS_OFFSET );
        } else {
            snprintf( valbuf, sizeof(valbuf), "BTN %d", *bindings[ i ].var );
        }

        if ( sel ) {
            CC_DrawRect( list_x - cc_chwidth, y - 1,
                         list_w + cc_chwidth, cc_chheight,
                         0, 80, 80, 80 );
            if ( waiting && !bindings[ i ].is_axis ) {
                CC_WriteStrHL( wstrfp, bindings[ i ].display_name, list_x, y );
                CC_WriteStrHL( wstrfp, "PRESS BTN/TRIG...",        col2_x, y );
            } else {
                CC_WriteStrHL( wstrfp, bindings[ i ].display_name, list_x, y );
                CC_WriteStrHL( wstrfp, valbuf,                     col2_x, y );
            }
        } else {
            CC_WriteStr( wstrfp, bindings[ i ].display_name, list_x, y );
            CC_WriteStr( wstrfp, valbuf,                     col2_x, y );
        }

        y += cc_chheight;
    }

    // ---- Deadzone rows (appended after binding rows) ----
    for ( int dz = 0; dz < NUM_JOY_DEADZONE_ROWS; dz++ ) {
        int abs_row = nbind + dz;
        int rel     = abs_row - ctrlcfg_scroll;
        if ( rel < 0 || rel >= visible ) continue;

        bool sel = ( ctrlcfg_selected == abs_row );

        char lbl[ 40 ];
        char val[ 64 ];
        snprintf( lbl, sizeof(lbl), "Deadzone Axis %d:", dz );
        int dzval  = ISDL_GetDeadzone( dz );
        int rawval = ISDL_GetRawAxis( dz );
        if ( sel )
            snprintf( val, sizeof(val), "< %d >  raw:%d", dzval, rawval );
        else
            snprintf( val, sizeof(val), "%d  raw:%d", dzval, rawval );

        if ( sel ) {
            CC_DrawRect( list_x - cc_chwidth, y - 1,
                         list_w + cc_chwidth, cc_chheight,
                         0, 80, 80, 80 );
            CC_WriteStrHL( wstrfp, lbl, list_x, y );
            CC_WriteStrHL( wstrfp, val, col2_x, y );
        } else {
            CC_WriteStr( wstrfp, lbl, list_x, y );
            CC_WriteStr( wstrfp, val, col2_x, y );
        }
        y += cc_chheight;
    }
}


static void DrawJoystickTab( WSFP wstrfp )
{
    if ( !ISDL_JoyAvailable() ) {
        int y = cc_text_y_base + cc_chheight * 4;
        CC_WriteStr( wstrfp, "  No joystick detected.", cc_text_x, y );
        return;
    }

    int content_top = cc_text_y_base + cc_chheight * 3;
    int list_x = cc_text_x;
    int list_w = cc_panel_w - cc_chwidth * 4;

    DrawJoystickList( wstrfp, list_x, content_top, list_w );
}


// ---------------------------------------------------------------------------
// hint line at panel bottom
// ---------------------------------------------------------------------------

static void DrawCtrlCfgHints( WSFP wstrfp )
{
    int y = cc_panel_y + cc_panel_h - 2 * cc_chheight;

    if ( ctrlcfg_waiting ) {
        if ( ctrlcfg_tab == CTRLTAB_KEYBOARD )
            CC_WriteStr( wstrfp, "  ESC to cancel remap", cc_text_x, y );
        else if ( ctrlcfg_tab == CTRLTAB_MOUSE )
            CC_WriteStr( wstrfp, "  ESC to cancel  /  click any mouse button to assign", cc_text_x, y );
        else
            CC_WriteStr( wstrfp, "  ESC to cancel  /  press any gamepad button to assign", cc_text_x, y );
    } else {
        switch ( ctrlcfg_tab ) {
            case CTRLTAB_KEYBOARD:
                CC_WriteStr( wstrfp, "  [Up/Down] Nav  [Enter] Remap L1  [Shift+Enter] L2  [Del] Clear  [R] Defaults  [Esc] Back", cc_text_x, y );
                break;
            case CTRLTAB_MOUSE:
                CC_WriteStr( wstrfp, "  [Up/Down] Nav  [Enter] Remap button  [Left/Right] Adjust  [Del] Reset  [R] Defaults  [Esc] Back", cc_text_x, y );
                break;
            case CTRLTAB_JOYSTICK:
                if ( ctrlcfg_selected >= ISDL_GetBindingCount() )
                    CC_WriteStr( wstrfp, "  [Left/Right] Adjust deadzone  [Del] Reset  [R] Defaults  [Esc] Back", cc_text_x, y );
                else
                    CC_WriteStr( wstrfp, "  [Up/Down] Nav  [Enter] Remap  [Del] Clear  [R] Defaults  [Esc] Back", cc_text_x, y );
                break;
        }
    }
}


// ---------------------------------------------------------------------------
// main draw entry point
// ---------------------------------------------------------------------------

void DrawCtrlConfigScreen()
{
    DoCtrlCfgSliding();
    CalcCtrlCfgMetrics();

    if ( AUX_DISABLE_FLOATING_MENU_DRAWING )
        return;

    WSFP wstrfp = D_WriteTrString;

    // Background panel
    DRAW_ClippedTrRect( cc_panel_x, cc_panel_y, cc_panel_w, cc_panel_h, TRTAB_PANELBACK );

    // Caption
    int cap_x = cc_text_x + (int)( ( cc_width - (int)strlen( CTRLCFG_CAPTION_STR ) ) / 2 ) * cc_chwidth;
    wstrfp( (char*)CTRLCFG_CAPTION_STR, cap_x, cc_text_y_base, TRTAB_PANELTEXT );

    int divider_y = cc_text_y_base + cc_chheight;
    CC_DrawDivider( wstrfp, cc_text_x, divider_y, cc_width );

    // Tab bar
    DrawCtrlConfigTabs( wstrfp );

    // Tab content
    switch ( ctrlcfg_tab ) {
        case CTRLTAB_KEYBOARD:  DrawKeyboardTab( wstrfp );  break;
        case CTRLTAB_MOUSE:     DrawMouseTab( wstrfp );     break;
        case CTRLTAB_JOYSTICK:  DrawJoystickTab( wstrfp );  break;
    }

    // Hint line
    DrawCtrlCfgHints( wstrfp );
}


// ---------------------------------------------------------------------------
// navigation
// ---------------------------------------------------------------------------

// Row count per tab
static int TabRowCount()
{
    switch ( ctrlcfg_tab ) {
        case CTRLTAB_KEYBOARD:  return NumGameFuncKeys();
        case CTRLTAB_MOUSE:     return MROW_COUNT; // all 5 rows navigable
        case CTRLTAB_JOYSTICK:  return ISDL_GetBindingCount() + NUM_JOY_DEADZONE_ROWS;
        default:                return 0;
    }
}

void CtrlCfgCursorUp()
{
    if ( ctrlcfg_waiting ) return;
    if ( ctrlcfg_selected > 0 ) {
        --ctrlcfg_selected;
    } else if ( ctrlcfg_selected == 0 ) {
        ctrlcfg_selected = -1;  // move up to tab bar
    } else {
        // already at tab bar (-1): wrap to last row
        ctrlcfg_selected = TabRowCount() - 1;
    }
    AUD_Select2();
}

void CtrlCfgCursorDown()
{
    if ( ctrlcfg_waiting ) return;
    if ( ctrlcfg_selected == -1 ) {
        ctrlcfg_selected = 0;   // move down from tab bar to first row
    } else if ( ++ctrlcfg_selected >= TabRowCount() ) {
        ctrlcfg_selected = 0;   // wrap back to top (skip tab bar on wrap)
    }
    AUD_Select2();
}

void CtrlCfgLeft()
{
    if ( ctrlcfg_waiting ) return;

    // Tab-bar focus: switch tab left
    if ( ctrlcfg_selected == -1 ) {
        if ( --ctrlcfg_tab < 0 ) ctrlcfg_tab = NUM_CTRLTABS - 1;
        ctrlcfg_scroll = 0;
        AUD_Select2();
        return;
    }

    if ( ctrlcfg_tab == CTRLTAB_MOUSE ) {
        // Only settings rows are adjustable with ←→
        switch ( ctrlcfg_selected ) {
            case MROW_INVERT:
                inp_mouse_invert_yaxis ^= 1;
                AUD_Select2();
                break;
            case MROW_SENSITIVITY:
                inp_mouse_sensitivity -= 100;
                if ( inp_mouse_sensitivity < 0 ) inp_mouse_sensitivity = 0;
                AUD_Select2();
                break;
            case MROW_DRIFT:
                inp_mouse_drift -= 5;
                if ( inp_mouse_drift < 0 ) inp_mouse_drift = 0;
                AUD_Select2();
                break;
            default:
                break; // button rows — ←→ does nothing (use Enter to remap)
        }
    } else if ( ctrlcfg_tab == CTRLTAB_JOYSTICK ) {
        int nbind = ISDL_GetBindingCount();
        if ( ctrlcfg_selected >= nbind ) {
            // Deadzone row
            int dz_ax = ctrlcfg_selected - nbind;
            int cur   = ISDL_GetDeadzone( dz_ax );
            int next  = cur - 1000;
            if ( next < 0 ) next = 0;
            ISDL_SetDeadzone( dz_ax, next );
            AUD_Select2();
        } else {
            joybinding_s *b = &ISDL_GetBindingTable()[ ctrlcfg_selected ];
            if ( b->is_axis ) {
                (*b->var)--;
                if ( *b->var < 0 ) *b->var = 7;
                AUD_Select2();
            }
        }
    } else {
        // Keyboard tab: switch tab left from any row
        if ( --ctrlcfg_tab < 0 ) ctrlcfg_tab = NUM_CTRLTABS - 1;
        ctrlcfg_selected = 0;
        ctrlcfg_scroll   = 0;
        AUD_Select2();
    }
}

void CtrlCfgRight()
{
    if ( ctrlcfg_waiting ) return;

    // Tab-bar focus: switch tab right
    if ( ctrlcfg_selected == -1 ) {
        if ( ++ctrlcfg_tab >= NUM_CTRLTABS ) ctrlcfg_tab = 0;
        ctrlcfg_scroll = 0;
        AUD_Select2();
        return;
    }

    if ( ctrlcfg_tab == CTRLTAB_MOUSE ) {
        switch ( ctrlcfg_selected ) {
            case MROW_INVERT:
                inp_mouse_invert_yaxis ^= 1;
                AUD_Select2();
                break;
            case MROW_SENSITIVITY:
                inp_mouse_sensitivity += 100;
                if ( inp_mouse_sensitivity > 2000 ) inp_mouse_sensitivity = 2000;
                AUD_Select2();
                break;
            case MROW_DRIFT:
                inp_mouse_drift += 5;
                if ( inp_mouse_drift > 100 ) inp_mouse_drift = 100;
                AUD_Select2();
                break;
            default:
                break; // button rows — ←→ does nothing
        }
    } else if ( ctrlcfg_tab == CTRLTAB_JOYSTICK ) {
        int nbind = ISDL_GetBindingCount();
        if ( ctrlcfg_selected >= nbind ) {
            // Deadzone row
            int dz_ax = ctrlcfg_selected - nbind;
            int cur   = ISDL_GetDeadzone( dz_ax );
            int next  = cur + 1000;
            if ( next > 30000 ) next = 30000;
            ISDL_SetDeadzone( dz_ax, next );
            AUD_Select2();
        } else {
            joybinding_s *b = &ISDL_GetBindingTable()[ ctrlcfg_selected ];
            if ( b->is_axis ) {
                (*b->var) = ( *b->var + 1 ) % 8;
                AUD_Select2();
            }
        }
    } else {
        // Keyboard tab: switch tab right from any row
        if ( ++ctrlcfg_tab >= NUM_CTRLTABS ) ctrlcfg_tab = 0;
        ctrlcfg_selected = 0;
        ctrlcfg_scroll   = 0;
        AUD_Select2();
    }
}

void CtrlCfgSelect()
{
    if ( ctrlcfg_selected == -1 ) return; // tab bar focused — Enter does nothing

    if ( ctrlcfg_tab == CTRLTAB_KEYBOARD ) {
        ctrlcfg_waiting = true;
        ctrlcfg_layer2  = false;
        cc_key_snap_valid = 0;
        AUD_Select2();
    } else if ( ctrlcfg_tab == CTRLTAB_MOUSE ) {
        // Button assignment rows (< MROW_INVERT) are remappable; settings use ←→
        if ( ctrlcfg_selected < MROW_INVERT ) {
            ctrlcfg_waiting     = true;
            cc_mouse_snap_valid = 0;
            AUD_Select2();
        }
    } else if ( ctrlcfg_tab == CTRLTAB_JOYSTICK ) {
        // Deadzone rows don't trigger wait mode
        if ( ctrlcfg_selected >= ISDL_GetBindingCount() ) return;
        joybinding_s *b = &ISDL_GetBindingTable()[ ctrlcfg_selected ];
        if ( !b->is_axis ) {
            // Snapshot all currently-held buttons/triggers so we don't
            // immediately re-capture whatever is held right now.
            ISDL_SnapshotButtons( cc_joy_snap, 40 );
            cc_joy_snap_valid = 1;
            ctrlcfg_waiting   = true;
            AUD_Select2();
        }
    }
}

void CtrlCfgSelectLayer2()
{
    if ( ctrlcfg_selected == -1 ) return;
    if ( ctrlcfg_tab == CTRLTAB_KEYBOARD ) {
        ctrlcfg_waiting   = true;
        ctrlcfg_layer2    = true;
        cc_key_snap_valid = 0;
        AUD_Select2();
    }
}

void CtrlCfgClearBinding()
{
    if ( ctrlcfg_waiting ) {
        ctrlcfg_waiting      = false;
        ctrlcfg_layer2       = false;
        cc_key_snap_valid    = 0;
        cc_mouse_snap_valid  = 0;
        cc_joy_snap_valid    = 0;
        return;
    }

    if ( ctrlcfg_selected == -1 ) return; // tab bar focused — Del does nothing

    if ( ctrlcfg_tab == CTRLTAB_KEYBOARD ) {
        if ( ctrlcfg_layer2 )
            ((dword *)&KeyAssignments[ 1 ])[ ctrlcfg_selected ] = MKC_NIL;
        else
            ((dword *)KeyAssignments)[ ctrlcfg_selected ] = MKC_NIL;
        AUD_Select2();
    } else if ( ctrlcfg_tab == CTRLTAB_MOUSE ) {
        // Button assignment rows: reset to default (gun/missile have hardware defaults; rest unbind)
        switch ( ctrlcfg_selected ) {
            case MROW_LBUTTON:         inp_mouse_gun_button         = MOUSE_BUTTON_LEFT;  break;
            case MROW_RBUTTON:         inp_mouse_missile_button     = MOUSE_BUTTON_RIGHT; break;
            case MROW_TARGET_BTN:      inp_mouse_target_button      = -1;                 break;
            case MROW_NEXTGUN_BTN:     inp_mouse_nextgun_button     = -1;                 break;
            case MROW_NEXTMISSILE_BTN: inp_mouse_nextmissile_button = -1;                 break;
            case MROW_ACCEL_BTN:       inp_mouse_accel_button       = -1;                 break;
            case MROW_DECEL_BTN:       inp_mouse_decel_button       = -1;                 break;
            default: break;
        }
        AUD_Select2();
    } else if ( ctrlcfg_tab == CTRLTAB_JOYSTICK ) {
        cc_joy_snap_valid = 0;
        int nbind = ISDL_GetBindingCount();
        if ( ctrlcfg_selected >= nbind ) {
            // Deadzone row: reset to default
            ISDL_SetDeadzone( ctrlcfg_selected - nbind, ISDL_JOY_DEADZONE_DEFAULT );
        } else {
            // Binding row: clear the binding
            joybinding_s *b = &ISDL_GetBindingTable()[ ctrlcfg_selected ];
            if ( !b->is_axis )
                *b->var = -1;
        }
        AUD_Select2();
    }
}

void CtrlCfgResetDefaults()
{
    if ( ctrlcfg_waiting ) return;

    switch ( ctrlcfg_tab ) {
        case CTRLTAB_KEYBOARD:
            // Re-execute the default key bindings script
            ExecExternalFile( (char *)"_keys" );
            break;
        case CTRLTAB_MOUSE:
            inp_mouse_invert_yaxis      = 0;
            inp_mouse_sensitivity       = 100;
            inp_mouse_drift             = 10;
            inp_mouse_gun_button        = MOUSE_BUTTON_LEFT;
            inp_mouse_missile_button    = MOUSE_BUTTON_RIGHT;
            inp_mouse_target_button     = -1;
            inp_mouse_nextgun_button    = -1;
            inp_mouse_nextmissile_button= -1;
            inp_mouse_accel_button      = -1;
            inp_mouse_decel_button      = -1;
            break;
        case CTRLTAB_JOYSTICK:
            ISDL_ResetBindings();
            for ( int ax = 0; ax < NUM_JOY_DEADZONE_ROWS; ax++ )
                ISDL_SetDeadzone( ax, ISDL_JOY_DEADZONE_DEFAULT );
            break;
    }
    AUD_Select2();
}

void CtrlCfgExit()
{
    ctrlcfg_waiting = false;
    // Transition back is driven by m_main.cpp on ESC keypress
    extern void ExitCtrlConfig();
    ExitCtrlConfig();
}

// Check R (reset) and Del (clear) keys directly via SDL.
// Called each frame from FloatingMenuKeyHandler when ctrlcfg is visible.
// Uses a per-key "was-down" latch so each physical press fires exactly once.
void CtrlCfgHandleSpecialKeys()
{
    if ( ctrlcfg_waiting ) return;

    const Uint8 *ks = SDL_GetKeyboardState( NULL );

    static int r_was_down   = 0;
    static int del_was_down = 0;
    static int sh_was_down  = 0; // Shift+Enter for layer-2 remap

    int r_down   = ks[ SDL_SCANCODE_R ];
    int del_down = ks[ SDL_SCANCODE_DELETE ];
    int sh_down  = ks[ SDL_SCANCODE_LSHIFT ] || ks[ SDL_SCANCODE_RSHIFT ];
    int ret_down = ks[ SDL_SCANCODE_RETURN ] || ks[ SDL_SCANCODE_KP_ENTER ];

    if ( r_down && !r_was_down )
        CtrlCfgResetDefaults();

    if ( del_down && !del_was_down )
        CtrlCfgClearBinding();

    // Shift+Enter → remap layer 2 (keyboard tab only)
    if ( sh_down && ret_down && !sh_was_down )
        CtrlCfgSelectLayer2();

    r_was_down   = r_down;
    del_was_down = del_down;
    sh_was_down  = sh_down && ret_down;
}


// ---------------------------------------------------------------------------
// per-frame input capture
// ---------------------------------------------------------------------------

bool CtrlCfgWaitingForInput()
{
    return ctrlcfg_waiting;
}


// ---------------------------------------------------------------------------
// per-frame input poll — call from FloatingMenuKeyHandler while waiting
// ---------------------------------------------------------------------------

// (cc_key_snap*, cc_mouse_snap* are declared in the state variables block above)

static void CtrlCfgHandleMouseButtonPress( int button_idx )
{
    if ( !ctrlcfg_waiting ) return;
    if ( ctrlcfg_tab != CTRLTAB_MOUSE ) return;

    int *target_var = NULL;
    switch ( ctrlcfg_selected ) {
        case MROW_LBUTTON:          target_var = &inp_mouse_gun_button;         break;
        case MROW_RBUTTON:          target_var = &inp_mouse_missile_button;     break;
        case MROW_TARGET_BTN:       target_var = &inp_mouse_target_button;      break;
        case MROW_NEXTGUN_BTN:      target_var = &inp_mouse_nextgun_button;     break;
        case MROW_NEXTMISSILE_BTN:  target_var = &inp_mouse_nextmissile_button; break;
        case MROW_ACCEL_BTN:        target_var = &inp_mouse_accel_button;       break;
        case MROW_DECEL_BTN:        target_var = &inp_mouse_decel_button;       break;
        default: return;
    }

    *target_var          = button_idx;
    ctrlcfg_waiting      = false;
    cc_mouse_snap_valid  = 0;
    AUD_Select2();
}

void CtrlCfgPollInput()
{
    if ( !ctrlcfg_waiting ) {
        cc_key_snap_valid   = 0;
        cc_mouse_snap_valid = 0;
        cc_joy_snap_valid   = 0;
        return;
    }

    // --- Mouse tab: scan SDL mouse button state + virtual scroll buttons ---
    if ( ctrlcfg_tab == CTRLTAB_MOUSE ) {
        cc_key_snap_valid = 0;
        Uint32 mbstate = SDL_GetMouseState( NULL, NULL );
        if ( !cc_mouse_snap_valid ) {
            // Snapshot current physical-button state so we ignore the button
            // that triggered wait mode (e.g. Enter pressed on the row).
            cc_mouse_snap       = mbstate;
            cc_mouse_snap_valid = 1;
            return;
        }
        // Check physical buttons (SDL is 1-based; skip virtual scroll indices)
        for ( int b = 0; b < MOUSE_BUTTON_SCROLL_UP; b++ ) {
            Uint32 mask = SDL_BUTTON( b + 1 );
            if ( ( mbstate & mask ) && !( cc_mouse_snap & mask ) ) {
                CtrlCfgHandleMouseButtonPress( b );
                return;
            }
        }
        // Check virtual scroll-wheel buttons via accumulator.
        // inp_mouse_scroll_y is set by INPs_Collect() and valid for the whole frame.
        if ( inp_mouse_scroll_y > 0 ) {
            CtrlCfgHandleMouseButtonPress( MOUSE_BUTTON_SCROLL_UP );
            return;
        }
        if ( inp_mouse_scroll_y < 0 ) {
            CtrlCfgHandleMouseButtonPress( MOUSE_BUTTON_SCROLL_DOWN );
            return;
        }
        return;
    }

    // --- Joystick tab: scan physical + virtual axis-buttons ---
    if ( ctrlcfg_tab == CTRLTAB_JOYSTICK ) {
        cc_key_snap_valid   = 0;
        cc_mouse_snap_valid = 0;
        // ISDL_JoyScanButtons() bypasses QueryJoystick / Op_Joystick and also
        // scans virtual axis-button slots (triggers), so L2/R2 show up here.
        int hit = ISDL_JoyScanButtons();
        if ( hit >= 0 && hit < 40 ) {
            // cc_joy_snap was populated by CtrlCfgSelect() the moment wait
            // mode was entered.  Only accept inputs NOT held at that instant.
            if ( !cc_joy_snap[ hit ] ) {
                CtrlCfgHandleButtonPress( hit );
            }
        }
        return;
    }

    // --- Keyboard tab: scan SDL keyboard state ---
    if ( ctrlcfg_tab == CTRLTAB_KEYBOARD ) {
        cc_mouse_snap_valid = 0;
        int numkeys = 0;
        const Uint8 *ks = SDL_GetKeyboardState( &numkeys );
        if ( numkeys > CC_KEY_SNAP_MAX ) numkeys = CC_KEY_SNAP_MAX;

        if ( !cc_key_snap_valid ) {
            // First call after entering wait mode: snapshot what is already held
            // so we don't immediately capture those keys.
            memcpy( cc_key_snap, ks, (size_t)numkeys );
            cc_key_snap_valid = 1;
            return;
        }

        // Scan for a key that was NOT held when we entered waiting mode.
        // Skip Escape — it is routed through FloatingMenuKeyEscape() instead.
        for ( int sc = 0; sc < numkeys; sc++ ) {
            if ( ks[ sc ] && !cc_key_snap[ sc ] ) {
                SDL_Keycode kc = SDL_GetKeyFromScancode( (SDL_Scancode)sc );
                if ( kc != SDLK_UNKNOWN && kc != SDLK_ESCAPE ) {
                    CtrlCfgHandleKeyPress( (dword)kc );
                    cc_key_snap_valid = 0;
                    return;
                }
            }
        }
    }
}

void CtrlCfgHandleKeyPress( dword mkc_code )
{
    if ( !ctrlcfg_waiting ) return;
    if ( ctrlcfg_tab != CTRLTAB_KEYBOARD ) return;

    // Ignore modifier-only presses
    if ( mkc_code == MKC_LSHIFT || mkc_code == MKC_RSHIFT ||
         mkc_code == MKC_LCONTROL || mkc_code == MKC_RCONTROL ||
         mkc_code == MKC_LALT || mkc_code == MKC_RALT ) {
        return;
    }

    // ESC cancels
    if ( mkc_code == MKC_ESCAPE ) {
        ctrlcfg_waiting = false;
        ctrlcfg_layer2  = false;
        return;
    }

    if ( ctrlcfg_layer2 )
        ((dword *)&KeyAssignments[ 1 ])[ ctrlcfg_selected ] = mkc_code;
    else
        ((dword *)KeyAssignments)[ ctrlcfg_selected ] = mkc_code;

    ctrlcfg_waiting = false;
    ctrlcfg_layer2  = false;
    AUD_Select2();
}

void CtrlCfgHandleButtonPress( int button_idx )
{
    if ( !ctrlcfg_waiting ) return;
    if ( ctrlcfg_tab != CTRLTAB_JOYSTICK ) return;

    ISDL_SetBinding( ctrlcfg_selected, button_idx );
    ctrlcfg_waiting   = false;
    cc_joy_snap_valid = 0;
    AUD_Select2();
}


// ---------------------------------------------------------------------------
// mouse click hit testing
// ---------------------------------------------------------------------------

int MouseOverCtrlCfg( int mousex, int mousey )
{
    // No diagram hotspots any more — nothing to hover over
    (void)mousex; (void)mousey;
    return -1;
}

void CtrlCfgMouseClick( int mousex, int mousey )
{
    if ( !cc_metrics_valid ) return;
    if ( ctrlcfg_waiting ) return;

    if ( ctrlcfg_tab == CTRLTAB_JOYSTICK ) {
        // Joystick list occupies the full panel width now (no diagram)
        int list_x  = cc_text_x;
        int list_w  = cc_panel_w - cc_chwidth * 4;
        int list_y  = cc_text_y_base + cc_chheight * 5; // after headers (tab + divider + col header + divider)
        int nbind   = ISDL_GetBindingCount();
        int visible = cc_height - 5;

        int total_rows = nbind + NUM_JOY_DEADZONE_ROWS;
        if ( mousex >= list_x && mousex < list_x + list_w ) {
            int row_idx = ( mousey - list_y ) / cc_chheight;
            int abs_idx = ctrlcfg_scroll + row_idx;
            if ( abs_idx >= 0 && abs_idx < total_rows && row_idx >= 0 && row_idx < visible ) {
                if ( ctrlcfg_selected != abs_idx ) {
                    ctrlcfg_selected = abs_idx;
                    AUD_Select2();
                }
                // Binding rows trigger remap on click; deadzone rows just select
                if ( abs_idx < nbind )
                    CtrlCfgSelect();
            }
        }
    }
}
