/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// r_ssr: screen space reflections of the finished 3D view (2026-11-03), ReShade style:
// works only with what is on screen (colour + depth), normals come from the depth buffer.
// Each pixel marches its reflected ray in screen space (perspective-correct depth), refines
// the hit with a binary search and blends the colour found there by a Fresnel-like weight.
uniform sampler2D u_ScreenImageMap;	// copy of the finished view (tr.sharpenImage)
uniform sampler2D u_ScreenDepthMap;	// scene depth (tr.renderDepthImage)
uniform sampler2D u_SsrMaskMap;		// r_ssr material mask (Jace 2026-11-04): 1 = matte ground (grass, sand, mud...)

uniform vec4 u_SsrProjection;		// projection matrix [0] [5] [8] [9]
uniform vec4 u_SsrParams;			// projection matrix [10] [14], strength, max ray length (world units)
uniform vec4 u_SsrViewport;			// view x y width height in window pixels
uniform vec4 u_SsrUp;				// world up in view space (x right, y up, z forward), w = mode (1 floors, 2 all)

out vec4 out_Color;

#define SSR_STEPS		32
#define SSR_REFINE		5
#define DEPTHHACK_LIMIT	0.3		// depth values below this are the first person weapon (depth hack range)
#define SKY_DEPTH		0.99999

// positive view distance from a depth buffer value
float SceneZ(float d)
{
	return u_SsrParams.y / ((d * 2.0 - 1.0) + u_SsrParams.x);
}

vec3 ViewPos(vec2 pixel, float d)
{
	vec2 ndc = (pixel - u_SsrViewport.xy) / u_SsrViewport.zw * 2.0 - 1.0;
	float z = SceneZ(d);
	return vec3((ndc.x + u_SsrProjection.z) * z / u_SsrProjection.x,
				(ndc.y + u_SsrProjection.w) * z / u_SsrProjection.y, z);
}

vec2 ToPixel(vec3 p)
{
	vec2 ndc = vec2(u_SsrProjection.x * p.x / p.z - u_SsrProjection.z,
					u_SsrProjection.y * p.y / p.z - u_SsrProjection.w);
	return u_SsrViewport.xy + (ndc * 0.5 + 0.5) * u_SsrViewport.zw;
}

float DepthAt(ivec2 p)
{
	return texelFetch(u_ScreenDepthMap, p, 0).r;
}

vec3 PosAt(ivec2 p)
{
	return ViewPos(vec2(p) + 0.5, DepthAt(p));
}

