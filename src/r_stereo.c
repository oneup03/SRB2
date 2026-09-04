// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 1999-2024 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file  r_stereo.c
/// \brief Stereoscopic 3D rendering: SbS, TaB, Anaglyph, Interlaced.
///
/// Per-eye off-axis asymmetric frustum, parameterized in CLIP SPACE (the
/// NVIDIA / 3Dmigoto convention, x += separation * (w - convergence)) rather
/// than as a physical eye separation in world units.
///
/// The single stereo knob is `separation`: the projection's [2][0] shear term
/// IS that value, with no convergence and no FoV factor mixed in, and its
/// magnitude equals the total at-infinity disparity as a fraction of the
/// screen width. The physical eye baseline that used to be the knob is now a
/// derived per-frame quantity, 2 * separation * tan(fov/2) * convergence.
///
/// Two consequences of that change, both intentional:
///   * Depth no longer inflates when the game zooms. The old form multiplied
///     the eye separation by 1/tan(fov/2) through the projection, so a narrow
///     FoV magnified disparity. The clip-space shear is FoV-invariant by
///     construction, which is also why the HUD/crosshair shift below no
///     longer reads cv_fov at all -- and why it now agrees with the world
///     even in splitscreen, where the frustum's FoV is fudged.
///   * Convergence no longer changes background depth. It used to divide the
///     shear (disparity ~ ipd/focal), so pushing the screen plane out
///     flattened the whole image; now it only moves what sits in front of the
///     screen. That drops an accidental safety coupling -- a large
///     convergence now spends more depth budget, not less.
///
/// Eye state is stashed in module globals and consumed by HWR_SetupView when
/// it builds FTransform for each pass.

#include <math.h>

#include "r_stereo.h"

#include "command.h"
#include "console.h"
#include "doomdef.h"
#include "doomstat.h"
#include "d_player.h"
#include "i_video.h"
#include "p_local.h"
#include "p_maputl.h"
#include "r_defs.h"
#include "r_main.h"
#include "r_state.h"
#include "screen.h"
#include "tables.h"

#ifdef HWRENDER
#include "hardware/hw_main.h"   // HWR_SetStereoMode / HWR_ResetStereoMode
#endif

static CV_PossibleValue_t stereomode_cons_t[] = {
	{STEREO_OFF,               "Off"},
	{STEREO_SBS,               "SideBySide"},
	{STEREO_TAB,               "TopBottom"},
	{STEREO_ANAGLYPH,          "Anaglyph"},
	{STEREO_ROW_INTERLACED,    "RowInterlaced"},
	{STEREO_COLUMN_INTERLACED, "ColumnInterlaced"},
	{STEREO_CHECKERBOARD,      "Checkerboard"},
	{STEREO_LEIASR,            "LeiaSR"},
	{0, NULL}
};

