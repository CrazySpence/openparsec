/*
 * PARSEC - Server-Side Bot AI
 *
 * Drives ship players internally without a real network client.  All AI logic
 * is ported from the client-side bot (g_bot_cl.cpp) with two key changes:
 *
 *   1. Output: instead of calling INP_UserRot* / INP_UserAcceleration(), we
 *      call E_SimClientState::ApplyBotInput() to inject directly into the sim.
 *
 *   2. Globals replaced: MyShip → m_pShip, NumRemPlayers → TheGame->GetNumJoined(),
 *      TargetObjNumber → m_nTargetObjNumber, TargetLocked → range check, etc.
 */

// C library
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
#include "e_defs.h"

// mathematics header
#include "utl_math.h"

// local module header
#include "e_bot_sv.h"

// proprietary module headers
#include "con_aux_sv.h"
#include "con_arg.h"
#include "con_com_sv.h"
#include "con_main_sv.h"
#include "e_simplayerinfo.h"
#include "net_game_sv.h"
#include "od_class.h"
#include "e_connmanager.h"
#include "e_simulator.h"
#include "e_gameserver.h"
#include "sys_refframe_sv.h"
#include "g_player.h"
#include "g_main_sv.h"
#include "e_world_trans.h"		// FetchFirstShip, FetchFirstExtra, FetchFirstCustom
#include "obj_creg.h"			// ShipClasses, NumShipClasses
#include "obj_cust.h"			// OBJ_FetchCustomTypeId
#include "g_telep.h"			// teleporter_type
#include "g_wfx.h"				// WFX_Activate/Deactivate Helix/Lightning/Photon
#include "e_simnetoutput.h"		// TheSimNetOutput, BufferForMulticastRE
#include "od_class.h"			// EXTRAINDX_*, *_CLASS constants
#include "od_masks.h"			// WPMASK_*, SPMASK_*


// bot respawn delay after death (refframes; FRAME_MEASURE_TIMEBASE = 600/sec)
#define BOT_RESPAWN_DELAY_REFFRAMES  ( FRAME_MEASURE_TIMEBASE * 5 )   // 5 seconds


// ---------------------------------------------------------------------------
// E_BotPlayer
// ---------------------------------------------------------------------------

// initialise the bot for an already-connected/joined slot -------------------
//
void E_BotPlayer::Init( int nClientID, const char* name, G_Player* pPlayer,
                        E_SimClientState* pSimState, ShipObject* pShip )
{
	ASSERT( pPlayer   != NULL );
	ASSERT( pSimState != NULL );
	ASSERT( pShip     != NULL );

	m_nClientID   = nClientID;
	strncpy( m_szName, name ? name : "bot", sizeof( m_szName ) - 1 );
	m_szName[ sizeof( m_szName ) - 1 ] = '\0';
	m_pPlayer     = pPlayer;
	m_pSimState   = pSimState;
	m_pShip       = pShip;
	m_nAgentMode  = AGENTMODE_ATTACK;
	m_fCurSpeed   = 0;
	m_Goal.Reset();
	memset( &m_oc, 0, sizeof( object_control_s ) );
	m_oc.pShip    = pShip;
}


// keep alive counter ticking so the connection-manager does not time out the slot
//
void E_BotPlayer::KeepAlive()
{
	E_ClientInfo* pInfo = TheConnManager->GetClientInfo( m_nClientID );
	if ( pInfo != NULL ) {
		pInfo->MarkAlive();
	}
}


// run one AI tick -----------------------------------------------------------
//
void E_BotPlayer::DoThink( refframe_t refframes )
{
	E_SimPlayerInfo* pSPI = TheSimulator->GetSimPlayerInfo( m_nClientID );
	if ( pSPI == NULL ) return;

	// Respawn after a delay when the ship has been destroyed.
	if ( !pSPI->IsPlayerJoined() ) {
		refframe_t now = SYSs_GetRefFrameCount();

		if ( m_DeathRefFrame == 0 ) {
			// Record the time of death on the first tick we notice the unjoin.
			m_DeathRefFrame = now;
			if ( m_bDebug )
				MSGOUT( "BOT[%d] died — will respawn in %d s",
				        m_nClientID, BOT_RESPAWN_DELAY_REFFRAMES / FRAME_MEASURE_TIMEBASE );
		}

		if ( ( now - m_DeathRefFrame ) < BOT_RESPAWN_DELAY_REFFRAMES ) {
			// Still waiting out the respawn delay.
			return;
		}

		// Delay elapsed — respawn now.
		pSPI->BotPerformJoin( m_szName );
		m_pShip       = pSPI->GetShipObject();
		m_fCurSpeed   = 0;
		m_DeathRefFrame = 0;
		m_Goal.Reset();
		m_nAgentMode  = AGENTMODE_ATTACK;
		if ( m_pShip ) {
			m_oc.pShip = m_pShip;
			FetchTVector( m_pShip->ObjPosition, &m_AgentPos );
		}
		if ( m_bDebug )
			MSGOUT( "BOT[%d] respawned", m_nClientID );
		return;  // skip AI this tick; let the new ship settle
	}
	// Bot is alive — clear death timer if it somehow lingered.
	m_DeathRefFrame = 0;

	m_pShip    = pSPI->GetShipObject();
	m_oc.pShip = m_pShip;

	if ( m_pShip == NULL ) return;

	// update current position
	FetchTVector( m_pShip->ObjPosition, &m_AgentPos );

	// determine mode
	_DoPlan();

	// check goal for current mode
	switch ( m_nAgentMode ) {
		case AGENTMODE_RETREAT:  _GoalCheck_Retreat();  break;
		case AGENTMODE_ATTACK:   _GoalCheck_Attack();   break;
		case AGENTMODE_IDLE:     _GoalCheck_Idle();     break;
		case AGENTMODE_POWERUP:  _GoalCheck_Powerup();  break;
		default: return;
	}

	// Nothing to do — bleed off speed and hold heading rather than steering
	// toward (0,0,0) which is what an unset goal contains.
	if ( m_nAgentMode == AGENTMODE_IDLE ) {
		m_fCurSpeed = 0;
		m_pSimState->ApplyBotInput( 0, 0, 0, 0, m_pShip->MaxSpeed );
		return;
	}

	// Debug: log bot state every ~2 seconds when debug is enabled
	if ( m_bDebug ) {
		m_nDebugCounter++;
		if ( m_nDebugCounter >= 20 ) {
			m_nDebugCounter = 0;
			static const char* kModeNames[] = { "?", "IDLE", "POWERUP", "ATTACK", "RETREAT" };
			const char* modeName = ( m_nAgentMode >= 1 && m_nAgentMode <= 4 )
			                        ? kModeNames[ m_nAgentMode ] : "?";
			Vector3* pGoal = m_Goal.GetGoalPosition();
			MSGOUT( "BOT[%d] %s  pos(%.0f,%.0f,%.0f)  goal(%.0f,%.0f,%.0f)  "
			        "spd=%.0f  hp=%d/%d  msl=%d  nrg=%.0f/%.0f",
			        m_nClientID, modeName,
			        m_AgentPos.X, m_AgentPos.Y, m_AgentPos.Z,
			        pGoal->X, pGoal->Y, pGoal->Z,
			        FIXED_TO_FLOAT( m_fCurSpeed ),
			        m_pShip->CurDamage, m_pShip->MaxDamage,
			        m_pShip->NumHomMissls,
			        (float)m_pShip->CurEnergy, (float)m_pShip->MaxEnergy );
		}
	}

	// steer toward goal
	_SteerToPosition( m_Goal.GetGoalPosition() );

	// apply control to the sim
	_ApplyControl( &m_oc, refframes );

	// count down weapon delays
	const float kDelta = 0.03f;
	m_fFireDelay    -= kDelta;
	m_fMissileDelay -= kDelta;
	m_fEMPDelay     -= kDelta;
}


