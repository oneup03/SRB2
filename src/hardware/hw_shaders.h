// SONIC ROBO BLAST 2
//-----------------------------------------------------------------------------
// Copyright (C) 2021-2024 by Sonic Team Junior.
//
// This program is free software distributed under the
// terms of the GNU General Public License, version 2.
// See the 'LICENSE' file for more details.
//-----------------------------------------------------------------------------
/// \file hw_shaders.h
/// \brief Handles the shaders used by the game.

#ifndef _HW_SHADERS_H_
#define _HW_SHADERS_H_

#include "../doomtype.h"

// ================
//  Vertex shaders
// ================

//
// Generic vertex shader
//

#define GLSL_DEFAULT_VERTEX_SHADER \
	"void main()\n" \
	"{\n" \
		"gl_Position = gl_ProjectionMatrix * gl_ModelViewMatrix * gl_Vertex;\n" \
		"gl_FrontColor = gl_Color;\n" \
		"gl_TexCoord[0].xy = gl_MultiTexCoord0.xy;\n" \
		"gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;\n" \
	"}\0"

// replicates the way fixed function lighting is used by the model lighting option,
// stores the lighting result to gl_Color
// (ambient lighting of 0.75 and diffuse lighting from above)
#define GLSL_MODEL_VERTEX_SHADER \
	"void main()\n" \
	"{\n" \
		"#ifdef SRB2_MODEL_LIGHTING\n" \
		"float nDotVP = dot(gl_Normal, vec3(0, 1, 0));\n" \
		"float light = min(0.75 + max(nDotVP, 0.0), 1.0);\n" \
		"gl_FrontColor = vec4(light, light, light, 1.0);\n" \
		"#else\n" \
		"gl_FrontColor = gl_Color;\n" \
		"#endif\n" \
		"gl_Position = gl_ProjectionMatrix * gl_ModelViewMatrix * gl_Vertex;\n" \
		"gl_TexCoord[0].xy = gl_MultiTexCoord0.xy;\n" \
		"gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;\n" \
	"}\0"

// ==================
//  Fragment shaders
// ==================

//
// Generic fragment shader
//

#define GLSL_DEFAULT_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	"uniform vec4 poly_color;\n" \
	"void main(void) {\n" \
		"gl_FragColor = texture2D(tex, gl_TexCoord[0].st) * poly_color;\n" \
	"}\0"

//
// Software fragment shader
//

// Include GLSL_FLOOR_FUDGES or GLSL_WALL_FUDGES or define the fudges in shaders that use this macro.
#define GLSL_DOOM_COLORMAP \
	"float R_DoomColormap(float light, float z)\n" \
	"{\n" \
		"float lightnum = clamp(light / 17.0, 0.0, 15.0);\n" \
		"float lightz = clamp(z / 16.0, 0.0, 127.0);\n" \
		"float startmap = (15.0 - lightnum) * 4.0;\n" \
		"float scale = 160.0 / (lightz + 1.0);\n" \
		"float cap = (155.0 - light) * 0.26;\n" \
		"return max(startmap * STARTMAP_FUDGE - scale * 0.5 * SCALE_FUDGE, cap);\n" \
	"}\n"
// lighting cap adjustment:
// first num (155.0), increase to make it start to go dark sooner
// second num (0.26), increase to make it go dark faster

#define GLSL_DOOM_LIGHT_EQUATION \
	"float R_DoomLightingEquation(float light)\n" \
	"{\n" \
		"float z = gl_FragCoord.z / gl_FragCoord.w;\n" \
		"float colormap = floor(R_DoomColormap(light, z)) + 0.5;\n" \
		"return clamp(colormap, 0.0, 31.0) / 32.0;\n" \
	"}\n"

