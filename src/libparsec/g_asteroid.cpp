/*
 * PARSEC - Asteroid Custom Object
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */

// C library
#include <math.h>
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

// subsystem headers
#include "net_defs.h"
#include "sys_defs.h"

// mathematics header
#include "utl_math.h"

// model header
#include "utl_model.h"

// local module header
#include "g_asteroid.h"

// proprietary module headers
#ifndef PARSEC_SERVER
#include "aud_defs.h"
#include "vid_defs.h"
#include "con_info.h"
#include "e_callbk.h"
#include "e_supp.h"
#include "g_supp.h"
#include "net_serv.h"
#include "obj_ctrl.h"
#include "r_sfx.h"
#else
#include "con_info_sv.h"
#include "e_simnetoutput.h"
#include "e_simulator.h"
#include "e_world_trans.h"
#include "net_game_sv.h"
#endif

#include "obj_clas.h"
#include "obj_cust.h"
#include "obj_creg.h"
#include "con_arg.h"

#ifndef PARSEC_SERVER
extern int headless_bot;
#endif


// type id for asteroid objects ------------------------------------------------
//
PUBLIC dword asteroid_type_id = TYPE_ID_INVALID;


// offset definitions into the Asteroid ----------------------------------------
//
#define OFS_ROTSPDX			offsetof( Asteroid, RotSpeedX )
#define OFS_ROTSPDY			offsetof( Asteroid, RotSpeedY )
#define OFS_ROTSPDZ			offsetof( Asteroid, RotSpeedZ )
#define OFS_ORBITSPEED		offsetof( Asteroid, OrbitSpeed )
#define OFS_ORBITRADIUS		offsetof( Asteroid, OrbitRadius )
#define OFS_ORBITSHAPE		offsetof( Asteroid, OrbitShape )
#define OFS_ORBITPARENTID	offsetof( Asteroid, OrbitParentId )
#define OFS_NOISESEED		offsetof( Asteroid, NoiseSeed )


// list of console-accessible properties ---------------------------------------
//
PRIVATE
proplist_s Asteroid_PropList[] = {

	{ "rotspdx",		OFS_ROTSPDX,		0,	0xffff,		  PROPTYPE_INT,   NULL },
	{ "rotspdy",		OFS_ROTSPDY,		0,	0xffff,		  PROPTYPE_INT,   NULL },
	{ "rotspdz",		OFS_ROTSPDZ,		0,	0xffff,		  PROPTYPE_INT,   NULL },
	{ "orbitspeed",		OFS_ORBITSPEED,		0,	0xffff,		  PROPTYPE_INT,   NULL },
	{ "orbitradius",	OFS_ORBITRADIUS,	0,	0x7FFF0000,   PROPTYPE_FLOAT, NULL },
	{ "orbitshape",		OFS_ORBITSHAPE,		0,	100,		  PROPTYPE_INT,   NULL },
	{ "orbitparentid",	OFS_ORBITPARENTID,	0,	0x7fffffff,   PROPTYPE_INT,   NULL },
	{ "noiseseed",		OFS_NOISESEED,		0,	0x7fffffff,   PROPTYPE_INT,   NULL },

	{ NULL,				0,					0,	0,			  0,              NULL },
};


// type fields init function for asteroid --------------------------------------
//
PRIVATE
void AsteroidInitType( CustomObject *base )
{
	ASSERT( base != NULL );

	Asteroid *asteroid = (Asteroid *) base;

	asteroid->RotSpeedX		= 0;
	asteroid->RotSpeedY		= 0;
	asteroid->RotSpeedZ		= 0;
	asteroid->CurOrbitPos	= 0;
	asteroid->OrbitSpeed	= 0;
	asteroid->OrbitRadius	= 0;
	asteroid->OrbitParent	= NULL;
	asteroid->OrbitShape	= 0;
	asteroid->OrbitParentId	= 0;
	asteroid->NoiseSeed		= 0;

	asteroid->SurfTexName[ 0 ] = '\0';
	asteroid->SurfTexture      = NULL;

#ifdef PARSEC_SERVER
	asteroid->pDist = NULL;
#endif
}


#ifndef PARSEC_SERVER

// callback type and flags
static int asteroid_callback_type = CBTYPE_DRAW_OBJECTS | CBFLAG_REMOVE;