// CVAR storage uses small whole-number "slider units" so the in-game slider
// shows friendly values; the runtime scales them down to recover the real
// quantity. The multiplier is picked per-CVAR to keep each slider's label
// range close to its useful tuning band.
//   stereosep             slider value * 0.001 = clip-space separation
//   stereofoclen          slider value * 1.0   = world units
//   stereohuddepth        slider value * 0.01  = depth fraction
//   stereocrosshairdepth  slider value * 0.01  = depth fraction
//   stereoghostcontrast   slider value * 0.01  = contrast multiplier
//   stereoghostlift       slider value * 0.001 = black-floor lift
//
// Separation is the total at-infinity disparity as a fraction of the screen
// width, so it means the same thing on every display and at every FoV. Its
// hard ceiling is IPD / screen_width -- roughly 0.105 on a 27" 16:9 desk
// monitor -- past which the background forces the eyes to diverge and cannot
// be fused at all, so the slider stops at 0.120 rather than somewhere
// arbitrary. The default 0.030 reproduces the previous defaults exactly:
// 6.0 wu eye separation at 100 wu focal and 90 degree FoV converts to
// 6.0 * 0.5 / (100 * tan(45 deg)) = 0.030. 0.050 is the usual comfortable
// starting point on a desktop monitor if you want more pop.
//
// Convergence is the world depth that lands on the screen plane. SRB2 world
// scale: player ~32 wu tall, characters ~50-200 wu away, rooms 100s-1000s wu
// across, so the useful band runs from a few tens to a few hundred wu. Its
// range is wider than before because convergence no longer scales the image's
// overall depth -- raising it is now cheap, it just pushes more of the scene
// in front of the screen.
//
// Ghost reduction (applied by the composite shaders, see hw_shaders.h):
// contrast squeezes the signal toward mid-grey, shrinking the inter-eye
// brightness difference every stereo display leaks; lift raises the black
// floor, which is what a display that actively CANCELS crosstalk needs --
// LeiaSR's weaver subtracts a fraction of the opposite eye, driving dark
// pixels below zero, and the clipped part is exactly what survives as a
// visible ghost. Both default to their exact no-op values.
//
// Out-of-range values can still be typed at the console -- the slider just
// visually clamps.
static CV_PossibleValue_t stereosep_cons_t[]            = {{0,    "MIN"}, {120, "MAX"}, {0, NULL}};   // 0.000-0.120 separation (default 0.030)
static CV_PossibleValue_t stereofoclen_cons_t[]         = {{20,   "MIN"}, {600, "MAX"}, {0, NULL}};   // 20-600 wu convergence (default 100 wu)
// The two overlay-depth sliders are a depth FRACTION d = 1 - convergence/z,
// not a distance: 0 is the screen plane, positive recedes behind it, negative
// pops out in front of it. d = 1 is the at-infinity limit, where the overlay
// carries exactly the background disparity (separation x screen width) and
// sits as deep as the sky; past 1 there is no matching world depth left, the
// value simply keeps scaling disparity, so treat 1.5 as "1.5x the background"
// and keep an eye on the divergence ceiling described above.
//
// Both ranges are the mirror of the ones this file shipped with, because the
// overlay shift used to run opposite to world geometry (see the SIGN FIX note
// on Stereo_OverlayShiftPixels). Mirroring them means the defaults still put
// the HUD and crosshair exactly where they have always appeared on screen --
// only the number in the menu changed sign.
static CV_PossibleValue_t stereohuddepth_cons_t[]       = {{-50,  "MIN"}, {100, "MAX"}, {0, NULL}};   // -0.50..+1.00 fraction (HUD biased toward sitting behind the screen)
static CV_PossibleValue_t stereocrosshairdepth_cons_t[] = {{0,    "MIN"}, {150, "MAX"}, {0, NULL}};   // 0.00..+1.50 fraction (crosshair sits at or behind the screen plane)
static CV_PossibleValue_t stereoghostcontrast_cons_t[]  = {{50,   "MIN"}, {100, "MAX"}, {0, NULL}};   // 0.50-1.00 contrast (100 = off)
static CV_PossibleValue_t stereoghostlift_cons_t[]      = {{0,    "MIN"}, { 80, "MAX"}, {0, NULL}};   // 0.000-0.080 lift (0 = off)

// The legacy eye-separation-in-world-units slider. Kept registered and saved
// only so an existing config carries its owner's tuning across into stereosep
// exactly once; the conversion zeroes it afterwards, and 0 means "nothing to
// migrate". See Stereo_ResolveLegacyIPD.
static CV_PossibleValue_t stereoipd_cons_t[]            = {{0,    "MIN"}, {400, "MAX"}, {0, NULL}};

static void Stereo_OnChange(void);
static void Stereo_SepOnChange(void);
static void Stereo_LegacyIPDOnChange(void);

