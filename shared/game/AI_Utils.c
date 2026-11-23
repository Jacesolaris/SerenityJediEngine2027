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

#ifndef SP_GAME // MP-only helpers used by the MP versions below
float DistancetoClosestPlayer(vec3_t position, const int enemy_team)
{
	//returns the distance to the closest human player on this team
	float bestdist = 9999;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		const gentity_t* player = &g_entities[i];
		if (!player || !player->client || !player->inuse
			|| player->client->pers.connected != CON_CONNECTED //not connected
			|| player->client->sess.sessionTeam == TEAM_SPECTATOR) //spectator
		{
			//player not active
			continue;
		}

		if (enemy_team != -1 && player->client->playerTeam != enemy_team)
		{
			//this player isn't on the team I hate.
			continue;
		}

		if (player->health <= 0 || player->s.eFlags & EF_DEAD)
		{
			//player is dead, dont use
			continue;
		}

		const float dist = Distance(player->client->ps.origin, position);
		if (dist < bestdist)
		{
			bestdist = dist;
		}
	}

	return bestdist;
}

gentity_t* FindClosestPlayer(vec3_t position, const int enemy_team)
{
	//Return the closest live player to position that is on this team.
	float bestdist = 9999;
	gentity_t* closestplayer = NULL;
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		gentity_t* player = &g_entities[i];
		if (!player || !player->client || !player->inuse
			|| player->client->pers.connected != CON_CONNECTED //not connected
			|| player->client->sess.sessionTeam == TEAM_SPECTATOR) //spectator
		{
			//player not active
			continue;
		}

		if (enemy_team != -1 && player->client->playerTeam != enemy_team)
		{
			//this player isn't on the team I hate.
			continue;
		}

		if (player->health <= 0 || player->s.eFlags & EF_DEAD)
		{
			//player is dead, dont use
			continue;
		}

		const float dist = Distance(player->client->ps.origin, position);
		if (dist < bestdist)
		{
			closestplayer = player;
			bestdist = dist;
		}
	}

	return closestplayer;
}

qboolean InPlayersFOV(vec3_t position, const int enemy_team, const int h_fov,
	const int v_fov, const qboolean check_clear_los)
{
	//checks to see if this position is the FOV of a player on this team
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		gentity_t* player = &g_entities[i];
		if (!player || !player->client || !player->inuse
			|| player->client->pers.connected != CON_CONNECTED //not connected
			|| player->client->sess.sessionTeam == TEAM_SPECTATOR) //spectator
		{
			//player not active
			continue;
		}

		if (enemy_team != -1 && player->client->playerTeam != enemy_team)
		{
			//this player isn't on the team I hate.
			continue;
		}

		if (player->health <= 0 || player->s.eFlags & EF_DEAD)
		{
			//player is dead, dont use
			continue;
		}

		if (!InFOV2(position, player, h_fov, v_fov))
		{
			//not in player's fov
			continue;
		}

		if (check_clear_los && !G_ClearLOS5(player, position))
		{
			//check for clearLOS if we want to.
			continue;
		}

		return qtrue;
	}

	return qfalse;
}
#endif

#ifndef SP_GAME // file-level declarations of the MP versions below
extern void G_TestLine(vec3_t start, vec3_t end, int color, int time);
#endif

#ifndef SP_GAME // MP-only helpers used by the MP versions below

#endif

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

// These utilities are meant for strictly non-player, non-team NPCs.
// These functions are in their own file because they are only intended
// for use with NPCs who's logic has been overriden from the original
// AI code, and who's code resides in files with the AI_ prefix.


#define MAX_RADIUS_ENTS 128
#define DEFAULT_RADIUS 45

#ifdef SP_GAME // MP: vmCvar_t of the same name
extern cvar_t* d_noGroupAI;
#endif
qboolean AI_ValidateGroupMember(const AIGroupInfo_t* group, const gentity_t* member);

int GetTime(int lastTime)
{
	return level.time - lastTime;
}

/*
-------------------------
AI_GetGroupSize
-------------------------
*/

#ifdef SP_GAME
int AI_GetGroupSize(vec3_t origin, const int radius, const team_t playerTeam, const gentity_t* avoid)
{
	gentity_t* radius_ents[MAX_RADIUS_ENTS];
	vec3_t mins{}, maxs{};
	int realCount = 0;

	//Setup the bbox to search in
	for (int i = 0; i < 3; i++)
	{
		mins[i] = origin[i] - radius;
		maxs[i] = origin[i] + radius;
	}

	//Get the number of entities in a given space
	const int num_ents = gi.EntitiesInBox(mins, maxs, radius_ents, MAX_RADIUS_ENTS);

	//Cull this list
	for (int j = 0; j < num_ents; j++)
	{
		//Validate clients
		if (radius_ents[j]->client == NULL)
			continue;

		//Skip the requested avoid ent if present
		if (avoid != NULL && radius_ents[j] == avoid)
			continue;

		//Must be on the same team
		if (radius_ents[j]->client->playerTeam != playerTeam)
			continue;

		//Must be alive
		if (radius_ents[j]->health <= 0)
			continue;

		realCount++;
	}

	return realCount;
}
#else // MP keeps its own version of this function
int AI_GetGroupSize(vec3_t origin, const int radius, const team_t playerTeam, const gentity_t* avoid)
{
	int radius_ents[MAX_RADIUS_ENTS];
	vec3_t mins, maxs;
	int real_count = 0;

	//Setup the bbox to search in
	for (int i = 0; i < 3; i++)
	{
		mins[i] = origin[i] - radius;
		maxs[i] = origin[i] + radius;
	}

	//Get the number of entities in a given space
	const int num_ents = trap->EntitiesInBox(mins, maxs, radius_ents, MAX_RADIUS_ENTS);

	//Cull this list
	for (int j = 0; j < num_ents; j++)
	{
		const gentity_t* check = &g_entities[radius_ents[j]];

		//Validate clients
		if (check->client == NULL)
			continue;

		//Skip the requested avoid ent if present
		if (avoid != NULL && check == avoid)
			continue;

		//Must be on the same team
		if (check->client->playerTeam != (npcteam_t)playerTeam)
			continue;

		//Must be alive
		if (check->health <= 0)
			continue;

		real_count++;
	}

	return real_count;
}
#endif

//Overload

#ifdef SP_GAME
int AI_GetGroupSize(gentity_t* ent, const int radius)
#else // MP name of the same function
int AI_GetGroupSize2(gentity_t* ent, const int radius)
#endif
{
	if (ent == NULL || ent->client == NULL)
		return -1;

	return AI_GetGroupSize(ent->SHR_currentOrigin, radius, ent->client->playerTeam, ent);
}

static void AI_SetClosestBuddy(AIGroupInfo_t* group)
{
	for (int i = 0; i < group->numGroup; i++)
	{
		group->member[i].closestBuddy = ENTITYNUM_NONE;

		int bestDist = Q3_INFINITE;
		for (int j = 0; j < group->numGroup; j++)
		{
			if (i == j)
				continue; // JA:R fix: never pick yourself as closest buddy
			const int dist = DistanceSquared(g_entities[group->member[i].number].SHR_currentOrigin,
				g_entities[group->member[j].number].SHR_currentOrigin);
			if (dist < bestDist)
			{
				bestDist = dist;
				group->member[i].closestBuddy = group->member[j].number;
			}
		}
	}
}