// decide which agent mode to be in ------------------------------------------
//
void E_BotPlayer::_DoPlan()
{
	agentmode_t prevMode = m_nAgentMode;

	if ( m_pShip->CurDamage > ( m_pShip->MaxDamage * ( 1.0f - m_fRetreatHP ) ) ) {
		_DeactivatePrefWeaponIfActive();
		if ( m_bDebug && m_nAgentMode != AGENTMODE_RETREAT )
			MSGOUT( "BOT[%d] plan: RETREAT (damage %d / max %d)",
			        m_nClientID,
			        m_pShip->CurDamage, m_pShip->MaxDamage );
		m_nAgentMode = AGENTMODE_RETREAT;
		return;
	}
	if ( m_pShip->CurEnergy < ( m_pShip->MaxEnergy * SV_BOT_ENERGY_LEVEL ) ) {
		_DeactivatePrefWeaponIfActive();
		if ( m_bDebug && m_nAgentMode != AGENTMODE_RETREAT )
			MSGOUT( "BOT[%d] plan: RETREAT (energy %.0f / max %.0f)",
			        m_nClientID,
			        (float)m_pShip->CurEnergy, (float)m_pShip->MaxEnergy );
		m_nAgentMode = AGENTMODE_RETREAT;
		return;
	}
	// Note: no retreat for msl=0 — bots fight with lasers/EMP when out of missiles.

	// if energy is getting low and there is a pack available, go fetch it now
	// rather than running dry mid-combat (especially relevant for cannon bots)
	if ( m_pShip->CurEnergy < ( m_pShip->MaxEnergy * SV_BOT_ENERGY_SEEK ) ) {
		if ( _SelectEnergyObject() != NULL ) {
			_DeactivatePrefWeaponIfActive();
			if ( m_bDebug && m_nAgentMode != AGENTMODE_POWERUP )
				MSGOUT( "BOT[%d] plan: POWERUP (low energy %.0f/%.0f, seeking pack)",
				        m_nClientID,
				        (float)m_pShip->CurEnergy, (float)m_pShip->MaxEnergy );
			m_nAgentMode = AGENTMODE_POWERUP;
			return;
		}
	}

	// seek configured preferred items before attacking — the bot is expected to
	// have its loadout before engaging, and will detour to collect one if available.
	// Each selector already gates on "not already owned" / "below 50%" / "not full".
	{
		ExtraObject* pPref = NULL;

		if ( pPref == NULL && m_nPrefWeapon != BOTWEAPON_NONE )
			pPref = _SelectPreferredWeaponObject();

		if ( pPref == NULL && m_nPrefMissile != BOTMISSILE_NONE )
			pPref = _SelectPreferredMissileObject();

		if ( pPref == NULL && m_bMineLayer )
			pPref = _SelectMinePackObject();

		if ( pPref != NULL ) {
			if ( m_bDebug && m_nAgentMode != AGENTMODE_POWERUP )
				MSGOUT( "BOT[%d] plan: POWERUP (seeking preferred item, ObjClass=%d)",
				        m_nClientID, pPref->ObjectClass );
			m_nAgentMode = AGENTMODE_POWERUP;
			return;
		}
	}

	// if there are other joined players, attack one
	if ( TheGame->GetNumJoined() > 1 ) {
		ShipObject* pTarget = _SelectAttackTarget();
		if ( pTarget != NULL ) {
			if ( m_bDebug && m_nAgentMode != AGENTMODE_ATTACK )
				MSGOUT( "BOT[%d] plan: ATTACK (target ObjId %u, joined=%d)",
				        m_nClientID, pTarget->HostObjNumber,
				        TheGame->GetNumJoined() );
			m_nAgentMode = AGENTMODE_ATTACK;
			return;
		}
		if ( m_bDebug )
			MSGOUT( "BOT[%d] plan: joined=%d but _SelectAttackTarget returned NULL",
			        m_nClientID, TheGame->GetNumJoined() );
	}

	// look for powerups
	ExtraObject* pExtra = FetchFirstExtra();
	if ( pExtra != NULL ) {
		if ( m_bDebug && m_nAgentMode != AGENTMODE_POWERUP )
			MSGOUT( "BOT[%d] plan: POWERUP (first extra ObjType=%d)",
			        m_nClientID, pExtra->ObjectType );
		m_nAgentMode = AGENTMODE_POWERUP;
	} else {
		if ( m_bDebug && m_nAgentMode != AGENTMODE_IDLE )
			MSGOUT( "BOT[%d] plan: IDLE (no targets, no extras)", m_nClientID );
		m_nAgentMode = AGENTMODE_IDLE;
	}
}


// validate a cached ship pointer against the live ship list ------------------
// Returns the pointer unchanged if it is still alive, NULL otherwise.
// MUST be called before dereferencing any cached ship/extra pointer.
//
static ShipObject* ValidateCachedShip( ShipObject* pShip )
{
	if ( pShip == NULL ) return NULL;
	for ( ShipObject* s = FetchFirstShip(); s != NULL;
	      s = (ShipObject*) s->NextObj ) {
		if ( s == pShip ) return pShip;
	}
	return NULL;   // not in live list — freed/unjoined
}

static ExtraObject* ValidateCachedExtra( ExtraObject* pExtra )
{
	if ( pExtra == NULL ) return NULL;
	for ( ExtraObject* e = FetchFirstExtra(); e != NULL;
	      e = (ExtraObject*) e->NextObj ) {
		if ( e == pExtra ) return pExtra;
	}
	return NULL;   // not in live list — collected/removed
}


