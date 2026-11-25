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

// NPC for both games. SP's version is built on C++ classes that the MP game rewrote in C, so this file
// still holds both complete versions; was code/game/NPC.cpp and codemp/game/NPC.c.

#include "game/g_npcshared.h"

#ifdef SP_GAME

//
// NPC.cpp - generic functions
//

#include "game/b_local.h"
#include "threads/q_profile.h"
#include "game/anims.h"
#include "game/g_functions.h"
#include "game/Q3_Interface.h"
#include "game/g_vehicles.h"
#include "game/g_pazaak.h"

extern vec3_t playerMins;
extern vec3_t playerMaxs;
extern void PM_SetTorsoAnimTimer(gentity_t* ent, int* torsoAnimTimer, int time);
extern void PM_SetLegsAnimTimer(gentity_t* ent, int* legsAnimTimer, int time);
extern void NPC_BSNoClip();
extern void G_AddVoiceEvent(const gentity_t* self, int event, int speak_debounce_time);
extern void NPC_ApplyRoff();
extern void NPC_TempLookTarget(const gentity_t* self, int lookEntNum, int minLookTime, int maxLookTime);
extern qboolean NPC_CheckLookTarget(const gentity_t* self);
extern void NPC_SetLookTarget(const gentity_t* self, int entNum, int clear_time);
extern void Mark1_dying(gentity_t* self);
extern void NPC_BSCinematic();
extern int GetTime(int lastTime);
extern void G_CheckCharmed(gentity_t* self);
extern qboolean Boba_Flying(const gentity_t* self);
extern qboolean RT_Flying(const gentity_t* self);
extern qboolean Jedi_CultistDestroyer(const gentity_t* self);
extern void Boba_Update();
extern bool Boba_Flee();
extern bool Boba_Tactics();
extern void bubble_shield_update();
extern qboolean PM_LockedAnim(int anim);
extern void npc_check_speak(gentity_t* speaker_npc);

extern cvar_t* g_dismemberment;
extern cvar_t* g_saberRealisticCombat;
extern cvar_t* g_corpseRemovalTime;

//Local Variables
// ai debug cvars
cvar_t* debugNPCAI; // used to print out debug info about the bot AI
cvar_t* debugNPCFreeze; // set to disable bot ai and temporarily freeze them in place
cvar_t* debugNPCName;
cvar_t* d_saberCombat;
cvar_t* d_JediAI;
cvar_t* d_noGroupAI;
cvar_t* d_asynchronousGroupAI;
cvar_t* d_slowmodeath;
cvar_t* g_longLeapCinematic;
cvar_t* d_slowmoaction;
cvar_t* d_saberInfo;
cvar_t* d_combatinfo;
cvar_t* d_blockinfo;
cvar_t* d_attackinfo;
cvar_t* d_npctalk;

extern qboolean stop_icarus;

gentity_t* NPC;
gNPC_t* NPCInfo;
gclient_t* client;
usercmd_t ucmd;
visibility_t enemyVisibility;

// NPC code shared with the MP game (shared/game/*.c) reaches the globals above as NPCS.NPC etc.
#include "game/g_npcshared.h"
npcSharedStatic_t NPCS = { NPC, NPCInfo, client, ucmd, enemyVisibility };

void NPC_SetAnim(gentity_t* ent, int setAnimParts, int anim, int setAnimFlags, int i_blend);
static bState_t G_CurrentBState(gNPC_t* gNPC);

extern int eventClearTime;

extern void GM_Dying(gentity_t* self);
extern void droideka_dying(gentity_t* self);

extern qboolean G_EntIsBreakable(int entityNum, const gentity_t* breaker);

qboolean NPC_EntityIsBreakable(const gentity_t* ent)
{
	if (ent
		&& ent->inuse
		&& ent->takedamage
		&& ent->classname
		&& ent->classname[0]
		&& ent->s.eType != ET_INVISIBLE
		&& ent->s.eType != ET_PLAYER
		&& !ent->client
		&& G_EntIsBreakable(ent->s.number, ent)
		&& !EntIsGlass(ent)
		&& ent->health > 0
		&& !(ent->svFlags & SVF_PLAYER_USABLE))
	{
		return qtrue;
	}

	return qfalse;
}

qboolean NPC_IsAlive(const gentity_t* self, const gentity_t* npc)
{
	if (!npc)
	{
		return qfalse;
	}

	if (self && self->client && NPC_EntityIsBreakable(npc) && npc->health > 0)
	{
		return qtrue;
	}

	if (npc->s.eType == ET_PLAYER)
	{
		if (npc->health <= 0 && (npc->client && npc->client->ps.stats[STAT_HEALTH] <= 0))
		{
			return qfalse;
		}
	}
	else
	{
		return qfalse;
	}

	return qtrue;
}

static void CorpsePhysics(gentity_t* self)
{
	// run the bot through the server like it was a real client
	memset(&ucmd, 0, sizeof ucmd);
	ClientThink(self->s.number, &ucmd);
	VectorCopy(self->s.origin, self->s.origin2);

	if (self->client->NPC_class == CLASS_GALAKMECH)
	{
		GM_Dying(self);
	}

	if (self->client->NPC_class == CLASS_DROIDEKA)
	{
		droideka_dying(self);
	}

	//FIXME: match my pitch and roll for the slope of my groundPlane
	if (self->client->ps.groundEntityNum != ENTITYNUM_NONE && !(self->flags & FL_DISINTEGRATED))
	{
		//on the ground
		//FIXME: check 4 corners
		pitch_roll_for_slope(self);
	}

	if (eventClearTime == level.time + ALERT_CLEAR_TIME)
	{
		//events were just cleared out so add me again
		if (!(self->client->ps.eFlags & EF_NODRAW))
		{
			AddSightEvent(self->enemy, self->currentOrigin, 384, AEL_DISCOVERED);
		}
	}

	if (level.time - self->s.time > 3000)
	{
		//been dead for 3 seconds
		if (g_dismemberment->integer < 1 && !g_saberRealisticCombat->integer)
		{
			//can't be dismembered once dead
			if (self->client->NPC_class != CLASS_PROTOCOL)
			{
				self->client->dismembered = true;
			}
		}
	}

	if (level.time - self->s.time > 1000)
	{
		//don't turn "nonsolid" until about 1 second after actual death
		if (self->client->NPC_class != CLASS_MARK1 && self->client->NPC_class != CLASS_INTERROGATOR)
			// The Mark1 & Interrogator stays solid.
		{
			self->contents = CONTENTS_CORPSE;
		}

		if (self->message)
		{
			self->contents |= CONTENTS_TRIGGER;
		}
	}
}

/*
----------------------------------------
NPC_RemoveBody

Determines when it's ok to ditch the corpse
----------------------------------------
*/
constexpr auto REMOVE_DISTANCE = 128;
#define REMOVE_DISTANCE_SQR (REMOVE_DISTANCE * REMOVE_DISTANCE)
extern qboolean InFOVFromPlayerView(const gentity_t* ent, int hFOV, int vFOV);

// corpse removal: true while the player could see this body (or body part), so it is only removed out of view
qboolean G_CorpseSeenByPlayer(const gentity_t* ent)
{
	gentity_t* player = &g_entities[0];
	if (!ent || !player->inuse || !player->client)
	{
		return qfalse;
	}
	if (DistanceSquared(player->currentOrigin, ent->currentOrigin) <= REMOVE_DISTANCE_SQR)
	{
		return qtrue;
	}
	if (InFOVFromPlayerView(ent, 110, 90) && NPC_ClearLOS(player, ent->currentOrigin)) // generous FOV check
	{
		return qtrue;
	}
	return qfalse;
}

qboolean G_OkayToRemoveCorpse(gentity_t* self)
{
	//if we're still on a vehicle, we won't remove ourselves until we get ejected
	if (self->client && self->client->NPC_class != CLASS_VEHICLE && self->s.m_iVehicleNum != 0)
	{
		Vehicle_t* p_veh = g_entities[self->s.m_iVehicleNum].m_pVehicle;
		if (p_veh)
		{
			if (!p_veh->m_pVehicleInfo->Eject(p_veh, self, qtrue))
			{
				//dammit, still can't get off the vehicle...
				return qfalse;
			}
		}
		else
		{
			assert(0);
#ifndef FINAL_BUILD
			Com_Printf(S_COLOR_RED"ERROR: Dead pilot's vehicle removed while corpse was riding it (pilot: %s)???\n", self->targetname);
#endif
		}
	}

	if (self->message)
	{
		//I still have a key
		return qfalse;
	}

	if (IIcarusInterface::GetIcarus()->IsRunning(self->m_iIcarusID))
	{
		//still running a script
		return qfalse;
	}

	if (self->activator
		&& self->activator->client
		&& (self->activator->client->ps.eFlags & EF_HELD_BY_RANCOR
			|| self->activator->client->ps.eFlags & EF_HELD_BY_SAND_CREATURE
			|| self->activator->client->ps.eFlags & EF_HELD_BY_WAMPA))
	{
		//still holding a victim?
		return qfalse;
	}

	if (self->client
		&& (self->client->ps.eFlags & EF_HELD_BY_RANCOR
			|| self->client->ps.eFlags & EF_HELD_BY_SAND_CREATURE
			|| self->client->ps.eFlags & EF_HELD_BY_WAMPA))
	{
		//being held by a creature
		return qfalse;
	}

	if (self->client
		&& self->client->ps.heldByClient < ENTITYNUM_WORLD)
	{
		//being dragged
		return qfalse;
	}

	//okay, well okay to remove us...?
	return qtrue;
}

void NPC_RemoveBody(gentity_t* ent)
{
	ent->nextthink = level.time + FRAMETIME / 2;

	//run physics at 20fps
	CorpsePhysics(ent);

	if (ent->NPC->nextBStateThink <= level.time)
	{
		//run logic at 10 fps
		if (ent->m_iIcarusID != IIcarusInterface::ICARUS_INVALID && !stop_icarus)
		{
			IIcarusInterface::GetIcarus()->Update(ent->m_iIcarusID);
		}
		ent->NPC->nextBStateThink = level.time + FRAMETIME;

		if (!G_OkayToRemoveCorpse(ent))
		{
			return;
		}

		// I don't consider this a hack, it's creative coding . . .
		// I agree, very creative... need something like this for ATST and GALAKMECH too!
		if (ent->client->NPC_class == CLASS_MARK1)
		{
			Mark1_dying(ent);
		}

		// Since these blow up, remove the bounding box.
		if (ent->client->NPC_class == CLASS_REMOTE
			|| ent->client->NPC_class == CLASS_SENTRY
			|| ent->client->NPC_class == CLASS_PROBE
			|| ent->client->NPC_class == CLASS_INTERROGATOR
			|| ent->client->NPC_class == CLASS_PROBE
			|| ent->client->NPC_class == CLASS_MARK2)
		{
			G_FreeEntity(ent);
			return;
		}

		//FIXME: don't ever inflate back up?
		ent->maxs[2] = ent->client->renderInfo.eyePoint[2] - ent->currentOrigin[2] + 4;
		if (ent->maxs[2] < -8)
		{
			ent->maxs[2] = -8;
		}

		if (ent->NPC->aiFlags & NPCAI_HEAL_ROSH)
		{
			//kothos twins' bodies are never removed
			return;
		}

		if (ent->client->NPC_class == CLASS_GALAKMECH)
		{
			//never disappears
			return;
		}

		if (ent->NPC && ent->NPC->timeOfDeath <= level.time)
		{
			ent->NPC->timeOfDeath = level.time + 1000;
			// only remove bodies the player can't see (any team)
			ent->nextthink = level.time + FRAMETIME; // try back in a second
			if (G_CorpseSeenByPlayer(ent))
			{
				return;
			}
			if (ent->enemy)
			{
				if (ent->client && ent->client->ps.saberEntityNum > 0 && ent->client->ps.saberEntityNum <
					ENTITYNUM_WORLD)
				{
					gentity_t* saberent = &g_entities[ent->client->ps.saberEntityNum];
					if (saberent)
					{
						G_FreeEntity(saberent);
					}
				}
				G_FreeEntity(ent);
			}
		}
	}
}

/*
----------------------------------------
NPC_RemoveBody

Determines when it's ok to ditch the corpse
----------------------------------------
*/

static int BodyRemovalPadTime(const gentity_t* ent)
{
	int time = 0;

	if (!ent || !ent->client)
		return 0;

	// team no longer indicates species/race, so in this case we'd use NPC_class, but
	switch (ent->client->NPC_class)
	{
	case CLASS_MOUSE:
	case CLASS_GONK:
	case CLASS_R2D2:
	case CLASS_R5D2:
		//case CLASS_PROTOCOL:
	case CLASS_MARK1:
	case CLASS_MARK2:
	case CLASS_PROBE:
	case CLASS_SEEKER:
	case CLASS_REMOTE:
	case CLASS_SENTRY:
	case CLASS_INTERROGATOR:
		time = 0;
		break;
	default:
		// never go away
		if (g_corpseRemovalTime->integer <= 0)
		{
			time = Q3_INFINITE;
		}
		else
		{
			time = g_corpseRemovalTime->integer * 1000;
		}
		break;
	}

	return time;
}

/*
----------------------------------------
NPC_RemoveBodyEffect

Effect to be applied when ditching the corpse
----------------------------------------
*/

#else // MP game

//
// NPC.cpp - generic functions
//
#include "game/b_local.h"
#include "game/anims.h"
#include "game/say.h"
#include "icarus/Q3_Interface.h"

extern vec3_t playerMins;
extern vec3_t playerMaxs;
extern void G_SoundOnEnt(gentity_t* ent, soundChannel_t channel, const char* sound_path);
extern void NPC_BSNoClip(void);
extern void G_AddVoiceEvent(const gentity_t* self, int event, int speak_debounce_time);
extern void NPC_ApplyRoff(void);
extern void NPC_TempLookTarget(const gentity_t* self, int lookEntNum, int minLookTime, int maxLookTime);
extern qboolean NPC_CheckLookTarget(const gentity_t* self);
extern void NPC_SetLookTarget(const gentity_t* self, int entNum, int clear_time);
extern void Mark1_dying(gentity_t* self);
extern void NPC_BSCinematic(void);
extern int GetTime(int lastTime);
extern void G_CheckCharmed(gentity_t* self);
extern qboolean Jedi_CultistDestroyer(const gentity_t* self);
extern void NPC_BSGM_Default(void);
extern qboolean Boba_Flying(const gentity_t* self);
extern void Boba_Update(void);
extern qboolean Boba_Flee(void);
extern qboolean Boba_TacticsLocked(void);

//Local Variables
npcStatic_t NPCS;
gNPC_t* NPCInfo;
gclient_t* client;
usercmd_t ucmd;
visibility_t enemyVisibility;

extern vmCvar_t g_saberRealisticCombat;

void NPC_SetAnim(gentity_t* ent, int setAnimParts, int anim, int setAnimFlags);
static bState_t G_CurrentBState(gNPC_t* gNPC);
void pitch_roll_for_slope(gentity_t* forwhom, vec3_t pass_slope);
extern void GM_Dying(gentity_t* self);

extern int eventClearTime;

extern void NPC_BehaviorSet_HazardTroop(int b_state);

extern void npc_check_speak(gentity_t* speaker_npc); // NPC_AI_Stormtrooper.c (SP: idle combat chatter, g_Advancedaitalk)
qboolean NPC_IsTrooper(const gentity_t* self)
{
	return self && self->NPC && self->s.weapon && !!(self->NPC->scriptFlags & SCF_NO_GROUPS);
}

// SP NPC.cpp: the big classes (the SP Jedi AI won't make them do flips or roll dodges)
qboolean NPC_IsOversized(const gentity_t* self)
{
	switch (self->client->NPC_class)
	{
	case CLASS_WOOKIE:
	case CLASS_SBD:
	case CLASS_ASSASSIN_DROID:
	case CLASS_HAZARD_TROOPER:
	case CLASS_ROCKETTROOPER:
	case CLASS_SABER_DROID:
	case CLASS_VADER:
		return qtrue;
	default:
		break;
	}

	return qfalse;
}

static void CorpsePhysics(gentity_t* self)
{
	// run the bot through the server like it was a real client
	memset(&NPCS.ucmd, 0, sizeof NPCS.ucmd);
	ClientThink(self->s.number, &NPCS.ucmd);
	VectorCopy(self->s.origin, self->s.origin2);

	if (self->client->NPC_class == CLASS_GALAKMECH)
	{
		GM_Dying(self);
	}

	/*if (self->client->pers.botclass == BCLASS_GALAKMECH)
	{
		GM_Dying(self);
	}*/
	//FIXME: match my pitch and roll for the slope of my groundPlane
	if (self->client->ps.groundEntityNum != ENTITYNUM_NONE && !(self->s.eFlags & EF_DISINTEGRATION))
	{
		//on the ground
		//FIXME: check 4 corners
		pitch_roll_for_slope(self, NULL);
	}

	if (eventClearTime == level.time + ALERT_CLEAR_TIME)
	{
		//events were just cleared out so add me again
		if (!(self->client->ps.eFlags & EF_NODRAW))
		{
			AddSightEvent(self->enemy, self->r.currentOrigin, 384, AEL_DISCOVERED, 0.0f);
		}
	}

	if (level.time - self->s.time > 3000)
	{
		//been dead for 3 seconds
		if (g_dismember.integer < 11381138 && !g_saberRealisticCombat.integer)
		{
			//can't be dismembered once dead
			if (self->client->NPC_class != CLASS_PROTOCOL)
			{
				//self->client->dismembered = qtrue;
			}
		}
	}

	if (level.time - self->s.time > 1000)
		//if (self->client->respawnTime < (level.time+ 3000))
	{
		//don't turn "nonsolid" until about 1 second after actual death
		if (self->client->NPC_class != CLASS_MARK1 && self->client->NPC_class != CLASS_INTERROGATOR)
			// The Mark1 & Interrogator stays solid.
		{
			self->r.contents = CONTENTS_CORPSE;
		}

		if (self->message)
		{
			self->r.contents |= CONTENTS_TRIGGER;
		}
	}
}

/*
----------------------------------------
NPC_RemoveBody

Determines when it's ok to ditch the corpse
----------------------------------------
*/
#define REMOVE_DISTANCE		128
#define REMOVE_DISTANCE_SQR (REMOVE_DISTANCE * REMOVE_DISTANCE)

extern float DistancetoClosestPlayer(vec3_t position, int enemy_team);
extern qboolean InPlayersFOV(vec3_t position, int enemy_team, int h_fov, int v_fov, qboolean check_clear_los);

qboolean G_OkayToRemoveCorpse(gentity_t* self)
{
	//racc - check to see if it's ok to remove this body.
	//if we're still on a vehicle, we won't remove ourselves until we get ejected
	if (self->client && self->client->NPC_class != CLASS_VEHICLE && self->s.m_iVehicleNum != 0)
	{
		Vehicle_t* p_veh = g_entities[self->s.m_iVehicleNum].m_pVehicle;
		if (p_veh)
		{
			// eject this corpse (as SP does), not the pilot: a dead passenger/droid ejected the live pilot,
			// and with no pilot it passed NULL
			if (!p_veh->m_pVehicleInfo->Eject(p_veh, (bgEntity_t*)self, qtrue))
			{
				//dammit, still can't get off the vehicle...
				return qfalse;
			}
		}
		else
		{
			//racc - this is bad.  We're on a vehicle, but the vehicle doesn't exist.
			//assert(0);
#ifndef FINAL_BUILD
			Com_Printf(S_COLOR_RED"ERROR: Dead pilot's vehicle removed while corpse was riding it (pilot: %s)???\n", self->targetname);
#endif
		}
	}

	if (self->message)
	{
		//I still have a key
		return qfalse;
	}

	if (trap->ICARUS_IsRunning(self->s.number))
	{
		//still running a script
		return qfalse;
	}

	if (self->activator
		&& self->activator->client
		&& self->activator->client->ps.eFlags2 & EF2_HELD_BY_MONSTER)
	{
		//still holding a victim?
		return qfalse;
	}

	if (self->client
		&& self->client->ps.eFlags2 & EF2_HELD_BY_MONSTER)
	{
		//being held by a creature
		return qfalse;
	}

	if (self->client
		&& self->client->ps.heldByClient)
	{
		//being dragged
		return qfalse;
	}

	//okay, well okay to remove us...?
	return qtrue;
}

void NPC_RemoveBody(gentity_t* ent)
{
	ent->nextthink = level.time + FRAMETIME / 2;

	//run physics at 20fps
	CorpsePhysics(ent);

	if (ent->NPC->nextBStateThink <= level.time)
	{
		//racc - run logic at 20fps
		trap->ICARUS_MaintainTaskManager(ent->s.number);
		ent->NPC->nextBStateThink = level.time + FRAMETIME;

		if (!G_OkayToRemoveCorpse(ent))
		{
			return;
		}
		if (ent->client->NPC_class == CLASS_MARK1)
		{
			Mark1_dying(ent);
		}

		// Since these blow up, remove the bounding box.
		if (ent->client->NPC_class == CLASS_REMOTE
			|| ent->client->NPC_class == CLASS_SENTRY
			|| ent->client->NPC_class == CLASS_PROBE
			|| ent->client->NPC_class == CLASS_INTERROGATOR
			|| ent->client->NPC_class == CLASS_PROBE
			|| ent->client->NPC_class == CLASS_MARK2)
		{
			G_FreeEntity(ent);
			return;
		}
		ent->r.maxs[2] = ent->client->renderInfo.eyePoint[2] - ent->r.currentOrigin[2] + 4;
		if (ent->r.maxs[2] < -8)
		{
			ent->r.maxs[2] = -8;
		}

		if (ent->NPC->aiFlags & NPCAI_HEAL_ROSH)
		{
			//kothos twins' bodies are never removed
			return;
		}

		if (ent->client->NPC_class == CLASS_GALAKMECH)
		{
			//never disappears
			return;
		}

		//if (self->client->pers.botclass == BCLASS_GALAKMECH)
		//{//never disappears
		//	return;
		//}
		if (ent->NPC && ent->NPC->timeOfDeath <= level.time)
		{
			ent->NPC->timeOfDeath = level.time + 1000;
			if (ent->client->playerTeam == NPCTEAM_ENEMY || ent->client->NPC_class == CLASS_PROTOCOL)
			{
				ent->nextthink = level.time + FRAMETIME; // try back in a second

				if (DistancetoClosestPlayer(ent->r.currentOrigin, -1) <= REMOVE_DISTANCE)
				{
					return;
				}

				if (InPlayersFOV(ent->r.currentOrigin, -1, 110, 90, qtrue))
				{
					//a player sees the body, delay removal.
					return;
				}
			}
			//if ( self->enemy )
			{
				if (ent->client && ent->client->ps.saberEntityNum > 0 && ent->client->ps.saberEntityNum <
					ENTITYNUM_WORLD)
				{
					gentity_t* saberent = &g_entities[ent->client->ps.saberEntityNum];
					if (saberent)
					{
						G_FreeEntity(saberent);
					}
				}
				G_FreeEntity(ent);
			}
		}
	}
}

