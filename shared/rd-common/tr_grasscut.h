/*
===========================================================================
Grass cutting (2026-11-02, DH): a saber swing cuts surface-sprite grass down to stubble.

The cgame sends every swing of a low blade as "grass_wave ... 3 ..." (type 3, tr_init.cpp R_GrassWave_f ->
R_GrassAddWave -> R_GrassCutFromCommand). The sweep is drawn into a grid of 4-unit cells: the lowest height the
blade went through each cell, and when. A grass blade standing in a cut cell is drawn as stubble up to that height;
with cg_grassCutFx 1 the cut tips glow like embers for 2 s, and the cut-off tops fall over and lie on the ground for
cg_grassCutDebrisTime seconds before they fade. After r_grassCutRegrow seconds the grass has grown back (it grows in
the last quarter of that time). r_grassCut 0 turns it off, 2 also reports every cut in the console (Debug builds only).

The grid wraps (cells GRASSCUT_SIZE apart share a slot, the newer cut wins). In rend2 the grid is the RGBA32F texture
the surface sprite shader reads (texel = cut height, time, cell x, cell y). The yavin plant models are cut on the same
grid (R_GrassCutPlantVertex, tr_grasscut_plant.h).

Header-only, included by one file of each renderer (rd-vanilla tr_surfacesprites.cpp, rd-rend2 tr_surface.cpp).
Define GRASSCUT_CVAR_GET(name, value) (the renderer's Cvar_Get with CVAR_ARCHIVE) and GRASSCUT_RI (ri. or ri->)
before including it.
===========================================================================
*/

#pragma once

#include <cmath>
#include <cstring>
#include <cstdlib>

#include "tr_grasscut_plant.h"

#define GRASSCUT_SIZE		512		// cells per side (a power of two)
#define GRASSCUT_CELL		4.0f	// units
#define GRASSCUT_EMBER_TIME	2.0f	// seconds the cut tips glow
#define GRASSCUT_FALL_TIME	0.45f	// seconds a clipping takes to fall over
#define GRASSCUT_MAX_PLANTS	4096	// plant models on a map (R_GrassCutLoadPlants)
#define GRASSCUT_MAX_NEAR	32		// plants near the cuts handed to the plant cut each frame (u_PlantCenters[32] in rend2)
#define GRASSCUT_MAX_GRASS	32768	// world surfaces with surface-sprite grass (R_GrassCutAddGrassSurface)

struct grassCutCell_t
{
	float z;	// lowest height the blade went through
	float time;	// seconds (refdef time) of that cut, 0 = never cut
	float cx;	// the cell this slot holds
	float cy;
};

static grassCutCell_t grassCutGrid[GRASSCUT_SIZE * GRASSCUT_SIZE];
static int grassCutDirtyMin = 0;				// rows changed since the last upload (rend2)
static int grassCutDirtyMax = GRASSCUT_SIZE - 1;
static float grassCutLastTime = -1.0e9f;		// seconds of the newest cut
static float grassCutFrameTime = 0.0f;			// seconds of the last frame drawn
static float grassCutFx = 1.0f;					// from the cgame: ember glow and clippings on (cg_grassCutFx)
static float grassCutDebris = 10.0f;			// from the cgame: seconds the clippings lie (cg_grassCutDebrisTime)
static cvar_t* r_grassCut = nullptr;
static cvar_t* r_grassCutRegrow = nullptr;
static int grassCutBladesCut = 0;				// r_grassCut 2: blades drawn cut since the last report (rd-vanilla)
static int grassCutPlantVerts = 0;				// r_grassCut 2: plant model vertices drawn cut since the last report (rd-vanilla)
static float grassCutReportTime = 0.0f;
static float grassCutBox[4] = { 1.0e9f, 1.0e9f, -1.0e9f, -1.0e9f };	// x/y min and max of the live cuts
static float grassCutLastPos[2] = { 0.0f, 0.0f };		// x/y of the newest cut

// The plant models of the map (misc_model, misc_model_static, misc_model_breakable with a plant model, from the
// entity lump): x, y, z of the origin and how far from it a vertex still counts as that plant's
struct grassCutPlant_t
{
	float x, y, z, radius;
};
static grassCutPlant_t grassCutPlants[GRASSCUT_MAX_PLANTS];
static int grassCutNumPlants = 0;
static float grassCutPlantBoxes[GRASSCUT_MAX_PLANTS][6];	// world surfaces with a plant shader (R_GrassCutAddPlantSurface)
static int grassCutPlantBoxShader[GRASSCUT_MAX_PLANTS];	// and that shader's index
static int grassCutNumPlantBoxes = 0;
static float grassCutGrass[GRASSCUT_MAX_GRASS][6];	// boxes of the world's surfaces with surface-sprite grass
static int grassCutNumGrass = 0;
static float grassCutHitTime = -1.0e9f;		// seconds of the last r_grassCutHit
static int grassCutNumNear = 0;				// r_grassCut 2: plants near the cuts in the last R_GrassCutNearPlants