#ifdef SP_GAME
static void AI_SortGroupByPathCostToEnemy(AIGroupInfo_t* group)
{
	if (group == NULL)
	{
		Com_Printf("AI_SortGroupByPathCostToEnemy: NULL group\n");
		return;
	}

	qboolean sort = qfalse;

	// ---------------------------------------------------------
	// Determine enemy waypoint
	// ---------------------------------------------------------
	if (group->enemy != NULL)
	{
		group->enemyWP = NAV::GetNearestNode(group->enemy);
	}
	else
	{
		group->enemyWP = WAYPOINT_NONE;
	}

	// ---------------------------------------------------------
	// Compute each member's path cost
	// ---------------------------------------------------------
	for (int i = 0; i < group->numGroup; i++)
	{
		if (group->enemyWP == WAYPOINT_NONE)
		{
			group->member[i].waypoint = WAYPOINT_NONE;
			group->member[i].pathCostToEnemy = Q3_INFINITE;
		}
		else
		{
			// JA:R fix: the member's own nav node, not the enemy's (every cost was the same)
			group->member[i].waypoint = NAV::GetNearestNode(&g_entities[group->member[i].number]);

			if (group->member[i].waypoint != WAYPOINT_NONE)
			{
				group->member[i].pathCostToEnemy =
					NAV::EstimateCostToGoal(group->member[i].waypoint, group->enemyWP);

				sort = qtrue;
			}
			else
			{
				group->member[i].pathCostToEnemy = Q3_INFINITE;
			}
		}
	}

	// ---------------------------------------------------------
	// Sorting
	// ---------------------------------------------------------
	if (sort == qtrue)
	{
		AIGroupMember_t bestMembers[MAX_GROUP_MEMBERS]{};

		// Initialize
		for (int j = 0; j < group->numGroup; j++)
		{
			bestMembers[j].number = ENTITYNUM_NONE;
			bestMembers[j].pathCostToEnemy = Q3_INFINITE;
			bestMembers[j].waypoint = WAYPOINT_NONE;
		}

		// Insert-sort by pathCostToEnemy
		for (int i = 0; i < group->numGroup; i++)
		{
			for (int j = 0; j < group->numGroup; j++)
			{
				if (bestMembers[j].number != ENTITYNUM_NONE)
				{
					if (group->member[i].pathCostToEnemy < bestMembers[j].pathCostToEnemy)
					{
						// Shift down safely: k starts at last valid index
						for (int k = group->numGroup - 1; k > j; k--)
						{
							memcpy(&bestMembers[k], &bestMembers[k - 1], sizeof(bestMembers[k]));
						}

						memcpy(&bestMembers[j], &group->member[i], sizeof(bestMembers[j]));
						break;
					}
				}
				else
				{
					// First empty slot
					memcpy(&bestMembers[j], &group->member[i], sizeof(bestMembers[j]));
					break;
				}
			}
		}

		// Copy sorted list back
		for (int i = 0; i < group->numGroup; i++)
		{
			memcpy(&group->member[i], &bestMembers[i], sizeof(group->member[i]));
		}
	}
}
#else // MP keeps its own version of this function
static void AI_SortGroupByPathCostToEnemy(AIGroupInfo_t* group)
{
	AIGroupMember_t best_members[MAX_GROUP_MEMBERS];
	int i = 0;
	qboolean sort = qfalse;

	if (group->enemy != NULL)
	{
		//FIXME: just use enemy->waypoint?
		group->enemyWP = NAV_FindClosestWaypointForEnt(group->enemy, WAYPOINT_NONE);
	}
	else
	{
		group->enemyWP = WAYPOINT_NONE;
	}

	for (i = 0; i < group->numGroup; i++)
	{
		if (group->enemyWP == WAYPOINT_NONE)
		{
			//FIXME: just use member->waypoint?
			group->member[i].waypoint = WAYPOINT_NONE;
			group->member[i].pathCostToEnemy = Q3_INFINITE;
		}
		else
		{
			//FIXME: just use member->waypoint?
			// JA:R fix: the member's own waypoint, not the enemy's (every cost was the same)
			group->member[i].waypoint = NAV_FindClosestWaypointForEnt(&g_entities[group->member[i].number], WAYPOINT_NONE);
			if (group->member[i].waypoint != WAYPOINT_NONE)
			{
				group->member[i].pathCostToEnemy = trap->Nav_GetPathCost(group->member[i].waypoint, group->enemyWP);
				//at least one of us has a path, so do sorting
				sort = qtrue;
			}
			else
			{
				group->member[i].pathCostToEnemy = Q3_INFINITE;
			}
		}
	}
	//Now sort
	if (sort)
	{
		int j = 0;
		//initialize bestMembers data
		for (j = 0; j < group->numGroup; j++)
		{
			best_members[j].number = ENTITYNUM_NONE;
		}

		for (i = 0; i < group->numGroup; i++)
		{
			for (j = 0; j < group->numGroup; j++)
			{
				if (best_members[j].number != ENTITYNUM_NONE)
				{
					//slot occupied
					if (group->member[i].pathCostToEnemy < best_members[j].pathCostToEnemy)
					{
						//this guy has a shorter path than the one currenly in this spot, bump him and put myself in here
						// JA:R fix: this loop never ran (k = numGroup; k < j), so members were overwritten and lost
						for (int k = group->numGroup - 1; k > j; k--)
						{
							memcpy(&best_members[k], &best_members[k - 1], sizeof best_members[k]);
						}
						memcpy(&best_members[j], &group->member[i], sizeof best_members[j]);
						break;
					}
				}
				else
				{
					//slot unoccupied, reached end of list, throw self in here
					memcpy(&best_members[j], &group->member[i], sizeof best_members[j]);
					break;
				}
			}
		}

		//Okay, now bestMembers is a sorted list, just copy it into group->members
		for (i = 0; i < group->numGroup; i++)
		{
			memcpy(&group->member[i], &best_members[i], sizeof group->member[i]);
		}
	}
}
#endif

static qboolean AI_FindSelfInPreviousGroup(const gentity_t* self)
{
	//go through other groups made this frame and see if any of those contain me already
	for (int i = 0; i < MAX_FRAME_GROUPS; i++)
	{
		if (level.groups[i].numGroup) //&& level.groups[i].enemy != NULL )
		{
			//check this one
			for (int j = 0; j < level.groups[i].numGroup; j++)
			{
				if (level.groups[i].member[j].number == self->s.number)
				{
					self->NPC->group = &level.groups[i];
					return qtrue;
				}
			}
		}
	}
	return qfalse;
}

static void AI_InsertGroupMember(AIGroupInfo_t* group, gentity_t* member)
{
	int i = 0;
	//okay, you know what?  Check this damn group and make sure we're not already in here!
	for (i = 0; i < group->numGroup; i++)
	{
		if (group->member[i].number == member->s.number)
		{
			//already in here
			break;
		}
	}
	if (i < group->numGroup)
	{
		//found him in group already
	}
	else
	{
		//add him in
		group->member[group->numGroup++].number = member->s.number;
		group->numState[member->NPC->squadState]++;
	}
	if (!group->commander || member->NPC->rank > group->commander->NPC->rank)
	{
		//keep track of highest rank
		group->commander = member;
	}
	member->NPC->group = group;
}

