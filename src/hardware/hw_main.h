// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1998-2000 by DooM Legacy Team.
// Copyright (C) 1999-2024 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_main.h
/// \brief 3D render mode functions

#ifndef __HWR_MAIN_H__
#define __HWR_MAIN_H__

#include "hw_data.h"
#include "hw_defs.h"

#include "../am_map.h"
#include "../d_player.h"
#include "../r_defs.h"

#include "../m_perfstats.h"

// Startup & Shutdown the hardware mode renderer
void HWR_Startup(void);
void HWR_Switch(void);
void HWR_Shutdown(void);

void HWR_drawAMline(const fline_t *fl, INT32 color);
void HWR_FadeScreenMenuBack(UINT16 color, UINT8 strength);
void HWR_DrawConsoleBack(UINT32 color, INT32 height);
void HWR_DrawTutorialBack(UINT32 color, INT32 boxheight);
void HWR_RenderSkyboxView(INT32 viewnumber, player_t *player);
void HWR_RenderPlayerView(INT32 viewnumber, player_t *player);
void HWR_ClearSkyDome(void);
void HWR_BuildSkyDome(void);
void HWR_DrawFlatFill(INT32 x, INT32 y, INT32 w, INT32 h, lumpnum_t flatlumpnum);
void HWR_SetViewSize(void);
void HWR_DrawStretchyFixedPatch(patch_t *gpatch, fixed_t x, fixed_t y, fixed_t pscale, fixed_t vscale, INT32 option, const UINT8 *colormap);
void HWR_DrawCroppedPatch(patch_t *gpatch, fixed_t x, fixed_t y, fixed_t pscale, fixed_t vscale, INT32 option, const UINT8 *colormap, fixed_t sx, fixed_t sy, fixed_t w, fixed_t h);
void HWR_MakePatch(const patch_t *patch, GLPatch_t *grPatch, GLMipmap_t *grMipmap, boolean makebitmap);
void HWR_CreatePlanePolygons(INT32 bspnum);
void HWR_CreateStaticLightmaps(INT32 bspnum);
void HWR_DrawFill(INT32 x, INT32 y, INT32 w, INT32 h, INT32 color);
void HWR_DrawFadeFill(INT32 x, INT32 y, INT32 w, INT32 h, INT32 color, UINT16 actualcolor, UINT8 strength);
void HWR_DrawConsoleFill(INT32 x, INT32 y, INT32 w, INT32 h, INT32 color, UINT32 actualcolor);	// Lat: separate flags from color since color needs to be an uint to work right.

UINT8 *HWR_GetScreenshot(void);
boolean HWR_Screenshot(const char *pathname);

void HWR_AddCommands(void);
void transform(float *cx, float *cy, float *cz);
INT32 HWR_GetTextureUsed(void);
void HWR_DoPostProcessor(player_t *player);
void HWR_StartScreenWipe(void);
void HWR_EndScreenWipe(void);
void HWR_DrawIntermissionBG(void);
// Captures the current framebuffer into HWD_SCREENTEXTURE_GENERIC1 — the
// "screen snapshot" slot sampled by HWR_DrawIntermissionBG and the
// underwater/heat wave. d_main.c calls this once after the stereo eye loop
// completes so the snapshot contains BOTH eyes' fully-painted halves; the
// per-player capture inside HWR_DoPostProcessor otherwise fires before each
// eye's HUD pass and would leave the snapshot's right half missing the HUD
// (visible as "intermission BG asymmetric, lives/rings only show in one eye").
void HWR_MakeScreenTexture(void);
void HWR_DoWipe(UINT8 wipenum, UINT8 scrnnum);
void HWR_MakeScreenFinalTexture(void);
void HWR_DrawScreenFinalTexture(int width, int height);
// Like HWR_DrawScreenFinalTexture but draws into the (x,y,width,height)
// sub-rect of the window without clearing — used to duplicate a captured
// mono backbuffer into per-eye SbS / TaB halves.
void HWR_DrawScreenFinalTextureAt(int x, int y, int width, int height);
// Returns the GL texture ID for the slot used by HWR_MakeScreenFinalTexture
// (GENERIC2 or GENERIC3 depending on palette-rendering state). Callers that
// can't reach HWD directly (e.g. ogl_sdl.c, which sets _CREATE_DLL_) use
// this to find the captured backbuffer for downstream effects like the
// LeiaSR weaver.
UINT32 HWR_GetScreenFinalTextureID(void);
// Tightly-fitted (NPOT) screen capture into HWD_SCREENTEXTURE_LEIA, used by
// the LeiaSR bridge as input to the weaver.
void   HWR_MakeScreenLeiaTexture(void);
// Capture the framebuffer at exact (width, height) into LEIA tex. Used when
// the rendered backbuffer is smaller than the SDL window — caller stretches
// the rendered content to fill first, then captures the stretched output.
void   HWR_MakeScreenLeiaTextureSized(INT32 width, INT32 height);
UINT32 HWR_GetScreenLeiaTextureID(void);

