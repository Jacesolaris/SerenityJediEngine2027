/*[Vertex]*/
out vec2 var_ScreenTex;

void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
	var_ScreenTex = position * 0.5 + vec2(0.5);
}

/*[Fragment]*/
uniform sampler2D u_ScreenDepthMap;
uniform vec4 u_ViewInfo; // zfar / znear, zfar

in vec2 var_ScreenTex;

out vec4 out_Color;

vec2 poissonDisc[9] = vec2[9](
vec2(-0.7055767, 0.196515),    vec2(0.3524343, -0.7791386),
vec2(0.2391056, 0.9189604),    vec2(-0.07580382, -0.09224417),
vec2(0.5784913, -0.002528916), vec2(0.192888, 0.4064181),
vec2(-0.6335801, -0.5247476),  vec2(-0.5579782, 0.7491854),
vec2(0.7320465, 0.6317794)
);

// Input: It uses texture coords as the random number seed.
// Output: Random number: [0,1), that is between 0.0 and 0.999999... inclusive.
// Author: Michael Pohoreski
// Copyright: Copyleft 2012 :-)
// Source: http://stackoverflow.com/questions/5149544/can-i-generate-a-random-number-inside-a-pixel-shader

float random( const vec2 p )
{
  // We need irrationals for pseudo randomness.
  // Most (all?) known transcendental numbers will (generally) work.
  const vec2 r = vec2(
    23.1406926327792690,  // e^pi (Gelfond's constant)
     2.6651441426902251); // 2^sqrt(2) (Gelfond-Schneider constant)
  //return fract( cos( mod( 123456789., 1e-7 + 256. * dot(p,r) ) ) );
  return mod( 123456789., 1e-7 + 256. * dot(p,r) );
}

mat2 randomRotation( const vec2 p )
{
	float r = random(p);
	float sinr = sin(r);
	float cosr = cos(r);
	return mat2(cosr, sinr, -sinr, cosr);
}

float getLinearDepth(sampler2D depthMap, const vec2 tex, const float zFarDivZNear)
{
		float sampleZDivW = texture(depthMap, tex).r;
		return 1.0 / mix(zFarDivZNear, 1.0, sampleZDivW);
}

float ambientOcclusion(sampler2D depthMap, const vec2 tex, const float zFarDivZNear, const float zFar)
{
	float result = 0;

	float sampleZ = zFar * getLinearDepth(depthMap, tex, zFarDivZNear);

	vec2 expectedSlope = vec2(dFdx(sampleZ), dFdy(sampleZ)) / vec2(dFdx(tex.x), dFdy(tex.y));

	if (length(expectedSlope) > 5000.0)
		return 1.0;

	vec2 offsetScale = vec2(3.0 / sampleZ);

	mat2 rmat = randomRotation(tex);

	int i;
	for (i = 0; i < 9; i++)
	{
		vec2 offset = rmat * poissonDisc[i] * offsetScale;
		float sampleZ2 = zFar * getLinearDepth(depthMap, tex + offset, zFarDivZNear);

		if (abs(sampleZ - sampleZ2) > 20.0)
			result += 1.0;
		else
		{
			float expectedZ = sampleZ + dot(expectedSlope, offset);
			result += step(expectedZ - 1.0, sampleZ2);
		}
	}

	result *= 0.11111;

	return result;
}

// r_aoHorizon (KatarnEngine): horizon-based AO (GTAO-style). For a few directions around the pixel, find how far
// the nearby geometry rises above the surface's tangent plane within a world-space radius.
// u_ViewInfo = zfar/znear, zfar, proj[0][0], proj[1][1]
vec3 ViewPosAO(vec2 tex)
{
	float z = u_ViewInfo.y * getLinearDepth(u_ScreenDepthMap, tex, u_ViewInfo.x);
	vec2 ndc = tex * 2.0 - 1.0;
	return vec3(ndc.x * z / u_ViewInfo.z, ndc.y * z / u_ViewInfo.w, z);
}

float horizonAO(vec2 tex)
{
	vec3 P = ViewPosAO(tex);
	if (P.z > u_ViewInfo.y * 0.98) return 1.0;
	vec3 N = normalize(cross(dFdy(P), dFdx(P)));
	if (dot(N, P) > 0.0) N = -N;

	const float radius = 40.0;                  // game units
	vec2 texRadius = vec2(radius * u_ViewInfo.z, radius * u_ViewInfo.w) / P.z * 0.5;
	if (max(texRadius.x, texRadius.y) < 0.002) return 1.0;
	texRadius = min(texRadius, vec2(0.15));

	float jitter = random(tex) * 6.2831853;
	float occ = 0.0;
	const int DIRS = 6, STEPS = 6;
	for (int d = 0; d < DIRS; d++)
	{
		float a = jitter + float(d) * (3.14159265 / float(DIRS));
		vec2 dir = vec2(cos(a), sin(a));
		for (int side = -1; side <= 1; side += 2)
		{
			float best = 0.0;
			for (int s = 1; s <= STEPS; s++)
			{
				float t = (float(s) - fract(jitter * 7.0)) / float(STEPS);
				vec3 S = ViewPosAO(tex + dir * float(side) * texRadius * t);
				vec3 D = S - P;
				float dist = length(D);
				if (dist < 0.1) continue;
				float h = dot(D / dist, N) - 0.05;             // elevation above the surface plane
				float fall = clamp(1.0 - dist / radius, 0.0, 1.0);
				best = max(best, h * fall);
			}
			occ += clamp(best, 0.0, 1.0);
		}
	}
	occ /= float(DIRS * 2);
	return clamp(1.0 - occ * 1.2, 0.25, 1.0);
}

void main()
{
	float result = (u_ViewInfo.z > 0.0) ? horizonAO(var_ScreenTex) : ambientOcclusion(u_ScreenDepthMap, var_ScreenTex, u_ViewInfo.x, u_ViewInfo.y);

	out_Color = vec4(vec3(result), 1.0);
}
