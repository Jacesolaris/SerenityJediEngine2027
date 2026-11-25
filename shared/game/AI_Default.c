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
extern void npc_check_speak(gentity_t* speaker_npc); // NPC_AI_Stormtrooper.c (SP: idle combat chatter, g_Advancedaitalk)
#endif

#ifndef SP_GAME // file-level declarations of the MP versions below
extern qboolean in_camera;
#endif

extern int g_crosshairEntNum;
extern void NPC_CheckEvasion(void);
extern void G_AddVoiceEvent(const gentity_t* self, int event, int speak_debounce_time);
extern qboolean NPC_IsGunner(const gentity_t* self);
extern void NPC_AngerSound(void);

//
extern qboolean npc_is_dark_jedi(const gentity_t* self);
extern qboolean npc_is_light_jedi(const gentity_t* self);
extern void jedi_set_enemy_info(vec3_t enemy_dest, vec3_t enemy_dir, float* enemy_dist, vec3_t enemy_movedir,
	float* enemy_movespeed, int prediction);

#ifdef SP_GAME // SP only: the MP game has no version of this
void npc_check_speak(gentity_t* speaker_npc)
{
	vec3_t enemy_dir, enemy_movedir, enemy_dest;
	float enemy_dist = 0.0f, enemy_movespeed = 0.0f;

	//See where enemy will be 300 ms from now
	jedi_set_enemy_info(enemy_dest, enemy_dir, &enemy_dist, enemy_movedir, &enemy_movespeed, 300);

	if (speaker_npc->check_speach_time > level.time)
	{
		return; // not yet...
	}

	speaker_npc->check_speach_time = level.time + 5000 + Q_irand(0, 10000);

	if (NPCS.NPC->enemy && SHR_ClearLOSEnt(NPCS.NPC->enemy)
		&& enemy_dist < 512 && enemy_dist > 64) // Have enemy...
	{
		if (npc_is_dark_jedi(speaker_npc))
		{
			// Do taunt/anger...
			const int voice_event = Q_irand(0, 8);

			if (SHR_CVAR(d_npctalk).integer)
			{
				gi.Printf("Dark Jedi speaker_npc talking\n");
			}

			switch (voice_event)
			{
			case 0:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_ANGER1, EV_ANGER3), 10000);
				break;
			case 1:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_COMBAT1, EV_COMBAT3), 10000);
				break;
			case 2:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_DEFLECT1, EV_DEFLECT3), 10000);
				break;
			case 3:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_GLOAT1, EV_GLOAT3), 10000);
				break;
			case 4:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_JCHASE1, EV_JCHASE3), 10000);
				break;
			case 5:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_JDETECTED1, EV_JDETECTED3), 10000);
				break;
			case 6:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_JLOST1, EV_JLOST3), 10000);
				break;
			case 7:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_TAUNT1, EV_TAUNT3), 10000);
				break;
			default:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_VICTORY1, EV_VICTORY3), 10000);
				break;
			}
		}
		else if (npc_is_light_jedi(speaker_npc))
		{
			// Do taunt...
			const int voice_event = Q_irand(0, 9);

			if (SHR_CVAR(d_npctalk).integer)
			{
				gi.Printf("Light Jedi speaker_npc talking\n");
			}

			switch (voice_event)
			{
			case 0:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_ANGER1, EV_ANGER3), 10000);
				break;
			case 1:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_COMBAT1, EV_COMBAT3), 10000);
				break;
			case 2:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_CONFUSE1, EV_CONFUSE3), 10000);
				break;
			case 3:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_DEFLECT1, EV_DEFLECT3), 10000);
				break;
			case 4:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_GLOAT1, EV_GLOAT3), 10000);
				break;
			case 5:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_JCHASE1, EV_JCHASE3), 10000);
				break;
			case 6:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_JDETECTED1, EV_JDETECTED3), 10000);
				break;
			case 7:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_JLOST1, EV_JLOST3), 10000);
				break;
			case 8:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_TAUNT1, EV_TAUNT3), 10000);
				break;
			default:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_VICTORY1, EV_VICTORY3), 10000);
				break;
			}
		}
		else
		{
			// Do taunt/anger...
			const int voice_event = Q_irand(0, 9);

			if (SHR_CVAR(d_npctalk).integer)
			{
				gi.Printf("Random speaker_npc talking\n");
			}

			switch (voice_event)
			{
			case 0:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_ANGER1, EV_ANGER3), 10000);
				break;
			case 1:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_CHASE1, EV_CHASE3), 10000);
				break;
			case 2:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_COVER1, EV_COVER5), 10000);
				break;
			case 3:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_DETECTED1, EV_DETECTED5), 10000);
				break;
			case 4:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_ESCAPING1, EV_ESCAPING3), 10000);
				break;
			case 5:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_GIVEUP1, EV_GIVEUP4), 10000);
				break;
			case 6:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_SIGHT1, EV_SIGHT3), 10000);
				break;
			case 7:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_LOOK1, EV_LOOK2), 10000);
				break;
			case 8:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_SUSPICIOUS1, EV_SUSPICIOUS5), 10000);
				break;
			default:
				G_AddVoiceEvent(speaker_npc, Q_irand(EV_TAUNT1, EV_TAUNT3), 10000);
				break;
			}
		}
	}
}
#endif