static qboolean AI_TryJoinPreviousGroup(gentity_t* self)
{
	//go through other groups made this frame and see if any of those have the same enemy as me... if so, add me in!
	for (int i = 0; i < MAX_FRAME_GROUPS; i++)
	{
		if (level.groups[i].numGroup
			&& level.groups[i].numGroup < MAX_GROUP_MEMBERS - 1
			//&& level.groups[i].enemy != NULL
			&& level.groups[i].enemy == self->enemy)
		{
			//has members, not full and has my enemy
			if (AI_ValidateGroupMember(&level.groups[i], self))
			{
				//I am a valid member for this group
				AI_InsertGroupMember(&level.groups[i], self);
				return qtrue;
			}
		}
	}
	return qfalse;
}

#ifdef SP_GAME
static qboolean AI_GetNextEmptyGroup(gentity_t* self)
{
	if (AI_FindSelfInPreviousGroup(self))
	{
		//already in one, no need to make a new one
		return qfalse;
	}

	if (AI_TryJoinPreviousGroup(self))
	{
		//try to just put us in one that already exists
		return qfalse;
	}

	//okay, make a whole new one, then
	for (auto& group : level.groups)
	{
		if (!group.numGroup)
		{
			//make a new one
			self->NPC->group = &group;
			return qtrue;
		}
	}

	//if ( i >= MAX_FRAME_GROUPS )
	{
		//WTF?  Out of groups!
		self->NPC->group = NULL;
		return qfalse;
	}
}
#else // MP keeps its own version of this function
static qboolean AI_GetNextEmptyGroup(gentity_t* self)
{
	if (AI_FindSelfInPreviousGroup(self))
	{
		//already in one, no need to make a new one
		return qfalse;
	}

	if (AI_TryJoinPreviousGroup(self))
	{
		//try to just put us in one that already exists
		return qfalse;
	}

	//okay, make a whole new one, then
	for (int i = 0; i < MAX_FRAME_GROUPS; i++)
	{
		if (!level.groups[i].numGroup)
		{
			//make a new one
			self->NPC->group = &level.groups[i];
			return qtrue;
		}
	}

	//if ( i >= MAX_FRAME_GROUPS )
	{
		//WTF?  Out of groups!
		self->NPC->group = NULL;
		return qfalse;
	}
}
#endif

#ifdef SP_GAME
static qboolean AI_ValidateNoEnemyGroupMember(const AIGroupInfo_t* group, const gentity_t* member)
{
	if (!group)
	{
		return qfalse;
	}
	vec3_t center;
	if (group->commander)
	{
		VectorCopy(group->commander->SHR_currentOrigin, center);
	}
	else
	{
		//hmm, just pick the first member
		if (group->member[0].number < 0 || group->member[0].number >= ENTITYNUM_WORLD)
		{
			return qfalse;
		}
		VectorCopy(g_entities[group->member[0].number].SHR_currentOrigin, center);
	}
	//FIXME: maybe it should be based on the center of the mass of the group, not the commander?
	if (DistanceSquared(center, member->SHR_currentOrigin) > 147456/*384*384*/)
	{
		return qfalse;
	}
	if (!gi.inPVS(member->SHR_currentOrigin, center))
	{
		//not within PVS of the group enemy
		return qfalse;
	}
	return qtrue;
}
#else // MP keeps its own version of this function
static qboolean AI_ValidateNoEnemyGroupMember(const AIGroupInfo_t* group, const gentity_t* member)
{
	vec3_t center;

	if (!group)
	{
		return qfalse;
	}
	if (group->commander)
	{
		VectorCopy(group->commander->r.currentOrigin, center);
	}
	else
	{
		//hmm, just pick the first member
		if (group->member[0].number < 0 || group->member[0].number >= ENTITYNUM_WORLD)
		{
			return qfalse;
		}
		VectorCopy(g_entities[group->member[0].number].r.currentOrigin, center);
	}
	//FIXME: maybe it should be based on the center of the mass of the group, not the commander?
	if (DistanceSquared(center, member->r.currentOrigin) > 147456/*384*384*/)
	{
		return qfalse;
	}
	if (!trap->InPVS(member->r.currentOrigin, center))
	{
		//not within PVS of the group enemy
		return qfalse;
	}
	return qtrue;
}
#endif

