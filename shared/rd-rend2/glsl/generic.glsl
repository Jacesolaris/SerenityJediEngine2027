/*[Vertex]*/
in vec3 attr_Position;
in vec3 attr_Normal;

#if defined(USE_VERTEX_ANIMATION)
in vec3 attr_Position2;
in vec3 attr_Normal2;
#elif defined(USE_SKELETAL_ANIMATION)
in uvec4 attr_BoneIndexes;
in vec4 attr_BoneWeights;
#endif

in vec4 attr_Color;
in vec2 attr_TexCoord0;

#if defined(USE_TCGEN)
in vec2 attr_TexCoord1;
in vec2 attr_TexCoord2;
in vec2 attr_TexCoord3;
in vec2 attr_TexCoord4;
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

#if defined(USE_DEFORM_VERTEXES) || defined(USE_RGBAGEN)
layout(std140) uniform ShaderInstance
{
	vec4 u_DeformParams0;
	vec4 u_DeformParams1;
	float u_Time;
	float u_PortalRange;
	int u_DeformType;
	int u_DeformFunc;
};
#endif

#if defined(USE_SKELETAL_ANIMATION)
layout(std140) uniform Bones
{
	mat3x4 u_BoneMatrices[MAX_G2_BONES];
	uvec4 u_BoneSlice[MAX_G2_BONES / 16]; // saber slice: 2 bits per cut per bone
};
#endif

uniform vec4 u_DiffuseTexMatrix;
uniform vec4 u_DiffuseTexOffTurb;

#if defined(USE_TCGEN)
uniform int u_TCGen0;
uniform vec3 u_TCGen0Vector0;
uniform vec3 u_TCGen0Vector1;
#endif

uniform vec4 u_BaseColor;
uniform vec4 u_VertColor;

#if defined(USE_RGBAGEN)
uniform int u_ColorGen;
uniform int u_AlphaGen;
#endif

#if defined(USE_RGBAGEN) || defined(USE_DEFORM_VERTEXES)
uniform vec4 u_Disintegration; // origin, threshhold
#endif

#if defined(USE_FLARE_TEST)
uniform sampler2D u_ScreenDepthMap;
uniform vec4 u_LightOrigin;
#endif

out vec2 var_DiffuseTex;
out vec4 var_Color;
#if defined(USE_SKELETAL_ANIMATION)
// saber slice (SJE): world position and the per-cut values (> 0 = cut away, +-1000 = not a plane cut here)
// for the flat cap and the glowing seam in the fragment stage
out vec3 var_SlicePos;
out vec4 var_SliceD;
#endif
#if defined(USE_FOG)
out vec3 var_WSPosition;
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
#if defined(USE_RGBAGEN)
	if (u_ColorGen == CGEN_DISINTEGRATION_2)
	{
		vec3 delta = u_Disintegration.xyz - pos;
		float sqrDistance = dot(delta, delta);
		vec3 normalScale = vec3(-0.01);
		if ( sqrDistance < u_Disintegration.w )
		{
			normalScale = vec3(2.0, 2.0, 0.5);
		}
		else if ( sqrDistance < u_Disintegration.w + 50 )
		{
			normalScale = vec3(1.0, 1.0, 0.0);
		}
		return pos + normal * normalScale;
	}
#endif
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
		//case DEFORM_DISINTEGRATION:
		//{
		//}
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
		(u_entityTime + u_frameTime + u_Time) * frequency );

	outNormal.y += amplitude * GetNoiseValue(
		100.0 * position.x * scale,
		position.y * scale,
		position.z * scale,
		(u_entityTime + u_frameTime + u_Time) * frequency );

	outNormal.z += amplitude * GetNoiseValue(
		200.0 * position.x * scale,
		position.y * scale,
		position.z * scale,
		(u_entityTime + u_frameTime + u_Time) * frequency );

	return outNormal;
}
#endif

