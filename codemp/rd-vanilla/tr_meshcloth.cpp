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

#define MC_MP
#include "qcommon/q_shared.h"
#define MDXABONEDEF
#include "rd-common/mdx_format.h"
#include "tr_meshcloth.h"

#include <vector>
#include <unordered_map>
#include <string>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <algorithm>

#ifdef MC_DEBUG
#include <cstdlib>
int g_mcDbgTether = 0;
#define MCDBG_OFF( n ) ( getenv( n ) != nullptr )
#else
#define MCDBG_OFF( n ) false
#endif
namespace {
	const int	MC_STEP_MS = 16;
	const float	MC_DT = 0.016f;
	const int	MC_MAX_STEPS = 2;		// SJE: was 4. A slow frame ran up to 4 steps, which made the next frame slower still
	// (frame rate collapsed with heavy capes). Past 2 steps the cloth just runs a little slow.
	const int	MC_STALE_MS = 250;
	const float	MC_TELEPORT = 120.0f;
	const int	MC_FORGET_MS = 10000;
	const float	MC_MARGIN = 0.6f;
	const float	MC_WELD = 0.01f;
	const int	MC_SETTLE_STEPS = 40;
	const float	MC_DEFAULT_SPIN = 0.0f;		// degrees/s, 0 = no spin limit unless the config sets one		// 0.64 s of settling when a character appears	// bind-pose distance that counts as the same point

	enum mcRuleType_t { MC_RULE_ABOVE, MC_RULE_NEAR, MC_RULE_SEGMENT };

	struct mcDef_t {
		std::string		model = "", surface = "";
		mcRuleType_t	rule = MC_RULE_ABOVE;
		float			a[8] = { 0 };
		float			tether = 0.0f;
		float			shape = 0.0f;
		float			bend = 0.0f;		// 0..1 bending stiffness (0 = limp silk, 1 = stiff leather)
		float			inertia = 0.0f;	// 0..1, or -1 to use r_meshClothInertia
		bool			self = false;		// self-collision (single-layer capes)
		float			smooth = 0.0f;		// 0..1 crease smoothing of the drawn shape
		float			maxAccel = 0.0f;	// units/s^2 of character acceleration the cloth feels, 0 = no limit, -1 = cvar
		float			maxSpin = 0.0f;	// degrees/s of the attached edge's turning the cloth feels, 0 = no limit, -1 = default
		float			spread = 0.0f;		// 0..1 sideways-only pull toward the animated shape (keeps a cape from roping)
		float			thickness = 0.0f;	// cloth thickness in units: keeps it this far off the body (half) and off itself (full)
		float			weight = 0.0f;		// relative mass per area: 1 = normal cloth, 2 = heavy wool/leather (air moves it half as much), 0.5 = silk
	};

	struct mcGroup_t {
		std::vector<const mdxmSurface_t*>	surfs = {};
		std::vector<const mcDef_t*>		defs = {};
		std::vector<int>					vertBase = {};		// first global vertex of each surface
		int									numVerts = 0;
		int									numParticles = 0;
		std::vector<int>					vertToParticle = {};	// global vertex -> particle
		std::vector<int>					partSurf = {}, partVert = {};	// representative surface / local vertex
		std::vector<float>					freedom = {}, tether = {}, shape = {}, bend = {}, inertia = {};	// per particle
		std::vector<char>					selfCol = {};		// per particle: takes part in self-collision
		std::vector<float>					smooth = {};			// per particle: crease smoothing of the drawn shape
		std::vector<float>					maxAccel = {};		// per particle
		std::vector<float>					maxSpin = {};		// per particle
		std::vector<float>					spread = {};		// per particle
		std::vector<float>					weight = {};		// per particle
		std::vector<float>					thick = {};		// per particle: cloth thickness
		float								selfThick = 1.2f;	// self-collision distance (largest thickness of the self-colliding surfaces)
		std::vector<char>					hangs = {};			// per particle: hanging cloth ("above" rule: capes, robes, skirts)
		std::vector<char>					hardCol = {};		// per particle: arms/hands always push it out (not for sleeves, which wrap the arms)
		bool								anySmooth = false;
		bool								anySelf = false;
		std::vector<int>					lraAnchor = {};		// nearest attached particle along the cloth (-1 none)
		std::vector<float>					lraLen = {};			// bind-pose distance to it along the cloth
		float								bindLinkSum = 0.0f;	// total bind length of the structural links
		std::vector<float>					bindPos = {};		// 3 per particle, model bind pose
		std::vector<int>					linkA = {}, linkB = {};
		std::vector<char>					linkBend = {};
		std::vector<int>					tris = {};			// particle indices
	};

	struct mcSurfEntry_t {
		mcGroup_t* group = nullptr;			// nullptr = not cloth
		int			surfIndex = -1;		// index into group->surfs
		int			numVerts = 0, numTris = 0, index = 0;	// to notice a reused address after a model reload
	};