#ifdef SP_GAME
qboolean AI_ValidateGroupMember(const AIGroupInfo_t* group, const gentity_t* member)
{
	//Validate ents
	if (member == NULL)
		return qfalse;

	//Validate clients
	if (member->client == NULL)
		return qfalse;

	//Validate NPCs
	if (member->NPC == NULL)
		return qfalse;

	//must be aware
	if (member->NPC->confusionTime > level.time)
		return qfalse;

	if (SHR_InsanityTime(member->NPC) > level.time)
		return qfalse;

	//must be allowed to join groups
	if (member->NPC->scriptFlags & SCF_NO_GROUPS)
		return qfalse;

	//Must not be in another group
	if (member->NPC->group != NULL && member->NPC->group != group)
	{
		//FIXME: if that group's enemy is mine, why not absorb that group into mine?
		return qfalse;
	}

	//Must be alive
	if (member->health <= 0)
		return qfalse;

	//can't be in an emplaced gun
	if (member->s.eFlags & EF_LOCKED_TO_WEAPON)
		return qfalse;

	if (member->s.eFlags & EF_HELD_BY_RANCOR)
		return qfalse;

	if (member->s.eFlags & EF_HELD_BY_SAND_CREATURE)
		return qfalse;

	if (member->s.eFlags & EF_HELD_BY_WAMPA)
		return qfalse;

	//Must be on the same team
	if (member->client->playerTeam != group->team)
		return qfalse;

	if (member->client->ps.weapon == WP_SABER || //!= self->s.weapon )
		member->client->ps.weapon == WP_THERMAL ||
		member->client->ps.weapon == WP_DISRUPTOR ||
		member->client->ps.weapon == WP_EMPLACED_GUN ||
		member->client->ps.weapon == WP_BOT_LASER || // Probe droid	- Laser blast
		member->client->ps.weapon == WP_MELEE ||
		member->client->ps.weapon == WP_TURRET || // turret guns
		member->client->ps.weapon == WP_ATST_MAIN ||
		member->client->ps.weapon == WP_ATST_SIDE ||
		member->client->ps.weapon == WP_TIE_FIGHTER)
	{
		//not really a squad-type guy
		return qfalse;
	}

	if (member->client->NPC_class == CLASS_ATST ||
		member->client->NPC_class == CLASS_PROBE ||
		member->client->NPC_class == CLASS_SEEKER ||
		member->client->NPC_class == CLASS_REMOTE ||
		member->client->NPC_class == CLASS_SENTRY ||
		member->client->NPC_class == CLASS_INTERROGATOR ||
		member->client->NPC_class == CLASS_MINEMONSTER ||
		member->client->NPC_class == CLASS_HOWLER ||
		member->client->NPC_class == CLASS_RANCOR ||
		member->client->NPC_class == CLASS_MARK1 ||
		member->client->NPC_class == CLASS_MARK2)
	{
		//these kinds of enemies don't actually use this group AI
		return qfalse;
	}

	//should have same enemy
	if (member->enemy != group->enemy)
	{
		if (member->enemy != NULL)
		{
			//he's fighting someone else, leave him out
			return qfalse;
		}
		if (!gi.inPVS(member->SHR_currentOrigin, group->enemy->SHR_currentOrigin))
		{
			//not within PVS of the group enemy
			return qfalse;
		}
	}
	else if (group->enemy == NULL)
	{
		//if the group is a patrol group, only take those within the room and radius
		if (!AI_ValidateNoEnemyGroupMember(group, member))
		{
			return qfalse;
		}
	}
	//must be actually in combat mode
	if (!TIMER_Done(member, "interrogating"))
		return qfalse;
	//FIXME: need to have a route to enemy and/or clear shot?
	return qtrue;
}
#else // MP keeps its own version of this function
qboolean AI_ValidateGroupMember(const AIGroupInfo_t* group, const gentity_t* member)
{
	//Validate ents
	if (member == NULL)
		return qfalse;

	//Validate clients
	if (member->client == NULL)
		return qfalse;

	//Validate NPCs
	if (member->NPC == NULL)
		return qfalse;

	//must be aware
	if (member->NPC->confusionTime > level.time)
		return qfalse;

	//must be allowed to join groups
	if (member->NPC->scriptFlags & SCF_NO_GROUPS)
		return qfalse;

	//Must not be in another group
	if (member->NPC->group != NULL && member->NPC->group != group)
	{
		//FIXME: if that group's enemy is mine, why not absorb that group into mine?
		return qfalse;
	}

	//Must be alive
	if (member->health <= 0)
		return qfalse;

	//can't be in an emplaced gun
	//	if( member->s.eFlags & EF_LOCKED_TO_WEAPON )
	//		return qfalse;
	//rwwFIXMEFIXME: support this flag

	//Must be on the same team
	if (member->client->playerTeam != (npcteam_t)group->team)
		return qfalse;

	if (member->client->ps.weapon == WP_SABER || //!= self->s.weapon )
		member->client->ps.weapon == WP_THERMAL ||
		member->client->ps.weapon == WP_DISRUPTOR ||
		member->client->ps.weapon == WP_EMPLACED_GUN ||
		//		member->client->ps.weapon == WP_BOT_LASER ||		// Probe droid	- Laser blast
		member->client->ps.weapon == WP_STUN_BATON ||
		member->client->ps.weapon == WP_TURRET /*||			// turret guns
		member->client->ps.weapon == WP_ATST_MAIN ||
		member->client->ps.weapon == WP_ATST_SIDE ||
		member->client->ps.weapon == WP_TIE_FIGHTER*/)
	{
		//not really a squad-type guy
		return qfalse;
	}

	if (member->client->NPC_class == CLASS_ATST ||
		member->client->NPC_class == CLASS_PROBE ||
		member->client->NPC_class == CLASS_SEEKER ||
		member->client->NPC_class == CLASS_REMOTE ||
		member->client->NPC_class == CLASS_SENTRY ||
		member->client->NPC_class == CLASS_INTERROGATOR ||
		member->client->NPC_class == CLASS_MINEMONSTER ||
		member->client->NPC_class == CLASS_HOWLER ||
		member->client->NPC_class == CLASS_MARK1 ||
		member->client->NPC_class == CLASS_MARK2)
	{
		//these kinds of enemies don't actually use this group AI
		return qfalse;
	}

	//should have same enemy
	if (member->enemy != group->enemy)
	{
		if (member->enemy != NULL)
		{
			//he's fighting someone else, leave him out
			return qfalse;
		}
		if (!trap->InPVS(member->r.currentOrigin, group->enemy->r.currentOrigin))
		{
			//not within PVS of the group enemy
			return qfalse;
		}
	}
	else if (group->enemy == NULL)
	{
		//if the group is a patrol group, only take those within the room and radius
		if (!AI_ValidateNoEnemyGroupMember(group, member))
		{
			return qfalse;
		}
	}
	//must be actually in combat mode
	if (!TIMER_Done(member, "interrogating"))
		return qfalse;
	//FIXME: need to have a route to enemy and/or clear shot?
	return qtrue;
}
#endif