void g_do_m_block_response(const gentity_t* speaker_npc_self)
{
	if (SHR_NPCTALK_DEBUG)
	{
		SHR_Printf("Combat speaker_npc talking\n");
	}
	const int voice_event = Q_irand(0, 5);

	switch (voice_event)
	{
	case 0:
		G_AddVoiceEvent(speaker_npc_self, Q_irand(EV_GLOAT1, EV_GLOAT3), 2000);
		break;
	case 1:
		G_AddVoiceEvent(speaker_npc_self, Q_irand(EV_JCHASE1, EV_JCHASE3), 2000);
		break;
	case 2:
		G_AddVoiceEvent(speaker_npc_self, Q_irand(EV_COMBAT1, EV_COMBAT3), 2000);
		break;
	case 3:
		G_AddVoiceEvent(speaker_npc_self, Q_irand(EV_ANGER1, EV_ANGER3), 2000);
		break;
	case 4:
		G_AddVoiceEvent(speaker_npc_self, Q_irand(EV_TAUNT1, EV_TAUNT3), 2000);
		break;
	default:
		G_AddVoiceEvent(speaker_npc_self, Q_irand(EV_PUSHED1, EV_PUSHED3), 2000);
		break;
	}
}

void NPC_LostEnemyDecideChase(void)
{
	switch (NPCS.NPCInfo->behaviorState)
	{
	case BS_HUNT_AND_KILL:
		//We were chasing him and lost him, so try to find him
		if (NPCS.NPC->enemy == NPCS.NPCInfo->goalEntity && NPCS.NPC->enemy->lastWaypoint != WAYPOINT_NONE)
		{
			//Remember his last valid Wp, then check it out
			//FIXME: Should we only do this if there's no other enemies or we've got LOCKED_ENEMY on?
			NPC_BSSearchStart(NPCS.NPC->enemy->lastWaypoint, BS_SEARCH);
		}
		//If he's not our goalEntity, we're running somewhere else, so lose him
		break;
	default:
		break;
	}
	G_ClearEnemy(NPCS.NPC);
}

/*
-------------------------
NPC_StandIdle
-------------------------
*/

static void NPC_StandIdle(void)
{
	if (!in_camera)
	{
		NPC_CheckEvasion();
	}
}

