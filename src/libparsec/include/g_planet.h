/*
 * PARSEC HEADER: g_planet.h
 */

#ifndef _G_PLANET_H_
#define _G_PLANET_H_


// planet limits and constants ------------------------------------------------
//
#define MAX_PLANET_NAME			255
#define MAX_RING_TEXNAME		63

#define PLANET_RING_SEGMENTS	128

#define PLANET_RING_TEXTURE		"ringtex1"
#define PLANET_RING_TEX_WIDTH	16
#define PLANET_RING_TEX_HEIGHT	64


// planet custom type structure -----------------------------------------------
//
struct Planet : PlanetObject {

	bams_t		RotSpeed;
	bams_t		CurOrbitPos;
	bams_t		OrbitSpeed;
	geomv_t		OrbitRadius;
	GenObject*	OrbitParent;
	int			HasRing;
	geomv_t		RingInnerRadius;
	geomv_t		RingOuterRadius;
	char		RingTexName[ MAX_RING_TEXNAME + 1 ];
	TextureMap*	RingTexture;

	char    	Name[ MAX_PLANET_NAME + 1 ];
	int			NumOrbitShips;

#ifdef PARSEC_SERVER
	E_Distributable *pDist;
#endif
};


// external objects
extern dword planet_type_id;

// external functions
int PlanetAnimate( CustomObject *base );


#endif // _G_PLANET_H_
