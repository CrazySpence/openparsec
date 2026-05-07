//-----------------------------------------------------------------------------
//	BSPLIB HEADER: ObjFile.h
//
//  Wavefront OBJ format input for BspLib / makeodt.
//  Allows models authored in Blender, Maya, etc. to be converted to OD2.
//-----------------------------------------------------------------------------

#ifndef _OBJFILE_H_
#define _OBJFILE_H_

// bsplib header files
#include "BspLibDefs.h"
#include "InputData3D.h"


BSPLIB_NAMESPACE_BEGIN


// file input class for Wavefront OBJ format files ----------------------------
//
class ObjFile : public InputData3D {

public:
	ObjFile( BspObjectList objectlist, const char *filename );
	~ObjFile();

public:
	int		ParseObjectData();
};


BSPLIB_NAMESPACE_END


#endif // _OBJFILE_H_
