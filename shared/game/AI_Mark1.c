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

#ifndef BOWCASTER_VELOCITY
#define BOWCASTER_VELOCITY			1600
#define BOWCASTER_SIZE				2
#define BOWCASTER_SPLASH_DAMAGE		0
#define BOWCASTER_SPLASH_RADIUS		0
#endif

#define MIN_MELEE_RANGE 320
#define	MIN_MELEE_RANGE_SQR			( MIN_MELEE_RANGE * MIN_MELEE_RANGE )

#define MIN_DISTANCE 128
#define MIN_DISTANCE_SQR			( MIN_DISTANCE * MIN_DISTANCE )

#define TURN_OFF 0x00000100

#define LEFT_ARM_HEALTH 40
#define RIGHT_ARM_HEALTH 40
#define AMMO_POD_HEALTH 40

//Local state enums
enum
{
	LSTATE_NONE = 0,
	LSTATE_ASLEEP,
	LSTATE_WAKEUP,
	LSTATE_FIRED0,
	LSTATE_FIRED1,
	LSTATE_FIRED2,
	LSTATE_FIRED3,
	LSTATE_FIRED4,
};

gentity_t* CreateMissile(vec3_t org, vec3_t dir, float vel, int life, gentity_t* owner, qboolean alt_fire);
void Mark1_BlasterAttack(qboolean advance);
void DeathFX(const gentity_t* ent);
extern gitem_t* SHR_FindItemForAmmo(ammo_t ammo);

/*
-------------------------
NPC_Mark1_Precache
-------------------------
*/
void NPC_Mark1_Precache(void)
{
	G_SoundIndex("sound/chars/mark1/misc/mark1_wakeup");
	G_SoundIndex("sound/chars/mark1/misc/shutdown");
	G_SoundIndex("sound/chars/mark1/misc/walk");
	G_SoundIndex("sound/chars/mark1/misc/run");
	G_SoundIndex("sound/chars/mark1/misc/death1");
	G_SoundIndex("sound/chars/mark1/misc/death2");
	G_SoundIndex("sound/chars/mark1/misc/anger");
	G_SoundIndex("sound/chars/mark1/misc/mark1_fire");
	G_SoundIndex("sound/chars/mark1/misc/mark1_pain");
	G_SoundIndex("sound/chars/mark1/misc/mark1_explo");

	G_EffectIndex("env/med_explode2");
	G_EffectIndex("explosions/probeexplosion1");
	G_EffectIndex("blaster/smoke_bolton");
	G_EffectIndex("bryar_old/muzzle_flash");
	G_EffectIndex("explosions/droidexplosion1");

	RegisterItem(SHR_FindItemForAmmo(AMMO_METAL_BOLTS));
	RegisterItem(SHR_FindItemForAmmo(AMMO_BLASTER));
	RegisterItem(SHR_FindItemForWeapon(WP_BOWCASTER));
	RegisterItem(SHR_FindItemForWeapon(WP_BRYAR_PISTOL));
}

/*
-------------------------
NPC_Mark1_Part_Explode
-------------------------
*/
static void NPC_Mark1_Part_Explode(gentity_t* self, const int bolt)
{
	if (bolt >= 0)
	{
		mdxaBone_t boltMatrix = { 0 };
		vec3_t org = { 0 }, dir = { 0 };

		SHR_GetBoltMatrix(self, bolt, boltMatrix, self->SHR_currentAngles, self->SHR_currentOrigin);

		SHR_GiveMeVector(boltMatrix, ORIGIN, org);
		SHR_GiveMeVector(boltMatrix, NEGATIVE_Y, dir);

		SHR_PlayEffect("env/med_explode2", org, dir);
		SHR_PlayEffectBolted("blaster/smoke_bolton", self, bolt, org, dir);
	}
}

/*
-------------------------
Mark1_Idle
-------------------------
*/
void Mark1_Idle(void)
{
	NPC_BSIdle();

	NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_SLEEP1, SETANIM_FLAG_NORMAL);
}

