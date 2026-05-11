/*
 * PARSEC HEADER: g_asteroid.h
 */

#ifndef _G_ASTEROID_H_
#define _G_ASTEROID_H_


// asteroid limits and constants -----------------------------------------------
//
#define MAX_ASTEROID_SURF_TEXNAME	63


// asteroid custom type structure ----------------------------------------------
//
struct Asteroid : CustomObject {

	bams_t		RotSpeedX;			// 3-axis tumble
	bams_t		RotSpeedY;
	bams_t		RotSpeedZ;
	bams_t		CurOrbitPos;
	bams_t		OrbitSpeed;
	geomv_t		OrbitRadius;
	GenObject*	OrbitParent;
	int			OrbitShape;			// eccentricity: 0=circle, 100=sharp ellipse
	dword		OrbitParentId;		// HostObjNumber of orbit centre (0 = world origin)
	int			NoiseSeed;			// seeded per-instance for reproducible jagged shape

	char		SurfTexName[ MAX_ASTEROID_SURF_TEXNAME + 1 ];
	TextureMap*	SurfTexture;

#ifdef PARSEC_SERVER
	E_Distributable *pDist;
#endif
};


// external objects
extern dword asteroid_type_id;

// external functions
int AsteroidAnimate( CustomObject *base );


#endif // _G_ASTEROID_H_