// idle: re-plan each tick in case enemies or powerups have appeared -----------
//
void E_BotPlayer::_GoalCheck_Idle()
{
	m_Goal.Reset();   // clear any stale (0,0,0) goal before re-planning
	_DoPlan();
	// if _DoPlan() changed the mode, the next tick's goal-check will handle it;
	// this tick we do nothing (the AGENTMODE_IDLE early-out in DoThink halts us)
}


// powerup: navigate to preferred extra, or nearest if no preference ----------
//
void E_BotPlayer::_GoalCheck_Powerup()
{
	Vector3* pGoalPos = m_Goal.GetGoalPosition();

	// Validate cached extra pointer — it may have been collected/removed.
	ExtraObject* pObject = ValidateCachedExtra( (ExtraObject*) m_Goal.GetTargetObject() );
	if ( pObject == NULL ) {
		m_Goal.SetTargetObject( NULL );

		// energy takes top priority when running low — weapons drain it fast
		if ( m_pShip->CurEnergy < ( m_pShip->MaxEnergy * SV_BOT_ENERGY_SEEK ) )
			pObject = _SelectEnergyObject();

		// seek preferred weapon if configured and not already owned
		if ( pObject == NULL && m_nPrefWeapon != BOTWEAPON_NONE )
			pObject = _SelectPreferredWeaponObject();

		// seek preferred missile supply if configured and running low
		if ( pObject == NULL && m_nPrefMissile != BOTMISSILE_NONE )
			pObject = _SelectPreferredMissileObject();

		// seek mine pack if mine-layer role
		if ( pObject == NULL && m_bMineLayer )
			pObject = _SelectMinePackObject();

		// fall back to whatever is available
		if ( pObject == NULL )
			pObject = FetchFirstExtra();

		if ( pObject == NULL ) {
			m_nAgentMode = AGENTMODE_IDLE;
			return;
		}
		m_Goal.SetTargetObject( pObject );
		FetchTVector( pObject->ObjPosition, pGoalPos );
	}

	pGoalPos = m_Goal.GetGoalPosition();

	Vector3 vec2Target;
	VECSUB( &vec2Target, pGoalPos, &m_AgentPos );
	float len = GEOMV_TO_FLOAT( VctLenX( &vec2Target ) );

	if ( len < 100.0f ) {
		m_Goal.SetTargetObject( NULL );
		_DoPlan();
	}
}


// attack: chase and shoot the weakest enemy ----------------------------------
//
void E_BotPlayer::_GoalCheck_Attack()
{
	Vector3*   pGoalPos      = m_Goal.GetGoalPosition();

	// Validate the cached target against the live ship list before touching it.
	// If the target died/unjoined since last tick, ValidateCachedShip returns NULL,
	// preventing any dereference of freed ShipObject memory.
	ShipObject* pTargetObject = ValidateCachedShip( (ShipObject*) m_Goal.GetTargetObject() );
	if ( pTargetObject == NULL ) {
		m_Goal.SetTargetObject( NULL );
		pTargetObject = _SelectAttackTarget();
		if ( pTargetObject == NULL ) {
			_DeactivatePrefWeaponIfActive();
			m_nAgentMode = AGENTMODE_IDLE;
			_DoPlan();
			return;
		}
		m_Goal.SetTargetObject( pTargetObject );
		FetchTVector( pTargetObject->ObjPosition, pGoalPos );
	}

	Vector3 TargetPos;
	FetchTVector( pTargetObject->ObjPosition, &TargetPos );

	Vector3 vec2Target;
	VECSUB( &vec2Target, &TargetPos, &m_AgentPos );
	float len = GEOMV_TO_FLOAT( VctLenX( &vec2Target ) );

	// weapon firing
	if ( _TargetInRange( m_pShip, (ShipObject*) pTargetObject, 1500.0f ) ) {

		// fire preferred cannon weapon if equipped — these are continuous beams.
		// Check WeaponsActive before and after: if it just turned on, multicast
		// RE_WEAPONSTATE WPSTATE_ON so clients render the beam effect.
		switch ( m_nPrefWeapon ) {
			case BOTWEAPON_HELIX:
				if ( m_pShip->Weapons & WPMASK_CANNON_HELIX ) {
					bool wasActive = ( m_pShip->WeaponsActive & WPMASK_CANNON_HELIX ) != 0;
					m_pPlayer->FireHelix();
					if ( !wasActive && ( m_pShip->WeaponsActive & WPMASK_CANNON_HELIX ) )
						_MulticastWeaponState( WPMASK_CANNON_HELIX, WPSTATE_ON );
				}
				break;
			case BOTWEAPON_LIGHTNING:
				if ( m_pShip->Weapons & WPMASK_CANNON_LIGHTNING ) {
					bool wasActive = ( m_pShip->WeaponsActive & WPMASK_CANNON_LIGHTNING ) != 0;
					m_pPlayer->FireLightning();
					if ( !wasActive && ( m_pShip->WeaponsActive & WPMASK_CANNON_LIGHTNING ) )
						_MulticastWeaponState( WPMASK_CANNON_LIGHTNING, WPSTATE_ON );
				}
				break;
			case BOTWEAPON_PHOTON:
				if ( m_pShip->Weapons & WPMASK_CANNON_PHOTON ) {
					bool wasActive = ( m_pShip->WeaponsActive & WPMASK_CANNON_PHOTON ) != 0;
					m_pPlayer->FirePhoton();
					if ( !wasActive && ( m_pShip->WeaponsActive & WPMASK_CANNON_PHOTON ) )
						_MulticastWeaponState( WPMASK_CANNON_PHOTON, WPSTATE_ON );
				}
				break;
			default:
				break;
		}

		if ( len < 600.0f && m_fFireDelay <= 0.0f ) {
			m_pPlayer->FireLaser();
			m_fFireDelay = 1.0f;
		}
		if ( len > 500.0f && m_pShip->NumHomMissls > 0 ) {
			// server bot uses the stored target number for homing missiles
			m_nTargetObjNumber = ( (ShipObject*) pTargetObject )->HostObjNumber;
			if ( m_fMissileDelay <= 0.0f ) {
				m_pPlayer->LaunchHomingMissile( (dword) m_nCurLauncher, m_nTargetObjNumber );
				m_nCurLauncher++;
				m_fMissileDelay = 2.0f;
			}
		}
		if ( len < 100.0f && m_fEMPDelay <= 0.0f ) {
			m_pPlayer->FireEMP( 0, true );  // true = multicast RE_CreateEmp so clients see the blast
			m_fEMPDelay = m_fEMPMinDelay;
		}
		if ( m_bMineLayer && _ShouldLayMine() ) {
			m_pPlayer->LaunchMine();
		}
	}

	// update goal position
	if ( len < 100.0f ) {
		memcpy( pGoalPos, &m_AgentPos, sizeof( Vector3 ) );
	} else {
		FetchTVector( pTargetObject->ObjPosition, pGoalPos );
	}

	// abandon target if it has moved too far away
	if ( len > 20000.0f ) {
		_DeactivatePrefWeaponIfActive();
		m_nAgentMode = AGENTMODE_IDLE;
		m_Goal.SetTargetObject( NULL );
		_DoPlan();
	}
}


