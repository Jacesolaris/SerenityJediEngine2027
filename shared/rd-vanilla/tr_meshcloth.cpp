/*
===========================================================================
Mesh cloth - see tr_meshcloth.h.

Per model LOD ("group", shared by every character using that model):
  surfaces   every cloth surface of the model, simulated together
  particles  vertices welded by bind position across all those surfaces,
			 so UV seams and stitched pieces can't tear apart
  freedom    0 = follows the animation exactly, 1 = fully free; vertices
			 touching a non-cloth surface are forced to 0 (closed seams)
  links      triangle edges (structural) and the opposite corners of
			 neighbouring triangles (bend); lengths come from the animated
			 pose each step, so the modelled folds are kept

Per character instance:
  particle positions (Verlet), fixed 16 ms steps, results per vertex.
===========================================================================
*/

#if !defined(_JK2EXE) && !defined(REND2_SP)	// MP renderers (vanilla and rend2)
#define MC_MP
#endif
#include "qcommon/q_shared.h"
#include "q_profile.h"
#ifdef MC_MP
#define MDXABONEDEF
#endif
#include "rd-common/mdx_format.h"
#include "rd-vanilla/tr_meshcloth.h"

#include <vector>
#include <queue>
#include <unordered_map>
#include <string>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <chrono>

#ifdef MC_DEBUG
#include <cstdlib>
int g_mcDbgTether = 0;
#define MCDBG_OFF( n ) ( getenv( n ) != nullptr )
#else
#define MCDBG_OFF( n ) false
#endif
namespace {
	const int	MC_STEP_MS = 16;
	const int	MC_LOD_MS = 32;		// mid-distance cloth (mcParams_t::lodNear) updates at most this often
	const float	MC_DT = 0.016f;
	const int	MC_MAX_STEPS = 2;		// SJE: was 4. A slow frame ran up to 4 steps, which made the next frame slower still
	// (frame rate collapsed with heavy capes). Past 2 steps the cloth just runs a little slow.
	const int	MC_STALE_MS = 250;
	const float	MC_TELEPORT = 120.0f;
	const int	MC_FORGET_MS = 10000;
	const float	MC_MARGIN = 0.6f;
	const float	MC_SWING_FREE = 240.0f;	// deg/s of attachment tilt the cloth lags by itself; faster is carried
	const float	MC_TWIST_FREE = 540.0f;	// deg/s of attachment turn about up the cloth lags by itself; faster is carried
	const float	MC_TWIST_FLICK0 = 150.0f;	// SJE: degrees of fast turn in one go up to which the cloth has its whole lag allowance
	const float	MC_TWIST_FLICK1 = 300.0f;	// ... and from which it has none (r_meshClothTwistLag: the allowance is for a flick, not a spin)
	const float	MC_FLOOR_GAP = 0.8f;		// units cloth keeps above the floor
	const float	MC_WELD = 0.01f;
	const int	MC_SETTLE_STEPS = 40;
	const int	MC_SHOW_STEPS = 16;		// SJE r_meshClothSettle: steps over which settled cloth is eased in from the animated pose
	// SJE r_meshClothSettleCut: a view is a camera cut when it is further than this from every view of the frame before:
	// MC_CUT_DIST units or MC_CUT_DEG degrees, more in a long frame (MC_CUT_*_MS per ms: a slow frame of a fast pan is
	// no cut). MC_CUT_ROOM: the cloth settled at once in the frames of one cut, in capes of MC_SETTLE_POINTS points
	// per job worker's room (about 100 ms of one thread); what comes after that settles over the next frames.
	const float	MC_CUT_DIST = 100.0f, MC_CUT_DEG = 30.0f, MC_CUT_DIST_MS = 6.0f, MC_CUT_DEG_MS = 3.0f, MC_CUT_DEG_MAX = 150.0f;
	const int	MC_CUT_GAP_MS = 250;
	const float	MC_CUT_ROOM = 6.0f;
	const int	MC_CUT_VIEWS = 6;
	const float	MC_SETTLE_POINTS = 200.0f;	// ... the cloth size its step count is given for (a step's cost grows with the size ^ 1.5)
	const int	MC_MAX_CAPS = 128;		// capsules a character may bring (limbs, hilts, others, fitted body shapes)
	const int	MC_NEAR_CAPS = 96;		// of those, the ones near the cloth
	const int	MC_REST_STEPS = 30;			// SJE r_meshClothSleep: 16 ms steps at rest (~0.5 s) before the cloth sleeps
	const float	MC_REST_SPEED = 0.03f;		//   at rest: no free point moved more than this in a step (~2 units/s)
	const float	MC_REST_SPAN = 0.6f;		//   SJE r_meshClothCalm: ... or all it did since the rest began fits in a box with this diagonal
	const float	MC_REST_DRIFT = 0.25f;		//   and the pose and body capsules moved less than this since the rest began
	const float	MC_DEFAULT_SPIN = 0.0f;		// degrees/s, 0 = no spin limit unless the config sets one		// 0.64 s of settling when a character appears	// bind-pose distance that counts as the same point

	enum mcRuleType_t { MC_RULE_ABOVE, MC_RULE_NEAR, MC_RULE_SEGMENT };

	struct mcDef_t {
		std::string		model, surface;
		mcRuleType_t	rule = MC_RULE_ABOVE;
		float			a[8] = {};
		float			tether = 12.0f;
		float			shape = 0.0f;
		float			bend = 0.3f;		// 0..1 bending stiffness (0 = limp silk, 1 = stiff leather)
		float			inertia = -1.0f;	// 0..1, or -1 to use r_meshClothInertia
		bool			self = false;		// self-collision (single-layer capes)
		float			smooth = 0.5f;		// 0..1 crease smoothing of the drawn shape
		float			maxAccel = -1.0f;	// units/s^2 of character acceleration the cloth feels, 0 = no limit, -1 = cvar
		float			maxSpin = -1.0f;	// degrees/s of the attached edge's turning the cloth feels, 0 = no limit, -1 = default
		float			spread = 0.0f;		// 0..1 sideways-only pull toward the animated shape (keeps a cape from roping)
		float			reach = 0.0f;		// >0: a point may stray from its animated spot by at most reach x its distance
		// along the cloth from the attached edge (on top of the tether); 0 = off
		float			turn = 0.0f;		// 0..1 fraction of the attached edge's turning the free cloth is carried along by
		// directly (angular inertia scale); the rest it feels as inertia. 0 = off
		float			thickness = 1.2f;	// cloth thickness in units: keeps it this far off the body (half) and off itself (full)
		float			weight = 1.0f;		// relative mass per area: 1 = normal cloth, 2 = heavy wool/leather (air moves it half as much), 0.5 = silk
		int				proxy = -1;		// simulation grid: 0 or -1 = none (every point simulated), N = a coarse grid of about N points
		float			stretch = 0.0f;		// 0..1 XPBD stretch give: 0 = rigid (default), 0.2 = a little elastic
		float			friction = 0.3f;	// 0..1 kinetic friction against the body and floor
		float			sfriction = 0.5f;	// static friction: contacts sliding slower than this x the push stick
		float			lift = 2.0f;		// units a free point may rise above the attached edge it hangs from; <0 = no limit
	};

	// SJE: r_meshClothLayers: a cloth point and a triangle of another cloth surface it lies over or under in the
	// bind pose; sgn = the side of the triangle (by its winding) the point is on
	struct mcLayerPair_t {
		int		p, a, b, c;
		float	sgn, gap, reach, lat;	// side, distance kept, how near the point must be to count, sideways limit
		bool	idle;					// a loose point under worn cloth: no order to keep (ComputeLayerPairs)
		float	bindArea;				// twice the triangle's area in the bind pose
	};

	// SJE r_meshClothShoulders: a vertex of the model's own surfaces that a shoulder bone carries (a shoulder plate)
	// and a drawn cloth triangle it lies under in the bind pose (BuildPlatePairs)
	const float MC_PLATE_BIND = 5.0f;	// a plate vertex this near under (or over) a cloth triangle in the bind pose is paired with it
	const float MC_PLATE_GAP = 0.4f;	// ... and kept this far on its side of it (less where the model has it nearer)
	const float MC_PLATE_STEP = 2.5f;	// ... the triangle moved at most this far in all
	const float MC_PLATE_KEEP = 0.2f;	// ... and kept this far from the model's nearest surface on its other side
	struct mcPlatePair_t {
		const mdxmSurface_t* s;
		int		v, tri, a, b, c;
		float	sgn, gap, reach, lat;	// side, distance kept, how near the vertex must be to count, sideways limit
		float	room;					// how far the triangle may be moved in all (to the model's next surface on the other side)
	};

	struct mcGroup_t {
		std::vector<const mdxmSurface_t*>	surfs;
		std::vector<const mcDef_t*>		defs;
		std::vector<int>					vertBase;		// first global vertex of each surface
		int									numVerts = 0;
		int									numParticles = 0;
		std::vector<int>					vertToParticle;	// global vertex -> particle
		std::vector<int>					partSurf, partVert;	// representative surface / local vertex
		std::vector<float>					freedom, tether, shape, bend, inertia;	// per particle
		std::vector<char>					selfCol;		// per particle: takes part in self-collision
		std::vector<float>					smooth;			// per particle: crease smoothing of the drawn shape
		std::vector<float>					maxAccel;		// per particle
		std::vector<float>					maxSpin;		// per particle
		std::vector<float>					spread;			// per particle
		std::vector<float>					weight;			// per particle
		std::vector<float>					stretchG, fricK, fricS, lift;	// per particle: XPBD stretch give, kinetic / static friction, lift limit
		std::vector<float>					thick;			// per particle: cloth thickness
		float								selfThick = 1.2f;	// self-collision distance (largest thickness of the self-colliding surfaces)
		std::vector<char>					hangs;			// per particle: hanging cloth ("above" rule: capes, robes, skirts)
		std::vector<char>					hardCol;		// per particle: arms/hands always push it out (not for sleeves, which wrap the arms)
		bool								anySmooth = false;
		bool								anySelf = false;
		std::vector<int>					lraAnchor;		// nearest attached particle along the cloth (-1 none)
		std::vector<float>					lraLen;			// bind-pose distance to it along the cloth
		float								bindLinkSum = 0.0f;	// total bind length of the structural links
		std::vector<float>					bindPos;		// 3 per particle, model bind pose
		mutable std::vector<float>			linkAlpha;		// per link: XPBD compliance (-1 = no bending resistance), lazy
		mutable std::vector<float>			rootDist;		// per particle: bind distance along the cloth from the attached edge (lazy)
		mutable std::vector<float>			rootS;			// SJE: the same as a part of its sheet's length: 0 at the edge, 1 at the hem (lazy)
		mutable std::vector<int>			sheetOf;		// SJE per particle: the sheet it belongs to - all cloth joined to it by links (ComputeRootDist, lazy)
		mutable int							numSheets = 0;
		mutable std::vector<unsigned char>	high;			// SJE per particle: 1 = it hangs from shoulder height or above (BuildPlatePairs, main thread; read only once plateBuilt is set)
		mutable std::vector<unsigned char>	wrap;			// SJE per particle: 1 = its surface wraps round the body at its height (ComputeWrap, lazy)
		mutable std::vector<unsigned char>	under;			// SJE per particle: 1 = a loose layer the model has UNDER worn cloth there (ComputeLayerPairs, lazy)
		mutable std::vector<unsigned>		partUse;		// SJE r_meshClothShown: per particle: the group's surfaces (bits) with a vertex on it (ComputeLayerPairs, lazily)
		mutable std::vector<mcPlatePair_t>	platePairs;		// SJE r_meshClothShoulders: plate vertices under the cloth (MC_BodyFit, main thread)
		mutable bool						plateBuilt = false;
		mutable float						reach = -1.0f, turn = -1.0f;	// from the defs (lazy)
		std::vector<int>					linkA, linkB;
		std::vector<char>					linkBend;
		std::vector<int>					tris;			// particle indices
		// SJE: isometric bending (r_meshClothBendModel 1, Bergou et al. 2006 quadratic bending): one stencil per
		// edge shared by two triangles - the edge's two points, then the two opposite points - with its
		// cotangent weights from the bind pose. Built lazily on the main thread (MC_Prepare).
		mutable std::vector<int>			bendSt;			// 4 particles per stencil
		mutable std::vector<float>			bendW;			// 4 cotangent weights per stencil (they sum to 0)
		mutable std::vector<float>			bendAlpha;		// per stencil: XPBD compliance (-1 = no bending resistance)
		mutable std::vector<float>			bendBs;			// per stencil: 0..1 bend stiffness (old projection)
		mutable std::vector<mcLayerPair_t>	layerPairs;	// SJE: r_meshClothLayers (lazy, main thread)
		mutable bool						layerBuilt = false;
		mutable bool						bendBuilt = false;

		// What is drawn. Dense cloth is simulated on a coarse grid of its own
		// vertices (like the separate simulation mesh of game cloth tools); every
		// drawn vertex follows the grid points around it. Without a grid the
		// drawn points are the particles themselves.
		bool								proxied = false;
		int									numDraw = 0;	// drawn points (welded vertices)
		std::vector<int>					vertToDraw;		// global vertex -> drawn point
		std::vector<float>					drawFree;		// per drawn point: freedom
		std::vector<char>					drawHangs;		// per drawn point: hanging cloth
		std::vector<int>					drawTris;		// drawn point indices
		std::vector<int>					drawIdx;		// 4 per drawn point: particles it follows (-1 unused)
		std::vector<float>					drawW;			// 4 per drawn point: their weights
		std::vector<int>					seedDraw;		// per particle: the drawn point it sits on
		std::vector<int>					drawLinkA, drawLinkB;	// edges between drawn points (grid only: smoothing)
	};

	struct mcSurfEntry_t {
		mcGroup_t* group;			// nullptr = not cloth
		int			surfIndex;		// index into group->surfs
		int			numVerts, numTris, index;	// to notice a reused address after a model reload
	};

	// SJE: r_meshClothFaceCol: a cloth triangle near a body shape, found at the start of a 16 ms step
	struct mcFacePair_t {
		int		tri;		// first index into group->tris
		int		cap;		// capsule index in the step's list
		// limbs only, worked out when the pair first touches in a step: how far apart the two were at the start
		// of the step, and the direction from the limb to the cloth then (unit; zero if they touched)
		int		have;
		float	d0;
		float	n0[3];
	};

	struct mcInstance_t {
		const mcGroup_t* group = nullptr;
		bool				active = false;
		int					lastTime = 0, lastSeen = 0, accum = 0, outTime = 0;
		// SJE r_meshClothSettle: steps still to do before the cloth is shown, the part of a step carried over to the
		// next frame, the steps since then (the drawn cloth is eased from the animated pose into the simulation over
		// MC_SHOW_STEPS), and the frame this instance was last counted in (MC_Prepare)
		int					settleLeft = 0, showSteps = 1000, settleSeen = -1;
		float				settleCredit = 0.0f;
		bool				settleNow = false;		// SJE r_meshClothSettleCut: it starts over in the frame of a camera cut: all at once (MC_Prepare)
		unsigned			offMask = 0;			// SJE r_meshClothShown: the group's surfaces (bits) this character has switched off (MC_Shown)
		std::vector<mcCapsule_t>	settleCaps;		// ... the body shapes at its last settling step, and the character's
		vec3_t				settleTravel = {};		//     travel since then (the shapes are swept from there through the next step)
		float				linkScale = 1.0f;		// animated / bind size of the cloth (model scale)
		int lod = 0;					// distance level of detail last used: 0 full, 1 reduced, 2 not simulated
		vec3_t				lastOrigin = {};		// character origin at the previous simulation
		vec3_t				bodyRel = {};			// SJE: centre of the attached cloth points against that origin (the body in its animation)
		bool				bodyRelOk = false;
		vec3_t				originVel = {};		// its velocity (units/s)
		vec3_t				up = {};				// the character's up direction (world)
		vec3_t				side = {};			// the character's left direction (world)
		std::vector<float>	pinPrev;		// attached particles' animated positions last frame
		float				twistRun = 0.0f;	// SJE: how far the attached edge has turned fast in one direction in one go (radians, signed)
		int					pinTime = 0;
		int					originTime = 0;
		std::vector<float>	p, prev, target, targetPrev;	// 3 per particle, world
		std::vector<float>	simTarget;		// animated pose at the simulation's own clock (it trails the frame by 'accum')
		std::vector<float>	outLocal, nrmLocal;				// 3 per particle, model space
		std::vector<float>	skinNrm;						// 3 per global vertex, model space
		std::vector<float>	scratchTarget, scratchFrom, scratchTo, scratchN;
		// SJE: watchdog against shaking (FightUpdate): per particle a count that trips when it jumps to and fro, its
		// place against the animation after the last step (3), its move in that step (3) and its animated place then (3)
		std::vector<float>	fight, fightRel, fightD, fightT;
		std::vector<float>	push, contactN, targetSub;	// per particle: this substep's body push, contact normal, previous substep target
		std::vector<char>	contact;
		std::vector<float>	lambda;			// XPBD: accumulated multiplier per link (reset every substep)
		std::vector<float>	scratchTarget2, scratchCnt;
		std::vector<float>	drawTarget, drawOut;	// 3 per drawn point, world
		std::vector<unsigned char>	drawPushed;	// SJE: per drawn point, 1 = an attached point moved off a shoulder plate (drawn although attached)
		std::vector<float>	scratchPlate, scratchPlateTri;	// SJE: the skinned plate vertices (3 per plate pair, world); moved per triangle
		std::vector<char>	scratchDone;
		std::vector<float>	impulse;		// pending velocity kicks (units/s), e.g. from explosions
		std::vector<float>	offPrev, offCur;	// cloth offset from the animation after the last two steps (for drawing between steps)
		std::vector<float>	pPrevStep, pCurStep;	// world positions after the last two steps
		vec3_t				oPrevStep = {}, oCurStep = {};	// character origin at those two steps
		// SJE: sleeping cloth (r_meshClothSleep): cloth that has come to rest, on a character that stands still,
		// stops simulating until the pose, the body capsules, the wind or a blast moves it again
		bool				asleep = false;
		std::vector<mcCapsule_t>	capsPrev;	// last frame's body capsules (world): swept through the substeps
		std::vector<mcCapsule_t>	scratchCaps;	// Simulate's capsule lists (5 x MC_NEAR_CAPS / MC_MAX_CAPS, 16 KB): off the stack
		int					restSteps = 0;		// 16 ms steps the cloth has been at rest
		float				breezeTaut = 0.0f;	// SJE r_meshClothBreeze: 0 = the cloth hangs relaxed and feels the breeze ... 1 = held stretched somewhere: none
		bool				breezeHere = false;	// SJE r_meshClothBreeze: the breeze reached this cloth at the last look (near enough to the camera)
		std::vector<float>	restRef;			// animated pose when the rest began (3 per particle)
		std::vector<unsigned char>	eased;		// SJE r_meshClothEase: 1 = the shake watchdog let this point go and gave it back while the pose holds it still (FightUpdate)
		std::vector<unsigned char>	quiet;		// SJE r_meshClothEase: steps running in which the point lay at rest against a pose that holds it still (FightUpdate, up to 255)
		std::vector<unsigned char>	letgo;		// SJE r_meshClothEase: steps left of the let-go after the watchdog tripped for this point (MC_FIGHT_RAMP .. 0)
		std::vector<unsigned char>	thrown;		// SJE r_meshClothRest: > 0 = the body's triangle test threw this point 'back to its own side' within the last steps
		std::vector<unsigned char>	limbPush;	// SJE r_meshClothCalm: > 0 = an arm, a hand or a held thing moved this point within the last steps (FaceCollide)
		std::vector<float>	restLo, restHi;		// SJE r_meshClothCalm: the box every point has filled since the rest count began (world, 3 per particle)
		float				restCaps[8] = {};	// body capsule signature then (sums of the ends and radii, count)
		// SJE: per-step constants (were worked out per particle or per link in every substep)
		std::vector<float>	kSpread, kShape, kBendLink;
		// SJE: self-collision in every substep (r_meshClothSelfSub): close pairs found once per step
		std::vector<int>	selfPairs;			// 2 particles per pair
		// SJE: triangle against body collision (r_meshClothFaceCol): close pairs found once per step,
		// and per triangle in the current substep: bounding sphere and how far it is off the animation (5 floats)
		std::vector<mcFacePair_t>	facePairs;
		std::vector<float>	triSphere;
		std::vector<float>	faceStart;				// the cloth at the start of the step (3 per particle)
		std::vector<int>	ptCapStart;				// SJE: BuildPointCaps: per particle, where its shapes start in ptCaps (+ 1 entry)
		std::vector<unsigned char>	ptCaps;
		std::vector<float>	ptFrom, ptAllow;		// ... the points then, and how far from there each list holds
		float				ripPhase = 0.0f;		// SJE: phase of the ripples the airflow sends down the cloth (r_meshClothDragQuad)
		const mcCapsule_t* faceCapsFrom = nullptr;	// the body shapes then (valid during the step)
		// SJE: per-triangle air (r_meshClothAero), isometric bending multipliers (one per stencil)
		std::vector<float>	aeroDv, aeroW, bendLambda;
	};

	std::vector<mcDef_t>										g_defs;
	std::unordered_map<const mdxmSurface_t*, mcSurfEntry_t>	g_surfs;
	std::vector<mcGroup_t*>									g_groups;
	std::unordered_map<unsigned long long, mcInstance_t*>		g_instances;
	int															g_lastPrune;
	// SJE r_meshClothSettle: how many characters' cloth is starting over or still settling in this frame and in the one
	// before. Counted in MC_Prepare (main thread, before the frame's MC_Run calls), only read in MC_Run.
	int															g_settleCount = 0, g_settleCountLast = 0, g_settleTime = -1;
	// SJE r_meshClothSettleCut (MC_ViewCut): the views of this frame and of the one before, the last frame a view
	// jumped in, and what is left of that cut's allowance for settling at once. Main thread only.
	struct mcView_t { float org[3], fwd[3]; };
	mcView_t													g_viewCur[MC_CUT_VIEWS], g_viewPrev[MC_CUT_VIEWS];
	int															g_viewCurN = 0, g_viewPrevN = 0, g_viewFrame = -1000, g_viewTime = 0, g_viewPrevTime = 0;
	int															g_cutFrame = -1000, g_cutMode = 0;
	float														g_cutRoom = 0.0f;

	// cloth instances are forgotten by the wall clock, not a scene time: an in-game menu model view (UI time) and the
	// level (game time) are drawn in the same frame with different times, and pruning by either one deleted every
	// instance of the other each frame (reset every frame: a 10 fps character menu, exploding capes)
	static int MC_WallMs() {
		static const auto start = std::chrono::steady_clock::now();
		return (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
	}

	// Local inlined vector helpers: the engine's VectorLength/VectorSubtract/...
	// are out-of-line calls in this codebase, and the inner loops below run them
	// hundreds of thousands of times per frame.
	inline float mcDot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
	inline void  mcSub(const float* a, const float* b, float* o) { o[0] = a[0] - b[0]; o[1] = a[1] - b[1]; o[2] = a[2] - b[2]; }
	inline void  mcAdd(const float* a, const float* b, float* o) { o[0] = a[0] + b[0]; o[1] = a[1] + b[1]; o[2] = a[2] + b[2]; }
	inline void  mcMA(const float* a, float s, const float* b, float* o) { o[0] = a[0] + s * b[0]; o[1] = a[1] + s * b[1]; o[2] = a[2] + s * b[2]; }
	inline void  mcScale(const float* a, float s, float* o) { o[0] = a[0] * s; o[1] = a[1] * s; o[2] = a[2] * s; }
	inline void  mcCopy(const float* a, float* o) { o[0] = a[0]; o[1] = a[1]; o[2] = a[2]; }
	inline float mcLen(const float* a) { return sqrtf(mcDot(a, a)); }
	inline void  mcCross(const float* a, const float* b, float* o) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }
	inline float mcNorm(float* a) { float l = mcLen(a); if (l > 1e-12f) { float i = 1.0f / l; a[0] *= i; a[1] *= i; a[2] *= i; } return l; }
	inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

	const char* ModelFolder(const char* modelName, char* buf, int bufSize) {
		// "models/players/palpatine/model" -> "palpatine"
		const char* end = strrchr(modelName, '/');
		if (!end) { Q_strncpyz(buf, modelName, bufSize); return buf; }
		const char* start = end - 1;
		while (start > modelName && start[-1] != '/') start--;
		int n = (int)(end - start);
		if (n >= bufSize) n = bufSize - 1;
		memcpy(buf, start, n);
		buf[n] = 0;
		return buf;
	}

	float DistToSegment(const float* p, const float* a, const float* b) {
		vec3_t ab, ap, c;
		mcSub(b, a, ab);
		mcSub(p, a, ap);
		float L = mcDot(ab, ab);
		float t = (L > 1e-6f) ? Clamp01(mcDot(ap, ab) / L) : 0.0f;
		mcMA(a, t, ab, c);
		mcSub(p, c, c);
		return mcLen(c);
	}

	float RuleFreedom(const mcDef_t* d, const float* v) {
		switch (d->rule) {
		case MC_RULE_ABOVE: {
			float span = d->a[0] - d->a[1];
			if (fabsf(span) < 0.001f) return v[2] < d->a[0] ? 1.0f : 0.0f;
			return Clamp01((d->a[0] - v[2]) / span);
		}
		case MC_RULE_NEAR: {
			vec3_t c = { d->a[0], d->a[1], d->a[2] }, t;
			mcSub(v, c, t);
			float r = mcLen(t), span = d->a[4] - d->a[3];
			return Clamp01((r - d->a[3]) / (span > 0.001f ? span : 0.001f));
		}
		case MC_RULE_SEGMENT: {
			float r = DistToSegment(v, &d->a[0], &d->a[3]), span = d->a[7] - d->a[6];
			return Clamp01((r - d->a[6]) / (span > 0.001f ? span : 0.001f));
		}
		}
		return 0.0f;
	}

	const mcDef_t* FindDef(const char* folder, const char* surfaceName) {
		for (const mcDef_t& d : g_defs) {
			if (!Q_stricmp(d.model.c_str(), folder) && !Q_stricmp(d.surface.c_str(), surfaceName)) return &d;
		}
		return nullptr;
	}

	const mdxmVertex_t* Verts(const mdxmSurface_t* s) {
		return reinterpret_cast<const mdxmVertex_t*>(reinterpret_cast<const byte*>(s) + s->ofsVerts);
	}

	const mdxmHeader_t* HeaderOf(const mdxmSurface_t* s) {
		return reinterpret_cast<const mdxmHeader_t*>(reinterpret_cast<const byte*>(s) + s->ofsHeader);
	}

	const char* SurfaceName(const mdxmHeader_t* h, int index) {
		const auto ho = reinterpret_cast<const mdxmHierarchyOffsets_t*>(reinterpret_cast<const byte*>(h) + sizeof(mdxmHeader_t));
		const auto sh = reinterpret_cast<const mdxmSurfHierarchy_t*>(reinterpret_cast<const byte*>(ho) + ho->offsets[index]);
		return sh->name;
	}

	// all surfaces of the LOD that contains s
	std::vector<const mdxmSurface_t*> LodSurfaces(const mdxmSurface_t* s) {
		std::vector<const mdxmSurface_t*> out;
		const mdxmHeader_t* h = HeaderOf(s);
		const byte* lod = reinterpret_cast<const byte*>(h) + h->ofsLODs;
		for (int l = 0; l < h->numLODs; l++) {
			const auto lh = reinterpret_cast<const mdxmLOD_t*>(lod);
			const auto lso = reinterpret_cast<const mdxmLODSurfOffset_t*>(lod + sizeof(mdxmLOD_t));
			const byte* end = lod + lh->ofsEnd;
			if (reinterpret_cast<const byte*>(s) >= lod && reinterpret_cast<const byte*>(s) < end) {
				for (int i = 0; i < h->numSurfaces; i++) {
					out.push_back(reinterpret_cast<const mdxmSurface_t*>(reinterpret_cast<const byte*>(lso) + lso->offsets[i]));
				}
				return out;
			}
			lod = end;
		}
		out.push_back(s);
		return out;
	}

