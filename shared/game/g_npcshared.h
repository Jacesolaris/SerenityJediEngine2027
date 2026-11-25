/*
===========================================================================
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2013 - 2015, SerenityJediEngine2027 contributors

This file is part of the SerenityJediEngine2027 source code.

SerenityJediEngine2027 is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License version 2 as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.
===========================================================================
*/

// g_npcshared.h -- adapter for NPC code shared by the SP game and the MP game.
//
// Files in shared/game that include this header are built twice: as C++ in the SP game DLL (SP_GAME)
// and as C in the MP game DLL (_GAME). Write them in plain C that is also valid C++ (NULL, no
// nullptr / constexpr / auto / default arguments / overloads) and use the names below where the two
// games differ. The SP code is the master copy: SP behaviour must stay as it was.
#pragma once

#ifdef SP_GAME
#include "game/b_local.h"
#include "game/g_functions.h"
#include "cgame/cg_local.h"
#include "game/Q3_Interface.h"
#include "game/wp_saber.h"
#include "game/g_navigator.h"
#include "game/g_nav.h"

// SP keeps its NPC globals (NPC, NPCInfo, client, ucmd, enemyVisibility). NPCS holds references to
// them, so shared code can write NPCS.NPC like the MP game does.
struct npcSharedStatic_t
{
	gentity_t*& NPC;
	gNPC_t*& NPCInfo;
	gclient_t*& client;
	usercmd_t& ucmd;
	visibility_t& enemyVisibility;
};
extern npcSharedStatic_t NPCS;

// entity fields (MP keeps these in ent->r)
#define SHR_currentOrigin	currentOrigin
#define SHR_currentAngles	currentAngles
#define SHR_contents		contents
#define SHR_svFlags			svFlags
#define SHR_absmin			absmin
#define SHR_absmax			absmax
#define SHR_mins			mins
#define SHR_maxs			maxs
#define SHR_legsAnimTimer	legsAnimTimer	// playerState_t
#define SHR_torsoAnimTimer	torsoAnimTimer
#define SHR_ORG(ent)			((ent)->currentOrigin)
#define SHR_ANG(ent)			((ent)->currentAngles)
#define SHR_MAXHEALTH(ent)		((ent)->max_health)

// engine calls
#define SHR_Trace(tr, start, mins, maxs, end, pass, mask) \
	gi.trace((tr), (start), (mins), (maxs), (end), (pass), (mask), (EG2_Collision)0, 0)
#define SHR_SoundOnEnt(ent, chan, path)	G_SoundOnEnt((ent), (chan), (path))
#define SHR_Sound(ent, index)			G_Sound((ent), (index))
#define SHR_PlayEffect(name, org, dir)	G_PlayEffect((name), (org), (dir))
#define SHR_PlayEffectUp(name, org)		G_PlayEffect((name), (org))
#define SHR_PlayEffectOnEnt(name, entnum)	G_PlayEffect((name), (entnum))
#define SHR_PlayEffectBolted(name, ent, bolt, org, dir) \
	G_PlayEffect(G_EffectIndex(name), (ent)->playerModel, (bolt), (ent)->s.number, (org))

// cvars (SP cvar_t pointers, MP vmCvar_t structs of the same name): SHR_CVAR(g_x).integer
#define SHR_CVAR(cv)		(*(cv))
#define SHR_CVAR_SET(cv)	((cv) != NULL)
#define SHR_SPSKILL		(g_spskill->integer)
#define SHR_SJE_MODE	(g_SerenityJediEngineMode->integer)	// MP always plays mode 2

#define SHR_HasWeapon(ps, w)	((ps).weapons[(w)])
#define SHR_SetShocked(ent, until)	do { (ent)->s.powerups |= 1 << PW_SHOCKED; (ent)->client->ps.powerups[PW_SHOCKED] = (until); } while (0)
#define SHR_CalcMuzzlePoint(ent, fwd, right, muzzle, lead)	calcmuzzlePoint((ent), (fwd), (muzzle), (lead))	// SJE SP: calcmuzzlePoint(ent, fwd, muzzle, lead)

// navigation and scripts (SP: game-side navigator and ICARUS; MP: engine)
#define SHR_ReachedEnt(actor, target, radius, flying)	STEER::Reached((actor), (target), (radius), !!(flying))
#define SHR_TaskIDPending(ent, tid)						Q3_TaskIDPending((ent), (tid))
#define SHR_TaskIDComplete(ent, tid)					Q3_TaskIDComplete((ent), (tid))
#define SHR_TEAM_PLAYER								TEAM_PLAYER
#define SHR_NPCTALK_DEBUG							(d_npctalk->integer)	// debug print of NPC combat chatter
#define SHR_Printf									gi.Printf
#define SHR_inPVS(p1, p2)							gi.inPVS((p1), (p2))