	struct mcInstance_t {
		const mcGroup_t* group = nullptr;
		bool				active = false;
		int					lastTime = 0, lastSeen = 0, accum = 0, outTime = 0;
		float				linkScale = 1.0f;		// animated / bind size of the cloth (model scale)
		vec3_t				lastOrigin = {};		// character origin at the previous simulation
		vec3_t				originVel = {};		// its velocity (units/s)
		vec3_t				up = {};				// the character's up direction (world)
		vec3_t				side = {};			// the character's left direction (world)
		std::vector<float>	pinPrev = {};		// attached particles' animated positions last frame
		int					pinTime = 0;
		int					originTime = 0;
		std::vector<float>	p = {}, prev = {}, target = {}, targetPrev = {};	// 3 per particle, world
		std::vector<float>	simTarget = {};		// animated pose at the simulation's own clock (it trails the frame by 'accum')
		std::vector<float>	outLocal = {}, nrmLocal = {};				// 3 per particle, model space
		std::vector<float>	skinNrm = {};						// 3 per global vertex, model space
		std::vector<float>	scratchTarget = {}, scratchFrom = {}, scratchTo = {}, scratchN = {};
		std::vector<float>	push = {}, contactN = {}, targetSub = {};	// per particle: this substep's body push, contact normal, previous substep target
		std::vector<char>	contact = {};
		std::vector<float>	scratchTarget2 = {}, scratchCnt = {};
		std::vector<float>	impulse = {};		// pending velocity kicks (units/s), e.g. from explosions
		std::vector<float>	offPrev = {}, offCur = {};	// cloth offset from the animation after the last two steps (for drawing between steps)
		std::vector<float>	pPrevStep = {}, pCurStep = {};	// world positions after the last two steps
		vec3_t				oPrevStep = {}, oCurStep = {};	// character origin at those two steps
	};

