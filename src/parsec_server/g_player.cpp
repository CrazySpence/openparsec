/*
 * PARSEC - Player logic -
 *
 * $Author: uberlinuxguy $ - $Date: 2004/09/15 12:25:45 $
 *
 * Orginally written by:
 *   Copyright (c) Clemens Beer        <cbx@parsec.org>   2002
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
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// compilation flags/debug support
#include "config.h"
#include "debug.h"

// general definitions
#include "general.h"
#include "objstruc.h"

// global externals
#include "globals.h"

// subsystem & headers
#include "net_defs.h"
#include "e_defs.h"

// mathematics header
#include "utl_math.h"

// utility headers
#include "utl_list.h"

// local module header
#include "g_player.h"

// proprietary module headers
#include "con_aux_sv.h"
//#include "g_extra.h"
//#include "net_game_sv.h"
//#include "net_util.h"
//#include "obj_clas.h"
//#include "obj_creg.h"
//#include "obj_name.h"
//#include "od_props.h"
//#include "od_class.h"
#include "e_simplayerinfo.h"
#include "g_main_sv.h"
//#include "g_stgate.h"
//#include "e_connmanager.h"
#include "e_gameserver.h"
#include "e_simulator.h"
#include "e_simnetoutput.h"
//#include "sys_refframe_sv.h"

#include "g_emp.h"
#include "g_wfx.h"

// reset all game variables ---------------------------------------------------
//
void G_Player::ResetGameVars()
{
      m_nKills                = 0;
      m_nDeaths               = 0;
      m_nPoints               = 0;
      m_nLastUnjoinFlag       = USER_EXIT;
      m_nLastKiller           = KILLERID_UNKNOWN;

      m_FireDisableFrames     = 1;
      m_MissileDisableFrames  = 1;

      m_CurGun                = 0;
      m_CurLauncher           = 0;
}


// reset all fields to defaults ( not connected ) -----------------------------
//
void G_Player::Reset()
{
      ResetGameVars();
      m_nClientID	= -1;
      m_pSimPlayerInfo	= NULL;
}


// set the player status to connected -----------------------------------------
//
void G_Player::Connect( int nClientID )
{
      Reset();
      m_nClientID	= nClientID;
      m_pSimPlayerInfo = TheSimulator->GetSimPlayerInfo( nClientID );
}


// set the player status to disconnected --------------------------------------
//
void G_Player::Disconnect()
{
      Reset();
}


// user fired laser -----------------------------------------------------------
//
void G_Player::FireLaser()
{
      // check whether laser firing is valid
      if ( m_FireDisableFrames > 0 ) {
       //       MSGOUT( "G_Player::FireLaser(): laser firing disabled: m_FireDisableFrames: %d", m_FireDisableFrames );
              return;
      }
     // MSGOUT( "G_Player::FireLaser(): laser firing OK: m_FireDisableFrames: %d", m_FireDisableFrames );

      // create laser
      _OBJ_ShootLaser();

      ShipObject* pShip = m_pSimPlayerInfo->GetShipObject();
      if ( ( m_FireDisableFrames += pShip->FireDisableDelay ) <= 0 ) {
              m_FireDisableFrames = 1;
      }
}

//user fired helix cannon

void G_Player::FireHelix()
{
	ShipObject* pShip = m_pSimPlayerInfo->GetShipObject();
    if ( ( pShip->WeaponsActive & WPMASK_CANNON_HELIX ) == 0 ) {
		WFX_ActivateHelix( pShip );
	}
}


// user fired lightning device ------------------------------------------------
//
void G_Player::FireLightning()
{
    ShipObject* pShip = m_pSimPlayerInfo->GetShipObject();
	if ( ( pShip->WeaponsActive & WPMASK_CANNON_LIGHTNING ) == 0 ) {
		WFX_ActivateLightning( pShip );
	}
}


// user fired photon cannon ---------------------------------------------------
//
void G_Player::FirePhoton()
{
    ShipObject* pShip = m_pSimPlayerInfo->GetShipObject();
	if ( (pShip->WeaponsActive & WPMASK_CANNON_PHOTON ) == 0 ) {
		WFX_ActivatePhoton( pShip );
	}
}

// user launched dumb missle -----------------------------------------------------------
//
void G_Player::LaunchMissile()
{
      // check whether missle launching is valid
      if ( m_MissileDisableFrames > 0 ) {
         //     MSGOUT( "G_Player::LaunchMissle(): missile launching disabled: m_MissileDisableFrames: %d", m_MissileDisableFrames );
              return;
      }
    //  MSGOUT( "G_Player::LaunchMissile(): Missile Launching OK: m_MissileDisableFrames: %d", m_MissileDisableFrames );

      // create missle
      _OBJ_LaunchMissile();

      ShipObject* pShip = m_pSimPlayerInfo->GetShipObject();
      if ( ( m_MissileDisableFrames += pShip->MissileDisableDelay ) <= 0 ) {
              m_MissileDisableFrames = 1;
      }
}
// user launched homing missle -----------------------------------------------------------
//
void G_Player::LaunchHomingMissile(dword launcher, dword targetid)
{

      // check whether missle launching is valid
      if ( m_MissileDisableFrames > 0 ) {
      //        MSGOUT( "G_Player::LaunchMissle(): missile launching disabled: m_MissileDisableFrames: %d", m_MissileDisableFrames );
              return;
      }
      //MSGOUT( "G_Player::LaunchHomingMissile(): Missile Launching OK: m_MissileDisableFrames: %d", m_MissileDisableFrames );

      // create missle
      _OBJ_LaunchHomingMissile(launcher, targetid);

      ShipObject* pShip = m_pSimPlayerInfo->GetShipObject();
      if ( ( m_MissileDisableFrames += pShip->MissileDisableDelay ) <= 0 ) {
              m_MissileDisableFrames = 1;
      }
}

void G_Player::LaunchMine()
{

	//MSGOUT( "G_Player::LaunchMine(); Mine Launch Ok");

	// create the mine object
	_OBJ_LaunchMine();
}

void G_Player::LaunchSwarm( dword targetid )
{
      // check whether missle launching is valid
      if ( m_MissileDisableFrames > 0 ) {
              //MSGOUT( "G_Player::LaunchSwarm(): Swarm missile launching disabled: m_MissileDisableFrames: %d", m_MissileDisableFrames );
              return;
      }
      //MSGOUT( "G_Player::LaunchSwarm(): Swarm Missile Launching OK: m_MissileDisableFrames: %d", m_MissileDisableFrames );

      // create missle
      _OBJ_LaunchSwarm( targetid );

      ShipObject* pShip = m_pSimPlayerInfo->GetShipObject();
      if ( ( m_MissileDisableFrames += pShip->MissileDisableDelay ) <= 0 ) {
              m_MissileDisableFrames = 1;
      }
}

// create laser originating from specified ship -------------------------------
//
void G_Player::_OBJ_ShootLaser()
{
	//NOTE: based on OBJ_GAME::OBJ_ShootLaser

	ASSERT( m_pSimPlayerInfo != NULL );
	ShipObject* pShip = m_pSimPlayerInfo->GetShipObject();
	ASSERT( pShip != NULL );

	//NOTE:
	// C: this function is called by INP_USER::User_CheckGunFire().
	// S: this function is called by G_Input::User_FireLaser

	//FIXME: GAMEVAR ( property of ship )
	#define MIN_LASER_ENERGY		20

#ifdef PARSEC_SERVER

	// check for availability
	if ( !TheGame->OBJ_DeviceAvailable( pShip, WPMASK_CANNON_LASER ) ) {
		return;
	}

#else
	// check if enough space in RE_List
	if ( !NET_RmEvAllowed( RE_CREATELASER ) )
		return;

	// check for availability
	if ( !OBJ_DeviceAvailable( pShip, WPMASK_CANNON_LASER ) ) {
		if ( pShip == MyShip ) {
			ShowMessage( no_standard_str );
		}
		return;
	}
#endif // PARSEC_SERVER

	// determine current laser upgrade level
	int curlevel = 0;
	if ( pShip->Specials & SPMASK_LASER_UPGRADE_2 ) {
		curlevel = 2;
	} else if ( pShip->Specials & SPMASK_LASER_UPGRADE_1 ) {
		curlevel = 1;
	}

	// sequence of gun (laser) barrels ----------------------------------------
	//
	#define MAX_GUN_BARRELS		4
	static int gun_barrels_sequence[ MAX_GUN_BARRELS ]	= { 0, 3, 1, 2 };

	int barrel = gun_barrels_sequence[ m_CurGun % MAX_GUN_BARRELS ];
	dword laserclass = pShip->Laser1_Class[ curlevel ][ barrel ];

	if ( !SV_CHEAT_ENERGY_CHECKS ) {

		// check if enough energy to shoot laser
		//FIXME: TheWorld->ObjClasses ??????????????
		int energyneeded = ( (LaserObject*)TheWorld->ObjClasses[ laserclass ] )->EnergyNeeded;
		if ( ( pShip->CurEnergy - energyneeded ) < MIN_LASER_ENERGY ) {

#ifdef PARSEC_SERVER
			//MSGOUT( "G_Player::_OBJ_ShootLaser(): client %d low energy", m_nClientID );
#else // !PARSEC_SERVER
			if ( pShip == MyShip ) {
				ShowMessage( low_energy_str );
				AUD_LowEnergy();
			}
#endif // !PARSEC_SERVER
			
			return;
		}
		pShip->CurEnergy -= energyneeded;
		//MSGOUT( "G_Player::_OBJ_ShootLaser(): client %d energy after shot: %d", m_nClientID, pShip->CurEnergy );
	}

	// create actual laser object(s)
	TheGame->OBJ_CreateLaserObject( pShip, curlevel, barrel, m_nClientID );
	m_CurGun++;

	if ( curlevel == 2 ) {
		barrel = gun_barrels_sequence[ m_CurGun % MAX_GUN_BARRELS ];
		TheGame->OBJ_CreateLaserObject( pShip, curlevel, barrel, m_nClientID );
		m_CurGun++;
	}

#ifdef PARSEC_CLIENT
	// play sound effect
	AUD_Laser( laserpo );
#endif // PARSEC_CLIENT
}

// create missile originating from specified position -------------------------
//
void G_Player::_OBJ_LaunchMissile()
{
	ASSERT( m_pSimPlayerInfo != NULL );
	ShipObject* pShip = m_pSimPlayerInfo->GetShipObject();
	ASSERT( pShip != NULL );

	//NOTE: based on OBJ_GAME::OBJ_LaunchMissle


	// check ammo
	if ( pShip->NumMissls <= 0 ) {
		return;
	}
	pShip->NumMissls--;
	
	
	#define MAX_MISSILE_BARRELS_MASK 	4
	static int missile_barrel_sequence[ MAX_MISSILE_BARRELS_MASK ]	= { 0, 3, 1, 2 };
	
	// select correct barrel
	int barrel       = missile_barrel_sequence[ m_CurLauncher%MAX_MISSILE_BARRELS_MASK ];
	
	TheGame->OBJ_CreateMissileObject( pShip, barrel, m_nClientID );
	m_CurLauncher++;
#ifdef PARSEC_CLIENT
	// insert remote event
	NET_RmEvMissile( missilepo, 0 );

	// record create event if recording active
	Record_MissileCreation( missilepo );

	// play sound effect
	AUD_Missile( missilepo );
#endif
}


// create homing missile originating from specified position ------------------
//
void G_Player::_OBJ_LaunchHomingMissile( dword launcher, dword targetid )
{
	ASSERT( m_pSimPlayerInfo != NULL );
	ShipObject* pShip = m_pSimPlayerInfo->GetShipObject();
	ASSERT( pShip != NULL );


	// check ammo
	if ( pShip->NumHomMissls <= 0 ) {
            return;
	}
	
	
	/* check if any target locked
	if (!targetid ) {
		MSGOUT("NO TARGET Targetid: %d", targetid); //This is server side, to be here a client has already fired at a target
		return;
	}*/ 
	pShip->NumHomMissls--;


	#define MAX_MISSILE_BARRELS_MASK 	4
	static int missile_barrel_sequence[ MAX_MISSILE_BARRELS_MASK ]	= { 0, 3, 1, 2 };
	
	// select correct barrel
	int barrel       = missile_barrel_sequence[ m_CurLauncher%MAX_MISSILE_BARRELS_MASK ];
	
	TheGame->OBJ_CreateHomingMissileObject( pShip, barrel, m_nClientID, targetid );
	m_CurLauncher++;



}