	struct mcWeldGrid_t {
		std::unordered_map<long long, std::vector<int>> cells;
		std::vector<const float*> pts;
		static long long Key(long long x, long long y, long long z) { return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL); }
		static long long Cell(float v) { return (long long)floorf(v * 10.0f); }	// 0.1 unit cells
		void Add(const float* p, int id) {
			if ((int)pts.size() <= id) pts.resize(id + 1);
			pts[id] = p;
			cells[Key(Cell(p[0]), Cell(p[1]), Cell(p[2]))].push_back(id);
		}
		int Find(const float* p) const {
			long long cx = Cell(p[0]), cy = Cell(p[1]), cz = Cell(p[2]);
			for (long long dx = -1; dx <= 1; dx++) for (long long dy = -1; dy <= 1; dy++) for (long long dz = -1; dz <= 1; dz++) {
				auto it = cells.find(Key(cx + dx, cy + dy, cz + dz));
				if (it == cells.end()) continue;
				for (int id : it->second) {
					vec3_t d;
					mcSub(p, pts[id], d);
					if (mcDot(d, d) < MC_WELD * MC_WELD) return id;
				}
			}
			return -1;
		}
	};

	// Long range attachments (Kim, Chentanez & Mueller 2012): each free
	// particle's nearest attached particle measured along the cloth, so the
	// solver can forbid it from ever getting further away than that. Needs
	// bindPos, links and freedom.
	void ComputeLRA(mcGroup_t* g) {
		std::vector<std::vector<std::pair<int, float>>> adj(g->numParticles);
		g->bindLinkSum = 0.0f;
		for (size_t l = 0; l < g->linkA.size(); l++) {
			if (g->linkBend[l]) continue;
			vec3_t d;
			mcSub(&g->bindPos[g->linkA[l] * 3], &g->bindPos[g->linkB[l] * 3], d);
			const float len = mcLen(d);
			g->bindLinkSum += len;
			adj[g->linkA[l]].push_back({ g->linkB[l], len });
			adj[g->linkB[l]].push_back({ g->linkA[l], len });
		}
		g->lraAnchor.assign(g->numParticles, -1);
		g->lraLen.assign(g->numParticles, 1e9f);
		std::vector<std::pair<float, int>> heap;	// min-heap by distance
		auto cmp = [](const std::pair<float, int>& x, const std::pair<float, int>& y) { return x.first > y.first; };
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] > 0.0f) continue;
			g->lraAnchor[p] = p;
			g->lraLen[p] = 0.0f;
			heap.push_back({ 0.0f, p });
		}
		std::make_heap(heap.begin(), heap.end(), cmp);
		while (!heap.empty()) {
			std::pop_heap(heap.begin(), heap.end(), cmp);
			const std::pair<float, int> top = heap.back();
			heap.pop_back();
			if (top.first > g->lraLen[top.second]) continue;
			for (const auto& e : adj[top.second]) {
				const float nd = top.first + e.second;
				if (nd < g->lraLen[e.first]) {
					g->lraLen[e.first] = nd;
					g->lraAnchor[e.first] = g->lraAnchor[top.second];
					heap.push_back({ nd, e.first });
					std::push_heap(heap.begin(), heap.end(), cmp);
				}
			}
		}
		for (int p = 0; p < g->numParticles; p++) if (g->lraAnchor[p] < 0) g->lraLen[p] = 0.0f;
	}

	// Drawn points = particles (no simulation grid).
	void IdentityDraw(mcGroup_t* g) {
		g->proxied = false;
		g->numDraw = g->numParticles;
		g->vertToDraw = g->vertToParticle;
		g->drawFree = g->freedom;
		g->drawHangs = g->hangs;
		g->drawTris = g->tris;
		g->drawIdx.assign((size_t)g->numParticles * 4, -1);
		g->drawW.assign((size_t)g->numParticles * 4, 0.0f);
		g->seedDraw.resize(g->numParticles);
		for (int p = 0; p < g->numParticles; p++) { g->drawIdx[p * 4] = p; g->drawW[p * 4] = 1.0f; g->seedDraw[p] = p; }
	}

	// Coarse simulation grid for dense cloth, only where the config asks for it
	// ("proxy N"), so existing capes keep exactly their tuned behaviour: about 'want' of the cloth's own
	// points, spread evenly along the surface (farthest point sampling by
	// distance along the cloth). Each particle of the grid owns the points
	// closest to it along the cloth; grid links join neighbouring regions and
	// grid triangles come from mesh triangles that touch three regions.
	mcGroup_t* MakeProxy(mcGroup_t* r, int want) {
		const int n = r->numParticles;
		std::vector<std::vector<std::pair<int, float>>> adj(n);
		for (size_t l = 0; l < r->linkA.size(); l++) {
			if (r->linkBend[l]) continue;
			vec3_t d;
			mcSub(&r->bindPos[r->linkA[l] * 3], &r->bindPos[r->linkB[l] * 3], d);
			const float len = mcLen(d);
			adj[r->linkA[l]].push_back({ r->linkB[l], len });
			adj[r->linkB[l]].push_back({ r->linkA[l], len });
		}
		std::vector<float> dist(n, 1e30f);
		std::vector<int> region(n, -1);
		std::vector<int> seeds;
		std::vector<std::pair<float, int>> heap;
		auto cmp = [](const std::pair<float, int>& x, const std::pair<float, int>& y) { return x.first > y.first; };
		auto grow = [&](int s) {
			const int id = (int)seeds.size();
			seeds.push_back(s);
			dist[s] = 0.0f; region[s] = id;
			heap.assign(1, { 0.0f, s });
			while (!heap.empty()) {
				std::pop_heap(heap.begin(), heap.end(), cmp);
				const std::pair<float, int> top = heap.back();
				heap.pop_back();
				if (top.first > dist[top.second]) continue;
				for (const auto& e : adj[top.second]) {
					const float nd = top.first + e.second;
					if (nd < dist[e.first]) {
						dist[e.first] = nd; region[e.first] = id;
						heap.push_back({ nd, e.first });
						std::push_heap(heap.begin(), heap.end(), cmp);
					}
				}
			}
			};
		// every separate piece gets at least one grid point; then the farthest point each time
		for (int v = 0; v < n; v++) if (region[v] < 0) grow(v);
		while ((int)seeds.size() < want) {
			int far = -1; float fd = 0.0f;
			for (int v = 0; v < n; v++) if (dist[v] > fd) { fd = dist[v]; far = v; }
			if (far < 0 || fd <= 1e-3f) break;
			grow(far);
		}
		const int m = (int)seeds.size();
		// a grid point whose region holds an attached point is attached: the grid
		// is the only thing that can hold the cloth there
		std::vector<float> regionFree(m, 1e30f);
		for (int v = 0; v < n; v++) if (r->freedom[v] < regionFree[region[v]]) regionFree[region[v]] = r->freedom[v];

		mcGroup_t* g = new mcGroup_t;
		g->surfs = r->surfs; g->defs = r->defs; g->vertBase = r->vertBase; g->numVerts = r->numVerts;
		g->numParticles = m;
		g->selfThick = r->selfThick; g->anySmooth = r->anySmooth; g->anySelf = r->anySelf;
		g->bindPos.resize((size_t)m * 3);
		for (int i = 0; i < m; i++) {
			const int s = seeds[i];
			g->partSurf.push_back(r->partSurf[s]); g->partVert.push_back(r->partVert[s]);
			g->freedom.push_back(MCDBG_OFF("NOREGIONPIN") ? r->freedom[s] : regionFree[i]); g->tether.push_back(r->tether[s]);
			g->shape.push_back(r->shape[s]); g->bend.push_back(r->bend[s]);
			g->inertia.push_back(r->inertia[s]); g->selfCol.push_back(r->selfCol[s]);
			g->smooth.push_back(r->smooth[s]); g->maxAccel.push_back(r->maxAccel[s]);
			g->maxSpin.push_back(r->maxSpin[s]); g->spread.push_back(r->spread[s]);
			g->weight.push_back(r->weight[s]); g->thick.push_back(r->thick[s]);
			g->lift.push_back(r->lift[s]); g->stretchG.push_back(r->stretchG[s]); g->fricK.push_back(r->fricK[s]); g->fricS.push_back(r->fricS[s]);
			g->hangs.push_back(r->hangs[s]); g->hardCol.push_back(r->hardCol[s]);
			mcCopy(&r->bindPos[s * 3], &g->bindPos[i * 3]);
		}
		// grid links and triangles
		std::unordered_map<unsigned long long, char> haveLink;
		std::unordered_map<unsigned long long, int> edgeOther;
		auto ekey = [](int a, int b) { if (a > b) { int t = a; a = b; b = t; } return ((unsigned long long)a << 32) | (unsigned)b; };
		auto addLink = [&](int a, int b, char bend) {
			if (a == b) return;
			const unsigned long long k = ekey(a, b);
			if (haveLink.count(k)) return;
			haveLink[k] = 1;
			g->linkA.push_back(a); g->linkB.push_back(b); g->linkBend.push_back(bend);
			};
		std::vector<std::vector<int>> nbr(m);
		for (size_t l = 0; l < r->linkA.size(); l++) {
			if (r->linkBend[l]) continue;
			const int a = region[r->linkA[l]], b = region[r->linkB[l]];
			if (a == b || haveLink.count(ekey(a, b))) continue;
			addLink(a, b, 0);
			nbr[a].push_back(b); nbr[b].push_back(a);
		}
		std::unordered_map<unsigned long long, char> haveTri;
		for (size_t t = 0; t + 2 < r->tris.size(); t += 3) {
			int a = region[r->tris[t]], b = region[r->tris[t + 1]], c = region[r->tris[t + 2]];
			if (a == b || b == c || a == c) continue;
			int lo = a < b ? (a < c ? a : c) : (b < c ? b : c), hi = a > b ? (a > c ? a : c) : (b > c ? b : c);
			const unsigned long long tk = ((unsigned long long)lo << 42) ^ ((unsigned long long)(a + b + c - lo - hi) << 21) ^ (unsigned long long)hi;
			if (haveTri.count(tk)) continue;
			haveTri[tk] = 1;
			const int pi[3] = { a, b, c };	// keeps the mesh triangle's winding
			for (int k = 0; k < 3; k++) g->tris.push_back(pi[k]);
			for (int k = 0; k < 3; k++) {
				const int x = pi[k], y = pi[(k + 1) % 3], o = pi[(k + 2) % 3];
				const unsigned long long ek = ekey(x, y);
				auto it = edgeOther.find(ek);
				if (it == edgeOther.end()) edgeOther[ek] = o;
				else addLink(it->second, o, 1);
			}
		}
		ComputeLRA(g);

		// drawn points follow up to 4 nearby grid particles: their own region's
		// and its neighbours', weighted by inverse squared bind distance
		g->proxied = true;
		g->numDraw = n;
		g->vertToDraw = r->vertToParticle;
		g->drawFree = r->freedom;
		g->drawHangs = r->hangs;
		g->drawTris = r->tris;
		g->seedDraw = seeds;
		// a drawn point close to an attached one (the attached edge, or a seam with
		// a rigid piece in the middle of the cloth) can't follow the grid fully: the
		// grid is too coarse to bend within that distance. Taper its freedom over
		// one grid spacing, measured along the cloth.
		{
			float spacing = 0.0f; int nl = 0;
			for (size_t l = 0; l < g->linkA.size(); l++) {
				if (g->linkBend[l]) continue;
				vec3_t d; mcSub(&g->bindPos[g->linkA[l] * 3], &g->bindPos[g->linkB[l] * 3], d);
				spacing += mcLen(d); nl++;
			}
			spacing = nl ? spacing / nl : 1.0f;
			std::vector<float> da(n, 1e30f);
			heap.clear();
			for (int v = 0; v < n; v++) if (r->freedom[v] <= 0.0f) { da[v] = 0.0f; heap.push_back({ 0.0f, v }); }
			std::make_heap(heap.begin(), heap.end(), cmp);
			while (!heap.empty()) {
				std::pop_heap(heap.begin(), heap.end(), cmp);
				const std::pair<float, int> top = heap.back();
				heap.pop_back();
				if (top.first > da[top.second]) continue;
				for (const auto& e : adj[top.second]) {
					const float nd = top.first + e.second;
					if (nd < da[e.first] && nd < spacing) { da[e.first] = nd; heap.push_back({ nd, e.first }); std::push_heap(heap.begin(), heap.end(), cmp); }
				}
			}
			for (int v = 0; v < n; v++) {
				if (da[v] >= spacing) continue;
				const float t = da[v] / spacing, taper = t * t * (3.0f - 2.0f * t);
				if (taper < g->drawFree[v]) g->drawFree[v] = taper;
			}
		}
		for (size_t l = 0; l < r->linkA.size(); l++) if (!r->linkBend[l]) { g->drawLinkA.push_back(r->linkA[l]); g->drawLinkB.push_back(r->linkB[l]); }
		g->drawIdx.assign((size_t)n * 4, -1);
		g->drawW.assign((size_t)n * 4, 0.0f);
		for (int v = 0; v < n; v++) {
			const int own = region[v];
			std::vector<std::pair<float, int>> cand;
			auto consider = [&](int q) {
				vec3_t d;
				mcSub(&r->bindPos[v * 3], &g->bindPos[q * 3], d);
				cand.push_back({ mcDot(d, d), q });
				};
			consider(own);
			for (int q : nbr[own]) consider(q);
			std::sort(cand.begin(), cand.end());
			if (cand[0].first < 1e-8f) { g->drawIdx[v * 4] = cand[0].second; g->drawW[v * 4] = 1.0f; continue; }
			float tot = 0.0f;
			const int k = cand.size() < 4 ? (int)cand.size() : 4;
			for (int j = 0; j < k; j++) {
				const float w = 1.0f / cand[j].first;
				g->drawIdx[v * 4 + j] = cand[j].second; g->drawW[v * 4 + j] = w; tot += w;
			}
			for (int j = 0; j < k; j++) g->drawW[v * 4 + j] /= tot;
		}
		delete r;
		return g;
	}

	mcGroup_t* FinishGroup(mcGroup_t* g) {
		int want = -1;
		bool off = false;
		for (const mcDef_t* d : g->defs) {
			if (d->proxy == 0) off = true;
			else if (d->proxy > want) want = d->proxy;
		}
		if (off || want <= 0 || want * 2 > g->numParticles || MCDBG_OFF("NOPROXY")) {
			IdentityDraw(g);
			return g;
		}
		return MakeProxy(g, want);
	}

	mcGroup_t* BuildGroup(const mdxmSurface_t* seed, const char* folder) {
		const mdxmHeader_t* h = HeaderOf(seed);
		std::vector<const mdxmSurface_t*> all = LodSurfaces(seed);
		mcGroup_t* g = new mcGroup_t;
		std::vector<const mdxmSurface_t*> rigid;
		for (const mdxmSurface_t* s : all) {
			const char* name = SurfaceName(h, s->thisSurfaceIndex);
			const mcDef_t* def = FindDef(folder, name);
			if (def && s->numVerts > 0 && s->numTriangles > 0) {
				g->surfs.push_back(s);
				g->defs.push_back(def);
			}
			else if (name[0] != '*' && !strstr(name, "_off") && s->numVerts > 0) {
				rigid.push_back(s);	// tags (*name) and dismemberment caps (_off) don't count as seams
			}
		}

		// rigid seam points
		mcWeldGrid_t rigidGrid;
		int rid = 0;
		for (const mdxmSurface_t* s : rigid) {
			const mdxmVertex_t* v = Verts(s);
			for (int i = 0; i < s->numVerts; i++) rigidGrid.Add(v[i].vertCoords, rid++);
		}

		// weld cloth vertices across surfaces
		mcWeldGrid_t grid;
		g->numVerts = 0;
		for (size_t si = 0; si < g->surfs.size(); si++) {
			g->vertBase.push_back(g->numVerts);
			g->numVerts += g->surfs[si]->numVerts;
		}
		g->vertToParticle.assign(g->numVerts, -1);
		for (size_t si = 0; si < g->surfs.size(); si++) {
			const mdxmSurface_t* s = g->surfs[si];
			const mdxmVertex_t* v = Verts(s);
			for (int i = 0; i < s->numVerts; i++) {
				int p = grid.Find(v[i].vertCoords);
				float f = RuleFreedom(g->defs[si], v[i].vertCoords);
				if (rigidGrid.Find(v[i].vertCoords) >= 0) f = 0.0f;	// seam with a non-cloth surface
				if (p < 0) {
					p = (int)g->partSurf.size();
					g->partSurf.push_back((int)si);
					g->partVert.push_back(i);
					g->freedom.push_back(f);
					g->tether.push_back(g->defs[si]->tether);
					g->shape.push_back(g->defs[si]->shape);
					g->bend.push_back(g->defs[si]->bend);
					g->inertia.push_back(g->defs[si]->inertia);
					g->selfCol.push_back(g->defs[si]->self ? 1 : 0);
					g->smooth.push_back(g->defs[si]->smooth);
					g->maxAccel.push_back(g->defs[si]->maxAccel);
					g->maxSpin.push_back(g->defs[si]->maxSpin);
					g->spread.push_back(g->defs[si]->spread);
					g->weight.push_back(g->defs[si]->weight);
					g->lift.push_back(g->defs[si]->lift);
					g->stretchG.push_back(g->defs[si]->stretch);
					g->fricK.push_back(g->defs[si]->friction);
					g->fricS.push_back(g->defs[si]->sfriction);
					g->thick.push_back(g->defs[si]->thickness);
					g->hardCol.push_back(g->defs[si]->rule == MC_RULE_SEGMENT ? 0 : 1);
					g->hangs.push_back(g->defs[si]->rule == MC_RULE_ABOVE ? 1 : 0);
					if (g->defs[si]->smooth > 0.0f) g->anySmooth = true;
					if (g->defs[si]->self) {
						if (!g->anySelf || g->defs[si]->thickness > g->selfThick) g->selfThick = g->defs[si]->thickness;
						g->anySelf = true;
					}
					grid.Add(v[i].vertCoords, p);
				}
				else {
					// shared by two pieces: the more attached and the shorter tether win
					if (f < g->freedom[p]) g->freedom[p] = f;
					if (g->defs[si]->tether < g->tether[p]) g->tether[p] = g->defs[si]->tether;
					if (g->defs[si]->shape > g->shape[p]) g->shape[p] = g->defs[si]->shape;
					if (g->defs[si]->bend > g->bend[p]) g->bend[p] = g->defs[si]->bend;
				}
				g->vertToParticle[g->vertBase[si] + i] = p;
			}
		}
		g->numParticles = (int)g->partSurf.size();
		// almost-attached vertices are attached: the solver has no partial springs
		for (int p = 0; p < g->numParticles; p++) if (g->freedom[p] < 0.02f) g->freedom[p] = 0.0f;

		// links from every cloth triangle
		std::unordered_map<unsigned long long, int> edgeOther;
		std::unordered_map<unsigned long long, char> haveLink;
		auto ekey = [](int a, int b) { if (a > b) { int t = a; a = b; b = t; } return ((unsigned long long)a << 32) | (unsigned)b; };
		auto addLink = [&](int a, int b, char bend) {
			if (a == b) return;
			unsigned long long k = ekey(a, b);
			if (haveLink.count(k)) return;
			haveLink[k] = 1;
			g->linkA.push_back(a); g->linkB.push_back(b); g->linkBend.push_back(bend);
			};
		for (size_t si = 0; si < g->surfs.size(); si++) {
			const mdxmSurface_t* s = g->surfs[si];
			const int* tri = reinterpret_cast<const int*>(reinterpret_cast<const byte*>(s) + s->ofsTriangles);
			for (int t = 0; t < s->numTriangles; t++) {
				int pi[3];
				for (int k = 0; k < 3; k++) {
					pi[k] = g->vertToParticle[g->vertBase[si] + tri[t * 3 + k]];
					g->tris.push_back(pi[k]);
				}
				for (int k = 0; k < 3; k++) {
					int a = pi[k], b = pi[(k + 1) % 3], o = pi[(k + 2) % 3];
					addLink(a, b, 0);
					unsigned long long ek = ekey(a, b);
					auto it = edgeOther.find(ek);
					if (it == edgeOther.end()) edgeOther[ek] = o;
					else addLink(it->second, o, 1);
				}
			}
		}

		g->bindPos.resize((size_t)g->numParticles * 3);
		for (int p = 0; p < g->numParticles; p++) mcCopy(Verts(g->surfs[g->partSurf[p]])[g->partVert[p]].vertCoords, &g->bindPos[p * 3]);
		ComputeLRA(g);
		return FinishGroup(g);
	}

	inline void ToWorld(const float* l, const vec3_t origin, const vec3_t axis[3], float* w) {
		for (int k = 0; k < 3; k++) w[k] = origin[k] + axis[0][k] * l[0] + axis[1][k] * l[1] + axis[2][k] * l[2];
	}

	inline void ToLocal(const float* w, const vec3_t origin, const vec3_t axis[3], float* l) {
		vec3_t d;
		mcSub(w, origin, d);
		for (int k = 0; k < 3; k++) {
			float L2 = mcDot(axis[k], axis[k]);
			l[k] = mcDot(d, axis[k]) / (L2 > 1e-8f ? L2 : 1.0f);
		}
	}

	inline void DirToLocal(const float* w, const vec3_t axis[3], float* l) {
		for (int k = 0; k < 3; k++) {
			float L2 = mcDot(axis[k], axis[k]);
			l[k] = mcDot(w, axis[k]) / (L2 > 1e-8f ? L2 : 1.0f);
		}
	}

	// Skin one vertex exactly like RB_SurfaceGhoul.
	void SkinVertex(const mdxmSurface_t* s, int i, mcBoneFn_t boneFn, void* ctx, float* outPos, float* outNrm) {
		const mdxmVertex_t* v = &Verts(s)[i];
		const int* refs = reinterpret_cast<const int*>(reinterpret_cast<const byte*>(s) + s->ofsBoneReferences);
		const int nw = G2_GetVertWeights(v);
		float tot = 0.0f;
		outPos[0] = outPos[1] = outPos[2] = 0.0f;
		outNrm[0] = outNrm[1] = outNrm[2] = 0.0f;
		for (int k = 0; k < nw; k++) {
			const float w = G2_GetVertBoneWeight(v, k, tot, nw);
			const float (*m)[4] = boneFn(ctx, refs[G2_GetVertBoneIndex(v, k)]);
			for (int r = 0; r < 3; r++) {
				outPos[r] += w * (m[r][0] * v->vertCoords[0] + m[r][1] * v->vertCoords[1] + m[r][2] * v->vertCoords[2] + m[r][3]);
				outNrm[r] += w * (m[r][0] * v->normal[0] + m[r][1] * v->normal[1] + m[r][2] * v->normal[2]);
			}
		}
	}

	// Push p out of the body capsules - but never further out than where the
	// animation itself puts this vertex (target). Robes are often modelled a
	// little inside the capsule shapes; forcing those parts out fights their
	// attached neighbours and stretches the mesh.
	// SJE: 'blend' (on with the triangle test, r_meshClothFaceCol): the pushes of the body's own shapes (the torso,
	// hip and leg capsules and the fitted body) on a point are all worked out from where it stands and their mean
	// is applied, instead of one push after the other. A body's shapes overlap (hip and thigh capsules, the fitted
	// shapes in them): pushed out of each in turn, a point ended outside the last one from wherever the others had
	// sent it - further out than any of them asked, and it was held there against its links. With the mean, robes
	// have fewer stretched links (Maul 48 -> 35 per step, Ventress 33 -> 13, Revan and Kylo a tenth fewer), shake
	// less, and the body is no deeper in them. Arms, shoulders and held things come after that, each in turn as before:
	// they have the last word (averaged in with the rest, hands came through: Maul's limb count 0.03 -> 0.14).
	// SJE: 'idx' (with the triangle test): only the n shapes caps[idx[0]] .. caps[idx[n - 1]] are looked at, in that
	// order - the ones the point can reach in this step (BuildPointCaps). That list holds while the point is within
	// 'allow2' (squared) of 'from'; if a push here takes it further, 'left' is set and the caller tests all shapes.
	bool Collide(float* p, const float* target, const mcCapsule_t* caps, int n, float* nOut, bool useHard, float margin, bool blend = false,
		const unsigned char* idx = nullptr, const float* from = nullptr, float allow2 = 0.0f, bool* left = nullptr) {
		bool hit = false;
		for (int pass = 0; pass < (blend ? 2 : 1); pass++) {
			const bool mean = blend && pass == 0;	// the body's shapes; the second pass is the limbs
			vec3_t p0, sum = { 0, 0, 0 };
			int cnt = 0;
			mcCopy(p, p0);
			for (int ci = 0; ci < n; ci++) {
				const int c = idx ? idx[ci] : ci;
				if (blend && mean == (caps[c].hard == 1 || caps[c].hard == 2 || caps[c].hard == 4)) continue;
				if (mean) mcCopy(p0, p);
				vec3_t ab, ap, cl, out;
				mcSub(caps[c].b, caps[c].a, ab);
				float L = mcDot(ab, ab);
				mcSub(p, caps[c].a, ap);
				float t = (L > 1e-6f) ? Clamp01(mcDot(ap, ab) / L) : 0.0f;
				mcMA(caps[c].a, t, ab, cl);
				mcSub(p, cl, out);
				float d = mcLen(out), minD = caps[c].r + margin;
				if (d >= minD || d < 1e-4f) continue;
				vec3_t at, ac, ao;
				mcSub(target, caps[c].a, at);
				float tt = (L > 1e-6f) ? Clamp01(mcDot(at, ab) / L) : 0.0f;
				mcMA(caps[c].a, tt, ab, ac);
				mcSub(target, ac, ao);
				float dT = mcLen(ao);
				// body parts the robe was modelled around may sit a little inside it;
				// arms and hands move through the cape, so those always push it out
				if (!(useHard && caps[c].hard) && dT < minD) minD = dT;
				if (d < minD) {
					mcMA(cl, minD / d, out, p);
					mcScale(out, 1.0f / d, nOut);
					hit = true;
					if (mean) { for (int k = 0; k < 3; k++) sum[k] += p[k] - p0[k]; cnt++; }
					else if (left) { vec3_t m; mcSub(p, from, m); if (mcDot(m, m) > allow2) *left = true; }
				}
				else if (d < minD + 0.25f) {
					// resting on the body: still in contact for friction
					mcScale(out, 1.0f / d, nOut);
					hit = true;
				}
			}
			if (mean) {
				mcCopy(p0, p);
				if (cnt > 0) mcMA(p, 1.0f / cnt, sum, p);
				if (left && cnt > 0) { vec3_t m; mcSub(p, from, m); if (mcDot(m, m) > allow2) *left = true; }
			}
		}
		return hit;
	}

	void DrawNormals(const mcGroup_t* g, const std::vector<float>& pos, std::vector<float>& n) {
		n.assign((size_t)g->numDraw * 3, 0.0f);
		for (size_t t = 0; t + 2 < g->drawTris.size(); t += 3) {
			const float* a = &pos[g->drawTris[t] * 3], * b = &pos[g->drawTris[t + 1] * 3], * c = &pos[g->drawTris[t + 2] * 3];
			vec3_t e1, e2, fn;
			mcSub(b, a, e1);
			mcSub(c, a, e2);
			mcCross(e1, e2, fn);
			for (int k = 0; k < 3; k++) {
				float* d = &n[g->drawTris[t + k] * 3];
				mcAdd(d, fn, d);
			}
		}
		for (int p = 0; p < g->numDraw; p++) mcNorm(&n[p * 3]);
	}

	void ParticleNormals(const mcGroup_t* g, const std::vector<float>& pos, std::vector<float>& n) {
		n.assign((size_t)g->numParticles * 3, 0.0f);
		for (size_t t = 0; t + 2 < g->tris.size(); t += 3) {
			const float* a = &pos[g->tris[t] * 3], * b = &pos[g->tris[t + 1] * 3], * c = &pos[g->tris[t + 2] * 3];
			vec3_t e1, e2, fn;
			mcSub(b, a, e1);
			mcSub(c, a, e2);
			mcCross(e1, e2, fn);
			for (int k = 0; k < 3; k++) {
				float* d = &n[g->tris[t + k] * 3];
				mcAdd(d, fn, d);
			}
		}
		for (int p = 0; p < g->numParticles; p++) mcNorm(&n[p * 3]);
	}

	// Only the capsules that can reach the cloth: bounds of the animated free
	// positions grown by the tether, against each capsule's bounds.
	int CullCapsules(const mcGroup_t* g, const std::vector<float>& target, const mcCapsule_t* caps, int ncaps,
		mcCapsule_t* out, int maxOut) {
		vec3_t mins = { 1e9f, 1e9f, 1e9f }, maxs = { -1e9f, -1e9f, -1e9f };
		float grow = MC_MARGIN;
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] <= 0.0f) continue;
			if (g->tether[p] + MC_MARGIN > grow) grow = g->tether[p] + MC_MARGIN;
			for (int k = 0; k < 3; k++) {
				float v = target[p * 3 + k];
				if (v < mins[k]) mins[k] = v;
				if (v > maxs[k]) maxs[k] = v;
			}
		}
		int n = 0;
		for (int c = 0; c < ncaps && n < maxOut; c++) {
			bool overlap = true;
			for (int k = 0; k < 3 && overlap; k++) {
				float lo = (caps[c].a[k] < caps[c].b[k] ? caps[c].a[k] : caps[c].b[k]) - caps[c].r;
				float hi = (caps[c].a[k] > caps[c].b[k] ? caps[c].a[k] : caps[c].b[k]) + caps[c].r;
				if (hi < mins[k] - grow || lo > maxs[k] + grow) overlap = false;
			}
			if (overlap) out[n++] = caps[c];
		}
		return n;
	}

	static void EdgeCollide(const mcGroup_t* g, mcInstance_t* in, const mcCapsule_t* caps, int ncaps);
	static void FaceCollide(const mcGroup_t* g, mcInstance_t* in, const mcCapsule_t* caps, int ncaps, int overArms, int rest, int ease);

	// XPBD compliance (Macklin, Mueller & Chentanez 2016) from a 0..1 'give': the fraction of a
	// constraint's error left over per substep at the reference 6 substeps per 16 ms. Calibrated
	// so the default setup behaves like the old position projection, but stiffness no longer
	// depends on the substep count, and extra solver iterations converge instead of making
	// cloth stiffer.
	inline float GiveToCompliance(float give) {
		if (give <= 0.0f) return 0.0f;
		if (give > 0.999f) give = 0.999f;
		const float h6 = MC_DT / 6.0f;
		const float frac = 1.0f - give;				// error removed per reference substep
		return h6 * h6 * 2.0f * (1.0f / frac - 1.0f);
	}
	inline float BendCompliance(float bs) {
		if (bs <= 0.0f) return -1.0f;				// no bending resistance
		const float b = bs < 0.99f ? bs : 0.99f;
		return GiveToCompliance(powf(1.0f - b, 1.0f / 6.0f));
	}

	// every link's XPBD compliance, from the static bend / stretch settings (lazy, main thread)
	static void ComputeLinkAlpha(const mcGroup_t* g) {
		if (g->linkAlpha.size() == g->linkA.size()) return;
		g->linkAlpha.resize(g->linkA.size());
		for (size_t l = 0; l < g->linkA.size(); l++) {
			const int a = g->linkA[l], b = g->linkB[l];
			g->linkAlpha[l] = g->linkBend[l] ? BendCompliance(0.5f * (g->bend[a] + g->bend[b]))
				: GiveToCompliance(0.5f * (g->stretchG[a] + g->stretchG[b]));
		}
	}

	// SJE: isometric bending stencils (r_meshClothBendModel 1). For an edge (x0 x1) shared by the triangles
	// (x0 x1 x2) and (x0 x1 x3), the curvature vector H = sum w_i x_i with the cotangent weights
	// w = { c03 + c04, c01 + c02, -c01 - c03, -c02 - c04 } (Bergou, Wardetzky, Harmon, Zorin & Grinspun 2006,
	// "A Quadratic Bending Model for Inextensible Surfaces"; c0k = cot of the angle at x0, c1k at x1).
	// Lazy, main thread.
	static float CotAngle(const float* u, const float* v) {
		vec3_t c;
		mcCross(u, v, c);
		const float s = mcLen(c), d = mcDot(u, v);
		if (s < 1e-6f) return d >= 0.0f ? 20.0f : -20.0f;
		const float r = d / s;
		return r > 20.0f ? 20.0f : (r < -20.0f ? -20.0f : r);
	}
	static void ComputeBendStencils(const mcGroup_t* g) {
		if (g->bendBuilt) return;
		g->bendBuilt = true;
		g->bendSt.clear(); g->bendW.clear(); g->bendAlpha.clear(); g->bendBs.clear();
		if ((int)g->bindPos.size() < g->numParticles * 3) return;
		std::unordered_map<unsigned long long, int> firstTri;	// edge -> opposite point of the first triangle on it
		std::unordered_map<unsigned long long, char> usedEdge;
		auto ekey = [](int a, int b) { if (a > b) { const int t = a; a = b; b = t; } return ((unsigned long long)a << 32) | (unsigned)b; };
		for (size_t t = 0; t + 2 < g->tris.size(); t += 3) {
			const int pi[3] = { g->tris[t], g->tris[t + 1], g->tris[t + 2] };
			if (pi[0] == pi[1] || pi[1] == pi[2] || pi[0] == pi[2]) continue;
			for (int k = 0; k < 3; k++) {
				const int x0 = pi[k], x1 = pi[(k + 1) % 3], o = pi[(k + 2) % 3];
				if (x0 < 0 || x1 < 0 || o < 0 || x0 >= g->numParticles || x1 >= g->numParticles || o >= g->numParticles) continue;
				const unsigned long long ek = ekey(x0, x1);
				auto it = firstTri.find(ek);
				if (it == firstTri.end()) { firstTri[ek] = o; continue; }
				if (usedEdge.count(ek) || it->second == o) continue;
				usedEdge[ek] = 1;
				const int x2 = it->second, x3 = o;
				if (g->freedom[x0] <= 0.0f && g->freedom[x1] <= 0.0f && g->freedom[x2] <= 0.0f && g->freedom[x3] <= 0.0f) continue;
				const float* p0 = &g->bindPos[x0 * 3], * p1 = &g->bindPos[x1 * 3], * p2 = &g->bindPos[x2 * 3], * p3 = &g->bindPos[x3 * 3];
				vec3_t e0, e1, e2, e3, e4, ne0, c;
				mcSub(p1, p0, e0); mcSub(p2, p0, e1); mcSub(p3, p0, e2); mcSub(p2, p1, e3); mcSub(p3, p1, e4);
				mcScale(e0, -1.0f, ne0);
				mcCross(e0, e1, c); const float A0 = 0.5f * mcLen(c);
				mcCross(e0, e2, c); const float A1 = 0.5f * mcLen(c);
				if (A0 < 1e-4f || A1 < 1e-4f) continue;
				const float c01 = CotAngle(e0, e1), c02 = CotAngle(e0, e2), c03 = CotAngle(ne0, e3), c04 = CotAngle(ne0, e4);
				const int st[4] = { x0, x1, x2, x3 };
				const float w[4] = { c03 + c04, c01 + c02, -c01 - c03, -c02 - c04 };
				float bs = 0.0f;
				for (int j = 0; j < 4; j++) { g->bendSt.push_back(st[j]); g->bendW.push_back(w[j]); bs += 0.25f * g->bend[st[j]]; }
				g->bendBs.push_back(bs);
				g->bendAlpha.push_back(BendCompliance(bs));
			}
		}
	}

	// Solver settings for one substep, from mcParams_t
	struct mcSolve_t {
		float	bendScale;	// exponent for the old projection's bend (1 / substeps)
		float	h;			// substep length (s)
		int		xpbd;		// 1: XPBD, 0: old projection
		int		xpbdIters;	// XPBD passes per substep
		float	liftLimit;	// -2 = per surface, -1 = no limit, N = units
		float	liftSlope;	// SJE: the lift limit drops by this much per unit of cloth length from the attached edge
		int		floorOk = 0;	// SJE: the floor under the character (the sloped lift limit never lies under it)
		float	floorPt[3] = { 0.0f, 0.0f, 0.0f }, floorN[3] = { 0.0f, 0.0f, 1.0f };
		int		edgeCol;	// 1: cloth edges against arms, fists and hilts
		int		faceCol;	// SJE: 1: cloth triangles against every body shape (FaceCollide; replaces edgeCol)
		int		faceNow;	// SJE: run it in this substep (every other one, and the last)
		int		overArms;	// SJE r_meshClothOverArms: cloth hanging from shoulder height lies over arms and hands
		int		rest;		// SJE r_meshClothRest: 1 = on
		int		ease;		// SJE r_meshClothEase: 1 = on
		float	tetherK;	// SJE: fraction of the over-tether distance reeled in per substep (per-step constant)
		const float* bendK;	// SJE: per link, old projection's bend fraction per substep (nullptr = work it out)
		int		bendModel;	// SJE: 1 = isometric bending stencils instead of the bend links
		const int* ptStart = nullptr;			// SJE: per point, the body shapes it can reach in this step (BuildPointCaps):
		const unsigned char* ptIdx = nullptr;	// ... ptIdx[ptStart[p]] to ptIdx[ptStart[p + 1] - 1]; nullptr = test all shapes
		const float* ptFrom = nullptr;			// ... where the points were when the lists were built (3 per point)
		const float* ptAllow = nullptr;			// ... and how far from there each list holds
	};

	// SJE: isometric bending (r_meshClothBendModel 1). The curvature vector of each stencil, H(x) = sum w_i x_i,
	// is zero for a flat stencil and points along the stencil's normal as it folds. Its size along the cloth's
	// own current normal is pulled toward the animated cloth's (h = H(t) . n(t)), a scalar XPBD constraint
	// C = H(x) . n(x) - h with the gradient w_i n(x) (the normal held for the pass). So a cape swung out keeps
	// the folds the model has, in whatever direction it hangs, instead of being pulled toward the animated pose.
	static void StencilNormal(const float* x0, const float* x1, const float* x2, const float* x3, float* n) {
		vec3_t e0, e1, e2, n1, n2;
		mcSub(x1, x0, e0); mcSub(x2, x0, e1); mcSub(x3, x0, e2);
		mcCross(e0, e1, n1);
		mcCross(e2, e0, n2);
		mcNorm(n1); mcNorm(n2);
		mcAdd(n1, n2, n);
		mcNorm(n);
	}
	static void SolveBendStencils(const mcGroup_t* g, mcInstance_t* in, const mcSolve_t& sv) {
		const size_t ns = g->bendAlpha.size();
		if (!ns || g->bendSt.size() != ns * 4 || g->bendW.size() != ns * 4) return;
		if (in->bendLambda.size() != ns) in->bendLambda.resize(ns);
		std::fill(in->bendLambda.begin(), in->bendLambda.end(), 0.0f);
		const float ih2 = 1.0f / (sv.h * sv.h);
		const int iters = !sv.xpbd ? 1 : (sv.xpbdIters < 1 ? 1 : (sv.xpbdIters > 8 ? 8 : sv.xpbdIters));
		for (int it = 0; it < iters; it++) {
			for (size_t s = 0; s < ns; s++) {
				const float alpha = g->bendAlpha[s];
				if (alpha < 0.0f) continue;
				const int* st = &g->bendSt[s * 4];
				const float* w = &g->bendW[s * 4];
				float wi[4], den = 0.0f;
				for (int j = 0; j < 4; j++) { wi[j] = g->freedom[st[j]] > 0.0f ? 1.0f : 0.0f; den += wi[j] * w[j] * w[j]; }
				if (den < 1e-6f) continue;
				const float* x[4] = { &in->p[st[0] * 3], &in->p[st[1] * 3], &in->p[st[2] * 3], &in->p[st[3] * 3] };
				const float* t[4] = { &in->target[st[0] * 3], &in->target[st[1] * 3], &in->target[st[2] * 3], &in->target[st[3] * 3] };
				vec3_t H = { 0, 0, 0 }, Ht = { 0, 0, 0 }, nx, nt;
				for (int j = 0; j < 4; j++) { mcMA(H, w[j], x[j], H); mcMA(Ht, w[j], t[j], Ht); }
				StencilNormal(x[0], x[1], x[2], x[3], nx);
				StencilNormal(t[0], t[1], t[2], t[3], nt);
				if (mcDot(nx, nx) < 0.5f || mcDot(nt, nt) < 0.5f) continue;	// folded flat onto itself: no normal
				const float C = mcDot(H, nx) - mcDot(Ht, nt);
				float dl;
				if (sv.xpbd) {
					const float at = alpha * ih2;
					dl = (-C - at * in->bendLambda[s]) / (den + at);
					in->bendLambda[s] += dl;
				}
				else {
					// old projection: the per-substep fraction of the bend, as for the bend links
					const float bs = g->bendBs[s] < 0.99f ? g->bendBs[s] : 0.99f;
					const float kb = bs <= 0.0f ? 0.0f : 1.0f - powf(1.0f - bs, sv.bendScale);
					dl = -C * kb / den;
				}
				for (int j = 0; j < 4; j++) {
					if (wi[j] <= 0.0f) continue;
					float* pj = &in->p[st[j] * 3];
					mcMA(pj, w[j] * dl, nx, pj);
				}
			}
		}
	}

	const unsigned char MC_THROWN_STEPS = 180;	// SJE r_meshClothRest: steps a point counts as thrown 'back to its own side' (3 s, as the shake watchdog holds)
	const unsigned char MC_LIMB_STEPS = 4;	// SJE r_meshClothCalm: steps a point counts as moved by a limb after the triangle test's limb pair moved it
	// SJE: three rules let cloth come to rest on a body that holds still. Each is on (1) or off (0):
	// r_meshClothCalm - a self-collision pair that shakes loses its closing speed (SolveSelfPairs), and a point
	//   that only trembles in place counts as at rest (Step);
	// r_meshClothRest - cloth that was thrown 'back to its own side' of a body shape and lies clear is not thrown
	//   again, and an arm holds cloth the animation partly holds no further off than the pose drapes it (FaceCollide);
	// r_meshClothEase - a point that trips the shake watchdog is let go over MC_FIGHT_RAMP steps, not at once; a
	//   point the watchdog gave back at rest is moved gently by the body's triangle test and the layer rule; and a
	//   triangle that has been lying at rest is not thrown 'back to its own side' (FaceCollide, SolveLayerPairs).
	// SJE: self-collision every substep (r_meshClothSelfSub 1; Mueller, "Ten Minute Physics" 15): the close
	// pairs are found once per 16 ms step with a widened radius (BuildSelfPairs, below SelfCollide), then
	// kept apart in every substep, with cloth-on-cloth friction: a touching pair loses part of its sliding
	// against each other (their velocities are blended toward the pair's mean).
	// SJE r_meshClothCalm. The separation moves both points and their previous positions alike ("a separation,
	// not a throw"), so a pair keeps all of its closing speed: where the cloth's own links press two folds together
	// (a cape heaped round a crouching body) the two points fell into each other again in every substep and were
	// pushed apart again - more than a unit, sixty times a second, for as long as the pose was held. Nothing came
	// to rest: the pair crept, snapped, or the two points circled each other for good (a cape that never stopped
	// twitching in a crouch). Now a pair with a point the shake watchdog has tripped (FightUpdate: it jumps to and
	// fro where the pose holds it still) loses its closing speed at the contact, out of both points alike; the
	// two still part freely and slide along each other, with the friction below.
	// Not where an arm, a hand or a held thing has moved one of the two in the last steps (limbPush, set by the
	// triangle test's limb pairs): there the limb rules decide where the cloth goes, and it may have to pass its
	// own folds on the way. With the closing speed taken out of every touching pair, the cape that is brought out
	// over a hand behind the back was held back by its own folds and the hand lay on top again.
	static void SolveSelfPairs(const mcGroup_t* g, mcInstance_t* in, float frictionScale, bool calm, float trip) {
		if (in->selfPairs.empty()) return;
		const bool watch = calm && in->fight.size() == (size_t)g->numParticles;
		const bool limbs = in->limbPush.size() == (size_t)g->numParticles;
		const float thick = g->selfThick * in->linkScale;
		if (!(thick > 0.05f) || thick > 64.0f) return;
		for (size_t i = 0; i + 1 < in->selfPairs.size(); i += 2) {
			const int p = in->selfPairs[i], q = in->selfPairs[i + 1];
			float* a = &in->p[p * 3], * b = &in->p[q * 3];
			vec3_t d;
			mcSub(a, b, d);
			const float len = mcLen(d);
			if (len >= thick || len < 1e-5f) continue;
			const bool hot = watch && (in->fight[p] >= trip || in->fight[q] >= trip)	// it shakes here ...
				&& !(limbs && (in->limbPush[p] || in->limbPush[q]));					// ... and no limb is moving this cloth
			const float corr = 0.5f * (thick - len) / len;
			mcMA(a, corr, d, a);
			mcMA(b, -corr, d, b);
			// a separation, not a throw
			float* pa = &in->prev[p * 3], * pb = &in->prev[q * 3];
			mcMA(pa, corr, d, pa);
			mcMA(pb, -corr, d, pb);
			if (hot) {
				// the pair's closing speed ends at the contact
				vec3_t n, va0, vb0;
				mcScale(d, 1.0f / len, n);
				mcSub(a, pa, va0);
				mcSub(b, pb, vb0);
				const float vn = mcDot(va0, n) - mcDot(vb0, n);
				if (vn < 0.0f) {
					mcMA(pa, 0.5f * vn, n, pa);
					mcMA(pb, -0.5f * vn, n, pb);
				}
			}
			// friction: blend both velocities toward their mean (v = p - prev, so prev moves the other way)
			const float fk = Clamp01(0.35f * (g->fricK[p] + g->fricK[q]) * frictionScale);	// default 0.21
			if (fk <= 0.0f) continue;
			vec3_t va, vb, dv;
			mcSub(a, pa, va);
			mcSub(b, pb, vb);
			mcSub(vb, va, dv);			// mean - va = 0.5 (vb - va)
			mcMA(pa, -0.5f * fk, dv, pa);
			mcMA(pb, 0.5f * fk, dv, pb);
		}
	}

	// One pass over every constraint, Gauss-Seidel. bendK is the per-substep
	// fraction of the bend error removed (stiffness made independent of the
	// substep count by the caller).
	void SolveConstraints(const mcGroup_t* g, mcInstance_t* in, const mcCapsule_t* caps, int ncaps, const mcSolve_t& sv) {
		const float bendScale = sv.bendScale;
		if ((int)in->contact.size() != g->numParticles) {
			in->contact.assign(g->numParticles, 0);
			in->push.assign((size_t)g->numParticles * 3, 0.0f);
			in->contactN.assign((size_t)g->numParticles * 3, 0.0f);
		}
		// attached vertices first: everything below measures against them, and
		// a vertex solved before its attachment point must not see where that
		// point was (on a jump that's a whole frame of movement lower)
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] <= 0.0f) mcCopy(&in->target[p * 3], &in->p[p * 3]);
		}
		// stretch: the fabric keeps the edge lengths the animated mesh has now
		// (rigidly - real cloth barely stretches); bend links resist folding
		if (sv.xpbd && g->linkAlpha.size() == g->linkA.size()) {
			// XPBD (r_meshClothXPBD): compliant constraints with accumulated multipliers
			if (in->lambda.size() != g->linkA.size()) in->lambda.resize(g->linkA.size());
			std::fill(in->lambda.begin(), in->lambda.end(), 0.0f);
			const float ih2 = 1.0f / (sv.h * sv.h);
			const int iters = sv.xpbdIters < 1 ? 1 : (sv.xpbdIters > 8 ? 8 : sv.xpbdIters);
			for (int it = 0; it < iters; it++) {
				for (size_t l = 0; l < g->linkA.size(); l++) {
					const float alpha = g->linkAlpha[l];
					if (alpha < 0.0f) continue;
					if (sv.bendModel && g->linkBend[l]) continue;	// SJE: the stencils below bend it instead
					const int a = g->linkA[l], b = g->linkB[l];
					const float wa = g->freedom[a] > 0.0f ? 1.0f : 0.0f, wb = g->freedom[b] > 0.0f ? 1.0f : 0.0f;
					if (wa + wb == 0.0f) continue;
					float* pa = &in->p[a * 3], * pb = &in->p[b * 3];
					vec3_t dT, d;
					mcSub(&in->target[b * 3], &in->target[a * 3], dT);
					const float rest = mcLen(dT);
					mcSub(pb, pa, d);
					const float len = mcLen(d);
					if (len < 1e-5f) continue;
					const float C = len - rest;
					const float at = alpha * ih2;
					const float dl = (-C - at * in->lambda[l]) / (wa + wb + at);
					in->lambda[l] += dl;
					// gradient: -n at a, +n at b (n = d / len)
					const float s = dl / len;
					mcMA(pa, -s * wa, d, pa);
					mcMA(pb, s * wb, d, pb);
				}
			}
		}
		else {
			for (size_t l = 0; l < g->linkA.size(); l++) {
				const int a = g->linkA[l], b = g->linkB[l];
				const float wa = g->freedom[a] > 0.0f ? 1.0f : 0.0f, wb = g->freedom[b] > 0.0f ? 1.0f : 0.0f;
				if (wa + wb == 0.0f) continue;
				float* pa = &in->p[a * 3], * pb = &in->p[b * 3];
				vec3_t dT, d;
				mcSub(&in->target[b * 3], &in->target[a * 3], dT);
				const float rest = mcLen(dT);
				mcSub(pb, pa, d);
				const float len = mcLen(d);
				if (len < 1e-5f) continue;
				float k = (len - rest) / (len * (wa + wb));
				if (g->linkBend[l]) {
					if (sv.bendModel) continue;	// SJE: the stencils below bend it instead
					// the fraction of the fold removed per substep, so that one
					// 16 ms step removes 'bend' of it whatever the substep count
					// (SJE: worked out once per step in Step, not per link per substep)
					float kb;
					if (sv.bendK) kb = sv.bendK[l];
					else {
						const float bs = 0.5f * (g->bend[a] + g->bend[b]);
						kb = bs <= 0.0f || bendScale <= 0.0f ? 0.0f : 1.0f - powf(1.0f - (bs < 0.99f ? bs : 0.99f), bendScale);
					}
					if (kb <= 0.0f) continue;
					k *= kb;
				}
				mcMA(pa, k * wa, d, pa);
				mcMA(pb, -k * wb, d, pb);
			}
		}
		if (sv.bendModel) SolveBendStencils(g, in, sv);
		for (int p = 0; p < g->numParticles; p++) {
			const float f = g->freedom[p];
			float* pp = &in->p[p * 3];
			const float* t = &in->target[p * 3];
			if (f <= 0.0f) { mcCopy(t, pp); continue; }
			// long range attachment: never further from the attached edge than
			// the cloth itself is long
			const int an = g->lraAnchor[p];
			if (an >= 0 && !MCDBG_OFF("NOLRA")) {
				vec3_t d;
				mcSub(pp, &in->p[an * 3], d);
				// skinning can stretch the animated mesh past its bind size
				// (raised shoulders): never limit tighter than the pose itself
				vec3_t dt;
				mcSub(t, &in->target[an * 3], dt);
				float maxLen = g->lraLen[p] * in->linkScale;
				const float animLen = mcLen(dt);
				if (animLen > maxLen) maxLen = animLen;
				maxLen *= 1.03f;
				const float len = mcLen(d);
				if (len > maxLen && len > 1e-5f) mcMA(&in->p[an * 3], maxLen / len, d, pp);
			}
			// never higher than both the attached edge it hangs from and its
			// animated spot: stops a fast turn or a spawn from flipping a cape
			// up over the shoulders
			// (per surface 'lift', or r_meshClothLift for all; below 0 = no limit)
			const float liftL = sv.liftLimit > -1.5f ? sv.liftLimit : g->lift[p];
			if (an >= 0 && liftL >= 0.0f && !MCDBG_OFF("NOHEIGHT")) {
				// SJE: ... and the limit slopes down from the attached edge (r_meshClothLiftSlope): a point may rise to
				// that part of its cloth length under the edge, not level with it. Level, a cape thrown up in a jump
				// or a fall lay as a flat sheet at shoulder height, spread out sideways like wings; sloped it streams
				// out behind and droops to its hem.
				const float hp = mcDot(pp, in->up), ha = mcDot(&in->p[an * 3], in->up) - sv.liftSlope * g->lraLen[p] * in->linkScale, ht = mcDot(t, in->up);
				float lim = (ha > ht ? ha : ht) + liftL * in->linkScale;
				// SJE: ... but the sloped limit never lies under the floor. In a low pose (lying, sitting, kneeling) the
				// attached edge is near the ground and the limit, lower by a part of the cloth's length, under it:
				// every substep the hem was pushed 6-13 units down to it, the body tests ran with the cloth under the
				// ground, and the floor pushed it back up. The cloth may always lie on the floor.
				if (sv.floorOk && sv.liftSlope > 0.0f) {
					const float un = mcDot(in->up, sv.floorN);
					if (un > 0.5f) {
						vec3_t fr;
						mcSub(pp, sv.floorPt, fr);
						const float hf = hp - (mcDot(fr, sv.floorN) - MC_FLOOR_GAP) / un;	// the height at which this point lies on the floor
						if (hf > lim) lim = hf;
					}
				}
				if (hp > lim) mcMA(pp, lim - hp, in->up, pp);
			}
			vec3_t before;
			mcCopy(pp, before);
			const float cmargin = MC_MARGIN > 0.5f * g->thick[p] ? MC_MARGIN : 0.5f * g->thick[p];
			const bool chard = g->hardCol[p] != 0 && f > 0.5f;
			// SJE: only the shapes this point can reach in this step (BuildPointCaps) - unless it has moved further from
			// where it was then than its list allowed for: then all shapes, so the result is always that of the full test
			bool nearOk = false;
			if (sv.ptStart) {
				vec3_t mv0;
				mcSub(pp, &sv.ptFrom[p * 3], mv0);
				nearOk = mcDot(mv0, mv0) <= sv.ptAllow[p] * sv.ptAllow[p];
			}
			bool chit, left = false;
			if (nearOk) {
				chit = Collide(pp, t, caps, sv.ptStart[p + 1] - sv.ptStart[p], &in->contactN[p * 3], chard, cmargin, (sv.faceCol & 1) != 0, sv.ptIdx + sv.ptStart[p],
					&sv.ptFrom[p * 3], sv.ptAllow[p] * sv.ptAllow[p], &left);
				if (left) {
					// a push took it out of its list's range: again from where it stood, with all shapes
					mcCopy(before, pp);
				}
			}
			if (!nearOk || left) chit = Collide(pp, t, caps, ncaps, &in->contactN[p * 3], chard, cmargin, (sv.faceCol & 1) != 0);
			if (chit) {
				in->contact[p] = 1;
				vec3_t pu;
				mcSub(pp, before, pu);
				mcAdd(&in->push[p * 3], pu, &in->push[p * 3]);
			}
			// max distance from the animated position. Soft: past the limit the
			// cloth is reeled back in over ~100 ms instead of being projected
			// onto it. A hard limit tied to the animated pose made the cape jump
			// instantly whenever the animation changed pose (landing from a jump,
			// starting a saber move) while the cape was out at the limit, e.g.
			// lifted up during a fall. A hard stop remains at twice the limit.
			vec3_t d;
			mcSub(pp, t, d);
			float maxLen = f * g->tether[p];
			if (g->reach > 0.0f && (int)g->rootDist.size() == g->numParticles) {
				const float rl = g->reach * g->rootDist[p] * in->linkScale;
				if (rl < maxLen) maxLen = rl;
			}
			const float len = mcLen(d);
			if (len > maxLen && len > 1e-5f && !MCDBG_OFF("NOTETHER")) {
				const float k = sv.tetherK;	// ~15% per 16/6 ms, frame-rate independent (SJE: per-step constant)
				float target = len - (len - maxLen) * k;
				if (target > 2.0f * maxLen) target = 2.0f * maxLen;
				mcMA(t, target / len, d, pp);
#ifdef MC_DEBUG
				g_mcDbgTether++;
#endif
			}
		}
		if (sv.faceCol & 1) { if (sv.faceNow) FaceCollide(g, in, caps, ncaps, sv.overArms, sv.rest, sv.ease); }	// SJE: triangles against the whole body (covers the edges too)
		else if (sv.edgeCol && !MCDBG_OFF("NOEDGECOL")) EdgeCollide(g, in, caps, ncaps);
	}

	// Edge against capsule (r_meshClothEdgeCol): arms, fists and hilts are thinner than the gap between
	// neighbouring cloth points on coarse (grid-simulated) capes, so a point-only test lets a forearm slip
	// between two points and end up on the wrong side of the cape. Here each cloth edge is tested against
	// the limb axis: if the closest pair of points is inside the limb, both ends of the edge are pushed out
	// along the separating direction, shared by how close each end is.
	static void ClosestSegSeg(const float* p1, const float* q1, const float* p2, const float* q2, float& s, float& t) {
		vec3_t d1, d2, r;
		mcSub(q1, p1, d1); mcSub(q2, p2, d2); mcSub(p1, p2, r);
		const float a = mcDot(d1, d1), e = mcDot(d2, d2), f = mcDot(d2, r);
		if (a < 1e-8f && e < 1e-8f) { s = t = 0.0f; return; }
		if (a < 1e-8f) { s = 0.0f; t = Clamp01(f / e); return; }
		const float c = mcDot(d1, r);
		if (e < 1e-8f) { t = 0.0f; s = Clamp01(-c / a); return; }
		const float b = mcDot(d1, d2), den = a * e - b * b;
		s = den > 1e-8f ? Clamp01((b * f - c * e) / den) : 0.0f;
		t = (b * s + f) / e;
		if (t < 0.0f) { t = 0.0f; s = Clamp01(-c / a); }
		else if (t > 1.0f) { t = 1.0f; s = Clamp01((b - c) / a); }
	}
	static void EdgeCollide(const mcGroup_t* g, mcInstance_t* in, const mcCapsule_t* caps, int ncaps) {
		for (int c = 0; c < ncaps; c++) {
			if (!caps[c].hard || caps[c].hard == 3) continue;	// limbs and hilts only, not the fitted body (r_meshClothBodyFit)
			const float minD = caps[c].r + MC_MARGIN;
			// SJE: cheap reject first - a bounding sphere round the capsule; an edge that can't reach it is skipped
			vec3_t cab, mid;
			mcSub(caps[c].b, caps[c].a, cab);
			mcMA(caps[c].a, 0.5f, cab, mid);
			const float sphR = 0.5f * mcLen(cab) + minD;
			for (size_t l = 0; l < g->linkA.size(); l++) {
				if (g->linkBend[l]) continue;
				const int a = g->linkA[l], b = g->linkB[l];
				const float wa = g->freedom[a] > 0.0f && g->hardCol[a] ? 1.0f : 0.0f, wb = g->freedom[b] > 0.0f && g->hardCol[b] ? 1.0f : 0.0f;
				if (wa + wb == 0.0f) continue;
				float* pa = &in->p[a * 3], * pb = &in->p[b * 3];
				{
					vec3_t ab, am;
					mcSub(pb, pa, ab); mcSub(mid, pa, am);
					const float L = mcDot(ab, ab);
					const float u = L > 1e-8f ? Clamp01(mcDot(am, ab) / L) : 0.0f;
					vec3_t q, dq;
					mcMA(pa, u, ab, q); mcSub(mid, q, dq);
					if (mcDot(dq, dq) > sphR * sphR) continue;
				}
				float s, t;
				ClosestSegSeg(pa, pb, caps[c].a, caps[c].b, s, t);
				if (s < 0.02f || s > 0.98f) continue;		// an end point: the point test handles it
				vec3_t cp, ca, ab, n;
				mcSub(pb, pa, ab); mcMA(pa, s, ab, cp);
				mcMA(caps[c].a, t, cab, ca);
				mcSub(cp, ca, n);
				const float d = mcLen(n);
				if (d >= minD) continue;
				// which side of the limb this piece of cloth belongs on: the side the animated cloth is on.
				// Where the animated cloth itself wraps the limb (modelled around it), leave it alone.
				const float* ta = &in->target[a * 3], * tb = &in->target[b * 3];
				float st, tt;
				ClosestSegSeg(ta, tb, caps[c].a, caps[c].b, st, tt);
				vec3_t tcp, tca, tab, nt;
				mcSub(tb, ta, tab); mcMA(ta, st, tab, tcp);
				mcMA(caps[c].a, tt, cab, tca);
				mcSub(tcp, tca, nt);
				const float dt = mcLen(nt);
				if (dt < minD || dt < 1e-4f) continue;
				mcScale(nt, 1.0f / dt, nt);
				// distance of the cloth's closest point from the limb axis, measured towards its own side
				// (negative = it has gone through to the other side)
				const float side = mcDot(n, nt);
				float depth = minD - side;
				if (depth <= 0.0f) continue;
				if (depth > 3.0f) depth = 3.0f;			// a little per substep: no tearing
				const float ka = wa * (1.0f - s), kb = wb * s, den = ka * ka + kb * kb;
				if (den < 1e-6f) continue;
				const float lam = depth / den;
				mcMA(pa, lam * ka, nt, pa);
				mcMA(pb, lam * kb, nt, pb);
				if (wa > 0.0f) { in->contact[a] = 1; mcCopy(nt, &in->contactN[a * 3]); }
				if (wb > 0.0f) { in->contact[b] = 1; mcCopy(nt, &in->contactN[b * 3]); }
			}
		}
	}

	// SJE: triangle against body (r_meshClothFaceCol). The point test above only stops the cloth's points
	// entering the body, and the edge test only covers arms, fists and hilts. On a coarse cape (Vader's has
	// 10-15 units between points) a leg, the hips or the back slides in between three points and comes out
	// through the triangle. Here each cloth triangle near a body shape - limbs, hilts, the torso capsules and
	// the fitted body shapes alike - is tested against the shape's axis: the closest points of the two, with
	// the push shared by the triangle's corners by their barycentric weights (as one 'virtual particle' at the
	// closest point, NvCloth / PhysX). Its edges and corners are part of the triangle, so this covers the edge
	// test too.
	//  - The side of a shape the cloth belongs on is the side the animated cloth is on: a triangle that slipped
	//    through is pulled back to its own side, not pushed out on the far side.
	//  - As in the point test against the torso, it is not pushed further out than the animation has it: coarse
	//    capes are often modelled a little into the body shapes (a triangle cuts the corner of the body's curve
	//    between its points), and pushing those out would float the cape off the body.
	//  - As in the point test, a push is kept as a move but not turned into speed (ContactVelocity).
	//  - The pairs that can touch are found once per 16 ms step, and tested in every other substep and the
	//    last (Vader: the whole simulation takes about a third longer; every substep would be about twice that).
	const float MC_FACE_STEP = 3.0f;	// largest push per substep (no tearing)
	const float MC_EASE_STEP = 0.05f;	// SJE r_meshClothEase: ... and for a point the shake watchdog gave back while the pose holds it still
	const float MC_FACE_TOOL = 0.8f;	// SJE: extra distance cloth triangles keep from held things (hilt, blade)
	const float MC_FACE_WORN = 3.0f;	// cloth the animation has within this of a body shape's surface is worn on it there
	const float MC_FACE_OVER = 0.5f;	// ... and a limb this much further out than that cloth lies on it
	const float MC_FACE_ABOVE = 6.0f;	// SJE: cloth hangs down over a limb if its attached anchor is this much higher than the limb
	const float MC_FACE_REST = 0.25f;	// SJE: a limb rests against a body shape if it moves less than this against it in a step
	const float MC_FACE_OUTSIDE = 0.3f;	// SJE: cloth lies over a limb if it is on its own outer side of it by this (cosine against its outward normal)
	const float MC_FACE_TIGHT = 0.75f;	// ... if the cloth lies this near the body shape (skin-tight), or
	const float MC_FACE_SLACK = 16.0f;	// ... if the cloth there is nearer than this to its attached edge (bind units along the cloth)
	// SJE: the body's own travel inside its animation (the attached edge's centre against the character's origin).
	// Slower than MC_BODY_SLOW units/s (a stride's bob, a lean, a swing) the cloth feels all of it; from
	// MC_BODY_FAST on it is carried along whole.
	const float MC_BODY_SLOW = 90.0f;
	const float MC_BODY_FAST = 220.0f;
	const float MC_AIR_MAX = 320.0f;	// units/s of the character's travel that count for r_meshClothDragQuad (a sprint)
	const float MC_AIR_FALL = 0.4f;		// part of the character's falling speed that counts for it
	const float MC_AIR_TETHER = 40.0f;	// cloth with this tether or more catches all of the travel wind
	// SJE: r_meshClothBreeze: still air is never still. A light breeze that wanders in strength and direction over
	// seconds (three slow sines per level axis, their phase also from the place, so no two points and no two
	// characters move alike) pushes hanging cloth through the same normal drag as the wind does: the hem of a long
	// cape sways about a unit instead of hanging frozen. Scaled by the tether like the travel wind (an inner robe
	// or a tunic stays put), level only (cloth lying on the ground does not feel it), not under water.
	// Only near the camera (MC_BREEZE_NEAR, fading out to MC_BREEZE_FAR, by the same field-of-view scaled distance
	// as the level of detail): cloth the breeze sways does not come to rest, so it does not sleep. Sleep is not
	// forbidden, the bar is only higher: in the breeze cloth is at rest when no free point moves MC_BREEZE_STILL
	// in a step (hanging cloth in the breeze never gets that slow; a dead character's cape on the ground, a tunic
	// or cloth at the edge of the range does, and sleeps as before). Sleeping cloth wakes when the breeze reaches
	// it (the camera comes near, the cvar is switched on).
	// And only cloth that hangs relaxed. Cloth that is held stretched somewhere (draped over a crouching body,
	// caught under another layer or under a foot: a link pulled longer than MC_BREEZE_TAUT0 times its length) keeps
	// still only because nothing moves it - a crouching character's cape shook all the time in the breeze. Such
	// cloth gets none (fading out up to MC_BREEZE_TAUT1, quickly; coming back slowly) and sleeps as before.
	const float MC_BREEZE_SPEED = 40.0f;	// units/s of air at r_meshClothBreeze 1 (the sines' peak)
	const float MC_BREEZE_NEAR = 300.0f;
	const float MC_BREEZE_FAR = 450.0f;
	const float MC_BREEZE_TAUT0 = 1.25f;	// the most stretched link at this many times its animated length: still all of the breeze
	const float MC_BREEZE_TAUT1 = 1.5f;		// at this: none
	const float MC_BREEZE_TAUT_IN = 1.0f / 8.0f;	// per step: the breeze goes within 8 steps of the cloth being pulled taut
	const float MC_BREEZE_TAUT_OUT = 1.0f / 90.0f;	// and comes back over a second and a half
	const float MC_BREEZE_STILL = 0.002f;	// units a step: in the breeze, cloth whose fastest point is slower than this is at rest
											// (a long cape in the breeze: never under 0.003; MC_REST_SPEED is 15 times this)
	static float BreezeNow(const mcParams_t* prm) {
		if (!(prm->breeze > 0.0f)) return 0.0f;
		const float f = (MC_BREEZE_FAR - prm->lodDist) / (MC_BREEZE_FAR - MC_BREEZE_NEAR);
		return f <= 0.0f ? 0.0f : (f >= 1.0f ? prm->breeze : prm->breeze * f);
	}
	// SJE: r_meshClothBillow: a cape in the air stream fills out. The travel wind alone only drags it straight
	// back, like a board on a hinge; here the cloth is also pushed outwards, away from the wearer's upright axis,
	// along its own normal: most a third of the way down its length, nothing at the attached edge and at the hem,
	// so it stands off the body and the lower cloth hangs from the bulge. With the square of the level travel
	// speed at a walk, levelling off from a run on; on the ground only (a jump or a fall adds nothing: no cape
	// blown up to the shoulders); behind the body only (cloth the air presses against the body is not lifted
	// off it); and by the tether as above.
	const float MC_BILLOW_K = 0.12f;		// 1/unit at r_meshClothBillow 1 (at a walk)
	const float MC_BILLOW_FULL = 100.0f;	// units/s of level travel at which it has reached half of what it levels off at
	const float MC_BILLOW_AIR0 = 36.0f;	// the character's origin this high over the floor: still on the ground (it stands at about 26)
	const float MC_BILLOW_AIR1 = 60.0f;	// this high or no floor found under it: in the air, no billow
	const float MC_FACE_NEAR = 0.25f;	// a triangle whose corners are all this close to the animated ones is left alone

	// closest point of the triangle (a b c) to p, as barycentric coordinates (Ericson, Real-Time Collision Detection 5.1.5)
	static void ClosestPtTri(const float* p, const float* a, const float* b, const float* c, float* bc) {
		vec3_t ab, ac, ap, bp, cp;
		mcSub(b, a, ab); mcSub(c, a, ac); mcSub(p, a, ap);
		const float d1 = mcDot(ab, ap), d2 = mcDot(ac, ap);
		if (d1 <= 0.0f && d2 <= 0.0f) { bc[0] = 1.0f; bc[1] = bc[2] = 0.0f; return; }
		mcSub(p, b, bp);
		const float d3 = mcDot(ab, bp), d4 = mcDot(ac, bp);
		if (d3 >= 0.0f && d4 <= d3) { bc[1] = 1.0f; bc[0] = bc[2] = 0.0f; return; }
		const float vc = d1 * d4 - d3 * d2;
		if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
			const float v = d1 - d3 > 1e-12f ? d1 / (d1 - d3) : 0.0f;
			bc[0] = 1.0f - v; bc[1] = v; bc[2] = 0.0f;
			return;
		}
		mcSub(p, c, cp);
		const float d5 = mcDot(ab, cp), d6 = mcDot(ac, cp);
		if (d6 >= 0.0f && d5 <= d6) { bc[2] = 1.0f; bc[0] = bc[1] = 0.0f; return; }
		const float vb = d5 * d2 - d1 * d6;
		if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
			const float w = d2 - d6 > 1e-12f ? d2 / (d2 - d6) : 0.0f;
			bc[0] = 1.0f - w; bc[1] = 0.0f; bc[2] = w;
			return;
		}
		const float va = d3 * d6 - d5 * d4;
		if (va <= 0.0f && d4 - d3 >= 0.0f && d5 - d6 >= 0.0f) {
			const float s = (d4 - d3) + (d5 - d6);
			const float w = s > 1e-12f ? (d4 - d3) / s : 0.0f;
			bc[0] = 0.0f; bc[1] = 1.0f - w; bc[2] = w;
			return;
		}
		const float den = va + vb + vc;
		const float v = den > 1e-12f ? vb / den : 1.0f / 3.0f, w = den > 1e-12f ? vc / den : 1.0f / 3.0f;
		bc[0] = 1.0f - v - w; bc[1] = v; bc[2] = w;
	}

	// closest points of the triangle (x0 x1 x2) and the segment (a b): the triangle's barycentric coordinates,
	// the segment's parameter t, and their distance (0 where the segment passes through the triangle)
	static float ClosestSegTri(const float* x0, const float* x1, const float* x2, const float* a, const float* b, float* bc, float& t) {
		vec3_t e1, e2, nn, sab;
		mcSub(x1, x0, e1); mcSub(x2, x0, e2); mcCross(e1, e2, nn); mcSub(b, a, sab);
		const float n2 = mcDot(nn, nn);
		if (n2 > 1e-10f) {
			vec3_t ra, rb;
			mcSub(a, x0, ra); mcSub(b, x0, rb);
			const float da = mcDot(ra, nn), db = mcDot(rb, nn);
			if (da * db <= 0.0f && da != db) {
				const float tt = da / (da - db);
				vec3_t x, xp, c1, c2;
				mcMA(a, tt, sab, x);
				mcSub(x, x0, xp);
				mcCross(xp, e2, c1); mcCross(e1, xp, c2);
				const float v = mcDot(c1, nn) / n2, w = mcDot(c2, nn) / n2;
				if (v >= 0.0f && w >= 0.0f && v + w <= 1.0f) {
					bc[0] = 1.0f - v - w; bc[1] = v; bc[2] = w;
					t = tt;
					return 0.0f;
				}
			}
		}
		// apart: the closest pair has a segment end or a triangle edge in it
		const float* X[3] = { x0, x1, x2 };
		float best = 1e30f;
		for (int e = 0; e < 2; e++) {
			const float* s = e ? b : a;
			float bq[3];
			vec3_t q, d;
			ClosestPtTri(s, x0, x1, x2, bq);
			for (int k = 0; k < 3; k++) q[k] = bq[0] * x0[k] + bq[1] * x1[k] + bq[2] * x2[k];
			mcSub(q, s, d);
			const float d2 = mcDot(d, d);
			if (d2 < best) { best = d2; bc[0] = bq[0]; bc[1] = bq[1]; bc[2] = bq[2]; t = (float)e; }
		}
		for (int k = 0; k < 3; k++) {
			const float* p = X[k], * r = X[(k + 1) % 3];
			float s, u;
			ClosestSegSeg(p, r, a, b, s, u);
			vec3_t pr, q, c, d;
			mcSub(r, p, pr);
			mcMA(p, s, pr, q); mcMA(a, u, sab, c);
			mcSub(q, c, d);
			const float d2 = mcDot(d, d);
			if (d2 < best) { best = d2; bc[k] = 1.0f - s; bc[(k + 1) % 3] = s; bc[(k + 2) % 3] = 0.0f; t = u; }
		}
		return sqrtf(best);
	}

	// closest point of the capsule axis (a b) to p
	static void ClosestOnAxis(const mcCapsule_t& c, const float* p, float* out) {
		vec3_t ab, ap;
		mcSub(c.b, c.a, ab); mcSub(p, c.a, ap);
		const float L = mcDot(ab, ab);
		mcMA(c.a, L > 1e-8f ? Clamp01(mcDot(ap, ab) / L) : 0.0f, ab, out);
	}

	// the pairs of a triangle and a body shape that can touch during this step: from the shapes at the start of
	// the step (caps) and how far they and the triangles can move in it (capsEnd: the shapes at its end)
	// SJE: cloth layers (r_meshClothLayers). A character's cloth surfaces are simulated together but only collide
	// with the body, so two layers worn over each other - Vader's cape over his robe - pass through one another
	// from the hips down. Here the layering is read from the bind pose once: for every cloth point, the nearest
	// triangles of the other surfaces it lies over or under (within MC_LAYER_BIND units; surfaces sewn together,
	// i.e. sharing welded points, are one sheet and are left out), and the side of each triangle it is on. In every
	// substep a point is kept on that side of its triangles, the push shared between the point and the triangle's
	// corners, kept as a move and not as speed. Layers can part and meet again; they cannot swap sides.
	const float MC_LAYER_BIND = 14.0f;	// bind-pose distance up to which two surfaces count as layers of each other
	const float MC_LAYER_STEP = 2.0f;	// largest push per substep
	const float MC_LAYER_UNDER = 2.0f;	// SJE: a heap pose: the animation has more than MC_LAYER_PILE of the free cloth points
	const float MC_LAYER_PILE = 0.045f;	// ... this far under the floor (full at 1.5 x as many): then no layer order is kept
	const float MC_LAYER_SQUASH = 0.35f;	// SJE: nor where the animation crushes a triangle to this part of its bind area; full again at twice this

	// SJE: does the cloth wrap round the body here? Per particle, from the bind pose: the angle round the character's
	// upright axis that the particles of its own surface within MC_WRAP_BAND of a height cover (the full circle less
	// the largest gap). A robe or a skirt covers most of the circle right down to its hem: it is a tube round the
	// body, held by it all round. It cannot lie over an arm or a hilt, nor make room for another cloth layer, without
	// stretching. A cape or a cloak wraps round the shoulders at its collar but is an open sheet at its hem, a panel
	// or a half skirt covers a part of the circle: those hang loose. So a surface is a tube if it wraps at its lower
	// edge (robes and skirts measure 244 - 345 degrees there, capes, cloaks and panels 71 - 186), and a point of it
	// counts as wrapping if the surface wraps at the point's own height as well.
	const float MC_WRAP_BAND = 6.0f;		// units of height either way
	const float MC_WRAP_HEM = 220.0f;	// degrees the surface must cover at its lower edge ...
	const float MC_WRAP_ANGLE = 240.0f;	// ... and at the point's height
	static float WrapAngle(const mcGroup_t* g, int surf, float z, std::vector<float>& ang) {
		const int n = g->numParticles;
		ang.clear();
		for (int q = 0; q < n; q++) {
			if (g->partSurf[q] != surf || fabsf(g->bindPos[q * 3 + 2] - z) > MC_WRAP_BAND) continue;
			ang.push_back(atan2f(g->bindPos[q * 3 + 1], g->bindPos[q * 3]));
		}
		if (ang.size() < 6) return 0.0f;
		std::sort(ang.begin(), ang.end());
		float gap = ang.front() + 6.2831853f - ang.back();
		for (size_t k = 1; k < ang.size(); k++) if (ang[k] - ang[k - 1] > gap) gap = ang[k] - ang[k - 1];
		return 360.0f - gap * 57.29578f;
	}
	static void ComputeWrap(const mcGroup_t* g) {
		const int n = g->numParticles;
		if ((int)g->wrap.size() == n) return;
		std::vector<unsigned char> wrap((size_t)n, 0);
		if ((int)g->bindPos.size() >= n * 3 && (int)g->partSurf.size() >= n) {
			const int ns = (int)g->surfs.size();
			std::vector<float> zmin((size_t)(ns > 0 ? ns : 1), 1e9f), ang;
			for (int q = 0; q < n; q++) {
				const int sq = g->partSurf[q];
				if (sq >= 0 && sq < ns && g->bindPos[q * 3 + 2] < zmin[sq]) zmin[sq] = g->bindPos[q * 3 + 2];
			}
			std::vector<unsigned char> tube((size_t)(ns > 0 ? ns : 1), 0);
			for (int si = 0; si < ns; si++) tube[si] = zmin[si] < 1e8f && WrapAngle(g, si, zmin[si] + MC_WRAP_BAND, ang) >= MC_WRAP_HEM;
			for (int p = 0; p < n; p++) {
				const int sp = g->partSurf[p];
				if (sp < 0 || sp >= ns || !tube[sp]) continue;
				if (WrapAngle(g, sp, g->bindPos[p * 3 + 2], ang) >= MC_WRAP_ANGLE) wrap[p] = 1;
			}
		}
		g->wrap.swap(wrap);
	}

	// SJE: watchdog against shaking. After every step, for every point the animation holds still: has it moved
	// against the animation, and the other way than in the step before? MC_FIGHT_TRIP such turns running and the
	// triangle test and the layer rule let go of it (FaceCollide / SolveLayerPairs give it no push) until the count
	// has run down again. While the animation moves the point nothing is counted (the solver's small corrections
	// turn round all the time in moving cloth, and shaking only shows where a pose is held), and a point that was
	// let go is given back quickly.
	const float MC_FIGHT_STILL = 0.08f;	// the animation holds a point still: it moves it less than this per step
	const float MC_FIGHT_MOVE = 0.05f;	// a move under this (units per step, against the animation) is no shaking
	const float MC_FIGHT_TRIP = 4.0f;	// turns running that trip it
	const float MC_FIGHT_HOLD = 10.0f;	// the count it is set to then ...
	const float MC_FIGHT_CALM = 0.03f;	// ... what a quiet step takes off it (about 3 s to let the rules back in)
	const float MC_FIGHT_FREE = 0.3f;	// ... and a step in which the animation moves the point (about 0.4 s)
	const unsigned char MC_FIGHT_RAMP = 8;	// SJE r_meshClothEase: steps over which a point that tripped it is let go
	const float MC_EASE_MOVE = 0.1f;	// SJE r_meshClothEase: a point lies at rest if it moves less than this per step against the animation ...
	const unsigned char MC_EASE_REST = 20;	// ... for this many steps running (a third of a second)
	static void FightUpdate(const mcGroup_t* g, mcInstance_t* in) {
		const size_t n = (size_t)g->numParticles;
		const bool fresh = in->fight.size() != n || in->fightRel.size() != n * 3 || in->fightD.size() != n * 3 || in->fightT.size() != n * 3;
		if (fresh || in->eased.size() != n) in->eased.assign(n, 0);	// SJE r_meshClothEase
		if (fresh || in->letgo.size() != n) in->letgo.assign(n, 0);
		if (fresh || in->quiet.size() != n) in->quiet.assign(n, 0);
		if (fresh) {
			in->fight.assign(n, 0.0f);
			in->fightRel.assign(n * 3, 0.0f);
			in->fightD.assign(n * 3, 0.0f);
			in->fightT.assign(n * 3, 0.0f);
		}
		const float eps = MC_FIGHT_MOVE * in->linkScale, stillLim = MC_FIGHT_STILL * in->linkScale;
		for (size_t p = 0; p < n; p++) {
			if (g->freedom[p] <= 0.0f) continue;
			vec3_t rel, d, dT;
			const float* tg = &in->target[p * 3];
			mcSub(&in->p[p * 3], tg, rel);
			float* r0 = &in->fightRel[p * 3], * d0 = &in->fightD[p * 3], * t0 = &in->fightT[p * 3];
			if (fresh) { mcCopy(rel, r0); mcCopy(tg, t0); continue; }
			mcSub(rel, r0, d);
			mcSub(tg, t0, dT);
			float& f = in->fight[p];
			if (in->letgo[p]) in->letgo[p]--;	// SJE r_meshClothEase
			if (mcDot(dT, dT) > stillLim * stillLim) {
				// the pose moves on here
				in->eased[p] = 0;	// SJE r_meshClothEase: in motion the rules act in full
				in->quiet[p] = 0;
				if (f < MC_FIGHT_TRIP) f = 0.0f;
				else { f -= MC_FIGHT_FREE; if (f < MC_FIGHT_TRIP) f = 0.0f; }
			}
			else {
				const float l1 = mcLen(d), l0 = mcLen(d0);
				if (l1 < MC_EASE_MOVE * in->linkScale) { if (in->quiet[p] < 255) in->quiet[p]++; } else in->quiet[p] = 0;	// SJE r_meshClothEase
				if (l1 > eps && l0 > eps && mcDot(d, d0) < -0.5f * l1 * l0) {
					if (f < MC_FIGHT_TRIP) { f += 1.0f; if (f >= MC_FIGHT_TRIP) { f = MC_FIGHT_HOLD; in->letgo[p] = MC_FIGHT_RAMP; } }	// SJE r_meshClothEase: let go over the next steps
					else if (f < MC_FIGHT_HOLD) f = MC_FIGHT_HOLD;
				}
				else if (f > 0.0f) {
					// below the trip the count leaks: a shake that turns in two steps out of three still trips it, the turns
					// of a slow swing (two in ten steps or more) never do
					if (f < MC_FIGHT_TRIP) { f -= 0.5f; if (f < 0.0f) f = 0.0f; }
					else { f -= MC_FIGHT_CALM; if (f < MC_FIGHT_TRIP) { f = 0.0f; in->eased[p] = 1; } }	// SJE r_meshClothEase: given back at rest
				}
			}
			mcCopy(rel, r0);
			mcCopy(d, d0);
			mcCopy(tg, t0);
		}
	}

	const float MC_LAYER_UNDERSHARE = 0.8f;	// SJE: a loose surface is a layer under a worn one if this part of its pairs has it on the inner side
	const int MC_LAYER_UNDERMIN = 8;		// ... of at least this many pairs
	static int triSurfOf(const mcGroup_t* g, int ia, int ib, int ic) {
		const int a = g->partSurf[ia], b = g->partSurf[ib], c = g->partSurf[ic];
		return (a == b || a == c) ? a : (b == c ? b : a);
	}
	static void ComputeLayerPairs(const mcGroup_t* g) {
		if (g->layerBuilt) return;
		g->layerBuilt = true;
		g->layerPairs.clear();
		g->under.assign((size_t)g->numParticles, 0);	// SJE r_meshClothWornAir
		{	// SJE r_meshClothShown: which surfaces have a vertex on each particle (surfaces welded together share particles)
			const int nsU = (int)g->surfs.size();
			g->partUse.assign((size_t)g->numParticles, 0u);
			if (nsU <= 32 && g->vertBase.size() >= (size_t)nsU) {
				for (int si = 0; si < nsU; si++) {
					for (int i = 0; i < g->surfs[si]->numVerts; i++) {
						const size_t vi = (size_t)g->vertBase[si] + (size_t)i;
						const int p = vi < g->vertToParticle.size() ? g->vertToParticle[vi] : -1;
						if (p >= 0 && p < g->numParticles) g->partUse[p] |= 1u << si;
					}
				}
				for (int p = 0; p < g->numParticles; p++) {
					if (!g->partUse[p] && (size_t)p < g->partSurf.size() && g->partSurf[p] >= 0 && g->partSurf[p] < nsU) g->partUse[p] = 1u << g->partSurf[p];
				}
			}
			else g->partUse.clear();
		}
		const int ns = (int)g->surfs.size(), np = g->numParticles, nt = (int)(g->tris.size() / 3);
		if (ns < 2 || nt < 1 || (int)g->bindPos.size() < np * 3) return;
		// surfaces sewn together (a vertex of one welded onto a point of another)
		std::vector<char> sewn((size_t)ns * ns, 0);
		for (int si = 0; si < ns; si++) {
			for (int i = 0; i < g->surfs[si]->numVerts; i++) {
				const int p = g->vertToParticle[g->vertBase[si] + i];
				if (p < 0 || p >= np) continue;
				const int sj = g->partSurf[p];
				if (sj != si && sj >= 0 && sj < ns) sewn[(size_t)si * ns + sj] = sewn[(size_t)sj * ns + si] = 1;
			}
		}
		std::vector<int> triSurf(nt);
		for (int t = 0; t < nt; t++) {
			const int a = g->partSurf[g->tris[t * 3]], b = g->partSurf[g->tris[t * 3 + 1]], c = g->partSurf[g->tris[t * 3 + 2]];
			triSurf[t] = (a == b || a == c) ? a : (b == c ? b : a);
		}
		for (int p = 0; p < np; p++) {
			const int sp = g->partSurf[p];
			const float* x = &g->bindPos[p * 3];
			mcLayerPair_t best[3];
			float bestD[3] = { 1e9f, 1e9f, 1e9f };
			for (int t = 0; t < nt; t++) {
				const int st = triSurf[t];
				if (st == sp || st < 0 || st >= ns || sp < 0 || sp >= ns || sewn[(size_t)sp * ns + st]) continue;
				const int ia = g->tris[t * 3], ib = g->tris[t * 3 + 1], ic = g->tris[t * 3 + 2];
				if (ia == p || ib == p || ic == p || ia == ib || ib == ic || ia == ic) continue;
				if (g->freedom[p] <= 0.0f && g->freedom[ia] <= 0.0f && g->freedom[ib] <= 0.0f && g->freedom[ic] <= 0.0f) continue;
				const float* a = &g->bindPos[ia * 3], * b = &g->bindPos[ib * 3], * c = &g->bindPos[ic * 3];
				float bc[3];
				ClosestPtTri(x, a, b, c, bc);
				vec3_t q, v, e1, e2, n;
				for (int k = 0; k < 3; k++) q[k] = bc[0] * a[k] + bc[1] * b[k] + bc[2] * c[k];
				mcSub(x, q, v);
				const float d = mcLen(v);
				if (d > MC_LAYER_BIND || d < 0.05f) continue;
				mcSub(b, a, e1); mcSub(c, a, e2); mcCross(e1, e2, n);
				if (mcNorm(n) < 1e-6f) continue;
				const float h = mcDot(v, n);
				if (fabsf(h) < 0.6f * d) continue;		// beside the triangle, not over or under it
				int slot = 0;
				for (int k = 1; k < 3; k++) if (bestD[k] > bestD[slot]) slot = k;
				if (d >= bestD[slot]) continue;
				vec3_t e3;
				mcSub(c, b, e3);
				float size = mcLen(e1);
				if (mcLen(e2) > size) size = mcLen(e2);
				if (mcLen(e3) > size) size = mcLen(e3);
				bestD[slot] = d;
				best[slot].p = p; best[slot].a = ia; best[slot].b = ib; best[slot].c = ic;
				best[slot].sgn = h > 0.0f ? 1.0f : -1.0f;
				{
					// Loose cloth is not held UNDER worn cloth (a robe or a skirt that wraps the body - ComputeWrap).
					// Where the bind pose has a point of a cape or a panel on the inner side of a robe's triangle
					// (nearer the character's upright axis), the two are modelled into each other: Kylo's cloak lies
					// within a unit or two of his robe, and 576 of his 1033 pairs were of this kind. The rule then
					// drew the cloak into the robe, towards the body under it: four fifths of all the pushes on the
					// cloak, and a quarter of its stretched links. The pair stays in the list and is skipped.
					const bool wrapOk = (int)g->wrap.size() == np;
					const float ox = a[0] + b[0] + c[0], oy = a[1] + b[1] + c[1];
					const float ol = sqrtf(ox * ox + oy * oy);
					best[slot].idle = wrapOk && !g->wrap[p] && g->wrap[ia] + g->wrap[ib] + g->wrap[ic] >= 2 && ol > 1e-3f
						&& best[slot].sgn * (n[0] * ox + n[1] * oy) < 0.0f;
				}
				{
					vec3_t b1, b2, bn;
					mcSub(&g->bindPos[ib * 3], &g->bindPos[ia * 3], b1); mcSub(&g->bindPos[ic * 3], &g->bindPos[ia * 3], b2); mcCross(b1, b2, bn);
					best[slot].bindArea = mcLen(bn);
				}
				best[slot].reach = d + size + 6.0f;
				best[slot].lat = 0.6f * size;
				const float want = 0.3f * (g->thick[p] + g->thick[ia]);	// about 0.6 x the cloth thickness between two layers
				best[slot].gap = want < 0.8f * fabsf(h) ? want : 0.8f * fabsf(h);
			}
			for (int k = 0; k < 3; k++) if (bestD[k] < 1e8f) g->layerPairs.push_back(best[k]);
		}
		// ... unless the loose surface is a layer UNDER the worn one: a tunic strip or a panel that the model has under
		// the robe everywhere they meet (Obi-Wan's tunic strip: 279 of 279 pairs on the inner side, Maul's side panel
		// 128 of 128), not a cloak that dips into a robe here and there (Kylo 69 of 368, Ventress 31 of 163, Revan 3
		// of 280). Such a layer stays under the robe: let go, the strip swung out through the robe's side in a turn.
		if ((int)g->wrap.size() == np) {
			std::vector<int> all((size_t)ns * ns, 0), inner((size_t)ns * ns, 0);
			for (const mcLayerPair_t& lp : g->layerPairs) {
				if (g->wrap[lp.p] || g->wrap[lp.a] + g->wrap[lp.b] + g->wrap[lp.c] < 2) continue;
				const size_t i = (size_t)g->partSurf[lp.p] * ns + triSurfOf(g, lp.a, lp.b, lp.c);
				all[i]++;
				if (lp.idle) inner[i]++;
			}
			for (mcLayerPair_t& lp : g->layerPairs) {
				if (!lp.idle) continue;
				const size_t i = (size_t)g->partSurf[lp.p] * ns + triSurfOf(g, lp.a, lp.b, lp.c);
				// (under: such a point is out of the travel wind with the robe over it, r_meshClothWornAir)
				if (all[i] >= MC_LAYER_UNDERMIN && (float)inner[i] >= MC_LAYER_UNDERSHARE * (float)all[i]) { lp.idle = false; g->under[lp.p] = 1; }
			}
		}
	}

	static void SolveLayerPairs(const mcGroup_t* g, mcInstance_t* in, const mcParams_t* prm) {
		// A heap pose: the animation puts a good part of the free cloth under the floor (crouching, kneeling, lying:
		// the skinned cape and robe go into the ground, the simulated ones pile up on it). Standing or in a stride
		// only the last hem points do. In a heap the rule pushed one layer under the other, into the floor, the
		// floor pushed it back, and cape and robe shook for as long as the crouch lasted.
		float pile = 0.0f;
		if (prm->floor) {
			int under = 0, nfree = 0;
			for (int p = 0; p < g->numParticles; p++) {
				if (g->freedom[p] <= 0.0f) continue;
				nfree++;
				vec3_t df;
				mcSub(&in->target[p * 3], prm->floorPt, df);
				if (mcDot(df, prm->floorN) < -MC_LAYER_UNDER) under++;
			}
			if (nfree > 0) pile = ((float)under / (float)nfree - MC_LAYER_PILE) / (0.5f * MC_LAYER_PILE);
			if (pile < 0.0f) pile = 0.0f;
			if (pile > 1.0f) pile = 1.0f;
		}
		const bool wrapOk = (int)g->wrap.size() == g->numParticles;
		// SJE r_meshClothShown: a cloth surface this character has switched off holds nothing and is held by nothing.
		// The model's cloth surfaces are simulated together whatever the skin shows (one model, several outfits: a
		// long cloak OR a shoulder cape and a robe), and the hidden ones kept the drawn ones on "their side" of cloth
		// nobody sees.
		// nobody sees. A point counts as hidden when no switched-on surface has a vertex on it (surfaces welded
		// together share points: a robed figure's visible tunic strips are the front of its hidden whole tunic), a
		// triangle when all three corners are.
		const unsigned offM = prm->shown && (int)g->partUse.size() == g->numParticles ? in->offMask : 0u;
		for (const mcLayerPair_t& lp : g->layerPairs) {
			if (offM && (!(g->partUse[lp.p] & ~offM) || (!(g->partUse[lp.a] & ~offM) && !(g->partUse[lp.b] & ~offM) && !(g->partUse[lp.c] & ~offM)))) continue;
			// Worn cloth (it wraps round the body: a robe, a skirt - ComputeWrap) is held by the body all round: it
			// does not give way to another layer. So a pair whose point is worn is left alone - its mirror, the
			// loose layer's points against this layer's triangles, keeps the order. (Against a loose triangle a
			// worn point has no side to be kept on either: a cape swings, folds and turns over.)
			const bool wornP = wrapOk && g->wrap[lp.p] != 0, wornT = wrapOk && g->wrap[lp.a] + g->wrap[lp.b] + g->wrap[lp.c] >= 2;
			if (wornP || lp.idle) continue;
			float* xp = &in->p[lp.p * 3], * xa = &in->p[lp.a * 3], * xb = &in->p[lp.b * 3], * xc = &in->p[lp.c * 3];
			float fade = 1.0f;
			// in a heap pose the cloth lies piled on the ground, one layer on the other: no order is kept
			if (pile > 0.0f) {
				fade = 1.0f - pile;
				if (fade <= 0.0f) continue;
			}
			vec3_t d0;
			mcSub(xp, xa, d0);
			const float reach = lp.reach * in->linkScale;
			if (mcDot(d0, d0) > reach * reach) continue;
			vec3_t e1, e2, n;
			mcSub(xb, xa, e1); mcSub(xc, xa, e2); mcCross(e1, e2, n);
			if (mcNorm(n) < 1e-6f) continue;
			// On its own side of the triangle's plane by clearly more than the gap: nothing to keep apart. (The distance
			// 'h' below is this same plane distance; found here first, nearly every pair ends here, before the squash
			// test and the search for the closest point.)
			if (lp.gap * in->linkScale - mcDot(d0, n) * lp.sgn < -0.05f) continue;
			{
				// Nor does cloth the pose itself crushes: the pair fades out as the ANIMATED triangle loses its area
				// (crouching, the skinned cape and robe collapse between the legs and the ground: there is no order
				// left to keep, and the rule shook the heap). Not the simulated triangle: a robe bunches up by itself
				// in a stride, and there the rule is wanted.
				vec3_t t1, t2, tn;
				mcSub(&in->target[lp.b * 3], &in->target[lp.a * 3], t1); mcSub(&in->target[lp.c * 3], &in->target[lp.a * 3], t2); mcCross(t1, t2, tn);
				if (wornT && mcDot(n, tn) <= 0.0f) continue;	// a worn triangle turned over against the animation (crumpled) has no side
				const float bind2 = lp.bindArea * in->linkScale * in->linkScale;
				if (bind2 > 1e-6f) {
					const float squash = (mcLen(tn) / bind2 - MC_LAYER_SQUASH) / MC_LAYER_SQUASH;
					if (squash <= 0.0f) continue;
					if (squash < fade) fade = squash;
				}
			}
			float bc[3];
			ClosestPtTri(xp, xa, xb, xc, bc);
			vec3_t q, v;
			for (int k = 0; k < 3; k++) q[k] = bc[0] * xa[k] + bc[1] * xb[k] + bc[2] * xc[k];
			mcSub(xp, q, v);
			const float h = mcDot(v, n), lat2 = mcDot(v, v) - h * h, lat = lp.lat * in->linkScale;
			if (lat2 > lat * lat) continue;				// off to the side of this triangle
			float C = lp.gap * in->linkScale - h * lp.sgn;
			if (C <= 0.0f) continue;
			if (C > MC_LAYER_STEP) C = MC_LAYER_STEP;
			C *= fade;
			// each side moves by how free it is: the freer cloth gives way, and cloth near the attached edge stays
			// with the animation (two layers that are both nearly attached there are left as modelled)
			const float fp = g->freedom[lp.p], fa = g->freedom[lp.a], fb = g->freedom[lp.b], fc = g->freedom[lp.c];
			// ... and a point that shakes is let go (FightUpdate)
			const bool watch = in->fight.size() == (size_t)g->numParticles;
			// SJE r_meshClothEase: ... over MC_FIGHT_RAMP steps (its move is scaled down step by step, 'sp' / 'sw'), and
			// once the watchdog has given it back while the pose holds it still, the rule moves it gently (as the
			// body's triangle test does: a strip under a robe that the pose half holds was pushed 9 units a step by the
			// rule and pulled back as far by the pose; given back at full strength it jumped 2 units every few seconds)
			const bool ramp = prm->ease && in->letgo.size() == (size_t)g->numParticles, soft = prm->ease && in->eased.size() == (size_t)g->numParticles;
			const int lid[4] = { lp.p, lp.a, lp.b, lp.c };
			float gt[4], sl[4];
			for (int k = 0; k < 4; k++) {
				gt[k] = 1.0f; sl[k] = 1.0f;
				if (watch && in->fight[lid[k]] >= MC_FIGHT_TRIP) {
					const float lg = ramp ? in->letgo[lid[k]] * (1.0f / MC_FIGHT_RAMP) : 0.0f;
					if (lg > 0.0f) sl[k] = lg; else gt[k] = 0.0f;
				}
			}
			const float gp = gt[0], ga = gt[1], gb = gt[2], gc = gt[3];
			const float gentle = MC_EASE_STEP * in->linkScale;
			// ... and a worn triangle does not give way: the loose point moves alone (a robe pushed in under a cape,
			// or out over the tunic under it, stretched and shook)
			const float ut = wornT ? 0.0f : 1.0f;
			const float wp = gp * (fp <= 0.0f ? 0.0f : (fp < 1.0f ? fp : 1.0f));
			const float w[3] = { ut * ga * (fa <= 0.0f ? 0.0f : (fa < 1.0f ? fa : 1.0f)) * bc[0], ut * gb * (fb <= 0.0f ? 0.0f : (fb < 1.0f ? fb : 1.0f)) * bc[1],
				ut * gc * (fc <= 0.0f ? 0.0f : (fc < 1.0f ? fc : 1.0f)) * bc[2] };
			const float den = wp + w[0] * bc[0] + w[1] * bc[1] + w[2] * bc[2];
			if (den < 1e-4f) continue;
			if (den < 0.5f) C *= den * 2.0f;
			const float lam = C / den * lp.sgn;
			float* xs[3] = { xa, xb, xc };
			const int id[3] = { lp.a, lp.b, lp.c };
			if (wp > 0.0f) {
				float mvp = lam * wp * sl[0];
				if (soft && in->eased[lp.p]) { if (mvp > gentle) mvp = gentle; else if (mvp < -gentle) mvp = -gentle; }
				mcMA(xp, mvp, n, xp); mcMA(&in->prev[lp.p * 3], mvp, n, &in->prev[lp.p * 3]);
			}
			for (int k = 0; k < 3; k++) {
				if (w[k] <= 0.0f) continue;
				float mvt = -lam * w[k] * sl[k + 1];
				if (soft && in->eased[id[k]]) { if (mvt > gentle) mvt = gentle; else if (mvt < -gentle) mvt = -gentle; }
				mcMA(xs[k], mvt, n, xs[k]);
				mcMA(&in->prev[id[k] * 3], mvt, n, &in->prev[id[k] * 3]);
			}
		}
	}

	// SJE: the body shapes each cloth point can reach in this step. The point test ran every point against every
	// shape in every substep (Vader: 209 points x about 40 shapes x 6); most shapes are nowhere near most points
	// (a hem and the head, the left arm and the right side of a cape). Here, once per step: a shape counts for a
	// point if the two can come within the shape's radius, the cloth's margin and the resting distance of each
	// other, given how far both can travel in the step - the point by its speed now over the substeps, by what
	// the animation moves its own place and its anchor's (a pose change pulls the cloth along), by a pending
	// kick, and MC_PT_SLACK for what the other rules push it; the shape by the travel of its ends. The substeps
	// then test only those shapes, in the same order. A point that moves further than that allowance after all
	// (the links throw single points of a robe 15-20 units inside one step in some animations) is tested against
	// all shapes in that substep, so the result is always the same as before.
	const float MC_PT_SLACK = 3.0f;
	static void BuildPointCaps(const mcGroup_t* g, mcInstance_t* in, const mcCapsule_t* caps, const mcCapsule_t* capsEnd, int ncaps, int sub,
		const std::vector<float>& tgtFrom, const std::vector<float>& tgtTo) {
		const int np = g->numParticles;
		in->ptCapStart.resize((size_t)np + 1);
		in->ptCaps.clear();
		in->ptFrom.assign(in->p.begin(), in->p.end());
		in->ptAllow.assign((size_t)np, 0.0f);
		if (ncaps > MC_NEAR_CAPS) ncaps = MC_NEAR_CAPS;
		float move[MC_NEAR_CAPS];
		for (int c = 0; c < ncaps; c++) {
			float m = 0.0f;
			if (capsEnd) {
				vec3_t d;
				mcSub(capsEnd[c].a, caps[c].a, d); m = mcLen(d);
				mcSub(capsEnd[c].b, caps[c].b, d); const float mb = mcLen(d);
				if (mb > m) m = mb;
			}
			move[c] = (capsEnd && capsEnd[c].r > caps[c].r ? capsEnd[c].r : caps[c].r) + m;
		}
		const bool kick = in->impulse.size() == in->p.size();
		for (int p = 0; p < np; p++) {
			in->ptCapStart[p] = (int)in->ptCaps.size();
			if (g->freedom[p] <= 0.0f) continue;
			const float* x = &in->p[p * 3];
			vec3_t d;
			mcSub(x, &in->prev[p * 3], d);
			float allow = mcLen(d) * sub + MC_PT_SLACK;
			mcSub(&tgtTo[p * 3], &tgtFrom[p * 3], d);
			allow += mcLen(d);
			const int an = g->lraAnchor[p];
			if (an >= 0) { mcSub(&tgtTo[an * 3], &tgtFrom[an * 3], d); allow += mcLen(d); }
			if (kick) allow += MC_DT * mcLen(&in->impulse[p * 3]);
			in->ptAllow[p] = allow;
			// (0.26: the point test's resting distance of 0.25, and rounding)
			const float travel = (MC_MARGIN > 0.5f * g->thick[p] ? MC_MARGIN : 0.5f * g->thick[p]) + 0.26f + allow;
			for (int c = 0; c < ncaps; c++) {
				vec3_t q;
				ClosestOnAxis(caps[c], x, q);
				mcSub(x, q, d);
				const float reach = move[c] + travel;
				if (mcDot(d, d) <= reach * reach) in->ptCaps.push_back((unsigned char)c);
			}
		}
		in->ptCapStart[np] = (int)in->ptCaps.size();
	}

	static void BuildFacePairs(const mcGroup_t* g, mcInstance_t* in, const mcCapsule_t* caps, const mcCapsule_t* capsEnd, int ncaps, int sub) {
		std::vector<mcFacePair_t>& out = in->facePairs;
		out.clear();
		in->faceStart.assign(in->p.begin(), in->p.end());
		in->faceCapsFrom = caps;
		const int nt = (int)(g->tris.size() / 3);
		if (nt <= 0 || ncaps <= 0) return;
		if (ncaps > MC_NEAR_CAPS) ncaps = MC_NEAR_CAPS;
		float move[MC_NEAR_CAPS], lo[MC_NEAR_CAPS][3], hi[MC_NEAR_CAPS][3];
		for (int c = 0; c < ncaps; c++) {
			float m = 0.0f;
			if (capsEnd) {
				vec3_t d;
				mcSub(capsEnd[c].a, caps[c].a, d); m = mcLen(d);
				mcSub(capsEnd[c].b, caps[c].b, d); const float mb = mcLen(d);
				if (mb > m) m = mb;
			}
			move[c] = m;
			for (int k = 0; k < 3; k++) {
				const float a = caps[c].a[k], b = caps[c].b[k], r = caps[c].r + m;
				lo[c][k] = (a < b ? a : b) - r;
				hi[c][k] = (a > b ? a : b) + r;
			}
		}
		for (int ti = 0; ti < nt; ti++) {
			const int i0 = g->tris[ti * 3], i1 = g->tris[ti * 3 + 1], i2 = g->tris[ti * 3 + 2];
			if (i0 == i1 || i1 == i2 || i0 == i2) continue;
			if (g->freedom[i0] <= 0.0f && g->freedom[i1] <= 0.0f && g->freedom[i2] <= 0.0f) continue;
			const float* x[3] = { &in->p[i0 * 3], &in->p[i1 * 3], &in->p[i2 * 3] };
			const int id[3] = { i0, i1, i2 };
			vec3_t ctr = { 0, 0, 0 };
			for (int k = 0; k < 3; k++) mcAdd(ctr, x[k], ctr);
			mcScale(ctr, 1.0f / 3.0f, ctr);
			float R = 0.0f, speed = 0.0f, margin = MC_MARGIN;
			for (int k = 0; k < 3; k++) {
				vec3_t d;
				mcSub(x[k], ctr, d);
				const float r = mcLen(d);
				if (r > R) R = r;
				mcSub(x[k], &in->prev[id[k] * 3], d);
				const float v = mcLen(d);
				if (v > speed) speed = v;
				const float hm = 0.5f * g->thick[id[k]];
				if (hm > margin) margin = hm;
			}
			// how far the triangle can get in this step: its speed now (per substep) over the substeps, and some
			const float travel = margin + speed * sub + 1.0f;
			vec3_t e1, e2, pn;
			mcSub(x[1], x[0], e1); mcSub(x[2], x[0], e2); mcCross(e1, e2, pn);
			const float pn2 = mcDot(pn, pn);
			for (int c = 0; c < ncaps; c++) {
				// a triangle small against the shape needs no test of its own: the shape can't pass between its
				// corners without one of them inside it (R <= minD), and with the corners held at minD its middle
				// sags into the shape by at most R^2 / (2 minD), within the cloth's own margin (dense robes)
				const float minDc = caps[c].r + margin;
				if (R <= minDc && R * R <= 2.0f * minDc * margin) continue;
				bool away = false;
				for (int k = 0; k < 3 && !away; k++) away = ctr[k] + R + travel < lo[c][k] || ctr[k] - R - travel > hi[c][k];
				if (away) continue;
				// how close the two can get in this step (the shape also moves)
				const float gap = caps[c].r + travel + move[c];
				vec3_t q, d;
				ClosestOnAxis(caps[c], ctr, q);
				mcSub(ctr, q, d);
				if (mcLen(d) > R + gap) continue;
				// the whole axis on one side of the triangle's plane, out of reach
				vec3_t ra, rb;
				mcSub(caps[c].a, x[0], ra); mcSub(caps[c].b, x[0], rb);
				const float da = mcDot(ra, pn), db = mcDot(rb, pn), lim2 = gap * gap * pn2;
				if (da * db > 0.0f && da * da >= lim2 && db * db >= lim2) continue;
				mcFacePair_t fp;
				fp.tri = ti * 3;
				fp.cap = c;
				fp.have = 0;
				out.push_back(fp);
			}
		}
		// arms, fists and hilts last, as the edge test ran after the point test: what pushes the cloth last in
		// a substep wins, and a limb has to push a cape that the body shapes pushed out back off itself
		std::stable_partition(out.begin(), out.end(), [caps](const mcFacePair_t& fp) { return caps[fp.cap].hard != 1 && caps[fp.cap].hard != 2 && caps[fp.cap].hard != 4; });
	}

	static void FaceCollide(const mcGroup_t* g, mcInstance_t* in, const mcCapsule_t* caps, int ncaps, int overArms, int rest, int ease) {
		if (in->facePairs.empty()) return;
		const int nt = (int)(g->tris.size() / 3);
		std::vector<float>& sph = in->triSphere;
		sph.assign((size_t)nt * 5, -1.0f);		// radius < 0: not worked out in this substep yet
		for (mcFacePair_t& fp : in->facePairs) {
			if (fp.cap >= ncaps) continue;
			const mcCapsule_t& cap = caps[fp.cap];
			const int id[3] = { g->tris[fp.tri], g->tris[fp.tri + 1], g->tris[fp.tri + 2] };
			// arms, fists and hilts don't push sleeves, which wrap them (as in the point and edge tests)
			const bool tool = cap.hard == 4;	// a hilt or a blade
			const bool limb = cap.hard == 1 || cap.hard == 2 || tool;
			const bool hand = cap.hard == 1 || tool;	// arms / hands and what they hold
			const bool watch = in->fight.size() == (size_t)g->numParticles;
			float w[3], sc[3], margin = MC_MARGIN;
			for (int k = 0; k < 3; k++) {
				// arms, hands and held things move cloth that is more than half free, as in the point test: nearer the
				// attached edge the animation holds the cloth, and it has the hand there on purpose (a hand with a
				// hilt on the hip lies in the top of a robe). Not the shoulders: a cape lies over them right at its
				// attached edge.
				const float fr = g->freedom[id[k]];
				w[k] = fr > 0.0f && (!limb || (g->hardCol[id[k]] && (!hand || fr > 0.5f))) ? 1.0f : 0.0f;
				// It shakes here: the body's triangle test lets go (FightUpdate). Not arms, hands, shoulders and held
				// things: cloth that rests on them always works a little, and let go there the hand came through
				// (Dooku's cape over the hands behind his back).
				// SJE r_meshClothEase: ... over MC_FIGHT_RAMP steps. Let go at once, a point the test had held against
				// the links jumped 2 units to where they pull it.
				sc[k] = 1.0f;
				if (watch && !limb && in->fight[id[k]] >= MC_FIGHT_TRIP) {
					const float lg = ease && in->letgo.size() == (size_t)g->numParticles ? in->letgo[id[k]] * (1.0f / MC_FIGHT_RAMP) : 0.0f;
					if (lg > 0.0f) sc[k] = lg; else w[k] = 0.0f;
				}
				const float hm = 0.5f * g->thick[id[k]];
				if (hm > margin) margin = hm;
			}
			if (w[0] + w[1] + w[2] == 0.0f) continue;
			float* x[3] = { &in->p[id[0] * 3], &in->p[id[1] * 3], &in->p[id[2] * 3] };
			// held things are kept a little further off: cloth pressed against a hilt or a blade by the travel wind
			// (a cape edge swinging round the saber hand, walking backwards) sat up to 0.6 units in their surface
			const float minD = cap.r + margin + (tool ? MC_FACE_TOOL : 0.0f);
			float* S = &sph[(size_t)(fp.tri / 3) * 5];
			if (S[3] < 0.0f) {
				S[0] = S[1] = S[2] = 0.0f;
				for (int k = 0; k < 3; k++) mcAdd(S, x[k], S);
				mcScale(S, 1.0f / 3.0f, S);
				float R = 0.0f, dev = 0.0f;
				for (int k = 0; k < 3; k++) {
					vec3_t d;
					mcSub(x[k], S, d);
					const float r = mcLen(d);
					if (r > R) R = r;
					mcSub(x[k], &in->target[id[k] * 3], d);
					const float e = mcDot(d, d);
					if (e > dev) dev = e;
				}
				S[3] = R;
				S[4] = dev;
			}
			// every corner within MC_FACE_NEAR of where the animation has it: the triangle can't be more than
			// that deeper in the body than the animated one, which is all this test would hold it to (cloth at
			// rest, and the parts near the attached edge). Not for limbs: a hand through the animated cape is
			// through cloth lying exactly where the animation has it.
			if (!limb && S[4] < MC_FACE_NEAR * MC_FACE_NEAR) continue;
			{
				vec3_t q, d;
				ClosestOnAxis(cap, S, q);
				mcSub(S, q, d);
				if (mcDot(d, d) > (S[3] + minD) * (S[3] + minD)) continue;
			}
			{
				// the whole axis on one side of the triangle's plane, further off than the radius: no contact
				vec3_t e1, e2, pn, ra, rb;
				mcSub(x[1], x[0], e1); mcSub(x[2], x[0], e2); mcCross(e1, e2, pn);
				mcSub(cap.a, x[0], ra); mcSub(cap.b, x[0], rb);
				const float da = mcDot(ra, pn), db = mcDot(rb, pn), lim2 = minD * minD * mcDot(pn, pn);
				if (da * db > 0.0f && da * da >= lim2 && db * db >= lim2) continue;
			}
			float bc[3], t;
			const float dist = ClosestSegTri(x[0], x[1], x[2], cap.a, cap.b, bc, t);
			if (dist >= minD) continue;		// clear of the shape (on whichever side)
			vec3_t cp, ca, ab, nv;
			for (int k = 0; k < 3; k++) cp[k] = bc[0] * x[0][k] + bc[1] * x[1][k] + bc[2] * x[2][k];
			mcSub(cap.b, cap.a, ab);
			mcMA(cap.a, t, ab, ca);
			mcSub(cp, ca, nv);
			// the animated cloth at the same spot of the triangle, and its distance from the shape
			vec3_t tp, tq, nt3;
			for (int k = 0; k < 3; k++) tp[k] = bc[0] * in->target[id[0] * 3 + k] + bc[1] * in->target[id[1] * 3 + k] + bc[2] * in->target[id[2] * 3 + k];
			ClosestOnAxis(cap, tp, tq);
			mcSub(tp, tq, nt3);
			const float dt = mcLen(nt3);
			// how far out it is held, and the side of the shape it belongs on
			float lim;
			const float* ref = nullptr;
			vec3_t refv;
			// An arm, a hand or a held thing that lies ON cloth worn over the body - the cloth is between it and the
			// body - is how the pose was made (arms hanging against a robe, a hand with a hilt on the hip): that
			// cloth stays where the animation has it. Pushed clear it bulged out over the arm, 5-8 units, stretched
			// and turned over. A limb pushes cloth away from the body only: from between the body and the cloth
			// (hands behind the back under a cape), and cloth that hangs free of the body.
			// Only near the cloth's attached edge (MC_FACE_SLACK of cloth length): there it cannot give. A cape down
			// the back also lies close to the body with the hands behind it further out, but it hangs far from its
			// edge and has the slack to lie over them.
			// Cloth that wraps round the body (a robe, a skirt: ComputeWrap) is worn wherever the animation has the
			// arm, hand or held thing in it, whichever side it is on: a tube cannot lie over a hilt on the belt or a
			// hand against it without stretching, and the pose shows it as the model was made. (Robes stretched 3-4 x
			// as much as before, half of it from hilts and hands pushing them 'clear'.) Cloth that lies skin-tight
			// on the body (a panel) is worn at any distance from its edge when the limb lies on it from outside.
			// SJE r_meshClothRest: does the pose hold this triangle still (its corners' animated places since the last step)?
			bool poseStill = false;
			if (rest) {
				poseStill = in->fightT.size() == in->p.size();
				const float sl = MC_FIGHT_STILL * in->linkScale;
				for (int k = 0; k < 3 && poseStill; k++) {
					vec3_t dT;
					mcSub(&in->target[id[k] * 3], &in->fightT[id[k] * 3], dT);
					if (mcDot(dT, dT) > sl * sl) poseStill = false;
				}
			}
			bool rests = false;
			const float fromEdge = bc[0] * g->lraLen[id[0]] + bc[1] * g->lraLen[id[1]] + bc[2] * g->lraLen[id[2]];
			const bool tube = (int)g->wrap.size() == g->numParticles && g->wrap[id[0]] + g->wrap[id[1]] + g->wrap[id[2]] >= 2;
			// SJE: where the two were when the step began (a limb's pairs only)
			if (limb && !fp.have) {
				fp.have = 1;
				fp.d0 = 0.0f;
				fp.n0[0] = fp.n0[1] = fp.n0[2] = 0.0f;
				if (in->faceCapsFrom && in->faceStart.size() == in->p.size()) {
					const mcCapsule_t& c0 = in->faceCapsFrom[fp.cap];
					const float* s[3] = { &in->faceStart[id[0] * 3], &in->faceStart[id[1] * 3], &in->faceStart[id[2] * 3] };
					float b0[3], t0;
					fp.d0 = ClosestSegTri(s[0], s[1], s[2], c0.a, c0.b, b0, t0);
					if (fp.d0 > 1e-4f) {
						vec3_t sp, sa, sab;
						for (int k = 0; k < 3; k++) sp[k] = b0[0] * s[0][k] + b0[1] * s[1][k] + b0[2] * s[2][k];
						mcSub(c0.b, c0.a, sab);
						mcMA(c0.a, t0, sab, sa);
						mcSub(sp, sa, fp.n0);
						mcScale(fp.n0, 1.0f / fp.d0, fp.n0);
					}
				}
			}
			// SJE: Loose cloth that hangs from above the arms (a cape, a cloak: attached near shoulder height or
			// above - 'high', from the bind pose) lies over an arm and a hand by nature: the arm is rooted under it.
			// (No limit near its attached edge is needed: a limb only moves corners that are more than half free.)
			// The pose knows nothing of the cloth: with a hand behind the
			// back under a cape, the skinned cape bends with the torso as its wearer looks down and passes through
			// the hand to its inner side (3-7 units). The rules below then took the animated cloth's side (cloth
			// 'modelled round the limb', or the limb 'lying on worn cloth': the skinned cape is skin-tight on the
			// back there, the hand further out) and pushed the cape through the hand: it lay under it, the hand on
			// top for all to see. For such cloth against an arm or a hand:
			// - where it was on its own outer side of the limb when the step began (its animated triangle's normal,
			//   turned away from the character's upright axis), it is kept on that side ('over'), clear of it or not:
			//   there it does not rest under the limb and is not taken as modelled round it;
			// - where it is CAUGHT between the limb and the body - the hand arrived on top of it: the stance was taken
			//   while its wearer already looked down - it is brought out over the limb ('bring': the simulated cloth
			//   here within MC_FACE_WORN of a body shape, the limb further out);
			// - elsewhere the rules below apply as for any cloth (a cape's edge swinging round the front of an arm is
			//   not pulled through the arm; always putting such cloth outside the arm cost when getting up from a roll).
			// Not held things (a blade reaches out past a cape), not cloth that wraps the body (a tube cannot lie
			// over a hand), and not cloth attached below the arms (a skirt, a panel on the belt: a hand lies ON that).
			bool over = false, bring = false;
			vec3_t outB = { 0.0f, 0.0f, 0.0f }, nOut = { 0.0f, 0.0f, 0.0f };
			if (overArms && cap.hard == 1 && !tube && g->plateBuilt && (int)g->high.size() == g->numParticles && g->high[id[0]] + g->high[id[1]] + g->high[id[2]] >= 2) {
				vec3_t e1, e2, od;
				mcSub(&in->target[id[1] * 3], &in->target[id[0] * 3], e1);
				mcSub(&in->target[id[2] * 3], &in->target[id[0] * 3], e2);
				mcCross(e1, e2, nOut);
				if (mcNorm(nOut) > 1e-6f) {
					mcSub(tp, in->lastOrigin, od);
					mcMA(od, -mcDot(od, in->up), in->up, od);
					if (mcDot(nOut, od) < 0.0f) mcScale(nOut, -1.0f, nOut);
					over = fp.d0 > 1e-4f && mcDot(fp.n0, nOut) > MC_FACE_OUTSIDE;
				}
				// (only where the cloth really hangs down over the limb: its anchor well above the limb right now. With
				// the body level or leaning far forward - a dash with the arms swept back over the cape, getting up from
				// a roll - the cloth lies on the body beside the arm, and bringing it over tore at it.)
				float above = -1e9f;
				for (int k = 0; k < 3; k++) {
					const int an = g->lraAnchor[id[k]];
					if (an < 0) continue;
					vec3_t da;
					mcSub(&in->p[an * 3], ca, da);
					const float ha = mcDot(da, in->up);
					if (ha > above) above = ha;
				}
				if (!over && above >= MC_FACE_ABOVE * in->linkScale) {
					for (int c = 0; c < ncaps && !bring; c++) {
						if (caps[c].hard != 0 && caps[c].hard != 3) continue;
						vec3_t qb, db;
						ClosestOnAxis(caps[c], cp, qb);
						mcSub(cp, qb, db);
						const float clothOut = mcLen(db) - caps[c].r;	// the simulated cloth over this body shape's surface
						if (clothOut > MC_FACE_WORN) continue;
						ClosestOnAxis(caps[c], ca, qb);
						mcSub(ca, qb, db);
						const float ll = mcLen(db);
						if (!(ll > 1e-3f && ll - caps[c].r > clothOut + MC_FACE_OVER)) continue;
						// ... and only once the limb has come to rest against that shape (a hand put behind the back
						// does; an arm in a swing does not, and cloth pulled over it was thrown off again: it shook)
						if (in->faceCapsFrom) {
							const mcCapsule_t& l0 = in->faceCapsFrom[fp.cap], & b0 = in->faceCapsFrom[c];
							vec3_t rel;
							for (int k = 0; k < 3; k++) rel[k] = 0.5f * ((cap.a[k] + cap.b[k]) - (caps[c].a[k] + caps[c].b[k]) - (l0.a[k] + l0.b[k]) + (b0.a[k] + b0.b[k]));
							if (mcDot(rel, rel) > MC_FACE_REST * MC_FACE_REST) continue;
						}
						bring = true;
						mcScale(db, 1.0f / ll, outB);
					}
				}
			}
			if (hand && dt < minD && tube) rests = true;
			else if (hand && dt < minD && !over && !bring) {
				for (int c = 0; c < ncaps && !rests; c++) {
					if (caps[c].hard != 0 && caps[c].hard != 3) continue;
					vec3_t qb, db;
					ClosestOnAxis(caps[c], tp, qb);
					mcSub(tp, qb, db);
					const float clothOut = mcLen(db) - caps[c].r;	// the animated cloth over this body shape's surface
					if (clothOut > MC_FACE_WORN) continue;
					if (fromEdge >= MC_FACE_SLACK && clothOut > MC_FACE_TIGHT) continue;
					ClosestOnAxis(caps[c], tq, qb);
					mcSub(tq, qb, db);
					if (mcLen(db) - caps[c].r > clothOut + MC_FACE_OVER) rests = true;	// the limb is further out than the cloth
				}
			}
			if (limb && !rests && (tool || over || bring || dt >= minD || dt < 0.5f * cap.r)) {
				// Arms, fists, hilts and blades. Where the animated cloth is clear of the limb, or the limb goes
				// right through it (hands behind the back in a cape, a blade through it: the animation knows
				// nothing of the cloth there), the cloth is kept clear of the limb, on the side it was on when
				// the step began - a limb pushes a cape from whichever side it comes. If they already overlapped
				// then: the animated cloth's side where that is clear, else outwards - cloth worn over the body
				// lies over a hand that is in it.
				lim = minD;
				// SJE r_meshClothRest: 'over' says on which side of the arm the cloth lies, not how far off it. Where the pose
				// itself drapes the cloth over the arm on that side, nearer than the clearance, and the animation holds a
				// corner of the triangle (one the arm cannot move: attached, or mostly held by the pose - a cape's collar on
				// the upper arm), holding it clear fought the pose: the one free corner took the whole push, a unit out, and
				// was pulled back in, every other substep, for as long as the pose was held (one point of Dooku's collar,
				// half a unit to and fro in each step). There it is held no further off than the pose has it, but never
				// inside the arm (0.9 of its radius: the pose's own drape is skin-tight there). Only while the pose holds
				// the triangle still. Triangles the arm can move whole keep the full clearance (a cape over a hand behind
				// the back, and the cape beside a forearm when its wearer looks down).
				if (rest && over && dt < minD && dt > 1e-4f && mcDot(nt3, nOut) > MC_FACE_OUTSIDE * dt
					&& (w[0] == 0.0f || w[1] == 0.0f || w[2] == 0.0f) && poseStill) lim = dt > 0.9f * cap.r ? dt : 0.9f * cap.r;
				if (bring) { mcCopy(outB, refv); ref = refv; }
				else if (fp.d0 >= 0.8f * minD) ref = fp.n0;
				else if (over) { mcCopy(nOut, refv); ref = refv; }
				else if (dt >= minD) { mcScale(nt3, 1.0f / dt, refv); ref = refv; }
				else {
					// outwards: the animated triangle's normal, turned away from the spine (the line through
					// the character's origin along its up direction)
					vec3_t e1, e2, od;
					mcSub(&in->target[id[1] * 3], &in->target[id[0] * 3], e1);
					mcSub(&in->target[id[2] * 3], &in->target[id[0] * 3], e2);
					mcCross(e1, e2, refv);
					if (mcNorm(refv) > 1e-6f) {
						mcSub(tp, in->lastOrigin, od);
						mcMA(od, -mcDot(od, in->up), in->up, od);
						if (mcDot(refv, od) < 0.0f) mcScale(refv, -1.0f, refv);
						ref = refv;
					}
				}
			}
			else {
				// The body, and limbs the cloth is modelled round (sleeves): clear of the shape where the
				// animated cloth is clear of it, otherwise no deeper than the animated cloth (the model's own
				// drape), on the animated cloth's side where that is clear enough to tell
				lim = dt < minD ? dt : minD;
				if (dt >= 0.6f * minD) { mcScale(nt3, 1.0f / dt, refv); ref = refv; }
			}
			if (lim < 0.25f) continue;
			const float side = ref ? mcDot(nv, ref) : 0.0f;
			float depth;
			vec3_t dir;
			bool through = ref && (dist < 1e-4f || side < -0.3f * dist);
			// SJE r_meshClothRest: 'gone through' is a triangle found on the far side of a shape from where the animation
			// has the cloth; it is thrown back along that direction, up to MC_FACE_STEP at a time. Cloth draped round a
			// thin shape of a body that holds still (a cape lying over the calf and heel of a crouching figure, the
			// animated cape passing the leg on its other side) has not gone through anything: it lies there. Its
			// triangles on the shape's flank tipped over the limit one after the other and were thrown 3 units 'back',
			// the links pulled them round again, and the cape tossed for seconds after its wearer had crouched. A
			// triangle that was thrown lately and lies clear of the shape again when a step begins, about where it is now,
			// is where the cloth lies: the throw did not hold. It is kept clear of the shape there (the normal contact), for
			// as long as it stays so. The first throw is made as before: cloth that can go back to its side still does.
			// Only while the pose holds the triangle still: in motion the throw is what brings cloth back to its side.
			// SJE r_meshClothEase: the same for a triangle that has been lying at rest (every free corner for
			// MC_EASE_REST steps), with no throw needed first. Cloth at rest between two shapes (the strip under a robe
			// between the legs of a crouching figure) sat just inside the limit; a drift of a tenth of a unit tipped a
			// triangle over it, it was thrown 3 units, its neighbours followed, and both legs threw the strip to and fro
			// until the shake watchdog let it go.
			bool lain = false;
			if (through && !limb && ease && poseStill && in->quiet.size() == (size_t)g->numParticles) {
				lain = true;
				for (int k = 0; k < 3; k++) if (g->freedom[id[k]] > 0.0f && in->quiet[id[k]] < MC_EASE_REST) lain = false;
			}
			if (through && !limb && dist >= 1e-4f && poseStill && in->thrown.size() == (size_t)g->numParticles
				&& (lain || (rest && (in->thrown[id[0]] || in->thrown[id[1]] || in->thrown[id[2]])))) {
				if (!fp.have) {
					fp.have = 1;
					fp.d0 = 0.0f;
					fp.n0[0] = fp.n0[1] = fp.n0[2] = 0.0f;
					if (in->faceCapsFrom && in->faceStart.size() == in->p.size()) {
						const mcCapsule_t& c0 = in->faceCapsFrom[fp.cap];
						const float* s[3] = { &in->faceStart[id[0] * 3], &in->faceStart[id[1] * 3], &in->faceStart[id[2] * 3] };
						float b0[3], t0;
						fp.d0 = ClosestSegTri(s[0], s[1], s[2], c0.a, c0.b, b0, t0);
						if (fp.d0 > 1e-4f) {
							vec3_t sp, sa, sab;
							for (int k = 0; k < 3; k++) sp[k] = b0[0] * s[0][k] + b0[1] * s[1][k] + b0[2] * s[2][k];
							mcSub(c0.b, c0.a, sab);
							mcMA(c0.a, t0, sab, sa);
							mcSub(sp, sa, fp.n0);
							mcScale(fp.n0, 1.0f / fp.d0, fp.n0);
						}
					}
				}
				if (fp.d0 >= 0.8f * minD && mcDot(fp.n0, nv) > 0.5f * dist) {	// clear, and within 60 degrees of where it is now
					through = false;
					for (int k = 0; k < 3; k++) in->thrown[id[k]] = MC_THROWN_STEPS;	// it lies here: left alone while it does
				}
			}
			if (through) {
				// gone through: back to its own side
				depth = lim - side;
				mcCopy(ref, dir);
			}
			else {
				if (dist >= lim || dist < 1e-4f) continue;
				// (a corner too: the point test ran before the other shapes' pushes, which may have moved it in)
				depth = lim - dist;
				mcScale(nv, 1.0f / dist, dir);
			}
			if (depth <= 0.0f) continue;
			if (depth > MC_FACE_STEP) depth = MC_FACE_STEP;
			// shared by the free corners by their weight in the closest point; where it is mostly at attached
			// corners, the animation holds it
			float kk[3], ksum = 0.0f, den = 0.0f;
			for (int k = 0; k < 3; k++) { kk[k] = w[k] * bc[k]; ksum += kk[k]; den += kk[k] * kk[k]; }
			if (ksum < 0.3f || den < 1e-6f) continue;
			const float lam = depth / den;
			for (int k = 0; k < 3; k++) {
				if (kk[k] <= 0.0f) continue;
				float mv = lam * kk[k];
				// arms, hands and held things have no lever: a corner never moves further than the contact is deep
				// (with the closest point mostly at attached corners, the free corner would move depth / its weight:
				// a hand on the hip threw the robe's top corners out). The body and the shoulders keep it: the cape
				// over the shoulder armour hangs from attached corners and has to be lifted clear by the free one.
				if (hand && mv > depth) mv = depth;
				if (mv > MC_FACE_STEP) mv = MC_FACE_STEP;
				mv *= sc[k];	// SJE r_meshClothEase: a point that is being let go
				// SJE r_meshClothEase: a point the shake watchdog let go (FightUpdate) was given back after about 3 s at the
				// test's full strength. Where the cloth had come to rest in a place two body shapes dispute (the strip under
				// a robe between the thighs of a crouching figure: each leg takes it for 'gone through' and throws it 3
				// units its own way), that was a throw of several units every few seconds for as long as the pose was held:
				// thrown, shaking, let go, at rest, given back, thrown. Such a point now comes back gently: while the pose
				// holds it still the body's shapes move it by a small step per call. Cloth that has a place to go creeps
				// there; cloth that has none trembles by less than can be seen until the watchdog lets it go again. Once
				// the pose moves the point, the test acts in full as before. Not arms, hands and held things (they are
				// not let go either).
				if (ease && !limb && in->eased.size() == (size_t)g->numParticles && in->eased[id[k]]) {
					const float gentle = MC_EASE_STEP * in->linkScale;
					if (mv > gentle) mv = gentle;
				}
				mcMA(x[k], mv, dir, x[k]);
				float* pu = &in->push[id[k] * 3];
				mcMA(pu, mv, dir, pu);
				if (!in->contact[id[k]]) in->contact[id[k]] = 1;
				if (through && !limb && in->thrown.size() == (size_t)g->numParticles) in->thrown[id[k]] = MC_THROWN_STEPS;	// SJE r_meshClothRest
				if (limb && in->limbPush.size() == (size_t)g->numParticles) in->limbPush[id[k]] = MC_LIMB_STEPS;	// SJE r_meshClothCalm (SolveSelfPairs)
				mcCopy(dir, &in->contactN[id[k] * 3]);
			}
			S[3] = -1.0f;	// moved: its bounding sphere is out of date
		}
	}

	void Reset(mcInstance_t* in, int time) {
#ifdef MC_DEBUG
		fprintf(stderr, "MC RESET at %d\n", time);
#endif
		in->p = in->target;
		in->prev = in->target;
		in->targetPrev = in->target;
		in->simTarget = in->target;
		in->accum = 0;
		in->lastTime = time;
		in->active = true;
		in->linkScale = 1.0f;
		in->lastOrigin[0] = in->lastOrigin[1] = in->lastOrigin[2] = 0.0f;
		in->originVel[0] = in->originVel[1] = in->originVel[2] = 0.0f;
		in->originTime = time;
		in->pinPrev.clear();
		in->side[0] = 1.0f; in->side[1] = in->side[2] = 0.0f;
		in->pinTime = time;
		in->asleep = false;
		in->restSteps = 0;
		in->restRef.clear();
		in->breezeTaut = 0.0f;
	}

	// Velocity pass after a substep's constraints, for vertices touching the
	// body (Macklin et al. 2014 / Mueller et al. 2020): Verlet turns every
	// position correction into speed, so a leg pushing the cape out would
	// throw it. The push is kept as a move but mostly not as speed, and the
	// sliding speed against the moving body is cut by friction.
	void ContactVelocity(const mcGroup_t* g, mcInstance_t* in, float frictionScale) {
		const float keepPush = 0.2f;	// part of the push that may become speed
		for (int p = 0; p < g->numParticles; p++) {
			if (!in->contact[p] || g->freedom[p] <= 0.0f) continue;
			float* pp = &in->p[p * 3], * pv = &in->prev[p * 3];
			mcMA(pv, 1.0f - keepPush, &in->push[p * 3], pv);
			// speed relative to the body surface (the animated vertex's motion)
			vec3_t v, vb, vr, vt;
			mcSub(pp, pv, v);
			mcSub(&in->target[p * 3], &in->targetSub[p * 3], vb);
			mcSub(v, vb, vr);
			const float* n = &in->contactN[p * 3];
			const float vn = mcDot(vr, n);
			mcMA(vr, -vn, n, vt);
			// Coulomb friction (Macklin et al. 2014): a contact that slides less than
			// sfriction x how hard it is pressed in sticks to the body (cloth resting on
			// shoulders stops buzzing); otherwise kinetic friction slows the slide.
			// r_meshClothFriction scales both (1 = the config's values, 0 = frictionless).
			const float press = mcLen(&in->push[p * 3]) + (vn < 0.0f ? -vn : 0.0f);
			const float slide = mcLen(vt);
			float k = Clamp01(g->fricK[p] * frictionScale);
			if (slide <= g->fricS[p] * frictionScale * press) k = 1.0f;
			mcMA(pv, k, vt, pv);
			// no new speed into the body
			if (vn < 0.0f) mcMA(pv, vn, n, pv);
		}
	}

	// Self-collision: keeps cloth from folding through itself. Particles
	// closer than the cloth thickness are pushed apart, except pairs that are
	// already close in the modelled shape (neighbours, layered panels).
	void SelfCollide(const mcGroup_t* g, mcInstance_t* in) {
		if (!g->anySelf) return;
		const float thick = g->selfThick * in->linkScale;
		if (!(thick > 0.05f) || thick > 64.0f) return;	// no thickness (or a broken scale): nothing to separate
		const float cell = thick;
		// SJE: allocation-free broad phase. The hash map of vectors it replaces was cleared and refilled
		// every step (hundreds of small heap allocations per character per step). Particles are sorted by
		// cell key instead; the 27 neighbour cells are then found by binary search in the sorted list.
		auto cellOf = [&](const float* v, int k) { return (long long)floorf(v[k] / cell); };
		auto key = [](long long x, long long y, long long z) { return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL); };
		struct cellEntry_t { long long key; int p; };
		thread_local static std::vector<cellEntry_t> cells;	// per thread: cloth can simulate on job workers
		cells.clear();
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] <= 0.0f || !g->selfCol[p]) continue;
			const float* v = &in->p[p * 3];
			cells.push_back({ key(cellOf(v, 0), cellOf(v, 1), cellOf(v, 2)), p });
		}
		std::sort(cells.begin(), cells.end(), [](const cellEntry_t& x, const cellEntry_t& y) { return x.key < y.key; });
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] <= 0.0f || !g->selfCol[p]) continue;
			float* a = &in->p[p * 3];
			const long long cx = cellOf(a, 0), cy = cellOf(a, 1), cz = cellOf(a, 2);
			for (long long dx = -1; dx <= 1; dx++) for (long long dy = -1; dy <= 1; dy++) for (long long dz = -1; dz <= 1; dz++) {
				const long long k = key(cx + dx, cy + dy, cz + dz);
				auto it = std::lower_bound(cells.begin(), cells.end(), k, [](const cellEntry_t& e, long long v) { return e.key < v; });
				for (; it != cells.end() && it->key == k; ++it) {
					const int q = it->p;
					if (q <= p) continue;
					vec3_t bd;
					mcSub(&g->bindPos[p * 3], &g->bindPos[q * 3], bd);
					const float bindDist = mcLen(bd) * in->linkScale;
					if (bindDist < 2.5f * thick) continue;	// neighbours in the cloth
					float* b = &in->p[q * 3];
					vec3_t d;
					mcSub(a, b, d);
					const float len = mcLen(d);
					if (len >= thick || len < 1e-5f) continue;
					const float corr = 0.5f * (thick - len) / len;
					mcMA(a, corr, d, a);
					mcMA(b, -corr, d, b);
					// a separation, not a throw
					mcMA(&in->prev[p * 3], corr, d, &in->prev[p * 3]);
					mcMA(&in->prev[q * 3], -corr, d, &in->prev[q * 3]);
				}
			}
		}
	}

	// SJE: r_meshClothSelfSub: the pairs of self-colliding particles closer than twice the cloth thickness
	// (cloth moves during a step: the wider radius catches pairs that close in on each other in it), found
	// once per 16 ms step with the same sorted-cell broad phase as SelfCollide. Solved per substep in
	// SolveSelfPairs.
	void BuildSelfPairs(const mcGroup_t* g, mcInstance_t* in) {
		in->selfPairs.clear();
		if (!g->anySelf) return;
		const float thick = g->selfThick * in->linkScale;
		if (!(thick > 0.05f) || thick > 64.0f) return;
		const float cell = 2.0f * thick;
		auto cellOf = [&](const float* v, int k) { return (long long)floorf(v[k] / cell); };
		auto key = [](long long x, long long y, long long z) { return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL); };
		struct cellEntry_t { long long key; int p; };
		thread_local static std::vector<cellEntry_t> cells;
		cells.clear();
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] <= 0.0f || !g->selfCol[p]) continue;
			const float* v = &in->p[p * 3];
			cells.push_back({ key(cellOf(v, 0), cellOf(v, 1), cellOf(v, 2)), p });
		}
		std::sort(cells.begin(), cells.end(), [](const cellEntry_t& x, const cellEntry_t& y) { return x.key < y.key; });
		const size_t maxPairs = 65536 * 2;	// safety cap (a broken scale bunching the whole cloth together)
		for (int p = 0; p < g->numParticles && in->selfPairs.size() < maxPairs; p++) {
			if (g->freedom[p] <= 0.0f || !g->selfCol[p]) continue;
			const float* a = &in->p[p * 3];
			const long long cx = cellOf(a, 0), cy = cellOf(a, 1), cz = cellOf(a, 2);
			for (long long dx = -1; dx <= 1; dx++) for (long long dy = -1; dy <= 1; dy++) for (long long dz = -1; dz <= 1; dz++) {
				const long long k = key(cx + dx, cy + dy, cz + dz);
				auto it = std::lower_bound(cells.begin(), cells.end(), k, [](const cellEntry_t& e, long long v) { return e.key < v; });
				for (; it != cells.end() && it->key == k; ++it) {
					const int q = it->p;
					if (q <= p) continue;
					vec3_t bd;
					mcSub(&g->bindPos[p * 3], &g->bindPos[q * 3], bd);
					if (mcLen(bd) * in->linkScale < 2.5f * thick) continue;	// neighbours in the cloth
					vec3_t d;
					mcSub(a, &in->p[q * 3], d);
					if (mcDot(d, d) >= cell * cell) continue;
					if (in->selfPairs.size() >= maxPairs) break;
					in->selfPairs.push_back(p);
					in->selfPairs.push_back(q);
				}
			}
		}
	}

	// The cloth's own side-to-side direction: the line of its attached edge
	// (shoulder to shoulder, belt left to right) in the horizontal plane.
	void UpdateSide(const mcGroup_t* g, mcInstance_t* in, const std::vector<float>& target) {
		const float* up = in->up;
		vec3_t c = { 0, 0, 0 };
		int n = 0;
		for (int p = 0; p < g->numParticles; p++) if (g->freedom[p] <= 0.0f) { mcAdd(c, &target[p * 3], c); n++; }
		if (n < 2) return;
		mcScale(c, 1.0f / n, c);
		// two horizontal axes, then the 2x2 covariance of the edge in that plane
		vec3_t e1, e2, ref = { 1, 0, 0 };
		if (fabsf(mcDot(ref, up)) > 0.9f) { ref[0] = 0; ref[1] = 1; }
		mcCross(up, ref, e1); mcNorm(e1);
		mcCross(up, e1, e2);
		float sxx = 0, sxy = 0, syy = 0;
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] > 0.0f) continue;
			vec3_t d;
			mcSub(&target[p * 3], c, d);
			const float x = mcDot(d, e1), y = mcDot(d, e2);
			sxx += x * x; sxy += x * y; syy += y * y;
		}
		const float ang = 0.5f * atan2f(2.0f * sxy, sxx - syy);
		for (int k = 0; k < 3; k++) in->side[k] = e1[k] * cosf(ang) + e2[k] * sinf(ang);
		mcNorm(in->side);
	}

	// Max spin: the rotational partner of max acceleration. Saber moves whip
	// the shoulders round far faster than cloth can follow, which winds a cape
	// into a rope. Measure how fast the attached edge turns about the up axis
	// this frame; any turning beyond the limit carries the free cloth round
	// with it instead of winding it up.

	// bind-pose distance of every particle from the attached edge, along the cloth's links
	static void ComputeRootDist(const mcGroup_t* g) {
		if (g->reach < 0.0f) {
			float r = 0.0f, t = 0.0f;
			for (const mcDef_t* d : g->defs) { if (d->reach > r) r = d->reach; if (d->turn > t) t = d->turn; }
			g->reach = r; g->turn = t;
		}
		if ((int)g->rootDist.size() == g->numParticles) return;
		const int n = g->numParticles;
		std::vector<std::vector<std::pair<int, float>>> adj(n);
		for (size_t l = 0; l < g->linkA.size(); l++) {
			if (g->linkBend[l]) continue;
			const int a = g->linkA[l], b = g->linkB[l];
			vec3_t d; mcSub(&g->bindPos[a * 3], &g->bindPos[b * 3], d);
			const float len = mcLen(d);
			adj[a].push_back({ b, len }); adj[b].push_back({ a, len });
		}
		std::vector<float> dist(n, 1e9f);
		std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<std::pair<float, int>>> q;
		for (int p = 0; p < n; p++) if (g->freedom[p] <= 0.0f) { dist[p] = 0.0f; q.push({ 0.0f, p }); }
		while (!q.empty()) {
			const auto top = q.top(); q.pop();
			if (top.first > dist[top.second]) continue;
			for (const auto& e : adj[top.second]) {
				const float nd = top.first + e.second;
				if (nd < dist[e.first]) { dist[e.first] = nd; q.push({ nd, e.first }); }
			}
		}
		for (float& d : dist) if (d > 1e8f) d = 0.0f;	// no attached point reachable: no reach limit (tether only)
		g->rootDist.swap(dist);
		// SJE: ... and as a part of the cloth's length (r_meshClothBillow: the bulge is strongest a third of the way down).
		// The length is that of the whole sheet a point belongs to - all cloth joined to it by links, across
		// surfaces: a cape modelled as two surfaces sewn together is one sheet from collar to hem.
		{
			std::vector<int> sheet(n, -1);
			std::vector<float> longest;
			std::vector<int> stack;
			for (int p0 = 0; p0 < n; p0++) {
				if (sheet[p0] >= 0) continue;
				const int id = (int)longest.size();
				longest.push_back(0.0f);
				sheet[p0] = id;
				stack.push_back(p0);
				while (!stack.empty()) {
					const int p = stack.back();
					stack.pop_back();
					if (g->rootDist[p] > longest[id]) longest[id] = g->rootDist[p];
					for (const auto& e : adj[p]) if (sheet[e.first] < 0) { sheet[e.first] = id; stack.push_back(e.first); }
				}
			}
			std::vector<float> part(n, 0.0f);
			for (int p = 0; p < n; p++) if (longest[sheet[p]] > 1.0f) part[p] = g->rootDist[p] / longest[sheet[p]];
			g->rootS.swap(part);
			g->numSheets = (int)longest.size();	// SJE r_meshClothCarryFloor
			g->sheetOf.swap(sheet);
		}
	}

	// rotate the free particles (position and previous position: velocity turns with them) about an
	// axis through c by angle a (radians)
	static void RotateFree(const mcGroup_t* g, mcInstance_t* in, const vec3_t c, const vec3_t axis, float a) {
		const float cs = cosf(a), sn = sinf(a);
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] <= 0.0f) continue;
			float* pts[2] = { &in->p[p * 3], &in->prev[p * 3] };
			for (float* q : pts) {
				vec3_t r, par, perp, side;
				mcSub(q, c, r);
				mcScale(axis, mcDot(r, axis), par);
				mcSub(r, par, perp);
				mcCross(axis, perp, side);
				for (int k = 0; k < 3; k++) q[k] = c[k] + par[k] + perp[k] * cs + side[k] * sn;
			}
		}
	}

	// one world plane (floor or wall): free cloth stays in front of it, with friction
	static void PlaneCollide(const mcGroup_t* g, mcInstance_t* in, const mcParams_t* prm, const float* pt, const float* pn) {
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] <= 0.0f) continue;
			float* pp = &in->p[p * 3], * pv = &in->prev[p * 3];
			vec3_t r;
			mcSub(pp, pt, r);
			const float d = mcDot(r, pn) - MC_FLOOR_GAP;
			if (d >= 0.0f) continue;
			mcMA(pp, -d, pn, pp);
			// friction: lose most of the sliding along the surface
			vec3_t v, vn;
			mcSub(pp, pv, v);
			mcScale(pn, mcDot(v, pn), vn);
			vec3_t vt;
			mcSub(v, vn, vt);
			const float fk = Clamp01(0.3f + g->fricK[p] * prm->frictionScale);	// floors and walls grip a little more than bodies (default 0.6)
			mcMA(pv, fk, vt, pv);
			if (mcDot(v, pn) < 0.0f) mcAdd(pv, vn, pv);
		}
	}

	// keep free cloth above the floor under the character and in front of the walls round it
	// (planes from traces by the renderer; walls: r_meshClothWalls)
	static void FloorCollide(const mcGroup_t* g, mcInstance_t* in, const mcParams_t* prm) {
		if (prm->floor) PlaneCollide(g, in, prm, prm->floorPt, prm->floorN);
		const int nw = prm->numWalls < 0 ? 0 : (prm->numWalls > 8 ? 8 : prm->numWalls);
		for (int w = 0; w < nw; w++) PlaneCollide(g, in, prm, prm->wallPt[w], prm->wallN[w]);
	}

	void SpinLimit(const mcGroup_t* g, mcInstance_t* in, const std::vector<float>& target, int timeMs, float defSpin, const mcParams_t* prm) {
		const size_t n3 = (size_t)g->numParticles * 3;
		const int dtMs = timeMs - in->pinTime;
		if (in->pinPrev.size() != n3 || dtMs <= 0 || dtMs > MC_STALE_MS) {
			in->pinPrev.assign(target.begin(), target.end());
			in->pinTime = timeMs;
			in->twistRun = 0.0f;
			return;
		}
		const float* up = in->up;
		// centroid of the attached particles, then their mean turn about it
		vec3_t c0 = { 0, 0, 0 }, c1 = { 0, 0, 0 };
		int np = 0;
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] > 0.0f) continue;
			mcAdd(c0, &in->pinPrev[p * 3], c0);
			mcAdd(c1, &target[p * 3], c1);
			np++;
		}
		float angle = 0.0f;
		if (np >= 3) {
			mcScale(c0, 1.0f / np, c0);
			mcScale(c1, 1.0f / np, c1);
			float sx = 0.0f, sy = 0.0f;	// summed cross and dot of the horizontal arms
			for (int p = 0; p < g->numParticles; p++) {
				if (g->freedom[p] > 0.0f) continue;
				vec3_t a, b, x;
				mcSub(&in->pinPrev[p * 3], c0, a);
				mcSub(&target[p * 3], c1, b);
				mcMA(a, -mcDot(a, up), up, a);
				mcMA(b, -mcDot(b, up), up, b);
				mcCross(a, b, x);
				sx += mcDot(x, up);
				sy += mcDot(a, b);
			}
			angle = atan2f(sx, sy);	// radians this frame, about up
		}
		// best-fit rotation of the attached points this frame (Horn's quaternion), split into the twist
		// about 'up' (handled below as before) and the swing (tilt: flips, rolls, lunges). A swing faster
		// than MC_SWING_FREE deg/s is carried over to the free cloth, so in a front flip or a cartwheel
		// the cape turns with the body instead of being left behind, inverted, inside it.
		if (np >= 6) {
			float M[3][3] = { { 0 } };
			float spread = 0.0f;
			for (int p = 0; p < g->numParticles; p++) {
				if (g->freedom[p] > 0.0f) continue;
				vec3_t a, b;
				mcSub(&in->pinPrev[p * 3], c0, a);
				mcSub(&target[p * 3], c1, b);
				spread += mcDot(a, a);
				for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) M[i][j] += a[i] * b[j];
			}
			const float Sxx = M[0][0], Sxy = M[0][1], Sxz = M[0][2], Syx = M[1][0], Syy = M[1][1], Syz = M[1][2];
			const float Szx = M[2][0], Szy = M[2][1], Szz = M[2][2];
			float N[4][4] = {
				{ Sxx + Syy + Szz, Syz - Szy, Szx - Sxz, Sxy - Syx },
				{ Syz - Szy, Sxx - Syy - Szz, Sxy + Syx, Szx + Sxz },
				{ Szx - Sxz, Sxy + Syx, -Sxx + Syy - Szz, Syz + Szy },
				{ Sxy - Syx, Szx + Sxz, Syz + Szy, -Sxx - Syy + Szz } };
			// largest eigenvector by power iteration on N + shift*I (shift keeps it positive definite)
			const float shift = spread + 1e-3f;
			float q[4] = { 1, 0, 0, 0 };
			for (int it = 0; it < 40; it++) {
				float r[4];
				for (int i = 0; i < 4; i++) r[i] = shift * q[i] + N[i][0] * q[0] + N[i][1] * q[1] + N[i][2] * q[2] + N[i][3] * q[3];
				const float l = sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2] + r[3] * r[3]);
				if (l < 1e-12f) break;
				for (int i = 0; i < 4; i++) q[i] = r[i] / l;
			}
			if (q[0] < 0) for (float& v : q) v = -v;
			// twist about up, swing = q * conj(twist)
			const float tv = q[1] * up[0] + q[2] * up[1] + q[3] * up[2];
			float tw[4] = { q[0], tv * up[0], tv * up[1], tv * up[2] };
			float tl = sqrtf(tw[0] * tw[0] + tw[1] * tw[1] + tw[2] * tw[2] + tw[3] * tw[3]);
			if (tl > 1e-6f) {
				for (float& v : tw) v /= tl;
				const float cw[4] = { tw[0], -tw[1], -tw[2], -tw[3] };
				float sw[4];
				sw[0] = q[0] * cw[0] - q[1] * cw[1] - q[2] * cw[2] - q[3] * cw[3];
				sw[1] = q[0] * cw[1] + q[1] * cw[0] + q[2] * cw[3] - q[3] * cw[2];
				sw[2] = q[0] * cw[2] - q[1] * cw[3] + q[2] * cw[0] + q[3] * cw[1];
				sw[3] = q[0] * cw[3] + q[1] * cw[2] - q[2] * cw[1] + q[3] * cw[0];
				if (sw[0] < 0) for (float& v : sw) v = -v;
				const float sv = sqrtf(sw[1] * sw[1] + sw[2] * sw[2] + sw[3] * sw[3]);
				const float sa = 2.0f * atan2f(sv, sw[0]);
				const float dts = dtMs * 0.001f;
				const float freeA = MC_SWING_FREE * (3.14159265f / 180.0f) * dts;
				if (sv > 1e-6f && sa > freeA) {
					const vec3_t ax = { sw[1] / sv, sw[2] / sv, sw[3] / sv };
					RotateFree(g, in, c1, ax, sa - freeA);
				}
			}
		}
		in->pinPrev.assign(target.begin(), target.end());
		in->pinTime = timeMs;
		if (angle == 0.0f) { in->twistRun = 0.0f; return; }
		// a turn faster than MC_TWIST_FREE deg/s (the player's body snapping round with the camera,
		// an instant re-face in an animation) carries the cloth by the excess
		{
			const float freeT = (prm && prm->twistFree > 0.0f ? prm->twistFree : MC_TWIST_FREE) * (3.14159265f / 180.0f) * (dtMs * 0.001f);
			if (fabsf(angle) <= freeT) in->twistRun = 0.0f;
			else {
				in->twistRun = in->twistRun != 0.0f && (in->twistRun > 0.0f) == (angle > 0.0f) ? in->twistRun + angle : angle;
				float ex = angle > 0.0f ? angle - freeT : angle + freeT;
				// SJE (r_meshClothTwistLag): ... but only as far as the free cloth is more than that many degrees behind
				// the turn. Carried at once, the cape went round with a fast flick of the body like a part of it and
				// was back with it, without trailing: at 1300 deg/s its hem made the whole turn with the body, at 700
				// half of it. The cloth's own turn about up against where the pose has it now (this frame's turn is in
				// it: the pose has moved on, the cloth not yet) is its lag.
				if (prm && prm->twistLag >= 0.0f) {
					float lx = 0.0f, ly = 0.0f;
					for (int p = 0; p < g->numParticles; p++) {
						if (g->freedom[p] <= 0.5f) continue;
						vec3_t a, b, x;
						mcSub(&target[p * 3], c1, a);
						mcSub(&in->p[p * 3], c1, b);
						mcMA(a, -mcDot(a, up), up, a);
						mcMA(b, -mcDot(b, up), up, b);
						mcCross(a, b, x);
						lx += mcDot(x, up);
						ly += mcDot(a, b);
					}
					const float lag = atan2f(lx, ly);	// the cloth's turn against the pose, in the sense of 'angle'
					// The allowance is for a flick. In a fast turn that goes on in one direction (a spin, circling while
					// strafing) it runs out: there the cloth is carried as soon as it is behind at all, as far as before,
					// and trails no further round its wearer than it did.
					float allow = prm->twistLag;
					const float run = fabsf(in->twistRun) * (180.0f / 3.14159265f);
					if (run > MC_TWIST_FLICK0) allow *= run >= MC_TWIST_FLICK1 ? 0.0f : (MC_TWIST_FLICK1 - run) / (MC_TWIST_FLICK1 - MC_TWIST_FLICK0);
					float need = (angle > 0.0f ? -lag : lag) - allow * (3.14159265f / 180.0f);
					if (need < 0.0f) need = 0.0f;
					if (fabsf(ex) > need) ex = ex > 0.0f ? need : -need;
				}
				if (prm) ex *= prm->twistCarry <= 0.0f ? 0.0f : (prm->twistCarry < 1.0f ? prm->twistCarry : 1.0f);
				if (ex != 0.0f) RotateFree(g, in, c1, up, ex);
				angle -= ex;
			}
		}
		// angular inertia scale: the attached edge's turn this frame (whatever turned it: the body
		// turning, the legs re-facing or the torso aiming, it is read from the posed attachment
		// itself) carries the free cloth along by 'turn', position and velocity alike; the cloth
		// feels only the rest of the turn as inertia
		if (g->turn > 0.0f) {
			const float ca = angle * g->turn;
			const float cs = cosf(ca), sn = sinf(ca);
			for (int p = 0; p < g->numParticles; p++) {
				if (g->freedom[p] <= 0.0f) continue;
				float* pts[2] = { &in->p[p * 3], &in->prev[p * 3] };
				for (float* q : pts) {
					vec3_t r, par, perp, side;
					mcSub(q, c1, r);
					mcScale(up, mcDot(r, up), par);
					mcSub(r, par, perp);
					mcCross(up, perp, side);
					for (int k = 0; k < 3; k++) q[k] = c1[k] + par[k] + perp[k] * cs + side[k] * sn;
				}
			}
			angle -= ca;
		}
		const float dt = dtMs * 0.001f;
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] <= 0.0f) continue;
			const float lim = (g->maxSpin[p] >= 0.0f ? g->maxSpin[p] : defSpin) * (3.14159265f / 180.0f) * dt;
			if (lim <= 0.0f || fabsf(angle) <= lim) continue;
			const float ex = angle > 0.0f ? angle - lim : angle + lim;
			const float cs = cosf(ex), sn = sinf(ex);
			float* pts[2] = { &in->p[p * 3], &in->prev[p * 3] };
			for (float* q : pts) {
				// rotate about the vertical line through the attached edge's centre
				vec3_t r, par, perp, side;
				mcSub(q, c1, r);
				mcScale(up, mcDot(r, up), par);
				mcSub(r, par, perp);
				mcCross(up, perp, side);
				for (int k = 0; k < 3; k++) q[k] = c1[k] + par[k] + perp[k] * cs + side[k] * sn;
			}
		}
	}

	// SJE: per-triangle air (r_meshClothAero 1). Each triangle feels the air by its own normal, area and mean
	// velocity relative to the wind: the normal ("sail") drag as before, plus extra lift (the drag's part across
	// the airflow, half again). Each point gets the area-weighted mean of its triangles' velocity change
	// (force / lumped mass), so small or sliver triangles no longer weigh as much as big ones and the push
	// follows the real faces instead of the averaged point normals. Per substep, before the points move.
	static void TriangleAir(const mcGroup_t* g, mcInstance_t* in, const mcParams_t* prm, const vec3_t windNow,
		const float* windF, float h) {
		const size_t n3 = (size_t)g->numParticles * 3;
		in->aeroDv.assign(n3, 0.0f);
		in->aeroW.assign(g->numParticles, 0.0f);
		const float wetW = 1.0f + Clamp01(prm->wet);
		for (size_t t = 0; t + 2 < g->tris.size(); t += 3) {
			const int i0 = g->tris[t], i1 = g->tris[t + 1], i2 = g->tris[t + 2];
			if (g->freedom[i0] <= 0.0f && g->freedom[i1] <= 0.0f && g->freedom[i2] <= 0.0f) continue;
			const float* a = &in->p[i0 * 3], * b = &in->p[i1 * 3], * c = &in->p[i2 * 3];
			vec3_t e1, e2, nn;
			mcSub(b, a, e1);
			mcSub(c, a, e2);
			mcCross(e1, e2, nn);
			const float area2 = mcNorm(nn);
			if (area2 < 1e-6f) continue;
			// mean velocity (per substep) relative to the air
			vec3_t u = { 0, 0, 0 }, vi;
			const int id[3] = { i0, i1, i2 };
			float wf = 0.0f, invW = 0.0f;
			for (int k = 0; k < 3; k++) {
				mcSub(&in->p[id[k] * 3], &in->prev[id[k] * 3], vi);
				mcAdd(u, vi, u);
				wf += windF ? windF[id[k]] : 1.0f;
				invW += 1.0f / (g->weight[id[k]] * wetW);
			}
			mcScale(u, 1.0f / 3.0f, u);
			mcMA(u, -h * wf / 3.0f, windNow, u);
			invW *= 1.0f / 3.0f;
			float kn = prm->drag * h * invW;
			if (kn > 1.0f) kn = 1.0f;
			const float un = mcDot(u, nn);
			vec3_t dv;
			mcScale(nn, -un * kn, dv);
			// lift: the part of the push across the airflow, half again
			const float ul = mcLen(u);
			if (ul > 1e-6f) {
				vec3_t uh, side;
				mcScale(u, 1.0f / ul, uh);
				mcMA(dv, -mcDot(dv, uh), uh, side);
				mcMA(dv, 0.5f, side, dv);
			}
			const float area = 0.5f * area2;
			for (int k = 0; k < 3; k++) {
				mcMA(&in->aeroDv[id[k] * 3], area, dv, &in->aeroDv[id[k] * 3]);
				in->aeroW[id[k]] += area;
			}
		}
	}

	// One 16 ms step, split into substeps ("Small Steps in Physics
	// Simulation", Macklin et al. 2019): many short steps with a single
	// constraint pass each keep the fabric from stretching or wobbling far
	// better than one long step with many passes.
	void Step(const mcGroup_t* g, mcInstance_t* in, const std::vector<float>& tgtFrom, const std::vector<float>& tgtTo,
		const mcCapsule_t* caps, int ncaps, const mcParams_t* prm, const mcCapsule_t* capsFrom = nullptr) {
		// body capsules swept through the substeps (from where they were at the start of this step to where they are at
		// its end): a fast saber arm moves several units per 16 ms step, and checking only its end position lets it
		// jump clean through the cape (KatarnEngine)
		mcCapsule_t capsSub[MC_NEAR_CAPS];
		if (ncaps > MC_NEAR_CAPS) ncaps = MC_NEAR_CAPS;
		const int sub = prm->iterations < 1 ? 1 : (prm->iterations > 16 ? 16 : prm->iterations);
		const float h = MC_DT / sub;
		const float damp = powf(0.99f, 1.0f / sub);			// 1% velocity loss per 16 ms
		// bend stiffness per substep so the total doesn't depend on the count
		const float bendScale = 1.0f / sub;	// exponent: k_sub = 1 - (1 - bend)^(1/sub)
		mcSolve_t solve;
		solve.bendScale = bendScale;
		solve.h = h;
		solve.xpbd = prm->xpbd;
		solve.xpbdIters = prm->xpbdIters;
		solve.liftLimit = prm->liftLimit < -1.5f ? -2.0f : prm->liftLimit;
		solve.liftSlope = prm->liftSlope > 0.0f ? (prm->liftSlope < 1.0f ? prm->liftSlope : 1.0f) : 0.0f;
		solve.floorOk = prm->floor ? 1 : 0;
		mcCopy(prm->floorPt, solve.floorPt);
		mcCopy(prm->floorN, solve.floorN);
		solve.edgeCol = prm->edgeCol;
		solve.faceCol = g->tris.size() >= 3 ? prm->faceCol : 0;
		solve.overArms = prm->overArms;
		solve.rest = prm->rest != 0;
		solve.ease = prm->ease != 0;
		// SJE: per-step constants. These powf()s ran per particle (tether, spread, shape) or per bend link in
		// every substep; they only depend on the substep count and the static settings.
		solve.tetherK = 1.0f - powf(0.85f, bendScale * 6.0f);
		solve.bendModel = prm->bendModel == 1 && !g->bendAlpha.empty() ? 1 : 0;
		solve.bendK = nullptr;
		if (!(solve.xpbd && g->linkAlpha.size() == g->linkA.size()) && !solve.bendModel) {
			std::vector<float>& kb = in->kBendLink;
			kb.resize(g->linkA.size());
			for (size_t l = 0; l < g->linkA.size(); l++) {
				kb[l] = 0.0f;
				if (!g->linkBend[l]) continue;
				const float bs = 0.5f * (g->bend[g->linkA[l]] + g->bend[g->linkB[l]]);
				if (bs > 0.0f) kb[l] = 1.0f - powf(1.0f - (bs < 0.99f ? bs : 0.99f), bendScale);
			}
			solve.bendK = kb.data();
		}
		{
			std::vector<float>& ksp = in->kSpread, & ksh = in->kShape;
			ksp.resize(g->numParticles);
			ksh.resize(g->numParticles);
			const float isub = 1.0f / sub;
			for (int p = 0; p < g->numParticles; p++) {
				const float sp = g->spread[p], sh = g->shape[p] * prm->shapeScale;
				ksp[p] = sp > 0.0f ? 1.0f - powf(1.0f - Clamp01(sp), isub) : 0.0f;
				ksh[p] = sh > 0.0f ? 1.0f - powf(1.0f - Clamp01(sh), isub) : 0.0f;
			}
		}
		std::vector<float>& n = in->scratchN;
		// wind with slow gusts: two incommensurate sines, so it never repeats visibly
		vec3_t windNow;
		{
			const float t = prm->timeSec;
			const float gs = 1.0f + prm->gust * (0.6f * sinf(t * 1.3f) + 0.4f * sinf(t * 3.7f + 1.1f));
			mcScale(prm->wind, gs, windNow);
		}
		// flutter (r_meshClothFlutter): the wind's strength also varies across the cloth, in ripples that travel
		// along it (a phase from each particle's position and the time), so wind flaps a cape instead of
		// pushing it as one stiff sheet. Worked out once per step, only when there is wind.
		thread_local static std::vector<float> windF;
		const bool flutter = prm->flutter > 0.0f && mcDot(windNow, windNow) > 1e-4f;
		if (flutter) {
			windF.resize(g->numParticles);
			const float t = prm->timeSec;
			const float a = Clamp01(prm->flutter);
			for (int p = 0; p < g->numParticles; p++) {
				const float* pp = &in->p[p * 3];
				const float ph = t * 7.3f + pp[0] * 0.045f + pp[1] * 0.037f + pp[2] * 0.051f;
				windF[p] = 1.0f + a * (0.65f * sinf(ph) + 0.35f * sinf(ph * 2.3f + 1.7f));
			}
		}

		// SJE: air resistance that grows with the square of the speed (r_meshClothDragQuad). Real air pushes
		// a cloth with a force that goes with the speed squared; the linear drag, set for calm swinging, gives
		// a sprinting character's cape a few degrees of trail where a real one streams out behind him.
		// The speed taken is the character's own travel through the air (against the wind), not each cloth
		// point's: a flip or a saber spin whips the points round at several hundred units a second, and the
		// square of that threw the cape about. The travel is smooth (run, jump, fall), capped at MC_AIR_MAX,
		// and the cloth's own motion against the air keeps its linear drag.
		// And air flowing over cloth never pushes it evenly: ripples travel down the cloth from its attached
		// edge (r_meshClothFlutter, here from the travel, not only the wind), faster the faster the air goes
		// by, so a cape flaps as it streams instead of trailing as a stiff sheet.
		thread_local static std::vector<float> rip;
		bool ripple = false;
		vec3_t airH = { 0, 0, 0 };	// the travel through the air, per substep
		if (prm->dragQuad > 0.0f) {
			mcSub(in->originVel, windNow, airH);
			// falling: only part of it counts. All of it throws a cape up to its lift limit, where it piles
			// up behind the shoulders; with part of it the cape goes light and fills out as its wearer drops
			const float down = mcDot(airH, in->up);
			if (down < 0.0f) mcMA(airH, -down * (1.0f - MC_AIR_FALL), in->up, airH);
			const float U = mcLen(airH);
			mcScale(airH, (U > MC_AIR_MAX ? MC_AIR_MAX / U : 1.0f) * h, airH);
		}
		if (prm->dragQuad > 0.0f && prm->flutter > 0.0f && (int)g->rootDist.size() == g->numParticles) {
			vec3_t air;
			mcSub(in->originVel, windNow, air);
			const float U = mcLen(air);
			const float a = Clamp01(prm->flutter) * Clamp01((U - 40.0f) / 120.0f);	// none in still air
			if (a > 0.01f) {
				ripple = true;
				float hz = 0.012f * U;
				hz = hz < 0.8f ? 0.8f : (hz > 3.5f ? 3.5f : hz);
				in->ripPhase += 6.2831853f * hz * MC_DT;
				if (in->ripPhase > 6283.1853f) in->ripPhase -= 6283.1853f;
				rip.resize(g->numParticles);
				const float ks = in->linkScale > 1e-3f ? 0.125f / in->linkScale : 0.125f;	// one ripple per ~50 units of cloth
				for (int p = 0; p < g->numParticles; p++) {
					vec3_t d;
					mcSub(&in->p[p * 3], in->lastOrigin, d);
					const float ph = in->ripPhase - ks * g->rootDist[p] + 0.07f * mcDot(d, in->side);
					rip[p] = 1.0f + a * (0.65f * sinf(ph) + 0.35f * sinf(ph * 2.3f + 1.7f));
				}
			}
		}

		// SJE: r_meshClothBreeze: this step's breeze at every point (units/s, level, already scaled by the tether
		// and by how free the point is)
		thread_local static std::vector<float> breezeV;
		float breezeK = BreezeNow(prm);
		if (breezeK > 0.0f) {
			// only cloth that hangs relaxed: the most stretched link after the last step, against its animated length
			float worst2 = 1.0f;
			if (tgtFrom.size() == in->p.size()) {
				for (size_t l = 0; l < g->linkA.size(); l++) {
					if (g->linkBend[l]) continue;
					const int a = g->linkA[l], b = g->linkB[l];
					if (g->freedom[a] <= 0.0f && g->freedom[b] <= 0.0f) continue;
					vec3_t d, dT;
					mcSub(&in->p[b * 3], &in->p[a * 3], d);
					mcSub(&tgtFrom[b * 3], &tgtFrom[a * 3], dT);
					const float r2 = mcDot(dT, dT);
					if (r2 < 0.25f) continue;
					const float s2 = mcDot(d, d) / r2;
					if (s2 > worst2) worst2 = s2;
				}
			}
			const float taut = Clamp01((sqrtf(worst2) - MC_BREEZE_TAUT0) / (MC_BREEZE_TAUT1 - MC_BREEZE_TAUT0));
			if (taut > in->breezeTaut) in->breezeTaut = taut - in->breezeTaut <= MC_BREEZE_TAUT_IN ? taut : in->breezeTaut + MC_BREEZE_TAUT_IN;
			else in->breezeTaut = in->breezeTaut - taut <= MC_BREEZE_TAUT_OUT ? taut : in->breezeTaut - MC_BREEZE_TAUT_OUT;
			breezeK *= 1.0f - in->breezeTaut;
		}
		if (breezeK > 0.0f) {
			breezeV.resize((size_t)g->numParticles * 2);
			const float t = prm->timeSec;
			for (int p = 0; p < g->numParticles; p++) {
				float* b = &breezeV[(size_t)p * 2];
				const float fr = g->freedom[p];
				if (fr <= 0.0f) { b[0] = b[1] = 0.0f; continue; }
				const float* pp = &in->p[p * 3];
				const float sx = pp[0] * 0.031f + pp[1] * 0.017f + pp[2] * 0.011f, sy = pp[1] * 0.029f - pp[0] * 0.013f + pp[2] * 0.019f;
				const float gx = 0.50f * sinf(t * 1.07f + sx) + 0.32f * sinf(t * 1.93f + 1.7f + sy * 0.8f) + 0.18f * sinf(t * 3.11f + 0.6f + sx * 1.3f);
				const float gy = 0.50f * sinf(t * 0.89f + 2.1f + sy) + 0.32f * sinf(t * 1.61f + 0.4f + sx * 0.7f) + 0.18f * sinf(t * 2.71f + 2.9f + sy * 1.2f);
				const float tk = g->tether[p] * (1.0f / MC_AIR_TETHER);
				const float k = MC_BREEZE_SPEED * breezeK * (tk >= 1.0f ? 1.0f : tk * tk * tk) * (fr < 1.0f ? fr : 1.0f);
				b[0] = gx * k;
				b[1] = gy * k;
			}
		}
		// SJE: r_meshClothBillow: the level travel through the air (per substep, squared with the strength) and
		// its direction
		float billowQ = 0.0f;
		vec3_t billowDir = { 0, 0, 0 };
		if (prm->billow > 0.0f && (int)g->rootS.size() == g->numParticles) {
			mcSub(in->originVel, windNow, billowDir);
			mcMA(billowDir, -mcDot(billowDir, in->up), in->up, billowDir);
			float U = mcNorm(billowDir);
			if (U > MC_AIR_MAX) U = MC_AIR_MAX;
			// with the square of the speed at a walk, levelling off from a run on: there the travel wind already
			// streams the cape out, and more push only lifted it higher (a flat sheet again)
			const float lv = U / (MC_BILLOW_FULL * (in->linkScale > 1e-3f ? in->linkScale : 1.0f));
			billowQ = U > 1.0f ? prm->billow * MC_BILLOW_K * (U * h) * (U * h) / (1.0f + lv * lv) : 0.0f;
			// on the ground only: it fades out as the character leaves the floor (a jump, a fall), where the cape
			// is thrown about enough as it is (strafing into a jump, the lifted cape went up to the shoulders)
			if (!prm->floor) billowQ = 0.0f;
			else if (billowQ > 0.0f) {
				vec3_t of;
				mcSub(in->lastOrigin, prm->floorPt, of);
				const float up = (MC_BILLOW_AIR1 - mcDot(of, prm->floorN)) / (MC_BILLOW_AIR1 - MC_BILLOW_AIR0);
				billowQ *= up <= 0.0f ? 0.0f : (up >= 1.0f ? 1.0f : up);
			}
		}

		// the cloth's current size against its bind pose (model scale)
		float animSum = 0.0f;
		for (size_t l = 0; l < g->linkA.size(); l++) {
			if (g->linkBend[l]) continue;
			vec3_t d;
			mcSub(&tgtTo[g->linkB[l] * 3], &tgtTo[g->linkA[l] * 3], d);
			animSum += mcLen(d);
		}
		in->linkScale = g->bindLinkSum > 1e-3f ? animSum / g->bindLinkSum : 1.0f;

		in->target.resize(tgtTo.size());
		in->push.resize(tgtTo.size());
		in->contactN.resize(tgtTo.size());
		in->contact.resize(g->numParticles);
		in->targetSub.assign(tgtFrom.begin(), tgtFrom.end());
		// SJE: r_meshClothSelfSub: the close pairs once per step, kept apart in every substep
		const bool selfSub = prm->selfSub && g->anySelf;
		if (selfSub) BuildSelfPairs(g, in);
		else in->selfPairs.clear();
		// SJE: r_meshClothFaceCol: the triangle / body shape pairs that can touch in this step
		if (solve.faceCol & 1) {
			BuildFacePairs(g, in, capsFrom ? capsFrom : caps, capsFrom ? caps : nullptr, ncaps, sub);
		}
		else in->facePairs.clear();
		// SJE: ... and the shapes each point can reach in it (the point test of the substeps looks at those only)
		if ((solve.faceCol & 1) && ncaps > 0 && tgtFrom.size() == in->p.size() && tgtTo.size() == in->p.size() && (int)g->lraAnchor.size() >= g->numParticles) {
			BuildPointCaps(g, in, capsFrom ? capsFrom : caps, capsFrom ? caps : nullptr, ncaps, sub, tgtFrom, tgtTo);
			solve.ptStart = in->ptCapStart.data();
			solve.ptIdx = in->ptCaps.data();
			solve.ptFrom = in->ptFrom.data();
			solve.ptAllow = in->ptAllow.data();
		}
		// SJE: r_meshClothAero: the air pushes each triangle (by its own normal, area and velocity)
		const bool aero = prm->aero && g->tris.size() >= 3;
		const bool wornCap = prm->wornAir < 1.0f && (int)g->wrap.size() == g->numParticles && (int)g->under.size() == g->numParticles;	// SJE r_meshClothWornAir
		// SJE r_meshClothCalm: which points a limb moved lately (set by FaceCollide, read by SolveSelfPairs)
		if (in->limbPush.size() != (size_t)g->numParticles) in->limbPush.assign((size_t)g->numParticles, 0);
		else for (unsigned char& lp : in->limbPush) if (lp) lp--;
		if (in->thrown.size() != (size_t)g->numParticles) in->thrown.assign((size_t)g->numParticles, 0);	// SJE r_meshClothRest
		else for (unsigned char& th : in->thrown) if (th) th--;
		for (int s = 0; s < sub; s++) {
			const float t1 = (float)(s + 1) / sub;
			if (s > 0) in->targetSub.assign(in->target.begin(), in->target.end());
			for (size_t i = 0; i < tgtTo.size(); i++) in->target[i] = tgtFrom[i] + (tgtTo[i] - tgtFrom[i]) * t1;
			std::fill(in->push.begin(), in->push.end(), 0.0f);
			std::fill(in->contact.begin(), in->contact.end(), 0);
			ParticleNormals(g, in->p, n);
			if (aero) TriangleAir(g, in, prm, windNow, flutter ? windF.data() : nullptr, h);
			for (int p = 0; p < g->numParticles; p++) {
				const float f = g->freedom[p];
				float* pp = &in->p[p * 3], * pv = &in->prev[p * 3];
				if (f <= 0.0f) {
					mcCopy(pp, pv);
					mcCopy(&in->target[p * 3], pp);
					continue;
				}
				vec3_t v;
				mcSub(pp, pv, v);
				mcScale(v, damp, v);
				if (s == 0 && in->impulse.size() == in->p.size()) mcMA(v, h, &in->impulse[p * 3], v);
				// under water (r_meshClothWater): heavy drag instead of the air, and the cloth floats up a little
				const bool underWater = prm->water && pp[2] < prm->waterZ;
				if (underWater) {
					float kw = prm->waterDrag * h;
					if (kw > 0.9f) kw = 0.9f;
					mcScale(v, 1.0f - kw, v);
				}
				// Air. The force comes from the cloth's motion RELATIVE TO THE AIR
				// (its own world velocity minus the wind), split in two:
				//  - through the cloth (along its normal): the strong "sail" drag
				//    that makes capes billow,
				//  - along the cloth: weaker skin drag, which is what lifts a cape
				//    when its wearer falls and streams it back when he runs.
				// Both are divided by the cloth's weight (mass per area): the same
				// air moves a heavy wool cloak less than a silk cape. Gravity is
				// not divided, since every mass falls alike. Wet cloth (rain,
				// r_meshClothRain) weighs more, so the air moves it less.
				if (!underWater) {
					const float invW = 1.0f / (g->weight[p] * (1.0f + Clamp01(prm->wet)));
					vec3_t rel;
					mcMA(v, flutter ? -h * windF[p] : -h, windNow, rel);		// v is per substep, wind is per second
					const float* nn = &n[p * 3];
					const float vn = mcDot(rel, nn);
					float kn = prm->drag * h * invW;
					if (kn > 1.0f) kn = 1.0f;
					float kt = prm->airDrag * h * invW;
					if (kt > 0.5f) kt = 0.5f;
					vec3_t relT;
					mcMA(rel, -vn, nn, relT);
					if (aero) {
						// SJE: the triangles' push (normal drag + lift), area-weighted onto this point
						const float aw = in->aeroW[p];
						if (aw > 1e-8f) mcMA(v, 1.0f / aw, &in->aeroDv[p * 3], v);
					}
					else {
						mcMA(v, -vn * kn, nn, v);
					}
					mcMA(v, -kt, relT, v);
					if (prm->dragQuad > 0.0f) {
						// the travel wind through the cloth (speeds are per substep: |a| h^2 = c |U h| |U h|).
						// How much of it a surface catches goes with its tether, the config's measure of how far
						// that cloth is meant to move off the figure: a cape or an outer robe (40 and up) all of
						// it, an inner skirt (25) a quarter, a tabard (19) a tenth, a tunic (13) next to none.
						// Without that Vader's inner robe flew out behind him like a second cape and bared his legs.
						const float tk = g->tether[p] * (1.0f / MC_AIR_TETHER);
						const float un = mcDot(airH, nn);
						// SJE r_meshClothWornAir: ... but cloth worn round the body (a robe, a skirt: ComputeWrap) at most a
						// quarter, whatever its tether: its wearer stands inside it, in the wind's way, and no air gets in
						// from the sides. With all of it the back of a long robe was blown out from the inside at a run:
						// its lower half flew up level as one flat sheet, kinked along a line from the hip. The same for a
						// loose layer the model has under such cloth (tunic strips under a robe): blown back at full
						// strength they pushed the robe out from the inside. A cape hangs open behind its wearer and
						// keeps all of it.
						float ak = tk >= 1.0f ? 1.0f : tk * tk * tk;
						if (wornCap && (g->wrap[p] || g->under[p]) && ak > prm->wornAir) ak = prm->wornAir;
						mcMA(v, -prm->dragQuad * fabsf(un) * un * invW * ak * (ripple ? rip[p] : 1.0f), nn, v);
					}
					if (breezeK > 0.0f) {
						// SJE: the breeze through the cloth, as the wind's normal drag
						const float* b = &breezeV[(size_t)p * 2];
						mcMA(v, kn * h * (b[0] * nn[0] + b[1] * nn[1]), nn, v);
					}
					if (billowQ > 0.0f) {
						// SJE: the cape fills out: outwards along its normal, most a third of the way down and fading out to
						// nothing at the hem, so the lower cloth hangs from the bulge (pushed most half way down, the hem flew
						// up further than the middle: a flat lifted sheet, no bulge)
						const float sl = g->rootS[p], prof = 6.75f * sl * (1.0f - sl) * (1.0f - sl);
						if (prof > 0.0f) {
							vec3_t ro;
							mcSub(pp, in->lastOrigin, ro);
							mcMA(ro, -mcDot(ro, in->up), in->up, ro);
							const float rl = mcLen(ro);
							if (rl > 1e-3f) {
								float lee = 0.5f * (1.0f - mcDot(ro, billowDir) / rl);
								lee *= lee;	// 1 behind the body, a quarter beside it, 0 in front
								const float tk = g->tether[p] * (1.0f / MC_AIR_TETHER);
								// by how squarely the cloth faces outwards, squared: cloth that hangs gets all of it, cloth that
								// already streams out level next to none (no shelf of cloth held up at hip height)
								const float out = mcDot(nn, ro) / rl;
								float ak = tk >= 1.0f ? 1.0f : tk * tk * tk;
								if (wornCap && (g->wrap[p] || g->under[p]) && ak > prm->wornAir) ak = prm->wornAir;	// SJE r_meshClothWornAir
								mcMA(v, billowQ * prof * lee * out * fabsf(out) * invW * ak, nn, v);
							}
						}
					}
				}
				mcCopy(pp, pv);
				mcAdd(pp, v, pp);
				if (underWater) {
					pp[2] += prm->gravity * 0.15f * h * h;	// buoyant: drifts slowly up instead of falling
				}
				else {
					pp[2] -= prm->gravity * h * h;
				}
				// sideways-only pull toward the animated shape: stops a cape
				// rolling up into a rope in saber spins, while it still trails,
				// swings and lifts freely front to back and up and down
				const float sp = in->kSpread[p];
				if (sp > 0.0f) {
					const float k = sp;
					vec3_t d;
					mcSub(&in->target[p * 3], pp, d);
					mcMA(pp, k * mcDot(d, in->side), in->side, pp);
				}
				// optional pull toward the modelled shape (off unless configured)
				const float sh = in->kShape[p];
				if (sh > 0.0f) {
					const float k = sh;
					vec3_t d;
					mcSub(&in->target[p * 3], pp, d);
					mcMA(pp, k, d, pp);
				}
			}
			const mcCapsule_t* cs = caps;
			if (capsFrom) {
				for (int c = 0; c < ncaps; c++) {
					capsSub[c] = caps[c];
					for (int k = 0; k < 3; k++) {
						capsSub[c].a[k] = capsFrom[c].a[k] + (caps[c].a[k] - capsFrom[c].a[k]) * t1;
						capsSub[c].b[k] = capsFrom[c].b[k] + (caps[c].b[k] - capsFrom[c].b[k]) * t1;
					}
				}
				cs = capsSub;
			}
			solve.faceNow = (s & 1) || s == sub - 1;
			SolveConstraints(g, in, cs, ncaps, solve);
			if (selfSub) SolveSelfPairs(g, in, prm->frictionScale, prm->calm != 0, MC_FIGHT_TRIP);
			if (prm->layers && !g->layerPairs.empty()) SolveLayerPairs(g, in, prm);	// SJE: cloth layers keep their order
			FloorCollide(g, in, prm);
			ContactVelocity(g, in, prm->frictionScale);
		}
		in->impulse.clear();
		if (!selfSub) SelfCollide(g, in);
		FightUpdate(g, in);	// SJE: watchdog against shaking
	}

	// SJE: sleeping cloth (r_meshClothSleep 1). A cape on a character standing still settles and then keeps
	// re-solving the same rest shape every step. Once it has been at rest for MC_REST_STEPS steps (no free
	// point moving, the pose and the body capsules staying put, no wind) it stops simulating and is drawn
	// where it came to rest. Any change wakes it: the pose or a capsule moving MC_REST_DRIFT, wind, or a
	// blast or force push.
	static void CapsSignature(const mcCapsule_t* caps, int ncaps, float* sig) {
		for (int k = 0; k < 8; k++) sig[k] = 0.0f;
		for (int c = 0; c < ncaps; c++) {
			for (int k = 0; k < 3; k++) { sig[k] += caps[c].a[k]; sig[3 + k] += caps[c].b[k]; }
			sig[6] += caps[c].r;
		}
		sig[7] = (float)ncaps;
	}
	static bool RestPoseHolds(const mcInstance_t* in, const std::vector<float>& target, const float* capSig, const mcParams_t* prm) {
		if (mcDot(prm->wind, prm->wind) > 1e-4f) return false;
		if (in->restRef.size() != target.size()) return false;
		for (int k = 0; k < 8; k++) if (fabsf(capSig[k] - in->restCaps[k]) > MC_REST_DRIFT) return false;
		const float d2 = MC_REST_DRIFT * MC_REST_DRIFT;
		for (size_t i = 0; i + 2 < target.size(); i += 3) {
			vec3_t d;
			mcSub(&target[i], &in->restRef[i], d);
			if (mcDot(d, d) > d2) return false;
		}
		return true;
	}
	static void TrackRest(const mcGroup_t* g, mcInstance_t* in, const std::vector<float>& target, const float* capSig,
		const mcParams_t* prm, int steps) {
		bool still = RestPoseHolds(in, target, capSig, prm) && in->pCurStep.size() == in->p.size() && in->pPrevStep.size() == in->p.size();
		if (still) {
			// SJE r_meshClothBreeze: in the breeze the cloth is only at rest if the breeze does not move it either
			const float lim = BreezeNow(prm) > 0.0f && in->breezeTaut < 1.0f ? MC_BREEZE_STILL : MC_REST_SPEED;
			const float s2 = lim * lim;
			// SJE r_meshClothCalm: a point that stays in one place is at rest too, however it trembles there.
			// Cloth pressed on a held body can keep a few points ringing by a few tenths of a unit for good (links
			// against a contact); by its speed alone such cloth never slept, was simulated for nothing and shimmered.
			// 'In one place': all the point did since the count began fits in a box of MC_REST_SPAN. A point that
			// really travels outgrows the box within the count and starts it anew, so cloth that slides or settles is
			// not frozen any sooner than before. Not in the breeze: there the cloth is meant to move.
			const bool place = prm->calm && lim == MC_REST_SPEED && in->restLo.size() == in->p.size() && in->restHi.size() == in->p.size();
			const float span2 = MC_REST_SPAN * MC_REST_SPAN * in->linkScale * in->linkScale;
			for (int p = 0; p < g->numParticles && still; p++) {
				if (g->freedom[p] <= 0.0f) continue;
				const float* c = &in->pCurStep[p * 3];
				if (place) {
					float* lo = &in->restLo[p * 3], * hi = &in->restHi[p * 3];
					for (int k = 0; k < 3; k++) { if (c[k] < lo[k]) lo[k] = c[k]; if (c[k] > hi[k]) hi[k] = c[k]; }
				}
				vec3_t d;
				mcSub(c, &in->pPrevStep[p * 3], d);
				if (mcDot(d, d) > s2) {
					if (place) {
						vec3_t e;
						mcSub(&in->restHi[p * 3], &in->restLo[p * 3], e);
						if (mcDot(e, e) <= span2) continue;
					}
					still = false;
				}
			}
		}
		if (!still) {
			in->restLo.assign(in->pCurStep.begin(), in->pCurStep.end());
			in->restHi.assign(in->pCurStep.begin(), in->pCurStep.end());
			in->restSteps = 0;
			in->restRef.assign(target.begin(), target.end());
			for (int k = 0; k < 8; k++) in->restCaps[k] = capSig[k];
			return;
		}
		in->restSteps += steps;
		if (in->restSteps < MC_REST_STEPS) return;
		in->asleep = true;
		// drawn from the last step alone while asleep (no blend between two slightly different steps)
		in->pPrevStep = in->pCurStep;
		in->offPrev = in->offCur;
		mcCopy(in->oCurStep, in->oPrevStep);
	}

	// SJE r_meshClothResume. A character that leaves the view is culled, and its cloth is not simulated. When it came
	// back after more than MC_STALE_MS the cloth started over: reset to the pose and settled with MC_SETTLE_STEPS full
	// steps in that one frame - about 30 ms for a cape like Vader's, and a frame of 40-60 ms when several capes came
	// back at once (a turn of the camera in a fight). With this on, cloth that was simulated up to resumeMs ago is
	// carried on instead: its points are moved with the attached edge (its travel, and its turn about up) from where
	// they were, keeping their speed against it, and the simulation goes on from there with one step.
	// Only where the character is in much the same pose as then (MC_RESUME_POSE): carried into another pose (it
	// crouched meanwhile) the cloth came to lie inside the body and stayed caught there; that case settles as before.
	const float MC_RESUME_POSE = 4.0f;	// units the animated cloth points may differ by, the edge's travel and turn taken out
	static bool Resume(const mcGroup_t* g, mcInstance_t* in, const std::vector<float>& target, int timeMs, const vec3_t origin, const vec3_t axis[3]) {
		const size_t n3 = (size_t)g->numParticles * 3;
		if (in->p.size() != n3 || in->prev.size() != n3 || in->targetPrev.size() != n3 || target.size() != n3) return false;
		vec3_t up;
		mcCopy(axis[2], up);
		if (mcNorm(up) < 1e-6f) return false;
		// the attached edge then and now: its centre, and its turn about up
		vec3_t c0 = { 0, 0, 0 }, c1 = { 0, 0, 0 };
		int na = 0;
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] > 0.0f) continue;
			mcAdd(c0, &in->targetPrev[p * 3], c0);
			mcAdd(c1, &target[p * 3], c1);
			na++;
		}
		if (na < 1) return false;
		mcScale(c0, 1.0f / na, c0);
		mcScale(c1, 1.0f / na, c1);
		float sx = 0.0f, sy = 0.0f;
		for (int p = 0; p < g->numParticles && na >= 3; p++) {
			if (g->freedom[p] > 0.0f) continue;
			vec3_t a, b, x;
			mcSub(&in->targetPrev[p * 3], c0, a);
			mcSub(&target[p * 3], c1, b);
			mcMA(a, -mcDot(a, up), up, a);
			mcMA(b, -mcDot(b, up), up, b);
			mcCross(a, b, x);
			sx += mcDot(x, up);
			sy += mcDot(a, b);
		}
		const float ang = (sx != 0.0f || sy != 0.0f) ? atan2f(sx, sy) : 0.0f;
		const float cs = cosf(ang), sn = sinf(ang);
		float worst = 0.0f;
		for (int p = 0; p < g->numParticles; p++) {
			vec3_t r, par, perp, side, w;
			mcSub(&in->targetPrev[p * 3], c0, r);
			mcScale(up, mcDot(r, up), par);
			mcSub(r, par, perp);
			mcCross(up, perp, side);
			for (int k = 0; k < 3; k++) w[k] = c1[k] + par[k] + perp[k] * cs + side[k] * sn - target[p * 3 + k];
			const float d2 = mcDot(w, w);
			if (d2 > worst) worst = d2;
		}
		if (worst > MC_RESUME_POSE * MC_RESUME_POSE) {
			return false;
		}
		for (int p = 0; p < g->numParticles; p++) {
			float* pts[2] = { &in->p[p * 3], &in->prev[p * 3] };
			for (float* q : pts) {
				if (g->freedom[p] <= 0.0f) { mcCopy(&target[p * 3], q); continue; }
				vec3_t r, par, perp, side;
				mcSub(q, c0, r);
				mcScale(up, mcDot(r, up), par);
				mcSub(r, par, perp);
				mcCross(up, perp, side);
				for (int k = 0; k < 3; k++) q[k] = c1[k] + par[k] + perp[k] * cs + side[k] * sn;
			}
		}
		// the clocks and references as if the last step had been one step ago, in this pose
		in->targetPrev.assign(target.begin(), target.end());
		in->simTarget.assign(target.begin(), target.end());
		in->accum = 0;
		in->lastTime = timeMs - MC_STEP_MS;
		mcCopy(origin, in->lastOrigin);
		in->originVel[0] = in->originVel[1] = in->originVel[2] = 0.0f;
		in->originTime = timeMs;
		in->bodyRelOk = false;
		in->pinPrev.clear();
		in->pinTime = timeMs;
		in->twistRun = 0.0f;
		mcCopy(up, in->up);
		in->asleep = false;
		in->restSteps = 0;
		in->restRef.assign(target.begin(), target.end());
		in->capsPrev.clear();	// no sweep from where the body was then
		in->fight.clear();
		in->pPrevStep = in->p;
		in->pCurStep = in->p;
		mcCopy(origin, in->oPrevStep);
		mcCopy(origin, in->oCurStep);
		in->offCur.resize(n3);
		for (size_t i = 0; i < n3; i++) in->offCur[i] = in->p[i] - target[i];
		in->offPrev = in->offCur;
		return true;
	}

	void Simulate(const mcGroup_t* g, mcInstance_t* in, int timeMs, const vec3_t origin, const vec3_t axis[3],
		const mcCapsule_t* caps, int ncaps, const mcParams_t* prm, mcBoneFn_t boneFn, void* boneCtx) {
		ComputeRootDist(g);
		// animated (target) positions of every particle, and skinned normals of every vertex
		std::vector<float>& target = in->scratchTarget;
		std::vector<float>& drawTarget = in->drawTarget;
		target.resize((size_t)g->numParticles * 3);
		drawTarget.resize((size_t)g->numDraw * 3);
		in->skinNrm.resize((size_t)g->numVerts * 3);
		std::vector<char>& done = in->scratchDone;
		done.assign(g->numDraw, 0);
		for (size_t si = 0; si < g->surfs.size(); si++) {
			const mdxmSurface_t* s = g->surfs[si];
			for (int i = 0; i < s->numVerts; i++) {
				const int gv = g->vertBase[si] + i, d = g->vertToDraw[gv];
				vec3_t pos;
				SkinVertex(s, i, boneFn, boneCtx, pos, &in->skinNrm[gv * 3]);
				if (!done[d]) { ToWorld(pos, origin, axis, &drawTarget[d * 3]); done[d] = 1; }
			}
		}
		for (int p = 0; p < g->numParticles; p++) mcCopy(&drawTarget[g->seedDraw[p] * 3], &target[p * 3]);

		if (in->active) {
			const int dt = timeMs - in->lastTime;
			vec3_t d;
			mcSub(&target[0], &in->targetPrev[0], d);
			if (prm->resumeMs > 0 && dt > MC_STALE_MS && dt <= prm->resumeMs && Resume(g, in, target, timeMs, origin, axis)) {
			}
			else if (dt < 0 || dt > MC_STALE_MS || mcLen(d) > MC_TELEPORT) in->active = false;
		}
		// the capsule lists live in the instance (16 KB on the stack was too much: C6262)
		in->scratchCaps.resize((size_t)MC_NEAR_CAPS * 4 + MC_MAX_CAPS);
		mcCapsule_t* const nearCaps = in->scratchCaps.data();
		mcCapsule_t* const nearPrev = nearCaps + MC_NEAR_CAPS;
		mcCapsule_t* const stepFrom = nearPrev + MC_NEAR_CAPS;
		mcCapsule_t* const stepTo = stepFrom + MC_NEAR_CAPS;
		mcCapsule_t* const useCaps = stepTo + MC_NEAR_CAPS; // MC_MAX_CAPS
		int nuse = 0;
		for (int c = 0; c < ncaps && nuse < MC_MAX_CAPS; c++) {
			if (caps[c].hard == 2 && !g->proxied) continue;
			useCaps[nuse] = caps[c];
			nuse++;
		}
		// last frame's capsules, if they line up with this frame's (same set of bones): swept collision
		const bool sweep = in->active && (int)in->capsPrev.size() == nuse;
		int nnear = 0;
		for (int c = 0; c < nuse && nnear < MC_NEAR_CAPS; c++) {
			// keep a capsule if the cloth is near where it is now or where it was last frame
			bool keep = CullCapsules(g, target, &useCaps[c], 1, &nearCaps[nnear], 1) == 1;
			if (!keep && sweep) keep = CullCapsules(g, target, &in->capsPrev[c], 1, &nearCaps[nnear], 1) == 1;
			if (!keep) continue;
			nearCaps[nnear] = useCaps[c];
			nearPrev[nnear] = sweep ? in->capsPrev[c] : useCaps[c];
			nnear++;
		}
		in->capsPrev.assign(useCaps, useCaps + nuse);
		auto capsAt = [&](float tt, mcCapsule_t* o) {
			for (int c = 0; c < nnear; c++) {
				o[c] = nearCaps[c];
				for (int k = 0; k < 3; k++) {
					o[c].a[k] = nearPrev[c].a[k] + (nearCaps[c].a[k] - nearPrev[c].a[k]) * tt;
					o[c].b[k] = nearPrev[c].b[k] + (nearCaps[c].b[k] - nearPrev[c].b[k]) * tt;
				}
			}
			};
		// SJE: sleeping cloth (r_meshClothSleep)
		float capSig[8];
		CapsSignature(nearCaps, nnear, capSig);
		// SJE r_meshClothBreeze: cloth that fell asleep out of the breeze wakes when the breeze reaches it - unless it
		// was held stretched when last looked at (the breeze does not act on it)
		const bool breezeHere = BreezeNow(prm) > 0.0f;
		const bool breezeCame = breezeHere && !in->breezeHere && in->breezeTaut < 1.0f;
		in->breezeHere = breezeHere;
		bool sleeping = false;
		if (in->active && in->asleep) {
			sleeping = prm->sleep && in->impulse.empty() && !breezeCame && RestPoseHolds(in, target, capSig, prm);
			if (!sleeping) {
				in->asleep = false;
				in->restSteps = 0;
				in->restRef.assign(target.begin(), target.end());
				for (int k = 0; k < 8; k++) in->restCaps[k] = capSig[k];
			}
		}
		// SJE r_meshClothSettle. Cloth that starts over (first seen, back in view after MC_STALE_MS, back inside the
		// far distance, a teleport) settled with MC_SETTLE_STEPS full steps in that one frame: 30 ms for a cape like
		// Vader's, 80-160 ms for a robe, on one thread - a stutter whenever a caped character came into view, several
		// frames in a row when a group did. With this on, the same steps are spread over the following frames:
		// prm->settle per 16 ms for a cloth of MC_SETTLE_POINTS points (heavier cloth fewer), shared out when more
		// cloth settles at once than the workers take without the frame growing. Meanwhile the cloth is drawn in its
		// animated pose - the settled shape is within 1-3 units of it on average - and then eased into the simulation
		// over MC_SHOW_STEPS. (Simulating it in view from the pose instead shows the swing the settling was there to
		// hide; steps with half the substeps still cost three quarters of a whole one, so they would not shorten the wait.)
		// SJE r_meshClothSettleCut: not in the frame of a camera cut (in->settleNow, set in MC_Prepare). There the cloth
		// settles at once, as before: nobody sees the frame of a cut take longer, but in a cutscene every cut showed
		// the capes in their modelled pose for a fifth of a second and then widening into their hanging shape.
		const bool spread = prm->settle > 0.0f && !in->settleNow;
		bool begun = false;
		if (!in->active && spread) {
			in->target.assign(target.begin(), target.end());
			Reset(in, timeMs);
			mcCopy(origin, in->lastOrigin);
			in->bodyRelOk = false;
			in->originTime = timeMs;
			mcCopy(axis[2], in->up);
			mcNorm(in->up);
			in->settleLeft = MC_SETTLE_STEPS;
			in->settleCredit = 0.0f;
			in->showSteps = 0;
			in->offCur.assign(target.size(), 0.0f);
			in->offPrev = in->offCur;
			in->pPrevStep = in->p;
			in->pCurStep = in->p;
			mcCopy(origin, in->oPrevStep);
			mcCopy(origin, in->oCurStep);
			in->settleCaps.assign(useCaps, useCaps + nuse);
			in->settleTravel[0] = in->settleTravel[1] = in->settleTravel[2] = 0.0f;
			begun = true;
		}
		if (in->active && in->settleLeft > 0) {
			mcCopy(axis[2], in->up);
			mcNorm(in->up);
			const int sdt = begun ? MC_STEP_MS : timeMs - in->lastTime;
			if (!begun) {
				// it settles in the character's own frame: carried along with all of the character's travel
				vec3_t dO;
				mcSub(origin, in->lastOrigin, dO);
				if (mcDot(dO, dO) > 0.0f) {
					for (int p = 0; p < g->numParticles; p++) {
						mcAdd(&in->targetPrev[p * 3], dO, &in->targetPrev[p * 3]);
						if (g->freedom[p] <= 0.0f) continue;
						mcAdd(&in->p[p * 3], dO, &in->p[p * 3]);
						mcAdd(&in->prev[p * 3], dO, &in->prev[p * 3]);
					}
					mcAdd(in->settleTravel, dO, in->settleTravel);
				}
				mcCopy(origin, in->lastOrigin);
			}
			UpdateSide(g, in, target);
			int steps = in->settleLeft;	// (switched off meanwhile: the rest at once)
			if (spread) {
				// (the workers share memory and cores: a crowd's steps take about a third of the workers' count at once)
				const int cnt = g_settleCount > g_settleCountLast ? g_settleCount : g_settleCountLast;
				const float room = ((prm->settleJobs > 1 ? prm->settleJobs : 1) + 2) / 3.0f;
				const float share = cnt > room ? room / cnt : 1.0f;
				const float size = g->numParticles / MC_SETTLE_POINTS;
				const float cost = size > 0.25f ? size * sqrtf(size) : 0.125f;
				const float ms = sdt < 0 ? 0.0f : (sdt > 4 * MC_STEP_MS ? 4.0f * MC_STEP_MS : (float)sdt);
				in->settleCredit += prm->settle * share * (ms / MC_STEP_MS) / cost;
				steps = (int)in->settleCredit;
				if (steps > in->settleLeft) steps = in->settleLeft;
				in->settleCredit -= steps;
			}
			for (int s = 0; s < steps; s++) {
				if (s == 0 && !begun) {
					// the pose has moved on since the last step: the attached edge is taken there through this step, and
					// the body's shapes are swept from where they were then (carried with the travel, as the cloth was)
					const bool swept = (int)in->settleCaps.size() == nuse;
					for (int k = 0; k < nnear; k++) {
						stepFrom[k] = nearCaps[k];
						for (int c = 0; swept && c < nuse; c++) {
							if (memcmp(&useCaps[c], &nearCaps[k], sizeof(mcCapsule_t)) != 0) continue;
							stepFrom[k] = in->settleCaps[c];
							mcAdd(stepFrom[k].a, in->settleTravel, stepFrom[k].a);
							mcAdd(stepFrom[k].b, in->settleTravel, stepFrom[k].b);
							break;
						}
					}
					in->scratchFrom.assign(in->targetPrev.begin(), in->targetPrev.end());
					Step(g, in, in->scratchFrom, target, nearCaps, nnear, prm, stepFrom);
				}
				else Step(g, in, target, target, nearCaps, nnear, prm);
			}
			if (steps > 0) {
				in->targetPrev.assign(target.begin(), target.end());
				in->simTarget.assign(target.begin(), target.end());
				in->settleCaps.assign(useCaps, useCaps + nuse);
				in->settleTravel[0] = in->settleTravel[1] = in->settleTravel[2] = 0.0f;
			}
			else in->target.assign(target.begin(), target.end());
			in->settleLeft -= steps;
			in->lastTime = timeMs;
			in->accum = 0;
			in->originVel[0] = in->originVel[1] = in->originVel[2] = 0.0f;
			in->originTime = timeMs;
			in->bodyRelOk = false;
			in->pinPrev.clear();
			in->pinTime = timeMs;
			in->twistRun = 0.0f;
			if (in->settleLeft <= 0) {
				// settled: at rest, as after the settling of old; shown from here on
				in->settleLeft = 0;
				in->showSteps = in->settleNow ? MC_SHOW_STEPS : 0;	// (a camera cut meanwhile: shown at once)
				in->prev.assign(in->p.begin(), in->p.end());
				in->offCur.resize(target.size());
				for (size_t i = 0; i < target.size(); i++) in->offCur[i] = in->p[i] - target[i];
				in->offPrev = in->offCur;
				in->pPrevStep = in->p;
				in->pCurStep = in->p;
				mcCopy(origin, in->oPrevStep);
				mcCopy(origin, in->oCurStep);
				in->restRef.assign(target.begin(), target.end());
				for (int k = 0; k < 8; k++) in->restCaps[k] = capSig[k];
				in->restSteps = 0;
			}
		}
		else if (!in->active) {
			in->settleLeft = 0;
			in->showSteps = MC_SHOW_STEPS;
			in->target.assign(target.begin(), target.end());
			Reset(in, timeMs);
			mcCopy(origin, in->lastOrigin);
			in->bodyRelOk = false;
			in->originTime = timeMs;
			mcCopy(axis[2], in->up);
			mcNorm(in->up);

			UpdateSide(g, in, target);
			// let the cloth fall into its hanging shape before it is first seen,
			// instead of starting in the modelled (often flared) pose
			for (int k = 0; k < MC_SETTLE_STEPS; k++) Step(g, in, target, target, nearCaps, nnear, prm);
			in->prev.assign(in->p.begin(), in->p.end());
			in->offCur.resize(target.size());
			for (size_t i = 0; i < target.size(); i++) in->offCur[i] = in->p[i] - target[i];
			in->offPrev = in->offCur;
			in->pPrevStep = in->p;
			in->pCurStep = in->p;
			mcCopy(origin, in->oPrevStep);
			mcCopy(origin, in->oCurStep);
			in->restRef.assign(target.begin(), target.end());
			for (int k = 0; k < 8; k++) in->restCaps[k] = capSig[k];
		}
		else if (sleeping) {
			// SJE: asleep - no steps; keep every clock and reference current so waking up carries on smoothly
			mcCopy(axis[2], in->up);
			mcNorm(in->up);
			mcCopy(origin, in->lastOrigin);
			in->originTime = timeMs;
			in->originVel[0] = in->originVel[1] = in->originVel[2] = 0.0f;
			in->pinPrev.assign(target.begin(), target.end());
			in->pinTime = timeMs;
			in->targetPrev.assign(target.begin(), target.end());
			in->simTarget.assign(target.begin(), target.end());
			in->target.assign(target.begin(), target.end());
			in->lastTime = timeMs;
			in->accum = 0;
		}
		else {
			mcCopy(axis[2], in->up);
			mcNorm(in->up);

			// inertia scale (as in game cloth tools): only part of the
			// character's travel through the world reaches the cloth, so a
			// running character's robe trails instead of being left behind
			vec3_t dO;
			mcSub(origin, in->lastOrigin, dO);
			const int odt = timeMs - in->originTime;
			// SJE: the body's own travel inside its animation. Animations move the body against the character's
			// origin: the jump animations lift it while it is in the air (41 units in a jump, 65 in a force jump)
			// and bring it down again in a few frames on landing, at 500-800 units/s - out of a run, 32 units in
			// one frame. That is no travel through the world (the origin's is, and the cloth feels that below):
			// it tore the attached edge down from under the cloth, so cape and robe flew up to their lift limit on
			// every landing (a flat sheet spread at shoulder height, the legs bare) and crashed back. Where the
			// attached edge as a whole moves fast against the origin, the cloth is carried along with it; slow
			// moves (a stride's bob, a lean, a saber swing) it feels as before.
			vec3_t bodyShift = { 0.0f, 0.0f, 0.0f };
			{
				vec3_t c = { 0.0f, 0.0f, 0.0f };
				int np = 0;
				for (int p = 0; p < g->numParticles; p++) {
					if (g->freedom[p] > 0.0f) continue;
					mcAdd(c, &target[p * 3], c);
					np++;
				}
				if (np > 0) {
					mcScale(c, 1.0f / np, c);
					mcSub(c, origin, c);
					if (in->bodyRelOk && odt > 0 && mcDot(dO, dO) < MC_TELEPORT * MC_TELEPORT) {
						vec3_t dC;
						mcSub(c, in->bodyRel, dC);
						const float v = mcLen(dC) / (odt * 0.001f), v0 = MC_BODY_SLOW * in->linkScale, v1 = MC_BODY_FAST * in->linkScale;
						float k = v1 > v0 ? (v - v0) / (v1 - v0) : 0.0f;
						k = k <= 0.0f ? 0.0f : (k >= 1.0f ? 1.0f : k * k * (3.0f - 2.0f * k));
						if (!(prm->bodyCarry > 0.0f)) k = 0.0f;
						mcScale(dC, k, bodyShift);
					}
					mcCopy(c, in->bodyRel);
					in->bodyRelOk = true;
				}
			}
			if (mcDot(dO, dO) < MC_TELEPORT * MC_TELEPORT && odt > 0) {
				const float dt = odt * 0.001f;
				// max acceleration (Battlefront II's cloth "MaxAcceleration"):
				// the cloth feels the character's speeding up, stopping and
				// turning only up to a limit; any harder jolt carries it along
				// instead of whipping it
				vec3_t vNew, acc, excess = { 0, 0, 0 };
				mcScale(dO, 1.0f / dt, vNew);
				// game frame times are uneven (whole ms, varying frame rate), so a
				// per-frame velocity is noisy and its derivative explodes into
				// thousands of units/s^2 even at a steady run. Smooth the velocity
				// over ~50 ms first, so only real starts, stops and jumps count.
				{
					const float k = 1.0f - expf(-dt / 0.05f);
					vec3_t dv;
					mcSub(vNew, in->originVel, dv);
					mcMA(in->originVel, k, dv, vNew);
				}
				mcSub(vNew, in->originVel, acc);
				mcScale(acc, 1.0f / dt, acc);
				const float aLen = mcLen(acc);
				// SJE r_meshClothCarryFloor: the body carry takes cloth down only as far as there is room above the floor.
				// The carry was made for landings, where the cloth is in the air with its wearer. Dropping into a crouch
				// moves the attached edge down just as fast (20 units in a few frames), and cloth that already hangs to
				// the ground - a long robe, tunic strips - was shifted that far INTO the floor; put back up in one step,
				// its lower half was thrown up and outwards and came down spread flat on the ground, a good way from its
				// wearer. (Limiting each point by its own room squeezed the cloth between its carried top and its hem:
				// thrown just the same.) So a whole sheet - all cloth joined by links - is carried down by no more than
				// its lowest free point has room; what is left of the drop reaches it through its attached edge, as any
				// slow move does, and it settles onto the ground under its wearer.
				// Only cloth worn round the body and loose layers under it (ComputeWrap, g->under: robes, skirts, the
				// strips under them): it hangs to the ground between its wearer's feet, where the legs come down into it.
				// A cape hangs free behind its wearer and keeps the whole carry (limited like this too, a cape came to
				// rest sooner in a crouch, but lay into the skirt under it more often and stretched more in falls).
				thread_local static std::vector<float> sheetRoom;
				thread_local static std::vector<int> sheetCnt;
				const float carryDn = mcDot(bodyShift, prm->floorN);
				const bool carryLim = prm->carryFloor && prm->floor && carryDn < 0.0f && g->numSheets > 0 && (int)g->sheetOf.size() == g->numParticles;
				if (carryLim) {
					const bool cls = (int)g->wrap.size() == g->numParticles && (int)g->under.size() == g->numParticles;
					sheetRoom.assign((size_t)g->numSheets, 1e9f);
					sheetCnt.assign((size_t)g->numSheets * 2, 0);
					for (int p = 0; p < g->numParticles; p++) {
						if (g->freedom[p] <= 0.0f) continue;
						vec3_t fr;
						mcSub(&in->p[p * 3], prm->floorPt, fr);
						const float room = mcDot(fr, prm->floorN) - MC_FLOOR_GAP;
						const int sh = g->sheetOf[p];
						if (room < sheetRoom[sh]) sheetRoom[sh] = room;
						sheetCnt[(size_t)sh * 2]++;
						if (cls && (g->wrap[p] || g->under[p])) sheetCnt[(size_t)sh * 2 + 1]++;
					}
					for (int sh = 0; sh < g->numSheets; sh++) {
						if (sheetRoom[sh] < 0.0f) sheetRoom[sh] = 0.0f;
						if (sheetCnt[(size_t)sh * 2 + 1] * 2 < sheetCnt[(size_t)sh * 2]) sheetRoom[sh] = 1e9f;	// not worn: no limit
					}
				}
				for (int p = 0; p < g->numParticles; p++) {
					vec3_t shift;
					const float carry = 1.0f - Clamp01(g->inertia[p] >= 0.0f ? g->inertia[p] : prm->inertia);
					mcScale(dO, carry, shift);
					const float amax = g->maxAccel[p] >= 0.0f ? g->maxAccel[p] : prm->maxAccel;
					if (amax > 0.0f && aLen > amax) {
						// the part of this frame's move that comes from acceleration over the limit
						mcScale(acc, (1.0f - amax / aLen) * dt * dt * (1.0f - carry), excess);
						mcAdd(shift, excess, shift);
					}
					mcAdd(shift, bodyShift, shift);	// SJE: carried along with the body's own fast travel
					if (mcDot(shift, shift) == 0.0f) continue;
					// the attached edge is fed in substep by substep from last
					// frame's animated position: move that reference by the same
					// amount, or a jump take-off puts the cloth ahead of its own
					// attachment for a moment and yanks it
					mcAdd(&in->targetPrev[p * 3], shift, &in->targetPrev[p * 3]);
					if (in->simTarget.size() == in->targetPrev.size()) mcAdd(&in->simTarget[p * 3], shift, &in->simTarget[p * 3]);
					if (g->freedom[p] <= 0.0f) continue;
					vec3_t shiftP;
					mcCopy(shift, shiftP);
					if (carryLim) {	// SJE r_meshClothCarryFloor (see above the loop)
						const float room = sheetRoom[g->sheetOf[p]];
						if (-carryDn > room) mcMA(shiftP, -(carryDn + room), prm->floorN, shiftP);
					}
					mcAdd(&in->p[p * 3], shiftP, &in->p[p * 3]);
					if (in->pCurStep.size() == in->p.size()) {
						mcAdd(&in->pCurStep[p * 3], shiftP, &in->pCurStep[p * 3]);
						mcAdd(&in->pPrevStep[p * 3], shiftP, &in->pPrevStep[p * 3]);
					}
					if (in->restLo.size() == in->p.size() && in->restHi.size() == in->p.size()) {
						mcAdd(&in->restLo[p * 3], shiftP, &in->restLo[p * 3]);
						mcAdd(&in->restHi[p * 3], shiftP, &in->restHi[p * 3]);
					}
					mcAdd(&in->prev[p * 3], shiftP, &in->prev[p * 3]);
				}
				mcCopy(vNew, in->originVel);
				in->originTime = timeMs;
			}
			else if (odt != 0) {
				in->originVel[0] = in->originVel[1] = in->originVel[2] = 0.0f;
				in->originTime = timeMs;
			}
			vec3_t originFrom;
			mcCopy(in->lastOrigin, originFrom);
			mcCopy(origin, in->lastOrigin);
			SpinLimit(g, in, target, timeMs, MC_DEFAULT_SPIN, prm);
			UpdateSide(g, in, target);
			const int dt = timeMs - in->lastTime;
			int steps = 0;
			if (dt > 0) {
				const int accumOld = in->accum;
				in->accum += dt;
				steps = in->accum / MC_STEP_MS;
				bool clamped = false;
				if (steps > MC_MAX_STEPS) { steps = MC_MAX_STEPS; in->accum = 0; clamped = true; }
				else in->accum -= steps * MC_STEP_MS;
				std::vector<float>& from = in->scratchFrom, & to = in->scratchTo;
				if (in->simTarget.size() != target.size()) in->simTarget.assign(in->targetPrev.begin(), in->targetPrev.end());
				to.assign(in->simTarget.begin(), in->simTarget.end());
				float tPrev = 0.0f;
				for (int s = 0; s < steps; s++) {
					// each step ends at a fixed 16 ms tick of the simulation clock;
					// the animated pose is sampled at that exact time, so the cloth
					// sees the body's true speed whatever the frame timing
					float t1 = clamped ? (float)(s + 1) / steps : (float)(MC_STEP_MS * (s + 1) - accumOld) / dt;
					if (t1 > 1.0f) t1 = 1.0f; else if (t1 < 0.0f) t1 = 0.0f;
					from.swap(to);
					to.resize(target.size());
					for (size_t i = 0; i < target.size(); i++) to[i] = in->targetPrev[i] + (target[i] - in->targetPrev[i]) * t1;
					capsAt(tPrev, stepFrom);
					capsAt(t1, stepTo);
					Step(g, in, from, to, stepTo, nnear, prm, sweep ? stepFrom : nullptr);
					tPrev = t1;
					in->offPrev.swap(in->offCur);
					in->offCur.resize(to.size());
					for (size_t i = 0; i < to.size(); i++) in->offCur[i] = in->p[i] - to[i];
					in->pPrevStep.swap(in->pCurStep);
					in->pCurStep.assign(in->p.begin(), in->p.end());
					mcCopy(in->oCurStep, in->oPrevStep);
					for (int k = 0; k < 3; k++) in->oCurStep[k] = originFrom[k] + (origin[k] - originFrom[k]) * t1;
				}
				// without a step the simulation clock stays put and keeps its pose sample
				if (steps > 0) in->simTarget.assign(to.begin(), to.end());
				if (in->showSteps < MC_SHOW_STEPS) in->showSteps += steps;	// SJE r_meshClothSettle
				in->targetPrev.assign(target.begin(), target.end());
				in->lastTime = timeMs;
				// SJE: r_meshClothSleep - count the steps the cloth has been at rest
				if (!prm->sleep) in->restSteps = 0;
				else if (steps > 0) TrackRest(g, in, target, capSig, prm, steps);
			}
			// frames without a full step still follow the current pose, so a
			// fast limb never ends up inside stale cloth
			if (steps == 0) in->target.assign(target.begin(), target.end());
		}

		// what gets drawn: the simulated positions with small creases smoothed
		// out of their offset from the animation (visual only, the simulation
		// keeps its state). A coarse game mesh can't show folds smaller than a
		// couple of triangles anyway; they only read as crumpling.
		// The simulation runs in fixed 16 ms steps but frames come every few
		// ms. Drawing the raw particles would leave the cloth standing still in
		// the world on frames without a step while the body moves on, then jump
		// to catch up: a stutter at the frame rate that reads as the cape
		// shaking when running. Instead the cloth is drawn as the current
		// animated pose plus its offset from the animation, blended between the
		// last two steps (standard fixed-timestep interpolation).
		std::vector<float>& draw = in->scratchFrom;
		std::vector<float>& off = in->scratchTo;
		draw.resize(target.size());
		off.resize(target.size());
		{
			float a = in->accum / (float)MC_STEP_MS;
			if (a < 0.0f) a = 0.0f; else if (a > 1.0f) a = 1.0f;
			const bool ok = in->offCur.size() == target.size() && in->offPrev.size() == target.size()
				&& in->pCurStep.size() == target.size() && in->pPrevStep.size() == target.size();
			// Two ways to place a vertex between steps, blended by how free it is:
			//  - attached cloth: animated pose + offset (stays glued to the body
			//    exactly, even through sudden pose changes),
			//  - free cloth: its own world position interpolated between steps,
			//    moved only by the character's TRAVEL since then. It must not
			//    follow the pose: an animation switch (landing, a new saber move)
			//    would otherwise teleport the whole cape to the new pose.
			vec3_t oNow;
			for (int k = 0; k < 3; k++) oNow[k] = origin[k] - (in->oPrevStep[k] + (in->oCurStep[k] - in->oPrevStep[k]) * a);
			for (int p = 0; p < g->numParticles; p++) {
				float w = g->freedom[p] <= 0.0f ? 0.0f : (g->freedom[p] >= 1.0f ? 1.0f : g->freedom[p]);
				if (!g->hangs[p] || MCDBG_OFF("OFFDRAW")) w = 0.0f;	// sleeves and hoods follow their limb exactly
				for (int k = 0; k < 3; k++) {
					const size_t i = p * 3 + k;
					if (!ok) { off[i] = in->p[i] - target[i]; continue; }
					const float offA = in->offPrev[i] + (in->offCur[i] - in->offPrev[i]) * a;
					const float world = in->pPrevStep[i] + (in->pCurStep[i] - in->pPrevStep[i]) * a + oNow[k];
					off[i] = (1.0f - w) * offA + w * (world - target[i]);
				}
				if (g->freedom[p] <= 0.0f) off[p * 3] = off[p * 3 + 1] = off[p * 3 + 2] = 0.0f;
			}
			// SJE r_meshClothSettle: cloth still settling is drawn in its animated pose; settled cloth is eased in from it
			if (in->settleLeft > 0 || in->showSteps < MC_SHOW_STEPS) {
				float sh = in->settleLeft > 0 ? 0.0f : Clamp01((in->showSteps + a) / MC_SHOW_STEPS);
				sh = sh * sh * (3.0f - 2.0f * sh);
				for (size_t i = 0; i < off.size(); i++) off[i] *= sh;
			}
		}
		if (g->anySmooth) {
			std::vector<float>& acc = in->scratchTarget2;
			acc.resize(in->p.size());
			std::vector<float>& cnt = in->scratchCnt;
			cnt.resize(g->numParticles);
			for (int pass = 0; pass < 2; pass++) {
				std::fill(acc.begin(), acc.end(), 0.0f);
				std::fill(cnt.begin(), cnt.end(), 0.0f);
				for (size_t l = 0; l < g->linkA.size(); l++) {
					if (g->linkBend[l]) continue;
					const int a = g->linkA[l], b = g->linkB[l];
					mcAdd(&acc[a * 3], &off[b * 3], &acc[a * 3]); cnt[a] += 1.0f;
					mcAdd(&acc[b * 3], &off[a * 3], &acc[b * 3]); cnt[b] += 1.0f;
				}
				for (int p = 0; p < g->numParticles; p++) {
					if (g->freedom[p] <= 0.0f || cnt[p] == 0.0f) continue;
					const float w = g->smooth[p] * (g->freedom[p] < 1.0f ? g->freedom[p] : 1.0f);
					for (int k = 0; k < 3; k++) off[p * 3 + k] += w * (acc[p * 3 + k] / cnt[p] - off[p * 3 + k]);
				}
			}
		}
		// grid offsets -> every drawn point (identity without a grid)
		std::vector<float>& out = in->drawOut;
		out.resize((size_t)g->numDraw * 3);
		for (int d = 0; d < g->numDraw; d++) {
			vec3_t o = { 0, 0, 0 };
			if (g->drawFree[d] > 0.0f) {
				float fq = 0.0f;
				for (int j = 0; j < 4; j++) {
					const int q = g->drawIdx[d * 4 + j];
					if (q < 0) break;
					mcMA(o, g->drawW[d * 4 + j], &off[q * 3], o);
					fq += g->drawW[d * 4 + j] * g->freedom[q];
				}
				// near the attached edge a point may be less free than the grid points around it
				if (g->proxied && fq > g->drawFree[d] && !MCDBG_OFF("NOFREESCALE")) mcScale(o, g->drawFree[d] / fq, o);
			}
			mcCopy(o, &out[d * 3]);
		}
		// between grid points the drawn cloth cuts corners. Arms, fists and the saber hilt move
		// through the cape fast and are thin enough to slip between grid points, so drawn points are
		// kept off those (hard 1). The push is made before the smoothing passes, which spread it over
		// the neighbours (a point pushed on its own makes a spike), and once more after them.
		// The torso and legs are left to the grid: pushing drawn points off them popped.
		mcCapsule_t limbs[16]; int nl = 0;
		if (g->proxied) {
			const bool all = MCDBG_OFF("DRAWCOL");
			for (int c = 0; c < nnear && nl < 16; c++) if (all || nearCaps[c].hard == 1 || nearCaps[c].hard == 4) limbs[nl++] = nearCaps[c];
		}
		auto pushLimbs = [&]() {
			for (int d = 0; d < g->numDraw && nl; d++) {
				if (g->drawFree[d] <= 0.0f) continue;
				vec3_t pp, nrm;
				mcAdd(&drawTarget[d * 3], &out[d * 3], pp);
				vec3_t was; mcCopy(pp, was);
				if (!Collide(pp, &drawTarget[d * 3], limbs, nl, nrm, false, MC_MARGIN)) continue;
				// a short nudge per pass: a point that would have to jump across the arm stays put
				// rather than tearing away from its neighbours
				vec3_t mv; mcSub(pp, was, mv);
				const float ml = mcLen(mv);
				if (ml > 1.5f) mcMA(was, 1.5f / ml, mv, pp);
				mcSub(pp, &drawTarget[d * 3], &out[d * 3]);
			}
			};
		pushLimbs();
		// the weights change from one grid region to the next: a few smoothing
		// passes over the drawn mesh make the offsets continuous (grid points stay put)
		if (g->proxied) {
			std::vector<float>& acc = in->scratchTarget2;
			std::vector<float>& cnt = in->scratchCnt;
			for (int pass = 0; pass < 3; pass++) {
				acc.assign(out.size(), 0.0f);
				cnt.assign(g->numDraw, 0.0f);
				for (size_t l = 0; l < g->drawLinkA.size(); l++) {
					const int a = g->drawLinkA[l], b = g->drawLinkB[l];
					mcAdd(&acc[a * 3], &out[b * 3], &acc[a * 3]); cnt[a] += 1.0f;
					mcAdd(&acc[b * 3], &out[a * 3], &acc[b * 3]); cnt[b] += 1.0f;
				}
				for (int d = 0; d < g->numDraw; d++) {
					if (g->drawFree[d] <= 0.0f || cnt[d] == 0.0f) continue;
					for (int k = 0; k < 3; k++) out[d * 3 + k] += 0.5f * (acc[d * 3 + k] / cnt[d] - out[d * 3 + k]);
				}
			}
		}
		pushLimbs();
		for (int d = 0; d < g->numDraw; d++) mcAdd(&drawTarget[d * 3], &out[d * 3], &out[d * 3]);
		// SJE r_meshClothShoulders: the drawn cloth is kept over what the shoulder bones carry. A shoulder plate
		// rides the clavicle and the upper arm; when the arm swings back (a run, a jump) it turns into the cape
		// that lies over it - in the animated pose itself, right at the cape's attached edge. The simulation holds
		// cloth "no deeper than the animation" and the nearly attached points cannot give, so it cannot help there,
		// and cloth simulated on its own mesh has no shoulder shape at all. So here, on the drawn cloth only: each
		// plate vertex is skinned, and where it is not on its own side of a cloth triangle it lay under in the bind
		// pose (by a small gap), that triangle's corners are moved out along its normal, shared by how near each
		// is. The simulation is not touched: nothing pulls against the attached edge, nothing can shake.
		in->drawPushed.assign((size_t)g->numDraw, 0);
		if (prm->shoulders && (prm->faceCol & 1) && !g->proxied && g->plateBuilt && !g->platePairs.empty()) {
			const float ls = in->linkScale > 1e-3f ? in->linkScale : 1.0f;
			const size_t np = g->platePairs.size();
			std::vector<float>& pw = in->scratchPlate;
			pw.resize(np * 3);
			std::vector<float>& moved = in->scratchPlateTri;	// how far each triangle has been moved this frame
			moved.assign(g->drawTris.size() / 3, 0.0f);
			for (size_t i = 0; i < np; i++) {
				const mcPlatePair_t& pp = g->platePairs[i];
				if (i > 0 && g->platePairs[i - 1].s == pp.s && g->platePairs[i - 1].v == pp.v) { mcCopy(&pw[(i - 1) * 3], &pw[i * 3]); continue; }
				vec3_t l, nr;
				SkinVertex(pp.s, pp.v, boneFn, boneCtx, l, nr);
				ToWorld(l, origin, axis, &pw[i * 3]);
			}
			for (int pass = 0; pass < 2; pass++) for (size_t i = 0; i < np; i++) {
				const mcPlatePair_t& pp = g->platePairs[i];
				const float* w = &pw[i * 3];
				float* xs[3] = { &out[pp.a * 3], &out[pp.b * 3], &out[pp.c * 3] };
				const int id[3] = { pp.a, pp.b, pp.c };
				vec3_t e1, e2, n, q, v;
				mcSub(xs[1], xs[0], e1); mcSub(xs[2], xs[0], e2); mcCross(e1, e2, n);
				if (mcNorm(n) < 1e-6f) continue;
				// on its own side of the triangle's plane by more than the gap: nothing to do (nearly every pair)
				mcSub(w, xs[0], v);
				if (pp.gap * ls - mcDot(v, n) * pp.sgn < -0.05f) continue;
				float bc[3];
				ClosestPtTri(w, xs[0], xs[1], xs[2], bc);
				for (int k = 0; k < 3; k++) q[k] = bc[0] * xs[0][k] + bc[1] * xs[1][k] + bc[2] * xs[2][k];
				mcSub(w, q, v);
				const float hh = mcDot(v, n), lat2 = mcDot(v, v) - hh * hh;
				if (lat2 > pp.lat * pp.lat * ls * ls || fabsf(hh) > pp.reach * ls) continue;	// not over this triangle (any more)
				float C = pp.gap * ls - hh * pp.sgn;
				if (C <= 0.0f) continue;
				const float allow = pp.room * ls - moved[pp.tri];	// (the room is at most MC_PLATE_STEP)
				if (allow <= 0.0f) continue;
				if (C > allow) C = allow;
				moved[pp.tri] += C;
				const float den = bc[0] * bc[0] + bc[1] * bc[1] + bc[2] * bc[2];
				if (den < 1e-4f) continue;
				const float lam = C / den * pp.sgn;
				for (int k = 0; k < 3; k++) {
					if (bc[k] <= 0.0f) continue;
					mcMA(xs[k], -lam * bc[k], n, xs[k]);
					if (g->drawFree[id[k]] <= 0.0f) in->drawPushed[id[k]] = 1;
				}
			}
		}
		// the attached part of a cape rides the spine, but the shoulders and upper arms move on their own
		// (a raised arm, a torso attack while the legs run) and would show through it. Attached points
		// follow the skin smoothly, so pushing them off those capsules is smooth too.
		if (g->proxied && !MCDBG_OFF("NOATTACHPUSH")) {
			mcCapsule_t sh[16]; int ns = 0;
			for (int c = 0; c < nnear && ns < 16; c++) if (nearCaps[c].hard == 1 || nearCaps[c].hard == 2 || nearCaps[c].hard == 4) sh[ns++] = nearCaps[c];
			if (ns) for (int d = 0; d < g->numDraw; d++) {
				if (g->drawFree[d] > 0.0f) continue;
				vec3_t nrm;
				Collide(&out[d * 3], &drawTarget[d * 3], sh, ns, nrm, true, MC_MARGIN);
			}
		}
#ifdef MC_DEBUG
		if (g->proxied && getenv("MC_DUMPSTRETCH")) {
			for (size_t l = 0; l < g->drawLinkA.size(); l++) {
				const int a = g->drawLinkA[l], b = g->drawLinkB[l];
				vec3_t e1, e2; mcSub(&out[a * 3], &out[b * 3], e1); mcSub(&drawTarget[a * 3], &drawTarget[b * 3], e2);
				const float la = mcLen(e1), lb = mcLen(e2);
				if (lb > 0.5f && la > 10.0f * lb) {
					printf("STRETCH t=%d edge %d-%d %.2f->%.2f free %.2f %.2f\n", timeMs, a, b, lb, la, g->drawFree[a], g->drawFree[b]);
					for (int v : { a, b }) for (int j = 0; j < 4; j++) {
						int q = g->drawIdx[v * 4 + j]; if (q < 0) break;
						printf("   v%d q%d w%.2f qfree %.2f off %.1f %.1f %.1f\n", v, q, g->drawW[v * 4 + j], g->freedom[q], off[q * 3], off[q * 3 + 1], off[q * 3 + 2]);
					}
					break;
				}
			}
		}
#endif
		// safety net: a degenerate pose (zero-scale bones, a broken model) must
		// never put NaNs on screen or keep them in the state - fall back to the
		// animation and start the cloth over next frame
		bool bad = false;
		for (size_t i = 0; i < in->p.size() && !bad; i++) if (!std::isfinite(in->p[i])) bad = true;
		for (size_t i = 0; i < out.size() && !bad; i++) if (!std::isfinite(out[i])) bad = true;
		if (bad) {
			out.assign(drawTarget.begin(), drawTarget.end());
			in->active = false;
		}

		// the drawn surface is blended from the simulation and the animation, so it can still reach past a wall or the
		// floor between grid points: keep the drawn free points in front of them too (r_meshClothWalls)
		if (prm->numWalls > 0) {
			const int nw = prm->numWalls > 8 ? 8 : prm->numWalls;
			for (int k = -1; k < nw; k++) {
				if (k < 0 && !prm->floor) continue;
				const float* pt = k < 0 ? prm->floorPt : prm->wallPt[k], * pn = k < 0 ? prm->floorN : prm->wallN[k];
				for (int d = 0; d < g->numDraw; d++) {
					if (g->drawFree[d] <= 0.0f) continue;
					vec3_t rr;
					mcSub(&out[d * 3], pt, rr);
					const float dist = mcDot(rr, pn) - MC_FLOOR_GAP * 0.5f;
					if (dist < 0.0f) mcMA(&out[d * 3], -dist * Clamp01(g->drawFree[d] * 4.0f), pn, &out[d * 3]);
				}
			}
		}

		// results back into model space, with normals from the moving triangles
		std::vector<float>& nw = in->scratchN;
		DrawNormals(g, out, nw);
		in->outLocal.resize((size_t)g->numDraw * 3);
		in->nrmLocal.resize((size_t)g->numDraw * 3);
		for (int p = 0; p < g->numDraw; p++) {
			ToLocal(&out[p * 3], origin, axis, &in->outLocal[p * 3]);
			DirToLocal(&nw[p * 3], axis, &in->nrmLocal[p * 3]);
			mcNorm(&in->nrmLocal[p * 3]);
		}
		in->outTime = timeMs;
	}
} // namespace

