/*
 * PARSEC - SDL OpenGL  Video Wrapper
 *
 * $Author: uberlinuxguy $ - $Date: 2004/09/15 12:25:37 $
 *
 * Orginally written by:
 *   Copyright (c) Andreas Varga       <sid@parsec.org>   1999
 *   Copyright (c) Markus Hadwiger     <msh@parsec.org>   1999
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */ 
#include "config.h"

// C library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// compilation flags/debug support
#include "config.h"
#include "debug.h"

// general definitions
#include "general.h"
#include "objstruc.h"

// global externals
#include "globals.h"

// subsystem headers
#include "vid_defs.h"

// rendering subsystem
#include "r_patch.h"

// opengl headers
#include "r_gl.h"
#include "ro_api.h"

// local module header
#include "vsdl_ogl.h"

// proprietary module headers
#include "con_aux.h"
#include "con_int.h"
#include "e_supp.h"



// flags
//#define DEBUG_640x480_WINDOWED
#define EXPLICIT_GL_ORTHO
//#define LIST_ONLY_SAFE_MODES
//#define REVERSED_DEPTH_RANGE


//NOTE:
// if REVERSED_DEPTH_RANGE is set, glDepthRange() will be used to reverse
// the depth range. otherwise, it will be reversed in the projection matrices.
// this is a work-around for the DRI drivers, since they have problems with a
// reversed depth range. for them, REVERSED_DEPTH_RANGE should not be set.
// currently, this is only an issue under Linux. for consistency and testing,
// this flag has also been added here.


// globally usable orthogonal projection-matrix (with glLoadMatrixf()) --------
//
GLfloat gl_orthogonal_matrix[] = {

	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f
};

GLfloat gl_orthogonal_params[ 6 ];

GLint 					sdl_wsz_x;
GLint 					sdl_wsz_y;
GLsizei 				sdl_wsz_w;
GLsizei 				sdl_wsz_h;

SDL_Window *			curwindow = NULL;
SDL_GLContext			curcontext = NULL;

// cached native drawable size — set once in SDL_RCSetup, used every frame
static int sdl_drawable_w = 0;
static int sdl_drawable_h = 0;


// display next opengl buffer -------------------------------------------------
//
void VSDL_CommitOGLBuff()
{
	// Tell SDL to swap the GL Buffers
	SDL_GL_SwapWindow(curwindow);

	// After the swap the new back buffer is the one that was rendered two frames
	// ago. Its letterbox bars may hold stale pixel data (previous game content or
	// uninitialised memory) which would strobe visibly on the next present.
	// Re-clear the full drawable to black now so bars are always clean when the
	// game starts rendering the next frame into the letterboxed viewport.
	if ( (Vid_ViewportX > 0 || Vid_ViewportY > 0) && sdl_drawable_w > 0 ) {
		glDisable(GL_SCISSOR_TEST);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glViewport(0, 0, sdl_drawable_w, sdl_drawable_h);
		glClear(GL_COLOR_BUFFER_BIT);
		// restore the letterboxed viewport and scissor for the upcoming frame
		glViewport(Vid_ViewportX, Vid_ViewportY, Vid_ViewportW, Vid_ViewportH);
		glScissor(Vid_ViewportX, Vid_ViewportY, Vid_ViewportW, Vid_ViewportH);
		glEnable(GL_SCISSOR_TEST);
	}

	// FIXME: Find a better location to put this...
	// Sleep for a few milliseconds every frame if the window isn't in focus.
	// Makes sure CPU usage stays sane if the window manager decides to avoid
	// vsync when the app is in the background.
	Uint32 flags = SDL_GetWindowFlags(curwindow);
	if ((flags & (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS)) == 0) {
		SDL_Delay(10);
	}
}