#if defined(USE_TCGEN)
vec2 GenTexCoords(int TCGen, vec3 position, vec3 normal, vec3 TCGenVector0, vec3 TCGenVector1)
{
	vec2 tex = attr_TexCoord0.st;

	switch (TCGen)
	{
		case TCGEN_LIGHTMAP:
			tex = attr_TexCoord1;
		break;

		case TCGEN_LIGHTMAP1:
			tex = attr_TexCoord2;
		break;

		case TCGEN_LIGHTMAP2:
			tex = attr_TexCoord3;
		break;

		case TCGEN_LIGHTMAP3:
			tex = attr_TexCoord4;
		break;

		case TCGEN_ENVIRONMENT_MAPPED:
		{
			vec3 localOrigin = (inverse(u_ModelMatrix) * vec4(u_ViewOrigin, 1.0)).xyz;
			vec3 viewer = normalize(localOrigin - position);
			vec2 ref = reflect(viewer, normal).yz;
			tex.s = ref.x * -0.5 + 0.5;
			tex.t = ref.y *  0.5 + 0.5;
		}
		break;

		case TCGEN_ENVIRONMENT_MAPPED_SP:
		{
			vec3 localOrigin = (inverse(u_ModelMatrix) * vec4(u_ViewOrigin, 1.0)).xyz;
			vec3 viewer = normalize(localOrigin - position);
			vec2 ref = reflect(viewer, normal).xy;
			tex.s = ref.x * -0.5;
			tex.t = ref.y * -0.5;
		}
		break;

		case TCGEN_ENVIRONMENT_MAPPED_SP_FP:
		{
			vec2 ref = reflect(u_ModelLightDir.xyz, normal).xy;
			tex.s = ref.x * -0.5 + 0.5 * u_ModelLightDir.x;
			tex.t = ref.y * -0.5 + 0.5 * u_ModelLightDir.y;
		}
		break;

		case TCGEN_VECTOR:
		{
			tex = vec2(dot(position, TCGenVector0), dot(position, TCGenVector1));
		}
		break;
	}

	return tex;
}
#endif

#if defined(USE_TCMOD)
vec2 ModTexCoords(vec2 st, vec3 position, vec4 texMatrix, vec4 offTurb)
{
	float amplitude = offTurb.z;
	float phase = offTurb.w * 2.0 * M_PI;
	vec2 st2;
	st2.x = st.x * texMatrix.x + (st.y * texMatrix.z + offTurb.x);
	st2.y = st.x * texMatrix.y + (st.y * texMatrix.w + offTurb.y);

	vec2 offsetPos = vec2(position.x + position.z, position.y);

	vec2 texOffset = sin(offsetPos * (2.0 * M_PI / 1024.0) + vec2(phase));

	return st2 + texOffset * amplitude;
}
#endif

