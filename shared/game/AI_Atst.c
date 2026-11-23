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

#define MIN_MELEE_RANGE 640
#define	MIN_MELEE_RANGE_SQR	( MIN_MELEE_RANGE * MIN_MELEE_RANGE )

#define MIN_DISTANCE 128
#define MIN_DISTANCE_SQR	( MIN_DISTANCE * MIN_DISTANCE )

#define TURN_OFF 0x00000100 //G2SURFACEFLAG_NODESCENDANTS;

#define LEFT_ARM_HEALTH 40
#define RIGHT_ARM_HEALTH 40

/*
-------------------------
NPC_ATST_Precache
-------------------------
*/
void NPC_ATST_Precache(void)
{
	G_SoundIndex("sound/chars/atst/atst_damaged1");
	G_SoundIndex("sound/chars/atst/atst_damaged2");

#ifdef SP_GAME
	RegisterItem(SHR_FindItemForWeapon(WP_ATST_MAIN)); //precache the weapon
#endif
	RegisterItem(SHR_FindItemForWeapon(WP_BOWCASTER)); //precache the weapon
	RegisterItem(SHR_FindItemForWeapon(WP_ROCKET_LAUNCHER)); //precache the weapon

	G_EffectIndex("env/med_explode2");
	//	G_EffectIndex( "smaller_chunks" );
	G_EffectIndex("blaster/smoke_bolton");
	G_EffectIndex("explosions/droidexplosion1");
}

//-----------------------------------------------------------------
static void ATST_PlayEffect(gentity_t* self, const int boltID, const char* fx)
{
	if (boltID >= 0 && fx && fx[0])
	{
		mdxaBone_t boltMatrix;
		vec3_t org, dir;

		SHR_GetBoltMatrix(self, boltID, boltMatrix, self->SHR_currentAngles, self->SHR_currentOrigin);

		SHR_GiveMeVector(boltMatrix, ORIGIN, org);
		SHR_GiveMeVector(boltMatrix, NEGATIVE_Y, dir);

		SHR_PlayEffect(fx, org, dir);
	}
}

/*
-------------------------
G_ATSTCheckPain

Called by NPC's and player in an ATST
-------------------------
*/

#ifdef SP_GAME
void g_atst_check_pain(gentity_t* self, gentity_t* other, const vec3_t point, int damage, int mod, const int hit_loc)
#else // MP: g_combat.c passes self, other, damage; hit location and point come from gPainHitLoc / gPainPoint
void g_atst_check_pain(gentity_t* self, gentity_t* other, int damage)
#endif
{
	int newBolt = 0;

	if (rand() & 1)
	{
		SHR_SoundOnEnt(self, CHAN_LESS_ATTEN, "sound/chars/atst/atst_damaged1");
	}
	else
	{
		SHR_SoundOnEnt(self, CHAN_LESS_ATTEN, "sound/chars/atst/atst_damaged2");
	}

	if (SHR_PAIN_HITLOC == HL_ARM_LT && self->locationDamage[HL_ARM_LT] > LEFT_ARM_HEALTH)
	{
		if (self->locationDamage[SHR_PAIN_HITLOC] >= LEFT_ARM_HEALTH) // Blow it up?
		{
			newBolt = SHR_AddBolt(self, "*flash3");
			if (newBolt != -1)
			{
				//				G_PlayEffect( "small_chunks", self->playerModel, self->genericBolt1, self->s.number);
				ATST_PlayEffect(self, SHR_Bolt(self, genericBolt1, "*flash3"), "env/med_explode2");
				SHR_PlayEffectBolted("blaster/smoke_bolton", self, newBolt, SHR_PAIN_POINT, vec3_origin);
			}

			SHR_SetSurfaceOnOff(self, "head_light_blaster_cann", TURN_OFF);
		}
	}
	else if (SHR_PAIN_HITLOC == HL_ARM_RT && self->locationDamage[HL_ARM_RT] > RIGHT_ARM_HEALTH) // Blow it up?
	{
		if (self->locationDamage[SHR_PAIN_HITLOC] >= RIGHT_ARM_HEALTH)
		{
			newBolt = SHR_AddBolt(self, "*flash4");
			if (newBolt != -1)
			{
				//				G_PlayEffect( "small_chunks", self->playerModel, self->genericBolt2, self->s.number);
				ATST_PlayEffect(self, SHR_Bolt(self, genericBolt2, "*flash4"), "env/med_explode2");
				SHR_PlayEffectBolted("blaster/smoke_bolton", self, newBolt, SHR_PAIN_POINT, vec3_origin);
			}

			SHR_SetSurfaceOnOff(self, "head_concussion_charger", TURN_OFF);
		}
	}
}

