/*
 * PARSEC - Utility Functions - client
 *
 * $Author: uberlinuxguy $ - $Date: 2004/09/26 03:43:40 $
 *
 * Orginally written by:
 *   Copyright (c) Clemens Beer        <cbx@parsec.org>   2002
 *   Copyright (c) Markus Hadwiger     <msh@parsec.org>   1996-2000
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */ 

// C library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

// compilation flags/debug support
#include "config.h"
#include "debug.h"

// general definitions
#include "general.h"
#include "objstruc.h"

// global externals
#include "globals.h"

// subsystem headers
#include "net_defs.h"
#include "vid_defs.h"

// drawing subsystem
#include "d_bmap.h"
#include "d_font.h"

// network code config
#include "net_conf.h"

// local module header
#include "net_util.h"

// proprietary module headers
#include "e_color.h"
#include "h_supp.h"
#include "net_swap.h"
#include "sys_bind.h"


// string constants -----------------------------------------------------------
//
static char unknown_str[]		= "unknown";
static char logged_in_str[]		= "players currently logged in";


//-----------------------------------------------------------------------------
// UTILITY FUNCTIONS FOR GAME CODE                                            -
//-----------------------------------------------------------------------------


// determine whether current protocol is peer to peer -------------------------
//
int NET_ProtocolPEER()
{
	return ( sys_BindType_PROTOCOL == BT_PROTOCOL_PEERTOPEER );
}


// determine whether current protocol is game server --------------------------
//
int NET_ProtocolGMSV()
{
	return ( sys_BindType_PROTOCOL == BT_PROTOCOL_GAMESERVER );
}


// determine whether currently connected in peer to peer game -----------------
//
int NET_ConnectedPEER()
{
	return ( NetConnected && NET_ProtocolPEER() );
}


// determine whether currently connected to game server -----------------------
//
int NET_ConnectedGMSV()
{
	return ( NetConnected && NET_ProtocolGMSV() );
}


// fetch pointer to name of remote player -------------------------------------
//
char *NET_FetchPlayerName( int playerid )
{
	//NOTE:
	// this function is declared in NET_SUBH.H

//	ASSERT( NetConnected );
	ASSERT( playerid >= 0 );
	ASSERT( playerid < MAX_NET_PROTO_PLAYERS );

	if ( ( (dword)playerid < (dword)MAX_NET_PROTO_PLAYERS ) && ( Player_Status[ playerid ] ) )
		return Player_Name[ playerid ];
	else
		return unknown_str;
}


// set name of local player ---------------------------------------------------
//
int NET_SetPlayerName( const char *name )
{
	ASSERT( name != NULL );

	//NOTE:
	// this function is declared in NET_SUBH.H and
	// used by CON_COM::CheckPlayerName().

	int rc = TRUE;

	// generate remote event that updates the other players to altered name
	if ( NetConnected )
		rc = NET_RmEvPlayerName( name );

	if ( rc ) {

		// store new name globally
		strcpy( LocalPlayerName, name );

		// copy local player name into list of network player names
		strcpy( Player_Name[ LocalPlayerId ], LocalPlayerName );
	}

	return rc;
}


// fetch pointer to ship of remote player -------------------------------------
//
ShipObject *NET_FetchOwnersShip( int ownerid )
{
	//NOTE:
	// this function is declared in NET_SUBH.H

	ASSERT( NetConnected );
	ASSERT( ownerid >= 0 );
	ASSERT( ownerid < MAX_NET_PROTO_PLAYERS );

	return (ShipObject *) Player_Ship[ ownerid ];
}


// update kill stat for single player (called by game code) -------------------
//
void NET_SetPlayerKillStat( int playerid, int amount )
{
	//NOTE:
	// this function is declared in NET_SUBH.H

	ASSERT( NetConnected );
	ASSERT( playerid >= 0 );
	ASSERT( playerid < MAX_NET_PROTO_PLAYERS );

	if ( ( (dword)playerid < (dword)MAX_NET_PROTO_PLAYERS ) && ( Player_Status[ playerid ] ) )
		Player_KillStat[ playerid ] += amount;

	// save biased id of player who killed us
	CurKiller = playerid + KILLERID_BIAS;

	// ensure that next packet contains killstat update
	// for the player that was killed right now
	CurKillUpdate = playerid;
}


// check whether the specified kill limit has already been reached ------------
//
int NET_KillStatLimitReached( int limit )
{
	ASSERT( NetConnected );

	//NOTE:
	// this function is only used by
	// G_MAIN::Gm_HandleGameOver().

	for ( int id = 0; id < MAX_NET_PROTO_PLAYERS; id++ ) {
		if ( Player_Status[ id ] != PLAYER_INACTIVE ) {
			if ( Player_KillStat[ id ] >= limit ) {
				return TRUE;
			}
		}
	}

	return FALSE;
}


// force all killstats to zero if no player joined ----------------------------
//
void NET_KillStatForceIdleZero()
{
	ASSERT( NetConnected );

	//NOTE:
	// this function is only used by
	// G_MAIN::Gm_HandleGameOver().

	int gameidle = TRUE;
	int id = 0;
	for ( id = 0; id < MAX_NET_PROTO_PLAYERS; id++ ) {
		if ( Player_Status[ id ] == PLAYER_JOINED ) {
			gameidle = FALSE;
		}
	}

	if ( gameidle ) {
		for ( id = 0; id < MAX_NET_PROTO_PLAYERS; id++ ) {
			Player_KillStat[ id ] = 0;
		}
	}
}