/*
-------------------------
AI_GetGroup
-------------------------
*/
#ifdef SP_GAME
void AI_GetGroup(gentity_t* self)
{
	//int	waiters[MAX_WAITERS];

	if (!self || !self->NPC)
	{
		return;
	}

	if (SHR_CVAR(d_noGroupAI).integer)
	{
		self->NPC->group = NULL;
		return;
	}

	if (!self->client)
	{
		self->NPC->group = NULL;
		return;
	}

	if (self->NPC->scriptFlags & SCF_NO_GROUPS)
	{
		self->NPC->group = NULL;
		return;
	}

	if (self->enemy && (!self->enemy->client || level.time - self->NPC->enemyLastSeenTime > 7000))
	{
		self->NPC->group = NULL;
		return;
	}

	if (!AI_GetNextEmptyGroup(self))
	{
		//either no more groups left or we're already in a group built earlier
		return;
	}

	//create a new one
	memset(self->NPC->group, 0, sizeof(AIGroupInfo_t));

	self->NPC->group->enemy = self->enemy;
	self->NPC->group->team = self->client->playerTeam;
	self->NPC->group->processed = qfalse;
	self->NPC->group->commander = self;
	self->NPC->group->memberValidateTime = level.time + 2000;
	self->NPC->group->activeMemberNum = 0;

	if (self->NPC->group->enemy)
	{
		self->NPC->group->lastSeenEnemyTime = level.time;
		self->NPC->group->lastClearShotTime = level.time;
		VectorCopy(self->NPC->group->enemy->SHR_currentOrigin, self->NPC->group->enemyLastSeenPos);
	}

	//	for ( i = 0, member = &SHR_PLAYER0; i < globals.num_entities ; i++, member++)
	for (int i = 0; i < globals.num_entities; i++)
	{
		if (!PInUse(i))
			continue;
		gentity_t* member = &g_entities[i];

		if (!AI_ValidateGroupMember(self->NPC->group, member))
		{
			//FIXME: keep track of those who aren't angry yet and see if we should wake them after we assemble the core group
			continue;
		}

		//store it
		AI_InsertGroupMember(self->NPC->group, member);

		if (self->NPC->group->numGroup >= MAX_GROUP_MEMBERS - 1)
		{
			//full
			break;
		}
	}

	/*
	//now go through waiters and see if any should join the group
	//NOTE:  Some should hang back and probably not attack, so we can ambush
	//NOTE: only do this if calling for reinforcements?
	for ( i = 0; i < numWaiters; i++ )
	{
		waiter = &g_entities[waiters[i]];

		for ( j = 0; j < self->NPC->group->numGroup; j++ )
		{
			member = &g_entities[self->NPC->group->member[j];

			if ( gi.inPVS( waiter->currentOrigin, member->currentOrigin ) )
			{//this waiter is within PVS of a current member
			}
		}
	}
	*/

	if (self->NPC->group->numGroup <= 0)
	{
		//none in group
		self->NPC->group = NULL;
		return;
	}

	AI_SortGroupByPathCostToEnemy(self->NPC->group);
	AI_SetClosestBuddy(self->NPC->group);
}
#else // MP keeps its own version of this function
void AI_GetGroup(gentity_t* self)
{
	//int	waiters[MAX_WAITERS];

	if (!self || !self->NPC)
	{
		return;
	}

	if (d_noGroupAI.integer)
	{
		self->NPC->group = NULL;
		return;
	}

	if (!self->client)
	{
		self->NPC->group = NULL;
		return;
	}

	if (self->NPC->scriptFlags & SCF_NO_GROUPS)
	{
		self->NPC->group = NULL;
		return;
	}

	if (self->enemy && (!self->enemy->client || level.time - self->NPC->enemyLastSeenTime > 7000))
	{
		self->NPC->group = NULL;
		return;
	}

	if (!AI_GetNextEmptyGroup(self))
	{
		//either no more groups left or we're already in a group built earlier
		return;
	}

	//create a new one
	memset(self->NPC->group, 0, sizeof(AIGroupInfo_t));

	self->NPC->group->enemy = self->enemy;
	self->NPC->group->team = (team_t)self->client->playerTeam;
	self->NPC->group->processed = qfalse;
	self->NPC->group->commander = self;
	self->NPC->group->memberValidateTime = level.time + 2000;
	self->NPC->group->activeMemberNum = 0;

	if (self->NPC->group->enemy)
	{
		self->NPC->group->lastSeenEnemyTime = level.time;
		self->NPC->group->lastClearShotTime = level.time;
		VectorCopy(self->NPC->group->enemy->r.currentOrigin, self->NPC->group->enemyLastSeenPos);
	}

	//	for ( i = 0, member = &g_entities[0]; i < globals.num_entities ; i++, member++)
	for (int i = 0; i < level.num_entities; i++)
	{
		gentity_t* member = &g_entities[i];

		if (!member->inuse)
		{
			continue;
		}

		if (!AI_ValidateGroupMember(self->NPC->group, member))
		{
			//FIXME: keep track of those who aren't angry yet and see if we should wake them after we assemble the core group
			continue;
		}

		//store it
		AI_InsertGroupMember(self->NPC->group, member);

		if (self->NPC->group->numGroup >= MAX_GROUP_MEMBERS - 1)
		{
			//full
			break;
		}
	}

	/*
	//now go through waiters and see if any should join the group
	//NOTE:  Some should hang back and probably not attack, so we can ambush
	//NOTE: only do this if calling for reinforcements?
	for ( i = 0; i < numWaiters; i++ )
	{
		waiter = &g_entities[waiters[i]];

		for ( j = 0; j < self->NPC->group->numGroup; j++ )
		{
			member = &g_entities[self->NPC->group->member[j];

			if ( trap->InPVS( waiter->r.currentOrigin, member->r.currentOrigin ) )
			{//this waiter is within PVS of a current member
			}
		}
	}
	*/

	if (self->NPC->group->numGroup <= 0)
	{
		//none in group
		self->NPC->group = NULL;
		return;
	}

	AI_SortGroupByPathCostToEnemy(self->NPC->group);
	AI_SetClosestBuddy(self->NPC->group);
}
#endif

static void AI_SetNewGroupCommander(AIGroupInfo_t* group)
{
	group->commander = NULL;
	for (int i = 0; i < group->numGroup; i++)
	{
		gentity_t* member = &g_entities[group->member[i].number];

		if (!group->commander || member && member->NPC && group->commander->NPC && member->NPC->rank > group->commander->NPC->rank)
		{
			//keep track of highest rank
			group->commander = member;
		}
	}
}

static void AI_DeleteGroupMember(AIGroupInfo_t* group, const int memberNum)
{
	//added additional sanity checks to fix some crashing problems.
	if (memberNum >= MAX_GROUP_MEMBERS || memberNum >= group->numGroup)
	{
		//invalid group member number
		return;
	}

	if (group->commander && group->commander->s.number == group->member[memberNum].number)
	{
		group->commander = NULL;
	}
	if (g_entities[group->member[memberNum].number].NPC)
	{
		g_entities[group->member[memberNum].number].NPC->group = NULL;
	}
	for (int i = memberNum; i < group->numGroup - 1 && i < MAX_GROUP_MEMBERS - 1; i++)
	{
		memcpy(&group->member[i], &group->member[i + 1], sizeof group->member[i]);
	}
	if (memberNum < group->activeMemberNum)
	{
		group->activeMemberNum--;
		if (group->activeMemberNum < 0)
		{
			group->activeMemberNum = 0;
		}
	}
	group->numGroup--;
	if (group->numGroup < 0)
	{
		group->numGroup = 0;
	}
	AI_SetNewGroupCommander(group);
}

void AI_DeleteSelfFromGroup(const gentity_t* self)
{
	//FIXME: if killed, keep track of how many in group killed?  To affect morale?
	for (int i = 0; i < self->NPC->group->numGroup; i++)
	{
		if (self->NPC->group->member[i].number == self->s.number)
		{
			AI_DeleteGroupMember(self->NPC->group, i);
			return;
		}
	}
}

extern void ST_AggressionAdjust(const gentity_t* self, int change);
extern void ST_MarkToCover(const gentity_t* self);
extern void ST_StartFlee(gentity_t* self, gentity_t* enemy, vec3_t danger_point, int danger_level, int min_time,
	int max_time);

// g_squadCasualtyReact (2026-11-22, AI audit item 11; archived, 1 = on): squadmates used to ignore a grunt's death
// (only an officer's death moved them). Now mates within 384 units who can see the body take cover, take up the
// squad's enemy if they had none, and the first of them shouts "take cover". 0 = the old behaviour.
static int AI_CasualtyReactOn(void)
{
#ifdef SP_GAME
	static cvar_t* cv = NULL;
	if (!cv)
	{
		cv = gi.cvar("g_squadCasualtyReact", "1", CVAR_ARCHIVE);
	}
	return cv ? cv->integer : 0;
#else
	static vmCvar_t cv;
	static qboolean registered = qfalse;
	if (!registered)
	{
		trap->Cvar_Register(&cv, "g_squadCasualtyReact", "1", CVAR_ARCHIVE);
		registered = qtrue;
	}
	else
	{
		trap->Cvar_Update(&cv);
	}
	return cv.integer;
#endif
}

#ifdef SP_GAME
extern void G_AddVoiceEvent(const gentity_t* self, int event, int speak_debounce_time);
#endif
static void AI_GroupCasualtyReact(const gentity_t* self, AIGroupInfo_t* group)
{
	qboolean shouted = qfalse;

	if (!AI_CasualtyReactOn())
	{
		return;
	}
	for (int i = 0; i < group->numGroup; i++)
	{
		gentity_t* member = &g_entities[group->member[i].number];

		if (member == self || !member->inuse || !member->NPC || !member->client || member->health <= 0)
		{
			continue;
		}
		if (DistanceSquared(member->SHR_currentOrigin, self->SHR_currentOrigin) > 384.0f * 384.0f
			|| !SHR_ClearLOSEntEnd(member, self->SHR_currentOrigin))
		{
			continue;
		}
		ST_MarkToCover(member);
		if (!member->enemy && group->enemy)
		{
			G_SetEnemy(member, group->enemy);
		}
		if (!shouted)
		{
			G_AddVoiceEvent(member, Q_irand(EV_COVER1, EV_COVER5), 3000);
			shouted = qtrue;
		}
	}
}

