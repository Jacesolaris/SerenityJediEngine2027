/*
===========================================================================
Saber slice (Jedi Survivor style cuts at the saber contact point)

Shared by the renderers (rd-vanilla SP + MP, rd-rend2). Header only.

The game ties each cut to a cut slot (tr_types.h SaberSlice_Seg: per skeleton
an anchor bone and the bone at the far end of its segment, found by name) and
sends it in refEntity_t as a plane in the anchor bone's space (RF_SABERSLICE,
sliceBone = slot | SABERSLICE_PLANE_LOCAL);
R_SliceResolvePlanes turns it into a world plane on the drawn skeleton. Here we decide, per skeleton bone, what
the cut does to the vertices that mostly follow that bone:

  SLICE_PLANE  the vertex is tested against the plane (the real cut):
               the anchor bone (and its twist bone), its parent and its
               children, plus the whole spine for a torso cut
  SLICE_GONE   the vertex is cut away whole (a hand beyond an arm cut)
  SLICE_KEEP   the vertex stays whole

Below a cut limb or neck segment everything goes with the piece. Other
bones attached to a PLANE bone go with the side of the plane their joint
is on, so a diagonal chest cut takes the arm whose shoulder is above the
cut and leaves the other arm on the legs. For a limb or neck cut such a
bone also has to be near the cut limb (skeletons where every bone hangs
off the pelvis or chest, like _humanoid: a right thigh cut must not take
the left foot). Everything else follows its parent.

The positive side of a plane is cut away. A flying piece shows only the
cut away side (sign -1).

Needs before including: mdxaHeader_t, mdxaSkel_t, mdxaSkelOffsets_t,
mdxaBone_t, refEntity_t (with the slice fields), Q_stricmp.
===========================================================================
*/
#pragma once

enum
{
	SLICE_KEEP = 0,
	SLICE_PLANE = 1,
	SLICE_GONE = 2
};

constexpr int SLICE_MAX_BONES = 256;
constexpr float SLICE_FAR = 1000.0f;

struct sliceModes_t
{
	int numCuts;
	int numBones;
	float plane[MAX_SABER_SLICES][4]; // model space (where the ghoul2 vertices are skinned)
	float sign[MAX_SABER_SLICES]; // +1 = keep the negative side, -1 = flying piece (keep the positive side)
	unsigned char mode[MAX_SABER_SLICES][SLICE_MAX_BONES];
};

static inline bool R_SliceActive(const refEntity_t& e)
{
	return (e.renderfx & RF_SABERSLICE) && e.sliceCount > 0 && e.sliceCount <= MAX_SABER_SLICES;
}

// world plane -> model space. p_world = origin + x*axis[0] + y*axis[1] + z*axis[2]
static inline void R_SlicePlaneToModel(const refEntity_t& e, const float* w, float* out)
{
	out[0] = w[0] * e.axis[0][0] + w[1] * e.axis[0][1] + w[2] * e.axis[0][2];
	out[1] = w[0] * e.axis[1][0] + w[1] * e.axis[1][1] + w[2] * e.axis[1][2];
	out[2] = w[0] * e.axis[2][0] + w[1] * e.axis[2][1] + w[2] * e.axis[2][2];
	out[3] = w[3] - (w[0] * e.origin[0] + w[1] * e.origin[1] + w[2] * e.origin[2]);
}

static inline const mdxaSkel_t* R_SliceSkel(const mdxaHeader_t* gla, const int i)
{
	const auto offsets = reinterpret_cast<const mdxaSkelOffsets_t*>(reinterpret_cast<const byte*>(gla) + sizeof(mdxaHeader_t));
	return reinterpret_cast<const mdxaSkel_t*>(reinterpret_cast<const byte*>(gla) + sizeof(mdxaHeader_t) + offsets->offsets[i]);
}