// remote player-list shown while waiting to join ------------------------------
//
PRIVATE
void DrawRemotePlayerListScreen()
{
	// Only HUD_CHARSETNO (index 2) is converted to a GL texture and can be
	// rendered in-game.  All other charset indices (including MSG_CHARSETNO)
	// silently draw nothing in release builds.
	D_SetWStrContext( CharsetInfo[ HUD_CHARSETNO ].charsetpointer,
					  CharsetInfo[ HUD_CHARSETNO ].geompointer,
					  NULL,
					  CharsetInfo[ HUD_CHARSETNO ].width,
					  CharsetInfo[ HUD_CHARSETNO ].height );
	SetHudCharColor( 4 );   // near-white

	int cw          = CharsetInfo[ HUD_CHARSETNO ].width;
	int ch          = CharsetInfo[ HUD_CHARSETNO ].height;
	int EM_Title_Y  = 100;
	int EM_PName_Y  = 120;
	int EM_LineDist = ch + 4;

	D_WriteString( logged_in_str,
				 ( Screen_Width - (int)strlen( logged_in_str ) * cw ) / 2, EM_Title_Y );

	const char *voidstr = "- available -";
	int stry = EM_PName_Y;

	for ( int i = 0; i < MAX_NET_PROTO_PLAYERS; i++ ) {

		const char *strp = Player_Status[ i ] ? Player_Name[ i ] : voidstr;

		if ( i == LocalPlayerId )
			strp = LocalPlayerName; // so local name can be changed on the fly

		int strx = ( Screen_Width - (int)strlen( strp ) * cw ) / 2;
		D_WriteString( strp, strx, stry );
		stry += EM_LineDist;
	}
}


// draw text for entry mode (names of all players currently logged in) --------
//
void NET_DrawEntryModeText()
{
	//NOTE:
	// this function is declared in NET_SUBH.H

	ASSERT( EntryMode );
	ASSERT( NetConnected );
	ASSERT( !FloatingMenu && !InFloatingMenu );

	// When NetJoined is TRUE the burst is in progress; NET_DrawJoiningOverlay()
	// handles the flashing "ENTERING GAME..." display in that case.
	if ( NetJoined ) {
		return;
	}

	DrawRemotePlayerListScreen();
}


// joining overlay: flash "ENTERING GAME..." during burst and for a short time
// after the burst completes so the message is always perceptible.
// -----------------------------------------------------------------
//
// Flash interval: half-period in refframes (300 = 0.5 s at 600 Hz → 1 Hz blink)
#define JOINING_FLASH_HALF  300
// Minimum display duration in refframes (600 = 1 s).  Guarantees visibility
// even when the burst completes before a single render frame fires (localhost).
#define JOINING_MIN_RF      600

static refframe_t s_JoiningRF      = 0;   // countdown: > 0 means display active
static refframe_t s_FlashPhaseRF   = 0;   // accumulates refframes for flash timing
static int        s_FlashVisible   = 1;   // 1 = text on, 0 = text off


// Call when the client joins (sends the join packet to the server).
// Resets the overlay timer so the message is guaranteed to appear.
//
void NET_StartJoiningDisplay()
{
	s_JoiningRF    = JOINING_MIN_RF;
	s_FlashPhaseRF = 0;
	s_FlashVisible = 1;
}


// Draw the flashing "ENTERING GAME..." overlay.
// Called unconditionally from the render section of g_gameloop.cpp so it keeps
// running even after EntryMode clears (provides the minimum-display linger).
//
void NET_DrawJoiningOverlay()
{
	// While the burst is still in progress keep the timer alive so the message
	// doesn't vanish before JOINDONE arrives.
	if ( EntryMode && NetJoined && NetConnected ) {
		if ( s_JoiningRF < JOINING_MIN_RF ) {
			s_JoiningRF = JOINING_MIN_RF;
		}
	}

	if ( s_JoiningRF <= 0 ) {
		return;
	}

	// Decrement countdown.
	s_JoiningRF -= CurScreenRefFrames;
	if ( s_JoiningRF < 0 ) {
		s_JoiningRF = 0;
	}

	// Advance flash phase and toggle visibility each half-period.
	s_FlashPhaseRF += CurScreenRefFrames;
	if ( s_FlashPhaseRF >= JOINING_FLASH_HALF ) {
		s_FlashPhaseRF -= JOINING_FLASH_HALF;
		s_FlashVisible  = !s_FlashVisible;
	}

	if ( !s_FlashVisible ) {
		return;
	}

	// Draw centered "ENTERING GAME..." using HUD_CHARSETNO — the only charset
	// that is converted to a GL texture and can actually render in-game.
	static const char joining_str[] = "ENTERING GAME...";

	D_SetWStrContext( CharsetInfo[ HUD_CHARSETNO ].charsetpointer,
	                  CharsetInfo[ HUD_CHARSETNO ].geompointer,
	                  NULL,
	                  CharsetInfo[ HUD_CHARSETNO ].width,
	                  CharsetInfo[ HUD_CHARSETNO ].height );
	SetHudCharColor( 4 );   // near-white — readable on any background

	int cw   = CharsetInfo[ HUD_CHARSETNO ].width;
	int ch   = CharsetInfo[ HUD_CHARSETNO ].height;
	int strx = ( Screen_Width  - (int)strlen( joining_str ) * cw ) / 2;
	int stry = ( Screen_Height - ch ) / 2;

	D_WriteString( joining_str, strx, stry );
}
