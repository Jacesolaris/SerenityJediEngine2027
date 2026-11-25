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
	const float	MC_NOROOM_FROM = 0.8f;	// SJE r_meshClothNoRoom: worn cloth is drawn to its pose from where its attached edge is
	const float	MC_NOROOM_SPAN = 0.3f;	// ... under this part of its length above the floor, in full this much further down,
	const float	MC_NOROOM_SHAPE = 0.05f;	// ... by this part of its distance per 16 ms step
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
		// SJE: heavy-cape options (all off unless the line asks for them; see MC_LoadConfig)
		float			dampH = 0.0f, dampV = 0.0f;	// 'damp H V': share of the cloth's speed (in a frame travelling with the character over the ground) taken per 16 ms, sideways / up-down
		float			ring = 0.0f;		// 'ring D': second tether level - every point further down is tied rigidly to the nearest point D units (along the cloth) below the attached edge
		float			armGap = 0.0f;	// SJE 'armgap G': arms keep G units more off this cloth, push it out in bigger steps, every substep (a cloak over the arms)
		float			freeC = 0.0f;	// SJE 'free F': over 0 = a free cape (r_meshClothFree; see MC_CAPE_UMAX)
		float			bell = 0.0f;		// 'bell B': 0..1 the free cloth's animated spot is its bind shape carried by the hip bone alone (a rigid bell), blended in by B x freedom
		float			spring = 0.0f, springFade = 0.0f;	// 'spring S A': pull per 16 ms toward the animated spot, S x exp(-A x metres along the cloth from the attached edge)
		bool			wrap = false;		// 'wrap': cloth on a grid ('proxy N') is drawn from the grid's triangles (each drawn vertex bound to the nearest few)
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

	const int MC_WRAP_K = 4;	// SJE 'wrap': grid triangles each drawn vertex is blended from
	// SJE r_meshClothSpringPose: 'spring' lets go when the pose itself jumps - by the speed of its own anchors against the
	// character's travel (units/s): from / fully. A walk's sway of the hips stays under it; an attack's first frames
	// move the cape's animated place at 200 - 290.
	const float MC_SPRING_POSE0 = 60.0f, MC_SPRING_POSE1 = 140.0f;
	const float MC_SPRING_BACK = 0.6f;	// SJE 'spring': seconds (time constant) it takes to take hold again after a run
	const float MC_WIND_DAMP = 0.15f;	// SJE 'damp' in wind of 500 units/s and more: this share of itself (the cape can flap)
	const float MC_WIND_FLUTTER = 0.9f;	// SJE: ripple strength (as r_meshClothFlutter) the cloth gets in wind of 1500 units/s and more
	const float MC_STORM_WEIGHT = 1.5f;	// SJE: in a storm lighter cloth is blown as cloth of this weight (the heavy cape's)
	const float MC_WIND_TAIL = 0.4f;	// SJE: wind from behind pushes cloth by this share of its forward part
	const float MC_LIFT_FALL0 = 150.0f;	// SJE r_meshClothLiftAir: air from below (a fall, an updraft) from which the lift limit opens
	const float MC_LIFT_FALL1 = 600.0f;	// ... and from which it is fully open (units/s)
	const float MC_LIFT_WIND0 = 500.0f;	// SJE: the same for wind over the ground (the strong weather wind gives 1000-1600)
	const float MC_LIFT_WIND1 = 1200.0f;
	const float MC_LIFT_OPEN = 1.0f;	// SJE: fully open, a point may rise this part of its cloth length above the attached edge
	const float MC_LIFT_BEHIND = 8.0f;	// SJE: ... but only behind that edge: fully from this far behind it, not at all in front
	const float MC_LIFT_CLOSE = 2.0f;	// SJE: when the air stops the opening closes by this much per second (no snap on landing)

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
		// SJE: heavy-cape options (per particle; empty unless a line of the group asks for one - anyHeavy)
		std::vector<float>					dampH, dampV, bellW, springS, springA, ringD;
		bool								anyHeavy = false, anyDamp = false, anyBell = false, anySpring = false, anyRing = false;
		float								armGap = 0.0f;	// SJE 'armgap': the largest of the group's lines (0 = none)
		mutable std::vector<int>			ringAnchor;		// per particle: the ring point it is tied to (-1 none; ComputeRing, lazy)
		mutable std::vector<float>			ringLen;		// ... its bind distance along the cloth to it
		mutable std::vector<float>			springK;		// per particle: spring per 16 ms (ComputeRing, lazy)
		mutable bool						ringBuilt = false;
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
		mutable std::vector<float>			sheetLen;		// SJE per sheet: its length = the longest bind distance from its attached edge (ComputeRootDist, lazy)
		mutable std::vector<unsigned char>	high;			// SJE per particle: 1 = it hangs from shoulder height or above (BuildPlatePairs, main thread; read only once plateBuilt is set)
		mutable std::vector<unsigned char>	wrap;			// SJE per particle: 1 = its surface wraps round the body at its height (ComputeWrap, lazy)
		mutable std::vector<unsigned char>	under;			// SJE per particle: 1 = a loose layer the model has UNDER worn cloth there (ComputeLayerPairs, lazy)
		mutable std::vector<int>			insidePts;		// SJE r_meshClothInside: the free points of loose layers under worn cloth (BuildInside, lazy)
		mutable std::vector<int>			insideTris;		// SJE r_meshClothInside: the triangles of the worn tube
		mutable std::vector<unsigned>		insideSewn;		// SJE r_meshClothInside: per surface, the surfaces sewn to it (bits)
		mutable bool						insideBuilt = false;
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
		mutable std::vector<float>			bendDen;		// per stencil: its weights squared, summed over its free points
		// SJE r_meshClothFine: the cloth's spacing - the median bind length of the edges of what is simulated
		// (the grid's, for cloth on a grid) - and which stencils lie on a loose sheet that collides with itself (a
		// cape, a cloak: no point of theirs is worn round the body, ComputeWrap, and all four take part in the
		// self-collision), with the bend links those stencils stand in for. Built with the stencils.
		mutable float						spacing = -1.0f;
		mutable std::vector<float>			partArea;		// per particle: the cloth area it stands for (bind pose)
		mutable std::vector<char>			bendLoose;		// per stencil
		mutable std::vector<int>			bendLooseIdx;	// the loose stencils that bend (in order)
		mutable std::vector<char>			linkLoose;		// per link
		mutable bool						anyLoose = false;
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
		// SJE 'wrap' (grid only): per drawn point the grid triangles it is drawn from (-1 none), its
		// place in each triangle's plane (3 barycentric weights, may be a little outside) and its distance off the plane
		bool								wrapOn = false;
		std::vector<int>					wrapTri;		// 'wrap': per drawn vertex MC_WRAP_K grid triangles (-1: none)
		std::vector<float>					wrapBc, wrapH, wrapW, wrapA;	// ... where it lies in each (3 each), how far off each, each one's share, its bind size (twice its area)
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
		float				sideLine = 0.0f;	// SJE r_meshClothTailSide: 0 .. 1 how much of a line the attached edge is (0 = a ring, 1 = straight)
		vec3_t				fwd = {};			// SJE: the character's facing (world, its entity axis; wind from behind)
		float				liftAir = 0.0f;		// SJE r_meshClothLiftAir: the lift limit's opening (closes slowly, Step)
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
		std::vector<unsigned char>	limbThrown;	// SJE r_meshClothLimbLie: > 0 = an arm's triangle test threw this point within the last steps
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
		unsigned int		turbSeed = 0;			// SJE r_meshClothTurbulence: this cloth's own random sequence (MC_Prepare)
		float				airFree = 0.0f;			// SJE r_meshClothAirFree: 0..1 how far moving air has freed the cloth (eased, Step)
		std::vector<float>	linkAlphaAir;			// ... this step's link compliances (the bend links give)
		std::vector<float>	expoP;					// SJE r_meshClothAirFree: per point, how far the air's eddies reach it (see MC_TURB_WALL)
		std::vector<float>	freeP;					// ... per point: how free in this step (0 where the air presses it onto its wearer)
		float				tailSide = 0.0f;		// SJE r_meshClothTailSide: the lee side of the last side wind (-1 .. +1; 0 = none yet)
		float				capeArm = 0.0f;			// SJE r_meshClothFree: this step's arm gap for a group with free cloth (MC_CAPE_ARM; 0 = the lines')
		float				tailUse = 0.0f;			// ... the side the cape is pushed to now (-1 / +1; 0 = none)
		float				tailFirst = 0.0f;		// ... the side it tried first
		int					tailTry = 0;			// ... 0 = on its first side, 1 = on the other, 2 = settled
		float				tailRamp = 0.0f, tailD = 0.0f, tailT = 0.0f, tailGone = 0.0f;	// ... the push's ramp, how far out (eased), seconds not out, seconds without tail wind
		float				tailBest[2] = { 0.0f, 0.0f };	// ... how far out the first / the other side was when it gave up
		float				tailBlade = 0.0f;		// ... 0 .. 1: a lit blade is held (eased; the push is stronger then, MC_TAIL_PUSH_BLADE)
		float				tailBladeT = 0.0f;		// ... seconds a lit blade has been on the side in use (MC_TAIL_BLADE_GIVE)
		bool				tailBladeGave = false;	// ... the side was given up to a blade since the rule started
		bool				tailWas = false;			// ... it has been out on the side in use
		std::vector<float>	tailRef, tailRefK;		// ... the wind reference place per point (3 each) and how far it holds (0 = the animated place); empty = none
		std::vector<unsigned char>	tailHi;			// ... per point: 1 = a cape's cloth the rule is pushing (it keeps its side of a body shape that moves)
		std::vector<unsigned char>	tailCapOn;		// ... per body shape of this step: 1 = it has moved (MC_TAIL_REF_MOVE): both hold against it
		int					tailRefMode = 0;		// ... which of the two is in use (bits: 1 the reference place, 4 the side at the step's start)
		std::vector<float>	tailA;					// ... this step's push per point (units/s^2; empty = none)
		float				landAir = 0.0f;			// ... 0..1 how lately its wearer fell (opens with the fall's air, fades after: the landing)
		bool				turbReady = false;		// ... its eddies are made (TurbInit)
		double				turbWander = 0.0;		// ... the clock of the stream's wander (noise cells; runs with the air's speed)
		double				turbPsi[8] = {};		// ... each eddy wave's phase (carried with the air, and turning on its own)
		float				turbK[8][3] = {}, turbQ[8][3] = {}, turbA[8] = {}, turbW[8] = {};	// ... wave vector, air direction, size, own turn
		const mcCapsule_t* faceCapsFrom = nullptr;	// the body shapes then (valid during the step)
		// SJE: per-triangle air (r_meshClothAero), isometric bending multipliers (one per stencil)
		std::vector<float>	aeroDv, aeroW, bendLambda;
		// SJE: heavy-cape options ('bell', 'damp', 'spring'; only for groups that use them):
		std::vector<float>	bellNow, bellPrev, bellSim;	// the hip-carried bind shape (world, 3 per particle) this frame, the last, at the last step
		std::vector<float>	bellFrom, bellTo;		// ... at the start and the end of the 16 ms step being run (Simulate sets them)
		std::vector<float>	anchor;					// per substep: each point's animated spot ('bell' blended in, never under the floor)
		bool				bellOk = false;			// bellFrom / bellTo hold this step's hip-carried shape
		vec3_t				dampVel = {};			// SJE r_meshClothDampLag: the speed of the frame 'damp' works in (follows originVel with a lag)
		bool				dampVelOk = false;
		float				turnRate = 0.0f, turnCalm = 0.0f;	// SJE r_meshClothTurnCalm: the attached edge's turn rate about up (degrees/s, eased) / 0 .. 1 how far it calms the cloth
		float				turnSigned = 0.0f;		// ... the eased turn rate with its direction (turnRate is its size)
		float				whirlRate = 0.0f;		// SJE r_meshClothWhirl: the attached edge's turn rate about up with its direction (degrees/s, eased)
		float				whirlTurn = 0.0f, whirlBack = 0.0f;	// ... degrees it has turned on in one direction (fading) / turned back since
		float				whirlPath = 0.0f, whirlPathBack = 0.0f;	// ... the same for his direction of travel
		float				whirlW = 0.0f;			// ... 0 .. 1 how far the rule is on
		vec3_t				whirlAcc = {};			// ... the origin's acceleration across its travel over the ground (units/s^2, eased)
		std::vector<float>	whirlA;				// ... per point: what it is carried with in this step (units/s^2; empty = none)
		std::vector<float>	whirlV;				// ... per point: the speed of the frame 'damp' works in (units/s; empty = his own, in->dampVel)
		float				springRun = 0.0f;		// 'spring': 0..1, how far the wearer's run has let it go (eased in and out)
		std::vector<float>	bendH0, bendH1, bendH;	// SJE: per stencil, the animated cloth's curvature at the step's start, at its end, in this substep
		std::vector<float>	bendTgt1;				// SJE: the animated cloth bendH1 was worked out from (a step starts where the last one ended)
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
			if (r->anyHeavy) {	// SJE: heavy-cape options, from the grid point's own vertex
				g->dampH.push_back(r->dampH[s]); g->dampV.push_back(r->dampV[s]); g->bellW.push_back(r->bellW[s]);
				g->springS.push_back(r->springS[s]); g->springA.push_back(r->springA[s]); g->ringD.push_back(r->ringD[s]);
			}
			mcCopy(&r->bindPos[s * 3], &g->bindPos[i * 3]);
		}
		g->anyHeavy = r->anyHeavy; g->anyDamp = r->anyDamp; g->anyBell = r->anyBell; g->anySpring = r->anySpring; g->anyRing = r->anyRing; g->armGap = r->armGap;
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
		// SJE: ... and every vertex has its grid particle (the one whose region its point lies in). The rules added
		// later read this table for the grid as for a plain group (which surfaces are sewn together, which
		// surfaces use a particle); without it a model with "proxy" and two cloth surfaces read past an empty
		// table and crashed when it was loaded.
		g->vertToParticle.assign(r->vertToParticle.size(), -1);
		for (size_t v = 0; v < r->vertToParticle.size(); v++) {
			const int rp = r->vertToParticle[v];
			if (rp >= 0 && rp < n) g->vertToParticle[v] = region[rp];
		}
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
		// SJE 'wrap': every drawn vertex is bound to the grid triangles nearest to it in the bind
		// pose (up to MC_WRAP_K that face the same way) - where it lies in each triangle's plane (barycentric, also a
		// little outside: the hem beyond the last grid row) and how far off it - and drawn from their blend as they are
		// now. The blend above, smoothed after, drew the hem up to several units inside the grid's own hem in a swing.
		// From the nearest triangle alone the dense cape showed the grid's flat facets and the creases between
		// them - corners in its outline on the round shoulders and back. Only the grid's own region and its
		// neighbours are looked at (the same sheet).
		for (const mcDef_t* d : r->defs) if (d->wrap) g->wrapOn = true;
		if (g->wrapOn) {
			std::vector<std::vector<int>> triOf(m);
			const int nt = (int)(g->tris.size() / 3);
			for (int t = 0; t < nt; t++) for (int k = 0; k < 3; k++) triOf[g->tris[t * 3 + k]].push_back(t);
			// the grid's spacing: how far apart the blended triangles may lie and still count nearly as much
			float spacing = 0.0f; int nl = 0;
			for (size_t l = 0; l < g->linkA.size(); l++) {
				if (g->linkBend[l]) continue;
				vec3_t dl; mcSub(&g->bindPos[g->linkA[l] * 3], &g->bindPos[g->linkB[l] * 3], dl);
				spacing += mcLen(dl); nl++;
			}
			spacing = nl ? spacing / nl : 1.0f;
			const float soft = 0.25f * spacing * spacing;	// (half a spacing, squared)
			g->wrapTri.assign((size_t)n * MC_WRAP_K, -1);
			g->wrapBc.assign((size_t)n * MC_WRAP_K * 3, 0.0f);
			g->wrapH.assign((size_t)n * MC_WRAP_K, 0.0f);
			g->wrapW.assign((size_t)n * MC_WRAP_K, 0.0f);
			g->wrapA.assign((size_t)n * MC_WRAP_K, 0.0f);
			struct wrapCand_t { float dist; int t; float bc[3]; float h; vec3_t nf; float area; };
			std::vector<int> cand;
			std::vector<wrapCand_t> found;
			for (int v = 0; v < n; v++) {
				const float* p = &r->bindPos[v * 3];
				cand.clear(); found.clear();
				const int own = region[v];
				for (int t : triOf[own]) cand.push_back(t);
				for (int q : nbr[own]) for (int t : triOf[q]) cand.push_back(t);
				std::sort(cand.begin(), cand.end());
				cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
				for (int t : cand) {
					const float* a = &g->bindPos[g->tris[t * 3] * 3], * b = &g->bindPos[g->tris[t * 3 + 1] * 3], * c = &g->bindPos[g->tris[t * 3 + 2] * 3];
					vec3_t e0, e1, ep, nf;
					mcSub(b, a, e0); mcSub(c, a, e1); mcSub(p, a, ep);
					mcCross(e0, e1, nf);
					const float area = mcNorm(nf);
					if (area < 1e-6f) continue;
					const float d00 = mcDot(e0, e0), d01 = mcDot(e0, e1), d11 = mcDot(e1, e1), d20 = mcDot(ep, e0), d21 = mcDot(ep, e1);
					const float den = d00 * d11 - d01 * d01;
					if (fabsf(den) < 1e-9f) continue;
					const float bv = (d11 * d20 - d01 * d21) / den, bw = (d00 * d21 - d01 * d20) / den, bu = 1.0f - bv - bw;
					// distance to the triangle itself (barycentric clamped), to choose the nearest
					float cu = bu < 0.0f ? 0.0f : bu, cv = bv < 0.0f ? 0.0f : bv, cw = bw < 0.0f ? 0.0f : bw;
					const float cs = cu + cv + cw;
					cu /= cs; cv /= cs; cw /= cs;
					vec3_t q;
					for (int k = 0; k < 3; k++) q[k] = cu * a[k] + cv * b[k] + cw * c[k];
					vec3_t dq; mcSub(p, q, dq);
					wrapCand_t wc;
					wc.area = area;
					wc.dist = mcDot(dq, dq); wc.t = t; wc.bc[0] = bu; wc.bc[1] = bv; wc.bc[2] = bw;
					wc.h = mcDot(ep, nf);	// (off the plane, along the triangle's normal)
					mcCopy(nf, wc.nf);
					found.push_back(wc);
				}
				if (found.empty()) continue;
				std::sort(found.begin(), found.end(), [](const wrapCand_t& x, const wrapCand_t& y) { return x.dist < y.dist; });
				// the nearest, and up to three more that face the same way (not a fold of the sheet, not another layer)
				float tot = 0.0f;
				int k = 0;
				for (size_t j = 0; j < found.size() && k < MC_WRAP_K; j++) {
					if (j > 0 && (mcDot(found[j].nf, found[0].nf) < 0.7f || found[j].dist > 4.0f * spacing * spacing)) continue;
					const size_t s = (size_t)v * MC_WRAP_K + k;
					g->wrapTri[s] = found[j].t;
					for (int e = 0; e < 3; e++) g->wrapBc[s * 3 + e] = found[j].bc[e];
					g->wrapH[s] = found[j].h;
					g->wrapA[s] = found[j].area;
					g->wrapW[s] = 1.0f / (found[j].dist + soft);
					tot += g->wrapW[s];
					k++;
				}
				for (int j = 0; j < k; j++) g->wrapW[(size_t)v * MC_WRAP_K + j] /= tot;
			}
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
		// SJE: heavy-cape options - only a group with a line that asks for one gets them (anyHeavy)
		for (const mcDef_t* d : g->defs) {
			if (d->dampH > 0.0f || d->dampV > 0.0f) g->anyDamp = true;
			if (d->bell > 0.0f) g->anyBell = true;
			if (d->spring > 0.0f) g->anySpring = true;
			if (d->ring > 0.0f) g->anyRing = true;
			if (d->armGap > g->armGap) g->armGap = d->armGap;	// SJE 'armgap'
		}
		g->anyHeavy = g->anyDamp || g->anyBell || g->anySpring || g->anyRing;
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
					if (g->anyHeavy) {
						const mcDef_t* d = g->defs[si];
						g->dampH.push_back(d->dampH); g->dampV.push_back(d->dampV); g->bellW.push_back(d->bell);
						g->springS.push_back(d->spring); g->springA.push_back(d->springFade); g->ringD.push_back(d->ring);
					}
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
					if (g->anyHeavy) {	// SJE: heavy-cape options: the stronger one wins
						const mcDef_t* d = g->defs[si];
						if (d->dampH > g->dampH[p]) g->dampH[p] = d->dampH;
						if (d->dampV > g->dampV[p]) g->dampV[p] = d->dampV;
						if (d->bell > g->bellW[p]) g->bellW[p] = d->bell;
						if (d->spring > g->springS[p]) { g->springS[p] = d->spring; g->springA[p] = d->springFade; }
						if (d->ring > 0.0f && (g->ringD[p] <= 0.0f || d->ring < g->ringD[p])) g->ringD[p] = d->ring;
					}
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
	static void FaceCollide(const mcGroup_t* g, mcInstance_t* in, const mcCapsule_t* caps, int ncaps, int overArms, int rest, int ease, int lie, int armOn);

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
		g->bendSt.clear(); g->bendW.clear(); g->bendAlpha.clear(); g->bendBs.clear(); g->bendDen.clear();
		g->bendLoose.clear(); g->bendLooseIdx.clear(); g->linkLoose.assign(g->linkA.size(), 0); g->anyLoose = false;
		if ((int)g->bindPos.size() < g->numParticles * 3) return;
		const bool wrapOk = (int)g->wrap.size() == g->numParticles;
		std::unordered_map<unsigned long long, char> looseLink;	// the two opposite points of every loose stencil
		{
			// SJE r_meshClothFine: the sheet's spacing
			std::vector<float> len;
			len.reserve(g->linkA.size());
			for (size_t l = 0; l < g->linkA.size(); l++) {
				if (g->linkBend[l]) continue;
				vec3_t d;
				mcSub(&g->bindPos[g->linkA[l] * 3], &g->bindPos[g->linkB[l] * 3], d);
				len.push_back(mcLen(d));
			}
			if (!len.empty()) {
				std::nth_element(len.begin(), len.begin() + len.size() / 2, len.end());
				g->spacing = len[len.size() / 2];
			}
			// ... and the cloth area each point stands for (a third of each of its triangles)
			g->partArea.assign((size_t)g->numParticles, 0.0f);
			for (size_t t = 0; t + 2 < g->tris.size(); t += 3) {
				const int a = g->tris[t], b = g->tris[t + 1], c = g->tris[t + 2];
				if (a < 0 || b < 0 || c < 0 || a >= g->numParticles || b >= g->numParticles || c >= g->numParticles) continue;
				vec3_t e1, e2, n;
				mcSub(&g->bindPos[b * 3], &g->bindPos[a * 3], e1); mcSub(&g->bindPos[c * 3], &g->bindPos[a * 3], e2); mcCross(e1, e2, n);
				const float third = mcLen(n) * (0.5f / 3.0f);
				g->partArea[a] += third; g->partArea[b] += third; g->partArea[c] += third;
			}
		}
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
				float bs = 0.0f, den = 0.0f;
				for (int j = 0; j < 4; j++) {
					g->bendSt.push_back(st[j]); g->bendW.push_back(w[j]); bs += 0.25f * g->bend[st[j]];
					if (g->freedom[st[j]] > 0.0f) den += w[j] * w[j];
				}
				g->bendDen.push_back(den);
				g->bendBs.push_back(bs);
				g->bendAlpha.push_back(BendCompliance(bs));
				// SJE r_meshClothFine: a stencil of a loose sheet that collides with itself. Not cloth without self-collision:
				// head tails, strips, flaps and hip cloths of the game's dense models went calmer in motion with the
				// stencils but restless where a pose was held (a crouch: Shaak Ti's head tails 0.19 -> 1.9 degrees a step,
				// pushed to and fro by the layer rule; Bultar, Jaro Tapal, Ahsoka crouching), and none of them has the
				// folds of a cape that the bend links fail to hold. They keep their bend links.
				const bool selfOk = (int)g->selfCol.size() == g->numParticles && g->selfCol[x0] && g->selfCol[x1] && g->selfCol[x2] && g->selfCol[x3];
				const bool loose = (!wrapOk || (!g->wrap[x0] && !g->wrap[x1] && !g->wrap[x2] && !g->wrap[x3])) && selfOk;
				g->bendLoose.push_back(loose ? 1 : 0);
				if (loose && bs > 0.0f && den >= 1e-6f) {
					looseLink[ekey(x2, x3)] = 1; g->anyLoose = true;
					if (g->bendAlpha.back() >= 0.0f) g->bendLooseIdx.push_back((int)g->bendLoose.size() - 1);
				}
			}
		}
		for (size_t l = 0; l < g->linkA.size(); l++) {
			if (g->linkBend[l] && looseLink.count(ekey(g->linkA[l], g->linkB[l]))) g->linkLoose[l] = 1;
		}
	}

	// SJE r_meshClothFree: which points are free cloth (their line has 'free F'; not cloth worn round the body or a layer
	// under it). Returns whether any is.
	static bool FreeCapePoints(const mcGroup_t* g, const mcParams_t* prm, std::vector<unsigned char>* out) {
		if (out) out->clear();
		if (!prm || !(prm->freeCape > 0.0f) || !(prm->airPhys > 0.0f) || (int)g->partSurf.size() != g->numParticles) return false;
		bool any = false;
		for (const mcDef_t* d : g->defs) if (d && d->freeC > 0.0f) { any = true; break; }
		if (!any) return false;
		const bool worn = (int)g->wrap.size() == g->numParticles && (int)g->under.size() == g->numParticles;
		any = false;
		if (out) out->assign((size_t)g->numParticles, 0);
		for (int p = 0; p < g->numParticles; p++) {
			const int si = g->partSurf[p];
			if (si < 0 || si >= (int)g->defs.size() || !g->defs[si] || !(g->defs[si]->freeC > 0.0f)) continue;
			if (worn && (g->wrap[p] || g->under[p])) continue;
			any = true;
			if (!out) return true;
			(*out)[p] = 1;
		}
		return any;
	}

	// Solver settings for one substep, from mcParams_t
	struct mcSolve_t {
		float	bendScale = 1.0f;	// exponent for the old projection's bend (1 / substeps)
		float	h = 0.0f;			// substep length (s)
		int		xpbd = 0;		// 1: XPBD, 0: old projection
		int		xpbdIters = 0;	// XPBD passes per substep
		float	liftLimit = -1.0f;	// -2 = per surface, -1 = no limit, N = units
		float	liftSlope = 0.0f;	// SJE: the lift limit drops by this much per unit of cloth length from the attached edge
		int		floorOk = 0;	// SJE: the floor under the character (the sloped lift limit never lies under it)
		float	floorPt[3] = { 0.0f, 0.0f, 0.0f }, floorN[3] = { 0.0f, 0.0f, 1.0f };
		int		edgeCol = 0;	// 1: cloth edges against arms, fists and hilts
		int		faceCol = 0;	// SJE: 1: cloth triangles against every body shape (FaceCollide; replaces edgeCol)
		int		faceNow = 0;	// SJE: run it in this substep (every other one, and the last)
		int		overArms = 0;	// SJE r_meshClothOverArms: cloth hanging from shoulder height lies over arms and hands
		int		rest = 0;		// SJE r_meshClothRest: 1 = on
		int		ease = 0;		// SJE r_meshClothEase: 1 = on
		int		limbLie = 0;	// SJE r_meshClothLimbLie: cloth lying against a still arm is not thrown again (FaceCollide)
		int		armGapOn = 0;	// SJE r_meshClothArmGap: honour the lines' 'armgap'
		float	tetherK = 0.0f;	// SJE: fraction of the over-tether distance reeled in per substep (per-step constant)
		const float* bendK = nullptr;	// SJE: per link, old projection's bend fraction per substep (nullptr = work it out)
		const float* linkAlpha = nullptr;	// SJE r_meshClothAirFree: per link, this step's XPBD compliance (nullptr = the group's)
		bool liftGrad = false;	// SJE r_meshClothAirFree: a point over the lift limit takes the nearest way back under it (not straight down)
		bool liftOpenAll = false;	// SJE r_meshClothAirPhys: the lift limit is open in every step (as liftAir 1), for cloth that hangs from the shoulders
		const unsigned char* liftLow = nullptr;	// ... per point: 1 = it hangs from below the trunk (a hip skirt; MC_AIRPHYS_HIP): the limit stays closed
		const unsigned char* liftFree = nullptr;	// SJE r_meshClothFree: per point, 1 = no lift limit
		const float* freeP = nullptr;	// SJE r_meshClothAirFree: per point, how free in this step (nullptr = none is)
		float	freeTether = 0.0f;		// ... 'tether' x (1 + this x freeP) for loose cloth
		const unsigned char* wornWrap = nullptr, * wornUnder = nullptr;	// ... which points are worn cloth (g->wrap / g->under)
		int		bendModel = 0;	// SJE: 1 = isometric bending stencils instead of the bend links; 2 = for loose cloth only, each
							//   stencil as soft as the surface's 'bend' says, whatever its shape (r_meshClothFine)
		const float* bendH = nullptr;	// SJE: per stencil, the animated cloth's curvature in this substep (MC_BEND_NONE = none)
		float	bendTrip = 0.0f;	// SJE: the shake watchdog's trip count (bendModel 2: a stencil with a shaking point lets go)
		const int* ptStart = nullptr;			// SJE: per point, the body shapes it can reach in this step (BuildPointCaps):
		const unsigned char* ptIdx = nullptr;	// ... ptIdx[ptStart[p]] to ptIdx[ptStart[p + 1] - 1]; nullptr = test all shapes
		const float* ptFrom = nullptr;			// ... where the points were when the lists were built (3 per point)
		const float* ptAllow = nullptr;			// ... and how far from there each list holds
		int		bell = 0;		// SJE 'bell': the tether measures from in->anchor for points with a bell weight
		int		ring = 0;		// SJE 'ring': the second tether level (ringAnchor / ringLen)
		float	ringGive = 1.0f;	// SJE r_meshClothTurnCalm: ... its length x this
		float	liftAir = 0.0f;	// SJE r_meshClothLiftAir: 0..1 how far the lift limit is open (air from below, strong wind)
		float	liftFwd[3] = { 0.0f, 0.0f, 0.0f };	// SJE: the character's facing over the ground (open only behind the edge)
		const unsigned char* liftWrap = nullptr;	// SJE: worn cloth keeps the limit (g->wrap / g->under; nullptr = not known)
		const unsigned char* liftUnder = nullptr;
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
	const float MC_BEND_NONE = 1e30f;	// no curvature to hold: the animated stencil is folded flat onto itself
	// one stencil's curvature along its own normal, for the points in 'pos' (3 per particle); false = no normal
	static inline bool StencilCurve(const mcGroup_t* g, size_t s, const float* pos, float* n, float* h) {
		const int* st = &g->bendSt[s * 4];
		const float* w = &g->bendW[s * 4];
		const float* x[4] = { pos + st[0] * 3, pos + st[1] * 3, pos + st[2] * 3, pos + st[3] * 3 };
		vec3_t H = { 0, 0, 0 };
		for (int j = 0; j < 4; j++) mcMA(H, w[j], x[j], H);
		StencilNormal(x[0], x[1], x[2], x[3], n);
		if (mcDot(n, n) < 0.5f) return false;	// folded flat onto itself
		*h = mcDot(H, n);
		return true;
	}
	// SJE: the animated cloth's curvature per stencil. Worked out for the two ends of a step (Step) and blended
	// for each substep, instead of once per stencil in every pass of every substep: a third of the stencils' cost.
	static void BendTargets(const mcGroup_t* g, const std::vector<float>& tgt, std::vector<float>& out, bool looseOnly) {
		const size_t ns = g->bendAlpha.size();
		if (looseOnly) {
			// only the loose stencils that bend (bendLooseIdx); the others hold nothing
			out.assign(ns, MC_BEND_NONE);
			for (const int s : g->bendLooseIdx) {
				vec3_t n;
				float h = 0.0f;
				if (StencilCurve(g, (size_t)s, tgt.data(), n, &h)) out[s] = h;
			}
			return;
		}
		out.resize(ns);
		for (size_t s = 0; s < ns; s++) {
			vec3_t n;
			float h = 0.0f;
			out[s] = g->bendAlpha[s] >= 0.0f && StencilCurve(g, s, tgt.data(), n, &h) ? h : MC_BEND_NONE;
		}
	}
	// SJE r_meshClothFine (bendModel 2): the stencils of loose cloth only, and each stencil gives by what its
	// surface's 'bend' says. As the stencils were (bendModel 1) the give was added to the stencil's squared weights:
	// a stencil of two even triangles took a quarter to a half of its fold out per substep, one with a thin triangle
	// (weights up to 40) all of it - rigid, and rigid against the body's shapes: flaps, head tails and hip cloths
	// lying on a crouching body shook for as long as the pose was held, a robe even standing. Now the give counts
	// against the stencil's own squared weights (as a link's does against its two ends), so every stencil takes
	// the same part of its fold out per substep; and a stencil with a point the shake watchdog has tripped lets go,
	// as the layer rule and the triangle test do.
	static void SolveBendStencils(const mcGroup_t* g, mcInstance_t* in, const mcSolve_t& sv) {
		const size_t ns = g->bendAlpha.size();
		if (!ns || !sv.bendH || g->bendSt.size() != ns * 4 || g->bendW.size() != ns * 4 || g->bendDen.size() != ns) return;
		if (in->bendLambda.size() != ns) in->bendLambda.resize(ns);
		std::fill(in->bendLambda.begin(), in->bendLambda.end(), 0.0f);
		const float ih2 = 1.0f / (sv.h * sv.h);
		const int iters = !sv.xpbd ? 1 : (sv.xpbdIters < 1 ? 1 : (sv.xpbdIters > 8 ? 8 : sv.xpbdIters));
		const bool fine = sv.bendModel == 2 && g->bendLoose.size() == ns;
		const bool watch = fine && sv.bendTrip > 0.0f && in->fight.size() == (size_t)g->numParticles;
		const size_t nl = fine ? g->bendLooseIdx.size() : ns;	// (fine: only the loose stencils, in the same order)
		for (int it = 0; it < iters; it++) {
			for (size_t k = 0; k < nl; k++) {
				const size_t s = fine ? (size_t)g->bendLooseIdx[k] : k;
				const float alpha = g->bendAlpha[s], den = g->bendDen[s], ht = sv.bendH[s];
				if (alpha < 0.0f || den < 1e-6f || ht >= MC_BEND_NONE) continue;
				if (fine && !g->bendLoose[s]) continue;
				const int* st = &g->bendSt[s * 4];
				if (watch && (in->fight[st[0]] >= sv.bendTrip || in->fight[st[1]] >= sv.bendTrip || in->fight[st[2]] >= sv.bendTrip || in->fight[st[3]] >= sv.bendTrip)) continue;
				const float* w = &g->bendW[s * 4];
				float wi[4];
				for (int j = 0; j < 4; j++) wi[j] = g->freedom[st[j]] > 0.0f ? 1.0f : 0.0f;
				vec3_t nx;
				float hx = 0.0f;
				if (!StencilCurve(g, s, in->p.data(), nx, &hx)) continue;
				const float C = hx - ht;
				float dl;
				if (sv.xpbd) {
					const float at = fine ? alpha * ih2 * 0.5f * den : alpha * ih2;
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
	// SJE r_meshClothFine ('fine': the cloth's spacing is under r_meshClothFine). In a crouch the skinned cape is
	// crushed between the body and the ground, and the links take their lengths from that crushed shape: they pull
	// the cloth into folds tighter than the thickness, the self-collision pushed it out again, and as the separation
	// keeps the speed the pair had ('a separation, not a throw') the two points pressed into each other in every
	// substep with about 100 units/s - stored, never at rest, and let out in a jolt every second or so that woke
	// the whole cape (a dense cape never came to rest in a crouch; 2.5 - 4 times the normal cape's tilt per step).
	// In a fine mesh such pairs are only a few edges apart (the dense Vader cape: 6 - 7 units of cloth that the
	// crouching pose lays 1.3 - 2.5 units apart); in a coarse mesh no points are that close. So in fine cloth:
	// - a pair the POSE lays closer together than the thickness is kept apart only as far as the pose has it;
	// - every touching pair loses its closing speed, not only one that shakes (but not where a limb moved the
	//   cloth): the speed the links put into a pressed pair is not stored.
	static void SolveSelfPairs(const mcGroup_t* g, mcInstance_t* in, float frictionScale, bool calm, float trip, bool fine = false) {
		if (in->selfPairs.empty()) return;
		const bool watch = calm && in->fight.size() == (size_t)g->numParticles;
		const bool limbs = in->limbPush.size() == (size_t)g->numParticles;
		const float thick = g->selfThick * in->linkScale;
		if (!(thick > 0.05f) || thick > 64.0f) return;
		const bool pose = fine && in->target.size() == in->p.size();
		for (size_t i = 0; i + 1 < in->selfPairs.size(); i += 2) {
			const int p = in->selfPairs[i], q = in->selfPairs[i + 1];
			float* a = &in->p[p * 3], * b = &in->p[q * 3];
			vec3_t d;
			mcSub(a, b, d);
			const float len = mcLen(d);
			if (len >= thick || len < 1e-5f) continue;
			float apart = thick;	// the distance the pair is kept at
			if (pose) {
				vec3_t dt;
				mcSub(&in->target[p * 3], &in->target[q * 3], dt);
				const float t2 = mcDot(dt, dt);
				if (t2 < thick * thick) {
					apart = sqrtf(t2);
					if (len >= apart) continue;
				}
			}
			const bool hot = watch && (in->fight[p] >= trip || in->fight[q] >= trip)	// it shakes here ...
				&& !(limbs && (in->limbPush[p] || in->limbPush[q]));					// ... and no limb is moving this cloth
			const float corr = 0.5f * (apart - len) / len;
			mcMA(a, corr, d, a);
			mcMA(b, -corr, d, b);
			// a separation, not a throw
			float* pa = &in->prev[p * 3], * pb = &in->prev[q * 3];
			mcMA(pa, corr, d, pa);
			mcMA(pb, -corr, d, pb);
			const bool calmFine = fine && !(limbs && (in->limbPush[p] || in->limbPush[q]));
			if (hot || calmFine) {
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
					const float alpha = sv.linkAlpha ? sv.linkAlpha[l] : g->linkAlpha[l];	// SJE r_meshClothAirFree
					if (alpha < 0.0f) continue;
					if (g->linkBend[l] && (sv.bendModel == 1 || (sv.bendModel == 2 && g->linkLoose[l]))) continue;	// SJE: the stencils below bend it instead
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
					if (sv.bendModel == 1 || (sv.bendModel == 2 && g->linkLoose[l])) continue;	// SJE: the stencils below bend it instead
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
			// SJE 'ring': the second tether level - no further from its ring point than the cloth
			// between them is long (or the pose has them, where that is more), no stretch allowed
			if (sv.ring) {
				const int rn = g->ringAnchor[p];
				if (rn >= 0) {
					vec3_t d, dt;
					mcSub(pp, &in->p[rn * 3], d);
					mcSub(t, &in->target[rn * 3], dt);
					float maxLen = g->ringLen[p] * in->linkScale * sv.ringGive;	// SJE r_meshClothTurnCalm: x ringGive
					const float animLen = mcLen(dt);
					if (animLen > maxLen) maxLen = animLen;
					const float len = mcLen(d);
					if (len > maxLen && len > 1e-5f) mcMA(&in->p[rn * 3], maxLen / len, d, pp);
				}
			}
			// never higher than both the attached edge it hangs from and its
			// animated spot: stops a fast turn or a spawn from flipping a cape
			// up over the shoulders
			// (per surface 'lift', or r_meshClothLift for all; below 0 = no limit)
			const float liftL = sv.liftLimit > -1.5f ? sv.liftLimit : g->lift[p];
			if (an >= 0 && liftL >= 0.0f && !(sv.liftFree && sv.liftFree[p]) && !MCDBG_OFF("NOHEIGHT")) {	// SJE r_meshClothFree: none for a free cape
				// SJE: ... and the limit slopes down from the attached edge (r_meshClothLiftSlope): a point may rise to
				// that part of its cloth length under the edge, not level with it. Level, a cape thrown up in a jump
				// or a fall lay as a flat sheet at shoulder height, spread out sideways like wings; sloped it streams
				// out behind and droops to its hem.
				const float hp = mcDot(pp, in->up), ha = mcDot(&in->p[an * 3], in->up) - sv.liftSlope * g->lraLen[p] * in->linkScale, ht = mcDot(t, in->up);
				float lim = (ha > ht ? ha : ht) + liftL * in->linkScale;
				const float limShut = lim;	// SJE r_meshClothAirFree: the limit as it is in front of the edge
				float rampK = 0.0f, rampB = 0.0f, rampW = 0.0f;	// ... its rise per unit behind the edge, how far behind the point is, where the rise ends
				// SJE r_meshClothLiftAir: air from below (a fall, an updraft) or strong wind opens the limit, up to MC_LIFT_OPEN
				// of the point's cloth length above the edge, so a cape's hem may flip up and stream out above the shoulders
				// (closed, it held the hem a part of the cape's length under the shoulders in every jump and in any wind).
				// Only behind the edge the point hangs from (fully MC_LIFT_BEHIND behind it, not at all in front): a cape never
				// tips forward over its wearer's head. Worn cloth (robes, layers under them) keeps the limit. Closed (runs,
				// turns, spawns): as before.
				const float liftA = sv.liftOpenAll ? 1.0f : sv.liftAir;	// SJE r_meshClothAirPhys: open in every step
				if (liftA > 0.0f && !(sv.liftWrap && (sv.liftWrap[p] || sv.liftUnder[p])) && !(sv.liftLow && sv.liftLow[p])) {
					vec3_t ra;
					mcSub(pp, &in->p[an * 3], ra);
					const float behind = -mcDot(ra, sv.liftFwd), bw = MC_LIFT_BEHIND * in->linkScale;
					if (behind > 0.0f) lim += liftA * (behind < bw ? behind / bw : 1.0f) * (sv.liftSlope + MC_LIFT_OPEN) * g->lraLen[p] * in->linkScale;
					if (sv.liftGrad && behind < bw && bw > 1e-3f) {
						rampK = liftA * (sv.liftSlope + MC_LIFT_OPEN) * g->lraLen[p] * in->linkScale / bw;
						rampB = behind;
						rampW = bw;
					}
				}
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
						if (hf > lim) { lim = hf; rampK = 0.0f; }	// (the floor holds the limit up: the plain way)
					}
				}
				if (hp > lim) {
					// SJE r_meshClothAirFree: back under the limit by the NEAREST way. Over the MC_LIFT_BEHIND units behind the edge
					// the limit falls from a cloth length above it to a part under it - 12 units of height per unit forward. Put
					// straight down, a cape that tipped forward over the shoulders at the end of a fall dropped 18 units in two
					// steps, faster than its wearer fell. The nearest point of that sloping surface lies a small step BACK
					// behind the edge: the projection along the limit's gradient (up + rise x forward).
					bool viaSlope = false;
					if (rampK > 0.0f) {
						const float over = hp - (limShut + rampK * rampB);	// over the slope's plane (behind the edge or in front of it)
						if (over > 0.0f) {
							const float inv = 1.0f / (1.0f + rampK * rampK), back = over * rampK * inv;
							// ... if it ends on the slope (behind the edge, before the rise is over) and is nearer than straight down
							if (rampB + back >= 0.0f && rampB + back <= rampW && over * over * inv < (hp - lim) * (hp - lim)) {
								mcMA(pp, -over * inv, in->up, pp);
								mcMA(pp, -back, sv.liftFwd, pp);
								viaSlope = true;
							}
							else if (rampB >= 0.0f && rampB + back > rampW) {
								// ... past the end of the rise: back to that end and down to the limit there, if that is nearer
								const float bk = rampW - rampB, top = limShut + rampK * rampW, dn = hp > top ? hp - top : 0.0f;
								if (bk * bk + dn * dn < (hp - lim) * (hp - lim)) {
									mcMA(pp, -dn, in->up, pp);
									mcMA(pp, -bk, sv.liftFwd, pp);
									viaSlope = true;
								}
							}
						}
					}
					if (!viaSlope) mcMA(pp, lim - hp, in->up, pp);
				}
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
			const float* ta = sv.bell && g->bellW[p] > 0.0f ? &in->anchor[p * 3] : t;	// SJE 'bell': measured from the hip-carried shape
			mcSub(pp, ta, d);
			float maxLen = f * g->tether[p];
			// SJE r_meshClothAirFree: in moving air it grows, for loose cloth the air carries away from its wearer
			if (sv.freeP && sv.freeP[p] > 0.0f && !(sv.wornWrap && (sv.wornWrap[p] || sv.wornUnder[p]))) maxLen *= 1.0f + sv.freeTether * sv.freeP[p];
			if (g->reach > 0.0f && (int)g->rootDist.size() == g->numParticles) {
				const float rl = g->reach * g->rootDist[p] * in->linkScale;
				if (rl < maxLen) maxLen = rl;
			}
			const float len = mcLen(d);
			if (len > maxLen && len > 1e-5f && !MCDBG_OFF("NOTETHER")) {
				const float k = sv.tetherK;	// ~15% per 16/6 ms, frame-rate independent (SJE: per-step constant)
				float target = len - (len - maxLen) * k;
				if (target > 2.0f * maxLen) target = 2.0f * maxLen;
				mcMA(ta, target / len, d, pp);
#ifdef MC_DEBUG
				g_mcDbgTether++;
#endif
			}
		}
		if (sv.faceCol & 1) { if (sv.faceNow) FaceCollide(g, in, caps, ncaps, sv.overArms, sv.rest, sv.ease, sv.limbLie, sv.armGapOn); }	// SJE: triangles against the whole body (covers the edges too)
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
	const float MC_LIE_EPS = 0.05f;	// SJE r_meshClothLimbLie: a triangle this near its clearance off an arm (or further) lies against it
	const float MC_LIE_MOVE = 1.0f;	// ... if no corner has moved more than this since the step began
	const float MC_ARMGAP_STEP = 3.0f;	// SJE 'armgap': arms push such cloth out up to this many times MC_FACE_STEP per substep
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
	// SJE r_meshClothTurbulence: the air a character moves through - or that blows past him - is never one even stream.
	// Behind a body it breaks up into eddies. Without them the travel air came exactly against the travel and only its
	// strength rippled, in a fixed rhythm: the same move streamed a cape the same way every time. Two parts, both only through the air drag, as the wind - the
	// body, the walls and the floor keep the cloth out as before:
	//  - the stream wanders slowly: its direction a few degrees to the side and up and down, its speed a little (smooth
	//    random values, McNoise, each cloth with its own sequence; uniform over the cloth);
	//  - eddies: a random field of MC_TURB_MODES plane waves whose air runs across each wave (Kraichnan's random Fourier
	//    modes for synthetic turbulence; such a field has no sources or sinks - air is neither made nor lost, as with curl
	//    noise, Bridson et al. 2007 - so it does not pull the cloth apart or together). Sizes from MC_TURB_L1 down to
	//    MC_TURB_L0 (a body's width; behind a body ~14 units wide at a run the Strouhal number, ~0.2, gives ~4 eddies a
	//    second), sized by the von Karman spectrum, carried past the cloth with the air (Taylor's frozen turbulence: a point
	//    further downstream meets them later) and changing slowly on their own.
	// All of it scales with the speed of the air against the character (none below MC_TURB_U0: still air keeps the breeze).
	const float MC_TURB_U0 = 30.0f;		// units/s of air against the character: none below ...
	const float MC_TURB_U1 = 120.0f;		// ... all from here on (a walk is about 100, a run 250-300)
	const int MC_TURB_MODES = 8;
	const float MC_TURB_EDDY = 0.3f;		// the eddies' speed (each direction, rms) as a part of the air's (behind a body: 0.2-0.3)
	const float MC_TURB_L0 = 16.0f;		// smallest eddy wave length (units) ...
	const float MC_TURB_L1 = 100.0f;		// ... largest
	const float MC_TURB_LSPEC = 10.0f;	// von Karman length scale (units): the spectrum peaks at waves of about 40 units
	const float MC_TURB_TRAVEL = 0.7f;	// eddies are carried past the cloth at this part of the air's speed
	const float MC_TURB_LIFE = 0.5f;		// ... and change on their own: a wave's phase turns at up to this x its number x the eddies' speed
	const float MC_TURB_WANDER = 160.0f;	// the stream's wander: units of air going by per noise cell ...
	const float MC_TURB_WCELL0 = 0.3f, MC_TURB_WCELL1 = 2.0f;	// ... seconds per cell, at least / at most
	const float MC_TURB_YAW = 0.25f;		// ... turning it this far to the side (part of its speed: up to about 14 degrees),
	const float MC_TURB_PITCH = 0.1f;		// ... up and down
	const float MC_TURB_SURGE = 0.2f;		// ... and making it this much faster and slower
	const float MC_TURB_SHEAR = 0.5f;		// ... and this much stronger on one side of the body than on the other,
	const float MC_TURB_SHEARW = 20.0f;	// ... in full from this far off his middle (units across the stream)
	// SJE r_meshClothAirFree: moving air frees the cloth. Still, a heavy cape is held to its animated shape: its bend
	// links take 30 - 80 % of every fold away per 16 ms, 'damp' stills it. That is right while nothing moves it - but
	// in a storm, a fall or a run the same hold made it a board on a hinge: every local gust was averaged away, the
	// whole cape moved as one (measured: 1.2 - 3 independent patterns, the hem never over the shoulders).
	// The air's speed against the character -
	// his travel, his fall, the wind - lets go: none below MC_FREE_U0, all from MC_FREE_U1 (eased: in over
	// MC_FREE_IN, out over MC_FREE_OUT seconds).
	const float MC_FREE_U0 = 100.0f;		// units/s (a walk)
	const float MC_FREE_U1 = 700.0f;		// (a run is 250 - 300, a long fall 600 and more, t1_rail's storm 3200)
	const float MC_FREE_IN = 0.15f, MC_FREE_OUT = 0.9f;
	// Loose cloth sinks through the air no faster than MC_FREE_SINK (x the root of its weight; a heavy cape: about 5
	// metres a second). The air's push came from the CHARACTER's travel only: after a landing he stands, and a cape
	// that had risen over him fell with the speed of his fall, undamped - down at once. With the limit it lags over a falling wearer (as a real cape does: he
	// falls faster than the cloth can) and sinks back after the landing. Not for worn cloth.
	// ... the eddies reach a point by its exposure e = 1 - (1 - lee) x exp(-d / MC_TURB_WALL): all of them in the body's
	// lee (they come off the body), none where the air presses the cloth onto it - there the random part dies out towards
	// the body as turbulence does towards a wall (van Driest's damping, 1 - exp(-y / A)). The cloth that still lay on the
	// back and round the flank got the eddies at the real wind's size as soon as it counted as free (r_meshClothTailSide
	// frees the whole pushed cape).
	const float MC_TURB_WALL0 = 2.0f;	// units from the body's shapes within which the cloth counts as lying on it
	const float MC_TURB_WALL = 8.0f;	// ... and the length over which the eddies come on beyond that
	const float MC_FREE_SINK = 110.0f;	// units/s at weight 1 (a heavy cape of weight 1.5: 135, about 3.4 metres a second)
	const float MC_LIFT_CLOSEF = 1.0f;	// ... and the lift limit closes by this much per second (MC_LIFT_CLOSE: 2), no faster than the cloth sinks
	// ... and for a moment after a fall loose cloth comes down no faster than MC_FREE_LAND against its wearer, whatever
	// pushes it. The drag above balances the cloth's weight - but a wearer who lands running goes on through the air, and
	// his travel wind pushed the cape that stood over him back down at once: 0.3 s after a running jump, against 0.9 s
	// after a jump on the spot. 'After a fall': from the fall's own air (as the lift limit opens), fading by
	// MC_FREE_LAND_FADE per second once it stops. Against the wearer, not the world: while he falls the cloth rises
	// against him and nothing is held, so the links are not left to drag it (as with a cap in the world they were).
	// And against the point's own animated spot where that goes down faster than he does (the dip of a landing, a
	// flip): held against him alone, the cloth below the collar could not follow the shoulders down and the links
	// dragged it there - more stretched edges after every jump.
	// SJE r_meshClothTailSide: with the back to a strong wind a cape does not lie on the back - it streams with the wind
	// past ONE side of its wearer. The cloth on that side of the spine - and
	// a little beyond it - is let go as lee cloth is and pushed: outwards along the back while it is behind the body and
	// its arm, on with the air beyond. All or nothing: a weaker push (0.6, 0.9) let the cape flap between back and side,
	// and each pass round the body's flank put cloth into it (cloth triangles inside the torso, 109 - 371 triangle-steps
	// in 287 steps; at 1.2: none in four runs); going further out before the turn forward (20 / 32) as well.
	// Which side: the lee side of the turn first (where the air went while it came from the side). But only one side is
	// free, and which depends on the stance (the hand with the hilt beside the hip; the hips turned with a lit saber):
	// pushed against the other the cape only leaned 10 units and, in one run of two, went into the body (359 / 399
	// triangle-steps). So it tries: not out within MC_TAIL_TRY seconds - the other side; not out there either - it stays
	// where it got further. Not for worn cloth.
	const float MC_TAIL_U0 = 300.0f, MC_TAIL_U1 = 700.0f;	// air over the ground against the character, units/s: from / in full (a run backwards: 250)
	const float MC_TAIL_C0 = 0.3f, MC_TAIL_C1 = 0.8f;		// how squarely it comes from behind (cosine): from / in full
	// (0.35 and 1 per second, not 0.2 and 4: in quick runs left / right in a wind
	// every change of direction set the lee side anew and sent the cape round behind his back to
	// the other side, 17 - 21 times in 24 changes. A real side wind - the turn out of it - still sets it.)
	const float MC_TAIL_SIDE = 0.35f;						// the air's sideways part (of 1) from which it sets the lee side
	const float MC_TAIL_SIDE_RATE = 1.0f;					// ... per second
	// 'forward' for the rule is where the cloth's attached edge faces (the line of the shoulders), not the entity's axis:
	// that is the legs, and in a strafe run they turn about 65 degrees into the travel while the shoulders turn 8 - 26 -
	// the rule saw a side wind that was none and started afresh on the other side. Only while the edge is a line at all:
	const float MC_TAIL_FWD_LINE = 0.15f;					// its spread along against across, 0 (a ring) .. 1 (straight): Vader 0.32 - 0.37, Dooku 0.60
	const float MC_TAIL_PUSH = 1.2f;						// x r_meshClothDragQuad x the air speed squared (capped at MC_AIR_MAX), / the cloth's weight
	const float MC_TAIL_RAMP = 4.0f;						// on the other side the push comes on by this much per second
	const float MC_TAIL_L0 = -12.0f, MC_TAIL_L1 = 2.0f;	// which cloth: from this far beyond the spine (units, on the far side) / in full from here
	const float MC_TAIL_A0 = 10.0f, MC_TAIL_A1 = 22.0f;	// pushed outwards up to this far from the spine / on with the air from here
	// ... and BESIDE the body (not behind the back) further out by MC_TAIL_BESIDE: with 10 the wing lay against the outside
	// of the arm. When the arm swung - a run backwards into the wind - the cloth got between arm and body, and the body's
	// side test threw it back behind the back: the hem from 14 units beside him to 15 behind him in 0.06 s, round and round
	// every 0.6 s.
	// Standing it is calmer too (links over 1.25 x their length 937 -> 109, 909 -> 500). Behind the back it stays at 10:
	// further out there the cape spanned across the body.
	const float MC_TAIL_BESIDE = 12.0f;
	const float MC_TAIL_RUN = 100.0f;	// units/s of the wearer's own travel from which the body's side test leaves the wing alone (FaceCollide)
	const float MC_TAIL_B0 = -10.0f, MC_TAIL_B1 = -2.0f;	// 'beside': from this far forward of the spine (units; minus = behind it) / fully from here
	const float MC_TAIL_OUT = 18.0f;						// 'out': the lower third's centre this far (units, to the side and forward) from its animated place
	const float MC_TAIL_OUT_T = 0.5f;						// ... eased over this many seconds
	const float MC_TAIL_OUT_LOW = 12.0f;					// ... and once out it counts as out down to here (a dip is not a side lost: it sent the cape on a detour)
	const float MC_TAIL_TRY = 1.0f, MC_TAIL_TRY2 = 3.0f;	// seconds the first side / the other side gets to bring it out (the other: the cloth has to cross the back)
	const float MC_TAIL_GONE = 0.5f;						// seconds without the wind from behind after which all starts afresh
	// ... but never over to the side a LIT BLADE is held on. The trial is made standing: there the blade is up in
	// front and the wing gets out on the saber hand's side. In a run backwards into the wind the hand swings down beside the
	// hip and the blade pushes the wing behind his back, over and over (Vader with the MD team's own animations, his saber
	// stance: the first side out by 16.7 units after a second, a little under MC_TAIL_OUT; tried the saber's side; then out
	// 40 % of a run backwards instead of 91 %, behind his back 25 %, the hem moving up to 14.5 units a step - with the first
	// side kept: 81 %). A lit blade: the held shapes are together longer than MC_TAIL_BLADE_LEN (hilt and blade 44 units, a
	// hilt alone 18); its side: their middle, by length, further than MC_TAIL_BLADE_LAT to one side of the spine (standing
	// 3, in the run 9 - 12; a blade held in the middle has none). The change is then not made and the side is settled.
	const float MC_TAIL_BLADE_LEN = 30.0f, MC_TAIL_BLADE_LAT = 1.5f;
	// ... and while a lit blade is held the push is MC_TAIL_PUSH_BLADE: the saber stance turns the shoulders so
	// that the air comes a little across the back, against the side in use, and the blade arm sweeps the wing - on t1_rail
	// the cape was beside him 81 % of the time with the saber lit (56 - 76 % in a run backwards) against 94 % without; with
	// 1.5: 89 % (79 - 96 %), the stretched links + 11 - 19 %. Not without a blade: there 1.2 is enough, and the stronger push
	// put Kylo's arm through his cloak twice as often in a storm run backwards (297 -> 622 triangle-steps). Eased in and
	// out over MC_TAIL_PUSH_T. (r_meshClothTailSide 2: the push of before, whatever is held.)
	const float MC_TAIL_PUSH_BLADE = 1.5f, MC_TAIL_PUSH_T = 0.3f;
	// ... and a cape that is ON a lit blade's side (the lee side of the turn was the saber hand's, or a side wind set it) gives
	// that side up at once, as soon as the air comes straight from behind: standing the blade is up in front and the cloth
	// can cross behind his back; in a run backwards the hand swings down beside the hip and the blade swept the wing behind
	// him (mirrored saber-lit runs on t1_rail: 100 % out for a lap, then 72 - 76 % in one run backwards and 1 - 5 % in the
	// stand after it; 56 - 64 % over three runs). Once per start of the rule.
	// The side given up is no longer the remembered lee side either: after a run with the wind the rule starts afresh at
	// the stop, and it started on the blade's side each time, at full strength, to give it up 0.3 s later - the cloth
	// crossed his back at every such stop (t1_rail mirrored, the stand after a run forward: out 99 -> 93 %, stretched links
	// x 2.4). A real side wind sets the lee side again as before.
	const float MC_TAIL_BLADE_GIVE = 0.3f;	// seconds the blade must have been on the side in use
	// ... the WIND REFERENCE PLACE. The body tests know where cloth belongs only from the pose, and the pose has a cape behind
	// the back: cloth the wind has carried past its wearer's side has no valid reference there. In a run backwards into the
	// wind the legs swing into the wing (thigh and calf in four recorded runs of six, the hand in two); the triangle test
	// found the cloth on the 'far side' of the leg from the animated cape and threw it there - the hem from 16 units beside
	// him to 13 behind him in five steps, out again with the push, round and round. Over five such runs in a row the cape was out 73 %
	// (left) / 72 % (right) of the time, and behind his back in four / three runs of the five.
	// So a cape's cloth (it hangs from shoulder height or above) that the rule is pushing, against a body shape that MOVES
	// (more than MC_TAIL_REF_MOVE in the step),
	// 1. keeps the side it was on when the step began, where it was clear of the shape then: the shape pushes the cloth
	//    ahead of itself, as arms do (Lewin, GDC 2018: predictive contacts);
	// 2. else has, in place of the animated one, a reference place of its own: where the wind would carry it - its animated
	//    place turned round the wearer's upright axis towards the side in use and out to a distance clear of body and arm.
	//    Not near the attached edge, and not while the cloth is still near its animated place: there the pose is right.
	// With both: out 100 % / 98 %, never behind his back, the hem's largest move in a step 9.6 -> 4.1 units (left).
	// Which shapes: the ones that move, not 'while he runs'. Gated by the wearer's own travel (over 100 units/s) the wing
	// was still thrown in the first steps of every run with the saber lit - at 1 .. 54 units/s, the leg swings at once -
	// (out 91 % / 76 %; by the shape's own move 100 % / 100 %) and in a walk (95 - 99 units/s: 89 % / 91 %).
	// Not against a shape at rest (standing): there the wing holds its place by itself; with the reference always on it
	// hung lower and less steady, and a finer limit (0.2) brought that back (out 95 %, standing after the run 85 %).
	// Not a skirt round the hips: its place is where the animation has it (with the reference it stretched: 123 -> 3995).
	// (Tried before, same day: pose-based fly-papering, Pixar US 7,911,469 - arms ignored for pushed cloth at the body: out
	// 92 - 100 % in single runs, but cloth through the arm, 3 -> 169 - 947 triangle-steps.)
	const float MC_TAIL_REF_PHI = 100.0f;					// degrees the hem's reference is turned towards the side in use (less further up the cloth)
	const float MC_TAIL_REF_AMAX = 115.0f;					// ... never past this from straight behind him
	const float MC_TAIL_REF_R0 = 14.0f, MC_TAIL_REF_R1 = 26.0f;	// ... its distance from the upright axis: at the attached edge / at the hem (units)
	const float MC_TAIL_REF_S0 = 0.10f, MC_TAIL_REF_S1 = 0.35f;	// which cloth: from this part of its sheet's length under the edge / in full from here
	const float MC_TAIL_REF_D0 = 6.0f, MC_TAIL_REF_D1 = 14.0f;	// ... and this far from its animated place / in full from here (units)
	const float MC_TAIL_REF_MOVE = 0.5f;					// against which shapes: either end has moved more than this in the step (units; 31 units/s)
	// Which cloth the two references speak for. The rule's push reaches cloth 12 units beyond the spine (MC_TAIL_L0):
	// a cloak where it lies on both shoulders and upper arms is pushed cloth too. Holding THAT at the full clearance on its
	// start side lifted it off the shoulders, stretched it and put the arms through it (Kylo's cloak, arms through cloth in
	// storm laps 318 / 3 -> 1006 / 656 triangle-steps, the stretch counter doubled to tripled). So the start side holds only
	// for a triangle whose three corners all are cloth the reference place speaks for (tailRefK > 0: not near the attached
	// edge, and carried away from its animated place), and the reference place stands in for all three corners or none.
	// Arms and hands: the reference place is the animated cape turned round the upright axis - beside him it passes where the
	// arm hangs, and what the arm test asks it there (which side, modelled round the arm, resting under it, caught under it)
	// was answered by chance: cloth behind the forearm was thrown out through it (Dooku, 16 -> 89). For such cloth against
	// an arm the side it was on when the step began holds from MC_TAIL_ARM_D0 x the arm's radius off its axis on, and it is
	// always tested as free cloth: held clear of the arm on that side.
	const float MC_TAIL_ARM_D0 = 0.5f;
	// SJE r_meshClothTurnCalm: the player's body turns with the mouse - the shoulders swing 85 - 110 degrees to and fro at
	// 400 - 1000 degrees/s while he keeps running nearly straight. 'spring' and 'ring' pull the loose cloth to the animated
	// pose, which turns with hips and torso, and the swing the turn starts is hardly damped (a cape hangs from the line of
	// the shoulders like a bifilar pendulum; sweeps every 0.8 s drive it near its own period): the hem went round after
	// 50 - 100 ms, swung 170 degrees against the pose's 82 and was flung out to 50 units. So while the attached edge turns
	// faster than a person turns the shoulders, the two let go and 'damp' sideways grows: swing 170 -> 114 degrees, the hem
	// 29 -> 15 degrees (rms) off 'straight behind his travel'. Slower turns, standing, straight runs: nothing changes.
	// (No single binding, the air, friction, stiffness or a limit on the acceleration the cloth feels did it - all tried.)
	const float MC_TURN_W0 = 120.0f, MC_TURN_W1 = 300.0f;	// degrees/s of the attached edge's turn about up: from / in full
	const float MC_TURN_BACK = 0.5f;						// seconds it takes to go again (it comes on in 0.05)
	const float MC_TURN_DAMP = 3.0f;						// 'damp' sideways x this, in full
	const float MC_TURN_RING = 0.5f;						// 'ring': its length x 1 + this, in full
	// SJE r_meshClothWhirl: the player can strafe and turn the same way for as long as he likes - at 250 units/s and 420
	// degrees/s a circle of 35 units, a metre, run at full speed: 1830 units/s^2 towards its centre, 2.3 x the cloth's
	// gravity. Two things then held the cape out level, the hem 47 - 50 units from his upright axis and 7 - 12 over the
	// origin (a straight run: 33 - 37 out, 1 - 5 under it) - a flat sheet that fell together between two swipes of the mouse
	// and was flung again:
	// 1. the whirl itself - the cloth is simulated in the world, trailed round behind him on the circle and was thrown
	//    outwards;
	// 2. 'damp' - its frame travels with him but does not turn with him and follows his speed with a lag: cloth that goes
	//    round with him was 160 units/s off it, with the turn-calm rule's 'damp' x 3 a pull of 3000 units/s^2 along the cape.
	// No runner can do that circle. So while he whirls - his direction of travel has turned on one way for more than
	// MC_WHIRL_PATH0 degrees, or the attached edge for more than MC_WHIRL_TURN0 (a spin on the spot) - each free point is
	// given what a point carried rigidly round with him would need (the path's turn, and - omega^2 r about his own axis)
	// beyond MC_WHIRL_MAX, and the frame 'damp' works in goes round with him. Quick sweeps to and fro in a run (the travel
	// swings 47 - 82 degrees, the shoulders 113 - 162), straight runs, stops and starts, jumps, wind: nothing changes.
	const float MC_WHIRL_PATH0 = 90.0f, MC_WHIRL_PATH1 = 180.0f;	// degrees his travel has turned on one way: from / in full
	const float MC_WHIRL_TURN0 = 180.0f, MC_WHIRL_TURN1 = 360.0f;	// ... or the attached edge about up
	const float MC_WHIRL_BACK = 30.0f;		// degrees turned back that end it (less is a sway)
	const float MC_WHIRL_MEM = 2.0f;		// seconds the turn is remembered over (a mouse hand turns in swipes, with a moment between them)
	const float MC_WHIRL_CAP = 1.25f;		// ... and no more of it than this x the 'in full'
	const float MC_WHIRL_MAX = 500.0f;		// units/s^2 of the whirl the cloth feels (x the figure's scale)
	const float MC_WHIRL_EASE = 0.1f;		// seconds the rule comes and goes in
	const float MC_WHIRL_ACC_T = 0.06f;		// seconds the path's turn is eased over (the origin's speed itself over 0.05)
	const float MC_WHIRL_V0 = 60.0f;		// units/s of travel from which there is a path
	// SJE r_meshClothAirPhys: the air's push on a point by that point's own speed through the air and by how
	// the cloth lies to it - r_meshClothDragQuad x |u_n| u_n / weight across the cloth (the constant the travel wind has),
	// MC_AIRPHYS_SKIN of that along it. In place of the travel wind (the character's speed, the same for every point), the
	// linear drag, the sink rule, the landing rule, the lift limit and 'damp' (the billow push of a run stays).
	const float MC_AIRPHYS_SKIN = 0.02f;	// the air's drag along the cloth, as a part of that across it
	const float MC_AIRPHYS_GAIN = 1.0f;		// x r_meshClothDragQuad (the old air had linear drag on top of the travel wind)
	// ... and LIFT: air that goes along a sheet at a small angle pushes it across by far more than the square of the small
	// part of its speed that is square to the sheet - a thin plate's lift grows with the angle itself (about 2 pi x the
	// angle for a long wing, a third of that for a sheet as wide as long). So the push across the cloth is
	// c (|u_n| + MC_AIRPHYS_LIFT |u_t|) u_n: square to the flow the plain drag, along it the lift. Without it a cape that
	// hangs straight down did not rise in any fall (the air went along it), and streamed less in a run.
	// (1.0: the hem in a run 34.6 units out and 6.9 under the origin - with the rules 35.9 / 6.1; 0.5: 31.7 / 6.6.)
	const float MC_AIRPHYS_LIFT = 1.0f;
	// ... and the POCKET: a cape is closed at its attached edge. Air that comes at it from the hem's side - a fall's air from
	// below - is caught between cape and wearer as in a parachute and pushes the cloth outwards, whatever way it leans
	// (without it a cape that hung down against the legs stayed there in a fall of 420 units/s: the air's push across it
	// pressed it onto them). Along the point's normal, towards the side away from his upright axis:
	// c MC_AIRPHYS_POCKET u_in^2, u_in = the air's speed against the WEARER along the cloth from the point towards its
	// attached edge.
	const float MC_AIRPHYS_POCKET = 0.6f;
	// The air speeds of levels are not real ones (t1_rail's storm: 3200 units/s, 90 m/s): as the rules do, the air against
	// a point counts up to MC_AIRPHYS_UMAX units/s, faster air pushes no harder (the rules: the square up to 520 units/s plus
	// the linear drag - at 3200 units/s about what this gives; 'in proportion beyond' made that storm four times as strong
	// as the tail-side rule was made for: with the saber lit the cape was beside him 73 / 85 / 74 / 20 % of the time).
	// (450, not 600: with the lift the push on a wing that streams past its wearer at a small angle is about what the rules
	// give at 3200 units/s; at 600 a lit saber's run backwards on t1_rail had the cape beside him 85 % of the time, at 450
	// 98 % - the rules 99 %. And 350, not 450: Kylo's cloak in a storm lay in his body twice as often as with the rules,
	// with 350 less often than with them, and his arm goes through it as often as with them; Vader's cape with a lit
	// saber is beside him as often and no more in his body. A fall reaches 350 after 0.44 s - a held jump's end.)
	const float MC_AIRPHYS_UMAX = 350.0f;
	const float MC_AIRPHYS_DAMP = 0.0f;		// how much of 'damp' stays
	// The lift limit (how high over its attached edge a point may rise) is OPEN in every step, as r_meshClothLiftAir opens
	// it at most: cloth may rise by its own length over the edge it hangs from, behind that edge (MC_LIFT_BEHIND) - never
	// forward over its wearer. Not for worn cloth (as there), and not for cloth that hangs from the hips: its attached
	// edge lies more than MC_AIRPHYS_HIP units under the top of the body's shapes (x the figure's size), the trunk is
	// above that edge and it cannot flip up over it. (A cape's edge at the shoulders lies 5 - 10 under the top of the
	// head, a hip skirt's 26 - 30.)
	const float MC_AIRPHYS_HIP = 18.0f;
	// SJE r_meshClothFree: cloth whose line has 'free F' - a cape that is an object of its own.
	// The air against it is capped as a FIELD, not point by point: the steady air against the wearer (the wind without its
	// gusts, less his own travel) counts up to MC_CAPE_UMAX units/s, and everything that varies - gusts, ripples, eddies -
	// is scaled with it. Cut point by point (MC_AIRPHYS_UMAX), any air faster than the cap pushed with one constant force:
	// in a storm nothing varied and the cape stood in one place. A point's own speed against that air counts up to
	// MC_CAPE_PEAK x the cap (a gust's peak, a whipped hem).
	const float MC_CAPE_UMAX = 450.0f;
	const float MC_CAPE_PEAK = 1.5f;
	// Air that goes ALONG a sheet: a sheet that flutters in it (a flag) is dragged by a tenth to a fifth of what air square
	// to it does - not by the 2 % of smooth skin friction (MC_AIRPHYS_SKIN). With that a cape edge-on to a storm hung in its
	// animated bell and did not move (t1_rail, turning on the spot: 70 degrees of the turn).
	const float MC_CAPE_SKIN = 0.1f;
	const float MC_CAPE_TAIL = 1.0f;		// wind from behind: this share of its forward part (MC_WIND_TAIL: 0.4)
	// Air from the SIDE gets under a cape's open side edge and lifts it off its wearer (the pocket, MC_AIRPHYS_POCKET, has
	// that for air from the hem's side only). Without it wind from the side and a little behind pressed the cape flat on
	// its wearer's back - no rule took it off (the tail-side rule begins further round) and it hung there as in still air.
	const float MC_CAPE_PSIDE = 1.0f;
	// The tail-side rule's push for a group with free cloth (MC_TAIL_PUSH: 1.2). With the wind in full from behind the
	// cape is pressed on its wearer harder than that push was made for: standing with a lit saber and the storm in his
	// back the cape stayed low beside him (hem 37 units under the shoulders, 18 % of it on the body); with 1.5: 29 / 11 %,
	// cloth in the body as often. (1.8: 25, and it moves less; 2.6: stiff, links stretch.)
	const float MC_CAPE_TAILPUSH = 1.5f;
	const float MC_CAPE_GAIN = 1.0f;		// x the air's push (x MC_AIRPHYS_GAIN)
	const float MC_CAPE_DAMP = 0.0f;		// how much of 'damp' stays (as MC_AIRPHYS_DAMP)
	// The turn-calm rule (MC_TURN_W0) is OFF for a group with free cloth: a saber swing turns the shoulders as fast as the
	// turns it was made for, and it held the cape in its bell through every swing - with it off the cape flares and flips
	// in a swing (hem 23 units from its animated place instead of 13, at 52 from his axis instead of 42).
	const int MC_CAPE_RULES = 1;			// bit 1: no turn-calm rule, bit 2: no circling rule
	const float MC_CAPE_TWIST = 0.0f;		// degrees/s of the attached edge's turn beyond which the cloth is carried along rigidly (0 = r_meshClothTwistFree)
	// r_meshClothFree 4. Simulated in the world alone, the hem of a cape stays behind when its wearer turns fast and is
	// then dragged round by the cloth above it, whose pull points inwards: the cape winds round him at the radius it
	// hangs at and swings out only when the turn is over. Here the turn's centrifugal pull acts on the free cloth
	// (MC_CAPE_FLING x turn rate squared x the distance from the axis): it stands out sideways while he turns. For a
	// turn, not for circling (it goes as the circling rule comes in). MC_CAPE_TURN: the share of the turn the free
	// cloth is carried along by - without it the lower cape lags so far behind the upper that the wing is pinched
	// where it leaves his back. (Half turn at 600 degrees/s: the hem's centre 38 units from the axis while he turns
	// instead of 26; quick sweeps in a run: the hem trails as far as before.)
	const float MC_CAPE_TURN = 0.3f;
	const float MC_CAPE_FLING = 0.12f;
	const float MC_CAPE_TURN5 = 0.5f, MC_CAPE_FLING5 = 0.15f;	// r_meshClothFree 5: the same, stronger
	// ... for a FAST turn only: from MC_CAPE_TURN_W0 degrees/s of the eased turn rate, in full from MC_CAPE_TURN_W1 (a
	// saber swing turns the shoulders at 120 - 300, a stance that flickers leaves an eased rate under 100).
	const float MC_CAPE_TURN_W0 = 200.0f, MC_CAPE_TURN_W1 = 400.0f;
	const float MC_CAPE_BILLOW = 1.0f;		// x the billow push of a run (r_meshClothBillow)
	// (half: the pull to the animated bell held the cape to one shape - after a jump the hem now flips up over the
	// shoulders, in a run it rides 8 units higher, a swing takes it further)
	const float MC_CAPE_SPRING = 0.5f;		// x the line's 'spring'
	const float MC_CAPE_ARM = 0.0f;			// units the arms keep more off it (as a line's 'armgap'; the larger counts)
	// ... and a swung ARM drags the air beside it along, as a lit blade does (MC_BLADE_AIR_R): free cloth near it gives way
	// before the arm touches it and goes after it. The arm's speed against its wearer's travel, across the arm, from
	// MC_CAPE_ARMAIR_SWING units/s on: MC_CAPE_ARMAIR of it at the arm, fading out over MC_CAPE_ARMAIR_R units.
	const float MC_CAPE_ARMAIR = 0.8f;
	const float MC_CAPE_ARMAIR_R = 10.0f;
	const float MC_CAPE_ARMAIR_SWING = 40.0f;
	const float MC_CAPE_ARMAIR_MAX = 300.0f;	// units/s of air at most
	const float MC_FREE_LAND = 70.0f;	// units/s at weight 1 (a heavy cape of weight 1.5: 86)
	const float MC_FREE_LAND_K = 0.6f;	// part of the excess taken away per substep, in full
	const float MC_FREE_LAND_FADE = 1.0f;
	// In a storm too: there the cape slammed down at the landing (t1_rail, a jump into the wind: the hem 34 units under the
	// shoulders as he lands, with the limit 15 over them and down in 0.4 s). Tried without it there (wind over the ground
	// from 150 to MC_LIFT_WIND0): fewer stretched links in storm jumps (links over 1.25 x their length half as often), but
	// the landing in the storm as before - and that is where it shows most.
	// And 'tether' lets go: no point could be further from its animated spot than its line's tether (capes: 40 - 50
	// units). A hem that hangs 55 units under the shoulders could so never rise over them, and in a storm the wind
	// held the whole cape at that limit, where it stood still in the shape the limit gave it - a hook, its end down.
	// In moving air the limit
	// grows to (1 + MC_FREE_TETHER) times itself - past the cloth's own length, so that its length (the long range
	// attachment), the lift limit's rule (never forward over the head) and the body hold it.
	// Both only where the air carries the cloth AWAY from its wearer (per point: MC_FREE_LEE0 / 1). Cloth the air
	// presses onto him - a cape on the back in wind from behind, its windward half in a side wind - has the body in
	// its way: let go there, it wrapped round the body under the storm's force and its coarse triangles cut
	// through it (cloth inside the body in half the steps, edges at 1.37 times their length). And not for worn cloth
	// (a robe, a skirt, the strips under it): it lies on the legs, and let go a storm stretched a robed figure's
	// cloth to 1.6 times its length and blew a hip skirt through its wearer's legs. Worn cloth stays as it was.
	const float MC_FREE_TETHER = 1.5f;
	const float MC_FREE_LEE0 = 0.2f, MC_FREE_LEE1 = 0.4f;	// the air's direction against the point's place round the body (cosine): not freed from -LEE0 back, fully from +LEE1
	const float MC_FREE_BEND = 0.85f;	// fully free, the bend links keep this much less of their 'bend'
	const float MC_FREE_DAMP = 0.85f;	// ... and 'damp' this much less of itself
	const float MC_AIR_MAX_WIND = 520.0f;	// ... and wind over the ground counts up to this for r_meshClothDragQuad (MC_AIR_MAX: 320)
	const float MC_LIFT_FALL0F = 100.0f, MC_LIFT_FALL1F = 300.0f;	// ... and the lift limit opens from this fall on / is open at this
	const float MC_TURB_PASS = 120.0f;	// the eddies pass the cloth at this speed at most (their size is the real air's): eddies of
										// 50 - 100 units then pass once or twice a second, slow enough for a heavy cloth to follow - at the
										// air's own speed they passed 3 - 20 times a second and it averaged them away
	// SJE r_meshClothBladeAir: a lit blade swung near cloth drags the air beside it along. Only its turn counts (the tip
	// faster than the hilt: a swing), not the travel of the whole blade with its wearer's run or arm. Cloth within
	// MC_BLADE_AIR_R of the blade (where it is and where it was half a step and a step ago: it sweeps the air on its way)
	// gets air of MC_BLADE_AIR_K x the swing's speed there, across the blade, fading out to the edge.
	const float MC_BLADE_AIR_R = 18.0f;
	const float MC_BLADE_AIR_K = 0.6f;
	const float MC_BLADE_AIR_SWING = 50.0f;	// units/s of tip against hilt below which a blade is not swung
	const float MC_BLADE_AIR_MAX = 450.0f;	// units/s of air at most
	// a random value in -1..1 for (seed, i), and a smooth random curve through one of them per whole t (Catmull-Rom)
	static float McHashF(unsigned int seed, int i) {
		unsigned int x = seed ^ ((unsigned int)i * 0x9E3779B1u);
		x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16;
		return (float)(x & 0xFFFFFFu) * (2.0f / 16777215.0f) - 1.0f;
	}
	static float McNoise(unsigned int seed, double t) {
		const double fl = floor(t);
		const int i = (int)(long long)fl;
		const float u = (float)(t - fl);
		const float p0 = McHashF(seed, i - 1), p1 = McHashF(seed, i), p2 = McHashF(seed, i + 1), p3 = McHashF(seed, i + 2);
		return 0.5f * (2.0f * p1 + (p2 - p0) * u + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u * u + (3.0f * (p1 - p2) + p3 - p0) * u * u * u);
	}
	// the eddy waves of one cloth: wave lengths spread evenly on a log scale, directions, air directions (across the wave)
	// and phases at random from its seed, sizes from the von Karman spectrum E(k) ~ (kL)^4 / (1 + (kL)^2)^(17/6), scaled
	// so that the field's speed is 1 (rms, each direction)
	static void TurbInit(mcInstance_t* in) {
		const unsigned int sd = in->turbSeed;
		int r = 0;
		auto rnd = [&]() { return McHashF(sd ^ 0x5BD1E995u, r++); };
		double sum = 0.0;
		for (int n = 0; n < MC_TURB_MODES; n++) {
			const float lam = MC_TURB_L1 * powf(MC_TURB_L0 / MC_TURB_L1, (float)n / (MC_TURB_MODES - 1));
			const float k = 6.2831853f / lam;
			const float z = rnd(), ph = 3.1415927f * rnd(), rz = sqrtf(1.0f - z * z > 0.0f ? 1.0f - z * z : 0.0f);
			const float dir[3] = { rz * cosf(ph), rz * sinf(ph), z };
			for (int c = 0; c < 3; c++) in->turbK[n][c] = k * dir[c];
			// the air runs across the wave: a random direction with its part along the wave taken out
			float q[3] = { rnd(), rnd(), rnd() };
			const float qd = mcDot(q, dir);
			for (int c = 0; c < 3; c++) q[c] -= qd * dir[c];
			if (mcNorm(q) < 1e-3f) { q[0] = -dir[1]; q[1] = dir[0]; q[2] = 0.0f; if (mcNorm(q) < 1e-3f) { q[0] = 1.0f; q[1] = q[2] = 0.0f; } }
			mcCopy(q, in->turbQ[n]);
			const float kl = k * MC_TURB_LSPEC;
			const float e = powf(kl, 4.0f) / powf(1.0f + kl * kl, 17.0f / 6.0f) * k;	// x k: the waves are spread evenly on a log scale
			in->turbA[n] = e;
			sum += e;
			in->turbPsi[n] = 3.1415927 * (double)rnd() + 3.1415927;
			in->turbW[n] = MC_TURB_LIFE * k * rnd();
		}
		// a wave of size a with its air along a random direction gives each direction a variance of a^2 / 6
		for (int n = 0; n < MC_TURB_MODES; n++) in->turbA[n] = sum > 0.0 ? sqrtf((float)(6.0 * in->turbA[n] / sum)) : 0.0f;
		in->turbWander = 0.0;
		in->turbReady = true;
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
	// The rule knows the body and its own limits (r_meshClothLayers 2 = without these three, as before). In a storm
	// it did harm and no good: Vader's hip skirt under the cape went into his legs (skirt triangles inside a leg in 158
	// triangle-steps of 600 in quick runs left / right, 121 of 500 in a run backwards), five times the stretched links - and
	// as many pairs in the wrong order as with the rule off. Of its 52000 pushes in that run backwards 20000 were at triangles
	// turned over against their animated ones (a cape folded over, a skirt crumpled at the leg: 'over' and 'under' are the
	// wrong way round there, and the cape was pushed INTO the body), many more at points far on the wrong side of a triangle
	// (a cape blown round to his side stands across the skirt points it hung behind), and every push moved both sides by how
	// free they are - the skirt inwards, into a leg 3.8 units thick that the triangle test looks at every second substep.
	// Over three storms: cloth in the body 1091 -> 112 triangle-steps, stretched links 35094 -> 6501 (as with the rule off:
	// 112, 6742), pairs in the wrong order 39025 -> 29176 (off: 44068).
	const float MC_LAYER_DEEP = 4.0f;	// a point further than this on the wrong side of the triangle is not over or under it any more: left alone
	const float MC_LAYER_BODY = 0.3f;	// a side a body shape has pushed in this substep does not give way against that push by more than
										// this part of the move: the other side takes it
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

	// SJE r_meshClothInside: a loose layer the model has under worn cloth (g->under: Obi-Wan's tunic strips under his robe)
	// stays inside that cloth. The layer rule keeps a point on its side of the few triangles it lies under in the bind pose,
	// while it stays near them; blown about in a storm (or flung in a swing) the strips left those triangles behind, went
	// between the legs and out through the robe's back, and showed outside it (front strips through the back of the robe in
	// a head wind). Once a step: a free point of such a layer whose line from the character's upright axis passes through a
	// triangle of the worn tube (g->wrap; another surface, not sewn to it) is put back just inside that triangle and loses
	// its speed outwards, by at most MC_INSIDE_STEP a step - unless the animated cloth has it outside there too (the model's
	// own drape) or it lies near its animated place (MC_INSIDE_FAR). Only in strong air (MC_INSIDE_WIND); cloth with no such
	// layer is not touched.
	const int MC_INSIDE_SECTORS = 16;	// sectors round the upright axis (a point's line is tested against its sector's triangles)
	const float MC_INSIDE_STEP = 4.0f;	// the largest move back per step
	const float MC_INSIDE_WIND = 500.0f;	// ... and only in air this strong (units/s): without wind, in runs, crouches and backwards walks, it
										// made the strips stretch more than it helped (strips stretched 1.57 -> 2.0, 99th percentile)
	const float MC_INSIDE_FAR = 5.0f;	// only points this far from their animated place: blown or flung out (in a storm the strips
										// outside the robe lay 13 units off it, half of them; pressed out by a crouch 1.7 - acting there
										// too made the strips of a crouching figure restless)
	static bool InsideSegTri(const float* a, const float* b, const float* p0, const float* p1, const float* p2, float& t) {
		vec3_t e1, e2, d, h, s, q;
		mcSub(p1, p0, e1); mcSub(p2, p0, e2); mcSub(b, a, d);
		mcCross(d, e2, h);
		const float det = mcDot(e1, h);
		if (fabsf(det) < 1e-9f) return false;
		const float inv = 1.0f / det;
		mcSub(a, p0, s);
		const float u = mcDot(s, h) * inv;
		if (u < 0.0f || u > 1.0f) return false;
		mcCross(s, e1, q);
		const float v = mcDot(d, q) * inv;
		if (v < 0.0f || u + v > 1.0f) return false;
		t = mcDot(e2, q) * inv;
		return t > 0.0f && t < 1.0f;
	}
	static void BuildInside(const mcGroup_t* g) {
		g->insideBuilt = true;
		g->insidePts.clear();
		g->insideTris.clear();
		g->insideSewn.clear();
		const int np = g->numParticles, ns = (int)g->surfs.size(), nt = (int)(g->tris.size() / 3);
		if ((int)g->under.size() != np || (int)g->wrap.size() != np || ns < 2 || ns > 32 || g->vertBase.size() < (size_t)ns) return;
		for (int p = 0; p < np; p++) if (g->under[p] && !g->wrap[p] && g->freedom[p] > 0.0f) g->insidePts.push_back(p);
		if (g->insidePts.empty()) return;
		g->insideSewn.assign((size_t)ns, 0u);
		for (int si = 0; si < ns; si++) {
			g->insideSewn[si] |= 1u << si;
			for (int i = 0; i < g->surfs[si]->numVerts; i++) {
				const int p = g->vertToParticle[g->vertBase[si] + i];
				if (p < 0 || p >= np) continue;
				const int sj = g->partSurf[p];
				if (sj >= 0 && sj < ns && sj != si) { g->insideSewn[si] |= 1u << sj; g->insideSewn[sj] |= 1u << si; }
			}
		}
		for (int t = 0; t < nt; t++) {
			const int a = g->tris[t * 3], b = g->tris[t * 3 + 1], c = g->tris[t * 3 + 2];
			if (a == b || b == c || a == c || !g->wrap[a] || !g->wrap[b] || !g->wrap[c]) continue;
			g->insideTris.push_back(t);
		}
	}
	static void KeepInside(const mcGroup_t* g, mcInstance_t* in) {
		if (!g->insideBuilt) BuildInside(g);
		if (g->insidePts.empty() || g->insideTris.empty()) return;
		vec3_t up, e1, e2, tmp = { 1.0f, 0.0f, 0.0f };
		mcCopy(in->up, up);
		if (mcNorm(up) < 0.5f) return;
		if (fabsf(up[0]) > 0.9f) { tmp[0] = 0.0f; tmp[1] = 1.0f; }
		mcCross(up, tmp, e1);
		mcNorm(e1);
		mcCross(up, e1, e2);
		const float* org = in->lastOrigin;
		const float PI2 = 6.2831853f, secW = PI2 / MC_INSIDE_SECTORS;
		thread_local static std::vector<int> bins[MC_INSIDE_SECTORS];
		for (auto& b : bins) b.clear();
		for (const int t : g->insideTris) {
			float az[3];
			for (int k = 0; k < 3; k++) {
				vec3_t d;
				mcSub(&in->p[g->tris[t * 3 + k] * 3], org, d);
				az[k] = atan2f(mcDot(d, e2), mcDot(d, e1));
			}
			std::sort(az, az + 3);
			// the smallest arc round the axis that holds the three corners: all but the largest gap between them
			const float g0 = az[1] - az[0], g1 = az[2] - az[1], g2 = az[0] + PI2 - az[2];
			float from = az[0], arc = az[2] - az[0];
			if (g0 >= g1 && g0 >= g2) { from = az[1]; arc = PI2 - g0; }
			else if (g1 >= g0 && g1 >= g2) { from = az[2]; arc = PI2 - g1; }
			if (arc > 3.1415927f) { for (auto& b : bins) b.push_back(t); continue; }
			const int s0 = (int)floorf((from + 3.1415927f) / secW), s1 = (int)floorf((from + arc + 3.1415927f) / secW);
			for (int s = s0; s <= s1; s++) bins[((s % MC_INSIDE_SECTORS) + MC_INSIDE_SECTORS) % MC_INSIDE_SECTORS].push_back(t);
		}
		const float margin = MC_MARGIN * in->linkScale;
		for (const int p : g->insidePts) {
			float* x = &in->p[p * 3];
			vec3_t d, a, v;
			mcSub(x, &in->target[p * 3], d);
			if (mcDot(d, d) < MC_INSIDE_FAR * MC_INSIDE_FAR * in->linkScale * in->linkScale) continue;	// near its animated place
			mcSub(x, org, d);
			mcMA(org, mcDot(d, up), up, a);
			mcSub(x, a, v);
			const float r = mcLen(v);
			if (r < 0.5f) continue;
			int s = (int)floorf((atan2f(mcDot(v, e2), mcDot(v, e1)) + 3.1415927f) / secW);
			s = s < 0 ? 0 : (s >= MC_INSIDE_SECTORS ? MC_INSIDE_SECTORS - 1 : s);
			const unsigned sewn = g->insideSewn[g->partSurf[p]];
			float best = 2.0f;
			int bt = -1;
			for (const int t : bins[s]) {
				const int ia = g->tris[t * 3], ib = g->tris[t * 3 + 1], ic = g->tris[t * 3 + 2];
				if (sewn & (1u << g->partSurf[ia])) continue;
				float tt;
				if (InsideSegTri(a, x, &in->p[ia * 3], &in->p[ib * 3], &in->p[ic * 3], tt) && tt < best) { best = tt; bt = t; }
			}
			if (bt < 0) continue;
			{	// the animated cloth has it outside there too: the model drapes it so
				const float* y = &in->target[p * 3];
				vec3_t dy, ay;
				mcSub(y, org, dy);
				mcMA(org, mcDot(dy, up), up, ay);
				const int ia = g->tris[bt * 3], ib = g->tris[bt * 3 + 1], ic = g->tris[bt * 3 + 2];
				float tt;
				if (InsideSegTri(ay, y, &in->target[ia * 3], &in->target[ib * 3], &in->target[ic * 3], tt)) continue;
			}
			vec3_t vn, mv;
			mcScale(v, 1.0f / r, vn);
			float keep = best * r - margin;
			if (keep < 0.5f * best * r) keep = 0.5f * best * r;
			float back = r - keep;
			if (back > MC_INSIDE_STEP * in->linkScale) back = MC_INSIDE_STEP * in->linkScale;
			mcScale(vn, -back, mv);
			mcAdd(x, mv, x);
			float* pv = &in->prev[p * 3];
			mcAdd(pv, mv, pv);	// a move, not a speed
			vec3_t vel;
			mcSub(x, pv, vel);
			const float vo = mcDot(vel, vn);
			if (vo > 0.0f) mcMA(pv, vo, vn, pv);	// and no speed outwards
		}
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
		// SJE r_meshClothFine: "a good part of the free cloth" is a part of its AREA. Counted in points it depends on
		// where the mesh is fine: of Vader's cape, standing, 7.8 % of the points are under the floor (the hem row of a
		// floor-length cape: the rule is off), of the same cape with every triangle split in four 4.7 % (the rule is
		// on) - the hem is one row of points either way, the rest of the cape has four times as many. So the fine
		// cape was held to the layer order standing, stopping, turning and getting up, where the coarse one is left
		// alone, and it shook there. By area the fine cape measures 7.6 %: what the coarse one's points say. Coarser
		// cloth keeps the count of points it was tuned with.
		// (Only for fine cloth that collides with itself, as the stencils: the game's other fine cloth keeps its count.)
		if (prm->floor && prm->fine > 0.0f && g->spacing > 0.0f && g->spacing < prm->fine && g->anySelf && (int)g->partArea.size() == g->numParticles) {
			float under = 0.0f, all = 0.0f;
			for (int p = 0; p < g->numParticles; p++) {
				if (g->freedom[p] <= 0.0f) continue;
				all += g->partArea[p];
				vec3_t df;
				mcSub(&in->target[p * 3], prm->floorPt, df);
				if (mcDot(df, prm->floorN) < -MC_LAYER_UNDER) under += g->partArea[p];
			}
			if (all > 0.0f) pile = (under / all - MC_LAYER_PILE) / (0.5f * MC_LAYER_PILE);
			if (pile < 0.0f) pile = 0.0f;
			if (pile > 1.0f) pile = 1.0f;
		}
		else if (prm->floor) {
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
		const bool body = prm->layers != 2;	// SJE: (see MC_LAYER_DEEP) 2 = the rule without its body tests
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
				// a triangle turned over against the animation has no side (a worn one crumpled; a loose one too - a
				// cape folded over in a storm, a skirt crumpled at the leg)
				if ((wornT || body) && mcDot(n, tn) <= 0.0f) continue;
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
			if (body && h * lp.sgn < -MC_LAYER_DEEP * in->linkScale) continue;	// SJE: far on the wrong side: not over or under it any more
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
			float wp = gp * (fp <= 0.0f ? 0.0f : (fp < 1.0f ? fp : 1.0f));
			float w[3] = { ut * ga * (fa <= 0.0f ? 0.0f : (fa < 1.0f ? fa : 1.0f)) * bc[0], ut * gb * (fb <= 0.0f ? 0.0f : (fb < 1.0f ? fb : 1.0f)) * bc[1],
				ut * gc * (fc <= 0.0f ? 0.0f : (fc < 1.0f ? fc : 1.0f)) * bc[2] };
			// SJE: ... and not into a body shape the side lies against (see MC_LAYER_BODY): the point moves along sgn x n, the
			// triangle's corners the other way
			if (body && in->contact.size() == (size_t)g->numParticles && in->contactN.size() == in->p.size()) {
				for (int k = 0; k < 4; k++) {
					float& wk = k ? w[k - 1] : wp;
					const size_t q = (size_t)lid[k];
					if (wk > 0.0f && in->contact[q] && (k ? -lp.sgn : lp.sgn) * mcDot(n, &in->contactN[q * 3]) < -MC_LAYER_BODY) wk = 0.0f;
				}
			}
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
		// SJE: a shape's middle and half length: a point further from the middle than the half length plus its
		// reach cannot be within reach of the axis, so the exact test below is only made for shapes near the
		// point (with a dense cape most point / shape tests were of shapes far away). The list is the same.
		float mid[MC_NEAR_CAPS][3], half[MC_NEAR_CAPS];
		for (int c = 0; c < ncaps; c++) {
			vec3_t ab;
			mcSub(caps[c].b, caps[c].a, ab);
			half[c] = 0.5f * mcLen(ab);
			for (int k = 0; k < 3; k++) mid[c][k] = 0.5f * (caps[c].a[k] + caps[c].b[k]);
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
				const float reach = move[c] + travel;
				const float far = reach + half[c];
				mcSub(x, mid[c], d);
				if (mcDot(d, d) > far * far * 1.0005f + 0.01f) continue;	// out of reach for sure (see above)
				vec3_t q;
				ClosestOnAxis(caps[c], x, q);
				mcSub(x, q, d);
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

	static void FaceCollide(const mcGroup_t* g, mcInstance_t* in, const mcCapsule_t* caps, int ncaps, int overArms, int rest, int ease, int lie, int armOn) {
		// SJE 'armgap' (r_meshClothArmGap): a cloak over the arms is kept further off them and pushed out harder
		const float armGl = in->capeArm > g->armGap ? in->capeArm : g->armGap;	// SJE r_meshClothFree (MC_CAPE_ARM)
		const float armG = armOn && armGl > 0.0f ? armGl * in->linkScale : 0.0f;
		if (in->facePairs.empty()) return;
		const int nt = (int)(g->tris.size() / 3);
		std::vector<float>& sph = in->triSphere;
		sph.assign((size_t)nt * 5, -1.0f);		// radius < 0: not worked out in this substep yet
		int tArm = 7;	// SJE r_meshClothTailSide: which cloth the wind references speak for (see MC_TAIL_ARM_D0)
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
			const float minD = cap.r + margin + (tool ? MC_FACE_TOOL : 0.0f) + (limb && !tool ? armG : 0.0f);	// SJE 'armgap'
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
			// (SJE r_meshClothTailSide: against a shape that moves, for a corner the wind has carried beside its wearer, its wind
			// reference place stands in for the animated one - see MC_TAIL_REF_PHI)
			vec3_t tp, tq, nt3;
			const bool capOn = fp.cap < (int)in->tailCapOn.size() && in->tailCapOn[fp.cap];
			const bool refOn = capOn && (in->tailRefMode & 1) && in->tailRefK.size() == (size_t)g->numParticles;
			const float* tg[3];
			const bool refAny = refOn && (in->tailRefK[id[0]] > 0.0f || in->tailRefK[id[1]] > 0.0f || in->tailRefK[id[2]] > 0.0f);
			const bool refAll = capOn && in->tailRefK.size() == (size_t)g->numParticles && in->tailRefK[id[0]] > 0.0f && in->tailRefK[id[1]] > 0.0f && in->tailRefK[id[2]] > 0.0f;
			const bool refTri = !(tArm & 2) || refAll;	// (all three corners or none: see MC_TAIL_ARM_D0)
			for (int k = 0; k < 3; k++) tg[k] = refOn && refTri && in->tailRefK[id[k]] > 0.0f ? &in->tailRef[(size_t)id[k] * 3] : &in->target[id[k] * 3];
			for (int k = 0; k < 3; k++) tp[k] = bc[0] * tg[0][k] + bc[1] * tg[1][k] + bc[2] * tg[2][k];
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
				mcSub(tg[1], tg[0], e1);
				mcSub(tg[2], tg[0], e2);
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
			// SJE r_meshClothTailSide: cloth the wind reference speaks for, against an arm or a hand - the side it was on when the step
			// began holds, and it is free cloth (see MC_TAIL_ARM_D0)
			const bool armKeep = (tArm & 1) && refAny && cap.hard == 1 && fp.d0 > 1e-4f && fp.d0 >= MC_TAIL_ARM_D0 * cap.r;
			if (armKeep) over = bring = false;
			if (armKeep) rests = false;
			else if (hand && dt < minD && tube) rests = true;
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
			if (limb && !rests && (tool || over || bring || dt >= minD || dt < 0.5f * cap.r || armKeep)) {
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
				else if (fp.d0 >= 0.8f * minD || armKeep) ref = fp.n0;
				else if (over) { mcCopy(nOut, refv); ref = refv; }
				else if (dt >= minD) { mcScale(nt3, 1.0f / dt, refv); ref = refv; }
				else {
					// outwards: the animated triangle's normal, turned away from the spine (the line through
					// the character's origin along its up direction)
					vec3_t e1, e2, od;
					mcSub(tg[1], tg[0], e1);
					mcSub(tg[2], tg[0], e2);
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
			// SJE r_meshClothTailSide: a cape's cloth the rule is pushing keeps, against a body shape that moves, the side it was
			// on when the step began - where it was clear of the shape then. A leg that swings into the wing pushes it ahead of
			// itself; with the animated cape as the reference the test threw the wing behind his back (see MC_TAIL_REF_PHI).
			// (only cloth that has been carried away from its animated place - see MC_TAIL_ARM_D0)
			if (capOn && (in->tailRefMode & 4) && !limb && in->tailHi.size() == (size_t)g->numParticles && in->tailHi[id[0]] && in->tailHi[id[1]] && in->tailHi[id[2]]
				&& (!(tArm & 4) || refAll)) {
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
				if (fp.d0 >= 0.8f * minD) { lim = minD; ref = fp.n0; }
			}
			if (lim < 0.25f) continue;
			const float side = ref ? mcDot(nv, ref) : 0.0f;
			float depth;
			vec3_t dir;
			bool through = ref && (dist < 1e-4f || side < -0.3f * dist);
			// SJE r_meshClothLimbLie: cloth that lies against an arm is not thrown again and again. A crouching figure's cloak lies
			// between his arm (resting on the knee) and his body: the test takes it for caught under the arm and throws it out over
			// the arm ('bring'), or takes a triangle at the clearance for 'through' when its closest point flips round the arm; the
			// links pull it straight back, and the throw came again every 2-3 steps - a point flew 3 units to and fro, the cloak
			// buzzed and never slept (Kylo). As r_meshClothRest does for the body: the first throw is made as before; a
			// triangle an arm threw lately that lies about where it lay when the step began, not inside the clearance, while the
			// pose holds it still and the arm holds still, is where the cloth lies - the throw did not hold. A moving arm or pose,
			// blades and hilts, and cloth that really went in are thrown as before.
			if (through && lie && (cap.hard == 1 || cap.hard == 2) && poseStill && in->faceCapsFrom && in->faceStart.size() == in->p.size()
				&& in->limbThrown.size() == (size_t)g->numParticles && (in->limbThrown[id[0]] || in->limbThrown[id[1]] || in->limbThrown[id[2]])
				&& dist >= lim - MC_LIE_EPS * in->linkScale) {
				const mcCapsule_t& c0 = in->faceCapsFrom[fp.cap];
				const float sl2 = MC_FACE_REST * MC_FACE_REST * in->linkScale * in->linkScale, ml2 = MC_LIE_MOVE * MC_LIE_MOVE * in->linkScale * in->linkScale;
				vec3_t da, db;
				mcSub(cap.a, c0.a, da);
				mcSub(cap.b, c0.b, db);
				bool lies = mcDot(da, da) <= sl2 && mcDot(db, db) <= sl2;
				for (int k = 0; k < 3 && lies; k++) {
					vec3_t dk;
					mcSub(x[k], &in->faceStart[id[k] * 3], dk);
					if (mcDot(dk, dk) > ml2) lies = false;
				}
				if (lies) through = false;
			}
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
			// SJE r_meshClothTailSide: cloth the wind has taken round to its wearer's side lies there as well - beside him, where
			// the animation never has it. The test took it for 'gone through' the body as soon as it came near (a run backwards
			// into the wind: the arm swings into the wing) and threw it back to the animated cape's side: the hem from 14 units
			// beside him to 15 behind him in 0.06 s, the push took it out again, round and round every 0.6 s. A side test that only knows
			// the pose cannot tell; what tells is where the cloth was (a point's last good side: Lewin, GDC 2018). So a triangle
			// whose corners the rule is pushing, clear of the shape when the step began and about where it is now, lies there:
			// kept clear of the shape (the normal contact), not thrown. Only while he runs (MC_TAIL_RUN): standing, the wing
			// holds its place by itself, and the throw is what brings cloth back that did get round to his front - without it
			// there the dense test cape on the saber arm's side had cloth inside the body in 541 / 567 triangle-steps instead of
			// 38 / 4, and the saber-lit stance stretched two to four times as often.
			const bool windOut = through && !limb && in->tailA.size() == in->p.size()
				&& mcDot(in->originVel, in->originVel) - mcDot(in->originVel, in->up) * mcDot(in->originVel, in->up) > MC_TAIL_RUN * MC_TAIL_RUN
				&& mcDot(&in->tailA[id[0] * 3], &in->tailA[id[0] * 3]) > 0.0f && mcDot(&in->tailA[id[1] * 3], &in->tailA[id[1] * 3]) > 0.0f
				&& mcDot(&in->tailA[id[2] * 3], &in->tailA[id[2] * 3]) > 0.0f;
			const bool liesStill = poseStill && (lain || (rest && (in->thrown[id[0]] || in->thrown[id[1]] || in->thrown[id[2]])));
			if (through && !limb && dist >= 1e-4f && in->thrown.size() == (size_t)g->numParticles && (liesStill || windOut)) {
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
					if (liesStill) for (int k = 0; k < 3; k++) in->thrown[id[k]] = MC_THROWN_STEPS;	// it lies here: left alone while it does
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
			const float fStep = limb && !tool ? MC_FACE_STEP * (armG > 0.0f ? MC_ARMGAP_STEP : 1.0f) : MC_FACE_STEP;	// SJE 'armgap'
			if (depth > fStep) depth = fStep;
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
				if (mv > fStep) mv = fStep;
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
				if (through && lie && (cap.hard == 1 || cap.hard == 2) && in->limbThrown.size() == (size_t)g->numParticles) in->limbThrown[id[k]] = MC_THROWN_STEPS;	// SJE r_meshClothLimbLie
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
		in->dampVelOk = false;	// SJE r_meshClothDampLag
		in->turnRate = in->turnCalm = in->turnSigned = 0.0f;	// SJE r_meshClothTurnCalm
		in->whirlRate = in->whirlTurn = in->whirlBack = in->whirlPath = in->whirlPathBack = in->whirlW = 0.0f;	// SJE r_meshClothWhirl
		in->whirlAcc[0] = in->whirlAcc[1] = in->whirlAcc[2] = 0.0f;
		in->whirlA.clear();
		in->whirlV.clear();
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
		// SJE: where each cell's run starts in the sorted list, found through a small hash table instead of a
		// binary search per neighbour cell (27 searches per point were a third of a dense cape's step set-up).
		// The runs and their order are the same, so the pairs come out in the same order as before.
		thread_local static std::vector<int> slot;
		size_t tabSize = 64;
		while (tabSize < cells.size() * 2) tabSize <<= 1;
		slot.assign(tabSize, -1);
		const size_t tabMask = tabSize - 1;
		auto slotOf = [&](long long k) { return (size_t)(((unsigned long long)k * 0x9E3779B97F4A7C15ULL) >> 20) & tabMask; };
		for (size_t i = 0; i < cells.size(); i++) {
			if (i > 0 && cells[i].key == cells[i - 1].key) continue;
			size_t s = slotOf(cells[i].key);
			while (slot[s] >= 0) s = (s + 1) & tabMask;
			slot[s] = (int)i;
		}
		const cellEntry_t* const cellEnd = cells.data() + cells.size();
		const size_t maxPairs = 65536 * 2;	// safety cap (a broken scale bunching the whole cloth together)
		for (int p = 0; p < g->numParticles && in->selfPairs.size() < maxPairs; p++) {
			if (g->freedom[p] <= 0.0f || !g->selfCol[p]) continue;
			const float* a = &in->p[p * 3];
			const long long cx = cellOf(a, 0), cy = cellOf(a, 1), cz = cellOf(a, 2);
			for (long long dx = -1; dx <= 1; dx++) for (long long dy = -1; dy <= 1; dy++) for (long long dz = -1; dz <= 1; dz++) {
				const long long k = key(cx + dx, cy + dy, cz + dz);
				size_t s = slotOf(k);
				while (slot[s] >= 0 && cells[slot[s]].key != k) s = (s + 1) & tabMask;
				if (slot[s] < 0) continue;
				for (const cellEntry_t* it = cells.data() + slot[s]; it != cellEnd && it->key == k; ++it) {
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
		{	// SJE r_meshClothTailSide: the edge's spread along its line against across it
			const float mean = 0.5f * (sxx + syy), dif = sqrtf(0.25f * (sxx - syy) * (sxx - syy) + sxy * sxy);
			in->sideLine = mean > 1e-6f ? dif / mean : 0.0f;
		}
		for (int k = 0; k < 3; k++) in->side[k] = e1[k] * cosf(ang) + e2[k] * sinf(ang);
		mcNorm(in->side);
	}

	// SJE r_meshClothWhirl: how far a turn has gone on in ONE direction (degrees, with its direction): 'da' more of it this
	// frame. Turned back by more than MC_WHIRL_BACK the count starts again from there; it fades over MC_WHIRL_MEM seconds and
	// holds no more than 'cap'.
	static void WhirlCount(float& turn, float& back, float da, float dts, float cap);
	// Max spin: the rotational partner of max acceleration. Saber moves whip
	// the shoulders round far faster than cloth can follow, which winds a cape
	// into a rope. Measure how fast the attached edge turns about the up axis
	// this frame; any turning beyond the limit carries the free cloth round
	// with it instead of winding it up.

	// bind-pose distance of every particle from the attached edge, along the cloth's links
	static void WhirlCount(float& turn, float& back, float da, float dts, float cap) {
		if (turn == 0.0f || (da >= 0.0f) == (turn > 0.0f)) { turn += da; back = 0.0f; }
		else {
			back += da;
			if (fabsf(back) > MC_WHIRL_BACK) { turn = back; back = 0.0f; }
		}
		turn *= expf(-dts / MC_WHIRL_MEM);
		if (fabsf(turn) > cap) turn = turn > 0.0f ? cap : -cap;
	}

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
			g->sheetLen.swap(longest);	// SJE r_meshClothNoRoom
		}
	}

	// SJE: heavy-cape options, worked out once per group (lazy, main thread, after ComputeRootDist).
	// 'spring S A': each point's pull per 16 ms toward its animated spot, S x exp(-A x its distance along the cloth from
	// the attached edge in metres, 39.37 units each) - strong near the collar, weak at the hem.
	// 'ring D': the second tether level. The upper cloth hangs from the attached edge; every point lower down is also tied,
	// with no stretch allowed, to a ring of points further down, so the lower cape keeps its length from that ring and
	// swings round it as one piece instead of stretching. The ring is the first row of
	// points D units (bind, along the cloth) below the attached edge; every point further down is tied to the ring point
	// nearest to it along the cloth, at most that bind distance away (or the pose's, where the pose is longer). The
	// first level is the long range attachment every free point has to the attached edge (lraAnchor).
	static void ComputeRing(const mcGroup_t* g) {
		if (g->ringBuilt || !g->anyHeavy) return;
		g->ringBuilt = true;
		const int n = g->numParticles;
		if ((int)g->rootDist.size() != n || (int)g->ringD.size() != n || (int)g->springS.size() != n) return;
		g->springK.assign(n, 0.0f);
		for (int p = 0; p < n; p++) {
			if (g->springS[p] <= 0.0f || g->freedom[p] <= 0.0f) continue;
			g->springK[p] = g->springS[p] * expf(-g->springA[p] * g->rootDist[p] * (1.0f / 39.37f));
		}
		g->ringAnchor.assign(n, -1);
		g->ringLen.assign(n, 0.0f);
		if (!g->anyRing) return;
		std::vector<std::vector<std::pair<int, float>>> adj(n);
		for (size_t l = 0; l < g->linkA.size(); l++) {
			if (g->linkBend[l]) continue;
			const int a = g->linkA[l], b = g->linkB[l];
			vec3_t d; mcSub(&g->bindPos[a * 3], &g->bindPos[b * 3], d);
			const float len = mcLen(d);
			adj[a].push_back({ b, len }); adj[b].push_back({ a, len });
		}
		auto lower = [&](int p) { return g->ringD[p] > 0.0f && g->freedom[p] > 0.0f && g->rootDist[p] >= g->ringD[p]; };
		std::vector<float> dist(n, 1e30f);
		std::vector<int> from(n, -1);
		std::priority_queue<std::pair<float, int>, std::vector<std::pair<float, int>>, std::greater<std::pair<float, int>>> q;
		for (int p = 0; p < n; p++) {
			if (!lower(p)) continue;
			bool edge = false;
			for (const auto& e : adj[p]) if (g->rootDist[e.first] < g->ringD[p]) { edge = true; break; }
			if (!edge) continue;
			dist[p] = 0.0f; from[p] = p;
			q.push({ 0.0f, p });
		}
		while (!q.empty()) {
			const auto top = q.top(); q.pop();
			if (top.first > dist[top.second]) continue;
			for (const auto& e : adj[top.second]) {
				if (!lower(e.first)) continue;
				const float nd = top.first + e.second;
				if (nd < dist[e.first]) { dist[e.first] = nd; from[e.first] = from[top.second]; q.push({ nd, e.first }); }
			}
		}
		for (int p = 0; p < n; p++) {
			if (from[p] < 0 || from[p] == p) continue;
			g->ringAnchor[p] = from[p];
			g->ringLen[p] = dist[p];
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

	// SJE r_meshClothFree 4 (see MC_CAPE_FLING): the pull of a fast turn on a free cape, and the share of the turn it
	// is carried along by. c1 = the attached edge's centre, up = the upright axis. A function of its own that is never
	// inlined: the code of SpinLimit round the call stays what it was.
#if defined(_MSC_VER)
	__declspec(noinline)
#else
	__attribute__((noinline))
#endif
	static void FreeCapeTurn(const mcGroup_t* g, mcInstance_t* in, const mcParams_t* prm, const float* c1, const float* up, int dtMs) {
		float ft = 0.0f, ff = 0.0f;
		if (prm && (int)(prm->freeCape + 0.5f) == 4) { ft = MC_CAPE_TURN; ff = MC_CAPE_FLING; }
		else if (prm && (int)(prm->freeCape + 0.5f) >= 5) { ft = MC_CAPE_TURN5; ff = MC_CAPE_FLING5; }
		thread_local static std::vector<unsigned char> fp;
		if ((ft > 0.0f || ff > 0.0f) && FreeCapePoints(g, prm, &fp)) {
			// the turn of this step by the eased rate (60 ms, with its direction): a tremble to and fro is no turn
			const float wStep = in->whirlRate * (3.14159265f / 180.0f) * (dtMs * 0.001f);
			// ... and only a FAST turn counts (MC_CAPE_TURN_W0 .. W1 degrees/s): below that it is setting 1
			const float fast = Clamp01((fabsf(in->whirlRate) - MC_CAPE_TURN_W0) / (MC_CAPE_TURN_W1 - MC_CAPE_TURN_W0));
			const float ca = wStep * Clamp01(ft) * fast;
			const float cs = cosf(ca), sn = sinf(ca);
			// the pull is for a turn, not for circling: it goes as the circling rule comes in
			const float tn = fabsf(in->whirlTurn);
			const float fade = tn <= MC_WHIRL_TURN0 ? 1.0f : (tn >= MC_WHIRL_TURN1 ? 0.0f : (MC_WHIRL_TURN1 - tn) / (MC_WHIRL_TURN1 - MC_WHIRL_TURN0));
			const float fl = ff * fade * fast * wStep * wStep;	// (turn rate x step)^2: the pull's way in this step, per unit from the axis
			for (int p = 0; p < g->numParticles; p++) {
				if (!fp[p] || g->freedom[p] <= 0.0f) continue;
				const float w = Clamp01(g->freedom[p]);
				float* pts[2] = { &in->p[p * 3], &in->prev[p * 3] };
				for (int qi = 0; qi < 2; qi++) {
					float* q = pts[qi];
					vec3_t r, par, perp, side;
					mcSub(q, c1, r);
					mcScale(up, mcDot(r, up), par);
					mcSub(r, par, perp);
					mcCross(up, perp, side);
					// (only the new position goes outwards: a push that stays as speed, not a shift)
					const float out = qi == 0 ? 1.0f + fl * w : 1.0f;
					for (int k = 0; k < 3; k++) q[k] = c1[k] + par[k] + (perp[k] * cs + side[k] * sn) * out;
				}
			}
		}
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
		{	// SJE r_meshClothTurnCalm: how fast the attached edge turns (eased over 60 ms)
			// ... the eased turn WITH its direction, then its size: a tremble to and fro is no turn. The game's slope
			// stance flickered between two leg poses in a stand with the saber lit (t1_rail's roof), the shoulders jumped 5 - 19
			// degrees either way every step - 400 - 550 degrees/s by the size of each step's turn alone - and the cape was held
			// as in a spin: it hung behind his back, the push to the side tripled-damped away. While a turn keeps its direction
			// this is the same number as before. (r_meshClothTurnCalm 2: the size of each step's turn, as before.)
			const float dts = dtMs * 0.001f, ease = 1.0f - expf(-dts / 0.06f);
			if (prm && prm->turnCalm >= 2.0f) {
				const float rate = fabsf(angle) / dts * (180.0f / 3.14159265f);
				in->turnRate += (rate - in->turnRate) * ease;
			}
			else {
				in->turnSigned += (angle / dts * (180.0f / 3.14159265f) - in->turnSigned) * ease;
				in->turnRate = fabsf(in->turnSigned);
			}
		}
		{	// SJE r_meshClothWhirl: the turn rate with its direction (eased over 60 ms), and how far the turn has gone on one way
			const float dts = dtMs * 0.001f, da = angle * (180.0f / 3.14159265f);
			in->whirlRate += (da / dts - in->whirlRate) * (1.0f - expf(-dts / 0.06f));
			WhirlCount(in->whirlTurn, in->whirlBack, da, dts, MC_WHIRL_TURN1 * MC_WHIRL_CAP);
		}
		if (angle == 0.0f) { in->twistRun = 0.0f; return; }
		// a turn faster than MC_TWIST_FREE deg/s (the player's body snapping round with the camera,
		// an instant re-face in an animation) carries the cloth by the excess
		{
			float twF = prm && prm->twistFree > 0.0f ? prm->twistFree : MC_TWIST_FREE;
			{	// SJE r_meshClothFree (see MC_CAPE_TWIST)
				float ft = MC_CAPE_TWIST;
				if (ft > 0.0f && ft < twF && FreeCapePoints(g, prm, nullptr)) twF = ft;
			}
			const float freeT = twF * (3.14159265f / 180.0f) * (dtMs * 0.001f);
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
		FreeCapeTurn(g, in, prm, c1, up, dtMs);	// SJE r_meshClothFree 4
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
		if (prm->airPhys > 0.0f) {
			// SJE r_meshClothAirPhys: the lift limit is open in every step (see MC_AIRPHYS_HIP) - a cape's end may flip up and
			// over behind its wearer, never forward over him; worn cloth and cloth that hangs from the hips keep it closed.
			bool keep = false;
			if (!keep) {
				solve.liftOpenAll = true;
				if ((int)g->wrap.size() == g->numParticles && (int)g->under.size() == g->numParticles) { solve.liftWrap = g->wrap.data(); solve.liftUnder = g->under.data(); }
				// cloth that hangs from the hips: by where its attached edge lies under the top of the body's shapes (not arms
				// or held things: a raised saber is no head)
				thread_local static std::vector<unsigned char> lowP;
				float top = -1.0e9f;
				for (int c = 0; c < ncaps; c++) {
					if (caps[c].hard == 1 || caps[c].hard == 4) continue;
					const float ha = mcDot(caps[c].a, in->up), hb = mcDot(caps[c].b, in->up);
					const float hc = (ha > hb ? ha : hb) + caps[c].r;
					if (hc > top) top = hc;
				}
				if (top > -1.0e8f && (int)g->lraAnchor.size() == g->numParticles && in->p.size() == (size_t)g->numParticles * 3) {
					float hip = MC_AIRPHYS_HIP;
					hip *= in->linkScale > 1e-3f ? in->linkScale : 1.0f;
					lowP.assign(g->numParticles, 0);
					for (int p = 0; p < g->numParticles; p++) {
						const int an = g->lraAnchor[p];
						if (an >= 0 && top - mcDot(&in->p[an * 3], in->up) > hip) lowP[p] = 1;
					}
					solve.liftLow = lowP.data();
				}
			}
		}
		// SJE r_meshClothFree: this group's free cloth (see MC_CAPE_UMAX)
		thread_local static std::vector<unsigned char> fcP, fcLift;
		const bool anyFc = FreeCapePoints(g, prm, &fcP);
		float fcUmax = MC_CAPE_UMAX, fcPeak = MC_CAPE_PEAK, fcSkin = MC_CAPE_SKIN, fcTail = MC_CAPE_TAIL, fcGain = MC_CAPE_GAIN, fcDamp = MC_CAPE_DAMP;
		float fcBillow = MC_CAPE_BILLOW, fcSpring = MC_CAPE_SPRING, fcArm = MC_CAPE_ARM, fcArmAir = MC_CAPE_ARMAIR, fcPSide = MC_CAPE_PSIDE;
		int fcRules = MC_CAPE_RULES;
		bool fcLiftOff = true;
		// r_meshClothFree 2: the calm one - the turn-calm rule and the whole 'spring' stay (the wind's part and no lift limit
		// only). 3: the heavy one - the air moves it less (MC_CAPE_GAIN 0.4, half the billow push; a storm pushes as before,
		// the cap higher for that), so it hangs lower in a run and swings by its wearer's moves.
		if (anyFc) {
			const int fcSet = (int)(prm->freeCape + 0.5f);
			if (fcSet == 2) { fcRules = 0; fcSpring = 1.0f; }
			else if (fcSet == 3) { fcGain = 0.4f; fcBillow = 0.5f; fcUmax = 700.0f; }
			// (4, 5: the cape stands out in a turn - MC_CAPE_FLING in SpinLimit; the rest as setting 1)
		}
		in->capeArm = anyFc ? fcArm : 0.0f;
		if (anyFc && fcLiftOff) {
			fcLift.assign(fcP.begin(), fcP.end());
			if (solve.liftLow) for (int p = 0; p < g->numParticles; p++) if (solve.liftLow[p]) fcLift[p] = 0;	// (cloth that hangs from the hips keeps its limit)
			solve.liftFree = fcLift.data();
		}
		solve.liftSlope = prm->liftSlope > 0.0f ? (prm->liftSlope < 1.0f ? prm->liftSlope : 1.0f) : 0.0f;
		solve.floorOk = prm->floor ? 1 : 0;
		mcCopy(prm->floorPt, solve.floorPt);
		mcCopy(prm->floorN, solve.floorN);
		solve.edgeCol = prm->edgeCol;
		solve.faceCol = g->tris.size() >= 3 ? prm->faceCol : 0;
		solve.overArms = prm->overArms;
		solve.rest = prm->rest != 0;
		solve.ease = prm->ease != 0;
		solve.limbLie = prm->limbLie;
		// SJE: heavy-cape options ('damp', 'spring', 'bell', 'ring'; only groups with a line that asks for one).
		solve.armGapOn = prm->armGap;	// SJE r_meshClothArmGap
		// Per substep each point gets its animated spot (in->anchor): the animated pose, with the hip-carried shape
		// blended in where the line has 'bell' (BellPose), never under the floor.
		// SJE r_meshClothAirFree: how far the air against the character frees the cloth in this step
		float airFree = 0.0f;
		{
			float want = 0.0f;
			if (prm->airFree > 0.0f) {
				vec3_t air;
				mcAdd(prm->wind, prm->zoneWind, air);
				mcSub(in->originVel, air, air);
				float u1 = MC_FREE_U1;
				want = Clamp01(prm->airFree) * Clamp01((mcLen(air) - MC_FREE_U0) * (1.0f / (u1 - MC_FREE_U0)));
			}
			in->airFree += (want - in->airFree) * (1.0f - expf(-MC_DT / (want > in->airFree ? MC_FREE_IN : MC_FREE_OUT)));
			if (in->airFree < 1e-3f && want <= 0.0f) in->airFree = 0.0f;
			airFree = in->airFree;
		}
		const bool heavy = g->anyHeavy && (int)g->dampH.size() == g->numParticles && (int)g->springK.size() == g->numParticles;
		const bool bellStep = heavy && g->anyBell && in->bellOk && in->bellFrom.size() == tgtTo.size() && in->bellTo.size() == tgtTo.size();
		solve.bell = bellStep ? 1 : 0;
		solve.ring = heavy && g->anyRing && (int)g->ringAnchor.size() == g->numParticles && (int)g->ringLen.size() == g->numParticles ? 1 : 0;
		thread_local static std::vector<float> kDampH, kDampV, kSpring;
		if (heavy) {
			const float isub = 1.0f / sub;
			// 'spring' lets go while its wearer runs: held to the hips the cape hung straight down in a run (the
			// travel wind could not stream it back); standing, walking, turning and swinging the saber it pulls in
			// full. Walking pace (100 units/s) and below: all of it; 190 and up (a run): none. It lets go over about
			// 0.15 s when a run starts; after a run it takes hold again slowly (MC_SPRING_BACK): taken up in 0.15 s,
			// the cape streaming behind was yanked to the hips when he stopped - the hem swung from 15 units behind
			// to 6 in front and 9 to the side within 0.3 s, then snapped into the pose. Slowly, it glides forward
			// on its own.
			{
				const float vu = mcDot(in->originVel, in->up);
				vec3_t vh;
				mcMA(in->originVel, -vu, in->up, vh);
				float want = MCDBG_OFF("NOSPRINGRUN") ? 0.0f : Clamp01((mcLen(vh) - 100.0f) * (1.0f / 90.0f));
				// ... and while he jumps or falls (up-down 100 units/s and more: lets go fully at 190): held to the hips
				// in a jump from standing the cape stayed glued to him in the air and on landing - its hem rose 5 units
				// over the pose where a free cape rises 21
				const float wj = Clamp01((fabsf(vu) - 100.0f) * (1.0f / 90.0f));
				if (wj > want) want = wj;
				// ... and in wind (the map's weather wind, a level wind, a pusher's air stream), by the same measure: air
				// going past at walking pace and more lets it go as a run does. Held to the hips, a standing character's
				// cape hung still in a storm and only blew while he moved. No wind (most maps): nothing changes.
				{
					vec3_t air;
					mcAdd(prm->wind, prm->zoneWind, air);
					const float ww = Clamp01((mcLen(air) - 100.0f) * (1.0f / 90.0f));
					if (ww > want) want = ww;
				}
				// SJE r_meshClothSpringPose: ... and when the pose itself jumps (an attack starts, one animation gives way to the
				// next: the hips snap round within a frame or two). 'spring' pulls to the hip-carried shape, and that shape jumped
				// with the pose: the cape was pulled over after it - of a jump of its animated place the hem made half within
				// 0.03 s (without 'spring': a fifth), at attacks and at any change of animation. By the speed of the spring's own
				// anchors against his travel; at once, not eased in - it is the pull of that very frame that moves the cloth.
				if (prm->springPose > 0.0f && tgtFrom.size() == in->p.size() && tgtTo.size() == in->p.size()) {
					float sw = 0.0f, sv = 0.0f;
					for (int p = 0; p < g->numParticles; p++) {
						const float kp = g->springK[p];
						if (!(kp > 0.0f) || g->freedom[p] <= 0.0f) continue;
						vec3_t a0, a1, dA;
						mcCopy(&tgtFrom[(size_t)p * 3], a0);
						mcCopy(&tgtTo[(size_t)p * 3], a1);
						if (bellStep && g->bellW[p] > 0.0f) {
							const float w = Clamp01(g->bellW[p] * g->freedom[p]);
							for (int k = 0; k < 3; k++) { a0[k] += w * (in->bellFrom[p * 3 + k] - a0[k]); a1[k] += w * (in->bellTo[p * 3 + k] - a1[k]); }
						}
						mcSub(a1, a0, dA);
						mcMA(dA, -MC_DT, in->originVel, dA);
						sv += kp * mcLen(dA);
						sw += kp;
					}
					if (sw > 0.0f) {
						const float wp = Clamp01(prm->springPose) * Clamp01((sv / (sw * MC_DT) - MC_SPRING_POSE0) * (1.0f / (MC_SPRING_POSE1 - MC_SPRING_POSE0)));
						if (wp > want) want = wp;
						if (wp > in->springRun) in->springRun = wp;
					}
				}
				const float tau = want > in->springRun ? 0.15f : MC_SPRING_BACK;
				in->springRun += (want - in->springRun) * (1.0f - expf(-MC_DT / tau));
			}
			// in wind 'damp' gives way (to MC_WIND_DAMP of itself from 500 units/s of air on): it calms the cape's swing
			// after a stop or a turn, but in wind it also killed the flapping - the cape stood blown out to one side
			// and stayed there.
			// No wind: x 1, nothing changes.
			// SJE r_meshClothDampLag: the frame 'damp' works in follows the character's speed with a lag - the air he drags
			// along does not stop or turn round in the instant he does. With his speed of the moment (smoothed over 50 ms) a
			// stop braked the cape at over 1000 units/s^2: it came in at 110 units/s and stood 0.1 s later, with no swing
			// past its place. A steady run: the
			// frame is his speed, as before.
			{
				float lag = prm->dampLag;
				// SJE r_meshClothWhirl: while he whirls that frame goes round with him - its lagging speed is turned as he turns,
				// and it follows his speed of the moment (the eased one turned on by what its 50 ms cost a vector that goes round)
				vec3_t dampTo;
				mcCopy(in->originVel, dampTo);
				if (in->whirlW > 0.0f) {
					const float om = in->whirlRate * (3.14159265f / 180.0f), x1 = om * 0.05f, ph1 = atanf(x1), ang = in->whirlW * om * MC_DT;
					float g1 = sqrtf(1.0f + x1 * x1);
					if (g1 > 1.5f) g1 = 1.5f;
					vec3_t vh, vcr;
					const float vu = mcDot(in->originVel, in->up);
					mcMA(in->originVel, -vu, in->up, vh);
					mcCross(in->up, vh, vcr);
					for (int k = 0; k < 3; k++) dampTo[k] += in->whirlW * (g1 * (vh[k] * cosf(ph1) + vcr[k] * sinf(ph1)) + vu * in->up[k] - in->originVel[k]);
					const float du = mcDot(in->dampVel, in->up);
					mcMA(in->dampVel, -du, in->up, vh);
					mcCross(in->up, vh, vcr);
					for (int k = 0; k < 3; k++) in->dampVel[k] = vh[k] * cosf(ang) + vcr[k] * sinf(ang) + du * in->up[k];
				}
				if (!(lag > 0.0f) || !in->dampVelOk) mcCopy(dampTo, in->dampVel);
				else {
					vec3_t dv;
					mcSub(dampTo, in->dampVel, dv);
					mcMA(in->dampVel, 1.0f - expf(-MC_DT / lag), dv, in->dampVel);
				}
				in->dampVelOk = true;
			}
			// SJE r_meshClothTurnCalm (see MC_TURN_W0)
			float turnD = 1.0f;
			{
				float want = prm->turnCalm > 0.0f ? Clamp01(prm->turnCalm) * Clamp01((in->turnRate - MC_TURN_W0) / (MC_TURN_W1 - MC_TURN_W0)) : 0.0f;
				if (anyFc && (fcRules & 1)) want = 0.0f;	// SJE r_meshClothFree
				want = want * want * (3.0f - 2.0f * want);
				in->turnCalm += (want - in->turnCalm) * (1.0f - expf(-MC_DT / (want > in->turnCalm ? 0.05f : MC_TURN_BACK)));
				turnD = 1.0f + in->turnCalm * (MC_TURN_DAMP - 1.0f);
				solve.ringGive = 1.0f + in->turnCalm * MC_TURN_RING;
			}
			float dampW = 1.0f;
			{
				vec3_t air;
				mcAdd(prm->wind, prm->zoneWind, air);
				const float wk = Clamp01((mcLen(air) - 100.0f) * (1.0f / 400.0f));
				float lo = MC_WIND_DAMP;
				dampW = 1.0f - wk * (1.0f - lo);
				// SJE r_meshClothAirFree: ... and to any moving air (a run, a fall), by how far it has freed the cloth
				const float fd = 1.0f - MC_FREE_DAMP * airFree;
				if (fd < dampW) dampW = fd;
			}
			if (prm->airPhys > 0.0f) {	// SJE r_meshClothAirPhys: 'damp' against a frame falls away (the air brakes the cloth) ...
				// ... but not while the turn-calm rule is on: that rule IS 'damp' (x 3 sideways) in turns no body can make, and
				// without it quick sweeps of the mouse whipped the cape again (wide sweeps: hem swing 122 -> 181 degrees, flung 27
				// units over the origin) and the circling rule had no frame to turn.
				float pd = anyFc ? fcDamp : MC_AIRPHYS_DAMP;	// SJE r_meshClothFree
				dampW *= pd > in->turnCalm ? pd : in->turnCalm;
			}
			kDampH.resize(g->numParticles); kDampV.resize(g->numParticles); kSpring.resize(g->numParticles);
			for (int p = 0; p < g->numParticles; p++) {
				{ float dh = g->dampH[p] * dampW * turnD; if (dh > 0.95f) dh = 0.95f; kDampH[p] = g->dampH[p] > 0.0f ? 1.0f - powf(1.0f - dh, isub) : 0.0f; }	// SJE r_meshClothTurnCalm: x turnD
				kDampV[p] = g->dampV[p] > 0.0f ? 1.0f - powf(1.0f - g->dampV[p] * dampW, isub) : 0.0f;
				const float ks = g->springK[p] * (1.0f - in->springRun) * (1.0f - in->turnCalm) * (anyFc && fcP[p] ? fcSpring : 1.0f);	// SJE r_meshClothTurnCalm; r_meshClothFree: x MC_CAPE_SPRING
				kSpring[p] = ks > 0.0f ? 1.0f - powf(1.0f - ks, isub) : 0.0f;
			}
			in->anchor.resize(tgtTo.size());
		}
		// SJE: per-step constants. These powf()s ran per particle (tether, spread, shape) or per bend link in
		// every substep; they only depend on the substep count and the static settings.
		solve.tetherK = 1.0f - powf(0.85f, bendScale * 6.0f);
		// SJE r_meshClothFine: in cloth finer than this, loose sheets that collide with themselves (a cape, a cloak)
		// bend by their curvature. A bend link's answer to a fold grows with the square of the fold's angle per edge,
		// and a mesh twice as fine has half the angle per edge for the same fold: the finer the mesh, the less its
		// bend links hold, and a fine cape ripples and does not come to rest where a coarse one of the same shape
		// lies still. The curvature stencils answer in proportion to the angle, whatever the spacing. Coarser cloth
		// keeps the bend links it was tuned with, and so does cloth worn round the body (a skirt, a robe: the pose
		// crushes it against the legs in every stride, and its bend links let that pass) and cloth without
		// self-collision (ComputeBendStencils).
		const bool fineCloth = prm->bendModel == 0 && prm->fine > 0.0f && g->spacing > 0.0f && g->spacing < prm->fine && g->anyLoose;
		// SJE r_meshClothFine: fine cloth's self-collision (SolveSelfPairs)
		const bool fineSelf = prm->fine > 0.0f && g->spacing > 0.0f && g->spacing < prm->fine && g->anySelf;
		solve.bendModel = g->bendAlpha.empty() ? 0 : (prm->bendModel == 1 ? 1 : (fineCloth ? 2 : 0));
		solve.bendTrip = MC_FIGHT_TRIP;
		// (bend model 1 works its target out in every substep, as it always did)
		const bool bendEnds = solve.bendModel == 2 && tgtFrom.size() == in->p.size() && tgtTo.size() == in->p.size();
		if (bendEnds) {
			// the start of this step is normally the end of the last: then its curvature is known already
			if (in->bendH1.size() == g->bendAlpha.size() && in->bendTgt1.size() == tgtFrom.size()
				&& memcmp(in->bendTgt1.data(), tgtFrom.data(), tgtFrom.size() * sizeof(float)) == 0) in->bendH0.swap(in->bendH1);
			else BendTargets(g, tgtFrom, in->bendH0, true);
			BendTargets(g, tgtTo, in->bendH1, true);
			in->bendTgt1.assign(tgtTo.begin(), tgtTo.end());
			in->bendH.resize(in->bendH0.size());
		}
		// SJE r_meshClothAirFree: in moving air 'tether' lets go and the bend links give - where the air carries the cloth
		// away from its wearer, not where it presses it onto him. Per point: the air's direction over the ground against
		// the point's place round the body's upright axis (air that comes from below or above presses nothing).
		solve.linkAlpha = nullptr;
		bool fpOk = false;	// in->freeP holds this step's values
		if (!(airFree > 0.0f)) in->freeP.clear();
		if (airFree > 0.0f) {
			float ft = MC_FREE_TETHER, fb = MC_FREE_BEND;
			std::vector<float>& fp = in->freeP;
			fp.resize(g->numParticles);
			in->expoP.resize(g->numParticles);
			const float wallSc = in->linkScale > 1e-3f ? in->linkScale : 1.0f;
			vec3_t air, ah;
			mcAdd(prm->wind, prm->zoneWind, air);
			mcSub(air, in->originVel, air);	// where the air goes, as the character meets it
			mcMA(air, -mcDot(air, in->up), in->up, ah);
			const float al = mcLen(air), hl = mcLen(ah), hs = al > 1.0f ? hl / al : 0.0f;
			for (int p = 0; p < g->numParticles; p++) {
				float lee = 1.0f;
				if (hs > 0.05f && g->freedom[p] > 0.0f) {
					vec3_t rv;
					mcSub(&in->p[p * 3], in->lastOrigin, rv);
					mcMA(rv, -mcDot(rv, in->up), in->up, rv);
					if (mcNorm(rv) > 1e-3f) {
						float w = Clamp01((mcDot(ah, rv) / hl + MC_FREE_LEE0) * (1.0f / (MC_FREE_LEE0 + MC_FREE_LEE1)));
						w = w * w * (3.0f - 2.0f * w);
						lee = 1.0f - hs * (1.0f - w);
					}
				}
				fp[p] = airFree * lee;
				// ... and how far the air's eddies reach the point: all of them in the body's lee, on its windward side fewer
				// the nearer the cloth lies to the body (none on it)
				float ex = 1.0f;
				if (lee < 1.0f && g->freedom[p] > 0.0f && ncaps > 0) {
					float dmin = 1.0e9f;
					for (int c = 0; c < ncaps; c++) {
						if (caps[c].hard == 4) continue;	// (not a thing in the hand)
						vec3_t q, dq;
						ClosestOnAxis(caps[c], &in->p[p * 3], q);
						mcSub(&in->p[p * 3], q, dq);
						const float dc = mcLen(dq) - caps[c].r;
						if (dc < dmin) dmin = dc;
					}
					const float d = dmin - MC_TURB_WALL0 * wallSc;
					ex = d > 0.0f ? 1.0f - (1.0f - lee) * expf(-d / (MC_TURB_WALL * wallSc)) : lee;
				}
				in->expoP[p] = ex;
			}
			fpOk = true;
			// SJE r_meshClothTailSide: with the back to a strong wind the cape goes off to one side (see MC_TAIL_PUSH)
			in->tailA.clear();
			in->tailRef.clear();
			in->tailRefK.clear();
			in->tailHi.clear();
			in->tailCapOn.clear();
			in->tailRefMode = 0;
			if (prm->tailSide > 0.0f && prm->dragQuad > 0.0f) {
				float tPush = MC_TAIL_PUSH, tTry = MC_TAIL_TRY, tTry2 = MC_TAIL_TRY2, tLift = 0.0f;
				if (anyFc) tPush = MC_CAPE_TAILPUSH;	// SJE r_meshClothFree
				int tBlade = 7;	// a lit blade's side: 1 = not tried (see MC_TAIL_BLADE_LEN), 2 = given up at once (MC_TAIL_BLADE_GIVE), 4 = ... and not remembered as the lee side
				vec3_t wd = { 0, 0, 0 }, fw, sd = { 0, 0, 0 };
				float kk = 0.0f;
				const float sc = in->linkScale > 1e-3f ? in->linkScale : 1.0f;
				const bool wornS = (int)g->wrap.size() == g->numParticles && (int)g->under.size() == g->numParticles;
				mcMA(in->fwd, -mcDot(in->fwd, in->up), in->up, fw);
				{
					// where the attached edge faces (see MC_TAIL_FWD_LINE); of its two directions the one the legs face
					bool edgeFwd = in->sideLine > MC_TAIL_FWD_LINE;
					if (edgeFwd) {
						vec3_t fa;
						mcCross(in->side, in->up, fa);
						if (mcNorm(fa) > 1e-3f) {
							if (mcDot(fa, fw) < 0.0f) mcScale(fa, -1.0f, fa);
							mcCopy(fa, fw);
						}
					}
				}
				if (hl > MC_TAIL_U0 && mcNorm(fw) > 1e-3f) {
					mcScale(ah, 1.0f / hl, wd);	// where the air goes, over the ground
					mcCross(in->up, fw, sd);
					mcNorm(sd);
					const float cf = mcDot(wd, fw), cs = mcDot(wd, sd);
					// the lee side: where the air goes while it comes from the side; kept while it comes straight from behind
					if (fabsf(cs) > MC_TAIL_SIDE) {
						const float r = MC_TAIL_SIDE_RATE * MC_DT;
						in->tailSide += ((cs > 0.0f ? 1.0f : -1.0f) - in->tailSide) * (r < 1.0f ? r : 1.0f);
					}
					if (in->tailSide == 0.0f) in->tailSide = (in->turbSeed & 1u) ? 0.01f : -0.01f;	// none yet: this cloth's own
					float kT = Clamp01((cf - MC_TAIL_C0) * (1.0f / (MC_TAIL_C1 - MC_TAIL_C0)));
					kT = kT * kT * (3.0f - 2.0f * kT);
					kk = Clamp01(prm->tailSide) * kT * Clamp01((hl - MC_TAIL_U0) * (1.0f / (MC_TAIL_U1 - MC_TAIL_U0)));
				}
				if (!(kk > 0.0f)) {
					// no wind from behind: after a moment all starts afresh
					in->tailGone += MC_DT;
					if (in->tailGone > MC_TAIL_GONE) { in->tailUse = 0.0f; in->tailTry = 0; in->tailWas = false; in->tailRamp = in->tailD = in->tailT = 0.0f; in->tailBladeT = 0.0f; in->tailBladeGave = false; }
				}
				else {
					in->tailGone = 0.0f;
					const float want = in->tailSide >= 0.0f ? 1.0f : -1.0f;
					// a new start: no side yet, or the air has come round to the other side (a side wind again)
					if (in->tailUse == 0.0f || (in->tailFirst != want && fabsf(mcDot(wd, sd)) > MC_TAIL_SIDE)) {
						in->tailUse = in->tailFirst = want;
						in->tailTry = 0;
						in->tailWas = false;
						in->tailRamp = 1.0f;	// (the first side at once: the cape comes streaming round to it with the turn)
						in->tailD = in->tailT = 0.0f;
						in->tailBest[0] = in->tailBest[1] = 0.0f;
						in->tailBladeT = 0.0f;
						in->tailBladeGave = false;
					}
					// SJE: a lit blade on the side in use, the air straight from behind: the other side at once, for good (see
					// MC_TAIL_BLADE_GIVE)
					if ((tBlade & 2) && prm->tailSide < 2.0f && !in->tailBladeGave) {
						float bl = 0.0f, bs = 0.0f;
						for (int c = 0; c < ncaps; c++) {
							if (caps[c].hard != 4) continue;
							vec3_t ab, md;
							mcSub(caps[c].b, caps[c].a, ab);
							const float l = mcLen(ab);
							for (int k = 0; k < 3; k++) md[k] = 0.5f * (caps[c].a[k] + caps[c].b[k]) - in->lastOrigin[k];
							bl += l;
							bs += l * mcDot(md, sd);
						}
						if (bl > MC_TAIL_BLADE_LEN * sc && bs * in->tailUse > MC_TAIL_BLADE_LAT * sc * bl && fabsf(mcDot(wd, sd)) <= MC_TAIL_SIDE)
							in->tailBladeT += MC_DT;
						else in->tailBladeT = 0.0f;
						if (in->tailBladeT > MC_TAIL_BLADE_GIVE) {
							in->tailUse = -in->tailUse;
							in->tailTry = 2;
							in->tailWas = false;
							in->tailRamp = in->tailD = in->tailT = 0.0f;
							in->tailBladeT = 0.0f;
							in->tailBladeGave = true;
							if (tBlade & 4) { in->tailSide = in->tailUse * fabsf(in->tailSide); in->tailFirst = in->tailUse; }	// (the lee side to start on next time)
						}
					}
					// how far the cloth's lower third is out: its centre against its animated place, to that side and forward
					// (per sheet, the furthest one counts: a skirt round the hips in the same group stays where it is and
					// halved the mean - the cape was out by 19 - 25 units, the measure said 12 - 16 and gave the side up)
					float dOut = -1.0f;	// (-1: cannot tell - then the first side stays)
					if ((int)g->rootS.size() == g->numParticles && (int)g->sheetOf.size() == g->numParticles && g->numSheets > 0 && tgtTo.size() == in->p.size()) {
						thread_local static std::vector<float> acc;
						acc.assign((size_t)g->numSheets * 3, 0.0f);
						for (int p = 0; p < g->numParticles; p++) {
							if (g->freedom[p] <= 0.0f || g->rootS[p] < 0.66f || (wornS && (g->wrap[p] || g->under[p]))) continue;
							const int sh = g->sheetOf[p];
							if (sh < 0 || sh >= g->numSheets) continue;
							vec3_t dp;
							mcSub(&in->p[p * 3], &tgtTo[(size_t)p * 3], dp);
							acc[(size_t)sh * 3] += mcDot(dp, sd) * in->tailUse;
							acc[(size_t)sh * 3 + 1] += mcDot(dp, fw);
							acc[(size_t)sh * 3 + 2] += 1.0f;
						}
						for (int sh = 0; sh < g->numSheets; sh++) {
							const float cnt = acc[(size_t)sh * 3 + 2];
							if (cnt < 3.0f) continue;
							float oS = acc[(size_t)sh * 3] / cnt, oF = acc[(size_t)sh * 3 + 1] / cnt;
							if (oS < 0.0f) oS = 0.0f;
							if (oF < 0.0f) oF = 0.0f;
							const float dd = sqrtf(oS * oS + oF * oF);
							if (dd > dOut) dOut = dd;
						}
					}
					if (dOut >= 0.0f) {
						in->tailD += (dOut - in->tailD) * (1.0f - expf(-MC_DT / MC_TAIL_OUT_T));
						if (in->tailD > MC_TAIL_OUT * sc) in->tailWas = true;
						if (in->tailD > (in->tailWas ? MC_TAIL_OUT_LOW : MC_TAIL_OUT) * sc) in->tailT = 0.0f;
						else in->tailT += MC_DT;
						if (in->tailTry < 2 && in->tailT > (in->tailTry == 0 ? tTry : tTry2)) {
							// not out on this side: the other one (the cloth has to cross the back first: more time) - or,
							// after both, the side on which it was further out when it gave up, for good. (Not the furthest it
							// ever was: on the first side that is the stream it came round with, and it sent the cape back to
							// the side it could not hold.)
							in->tailBest[in->tailTry] = in->tailD;
							// ... but not over to the side a lit blade is held on (see MC_TAIL_BLADE_LEN): it stays, and that is settled
							float bladeSide = 0.0f;
							if (tBlade & 1) {
								float bl = 0.0f, bs = 0.0f;
								for (int c = 0; c < ncaps; c++) {
									if (caps[c].hard != 4) continue;
									vec3_t ab, md;
									mcSub(caps[c].b, caps[c].a, ab);
									const float l = mcLen(ab);
									for (int k = 0; k < 3; k++) md[k] = 0.5f * (caps[c].a[k] + caps[c].b[k]) - in->lastOrigin[k];
									bl += l;
									bs += l * mcDot(md, sd);
								}
								if (bl > MC_TAIL_BLADE_LEN * sc && fabsf(bs) > MC_TAIL_BLADE_LAT * sc * bl) bladeSide = bs > 0.0f ? 1.0f : -1.0f;
							}
							if (bladeSide != 0.0f && -in->tailUse == bladeSide) in->tailTry = 1;	// (stays: 'settled' with the count below)
							else if (in->tailTry == 0 || in->tailBest[0] > in->tailBest[1]) {
								in->tailUse = -in->tailUse;
								in->tailRamp = in->tailD = 0.0f;
								in->tailWas = false;
							}
							in->tailTry++;
							in->tailT = 0.0f;
						}
					}
					in->tailRamp += MC_TAIL_RAMP * MC_DT;
					if (in->tailRamp > 1.0f) in->tailRamp = 1.0f;
					kk *= in->tailRamp * (1.0f - Clamp01(tLift) * Clamp01(in->liftAir));
					const float sg = in->tailUse;
					// a lit blade in hand: the stronger push (see MC_TAIL_PUSH_BLADE)
					{
						float bl = 0.0f;
						if (prm->tailSide < 2.0f)
							for (int c = 0; c < ncaps; c++) {
								if (caps[c].hard != 4) continue;
								vec3_t ab;
								mcSub(caps[c].b, caps[c].a, ab);
								bl += mcLen(ab);
							}
						const float want = bl > MC_TAIL_BLADE_LEN * sc ? 1.0f : 0.0f;
						if (want != in->tailBlade) {
							in->tailBlade += (want - in->tailBlade) * (1.0f - expf(-MC_DT / MC_TAIL_PUSH_T));
							if (fabsf(want - in->tailBlade) < 0.01f) in->tailBlade = want;
						}
						if (in->tailBlade > 0.0f) tPush += (MC_TAIL_PUSH_BLADE - MC_TAIL_PUSH) * in->tailBlade;
					}
					const float uc = hl > MC_AIR_MAX ? MC_AIR_MAX : hl, q = tPush * prm->dragQuad * uc * uc;
					in->tailA.assign((size_t)g->numParticles * 3, 0.0f);
					// ... and a cape's own references for the body tests, against the shapes that move (see MC_TAIL_REF_PHI)
					int tRef = 5;
					const bool refOn = tRef != 0 && kk > 0.0f && tgtFrom.size() == in->p.size() && (int)g->rootS.size() == g->numParticles
						&& g->plateBuilt && (int)g->high.size() == g->numParticles && capsFrom;
					if (refOn) {
						in->tailCapOn.assign((size_t)ncaps, 0);
						for (int c = 0; c < ncaps; c++) {
							vec3_t da, db;
							mcSub(caps[c].a, capsFrom[c].a, da);
							mcSub(caps[c].b, capsFrom[c].b, db);
							const float lim2 = MC_TAIL_REF_MOVE * sc * MC_TAIL_REF_MOVE * sc;
							if (mcDot(da, da) > lim2 || mcDot(db, db) > lim2) in->tailCapOn[c] = 1;
						}
						in->tailRef.assign((size_t)g->numParticles * 3, 0.0f);
						in->tailRefK.assign((size_t)g->numParticles, 0.0f);
						in->tailHi.assign((size_t)g->numParticles, 0);
						in->tailRefMode = tRef;
					}
					for (int p = 0; kk > 0.0f && p < g->numParticles; p++) {
						if (g->freedom[p] <= 0.0f || (wornS && (g->wrap[p] || g->under[p]))) continue;
						vec3_t rv;
						mcSub(&in->p[p * 3], in->lastOrigin, rv);
						const float hz = mcDot(rv, in->up), lat = mcDot(rv, sd) * sg;
						float kS = Clamp01((lat - MC_TAIL_L0 * sc) * (1.0f / ((MC_TAIL_L1 - MC_TAIL_L0) * sc)));
						kS = kS * kS * (3.0f - 2.0f * kS);
						// (beside the body's height only: feet 24 under the origin, head 40 over it)
						const float lo = Clamp01((hz + 32.0f * sc) * (1.0f / (8.0f * sc))), hi = Clamp01((48.0f * sc - hz) * (1.0f / (8.0f * sc)));
						const float k = kk * kS * (lo < hi ? lo : hi);
						if (k <= 0.0f) continue;
						if (fp[p] < airFree * k) fp[p] = airFree * k;	// let go, as the cloth in the body's lee is
						// outwards along the back while still behind the body and its arm, on with the air beyond
						// (beside the body further out first: MC_TAIL_BESIDE)
						float wB = Clamp01((mcDot(rv, fw) - MC_TAIL_B0 * sc) * (1.0f / ((MC_TAIL_B1 - MC_TAIL_B0) * sc)));
						wB = wB * wB * (3.0f - 2.0f * wB);
						float f = Clamp01((lat - (MC_TAIL_A0 + MC_TAIL_BESIDE * wB) * sc) * (1.0f / ((MC_TAIL_A1 - MC_TAIL_A0) * sc)));
						f = f * f * (3.0f - 2.0f * f);
						vec3_t dir;
						mcScale(sd, sg * (1.0f - f), dir);
						mcMA(dir, f, wd, dir);
						// (the push is not divided by the wetness: a map's storm is raised for soaked cloth - MC_StrongWindSpeed -
						// but this push's air speed is capped at MC_AIR_MAX, so in the rain it was half of what it is on dry cloth: 0.6,
						// the strength at which the cape flaps between back and side. t1_rail rains: run backwards out 33 - 52 %, the hem
						// 7 - 12 units beside him, the side in use changed once or twice a run -> out 69 - 91 %, no change of side.)
						mcScale(dir, k * q / g->weight[p], &in->tailA[(size_t)p * 3]);
						if (refOn && g->high[p]) {
							in->tailHi[p] = 1;
							// how far the reference place holds: not near the attached edge, not while the cloth is near its animated place
							const float s01 = g->rootS[p];
							float kR = Clamp01((s01 - MC_TAIL_REF_S0) / (MC_TAIL_REF_S1 - MC_TAIL_REF_S0));
							kR = k * kR * kR * (3.0f - 2.0f * kR);
							if (kR > 0.0f) {
								vec3_t dd;
								mcSub(&in->p[p * 3], &tgtFrom[(size_t)p * 3], dd);
								float kD = Clamp01((mcLen(dd) - MC_TAIL_REF_D0 * sc) / ((MC_TAIL_REF_D1 - MC_TAIL_REF_D0) * sc));
								kR *= kD * kD * (3.0f - 2.0f * kD);
							}
							if (kR > 0.0f) {
								// the animated place in his frame: its height, its distance from the upright axis, its angle from
								// straight behind him towards the side in use - turned on and moved out by how far the reference holds
								vec3_t a3, hh;
								mcSub(&tgtFrom[(size_t)p * 3], in->lastOrigin, a3);
								const float az = mcDot(a3, in->up);
								mcMA(a3, -az, in->up, hh);
								const float r0 = mcLen(hh);
								const float al = atan2f(mcDot(hh, sd) * sg, -mcDot(hh, fw));
								const float amax = MC_TAIL_REF_AMAX * (3.14159265f / 180.0f);
								float al2 = al + kR * MC_TAIL_REF_PHI * s01 * (3.14159265f / 180.0f);
								if (al2 > amax) al2 = al > amax ? al : amax;
								const float rw = (MC_TAIL_REF_R0 + (MC_TAIL_REF_R1 - MC_TAIL_REF_R0) * s01) * sc;
								const float R = r0 + kR * (rw > r0 ? rw - r0 : 0.0f);
								float* Wp = &in->tailRef[(size_t)p * 3];
								for (int c = 0; c < 3; c++) Wp[c] = in->lastOrigin[c] + az * in->up[c] + R * (-cosf(al2) * fw[c] + sinf(al2) * sg * sd[c]);
								in->tailRefK[p] = kR;
							}
						}
					}
				}
			}
			solve.freeP = fp.data();
			solve.freeTether = ft;
			const bool worn = (int)g->wrap.size() == g->numParticles && (int)g->under.size() == g->numParticles;
			if (worn) { solve.wornWrap = g->wrap.data(); solve.wornUnder = g->under.data(); }
			if (solve.xpbd && g->linkAlpha.size() == g->linkA.size()) {
				std::vector<float>& la = in->linkAlphaAir;
				la.resize(g->linkA.size());
				for (size_t l = 0; l < g->linkA.size(); l++) {
					la[l] = g->linkAlpha[l];
					if (!g->linkBend[l] || la[l] < 0.0f) continue;
					const int a = g->linkA[l], b = g->linkB[l];
					if (worn && (g->under[a] || g->under[b] || g->wrap[a] || g->wrap[b])) continue;
					const float f = fp[a] < fp[b] ? fp[a] : fp[b];
					if (f > 0.0f) la[l] = BendCompliance(0.5f * (g->bend[a] + g->bend[b]) * (1.0f - Clamp01(fb) * f));
				}
				solve.linkAlpha = la.data();
			}
		}
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
			// SJE r_meshClothNoRoom: worn cloth that has no room to hang takes the shape its wearer's pose gives it.
			// A robe, a skirt or the strips under it are about as long as their wearer's legs. When he crouches, kneels,
			// sits or lies, their attached edge comes down to half the cloth's length above the floor and the rest has
			// nowhere to hang: it was laid on the ground as it fell, crumpled and twisted over itself (strips three
			// points wide fold like paper and read as torn), spread out round the legs, and went on working against
			// floor and legs for seconds. The pose says where such cloth belongs then: it is skinned to the legs, the
			// modeller draped it over the knees. So it is drawn towards its animated shape, the more the less room it
			// has: not at all while its attached edge is higher over the floor than MC_NOROOM_FROM of the room it needs
			// (standing, walking, running, in the air: as before, bit for bit), in full from MC_NOROOM_FROM -
			// MC_NOROOM_SPAN down. The room it needs is the sheet's length, or the height its attached edge stands at
			// in the bind pose where that is less (a robe with a train lies on the ground while its wearer stands and
			// is not short of room for that; a short skirt has room in a crouch too). Body tests, floor and layers act
			// as ever. Not capes and cloaks: they are skinned to the back, their pose in a crouch is a board through
			// the floor; they pile up behind their wearer as before.
			if (prm->noRoom > 0.0f && prm->floor && g->numSheets > 0 && (int)g->sheetOf.size() == g->numParticles
				&& (int)g->sheetLen.size() == g->numSheets && (int)g->wrap.size() == g->numParticles
				&& (int)g->under.size() == g->numParticles && tgtTo.size() == (size_t)g->numParticles * 3
				&& g->bindPos.size() >= (size_t)g->numParticles * 3) {
				thread_local static std::vector<float> edgeH, edgeB, crowd;
				thread_local static std::vector<int> edgeN;
				edgeH.assign((size_t)g->numSheets, 0.0f);
				edgeB.assign((size_t)g->numSheets, 0.0f);
				edgeN.assign((size_t)g->numSheets, 0);
				for (int p = 0; p < g->numParticles; p++) {
					if (g->freedom[p] > 0.0f) continue;
					vec3_t fr;
					mcSub(&tgtTo[p * 3], prm->floorPt, fr);
					edgeH[g->sheetOf[p]] += mcDot(fr, prm->floorN);
					edgeB[g->sheetOf[p]] += g->bindPos[(size_t)p * 3 + 2];	// (bind-pose model units, feet at 0)
					edgeN[g->sheetOf[p]]++;
				}
				crowd.assign((size_t)g->numSheets, 0.0f);
				bool any = false;
				const float ls = in->linkScale > 1e-3f ? in->linkScale : 1.0f;
				for (int sh = 0; sh < g->numSheets; sh++) {
					if (!edgeN[sh]) continue;
					float need = g->sheetLen[sh];
					const float stood = edgeB[sh] / edgeN[sh];
					if (stood > 1.0f && stood < need) need = stood;
					need *= ls;
					if (need < 4.0f) continue;
					crowd[sh] = Clamp01((MC_NOROOM_FROM * need - edgeH[sh] / edgeN[sh]) / (MC_NOROOM_SPAN * need));
					if (crowd[sh] > 0.0f) any = true;
				}
				if (any) for (int p = 0; p < g->numParticles; p++) {
					const float c = crowd[g->sheetOf[p]];
					if (c <= 0.0f || g->freedom[p] <= 0.0f || !(g->wrap[p] || g->under[p])) continue;
					const float pull = Clamp01(g->shape[p] * prm->shapeScale + prm->noRoom * MC_NOROOM_SHAPE * c);
					ksh[p] = pull > 0.0f ? 1.0f - powf(1.0f - pull, isub) : 0.0f;
				}
			}
		}
		std::vector<float>& n = in->scratchN;
		// wind with slow gusts: two incommensurate sines, so it never repeats visibly
		vec3_t windNow;
		vec3_t windFc = { 0.0f, 0.0f, 0.0f };	// SJE r_meshClothFree: this step's wind for free cloth (from behind: MC_CAPE_TAIL of it)
		bool fcWind = false;
		float fcK = 1.0f;	// ... and the scale of the air field for it (see MC_CAPE_UMAX)
		{
			const float t = prm->timeSec;
			const float gs = 1.0f + prm->gust * (0.6f * sinf(t * 1.3f) + 0.4f * sinf(t * 3.7f + 1.1f));
			mcScale(prm->wind, gs, windNow);
			// SJE r_meshClothFans: the air of pushers / jump pads, not gusted (it outruns the push by a set amount)
			if (mcDot(prm->zoneWind, prm->zoneWind) > 0.0f) mcAdd(windNow, prm->zoneWind, windNow);
			// wind from behind pushes the cloth only by MC_WIND_TAIL of its forward part: in full it pressed a cape onto
			// its wearer's back and legs, where body and wind fought every step - the cape shook and its edges
			// stretched to more than twice their length
			float tail = MC_WIND_TAIL;
			if (tail < 1.0f && mcDot(windNow, windNow) > 1e-4f) {
				// the character's facing over the ground (its entity axis: the cape's attached edge gives the shoulder
				// line only roughly - on a collar it can lie front to back)
				vec3_t fwd;
				mcMA(in->fwd, -mcDot(in->fwd, in->up), in->up, fwd);
				const float fl = mcLen(fwd);
				const float wf = fl > 1e-3f ? mcDot(windNow, fwd) / fl : 0.0f;
				// SJE r_meshClothFree: free cloth takes MC_CAPE_TAIL of it (in full: the tail-side rule takes a free cape off to
				// one side); the rest of the group - a hip skirt under the cape - keeps its share
				if (anyFc) { mcCopy(windNow, windFc); fcWind = true; if (wf > 0.0f) mcMA(windFc, -(1.0f - fcTail) * wf / fl, fwd, windFc); }
				if (wf > 0.0f) mcMA(windNow, -(1.0f - tail) * wf / fl, fwd, windNow);
			}
			if (anyFc && !fcWind) mcCopy(windNow, windFc);
			if (anyFc && fcPeak > 0.0f) {
				// the steady air against the wearer: this step's wind without its gust, less his own travel
				vec3_t st;
				mcScale(windFc, gs > 0.05f ? 1.0f / gs : 1.0f, st);
				if (gs > 0.05f && mcDot(prm->zoneWind, prm->zoneWind) > 0.0f) mcMA(st, 1.0f - 1.0f / gs, prm->zoneWind, st);	// (the pushers' air is not gusted)
				mcSub(st, in->originVel, st);
				const float ul = mcLen(st);
				if (ul > fcUmax && fcUmax > 0.0f) fcK = fcUmax / ul;
			}
		}
		// SJE r_meshClothLiftAir: how far the lift limit opens in this step (SolveConstraints): air from below - the
		// character's fall against the air, or an updraft - from MC_LIFT_FALL0 to MC_LIFT_FALL1 units/s, or wind over the
		// ground from MC_LIFT_WIND0 to MC_LIFT_WIND1 (not the character's travel: a run keeps its look). It opens at once
		// and closes by MC_LIFT_CLOSE per second, so a landing does not snap a raised cape down. No fall, no wind: closed.
		{
			float open = 0.0f, kfLand = 0.0f;	// SJE r_meshClothAirFree: kfLand = the fall's part of it (for the landing)
			if (prm->liftAir > 0.0f) {
				vec3_t air, ah;
				mcSub(windNow, in->originVel, air);
				const float up = mcDot(air, in->up);
				mcMA(windNow, -mcDot(windNow, in->up), in->up, ah);
				// SJE r_meshClothAirFree: ... from a slower fall (a real cape trails above a body that falls faster than
				// the cloth alone would: a few metres a second)
				const float f0 = prm->airFree > 0.0f ? MC_LIFT_FALL0F : MC_LIFT_FALL0, f1 = prm->airFree > 0.0f ? MC_LIFT_FALL1F : MC_LIFT_FALL1;
				const float kf = Clamp01((up - f0) * (1.0f / (f1 - f0)));
				kfLand = kf;
				const float kw = Clamp01((mcLen(ah) - MC_LIFT_WIND0) * (1.0f / (MC_LIFT_WIND1 - MC_LIFT_WIND0)));
				open = Clamp01(prm->liftAir) * (kf > kw ? kf : kw);
			}
			const float shut = in->liftAir - (prm->airFree > 0.0f ? MC_LIFT_CLOSEF : MC_LIFT_CLOSE) * MC_DT;	// SJE r_meshClothAirFree: slower
			// SJE r_meshClothAirFree: how lately the wearer fell (the landing rule in the substeps)
			{
				const float fade = in->landAir - MC_FREE_LAND_FADE * MC_DT;
				in->landAir = prm->airFree > 0.0f ? (kfLand > fade ? kfLand : (fade > 0.0f ? fade : 0.0f)) : 0.0f;
			}
			in->liftAir = open > shut ? open : (shut > 0.0f ? shut : 0.0f);
			solve.liftAir = in->liftAir;
			solve.liftGrad = prm->airFree > 0.0f;	// SJE r_meshClothAirFree
			if (solve.liftAir > 0.0f) {
				mcMA(in->fwd, -mcDot(in->fwd, in->up), in->up, solve.liftFwd);
				const float fl = mcLen(solve.liftFwd);
				if (fl > 1e-3f) mcScale(solve.liftFwd, 1.0f / fl, solve.liftFwd); else solve.liftAir = 0.0f;
				if ((int)g->wrap.size() == g->numParticles && (int)g->under.size() == g->numParticles) { solve.liftWrap = g->wrap.data(); solve.liftUnder = g->under.data(); }
			}
		}
		if (solve.liftOpenAll && !(solve.liftAir > 0.0f)) {	// SJE r_meshClothAirPhys: (with liftAir > 0 it is set above)
			mcMA(in->fwd, -mcDot(in->fwd, in->up), in->up, solve.liftFwd);
			const float fl = mcLen(solve.liftFwd);
			if (fl > 1e-3f) mcScale(solve.liftFwd, 1.0f / fl, solve.liftFwd); else solve.liftOpenAll = false;
		}
		// flutter (r_meshClothFlutter): the wind's strength also varies across the cloth, in ripples that travel
		// along it (a phase from each particle's position and the time), so wind flaps a cape instead of
		// pushing it as one stiff sheet. Worked out once per step, only when there is wind.
		// SJE: strong wind flaps the cloth harder - the ripples grow to MC_WIND_FLUTTER from 500 to 1500 units/s of wind
		// (with the gentle flutter alone a blown cape stood out to one side and hardly moved). No wind: as before.
		float flutA = Clamp01(prm->flutter);
		{
			float boost = MC_WIND_FLUTTER;
			const float wb = boost * Clamp01((mcLen(windNow) - 500.0f) * (1.0f / 1000.0f));
			if (prm->flutter > 0.0f && wb > flutA) flutA = Clamp01(wb);
		}
		// SJE: in a storm (the strong weather wind, MC_STRONG_WIND and up) cloth lighter than MC_STORM_WEIGHT is blown as hard
		// as cloth of that weight, not harder: the storm's strength was set on the heavy cape (weight 1.5); a robe of
		// weight 1 took half as much again and tore into jagged strips in saber swings and jumps (t1_rail).
		// Weaker wind and no wind: as before.
		float stormW = MC_STORM_WEIGHT;
		const float stormK = Clamp01((mcLen(prm->wind) - MC_STRONG_WIND) * (1.0f / 600.0f));
		thread_local static std::vector<float> windF;
		const bool flutter = prm->flutter > 0.0f && mcDot(windNow, windNow) > 1e-4f;
		if (flutter) {
			windF.resize(g->numParticles);
			const float t = prm->timeSec;
			const float a = flutA;
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
		vec3_t airHW = { 0, 0, 0 };	// SJE r_meshClothAirFree: the same for worn cloth (under the old single cap)
		if (prm->dragQuad > 0.0f) {
			mcSub(in->originVel, windNow, airH);
			// falling: only part of it counts. All of it throws a cape up to its lift limit, where it piles
			// up behind the shoulders; with part of it the cape goes light and fills out as its wearer drops
			const float down = mcDot(airH, in->up);
			// SJE r_meshClothLiftAir: ... while the lift limit is open (a long fall) more of it counts, up to all of it
			const float fallK = MC_AIR_FALL + (1.0f - MC_AIR_FALL) * solve.liftAir;
			if (down < 0.0f) mcMA(airH, -down * (1.0f - fallK), in->up, airH);
			const float U = mcLen(airH);
			mcScale(airH, (U > MC_AIR_MAX ? MC_AIR_MAX / U : 1.0f) * h, airHW);
			if (prm->airFree > 0.0f) {
				// SJE r_meshClothAirFree: the cap holds for the fall and for the air over the ground apart - under one cap a
				// storm's 3200 units/s left a fall's 400 from below as 40, and a cape fell with its wearer as in still air -
				// and wind over the ground counts up to MC_AIR_MAX_WIND (from 500 to 1500 units/s of it)
				float capW = MC_AIR_MAX_WIND;
				const float vz = mcDot(airH, in->up);
				vec3_t ahh;
				mcMA(airH, -vz, in->up, ahh);
				const float uh = mcLen(ahh);
				// (not wind from behind: that presses the cloth onto its wearer)
				vec3_t fhc, whc;
				mcMA(in->fwd, -mcDot(in->fwd, in->up), in->up, fhc);
				mcMA(windNow, -mcDot(windNow, in->up), in->up, whc);
				const float wlc = mcLen(whc), notTail = mcNorm(fhc) > 1e-3f && wlc > 1.0f ? 1.0f - Clamp01(mcDot(whc, fhc) / wlc) : 1.0f;
				const float capH = MC_AIR_MAX + (capW - MC_AIR_MAX) * Clamp01(prm->airFree) * notTail * Clamp01((mcLen(windNow) - 500.0f) * (1.0f / 1000.0f));
				const float kh = uh > capH ? capH / uh : 1.0f, kz = fabsf(vz) > MC_AIR_MAX ? MC_AIR_MAX / fabsf(vz) : 1.0f;
				for (int c = 0; c < 3; c++) airH[c] = (ahh[c] * kh + in->up[c] * vz * kz) * h;
			}
			else mcScale(airH, (U > MC_AIR_MAX ? MC_AIR_MAX / U : 1.0f) * h, airH);
		}
		if (prm->dragQuad > 0.0f && prm->flutter > 0.0f && (int)g->rootDist.size() == g->numParticles) {
			vec3_t air;
			mcSub(in->originVel, windNow, air);
			const float U = mcLen(air);
			const float a = flutA * Clamp01((U - 40.0f) / 120.0f);	// none in still air
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
		// SJE r_meshClothTurbulence / r_meshClothBladeAir: this step's extra air at every point (world, units/s): the travel air's
		// wander and eddies (airT: through the drag as the travel air is) and the blades' air (airB)
		thread_local static std::vector<float> airT, airB;
		bool anyAirT = false, anyAirB = false;
		float airTQ = 1.0f;	// SJE r_meshClothAirFree: the part of airT that counts in the travel wind's square (its cap)
		{
			// the air against the character, as for the travel wind (falling: only part of it, unless the lift limit is open)
			vec3_t U;
			mcSub(in->originVel, windNow, U);
			const float down = mcDot(U, in->up);
			if (down < 0.0f) mcMA(U, -down * (1.0f - (MC_AIR_FALL + (1.0f - MC_AIR_FALL) * solve.liftAir)), in->up, U);
			float Ul = mcLen(U);
			const float Ufull = Ul;	// SJE r_meshClothAirFree: the eddies' size (their travel past the cloth stays capped)
			if (Ul > MC_AIR_MAX) { mcScale(U, MC_AIR_MAX / Ul, U); Ul = MC_AIR_MAX; }
			if (prm->airFree > 0.0f && Ufull > Ul) airTQ = Ul / Ufull;
			const float tk = prm->turb > 0.0f ? prm->turb * Clamp01((Ul - MC_TURB_U0) * (1.0f / (MC_TURB_U1 - MC_TURB_U0))) : 0.0f;
			float eddyK = MC_TURB_EDDY, yawK = MC_TURB_YAW, pitchK = MC_TURB_PITCH, surgeK = MC_TURB_SURGE, shearK = MC_TURB_SHEAR;
			if (tk > 0.0f) {
				if (!in->turbReady) TurbInit(in);
				vec3_t e1, e2, e3;
				mcScale(U, 1.0f / Ul, e1);
				e2[0] = in->up[1] * e1[2] - in->up[2] * e1[1]; e2[1] = in->up[2] * e1[0] - in->up[0] * e1[2]; e2[2] = in->up[0] * e1[1] - in->up[1] * e1[0];
				if (mcNorm(e2) < 0.3f) { mcCopy(in->side, e2); mcMA(e2, -mcDot(e2, e1), e1, e2); mcNorm(e2); }	// air from straight below: across = the shoulders
				e3[0] = e1[1] * e2[2] - e1[2] * e2[1]; e3[1] = e1[2] * e2[0] - e1[0] * e2[2]; e3[2] = e1[0] * e2[1] - e1[1] * e2[0];
				// the stream's wander, the same for the whole cloth
				float wc = MC_TURB_WANDER / Ul;
				wc = wc < MC_TURB_WCELL0 ? MC_TURB_WCELL0 : (wc > MC_TURB_WCELL1 ? MC_TURB_WCELL1 : wc);
				in->turbWander += MC_DT / wc;
				if (in->turbWander > 1.0e6) in->turbWander -= 1.0e6;
				const unsigned int sd = in->turbSeed;
				vec3_t wander;
				mcScale(e2, yawK * McNoise(sd ^ 0x51A3C1u, in->turbWander), wander);
				mcMA(wander, pitchK * McNoise(sd ^ 0x2B7E15u, in->turbWander), e3, wander);
				mcMA(wander, surgeK * McNoise(sd ^ 0x6A09E6u, in->turbWander), e1, wander);
				// ... and the stream is stronger on one side of the body than on the other, for a while (the eddies leave a body's
				// two sides in turn): the cloth trails skewed, now to one side, now to the other
				const float shear = shearK * McNoise(sd ^ 0x7F4A7Cu, in->turbWander * 0.7 + 31.0);
				// SJE r_meshClothAirFree: ... past the cloth at MC_TURB_PASS at most
				const float trav = prm->airFree > 0.0f && MC_TURB_TRAVEL * Ul > MC_TURB_PASS ? MC_TURB_PASS / Ul : MC_TURB_TRAVEL;
				const bool wornKnownT = (int)g->wrap.size() == g->numParticles && (int)g->under.size() == g->numParticles;
				// the eddies: carried past with the air (a point at r from the character meets at time t what was at r + travel),
				// each wave also turning on its own
				for (int m = 0; m < MC_TURB_MODES; m++) {
					in->turbPsi[m] += (double)((trav * mcDot(in->turbK[m], U) + in->turbW[m] * eddyK * Ul) * MC_DT);
					in->turbPsi[m] = fmod(in->turbPsi[m], 6.283185307179586);
				}
				const float amp = Ul * tk;
				airT.resize((size_t)g->numParticles * 3);
				for (int p = 0; p < g->numParticles; p++) {
					float* a = &airT[(size_t)p * 3];
					if (g->freedom[p] <= 0.0f) { a[0] = a[1] = a[2] = 0.0f; continue; }
					vec3_t r, u = { 0.0f, 0.0f, 0.0f };
					mcSub(&in->p[p * 3], in->lastOrigin, r);
					for (int m = 0; m < MC_TURB_MODES; m++) mcMA(u, in->turbA[m] * cosf(mcDot(in->turbK[m], r) + (float)in->turbPsi[m]), in->turbQ[m], u);
					float across = mcDot(r, e2) * (1.0f / MC_TURB_SHEARW);
					across = across < -1.0f ? -1.0f : (across > 1.0f ? 1.0f : across);
					// SJE r_meshClothAirFree: the eddies at the real air's size where the air carries loose cloth away (pressing it
					// onto its wearer, or on worn cloth: at the capped size, as before)
					// ... and by the point's exposure (MC_TURB_WALL): the size grows with it, and the whole random part is x it.
					// (Not by in->freeP: r_meshClothTailSide counts cloth as free that still lies on the body.)
					float ampP = amp;
					if (fpOk && !(wornKnownT && (g->wrap[p] || g->under[p]))) {
						const float ex = in->expoP[p];
						ampP = (Ufull > Ul ? (Ul + (Ufull - Ul) * (airFree * ex)) * tk : amp) * ex;
					}
					for (int c = 0; c < 3; c++) a[c] = ampP * (wander[c] + shear * across * e1[c] + eddyK * u[c]);
				}
				anyAirT = true;
			}
		}
		if (prm->bladeAir > 0.0f && prm->numBladeAir > 0) {
			const int nb = prm->numBladeAir > MC_MAX_BLADE_AIR ? MC_MAX_BLADE_AIR : prm->numBladeAir;
			for (int b = 0; b < nb; b++) {
				const float* A = prm->bladeA[b], * B = prm->bladeB[b], * VA = prm->bladeVA[b], * VB = prm->bladeVB[b];
				vec3_t ax, sw;
				mcSub(B, A, ax);
				const float len = mcNorm(ax);
				if (len < 1.0f) continue;
				// the swing: the tip against the hilt, across the blade
				mcSub(VB, VA, sw);
				mcMA(sw, -mcDot(sw, ax), ax, sw);
				const float swl = mcLen(sw);
				if (swl < MC_BLADE_AIR_SWING) continue;
				const float va = mcLen(VA), vb = mcLen(VB);
				const float reach = 0.5f * len + MC_BLADE_AIR_R + MC_DT * (va > vb ? va : vb);
				vec3_t mid;
				for (int c = 0; c < 3; c++) mid[c] = 0.5f * (A[c] + B[c]);
				for (int p = 0; p < g->numParticles; p++) {
					if (g->freedom[p] <= 0.0f) continue;
					const float* pp = &in->p[p * 3];
					vec3_t dm;
					mcSub(pp, mid, dm);
					if (mcDot(dm, dm) > reach * reach) continue;
					float best = 1e9f, bt = 0.0f;
					for (int q = 0; q < 3; q++) {
						const float back = 0.5f * MC_DT * q;
						vec3_t a0, b0, ab, ap, c, dd;
						mcMA(A, -back, VA, a0);
						mcMA(B, -back, VB, b0);
						mcSub(b0, a0, ab);
						mcSub(pp, a0, ap);
						const float l2 = mcDot(ab, ab);
						float s = l2 > 1e-4f ? mcDot(ap, ab) / l2 : 0.0f;
						s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
						mcMA(a0, s, ab, c);
						mcSub(pp, c, dd);
						const float d2 = mcDot(dd, dd);
						if (d2 < best) { best = d2; bt = s; }
					}
					if (best >= MC_BLADE_AIR_R * MC_BLADE_AIR_R) continue;
					if (!anyAirB) { airB.assign((size_t)g->numParticles * 3, 0.0f); anyAirB = true; }
					const float fo = 1.0f - sqrtf(best) * (1.0f / MC_BLADE_AIR_R);
					mcMA(&airB[(size_t)p * 3], prm->bladeAir * MC_BLADE_AIR_K * bt * fo * fo, sw, &airB[(size_t)p * 3]);	// the hilt does not swing: bt x the swing
				}
			}
			if (anyAirB) for (int p = 0; p < g->numParticles; p++) {
				float* a = &airB[(size_t)p * 3];
				const float l = mcLen(a);
				if (l > MC_BLADE_AIR_MAX) mcScale(a, MC_BLADE_AIR_MAX / l, a);
			}
		}
		// SJE r_meshClothFree: the air a swung arm drags along (see MC_CAPE_ARMAIR), into the blades' air
		if (anyFc && fcArmAir > 0.0f && capsFrom) {
			bool armAny = false;
			for (int c = 0; c < ncaps; c++) {
				if (caps[c].hard != 1) continue;
				vec3_t va, vb, ax;
				for (int k = 0; k < 3; k++) {
					va[k] = (caps[c].a[k] - capsFrom[c].a[k]) * (1.0f / MC_DT) - in->originVel[k];
					vb[k] = (caps[c].b[k] - capsFrom[c].b[k]) * (1.0f / MC_DT) - in->originVel[k];
				}
				mcSub(caps[c].b, caps[c].a, ax);
				const float len = mcNorm(ax);
				if (len < 0.5f) continue;
				mcMA(va, -mcDot(va, ax), ax, va);
				mcMA(vb, -mcDot(vb, ax), ax, vb);
				if (mcLen(va) < MC_CAPE_ARMAIR_SWING && mcLen(vb) < MC_CAPE_ARMAIR_SWING) continue;
				const float reach = len + caps[c].r + MC_CAPE_ARMAIR_R;
				for (int p = 0; p < g->numParticles; p++) {
					if (!fcP[p] || g->freedom[p] <= 0.0f) continue;
					const float* pp = &in->p[p * 3];
					vec3_t ap, cl, dd;
					mcSub(pp, caps[c].a, ap);
					if (mcDot(ap, ap) > reach * reach) continue;
					float s = mcDot(ap, ax) / len;
					s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
					mcMA(caps[c].a, s * len, ax, cl);
					mcSub(pp, cl, dd);
					float d = mcLen(dd) - caps[c].r;
					if (d >= MC_CAPE_ARMAIR_R) continue;
					if (d < 0.0f) d = 0.0f;
					if (!anyAirB) { airB.assign((size_t)g->numParticles * 3, 0.0f); anyAirB = true; }
					const float fo = 1.0f - d * (1.0f / MC_CAPE_ARMAIR_R), k = fcArmAir * fo * fo;
					float* a = &airB[(size_t)p * 3];
					for (int q = 0; q < 3; q++) a[q] += k * (va[q] + s * (vb[q] - va[q]));
					armAny = true;
				}
			}
			if (armAny) for (int p = 0; p < g->numParticles; p++) {
				float* a = &airB[(size_t)p * 3];
				const float l = mcLen(a);
				const float mx = MC_BLADE_AIR_MAX > MC_CAPE_ARMAIR_MAX ? MC_BLADE_AIR_MAX : MC_CAPE_ARMAIR_MAX;
				if (l > mx) mcScale(a, mx / l, a);
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
		// SJE r_meshClothWhirl: in a turn that goes on round and round the cloth is carried with its wearer's turning beyond
		// what a body can do (see MC_WHIRL_MAX)
		in->whirlA.clear();
		in->whirlV.clear();
		{
			float amax = MC_WHIRL_MAX, wt0 = MC_WHIRL_TURN0, wt1 = MC_WHIRL_TURN1, wp0 = MC_WHIRL_PATH0, wp1 = MC_WHIRL_PATH1;
			if (prm->whirl >= 2.0f) amax = 1.0e9f;	// (2: the frame of 'damp' alone - the cloth feels all of the whirl, as cloth would)
			float want = 0.0f;
			if (prm->whirl > 0.0f && !(anyFc && (fcRules & 2))) {	// SJE r_meshClothFree
				if (wt1 > wt0) want = Clamp01((fabsf(in->whirlTurn) - wt0) / (wt1 - wt0));
				if (wp1 > wp0) { const float wp = Clamp01((fabsf(in->whirlPath) - wp0) / (wp1 - wp0)); if (wp > want) want = wp; }
				want = Clamp01(prm->whirl) * want * want * (3.0f - 2.0f * want);
			}
			in->whirlW += (want - in->whirlW) * (1.0f - expf(-MC_DT / MC_WHIRL_EASE));
			if (want == 0.0f && in->whirlW < 0.001f) in->whirlW = 0.0f;
			if (in->whirlW > 0.0f) {
				const float wsc = in->linkScale > 1e-3f ? in->linkScale : 1.0f, lim = amax * wsc;
				const float om = in->whirlRate * (3.14159265f / 180.0f);
				// the path's turn as measured is behind by what its easing costs a vector that goes round at om (the origin's
				// speed over 50 ms, then MC_WHIRL_ACC_T): turned on and raised by that
				const float x1 = om * 0.05f, x2 = om * MC_WHIRL_ACC_T, phi = atanf(x1) + atanf(x2);
				float gain = sqrtf((1.0f + x1 * x1) * (1.0f + x2 * x2));
				if (gain > 2.0f) gain = 2.0f;
				vec3_t ac, cr;
				mcCross(in->up, in->whirlAcc, cr);
				for (int k = 0; k < 3; k++) ac[k] = gain * (in->whirlAcc[k] * cosf(phi) + cr[k] * sinf(phi));
				in->whirlV.assign((size_t)g->numParticles * 3, 0.0f);
				bool any = false;
				for (int p = 0; p < g->numParticles; p++) {
					if (g->freedom[p] <= 0.0f) continue;
					vec3_t r, a, tr;
					mcSub(&in->p[p * 3], in->lastOrigin, r);
					mcMA(r, -mcDot(r, in->up), in->up, r);
					// the frame 'damp' works in, here: his (see the damp lag) and this point's share of the turn about his axis
					mcCross(in->up, r, tr);
					mcMA(in->dampVel, in->whirlW * om, tr, &in->whirlV[(size_t)p * 3]);
					// what a point carried rigidly with him would need here: the path's turn, and the turn about his upright axis
					mcMA(ac, -om * om, r, a);
					const float l = mcLen(a);
					if (l <= lim) continue;
					if (!any) { in->whirlA.assign((size_t)g->numParticles * 3, 0.0f); any = true; }
					mcScale(a, in->whirlW * (1.0f - lim / l), &in->whirlA[(size_t)p * 3]);
				}
			}
		}
		// SJE: r_meshClothAero: the air pushes each triangle (by its own normal, area and velocity)
		const bool aero = prm->aero && g->tris.size() >= 3;
		const bool physAir = prm->airPhys > 0.0f;	// SJE r_meshClothAirPhys
		float physSkin = MC_AIRPHYS_SKIN, physGain = MC_AIRPHYS_GAIN, physLiftK = MC_AIRPHYS_LIFT, physPocket = MC_AIRPHYS_POCKET, physUmax = MC_AIRPHYS_UMAX;
		bool physBillow = true;	// (the billow push stays: the air that gets under a cape in a run - a point's own drag does not give that)
		const bool wornCap = prm->wornAir < 1.0f && (int)g->wrap.size() == g->numParticles && (int)g->under.size() == g->numParticles;	// SJE r_meshClothWornAir
		bool sinkOn = prm->airFree > 0.0f;	// SJE r_meshClothAirFree: the air's drag on loose cloth's own way down (below)
		float sinkV = MC_FREE_SINK;
		// ... and the landing: how strongly, the speed allowed at weight 1, and the wearer's own speed up or down
		float landV = MC_FREE_LAND, landKK = MC_FREE_LAND_K;
		bool landAnim = tgtFrom.size() == in->p.size() && tgtTo.size() == in->p.size();	// ... against each point's animated spot too
		const float landK = sinkOn && landV > 1.0f ? Clamp01(in->landAir * 2.0f) * landKK : 0.0f;
		const float landVz = in->originVel[2];
		// SJE r_meshClothCalm: which points a limb moved lately (set by FaceCollide, read by SolveSelfPairs)
		if (in->limbPush.size() != (size_t)g->numParticles) in->limbPush.assign((size_t)g->numParticles, 0);
		else for (unsigned char& lp : in->limbPush) if (lp) lp--;
		if (in->thrown.size() != (size_t)g->numParticles) in->thrown.assign((size_t)g->numParticles, 0);	// SJE r_meshClothRest
		else for (unsigned char& th : in->thrown) if (th) th--;
		if (in->limbThrown.size() != (size_t)g->numParticles) in->limbThrown.assign((size_t)g->numParticles, 0);	// SJE r_meshClothLimbLie
		else for (unsigned char& th : in->limbThrown) if (th) th--;
		for (int s = 0; s < sub; s++) {
			const float t1 = (float)(s + 1) / sub;
			if (s > 0) in->targetSub.assign(in->target.begin(), in->target.end());
			for (size_t i = 0; i < tgtTo.size(); i++) in->target[i] = tgtFrom[i] + (tgtTo[i] - tgtFrom[i]) * t1;
			if (heavy) {
				// SJE: each point's animated spot at the end of this substep ('bell' blended in)
				for (int p = 0; p < g->numParticles; p++) {
					float* A = &in->anchor[p * 3];
					mcCopy(&in->target[p * 3], A);
					if (g->freedom[p] <= 0.0f) continue;
					if (bellStep && g->bellW[p] > 0.0f) {
						const float w = Clamp01(g->bellW[p] * g->freedom[p]);
						const float* bf = &in->bellFrom[p * 3], * bt = &in->bellTo[p * 3];
						for (int k = 0; k < 3; k++) A[k] += w * (bf[k] + (bt[k] - bf[k]) * t1 - A[k]);
					}
					// never under the floor: a crouch takes the pose (and the hip-carried bell) down through it, and a
					// spring or tether toward a spot under the ground fought the floor every substep
					if (prm->floor) {
						vec3_t fr;
						mcSub(A, prm->floorPt, fr);
						const float u = mcDot(fr, prm->floorN) - MC_FLOOR_GAP;
						if (u < 0.0f) mcMA(A, -u, prm->floorN, A);
					}
				}
			}
			if (solve.bendModel) {
				// SJE: the curvature the stencils hold in this substep
				if (bendEnds) {
					for (size_t b = 0; b < in->bendH.size(); b++) {
						const float h0 = in->bendH0[b], h1 = in->bendH1[b];
						in->bendH[b] = h0 >= MC_BEND_NONE || h1 >= MC_BEND_NONE ? MC_BEND_NONE : h0 + (h1 - h0) * t1;
					}
				}
				else BendTargets(g, in->target, in->bendH, solve.bendModel == 2);
				solve.bendH = in->bendH.data();
			}
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
				if (heavy && (kDampH[p] > 0.0f || kDampV[p] > 0.0f)) {
					// SJE 'damp': the cloth's speed in a frame that travels with the character but
					// does not turn with him dies out, sideways and up-down apart. So his run is not damped (no head wind
					// that streams the cape back), a swing after a stop or a turn dies out in about half a second, and
					// in a turn or a saber swing the cape still lags and flares. (Damped against its animated spot
					// instead, the cape was dragged round with every turn: it hardly flared.)
					vec3_t va, r, rh;
					if (!in->whirlV.empty()) mcScale(&in->whirlV[(size_t)p * 3], h, va);	// SJE r_meshClothWhirl: the frame goes round with him
					else mcScale(in->dampVel, h, va);	// SJE r_meshClothDampLag (his speed, with a lag; lag 0: in->originVel)
					// the frame travels with him over the ground only, not up and down: travelling with his jumps it
					// glued the cape to him in the air and on landing; so a jump or a fall has the cape lag and settle
					// by its own weight, as without 'damp'
					mcMA(va, -mcDot(va, in->up), in->up, va);
					mcSub(v, va, r);
					const float rv = mcDot(r, in->up);
					mcMA(r, -rv, in->up, rh);
					mcMA(v, -kDampH[p], rh, v);
					mcMA(v, -kDampV[p] * rv, in->up, v);
				}
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
					float ws = 1.0f;	// SJE: light cloth in a storm (stormK above)
					if (stormK > 0.0f && g->weight[p] < stormW) ws = 1.0f - stormK * (1.0f - g->weight[p] / stormW);
					// ... and a layer the model has under other cloth (tunic strips under a robe) is in that cloth's lee: it gets
					// the wind as it gets the travel air (r_meshClothWornAir). In full the storm blew Ben's tunic strips out from
					// under his robe as jagged pieces stretched to three times their length. No wind: no effect.
					if (wornCap && g->under[p]) ws *= prm->wornAir;
					mcMA(v, (flutter ? -h * windF[p] : -h) * ws, windNow, rel);		// v is per substep, wind is per second
					if (anyAirT) mcMA(rel, -h * ws, &airT[(size_t)p * 3], rel);	// SJE r_meshClothTurbulence: the eddies are air too
					if (anyAirB) mcMA(rel, -h * ws, &airB[(size_t)p * 3], rel);	// SJE r_meshClothBladeAir
					const bool fcPt = anyFc && physAir && fcP[p] != 0;	// SJE r_meshClothFree
					if (fcPt) {
						// the air field scaled as a whole: the point's speed against the wearer, less the scaled air against him
						// (its wind with gust and ripple, eddies); a blade's air is no wind and stays as it is
						vec3_t aw = { 0.0f, 0.0f, 0.0f };
						mcMA(aw, (flutter ? h * windF[p] : h) * ws, windFc, aw);
						if (anyAirT) mcMA(aw, h * ws, &airT[(size_t)p * 3], aw);
						mcMA(v, -(1.0f - fcK) * h, in->originVel, rel);
						mcMA(rel, -fcK, aw, rel);
						if (anyAirB) mcMA(rel, -h * ws, &airB[(size_t)p * 3], rel);
					}
					const float* nn = &n[p * 3];
					const float vn = mcDot(rel, nn);
					float kn = prm->drag * h * invW;
					if (kn > 1.0f) kn = 1.0f;
					float kt = prm->airDrag * h * invW;
					if (kt > 0.5f) kt = 0.5f;
					vec3_t relT;
					mcMA(rel, -vn, nn, relT);
					if (physAir) {
						// SJE r_meshClothAirPhys: by the point's own speed against the air (rel, per substep) - across the cloth by its
						// square, along it a small part of that. Implicit: u / (1 + c |u|) never passes rest.
						const float tkp = g->tether[p] * (1.0f / MC_AIR_TETHER);
						float akp = tkp >= 1.0f ? 1.0f : tkp * tkp * tkp;
						if (wornCap && (g->wrap[p] || g->under[p]) && akp > prm->wornAir) akp = prm->wornAir;
						const float lt = mcLen(relT), lr = mcLen(rel), um = (fcPt && fcPeak > 0.0f ? fcUmax * fcPeak : physUmax) * h;	// SJE r_meshClothFree
						const float uk = lr > um && um > 0.0f ? um / lr : 1.0f;	// (beyond MC_AIRPHYS_UMAX the push grows no further)
						const float cq = prm->dragQuad * physGain * (fcPt ? fcGain : 1.0f) * invW * akp * uk * uk;
						const float an = cq * (fabsf(vn) + physLiftK * lt);
						mcMA(v, -vn * an / (1.0f + an), nn, v);
						const float at = cq * (fcPt ? fcSkin : physSkin) * lt;	// SJE r_meshClothFree: a fluttering sheet's drag
						if (lt > 0.0f) mcMA(v, -at / (1.0f + at), relT, v);
						// the pocket (see MC_AIRPHYS_POCKET): not for cloth worn round the body
						if (physPocket > 0.0f && (int)g->lraAnchor.size() == g->numParticles && g->lraAnchor[p] >= 0 && g->lraAnchor[p] != p
							&& !(wornCap && (g->wrap[p] || g->under[p]))) {
							vec3_t sa, ro;
							mcSub(&in->p[g->lraAnchor[p] * 3], pp, sa);
							if (mcNorm(sa) > 1e-3f) {
								// (the WEARER's speed against the air, not the point's own: the pocket is cape and wearer together. With the
								// point's own speed every swing downwards pumped the cape outwards - in a plain run the hem stood 12 units
								// over the origin, standing after it 22.)
								float uin = h * (mcDot(fcPt ? windFc : windNow, sa) - mcDot(in->originVel, sa));	// along the cloth towards the attached edge (per substep)
								if (fcPt && fcPeak > 0.0f) { uin *= fcK; if (uin > um) uin = um; }	// SJE r_meshClothFree: of the scaled field
								if (uin > 0.0f) {
									mcSub(pp, in->lastOrigin, ro);
									mcMA(ro, -mcDot(ro, in->up), in->up, ro);
									if (mcNorm(ro) > 1e-3f) mcMA(v, cq * physPocket * uin * uin * mcDot(nn, ro), nn, v);
								}
								if (fcPt && fcPSide > 0.0f) {
									// SJE r_meshClothFree: a cape is open at its SIDES too. Air that comes along it from the side gets under its
									// edge and lifts it off its wearer, as the fall's air does from the hem (MC_CAPE_PSIDE): the part of the air
									// against the wearer that goes along the cloth, across the way to the attached edge.
									vec3_t as, rs;
									for (int q = 0; q < 3; q++) as[q] = h * fcK * (windFc[q] - in->originVel[q]);
									mcMA(as, -mcDot(as, nn), nn, as);
									mcMA(as, -mcDot(as, sa), sa, as);
									float us = mcLen(as);
									if (us > um) us = um;
									mcSub(pp, in->lastOrigin, rs);
									mcMA(rs, -mcDot(rs, in->up), in->up, rs);
									if (us > 0.0f && mcNorm(rs) > 1e-3f) mcMA(v, cq * fcPSide * us * us * mcDot(nn, rs), nn, v);
								}
							}
						}
					}
					else if (aero) {
						// SJE: the triangles' push (normal drag + lift), area-weighted onto this point
						const float aw = in->aeroW[p];
						if (aw > 1e-8f) mcMA(v, 1.0f / aw, &in->aeroDv[p * 3], v);
					}
					else {
						mcMA(v, -vn * kn, nn, v);
					}
					if (!physAir) mcMA(v, -kt, relT, v);
					if (prm->dragQuad > 0.0f && !physAir) {
						// the travel wind through the cloth (speeds are per substep: |a| h^2 = c |U h| |U h|).
						// How much of it a surface catches goes with its tether, the config's measure of how far
						// that cloth is meant to move off the figure: a cape or an outer robe (40 and up) all of
						// it, an inner skirt (25) a quarter, a tabard (19) a tenth, a tunic (13) next to none.
						// Without that Vader's inner robe flew out behind him like a second cape and bared his legs.
						const float tk = g->tether[p] * (1.0f / MC_AIR_TETHER);
						// SJE r_meshClothAirFree: worn cloth (a robe, a skirt round the hips, the strips under it) keeps the travel
						// wind under its old single cap: with the wind's higher cap a storm from the side blew a hip skirt across
						// its wearer's legs and through them (cloth inside the body in 89 of 75 steps' triangles)
						float un = mcDot(wornCap && (g->wrap[p] || g->under[p]) ? airHW : airH, nn);
						if (anyAirT) un -= h * airTQ * mcDot(&airT[(size_t)p * 3], nn);	// SJE r_meshClothTurbulence: the travel air with its eddies
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
					if (anyAirB && prm->dragQuad > 0.0f && !physAir) {	// (SJE r_meshClothAirPhys: it is in 'rel' there)
						// SJE r_meshClothBladeAir: the blade's air through the cloth, by its square as the travel air (not cut down for worn
						// cloth: the blade is outside it)
						const float ub = h * mcDot(&airB[(size_t)p * 3], nn);
						mcMA(v, prm->dragQuad * fabsf(ub) * ub * invW * ws, nn, v);
					}
					if (breezeK > 0.0f) {
						// SJE: the breeze through the cloth, as the wind's normal drag
						const float* b = &breezeV[(size_t)p * 2];
						mcMA(v, kn * h * (b[0] * nn[0] + b[1] * nn[1]), nn, v);
					}
					if (billowQ > 0.0f && (!physAir || physBillow)) {
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
								mcMA(v, billowQ * (fcPt ? fcBillow : 1.0f) * prof * lee * out * fabsf(out) * invW * ak, nn, v);	// SJE r_meshClothFree: x MC_CAPE_BILLOW
							}
						}
					}
				}
				// SJE r_meshClothAirFree: loose cloth sinks through the air no faster than MC_FREE_SINK x the root of its weight:
				// the air's drag on its own way down, by the square of that speed (it balances the cloth's weight at that speed).
				// A force, not a cap on the speed: under a falling wearer the cloth is pulled down by its attachment faster than
				// it can sink, and a cap that took that speed away in every substep left the links to drag every point the
				// whole way - the edges stretched to 1.4 times their length.
				if (sinkOn && !physAir && !underWater && !(wornCap && (g->wrap[p] || g->under[p]))) {
					const float vt = sinkV * sqrtf(g->weight[p] * (1.0f + Clamp01(prm->wet)));
					const float sp = windNow[2] - v[2] * (1.0f / h);	// units/s down against the air
					if (sp > 0.0f) {
						float dv = prm->gravity * (sp / vt) * (sp / vt) * h;	// units/s taken away in this substep
						if (sp > vt && dv > sp - vt) dv = sp - vt;	// ... never past the speed it settles at
						else if (sp <= vt && dv > 0.5f * sp) dv = 0.5f * sp;
						v[2] += dv * h;
					}
					// ... and for a moment after a fall no faster than MC_FREE_LAND against its wearer (see there)
					if (landK > 0.0f) {
						float base = landVz * h;	// the wearer's way down in this substep, or the point's animated spot's if that is more
						if (landAnim) { const float az = (tgtTo[(size_t)p * 3 + 2] - tgtFrom[(size_t)p * 3 + 2]) * (1.0f / sub); if (az < base) base = az; }
						const float lim = base - landV * sqrtf(g->weight[p] * (1.0f + Clamp01(prm->wet))) * h;
						if (v[2] < lim) v[2] += (lim - v[2]) * landK;
					}
				}
				if (!in->tailA.empty() && !underWater) mcMA(v, h * h, &in->tailA[(size_t)p * 3], v);	// SJE r_meshClothTailSide
				if (!in->whirlA.empty() && !underWater) mcMA(v, h * h, &in->whirlA[(size_t)p * 3], v);	// SJE r_meshClothWhirl
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
				if (heavy && kSpring[p] > 0.0f) {
					// SJE 'spring': pulled toward its animated spot, strong near the attached edge
					vec3_t d;
					mcSub(&in->anchor[p * 3], pp, d);
					mcMA(pp, kSpring[p], d, pp);
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
			solve.faceNow = (s & 1) || s == sub - 1 || (solve.armGapOn && g->armGap > 0.0f);	// SJE 'armgap'
			SolveConstraints(g, in, cs, ncaps, solve);
			if (selfSub) SolveSelfPairs(g, in, prm->frictionScale, prm->calm != 0, MC_FIGHT_TRIP, fineSelf);
			if (prm->layers && !g->layerPairs.empty()) SolveLayerPairs(g, in, prm);	// SJE: cloth layers keep their order
			FloorCollide(g, in, prm);
			ContactVelocity(g, in, prm->frictionScale);
		}
		if (prm->inside) {	// SJE r_meshClothInside: in strong air only (a storm, a level wind, a pusher's air stream)
			vec3_t air;
			mcAdd(prm->wind, prm->zoneWind, air);
			if (mcDot(air, air) > MC_INSIDE_WIND * MC_INSIDE_WIND) KeepInside(g, in);
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
		if (mcDot(prm->zoneWind, prm->zoneWind) > 1e-4f) return false;	// SJE r_meshClothFans
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
		// SJE r_meshClothSleepShown: cloth this character's skin has switched off (r_meshClothShown: a point no switched-on surface
		// uses) is simulated with the rest but nobody sees it, and it holds nothing: it does not keep the drawn cloth awake. Kylo's
		// hidden robe went on settling under his cloak in a still crouch long after the cloak was at rest.
		const unsigned offR = prm->sleepShown && prm->shown && in->offMask && (int)g->partUse.size() == g->numParticles ? in->offMask : 0u;
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
				if (offR && !(g->partUse[p] & ~offR)) continue;	// SJE r_meshClothSleepShown
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

	// SJE 'bell': the group's bind shape carried by the hip bone alone, in world space: a rigid bell that turns with the
	// pelvis and never walks with the legs. Without it the animated shape is wherever the model's own skinning puts it - a cape skinned
	// partly to thighs and shins swings with every stride. The points of a line with 'bell' measure their tether and
	// spring from this shape instead, blended in by B x their freedom (Step). Clears bellNow where not used.
	static void BellPose(const mcGroup_t* g, mcInstance_t* in, const mcParams_t* prm, mcBoneFn_t boneFn, void* boneCtx,
		const vec3_t origin, const vec3_t axis[3]) {
		if (!g->anyBell || prm->hipBone < 0 || (int)g->bellW.size() != g->numParticles || (int)g->bindPos.size() < g->numParticles * 3) {
			in->bellNow.clear();
			return;
		}
		const float (*m)[4] = boneFn(boneCtx, prm->hipBone);
		in->bellNow.resize((size_t)g->numParticles * 3);
		for (int p = 0; p < g->numParticles; p++) {
			const float* b = &g->bindPos[p * 3];
			vec3_t l;
			for (int r = 0; r < 3; r++) l[r] = m[r][0] * b[0] + m[r][1] * b[1] + m[r][2] * b[2] + m[r][3];
			ToWorld(l, origin, axis, &in->bellNow[p * 3]);
		}
	}

	void Simulate(const mcGroup_t* g, mcInstance_t* in, int timeMs, const vec3_t origin, const vec3_t axis[3],
		const mcCapsule_t* caps, int ncaps, const mcParams_t* prm, mcBoneFn_t boneFn, void* boneCtx) {
		mcCopy(axis[0], in->fwd);
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
		// SJE 'bell' (only groups with a line that asks for it): this frame's hip-carried shape. The
		// steps below take it from where the last step left it to here, as the animated pose (bellFrom / bellTo).
		in->bellOk = false;
		if (g->anyBell) BellPose(g, in, prm, boneFn, boneCtx, origin, axis);
		auto bellAtOnce = [&]() {	// settling, sleeping: every step at this frame's shape
			if (in->bellNow.empty()) return;
			in->bellPrev = in->bellNow; in->bellSim = in->bellNow; in->bellFrom = in->bellNow; in->bellTo = in->bellNow;
			in->bellOk = true;
		};

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
			if (g->anyBell) bellAtOnce();	// SJE 'bell'
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
			if (g->anyBell) bellAtOnce();	// SJE 'bell'
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
			if (g->anyBell) bellAtOnce();	// SJE 'bell'
		}
		else {
			mcCopy(axis[2], in->up);
			mcNorm(in->up);
			// SJE 'bell': the hip-carried shape of the last frame and of the last step (first time: this frame's)
			const bool bellRun = !in->bellNow.empty();
			if (bellRun && (in->bellPrev.size() != in->bellNow.size() || in->bellSim.size() != in->bellNow.size())) {
				in->bellPrev = in->bellNow;
				in->bellSim = in->bellNow;
			}

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
				// max acceleration:
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
				// SJE r_meshClothWhirl: the part of it across his travel over the ground - the path turning -, eased over
				// MC_WHIRL_ACC_T; and how far his direction of travel has turned on one way
				{
					vec3_t ah, vh, v0;
					mcMA(acc, -mcDot(acc, in->up), in->up, ah);
					mcMA(vNew, -mcDot(vNew, in->up), in->up, vh);
					mcMA(in->originVel, -mcDot(in->originVel, in->up), in->up, v0);
					const float v2 = mcDot(vh, vh), v02 = mcDot(v0, v0), vmin = MC_WHIRL_V0 * MC_WHIRL_V0;
					if (v2 > vmin) mcMA(ah, -mcDot(ah, vh) / v2, vh, ah);
					else ah[0] = ah[1] = ah[2] = 0.0f;
					const float wk = 1.0f - expf(-dt / MC_WHIRL_ACC_T);
					for (int c = 0; c < 3; c++) in->whirlAcc[c] += (ah[c] - in->whirlAcc[c]) * wk;
					float da = 0.0f;
					if (v2 > vmin && v02 > vmin) {
						vec3_t cx;
						mcCross(v0, vh, cx);
						da = atan2f(mcDot(cx, in->up), mcDot(v0, vh)) * (180.0f / 3.14159265f);
					}
					WhirlCount(in->whirlPath, in->whirlPathBack, da, dt, MC_WHIRL_PATH1 * MC_WHIRL_CAP);
				}
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
					if (bellRun) {	// SJE 'bell': the hip-carried shape is carried along as the animated pose is
						mcAdd(&in->bellPrev[p * 3], shift, &in->bellPrev[p * 3]);
						mcAdd(&in->bellSim[p * 3], shift, &in->bellSim[p * 3]);
					}
					if (g->freedom[p] <= 0.0f) continue;
					vec3_t shiftP;
					mcCopy(shift, shiftP);
					// SJE r_meshClothAirFree: loose cloth the air has let go - a cape up over a landing - is not carried down with
					// the body's drop: it sinks by itself
					if (prm->airFree > 0.0f && (int)in->freeP.size() == g->numParticles && in->freeP[p] > 0.0f
						&& !((int)g->wrap.size() == g->numParticles && (int)g->under.size() == g->numParticles && (g->wrap[p] || g->under[p])))
						mcMA(shiftP, -(in->freeP[p] < 1.0f ? in->freeP[p] : 1.0f), bodyShift, shiftP);
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
				in->whirlAcc[0] = in->whirlAcc[1] = in->whirlAcc[2] = 0.0f;	// SJE r_meshClothWhirl
				in->whirlPath = in->whirlPathBack = 0.0f;
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
				if (bellRun) in->bellTo.assign(in->bellSim.begin(), in->bellSim.end());	// SJE 'bell'
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
					if (bellRun) {	// SJE 'bell': the hip-carried shape over the same step
						in->bellFrom.swap(in->bellTo);
						in->bellTo.resize(target.size());
						for (size_t i = 0; i < target.size(); i++) in->bellTo[i] = in->bellPrev[i] + (in->bellNow[i] - in->bellPrev[i]) * t1;
						in->bellOk = true;
					}
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
				if (bellRun) {	// SJE 'bell'
					if (steps > 0) in->bellSim.assign(in->bellTo.begin(), in->bellTo.end());
					in->bellPrev.assign(in->bellNow.begin(), in->bellNow.end());
				}
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
		// SJE 'wrap': the grid points where they are drawn now (animated place + offset), for drawing from the triangles
		const bool wrapDraw = g->proxied && g->wrapOn && g->wrapTri.size() == (size_t)g->numDraw * MC_WRAP_K && g->wrapW.size() == g->wrapTri.size();
		thread_local static std::vector<float> wrapP;
		if (wrapDraw) {
			wrapP.resize((size_t)g->numParticles * 3);
			for (int q = 0; q < g->numParticles; q++) mcAdd(&target[q * 3], &off[q * 3], &wrapP[q * 3]);
		}
		for (int d = 0; d < g->numDraw; d++) {
			vec3_t o = { 0, 0, 0 };
			if (wrapDraw && g->drawFree[d] > 0.0f && g->wrapTri[(size_t)d * MC_WRAP_K] >= 0) {
				// the blend of its grid triangles as they are now (each: where it lay in the triangle's plane, and as far
				// off it along the triangle's normal), shared by how near each lay in the bind pose
				vec3_t pos = { 0, 0, 0 };
				float fq = 0.0f, wUsed = 0.0f;
				// (a grid triangle squeezed to under a quarter of its bind size in a fast swing has an unsteady normal - a
				// point drawn from it along that normal can be flung off for a frame: such a triangle is left out of the
				// blend while it is squeezed; the nearest one is always used)
				const bool wrapGuard = g->wrapA.size() == g->wrapTri.size();
				for (int j = 0; j < MC_WRAP_K; j++) {
					const size_t s = (size_t)d * MC_WRAP_K + j;
					if (g->wrapTri[s] < 0) break;
					const int* tc = &g->tris[g->wrapTri[s] * 3];
					const float* bc = &g->wrapBc[s * 3];
					const float* pa = &wrapP[tc[0] * 3], * pb = &wrapP[tc[1] * 3], * pc = &wrapP[tc[2] * 3];
					vec3_t e0, e1, nf;
					mcSub(pb, pa, e0); mcSub(pc, pa, e1); mcCross(e0, e1, nf);
					const float areaNow = mcNorm(nf);
					if (areaNow < 1e-6f) nf[0] = nf[1] = nf[2] = 0.0f;
					if (j > 0 && wrapGuard && areaNow < 0.25f * g->wrapA[s]) continue;
					const float w = g->wrapW[s];
					wUsed += w;
					for (int k = 0; k < 3; k++) pos[k] += w * (bc[0] * pa[k] + bc[1] * pb[k] + bc[2] * pc[k] + g->wrapH[s] * nf[k]);
					// near the attached edge a point may be less free than the grid triangle's corners (as below)
					float tq = 0.0f, ws = 0.0f;
					for (int k = 0; k < 3; k++) { const float bk = bc[k] > 0.0f ? bc[k] : 0.0f; tq += bk * g->freedom[tc[k]]; ws += bk; }
					fq += w * (ws > 1e-6f ? tq / ws : 0.0f);
				}
				if (wUsed > 1e-6f && wUsed < 0.9999f) { mcScale(pos, 1.0f / wUsed, pos); fq /= wUsed; }
				mcSub(pos, &drawTarget[d * 3], o);
				if (fq > g->drawFree[d]) mcScale(o, g->drawFree[d] / fq, o);
			}
			else if (g->drawFree[d] > 0.0f) {
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
		// (SJE 'wrap': drawn from the grid's triangles the cloth is continuous already: not smoothed)
		if (g->proxied && !wrapDraw) {
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
		// SJE: ... and drawn TRIANGLES are kept off them too (cloth on a grid). The dense cape drawn from the grid has
		// small triangles (about 1.8 units); a hand or the hilt in the middle of one touched none of its corners, so the
		// point push did not see it: in a running turn the cape swung sideways over Vader's sword hand and the hand with
		// the hilt came out through it (drawn triangles through a hand or arm 3-31 per measure against 2-3 of the
		// grid's). A triangle a limb or a held thing goes into is moved off it along the line from the shape's axis to
		// the triangle (its animated side where the axis goes right through), its free corners shared by how near the
		// touch lies to each. The moves are gathered per drawn point and their sum is held to the same short nudge a
		// point gets (1.5 units), so nothing tears: added straight onto the point, the moves of all its triangles - up
		// to six - from every limb shape near it summed up, and in a saber swing single points shot out of the
		// shoulder by many units for a frame or two (thin spikes).
		thread_local static std::vector<float> triMove;
		if (nl && g->proxied) {
			triMove.assign((size_t)g->numDraw * 3, 0.0f);
			const int ndt = (int)(g->drawTris.size() / 3);
			for (int c = 0; c < nl; c++) {
				const mcCapsule_t& cap = limbs[c];
				const float reach = cap.r + MC_MARGIN;
				float lo[3], hi[3];
				for (int k = 0; k < 3; k++) {
					lo[k] = (cap.a[k] < cap.b[k] ? cap.a[k] : cap.b[k]) - reach;
					hi[k] = (cap.a[k] > cap.b[k] ? cap.a[k] : cap.b[k]) + reach;
				}
				for (int t = 0; t < ndt; t++) {
					const int id[3] = { g->drawTris[t * 3], g->drawTris[t * 3 + 1], g->drawTris[t * 3 + 2] };
					const float fr[3] = { g->drawFree[id[0]], g->drawFree[id[1]], g->drawFree[id[2]] };
					if (fr[0] <= 0.0f && fr[1] <= 0.0f && fr[2] <= 0.0f) continue;
					vec3_t P[3];
					for (int k = 0; k < 3; k++) mcAdd(&drawTarget[id[k] * 3], &out[id[k] * 3], P[k]);
					bool away = false;
					for (int a = 0; a < 3 && !away; a++) {
						const float mn = P[0][a] < P[1][a] ? (P[0][a] < P[2][a] ? P[0][a] : P[2][a]) : (P[1][a] < P[2][a] ? P[1][a] : P[2][a]);
						const float mx = P[0][a] > P[1][a] ? (P[0][a] > P[2][a] ? P[0][a] : P[2][a]) : (P[1][a] > P[2][a] ? P[1][a] : P[2][a]);
						away = mx < lo[a] || mn > hi[a];
					}
					if (away) continue;
					float bc[3], tt;
					const float dd = ClosestSegTri(P[0], P[1], P[2], cap.a, cap.b, bc, tt);
					float need = reach - dd;
					if (need <= 0.0f) continue;
					if (need > 1.5f) need = 1.5f;
					// the direction: from the axis to the touch; right through, the side the animated triangle is on
					vec3_t q, s, dir, ab;
					for (int k = 0; k < 3; k++) q[k] = bc[0] * P[0][k] + bc[1] * P[1][k] + bc[2] * P[2][k];
					mcSub(cap.b, cap.a, ab);
					mcMA(cap.a, tt, ab, s);
					mcSub(q, s, dir);
					if (mcNorm(dir) < 1e-3f) {
						vec3_t qa, sa;
						for (int k = 0; k < 3; k++) qa[k] = bc[0] * drawTarget[id[0] * 3 + k] + bc[1] * drawTarget[id[1] * 3 + k] + bc[2] * drawTarget[id[2] * 3 + k];
						ClosestOnAxis(cap, qa, sa);
						mcSub(qa, sa, dir);
						if (mcNorm(dir) < 1e-3f) continue;
					}
					float den = 0.0f;
					for (int k = 0; k < 3; k++) if (fr[k] > 0.0f) den += bc[k] * bc[k];
					if (den < 1e-4f) continue;
					for (int k = 0; k < 3; k++) {
						if (fr[k] <= 0.0f || bc[k] <= 0.0f) continue;
						float* to = &triMove[(size_t)id[k] * 3];
						mcMA(to, need * bc[k] / den, dir, to);
					}
				}
			}
			for (int d = 0; d < g->numDraw; d++) {
				float* m = &triMove[(size_t)d * 3];
				const float ml = mcLen(m);
				if (ml < 1e-6f) continue;
				mcMA(&out[d * 3], ml > 1.5f ? 1.5f / ml : 1.0f, m, &out[d * 3]);
			}
		}
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
		// Not for cloth simulated on a grid ("proxy N"): its drawn points follow the grid and are not smoothed after this
		// push, so each pushed triangle of the dense drawn mesh stood off on its own - boxy corners on both shoulders of
		// the dense Vader, standing still, for 4 % fewer body points through the cape.
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
			// SJE: heavy-cape options (each off unless given)
			else if (!Q_stricmp(tok, "damp")) {
				d.dampH = (float)atof(COM_ParseExt(&p, qfalse)); d.dampV = (float)atof(COM_ParseExt(&p, qfalse));
				d.dampH = d.dampH < 0.0f ? 0.0f : (d.dampH > 0.9f ? 0.9f : d.dampH);
				d.dampV = d.dampV < 0.0f ? 0.0f : (d.dampV > 0.9f ? 0.9f : d.dampV);
			}
			else if (!Q_stricmp(tok, "ring")) { d.ring = (float)atof(COM_ParseExt(&p, qfalse)); if (d.ring < 0.0f) d.ring = 0.0f; }
			else if (!Q_stricmp(tok, "bell")) d.bell = Clamp01((float)atof(COM_ParseExt(&p, qfalse)));
			else if (!Q_stricmp(tok, "armgap")) { d.armGap = (float)atof(COM_ParseExt(&p, qfalse)); if (d.armGap < 0.0f) d.armGap = 0.0f; if (d.armGap > 4.0f) d.armGap = 4.0f; }	// SJE
			else if (!Q_stricmp(tok, "wrap")) d.wrap = true;
			else if (!Q_stricmp(tok, "free")) d.freeC = Clamp01((float)atof(COM_ParseExt(&p, qfalse)));	// SJE r_meshClothFree
			else if (!Q_stricmp(tok, "spring")) {
				d.spring = (float)atof(COM_ParseExt(&p, qfalse)); d.springFade = (float)atof(COM_ParseExt(&p, qfalse));
				d.spring = d.spring < 0.0f ? 0.0f : (d.spring > 0.9f ? 0.9f : d.spring);
				if (d.springFade < 0.0f) d.springFade = 0.0f;
			}
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

	// SJE r_meshClothGarment: is this body surface itself a skirt? Some models wear a second, inner robe or the lower
	// half of a coat as a plain body part: a tube round both legs, bound to the hips and half to each leg. Skinned, it
	// is stiff and swings with the legs. Fitted like the body (r_meshClothBodyFit) it gave shapes as wide as the cloth
	// worn over it, at the hips and down both legs, and at a run they shook that cloth at every step (a floor-length
	// robe: its points' change of move from step to step 1.6 units, a fifth of them turning round; 0.8 and a
	// twentieth without those shapes, as for other robes). Such a part is no body for cloth to lie on: it is left out
	// of the fit. The legs under it (their own surfaces, the leg capsules) hold the cloth as on any robed figure.
	// A skirt here: reaches under the knees (its lower edge under MC_GARMENT_LOW of the model's height), begins at
	// the waist or lower (its upper edge under MC_GARMENT_TOP: a whole robe from the shoulders down is the body a
	// cape lies on, and stays), is tall (MC_GARMENT_TALL), and in the lowest part of its own height
	// (MC_GARMENT_BAND) its vertices lie all round the model's upright axis - in each of eight sectors. Two trouser
	// legs in one surface leave the sectors in front and behind empty; shorts and knee-length tunics end too high;
	// one leg lies on one side. (Of 273 models with cloth lines 13 have such a part, by their own flags.)
	const float MC_GARMENT_LOW = 0.25f, MC_GARMENT_TOP = 0.70f, MC_GARMENT_TALL = 0.20f, MC_GARMENT_BAND = 0.30f;
	bool FitGarment(const mdxmSurface_t* s, const float top) {
		if (s->numVerts < 24 || top < 1.0f) return false;
		const mdxmVertex_t* v = Verts(s);
		float lo = 1e9f, hi = -1e9f;
		for (int i = 0; i < s->numVerts; i++) {
			const float z = v[i].vertCoords[2];
			if (z < lo) lo = z;
			if (z > hi) hi = z;
		}
		if (lo > MC_GARMENT_LOW * top || hi > MC_GARMENT_TOP * top || hi - lo < MC_GARMENT_TALL * top) return false;
		float band = lo + MC_GARMENT_BAND * (hi - lo);
		if (band > MC_GARMENT_LOW * top) band = MC_GARMENT_LOW * top;
		int sect[8] = {}, n = 0;
		for (int i = 0; i < s->numVerts; i++) {
			if (v[i].vertCoords[2] > band) continue;
			const float x = v[i].vertCoords[0], y = v[i].vertCoords[1];
			if (x * x + y * y < 1.0f) continue;
			// (sectors centred on the axes: straight in front, front-left, left ...)
			const int k = (int)floorf((atan2f(y, x) + 3.14159265f + 0.39269908f) * (8.0f / 6.2831853f));
			sect[k & 7]++;
			n++;
		}
		if (n < 12) return false;
		for (int k = 0; k < 8; k++) if (!sect[k]) return false;
		return true;
	}

	// SJE r_meshClothShown: 'drawn' (one byte per surface index; nullptr = not known) says which surfaces this
	// character has switched on. Without it the model's own flags decide - and a model that holds several outfits
	// has most of them on: a hood, a scarf, hip padding the skin does not draw then pushed the cloth off the body.
	void FitBody(const mcGroup_t* g, const int* boneClass, int numBones, std::vector<mcFitShape_t>& out, const unsigned char* drawn = nullptr, const int numDrawn = 0, const int garment = 0) {
		out.clear();
		if (g->surfs.empty()) return;
		const mdxmHeader_t* h = HeaderOf(g->surfs[0]);
		const auto ho = reinterpret_cast<const mdxmHierarchyOffsets_t*>(reinterpret_cast<const byte*>(h) + sizeof(mdxmHeader_t));
		char folder[MAX_QPATH];
		ModelFolder(h->name, folder, sizeof(folder));
		std::unordered_map<int, std::vector<float>> pts;	// bone -> bind positions
		float top = 0.0f;	// SJE r_meshClothGarment: the model's height (bind pose: upright, feet at 0)
		if (garment) for (const mdxmSurface_t* s : LodSurfaces(g->surfs[0])) {
			const mdxmVertex_t* v = Verts(s);
			for (int i = 0; i < s->numVerts; i++) if (v[i].vertCoords[2] > top) top = v[i].vertCoords[2];
		}
		for (const mdxmSurface_t* s : LodSurfaces(g->surfs[0])) {
			if (std::find(g->surfs.begin(), g->surfs.end(), s) != g->surfs.end()) continue;	// cloth isn't body
			const auto sit = g_surfs.find(s);
			if (sit != g_surfs.end() && sit->second.group) continue;	// cloth of another group
			const auto sh = reinterpret_cast<const mdxmSurfHierarchy_t*>(reinterpret_cast<const byte*>(ho) + ho->offsets[s->thisSurfaceIndex]);
			if (drawn ? !(s->thisSurfaceIndex >= 0 && s->thisSurfaceIndex < numDrawn && drawn[s->thisSurfaceIndex]) : (sh->flags & (G2SURFACEFLAG_OFF | G2SURFACEFLAG_ISBOLT)) != 0) continue;
			if (strstr(sh->name, "_cap_") || strstr(sh->name, "_off")) continue;
			if (FindDef(folder, sh->name)) continue;	// a cloth surface (meshcloth.cfg)
			if (garment && FitGarment(s, top)) {	// SJE r_meshClothGarment: a stiff skirt is no body
				continue;
			}
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

const void* MC_BodyFit(const void* clothSurface, const int* boneClass, int numBones, const unsigned char* drawn, const int numDrawn, const int garment) {
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
	if (garment) key = (key ^ 7ULL) * 1099511628211ULL;	// SJE r_meshClothGarment: its own fit
	auto fit = g_fits.find(key);
	if (fit == g_fits.end()) {
		std::vector<mcFitShape_t> shapes;
		FitBody(g, boneClass, numBones, shapes, drawn, numDrawn, garment);
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
		static unsigned int turbSeeds = 0;	// SJE r_meshClothTurbulence: each cloth its own eddies (the same in every run)
		in->turbSeed = 0x9E3779B9u * ++turbSeeds;
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
	ComputeRing(g);	// SJE: 'ring' and 'spring' (only groups that use them)
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

// SJE r_meshClothBladeAir: the lit blades told this frame and the frame before (MC_BladeMotion)
namespace {
	struct mcBladeSeen_t { float a[3], b[3]; };
	std::vector<mcBladeSeen_t> g_bladeCur, g_bladePrev;
	int g_bladeFrame = -1, g_bladeTime = 0, g_bladePrevTime = 0;
}
bool MC_BladeMotion(const int frameId, const int timeMs, const float a[3], const float b[3], float va[3], float vb[3]) {
	va[0] = va[1] = va[2] = vb[0] = vb[1] = vb[2] = 0.0f;
	if (frameId != g_bladeFrame) {
		g_bladeFrame = frameId;
		if (timeMs < g_bladeTime) { g_bladeCur.clear(); g_bladePrev.clear(); g_bladePrevTime = timeMs; }	// the time went back: a new map
		else if (timeMs > g_bladeTime) { g_bladePrev.swap(g_bladeCur); g_bladeCur.clear(); g_bladePrevTime = g_bladeTime; }
		// (the same time - a paused game: this frame's list goes on from the last one's)
		g_bladeTime = timeMs;
	}
	bool known = false;
	for (const mcBladeSeen_t& s : g_bladeCur) {
		vec3_t da, db;
		mcSub(s.a, a, da);
		mcSub(s.b, b, db);
		if (mcDot(da, da) < 0.01f && mcDot(db, db) < 0.01f) { known = true; break; }
	}
	if (!known && g_bladeCur.size() < 64) {
		mcBladeSeen_t s;
		mcCopy(a, s.a);
		mcCopy(b, s.b);
		g_bladeCur.push_back(s);
	}
	const float dt = (float)(g_bladeTime - g_bladePrevTime) * 0.001f;
	if (dt <= 0.0f || dt > 0.1f) return false;
	// the same blade a frame ago: the nearest by hilt and tip (a hilt moves less than 32 units a frame, a tip less than 64)
	int best = -1;
	float bestD = 1e9f;
	for (int i = 0; i < (int)g_bladePrev.size(); i++) {
		vec3_t da, db;
		mcSub(g_bladePrev[i].a, a, da);
		mcSub(g_bladePrev[i].b, b, db);
		const float a2 = mcDot(da, da), b2 = mcDot(db, db);
		if (a2 > 32.0f * 32.0f || b2 > 64.0f * 64.0f) continue;
		if (a2 + b2 < bestD) { bestD = a2 + b2; best = i; }
	}
	if (best < 0) return false;
	for (int c = 0; c < 3; c++) {
		va[c] = (a[c] - g_bladePrev[best].a[c]) / dt;
		vb[c] = (b[c] - g_bladePrev[best].b[c]) / dt;
	}
	return true;
}

// SJE r_meshClothLee: wind shadow. Five looks upwind (the character's origin is the middle of a standing body, feet 24
// below, head 40 above): straight upwind at the legs, the hips and the shoulders, and 25 degrees to either side at the
// hips. A wall within MC_LEE_FULL shelters fully, one at MC_LEE_REACH not at all (smoothly between); the cloth keeps
// MC_LEE_KEEP of the wind in full lee (air comes round and over). Looked at every MC_LEE_MS, eased in and out.
namespace {
	const float MC_LEE_REACH = 192.0f;
	const float MC_LEE_FULL = 40.0f;
	const float MC_LEE_KEEP = 0.25f;
	const int MC_LEE_MS = 150;
	const float MC_LEE_IN = 0.25f;	// seconds to go into the lee ...
	const float MC_LEE_OUT = 0.5f;	// ... and out of it
	struct mcLee_t { int look = 0; int ms = 0; float target = 0.0f; float now = 0.0f; bool ok = false; };
	std::unordered_map<const void*, mcLee_t> g_lee;
}
float MC_WindLee(const void* key, const int timeMs, const float origin[3], const float wind[3], mcLeeTraceFn_t trace) {
	if (g_lee.size() > 256) g_lee.clear();
	mcLee_t& L = g_lee[key];
	bool fresh = false;
	if (!L.ok || timeMs < L.ms || timeMs - L.ms > 1000) {
		L = mcLee_t();
		L.ok = true;
		L.ms = timeMs;
		L.look = timeMs - MC_LEE_MS;
		fresh = true;
	}
	const float wl = sqrtf(wind[0] * wind[0] + wind[1] * wind[1]);
	if (wl < 1.0f || !trace) L.target = 0.0f;
	else if (timeMs - L.look >= MC_LEE_MS) {
		L.look = timeMs;
		const float ux = -wind[0] / wl, uy = -wind[1] / wl;	// towards where the wind comes from
		static const float ray[5][3] = { { 0.0f, -14.0f, 0.25f }, { 0.0f, 4.0f, 0.35f }, { 0.0f, 26.0f, 0.2f }, { 0.436f, 4.0f, 0.1f }, { -0.436f, 4.0f, 0.1f } };	// turn, height, weight
		float sum = 0.0f;
		for (const auto& ry : ray) {
			const float c = cosf(ry[0]), s = sinf(ry[0]);
			const float from[3] = { origin[0], origin[1], origin[2] + ry[1] };
			const float to[3] = { from[0] + (ux * c - uy * s) * MC_LEE_REACH, from[1] + (ux * s + uy * c) * MC_LEE_REACH, from[2] };
			const float d = trace(from, to) * MC_LEE_REACH;
			float sh = d <= MC_LEE_FULL ? 1.0f : (d >= MC_LEE_REACH ? 0.0f : 1.0f - (d - MC_LEE_FULL) / (MC_LEE_REACH - MC_LEE_FULL));
			sh = sh * sh * (3.0f - 2.0f * sh);
			sum += ry[2] * sh;
		}
		L.target = sum;
	}
	if (fresh) L.now = L.target;
	else {
		const float dt = (float)(timeMs - L.ms) * 0.001f;
		if (L.target > L.now) { L.now += dt / MC_LEE_IN; if (L.now > L.target) L.now = L.target; }
		else { L.now -= dt / MC_LEE_OUT; if (L.now < L.target) L.now = L.target; }
	}
	L.ms = timeMs;
	return 1.0f - (1.0f - MC_LEE_KEEP) * L.now;
}

// SJE r_meshClothBlastGain: a blast wave throws light cloth far harder than the body it knocks back. At 1 an exploding
// crate (600 / 160) moved Vader's cape 3.5 units at 90 units off - his heavy cape's damping and spring swallowed the push
// while the game threw him away; at 3 it is blown 12 units (stretch as before). Set each frame by the glue.
static float g_blastGain = 3.0f;
void MC_SetBlastGain(float gain) {
	g_blastGain = gain > 0.0f ? gain : 0.0f;
}

void MC_Blast(const float center[3], float strength, float radius, mcVisibleFn_t visible) {
	strength *= g_blastGain;	// SJE r_meshClothBlastGain
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

// SJE r_meshClothStrongWind (see tr_meshcloth.h). Main thread only.
// A map's strong wind is a speed for its rain and snow (t1_rail's train roof: 5000 units/s): the cloth got all of it and
// was torn into strips. Over MC_STRONG_WIND (where the game braces its characters against the wind) it rises slowly with
// the map's speed, up to MC_STRONG_WIND_MAX: strongly blown from that side, still one sheet (the 0.5 gusts on top).
// Soaked cloth weighs twice as much and the air moves it half as far (t1_rail rains: its heavy cape hardly stirred):
// the strong wind on it is raised by its wetness, so it is blown as clearly as dry cloth - growing in from nothing at
// MC_STRONG_WIND to all of it at twice that, so a gusting wind that crosses MC_STRONG_WIND does not jump.
const float MC_STRONG_WIND_K = 0.15f;		// cloth wind per unit of map wind above MC_STRONG_WIND
const float MC_STRONG_WIND_MAX = 1600.0f;	// units/s at most (dry cloth)
float MC_StrongWindSpeed(float mapSpeed, float wet) {
	if (mapSpeed <= MC_STRONG_WIND) return mapSpeed;
	float v = MC_STRONG_WIND + MC_STRONG_WIND_K * (mapSpeed - MC_STRONG_WIND);
	if (v > MC_STRONG_WIND_MAX) v = MC_STRONG_WIND_MAX;
	return v * (1.0f + Clamp01(wet) * Clamp01((mapSpeed - MC_STRONG_WIND) / MC_STRONG_WIND));
}

struct mcEase_t {
	float	value = 0.0f;
	int		time = 0;
};
static std::unordered_map<const void*, mcEase_t> g_strongOut;

float MC_StrongWindOutside(const void* instanceKey, int timeMs, bool outside) {
	const float o = outside ? 1.0f : 0.0f;
	auto found = g_strongOut.find(instanceKey);
	if (found == g_strongOut.end()) {
		if (g_strongOut.size() > 512) {
			for (auto it = g_strongOut.begin(); it != g_strongOut.end(); ) {
				if (timeMs - it->second.time > MC_FORGET_MS || timeMs < it->second.time) it = g_strongOut.erase(it);
				else ++it;
			}
		}
		mcEase_t e;
		e.value = o;	// a new character starts where it is, no fade
		e.time = timeMs;
		found = g_strongOut.emplace(instanceKey, e).first;
	}
	mcEase_t& e = found->second;
	float dt = (timeMs - e.time) * 0.001f;
	if (dt < 0.0f || dt > 1.0f) dt = 1.0f;	// a reload or a long pause: catch up at once
	e.time = timeMs;
	const float os = dt / 0.4f;
	e.value = e.value < o ? (e.value + os > o ? o : e.value + os) : (e.value - os < o ? o : e.value - os);
	return e.value;
}

// SJE r_meshClothFans: wind zones (see tr_meshcloth.h). Main thread only.
struct mcZone_t {
	float	mins[3], maxs[3], air[3];
	float	ahead[3];	// how far the stream runs ahead of the push it gives (air - the push's velocity)
	bool	hasAhead;	// false: not told (the stream against the character's own motion is taken instead)
};
static std::vector<mcZone_t> g_zones;
struct mcZoneMem_t {
	float	rel[3] = { 0.0f, 0.0f, 0.0f };	// the air against its own motion when it was last in a zone
	int		left = -1;						// when it was last in one (-1: not yet)
	float	org[3] = { 0.0f, 0.0f, 0.0f };	// the middle of its box, last time
	float	vel[3] = { 0.0f, 0.0f, 0.0f };	// its velocity (from the last frames, smoothed)
	int		time = -1;						// ... when
};
static std::unordered_map<const void*, mcZoneMem_t> g_zoneMem;
const int MC_MAX_ZONES = 256;
const int MC_ZONE_HOLD_MS = 800;	// a character a pusher threw out of its zone keeps the stream this long (a jump pad's
const int MC_ZONE_FADE_MS = 600;	//   trigger is a thin slab: it is out of it the frame after the throw), then it fades out
const float MC_ZONE_SWEEP_SPEED = 3000.0f;	// units/s: a move between two frames further than this is a teleport

void MC_WindZoneAdd(const float mins[3], const float maxs[3], const float air[3], const float pushSpeed) {
	if ((int)g_zones.size() >= MC_MAX_ZONES) return;
	mcZone_t z;
	for (int k = 0; k < 3; k++) {
		z.mins[k] = mins[k] < maxs[k] ? mins[k] : maxs[k];
		z.maxs[k] = mins[k] < maxs[k] ? maxs[k] : mins[k];
		z.air[k] = air[k];
	}
	const float a = mcLen(z.air);
	z.hasAhead = pushSpeed >= 0.0f && a > 1e-3f;
	mcScale(z.air, z.hasAhead ? (a - (pushSpeed < a ? pushSpeed : a)) / a : 1.0f, z.ahead);
	g_zones.push_back(z);
}

void MC_WindZonesClear(void) {
	g_zones.clear();	// the characters' memory stays: one a pusher just threw keeps its stream when the set changes
}

int MC_WindZoneCount(void) {
	return (int)g_zones.size();
}

// does the segment from a to b (a box's middle; h: its half size) touch the zone? (slabs of the zone grown by h)
static bool MC_ZoneSwept(const mcZone_t& z, const float a[3], const float b[3], const float h[3]) {
	float t0 = 0.0f, t1 = 1.0f;
	for (int k = 0; k < 3; k++) {
		const float lo = z.mins[k] - h[k], hi = z.maxs[k] + h[k], d = b[k] - a[k];
		if (fabsf(d) < 1e-6f) {
			if (a[k] < lo || a[k] > hi) return false;
			continue;
		}
		float u0 = (lo - a[k]) / d, u1 = (hi - a[k]) / d;
		if (u0 > u1) { const float s = u0; u0 = u1; u1 = s; }
		if (u0 > t0) t0 = u0;
		if (u1 < t1) t1 = u1;
		if (t0 > t1) return false;
	}
	return true;
}

bool MC_ZoneAir(const void* instanceKey, int timeMs, const float mins[3], const float maxs[3], float air[3]) {
	air[0] = air[1] = air[2] = 0.0f;
	if (g_zones.empty() && g_zoneMem.empty()) return false;
	auto found = g_zoneMem.find(instanceKey);
	if (g_zones.empty()) {
		// no zones (now): only a character that a pusher just threw keeps its stream; the others are let go of
		if (found == g_zoneMem.end()) return false;
		if (found->second.left < 0 || timeMs < found->second.time || timeMs - found->second.left >= MC_ZONE_HOLD_MS + MC_ZONE_FADE_MS) {
			g_zoneMem.erase(found);
			return false;
		}
	}
	if (found == g_zoneMem.end()) {
		if (g_zoneMem.size() > 512) {
			for (auto it = g_zoneMem.begin(); it != g_zoneMem.end(); ) {
				if (timeMs - it->second.time > MC_FORGET_MS || timeMs < it->second.time) it = g_zoneMem.erase(it);
				else ++it;
			}
		}
		found = g_zoneMem.emplace(instanceKey, mcZoneMem_t()).first;
	}
	mcZoneMem_t& m = found->second;
	if (m.time >= 0 && timeMs < m.time) m = mcZoneMem_t();	// the time went back (a new map, a load): start over
	float c[3], h[3];
	for (int k = 0; k < 3; k++) {
		c[k] = 0.5f * (mins[k] + maxs[k]);
		h[k] = 0.5f * (maxs[k] - mins[k]);
	}
	// swept from where it was last frame: a jump pad's trigger is a thin slab, and a character thrown at 2000 units/s is
	// through it between two frames. Not across a teleport (further than it could have flown) or a long gap.
	const float* from = c;
	const int dtMs = timeMs - m.time;
	if (m.time >= 0 && dtMs > 0 && dtMs <= 200) {
		vec3_t dv;
		mcSub(c, m.org, dv);
		const float reach = MC_ZONE_SWEEP_SPEED * dtMs * 0.001f + 64.0f;
		if (mcDot(dv, dv) <= reach * reach) {
			from = m.org;
			mcScale(dv, 1000.0f / dtMs, dv);
			for (int k = 0; k < 3; k++) m.vel[k] = 0.5f * (m.vel[k] + dv[k]);
		}
		else {
			m.vel[0] = m.vel[1] = m.vel[2] = 0.0f;	// a teleport
		}
	}
	else if (dtMs != 0) {
		m.vel[0] = m.vel[1] = m.vel[2] = 0.0f;	// first seen, or after a gap
	}
	// the strongest of the zones it touches: pushers that overlap are one stream (a shaft has one pusher for the player
	// and one for the others, both blowing up it)
	bool in = false;
	float best = -1.0f;
	const mcZone_t* bestZone = nullptr;
	for (const mcZone_t& z : g_zones) {
		if (!MC_ZoneSwept(z, from, c, h)) continue;
		const float s = mcDot(z.air, z.air);
		if (s > best) {
			best = s;
			bestZone = &z;
			mcCopy(z.air, air);
		}
		in = true;
	}
	mcCopy(c, m.org);
	m.time = timeMs;
	if (in) {
		// how far the stream runs ahead of the push: as the cgame told it, else the stream against its own motion now
		// (which depends on when it leaves: a character that falls onto a pad and is out of it as soon as it is thrown
		// would keep the stream against its fall)
		if (bestZone && bestZone->hasAhead) mcCopy(bestZone->ahead, m.rel);
		else mcSub(air, m.vel, m.rel);
		m.left = timeMs;
		return true;
	}
	const int since = timeMs - m.left;
	if (m.left < 0 || since < 0 || since >= MC_ZONE_HOLD_MS + MC_ZONE_FADE_MS) {
		m.left = -1;
		return false;
	}
	// thrown out of it: the stream goes along with it, still running ahead of it by as much (whatever its flight does
	// to its speed), then fades out to still air
	float k = since <= MC_ZONE_HOLD_MS ? 1.0f : 1.0f - (float)(since - MC_ZONE_HOLD_MS) / (float)MC_ZONE_FADE_MS;
	k = k * k * (3.0f - 2.0f * k);
	mcAdd(m.vel, m.rel, air);
	mcScale(air, k, air);
	return true;
}

// SJE r_meshClothLevelWind: ext_data/meshcloth_wind.dat (see tr_meshcloth.h)
static void MC_BareMapName(const char* in, char* out, const int outSize) {
	const char* s = in;
	if (!Q_stricmpn(s, "maps/", 5) || !Q_stricmpn(s, "maps\\", 5)) s += 5;
	Q_strncpyz(out, s, outSize);
	const int n = (int)strlen(out);
	if (n > 4 && !Q_stricmp(out + n - 4, ".bsp")) out[n - 4] = 0;
}

void MC_LevelWindParse(const char* text, const char* mapName, mcLevelWind_t* out) {
	*out = mcLevelWind_t();
	if (!text || !mapName || !mapName[0]) return;
	char want[MAX_QPATH];
	MC_BareMapName(mapName, want, sizeof(want));
#ifdef MC_MP
	COM_BeginParseSession("meshcloth_wind");
#else
	COM_ParseSession session;	// the SP parser refuses to run outside a session
#endif
	const char* p = text;
	while (p) {
		const char* tok = COM_ParseExt(&p, qtrue);
		if (!tok[0]) break;
		if (tok[0] == '#') { while (COM_ParseExt(&p, qfalse)[0]) {} continue; }
		char name[MAX_QPATH];
		MC_BareMapName(tok, name, sizeof(name));
		mcLevelWind_t w;
		int k;
		for (k = 0; k < 3; k++) {
			tok = COM_ParseExt(&p, qfalse);
			if (!tok[0]) break;
			w.wind[k] = (float)atof(tok);
		}
		if (k < 3) continue;
		w.found = 1;
		while ((tok = COM_ParseExt(&p, qfalse))[0]) {
			if (!Q_stricmp(tok, "outside")) w.outside = 1;
			else if (!Q_stricmp(tok, "only")) w.only = 1;
			else if ((tok[0] >= '0' && tok[0] <= '9') || tok[0] == '.') w.gust = Clamp01((float)atof(tok));
		}
		if (!Q_stricmp(name, want)) *out = w;	// the last line for the map counts
	}
}

// MC_SetCharacters / MC_NearCharacters (r_meshClothOthers): other characters push cloth. Main thread.
static std::vector<float> g_charOrigins;
static std::vector<float> g_charScales;	// SJE r_meshClothShapeScale: the size each character is drawn at (empty = all 1)
static int g_charFrame = -1;

void MC_SetCharacters(int frameId, const float (*origins)[3], int count, const float* scales) {
	if (scales && count > 0) g_charScales.assign(scales, scales + count);
	else g_charScales.clear();
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
		// SJE r_meshClothShapeScale: the column follows the other's drawn size (Yoda at 45 % stood as a full-size column)
		const float s = (int)g_charScales.size() == count ? g_charScales[i] : 1.0f;
		mcCapsule_t& c = out[n++];
		c.a[0] = o[0]; c.a[1] = o[1]; c.a[2] = o[2] - 12.0f * s;
		c.b[0] = o[0]; c.b[1] = o[1]; c.b[2] = o[2] + 28.0f * s;
		c.r = 12.0f * s;
		c.hard = 0;
	}
	return n;
}