// retreat: seek repair / energy / missile packs ------------------------------
//
void E_BotPlayer::_GoalCheck_Retreat()
{
	Vector3*     pGoalPos = m_Goal.GetGoalPosition();
	ExtraObject* pObject  = NULL;

	// Validate cached extra pointer — it may have been collected/removed.
	pObject = ValidateCachedExtra( (ExtraObject*) m_Goal.GetTargetObject() );
	if ( pObject == NULL ) {
		m_Goal.SetTargetObject( NULL );

		if ( m_pShip->NumHomMissls < (int)( m_pShip->MaxNumHomMissls * SV_BOT_GMISSL_LEVEL ) ) {
			pObject = _SelectMissileObject();
		}
		if ( m_pShip->CurEnergy < ( m_pShip->MaxEnergy * SV_BOT_ENERGY_LEVEL ) ) {
			pObject = _SelectEnergyObject();
		}
		if ( m_pShip->CurDamage > ( m_pShip->MaxDamage * ( 1.0f - m_fRetreatHP ) ) ) {
			pObject = _SelectRepairObject();
		}

		if ( pObject == NULL ) {
			// No suitable powerup found — hold position rather than steering
			// toward the default (0,0,0) goal.  Re-plan to attack if possible.
			if ( m_bDebug )
				MSGOUT( "BOT[%d] RETREAT: no suitable extra found, holding position", m_nClientID );
			_DoPlan();
			if ( m_nAgentMode == AGENTMODE_RETREAT )
				m_nAgentMode = AGENTMODE_IDLE;   // don't steer to origin
			return;
		}

		m_Goal.SetTargetObject( pObject );
		FetchTVector( pObject->ObjPosition, pGoalPos );
	}

	pGoalPos = m_Goal.GetGoalPosition();

	Vector3 vec2Target;
	VECSUB( &vec2Target, pGoalPos, &m_AgentPos );
	float len = GEOMV_TO_FLOAT( VctLenX( &vec2Target ) );

	if ( len < 100.0f ) {
		m_Goal.SetTargetObject( NULL );
		_DoPlan();
	}
}


// translate object_control_s into E_SimClientState input deltas -------------
//
void E_BotPlayer::_ApplyControl( object_control_s* pObjctl, refframe_t refframes )
{
	ASSERT( pObjctl     != NULL );
	ASSERT( m_pShip     != NULL );
	ASSERT( m_pSimState != NULL );

	// Rotation RATES (bams/refframe) — CalcNewState multiplies by CurSimRefFrames itself,
	// so do NOT pre-multiply by refframes here.
	//
	// Sign note: the client's INP_UserRotY/X apply "angle = -angle / frames" before
	// accumulating into CurYaw/CurPitch that the server then passes to ObjRotY/X.
	// The locomotion controller was written against the client convention, so the
	// server bot must negate each axis to match the same physical rotation direction.
	bams_t yaw   = (bams_t)( -pObjctl->rot_y * (float)m_pShip->YawPerRefFrame   );
	bams_t pitch = (bams_t)( -pObjctl->rot_x * (float)m_pShip->PitchPerRefFrame );
	bams_t roll  = (bams_t)( -pObjctl->rot_z * (float)m_pShip->RollPerRefFrame  );

	// Absolute speed — bot accumulates it (sim carries speed as an absolute value,
	// not a delta, so we must track it ourselves between think ticks).
	if ( pObjctl->accel != 0.0f ) {
		m_fCurSpeed += (fixed_t)( pObjctl->accel * (float)m_pShip->SpeedIncPerRefFrame * (float)refframes );
		if ( m_fCurSpeed < 0 )                    m_fCurSpeed = 0;
		if ( m_fCurSpeed > m_pShip->MaxSpeed )    m_fCurSpeed = m_pShip->MaxSpeed;
	}

	m_pSimState->ApplyBotInput( yaw, pitch, roll, m_fCurSpeed, m_pShip->MaxSpeed );
}


// steer the bot toward a world position -------------------------------------
//
void E_BotPlayer::_SteerToPosition( Vector3* pTargetPos )
{
	// Sync the ship's CurSpeed from our tracked value so ControlOjbect sees the
	// real speed (the sim never writes CurSpeed back to the ShipObject).
	m_pShip->CurSpeed = m_fCurSpeed;

	Vector3 vec2Target;
	VECSUB( &vec2Target, pTargetPos, &m_AgentPos );
	m_Loco.ControlOjbect( &m_oc, &vec2Target, m_pShip->MaxSpeed );
}


// check whether target is within range (and in front of) ship ---------------
//
int E_BotPlayer::_TargetInRange( ShipObject* ship, ShipObject* target, geomv_t range )
{
	ASSERT( ship   != NULL );
	ASSERT( target != NULL );

	Vector3 tgtnormal;
	FetchZVector( target->ObjPosition, &tgtnormal );

	Vertex3 tgtpos, shippos;
	FetchTVector( target->ObjPosition, &tgtpos );
	FetchTVector( ship->ObjPosition,   &shippos );

	geomv_t shipdot  = -DOT_PRODUCT( &tgtnormal, &shippos );
	geomv_t tgtdot   = -DOT_PRODUCT( &tgtnormal, &tgtpos );
	geomv_t distance = shipdot - tgtdot;

	if ( !GEOMV_NEGATIVE( distance ) ) return FALSE;
	if ( distance < range )            return TRUE;
	return FALSE;
}


// select the weakest ship as attack target -----------------------------------
//
ShipObject* E_BotPlayer::_SelectAttackTarget()
{
	// Pick the nearest ship.  Bots are at different positions so each
	// naturally ends up with a different nearest target — far better than
	// the old "highest HP" criterion which caused all bots to pile onto the
	// same ship.  When two bots happen to be equidistant from a candidate
	// we break the tie with client ID so they still diverge.

	geomv_t myX = m_pShip->ObjPosition[ 0 ][ 3 ];
	geomv_t myY = m_pShip->ObjPosition[ 1 ][ 3 ];
	geomv_t myZ = m_pShip->ObjPosition[ 2 ][ 3 ];

	ShipObject* pBestTarget = NULL;
	geomv_t     fBestDist2  = 0;

	for ( ShipObject* pShip = FetchFirstShip(); pShip != NULL;
	      pShip = (ShipObject*) pShip->NextObj ) {

		if ( pShip == m_pShip ) continue;

		geomv_t dx = pShip->ObjPosition[ 0 ][ 3 ] - myX;
		geomv_t dy = pShip->ObjPosition[ 1 ][ 3 ] - myY;
		geomv_t dz = pShip->ObjPosition[ 2 ][ 3 ] - myZ;
		geomv_t dist2 = dx*dx + dy*dy + dz*dz;

		// Tiebreak on HostObjNumber XOR'd with client ID so two bots that
		// are equidistant from a candidate still pick differently.
		if ( pBestTarget == NULL || dist2 < fBestDist2 ||
		     ( dist2 == fBestDist2 &&
		       ( (pShip->HostObjNumber ^ (dword)m_nClientID) <
		         (pBestTarget->HostObjNumber ^ (dword)m_nClientID) ) ) ) {
			pBestTarget = pShip;
			fBestDist2  = dist2;
		}
	}

	if ( pBestTarget != NULL ) {
		m_nTargetObjNumber = pBestTarget->HostObjNumber;
	}
	return pBestTarget;
}


