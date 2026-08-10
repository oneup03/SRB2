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
///
/// Thin adapter over SR-lib's SimulatedReality::SRInterfaceOGL wrapper
/// (libs/SR-lib — bo3b/SR-lib, api_expansion branch). CreateSRInterfaceOGL()
/// replaces what used to be hand-rolled here:
///
///   * LoadLibraryW probing of the delay-loaded SR DLLs, core *and* the
///     OpenGL backend DLL, before any SDK entry point is touched. A delay-load
///     failure raises SEH, which a C++ catch can't intercept without /EHa.
///   * SRContext::create() -> CreateGLWeaver() -> initialize(), in that order.
///     initialize() running before weaver creation (or not at all) leaves eye
///     tracking dead while every call still reports success — the panel shows
///     an image that ignores head motion.
///   * Destroying the context with SRContext::deleteSRContext() rather than
///     `delete`, since the object lives inside the SR DLL.
///   * Containing SR's C++ exceptions behind an HRESULT return.
///
/// What stays on this side: the one-shot init latch, the SbS-texture binding
/// cache, and a try/catch around Weave() so unplugging the SR display
/// mid-session downgrades to the SbS fallback instead of killing the process.

extern "C" {
#include "doomtype.h"
#include "console.h"
#include "r_stereo_leiasr.h"
}

// HAVE_LEIASR is set only by the configurations that actually point at the SR
// SDK (Win32, where libs/SR-lib ships a 32-bit import-library tree). Everything
// else — x64/ARM MSVC configurations, and the CMake/Linux build — falls through
// to the stubs at the bottom, so no #ifdef leaks into the call sites.
#if defined(_WIN32) && defined(HAVE_LEIASR)

// doomtype.h #defines boolean -> bool. SR.hpp pulls in <d3d9.h> -> <unknwn.h>
// -> <rpcndr.h>, which contains `typedef unsigned char boolean;` — with the
// macro live that becomes `typedef unsigned char bool;` and the TU stops
// compiling. Drop the macro across the SR/Windows includes, restore it after.
#undef boolean

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdexcept>

#include "SR.hpp"

#define boolean bool

namespace {

// One interface for the process; SR-lib keeps the SRContext behind it and
// tears it down when the last interface is Delete()d.
SimulatedReality::SRInterfaceOGL *g_sr = nullptr;
bool g_init_attempted = false;
bool g_available = false;

// Binding cache. SetInputTexture re-binds the weaver's sampling source (and
// re-queries the texture's size/format from GL), which isn't free, so only do
// it when something actually changed. SRB2's screen-texture slots keep the
// same GL name across a resolution change and re-allocate the storage under
// it, hence the dimensions in the key.
unsigned int g_last_tex = 0;
int g_last_w = 0;
int g_last_h = 0;

void DestroyInterface(void)
{
	if (g_sr)
	{
		try { g_sr->Delete(); } catch (...) {}
		g_sr = nullptr;
	}
	g_last_tex = 0;
	g_last_w = 0;
	g_last_h = 0;
}

} // namespace

extern "C" void R_LeiaSR_SetupDpiAwareness(void)
{
	// Resolved dynamically so the exe still starts on Windows versions that
	// predate each entry point. Newest first; the first one that takes wins,
	// and any later call is a no-op anyway (awareness is one-shot per process).
	typedef BOOL (WINAPI *pfnSetProcessDpiAwarenessContext)(HANDLE);
	typedef HRESULT (WINAPI *pfnSetProcessDpiAwareness)(int);

	HMODULE user32 = GetModuleHandleW(L"user32.dll");
	if (user32)
	{
		pfnSetProcessDpiAwarenessContext set_ctx =
			(pfnSetProcessDpiAwarenessContext)(void *)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
		// DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
		if (set_ctx && set_ctx((HANDLE)-4))
			return;
	}

	{
		// Windows 8.1 fallback. shcore.dll is loaded on demand — we're the
		// only user of it, so there's nothing to keep it alive for.
		HMODULE shcore = LoadLibraryW(L"shcore.dll");
		if (shcore)
		{
			pfnSetProcessDpiAwareness set_aware =
				(pfnSetProcessDpiAwareness)(void *)GetProcAddress(shcore, "SetProcessDpiAwareness");
			// PROCESS_PER_MONITOR_DPI_AWARE == 2
			const bool ok = (set_aware && SUCCEEDED(set_aware(2)));
			FreeLibrary(shcore);
			if (ok)
				return;
		}
	}

	// Vista fallback: system-DPI aware only, but still better than virtualized.
	SetProcessDPIAware();
}

