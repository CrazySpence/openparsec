//-----------------------------------------------------------------------------
//	BSPLIB MODULE: ObjFile.cpp
//
//  Wavefront OBJ format input for BspLib / makeodt.
//
//  COORDINATE CONVENTION
//  =====================
//  OBJ (Blender default): X right, Y up, Z toward viewer (right-handed Y-up).
//  BspLib internal:        X right, Y up, Z toward viewer (same as VRML 1.0).
//  ObjectBinFormat output: negates Y and Z when writing, and multiplies all
//                          vertex coordinates by VERTEX_SCALE_FAC (1/20).
//
//  Therefore: store OBJ coordinates directly into BspLib with NO scale change
//  and NO axis flip. When exporting from Blender use the default OBJ export
//  settings (Forward: -Z, Up: Y) which matches BspLib's coordinate frame.
//
//  UV CONVENTION
//  =============
//  OBJ UV origin is bottom-left; the engine expects top-left.
//  V coordinates are flipped: engine_v = 1.0 - obj_v.
//
//  TEXTURE MAPPING
//  ===============
//  The TriMapping attached to each Face stores 3-vertex correspondences used
//  by ObjectBinFormat::OD2_CalcAffineMapping() to build the affine texture
//  matrix. MapXY holds object-space XYZ (X, Y in the Vertex2 X/Y fields; Z in W),
//  MapUV holds [0,1]-range UV coordinates.
//  For n-gons with n > 3 only the first three vertices are used for the
//  affine mapping (same limitation as the original VRML/AOD pipeline).
//
//  MTL SUPPORT
//  ===========
//  Reads the paired .mtl file. Recognises:
//    newmtl <name>       - starts a new material definition
//    map_Kd <file>       - diffuse texture (enables persptex_shad)
//    Kd <r> <g> <b>      - diffuse colour used when no texture is present
//  All other MTL directives are silently ignored.
//-----------------------------------------------------------------------------

// bsplib header files
#include "ObjFile.h"
#include "BspObject.h"
#include "BspObjectList.h"
#include "Face.h"
#include "Mapping.h"
#include "Texture.h"
#include "Vertex.h"

// C standard library
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cstdlib>
#include <cstdio>


BSPLIB_NAMESPACE_BEGIN


//-----------------------------------------------------------------------------
// Internal data structures used during parsing
//-----------------------------------------------------------------------------

struct ObjPos  { double x, y, z; };	// raw OBJ vertex position
struct ObjUV   { double u, v; };	// raw OBJ texture coordinate
struct ObjVRef { int vIdx; int vtIdx; };// 0-based indices (-1 = absent)

struct ObjFaceRaw {
	std::vector<ObjVRef> verts;
	std::string          material;
};

struct MtlMat {
	std::string texFile;	// from map_Kd (empty = no texture)
	float r, g, b;		// from Kd
	MtlMat() : r( 0.8f ), g( 0.8f ), b( 0.8f ) { }
};


//-----------------------------------------------------------------------------
// Helper: return the directory portion of a file path (with trailing separator)
//-----------------------------------------------------------------------------
static std::string DirOfPath( const char *path )
{
	const char *last = path;
	for ( const char *p = path; *p; p++ )
		if ( *p == '/' || *p == '\\' )
			last = p + 1;
	return std::string( path, last - path );
}


//-----------------------------------------------------------------------------
// Helper: load a .mtl file and populate the material map
//-----------------------------------------------------------------------------
static void ParseMtlFile( const char *path,
                           std::map<std::string, MtlMat>& mats )
{
	FILE *fp = fopen( path, "r" );
	if ( !fp ) return;

	char line[ 1024 ];
	MtlMat *cur = NULL;
	while ( fgets( line, sizeof( line ), fp ) ) {
		char *tok = strtok( line, " \t\r\n" );
		if ( !tok || tok[ 0 ] == '#' ) continue;

		if ( strcmp( tok, "newmtl" ) == 0 ) {
			tok = strtok( NULL, " \t\r\n" );
			if ( tok ) {
				std::string name( tok );
				mats[ name ] = MtlMat();
				cur = &mats[ name ];
			}
		} else if ( cur && strcmp( tok, "map_Kd" ) == 0 ) {
			tok = strtok( NULL, " \t\r\n" );
			if ( tok ) {
				// Strip directory component — store only the basename so the
				// engine can match it against the internal name used in
				// "load texture <name> file <basename>".  map_Kd paths from
				// Blender (and most exporters) are often absolute paths on the
				// artist's machine which are useless at runtime.
				const char *slash = strrchr( tok, '/' );
				const char *bslash = strrchr( tok, '\\' );
				const char *base = tok;
				if ( slash  && slash  > base ) base = slash  + 1;
				if ( bslash && bslash > base ) base = bslash + 1;
				cur->texFile = base;
			}
		} else if ( cur && strcmp( tok, "Kd" ) == 0 ) {
			double r = 0.8, g = 0.8, b = 0.8;
			tok = strtok( NULL, " \t\r\n" ); if ( tok ) r = atof( tok );
			tok = strtok( NULL, " \t\r\n" ); if ( tok ) g = atof( tok );
			tok = strtok( NULL, " \t\r\n" ); if ( tok ) b = atof( tok );
			cur->r = (float)r;
			cur->g = (float)g;
			cur->b = (float)b;
		}
	}
	fclose( fp );
}