int MC_LoadConfig(const char* text) {
	g_defs.clear();
	MC_Clear();
#ifdef MC_MP
	COM_BeginParseSession("meshcloth");
#else
	COM_ParseSession session;	// the SP parser refuses to run outside a session
#endif
	const char* p = text;
	while (p) {
		const char* tok = COM_ParseExt(&p, qtrue);
		if (!tok[0]) break;
		mcDef_t d;
		d.model = tok;
		tok = COM_ParseExt(&p, qfalse); if (!tok[0]) break;
		d.surface = tok;
		tok = COM_ParseExt(&p, qfalse);
		int need;
		if (!Q_stricmp(tok, "above")) { d.rule = MC_RULE_ABOVE; need = 2; }
		else if (!Q_stricmp(tok, "near")) { d.rule = MC_RULE_NEAR; need = 5; }
		else if (!Q_stricmp(tok, "segment")) { d.rule = MC_RULE_SEGMENT; need = 8; }
		else { while (COM_ParseExt(&p, qfalse)[0]) {} continue; }	// unknown rule: skip line
		int k;
		for (k = 0; k < need; k++) {
			tok = COM_ParseExt(&p, qfalse);
			if (!tok[0]) break;
			d.a[k] = (float)atof(tok);
		}
		if (k < need) continue;
		d.tether = 12.0f;
		d.shape = 0.0f;
		d.bend = 0.3f;
		d.inertia = -1.0f;
		d.self = false;
		d.smooth = 0.5f;
		d.maxAccel = -1.0f;
		d.maxSpin = -1.0f;
		d.spread = 0.0f;
		d.reach = 0.0f;
		d.turn = 0.0f;
		d.weight = 1.0f;
		d.thickness = 1.2f;
		d.proxy = -1;
		d.stretch = 0.0f;
		d.lift = 2.0f;
		d.friction = 0.3f;
		d.sfriction = 0.5f;
		while (1) {
			tok = COM_ParseExt(&p, qfalse);
			if (!tok[0]) break;
			if (!Q_stricmp(tok, "tether")) { d.tether = (float)atof(COM_ParseExt(&p, qfalse)); if (d.tether < 0.0f) d.tether = 0.0f; }
			else if (!Q_stricmp(tok, "thickness")) { d.thickness = (float)atof(COM_ParseExt(&p, qfalse)); if (d.thickness < 0.0f) d.thickness = 0.0f; if (d.thickness > 8.0f) d.thickness = 8.0f; }
			else if (!Q_stricmp(tok, "shape")) d.shape = (float)atof(COM_ParseExt(&p, qfalse));
			else if (!Q_stricmp(tok, "self")) d.self = true;
			else if (!Q_stricmp(tok, "proxy")) { d.proxy = atoi(COM_ParseExt(&p, qfalse)); if (d.proxy < 0) d.proxy = 0; if (d.proxy > 4000) d.proxy = 4000; }
			else if (!Q_stricmp(tok, "weight")) { d.weight = (float)atof(COM_ParseExt(&p, qfalse)); if (d.weight < 0.1f) d.weight = 0.1f; if (d.weight > 10.0f) d.weight = 10.0f; }
			else if (!Q_stricmp(tok, "spread")) d.spread = Clamp01((float)atof(COM_ParseExt(&p, qfalse)));
			else if (!Q_stricmp(tok, "reach")) { d.reach = (float)atof(COM_ParseExt(&p, qfalse)); if (d.reach < 0.0f) d.reach = 0.0f; }
			else if (!Q_stricmp(tok, "turn")) d.turn = Clamp01((float)atof(COM_ParseExt(&p, qfalse)));
			else if (!Q_stricmp(tok, "maxspin")) { d.maxSpin = (float)atof(COM_ParseExt(&p, qfalse)); if (d.maxSpin < 0.0f) d.maxSpin = 0.0f; }
			else if (!Q_stricmp(tok, "maxaccel")) { d.maxAccel = (float)atof(COM_ParseExt(&p, qfalse)); if (d.maxAccel < 0.0f) d.maxAccel = 0.0f; }
			else if (!Q_stricmp(tok, "smooth")) d.smooth = Clamp01((float)atof(COM_ParseExt(&p, qfalse)));
			else if (!Q_stricmp(tok, "inertia")) d.inertia = Clamp01((float)atof(COM_ParseExt(&p, qfalse)));
			else if (!Q_stricmp(tok, "bend")) d.bend = Clamp01((float)atof(COM_ParseExt(&p, qfalse)));
			else if (!Q_stricmp(tok, "lift")) { tok = COM_ParseExt(&p, qfalse); d.lift = !Q_stricmp(tok, "off") ? -1.0f : (float)atof(tok); }
			else if (!Q_stricmp(tok, "stretch")) { d.stretch = Clamp01((float)atof(COM_ParseExt(&p, qfalse))); if (d.stretch > 0.95f) d.stretch = 0.95f; }
			else if (!Q_stricmp(tok, "friction")) d.friction = Clamp01((float)atof(COM_ParseExt(&p, qfalse)));
			else if (!Q_stricmp(tok, "sfriction")) { d.sfriction = (float)atof(COM_ParseExt(&p, qfalse)); if (d.sfriction < 0.0f) d.sfriction = 0.0f; if (d.sfriction > 4.0f) d.sfriction = 4.0f; }
		}
		if (d.tether < 0.0f) d.tether = 0.0f;
		g_defs.push_back(d);
	}
	return (int)g_defs.size();
}