#define GLSL_SOFTWARE_TINT_EQUATION \
	"if (tint_color.a > 0.0) {\n" \
		"float color_bright = sqrt((base_color.r * base_color.r) + (base_color.g * base_color.g) + (base_color.b * base_color.b));\n" \
		"float strength = sqrt(tint_color.a);\n" \
		"final_color.r = clamp((color_bright * (tint_color.r * strength)) + (base_color.r * (1.0 - strength)), 0.0, 1.0);\n" \
		"final_color.g = clamp((color_bright * (tint_color.g * strength)) + (base_color.g * (1.0 - strength)), 0.0, 1.0);\n" \
		"final_color.b = clamp((color_bright * (tint_color.b * strength)) + (base_color.b * (1.0 - strength)), 0.0, 1.0);\n" \
	"}\n"

#define GLSL_SOFTWARE_FADE_EQUATION \
	"float darkness = R_DoomLightingEquation(lighting);\n" \
	"if (fade_start != 0.0 || fade_end != 31.0) {\n" \
		"float fs = fade_start / 31.0;\n" \
		"float fe = fade_end / 31.0;\n" \
		"float fd = fe - fs;\n" \
		"darkness = clamp((darkness - fs) * (1.0 / fd), 0.0, 1.0);\n" \
	"}\n" \
	"final_color = mix(final_color, fade_color, darkness);\n"

#define GLSL_PALETTE_RENDERING \
	"float tex_pal_idx = texture3D(palette_lookup_tex, vec3((texel * 63.0 + 0.5) / 64.0))[0] * 255.0;\n" \
	"float z = gl_FragCoord.z / gl_FragCoord.w;\n" \
	"float light_y = clamp(floor(R_DoomColormap(lighting, z)), 0.0, 31.0);\n" \
	"vec2 lighttable_coord = vec2((tex_pal_idx + 0.5) / 256.0, (light_y + 0.5) / 32.0);\n" \
	"vec4 final_color = texture2D(lighttable_tex, lighttable_coord);\n" \
	"final_color.a = texel.a * poly_color.a;\n" \
	"gl_FragColor = final_color;\n" \

#define GLSL_SOFTWARE_FRAGMENT_SHADER \
	"#ifdef SRB2_PALETTE_RENDERING\n" \
	"uniform sampler2D tex;\n" \
	"uniform sampler3D palette_lookup_tex;\n" \
	"uniform sampler2D lighttable_tex;\n" \
	"uniform vec4 poly_color;\n" \
	"uniform float lighting;\n" \
	GLSL_DOOM_COLORMAP \
	"void main(void) {\n" \
		"vec4 texel = texture2D(tex, gl_TexCoord[0].st);\n" \
		GLSL_PALETTE_RENDERING \
	"}\n" \
	"#else\n" \
	"uniform sampler2D tex;\n" \
	"uniform vec4 poly_color;\n" \
	"uniform vec4 tint_color;\n" \
	"uniform vec4 fade_color;\n" \
	"uniform float lighting;\n" \
	"uniform float fade_start;\n" \
	"uniform float fade_end;\n" \
	GLSL_DOOM_COLORMAP \
	GLSL_DOOM_LIGHT_EQUATION \
	"void main(void) {\n" \
		"vec4 texel = texture2D(tex, gl_TexCoord[0].st);\n" \
		"vec4 base_color = texel * poly_color;\n" \
		"vec4 final_color = base_color;\n" \
		GLSL_SOFTWARE_TINT_EQUATION \
		GLSL_SOFTWARE_FADE_EQUATION \
		"final_color.a = texel.a * poly_color.a;\n" \
		"gl_FragColor = final_color;\n" \
	"}\n" \
	"#endif\0"

// hand tuned adjustments for light level calculation
#define GLSL_FLOOR_FUDGES \
	"#define STARTMAP_FUDGE 1.06\n" \
	"#define SCALE_FUDGE 1.15\n"

#define GLSL_WALL_FUDGES \
	"#define STARTMAP_FUDGE 1.05\n" \
	"#define SCALE_FUDGE 2.2\n"

#define GLSL_FLOOR_FRAGMENT_SHADER \
	GLSL_FLOOR_FUDGES \
	GLSL_SOFTWARE_FRAGMENT_SHADER

