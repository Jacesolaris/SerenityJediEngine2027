/*
===========================================================================
Mesh cloth: simulates the real vertices of chosen Ghoul2 surfaces (robes,
sleeves, hoods) instead of drawing extra cloth. Engine-neutral: the
renderer glue in tr_ghoul2.cpp supplies bone matrices, the entity
transform and body capsules; this module moves the free vertices.

All cloth surfaces of one model are simulated together as one cloth, so
pieces stitched to each other (robe + leg panels) can't separate. Any
cloth vertex that touches a non-cloth surface (torso robe, body) stays
attached, so those seams stay closed too.

Which surfaces are cloth, and which of their vertices stay attached, comes
from a config text (ext_data/meshcloth.dat), one line per surface:

  model  surface  rule...  [tether T]  [shape S]

  model    model folder name (models/players/<model>/model.glm)
  surface  surface name in the .glm, e.g. hips_robe
  rule     where the attached part is, in the model's bind pose (the
		   coordinates you see in the modelling tool: z up, feet at 0):
			 above   Zpin Zfree
					 attached at/above Zpin, fully free at/below Zfree
			 near    x y z  Rpin Rfree
					 attached within Rpin of the point, free beyond Rfree
			 segment x0 y0 z0  x1 y1 z1  Rpin Rfree
					 same, measured from a line (e.g. shoulder to wrist)
  tether   how far (game units) a fully free vertex may move away from
		   where the animation puts it. Default 12.
  bend     0..1 bending stiffness: 0 folds like silk, 1 holds its shape
		   like leather. Default 0.3.
  inertia  0..1 how much of the character's running and turning this
		   cloth feels (1 = all of it, a cape that flies out behind).
		   Default: the r_meshClothInertia console value.
  self     (no value) self-collision, so a single-layer cape can't fold
		   through itself. Leave off for layered robes and sleeves.
  smooth   0..1 smooths small creases out of the drawn cloth (visual only).
		   Default 0.5.
  maxaccel units/s^2: the character's acceleration the cloth feels, at
		   most (Battlefront II's MaxAcceleration; 20 m/s^2 is about 790).
		   0 = no limit. Default: the r_meshClothMaxAccel console value.
  maxspin  degrees/s: how fast the attached edge may turn before the cloth
		   is carried round with it instead of winding up (fast saber spins).
		   0 = no limit (default).
  spread   0..1 sideways-only pull toward the animated shape: keeps a
		   cape from rolling into a rope in saber spins without slowing
		   its swing front to back. Default 0.
  shape    0..1 optional pull back toward the modelled shape. Default 0
		   (real cloth: only the attached part follows the animation).
  stretch  0..1 XPBD stretch give: 0 = rigid fabric (default), 0.1-0.3 = a
		   little elastic (knits, leather straps). Needs r_meshClothXPBD 1.
  friction 0..1 kinetic friction against the body and the floor. Default 0.3.
  sfriction static friction: a contact sliding slower than sfriction x how
		   hard it is pressed in sticks (cloth rests on shoulders instead of
		   buzzing). Default 0.5. 0 = off.
  lift     units a free point may rise above the attached edge it hangs
		   from (stops capes flipping over the head on spawns and fast
		   turns). Default 2. 'lift off' = no limit: capes can blow up and
		   over the head (also needs a long enough tether).

The solver is XPBD (r_meshClothXPBD 1: compliance from bend/stretch,
r_meshClothXPBDIter passes per substep) or plain position-based dynamics
(r_meshClothXPBD 0), with substeps (r_meshClothIter
substeps per 16 ms), rigid stretch constraints, bending, long range
attachments, body capsules and an inertia scale (r_meshClothInertia).
===========================================================================
*/
#pragma once

struct mcCapsule_t {
	vec3_t	a, b;
	float	r;
	int		hard;	// 1 = limbs/hands: always push cloth fully out, even where the animated cloth is inside;
	// 2 = the same, for the shoulders (used by grid-simulated cloth only, so older capes keep their tuning)
};

struct mcParams_t {
	float	gravity;		// units / s^2
	float	drag;			// air resistance along the cloth normal
	int		iterations;		// constraint passes per step
	float	shapeScale;		// multiplies each surface's shape value
	float	inertia;		// 0..1 how much of the character's own travel the cloth feels
	float	maxAccel;		// units/s^2 of character acceleration the cloth feels (0 = no limit)
	float	airDrag;		// 1/s drag on all motion relative to the air: capes trail when running and lift when falling
	float	wind[3];		// world wind velocity, units/s (still air = 0 0 0)
	float	gust;			// 0..1 how much the wind strength varies over time
	float	flutter;		// 0..1 how much the wind ripples across the cloth (r_meshClothFlutter; 0 = it pushes it evenly)
	float	timeSec;		// current time for gusts (filled in by MC_Apply)
	int		xpbd = 0;		// r_meshClothXPBD: 1 = XPBD constraints (compliance, substep independent), 0 = old projection
	int		xpbdIters = 1;	// r_meshClothXPBDIter: XPBD solver passes per substep
	float	frictionScale = 1.0f;	// r_meshClothFriction: multiplies every surface's friction (0 = frictionless)
	float	liftLimit = -2.0f;	// r_meshClothLift: overrides every surface's 'lift': -2 = use the config, -1 = no limit, N = units
	int		edgeCol = 0;	// r_meshClothEdgeCol: 1 = cloth edges are also pushed off arms, fists and hilts
	int		floor;			// 1: floorPt / floorN hold the floor plane under the character (world)
	float	floorPt[3];
	float	floorN[3];
	float	lodDist;		// distance level of detail: the character's distance from the camera
	float	lodNear;		//   (scaled by the field of view). Beyond lodNear: half the constraint passes,
	float	lodFar;			//   updated at most every 32 ms. Beyond lodFar: not simulated. 0 = no limit.
	// water and rain (r_meshClothWater, r_meshClothRain); zero = dry, as before
	int		water = 0;		// 1: the character stands in water up to waterZ (world height)
	float	waterZ = 0.0f;
	float	waterDrag = 0.0f;	// 1/s drag on cloth below waterZ; it also floats up gently there
	float	wet = 0.0f;		// 0..1 how soaked the cloth is: wet cloth weighs up to twice as much
};