// init interface code to GL functions ---------------------------------------
//
int VSDL_InitOGLInterface( int printmodelistflags )
{
	MSGOUT( "Using the OpenGL subsystem as rendering device." );

	int displayCount = 0;
	if ( SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0 ) {
		MSGOUT("[VIDEO]: ERROR: Trouble initializing video subsystem.");
		return FALSE;
	}

	Resolutions.clear();

	SDL_DisplayMode curmode = {};
	SDL_GetDesktopDisplayMode(0, &curmode);

	MaxScreenBPP = SDL_BITSPERPIXEL(curmode.format);

	// grab the display count, and check it prior to entering the loop
	// so that we can output an SDL Error if need be.
	displayCount = SDL_GetNumDisplayModes(0);
	if(displayCount < 1) {
		MSGOUT("[VIDEO]: Error getting number of displays: %s\n", SDL_GetError());
		ASSERT(displayCount > 0);
	}

	// Curated allowlist of standard aspect-ratio resolutions (no wider than 16:9).
	// The 1999/2012 projection math (D_Value = Screen_Width * 0.78) breaks at
	// wider-than-16:9 aspect ratios, causing visible nebula and object-scale
	// artifacts on ultrawide displays.
	static const resinfo_s allowed_resolutions[] = {
		resinfo_s( 640,  480),   // 4:3
		resinfo_s( 800,  600),   // 4:3
		resinfo_s(1024,  768),   // 4:3
		resinfo_s(1280,  720),   // 16:9  (Steam Deck display res)
		resinfo_s(1280,  800),   // 16:10 (Steam Deck native panel)
		resinfo_s(1366,  768),   // 16:9  (common laptop)
		resinfo_s(1920, 1080),   // 16:9
		resinfo_s(2560, 1440),   // 16:9  (1440p)
	};
	static const int num_allowed = (int)(sizeof(allowed_resolutions) / sizeof(allowed_resolutions[0]));

	// Collect what SDL actually reports so we can validate each allowlist entry.
	std::vector<resinfo_s> sdl_modes;
	for (int i = 0; i < displayCount; i++) {
		if (SDL_GetDisplayMode(0, i, &curmode) < 0) {
			MSGOUT("[VIDEO]: Could not get info for SDL display mode #%d: %s\n", i, SDL_GetError());
			continue;
		}
		int bpp = SDL_BITSPERPIXEL(curmode.format);
		if ((bpp == 32 || bpp == 16 || bpp == 24) && bpp >= MaxScreenBPP)
			sdl_modes.push_back(resinfo_s(curmode.w, curmode.h));
	}

	// Add each allowlist entry only if the display hardware supports it.
	for (int a = 0; a < num_allowed; a++) {
		bool supported = false;
		for (size_t s = 0; s < sdl_modes.size(); s++) {
			if (sdl_modes[s] == allowed_resolutions[a]) { supported = true; break; }
		}
		if (supported && GetResolutionIndex(allowed_resolutions[a].width, allowed_resolutions[a].height) < 0) {
			Resolutions.push_back(allowed_resolutions[a]);
			MSGOUT("[VIDEO]: Added resolution: %dx%d\n", allowed_resolutions[a].width, allowed_resolutions[a].height);
		}
	}

	// Fallback: if nothing matched (unusual display), accept all SDL modes ≥ 640×480
	// so the game is always launchable even on exotic hardware.
	if (Resolutions.empty()) {
		MSGOUT("[VIDEO]: No standard resolutions found; falling back to full SDL mode list.\n");
		for (size_t s = 0; s < sdl_modes.size(); s++) {
			int xres = sdl_modes[s].width, yres = sdl_modes[s].height;
			if (xres >= 640 && yres >= 480 && xres % 2 == 0 && yres % 2 == 0 &&
			    GetResolutionIndex(xres, yres) < 0) {
				Resolutions.push_back(sdl_modes[s]);
			}
		}
	}

	ASSERT(Resolutions.size() > 0);

	// sort resolution list, putting the smallest at the front
	std::sort(Resolutions.begin(), Resolutions.end());

	return TRUE;
}


// globally usable projective projection-matrix (with glLoadMatrixf()) --------
//
GLfloat gl_projective_matrix[] = {

	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 1.0f,
	0.0f, 0.0f, 0.0f, 0.0f
};