#ifdef SP_GAME
static qboolean NPC_StandTrackAndShoot(const gentity_t* npc, const qboolean can_duck)
{
	qboolean attack_ok = qfalse;
	qboolean duck_ok = qfalse;
	qboolean faced = qfalse;

	// Update enemy every frame
	NPC_CheckEnemy(qtrue, qfalse, qtrue);

	// Duck logic (reduced frequency)
	if (can_duck && npc->health < 20)
	{
		if (Q_flrand(0.0f, 1.0f) < 0.25f)
			duck_ok = qtrue;
	}

	// Try to attack
	if (!duck_ok)
	{
		float attack_scale = 1.25f; // more human-like firing
		attack_ok = NPC_CheckCanAttack(attack_scale, qtrue);
		faced = qtrue;
	}

	// Duck if needed
	if (can_duck && (duck_ok || (!attack_ok && NPCS.client->fireDelay == 0)) && NPCS.ucmd.upmove != -127)
	{
		if (!duck_ok && npc->enemy && npc->enemy->client)
		{
			if (npc->enemy->enemy == npc && (npc->enemy->client->buttons & BUTTON_ATTACK))
			{
				if (NPC_CheckDefend(1.0f))
					duck_ok = qtrue;
			}
		}

		if (duck_ok)
		{
			NPCS.ucmd.upmove = -127;
			NPCS.NPCInfo->duckDebounceTime = level.time + 1000;
		}
	}

	return faced;
}
#else // MP keeps its own version of this function
qboolean NPC_StandTrackAndShoot(const gentity_t* npc, const qboolean can_duck)
{
	qboolean attack_ok = qfalse;
	qboolean duck_ok = qfalse;
	qboolean faced = qfalse;

	//First see if we're hurt bad- if so, duck
	//FIXME: if even when ducked, we can shoot someone, we should.
	//Maybe is can be shot even when ducked, we should run away to the nearest cover?
	if (can_duck)
	{
		if (npc->health < 20)
		{
			if (Q_flrand(0.0f, 1.0f))
			{
				duck_ok = qtrue;
			}
		}
	}

	if (!duck_ok)
	{
		const float attack_scale = 1.0;
		//made this whole part a function call
		attack_ok = NPC_CheckCanAttack(attack_scale, qtrue);
		faced = qtrue;
	}

	if (can_duck && (duck_ok || !attack_ok && NPCS.client->ps.weaponTime <= 0) && NPCS.ucmd.upmove != -127)
	{
		//if we didn't attack check to duck if we're not already
		if (!duck_ok)
		{
			if (npc->enemy->client)
			{
				if (npc->enemy->enemy == npc)
				{
					if (npc->enemy->client->buttons & BUTTON_ATTACK)
					{
						//FIXME: determine if enemy fire angles would hit me or get close
						if (NPC_CheckDefend(1.0)) //FIXME: Check self-preservation?  Health?
						{
							duck_ok = qtrue;
						}
					}
				}
			}
		}

		if (duck_ok)
		{
			//duck and don't shoot
			NPCS.ucmd.upmove = -127;
			NPCS.NPCInfo->duckDebounceTime = level.time + 1000; //duck for a full second
		}
	}

	return faced;
}
#endif

void NPC_BSIdle(void)
{
	//FIXME if there is no nav data, we need to do something else
	// if we're stuck, try to move around it
	if (UpdateGoal())
	{
		NPC_MoveToGoal(qtrue);
	}

	if (NPCS.ucmd.forwardmove == 0 && NPCS.ucmd.rightmove == 0 && NPCS.ucmd.upmove == 0)
	{
		NPC_StandIdle();
	}

	NPC_UpdateAngles(qtrue, qtrue);
	NPCS.ucmd.buttons |= BUTTON_WALKING;
}

static void NPC_BSRun(void)
{
	//FIXME if there is no nav data, we need to do something else
	// if we're stuck, try to move around it
	if (UpdateGoal())
	{
		NPC_MoveToGoal(qtrue);
	}

	NPC_UpdateAngles(qtrue, qtrue);
}

