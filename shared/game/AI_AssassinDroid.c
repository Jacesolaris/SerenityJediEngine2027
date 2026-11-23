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

#include "game/g_npcshared.h"

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

#define ASSASSIN_SHIELD_SIZE 75
#define TURN_ON 0x00000000
#define TURN_OFF 0x00000100

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME
static bool BubbleShield_IsOn(void)
{
	return NPCS.NPC->flags & FL_SHIELDED;
}
#else // MP keeps its own version of this function
static qboolean BubbleShield_IsOn(const gentity_t* self)
{
	return self->flags & FL_SHIELDED;
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME
void BubbleShield_TurnOn(void)
{
	if (!BubbleShield_IsOn())
	{
		NPCS.NPC->flags |= FL_SHIELDED;
		NPCS.NPC->client->ps.powerups[PW_GALAK_SHIELD] = Q3_INFINITE;
		SHR_SetSurfaceOnOff(NPCS.NPC, "force_shield", TURN_ON);
	}
}
#else // MP keeps its own version of this function
static void BubbleShield_TurnOn(gentity_t* self)
{
	if (!BubbleShield_IsOn(self))
	{
		self->flags |= FL_SHIELDED;
		NPCS.NPC->client->ps.powerups[PW_GALAK_SHIELD] = Q3_INFINITE;
		NPC_SetSurfaceOnOff(self, "force_shield", TURN_ON);
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME
static void BubbleShield_TurnOff(void)
{
	if (BubbleShield_IsOn())
	{
		NPCS.NPC->flags &= ~FL_SHIELDED;
		NPCS.NPC->client->ps.powerups[PW_GALAK_SHIELD] = 0;
		SHR_SetSurfaceOnOff(NPCS.NPC, "force_shield", TURN_OFF);
	}
}
#else // MP keeps its own version of this function
void BubbleShield_TurnOff(gentity_t* self)
{
	if (BubbleShield_IsOn(self))
	{
		self->flags &= ~FL_SHIELDED;
		NPCS.NPC->client->ps.powerups[PW_GALAK_SHIELD] = 0;
		NPC_SetSurfaceOnOff(self, "force_shield", TURN_OFF);
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
// Push A Particular Ent
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME
static void BubbleShield_PushEnt(gentity_t* pushed, vec3_t smack_dir)
{
	G_Damage(pushed, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->SHR_currentOrigin, (SHR_SPSKILL + 1) * Q_irand(5, 10),
		DAMAGE_NO_KNOCKBACK, MOD_ELECTROCUTE);
	G_Throw(pushed, smack_dir, 10);

	// Make Em Electric
	//------------------
	pushed->s.powerups |= 1 << PW_SHOCKED;
	if (pushed->client)
	{
		pushed->client->ps.powerups[PW_SHOCKED] = level.time + 1000;
	}
}
#else // MP keeps its own version of this function
static void BubbleShield_PushEnt(gentity_t* pushed, vec3_t smack_dir)
{
	G_Damage(pushed, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->r.currentOrigin,
		(g_npcspskill.integer + 1) * Q_irand(5, 10), DAMAGE_NO_KNOCKBACK, MOD_ELECTROCUTE);
	G_Throw(pushed, smack_dir, 10);

	// Make Em Electric
	//------------------
	if (pushed->client)
	{
		pushed->client->ps.electrifyTime = level.time + 1000;
	}
}
#endif

#ifdef SP_GAME // SP only: the MP game has no version of this
static void deka_bubble_shield_push_ent(gentity_t* pushed, vec3_t smack_dir)
{
	G_Damage(pushed, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->SHR_currentOrigin, Q_irand(10, 20), DAMAGE_EXTRA_KNOCKBACK, MOD_ELECTROCUTE);
	G_Throw(pushed, smack_dir, 50);

	// Make Em Electric
	//------------------
	pushed->s.powerups |= 1 << PW_SHOCKED;
	if (pushed->client)
	{
		pushed->client->ps.powerups[PW_SHOCKED] = level.time + 3000;
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
// Go Through All The Ents Within The Radius Of The Shield And Push Them
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME
static void BubbleShield_PushRadiusEnts(void)
{
	gentity_t* radius_ents[128];
	const float radius = ASSASSIN_SHIELD_SIZE;
	vec3_t mins{}, maxs{};

	for (int i = 0; i < 3; i++)
	{
		mins[i] = NPCS.NPC->SHR_currentOrigin[i] - radius;
		maxs[i] = NPCS.NPC->SHR_currentOrigin[i] + radius;
	}

	const int num_ents = gi.EntitiesInBox(mins, maxs, radius_ents, 128);
	for (int ent_index = 0; ent_index < num_ents; ent_index++)
	{
		vec3_t smack_dir;
		// Only Clients
		//--------------
		if (!radius_ents[ent_index] || !radius_ents[ent_index]->client)
		{
			continue;
		}

		// Don't Push Away Other Assassin Droids
		//---------------------------------------
		if (radius_ents[ent_index]->client->NPC_class == NPCS.NPC->client->NPC_class)
		{
			continue;
		}

		// Should Have Already Pushed The Enemy If He Touched Us
		//-------------------------------------------------------
		if (NPCS.NPC->enemy && NPCS.NPCInfo->touchedByPlayer == NPCS.NPC->enemy && radius_ents[ent_index] == NPCS.NPC->enemy)
		{
			continue;
		}

		// Do The Vector Distance Test
		//-----------------------------
		VectorSubtract(radius_ents[ent_index]->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin, smack_dir);
		const float smackDist = VectorNormalize(smack_dir);
		if (smackDist < radius)
		{
			BubbleShield_PushEnt(radius_ents[ent_index], smack_dir);
		}
	}
}
#else // MP keeps its own version of this function
static void BubbleShield_PushRadiusEnts(void)
{
	// static: too big for the stack, and BG_Alloc is never freed: asked for on every call, this ran the pool dry
	static int entity_list[MAX_GENTITIES];

	const float radius = ASSASSIN_SHIELD_SIZE;
	vec3_t mins = { 0, 0, 0 }, maxs = { 0, 0, 0 };

	// Compute bounding box around NPC
	for (int i = 0; i < 3; i++)
	{
		mins[i] = NPCS.NPC->r.currentOrigin[i] - radius;
		maxs[i] = NPCS.NPC->r.currentOrigin[i] + radius;
	}

	// Query entities inside bounding box
	const int num_ents = trap->EntitiesInBox(mins, maxs, entity_list, MAX_GENTITIES);

	for (int i = 0; i < num_ents; i++)
	{
		const int entNum = entity_list[i];

		// Validate index
		if (entNum < 0 || entNum >= MAX_GENTITIES)
		{
			continue;
		}

		gentity_t* radius_ent = &g_entities[entNum];

		// Only clients
		if (!radius_ent || !radius_ent->client)
		{
			continue;
		}

		// Do not push other Assassin Droids
		if (radius_ent->client->NPC_class == NPCS.NPC->client->NPC_class)
		{
			continue;
		}

		// Enemy already pushed by touch logic
		if (NPCS.NPC->enemy &&
			NPCS.NPCInfo->touchedByPlayer == NPCS.NPC->enemy &&
			radius_ent == NPCS.NPC->enemy)
		{
			continue;
		}

		// Compute direction from NPC to entity
		vec3_t smack_dir;
		VectorSubtract(radius_ent->r.currentOrigin, NPCS.NPC->r.currentOrigin, smack_dir);

		const float smackDist = VectorNormalize(smack_dir);

		// Inside radius → push entity
		if (smackDist < radius)
		{
			BubbleShield_PushEnt(radius_ent, smack_dir);
		}
	}
}
#endif

#ifdef SP_GAME // SP only: the MP game has no version of this
static void deka_bubble_shield_push_radius_ents(void)
{
	gentity_t* radius_ents[128];
	const float radius = ASSASSIN_SHIELD_SIZE;
	vec3_t mins{}, maxs{};

	for (int i = 0; i < 3; i++)
	{
		mins[i] = NPCS.NPC->SHR_currentOrigin[i] - radius;
		maxs[i] = NPCS.NPC->SHR_currentOrigin[i] + radius;
	}

	const int num_ents = gi.EntitiesInBox(mins, maxs, radius_ents, 128);
	for (int ent_index = 0; ent_index < num_ents; ent_index++)
	{
		vec3_t smack_dir;
		// Only Clients
		//--------------
		if (!radius_ents[ent_index] || !radius_ents[ent_index]->client)
		{
			continue;
		}

		// Don't Push Away Other Assassin Droids
		//---------------------------------------
		if (radius_ents[ent_index]->client->NPC_class == NPCS.NPC->client->NPC_class)
		{
			continue;
		}

		// Should Have Already Pushed The Enemy If He Touched Us
		//-------------------------------------------------------
		if (NPCS.NPC->enemy && NPCS.NPCInfo->touchedByPlayer == NPCS.NPC->enemy && radius_ents[ent_index] == NPCS.NPC->enemy)
		{
			continue;
		}

		// Do The Vector Distance Test
		//-----------------------------
		VectorSubtract(radius_ents[ent_index]->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin, smack_dir);
		const float smack_dist = VectorNormalize(smack_dir);
		if (smack_dist < radius)
		{
			deka_bubble_shield_push_ent(radius_ents[ent_index], smack_dir);
		}
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME
void bubble_shield_update(void)
{
	// Shields Go When You Die
	//-------------------------
	if (NPCS.NPC->health <= 0)
	{
		if (BubbleShield_IsOn())
		{
			BubbleShield_TurnOff();
		}
		return;
	}

	// Recharge Shields
	//------------------
	NPCS.NPC->client->ps.stats[STAT_ARMOR] += 1;
	if (NPCS.NPC->client->ps.stats[STAT_ARMOR] > 250)
	{
		NPCS.NPC->client->ps.stats[STAT_ARMOR] = 250;
	}

	// If We Have Enough Armor And Are Not Shooting Right Now, Kick The Shield On
	//----------------------------------------------------------------------------
	if (NPCS.NPC->client->ps.stats[STAT_ARMOR] > 100 && TIMER_Done(NPCS.NPC, "ShieldsDown"))
	{
		// Check On Timers To Raise And Lower Shields
		//--------------------------------------------
		if (level.time - NPCS.NPCInfo->enemyLastSeenTime < 1000 && TIMER_Done(NPCS.NPC, "ShieldsUp"))
		{
			TIMER_Set(NPCS.NPC, "ShieldsDown", 2000); // Drop Shields
			TIMER_Set(NPCS.NPC, "ShieldsUp", Q_irand(4000, 5000)); // Then Bring Them Back Up For At Least 3 sec
		}

		BubbleShield_TurnOn();
		if (BubbleShield_IsOn())
		{
			// Update Our Shader Value
			//-------------------------
			NPCS.NPC->client->renderInfo.customRGBA[0] =
				NPCS.NPC->client->renderInfo.customRGBA[1] =
				NPCS.NPC->client->renderInfo.customRGBA[2] =
				NPCS.NPC->client->renderInfo.customRGBA[3] = NPCS.NPC->client->ps.stats[STAT_ARMOR] - 100;

			// If Touched By An Enemy, ALWAYS Shove Them
			//-------------------------------------------
			if (NPCS.NPC->enemy && NPCS.NPCInfo->touchedByPlayer == NPCS.NPC->enemy)
			{
				vec3_t dir;
				VectorSubtract(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin, dir);
				VectorNormalize(dir);
				BubbleShield_PushEnt(NPCS.NPC->enemy, dir);
			}

			// Push Anybody Else Near
			//------------------------
			BubbleShield_PushRadiusEnts();
		}
	}

	// Shields Gone
	//--------------
	else
	{
		BubbleShield_TurnOff();
	}
}
#else // MP keeps its own version of this function
void bubble_shield_update(void)
{
	// Shields Go When You Die
	//-------------------------
	if (NPCS.NPC->health <= 0)
	{
		BubbleShield_TurnOff(NPCS.NPC);
		return;
	}

	// Recharge Shields
	//------------------
	NPCS.NPC->client->ps.stats[STAT_ARMOR] += 1;
	if (NPCS.NPC->client->ps.stats[STAT_ARMOR] > 250)
	{
		NPCS.NPC->client->ps.stats[STAT_ARMOR] = 250;
	}

	// If We Have Enough Armor And Are Not Shooting Right Now, Kick The Shield On
	//----------------------------------------------------------------------------
	if (NPCS.NPC->client->ps.stats[STAT_ARMOR] > 100 && TIMER_Done(NPCS.NPC, "ShieldsDown"))
	{
		// Check On Timers To Raise And Lower Shields
		//--------------------------------------------
		if (level.time - NPCS.NPCInfo->enemyLastSeenTime < 1000 && TIMER_Done(NPCS.NPC, "ShieldsUp"))
		{
			TIMER_Set(NPCS.NPC, "ShieldsDown", 2000); // Drop Shields
			TIMER_Set(NPCS.NPC, "ShieldsUp", Q_irand(4000, 5000)); // Then Bring Them Back Up For At Least 3 sec
		}

		BubbleShield_TurnOn(NPCS.NPC);
		if (BubbleShield_IsOn(NPCS.NPC))
		{
			// Update Our Shader Value
			//-------------------------
			NPCS.NPC->s.customRGBA[0] =
				NPCS.NPC->s.customRGBA[1] =
				NPCS.NPC->s.customRGBA[2] =
				NPCS.NPC->s.customRGBA[3] = NPCS.NPC->client->ps.stats[STAT_ARMOR] - 100;

			// If Touched By An Enemy, ALWAYS Shove Them
			//-------------------------------------------
			if (NPCS.NPC->enemy && NPCS.NPCInfo->touchedByPlayer == NPCS.NPC->enemy)
			{
				vec3_t dir;
				VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, dir);
				VectorNormalize(dir);
				BubbleShield_PushEnt(NPCS.NPC->enemy, dir);
			}

			// Push Anybody Else Near
			//------------------------
			BubbleShield_PushRadiusEnts();
		}
	}
	else
	{
		BubbleShield_TurnOff(NPCS.NPC);
	}
}
#endif

#ifdef SP_GAME // SP only: the MP game has no version of this
void deka_bubble_shield_update(void)
{
	// Shields Go When You Die
	//-------------------------
	if (NPCS.NPC->health <= 0)
	{
		if (BubbleShield_IsOn())
		{
			BubbleShield_TurnOff();
		}
		return;
	}

	if (NPCS.NPC->client->ps.powerups[PW_STUNNED])
	{
		if (BubbleShield_IsOn())
		{
			BubbleShield_TurnOff();
		}
		return;
	}

	// Recharge Shields
	//------------------
	NPCS.NPC->client->ps.stats[STAT_ARMOR] += 1;

	if (NPCS.NPC->client->ps.stats[STAT_ARMOR] > 250)
	{
		NPCS.NPC->client->ps.stats[STAT_ARMOR] = 250;
	}

	// If We Have Enough Armor And Are Not Shooting Right Now, Kick The Shield On
	//----------------------------------------------------------------------------
	if (NPCS.NPC->client->ps.stats[STAT_ARMOR] > 100 && TIMER_Done(NPCS.NPC, "ShieldsDown"))
	{
		// Check On Timers To Raise And Lower Shields
		//--------------------------------------------
		if (level.time - NPCS.NPCInfo->enemyLastSeenTime < 1000 && TIMER_Done(NPCS.NPC, "ShieldsUp"))
		{
			TIMER_Set(NPCS.NPC, "ShieldsDown", 2000); // Drop Shields
			TIMER_Set(NPCS.NPC, "ShieldsUp", Q_irand(8000, 16000)); // Then Bring Them Back Up For At Least 3 sec
		}

		BubbleShield_TurnOn();

		if (BubbleShield_IsOn() && NPCS.NPC->client->ps.stats[STAT_ARMOR] > 50)
		{
			// Update Our Shader Value
			//-------------------------
			NPCS.NPC->client->renderInfo.customRGBA[0] =
				NPCS.NPC->client->renderInfo.customRGBA[1] =
				NPCS.NPC->client->renderInfo.customRGBA[2] =
				NPCS.NPC->client->renderInfo.customRGBA[3] = NPCS.NPC->client->ps.stats[STAT_ARMOR] - 100;

			// If Touched By An Enemy, ALWAYS Shove Them
			//-------------------------------------------
			if (NPCS.NPC->enemy && NPCS.NPCInfo->touchedByPlayer == NPCS.NPC->enemy)
			{
				vec3_t dir;
				VectorSubtract(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin, dir);
				VectorNormalize(dir);
				deka_bubble_shield_push_ent(NPCS.NPC->enemy, dir);
			}

			// Push Anybody Else Near
			//------------------------
			deka_bubble_shield_push_radius_ents();
		}
	}

	// Shields Gone
	//--------------
	else
	{
		BubbleShield_TurnOff();
	}
}
#endif