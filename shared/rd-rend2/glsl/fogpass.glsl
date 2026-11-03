/*[Vertex]*/
in vec3 attr_Position;
in vec3 attr_Normal;

in vec2 attr_TexCoord0;

#if defined(USE_VERTEX_ANIMATION)
in vec3 attr_Position2;
in vec3 attr_Normal2;
#elif defined(USE_SKELETAL_ANIMATION)
in uvec4 attr_BoneIndexes;
in vec4 attr_BoneWeights;
#endif

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

layout(std140) uniform Entity
{
	mat4 u_ModelMatrix;
	vec4 u_LocalLightOrigin;
	vec3 u_AmbientLight;
	float u_entityTime;
	vec3 u_DirectedLight;
	float u_FXVolumetricBase;
	vec3 u_ModelLightDir;
	float u_VertexLerp;
	vec4 u_SlicePlanes[4]; // saber slice: world space cut planes (tr_saberslice.h)
	vec4 u_SliceSign;
	int u_SliceCount;
	float u_SliceHeat;
};

layout(std140) uniform ShaderInstance
{
	vec4 u_DeformParams0;
	vec4 u_DeformParams1;
	float u_Time;
	float u_PortalRange;
	int u_DeformType;
	int u_DeformFunc;
};

#if defined(USE_SKELETAL_ANIMATION)
layout(std140) uniform Bones
{
	mat3x4 u_BoneMatrices[MAX_G2_BONES];
	uvec4 u_BoneSlice[MAX_G2_BONES / 16]; // saber slice: 2 bits per cut per bone
};
#endif

out vec3 var_WSPosition;
#if defined(USE_ALPHA_TEST)
out vec2 var_TexCoords;
#endif

#if defined(USE_DEFORM_VERTEXES)
float GetNoiseValue( float x, float y, float z, float t )
{
	// Variation on the 'one-liner random function'.
	// Not sure if this is still 'correctly' random
	return fract( sin( dot(
		vec4( x, y, z, t ),
		vec4( 12.9898, 78.233, 12.9898, 78.233 )
	)) * 43758.5453 );
}

float CalculateDeformScale( in int func, in float time, in float phase, in float frequency )
{
	float value = phase + time * frequency;

	switch ( func )
	{
		case WF_SIN:
			return sin(value * 2.0 * M_PI);
		case WF_SQUARE:
			return sign(0.5 - fract(value));
		case WF_TRIANGLE:
			return abs(fract(value + 0.75) - 0.5) * 4.0 - 1.0;
		case WF_SAWTOOTH:
			return fract(value);
		case WF_INVERSE_SAWTOOTH:
			return 1.0 - fract(value);
		default:
			return 0.0;
	}
}

vec3 DeformPosition(const vec3 pos, const vec3 normal, const vec2 st)
{
	switch ( u_DeformType )
	{
		default:
		{
			return pos;
		}

		case DEFORM_BULGE:
		{
			float bulgeHeight = u_DeformParams0.y; // amplitude
			float bulgeWidth = u_DeformParams0.z; // phase
			float bulgeSpeed = u_DeformParams0.w; // frequency

			float scale = CalculateDeformScale( WF_SIN, (u_entityTime + u_frameTime + u_Time), bulgeWidth * st.x, bulgeSpeed );

			return pos + normal * scale * bulgeHeight;
		}

		case DEFORM_BULGE_UNIFORM:
		{
			float bulgeHeight = u_DeformParams0.y; // amplitude

			return pos + normal * bulgeHeight;
		}

		case DEFORM_WAVE:
		{
			float base = u_DeformParams0.x;
			float amplitude = u_DeformParams0.y;
			float phase = u_DeformParams0.z;
			float frequency = u_DeformParams0.w;
			float spread = u_DeformParams1.x;

			float offset = dot( pos.xyz, vec3( spread ) );
			float scale = CalculateDeformScale( u_DeformFunc, (u_entityTime + u_frameTime + u_Time), phase + offset, frequency );

			return pos + normal * (base + scale * amplitude);
		}

		case DEFORM_MOVE:
		{
			float base = u_DeformParams0.x;
			float amplitude = u_DeformParams0.y;
			float phase = u_DeformParams0.z;
			float frequency = u_DeformParams0.w;
			vec3 direction = u_DeformParams1.xyz;

			float scale = CalculateDeformScale( u_DeformFunc, (u_entityTime + u_frameTime + u_Time), phase, frequency );

			return pos + direction * (base + scale * amplitude);
		}

		case DEFORM_PROJECTION_SHADOW:
		{
			vec3 ground = u_DeformParams0.xyz;
			float groundDist = u_DeformParams0.w;
			vec3 lightDir = u_DeformParams1.xyz;

			float d = dot( lightDir, ground );

			lightDir = lightDir * max( 0.5 - d, 0.0 ) + ground;
			d = 1.0 / dot( lightDir, ground );

			vec3 lightPos = lightDir * d;

			return pos - lightPos * dot( pos, ground ) + groundDist;
		}
	}
}