// select a repair pack -------------------------------------------------------
//
ExtraObject* E_BotPlayer::_SelectRepairObject()
{
	for ( ExtraObject* p = FetchFirstExtra(); p != NULL;
	      p = (ExtraObject*) p->NextObj ) {
		if ( p->ObjectType == EXTRA1TYPE ) {
			Extra1Obj* e = (Extra1Obj*) p;
			if ( e->ObjectClass == REPAIR_EXTRA_CLASS )
				return p;
		}
	}
	return NULL;
}


// select an energy pack ------------------------------------------------------
//
ExtraObject* E_BotPlayer::_SelectEnergyObject()
{
	for ( ExtraObject* p = FetchFirstExtra(); p != NULL;
	      p = (ExtraObject*) p->NextObj ) {
		if ( p->ObjectType == EXTRA1TYPE ) {
			Extra1Obj* e = (Extra1Obj*) p;
			if ( e->ObjectClass == ENERGY_EXTRA_CLASS )
				return p;
		}
	}
	return NULL;
}


// select a homing missile pack -----------------------------------------------
//
ExtraObject* E_BotPlayer::_SelectMissileObject()
{
	for ( ExtraObject* p = FetchFirstExtra(); p != NULL;
	      p = (ExtraObject*) p->NextObj ) {
		if ( p->ObjectType == EXTRA2TYPE ) {
			Extra2Obj* e = (Extra2Obj*) p;
			if ( e->ObjectClass == GUIDE_PACK_CLASS )
				return p;
		}
	}
	return NULL;
}


// send RE_WEAPONSTATE to all clients for this bot's ship ---------------------
// mirrors what e_simnetinput.cpp does when a real client fires a duration weapon
//
void E_BotPlayer::_MulticastWeaponState( dword weaponMask, byte state )
{
	RE_WeaponState re;
	memset( &re, 0, sizeof( re ) );
	re.RE_Type      = RE_WEAPONSTATE;
	re.RE_BlockSize = sizeof( RE_WeaponState );
	re.WeaponMask   = weaponMask;
	re.State        = state;
	re.CurEnergy    = m_pShip ? m_pShip->CurEnergy : 0;
	re.SenderId     = m_nClientID;
	TheSimNetOutput->BufferForMulticastRE( (RE_Header*)&re, m_nClientID, FALSE );
}


// deactivate any continuous cannon weapon that is currently firing -----------
// called whenever the bot stops attacking (target lost, mode change, death).
//
void E_BotPlayer::_DeactivatePrefWeaponIfActive()
{
	if ( m_pShip == NULL ) return;
	if ( m_pShip->WeaponsActive & WPMASK_CANNON_HELIX ) {
		WFX_DeactivateHelix( m_pShip );
		_MulticastWeaponState( WPMASK_CANNON_HELIX, WPSTATE_OFF );
	}
	if ( m_pShip->WeaponsActive & WPMASK_CANNON_LIGHTNING ) {
		WFX_DeactivateLightning( m_pShip );
		_MulticastWeaponState( WPMASK_CANNON_LIGHTNING, WPSTATE_OFF );
	}
	if ( m_pShip->WeaponsActive & WPMASK_CANNON_PHOTON ) {
		WFX_DeactivatePhoton( m_pShip );
		_MulticastWeaponState( WPMASK_CANNON_PHOTON, WPSTATE_OFF );
	}
}


// select the preferred weapon device pickup (bot doesn't have it yet) --------
//
ExtraObject* E_BotPlayer::_SelectPreferredWeaponObject()
{
	if ( m_nPrefWeapon == BOTWEAPON_NONE ) return NULL;

	// bail out if bot already has the preferred weapon
	switch ( m_nPrefWeapon ) {
		case BOTWEAPON_LASER2:
			if ( m_pShip->Specials & SPMASK_LASER_UPGRADE_1 ) return NULL;
			break;
		case BOTWEAPON_LASER3:
			if ( m_pShip->Specials & SPMASK_LASER_UPGRADE_2 ) return NULL;
			break;
		case BOTWEAPON_HELIX:
			if ( m_pShip->Weapons & WPMASK_CANNON_HELIX )     return NULL;
			break;
		case BOTWEAPON_LIGHTNING:
			if ( m_pShip->Weapons & WPMASK_CANNON_LIGHTNING ) return NULL;
			break;
		case BOTWEAPON_PHOTON:
			if ( m_pShip->Weapons & WPMASK_CANNON_PHOTON )    return NULL;
			break;
		default:
			return NULL;
	}

	// determine which class to find
	int targetClass = -1;
	switch ( m_nPrefWeapon ) {
		case BOTWEAPON_LASER2:
			targetClass = LASERUPGRADE1_CLASS;
			break;
		case BOTWEAPON_LASER3:
			// need upgrade 1 before upgrade 2
			targetClass = ( m_pShip->Specials & SPMASK_LASER_UPGRADE_1 )
			              ? LASERUPGRADE2_CLASS : LASERUPGRADE1_CLASS;
			break;
		case BOTWEAPON_HELIX:     targetClass = HELIX_DEVICE_CLASS;     break;
		case BOTWEAPON_LIGHTNING: targetClass = LIGHTNING_DEVICE_CLASS; break;
		case BOTWEAPON_PHOTON:    targetClass = PHOTON_DEVICE_CLASS;    break;
		default: return NULL;
	}

	for ( ExtraObject* p = FetchFirstExtra(); p != NULL;
	      p = (ExtraObject*) p->NextObj ) {
		if ( p->ObjectType == EXTRA3TYPE ) {
			Extra3Obj* e = (Extra3Obj*) p;
			if ( e->ObjectClass == targetClass )
				return p;
		}
	}
	return NULL;
}


