//-----------------------------------------------------------------------------
//	BSPLIB MODULE: InputData3D.cpp
//
//  Copyright (c) 1997-1998 by Markus Hadwiger
//  All Rights Reserved.
//-----------------------------------------------------------------------------

// bsplib header files
#include "InputData3D.h"
#include "AodInput.h"
#include "BspInput.h"
#include "VrmlFile.h"
#include "ObjFile.h"
//ADD_FORMAT:


BSPLIB_NAMESPACE_BEGIN


// "virtual" constructor for different input format objects -------------------
//
InputData3D::InputData3D( BspObjectList objectlist, const char *filename, int format ) :
	IOData3D( objectlist, String( filename ) ),
	m_data( NULL )
{
	// create "real" input object according to format of input file
	if ( format != DONT_CREATE_OBJECT ) {
		switch ( m_objectformat = ReadFileSignature() ) {

		case AOD_FORMAT_1_1:
			m_data = new AodInput( objectlist, filename );
			break;

		case BSP_FORMAT_1_1:
			m_data = new BspInput( objectlist, filename );
			break;

		case VRML_FORMAT_1_0:
			m_data = new VrmlFile( objectlist, filename );
			break;

		case _3DX_FORMAT_1_0:
			//m_data = new ThreeDXInput( objectlist, filename );
			//TODO: add 3dx support
			break;

		case OBJ_FORMAT_1_0:
			// Use m_filename rather than the raw filename parameter: ReadFileSignature
			// may have appended ".obj" to a bare name and stored the result in m_filename.
			m_data = new ObjFile( objectlist, (char*)m_filename );
			break;

		//ADD_FORMAT:

		default:
			ErrorMessage( "[InputData3D]: Unrecognized input file format." );
			m_objectformat = UNKNOWN_FORMAT;
		}
	} else {
		m_objectformat = format;
	}

	m_inputok = m_data ? m_data->InputDataValid() : FALSE;
}


// virtual destructor ---------------------------------------------------------
//
InputData3D::~InputData3D()
{
	// delete "real" object
	delete m_data;
}


// redirection to ParseObjectData() of "real" object --------------------------
//
int InputData3D::ParseObjectData()
{
	return m_data ? m_data->ParseObjectData() : FALSE;
}


// determine file type according to signature in first line -------------------
//
int InputData3D::ReadFileSignature()
{
	// OBJ detection: check extension before opening the file (OBJ has no magic).
	// If the filename has no recognised extension but fname+".obj" exists on
	// disk, update m_filename so that subsequent open calls use the .obj path.
	{
		const char *fname = (char*)m_filename;
		size_t len = strlen( fname );
		bool hasObjExt = ( len > 4 ) && ( stricmp( fname + len - 4, ".obj" ) == 0 );
		if ( hasObjExt ) {
			// Already has .obj extension — detected below after sig read.
		} else {
			// No .obj extension: probe whether fname+".obj" exists.
			// Also probe whether the bare fname exists; if not, try .obj first
			// so FileAccess doesn't abort on a missing file.
			FILE *bare = fopen( fname, "r" );
			if ( bare ) {
				fclose( bare );
				// Bare file exists — let signature matching decide format below.
			} else {
				// Bare file not found; try appending .obj
				char tryname[ 4096 ];
				snprintf( tryname, sizeof( tryname ) - 1, "%s.obj", fname );
				tryname[ sizeof( tryname ) - 1 ] = '\0';
				FILE *probe = fopen( tryname, "r" );
				if ( probe ) {
					fclose( probe );
					m_filename = String( tryname );
					return OBJ_FORMAT_1_0;
				}
				// Neither exists — fall through and let FileAccess report the error.
			}
		}
	}

	FileAccess sig( m_filename, "r" );
	sig.ReadLine( line, TEXTLINE_MAX, CHECK_ERRORS );

	int filetype = UNKNOWN_FORMAT;

	if ( strncmp( line, AOD_SIGNATURE_1_1, strlen( AOD_SIGNATURE_1_1 ) ) == 0 )
		filetype = AOD_FORMAT_1_1;
	else if ( strncmp( line, BSP_SIGNATURE_1_1, strlen( BSP_SIGNATURE_1_1 ) ) == 0 )
		filetype = BSP_FORMAT_1_1;
	else if ( strncmp( line, VRML_SIGNATURE_1_0, strlen( VRML_SIGNATURE_1_0 ) ) == 0 )
		filetype = VRML_FORMAT_1_0;
	else if ( strncmp( line, _3DX_SIGNATURE_1_0, strlen( _3DX_SIGNATURE_1_0 ) ) == 0 )
		filetype = _3DX_FORMAT_1_0;
	//ADD_FORMAT:

	// OBJ has no fixed magic — detect by .obj / .OBJ file extension
	if ( filetype == UNKNOWN_FORMAT ) {
		const char *fname = (char*)m_filename;
		size_t len = strlen( fname );
		if ( len > 4 && stricmp( fname + len - 4, ".obj" ) == 0 )
			filetype = OBJ_FORMAT_1_0;
	}

	return filetype;
}


// set flag controlling color index to RGB conversion -------------------------
//
int InputData3D::EnableRGBConversion( int enable )
{
	int precstate = getRGBConversionFlag();
	if ( enable )
		PostProcessingFlags |= CONVERT_COLINDXS_TO_RGB;
	else
		PostProcessingFlags &= ~CONVERT_COLINDXS_TO_RGB;
	return precstate;
}


// set flag controlling application of object scale factors -------------------
//
int InputData3D::EnableScaleFactors( int enable )
{
	int precstate = getScaleFactorsFlag();
	if ( enable )
		PostProcessingFlags |= FILTER_SCALE_FACTORS;
	else
		PostProcessingFlags &= ~FILTER_SCALE_FACTORS;
	return precstate;
}


// set flag controlling change of axes per command ----------------------------
//
int InputData3D::EnableAxesChange( int enable )
{
	int precstate = getAxesChangeFlag();
	if ( enable )
		PostProcessingFlags |= DO_AXES_EXCHANGE;
	else
		PostProcessingFlags &= ~DO_AXES_EXCHANGE;
	return precstate;
}


// set flag controlling enforcement of maximum coordinate extent --------------
//
int InputData3D::EnableMaximumExtent( int enable, double extent )
{
	int precstate		 = getEnforceExtentsFlag();
	MaximumExtentToForce = extent;
	if ( enable )
		PostProcessingFlags |= FORCE_MAXIMUM_EXTENT;
	else
		PostProcessingFlags &= ~FORCE_MAXIMUM_EXTENT;
	return precstate;
}


// set flag controlling n-gon enabling/disabling -------------------------------
//
int InputData3D::EnableAllowNGons( int enable )
{
	int precstate = getAllowNGonFlag();
	if ( enable )
		PostProcessingFlags |= ALLOW_N_GONS;
	else
		PostProcessingFlags &= ~ALLOW_N_GONS;
	return precstate;
}


// InputData3D specific static variables --------------------------------------
//
dword		InputData3D::PostProcessingFlags		= InputData3D::FILTER_SCALE_FACTORS | InputData3D::DO_AXES_EXCHANGE;
double		InputData3D::MaximumExtentToForce		= 100.0;
const char	InputData3D::parser_err_str[]			= "\nObject parser: **ERROR** ";


BSPLIB_NAMESPACE_END

//-----------------------------------------------------------------------------
