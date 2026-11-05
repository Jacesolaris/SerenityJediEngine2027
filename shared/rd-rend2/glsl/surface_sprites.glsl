/*[Vertex]*/
in vec4 attr_Position; // x, y, z, random value [0.0, 1.0]
in vec3 attr_Normal;
in vec3 attr_Color;
in vec4 attr_Position2; // width, height, skew.x, skew.y

layout(std140) uniform Scene
{
	vec4 u_PrimaryLightOrigin;
	vec3 u_PrimaryLightAmbient;
	int  u_globalFogIndex;
	vec3 u_PrimaryLightColor;
	float u_PrimaryLightRadius;
	float u_frameTime;
	float u_deltaTime;
};

layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
};

layout(std140) uniform SurfaceSprite
{
	vec2  u_FxGrow;
	float u_FxDuration;
	float u_FadeStartDistance;
	float u_FadeEndDistance;
	float u_FadeScale;
	float u_Wind;
	float u_WindIdle;
	float u_FxAlphaStart;
	float u_FxAlphaEnd;
};

// Grass wind and bending (2026-10-26): characters near the grass (x, y, z, radius), and
// x = how many, y = r_grassWind sway on all grass, z = r_grassPush strength
uniform vec4 u_GrassPushers[16];
uniform vec4 u_GrassParams;
// Grass force waves (2026-10-27): force push, pull and repulse waves, two vec4 each: origin + seconds since it
// started, flat direction + type (0 push, 1 pull, 2 repulse) + range; params x = how many, y = r_grassForceWave
uniform vec4 u_GrassWaves[8];
uniform vec4 u_GrassWaveParams;
// Grass cutting (2026-11-02, tr_grasscut.h): u_GrassWaveParams.z = 1: cut grass is stubble, 2: draw only the clippings
// (the cut tops falling over and lying). Grid texel: cut height, seconds of the cut, cell x, cell y (4-unit cells,
// 512 wide, it wraps). x = now (seconds), y = r_grassCutRegrow, z = seconds the clippings lie, w = ember glow on
uniform sampler2D u_GrassCutMap;
uniform vec4 u_GrassCutParams;

#if defined(VELOCITY_PASS)
layout(std140) uniform TemporalInfo
{
	mat4 u_previousViewProjectionMatrix;
	vec2 u_currentJitter;
	vec2 u_previousJitter;
	float u_previousFrameTime;
};
out vec4 var_Position;
out vec4 var_prevPosition;
#endif

out vec2 var_TexCoords;
out float var_Alpha;
out vec3 var_Color;
out vec3 var_Ember;

#if defined(FX_SPRITE)
out float var_Effectpos;
#endif

#if defined(USE_FOG)
out vec3 var_WSPosition;
#endif

#if !defined(FACE_UP) && !defined(FX_SPRITE)
// Same numbers as tr_grasscut.h R_GrassCutBlade: keep = the part of the blade that stands now, keepCut = what the cut
// left, age = seconds since the cut. false: not cut.
bool GrassCut(in float height, out float keep, out float keepCut, out float age)
{
	keep = 1.0;
	keepCut = 1.0;
	age = 0.0;
	if (u_GrassWaveParams.z < 0.5 || height <= 0.0)
		return false;
	vec2 cell = floor(attr_Position.xy / 4.0);
	vec4 c = texelFetch(u_GrassCutMap, ivec2(mod(cell, 512.0)), 0);
	if (c.y <= 0.0 || c.z != cell.x || c.w != cell.y)
		return false;
	float a = u_GrassCutParams.x - c.y;
	if (a < -0.25 || a >= u_GrassCutParams.y)
		return false;
	float k = (c.x - attr_Position.z) / height;
	if (k >= 0.92 || k < -0.25)
		return false;
	keepCut = max(k, 0.12);
	keep = mix(keepCut, 1.0, smoothstep(0.75 * u_GrassCutParams.y, u_GrassCutParams.y, a));
	age = max(a, 0.0);
	return true;
}
#endif

