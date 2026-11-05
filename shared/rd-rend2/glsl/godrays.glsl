/*[Vertex]*/
void main()
{
	vec2 position = vec2(2.0 * float(gl_VertexID & 2) - 1.0, 4.0 * float(gl_VertexID & 1) - 1.0);
	gl_Position = vec4(position, 0.0, 1.0);
}

/*[Fragment]*/
// r_godRays: screen space light shafts (2026-11-03). Runs at half resolution: every pixel
// walks towards the sun's screen position and sums the bright sky it passes (sky = depth 1),
// so walls, pillars and characters in front of the sky cut dark gaps into the shafts.
// The result is added on top of the view (GPU Gems 3, ch. 13 style).
uniform sampler2D u_ScreenImageMap;	// copy of the finished view (tr.sharpenImage)
uniform sampler2D u_ScreenDepthMap;	// scene depth (tr.renderDepthImage)

uniform vec4 u_GodRaySun;			// sun position in window pixels xy, z = half-res target scale (window / target), w = ray length 0..1
uniform vec4 u_GodRayParams;		// brightness threshold, decay per sample, sun glow radius (pixels), unused

out vec4 out_Color;

#define GODRAY_SAMPLES	64
#define SKY_DEPTH		0.99999

float Dither(vec2 p)
{
	return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715))));
}

void main()
{
	ivec2 hi = textureSize(u_ScreenDepthMap, 0) - ivec2(1);
	vec2 pix = gl_FragCoord.xy * u_GodRaySun.z;
	vec2 delta = (u_GodRaySun.xy - pix) * u_GodRaySun.w / float(GODRAY_SAMPLES);
	float threshold = u_GodRayParams.x;
	float decay = u_GodRayParams.y;
	float glowRadius = u_GodRayParams.z;

	vec2 p = pix + delta * Dither(gl_FragCoord.xy);
	float illum = 1.0;
	float wsum = 0.0;
	vec3 sum = vec3(0.0);
	for (int i = 0; i < GODRAY_SAMPLES; i++)
	{
		ivec2 ip = clamp(ivec2(p), ivec2(0), hi);
		if (texelFetch(u_ScreenDepthMap, ip, 0).r >= SKY_DEPTH)
		{
			vec3 c = texelFetch(u_ScreenImageMap, ip, 0).rgb;
			// bright sky only, a little more around the sun itself
			vec3 bright = max(c - vec3(threshold), vec3(0.0)) / (1.0 - threshold);
			float glow = 1.0 + 2.0 * exp(-length(p - u_GodRaySun.xy) / glowRadius);
			sum += bright * glow * illum;
		}
		wsum += illum;
		illum *= decay;
		p += delta;
	}

	// normalised so a ray that sees only bright sky gives that sky's brightness
	out_Color = vec4(sum / wsum, 1.0);
}