static inline int R_SliceFindBone(const mdxaHeader_t* gla, const char* name)
{
	const int n = gla->numBones < SLICE_MAX_BONES ? gla->numBones : SLICE_MAX_BONES;
	for (int i = 0; i < n; i++)
	{
		if (!Q_stricmp(R_SliceSkel(gla, i)->name, name))
		{
			return i;
		}
	}
	return -1;
}

static inline int R_SliceHasBone(void* gla, const char* name)
{
	return R_SliceFindBone(static_cast<const mdxaHeader_t*>(gla), name) >= 0;
}

constexpr int SLICE_CHAIN = 4;
constexpr int SLICE_SPINE = 5 + SABERSLICE_SLOT_COUNT;

// bone indices we need, looked up once per skeleton
struct sliceSkelInfo_t
{
	const mdxaHeader_t* gla;
	int anchor[SABERSLICE_SLOT_COUNT];
	int extra[SABERSLICE_SLOT_COUNT]; // twist bone cut with the anchor
	int chain[SABERSLICE_SLOT_COUNT][SLICE_CHAIN]; // anchor, child, then the next segments down the limb (-1 ends)
	float radius[SABERSLICE_SLOT_COUNT];
	int spine[SLICE_SPINE]; // pelvis, lower_lumbar, upper_lumbar, thoracic, cervical + this skeleton's torso anchors
};

static inline const sliceSkelInfo_t* R_SliceSkelInfo(const mdxaHeader_t* gla)
{
	static sliceSkelInfo_t cache[16];
	static int next = 0;
	for (auto& c : cache)
	{
		if (c.gla == gla)
		{
			return &c;
		}
	}
	sliceSkelInfo_t& c = cache[next];
	next = (next + 1) % 16;
	c.gla = gla;
	void* ctx = const_cast<mdxaHeader_t*>(gla);
	static const char* const spineNames[5] = { "pelvis", "lower_lumbar", "upper_lumbar", "thoracic", "cervical" };
	for (int i = 0; i < 5; i++)
	{
		c.spine[i] = R_SliceFindBone(gla, spineNames[i]);
	}
	for (int b = 0; b < SABERSLICE_SLOT_COUNT; b++)
	{
		const saberSliceSeg_t* seg = SaberSlice_FindSeg(b, R_SliceHasBone, ctx);
		c.anchor[b] = seg ? R_SliceFindBone(gla, seg->bone) : -1;
		c.extra[b] = seg && seg->extra ? R_SliceFindBone(gla, seg->extra) : -1;
		c.radius[b] = seg ? seg->radius : 3.0f;
		c.spine[5 + b] = seg && SaberSlice_IsTorso(b) ? c.anchor[b] : -1;
		for (int k = 0; k < SLICE_CHAIN; k++)
		{
			c.chain[b][k] = -1;
		}
		if (!seg)
		{
			continue;
		}
		// down the limb: the anchor, its child, then any segment that starts where the last one ends
		c.chain[b][0] = c.anchor[b];
		c.chain[b][1] = R_SliceFindBone(gla, seg->child);
		const char* end = seg->child;
		for (int k = 2; k < SLICE_CHAIN; k++)
		{
			const saberSliceSeg_t* next = nullptr;
			const saberSliceSeg_t* s;
			for (int i = 0; (s = SaberSlice_Seg(i)) != nullptr; i++)
			{
				if (!Q_stricmp(s->bone, end) && R_SliceHasBone(ctx, s->child))
				{
					next = s;
					break;
				}
			}
			if (!next)
			{
				break;
			}
			c.chain[b][k] = R_SliceFindBone(gla, next->child);
			end = next->child;
		}
	}
	return &c;
}

// joint position of bone i in model space: render matrix * base pose
template <class TBoneCache>
static inline void R_SliceJoint(TBoneCache& bones, const mdxaHeader_t* gla, const int i, float* out)
{
	const mdxaBone_t& m = bones.EvalRender(i);
	const mdxaSkel_t* skel = R_SliceSkel(gla, i);
	const float bx = skel->BasePoseMat.matrix[0][3];
	const float by = skel->BasePoseMat.matrix[1][3];
	const float bz = skel->BasePoseMat.matrix[2][3];
	for (int k = 0; k < 3; k++)
	{
		out[k] = m.matrix[k][0] * bx + m.matrix[k][1] * by + m.matrix[k][2] * bz + m.matrix[k][3];
	}
}

