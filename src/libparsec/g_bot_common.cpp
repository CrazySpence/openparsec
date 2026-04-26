/*
 * PARSEC - Shared Bot Locomotion Controller
 *
 * Shared between the client-side bot (g_bot_cl.cpp) and the server-side bot
 * (e_bot_sv.cpp).  Contains only logic that compiles cleanly on both sides.
 *
 * Originally part of g_bot_cl.cpp:
 *   Copyright (c) Clemens Beer <cbx@parsec.org> 2002
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

// C library
#include <stdlib.h>
#include <math.h>

// compilation flags/debug support
#include "config.h"
#include "debug.h"

// general definitions
#include "general.h"
#include "objstruc.h"

// mathematics header
#include "utl_math.h"

// local module header
#include "g_bot_common.h"


// ----------------------------------------------------------------------------
// UTL_LocomotionController::ControlOjbect
//
// Computes the rotation and acceleration commands in pObjctl that will steer
// pObjctl->pShip toward pDesiredVelocity at _DesiredSpeed.
// ----------------------------------------------------------------------------
//
void UTL_LocomotionController::ControlOjbect( object_control_s* pObjctl,
                                              Vector3*          pDesiredVelocity,
                                              fixed_t           _DesiredSpeed )
{
	ASSERT( pObjctl          != NULL );
	ASSERT( pDesiredVelocity != NULL );

	Vector3 xDir, yDir, zDir;
	FetchXVector( pObjctl->pShip->ObjPosition, &xDir );
	FetchYVector( pObjctl->pShip->ObjPosition, &yDir );
	FetchZVector( pObjctl->pShip->ObjPosition, &zDir );

	geomv_t len = VctLenX( pDesiredVelocity );

	// stop control if desired velocity is zero
	if ( len <= GEOMV_VANISHING ) {
		pObjctl->rot_x = 0;
		pObjctl->rot_y = 0;
		pObjctl->accel = -0.82f;
		return;
	}

	Vector3 DesVelNorm;
	DesVelNorm.X = FLOAT_TO_GEOMV( pDesiredVelocity->X / len );
	DesVelNorm.Y = FLOAT_TO_GEOMV( pDesiredVelocity->Y / len );
	DesVelNorm.Z = FLOAT_TO_GEOMV( pDesiredVelocity->Z / len );

	geomv_t yaw_dot     = DOT_PRODUCT( &DesVelNorm, &xDir );
	geomv_t pitch_dot   = DOT_PRODUCT( &DesVelNorm, &yDir );
	geomv_t heading_dot = DOT_PRODUCT( &DesVelNorm, &zDir );

	float fDesiredSpeed = FIXED_TO_FLOAT( _DesiredSpeed );
	float fCurSpeed     = FIXED_TO_FLOAT( pObjctl->pShip->CurSpeed );

	float pitch = 0.0f;
	float yaw   = 0.0f;

	// target is behind us — initiate a turn
	sincosval_s fullturn;
	GetSinCos( DEG_TO_BAMS( 2 * m_nRelaxedHeadingAngle ), &fullturn );

	if ( heading_dot < -fullturn.cosval ) {

		if ( !pObjctl->IsYaw() || !pObjctl->IsPitch() ) {
			// default to random direction
			yaw   = (float)( RAND() % 3 ) - 1.0f;
			pitch = (float)( RAND() % 3 ) - 1.0f;

			// steer toward goal
			if      ( yaw_dot   < -GEOMV_VANISHING ) yaw   = OCT_YAW_LEFT;
			else if ( yaw_dot   >  GEOMV_VANISHING  ) yaw   = OCT_YAW_RIGHT;
			if      ( pitch_dot < -GEOMV_VANISHING ) pitch = OCT_PITCH_UP;
			else if ( pitch_dot >  GEOMV_VANISHING  ) pitch = OCT_PITCH_DOWN;
		} else {
			// reuse previous turn information
			pitch = pObjctl->rot_x;
			yaw   = pObjctl->rot_y;
		}

		// maintain minimum speed during turns
		if ( fCurSpeed > m_fMinSpeedTurn ) {
			pObjctl->accel = -0.42f;
		}

	} else {

		// determine acceleration
		if      ( fDesiredSpeed > fCurSpeed ) pObjctl->accel = OCT_ACCELERATE;
		else if ( fDesiredSpeed < fCurSpeed ) pObjctl->accel = OCT_DECELERATE;
		else                                   pObjctl->accel = 0.0f;

		// heading must be inside the relaxed cone angle
		sincosval_s sincosv;
		GetSinCos( DEG_TO_BAMS( m_nRelaxedHeadingAngle ), &sincosv );
		if      ( yaw_dot   < -sincosv.sinval ) yaw   = OCT_YAW_LEFT;
		else if ( yaw_dot   >  sincosv.sinval  ) yaw   = OCT_YAW_RIGHT;
		if      ( pitch_dot < -sincosv.sinval ) pitch = OCT_PITCH_UP;
		else if ( pitch_dot >  sincosv.sinval  ) pitch = OCT_PITCH_DOWN;

		// if heading is outside the full-speed cone, decelerate
		GetSinCos( DEG_TO_BAMS( m_nFullSpeedHeading ), &sincosv );
		bool_t bYawOutside   = ( yaw_dot   < -sincosv.sinval ) || ( yaw_dot   > sincosv.sinval );
		bool_t bPitchOutside = ( pitch_dot < -sincosv.sinval ) || ( pitch_dot > sincosv.sinval );
		if ( bYawOutside || bPitchOutside ) {
			float minspd = m_fMinSpeedTurn < fDesiredSpeed ? m_fMinSpeedTurn : fDesiredSpeed;
			if ( fCurSpeed > minspd ) {
				pObjctl->accel = OCT_DECELERATE;
			}
		}
	}

	pObjctl->rot_x = pitch;
	pObjctl->rot_y = yaw;
}