// ---------------------------------------------------------------------------
// r_meshClothBodyFit (KatarnEngine): body shape fitted from the model's own mesh. For every body bone the vertices it
// mostly carries are cut into slices along the height; each slice becomes a "stadium" (a capsule across the slice:
// flat chest and back, round sides) sized to the mesh, so cloth rests on the real torso, hips and legs instead of on a
// few round tubes. Fitted once per cloth group on the main thread, posed on any thread.
// ---------------------------------------------------------------------------
namespace {
	struct mcFitShape_t { int bone; float a[3], b[3], r; };
	std::unordered_map<unsigned long long, std::vector<mcFitShape_t>> g_fits;	// per cloth group and set of switched-on surfaces (SJE r_meshClothShown)
	const float MC_FIT_PCT = 0.95f;	// fitted shapes reach out to where this share of the mesh is inside them

	float FitPct(std::vector<float>& v, float q) {
		if (v.empty()) return 0.0f;
		const size_t k = (size_t)(q * (v.size() - 1));
		std::nth_element(v.begin(), v.begin() + k, v.end());
		return v[k];
	}

	// main axis of a point set (power iteration on the covariance), kept orthogonal to 'ortho'
	void FitMainAxis(const std::vector<float>& P, const float* c, float* axis, const float* ortho) {
		double C[3][3] = {};
		for (size_t i = 0; i < P.size(); i += 3) {
			float d[3] = { P[i] - c[0], P[i + 1] - c[1], P[i + 2] - c[2] };
			const float t = mcDot(d, ortho); mcMA(d, -t, ortho, d);
			for (int r = 0; r < 3; r++) for (int q = 0; q < 3; q++) C[r][q] += d[r] * d[q];
		}
		float v[3] = { 0.577f, 0.577f, 0.577f };
		{ const float t = mcDot(v, ortho); mcMA(v, -t, ortho, v); if (mcNorm(v) < 1e-3f) { v[0] = 1; v[1] = v[2] = 0; } }
		for (int it = 0; it < 32; it++) {
			float w[3];
			for (int r = 0; r < 3; r++) w[r] = (float)(C[r][0] * v[0] + C[r][1] * v[1] + C[r][2] * v[2]);
			const float t = mcDot(w, ortho); mcMA(w, -t, ortho, w);
			if (mcNorm(w) < 1e-9f) break;
			mcCopy(w, v);
		}
		mcCopy(v, axis);
	}