/*
----------------------------------------
NPC_RemoveBody

Determines when it's ok to ditch the corpse
----------------------------------------
*/

static int BodyRemovalPadTime(const gentity_t* ent)
{
	int time = 0;

	if (!ent || !ent->client)
		return 0;

	// team no longer indicates species/race, so in this case we'd use NPC_class, but
	switch (ent->client->NPC_class)
	{
	case CLASS_MOUSE:
	case CLASS_GONK:
	case CLASS_R2D2:
	case CLASS_R5D2:
	case CLASS_MARK1:
	case CLASS_MARK2:
	case CLASS_PROBE:
	case CLASS_SEEKER:
	case CLASS_REMOTE:
	case CLASS_SENTRY:
	case CLASS_INTERROGATOR:
		time = 0;
		break;
	default:
		// never go away
		if (g_corpseRemovalTime.integer <= 0)
		{
			time = Q3_INFINITE;
		}
		else
		{
			time = g_corpseRemovalTime.integer * 1000;
		}
		break;
	}
	return time;
}

/*
----------------------------------------
NPC_RemoveBodyEffect

Effect to be applied when ditching the corpse
----------------------------------------
*/

#endif
static void NPC_RemoveBodyEffect(void)
{
	if (!NPCS.NPC || !NPCS.NPC->client || NPCS.NPC->s.eFlags & EF_NODRAW)
		return;

	// stub code
	switch (NPCS.NPC->client->NPC_class)
	{
	case CLASS_PROBE:
	case CLASS_SEEKER:
	case CLASS_REMOTE:
	case CLASS_SENTRY:
	case CLASS_GONK:
	case CLASS_R2D2:
	case CLASS_R5D2:
		//case CLASS_PROTOCOL:
	case CLASS_MARK1:
	case CLASS_MARK2:
	case CLASS_INTERROGATOR:
	case CLASS_ATST:
		break;
	default:
		break;
	}
}
#ifdef SP_GAME

/*
====================================================================
void pitch_roll_for_slope (edict_t *forwhom, vec3_t *slope, vec3_t storeAngles )

MG

This will adjust the pitch and roll of a monster to match
a given slope - if a non-'0 0 0' slope is passed, it will
use that value, otherwise it will use the ground underneath
the monster.  If it doesn't find a surface, it does nothinh\g
and returns.
====================================================================
*/

void pitch_roll_for_slope(gentity_t* forwhom, vec3_t pass_slope, vec3_t store_angles, const qboolean keep_pitch)
{
	vec3_t slope;
	vec3_t nvf, ovf, ovr, new_angles = { 0, 0, 0 };

	//if we don't have a slope, get one
	if (!pass_slope || VectorCompare(vec3_origin, pass_slope))
	{
		vec3_t endspot;
		vec3_t startspot;
		trace_t trace;

		VectorCopy(forwhom->currentOrigin, startspot);
		startspot[2] += forwhom->mins[2] + 4;
		VectorCopy(startspot, endspot);
		endspot[2] -= 300;
		gi.trace(&trace, forwhom->currentOrigin, vec3_origin, vec3_origin, endspot, forwhom->s.number, MASK_SOLID,
			static_cast<EG2_Collision>(0), 0);
		//		if(trace_fraction>0.05&&forwhom.movetype==MOVETYPE_STEP)
		//			forwhom.flags(-)FL_ONGROUND;

		if (trace.fraction >= 1.0)
			return;

		if (VectorCompare(vec3_origin, trace.plane.normal))
			return;

		VectorCopy(trace.plane.normal, slope);
	}
	else
	{
		VectorCopy(pass_slope, slope);
	}

	float old_pitch = 0;
	if (forwhom->client && forwhom->client->NPC_class == CLASS_VEHICLE)
	{
		//special code for vehicles
		const Vehicle_t* p_veh = forwhom->m_pVehicle;

		vec3_t temp_angles{};
		temp_angles[PITCH] = temp_angles[ROLL] = 0;
		temp_angles[YAW] = p_veh->m_vOrientation[YAW];
		AngleVectors(temp_angles, ovf, ovr, nullptr);
	}
	else
	{
		old_pitch = forwhom->currentAngles[PITCH];
		AngleVectors(forwhom->currentAngles, ovf, ovr, nullptr);
	}

	vectoangles(slope, new_angles);
	float pitch = new_angles[PITCH] + 90;
	if (keep_pitch)
	{
		pitch += old_pitch;
	}
	new_angles[ROLL] = new_angles[PITCH] = 0;

	AngleVectors(new_angles, nvf, nullptr, nullptr);

	float mod = DotProduct(nvf, ovr);

	if (mod < 0)
		mod = -1;
	else
		mod = 1;

	const float dot = DotProduct(nvf, ovf);

	if (store_angles)
	{
		store_angles[PITCH] = dot * pitch;
		store_angles[ROLL] = (1 - Q_fabs(dot)) * pitch * mod;
	}
	else if (forwhom->client)
	{
		forwhom->client->ps.viewangles[PITCH] = dot * pitch;
		forwhom->client->ps.viewangles[ROLL] = (1 - Q_fabs(dot)) * pitch * mod;
		const float oldmins2 = forwhom->mins[2];
		forwhom->mins[2] = -24 + 12 * fabs(forwhom->client->ps.viewangles[PITCH]) / 180.0f;
		//FIXME: if it gets bigger, move up
		if (oldmins2 > forwhom->mins[2])
		{
			//our mins is now lower, need to move up
			//FIXME: trace?
			forwhom->client->ps.origin[2] += oldmins2 - forwhom->mins[2];
			forwhom->currentOrigin[2] = forwhom->client->ps.origin[2];
			gi.linkentity(forwhom);
		}
	}
	else
	{
		forwhom->currentAngles[PITCH] = dot * pitch;
		forwhom->currentAngles[ROLL] = (1 - Q_fabs(dot)) * pitch * mod;
	}
}

/*
void NPC_PostDeathThink( void )
{
	float	mostdist;
	trace_t trace1, trace2, trace3, trace4, movetrace;
	vec3_t	org, endpos, startpos, forward, right;
	int		whichtrace = 0;
	float	cornerdist[4];
	qboolean	frontbackbothclear = false;
	qboolean	rightleftbothclear = false;

	if( NPC->client->ps.groundEntityNum == ENTITYNUM_NONE || !VectorCompare( vec3_origin, NPC->client->ps.velocity ) )
	{
		if ( NPC->client->ps.groundEntityNum != ENTITYNUM_NONE && NPC->client->ps.friction == 1.0 )//check avelocity?
		{
			pitch_roll_for_slope( NPC );
		}

		return;
	}

	cornerdist[FRONT] = cornerdist[BACK] = cornerdist[RIGHT] = cornerdist[LEFT] = 0.0f;

	mostdist = MIN_DROP_DIST;

	AngleVectors( NPC->currentAngles, forward, right, NULL );
	VectorCopy( NPC->currentOrigin, org );
	org[2] += NPC->mins[2];

	VectorMA( org, NPC->dead_size, forward, startpos );
	VectorCopy( startpos, endpos );
	endpos[2] -= 128;
	gi.trace( &trace1, startpos, vec3_origin, vec3_origin, endpos, NPC->s.number, MASK_SOLID, );
	if( !trace1.allsolid && !trace1.startsolid )
	{
		cornerdist[FRONT] = trace1.fraction;
		if ( trace1.fraction > mostdist )
		{
			mostdist = trace1.fraction;
			whichtrace = 1;
		}
	}

	VectorMA( org, -NPC->dead_size, forward, startpos );
	VectorCopy( startpos, endpos );
	endpos[2] -= 128;
	gi.trace( &trace2, startpos, vec3_origin, vec3_origin, endpos, NPC->s.number, MASK_SOLID );
	if( !trace2.allsolid && !trace2.startsolid )
	{
		cornerdist[BACK] = trace2.fraction;
		if ( trace2.fraction > mostdist )
		{
			mostdist = trace2.fraction;
			whichtrace = 2;
		}
	}

	VectorMA( org, NPC->dead_size/2, right, startpos );
	VectorCopy( startpos, endpos );
	endpos[2] -= 128;
	gi.trace( &trace3, startpos, vec3_origin, vec3_origin, endpos, NPC->s.number, MASK_SOLID );
	if ( !trace3.allsolid && !trace3.startsolid )
	{
		cornerdist[RIGHT] = trace3.fraction;
		if ( trace3.fraction>mostdist )
		{
			mostdist = trace3.fraction;
			whichtrace = 3;
		}
	}

	VectorMA( org, -NPC->dead_size/2, right, startpos );
	VectorCopy( startpos, endpos );
	endpos[2] -= 128;
	gi.trace( &trace4, startpos, vec3_origin, vec3_origin, endpos, NPC->s.number, MASK_SOLID );
	if ( !trace4.allsolid && !trace4.startsolid )
	{
		cornerdist[LEFT] = trace4.fraction;
		if ( trace4.fraction > mostdist )
		{
			mostdist = trace4.fraction;
			whichtrace = 4;
		}
	}

	//OK!  Now if two opposite sides are hanging, use a third if any, else, do nothing
	if ( cornerdist[FRONT] > MIN_DROP_DIST && cornerdist[BACK] > MIN_DROP_DIST )
		frontbackbothclear = true;

	if ( cornerdist[RIGHT] > MIN_DROP_DIST && cornerdist[LEFT] > MIN_DROP_DIST )
		rightleftbothclear = true;

	if ( frontbackbothclear && rightleftbothclear )
		return;

	if ( frontbackbothclear )
	{
		if ( cornerdist[RIGHT] > MIN_DROP_DIST )
			whichtrace = 3;
		else if ( cornerdist[LEFT] > MIN_DROP_DIST )
			whichtrace = 4;
		else
			return;
	}

	if ( rightleftbothclear )
	{
		if ( cornerdist[FRONT] > MIN_DROP_DIST )
			whichtrace = 1;
		else if ( cornerdist[BACK] > MIN_DROP_DIST )
			whichtrace = 2;
		else
			return;
	}

	switch ( whichtrace )
	{//check for stuck
	case 1:
		VectorMA( NPC->currentOrigin, NPC->maxs[0], forward, endpos );
		gi.trace( &movetrace, NPC->currentOrigin, NPC->mins, NPC->maxs, endpos, NPC->s.number, MASK_MONSTERSOLID );
		if ( movetrace.allsolid || movetrace.startsolid || movetrace.fraction < 1.0 )
			if ( canmove( movetrace.ent ) )
				whichtrace = -1;
			else
				whichtrace = 0;
		break;
	case 2:
		VectorMA( NPC->currentOrigin, -NPC->maxs[0], forward, endpos );
		gi.trace( &movetrace, NPC->currentOrigin, NPC->mins, NPC->maxs, endpos, NPC->s.number, MASK_MONSTERSOLID );
		if ( movetrace.allsolid || movetrace.startsolid || movetrace.fraction < 1.0 )
			if ( canmove( movetrace.ent ) )
				whichtrace = -1;
			else
				whichtrace = 0;
		break;
	case 3:
		VectorMA( NPC->currentOrigin, NPC->maxs[0], right, endpos );
		gi.trace( &movetrace, NPC->currentOrigin, NPC->mins, NPC->maxs, endpos, NPC->s.number, MASK_MONSTERSOLID );
		if ( movetrace.allsolid || movetrace.startsolid || movetrace.fraction < 1.0 )
			if ( canmove( movetrace.ent ) )
				whichtrace = -1;
			else
				whichtrace = 0;
		break;
	case 4:
		VectorMA( NPC->currentOrigin, -NPC->maxs[0], right, endpos );
		gi.trace( &movetrace, NPC->currentOrigin, NPC->mins, NPC->maxs, endpos, NPC->s.number, MASK_MONSTERSOLID );
		if (movetrace.allsolid || movetrace.startsolid || movetrace.fraction < 1.0 )
			if ( canmove( movetrace.ent ) )
				whichtrace = -1;
			else
				whichtrace = 0;
		break;
	}

	switch ( whichtrace )
	{
	case 1:
		VectorMA( NPC->client->ps.velocity, 200, forward, NPC->client->ps.velocity );
		if ( trace1.fraction >= 0.9 )
		{
//can't anymore, origin not in center of deathframe!
//			NPC->avelocity[PITCH] = -300;
			NPC->client->ps.friction = 1.0;
		}
		else
		{
			pitch_roll_for_slope( NPC, &trace1.plane.normal );
			NPC->client->ps.friction = trace1.plane.normal[2] * 0.1;
		}
		return;
		break;

	case 2:
		VectorMA( NPC->client->ps.velocity, -200, forward, NPC->client->ps.velocity );
		if(trace2.fraction >= 0.9)
		{
//can't anymore, origin not in center of deathframe!
//			NPC->avelocity[PITCH] = 300;
			NPC->client->ps.friction = 1.0;
		}
		else
		{
			pitch_roll_for_slope( NPC, &trace2.plane.normal );
			NPC->client->ps.friction = trace2.plane.normal[2] * 0.1;
		}
		return;
		break;

	case 3:
		VectorMA( NPC->client->ps.velocity, 200, right, NPC->client->ps.velocity );
		if ( trace3.fraction >= 0.9 )
		{
//can't anymore, origin not in center of deathframe!
//			NPC->avelocity[ROLL] = -300;
			NPC->client->ps.friction = 1.0;
		}
		else
		{
			pitch_roll_for_slope( NPC, &trace3.plane.normal );
			NPC->client->ps.friction = trace3.plane.normal[2] * 0.1;
		}
		return;
		break;

	case 4:
		VectorMA( NPC->client->ps.velocity, -200, right, NPC->client->ps.velocity );
		if ( trace4.fraction >= 0.9 )
		{
//can't anymore, origin not in center of deathframe!
//			NPC->avelocity[ROLL] = 300;
			NPC->client->ps.friction = 1.0;
		}
		else
		{
			pitch_roll_for_slope( NPC, &trace4.plane.normal );
			NPC->client->ps.friction = trace4.plane.normal[2] * 0.1;
		}
		return;
		break;
	}

	//on solid ground
	if ( whichtrace == -1 )
	{
		return;
	}
	NPC->client->ps.friction = 1.0;

	//VectorClear( NPC->avelocity );
	pitch_roll_for_slope( NPC );

	//gi.linkentity (NPC);
}
*/

/*
----------------------------------------
DeadThink
----------------------------------------
*/
static void DeadThink()
{
	trace_t trace;
	const float old_maxs2 = NPC->maxs[2];

	NPC->maxs[2] = NPC->client->renderInfo.eyePoint[2] - NPC->currentOrigin[2] + 4;

	if (NPC->maxs[2] < -8)
	{
		NPC->maxs[2] = -8;
	}
	if (NPC->maxs[2] > old_maxs2)
	{
		//inflating maxs, make sure we're not inflating into solid
		gi.trace(&trace, NPC->currentOrigin, NPC->mins, NPC->maxs, NPC->currentOrigin, NPC->s.number, NPC->clipmask,
			static_cast<EG2_Collision>(0), 0);
		if (trace.allsolid)
		{
			//must be inflating
			NPC->maxs[2] = old_maxs2;
		}
	}

	if (level.time >= NPCInfo->timeOfDeath + BodyRemovalPadTime(NPC))
	{
		if (NPC->client->ps.eFlags & EF_NODRAW)
		{
			if (!IIcarusInterface::GetIcarus()->IsRunning(NPC->m_iIcarusID))
			{
				NPC->e_ThinkFunc = thinkF_G_FreeEntity;
				NPC->nextthink = level.time + FRAMETIME;
			}
		}
		else
		{
			if (G_CorpseSeenByPlayer(NPC))
			{
				// the player can see the body: check again in half a second (droids would vanish in view)
				NPCInfo->timeOfDeath = level.time + 500 - BodyRemovalPadTime(NPC);
				CorpsePhysics(NPC);
				return;
			}
			// Start the body effect first, then delay 400ms before ditching the corpse
			NPC_RemoveBodyEffect();

			//FIXME: keep it running through physics somehow?
			NPC->e_ThinkFunc = thinkF_NPC_RemoveBody;
			NPC->nextthink = level.time + FRAMETIME / 2;

			const class_t npc_class = NPC->client->NPC_class;
			// check for droids
			if (npc_class == CLASS_ATST || npc_class == CLASS_GONK ||
				npc_class == CLASS_INTERROGATOR || npc_class == CLASS_MARK1 ||
				npc_class == CLASS_MARK2 || npc_class == CLASS_MOUSE ||
				npc_class == CLASS_PROBE || npc_class == CLASS_PROTOCOL ||
				npc_class == CLASS_R2D2 || npc_class == CLASS_R5D2 ||
				npc_class == CLASS_SEEKER || npc_class == CLASS_SENTRY ||
				npc_class == CLASS_SBD || npc_class == CLASS_BATTLEDROID ||
				npc_class == CLASS_DROIDEKA || npc_class == CLASS_OBJECT ||
				npc_class == CLASS_ASSASSIN_DROID || npc_class == CLASS_SABER_DROID)
			{
				// special droid only behaviors
				NPC->client->ps.eFlags |= EF_NODRAW;
				NPCInfo->timeOfDeath = level.time + FRAMETIME * 8;
			}
			else
			{
				NPCInfo->timeOfDeath = level.time + FRAMETIME * 4;
			}
		}
		return;
	}

	// If the player is on the ground and the resting position contents haven't been set yet...(BounceCount tracks the contents)
	if (NPC->bounceCount < 0 && NPC->s.groundEntityNum >= 0)
	{
		// if client is in a nodrop area, make him/her nodraw
		const int contents = NPC->bounceCount = gi.pointcontents(NPC->currentOrigin, -1);

		if (contents & CONTENTS_NODROP)
		{
			NPC->client->ps.eFlags |= EF_NODRAW;
		}
	}

	CorpsePhysics(NPC);
}

/*
===============
SetNPCGlobals

local function to set globals used throughout the AI code
===============
*/
void SetNPCGlobals(gentity_t* ent)
{
	NPC = ent;
	NPCInfo = ent->NPC;
	client = ent->client;
	memset(&ucmd, 0, sizeof(usercmd_t));
}

gentity_t* _saved_NPC;
gNPC_t* _saved_NPCInfo;
gclient_t* _saved_client;
usercmd_t _saved_ucmd;

void SaveNPCGlobals()
{
	_saved_NPC = NPC;
	_saved_NPCInfo = NPCInfo;
	_saved_client = client;
	memcpy(&_saved_ucmd, &ucmd, sizeof(usercmd_t));
}

void RestoreNPCGlobals()
{
	NPC = _saved_NPC;
	NPCInfo = _saved_NPCInfo;
	client = _saved_client;
	memcpy(&ucmd, &_saved_ucmd, sizeof(usercmd_t));
}

#else // MP game

/*
====================================================================
void pitch_roll_for_slope( gentity_t *forwhom, vec3_t pass_slope )

MG

This will adjust the pitch and roll of a monster to match
a given slope - if a non-'0 0 0' slope is passed, it will
use that value, otherwise it will use the ground underneath
the monster.  If it doesn't find a surface, it does nothing
and returns.
====================================================================
*/

void pitch_roll_for_slope(gentity_t* forwhom, vec3_t pass_slope)
{
	vec3_t slope;
	vec3_t nvf, ovf, ovr, new_angles = { 0, 0, 0 };

	//if we don't have a slope, get one
	if (!pass_slope || VectorCompare(vec3_origin, pass_slope))
	{
		vec3_t endspot;
		vec3_t startspot;
		trace_t trace;

		VectorCopy(forwhom->r.currentOrigin, startspot);
		startspot[2] += forwhom->r.mins[2] + 4;
		VectorCopy(startspot, endspot);
		endspot[2] -= 300;
		trap->Trace(&trace, forwhom->r.currentOrigin, vec3_origin, vec3_origin, endspot, forwhom->s.number, MASK_SOLID,
			qfalse, 0, 0);

		if (trace.fraction >= 1.0)
			return;

		if (VectorCompare(vec3_origin, trace.plane.normal))
			return;

		VectorCopy(trace.plane.normal, slope);
	}
	else
	{
		VectorCopy(pass_slope, slope);
	}

	AngleVectors(forwhom->r.currentAngles, ovf, ovr, NULL);

	vectoangles(slope, new_angles);
	const float pitch = new_angles[PITCH] + 90;
	new_angles[ROLL] = new_angles[PITCH] = 0;

	AngleVectors(new_angles, nvf, NULL, NULL);

	float mod = DotProduct(nvf, ovr);

	if (mod < 0)
		mod = -1;
	else
		mod = 1;

	const float dot = DotProduct(nvf, ovf);

	if (forwhom->client)
	{
		forwhom->client->ps.viewangles[PITCH] = dot * pitch;
		forwhom->client->ps.viewangles[ROLL] = (1 - Q_fabs(dot)) * pitch * mod;
		const float oldmins2 = forwhom->r.mins[2];
		forwhom->r.mins[2] = -24 + 12 * fabs(forwhom->client->ps.viewangles[PITCH]) / 180.0f;
		//FIXME: if it gets bigger, move up
		if (oldmins2 > forwhom->r.mins[2])
		{
			//our mins is now lower, need to move up
			//FIXME: trace?
			forwhom->client->ps.origin[2] += oldmins2 - forwhom->r.mins[2];
			forwhom->r.currentOrigin[2] = forwhom->client->ps.origin[2];
			trap->LinkEntity((sharedEntity_t*)forwhom);
		}
	}
	else
	{
		forwhom->r.currentAngles[PITCH] = dot * pitch;
		forwhom->r.currentAngles[ROLL] = (1 - Q_fabs(dot)) * pitch * mod;
	}
}