/*
Frozen modes: which side each attached bone goes with is decided once, the
first time a cut is drawn, and then kept for as long as the entity is drawn.
Deciding it again every frame from the current pose made whole limbs pop in
and out while a cut body moved (a fall, a death anim): a leg whose hip joint
crossed the infinite cut plane flipped between the body and the piece.
Cuts are matched by anchor bone + piece bit (the game never cuts the same
bone twice), so a cut the cgame skips for a frame doesn't shift the others.
*/
constexpr int SLICE_FROZEN_ENTRIES = 32;
constexpr int SLICE_FROZEN_TIMEOUT = 2000; // frames not drawn before an entry is forgotten

struct sliceFrozen_t
{
	const void* ghoul2;
	const mdxaHeader_t* gla;
	int lastFrame;
	float heat;
	int count;
	int bone[MAX_SABER_SLICES];
	int invert[MAX_SABER_SLICES];
	unsigned char mode[MAX_SABER_SLICES][SLICE_MAX_BONES];
};

static inline int R_SliceFrozenFind(const sliceFrozen_t* f, const int bone, const int invert)
{
	for (int i = 0; i < f->count; i++)
	{
		if (f->bone[i] == bone && f->invert[i] == invert)
		{
			return i;
		}
	}
	return -1;
}

static inline sliceFrozen_t* R_SliceFrozenFor(const refEntity_t& e, const mdxaHeader_t* gla, const int frameNum)
{
	static sliceFrozen_t cache[SLICE_FROZEN_ENTRIES];
	sliceFrozen_t* f = nullptr;
	for (auto& c : cache)
	{
		if (c.ghoul2 == e.ghoul2 && c.gla == gla)
		{
			f = &c;
			break;
		}
	}
	if (f)
	{
		const int age = frameNum - f->lastFrame;
		bool newCut = false;
		for (int s = 0; s < e.sliceCount; s++)
		{
			if (R_SliceFrozenFind(f, e.sliceBone[s] & SABERSLICE_BONE_MASK, (e.sliceInvert >> s) & 1) < 0)
			{
				newCut = true;
			}
		}
		// hotter than last time with no new cut: a fresh entity in the same slot, cut at the same bones
		if (age < 0 || age > SLICE_FROZEN_TIMEOUT || (e.sliceHeat > f->heat + 0.05f && !newCut))
		{
			f->count = 0;
		}
	}
	else
	{
		// the entry drawn longest ago
		f = &cache[0];
		for (auto& c : cache)
		{
			if (!c.ghoul2)
			{
				f = &c;
				break;
			}
			if (c.lastFrame - frameNum < f->lastFrame - frameNum)
			{
				f = &c;
			}
		}
		f->ghoul2 = e.ghoul2;
		f->gla = gla;
		f->count = 0;
	}
	f->lastFrame = frameNum;
	f->heat = e.sliceHeat;
	return f;
}

static inline void R_SliceFreeze(sliceFrozen_t* f, const int bone, const int invert, const unsigned char* mode)
{
	if (!f || f->count >= MAX_SABER_SLICES)
	{
		return;
	}
	const int i = f->count++;
	f->bone[i] = bone;
	f->invert[i] = invert;
	memcpy(f->mode[i], mode, sizeof(f->mode[i]));
}

// a metal body (droids): the cut face is hot metal, not flesh
static inline bool R_SliceMetal(const refEntity_t& e)
{
	for (int s = 0; s < e.sliceCount && s < MAX_SABER_SLICES; s++)
	{
		if (e.sliceBone[s] & SABERSLICE_METAL)
		{
			return true;
		}
	}
	return false;
}