	// SJE r_meshClothShown: 'drawn' (one byte per surface index; nullptr = not known) says which surfaces this
	// character has switched on. Without it the model's own flags decide - and a model that holds several outfits
	// has most of them on: a hood, a scarf, hip padding the skin does not draw then pushed the cloth off the body.
	void FitBody(const mcGroup_t* g, const int* boneClass, int numBones, std::vector<mcFitShape_t>& out, const unsigned char* drawn = nullptr, const int numDrawn = 0) {
		out.clear();
		if (g->surfs.empty()) return;
		const mdxmHeader_t* h = HeaderOf(g->surfs[0]);
		const auto ho = reinterpret_cast<const mdxmHierarchyOffsets_t*>(reinterpret_cast<const byte*>(h) + sizeof(mdxmHeader_t));
		char folder[MAX_QPATH];
		ModelFolder(h->name, folder, sizeof(folder));
		std::unordered_map<int, std::vector<float>> pts;	// bone -> bind positions
		for (const mdxmSurface_t* s : LodSurfaces(g->surfs[0])) {
			if (std::find(g->surfs.begin(), g->surfs.end(), s) != g->surfs.end()) continue;	// cloth isn't body
			const auto sit = g_surfs.find(s);
			if (sit != g_surfs.end() && sit->second.group) continue;	// cloth of another group
			const auto sh = reinterpret_cast<const mdxmSurfHierarchy_t*>(reinterpret_cast<const byte*>(ho) + ho->offsets[s->thisSurfaceIndex]);
			if (drawn ? !(s->thisSurfaceIndex >= 0 && s->thisSurfaceIndex < numDrawn && drawn[s->thisSurfaceIndex]) : (sh->flags & (G2SURFACEFLAG_OFF | G2SURFACEFLAG_ISBOLT)) != 0) continue;
			if (strstr(sh->name, "_cap_") || strstr(sh->name, "_off")) continue;
			if (FindDef(folder, sh->name)) continue;	// a cloth surface (meshcloth.cfg)
			const int* refs = reinterpret_cast<const int*>(reinterpret_cast<const byte*>(s) + s->ofsBoneReferences);
			const mdxmVertex_t* v = Verts(s);
			for (int i = 0; i < s->numVerts; i++) {
				const int nw = G2_GetVertWeights(&v[i]);
				float tot = 0.0f, best = 0.0f; int bb = -1;
				for (int k = 0; k < nw; k++) {
					const float w = G2_GetVertBoneWeight(&v[i], k, tot, nw);
					if (w > best) { best = w; bb = refs[G2_GetVertBoneIndex(&v[i], k)]; }
				}
				if (bb < 0 || bb >= numBones || boneClass[bb] != 0 || best < 0.5f) continue;	// (SJE: class 1 = shoulder bones, no fitted shape)
				auto& P = pts[bb];
				P.insert(P.end(), v[i].vertCoords, v[i].vertCoords + 3);
			}
		}
		for (auto& kv : pts) {
			const std::vector<float>& P = kv.second;
			const int n = (int)(P.size() / 3);
			if (n < 20) continue;
			float c[3] = { 0, 0, 0 };
			for (int i = 0; i < n; i++) mcAdd(c, &P[i * 3], c);
			mcScale(c, 1.0f / n, c);
			// torso, hips, legs and head all stand upright in the bind (T) pose: slice by height
			const float e1[3] = { 0.0f, 0.0f, 1.0f };
			std::vector<float> proj(n);
			for (int i = 0; i < n; i++) { float d[3]; mcSub(&P[i * 3], c, d); proj[i] = mcDot(d, e1); }
			std::vector<float> tmp = proj;
			const float lo = FitPct(tmp, 0.03f), hi = FitPct(tmp, 0.97f);
			const float len = hi - lo;
			if (len < 0.5f) continue;
			int K = (int)(len / 3.0f + 0.5f);
			K = K < 1 ? 1 : (K > 12 ? 12 : K);
			for (int k = 0; k < K; k++) {
				const float s0 = lo + len * k / K, s1 = lo + len * (k + 1) / K;
				std::vector<float> S;
				for (int i = 0; i < n; i++) if (proj[i] >= s0 - 0.5f && proj[i] <= s1 + 0.5f) S.insert(S.end(), &P[i * 3], &P[i * 3] + 3);
				const int m = (int)(S.size() / 3);
				if (m < 6) continue;
				float sc[3] = { 0, 0, 0 };
				for (int i = 0; i < m; i++) mcAdd(sc, &S[i * 3], sc);
				mcScale(sc, 1.0f / m, sc);
				float u[3], vv[3];
				FitMainAxis(S, sc, u, e1);
				mcCross(e1, u, vv);
				mcNorm(vv);
				// extents each side (the cross-section needn't be centred on the centroid)
				std::vector<float> pu, nu, pv, nv;
				for (int i = 0; i < m; i++) {
					float d[3]; mcSub(&S[i * 3], sc, d);
					const float du = mcDot(d, u), dv = mcDot(d, vv);
					(du >= 0 ? pu : nu).push_back(fabsf(du));
					(dv >= 0 ? pv : nv).push_back(fabsf(dv));
				}
				const float q = MC_FIT_PCT, au = FitPct(pu, q), bu = FitPct(nu, q), av = FitPct(pv, q), bv = FitPct(nv, q);
				float mid[3];
				mcMA(sc, 0.5f * (s0 + s1) - mcDot(sc, e1) + mcDot(c, e1), e1, mid);
				mcMA(mid, 0.5f * (au - bu), u, mid);
				mcMA(mid, 0.5f * (av - bv), vv, mid);
				const float A = 0.5f * (au + bu), B = 0.5f * (av + bv);
				if (B < 0.3f) continue;
				mcFitShape_t f;
				f.bone = kv.first;
				f.r = B;
				const float half = A > B ? A - B : 0.0f;
				mcMA(mid, -half, u, f.a);
				mcMA(mid, half, u, f.b);
				out.push_back(f);
			}
		}
	}
} // namespace