consvar_t cv_stereomode             = CVAR_INIT("stereomode",             "Off",  CV_SAVE|CV_CALL, stereomode_cons_t, Stereo_OnChange);
consvar_t cv_stereosep              = CVAR_INIT("stereosep",              "30",   CV_SAVE|CV_CALL|CV_NOINIT, stereosep_cons_t, Stereo_SepOnChange);      // *0.001 -> 0.030 separation
consvar_t cv_stereofoclen           = CVAR_INIT("stereofoclen",           "100",  CV_SAVE,         stereofoclen_cons_t,  NULL);   // *1.0 -> 100.0 wu convergence
consvar_t cv_stereoswap             = CVAR_INIT("stereoswap",             "Off",  CV_SAVE,         CV_OnOff,             NULL);
consvar_t cv_stereohuddepth         = CVAR_INIT("stereohuddepth",         "20",   CV_SAVE,         stereohuddepth_cons_t,       NULL);   // +0.20 fraction -- HUD sits just behind the screen plane (z = 1.25 x convergence)
consvar_t cv_stereocrosshairdepth   = CVAR_INIT("stereocrosshairdepth",   "100",  CV_SAVE,         stereocrosshairdepth_cons_t, NULL);   // +1.00 fraction -- crosshair sits at the at-infinity limit, as deep as the sky
consvar_t cv_stereoghostcontrast    = CVAR_INIT("stereoghostcontrast",    "100",  CV_SAVE,         stereoghostcontrast_cons_t,  NULL);   // *0.01 -> 1.00 (exact no-op)
consvar_t cv_stereoghostlift        = CVAR_INIT("stereoghostlift",        "0",    CV_SAVE,         stereoghostlift_cons_t,      NULL);   // *0.001 -> 0.000 (exact no-op)
consvar_t cv_stereoipd              = CVAR_INIT("stereoipd",              "0",    CV_SAVE|CV_CALL|CV_NOINIT, stereoipd_cons_t, Stereo_LegacyIPDOnChange);   // 0 = nothing to migrate

// current_eye holds the *perspective* eye for the active pass — it tracks
// which eye's view is being rendered (and is what HUD/crosshair shifts and
// the off-axis frustum should follow). When "Swap Eyes" is on, this is the
// opposite of the placement eye that SetStereoMode used to pick the
// viewport / color mask / stencil region.
//
// current_placement_eye holds that placement eye separately so anything
// that needs to re-apply the per-eye GL state mid-pass (e.g. the
// HWR_ClearView / HWR_RenderPlayerView re-applies in d_main.c and
// hw_main.c after a GClipRect) can do so against the original viewport
// region rather than the swapped perspective.
static SINT8    current_eye           = STEREO_EYE_MONO;
static SINT8    current_placement_eye = STEREO_EYE_MONO;
static float   current_separation    = 0.0f;
static float   current_convergence   = 1.0f;
static fixed_t cached_crosshair_dist = 0; // populated by R_UpdateStereoCrosshairTrace
static boolean drawing_crosshair_hud = false;
static boolean backbuffer_is_stereo  = false;

// Legacy-config migration state. stereo_vars_registered gates the OnChange
// handlers so the register-time default assignment doesn't look like a user
// edit; legacy_ipd holds a pre-clip-space "stereoipd" seen in config.cfg
// until the conversion can run (it needs stereofoclen, which config.cfg may
// not have applied yet at that point), and sep_explicitly_set records that
// the config already carried a stereosep, in which case there is nothing to
// migrate and the legacy value is dropped.
static boolean stereo_vars_registered = false;
static boolean sep_explicitly_set     = false;
static INT32   legacy_ipd             = 0;

static void Stereo_OnChange(void)
{
	// Preserve the user's preference even if the renderer is currently
	// software — R_StereoActive() handles the runtime gate silently. We only
	// warn when both the active renderer AND the saved cv_renderer preference
	// are software, so a config.cfg load that sets cv_renderer=OpenGL doesn't
	// fire a false alarm on the line that processes cv_stereomode (the order
	// of CVAR processing in config.cfg isn't guaranteed).
	if (cv_stereomode.value != STEREO_OFF
		&& rendermode != render_opengl
		&& cv_renderer.value != 2 /* OpenGL */)
	{
		CONS_Alert(CONS_WARNING, "Stereoscopic 3D requires the OpenGL renderer; setting will take effect once OpenGL is selected.\n");
	}
}