//-----------------------------------------------------------------------------
// Helper: parse one OBJ face vertex token  "v", "v/vt", "v/vt/vn", "v//vn"
// Returns 0-based vIdx and vtIdx (-1 if absent).
// OBJ uses 1-based indices; negative indices are relative to the end.
//-----------------------------------------------------------------------------
static void ParseFaceToken( const char *tok,
                            int numPositions, int numUVs,
                            int& vIdx, int& vtIdx )
{
	vIdx = vtIdx = -1;
	const char *p = tok;

	int v = atoi( p );
	if ( v == 0 ) return;
	vIdx = ( v > 0 ) ? v - 1 : numPositions + v;

	while ( *p && *p != '/' ) p++;
	if ( *p != '/' ) return;	// no UV part
	p++;
	if ( *p == '/' || *p == '\0' ) return;	// v//vn or v/ — no UV

	int vt = atoi( p );
	if ( vt != 0 )
		vtIdx = ( vt > 0 ) ? vt - 1 : numUVs + vt;
}


//-----------------------------------------------------------------------------
// ObjFile constructor — parse immediately on construction (mirrors AodInput)
//-----------------------------------------------------------------------------
ObjFile::ObjFile( BspObjectList objectlist, const char *filename ) :
	InputData3D( objectlist, filename, DONT_CREATE_OBJECT )
{
	ParseObjectData();
}


//-----------------------------------------------------------------------------
// ObjFile destructor
//-----------------------------------------------------------------------------
ObjFile::~ObjFile()
{
}


