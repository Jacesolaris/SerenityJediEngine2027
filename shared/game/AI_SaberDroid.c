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
#ifdef SP_GAME
extern void WP_DeactivateSaber(const gentity_t* self, qboolean clear_length);
#endif

#ifndef SP_GAME // file-level declarations of the MP versions below
extern void WP_ActivateSaber(gentity_t* self);
#endif


static qboolean enemyLOS;
static qboolean enemyCS;
static qboolean faceEnemy;
static qboolean doMove;
static qboolean shoot;
static float enemyDist;
extern void NPC_AngerSound(void);

static void SaberDroid_HoldPosition(void)
{
	NPC_FreeCombatPoint(NPCS.NPCInfo->combatPoint, qtrue);
	NPCS.NPCInfo->goalEntity = NULL;
}

/*
-------------------------
ST_Move
-------------------------
*/

static qboolean SaberDroid_Move(void)
{
	NPCS.NPCInfo->combatMove = qtrue;
	UpdateGoal();
	if (!NPCS.NPCInfo->goalEntity)
	{
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
	}
	NPCS.NPCInfo->goalRadius = 30.0f;

	const qboolean moved = NPC_MoveToGoal(qtrue);
	navInfo_t info SHR_NAVINFO_INIT;

	//Get the doMove info
	SHR_NAV_GetLastMove(info);

	//FIXME: if we bump into another one of our guys and can't get around him, just stop!
	//If we hit our target, then stop and fire!
	if (info.flags & NIF_COLLISION)
	{
		if (info.blocker == NPCS.NPC->enemy)
		{
			SaberDroid_HoldPosition();
		}
	}

	//If our doMove failed, then reset
	if (moved == qfalse)
	{
		//couldn't get to enemy
		//just hang here
		SaberDroid_HoldPosition();
	}

	return moved;
}

/*
-------------------------
NPC_BSSaberDroid_Patrol
-------------------------
*/