void NPC_BSStandGuard(void)
{
	if (NPCS.NPC->enemy == NULL)
	{
		//Possible to pick one up by being shot
		if (Q_flrand(0.0f, 1.0f) < 0.5)
		{
			if (NPCS.NPC->client->enemyTeam)
			{
				gentity_t* newenemy = NPC_PickEnemy(
					NPCS.NPC, NPCS.NPC->client->enemyTeam,
					(qboolean)(NPCS.NPC->cantHitEnemyCounter < 10),
					(qboolean)(NPCS.NPC->client->enemyTeam == SHR_TEAM_PLAYER),
					qtrue);

				//only checks for vis if couldn't hit last enemy
				if (newenemy)
				{
					G_SetEnemy(NPCS.NPC, newenemy);
				}
			}
		}
	}

	npc_check_speak(NPCS.NPC);

	if (NPCS.NPC->enemy != NULL)
	{
		if (NPCS.NPCInfo->tempBehavior == BS_STAND_GUARD)
		{
			NPCS.NPCInfo->tempBehavior = BS_DEFAULT;
		}

		if (NPCS.NPCInfo->behaviorState == BS_STAND_GUARD)
		{
			NPCS.NPCInfo->behaviorState = BS_STAND_AND_SHOOT;
		}
	}

	NPC_UpdateAngles(qtrue, qtrue);
}

/*
-------------------------
NPC_BSHuntAndKill
-------------------------
*/

void NPC_BSHuntAndKill(void)
{
	qboolean turned = qfalse;

	if (!in_camera)
	{
		NPC_CheckEvasion();
	}

	NPC_CheckEnemy((qboolean)(NPCS.NPCInfo->tempBehavior != BS_HUNT_AND_KILL), qfalse, qtrue);
	//don't find new enemy if this is tempbehav

	if (NPCS.NPC->enemy)
	{
		const visibility_t o_e_vis = NPCS.enemyVisibility = NPC_CheckVisibility(NPCS.NPC->enemy, CHECK_FOV | CHECK_SHOOT);
		//CHECK_360|//CHECK_PVS|
		if (NPCS.enemyVisibility > VIS_PVS)
		{
			if (!NPC_EnemyTooFar(NPCS.NPC->enemy, 0, qtrue))
			{
				//Enemy is close enough to shoot - FIXME: this next func does this also, but need to know here for info on whether ot not to turn later
				NPC_CheckCanAttack(1.0, qfalse);
				turned = qtrue;
			}
		}

		const int cur_anim = NPCS.NPC->client->ps.legsAnim;
		if (cur_anim != BOTH_ATTACK1 && cur_anim != BOTH_ATTACK2 && cur_anim != BOTH_ATTACK_DUAL && cur_anim !=
			BOTH_ATTACK_FP && cur_anim != BOTH_ATTACK3 && cur_anim != BOTH_MELEE1 && cur_anim != BOTH_MELEE2
			&& cur_anim != BOTH_MELEE3 && cur_anim != BOTH_MELEE4 && cur_anim != BOTH_MELEE5 && cur_anim != BOTH_MELEE6
			&&
			cur_anim != BOTH_MELEE_L
			&& cur_anim != BOTH_MELEE_R && cur_anim != BOTH_MELEEUP && cur_anim != BOTH_WOOKIE_SLAP)
		{
			vec3_t vec;
			//Don't move toward enemy if we're in a full-body attack anim
			//FIXME, use ideal_distance to determin if we need to close distance
			VectorSubtract(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin, vec);
			const float enemy_dist = VectorLength(vec);
			if (enemy_dist > 48 && (enemy_dist * 1.5 * (enemy_dist * 1.5) >= NPC_MaxDistSquaredForWeapon() ||
				o_e_vis != VIS_SHOOT ||
				//!(ucmd.buttons & BUTTON_ATTACK) ||
				enemy_dist > ideal_distance() * 3))
			{
				//We should close in?
				NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;

				NPC_MoveToGoal(qtrue);
			}
			else if (enemy_dist < ideal_distance())
			{
				//We should back off?
				//if(ucmd.buttons & BUTTON_ATTACK)
				{
					NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
					NPCS.NPCInfo->goalRadius = 12;
					NPC_MoveToGoal(qtrue);

					NPCS.ucmd.forwardmove *= -1;
					NPCS.ucmd.rightmove *= -1;
					VectorScale(NPCS.NPC->client->ps.moveDir, -1, NPCS.NPC->client->ps.moveDir);

					NPCS.ucmd.buttons |= BUTTON_WALKING;
				}
			} //otherwise, stay where we are
		}
	}
	else
	{
		//ok, stand guard until we find an enemy
		if (!in_camera)
		{
			NPC_CheckEvasion();
		}
		if (NPCS.NPCInfo->tempBehavior == BS_HUNT_AND_KILL)
		{
			NPCS.NPCInfo->tempBehavior = BS_DEFAULT;
		}
		else
		{
			NPCS.NPCInfo->tempBehavior = BS_STAND_GUARD;
			NPC_BSStandGuard();
		}
		return;
	}

	if (!turned)
	{
		NPC_UpdateAngles(qtrue, qtrue);
	}
}

