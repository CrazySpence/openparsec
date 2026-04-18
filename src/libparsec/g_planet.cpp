/*
 * PARSEC - Planet Custom Object
 *
 * $Author: uberlinuxguy $ - $Date: 2004/09/26 03:43:36 $
 *
 * Orginally written by:
 *   Copyright (c) Andreas Varga       <sid@parsec.org>   2000
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
#include "g_planet.h"

// proprietary module headers
#ifndef PARSEC_SERVER
#include "aud_defs.h"
#include "vid_defs.h"
#include "d_font.h"
#include "d_iter.h"
#include "d_misc.h"
#include "r_sfx.h"
#include "con_info.h"
#include "e_callbk.h"
#include "e_supp.h"
#include "g_supp.h"
#include "h_supp.h"
#include "net_serv.h"
#include "obj_ctrl.h"
#include "obj_xtra.h"
#include "part_api.h"
#else
#include "con_info_sv.h"
#include "e_simnetoutput.h"
#include "e_simulator.h"
#include "net_game_sv.h"
#endif

#include "obj_clas.h"
#include "obj_cust.h"
#include "obj_creg.h"
#include "con_arg.h"

#ifndef PARSEC_SERVER
extern int headless_bot;
#endif

// flags
//#define ORBIT_ANIMATION
//#define SHIP_ORBIT_ANIMATION


// type id for planet objects -------------------------------------------------
//
PUBLIC dword planet_type_id = TYPE_ID_INVALID;


// generic string paste area --------------------------------------------------
//
#define PASTE_STR_LEN 255

// offset definitions into the Planet -----------------------------------------
//
#define OFS_ROTSPEED		offsetof( Planet, RotSpeed )
#define OFS_ORBITSPEED		offsetof( Planet, OrbitSpeed )
#define OFS_ORBITRADIUS		offsetof( Planet, OrbitRadius )
#define OFS_NAME			offsetof( Planet, Name )
#define OFS_HASRING			offsetof( Planet, HasRing )
#define OFS_RINGOUTERRADIUS	offsetof( Planet, RingOuterRadius )
#define OFS_RINGINNERRADIUS	offsetof( Planet, RingInnerRadius )
#define OFS_RINGTEXNAME		offsetof( Planet, RingTexName )


// list of console-accessible properties --------------------------------------
//
PRIVATE
proplist_s Planet_PropList[] = {

	{ "rotspeed",		 OFS_ROTSPEED,	 		0,			0xffff,			  PROPTYPE_INT,	   NULL	},
	{ "orbitspeed",		 OFS_ORBITSPEED,		0,			0xffff,			  PROPTYPE_INT,	   NULL	},
	{ "orbitradius",	 OFS_ORBITRADIUS, 		0x10000,	0x4000000,		  PROPTYPE_FLOAT,  NULL	},
	{ "name",			 OFS_NAME,		 		0,			MAX_PLANET_NAME,  PROPTYPE_STRING, NULL	},
	{ "hasring",		 OFS_HASRING, 	 		0x0,		0x1,		 	  PROPTYPE_INT,    NULL	},
	{ "ringouterradius", OFS_RINGOUTERRADIUS, 	0x10000,	0x4000000,	 	  PROPTYPE_FLOAT,  NULL	},
	{ "ringinnerradius", OFS_RINGINNERRADIUS, 	0x10000,	0x4000000,	 	  PROPTYPE_FLOAT,  NULL	},
	{ "ringtexname",	 OFS_RINGTEXNAME, 		0,			MAX_RING_TEXNAME, PROPTYPE_STRING, NULL	},

	{ NULL,				0,					0,			0,				  0,			   NULL	},
};


// type fields init function for planet ---------------------------------------
//
PRIVATE
void PlanetInitType( CustomObject *base )
{
	ASSERT( base != NULL );

	Planet *planet = (Planet *) base;

	planet->RotSpeed		= 0x0001;
	planet->CurOrbitPos		= 0;
	planet->OrbitSpeed		= 0x0001;
	planet->OrbitRadius		= 3000000;
	planet->OrbitParent 	= NULL;
	planet->HasRing		 	= FALSE;
	planet->RingOuterRadius = FLOAT_TO_GEOMV( 600.0f );
	planet->RingInnerRadius = FLOAT_TO_GEOMV( 300.0f );
	planet->RingTiltX		= 0;
	planet->RingTiltZ		= 0;

	strncpy( planet->RingTexName, PLANET_RING_TEXTURE, MAX_RING_TEXNAME );
	planet->RingTexName[ MAX_RING_TEXNAME ] = 0;

	planet->RingTexture		= NULL;

	planet->NumOrbitShips	= 0;

	planet->SurfTexName[ 0 ] = '\0';
	planet->SurfTexture      = NULL;

#ifdef PARSEC_SERVER
	planet->pDist = NULL;
#endif
}


#ifndef PARSEC_SERVER

static float  orbit_depth = 1.0f;


// draw planet rings ----------------------------------------------------------
//
void PlanetDraw_Rings( Planet *planet )
{
	// skip if no ring texture is loaded
	if ( planet->RingTexture == NULL )
		return;

	// create vertex array
	IterArray3 *itarray = (IterArray3 *) ALLOCMEM(
		(size_t)&((IterArray3*)0)->Vtxs[ PLANET_RING_SEGMENTS * 2 ] );
	if ( itarray == NULL )
		OUTOFMEM( 0 );

	// texwidth for circumferential U coordinate
	int texwidth = 1 << planet->RingTexture->Width;

	itarray->NumVerts	= PLANET_RING_SEGMENTS * 2;
	itarray->arrayinfo	= ITERARRAY_USE_COLOR |
						  ITERARRAY_USE_TEXTURE | ITERARRAY_GLOBAL_TEXTURE;
	itarray->flags		= ITERFLAG_Z_DIV_XYZ | ITERFLAG_Z_DIV_UVW |
						  ITERFLAG_Z_TO_DEPTH;
	itarray->itertype	= iter_texrgba | iter_alphablend;
	itarray->raststate	= rast_zcompare | rast_texclamp | rast_chromakeyoff;
	itarray->rastmask	= rast_nomask;
	itarray->texmap		= planet->RingTexture;

	bams_t angleoffs = BAMS_DEG360 / PLANET_RING_SEGMENTS;
	int seg = 0;
	for ( seg = 0; seg < PLANET_RING_SEGMENTS; seg++ ) {

		int vid = seg * 2;

		bams_t angle = seg * angleoffs;

		sincosval_s sincos;
		GetSinCos( angle, &sincos );

		// white vertex colour so the ring texture's own colours are used as-is;
		// alpha gives overall ring translucency (0xb0 ≈ 69%)
		byte r = 255;
		byte g = 255;
		byte b = 255;
		byte a = 0xb0;

		// U wraps around the circumference; V maps inner→outer ring edge
		int u = ( seg * texwidth ) / PLANET_RING_SEGMENTS;

		// inner vertex (closest to planet surface)
		itarray->Vtxs[ vid ].X = GEOMV_MUL( planet->BoundingSphere + planet->RingInnerRadius, sincos.cosval );
		itarray->Vtxs[ vid ].Y = GEOMV_0;
		itarray->Vtxs[ vid ].Z = GEOMV_MUL( planet->BoundingSphere + planet->RingInnerRadius, sincos.sinval );
		itarray->Vtxs[ vid ].W = GEOMV_1;
		itarray->Vtxs[ vid ].U = u;
		itarray->Vtxs[ vid ].V = 0;
		itarray->Vtxs[ vid ].R = r;
		itarray->Vtxs[ vid ].G = g;
		itarray->Vtxs[ vid ].B = b;
		itarray->Vtxs[ vid ].A = a;

		// outer vertex (farthest from planet surface)
		itarray->Vtxs[ vid + 1 ].X = GEOMV_MUL( planet->BoundingSphere + planet->RingOuterRadius, sincos.cosval );
		itarray->Vtxs[ vid + 1 ].Y = GEOMV_0;
		itarray->Vtxs[ vid + 1 ].Z = GEOMV_MUL( planet->BoundingSphere + planet->RingOuterRadius, sincos.sinval );
		itarray->Vtxs[ vid + 1 ].W = GEOMV_1;
		itarray->Vtxs[ vid + 1 ].U = u;
		itarray->Vtxs[ vid + 1 ].V = PLANET_RING_TEX_HEIGHT;
		itarray->Vtxs[ vid + 1 ].R = r;
		itarray->Vtxs[ vid + 1 ].G = g;
		itarray->Vtxs[ vid + 1 ].B = b;
		itarray->Vtxs[ vid + 1 ].A = a;
	}

	size_t numtriindxs = PLANET_RING_SEGMENTS * 6;

	uint16 *vindxs = (uint16 *) ALLOCMEM( numtriindxs * sizeof( uint16 ) );
	if ( vindxs == NULL )
		OUTOFMEM( 0 );

	int dstindx = 0;
	int srcindx = 0;

	// store all strip indexes but last
	for ( seg = 0; seg < PLANET_RING_SEGMENTS - 1; seg++ ) {

		vindxs[ dstindx + 0 ] = srcindx;
		vindxs[ dstindx + 1 ] = srcindx + 1;
		vindxs[ dstindx + 2 ] = srcindx + 3;
		dstindx += 3;

		vindxs[ dstindx + 0 ] = srcindx;
		vindxs[ dstindx + 1 ] = srcindx + 2;
		vindxs[ dstindx + 2 ] = srcindx + 3;
		dstindx += 3;

		srcindx += 2;
	}

	// store last strip index
	vindxs[ dstindx + 0 ] = srcindx;
	vindxs[ dstindx + 1 ] = srcindx + 1;
	vindxs[ dstindx + 2 ] = 1;
	dstindx += 3;

	vindxs[ dstindx + 0 ] = srcindx;
	vindxs[ dstindx + 1 ] = 0;
	vindxs[ dstindx + 2 ] = 1;

	// calculate transformation matrix using position only (no planet rotation),
	// then apply the fixed ring tilt angles so rings stay stationary while the
	// planet surface spins beneath them
	Xmatrx ring_matrix;
	MakeIdMatrx( ring_matrix );
	ring_matrix[ 0 ][ 3 ] = planet->ObjPosition[ 0 ][ 3 ];
	ring_matrix[ 1 ][ 3 ] = planet->ObjPosition[ 1 ][ 3 ];
	ring_matrix[ 2 ][ 3 ] = planet->ObjPosition[ 2 ][ 3 ];
	if ( planet->RingTiltX != 0 )
		ObjRotX( ring_matrix, planet->RingTiltX );
	if ( planet->RingTiltZ != 0 )
		ObjRotZ( ring_matrix, planet->RingTiltZ );
	MtxMtxMUL( ViewCamera, ring_matrix, DestXmatrx );

	// setup transformation matrix
	D_LoadIterMatrix( DestXmatrx );

	// lock array
	D_LockIterArray3( itarray, 0, itarray->NumVerts );

	// draw indexed triangles in a single call (no far-plane clipping!)
	D_DrawIterArrayIndexed(
		ITERARRAY_MODE_TRIANGLES, numtriindxs, vindxs, 0x3d );

	// unlock array
	D_UnlockIterArray();

	// restore identity transformation
	D_LoadIterMatrix( NULL );

	// free vertex index array
	FREEMEM( vindxs );

	// free vertex array
	FREEMEM( itarray );
}


// planet effect drawing callback ---------------------------------------------
//
int PlanetDraw( void *param )
{
	if ( headless_bot )
		return TRUE;
	ASSERT( param != NULL );
	Planet *planet = (Planet *) param;

	// temporarily override FaceList[0].TexMap for this instance only.
	// FaceList is shared class-level data, so we save and restore around
	// the draw call to keep each planet's texture independent.
	// NOTE: do NOT guard on NumFaces — R_DrawPlanet zeroes NumFaces every
	// frame to suppress standard polygon rendering, but FaceList itself
	// remains a valid pointer into the class data regardless.
	TextureMap *saved_texmap = NULL;
	if ( planet->SurfTexture != NULL && planet->FaceList != NULL ) {
		saved_texmap = planet->FaceList[ 0 ].TexMap;
		planet->FaceList[ 0 ].TexMap = planet->SurfTexture;
	}

	// draw sphere
	R_DrawPlanet( planet );

	// restore shared FaceList so other instances are unaffected
	if ( planet->SurfTexture != NULL && planet->FaceList != NULL ) {
		planet->FaceList[ 0 ].TexMap = saved_texmap;
	}

	if ( planet->HasRing ) {
		PlanetDraw_Rings( planet );
	}

	if ( orbit_depth == 1.0f ) {
		return FALSE;
	}

	colrgba_s glowcol;

	int val = (int)(255 * 4 - ( orbit_depth * 255 ) * 4);

	glowcol.R = ( val > 255 ) ? 255 : val;
	glowcol.G = 0;
	glowcol.B = 0;
	glowcol.A = glowcol.R;

	VIDs_SetScreenToColor( glowcol );

	orbit_depth = 1.0f;

	return TRUE;
}


// planet constructor (class instantiation) -----------------------------------
//
PRIVATE
void PlanetInstantiate( CustomObject *base )
{
	ASSERT( base != NULL );
	Planet *planet = (Planet *) base;

	planet->RingTexture = FetchTextureMap( planet->RingTexName );
	if ( planet->RingTexture == NULL ) {
		MSGOUT( "texture '%s' was not found.", planet->RingTexName );
	}

	// apply surface texture override if a name was already set
	// (can happen when NET_ExecRmEvPlanet sets SurfTexName before SummonObject)
	if ( planet->SurfTexName[ 0 ] != '\0' ) {
		planet->SurfTexture = FetchTextureMap( planet->SurfTexName );
		// NOTE: do NOT write to FaceList[0].TexMap here — FaceList is
		// shared class data.  PlanetDraw handles the per-instance override.
	}
}

// callback type and flags ----------------------------------------------------
//
//static int callback_type = CBTYPE_DRAW_CUSTOM_ITER | CBFLAG_REMOVE;
static int callback_type = CBTYPE_DRAW_OBJECTS | CBFLAG_REMOVE;

#endif // !PARSEC_SERVER


// planet animation callback --------------------------------------------------
// PUBLIC: also called directly from g_main_sv.cpp::_WalkCustomObjects()
//
int PlanetAnimate( CustomObject *base )
{
	ASSERT( base != NULL );
	Planet *planet = (Planet *) base;

	planet->CullMask = 0x00;		// no far plane clipping

	// simply rotate around Z
#ifndef PARSEC_SERVER
	ObjRotZ( planet->ObjPosition, planet->RotSpeed * CurScreenRefFrames );
#else
	ObjRotZ( planet->ObjPosition, planet->RotSpeed * TheSimulator->GetThisFrameRefFrames() );
#endif

	return TRUE;
}


// planet destructor (instance destruction) -----------------------------------
//
PRIVATE
void PlanetDestroy( CustomObject *base )
{
	ASSERT( base != NULL );

#ifndef PARSEC_SERVER
	// ensure pending callbacks are destroyed to avoid
	// calling them with invalid pointers
	int numremoved = CALLBACK_DestroyCallback( callback_type, (void *) base );
	if ( !headless_bot )
		ASSERT( numremoved <= 1 );
#endif
}


// planet collision callback --------------------------------------------------
// On the server, collision detection is handled by G_CollDet::_CheckShipPlanetCollision().
// On the client: registers the draw callback and computes proximity warning.
//
PRIVATE
int PlanetCollide( CustomObject *base )
{
	ASSERT( base != NULL );

#ifndef PARSEC_SERVER
	// always register the draw callback so the planet renders
	CALLBACK_RegisterCallback( callback_type, PlanetDraw, (void *) base );

	// proximity warning — only meaningful when the local ship exists
	if ( headless_bot || MyShip == NULL )
		return TRUE;

	Planet *planet = (Planet *) base;

	// distance from ship to planet centre
	float dx = MyShip->ObjPosition[ 0 ][ 3 ] - planet->ObjPosition[ 0 ][ 3 ];
	float dy = MyShip->ObjPosition[ 1 ][ 3 ] - planet->ObjPosition[ 1 ][ 3 ];
	float dz = MyShip->ObjPosition[ 2 ][ 3 ] - planet->ObjPosition[ 2 ][ 3 ];
	float dist = sqrt( dx*dx + dy*dy + dz*dz );

	float radius   = GEOMV_TO_FLOAT( planet->BoundingSphere );
	float warn_far = radius * 1.5f;	// outer edge of warning zone
	float warn_mid = radius * 1.1f;	// inner edge — full red (kill happens at 1.0x)

	if ( dist < warn_far ) {
		// depth in warning zone: 0.0 = outer edge, 1.0 = at surface
		float depth = ( warn_far - dist ) / ( warn_far - warn_mid );
		if ( depth > 1.0f ) depth = 1.0f;

		// keep strongest warning across all planets this frame
		if ( depth > ( 1.0f - orbit_depth ) )
			orbit_depth = 1.0f - depth;

		// rate-limited warning message — at most once every ~300 frames (~5 s)
		static dword last_warn_frame = 0;
		if ( ( CurVisibleFrame - last_warn_frame ) > 300 ) {
			last_warn_frame = CurVisibleFrame;
			ShowMessage( "WARNING: Entering planetary gravity well!" );
		}
	}
#endif

	return TRUE;
}


// handle persistency (server → client sync) ----------------------------------
//
PRIVATE
int PlanetPersistToStream( CustomObject *base, int tostream, void *rl )
{
#ifdef PARSEC_SERVER
	ASSERT( base != NULL );
	ASSERT( tostream == TRUE );
	Planet *planet = (Planet *) base;

	// determine RE size
	size_t size = E_REList::RmEvGetSizeFromType( RE_PLANET );

	// write to RE list
	if ( rl != NULL ) {

		E_REList *pREList = (E_REList *)rl;

		RE_Planet *re_planet = (RE_Planet *)pREList->NET_Allocate( RE_PLANET );
		ASSERT( re_planet != NULL );

		re_planet->hostid         = planet->HostObjNumber;
		re_planet->pos[ 0 ]       = planet->ObjPosition[ 0 ][ 3 ];
		re_planet->pos[ 1 ]       = planet->ObjPosition[ 1 ][ 3 ];
		re_planet->pos[ 2 ]       = planet->ObjPosition[ 2 ][ 3 ];
		re_planet->rotspeed       = planet->RotSpeed;
		re_planet->boundsphere    = GEOMV_TO_FLOAT( planet->BoundingSphere );
		re_planet->hasring        = planet->HasRing;
		re_planet->ringinnerradius = planet->RingInnerRadius;
		re_planet->ringouterradius = planet->RingOuterRadius;
		re_planet->ringtiltx      = planet->RingTiltX;
		re_planet->ringtiltz      = planet->RingTiltZ;
		strncpy( re_planet->ringtexname, planet->RingTexName, sizeof( re_planet->ringtexname ) - 1 );
		re_planet->ringtexname[ sizeof( re_planet->ringtexname ) - 1 ] = '\0';
		strncpy( re_planet->surtexname, planet->SurfTexName, sizeof( re_planet->surtexname ) - 1 );
		re_planet->surtexname[ sizeof( re_planet->surtexname ) - 1 ] = '\0';
	}

	return (int)size;
#else
	return 0;
#endif
}


// register object type for planet --------------------------------------------
//
PRIVATE
void PlanetRegisterCustomType()
{
	custom_type_info_s info;
	memset( &info, 0, sizeof( info ) );

	info.type_name			= "planet";
	info.type_id			= 0x00000000;
	info.type_size			= sizeof( Planet );
	info.type_template		= NULL;
	info.type_flags			= CUSTOM_TYPE_DEFAULT;
	info.callback_init		= PlanetInitType;
#ifndef PARSEC_SERVER
	info.callback_instant	= PlanetInstantiate;
#else
	info.callback_instant	= NULL;
#endif
	info.callback_destroy	= PlanetDestroy;
	info.callback_animate	= PlanetAnimate;
	info.callback_collide	= PlanetCollide;
	info.callback_notify	= NULL;
	info.callback_persist	= PlanetPersistToStream;

	planet_type_id = OBJ_RegisterCustomType( &info );
	CON_RegisterCustomType( info.type_id, Planet_PropList );
}


// module registration function -----------------------------------------------
//
REGISTER_MODULE( G_PLANET )
{
	// register type
	PlanetRegisterCustomType();
}
