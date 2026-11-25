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
		   most (20 m/s^2 is about 790).
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

SJE: heavy-cape options (each off unless given):
  damp H V  0..0.9 share of the cloth's speed taken away per 16 ms,
		   sideways (H) and up-down (V), measured in a frame that travels with
		   the character over the ground (not up and down) but does not turn
		   with him, on top of the solver's own damping: a swing dies out
		   instead of wobbling, his run is not damped, a turn still flares the
		   cloth, in a jump it lags and settles by its own weight.
  ring D   second tether level: every point more than D units (along the
		   cloth) below the attached edge is tied, with no stretch, to the
		   nearest point of the row D units down, so the lower cloth swings
		   round that ring as one piece.
  bell B   0..1 the free cloth's animated spot (what 'tether' and 'spring'
		   measure from) is its bind shape carried by the hip bone alone - a
		   rigid bell that turns with the hips, not with the legs - blended in
		   by B x the point's freedom (0 at the attached edge).
  spring S A  pull per 16 ms toward the animated spot: S at the attached
		   edge, S x exp(-A x metres along the cloth) further down (one metre =
		   39.37 units). Spots under the floor are lifted onto it first. It
		   lets go while the wearer runs (from 100 units/s on, fully at 190),
		   jumps or falls, and takes hold again slowly after a run.
  wrap     (no value; cloth on a grid, 'proxy N', only) every drawn vertex
		   is bound to its nearest grid triangles (up to 4) in the bind pose
		   and drawn from their blend: the hem and the grid points are drawn
		   where the grid has them (otherwise a blend of nearby grid points,
		   smoothed).

Optional (SJE 2026-11-01): r_meshClothSleep 1 (default) lets cloth at rest sleep until the pose, the
body, wind or a blast moves it; r_meshClothSelfSub 1 (default) solves self-collision in every substep,
with cloth-on-cloth friction; r_meshClothBendModel 1 uses isometric (cotangent) bending instead of bend
links; r_meshClothAero 1 pushes each triangle with the air (drag + lift) instead of each point.

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
	// 3 = body shape fitted from the mesh (MC_BodyShapesPose, r_meshClothBodyFit): pushes hanging cloth fully out,
	//     no edge or drawn-point pushes
	// 4 = SJE: a thing held in the hand (saber hilt, lit blade): as 1, and cloth is never taken to be modelled
	//     round it (as a sleeve is round an arm), so it is always kept clear of it
};

const int MC_MAX_BLADE_AIR = 4;	// SJE r_meshClothBladeAir: lit blades whose air one character's cloth feels