static void NPC_BSSaberDroid_Patrol(void)
{
	//FIXME: pick up on bodies of dead buddies?
	if (NPCS.NPCInfo->confusionTime < level.time && SHR_InsanityTime(NPCS.NPCInfo) < level.time)
	{
		//Look for any enemies
		if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
		{
			if (NPC_CheckPlayerTeamStealth())
			{
				NPC_AngerSound();
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
		}

		if (!(NPCS.NPCInfo->scriptFlags & SCF_IGNORE_ALERTS))
		{
			//Is there danger nearby
			const int alert_event = NPC_CheckAlertEvents(qtrue, qtrue, -1, qfalse, AEL_SUSPICIOUS);
			//There is an event to look at
			if (alert_event >= 0) //&& level.alertEvents[alert_event].ID != NPCInfo->lastAlertID )
			{
				//NPCInfo->lastAlertID = level.alertEvents[alert_event].ID;
				if (level.alertEvents[alert_event].level >= AEL_DISCOVERED)
				{
					if (level.alertEvents[alert_event].owner &&
						level.alertEvents[alert_event].owner->client &&
						level.alertEvents[alert_event].owner->health >= 0 &&
						level.alertEvents[alert_event].owner->client->playerTeam == NPCS.NPC->client->enemyTeam)
					{
						//an enemy
						G_SetEnemy(NPCS.NPC, level.alertEvents[alert_event].owner);
						//NPCInfo->enemyLastSeenTime = level.time;
						TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
					}
				}
				else
				{
					//FIXME: get more suspicious over time?
					//Save the position for movement (if necessary)
					VectorCopy(level.alertEvents[alert_event].position, NPCS.NPCInfo->investigateGoal);
					NPCS.NPCInfo->investigateDebounceTime = level.time + Q_irand(500, 1000);
					if (level.alertEvents[alert_event].level == AEL_SUSPICIOUS)
					{
						//suspicious looks longer
						NPCS.NPCInfo->investigateDebounceTime += Q_irand(500, 2500);
					}
				}
			}

			if (NPCS.NPCInfo->investigateDebounceTime > level.time)
			{
				//FIXME: walk over to it, maybe?  Not if not chase enemies
				//NOTE: stops walking or doing anything else below
				vec3_t dir = { 0 }, angles = { 0 };

				VectorSubtract(NPCS.NPCInfo->investigateGoal, NPCS.NPC->client->renderInfo.eyePoint, dir);
				vectoangles(dir, angles);

				const float o_yaw = NPCS.NPCInfo->desiredYaw;
				const float o_pitch = NPCS.NPCInfo->desiredPitch;
				NPCS.NPCInfo->desiredYaw = angles[YAW];
				NPCS.NPCInfo->desiredPitch = angles[PITCH];

				NPC_UpdateAngles(qtrue, qtrue);

				NPCS.NPCInfo->desiredYaw = o_yaw;
				NPCS.NPCInfo->desiredPitch = o_pitch;
				return;
			}
		}
	}

	//If we have somewhere to go, then do that
	if (UpdateGoal())
	{
		NPCS.ucmd.buttons |= BUTTON_WALKING;
		NPC_MoveToGoal(qtrue);
	}
	else if (!NPCS.NPC->client->ps.weaponTime
		&& TIMER_Done(NPCS.NPC, "attackDelay")
		&& TIMER_Done(NPCS.NPC, "inactiveDelay"))
	{
		if (SHR_SaberActive(&NPCS.NPC->client->ps))
		{
			SHR_DeactivateSaber(NPCS.NPC);
			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_TURNOFF, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		}
	}

	NPC_UpdateAngles(qtrue, qtrue);
}

int SaberDroid_PowerLevelForSaberAnim(const gentity_t* self)
{
	switch (self->client->ps.legsAnim)
	{
	case BOTH_A2_TR_BL:
	{
		if (self->client->ps.SHR_torsoAnimTimer <= 200)
		{
			//end of anim
			return FORCE_LEVEL_0;
		}
		if (SHR_AnimLength(self, self->client->ps.legsAnim) - self->client->ps.SHR_torsoAnimTimer <
			200)
		{
			//beginning of anim
			return FORCE_LEVEL_0;
		}
	}
	return FORCE_LEVEL_2;
	case BOTH_A1_BL_TR:
	{
		if (self->client->ps.SHR_torsoAnimTimer <= 300)
		{
			//end of anim
			return FORCE_LEVEL_0;
		}
		if (SHR_AnimLength(self, self->client->ps.legsAnim) - self->client->ps.SHR_torsoAnimTimer <
			200)
		{
			//beginning of anim
			return FORCE_LEVEL_0;
		}
	}
	return FORCE_LEVEL_1;
	case BOTH_A1__L__R:
	{
		if (self->client->ps.SHR_torsoAnimTimer <= 250)
		{
			//end of anim
			return FORCE_LEVEL_0;
		}
		if (SHR_AnimLength(self, self->client->ps.legsAnim) - self->client->ps.SHR_torsoAnimTimer <
			150)
		{
			//beginning of anim
			return FORCE_LEVEL_0;
		}
	}
	return FORCE_LEVEL_1;
	case BOTH_A3__L__R:
	{
		if (self->client->ps.SHR_torsoAnimTimer <= 200)
		{
			//end of anim
			return FORCE_LEVEL_0;
		}
		if (SHR_AnimLength(self, self->client->ps.legsAnim) - self->client->ps.SHR_torsoAnimTimer <
			300)
		{
			//beginning of anim
			return FORCE_LEVEL_0;
		}
	}
	return FORCE_LEVEL_3;
	default:;
	}
	return FORCE_LEVEL_0;
}

/*
-------------------------
NPC_BSSaberDroid_Attack
-------------------------
*/

#ifdef SP_GAME
static void NPC_SaberDroid_PickAttack(void)
{
	int attack_anim = Q_irand(0, 3);
	switch (attack_anim)
	{
	case 0:
	default:
		attack_anim = BOTH_A2_TR_BL;
		NPCS.NPC->client->ps.saberMove = LS_A_TR2BL;
		NPCS.NPC->client->ps.saberAnimLevel = SS_MEDIUM;
		break;
	case 1:
		attack_anim = BOTH_A1_BL_TR;
		NPCS.NPC->client->ps.saberMove = LS_A_BL2TR;
		NPCS.NPC->client->ps.saberAnimLevel = SS_FAST;
		break;
	case 2:
		attack_anim = BOTH_A1__L__R;
		NPCS.NPC->client->ps.saberMove = LS_A_L2R;
		NPCS.NPC->client->ps.saberAnimLevel = SS_FAST;
		break;
	case 3:
		attack_anim = BOTH_A3__L__R;
		NPCS.NPC->client->ps.saberMove = LS_A_L2R;
		NPCS.NPC->client->ps.saberAnimLevel = SS_STRONG;
		break;
	}
	NPCS.NPC->client->ps.saberBlocking = saberMoveData[NPCS.NPC->client->ps.saberMove].blocking;
	if (saberMoveData[NPCS.NPC->client->ps.saberMove].trailLength > 0)
	{
		NPCS.NPC->client->ps.SaberActivateTrail(saberMoveData[NPCS.NPC->client->ps.saberMove].trailLength);
	}
	else
	{
		NPCS.NPC->client->ps.SaberDeactivateTrail(0);
	}

	NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, attack_anim, SETANIM_FLAG_HOLD | SETANIM_FLAG_OVERRIDE);
	NPCS.NPC->client->ps.torsoAnim = NPCS.NPC->client->ps.legsAnim;
	//need to do this because we have no anim split but saber code checks torsoAnim
	NPCS.NPC->client->ps.weaponTime = NPCS.NPC->client->ps.SHR_torsoAnimTimer = NPCS.NPC->client->ps.SHR_legsAnimTimer;
	NPCS.NPC->client->ps.weaponstate = WEAPON_FIRING;
}
#else // MP keeps its own version of this function
void NPC_SaberDroid_PickAttack(void)
{
	int attackAnim = Q_irand(0, 3);
	switch (attackAnim)
	{
	case 0:
	default:
		attackAnim = BOTH_A2_TR_BL;
		NPCS.NPC->client->ps.saberMove = LS_A_TR2BL;
		NPCS.NPC->client->ps.fd.saberAnimLevel = SS_MEDIUM;
		break;
	case 1:
		attackAnim = BOTH_A1_BL_TR;
		NPCS.NPC->client->ps.saberMove = LS_A_BL2TR;
		NPCS.NPC->client->ps.fd.saberAnimLevel = SS_FAST;
		break;
	case 2:
		attackAnim = BOTH_A1__L__R;
		NPCS.NPC->client->ps.saberMove = LS_A_L2R;
		NPCS.NPC->client->ps.fd.saberAnimLevel = SS_FAST;
		break;
	case 3:
		attackAnim = BOTH_A3__L__R;
		NPCS.NPC->client->ps.saberMove = LS_A_L2R;
		NPCS.NPC->client->ps.fd.saberAnimLevel = SS_STRONG;
		break;
	}
	NPCS.NPC->client->ps.saberBlocking = saberMoveData[NPCS.NPC->client->ps.saberMove].blocking;
	NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, attackAnim, SETANIM_FLAG_HOLD | SETANIM_FLAG_OVERRIDE);
	NPCS.NPC->client->ps.torsoAnim = NPCS.NPC->client->ps.legsAnim;
	//need to do this because we have no anim split but saber code checks torsoAnim
	NPCS.NPC->client->ps.weaponTime = NPCS.NPC->client->ps.torsoTimer = NPCS.NPC->client->ps.legsTimer;
	NPCS.NPC->client->ps.weaponstate = WEAPON_FIRING;
}
#endif