static void R_GrassCutCvars()
{
	if (!r_grassCut)
	{
		r_grassCut = GRASSCUT_CVAR_GET("r_grassCut", "1");
		r_grassCutRegrow = GRASSCUT_CVAR_GET("r_grassCutRegrow", "60");
	}
}

// Seconds until cut grass has grown back
static float R_GrassCutRegrowTime()
{
	R_GrassCutCvars();
	const float t = r_grassCutRegrow->value;
	return t < 5.0f ? 5.0f : t > 3600.0f ? 3600.0f : t;
}

// Seconds the clippings lie (0 = no clippings)
static float R_GrassCutDebrisTime()
{
	return grassCutFx > 0.0f ? grassCutDebris : 0.0f;
}

static void R_GrassCutClear()
{
	memset(grassCutGrid, 0, sizeof(grassCutGrid));
	grassCutDirtyMin = 0;
	grassCutDirtyMax = GRASSCUT_SIZE - 1;
	grassCutLastTime = -1.0e9f;
	grassCutBox[0] = grassCutBox[1] = 1.0e9f;
	grassCutBox[2] = grassCutBox[3] = -1.0e9f;
}

// Once a frame before drawing grass: is anything cut (or lying) now? A clock that went back (a new map, a restart)
// clears the old cuts.
static bool R_GrassCutFrame(const float now)
{
	R_GrassCutCvars();
	if (now < grassCutFrameTime - 1.0f)
	{
		R_GrassCutClear();
	}
	grassCutFrameTime = now;
#ifdef _DEBUG	// the reports print in Debug builds only (DH)
	if (r_grassCut->integer == 2 && (now - grassCutReportTime >= 1.0f || now < grassCutReportTime))
	{
		if (grassCutBladesCut || grassCutPlantVerts)
		{
			GRASSCUT_RI Printf(PRINT_ALL, "grass cut: %d blades, %d plant model vertices drawn cut in the last second (%d of %d plants near the cuts)\n",
				grassCutBladesCut, grassCutPlantVerts, grassCutNumNear, grassCutNumPlants);
		}
		grassCutBladesCut = 0;
		grassCutPlantVerts = 0;
		grassCutReportTime = now;
	}
#endif
	if (!r_grassCut->integer)
	{
		return false;
	}
	const float debris = R_GrassCutDebrisTime() + GRASSCUT_FALL_TIME;
	const float regrow = R_GrassCutRegrowTime();
	if (now - grassCutLastTime < (regrow > debris ? regrow : debris))
	{
		return true;
	}
	grassCutBox[0] = grassCutBox[1] = 1.0e9f;	// nothing live: the next cuts start a new box
	grassCutBox[2] = grassCutBox[3] = -1.0e9f;
	return false;
}

static inline grassCutCell_t* R_GrassCutSlot(const int cx, const int cy)
{
	return &grassCutGrid[(cy & (GRASSCUT_SIZE - 1)) * GRASSCUT_SIZE + (cx & (GRASSCUT_SIZE - 1))];
}

// The blade went through (x, y, z) at time t: the cells within radius are cut down to z (a cell already cut lower
// keeps its cut)
static void R_GrassCutMark(const float x, const float y, const float z, const float radius, const float t, const float regrow)
{
	const int x0 = static_cast<int>(floorf((x - radius) / GRASSCUT_CELL));
	const int x1 = static_cast<int>(floorf((x + radius) / GRASSCUT_CELL));
	const int y0 = static_cast<int>(floorf((y - radius) / GRASSCUT_CELL));
	const int y1 = static_cast<int>(floorf((y + radius) / GRASSCUT_CELL));
	const float reach = radius + GRASSCUT_CELL * 0.5f;
	grassCutBox[0] = x - radius < grassCutBox[0] ? x - radius : grassCutBox[0];
	grassCutBox[1] = y - radius < grassCutBox[1] ? y - radius : grassCutBox[1];
	grassCutBox[2] = x + radius > grassCutBox[2] ? x + radius : grassCutBox[2];
	grassCutBox[3] = y + radius > grassCutBox[3] ? y + radius : grassCutBox[3];

	for (int cy = y0; cy <= y1; cy++)
	{
		const float dy = (static_cast<float>(cy) + 0.5f) * GRASSCUT_CELL - y;
		for (int cx = x0; cx <= x1; cx++)
		{
			const float dx = (static_cast<float>(cx) + 0.5f) * GRASSCUT_CELL - x;
			if (dx * dx + dy * dy > reach * reach)
			{
				continue;
			}
			grassCutCell_t* c = R_GrassCutSlot(cx, cy);
			const bool live = c->time > 0.0f && c->cx == static_cast<float>(cx) && c->cy == static_cast<float>(cy)
				&& t - c->time < regrow && t >= c->time;
			if (live && z >= c->z - 1.0f)
			{
				continue;
			}
			c->z = z;
			c->time = t;
			c->cx = static_cast<float>(cx);
			c->cy = static_cast<float>(cy);
			const int row = cy & (GRASSCUT_SIZE - 1);
			if (row < grassCutDirtyMin)
				grassCutDirtyMin = row;
			if (row > grassCutDirtyMax)
				grassCutDirtyMax = row;
		}
	}
}