static void Stereo_SepOnChange(void)
{
	// Any assignment after registration -- console, menu, or a config.cfg
	// line -- means this profile is already on the clip-space knob, so a
	// legacy stereoipd sitting in the same file is stale and must not
	// overwrite it. CV_NOINIT keeps the register-time default from
	// tripping this.
	if (stereo_vars_registered)
	{
		sep_explicitly_set = true;
		legacy_ipd         = 0;
	}
}

static void Stereo_LegacyIPDOnChange(void)
{
	// Just record it. The conversion divides by the convergence distance,
	// and config.cfg writes CVARs in registration order, so stereofoclen
	// may not have been applied yet when this fires. Resolve lazily
	// instead, once everything is loaded.
	if (stereo_vars_registered && !sep_explicitly_set)
		legacy_ipd = cv_stereoipd.value;
}

// One-shot migration from the pre-clip-space (eye-separation-in-world-units)
// parameterization. The two forms are algebraically identical, so this is an
// exact conversion rather than an approximation -- it reproduces the user's
// existing image pixel-for-pixel at their saved convergence:
//
//   separation = ipd / (2 * convergence * tan(reference_fov / 2))
//
// The old form had no reference-FoV slider (it read the live cv_fov), so the
// FoV the user tuned at is best approximated by their current cv_fov.
static void Stereo_ResolveLegacyIPD(void)
{
	float ipd, tan_half, sep;
	INT32 slider;

	if (legacy_ipd <= 0)
		return;

	ipd      = legacy_ipd * 0.1f;   // old slider units were *0.1 world units
	tan_half = (float)tan((double)((FIXED_TO_FLOAT(cv_fov.value) * (float)M_PIl) / 360.0f));

	// One shot, whatever happens below. Zeroing the CVAR too means the
	// converted value is never re-applied on a later run: CV_StealthSetValue
	// skips the CV_CALL handler, so this doesn't re-enter.
	legacy_ipd = 0;
	CV_StealthSetValue(&cv_stereoipd, 0);

	if (tan_half <= 0.0f || cv_stereofoclen.value <= 0)
		return;

	sep = ipd * 0.5f / ((float)cv_stereofoclen.value * tan_half);

	slider = (INT32)(sep * 1000.0f + 0.5f);
	if (slider < 0)
		slider = 0;

	CV_StealthSetValue(&cv_stereosep, slider);
	sep_explicitly_set = true;

	CONS_Printf("Stereoscopic 3D: converted legacy stereoipd %.1f (at %d wu convergence) to stereosep %.3f.\n",
		ipd, cv_stereofoclen.value, slider / 1000.0f);
}

void R_RegisterStereoVars(void)
{
	CV_RegisterVar(&cv_stereomode);
	CV_RegisterVar(&cv_stereosep);
	CV_RegisterVar(&cv_stereofoclen);
	CV_RegisterVar(&cv_stereoswap);
	CV_RegisterVar(&cv_stereohuddepth);
	CV_RegisterVar(&cv_stereocrosshairdepth);
	CV_RegisterVar(&cv_stereoghostcontrast);
	CV_RegisterVar(&cv_stereoghostlift);
	CV_RegisterVar(&cv_stereoipd);   // legacy, migration only

	stereo_vars_registered = true;
}

boolean R_StereoActive(void)
{
	// Cheapest reliable place to land the one-shot legacy-config migration:
	// this runs every frame once the renderer is up, by which point every
	// CVAR config.cfg carries has been applied, and it still runs when the
	// user never enables stereo at all (so their old stereoipd is converted
	// before the config gets rewritten without it). Guarded by a single
	// compare in the common case.
	Stereo_ResolveLegacyIPD();

	return (cv_stereomode.value != STEREO_OFF) && (rendermode == render_opengl);
}