vec3 DeformNormal( const in vec3 position, const in vec3 normal )
{
	if ( u_DeformType != DEFORM_NORMALS )
	{
		return normal;
	}

	float amplitude = u_DeformParams0.y;
	float frequency = u_DeformParams0.w;

	vec3 outNormal = normal;
	const float scale = 0.98;

	outNormal.x += amplitude * GetNoiseValue(
		position.x * scale,
		position.y * scale,
		position.z * scale,
		u_Time * frequency );

	outNormal.y += amplitude * GetNoiseValue(
		100.0 * position.x * scale,
		position.y * scale,
		position.z * scale,
		u_Time * frequency );

	outNormal.z += amplitude * GetNoiseValue(
		200.0 * position.x * scale,
		position.y * scale,
		position.z * scale,
		u_Time * frequency );

	return outNormal;
}
#endif

#if defined(USE_SKELETAL_ANIMATION)
mat4x3 GetBoneMatrix(uint index)
{
	mat3x4 bone = u_BoneMatrices[index];
	return mat4x3(
		bone[0].x, bone[1].x, bone[2].x,
		bone[0].y, bone[1].y, bone[2].y,
		bone[0].z, bone[1].z, bone[2].z,
		bone[0].w, bone[1].w, bone[2].w);
}
#endif

#if defined(USE_SKELETAL_ANIMATION)
// saber slice (tr_saberslice.h): one clip distance per cut, the positive side of a cut is cut away
float SliceValue(int cut, uint mode, vec3 wsPos)
{
	float d = -1000.0;
	if (mode == 1u)
		d = dot(u_SlicePlanes[cut].xyz, wsPos) - u_SlicePlanes[cut].w;
	else if (mode == 2u)
		d = 1000.0;
	return d * u_SliceSign[cut];
}

void SliceClip(vec3 wsPos)
{
	gl_ClipDistance[0] = 1.0;
	gl_ClipDistance[1] = 1.0;
	gl_ClipDistance[2] = 1.0;
	gl_ClipDistance[3] = 1.0;
	if (u_SliceCount <= 0)
		return;

	// the bone this vertex mostly follows
	uint bone = attr_BoneIndexes[0];
	float w = attr_BoneWeights[0];
	if (attr_BoneWeights[1] > w) { bone = attr_BoneIndexes[1]; w = attr_BoneWeights[1]; }
	if (attr_BoneWeights[2] > w) { bone = attr_BoneIndexes[2]; w = attr_BoneWeights[2]; }
	if (attr_BoneWeights[3] > w) { bone = attr_BoneIndexes[3]; }
	uint bits = (u_BoneSlice[bone >> 4u][(bone >> 2u) & 3u] >> ((bone & 3u) * 8u)) & 255u;

	gl_ClipDistance[0] = -SliceValue(0, bits & 3u, wsPos);
	if (u_SliceCount > 1)
		gl_ClipDistance[1] = -SliceValue(1, (bits >> 2u) & 3u, wsPos);
	if (u_SliceCount > 2)
		gl_ClipDistance[2] = -SliceValue(2, (bits >> 4u) & 3u, wsPos);
	if (u_SliceCount > 3)
		gl_ClipDistance[3] = -SliceValue(3, (bits >> 6u) & 3u, wsPos);
}
#endif