#if defined(USE_RGBAGEN)
vec4 CalcColor(vec3 position, vec3 normal)
{
	vec4 color = u_VertColor * attr_Color + u_BaseColor;

	if (u_ColorGen == CGEN_LIGHTING_DIFFUSE)
	{
		float incoming = clamp(dot(normal, u_ModelLightDir), 0.0, 1.0);

		color.rgb = clamp(u_DirectedLight * incoming + u_AmbientLight, 0.0, 1.0);
	}
	else if (u_ColorGen == CGEN_DISINTEGRATION_1)
	{
		vec3 delta = u_Disintegration.xyz - position;
		float sqrDistance = dot(delta, delta);
		if (sqrDistance < u_Disintegration.w)
		{
			color *= 0.0;
		}
		else if (sqrDistance < u_Disintegration.w + 60.0)
		{
			color *= vec4(0.0, 0.0, 0.0, 1.0);
		}
		else if (sqrDistance < u_Disintegration.w + 150.0)
		{
			color *= vec4(0.435295, 0.435295, 0.435295, 1.0);
		}
		else if (sqrDistance < u_Disintegration.w + 180.0)
		{
			color *= vec4(0.6862745, 0.6862745, 0.6862745, 1.0);
		}
		return color;
	}
	else if (u_ColorGen == CGEN_DISINTEGRATION_2)
	{
		vec3 delta = u_Disintegration.xyz - position;
		float sqrDistance = dot(delta, delta);
		if (sqrDistance < u_Disintegration.w)
		{
			return vec4(0.0);
		}
		return color;
	}

	vec3 localOrigin = (inverse(u_ModelMatrix) * vec4(u_ViewOrigin, 1.0)).xyz;
	vec3 viewer = localOrigin - position;

	if (u_AlphaGen == AGEN_LIGHTING_SPECULAR)
	{
		vec3 lightDir = normalize(vec3(-960.0, 1980.0, 96.0) - position);
		vec3 reflected = -reflect(lightDir, normal);

		color.a = clamp(dot(reflected, normalize(viewer)), 0.0, 1.0);
		color.a *= color.a;
		color.a *= color.a;
	}
	else if (u_AlphaGen == AGEN_LIGHTING_SPECULAR_STATIC)
	{
		vec3 reflected = -reflect(u_ModelLightDir, normal);

		color.a = clamp(dot(reflected, normalize(viewer)), 0.0, 1.0);
		color.a *= color.a;
		color.a *= color.a;
	}
	else if (u_AlphaGen == AGEN_PORTAL)
	{
		color.a = clamp(length(viewer) / u_PortalRange, 0.0, 1.0);
	}

	return color;
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
	var_SlicePos = wsPos;
	var_SliceD = vec4(-1000.0);
	if (u_SliceCount <= 0)
		return;

	// the bone this vertex mostly follows
	uint bone = attr_BoneIndexes[0];
	float w = attr_BoneWeights[0];
	if (attr_BoneWeights[1] > w) { bone = attr_BoneIndexes[1]; w = attr_BoneWeights[1]; }
	if (attr_BoneWeights[2] > w) { bone = attr_BoneIndexes[2]; w = attr_BoneWeights[2]; }
	if (attr_BoneWeights[3] > w) { bone = attr_BoneIndexes[3]; }
	uint bits = (u_BoneSlice[bone >> 4u][(bone >> 2u) & 3u] >> ((bone & 3u) * 8u)) & 255u;

	var_SliceD.x = SliceValue(0, bits & 3u, wsPos);
	gl_ClipDistance[0] = -var_SliceD.x;
	if (u_SliceCount > 1)
	{
		var_SliceD.y = SliceValue(1, (bits >> 2u) & 3u, wsPos);
		gl_ClipDistance[1] = -var_SliceD.y;
	}
	if (u_SliceCount > 2)
	{
		var_SliceD.z = SliceValue(2, (bits >> 4u) & 3u, wsPos);
		gl_ClipDistance[2] = -var_SliceD.z;
	}
	if (u_SliceCount > 3)
	{
		var_SliceD.w = SliceValue(3, (bits >> 6u) & 3u, wsPos);
		gl_ClipDistance[3] = -var_SliceD.w;
	}
}
#endif