// select the preferred missile pack if running low ---------------------------
//
ExtraObject* E_BotPlayer::_SelectPreferredMissileObject()
{
	if ( m_nPrefMissile == BOTMISSILE_NONE ) return NULL;

	int targetClass = -1;

	switch ( m_nPrefMissile ) {
		case BOTMISSILE_DUMB:
			if ( m_pShip->NumMissls >= (int)( m_pShip->MaxNumMissls * 0.5f ) )
				return NULL;
			targetClass = DUMB_PACK_CLASS;
			break;
		case BOTMISSILE_GUIDE:
			if ( m_pShip->NumHomMissls >= (int)( m_pShip->MaxNumHomMissls * 0.5f ) )
				return NULL;
			targetClass = GUIDE_PACK_CLASS;
			break;
		case BOTMISSILE_SWARM:
			if ( m_pShip->NumPartMissls >= (int)( m_pShip->MaxNumPartMissls * 0.5f ) )
				return NULL;
			targetClass = SWARM_PACK_CLASS;
			break;
		default:
			return NULL;
	}

	for ( ExtraObject* p = FetchFirstExtra(); p != NULL;
	      p = (ExtraObject*) p->NextObj ) {
		if ( p->ObjectType == EXTRA2TYPE ) {
			Extra2Obj* e = (Extra2Obj*) p;
			if ( e->ObjectClass == targetClass )
				return p;
		}
	}
	return NULL;
}


// select a mine pack (mine-layer role) ---------------------------------------
//
ExtraObject* E_BotPlayer::_SelectMinePackObject()
{
	// already at capacity
	if ( m_pShip->NumMines >= m_pShip->MaxNumMines ) return NULL;

	for ( ExtraObject* p = FetchFirstExtra(); p != NULL;
	      p = (ExtraObject*) p->NextObj ) {
		if ( p->ObjectType == EXTRA2TYPE ) {
			Extra2Obj* e = (Extra2Obj*) p;
			if ( e->ObjectClass == MINE_PACK_CLASS )
				return p;
		}
	}
	return NULL;
}


// decide whether a mine-layer bot should drop a mine now ---------------------
//
bool E_BotPlayer::_ShouldLayMine()
{
	if ( m_pShip->NumMines <= 0 ) return false;

	// condition 1: an enemy ship is within 250 units (being chased)
	for ( ShipObject* s = FetchFirstShip(); s != NULL;
	      s = (ShipObject*) s->NextObj ) {
		if ( s == m_pShip ) continue;
		Vector3 spos;
		FetchTVector( s->ObjPosition, &spos );
		Vector3 diff;
		VECSUB( &diff, &spos, &m_AgentPos );
		float dist = GEOMV_TO_FLOAT( VctLenX( &diff ) );
		if ( dist < 250.0f ) return true;
	}

	// condition 2: within 400 units of a stargate or teleporter
	static dword sg_typeid = TYPE_ID_INVALID;
	if ( sg_typeid == TYPE_ID_INVALID )
		sg_typeid = OBJ_FetchCustomTypeId( "stargate" );

	for ( CustomObject* obj = FetchFirstCustom(); obj != NULL;
	      obj = (CustomObject*) obj->NextObj ) {
		if ( obj->ObjectType == CUSTM_LIST_NO ) break;
		if ( obj->ObjectType == teleporter_type || obj->ObjectType == sg_typeid ) {
			Vector3 opos;
			FetchTVector( obj->ObjPosition, &opos );
			Vector3 diff;
			VECSUB( &diff, &opos, &m_AgentPos );
			float dist = GEOMV_TO_FLOAT( VctLenX( &diff ) );
			if ( dist < 400.0f ) return true;
		}
	}

	return false;
}


// apply a named config parameter (returns false if param unknown) ------------
//
bool E_BotPlayer::ApplyConfig( const char* param, const char* value )
{
	ASSERT( param != NULL );

	if ( strcmp( param, "empdly" ) == 0 ) {
		float v = (float)atof( value ? value : "1" );
		if ( v < 0.5f ) v = 0.5f;
		if ( v > 60.0f ) v = 60.0f;
		m_fEMPMinDelay = v;
		MSGOUT( "BOT[%d] empdly = %.2f", m_nClientID, m_fEMPMinDelay );
		return true;
	}

	if ( strcmp( param, "weapon" ) == 0 ) {
		const char* v = value ? value : "none";
		if      ( strcmp( v, "none"      ) == 0 ) m_nPrefWeapon = BOTWEAPON_NONE;
		else if ( strcmp( v, "laser2"    ) == 0 ) m_nPrefWeapon = BOTWEAPON_LASER2;
		else if ( strcmp( v, "laser3"    ) == 0 ) m_nPrefWeapon = BOTWEAPON_LASER3;
		else if ( strcmp( v, "helix"     ) == 0 ) m_nPrefWeapon = BOTWEAPON_HELIX;
		else if ( strcmp( v, "lightning" ) == 0 ) m_nPrefWeapon = BOTWEAPON_LIGHTNING;
		else if ( strcmp( v, "photon"    ) == 0 ) m_nPrefWeapon = BOTWEAPON_PHOTON;
		else {
			MSGOUT( "BOT[%d] weapon: unknown value '%s' (none|laser2|laser3|helix|lightning|photon)", m_nClientID, v );
			return false;
		}
		MSGOUT( "BOT[%d] weapon = %s", m_nClientID, v );
		return true;
	}

	if ( strcmp( param, "missile" ) == 0 ) {
		const char* v = value ? value : "none";
		if      ( strcmp( v, "none"  ) == 0 ) m_nPrefMissile = BOTMISSILE_NONE;
		else if ( strcmp( v, "dumb"  ) == 0 ) m_nPrefMissile = BOTMISSILE_DUMB;
		else if ( strcmp( v, "guide" ) == 0 ) m_nPrefMissile = BOTMISSILE_GUIDE;
		else if ( strcmp( v, "swarm" ) == 0 ) m_nPrefMissile = BOTMISSILE_SWARM;
		else {
			MSGOUT( "BOT[%d] missile: unknown value '%s' (none|dumb|guide|swarm)", m_nClientID, v );
			return false;
		}
		MSGOUT( "BOT[%d] missile = %s", m_nClientID, v );
		return true;
	}

	if ( strcmp( param, "miner" ) == 0 ) {
		m_bMineLayer = ( value && atoi( value ) != 0 );
		MSGOUT( "BOT[%d] miner = %d", m_nClientID, (int)m_bMineLayer );
		return true;
	}

	if ( strcmp( param, "retreathp" ) == 0 ) {
		float v = (float)atof( value ? value : "10" );
		if ( v < 1.0f )  v = 1.0f;
		if ( v > 99.0f ) v = 99.0f;
		m_fRetreatHP = v / 100.0f;
		MSGOUT( "BOT[%d] retreathp = %.0f%%", m_nClientID, v );
		return true;
	}

	MSGOUT( "BOT[%d] unknown config param '%s'", m_nClientID, param );
	return false;
}