static void NPC_BSSaberDroid_Attack(void)
{
	//Don't do anything if we're hurt
	if (NPCS.NPC->painDebounceTime > level.time)
	{
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	//NPC_CheckEnemy( qtrue, qfalse , qtrue);
	//If we don't have an enemy, just idle
	if (NPC_CheckEnemyExt(qfalse) == qfalse) //!NPC->enemy )//
	{
		NPCS.NPC->enemy = NULL;
		NPC_BSSaberDroid_Patrol(); //FIXME: or patrol?
		return;
	}

	if (!NPCS.NPC->enemy)
	{
		//WTF?  somehow we lost our enemy?
		NPC_BSSaberDroid_Patrol(); //FIXME: or patrol?
		return;
	}

	enemyLOS = enemyCS = qfalse;
	doMove = qtrue;
	faceEnemy = qfalse;
	shoot = qfalse;
	enemyDist = DistanceSquared(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin);

	//can we see our target?
	if (SHR_ClearLOSEnt(NPCS.NPC->enemy))
	{
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
		enemyLOS = qtrue;

		if (enemyDist <= 4096 && SHR_InFOVSpot(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin, NPCS.NPC->client->ps.viewangles, 90,
			45)) //within 64 & infront
		{
			VectorCopy(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPCInfo->enemyLastSeenLocation);
			enemyCS = qtrue;
		}
	}

	if (enemyLOS)
	{
		//FIXME: no need to face enemy if we're moving to some other goal and he's too far away to shoot?
		faceEnemy = qtrue;
	}

	if (!TIMER_Done(NPCS.NPC, "taunting"))
	{
		doMove = qfalse;
	}
	else if (enemyCS)
	{
		shoot = qtrue;
		if (enemyDist < (NPCS.NPC->SHR_maxs[0] + NPCS.NPC->enemy->SHR_maxs[0] + 32) * (NPCS.NPC->SHR_maxs[0] + NPCS.NPC->enemy->SHR_maxs[0] + 32))
		{
			//close enough
			doMove = qfalse;
		}
	} //this should make him chase enemy when out of range...?

	if (NPCS.NPC->client->ps.SHR_legsAnimTimer
		&& NPCS.NPC->client->ps.legsAnim != BOTH_A3__L__R) //this one is a running attack
	{
		//in the middle of a held, stationary anim, can't doMove
		doMove = qfalse;
	}

	if (doMove)
	{
		//doMove toward goal
		doMove = SaberDroid_Move();
		if (doMove)
		{
			//if we had to chase him, be sure to attack as soon as possible
			TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.weaponTime);
		}
	}

	if (!faceEnemy)
	{
		//we want to face in the dir we're running
		if (doMove)
		{
			//don't run away and shoot
			NPCS.NPCInfo->desiredYaw = NPCS.NPCInfo->lastPathAngles[YAW];
			NPCS.NPCInfo->desiredPitch = 0;
			shoot = qfalse;
		}
		NPC_UpdateAngles(qtrue, qtrue);
	}
	else // if ( faceEnemy )
	{
		//face the enemy
		NPC_FaceEnemy(qtrue);
	}

	if (NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE)
	{
		shoot = qfalse;
	}

	//FIXME: need predicted blocking?
	//FIXME: don't shoot right away!
	if (shoot)
	{
		//try to shoot if it's time
		if (TIMER_Done(NPCS.NPC, "attackDelay"))
		{
			if (!(NPCS.NPCInfo->scriptFlags & SCF_FIRE_WEAPON)) // we've already fired, no need to do it again here
			{
				NPC_SaberDroid_PickAttack();
				if (NPCS.NPCInfo->rank > RANK_CREWMAN)
				{
					TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.weaponTime + Q_irand(0, 1000));
				}
				else
				{
					TIMER_Set(NPCS.NPC, "attackDelay",
						NPCS.NPC->client->ps.weaponTime + Q_irand(0, 1000) + Q_irand(0, (3 - SHR_SPSKILL) * 2) *
						500);
				}
			}
		}
	}
}