void main()
{
#if defined(USE_VERTEX_ANIMATION)
	vec3 position  = mix(attr_Position, attr_Position2, u_VertexLerp);
	vec3 normal    = mix(attr_Normal,   attr_Normal2,   u_VertexLerp);
	normal = normalize(normal - vec3(0.5));
#elif defined(USE_SKELETAL_ANIMATION)
	mat4x3 influence =
		GetBoneMatrix(attr_BoneIndexes[0]) * attr_BoneWeights[0] +
        GetBoneMatrix(attr_BoneIndexes[1]) * attr_BoneWeights[1] +
        GetBoneMatrix(attr_BoneIndexes[2]) * attr_BoneWeights[2] +
        GetBoneMatrix(attr_BoneIndexes[3]) * attr_BoneWeights[3];

    vec3 position = influence * vec4(attr_Position, 1.0);
    vec3 normal = normalize(influence * vec4(attr_Normal - vec3(0.5), 0.0));
#else
	vec3 position  = attr_Position;
	vec3 normal    = attr_Normal * 2.0 - vec3(1.0);
#endif

#if defined(USE_DEFORM_VERTEXES)
	position = DeformPosition(position, normal, attr_TexCoord0.st);
	normal = DeformNormal( position, normal );
#endif

	vec4 wsPosition = u_ModelMatrix * vec4(position, 1.0);
#if defined(USE_SKELETAL_ANIMATION)
	SliceClip(wsPosition.xyz);
#endif
	gl_Position = u_viewProjectionMatrix * wsPosition;

#if defined(USE_TCGEN)
	vec2 tex = GenTexCoords(u_TCGen0, position.xyz, normal, u_TCGen0Vector0, u_TCGen0Vector1);
#else
	vec2 tex = attr_TexCoord0.st;
#endif

#if defined(USE_TCMOD)
	var_DiffuseTex = ModTexCoords(tex, position, u_DiffuseTexMatrix, u_DiffuseTexOffTurb);
#else
    var_DiffuseTex = tex;
#endif

	if ( u_FXVolumetricBase >= 0.0 )
	{
		vec3 viewForward = u_ViewForward.xyz;
		float d = clamp(dot(normalize(viewForward), normalize(normal)), 0.0, 1.0);
		d = d * d;
		d = d * d;

		var_Color = vec4(u_FXVolumetricBase * (1.0 - d));
	}
	else
	{
#if defined(USE_RGBAGEN)
		var_Color = CalcColor(position.xyz, normal);
#else
		var_Color = u_VertColor * attr_Color + u_BaseColor;
#endif
	}
	
#if defined(USE_FLARE_TEST)
	vec4 samplePosition = u_viewProjectionMatrix * u_LightOrigin;
	vec3 depthPosition = samplePosition.xyz / samplePosition.w * 0.5 + 0.5;
	float depthSample = texture(u_ScreenDepthMap, depthPosition.xy).r;
	gl_Position *= float(depthPosition.z < depthSample);
#endif

#if defined(USE_FOG)
	var_WSPosition = wsPosition.xyz;
#endif
}


/*[Fragment]*/
#if defined(USE_FOG)
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

layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
};
#elif defined(USE_SKELETAL_ANIMATION)
// saber slice (SJE): the flat cap needs the camera position
layout(std140) uniform Camera
{
	mat4 u_viewProjectionMatrix;
	vec4 u_ViewInfo;
	vec3 u_ViewOrigin;
	vec3 u_ViewForward;
	vec3 u_ViewLeft;
	vec3 u_ViewUp;
};
#endif

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

uniform sampler2D u_DiffuseMap;
#if defined(USE_ALPHA_TEST)
uniform int u_AlphaTestType;
#endif

// x = glow out, y = deluxe, z = screen shadow, w = cube
uniform vec4 u_EnableTextures;

in vec2 var_DiffuseTex;
in vec4 var_Color;
#if defined(USE_SKELETAL_ANIMATION)
in vec3 var_SlicePos;	// saber slice (SJE): see the vertex stage
in vec4 var_SliceD;
#endif
#if defined(USE_FOG)
in vec3 var_WSPosition;
#endif

out vec4 out_Color;
out vec4 out_Glow;


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

	bool intersects = (t > 0.0 && t < 0.995);
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

#if defined(USE_SKELETAL_ANIMATION)
// saber slice: back faces of a cut model are only seen through the cut, draw them as molten metal
uniform int u_SliceMolten;
uniform sampler2D u_SliceCapsMap;
uniform sampler2D u_SliceBloodMap;
uniform sampler2D u_SliceGlowMap;

