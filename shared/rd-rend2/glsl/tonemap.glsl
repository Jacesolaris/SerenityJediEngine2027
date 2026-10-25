// The SMAA Copyright notice 
/**
 * Copyright (C) 2013 Jorge Jimenez (jorge@iryoku.com)
 * Copyright (C) 2013 Jose I. Echevarria (joseignacioechevarria@gmail.com)
 * Copyright (C) 2013 Belen Masia (bmasia@unizar.es)
 * Copyright (C) 2013 Fernando Navarro (fernandn@microsoft.com)
 * Copyright (C) 2013 Diego Gutierrez (diegog@unizar.es)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software. As clarification, there
 * is no requirement that the copyright notice and permission be included in
 * binary distributions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*[Vertex]*/
out vec2 var_TexCoords;

#if defined(USE_SMAA)
out vec4 var_offset;

/**
 * Neighborhood Blending Vertex Shader
 */
void SMAANeighborhoodBlendingVS(vec2 texcoord,
                                out vec4 offset) {
    offset = SMAA_RT_METRICS.xyxy * vec4( 1.0, 0.0, 0.0,  1.0) + texcoord.xyxy;
}
#endif

void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
	var_TexCoords = position * 0.5 + vec2(0.5);
	#if defined(USE_SMAA)
	SMAANeighborhoodBlendingVS(var_TexCoords, var_offset);
	#endif
}

/*[Fragment]*/
uniform sampler2D u_TextureMap;
uniform sampler2D u_LevelsMap;
uniform vec4 u_Color;
uniform vec2 u_AutoExposureMinMax;
uniform vec3 u_ToneMinAvgMaxLinear;
uniform vec4 u_Grade0;
uniform vec4 u_Grade1;
uniform vec4 u_Grade2;
uniform vec4 u_Grade3;

in vec2 var_TexCoords;

#if defined(USE_SMAA)
uniform sampler2D u_BlendMap;
in vec4 var_offset;
#endif

out vec4 out_Color;

#if defined(USE_SMAA)
/**
 * Conditional move:
 */
void SMAAMovc(bvec2 cond, inout vec2 variable, vec2 value) {
    if (cond.x) variable.x = value.x;
    if (cond.y) variable.y = value.y;
}

void SMAAMovc(bvec4 cond, inout vec4 variable, vec4 value) {
    SMAAMovc(cond.xy, variable.xy, value.xy);
    SMAAMovc(cond.zw, variable.zw, value.zw);
}

//-----------------------------------------------------------------------------
// Neighborhood Blending Pixel Shader (Third Pass)

vec4 SMAANeighborhoodBlendingPS(  vec2 texcoord,
                                  vec4 offset,
                                  sampler2D colorTex,
                                  sampler2D blendTex
                                  ) {
    // Fetch the blending weights for current pixel:
    vec4 a;
    a.x = texture(blendTex, offset.xy).a; // Right
    a.y = texture(blendTex, offset.zw).g; // Top
    a.wz = texture(blendTex, texcoord).xz; // Bottom / Left

    // Is there any blending weight with a value greater than 0.0?
    if (dot(a, vec4(1.0, 1.0, 1.0, 1.0)) < 1e-5) {
        vec4 color = textureLod(colorTex, texcoord, 0.0);
        return color;
    } else {
        bool h = max(a.x, a.z) > max(a.y, a.w); // max(horizontal) > max(vertical)

        // Calculate the blending offsets:
        vec4 blendingOffset = vec4(0.0, a.y, 0.0, a.w);
        vec2 blendingWeight = a.yw;
        SMAAMovc(bvec4(h, h, h, h), blendingOffset, vec4(a.x, 0.0, a.z, 0.0));
        SMAAMovc(bvec2(h, h), blendingWeight, a.xz);
        blendingWeight /= dot(blendingWeight, vec2(1.0, 1.0));

        // Calculate the texture coordinates:
        vec4 blendingCoord = blendingOffset * vec4(SMAA_RT_METRICS.xy, -SMAA_RT_METRICS.xy) + texcoord.xyxy;

        // We exploit bilinear filtering to mix current pixel with the chosen
        // neighbor:
        vec4 color = blendingWeight.x * textureLod(colorTex, blendingCoord.xy, 0.0);
        color += blendingWeight.y * textureLod(colorTex, blendingCoord.zw, 0.0);

        return color;
    }
}
#endif

vec3 LinearTosRGB( in vec3 color )
{
	vec3 lo = 12.92 * color;
	vec3 hi = 1.055 * pow(color, vec3(0.4166666)) - 0.055;
	return mix(lo, hi, greaterThanEqual(color, vec3(0.0031308)));
}

vec3 FilmicTonemap(vec3 x)
{
	const float SS  = 0.22; // Shoulder Strength
	const float LS  = 0.30; // Linear Strength
	const float LA  = 0.10; // Linear Angle
	const float TS  = 0.20; // Toe Strength
	const float TAN = 0.01; // Toe Angle Numerator
	const float TAD = 0.30; // Toe Angle Denominator

	vec3 SSxx = SS * x * x;
	vec3 LSx = LS * x;
	vec3 LALSx = LSx * LA;

	return ((SSxx + LALSx + TS * TAN) / (SSxx + LSx + TS * TAD)) - TAN / TAD;

	//return ((x*(SS*x+LA*LS)+TS*TAN)/(x*(SS*x+LS)+TS*TAD)) - TAN/TAD;
}