// Per character (instanceKey), eased over time, for the glue (main thread):
//   outside: the weather wind fades in and out over ~0.4 s at a shelter edge instead of switching
//   wet:     wetTarget 0 = dry, 1 = in the rain (soaks in ~8 s), 2 = in water (soaked at once); dries in ~45 s
void MC_WeatherState(const void* instanceKey, int timeMs, bool outside, float wetTarget, float* outsideOut, float* wetOut);

// Returns the 3x4 skinning matrix (model space) for a skeleton bone index.
typedef const float (*(*mcBoneFn_t)(void* ctx, int boneIndex))[4];

// Load (or reload) the surface list. Returns the number of entries.
int  MC_LoadConfig(const char* text);

// Forget all cached meshes and cloth state (call when models are freed).
void MC_Clear(void);

// True if this surface is configured as cloth (cached per surface).
// surface is a const mdxmSurface_t* (passed opaquely: the .glm structs are
// unnamed types, which can't cross translation units in C++ signatures).
// modelName is the path the model was loaded from, e.g. "models/players/palpatine/model.glm"
// (SJE: the folder decides the config line, so stock models work whatever their internal name).
// altModelName (optional) is the .glm's internal name, tried if modelName's folder has no line.
bool MC_IsClothSurface(const void* surface, const char* modelName, const char* surfaceName, const char* altModelName = nullptr);

// Replace the free vertices of an already-skinned surface with simulated
// positions. xyz/normal point at the surface's first vertex in model space
// (stride 4 floats). origin/axis convert model space to world space.
// instanceKey identifies one character's copy of the model across frames.
// The first call for a model in a frame simulates all of its cloth
// surfaces (skinning the others through boneFn); later calls reuse it.
void MC_Apply(const void* surface, const void* instanceKey, int timeMs,
	const vec3_t origin, const vec3_t axis[3],
	const mcCapsule_t* capsulesWorld, int numCapsules,
	const mcParams_t* params,
	mcBoneFn_t boneFn, void* boneCtx,
	float (*xyz)[4], float (*normal)[4]);

// Drop instances not drawn for a while. Call about once a frame.
void MC_Prune(int timeMs);

// Threaded use (r_threads). MC_Apply = MC_Prepare + MC_Run.
//   MC_Prepare: main thread only (finds or creates the cloth state).
//   MC_Run:     any thread. All surfaces that share an instanceKey must run
//               on the same thread, in draw order; different instanceKeys
//               may run at the same time. It writes only that instance.
//   MC_Prune / MC_Clear / MC_Blast / MC_LoadConfig: main thread, never
//               while an MC_Run is going.
struct mcRun_t {
	void* instance;
	const void* group;
	int			surfIndex;
};

bool MC_Prepare(const void* surface, const void* instanceKey, int timeMs, mcRun_t* run);

void MC_Run(const mcRun_t* run, const void* surface, int timeMs,
	const vec3_t origin, const vec3_t axis[3],
	const mcCapsule_t* capsulesWorld, int numCapsules,
	const mcParams_t* params,
	mcBoneFn_t boneFn, void* boneCtx,
	float (*xyz)[4], float (*normal)[4]);

// Push all cloth near a point outward, like the blast wave of an explosion.
// strength: speed (units/s) given to cloth at the centre, fading to 0 at radius.
// visible( from, to ): false when a wall is between them. Optional: without it a blast reaches cloth through walls.
typedef bool (*mcVisibleFn_t)(const float from[3], const float to[3]);
void MC_Blast(const float center[3], float strength, float radius, mcVisibleFn_t visible = nullptr);
// a force push (strength > 0) or pull (< 0) wave from center along dir: cloth within radius and inside the cone
// (cos of its half angle: coneDot) is kicked away from (pull: toward) center, less with distance. Main thread.
void MC_Push(const float center[3], const float dir[3], float strength, float radius, float coneDot);

// Other characters push cloth (r_meshClothOthers). The glue lists every character's origin once a frame
// (MC_SetCharacters; MC_CharactersListed says if this frame's list is there), then MC_NearCharacters
// gives up to max standing-body capsules (world space) of the characters close to one character. Main thread.
void MC_SetCharacters(int frameId, const float (*origins)[3], int count);
bool MC_CharactersListed(int frameId);
int  MC_NearCharacters(const float origin[3], mcCapsule_t* out, int max);
const int MC_MAX_OTHERS = 4;
