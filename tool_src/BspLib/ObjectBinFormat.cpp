//-----------------------------------------------------------------------------
//	BSPLIB MODULE: ObjectBinFormat.cpp
//
//  Copyright (c) 1998-1999 by Markus Hadwiger
//  All Rights Reserved.
//-----------------------------------------------------------------------------

// bsplib header files
#include "BspLibDefs.h"
#include "ObjectBinFormat.h"
#include "Transform2.h"

// parsec header files
#include "../../src/libparsec/include/od_geomv.h"
#include "../../src/libparsec/include/od_odt.h"


#define VERTEX_SCALE_FAC ( 1.0 / 20.0 )

// Use compiler-provided byte-order macros to detect true host endianness.
// NOTE: Do NOT use the POSIX BIG_ENDIAN/LITTLE_ENDIAN constants — on macOS
// those are numeric values (4321 / 1234) defined even on little-endian hosts,
// so #ifdef BIG_ENDIAN is always true on macOS regardless of architecture.
// The OD2 files must be written in host byte order so the engine (which uses
// SWAP_32 = identity on little-endian) can read them without swapping.
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
  // True big-endian host (e.g. PowerPC): write big-endian, no swap needed.
  #define SWAP_16(s)   ( s )
  #define SWAP_32(l)   ( l )
#else
  // Little-endian host (x86, ARM, etc.): write little-endian, no swap needed.
  #define SWAP_16(s)   ( s )
  #define SWAP_32(l)   ( l )
#endif


void OD2_Geomv_out( float *value )
{

  dword tmp = SWAP_32( DW32( *value ) );
  *(dword *)value = tmp;

}


BSPLIB_NAMESPACE_BEGIN


// calculate affine mapping using face's mapping specification ----------------
//
void ObjectBinFormat::ODT_CalcAffineMapping( Face& face, dword *dmatrx )
{
	Vertex2	vtx;
	double	mat[3][3];
	int i = 0;
	// build xyw-matrix
	for (  i = 0; i < 3; i++ ) {
		vtx = face.MapXY( i );
		mat[ 0 ][ i ] = vtx.getX() *  VERTEX_SCALE_FAC;
		mat[ 1 ][ i ] = vtx.getY() * -VERTEX_SCALE_FAC;
		mat[ 2 ][ i ] = vtx.getW() * -VERTEX_SCALE_FAC;
	}
	Transform2 xyw( (const double(*)[3]) mat );

	// build uv1-matrix
	for ( i = 0; i < 3; i++ ) {
		vtx = face.MapUV( i );
		mat[ 0 ][ i ] = vtx.getX();
		mat[ 1 ][ i ] = vtx.getY();
		mat[ 2 ][ i ] = vtx.getW();
	}
	Transform2 uv1( (const double(*)[3]) mat );

	// invert uv1
	Transform2 uv1i;
	if ( !uv1.Inverse( uv1i ) ) {
		ErrorMessage( "ObjectBinFormat::ODT_CalcAffineMapping(): Collinear mapping coordinates encountered!" );
	}

	// calculate affine mapping
	Transform2 map( xyw );
	map.Concat( uv1i );

	// store result into destination structure (9 coefficients)
	double (*affinemap)[ 3 ] = ( double (*)[3] ) map.LinMatrixAccess();
	fixed_t (*dest)[ 4 ] = (fixed_t (*)[4]) dmatrx;
	for ( i = 0; i < 3; i++ ) {
		dest[ i ][ 0 ] = FLOAT_TO_FIXED( affinemap[ i ][ 0 ] );
		dest[ i ][ 1 ] = FLOAT_TO_FIXED( affinemap[ i ][ 1 ] );
		dest[ i ][ 2 ] = FLOAT_TO_FIXED( 0.0 ); // third column is zero
		dest[ i ][ 3 ] = FLOAT_TO_FIXED( affinemap[ i ][ 2 ] );
	}
}