stereomode_t R_StereoMode(void)
{
	if (!R_StereoActive())
		return STEREO_OFF;

	// Row-Interlaced: render TaB internally and composite to row-interleaved
	// at display resolution in OglSdlFinishUpdate via a fragment shader.
	// TaB gives each eye full horizontal resolution and half vertical
	// resolution, matching a row-interleaved display's per-eye row count
	// when render_height == display_height — rendered pixels map cleanly
	// to display eye-rows.
	//
	// Splitscreen also works through the same path: the TaB+splitscreen
	// layout (P1L/P2L/P1R/P2R top-to-bottom stripes) already puts both
	// players' L views in the top half of the source texture and both R
	// views in the bottom half, which is exactly what the eye-0/eye-1
	// composite samples — each player's interlaced view ends up in their
	// own half of the display.
	if (cv_stereomode.value == STEREO_ROW_INTERLACED)
		return STEREO_TAB;

	// Column-Interlaced and Checkerboard render SbS internally and
	// composite at display resolution via fragment shaders that pick
	// per-pixel which eye's half to sample (by column parity or
	// (col+row) parity respectively). Same SbS-internal layout as LeiaSR
	// — preserves the per-eye horizontal resolution that matters for
	// column-based displays.
	//
	// Anaglyph also renders SbS internally now: the Dubois optimization
	// requires both eye color values per output pixel, which we get by
	// reading from each half of the SbS source in a composite shader.
	// The old color-mask path (left writes R, right writes GB) can't
	// supply this — each eye only touched its own channels, so the
	// negative cross-eye coefficients in the Dubois matrix had no input
	// data to act on.
	if (cv_stereomode.value == STEREO_COLUMN_INTERLACED
		|| cv_stereomode.value == STEREO_CHECKERBOARD
		|| cv_stereomode.value == STEREO_ANAGLYPH)
		return STEREO_SBS;

	return (stereomode_t)cv_stereomode.value;
}

int R_StereoNumEyes(void)
{
	return R_StereoActive() ? 2 : 1;
}

SINT8 R_StereoEyeForPass(int pass)
{
	if (!R_StereoActive())
		return STEREO_EYE_MONO;

	// Always pass 0 = LEFT placement, pass 1 = RIGHT placement. The "Swap
	// Eyes" CVAR is applied inside R_BeginStereoEye by inverting the shear
	// sign (perspective) without moving the placement — so the physical-
	// screen region chosen by SetStereoMode (left half / red channel / even
	// rows) ends up showing the OPPOSITE eye's view. Inverting both at the
	// same time (the previous behavior) cancelled out and made the toggle a
	// no-op for SbS/TaB/Anaglyph.
	return (pass == 0) ? STEREO_EYE_LEFT : STEREO_EYE_RIGHT;
}

void R_BeginStereoEye(SINT8 eye)
{
	// "Swap Eyes": the placement eye stays as passed in (so SetStereoMode
	// already routed us to the correct half / channel / row). We only flip
	// which perspective gets rendered into that placement, by inverting the
	// effective eye used to derive the shear sign and the HUD-shift state.
	SINT8 perspective_eye = eye;
	if (cv_stereoswap.value && eye != STEREO_EYE_MONO)
		perspective_eye = (eye == STEREO_EYE_LEFT) ? STEREO_EYE_RIGHT : STEREO_EYE_LEFT;

	current_eye           = perspective_eye;
	current_placement_eye = eye;
	// stereofoclen slider value is already in world units (*1.0).
	current_convergence = (cv_stereofoclen.value > 0) ? (float)cv_stereofoclen.value : 1.0f;

	if (perspective_eye == STEREO_EYE_MONO)
	{
		current_separation = 0.0f;
	}
	else
	{
		// stereosep slider value * 0.001 = clip-space separation. Signed by
		// the shear direction, which for this renderer's glFrustum-form
		// matrix is +1 for the left eye (see R_StereoEyeShearDir).
		const float sep = cv_stereosep.value * 0.001f;
		current_separation = R_StereoEyeShearDir(perspective_eye) * sep;
	}
}