#if defined(USE_DEFORM_VERTEXES)
// Grass cutting, plant models (2026-11-02, rd-common tr_grasscut.h R_GrassCutPlantVertex has the same numbers): the
// plant models fall over where a saber cut them, lie, sink to stubble and grow back. u_GrassCutMap = the cut grid
// (texel = cut height, seconds of the cut, cell x, cell y; 4-unit cells, 512 wide, it wraps). u_PlantCut: x = now
// (seconds), y = r_grassCutRegrow, z = seconds the clippings lie (0 = stubble at once), w = 0 off (anything but a plant
// while grass is cut), 1 on, 2 on with embers. u_PlantCenters: the plants near the cuts (x, y, z of the origin, radius),
// u_PlantCount of them. rest and pos are in world space.
uniform sampler2D u_GrassCutMap;
uniform vec4 u_PlantCut;
uniform vec4 u_PlantCenters[32];
uniform int u_PlantCount;

// the cell at p cut below height z (up to 128 units below, not below floorZ), live now: the lower cut wins
void PlantCutSample(in vec2 p, in float z, in float floorZ, inout float cz, inout float ct)
{
	vec2 cell = floor(p / 4.0);
	vec4 c = texelFetch(u_GrassCutMap, ivec2(mod(cell, 512.0)), 0);
	float age = u_PlantCut.x - c.y;
	if (c.y <= 0.0 || c.z != cell.x || c.w != cell.y || c.x >= cz || c.x >= z || z - c.x > 128.0 || c.x < floorZ
		|| age < -0.25 || age >= u_PlantCut.y)
		return;
	cz = c.x;
	ct = c.y;
}

