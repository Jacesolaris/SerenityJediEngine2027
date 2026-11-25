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

gentity_t* CreateMissile(vec3_t org, vec3_t dir, float vel, int life, gentity_t* owner, qboolean alt_fire);
extern gitem_t* SHR_FindItemForAmmo(ammo_t ammo);

//Local state enums
enum
{
	LSTATE_NONE = 0,
	LSTATE_BACKINGUP,
	LSTATE_SPINNING,
	LSTATE_PAIN,
	LSTATE_DROP
};

void ImperialProbe_Idle(void);

void NPC_Probe_Precache(void)
{
	for (int i = 1; i < 4; i++)
	{
		G_SoundIndex(va("sound/chars/probe/misc/probetalk%d", i));
	}
	G_SoundIndex("sound/chars/probe/misc/probedroidloop");
	G_SoundIndex("sound/chars/probe/misc/anger1");
	G_SoundIndex("sound/chars/probe/misc/fire");

	G_EffectIndex("chunks/probehead");
	G_EffectIndex("env/med_explode2");
	G_EffectIndex("explosions/probeexplosion1");
	SHR_ModelIndex("models/map_objects/imp_mine/probedestroyed.md3"); // SJE probe wreck (2026-10-16), g_combat.cpp DeathFX
	G_EffectIndex("bryar_old/muzzle_flash");

	RegisterItem(SHR_FindItemForAmmo(AMMO_BLASTER));
	RegisterItem(SHR_FindItemForWeapon(WP_BRYAR_PISTOL));
}

/*
-------------------------
Hunter_MaintainHeight
-------------------------
*/

#define VELOCITY_DECAY 0.85f

static void ImperialProbe_MaintainHeight(void)
{
	float dif = 0.0f;
	//	vec3_t	endPos;
	//	trace_t	trace;

	// Update our angles regardless
	NPC_UpdateAngles(qtrue, qtrue);

	// If we have an enemy, we should try to hover at about enemy eye level
	if (NPCS.NPC->enemy)
	{
		// Find the height difference
		dif = NPCS.NPC->enemy->SHR_currentOrigin[2] - NPCS.NPC->SHR_currentOrigin[2];

		// cap to prevent dramatic height shifts
		if (fabs(dif) > 8)
		{
			if (fabs(dif) > 16)
			{
				dif = dif < 0 ? -16 : 16;
			}

			NPCS.NPC->client->ps.velocity[2] = (NPCS.NPC->client->ps.velocity[2] + dif) / 2;
		}
	}
	else
	{
		const gentity_t* goal = NULL;

		if (NPCS.NPCInfo->goalEntity) // Is there a goal?
		{
			goal = NPCS.NPCInfo->goalEntity;
		}
		else
		{
			goal = NPCS.NPCInfo->lastGoalEntity;
		}
		if (goal)
		{
			dif = goal->SHR_currentOrigin[2] - NPCS.NPC->SHR_currentOrigin[2];

			if (fabs(dif) > 24)
			{
				NPCS.ucmd.upmove = NPCS.ucmd.upmove < 0 ? -4 : 4;
			}
			else
			{
				if (NPCS.NPC->client->ps.velocity[2])
				{
					NPCS.NPC->client->ps.velocity[2] *= VELOCITY_DECAY;

					if (fabs(NPCS.NPC->client->ps.velocity[2]) < 2)
					{
						NPCS.NPC->client->ps.velocity[2] = 0;
					}
				}
			}
		}
		// Apply friction
		else if (NPCS.NPC->client->ps.velocity[2])
		{
			NPCS.NPC->client->ps.velocity[2] *= VELOCITY_DECAY;

			if (fabs(NPCS.NPC->client->ps.velocity[2]) < 1)
			{
				NPCS.NPC->client->ps.velocity[2] = 0;
			}
		}
	}

	// Apply friction
	if (NPCS.NPC->client->ps.velocity[0])
	{
		NPCS.NPC->client->ps.velocity[0] *= VELOCITY_DECAY;

		if (fabs(NPCS.NPC->client->ps.velocity[0]) < 1)
		{
			NPCS.NPC->client->ps.velocity[0] = 0;
		}
	}

	if (NPCS.NPC->client->ps.velocity[1])
	{
		NPCS.NPC->client->ps.velocity[1] *= VELOCITY_DECAY;

		if (fabs(NPCS.NPC->client->ps.velocity[1]) < 1)
		{
			NPCS.NPC->client->ps.velocity[1] = 0;
		}
	}
}

/*
-------------------------
ImperialProbe_Strafe
-------------------------
*/