vec3 CalculateVertexOffset( in int vertex_id, in float sprite_time, in float fadeScale, in float keep)
{
	float width = attr_Position2.x;
	float height = attr_Position2.y;
	vec2 skew = attr_Position2.zw;
	
	width += u_FadeScale * fadeScale * width;

#if defined(FX_SPRITE)
	var_Effectpos = fract((sprite_time+10000.0*attr_Position.w) / u_FxDuration);
	width += var_Effectpos * width * u_FxGrow.x;
	height += var_Effectpos * height * u_FxGrow.y;
#endif

#if !defined(FACE_FLATTENED)
	float halfWidth = width * 0.5;
	vec3 offsets[] = vec3[](
#if defined(FACE_UP)
		vec3( halfWidth, -halfWidth, 0.0),
		vec3( halfWidth,  halfWidth, 0.0),
		vec3(-halfWidth,  halfWidth, 0.0),
		vec3(-halfWidth, -halfWidth, 0.0)
#else
		vec3( halfWidth, 0.0, 0.0),
		vec3( halfWidth, 0.0, height),
		vec3(-halfWidth, 0.2, height), // Offset this upper vertex to make sprite visable from above
		vec3(-halfWidth, 0.0, 0.0)
#endif
	);
#else
	float offsetValue = mix(width, height, attr_Position.w);
	vec3 offsets[] = vec3[](
		vec3( offsetValue, 0.0, 0.0),
		vec3( offsetValue, 0.0, height),
		vec3(-offsetValue, 0.2, height), // Offset this upper vertex to make sprite visable from above
		vec3(-offsetValue, 0.0, 0.0)
	);
#endif

	vec3 offset = offsets[vertex_id];

#if defined(FACE_CAMERA)
	offset = (offset.x * normalize(u_ViewLeft)) + (offset.z * normalize(u_ViewUp));
#elif defined(FACE_FLATTENED)
	// Make this sprite face in some direction
	vec3 fwdVec = cross(attr_Normal, vec3(0.0, 0.0, 1.0));
	offset.xy = (offset.x * attr_Normal.xy) + (offset.y * width * fwdVec.xy);
#elif !defined(FACE_UP)
	// Make this sprite face in some direction in direction of the camera
	vec3 lftVec = normalize(u_ViewLeft);
	vec3 fwdVec = cross(lftVec, vec3(0.0, 0.0, 1.0));
	offset.xy = (offset.x * normalize(attr_Normal.xy + 2.0 * lftVec.xy)) + (offset.y * width * fwdVec.xy);
#endif

#if !defined(FACE_UP) && !defined(FX_SPRITE)
	vec2 standXY = offset.xy;
	float isLowerVertex = float(offset.z == 0.0);
	offset.xy += mix(skew, vec2(0.0), isLowerVertex);
	float angle = (attr_Position.x + attr_Position.y) * 0.02 + (sprite_time * 0.0015);
	float windsway = mix(height* u_WindIdle * 0.075, 0.0, isLowerVertex);
	offset.xy += vec2(cos(angle), sin(angle)) * windsway;

	float upper = 1.0 - isLowerVertex;
	if (u_GrassParams.y > 0.0)
	{
		// sway and gusts rolling across the field (also on grass with its own wind, that sway was too small to see)
		offset.xy += vec2(cos(angle), sin(angle)) * (height * 0.12 * u_GrassParams.y * upper);
		const vec2 gustDir = vec2(0.8, 0.6);
		float gust = 0.5 + 0.5 * sin(sprite_time * 0.0009 - dot(attr_Position.xy, gustDir) * 0.006);
		float flutter = 0.45 + 0.06 * sin(sprite_time * 0.006 + (attr_Position.x + attr_Position.y) * 0.05);
		float lean = height * u_GrassParams.y * gust * gust * flutter * upper;
		offset.xy += gustDir * lean;
		offset.z -= 0.3 * lean;
	}

	// bend away from anyone standing in the grass
	int numPushers = int(u_GrassParams.x);
	if (numPushers > 0 && upper > 0.0)
	{
		vec2 bend = vec2(0.0);
		float down = 0.0;
		for (int i = 0; i < numPushers; i++)
		{
			vec4 p = u_GrassPushers[i];
			float dz = attr_Position.z - p.z;
			vec2 d = attr_Position.xy - p.xy;
			float dist = length(d);
			if (dz < -2.25 * p.w || dz > 1.25 * p.w || dist >= p.w)
				continue;
			float s = 1.0 - dist / p.w;
			s = s * s * (3.0 - 2.0 * s);
			float a = attr_Position.x * 12.9898 + attr_Position.y * 78.233;
			vec2 dir = dist > 0.5 ? d / dist : vec2(cos(a), sin(a));
			bend += dir * s;
			down += s;
		}
		float len = length(bend);
		if (len > 1.0)
			bend /= len;
		down = min(down, 1.0);
		offset.xy += bend * (height * 0.9 * u_GrassParams.z);
		offset.z = max(offset.z - down * height * 0.6 * u_GrassParams.z, height * 0.1);
	}

	// force push, pull and repulse waves running out through the grass (same numbers as rd-vanilla tr_surfacesprites.cpp)
	int numWaves = int(u_GrassWaveParams.x);
	if (numWaves > 0 && upper > 0.0)
	{
		vec2 bend = vec2(0.0);
		float down = 0.0;
		for (int i = 0; i < numWaves; i++)
		{
			vec4 wo = u_GrassWaves[i * 2];
			vec4 wd = u_GrassWaves[i * 2 + 1];
			float dz = attr_Position.z - wo.z;
			vec2 d = attr_Position.xy - wo.xy;
			float dist = length(d);
			if (dz < -160.0 || dz > 96.0 || dist >= wd.w)
				continue;
			float a = attr_Position.x * 12.9898 + attr_Position.y * 78.233;
			vec2 dir = dist > 0.5 ? d / dist : vec2(cos(a), sin(a));
			float cone = 1.0;
			if (wd.z < 1.5 && dist > 32.0)
				cone = smoothstep(0.35, 0.75, dot(dir, wd.xy));
			float tp = wo.w - dist / 1400.0; // seconds since the wave front passed this blade
			if (tp <= 0.0 || cone <= 0.0)
				continue;
			// flattened at once, then it springs back with a little overshoot
			float s = min(tp / 0.06, 1.0) * exp(-2.2 * tp) * cos(tp * 3.5) * cone * (1.0 - (dist * dist) / (wd.w * wd.w));
			bend += dir * (wd.z > 0.5 && wd.z < 1.5 ? -s : s);
			down += abs(s);
		}
		float len = length(bend);
		if (len > 1.0)
			bend /= len;
		down = min(down, 1.0);
		offset.xy += bend * (height * 1.2 * u_GrassWaveParams.y);
		offset.z = max(offset.z - down * height * 0.7 * u_GrassWaveParams.y, height * 0.1);
	}

	// cut: stubble up to the cut (the bends shrink with it)
	offset.xy = standXY + (offset.xy - standXY) * keep;
	offset.z *= keep;
#endif
	return offset;
}