// calculate the global projective projection-matrix --------------------------
//
void VSDL_CalcProjectiveMatrix()
{
	//NOTE:
	// this matrix is equivalent to the following:
	// - glLoadIdentity();
	// - glFrustum( left, right, bottom, top, znear, zfar );
	// - glMultMatrixf( { 1,0,0,0, 0,1,0,0, 0,0,-1,0, 0,0,0,1 } );

	float crit_x = Criterion_X * Near_View_Plane;
	float crit_y = Criterion_Y * Near_View_Plane;

	float left	= -crit_x;
	float right	=  crit_x;
	float bottom	=  crit_y;
	float top		= -crit_y;
	float znear	= Near_View_Plane;
	float zfar	= Far_View_Plane;

	float r_min_l	= right - left;
	float t_min_b	= top   - bottom;
	float f_min_n	= zfar  - znear;
	float n2		= znear * 2;

	gl_projective_matrix[ 0 ]	= n2 / r_min_l;
	gl_projective_matrix[ 5 ]	= n2 / t_min_b;
	gl_projective_matrix[ 8 ]	= -( right + left ) / r_min_l;
	gl_projective_matrix[ 9 ]	= -( top + bottom ) / t_min_b;
	gl_projective_matrix[ 10 ]	=  ( zfar + znear ) / f_min_n;
	gl_projective_matrix[ 14 ]	= -( zfar * n2    ) / f_min_n;
}



// calculate the global orthographic projection-matrix ------------------------
//
void VSDL_CalcOrthographicMatrix()
{
	//NOTE:
	// this matrix is equivalent to the following:
	// - glLoadIdentity();
	// - glOrtho( wgl_wsz_x, wgl_wsz_x + wgl_wsz_w,
	//            wgl_wsz_y + wgl_wsz_h, wgl_wsz_y,
	//            -1.0, 0.0 );

	float left	= sdl_wsz_x;
	float right	= sdl_wsz_x + sdl_wsz_w;
	float bottom	= sdl_wsz_y + sdl_wsz_h;
	float top		= sdl_wsz_y;
	float znear	= 1.0f;
	float zfar	= 0.0f;

	// make the parameters accessible separately
	// to enable use of glOrtho()
	gl_orthogonal_params[ 0 ] = left;
	gl_orthogonal_params[ 1 ] = right;
	gl_orthogonal_params[ 2 ] = bottom;
	gl_orthogonal_params[ 3 ] = top;
	gl_orthogonal_params[ 4 ] = -znear;
	gl_orthogonal_params[ 5 ] = -zfar;

	float r_min_l	= right - left;
	float t_min_b	= top   - bottom;
	float f_min_n	= zfar  - znear;

	gl_orthogonal_matrix[ 0 ]	= 2.0f / r_min_l;
	gl_orthogonal_matrix[ 5 ]	= 2.0f / t_min_b;
	gl_orthogonal_matrix[ 10 ]	= 2.0f / f_min_n;
	gl_orthogonal_matrix[ 12 ]	= -( right + left ) / r_min_l;
	gl_orthogonal_matrix[ 13 ]	= -( top + bottom ) / t_min_b;
	gl_orthogonal_matrix[ 14 ]	= -( zfar + znear ) / f_min_n;
}


// configure opengl projection matrix and viewport ----------------------------
//
PRIVATE
void SDL_ConfigureProjection()
{

}


// query available extensions -------------------------------------------------
//
PRIVATE
void SDL_RCQueryExtensions()
{
	const char * extliststr = (const char *) glGetString(GL_EXTENSIONS);
	
	if (!extliststr || !extliststr[0])
		return;
	
	MSGOUT("Extensions:\n");
	
	size_t extliststrlen = strlen(extliststr);
	int extlen = 0, startindex = -1;
	
	// extensions are separated by a single space, so we need to iterate over the string to find extension names
	for (size_t i = 0; i < extliststrlen; i++) {
		// print current extension if we encounter a space or we're at the end of the string
		if (extliststr[i] == ' ' || i == extliststrlen-1) {
			if (i == extliststrlen-1)
				extlen++;

			if (extlen > 0) // extension has been found, lets print it
				MSGOUT("\t%.*s\n", extlen, extliststr + startindex);
			
			// reset counters
			extlen = 0;
			startindex = -1;
		} else {
			// keep track of the length of the current extension we're traversing
			extlen++;
			if (startindex == -1) // keep track of the start index as well
				startindex = i;
		}
	}
}