struct mcParams_t {
	float	gravity;		// units / s^2
	float	drag;			// air resistance along the cloth normal
	int		iterations;		// constraint passes per step
	float	shapeScale;		// multiplies each surface's shape value
	float	inertia;		// 0..1 how much of the character's own travel the cloth feels
	float	maxAccel;		// units/s^2 of character acceleration the cloth feels (0 = no limit)
	float	airDrag;		// 1/s drag on all motion relative to the air: capes trail when running and lift when falling
	int		layers = 0;		// SJE r_meshClothLayers: 1 = a character's cloth layers keep their order (a cape stays over a robe); 2 = without the body tests
	float	twistFree = 540.0f;	// SJE r_meshClothTwistFree: deg/s of the attached edge's turn about up that the cloth lags by itself
	float	twistLag = 20.0f;	// SJE r_meshClothTwistLag: degrees the free cloth may be behind a faster turn before it is carried along
							// (under 0 = carried at once, wherever the cloth is: as before). For a flick: in a turn that goes on
							// in one direction the allowance runs out (MC_TWIST_FLICK0 / 1).
	float	twistCarry = 1.0f;	// SJE r_meshClothTwistCarry: the part of the excess that is carried (1 = all)
	int		resumeMs = 0;		// SJE r_meshClothResume: cloth not simulated for up to this long carries on where it was, moved with its
							// character, instead of settling anew from the pose (0 = off)
	float	settle = 0.0f;		// SJE r_meshClothSettle: cloth that starts over (first seen, back in view, back in range) used to settle
							// with MC_SETTLE_STEPS steps in that one frame (30 ms for a cape, up to 160 ms for a robe). Over 0: at
							// most this many of those steps per 16 ms for a cloth of 200 points (heavier cloth fewer), over the
							// next frames; meanwhile the cloth is drawn in its animated pose, then eased into the simulation.
							// 0 = all at once, as before
	int		settleJobs = 1;		// ... and the number of job workers: more cloth settling at once than they take without the frame
							// growing shares the frame's steps
	float	bodyCarry = 1.0f;	// SJE r_meshClothBodyCarry: over 0 = the cloth is carried along with the body's own fast travel
							// inside its animation (a landing brings the body down 40-65 units in a few frames)
	float	dragQuad = 0.0f;	// SJE r_meshClothDragQuad: air resistance through the cloth that grows with the square of the
							//   speed (1/unit), as real air does: a cape streams out behind a runner and falls back
							//   when he stops, instead of lying against him at any speed. 0 = off (linear drag only)
	float	wind[3];		// world wind velocity, units/s (still air = 0 0 0)
	float	zoneWind[3] = { 0.0f, 0.0f, 0.0f };	// SJE r_meshClothFans: the air of the pushers (fans, jump pads, air streams)
							//   the character is in or was just thrown by (world, units/s; MC_ZoneAir). Added to the wind,
							//   but not varied by 'gust': it is made to outrun the push by a set amount
	float	gust;			// 0..1 how much the wind strength varies over time
	float	flutter;		// 0..1 how much the wind ripples across the cloth (r_meshClothFlutter; 0 = it pushes it evenly)
	float	timeSec;		// current time for gusts (filled in by MC_Apply)
	int		xpbd = 0;		// r_meshClothXPBD: 1 = XPBD constraints (compliance, substep independent), 0 = old projection
	int		xpbdIters = 1;	// r_meshClothXPBDIter: XPBD solver passes per substep
	float	frictionScale = 1.0f;	// r_meshClothFriction: multiplies every surface's friction (0 = frictionless)
	float	liftLimit = -2.0f;	// r_meshClothLift: overrides every surface's 'lift': -2 = use the config, -1 = no limit, N = units
	float	liftAir = 1.0f;		// SJE r_meshClothLiftAir: air from below (a fall, an updraft) and strong wind open the lift limit
							// behind the attached edge, so a cape's hem may flip up (0 = closed, as before)
	float	liftSlope = 0.3f;	// SJE r_meshClothLiftSlope: the lift limit drops by this much per unit of cloth length from the
							// attached edge (0 = level with it, as before): no flat sheet at shoulder height in a jump
	float	breeze = 1.0f;	// SJE r_meshClothBreeze: a light, slowly wandering breeze sways hanging cloth near the camera
							// (1 = about a unit at the hem of a long cape, 0 = off: still air, the cloth hangs frozen)
	float	billow = 1.0f;	// SJE r_meshClothBillow: a cape in the air stream of its wearer's travel fills out (pushed
							// outwards, most a third of the way down its length; 0 = off: only dragged straight back)
	float	wornAir = 0.25f;	// SJE r_meshClothWornAir: cloth worn round the body (a robe, a skirt: it wraps its wearer, who
							//   stands in the wind's way) and loose layers under it catch at most this part of the travel
							//   wind (dragQuad, billow), whatever their tether; 1 = all of it, as before
	int		shown = 1;		// SJE r_meshClothShown: 1 = what the character's skin has switched off takes no part: the body
							//   shapes are fitted to the switched-on surfaces only, and switched-off cloth surfaces hold no
							//   drawn cloth through the layer rule; 0 = as before (the model's own flags, all cloth)
	int		carryFloor = 1;	// SJE r_meshClothCarryFloor: 1 = the body carry (bodyCarry) takes cloth worn round the body (a robe,
							//   a skirt) and loose layers under it down only as far as its lowest point has room above the
							//   floor: not into the ground when its wearer drops into a crouch; 0 = as before
	float	noRoom = 1.0f;	// SJE r_meshClothNoRoom: over 0 = cloth worn round the body (a robe, a skirt) and loose layers
							//   under it take the shape their wearer's pose gives them while they have no room to hang
							//   (crouching, kneeling, sitting, lying: the attached edge is nearer the floor than the cloth is
							//   long); the value scales the pull. 0 = as before
	int		garment = 1;	// SJE r_meshClothGarment: 1 = a body part that is itself a skirt (a tube round both legs, not made
							//   cloth: an inner robe) takes no part in the body fit - stiff, it swings with the legs and its
							//   fitted shapes shook the cloth worn over it at a run; 0 = as before
	int		shoulders = 1;	// SJE r_meshClothShoulders: 1 = the drawn cloth is kept over what the shoulder bones carry
							//   (a shoulder plate turns with the arm, into the cape lying over it); with faceCol only
	int		overArms = 1;	// SJE r_meshClothOverArms: 1 = cloth that hangs from shoulder height (a cape, a cloak) lies over
							//   arms and hands: a hand put behind the back stays under it; with faceCol only
	int		edgeCol = 0;	// r_meshClothEdgeCol: 1 = cloth edges are also pushed off arms, fists and hilts
	int		faceCol = 0;	// r_meshClothFaceCol: 1 = the cloth's triangles (not only their corners) are kept out of the whole
							//   body and on their own side of it (replaces edgeCol: the triangles' edges are tested too)
	int		sleepShown = 1;	// SJE r_meshClothSleepShown: 1 = cloth the skin has switched off (shown) does not keep the rest awake
	int		armGap = 1;		// SJE r_meshClothArmGap: 1 = a group whose lines have 'armgap G' is kept G further off the arms (0 = ignored)
	int		limbLie = 1;	// SJE r_meshClothLimbLie: 1 = cloth lying against a still arm is not thrown as 'gone through' it
	int		inside = 1;		// SJE r_meshClothInside: 1 = in strong wind a loose layer the model has under worn cloth (tunic strips under a
							//   robe) stays inside that cloth: blown out through it, it is put back; 0 = as before
	int		sleep = 0;		// r_meshClothSleep: 1 = cloth at rest on a character standing still stops simulating until something moves it
	int		selfSub = 0;	// r_meshClothSelfSub: 1 = self-collision ('self' surfaces) in every substep, with cloth-on-cloth friction
	int		rest = 1;		// SJE r_meshClothRest: 1 = cloth lying on a body that holds still is left to rest by the triangle test:
							//   no throw 'back to its own side' for cloth that lay clear on a shape's far side when the step began,
							//   and cloth the animation partly holds is not held further off an arm than the pose drapes it
	int		ease = 1;		// SJE r_meshClothEase: 1 = a point the shake watchdog let go comes back gently: while the pose holds it
							//   still the body's triangle test moves it by a small step per call, not at full strength (no throw
							//   of several units every few seconds where two body shapes dispute a piece of cloth)
	int		calm = 1;		// SJE r_meshClothCalm: 1 = cloth on a body that holds still comes to rest: a self-collision pair that
							//   shakes loses its closing speed (not where a limb moves the cloth), and a point that only
							//   trembles in place counts as at rest. 0 = off
	int		bendModel = 0;	// r_meshClothBendModel: 0 = bend links (default), 1 = isometric (cotangent) bending
	float	fine = 0.0f;	// SJE r_meshClothFine: self-colliding cloth (a cape, a cloak) whose spacing (the median length of
							//   its edges, units) is under this counts as fine. Then (1) with bendModel 0 its loose sheets bend
							//   by their curvature - bend links hold less the finer the mesh is - (2) the layer rule measures a
							//   heap pose by the cloth area under the floor, not by the points, and (3) its self-collision keeps
							//   two points only as far apart as the pose lays them, a touching pair losing its closing speed.
							//   0 = off (as before)
	int		aero = 0;		// r_meshClothAero: 1 = the air pushes each triangle (drag + lift, area-weighted) instead of each point
	int		floor;			// 1: floorPt / floorN hold the floor plane under the character (world)
	float	floorPt[3];
	float	floorN[3];
	int		numWalls = 0;	// r_meshClothWalls: walls round the character (world planes the cloth stays in front of)
	float	wallPt[8][3];
	float	wallN[8][3];
	float	lodDist;		// distance level of detail: the character's distance from the camera
	float	lodNear;		//   (scaled by the field of view). Beyond lodNear: half the constraint passes,
	float	lodFar;			//   updated at most every 32 ms. Beyond lodFar: not simulated. 0 = no limit.
	// water and rain (r_meshClothWater, r_meshClothRain); zero = dry, as before
	int		water = 0;		// 1: the character stands in water up to waterZ (world height)
	float	waterZ = 0.0f;
	float	waterDrag = 0.0f;	// 1/s drag on cloth below waterZ; it also floats up gently there
	float	wet = 0.0f;		// 0..1 how soaked the cloth is: wet cloth weighs up to twice as much
	int		hipBone = -1;	// SJE: the skeleton's hip bone (pelvis), for cloth lines with 'bell'; -1 = none
	float	turb = 1.0f;	// SJE r_meshClothTurbulence: the air a character moves through, or that blows past him, is not one even
						//   stream: its direction wanders, and eddies travel down the cloth with it (random, smooth, each character
						//   its own), so the same move does not stream the cloth the same way every time. 0 = off (as before)
	float	bladeAir = 1.0f;	// SJE r_meshClothBladeAir: a lit blade swung near cloth drags the air beside it along (0 = off)
	int		numBladeAir = 0;	// ... the blades near this character: their ends now and how fast those move (world, units/s)
	float	bladeA[MC_MAX_BLADE_AIR][3], bladeB[MC_MAX_BLADE_AIR][3], bladeVA[MC_MAX_BLADE_AIR][3], bladeVB[MC_MAX_BLADE_AIR][3];
	float	dampLag = 0.3f;	// SJE r_meshClothDampLag: seconds by which the frame 'damp' works in follows the character's speed (the
						//   air he drags along): after a stop or a change of direction the cape swings out. 0 = his speed at once
	float	springPose = 1.0f;	// SJE r_meshClothSpringPose: 'spring' lets go at once when the pose itself jumps (an attack, a change of
						//   animation) and takes hold again slowly: the cape swings on from where it is. 0 = it is pulled after the pose
	float	tailSide = 1.0f;	// SJE r_meshClothTailSide: with the back to a strong wind the cape goes off to one side and streams
						//   past the body there (it tries the lee side first, then the other). 0 = it lies on the back
	float	turnCalm = 1.0f;	// SJE r_meshClothTurnCalm: while the attached edge turns faster than a person turns the shoulders, 'spring'
						//   and 'ring' let go and 'damp' sideways grows: the cape is not whipped round with every flick of the mouse. 0 = off
	float	whirl = 1.0f;	// SJE r_meshClothWhirl: in a turn that goes on round and round (circling, a spin on the spot) the cloth is
						//   carried with its wearer's turning beyond what a body can do: it streams as in a run, it is not flung out level. 0 = off
	float	airPhys = 0.0f;	// SJE r_meshClothAirPhys: the air acts on each point by its own speed through it and by how the
						//   cloth lies to it (drag across the cloth by the square of the speed, next to none along it); the travel wind,
						//   the linear drag, the sink and landing rules, the lift limit and 'damp' fall away. 0 = off (those rules)
	float	freeCape = 0.0f;	// SJE r_meshClothFree: over 0 = a cloth line's 'free F' counts (with r_meshClothAirPhys): that cloth has no
						//   lift limit, a storm's gusts and eddies reach it, air along it drags it, wind from behind counts in full
	float	airFree = 1.0f;	// SJE r_meshClothAirFree: moving air (travel, a fall, wind) frees the cloth - its bend links and 'damp'
						//   give, the eddies are sized by the real air speed, a fall's air counts apart from the wind's, the lift
						//   limit opens from a slower fall, loose cloth sinks no faster than the air lets it. 0 = as before
};

