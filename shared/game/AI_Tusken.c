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
static float tuskenEnemyDist;
#endif

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

extern void G_AddVoiceEvent(const gentity_t* self, int event, int speak_debounce_time);
extern void NPC_TempLookTarget(const gentity_t* self, int lookEntNum, int minLookTime, int maxLookTime);
extern qboolean G_ExpandPointToBBox(vec3_t point, const vec3_t mins, const vec3_t maxs, int ignore, int clipmask);
extern void NPC_AimAdjust(int change);
extern qboolean FlyingCreature(const gentity_t* ent);
extern qboolean NPC_IsGunner(const gentity_t* self);
extern void NPC_AngerSound(void);
extern void npc_check_speak(gentity_t* speaker_npc);

#define MAX_VIEW_DIST 1024
#define MAX_VIEW_SPEED 250
#define MAX_LIGHT_INTENSITY 255
#define MIN_LIGHT_THRESHOLD 0.1

#define DISTANCE_SCALE 0.25f
#define DISTANCE_THRESHOLD 0.075f
#define SPEED_SCALE 0.25f
#define FOV_SCALE 0.5f
#define LIGHT_SCALE 0.25f

#define REALIZE_THRESHOLD 0.6f
#define CAUTIOUS_THRESHOLD	( REALIZE_THRESHOLD * 0.75 )

static float enemyDist;

//Local state enums
enum
{
	LSTATE_NONE = 0,
	LSTATE_UNDERFIRE,
	LSTATE_INVESTIGATE,
};

/*
-------------------------
NPC_Tusken_Precache
-------------------------
*/
#ifdef SP_GAME // MP has its own in NPC_stats.c
void NPC_Tusken_Precache(void)
{
	for (int i = 1; i < 5; i++)
	{
		G_SoundIndex(va("sound/weapons/tusken_staff/stickhit%d.wav", i));
	}
}
#endif

static void Tusken_ClearTimers(const gentity_t* ent)
{
	TIMER_Set(ent, "chatter", 0);
	TIMER_Set(ent, "duck", 0);
	TIMER_Set(ent, "stand", 0);
	TIMER_Set(ent, "shuffleTime", 0);
	TIMER_Set(ent, "sleepTime", 0);
	TIMER_Set(ent, "enemyLastVisible", 0);
	TIMER_Set(ent, "roamTime", 0);
	TIMER_Set(ent, "hideTime", 0);
	TIMER_Set(ent, "attackDelay", 0); //FIXME: Slant for difficulty levels
	TIMER_Set(ent, "stick", 0);
	TIMER_Set(ent, "scoutTime", 0);
	TIMER_Set(ent, "flee", 0);
	TIMER_Set(ent, "taunting", 0);
}

static void NPC_Tusken_PlayConfusionSound(gentity_t* self)
{
	//FIXME: make this a custom sound in sound set
	if (self->health > 0)
	{
		G_AddVoiceEvent(self, Q_irand(EV_CONFUSE1, EV_CONFUSE3), 2000);
	}
	//reset him to be totally unaware again
	TIMER_Set(self, "enemyLastVisible", 0);
	TIMER_Set(self, "flee", 0);
	self->NPC->squadState = SQUAD_IDLE;
	self->NPC->tempBehavior = BS_DEFAULT;

	//self->NPC->behaviorState = BS_PATROL;
	G_ClearEnemy(self); //FIXME: or just self->enemy = NULL;?

	self->NPC->investigateCount = 0;
}

/*
-------------------------
NPC_ST_Pain
-------------------------
*/

void NPC_Tusken_Pain(SHR_PAIN_PARAMS)
{
	self->NPC->localState = LSTATE_UNDERFIRE;

	TIMER_Set(self, "duck", -1);
	TIMER_Set(self, "stand", 2000);

	NPC_Pain(SHR_PAIN_PASS);

	if (!damage && self->health > 0)
	{
		//FIXME: better way to know I was pushed
		G_AddVoiceEvent(self, Q_irand(EV_PUSHED1, EV_PUSHED3), 2000);
	}
}