void NPC_BSStandAndShoot(void)
{
	if (NPCS.NPC->client->playerTeam && NPCS.NPC->client->enemyTeam)
	{
	}
	if (!in_camera)
	{
		NPC_CheckEvasion();
	}

	NPC_CheckEnemy(qtrue, qfalse, qtrue);

	if (NPCS.NPCInfo->duckDebounceTime > level.time && NPCS.NPC->client->ps.weapon != WP_SABER)
	{
		NPCS.ucmd.upmove = -127;
		if (NPCS.NPC->enemy)
		{
			NPC_CheckCanAttack(1.0, qtrue);
		}
		return;
	}

	if (NPCS.NPC->enemy)
	{
		if (!NPC_StandTrackAndShoot(NPCS.NPC, qtrue))
		{
			//That func didn't update our angles
			NPCS.NPCInfo->desiredYaw = NPCS.NPC->client->ps.viewangles[YAW];
			NPCS.NPCInfo->desiredPitch = NPCS.NPC->client->ps.viewangles[PITCH];
			NPC_UpdateAngles(qtrue, qtrue);
		}
	}
	else
	{
		NPCS.NPCInfo->desiredYaw = NPCS.NPC->client->ps.viewangles[YAW];
		NPCS.NPCInfo->desiredPitch = NPCS.NPC->client->ps.viewangles[PITCH];
		NPC_UpdateAngles(qtrue, qtrue);
	}
}

void NPC_BSRunAndShoot(void)
{
	if (!in_camera)
	{
		NPC_CheckEvasion();
	}

	NPC_CheckEnemy(qtrue, qfalse, qtrue);

	npc_check_speak(NPCS.NPC);

	if (NPCS.NPCInfo->duckDebounceTime > level.time) // && NPCInfo->hidingGoal )
	{
		NPCS.ucmd.upmove = -127;
		if (NPCS.NPC->enemy)
		{
			NPC_CheckCanAttack(1.0, qfalse);
		}
		return;
	}

	if (NPCS.NPC->enemy)
	{
		const int monitor = NPCS.NPC->cantHitEnemyCounter;
		NPC_StandTrackAndShoot(NPCS.NPC, qfalse); //(NPCInfo->hidingGoal != NULL) );

		if (!(NPCS.ucmd.buttons & BUTTON_ATTACK) && NPCS.ucmd.upmove >= 0 && NPCS.NPC->cantHitEnemyCounter > monitor)
		{
			//not crouching and not firing
			vec3_t vec;

			VectorSubtract(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin, vec);
			vec[2] = 0;
			if (VectorLength(vec) > 128 || NPCS.NPC->cantHitEnemyCounter >= 10)
			{
				if (NPCS.NPC->cantHitEnemyCounter > 60)
				{
					NPCS.NPC->cantHitEnemyCounter = 60;
				}

				if (NPCS.NPC->cantHitEnemyCounter >= (NPCS.NPCInfo->stats.aggression + 1) * 10)
				{
					NPC_LostEnemyDecideChase();
				}

				//chase and face
				NPCS.ucmd.angles[YAW] = 0;
				NPCS.ucmd.angles[PITCH] = 0;
				NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
				NPCS.NPCInfo->goalRadius = 12;
				NPC_MoveToGoal(qtrue);
				NPC_UpdateAngles(qtrue, qtrue);
			}
			else
			{
				//FIXME: this could happen if they're just on the other side
				//of a thin wall or something else blocking out shot.  That
				//would make us just stand there and not go around it...
				//but maybe it's okay- might look like we're waiting for
				//him to come out...?
				//Current solution: runs around if cantHitEnemyCounter gets
				//to 10 (1 second).
			}
		}
		else
		{
			//Clear the can't hit enemy counter here
			NPCS.NPC->cantHitEnemyCounter = 0;
		}
	}
	else
	{
		if (NPCS.NPCInfo->tempBehavior == BS_HUNT_AND_KILL)
		{
			//lost him, go back to what we were doing before
			NPCS.NPCInfo->tempBehavior = BS_DEFAULT;
		}
	}
}

