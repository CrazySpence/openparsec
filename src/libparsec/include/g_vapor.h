/*
 * PARSEC HEADER: g_vapor.h
 */

#ifndef _G_VAPOR_H_
#define _G_VAPOR_H_

// vapor trails are purely client-side (visual only, never synced)
#ifndef PARSEC_SERVER

void CreateVaporTrail( GenObject *ownerpo );

#endif // !PARSEC_SERVER

#endif // _G_VAPOR_H_