void main()
{
	var_Ember = vec3(0.0);
	vec3 V = u_ViewOrigin - attr_Position.xyz;
	float distanceToCamera = length(V);
	float fadeScale = smoothstep(u_FadeStartDistance, u_FadeEndDistance,
						distanceToCamera);
	
	if (fadeScale >= 1.0)
	{
		gl_Position = vec4(0.0);
		return;
	}

	float sprite_time = u_frameTime * 1000.0;
	int vertex_id = gl_VertexID % 4;

	float keep = 1.0, keepCut = 1.0, cutAge = 0.0;
	bool cut = false;
#if !defined(FACE_UP) && !defined(FX_SPRITE)
	cut = GrassCut(attr_Position2.y, keep, keepCut, cutAge);
#endif
	if (u_GrassWaveParams.z > 1.5)
	{
		// the clippings pass: the cut top of each cut blade falls over and lies, everything else is left out
#if !defined(FACE_UP) && !defined(FX_SPRITE)
		float debris = u_GrassCutParams.z;
		if (!cut || cutAge >= debris + 0.45)
		{
			gl_Position = vec4(0.0);
			return;
		}
		int bottomId = (vertex_id == 0 || vertex_id == 1) ? 0 : 3;
		vec3 offB = CalculateVertexOffset(bottomId, sprite_time, fadeScale, 1.0);
		vec3 offT = CalculateVertexOffset(bottomId == 0 ? 1 : 2, sprite_time, fadeScale, 1.0);
		vec3 cutP = mix(offB, offT, keepCut);
		float len = length(offT - cutP);
		float ra = attr_Position.x * 12.9898 + attr_Position.y * 78.233;
		vec2 d = vec2(cos(ra), sin(ra));
		float f = cutAge / 0.45;
		f = f >= 1.0 ? 1.0 : f * f;
		vec3 lyingSide = vec3(-d.y, d.x, 0.0) * length(offB.xy) * (bottomId == 0 ? 1.0 : -1.0);
		vec3 lyingB = vec3(d * 2.0, 0.6) + lyingSide;
		vec3 lyingT = lyingB + vec3(d * len, 0.0);
		vec3 clipOffset = (vertex_id == 1 || vertex_id == 2) ? mix(offT, lyingT, f) : mix(cutP, lyingB, f);
		vec4 clipPos = vec4(attr_Position.xyz + clipOffset, 1.0);
		gl_Position = u_viewProjectionMatrix * clipPos;
#if defined(USE_FOG)
		var_WSPosition = clipPos.xyz;
#endif
#if defined(VELOCITY_PASS)
		var_Position = gl_Position;
		var_prevPosition = gl_Position;
#endif
		const vec2 clipTexcoords[] = vec2[](vec2(1.0, 1.0), vec2(1.0, 0.0), vec2(0.0, 0.0), vec2(0.0, 1.0));
		var_TexCoords = clipTexcoords[vertex_id];
		var_Color = attr_Color;
		float fadeStart = debris * 0.5;
		float clipAlpha = cutAge <= fadeStart ? 1.0 : 1.0 - (cutAge - fadeStart) / (debris - fadeStart + 0.45);
		var_Alpha = (1.0 - fadeScale) * clamp(clipAlpha, 0.0, 1.0);
#else
		gl_Position = vec4(0.0);
#endif
		return;
	}
	if (cut && u_GrassCutParams.w > 0.5 && cutAge < 2.0 && (vertex_id == 1 || vertex_id == 2))
	{	// the cut tips glow like embers
		float e = 1.0 - cutAge / 2.0;
		var_Ember = vec3(1.0, 0.35, 0.05) * (1.5 * e * e);
	}

	vec3 offset = CalculateVertexOffset(vertex_id, sprite_time, fadeScale, keep);

	vec4 worldPos = vec4(attr_Position.xyz + offset, 1.0);
	gl_Position = u_viewProjectionMatrix * worldPos;
#if defined(USE_FOG)
	var_WSPosition = worldPos.xyz;
#endif

#if defined(VELOCITY_PASS)
	var_Position = gl_Position;
	sprite_time = u_previousFrameTime * 1000.0;
	offset = CalculateVertexOffset(vertex_id, sprite_time, fadeScale, keep);
	worldPos = vec4(attr_Position.xyz + offset, 1.0);
	var_prevPosition = u_previousViewProjectionMatrix * worldPos;
#endif

	const vec2 texcoords[] = vec2[](
		vec2(1.0, 1.0),
		vec2(1.0, 0.0),
		vec2(0.0, 0.0),
		vec2(0.0, 1.0)
	);
	var_TexCoords = texcoords[vertex_id];
	var_Color = attr_Color;
	var_Alpha = 1.0 - fadeScale;

}

