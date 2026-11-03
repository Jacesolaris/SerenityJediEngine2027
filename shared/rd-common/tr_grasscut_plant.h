/*
===========================================================================
Grass cutting, plant models (2026-11-02, DH): plant models (model shaders under models/map_objects/ named like a plant:
grass, fern, vine, plant, bush, shrub, weed, reed, frond; not trees or leaves, except tree vines) are cut like the
surface-sprite grass, on the same grid (tr_grasscut.h): the ones q3map2 bakes into the world (misc_model) and plain md3
entities (misc_model_static / misc_model_breakable). A shader whose name matches gets shader_t grassCutPlant when it is
created. rd-vanilla moves the vertices on the CPU (tr_surfacesprites.cpp RB_GrassCutPlants), rd-rend2 in the vertex
shaders (generic / velocity / fogpass.glsl PlantCut, in world space); rend2 draws plant shaders with the deform variant
of those programs even without a sway deform (tr_local.h ShaderUsesGPUDeforms).

Header-only, included by each renderer's tr_shader.cpp.
===========================================================================
*/

#pragma once

#include <cstring>

static inline bool R_GrassCutIsPlantShader(const char* name)
{
	static const char prefix[] = "models/map_objects/";
	char lower[64];
	size_t i = 0;
	for (; name[i] && i < sizeof(lower) - 1; i++)
	{
		lower[i] = name[i] >= 'A' && name[i] <= 'Z' ? static_cast<char>(name[i] - 'A' + 'a') : name[i] == '\\' ? '/' : name[i];
	}
	lower[i] = '\0';
	if (strncmp(lower, prefix, sizeof(prefix) - 1))
	{
		return false;
	}
	// the last part of the name (models/map_objects/yavin/grass_tall_b -> grass_tall_b)
	const char* n = strrchr(lower, '/') + 1;
	static const char* const plants[] = { "grass", "fern", "vine", "plant", "bush", "shrub", "weed", "reed", "frond" };
	bool plant = false;
	for (const char* p : plants)
	{
		plant = plant || strstr(n, p);
	}
	if (!plant || strstr(n, "leaves") || strstr(n, "leaf"))
	{
		return false;
	}
	return !strstr(n, "tree") || strstr(n, "vine"); // tree vines yes, trunks and canopies no
}
