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

#include "b_local.h"
#include "g_nav.h"

// These define the working combat range for these suckers
#define MIN_DISTANCE		48
#define MIN_DISTANCE_SQR	( MIN_DISTANCE * MIN_DISTANCE )

#define MAX_DISTANCE		1024
#define MAX_DISTANCE_SQR	( MAX_DISTANCE * MAX_DISTANCE )

#define LSTATE_CLEAR		0
#define LSTATE_WAITING		1

float enemyDist = 0;
void Wampa_Move(const qboolean visible);
void Wampa_Attack(const float distance, const qboolean do_charge);

void Wampa_SetBolts(gentity_t* self)
{
	if (self && self->client)
	{
		renderInfo_t* ri = &self->client->renderInfo;
		ri->headBolt = trap->G2API_AddBolt(self->ghoul2, 0, "*head_eyes");
		ri->torsoBolt = trap->G2API_AddBolt(self->ghoul2, 0, "lower_spine");
		ri->crotchBolt = trap->G2API_AddBolt(self->ghoul2, 0, "rear_bone");
		ri->handLBolt = trap->G2API_AddBolt(self->ghoul2, 0, "*l_hand");
		ri->handRBolt = trap->G2API_AddBolt(self->ghoul2, 0, "*r_hand");
		ri->footLBolt = trap->G2API_AddBolt(self->ghoul2, 0, "*l_leg_foot");
		ri->footRBolt = trap->G2API_AddBolt(self->ghoul2, 0, "*r_leg_foot");
	}
}

/*
-------------------------
NPC_Wampa_Precache
-------------------------
*/
void NPC_Wampa_Precache(void)
{
	for (int i = 1; i < 3; i++)
	{
		G_SoundIndex(va("sound/chars/wampa/snort%d.wav", i));
	}
	G_SoundIndex("sound/chars/rancor/swipehit.wav");
}

/*
-------------------------
Wampa_Idle
-------------------------
*/
void Wampa_Idle(void)
{
	NPCS.NPCInfo->localState = LSTATE_CLEAR;

	//If we have somewhere to go, then do that
	if (UpdateGoal())
	{
		NPCS.ucmd.buttons &= ~BUTTON_WALKING;
		NPC_MoveToGoal(qtrue);
	}
}