/*[Fragment]*/
uniform sampler2D u_DiffuseMap;

#if defined(VELOCITY_PASS)
layout(std140) uniform TemporalInfo
{
	mat4 u_previousViewProjectionMatrix;
	vec2 u_currentJitter;
	vec2 u_previousJitter;
	float u_previousFrameTime;
};
in vec4 var_Position;
in vec4 var_prevPosition;
#endif

in vec2 var_TexCoords;
in vec3 var_Color;
in vec3 var_Ember;
in float var_Alpha;

#if defined(FX_SPRITE)
in float var_Effectpos;
#endif

#if defined(USE_FOG)
in vec3 var_WSPosition;
#endif

layout(std140) uniform SurfaceSprite
{
	vec2  u_FxGrow;
	float u_FxDuration;
	float u_FadeStartDistance;
	float u_FadeEndDistance;
	float u_FadeScale;
	float u_Wind;
	float u_WindIdle;
	float u_FxAlphaStart;
	float u_FxAlphaEnd;
};

#if defined(USE_FOG)
layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
};

struct Fog
{
	vec4 plane;
	vec4 color;
	float depthToOpaque;
	bool hasPlane;
};

layout(std140) uniform Fogs
{
	int u_NumFogs;
	Fog u_Fogs[MAX_GPU_FOGS];
};