// asteroid draw callback ------------------------------------------------------
//
int AsteroidDraw( void *param )
{
	if ( headless_bot )
		return TRUE;
	ASSERT( param != NULL );
	Asteroid *asteroid = (Asteroid *) param;

	// Temporarily override FaceList[0].TexMap for this instance.
	// FaceList is shared class-level data — save and restore around draw.
	TextureMap *saved_texmap = NULL;
	if ( asteroid->SurfTexture != NULL && asteroid->FaceList != NULL ) {
		saved_texmap = asteroid->FaceList[ 0 ].TexMap;
		asteroid->FaceList[ 0 ].TexMap = asteroid->SurfTexture;
	}

	// Draw the asteroid sphere with noise-perturbed vertices
	R_DrawAsteroid( asteroid );

	// Restore shared FaceList
	if ( asteroid->SurfTexture != NULL && asteroid->FaceList != NULL ) {
		asteroid->FaceList[ 0 ].TexMap = saved_texmap;
	}

	return TRUE;
}


// asteroid instantiation callback (client only) --------------------------------
//
PRIVATE
void AsteroidInstantiate( CustomObject *base )
{
	ASSERT( base != NULL );
	Asteroid *asteroid = (Asteroid *) base;

	if ( asteroid->SurfTexName[ 0 ] != '\0' ) {
		asteroid->SurfTexture = FetchTextureMap( asteroid->SurfTexName );
		if ( asteroid->SurfTexture == NULL )
			MSGOUT( "asteroid surface texture '%s' not found.", asteroid->SurfTexName );
	}
}

#endif // !PARSEC_SERVER


// asteroid destructor (instance destruction) ----------------------------------
//
PRIVATE
void AsteroidDestroy( CustomObject *base )
{
	ASSERT( base != NULL );

#ifndef PARSEC_SERVER
	int numremoved = CALLBACK_DestroyCallback( asteroid_callback_type, (void *) base );
	if ( !headless_bot )
		ASSERT( numremoved <= 1 );
#endif
}


// asteroid animation callback -------------------------------------------------
//
int AsteroidAnimate( CustomObject *base )
{
	ASSERT( base != NULL );
	Asteroid *asteroid = (Asteroid *) base;

	asteroid->CullMask = 0x00;

	// 3-axis tumble rotation
#ifndef PARSEC_SERVER
	{
		bams_t refframes = CurScreenRefFrames;
		if ( asteroid->RotSpeedX != 0 )
			ObjRotX( asteroid->ObjPosition, asteroid->RotSpeedX * refframes );
		if ( asteroid->RotSpeedY != 0 )
			ObjRotY( asteroid->ObjPosition, asteroid->RotSpeedY * refframes );
		if ( asteroid->RotSpeedZ != 0 )
			ObjRotZ( asteroid->ObjPosition, asteroid->RotSpeedZ * refframes );
	}

	// Register the draw callback
	CALLBACK_RegisterCallback( asteroid_callback_type, AsteroidDraw, (void *) base );
#else
	{
		bams_t refframes = TheSimulator->GetThisFrameRefFrames();
		if ( asteroid->RotSpeedX != 0 )
			ObjRotX( asteroid->ObjPosition, asteroid->RotSpeedX * refframes );
		if ( asteroid->RotSpeedY != 0 )
			ObjRotY( asteroid->ObjPosition, asteroid->RotSpeedY * refframes );
		if ( asteroid->RotSpeedZ != 0 )
			ObjRotZ( asteroid->ObjPosition, asteroid->RotSpeedZ * refframes );
	}
#endif

	// Orbital translation — server-authoritative; both sides advance locally.
	if ( asteroid->OrbitSpeed != 0 ) {
		// Lazy OrbitParent resolution
		if ( asteroid->OrbitParentId != 0 && asteroid->OrbitParent == NULL ) {
			GenObject *walk = (GenObject*)FetchFirstCustom();
			for ( ; walk != NULL; walk = walk->NextObj ) {
				if ( walk->HostObjNumber == asteroid->OrbitParentId ) {
					asteroid->OrbitParent = walk;
					break;
				}
			}
		}
		geomv_t cx = GEOMV_0, cy = GEOMV_0, cz = GEOMV_0;
		if ( asteroid->OrbitParent != NULL ) {
			cx = asteroid->OrbitParent->ObjPosition[ 0 ][ 3 ];
			cy = asteroid->OrbitParent->ObjPosition[ 1 ][ 3 ];
			cz = asteroid->OrbitParent->ObjPosition[ 2 ][ 3 ];
		}
#ifdef PARSEC_SERVER
		asteroid->CurOrbitPos += asteroid->OrbitSpeed * TheSimulator->GetThisFrameRefFrames();
#else
		asteroid->CurOrbitPos += asteroid->OrbitSpeed * CurScreenRefFrames;
#endif
		sincosval_s sc;
		GetSinCos( asteroid->CurOrbitPos, &sc );
		float eccFactor = 1.0f - ( asteroid->OrbitShape / 100.0f ) * 0.90f;
		geomv_t semiminor = (geomv_t)( GEOMV_TO_FLOAT( asteroid->OrbitRadius ) * eccFactor );
		asteroid->ObjPosition[ 0 ][ 3 ] = cx + GEOMV_MUL( sc.sinval, asteroid->OrbitRadius );
		asteroid->ObjPosition[ 1 ][ 3 ] = cy;
		asteroid->ObjPosition[ 2 ][ 3 ] = cz + GEOMV_MUL( sc.cosval, semiminor );
	}

	return TRUE;
}