#define GLSL_WALL_FRAGMENT_SHADER \
	GLSL_WALL_FUDGES \
	GLSL_SOFTWARE_FRAGMENT_SHADER

// same as above but multiplies results with the lighting value from the
// accompanying vertex shader (stored in gl_Color) if model lighting is enabled
#define GLSL_MODEL_FRAGMENT_SHADER \
	GLSL_WALL_FUDGES \
	"#ifdef SRB2_PALETTE_RENDERING\n" \
	"uniform sampler2D tex;\n" \
	"uniform sampler3D palette_lookup_tex;\n" \
	"uniform sampler2D lighttable_tex;\n" \
	"uniform vec4 poly_color;\n" \
	"uniform float lighting;\n" \
	GLSL_DOOM_COLORMAP \
	"void main(void) {\n" \
		"vec4 texel = texture2D(tex, gl_TexCoord[0].st);\n" \
		"#ifdef SRB2_MODEL_LIGHTING\n" \
		"texel *= gl_Color;\n" \
		"#endif\n" \
		GLSL_PALETTE_RENDERING \
	"}\n" \
	"#else\n" \
	"uniform sampler2D tex;\n" \
	"uniform vec4 poly_color;\n" \
	"uniform vec4 tint_color;\n" \
	"uniform vec4 fade_color;\n" \
	"uniform float lighting;\n" \
	"uniform float fade_start;\n" \
	"uniform float fade_end;\n" \
	GLSL_DOOM_COLORMAP \
	GLSL_DOOM_LIGHT_EQUATION \
	"void main(void) {\n" \
		"vec4 texel = texture2D(tex, gl_TexCoord[0].st);\n" \
		"vec4 base_color = texel * poly_color;\n" \
		"vec4 final_color = base_color;\n" \
		GLSL_SOFTWARE_TINT_EQUATION \
		GLSL_SOFTWARE_FADE_EQUATION \
		"#ifdef SRB2_MODEL_LIGHTING\n" \
		"final_color *= gl_Color;\n" \
		"#endif\n" \
		"final_color.a = texel.a * poly_color.a;\n" \
		"gl_FragColor = final_color;\n" \
	"}\n" \
	"#endif\0"

//
// Water surface shader
//
// Mostly guesstimated, rather than the rest being built off Software science.
// Still needs to distort things underneath/around the water...
//

#define GLSL_WATER_TEXEL \
	"float water_z = (gl_FragCoord.z / gl_FragCoord.w) / 2.0;\n" \
	"float a = -pi * (water_z * freq) + (leveltime * speed);\n" \
	"float sdistort = sin(a) * amp;\n" \
	"float cdistort = cos(a) * amp;\n" \
	"vec4 texel = texture2D(tex, vec2(gl_TexCoord[0].s - sdistort, gl_TexCoord[0].t - cdistort));\n"

#define GLSL_WATER_FRAGMENT_SHADER \
	GLSL_FLOOR_FUDGES \
	"const float freq = 0.025;\n" \
	"const float amp = 0.025;\n" \
	"const float speed = 2.0;\n" \
	"const float pi = 3.14159;\n" \
	"#ifdef SRB2_PALETTE_RENDERING\n" \
	"uniform sampler2D tex;\n" \
	"uniform sampler3D palette_lookup_tex;\n" \
	"uniform sampler2D lighttable_tex;\n" \
	"uniform vec4 poly_color;\n" \
	"uniform float lighting;\n" \
	"uniform float leveltime;\n" \
	GLSL_DOOM_COLORMAP \
	"void main(void) {\n" \
		GLSL_WATER_TEXEL \
		GLSL_PALETTE_RENDERING \
	"}\n" \
	"#else\n" \
	"uniform sampler2D tex;\n" \
	"uniform vec4 poly_color;\n" \
	"uniform vec4 tint_color;\n" \
	"uniform vec4 fade_color;\n" \
	"uniform float lighting;\n" \
	"uniform float fade_start;\n" \
	"uniform float fade_end;\n" \
	"uniform float leveltime;\n" \
	GLSL_DOOM_COLORMAP \
	GLSL_DOOM_LIGHT_EQUATION \
	"void main(void) {\n" \
		GLSL_WATER_TEXEL \
		"vec4 base_color = texel * poly_color;\n" \
		"vec4 final_color = base_color;\n" \
		GLSL_SOFTWARE_TINT_EQUATION \
		GLSL_SOFTWARE_FADE_EQUATION \
		"final_color.a = texel.a * poly_color.a;\n" \
		"gl_FragColor = final_color;\n" \
	"}\n" \
	"#endif\0"

