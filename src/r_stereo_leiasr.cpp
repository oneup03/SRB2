// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1999-2024 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_stereo_leiasr.cpp
/// \brief LeiaSR autostereoscopic display weaver bridge (C++ implementation).

extern "C" {
#include "doomtype.h"
#include "console.h"
#include "r_stereo_leiasr.h"
}

#ifdef _WIN32

// Windows + LeiaSR pulls in <windows.h>; do that before anything that might
// clash. Wrapping in our own include block keeps SDL/Doom symbols isolated
// from the LeiaSR side.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdexcept>

#include <sr/management/srcontext.h>
#include <sr/weaver/glweaver.h>

namespace {

// LeiaSR holds its SRContext and weaver for the lifetime of the GL context.
// One context + one weaver = one Leia display target.
SR::SRContext *g_context = nullptr;
SR::IGLWeaver1 *g_weaver = nullptr;
bool g_init_attempted = false;
bool g_available = false;

// Required LeiaSR DLLs (and their direct OpenCV dependency). These are
// configured as DelayLoadDLLs in Srb2SDL-vc10.vcxproj so the import resolver
// won't refuse to launch the .exe when they're absent — but we still have
// to gate any actual call into them on the DLLs being present, otherwise the
// first call would trigger a delay-load failure (SEH, may or may not be
// catchable depending on /EHa). LoadLibraryW returns NULL when the DLL OR
// any of its transitive dependencies are missing, so a successful probe
// here is a clean go-signal for the rest of the LeiaSR init.
//
// Only the DLLs the exe actually imports symbols from need to be probed —
// simulatedreality32.dll and DimencoWeaving32.dll are umbrella .libs that
// forward through Core/OpenGL at link time, so probing those two would be
// a wasted LoadLibrary. The transitive deps (realsense2, libserialport32,
// vcruntime, msvcp, glog, etc.) load implicitly when Core/OpenGL/OpenCV
// load — if any are missing, LoadLibraryW returns NULL for the parent and
// the probe naturally catches it.
const wchar_t *const k_required_dlls[] = {
	L"SimulatedRealityCore32.dll",
	L"SimulatedRealityOpenGL32.dll",
	L"opencv_world343.dll",
};

bool ProbeLeiaSRDLLs(void)
{
	for (const wchar_t *name : k_required_dlls)
	{
		HMODULE h = LoadLibraryW(name);
		if (!h)
			return false;
		// We don't keep the handle around — the delay-loaded import will
		// LoadLibrary it again at the first call, which is cheap because
		// the loader maintains a reference count and finds the already-
		// resolved module immediately.
		FreeLibrary(h);
	}
	return true;
}

} // namespace

extern "C" boolean R_LeiaSR_Init(void *hwnd)
{
	if (g_init_attempted)
		return g_available ? true : false;
	g_init_attempted = true;

	// DLL availability probe BEFORE touching any LeiaSR API. If the LeiaSR
	// runtime isn't installed (or one of the transitive deps like
	// opencv_world343.dll is missing), bail to SbS fallback silently — this
	// is the common case on machines without Leia hardware and shouldn't
	// produce scary console spam.
	if (!ProbeLeiaSRDLLs())
	{
		g_available = false;
		return false;
	}

	// Construction can throw if the SR runtime/service isn't reachable, or
	// if no Leia display is connected. Catch any failure and downgrade to
	// "not available" so the caller falls back to plain SbS.
	try
	{
		g_context = new SR::SRContext();
		const WeaverErrorCode err = SR::CreateGLWeaver(*g_context,
			reinterpret_cast<HWND>(hwnd), &g_weaver);
		if (err != WeaverSuccess || g_weaver == nullptr)
		{
			CONS_Alert(CONS_WARNING, "LeiaSR: CreateGLWeaver failed (code %d); LeiaSR mode unavailable.\n", (int)err);
			delete g_context;
			g_context = nullptr;
			g_weaver = nullptr;
			g_available = false;
			return false;
		}
		g_context->initialize();
		g_available = true;
		CONS_Printf("LeiaSR: weaver initialized.\n");
		return true;
	}
	catch (const std::exception &ex)
	{
		CONS_Alert(CONS_WARNING, "LeiaSR: init threw: %s — LeiaSR mode unavailable.\n", ex.what());
	}
	catch (...)
	{
		CONS_Alert(CONS_WARNING, "LeiaSR: init threw an unknown exception — LeiaSR mode unavailable.\n");
	}

	if (g_weaver) { g_weaver->destroy(); g_weaver = nullptr; }
	delete g_context;
	g_context = nullptr;
	g_available = false;
	return false;
}

extern "C" boolean R_LeiaSR_Available(void)
{
	return g_available ? true : false;
}

extern "C" void R_LeiaSR_Weave(unsigned int tex_id, int width, int height)
{
	if (!g_available || g_weaver == nullptr)
		return;

	// SRB2 backbuffer is RGBA8 (or RGB5_A1 in low-bpp mode). Pass GL_RGBA;
	// the weaver will sample as needed.
	try
	{
		g_weaver->setInputViewTexture(tex_id, width, height, /*GL_RGBA=*/0x1908);
		// IWeaverBase1::weave() takes no arguments — uses the bound viewport
		// and the input texture set above.
		g_weaver->weave();
	}
	catch (const std::exception &ex)
	{
		CONS_Alert(CONS_WARNING, "LeiaSR: weave threw: %s — disabling LeiaSR mode.\n", ex.what());
		g_available = false;
	}
	catch (...)
	{
		CONS_Alert(CONS_WARNING, "LeiaSR: weave threw an unknown exception — disabling LeiaSR mode.\n");
		g_available = false;
	}
}

extern "C" void R_LeiaSR_Shutdown(void)
{
	if (g_weaver)
	{
		try { g_weaver->destroy(); } catch (...) {}
		g_weaver = nullptr;
	}
	if (g_context)
	{
		delete g_context;
		g_context = nullptr;
	}
	g_available = false;
	g_init_attempted = false;
}

#else // !_WIN32 — LeiaSR is Windows-only. Stub everything.

extern "C" boolean R_LeiaSR_Init(void *hwnd)              { (void)hwnd; return false; }
extern "C" boolean R_LeiaSR_Available(void)               { return false; }
extern "C" void    R_LeiaSR_Weave(unsigned int t, int w, int h) { (void)t; (void)w; (void)h; }
extern "C" void    R_LeiaSR_Shutdown(void)                {}

#endif