#define HUNTER_STRAFE_VEL 256
#define HUNTER_STRAFE_DIS 200
#define HUNTER_UPWARD_PUSH 32

static void ImperialProbe_Strafe(void)
{
	vec3_t end = { 0 }, right = { 0 };
	trace_t tr = { qfalse };

	AngleVectors(NPCS.NPC->client->renderInfo.eyeAngles, NULL, right, NULL);

	// Pick a random strafe direction, then check to see if doing a strafe would be
	//	reasonable valid
	const int dir = rand() & 1 ? -1 : 1;
	VectorMA(NPCS.NPC->SHR_currentOrigin, HUNTER_STRAFE_DIS * dir, right, end);

	SHR_Trace(&tr, NPCS.NPC->SHR_currentOrigin, NULL, NULL, end, NPCS.NPC->s.number, MASK_SOLID);

	// Close enough
	if (tr.fraction > 0.9f)
	{
		VectorMA(NPCS.NPC->client->ps.velocity, HUNTER_STRAFE_VEL * dir, right, NPCS.NPC->client->ps.velocity);

		// Add a slight upward push
		NPCS.NPC->client->ps.velocity[2] += HUNTER_UPWARD_PUSH;

		// Set the strafe start time so we can do a controlled roll
		NPCS.NPC->fx_time = level.time;
		NPCS.NPCInfo->standTime = level.time + 3000 + Q_flrand(0.0f, 1.0f) * 500;
	}
}

/*
-------------------------
ImperialProbe_Hunt
-------------------------`
*/

#define HUNTER_FORWARD_BASE_SPEED 10
#define HUNTER_FORWARD_MULTIPLIER 5

static void ImperialProbe_Hunt(const qboolean visible, const qboolean advance)
{
	vec3_t forward = { 0 };

	NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_RUN1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

	//If we're not supposed to stand still, pursue the player
	if (NPCS.NPCInfo->standTime < level.time)
	{
		// Only strafe when we can see the player
		if (visible)
		{
			ImperialProbe_Strafe();
			return;
		}
	}

	//If we don't want to advance, stop here
	if (advance == qfalse)
		return;

	//Only try and navigate if the player is visible
	if (visible == qfalse)
	{
		// Move towards our goal
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
		NPCS.NPCInfo->goalRadius = 12;

		NPC_MoveToGoal(qtrue);
		return;
	}
	VectorSubtract(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin, forward);
	/*distance = */
	VectorNormalize(forward);

	const float speed = HUNTER_FORWARD_BASE_SPEED + HUNTER_FORWARD_MULTIPLIER * SHR_SPSKILL;
	VectorMA(NPCS.NPC->client->ps.velocity, speed, forward, NPCS.NPC->client->ps.velocity);
}

/*
-------------------------
ImperialProbe_FireBlaster
-------------------------
*/
static void ImperialProbe_FireBlaster(void)
{
	vec3_t muzzle1 = { 0 };
	static vec3_t forward, vright, up;
	mdxaBone_t boltMatrix = { 0 };

	//FIXME: use {0, NPCS.NPC->client->ps.legsYaw, 0}
	SHR_GetBoltMatrix(NPCS.NPC, SHR_Bolt(NPCS.NPC, genericBolt1, "*flash"), boltMatrix, NPCS.NPC->SHR_currentAngles, NPCS.NPC->SHR_currentOrigin);

	SHR_GiveMeVector(boltMatrix, ORIGIN, muzzle1);

	SHR_PlayEffectUp("bryar_old/muzzle_flash", muzzle1);

	SHR_Sound(NPCS.NPC, G_SoundIndex("sound/chars/probe/misc/fire"));

	if (NPCS.NPC->health)
	{
		vec3_t angle_to_enemy1 = { 0 };
		vec3_t delta1 = { 0 };
		vec3_t enemy_org1 = { 0 };
		CalcEntitySpot(NPCS.NPC->enemy, SPOT_CHEST, enemy_org1);
		enemy_org1[0] += Q_irand(0, 10);
		enemy_org1[1] += Q_irand(0, 10);
		VectorSubtract(enemy_org1, muzzle1, delta1);
		vectoangles(delta1, angle_to_enemy1);
		AngleVectors(angle_to_enemy1, forward, vright, up);
	}
	else
	{
		AngleVectors(NPCS.NPC->SHR_currentAngles, forward, vright, up);
	}

	gentity_t* missile = CreateMissile(muzzle1, forward, 1600, 10000, NPCS.NPC, qfalse);

	missile->classname = "bryar_proj";
	missile->s.weapon = WP_BRYAR_PISTOL;

	if (SHR_SPSKILL <= 1)
	{
		missile->damage = 5;
	}
	else
	{
		missile->damage = 10;
	}

	missile->dflags = DAMAGE_DEATH_KNOCKBACK;
	missile->methodOfDeath = SHR_MOD_ENERGY;
	missile->clipmask = MASK_SHOT;
}