/*
R_SliceResolvePlanes: the cgame sends each cut in its anchor bone's space
(sliceBone | SABERSLICE_PLANE_LOCAL). Place it on the skeleton the renderer
draws this frame (smoothed, ragdoll) and write the world plane back, so the
cut can't drift off the drawn body: the cgame's bolt matrices are unsmoothed,
and a falling ragdoll could show a cut-off head on the body again.
Call once per entity per frame, after the main model's bones are set up.
*/
static inline void R_SlicePlaneModelToWorld(const refEntity_t& e, const float* model, float* out)
{
	// p_world = origin + sum p_k axis[k]; the axes may be scaled
	float n[3] = { 0.0f, 0.0f, 0.0f };
	for (int k = 0; k < 3; k++)
	{
		const float lenSq = e.axis[k][0] * e.axis[k][0] + e.axis[k][1] * e.axis[k][1] + e.axis[k][2] * e.axis[k][2];
		if (lenSq > 0.0001f)
		{
			for (int c = 0; c < 3; c++)
			{
				n[c] += model[k] / lenSq * e.axis[k][c];
			}
		}
	}
	float d = model[3] + n[0] * e.origin[0] + n[1] * e.origin[1] + n[2] * e.origin[2];
	const float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (len > 0.0001f)
	{
		n[0] /= len;
		n[1] /= len;
		n[2] /= len;
		d /= len;
	}
	out[0] = n[0];
	out[1] = n[1];
	out[2] = n[2];
	out[3] = d;
}

template <class TBoneCache>
static inline void R_SliceResolvePlanes(refEntity_t& e, TBoneCache& bones, const mdxaHeader_t* gla)
{
	if (!gla || !R_SliceActive(e))
	{
		return;
	}
	const sliceSkelInfo_t* info = nullptr;
	const int numBones = gla->numBones < SLICE_MAX_BONES ? gla->numBones : SLICE_MAX_BONES;
	for (int s = 0; s < e.sliceCount; s++)
	{
		if (!(e.sliceBone[s] & SABERSLICE_PLANE_LOCAL))
		{
			continue;
		}
		e.sliceBone[s] &= ~SABERSLICE_PLANE_LOCAL;
		const int b = e.sliceBone[s] & SABERSLICE_BONE_MASK;
		if (!info)
		{
			info = R_SliceSkelInfo(gla);
		}
		const int a = b < SABERSLICE_SLOT_COUNT ? info->anchor[b] : -1;
		float model[4];
		bool ok = false;
		if (a >= 0 && a < numBones)
		{
			// the bolt matrix the game made the plane with: bone * base pose, axes normalized (G2API_GetBoltMatrix)
			const mdxaBone_t& r = bones.EvalRender(a);
			const mdxaBone_t& base = R_SliceSkel(gla, a)->BasePoseMat;
			float m[3][4];
			for (int i = 0; i < 3; i++)
			{
				for (int j = 0; j < 4; j++)
				{
					m[i][j] = r.matrix[i][0] * base.matrix[0][j] + r.matrix[i][1] * base.matrix[1][j] + r.matrix[i][2] * base.matrix[2][j];
				}
				m[i][3] += r.matrix[i][3];
				const float len = sqrtf(m[i][0] * m[i][0] + m[i][1] * m[i][1] + m[i][2] * m[i][2]);
				if (len > 0.0001f)
				{
					m[i][0] /= len;
					m[i][1] /= len;
					m[i][2] /= len;
				}
			}
			ok = SaberSlice_PlaneFromBone(m, e.slicePlane[s], model) != 0;
		}
		if (ok)
		{
			R_SlicePlaneModelToWorld(e, model, e.slicePlane[s]);
		}
		else
		{
			// nothing to place it on: the body keeps everything, a piece shows nothing (as the cgame did)
			e.slicePlane[s][0] = 0.0f;
			e.slicePlane[s][1] = 0.0f;
			e.slicePlane[s][2] = 1.0f;
			e.slicePlane[s][3] = 1.0e6f;
		}
	}
}