uniform int u_FogIndex;
uniform vec4 u_FogColorMask;

#if defined(USE_VOLUMETRIC_FOG)
uniform sampler3D u_VolumetricLightMap;

uniform vec3 u_LightGridOrigin;
uniform vec3 u_LightGridCellInverseSize;
#endif
#endif

#if defined(USE_ALPHA_TEST)
uniform int u_AlphaTestType;
#endif

out vec4 out_Color;
#if !defined(VELOCITY_PASS)
out vec4 out_Glow;
out vec4 out_SsrMask;	// r_ssr material mask (Jace 2026-11-04): grass / plant sprites never reflect
#endif

#if defined(USE_FOG)
#if defined(USE_VOLUMETRIC_FOG)
vec3 CalcVolumetricFogColor(in vec3 startPosition, in vec3 endPosition, in Fog fog)
{
	ivec3 gridSize = textureSize(u_VolumetricLightMap, 0);
	vec3 invGridSize = u_LightGridCellInverseSize / vec3(gridSize);
	
	const int steps = r_volumetricFogSamples;
	vec3 step = (endPosition - startPosition) / steps;
	float z = fog.depthToOpaque * length(step);

	vec3 position = startPosition;
	float transmittance  = 1.0;
	vec3 color = vec3(0.0);
	for (int i = 0; i < steps; i++)
	{
		float currentTransmittance = exp(-z);
		float currentOpacity = 1.0 - currentTransmittance;

		vec3 gridCell = (position - u_LightGridOrigin) * invGridSize;
		color += texture(u_VolumetricLightMap, gridCell).rgb * transmittance * currentOpacity;
		transmittance *= currentTransmittance;
		
		position += step;
	}
	return color;
}
#endif

vec4 CalcFog(in vec3 viewOrigin, in vec3 position, in Fog fog)
{
	bool inFog = dot(viewOrigin, fog.plane.xyz) - fog.plane.w >= 0.0 || !fog.hasPlane;

	// line: x = o + tv
	// plane: (x . n) + d = 0
	// intersects: dot(o + tv, n) + d = 0
	//             dot(o + tv, n) = -d
	//             dot(o, n) + t*dot(n, v) = -d
	//             t = -(d + dot(o, n)) / dot(n, v)
	vec3 V = position - viewOrigin;

	// fogPlane is inverted in tr_bsp for some reason.
	// A view ray parallel to the fog plane made this divide by 0 (t = inf/NaN -> NaN fog -> white blobs with HDR).
	float denom = dot(V, -fog.plane.xyz);
	float t = abs(denom) > 1e-6 ? -(fog.plane.w + dot(viewOrigin, -fog.plane.xyz)) / denom : -1.0;

	bool intersects = (t > 0.0 && t <= 1.0);
	if (inFog == intersects)
		return vec4(0.0);

	float distToVertexFromViewOrigin = length(V);
	float distToIntersectionFromViewOrigin = t * distToVertexFromViewOrigin;

	float distOutsideFog = max(distToVertexFromViewOrigin - distToIntersectionFromViewOrigin, 0.0);
	float distThroughFog = (inFog ? distToVertexFromViewOrigin : distOutsideFog);

	float z = fog.depthToOpaque * distThroughFog;
#if defined(USE_VOLUMETRIC_FOG)
	vec3 startPosition = (inFog ? viewOrigin : (V * t) + viewOrigin);
	vec3 endPosition = (normalize(V) * distThroughFog) + startPosition;
	vec3 color = CalcVolumetricFogColor(startPosition, endPosition, fog);
	return vec4(color * fog.color.rgb, 1.0 - clamp(exp(-z), 0.0, 1.0));
#else
	return vec4(fog.color.rgb, 1.0 - clamp(exp(-(z * z)), 0.0, 1.0));
#endif
}
#endif