// Per character (instanceKey), eased over time, for the glue (main thread):
//   outside: the weather wind fades in and out over ~0.4 s at a shelter edge instead of switching
//   wet:     wetTarget 0 = dry, 1 = in the rain (soaks in ~8 s), 2 = in water (soaked at once); dries in ~45 s
void MC_WeatherState(const void* instanceKey, int timeMs, bool outside, float wetTarget, float* outsideOut, float* wetOut);

// SJE r_meshClothStrongWind: strong weather wind - over MC_STRONG_WIND units/s, where the game has its characters brace
// against it (bg_panimate.cpp TorsoAgainstWindTest: R_GetWindGusting, R_IsOutside at the character's origin) - blows the
// cloth by the game's own outside test (at the origin, in the air or on the ground alike), at a strength the cloth takes
// without being torn into strips: MC_StrongWindSpeed(map speed, the cloth's wetness 0..1 - soaked cloth weighs twice as
// much, so from twice MC_STRONG_WIND on it gets twice the wind and is blown as clearly). MC_StrongWindOutside eases the
// game's outside test per character (key) over ~0.4 s, as MC_WeatherState does. Main thread.
const float MC_STRONG_WIND = 1000.0f;
float MC_StrongWindSpeed(float mapSpeed, float wet);
float MC_StrongWindOutside(const void* instanceKey, int timeMs, bool outside);

