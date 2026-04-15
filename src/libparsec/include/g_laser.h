/*
 * PARSEC HEADER: g_laser.h
 */

#ifndef _G_LASER_H_
#define _G_LASER_H_


// external functions

int		KillLaserBeam( dword laserbeamobjno );

#ifndef PARSEC_SERVER
int		CreateLaserBeam( GenObject *ownerpo, dword targetobjno, dword *laserbeamobjno );
#endif


#endif // _G_LASER_H_