// create object recognizeable by engine (ODT format) -------------------------
//
byte *ObjectBinFormat::ODT_CreateEngineObject( int& memblocksize )
{
	// fetch object data lists
	VertexChunk& vtxlist  = getVertexList();
	PolygonList& polylist = getPolygonList();
	FaceChunk& facelist   = getFaceList();
	TextureChunk& texlist = getTextureList();

	// calculate some basic numbers
	int numvertices	= getNumVertices();
	int numnormals	= getNumNormals();
	int numallvtxs	= numvertices + numnormals;
	int numpolygons	= BSPTreeAvailable() ? getBspPolygons() : getNumPolygons();
	int numfaces	= getNumFaces();
	int numbspnodes	= getBspPolygons();

	// count number of vertices of all polygons
	int numvertexindices = 0;
	if ( BSPTreeAvailable() )
		bsptree->SumVertexNums( numvertexindices );
	else
		numvertexindices = polylist.FetchHead()->SumVertexNumsEntireList();

	// calculate size of entire object structure
	size_t objectmemsize = sizeof( ODT_GenObject ) +									// generic object header
						sizeof( ODT_Vertex3 ) * numallvtxs +						// object space vertices
						sizeof( ODT_Vertex3 ) * numallvtxs +						// view space vertices
						sizeof( ODT_ProjPoint ) * numallvtxs +						// projected vertices
						sizeof( ODT_SPoint ) * numallvtxs +							// screen space vertices
						sizeof( ODT_Poly ) * numpolygons +							// polygon control data
						sizeof( dword ) * numvertexindices +						// polygon vertindx lists
						sizeof( ODT_Face ) * numfaces +								// face control data
						sizeof( ODT_VisPolys ) + sizeof( dword ) * numpolygons +	// vispolylist
						sizeof( ODT_BSPNode ) * ( numbspnodes + 1 );				// bsp tree

	// allocate memory for all object data (only excluding texturemaps)
	ODT_GenObject *binobj = (ODT_GenObject *) new char[ objectmemsize ];
	memset( binobj, 0x00, objectmemsize );

	// fill in objectheader
	binobj->NextObj			= NULL;
	binobj->PrevObj			= NULL;
	binobj->NextVisObj		= NULL;
	binobj->InstanceSize	= sizeof( ODT_GenObject );
	binobj->NumVerts		= numallvtxs;
	binobj->NumPolyVerts	= numvertices;
	binobj->NumNormals		= numnormals;
	binobj->VertexList		= (ODT_Vertex3 *) ( binobj + 1 );
	binobj->X_VertexList	= (ODT_Vertex3 *)
		( (char *) binobj->VertexList + sizeof( ODT_Vertex3 ) * numallvtxs );
	binobj->P_VertexList	= (ODT_ProjPoint *)
		( (char *) binobj->X_VertexList + sizeof( ODT_Vertex3 ) * numallvtxs );
	binobj->S_VertexList	= (ODT_SPoint *)
		( (char *) binobj->P_VertexList + sizeof( ODT_ProjPoint ) * numallvtxs );
	binobj->NumPolys		= numpolygons;
	binobj->PolyList		= (ODT_Poly *)
		( (char *) binobj->S_VertexList + sizeof( ODT_SPoint ) * numallvtxs );
	binobj->NumFaces		= numfaces;
	binobj->FaceList		= (ODT_Face *)
		( (char *) binobj->PolyList + sizeof( ODT_Poly ) * numpolygons + sizeof( dword ) * numvertexindices );
	binobj->VisPolyList		= (ODT_VisPolys *)
		( (char *) binobj->FaceList + sizeof( ODT_Face ) * numfaces );
	binobj->BSPTree			= (ODT_BSPNode *)
		( (char *) binobj->VisPolyList + sizeof( ODT_VisPolys ) + sizeof( dword ) * numpolygons );

	// calculate bounding sphere for object
	double maxx = -100000; double minx = 100000;
	double maxy = -100000; double miny = 100000;
	double maxz = -100000; double minz = 100000;
	double maxsphere = 0;
	int i = 0;
	for (  i = 0; i < numvertices; i++ ) {
		// calc bounding sphere
		double ctlength = ( (Vector3) vtxlist[ i ] ).VecLength();
		if ( ctlength > maxsphere )
			maxsphere = ctlength;
		// calc bounding box
		if ( vtxlist[ i ].getX() > maxx ) maxx = vtxlist[ i ].getX();
		if ( vtxlist[ i ].getY() > maxy ) maxy = vtxlist[ i ].getY();
		if ( vtxlist[ i ].getZ() > maxz ) maxz = vtxlist[ i ].getZ();
		if ( vtxlist[ i ].getX() < minx ) minx = vtxlist[ i ].getX();
		if ( vtxlist[ i ].getY() < miny ) miny = vtxlist[ i ].getY();
		if ( vtxlist[ i ].getZ() < minz ) minz = vtxlist[ i ].getZ();
	}

	maxsphere *= VERTEX_SCALE_FAC;
	binobj->BoundingSphere  = FLOAT_TO_FIXED( maxsphere );
	binobj->BoundingSphere2 = FLOAT_TO_FIXED( maxsphere * maxsphere );

	// fill vertex list -----------------------------------
	ODT_Vertex3 *vfillp = binobj->VertexList;
	// store face normals first
	for ( i = 0; i < numnormals; i++, vfillp++ ) {
		Vector3 normal( facelist[ i ].getPlaneNormal() );
		vfillp->X	  = FLOAT_TO_FIXED(  normal.getX() );
		vfillp->Y	  = FLOAT_TO_FIXED( -normal.getY() );
		vfillp->Z	  = FLOAT_TO_FIXED( -normal.getZ() );
		vfillp->Flags = 0x00000000L;
	}
	// store real vertices after face normals
	for ( i = 0; i < numvertices; i++, vfillp++ ) {
		vfillp->X	  = FLOAT_TO_FIXED(  vtxlist[ i ].getX() * VERTEX_SCALE_FAC );
		vfillp->Y	  = FLOAT_TO_FIXED( -vtxlist[ i ].getY() * VERTEX_SCALE_FAC );
		vfillp->Z	  = FLOAT_TO_FIXED( -vtxlist[ i ].getZ() * VERTEX_SCALE_FAC );
		vfillp->Flags = 0x00000000L;
	}

	// build flat bsp tree if not available
	if ( BSPTreeAvailable() && !BSPTreeFlatAvailable() ) {
		//TODO:
		// implement flat->linked
	}

	// fill polygon array and vertex index arrays----------
	ODT_Poly *pfillp = binobj->PolyList;
	char *vertbase = (char *) pfillp + sizeof( ODT_Poly ) * numpolygons;
	int  countofs  = 0;
	if ( BSPTreeFlatAvailable() ) {
		// scan polygons of flat bsp tree
		int numnodes = bsptreeflat.getNumNodes();
		for ( i = 1; i <= numnodes; i++ ) {
			// node zero does not correspond to any polygon
			// and is also not included in the number of nodes!
			BSPNodeFlat *node = bsptreeflat.FetchNodePerId( i );
			Polygon *polyscan = node->getPolygon();
			int polyno = polyscan->getId();
			pfillp[ polyno ].NumVerts  = polyscan->getNumVertices();
			pfillp[ polyno ].FaceIndx  = polyscan->getFaceId();
			pfillp[ polyno ].VertIndxs = (dword *) polyscan;
		}
		// scan polygons once again to assign vertex index lists
		// in order instead of in the order of bsp nodes
		for ( i = 1; i <= numnodes; i++, pfillp++ ) {
			Polygon *polyscan = (Polygon *) pfillp->VertIndxs;
			pfillp->VertIndxs = (dword *) ( vertbase + countofs );
			// fill in array of vertex indexes
			polyscan->FillVertexIndexArray( pfillp->VertIndxs );
			countofs += sizeof( dword ) * pfillp->NumVerts;
		}
	} else {
		// scan entire polygon list
		Polygon *polyscan = polylist.FetchHead();
		for ( i = 0; i < polylist.getNumElements(); i++, polyscan = polyscan->getNext(), pfillp++ ) {
			pfillp->NumVerts = polyscan->getNumVertices();
			pfillp->FaceIndx = polyscan->getFaceId();
			pfillp->VertIndxs = (dword *) ( vertbase + countofs );
			// fill in array of vertex indexes
			polyscan->FillVertexIndexArray( pfillp->VertIndxs );
			countofs += sizeof( dword ) * pfillp->NumVerts;
		}
	}
	// correct vertex indexes to take face normals into account
	dword *dfillp = (dword *) vertbase;
	for ( i = 0; i < numvertexindices; i++ )
		*dfillp++ += numnormals;

	//NOTE:
	// the object contains a pointer to the polygon list.
	// this list contains all the polygon structures (no vertex indexes!)
	// the vertex index lists for all the polygons of the object follow
	// contiguously after all the polygon structures.

	// fill face list (defines surface properties) --------
	ODT_Face *ffillp = binobj->FaceList;
	for ( i = 0; i < numfaces; i++, ffillp++ ) {
		ffillp->TexMap			= NULL;
		ffillp->TexEqui 		= NULL;
		ffillp->ColorRGB		= 0;
		ffillp->ColorIndx		= 0;
		ffillp->FaceNormalIndx	= i;
		ffillp->Shading 		= facelist[ i ].getShadingType() & Face::base_mask;

		// write color if any attached and valid
		if ( facelist[ i ].getShadingType() & Face::color_mask ) {
			int coltype = facelist[ i ].getColorType();
			if ( coltype == Face::indexed_col ) {
				dword colindx;
				facelist[ i ].getColorIndex( colindx );
				ffillp->ColorIndx = ( ( ( ( ( colindx << 8 ) + colindx ) << 8 ) + colindx ) << 8 ) + colindx;
			} else if ( coltype == Face::rgb_col ) {
				ColorRGBA coltuple;
				facelist[ i ].getColorRGBA( coltuple );
				dword colrgb = ( ( ( ( ( coltuple.A << 8 ) + coltuple.B ) << 8 ) + coltuple.G ) << 8 ) + coltuple.R;
				ffillp->ColorRGB = colrgb;
			}
		}

		// attach texture
		if ( facelist[ i ].getShadingType() & Face::texmap_mask ) {
			const char *texname = facelist[ i ].getTextureName();
			// simply store name and calc mapping
			ffillp->TexMap = (char *) texname;
			ODT_CalcAffineMapping( facelist[ i ], (dword *) ffillp->TexXmatrx );
		}
	}

	// create bsp tree in object structure
	if ( BSPTreeFlatAvailable() ) {
		ODT_BSPNode *curbspnode = binobj->BSPTree + 1; // skip node at pos zero
		for ( int i = 1; i <= bsptreeflat.getNumNodes(); i++, curbspnode++ ) {
			BSPNodeFlat *node = bsptreeflat.FetchNodePerId( i );
			curbspnode->Polygon   = node->getPolygon()->getId();
			curbspnode->Contained = node->getContainedList();
			//curbspnode->BackList= node->getBackList(); //NOTE: not implemented!
			curbspnode->FrontTree = node->getFrontSubTree();
			curbspnode->BackTree  = node->getBackSubTree();
		}
	}

	memblocksize = objectmemsize;
	return (byte *) binobj;
}