// print the bot's current config to the console ------------------------------
//
void E_BotPlayer::PrintConfig() const
{
	static const char* kWeaponNames[]  = { "none", "laser2", "laser3", "helix", "lightning", "photon" };
	static const char* kMissileNames[] = { "none", "dumb", "guide", "swarm" };

	const char* wname = ( m_nPrefWeapon  >= 0 && m_nPrefWeapon  <= 5 ) ? kWeaponNames[ m_nPrefWeapon ]  : "?";
	const char* mname = ( m_nPrefMissile >= 0 && m_nPrefMissile <= 3 ) ? kMissileNames[ m_nPrefMissile ] : "?";

	char buf[ 256 ];
	snprintf( buf, sizeof(buf),
	          "BOT[%d] '%s'  empdly=%.2f  weapon=%s  missile=%s  miner=%d  retreathp=%.0f%%",
	          m_nClientID, m_szName,
	          m_fEMPMinDelay, wname, mname, (int)m_bMineLayer,
	          m_fRetreatHP * 100.0f );
	MSGOUT( "%s", buf );
}


// ---------------------------------------------------------------------------
// E_BotManager
// ---------------------------------------------------------------------------

// add a new bot with the given name -----------------------------------------
//
bool E_BotManager::AddBot( const char* name, int shipClassIdx )
{
	ASSERT( name != NULL );

	// allocate a player slot
	int nClientID = TheConnManager->ConnectBotClient( name );
	if ( nClientID < 0 ) {
		MSGOUT( "E_BotManager::AddBot(): ConnectBotClient failed for '%s'", name );
		return false;
	}

	// perform the in-world join (shipClassIdx=-1 → random inside BotPerformJoin)
	E_SimPlayerInfo* pSPI = TheSimulator->GetSimPlayerInfo( nClientID );
	ASSERT( pSPI != NULL );
	pSPI->BotPerformJoin( name, shipClassIdx );

	if ( !pSPI->IsPlayerJoined() ) {
		MSGOUT( "E_BotManager::AddBot(): BotPerformJoin failed for '%s'", name );
		return false;
	}

	// grow the bot array
	E_BotPlayer* pNewBots = new E_BotPlayer[ m_nNumBots + 1 ];
	for ( int i = 0; i < m_nNumBots; i++ ) {
		pNewBots[ i ] = m_Bots[ i ];
	}
	delete[] m_Bots;
	m_Bots = pNewBots;

	// init the new entry
	G_Player* pPlayer = TheGame->GetPlayer( nClientID );
	ASSERT( pPlayer != NULL );

	E_SimClientState* pSimState = TheSimulator->GetSimClientState( nClientID );
	ASSERT( pSimState != NULL );

	ShipObject* pShip = pSPI->GetShipObject();
	ASSERT( pShip != NULL );

	m_Bots[ m_nNumBots ].Init( nClientID, name, pPlayer, pSimState, pShip );
	m_nNumBots++;

	MSGOUT( "E_BotManager: added bot '%s' (slot %d), total bots: %d", name, nClientID, m_nNumBots );
	return true;
}


// remove a bot by client slot -----------------------------------------------
//
bool E_BotManager::RemoveBot( int nClientID )
{
	for ( int i = 0; i < m_nNumBots; i++ ) {
		if ( m_Bots[ i ].GetClientID() == nClientID ) {

			// disconnect the slot (will unjoin and destroy ship)
			TheConnManager->DisconnectClient( nClientID );

			// compact the array
			for ( int j = i; j < m_nNumBots - 1; j++ ) {
				m_Bots[ j ] = m_Bots[ j + 1 ];
			}
			m_nNumBots--;

			MSGOUT( "E_BotManager: removed bot in slot %d, total bots: %d", nClientID, m_nNumBots );
			return true;
		}
	}

	MSGOUT( "E_BotManager::RemoveBot(): no bot found in slot %d", nClientID );
	return false;
}


// run AI ticks for all bots --------------------------------------------------
//
void E_BotManager::Tick( refframe_t refframes )
{
	if ( m_nNumBots == 0 ) return;

	m_nFrameCounter++;
	if ( m_nFrameCounter < m_nThinkInterval ) return;
	m_nFrameCounter = 0;

	for ( int i = 0; i < m_nNumBots; i++ ) {
		m_Bots[ i ].KeepAlive();
		m_Bots[ i ].DoThink( refframes * m_nThinkInterval );
	}
}


// enable / disable debug output for a bot by client slot --------------------
//
bool E_BotManager::SetBotDebug( int nClientID, bool b )
{
	for ( int i = 0; i < m_nNumBots; i++ ) {
		if ( m_Bots[ i ].GetClientID() == nClientID ) {
			m_Bots[ i ].SetDebug( b );
			MSGOUT( "E_BotManager: debug %s for bot in slot %d",
			        b ? "enabled" : "disabled", nClientID );
			return true;
		}
	}
	MSGOUT( "E_BotManager::SetBotDebug(): no bot found in slot %d", nClientID );
	return false;
}


// find a bot by client slot (returns NULL if not found) ----------------------
//
E_BotPlayer* E_BotManager::GetBotByClientID( int nClientID )
{
	for ( int i = 0; i < m_nNumBots; i++ ) {
		if ( m_Bots[ i ].GetClientID() == nClientID )
			return &m_Bots[ i ];
	}
	return NULL;
}


// configure a per-bot personality parameter ----------------------------------
// param == NULL → print current config; value ignored for miner/weapon/missile
// when value == NULL the parameter is reset to its default.
//
bool E_BotManager::ConfigBot( int nClientID, const char* param, const char* value )
{
	E_BotPlayer* pBot = GetBotByClientID( nClientID );
	if ( pBot == NULL ) {
		MSGOUT( "E_BotManager::ConfigBot(): no bot in slot %d", nClientID );
		return false;
	}

	if ( param == NULL ) {
		pBot->PrintConfig();
		return true;
	}

	return pBot->ApplyConfig( param, value );
}


// ---------------------------------------------------------------------------
// Console commands: sv.bot.add <name>  /  sv.bot.remove <slot>  /  sv.bot.debug <slot> <1/0>
// ---------------------------------------------------------------------------

// defined in e_gameserver.cpp so TheServer is accessible
extern E_BotManager* SV_GetBotManager();