void R_EndStereoEye(void)
{
	current_eye           = STEREO_EYE_MONO;
	current_placement_eye = STEREO_EYE_MONO;
	current_separation    = 0.0f;
	current_convergence   = 1.0f;
}

SINT8 R_GetCurrentEye(void)
{
	return current_eye;
}

SINT8 R_GetCurrentPlacementEye(void)
{
	return current_placement_eye;
}

// The per-eye sign of the projection shear.
//
// This is NOT guessed from "left is negative" folklore -- it is read off this
// renderer's own frustum math. GLPerspectiveStereo builds a glFrustum-form
// matrix and stores the shear in m[2][0]; for a point at depth z the resulting
// NDC x offset works out to eye_sign * separation * (1 - convergence/z), i.e.
// NDC picks up -m[2][0]. Sustaining the existing (correct) image therefore
// needs m[2][0] positive for the left eye. Cross-check: at infinity the left
// eye lands at -separation, which is uncrossed disparity -- background behind
// the screen -- and nearer than convergence the term flips to crossed
// disparity, which pops out. Both are what you want.
//
// Note that this is the SHEAR convention, not a compositing one. The output
// layout's notion of "which half is the left eye" lives in
// R_StereoComputePlayerEyeRect and is a genuinely separate question; the two
// are not required to agree and here they do not.
SINT8 R_StereoEyeShearDir(SINT8 perspective_eye)
{
	if (perspective_eye == STEREO_EYE_MONO)
		return 0;
	return (perspective_eye == STEREO_EYE_LEFT) ? (SINT8)+1 : (SINT8)-1;
}

float R_GetStereoSeparation(void)
{
	return current_separation;
}

float R_GetStereoConvergence(void)
{
	return current_convergence;
}

float R_GetStereoGhostContrast(void)
{
	return cv_stereoghostcontrast.value * 0.01f;
}

float R_GetStereoGhostLift(void)
{
	return cv_stereoghostlift.value * 0.001f;
}

boolean R_StereoGhostReduceActive(void)
{
	return (cv_stereoghostcontrast.value != 100) || (cv_stereoghostlift.value != 0);
}

// Per-eye screen-pixel shift for an overlay sitting at a given depth,
// expressed as a fraction of the screen width by the caller.
//
// The clip-space form of the shift is
//
//     shift_px(z) = dir * separation * (convergence/z - 1) * eye_w/2
//
// but the HUD and crosshair CVARs don't store a depth in world units -- they
// store a "depth fraction" d = 1 - convergence/z, which is 0 at the screen
// plane, negative in front of it and positive behind. Substituting:
//
//     shift_px(d) = -dir * separation * d * eye_w/2
//                 = eye_sign * separation * d * eye_w/2
//
// so convergence cancels out completely and the shift depends only on the
// separation and the depth fraction. That is a real property of the clip-space
// parameterization, not a simplification: a UI layer placed at a fixed
// multiple of the convergence distance has a convergence-invariant disparity.
//
// What is gone versus the old form: the P00 / tan(fov/2) term and the world-
// unit conversion. Their absence is the fix for a latent disagreement between
// this path and the frustum -- this helper used to read the global cv_fov
// while the projection used the possibly-fudged per-pass FoV (splitscreen
// rescales it), so the HUD sat at a different depth than the same depth
// fraction implied for world geometry.
//
// eye_w is vid.width because hw_draw.c converts the result back to NDC by
// dividing by vid.width again; the two cancel, so the absolute width used
// here only has to be self-consistent.
//
// SIGN FIX, and the reason a saved HUD/crosshair depth now reads the other
// way round: the old form negated current_eye here, so the overlay shift ran
// OPPOSITE to world geometry at the same depth -- a negative depth fraction
// pushed the HUD behind the screen instead of in front of it, contradicting
// both CVARs' documented meaning and their defaults. That is the classic
// two-independent-sign-conventions trap: the frustum shear and the screen-
// space overlay shift are separate code paths answering different questions,
// and they had been verified separately only for the frustum. They now share
// one convention, derived above from the frustum itself. The magnitude is
// unchanged, so the fix is a pure sign flip: the defaults and slider ranges
// above were mirrored to match, which keeps the shipped picture identical
// and leaves only the number in the menu reading the other way round. A saved
// config from before the fix needs the same treatment -- negate it.
static float Stereo_OverlayShiftPixels(float depth_frac)
{
	const float sep = cv_stereosep.value * 0.001f;

	if (current_eye == STEREO_EYE_MONO)
		return 0.0f;

	return current_eye * depth_frac * sep * ((float)vid.width * 0.5f);
}