// create object that can be saved to file as single block (ODT format) -------
//
byte *ObjectBinFormat::ODT_CreateFileObject( int& memblocksize, byte *engineobj )
{
	ODT_GenObject *binobj = (ODT_GenObject *) engineobj;
	TextureChunk& texlist = getTextureList();

	// create table of texture names
	char **texnameaddxs;
	char *texturenames, *nexttexname;
	int numtextures = texlist.getNumElements();
	int texnamesize = 0;
	int i = 0;
	if ( numtextures > 0 ) {
		for (  i = 0; i < numtextures; i++ )
			texnamesize += strlen( texlist[ i ].getName() ) + 1;
		texturenames = new char[ texnamesize ];
		nexttexname  = texturenames;
		texnameaddxs = new char*[ numtextures ];
		for ( i = 0; i < numtextures; i++ ) {
			strcpy( nexttexname, texlist[ i ].getName() );
			texnameaddxs[ i ] = nexttexname;
			nexttexname += strlen( nexttexname ) + 1;
		}
	}

	// correct texture pointers to point to texture names in block
	ODT_Face *facescan = binobj->FaceList;
	dword j = 0;
	for (  j = 0; j < binobj->NumFaces; j++, facescan++ )
		if ( facescan->TexMap != NULL )
			for ( int k = 0; k < numtextures; k++ )
				if ( strcmp( texnameaddxs[ k ], facescan->TexMap ) == 0 ) {
//					delete facescan->TexMap;	// legacy
					facescan->TexMap = (char *)
						( (ptrdiff_t) texnameaddxs[ k ] - (ptrdiff_t) texturenames + memblocksize );
					break;
				}

	// make absolute pointers to vertex index lists header relative
	ODT_Poly *polylist = binobj->PolyList;
	for ( j = 0; j < binobj->NumPolys; j++, polylist++ ) {
		polylist->VertIndxs = (dword *) ( (ptrdiff_t) polylist->VertIndxs - (ptrdiff_t) binobj );
	}

	// correct absolute pointers in object header to header-relative pointers
	binobj->VertexList	 = (ODT_Vertex3 *)		( (ptrdiff_t) binobj->VertexList	- (ptrdiff_t) binobj );
	binobj->X_VertexList = (ODT_Vertex3 *)		( (ptrdiff_t) binobj->X_VertexList	- (ptrdiff_t) binobj );
	binobj->P_VertexList = (ODT_ProjPoint *)	( (ptrdiff_t) binobj->P_VertexList	- (ptrdiff_t) binobj );
	binobj->S_VertexList = (ODT_SPoint *)		( (ptrdiff_t) binobj->S_VertexList	- (ptrdiff_t) binobj );
	binobj->PolyList	 = (ODT_Poly *)			( (ptrdiff_t) binobj->PolyList		- (ptrdiff_t) binobj );
	binobj->FaceList	 = (ODT_Face *)			( (ptrdiff_t) binobj->FaceList		- (ptrdiff_t) binobj );
	binobj->VisPolyList	 = (ODT_VisPolys *)		( (ptrdiff_t) binobj->VisPolyList	- (ptrdiff_t) binobj );
	binobj->BSPTree		 = (ODT_BSPNode *)		( (ptrdiff_t) binobj->BSPTree		- (ptrdiff_t) binobj );

	// create block
	byte *block = new byte[ memblocksize + texnamesize ];
	memcpy( block, binobj, memblocksize );
	if ( texnamesize > 0 ) {
		memcpy( block + memblocksize, texturenames, texnamesize );
		memblocksize += texnamesize;
		// free texture name table
		delete texturenames;
		delete texnameaddxs;
	}

	return block;
}


