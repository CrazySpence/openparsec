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

int		WFX_MaintainHelix( ShipObject *shippo, int playerid );
int		WFX_ActivateHelix( ShipObject *shippo );
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