/*
-------------------------
Mark1Dead_FireRocket
- Shoot the left weapon, the multi-blaster
-------------------------
*/
void Mark1Dead_FireRocket(void)
{
	mdxaBone_t boltMatrix = { 0 };
	vec3_t muzzle1 = { 0 }, muzzle_dir = { 0 };

	const int damage = 50;

	SHR_GetBoltMatrix(NPCS.NPC, SHR_Bolt(NPCS.NPC, genericBolt5, "*flash5"), boltMatrix, NPCS.NPC->SHR_currentAngles, NPCS.NPC->SHR_currentOrigin);

	SHR_GiveMeVector(boltMatrix, ORIGIN, muzzle1);
	SHR_GiveMeVector(boltMatrix, NEGATIVE_Y, muzzle_dir);

	SHR_PlayEffect("bryar/muzzle_flash", muzzle1, muzzle_dir);

	SHR_Sound(NPCS.NPC, G_SoundIndex("sound/chars/mark1/misc/mark1_fire"));

	gentity_t* missile = CreateMissile(muzzle1, muzzle_dir, BOWCASTER_VELOCITY, 10000, NPCS.NPC, qfalse);

	missile->classname = "bowcaster_proj";
	missile->s.weapon = WP_BOWCASTER;

	VectorSet(missile->SHR_maxs, BOWCASTER_SIZE, BOWCASTER_SIZE, BOWCASTER_SIZE);
	VectorScale(missile->SHR_maxs, -1, missile->SHR_mins);

	missile->damage = damage;
	missile->dflags = DAMAGE_DEATH_KNOCKBACK | DAMAGE_EXTRA_KNOCKBACK;
	missile->methodOfDeath = SHR_MOD_ENERGY;
	missile->clipmask = MASK_SHOT;
	missile->splashDamage = BOWCASTER_SPLASH_DAMAGE;
	missile->splashRadius = BOWCASTER_SPLASH_RADIUS;

	// we don't want it to bounce
	missile->bounceCount = 0;
}

/*
-------------------------
Mark1Dead_FireBlaster
- Shoot the left weapon, the multi-blaster
-------------------------
*/
void Mark1Dead_FireBlaster(void)
{
	vec3_t muzzle1 = { 0 }, muzzle_dir = { 0 };
	mdxaBone_t boltMatrix = { 0 };

	const int bolt = SHR_Bolt(NPCS.NPC, genericBolt1, "*flash1");

	SHR_GetBoltMatrix(NPCS.NPC, bolt, boltMatrix, NPCS.NPC->SHR_currentAngles, NPCS.NPC->SHR_currentOrigin);

	SHR_GiveMeVector(boltMatrix, ORIGIN, muzzle1);
	SHR_GiveMeVector(boltMatrix, NEGATIVE_Y, muzzle_dir);

	SHR_PlayEffect("bryar_old/muzzle_flash", muzzle1, muzzle_dir);

	gentity_t* missile = CreateMissile(muzzle1, muzzle_dir, 1600, 10000, NPCS.NPC, qfalse);

	SHR_Sound(NPCS.NPC, G_SoundIndex("sound/chars/mark1/misc/mark1_fire"));

	missile->classname = "bryar_proj";
	missile->s.weapon = WP_BRYAR_PISTOL;

	missile->damage = 1;
	missile->dflags = DAMAGE_DEATH_KNOCKBACK | DAMAGE_EXTRA_KNOCKBACK;
	missile->methodOfDeath = SHR_MOD_ENERGY;
	missile->clipmask = MASK_SHOT;
}

