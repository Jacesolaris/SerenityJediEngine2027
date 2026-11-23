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

#define MIN_DISTANCE 256
#define MIN_DISTANCE_SQR	( MIN_DISTANCE * MIN_DISTANCE )

#define SENTRY_FORWARD_BASE_SPEED 10
#define SENTRY_FORWARD_MULTIPLIER 5

#define SENTRY_VELOCITY_DECAY 0.85f
#define SENTRY_STRAFE_VEL 256
#define SENTRY_STRAFE_DIS 200
#define SENTRY_UPWARD_PUSH 32
#define SENTRY_HOVER_HEIGHT 24

//Local state enums
enum
{
	LSTATE_NONE = 0,
	LSTATE_ASLEEP,
	LSTATE_WAKEUP,
	LSTATE_ACTIVE,
	LSTATE_POWERING_UP,
	LSTATE_ATTACKING,
};

/*
-------------------------
NPC_Sentry_Precache
-------------------------
*/
void NPC_Sentry_Precache(void)
{
	G_SoundIndex("sound/chars/sentry/misc/sentry_explo");
	G_SoundIndex("sound/chars/sentry/misc/sentry_pain");
	G_SoundIndex("sound/chars/sentry/misc/sentry_shield_open");
	G_SoundIndex("sound/chars/sentry/misc/sentry_shield_close");
	G_SoundIndex("sound/chars/sentry/misc/sentry_hover_1_lp");
	G_SoundIndex("sound/chars/sentry/misc/sentry_hover_2_lp");
	G_SoundIndex("sound/chars/sentry/misc/hover");

	for (int i = 1; i < 4; i++)
	{
		G_SoundIndex(va("sound/chars/sentry/misc/talk%d", i));
	}

	G_EffectIndex("bryar/muzzle_flash");
	G_EffectIndex("env/med_explode");

	RegisterItem(SHR_FindItemForAmmo(AMMO_BLASTER));
}