#define SHR_NAV_GetLastMove(info)		NAV_GetLastMove(info)
#define SHR_TryJumpEnt(goal, xy, z)		NPC_TryJump((goal), (xy), (z))
#define SHR_BSFleeRun()					NPC_BSFlee()	// runs the flee, qtrue while still running away

// fields only SP has (MP: never set)
#define SHR_InsanityTime(npcinfo)	((npcinfo)->insanityTime)
#define SHR_SaberActive(ps)			((ps)->SaberActive())
#define SHR_InFOVSpot				InFOV	// InFOV(spot, from, fromAngles, hFOV, vFOV)
#define SHR_MOD_IMPACT				MOD_IMPACT

// game functions spelled differently
#define SHR_EntIsBreakable(num, breaker)	G_EntIsBreakable((num), (breaker))
#define SHR_HasAnimation(ent, anim)	PM_HasAnimation((ent), (anim))
#define SHR_DeactivateSaber(ent)	WP_DeactivateSaber((ent), qfalse)	// SP: game/wp_saber.h
extern qboolean PM_HasAnimation(const gentity_t* ent, int animation);
extern int PM_AnimLength(int index, animNumber_t anim);
#define SHR_AnimLength(ent, anim)	PM_AnimLength((ent)->client->clientInfo.animFileIndex, (animNumber_t)(anim))
#define SHR_FindItemForWeapon	FindItemForWeapon
#define SHR_ModelIndex			G_ModelIndex
#define SHR_FindItemForAmmo		FindItemForAmmo
#define SHR_ClearLOSEnt(ent)			NPC_ClearLOS(ent)
#define SHR_ClearLOSVec(end)			NPC_ClearLOS(end)
#define SHR_ClearLOSStartEnt(start, ent)	NPC_ClearLOS((start), (ent))
#define SHR_ClearLOSEntEnd(ent, end)	NPC_ClearLOS((ent), (end))

// means of death MP spells differently
#define SHR_MOD_ENERGY	MOD_ENERGY

// Ghoul2 (SP: per-model ghoul2 array + bolts cached at spawn; MP: model 0, bolts looked up by name)
#define SHR_AddBolt(ent, name)					gi.G2API_AddBolt(&(ent)->ghoul2[(ent)->playerModel], (name))
#define SHR_Bolt(ent, field, name)				((ent)->field)
#define SHR_BoltField(ent, field)				((ent)->field)	// MP keeps a few in ent->NPC
#define SHR_GetBoltPosition(ent, bolt, out)		G_GetBoltPosition((ent), (bolt), (out), 0)
#define SHR_GetBoltMatrix(ent, bolt, matrix, angles, origin) \
	gi.G2API_GetBoltMatrix((ent)->ghoul2, (ent)->playerModel, (bolt), &(matrix), (angles), (origin), \
		(cg.time ? cg.time : level.time), NULL, (ent)->s.modelScale)
#define SHR_GiveMeVector(matrix, flags, out)	gi.G2API_GiveMeVectorFromMatrix((matrix), (flags), (out))
#define SHR_SetSurfaceOnOff(ent, name, flags)	gi.G2API_SetSurfaceOnOff(&(ent)->ghoul2[(ent)->playerModel], (name), (flags))
#define SHR_GetSurfaceRenderStatus(ent, name)	gi.G2API_GetSurfaceRenderStatus(&(ent)->ghoul2[(ent)->playerModel], (name))

// callbacks (SP: function enums, MP: pointers)
#define SHR_SET_USE(ent, func)	((ent)->e_UseFunc = useF_##func)
#define SHR_Die(ent, inflictor, attacker, damage, mod)	GEntity_DieFunc((ent), (inflictor), (attacker), (damage), (mod))
#define SHR_DieEx(ent, inflictor, attacker, damage, mod, dflags, hitloc) \
	GEntity_DieFunc((ent), (inflictor), (attacker), (damage), (mod), (dflags), (hitloc))

// NPC_BSFlee: SP runs the flee and says whether the flee goal was reached; MP's returns nothing and its
// civilians never called it there, so MP counts as reached
#define SHR_BSFleeReached()	NPC_BSFlee()
// SP code that means "the player" (g_entities[0]): MP has no single player, so this is left undefined there and
// such a function keeps the MP game's own version (per-function fallback)
#define SHR_PLAYER0	g_entities[0]