/*
-------------------------
ST_HoldPosition
-------------------------
*/

static void Tusken_HoldPosition(void)
{
	NPC_FreeCombatPoint(NPCS.NPCInfo->combatPoint, qtrue);
	NPCS.NPCInfo->goalEntity = NULL;
}

/*
-------------------------
ST_Move
-------------------------
*/

static qboolean Tusken_Move(void)
{
	NPCS.NPCInfo->combatMove = qtrue; //always move straight toward our goal

	const qboolean moved = NPC_MoveToGoal(qtrue);

	//If our move failed, then reset
	if (moved == qfalse)
	{
		//couldn't get to enemy
		//just hang here
		Tusken_HoldPosition();
	}

	return moved;
}

/*
-------------------------
NPC_BSTusken_Patrol
-------------------------
*/

static void NPC_BSTusken_Patrol(void)
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
			if (NPC_CheckForDanger(alert_event))
			{
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
			//check for other alert events
			//There is an event to look at
			if (alert_event >= 0) //&& level.alertEvents[alert_event].ID != NPCInfo->lastAlertID )
			{
				//NPCInfo->lastAlertID = level.alertEvents[alert_event].ID;
				if (level.alertEvents[alert_event].level == AEL_DISCOVERED)
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

	NPC_UpdateAngles(qtrue, qtrue);
}

void NPC_Tusken_Taunt(void)
{
	NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_TUSKENTAUNT1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	TIMER_Set(NPCS.NPC, "taunting", NPCS.NPC->client->ps.SHR_torsoAnimTimer);
	TIMER_Set(NPCS.NPC, "duck", -1);
}

/*
-------------------------
NPC_BSTusken_Attack
-------------------------
*/

#ifdef SP_GAME
static void NPC_BSTusken_Attack(void)
{
	// IN PAIN
	//---------
	if (NPCS.NPC->painDebounceTime > level.time)
	{
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	// IN FLEE
	//---------
	if (TIMER_Done(NPCS.NPC, "flee") && NPC_CheckForDanger(NPC_CheckAlertEvents(qtrue, qtrue, -1, qfalse, AEL_DANGER)))
	{
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	// UPDATE OUR ENEMY
	//------------------
	if (NPC_CheckEnemyExt(qfalse) == qfalse || !NPCS.NPC->enemy)
	{
		NPC_BSTusken_Patrol();
		return;
	}
	enemyDist = Distance(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin);

	// Is The Current Enemy A Jawa?
	//------------------------------
	if (NPCS.NPC->enemy->client && NPCS.NPC->enemy->client->NPC_class == CLASS_JAWA)
	{
		// Make Sure His Enemy Is Me
		//---------------------------
		if (NPCS.NPC->enemy->enemy != NPCS.NPC)
		{
			G_SetEnemy(NPCS.NPC->enemy, NPCS.NPC);
		}

		// Should We Forget About Our Current Enemy And Go After The Player?
		//-------------------------------------------------------------------
		if (player && // If There Is A Player Pointer
			player != NPCS.NPC->enemy && // The Player Is Not Currently My Enemy
			Distance(player->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin) < 130.0f && // The Player Is Close Enough
			NAV::InSameRegion(NPCS.NPC, player) // And In The Same Region
			)
		{
			G_SetEnemy(NPCS.NPC, player);
		}
	}

	// Update Our Last Seen Time
	//---------------------------
	if (SHR_ClearLOSEnt(NPCS.NPC->enemy))
	{
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
	}

	// Check To See If We Are In Attack Range
	//----------------------------------------
	const float bounds_min = NPCS.NPC->SHR_maxs[0] + NPCS.NPC->enemy->SHR_maxs[0];
	const float lunge_range = bounds_min + 65.0f;
	const float strike_range = bounds_min + 40.0f;
	const bool melee_range = enemyDist < lunge_range;
	const bool melee_weapon = NPCS.NPC->client->ps.weapon != WP_TUSKEN_RIFLE;
	const bool can_see_enemy = level.time - NPCS.NPCInfo->enemyLastSeenTime < 3000;

	// Check To Start Taunting
	//-------------------------
	if (can_see_enemy && !melee_range && TIMER_Done(NPCS.NPC, "tuskenTauntCheck"))
	{
		TIMER_Set(NPCS.NPC, "tuskenTauntCheck", Q_irand(2000, 6000));
		if (!Q_irand(0, 3))
		{
			NPC_Tusken_Taunt();
		}
	}

	if (TIMER_Done(NPCS.NPC, "taunting"))
	{
		// Should I Attack?
		//------------------
		if (melee_range || !melee_weapon && can_see_enemy)
		{
			if (!(NPCS.NPCInfo->scriptFlags & SCF_FIRE_WEAPON) && // If This Flag Is On, It Calls Attack From Elsewhere
				!(NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE) && // If This Flag Is On, Don't Fire At All
				TIMER_Done(NPCS.NPC, "attackDelay")
				)
			{
				NPCS.ucmd.buttons &= ~BUTTON_ALT_ATTACK;

				// If Not In Strike Range, Do Lunge, Or If We Don't Have The Staff, Just Shoot Normally
				//--------------------------------------------------------------------------------------
				if (enemyDist > strike_range)
				{
					NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
				}

				WeaponThink();
				TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPCInfo->shotTime - level.time);
			}

			if (!TIMER_Done(NPCS.NPC, "duck"))
			{
				NPCS.ucmd.upmove = -127;
			}
		}

		// Or Should I Move?
		//-------------------
		else if (NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES)
		{
			NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
			NPCS.NPCInfo->goalRadius = lunge_range;
			Tusken_Move();
		}
	}

	// UPDATE ANGLES
	//---------------
	if (can_see_enemy)
	{
		NPC_FaceEnemy(qtrue);
	}
	NPC_UpdateAngles(qtrue, qtrue);
}
#else // MP keeps its own version of this function
static void NPC_BSTusken_Attack(void)
{
	// IN PAIN
	if (NPCS.NPC->painDebounceTime > level.time)
	{
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	// IN FLEE
	if (TIMER_Done(NPCS.NPC, "flee") && NPC_CheckForDanger(NPC_CheckAlertEvents(qtrue, qtrue, -1, qfalse, AEL_DANGER)))
	{
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	// UPDATE OUR ENEMY
	if (NPC_CheckEnemyExt(qfalse) == qfalse || !NPCS.NPC->enemy)
	{
		NPC_BSTusken_Patrol();
		return;
	}
	tuskenEnemyDist = Distance(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin);

	// Is The Current Enemy A Jawa?
	if (NPCS.NPC->enemy->client && NPCS.NPC->enemy->client->NPC_class == CLASS_JAWA)
	{
		// Make Sure His Enemy Is Me
		if (NPCS.NPC->enemy->enemy != NPCS.NPC)
		{
			G_SetEnemy(NPCS.NPC->enemy, NPCS.NPC);
		}

		// Should We Forget About Our Current Enemy And Go After A Player? (SP: the player, close and in the same region)
		gentity_t* closest = FindClosestPlayer(NPCS.NPC->r.currentOrigin, NPCS.NPC->client->enemyTeam);
		if (closest && closest != NPCS.NPC->enemy
			&& Distance(closest->r.currentOrigin, NPCS.NPC->r.currentOrigin) < 130.0f
			&& trap->InPVS(closest->r.currentOrigin, NPCS.NPC->r.currentOrigin))
		{
			G_SetEnemy(NPCS.NPC, closest);
		}
	}

	// Update Our Last Seen Time
	if (NPC_ClearLOS4(NPCS.NPC->enemy))
	{
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
	}

	// Check To See If We Are In Attack Range
	const float boundsMin = NPCS.NPC->r.maxs[0] + NPCS.NPC->enemy->r.maxs[0];
	const float lungeRange = boundsMin + 65.0f;
	const float strikeRange = boundsMin + 40.0f;
	const qboolean meleeRange = tuskenEnemyDist < lungeRange ? qtrue : qfalse;
	const qboolean meleeWeapon = NPCS.NPC->client->ps.weapon != WP_TUSKEN_RIFLE ? qtrue : qfalse;
	const qboolean canSeeEnemy = level.time - NPCS.NPCInfo->enemyLastSeenTime < 3000 ? qtrue : qfalse;

	// Check To Start Taunting
	if (canSeeEnemy && !meleeRange && TIMER_Done(NPCS.NPC, "tuskenTauntCheck"))
	{
		TIMER_Set(NPCS.NPC, "tuskenTauntCheck", Q_irand(2000, 6000));
		if (!Q_irand(0, 3))
		{
			NPC_Tusken_Taunt();
		}
	}

	if (TIMER_Done(NPCS.NPC, "taunting"))
	{
		// Should I Attack?
		if (meleeRange || !meleeWeapon && canSeeEnemy)
		{
			if (!(NPCS.NPCInfo->scriptFlags & SCF_FIRE_WEAPON) // If This Flag Is On, It Calls Attack From Elsewhere
				&& !(NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE) // If This Flag Is On, Don't Fire At All
				&& TIMER_Done(NPCS.NPC, "attackDelay"))
			{
				NPCS.ucmd.buttons &= ~BUTTON_ALT_ATTACK;

				// If Not In Strike Range, Do Lunge, Or If We Don't Have The Staff, Just Shoot Normally
				if (tuskenEnemyDist > strikeRange)
				{
					NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
				}

				WeaponThink();
				TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPCInfo->shotTime - level.time);
			}

			if (!TIMER_Done(NPCS.NPC, "duck"))
			{
				NPCS.ucmd.upmove = -127;
			}
		}
		// Or Should I Move?
		else if (NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES)
		{
			NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
			NPCS.NPCInfo->goalRadius = lungeRange;
			Tusken_Move();
		}
	}

	// UPDATE ANGLES
	if (canSeeEnemy)
	{
		NPC_FaceEnemy(qtrue);
	}
	NPC_UpdateAngles(qtrue, qtrue);
}
#endif

extern void G_Knockdown(gentity_t* self, gentity_t* attacker, const vec3_t push_dir, float strength, const qboolean breakSaberLock);

#ifdef SP_GAME // SP only: the MP game has no version of this
static void Tusken_StaffTrace(void)
{
	if (!NPCS.NPC->ghoul2.size()
		|| NPCS.NPC->weaponModel[0] <= 0)
	{
		return;
	}

	const int boltIndex = gi.G2API_AddBolt(&NPCS.NPC->ghoul2[NPCS.NPC->weaponModel[0]], "*weapon");
	if (boltIndex != -1)
	{
		const int curTime = cg.time ? cg.time : level.time;
		qboolean hit = qfalse;
		int last_hit = ENTITYNUM_NONE;
		for (int time = curTime - 25; time <= curTime + 25 && !hit; time += 25)
		{
			mdxaBone_t boltMatrix = { 0 };
			vec3_t tip = { 0 }, dir = { 0 }, base = { 0 };
			const vec3_t angles = { 0, NPCS.NPC->SHR_currentAngles[YAW], 0 };
			const vec3_t mins = { -2, -2, -2 }, maxs = { 2, 2, 2 };
			trace_t trace = { qfalse };

			gi.G2API_GetBoltMatrix(NPCS.NPC->ghoul2, NPCS.NPC->weaponModel[0],
				boltIndex,
				&boltMatrix, angles, NPCS.NPC->SHR_currentOrigin, time,
				NULL, NPCS.NPC->s.modelScale);
			SHR_GiveMeVector(boltMatrix, ORIGIN, base);
			SHR_GiveMeVector(boltMatrix, NEGATIVE_Y, dir);
			VectorMA(base, -20, dir, base);
			VectorMA(base, 78, dir, tip);
#ifndef FINAL_BUILD
			if (SHR_CVAR(d_saberCombat).integer > 1 || SHR_CVAR(g_DebugSaberCombat).integer)
			{
				G_DebugLine(base, tip, 1000, 0x000000ff);
			}
#endif
			gi.trace(&trace, base, mins, maxs, tip, NPCS.NPC->s.number, MASK_SHOT, G2_RETURNONHIT, 10);
			if (trace.fraction < 1.0f && trace.entityNum != last_hit)
			{
				//hit something
				gentity_t* traceEnt = &g_entities[trace.entityNum];
				if (traceEnt->takedamage
					&& (!traceEnt->client || traceEnt == NPCS.NPC->enemy || traceEnt->client->NPC_class != NPCS.NPC->client->NPC_class))
				{
					//smack
					const int dmg = Q_irand(5, 10) * (SHR_SPSKILL + 1);

					//FIXME: debounce?
					SHR_Sound(traceEnt, G_SoundIndex(va("sound/weapons/tusken_staff/stickhit%d.wav", Q_irand(1, 4))));
					G_Damage(traceEnt, NPCS.NPC, NPCS.NPC, vec3_origin, trace.endpos, dmg, DAMAGE_NO_KNOCKBACK, MOD_MELEE);
					if (traceEnt->health > 0
						&& (traceEnt->client && traceEnt->client->NPC_class == CLASS_JAWA && !Q_irand(0, 1)
							|| dmg > 19)) //FIXME: base on skill!
					{
						//do pain on enemy
						G_Knockdown(traceEnt, NPCS.NPC, dir, 300, qtrue);
					}
					last_hit = trace.entityNum;
					hit = qtrue;
				}
			}
		}
	}
}
#endif

#ifdef SP_GAME // SP only: the MP game has no version of this
void Tusken_StaffTracenew(gentity_t* self)
{
	if (!self->ghoul2.size()
		|| self->weaponModel[0] <= 0)
	{
		return;
	}

	const int boltIndex = gi.G2API_AddBolt(&self->ghoul2[self->weaponModel[0]], "*weapon");
	if (boltIndex != -1)
	{
		const int curTime = cg.time ? cg.time : level.time;
		qboolean hit = qfalse;
		int last_hit = ENTITYNUM_NONE;
		for (int time = curTime - 25; time <= curTime + 25 && !hit; time += 25)
		{
			mdxaBone_t boltMatrix = { 0 };
			vec3_t tip = { 0 }, dir = { 0 }, base = { 0 };
			const vec3_t angles = { 0, self->SHR_currentAngles[YAW], 0 };
			const vec3_t mins = { -2, -2, -2 }, maxs = { 2, 2, 2 };
			trace_t trace = { qfalse };

			gi.G2API_GetBoltMatrix(self->ghoul2, self->weaponModel[0],
				boltIndex,
				&boltMatrix, angles, self->SHR_currentOrigin, time,
				NULL, self->s.modelScale);
			SHR_GiveMeVector(boltMatrix, ORIGIN, base);
			SHR_GiveMeVector(boltMatrix, NEGATIVE_Y, dir);
			VectorMA(base, -20, dir, base);
			VectorMA(base, 78, dir, tip);
#ifndef FINAL_BUILD
			if (SHR_CVAR(d_saberCombat).integer > 1 || SHR_CVAR(g_DebugSaberCombat).integer)
			{
				G_DebugLine(base, tip, 1000, 0x000000ff);
			}
#endif
			gi.trace(&trace, base, mins, maxs, tip, self->s.number, MASK_SHOT, G2_RETURNONHIT, 10);
			if (trace.fraction < 1.0f && trace.entityNum != last_hit)
			{
				//hit something
				gentity_t* traceEnt = &g_entities[trace.entityNum];
				if (traceEnt->takedamage
					&& (!traceEnt->client || traceEnt == self->enemy || traceEnt->client->NPC_class != self->client->NPC_class))
				{
					//smack
					const int dmg = Q_irand(5, 10) * (SHR_SPSKILL + 1);

					//FIXME: debounce?
					SHR_Sound(traceEnt, G_SoundIndex(va("sound/weapons/tusken_staff/stickhit%d.wav", Q_irand(1, 4))));
					G_Damage(traceEnt, self, self, vec3_origin, trace.endpos, dmg, DAMAGE_NO_KNOCKBACK, MOD_MELEE);
					if (traceEnt->health > 0
						&& (traceEnt->client && traceEnt->client->NPC_class == CLASS_JAWA && !Q_irand(0, 1)
							|| dmg > 19)) //FIXME: base on skill!
					{
						//do pain on enemy
						G_Knockdown(traceEnt, self, dir, 300, qtrue);
					}
					last_hit = trace.entityNum;
					hit = qtrue;
				}
			}
		}
	}
}
#endif

#ifdef SP_GAME // SP only: the MP game has no version of this
qboolean G_TuskenAttackAnimDamage(gentity_t* self)
{
	if (self->client->ps.torsoAnim == BOTH_TUSKENATTACK1 ||
		self->client->ps.torsoAnim == BOTH_TUSKENATTACK2 ||
		self->client->ps.torsoAnim == BOTH_TUSKENATTACK3 ||
		self->client->ps.torsoAnim == BOTH_TUSKENLUNGE1)
	{
		float current = 0.0f;
		int end = 0;
		int start = 0;
		if (!!gi.G2API_GetBoneAnimIndex(&
			self->ghoul2[self->playerModel],
			self->lowerLumbarBone,
			level.time,
			&current,
			&start,
			&end,
			NULL,
			NULL,
			NULL))
		{
			const float percent_complete = (current - start) / (end - start);
			//gi.Printf("%f\n", percentComplete);
			switch (self->client->ps.torsoAnim)
			{
			case BOTH_TUSKENATTACK1: return (qboolean)(percent_complete > 0.3 && percent_complete < 0.7);
			case BOTH_TUSKENATTACK2: return (qboolean)(percent_complete > 0.3 && percent_complete < 0.7);
			case BOTH_TUSKENATTACK3: return (qboolean)(percent_complete > 0.1 && percent_complete < 0.5);
			case BOTH_TUSKENLUNGE1: return (qboolean)(percent_complete > 0.3 && percent_complete < 0.5);
			default:;
			}
		}
	}
	return qfalse;
}
#endif

#ifdef SP_GAME
void NPC_BSTusken_Default(void)
{
	if (NPCS.NPCInfo->scriptFlags & SCF_FIRE_WEAPON)
	{
		WeaponThink();
	}

	if (G_TuskenAttackAnimDamage(NPCS.NPC))
	{
		//Tusken_StaffTrace();
		Tusken_StaffTracenew(NPCS.NPC);
	}

	if (!NPCS.NPC->enemy)
	{
		//don't have an enemy, look for one
		NPC_BSTusken_Patrol();
	}
	else //if ( NPC->enemy )
	{
		//have an enemy
		NPC_BSTusken_Attack();

		npc_check_speak(NPCS.NPC);
	}
}
#else // MP keeps its own version of this function
void NPC_BSTusken_Default(void)
{
	if (NPCS.NPCInfo->scriptFlags & SCF_FIRE_WEAPON)
	{
		WeaponThink();
	}

	if (!NPCS.NPC->enemy)
	{
		//don't have an enemy, look for one
		NPC_BSTusken_Patrol();
	}
	else
	{
		//have an enemy
		NPC_BSTusken_Attack();

		npc_check_speak(NPCS.NPC); // (SP)
	}
}
#endif