extern "C" boolean R_LeiaSR_Init(void *hwnd)
{
	if (g_init_attempted)
		return g_available ? true : false;
	g_init_attempted = true;

	if (hwnd == NULL)
	{
		// No HWND yet — the SDL window isn't up. Re-arm so a later frame can
		// try again rather than latching "unavailable" for the session.
		g_init_attempted = false;
		return false;
	}

	// SR-lib probes the delay-loaded SR DLLs (core + SimulatedRealityOpenGL32)
	// with LoadLibraryW before touching the SDK, so a machine without the SR
	// runtime installed lands here as a plain E_NOINTERFACE rather than an
	// uncatchable delay-load SEH. Missing runtime is the common case on
	// machines without Leia hardware, so keep that path quiet.
	const HRESULT hr = SimulatedReality::CreateSRInterfaceOGL(
		reinterpret_cast<HWND>(hwnd), &g_sr);
	if (FAILED(hr) || g_sr == nullptr)
	{
		g_sr = nullptr;
		g_available = false;
		if (hr != E_NOINTERFACE)
			CONS_Alert(CONS_WARNING, "LeiaSR: weaver creation failed (0x%08lX); falling back to Side-by-Side.\n", (unsigned long)hr);
		return false;
	}

	// SRB2's screen textures are plain GL_RGBA (no sRGB view) and the eye loop
	// writes sRGB-encoded values into them, while the default framebuffer is
	// likewise a non-sRGB surface — so the weaver should decode on read and
	// re-encode on write. There is no universally right answer here; this is
	// the pairing that matches our formats.
	try
	{
		g_sr->SetShaderSRGBConversion(true, true);
	}
	catch (...) {}

	g_available = true;
	CONS_Printf("LeiaSR: weaver initialized.\n");
	return true;
}

extern "C" boolean R_LeiaSR_Available(void)
{
	return g_available ? true : false;
}

extern "C" void R_LeiaSR_Weave(unsigned int tex_id, int width, int height)
{
	if (!g_available || g_sr == nullptr)
		return;

	// SR-lib's Weave() calls straight into the SDK weaver, which throws if the
	// SR service dies or the display is unplugged mid-session. Catch it here
	// and downgrade to the SbS fallback for the rest of the session.
	try
	{
		if (tex_id != g_last_tex || width != g_last_w || height != g_last_h)
		{
			// Dimensions and internal format are read off the GL texture
			// object by SR-lib; we only pass the name.
			g_sr->SetInputTexture(tex_id);
			g_last_tex = tex_id;
			g_last_w = width;
			g_last_h = height;
		}
		g_sr->Weave();
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
	DestroyInterface();
	g_available = false;
	g_init_attempted = false;
}

#else // no SR SDK in this configuration — stub everything.

extern "C" void    R_LeiaSR_SetupDpiAwareness(void)              { }
extern "C" boolean R_LeiaSR_Init(void *hwnd)                     { (void)hwnd; return false; }
extern "C" boolean R_LeiaSR_Available(void)                      { return false; }
extern "C" void    R_LeiaSR_Weave(unsigned int t, int w, int h)  { (void)t; (void)w; (void)h; }
extern "C" void    R_LeiaSR_Shutdown(void)                       { }

#endif