// create mine object ---------------------------------------------------------
//
void G_Player::_OBJ_LaunchMine()
{
	ASSERT( m_pSimPlayerInfo != NULL );
	ShipObject* pShip = m_pSimPlayerInfo->GetShipObject();
	ASSERT( pShip != NULL );

	// check ammo
	if ( pShip->NumMines <= 0 ) {
		return;
	}
	pShip->NumMines--;
	TheGame->OBJ_CreateMineObject( pShip, m_nClientID );
	

}


// create swarm missiles ------------------------------------------------------
//
void G_Player::_OBJ_LaunchSwarm( dword targetid )
{

	ASSERT( m_pSimPlayerInfo != NULL );
	ShipObject* pShip = m_pSimPlayerInfo->GetShipObject();
	ASSERT( pShip != NULL );

	// check ammo
	if ( pShip->NumPartMissls <= 0 ) {
		return;
	}

	TheGame->OBJ_CreateSwarm(pShip, m_nClientID, targetid);
    
   	pShip->NumPartMissls--;

}

void G_Player::FireEMP(byte Upgradelevel, bool broadcastToClients) {
	ASSERT( m_pSimPlayerInfo != NULL );
	ShipObject* pShip = m_pSimPlayerInfo->GetShipObject();
	ASSERT( pShip != NULL );
	int curdelay = 0;
	int energy_consumption = emp_energy[ Upgradelevel ] * emp_waves[ Upgradelevel ];

	// check if enough energy to shoot emp
	if ( pShip->CurEnergy >= energy_consumption  ) {

		pShip->CurEnergy -= energy_consumption;

		for ( int i = 0; i < emp_waves[ Upgradelevel ]; i++ ) {
			CreateEmp( pShip, curdelay, 0, Upgradelevel, m_nClientID );
			curdelay += emp_delay[ Upgradelevel ];
		}

		// Server-internal bots have no network path that multicasts RE_CreateEmp
		// to clients (e_simnetinput.cpp only runs for real network clients). Build
		// and multicast it here so all clients render the EMP visual effect.
		// Real-client callers pass broadcastToClients=false because e_simnetinput.cpp
		// already multicasts the original received RE right after calling FireEMP.
		if ( broadcastToClients ) {
			RE_CreateEmp re;
			memset( &re, 0, sizeof( re ) );
			re.RE_Type      = RE_CREATEEMP;
			re.RE_BlockSize = sizeof( RE_CreateEmp );
			re.SenderId     = m_nClientID;
			re.Upgradelevel = Upgradelevel;
			TheSimNetOutput->BufferForMulticastRE( (RE_Header*)&re, m_nClientID, FALSE );
		}
	}
}