namespace {
	const float MC_HIGH_PART = 0.81f;	// SJE: cloth hangs 'from above the arms' if its anchor lies at this part of the shoulder top's height or above
	void BuildPlatePairs(const mcGroup_t* g, const int* boneClass, int numBones) {
		g->platePairs.clear();
		// SJE: which cloth hangs from shoulder height or above (FaceCollide: such cloth lies over arms and hands).
		// The top of what the shoulder bones carry, in the bind pose (upright, feet at 0), against each particle's
		// attached anchor.
		{
			std::vector<unsigned char> high((size_t)g->numParticles, 0);
			float top = -1e9f;
			if (!g->surfs.empty()) {
				for (const mdxmSurface_t* bs : LodSurfaces(g->surfs[0])) {
					const int* refs = reinterpret_cast<const int*>(reinterpret_cast<const byte*>(bs) + bs->ofsBoneReferences);
					const mdxmVertex_t* v = Verts(bs);
					for (int i = 0; i < bs->numVerts; i++) {
						const int nw = G2_GetVertWeights(&v[i]);
						float tot = 0.0f, bw = 0.0f;
						int bb = -1;
						for (int k = 0; k < nw; k++) {
							const float w = G2_GetVertBoneWeight(&v[i], k, tot, nw);
							if (w > bw) { bw = w; bb = refs[G2_GetVertBoneIndex(&v[i], k)]; }
						}
						if (bb >= 0 && bb < numBones && boneClass[bb] == 1 && v[i].vertCoords[2] > top) top = v[i].vertCoords[2];
					}
				}
			}
			if (top > 1.0f && (int)g->bindPos.size() >= g->numParticles * 3 && (int)g->lraAnchor.size() >= g->numParticles) {
				for (int p = 0; p < g->numParticles; p++) {
					const int an = g->lraAnchor[p] >= 0 ? g->lraAnchor[p] : p;
					if (g->bindPos[(size_t)an * 3 + 2] >= MC_HIGH_PART * top) high[p] = 1;
				}
			}
			g->high.swap(high);
		}
		if (g->surfs.empty() || g->drawTris.size() < 3) return;
		std::vector<float> db((size_t)g->numDraw * 3, 0.0f);	// bind positions of the drawn cloth points
		for (size_t si = 0; si < g->surfs.size(); si++) {
			const mdxmSurface_t* cs = g->surfs[si];
			const mdxmVertex_t* cv = Verts(cs);
			for (int i = 0; i < cs->numVerts; i++) {
				const size_t dp = (size_t)g->vertToDraw[g->vertBase[si] + i] * 3;
				mcCopy(cv[i].vertCoords, &db[dp]);
			}
		}
		const mdxmHeader_t* h = HeaderOf(g->surfs[0]);
		const auto ho = reinterpret_cast<const mdxmHierarchyOffsets_t*>(reinterpret_cast<const byte*>(h) + sizeof(mdxmHeader_t));
		char folder[MAX_QPATH];
		ModelFolder(h->name, folder, sizeof(folder));
		const int nt = (int)(g->drawTris.size() / 3);
		float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
		for (int d = 0; d < g->numDraw; d++) for (int k = 0; k < 3; k++) {
			if (db[d * 3 + k] < lo[k]) lo[k] = db[d * 3 + k];
			if (db[d * 3 + k] > hi[k]) hi[k] = db[d * 3 + k];
		}
		// the model's own surfaces near the cloth; 'plate': a shoulder bone mostly carries the vertex
		struct bodyVert_t { const mdxmSurface_t* s; int v; bool plate; };
		std::vector<bodyVert_t> body;
		for (const mdxmSurface_t* bs : LodSurfaces(g->surfs[0])) {
			if (std::find(g->surfs.begin(), g->surfs.end(), bs) != g->surfs.end()) continue;	// cloth isn't body
			const auto sit = g_surfs.find(bs);
			if (sit != g_surfs.end() && sit->second.group) continue;	// cloth of another group
			const auto sh = reinterpret_cast<const mdxmSurfHierarchy_t*>(reinterpret_cast<const byte*>(ho) + ho->offsets[bs->thisSurfaceIndex]);
			if (sh->flags & (G2SURFACEFLAG_OFF | G2SURFACEFLAG_ISBOLT)) continue;
			if (strstr(sh->name, "_cap_") || strstr(sh->name, "_off")) continue;
			if (FindDef(folder, sh->name)) continue;	// a cloth surface (meshcloth.cfg)
			const int* refs = reinterpret_cast<const int*>(reinterpret_cast<const byte*>(bs) + bs->ofsBoneReferences);
			const mdxmVertex_t* v = Verts(bs);
			for (int i = 0; i < bs->numVerts; i++) {
				const float* x = v[i].vertCoords;
				bool far = false;
				for (int k = 0; k < 3; k++) if (x[k] < lo[k] - MC_PLATE_BIND || x[k] > hi[k] + MC_PLATE_BIND) far = true;
				if (far) continue;
				const int nw = G2_GetVertWeights(&v[i]);
				float tot = 0.0f, bw = 0.0f;
				int bb = -1;
				for (int k = 0; k < nw; k++) {
					const float w = G2_GetVertBoneWeight(&v[i], k, tot, nw);
					if (w > bw) { bw = w; bb = refs[G2_GetVertBoneIndex(&v[i], k)]; }
				}
				body.push_back({ bs, i, bb >= 0 && bb < numBones && boneClass[bb] == 1 });
			}
		}
		// per triangle and side (0: the side its normal points to, 1: the other): how near the nearest vertex of the
		// model's own surfaces lies over it in the bind pose
		std::vector<float> room((size_t)nt * 2, 1e9f);
		for (const bodyVert_t& bv : body) {
			const float* x = Verts(bv.s)[bv.v].vertCoords;
			for (int t = 0; t < nt; t++) {
				const int ia = g->drawTris[t * 3], ib = g->drawTris[t * 3 + 1], ic = g->drawTris[t * 3 + 2];
				if (ia == ib || ib == ic || ia == ic) continue;
				const float* a = &db[ia * 3], * b = &db[ib * 3], * c = &db[ic * 3];
				bool out = false;
				for (int k = 0; k < 3 && !out; k++) {
					const float mn = a[k] < b[k] ? (a[k] < c[k] ? a[k] : c[k]) : (b[k] < c[k] ? b[k] : c[k]);
					const float mx = a[k] > b[k] ? (a[k] > c[k] ? a[k] : c[k]) : (b[k] > c[k] ? b[k] : c[k]);
					out = x[k] < mn - MC_PLATE_BIND || x[k] > mx + MC_PLATE_BIND;
				}
				if (out) continue;
				float bc[3];
				ClosestPtTri(x, a, b, c, bc);
				vec3_t q, d, e1, e2, n;
				for (int k = 0; k < 3; k++) q[k] = bc[0] * a[k] + bc[1] * b[k] + bc[2] * c[k];
				mcSub(x, q, d);
				const float dl = mcLen(d);
				if (dl > MC_PLATE_BIND || dl < 0.05f) continue;
				mcSub(b, a, e1); mcSub(c, a, e2); mcCross(e1, e2, n);
				if (mcNorm(n) < 1e-6f) continue;
				const float hh = mcDot(d, n);
				if (fabsf(hh) < 0.6f * dl) continue;	// beside the triangle
				float& r = room[(size_t)t * 2 + (hh > 0.0f ? 0 : 1)];
				if (fabsf(hh) < r) r = fabsf(hh);
			}
		}
		for (const bodyVert_t& bv : body) {
			if (!bv.plate) continue;
			// the three nearest cloth triangles it lies under (or over), as the cloth layers are paired
			const float* x = Verts(bv.s)[bv.v].vertCoords;
			mcPlatePair_t best[3];
			float bestD[3] = { 1e9f, 1e9f, 1e9f };
			for (int t = 0; t < nt; t++) {
				const int ia = g->drawTris[t * 3], ib = g->drawTris[t * 3 + 1], ic = g->drawTris[t * 3 + 2];
				if (ia == ib || ib == ic || ia == ic) continue;
				const float* a = &db[ia * 3], * b = &db[ib * 3], * c = &db[ic * 3];
				float bc[3];
				ClosestPtTri(x, a, b, c, bc);
				vec3_t q, d, e1, e2, e3, n;
				for (int k = 0; k < 3; k++) q[k] = bc[0] * a[k] + bc[1] * b[k] + bc[2] * c[k];
				mcSub(x, q, d);
				const float dl = mcLen(d);
				int slot = 0;
				for (int k = 1; k < 3; k++) if (bestD[k] > bestD[slot]) slot = k;
				if (dl > MC_PLATE_BIND || dl < 0.05f || dl >= bestD[slot]) continue;
				mcSub(b, a, e1); mcSub(c, a, e2); mcSub(c, b, e3); mcCross(e1, e2, n);
				if (mcNorm(n) < 1e-6f) continue;
				const float hh = mcDot(d, n);
				if (fabsf(hh) < 0.6f * dl) continue;	// beside the triangle, not under or over it
				// Room: the triangle is moved away from this vertex, towards its other side - never further than the
				// nearest surface of the model lies from it on that side in the bind pose. Cloth the model has
				// between two of its own surfaces stays between them (Revan's cape starts between his shoulder mantle
				// and his back armour: moved clear of the mantle it came out through the armour, 34 more body
				// vertices through the cape per frame); cloth that is the outer layer has all the room it needs.
				const float rm = room[(size_t)t * 2 + (hh > 0.0f ? 1 : 0)] - MC_PLATE_KEEP;
				if (rm < 0.05f) continue;
				float size = mcLen(e1);
				if (mcLen(e2) > size) size = mcLen(e2);
				if (mcLen(e3) > size) size = mcLen(e3);
				bestD[slot] = dl;
				best[slot].s = bv.s; best[slot].v = bv.v; best[slot].tri = t; best[slot].a = ia; best[slot].b = ib; best[slot].c = ic;
				best[slot].sgn = hh > 0.0f ? 1.0f : -1.0f;
				best[slot].gap = MC_PLATE_GAP < 0.8f * fabsf(hh) ? MC_PLATE_GAP : 0.8f * fabsf(hh);
				best[slot].room = rm < MC_PLATE_STEP ? rm : MC_PLATE_STEP;
				best[slot].reach = dl + size + 4.0f;
				best[slot].lat = 0.6f * size;
			}
			for (int k = 0; k < 3; k++) if (bestD[k] < 1e8f) g->platePairs.push_back(best[k]);
		}
	}
} // namespace