// display current rendering context info -------------------------------------
//
void SDL_RCDisplayInfo()
{
	MSGOUT( "--OpenGL driver info--\n" );

	MSGOUT( "Vendor:\n" );
	MSGOUT( " %s\n", glGetString( GL_VENDOR ) );
	MSGOUT( "Renderer:\n" );
	MSGOUT( " %s\n", glGetString( GL_RENDERER ) );
	MSGOUT( "Version:\n" );
	MSGOUT( " %s\n", glGetString( GL_VERSION ) );

	// query extensions string
	//SDL_RCQueryExtensions();
}


// initialize OpenGL extensions via GLEW --------------------------------------
//
PRIVATE
void VSDL_InitGLExtensions()
{
#ifdef GLEW_VERSION
	// don't rely purely on extension string list to determine capabilities
	glewExperimental = GL_TRUE;

	// initialize extensions from GLEW
	if (glewInit() != GLEW_OK) {
		MSGOUT("Warning: failed to initialize GLEW\n");
		return;
	}

	// some drivers support core GL occlusion querying but not the ARB extension
	// so we just use the core GL function pointers for the ARB functions since the interface is the same
	if (GLEW_VERSION_1_5 && !GLEW_ARB_occlusion_query) {
		glBeginQueryARB = (PFNGLBEGINQUERYARBPROC) glBeginQuery;
		glDeleteQueriesARB = (PFNGLDELETEQUERIESARBPROC) glDeleteQueries;
		glEndQueryARB = (PFNGLENDQUERYARBPROC) glEndQuery;
		glGenQueriesARB = (PFNGLGENQUERIESARBPROC) glGenQueries;
		glGetQueryObjectivARB = (PFNGLGETQUERYOBJECTIVARBPROC) glGetQueryObjectiv;
		glGetQueryObjectuivARB = (PFNGLGETQUERYOBJECTUIVARBPROC) glGetQueryObjectuiv;
		glGetQueryivARB = (PFNGLGETQUERYIVARBPROC) glGetQueryiv;
		glIsQueryARB = (PFNGLISQUERYARBPROC) glIsQuery;
	}

	// same deal with vertex buffers
	if (GLEW_VERSION_1_5 && !GLEW_ARB_vertex_buffer_object) {
		glBindBufferARB = glBindBuffer;
		glBufferDataARB = glBufferData;
		glBufferSubDataARB = glBufferSubData;
		glDeleteBuffersARB = glDeleteBuffers;
		glGenBuffersARB = glGenBuffers;
		glGetBufferParameterivARB = glGetBufferParameteriv;
		glGetBufferPointervARB = glGetBufferPointerv;
		glGetBufferSubDataARB = glGetBufferSubData;
		glIsBufferARB = glIsBuffer;
		glMapBufferARB = glMapBuffer;
		glUnmapBufferARB = glUnmapBuffer;
	}
#endif
}