// record a kill --------------------------------------------------------------
//
void G_Player::RecordKill() 
{
	if ( m_pSimPlayerInfo->IsPlayerJoined() ) {
		m_nKills++;
	}
}


// record a death -------------------------------------------------------------
//
void G_Player::RecordDeath( int nClientID_Killer ) 
{
	ASSERT( ( nClientID_Killer >= 0 ) && ( nClientID_Killer < MAX_NUM_CLIENTS ) );
	if ( m_pSimPlayerInfo->IsPlayerJoined() ) {
		m_nDeaths++;
		m_nLastUnjoinFlag	= SHIP_DOWNED;
		m_nLastKiller		= nClientID_Killer;
        WFX_EnsureParticleWeaponsInactive( m_pSimPlayerInfo->GetShipObject() );
	}
}


// reset any death info for the player ----------------------------------------
//
void G_Player::ResetDeathInfo()
{
	m_nLastUnjoinFlag		= USER_EXIT;
	m_nLastKiller			= KILLERID_UNKNOWN;
}


// return the ship object assigned to the player ------------------------------
//
ShipObject* G_Player::GetShipObject() 
{ 
	ASSERT( m_pSimPlayerInfo != NULL );
	return m_pSimPlayerInfo->GetShipObject(); 
}

// maintain weapon firing delays ----------------------------------------------
//
void G_Player::MaintainWeaponDelays()
{
	// count down fire disable delay
	if ( m_FireDisableFrames > 0 ) {
		m_FireDisableFrames -= TheSimulator->GetThisFrameRefFrames();
	}
	if ( m_MissileDisableFrames > 0 ) {
		m_MissileDisableFrames -= TheSimulator->GetThisFrameRefFrames();
	}
}


