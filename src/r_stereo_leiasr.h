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
/// The actual SR/Weaver classes are C++. This header exposes a thin C-linkage
/// shim that the rest of SRB2's C codebase can call without pulling in the
/// LeiaSR headers. Implementation lives in r_stereo_leiasr.cpp.

#ifndef __R_STEREO_LEIASR__
#define __R_STEREO_LEIASR__

#include "doomtype.h"

#ifdef __cplusplus
extern "C" {
#endif

// One-shot initialization. Must be called after the SDL window + GL context
// exist (needs the HWND). Safe to call repeatedly — only the first call does
// real work. Returns true on success, false if the LeiaSR runtime / hardware
// isn't available (in which case STEREO_LEIASR mode should fall back to SbS).
boolean R_LeiaSR_Init(void *hwnd);

// True after a successful init AND when the runtime is still healthy.
boolean R_LeiaSR_Available(void);

// Per-frame: hand the weaver a SbS texture (whole frame as one texture, left
// half = left eye, right half = right eye) and ask it to render the woven
// image into the currently-bound framebuffer (typically the default FBO).
// width/height are the SbS texture dimensions. tex_id must remain valid
// until R_LeiaSR_Weave returns.
void R_LeiaSR_Weave(unsigned int tex_id, int width, int height);

// Tear down the weaver + SR context. Safe to call from shutdown.
void R_LeiaSR_Shutdown(void);

#ifdef __cplusplus
}
#endif

#endif