/*
================
sentry_use
================
*/
void sentry_use(gentity_t* self, gentity_t* other, gentity_t* activator)
{
	G_ActivateBehavior(self, BSET_USE);

	self->flags &= ~FL_SHIELDED;
	NPC_SetAnim(self, SETANIM_BOTH, BOTH_POWERUP1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	//	self->NPC->localState = LSTATE_WAKEUP;
	self->NPC->localState = LSTATE_ACTIVE;
}

/*
-------------------------
NPC_Sentry_Pain
-------------------------
*/
void NPC_Sentry_Pain(SHR_PAIN_PARAMS)
{
	NPC_Pain(SHR_PAIN_PASS);

	if (SHR_PAIN_MOD == MOD_DEMP2 || SHR_PAIN_MOD == MOD_DEMP2_ALT)
	{
		self->NPC->burstCount = 0;
		TIMER_Set(self, "attackDelay", Q_irand(9000, 12000));
		self->flags |= FL_SHIELDED;
		NPC_SetAnim(self, SETANIM_BOTH, BOTH_FLY_SHIELDED, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		SHR_SoundOnEnt(self, CHAN_AUTO, "sound/chars/sentry/misc/sentry_pain");

		self->NPC->localState = LSTATE_ACTIVE;
	}
}

/*
-------------------------
Sentry_Fire
-------------------------
*/
static void Sentry_Fire(void)
{
	vec3_t muzzle;
	static vec3_t forward, vright, up;
	mdxaBone_t boltMatrix;
	int bolt = 0;

	NPCS.NPC->flags &= ~FL_SHIELDED;

	if (NPCS.NPCInfo->localState == LSTATE_POWERING_UP)
	{
		if (TIMER_Done(NPCS.NPC, "powerup"))
		{
			NPCS.NPCInfo->localState = LSTATE_ATTACKING;
			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ATTACK1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		}
		else
		{
			// can't do anything right now
			return;
		}
	}
	else if (NPCS.NPCInfo->localState == LSTATE_ACTIVE)
	{
		NPCS.NPCInfo->localState = LSTATE_POWERING_UP;

		SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, "sound/chars/sentry/misc/sentry_shield_open");
		NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_POWERUP1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		TIMER_Set(NPCS.NPC, "powerup", 250);
		return;
	}
	else if (NPCS.NPCInfo->localState != LSTATE_ATTACKING)
	{
		// bad because we are uninitialized
		NPCS.NPCInfo->localState = LSTATE_ACTIVE;
		return;
	}

	// Which muzzle to fire from?
	const int which = NPCS.NPCInfo->burstCount % 3;
	switch (which)
	{
	case 0:
		bolt = SHR_Bolt(NPCS.NPC, genericBolt1, "*flash1");
		break;
	case 1:
		bolt = SHR_Bolt(NPCS.NPC, genericBolt2, "*flash2");
		break;
	case 2:
	default:
		bolt = SHR_Bolt(NPCS.NPC, genericBolt3, "*flash03");
	}

	SHR_GetBoltMatrix(NPCS.NPC, bolt, boltMatrix, NPCS.NPC->SHR_currentAngles, NPCS.NPC->SHR_currentOrigin);

	SHR_GiveMeVector(boltMatrix, ORIGIN, muzzle);

	AngleVectors(NPCS.NPC->SHR_currentAngles, forward, vright, up);

	SHR_PlayEffect("bryar_old/muzzle_flash", muzzle, forward);

	gentity_t* missile = CreateMissile(muzzle, forward, 1600, 10000, NPCS.NPC, qfalse);

	missile->classname = "bryar_proj";
	missile->s.weapon = WP_BRYAR_PISTOL;

	missile->dflags = DAMAGE_DEATH_KNOCKBACK | DAMAGE_EXTRA_KNOCKBACK;
	missile->methodOfDeath = SHR_MOD_ENERGY;
	missile->clipmask = MASK_SHOT;

	NPCS.NPCInfo->burstCount++;
	NPCS.NPC->attackDebounceTime = level.time + 50;
	missile->damage = 5;

	// now scale for difficulty
	if (SHR_SPSKILL == 0)
	{
		NPCS.NPC->attackDebounceTime += 200;
		missile->damage = 1;
	}
	else if (SHR_SPSKILL == 1)
	{
		NPCS.NPC->attackDebounceTime += 100;
		missile->damage = 3;
	}
}

/*
-------------------------
Sentry_MaintainHeight
-------------------------
*/
static void Sentry_MaintainHeight(void)
{
	float dif = 0.0f;

	NPCS.NPC->s.loopSound = G_SoundIndex("sound/chars/sentry/misc/sentry_hover_1_lp");

	// Update our angles regardless
	NPC_UpdateAngles(qtrue, qtrue);

	// If we have an enemy, we should try to hover at about enemy eye level
	if (NPCS.NPC->enemy)
	{
		// Find the height difference
		dif = NPCS.NPC->enemy->SHR_currentOrigin[2] + NPCS.NPC->enemy->SHR_maxs[2] - NPCS.NPC->SHR_currentOrigin[2];

		// cap to prevent dramatic height shifts
		if (fabs(dif) > 8)
		{
			if (fabs(dif) > SENTRY_HOVER_HEIGHT)
			{
				dif = dif < 0 ? -24 : 24;
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

			if (fabs(dif) > SENTRY_HOVER_HEIGHT)
			{
				NPCS.ucmd.upmove = NPCS.ucmd.upmove < 0 ? -4 : 4;
			}
			else
			{
				if (NPCS.NPC->client->ps.velocity[2])
				{
					NPCS.NPC->client->ps.velocity[2] *= SENTRY_VELOCITY_DECAY;

					if (fabs(NPCS.NPC->client->ps.velocity[2]) < 2)
					{
						NPCS.NPC->client->ps.velocity[2] = 0;
					}
				}
			}
		}
		// Apply friction to Z
		else if (NPCS.NPC->client->ps.velocity[2])
		{
			NPCS.NPC->client->ps.velocity[2] *= SENTRY_VELOCITY_DECAY;

			if (fabs(NPCS.NPC->client->ps.velocity[2]) < 1)
			{
				NPCS.NPC->client->ps.velocity[2] = 0;
			}
		}
	}

	// Apply friction
	if (NPCS.NPC->client->ps.velocity[0])
	{
		NPCS.NPC->client->ps.velocity[0] *= SENTRY_VELOCITY_DECAY;

		if (fabs(NPCS.NPC->client->ps.velocity[0]) < 1)
		{
			NPCS.NPC->client->ps.velocity[0] = 0;
		}
	}

	if (NPCS.NPC->client->ps.velocity[1])
	{
		NPCS.NPC->client->ps.velocity[1] *= SENTRY_VELOCITY_DECAY;

		if (fabs(NPCS.NPC->client->ps.velocity[1]) < 1)
		{
			NPCS.NPC->client->ps.velocity[1] = 0;
		}
	}

	NPC_FaceEnemy(qtrue);
}

/*
-------------------------
Sentry_Idle
-------------------------
*/
static void Sentry_Idle(void)
{
	Sentry_MaintainHeight();

	// Is he waking up?
	if (NPCS.NPCInfo->localState == LSTATE_WAKEUP)
	{
		if (NPCS.NPC->client->ps.SHR_torsoAnimTimer <= 0)
		{
			NPCS.NPCInfo->scriptFlags |= SCF_LOOK_FOR_ENEMIES;
			NPCS.NPCInfo->burstCount = 0;
		}
	}
	else
	{
		NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_SLEEP1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		NPCS.NPC->flags |= FL_SHIELDED;

		NPC_BSIdle();
	}
}

/*
-------------------------
Sentry_Strafe
-------------------------
*/
static void Sentry_Strafe(void)
{
	vec3_t end, right;
	trace_t tr;

	AngleVectors(NPCS.NPC->client->renderInfo.eyeAngles, NULL, right, NULL);

	// Pick a random strafe direction, then check to see if doing a strafe would be
	//	reasonable valid
	const int dir = rand() & 1 ? -1 : 1;
	VectorMA(NPCS.NPC->SHR_currentOrigin, SENTRY_STRAFE_DIS * dir, right, end);

	SHR_Trace(&tr, NPCS.NPC->SHR_currentOrigin, NULL, NULL, end, NPCS.NPC->s.number, MASK_SOLID);

	// Close enough
	if (tr.fraction > 0.9f)
	{
		VectorMA(NPCS.NPC->client->ps.velocity, SENTRY_STRAFE_VEL * dir, right, NPCS.NPC->client->ps.velocity);

		// Add a slight upward push
		NPCS.NPC->client->ps.velocity[2] += SENTRY_UPWARD_PUSH;

		// Set the strafe start time so we can do a controlled roll
		NPCS.NPC->fx_time = level.time;
		NPCS.NPCInfo->standTime = level.time + 3000 + Q_flrand(0.0f, 1.0f) * 500;

		// Play the hover sound effect (but only sometimes)
		if (Q_irand(1, 2) == 1)
		{
			SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, "sound/chars/sentry/misc/hover");
		}
	}
}

/*
-------------------------
Sentry_Hunt
-------------------------
*/
static void Sentry_Hunt(const qboolean visible, const qboolean advance)
{
	vec3_t forward;

	//If we're not supposed to stand still, pursue the player
	if (NPCS.NPCInfo->standTime < level.time)
	{
		// Only strafe when we can see the player
		if (visible)
		{
			Sentry_Strafe();
			return;
		}
	}

	//If we don't want to advance, stop here
	if (!advance && visible)
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

	const float speed = SENTRY_FORWARD_BASE_SPEED + SENTRY_FORWARD_MULTIPLIER * SHR_SPSKILL;
	VectorMA(NPCS.NPC->client->ps.velocity, speed, forward, NPCS.NPC->client->ps.velocity);
}

/*
-------------------------
Sentry_RangedAttack
-------------------------
*/
static void Sentry_RangedAttack(const qboolean visible, const qboolean advance)
{
	if (TIMER_Done(NPCS.NPC, "attackDelay") && NPCS.NPC->attackDebounceTime < level.time && visible) // Attack?
	{
		if (NPCS.NPCInfo->burstCount > 6)
		{
			if (!NPCS.NPC->fly_sound_debounce_time)
			{
				//delay closing down to give the player an opening
				NPCS.NPC->fly_sound_debounce_time = level.time + Q_irand(500, 2000);
			}
			else if (NPCS.NPC->fly_sound_debounce_time < level.time)
			{
				NPCS.NPCInfo->localState = LSTATE_ACTIVE;
				NPCS.NPC->fly_sound_debounce_time = NPCS.NPCInfo->burstCount = 0;
				TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(2000, 3500));
				NPCS.NPC->flags |= FL_SHIELDED;
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_FLY_SHIELDED, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, "sound/chars/sentry/misc/sentry_shield_close");
			}
		}
		else
		{
			Sentry_Fire();
		}
	}

	if (NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES)
	{
		Sentry_Hunt(visible, advance);
	}
}