/*
----------------------------------------
DeadThink
----------------------------------------
*/
static void DeadThink(void)
{
	trace_t trace;

	const float oldMaxs2 = NPCS.NPC->r.maxs[2];

	NPCS.NPC->r.maxs[2] = NPCS.NPC->client->renderInfo.eyePoint[2] - NPCS.NPC->r.currentOrigin[2] + 4;
	if (NPCS.NPC->r.maxs[2] < -8)
	{
		NPCS.NPC->r.maxs[2] = -8;
	}

	if (NPCS.NPC->r.maxs[2] > oldMaxs2)
	{
		//inflating maxs, make sure we're not inflating into solid
		trap->Trace(&trace, NPCS.NPC->r.currentOrigin, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, NPCS.NPC->r.currentOrigin,
			NPCS.NPC->s.number, NPCS.NPC->clipmask, qfalse, 0, 0);
		if (trace.allsolid)
		{
			//must be inflating
			NPCS.NPC->r.maxs[2] = oldMaxs2;
		}
	}
	//death anim done (or were given a specific amount of time to wait before removal), wait the requisite amount of time them remove
	if (level.time >= NPCS.NPCInfo->timeOfDeath + BodyRemovalPadTime(NPCS.NPC))
	{
		if (NPCS.NPC->client->ps.eFlags & EF_NODRAW)
		{
			if (!trap->ICARUS_IsRunning(NPCS.NPC->s.number))
			{
				NPCS.NPC->think = G_FreeEntity;
				NPCS.NPC->nextthink = level.time + FRAMETIME;
			}
		}
		else
		{
			// Start the body effect first, then delay 400ms before ditching the corpse
			NPC_RemoveBodyEffect();
			NPCS.NPC->think = NPC_RemoveBody;
			NPCS.NPC->nextthink = level.time + FRAMETIME / 2;

			const class_t npc_class = NPCS.NPC->client->NPC_class;
			// check for droids
			if (npc_class == CLASS_SEEKER || npc_class == CLASS_PROBE || npc_class == CLASS_MOUSE ||
				npc_class == CLASS_SBD || npc_class == CLASS_BATTLEDROID || npc_class == CLASS_DROIDEKA ||
				npc_class == CLASS_GONK || npc_class == CLASS_R2D2 || npc_class == CLASS_R5D2 || npc_class ==
				CLASS_REMOTE ||
				npc_class == CLASS_MARK1 || npc_class == CLASS_MARK2 || npc_class == CLASS_PROTOCOL ||
				npc_class == CLASS_INTERROGATOR || npc_class == CLASS_ATST || npc_class == CLASS_SENTRY)
			{
				// special droid only behaviors
				NPCS.NPC->client->ps.eFlags |= EF_NODRAW;
				NPCS.NPCInfo->timeOfDeath = level.time + FRAMETIME * 8;
			}
			else
				NPCS.NPCInfo->timeOfDeath = level.time + FRAMETIME * 4;
		}
		return;
	}

	// If the player is on the ground and the resting position contents haven't been set yet...(BounceCount tracks the contents)
	if (NPCS.NPC->bounceCount < 0 && NPCS.NPC->s.groundEntityNum >= 0)
	{
		// if client is in a nodrop area, make him/her nodraw
		const int contents = NPCS.NPC->bounceCount = trap->PointContents(NPCS.NPC->r.currentOrigin, -1);

		if (contents & CONTENTS_NODROP)
		{
			NPCS.NPC->client->ps.eFlags |= EF_NODRAW;
		}
	}

	CorpsePhysics(NPCS.NPC);
}

/*
===============
SetNPCGlobals

local function to set globals used throughout the AI code
===============
*/
void SetNPCGlobals(gentity_t* ent)
{
	if (!ent)
	{
		return;
	}

	// require the entity to be an NPC with client and NPC info
	if (!ent->NPC || !ent->client)
	{
		// optionally log an error for debugging builds
#ifdef _DEBUG
		Com_Printf("SetNPCGlobals: entity %d has no NPC or client\n", ent ? ent->s.number : -1);
#endif
		NPCS.NPC = NULL;
		NPCS.NPCInfo = NULL;
		NPCS.client = NULL;
		return;
	}
	NPCS.NPC = ent;
	NPCS.NPCInfo = ent->NPC;
	NPCS.client = ent->client;
	memset(&NPCS.ucmd, 0, sizeof(usercmd_t));
}

npcStatic_t _saved_NPCS;

void SaveNPCGlobals(void)
{
	memcpy(&_saved_NPCS, &NPCS, sizeof _saved_NPCS);
}

void RestoreNPCGlobals(void)
{
	memcpy(&NPCS, &_saved_NPCS, sizeof _saved_NPCS);
}

#endif
//We MUST do this, other funcs were using NPC illegally when "self" wasn't the global NPC
void ClearNPCGlobals(void)
{
	NPCS.NPC = NULL;
	NPCS.NPCInfo = NULL;
	NPCS.client = NULL;
}
#ifdef SP_GAME

//===============

extern qboolean showBBoxes;
vec3_t NPCDEBUG_RED = { 1.0, 0.0, 0.0 };
vec3_t NPCDEBUG_GREEN = { 0.0, 1.0, 0.0 };
vec3_t NPCDEBUG_BLUE = { 0.0, 0.0, 1.0 };
vec3_t NPCDEBUG_LIGHT_BLUE = { 0.3f, 0.7f, 1.0 };
extern void CG_Cube(vec3_t mins, vec3_t maxs, vec3_t color, float alpha);

void NPC_ShowDebugInfo()
{
	if (showBBoxes)
	{
		gentity_t* found = nullptr;
		vec3_t mins, maxs;

		//do player, too
		VectorAdd(player->currentOrigin, player->mins, mins);
		VectorAdd(player->currentOrigin, player->maxs, maxs);
		CG_Cube(mins, maxs, NPCDEBUG_RED, 0.25);
		//do NPCs
		while ((found = G_Find(found, FOFS(classname), "NPC")) != nullptr)
		{
			if (gi.inPVS(found->currentOrigin, g_entities[0].currentOrigin))
			{
				VectorAdd(found->currentOrigin, found->mins, mins);
				VectorAdd(found->currentOrigin, found->maxs, maxs);
				CG_Cube(mins, maxs, NPCDEBUG_RED, 0.25);
			}
		}
	}
}

static void NPC_ApplyScriptFlags()
{
	if (NPCInfo->scriptFlags & SCF_CROUCHED)
	{
		if ((NPCInfo->charmedTime > level.time || NPCInfo->darkCharmedTime > level.time) && (ucmd.forwardmove || ucmd.
			rightmove))
		{
			//ugh, if charmed and moving, ignore the crouched command
		}
		else
		{
			ucmd.upmove = -127;
		}
	}

	if (NPCInfo->scriptFlags & SCF_RUNNING)
	{
		ucmd.buttons &= ~BUTTON_WALKING;
	}
	else if (NPCInfo->scriptFlags & SCF_WALKING)
	{
		if ((NPCInfo->charmedTime > level.time || NPCInfo->darkCharmedTime > level.time) && (ucmd.forwardmove || ucmd.
			rightmove))
		{
			//ugh, if charmed and moving, ignore the walking command
		}
		else
		{
			ucmd.buttons |= BUTTON_WALKING;
		}
	}

	if (NPCInfo->scriptFlags & SCF_LEAN_RIGHT)
	{
		ucmd.buttons |= BUTTON_USE;
		ucmd.rightmove = 127;
		ucmd.forwardmove = 0;
		ucmd.upmove = 0;
	}
	else if (NPCInfo->scriptFlags & SCF_LEAN_LEFT)
	{
		ucmd.buttons |= BUTTON_USE;
		ucmd.rightmove = -127;
		ucmd.forwardmove = 0;
		ucmd.upmove = 0;
	}

	if (NPCInfo->scriptFlags & SCF_ALT_FIRE && ucmd.buttons & BUTTON_ATTACK)
	{
		//Use altfire instead
		ucmd.buttons |= BUTTON_ALT_ATTACK;
	}

	// only removes NPC when it's safe too (Player is out of PVS)
	if (NPCInfo->scriptFlags & SCF_SAFE_REMOVE)
	{
		// take from BSRemove
		if (!gi.inPVS(NPC->currentOrigin, g_entities[0].currentOrigin)) //FIXME: use cg.vieworg?
		{
			G_UseTargets2(NPC, NPC, NPC->target3);
			NPC->s.eFlags |= EF_NODRAW;
			NPC->svFlags &= ~SVF_NPC;
			NPC->s.eType = ET_INVISIBLE;
			NPC->contents = 0;
			NPC->health = 0;
			NPC->targetname = nullptr;

			//Disappear in half a second
			NPC->e_ThinkFunc = thinkF_G_FreeEntity;
			NPC->nextthink = level.time + FRAMETIME;
		} //FIXME: else allow for out of FOV???
	}

	// npcs with thermals must calculate distance to enemy and hold fire properly
	if (NPC->client->ps.weapon == WP_THERMAL && NPC->enemy)
	{
		NPCInfo->attackHold = static_cast<int>(Distance(client->ps.origin, NPC->enemy->client->ps.origin)) * 2;
	}
}

extern qboolean JET_Flying(const gentity_t* self);
extern void JET_FlyStart(gentity_t* self);
extern void jet_fly_stop(gentity_t* self);

static void NPC_HandleAIFlags()
{
	// Update Guys With Jet Packs
	//----------------------------
	if (NPCInfo->scriptFlags & SCF_FLY_WITH_JET)
	{
		bool should_fly = !!(NPCInfo->aiFlags & NPCAI_FLY);
		const bool is_flying = !!JET_Flying(NPC);
		bool is_in_the_air = NPC->client->ps.groundEntityNum == ENTITYNUM_NONE;

		if (is_flying)
		{
			// Don't Stop Flying Until Near The Ground
			//-----------------------------------------
			if (is_in_the_air)
			{
				vec3_t ground;
				trace_t trace;
				VectorCopy(NPC->currentOrigin, ground);
				ground[2] -= 60.0f;
				gi.trace(&trace, NPC->currentOrigin, nullptr, nullptr, ground, NPC->s.number, NPC->clipmask,
					static_cast<EG2_Collision>(0), 0);

				is_in_the_air = !trace.allsolid && !trace.startsolid && trace.fraction > 0.9f;
			}

			// If Flying, Remember The Last Time
			//-----------------------------------
			if (is_in_the_air)
			{
				NPC->lastInAirTime = level.time;
				should_fly = true;
			}

			// Auto Turn Off Jet Pack After 1 Second On The Ground
			//-----------------------------------------------------
			else if (!should_fly && level.time - NPC->lastInAirTime > 3000)
			{
				NPCInfo->aiFlags &= ~NPCAI_FLY;
			}
		}

		// If We Should Be Flying And Are Not, Start Er Up
		//-------------------------------------------------
		if (should_fly && !is_flying)
		{
			JET_FlyStart(NPC); // EVENTUALLY, Remove All Other Calls
		}

		// Otherwise, If Needed, Shut It Off
		//-----------------------------------
		else if (!should_fly && is_flying)
		{
			jet_fly_stop(NPC); // EVENTUALLY, Remove All Other Calls
		}
	}

	//FIXME: make these flags checks a function call like NPC_CheckAIFlagsAndTimers
	if (NPCInfo->aiFlags & NPCAI_LOST)
	{
		//Print that you need help!
		//FIXME: shouldn't remove this just yet if cg_draw needs it
		NPCInfo->aiFlags &= ~NPCAI_LOST;

		/*
		if ( showWaypoints )
		{
			Quake3Game()->DebugPrint(WL_WARNING, "%s can't navigate to target %s (my wp: %d, goal wp: %d)\n", NPC->targetname, NPCInfo->goalEntity->targetname, NPC->waypoint, NPCInfo->goalEntity->waypoint );
		}
		*/

		if (NPCInfo->goalEntity && NPCInfo->goalEntity == NPC->enemy)
		{
			//We can't nav to our enemy
			//Drop enemy and see if we should search for him
			NPC_LostEnemyDecideChase();

			npc_check_speak(NPC);
		}
	}

	//been told to play a victory sound after a delay
	if (NPCInfo->greetingDebounceTime && NPCInfo->greetingDebounceTime < level.time)
	{
		G_AddVoiceEvent(NPC, Q_irand(EV_VICTORY1, EV_VICTORY3), Q_irand(2000, 4000));
		NPCInfo->greetingDebounceTime = 0;
	}

	if (NPCInfo->ffireCount > 0)
	{
		if (NPCInfo->ffireFadeDebounce < level.time)
		{
			NPCInfo->ffireCount--;
			NPCInfo->ffireFadeDebounce = level.time + 3000;
		}
	}
}

extern void NPC_ObstacleClimb();

#else // MP game

//===============

extern qboolean showBBoxes;
vec3_t NPCDEBUG_RED = { 1.0, 0.0, 0.0 };
vec3_t NPCDEBUG_GREEN = { 0.0, 1.0, 0.0 };
vec3_t NPCDEBUG_BLUE = { 0.0, 0.0, 1.0 };
vec3_t NPCDEBUG_LIGHT_BLUE = { 0.3f, 0.7f, 1.0 };
extern void G_Cube(vec3_t mins, vec3_t maxs, vec3_t color, float alpha);
extern void G_Line(vec3_t start, vec3_t end, vec3_t color, float alpha);
extern qboolean InPlayersPVS(vec3_t point);

static void NPC_ApplyScriptFlags(void)
{
	if (NPCS.NPCInfo->scriptFlags & SCF_CROUCHED)
	{
		if (NPCS.NPCInfo->charmedTime > level.time && (NPCS.ucmd.forwardmove || NPCS.ucmd.rightmove))
		{
			//ugh, if charmed and moving, ignore the crouched command
		}
		else
		{
			NPCS.ucmd.upmove = -127;
		}
	}

	if (NPCS.NPCInfo->scriptFlags & SCF_RUNNING)
	{
		NPCS.ucmd.buttons &= ~BUTTON_WALKING;
	}
	else if (NPCS.NPCInfo->scriptFlags & SCF_WALKING)
	{
		if (NPCS.NPCInfo->charmedTime > level.time && (NPCS.ucmd.forwardmove || NPCS.ucmd.rightmove))
		{
			//ugh, if charmed and moving, ignore the walking command
		}
		else
		{
			NPCS.ucmd.buttons |= BUTTON_WALKING;
		}
	}

	if (NPCS.NPCInfo->scriptFlags & SCF_LEAN_RIGHT)
	{
		NPCS.ucmd.buttons |= BUTTON_USE;
		NPCS.ucmd.rightmove = 127;
		NPCS.ucmd.forwardmove = 0;
		NPCS.ucmd.upmove = 0;
	}
	else if (NPCS.NPCInfo->scriptFlags & SCF_LEAN_LEFT)
	{
		NPCS.ucmd.buttons |= BUTTON_USE;
		NPCS.ucmd.rightmove = -127;
		NPCS.ucmd.forwardmove = 0;
		NPCS.ucmd.upmove = 0;
	}

	if (NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE && NPCS.ucmd.buttons & BUTTON_ATTACK)
	{
		//Use altfire instead
		// new code so snipers shoot their altfire properly
		if (NPCS.NPC->client->ps.weapon != WP_DISRUPTOR || NPCS.NPC->client->ps.zoomMode == 0)
		{
			NPCS.ucmd.buttons &= ~BUTTON_ATTACK; // if using alt fire, dont use normal fire
			NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
		}

		if (NPCS.NPC->client->ps.weapon == WP_DISRUPTOR && NPCS.NPC->client->ps.zoomMode != 0 && NPCS.NPCInfo->
			attackHold == 0)
		{
			// snipers can use charged shot
			NPCS.NPCInfo->attackHold = 2000;
		}
		else if (NPCS.NPC->client->ps.weapon == WP_ROCKET_LAUNCHER)
		{
			// npcs can use homing missiles properly now
			NPCS.NPCInfo->attackHold = 2000;
		}
		else if (NPCS.NPC->client->ps.weapon == WP_BRYAR_PISTOL && NPCS.ucmd.buttons & BUTTON_ALT_ATTACK && !
			Q_irand(0, 1))
		{
			// npcs can use charged pistol
			NPCS.NPCInfo->attackHold = 2000;
		}
	}
	else if (NPCS.NPC->client->ps.weapon == WP_DISRUPTOR && !(NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE) && NPCS.NPC->
		client->ps.zoomMode != 0)
	{
		// reset sniper zoomMode when switching back to primary fire
		NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
	}
	else if (NPCS.NPC->client->ps.weapon == WP_BOWCASTER && NPCS.ucmd.buttons & BUTTON_ATTACK && !Q_irand(0, 1))
	{
		// npcs can use charged bowcaster
		NPCS.NPCInfo->attackHold = 2000;
	}
	// only removes NPC when it's safe too (Player is out of PVS)
	if (NPCS.NPCInfo->scriptFlags & SCF_SAFE_REMOVE)
	{
		// take from BSRemove
		if (!InPlayersPVS(NPCS.NPC->r.currentOrigin))
		{
			//racc - we can be safely removed.
			G_UseTargets2(NPCS.NPC, NPCS.NPC, NPCS.NPC->target3);
			NPCS.NPC->s.eFlags |= EF_NODRAW;
			NPCS.NPC->s.eType = ET_INVISIBLE;
			NPCS.NPC->r.contents = 0;
			NPCS.NPC->health = 0;
			NPCS.NPC->targetname = NULL;

			//Disappear in half a second
			NPCS.NPC->think = G_FreeEntity;
			NPCS.NPC->nextthink = level.time + FRAMETIME;
		}
	}

	// npcs with thermals must calculate distance to enemy and hold fire properly
	if (NPCS.NPC->client->ps.weapon == WP_THERMAL && NPCS.NPC->enemy)
	{
		NPCS.NPCInfo->attackHold = (int)Distance(NPCS.client->ps.origin, NPCS.NPC->enemy->client->ps.origin) * 2;
	}
}

void Q3_DebugPrint(int level, const char* format, ...);

static void NPC_HandleAIFlags(void)
{
	//FIXME: make these flags checks a function call like NPC_CheckAIFlagsAndTimers
	if (NPCS.NPCInfo->aiFlags & NPCAI_LOST)
	{
		//Print that you need help!
		//FIXME: shouldn't remove this just yet if cg_draw needs it
		NPCS.NPCInfo->aiFlags &= ~NPCAI_LOST;

		if (NPCS.NPCInfo->goalEntity && NPCS.NPCInfo->goalEntity == NPCS.NPC->enemy)
		{
			//We can't nav to our enemy
			//Drop enemy and see if we should search for him
			NPC_LostEnemyDecideChase();

			npc_check_speak(NPCS.NPC); // (SP)
		}
	}

	//been told to play a victory sound after a delay
	if (NPCS.NPCInfo->greetingDebounceTime && NPCS.NPCInfo->greetingDebounceTime < level.time)
	{
		G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_VICTORY1, EV_VICTORY3), Q_irand(2000, 4000));
		NPCS.NPCInfo->greetingDebounceTime = 0;
	}

	if (NPCS.NPCInfo->ffireCount > 0)
	{
		if (NPCS.NPCInfo->ffireFadeDebounce < level.time)
		{
			NPCS.NPCInfo->ffireCount--;
			//Com_Printf( "drop: %d < %d\n", NPCInfo->ffireCount, 3+((2-g_npcspskill.integer)*2) );
			NPCS.NPCInfo->ffireFadeDebounce = level.time + 3000;
		}
	}
	if (d_patched.integer)
	{
		//use patch-style navigation
		if (NPCS.NPCInfo->consecutiveBlockedMoves > 20)
		{
			//been stuck for a while, try again?
			NPCS.NPCInfo->consecutiveBlockedMoves = 0;
		}
	}
}

#endif
static void NPC_AvoidWallsAndCliffs(void)
{
	/*
		vec3_t	forward, right, testPos, angles, mins;
		trace_t	trace;
		float	fwdDist, rtDist;
		//FIXME: set things like this forward dir once at the beginning
		//of a frame instead of over and over again
		if ( NPCInfo->aiFlags & NPCAI_NO_COLL_AVOID )
		{
			return;
		}

		if ( ucmd.upmove > 0 || NPC->client->ps.groundEntityNum == ENTITYNUM_NONE )
		{//Going to jump or in the air
			return;
		}

		if ( !ucmd.forwardmove && !ucmd.rightmove )
		{
			return;
		}

		if ( fabs( AngleDelta( NPC->currentAngles[YAW], NPCInfo->desiredYaw ) ) < 5.0 )//!ucmd.angles[YAW] )
		{//Not turning much, don't do this
			//NOTE: Should this not happen only if you're not turning AT ALL?
			//	You could be turning slowly but moving fast, so that would
			//	still let you walk right off a cliff...
			//NOTE: Or maybe it is a good idea to ALWAYS do this, regardless
			//	of whether ot not we're turning?  But why would we be walking
			//  straight into a wall or off	a cliff unless we really wanted to?
			return;
		}

		VectorCopy( NPC->mins, mins );
		mins[2] += STEPSIZE;
		angles[YAW] = NPC->client->ps.viewangles[YAW];//Add ucmd.angles[YAW]?
		AngleVectors( angles, forward, right, NULL );
		fwdDist = ((float)ucmd.forwardmove)/16.0f;
		rtDist = ((float)ucmd.rightmove)/16.0f;
		VectorMA( NPC->currentOrigin, fwdDist, forward, testPos );
		VectorMA( testPos, rtDist, right, testPos );
		gi.trace( &trace, NPC->currentOrigin, mins, NPC->maxs, testPos, NPC->s.number, NPC->clipmask );
		if ( trace.allsolid || trace.startsolid || trace.fraction < 1.0 )
		{//Going to bump into something, don't move, just turn
			ucmd.forwardmove = 0;
			ucmd.rightmove = 0;
			return;
		}

		VectorCopy(trace.endpos, testPos);
		testPos[2] -= 128;

		gi.trace( &trace, trace.endpos, mins, NPC->maxs, testPos, NPC->s.number, NPC->clipmask );
		if ( trace.allsolid || trace.startsolid || trace.fraction < 1.0 )
		{//Not going off a cliff
			return;
		}

		//going to fall at least 128, don't move, just turn... is this bad, though?  What if we want them to drop off?
		ucmd.forwardmove = 0;
		ucmd.rightmove = 0;
		return;
	*/
}

