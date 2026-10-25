/*
===========================================================================
Copyright (C) 2011 Andrei Drexler, Richard Allen, James Canete

This file is part of Reaction source code.

Reaction source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Reaction source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Reaction source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

#include "tr_local.h"

void RB_ToneMap(FBO_t* hdrFbo, vec4i_t hdrBox, FBO_t* ldrFbo, vec4i_t ldrBox, int autoExposure)
{
	vec4i_t srcBox{}, dstBox{};
	vec4_t color{};
	static int lastFrameCount = 0;

	if (autoExposure)
	{
		if (lastFrameCount == 0 || tr.frameCount < lastFrameCount || tr.frameCount - lastFrameCount > 5)
		{
			// determine average log luminance
			FBO_t* srcFbo = nullptr, * dstFbo = nullptr, * tmp = nullptr;
			int size = 256;

			lastFrameCount = tr.frameCount;

			VectorSet4(dstBox, 0, 0, size, size);

			FBO_Blit(hdrFbo, hdrBox, NULL, tr.textureScratchFbo[0], dstBox, &tr.calclevels4xShader[0], NULL, 0);

			srcFbo = tr.textureScratchFbo[0];
			dstFbo = tr.textureScratchFbo[1];

			// downscale to 1x1 texture
			while (size > 1)
			{
				VectorSet4(srcBox, 0, 0, size, size);
				size >>= 1;
				VectorSet4(dstBox, 0, 0, size, size);

				if (size == 1)
					dstFbo = tr.targetLevelsFbo;

				FBO_Blit(srcFbo, srcBox, NULL, dstFbo, dstBox, &tr.calclevels4xShader[1], NULL, 0);

				tmp = srcFbo;
				srcFbo = dstFbo;
				dstFbo = tmp;
			}
		}

		// blend with old log luminance for gradual change
		VectorSet4(srcBox, 0, 0, 0, 0);

		color[0] =
			color[1] =
			color[2] = 1.0f;

		color[3] = Com_Clamp(0.0f, 1.0f, 0.001f * backEnd.refdef.frameTime);
		FBO_Blit(tr.targetLevelsFbo, srcBox, NULL, tr.calcLevelsFbo, NULL, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	}

	// tonemap
	color[0] =
		color[1] =
		color[2] = powf(2.0f, r_cameraExposure->value + tr.overbrightBits); //exp2(r_cameraExposure->value);
	color[3] = 1.0f;

	if (autoExposure)
		GL_BindToTMU(tr.calcLevelsImage, TB_LEVELSMAP);
	else
		GL_BindToTMU(tr.fixedLevelsImage, TB_LEVELSMAP);

	bool srgbTransform = tr.hdrLighting == qtrue;
	shaderProgram_t* shader = srgbTransform ? &tr.tonemapShader[1] : &tr.tonemapShader[0];

	if (r_smaa->integer == 1)
		GL_BindToTMU(tr.smaaBlendImage, 2);

	FBO_Bind(NULL);
	GL_SetViewportAndScissor(ldrBox[0], ldrBox[1], ldrBox[2], ldrBox[3]);
	GL_Cull(CT_TWO_SIDED);
	GL_State(GLS_DEPTHTEST_DISABLE);
	GLSL_BindProgram(shader);
	GL_BindToTMU(hdrFbo->colorImage[0], TB_COLORMAP);
	GLSL_SetUniformVec4(shader, UNIFORM_COLOR, color);
	GLSL_SetUniformVec2(shader, UNIFORM_AUTOEXPOSUREMINMAX, tr.refdef.autoExposureMinMax);
	GLSL_SetUniformVec3(shader, UNIFORM_TONEMINAVGMAXLINEAR, tr.refdef.toneMinAvgMaxLinear);
	{
		// colour grading (r_grade): u_Grade1.w < 0 = the stock tone map
		vec4_t g0, g1, g2, g3;
		R_GradeUniforms(g0, g1, g2, g3);
		GLSL_SetUniformVec4(shader, UNIFORM_GRADE0, g0);
		GLSL_SetUniformVec4(shader, UNIFORM_GRADE1, g1);
		GLSL_SetUniformVec4(shader, UNIFORM_GRADE2, g2);
		GLSL_SetUniformVec4(shader, UNIFORM_GRADE3, g3);
	}
	RB_InstantTriangle();
}

/*
=============
RB_BokehBlur

Blurs a part of one framebuffer to another.

Framebuffers can be identical.
=============
*/
void RB_BokehBlur(FBO_t* src, vec4i_t srcBox, FBO_t* dst, vec4i_t dstBox, float blur)
{
	vec4_t color{};

	blur *= 10.0f;

	if (blur < 0.004f)
		return;

	// bokeh blur
	if (blur > 0.0f)
	{
		vec4i_t quarterBox{};

		quarterBox[0] = 0;
		quarterBox[1] = tr.quarterFbo[0]->height;
		quarterBox[2] = tr.quarterFbo[0]->width;
		quarterBox[3] = -tr.quarterFbo[0]->height;

		// create a quarter texture
		FBO_FastBlit(src, srcBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	}

#ifndef HQ_BLUR
	if (blur > 1.0f)
	{
		// create a 1/16th texture
		FBO_FastBlit(tr.quarterFbo[0], NULL, tr.textureScratchFbo[0], NULL, GL_COLOR_BUFFER_BIT, GL_LINEAR);
	}
#endif

	if (blur > 0.0f && blur <= 1.0f)
	{
		// Crossfade original with quarter texture
		VectorSet4(color, 1, 1, 1, blur);

		FBO_Blit(tr.quarterFbo[0], NULL, NULL, dst, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	}
#ifndef HQ_BLUR
	// ok blur, but can see some pixelization
	else if (blur > 1.0f && blur <= 2.0f)
	{
		// crossfade quarter texture with 1/16th texture
		FBO_Blit(tr.quarterFbo[0], NULL, NULL, dst, dstBox, NULL, NULL, 0);

		VectorSet4(color, 1, 1, 1, blur - 1.0f);

		FBO_Blit(tr.textureScratchFbo[0], NULL, NULL, dst, dstBox, NULL, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
	}
	else if (blur > 2.0f)
	{
		// blur 1/16th texture then replace
		int i = 0;

		for (i = 0; i < 2; i++)
		{
			vec2_t blurTexScale{};
			float subblur = 0.0f;

			subblur = ((blur - 2.0f) / 2.0f) / 3.0f * (float)(i + 1);

			blurTexScale[0] =
				blurTexScale[1] = subblur;

			color[0] =
				color[1] =
				color[2] = 0.5f;
			color[3] = 1.0f;

			if (i != 0)
				FBO_Blit(tr.textureScratchFbo[0], NULL, blurTexScale, tr.textureScratchFbo[1], NULL, &tr.bokehShader, color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
			else
				FBO_Blit(tr.textureScratchFbo[0], NULL, blurTexScale, tr.textureScratchFbo[1], NULL, &tr.bokehShader, color, 0);
		}

		FBO_Blit(tr.textureScratchFbo[1], NULL, NULL, dst, dstBox, &tr.textureColorShader[TEXCOLORDEF_USE_VERTICES], NULL, 0);
	}
#else // higher quality blur, but slower
	else if (blur > 1.0f)
	{
		// blur quarter texture then replace
		int i;

		src = tr.quarterFbo[0];
		dst = tr.quarterFbo[1];

		VectorSet4(color, 0.5f, 0.5f, 0.5f, 1);

		for (i = 0; i < 2; i++)
		{
			vec2_t blurTexScale;
			float subblur;

			subblur = (blur - 1.0f) / 2.0f * (float)(i + 1);

			blurTexScale[0] =
				blurTexScale[1] = subblur;

			color[0] =
				color[1] =
				color[2] = 1.0f;
			if (i != 0)
				color[3] = 1.0f;
			else
				color[3] = 0.5f;

			FBO_Blit(tr.quarterFbo[0], NULL, blurTexScale, tr.quarterFbo[1], NULL, &tr.bokehShader, color, GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA);
		}

		FBO_Blit(tr.quarterFbo[1], NULL, NULL, dst, dstBox, &tr.textureColorShader, NULL, 0);
	}
#endif
}

static void RB_RadialBlur(FBO_t* srcFbo, FBO_t* dstFbo, int passes, float stretch, float x, float y, float w, float h, float xcenter, float ycenter, float alpha)
{
	vec4i_t srcBox{}, dstBox{};
	vec4_t color{};
	const float inc = 1.f / passes;
	const float mul = powf(stretch, inc);
	float scale = 0.0f;

	{
		vec2_t texScale{};

		texScale[0] =
			texScale[1] = 1.0f;

		alpha *= inc;
		VectorSet4(color, alpha, alpha, alpha, 1.0f);

		VectorSet4(srcBox, 0, 0, srcFbo->width, srcFbo->height);
		VectorSet4(dstBox, x, y, w, h);
		FBO_Blit(srcFbo, srcBox, texScale, dstFbo, dstBox, &tr.textureColorShader[TEXCOLORDEF_USE_VERTICES], color, 0);

		--passes;
		scale = mul;
		while (passes > 0)
		{
			float iscale = 1.f / scale;
			float s0 = xcenter * (1.f - iscale);
			float t0 = (1.0f - ycenter) * (1.f - iscale);
			float s1 = iscale + s0;
			float t1 = iscale + t0;

			if (srcFbo)
			{
				srcBox[0] = s0 * srcFbo->width;
				srcBox[1] = t0 * srcFbo->height;
				srcBox[2] = (s1 - s0) * srcFbo->width;
				srcBox[3] = (t1 - t0) * srcFbo->height;
			}
			else
			{
				srcBox[0] = s0 * glConfig.vidWidth;
				srcBox[1] = t0 * glConfig.vidHeight;
				srcBox[2] = (s1 - s0) * glConfig.vidWidth;
				srcBox[3] = (t1 - t0) * glConfig.vidHeight;
			}

			FBO_Blit(srcFbo, srcBox, texScale, dstFbo, dstBox, &tr.textureColorShader[TEXCOLORDEF_USE_VERTICES], color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);

			scale *= mul;
			--passes;
		}
	}
}

static qboolean RB_UpdateSunFlareVis(void)
{
	GLuint sampleCount = 0;

	tr.sunFlareQueryIndex ^= 1;
	if (!tr.sunFlareQueryActive[tr.sunFlareQueryIndex])
		return qtrue;

	/* debug code */
	if (0)
	{
		int iter = 0;
		for (iter = 0; ; ++iter)
		{
			GLint available = 0;
			qglGetQueryObjectiv(tr.sunFlareQuery[tr.sunFlareQueryIndex], GL_QUERY_RESULT_AVAILABLE, &available);
			if (available)
				break;
		}

		ri.Printf(PRINT_DEVELOPER, "Waited %d iterations\n", iter);
	}

	qglGetQueryObjectuiv(tr.sunFlareQuery[tr.sunFlareQueryIndex], GL_QUERY_RESULT, &sampleCount);
	return (qboolean)(sampleCount > 0);
}

void RB_SunRays(FBO_t* srcFbo, vec4i_t srcBox, FBO_t* dstFbo, vec4i_t dstBox)
{
	vec4_t color{};
	float dot = 0.0f;
	const float cutoff = 0.25f;
	qboolean colorize = qtrue;
	matrix_t mvp;
	vec4_t pos, hpos;

	dot = DotProduct(tr.sunDirection, backEnd.viewParms.ori.axis[0]);
	if (dot < cutoff)
		return;

	if (!RB_UpdateSunFlareVis())
		return;

	// From RB_DrawSun()
	{
		float dist = 0.0f;
		matrix_t trans, model;

		Matrix16Translation(backEnd.viewParms.ori.origin, trans);
		Matrix16Multiply(backEnd.viewParms.world.modelViewMatrix, trans, model);
		Matrix16Multiply(backEnd.viewParms.projectionMatrix, model, mvp);

		dist = backEnd.viewParms.zFar / 1.75;		// div sqrt(3)

		VectorScale(tr.sunDirection, dist, pos);
	}

	// project sun point
	Matrix16Transform(mvp, pos, hpos);

	// transform to UV coords
	hpos[3] = 0.5f / hpos[3];

	pos[0] = 0.5f + hpos[0] * hpos[3];
	pos[1] = 0.5f + hpos[1] * hpos[3];

	// initialize quarter buffers
	{
		float mul = 1.f;
		vec2_t texScale{};
		vec4i_t rayBox{}, quarterBox{};

		texScale[0] =
			texScale[1] = 1.0f;

		VectorSet4(color, mul, mul, mul, 1);

		if (srcFbo)
		{
			rayBox[0] = srcBox[0] * tr.sunRaysFbo->width / srcFbo->width;
			rayBox[1] = srcBox[1] * tr.sunRaysFbo->height / srcFbo->height;
			rayBox[2] = srcBox[2] * tr.sunRaysFbo->width / srcFbo->width;
			rayBox[3] = srcBox[3] * tr.sunRaysFbo->height / srcFbo->height;
		}
		else
		{
			rayBox[0] = srcBox[0] * tr.sunRaysFbo->width / glConfig.vidWidth;
			rayBox[1] = srcBox[1] * tr.sunRaysFbo->height / glConfig.vidHeight;
			rayBox[2] = srcBox[2] * tr.sunRaysFbo->width / glConfig.vidWidth;
			rayBox[3] = srcBox[3] * tr.sunRaysFbo->height / glConfig.vidHeight;
		}

		quarterBox[0] = 0;
		quarterBox[1] = tr.quarterFbo[0]->height;
		quarterBox[2] = tr.quarterFbo[0]->width;
		quarterBox[3] = -tr.quarterFbo[0]->height;

		// first, downsample the framebuffer
		if (colorize)
		{
			FBO_FastBlit(srcFbo, srcBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
			FBO_Blit(tr.sunRaysFbo, rayBox, NULL, tr.quarterFbo[0], quarterBox, NULL, color, GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO);
		}
		else
		{
			FBO_FastBlit(tr.sunRaysFbo, rayBox, tr.quarterFbo[0], quarterBox, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		}
	}

	// radial blur passes, ping-ponging between the two quarter-size buffers
	{
		const float stretch_add = 2.f / 3.f;
		float stretch = 1.f + stretch_add;
		int i = 0;
		for (i = 0; i < 2; ++i)
		{
			RB_RadialBlur(tr.quarterFbo[i & 1], tr.quarterFbo[(~i) & 1], 5, stretch, 0.f, 0.f, tr.quarterFbo[0]->width, tr.quarterFbo[0]->height, pos[0], pos[1], 1.125f);
			stretch += stretch_add;
		}
	}

	// add result back on top of the main buffer
	{
		float mul = 1.f;
		vec2_t texScale{};

		texScale[0] =
			texScale[1] = 1.0f;

		VectorSet4(color, mul, mul, mul, 1);

		FBO_Blit(tr.quarterFbo[0], NULL, texScale, dstFbo, dstBox, &tr.textureColorShader[TEXCOLORDEF_USE_VERTICES], color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
	}
}

static void RB_BlurAxis(FBO_t* srcFbo, FBO_t* dstFbo, float strength, qboolean horizontal)
{
	float dx = 0.0f, dy = 0.0f;
	float xmul = 0.0f, ymul = 0.0f;
	float weights[3] = {
		0.227027027f,
		0.316216216f,
		0.070270270f,
	};
	float offsets[3] = {
		0.f,
		1.3846153846f,
		3.2307692308f,
	};

	xmul = horizontal;
	ymul = 1.f - xmul;

	xmul *= strength;
	ymul *= strength;

	{
		vec4i_t srcBox{}, dstBox{};
		vec4_t color{};
		vec2_t texScale{};

		texScale[0] =
			texScale[1] = 1.0f;

		VectorSet4(color, weights[0], weights[0], weights[0], 1.0f);
		VectorSet4(srcBox, 0, 0, srcFbo->width, srcFbo->height);
		VectorSet4(dstBox, 0, 0, dstFbo->width, dstFbo->height);
		FBO_Blit(srcFbo, srcBox, texScale, dstFbo, dstBox, &tr.textureColorShader[TEXCOLORDEF_USE_VERTICES], color, 0);

		VectorSet4(color, weights[1], weights[1], weights[1], 1.0f);
		dx = offsets[1] * xmul;
		dy = offsets[1] * ymul;
		VectorSet4(srcBox, dx, dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, texScale, dstFbo, dstBox, &tr.textureColorShader[TEXCOLORDEF_USE_VERTICES], color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		VectorSet4(srcBox, -dx, -dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, texScale, dstFbo, dstBox, &tr.textureColorShader[TEXCOLORDEF_USE_VERTICES], color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);

		VectorSet4(color, weights[2], weights[2], weights[2], 1.0f);
		dx = offsets[2] * xmul;
		dy = offsets[2] * ymul;
		VectorSet4(srcBox, dx, dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, texScale, dstFbo, dstBox, &tr.textureColorShader[TEXCOLORDEF_USE_VERTICES], color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
		VectorSet4(srcBox, -dx, -dy, srcFbo->width, srcFbo->height);
		FBO_Blit(srcFbo, srcBox, texScale, dstFbo, dstBox, &tr.textureColorShader[TEXCOLORDEF_USE_VERTICES], color, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE);
	}
}

void RB_HBlur(FBO_t* srcFbo, FBO_t* dstFbo, float strength)
{
	RB_BlurAxis(srcFbo, dstFbo, strength, qtrue);
}

void RB_VBlur(FBO_t* srcFbo, FBO_t* dstFbo, float strength)
{
	RB_BlurAxis(srcFbo, dstFbo, strength, qfalse);
}

void RB_GaussianBlur(FBO_t* srcFbo, FBO_t* intermediateFbo, FBO_t* dstFbo, float spread)
{
	// Blur X
	vec2_t scale{};
	VectorSet2(scale, spread, spread);

	FBO_Blit(srcFbo, NULL, scale, intermediateFbo, NULL, &tr.gaussianBlurShader[0], NULL, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO);

	// Blur Y
	FBO_Blit(intermediateFbo, NULL, scale, dstFbo, NULL, &tr.gaussianBlurShader[1], NULL, GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO);
}

void RB_BloomDownscale(image_t* sourceImage, FBO_t* destFBO)
{
	vec2_t invTexRes = { 1.0f / sourceImage->width, 1.0f / sourceImage->height };

	FBO_Bind(destFBO);
	GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO);
	GL_SetViewportAndScissor(0, 0, destFBO->width, destFBO->height);
	qglClearBufferfv(GL_COLOR, 0, colorBlack);

	GLSL_BindProgram(&tr.dglowDownsample);
	GLSL_SetUniformVec2(&tr.dglowDownsample, UNIFORM_INVTEXRES, invTexRes);
	GL_BindToTMU(sourceImage, 0);

	// Draw fullscreen triangle
	qglDrawArrays(GL_TRIANGLES, 0, 3);
}

void RB_BloomDownscale(FBO_t* sourceFBO, FBO_t* destFBO)
{
	RB_BloomDownscale(sourceFBO->colorImage[0], destFBO);
}

void RB_BloomUpscale(FBO_t* sourceFBO, FBO_t* destFBO)
{
	image_t* sourceImage = sourceFBO->colorImage[0];
	vec2_t invTexRes = { 1.0f / sourceImage->width, 1.0f / sourceImage->height };

	FBO_Bind(destFBO);
	GL_State(GLS_SRCBLEND_ONE | GLS_DSTBLEND_ZERO);
	GL_SetViewportAndScissor(0, 0, destFBO->width, destFBO->height);
	qglClearBufferfv(GL_COLOR, 0, colorBlack);

	GLSL_BindProgram(&tr.dglowUpsample);
	GLSL_SetUniformVec2(&tr.dglowUpsample, UNIFORM_INVTEXRES, invTexRes);
	GL_BindToTMU(sourceImage, 0);

	// Draw fullscreen triangle
	qglDrawArrays(GL_TRIANGLES, 0, 3);
}

/*
==============================================================================
Colour grading (r_grade*) - filmic tone curve and per-map colour grading.
Ported 23/10/2026 from a friend's MD 9 rend2 (tr_grade.cpp), merged into this file.

Every setting is an r_grade* cvar, so the look can be tuned live from the console.
r_grade 0 (default) = the stock rend2 tone map, unchanged. r_grade 1 = graded.
When a map loads, maps/<mapname>.grade is read if it exists (lines of "<cvar> <value>",
or "preset <name>"; // comments allowed); otherwise the look settings go back to their
defaults (r_grade itself is left alone). "grade save" writes the current values to
maps/<mapname>.grade under the home path, "grade reset" restores the defaults,
"grade preset <name>" applies a named look and "grade" prints the current values.
==============================================================================
*/

#ifdef REND2_SP
#define GRADE_RI ri.
#define GRADE_CVAR_GET(n, d, f) ri.Cvar_Get(n, d, f)
#else
#define GRADE_RI ri.
#define GRADE_CVAR_GET(n, d, f) ri.Cvar_Get(n, d, f, "")
#endif

struct gradeVar_t { const char* name; const char* def; cvar_t* cv; };

static gradeVar_t gradeVars[] = {
	{ "r_grade",            "0"     , nullptr }, // 0 = stock rend2 tonemap, 1 = graded
	{ "r_gradeCurve",       "1"     , nullptr }, // 0 = stock filmic, 1 = ACES, 2 = Uchimura (GT, soft highlights)
	{ "r_gradeExposure",    "0"     , nullptr }, // stops
	{ "r_gradeContrast",    "1.12"  , nullptr }, // around 18% grey, in log space
	{ "r_gradeSaturation",  "0.92"  , nullptr },
	{ "r_gradeTemperature", "0"     , nullptr }, // -1 cold .. +1 warm
	{ "r_gradeTint",        "0"     , nullptr }, // -1 green .. +1 magenta
	{ "r_gradeShadows",     "1 1 1" , nullptr }, // multiplier for the dark end (lift colour)
	{ "r_gradeHighlights",  "1 1 1" , nullptr }, // multiplier for the bright end (gain colour)
	{ "r_gradeBlack",       "0"     , nullptr }, // black crush (0..0.1)
	{ "r_gradeVignette",    "0.25"  , nullptr }, // 0..1
};
static const int numGradeVars = ARRAY_LEN(gradeVars);

static cvar_t* GV(const int i) { return gradeVars[i].cv; }

static void GradeParseVec3(const char* s, vec3_t out)
{
	out[0] = out[1] = out[2] = 1.0f;
	const int n = sscanf(s, "%f %f %f", &out[0], &out[1], &out[2]);
	if (n == 1) out[1] = out[2] = out[0];
}

void R_GradeRegisterCvars(void)
{
	for (int i = 0; i < numGradeVars; i++)
		gradeVars[i].cv = GRADE_CVAR_GET(gradeVars[i].name, gradeVars[i].def, CVAR_ARCHIVE_ND);
}

qboolean R_GradeEnabled(void)
{
	return (qboolean)(GV(0) && GV(0)->integer);
}

// u_Grade0 = exposure mul, contrast, saturation, vignette
// u_Grade1 = shadow rgb, curve (-1 = stock tone map)
// u_Grade2 = highlight rgb, black crush
// u_Grade3 = white balance rgb, unused
void R_GradeUniforms(vec4_t g0, vec4_t g1, vec4_t g2, vec4_t g3)
{
	if (!R_GradeEnabled())
	{
		VectorSet4(g0, 1, 1, 1, 0); VectorSet4(g1, 1, 1, 1, -1); VectorSet4(g2, 1, 1, 1, 0); VectorSet4(g3, 1, 1, 1, 0);
		return;
	}
	vec3_t sh, hi;
	GradeParseVec3(GV(7)->string, sh);
	GradeParseVec3(GV(8)->string, hi);

	VectorSet4(g0, powf(2.0f, GV(2)->value), GV(3)->value, GV(4)->value, Com_Clamp(0.0f, 1.0f, GV(10)->value));
	VectorSet4(g1, sh[0], sh[1], sh[2], static_cast<float>(GV(1)->integer));
	VectorSet4(g2, hi[0], hi[1], hi[2], Com_Clamp(0.0f, 0.2f, GV(9)->value));

	// simple white balance: temperature pushes red/blue apart, tint pushes green against magenta
	const float t = Com_Clamp(-1.0f, 1.0f, GV(5)->value), m = Com_Clamp(-1.0f, 1.0f, GV(6)->value);
	VectorSet4(g3, 1.0f + 0.18f * t + 0.06f * m, 1.0f - 0.12f * m, 1.0f - 0.18f * t + 0.06f * m, 0.0f);
	const float lum = 0.2126f * g3[0] + 0.7152f * g3[1] + 0.0722f * g3[2];
	g3[0] /= lum; g3[1] /= lum; g3[2] /= lum;
}

// order: curve exposure contrast saturation temperature tint shadows highlights black vignette
struct gradePreset_t { const char* name; const char* v[10]; };
static const gradePreset_t gradePresets[] = {
	{ "default",{ "1", "0",    "1.12", "0.92", "0",     "0",     "1 1 1",          "1 1 1",          "0",    "0.25" } },
	{ "interior",{ "1", "-0.1", "1.22", "0.78", "-0.25", "0",     "0.92 0.97 1.08", "1.02 1.01 0.98", "0.02", "0.30" } }, // cold metal halls
	{ "sunset",{ "2", "0.1",  "1.15", "1.05", "0.55",  "0.05",  "0.95 0.92 1.05", "1.10 1.00 0.86", "0.01", "0.30" } }, // Bespin / Geonosis dusk
	{ "daylight",{ "2", "0",    "1.10", "0.95", "0.15",  "-0.05", "0.96 1.00 1.04", "1.04 1.02 0.96", "0.01", "0.20" } }, // forest / exterior
	{ "space",{ "1", "0",    "1.25", "0.90", "0.20",  "0",     "0.90 0.95 1.10", "1.08 1.03 0.92", "0.03", "0.35" } },
	{ "noir",{ "1", "0",    "1.30", "0.15", "0",     "0",     "1 1 1",          "1 1 1",          "0.03", "0.45" } },
};

static void GradeApplyPreset(const gradePreset_t& p)
{
	for (int i = 0; i < 10; i++)
		GRADE_RI Cvar_Set(gradeVars[i + 1].name, p.v[i]);
}

// the look settings only: r_grade (on / off) is the player's choice and stays
static void GradeReset(void)
{
	for (int i = 1; i < numGradeVars; i++)
		GRADE_RI Cvar_Set(gradeVars[i].name, gradeVars[i].def);
}

static void GradeMapPath(char* out, const int size)
{
	char base[MAX_QPATH];
	Q_strncpyz(base, tr.world ? COM_SkipPath(tr.world->name) : "nomap", sizeof(base));
	COM_StripExtension(base, base, sizeof(base));
	Com_sprintf(out, size, "maps/%s.grade", base);
}

void R_GradeLoadForMap(void)
{
	if (!GV(0))
	{
		return;
	}

	char path[MAX_QPATH];
	GradeMapPath(path, sizeof(path));

	GradeReset();

	char* buf = nullptr;

	if (GRADE_RI FS_ReadFile(path, reinterpret_cast<void**>(&buf)) <= 0 || !buf)
	{
		return;
	}

	const char* p = buf;
	int set = 0;

	while (*p)
	{
		char line[256] = { 0 };
		int n = 0;

		while (*p && *p != '\n' && n < 255)
		{
			line[n++] = *p++;
		}

		line[n] = '\0';

		// Longer line: skip the remainder.
		while (*p && *p != '\n')
		{
			p++;
		}

		if (*p)
		{
			p++;
		}

		char* c = strstr(line, "//");

		if (c)
		{
			*c = '\0';
		}

		char key[64] = { 0 };
		char val[192] = { 0 };

		if (sscanf(line, " %63s %191[^\r\n]", key, val) != 2)
		{
			continue;
		}

		char* v = val;

		while (*v == ' ' || *v == '"')
		{
			v++;
		}

		const size_t remaining =
			sizeof(val) - static_cast<size_t>(v - val);

		size_t len = strnlen(v, remaining);

		while (len > 0)
		{
			char* e = &v[len - 1];

			if (*e != ' ' && *e != '"')
			{
				break;
			}

			*e = '\0';
			len--;
		}

		if (!Q_stricmp(key, "preset"))
		{
			for (const auto& pr : gradePresets)
			{
				if (!Q_stricmp(v, pr.name))
				{
					GradeApplyPreset(pr);
					set++;
				}
			}

			continue;
		}

		for (int i = 1; i < numGradeVars; i++)
		{
			if (!Q_stricmp(key, gradeVars[i].name))
			{
				GRADE_RI Cvar_Set(gradeVars[i].name, v);
				set++;
			}
		}
	}

	GRADE_RI FS_FreeFile(buf);

	GRADE_RI Printf(
		PRINT_ALL,
		"grade: loaded %s (%d settings)\n",
		path,
		set);
}

void R_Grade_f(void)
{
	const char* cmd = GRADE_RI Cmd_Argc() > 1 ? GRADE_RI Cmd_Argv(1) : "";
	char path[MAX_QPATH];
	GradeMapPath(path, sizeof(path));

	if (!Q_stricmp(cmd, "save"))
	{
		char out[2048]{}; int len = 0;
		len += Com_sprintf(out + len, sizeof(out) - len, "// colour grade for %s (written by \"grade save\")\n", path);
		for (int i = 1; i < numGradeVars; i++)
			len += Com_sprintf(out + len, sizeof(out) - len, "%s \"%s\"\n", gradeVars[i].name, gradeVars[i].cv->string);
		GRADE_RI FS_WriteFile(path, out, len);
		GRADE_RI Printf(PRINT_ALL, "grade: saved %s\n", path);
	}
	else if (!Q_stricmp(cmd, "reset"))
	{
		GradeReset();
		GRADE_RI Printf(PRINT_ALL, "grade: defaults restored\n");
	}
	else if (!Q_stricmp(cmd, "preset"))
	{
		const char* n = GRADE_RI Cmd_Argc() > 2 ? GRADE_RI Cmd_Argv(2) : "";
		for (const auto& p : gradePresets)
			if (!Q_stricmp(n, p.name)) { GradeApplyPreset(p); GRADE_RI Printf(PRINT_ALL, "grade: preset %s\n", p.name); return; }
		GRADE_RI Printf(PRINT_ALL, "grade preset <name>:");
		for (const auto& p : gradePresets) GRADE_RI Printf(PRINT_ALL, " %s", p.name);
		GRADE_RI Printf(PRINT_ALL, "\n");
	}
	else if (!Q_stricmp(cmd, "load"))
	{
		R_GradeLoadForMap();
	}
	else
	{
		GRADE_RI Printf(PRINT_ALL, "grade save | load | reset | preset <name>   (map file: %s)\n", path);
		for (int i = 0; i < numGradeVars; i++)
			GRADE_RI Printf(PRINT_ALL, "  %-20s %s\n", gradeVars[i].name, gradeVars[i].cv->string);
	}
}