// Compute the flat chrome-HUD shift (depth-fraction-based). Doesn't consult
// drawing_crosshair_hud, so it's safe to call as a recursion-free fallback
// from R_GetStereoCrosshairShift.
static INT32 R_GetStereoChromeHUDShift_Raw(void)
{
	if (!R_StereoActive() || current_eye == STEREO_EYE_MONO)
		return 0;
	if (cv_stereohuddepth.value == 0 || cv_stereosep.value == 0)
		return 0;

	// depth_frac = 1 - convergence/z: 0 is the screen plane, positive recedes
	// behind it (uncrossed disparity), negative pops out in front of it.
	return (INT32)Stereo_OverlayShiftPixels(cv_stereohuddepth.value / 100.0f);
}

INT32 R_GetStereoHUDShift(void)
{
	if (!R_StereoActive() || current_eye == STEREO_EYE_MONO)
		return 0;

	// When the crosshair is being drawn, route to the dynamic-depth shift
	// so the crosshair sits at the world-hit depth instead of the flat
	// chrome-HUD depth.
	if (drawing_crosshair_hud)
		return R_GetStereoCrosshairShift();

	return R_GetStereoChromeHUDShift_Raw();
}

INT32 R_GetStereoCrosshairShift(void)
{
	if (!R_StereoActive() || current_eye == STEREO_EYE_MONO)
		return 0;
	if (cv_stereocrosshairdepth.value == 0 || cv_stereosep.value == 0)
		return 0;

	// Same formula as the chrome HUD, driven by the separate crosshair-depth
	// CVAR so the crosshair can sit at a different 3D plane than the chrome
	// (e.g. crosshair out at the aim depth, chrome near the screen plane).
	return (INT32)Stereo_OverlayShiftPixels(cv_stereocrosshairdepth.value / 100.0f);
}

void R_UpdateStereoCrosshairTrace(player_t *player)
{
	(void)player;
	// No-op: the dynamic raycast was replaced by the static
	// cv_stereocrosshairdepth CVAR. Kept as a stub so the call site in
	// d_main.c doesn't need conditional compilation.
	cached_crosshair_dist = 0;
}

boolean R_BackbufferIsStereo(void)
{
	return backbuffer_is_stereo;
}

void R_SetBackbufferIsStereo(boolean is_stereo)
{
	backbuffer_is_stereo = is_stereo;
}

void R_BeginCrosshairHUDDraw(void)
{
	drawing_crosshair_hud = true;
}

void R_EndCrosshairHUDDraw(void)
{
	drawing_crosshair_hud = false;
}