// ----------------------------------------------------------------------------
//
#define DOUBLE_TO_OD2FLOAT(x)	(float)(x)


// calculate affine mapping using face's mapping specification ----------------
//
void ObjectBinFormat::OD2_CalcAffineMapping( Face& face, dword *dmatrx )
{
	Vertex2	vtx;
	double	mat[3][3];
	int i = 0;
	// build xyw-matrix
	for (  i = 0; i < 3; i++ ) {
		vtx = face.MapXY( i );
		mat[ 0 ][ i ] = vtx.getX() *  VERTEX_SCALE_FAC;
		mat[ 1 ][ i ] = vtx.getY() * -VERTEX_SCALE_FAC;
		mat[ 2 ][ i ] = vtx.getW() * -VERTEX_SCALE_FAC;
	}
	Transform2 xyw( (const double(*)[3]) mat );

	// build uv1-matrix
	for ( i = 0; i < 3; i++ ) {
		vtx = face.MapUV( i );
		mat[ 0 ][ i ] = vtx.getX();
		mat[ 1 ][ i ] = vtx.getY();
		mat[ 2 ][ i ] = vtx.getW();
	}
	Transform2 uv1( (const double(*)[3]) mat );

	// invert uv1
	Transform2 uv1i;
	if ( !uv1.Inverse( uv1i ) ) {
		// UV triangle is degenerate (collinear UV coordinates).
		// This should be caught upstream by ObjFile's triplet search, but handle
		// it here too as a safety net: leave the matrix as zeroes and return.
		// The engine will use an identity-like mapping for this face.
		return;
	}

	// calculate affine mapping
	Transform2 map( xyw );
	map.Concat( uv1i );
	// store result into destination structure (9 coefficients)
	double (*affinemap)[ 3 ] = ( double (*)[3] ) map.LinMatrixAccess();
	float (*dest)[ 4 ] = (float (*)[4]) dmatrx;
	for ( i = 0; i < 3; i++ ) {

	    dest[ i ][ 0 ] = DOUBLE_TO_OD2FLOAT( affinemap[ i ][ 0 ] );
 	    dest[ i ][ 1 ] = DOUBLE_TO_OD2FLOAT( affinemap[ i ][ 1 ] );
 	    dest[ i ][ 2 ] = DOUBLE_TO_OD2FLOAT( 0.0 );
 	    dest[ i ][ 3 ] = DOUBLE_TO_OD2FLOAT( affinemap[ i ][ 2 ] );

		OD2_Geomv_out( &dest[ i ][ 0 ] );
		OD2_Geomv_out( &dest[ i ][ 1 ] );
		OD2_Geomv_out( &dest[ i ][ 2 ] );
		OD2_Geomv_out( &dest[ i ][ 3 ] );
	}
}


