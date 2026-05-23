// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 2014-2023 by Sonic Team Junior.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
//-----------------------------------------------------------------------------
/// \file
/// \brief SDL specific part of the OpenGL API for SRB2

#ifdef _MSC_VER
#pragma warning(disable : 4214 4244)
#endif

#ifdef HAVE_SDL
#define _MATH_DEFINES_DEFINED

#include "SDL.h"

#include "sdlmain.h"

#ifdef _MSC_VER
#pragma warning(default : 4214 4244)
#endif

#include "../doomdef.h"

#ifdef HWRENDER
#include "../hardware/r_opengl/r_opengl.h"
#include "../hardware/hw_main.h"
#include "ogl_sdl.h"
#include "../i_system.h"
#include "../i_video.h"
#include "hwsym_sdl.h"
#include "../m_argv.h"
#include "../r_stereo.h"
#include "../r_stereo_leiasr.h"

#ifdef DEBUG_TO_FILE
#include <stdarg.h>
#if defined (_WIN32) && !defined (__CYGWIN__)
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#endif

#ifdef USE_WGL_SWAP
PFNWGLEXTSWAPCONTROLPROC wglSwapIntervalEXT = NULL;
#else
typedef int (*PFNGLXSWAPINTERVALPROC) (int);
PFNGLXSWAPINTERVALPROC glXSwapIntervalSGIEXT = NULL;
#endif

#ifndef STATIC_OPENGL
PFNglClear pglClear;
PFNglGetIntegerv pglGetIntegerv;
PFNglGetString pglGetString;
#endif

/**	\brief SDL video display surface
*/
INT32 oglflags = 0;
SDL_GLContext sdlglcontext = 0;

void *GetGLFunc(const char *proc)
{
	return SDL_GL_GetProcAddress(proc);
}

boolean LoadGL(void)
{
#ifndef STATIC_OPENGL
	const char *OGLLibname = NULL;

	if (M_CheckParm("-OGLlib") && M_IsNextParm())
		OGLLibname = M_GetNextParm();

	if (SDL_GL_LoadLibrary(OGLLibname) != 0)
	{
		CONS_Alert(CONS_ERROR, "Could not load OpenGL Library: %s\n"
					"Falling back to Software mode.\n", SDL_GetError());
		if (!M_CheckParm("-OGLlib"))
			CONS_Printf("If you know what is the OpenGL library's name, use -OGLlib\n");
		return 0;
	}
#endif
	return SetupGLfunc();
}

/**	\brief	The OglSdlSurface function

	\param	w	width
	\param	h	height
	\param	isFullscreen	if true, go fullscreen

	\return	if true, changed video mode
*/
boolean OglSdlSurface(INT32 w, INT32 h)
{
	INT32 cbpp = cv_scr_depth.value < 16 ? 16 : cv_scr_depth.value;
	static boolean first_init = false;
	static int majorGL = 0, minorGL = 0;

	oglflags = 0;

	if (!first_init)
	{
		gl_version = pglGetString(GL_VERSION);
		gl_renderer = pglGetString(GL_RENDERER);
		gl_extensions = pglGetString(GL_EXTENSIONS);

		GL_DBG_Printf("OpenGL %s\n", gl_version);
		GL_DBG_Printf("GPU: %s\n", gl_renderer);
		GL_DBG_Printf("Extensions: %s\n", gl_extensions);

		if (strcmp((const char*)gl_renderer, "GDI Generic") == 0 &&
			strcmp((const char*)gl_version, "1.1.0") == 0)
		{
			// Oh no... Windows gave us the GDI Generic rasterizer, so something is wrong...
			// The game will crash later on when unsupported OpenGL commands are encountered.
			// Instead of a nondescript crash, show a more informative error message.
			// Also set the renderer variable back to software so the next launch won't
			// repeat this error.
			CV_StealthSet(&cv_renderer, "Software");
			I_Error("OpenGL Error: Failed to access the GPU. Possible reasons include:\n"
					"- GPU vendor has dropped OpenGL support on your GPU and OS. (Old GPU?)\n"
					"- GPU drivers are missing or broken. You may need to update your drivers.");
		}
	}
	first_init = true;

	if (isExtAvailable("GL_EXT_texture_filter_anisotropic", gl_extensions))
		pglGetIntegerv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maximumAnisotropy);
	else
		maximumAnisotropy = 1;

	if (sscanf((const char*)gl_version, "%d.%d", &majorGL, &minorGL)
		&& (!(majorGL == 1 && minorGL <= 3)))
		supportMipMap = true;
	else
		supportMipMap = false;

	SetupGLFunc4();

	glanisotropicmode_cons_t[1].value = maximumAnisotropy;

	SDL_GL_SetSwapInterval(cv_vidwait.value ? 1 : 0);

	SetModelView(w, h);
	SetStates();
	pglClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

	HWR_Startup();
	textureformatGL = cbpp > 16 ? GL_RGBA : GL_RGB5_A1;

	return true;
}

