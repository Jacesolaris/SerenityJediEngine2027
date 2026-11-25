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

// These define the working combat range for these suckers
#define MIN_DISTANCE 54
#define MIN_DISTANCE_SQR	( MIN_DISTANCE * MIN_DISTANCE )

#define MAX_DISTANCE 128
#define MAX_DISTANCE_SQR	( MAX_DISTANCE * MAX_DISTANCE )

#define LSTATE_CLEAR 0
#define LSTATE_WAITING 1
#define LSTATE_FLEE 2
#define LSTATE_BERZERK 3

#define HOWLER_RETREAT_DIST 300.0f
#define HOWLER_PANIC_HEALTH 10

extern void G_UcmdMoveForDir(const gentity_t* self, usercmd_t* cmd, vec3_t dir);
extern void G_GetBoltPosition(gentity_t* self, int boltIndex, vec3_t pos, int modelIndex);
extern qboolean NAV_DirSafe(const gentity_t* self, vec3_t dir, float dist);
extern void G_Knockdown(gentity_t* self, gentity_t* attacker, const vec3_t push_dir, float strength, const qboolean breakSaberLock);
extern float NPC_EntRangeFromBolt(const gentity_t* targ_ent, int boltIndex);
#ifdef SP_GAME // MP declares its own (entity numbers)
extern int NPC_GetEntsNearBolt(gentity_t** radius_ents, float radius, int boltIndex, vec3_t bolt_org);
#endif
extern qboolean PM_InKnockDown(const playerState_t* ps);

static void Howler_Attack(float enemy_dist, qboolean howl);
/*
-------------------------
NPC_Howler_Precache
-------------------------
*/
void NPC_Howler_Precache(void)
{
	int i = 0;
	G_EffectIndex("howler/sonic");
	G_SoundIndex("sound/chars/howler/howl.mp3");
	for (i = 1; i < 3; i++)
	{
		G_SoundIndex(va("sound/chars/howler/idle_hiss%d.mp3", i));
	}
	for (i = 1; i < 6; i++)
	{
		G_SoundIndex(va("sound/chars/howler/howl_talk%d.mp3", i));
		G_SoundIndex(va("sound/chars/howler/howl_yell%d.mp3", i));
	}
}

void Howler_ClearTimers(const gentity_t* self)
{
	//clear all my timers
	TIMER_Set(self, "flee", -level.time);
	TIMER_Set(self, "retreating", -level.time);
	TIMER_Set(self, "standing", -level.time);
	TIMER_Set(self, "walking", -level.time);
	TIMER_Set(self, "running", -level.time);
	TIMER_Set(self, "aggressionDecay", -level.time);
	TIMER_Set(self, "speaking", -level.time);
}

static qboolean NPC_Howler_Move(const int random_jump_chance)
{
	if (!TIMER_Done(NPCS.NPC, "standing"))
	{
		//standing around
		return qfalse;
	}
	if (NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		//in air, don't do anything
		return qfalse;
	}
	if (!NPCS.NPC->enemy && TIMER_Done(NPCS.NPC, "running") || !TIMER_Done(NPCS.NPC, "walking"))
	{
		NPCS.ucmd.buttons |= BUTTON_WALKING;
	}
	if ((!random_jump_chance || Q_irand(0, random_jump_chance))
		&& NPC_MoveToGoal(qtrue))
	{
		if (VectorCompare(NPCS.NPC->client->ps.moveDir, vec3_origin)
			|| !NPCS.NPC->client->ps.speed)
		{
			//uh.... wtf?  Got there?
			if (NPCS.NPCInfo->goalEntity)
			{
				NPC_FaceEntity(NPCS.NPCInfo->goalEntity, qfalse);
			}
			else
			{
				NPC_UpdateAngles(qfalse, qtrue);
			}
			return qtrue;
		}
		//TEMP: don't want to strafe
		VectorClear(NPCS.NPC->client->ps.moveDir);
		NPCS.ucmd.rightmove = 0.0f;
		//if backing up, go slow...
		if (NPCS.ucmd.forwardmove < 0.0f)
		{
			NPCS.ucmd.buttons |= BUTTON_WALKING;
			NPCS.NPC->client->ps.speed = NPCS.NPCInfo->stats.walkSpeed;
		}
		else
		{
			if (NPCS.ucmd.buttons & BUTTON_WALKING)
			{
				NPCS.NPC->client->ps.speed = NPCS.NPCInfo->stats.walkSpeed;
			}
			else
			{
				NPCS.NPC->client->ps.speed = NPCS.NPCInfo->stats.runSpeed;
			}
		}
		NPCS.NPCInfo->lockedDesiredYaw = NPCS.NPCInfo->desiredYaw = NPCS.NPCInfo->lastPathAngles[YAW];
		NPC_UpdateAngles(qfalse, qtrue);
	}
	else if (NPCS.NPCInfo->goalEntity)
	{
		//couldn't get where we wanted to go, try to jump there
		NPC_FaceEntity(NPCS.NPCInfo->goalEntity, qfalse);
		SHR_TryJumpEnt(NPCS.NPCInfo->goalEntity, 400.0f, -256.0f);
	}
	return qtrue;
}