void R_StereoComputePlayerEyeRect(stereomode_t mode, SINT8 eye, int player_idx,
                                  INT32 *x, INT32 *y, INT32 *w, INT32 *h)
{
	const INT32 vw         = vid.width;
	const INT32 vh         = vid.height;
	const INT32 vw_half    = vw / 2;
	const INT32 vh_half    = vh / 2;
	const INT32 vh_quarter = vh / 4;
	// player_idx == -1 forces single-player layout (eye half of full
	// screen), regardless of splitscreen state. Used for the top-of-eye-
	// loop overlay setup and the post-render HUD pass — both want the
	// HUD/menu/overlays to span both players' regions in each eye half.
	const boolean is_split = (player_idx >= 0) && splitscreen;
	const boolean is_left  = (eye < 0);
	// In SRB2 conventions, displayplayer (P1) renders into the upper half of
	// the screen — which is the upper viewport rectangle in GL coords (Y is
	// bottom-up). So for splitscreen, P1's vertical region is the upper one.
	const boolean is_p1    = (!is_split || player_idx == 0);

	// LeiaSR shares its viewport layout with SbS — the SR weaver is fed the
	// SbS-composited backbuffer.
	if (mode == STEREO_LEIASR)
		mode = STEREO_SBS;

	switch (mode)
	{
		case STEREO_SBS:
		{
			// Left col (x=0) for L eye, right col (x=vw/2) for R eye.
			*x = is_left ? 0 : vw_half;
			*w = vw_half;
			if (is_split)
			{
				// Top row (GL y=vh/2) for P1, bottom row (GL y=0) for P2.
				*y = is_p1 ? vh_half : 0;
				*h = vh_half;
			}
			else
			{
				*y = 0;
				*h = vh;
			}
			break;
		}
		case STEREO_TAB:
		{
			*x = 0;
			*w = vw;
			if (is_split)
			{
				// 4 horizontal stripes:
				//   GL y=3*vh/4 (top quarter visually): P1 L
				//   GL y=  vh/2 (second quarter):       P2 L
				//   GL y=  vh/4 (third quarter):        P1 R
				//   GL y=     0 (bottom quarter):       P2 R
				if (is_left)
					*y = is_p1 ? (3 * vh_quarter) : vh_half;
				else
					*y = is_p1 ? vh_quarter       : 0;
				*h = vh_quarter;
			}
			else
			{
				// Top half (GL y=vh/2) = L eye, bottom half (GL y=0) = R eye.
				*y = is_left ? vh_half : 0;
				*h = vh_half;
			}
			break;
		}
		case STEREO_ANAGLYPH:
		case STEREO_ROW_INTERLACED:
		case STEREO_COLUMN_INTERLACED:
		case STEREO_CHECKERBOARD:
		{
			// Defensive fallback only — R_StereoMode() substitutes all four
			// of these to STEREO_TAB (Row-Interlaced) or STEREO_SBS (Anaglyph
			// Dubois / Column-Interlaced / Checkerboard) before reaching
			// this function in normal flow, so the SbS/TaB cases above are
			// what actually drives per-eye viewports. Eye separation for
			// these modes happens at present time via composite shaders.
			*x = 0;
			*w = vw;
			if (is_split)
			{
				*y = is_p1 ? vh_half : 0;
				*h = vh_half;
			}
			else
			{
				*y = 0;
				*h = vh;
			}
			break;
		}
		default: // STEREO_OFF
			*x = 0;
			*y = 0;
			*w = vw;
			*h = vh;
			break;
	}
}

void R_DrawAcrossStereoEyes(void (*drawfn)(void))
{
	if (drawfn == NULL)
		return;

#ifdef HWRENDER
	if (R_StereoActive())
	{
		const int npasses = R_StereoNumEyes();
		const stereomode_t mode = R_StereoMode();
		int p;
		// Transient screens (loading, title card, quit) span the whole
		// window, so pass player_idx = -1 to force the single-player
		// layout (eye half of full screen) regardless of whether
		// splitscreen happens to be configured at the time.
		for (p = 0; p < npasses; p++)
		{
			const SINT8 eye = R_StereoEyeForPass(p);
			INT32 rx, ry, rw, rh;
			R_StereoComputePlayerEyeRect(mode, eye, -1, &rx, &ry, &rw, &rh);
			HWR_SetStereoMode((INT32)mode, eye, rx, ry, rw, rh);
			R_BeginStereoEye(eye);
			drawfn();
			R_EndStereoEye();
		}
		HWR_ResetStereoMode();
		R_SetBackbufferIsStereo(true);
		return;
	}
#endif

	drawfn();
	R_SetBackbufferIsStereo(false);
}