/*
-------------------------
ImperialProbe_Ranged
-------------------------
*/
static void ImperialProbe_Ranged(const qboolean visible, const qboolean advance)
{
	if (TIMER_Done(NPCS.NPC, "attackDelay")) // Attack?
	{
		int delay_max = 0;
		int delay_min = 0;
		if (SHR_SPSKILL == 0)
		{
			delay_min = 500;
			delay_max = 3000;
		}
		else if (SHR_SPSKILL > 1)
		{
			delay_min = 500;
			delay_max = 2000;
		}
		else
		{
			delay_min = 300;
			delay_max = 1500;
		}

		TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(delay_min, delay_max));
		ImperialProbe_FireBlaster();
	}

	if (NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES)
	{
		ImperialProbe_Hunt(visible, advance);
	}
}

/*
-------------------------
ImperialProbe_AttackDecision
-------------------------
*/

#define MIN_MELEE_RANGE 320
#define	MIN_MELEE_RANGE_SQR	( MIN_MELEE_RANGE * MIN_MELEE_RANGE )

#define MIN_DISTANCE 128
#define MIN_DISTANCE_SQR	( MIN_DISTANCE * MIN_DISTANCE )

static void ImperialProbe_AttackDecision(void)
{
	// Always keep a good height off the ground
	ImperialProbe_MaintainHeight();

	//randomly talk
	if (TIMER_Done(NPCS.NPC, "patrolNoise"))
	{
		if (TIMER_Done(NPCS.NPC, "angerNoise"))
		{
			SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, va("sound/chars/probe/misc/probetalk%d", Q_irand(1, 3)));

			TIMER_Set(NPCS.NPC, "patrolNoise", Q_irand(4000, 10000));
		}
	}

	// If we don't have an enemy, just idle
	if (NPC_CheckEnemyExt(qfalse) == qfalse)
	{
		ImperialProbe_Idle();
		return;
	}

	NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_RUN1, SETANIM_FLAG_NORMAL);

	// Rate our distance to the target, and our visibilty
	const float distance = (int)(DistanceHorizontalSquared(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->enemy->SHR_currentOrigin));
	//	distance_e	distRate	= ( distance > MIN_MELEE_RANGE_SQR ) ? DIST_LONG : DIST_MELEE;
	const qboolean visible = SHR_ClearLOSEnt(NPCS.NPC->enemy);
	const qboolean advance = (qboolean)(distance > MIN_DISTANCE_SQR);

	// If we cannot see our target, move to see it
	if (visible == qfalse)
	{
		if (NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES)
		{
			ImperialProbe_Hunt(visible, advance);
			return;
		}
	}

	// Sometimes I have problems with facing the enemy I'm attacking, so force the issue so I don't look dumb
	NPC_FaceEnemy(qtrue);

	// Decide what type of attack to do
	ImperialProbe_Ranged(visible, advance);
}