static void NPC_CheckAttackScript(void)
{
	if (!(NPCS.ucmd.buttons & BUTTON_ATTACK))
	{
		return;
	}

	G_ActivateBehavior(NPCS.NPC, BSET_ATTACK);
}
#ifdef SP_GAME

float NPC_MaxDistSquaredForWeapon();

#else // MP game

float NPC_MaxDistSquaredForWeapon(void);

#endif
static void NPC_CheckAttackHold(void)
{
	vec3_t vec;

	// If they don't have an enemy they shouldn't hold their attack anim.
	if (!NPCS.NPC->enemy)
	{
		NPCS.NPCInfo->attackHoldTime = 0;
		return;
	}
	VectorSubtract(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin, vec);
	if (VectorLengthSquared(vec) > NPC_MaxDistSquaredForWeapon())
	{
		NPCS.NPCInfo->attackHoldTime = 0;
	}
	else if (NPCS.NPCInfo->attackHoldTime && NPCS.NPCInfo->attackHoldTime > level.time)
	{
		NPCS.ucmd.buttons |= BUTTON_ATTACK;
	}
	else if (NPCS.NPCInfo->attackHold && NPCS.ucmd.buttons & BUTTON_ATTACK)
	{
		NPCS.NPCInfo->attackHoldTime = level.time + NPCS.NPCInfo->attackHold;
	}
	else
	{
		NPCS.NPCInfo->attackHoldTime = 0;
	}
}
#ifndef SP_GAME // MP only

qboolean NPC_CanUseAdvancedFighting()
{
	// Evasion/Weapon Switching/etc...
	switch (NPCS.NPC->client->NPC_class)
	{
	case CLASS_BESPIN_COP:
	case CLASS_CLAW:
	case CLASS_COMMANDO:
	case CLASS_DESANN:
	case CLASS_GALAK:
	case CLASS_GRAN:
	case CLASS_IMPERIAL:
	case CLASS_JAN:
	case CLASS_JEDI:
	case CLASS_KYLE:
	case CLASS_LANDO:
	case CLASS_LUKE:
	case CLASS_MORGANKATARN:
	case CLASS_MURJJ:
	case CLASS_REBEL:
	case CLASS_REBORN:
	case CLASS_REELO:
	case CLASS_RODIAN:
	case CLASS_SHADOWTROOPER:
	case CLASS_TAVION:
	case CLASS_TRANDOSHAN:
	case CLASS_WEEQUAY:
	case CLASS_BOBAFETT:
	case CLASS_WOOKIE:
		break;
	default:
		return qfalse;
	}
	return qtrue;
}

#endif
/*
void NPC_KeepCurrentFacing(void)

Fills in a default ucmd to keep current angles facing
*/
static void NPC_KeepCurrentFacing(void)
{
	if (!NPCS.ucmd.angles[YAW])
	{
		NPCS.ucmd.angles[YAW] = ANGLE2SHORT(NPCS.client->ps.viewangles[YAW]) - NPCS.client->ps.delta_angles[YAW];
	}

	if (!NPCS.ucmd.angles[PITCH])
	{
		NPCS.ucmd.angles[PITCH] = ANGLE2SHORT(NPCS.client->ps.viewangles[PITCH]) - NPCS.client->ps.delta_angles[PITCH];
	}
}

/*
-------------------------
NPC_BehaviorSet_Charmed
-------------------------
*/

static void NPC_BehaviorSet_Charmed(const int b_state)
{
	switch (b_state)
	{
	case BS_FOLLOW_LEADER: //# 40: Follow your leader and shoot any enemies you come across
		NPC_BSFollowLeader();
		break;
	case BS_REMOVE:
		NPC_BSRemove();
		break;
	case BS_SEARCH: //# 43: Using current waypoint as a base, search the immediate branches of waypoints for enemies
		NPC_BSSearch();
		break;
	case BS_WANDER: //# 46: Wander down random waypoint paths
		NPC_BSWander();
		break;
	case BS_FLEE:
		NPC_BSFlee();
		break;
	case BS_FOLLOW_OVERRIDE: //# 40: Follow your leader and shoot any enemies you come across
		NPC_BSFollowLeader();
		break;
	default:
	case BS_DEFAULT: //whatever
		NPC_BSDefault();
		break;
	}
}
#ifdef SP_GAME

/*
-------------------------
NPC_BehaviorSet_Default
-------------------------
*/

void NPC_BehaviorSet_Default(const int b_state)
{
	switch (b_state)
	{
	case BS_ADVANCE_FIGHT: //head toward captureGoal, shoot anything that gets in the way
		NPC_BSAdvanceFight();
		break;
	case BS_SLEEP: //Follow a path, looking for enemies
		NPC_BSSleep();
		break;
	case BS_FOLLOW_LEADER: //# 40: Follow your leader and shoot any enemies you come across
		NPC_BSFollowLeader();
		break;
	case BS_JUMP: //41: Face navgoal and jump to it.
		NPC_BSJump();
		break;
	case BS_REMOVE:
		NPC_BSRemove();
		break;
	case BS_SEARCH: //# 43: Using current waypoint as a base, search the immediate branches of waypoints for enemies
		NPC_BSSearch();
		break;
	case BS_NOCLIP:
		NPC_BSNoClip();
		break;
	case BS_WANDER: //# 46: Wander down random waypoint paths
		NPC_BSWander();
		break;
	case BS_FLEE:
		NPC_BSFlee();
		break;
	case BS_WAIT:
		NPC_BSWait();
		break;
	case BS_CINEMATIC:
		NPC_BSCinematic();
		break;
	case BS_FOLLOW_OVERRIDE: //# 40: Follow your leader and shoot any enemies you come across
		NPC_BSFollowLeader();
		break;
	default:
	case BS_DEFAULT: //whatever
		NPC_BSDefault();
		break;
	}
}

#else // MP game

/*
-------------------------
NPC_BehaviorSet_Default
-------------------------
*/

void NPC_BehaviorSet_Default(const int b_state)
{
	if (NPCS.NPC->enemy && NPCS.NPC->enemy->inuse && NPCS.NPC->enemy->health > 0)
	{
		// Have an anemy... Check if we should use advanced fighting for this NPC...
		if (NPC_CanUseAdvancedFighting())
		{
			// This NPC can use advanced tactics... Use them!!!
			NPC_BSJedi_Default();
			return;
		}
	}
	switch (b_state)
	{
	case BS_ADVANCE_FIGHT: //head toward captureGoal, shoot anything that gets in the way
		NPC_BSAdvanceFight();
		break;
	case BS_SLEEP: //Follow a path, looking for enemies
		NPC_BSSleep();
		break;
	case BS_FOLLOW_LEADER: //# 40: Follow your leader and shoot any enemies you come across
		NPC_BSFollowLeader();
		break;
	case BS_JUMP: //41: Face navgoal and jump to it.
		NPC_BSJump();
		break;
	case BS_REMOVE:
		NPC_BSRemove();
		break;
	case BS_SEARCH: //# 43: Using current waypoint as a base, search the immediate branches of waypoints for enemies
		NPC_BSSearch();
		break;
	case BS_NOCLIP:
		NPC_BSNoClip();
		break;
	case BS_WANDER: //# 46: Wander down random waypoint paths
		NPC_BSWander();
		break;
	case BS_FLEE:
		NPC_BSFlee();
		break;
	case BS_WAIT:
		NPC_BSWait();
		break;
	case BS_CINEMATIC:
		NPC_BSCinematic();
		break;
	default:
	case BS_DEFAULT: //whatever
		NPC_BSDefault();
		break;
	}
}

#endif
/*
-------------------------
NPC_BehaviorSet_Interrogator
-------------------------
*/
static void NPC_BehaviorSet_Interrogator(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSInterrogator_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}

/*
-------------------------
NPC_BehaviorSet_ImperialProbe
-------------------------
*/
static void NPC_BehaviorSet_ImperialProbe(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSImperialProbe_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}
#ifdef SP_GAME

void NPC_BSSeeker_Default();

#else // MP game

void NPC_BSSeeker_Default(void);

#endif
/*
-------------------------
NPC_BehaviorSet_Seeker
-------------------------
*/
static void NPC_BehaviorSet_Seeker(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
	default:
		NPC_BSSeeker_Default();
		break;
	}
}
#ifdef SP_GAME

void NPC_BSRemote_Default();

#else // MP game

void NPC_BSRemote_Default(void);

#endif
/*
-------------------------
NPC_BehaviorSet_Remote
-------------------------
*/
static void NPC_BehaviorSet_Remote(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSRemote_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}
#ifdef SP_GAME

void NPC_BSSentry_Default();

#else // MP game

void NPC_BSSentry_Default(void);

#endif
/*
-------------------------
NPC_BehaviorSet_Sentry
-------------------------
*/
static void NPC_BehaviorSet_Sentry(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSSentry_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}

/*
-------------------------
NPC_BehaviorSet_Grenadier
-------------------------
*/
static void NPC_BehaviorSet_Grenadier(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSGrenadier_Default();
		break;

	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}
#ifndef SP_GAME // MP only

/*
-------------------------
NPC_BehaviorSet_Tusken
-------------------------
*/
extern void NPC_BSTusken_Default(void);

#endif
/*
-------------------------
NPC_BehaviorSet_Tusken
-------------------------
*/
static void NPC_BehaviorSet_Tusken(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSTusken_Default();
		break;

	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}

/*
-------------------------
NPC_BehaviorSet_Sniper
-------------------------
*/
static void NPC_BehaviorSet_Sniper(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSSniper_Default();
		break;

	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}

/*
-------------------------
NPC_BehaviorSet_Stormtrooper
-------------------------
*/

void NPC_BehaviorSet_Stormtrooper(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSST_Default();
		break;

	case BS_INVESTIGATE:
		NPC_BSST_Investigate();
		break;

	case BS_SLEEP:
		NPC_BSST_Sleep();
		break;

	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}
#ifdef SP_GAME

static void NPC_BehaviorSet_Object(int b_state)
{}

/*
-------------------------
NPC_BehaviorSet_Jedi
-------------------------
*/

#else // MP game

/*
-------------------------
NPC_BehaviorSet_Trooper
-------------------------
*/
void NPC_BehaviorSet_Trooper(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSST_Default();
		break;

	case BS_INVESTIGATE:
		NPC_BSST_Investigate();
		break;

	case BS_SLEEP:
		NPC_BSST_Sleep();
		break;

	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}

/*
-------------------------
NPC_BehaviorSet_Jedi
-------------------------
*/

#endif
static void NPC_BehaviorSet_Jedi(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_INVESTIGATE: //WTF???!!
	case BS_DEFAULT:
		NPC_BSJedi_Default();
		break;

	case BS_FOLLOW_LEADER:
		NPC_BSJedi_FollowLeader();
		break;
	case BS_FOLLOW_OVERRIDE: //# 40: Follow your leader and shoot any enemies you come across
		NPC_BSJedi_FollowLeader();
		break;

	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}
#ifdef SP_GAME

qboolean G_JediInNormalAI(const gentity_t* ent)
{
	//NOTE: should match above func's switch!
	//check our b_state
	const bState_t b_state = G_CurrentBState(ent->NPC);
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_INVESTIGATE:
	case BS_DEFAULT:
	case BS_FOLLOW_LEADER:
	case BS_FOLLOW_OVERRIDE:
		return qtrue;
	default:
		break;
	}
	return qfalse;
}

#else // MP game

//qboolean G_JediInNormalAI(const gentity_t* ent)
//{
//	//NOTE: should match above func's switch!
//	//check our b_state
//	const bState_t b_state = G_CurrentBState(ent->NPC);
//	switch (b_state)
//	{
//	case BS_STAND_GUARD:
//	case BS_PATROL:
//	case BS_STAND_AND_SHOOT:
//	case BS_HUNT_AND_KILL:
//	case BS_INVESTIGATE:
//	case BS_DEFAULT:
//	case BS_FOLLOW_LEADER:
//		return qtrue;
//	default: ;
//	}
//	return qfalse;
//}