//
// Fog block shader
//
// Alpha of the planes themselves are still slightly off -- see HWR_FogBlockAlpha
//

// The floor fudges are used, but should the wall fudges be used instead? or something inbetween?
// or separate values for floors and walls? (need to change more than this shader for that)
#define GLSL_FOG_FRAGMENT_SHADER \
	GLSL_FLOOR_FUDGES \
	"uniform vec4 tint_color;\n" \
	"uniform vec4 fade_color;\n" \
	"uniform float lighting;\n" \
	"uniform float fade_start;\n" \
	"uniform float fade_end;\n" \
	GLSL_DOOM_COLORMAP \
	GLSL_DOOM_LIGHT_EQUATION \
	"void main(void) {\n" \
		"vec4 base_color = gl_Color;\n" \
		"vec4 final_color = base_color;\n" \
		GLSL_SOFTWARE_TINT_EQUATION \
		GLSL_SOFTWARE_FADE_EQUATION \
		"gl_FragColor = final_color;\n" \
	"}\0"

//
// Sky fragment shader
// Modulates poly_color with gl_Color
//
#define GLSL_SKY_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	"uniform vec4 poly_color;\n" \
	"void main(void) {\n" \
		"gl_FragColor = texture2D(tex, gl_TexCoord[0].st) * gl_Color * poly_color;\n" \
	"}\0"

// Shader for the palette rendering postprocess step
#define GLSL_PALETTE_POSTPROCESS_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	"uniform sampler3D palette_lookup_tex;\n" \
	"uniform sampler1D palette_tex;\n" \
	"void main(void) {\n" \
		"vec4 texel = texture2D(tex, gl_TexCoord[0].st);\n" \
		"float tex_pal_idx = texture3D(palette_lookup_tex, vec3((texel * 63.0 + 0.5) / 64.0))[0] * 255.0;\n" \
		"float palette_coord = (tex_pal_idx + 0.5) / 256.0;\n" \
		"vec4 final_color = texture1D(palette_tex, palette_coord);\n" \
		"gl_FragColor = final_color;\n" \
	"}\0"

// Applies a palettized colormap fade to tex
#define GLSL_UI_COLORMAP_FADE_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	"uniform float lighting;\n" \
	"uniform sampler3D palette_lookup_tex;\n" \
	"uniform sampler2D lighttable_tex;\n" \
	"void main(void) {\n" \
		"vec4 texel = texture2D(tex, gl_TexCoord[0].st);\n" \
		"float tex_pal_idx = texture3D(palette_lookup_tex, vec3((texel * 63.0 + 0.5) / 64.0))[0] * 255.0;\n" \
		"vec2 lighttable_coord = vec2((tex_pal_idx + 0.5) / 256.0, (lighting + 0.5) / 32.0);\n" \
		"gl_FragColor = texture2D(lighttable_tex, lighttable_coord);\n" \
	"}\0"

// For wipes that use additive and subtractive blending.
// alpha_factor = 31 * 8 / 10 = 24.8
// Calculated based on the use of the "fade" variable from the GETCOLOR macro
// in r_data.c:R_CreateFadeColormaps.
// However this value created some ugliness in fades to white (special stage entry)
// while palette rendering is enabled, so I raised the value just a bit.
#define GLSL_UI_TINTED_WIPE_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	"uniform vec4 poly_color;\n" \
	"const float alpha_factor = 24.875;\n" \
	"void main(void) {\n" \
		"vec4 texel = texture2D(tex, gl_TexCoord[0].st);\n" \
		"vec4 final_color = poly_color;\n" \
		"float alpha = texel.a;\n" \
		"if (final_color.a >= 0.5)\n" \
			"alpha = 1.0 - alpha;\n" \
		"alpha *= alpha_factor;\n" \
		"final_color *= alpha;\n" \
		"final_color.a = 1.0;\n" \
		"gl_FragColor = final_color;\n" \
	"}\0"

