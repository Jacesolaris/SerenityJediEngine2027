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

extern qboolean NPC_CheckSurrender(void);
extern void NPC_BehaviorSet_Default(int b_state);

void NPC_BSCivilian_Default(const int b_state)
{
	if (NPCS.NPC->enemy
		&& NPCS.NPC->s.weapon == WP_NONE
		&& NPC_CheckSurrender())
	{
		//surrendering, do nothing
	}
	else if (NPCS.NPC->enemy
		&& NPCS.NPC->s.weapon == WP_NONE
		&& b_state != BS_HUNT_AND_KILL
		&& !SHR_TaskIDPending(NPCS.NPC, TID_MOVE_NAV))
	{
		//if in battle and have no weapon, run away, fixme: when in BS_HUNT_AND_KILL, they just stand there
		if (!NPCS.NPCInfo->goalEntity
			|| b_state != BS_FLEE //not fleeing
			|| SHR_BSFleeReached() //have reached our flee goal
			&& NPCS.NPC->enemy //still have enemy (NPC_BSFlee checks enemy and can clear it)
			&& DistanceSquared(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->enemy->SHR_currentOrigin) < 16384) //enemy within 128
		{
			//run away!
			NPC_StartFlee(NPCS.NPC->enemy, NPCS.NPC->enemy->SHR_currentOrigin, AEL_DANGER_GREAT, 5000, 10000);
		}
	}
	else
	{
		//not surrendering
		NPC_BehaviorSet_Default(b_state);
	}
	if (!VectorCompare(NPCS.NPC->client->ps.moveDir, vec3_origin))
	{
		//moving
		if (NPCS.NPC->client->ps.legsAnim == BOTH_COWER1)
		{
			//stop cowering anim on legs
			NPCS.NPC->client->ps.SHR_legsAnimTimer = 0;
		}
	}
}