// Set the GL viewport to (0, 0, width, height). Used by the LeiaSR present
// path right before R_LeiaSR_Weave so the weaver writes to the full SDL
// window instead of the engine's render rectangle (which may be smaller).
void   HWR_SetPresentViewport(INT32 width, INT32 height);
// Thin pass-throughs for the stereo-mode driver hooks, so callers that can't
// reach HWD directly (anything that includes r_opengl.h, which sets
// _CREATE_DLL_) can still drive per-eye state.
void HWR_SetStereoMode(INT32 mode, INT32 eye, INT32 x, INT32 y, INT32 w, INT32 h);
void HWR_ResetStereoMode(void);
// Composite the captured LEIA texture into a stereo display format via a
// fragment shader. shader_target selects the composite kind:
//   SHADER_ROW_INTERLACED_COMPOSITE     — TaB source → row-interleaved
//   SHADER_COLUMN_INTERLACED_COMPOSITE  — SbS source → column-interleaved
//   SHADER_CHECKERBOARD_COMPOSITE       — SbS source → checkerboard
//   SHADER_ANAGLYPH_DUBOIS_COMPOSITE    — SbS source → red/cyan Dubois anaglyph
void HWR_DrawStereoComposite(INT32 shader_target, INT32 width, INT32 height);

// This stuff is put here so models can use them
boolean HWR_UseShader(void);
void HWR_Lighting(FSurfaceInfo *Surface, INT32 light_level, extracolormap_t *colormap);
UINT8 HWR_FogBlockAlpha(INT32 light, extracolormap_t *colormap); // Let's see if this can work

UINT8 HWR_GetTranstableAlpha(INT32 transtablenum);
FBITFIELD HWR_GetBlendModeFlag(INT32 style);
FBITFIELD HWR_SurfaceBlend(INT32 style, INT32 transtablenum, FSurfaceInfo *pSurf);
FBITFIELD HWR_TranstableToAlpha(INT32 transtablenum, FSurfaceInfo *pSurf);

boolean HWR_ShouldUsePaletteRendering(void);

extern CV_PossibleValue_t glanisotropicmode_cons_t[];

#ifdef ALAM_LIGHTING
extern consvar_t cv_gldynamiclighting;
extern consvar_t cv_glstaticlighting;
extern consvar_t cv_glcoronas;
extern consvar_t cv_glcoronasize;
#endif

extern consvar_t cv_glshaders, cv_glallowshaders;
extern consvar_t cv_glmodels;
extern consvar_t cv_glmodelinterpolation;
extern consvar_t cv_glmodellighting;
extern consvar_t cv_glfiltermode;
extern consvar_t cv_glanisotropicmode;
extern consvar_t cv_glsolvetjoin;
extern consvar_t cv_glshearing;
extern consvar_t cv_glspritebillboarding;
extern consvar_t cv_glskydome;
extern consvar_t cv_glfakecontrast;
extern consvar_t cv_glslopecontrast;
extern consvar_t cv_glbatching;
extern consvar_t cv_glpaletterendering;
extern consvar_t cv_glpalettedepth;

extern consvar_t cv_glwireframe;

// BP: big hack for a test in lighting ref : 1249753487AB
extern fixed_t *hwbbox;
extern FTransform atransform;
extern float gl_viewsin, gl_viewcos;

// Render stats
extern ps_metric_t ps_hw_skyboxtime;
extern ps_metric_t ps_hw_nodesorttime;
extern ps_metric_t ps_hw_nodedrawtime;
extern ps_metric_t ps_hw_spritesorttime;
extern ps_metric_t ps_hw_spritedrawtime;

// Render stats for batching
extern ps_metric_t ps_hw_numpolys;
extern ps_metric_t ps_hw_numverts;
extern ps_metric_t ps_hw_numcalls;
extern ps_metric_t ps_hw_numshaders;
extern ps_metric_t ps_hw_numtextures;
extern ps_metric_t ps_hw_numpolyflags;
extern ps_metric_t ps_hw_numcolors;
extern ps_metric_t ps_hw_batchsorttime;
extern ps_metric_t ps_hw_batchdrawtime;

extern boolean gl_init;
extern boolean gl_maploaded;
extern boolean gl_maptexturesloaded;
extern boolean gl_sessioncommandsadded;
extern boolean gl_shadersavailable;

#endif