// SJE r_meshClothFans: wind zones - boxes of moving air (a map's pushers: fans, jump pads, air streams), told by the
// cgame through the renderer commands meshcloth_windzone / meshcloth_windzones_clear. Main thread.
// pushSpeed: the speed of the push the zone gives (its air runs ahead of it by the rest); under 0 = not known
void MC_WindZoneAdd(const float mins[3], const float maxs[3], const float air[3], float pushSpeed = -1.0f);
void MC_WindZonesClear(void);
int  MC_WindZoneCount(void);
// The air of the zones a character's box (world mins / maxs) touches (the strongest). A character that leaves them keeps their
// air for a moment, fading out (thrown up by a jump pad, the stream goes along for a short way), also when the set of
// zones changes meanwhile (MC_WindZonesClear forgets the zones only). Returns false when there is none (no zones and no
// character thrown: nothing is looked up). Main thread.
bool MC_ZoneAir(const void* instanceKey, int timeMs, const float mins[3], const float maxs[3], float air[3]);

// SJE r_meshClothLevelWind: a wind for one map from ext_data/meshcloth_wind.dat, one line per map:
//   mapname  x y z  [gust]  [outside]  [only]
// x y z: world wind, units/s; gust 0..1: how much its strength varies (replaces r_meshClothGust on that map);
// outside: only where the game counts as outside (R_IsOutside at the character's origin), eased in and out;
// only: the map's own weather wind does not blow the cloth there (for maps whose weather wind is wrong for cloth).
// '//' or '#' start a comment. MC_LevelWindParse finds the line for mapName (the bare name, e.g. "t1_rail") in text.
struct mcLevelWind_t {
	int		found = 0;
	float	wind[3] = { 0.0f, 0.0f, 0.0f };
	float	gust = -1.0f;	// under 0: not given
	int		outside = 0;
	int		only = 0;
};
void MC_LevelWindParse(const char* text, const char* mapName, mcLevelWind_t* out);

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