void AI_GroupMemberKilled(const gentity_t* self)
{
	AIGroupInfo_t* group = self && self->NPC ? self->NPC->group : NULL; // (read self->NPC before its NULL check)
	gentity_t* member = NULL;
	qboolean noflee = qfalse;

	if (!group)
	{
		//what group?
		return;
	}
	if (!self || !self->NPC || self->NPC->rank < RANK_ENSIGN)
	{
		//I'm not an officer: the mates who see it still react (g_squadCasualtyReact)
		if (self && self->NPC)
		{
			AI_GroupCasualtyReact(self, group);
		}
		return;
	}
	//temporarily drop group morale for a few seconds
	group->moraleAdjust -= self->NPC->rank;
	//go through and drop aggression on my teammates (more cover, worse aim)
	for (int i = 0; i < group->numGroup; i++)
	{
		member = &g_entities[group->member[i].number];
		if (member == self)
		{
			continue;
		}
		if (member->NPC->rank > RANK_ENSIGN)
		{
			//officers do not panic
			noflee = qtrue;
		}
		else
		{
			ST_AggressionAdjust(member, -1);
			member->NPC->currentAim -= Q_irand(0, 10);
		}
	}
	//okay, if I'm the group commander, make everyone else flee
	if (group->commander != self)
	{
		//I'm not the commander... hmm, should maybe a couple flee... maybe those near me?
		return;
	}
	//now see if there is another of sufficient rank to keep them from fleeing
	if (!noflee)
	{
		self->NPC->group->speechDebounceTime = 0;
		for (int i = 0; i < group->numGroup; i++)
		{
			member = &g_entities[group->member[i].number];
			if (member == self)
			{
				continue;
			}
			if (member->NPC->rank < RANK_ENSIGN)
			{
				//grunt
				if (group->enemy && DistanceSquared(member->SHR_currentOrigin, group->enemy->SHR_currentOrigin) < 65536)
					//256*256
				{
					//those close to enemy run away!
					ST_StartFlee(member, group->enemy, member->SHR_currentOrigin, AEL_DANGER_GREAT, 3000, 5000);
				}
				else if (DistanceSquared(member->SHR_currentOrigin, self->SHR_currentOrigin) < 65536)
				{
					//those close to me run away!
					ST_StartFlee(member, group->enemy, member->SHR_currentOrigin, AEL_DANGER_GREAT, 3000, 5000);
				}
				else
				{
					//else, maybe just a random chance
					if (Q_irand(0, self->NPC->rank) > member->NPC->rank)
					{
						//lower rank they are, higher rank I am, more likely they are to flee
						ST_StartFlee(member, group->enemy, member->SHR_currentOrigin, AEL_DANGER_GREAT, 3000, 5000);
					}
					else
					{
						ST_MarkToCover(member);
					}
				}
				member->NPC->currentAim -= Q_irand(1, 15);
			}
			member->NPC->currentAim -= Q_irand(1, 15);
		}
	}
}

void AI_GroupUpdateEnemyLastSeen(AIGroupInfo_t* group, vec3_t spot)
{
	if (!group)
	{
		return;
	}
	group->lastSeenEnemyTime = level.time;
	VectorCopy(spot, group->enemyLastSeenPos);
}

void AI_GroupUpdateClearShotTime(AIGroupInfo_t* group)
{
	if (!group)
	{
		return;
	}
	group->lastClearShotTime = level.time;
}

void AI_GroupUpdateSquadstates(AIGroupInfo_t* group, const gentity_t* member, const int newSquadState)
{
	if (!group)
	{
		member->NPC->squadState = newSquadState;
		return;
	}

	for (int i = 0; i < group->numGroup; i++)
	{
		if (group->member[i].number == member->s.number)
		{
			group->numState[member->NPC->squadState]--;
			member->NPC->squadState = newSquadState;
			group->numState[member->NPC->squadState]++;
			return;
		}
	}
}