//
// Ghost / crosstalk reduction (shared by every stereo composite shader).
//
// Every stereo display leaks some of each eye's image into the other. How
// visible that leak is depends on the BRIGHTNESS DIFFERENCE between the eyes,
// so compressing the signal range before it reaches the display reduces what
// you actually see -- the standard range-compression approach from the stereo
// crosstalk literature. Two levers, both global, both exact no-ops at their
// defaults:
//
//   stereo_ghost_contrast (1.0 = off) squeezes everything toward mid-grey.
//     That shrinks |L - R| directly and leaves (1 - contrast)/2 of headroom at
//     BOTH ends of the range. Costs contrast across the whole image. 0.90 is a
//     good first try; go lower only if edges still ghost.
//
//   stereo_ghost_lift (0.0 = off) raises the black floor and leaves white
//     alone. This one is for displays that actively CANCEL crosstalk by
//     pre-subtracting a fraction of the opposite eye -- LeiaSR's weaver does.
//     That subtraction drives dark pixels below zero, the render target clamps
//     them, and the clipped part is exactly what survives as a visible ghost.
//     Lift buys the "foot-room" the cancellation needs, targeting the end that
//     actually clips instead of spending most of its effect on highlights.
//     0.02-0.05 is the useful band; blacks go grey fast above that. On a
//     display that does NOT cancel there is no clipping to relieve, and only
//     contrast helps.
//
// The remap must run in the space the display's cancellation runs in.
// Cancelling displays generally work in linear light, so this pivots around
// 0.5 in linear via a plain 2.2 gamma rather than the piecewise sRGB curve --
// getting that backwards makes ghosting worse, not better.
//
// Deliberately NOT adaptive. Localizing it spatially cannot work: ghosting IS
// inter-eye difference, so a correction applied unevenly manufactures more of
// it and leaks in turn. Deriving a global scalar per frame is structurally
// sound but reads as the whole image pumping brighter and darker as content
// changes; a fixed slider is less distracting than a correct but moving one.
//
// The snippet is textually pasted into each composite shader below rather
// than #included, because SRB2's shaders are string literals with no
// preprocessor of their own.
#define GLSL_STEREO_GHOST_REDUCE \
	"uniform float stereo_ghost_contrast;\n" \
	"uniform float stereo_ghost_lift;\n" \
	"vec3 ghost_reduce(vec3 c) {\n" \
		"if (stereo_ghost_contrast == 1.0 && stereo_ghost_lift == 0.0)\n" \
			"return c;\n" \
		"vec3 lin = pow(clamp(c, 0.0, 1.0), vec3(2.2));\n" \
		"lin = (lin - 0.5) * stereo_ghost_contrast + 0.5;\n" \
		"lin = lin * (1.0 - stereo_ghost_lift) + stereo_ghost_lift;\n" \
		"return pow(clamp(lin, 0.0, 1.0), vec3(1.0 / 2.2));\n" \
	"}\n"

// Composite a TaB-rendered source texture (top half = eye 0, bottom half =
// eye 1) into a row-interleaved output. For each destination row we sample
// either the top half or the bottom half of the source based on the row's
// parity — replaces the previous stencil-buffer-based composite, which
// was unreliable on some driver/display combinations.
//
// gl_FragCoord.y is in screen-space pixels (bottom-up in GL), so the
// per-row parity matches the framebuffer row index directly. The destination
// texcoord is mapped: y * 0.5 + 0.5 for the top half (eye 0), y * 0.5 for
// the bottom half (eye 1).
#define GLSL_ROW_INTERLACED_COMPOSITE_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	GLSL_STEREO_GHOST_REDUCE \
	"void main(void) {\n" \
		"vec2 uv = gl_TexCoord[0].st;\n" \
		"int row = int(gl_FragCoord.y);\n" \
		"if ((row - (row / 2) * 2) == 0)\n" \
			"uv.y = uv.y * 0.5 + 0.5;\n" \
		"else\n" \
			"uv.y = uv.y * 0.5;\n" \
		"gl_FragColor = vec4(ghost_reduce(texture2D(tex, uv).rgb), 1.0);\n" \
	"}\0"