void main()
{
#if defined(USE_ALPHA_TEST)
	float alphaTestValue = 0.5;
	if (u_AlphaTestType == ALPHA_TEST_GT0)
	{
		alphaTestValue = 0.0;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE192)
	{
		alphaTestValue = 0.75;
	}
#else
	const float alphaTestValue = 0.5;
#endif

	out_Color = texture(u_DiffuseMap, var_TexCoords);
	out_Color.rgb *= var_Color;
	out_Color.a *= var_Alpha*(1.0 - alphaTestValue) + alphaTestValue;

#if defined(FX_SPRITE)
	float fxalpha = u_FxAlphaEnd - u_FxAlphaStart;
	if (u_FxAlphaEnd < 0.05)
	{
	if (var_Effectpos > 0.5)
		out_Color.a *= u_FxAlphaStart + (fxalpha * (var_Effectpos - 0.5) * 2.0);
	else
		out_Color.a *= u_FxAlphaStart + (fxalpha * (0.5 - var_Effectpos) * 2.0);
	}
	else
	{
		out_Color.a *= u_FxAlphaStart + (fxalpha * var_Effectpos);
	}
#endif

#if defined(USE_ALPHA_TEST)
	if (u_AlphaTestType == ALPHA_TEST_GT0)
	{
		if (out_Color.a == 0.0)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_LT128)
	{
		if (out_Color.a >= 0.5)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE128)
	{
		if (out_Color.a < 0.5)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE192)
	{
		if (out_Color.a < 0.75)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_E255)
	{
		if (out_Color.a < 1.00)
			discard;
	}
#endif

	out_Color.rgb += var_Ember * out_Color.a;

#if defined(USE_FOG)
	Fog fog = u_Fogs[u_FogIndex];
	vec4 fogColorOpacity = CalcFog(u_ViewOrigin, var_WSPosition, fog);
#if defined(ADDITIVE_BLEND)
	out_Color.rgb *= fogColorOpacity.rgb * (1.0 - fogColorOpacity.a);
#else
#if defined(USE_VOLUMETRIC_FOG)
	out_Color.rgb = out_Color.rgb * (1.0-fogColorOpacity.a) + fogColorOpacity.rgb;
#else
	out_Color.rgb = mix(out_Color.rgb, fogColorOpacity.rgb, fogColorOpacity.a);
#endif
#endif
#endif

#if defined(ADDITIVE_BLEND)
	out_Color.rgb *= out_Color.a;
#endif

#if defined(VELOCITY_PASS)
	vec2 currentPos = (var_Position.xy / var_Position.w) * 0.5 + 0.5;
	vec2 prevPos = (var_prevPosition.xy / var_prevPosition.w) * 0.5 + 0.5;
	vec2 motionVector = currentPos - prevPos;

	motionVector -= u_currentJitter / r_FBufScale.xy;
	motionVector -= u_previousJitter / r_FBufScale.xy;

	out_Color = vec4(motionVector, 0.0, 1.0);
#else
	out_Glow = vec4(var_Ember * out_Color.a, 0.0);
	out_SsrMask = vec4(1.0);
#endif
	
}