//Simply turn until facing desired angles
static void NPC_BSFace(void)
{
	//FIXME: once you stop sending turning info, they reset to whatever their delta_angles was last????
	//Once this is over, it snaps back to what it was facing before- WHY???
	if (NPC_UpdateAngles(qtrue, qtrue))
	{
		SHR_TaskIDComplete(NPCS.NPC, TID_BSTATE);

		NPCS.NPCInfo->desiredYaw = NPCS.client->ps.viewangles[YAW];
		NPCS.NPCInfo->desiredPitch = NPCS.client->ps.viewangles[PITCH];

		NPCS.NPCInfo->aimTime = 0; //ok to turn normally now
	}
}

void NPC_BSPointShoot(const qboolean shoot)
{
	//FIXME: doesn't check for clear shot...
	vec3_t muzzle, dir, angles, org;

	if (!NPCS.NPC->enemy || !NPCS.NPC->enemy->inuse || NPCS.NPC->enemy->NPC && NPCS.NPC->enemy->health <= 0)
	{
		//FIXME: should still keep shooting for a second or two after they actually die...
		SHR_TaskIDComplete(NPCS.NPC, TID_BSTATE);
		goto finished;
	}

	if (!in_camera)
	{
		NPC_CheckEvasion();
	}

	CalcEntitySpot(NPCS.NPC, SPOT_WEAPON, muzzle);
	CalcEntitySpot(NPCS.NPC->enemy, SPOT_HEAD, org); //Was spot_org
	//Head is a little high, so let's aim for the chest:
	if (NPCS.NPC->enemy->client)
	{
		org[2] -= 12; //NOTE: is this enough?
	}

	VectorSubtract(org, muzzle, dir);
	vectoangles(dir, angles);

	switch (NPCS.NPC->client->ps.weapon)
	{
	case WP_NONE:
	case WP_MELEE:
	case WP_TUSKEN_STAFF:
	case WP_SABER:
		//don't do any pitch change if not holding a firing weapon
		break;
	default:
		NPCS.NPCInfo->desiredPitch = NPCS.NPCInfo->lockedDesiredPitch = AngleNormalize360(angles[PITCH]);
		break;
	}

	NPCS.NPCInfo->desiredYaw = NPCS.NPCInfo->lockedDesiredYaw = AngleNormalize360(angles[YAW]);

	if (NPC_UpdateAngles(qtrue, qtrue))
	{
		//FIXME: if angles clamped, this may never work!
		//NPCInfo->shotTime = NPC->attackDebounceTime = 0;

		if (shoot)
		{
			//FIXME: needs to hold this down if using a weapon that requires it, like phaser...
			NPCS.ucmd.buttons |= BUTTON_ATTACK;
		}

		if (!shoot || !(NPCS.NPC->SHR_svFlags & SVF_LOCKEDENEMY))
		{
			//If locked_enemy is on, dont complete until it is destroyed...
			SHR_TaskIDComplete(NPCS.NPC, TID_BSTATE);
			goto finished;
		}
	}
	else if (shoot && NPCS.NPC->SHR_svFlags & SVF_LOCKEDENEMY)
	{
		//shooting them till their dead, not aiming right at them yet...
		const float dist = VectorLength(dir);
		float yaw_miss_allow = NPCS.NPC->enemy->SHR_maxs[0];
		float pitch_miss_allow = (NPCS.NPC->enemy->SHR_maxs[2] - NPCS.NPC->enemy->SHR_mins[2]) / 2;

		if (yaw_miss_allow < 8.0f)
		{
			yaw_miss_allow = 8.0f;
		}

		if (pitch_miss_allow < 8.0f)
		{
			pitch_miss_allow = 8.0f;
		}

		const float yaw_miss = tan(DEG2RAD(AngleDelta(NPCS.NPC->client->ps.viewangles[YAW], NPCS.NPCInfo->desiredYaw))) * dist;
		const float pitch_miss = tan(DEG2RAD(AngleDelta(NPCS.NPC->client->ps.viewangles[PITCH], NPCS.NPCInfo->desiredPitch))) *
			dist;

		if (yaw_miss_allow >= yaw_miss && pitch_miss_allow > pitch_miss)
		{
			NPCS.ucmd.buttons |= BUTTON_ATTACK;
		}
	}

	return;

finished:
	NPCS.NPCInfo->desiredYaw = NPCS.client->ps.viewangles[YAW];
	NPCS.NPCInfo->desiredPitch = NPCS.client->ps.viewangles[PITCH];

	NPCS.NPCInfo->aimTime = 0; //ok to turn normally now
}