/*
-------------------------
NPC_ATST_Pain
-------------------------
*/
void NPC_ATST_Pain(SHR_PAIN_PARAMS)
{
#ifdef SP_GAME
	g_atst_check_pain(self, other, SHR_PAIN_POINT, damage, SHR_PAIN_MOD, SHR_PAIN_HITLOC);
#else
	g_atst_check_pain(self, other, damage);
#endif
	NPC_Pain(SHR_PAIN_PASS);
}

/*
-------------------------
ATST_Hunt
-------------------------`
*/
static void ATST_Hunt(qboolean visible, qboolean advance)
{
	if (NPCS.NPCInfo->goalEntity == NULL)
	{
		//hunt
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
	}

	NPCS.NPCInfo->combatMove = qtrue;

	NPC_MoveToGoal(qtrue);
}

/*
-------------------------
ATST_Ranged
-------------------------
*/
static void ATST_Ranged(const qboolean visible, const qboolean advance, const qboolean altAttack)
{
	if (TIMER_Done(NPCS.NPC, "atkDelay") && visible) // Attack?
	{
		TIMER_Set(NPCS.NPC, "atkDelay", Q_irand(500, 3000));

		if (altAttack)
		{
			NPCS.ucmd.buttons |= BUTTON_ATTACK | BUTTON_ALT_ATTACK;
		}
		else
		{
			NPCS.ucmd.buttons |= BUTTON_ATTACK;
		}
	}

	if (NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES)
	{
		ATST_Hunt(visible, advance);
	}
}

/*
-------------------------
ATST_Attack
-------------------------
*/
static void ATST_Attack(void)
{
	qboolean altAttack = qfalse;
	int blasterTest = 0, chargerTest = 0;

	if (NPC_CheckEnemyExt(qfalse) == qfalse) //!NPC->enemy )//
	{
		NPCS.NPC->enemy = NULL;
		return;
	}

	NPC_FaceEnemy(qtrue);

	// Rate our distance to the target, and our visibilty
	const float distance = (int)(DistanceHorizontalSquared(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->enemy->SHR_currentOrigin));
	const distance_e distRate = distance > MIN_MELEE_RANGE_SQR ? DIST_LONG : DIST_MELEE;
	const qboolean visible = SHR_ClearLOSEnt(NPCS.NPC->enemy);
	const qboolean advance = (qboolean)(distance > MIN_DISTANCE_SQR);

	// If we cannot see our target, move to see it
	if (visible == qfalse)
	{
		if (NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES)
		{
			ATST_Hunt(visible, advance);
			return;
		}
	}

	// Decide what type of attack to do
	switch (distRate)
	{
	case DIST_MELEE:
#ifdef SP_GAME
		NPC_ChangeWeapon(WP_ATST_MAIN);
#endif
		break;

	case DIST_LONG:

#ifdef SP_GAME
		NPC_ChangeWeapon(WP_ATST_SIDE);
#endif

		// See if the side weapons are there
		blasterTest = SHR_GetSurfaceRenderStatus(NPCS.NPC, "head_light_blaster_cann");
		chargerTest = SHR_GetSurfaceRenderStatus(NPCS.NPC, "head_concussion_charger");

		// It has both side weapons
		if (!(blasterTest & TURN_OFF) && !(chargerTest & TURN_OFF))
		{
			const int weapon = Q_irand(0, 1); // 0 is blaster, 1 is charger (ALT SIDE)

			if (weapon) // Fire charger
			{
				altAttack = qtrue;
			}
			else
			{
				altAttack = qfalse;
			}
		}
		else if (!(blasterTest & TURN_OFF)) // Blaster is on
		{
			altAttack = qfalse;
		}
		else if (!(chargerTest & TURN_OFF)) // Blaster is on
		{
			altAttack = qtrue;
		}
		else
		{
			NPC_ChangeWeapon(WP_NONE);
		}
		break;
	}

	NPC_FaceEnemy(qtrue);

	ATST_Ranged(visible, advance, altAttack);
}

/*
-------------------------
ATST_Patrol
-------------------------
*/
static void ATST_Patrol(void)
{
	if (NPC_CheckPlayerTeamStealth())
	{
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
ATST_Idle
-------------------------
*/
static void ATST_Idle(void)
{
	NPC_BSIdle();

	NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_STAND1, SETANIM_FLAG_NORMAL);
}

/*
-------------------------
NPC_BSDroid_Default
-------------------------
*/
void NPC_BSATST_Default(void)
{
	if (NPCS.NPC->enemy)
	{
		if (NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES)
		{
			NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
		}
		ATST_Attack();
	}
	else if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
	{
		ATST_Patrol();
	}
	else
	{
		ATST_Idle();
	}
}