/*
-------------------------
Sentry_AttackDecision
-------------------------
*/
static void Sentry_AttackDecision(void)
{
	// Always keep a good height off the ground
	Sentry_MaintainHeight();

	NPCS.NPC->s.loopSound = G_SoundIndex("sound/chars/sentry/misc/sentry_hover_2_lp");

	//randomly talk
	if (TIMER_Done(NPCS.NPC, "patrolNoise"))
	{
		if (TIMER_Done(NPCS.NPC, "angerNoise"))
		{
			SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, va("sound/chars/sentry/misc/talk%d", Q_irand(1, 3)));

			TIMER_Set(NPCS.NPC, "patrolNoise", Q_irand(4000, 10000));
		}
	}

	// He's dead.
	if (NPCS.NPC->enemy->health < 1)
	{
		NPCS.NPC->enemy = NULL;
		Sentry_Idle();
		return;
	}

	// If we don't have an enemy, just idle
	if (NPC_CheckEnemyExt(qfalse) == qfalse)
	{
		Sentry_Idle();
		return;
	}

	// Rate our distance to the target and visibilty
	const float distance = (int)(DistanceHorizontalSquared(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->enemy->SHR_currentOrigin));
	const qboolean visible = SHR_ClearLOSEnt(NPCS.NPC->enemy);
	const qboolean advance = (qboolean)(distance > MIN_DISTANCE_SQR);

	// If we cannot see our target, move to see it
	if (visible == qfalse)
	{
		if (NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES)
		{
			Sentry_Hunt(visible, advance);
			return;
		}
	}

	NPC_FaceEnemy(qtrue);

	Sentry_RangedAttack(visible, advance);
}