vec3 PlantCut(in vec3 rest, in vec3 pos, out float ember)
{
	ember = 0.0;
	if (u_PlantCut.w < 0.5)
		return pos;
	float cz = 1.0e9;
	float ct = 0.0;
	// the plant it belongs to: the nearest origin within its radius
	int best = -1;
	float bestDist2 = 1.0e18;
	for (int i = 0; i < u_PlantCount; i++)
	{
		vec2 d = u_PlantCenters[i].xy - rest.xy;
		float d2 = dot(d, d);
		if (d2 <= u_PlantCenters[i].w * u_PlantCenters[i].w && d2 < bestDist2)
		{
			best = i;
			bestDist2 = d2;
		}
	}
	vec2 spot = rest.xy; // picks the way it falls
	if (best >= 0)
	{
		vec4 plant = u_PlantCenters[best];
		spot = plant.xy;
		// the stem (within 16 units of the origin, up to 96 units over the foot) cut: all of the plant above that cut falls
		float sz = 1.0e9;
		float st = 0.0;
		for (int i = 0; i < 17; i++)
		{
			float ang = float(i) * 0.7854;
			PlantCutSample(plant.xy + vec2(cos(ang), sin(ang)) * (i == 0 ? 0.0 : i <= 8 ? 8.0 : 16.0), plant.z + 96.0, plant.z - 24.0, sz, st);
		}
		if (sz < 1.0e8)
		{
			if (rest.z <= sz)
				return pos; // below the cut, it stays
			cz = sz;
			ct = st;
		}
		else
		{	// else where the blade went through the line from it to the plant's origin: a leaf falls where it was cut
			int n = clamp(int(ceil(sqrt(bestDist2) / 3.0)), 1, 32);
			for (int k = 0; k <= n; k++)
				PlantCutSample(mix(rest.xy, plant.xy, float(k) / float(n)), rest.z, plant.z - 24.0, cz, ct);
		}
	}
	else
	{	// a plant not in the entity lump: the vertex's cell, then 8 around at 8, 16 and 24 units
		const vec2 dirs[8] = vec2[](vec2(1.0, 0.0), vec2(0.7071, 0.7071), vec2(0.0, 1.0), vec2(-0.7071, 0.7071),
			vec2(-1.0, 0.0), vec2(-0.7071, -0.7071), vec2(0.0, -1.0), vec2(0.7071, -0.7071));
		for (int i = 0; i < 25; i++)
		{
			float dist = i > 0 ? 8.0 * float((i - 1) / 8 + 1) : 0.0;
			PlantCutSample(rest.xy + dirs[(i + 7) & 7] * dist, rest.z, -1.0e9, cz, ct);
		}
	}
	if (cz > 1.0e8)
		return pos;
	float a = max(u_PlantCut.x - ct, 0.0);
	float h = rest.z - cz;
	// stubble 1.5 units over the cut
	vec3 cutPos = vec3(rest.xy, cz + min(h, 1.5));
	float debris = u_PlantCut.z;
	if (debris > 0.0 && a < debris + 0.45)
	{	// what stood above the cut falls over (all of a plant the same way, its origin picks it), lies, then sinks
		float r = floor(spot.x / 32.0) * 12.9898 + floor(spot.y / 32.0) * 78.233;
		vec2 d = vec2(cos(r), sin(r));
		float f = a / 0.45;
		f = f >= 1.0 ? 1.0 : f * f;
		float ang = f * 1.45;
		vec3 fallen = vec3(rest.xy + d * (h * sin(ang)), cz + 0.6 + h * cos(ang));
		cutPos = mix(fallen, cutPos, clamp((a - debris * 0.5) / (debris * 0.5 + 0.45), 0.0, 1.0));
	}
	float g = smoothstep(0.75 * u_PlantCut.y, u_PlantCut.y, a);
	if (u_PlantCut.w > 1.5 && a < 2.0)
	{	// the cut glows like embers, most at the cut
		float e = 1.0 - a / 2.0;
		ember = e * e * (1.0 - g) * max(1.0 - h / 24.0, 0.25);
	}
	return pos + (cutPos - rest) * (1.0 - g);
}
#endif
void main()
{
#if defined(USE_VERTEX_ANIMATION)
	vec3 position = mix(attr_Position, attr_Position2, u_VertexLerp);
#elif defined(USE_SKELETAL_ANIMATION)
	mat4x3 influence =
		GetBoneMatrix(attr_BoneIndexes[0]) * attr_BoneWeights[0] +
        GetBoneMatrix(attr_BoneIndexes[1]) * attr_BoneWeights[1] +
        GetBoneMatrix(attr_BoneIndexes[2]) * attr_BoneWeights[2] +
        GetBoneMatrix(attr_BoneIndexes[3]) * attr_BoneWeights[3];
    vec3 position = influence * vec4(attr_Position, 1.0);
#else
	vec3 position = attr_Position;
#endif

#if defined(USE_DEFORM_VERTEXES)
	#if defined(USE_VERTEX_ANIMATION)
		vec3 normal   = mix(attr_Normal,   attr_Normal2,   u_VertexLerp);
		normal = normalize(normal - vec3(0.5));
	#elif defined(USE_SKELETAL_ANIMATION)
		vec3 normal = normalize(influence * vec4(attr_Normal - vec3(0.5), 0.0));
	#else
		vec3 normal   = attr_Normal * 2.0 - vec3(1.0);
	#endif
	vec3 restPosition = position;
	position = DeformPosition(position, normal, attr_TexCoord0.st);
#endif

	vec4 wsPosition = u_ModelMatrix * vec4(position, 1.0);
#if defined(USE_DEFORM_VERTEXES)
	// grass cutting, in world space (world and entity plants)
	float plantEmber;
	wsPosition.xyz = PlantCut((u_ModelMatrix * vec4(restPosition, 1.0)).xyz, wsPosition.xyz, plantEmber);
#endif
#if defined(USE_SKELETAL_ANIMATION)
	SliceClip(wsPosition.xyz);
#endif
	gl_Position = u_viewProjectionMatrix * wsPosition;

	var_WSPosition = wsPosition.xyz;
#if defined(USE_ALPHA_TEST)
	var_TexCoords = attr_TexCoord0;
#endif
}