// squared distance from p to the cut limb (its chain of joints, model space)
template <class TBoneCache>
static inline float R_SliceChainDistSq(TBoneCache& bones, const mdxaHeader_t* gla, const sliceSkelInfo_t* info, const int slot, const float* p)
{
	const int numBones = gla->numBones < SLICE_MAX_BONES ? gla->numBones : SLICE_MAX_BONES;
	float best = 1e30f;
	float prev[3];
	bool havePrev = false;
	for (int k = 0; k < SLICE_CHAIN; k++)
	{
		const int i = info->chain[slot][k];
		if (i < 0 || i >= numBones)
		{
			break;
		}
		float j[3];
		R_SliceJoint(bones, gla, i, j);
		float d[3] = { p[0] - j[0], p[1] - j[1], p[2] - j[2] };
		if (havePrev)
		{
			// nearest point on the segment prev -> j
			const float ab[3] = { j[0] - prev[0], j[1] - prev[1], j[2] - prev[2] };
			const float ap[3] = { p[0] - prev[0], p[1] - prev[1], p[2] - prev[2] };
			const float lenSq = ab[0] * ab[0] + ab[1] * ab[1] + ab[2] * ab[2];
			float t = lenSq > 0.0001f ? (ap[0] * ab[0] + ap[1] * ab[1] + ap[2] * ab[2]) / lenSq : 0.0f;
			t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
			for (int c = 0; c < 3; c++)
			{
				d[c] = ap[c] - t * ab[c];
			}
		}
		const float distSq = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
		if (distSq < best)
		{
			best = distSq;
		}
		prev[0] = j[0];
		prev[1] = j[1];
		prev[2] = j[2];
		havePrev = true;
	}
	return best;
}

// bone i is the cut limb's child bone (or further down it), or the anchor's twist bone
static inline bool R_SliceInChain(const sliceSkelInfo_t* info, const int slot, const int i)
{
	if (info->extra[slot] == i)
	{
		return true;
	}
	for (int k = 1; k < SLICE_CHAIN; k++)
	{
		if (info->chain[slot][k] == i)
		{
			return true;
		}
	}
	return false;
}

// bone i's joint is on the cut away side (and, for a limb, near the cut limb)
template <class TBoneCache>
static inline bool R_SliceGoesWithPiece(TBoneCache& bones, const mdxaHeader_t* gla, const sliceSkelInfo_t* info, const int slot, const int i,
	const float* pl, const bool torso, const float chainNearSq)
{
	float j[3];
	R_SliceJoint(bones, gla, i, j);
	if (j[0] * pl[0] + j[1] * pl[1] + j[2] * pl[2] - pl[3] <= 0.0f)
	{
		return false;
	}
	return torso || R_SliceChainDistSq(bones, gla, info, slot, j) <= chainNearSq;
}