#ifdef SP_GAME
qboolean AI_RefreshGroup(AIGroupInfo_t* group)
{
	gentity_t* member = NULL;
	int i = 0; //, j;

	//see if we should merge with another group
	for (i = 0; i < MAX_FRAME_GROUPS; i++)
	{
		if (&level.groups[i] == group)
		{
			break;
		}
		// JA:R fix: never merge squads of different teams, or into an empty slot
		if (level.groups[i].enemy == group->enemy && level.groups[i].team == group->team && level.groups[i].numGroup > 0)
		{
			//2 groups with same enemy
			if (level.groups[i].numGroup + group->numGroup < MAX_GROUP_MEMBERS - 1)
			{
				//combining the members would fit in one group
				qboolean deleteWhenDone = qtrue;
				//combine the members of mine into theirs
				for (int j = 0; j < group->numGroup; j++)
				{
					member = &g_entities[group->member[j].number];
					if (level.groups[i].enemy == NULL)
					{
						//special case for groups without enemies, must be in range
						if (!AI_ValidateNoEnemyGroupMember(&level.groups[i], member))
						{
							deleteWhenDone = qfalse;
							continue;
						}
					}
					//remove this member from this group
					AI_DeleteGroupMember(group, j);
					//keep marker at same place since we deleted this guy and shifted everyone up one
					j--;
					//add them to the earlier group
					AI_InsertGroupMember(&level.groups[i], member);
				}
				//return and delete this group
				if (deleteWhenDone)
				{
					return qfalse;
				}
			}
		}
	}
	//clear numStates
	for (i = 0; i < NUM_SQUAD_STATES; i++)
	{
		group->numState[i] = 0;
	}

	//go through group and validate each membership
	group->commander = NULL;
	for (i = 0; i < group->numGroup; i++)
	{
		member = &g_entities[group->member[i].number];

		//Must be alive
		if (member->health <= 0)
		{
			AI_DeleteGroupMember(group, i);
			//keep marker at same place since we deleted this guy and shifted everyone up one
			i--;
		}
		else if (group->memberValidateTime < level.time && !AI_ValidateGroupMember(group, member))
		{
			//remove this one from the group
			AI_DeleteGroupMember(group, i);
			//keep marker at same place since we deleted this guy and shifted everyone up one
			i--;
		}
		else
		{
			//membership is valid
			//keep track of squadStates
			group->numState[member->NPC->squadState]++;
			if (!group->commander || member->NPC->rank > group->commander->NPC->rank)
			{
				//keep track of highest rank
				group->commander = member;
			}
		}
	}
	if (group->memberValidateTime < level.time)
	{
		group->memberValidateTime = level.time + Q_irand(500, 2500);
	}

	//calc the morale of this group
	group->morale = group->moraleAdjust;
	for (i = 0; i < group->numGroup; i++)
	{
		member = &g_entities[group->member[i].number];
		if (member->NPC->rank < RANK_ENSIGN)
		{
			//grunts
			group->morale++;
		}
		else
		{
			group->morale += member->NPC->rank;
		}
		if (group->commander && SHR_CVAR(debugNPCAI).integer)
		{
			G_DebugLine(group->commander->SHR_currentOrigin, member->SHR_currentOrigin, FRAMETIME, 0x00ff00ff);
		}
	}
	if (group->enemy)
	{
		//modify morale based on enemy health and weapon
		if (group->enemy->health < 10)
		{
			group->morale += 10;
		}
		else if (group->enemy->health < 25)
		{
			group->morale += 5;
		}
		else if (group->enemy->health < 50)
		{
			group->morale += 2;
		}
		switch (group->enemy->s.weapon)
		{
		case WP_SABER:
			group->morale -= 5;
			break;
		case WP_BRYAR_PISTOL:
		case WP_SBD_PISTOL:
		case WP_BLASTER_PISTOL:
		case WP_DROIDEKA:
			group->morale += 3;
			break;
		case WP_DISRUPTOR:
			group->morale += 2;
			break;
		case WP_REPEATER:
		case WP_Z6_ROTARY_CANNON:
			group->morale -= 1;
			break;
		case WP_FLECHETTE:
			group->morale -= 2;
			break;
		case WP_ROCKET_LAUNCHER:
			group->morale -= 10;
			break;
		case WP_CONCUSSION:
			group->morale -= 12;
			break;
		case WP_THERMAL:
			group->morale -= 5;
			break;
		case WP_TRIP_MINE:
			group->morale -= 3;
			break;
		case WP_DET_PACK:
			group->morale -= 10;
			break;
		case WP_MELEE: // Any ol' melee attack
			group->morale += 20;
			break;
		case WP_STUN_BATON:
			group->morale += 10;
			break;
		case WP_EMPLACED_GUN:
			group->morale -= 8;
			break;
		case WP_ATST_MAIN:
			group->morale -= 8;
			break;
		case WP_ATST_SIDE:
			group->morale -= 20;
			break;
		default:;
		}
	}
	if (group->moraleDebounce < level.time)
	{
		//slowly degrade whatever moraleAdjusters we may have
		if (group->moraleAdjust > 0)
		{
			group->moraleAdjust--;
		}
		else if (group->moraleAdjust < 0)
		{
			group->moraleAdjust++;
		}
		group->moraleDebounce = level.time + 1000; //FIXME: define?
	}
	//mark this group as not having been run this frame
	group->processed = qfalse;

	return (qboolean)(group->numGroup > 0);
}
#else // MP keeps its own version of this function
qboolean AI_RefreshGroup(AIGroupInfo_t* group)
{
	gentity_t* member = NULL;
	int i = 0; //, j;

	//see if we should merge with another group
	for (i = 0; i < MAX_FRAME_GROUPS; i++)
	{
		if (&level.groups[i] == group)
		{
			break;
		}
		// JA:R fix: never merge squads of different teams, or into an empty slot
		if (level.groups[i].enemy == group->enemy && level.groups[i].team == group->team && level.groups[i].numGroup > 0)
		{
			//2 groups with same enemy
			if (level.groups[i].numGroup + group->numGroup < MAX_GROUP_MEMBERS - 1)
			{
				//combining the members would fit in one group
				qboolean delete_when_done = qtrue;

				//combine the members of mine into theirs
				for (int j = 0; j < group->numGroup; j++)
				{
					member = &g_entities[group->member[j].number];
					if (level.groups[i].enemy == NULL)
					{
						//special case for groups without enemies, must be in range
						if (!AI_ValidateNoEnemyGroupMember(&level.groups[i], member))
						{
							delete_when_done = qfalse;
							continue;
						}
					}
					//remove this member from this group
					AI_DeleteGroupMember(group, j);
					//keep marker at same place since we deleted this guy and shifted everyone up one
					j--;
					//add them to the earlier group
					AI_InsertGroupMember(&level.groups[i], member);
				}
				//return and delete this group
				if (delete_when_done)
				{
					return qfalse;
				}
			}
		}
	}
	//clear numStates
	for (i = 0; i < NUM_SQUAD_STATES; i++)
	{
		group->numState[i] = 0;
	}

	//go through group and validate each membership
	group->commander = NULL;
	for (i = 0; i < group->numGroup; i++)
	{
		/*
		//this checks for duplicate copies of one member in a group
		for ( j = 0; j < group->numGroup; j++ )
		{
			if ( i != j )
			{
				if ( group->member[i].number == group->member[j].number )
				{
					break;
				}
			}
		}
		if ( j < group->numGroup )
		{//found a dupe!
			trap->Printf( S_COLOR_RED"ERROR: member %s(%d) a duplicate group member!!!\n", g_entities[group->member[i].number].targetname, group->member[i].number );
			AI_DeleteGroupMember( group, i );
			i--;
			continue;
		}
		*/
		member = &g_entities[group->member[i].number];

		//Must be alive
		if (member->health <= 0)
		{
			AI_DeleteGroupMember(group, i);
			//keep marker at same place since we deleted this guy and shifted everyone up one
			i--;
		}
		else if (group->memberValidateTime < level.time && !AI_ValidateGroupMember(group, member))
		{
			//remove this one from the group
			AI_DeleteGroupMember(group, i);
			//keep marker at same place since we deleted this guy and shifted everyone up one
			i--;
		}
		else
		{
			//membership is valid
			//keep track of squadStates
			group->numState[member->NPC->squadState]++;
			if (!group->commander || member->NPC->rank > group->commander->NPC->rank)
			{
				//keep track of highest rank
				group->commander = member;
			}
		}
	}
	if (group->memberValidateTime < level.time)
	{
		group->memberValidateTime = level.time + Q_irand(500, 2500);
	}
	//Now add any new guys as long as we're not full
	/*
	for ( i = 0, member = &g_entities[0]; i < globals.num_entities && group->numGroup < (MAX_GROUP_MEMBERS - 1); i++, member++)
	{
		if ( !AI_ValidateGroupMember( group, member ) )
		{//FIXME: keep track of those who aren't angry yet and see if we should wake them after we assemble the core group
			continue;
		}
		if ( member->NPC->group == group )
		{//DOH, already in our group
			continue;
		}

		//store it
		AI_InsertGroupMember( group, member );
	}
	*/

	//calc the morale of this group
	group->morale = group->moraleAdjust;
	for (i = 0; i < group->numGroup; i++)
	{
		member = &g_entities[group->member[i].number];
		if (member->NPC->rank < RANK_ENSIGN)
		{
			//grunts
			group->morale++;
		}
		else
		{
			group->morale += member->NPC->rank;
		}
		if (group->commander && d_npcai.integer)
		{
			//G_DebugLine( group->commander->r.currentOrigin, member->r.currentOrigin, FRAMETIME, 0x00ff00ff, qtrue );
			G_TestLine(group->commander->r.currentOrigin, member->r.currentOrigin, 0x00000ff, FRAMETIME);
		}
	}
	if (group->enemy)
	{
		//modify morale based on enemy health and weapon
		if (group->enemy->health < 10)
		{
			group->morale += 10;
		}
		else if (group->enemy->health < 25)
		{
			group->morale += 5;
		}
		else if (group->enemy->health < 50)
		{
			group->morale += 2;
		}
		switch (group->enemy->s.weapon)
		{
		case WP_SABER:
			group->morale -= 5;
			break;
		case WP_BRYAR_PISTOL:
			group->morale += 3;
			break;
		case WP_DISRUPTOR:
			group->morale += 2;
			break;
		case WP_REPEATER:
			group->morale -= 1;
			break;
		case WP_FLECHETTE:
			group->morale -= 2;
			break;
		case WP_ROCKET_LAUNCHER:
			group->morale -= 10;
			break;
		case WP_THERMAL:
			group->morale -= 5;
			break;
		case WP_TRIP_MINE:
			group->morale -= 3;
			break;
		case WP_DET_PACK:
			group->morale -= 10;
			break;
		case WP_MELEE: // Any ol' melee attack
			group->morale += 20;
			break;
		case WP_STUN_BATON:
			group->morale += 10;
			break;
		case WP_EMPLACED_GUN:
			group->morale -= 8;
			break;
		default:;
			//		case WP_ATST_MAIN:
			//			group->morale -= 8;
			//			break;
			//		case WP_ATST_SIDE:
			//			group->morale -= 20;
			//			break;
		}
	}
	if (group->moraleDebounce < level.time)
	{
		//slowly degrade whatever moraleAdjusters we may have
		if (group->moraleAdjust > 0)
		{
			group->moraleAdjust--;
		}
		else if (group->moraleAdjust < 0)
		{
			group->moraleAdjust++;
		}
		group->moraleDebounce = level.time + 1000; //FIXME: define?
	}
	//mark this group as not having been run this frame
	group->processed = qfalse;

	return group->numGroup > 0;
}
#endif