// The ACES code in this file was originally written by Stephen Hill (@self_shadow), who deserves all
// credit for coming up with this fit and implementing it. Buy him a beer next time you see him. :)

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
const mat3 ACESInputMat = mat3
(
    vec3(0.59719, 0.35458, 0.04823),
    vec3(0.07600, 0.90834, 0.01566),
    vec3(0.02840, 0.13383, 0.83777)
);

// ODT_SAT => XYZ => D60_2_D65 => sRGB
const mat3 ACESOutputMat = mat3
(
    vec3( 1.60475, -0.53108, -0.07367),
    vec3(-0.10208,  1.10813, -0.00605),
    vec3(-0.00327, -0.07276,  1.07602)
);

vec3 RRTAndODTFit(vec3 v)
{
    vec3 a = v * (v + 0.0245786f) - 0.000090537f;
    vec3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

vec3 ACESFitted(vec3 color)
{
    color = color * ACESInputMat;

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = color * ACESOutputMat;

    // Clamp to [0, 1]
    color = clamp(color, 0.0, 1.0);

    return color;
}


vec3 Uchimura(vec3 x)
{
	// Gran Turismo curve (H. Uchimura 2017): long linear section, gentle shoulder
	const float P = 1.0, a = 1.0, m = 0.22, l = 0.4, c = 1.33, b = 0.0;
	float l0 = ((P - m) * l) / a;
	float S0 = m + l0, S1 = m + a * l0, C2 = (a * P) / (P - S1), CP = -C2 / P;
	vec3 w0 = 1.0 - smoothstep(0.0, m, x);
	vec3 w2 = step(m + l0, x);
	vec3 w1 = 1.0 - w0 - w2;
	vec3 T = m * pow(x / m, vec3(c)) + b;
	vec3 S = P - (P - S1) * exp(CP * (x - S0));
	vec3 L = m + a * (x - m);
	return T * w0 + L * w1 + S * w2;
}

// colour grading (r_grade 1, see tr_postprocess.cpp)
vec3 GradeAndTonemap(vec3 c, float whiteLevel)
{
	const vec3 LUM = vec3(0.2126, 0.7152, 0.0722);
	// exposure and white balance (scene-linear)
	c *= u_Grade0.x * u_Grade3.rgb;
	// contrast in log2 space around middle grey
	const float grey = 0.18;
	c = grey * pow(max(c, vec3(1e-6)) / grey, vec3(u_Grade0.y));
	// saturation
	float y = dot(c, LUM);
	c = max(vec3(0.0), mix(vec3(y), c, u_Grade0.z));
	// split toning: shadow colour fades out by middle grey, highlight colour fades in above it
	float t = smoothstep(0.0, 1.0, clamp(log2(max(y, 1e-6) / grey) * 0.25 + 0.5, 0.0, 1.0));
	c *= mix(u_Grade1.rgb, u_Grade2.rgb, t);

	// tone curve, normalised so the scene white level maps to 1
	int curve = int(u_Grade1.w + 0.5);
	if (curve == 1)
		c = ACESFitted(c * 0.6) / ACESFitted(vec3(whiteLevel * 0.6)).g;
	else if (curve == 2)
		c = Uchimura(c) / Uchimura(vec3(whiteLevel)).g;
	else
		c = FilmicTonemap(c) / FilmicTonemap(vec3(whiteLevel));
	c = clamp(c, 0.0, 1.0);

	// black crush (display-referred)
	c = max(vec3(0.0), c - u_Grade2.w) / (1.0 - u_Grade2.w);
	// vignette
	vec2 v = var_TexCoords - 0.5;
	c *= 1.0 - u_Grade0.w * smoothstep(0.15, 0.75, dot(v, v) * 2.0);
	return c;
}

void main()
{
	#if defined(USE_SMAA)
	vec4 color = SMAANeighborhoodBlendingPS(var_TexCoords,
											var_offset,
											u_TextureMap,
											u_BlendMap) * u_Color;
	#else
	vec4 color = texture(u_TextureMap, var_TexCoords) * u_Color;
	#endif

	vec3 minAvgMax = texture(u_LevelsMap, var_TexCoords).rgb;
	vec3 logMinAvgMaxLum = clamp(minAvgMax * 20.0 - 10.0, -u_AutoExposureMinMax.y, -u_AutoExposureMinMax.x);

	float avgLum = exp2(logMinAvgMaxLum.y);
	//float maxLum = exp2(logMinAvgMaxLum.z);

	color.rgb *= u_ToneMinAvgMaxLinear.y / avgLum;
	color.rgb = max(vec3(0.0), color.rgb - vec3(u_ToneMinAvgMaxLinear.x));

	if (u_Grade1.w < 0.0)
	{
		vec3 fWhite = 1.0 / FilmicTonemap(vec3(u_ToneMinAvgMaxLinear.z - u_ToneMinAvgMaxLinear.x));
		color.rgb = FilmicTonemap(color.rgb) * fWhite;
	}
	else
	{
		color.rgb = GradeAndTonemap(color.rgb, u_ToneMinAvgMaxLinear.z - u_ToneMinAvgMaxLinear.x);
	}

	#if defined(USE_LINEAR_LIGHT)
		color.rgb = LinearTosRGB(color.rgb);
	#endif

	out_Color = clamp(color, 0.0, 1.0);
}