PRIVATE
int Cmd_SVBOT_ADD( char* pszArgs )
{
	ASSERT( pszArgs != NULL );
	HANDLE_COMMAND_DOMAIN_SEP( pszArgs );

	// trim leading whitespace
	while ( *pszArgs == ' ' ) pszArgs++;

	if ( *pszArgs == '\0' ) {
		CON_AddLine( "usage: sv.bot.add <name> [ship]" );
		CON_AddLine( "  ship: 0-based index or omit for random" );
		// list available ship classes
		char linebuf[ 128 ];
		for ( int i = 0; i < NumShipClasses; i++ ) {
			snprintf( linebuf, sizeof(linebuf), "  %d: %s",
			          i, ObjectInfo[ ShipClasses[ i ] ].name );
			CON_AddLine( linebuf );
		}
		return TRUE;
	}

	// split on the last space: everything before is name, after is optional ship index
	char name[ 64 ] = {};
	int  shipIdx    = -1;   // -1 = random

	// find the last space-separated token and try to parse it as a number
	char* pLastSpace = strrchr( pszArgs, ' ' );
	if ( pLastSpace != NULL && pLastSpace != pszArgs ) {
		char* pShipArg = pLastSpace + 1;
		char* pEnd     = NULL;
		long  val      = strtol( pShipArg, &pEnd, 10 );
		if ( pEnd != pShipArg && *pEnd == '\0' && val >= 0 && val < NumShipClasses ) {
			// valid index — split name here
			int nameLen = (int)( pLastSpace - pszArgs );
			if ( nameLen >= (int)sizeof(name) ) nameLen = (int)sizeof(name) - 1;
			strncpy( name, pszArgs, nameLen );
			name[ nameLen ] = '\0';
			shipIdx = (int)val;
		}
	}
	if ( name[ 0 ] == '\0' ) {
		// no valid ship token — whole arg is the name
		strncpy( name, pszArgs, sizeof(name) - 1 );
		name[ sizeof(name) - 1 ] = '\0';
	}

	if ( SV_GetBotManager()->AddBot( name, shipIdx ) ) {
		char msg[ 80 ];
		if ( shipIdx >= 0 ) {
			snprintf( msg, sizeof(msg), "bot added (ship: %s).",
			          ObjectInfo[ ShipClasses[ shipIdx ] ].name );
		} else {
			snprintf( msg, sizeof(msg), "bot added." );
		}
		CON_AddLine( msg );
	} else {
		CON_AddLine( "failed to add bot (server full?)." );
	}
	return TRUE;
}


PRIVATE
int Cmd_SVBOT_REMOVE( char* pszSlot )
{
	ASSERT( pszSlot != NULL );
	HANDLE_COMMAND_DOMAIN_SEP( pszSlot );

	int slot = -1;
	sscanf( pszSlot, " %d", &slot );
	if ( slot < 0 ) {
		CON_AddLine( "usage: sv.bot.remove <slot>" );
		return TRUE;
	}

	if ( SV_GetBotManager()->RemoveBot( slot ) ) {
		CON_AddLine( "bot removed." );
	} else {
		CON_AddLine( "no bot found in that slot." );
	}
	return TRUE;
}


PRIVATE
int Cmd_SVBOT_DEBUG( char* pszArgs )
{
	ASSERT( pszArgs != NULL );
	HANDLE_COMMAND_DOMAIN_SEP( pszArgs );

	int slot = -1, enable = -1;
	if ( sscanf( pszArgs, " %d %d", &slot, &enable ) != 2 || slot < 0 || ( enable != 0 && enable != 1 ) ) {
		CON_AddLine( "usage: sv.bot.debug <slot> <1/0>" );
		return TRUE;
	}

	if ( SV_GetBotManager()->SetBotDebug( slot, enable != 0 ) ) {
		CON_AddLine( enable ? "bot debug enabled." : "bot debug disabled." );
	} else {
		CON_AddLine( "no bot found in that slot." );
	}
	return TRUE;
}


PRIVATE
int Cmd_SVBOT_CONFIG( char* pszArgs )
{
	ASSERT( pszArgs != NULL );
	HANDLE_COMMAND_DOMAIN_SEP( pszArgs );

	// trim leading whitespace
	while ( *pszArgs == ' ' ) pszArgs++;

	if ( *pszArgs == '\0' ) {
		CON_AddLine( "usage: sv.bot.config <slot> [param] [value]" );
		CON_AddLine( "  params: empdly <sec>  weapon <none|laser2|laser3|helix|lightning|photon>" );
		CON_AddLine( "          missile <none|dumb|guide|swarm>  miner <0|1>  retreathp <pct>" );
		CON_AddLine( "  omit param to print current config for that slot" );
		return TRUE;
	}

	// parse slot number
	char* pEnd = NULL;
	long slot = strtol( pszArgs, &pEnd, 10 );
	if ( pEnd == pszArgs || slot < 0 ) {
		CON_AddLine( "sv.bot.config: invalid slot number" );
		return TRUE;
	}

	// skip whitespace after slot
	while ( *pEnd == ' ' ) pEnd++;

	if ( *pEnd == '\0' ) {
		// no param — print config
		if ( !SV_GetBotManager()->ConfigBot( (int)slot, NULL, NULL ) )
			CON_AddLine( "no bot found in that slot." );
		return TRUE;
	}

	// parse param name
	char param[ 32 ] = {};
	const char* pParam = pEnd;
	int plen = 0;
	while ( *pEnd != '\0' && *pEnd != ' ' && plen < (int)sizeof(param) - 1 )
		param[ plen++ ] = *pEnd++;
	param[ plen ] = '\0';

	// skip whitespace before value
	while ( *pEnd == ' ' ) pEnd++;
	const char* pValue = ( *pEnd != '\0' ) ? pEnd : NULL;

	if ( !SV_GetBotManager()->ConfigBot( (int)slot, param, pValue ) )
		CON_AddLine( "no bot found in that slot, or unknown param." );

	return TRUE;
}


// register server bot console commands (called from E_GameServer::Init) -----
//
void SV_BotManager_RegisterCommands()
{
	user_command_s regcom;
	memset( &regcom, 0, sizeof( user_command_s ) );

	regcom.command   = "sv.bot.add";
	regcom.numparams = 1;
	regcom.execute   = Cmd_SVBOT_ADD;
	regcom.statedump = NULL;
	CON_RegisterUserCommand( &regcom );

	regcom.command   = "sv.bot.remove";
	regcom.numparams = 1;
	regcom.execute   = Cmd_SVBOT_REMOVE;
	regcom.statedump = NULL;
	CON_RegisterUserCommand( &regcom );

	regcom.command   = "sv.bot.debug";
	regcom.numparams = 1;
	regcom.execute   = Cmd_SVBOT_DEBUG;
	regcom.statedump = NULL;
	CON_RegisterUserCommand( &regcom );

	regcom.command   = "sv.bot.config";
	regcom.numparams = 1;
	regcom.execute   = Cmd_SVBOT_CONFIG;
	regcom.statedump = NULL;
	CON_RegisterUserCommand( &regcom );
}