// Composite an SbS-rendered source texture (left half = eye 0, right half =
// eye 1) into a column-interleaved output. Per-fragment column-parity picks
// which half of the source to sample. Even columns → left half (eye 0);
// odd columns → right half (eye 1).
#define GLSL_COLUMN_INTERLACED_COMPOSITE_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	GLSL_STEREO_GHOST_REDUCE \
	"void main(void) {\n" \
		"vec2 uv = gl_TexCoord[0].st;\n" \
		"int col = int(gl_FragCoord.x);\n" \
		"if ((col - (col / 2) * 2) == 0)\n" \
			"uv.x = uv.x * 0.5;\n" \
		"else\n" \
			"uv.x = uv.x * 0.5 + 0.5;\n" \
		"gl_FragColor = vec4(ghost_reduce(texture2D(tex, uv).rgb), 1.0);\n" \
	"}\0"

// Composite an SbS-rendered source texture into a checkerboard output for
// DLP-style checkerboard 3D displays. (col + row) parity picks the source
// half: even sum → left half (eye 0); odd sum → right half (eye 1).
#define GLSL_CHECKERBOARD_COMPOSITE_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	GLSL_STEREO_GHOST_REDUCE \
	"void main(void) {\n" \
		"vec2 uv = gl_TexCoord[0].st;\n" \
		"int col = int(gl_FragCoord.x);\n" \
		"int row = int(gl_FragCoord.y);\n" \
		"int sum = col + row;\n" \
		"if ((sum - (sum / 2) * 2) == 0)\n" \
			"uv.x = uv.x * 0.5;\n" \
		"else\n" \
			"uv.x = uv.x * 0.5 + 0.5;\n" \
		"gl_FragColor = vec4(ghost_reduce(texture2D(tex, uv).rgb), 1.0);\n" \
	"}\0"

// Composite an SbS-rendered source texture (left half = eye A / left eye,
// right half = eye B / right eye) into a red/cyan anaglyph using the Dubois
// optimization. The Dubois 3x6 matrix mixes both eyes' RGB values to
// minimize ghost-image (cross-talk) for standard red/cyan glasses while
// preserving more color information than the naive channel-swap.
//
// Coefficients from Eric Dubois's optimized red/cyan anaglyph projection
// (https://www.site.uottawa.ca/~edubois/anaglyph/). Each output channel
// pulls in contributions from BOTH eyes — the small negative cross-eye
// terms are the key to suppressing the bleed that makes naive anaglyphs
// look smeary and color-warped.
#define GLSL_ANAGLYPH_DUBOIS_COMPOSITE_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	GLSL_STEREO_GHOST_REDUCE \
	"void main(void) {\n" \
		"vec2 uv = gl_TexCoord[0].st;\n" \
		"vec2 uvL = vec2(uv.x * 0.5,        uv.y);\n" \
		"vec2 uvR = vec2(uv.x * 0.5 + 0.5,  uv.y);\n" \
		"vec3 cA = texture2D(tex, uvL).rgb;\n" \
		"vec3 cB = texture2D(tex, uvR).rgb;\n" \
		"float r = clamp( 0.437*cA.r + 0.449*cA.g + 0.164*cA.b\n" \
		                "-0.011*cB.r - 0.032*cB.g - 0.007*cB.b, 0.0, 1.0);\n" \
		"float g = clamp(-0.062*cA.r - 0.062*cA.g - 0.024*cA.b\n" \
		                "+0.377*cB.r + 0.761*cB.g + 0.009*cB.b, 0.0, 1.0);\n" \
		"float b = clamp(-0.048*cA.r - 0.050*cA.g - 0.017*cA.b\n" \
		                "-0.026*cB.r - 0.093*cB.g + 1.234*cB.b, 0.0, 1.0);\n" \
		"gl_FragColor = vec4(ghost_reduce(vec3(r, g, b)), 1.0);\n" \
	"}\0"