	std::vector<mcDef_t>										g_defs;
	std::unordered_map<const mdxmSurface_t*, mcSurfEntry_t>	g_surfs;
	std::vector<mcGroup_t*>									g_groups;
	std::unordered_map<unsigned long long, mcInstance_t*>		g_instances;
	int															g_lastPrune;

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
			if ((int)pts.size() <= id) pts.resize(static_cast<std::vector<const float*, std::allocator<const float*>>::size_type>(id) + 1);
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
				g->vertToParticle[g->vertBase[si] + static_cast<std::vector<int, std::allocator<int>>::size_type>(i)] = p;
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
				int pi[3]{};
				for (int k = 0; k < 3; k++) {
					pi[k] = g->vertToParticle[static_cast<std::vector<int, std::allocator<int>>::size_type>(g->vertBase[si] + tri[t * 3 + k])];
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

		// long range attachments (Kim, Chentanez & Mueller 2012): each free
		// particle's nearest attached particle measured along the cloth, so the
		// solver can forbid it from ever getting further away than that
		std::vector<const float*> bindPos(g->numParticles);
		g->bindPos.resize((size_t)g->numParticles * 3);
		for (int p = 0; p < g->numParticles; p++) {
			bindPos[p] = Verts(g->surfs[g->partSurf[p]])[g->partVert[p]].vertCoords;
			mcCopy(bindPos[p], &g->bindPos[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
		}
		std::vector<std::vector<std::pair<int, float>>> adj(g->numParticles);
		g->bindLinkSum = 0.0f;
		for (size_t l = 0; l < g->linkA.size(); l++) {
			if (g->linkBend[l]) continue;
			vec3_t d;
			mcSub(bindPos[g->linkA[l]], bindPos[g->linkB[l]], d);
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
		return g;
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
	bool Collide(float* p, const float* target, const mcCapsule_t* caps, int n, float* nOut, bool useHard, float margin) {
		bool hit = false;
		for (int c = 0; c < n; c++) {
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
			}
			else if (d < minD + 0.25f) {
				// resting on the body: still in contact for friction
				mcScale(out, 1.0f / d, nOut);
				hit = true;
			}
		}
		return hit;
	}

	void ParticleNormals(const mcGroup_t* g, const std::vector<float>& pos, std::vector<float>& n) {
		n.assign((size_t)g->numParticles * 3, 0.0f);
		for (size_t t = 0; t + 2 < g->tris.size(); t += 3) {
			const float* a = &pos[static_cast<std::vector<float, std::allocator<float>>::size_type>(g->tris[static_cast<std::vector<int, std::allocator<int>>::size_type>(t)] * 3)], * b = &pos[static_cast<std::vector<float, std::allocator<float>>::size_type>(g->tris[static_cast<std::vector<int, std::allocator<int>>::size_type>(t + 1)] * 3)], * c = &pos[static_cast<std::vector<float, std::allocator<float>>::size_type>(g->tris[static_cast<std::vector<int, std::allocator<int>>::size_type>(t + 2)] * 3)];
			vec3_t e1, e2, fn;
			mcSub(b, a, e1);
			mcSub(c, a, e2);
			mcCross(e1, e2, fn);
			for (int k = 0; k < 3; k++) {
				float* d = &n[static_cast<std::vector<float, std::allocator<float>>::size_type>(g->tris[static_cast<std::vector<int, std::allocator<int>>::size_type>(t + k)] * 3)];
				mcAdd(d, fn, d);
			}
		}
		for (int p = 0; p < g->numParticles; p++) mcNorm(&n[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
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
				float v = target[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3 + k];
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

	// One pass over every constraint, Gauss-Seidel. bendK is the per-substep
	// fraction of the bend error removed (stiffness made independent of the
	// substep count by the caller).
	void SolveConstraints(const mcGroup_t* g, mcInstance_t* in, const mcCapsule_t* caps, int ncaps, float bendScale) {
		if ((int)in->contact.size() != g->numParticles) {
			in->contact.assign(g->numParticles, 0);
			in->push.assign((size_t)g->numParticles * 3, 0.0f);
			in->contactN.assign((size_t)g->numParticles * 3, 0.0f);
		}
		// attached vertices first: everything below measures against them, and
		// a vertex solved before its attachment point must not see where that
		// point was (on a jump that's a whole frame of movement lower)
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] <= 0.0f) mcCopy(&in->target[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], &in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
		}
		// stretch: the fabric keeps the edge lengths the animated mesh has now
		// (rigidly - real cloth barely stretches); bend links resist folding
		for (size_t l = 0; l < g->linkA.size(); l++) {
			const int a = g->linkA[l], b = g->linkB[l];
			const float wa = g->freedom[a] > 0.0f ? 1.0f : 0.0f, wb = g->freedom[b] > 0.0f ? 1.0f : 0.0f;
			if (wa + wb == 0.0f) continue;
			float* pa = &in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(a) * 3], * pb = &in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(b) * 3];
			vec3_t dT, d;
			mcSub(&in->target[static_cast<std::vector<float, std::allocator<float>>::size_type>(b) * 3], &in->target[static_cast<std::vector<float, std::allocator<float>>::size_type>(a) * 3], dT);
			const float rest = mcLen(dT);
			mcSub(pb, pa, d);
			const float len = mcLen(d);
			if (len < 1e-5f) continue;
			float k = (len - rest) / (len * (wa + wb));
			if (g->linkBend[l]) {
				const float bs = 0.5f * (g->bend[a] + g->bend[b]);
				if (bs <= 0.0f || bendScale <= 0.0f) continue;
				// the fraction of the fold removed per substep, so that one
				// 16 ms step removes 'bend' of it whatever the substep count
				k *= 1.0f - powf(1.0f - (bs < 0.99f ? bs : 0.99f), bendScale);
			}
			mcMA(pa, k * wa, d, pa);
			mcMA(pb, -k * wb, d, pb);
		}
		for (int p = 0; p < g->numParticles; p++) {
			const float f = g->freedom[p];
			float* pp = &in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3];
			const float* t = &in->target[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3];
			if (f <= 0.0f) { mcCopy(t, pp); continue; }
			// long range attachment: never further from the attached edge than
			// the cloth itself is long
			const int an = g->lraAnchor[p];
			if (an >= 0 && !MCDBG_OFF("NOLRA")) {
				vec3_t d;
				mcSub(pp, &in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(an) * 3], d);
				// skinning can stretch the animated mesh past its bind size
				// (raised shoulders): never limit tighter than the pose itself
				vec3_t dt;
				mcSub(t, &in->target[static_cast<std::vector<float, std::allocator<float>>::size_type>(an) * 3], dt);
				float maxLen = g->lraLen[p] * in->linkScale;
				const float animLen = mcLen(dt);
				if (animLen > maxLen) maxLen = animLen;
				maxLen *= 1.03f;
				const float len = mcLen(d);
				if (len > maxLen && len > 1e-5f) mcMA(&in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(an) * 3], maxLen / len, d, pp);
			}
			// never higher than both the attached edge it hangs from and its
			// animated spot: stops a fast turn or a spawn from flipping a cape
			// up over the shoulders
			if (an >= 0 && !MCDBG_OFF("NOHEIGHT")) {
				const float hp = mcDot(pp, in->up), ha = mcDot(&in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(an) * 3], in->up), ht = mcDot(t, in->up);
				const float lim = (ha > ht ? ha : ht) + 2.0f * in->linkScale;
				if (hp > lim) mcMA(pp, lim - hp, in->up, pp);
			}
			vec3_t before;
			mcCopy(pp, before);
			if (Collide(pp, t, caps, ncaps, &in->contactN[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], g->hardCol[static_cast<std::vector<float, std::allocator<float>>::size_type>(p)] != 0 && f > 0.5f,
				(MC_MARGIN > 0.5f * g->thick[static_cast<std::vector<float, std::allocator<float>>::size_type>(p)] ? MC_MARGIN : 0.5f * g->thick[static_cast<std::vector<float, std::allocator<float>>::size_type>(p)]))) {
				in->contact[static_cast<std::vector<float, std::allocator<float>>::size_type>(p)] = 1;
				vec3_t pu;
				mcSub(pp, before, pu);
				mcAdd(&in->push[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], pu, &in->push[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
			}
			// max distance from the animated position. Soft: past the limit the
			// cloth is reeled back in over ~100 ms instead of being projected
			// onto it. A hard limit tied to the animated pose made the cape jump
			// instantly whenever the animation changed pose (landing from a jump,
			// starting a saber move) while the cape was out at the limit, e.g.
			// lifted up during a fall. A hard stop remains at twice the limit.
			vec3_t d;
			mcSub(pp, t, d);
			const float len = mcLen(d), maxLen = f * g->tether[p];
			if (len > maxLen && len > 1e-5f && !MCDBG_OFF("NOTETHER")) {
				const float k = 1.0f - powf(0.85f, bendScale > 0.0f ? bendScale * 6.0f : 6.0f);	// ~15% per 16/6 ms, frame-rate independent
				float target = len - (len - maxLen) * k;
				if (target > 2.0f * maxLen) target = 2.0f * maxLen;
				mcMA(t, target / len, d, pp);
#ifdef MC_DEBUG
				g_mcDbgTether++;
#endif
			}
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
	}

	// Velocity pass after a substep's constraints, for vertices touching the
	// body (Macklin et al. 2014 / Mueller et al. 2020): Verlet turns every
	// position correction into speed, so a leg pushing the cape out would
	// throw it. The push is kept as a move but mostly not as speed, and the
	// sliding speed against the moving body is cut by friction.
	void ContactVelocity(const mcGroup_t* g, mcInstance_t* in) {
		const float keepPush = 0.2f;	// part of the push that may become speed
		const float friction = 0.3f;	// tangential speed removed per substep in contact
		for (int p = 0; p < g->numParticles; p++) {
			if (!in->contact[p] || g->freedom[p] <= 0.0f) continue;
			float* pp = &in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], * pv = &in->prev[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3];
			mcMA(pv, 1.0f - keepPush, &in->push[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], pv);
			// speed relative to the body surface (the animated vertex's motion)
			vec3_t v, vb, vr, vt;
			mcSub(pp, pv, v);
			mcSub(&in->target[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], &in->targetSub[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], vb);
			mcSub(v, vb, vr);
			const float* n = &in->contactN[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3];
			const float vn = mcDot(vr, n);
			mcMA(vr, -vn, n, vt);
			mcMA(pv, friction, vt, pv);
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
		static std::vector<cellEntry_t> cells;
		cells.clear();
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] <= 0.0f || !g->selfCol[p]) continue;
			const float* v = &in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3];
			cells.push_back({ key(cellOf(v, 0), cellOf(v, 1), cellOf(v, 2)), p });
		}
		std::sort(cells.begin(), cells.end(), [](const cellEntry_t& x, const cellEntry_t& y) { return x.key < y.key; });
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] <= 0.0f || !g->selfCol[p]) continue;
			float* a = &in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3];
			const long long cx = cellOf(a, 0), cy = cellOf(a, 1), cz = cellOf(a, 2);
			for (long long dx = -1; dx <= 1; dx++) for (long long dy = -1; dy <= 1; dy++) for (long long dz = -1; dz <= 1; dz++) {
				const long long k = key(cx + dx, cy + dy, cz + dz);
				auto it = std::lower_bound(cells.begin(), cells.end(), k, [](const cellEntry_t& e, long long v) { return e.key < v; });
				for (; it != cells.end() && it->key == k; ++it) {
					const int q = it->p;
					if (q <= p) continue;
					vec3_t bd;
					mcSub(&g->bindPos[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], &g->bindPos[static_cast<std::vector<float, std::allocator<float>>::size_type>(q) * 3], bd);
					const float bindDist = mcLen(bd) * in->linkScale;
					if (bindDist < 2.5f * thick) continue;	// neighbours in the cloth
					float* b = &in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(q) * 3];
					vec3_t d;
					mcSub(a, b, d);
					const float len = mcLen(d);
					if (len >= thick || len < 1e-5f) continue;
					const float corr = 0.5f * (thick - len) / len;
					mcMA(a, corr, d, a);
					mcMA(b, -corr, d, b);
					// a separation, not a throw
					mcMA(&in->prev[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], corr, d, &in->prev[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
					mcMA(&in->prev[static_cast<std::vector<float, std::allocator<float>>::size_type>(q) * 3], -corr, d, &in->prev[static_cast<std::vector<float, std::allocator<float>>::size_type>(q) * 3]);
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
		for (int p = 0; p < g->numParticles; p++) if (g->freedom[p] <= 0.0f) { mcAdd(c, &target[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], c); n++; }
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
			mcSub(&target[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], c, d);
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
	void SpinLimit(const mcGroup_t* g, mcInstance_t* in, const std::vector<float>& target, int timeMs, float defSpin) {
		const size_t n3 = (size_t)g->numParticles * 3;
		const int dtMs = timeMs - in->pinTime;
		if (in->pinPrev.size() != n3 || dtMs <= 0 || dtMs > MC_STALE_MS) {
			in->pinPrev.assign(target.begin(), target.end());
			in->pinTime = timeMs;
			return;
		}
		const float* up = in->up;
		// centroid of the attached particles, then their mean turn about it
		vec3_t c0 = { 0, 0, 0 }, c1 = { 0, 0, 0 };
		int np = 0;
		for (int p = 0; p < g->numParticles; p++) {
			if (g->freedom[p] > 0.0f) continue;
			mcAdd(c0, &in->pinPrev[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], c0);
			mcAdd(c1, &target[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], c1);
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
				mcSub(&in->pinPrev[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], c0, a);
				mcSub(&target[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], c1, b);
				mcMA(a, -mcDot(a, up), up, a);
				mcMA(b, -mcDot(b, up), up, b);
				mcCross(a, b, x);
				sx += mcDot(x, up);
				sy += mcDot(a, b);
			}
			angle = atan2f(sx, sy);	// radians this frame, about up
		}
		in->pinPrev.assign(target.begin(), target.end());
		in->pinTime = timeMs;
		if (angle == 0.0f) return;
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

	// One 16 ms step, split into substeps ("Small Steps in Physics
	// Simulation", Macklin et al. 2019): many short steps with a single
	// constraint pass each keep the fabric from stretching or wobbling far
	// better than one long step with many passes.
	void Step(const mcGroup_t* g, mcInstance_t* in, const std::vector<float>& tgtFrom, const std::vector<float>& tgtTo,
		const mcCapsule_t* caps, int ncaps, const mcParams_t* prm) {
		const int sub = prm->iterations < 1 ? 1 : (prm->iterations > 16 ? 16 : prm->iterations);
		const float h = MC_DT / sub;
		const float damp = powf(0.99f, 1.0f / sub);			// 1% velocity loss per 16 ms
		// bend stiffness per substep so the total doesn't depend on the count
		const float bendScale = 1.0f / sub;	// exponent: k_sub = 1 - (1 - bend)^(1/sub)
		std::vector<float>& n = in->scratchN;
		// wind with slow gusts: two incommensurate sines, so it never repeats visibly
		vec3_t windNow;
		{
			const float t = prm->timeSec;
			const float gs = 1.0f + prm->gust * (0.6f * sinf(t * 1.3f) + 0.4f * sinf(t * 3.7f + 1.1f));
			mcScale(prm->wind, gs, windNow);
		}

		// the cloth's current size against its bind pose (model scale)
		float animSum = 0.0f;
		for (size_t l = 0; l < g->linkA.size(); l++) {
			if (g->linkBend[l]) continue;
			vec3_t d;
			mcSub(&tgtTo[static_cast<std::vector<float, std::allocator<float>>::size_type>(g->linkB[l]) * 3], &tgtTo[static_cast<std::vector<float, std::allocator<float>>::size_type>(g->linkA[l]) * 3], d);
			animSum += mcLen(d);
		}
		in->linkScale = g->bindLinkSum > 1e-3f ? animSum / g->bindLinkSum : 1.0f;

		in->target.resize(tgtTo.size());
		in->push.resize(tgtTo.size());
		in->contactN.resize(tgtTo.size());
		in->contact.resize(g->numParticles);
		in->targetSub.assign(tgtFrom.begin(), tgtFrom.end());
		for (int s = 0; s < sub; s++) {
			const float t1 = (float)(s + 1) / sub;
			if (s > 0) in->targetSub.assign(in->target.begin(), in->target.end());
			for (size_t i = 0; i < tgtTo.size(); i++) in->target[i] = tgtFrom[i] + (tgtTo[i] - tgtFrom[i]) * t1;
			std::fill(in->push.begin(), in->push.end(), 0.0f);
			std::fill(in->contact.begin(), in->contact.end(), 0);
			ParticleNormals(g, in->p, n);
			for (int p = 0; p < g->numParticles; p++) {
				const float f = g->freedom[p];
				float* pp = &in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], * pv = &in->prev[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3];
				if (f <= 0.0f) {
					mcCopy(pp, pv);
					mcCopy(&in->target[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], pp);
					continue;
				}
				vec3_t v;
				mcSub(pp, pv, v);
				mcScale(v, damp, v);
				if (s == 0 && in->impulse.size() == in->p.size()) mcMA(v, h, &in->impulse[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], v);
				// Air. The force comes from the cloth's motion RELATIVE TO THE AIR
				// (its own world velocity minus the wind), split in two:
				//  - through the cloth (along its normal): the strong "sail" drag
				//    that makes capes billow,
				//  - along the cloth: weaker skin drag, which is what lifts a cape
				//    when its wearer falls and streams it back when he runs.
				// Both are divided by the cloth's weight (mass per area): the same
				// air moves a heavy wool cloak less than a silk cape. Gravity is
				// not divided, since every mass falls alike.
				{
					const float invW = 1.0f / g->weight[p];
					vec3_t rel;
					mcMA(v, -h, windNow, rel);		// v is per substep, wind is per second
					const float* nn = &n[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3];
					const float vn = mcDot(rel, nn);
					float kn = prm->drag * h * invW;
					if (kn > 1.0f) kn = 1.0f;
					float kt = prm->airDrag * h * invW;
					if (kt > 0.5f) kt = 0.5f;
					vec3_t relT;
					mcMA(rel, -vn, nn, relT);
					mcMA(v, -vn * kn, nn, v);
					mcMA(v, -kt, relT, v);
				}
				mcCopy(pp, pv);
				mcAdd(pp, v, pp);
				pp[2] -= prm->gravity * h * h;
				// sideways-only pull toward the animated shape: stops a cape
				// rolling up into a rope in saber spins, while it still trails,
				// swings and lifts freely front to back and up and down
				const float sp = g->spread[p];
				if (sp > 0.0f) {
					const float k = 1.0f - powf(1.0f - Clamp01(sp), 1.0f / sub);
					vec3_t d;
					mcSub(&in->target[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], pp, d);
					mcMA(pp, k * mcDot(d, in->side), in->side, pp);
				}
				// optional pull toward the modelled shape (off unless configured)
				const float sh = g->shape[static_cast<std::vector<float, std::allocator<float>>::size_type>(p)] * prm->shapeScale;
				if (sh > 0.0f) {
					const float k = 1.0f - powf(1.0f - Clamp01(sh), 1.0f / sub);
					vec3_t d;
					mcSub(&in->target[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], pp, d);
					mcMA(pp, k, d, pp);
				}
			}
			SolveConstraints(g, in, caps, ncaps, bendScale);
			ContactVelocity(g, in);
		}
		in->impulse.clear();
		SelfCollide(g, in);
	}

	void Simulate(const mcGroup_t* g, mcInstance_t* in, int timeMs, const vec3_t origin, const vec3_t axis[3],
		const mcCapsule_t* caps, int ncaps, const mcParams_t* prm, mcBoneFn_t boneFn, void* boneCtx) {
		// animated (target) positions of every particle, and skinned normals of every vertex
		std::vector<float>& target = in->scratchTarget;
		target.resize((size_t)g->numParticles * 3);
		in->skinNrm.resize((size_t)g->numVerts * 3);
		static std::vector<char> done;		// SJE: reused - was a new allocation every frame
		done.assign(g->numParticles, 0);
		for (size_t si = 0; si < g->surfs.size(); si++) {
			const mdxmSurface_t* s = g->surfs[si];
			for (int i = 0; i < s->numVerts; i++) {
				const int gv = g->vertBase[si] + i, p = g->vertToParticle[gv];
				vec3_t pos;
				SkinVertex(s, i, boneFn, boneCtx, pos, &in->skinNrm[static_cast<std::vector<float, std::allocator<float>>::size_type>(gv) * 3]);
				if (!done[p]) { ToWorld(pos, origin, axis, &target[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]); done[p] = 1; }
			}
		}

		if (in->active) {
			const int dt = timeMs - in->lastTime;
			vec3_t d;
			mcSub(&target[0], &in->targetPrev[0], d);
			if (dt < 0 || dt > MC_STALE_MS || mcLen(d) > MC_TELEPORT) in->active = false;
		}
		mcCapsule_t nearCaps[32];
		const int nnear = CullCapsules(g, target, caps, ncaps, nearCaps, 32);
		if (!in->active) {
			in->target.assign(target.begin(), target.end());
			Reset(in, timeMs);
			mcCopy(origin, in->lastOrigin);
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
					if (mcDot(shift, shift) == 0.0f) continue;
					// the attached edge is fed in substep by substep from last
					// frame's animated position: move that reference by the same
					// amount, or a jump take-off puts the cloth ahead of its own
					// attachment for a moment and yanks it
					mcAdd(&in->targetPrev[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], shift, &in->targetPrev[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
					if (in->simTarget.size() == in->targetPrev.size()) mcAdd(&in->simTarget[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], shift, &in->simTarget[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
					if (g->freedom[p] <= 0.0f) continue;
					mcAdd(&in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], shift, &in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
					if (in->pCurStep.size() == in->p.size()) {
						mcAdd(&in->pCurStep[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], shift, &in->pCurStep[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
						mcAdd(&in->pPrevStep[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], shift, &in->pPrevStep[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
					}
					mcAdd(&in->prev[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], shift, &in->prev[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
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
			SpinLimit(g, in, target, timeMs, MC_DEFAULT_SPIN);
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
				for (int s = 0; s < steps; s++) {
					// each step ends at a fixed 16 ms tick of the simulation clock;
					// the animated pose is sampled at that exact time, so the cloth
					// sees the body's true speed whatever the frame timing
					float t1 = clamped ? (float)(s + 1) / steps : (float)(MC_STEP_MS * (s + 1) - accumOld) / dt;
					if (t1 > 1.0f) t1 = 1.0f; else if (t1 < 0.0f) t1 = 0.0f;
					from.swap(to);
					to.resize(target.size());
					for (size_t i = 0; i < target.size(); i++) to[i] = in->targetPrev[i] + (target[i] - in->targetPrev[i]) * t1;
					Step(g, in, from, to, nearCaps, nnear, prm);
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
				in->targetPrev.assign(target.begin(), target.end());
				in->lastTime = timeMs;
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
			vec3_t oNow{};
			for (int k = 0; k < 3; k++) oNow[k] = origin[k] - (in->oPrevStep[k] + (in->oCurStep[k] - in->oPrevStep[k]) * a);
			for (int p = 0; p < g->numParticles; p++) {
				float w = g->freedom[p] <= 0.0f ? 0.0f : (g->freedom[p] >= 1.0f ? 1.0f : g->freedom[p]);
				if (!g->hangs[p] || MCDBG_OFF("OFFDRAW")) w = 0.0f;	// sleeves and hoods follow their limb exactly
				for (int k = 0; k < 3; k++) {
					const size_t i = static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3 + k;
					if (!ok) { off[i] = in->p[i] - target[i]; continue; }
					const float offA = in->offPrev[i] + (in->offCur[i] - in->offPrev[i]) * a;
					const float world = in->pPrevStep[i] + (in->pCurStep[i] - in->pPrevStep[i]) * a + oNow[k];
					off[i] = (1.0f - w) * offA + w * (world - target[i]);
				}
				if (g->freedom[p] <= 0.0f) off[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3] = off[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3 + 1] = off[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3 + 2] = 0.0f;
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
					mcAdd(&acc[static_cast<std::vector<float, std::allocator<float>>::size_type>(a) * 3], &off[static_cast<std::vector<float, std::allocator<float>>::size_type>(b) * 3], &acc[static_cast<std::vector<float, std::allocator<float>>::size_type>(a) * 3]); cnt[a] += 1.0f;
					mcAdd(&acc[static_cast<std::vector<float, std::allocator<float>>::size_type>(b) * 3], &off[static_cast<std::vector<float, std::allocator<float>>::size_type>(a) * 3], &acc[static_cast<std::vector<float, std::allocator<float>>::size_type>(b) * 3]); cnt[b] += 1.0f;
				}
				for (int p = 0; p < g->numParticles; p++) {
					if (g->freedom[p] <= 0.0f || cnt[static_cast<std::vector<float, std::allocator<float>>::size_type>(p)] == 0.0f) continue;
					const float w = g->smooth[p] * (g->freedom[p] < 1.0f ? g->freedom[p] : 1.0f);
					for (int k = 0; k < 3; k++) off[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3 + k] += w * (acc[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3 + k] / cnt[static_cast<std::vector<float, std::allocator<float>>::size_type>(p)] - off[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3 + k]);
				}
			}
		}
		for (size_t i = 0; i < draw.size(); i++) draw[i] = target[i] + off[i];
		// safety net: a degenerate pose (zero-scale bones, a broken model) must
		// never put NaNs on screen or keep them in the state - fall back to the
		// animation and start the cloth over next frame
		for (size_t i = 0; i < draw.size(); i++) {
			if (!std::isfinite(draw[i]) || !std::isfinite(in->p[i])) {
				draw.assign(target.begin(), target.end());
				in->active = false;
				break;
			}
		}

		// results back into model space, with normals from the moving triangles
		std::vector<float>& nw = in->scratchN;
		ParticleNormals(g, draw, nw);
		in->outLocal.resize((size_t)g->numParticles * 3);
		in->nrmLocal.resize((size_t)g->numParticles * 3);
		for (int p = 0; p < g->numParticles; p++) {
			ToLocal(&draw[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], origin, axis, &in->outLocal[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
			DirToLocal(&nw[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], axis, &in->nrmLocal[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
			mcNorm(&in->nrmLocal[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
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
		d.weight = 1.0f;
		d.thickness = 1.2f;
		while (1) {
			tok = COM_ParseExt(&p, qfalse);
			if (!tok[0]) break;
			if (!Q_stricmp(tok, "tether")) { d.tether = (float)atof(COM_ParseExt(&p, qfalse)); if (d.tether < 0.0f) d.tether = 0.0f; }
			else if (!Q_stricmp(tok, "thickness")) { d.thickness = (float)atof(COM_ParseExt(&p, qfalse)); if (d.thickness < 0.0f) d.thickness = 0.0f; if (d.thickness > 8.0f) d.thickness = 8.0f; }
			else if (!Q_stricmp(tok, "shape")) d.shape = (float)atof(COM_ParseExt(&p, qfalse));
			else if (!Q_stricmp(tok, "self")) d.self = true;
			else if (!Q_stricmp(tok, "weight")) { d.weight = (float)atof(COM_ParseExt(&p, qfalse)); if (d.weight < 0.1f) d.weight = 0.1f; if (d.weight > 10.0f) d.weight = 10.0f; }
			else if (!Q_stricmp(tok, "spread")) d.spread = Clamp01((float)atof(COM_ParseExt(&p, qfalse)));
			else if (!Q_stricmp(tok, "maxspin")) { d.maxSpin = (float)atof(COM_ParseExt(&p, qfalse)); if (d.maxSpin < 0.0f) d.maxSpin = 0.0f; }
			else if (!Q_stricmp(tok, "maxaccel")) { d.maxAccel = (float)atof(COM_ParseExt(&p, qfalse)); if (d.maxAccel < 0.0f) d.maxAccel = 0.0f; }
			else if (!Q_stricmp(tok, "smooth")) d.smooth = Clamp01((float)atof(COM_ParseExt(&p, qfalse)));
			else if (!Q_stricmp(tok, "inertia")) d.inertia = Clamp01((float)atof(COM_ParseExt(&p, qfalse)));
			else if (!Q_stricmp(tok, "bend")) d.bend = Clamp01((float)atof(COM_ParseExt(&p, qfalse)));
		}
		if (d.tether < 0.0f) d.tether = 0.0f;
		g_defs.push_back(d);
	}
	return (int)g_defs.size();
}

void MC_Clear(void) {
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

void MC_Apply(const void* surfacePtr, const void* instanceKey, int timeMs,
	const vec3_t origin, const vec3_t axis[3],
	const mcCapsule_t* caps, int ncaps, const mcParams_t* prm,
	mcBoneFn_t boneFn, void* boneCtx,
	float (*xyz)[4], float (*normal)[4]) {
	const mdxmSurface_t* surface = static_cast<const mdxmSurface_t*>(surfacePtr);
	auto sit = g_surfs.find(surface);
	if (sit == g_surfs.end() || !sit->second.group) return;
	const mcGroup_t* g = sit->second.group;
	const int si = sit->second.surfIndex;
	if (g->surfs[si]->numVerts != surface->numVerts) return;

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
	in->lastSeen = timeMs;

	// the first cloth surface drawn this frame simulates the whole model;
	// the others (and shadow/mirror passes) reuse the result
	if (in->outTime != timeMs || !in->active) {
		mcParams_t pt = *prm;
		pt.timeSec = timeMs * 0.001f;
		Simulate(g, in, timeMs, origin, axis, caps, ncaps, &pt, boneFn, boneCtx);
	}

	const int base = g->vertBase[si];
	for (int i = 0; i < surface->numVerts; i++) {
		const int p = g->vertToParticle[static_cast<std::vector<int, std::allocator<int>>::size_type>(base + i)];
		const float f = g->freedom[p];
		if (f <= 0.0f) continue;
		mcCopy(&in->outLocal[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], xyz[i]);
		// keep this vertex's side of the surface (two-sided robes, seams)
		const float* cn = &in->nrmLocal[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3];
		const float* sn = &in->skinNrm[static_cast<std::vector<float, std::allocator<float>>::size_type>(base + i) * 3];
		const float sgn = mcDot(cn, sn) < 0.0f ? -1.0f : 1.0f;
		const float b = f < 0.25f ? f * 4.0f : 1.0f;		// fade in near the attached edge
		for (int k = 0; k < 3; k++) normal[i][k] = normal[i][k] * (1.0f - b) + cn[k] * sgn * b;
		mcNorm(normal[i]);
	}
}

void MC_Blast(const float center[3], float strength, float radius) {
	if (radius <= 1.0f) return;
	for (auto& kv : g_instances) {
		mcInstance_t* in = kv.second;
		if (!in || !in->active || in->p.empty()) continue;
		const int np = (int)(in->p.size() / 3);
		if (in->impulse.size() != in->p.size()) in->impulse.assign(in->p.size(), 0.0f);
		for (int p = 0; p < np; p++) {
			vec3_t d;
			mcSub(&in->p[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], center, d);
			const float dist = mcLen(d);
			if (dist >= radius || dist < 1e-3f) continue;
			// falls off with distance; a little upward bias, as blast waves lift cloth
			const float sp = strength * (1.0f - dist / radius);
			mcScale(d, sp / dist, d);
			d[2] += 0.3f * sp;
			mcAdd(&in->impulse[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3], d, &in->impulse[static_cast<std::vector<float, std::allocator<float>>::size_type>(p) * 3]);
		}
	}
}

void MC_Prune(int timeMs) {
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