void MC_Shown(const void* clothSurface, const void* instanceKey, const unsigned char* drawn, const int numDrawn) {
	const auto sit = g_surfs.find(static_cast<const mdxmSurface_t*>(clothSurface));
	if (sit == g_surfs.end() || !sit->second.group) return;
	const mcGroup_t* g = sit->second.group;
	const auto iit = g_instances.find((unsigned long long)(size_t)instanceKey * 31ULL ^ (unsigned long long)(size_t)g);
	if (iit == g_instances.end()) return;
	unsigned off = 0;
	const int ns = (int)g->surfs.size();
	if (drawn && ns <= 32) {
		for (int i = 0; i < ns; i++) {
			const int idx = g->surfs[i]->thisSurfaceIndex;
			if (idx >= 0 && idx < numDrawn && !drawn[idx]) off |= 1u << i;
		}
	}
	iit->second->offMask = off;
}

const void* MC_BodyFit(const void* clothSurface, const int* boneClass, int numBones, const unsigned char* drawn, const int numDrawn) {
	const auto sit = g_surfs.find(static_cast<const mdxmSurface_t*>(clothSurface));
	if (sit == g_surfs.end() || !sit->second.group) return nullptr;
	const mcGroup_t* g = sit->second.group;
	// SJE r_meshClothShoulders: what the shoulder bones carry, under the cloth (once per model, main thread)
	if (!g->plateBuilt) {
		BuildPlatePairs(g, boneClass, numBones);
		g->plateBuilt = true;
	}
	// SJE r_meshClothShown: one fit per set of switched-on surfaces (a model's skins: a handful)
	unsigned long long key = (1469598103934665603ULL ^ (unsigned long long)(size_t)g) * 1099511628211ULL;
	if (drawn) for (int i = 0; i < numDrawn; i++) key = (key ^ (unsigned long long)(drawn[i] ? 2 : 1)) * 1099511628211ULL;
	auto fit = g_fits.find(key);
	if (fit == g_fits.end()) {
		std::vector<mcFitShape_t> shapes;
		FitBody(g, boneClass, numBones, shapes, drawn, numDrawn);
		fit = g_fits.emplace(key, std::move(shapes)).first;
	}
	return fit->second.empty() ? nullptr : &fit->second;
}

