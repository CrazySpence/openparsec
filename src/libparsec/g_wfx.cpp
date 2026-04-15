/*
 * PARSEC - Weapons Effects
 *
 * $Author: uberlinuxguy $ - $Date: 2004/09/26 03:43:37 $
 *
 * Orginally written by:
 *   Copyright (c) Markus Hadwiger     <msh@parsec.org>   1997-1999
 *   Copyright (c) Michael Woegerbauer <maiki@parsec.org> 1999-2000
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
#include <math.h>
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

// subsystem headers
#include "net_defs.h"

// mathematics header
#include "utl_math.h"

// particle types
#include "parttype.h"

// local module header
#include "g_wfx.h"

#ifndef PARSEC_SERVER

// client-only subsystem headers
#include "aud_defs.h"
#include "vid_defs.h"

// client-only proprietary module headers
#include "con_aux.h"
#include "e_color.h"
#include "e_record.h"
#include "h_supp.h"
#include "obj_ctrl.h"
#include "obj_game.h"
#include "part_ani.h"
#include "part_api.h"
#include "part_def.h"

#else // PARSEC_SERVER

#include "e_global_sv.h"
#include "e_simulator.h"
#include "g_main_sv.h"

#endif // PARSEC_SERVER



// flags
#define SECOND_HELIX



// lightning energy properties
#define MIN_LIGHTNING_ENERGY				10
#define LIGHTNING_ENERGY_CONSUMPTION		7000          // 65536 = 1.0

// properties of lightning particles
#define LIGHTNING_BM_INDX					BM_LIGHTNING1
#define LIGHTNING_REF_Z						9.0f
#define LIGHTNING_PARTICLE_COLOR			255
#ifndef PARSEC_SERVER
#define LIGHTNING_SIZZLE_SPEED				60
#endif // !PARSEC_SERVER

// spread energy properties
#define MIN_SPREAD_ENERGY					10
#define SPREAD_ENERGY_CONSUMPTION			2

// helix energy properties
#define MIN_HELIX_ENERGY					10
#define HELIX_ENERGY_CONSUMPTION			2
#define HELIX_LIFETIME						1500

// properties of spreadfire particles
#define SPREADFIRE_PARTICLE_COLOR			174
#define SPREADFIRE_BM_INDX					BM_FIREBALL1
#define SPREADFIRE_REF_Z					20.0f

// photon properties
#define MIN_PHOTON_ENERGY                   10
#define PHOTON_ENERGY_CONSUMPTION           0x6000  // 0x10000 = 1.0

#define PHOTON_ROT_PITCH					0x0015
#define PHOTON_ROT_YAW						-0x0022
#define PHOTON_ROT_ROLL						0x000d

#define PHOTON_SPHERE_PARTICLES				256
#define PHOTON_COLOR						123
#define PHOTON_REF_Z						75.0
#define PHOTON_CONTRACTION_TIME				50
#define PHOTON_CONTRACTION_SPEED			FIXED_TO_GEOMV( 0xA000 )
#define PHOTON_MAX_LOADING_TIME				1800
#define PHOTON_NUMLOADS						32



#ifndef PARSEC_SERVER

// string constants -------------------------------------------------------
//
static char no_spreadfire_str[]		= "no spreadfire gun";
static char no_helixcannon_str[]	= "no helix cannon";
static char no_lightning_str[]		= "no lightning device";
static char no_photoncannon_str[]   = "no photon cannon";
static char low_energy_str[]		= "low energy";

#endif // !PARSEC_SERVER


// reference z values for particles (client-only: server has its own in e_world.cpp)
//
#ifndef PARSEC_SERVER
float	spreadfire_ref_z			= 1.0f;
float	lightning_ref_z				= 1.0f;
float	photon_ref_z				= 1.0f;
#endif // !PARSEC_SERVER


// helix props ----------------------------------------------------------------
//
bams_t  HelixBamsInc			= 0x1500;
int     HelixRefFrames			= 10;                     // create one particle every ... frames
geomv_t HelixRadius				= INT_TO_GEOMV( 10 );


#ifndef PARSEC_SERVER

// init reference z values for particles according to resolution --------------
//
void WFX_InitParticleSizes( float resoscale )
{
	spreadfire_ref_z = resoscale * SPREADFIRE_REF_Z;
	lightning_ref_z  = resoscale * LIGHTNING_REF_Z;
	photon_ref_z     = resoscale * PHOTON_REF_Z;
}


// create one load of spreadfire particles ------------------------------------
//
PRIVATE
int ShootSpreadfire( ShipObject *shippo, int owner )
{
	ASSERT( shippo != NULL );

	int bitmap	  = SPREADFIRE_BM_INDX;
	int color	  = SPREADFIRE_PARTICLE_COLOR;
	float ref_z = spreadfire_ref_z;
	int sizebound = partbitmap_size_bound;
	int lifetime  = shippo->SpreadLifeTime;

	pextinfo_s extinfo;
	pextinfo_s *pextinfo = NULL;

	if ( AUX_SPREADFIRE_PARTICLES_EXTINFO ) {

		// alter basic attributes
		ref_z    *= 2;
		bitmap    = iter_texrgba | iter_specularadd;
		sizebound = PRT_NO_SIZEBOUND;

		// fetch pdef
		pdef_s *pdef;
		pdef = PDEF_pflare01();
		if ( pdef == NULL ) {
			MSGOUT( "spreadfire particles invalid." );
			return FALSE;
		}

		// create pextinfo
		pextinfo = &extinfo;
		PRT_InitParticleExtInfo( pextinfo, pdef, NULL, NULL );
	}

	if ( shippo->CurEnergy < MIN_SPREAD_ENERGY + SPREAD_ENERGY_CONSUMPTION ) {
		if ( shippo == MyShip ) {
			ShowMessage( low_energy_str );
			AUD_LowEnergy();
		}
		return FALSE;
	}
	shippo->CurEnergy -= SPREAD_ENERGY_CONSUMPTION;

	// first particle ---
	Vertex3 object_space;
	object_space.X = shippo->Spread_X[ 0 ];
	object_space.Y = shippo->Spread_Y;
	object_space.Z = shippo->Spread_Z;

	Vertex3 world_space;
	MtxVctMUL( shippo->ObjPosition, &object_space, &world_space );

	Vector3 dirvec;
	fixed_t speed = shippo->SpreadSpeed + shippo->CurSpeed;
	DirVctMUL( shippo->ObjPosition, FIXED_TO_GEOMV( speed ), &dirvec );

	particle_s particle;
	PRT_InitParticle( particle, bitmap, color, sizebound,
					  ref_z, &world_space, &dirvec,
					  lifetime, owner, pextinfo );
	PRT_CreateLinearParticle( particle );

	// second particle ---
	object_space.X = shippo->Spread_X[ 3 ];
	MtxVctMUL( shippo->ObjPosition, &object_space, &world_space );

	PRT_InitParticle( particle, bitmap, color, sizebound,
					  ref_z, &world_space, &dirvec,
					  lifetime, owner, pextinfo );
	PRT_CreateLinearParticle( particle );

	// play sound
	AUD_Laser( NULL );

	return TRUE;
}


// create particles of particle weapon originating from specified position ----
//
int WFX_ShootParticleWeapon( ShipObject *shippo, int type )
{
	ASSERT( shippo != NULL );

	// check if enough space in RE_List
	if ( !NET_RmEvAllowed( RE_PARTICLEOBJECT ) )
		return FALSE;

	if ( type == PARTICLEGUN_SPREADFIRE ) {

		if ( !OBJ_DeviceAvailable( shippo, WPMASK_ALIAS_SPREADFIRE ) ) {
			ShowMessage( no_spreadfire_str );
			return FALSE;
		}

		// just return if not able to create spreadfire particles
		if ( !ShootSpreadfire( shippo, LocalPlayerId ) ) {
			return FALSE;
		}

		// insert remote event
		Vertex3 dummyorigin;
		NET_RmEvParticleObject( POBJ_SPREADFIRE, dummyorigin );

		// record create event if recording active
		Record_SpreadFireFiring();

		return TRUE;
	}

	return FALSE;
}


// remote player shot spreadfire ----------------------------------------------
//
void WFX_RemoteShootSpreadfire( int playerid )
{
	// fetch pointer to remote player's ship
	ShipObject *shippo = NET_FetchOwnersShip( playerid );
	ASSERT( shippo != NULL );
	ASSERT( shippo != MyShip );

	// currently same as local
	ShootSpreadfire( shippo, playerid );
}

#endif // !PARSEC_SERVER



// ----------------------------------------------------------------------------
// HELIX STUFF
// ----------------------------------------------------------------------------


// create helix particles for current frame -----------------------------------
//
int WFX_MaintainHelix( ShipObject *shippo, int playerid )
{
	ASSERT( shippo != NULL );

#ifndef PARSEC_SERVER
	int bitmap    = SPREADFIRE_BM_INDX;
#endif
	int color     = SPREADFIRE_PARTICLE_COLOR;
#ifdef PARSEC_SERVER
	float ref_z   = 1.0f;
#else
	float ref_z   = spreadfire_ref_z;
#endif
	int sizebound = partbitmap_size_bound;
	int lifetime  = shippo->HelixLifeTime;

	pextinfo_s *pextinfo = NULL;

#ifndef PARSEC_SERVER
	pextinfo_s extinfo;

	if ( AUX_SPREADFIRE_PARTICLES_EXTINFO ) {

		// alter basic attributes
		ref_z    *= 2;
		bitmap    = iter_texrgba | iter_specularadd;
		sizebound = PRT_NO_SIZEBOUND;

		// fetch pdef
		pdef_s *pdef;
		pdef = PDEF_pflare06();
		if ( pdef == NULL ) {
			MSGOUT( "helix particles invalid." );
			shippo->WeaponsActive &= ~WPMASK_CANNON_HELIX;
			AUD_HelixOff( shippo );
			return FALSE;
		}

		// create pextinfo
		pextinfo = &extinfo;
		PRT_InitParticleExtInfo( pextinfo, pdef, NULL, NULL );
	}
#endif // !PARSEC_SERVER

	// calc number of particles according to time passed (remember remainder)
#ifdef PARSEC_SERVER
	refframe_t numrefframes = TheSimulator->GetThisFrameRefFrames() + shippo->helix_refframes_delta;
#else
	refframe_t numrefframes = CurScreenRefFrames + shippo->helix_refframes_delta;
#endif
	int numparticles              = numrefframes / HelixRefFrames;
	shippo->helix_refframes_delta = numrefframes % HelixRefFrames;

	Vector3 dirvec;
	fixed_t speed = shippo->HelixSpeed + shippo->CurSpeed;
	DirVctMUL( shippo->ObjPosition, FIXED_TO_GEOMV( speed ), &dirvec );

	for ( int curp = 0; curp < numparticles; curp++ ) {

		// check if enough energy to shoot helix
		if ( shippo->CurEnergy < MIN_HELIX_ENERGY + HELIX_ENERGY_CONSUMPTION ) {

			WFX_DeactivateHelix( shippo );

#ifndef PARSEC_SERVER
			if ( shippo == MyShip ) {
				ShowMessage( low_energy_str );
				AUD_LowEnergy();
			}
#endif
			return FALSE;
		}
		shippo->CurEnergy -= HELIX_ENERGY_CONSUMPTION;

		sincosval_s resultp;
		GetSinCos( shippo->HelixCurBams, &resultp );

		// create oldest particle first
		int     invcurp = numparticles - curp - 1;
		fixed_t timefrm = invcurp * HelixRefFrames + shippo->helix_refframes_delta;
		fixed_t timepos = timefrm * shippo->HelixSpeed;

		// create one full frame set back because the current frame will
		// be added by the linear particle animation code in the same frame
#ifdef PARSEC_SERVER
		timepos -= speed * TheSimulator->GetThisFrameRefFrames();
#else
		timepos -= speed * CurScreenRefFrames;
#endif

		Vector3 pofsvec;
		pofsvec.X = GEOMV_MUL( HelixRadius, resultp.sinval );
		pofsvec.Y = GEOMV_MUL( HelixRadius, resultp.cosval );
		pofsvec.Z = FIXED_TO_GEOMV( timepos );

		// first particle ---
		Vertex3 object_space;
		object_space.X = shippo->Helix_X + pofsvec.X;
		object_space.Y = shippo->Helix_Y + pofsvec.Y;
		object_space.Z = shippo->Helix_Z + pofsvec.Z;

		Vertex3 world_space;
		MtxVctMUL( shippo->ObjPosition, &object_space, &world_space );

		particle_s particle;
#ifdef PARSEC_SERVER
		TheWorld->PRT_InitParticle( particle, color, sizebound,
		                            ref_z, &world_space, &dirvec,
		                            lifetime, playerid, pextinfo );
#else
		PRT_InitParticle( particle, bitmap, color, sizebound,
		                  ref_z, &world_space, &dirvec,
		                  lifetime, playerid, pextinfo );
#endif
		particle.flags |= PARTICLE_COLLISION;
#ifdef PARSEC_SERVER
		TheWorld->PRT_CreateLinearParticle( particle );
#else
		PRT_CreateLinearParticle( particle );
#endif

		// second particle ---
		object_space.X = shippo->Helix_X - pofsvec.X;
#ifdef SECOND_HELIX
		object_space.Y = shippo->Helix_Y - pofsvec.Y;
#else
		object_space.Y = shippo->Helix_Y + pofsvec.Y;
#endif
		MtxVctMUL( shippo->ObjPosition, &object_space, &world_space );

#ifdef PARSEC_SERVER
		TheWorld->PRT_InitParticle( particle, color, sizebound,
		                            ref_z, &world_space, &dirvec,
		                            lifetime, playerid, pextinfo );
#else
		PRT_InitParticle( particle, bitmap, color, sizebound,
		                  ref_z, &world_space, &dirvec,
		                  lifetime, playerid, pextinfo );
#endif
		particle.flags |= PARTICLE_COLLISION;
		particle.flags |= PARTICLE_IS_HELIX;
#ifdef PARSEC_SERVER
		TheWorld->PRT_CreateLinearParticle( particle );
#else
		PRT_CreateLinearParticle( particle );
#endif

		shippo->HelixCurBams = ( shippo->HelixCurBams + HelixBamsInc ) & 0xffff;
	}

	return TRUE;
}


// activate helix cannon of ship ----------------------------------------------
//
int WFX_ActivateHelix( ShipObject *shippo )
{
	ASSERT( shippo != NULL );
	ASSERT( ( shippo->WeaponsActive & WPMASK_CANNON_HELIX ) == 0 );

#ifndef PARSEC_SERVER
	ASSERT( shippo == MyShip );

	// check if enough space in RE_List
	if ( !NET_RmEvAllowed( RE_WEAPONSTATE ) )
		return FALSE;

	// check if helix available
	if ( !OBJ_DeviceAvailable( shippo, WPMASK_CANNON_HELIX ) ) {
		ShowMessage( no_helixcannon_str );
		return FALSE;
	}
#else
	if ( !TheGame->OBJ_DeviceAvailable( shippo, WPMASK_CANNON_HELIX ) )
		return FALSE;
#endif

	// check if enough energy to shoot helix
	if ( shippo->CurEnergy < MIN_HELIX_ENERGY + HELIX_ENERGY_CONSUMPTION ) {
#ifndef PARSEC_SERVER
		ShowMessage( low_energy_str );
		AUD_LowEnergy();
#endif
		return FALSE;
	}

	// set active flag
	shippo->WeaponsActive |= WPMASK_CANNON_HELIX;

#ifndef PARSEC_SERVER
	// send remote event to switch helix on
	NET_RmEvWeaponState( WPMASK_CANNON_HELIX, WPSTATE_ON, shippo->CurEnergy, shippo->Specials );

	// record activation event if recording active
	Record_HelixActivation();

	// play sound
	AUD_Helix( shippo );
#endif

	return TRUE;
}


// deactivate helix cannon of specified ship ----------------------------------
//
void WFX_DeactivateHelix( ShipObject *shippo )
{
	ASSERT( shippo != NULL );
	ASSERT( shippo->WeaponsActive & WPMASK_CANNON_HELIX );

#ifndef PARSEC_SERVER
	// local ship is special case
	if ( shippo == MyShip ) {

		// check if enough space in RE_List
		if ( !NET_RmEvAllowed( RE_WEAPONSTATE ) )
			return;

		// send remote event to switch helix off
		NET_RmEvWeaponState( WPMASK_CANNON_HELIX, WPSTATE_OFF, shippo->CurEnergy, shippo->Specials );

		// record deactivation event if recording active
		Record_HelixDeactivation();
	}
#endif

	// reset activation flag
	shippo->WeaponsActive &= ~WPMASK_CANNON_HELIX;

#ifndef PARSEC_SERVER
	// stop sound
	AUD_HelixOff( shippo );
#endif
}


#ifndef PARSEC_SERVER

// remote player activated helix cannon ---------------------------------------
//
void WFX_RemoteActivateHelix( int playerid )
{
	// fetch pointer to remote player's ship
	ShipObject *shippo = NET_FetchOwnersShip( playerid );
	ASSERT( shippo != NULL );
	ASSERT( shippo != MyShip );

	// make sure helix cannon is active
	if ( ( shippo->WeaponsActive & WPMASK_CANNON_HELIX ) == 0 ) {

		// avoid additional stuff that would be done
		// by WFX_ActivateHelix()
		shippo->WeaponsActive |= WPMASK_CANNON_HELIX;
		AUD_Helix( shippo );
	}
}


// remote player deactivated helix cannon -------------------------------------
//
void WFX_RemoteDeactivateHelix( int playerid )
{
	// fetch pointer to remote player's ship
	ShipObject *shippo = NET_FetchOwnersShip( playerid );
	ASSERT( shippo != NULL );
	ASSERT( shippo != MyShip );

	// make sure helix cannon is inactive
	if ( shippo->WeaponsActive & WPMASK_CANNON_HELIX ) {

		// sound will also be switched off by this
		WFX_DeactivateHelix( shippo );
	}
}

#endif // !PARSEC_SERVER


// ensure that helix cannon is inactive for ship ------------------------------
//
void WFX_EnsureHelixInactive( ShipObject *shippo )
{
	ASSERT( shippo != NULL );

	if ( shippo->WeaponsActive & WPMASK_CANNON_HELIX ) {
		shippo->WeaponsActive &= ~WPMASK_CANNON_HELIX;
#ifndef PARSEC_SERVER
		AUD_HelixOff( shippo );
#endif
	}

	ASSERT( ( shippo->WeaponsActive & WPMASK_CANNON_HELIX ) == 0 );
}



// ----------------------------------------------------------------------------
// LIGHTNING STUFF
// ----------------------------------------------------------------------------


#ifndef PARSEC_SERVER

// create two lightning beams -------------------------------------------------
//
PRIVATE
lightning_pcluster_s *CreateLightningParticles( ShipObject *shippo, int owner )
{
	ASSERT( shippo != NULL );

	// create appearance
	int bitmap	  = LIGHTNING_BM_INDX;
	int color	  = LIGHTNING_PARTICLE_COLOR;
	float ref_z = lightning_ref_z;
	int sizebound = partbitmap_size_bound;

	pextinfo_s extinfo;

	if ( AUX_LIGHTNING_PARTICLES_EXTINFO ) {

		// alter basic attributes
		ref_z    *= 2;
		bitmap    = iter_texrgba | iter_specularadd;
		sizebound = PRT_NO_SIZEBOUND;

		// fetch pdef
		pdef_s *pdef = PDEF_plight01();
		if ( pdef == NULL ) {
			MSGOUT( "lightning particles invalid." );
			return NULL;
		}

		// create pextinfo
		PRT_InitParticleExtInfo( &extinfo, pdef, NULL, NULL );
	}

	// create cluster
	dword clustertype = CT_LIGHTNING;

	if ( AUX_LIGHTNING_PARTICLES_EXTINFO )
		clustertype |= CT_EXTINFO_STORAGE;

	lightning_pcluster_s *cluster = (lightning_pcluster_s *)
		PRT_NewCluster( clustertype, LIGHTNING_LENGTH * 2, 0 );

	// fill in basic fields
	cluster->animtype	 = SAT_LIGHTNING;
	cluster->sizzlespeed = LIGHTNING_SIZZLE_SPEED;

	Vertex3 startpos;

	startpos.X = shippo->Beam_X[ 0 ];
	startpos.Y = shippo->Beam_Y;
	startpos.Z = shippo->Beam_Z;
	cluster->beamstart1 = startpos;

	startpos.X = shippo->Beam_X[ 3 ];
	cluster->beamstart2 = startpos;

	// create particles

	int curp = 0;
	if ( AUX_LIGHTNING_PARTICLES_EXTINFO ) {
		for ( curp = 0; curp < LIGHTNING_LENGTH; curp++ ) {

			pextinfo_s *curextinfo =
				(pextinfo_s *)( cluster->rep + cluster->maxnumel ) + curp;
			memcpy( curextinfo, &extinfo, sizeof( pextinfo_s ) );

			PRT_InitClusterParticle( cluster, curp, bitmap, color,
									 sizebound, ref_z,
						 	 		 &cluster->beamstart1, NULL,
						 	 		 INFINITE_LIFETIME, owner,
									 curextinfo );
		}

		for ( ; curp < LIGHTNING_LENGTH * 2; curp++ ) {

			pextinfo_s *curextinfo =
				(pextinfo_s *)( cluster->rep + cluster->maxnumel ) + curp;
			memcpy( curextinfo, &extinfo, sizeof( pextinfo_s ) );

			PRT_InitClusterParticle( cluster, curp, bitmap, color,
									 sizebound, ref_z,
						 	 		 &cluster->beamstart2, NULL,
						 	 		 INFINITE_LIFETIME, owner,
									 curextinfo );
		}

	} else {

		for ( curp = 0; curp < LIGHTNING_LENGTH; curp++ )
			PRT_InitClusterParticle( cluster, curp, bitmap, color,
									 sizebound, ref_z,
						 	 		 &cluster->beamstart1, NULL,
						 	 		 INFINITE_LIFETIME, owner,
									 NULL );
		for ( ; curp < LIGHTNING_LENGTH * 2; curp++ )
			PRT_InitClusterParticle( cluster, curp, bitmap, color,
									 sizebound, ref_z,
						 	 		 &cluster->beamstart2, NULL,
						 	 		 INFINITE_LIFETIME, owner,
									 NULL );
	}

	// attach lightning particle cluster to ship
	PRT_AttachClusterToObject( shippo, cluster );

	return cluster;
}

#endif // !PARSEC_SERVER


// activate lightning device of ship ------------------------------------------
//
int WFX_ActivateLightning( ShipObject *shippo )
{
	ASSERT( shippo != NULL );

#ifndef PARSEC_SERVER
	ASSERT( shippo == MyShip );
	ASSERT( ( shippo->WeaponsActive & WPMASK_CANNON_LIGHTNING ) == 0 );

	// check if enough space in RE_List
	if ( !NET_RmEvAllowed( RE_WEAPONSTATE ) )
		return FALSE;

	// check if lightning device available
	if ( !OBJ_DeviceAvailable( shippo, WPMASK_CANNON_LIGHTNING ) ) {
		ShowMessage( no_lightning_str );
		return FALSE;
	}

	refframe_t curRefFrames = CurScreenRefFrames;
#else
	if ( !TheGame->OBJ_DeviceAvailable( shippo, WPMASK_CANNON_LIGHTNING ) )
		return FALSE;

	refframe_t curRefFrames = TheSimulator->GetThisFrameRefFrames();
#endif

	dword energy_consumption = shippo->CurEnergyFrac +
		( curRefFrames * LIGHTNING_ENERGY_CONSUMPTION );

	// check if enough energy to shoot lightning
	if ( (dword)shippo->CurEnergy < ( MIN_LIGHTNING_ENERGY + ( energy_consumption >> 16 ) ) ) {
#ifndef PARSEC_SERVER
		ShowMessage( low_energy_str );
		AUD_LowEnergy();
#endif
		return FALSE;
	}

	// attach lightning particle cluster
#ifndef PARSEC_SERVER
	ASSERT( PRT_ObjectHasAttachedClustersOfType( shippo, SAT_LIGHTNING ) == NULL );
	if ( CreateLightningParticles( shippo, LocalPlayerId ) == NULL )
		return FALSE;
#else
	if ( TheWorld->CreateLightningParticles( shippo, GetObjectOwner( shippo ) ) == NULL )
		return FALSE;
#endif

	// set activation flag
	shippo->WeaponsActive |= WPMASK_CANNON_LIGHTNING;

#ifndef PARSEC_SERVER
	// send remote event to switch lightning on
	NET_RmEvWeaponState( WPMASK_CANNON_LIGHTNING, WPSTATE_ON, shippo->CurEnergy, shippo->Specials );

	// record activation event if recording active
	Record_LightningActivation();

	// play sound
	AUD_Lightning( shippo );
#endif

	// signify that lightning is now active
	return TRUE;
}


// deactivate lightning device of specified ship ------------------------------
//
void WFX_DeactivateLightning( ShipObject *shippo )
{
	ASSERT( shippo != NULL );
	ASSERT( shippo->WeaponsActive & WPMASK_CANNON_LIGHTNING );

#ifndef PARSEC_SERVER
	// local ship is special case
	if ( shippo == MyShip ) {

		// check if enough space in RE_List
		if ( !NET_RmEvAllowed( RE_WEAPONSTATE ) )
			return;

		// send remote event to switch lightning off
		NET_RmEvWeaponState( WPMASK_CANNON_LIGHTNING, WPSTATE_OFF, shippo->CurEnergy, shippo->Specials );

		// record deactivation event if recording active
		Record_LightningDeactivation();
	}

	// remove lightning particles
	PRT_DeleteAttachedClustersOfType( shippo, SAT_LIGHTNING );

	// stop sound
	AUD_LightningOff( shippo );
#else
	TheWorld->PRT_DeleteAttachedClustersOfType( shippo, SAT_LIGHTNING );
#endif

	// reset activation flag
	shippo->WeaponsActive &= ~WPMASK_CANNON_LIGHTNING;
}


#ifndef PARSEC_SERVER

// remote player activated lightning device -----------------------------------
//
void WFX_RemoteActivateLightning( int playerid )
{
	// fetch pointer to remote player's ship
	ShipObject *shippo = NET_FetchOwnersShip( playerid );
	ASSERT( shippo != NULL );
	ASSERT( shippo != MyShip );

	// make sure lightning device is active
	if ( ( shippo->WeaponsActive & WPMASK_CANNON_LIGHTNING ) == 0 ) {

		// avoid additional stuff that would be done
		// by WFX_ActivateLightning()
		if ( CreateLightningParticles( shippo, playerid ) != NULL ) {
			shippo->WeaponsActive |= WPMASK_CANNON_LIGHTNING;
			AUD_Lightning( shippo );
		}
	}
}


// remote player deactivated lightning device ---------------------------------
//
void WFX_RemoteDeactivateLightning( int playerid )
{
	// fetch pointer to remote player's ship
	ShipObject *shippo = NET_FetchOwnersShip( playerid );
	ASSERT( shippo != NULL );
	ASSERT( shippo != MyShip );

	// make sure lightning device is inactive
	if ( ( shippo->WeaponsActive & WPMASK_CANNON_LIGHTNING ) != 0 ) {

		// sound will also be switched off by this
		WFX_DeactivateLightning( shippo );
	}
}

#endif // !PARSEC_SERVER


// ensure that lightning is inactive for ship ---------------------------------
//
void WFX_EnsureLightningInactive( ShipObject *shippo )
{
	ASSERT( shippo != NULL );

#ifndef PARSEC_SERVER
	if ( PRT_DeleteAttachedClustersOfType( shippo, SAT_LIGHTNING ) != 0 ) {
		shippo->WeaponsActive &= ~WPMASK_CANNON_LIGHTNING;
		AUD_LightningOff( shippo );
	}
#else
	if ( TheWorld->PRT_DeleteAttachedClustersOfType( shippo, SAT_LIGHTNING ) != 0 ) {
		shippo->WeaponsActive &= ~WPMASK_CANNON_LIGHTNING;
	}
#endif

	ASSERT( ( shippo->WeaponsActive & WPMASK_CANNON_LIGHTNING ) == 0 );
}


// check whether to turn off lightning due to too little energy ---------------
//
void WFX_MaintainLightning( ShipObject *shippo )
{
	ASSERT( shippo != NULL );
	ASSERT( shippo->WeaponsActive & WPMASK_CANNON_LIGHTNING );

#ifdef PARSEC_SERVER
	refframe_t curRefFrames = TheSimulator->GetThisFrameRefFrames();
#else
	refframe_t curRefFrames = CurScreenRefFrames;
#endif

	dword energy_consumption = shippo->CurEnergyFrac +
		( curRefFrames * LIGHTNING_ENERGY_CONSUMPTION );

	// check if enough energy to shoot lightning
	if ( (dword)shippo->CurEnergy < ( MIN_LIGHTNING_ENERGY + ( energy_consumption >> 16 ) ) ) {

		WFX_DeactivateLightning( shippo );

#ifndef PARSEC_SERVER
		if ( shippo == MyShip ) {
			ShowMessage( low_energy_str );
			AUD_LowEnergy();
		}
#endif

	} else {

		shippo->CurEnergyFrac = ( energy_consumption & 0xffff );
		shippo->CurEnergy    -= ( energy_consumption >> 16 );
	}
}



// ----------------------------------------------------------------------------
// PHOTON CANNON STUFF
// ----------------------------------------------------------------------------


#ifndef PARSEC_SERVER

// calc animation of photon sphere --------------------------------------------
//
void WFX_CalcPhotonSphereAnimation( photon_sphere_pcluster_s *cluster )
{
	ASSERT( cluster != NULL );
    ASSERT( ( cluster->type & CT_TYPEMASK ) == CT_PHOTON_SPHERE );

    ShipObject *shippo = (ShipObject *) cluster->baseobject;

    float part_prf = ( cluster->max_loading_time / cluster->maxnumel );
	Vertex3 old_center;
	geomv_t contraction;

    if ( !cluster->firing ) {

        // determine maximum remaining loading time
        int working_time = cluster->max_loading_time - cluster->alive;

        // determine actual loading time
        if ( working_time > CurScreenRefFrames ) {
            working_time = CurScreenRefFrames;
        }
        cluster->alive += working_time;

        dword energy_consumption = shippo->CurEnergyFrac +
            ( working_time * PHOTON_ENERGY_CONSUMPTION );

        // check if enough energy to build
        if ( (dword)shippo->CurEnergy < ( MIN_PHOTON_ENERGY + energy_consumption >> 16 ) ) {
            cluster->alive = (int)(cluster->alive -( working_time - ( shippo->CurEnergy - MIN_PHOTON_ENERGY ) /
                            FIXED_TO_FLOAT( PHOTON_ENERGY_CONSUMPTION ) ));
            shippo->CurEnergy = MIN_PHOTON_ENERGY - 1;
            WFX_DeactivatePhoton( shippo );
        } else {
            shippo->CurEnergyFrac = ( energy_consumption & 0xffff );
            shippo->CurEnergy    -= ( energy_consumption >> 16 );
        }

        // calculate number of visible particles
        if ( ( cluster->alive < cluster->max_loading_time ) ) {
            cluster->numel = (int)( cluster->alive / part_prf );
            if ( cluster->numel == 0 ) {
                // at least one particle must be visible
                cluster->numel = 1;
            }
        }
        else {
            cluster->numel = cluster->maxnumel;

            //NOTE:
            // CurScreenRefFrames get added some lines below anyway,
            // so we must not add them here
            cluster->cur_contraction_time -= working_time;

            WFX_DeactivatePhoton( shippo );
        }
    }

    if ( cluster->firing ) {

        // increase counter
        cluster->cur_contraction_time += CurScreenRefFrames;

        // create linear particles

        // number of particles in full load
        int single_load = ( cluster->maxnumel / cluster->numloads );
        int old_numel = cluster->numel;

        // number of full loads to be created
        int numloads = ( cluster->cur_contraction_time / cluster->contraction_time );
        // minimize on full loads available
        if ( numloads > ( old_numel / single_load ) ) {
            numloads = ( old_numel / single_load );
        }

        // set particle properties
        int bitmap    = iter_texrgba | iter_specularadd;
        int color     = PHOTON_COLOR;
        float ref_z = photon_ref_z;
        int sizebound = PRT_NO_SIZEBOUND;
        int lifetime  = shippo->PhotonLifeTime;
        int playerid  = GetObjectOwner( shippo );

        // fetch pdef
        static int pdefid = -1;
        pdef_s *pdef = ( pdefid != -1 ) ?
            PRT_AcquireParticleDefinitionById( pdefid ) :
            PRT_AcquireParticleDefinition( "photon1", &pdefid );

        // create extinfo
        pextinfo_s extinfo;
        PRT_InitParticleExtInfo( &extinfo, pdef, NULL, NULL );

        // set speed and direction vector
        Vector3 dirvec;
        fixed_t speed = shippo->PhotonSpeed + shippo->CurSpeed;
        DirVctMUL( shippo->ObjPosition, FIXED_TO_GEOMV( speed ), &dirvec );

        // radius of load
        geomv_t radius = shippo->BoundingSphere - GEOMV_MUL( cluster->contraction_speed, cluster->contraction_time );

        fixed_t timefrm;
        fixed_t timepos;
        Vertex3 object_space;
		int cur_load = 0;

        // create full loads
        for ( cur_load = 0; cur_load < numloads; cur_load++ ) {
			timefrm = cluster->cur_contraction_time - ( ( cur_load + 1 ) * cluster->contraction_time );
			timepos = timefrm * shippo->PhotonSpeed;

			// create one full frame set back because the current frame will
			// be added by the linear particle animation code in the same frame
			timepos -= speed * CurScreenRefFrames;

            for ( int i = 0 ; i < single_load; i++ ) {
                CalcSphereParticlePosition( object_space, radius, SAT_SPHERETYPE_NORMAL );
                object_space.Z += FIXED_TO_GEOMV( 0x10000 * cluster->contraction_time )
                                + FIXED_TO_GEOMV( timepos );

                Vertex3 world_space;
                MtxVctMUL( shippo->ObjPosition, &object_space, &world_space );

                particle_s particle;
                PRT_InitParticle( particle, bitmap, color, sizebound,
                                ref_z, &world_space, &dirvec,
                                lifetime, playerid, &extinfo );
                particle.flags |= PARTICLE_COLLISION;
                particle.flags |= PARTICLE_IS_PHOTON;
                PRT_CreateLinearParticle( particle );
            }

            // play firing sample
            AUD_PhotonFiring( shippo );
        }

        // check if last load should be created
        if ( ( cluster->cur_contraction_time / cluster->contraction_time ) > ( old_numel / single_load ) ) {
			timefrm = cluster->cur_contraction_time - ( ( cur_load + 1 ) * cluster->contraction_time );
			timepos = timefrm * shippo->PhotonSpeed;

			// create one full frame set back because the current frame will
			// be added by the linear particle animation code in the same frame
			timepos -= speed * CurScreenRefFrames;

            for ( int i = 0 ; i < ( old_numel % single_load ); i++ ) {
                CalcSphereParticlePosition( object_space, radius, SAT_SPHERETYPE_NORMAL );
                object_space.Z += FIXED_TO_GEOMV( 0x10000 * cluster->contraction_time )
                                + FIXED_TO_GEOMV( timepos );

                Vertex3 world_space;
                MtxVctMUL( shippo->ObjPosition, &object_space, &world_space );

                particle_s particle;
                PRT_InitParticle( particle, bitmap, color, sizebound,
                                ref_z, &world_space, &dirvec,
                                lifetime, playerid, &extinfo );
                particle.flags |= PARTICLE_COLLISION;
                particle.flags |= PARTICLE_IS_PHOTON;
                PRT_CreateLinearParticle( particle );
            }

            // play firing sample
            AUD_PhotonFiring( shippo );
        }

        // calculate remaining visible sphere particles
        cluster->numel -= ( cluster->cur_contraction_time / cluster->contraction_time ) * single_load;
        if ( cluster->numel < 0 ) {
            cluster->numel = 0;
        }

        int next_numel = cluster->numel - single_load;
        if ( next_numel < 0 ) {
            next_numel = 0;
        }

        if ( cluster->numel == 0 ) {
            // cluster will be deleted automatically
        }
        else {
            // contract and rotate appropriate amount of particles

            // set old and new center values
            old_center.X = cluster->center.X;
            old_center.Y = cluster->center.Y;
            old_center.Z = ( cluster->cur_contraction_time / cluster->contraction_time ) ? 0 : cluster->center.Z;
            cluster->center.Z = FIXED_TO_GEOMV( 0x10000
                    * ( cluster->cur_contraction_time % cluster->contraction_time ) );

            contraction = ( cluster->cur_contraction_time < cluster->contraction_time )
                                    ? ( cluster->contraction_speed * CurScreenRefFrames )
                                    : ( cluster->contraction_speed * ( cluster->cur_contraction_time % cluster->contraction_time ) );

            bams_t pitch = cluster->pitch * CurScreenRefFrames;
            bams_t yaw   = cluster->yaw   * CurScreenRefFrames;
            bams_t roll  = cluster->roll  * CurScreenRefFrames;
	    int pid = 0;
            for ( pid = ( cluster->numel - 1 ); pid >= next_numel; pid-- ) {

                cluster->rep[ pid ].position.X -= old_center.X;
                cluster->rep[ pid ].position.Y -= old_center.Y;
                cluster->rep[ pid ].position.Z -= old_center.Z;

                CalcSphereParticleRotation( cluster->rep[ pid ].position,
                                            pitch, yaw, roll );
                CalcSphereContraction( cluster->rep[ pid ].position, contraction );

                // move particles forward
                cluster->rep[ pid ].position.X += cluster->center.X;
                cluster->rep[ pid ].position.Y += cluster->center.Y;
                cluster->rep[ pid ].position.Z += cluster->center.Z;
            }
            for ( pid = next_numel - 1; pid >= 0; pid-- ) {
                CalcSphereParticleRotation( cluster->rep[ pid ].position,
                                            pitch, yaw, roll );
            }
            // update cluster radius ?
            cluster->bdsphere -= contraction;
        }

        cluster->cur_contraction_time %= cluster->contraction_time;
    }
    else {
        bams_t pitch = cluster->pitch * CurScreenRefFrames;
        bams_t yaw   = cluster->yaw   * CurScreenRefFrames;
        bams_t roll  = cluster->roll  * CurScreenRefFrames;

        for ( int pid = 0; pid < cluster->numel; pid++ ) {
            CalcSphereParticleRotation( cluster->rep[ pid ].position,
                                        pitch, yaw, roll );
        }
    }
}


// create particle sphere for photon cannon -----------------------------------
//
PRIVATE
photon_sphere_pcluster_s *CreatePhotonSphere( ShipObject *shippo )
{
	ASSERT( shippo != NULL );
	ASSERT( OBJECT_TYPE_SHIP( shippo ) );

	// fetch pdef
	static int pdefid = -1;
	pdef_s *pdef = ( pdefid != -1 ) ?
		PRT_AcquireParticleDefinitionById( pdefid ) :
		PRT_AcquireParticleDefinition( "photon1", &pdefid );
	if ( pdef == NULL ) {
        MSGOUT( "photon particles invalid." );
        return NULL;
	}

	// create extinfo
    pextinfo_s extinfo;
	PRT_InitParticleExtInfo( &extinfo, pdef, NULL, NULL );

	// determine cluster type and hints
    dword clustertype = CT_PHOTON_SPHERE;
	clustertype |= CT_EXTINFO_STORAGE;
	clustertype |= CT_CLUSTER_GLOBAL_EXTINFO;
	clustertype |= CT_HINT_PARTICLES_HAVE_EXTINFO;
	clustertype |= CT_HINT_PARTICLES_IDENTICAL;

    // create new cluster
    int clustersiz = PHOTON_SPHERE_PARTICLES;
	photon_sphere_pcluster_s *cluster = (photon_sphere_pcluster_s *)
		PRT_NewCluster( clustertype, clustersiz, 0 );

	// fill in basic fields
	cluster->animtype				= SAT_PHOTON;
	cluster->bdsphere				= shippo->BoundingSphere;
	cluster->contraction_time		= PHOTON_CONTRACTION_TIME;
	cluster->cur_contraction_time	= 0;
	cluster->contraction_speed		= PHOTON_CONTRACTION_SPEED;
	cluster->max_loading_time		= PHOTON_MAX_LOADING_TIME;
	cluster->firing					= FALSE;
	cluster->numloads				= PHOTON_NUMLOADS;
	cluster->alive					= 0;
	cluster->center.X				= 0;
	cluster->center.Y				= 0;
	cluster->center.Z				= 0;
	cluster->pitch					= PHOTON_ROT_PITCH;
	cluster->yaw					= PHOTON_ROT_YAW;
	cluster->roll					= PHOTON_ROT_ROLL;

	// set particle properties
	for ( int curp = 0; curp < clustersiz; curp++ ) {

		Vertex3 particlepos;
        CalcSphereParticlePosition( particlepos, shippo->BoundingSphere, SAT_SPHERETYPE_NORMAL );

		// copy extinfo into cluster
		pextinfo_s *curextinfo = (pextinfo_s *)( cluster->rep + clustersiz ) + curp;
		memcpy( curextinfo, &extinfo, sizeof( pextinfo_s ) );

		// init particle in cluster
        cluster->rep[ curp ].owner		= GetObjectOwner( shippo );
        cluster->rep[ curp ].flags		= PARTICLE_ACTIVE;
        cluster->rep[ curp ].lifetime	= INFINITE_LIFETIME;
        cluster->rep[ curp ].extinfo	= curextinfo;
        cluster->rep[ curp ].bitmap		= iter_texrgba | iter_specularadd;
        cluster->rep[ curp ].color		= PHOTON_COLOR;
        cluster->rep[ curp ].sizebound	= PRT_NO_SIZEBOUND;
        cluster->rep[ curp ].ref_z		= photon_ref_z;
        cluster->rep[ curp ].position	= particlepos;
        cluster->rep[ curp ].velocity.X	= GEOMV_0;
		cluster->rep[ curp ].velocity.Y	= GEOMV_0;
		cluster->rep[ curp ].velocity.Z	= GEOMV_0;

        // increase number of elements
        Particles->numel++;
    }

	// at least one particle has to be visible
    cluster->numel = 1;

    // attach sphere's particle cluster to object
	PRT_AttachClusterToObject( shippo, (objectbase_pcluster_s *)cluster );

    return cluster;
}

#endif // !PARSEC_SERVER


// activate photon cannon of ship ---------------------------------------------
//
int WFX_ActivatePhoton( ShipObject *shippo )
{
	ASSERT( shippo != NULL );

#ifndef PARSEC_SERVER
	ASSERT( shippo == MyShip );
    ASSERT( ( shippo->WeaponsActive & WPMASK_CANNON_PHOTON ) == 0 );

	// check if enough space in RE_List
	if ( !NET_RmEvAllowed( RE_WEAPONSTATE ) )
		return FALSE;

    // check if photon available
    if ( !OBJ_DeviceAvailable( shippo, WPMASK_CANNON_PHOTON ) ) {
        ShowMessage( no_photoncannon_str );
		return FALSE;
	}
#else
	if ( !TheGame->OBJ_DeviceAvailable( shippo, WPMASK_CANNON_PHOTON ) )
		return FALSE;
#endif

    // check if enough energy to shoot photon
    if ( shippo->CurEnergy < MIN_PHOTON_ENERGY ) {
#ifndef PARSEC_SERVER
		ShowMessage( low_energy_str );
		AUD_LowEnergy();
#endif
		return FALSE;
	}

    // check if photon cannon still firing
#ifndef PARSEC_SERVER
    if ( PRT_ObjectHasAttachedClustersOfType( shippo, SAT_PHOTON ) )
        return FALSE;

    // create particle sphere
    if ( !CreatePhotonSphere( shippo ) ) {
        ShowMessage( "could not create sphere" );
        return FALSE;
    }
#else
    if ( TheWorld->PRT_ObjectHasAttachedClustersOfType( shippo, SAT_PHOTON ) )
        return FALSE;

    if ( !TheWorld->CreatePhotonSphere( shippo ) )
        return FALSE;
#endif

    // set active flag
    shippo->WeaponsActive |= WPMASK_CANNON_PHOTON;

#ifndef PARSEC_SERVER
    // send remote event to switch photon on
    NET_RmEvWeaponState( WPMASK_CANNON_PHOTON, WPSTATE_ON, shippo->CurEnergy, shippo->Specials );

	// record activation event if recording active
	Record_PhotonActivation();

	// play sound
	AUD_PhotonLoading( shippo );
#endif

	return TRUE;
}


// deactivate photon cannon of specified ship ----------------------------------
//
void WFX_DeactivatePhoton( ShipObject *shippo )
{
	ASSERT( shippo != NULL );
    ASSERT( shippo->WeaponsActive & WPMASK_CANNON_PHOTON );

#ifndef PARSEC_SERVER
	// local ship is special case
	if ( shippo == MyShip ) {

		// check if enough space in RE_List
		if ( !NET_RmEvAllowed( RE_WEAPONSTATE ) )
			return;

        // send remote event to switch photon off
        NET_RmEvWeaponState( WPMASK_CANNON_PHOTON, WPSTATE_OFF, shippo->CurEnergy, shippo->Specials );

		// record deactivation event if recording active
		Record_PhotonDeactivation();
	}

	// stop sound
    AUD_PhotonLoadingOff( shippo );
#endif

	// reset activation flag
    shippo->WeaponsActive &= ~WPMASK_CANNON_PHOTON;

    // start firing
#ifndef PARSEC_SERVER
    photon_sphere_pcluster_s* cluster = (photon_sphere_pcluster_s *)
		PRT_ObjectHasAttachedClustersOfType( shippo, SAT_PHOTON );
#else
    photon_sphere_pcluster_s* cluster = (photon_sphere_pcluster_s *)
		TheWorld->PRT_ObjectHasAttachedClustersOfType( shippo, SAT_PHOTON );
#endif
    if ( cluster != NULL )
        cluster->firing = TRUE;
}


#ifndef PARSEC_SERVER

// remote player activated photon cannon --------------------------------------
//
void WFX_RemoteActivatePhoton( int playerid )
{
	// fetch pointer to remote player's ship
	ShipObject *shippo = NET_FetchOwnersShip( playerid );
	ASSERT( shippo != NULL );
	ASSERT( shippo != MyShip );

    // make sure photon cannon is active
    if ( ( shippo->WeaponsActive & WPMASK_CANNON_PHOTON ) == 0 ) {

		// avoid additional stuff that would be done
        // by WFX_ActivatePhoton()

        // check if photon cannon still firing
        if ( PRT_ObjectHasAttachedClustersOfType( shippo, SAT_PHOTON ) )
            return;

        // create particle sphere
	    if ( !CreatePhotonSphere( shippo ) ) {
	        ShowMessage( "could not create sphere" );
	        return;
	    }

        shippo->WeaponsActive |= WPMASK_CANNON_PHOTON;

		// play sound
		AUD_PhotonLoading( shippo );
	}
}


// remote player deactivated photon cannon ------------------------------------
//
void WFX_RemoteDeactivatePhoton( int playerid )
{
	// fetch pointer to remote player's ship
	ShipObject *shippo = NET_FetchOwnersShip( playerid );
	ASSERT( shippo != NULL );
	ASSERT( shippo != MyShip );

    // make sure photon cannon is inactive
    if ( shippo->WeaponsActive & WPMASK_CANNON_PHOTON ) {

		// sound will also be switched off by this
        WFX_DeactivatePhoton( shippo );
	}
}

#endif // !PARSEC_SERVER


// ensure that photon cannon is inactive for ship -----------------------------
//
void WFX_EnsurePhotonInactive( ShipObject *shippo )
{
	ASSERT( shippo != NULL );

    if ( shippo->WeaponsActive & WPMASK_CANNON_PHOTON ) {
        shippo->WeaponsActive &= ~WPMASK_CANNON_PHOTON;
#ifndef PARSEC_SERVER
		// stop sound
		AUD_PhotonLoadingOff( shippo );
#endif
	}

    ASSERT( ( shippo->WeaponsActive & WPMASK_CANNON_PHOTON ) == 0 );
}


// ensure that no particle weapons are active for ship ------------------------
//
void WFX_EnsureParticleWeaponsInactive( ShipObject *shippo )
{
	ASSERT( shippo != NULL );

	// ensure all particle weapons are destroyed
	WFX_EnsureLightningInactive( shippo );
	WFX_EnsureHelixInactive( shippo );
	WFX_EnsurePhotonInactive( shippo );
}