// A world surface with surface-sprite grass (map load): cuts in its box make the grass fizz (R_GrassCutOnGrass)
static void R_GrassCutAddGrassSurface(const float* mins, const float* maxs)
{
	if (grassCutNumGrass < GRASSCUT_MAX_GRASS)
	{
		float* b = grassCutGrass[grassCutNumGrass++];
		for (int k = 0; k < 3; k++)
		{
			b[k] = mins[k];
			b[k + 3] = maxs[k];
		}
	}
}

// Is p in surface-sprite grass (over a grass surface, up to 64 units) or in a plant model?
static bool R_GrassCutOnGrass(const float* p)
{
	for (int i = 0; i < grassCutNumGrass; i++)
	{
		const float* b = grassCutGrass[i];
		if (p[0] >= b[0] - 8.0f && p[0] <= b[3] + 8.0f && p[1] >= b[1] - 8.0f && p[1] <= b[4] + 8.0f && p[2] >= b[2] - 16.0f
			&& p[2] <= b[5] + 64.0f)
		{
			return true;
		}
	}
	for (int i = 0; i < grassCutNumPlants; i++)
	{
		const grassCutPlant_t* pl = &grassCutPlants[i];
		const float dx = p[0] - pl->x, dy = p[1] - pl->y;
		if (dx * dx + dy * dy <= pl->radius * pl->radius && p[2] >= pl->z - 16.0f && p[2] <= pl->z + 160.0f)
		{
			return true;
		}
	}
	return false;
}

// grass_wave time bx by bz 0 0 3 0 pbx pby pbz ptx pty ptz tx ty tz fx clippingSeconds radius: one frame of a swing,// the blade from base b to tip t, last frame from pb to pt (cgame cg_players CG_GrassCut). Called by R_GrassAddWave
// while the grass_wave command runs.
static void R_GrassCutFromCommand(const int timeMs, const float* base)
{
	R_GrassCutCvars();
	if (!r_grassCut->integer || GRASSCUT_RI Cmd_Argc() < 21)
	{
		return;
	}
	float pb[3], pt[3], tip[3];
	for (int k = 0; k < 3; k++)
	{
		pb[k] = static_cast<float>(atof(GRASSCUT_RI Cmd_Argv(9 + k)));
		pt[k] = static_cast<float>(atof(GRASSCUT_RI Cmd_Argv(12 + k)));
		tip[k] = static_cast<float>(atof(GRASSCUT_RI Cmd_Argv(15 + k)));
	}
	grassCutFx = static_cast<float>(atof(GRASSCUT_RI Cmd_Argv(18)));
	const float debris = static_cast<float>(atof(GRASSCUT_RI Cmd_Argv(19)));
	grassCutDebris = debris < 0.0f ? 0.0f : debris > 120.0f ? 120.0f : debris;
	float radius = static_cast<float>(atof(GRASSCUT_RI Cmd_Argv(20)));
	radius = radius < 1.0f ? 1.0f : radius > 16.0f ? 16.0f : radius;

	const float t = static_cast<float>(timeMs) * 0.001f;
	if (t < grassCutLastTime - 1.0f)
	{	// the clock went back: a new map or a restart
		R_GrassCutClear();
	}
	const float regrow = R_GrassCutRegrowTime();

	// sweep from last frame's blade to this one, in steps of the tip's travel
	const float tx = tip[0] - pt[0], ty = tip[1] - pt[1], tz = tip[2] - pt[2];
	int steps = static_cast<int>(ceilf(sqrtf(tx * tx + ty * ty + tz * tz) / (GRASSCUT_CELL * 1.5f)));
	steps = steps < 1 ? 1 : steps > 16 ? 16 : steps;
	for (int s = 1; s <= steps; s++)
	{
		const float f = static_cast<float>(s) / static_cast<float>(steps);
		float b[3], e[3];
		for (int k = 0; k < 3; k++)
		{
			b[k] = pb[k] + (base[k] - pb[k]) * f;
			e[k] = pt[k] + (tip[k] - pt[k]) * f;
		}
		const float lx = e[0] - b[0], ly = e[1] - b[1], lz = e[2] - b[2];
		int n = static_cast<int>(ceilf(sqrtf(lx * lx + ly * ly + lz * lz) / (GRASSCUT_CELL * 0.75f)));
		n = n < 1 ? 1 : n > 64 ? 64 : n;
		for (int i = 0; i <= n; i++)
		{
			const float g = static_cast<float>(i) / static_cast<float>(n);
			R_GrassCutMark(b[0] + lx * g, b[1] + ly * g, b[2] + lz * g, radius, t, regrow);
		}
	}
	grassCutLastTime = t;
	grassCutLastPos[0] = base[0];
	grassCutLastPos[1] = base[1];
	// the blade's low end in grass or a plant: r_grassCutHit = "time x y" of this cut (ms, where), at most every 100 ms;
	// the cgame fizzes on it near its blade (cg_grassCutSound), also on maps whose ground has no grass material
	if (t - grassCutHitTime >= 0.1f || t < grassCutHitTime)
	{
		const float* low = base[2] < tip[2] ? base : tip;
		if (R_GrassCutOnGrass(low))
		{
			char value[48];
			Com_sprintf(value, sizeof(value), "%d %.0f %.0f", timeMs, low[0], low[1]);
			GRASSCUT_RI Cvar_Set("r_grassCutHit", value);
			grassCutHitTime = t;
		}
	}
#ifdef _DEBUG
	if (r_grassCut->integer == 2 && R_GrassCutOnGrass(base[2] < tip[2] ? base : tip))
	{	// r_grassCut 2: report every cut that lands in grass or a plant (Debug builds only; a low swing over bare
		// ground marks the grid too, but would flood the console)
		GRASSCUT_RI Printf(PRINT_ALL, "grass cut %.2f: base %.0f %.0f %.0f tip %.0f %.0f %.0f, %d steps\n", t, base[0], base[1],
			base[2], tip[0], tip[1], tip[2], steps);
	}
#endif
}