// setup current rendering context --------------------------------------------
//
PRIVATE
void SDL_RCSetup()
{
	VSDL_InitGLExtensions();
	
	// attempt to enable MSAA if set
	if (AUX_MSAA > 0) {
		glEnable(GL_MULTISAMPLE);

		GLint buffers = 0, samples = 0;
		glGetIntegerv(GL_SAMPLE_BUFFERS, &buffers);
		glGetIntegerv(GL_SAMPLES, &samples);

		if ((AUX_MSAA && !buffers) || (AUX_MSAA != samples)) {
			// OpenGL multisampling values different than expected 
			MSGOUT("MSAA warning: expected 1 buffer and %d samples, got %d buffer(s) and %d sample(s)\n", AUX_MSAA, buffers, samples);
			if (samples > 0 && buffers > 0) {
				AUX_MSAA = samples;
			} else {
				// clean up
				glDisable(GL_MULTISAMPLE);
				AUX_MSAA = 0;
			}
		}
	}

	// calculate projection matrix, setup projection
	// calc projection matrices
	VSDL_CalcProjectiveMatrix();
	VSDL_CalcOrthographicMatrix();

	// set projection to screen coordinate identity
	glMatrixMode( GL_PROJECTION );
	/*
	glLoadIdentity();
	glOrtho( sdl_wsz_x, sdl_wsz_x + sdl_wsz_w,
	            sdl_wsz_y + sdl_wsz_h, sdl_wsz_y,
	            -1.0, 0.0 );
	*/

#ifdef EXPLICIT_GL_ORTHO
	glLoadIdentity();

	//glOrtho( sdl_wsz_x, sdl_wsz_x + sdl_wsz_w,
    //        sdl_wsz_y + sdl_wsz_h, sdl_wsz_y,
    //        -1.0, 0.0 );
	glOrtho( gl_orthogonal_params[ 0 ], gl_orthogonal_params[ 1 ],
			 gl_orthogonal_params[ 2 ], gl_orthogonal_params[ 3 ],
			 gl_orthogonal_params[ 4 ], gl_orthogonal_params[ 5 ] );

#else
	glLoadMatrixf( gl_orthogonal_matrix );
#endif

	// select reversed depth range
	glDepthRange( 1.0, 0.0 );

	// no transformation
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();

	int window_w, window_h;
	SDL_GetWindowSize(curwindow, &window_w, &window_h);

	// set viewport using the window's pixel backing size, rather than its
	// "dpi-independent" size.
	int drawable_w, drawable_h;
	SDL_GL_GetDrawableSize(curwindow, &drawable_w, &drawable_h);

	// Cache native drawable size for use in VSDL_CommitOGLBuff every frame.
	sdl_drawable_w = drawable_w;
	sdl_drawable_h = drawable_h;

	// When running fullscreen on a display larger than the selected resolution
	// (SDL_WINDOW_FULLSCREEN_DESKTOP always gives the native drawable size),
	// letterbox/pillarbox the game into a centred, aspect-correct sub-region.
	// Scale is capped at 1.0: the viewport is at most the selected resolution,
	// never scaled up. On a matched display (windowed or native) scale == 1.0
	// and the offset is (0,0) — identical to the previous behaviour.
	{
		float scale_x = (float)drawable_w / (float)Screen_Width;
		float scale_y = (float)drawable_h / (float)Screen_Height;
		float scale   = (scale_x < scale_y) ? scale_x : scale_y;
		if (scale > 1.0f) scale = 1.0f;   // never scale up

		Vid_ViewportW = (int)(Screen_Width  * scale);
		Vid_ViewportH = (int)(Screen_Height * scale);
		Vid_ViewportX = (drawable_w - Vid_ViewportW) / 2;
		Vid_ViewportY = (drawable_h - Vid_ViewportH) / 2;

		// Clear BOTH back buffers to black so bars start clean on every buffer.
		// (double-buffering: swap once, clear the other buffer, then continue.)
		glDisable(GL_SCISSOR_TEST);
		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glViewport(0, 0, drawable_w, drawable_h);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		SDL_GL_SwapWindow(curwindow);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Restrict rendering to the letterboxed sub-region for all subsequent
		// frames. GL_SCISSOR_TEST prevents fragment writes outside this area.
		glViewport(Vid_ViewportX, Vid_ViewportY, Vid_ViewportW, Vid_ViewportH);
		glScissor(Vid_ViewportX, Vid_ViewportY, Vid_ViewportW, Vid_ViewportH);
		glEnable(GL_SCISSOR_TEST);
	}

	//glFrontFace( GL_CW ); //FIXME: ?? why was this labelled FIXME?

	// initial clear
	glClearColor( 0, 0, 0, 1 );
	glClearDepth( 0 );
	
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

	// limit maximum texture size
	int maxgltexsize = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxgltexsize);

	if (maxgltexsize < VidInfo_MaxTextureSize) {
		VidInfo_MaxTextureSize = maxgltexsize;
		VidInfo_MaxTextureLod  = CeilPow2Exp(maxgltexsize);
	}

	// determine maximum number of active texture units
	// TODO: If/when shaders are used, use GL_MAX_TEXTURE_IMAGE_UNITS instead.
	GLint maxtextureunits;
	glGetIntegerv(GL_MAX_TEXTURE_UNITS, &maxtextureunits);

	VidInfo_NumTextureUnits = max(maxtextureunits, 1);

	// initialize GL state tracking
	RO_InitializeState();

	RO_SetPixelDPIScale((float)drawable_h / (float)window_h);
}