/*
R_SliceBuildModes: per bone modes for the main model of a sliced entity.
Cached per bone cache and frame (a model can be drawn in several passes).
Returns nullptr when nothing is cut (no anchor bone in this skeleton).
*/
template <class TBoneCache>
static inline const sliceModes_t* R_SliceBuildModes(const refEntity_t& e, TBoneCache& bones, const mdxaHeader_t* gla, const int frameNum)
{
	struct entry_t
	{
		const void* key;
		int frame;
		bool valid;
		sliceModes_t m;
	};
	static entry_t cache[16];
	static int next = 0;

	if (!gla || !R_SliceActive(e))
	{
		return nullptr;
	}
	// a cut still in bone space (its main model's bones weren't set up through the usual path): place it now
	R_SliceResolvePlanes(const_cast<refEntity_t&>(e), bones, gla);
	for (auto& c : cache)
	{
		if (c.key == &bones && c.frame == frameNum)
		{
			return c.valid ? &c.m : nullptr;
		}
	}

	entry_t& c = cache[next];
	next = (next + 1) % 16;
	c.key = &bones;
	c.frame = frameNum;
	c.valid = false;

	const sliceSkelInfo_t* info = R_SliceSkelInfo(gla);
	const int numBones = gla->numBones < SLICE_MAX_BONES ? gla->numBones : SLICE_MAX_BONES;
	sliceModes_t& m = c.m;
	m.numCuts = 0;
	m.numBones = numBones;
	sliceFrozen_t* frozen = R_SliceFrozenFor(e, gla, frameNum);

	for (int s = 0; s < e.sliceCount; s++)
	{
		// cut index == slice index, so rend2 can use the refEntity planes as they are
		const int b = e.sliceBone[s] & SABERSLICE_BONE_MASK;
		const int cut = m.numCuts;
		float* pl = m.plane[cut];
		R_SlicePlaneToModel(e, e.slicePlane[s], pl);
		m.sign[cut] = (e.sliceInvert & (1 << s)) ? -1.0f : 1.0f;
		if (b < 0 || b >= SABERSLICE_SLOT_COUNT || info->anchor[b] < 0)
		{
			// anchor bone not in this skeleton: this cut leaves the model whole
			memset(m.mode[cut], SLICE_KEEP, sizeof(m.mode[cut]));
			m.numCuts++;
			continue;
		}
		const int a = info->anchor[b];

		// already decided when this cut was first drawn
		const int invert = (e.sliceInvert >> s) & 1;
		const int kept = R_SliceFrozenFind(frozen, b, invert);
		if (kept >= 0)
		{
			memcpy(m.mode[cut], frozen->mode[kept], sizeof(m.mode[cut]));
			m.numCuts++;
			continue;
		}

		const bool torso = SaberSlice_IsTorso(b) != 0;
		const int anchorParent = R_SliceSkel(gla, a)->parent;

		// the bones whose vertices are tested against the plane
		unsigned char inPlane[SLICE_MAX_BONES] = {};
		inPlane[a] = 1;
		if (info->extra[b] >= 0 && info->extra[b] < numBones)
		{
			inPlane[info->extra[b]] = 1;
		}
		if (anchorParent >= 0 && anchorParent < numBones)
		{
			inPlane[anchorParent] = 1;
		}
		for (int i = 0; i < numBones; i++)
		{
			if (R_SliceSkel(gla, i)->parent == a)
			{
				inPlane[i] = 1;
			}
		}
		if (torso)
		{
			for (const int sp : info->spine)
			{
				if (sp >= 0)
				{
					inPlane[sp] = 1;
				}
			}
		}

		// how near the cut limb a bone hanging off the anchor's parent has to be to go with the piece
		float chainNearSq = 0.0f;
		if (!torso)
		{
			float j0[3], j1[3];
			R_SliceJoint(bones, gla, a, j0);
			const int c1 = info->chain[b][1];
			float len = 0.0f;
			if (c1 >= 0 && c1 < numBones)
			{
				R_SliceJoint(bones, gla, c1, j1);
				len = sqrtf((j1[0] - j0[0]) * (j1[0] - j0[0]) + (j1[1] - j0[1]) * (j1[1] - j0[1]) + (j1[2] - j0[2]) * (j1[2] - j0[2]));
			}
			const float nearDist = 0.5f * len + info->radius[b];
			chainNearSq = nearDist * nearDist;
		}

		// the anchor's parent and its parents up to the root
		unsigned char ancestor[SLICE_MAX_BONES] = {};
		for (int p = anchorParent, guard = 0; p >= 0 && p < numBones && guard < SLICE_MAX_BONES; p = R_SliceSkel(gla, p)->parent, guard++)
		{
			ancestor[p] = 1;
		}

		// resolve every bone (parents first, the skeleton is small so just iterate until done)
		unsigned char* mode = m.mode[cut];
		unsigned char done[SLICE_MAX_BONES] = {};
		int remaining = numBones;
		for (int pass = 0; pass < 64 && remaining > 0; pass++)
		{
			for (int i = 0; i < numBones; i++)
			{
				if (done[i])
				{
					continue;
				}
				const int parent = R_SliceSkel(gla, i)->parent;
				if (inPlane[i])
				{
					mode[i] = SLICE_PLANE;
				}
				else if (parent < 0 || parent >= numBones)
				{
					mode[i] = SLICE_KEEP;
				}
				else if (inPlane[parent] && !torso && parent != anchorParent && R_SliceInChain(info, b, parent))
				{
					// beyond the cut segment of a limb or the neck: always goes with the piece
					mode[i] = SLICE_GONE;
				}
				else if (inPlane[parent])
				{
					// goes with the side its joint is on; for a limb only when it's near the cut limb
					// (another limb hanging off the same parent stays: flat skeletons, R2-D2's legs on a dome cut)
					mode[i] = R_SliceGoesWithPiece(bones, gla, info, b, i, pl, torso, chainNearSq) ? SLICE_GONE : SLICE_KEEP;
				}
				else if (done[parent])
				{
					mode[i] = mode[parent];
					if (mode[i] == SLICE_KEEP && !torso && ancestor[parent] && R_SliceGoesWithPiece(bones, gla, info, b, i, pl, false, chainNearSq))
					{
						mode[i] = SLICE_GONE; // part of the cut limb hanging off the root (rancor and wampa feet and toes)
					}
				}
				else
				{
					continue;
				}
				done[i] = 1;
				remaining--;
			}
		}
		for (int i = 0; i < numBones; i++)
		{
			if (!done[i])
			{
				mode[i] = SLICE_KEEP;
			}
		}
		R_SliceFreeze(frozen, b, invert, mode);
		m.numCuts++;
	}

	c.valid = m.numCuts > 0;
	return c.valid ? &m : nullptr;
}