// create object recognizeable by engine (OD2 format) -------------------------
//
// NOTE: All three on-disk structs (OD2_Root32, OD2_Poly32, OD2_Face32) use
// 32-bit dword fields for every pointer/offset.  The native OD2_Root/OD2_Poly/
// OD2_Face structs contain real C pointers which are 8 bytes on 64-bit hosts,
// making their sizeof() values larger than what the loader expects.  We always
// write the "32" variants so the file format is host-independent.
//
byte *ObjectBinFormat::OD2_CreateEngineObject( int& memblocksize )
{
	// fetch object data lists
	VertexChunk& vtxlist  = getVertexList();
	PolygonList& polylist = getPolygonList();
	FaceChunk& facelist   = getFaceList();
	TextureChunk& texlist = getTextureList();

	// calculate some basic numbers
	int numvertices	= getNumVertices();
	int numnormals	= getNumNormals();
	int numallvtxs	= numvertices + numnormals;
	int numpolygons	= BSPTreeAvailable() ? getBspPolygons() : getNumPolygons();
	int numfaces	= getNumFaces();

	// count number of vertices of all polygons
	int numvertexindices = 0;
	if ( BSPTreeAvailable() )
		bsptree->SumVertexNums( numvertexindices );
	else
		numvertexindices = polylist.FetchHead()->SumVertexNumsEntireList();

	// Calculate size using the fixed-width 32-bit on-disk structs
	size_t objectmemsize = sizeof( OD2_Root32 ) +					// generic object header
						sizeof( OD2_Vertex3 ) * numallvtxs +		// object space vertices
						sizeof( OD2_Poly32 )  * numpolygons +		// polygon control data
						sizeof( dword )       * numvertexindices +	// polygon vertindx lists
						sizeof( OD2_Face32 )  * numfaces;			// face control data

	// allocate memory for all object data (only excluding texturemaps)
	OD2_Root32 *binobj = (OD2_Root32 *) new char[ objectmemsize ];
	memset( binobj, 0x00, objectmemsize );

	// Pre-compute byte offsets of each data section (relative to binobj start)
	dword vtxOffset      = (dword) sizeof( OD2_Root32 );
	dword polyOffset     = vtxOffset      + (dword)( sizeof( OD2_Vertex3 ) * numallvtxs );
	dword vertIndxOffset = polyOffset     + (dword)( sizeof( OD2_Poly32 )  * numpolygons );
	dword faceOffset     = vertIndxOffset + (dword)( sizeof( dword ) * numvertexindices );

	// fill in objectheader (using OD2_Root32 field names — all pointer fields are dword)
	strcpy( binobj->odt2, "ODT2\0" );
	binobj->major		= 1;
	binobj->minor		= 0;
	binobj->rootflags	= 0x0000;
	binobj->rootflags2	= 0x0000;
	binobj->pNodeList	= 0;
	binobj->pChildren[ 0 ]	= 0;
	binobj->pChildren[ 1 ]	= 0;
	binobj->InstanceSize	= SWAP_32( sizeof( OD2_Root32 ) );
	binobj->NumVerts	= SWAP_32( numallvtxs );
	binobj->NumPolyVerts	= SWAP_32( numvertices );
	binobj->NumNormals	= SWAP_32( numnormals );
	// pVertexList / pPolyList / pFaceList hold block-relative byte offsets.
	// They are NOT yet SWAP_32'd here — OD2_CreateFileObject will do that.
	binobj->pVertexList	= vtxOffset;
	binobj->NumPolys	= numpolygons;		// NOT yet SWAP_32 (used natively in CreateFileObject)
	binobj->pPolyList	= polyOffset;
	binobj->NumFaces	= numfaces;		// NOT yet SWAP_32
	binobj->pFaceList	= faceOffset;
	binobj->NumTextures	= 0;			// set later in OD2_CreateFileObject

	// Working pointers into the allocated block
	OD2_Vertex3 *vfillp = (OD2_Vertex3 *)( (char *)binobj + vtxOffset );
	OD2_Poly32  *pfillp = (OD2_Poly32  *)( (char *)binobj + polyOffset );
	char *vertbase      =                   (char *)binobj + vertIndxOffset;
	OD2_Face32  *ffillp = (OD2_Face32  *)( (char *)binobj + faceOffset );

	// calculate bounding sphere for object
	double maxx = -100000; double minx = 100000;
	double maxy = -100000; double miny = 100000;
	double maxz = -100000; double minz = 100000;
	double maxsphere = 0;
	int i = 0;
	for (  i = 0; i < numvertices; i++ ) {
		// calc bounding sphere
		double ctlength = ( (Vector3) vtxlist[ i ] ).VecLength();
		if ( ctlength > maxsphere )
			maxsphere = ctlength;
		// calc bounding box
		if ( vtxlist[ i ].getX() > maxx ) maxx = vtxlist[ i ].getX();
		if ( vtxlist[ i ].getY() > maxy ) maxy = vtxlist[ i ].getY();
		if ( vtxlist[ i ].getZ() > maxz ) maxz = vtxlist[ i ].getZ();
		if ( vtxlist[ i ].getX() < minx ) minx = vtxlist[ i ].getX();
		if ( vtxlist[ i ].getY() < miny ) miny = vtxlist[ i ].getY();
		if ( vtxlist[ i ].getZ() < minz ) minz = vtxlist[ i ].getZ();
	}

	maxsphere *= VERTEX_SCALE_FAC;
	binobj->BoundingSphere = DOUBLE_TO_OD2FLOAT( maxsphere );
	OD2_Geomv_out( &binobj->BoundingSphere );

	// fill vertex list -----------------------------------
	// store face normals first
	for ( i = 0; i < numnormals; i++, vfillp++ ) {
		Vector3 normal( facelist[ i ].getPlaneNormal() );

		vfillp->X = DOUBLE_TO_OD2FLOAT( normal.getX() );
		vfillp->Y = DOUBLE_TO_OD2FLOAT( -normal.getY() );
		vfillp->Z = DOUBLE_TO_OD2FLOAT( -normal.getZ() );

		OD2_Geomv_out( &vfillp->X );
		OD2_Geomv_out( &vfillp->Y );
		OD2_Geomv_out( &vfillp->Z );

		vfillp->Flags = SWAP_32( 0x00000000L );
	}
	// store real vertices after face normals
	for ( i = 0; i < numvertices; i++, vfillp++ ) {

	    vfillp->X = DOUBLE_TO_OD2FLOAT(  vtxlist[ i ].getX() * VERTEX_SCALE_FAC );
	    vfillp->Y = DOUBLE_TO_OD2FLOAT( -vtxlist[ i ].getY() * VERTEX_SCALE_FAC );
	    vfillp->Z = DOUBLE_TO_OD2FLOAT( -vtxlist[ i ].getZ() * VERTEX_SCALE_FAC );

		OD2_Geomv_out( &vfillp->X );
		OD2_Geomv_out( &vfillp->Y );
		OD2_Geomv_out( &vfillp->Z );

		vfillp->Flags = SWAP_32( 0x00000000L );
	}

	// fill polygon array and vertex index arrays (using OD2_Poly32) ----------
	int countofs = 0;
	// scan entire polygon list
	Polygon *polyscan = polylist.FetchHead();
	for ( i = 0; i < polylist.getNumElements(); i++, polyscan = polyscan->getNext(), pfillp++ ) {
		pfillp->NumVerts  = polyscan->getNumVertices();
		pfillp->FaceIndx  = SWAP_32( polyscan->getFaceId() );
		// Store block-relative byte offset to this polygon's vertex index array.
		// Not yet SWAP_32'd — OD2_CreateFileObject will do that.
		pfillp->pVertIndxs = vertIndxOffset + (dword)countofs;
		// fill in array of vertex indexes
		polyscan->FillVertexIndexArray( (dword *)( vertbase + countofs ) );

		countofs += sizeof( dword ) * pfillp->NumVerts;

		pfillp->NumVerts = SWAP_32( pfillp->NumVerts );
	}

	// correct vertex indexes to take face normals into account
	dword *dfillp = (dword *) vertbase;
	for ( i = 0; i < numvertexindices; i++ ) {
		*dfillp += numnormals;
		*dfillp = SWAP_32( *dfillp );
		dfillp++;
	}

	//NOTE:
	// the object contains a pointer to the polygon list.
	// this list contains all the polygon structures (no vertex indexes!)
	// the vertex index lists for all the polygons of the object follow
	// contiguously after all the polygon structures.

	// fill face list (defines surface properties) using OD2_Face32 ------------
	for ( i = 0; i < numfaces; i++, ffillp++ ) {
		// pTexMap: 0 = no texture.  If textured, we store a 1-based index into
		// the TextureChunk as a placeholder; OD2_CreateFileObject replaces it
		// with the SWAP_32'd block-relative offset to the texture name string.
		// A raw 64-bit pointer cannot be stored in a 4-byte dword field.
		ffillp->pTexMap		= 0;
		ffillp->ColorRGB	= 0;
		ffillp->ColorIndx	= 0;
		ffillp->FaceNormalIndx	= SWAP_32( i );
		ffillp->Shading 	= SWAP_32( facelist[ i ].getShadingType() & Face::base_mask );

		// write color if any attached and valid
		if ( facelist[ i ].getShadingType() & Face::color_mask ) {
			int coltype = facelist[ i ].getColorType();
			if ( coltype == Face::indexed_col ) {
				dword colindx;
				facelist[ i ].getColorIndex( colindx );
				ffillp->ColorIndx = SWAP_32( ( ( ( ( ( colindx << 8 ) + colindx ) << 8 ) + colindx ) << 8 ) + colindx);
			} else if ( coltype == Face::rgb_col ) {
				ColorRGBA coltuple;
				facelist[ i ].getColorRGBA( coltuple );
				dword colrgb = ( ( ( ( ( coltuple.A << 8 ) + coltuple.B ) << 8 ) + coltuple.G ) << 8 ) + coltuple.R;
				ffillp->ColorRGB = SWAP_32( colrgb );
			}
		}

		// attach texture: find 0-based index in TextureChunk, store (index+1)
		if ( facelist[ i ].getShadingType() & Face::texmap_mask ) {
			const char *texname = facelist[ i ].getTextureName();
			for ( int k = 0; k < texlist.getNumElements(); k++ ) {
				if ( strcmp( texlist[ k ].getName(), texname ) == 0 ) {
					ffillp->pTexMap = (dword)( k + 1 );	// 1-based placeholder
					break;
				}
			}
			OD2_CalcAffineMapping( facelist[ i ], (dword *) ffillp->TexXmatrx );
		}
	}

	memblocksize = objectmemsize;
	return (byte *) binobj;
}