/*
void NPC_BSMove(void)
Move in a direction, face another
*/
static void NPC_BSMove(void)
{
	NPC_CheckEnemy(qtrue, qfalse, qtrue);
	if (NPCS.NPC->enemy)
	{
		NPC_CheckCanAttack(1.0, qfalse);
	}
	else
	{
		if (!in_camera)
		{
			NPC_CheckEvasion();
		}
		NPC_UpdateAngles(qtrue, qtrue);
	}

	const gentity_t* goal = UpdateGoal();
	if (goal)
	{
		NPC_SlideMoveToGoal();
	}
}

/*
void NPC_BSShoot(void)
Move in a direction, face another
*/

static void NPC_BSShoot(void)
{
	if (!in_camera)
	{
		NPC_CheckEvasion();
	}

	NPCS.enemyVisibility = VIS_SHOOT;

	if (NPCS.client->ps.weaponstate != WEAPON_READY && NPCS.client->ps.weaponstate != WEAPON_FIRING)
	{
		NPCS.client->ps.weaponstate = WEAPON_READY;
	}

	WeaponThink();
}

void NPC_BSPatrol(void)
{
	if (!in_camera)
	{
		NPC_CheckEvasion();
	}

	if (level.time > NPCS.NPCInfo->enemyCheckDebounceTime)
	{
		NPCS.NPCInfo->enemyCheckDebounceTime = level.time + NPCS.NPCInfo->stats.vigilance * 1000;
		NPC_CheckEnemy(qtrue, qfalse, qtrue);
		if (NPCS.NPC->enemy)
		{
			//FIXME: do anger script
			NPCS.NPCInfo->behaviorState = BS_HUNT_AND_KILL;
			NPC_AngerSound();
			return;
		}
	}

	NPCS.NPCInfo->investigateSoundDebounceTime = 0;
	//FIXME if there is no nav data, we need to do something else
	// if we're stuck, try to move around it
	if (UpdateGoal())
	{
		NPC_MoveToGoal(qtrue);
	}

	NPC_UpdateAngles(qtrue, qtrue);

	NPCS.ucmd.buttons |= BUTTON_WALKING;
}

/*
void NPC_BSDefault(void)
	uses various scriptflags to determine how an npc should behave
*/
extern void NPC_CheckGetNewWeapon(void);
extern void NPC_BSST_Attack(void);