// the player an NPC with no team logic goes for: SP has one player
#define SHR_NearestPlayer(ent)	(&g_entities[0])

// pain callback parameters (other = attacker; inflictor / point / mod / hit_loc exist only in SP) and the
// arguments for passing them on to NPC_Pain
#define SHR_PAIN_PARAMS gentity_t* self, gentity_t* inflictor, gentity_t* other, const vec3_t point, \
	const int damage, int mod, int hit_loc
#define SHR_PAIN_PASS self, inflictor, other, point, damage, mod
#define SHR_PAIN_MOD mod
#define SHR_PAIN_HITLOC hit_loc
#define SHR_PAIN_POINT point
#define SHR_PAIN_ATTACKER other

#else // MP game

#include "game/b_local.h"
#include "game/g_nav.h"
#include <stdbool.h>	// SP code uses C++ bool; C99 has it too

#define SHR_currentOrigin	r.currentOrigin
#define SHR_currentAngles	r.currentAngles
#define SHR_contents		r.contents
#define SHR_svFlags			r.svFlags
#define SHR_absmin			r.absmin
#define SHR_absmax			r.absmax
#define SHR_mins			r.mins
#define SHR_maxs			r.maxs
#define SHR_legsAnimTimer	legsTimer
#define SHR_torsoAnimTimer	torsoTimer
#define SHR_ORG(ent)			((ent)->r.currentOrigin)
#define SHR_ANG(ent)			((ent)->r.currentAngles)
#define SHR_MAXHEALTH(ent)		((ent)->client->pers.maxHealth)

#define SHR_Trace(tr, start, mins, maxs, end, pass, mask) \
	trap->Trace((tr), (start), (mins), (maxs), (end), (pass), (mask), qfalse, 0, 0)
#define SHR_SoundOnEnt(ent, chan, path)	G_Sound((ent), (chan), G_SoundIndex(path))
#define SHR_Sound(ent, index)			G_Sound((ent), CHAN_AUTO, (index))
#define SHR_PlayEffect(name, org, dir)	G_PlayEffectID(G_EffectIndex(name), (float*)(org), (float*)(dir))
#define SHR_PlayEffectUp(name, org)		G_PlayEffectID(G_EffectIndex(name), (org), (float[3]){ 0.0f, 0.0f, 1.0f })
#define SHR_PlayEffectOnEnt(name, entnum)	G_PlayEffectID(G_EffectIndex(name), g_entities[(entnum)].r.currentOrigin, (float[3]){ 0.0f, 0.0f, 1.0f })
#define SHR_PlayEffectBolted(name, ent, bolt, org, dir)	G_PlayEffectID(G_EffectIndex(name), (org), (dir))

#define SHR_CVAR(cv)		(cv)
#define SHR_CVAR_SET(cv)	1
#define SHR_SPSKILL		(g_npcspskill.integer)
#define SHR_SJE_MODE	2

#define SHR_HasWeapon(ps, w)	((ps).stats[STAT_WEAPONS] & (1 << (w)))
#define SHR_SetShocked(ent, until)	((ent)->client->ps.electrifyTime = (until))	// MP shows the shock this way
#ifndef ARMOR_EFFECT_TIME
#define ARMOR_EFFECT_TIME	500	// SP wp_saber.h
#endif
#define SHR_CalcMuzzlePoint(ent, fwd, right, muzzle, lead)	calcmuzzlePoint((ent), (fwd), (right), (muzzle))

#define SHR_ReachedEnt(actor, target, radius, flying) \
	NAV_HitNavGoal((actor)->r.currentOrigin, (actor)->r.mins, (actor)->r.maxs, (target)->r.currentOrigin, (radius), (flying))
#define SHR_TaskIDPending(ent, tid)	trap->ICARUS_TaskIDPending((sharedEntity_t*)(ent), (tid))
#define SHR_TaskIDComplete(ent, tid)	trap->ICARUS_TaskIDComplete((sharedEntity_t*)(ent), (tid))
#define SHR_TEAM_PLAYER				NPCTEAM_PLAYER
#define SHR_NPCTALK_DEBUG			0	// MP has no d_npctalk cvar
#define SHR_Printf					Com_Printf
#define SHR_inPVS(p1, p2)			trap->InPVS((p1), (p2))

#define SHR_NAV_GetLastMove(info)		NAV_GetLastMove(&(info))
#define SHR_TryJumpEnt(goal, xy, z)		NPC_TryJump_Gent((goal), (xy), (z))
#define SHR_BSFleeRun()					(NPC_BSFlee(), qtrue)