/*
-------------------------
Mark1_die
-------------------------
*/
void Mark1_die(gentity_t* self, gentity_t* inflictor, gentity_t* attacker, int damage, int mod, int dflags, int hit_loc)
{
	SHR_Sound(self, G_SoundIndex(va("sound/chars/mark1/misc/death%d.wav", Q_irand(1, 2))));

	// Choose a death anim
	if (Q_irand(1, 10) > 5)
	{
		NPC_SetAnim(self, SETANIM_BOTH, BOTH_DEATH2, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	}
	else
	{
		NPC_SetAnim(self, SETANIM_BOTH, BOTH_DEATH1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	}
}

/*
-------------------------
Mark1_dying
-------------------------
*/
void Mark1_dying(gentity_t* self)
{
	if (self->client->ps.SHR_torsoAnimTimer > 0)
	{
		if (TIMER_Done(self, "dyingExplosion"))
		{
			int new_bolt = 0;
			int num = Q_irand(1, 3);

			// Find place to generate explosion
			if (num == 1)
			{
				num = Q_irand(8, 10);
				new_bolt = SHR_AddBolt(self, va("*flash%d", num));
				NPC_Mark1_Part_Explode(self, new_bolt);
			}
			else
			{
				num = Q_irand(1, 6);
				new_bolt = SHR_AddBolt(self, va("*torso_tube%d", num));
				NPC_Mark1_Part_Explode(self, new_bolt);
				SHR_SetSurfaceOnOff(self, va("torso_tube%d", num), TURN_OFF);
			}

			TIMER_Set(self, "dyingExplosion", Q_irand(300, 1000));
		}
		// Randomly fire blaster
		if (!SHR_GetSurfaceRenderStatus(self, "l_arm"))
			// Is the blaster still on the model?
		{
			if (Q_irand(1, 5) == 1)
			{
				SaveNPCGlobals();
				SetNPCGlobals(self);
				Mark1Dead_FireBlaster();
				RestoreNPCGlobals();
			}
		}

		// Randomly fire rocket
		if (!SHR_GetSurfaceRenderStatus(self, "r_arm"))
			// Is the rocket still on the model?
		{
			if (Q_irand(1, 10) == 1)
			{
				SaveNPCGlobals();
				SetNPCGlobals(self);
				Mark1Dead_FireRocket();
				RestoreNPCGlobals();
			}
		}
	}
}

/*
-------------------------
NPC_Mark1_Pain
- look at what was hit and see if it should be removed from the model.
-------------------------
*/
void NPC_Mark1_Pain(SHR_PAIN_PARAMS)
{
	int new_bolt = 0;

	NPC_Pain(SHR_PAIN_PASS);

	SHR_Sound(self, G_SoundIndex("sound/chars/mark1/misc/mark1_pain"));

	// Hit in the CHEST???
	if (SHR_PAIN_HITLOC == HL_CHEST)
	{
		const int chance = Q_irand(1, 4);

		if (chance == 1 && damage > 5)
		{
			NPC_SetAnim(self, SETANIM_BOTH, BOTH_PAIN1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		}
	}
	// Hit in the left arm?
	else if (SHR_PAIN_HITLOC == HL_ARM_LT && self->locationDamage[HL_ARM_LT] > LEFT_ARM_HEALTH)
	{
		if (self->locationDamage[SHR_PAIN_HITLOC] >= LEFT_ARM_HEALTH) // Blow it up?
		{
			new_bolt = SHR_AddBolt(self, "*flash3");
			if (new_bolt != -1)
			{
				NPC_Mark1_Part_Explode(self, new_bolt);
			}

			SHR_SetSurfaceOnOff(self, "l_arm", TURN_OFF);
		}
	}
	// Hit in the right arm?
	else if (SHR_PAIN_HITLOC == HL_ARM_RT && self->locationDamage[HL_ARM_RT] > RIGHT_ARM_HEALTH) // Blow it up?
	{
		if (self->locationDamage[SHR_PAIN_HITLOC] >= RIGHT_ARM_HEALTH)
		{
			new_bolt = SHR_AddBolt(self, "*flash4");
			if (new_bolt != -1)
			{
				//				G_PlayEffect( "small_chunks", self->playerModel, self->genericBolt2, self->s.number);
				NPC_Mark1_Part_Explode(self, new_bolt);
			}

			SHR_SetSurfaceOnOff(self, "r_arm", TURN_OFF);
		}
	}
	// Check ammo pods
	else
	{
		for (int i = 0; i < 6; i++)
		{
			if (SHR_PAIN_HITLOC == HL_GENERIC1 + i && self->locationDamage[HL_GENERIC1 + i] > AMMO_POD_HEALTH) // Blow it up?
			{
				if (self->locationDamage[SHR_PAIN_HITLOC] >= AMMO_POD_HEALTH)
				{
					new_bolt = SHR_AddBolt(self, va("*torso_tube%d", i + 1));
					if (new_bolt != -1)
					{
						NPC_Mark1_Part_Explode(self, new_bolt);
					}
					SHR_SetSurfaceOnOff(self, va("torso_tube%d", i + 1), TURN_OFF);
					NPC_SetAnim(self, SETANIM_BOTH, BOTH_PAIN1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					break;
				}
			}
		}
	}

	// Are both guns shot off?
	if (SHR_GetSurfaceRenderStatus(self, "l_arm") &&
		SHR_GetSurfaceRenderStatus(self, "r_arm"))
	{
		G_Damage(self, NULL, NULL, NULL, NULL, self->health, 0, MOD_UNKNOWN);
	}
}

/*
-------------------------
Mark1_Hunt
- look for enemy.
-------------------------`
*/
void Mark1_Hunt(void)
{
	if (NPCS.NPCInfo->goalEntity == NULL)
	{
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
	}

	NPC_FaceEnemy(qtrue);

	NPCS.NPCInfo->combatMove = qtrue;
	NPC_MoveToGoal(qtrue);
}

/*
-------------------------
Mark1_FireBlaster
- Shoot the left weapon, the multi-blaster
-------------------------
*/
void Mark1_FireBlaster(void)
{
	vec3_t muzzle1 = { 0 };
	static vec3_t forward, vright, up;
	mdxaBone_t boltMatrix = { 0 };
	int bolt = 0;

	// Which muzzle to fire from?
	if (NPCS.NPCInfo->localState <= LSTATE_FIRED0 || NPCS.NPCInfo->localState == LSTATE_FIRED4)
	{
		NPCS.NPCInfo->localState = LSTATE_FIRED1;
		bolt = SHR_Bolt(NPCS.NPC, genericBolt1, "*flash1");
	}
	else if (NPCS.NPCInfo->localState == LSTATE_FIRED1)
	{
		NPCS.NPCInfo->localState = LSTATE_FIRED2;
		bolt = SHR_Bolt(NPCS.NPC, genericBolt2, "*flash2");
	}
	else if (NPCS.NPCInfo->localState == LSTATE_FIRED2)
	{
		NPCS.NPCInfo->localState = LSTATE_FIRED3;
		bolt = SHR_Bolt(NPCS.NPC, genericBolt3, "*flash3");
	}
	else
	{
		NPCS.NPCInfo->localState = LSTATE_FIRED4;
		bolt = SHR_Bolt(NPCS.NPC, genericBolt4, "*flash4");
	}

	SHR_GetBoltMatrix(NPCS.NPC, bolt, boltMatrix, NPCS.NPC->SHR_currentAngles, NPCS.NPC->SHR_currentOrigin);

	SHR_GiveMeVector(boltMatrix, ORIGIN, muzzle1);

	if (NPCS.NPC->health)
	{
		vec3_t angle_to_enemy1 = { 0 };
		vec3_t delta1 = { 0 };
		vec3_t enemy_org1 = { 0 };
		CalcEntitySpot(NPCS.NPC->enemy, SPOT_HEAD, enemy_org1);
		VectorSubtract(enemy_org1, muzzle1, delta1);
		vectoangles(delta1, angle_to_enemy1);
		AngleVectors(angle_to_enemy1, forward, vright, up);
	}
	else
	{
		AngleVectors(NPCS.NPC->SHR_currentAngles, forward, vright, up);
	}

	SHR_PlayEffect("bryar_old/muzzle_flash", muzzle1, forward);

	SHR_Sound(NPCS.NPC, G_SoundIndex("sound/chars/mark1/misc/mark1_fire"));

	gentity_t* missile = CreateMissile(muzzle1, forward, 1600, 10000, NPCS.NPC, qfalse);

	missile->classname = "bryar_proj";
	missile->s.weapon = WP_BRYAR_PISTOL;

	missile->damage = 1;
	missile->dflags = DAMAGE_DEATH_KNOCKBACK | DAMAGE_EXTRA_KNOCKBACK;
	missile->methodOfDeath = SHR_MOD_ENERGY;
	missile->clipmask = MASK_SHOT;
}

/*
-------------------------
Mark1_BlasterAttack
-------------------------
*/
void Mark1_BlasterAttack(const qboolean advance)
{
	if (TIMER_Done(NPCS.NPC, "attackDelay")) // Attack?
	{
		int chance = Q_irand(1, 5);

		NPCS.NPCInfo->burstCount++;

		if (NPCS.NPCInfo->burstCount < 3) // Too few shots this burst?
		{
			chance = 2; // Force it to keep firing.
		}
		else if (NPCS.NPCInfo->burstCount > 12) // Too many shots fired this burst?
		{
			NPCS.NPCInfo->burstCount = 0;
			chance = 1; // Force it to stop firing.
		}

		// Stop firing.
		if (chance == 1)
		{
			NPCS.NPCInfo->burstCount = 0;
			TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(1000, 3000));
			NPCS.NPC->client->ps.SHR_torsoAnimTimer = 0; // Just in case the firing anim is running.
		}
		else
		{
			if (TIMER_Done(NPCS.NPC, "attackDelay2")) // Can't be shooting every frame.
			{
				TIMER_Set(NPCS.NPC, "attackDelay2", Q_irand(50, 50));
				Mark1_FireBlaster();
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ATTACK1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			}
		}
	}
	else if (advance)
	{
		if (NPCS.NPC->client->ps.torsoAnim == BOTH_ATTACK1)
		{
			NPCS.NPC->client->ps.SHR_torsoAnimTimer = 0; // Just in case the firing anim is running.
		}
		Mark1_Hunt();
	}
	else // Make sure he's not firing.
	{
		if (NPCS.NPC->client->ps.torsoAnim == BOTH_ATTACK1)
		{
			NPCS.NPC->client->ps.SHR_torsoAnimTimer = 0; // Just in case the firing anim is running.
		}
	}
}

/*
-------------------------
Mark1_FireRocket
-------------------------
*/
void Mark1_FireRocket(void)
{
	mdxaBone_t boltMatrix = { 0 };
	vec3_t muzzle1 = { 0 }, enemy_org1 = { 0 }, delta1 = { 0 }, angle_to_enemy1 = { 0 };
	static vec3_t forward, vright, up;

	const int damage = 50;

	SHR_GetBoltMatrix(NPCS.NPC, SHR_Bolt(NPCS.NPC, genericBolt5, "*flash5"), boltMatrix, NPCS.NPC->SHR_currentAngles, NPCS.NPC->SHR_currentOrigin);

	SHR_GiveMeVector(boltMatrix, ORIGIN, muzzle1);

	CalcEntitySpot(NPCS.NPC->enemy, SPOT_HEAD, enemy_org1);
	VectorSubtract(enemy_org1, muzzle1, delta1);
	vectoangles(delta1, angle_to_enemy1);
	AngleVectors(angle_to_enemy1, forward, vright, up);

	SHR_Sound(NPCS.NPC, G_SoundIndex("sound/chars/mark1/misc/mark1_fire"));

	gentity_t* missile = CreateMissile(muzzle1, forward, BOWCASTER_VELOCITY, 10000, NPCS.NPC, qfalse);

	missile->classname = "bowcaster_proj";
	missile->s.weapon = WP_BOWCASTER;

	VectorSet(missile->SHR_maxs, BOWCASTER_SIZE, BOWCASTER_SIZE, BOWCASTER_SIZE);
	VectorScale(missile->SHR_maxs, -1, missile->SHR_mins);

	missile->damage = damage;
	missile->dflags = DAMAGE_DEATH_KNOCKBACK | DAMAGE_EXTRA_KNOCKBACK;
	missile->methodOfDeath = SHR_MOD_ENERGY;
	missile->clipmask = MASK_SHOT;
	missile->splashDamage = BOWCASTER_SPLASH_DAMAGE;
	missile->splashRadius = BOWCASTER_SPLASH_RADIUS;

	// we don't want it to bounce
	missile->bounceCount = 0;
}

/*
-------------------------
Mark1_RocketAttack
-------------------------
*/
void Mark1_RocketAttack(const qboolean advance)
{
	if (TIMER_Done(NPCS.NPC, "attackDelay")) // Attack?
	{
		TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(1000, 3000));
		NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, BOTH_ATTACK2, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		Mark1_FireRocket();
	}
	else if (advance)
	{
		Mark1_Hunt();
	}
}

/*
-------------------------
Mark1_AttackDecision
-------------------------
*/
void Mark1_AttackDecision(void)
{
	//randomly talk
	if (TIMER_Done(NPCS.NPC, "patrolNoise"))
	{
		if (TIMER_Done(NPCS.NPC, "angerNoise"))
		{
			//			G_Sound( NPC, G_SoundIndex(va("sound/chars/mark1/misc/talk%d.wav",	Q_irand(1, 4))));
			TIMER_Set(NPCS.NPC, "patrolNoise", Q_irand(4000, 10000));
		}
	}

	// Enemy is dead or he has no enemy.
	if (NPCS.NPC->enemy->health < 1 || NPC_CheckEnemyExt(qfalse) == qfalse)
	{
		NPCS.NPC->enemy = NULL;
		return;
	}

	// Rate our distance to the target and visibility
	const float distance = (int)(DistanceHorizontalSquared(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->enemy->SHR_currentOrigin));
	distance_e dist_rate = distance > MIN_MELEE_RANGE_SQR ? DIST_LONG : DIST_MELEE;
	const qboolean visible = SHR_ClearLOSEnt(NPCS.NPC->enemy);
	const qboolean advance = (qboolean)(distance > MIN_DISTANCE_SQR);

	// If we cannot see our target, move to see it
	if (!visible || !NPC_FaceEnemy(qtrue))
	{
		Mark1_Hunt();
		return;
	}

	// See if the side weapons are there
	const int blasterTest = SHR_GetSurfaceRenderStatus(NPCS.NPC, "l_arm");
	const int rocketTest = SHR_GetSurfaceRenderStatus(NPCS.NPC, "r_arm");

	// It has both side weapons
	if (!blasterTest && !rocketTest)
	{
	}
	else if (blasterTest)
	{
		dist_rate = DIST_LONG;
	}
	else if (rocketTest)
	{
		dist_rate = DIST_MELEE;
	}
	else // It should never get here, but just in case
	{
		NPCS.NPC->health = 0;
		NPCS.NPC->client->ps.stats[STAT_HEALTH] = 0;
		SHR_Die(NPCS.NPC, NPCS.NPC, NPCS.NPC, 100, MOD_UNKNOWN);
	}

	// We can see enemy so shoot him if timers let you.
	NPC_FaceEnemy(qtrue);

	if (dist_rate == DIST_MELEE)
	{
		Mark1_BlasterAttack(advance);
	}
	else if (dist_rate == DIST_LONG)
	{
		Mark1_RocketAttack(advance);
	}
}

/*
-------------------------
Mark1_Patrol
-------------------------
*/
void Mark1_Patrol(void)
{
	if (NPC_CheckPlayerTeamStealth())
	{
		SHR_Sound(NPCS.NPC, G_SoundIndex("sound/chars/mark1/misc/mark1_wakeup"));
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	//If we have somewhere to go, then do that
	if (!NPCS.NPC->enemy)
	{
		if (UpdateGoal())
		{
			NPCS.ucmd.buttons |= BUTTON_WALKING;
			NPC_MoveToGoal(qtrue);
			NPC_UpdateAngles(qtrue, qtrue);
		}
	}
}

/*
-------------------------
NPC_BSMark1_Default
-------------------------
*/
void NPC_BSMark1_Default(void)
{
	if (NPCS.NPC->enemy)
	{
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
		Mark1_AttackDecision();
	}
	else if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
	{
		Mark1_Patrol();
	}
	else
	{
		Mark1_Idle();
	}
}