// SJE r_meshClothSettleCut: where a view of the level looks from, told once per view and frame, before that frame's
// MC_Prepare calls (main thread). A view that is near none of the views of the frame before is a camera cut (a
// cutscene's change of camera, a teleport, the first frame after a menu): cloth that starts over in that frame or
// the next one settles at once, as it did before r_meshClothSettle, up to an allowance - the frame of a cut hides
// the cost, and cloth seen changing from its pose into its hanging shape after every cut is what shows.
// mode 0 = off; jobs = the number of job workers (the allowance grows with them).
void MC_ViewCut(int frame, int timeMs, const float* viewOrigin, const float* viewForward, int mode, int jobs);

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
// SJE: scales (optional, one per character): the size each is drawn at, 1 = its own (r_meshClothShapeScale)
// SJE r_meshClothBlastGain: multiplies every MC_Blast (default 3; 1 = as before). Main thread.
void MC_SetBlastGain(float gain);
// SJE r_meshClothBladeAir: how fast a lit blade moves (base a, tip b; world). The glue tells every blade it finds near a
// character, once per frame (frameId: the renderer's frame count; timeMs: the scene time); it is matched to the blades
// told in the frame before. False (va, vb zero) for a blade not seen then, or when the time did not move. Main thread.
bool MC_BladeMotion(int frameId, int timeMs, const float a[3], const float b[3], float va[3], float vb[3]);
// SJE r_meshClothLee: the part of the weather wind that reaches a character's cloth (1 = all, down to MC_LEE_KEEP): a
// wall, a crate, a train car upwind of it shelters it. trace(from, to) gives the part of the line that is clear (0..1); it
// is called five times per character every 150 ms, and the result is eased per key. Main thread.
typedef float (*mcLeeTraceFn_t)(const float from[3], const float to[3]);
float MC_WindLee(const void* key, int timeMs, const float origin[3], const float wind[3], mcLeeTraceFn_t trace);
void MC_SetCharacters(int frameId, const float (*origins)[3], int count, const float* scales = nullptr);
bool MC_CharactersListed(int frameId);
int  MC_NearCharacters(const float origin[3], mcCapsule_t* out, int max);
const int MC_MAX_OTHERS = 4;
const int MC_MAX_HELD = 6;		// SJE: fitted shapes of the things bolted to a character (r_meshClothHeld)
const int MC_MAX_BLADES = 4;	// SJE: lit lightsaber blades near a character that push its cloth (r_meshClothBlade)
const int MC_MAX_CARRIED = 6;	// SJE: fitted shapes of the things a character carries drawn as models of their own (r_meshClothCarried)