vec4 SliceMoltenColor(float alpha, vec2 uv)
{
	if (u_SliceHeat > 1.5)
	{
		// a metal body (super battle droid, heat + 2): white-hot grey metal that cools to dark steel
		float mheat = clamp(u_SliceHeat - 2.0, 0.0, 1.0);
		float grain;
		if ((u_SliceMolten & 4) != 0)
			grain = 0.7 + 0.6 * dot(texture(u_SliceCapsMap, uv * 3.0).rgb, vec3(0.299, 0.587, 0.114));
		else
			grain = 0.88 + 0.12 * sin(gl_FragCoord.x * 0.37 + gl_FragCoord.y * 0.29) * sin(gl_FragCoord.y * 0.21 - gl_FragCoord.x * 0.13);
		vec3 dark = vec3(0.07, 0.07, 0.075);
		vec3 grey = vec3(0.42, 0.40, 0.39);
		vec3 hot = vec3(1.9, 1.25, 0.75);
		vec3 metal = mheat > 0.5 ? mix(grey, hot, (mheat - 0.5) * 2.0) : mix(dark, grey, mheat * 2.0);
		return vec4(metal * grain, alpha);
	}
	float heat = clamp(u_SliceHeat, 0.0, 1.0);
	if ((u_SliceMolten & 4) != 0)
	{
		// DH's cap images (gfx/effects/saberslice_caps*): charred flesh with a little blood under it,
		// the glowing swirls fade out as the cut cools (same mix as the vanilla saberslice_inside shaders)
		vec2 t = uv * 3.0;
		vec3 caps = texture(u_SliceCapsMap, t).rgb;
		vec3 blood = texture(u_SliceBloodMap, t * 0.75 + vec2(0.31, 0.17)).rgb;
		vec3 glow = texture(u_SliceGlowMap, t).rgb;
		vec3 base = caps * 1.2 + blood * 0.35;
		return vec4(base * (1.0 - 0.4 * heat) + glow * (2.2 * heat), alpha);
	}
	vec3 hot = vec3(2.4, 0.9, 0.2);
	vec3 cool = vec3(0.45, 0.07, 0.015);
	float grain = 0.88 + 0.12 * sin(gl_FragCoord.x * 0.37 + gl_FragCoord.y * 0.29) * sin(gl_FragCoord.y * 0.21 - gl_FragCoord.x * 0.13);
	return vec4(mix(cool, hot, heat) * grain, alpha);
}

// SJE (r_saberSliceFlatCap, u_SliceMolten & 8): a back face seen through a cut is drawn as the flat cap the
// ray passes through on its way in. For a closed body, the view ray from the camera to this fragment entered
// the kept part where it crossed the last cut plane facing the camera; that point is shaded instead, with the
// cut texture laid flat on the plane, so the cut reads as a solid flat face instead of a hollow shell. The
// pixels are the same ones the back faces cover, so no depth or stencil work is needed (the depth stays the
// back face's: other models passing inside the cut body are the only case that could tell).
bool SliceCapPoint(out vec3 capPos, out vec3 capN)
{
	vec3 C = u_ViewOrigin;
	float best = -1.0;
	capN = vec3(0.0, 0.0, 1.0);
	for (int c = 0; c < 4; c++)
	{
		if (c >= u_SliceCount)
			break;
		float sP = var_SliceD[c];
		if (abs(sP) > 500.0)
			continue;	// not a plane cut on this part of the body
		float sC = (dot(u_SlicePlanes[c].xyz, C) - u_SlicePlanes[c].w) * u_SliceSign[c];
		if (sC <= 0.0 || sC - sP < 0.0001)
			continue;	// the camera is on the kept side of this cut: not the face we look through
		float t = sC / (sC - sP);
		if (t > best)
		{
			best = t;
			capN = u_SlicePlanes[c].xyz * u_SliceSign[c];
		}
	}
	capPos = C + (var_SlicePos - C) * clamp(best, 0.0, 1.0);
	capN = normalize(capN);
	return best > 0.0;
}