// set opengl graphics mode ---------------------------------------------------
//
int VSDL_InitOGLMode()
{
	Uint32 mode_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;

	if ( !Op_WindowedMode ) {
		mode_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	// set the SDL GL Attributes

	if ( GameScreenBPP == 32 ) {

	    SDL_GL_SetAttribute(SDL_GL_RED_SIZE,    	    8);
	    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,  	    8);
	    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,   	    8);
	    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,  	    8);
	    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,  	    24);

	} else if ( GameScreenBPP == 16 ) {
	
	    SDL_GL_SetAttribute(SDL_GL_RED_SIZE,    	    5);
	    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,  	    6);
	    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,   	    5);
	    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE,  	    0);
	    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,  	    16);

	} else {
	
		ASSERT( 0 );
		PANIC( 0 );
	}
	
	// use double buffering if possible
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	// set antialiasing
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, AUX_MSAA > 0 ? 1 : 0);
	SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, AUX_MSAA);

    // SDL's default values.
    int contextprofile = 0;
    int gl_majorversion = 2;
    int gl_minorversion = 1;

    const char *driver = SDL_GetCurrentVideoDriver();

    // We always want to use OpenGL ES on some video backends.
    if (driver && strstr(driver, "RPI")) {
        contextprofile |= SDL_GL_CONTEXT_PROFILE_ES;

        // OpenGL ES 1.1 for now...
        gl_majorversion = 1;
        gl_minorversion = 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, contextprofile);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, gl_majorversion);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gl_minorversion);

	if (curwindow != NULL) {
		SDL_DestroyWindow(curwindow);
		curwindow = NULL;
	}

    if (curcontext != NULL) {
        SDL_GL_DeleteContext(curcontext);
        curcontext = NULL;
    }

	curwindow = SDL_CreateWindow("Parsec", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_UNDEFINED, GameScreenRes.width, GameScreenRes.height, mode_flags);

	if (curwindow == NULL) {
		MSGOUT("Could not create SDL window: %s\n", SDL_GetError());
		return FALSE;
	}

    curcontext = SDL_GL_CreateContext(curwindow);

    if (curcontext == NULL) {
        MSGOUT("Could not create OpenGL context: %s\n", SDL_GetError());
        return FALSE;
    }

	sdl_wsz_x = 0;
	sdl_wsz_y = 0;
	// For SDL_WINDOW_FULLSCREEN_DESKTOP, SDL_GetWindowSize returns the desktop
	// resolution rather than the selected game resolution. Do NOT use it to set
	// sdl_wsz_w/h or overwrite GameScreenRes: the ortho matrix
	// (VSDL_CalcOrthographicMatrix) is built from sdl_wsz_w/h, and the second
	// VID_SetResolutionVars() call in VID_InitMode() derives Screen_Width from
	// GameScreenRes — both must stay at the selected resolution so coordinate
	// systems, D_Value, and HUD/font positioning remain correct.
	// The letterbox viewport in SDL_RCSetup() reads the actual drawable size via
	// SDL_GL_GetDrawableSize() independently.
	sdl_wsz_w = GameScreenRes.width;
	sdl_wsz_h = GameScreenRes.height;

	// set vertical synchronization
	SDL_GL_SetSwapInterval(FlipSynched ? 1 : 0);

	printf("Vid mode changed to %ix%i, bpp: %i, vsync: %d, aa: %dx\n", GameScreenRes.width, GameScreenRes.height, GameScreenBPP, FlipSynched, AUX_MSAA);

	// setup current rendering context
	SDL_RCSetup();

	// display current rendering context info
	SDL_RCDisplayInfo();

    // disable system cursor inside the SDL window
	SDL_ShowCursor(SDL_DISABLE);

	return TRUE;
}


// shut down rendering canvas -------------------------------------------------
//
void VSDL_ShutDownOGL()
{
	// invalidate texture cache
	R_InvalidateCachedTexture( NULL );
	
	SDL_ShowCursor(SDL_ENABLE);
	
	if (curwindow != NULL) {
		SDL_DestroyWindow(curwindow);
		curwindow = NULL;
	}
	
	if (curcontext != NULL) {
		SDL_GL_DeleteContext(curcontext);
		curcontext = NULL;
	}
}

// module registration function -----------------------------------------------
//
REGISTER_MODULE( VSDL_OGL )
{

}