// create object that can be saved to file as single block (OD2 format) -------
//
byte *ObjectBinFormat::OD2_CreateFileObject( int& memblocksize, byte *engineobj )
{
	// engineobj was built by OD2_CreateEngineObject using OD2_Root32 layout
	OD2_Root32 *binobj = (OD2_Root32 *) engineobj;
	TextureChunk& texlist = getTextureList();

	// create table of texture names
	char **texnameaddxs;
	char *texturenames, *nexttexname;
	int numtextures = texlist.getNumElements();
	int texnamesize = 0;
	int i = 0;
	if ( numtextures > 0 ) {
		for (  i = 0; i < numtextures; i++ )
			texnamesize += strlen( texlist[ i ].getName() ) + 1;
		texturenames = new char[ texnamesize ];
		nexttexname  = texturenames;
		texnameaddxs = new char*[ numtextures ];
		for ( i = 0; i < numtextures; i++ ) {
			strcpy( nexttexname, texlist[ i ].getName() );
			texnameaddxs[ i ] = nexttexname;
			nexttexname += strlen( nexttexname ) + 1;
		}
	}

	// store number of textures
	binobj->NumTextures = SWAP_32( numtextures );

	// Resolve texture placeholders in the face array.
	// pTexMap contains a 1-based TextureChunk index set by OD2_CreateEngineObject.
	// Replace each with the SWAP_32'd block-relative offset to the texture name.
	// binobj->NumFaces and pFaceList are still in native (non-swapped) form here.
	OD2_Face32 *facescan = (OD2_Face32 *)( (char *)binobj + binobj->pFaceList );
	dword j = 0;
	for ( j = 0; j < (dword)binobj->NumFaces; j++, facescan++ ) {
		if ( facescan->pTexMap != 0 ) {
			int k = (int)facescan->pTexMap - 1;		// back to 0-based index
			if ( k >= 0 && k < numtextures ) {
				dword strOffset = (dword)( (ptrdiff_t)texnameaddxs[ k ] -
				                           (ptrdiff_t)texturenames ) +
				                  (dword)memblocksize;
				facescan->pTexMap = SWAP_32( strOffset );
			} else {
				facescan->pTexMap = 0;	// safety: clear invalid index
			}
		}
	}

	binobj->NumFaces = SWAP_32( binobj->NumFaces );

	// SWAP_32 the block-relative vertex index offsets stored in each OD2_Poly32.
	// binobj->NumPolys and pPolyList are still native here.
	OD2_Poly32 *polylist = (OD2_Poly32 *)( (char *)binobj + binobj->pPolyList );
	for ( j = 0; j < (dword)binobj->NumPolys; j++, polylist++ ) {
		polylist->pVertIndxs = SWAP_32( polylist->pVertIndxs );
	}

	binobj->NumPolys = SWAP_32( binobj->NumPolys );

	// SWAP_32 the block-relative pointer fields in the header
	// (pNodeList and pChildren are 0/NULL — SWAP_32(0)==0, no-op)
	binobj->pVertexList = SWAP_32( binobj->pVertexList );
	binobj->pPolyList   = SWAP_32( binobj->pPolyList );
	binobj->pFaceList   = SWAP_32( binobj->pFaceList );

	// create block
	byte *block = new byte[ memblocksize + texnamesize ];
	memcpy( block, binobj, memblocksize );
	if ( texnamesize > 0 ) {
		memcpy( block + memblocksize, texturenames, texnamesize );
		memblocksize += texnamesize;
		// free texture name table
		delete texturenames;
		delete texnameaddxs;
	}

	return block;
}