vec4 SliceCapColor(float alpha, vec2 uv)
{
	vec3 X, N;
	if (!SliceCapPoint(X, N))
		return SliceMoltenColor(alpha, uv);
	vec3 a = normalize(cross(N, abs(N.z) < 0.9 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0)));
	vec3 b = cross(N, a);
	vec4 col = SliceMoltenColor(alpha, vec2(dot(X, a), dot(X, b)) * (1.0 / 40.0));
	// a flat face: a little darker seen edge-on
	col.rgb *= 0.65 + 0.35 * abs(dot(N, normalize(u_ViewOrigin - X)));
	return col;
}

// SJE (r_saberSliceSeam, u_SliceMolten & 16): a glowing cauterised band on the body's own surface along the
// cut edge, hot orange (metal: white hot) fading to nothing as the cut cools
vec3 SliceSeam()
{
	float d = 1000.0;
	for (int c = 0; c < 4; c++)
	{
		if (c >= u_SliceCount)
			break;
		d = min(d, -var_SliceD[c]);	// distance from the cut on the kept side
	}
	const float width = 1.6;
	if (d >= width)
		return vec3(0.0);
	float k = 1.0 - max(d, 0.0) / width;
	k *= k;
	if (u_SliceHeat > 1.5)
	{
		float mheat = clamp(u_SliceHeat - 2.0, 0.0, 1.0);
		return vec3(2.2, 1.7, 1.2) * (k * mheat);
	}
	float heat = clamp(u_SliceHeat, 0.0, 1.0);
	return mix(vec3(0.6, 0.06, 0.0), vec3(2.8, 1.2, 0.35), heat) * (k * heat);
}
#endif

void main()
{
	vec4 color  = texture(u_DiffuseMap, var_DiffuseTex);
	color *= var_Color;
#if defined(USE_ALPHA_TEST)
	if (u_AlphaTestType == ALPHA_TEST_GT0)
	{
		if (color.a == 0.0)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_LT128)
	{
		if (color.a >= 0.5)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE128)
	{
		if (color.a < 0.5)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE192)
	{
		if (color.a < 0.75)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_E255)
	{
		if (color.a < 1.00)
			discard;
	}
	else if (u_AlphaTestType == ALPHA_TEST_GE128_A2C || u_AlphaTestType == ALPHA_TEST_GE192_A2C)
	{
		// SJE: alpha-to-coverage - sharpen the alpha around the cut-off so the edge fades over about one pixel
		// and becomes MSAA sample coverage (anti-aliased foliage / fences) instead of a hard jagged discard
		float cutoff = (u_AlphaTestType == ALPHA_TEST_GE128_A2C) ? 0.5 : 0.75;
		color.a = (color.a - cutoff) / max(fwidth(color.a), 0.0001) + 0.5;
		if (color.a <= 0.0)
			discard;
		color.a = clamp(color.a, 0.0, 1.0);
	}
#endif

#if defined(USE_FOG)
	Fog fog = u_Fogs[u_FogIndex];
	vec4 fogColorOpacity = CalcFog(u_ViewOrigin, var_WSPosition, fog);
#if defined(USE_VOLUMETRIC_FOG)
	color.rgb *= 1.0 - u_FogColorMask.a * fogColorOpacity.a;
	color.rgb += u_FogColorMask.a * fogColorOpacity.rgb;
	color.rgb *= vec3(1.0) - u_FogColorMask.rgb * fogColorOpacity.a;
#else
	color *= vec4(1.0) - u_FogColorMask * fogColorOpacity.a;
#endif
#endif

#if defined(USE_SKELETAL_ANIMATION)
	if (((u_SliceMolten & 3) == 1 && gl_FrontFacing) || ((u_SliceMolten & 3) == 2 && !gl_FrontFacing))
		color = (u_SliceMolten & 8) != 0 ? SliceCapColor(color.a, var_DiffuseTex) : SliceMoltenColor(color.a, var_DiffuseTex);
	else if ((u_SliceMolten & 16) != 0)
		color.rgb += SliceSeam();
#endif

	out_Color = color;
	out_Glow = mix(vec4(0.0, 0.0, 0.0, color.a), color, u_EnableTextures.x);
}