/*
-------------------------
NPC_Sentry_Patrol
-------------------------
*/
static void NPC_Sentry_Patrol(void)
{
	Sentry_MaintainHeight();

	//If we have somewhere to go, then do that
	if (!NPCS.NPC->enemy)
	{
		if (NPC_CheckPlayerTeamStealth())
		{
			NPC_UpdateAngles(qtrue, qtrue);
			return;
		}

		if (UpdateGoal())
		{
			//start loop sound once we move
			NPCS.ucmd.buttons |= BUTTON_WALKING;
			NPC_MoveToGoal(qtrue);
		}

		//randomly talk
		if (TIMER_Done(NPCS.NPC, "patrolNoise"))
		{
			SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, va("sound/chars/sentry/misc/talk%d", Q_irand(1, 3)));

			TIMER_Set(NPCS.NPC, "patrolNoise", Q_irand(2000, 4000));
		}
	}

	NPC_UpdateAngles(qtrue, qtrue);
}

/*
-------------------------
NPC_BSSentry_Default
-------------------------
*/
void NPC_BSSentry_Default(void)
{
	if (NPCS.NPC->targetname)
	{
		SHR_SET_USE(NPCS.NPC, sentry_use);
	}

	if (NPCS.NPC->enemy && NPCS.NPCInfo->localState != LSTATE_WAKEUP)
	{
		// Don't attack if waking up or if no enemy
		Sentry_AttackDecision();
	}
	else if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
	{
		NPC_Sentry_Patrol();
	}
	else
	{
		Sentry_Idle();
	}
}