/*
-------------------------
Howler_Idle
-------------------------
*/
static void Howler_Idle(void)
{}

/*
-------------------------
Howler_Patrol
-------------------------
*/
static void Howler_Patrol(void)
{
	NPCS.NPCInfo->localState = LSTATE_CLEAR;

	//If we have somewhere to go, then do that
	if (UpdateGoal())
	{
		NPC_Howler_Move(100);
	}

	gentity_t* near_player = SHR_NearestPlayer(NPCS.NPC);

	if (near_player)
	{
		vec3_t dif = { 0 };
		VectorSubtract(near_player->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin, dif);

		if (VectorLengthSquared(dif) < 256 * 256)
		{
			G_SetEnemy(NPCS.NPC, near_player);
		}
	}

	if (NPC_CheckEnemyExt(qtrue) == qfalse)
	{
		Howler_Idle();
		return;
	}

	Howler_Attack(0.0f, qtrue);
}

/*
-------------------------
Howler_Move
-------------------------
*/
static qboolean Howler_Move(void)
{
	if (NPCS.NPCInfo->localState != LSTATE_WAITING)
	{
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
		NPCS.NPCInfo->goalRadius = MAX_DISTANCE; // just get us within combat range
		return NPC_Howler_Move(30);
	}
	return qfalse;
}

//---------------------------------------------------------
static void Howler_TryDamage(const int damage, const qboolean tongue, const qboolean knockdown)
{
	vec3_t start = { 0 }, end = { 0 }, dir = { 0 };
	trace_t tr = { qfalse };

	if (tongue)
	{
		SHR_GetBoltPosition(NPCS.NPC, SHR_BoltField(NPCS.NPC, genericBolt1), start);
		SHR_GetBoltPosition(NPCS.NPC, SHR_BoltField(NPCS.NPC, genericBolt2), end);
		VectorSubtract(end, start, dir);
		const float dist = VectorNormalize(dir);
		VectorMA(start, dist + 16, dir, end);
	}
	else
	{
		VectorCopy(NPCS.NPC->SHR_currentOrigin, start);
		AngleVectors(NPCS.NPC->SHR_currentAngles, dir, NULL, NULL);
		VectorMA(start, MIN_DISTANCE * 2, dir, end);
	}

#ifndef FINAL_BUILD
	if (SHR_CVAR(d_saberCombat).integer > 1 || SHR_CVAR(g_DebugSaberCombat).integer)
	{
		G_DebugLine(start, end, 1000, 0x000000ff);
	}
#endif
	// Should probably trace from the mouth, but, ah well.
	SHR_Trace(&tr, start, vec3_origin, vec3_origin, end, NPCS.NPC->s.number, MASK_SHOT);

	if (tr.entityNum < ENTITYNUM_WORLD)
	{
		//hit *something*
		gentity_t* victim = &g_entities[tr.entityNum];
		if (!victim->client
			|| victim->client->NPC_class != CLASS_HOWLER)
		{
			//not another howler
			if (knockdown && victim->client)
			{
				//only do damage if victim isn't knocked down.  If he isn't, knock him down
				if (PM_InKnockDown(&victim->client->ps))
				{
					return;
				}
			}

			G_Damage(victim, NPCS.NPC, NPCS.NPC, dir, tr.endpos, damage, DAMAGE_NO_KNOCKBACK, MOD_MELEE);

			NPCS.NPC->enemy->client->poisonDamage = 18;
			NPCS.NPC->enemy->client->poisonTime = level.time + 1000;

			// Drug our enemy up and do the wonky vision thing
			gentity_t* tent = G_TempEntity(NPCS.NPC->enemy->SHR_currentOrigin, EV_DRUGGED);
			tent->owner = NPCS.NPC->enemy;

			if (knockdown && victim->health > 0)
			{
				//victim still alive
				G_Knockdown(victim, NPCS.NPC, NPCS.NPC->client->ps.velocity, 500, qfalse);
			}
		}
	}
}