// Ghost reduction with no repacking: samples the source 1:1 and applies the
// range compression above. This is the path for the output modes that have no
// composite step of their own -- plain Side-by-Side, Top-and-Bottom, and the
// SbS intermediate handed to the LeiaSR weaver -- so those get the same two
// levers as the interlaced and anaglyph modes. It is only ever bound when at
// least one lever is off its default (see R_StereoGhostReduceActive), so the
// untouched present path keeps its original pass count.
#define GLSL_STEREO_GHOST_COMPOSITE_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	GLSL_STEREO_GHOST_REDUCE \
	"void main(void) {\n" \
		"gl_FragColor = vec4(ghost_reduce(texture2D(tex, gl_TexCoord[0].st).rgb), 1.0);\n" \
	"}\0"

//
// Generic vertex shader
//

#define GLSL_FALLBACK_VERTEX_SHADER \
	"void main()\n" \
	"{\n" \
		"gl_Position = gl_ProjectionMatrix * gl_ModelViewMatrix * gl_Vertex;\n" \
		"gl_FrontColor = gl_Color;\n" \
		"gl_TexCoord[0].xy = gl_MultiTexCoord0.xy;\n" \
		"gl_ClipVertex = gl_ModelViewMatrix * gl_Vertex;\n" \
	"}\0"

//
// Generic fragment shader
//

#define GLSL_FALLBACK_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	"uniform vec4 poly_color;\n" \
	"void main(void) {\n" \
		"gl_FragColor = texture2D(tex, gl_TexCoord[0].st) * poly_color;\n" \
	"}\0"

//
// Software fragment shader
//

#define GLSL_SOFTWARE_FADE_EQUATION \
	"float darkness = R_DoomLightingEquation(lighting);\n" \
	"if (fade_start != 0.0 || fade_end != 31.0) {\n" \
		"float fs = fade_start / 31.0;\n" \
		"float fe = fade_end / 31.0;\n" \
		"float fd = fe - fs;\n" \
		"darkness = clamp((darkness - fs) * (1.0 / fd), 0.0, 1.0);\n" \
	"}\n" \
	"final_color = mix(final_color, fade_color, darkness);\n"

// same as above but multiplies results with the lighting value from the
// accompanying vertex shader (stored in gl_Color)
#define GLSL_SOFTWARE_MODEL_LIGHTING_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	"uniform vec4 poly_color;\n" \
	"uniform vec4 tint_color;\n" \
	"uniform vec4 fade_color;\n" \
	"uniform float lighting;\n" \
	"uniform float fade_start;\n" \
	"uniform float fade_end;\n" \
	GLSL_DOOM_COLORMAP \
	GLSL_DOOM_LIGHT_EQUATION \
	"void main(void) {\n" \
		"vec4 texel = texture2D(tex, gl_TexCoord[0].st);\n" \
		"vec4 base_color = texel * poly_color;\n" \
		"vec4 final_color = base_color;\n" \
		GLSL_SOFTWARE_TINT_EQUATION \
		GLSL_SOFTWARE_FADE_EQUATION \
		"final_color *= gl_Color;\n" \
		"final_color.a = texel.a * poly_color.a;\n" \
		"gl_FragColor = final_color;\n" \
	"}\0"

//
// Sky fragment shader
// Modulates poly_color with gl_Color
//
#define GLSL_SKY_FRAGMENT_SHADER \
	"uniform sampler2D tex;\n" \
	"uniform vec4 poly_color;\n" \
	"void main(void) {\n" \
		"gl_FragColor = texture2D(tex, gl_TexCoord[0].st) * gl_Color * poly_color;\n" \
	"}\0"

#endif