qboolean Wampa_CheckRoar(gentity_t* self)
{
	if (self->wait < level.time)
	{
		self->wait = level.time + Q_irand(5000, 20000);
		NPC_SetAnim(self, SETANIM_BOTH, Q_irand(BOTH_GESTURE1, BOTH_GESTURE2), SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		TIMER_Set(self, "rageTime", self->client->ps.legsTimer);
		G_Sound(self, CHAN_VOICE, G_SoundIndex(va("sound/chars/howler/howl.mp3")));
		return qtrue;
	}
	return qfalse;
}

/*
-------------------------
Wampa_Patrol
-------------------------
*/
void Wampa_Patrol(void)
{
	gentity_t* closest_player = FindClosestPlayer(NPCS.NPC->r.currentOrigin, NPCS.NPC->client->enemyTeam);
	vec3_t dif;

	NPCS.NPCInfo->localState = LSTATE_CLEAR;

	//If we have somewhere to go, then do that
	if (UpdateGoal())
	{
		Wampa_Move(qtrue);
	}
	else
	{
		if (TIMER_Done(NPCS.NPC, "patrolTime"))
		{
			TIMER_Set(NPCS.NPC, "patrolTime", flrand(-1.0f, 1.0f) * 5000 + 5000);
		}
	}
	VectorSubtract(g_entities[0].r.currentOrigin, NPCS.NPC->r.currentOrigin, dif);

	if (VectorLengthSquared(dif) < 256 * 256)
	{
		G_SetEnemy(NPCS.NPC, &g_entities[0]);
	}

	if (closest_player)
	{
		//attack enemy players that are close.
		if (Distance(closest_player->r.currentOrigin, NPCS.NPC->r.currentOrigin) < 256 * 256)
		{
			G_SetEnemy(NPCS.NPC, closest_player);
		}
	}

	if (NPC_CheckEnemyExt(qtrue) == qfalse)
	{
		Wampa_Idle();
		return;
	}

	Wampa_Attack(0, qtrue);

	TIMER_Set(NPCS.NPC, "lookForNewEnemy", Q_irand(5000, 15000));
}

/*
-------------------------
Wampa_Move
-------------------------
*/
void Wampa_Move(const qboolean visible)
{
	if (NPCS.NPCInfo->localState != LSTATE_WAITING)
	{
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;

		if (NPCS.NPC->enemy)
		{
			//pick correct movement speed and anim
			//run by default
			NPCS.ucmd.buttons &= ~BUTTON_WALKING;
			if (!TIMER_Done(NPCS.NPC, "runfar")
				|| !TIMER_Done(NPCS.NPC, "runclose"))
			{
				//keep running with this anim & speed for a bit
			}
			else if (!TIMER_Done(NPCS.NPC, "walk"))
			{
				//keep walking for a bit
				NPCS.ucmd.buttons |= BUTTON_WALKING;
			}
			else if (visible && enemyDist > 384 && NPCS.NPCInfo->stats.runSpeed == 180)
			{
				//fast run, all fours
				NPCS.NPCInfo->stats.runSpeed = 300;
				TIMER_Set(NPCS.NPC, "runfar", Q_irand(2000, 4000));
			}
			else if (enemyDist > 256 && NPCS.NPCInfo->stats.runSpeed == 300)
			{
				//slow run, upright
				NPCS.NPCInfo->stats.runSpeed = 180;
				TIMER_Set(NPCS.NPC, "runclose", Q_irand(3000, 5000));
			}
			else if (enemyDist < 128)
			{
				//walk
				NPCS.NPCInfo->stats.runSpeed = 180;
				NPCS.ucmd.buttons |= BUTTON_WALKING;
				TIMER_Set(NPCS.NPC, "walk", Q_irand(4000, 6000));
			}
		}

		if (NPCS.NPCInfo->stats.runSpeed == 300)
		{
			//need to use the alternate run - hunched over on all fours
			NPCS.NPC->client->ps.eFlags2 |= EF2_USE_ALT_ANIM;
		}
		NPC_MoveToGoal(qtrue);
		NPCS.NPCInfo->goalRadius = MAX_DISTANCE; // just get us within combat range
	}
}

//---------------------------------------------------------
extern void G_Knockdown(gentity_t* self, gentity_t* attacker, const vec3_t push_dir, float strength, const qboolean breakSaberLock);
extern void G_Dismember(const gentity_t* ent, const gentity_t* enemy, vec3_t point, int limb_type);
extern int NPC_GetEntsNearBolt(int* radius_ents, float radius, int boltIndex, vec3_t bolt_org);

void Wampa_Slash(const int boltIndex, const qboolean backhand)
{
	int radius_ent_nums[128];
	const float radius = 88;
	const float radiusSquared = radius * radius;
	vec3_t bolt_org;
	const int damage = backhand ? Q_irand(10, 15) : Q_irand(20, 30);

	const int num_ents = NPC_GetEntsNearBolt(radius_ent_nums, radius, boltIndex, bolt_org);

	for (int i = 0; i < num_ents; i++)
	{
		gentity_t* radius_ent = &g_entities[radius_ent_nums[i]];
		if (!radius_ent->inuse)
		{
			continue;
		}

		if (radius_ent == NPCS.NPC)
		{
			//Skip the wampa ent
			continue;
		}

		if (radius_ent->client == NULL)
		{
			//must be a client
			continue;
		}

		if (DistanceSquared(radius_ent->r.currentOrigin, bolt_org) <= radiusSquared)
		{
			//smack
			G_Damage(radius_ent, NPCS.NPC, NPCS.NPC, vec3_origin, radius_ent->r.currentOrigin, damage,
				backhand ? DAMAGE_NO_ARMOR : DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_MELEE);
			if (backhand)
			{
				//actually push the enemy
				vec3_t push_dir;
				vec3_t angs;
				VectorCopy(NPCS.NPC->client->ps.viewangles, angs);
				angs[YAW] += flrand(25, 50);
				angs[PITCH] = flrand(-25, -15);
				AngleVectors(angs, push_dir, NULL, NULL);
				if (radius_ent->client->NPC_class != CLASS_WAMPA
					&& radius_ent->client->NPC_class != CLASS_RANCOR
					&& radius_ent->client->NPC_class != CLASS_ATST
					&& !(radius_ent->flags & FL_NO_KNOCKBACK))
				{
					G_Throw(radius_ent, push_dir, 65);
					if (BG_KnockDownable(&radius_ent->client->ps) &&
						radius_ent->health > 0 && Q_irand(0, 1))
					{
						//do pain on enemy (SP knockdown and getup, the same as SP)
						G_Knockdown(radius_ent, NPCS.NPC, push_dir, 300, qtrue);
					}
				}
			}
			else if (radius_ent->health <= 0 && radius_ent->client)
			{
				//killed them, chance of dismembering
				if (!Q_irand(0, 1))
				{
					//bite something off
					const int hit_loc = Q_irand(G2_MODELPART_HEAD, G2_MODELPART_RLEG);
					if (hit_loc == G2_MODELPART_HEAD)
					{
						NPC_SetAnim(radius_ent, SETANIM_BOTH, BOTH_DEATH17, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					}
					else if (hit_loc == G2_MODELPART_WAIST)
					{
						NPC_SetAnim(radius_ent, SETANIM_BOTH, BOTH_DEATHBACKWARD2,
							SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					}
					G_Dismember(radius_ent, NPCS.NPC, radius_ent->r.currentOrigin, hit_loc);
				}
			}
			else if (!Q_irand(0, 3) && radius_ent->health > 0)
			{
				//one out of every 4 normal hits does a knockdown, too
				vec3_t push_dir;
				vec3_t angs;
				VectorCopy(NPCS.NPC->client->ps.viewangles, angs);
				angs[YAW] += flrand(25, 50);
				angs[PITCH] = flrand(-25, -15);
				AngleVectors(angs, push_dir, NULL, NULL);
				G_Knockdown(radius_ent, NPCS.NPC, push_dir, 35, qtrue);
			}
			G_Sound(radius_ent, CHAN_WEAPON, G_SoundIndex("sound/chars/rancor/swipehit.wav"));
		}
	}
}

//------------------------------
void Wampa_Attack(const float distance, const qboolean do_charge)
{
	if (!TIMER_Exists(NPCS.NPC, "attacking"))
	{
		if (Q_irand(0, 2) && !do_charge)
		{
			//double slash
			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ATTACK1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			TIMER_Set(NPCS.NPC, "attack_dmg", 750);
		}
		else if (do_charge || distance > 270 && distance < 430 && !Q_irand(0, 1))
		{
			//leap
			vec3_t fwd, yawAng;
			VectorSet(yawAng, 0, NPCS.NPC->client->ps.viewangles[YAW], 0);
			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ATTACK2, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			TIMER_Set(NPCS.NPC, "attack_dmg", 500);
			AngleVectors(yawAng, fwd, NULL, NULL);
			VectorScale(fwd, distance * 1.5f, NPCS.NPC->client->ps.velocity);
			NPCS.NPC->client->ps.velocity[2] = 150;
			NPCS.NPC->client->ps.groundEntityNum = ENTITYNUM_NONE;
		}
		else
		{
			//backhand
			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ATTACK3, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			TIMER_Set(NPCS.NPC, "attack_dmg", 250);
		}

		TIMER_Set(NPCS.NPC, "attacking", NPCS.NPC->client->ps.legsTimer + Q_flrand(0.0f, 1.0f) * 200);
		//allow us to re-evaluate our running speed/anim
		TIMER_Set(NPCS.NPC, "runfar", -1);
		TIMER_Set(NPCS.NPC, "runclose", -1);
		TIMER_Set(NPCS.NPC, "walk", -1);
	}

	// Need to do delayed damage since the attack animations encapsulate multiple mini-attacks

	if (TIMER_Done2(NPCS.NPC, "attack_dmg", qtrue))
	{
		switch (NPCS.NPC->client->ps.legsAnim)
		{
		case BOTH_ATTACK1:
			Wampa_Slash(NPCS.NPC->client->renderInfo.handRBolt, qfalse);
			//do second hit
			TIMER_Set(NPCS.NPC, "attack_dmg2", 100);
			break;
		case BOTH_ATTACK2:
			Wampa_Slash(NPCS.NPC->client->renderInfo.handRBolt, qfalse);
			TIMER_Set(NPCS.NPC, "attack_dmg2", 100);
			break;
		case BOTH_ATTACK3:
			Wampa_Slash(NPCS.NPC->client->renderInfo.handLBolt, qtrue);
			break;
		default:;
		}
	}
	else if (TIMER_Done2(NPCS.NPC, "attack_dmg2", qtrue))
	{
		switch (NPCS.NPC->client->ps.legsAnim)
		{
		case BOTH_ATTACK1:
			Wampa_Slash(NPCS.NPC->client->renderInfo.handLBolt, qfalse);
			break;
		case BOTH_ATTACK2:
			Wampa_Slash(NPCS.NPC->client->renderInfo.handLBolt, qfalse);
			break;
		default:;
		}
	}

	// Just using this to remove the attacking flag at the right time
	TIMER_Done2(NPCS.NPC, "attacking", qtrue);

	if (NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK1 && distance > NPCS.NPC->r.maxs[0] + MIN_DISTANCE)
	{
		//okay to keep moving
		NPCS.ucmd.buttons |= BUTTON_WALKING;
		Wampa_Move(qtrue);
	}
}

//----------------------------------
void Wampa_Combat(void)
{
	// If we cannot see our target or we have somewhere to go, then do that
	if (!NPC_ClearLOS(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin))
	{
		if (!Q_irand(0, 10))
		{
			if (Wampa_CheckRoar(NPCS.NPC))
			{
				return;
			}
		}
		NPCS.NPCInfo->combatMove = qtrue;
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
		NPCS.NPCInfo->goalRadius = MAX_DISTANCE; // just get us within combat range

		Wampa_Move(qfalse);
		return;
	}
	if (UpdateGoal())
	{
		NPCS.NPCInfo->combatMove = qtrue;
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
		NPCS.NPCInfo->goalRadius = MAX_DISTANCE; // just get us within combat range

		Wampa_Move(qtrue);
		return;
	}
	const float distance = enemyDist = Distance(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin);
	qboolean advance = distance > NPCS.NPC->r.maxs[0] + MIN_DISTANCE ? qtrue : qfalse;
	qboolean doCharge = qfalse;

	// Sometimes I have problems with facing the enemy I'm attacking, so force the issue so I don't look dumb
	//FIXME: always seems to face off to the left or right?!!!!
	NPC_FaceEnemy(qtrue);

	if (advance)
	{
		//have to get closer
		vec3_t yawOnlyAngles;
		VectorSet(yawOnlyAngles, 0, NPCS.NPC->r.currentAngles[YAW], 0);
		if (NPCS.NPC->enemy->health > 0 //enemy still alive
			&& fabs(distance - 350) <= 80 //enemy anywhere from 270 to 430 away
			&& InFOV3(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, yawOnlyAngles, 20, 20))
			//enemy generally in front
		{
			//10% chance of doing charge anim
			if (!Q_irand(0, 9))
			{
				//go for the charge
				doCharge = qtrue;
				advance = qfalse;
			}
		}
	}

	if ((advance || NPCS.NPCInfo->localState == LSTATE_WAITING) && TIMER_Done(NPCS.NPC, "attacking"))
		// waiting monsters can't attack
	{
		if (TIMER_Done2(NPCS.NPC, "takingPain", qtrue))
		{
			NPCS.NPCInfo->localState = LSTATE_CLEAR;
		}
		else
		{
			Wampa_Move(qtrue);
		}
	}
	else
	{
		if (!Q_irand(0, 20))
		{
			//FIXME: only do this if we just damaged them or vice-versa?
			if (Wampa_CheckRoar(NPCS.NPC))
			{
				return;
			}
		}
		if (!Q_irand(0, 1))
		{
			//FIXME: base on skill
			Wampa_Attack(distance, doCharge);
		}
	}
}

/*
-------------------------
NPC_Wampa_Pain
-------------------------
*/
void NPC_Wampa_Pain(gentity_t* self, gentity_t* attacker, const int damage)
{
	qboolean hitByWampa = qfalse;
	if (attacker && attacker->client && attacker->client->NPC_class == CLASS_WAMPA)
	{
		hitByWampa = qtrue;
	}
	if (attacker
		&& attacker->inuse
		&& attacker != self->enemy
		&& !(attacker->flags & FL_NOTARGET))
	{
		if (!attacker->s.number && !Q_irand(0, 3)
			|| !self->enemy
			|| self->enemy->health == 0
			|| self->enemy->client && self->enemy->client->NPC_class == CLASS_WAMPA
			|| !Q_irand(0, 4) && DistanceSquared(attacker->r.currentOrigin, self->r.currentOrigin) < DistanceSquared(
				self->enemy->r.currentOrigin, self->r.currentOrigin))
		{
			//if my enemy is dead (or attacked by player) and I'm not still holding/eating someone, turn on the attacker
			//FIXME: if can't nav to my enemy, take this guy if I can nav to him
			G_SetEnemy(self, attacker);
			TIMER_Set(self, "lookForNewEnemy", Q_irand(5000, 15000));
			if (hitByWampa)
			{
				//stay mad at this Wampa for 2-5 secs before looking for attacker enemies
				TIMER_Set(self, "wampaInfight", Q_irand(2000, 5000));
			}
		}
	}
	if ((hitByWampa || Q_irand(0, 100) < damage) //hit by wampa, hit while holding live victim, or took a lot of damage
		&& self->client->ps.legsAnim != BOTH_GESTURE1
		&& self->client->ps.legsAnim != BOTH_GESTURE2
		&& TIMER_Done(self, "takingPain"))
	{
		if (!Wampa_CheckRoar(self))
		{
			if (self->client->ps.legsAnim != BOTH_ATTACK1
				&& self->client->ps.legsAnim != BOTH_ATTACK2
				&& self->client->ps.legsAnim != BOTH_ATTACK3)
			{
				//cant interrupt one of the big attack anims
				if (self->health > 100 || hitByWampa)
				{
					TIMER_Remove(self, "attacking");

					VectorCopy(self->NPC->lastPathAngles, self->s.angles);

					if (!Q_irand(0, 1))
					{
						NPC_SetAnim(self, SETANIM_BOTH, BOTH_PAIN2, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					}
					else
					{
						NPC_SetAnim(self, SETANIM_BOTH, BOTH_PAIN1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					}
					TIMER_Set(self, "takingPain", self->client->ps.legsTimer + Q_irand(0, 500));
					//allow us to re-evaluate our running speed/anim
					TIMER_Set(self, "runfar", -1);
					TIMER_Set(self, "runclose", -1);
					TIMER_Set(self, "walk", -1);

					if (self->NPC)
					{
						self->NPC->localState = LSTATE_WAITING;
					}
				}
			}
		}
	}
}

/*
-------------------------
NPC_BSWampa_Default
-------------------------
*/
void NPC_BSWampa_Default(void)
{
	NPCS.NPC->client->ps.eFlags2 &= ~EF2_USE_ALT_ANIM;
	//NORMAL ANIMS
	//	stand1 = normal stand
	//	walk1 = normal, non-angry walk
	//	walk2 = injured
	//	run1 = far away run
	//	run2 = close run
	//VICTIM ANIMS
	//	grabswipe = melee1 - sweep out and grab
	//	stand2 attack = attack4 - while holding victim, swipe at him
	//	walk3_drag = walk5 - walk with drag
	//	stand2 = hold victim
	//	stand2to1 = drop victim
	if (!TIMER_Done(NPCS.NPC, "rageTime"))
	{
		//do nothing but roar first time we see an enemy
		NPC_FaceEnemy(qtrue);
		return;
	}
	if (NPCS.NPC->enemy)
	{
		if (!TIMER_Done(NPCS.NPC, "attacking"))
		{
			//in middle of attack
			//face enemy
			NPC_FaceEnemy(qtrue);
			//continue attack logic
			enemyDist = Distance(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin);
			Wampa_Attack(enemyDist, qfalse);
			return;
		}
		if (TIMER_Done(NPCS.NPC, "angrynoise"))
		{
			G_Sound(NPCS.NPC, CHAN_VOICE, G_SoundIndex(va("sound/chars/wampa/misc/anger%d.wav", Q_irand(1, 2))));

			TIMER_Set(NPCS.NPC, "angrynoise", Q_irand(5000, 10000));
		}
		//else, if he's in our hand, we eat, else if he's on the ground, we keep attacking his dead body for a while
		if (NPCS.NPC->enemy->client && NPCS.NPC->enemy->client->NPC_class == CLASS_WAMPA)
		{
			//got mad at another Wampa, look for a valid enemy
			if (TIMER_Done(NPCS.NPC, "wampaInfight"))
			{
				NPC_CheckEnemyExt(qtrue);
			}
		}
		else
		{
			if (ValidEnemy(NPCS.NPC->enemy) == qfalse)
			{
				TIMER_Remove(NPCS.NPC, "lookForNewEnemy"); //make them look again right now
				if (!NPCS.NPC->enemy->inuse || level.time - NPCS.NPC->enemy->s.time > Q_irand(10000, 15000))
				{
					//it's been a while since the enemy died, or enemy is completely gone, get bored with him
					NPCS.NPC->enemy = NULL;
					Wampa_Patrol();
					NPC_UpdateAngles(qtrue, qtrue);
					//just lost my enemy
					if (NPCS.NPC->spawnflags & 2)
					{
						//search around me if I don't have an enemy
						NPC_BSSearchStart(NPCS.NPC->waypoint, BS_SEARCH);
						NPCS.NPCInfo->tempBehavior = BS_DEFAULT;
					}
					else if (NPCS.NPC->spawnflags & 1)
					{
						//wander if I don't have an enemy
						NPC_BSSearchStart(NPCS.NPC->waypoint, BS_WANDER);
						NPCS.NPCInfo->tempBehavior = BS_DEFAULT;
					}
					return;
				}
			}
			if (TIMER_Done(NPCS.NPC, "lookForNewEnemy"))
			{
				gentity_t* sav_enemy = NPCS.NPC->enemy; //FIXME: what about NPC->lastEnemy?
				NPCS.NPC->enemy = NULL;
				gentity_t* newEnemy = NPC_CheckEnemy(NPCS.NPCInfo->confusionTime < level.time, qfalse, qfalse);
				NPCS.NPC->enemy = sav_enemy;
				if (newEnemy && newEnemy != sav_enemy)
				{
					//picked up a new enemy!
					NPCS.NPC->lastEnemy = NPCS.NPC->enemy;
					G_SetEnemy(NPCS.NPC, newEnemy);
					//hold this one for at least 5-15 seconds
					TIMER_Set(NPCS.NPC, "lookForNewEnemy", Q_irand(5000, 15000));
				}
				else
				{
					//look again in 2-5 secs
					TIMER_Set(NPCS.NPC, "lookForNewEnemy", Q_irand(2000, 5000));
				}
			}
		}
		Wampa_Combat();
		return;
	}
	if (TIMER_Done(NPCS.NPC, "idlenoise"))
	{
		G_Sound(NPCS.NPC, CHAN_AUTO, G_SoundIndex("sound/chars/wampa/misc/anger3.wav"));

		TIMER_Set(NPCS.NPC, "idlenoise", Q_irand(2000, 4000));
	}
	if (NPCS.NPC->spawnflags & 2)
	{
		//search around me if I don't have an enemy
		if (NPCS.NPCInfo->homeWp == WAYPOINT_NONE)
		{
			//no homewap, initialize the search behavior
			NPC_BSSearchStart(WAYPOINT_NONE, BS_SEARCH);
			NPCS.NPCInfo->tempBehavior = BS_DEFAULT;
		}
		NPCS.ucmd.buttons |= BUTTON_WALKING;
		NPC_BSSearch(); //this automatically looks for enemies
	}
	else if (NPCS.NPC->spawnflags & 1)
	{
		//wander if I don't have an enemy
		if (NPCS.NPCInfo->homeWp == WAYPOINT_NONE)
		{
			//no homewap, initialize the wander behavior
			NPC_BSSearchStart(WAYPOINT_NONE, BS_WANDER);
			NPCS.NPCInfo->tempBehavior = BS_DEFAULT;
		}
		NPCS.ucmd.buttons |= BUTTON_WALKING;
		NPC_BSWander();
		if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
		{
			if (NPC_CheckEnemyExt(qtrue) == qfalse)
			{
				Wampa_Idle();
			}
			else
			{
				Wampa_CheckRoar(NPCS.NPC);
				TIMER_Set(NPCS.NPC, "lookForNewEnemy", Q_irand(5000, 15000));
			}
		}
	}
	else
	{
		if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
		{
			Wampa_Patrol();
		}
		else
		{
			Wampa_Idle();
		}
	}

	NPC_UpdateAngles(qtrue, qtrue);
}

// ---------------------------------------------------------------------------------------------------------------
// DH 2026-10-26: wampa and rancor players and bots grab, hold and eat like the SP AI (AI_Wampa.cpp,
// AI_Rancor.cpp). The victim is held with EF2_HELD_BY_MONSTER + lookTarget, as by the MP rancor NPC, and
// G_HeldByMonster (g_active.c) puts him in the wampa's hand or the rancor's hand / mouth every frame.
//   Wampa: alt attack next to someone = grab (else the backhand). Holding: attack = maul him, alt = drop him.
//          Any hit on a wampa that holds someone makes it drop him (SP); a held saber user can slash at it.
//   Rancor: alt attack next to someone = grab swing (else the bite). Holding: attack = quick bite, alt = eat him
//          whole (he is cut in half, swallowed and gone). A dead victim in its hand is eaten.
// Bots do the same on their own. g_monsterGrab 0 = the old attacks only.
// ---------------------------------------------------------------------------------------------------------------
#define MONSTER_HOLD_HAND	1
#define MONSTER_HOLD_MOUTH	2

extern void G_GetBoltPosition(gentity_t* self, int boltIndex, vec3_t pos, int modelIndex);

int G_MonsterPlayerClass(const gentity_t* ent)
{
	if (!ent || !ent->inuse || !ent->client || ent->s.number >= MAX_CLIENTS || level.gametype == GT_SIEGE
		|| ent->client->sess.sessionTeam == TEAM_SPECTATOR || ent->client->tempSpectate >= level.time)
	{
		return BCLASS_NONE;
	}
	if (ent->client->pers.botclass != BCLASS_WAMPA && ent->client->pers.botclass != BCLASS_RANCOR)
	{
		return BCLASS_NONE;
	}
	if (BG_AnimSetIsHumanoidLike(ent->localAnimIndex))
	{
		return BCLASS_NONE;	// on the default model, not the creature's own skeleton
	}
	return ent->client->pers.botclass;
}

// the SP rule (Wampa_TryGrab / Rancor_Swing): no droids, machines, vehicles or other rancors
static qboolean G_MonsterCanGrab(const gentity_t* self, const gentity_t* targ)
{
	if (!targ || !targ->inuse || !targ->client || targ == self || targ->health <= 0 || !targ->takedamage)
	{
		return qfalse;
	}
	if (targ->client->ps.eFlags2 & EF2_HELD_BY_MONSTER || targ->client->ps.m_iVehicleNum
		|| targ->client->sess.sessionTeam == TEAM_SPECTATOR || targ->client->tempSpectate >= level.time
		|| targ->client->ps.eFlags & EF_NODRAW)
	{
		return qfalse;
	}
	if (OnSameTeam(self, targ))
	{
		return qfalse;
	}
	if (self->client->ps.duelInProgress && self->client->ps.duelIndex != targ->s.number
		|| targ->client->ps.duelInProgress && targ->client->ps.duelIndex != self->s.number)
	{
		return qfalse;
	}
	switch (targ->client->NPC_class)
	{
	case CLASS_RANCOR:
	case CLASS_GALAKMECH:
	case CLASS_ATST:
	case CLASS_GONK:
	case CLASS_R2D2:
	case CLASS_R5D2:
	case CLASS_MARK1:
	case CLASS_MARK2:
	case CLASS_MOUSE:
	case CLASS_PROBE:
	case CLASS_SEEKER:
	case CLASS_REMOTE:
	case CLASS_SENTRY:
	case CLASS_INTERROGATOR:
	case CLASS_DROIDEKA:
	case CLASS_SAND_CREATURE:
	case CLASS_VEHICLE:
		return qfalse;
	case CLASS_WAMPA:
		if (G_MonsterPlayerClass(self) == BCLASS_WAMPA)
		{
			return qfalse;	// a wampa can't hang from a wampa's hand
		}
		break;
	default:
		break;
	}
	if (targ->s.number < MAX_CLIENTS)
	{
		switch (targ->client->pers.botclass)
		{
		case BCLASS_RANCOR:
		case BCLASS_R2D2:
		case BCLASS_R5D2:
		case BCLASS_GONK:
		case BCLASS_MOUSE:
		case BCLASS_PROBE:
		case BCLASS_SEEKER:
		case BCLASS_SENTRY:
		case BCLASS_INTERROGATOR:
		case BCLASS_MARK1:
		case BCLASS_MARK2:
		case BCLASS_DROIDEKA:
		case BCLASS_GALAKMECH:
			return qfalse;
		case BCLASS_WAMPA:
			if (G_MonsterPlayerClass(self) == BCLASS_WAMPA)
			{
				return qfalse;
			}
			break;
		default:
			break;
		}
	}
	return qtrue;
}

static void G_MonsterBoltOrg(gentity_t* self, const char* boltName, vec3_t out)
{
	VectorCopy(self->r.currentOrigin, out);
	if (self->ghoul2)
	{
		const int bolt = trap->G2API_AddBolt(self->ghoul2, 0, boltName);
		if (bolt != -1)
		{
			G_GetBoltPosition(self, bolt, out, 0);
		}
	}
}

// the nearest one we may grab within radius of org (in front of us when frontOnly)
static gentity_t* G_MonsterFindVictim(gentity_t* self, const vec3_t org, const float radius, const qboolean frontOnly)
{
	int touch[128];
	vec3_t mins, maxs, fwd, yaw;
	gentity_t* best = NULL;
	float bestDist = radius;

	VectorSet(yaw, 0, self->client->ps.viewangles[YAW], 0);
	AngleVectors(yaw, fwd, NULL, NULL);
	for (int i = 0; i < 3; i++)
	{
		mins[i] = org[i] - radius;
		maxs[i] = org[i] + radius;
	}
	const int num = trap->EntitiesInBox(mins, maxs, touch, 128);
	for (int i = 0; i < num; i++)
	{
		gentity_t* ent = &g_entities[touch[i]];
		vec3_t dir;

		if (!G_MonsterCanGrab(self, ent))
		{
			continue;
		}
		const float dist = Distance(ent->r.currentOrigin, org);
		if (dist > bestDist)
		{
			continue;
		}
		if (frontOnly)
		{
			VectorSubtract(ent->r.currentOrigin, self->r.currentOrigin, dir);
			dir[2] = 0;
			VectorNormalize(dir);
			if (DotProduct(dir, fwd) < 0.3f)
			{
				continue;
			}
		}
		best = ent;
		bestDist = dist;
	}
	return best;
}

static gentity_t* G_MonsterVictim(const gentity_t* self)
{
	if (!self->client->monsterHold)
	{
		return NULL;
	}
	gentity_t* victim = &g_entities[self->client->monsterVictim];
	if (!victim->inuse || !victim->client || !(victim->client->ps.eFlags2 & EF2_HELD_BY_MONSTER)
		|| !victim->client->ps.hasLookTarget || victim->client->ps.lookTarget != self->s.number)
	{
		return NULL;
	}
	return victim;
}

// is targ in front of it, within reach (flat distance) and not far above / below it
static qboolean G_MonsterInReach(const gentity_t* self, const gentity_t* targ, const float reach)
{
	vec3_t dir, fwd, yaw;

	VectorSubtract(targ->r.currentOrigin, self->r.currentOrigin, dir);
	if (targ->r.currentOrigin[2] < self->r.absmin[2] - 16.0f || targ->r.currentOrigin[2] > self->r.absmax[2] + 32.0f)
	{
		return qfalse;
	}
	dir[2] = 0;
	if (VectorNormalize(dir) > reach)
	{
		return qfalse;
	}
	VectorSet(yaw, 0, self->client->ps.viewangles[YAW], 0);
	AngleVectors(yaw, fwd, NULL, NULL);
	return DotProduct(dir, fwd) >= 0.3f ? qtrue : qfalse;
}

static void G_MonsterTakeVictim(gentity_t* self, gentity_t* victim)
{
	victim->client->ps.eFlags2 |= EF2_HELD_BY_MONSTER;
	victim->client->ps.hasLookTarget = qtrue;
	victim->client->ps.lookTarget = self->s.number;
	victim->client->ps.forceHandExtend = HANDEXTEND_NONE;
	victim->client->ps.forceHandExtendTime = 0;
	victim->client->heldAttackTime = 0;
	victim->client->heldAttackDebounce = 0;
	self->client->monsterHold = MONSTER_HOLD_HAND;
	self->client->monsterVictim = victim->s.number;
	self->client->monsterSniffed = qfalse;
	self->client->monsterSniffDone = 0;
	self->client->monsterLastHealth = self->health;
	self->client->monsterBotTime = level.time + Q_irand(500, 1500);
	self->client->monsterBites = 0;
}

void G_MonsterReleaseVictim(gentity_t* self, const qboolean dropAnim)
{
	gclient_t* cl = self->client;

	if (!cl || !cl->monsterHold)
	{
		return;
	}
	gentity_t* victim = G_MonsterVictim(self);
	if (victim)
	{
		victim->client->ps.eFlags2 &= ~EF2_HELD_BY_MONSTER;
		victim->client->ps.hasLookTarget = qfalse;
		victim->client->ps.lookTarget = ENTITYNUM_NONE;
		victim->client->heldAttackTime = 0;
		victim->client->ps.viewangles[PITCH] = 0;
		victim->client->ps.viewangles[ROLL] = 0;
		victim->r.currentAngles[PITCH] = 0;
		victim->r.currentAngles[ROLL] = 0;
		G_SetAngles(victim, victim->r.currentAngles);

		if (cl->monsterHold == MONSTER_HOLD_MOUTH)
		{
			// eaten: nothing is left of him
			victim->client->ps.eFlags |= EF_NODRAW;
			victim->r.contents = 0;
			victim->client->noCorpse = qtrue;
		}
		else if (victim->health > 0)
		{
			if (victim->s.number < MAX_CLIENTS)
			{
				vec3_t vicAngles;
				VectorSet(vicAngles, 0, AngleNormalize180(cl->ps.viewangles[YAW] + 180.0f), 0);
				SetClientViewAngle(victim, vicAngles);
			}
			if (G_MonsterPlayerClass(self) == BCLASS_WAMPA)
			{
				NPC_SetAnim(victim, SETANIM_BOTH, BOTH_RELEASED, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				victim->client->ps.legsTimer += 500;
				victim->client->ps.weaponTime = victim->client->ps.torsoTimer = victim->client->ps.legsTimer;
			}
			else
			{
				victim->client->ps.legsTimer = 0;
				victim->client->ps.torsoTimer = 0;
			}
			if (victim->NPC)
			{
				victim->NPC->nextBStateThink = level.time;
			}
		}
		else
		{
			victim->client->ps.legsTimer = 0;
			victim->client->ps.torsoTimer = 0;
		}
	}
	if (dropAnim && self->health > 0 && G_MonsterPlayerClass(self) == BCLASS_WAMPA)
	{
		NPC_SetAnim(self, SETANIM_BOTH, BOTH_STAND2TO1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		cl->ps.weaponTime = cl->ps.legsTimer;
	}
	cl->monsterHold = 0;
	cl->monsterVictim = 0;
	cl->monsterDmgTime = 0;
	cl->monsterDmg2Time = 0;
	cl->monsterDropTime = 0;
	cl->monsterClearTime = 0;
	cl->ps.eFlags2 &= ~(EF2_USE_ALT_ANIM | EF2_GENERIC_NPC_FLAG);
}

static void G_MonsterStartAnim(gentity_t* self, const int anim, const int dmgDelay)
{
	NPC_SetAnim(self, SETANIM_BOTH, anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART);
	self->client->ps.weaponTime = self->client->ps.legsTimer;
	self->client->monsterDmgTime = dmgDelay > 0 ? level.time + dmgDelay : 0;
	self->client->monsterDmg2Time = 0;
}

static qboolean G_MonsterAnimDone(const gentity_t* self)
{
	return (self->client->ps.legsTimer <= 0 || self->client->ps.legsAnim == BOTH_HOLD_IDLE)
		&& self->client->ps.weaponTime <= 0 ? qtrue : qfalse;
}

// the wampa's grab / hold anims and the rancor's grab / eat anims: it stands still while they play (the SP AI does)
static qboolean G_MonsterStandStillAnim(const gentity_t* self, const int monster)
{
	const int anim = self->client->ps.legsAnim;

	if (self->client->ps.legsTimer <= 0)
	{
		return qfalse;
	}
	switch (anim)
	{
	case BOTH_HOLD_START:
	case BOTH_HOLD_END:
	case BOTH_HOLD_MISS:
	case BOTH_HOLD_SNIFF:
	case BOTH_HOLD_ATTACK:
	case BOTH_HOLD_DROP:
		return monster == BCLASS_WAMPA ? qtrue : qfalse;
	case BOTH_ATTACK2:
	case BOTH_ATTACK3:
	case BOTH_ATTACK10:
	case BOTH_ATTACK11:
		return monster == BCLASS_RANCOR ? qtrue : qfalse;
	case BOTH_ATTACK1:
		return monster == BCLASS_RANCOR && self->client->monsterHold ? qtrue : qfalse;
	default:
		return qfalse;
	}
}

// ------------------------------------ wampa ------------------------------------

// SP Wampa_TryGrab, at the end of BOTH_HOLD_START
static void G_WampaTryGrab(gentity_t* self)
{
	vec3_t hand;
	gentity_t* target = self->client->monsterTarget >= 0 && self->client->monsterTarget < ENTITYNUM_WORLD
		? &g_entities[self->client->monsterTarget] : NULL;

	G_MonsterBoltOrg(self, "*r_hand", hand);
	gentity_t* victim = G_MonsterFindVictim(self, hand, 64.0f, qfalse);
	if (victim)
	{
		G_MonsterTakeVictim(self, victim);
		NPC_SetAnim(victim, SETANIM_BOTH, BOTH_GRABBED, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		G_MonsterStartAnim(self, BOTH_HOLD_END, 0);
		self->client->ps.weaponTime = self->client->ps.legsTimer + Q_irand(500, 1500);
		return;
	}
	G_MonsterStartAnim(self, BOTH_HOLD_MISS, 0);
	if (target && G_MonsterCanGrab(self, target) && Distance(target->r.currentOrigin, hand) < 128.0f)
	{
		// smack
		vec3_t push_dir, angs;

		G_Sound(target, CHAN_AUTO, G_SoundIndex("sound/chars/rancor/swipehit.wav"));
		VectorCopy(self->client->ps.viewangles, angs);
		angs[YAW] += flrand(25, 50);
		angs[PITCH] = flrand(-25, -15);
		AngleVectors(angs, push_dir, NULL, NULL);
		if (!(target->flags & FL_NO_KNOCKBACK))
		{
			G_Throw(target, push_dir, Q_irand(30, 70));
			G_Knockdown(target, self, push_dir, 300, qtrue);
		}
	}
}

static void G_WampaThink(gentity_t* self, usercmd_t* ucmd, const qboolean isBot)
{
	gclient_t* cl = self->client;
	gentity_t* victim = G_MonsterVictim(self);
	qboolean wantAttack = ucmd->buttons & BUTTON_ATTACK ? qtrue : qfalse;
	qboolean wantAlt = ucmd->buttons & BUTTON_ALT_ATTACK ? qtrue : qfalse;

	if (cl->monsterHold && !victim)
	{
		G_MonsterReleaseVictim(self, qfalse);
	}

	if (cl->ps.legsAnim == BOTH_HOLD_START)
	{
		ucmd->buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK);
		if (cl->monsterGrabTime && level.time >= cl->monsterGrabTime)
		{
			cl->monsterGrabTime = 0;
			G_WampaTryGrab(self);
		}
		return;
	}
	cl->monsterGrabTime = 0;

	if (!cl->monsterHold)
	{
		vec3_t front, fwd, yaw;

		if (cl->ps.weaponTime > 0 || cl->ps.groundEntityNum == ENTITYNUM_NONE)
		{
			return;
		}
		VectorSet(yaw, 0, cl->ps.viewangles[YAW], 0);
		AngleVectors(yaw, fwd, NULL, NULL);
		VectorMA(self->r.currentOrigin, self->r.maxs[0] + 40.0f, fwd, front);
		gentity_t* target = G_MonsterFindVictim(self, front, 70.0f, qtrue);
		if (isBot && target && level.time >= cl->monsterBotTime)
		{
			// the SP AI grabs when close (Wampa_Attack), now and then
			cl->monsterBotTime = level.time + Q_irand(1000, 2500);
			wantAlt = Q_irand(0, 1) ? qtrue : qfalse;
		}
		if (wantAlt && target)
		{
			// grab
			ucmd->buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK);
			G_MonsterStartAnim(self, BOTH_HOLD_START, 0);
			cl->ps.legsTimer += 200;
			cl->ps.torsoTimer = cl->ps.weaponTime = cl->ps.legsTimer;
			cl->monsterGrabTime = level.time + cl->ps.legsTimer - 200;
			cl->monsterTarget = target->s.number;
		}
		return;
	}

	// holding someone
	ucmd->buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK);
	if (!victim)
	{
		return;
	}

	if (self->health < cl->monsterLastHealth)
	{
		// hit while holding: drop him (SP NPC_Wampa_Pain)
		G_MonsterReleaseVictim(self, qtrue);
		return;
	}
	cl->monsterLastHealth = self->health;

	if (cl->monsterDmgTime && level.time >= cl->monsterDmgTime && cl->ps.legsAnim == BOTH_HOLD_ATTACK)
	{
		cl->monsterDmgTime = 0;
		G_Sound(victim, CHAN_AUTO, G_SoundIndex("sound/chars/rancor/swipehit.wav"));
		G_Damage(victim, self, self, vec3_origin, victim->r.currentOrigin, Q_irand(25, 40),
			DAMAGE_NO_KNOCKBACK | DAMAGE_NO_ARMOR, MOD_MELEE);
		if (victim->health <= 0)
		{
			// killed him, rip something off
			G_Dismember(victim, self, victim->r.currentOrigin, Q_irand(G2_MODELPART_LARM, G2_MODELPART_RLEG));
			cl->monsterSniffDone = level.time + Q_irand(2000, 5000);
		}
		else
		{
			NPC_SetAnim(victim, SETANIM_BOTH, BOTH_HANG_PAIN, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		}
	}
	if (cl->monsterDropTime && level.time >= cl->monsterDropTime)
	{
		cl->monsterDropTime = 0;
		G_MonsterReleaseVictim(self, qfalse);
		return;
	}
	if (victim->health <= 0)
	{
		// sniff the corpse for a moment, then drop it
		if (!cl->monsterSniffDone)
		{
			cl->monsterSniffDone = level.time + Q_irand(2000, 5000);
		}
		if (level.time >= cl->monsterSniffDone && G_MonsterAnimDone(self))
		{
			G_MonsterStartAnim(self, BOTH_HOLD_DROP, 0);
			cl->monsterDropTime = level.time + 500;
		}
		return;
	}
	if (!G_MonsterAnimDone(self))
	{
		return;
	}
	if (isBot)
	{
		wantAttack = wantAlt = qfalse;
		if (level.time >= cl->monsterBotTime)
		{
			if (!cl->monsterSniffed)
			{
				// SP: the first thing it does is sniff him
				cl->monsterSniffed = qtrue;
				G_MonsterStartAnim(self, BOTH_HOLD_SNIFF, 0);
				cl->monsterBotTime = level.time + cl->ps.legsTimer + Q_irand(300, 1000);
				return;
			}
			wantAttack = qtrue;
			cl->monsterBotTime = level.time + Q_irand(1500, 4000);
		}
	}
	if (wantAlt)
	{
		G_MonsterStartAnim(self, BOTH_HOLD_DROP, 0);
		cl->monsterDropTime = level.time + 500;
	}
	else if (wantAttack)
	{
		G_MonsterStartAnim(self, BOTH_HOLD_ATTACK, 1400);
	}
	else if (!ucmd->forwardmove && !ucmd->rightmove)
	{
		// standing: holds him up (SP BOTH_HOLD_IDLE); walking plays the usual walk / run
		if (cl->ps.legsAnim != BOTH_HOLD_IDLE)
		{
			NPC_SetAnim(self, SETANIM_BOTH, BOTH_HOLD_IDLE, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		}
		if (cl->ps.legsTimer < 200)
		{
			cl->ps.legsTimer = cl->ps.torsoTimer = 200;
		}
	}
	else if (cl->ps.legsAnim == BOTH_HOLD_IDLE)
	{
		cl->ps.legsTimer = cl->ps.torsoTimer = 0;
	}
}

// ------------------------------------ rancor ------------------------------------

// SP Rancor_Swing with try_grab: grab one, smack the rest
static void G_RancorSwing(gentity_t* self)
{
	int touch[128];
	vec3_t hand, mins, maxs;
	const float scale = self->modelScale[0] > 0.0f ? self->modelScale[0] : 1.0f;
	// the SP reach (88) also for the smaller MP rancor (70%), more for a bigger one
	const float radius = 88.0f * (scale > 1.0f ? scale : 1.0f);
	gentity_t* target = self->client->monsterTarget >= 0 && self->client->monsterTarget < ENTITYNUM_WORLD
		? &g_entities[self->client->monsterTarget] : NULL;

	G_MonsterBoltOrg(self, "*r_hand", hand);
	G_Sound(self, CHAN_AUTO, G_SoundIndex(va("sound/chars/rancor/swipe%d.wav", Q_irand(1, 3))));

	gentity_t* victim = G_MonsterFindVictim(self, hand, radius, qfalse);
	if (!victim && G_MonsterCanGrab(self, target) && G_MonsterInReach(self, target, self->r.maxs[0] + 64.0f))
	{
		// the one it swung at is still right in front of it: it gets him (the hand bolt alone missed him too often)
		victim = target;
	}
	if (victim)
	{
		G_MonsterTakeVictim(self, victim);
		NPC_SetAnim(victim, SETANIM_BOTH, BOTH_SWIM_IDLE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		self->client->ps.weaponTime = self->client->ps.legsTimer + Q_irand(500, 2500);
		// a bot holds him up a moment before it bites (he can still fight free)
		self->client->monsterBotTime = level.time + self->client->ps.legsTimer + Q_irand(1500, 3000);
	}
	for (int i = 0; i < 3; i++)
	{
		mins[i] = hand[i] - radius;
		maxs[i] = hand[i] + radius;
	}
	const int num = trap->EntitiesInBox(mins, maxs, touch, 128);
	for (int i = 0; i < num; i++)
	{
		gentity_t* ent = &g_entities[touch[i]];
		vec3_t push_dir, angs;

		if (ent == victim || !G_MonsterCanGrab(self, ent) || Distance(ent->r.currentOrigin, hand) > radius)
		{
			continue;
		}
		// smack
		G_Sound(ent, CHAN_AUTO, G_SoundIndex("sound/chars/rancor/swipehit.wav"));
		VectorCopy(self->client->ps.viewangles, angs);
		angs[YAW] += flrand(25, 50);
		angs[PITCH] = flrand(-25, -15);
		AngleVectors(angs, push_dir, NULL, NULL);
		G_Damage(ent, self, self, vec3_origin, ent->r.currentOrigin, Q_irand(25, 40), DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK,
			MOD_MELEE);
		G_Throw(ent, push_dir, 250);
		if (ent->health > 0)
		{
			G_Knockdown(ent, self, push_dir, 100, qtrue);
		}
	}
}

static void G_RancorKillVictim(gentity_t* self, gentity_t* victim)
{
	if (victim->health > 0)
	{
		G_Dismember(victim, self, victim->r.currentOrigin, G2_MODELPART_WAIST);
		G_Damage(victim, self, self, vec3_origin, victim->r.currentOrigin, victim->health + 9999,
			DAMAGE_NO_PROTECTION | DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK | DAMAGE_NO_HIT_LOC, MOD_MELEE);
		G_AddEvent(victim, EV_JUMP, victim->health);
	}
	victim->client->ps.forceHandExtend = HANDEXTEND_NONE;
	victim->client->ps.forceHandExtendTime = 0;
	NPC_SetAnim(victim, SETANIM_BOTH, BOTH_SWIM_IDLE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
}

static void G_RancorThink(gentity_t* self, usercmd_t* ucmd, const qboolean isBot)
{
	gclient_t* cl = self->client;
	gentity_t* victim = G_MonsterVictim(self);
	qboolean wantAttack = ucmd->buttons & BUTTON_ATTACK ? qtrue : qfalse;
	qboolean wantAlt = ucmd->buttons & BUTTON_ALT_ATTACK ? qtrue : qfalse;

	if (cl->monsterHold && !victim)
	{
		G_MonsterReleaseVictim(self, qfalse);
	}

	// delayed damage of the anim playing (SP Rancor_Attack)
	if (cl->monsterDmgTime && level.time >= cl->monsterDmgTime)
	{
		cl->monsterDmgTime = 0;
		switch (cl->ps.legsAnim)
		{
		case BOTH_ATTACK2:
		case BOTH_ATTACK10:
		case BOTH_ATTACK11:
			if (!cl->monsterHold)
			{
				G_RancorSwing(self);
				// the swing may have grabbed someone: he is the victim from now on (it was still NULL here, and the
				// "holding someone" code below crashed on it - Jace 2026-10-29)
				victim = G_MonsterVictim(self);
			}
			break;
		case BOTH_ATTACK1:
			if (victim && cl->monsterHold == MONSTER_HOLD_HAND)
			{
				// quick bite
				G_Damage(victim, self, self, vec3_origin, victim->r.currentOrigin, Q_irand(50, 80),
					DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_MELEE);
				if (victim->health <= 0)
				{
					// bit his head off
					G_Dismember(victim, self, victim->r.currentOrigin, G2_MODELPART_HEAD);
					victim->client->ps.forceHandExtend = HANDEXTEND_NONE;
					victim->client->ps.forceHandExtendTime = 0;
					NPC_SetAnim(victim, SETANIM_BOTH, BOTH_SWIM_IDLE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				}
				G_Sound(victim, CHAN_AUTO, G_SoundIndex("sound/chars/rancor/chomp.wav"));
			}
			break;
		case BOTH_ATTACK3:
			if (victim && cl->monsterHold == MONSTER_HOLD_HAND)
			{
				// cut in half
				G_RancorKillVictim(self, victim);
				G_Sound(victim, CHAN_AUTO, G_SoundIndex("sound/chars/rancor/swipehit.wav"));
				cl->monsterDmg2Time = level.time + 1350;
			}
			break;
		default:
			break;
		}
	}
	else if (cl->monsterDmg2Time && level.time >= cl->monsterDmg2Time)
	{
		cl->monsterDmg2Time = 0;
		if (cl->ps.legsAnim == BOTH_ATTACK3 && victim && cl->monsterHold == MONSTER_HOLD_HAND)
		{
			// swallow him
			G_Sound(victim, CHAN_AUTO, G_SoundIndex("sound/chars/rancor/chomp.wav"));
			G_RancorKillVictim(self, victim);
			victim->client->ps.eFlags |= EF_NODRAW;
			victim->client->noCorpse = qtrue;
			cl->monsterHold = MONSTER_HOLD_MOUTH;
			cl->monsterClearTime = level.time + 2600;
		}
	}

	if (cl->monsterHold == MONSTER_HOLD_MOUTH)
	{
		ucmd->buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK);
		cl->ps.eFlags2 |= EF2_USE_ALT_ANIM | EF2_GENERIC_NPC_FLAG;
		if (level.time >= cl->monsterClearTime)
		{
			G_MonsterReleaseVictim(self, qfalse);
		}
		return;
	}

	if (!cl->monsterHold)
	{
		vec3_t front, fwd, center, yaw;

		cl->ps.eFlags2 &= ~(EF2_USE_ALT_ANIM | EF2_GENERIC_NPC_FLAG);
		if (cl->ps.weaponTime > 0 || cl->ps.groundEntityNum == ENTITYNUM_NONE)
		{
			return;
		}
		VectorSet(yaw, 0, cl->ps.viewangles[YAW], 0);
		AngleVectors(yaw, fwd, NULL, NULL);
		VectorMA(self->r.currentOrigin, self->r.maxs[0] + 50.0f, fwd, front);
		gentity_t* target = G_MonsterFindVictim(self, front, 90.0f, qtrue);
		if (isBot && target && level.time >= cl->monsterBotTime)
		{
			cl->monsterBotTime = level.time + Q_irand(1000, 2500);
			wantAlt = Q_irand(0, 1) ? qtrue : qfalse;
		}
		if (wantAlt && target)
		{
			// grab swing, high or low for where he is (SP)
			int grabAnim = BOTH_ATTACK2;

			VectorCopy(target->r.currentOrigin, center);
			if (center[2] - self->r.currentOrigin[2] >= 128.0f * self->modelScale[0])
			{
				grabAnim = BOTH_ATTACK11;
			}
			else if (center[2] - self->r.currentOrigin[2] >= 64.0f * self->modelScale[0])
			{
				grabAnim = BOTH_ATTACK10;
			}
			ucmd->buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK);
			G_MonsterStartAnim(self, grabAnim, 800);
			cl->monsterTarget = target->s.number;
		}
		return;
	}

	// holding someone in the hand
	ucmd->buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK);
	cl->ps.eFlags2 |= EF2_USE_ALT_ANIM;
	cl->ps.eFlags2 &= ~EF2_GENERIC_NPC_FLAG;
	if (!victim)
	{
		return; // (as the wampa) no valid victim this frame
	}

	if (victim->health > 0 && cl->monsterLastHealth - self->health >= 40 && cl->ps.legsAnim != BOTH_ATTACK3)
	{
		// hurt enough while holding him (his saber, his friends): it lets go and he is free
		G_MonsterReleaseVictim(self, qfalse);
		return;
	}

	if (!G_MonsterAnimDone(self))
	{
		return;
	}
	if (victim->health <= 0)
	{
		// a dead one in the hand gets eaten
		wantAttack = qfalse;
		wantAlt = qtrue;
	}
	else if (isBot)
	{
		// SP bites or eats at once; a bot holds him a moment and bites before it eats, so he has a chance to fight free
		wantAttack = wantAlt = qfalse;
		if (level.time >= cl->monsterBotTime)
		{
			if (victim->health <= 50 || cl->monsterBites >= 2 || cl->monsterBites && !Q_irand(0, 2))
			{
				wantAlt = qtrue;
			}
			else
			{
				wantAttack = qtrue;
			}
		}
	}
	if (wantAlt)
	{
		// full eat
		G_MonsterStartAnim(self, BOTH_ATTACK3, 900);
		if (victim->health > 0)
		{
			// he screams
			G_AddEvent(victim, Q_irand(EV_DEATH1, EV_DEATH3), 0);
			NPC_SetAnim(victim, SETANIM_TORSO, BOTH_FALLDEATH1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		}
	}
	else if (wantAttack)
	{
		G_MonsterStartAnim(self, BOTH_ATTACK1, 450);
		cl->ps.weaponTime = cl->ps.legsTimer + Q_irand(500, 1500);
		cl->monsterBotTime = level.time + cl->ps.weaponTime + Q_irand(1500, 3000);
		cl->monsterBites++;
	}
}

// ClientThink, before the move: the wampa / rancor player's grab, hold and eat
void G_MonsterPlayerThink(gentity_t* ent, usercmd_t* ucmd)
{
	const int monster = G_MonsterPlayerClass(ent);

	if (!ent->client)
	{
		return;
	}
	if (monster == BCLASS_NONE || ent->health <= 0 || !g_monsterGrab.integer
		|| ent->client->ps.eFlags2 & EF2_HELD_BY_MONSTER)
	{
		if (ent->client->monsterHold)
		{
			G_MonsterReleaseVictim(ent, qfalse);
		}
		ent->client->monsterGrabTime = 0;
		return;
	}
	const qboolean isBot = ent->r.svFlags & SVF_BOT ? qtrue : qfalse;
	if (monster == BCLASS_WAMPA)
	{
		G_WampaThink(ent, ucmd, isBot);
	}
	else
	{
		G_RancorThink(ent, ucmd, isBot);
	}
	if (G_MonsterStandStillAnim(ent, monster) || isBot && ent->client->monsterHold)
	{
		// SP: it stands still to grab, eat or maul (a bot also while it holds someone)
		ucmd->forwardmove = 0;
		ucmd->rightmove = 0;
		ucmd->upmove = 0;
	}
}

// G_HeldByMonster (g_active.c): someone held by a wampa / rancor player or bot (holders below MAX_CLIENTS; the rancor
// NPC is handled there). qtrue = handled here: he hangs in its hand (or is in its mouth), or he was let go because that
// monster no longer holds him (EF2_HELD_BY_MONSTER cleared). Held by a wampa he may slash at it with his saber (SP).
qboolean G_HeldByMonsterPlayer(gentity_t* ent, gentity_t* monster, usercmd_t* ucmd)
{
	if (!monster || monster->s.number >= MAX_CLIENTS)
	{
		return qfalse;
	}
	const int monsterClass = G_MonsterPlayerClass(monster);
	if (monsterClass == BCLASS_NONE || monster->health <= 0 || G_MonsterVictim(monster) != ent)
	{
		ent->client->ps.eFlags2 &= ~EF2_HELD_BY_MONSTER;
		ent->client->ps.hasLookTarget = qfalse;
		ent->client->ps.lookTarget = ENTITYNUM_NONE;
		ent->client->ps.viewangles[PITCH] = 0;
		ent->client->ps.viewangles[ROLL] = 0;
		ent->client->heldAttackTime = 0;
		return qtrue;
	}

	BG_AttachToRancor(monster->ghoul2, monster->client->ps.viewangles[YAW], monster->r.currentOrigin, level.time, NULL,
		monster->modelScale, monster->client->monsterHold == MONSTER_HOLD_MOUTH ? qtrue : qfalse,
		ent->client->ps.origin, ent->client->ps.viewangles, NULL);

	if (ent->health > 0)
	{
		gclient_t* cl = ent->client;
		int idleAnim = BOTH_SWIM_IDLE1;

		if (monsterClass == BCLASS_WAMPA)
		{
			idleAnim = BOTH_HANG_IDLE;
		}
		if (cl->heldAttackTime && level.time >= cl->heldAttackTime)
		{
			// the slash lands: any hit makes a wampa drop him, 40 damage a rancor (G_RancorThink)
			vec3_t dir;
			cl->heldAttackTime = 0;
			VectorSubtract(monster->r.currentOrigin, cl->ps.origin, dir);
			VectorNormalize(dir);
			G_Sound(monster, CHAN_AUTO, G_SoundIndex(va("sound/weapons/saber/saberhit%d.wav", Q_irand(1, 3))));
			G_Damage(monster, ent, ent, dir, cl->ps.origin, Q_irand(15, 25), DAMAGE_NO_KNOCKBACK, MOD_SABER);
			cl->heldAttackDebounce = level.time + (monsterClass == BCLASS_WAMPA ? 0 : 300);
		}
		else if (cl->ps.weapon == WP_SABER && ucmd->buttons & BUTTON_ATTACK && !cl->heldAttackTime
			&& level.time >= cl->heldAttackDebounce && cl->ps.legsAnim == idleAnim
			&& cl->ps.torsoAnim != BOTH_FALLDEATH1 && monster->client->ps.legsAnim != BOTH_ATTACK3)
		{
			cl->ps.saberHolstered = 0;
			if (monsterClass == BCLASS_WAMPA)
			{
				// SP PM_WeaponWampa: swing the saber up at it
				NPC_SetAnim(ent, SETANIM_BOTH, BOTH_HANG_ATTACK, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			}
			else
			{
				// in the rancor's fist: slash at its arm
				NPC_SetAnim(ent, SETANIM_TORSO, BOTH_A2_TR_BL, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			}
			cl->heldAttackTime = level.time + 400;
		}
		if (monsterClass != BCLASS_WAMPA && cl->ps.torsoAnim != idleAnim && cl->ps.torsoAnim != BOTH_FALLDEATH1
			&& cl->ps.torsoTimer <= 50)
		{
			NPC_SetAnim(ent, SETANIM_TORSO, idleAnim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		}
		if (cl->ps.legsAnim != idleAnim)
		{
			// grabbed / slash / pain anims play out, then back to hanging there
			const qboolean oneShot = cl->ps.legsAnim == BOTH_GRABBED || cl->ps.legsAnim == BOTH_HANG_ATTACK
				|| cl->ps.legsAnim == BOTH_HANG_PAIN || cl->ps.torsoAnim == BOTH_FALLDEATH1;
			if (!oneShot || cl->ps.legsTimer <= 50)
			{
				NPC_SetAnim(ent, SETANIM_BOTH, idleAnim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			}
		}
		if (cl->ps.legsAnim == idleAnim)
		{
			// keep the looping hang anim on (nothing else may play while held)
			if (cl->ps.legsTimer < 100)
			{
				cl->ps.legsTimer = 100;
			}
			if (cl->ps.torsoAnim == idleAnim && cl->ps.torsoTimer < 100)
			{
				cl->ps.torsoTimer = 100;
			}
		}
	}
	ucmd->buttons = 0;	// nothing else while held
	return qtrue;
}