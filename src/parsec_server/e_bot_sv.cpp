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
#include "g_player.h"
#include "g_main_sv.h"
#include "e_world_trans.h"		// FetchFirstShip, FetchFirstExtra


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

	// Respawn if the ship was destroyed (collision det calls PerformUnjoin on kill)
	if ( !pSPI->IsPlayerJoined() ) {
		pSPI->BotPerformJoin( m_szName );
		m_pShip    = pSPI->GetShipObject();
		m_fCurSpeed = 0;
		m_Goal.Reset();
		m_nAgentMode = AGENTMODE_ATTACK;
		if ( m_pShip ) {
			m_oc.pShip = m_pShip;
			FetchTVector( m_pShip->ObjPosition, &m_AgentPos );
		}
		return;  // skip AI this tick; let the new ship settle
	}

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
	if ( m_pShip->CurDamage > ( m_pShip->MaxDamage * SV_BOT_REPAIR_LEVEL ) ) {
		m_nAgentMode = AGENTMODE_RETREAT;
		return;
	}
	if ( m_pShip->NumHomMissls < 1 ) {
		m_nAgentMode = AGENTMODE_RETREAT;
		return;
	}
	if ( m_pShip->CurEnergy < ( m_pShip->MaxEnergy * SV_BOT_ENERGY_LEVEL ) ) {
		m_nAgentMode = AGENTMODE_RETREAT;
		return;
	}

	// if there are other joined players, attack one
	if ( TheGame->GetNumJoined() > 1 ) {
		ShipObject* pTarget = _SelectAttackTarget();
		if ( pTarget != NULL ) {
			m_nAgentMode = AGENTMODE_ATTACK;
			return;
		}
	}

	// look for powerups
	ExtraObject* pExtra = FetchFirstExtra();
	if ( pExtra != NULL ) {
		m_nAgentMode = AGENTMODE_POWERUP;
	} else {
		m_nAgentMode = AGENTMODE_IDLE;
	}
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


// powerup: navigate to nearest extra -----------------------------------------
//
void E_BotPlayer::_GoalCheck_Powerup()
{
	Vector3* pGoalPos = m_Goal.GetGoalPosition();

	if ( m_Goal.GetTargetObject() == NULL ) {
		ExtraObject* pObject = FetchFirstExtra();
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
	float len = VctLenX( &vec2Target );

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
	GenObject* pTargetObject = m_Goal.GetTargetObject();

	if ( ( pTargetObject == NULL ) || !OBJECT_TYPE_SHIP( pTargetObject ) ) {
		pTargetObject = _SelectAttackTarget();
		if ( pTargetObject == NULL ) {
			m_nAgentMode = AGENTMODE_IDLE;
			m_Goal.SetTargetObject( NULL );
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
	float len = VctLenX( &vec2Target );

	// weapon firing
	if ( _TargetInRange( m_pShip, (ShipObject*) pTargetObject, 1500.0f ) ) {
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
			m_pPlayer->FireEMP( 0 );
			m_fEMPDelay = 1.0f;
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

	if ( m_Goal.GetTargetObject() == NULL ) {

		if ( m_pShip->NumHomMissls < (int)( m_pShip->MaxNumHomMissls * SV_BOT_GMISSL_LEVEL ) ) {
			pObject = _SelectMissileObject();
		}
		if ( m_pShip->CurEnergy < ( m_pShip->MaxEnergy * SV_BOT_ENERGY_LEVEL ) ) {
			pObject = _SelectEnergyObject();
		}
		if ( m_pShip->CurDamage > ( m_pShip->MaxDamage * SV_BOT_REPAIR_LEVEL ) ) {
			pObject = _SelectRepairObject();
		}

		if ( pObject == NULL ) {
			_DoPlan();
			return;
		}

		m_Goal.SetTargetObject( pObject );
		FetchTVector( pObject->ObjPosition, pGoalPos );
	}

	pGoalPos = m_Goal.GetGoalPosition();

	Vector3 vec2Target;
	VECSUB( &vec2Target, pGoalPos, &m_AgentPos );
	float len = VctLenX( &vec2Target );

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
	bams_t yaw   = (bams_t)( pObjctl->rot_y * (float)m_pShip->YawPerRefFrame   );
	bams_t pitch = (bams_t)( pObjctl->rot_x * (float)m_pShip->PitchPerRefFrame );
	bams_t roll  = (bams_t)( pObjctl->rot_z * (float)m_pShip->RollPerRefFrame  );

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
	ShipObject* pBestTarget = NULL;
	int         nBestHP     = 0;

	for ( ShipObject* pShip = FetchFirstShip(); pShip != NULL;
	      pShip = (ShipObject*) pShip->NextObj ) {

		if ( pShip == m_pShip ) continue;   // never target self

		int nHP = pShip->MaxDamage - pShip->CurDamage;
		if ( pBestTarget == NULL || nHP > nBestHP ) {
			pBestTarget = pShip;
			nBestHP     = nHP;
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


// ---------------------------------------------------------------------------
// E_BotManager
// ---------------------------------------------------------------------------

// add a new bot with the given name -----------------------------------------
//
bool E_BotManager::AddBot( const char* name )
{
	ASSERT( name != NULL );

	// allocate a player slot
	int nClientID = TheConnManager->ConnectBotClient( name );
	if ( nClientID < 0 ) {
		MSGOUT( "E_BotManager::AddBot(): ConnectBotClient failed for '%s'", name );
		return false;
	}

	// perform the in-world join
	E_SimPlayerInfo* pSPI = TheSimulator->GetSimPlayerInfo( nClientID );
	ASSERT( pSPI != NULL );
	pSPI->BotPerformJoin( name );

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


// ---------------------------------------------------------------------------
// Console commands: sv.bot.add <name>  /  sv.bot.remove <slot>
// ---------------------------------------------------------------------------

// defined in e_gameserver.cpp so TheServer is accessible
extern E_BotManager* SV_GetBotManager();


PRIVATE
int Cmd_SVBOT_ADD( char* pszName )
{
	ASSERT( pszName != NULL );
	HANDLE_COMMAND_DOMAIN_SEP( pszName );

	// trim leading whitespace
	while ( *pszName == ' ' ) pszName++;

	if ( *pszName == '\0' ) {
		CON_AddLine( "usage: sv.bot.add <name>" );
		return TRUE;
	}

	if ( SV_GetBotManager()->AddBot( pszName ) ) {
		CON_AddLine( "bot added." );
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
}