/*
-------------------------
NPC_BSDroid_Pain
-------------------------
*/
void NPC_Probe_Pain(SHR_PAIN_PARAMS)
{
	gentity_t* attacker = SHR_PAIN_ATTACKER;
	const int pain_mod = SHR_PAIN_MOD;

	VectorCopy(self->NPC->lastPathAngles, self->s.angles);

	if (self->health < 30 || pain_mod == MOD_DEMP2 || pain_mod == MOD_DEMP2_ALT) // demp2 always messes them up real good
	{
		vec3_t end_pos = { 0 };
		trace_t trace = { qfalse };

		VectorSet(end_pos, self->SHR_currentOrigin[0], self->SHR_currentOrigin[1], self->SHR_currentOrigin[2] - 128);
		SHR_Trace(&trace, self->SHR_currentOrigin, NULL, NULL, end_pos, self->s.number, MASK_SOLID);

		if (trace.fraction == 1.0f || pain_mod == MOD_DEMP2) // demp2 always does this
		{
#ifdef SP_GAME // MP has no clientInfo.headModel
			if (self->client->clientInfo.headModel != 0)
			{
				vec3_t origin = { 0 };

				VectorCopy(self->SHR_currentOrigin, origin);
				origin[2] += 50;
				//				G_PlayEffect( "small_chunks", origin );
				SHR_PlayEffectUp("chunks/probehead", origin);
				SHR_PlayEffectUp("env/med_explode2", origin);
				self->client->clientInfo.headModel = 0;
				self->client->moveType = MT_RUNJUMP;
				self->client->ps.gravity = g_gravity->value * .1;
			}
#endif


			if ((pain_mod == MOD_DEMP2 || pain_mod == MOD_DEMP2_ALT) && attacker)
			{
				vec3_t dir = { 0 };

				NPC_SetAnim(self, SETANIM_BOTH, BOTH_PAIN1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

				VectorSubtract(self->SHR_currentOrigin, attacker->SHR_currentOrigin, dir);
				VectorNormalize(dir);

				VectorMA(self->client->ps.velocity, 550, dir, self->client->ps.velocity);
				self->client->ps.velocity[2] -= 127;
			}

#ifdef SP_GAME
			self->s.powerups |= 1 << PW_SHOCKED;
			self->client->ps.powerups[PW_SHOCKED] = level.time + 3000;
#else // MP shows the shock through electrifyTime
			self->client->ps.electrifyTime = level.time + 3000;
#endif

			self->NPC->localState = LSTATE_DROP;
		}
	}
	else
	{
		const float pain_chance = NPC_GetPainChance(self, damage);

		if (Q_flrand(0.0f, 1.0f) < pain_chance) // Spin around in pain?
		{
			NPC_SetAnim(self, SETANIM_BOTH, BOTH_PAIN1, SETANIM_FLAG_OVERRIDE);
		}
	}

	NPC_Pain(SHR_PAIN_PASS);
}

/*
-------------------------
ImperialProbe_Idle
-------------------------
*/

void ImperialProbe_Idle(void)
{
	ImperialProbe_MaintainHeight();

	NPC_BSIdle();
}

/*
-------------------------
NPC_BSImperialProbe_Patrol
-------------------------
*/
static void ImperialProbe_Patrol(void)
{
	ImperialProbe_MaintainHeight();

	if (NPC_CheckPlayerTeamStealth())
	{
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	//If we have somewhere to go, then do that
	if (!NPCS.NPC->enemy)
	{
		NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_RUN1, SETANIM_FLAG_NORMAL);

		if (UpdateGoal())
		{
			//start loop sound once we move
			NPCS.NPC->s.loopSound = G_SoundIndex("sound/chars/probe/misc/probedroidloop");
			NPCS.ucmd.buttons |= BUTTON_WALKING;
			NPC_MoveToGoal(qtrue);
		}
		//randomly talk
		if (TIMER_Done(NPCS.NPC, "patrolNoise"))
		{
			SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, va("sound/chars/probe/misc/probetalk%d", Q_irand(1, 3)));

			TIMER_Set(NPCS.NPC, "patrolNoise", Q_irand(2000, 4000));
		}
	}
	else // He's got an enemy. Make him angry.
	{
		SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, "sound/chars/probe/misc/anger1");
		TIMER_Set(NPCS.NPC, "angerNoise", Q_irand(2000, 4000));
		//NPCS.NPCInfo->behaviorState = BS_HUNT_AND_KILL;
	}

	NPC_UpdateAngles(qtrue, qtrue);
}

/*
-------------------------
ImperialProbe_Wait
-------------------------
*/
static void ImperialProbe_Wait(void)
{
	if (NPCS.NPCInfo->localState == LSTATE_DROP)
	{
		vec3_t end_pos = { 0 };
		trace_t trace = { qfalse };

		NPCS.NPCInfo->desiredYaw = AngleNormalize360(NPCS.NPCInfo->desiredYaw + 25);

		VectorSet(end_pos, NPCS.NPC->SHR_currentOrigin[0], NPCS.NPC->SHR_currentOrigin[1], NPCS.NPC->SHR_currentOrigin[2] - 32);
		SHR_Trace(&trace, NPCS.NPC->SHR_currentOrigin, NULL, NULL, end_pos, NPCS.NPC->s.number, MASK_SOLID);

		if (trace.fraction != 1.0f)
		{
			G_Damage(NPCS.NPC, NPCS.NPC->enemy, NPCS.NPC->enemy, NULL, NULL, 2000, 0, MOD_UNKNOWN);
		}
	}

	NPC_UpdateAngles(qtrue, qtrue);
}

/*
-------------------------
NPC_BSImperialProbe_Default
-------------------------
*/
void NPC_BSImperialProbe_Default(void)
{
	if (NPCS.NPC->enemy)
	{
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
		ImperialProbe_AttackDecision();
	}
	else if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
	{
		ImperialProbe_Patrol();
	}
	else if (NPCS.NPCInfo->localState == LSTATE_DROP)
	{
		ImperialProbe_Wait();
	}
	else
	{
		ImperialProbe_Idle();
	}
}