/*[Fragment]*/
#if defined(USE_ALPHA_TEST)
uniform int u_AlphaTestType;
uniform sampler2D u_DiffuseMap;
#endif

#if defined(USE_VOLUMETRIC_FOG)
uniform sampler3D u_VolumetricLightMap;

uniform vec3 u_LightGridOrigin;
uniform vec3 u_LightGridCellInverseSize;
#endif

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

layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
};

layout(std140) uniform Entity
{
	mat4 u_ModelMatrix;
	vec4 u_LocalLightOrigin;
	vec3 u_AmbientLight;
	float u_entityTime;
	vec3 u_DirectedLight;
	float u_FXVolumetricBase;
	vec3 u_ModelLightDir;
	float u_VertexLerp;
	vec4 u_SlicePlanes[4]; // saber slice: world space cut planes (tr_saberslice.h)
	vec4 u_SliceSign;
	int u_SliceCount;
	float u_SliceHeat;
};

uniform int u_FogIndex;

in vec3 var_WSPosition;
#if defined(USE_ALPHA_TEST)
in vec2 var_TexCoords;
#endif

out vec4 out_Color;
out vec4 out_Glow;

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
	bool intersects = (t > 0.0 && t < 0.995);

	// only use this for objects with potentially two contibuting fogs
	#if defined(USE_FALLBACK_GLOBAL_FOG)
	if (inFog == intersects)
	{
		Fog globalFog = u_Fogs[u_globalFogIndex];

		float distToVertex = length(V);
		float distFromIntersection = distToVertex - (t * distToVertex);
		float distThroughFog = (intersects ? distFromIntersection : distToVertex);
		float z = globalFog.depthToOpaque * distThroughFog;
	#if defined(USE_VOLUMETRIC_FOG)
		vec3 startPosition = (intersects ? (V * t) + viewOrigin : viewOrigin);
		vec3 endPosition = (normalize(V) * distThroughFog) + startPosition;
		vec3 color = CalcVolumetricFogColor(viewOrigin, endPosition, fog);
		return vec4(color * globalFog.color.rgb, 1.0 - clamp(exp(-z), 0.0, 1.0));
	#else
		return vec4(globalFog.color.rgb, 1.0 - clamp(exp(-(z * z)), 0.0, 1.0));
	#endif
	}
	#elif defined(USE_VOLUMETRIC_FOG)
	if (inFog == intersects)
	{
		if (u_FogIndex == u_globalFogIndex)
		{
			float distToVertexFromViewOrigin = length(V);
			float distToIntersectionFromViewOrigin = t * distToVertexFromViewOrigin;
			float z = fog.depthToOpaque * distToIntersectionFromViewOrigin;
			vec3 startPosition = viewOrigin;
			vec3 endPosition = (normalize(V) * distToIntersectionFromViewOrigin) + startPosition;
			vec3 color = CalcVolumetricFogColor(startPosition, endPosition, fog);
			return vec4(color * fog.color.rgb, 1.0 - clamp(exp(-z), 0.0, 1.0));
		}
		return vec4(0.0);
	}
	#else
	if (inFog == intersects)
	{
		return vec4(0.0);
	}
	#endif

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

void main()
{
#if defined(USE_ALPHA_TEST)
	float alpha = texture(u_DiffuseMap, var_TexCoords).a;
	if (u_AlphaTestType == ALPHA_TEST_GT0)
	{
		if (alpha == 0.0)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_LT128)
	{
		if (alpha >= 0.5)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE128)
	{
		if (alpha < 0.5)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE192)
	{
		if (alpha < 0.75)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_E255)
	{
		if (alpha < 1.00)
			discard;
	}
#endif
	Fog fog = u_Fogs[u_FogIndex];
	out_Color = CalcFog(u_ViewOrigin, var_WSPosition, fog);
	out_Glow = vec4(0.0, 0.0, 0.0, out_Color.a);
}