void AI_UpdateGroups(void)
{
	if (SHR_CVAR(d_noGroupAI).integer)
	{
		return;
	}
	//Clear all Groups
	for (int i = 0; i < MAX_FRAME_GROUPS; i++)
	{
		if (!level.groups[i].numGroup || AI_RefreshGroup(&level.groups[i]) == qfalse)
		{
			memset(&level.groups[i], 0, sizeof level.groups[i]);
		}
	}
}

qboolean AI_GroupContainsEntNum(const AIGroupInfo_t* group, const int entNum)
{
	if (!group)
	{
		return qfalse;
	}
	for (int i = 0; i < group->numGroup; i++)
	{
		if (group->member[i].number == entNum)
		{
			return qtrue;
		}
	}
	return qfalse;
}

//Overload

/*
void AI_GetGroup( AIGroupInfo_t &group, gentity_t *ent, int radius )
{
	if ( ent->client == NULL )
		return;

	vec3_t	temp, angles;

	//FIXME: This is specialized code.. move?
	if ( ent->enemy )
	{
		VectorSubtract( ent->enemy->currentOrigin, ent->currentOrigin, temp );
		VectorNormalize( temp );	//FIXME: Needed?
		vectoangles( temp, angles );
	}
	else
	{
		VectorCopy( ent->currentAngles, angles );
	}

	AI_GetGroup( group, ent->currentOrigin, ent->currentAngles, DEFAULT_RADIUS, radius, ent->client->playerTeam, ent, ent->enemy );
}
*/

/*
-------------------------
AI_DistributeAttack
-------------------------
*/

#ifdef SP_GAME
gentity_t* AI_DistributeAttack(const gentity_t* attacker, gentity_t* enemy, const team_t team, const int threshold)
{
	//Don't take new targets
	if (NPCS.NPC->SHR_svFlags & SVF_LOCKEDENEMY)
		return enemy;

	const int numSurrounding = AI_GetGroupSize(enemy->SHR_currentOrigin, 48, team, attacker);

	//First, see if we should look for the player
	if (enemy != &SHR_PLAYER0)
	{
		const int aroundPlayer = AI_GetGroupSize(SHR_PLAYER0.SHR_currentOrigin, 48, team, attacker);

		//See if we're above our threshold
		if (aroundPlayer < threshold)
		{
			return &SHR_PLAYER0;
		}
	}

	//See if our current enemy is still ok
	if (numSurrounding < threshold)
		return enemy;

	//Otherwise we need to take a new enemy if possible
	vec3_t mins{}, maxs{};

	//Setup the bbox to search in
	for (int i = 0; i < 3; i++)
	{
		mins[i] = enemy->SHR_currentOrigin[i] - 512;
		maxs[i] = enemy->SHR_currentOrigin[i] + 512;
	}

	//Get the number of entities in a given space
	gentity_t* radius_ents[MAX_RADIUS_ENTS];

	const int num_ents = gi.EntitiesInBox(mins, maxs, radius_ents, MAX_RADIUS_ENTS);

	//Cull this list
	for (int j = 0; j < num_ents; j++)
	{
		//Validate clients
		if (radius_ents[j]->client == NULL)
			continue;

		//Skip the requested avoid ent if present
		if (radius_ents[j] == enemy)
			continue;

		//Must be on the same team
		if (radius_ents[j]->client->playerTeam != enemy->client->playerTeam)
			continue;

		//Must be alive
		if (radius_ents[j]->health <= 0)
			continue;

		//Must not be overwhelmed
		if (AI_GetGroupSize(radius_ents[j]->SHR_currentOrigin, 48, team, attacker) > threshold)
			continue;

		return radius_ents[j];
	}

	return NULL;
}
#else // MP keeps its own version of this function
gentity_t* AI_DistributeAttack(const gentity_t* attacker, gentity_t* enemy, const team_t team, const int threshold)
{
	int radius_ents[MAX_RADIUS_ENTS];
	vec3_t mins, maxs;

	//Don't take new targets
	//	if ( NPC->svFlags & SVF_LOCKEDENEMY )
	//		return enemy;

	const int num_surrounding = AI_GetGroupSize(enemy->r.currentOrigin, 48, team, attacker);

	//First, see if we should look for the player
	if (enemy != &g_entities[0])
	{
		//rwwFIXMEFIXME: care about all clients not just 0
		const int around_player = AI_GetGroupSize(g_entities[0].r.currentOrigin, 48, team, attacker);

		//See if we're above our threshold
		if (around_player < threshold)
		{
			return &g_entities[0];
		}
	}

	//See if our current enemy is still ok
	if (num_surrounding < threshold)
		return enemy;

	//Otherwise we need to take a new enemy if possible

	//Setup the bbox to search in
	for (int i = 0; i < 3; i++)
	{
		mins[i] = enemy->r.currentOrigin[i] - 512;
		maxs[i] = enemy->r.currentOrigin[i] + 512;
	}

	//Get the number of entities in a given space
	const int num_ents = trap->EntitiesInBox(mins, maxs, radius_ents, MAX_RADIUS_ENTS);

	//Cull this list
	for (int j = 0; j < num_ents; j++)
	{
		gentity_t* check = &g_entities[radius_ents[j]];

		//Validate clients
		if (check->client == NULL)
			continue;

		//Skip the requested avoid ent if present
		if (check == enemy)
			continue;

		//Must be on the same team
		if (check->client->playerTeam != enemy->client->playerTeam)
			continue;

		//Must be alive
		if (check->health <= 0)
			continue;

		//Must not be overwhelmed
		if (AI_GetGroupSize(check->r.currentOrigin, 48, team, attacker) > threshold)
			continue;

		return check;
	}

	return NULL;
}
#endif