/**	\brief	The OglSdlFinishUpdate function

	\param	vidwait	wait for video sync

	\return	void
*/
void OglSdlFinishUpdate(boolean waitvbl)
{
	static boolean oldwaitvbl = false;
	int sdlw, sdlh;
	if (oldwaitvbl != waitvbl)
	{
		SDL_GL_SetSwapInterval(waitvbl ? 1 : 0);
	}

	oldwaitvbl = waitvbl;

	SDL_GetWindowSize(window, &sdlw, &sdlh);

	// Decide which present path to take. Shader-composite modes
	// (Anaglyph-Dubois, Row-Interlaced, Column-Interlaced, Checkerboard)
	// all share the same recipe — stretch the TaB or SbS internal render
	// to fill the SDL window, recapture into the LEIA texture, then run a
	// per-mode fragment-shader composite at display resolution. LeiaSR has
	// its own SR-weaver-driven path. Everything else just stretches and
	// presents directly.
	INT32 composite_shader = -1;
	if (R_StereoActive())
	{
		switch (cv_stereomode.value)
		{
			case STEREO_ANAGLYPH:
				composite_shader = SHADER_ANAGLYPH_DUBOIS_COMPOSITE;
				break;
			case STEREO_ROW_INTERLACED:
				composite_shader = SHADER_ROW_INTERLACED_COMPOSITE;
				break;
			case STEREO_COLUMN_INTERLACED:
				composite_shader = SHADER_COLUMN_INTERLACED_COMPOSITE;
				break;
			case STEREO_CHECKERBOARD:
				composite_shader = SHADER_CHECKERBOARD_COMPOSITE;
				break;
			default:
				break;
		}
	}

	if (composite_shader >= 0)
	{
		HWR_MakeScreenFinalTexture();
		HWR_DrawScreenFinalTexture(sdlw, sdlh);

		HWR_MakeScreenLeiaTextureSized(sdlw, sdlh);
		HWR_DrawStereoComposite(composite_shader, sdlw, sdlh);
	}
	// LeiaSR mode: hand the captured backbuffer (SbS-packed by the eye
	// loop) directly to the SR weaver. The weaver writes into the
	// currently-bound viewport and does any upscale internally as it
	// samples the input texture — no separate stretch + recapture pass
	// is needed (see the leiasr-integration skill, section A.4).
	//
	// Three steps:
	//   1. Capture the backbuffer at exact engine render size into a
	//      tightly-fitted NPOT LINEAR-filtered texture (LEIA slot).
	//   2. Set the GL viewport to the SDL window dimensions so the
	//      weaver's output fills the panel (after the eye loop +
	//      ResetStereoMode the viewport is still at vid.width ×
	//      vid.height, which may be smaller than the SDL window).
	//   3. Weave. The weaver samples LEIA (vid.width × vid.height) and
	//      writes to (sdlw × sdlh).
	//
	// We deliberately do NOT call GClipRect between the viewport set
	// and the weave — GClipRect's viewport math uses screen_height
	// (the rendered backbuffer size) and would produce a negative Y
	// when sdlh > screen_height, clipping the weave output to a
	// sub-region of the display.
	//
	// Falls back to the normal final-texture composite if the LeiaSR
	// runtime didn't initialize (no Leia hardware/service).
	else if (R_StereoMode() == STEREO_LEIASR)
	{
		R_LeiaSR_Init(I_GetWindowHandle()); // lazy init
		if (R_LeiaSR_Available())
		{
			HWR_MakeScreenLeiaTexture();
			HWR_SetPresentViewport(sdlw, sdlh);
			{
				const UINT32 tex_id = HWR_GetScreenLeiaTextureID();
				R_LeiaSR_Weave(tex_id, vid.width, vid.height);
			}
		}
		else
		{
			HWR_MakeScreenFinalTexture();
			HWR_DrawScreenFinalTexture(sdlw, sdlh);
		}
	}
	else
	{
		HWR_MakeScreenFinalTexture();
		HWR_DrawScreenFinalTexture(sdlw, sdlh);
	}
	SDL_GL_SwapWindow(window);

	GClipRect(0, 0, realwidth, realheight, NZCLIP_PLANE);

	// Sryder:	We need to draw the final screen texture again into the other buffer in the original position so that
	//			effects that want to take the old screen can do so after this
	// Generic2 has the screen image without palette rendering brightness adjustments.
	// Using that here will prevent brightness adjustments being applied twice.
	DrawScreenTexture(HWD_SCREENTEXTURE_GENERIC2, NULL, 0);
}

EXPORT void HWRAPI(OglSdlSetPalette) (RGBA_t *palette)
{
	size_t palsize = (sizeof(RGBA_t) * 256);
	// on a palette change, you have to reload all of the textures
	if (memcmp(&myPaletteData, palette, palsize))
	{
		memcpy(&myPaletteData, palette, palsize);
		Flush();
	}
}

#endif //HWRENDER
#endif //SDL