#endif
/*
-------------------------
NPC_BehaviorSet_Droid
-------------------------
*/
static void NPC_BehaviorSet_Droid(const int b_state)
{
	switch (b_state)
	{
	case BS_DEFAULT:
	case BS_STAND_GUARD:
	case BS_PATROL:
		NPC_BSDroid_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}

/*
-------------------------
NPC_BehaviorSet_Mark1
-------------------------
*/
static void NPC_BehaviorSet_Mark1(const int b_state)
{
	switch (b_state)
	{
	case BS_DEFAULT:
	case BS_STAND_GUARD:
	case BS_PATROL:
		NPC_BSMark1_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}

/*
-------------------------
NPC_BehaviorSet_Mark2
-------------------------
*/
static void NPC_BehaviorSet_Mark2(const int b_state)
{
	switch (b_state)
	{
	case BS_DEFAULT:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
		NPC_BSMark2_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}

/*
-------------------------
NPC_BehaviorSet_ATST
-------------------------
*/
static void NPC_BehaviorSet_ATST(const int b_state)
{
	switch (b_state)
	{
	case BS_DEFAULT:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
		NPC_BSATST_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}

/*
-------------------------
NPC_BehaviorSet_MineMonster
-------------------------
*/
static void NPC_BehaviorSet_MineMonster(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSMineMonster_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}

/*
-------------------------
NPC_BehaviorSet_Howler
-------------------------
*/
static void NPC_BehaviorSet_Howler(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSHowler_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}

/*
-------------------------
NPC_BehaviorSet_Rancor
-------------------------
*/
static void NPC_BehaviorSet_Rancor(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSRancor_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}
#ifndef SP_GAME // MP only

/*
-------------------------
NPC_BehaviorSet_Wampa
-------------------------
*/
extern void NPC_BSWampa_Default(void);

#endif
/*
-------------------------
NPC_BehaviorSet_Wampa
-------------------------
*/
static void NPC_BehaviorSet_Wampa(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSWampa_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}

/*
-------------------------
NPC_BehaviorSet_SandCreature
-------------------------
*/
static void NPC_BehaviorSet_SandCreature(const int b_state)
{
	switch (b_state)
	{
	case BS_STAND_GUARD:
	case BS_PATROL:
	case BS_STAND_AND_SHOOT:
	case BS_HUNT_AND_KILL:
	case BS_DEFAULT:
		NPC_BSSandCreature_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}
#ifndef SP_GAME // MP only

/*
-------------------------
NPC_BehaviorSet_Droid
-------------------------
*/
// Added 01/21/03 by AReis.
extern void NPC_BSAnimal_Default(void);

#endif
/*
-------------------------
NPC_BehaviorSet_Droid
-------------------------
*/
// Added 01/21/03 by AReis.
static void NPC_BehaviorSet_Animal(const int b_state)
{
	switch (b_state)
	{
	case BS_DEFAULT:
	case BS_STAND_GUARD:
	case BS_PATROL:
		NPC_BSAnimal_Default();
		break;
	default:
		NPC_BehaviorSet_Default(b_state);
		break;
	}
}
#ifdef SP_GAME

/*
-------------------------
NPC_RunBehavior
-------------------------
*/
extern void NPC_BSEmplaced();
extern qboolean NPC_CheckSurrender();
extern void NPC_BSRT_Default();
extern void NPC_BSCivilian_Default(int b_state);
extern void NPC_BSSD_Default();
extern void NPC_BehaviorSet_Trooper(int b_state);
extern bool NPC_IsTrooper(const gentity_t* ent);
extern bool Pilot_MasterUpdate();
extern void NPC_BSGM_Default();
extern void npc_bsdroideka_default();
extern qboolean droideka_npc(const gentity_t* ent);
extern void deka_bubble_shield_update();

extern qboolean NPC_FighterAI();

static void NPC_RunBehavior(const int team, const int b_state)
{
	// flying a fighter (AI_Fighter.cpp: the MP space maps' ships)
	if (NPC_FighterAI())
	{
		return;
	}
	//
	if (b_state == BS_FOLLOW_OVERRIDE)
	{
		NPC_BSFollowLeader();
	}
	if (b_state == BS_CINEMATIC)
	{
		NPC_BSCinematic();
	}
	else if ((NPCInfo->scriptFlags & SCF_PILOT) && Pilot_MasterUpdate())
	{
		return;
	}
	else if (NPC_JumpBackingUp())
	{
	}
	else if (!TIMER_Done(NPC, "DEMP2_StunTime"))
	{
		NPC_UpdateAngles(qtrue, qtrue);
	}
	else if (NPC->client->ps.weapon == WP_EMPLACED_GUN)
	{
		NPC_BSEmplaced();
		G_CheckCharmed(NPC);
	}
	else if (NPC->client->NPC_class == CLASS_HOWLER)
	{
		NPC_BehaviorSet_Howler(b_state);
	}
	else if (Jedi_CultistDestroyer(NPC))
	{
		NPC_BSJedi_Default();
	}
	else if (NPC->client->NPC_class == CLASS_SABER_DROID)
	{
		//saber droid
		NPC_BSSD_Default();
	}
	else if (NPC->client->ps.weapon == WP_SABER)
	{
		//jedi
		NPC_BehaviorSet_Jedi(b_state);
	}
	else if (NPC->client->NPC_class == CLASS_REBORN && NPC->client->ps.weapon == WP_MELEE)
	{
		//force-only reborn
		NPC_BehaviorSet_Jedi(b_state);
	}
	else if (NPC->client->NPC_class == CLASS_SITHLORD && NPC->client->ps.weapon == WP_MELEE)
	{
		//force-only reborn
		NPC_BehaviorSet_Jedi(b_state);
	}
	else if (NPC->client->NPC_class == CLASS_SITHLORD && NPC->client->ps.weapon == WP_NONE)
	{
		//force-only reborn
		NPC_BehaviorSet_Jedi(b_state);
	}
	else if (NPC->client->NPC_class == CLASS_STORMTROOPER && (NPC->client->ps.weapon == WP_TUSKEN_RIFLE || NPC->client->
		ps.weapon == WP_FLECHETTE)) // (parentheses added 2026-11-22: any NPC with a flechette ran the Jedi AI)
	{
		//force-only reborn
		NPC_BehaviorSet_Jedi(b_state);
	}
	else if (NPC->client->NPC_class == CLASS_STORMCOMMANDO || NPC->client->NPC_class == CLASS_CLONETROOPER)
	{
		//force-only reborn
		NPC_BehaviorSet_Jedi(b_state);
	}
	else if (NPC->client->NPC_class == CLASS_BOBAFETT)
	{
		Boba_Update();
		if (NPCInfo->surrenderTime)
		{
			Boba_Flee();
		}
		else
		{
			if (!Boba_Tactics())
			{
				if (Boba_Flying(NPC))
				{
					NPC_BehaviorSet_Seeker(b_state);
				}
				else
				{
					NPC_BehaviorSet_Jedi(b_state);
				}
			}
		}
	}
	else if (NPC->client->NPC_class == CLASS_MANDO)
	{
		Boba_Update();
		if (NPCInfo->surrenderTime)
		{
			Boba_Flee();
		}
		else
		{
			if (!Boba_Tactics())
			{
				if (Boba_Flying(NPC))
				{
					NPC_BehaviorSet_Seeker(b_state);
				}
				else
				{
					NPC_BehaviorSet_Jedi(b_state);
				}
			}
		}
	}
	else if (NPC->client->NPC_class == CLASS_ROCKETTROOPER)
	{
		//bounty hunter
		if (RT_Flying(NPC) || NPC->enemy != nullptr)
		{
			NPC_BSRT_Default();
		}
		else
		{
			NPC_BehaviorSet_Stormtrooper(b_state);
		}
		G_CheckCharmed(NPC);
	}
	else if (NPC->client->NPC_class == CLASS_BATTLEDROID)
	{
		NPC_BehaviorSet_Stormtrooper(b_state);
	}
	else if (NPC->client->NPC_class == CLASS_RANCOR)
	{
		NPC_BehaviorSet_Rancor(b_state);
	}
	else if (NPC->client->NPC_class == CLASS_SAND_CREATURE)
	{
		NPC_BehaviorSet_SandCreature(b_state);
	}
	else if (NPC->client->NPC_class == CLASS_WAMPA)
	{
		NPC_BehaviorSet_Wampa(b_state);
		G_CheckCharmed(NPC);
	}
	else if (NPCInfo->scriptFlags & SCF_FORCED_MARCH)
	{
		//being forced to march
		NPC_BSDefault();
	}
	else if (NPC->client->ps.weapon == WP_TUSKEN_RIFLE)
	{
		if (NPCInfo->scriptFlags & SCF_ALT_FIRE)
		{
			NPC_BehaviorSet_Sniper(b_state);
			G_CheckCharmed(NPC);
			return;
		}
		NPC_BehaviorSet_Tusken(b_state);
		G_CheckCharmed(NPC);
	}
	else if (NPC->client->ps.weapon == WP_TUSKEN_STAFF)
	{
		NPC_BehaviorSet_Tusken(b_state);
		G_CheckCharmed(NPC);
	}
	else if (NPC->client->ps.weapon == WP_NOGHRI_STICK)
	{
		NPC_BehaviorSet_Stormtrooper(b_state);
		G_CheckCharmed(NPC);
	}
	else if (NPC->client->NPC_class == CLASS_OBJECT)
	{
		NPC_BehaviorSet_Object(b_state);
	}
	else
	{
		switch (team)
		{
			// not sure if TEAM_ENEMY is appropriate here, I think I should be using NPC_class to check for behavior - dmv
		case TEAM_ENEMY:
			// special cases for enemy droids
			switch (NPC->client->NPC_class)
			{
			case CLASS_ATST:
				NPC_BehaviorSet_ATST(b_state);
				return;
			case CLASS_PROBE:
				NPC_BehaviorSet_ImperialProbe(b_state);
				return;
			case CLASS_REMOTE:
				NPC_BehaviorSet_Remote(b_state);
				return;
			case CLASS_SENTRY:
				NPC_BehaviorSet_Sentry(b_state);
				return;
			case CLASS_INTERROGATOR:
				NPC_BehaviorSet_Interrogator(b_state);
				return;
			case CLASS_MINEMONSTER:
				NPC_BehaviorSet_MineMonster(b_state);
				return;
			case CLASS_HOWLER:
				NPC_BehaviorSet_Howler(b_state);
				return;
			case CLASS_RANCOR:
				NPC_BehaviorSet_Rancor(b_state);
				return;
			case CLASS_SAND_CREATURE:
				NPC_BehaviorSet_SandCreature(b_state);
				return;
			case CLASS_MARK1:
				NPC_BehaviorSet_Mark1(b_state);
				return;
			case CLASS_MARK2:
				NPC_BehaviorSet_Mark2(b_state);
				return;
			case CLASS_GALAKMECH:
				NPC_BSGM_Default();
				return;
			default:
				break;
			}

			if (NPC->client->NPC_class == CLASS_ASSASSIN_DROID)
			{
				bubble_shield_update();
			}

			if (droideka_npc(NPC))
			{
				if (g_newgameplusJKO->integer || g_newgameplusJKA->integer)
				{
					npc_bsdroideka_default();
				}
				else
				{
					deka_bubble_shield_update();
					NPC_BehaviorSet_Stormtrooper(b_state);
				}
			}

			if (NPC_IsTrooper(NPC))
			{
				NPC_BehaviorSet_Trooper(b_state);
				return;
			}

			if (NPC->enemy && NPC->client->ps.weapon == WP_NONE && b_state != BS_HUNT_AND_KILL && !Q3_TaskIDPending(
				NPC, TID_MOVE_NAV))
			{
				//if in battle and have no weapon, run away, fixme: when in BS_HUNT_AND_KILL, they just stand there
				if (b_state != BS_FLEE)
				{
					NPC_StartFlee(NPC->enemy, NPC->enemy->currentOrigin, AEL_DANGER_GREAT, 5000, 10000);
				}
				else
				{
					NPC_BSFlee();
				}
				return;
			}
			if (NPC->client->ps.weapon == WP_SABER)
			{
				//special melee exception
				NPC_BehaviorSet_Default(b_state);
				return;
			}
			if (NPC->client->ps.weapon == WP_DISRUPTOR && NPCInfo->scriptFlags & SCF_ALT_FIRE)
			{
				//a sniper
				NPC_BehaviorSet_Sniper(b_state);
				return;
			}
			if (NPC->client->ps.weapon == WP_THERMAL
				|| NPC->client->ps.weapon == WP_MELEE) //FIXME: separate AI for melee fighters
			{
				//a grenadier
				NPC_BehaviorSet_Grenadier(b_state);
				return;
			}
			if (NPC_CheckSurrender())
			{
				return;
			}
			NPC_BehaviorSet_Stormtrooper(b_state);
			break;

		case TEAM_NEUTRAL:

			// special cases for enemy droids
			if (NPC->client->NPC_class == CLASS_PROTOCOL)
			{
				NPC_BehaviorSet_Default(b_state);
			}
			else if (NPC->client->NPC_class == CLASS_UGNAUGHT
				|| NPC->client->NPC_class == CLASS_JAWA)
			{
				//others, too?
				NPC_BSCivilian_Default(b_state);
			}
			// Add special vehicle behavior here.
			else if (NPC->client->NPC_class == CLASS_VEHICLE)
			{
				const Vehicle_t* pVehicle = NPC->m_pVehicle;
				if (!pVehicle->m_pPilot && pVehicle->m_iBoarding == 0)
				{
					if (pVehicle->m_pVehicleInfo->type == VH_ANIMAL)
					{
						NPC_BehaviorSet_Animal(b_state);
					}
				}
			}

			// Default behaviour for TEAM_NEUTRAL so they aren't treated as droids
			else if (NPC->client->ps.weapon != WP_NONE)
			{
				//special melee exception
				NPC_BehaviorSet_Default(b_state);
				return;
			}
			else
			{
				// Just one of the average droids
				NPC_BehaviorSet_Droid(b_state);
			}
			break;

		default:
			if (NPC->client->NPC_class == CLASS_SEEKER)
			{
				NPC_BehaviorSet_Seeker(b_state);
			}
			else
			{
				if (NPCInfo->charmedTime > level.time || NPCInfo->darkCharmedTime > level.time)
				{
					NPC_BehaviorSet_Charmed(b_state);
				}
				else
				{
					NPC_BehaviorSet_Default(b_state);
				}
				G_CheckCharmed(NPC);
			}
			break;
		}
	}
}

#else // MP game

/*
-------------------------
NPC_RunBehavior
-------------------------
*/
extern void NPC_BSRT_Default(void);
extern qboolean RT_Flying(const gentity_t* self);
extern void NPC_BSEmplaced(void);
extern qboolean NPC_CheckSurrender(void);
extern void Boba_FlyStop(gentity_t* self);
extern qboolean Jedi_CultistDestroyer(const gentity_t* self);
extern void bubble_shield_update(void);
extern void NPC_BSSD_Default(void);
extern void NPC_BSCivilian_Default(int b_state);

extern qboolean NPC_FighterAI(void);
extern qboolean Pilot_MasterUpdate(void); // AI_Vehicle.c

static void NPC_RunBehavior(const npcteam_t team, const int b_state)
{
	qboolean dontSetAim;

	// flying a fighter (ai_fighter.c: the space maps' empty ships)
	if (NPC_FighterAI())
	{
		return;
	}

	if (NPCS.NPC->s.NPC_class == CLASS_VEHICLE &&
		NPCS.NPC->m_pVehicle)
	{
		//vehicles don't do AI!
		return;
	}

	if (b_state == BS_CINEMATIC)
	{
		NPC_BSCinematic();
	}
	else if (NPCS.NPCInfo->scriptFlags & SCF_PILOT && Pilot_MasterUpdate())
	{
		// on a swoop or on the way to one (AI_Vehicle.c, as SP)
		return;
	}
	else if (NPC_JumpBackingUp())
	{
		//racc - NPC is backing up for a large jump
	}
	else if (!TIMER_Done(NPCS.NPC, "DEMP2_StunTime"))
	{
		//we've been stunned by a demp2 shot.
		NPC_UpdateAngles(qtrue, qtrue);
	}
	else if (NPCS.NPC->client->ps.weapon == WP_EMPLACED_GUN)
	{
		NPC_BSEmplaced();
		G_CheckCharmed(NPCS.NPC);
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_HOWLER)
	{
		NPC_BehaviorSet_Howler(b_state);
	}
	else if (Jedi_CultistDestroyer(NPCS.NPC))
	{
		NPC_BSJedi_Default();
		dontSetAim = qtrue;
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_SABER_DROID)
	{
		//saber droid
		NPC_BSSD_Default();
	}
	else if (NPCS.NPC->client->ps.weapon == WP_SABER ||
		(NPCS.NPC->client->NPC_class == CLASS_SITHLORD && NPCS.NPC->client->ps.weapon == WP_MELEE) ||
		(NPCS.NPC->client->NPC_class == CLASS_SITHLORD && NPCS.NPC->client->ps.weapon == WP_NONE))
	{
		//jedi (as SP: also an unarmed / melee Sith lord)
		NPC_BehaviorSet_Jedi(b_state);
		dontSetAim = qtrue;
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_REBORN && NPCS.NPC->client->ps.weapon == WP_MELEE)
	{
		//force-only reborn
		NPC_BehaviorSet_Jedi(b_state);
		dontSetAim = qtrue;
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_JEDI ||
		NPCS.NPC->client->NPC_class == CLASS_REBORN ||
		NPCS.NPC->client->NPC_class == CLASS_TAVION ||
		NPCS.NPC->client->ps.weapon == WP_SABER)
	{
		//jedi
		NPC_BehaviorSet_Jedi(b_state);
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_RANCOR)
	{
		NPC_BehaviorSet_Rancor(b_state);
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_SAND_CREATURE)
	{
		NPC_BehaviorSet_SandCreature(b_state);
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_WAMPA)
	{
		//wampa
		NPC_BSWampa_Default();
		G_CheckCharmed(NPCS.NPC);
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_REMOTE)
	{
		NPC_BehaviorSet_Remote(b_state);
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_SEEKER)
	{
		NPC_BehaviorSet_Seeker(b_state);
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_BOBAFETT)
	{
		//bounty hunter: as SP, update / flee / tactics first (NPC_AI_BobaFett.c)
		Boba_Update();
		if (NPCS.NPCInfo->surrenderTime)
		{
			Boba_Flee();
		}
		else if (!Boba_TacticsLocked())
		{
			if (Boba_Flying(NPCS.NPC))
			{
				NPC_BehaviorSet_Seeker(b_state);
			}
			else
			{
				NPC_BehaviorSet_Jedi(b_state);
			}
		}
		dontSetAim = qtrue;
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER)
	{
		//bounty hunter
		if (RT_Flying(NPCS.NPC) || NPCS.NPC->enemy != NULL)
		{
			NPC_BSRT_Default();
		}
		else
		{
			NPC_BehaviorSet_Stormtrooper(b_state);
		}
		G_CheckCharmed(NPCS.NPC);
		dontSetAim = qtrue;
	}
	else if (Jedi_CultistDestroyer(NPCS.NPC))
	{
		NPC_BSJedi_Default();
	}
	else if (NPCS.NPCInfo->scriptFlags & SCF_FORCED_MARCH)
	{
		//being forced to march
		NPC_BSDefault();
	}
	else if (NPCS.NPC->client->ps.weapon == WP_TUSKEN_RIFLE)
	{
		//as SP: a tusken rifle with alt fire is a sniper, else the tusken AI
		if (NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)
		{
			NPC_BehaviorSet_Sniper(b_state);
		}
		else
		{
			NPC_BehaviorSet_Tusken(b_state);
		}
		G_CheckCharmed(NPCS.NPC);
	}
	else if (NPCS.NPC->client->ps.weapon == WP_TUSKEN_STAFF)
	{
		NPC_BehaviorSet_Tusken(b_state);
		G_CheckCharmed(NPCS.NPC);
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_GONK || NPCS.NPC->client->NPC_class == CLASS_MOUSE
		|| NPCS.NPC->client->NPC_class == CLASS_R2D2 || NPCS.NPC->client->NPC_class == CLASS_R5D2)
	{
		// SJE: the weaponless droids wander and keep out of fights like SP's (team NEUTRAL there). MP's NPC file
		// parser never reads a team (it looks up "NPCplayerTeam"), so a gonk ran the combat AI: it chased players,
		// "fired" WP_NONE every frame and slid along in anims it does not have.
		NPC_BehaviorSet_Droid(b_state);
	}
	else
	{
		switch (team)
		{
		case (npcteam_t)NPCTEAM_ENEMY:
			// special cases for enemy droids
			switch (NPCS.NPC->client->NPC_class)
			{
			case CLASS_ALORA:
				NPC_BehaviorSet_Jedi(b_state);
				return;
			case CLASS_ATST:
				NPC_BehaviorSet_ATST(b_state);
				return;
			case CLASS_PROBE:
				NPC_BehaviorSet_ImperialProbe(b_state);
				return;
			case CLASS_REMOTE:
				NPC_BehaviorSet_Remote(b_state);
				return;
			case CLASS_SENTRY:
				NPC_BehaviorSet_Sentry(b_state);
				return;
			case CLASS_INTERROGATOR:
				NPC_BehaviorSet_Interrogator(b_state);
				return;
			case CLASS_MINEMONSTER:
				NPC_BehaviorSet_MineMonster(b_state);
				return;
			case CLASS_HOWLER:
				NPC_BehaviorSet_Howler(b_state);
				return;
			case CLASS_RANCOR:
				NPC_BehaviorSet_Rancor(b_state);
				return;
			case CLASS_SAND_CREATURE:
				NPC_BehaviorSet_SandCreature(b_state);
				return;
			case CLASS_SABOTEUR:
			case CLASS_HAZARD_TROOPER:
			case CLASS_REELO:
			case CLASS_COMMANDO:
			case CLASS_IMPERIAL:
			case CLASS_RODIAN:
			case CLASS_TRANDOSHAN:
			{
				if (NPCS.NPC->client->ps.weapon == WP_DISRUPTOR && NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)
				{
					//a sniper
					NPC_BehaviorSet_Sniper(b_state);
					return;
				}
				NPC_BehaviorSet_Stormtrooper(b_state);
				return;
			}
			case CLASS_TUSKEN:
			{
				if (NPCS.NPC->client->ps.weapon == WP_DISRUPTOR /*&& (NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)*/)
				{
					//a sniper
					NPC_BehaviorSet_Sniper(b_state);
					return;
				}
				NPC_BehaviorSet_Stormtrooper(b_state);
				return;
			}
			case CLASS_MARK1:
				NPC_BehaviorSet_Mark1(b_state);
				return;
			case CLASS_MARK2:
				NPC_BehaviorSet_Mark2(b_state);
				return;
			case CLASS_GALAKMECH:
				NPC_BSGM_Default();
				return;
			default:
				break;
			}
			/*if (NPCS.NPC->client->NPC_class == CLASS_ASSASSIN_DROID)
			{
				BubbleShield_Update();
			}*/

			if (NPCS.NPC->client->NPC_class == CLASS_DROIDEKA)
			{
				bubble_shield_update();
			}

			/*if (NPCS.NPC->client->NPC_class == CLASS_GALAKMECH)
			{
				BubbleShield_Update();
			}*/

			if (NPC_IsTrooper(NPCS.NPC))
			{
				NPC_BehaviorSet_HazardTroop(b_state); // (SP's hazard trooper squads, NPC_AI_HazardTrooper.c)
				return;
			}

			if (NPCS.NPC->enemy && NPCS.NPC->s.weapon == WP_NONE && b_state != BS_HUNT_AND_KILL && !trap->
				ICARUS_TaskIDPending(
					(sharedEntity_t*)NPCS.NPC, TID_MOVE_NAV))
			{
				//if in battle and have no weapon, run away, fixme: when in BS_HUNT_AND_KILL, they just stand there
				if (b_state != BS_FLEE)
				{
					NPC_StartFlee(NPCS.NPC->enemy, NPCS.NPC->enemy->r.currentOrigin, AEL_DANGER_GREAT, 5000, 10000);
				}
				else
				{
					NPC_BSFlee();
				}
				return;
			}
			if (NPCS.NPC->client->ps.weapon == WP_SABER)
			{
				//special melee exception
				NPC_BehaviorSet_Default(b_state);
				return;
			}
			if (NPCS.NPC->client->ps.weapon == WP_DISRUPTOR && NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)
			{
				//a sniper
				NPC_BehaviorSet_Sniper(b_state);
				return;
			}
			if (NPCS.NPC->client->ps.weapon == WP_THERMAL || NPCS.NPC->client->ps.weapon == WP_MELEE)
			{
				//a grenadier
				NPC_BehaviorSet_Grenadier(b_state);
				return;
			}
			if (NPC_CheckSurrender())
			{
				return;
			}
			NPC_BehaviorSet_Stormtrooper(b_state);
			break;

		case (npcteam_t)NPCTEAM_NEUTRAL:

			// special cases for enemy droids
			if (NPCS.NPC->client->NPC_class == CLASS_PROTOCOL)
			{
				NPC_BehaviorSet_Default(b_state);
			}
			else if (NPCS.NPC->client->NPC_class == CLASS_UGNAUGHT || NPCS.NPC->client->NPC_class == CLASS_JAWA)
			{
				//others, too?
				NPC_BSCivilian_Default(b_state);
			}
			else if (NPCS.NPC->client->NPC_class == CLASS_VEHICLE)
			{
				const Vehicle_t* pVehicle = NPCS.NPC->m_pVehicle;

				if (pVehicle && !pVehicle->m_pPilot && pVehicle->m_iBoarding == 0)
				{
					if (!pVehicle)
					{
						// no vehicle attached to this NPC; nothing to do
						return;
					}
					//racc - no pilot
					if (pVehicle->m_pVehicleInfo->type == VH_ANIMAL)
					{
						//animal vehicles act like animals while not being rode.
						NPC_BehaviorSet_Animal(b_state);
					}
				}
			}
			else
			{
				// Just one of the average droids
				NPC_BehaviorSet_Droid(b_state);
			}
			break;

		default:
			if (NPCS.NPC->client->NPC_class == CLASS_SEEKER)
			{
				NPC_BehaviorSet_Seeker(b_state);
			}
			else
			{
				if (NPCS.NPCInfo->charmedTime > level.time)
				{
					NPC_BehaviorSet_Charmed(b_state);
				}
				else
				{
					NPC_BehaviorSet_Default(b_state);
				}
				G_CheckCharmed(NPCS.NPC);
				dontSetAim = qtrue;
			}
			break;
		}
	}
}

#endif
static bState_t G_CurrentBState(gNPC_t* gNPC)
{
	if (gNPC->tempBehavior != BS_DEFAULT)
	{
		//Overrides normal behavior until cleared
		return gNPC->tempBehavior;
	}

	if (gNPC->behaviorState == BS_DEFAULT)
	{
		gNPC->behaviorState = gNPC->defaultBehavior;
	}

	return gNPC->behaviorState;
}
#ifdef SP_GAME

/*
===============
NPC_ExecuteBState

  MCG

NPC Behavior state thinking

===============
*/
extern cvar_t* g_npcChatter;
extern stringID_table_t ClassTable[];
extern qboolean G_ControlledByPlayer(const gentity_t* self);

//---------------------------------------------------------------------------------------------------------------------
// NPC chatter (2026-11-16, DH; after Jedi Knight Galaxies' NPC conversations, NPC_Conversations.cpp; sounds and voice
// sets in ext_data/sje_chatter.dat): two idle NPCs of a voice set standing close together (no enemy, not in a
// script) turn to each other and talk, taking turns with talk animations. Stormtroopers and mercs play a whole little
// dialog; the civilian sets (Rodians, Gran, Trandoshans, Weequay, Jawas, droids...) trade random lines and may mix.
// It stops at once when either sees an enemy, gets hurt or walks off. Only one talk within CHAT_QUIET_RANGE, and
// each NPC looks for a partner only every few seconds (cheap with many NPCs). g_npcChatter 0 = off.
//---------------------------------------------------------------------------------------------------------------------
#define CHATTER_FILE			"ext_data/sje_chatter.dat"
#define MAX_CHAT_SETS			16
#define MAX_CHAT_DIALOGS		16
#define MAX_CHAT_DIALOG_LINES	8
#define MAX_CHAT_LINES			128
#define MAX_CHAT_CLASSES		8
#define MAX_ACTIVE_CHATS		16
#define CHAT_PARTNER_RANGE		128.0f	// JKG: within 128 units
#define CHAT_QUIET_RANGE		1024.0f	// no second talk this close to one going on
#define CHAT_COOLDOWN			45000	// ms before an NPC talks again

typedef struct chatSet_s
{
	char name[32];
	int classes[MAX_CHAT_CLASSES];
	int numClasses;
	char types[MAX_CHAT_CLASSES][32]; // parts of NPC type names (lower case)
	int numTypes;
	char dialog[MAX_CHAT_DIALOGS][MAX_CHAT_DIALOG_LINES][MAX_QPATH];
	int dialogLines[MAX_CHAT_DIALOGS];
	int numDialogs;
	char line[MAX_CHAT_LINES][MAX_QPATH];
	int numLines;
} chatSet_t;

static chatSet_t chatSets[MAX_CHAT_SETS];
static int numChatSets = -1; // -1 = not loaded yet
static int activeChats[MAX_ACTIVE_CHATS]; // entity number + 1 of each talk's leader, 0 = free

static void NPC_ChatterLoad(void)
{
	char* buf = nullptr;
	char* file = nullptr;
	const char* p;

	numChatSets = 0;
	memset(chatSets, 0, sizeof chatSets);
	memset(activeChats, 0, sizeof activeChats);
	if (gi.FS_ReadFile(CHATTER_FILE, reinterpret_cast<void**>(&file)) <= 0 || !file)
	{
		return;
	}
	buf = file;
	p = buf;
	COM_BeginParseSession(CHATTER_FILE);
	while (p && numChatSets < MAX_CHAT_SETS)
	{
		chatSet_t* cs = &chatSets[numChatSets];
		const char* token = COM_ParseExt(&p, qtrue);

		if (!token[0])
		{
			break;
		}
		Q_strncpyz(cs->name, token, sizeof cs->name);
		token = COM_ParseExt(&p, qtrue);
		if (Q_stricmp(token, "{"))
		{
			break;
		}
		while (1)
		{
			char key[32];

			token = COM_ParseExt(&p, qtrue);
			if (!token[0] || !Q_stricmp(token, "}"))
			{
				break;
			}
			Q_strncpyz(key, token, sizeof key);
			token = COM_ParseExt(&p, qfalse);
			if (!Q_stricmp(key, "classes") || !Q_stricmp(key, "types") || !Q_stricmp(key, "dialog"))
			{
				// a list: space separated (classes, types) or ";" separated (dialog)
				char list[1024];
				char* s;
				const char sep = !Q_stricmp(key, "dialog") ? ';' : ' ';
				int n = 0;

				Q_strncpyz(list, token, sizeof list);
				s = list;
				while (*s)
				{
					char* e = strchr(s, sep);
					if (e)
					{
						*e = 0;
					}
					if (*s)
					{
						if (!Q_stricmp(key, "classes") && cs->numClasses < MAX_CHAT_CLASSES)
						{
							const int c = GetIDForString(ClassTable, va("CLASS_%s", s));
							if (c >= 0)
							{
								cs->classes[cs->numClasses++] = c;
							}
						}
						else if (!Q_stricmp(key, "types") && cs->numTypes < MAX_CHAT_CLASSES)
						{
							Q_strncpyz(cs->types[cs->numTypes], s, sizeof cs->types[0]);
							Q_strlwr(cs->types[cs->numTypes]);
							cs->numTypes++;
						}
						else if (!Q_stricmp(key, "dialog") && cs->numDialogs < MAX_CHAT_DIALOGS && n < MAX_CHAT_DIALOG_LINES)
						{
							Q_strncpyz(cs->dialog[cs->numDialogs][n], s, MAX_QPATH);
							n++;
						}
					}
					if (!e)
					{
						break;
					}
					s = e + 1;
				}
				if (!Q_stricmp(key, "dialog") && n > 0 && cs->numDialogs < MAX_CHAT_DIALOGS)
				{
					cs->dialogLines[cs->numDialogs++] = n;
				}
			}
			else if (!Q_stricmp(key, "line") && cs->numLines < MAX_CHAT_LINES)
			{
				Q_strncpyz(cs->line[cs->numLines++], token, MAX_QPATH);
			}
		}
		if (cs->numDialogs || cs->numLines)
		{
			numChatSets++;
		}
	}
	gi.FS_FreeFile(file);
}

#else // MP game

/*
===============
NPC_ExecuteBState

  MCG

NPC Behavior state thinking

===============
*/
extern stringID_table_t ClassTable[];

//---------------------------------------------------------------------------------------------------------------------
// NPC chatter (2026-11-16, DH; after Jedi Knight Galaxies' NPC conversations, NPC_Conversations.cpp; sounds and voice
// sets in ext_data/sje_chatter.dat): two idle NPCs of a voice set standing close together (no enemy, not in a
// script) turn to each other and talk, taking turns with talk animations. Stormtroopers and mercs play a whole little
// dialog; the civilian sets (Rodians, Gran, Trandoshans, Weequay, Jawas, droids...) trade random lines and may mix.
// It stops at once when either sees an enemy, gets hurt or walks off. Only one talk within CHAT_QUIET_RANGE, and
// each NPC looks for a partner only every few seconds (cheap with many NPCs). g_npcChatter 0 = off.
//---------------------------------------------------------------------------------------------------------------------
#define CHATTER_FILE			"ext_data/sje_chatter.dat"
#define MAX_CHAT_SETS			16
#define MAX_CHAT_DIALOGS		16
#define MAX_CHAT_DIALOG_LINES	8
#define MAX_CHAT_LINES			128
#define MAX_CHAT_CLASSES		8
#define MAX_ACTIVE_CHATS		16
#define CHAT_PARTNER_RANGE		128.0f	// JKG: within 128 units
#define CHAT_QUIET_RANGE		1024.0f	// no second talk this close to one going on
#define CHAT_COOLDOWN			45000	// ms before an NPC talks again

typedef struct chatSet_s
{
	char name[32];
	int classes[MAX_CHAT_CLASSES];
	int numClasses;
	char types[MAX_CHAT_CLASSES][32]; // parts of NPC type names (lower case)
	int numTypes;
	char dialog[MAX_CHAT_DIALOGS][MAX_CHAT_DIALOG_LINES][MAX_QPATH];
	int dialogLines[MAX_CHAT_DIALOGS];
	int numDialogs;
	char line[MAX_CHAT_LINES][MAX_QPATH];
	int numLines;
} chatSet_t;

static chatSet_t chatSets[MAX_CHAT_SETS];
static int numChatSets = -1; // -1 = not loaded yet
static int activeChats[MAX_ACTIVE_CHATS]; // entity number + 1 of each talk's leader, 0 = free

static void NPC_ChatterLoad(void)
{
	char* buf = NULL;
	const char* p;

	numChatSets = 0;
	memset(chatSets, 0, sizeof chatSets);
	memset(activeChats, 0, sizeof activeChats);
	{
		static char chat_buf[65536];
		fileHandle_t f = 0;
		const int len = trap->FS_Open(CHATTER_FILE, &f, FS_READ);

		if (len <= 0 || len >= (int)sizeof chat_buf)
		{
			if (f)
			{
				trap->FS_Close(f);
			}
			return;
		}
		trap->FS_Read(chat_buf, len, f);
		trap->FS_Close(f);
		chat_buf[len] = 0;
		buf = chat_buf;
	}
	p = buf;
	COM_BeginParseSession(CHATTER_FILE);
	while (p && numChatSets < MAX_CHAT_SETS)
	{
		chatSet_t* cs = &chatSets[numChatSets];
		const char* token = COM_ParseExt(&p, qtrue);

		if (!token[0])
		{
			break;
		}
		Q_strncpyz(cs->name, token, sizeof cs->name);
		token = COM_ParseExt(&p, qtrue);
		if (Q_stricmp(token, "{"))
		{
			break;
		}
		while (1)
		{
			char key[32];

			token = COM_ParseExt(&p, qtrue);
			if (!token[0] || !Q_stricmp(token, "}"))
			{
				break;
			}
			Q_strncpyz(key, token, sizeof key);
			token = COM_ParseExt(&p, qfalse);
			if (!Q_stricmp(key, "classes") || !Q_stricmp(key, "types") || !Q_stricmp(key, "dialog"))
			{
				// a list: space separated (classes, types) or ";" separated (dialog)
				char list[1024];
				char* s;
				const char sep = !Q_stricmp(key, "dialog") ? ';' : ' ';
				int n = 0;

				Q_strncpyz(list, token, sizeof list);
				s = list;
				while (*s)
				{
					char* e = strchr(s, sep);
					if (e)
					{
						*e = 0;
					}
					if (*s)
					{
						if (!Q_stricmp(key, "classes") && cs->numClasses < MAX_CHAT_CLASSES)
						{
							const int c = GetIDForString(ClassTable, va("CLASS_%s", s));
							if (c >= 0)
							{
								cs->classes[cs->numClasses++] = c;
							}
						}
						else if (!Q_stricmp(key, "types") && cs->numTypes < MAX_CHAT_CLASSES)
						{
							Q_strncpyz(cs->types[cs->numTypes], s, sizeof cs->types[0]);
							Q_strlwr(cs->types[cs->numTypes]);
							cs->numTypes++;
						}
						else if (!Q_stricmp(key, "dialog") && cs->numDialogs < MAX_CHAT_DIALOGS && n < MAX_CHAT_DIALOG_LINES)
						{
							Q_strncpyz(cs->dialog[cs->numDialogs][n], s, MAX_QPATH);
							n++;
						}
					}
					if (!e)
					{
						break;
					}
					s = e + 1;
				}
				if (!Q_stricmp(key, "dialog") && n > 0 && cs->numDialogs < MAX_CHAT_DIALOGS)
				{
					cs->dialogLines[cs->numDialogs++] = n;
				}
			}
			else if (!Q_stricmp(key, "line") && cs->numLines < MAX_CHAT_LINES)
			{
				Q_strncpyz(cs->line[cs->numLines++], token, MAX_QPATH);
			}
		}
		if (cs->numDialogs || cs->numLines)
		{
			numChatSets++;
		}
	}
}

#endif
// the voice set an NPC talks with, -1 = none (cached in chatSet: 0 = not looked up, else set + 1, -1 = none)
static int NPC_ChatterSet(gentity_t* ent)
{
	int i, j;

	if (ent->chatSet)
	{
		return ent->chatSet > 0 ? ent->chatSet - 1 : -1;
	}
	ent->chatSet = -1;
	for (i = 0; i < numChatSets; i++)
	{
		for (j = 0; j < chatSets[i].numClasses; j++)
		{
			if (ent->client->NPC_class == chatSets[i].classes[j])
			{
				ent->chatSet = i + 1;
				return i;
			}
		}
		for (j = 0; j < chatSets[i].numTypes && ent->NPC_type; j++)
		{
			char type[64];
			Q_strncpyz(type, ent->NPC_type, sizeof type);
			Q_strlwr(type);
			if (strstr(type, chatSets[i].types[j]))
			{
				ent->chatSet = i + 1;
				return i;
			}
		}
	}
	return -1;
}
#ifdef SP_GAME

// can this NPC talk now (idle, alive, no enemy, standing, not scripted)
static qboolean NPC_ChatterFree(const gentity_t* ent)
{
	return ent && ent->inuse && ent->client && ent->NPC && ent->health > 0 && !ent->enemy
		&& ent->NPC->behaviorState != BS_CINEMATIC && VectorLength(ent->client->ps.velocity) <= 16.0f
		&& ent->painDebounceTime < level.time && !G_ControlledByPlayer(ent) ? qtrue : qfalse;
}

#else // MP game

// can this NPC talk now (idle, alive, no enemy, standing, not scripted)
static qboolean NPC_ChatterFree(const gentity_t* ent)
{
	return ent && ent->inuse && ent->client && ent->NPC && ent->health > 0 && !ent->enemy
		&& ent->NPC->behaviorState != BS_CINEMATIC && VectorLength(ent->client->ps.velocity) <= 16.0f
		&& ent->painDebounceTime < level.time;
}

#endif
static void NPC_ChatterEnd(gentity_t* ent)
{
	gentity_t* other = ent->chatPartner > 0 ? &g_entities[ent->chatPartner - 1] : NULL;
	int i;

	for (i = 0; i < MAX_ACTIVE_CHATS; i++)
	{
		if (activeChats[i] == ent->s.number + 1 || (other && activeChats[i] == other->s.number + 1))
		{
			activeChats[i] = 0;
		}
	}
	if (other && other->chatPartner == ent->s.number + 1)
	{
		other->chatPartner = 0;
		other->chatNext = level.time + CHAT_COOLDOWN + Q_irand(0, 15000);
	}
	ent->chatPartner = 0;
	ent->chatNext = level.time + CHAT_COOLDOWN + Q_irand(0, 15000);
}

static void NPC_ChatterFace(gentity_t* ent, const gentity_t* other)
{
	vec3_t dir, angles;

	VectorSubtract(other->SHR_currentOrigin, ent->SHR_currentOrigin, dir);
	vectoangles(dir, angles);
	ent->NPC->desiredYaw = AngleNormalize360(angles[YAW]);
}
#ifdef SP_GAME

static void NPC_ChatterSay(gentity_t* speaker, const char* sound)
{
	G_SoundOnEnt(speaker, CHAN_VOICE_ATTEN, sound);
	if (g_npcChatter->integer > 1)
	{
		Com_Printf("chatter: %i (%s) says %s\n", speaker->s.number, speaker->NPC_type, sound); // g_npcChatter 2 = debug
	}
	NPC_SetAnim(speaker, SETANIM_TORSO, Q_irand(0, 1) ? BOTH_TALK1 : BOTH_TALK2, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
}

// every NPC think (after its behaviour state)
extern bool in_camera;

void NPC_ChatterThink(gentity_t* ent)
{
	gentity_t* other;
	int set, i;

	if (!(g_npcChatter && g_npcChatter->integer) || !ent || !ent->client || !ent->NPC || in_camera)
	{
		return;
	}
	if (numChatSets < 0)
	{
		NPC_ChatterLoad();
	}
	if (!numChatSets)
	{
		return;
	}

	if (ent->chatPartner > 0)
	{
		// talking: the leader (chatRole 1) runs the talk
		other = &g_entities[ent->chatPartner - 1];
		if (!NPC_ChatterFree(ent) || !NPC_ChatterFree(other) || other->chatPartner != ent->s.number + 1
			|| DistanceSquared(ent->currentOrigin, other->currentOrigin) > 200.0f * 200.0f)
		{
			NPC_ChatterEnd(ent);
			return;
		}
		NPC_ChatterFace(ent, other);
		if (ent->chatRole != 1 || level.time < ent->chatTime)
		{
			return;
		}
		set = NPC_ChatterSet(ent);
		if (set < 0)
		{
			NPC_ChatterEnd(ent);
			return;
		}
		{
			const chatSet_t* cs = &chatSets[set];
			gentity_t* speaker = ent->chatLine & 1 ? other : ent;
			const chatSet_t* speaker_set = &chatSets[NPC_ChatterSet(speaker) >= 0 ? NPC_ChatterSet(speaker) : set];

			if (ent->chatDialog >= 0)
			{
				if (ent->chatLine >= cs->dialogLines[ent->chatDialog])
				{
					NPC_ChatterEnd(ent);
					return;
				}
				NPC_ChatterSay(speaker, cs->dialog[ent->chatDialog][ent->chatLine]);
			}
			else
			{
				if (ent->chatLine >= ent->chatLines || !speaker_set->numLines)
				{
					NPC_ChatterEnd(ent);
					return;
				}
				NPC_ChatterSay(speaker, speaker_set->line[Q_irand(0, speaker_set->numLines - 1)]);
			}
			ent->chatLine++;
			ent->chatTime = level.time + Q_irand(3500, 6000);
		}
		return;
	}

	// looking for someone to talk to, every few seconds
	if (level.time < ent->chatNext)
	{
		return;
	}
	ent->chatNext = level.time + Q_irand(3000, 5000);
	if (!NPC_ChatterFree(ent) || (set = NPC_ChatterSet(ent)) < 0)
	{
		return;
	}
	for (i = 0; i < MAX_ACTIVE_CHATS; i++)
	{
		const gentity_t* lead = activeChats[i] ? &g_entities[activeChats[i] - 1] : nullptr;
		if (lead && (!lead->inuse || lead->chatPartner <= 0))
		{
			activeChats[i] = 0; // a talk that ended without telling us
			continue;
		}
		if (lead && DistanceSquared(lead->currentOrigin, ent->currentOrigin) < CHAT_QUIET_RANGE * CHAT_QUIET_RANGE)
		{
			return; // someone is already talking near here
		}
	}
	{
		static gentity_t* near_list[MAX_GENTITIES];
		const int count = G_RadiusList(ent->currentOrigin, CHAT_PARTNER_RANGE, ent, qtrue, near_list);
		const qboolean scripted = chatSets[set].numDialogs > 0 ? qtrue : qfalse;
		int slot = -1;

		for (i = 0; i < MAX_ACTIVE_CHATS; i++)
		{
			if (!activeChats[i])
			{
				slot = i;
				break;
			}
		}
		if (slot < 0)
		{
			return;
		}
		for (i = 0; i < count; i++)
		{
			int other_set;

			other = near_list[i];
			if (!other || other == ent || !NPC_ChatterFree(other) || other->chatPartner > 0 || other->chatNext > level.time + CHAT_COOLDOWN)
			{
				continue;
			}
			other_set = NPC_ChatterSet(other);
			if (other_set < 0 || (scripted && other_set != set) || (!scripted && chatSets[other_set].numDialogs > 0))
			{
				continue; // troopers / mercs only with their own kind, civilians with any civilians
			}
			// start: this one leads
			ent->chatPartner = other->s.number + 1;
			other->chatPartner = ent->s.number + 1;
			ent->chatRole = 1;
			other->chatRole = 2;
			ent->chatLine = 0;
			ent->chatDialog = scripted ? Q_irand(0, chatSets[set].numDialogs - 1) : -1;
			ent->chatLines = Q_irand(4, 6);
			ent->chatTime = level.time + 500;
			activeChats[slot] = ent->s.number + 1;
			return;
		}
	}
}

extern void G_CivilianReset(void);

#else // MP game

static void NPC_ChatterSay(gentity_t* speaker, const char* sound)
{
	G_SoundOnEnt(speaker, CHAN_VOICE_ATTEN, sound);
	if (g_npcChatter.integer > 1)
	{
		Com_Printf("chatter: %i (%s) says %s\n", speaker->s.number, speaker->NPC_type, sound); // g_npcChatter 2 = debug
	}
	NPC_SetAnim(speaker, SETANIM_TORSO, Q_irand(0, 1) ? BOTH_TALK1 : BOTH_TALK2, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
}

// every NPC think (after its behaviour state)
void NPC_ChatterThink(gentity_t* ent)
{
	gentity_t* other;
	int set, i;

	if (!g_npcChatter.integer || !ent || !ent->client || !ent->NPC)
	{
		return;
	}
	if (numChatSets < 0)
	{
		NPC_ChatterLoad();
	}
	if (!numChatSets)
	{
		return;
	}

	if (ent->chatPartner > 0)
	{
		// talking: the leader (chatRole 1) runs the talk
		other = &g_entities[ent->chatPartner - 1];
		if (!NPC_ChatterFree(ent) || !NPC_ChatterFree(other) || other->chatPartner != ent->s.number + 1
			|| DistanceSquared(ent->r.currentOrigin, other->r.currentOrigin) > 200.0f * 200.0f)
		{
			NPC_ChatterEnd(ent);
			return;
		}
		NPC_ChatterFace(ent, other);
		if (ent->chatRole != 1 || level.time < ent->chatTime)
		{
			return;
		}
		set = NPC_ChatterSet(ent);
		if (set < 0)
		{
			NPC_ChatterEnd(ent);
			return;
		}
		{
			const chatSet_t* cs = &chatSets[set];
			gentity_t* speaker = ent->chatLine & 1 ? other : ent;
			const chatSet_t* speaker_set = &chatSets[NPC_ChatterSet(speaker) >= 0 ? NPC_ChatterSet(speaker) : set];

			if (ent->chatDialog >= 0)
			{
				if (ent->chatLine >= cs->dialogLines[ent->chatDialog])
				{
					NPC_ChatterEnd(ent);
					return;
				}
				NPC_ChatterSay(speaker, cs->dialog[ent->chatDialog][ent->chatLine]);
			}
			else
			{
				if (ent->chatLine >= ent->chatLines || !speaker_set->numLines)
				{
					NPC_ChatterEnd(ent);
					return;
				}
				NPC_ChatterSay(speaker, speaker_set->line[Q_irand(0, speaker_set->numLines - 1)]);
			}
			ent->chatLine++;
			ent->chatTime = level.time + Q_irand(3500, 6000);
		}
		return;
	}

	// looking for someone to talk to, every few seconds
	if (level.time < ent->chatNext)
	{
		return;
	}
	ent->chatNext = level.time + Q_irand(3000, 5000);
	if (!NPC_ChatterFree(ent) || (set = NPC_ChatterSet(ent)) < 0)
	{
		return;
	}
	for (i = 0; i < MAX_ACTIVE_CHATS; i++)
	{
		const gentity_t* lead = activeChats[i] ? &g_entities[activeChats[i] - 1] : NULL;
		if (lead && (!lead->inuse || lead->chatPartner <= 0))
		{
			activeChats[i] = 0; // a talk that ended without telling us
			continue;
		}
		if (lead && DistanceSquared(lead->r.currentOrigin, ent->r.currentOrigin) < CHAT_QUIET_RANGE * CHAT_QUIET_RANGE)
		{
			return; // someone is already talking near here
		}
	}
	{
		static gentity_t* near_list[MAX_GENTITIES];
		const int count = G_RadiusList(ent->r.currentOrigin, CHAT_PARTNER_RANGE, ent, qtrue, near_list);
		const qboolean scripted = chatSets[set].numDialogs > 0;
		int slot = -1;

		for (i = 0; i < MAX_ACTIVE_CHATS; i++)
		{
			if (!activeChats[i])
			{
				slot = i;
				break;
			}
		}
		if (slot < 0)
		{
			return;
		}
		for (i = 0; i < count; i++)
		{
			int other_set;

			other = near_list[i];
			if (!other || other == ent || !NPC_ChatterFree(other) || other->chatPartner > 0 || other->chatNext > level.time + CHAT_COOLDOWN)
			{
				continue;
			}
			other_set = NPC_ChatterSet(other);
			if (other_set < 0 || (scripted && other_set != set) || (!scripted && chatSets[other_set].numDialogs > 0))
			{
				continue; // troopers / mercs only with their own kind, civilians with any civilians
			}
			// start: this one leads
			ent->chatPartner = other->s.number + 1;
			other->chatPartner = ent->s.number + 1;
			ent->chatRole = 1;
			other->chatRole = 2;
			ent->chatLine = 0;
			ent->chatDialog = scripted ? Q_irand(0, chatSets[set].numDialogs - 1) : -1;
			ent->chatLines = Q_irand(4, 6);
			ent->chatTime = level.time + 500;
			activeChats[slot] = ent->s.number + 1;
			return;
		}
	}
}

extern void G_CivilianReset(void);


// Bots (ai_main.c bot_social_think, 2026-11-16): a random line of the voice set this bot / NPC has, qfalse = none
qboolean NPC_ChatterRandomLine(gentity_t* ent, char* out, const int out_size)
{
	int set;
	const chatSet_t* cs;

	if (numChatSets < 0)
	{
		NPC_ChatterLoad();
	}
	if (!ent || !ent->client || (set = NPC_ChatterSet(ent)) < 0)
	{
		return qfalse;
	}
	cs = &chatSets[set];
	if (cs->numLines)
	{
		Q_strncpyz(out, cs->line[Q_irand(0, cs->numLines - 1)], out_size);
		return qtrue;
	}
	if (cs->numDialogs)
	{
		const int d = Q_irand(0, cs->numDialogs - 1);
		Q_strncpyz(out, cs->dialog[d][Q_irand(0, cs->dialogLines[d] - 1)], out_size);
		return qtrue;
	}
	return qfalse;
}

#endif
// a new map: reload the sets (the file may differ per mod) and forget old talks
void NPC_ChatterReset(void)
{
	numChatSets = -1;
}
#ifdef SP_GAME

static void NPC_ExecuteBState(const gentity_t* self) //, int msec )
{
	NPC_HandleAIFlags();

	//FIXME: these next three bits could be a function call, some sort of setup/cleanup func
	//Lookmode must be reset every think cycle
	if (NPC->delayScriptTime && NPC->delayScriptTime <= level.time)
	{
		G_ActivateBehavior(NPC, BSET_DELAYED);
		NPC->delayScriptTime = 0;
	}

	//Clear this and let b_state set it itself, so it automatically handles changing bStates... but we need a set b_state wrapper func
	NPCInfo->combatMove = qfalse;

	//Execute our b_state
	const bState_t b_state = G_CurrentBState(NPCInfo);

	//Pick the proper bstate for us and run it
	NPC_RunBehavior(self->client->playerTeam, b_state);

	if (NPC->enemy)
	{
		if (!NPC->enemy->inuse)
		{
			//just in case b_state doesn't catch this
			G_ClearEnemy(NPC);
		}
	}

	if (NPC->client->ps.saberLockTime && NPC->client->ps.saberLockEnemy != ENTITYNUM_NONE)
	{
		NPC_SetLookTarget(NPC, NPC->client->ps.saberLockEnemy, level.time + 1000);
	}
	else if (!NPC_CheckLookTarget(NPC))
	{
		if (NPC->enemy)
		{
			NPC_SetLookTarget(NPC, NPC->enemy->s.number, 0);
		}
	}

	if (NPC->enemy)
	{
		if (NPC->enemy->flags & FL_DONT_SHOOT)
		{
			ucmd.buttons &= ~BUTTON_ATTACK;
			ucmd.buttons &= ~BUTTON_ALT_ATTACK;
		}
		else if (NPC->client->playerTeam != TEAM_ENEMY //not an enemy
			&& (NPC->client->playerTeam != TEAM_FREE || NPC->client->NPC_class == CLASS_TUSKEN && Q_irand(0, 4))
			//not a rampaging creature or I'm a tusken and I feel generous (temporarily)
			&& NPC->enemy->NPC
			&& (NPC->enemy->NPC->surrenderTime > level.time || NPC->enemy->NPC->scriptFlags & SCF_FORCED_MARCH))
		{
			//don't shoot someone who's surrendering if you're a good guy
			ucmd.buttons &= ~BUTTON_ATTACK;
			ucmd.buttons &= ~BUTTON_ALT_ATTACK;
		}

		if (client->ps.weaponstate == WEAPON_IDLE)
		{
			client->ps.weaponstate = WEAPON_READY;
		}
	}
	else
	{
		if (client->ps.weaponstate == WEAPON_READY)
		{
			client->ps.weaponstate = WEAPON_IDLE;
		}
	}

	if (!(ucmd.buttons & BUTTON_ATTACK) && NPC->attackDebounceTime > level.time)
	{
		//We just shot but aren't still shooting, so hold the gun up for a while
		if (client->ps.weapon == WP_SABER)
		{
			//One-handed
			NPC_SetAnim(NPC, SETANIM_TORSO, BOTH_STAND2, SETANIM_FLAG_NORMAL);
		}
		else if (self->client->NPC_class == CLASS_SBD)
		{
			NPC_SetAnim(NPC, SETANIM_TORSO, SBD_WEAPON_OUT_STANDING, SETANIM_FLAG_NORMAL);
		}
		else if (client->ps.weapon == WP_BRYAR_PISTOL)
		{
			//Sniper pose
			if (self->client->NPC_class == CLASS_SBD)
			{
				NPC_SetAnim(NPC, SETANIM_TORSO, SBD_WEAPON_OUT_STANDING, SETANIM_FLAG_NORMAL);
			}
			else
			{
				NPC_SetAnim(NPC, SETANIM_TORSO, TORSO_WEAPONREADY3, SETANIM_FLAG_NORMAL);
			}
		}
		else if (client->ps.weapon == WP_SBD_PISTOL)
		{
			//Sniper pose
			if (self->client->NPC_class == CLASS_SBD)
			{
				NPC_SetAnim(NPC, SETANIM_TORSO, SBD_WEAPON_OUT_STANDING, SETANIM_FLAG_NORMAL);
			}
			else
			{
				NPC_SetAnim(NPC, SETANIM_TORSO, TORSO_WEAPONREADY3, SETANIM_FLAG_NORMAL);
			}
		}
	}
	else if (!NPC->enemy) //HACK!
	{
		if (NPC->s.torsoAnim == TORSO_WEAPONREADY1 || NPC->s.torsoAnim == TORSO_WEAPONREADY3)
		{
			//we look ready for action, using one of the first 2 weapon, let's rest our weapon on our shoulder
			NPC_SetAnim(NPC, SETANIM_TORSO, TORSO_WEAPONIDLE3, SETANIM_FLAG_NORMAL);
		}
	}

	NPC_CheckAttackHold();
	NPC_ApplyScriptFlags();

	//cliff and wall avoidance
	NPC_AvoidWallsAndCliffs();

	// stuck against something on the way: climb / jump onto it or go round it (g_obstacleClimb, NPC_move.cpp)
	NPC_ObstacleClimb();

	// run the bot through the server like it was a real client
	//=== Save the ucmd for the second no-think Pmove ============================
	ucmd.serverTime = level.time - 50;
	memcpy(&NPCInfo->last_ucmd, &ucmd, sizeof(usercmd_t));
	if (!NPCInfo->attackHoldTime)
	{
		NPCInfo->last_ucmd.buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_FORCE_FOCUS);
		//so we don't fire twice in one think
	}
	//============================================================================
	NPC_CheckAttackScript();
	NPC_KeepCurrentFacing();

	if (!NPC->next_roff_time || NPC->next_roff_time < level.time)
	{
		//If we were following a roff, we don't do normal pmoves.
		ClientThink(NPC->s.number, &ucmd);
	}
	else
	{
		NPC_ApplyRoff();
	}

	// end of thinking cleanup
	NPCInfo->touchedByPlayer = nullptr;
}

static void NPC_CheckInSolid()
{
	trace_t trace;
	vec3_t point;
	VectorCopy(NPC->currentOrigin, point);
	point[2] -= 0.25;

	gi.trace(&trace, NPC->currentOrigin, NPC->mins, NPC->maxs, point, NPC->s.number, NPC->clipmask,
		static_cast<EG2_Collision>(0), 0);
	if (!trace.startsolid && !trace.allsolid)
	{
		VectorCopy(NPC->currentOrigin, NPCInfo->lastClearOrigin);
	}
	else
	{
		if (VectorLengthSquared(NPCInfo->lastClearOrigin))
		{
			//			gi.Printf("%s stuck in solid at %s: fixing...\n", NPC->script_targetname, vtos(NPC->currentOrigin));
			G_SetOrigin(NPC, NPCInfo->lastClearOrigin);
			gi.linkentity(NPC);
		}
	}
}

/*
===============
NPC_Think

Main NPC AI - called once per frame
===============
*/
// SS_BLASTER (2026-10-24): an NPC on the blaster saber style shoots its left hand pistol at an enemy out of saber reach
// and in sight - a short burst through the player's own fire path (Walk + Dash: auto aim, clip, anim), then a pause
// (g_saberBlasterAIFire ms on average, shorter for higher ranks; 0 = never) - and reloads (Walk + Dash + Attack) when
// the clip is empty and nobody is in saber reach. Close up it fights with the saber as before.
static constexpr float NPC_BLASTER_SABER_REACH = 128.0f;
static constexpr int NPC_BLASTER_BURST = 300;

static void NPC_SaberBlaster(usercmd_t* cmd)
{
	const gclient_t* client = NPC->client;

	if (!g_saberBlasterAIFire || g_saberBlasterAIFire->integer <= 0 || !client || client->ps.weapon != WP_SABER
		|| client->ps.saberAnimLevel != SS_BLASTER || client->ps.saberInFlight || client->ps.saberLockTime >= level.time)
	{
		return;
	}

	gentity_t* enemy = NPC->enemy;
	const float dist = enemy && enemy->client && enemy->health > 0
		? Distance(NPC->currentOrigin, enemy->currentOrigin) : 99999.0f;

	if (client->ps.ammo[SABERBLASTER_CLIP_SLOT] >= SABERBLASTER_CLIP_SIZE)
	{
		// empty clip: reload, unless someone is close enough to cut it down meanwhile
		if (dist > NPC_BLASTER_SABER_REACH && client->ps.weaponTime <= 0)
		{
			cmd->buttons |= BUTTON_WALKING | BUTTON_DASH | BUTTON_ATTACK;
		}
		return;
	}

	if (dist <= NPC_BLASTER_SABER_REACH || dist >= 99999.0f || !NPC_ClearLOS(enemy))
	{
		return;
	}

	if (TIMER_Done(NPC, "saberBlasterBurst") && TIMER_Done(NPC, "saberBlasterNext"))
	{
		// rank: a captain waits about half as long as a civilian
		const int rank = NPCInfo->rank < RANK_CIVILIAN ? RANK_CIVILIAN : NPCInfo->rank > RANK_CAPTAIN ? RANK_CAPTAIN : NPCInfo->rank;
		const float scale = 1.25f - 0.75f * (rank - RANK_CIVILIAN) / static_cast<float>(RANK_CAPTAIN - RANK_CIVILIAN);
		const int pause = static_cast<int>(g_saberBlasterAIFire->integer * scale);

		TIMER_Set(NPC, "saberBlasterBurst", NPC_BLASTER_BURST);
		TIMER_Set(NPC, "saberBlasterNext", NPC_BLASTER_BURST + Q_irand(pause / 2, pause * 3 / 2));
	}

	if (!TIMER_Done(NPC, "saberBlasterBurst"))
	{
		cmd->buttons |= BUTTON_WALKING | BUTTON_DASH;
		cmd->buttons &= ~BUTTON_ATTACK; // attack held too would reload instead
	}
}

// g_npcThinkLod: an NPC farther than this from the player, not fighting the player and not in a script or
// cutscene, decides what to do every other game frame instead of every frame (it still moves every frame,
// with the last decision). The far ones are split between the even and the odd frames by entity number, so
// the AI of a big battle no longer lands all in the same frame. 0 = every NPC every frame, as before.
// Returns the extra delay to add to nextBStateThink.
extern bool in_camera;

static int NPC_ThinkLodDelay(const gentity_t* npc)
{
	static cvar_t* lod = nullptr;
	if (!lod)
	{
		lod = gi.cvar("g_npcThinkLod", "2048", CVAR_ARCHIVE);
	}
	const gentity_t* player = &g_entities[0];
	if (!lod || lod->value <= 0.0f || in_camera || !player->client || !npc->NPC || !npc->client
		|| npc->enemy == player || npc->NPC->behaviorState == BS_CINEMATIC
		|| npc->m_iIcarusID != IIcarusInterface::ICARUS_INVALID && IIcarusInterface::GetIcarus()->IsRunning(npc->m_iIcarusID)
		|| DistanceSquared(npc->currentOrigin, player->currentOrigin) < lod->value * lod->value)
	{
		return 0;
	}
	// next decision two game frames on; if that frame has the wrong parity for this NPC, one more
	const int frame = level.time / (FRAMETIME / 2) + 2;
	return FRAMETIME / 2 + ((frame + npc->s.number) & 1 ? FRAMETIME / 2 : 0);
}

#if	AI_TIMERS
extern int AITime;
#endif//	AI_TIMERS
void NPC_Think(gentity_t* self)
{
	PROFILE_ZONE("NPC_Think");
	vec3_t old_move_dir;

	self->nextthink = level.time + FRAMETIME / 2;

	SetNPCGlobals(self);

	memset(&ucmd, 0, sizeof ucmd);

	VectorCopy(self->client->ps.moveDir, old_move_dir);
	VectorClear(self->client->ps.moveDir);

	if (self->client->playerTeam == g_entities[0].client->playerTeam &&
		Distance(self->currentOrigin, g_entities[0].currentOrigin) <= 1024.0f)
	{
		g_entities[0].nearAllies = self->s.number;
	}

	// see if NPC ai is frozen
	if (debugNPCFreeze->integer || NPC->svFlags & SVF_ICARUS_FREEZE || self->client->ps.
		stasisTime > level.time)
	{
		NPC_UpdateAngles(qtrue, qtrue);
		ClientThink(self->s.number, &ucmd);
		VectorCopy(self->s.origin, self->s.origin2);
		return;
	}
	if (debugNPCFreeze->integer || NPC->svFlags & SVF_ICARUS_FREEZE || self->client->ps.
		stasisJediTime > level.time)
	{
		NPC_UpdateAngles(qtrue, qtrue);
		ClientThink(self->s.number, &ucmd);
		VectorCopy(self->s.origin, self->s.origin2);
		return;
	}

	// sits at a Pazaak match with the player (g_pazaak.cpp): his AI and scripts rest, he keeps facing the player
	if (G_Pazaak_IsNPCPlaying(self))
	{
		NPC_UpdateAngles(qtrue, qtrue);
		ClientThink(self->s.number, &ucmd);
		VectorCopy(self->s.origin, self->s.origin2);
		return;
	}

	if (!self || !self->NPC || !self->client)
	{
		return;
	}

	// dead NPCs have a special think, don't run scripts (for now)
	//FIXME: this breaks deathscripts
	if (self->health <= 0)
	{
		DeadThink();
		if (NPCInfo->nextBStateThink <= level.time)
		{
			if (self->m_iIcarusID != IIcarusInterface::ICARUS_INVALID && !stop_icarus)
			{
				IIcarusInterface::GetIcarus()->Update(self->m_iIcarusID);
			}
		}
		return;
	}

	// TODO! Tauntaun's (and other creature vehicles?) think, we'll need to make an exception here to allow that.

	if (self->client
		&& self->client->NPC_class == CLASS_VEHICLE
		&& self->NPC_type
		&& !self->m_pVehicle->m_pVehicleInfo->Inhabited(self->m_pVehicle))
	{
		//empty swoop logic
		if (self->owner)
		{
			//still have attached owner, check and see if can forget him (so he can use me later)
			vec3_t dir2_owner;
			VectorSubtract(self->owner->currentOrigin, self->currentOrigin, dir2_owner);

			gentity_t* old_owner = self->owner;
			self->owner = nullptr; //clear here for that SpotWouldTelefrag check...?

			if (VectorLengthSquared(dir2_owner) > 128 * 128
				|| !(self->clipmask & old_owner->clipmask)
				|| DotProduct(self->client->ps.velocity, old_owner->client->ps.velocity) < -200.0f && !
				G_BoundsOverlap(self->absmin, self->absmin, old_owner->absmin, old_owner->absmax))
			{
				//all clear, become solid to our owner now
				gi.linkentity(self);
			}
			else
			{
				//blocked, retain owner
				self->owner = old_owner;
			}
		}
	}
	if (player->client->ps.viewEntity == self->s.number)
	{
		//being controlled by player
		if (self->client)
		{
			//make the noises
			if (TIMER_Done(self, "patrolNoise") && !Q_irand(0, 20))
			{
				switch (self->client->NPC_class)
				{
				case CLASS_R2D2: // droid
					G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/r2d2/misc/r2d2talk0%d.wav", Q_irand(1, 3)));
					break;
				case CLASS_R5D2: // droid
					G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/r5d2/misc/r5talk%d.wav", Q_irand(1, 4)));
					break;
				case CLASS_PROBE: // droid
					G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/probe/misc/probetalk%d.wav", Q_irand(1, 3)));
					break;
				case CLASS_MOUSE: // droid
					G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/mouse/misc/mousego%d.wav", Q_irand(1, 3)));
					break;
				case CLASS_GONK: // droid
					G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/gonk/misc/gonktalk%d.wav", Q_irand(1, 2)));
					break;
				default:
					break;
				}
				TIMER_Set(self, "patrolNoise", Q_irand(2000, 4000));
			}
		}
		//FIXME: might want to at least make sounds or something?
		//NPC_UpdateAngles(qtrue, qtrue);
		//Which ucmd should we send?  Does it matter, since it gets overridden anyway?
		NPCInfo->last_ucmd.serverTime = level.time - 50;
		ClientThink(NPC->s.number, &ucmd);
		VectorCopy(self->s.origin, self->s.origin2);
		return;
	}

	if (NPCInfo->nextBStateThink <= level.time)
	{
#if	AI_TIMERS
		int	startTime = GetTime(0);
#endif//	AI_TIMERS
		if (NPC->s.eType != ET_PLAYER)
		{
			//Something drastic happened in our script
			return;
		}

		if (NPC->s.weapon == WP_SABER && NPC->client->ps.SaberActive())
		{
			//Jedi think faster
			NPCInfo->nextBStateThink = level.time + FRAMETIME / 2;
		}
		else
		{
			//Maybe even 200 ms?
			//NPCInfo->nextBStateThink = level.time + FRAMETIME;
			NPCInfo->nextBStateThink = level.time + FRAMETIME / 2;
		}
		NPCInfo->nextBStateThink += NPC_ThinkLodDelay(NPC);

		//nextthink is set before this so something in here can override it
		NPC_ExecuteBState(self);
		NPC_ChatterThink(self); // NPC chatter (2026-11-16)

#if	AI_TIMERS
		int addTime = GetTime(startTime);
		if (addTime > 50)
		{
			gi.Printf(S_COLOR_RED"ERROR: NPC number %d, %s %s at %s, weaponnum: %d, using %d of AI time!!!\n", NPC->s.number, NPC->NPC_type, NPC->targetname, vtos(NPC->currentOrigin), NPC->s.weapon, addTime);
		}
		AITime += addTime;
#endif//	AI_TIMERS
	}
	else
	{
		if (NPC->client
			&& NPC->client->NPC_class == CLASS_ROCKETTROOPER
			&& (NPC->client->ps.eFlags & EF_FORCE_GRIPPED || NPC->client->ps.eFlags & EF_FORCE_GRABBED)
			&& NPC->client->moveType == MT_FLYSWIM
			&& NPC->client->ps.groundEntityNum == ENTITYNUM_NONE)
		{
			//reduce velocity
			VectorScale(NPC->client->ps.velocity, 0.75f, NPC->client->ps.velocity);
		}
		VectorCopy(old_move_dir, self->client->ps.moveDir);
		//or use client->pers.lastCommand?
		NPCInfo->last_ucmd.serverTime = level.time - 50;
		if (!NPC->next_roff_time || NPC->next_roff_time < level.time)
		{
			//If we were following a roff, we don't do normal pmoves.
			//FIXME: firing angles (no aim offset) or regular angles?
			NPC_UpdateAngles(qtrue, qtrue);
			memcpy(&ucmd, &NPCInfo->last_ucmd, sizeof(usercmd_t));
			NPC_SaberBlaster(&ucmd); // SS_BLASTER (2026-10-24): the left hand pistol out of saber reach
			ClientThink(NPC->s.number, &ucmd);
		}
		else
		{
			NPC_ApplyRoff();
		}
		VectorCopy(self->s.origin, self->s.origin2);
	}
	//must update icarus *every* frame because of certain animation completions in the pmove stuff that can leave a 50ms gap between ICARUS animation commands
	if (self->m_iIcarusID != IIcarusInterface::ICARUS_INVALID && !stop_icarus)
	{
		IIcarusInterface::GetIcarus()->Update(self->m_iIcarusID);
	}
}

static void NPC_InitAI()
{
	debugNPCAI = gi.cvar("d_npcai", "0", CVAR_CHEAT);
	debugNPCFreeze = gi.cvar("d_npcfreeze", "0", CVAR_CHEAT);
	d_JediAI = gi.cvar("d_JediAI", "0", CVAR_CHEAT);
	d_noGroupAI = gi.cvar("d_noGroupAI", "0", CVAR_CHEAT);
	d_asynchronousGroupAI = gi.cvar("d_asynchronousGroupAI", "1", CVAR_CHEAT);
	d_npctalk = gi.cvar("d_npctalk", "0", CVAR_ARCHIVE);

	//0 = never (BORING)
	//1 = kyle only
	//2 = kyle and last enemy jedi
	//3 = kyle and any enemy jedi
	//4 = kyle and last enemy in a group, special kicks
	//5 = kyle and any enemy
	//6 = also when kyle takes pain or enemy jedi dodges player saber swing or does an acrobatic evasion
	// NOTE : I also create this in UI_Init()
	d_slowmodeath = gi.cvar("d_slowmodeath", "0", CVAR_ARCHIVE); //save this setting
	g_longLeapCinematic = gi.cvar("g_longLeapCinematic", "0", CVAR_ARCHIVE); // force long leap: 1 = SP's spinning slow motion camera, 0 = as MP (camera eases back)

	d_saberCombat = gi.cvar("d_saberCombat", "0", CVAR_CHEAT);

	d_slowmoaction = gi.cvar("d_slowmoaction", "1", CVAR_ARCHIVE | CVAR_SAVEGAME | CVAR_NORESTART);
	//save this setting

	d_saberInfo = gi.cvar("d_saberInfo", "0", CVAR_ARCHIVE);
	d_combatinfo = gi.cvar("d_combatinfo", "0", CVAR_ARCHIVE);
	d_blockinfo = gi.cvar("d_blockinfo", "0", CVAR_ARCHIVE);
	d_attackinfo = gi.cvar("d_attackinfo", "0", CVAR_ARCHIVE);
}

/*
==================================
void NPC_InitAnimTable( void )

  Need to initialize this table.
  If someone tried to play an anim
  before table is filled in with
  values, causes tasks that wait for
  anim completion to never finish.
  (frameLerp of 0 * numFrames of 0 = 0)
==================================
*/
static void NPC_InitAnimTable()
{
	for (auto& knownAnimFileSet : level.knownAnimFileSets)
	{
		for (auto& animation : knownAnimFileSet.animations)
		{
			animation.firstFrame = 0;
			animation.frameLerp = 100;
			//			level.knownAnimFileSets[i].animations[j].initialLerp = 100;
			animation.numFrames = 0;
		}
	}
}

extern cvar_t* com_outcast;
extern int G_ParseAnimFileSet(const char* skeletonName, const char* modelName = nullptr);

void NPC_InitGame()
{
	NPC_ChatterReset(); // NPC chatter (2026-11-16)
	G_CivilianReset(); // wandering civilians (2026-11-16, NPC_spawn)
	debugNPCName = gi.cvar("d_npc", "", 0);

	if (com_outcast && com_outcast->integer == 0) //jka version
	{
		NPC_LoadParms();
	}
	else if (com_outcast && com_outcast->integer == 1) //playing outcast
	{
		NPC_LoadParms1();
	}
	else if (com_outcast && com_outcast->integer == 2) //playing creative
	{
		NPC_LoadParms2();
	}
	else if (com_outcast && com_outcast->integer == 3) //playing yav
	{
		NPC_LoadParms3();
	}
	else if (com_outcast && com_outcast->integer == 4) //playing darkforces
	{
		NPC_LoadParms4();
	}
	else if (com_outcast && com_outcast->integer == 5) //playing kotor
	{
		NPC_LoadParms5();
	}
	else if (com_outcast && com_outcast->integer == 6) //surv version
	{
		NPC_LoadParms6();
	}
	else if (com_outcast && com_outcast->integer == 7) //nina version
	{
		NPC_LoadParms7();
	}
	else if (com_outcast && com_outcast->integer == 8) //veng version
	{
		NPC_LoadParms8();
	}
	else
	{
		NPC_LoadParms(); //default version
	}
	NPC_InitAI();
	NPC_InitAnimTable();
	G_ParseAnimFileSet("_humanoid"); //GET THIS CACHED NOW BEFORE CGAME STARTS
}

void NPC_SetAnim(gentity_t* ent, int setAnimParts, const int anim, const int setAnimFlags, const int i_blend)
{
	// FIXME : once torsoAnim and legsAnim are in the same structure for NCP and Players
	// rename PM_SETAnimFinal to PM_SetAnim and have both NCP and Players call PM_SetAnim

	if (!ent)
	{
		return;
	}

	if (ent->health > 0)
	{
		//don't lock anims if the guy is dead
		if (ent->client->ps.torsoAnimTimer
			&& PM_LockedAnim(ent->client->ps.torsoAnim)
			&& !PM_LockedAnim(anim))
		{
			//nothing can override these special anims
			setAnimParts &= ~SETANIM_TORSO;
		}

		if (ent->client->ps.legsAnimTimer
			&& PM_LockedAnim(ent->client->ps.legsAnim)
			&& !PM_LockedAnim(anim))
		{
			//nothing can override these special anims
			setAnimParts &= ~SETANIM_LEGS;
		}
	}

	if (!setAnimParts)
	{
		return;
	}

	if (ent->client->ps.stasisTime > level.time)
	{
		return;
	}

	if (ent->client->ps.stasisJediTime > level.time)
	{
		return;
	}

	if (ent->client)
	{
		//Players, NPCs
		if (setAnimFlags & SETANIM_FLAG_OVERRIDE)
		{
			if (setAnimParts & SETANIM_TORSO)
			{
				if (setAnimFlags & SETANIM_FLAG_RESTART || ent->client->ps.torsoAnim != anim)
				{
					PM_SetTorsoAnimTimer(ent, &ent->client->ps.torsoAnimTimer, 0);
				}
			}
			if (setAnimParts & SETANIM_LEGS)
			{
				if (setAnimFlags & SETANIM_FLAG_RESTART || ent->client->ps.legsAnim != anim)
				{
					PM_SetLegsAnimTimer(ent, &ent->client->ps.legsAnimTimer, 0);
				}
			}
		}

		PM_SetAnimFinal(&ent->client->ps.torsoAnim, &ent->client->ps.legsAnim, setAnimParts, anim, setAnimFlags,
			&ent->client->ps.torsoAnimTimer, &ent->client->ps.legsAnimTimer, ent, i_blend);
	}
	else
	{
		//bodies, etc.
		if (setAnimFlags & SETANIM_FLAG_OVERRIDE)
		{
			if (setAnimParts & SETANIM_TORSO)
			{
				if (setAnimFlags & SETANIM_FLAG_RESTART || ent->s.torsoAnim != anim)
				{
					PM_SetTorsoAnimTimer(ent, &ent->s.torsoAnimTimer, 0);
				}
			}
			if (setAnimParts & SETANIM_LEGS)
			{
				if (setAnimFlags & SETANIM_FLAG_RESTART || ent->s.legsAnim != anim)
				{
					PM_SetLegsAnimTimer(ent, &ent->s.legsAnimTimer, 0);
				}
			}
		}

		PM_SetAnimFinal(&ent->s.torsoAnim, &ent->s.legsAnim, setAnimParts, anim, setAnimFlags,
			&ent->s.torsoAnimTimer, &ent->s.legsAnimTimer, ent);
	}
}

qboolean NPC_IsMando(const gentity_t* self)
{
	switch (self->client->NPC_class)
	{
	case CLASS_ROCKETTROOPER:
	case CLASS_BOBAFETT:
	case CLASS_MANDO:
		// Is Jedi...
		return qtrue;
	default:
		// NOT Jedi...
		break;
	}

	return qfalse;
}

qboolean NPC_IsOversized(const gentity_t* self)
{
	switch (self->client->NPC_class)
	{
	case CLASS_WOOKIE:
	case CLASS_SBD:
	case CLASS_ASSASSIN_DROID:
	case CLASS_HAZARD_TROOPER:
	case CLASS_ROCKETTROOPER:
	case CLASS_SABER_DROID:
	case CLASS_VADER:
		// Is Jedi...
		return qtrue;
	default:
		// NOT Jedi...
		break;
	}

	return qfalse;
}

#else // MP game

static void NPC_ExecuteBState(const gentity_t* self) //, int msec )
{
	if (!self)
	{
		return;
	}

	if (!NPCS.NPC || !NPCS.NPCInfo || !NPCS.client)
	{
		// debug log with entity id so you can locate caller/sequence
#ifdef _DEBUG
		Com_Printf("NPC_ExecuteBState: NPCS not initialized for entity %d\n", self->s.number);
#endif
		return;
	}

	if (self->enemy && self->enemy->client && self->enemy->client->ps.duelInProgress)
	{
		G_ClearEnemy(NPCS.NPC);
	}

	NPC_HandleAIFlags();

	//FIXME: these next three bits could be a function call, some sort of setup/cleanup func
	//Lookmode must be reset every think cycle
	if (NPCS.NPC->delayScriptTime && NPCS.NPC->delayScriptTime <= level.time)
	{
		G_ActivateBehavior(NPCS.NPC, BSET_DELAYED);
		NPCS.NPC->delayScriptTime = 0;
	}

	//Clear this and let b_state set it itself, so it automatically handles changing bStates... but we need a set b_state wrapper func
	NPCS.NPCInfo->combatMove = qfalse;

	//Execute our b_state
	const bState_t b_state = G_CurrentBState(NPCS.NPCInfo);

	//Pick the proper bstate for us and run it
	NPC_RunBehavior(self->client->playerTeam, b_state);

	if (NPCS.NPC->enemy)
	{
		if (!NPCS.NPC->enemy->inuse)
		{
			//just in case b_state doesn't catch this
			G_ClearEnemy(NPCS.NPC);
		}
	}

	if (NPCS.NPC->client->ps.saberLockTime && NPCS.NPC->client->ps.saberLockEnemy != ENTITYNUM_NONE)
	{
		NPC_SetLookTarget(NPCS.NPC, NPCS.NPC->client->ps.saberLockEnemy, level.time + 1000);
	}
	else if (!NPC_CheckLookTarget(NPCS.NPC))
	{
		if (NPCS.NPC->enemy)
		{
			NPC_SetLookTarget(NPCS.NPC, NPCS.NPC->enemy->s.number, 0);
		}
	}

	if (NPCS.NPC->enemy)
	{
		if (NPCS.NPC->enemy->flags & FL_DONT_SHOOT)
		{
			NPCS.ucmd.buttons &= ~BUTTON_ATTACK;
			NPCS.ucmd.buttons &= ~BUTTON_ALT_ATTACK;
		}
		else if (NPCS.NPC->client->playerTeam != NPCTEAM_ENEMY //not an enemy
			&& (NPCS.NPC->client->playerTeam != NPCTEAM_FREE
				|| NPCS.NPC->client->NPC_class == CLASS_TUSKEN && Q_irand(0, 4))
			//not a rampaging creature or I'm a tusken and I feel generous (temporarily)
			&& NPCS.NPC->enemy->NPC
			&& (NPCS.NPC->enemy->NPC->surrenderTime > level.time || NPCS.NPC->enemy->NPC->scriptFlags &
				SCF_FORCED_MARCH))
		{
			//don't shoot someone who's surrendering if you're a good guy
			NPCS.ucmd.buttons &= ~BUTTON_ATTACK;
			NPCS.ucmd.buttons &= ~BUTTON_ALT_ATTACK;
		}

		if (NPCS.client->ps.weaponstate == WEAPON_IDLE)
		{
			NPCS.client->ps.weaponstate = WEAPON_READY;
		}
	}
	else
	{
		if (NPCS.client->ps.weaponstate == WEAPON_READY)
		{
			NPCS.client->ps.weaponstate = WEAPON_IDLE;
		}
	}

	if (!(NPCS.ucmd.buttons & BUTTON_ATTACK) && NPCS.NPC->attackDebounceTime > level.time)
	{
		//We just shot but aren't still shooting, so hold the gun up for a while
		if (NPCS.client->ps.weapon == WP_SABER)
		{
			//One-handed
			NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, BOTH_STAND2, SETANIM_FLAG_NORMAL);
		}
		else if (NPCS.client->ps.weapon == WP_BRYAR_PISTOL)
		{
			//Sniper pose
			if (self->client->NPC_class == CLASS_SBD)
			{
				NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, SBD_WEAPON_OUT_STANDING, SETANIM_FLAG_NORMAL);
			}
			else
			{
				NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, TORSO_WEAPONREADY3, SETANIM_FLAG_NORMAL);
			}
		}
	}
	else if (!NPCS.NPC->enemy) //HACK!
	{
		if (NPCS.NPC->s.torsoAnim == TORSO_WEAPONREADY1 || NPCS.NPC->s.torsoAnim == TORSO_WEAPONREADY3)
		{
			//we look ready for action, using one of the first 2 weapon, let's rest our weapon on our shoulder
			NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, TORSO_WEAPONIDLE3, SETANIM_FLAG_NORMAL);
		}
	}

	NPC_CheckAttackHold();
	NPC_ApplyScriptFlags();

	//cliff and wall avoidance
	NPC_AvoidWallsAndCliffs();

	// stuck against something on the way: climb / jump onto it or go round it (g_obstacleClimb, NPC_move.c)
	NPC_ObstacleClimb();

	// run the bot through the server like it was a real client
	//=== Save the ucmd for the second no-think Pmove ============================
	NPCS.ucmd.serverTime = level.time - 50;
	memcpy(&NPCS.NPCInfo->last_ucmd, &NPCS.ucmd, sizeof(usercmd_t));
	if (!NPCS.NPCInfo->attackHoldTime)
	{
		NPCS.NPCInfo->last_ucmd.buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK);
		//so we don't fire twice in one think
	}
	//============================================================================
	NPC_CheckAttackScript();
	NPC_KeepCurrentFacing();

	if (!NPCS.NPC->next_roff_time || NPCS.NPC->next_roff_time < level.time)
	{
		//If we were following a roff, we don't do normal pmoves.
		ClientThink(NPCS.NPC->s.number, &NPCS.ucmd);
	}
	else
	{
		NPC_ApplyRoff();
	}

	// end of thinking cleanup
	NPCS.NPCInfo->touchedByPlayer = NULL;
}

static void NPC_CheckInSolid(void)
{
	trace_t trace;
	vec3_t point;
	VectorCopy(NPCS.NPC->r.currentOrigin, point);
	point[2] -= 0.25;

	trap->Trace(&trace, NPCS.NPC->r.currentOrigin, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, point,
		NPCS.NPC->s.number,
		NPCS.NPC->clipmask, qfalse, 0, 0);
	if (!trace.startsolid && !trace.allsolid)
	{
		VectorCopy(NPCS.NPC->r.currentOrigin, NPCS.NPCInfo->lastClearOrigin);
	}
	else
	{
		if (VectorLengthSquared(NPCS.NPCInfo->lastClearOrigin))
		{
			G_SetOrigin(NPCS.NPC, NPCS.NPCInfo->lastClearOrigin);
			trap->LinkEntity((sharedEntity_t*)NPCS.NPC);
		}
	}
}

static void G_DroidSounds(gentity_t* self)
{
	if (self->client)
	{
		//make the noises
		if (TIMER_Done(self, "patrolNoise") && !Q_irand(0, 20))
		{
			switch (self->client->NPC_class)
			{
			case CLASS_R2D2: // droid
				G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/r2d2/misc/r2d2talk0%d.wav", Q_irand(1, 3)));
				break;
			case CLASS_R5D2: // droid
				G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/r5d2/misc/r5talk%d.wav", Q_irand(1, 4)));
				break;
			case CLASS_PROBE: // droid
				G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/probe/misc/probetalk%d.wav", Q_irand(1, 3)));
				break;
			case CLASS_MOUSE: // droid
				G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/mouse/misc/mousego%d.wav", Q_irand(1, 3)));
				break;
			case CLASS_GONK: // droid
				G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/gonk/misc/gonktalk%d.wav", Q_irand(1, 2)));
				break;
			default:
				break;
			}
			TIMER_Set(self, "patrolNoise", Q_irand(2000, 4000));
		}
	}
}

/*
===============
NPC_Think

Main NPC AI - called once per frame
===============
*/
#if	AI_TIMERS
extern int AITime;
#endif//	AI_TIMERS
extern void G_ScriptHeldAnimThink(gentity_t* ent);

void NPC_Think(gentity_t* self)
{
	vec3_t oldMoveDir;
	const int i = 0;
	static qboolean spreadRegistered = qfalse;

	self->nextthink = level.time + FRAMETIME / 2;

	// g_npcThinkSpread: NPCs think every 50 ms, which is every other server frame (sv_fps 40). Split them
	// between the two frames by entity number, so NPCs spawned together don't all think in the same frame.
	// 0 = as before.
	if (!spreadRegistered)
	{
		trap->Cvar_Register(NULL, "g_npcThinkSpread", "1", CVAR_ARCHIVE);
		spreadRegistered = qtrue;
	}
	const int frameMsec = level.time - level.previousTime;	// one server frame
	if (trap->Cvar_VariableIntegerValue("g_npcThinkSpread") && frameMsec > 0 && frameMsec < FRAMETIME / 2)
	{
		const int frame = self->nextthink / frameMsec;
		if ((frame + self->s.number) & 1)
		{
			self->nextthink += frameMsec;
		}
	}

	SetNPCGlobals(self);

	if (self->NPC && self->NPC->teamownerIsTeam && self->client)
	{
		self->s.teamowner = self->client->playerTeam; // (the client's crosshair and radar colour: NPC_spawn.c)
	}

	memset(&NPCS.ucmd, 0, sizeof NPCS.ucmd);

	VectorCopy(self->client->ps.moveDir, oldMoveDir);

	if (self->s.NPC_class != CLASS_VEHICLE)
	{
		//YOU ARE BREAKING MY PREDICTION. Bad clear.
		VectorClear(self->client->ps.moveDir);
	}

	if (d_npcfreeze.value || NPCS.NPC->r.svFlags & SVF_ICARUS_FREEZE)
	{
		//our AI is frozen.
		NPC_UpdateAngles(qtrue, qtrue);
		ClientThink(self->s.number, &NPCS.ucmd);
		VectorCopy(self->s.origin, self->s.origin2);
		return;
	}

	if (!self || !self->NPC || !self->client)
	{
		return;
	}

	// dead NPCs have a special think, don't run scripts (for now)
	if (self->health <= 0)
	{
		DeadThink();
		if (NPCS.NPCInfo->nextBStateThink <= level.time)
		{
			trap->ICARUS_MaintainTaskManager(self->s.number);
		}
		return;
	}

	if (self->client
		&& self->client->NPC_class == CLASS_VEHICLE
		&& self->NPC_type
		&& !self->m_pVehicle->m_pVehicleInfo->Inhabited(self->m_pVehicle))
	{
		//empty swoop logic
		if (self->s.owner != ENTITYNUM_NONE)
		{
			//still have attached owner, check and see if can forget him (so he can use me later)
			vec3_t dir2owner;
			const gentity_t* oldOwner = &g_entities[self->s.owner];

			VectorSubtract(g_entities[self->s.owner].r.currentOrigin, self->r.currentOrigin, dir2owner);

			self->s.owner = ENTITYNUM_NONE; //clear here for that SpotWouldTelefrag check...?

			if (VectorLengthSquared(dir2owner) > 128 * 128
				|| !(self->clipmask & oldOwner->clipmask)
				|| DotProduct(self->client->ps.velocity, oldOwner->client->ps.velocity) < -200.0f && !
				G_BoundsOverlap(self->r.absmin, self->r.absmin, oldOwner->r.absmin, oldOwner->r.absmax))
			{
				//all clear, become solid to our owner now
				trap->LinkEntity((sharedEntity_t*)self);
			}
			else
			{
				//blocked, retain owner
				self->s.owner = oldOwner->s.number;
			}
		}
	}
	const gentity_t* player = &g_entities[i];

	if (player->client->ps.viewEntity == self->s.number)
	{
		//being controlled by player
		if (self->client)
		{
			//make the noises
			if (TIMER_Done(self, "patrolNoise") && !Q_irand(0, 20))
			{
				switch (self->client->NPC_class)
				{
				case CLASS_R2D2: // droid
					G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/r2d2/misc/r2d2talk0%d.wav", Q_irand(1, 3)));
					break;
				case CLASS_R5D2: // droid
					G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/r5d2/misc/r5talk%d.wav", Q_irand(1, 4)));
					break;
				case CLASS_PROBE: // droid
					G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/probe/misc/probetalk%d.wav", Q_irand(1, 3)));
					break;
				case CLASS_MOUSE: // droid
					G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/mouse/misc/mousego%d.wav", Q_irand(1, 3)));
					break;
				case CLASS_GONK: // droid
					G_SoundOnEnt(self, CHAN_AUTO, va("sound/chars/gonk/misc/gonktalk%d.wav", Q_irand(1, 2)));
					break;
				default:;
				}
				TIMER_Set(self, "patrolNoise", Q_irand(2000, 4000));
			}
		}
		NPCS.NPCInfo->last_ucmd.serverTime = level.time - 50;
		ClientThink(NPCS.NPC->s.number, &NPCS.ucmd);
		VectorCopy(self->s.origin, self->s.origin2);
		return;
	}

	if (NPCS.NPCInfo->nextBStateThink <= level.time)
	{
#if	AI_TIMERS
		int	startTime = GetTime(0);
#endif//	AI_TIMERS
		if (NPCS.NPC->s.eType != ET_NPC)
		{
			//Something drastic happened in our script
			return;
		}

		if (NPCS.NPC->s.weapon == WP_SABER && g_npcspskill.integer >= 2 && NPCS.NPCInfo->rank > RANK_LT_JG)
		{
			//Jedi think faster on hard difficulty, except low-rank (reborn)
			NPCS.NPCInfo->nextBStateThink = level.time + FRAMETIME / 2;
		}
		else
		{
			//Maybe even 200 ms?
			NPCS.NPCInfo->nextBStateThink = level.time + FRAMETIME;
		}

		//nextthink is set before this so something in here can override it
		NPC_ExecuteBState(self);
		NPC_ChatterThink(self); // NPC chatter (2026-11-16)

#if	AI_TIMERS
		int addTime = GetTime(startTime);
		if (addTime > 50)
		{
			gi.Printf(S_COLOR_RED"ERROR: NPC number %d, %s %s at %s, weaponnum: %d, using %d of AI time!!!\n", NPC->s.number, NPC->NPC_type, NPC->targetname, vtos(NPC->currentOrigin), NPC->s.weapon, addTime);
		}
		AITime += addTime;
#endif//	AI_TIMERS
	}
	else
	{
		if (NPCS.NPC->client
			&& NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER
			&& NPCS.NPC->client->ps.fd.forceGripBeingGripped > level.time
			&& NPCS.NPC->client->ps.eFlags2 & EF2_FLYING
			&& NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE)
		{
			//reduce velocity
			VectorScale(NPCS.NPC->client->ps.velocity, 0.75f, NPCS.NPC->client->ps.velocity);
		}
		VectorCopy(oldMoveDir, self->client->ps.moveDir);
		//or use client->pers.lastCommand?
		NPCS.NPCInfo->last_ucmd.serverTime = level.time - 50;
		if (!NPCS.NPC->next_roff_time || NPCS.NPC->next_roff_time < level.time)
		{
			//If we were following a roff, we don't do normal pmoves.
			NPC_UpdateAngles(qtrue, qtrue);
			memcpy(&NPCS.ucmd, &NPCS.NPCInfo->last_ucmd, sizeof(usercmd_t));
			ClientThink(NPCS.NPC->s.number, &NPCS.ucmd);
		}
		else
		{
			NPC_ApplyRoff();
		}
		VectorCopy(self->s.origin, self->s.origin2);
	}
	G_ScriptHeldAnimThink(self); // (a script's held anim stays, as in SP: g_ICARUScb.c)
	//must update icarus *every* frame because of certain animation completions in the pmove stuff that can leave a 50ms gap between ICARUS animation commands
	trap->ICARUS_MaintainTaskManager(self->s.number);
}

void NPC_InitGame(void)
{
	NPC_ChatterReset(); // NPC chatter (2026-11-16)
	G_CivilianReset(); // wandering civilians (2026-11-16, NPC_spawn)
	NPC_LoadParms();
}

void NPC_SetAnim(gentity_t* ent, const int setAnimParts, const int anim, const int setAnimFlags)
{
	G_SetAnim(ent, NULL, setAnimParts, anim, setAnimFlags, 0);
}

#endif
