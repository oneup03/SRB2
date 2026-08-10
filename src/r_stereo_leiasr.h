// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1999-2024 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_stereo_leiasr.h
/// \brief LeiaSR autostereoscopic display weaver bridge (C interface).
///
/// The SR runtime is C++. This header exposes a thin C-linkage shim that the
/// rest of SRB2's C codebase can call without pulling in the SR headers.
/// Implementation lives in r_stereo_leiasr.cpp, which drives SR-lib's
/// SimulatedReality::SRInterfaceOGL wrapper (libs/SR-lib, a git submodule of
/// bo3b/SR-lib on the api_expansion branch). SR-lib owns the SRContext
/// lifetime, the create-weaver-then-initialize() ordering, the delay-loaded
/// DLL probing and the exception containment; everything below is just SRB2's
/// side of the handshake.

#ifndef __R_STEREO_LEIASR__
#define __R_STEREO_LEIASR__

#include "doomtype.h"

#ifdef __cplusplus
extern "C" {
#endif

// Declare the process per-monitor-DPI-aware. MUST be called before SDL touches
// its video subsystem (process DPI awareness is one-shot — the first declaration
// wins and later calls silently no-op, and SDL declares it during
// SDL_Init/SDL_InitSubSystem(SDL_INIT_VIDEO)).
//
// This is a hard prerequisite for the weave, not a nicety: the lenticular
// interleave only produces autostereo when it lands 1:1 on physical panel
// pixels. On a display at >100% Windows scale, a DPI-unaware process gets its
// window — and therefore the weaver's output — rendered into a virtualized
// sub-region and stretched up by the OS, which destroys the 3D. It matters for
// the row/column-interlaced and checkerboard modes for the same reason.
void R_LeiaSR_SetupDpiAwareness(void);

// One-shot initialization. Must be called after the SDL window + GL context
// exist (needs the HWND, and the GL weaver binds to the GL context current on
// the calling thread). Safe to call repeatedly — only the first call does real
// work. Returns true on success, false if the SR runtime / hardware isn't
// available (in which case STEREO_LEIASR mode should fall back to SbS).
boolean R_LeiaSR_Init(void *hwnd);

// True after a successful init AND when the runtime is still healthy.
boolean R_LeiaSR_Available(void);

// Per-frame: hand the weaver a SbS texture (whole frame as one texture, left
// half = left eye, right half = right eye) and ask it to render the woven
// image into the currently-bound framebuffer (typically the default FBO) at
// the current viewport.
//
// SR-lib reads the texture's dimensions and internal format off the GL texture
// object itself, so it can never be told a size the texture doesn't have. The
// width/height passed here are only used to notice when a texture slot has been
// re-allocated at a new size under the same GL name, so the weaver's input
// binding gets refreshed. tex_id must remain valid until R_LeiaSR_Weave returns.
void R_LeiaSR_Weave(unsigned int tex_id, int width, int height);

// Tear down the weaver + SR context. Must be called while the GL context is
// still current — the SR runtime holds GL resources keyed to it.
void R_LeiaSR_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