// A grass blade standing at base, height tall: is it cut? keep = the part of it that stands now (0.12 - 1, it grows
// back in the last quarter of r_grassCutRegrow), keepCut = the part the cut left, age = seconds since the cut.
static bool R_GrassCutBlade(const float* base, const float height, const float now, const float regrow, float* keep,
	float* keepCut, float* age)
{
	if (height <= 0.0f)
	{
		return false;
	}
	const int cx = static_cast<int>(floorf(base[0] / GRASSCUT_CELL));
	const int cy = static_cast<int>(floorf(base[1] / GRASSCUT_CELL));
	const grassCutCell_t* c = R_GrassCutSlot(cx, cy);
	if (c->time <= 0.0f || c->cx != static_cast<float>(cx) || c->cy != static_cast<float>(cy))
	{
		return false;
	}
	float a = now - c->time;
	if (a < -0.25f || a >= regrow)
	{
		return false;
	}
	if (a < 0.0f)
	{
		a = 0.0f;
	}
	float k = (c->z - base[2]) / height;
	if (k >= 0.92f || k < -0.25f)
	{	// the blade went over this grass, or far below it (another floor)
		return false;
	}
	k = k < 0.12f ? 0.12f : k;
	float g = (a - 0.75f * regrow) / (0.25f * regrow);
	g = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
	g = g * g * (3.0f - 2.0f * g);
	*keepCut = k;
	*keep = k + (1.0f - k) * g;
	grassCutBladesCut++;
	*age = a;
	return true;
}

// How much the cut tips glow (0 - 1)
static float R_GrassCutEmber(const float age)
{
	if (grassCutFx <= 0.0f || age >= GRASSCUT_EMBER_TIME)
	{
		return 0.0f;
	}
	const float e = 1.0f - age / GRASSCUT_EMBER_TIME;
	return e * e;
}