// handle persistency (server → client sync) -----------------------------------
//
PRIVATE
int AsteroidPersistToStream( CustomObject *base, int tostream, void *rl )
{
#ifdef PARSEC_SERVER
	ASSERT( base != NULL );
	ASSERT( tostream == TRUE );
	Asteroid *asteroid = (Asteroid *) base;

	// determine RE size
	size_t size = E_REList::RmEvGetSizeFromType( RE_ASTEROID );

	// write to RE list
	if ( rl != NULL ) {

		E_REList *pREList = (E_REList *)rl;

		RE_Asteroid *re_ast = (RE_Asteroid *)pREList->NET_Allocate( RE_ASTEROID );
		if ( re_ast == NULL ) {
			MSGOUT( "AsteroidPersistToStream: RE_ASTEROID allocation failed (buffer full)" );
			return (int)size;
		}

		re_ast->hostid       = asteroid->HostObjNumber;
		re_ast->pos[ 0 ]     = asteroid->ObjPosition[ 0 ][ 3 ];
		re_ast->pos[ 1 ]     = asteroid->ObjPosition[ 1 ][ 3 ];
		re_ast->pos[ 2 ]     = asteroid->ObjPosition[ 2 ][ 3 ];
		re_ast->boundsphere  = GEOMV_TO_FLOAT( asteroid->BoundingSphere );
		re_ast->rotspdx      = asteroid->RotSpeedX;
		re_ast->rotspdy      = asteroid->RotSpeedY;
		re_ast->rotspdz      = asteroid->RotSpeedZ;
		re_ast->noiseseed    = asteroid->NoiseSeed;
		strncpy( re_ast->surtexname, asteroid->SurfTexName, sizeof( re_ast->surtexname ) - 1 );
		re_ast->surtexname[ sizeof( re_ast->surtexname ) - 1 ] = '\0';
		re_ast->orbitspeed    = asteroid->OrbitSpeed;
		re_ast->orbitradius   = GEOMV_TO_FLOAT( asteroid->OrbitRadius );
		re_ast->curorbitpos   = asteroid->CurOrbitPos;
		re_ast->orbitparentid = asteroid->OrbitParentId;
		re_ast->orbitshape    = asteroid->OrbitShape;
	}

	return (int)size;
#else
	return 0;
#endif
}


// register object type for asteroid -------------------------------------------
//
PRIVATE
void AsteroidRegisterCustomType()
{
	custom_type_info_s info;
	memset( &info, 0, sizeof( info ) );

	info.type_name			= "asteroid";
	info.type_id			= 0x00000000;
	info.type_size			= sizeof( Asteroid );
	info.type_template		= NULL;
	info.type_flags			= CUSTOM_TYPE_DEFAULT;
	info.callback_init		= AsteroidInitType;
#ifndef PARSEC_SERVER
	info.callback_instant	= AsteroidInstantiate;
#else
	info.callback_instant	= NULL;
#endif
	info.callback_destroy	= AsteroidDestroy;
	info.callback_animate	= AsteroidAnimate;
	info.callback_collide	= NULL;
	info.callback_notify	= NULL;
	info.callback_persist	= AsteroidPersistToStream;

	asteroid_type_id = OBJ_RegisterCustomType( &info );
	CON_RegisterCustomType( info.type_id, Asteroid_PropList );
}


// module registration function ------------------------------------------------
//
REGISTER_MODULE( G_ASTEROID )
{
	// register type
	AsteroidRegisterCustomType();
}
