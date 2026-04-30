/*
 * PARSEC HEADER: g_wfx.h
 */

#ifndef _G_WFX_H_
#define _G_WFX_H_


// constants for particle weapons

enum {

	PARTICLEGUN_SPREADFIRE
};


// external functions

#ifndef PARSEC_SERVER

void	WFX_InitParticleSizes( float resoscale );

int		WFX_ShootParticleWeapon( ShipObject *shippo, int type );
void	WFX_RemoteShootSpreadfire( int playerid );

void	WFX_RemoteActivateHelix( int playerid );
void	WFX_RemoteDeactivateHelix( int playerid );

void	WFX_RemoteActivateLightning( int playerid );
void	WFX_RemoteDeactivateLightning( int playerid );

struct photon_sphere_pcluster_s;
void    WFX_CalcPhotonSphereAnimation( photon_sphere_pcluster_s *cluster );
void    WFX_RemoteActivatePhoton( int playerid );
void    WFX_RemoteDeactivatePhoton( int playerid );

#endif // !PARSEC_SERVER

// server_use_re_collision: server passes true for real network clients so the
// function skips creating server-side collision particles (RE_HelixParticle
// from the client provides authoritative geometry instead).  Client always
// uses the default (false).
int		WFX_MaintainHelix( ShipObject *shippo, int playerid, bool server_use_re_collision = false );
int		WFX_ActivateHelix( ShipObject *shippo );

#ifdef PARSEC_SERVER
// Create a collision particle on the server from a client-reported position.
// Called by the RE_HelixParticle handler in e_simnetinput.cpp.
void    SV_CreateHelixCollisionParticle( int playerid,
                                          geomv_t x,  geomv_t y,  geomv_t z,
                                          geomv_t vx, geomv_t vy, geomv_t vz,
                                          int rtt_ms );
#endif // PARSEC_SERVER
void	WFX_DeactivateHelix( ShipObject *shippo );
void	WFX_EnsureHelixInactive( ShipObject *shippo );

void	WFX_MaintainLightning( ShipObject *shippo );
int		WFX_ActivateLightning( ShipObject *shippo );
void	WFX_DeactivateLightning( ShipObject *shippo );
void	WFX_EnsureLightningInactive( ShipObject *shippo );

int     WFX_ActivatePhoton( ShipObject *shippo );
void    WFX_DeactivatePhoton( ShipObject *shippo );
void    WFX_EnsurePhotonInactive( ShipObject *shippo );

void	WFX_EnsureParticleWeaponsInactive( ShipObject *shippo );


#endif // _G_WFX_H_