/*
============
Howler_Howl
------------
Howler sonic howl attack:
- Finds nearby entities around the howl bolt.
- Deals minor damage + poison to close non-howler NPCs.
- Triggers sonic pain animations.
- Shakes camera if player is close.
============
*/
static void Howler_Howl(void)
{
	gentity_t* radius_ents[128] = { NULL };
	const float radius = (NPCS.NPC->spawnflags & 1) ? 256.0f : 128.0f;
	const float half_rad = radius * 0.5f;
	const float half_rad_squared = half_rad * half_rad;
	const float radius_squared = radius * radius;
	vec3_t bolt_org = { 0 };

	// Sound event for AI awareness
	AddSoundEvent(NPCS.NPC, NPCS.NPC->SHR_currentOrigin, 512, AEL_DANGER, qfalse, qtrue);

#ifdef SP_GAME
	const int num_ents = NPC_GetEntsNearBolt(radius_ents, radius, NPCS.NPC->handLBolt, bolt_org);
#else
	int radius_nums[128] = { 0 };
	const int num_ents = NPC_GetEntsNearBolt(radius_nums, radius, SHR_BoltField(NPCS.NPC, genericBolt1), bolt_org);
	for (int n = 0; n < num_ents; n++)
	{
		radius_ents[n] = &g_entities[radius_nums[n]];
	}
#endif

	for (int i = 0; i < num_ents; i++)
	{
		gentity_t* ent = radius_ents[i];

		if (ent == NULL || ent->inuse == qfalse)
		{
			continue;
		}

		if (ent == NPCS.NPC)
		{
			continue; // skip self
		}

		if (ent->client == NULL)
		{
			continue; // must be a client
		}

		if (ent->client->NPC_class == CLASS_HOWLER)
		{
			continue; // howlers immune
		}

		const float dist_sq = DistanceSquared(ent->SHR_currentOrigin, bolt_org);
		if (dist_sq <= radius_squared)
		{
			// ------------------------------------------------------------------
			// Close range: damage + poison + drugged vision
			// ------------------------------------------------------------------
			if (dist_sq < half_rad_squared)
			{
				const int doDamage = Q_irand(0, SHR_SPSKILL);

				if (doDamage != 0)
				{
					// Damage victim
					G_Damage(ent,
						NPCS.NPC,
						NPCS.NPC,
						vec3_origin,
						NPCS.NPC->SHR_currentOrigin,
						1,
						DAMAGE_NO_KNOCKBACK,
						SHR_MOD_IMPACT);

					// Apply poison to the actual victim
					qboolean validVictim =
						(ent != NULL &&
							ent->inuse == qtrue &&
							ent->client != NULL) ? qtrue : qfalse;

					if (validVictim == qtrue)
					{
						if (SHR_SPSKILL > 1)
						{ // higher skill levels = more poison
							ent->client->poisonDamage = 10;
							ent->client->poisonTime = level.time + 1000;

							// Drugged vision effect
							gentity_t* tent = G_TempEntity(ent->SHR_currentOrigin, EV_STUNNED);
							if (tent != NULL)
							{
								tent->owner = ent;
							}
						}
						else
						{
							ent->client->poisonDamage = 5;
							ent->client->poisonTime = level.time + 500;

							// Drugged vision effect
							gentity_t* tent = G_TempEntity(ent->SHR_currentOrigin, EV_STUNNED);
							if (tent != NULL)
							{
								tent->owner = ent;
							}
						}
					}
				}
			}

			// ------------------------------------------------------------------
			// Sonic pain animation logic
			// ------------------------------------------------------------------
			qboolean canAffect =
				(ent->health > 0 &&
					ent->client != NULL &&
					ent->client->NPC_class != CLASS_RANCOR &&
					ent->client->NPC_class != CLASS_ATST &&
					(PM_InKnockDown(&ent->client->ps) == qfalse)) ? qtrue : qfalse;

			if (canAffect == qtrue)
			{
				if (SHR_HasAnimation(ent, BOTH_SONICPAIN_START) == qtrue)
				{
					const int torsoAnim = ent->client->ps.torsoAnim;
					const int torsoTimer = ent->client->ps.SHR_torsoAnimTimer;

					qboolean inStartOrHold =
						(torsoAnim == BOTH_SONICPAIN_START ||
							torsoAnim == BOTH_SONICPAIN_HOLD) ? qtrue : qfalse;

					if (inStartOrHold == qfalse)
					{
						// Start sonic pain
						NPC_SetAnim(ent, SETANIM_LEGS, BOTH_SONICPAIN_START, SETANIM_FLAG_NORMAL);
						NPC_SetAnim(ent, SETANIM_TORSO, BOTH_SONICPAIN_START,
							SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

						ent->client->ps.SHR_torsoAnimTimer = torsoTimer + 100;
						ent->client->ps.weaponTime = ent->client->ps.SHR_torsoAnimTimer;
					}
					else
					{
						if (torsoTimer <= 100)
						{
							// Transition to hold
							NPC_SetAnim(ent, SETANIM_LEGS, BOTH_SONICPAIN_HOLD, SETANIM_FLAG_NORMAL);
							NPC_SetAnim(ent, SETANIM_TORSO, BOTH_SONICPAIN_HOLD,
								SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

							ent->client->ps.SHR_torsoAnimTimer = torsoTimer + 100;
							ent->client->ps.weaponTime = ent->client->ps.SHR_torsoAnimTimer;
						}
					}
				}
			}
		}
	}

	// ----------------------------------------------------------------------
	// Camera shake for player if close to howl origin
	// ----------------------------------------------------------------------
#ifdef SP_GAME
	const float player_dist = NPC_EntRangeFromBolt(player, SHR_BoltField(NPCS.NPC, genericBolt1));
	if (player_dist < 256.0f)
	{
		const float shakeScale = (1.0f * player_dist) / 64.0f;
		CGCam_Shake(shakeScale, 200);
	}
#else
	for (int shake_client = 0; shake_client < MAX_CLIENTS; shake_client++)
	{
		gentity_t* shake_ent = &g_entities[shake_client];

		if (!shake_ent->inuse || !shake_ent->client)
		{
			continue;
		}
		const float player_dist = NPC_EntRangeFromBolt(shake_ent, SHR_BoltField(NPCS.NPC, genericBolt1));
		if (player_dist < 256.0f)
		{
			G_ScreenShake(NPCS.NPC->SHR_currentOrigin, shake_ent, (1.0f * player_dist) / 64.0f, 200, qfalse);
		}
	}
#endif
}

//------------------------------
static void Howler_Attack(const float enemy_dist, const qboolean howl)
{
	const int dmg = NPCS.NPCInfo->localState == LSTATE_BERZERK ? 4 : 2;

	if (!TIMER_Exists(NPCS.NPC, "attacking"))
	{
		int attack_anim = BOTH_GESTURE1;
		// Going to do an attack
		if (NPCS.NPC->enemy && NPCS.NPC->enemy->client && PM_InKnockDown(&NPCS.NPC->enemy->client->ps)
			&& enemy_dist <= MIN_DISTANCE)
		{
			attack_anim = BOTH_ATTACK2;
		}
		else if (!Q_irand(0, 4) || howl)
		{
			//howl attack
			SHR_SoundOnEnt(NPCS.NPC, CHAN_VOICE, "sound/chars/howler/howl.mp3");
		}
		else if (enemy_dist > MIN_DISTANCE && Q_irand(0, 1))
		{
			//lunge attack
			//jump foward
			vec3_t fwd = { 0 };
			const vec3_t yawAng = { 0, NPCS.NPC->client->ps.viewangles[YAW], 0 };
			AngleVectors(yawAng, fwd, NULL, NULL);
			VectorScale(fwd, enemy_dist * 3.0f, NPCS.NPC->client->ps.velocity);
			NPCS.NPC->client->ps.velocity[2] = 200;
			NPCS.NPC->client->ps.groundEntityNum = ENTITYNUM_NONE;

			attack_anim = BOTH_ATTACK1;
		}
		else
		{
			//tongue attack
			attack_anim = BOTH_ATTACK2;
		}

		NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, attack_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART);
		if (NPCS.NPCInfo->localState == LSTATE_BERZERK)
		{
			//attack again right away
			TIMER_Set(NPCS.NPC, "attacking", NPCS.NPC->client->ps.SHR_legsAnimTimer);
		}
		else
		{
			TIMER_Set(NPCS.NPC, "attacking", NPCS.NPC->client->ps.SHR_legsAnimTimer + Q_irand(0, 1500)); //FIXME: base on skill
			TIMER_Set(NPCS.NPC, "standing", -level.time);
			TIMER_Set(NPCS.NPC, "walking", -level.time);
			TIMER_Set(NPCS.NPC, "running", NPCS.NPC->client->ps.SHR_legsAnimTimer + 5000);
		}

		TIMER_Set(NPCS.NPC, "attack_dmg", 200); // level two damage
	}

	// Need to do delayed damage since the attack animations encapsulate multiple mini-attacks
	switch (NPCS.NPC->client->ps.legsAnim)
	{
	case BOTH_ATTACK1:
	case BOTH_MELEE1:
		if (NPCS.NPC->client->ps.SHR_legsAnimTimer > 650 //more than 13 frames left
			&& SHR_AnimLength(NPCS.NPC, NPCS.NPC->client->ps.legsAnim)
			- NPCS.NPC->client->ps.SHR_legsAnimTimer >= 800) //at least 16 frames into anim
		{
			Howler_TryDamage(dmg, qfalse, qfalse);
		}
		break;
	case BOTH_ATTACK2:
	case BOTH_MELEE2:
		if (NPCS.NPC->client->ps.SHR_legsAnimTimer > 350 //more than 7 frames left
			&& SHR_AnimLength(NPCS.NPC, NPCS.NPC->client->ps.legsAnim)
			- NPCS.NPC->client->ps.SHR_legsAnimTimer >= 550) //at least 11 frames into anim
		{
			Howler_TryDamage(dmg, qtrue, qfalse);
		}
		break;
	case BOTH_GESTURE1:
	{
		if (NPCS.NPC->client->ps.SHR_legsAnimTimer > 1800 //more than 36 frames left
			&& SHR_AnimLength(NPCS.NPC, NPCS.NPC->client->ps.legsAnim) - NPCS.NPC->client->ps.SHR_legsAnimTimer >=
			950) //at least 19 frames into anim
		{
			Howler_Howl();
			if (!NPCS.NPC->count)
			{
#ifdef SP_GAME
				G_PlayEffect(G_EffectIndex("howler/sonic"), NPCS.NPC->playerModel, SHR_BoltField(NPCS.NPC, genericBolt1), NPCS.NPC->s.number,
					NPCS.NPC->SHR_currentOrigin, 4750, qtrue);
#else
				{
					vec3_t bolt_org = { 0 }, fwd = { 0 };
					SHR_GetBoltPosition(NPCS.NPC, SHR_BoltField(NPCS.NPC, genericBolt1), bolt_org);
					AngleVectors(NPCS.NPC->client->ps.viewangles, fwd, NULL, NULL);
					G_PlayEffectID(G_EffectIndex("howler/sonic"), bolt_org, fwd);
				}
#endif
				SHR_SoundOnEnt(NPCS.NPC, CHAN_VOICE, "sound/chars/howler/howl.mp3");
				NPCS.NPC->count = 1;
			}
		}
	}
	break;
	default:
		//anims seem to get reset after a load, so just stop attacking and it will restart as needed.
		TIMER_Remove(NPCS.NPC, "attacking");
		break;
	}

	// Just using this to remove the attacking flag at the right time
	TIMER_Done2(NPCS.NPC, "attacking", qtrue);
}

//----------------------------------
static void Howler_Combat(void)
{
	qboolean faced = qfalse;
	if (NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		//not on the ground
		if (NPCS.NPC->client->ps.legsAnim == BOTH_JUMP1
			|| NPCS.NPC->client->ps.legsAnim == BOTH_INAIR1)
		{
			//flying through the air with the greatest of ease, etc
			Howler_TryDamage(5, qfalse, qfalse);
		}
	}
	else
	{
		qboolean advance = qfalse;
		//not in air, see if we should attack or advance
		// If we cannot see our target or we have somewhere to go, then do that
		if (!SHR_ClearLOSEnt(NPCS.NPC->enemy)) //|| UpdateGoal( ))
		{
			NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
			NPCS.NPCInfo->goalRadius = MAX_DISTANCE; // just get us within combat range

			if (NPCS.NPCInfo->localState == LSTATE_BERZERK)
			{
				NPC_Howler_Move(3);
			}
			else
			{
				NPC_Howler_Move(10);
			}
			NPC_UpdateAngles(qfalse, qtrue);
			return;
		}

		const float distance = DistanceHorizontal(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->enemy->SHR_currentOrigin);

		if (NPCS.NPC->enemy && NPCS.NPC->enemy->client && PM_InKnockDown(&NPCS.NPC->enemy->client->ps))
		{
			//get really close to knocked down enemies
			advance = distance > MIN_DISTANCE ? qtrue : qfalse;
		}
		else
		{
			advance = distance > MAX_DISTANCE ? qtrue : qfalse; //MIN_DISTANCE
		}

		if ((advance || NPCS.NPCInfo->localState == LSTATE_WAITING) && TIMER_Done(NPCS.NPC, "attacking"))
			// waiting monsters can't attack
		{
			if (TIMER_Done2(NPCS.NPC, "takingPain", qtrue))
			{
				NPCS.NPCInfo->localState = LSTATE_CLEAR;
			}
			else if (TIMER_Done(NPCS.NPC, "standing"))
			{
				faced = Howler_Move();
			}
		}
		else
		{
			Howler_Attack(distance, qfalse);
		}
	}

	if (!faced)
	{
		if (TIMER_Done(NPCS.NPC, "attacking")) // not attacking
		{
			//not standing around
			// Sometimes I have problems with facing the enemy I'm attacking, so force the issue so I don't look dumb
			NPC_FaceEnemy(qtrue);
		}
		else
		{
			NPC_UpdateAngles(qfalse, qtrue);
		}
	}
}

/*
-------------------------
NPC_Howler_Pain
-------------------------
*/
void NPC_Howler_Pain(SHR_PAIN_PARAMS)
{
	if (!self || !self->NPC)
	{
		return;
	}

	if (self->NPC->localState != LSTATE_BERZERK) //damage >= 10 )
	{
		self->NPC->stats.aggression += damage;
		self->NPC->localState = LSTATE_WAITING;

		TIMER_Remove(self, "attacking");

		VectorCopy(self->NPC->lastPathAngles, self->s.angles);

		//if ( self->client->ps.legsAnim == BOTH_GESTURE1 )
		{
#ifdef SP_GAME // MP plays the howl once, nothing to stop
			G_StopEffect(G_EffectIndex("howler/sonic"), self->playerModel, self->genericBolt1, self->s.number);
#endif
		}

		NPC_SetAnim(self, SETANIM_BOTH, BOTH_PAIN1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		TIMER_Set(self, "takingPain", self->client->ps.SHR_legsAnimTimer); //2900 );

		if (self->health > HOWLER_PANIC_HEALTH)
		{
			//still have some health left
			if (Q_irand(0, SHR_MAXHEALTH(self)) > self->health) //FIXME: or check damage?
			{
				//back off!
				TIMER_Set(self, "standing", -level.time);
				TIMER_Set(self, "running", -level.time);
				TIMER_Set(self, "walking", -level.time);
				TIMER_Set(self, "retreating", Q_irand(1000, 5000));
			}
			else
			{
				//go after him!
				TIMER_Set(self, "standing", -level.time);
				TIMER_Set(self, "running", self->client->ps.SHR_legsAnimTimer + Q_irand(3000, 6000));
				TIMER_Set(self, "walking", -level.time);
				TIMER_Set(self, "retreating", -level.time);
			}
		}
		else if (self->NPC)
		{
			//panic!
			if (Q_irand(0, 1))
			{
				//berzerk
				self->NPC->localState = LSTATE_BERZERK;
			}
			else
			{
				//flee
				self->NPC->localState = LSTATE_FLEE;
				TIMER_Set(self, "flee", Q_irand(10000, 30000));
			}
		}
	}
}

/*
-------------------------
NPC_BSHowler_Default
-------------------------
*/
void NPC_BSHowler_Default(void)
{
	if (NPCS.NPC->client->ps.legsAnim != BOTH_GESTURE1)
	{
		NPCS.NPC->count = 0;
	}
	//FIXME: if in jump, do damage in front and maybe knock them down?
	if (!TIMER_Done(NPCS.NPC, "attacking"))
	{
		if (NPCS.NPC->enemy)
		{
			Howler_Attack(Distance(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin), qfalse);
		}
		else
		{
			Howler_Attack(0.0f, qfalse);
		}
		NPC_UpdateAngles(qfalse, qtrue);
		return;
	}

	if (NPCS.NPC->enemy)
	{
		if (NPCS.NPCInfo->stats.aggression > 0)
		{
			if (TIMER_Done(NPCS.NPC, "aggressionDecay"))
			{
				NPCS.NPCInfo->stats.aggression--;
				TIMER_Set(NPCS.NPC, "aggressionDecay", 500);
			}
		}
		if (!TIMER_Done(NPCS.NPC, "flee")
			&& SHR_BSFleeRun()) //this can clear ENEMY
		{
			//successfully trying to run away
			return;
		}
		if (NPCS.NPC->enemy == NULL)
		{
			NPC_UpdateAngles(qfalse, qtrue);
			return;
		}
		if (NPCS.NPCInfo->localState == LSTATE_FLEE)
		{
			//we were fleeing, now done (either timer ran out or we cannot flee anymore
			if (SHR_ClearLOSEnt(NPCS.NPC->enemy))
			{
				//if enemy is still around, go berzerk
				NPCS.NPCInfo->localState = LSTATE_BERZERK;
			}
			else
			{
				//otherwise, lick our wounds?
				NPCS.NPCInfo->localState = LSTATE_CLEAR;
				TIMER_Set(NPCS.NPC, "standing", Q_irand(3000, 10000));
			}
		}
		else if (NPCS.NPCInfo->localState == LSTATE_BERZERK)
		{
			//go nuts!
		}
		else if (NPCS.NPCInfo->stats.aggression >= Q_irand(75, 125))
		{
			//that's it, go nuts!
			NPCS.NPCInfo->localState = LSTATE_BERZERK;
		}
		else if (!TIMER_Done(NPCS.NPC, "retreating"))
		{
			//trying to back off
			NPC_FaceEnemy(qtrue);
			if (NPCS.NPC->client->ps.speed > NPCS.NPCInfo->stats.walkSpeed)
			{
				NPCS.NPC->client->ps.speed = NPCS.NPCInfo->stats.walkSpeed;
			}
			NPCS.ucmd.buttons |= BUTTON_WALKING;
			if (Distance(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin) < HOWLER_RETREAT_DIST)
			{
				//enemy is close
				vec3_t moveDir = { 0 };
				AngleVectors(NPCS.NPC->SHR_currentAngles, moveDir, NULL, NULL);
				VectorScale(moveDir, -1, moveDir);
				if (!NAV_DirSafe(NPCS.NPC, moveDir, 8))
				{
					//enemy is backing me up against a wall or ledge!  Start to get really mad!
					NPCS.NPCInfo->stats.aggression += 2;
				}
				else
				{
					//back off
					NPCS.ucmd.forwardmove = -127;
				}
				//enemy won't leave me alone, get mad...
				NPCS.NPCInfo->stats.aggression++;
			}
			return;
		}
		else if (TIMER_Done(NPCS.NPC, "standing"))
		{
			//not standing around
			if (!NPCS.NPCInfo->last_ucmd.forwardmove
				&& !NPCS.NPCInfo->last_ucmd.rightmove)
			{
				//stood last frame
				if (TIMER_Done(NPCS.NPC, "walking")
					&& TIMER_Done(NPCS.NPC, "running"))
				{
					//not walking or running
					if (Q_irand(0, 2))
					{
						//run for a while
						TIMER_Set(NPCS.NPC, "walking", Q_irand(4000, 8000));
					}
					else
					{
						//walk for a bit
						TIMER_Set(NPCS.NPC, "running", Q_irand(2500, 5000));
					}
				}
			}
			else if (NPCS.NPCInfo->last_ucmd.buttons & BUTTON_WALKING)
			{
				//walked last frame
				if (TIMER_Done(NPCS.NPC, "walking"))
				{
					//just finished walking
					if (Q_irand(0, 5) || DistanceSquared(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin) <
						MAX_DISTANCE_SQR)
					{
						//run for a while
						TIMER_Set(NPCS.NPC, "running", Q_irand(4000, 20000));
					}
					else
					{
						//stand for a bit
						TIMER_Set(NPCS.NPC, "standing", Q_irand(2000, 6000));
					}
				}
			}
			else
			{
				//ran last frame
				if (TIMER_Done(NPCS.NPC, "running"))
				{
					//just finished running
					if (Q_irand(0, 8) || DistanceSquared(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin) <
						MAX_DISTANCE_SQR)
					{
						//walk for a while
						TIMER_Set(NPCS.NPC, "walking", Q_irand(3000, 10000));
					}
					else
					{
						//stand for a bit
						TIMER_Set(NPCS.NPC, "standing", Q_irand(2000, 6000));
					}
				}
			}
		}
		if (NPC_ValidEnemy(NPCS.NPC->enemy) == qfalse)
		{
			TIMER_Remove(NPCS.NPC, "lookForNewEnemy"); //make them look again right now
			if (!NPCS.NPC->enemy->inuse || level.time - NPCS.NPC->enemy->s.time > Q_irand(10000, 15000))
			{
				//it's been a while since the enemy died, or enemy is completely gone, get bored with him
				NPCS.NPC->enemy = NULL;
				Howler_Patrol();
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
		}
		if (TIMER_Done(NPCS.NPC, "lookForNewEnemy"))
		{
			gentity_t* sav_enemy = NPCS.NPC->enemy; //FIXME: what about NPC->lastEnemy?
			NPCS.NPC->enemy = NULL;
			gentity_t* new_enemy = NPC_CheckEnemy(
				(qboolean)(NPCS.NPCInfo->confusionTime < level.time && SHR_InsanityTime(NPCS.NPCInfo) < level.time),
				qfalse, qfalse);
			NPCS.NPC->enemy = sav_enemy;
			if (new_enemy && new_enemy != sav_enemy)
			{
				//picked up a new enemy!
				NPCS.NPC->lastEnemy = NPCS.NPC->enemy;
				G_SetEnemy(NPCS.NPC, new_enemy);
				if (NPCS.NPC->enemy != NPCS.NPC->lastEnemy)
				{
					//clear this so that we only sniff the player the first time we pick them up
					NPCS.NPC->useDebounceTime = 0;
				}
				//hold this one for at least 5-15 seconds
				TIMER_Set(NPCS.NPC, "lookForNewEnemy", Q_irand(5000, 15000));
			}
			else
			{
				//look again in 2-5 secs
				TIMER_Set(NPCS.NPC, "lookForNewEnemy", Q_irand(2000, 5000));
			}
		}
		Howler_Combat();
		if (TIMER_Done(NPCS.NPC, "speaking"))
		{
			if (!TIMER_Done(NPCS.NPC, "standing")
				|| !TIMER_Done(NPCS.NPC, "retreating"))
			{
				SHR_SoundOnEnt(NPCS.NPC, CHAN_VOICE, va("sound/chars/howler/idle_hiss%d.mp3", Q_irand(1, 2)));
			}
			else if (!TIMER_Done(NPCS.NPC, "walking")
				|| NPCS.NPCInfo->localState == LSTATE_FLEE)
			{
				SHR_SoundOnEnt(NPCS.NPC, CHAN_VOICE, va("sound/chars/howler/howl_talk%d.mp3", Q_irand(1, 5)));
			}
			else
			{
				SHR_SoundOnEnt(NPCS.NPC, CHAN_VOICE, va("sound/chars/howler/howl_yell%d.mp3", Q_irand(1, 5)));
			}
			if (NPCS.NPCInfo->localState == LSTATE_BERZERK
				|| NPCS.NPCInfo->localState == LSTATE_FLEE)
			{
				TIMER_Set(NPCS.NPC, "speaking", Q_irand(1000, 4000));
			}
			else
			{
				TIMER_Set(NPCS.NPC, "speaking", Q_irand(3000, 8000));
			}
		}
		return;
	}
	if (TIMER_Done(NPCS.NPC, "speaking"))
	{
		if (!Q_irand(0, 3))
		{
			SHR_SoundOnEnt(NPCS.NPC, CHAN_VOICE, va("sound/chars/howler/idle_hiss%d.mp3", Q_irand(1, 2)));
		}
		else
		{
			SHR_SoundOnEnt(NPCS.NPC, CHAN_VOICE, va("sound/chars/howler/howl_talk%d.mp3", Q_irand(1, 5)));
		}
		TIMER_Set(NPCS.NPC, "speaking", Q_irand(4000, 12000));
	}
	if (NPCS.NPCInfo->stats.aggression > 0)
	{
		if (TIMER_Done(NPCS.NPC, "aggressionDecay"))
		{
			NPCS.NPCInfo->stats.aggression--;
			TIMER_Set(NPCS.NPC, "aggressionDecay", 200);
		}
	}
	if (TIMER_Done(NPCS.NPC, "standing"))
	{
		//not standing around
		if (!NPCS.NPCInfo->last_ucmd.forwardmove
			&& !NPCS.NPCInfo->last_ucmd.rightmove)
		{
			//stood last frame
			if (TIMER_Done(NPCS.NPC, "walking")
				&& TIMER_Done(NPCS.NPC, "running"))
			{
				//not walking or running
				if (NPCS.NPCInfo->goalEntity)
				{
					//have somewhere to go
					if (Q_irand(0, 2))
					{
						//walk for a while
						TIMER_Set(NPCS.NPC, "walking", Q_irand(3000, 10000));
					}
					else
					{
						//run for a bit
						TIMER_Set(NPCS.NPC, "running", Q_irand(2500, 5000));
					}
				}
			}
		}
		else if (NPCS.NPCInfo->last_ucmd.buttons & BUTTON_WALKING)
		{
			//walked last frame
			if (TIMER_Done(NPCS.NPC, "walking"))
			{
				//just finished walking
				if (Q_irand(0, 3))
				{
					//run for a while
					TIMER_Set(NPCS.NPC, "running", Q_irand(3000, 6000));
				}
				else
				{
					//stand for a bit
					TIMER_Set(NPCS.NPC, "standing", Q_irand(2500, 5000));
				}
			}
		}
		else
		{
			//ran last frame
			if (TIMER_Done(NPCS.NPC, "running"))
			{
				//just finished running
				if (Q_irand(0, 2))
				{
					//walk for a while
					TIMER_Set(NPCS.NPC, "walking", Q_irand(6000, 15000));
				}
				else
				{
					//stand for a bit
					TIMER_Set(NPCS.NPC, "standing", Q_irand(4000, 6000));
				}
			}
		}
	}
	if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
	{
		Howler_Patrol();
	}
	else
	{
		Howler_Idle();
	}

	NPC_UpdateAngles(qfalse, qtrue);
}