//-----------------------------------------------------------------------------
// ParseObjectData — two-pass OBJ import
//-----------------------------------------------------------------------------
int ObjFile::ParseObjectData()
{
	InfoMessage( "Processing input data file (format='OBJ V1.0') ..." );

	// -----------------------------------------------------------------------
	// Pass 1: collect raw OBJ data into STL containers
	// -----------------------------------------------------------------------

	std::vector<ObjPos>     positions;
	std::vector<ObjUV>      uvs;
	std::vector<ObjFaceRaw> faces;
	std::string             mtlFileName;
	std::string             curMaterial;

	FILE *fp = fopen( (char*)m_filename, "r" );
	if ( !fp ) {
		ErrorMessage( "[ObjFile]: Cannot open input file." );
		HandleCriticalError();
		return FALSE;
	}

	char buf[ 1024 ];
	while ( fgets( buf, sizeof( buf ), fp ) ) {
		char *tok = strtok( buf, " \t\r\n" );
		if ( !tok || tok[ 0 ] == '#' ) continue;

		if ( strcmp( tok, "mtllib" ) == 0 ) {
			tok = strtok( NULL, " \t\r\n" );
			if ( tok ) mtlFileName = tok;

		} else if ( strcmp( tok, "usemtl" ) == 0 ) {
			tok = strtok( NULL, " \t\r\n" );
			if ( tok ) curMaterial = tok;

		} else if ( strcmp( tok, "v" ) == 0 ) {
			ObjPos pos = { 0, 0, 0 };
			tok = strtok( NULL, " \t\r\n" ); if ( tok ) pos.x = atof( tok );
			tok = strtok( NULL, " \t\r\n" ); if ( tok ) pos.y = atof( tok );
			tok = strtok( NULL, " \t\r\n" ); if ( tok ) pos.z = atof( tok );
			positions.push_back( pos );

		} else if ( strcmp( tok, "vt" ) == 0 ) {
			ObjUV uv = { 0, 0 };
			tok = strtok( NULL, " \t\r\n" ); if ( tok ) uv.u = atof( tok );
			tok = strtok( NULL, " \t\r\n" ); if ( tok ) uv.v = atof( tok );
			uvs.push_back( uv );

		} else if ( strcmp( tok, "f" ) == 0 ) {
			ObjFaceRaw face;
			face.material = curMaterial;
			while ( ( tok = strtok( NULL, " \t\r\n" ) ) != NULL ) {
				int vIdx, vtIdx;
				ParseFaceToken( tok,
				                (int)positions.size(), (int)uvs.size(),
				                vIdx, vtIdx );
				if ( vIdx >= 0 ) {
					ObjVRef vr;
					vr.vIdx  = vIdx;
					vr.vtIdx = vtIdx;
					face.verts.push_back( vr );
				}
			}
			if ( (int)face.verts.size() >= 3 )
				faces.push_back( face );
		}
	}
	fclose( fp );

	// -----------------------------------------------------------------------
	// Load MTL file (optional — skip silently if absent)
	// -----------------------------------------------------------------------

	std::map<std::string, MtlMat> mats;
	if ( !mtlFileName.empty() ) {
		std::string dir     = DirOfPath( (char*)m_filename );
		std::string mtlPath = dir + mtlFileName;
		ParseMtlFile( mtlPath.c_str(), mats );
	}

	// -----------------------------------------------------------------------
	// Pass 2: populate BspObject
	// -----------------------------------------------------------------------

	BspObject *obj = m_objectlist.CreateNewObject();

	// -- Vertices -----------------------------------------------------------
	// Store OBJ coordinates directly; ObjectBinFormat applies VERTEX_SCALE_FAC
	// (1/20) and negates Y and Z when writing the OD2 binary.  Export from
	// Blender with Forward: -Z, Up: Y to match BspLib's Y-up coordinate frame.

	for ( const ObjPos& p : positions ) {
		obj->getVertexList().AddVertex( Vertex3( p.x, p.y, p.z ) );
	}

	// -- TextureChunk -------------------------------------------------------
	// OD2_CreateFileObject matches Face::getTextureName() against
	// Texture::getName() entries in the TextureChunk to build the texture
	// name table appended to the file.  Populate one Texture per unique
	// texture filename so the TexMap pointers are correctly patched.

	{
		std::map<std::string, bool> seenTex;
		for ( const ObjFaceRaw& f : faces ) {
			auto it = mats.find( f.material );
			if ( it != mats.end() && !it->second.texFile.empty() ) {
				const std::string& tname = it->second.texFile;
				if ( seenTex.find( tname ) == seenTex.end() ) {
					seenTex[ tname ] = true;
					Texture tex;
					tex.setName( tname.c_str() );
					tex.setFile( tname.c_str() );
					obj->getTextureList().AddElement( tex );
				}
			}
		}
	}

	// -- Faces and Polygons -------------------------------------------------
	// One BspLib Face + one BspLib Polygon per OBJ face directive.
	// Auto-assignment in PolygonList::NewPolygon() sets faceId = polygon index,
	// which matches the FaceChunk index since we add one face per polygon.

	for ( const ObjFaceRaw& f : faces ) {
		int faceId = obj->getFaceList().getNumElements();

		// ---- Build Face ---------------------------------------------------
		Face tempface;
		tempface.setId( faceId );

		auto it = mats.find( f.material );
		bool hasTexture = ( it != mats.end() && !it->second.texFile.empty() );
		bool hasColor   = ( it != mats.end() );

		if ( hasTexture ) {
			tempface.setShadingType( Face::persptex_shad );
			tempface.setTextureName( it->second.texFile.c_str() );
		} else if ( hasColor ) {
			tempface.setShadingType( Face::flat_shad );
			ColorRGBA col;
			col.R = (byte)( it->second.r * 255.0f );
			col.G = (byte)( it->second.g * 255.0f );
			col.B = (byte)( it->second.b * 255.0f );
			col.A = 255;
			tempface.setFaceColor( col );
		} else {
			// No material — default light grey
			tempface.setShadingType( Face::flat_shad );
			ColorRGBA col = { 200, 200, 200, 255 };
			tempface.setFaceColor( col );
		}

		// ---- UV mapping (TriMapping on Face) ------------------------------
		// ObjectBinFormat uses face.MapXY / face.MapUV for affine texture
		// matrix computation.  Only the first three vertices are used
		// (TriMapping holds exactly 3 correspondences).
		// MapXY: object-space X/Y/Z (Z in Vertex2::W, same as InitFromVertex3).
		// MapUV: [0,1] UV range; V is flipped to convert OBJ bottom-left
		//        origin to engine top-left origin.

		if ( hasTexture ) {
			// Check that all first-3 vertices have UV assignments
			bool allHaveUV = true;
			for ( int k = 0; k < 3 && k < (int)f.verts.size(); k++ )
				if ( f.verts[ k ].vtIdx < 0 ) { allHaveUV = false; break; }

			if ( allHaveUV ) {
				// Find 3 vertices whose UV coordinates form a non-degenerate triangle.
				// The affine mapping matrix requires the UV triangle to be non-collinear.
				// For triangles there is only one possible triplet; for quads/n-gons we
				// try all C(n,3) combinations and pick the first with nonzero UV area.
				// Threshold: |cross| > 1e-8 in [0,1]^2 UV space.
				int n = (int)f.verts.size();
				int pick[3] = { 0, 1, 2 };  // default: first 3
				bool foundNonDeg = false;

				for ( int a = 0; a < n - 2 && !foundNonDeg; a++ ) {
					if ( f.verts[ a ].vtIdx < 0 ) continue;
					for ( int b = a + 1; b < n - 1 && !foundNonDeg; b++ ) {
						if ( f.verts[ b ].vtIdx < 0 ) continue;
						for ( int c = b + 1; c < n && !foundNonDeg; c++ ) {
							if ( f.verts[ c ].vtIdx < 0 ) continue;
							const ObjUV& t0 = uvs[ f.verts[ a ].vtIdx ];
							const ObjUV& t1 = uvs[ f.verts[ b ].vtIdx ];
							const ObjUV& t2 = uvs[ f.verts[ c ].vtIdx ];
							double cross = ( t1.u - t0.u ) * ( t2.v - t0.v )
							             - ( t1.v - t0.v ) * ( t2.u - t0.u );
							if ( cross * cross > 1e-16 ) {
								pick[0] = a; pick[1] = b; pick[2] = c;
								foundNonDeg = true;
							}
						}
					}
				}

				if ( foundNonDeg ) {
					for ( int k = 0; k < 3; k++ ) {
						const ObjVRef& vr = f.verts[ pick[ k ] ];
						const ObjPos&  p  = positions[ vr.vIdx ];
						const ObjUV&   t  = uvs[ vr.vtIdx ];

						// MapXY: X, Y, Z of the vertex in object space (Z stored in W slot,
						// matching Vertex2::InitFromVertex3 used by all other BspLib paths).
						// Without the Z component the XYW matrix is rank-deficient for any
						// face lying in an axis-aligned plane (e.g. top/bottom of a cube),
						// causing the texture-gradient adjoint to be zero and the face to
						// render as solid black or invisible.
						tempface.MapXY( k ) = Vertex2( p.x, p.y, p.z );

						// MapUV: OBJ UV uses V=0 at bottom-left, same convention as the
						// engine (matching VRML ships in BRep.cpp which also pass UV
						// straight through without any flip).  No V-inversion needed.
						tempface.MapUV( k ) = Vertex2( t.u, t.v );
					}
				} else {
					// All UV triplets are degenerate — face is genuinely unmappable.
					// Fall back to flat shading with the material colour.
					tempface.setShadingType( Face::flat_shad );
					ColorRGBA col = { 200, 200, 200, 255 };
					if ( hasColor ) {
						col.R = (byte)( it->second.r * 255.0f );
						col.G = (byte)( it->second.g * 255.0f );
						col.B = (byte)( it->second.b * 255.0f );
					}
					tempface.setFaceColor( col );
				}
			} else {
				// Missing UV indices on some vertices — fall back to flat shading
				tempface.setShadingType( Face::flat_shad );
				ColorRGBA col = { 200, 200, 200, 255 };
				if ( hasColor ) {
					col.R = (byte)( it->second.r * 255.0f );
					col.G = (byte)( it->second.g * 255.0f );
					col.B = (byte)( it->second.b * 255.0f );
				}
				tempface.setFaceColor( col );
			}
		}

		obj->getFaceList().AddElement( tempface );

		// ---- Build Polygon ------------------------------------------------
		// OBJ uses counter-clockwise (CCW) winding.  BspLib's Plane constructor
		// uses clockwise (CW) convention (Plane.h: "front face in clockwise
		// order") so its cross product (v3-v1)×(v2-v1) gives an OUTWARD normal
		// only when vertices are in CW order.  Inserting each vertex at the HEAD
		// (PrependNewVIndx) reverses the OBJ CCW order to CW, matching the same
		// pattern BRep::BuildFromIndexedFaceSet uses for VRML CCW faces.
		// Without this reversal the stored engine face normals point inward,
		// causing nearly all faces to appear front-facing regardless of the view
		// angle and producing the "pentagon" silhouette effect.
		Polygon *poly = obj->getPolygonList().NewPolygon();
		poly->setFaceId( faceId );

		for ( const ObjVRef& vr : f.verts )
			obj->getPolygonList().PrependNewVIndx( vr.vIdx );
	}

	obj->CheckParsedData();
	InfoMessage( "\nObject data ok.\n" );

	return ( m_inputok = TRUE );
}


BSPLIB_NAMESPACE_END

//-----------------------------------------------------------------------------