#ifdef SP_GAME
void NPC_BSSD_Default(void)
{
	if (!NPCS.NPC->enemy)
	{
		//don't have an enemy, look for one
		NPC_BSSaberDroid_Patrol();
	}
	else //if ( NPC->enemy )
	{
		//have an enemy
		if (!SHR_SaberActive(&NPCS.NPC->client->ps))
		{
			NPCS.NPC->client->ps.SaberActivate();
			if (NPCS.NPC->client->ps.legsAnim == BOTH_TURNOFF
				|| NPCS.NPC->client->ps.legsAnim == BOTH_STAND1)
			{
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_TURNON, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			}
		}

		NPC_BSSaberDroid_Attack();
		TIMER_Set(NPCS.NPC, "inactiveDelay", Q_irand(2000, 4000));
	}
	if (!NPCS.NPC->client->ps.weaponTime)
	{
		NPCS.NPC->client->ps.saberMove = LS_READY;
		NPCS.NPC->client->ps.saberBlocking = saberMoveData[LS_READY].blocking;
		NPCS.NPC->client->ps.SaberDeactivateTrail(0);
		NPCS.NPC->client->ps.saberAnimLevel = SS_MEDIUM;
		NPCS.NPC->client->ps.weaponstate = WEAPON_READY;
	}
}
#else // MP keeps its own version of this function
void NPC_BSSD_Default(void)
{
	if (!NPCS.NPC->enemy)
	{
		//don't have an enemy, look for one
		NPC_BSSaberDroid_Patrol();
	}
	else //if ( NPC->enemy )
	{
		//have an enemy
		if (NPCS.NPC->client->ps.saberHolstered == 2)
		{
			//turn saber on
			WP_ActivateSaber(NPCS.NPC);
			if (NPCS.NPC->client->ps.legsAnim == BOTH_TURNOFF
				|| NPCS.NPC->client->ps.legsAnim == BOTH_STAND1)
			{
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_TURNON, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			}
		}

		NPC_BSSaberDroid_Attack();
		TIMER_Set(NPCS.NPC, "inactiveDelay", Q_irand(2000, 4000));
	}
	if (!NPCS.NPC->client->ps.weaponTime)
	{
		//we're not attacking.
		NPCS.NPC->client->ps.saberMove = LS_READY;
		NPCS.NPC->client->ps.saberBlocking = saberMoveData[LS_READY].blocking;
		//RAFIXME - since this is saber trail code, I think this needs to be ported to cgame.
		NPCS.NPC->client->ps.fd.saberAnimLevel = SS_MEDIUM;
		NPCS.NPC->client->ps.weaponstate = WEAPON_READY;
	}
}
#endif