// write entire object as binary file -----------------------------------------
//
int ObjectBinFormat::WriteDataToFile( const char *filename, int format )
{
	// to be filled
	int		memblocksize;
	byte*	engineobj = NULL;
	byte*	fileobj   = NULL;

	if ( format == BINFORMAT_ODT ) {

		sprintf( line, "Writing object data to ODT file: \"%s\"...\n", filename );
		InfoMessage( line );

		// create object as binary block
		engineobj = ODT_CreateEngineObject( memblocksize );

		// convert object to destination file format
		fileobj = ODT_CreateFileObject( memblocksize, engineobj );

	} else if ( format == BINFORMAT_OD2 ) {

		sprintf( line, "Writing object data to OD2 file: \"%s\"...\n", filename );
		InfoMessage( line );

		// create object as binary block
		engineobj = OD2_CreateEngineObject( memblocksize );

		// convert object to destination file format
		fileobj = OD2_CreateFileObject( memblocksize, engineobj );

	} else {
		return FALSE;
	}

	// write binary object representation to file
	int wstat = 0;
	{
	FileAccess ofile( filename, "wb" );
	ofile.Write( fileobj, 1, memblocksize );
	wstat = ofile.Status();
	}

	// free binary object memory blocks
	delete fileobj;
	delete engineobj;

	return ( wstat == SYSTEM_IO_OK );
}


// string scratchpad ----------------------------------------------------------
//
char ObjectBinFormat::line[ 128 ] = "";


BSPLIB_NAMESPACE_END

//-----------------------------------------------------------------------------