// interleaved gradient noise (Jimenez 2014): breaks up step banding
float Dither(vec2 p)
{
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main()
{
	ivec2 ip = ivec2(gl_FragCoord.xy);
	vec3 base = texelFetch(u_ScreenImageMap, ip, 0).rgb;
	out_Color = vec4(base, 1.0);

	ivec2 lo = ivec2(u_SsrViewport.xy);
	ivec2 hi = lo + ivec2(u_SsrViewport.zw) - ivec2(1);

	float d = DepthAt(ip);
	if (d >= SKY_DEPTH || d < DEPTHHACK_LIMIT)
		return;

	// grass, sand, mud and other matte ground never reflect
	if (texelFetch(u_SsrMaskMap, ip, 0).r > 0.5)
		return;

	vec3 P = ViewPos(vec2(ip) + 0.5, d);

	// normal from the depth buffer: take the neighbour on each axis that lies closer
	// in depth, so silhouette edges do not bend the normal
	vec3 pl = PosAt(clamp(ip + ivec2(-1, 0), lo, hi));
	vec3 pr = PosAt(clamp(ip + ivec2( 1, 0), lo, hi));
	vec3 pd = PosAt(clamp(ip + ivec2( 0,-1), lo, hi));
	vec3 pu = PosAt(clamp(ip + ivec2( 0, 1), lo, hi));
	vec3 dx = (abs(pr.z - P.z) < abs(P.z - pl.z)) ? pr - P : P - pl;
	vec3 dy = (abs(pu.z - P.z) < abs(P.z - pd.z)) ? pu - P : P - pd;
	vec3 N = cross(dx, dy);
	float nlen = length(N);
	if (nlen < 1.0e-6)
		return;
	N /= nlen;
	if (dot(N, P) > 0.0)
		N = -N;

	// mode 1: only surfaces facing up (floors, water, wet ground)
	float surf = 1.0;
	if (u_SsrUp.w < 1.5)
		surf = smoothstep(0.7, 0.9, dot(N, u_SsrUp.xyz));
	if (surf <= 0.0)
		return;

	vec3 V = P / length(P);
	vec3 R = reflect(V, N);

	// rays pointing back at the camera leave the screen at once
	float facing = smoothstep(-0.25, 0.15, R.z);
	if (facing <= 0.0)
		return;

	// ray end: max length, kept in front of the camera
	float len = u_SsrParams.w;
	if (R.z < 0.0)
		len = min(len, (P.z - 2.0) / -R.z * 0.95);
	if (len <= 1.0)
		return;

	vec3 P0 = P + N * (0.5 + P.z * 0.002);
	vec3 P1 = P0 + R * len;
	vec2 s0 = ToPixel(P0);
	vec2 s1 = ToPixel(P1);
	float k0 = 1.0 / P0.z;
	float k1 = 1.0 / P1.z;

	// clip the screen segment to the view
	vec2 vmin = vec2(lo);
	vec2 vmax = vec2(hi) + 0.999;
	vec2 sd = s1 - s0;
	float tEnd = 1.0;
	if (s1.x > vmax.x) tEnd = min(tEnd, (vmax.x - s0.x) / sd.x);
	if (s1.x < vmin.x) tEnd = min(tEnd, (vmin.x - s0.x) / sd.x);
	if (s1.y > vmax.y) tEnd = min(tEnd, (vmax.y - s0.y) / sd.y);
	if (s1.y < vmin.y) tEnd = min(tEnd, (vmin.y - s0.y) / sd.y);
	tEnd = clamp(tEnd, 0.0, 1.0);
	s1 = s0 + sd * tEnd;
	k1 = mix(k0, k1, tEnd);

	if (length(s1 - s0) < 2.0)
		return;

	float jitter = Dither(gl_FragCoord.xy);
	float tPrev = 0.0;
	float tHit = -1.0;
	for (int i = 0; i < SSR_STEPS; i++)
	{
		float t = (float(i) + jitter) / float(SSR_STEPS);
		vec2 s = mix(s0, s1, t);
		float rayZ = 1.0 / mix(k0, k1, t);
		float sd2 = DepthAt(clamp(ivec2(s), lo, hi));
		if (sd2 >= DEPTHHACK_LIMIT)
		{
			float diff = rayZ - SceneZ(sd2);
			if (diff > 0.0 && diff < 8.0 + rayZ * 0.03)
			{
				tHit = t;
				break;
			}
		}
		tPrev = t;
	}

	if (tHit < 0.0)
		return;

	// binary search between the last miss and the hit
	float tLo = tPrev;
	float tHi = tHit;
	for (int j = 0; j < SSR_REFINE; j++)
	{
		float t = 0.5 * (tLo + tHi);
		float rayZ = 1.0 / mix(k0, k1, t);
		float sz = SceneZ(DepthAt(clamp(ivec2(mix(s0, s1, t)), lo, hi)));
		if (rayZ > sz)
			tHi = t;
		else
			tLo = t;
	}

	vec2 hitPixel = mix(s0, s1, tHi);
	float hitDepth = DepthAt(clamp(ivec2(hitPixel), lo, hi));
	if (hitDepth >= SKY_DEPTH || hitDepth < DEPTHHACK_LIMIT)
		return;

	vec3 refl = textureLod(u_ScreenImageMap, hitPixel / vec2(textureSize(u_ScreenImageMap, 0)), 0.0).rgb;

	// fade near the view edges and towards the end of the ray
	vec2 e = (hitPixel - u_SsrViewport.xy) / u_SsrViewport.zw;
	float edge = smoothstep(0.0, 0.08, min(e.x, 1.0 - e.x)) * smoothstep(0.0, 0.08, min(e.y, 1.0 - e.y));
	float tRay = tHi * tEnd;
	float distFade = 1.0 - tRay * tRay;

	// Fresnel-like: grazing angles reflect more
	float fresnel = mix(0.35, 1.0, pow(1.0 - clamp(dot(-V, N), 0.0, 1.0), 4.0));

	float w = clamp(u_SsrParams.z * surf * facing * edge * distFade * fresnel, 0.0, 1.0);
	out_Color = vec4(mix(base, refl, w), 1.0);
}