#define SHR_InsanityTime(npcinfo)	0
#define SHR_SaberActive(ps)			(!BG_SabersOff(ps))
#define SHR_InFOVSpot				InFOV3
#define SHR_MOD_IMPACT				MOD_CRUSH
qboolean BG_SabersOff(const playerState_t* ps);
qboolean NPC_TryJump_Gent(gentity_t* goal, float max_xy_dist, float max_z_diff);
int NPC_GetEntsNearBolt(int* radius_ents, float radius, int boltIndex, vec3_t bolt_org);
void Saboteur_Decloak(gentity_t* self, int uncloak_time);
qboolean NPC_TryJump(void);
void G_AddVoiceEvent(const gentity_t* self, int event, int speak_debounce_time);
void G_UcmdMoveForDir(const gentity_t* self, usercmd_t* cmd, vec3_t dir);
qboolean G_EntIsBreakable(int entityNum);
void G_SoundOnEnt(gentity_t* ent, soundChannel_t channel, const char* sound_path);

#define SHR_EntIsBreakable(num, breaker)	G_EntIsBreakable(num)	// MP has no breaker parameter
#define SHR_HasAnimation(ent, anim)	BG_HasAnimation((ent)->localAnimIndex, (anim))
#define SHR_DeactivateSaber(ent)	WP_DeactivateSaber(ent)
void WP_DeactivateSaber(const gentity_t* self);	// as the MP NPC files declare it
int G_GetHitLocation(const gentity_t* target, vec3_t ppoint);
#define SHR_AnimLength(ent, anim)	BG_AnimLength((ent)->localAnimIndex, (anim))
#define SHR_FindItemForWeapon	BG_FindItemForWeapon
#define SHR_ModelIndex			G_model_index
#define SHR_FindItemForAmmo		BG_FindItemForAmmo
gitem_t* BG_FindItemForAmmo(const ammo_t ammo);

#define SHR_ClearLOSEnt(ent)			NPC_ClearLOS4(ent)
#define SHR_ClearLOSVec(end)			NPC_ClearLOS5(end)
#define SHR_ClearLOSStartEnt(start, ent)	NPC_ClearLOS3((start), (ent))
#define SHR_ClearLOSEntEnd(ent, end)	NPC_ClearLOS2((ent), (end))

#define SHR_MOD_ENERGY	MOD_BRYAR_PISTOL

#define SHR_AddBolt(ent, name)					trap->G2API_AddBolt((ent)->ghoul2, 0, (name))
#define SHR_Bolt(ent, field, name)				trap->G2API_AddBolt((ent)->ghoul2, 0, (name))
#define SHR_BoltField(ent, field)				((ent)->NPC->field)
#define SHR_GetBoltPosition(ent, bolt, out)		G_GetBoltPosition((ent), (bolt), (out), 0)
#define SHR_GetBoltMatrix(ent, bolt, matrix, angles, origin) \
	trap->G2API_GetBoltMatrix((ent)->ghoul2, 0, (bolt), &(matrix), (angles), (origin), level.time, NULL, (ent)->modelScale)
#define SHR_GiveMeVector(matrix, flags, out)	BG_GiveMeVectorFromMatrix(&(matrix), (flags), (out))
#define SHR_SetSurfaceOnOff(ent, name, flags)	NPC_SetSurfaceOnOff((ent), (name), (flags))
#define SHR_GetSurfaceRenderStatus(ent, name)	trap->G2API_GetSurfaceRenderStatus((ent)->ghoul2, 0, (name))

#define SHR_SET_USE(ent, func)	((ent)->use = (func))
#define SHR_Die(ent, inflictor, attacker, damage, mod) \
	do { if ((ent)->die) (ent)->die((ent), (inflictor), (attacker), (damage), (mod)); } while (0)
#define SHR_DieEx(ent, inflictor, attacker, damage, mod, dflags, hitloc)	SHR_Die(ent, inflictor, attacker, damage, mod)

#define SHR_BSFleeReached()	qtrue
// MP co-op: the closest living, connected, non-spectator player (may be NULL)
#define SHR_NearestPlayer(ent)	FindClosestPlayer(SHR_ORG(ent), -1)

#define SHR_PAIN_PARAMS gentity_t* self, gentity_t* other, const int damage
#define SHR_PAIN_PASS self, other, damage
#define SHR_PAIN_MOD gPainMOD
#define SHR_PAIN_HITLOC gPainHitLoc
#define SHR_PAIN_POINT gPainPoint
#define SHR_PAIN_ATTACKER other

#endif