// The cut-off top of a blade (from the cut to its tip; base and tip as drawn, right = half the blade's width across)
// falls over and lies on the ground: the quad's corners in the blade's order (bottom right, top right, top left, bottom
// left) and its alpha (0 - 1). false: no clipping (gone, or clippings off).
static bool R_GrassCutClipping(const float* base, const float* tip, const float* right, const float keepCut, const float age,
	float out[4][3], float* alpha)
{
	const float debris = R_GrassCutDebrisTime();
	if (debris <= 0.0f || age >= debris + GRASSCUT_FALL_TIME)
	{
		return false;
	}
	float cut[3], piece[3];
	for (int k = 0; k < 3; k++)
	{
		cut[k] = base[k] + (tip[k] - base[k]) * keepCut;
		piece[k] = tip[k] - cut[k];
	}
	const float len = sqrtf(piece[0] * piece[0] + piece[1] * piece[1] + piece[2] * piece[2]);
	if (len < 1.0f)
	{
		return false;
	}
	// it falls the way the blade's spot picks, faster and faster
	const float a = base[0] * 12.9898f + base[1] * 78.233f;
	const float dx = cosf(a), dy = sinf(a);
	float f = age / GRASSCUT_FALL_TIME;
	f = f > 1.0f ? 1.0f : f * f;

	const float halfWidth = sqrtf(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
	const float lyingBottom[3] = { base[0] + dx * 2.0f, base[1] + dy * 2.0f, base[2] + 0.6f };
	const float lyingTop[3] = { lyingBottom[0] + dx * len, lyingBottom[1] + dy * len, lyingBottom[2] };
	const float lyingRight[3] = { -dy * halfWidth, dx * halfWidth, 0.0f };
	float bottom[3], top[3], r[3];
	for (int k = 0; k < 3; k++)
	{
		bottom[k] = cut[k] + (lyingBottom[k] - cut[k]) * f;
		top[k] = tip[k] + (lyingTop[k] - tip[k]) * f;
		r[k] = right[k] + (lyingRight[k] - right[k]) * f;
		out[0][k] = bottom[k] + r[k];
		out[1][k] = top[k] + r[k];
		out[2][k] = top[k] - r[k];
		out[3][k] = bottom[k] - r[k];
	}
	// lies, then fades over the second half of its time
	const float fadeStart = debris * 0.5f;
	float al = age <= fadeStart ? 1.0f : 1.0f - (age - fadeStart) / (debris - fadeStart + GRASSCUT_FALL_TIME);
	*alpha = al < 0.0f ? 0.0f : al;
	return *alpha > 0.0f;
}

// How far an md3 model reaches out from its origin (the first frame's bounds, flat); 96 when it cannot be read
static float R_GrassCutModelRadius(const char* model)
{
	static char names[64][MAX_QPATH];
	static float radii[64];
	static int numNames = 0;
	for (int i = 0; i < numNames; i++)
	{
		if (!Q_stricmp(names[i], model))
		{
			return radii[i];
		}
	}
	float radius = 96.0f;
	void* buffer = nullptr;
	const int length = GRASSCUT_RI FS_ReadFile(model, &buffer);
	if (buffer && length >= 108 && !strncmp(static_cast<const char*>(buffer), "IDP3", 4))
	{	// md3 header: ofsFrames at byte 92; frame: mins[3], maxs[3], ...
		const byte* b = static_cast<const byte*>(buffer);
		int ofsFrames;
		memcpy(&ofsFrames, b + 92, sizeof(ofsFrames));
		if (ofsFrames > 0 && ofsFrames + 24 <= length)
		{
			float bounds[6];
			memcpy(bounds, b + ofsFrames, sizeof(bounds));
			radius = 0.0f;
			for (const int k : { 0, 1, 3, 4 })
			{
				radius = fabsf(bounds[k]) > radius ? fabsf(bounds[k]) : radius;
			}
			radius = radius < 16.0f ? 16.0f : radius > 512.0f ? 512.0f : radius;
		}
	}
	if (buffer)
	{
		GRASSCUT_RI FS_FreeFile(buffer);
	}
	if (numNames < 64)
	{
		Q_strncpyz(names[numNames], model, sizeof(names[numNames]));
		radii[numNames++] = radius;
	}
	return radius;
}

// The plant models from the map's entity lump (main world only): misc_model (baked into the world by q3map2),
// misc_model_static and misc_model_breakable whose model is a plant (tr_grasscut_plant.h). radius = how far the model
// reaches out (its md3 bounds) x modelscale + 8, how far from the origin a vertex still counts as that plant's.
static void R_GrassCutLoadPlants(const char* text, const int length)
{
	grassCutNumPlants = 0;
	grassCutNumPlantBoxes = 0;
	grassCutNumGrass = 0;
	char key[64] = "", classname[64] = "", model[128] = "";
	float origin[3] = { 0.0f, 0.0f, 0.0f }, scale = 1.0f;
	bool inEntity = false, haveKey = false;
	for (int i = 0; i < length && text[i]; i++)
	{
		if (text[i] == '{')
		{
			inEntity = true;
			haveKey = false;
			classname[0] = model[0] = '\0';
			origin[0] = origin[1] = origin[2] = 0.0f;
			scale = 1.0f;
		}
		else if (text[i] == '}' && inEntity)
		{
			inEntity = false;
			char name[128];
			Q_strncpyz(name, model, sizeof(name));
			char* ext = strrchr(name, '.');
			if (ext)
			{
				*ext = '\0';
			}
			if (!strncmp(classname, "misc_model", 10) && name[0] && R_GrassCutIsPlantShader(name)
				&& grassCutNumPlants < GRASSCUT_MAX_PLANTS)
			{
				grassCutPlant_t* p = &grassCutPlants[grassCutNumPlants++];
				p->x = origin[0];
				p->y = origin[1];
				p->z = origin[2];
				p->radius = R_GrassCutModelRadius(model) * (scale > 0.05f ? scale : 1.0f) + 8.0f;
			}
		}
		else if (text[i] == '"' && inEntity)
		{	// a key or a value
			char token[128];
			int n = 0;
			for (i++; i < length && text[i] && text[i] != '"'; i++)
			{
				if (n < static_cast<int>(sizeof(token)) - 1)
				{
					token[n++] = text[i];
				}
			}
			token[n] = '\0';
			if (!haveKey)
			{
				Q_strncpyz(key, token, sizeof(key));
				haveKey = true;
				continue;
			}
			haveKey = false;
			if (!Q_stricmp(key, "classname"))
			{
				Q_strncpyz(classname, token, sizeof(classname));
			}
			else if (!Q_stricmp(key, "model"))
			{
				Q_strncpyz(model, token, sizeof(model));
			}
			else if (!Q_stricmp(key, "origin"))
			{
				if (sscanf(token, "%f %f %f", &origin[0], &origin[1], &origin[2]) != 3)
				{
					origin[0] = origin[1] = origin[2] = 0.0f; // a broken origin: as if it had none
				}
			}
			else if (!Q_stricmp(key, "modelscale"))
			{
				scale = static_cast<float>(atof(token));
			}
			else if (!Q_stricmp(key, "modelscale_vec"))
			{
				float v[3] = { 1.0f, 1.0f, 1.0f };
				if (sscanf(token, "%f %f %f", &v[0], &v[1], &v[2]) != 3)
				{
					v[0] = v[1] = v[2] = 1.0f; // a broken scale: unscaled
				}
				scale = v[0] > v[1] ? v[0] : v[1];
				scale = scale > v[2] ? scale : v[2];
			}
		}
	}
}

// Baked plants without an entity in the lump (some compilers strip the misc_model entities): the world's surfaces with a
// plant shader are collected (R_GrassCutAddPlantSurface), and R_GrassCutFinishPlantSurfaces joins the ones whose boxes
// touch and share a shader into plants (the box's middle as the origin, its bottom as the foot, its reach as the radius), leaving out the
// ones that hold a plant origin from the entity lump.
static void R_GrassCutAddPlantSurface(const float* mins, const float* maxs, const int shader)
{
	if (grassCutNumPlantBoxes < GRASSCUT_MAX_PLANTS)
	{
		grassCutPlantBoxShader[grassCutNumPlantBoxes] = shader;
		float* b = grassCutPlantBoxes[grassCutNumPlantBoxes++];
		for (int k = 0; k < 3; k++)
		{
			b[k] = mins[k];
			b[k + 3] = maxs[k];
		}
	}
}

static void R_GrassCutFinishPlantSurfaces()
{
	static int group[GRASSCUT_MAX_PLANTS];
	const int n = grassCutNumPlantBoxes;
	for (int i = 0; i < n; i++)
	{
		group[i] = i;
	}
	auto root = [](int i)
	{
		while (group[i] != i)
		{
			group[i] = group[group[i]];
			i = group[i];
		}
		return i;
	};
	for (int i = 0; i < n; i++)
	{	// boxes of one shader that touch (4 units apart at most) join one group
		const float* a = grassCutPlantBoxes[i];
		for (int j = i + 1; j < n; j++)
		{
			const float* b = grassCutPlantBoxes[j];
			if (grassCutPlantBoxShader[i] != grassCutPlantBoxShader[j] || a[0] > b[3] + 4.0f || b[0] > a[3] + 4.0f || a[1] > b[4] + 4.0f || b[1] > a[4] + 4.0f || a[2] > b[5] + 4.0f
				|| b[2] > a[5] + 4.0f)
			{
				continue;
			}
			const int ra = root(i), rb = root(j);
			if (ra != rb)
			{
				group[ra > rb ? ra : rb] = ra < rb ? ra : rb;
			}
		}
	}
	for (int i = 0; i < n; i++)
	{
		group[i] = root(i);
	}
	const int entityPlants = grassCutNumPlants;
	for (int g = 0; g < n; g++)
	{
		float box[6] = { 1.0e9f, 1.0e9f, 1.0e9f, -1.0e9f, -1.0e9f, -1.0e9f };
		bool any = false;
		for (int i = 0; i < n; i++)
		{
			if (group[i] != g)
			{
				continue;
			}
			any = true;
			for (int k = 0; k < 3; k++)
			{
				box[k] = grassCutPlantBoxes[i][k] < box[k] ? grassCutPlantBoxes[i][k] : box[k];
				box[k + 3] = grassCutPlantBoxes[i][k + 3] > box[k + 3] ? grassCutPlantBoxes[i][k + 3] : box[k + 3];
			}
		}
		if (!any || grassCutNumPlants >= GRASSCUT_MAX_PLANTS)
		{
			continue;
		}
		bool known = false;
		for (int p = 0; p < entityPlants && !known; p++)
		{
			const grassCutPlant_t* e = &grassCutPlants[p];
			known = e->x >= box[0] - 8.0f && e->x <= box[3] + 8.0f && e->y >= box[1] - 8.0f && e->y <= box[4] + 8.0f
				&& e->z >= box[2] - 32.0f && e->z <= box[5];
		}
		if (known)
		{
			continue;
		}
		grassCutPlant_t* p = &grassCutPlants[grassCutNumPlants++];
		p->x = (box[0] + box[3]) * 0.5f;
		p->y = (box[1] + box[4]) * 0.5f;
		p->z = box[2];
		const float rx = (box[3] - box[0]) * 0.5f, ry = (box[4] - box[1]) * 0.5f;
		p->radius = (rx > ry ? rx : ry) + 8.0f;
	}
	grassCutNumPlantBoxes = 0;
}

// The plants that can stand in the cuts (their circle touches the box of the live cuts), the GRASSCUT_MAX_NEAR nearest// to the newest cut: x, y, z, radius each. Once a frame for the plant cut.
static int R_GrassCutNearPlants(float plantsNear[][4])
{
	int n = 0;
	float dist2[GRASSCUT_MAX_NEAR];
	grassCutNumNear = 0;
	if (grassCutBox[0] > grassCutBox[2])
	{
		return 0;
	}
	for (int i = 0; i < grassCutNumPlants; i++)
	{
		const grassCutPlant_t* p = &grassCutPlants[i];
		if (p->x + p->radius < grassCutBox[0] || p->x - p->radius > grassCutBox[2] || p->y + p->radius < grassCutBox[1]
			|| p->y - p->radius > grassCutBox[3])
		{
			continue;
		}
		const float dx = p->x - grassCutLastPos[0], dy = p->y - grassCutLastPos[1];
		const float d2 = dx * dx + dy * dy;
		int slot = n;
		if (n == GRASSCUT_MAX_NEAR)
		{	// full: replace the farthest when this one is nearer
			slot = 0;
			for (int k = 1; k < n; k++)
			{
				slot = dist2[k] > dist2[slot] ? k : slot;
			}
			if (d2 >= dist2[slot])
			{
				continue;
			}
		}
		else
		{
			n++;
		}
		plantsNear[slot][0] = p->x;
		plantsNear[slot][1] = p->y;
		plantsNear[slot][2] = p->z;
		plantsNear[slot][3] = p->radius;
		dist2[slot] = d2;
	}
	grassCutNumNear = n;
	return n;
}

// Is the cell at (x, y) cut below height z (up to 128 units below, not below floorZ), live now? Then the lower cut wins
// (cz, ct).
static inline void R_GrassCutPlantSample(const float x, const float y, const float z, const float floorZ, const float now,
	const float regrow, float* cz, float* ct)
{
	const int cx = static_cast<int>(floorf(x / GRASSCUT_CELL));
	const int cy = static_cast<int>(floorf(y / GRASSCUT_CELL));
	const grassCutCell_t* c = R_GrassCutSlot(cx, cy);
	if (c->time <= 0.0f || c->cx != static_cast<float>(cx) || c->cy != static_cast<float>(cy) || c->z >= *cz || c->z >= z
		|| z - c->z > 128.0f || c->z < floorZ)
	{
		return;
	}
	const float age = now - c->time;
	if (age < -0.25f || age >= regrow)
	{
		return;
	}
	*cz = c->z;
	*ct = c->time;
}

// A vertex of a plant model (tr_grasscut_plant.h) at rest, in world space. plantsNear = R_GrassCutNearPlants. When it belongs
// to one of them (the nearest origin within its radius): when the blade went through the plant's stem (within 16 units
// of the origin, up to 96 units over its foot) all of the plant above that cut falls; else it is cut when the blade went
// through the line from it to the plant's origin below it (up to 128 units below, not below the plant's foot): a leaf
// falls where the blade went through it, the part between the cut and the origin stays. Else (a plant not in the entity
// lump) when a cut cell within 24 units of it was cut below it. The lowest such cut counts. What stood above the cut
// falls over (all of a plant the same way, its origin picks it), lies, sinks to stubble 1.5 units over the cut and grows
// back in the last quarter of r_grassCutRegrow.
// Without clippings it is stubble at once. out = where it is drawn, ember = how much it glows (0 - 1). false: not cut
// (out untouched). Same numbers as rd-rend2 PlantCut (generic.glsl).
static bool R_GrassCutPlantVertex(const float* rest, const float now, const float regrow, const float plantsNear[][4],
	const int numNear, float* out, float* ember)
{
	float cz = 1.0e9f, ct = 0.0f;
	int best = -1;
	float bestDist2 = 1.0e18f;
	for (int i = 0; i < numNear; i++)
	{
		const float dx = plantsNear[i][0] - rest[0], dy = plantsNear[i][1] - rest[1];
		const float d2 = dx * dx + dy * dy;
		if (d2 <= plantsNear[i][3] * plantsNear[i][3] && d2 < bestDist2)
		{
			best = i;
			bestDist2 = d2;
		}
	}
	float spot[2] = { rest[0], rest[1] };	// picks the way it falls
	if (best >= 0)
	{
		const float* p = plantsNear[best];
		spot[0] = p[0];
		spot[1] = p[1];
		// the stem (within 16 units of the origin, up to 96 units over the foot) cut: all of the plant above that cut falls
		float sz = 1.0e9f, st = 0.0f;
		for (int i = 0; i < 17; i++)
		{
			const float ang = static_cast<float>(i) * 0.7854f;
			const float d = i == 0 ? 0.0f : i <= 8 ? 8.0f : 16.0f;
			R_GrassCutPlantSample(p[0] + cosf(ang) * d, p[1] + sinf(ang) * d, p[2] + 96.0f, p[2] - 24.0f, now, regrow, &sz, &st);
		}
		if (sz < 1.0e8f)
		{
			if (rest[2] <= sz)
			{
				return false;	// below the cut, it stays
			}
			cz = sz;
			ct = st;
		}
		else
		{	// else along the line to the plant's origin, every 3 units (at most 33 samples, wider for big plants): a leaf
			// falls where the blade went through it
			const float dist = sqrtf(bestDist2);
			int n = static_cast<int>(ceilf(dist / 3.0f));
			n = n < 1 ? 1 : n > 32 ? 32 : n;
			for (int k = 0; k <= n; k++)
			{
				const float t = static_cast<float>(k) / static_cast<float>(n);
				R_GrassCutPlantSample(rest[0] + (p[0] - rest[0]) * t, rest[1] + (p[1] - rest[1]) * t, rest[2], p[2] - 24.0f, now,
					regrow, &cz, &ct);
			}
		}
	}
	else
	{
		static const float dirs[8][2] = { { 1.0f, 0.0f }, { 0.7071f, 0.7071f }, { 0.0f, 1.0f }, { -0.7071f, 0.7071f },
			{ -1.0f, 0.0f }, { -0.7071f, -0.7071f }, { 0.0f, -1.0f }, { 0.7071f, -0.7071f } };
		for (int i = 0; i < 25; i++)
		{	// the vertex's cell, then 8 around at 8, 16 and 24 units
			const float dist = i ? 8.0f * static_cast<float>((i - 1) / 8 + 1) : 0.0f;
			const float* d = dirs[(i + 7) & 7];
			R_GrassCutPlantSample(rest[0] + d[0] * dist, rest[1] + d[1] * dist, rest[2], -1.0e9f, now, regrow, &cz, &ct);
		}
	}
	if (cz > 1.0e8f)
	{
		return false;
	}
	float a = now - ct;
	const float h = rest[2] - cz;
	if (a < 0.0f)
	{
		a = 0.0f;
	}
	float cut[3] = { rest[0], rest[1], cz + (h < 1.5f ? h : 1.5f) };
	const float debris = R_GrassCutDebrisTime();
	if (debris > 0.0f && a < debris + GRASSCUT_FALL_TIME)
	{
		const float r = floorf(spot[0] / 32.0f) * 12.9898f + floorf(spot[1] / 32.0f) * 78.233f;
		const float dx = cosf(r), dy = sinf(r);
		float f = a / GRASSCUT_FALL_TIME;
		f = f >= 1.0f ? 1.0f : f * f;
		const float ang = f * 1.45f;
		const float fallen[3] = { rest[0] + dx * h * sinf(ang), rest[1] + dy * h * sinf(ang), cz + 0.6f + h * cosf(ang) };
		float sink = (a - debris * 0.5f) / (debris * 0.5f + GRASSCUT_FALL_TIME);
		sink = sink < 0.0f ? 0.0f : sink > 1.0f ? 1.0f : sink;
		for (int k = 0; k < 3; k++)
		{
			cut[k] = fallen[k] + (cut[k] - fallen[k]) * sink;
		}
	}
	float g = (a - 0.75f * regrow) / (0.25f * regrow);
	g = g < 0.0f ? 0.0f : g > 1.0f ? 1.0f : g;
	g = g * g * (3.0f - 2.0f * g);
	grassCutPlantVerts++;
	*ember = 0.0f;
	if (grassCutFx > 0.0f && a < GRASSCUT_EMBER_TIME)
	{	// most at the cut
		const float e = 1.0f - a / GRASSCUT_EMBER_TIME;
		float w = 1.0f - h / 24.0f;
		w = w < 0.25f ? 0.25f : w;
		*ember = e * e * (1.0f - g) * w;
	}
	for (int k = 0; k < 3; k++)
	{
		out[k] = rest[k] + (cut[k] - rest[k]) * (1.0f - g);
	}
	return true;
}
