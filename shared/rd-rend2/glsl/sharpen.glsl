/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// r_sharpen: contrast-adaptive sharpening of the finished 3D view (Warzone port, 2026-11-02).
// Algorithm: AMD FidelityFX RCAS ("Robust Contrast Adaptive Sharpening"),
// Copyright (c) 2021 Advanced Micro Devices, Inc., MIT licence (https://gpuopen.com/fsr).
// Warzone's fsrSharpen shader sampled with pixel coordinates and skipped the left
// neighbour in the min/max ring; this version fetches whole pixels and uses all four.
uniform sampler2D u_ScreenImageMap;	// copy of the finished view (tr.sharpenImage)
uniform float u_BloomStrength;		// reused uniform slot: sharpening strength 0..1 (r_sharpen)

out vec4 out_Color;

#define RCAS_LIMIT (0.25 - (1.0 / 16.0))

vec3 FetchPixel(ivec2 p, ivec2 hi)
{
	return texelFetch(u_ScreenImageMap, clamp(p, ivec2(0), hi), 0).rgb;
}

void main()
{
	ivec2 hi = textureSize(u_ScreenImageMap, 0) - ivec2(1);
	ivec2 ip = ivec2(gl_FragCoord.xy);

	//    b
	//  d e f
	//    h
	vec3 b = FetchPixel(ip + ivec2( 0, -1), hi);
	vec3 d = FetchPixel(ip + ivec2(-1,  0), hi);
	vec3 e = FetchPixel(ip, hi);
	vec3 f = FetchPixel(ip + ivec2( 1,  0), hi);
	vec3 h = FetchPixel(ip + ivec2( 0,  1), hi);

	// min and max of the ring
	vec3 mn4 = min(min(b, d), min(f, h));
	vec3 mx4 = max(max(b, d), max(f, h));

	// limiters: how much sharpening each channel takes before it would clip
	vec3 hitMin = mn4 / max(4.0 * mx4, vec3(1.0e-5));
	vec3 hitMax = (vec3(1.0) - mx4) / min(4.0 * mn4 - vec3(4.0), vec3(-1.0e-5));
	vec3 lobeRGB = max(-hitMin, hitMax);
	float lobe = max(-RCAS_LIMIT, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * clamp(u_BloomStrength, 0.0, 1.0);

	vec3 col = (lobe * (b + d + f + h) + e) / (4.0 * lobe + 1.0);
	out_Color = vec4(clamp(col, 0.0, 1.0), 1.0);
}