// r_meshClothBodyFit: collision shapes fitted to the model's own (non-cloth) mesh for the cloth group of
// clothSurface: each torso, hip, leg, neck and head bone's vertices cut into height slices, each slice a "stadium"
// (flat front and back, round sides). boneClass[bone]: 0 = fit this bone, -1 = skip. Fitted on the first call per
// model. Main thread only. Returns a handle for MC_BodyShapesPose, or nullptr.
// SJE r_meshClothShown: 'drawn' = one byte per surface of the model (by its index): 1 = switched on for this
// character (nullptr: not known - the model's own flags decide, as before). One fit per set of them.
const void* MC_BodyFit(const void* clothSurface, const int* boneClass, int numBones, const unsigned char* drawn = nullptr, int numDrawn = 0, int garment = 0);
// SJE r_meshClothShown: tells a character's cloth which of the model's surfaces are switched on (after MC_Prepare,
// main thread; nullptr: all). Layer pairs with a switched-off cloth surface are skipped.
void MC_Shown(const void* clothSurface, const void* instanceKey, const unsigned char* drawn, int numDrawn);
// SJE r_meshClothFist: does this surface's cloth hang from shoulder height or above anywhere (a cape, a cloak)?
// Main thread, after MC_BodyFit.
bool MC_ClothHangsHigh(const void* clothSurface);
// Any thread: the fitted shapes posed by the bone matrices, in world space (hard 3). Returns the number written.
int MC_BodyShapesPose(const void* fit, mcBoneFn_t boneFn, void* boneCtx, const vec3_t origin, const vec3_t axis[3],
	mcCapsule_t* out, int maxOut);

// SJE: r_meshClothHeld: collision shapes fitted from the mesh of a thing bolted to a character (saber hilt, blaster,
// jet pack), per model. mdxmHeader: the bolted model's mesh header. Fitted on the first call (main thread only);
// returns a handle for MC_HeldShapesPose, or nullptr (nothing to fit, or not a held thing: longer than 64 units).
const void* MC_HeldFit(const void* mdxmHeader);
// Any thread: those shapes posed by the bolted model's own bone matrices, in world space (hard 4).
int MC_HeldShapesPose(const void* fit, mcBoneFn_t boneFn, void* boneCtx, const vec3_t origin, const vec3_t axis[3],
	mcCapsule_t* out, int maxOut);

// SJE: r_meshClothSubdiv: the point drawn in the middle of a cloth edge (pa pb, normals na nb, unit length) when its
// triangles are drawn split in four. It lies on the smooth curved patch through the corners and their normals (PN
// triangles, Vlachos, Peters, Boyd & Mitchell 2001): the edge's cubic Bezier curve has its middle at
// (pa + pb) / 2 - (wab na + wba nb) / 8 with wab = (pb - pa) . na, and the patch's quadratic normal there. Both are the
// same if the normals point the other way, so single-sided, double-sided and layered capes curve alike.
// SJE r_meshClothSubdivMean (mean != 0): the new point's normal is the mean of the two ends' normals. The curved
// patch's own normal mirrors the summed normals in the plane across the edge; that is right where the surface bends
// along the edge (the two normals lean apart or together), but where both lean the SAME way along the edge - cloth
// sheared in its plane, a narrow strip, a normal taken from stretched triangles - it leans the new point's normal the
// other way, and the surface is lit in light and dark patches that are not folds (seen on the front strips of a
// tunic). Where the surface does bend along the edge the two are the same. The new point's place is not changed.
inline void MC_EdgePoint(const float* pa, const float* na, const float* pb, const float* nb, float* p, float* n, const int mean = 0)
{
	const float e[3] = { pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2] };
	const float ns[3] = { na[0] + nb[0], na[1] + nb[1], na[2] + nb[2] };
	const float wab = e[0] * na[0] + e[1] * na[1] + e[2] * na[2];
	const float wba = -(e[0] * nb[0] + e[1] * nb[1] + e[2] * nb[2]);
	const float ee = e[0] * e[0] + e[1] * e[1] + e[2] * e[2];
	const float v = (!mean && ee > 1e-8f) ? 2.0f * (e[0] * ns[0] + e[1] * ns[1] + e[2] * ns[2]) / ee : 0.0f;
	for (int k = 0; k < 3; k++)
	{
		p[k] = 0.5f * (pa[k] + pb[k]) - 0.125f * (wab * na[k] + wba * nb[k]);
		n[k] = ns[k] - v * e[k];
	}
	const float l2 = n[0] * n[0] + n[1] * n[1] + n[2] * n[2];
	if (l2 > 1e-8f)
	{
		const float il = 1.0f / sqrtf(l2);
		n[0] *= il; n[1] *= il; n[2] *= il;
	}
	else
	{
		n[0] = na[0]; n[1] = na[1]; n[2] = na[2];
	}
}