/*
R_SliceBoltOnModes: a model bolted onto the main model (a saber or gun in a hand)
goes whole with the side its bolt point is on, but only when the main model bone
nearest that point is affected by the cut.
out[cut] = SLICE_KEEP or SLICE_GONE.
*/
template <class TBoneCache>
static inline void R_SliceBoltOnModes(const sliceModes_t& m, TBoneCache& mainBones, const mdxaHeader_t* mainGla, const float* boltPos, unsigned char* out)
{
	int nearest = -1;
	float best = 1e30f;
	for (int i = 0; i < m.numBones; i++)
	{
		float j[3];
		R_SliceJoint(mainBones, mainGla, i, j);
		const float dx = j[0] - boltPos[0], dy = j[1] - boltPos[1], dz = j[2] - boltPos[2];
		const float d = dx * dx + dy * dy + dz * dz;
		if (d < best)
		{
			best = d;
			nearest = i;
		}
	}
	for (int cut = 0; cut < m.numCuts; cut++)
	{
		unsigned char mode = nearest >= 0 ? m.mode[cut][nearest] : static_cast<unsigned char>(SLICE_KEEP);
		if (mode == SLICE_PLANE)
		{
			const float* pl = m.plane[cut];
			mode = (boltPos[0] * pl[0] + boltPos[1] * pl[1] + boltPos[2] * pl[2] - pl[3]) > 0.0f ? SLICE_GONE : SLICE_KEEP;
		}
		out[cut] = mode;
	}
}

// signed value for one vertex and one cut: > 0 means this vertex is hidden
static inline float R_SliceValue(const sliceModes_t& m, const int cut, const unsigned char mode, const float* p)
{
	float d;
	if (mode == SLICE_PLANE)
	{
		const float* pl = m.plane[cut];
		d = p[0] * pl[0] + p[1] * pl[1] + p[2] * pl[2] - pl[3];
	}
	else
	{
		d = mode == SLICE_GONE ? SLICE_FAR : -SLICE_FAR;
	}
	return d * m.sign[cut];
}