void NPC_BSDefault(void)
{
	qboolean move = qtrue;

	if (NPCS.NPCInfo->scriptFlags & SCF_FIRE_WEAPON)
	{
		WeaponThink();
	}

	if (NPCS.NPCInfo->scriptFlags & SCF_FORCED_MARCH)
	{
		//being forced to walk
		if (NPCS.NPC->client->ps.torsoAnim != TORSO_SURRENDER_START)
		{
			NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, TORSO_SURRENDER_START, SETANIM_FLAG_HOLD);
		}
	}

	//look for a new enemy if don't have one and are allowed to look, validate current enemy if have one
	NPC_CheckEnemy((qboolean)((NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES) != 0), qfalse, qtrue);

	if (!NPCS.NPC->enemy)
	{
		if (!(NPCS.NPCInfo->scriptFlags & SCF_IGNORE_ALERTS))
		{
			const int alert_event = NPC_CheckAlertEvents(qtrue, qtrue, -1, qtrue, AEL_DISCOVERED);

			if (alert_event >= 0)
			{
				if (level.alertEvents[alert_event].level >= AEL_DISCOVERED &&
					(NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES))
				{
					if (level.alertEvents[alert_event].owner &&
						level.alertEvents[alert_event].owner != NPCS.NPC &&
						level.alertEvents[alert_event].owner->client &&
						level.alertEvents[alert_event].owner->health >= 0 &&
						level.alertEvents[alert_event].owner->client->playerTeam == NPCS.NPC->client->enemyTeam)
					{
						G_SetEnemy(NPCS.NPC, level.alertEvents[alert_event].owner);
					}
				}
			}
		}
	}

	if (NPCS.NPC->enemy && !(NPCS.NPCInfo->scriptFlags & SCF_FORCED_MARCH))
	{
		NPC_CheckGetNewWeapon();

		if (NPCS.NPC->client->leader &&
			NPCS.NPCInfo->goalEntity == NPCS.NPC->client->leader &&
			!SHR_TaskIDPending(NPCS.NPC, TID_MOVE_NAV))
		{
			NPC_ClearGoal();
		}

		NPC_BSST_Attack();
		npc_check_speak(NPCS.NPC);
		return;
	}

	if (UpdateGoal())
	{
		if (!NPCS.NPC->enemy &&
			NPCS.NPC->client->leader &&
			NPCS.NPCInfo->goalEntity == NPCS.NPC->client->leader &&
			!SHR_TaskIDPending(NPCS.NPC, TID_MOVE_NAV))
		{
			NPC_BSFollowLeader();
		}
		else
		{
			// FACE DIRECTION LOGIC — now NULL‑safe
			if (NPCS.NPCInfo->scriptFlags & SCF_FACE_MOVE_DIR ||
				(NPCS.NPCInfo->goalEntity && NPCS.NPCInfo->goalEntity != NPCS.NPC->enemy))
			{
				NPCS.NPCInfo->combatMove = qfalse;
			}
			else if (NPCS.NPCInfo->goalEntity)   // NULL‑check added here
			{
				vec3_t dir, angles;

				NPCS.NPCInfo->combatMove = qfalse;

				VectorSubtract(NPCS.NPCInfo->goalEntity->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin, dir);
				vectoangles(dir, angles);

				NPCS.NPCInfo->desiredYaw = angles[YAW];

				if (NPCS.NPCInfo->goalEntity == NPCS.NPC->enemy)
				{
					NPCS.NPCInfo->desiredPitch = angles[PITCH];
				}
			}

			// movement flags
			if (NPCS.NPCInfo->scriptFlags & SCF_RUNNING)
			{
				NPCS.ucmd.buttons &= ~BUTTON_WALKING;
			}
			else if (NPCS.NPCInfo->scriptFlags & SCF_WALKING)
			{
				NPCS.ucmd.buttons |= BUTTON_WALKING;
			}
			else if (NPCS.NPCInfo->goalEntity == NPCS.NPC->enemy)
			{
				NPCS.ucmd.buttons &= ~BUTTON_WALKING;
			}
			else
			{
				NPCS.ucmd.buttons |= BUTTON_WALKING;
			}

			if (NPCS.NPCInfo->scriptFlags & SCF_FORCED_MARCH)
			{
				if (g_crosshairEntNum != NPCS.NPC->s.number)
				{
					move = qfalse;
				}
			}

			if (move)
			{
				NPC_MoveToGoal(qtrue);
			}
		}
	}
	else if (!NPCS.NPC->enemy && NPCS.NPC->client->leader)
	{
		NPC_BSFollowLeader();
	}

	NPC_UpdateAngles(qtrue, qtrue);
}