// SJE r_meshClothFist: does this surface's cloth (its character's cloth, all surfaces) hang from shoulder height or
// above anywhere - a cape, a cloak ('high', BuildPlatePairs)? Such cloth lies over arms and hands (FaceCollide), and
// for it the fist shape is turned along the hand. Main thread, after MC_BodyFit.
bool MC_ClothHangsHigh(const void* clothSurface) {
	const auto sit = g_surfs.find(static_cast<const mdxmSurface_t*>(clothSurface));
	if (sit == g_surfs.end() || !sit->second.group) return false;
	const mcGroup_t* g = sit->second.group;
	if (!g->plateBuilt || (int)g->high.size() != g->numParticles) return false;
	return std::find(g->high.begin(), g->high.end(), (unsigned char)1) != g->high.end();
}

int MC_BodyShapesPose(const void* fitHandle, mcBoneFn_t boneFn, void* boneCtx, const vec3_t origin, const vec3_t axis[3],
	mcCapsule_t* out, int maxOut) {
	if (!fitHandle) return 0;
	const auto& shapes = *static_cast<const std::vector<mcFitShape_t>*>(fitHandle);
	int n = 0;
	for (const mcFitShape_t& f : shapes) {
		if (n >= maxOut) break;
		const float (*m)[4] = boneFn(boneCtx, f.bone);
		float pa[3], pb[3];
		for (int r = 0; r < 3; r++) {
			pa[r] = m[r][0] * f.a[0] + m[r][1] * f.a[1] + m[r][2] * f.a[2] + m[r][3];
			pb[r] = m[r][0] * f.b[0] + m[r][1] * f.b[1] + m[r][2] * f.b[2] + m[r][3];
		}
		ToWorld(pa, origin, axis, out[n].a);
		ToWorld(pb, origin, axis, out[n].b);
		out[n].r = f.r;
		out[n].hard = 3;	// hanging cloth is pushed fully out of the real body shape
		n++;
	}
	return n;
}

// ---------------------------------------------------------------------------
// SJE: r_meshClothHeld. Things bolted to a character - a saber hilt in the hand or on the belt, a blaster, a jet
// pack - get collision shapes fitted from their own mesh, as the body does (r_meshClothBodyFit). The fixed hilt
// capsule (r_meshClothHilt) is a straight stick 4 units behind and 9 in front of the hand whatever is held: Dooku's
// curved hilt reaches 9 units behind its blade and 4 to the side, and that end went through his cape untouched.
// The mesh is cut into up to three slices along its longest side; a capsule joins the middles of each slice's two
// ends, so a curved hilt gets shapes that follow its curve. Fitted once per model (main thread), posed on any thread.
// ---------------------------------------------------------------------------
namespace {
	std::unordered_map<const void*, std::vector<mcFitShape_t>> g_heldFits;	// per mdxm header

	void FitHeld(const mdxmHeader_t* h, std::vector<mcFitShape_t>& out) {
		out.clear();
		if (!h || h->numLODs < 1 || h->numSurfaces < 1) return;
		const auto ho = reinterpret_cast<const mdxmHierarchyOffsets_t*>(reinterpret_cast<const byte*>(h) + sizeof(mdxmHeader_t));
		const byte* lod = reinterpret_cast<const byte*>(h) + h->ofsLODs;
		const auto lso = reinterpret_cast<const mdxmLODSurfOffset_t*>(lod + sizeof(mdxmLOD_t));
		std::vector<float> P;						// bind positions
		std::unordered_map<int, int> boneCount;	// bone -> vertices it mostly carries
		for (int i = 0; i < h->numSurfaces; i++) {
			const auto s = reinterpret_cast<const mdxmSurface_t*>(reinterpret_cast<const byte*>(lso) + lso->offsets[i]);
			const auto sh = reinterpret_cast<const mdxmSurfHierarchy_t*>(reinterpret_cast<const byte*>(ho) + ho->offsets[s->thisSurfaceIndex]);
			if (sh->flags & (G2SURFACEFLAG_OFF | G2SURFACEFLAG_ISBOLT)) continue;
			if (sh->name[0] == '*' || strstr(sh->name, "_off")) continue;
			if (s->numVerts <= 0 || P.size() / 3 + (size_t)s->numVerts > 8000) continue;
			const int* refs = reinterpret_cast<const int*>(reinterpret_cast<const byte*>(s) + s->ofsBoneReferences);
			const mdxmVertex_t* v = Verts(s);
			for (int k = 0; k < s->numVerts; k++) {
				P.insert(P.end(), v[k].vertCoords, v[k].vertCoords + 3);
				const int nw = G2_GetVertWeights(&v[k]);
				float tot = 0.0f, best = -1.0f;
				int bb = -1;
				for (int j = 0; j < nw; j++) {
					const float w = G2_GetVertBoneWeight(&v[k], j, tot, nw);
					if (w > best) { best = w; bb = refs[G2_GetVertBoneIndex(&v[k], j)]; }
				}
				if (bb >= 0) boneCount[bb]++;
			}
		}
		const int n = (int)(P.size() / 3);
		if (n < 8 || boneCount.empty()) return;
		int bone = -1, most = 0;
		for (const auto& kv : boneCount) if (kv.second > most) { most = kv.second; bone = kv.first; }
		float lo[3] = { 1e9f, 1e9f, 1e9f }, hi[3] = { -1e9f, -1e9f, -1e9f };
		for (int i = 0; i < n; i++) for (int k = 0; k < 3; k++) {
			if (P[i * 3 + k] < lo[k]) lo[k] = P[i * 3 + k];
			if (P[i * 3 + k] > hi[k]) hi[k] = P[i * 3 + k];
		}
		int ax = 0;
		for (int k = 1; k < 3; k++) if (hi[k] - lo[k] > hi[ax] - lo[ax]) ax = k;
		const float len = hi[ax] - lo[ax];
		if (len < 1.0f || len > 64.0f) return;		// nothing to hold, or no held thing (a whole figure)
		const int K = len < 7.0f ? 1 : (len < 16.0f ? 2 : 3);
		// the middle of the mesh at each slice boundary (a band a third of a slice thick)
		const float sl = len / K, band = sl * 0.34f;
		float node[4][3];
		for (int j = 0; j <= K; j++) {
			const float at = lo[ax] + sl * j;
			float c[3] = { 0, 0, 0 };
			int m = 0;
			for (float w = band; m < 3 && w < len + band; w *= 2.0f) {
				c[0] = c[1] = c[2] = 0.0f; m = 0;
				for (int i = 0; i < n; i++) if (fabsf(P[i * 3 + ax] - at) <= w) { mcAdd(c, &P[i * 3], c); m++; }
			}
			if (!m) return;
			mcScale(c, 1.0f / m, node[j]);
			node[j][ax] = at;
		}
		for (int j = 0; j < K; j++) {
			const float* a = node[j], * b = node[j + 1];
			vec3_t ab;
			mcSub(b, a, ab);
			const float L = mcDot(ab, ab);
			std::vector<float> d;
			for (int i = 0; i < n; i++) {
				if (P[i * 3 + ax] < a[ax] - 0.01f || P[i * 3 + ax] > b[ax] + 0.01f) continue;
				vec3_t ap, q, e;
				mcSub(&P[i * 3], a, ap);
				mcMA(a, L > 1e-6f ? Clamp01(mcDot(ap, ab) / L) : 0.0f, ab, q);
				mcSub(&P[i * 3], q, e);
				d.push_back(mcLen(e));
			}
			if (d.size() < 4) continue;
			float r = FitPct(d, 0.85f);
			r = r < 0.5f ? 0.5f : (r > 5.0f ? 5.0f : r);
			mcFitShape_t f;
			f.bone = bone;
			f.r = r;
			mcCopy(a, f.a);
			mcCopy(b, f.b);
			// the round ends reach past the mesh's ends: pull those in by the radius
			const float sL = sqrtf(L);
			if (sL > 2.0f * r) {
				if (j == 0) mcMA(f.a, r / sL, ab, f.a);
				if (j == K - 1) mcMA(f.b, -r / sL, ab, f.b);
			}
			out.push_back(f);
		}
	}
} // namespace

const void* MC_HeldFit(const void* mdxmHeader) {
	if (!mdxmHeader) return nullptr;
	auto fit = g_heldFits.find(mdxmHeader);
	if (fit == g_heldFits.end()) {
		std::vector<mcFitShape_t> shapes;
		FitHeld(static_cast<const mdxmHeader_t*>(mdxmHeader), shapes);
		fit = g_heldFits.emplace(mdxmHeader, std::move(shapes)).first;
	}
	return fit->second.empty() ? nullptr : &fit->second;
}

int MC_HeldShapesPose(const void* fitHandle, mcBoneFn_t boneFn, void* boneCtx, const vec3_t origin, const vec3_t axis[3],
	mcCapsule_t* out, int maxOut) {
	const int n = MC_BodyShapesPose(fitHandle, boneFn, boneCtx, origin, axis, out, maxOut);
	for (int i = 0; i < n; i++) out[i].hard = 4;	// a held thing: cloth is never modelled round it
	return n;
}

void MC_Clear(void) {
	g_fits.clear();
	g_heldFits.clear();
	for (mcGroup_t* g : g_groups) delete g;
	g_groups.clear();
	g_surfs.clear();
	for (auto& kv : g_instances) delete kv.second;
	g_instances.clear();
}

bool MC_IsClothSurface(const void* surfacePtr, const char* modelName, const char* surfaceName, const char* altModelName) {
	const mdxmSurface_t* surface = static_cast<const mdxmSurface_t*>(surfacePtr);
	auto it = g_surfs.find(surface);
	if (it != g_surfs.end()) {
		const mcSurfEntry_t& e = it->second;
		if (e.numVerts == surface->numVerts && e.numTris == surface->numTriangles && e.index == surface->thisSurfaceIndex) {
			return e.group != nullptr;
		}
		// same address, different surface: models were reloaded - start over
		std::vector<mcDef_t> keep = g_defs;
		MC_Clear();
		g_defs = keep;
	}

	char folder[MAX_QPATH];
	ModelFolder(modelName, folder, sizeof(folder));
	if (altModelName && altModelName[0] && !FindDef(folder, surfaceName)) {
		// SJE: no line for the load-path folder - fall back to the .glm's internal name
		char alt[MAX_QPATH];
		ModelFolder(altModelName, alt, sizeof(alt));
		if (FindDef(alt, surfaceName)) Q_strncpyz(folder, alt, sizeof(folder));
	}
	mcSurfEntry_t e;
	e.group = nullptr;
	e.surfIndex = -1;
	e.numVerts = surface->numVerts;
	e.numTris = surface->numTriangles;
	e.index = surface->thisSurfaceIndex;
	if (FindDef(folder, surfaceName)) {
		mcGroup_t* g = BuildGroup(surface, folder);
		g_groups.push_back(g);
		for (size_t si = 0; si < g->surfs.size(); si++) {
			mcSurfEntry_t ge;
			ge.group = g;
			ge.surfIndex = (int)si;
			ge.numVerts = g->surfs[si]->numVerts;
			ge.numTris = g->surfs[si]->numTriangles;
			ge.index = g->surfs[si]->thisSurfaceIndex;
			g_surfs[g->surfs[si]] = ge;
		}
		auto gi = g_surfs.find(surface);
		if (gi != g_surfs.end()) return gi->second.group != nullptr;
	}
	g_surfs[surface] = e;
	return false;
}

// SJE r_meshClothSettleCut. See tr_meshcloth.h. The views of a frame are kept (the level's view, a sky portal, a
// monitor: each has its like in the frame before), so only a view with no neighbour there counts as a cut.
void MC_ViewCut(const int frame, const int timeMs, const float* viewOrigin, const float* viewForward, const int mode, const int jobs) {
	g_cutMode = mode;
	if (frame != g_viewFrame) {
		if (frame == g_viewFrame + 1) {
			memcpy(g_viewPrev, g_viewCur, sizeof(g_viewPrev));
			g_viewPrevN = g_viewCurN;
			g_viewPrevTime = g_viewTime;
		}
		else g_viewPrevN = 0;	// frames without a view of the level in between (a menu, a map change): what comes now is a cut
		g_viewCurN = 0;
		g_viewFrame = frame;
		g_viewTime = timeMs;
	}
	for (int i = 0; i < g_viewCurN; i++) {
		if (!memcmp(g_viewCur[i].org, viewOrigin, sizeof(g_viewCur[i].org)) && !memcmp(g_viewCur[i].fwd, viewForward, sizeof(g_viewCur[i].fwd))) return;	// this view's other passes
	}
	if (g_viewCurN >= MC_CUT_VIEWS) return;
	mcView_t& v = g_viewCur[g_viewCurN++];
	mcCopy(viewOrigin, v.org);
	mcCopy(viewForward, v.fwd);
	const int dt = timeMs - g_viewPrevTime;
	bool cut = true;
	if (dt >= 0 && dt <= MC_CUT_GAP_MS) {
		const float ms = dt > 100 ? 100.0f : (float)dt;
		float distLim = MC_CUT_DIST_MS * ms, degLim = MC_CUT_DEG_MS * ms;
		if (distLim < MC_CUT_DIST) distLim = MC_CUT_DIST;
		if (degLim < MC_CUT_DEG) degLim = MC_CUT_DEG;
		if (degLim > MC_CUT_DEG_MAX) degLim = MC_CUT_DEG_MAX;
		const float cosLim = cosf(degLim * 0.01745329f);
		for (int i = 0; i < g_viewPrevN && cut; i++) {
			vec3_t d;
			mcSub(viewOrigin, g_viewPrev[i].org, d);
			if (mcDot(d, d) <= distLim * distLim && mcDot(viewForward, g_viewPrev[i].fwd) >= cosLim) cut = false;
		}
	}
	if (cut) {
		if (g_cutFrame != frame) g_cutRoom = MC_CUT_ROOM * ((jobs > 1 ? jobs : 1) + 2) / 3.0f;
		g_cutFrame = frame;
	}
}

bool MC_Prepare(const void* surfacePtr, const void* instanceKey, int timeMs, mcRun_t* run) {
	const mdxmSurface_t* surface = static_cast<const mdxmSurface_t*>(surfacePtr);
	auto sit = g_surfs.find(surface);
	if (sit == g_surfs.end() || !sit->second.group) return false;
	const mcGroup_t* g = sit->second.group;
	const int si = sit->second.surfIndex;
	if (g->surfs[si]->numVerts != surface->numVerts) return false;

	const unsigned long long key = (unsigned long long)(size_t)instanceKey * 31ULL ^ (unsigned long long)(size_t)g;
	mcInstance_t* in;
	auto iit = g_instances.find(key);
	if (iit == g_instances.end()) {
		in = new mcInstance_t;
		in->group = g;
		in->active = false;
		in->outTime = -1;
		g_instances[key] = in;
	}
	else {
		in = iit->second;
	}
	in->lastSeen = MC_WallMs();
	// SJE r_meshClothSettle (see g_settleCount)
	if (timeMs != g_settleTime) {
		g_settleCountLast = g_settleCount;
		g_settleCount = 0;
		g_settleTime = timeMs;
	}
	if (in->settleSeen != timeMs) {
		in->settleSeen = timeMs;
		const bool begins = !in->active || in->settleLeft > 0 || timeMs - in->lastTime > MC_STALE_MS || timeMs < in->lastTime;
		if (begins && in->lod != 2) g_settleCount++;
		// SJE r_meshClothSettleCut: cloth that starts over in the frame of a camera cut, or the one after, settles at
		// once while the cut's allowance lasts (by the cloth's size, as the settling steps are shared out). The
		// distance level is not known yet (in->lod is the last one, from before the cut): cloth that was far away
		// then is let through without counting - if it is still far it is not simulated at all.
		in->settleNow = false;
		if (begins && g_cutMode > 0 && g_viewFrame - g_cutFrame <= 1 && g_viewFrame >= g_cutFrame && g_cutRoom > 0.0f) {
			const float size = g->numParticles / MC_SETTLE_POINTS;
			if (in->lod != 2) g_cutRoom -= size > 0.25f ? size * sqrtf(size) : 0.125f;
			in->settleNow = true;
		}
	}

	// the group's lazily built data is shared by every character with this model: build it
	// here, on the main thread, so MC_Run (maybe on a job worker) only ever reads the group
	ComputeRootDist(g);
	ComputeWrap(g);	// SJE
	ComputeLayerPairs(g);	// SJE: r_meshClothLayers
	ComputeLinkAlpha(g);
	ComputeBendStencils(g);	// SJE: r_meshClothBendModel stencils

	run->instance = in;
	run->group = g;
	run->surfIndex = si;
	return true;
}

void MC_Run(const mcRun_t* run, const void* surfacePtr, int timeMs,
	const vec3_t origin, const vec3_t axis[3],
	const mcCapsule_t* caps, int ncaps, const mcParams_t* prm,
	mcBoneFn_t boneFn, void* boneCtx,
	float (*xyz)[4], float (*normal)[4]) {
	PROFILE_ZONE("Mesh cloth");
	const mdxmSurface_t* surface = static_cast<const mdxmSurface_t*>(surfacePtr);
	const mcGroup_t* g = static_cast<const mcGroup_t*>(run->group);
	mcInstance_t* in = static_cast<mcInstance_t*>(run->instance);
	const int si = run->surfIndex;

	// distance level of detail. Coming back in, the band edges are 10% nearer,
	// so a character standing on an edge doesn't flip between bands every frame.
	// lodDist < 0 (rend2 shadow map views): keep the level the camera's view chose - measured from a
	// light or the sun, every cape was "far away", so a shadow pass drawn first each frame froze it.
	int lod = 0;
	if (prm->lodDist < 0.0f) lod = in->lod;
	else if (prm->lodFar > 0.0f && prm->lodDist > prm->lodFar * (in->lod == 2 ? 0.9f : 1.0f)) lod = 2;
	else if (prm->lodNear > 0.0f && prm->lodDist > prm->lodNear * (in->lod >= 1 ? 0.9f : 1.0f)) lod = 1;
	in->lod = lod;
	if (lod == 2) {
		// far away: not simulated, the surface keeps its animated pose. The cloth
		// settles again from the pose when the character comes closer.
		in->active = false;
		return;
	}

	// the first cloth surface drawn this frame simulates the whole model;
	// the others (and shadow/mirror passes) reuse the result
	bool simulate = in->outTime != timeMs || !in->active;
	// mid distance: at most every MC_LOD_MS, with half the constraint passes. Frames in
	// between reuse the last result; the fixed 16 ms steps still all run on the next update.
	if (simulate && lod == 1 && in->active && timeMs >= in->outTime && timeMs - in->outTime < MC_LOD_MS) simulate = false;
	if (simulate) {
		mcParams_t pt = *prm;
		pt.timeSec = timeMs * 0.001f;
		if (lod == 1 && pt.iterations > 1) pt.iterations = (pt.iterations + 1) / 2;
		Simulate(g, in, timeMs, origin, axis, caps, ncaps, &pt, boneFn, boneCtx);
	}

	const int base = g->vertBase[si];
	for (int i = 0; i < surface->numVerts; i++) {
		const int p = g->vertToDraw[base + i];
		const float f = g->drawFree[p];
		if (f <= 0.0f && !(p < (int)in->drawPushed.size() && in->drawPushed[p])) continue;	// (SJE: or moved off a shoulder plate)
		mcCopy(&in->outLocal[p * 3], xyz[i]);
		// keep this vertex's side of the surface (two-sided robes, seams)
		const float* cn = &in->nrmLocal[p * 3];
		const float* sn = &in->skinNrm[(base + i) * 3];
		const float sgn = mcDot(cn, sn) < 0.0f ? -1.0f : 1.0f;
		const float b = f < 0.25f ? f * 4.0f : 1.0f;		// fade in near the attached edge
		for (int k = 0; k < 3; k++) normal[i][k] = normal[i][k] * (1.0f - b) + cn[k] * sgn * b;
		mcNorm(normal[i]);
	}
}

void MC_Apply(const void* surfacePtr, const void* instanceKey, int timeMs,
	const vec3_t origin, const vec3_t axis[3],
	const mcCapsule_t* caps, int ncaps, const mcParams_t* prm,
	mcBoneFn_t boneFn, void* boneCtx,
	float (*xyz)[4], float (*normal)[4]) {
	mcRun_t run;
	if (!MC_Prepare(surfacePtr, instanceKey, timeMs, &run)) return;
	MC_Run(&run, surfacePtr, timeMs, origin, axis, caps, ncaps, prm, boneFn, boneCtx, xyz, normal);
}

void MC_Blast(const float center[3], float strength, float radius, mcVisibleFn_t visible) {
	if (radius <= 1.0f) return;
	for (auto& kv : g_instances) {
		mcInstance_t* in = kv.second;
		if (!in || !in->active || in->p.empty()) continue;
		const int np = (int)(in->p.size() / 3);
		if (visible) {
			// one check per character, to the middle of its cloth: a blast behind a wall leaves it alone
			vec3_t mid = { 0, 0, 0 };
			for (int p = 0; p < np; p++) mcAdd(mid, &in->p[p * 3], mid);
			mcScale(mid, 1.0f / np, mid);
			vec3_t d;
			mcSub(mid, center, d);
			if (mcDot(d, d) > radius * radius * 4.0f || !visible(center, mid)) continue;
		}
		if (in->impulse.size() != in->p.size()) in->impulse.assign(in->p.size(), 0.0f);
		for (int p = 0; p < np; p++) {
			vec3_t d;
			mcSub(&in->p[p * 3], center, d);
			const float dist = mcLen(d);
			if (dist >= radius || dist < 1e-3f) continue;
			// falls off with distance; a little upward bias, as blast waves lift cloth
			const float sp = strength * (1.0f - dist / radius);
			mcScale(d, sp / dist, d);
			d[2] += 0.3f * sp;
			mcAdd(&in->impulse[p * 3], d, &in->impulse[p * 3]);
		}
	}
}

void MC_Push(const float center[3], const float dir[3], float strength, float radius, float coneDot) {
	if (radius <= 1.0f || strength == 0.0f) return;
	for (auto& kv : g_instances) {
		mcInstance_t* in = kv.second;
		if (!in || !in->active || in->p.empty()) continue;
		const int np = (int)(in->p.size() / 3);
		if (in->impulse.size() != in->p.size()) in->impulse.assign(in->p.size(), 0.0f);
		for (int p = 0; p < np; p++) {
			vec3_t d;
			mcSub(&in->p[p * 3], center, d);
			const float dist = mcLen(d);
			if (dist >= radius || dist < 1e-3f) continue;
			mcScale(d, 1.0f / dist, d);
			if (mcDot(d, dir) < coneDot) continue;	// behind or beside the pusher
			// strongest near the hand, nothing at the edge; mostly along the wave, a little up
			const float sp = strength * (1.0f - dist / radius);
			mcScale(d, sp, d);
			d[2] += 0.15f * fabsf(sp);
			mcAdd(&in->impulse[p * 3], d, &in->impulse[p * 3]);
		}
	}
}

void MC_Prune(int) {
	const int timeMs = MC_WallMs();
	if (timeMs - g_lastPrune < 1000 && timeMs >= g_lastPrune) return;
	g_lastPrune = timeMs;
	for (auto it = g_instances.begin(); it != g_instances.end(); ) {
		if (timeMs - it->second->lastSeen > MC_FORGET_MS || timeMs < it->second->lastSeen) {
			delete it->second;
			it = g_instances.erase(it);
		}
		else {
			++it;
		}
	}
}

// MC_WeatherState: per character, eased so nothing jumps (see tr_meshcloth.h). Main thread only.
struct mcWeather_t {
	float	outside = 0.0f;
	float	wet = 0.0f;
	int		time = 0;
};
static std::unordered_map<const void*, mcWeather_t> g_weather;

void MC_WeatherState(const void* instanceKey, int timeMs, bool outside, float wetTarget, float* outsideOut, float* wetOut) {
	auto found = g_weather.find(instanceKey);
	if (found == g_weather.end()) {
		if (g_weather.size() > 512) {
			// forget characters not seen for a while
			for (auto it = g_weather.begin(); it != g_weather.end(); ) {
				if (timeMs - it->second.time > MC_FORGET_MS || timeMs < it->second.time) it = g_weather.erase(it);
				else ++it;
			}
		}
		mcWeather_t w;
		w.outside = outside ? 1.0f : 0.0f;	// a new character starts where it is, no fade
		w.wet = Clamp01(wetTarget);
		w.time = timeMs;
		found = g_weather.emplace(instanceKey, w).first;
	}
	mcWeather_t& w = found->second;
	float dt = (timeMs - w.time) * 0.001f;
	if (dt < 0.0f || dt > 1.0f) dt = 1.0f;	// a reload or a long pause: catch up at once
	w.time = timeMs;
	const float o = outside ? 1.0f : 0.0f;
	const float os = dt / 0.4f;
	w.outside = w.outside < o ? (w.outside + os > o ? o : w.outside + os) : (w.outside - os < o ? o : w.outside - os);
	const float wt = Clamp01(wetTarget);
	if (wt >= 1.0f && w.wet < 1.0f && wetTarget > 1.0f) {
		w.wet = 1.0f;	// in water: soaked at once
	}
	else if (w.wet < wt) {
		w.wet += dt / 8.0f;
		if (w.wet > wt) w.wet = wt;
	}
	else if (w.wet > wt) {
		w.wet -= dt / 45.0f;
		if (w.wet < wt) w.wet = wt;
	}
	*outsideOut = w.outside;
	*wetOut = w.wet;
}

// MC_SetCharacters / MC_NearCharacters (r_meshClothOthers): other characters push cloth. Main thread.
static std::vector<float> g_charOrigins;
static int g_charFrame = -1;

void MC_SetCharacters(int frameId, const float (*origins)[3], int count) {
	g_charFrame = frameId;
	g_charOrigins.assign(&origins[0][0], &origins[0][0] + count * 3);
}

bool MC_CharactersListed(int frameId) {
	return g_charFrame == frameId;
}

int MC_NearCharacters(const float origin[3], mcCapsule_t* out, int max) {
	// a standing character as a capsule from the knees to the chest; the cloth of a robe reaches about
	// 40 units out, so only characters within NEAR units of this one can touch it
	const float NEAR = 72.0f;
	int n = 0;
	const int count = (int)g_charOrigins.size() / 3;
	for (int i = 0; i < count && n < max; i++) {
		const float* o = &g_charOrigins[i * 3];
		const float dx = o[0] - origin[0], dy = o[1] - origin[1], dz = o[2] - origin[2];
		const float d2 = dx * dx + dy * dy;
		if (d2 < 1.0f && fabsf(dz) < 1.0f) continue;	// this character itself
		if (d2 > NEAR * NEAR || fabsf(dz) > 64.0f) continue;
		mcCapsule_t& c = out[n++];
		c.a[0] = o[0]; c.a[1] = o[1]; c.a[2] = o[2] - 12.0f;
		c.b[0] = o[0]; c.b[1] = o[1]; c.b[2] = o[2] + 28.0f;
		c.r = 12.0f;
		c.hard = 0;
	}
	return n;
}