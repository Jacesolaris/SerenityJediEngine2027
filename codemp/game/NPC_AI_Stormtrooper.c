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
#include "anims.h"

extern void G_AddVoiceEvent(const gentity_t* self, int event, int speak_debounce_time);
extern void AI_GroupUpdateSquadstates(AIGroupInfo_t* group, const gentity_t* member, int new_squad_state);
extern qboolean AI_GroupContainsEntNum(const AIGroupInfo_t* group, int entNum);
extern void AI_GroupUpdateEnemyLastSeen(AIGroupInfo_t* group, vec3_t spot);
extern void AI_GroupUpdateClearShotTime(AIGroupInfo_t* group);
extern void NPC_TempLookTarget(const gentity_t* self, int lookEntNum, int minLookTime, int maxLookTime);
extern qboolean G_ExpandPointToBBox(vec3_t point, const vec3_t mins, const vec3_t maxs, int ignore, int clipmask);
extern void ChangeWeapon(const gentity_t* ent, int new_weapon);
extern void NPC_CheckGetNewWeapon(void);
extern int GetTime(int lastTime);
extern void NPC_AimAdjust(int change);
extern qboolean FlyingCreature(const gentity_t* ent);
extern void NPC_EvasionSaber(void);
extern qboolean RT_Flying(const gentity_t* self);
extern float NPC_EnemyRangeFromBolt(int boltIndex);
extern qboolean in_camera;
extern void NPC_CheckEvasion(void);
extern qboolean PM_InKnockDown(const playerState_t* ps);
extern qboolean NPC_CanUseAdvancedFighting();

#define	MAX_VIEW_DIST		1024
#define MAX_VIEW_SPEED		250
#define	MAX_LIGHT_INTENSITY 255
#define	MIN_LIGHT_THRESHOLD	0.1
#define	ST_MIN_LIGHT_THRESHOLD 30
#define	ST_MAX_LIGHT_THRESHOLD 180
#define	DISTANCE_THRESHOLD	0.075f
#define	MIN_TURN_AROUND_DIST_SQ	(10000)	//(100 squared) don't stop running backwards
//if your goal is less than 100 away

#define	DISTANCE_SCALE		0.35f	//These first three get your base detection rating, ideally add up to 1
#define	FOV_SCALE			0.40f	//
#define	LIGHT_SCALE			0.25f	//

#define	SPEED_SCALE			0.25f	//These next two are bonuses
#define	TURNING_SCALE		0.25f	//

#define	REALIZE_THRESHOLD	0.6f
#define CAUTIOUS_THRESHOLD	( REALIZE_THRESHOLD * 0.75 )

qboolean NPC_CheckPlayerTeamStealth(void);

static qboolean enemyLOS;
static qboolean enemyCS;
static qboolean enemyInFOV;
static qboolean hitAlly;
static qboolean faceEnemy;
static qboolean shoot;
static float enemyDist;
static vec3_t impactPos;

int groupSpeechDebounceTime[TEAM_NUM_TEAMS]; //used to stop several group AI from speaking all at once

// ---- externs for the SP port ----
extern void NPC_AngerSound(void);
extern qboolean WP_AbsorbKick(gentity_t* hit_ent, gentity_t* pusher, vec3_t push_dir);
extern qboolean NPC_MoveDirClear(const int forwardmove, const int rightmove, const qboolean reset);
extern void G_UcmdMoveForDir(const gentity_t* self, usercmd_t* cmd, vec3_t dir);
extern void Jedi_TimersApply(void);
extern qboolean npc_is_dark_jedi(const gentity_t* self);
extern qboolean npc_is_light_jedi(const gentity_t* self);
extern qboolean PM_InOnGroundAnim(const int anim);
// SP's in_camera: MP has no SP cinematic camera
static qboolean st_in_camera = qfalse;

// ---- SP top-level declarations (ported) ----
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

#define SABER_AVOID_DIST (128.0f) //256.0f;
#define SABER_AVOID_DIST_SQ (SABER_AVOID_DIST*SABER_AVOID_DIST)

static qboolean doMove;

//Local state enums

/*
-------------------------
ST_Move
-------------------------
*/

/*
-------------------------
NPC_ST_InvestigateEvent
-------------------------
*/

/*
-------------------------
NPC_BSST_Attack
-------------------------
*/

//===========Enhanced AI====================

// ---- MP top-level declarations ----

extern void G_SoundOnEnt(gentity_t* ent, soundChannel_t channel, const char* sound_path);

//Local state enums
enum
{
	LSTATE_NONE = 0,
	LSTATE_UNDERFIRE,
	LSTATE_INVESTIGATE,
};

enum
{
	SPEECH_CHASE,
	SPEECH_CONFUSED,
	SPEECH_COVER,
	SPEECH_DETECTED,
	SPEECH_GIVEUP,
	SPEECH_LOOK,
	SPEECH_LOST,
	SPEECH_OUTFLANK,
	SPEECH_ESCAPING,
	SPEECH_SIGHT,
	SPEECH_SOUND,
	SPEECH_SUSPICIOUS,
	SPEECH_YELL,
	SPEECH_PUSHED
};

extern qboolean PM_InOnGroundAnims(const playerState_t* ps);

/*
-------------------------
ST_Move
-------------------------
*/
void ST_TransferMoveGoal(const gentity_t* self, const gentity_t* other);

extern float Q_flrand(float min, float max);

/*
-------------------------
NPC_ST_InvestigateEvent
-------------------------
*/

#define	MAX_CHECK_THRESHOLD	1

/*
-------------------------
ST_Commander

  Make decisions about who should go where, etc.

FIXME: leader (group-decision-making) AI?
FIXME: need alternate routes!
FIXME: more group voice interaction
FIXME: work in pairs?

-------------------------
*/
extern qboolean AI_RefreshGroup(AIGroupInfo_t* group);

extern void G_Knockdown(gentity_t* self, gentity_t* attacker, const vec3_t push_dir, float strength, const qboolean breakSaberLock);

/*
-------------------------
NPC_BSST_Attack
-------------------------
*/

extern qboolean PM_CrouchAnim(int anim);
#define MELEE_DIST_SQUARED 6400

// ---- prototypes ----
void NPC_Saboteur_Precache(void);
static void Saboteur_Decloak(gentity_t* self, const int uncloak_time);
void Saboteur_Cloak(gentity_t* self);
void ST_AggressionAdjust(const gentity_t* self, const int change);
void ST_ClearTimers(const gentity_t* ent);
static qboolean NPC_IsGunner(const gentity_t* self);
static void ST_Speech(const gentity_t* self, const int speech_type, const float fail_chance);
void ST_MarkToCover(const gentity_t* self);
void ST_StartFlee(gentity_t* self, gentity_t* enemy, vec3_t danger_point, const int danger_level, const int min_time, const int max_time);
void NPC_ST_Pain(gentity_t* self, gentity_t* attacker, const int damage);
static void ST_HoldPosition(void);
static void NPC_ST_SayMovementSpeech(void);
static void NPC_ST_StoreMovementSpeech(const int speech, const float chance);
static qboolean ST_Move(void);
static void NPC_ST_SleepShuffle(void);
void NPC_BSST_Sleep(void);
static qboolean NPC_CheckEnemyStealth_Smart(gentity_t* target);
static qboolean NPC_CheckEnemyStealth(gentity_t* target);
qboolean NPC_CheckPlayerTeamStealth(void);
static qboolean NPC_CheckEnemiesInSpotlight(void);
static qboolean NPC_ST_InvestigateEvent(const int event_id, const qboolean extra_suspicious);
static void ST_OffsetLook(const float offset, vec3_t out);
static void ST_LookAround(void);
void NPC_BSST_Investigate(void);
void NPC_BSST_Patrol(void);
static void ST_CheckMoveState(void);
static void ST_ResolveBlockedShot(const int hit);
static void ST_CheckFireState(void);
static void ST_TrackEnemy(const gentity_t* self, vec3_t enemy_pos);
static int ST_ApproachEnemy(const gentity_t* self);
static void ST_HuntEnemy(const gentity_t* self);
static void ST_TransferTimers(const gentity_t* self, const gentity_t* other);
void ST_TransferMoveGoal(const gentity_t* self, const gentity_t* other);
static int ST_GetCPFlags(void);
static void ST_Commander(void);
static qboolean Melee_CanDoGrab(void);
static void Melee_GrabEnemy(void);
void NPC_BSST_Attack(void);
static void Gunner_HoldPosition(void);
static qboolean Gunner_Move(gentity_t* goal, const qboolean retreat);
static void Gunner_FaceEnemy(const qboolean do_pitch);
static float GunnerDistanceToEnemy(gentity_t* self);
static qboolean Gunner_HasClearShot(void);
static void Gunner_FireIfClearShot(const float enemy_dist);
static void Enhanced_Gunner_SetEnemyInfo(vec3_t enemy_dest, vec3_t enemy_dir, float* enemy_dist, vec3_t enemy_movedir, float* enemy_movespeed, const int prediction);
static qboolean Gunner_Strafe(const int strafeTimeMin, const int strafeTimeMax, const int nextStrafeTimeMin, const int nextStrafeTimeMax, const qboolean walking);
static void Gunner_CombatTimersUpdate(const int enemy_dist);
static void Gunner_DirectMove(const qboolean away);
static void Gunner_CombatDistance(const float enemy_dist);
static void Enhanced_Gunner_Combat(void);
static void Enhanced_Attack(void);
static void NPC_BSST_AttackAdvanced(void);
void NPC_BSST_Default(void);
static void npc_check_speak(gentity_t* speaker_npc);

void NPC_Saboteur_Precache(void)
{
	G_SoundIndex("sound/chars/shadowtrooper/cloak.wav");
	G_SoundIndex("sound/chars/shadowtrooper/decloak.wav");
}

static void Saboteur_Decloak(gentity_t* self, const int uncloak_time)
{
	if (self && self->client)
	{
		if (self->client->ps.powerups[PW_CLOAKED] && TIMER_Done(self, "decloakwait"))
		{
			//Uncloak
			self->client->ps.powerups[PW_CLOAKED] = 0;
			G_SoundOnEnt(self, CHAN_ITEM, "sound/chars/shadowtrooper/decloak.wav");
			TIMER_Set(self, "nocloak", uncloak_time);
			NPC_SetAnim(self, SETANIM_TORSO, BOTH_FORCE_DRAIN_RELEASE, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		}
	}
}

void Saboteur_Cloak(gentity_t* self)
{
	if (self && self->client && self->NPC)
	{
		if (st_in_camera) // Cinematic
		{
			Saboteur_Decloak(self, 2000);
		}
		if (TIMER_Done(self, "nocloak"))
		{
			//not sitting around waiting to cloak again
			if (!(self->NPC->aiFlags & NPCAI_SHIELDS))
			{
				//not allowed to cloak, actually
				Saboteur_Decloak(self, 2000);
			}
			else if (!self->client->ps.powerups[PW_CLOAKED])
			{
				//cloak
				self->client->ps.powerups[PW_CLOAKED] = Q3_INFINITE;
				G_SoundOnEnt(self, CHAN_ITEM, "sound/chars/shadowtrooper/cloak.wav");
				NPC_SetAnim(self, SETANIM_TORSO, BOTH_FORCE_PROTECT_FAST, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			}
		}
	}
}

void ST_AggressionAdjust(const gentity_t* self, const int change)
{
	int upper_threshold = 0, lower_threshold = 0;

	self->NPC->stats.aggression += change;

	//FIXME: base this on initial NPCS.NPC stats
	if (self->client->playerTeam == NPCTEAM_PLAYER)
	{
		//good guys are less aggressive
		upper_threshold = 10;
		lower_threshold = 5;
	}
	else
	{
		//bad guys are more aggressive
		if (self->client->NPC_class == CLASS_IMPERIAL ||
			self->client->NPC_class == CLASS_RODIAN ||
			self->client->NPC_class == CLASS_TRANDOSHAN ||
			self->client->NPC_class == CLASS_TUSKEN ||
			self->client->NPC_class == CLASS_WEEQUAY ||
			self->client->NPC_class == CLASS_STORMTROOPER ||
			self->client->NPC_class == CLASS_CLONETROOPER ||
			self->client->NPC_class == CLASS_STORMCOMMANDO)
		{
			upper_threshold = 20;
			lower_threshold = 10;
		}
		else
		{
			upper_threshold = 15;
			lower_threshold = 5;
		}
	}

	if (self->NPC->stats.aggression > upper_threshold)
	{
		self->NPC->stats.aggression = upper_threshold;
	}
	else if (self->NPC->stats.aggression < lower_threshold)
	{
		self->NPC->stats.aggression = lower_threshold;
	}
}

void ST_ClearTimers(const gentity_t* ent)
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
	TIMER_Set(ent, "interrogating", 0);
	TIMER_Set(ent, "verifyCP", 0);
	TIMER_Set(ent, "strafeRight", 0);
	TIMER_Set(ent, "strafeLeft", 0);
	TIMER_Set(ent, "slapattackDelay", 0);
	TIMER_Set(ent, "smackTime", 0);
}

static qboolean NPC_IsGunner(const gentity_t* self)
{
	switch (self->client->NPC_class)
	{
	case CLASS_BESPIN_COP:
	case CLASS_COMMANDO:
	case CLASS_GALAK:
	case CLASS_GRAN:
	case CLASS_IMPERIAL:
	case CLASS_IMPWORKER:
	case CLASS_JAN:
	case CLASS_LANDO:
	case CLASS_GALAKMECH:
	case CLASS_MONMOTHA:
	case CLASS_PRISONER:
	case CLASS_REBEL:
	case CLASS_REELO:
	case CLASS_RODIAN:
	case CLASS_STORMTROOPER:
	case CLASS_SWAMPTROOPER:
	case CLASS_SABOTEUR:
	case CLASS_TRANDOSHAN:
	case CLASS_UGNAUGHT:
	case CLASS_JAWA:
	case CLASS_WEEQUAY:
	case CLASS_BOBAFETT:
	case CLASS_MANDO:
	case CLASS_SBD:
	case CLASS_BATTLEDROID:
	case CLASS_STORMCOMMANDO:
	case CLASS_CLONETROOPER:
	case CLASS_TUSKEN:
	case CLASS_WOOKIE:
		return qtrue;
	default:
		break;
	}

	return qfalse;
}

static void ST_Speech(const gentity_t* self, const int speech_type, const float fail_chance)
{
	if (flrand(0.0f, 1.0f) < fail_chance)
	{
		return;
	}

	if (fail_chance >= 0)
	{
		//a negative failChance makes it always talk
		if (self->NPC->group)
		{
			//group AI speech debounce timer
			if (self->NPC->group->speechDebounceTime > level.time)
			{
				return;
			}
		}
		else if (!TIMER_Done(self, "chatter"))
		{
			//personal timer
			return;
		}
		else if (groupSpeechDebounceTime[self->client->playerTeam] > level.time)
		{
			//for those not in group AI
			return;
		}
	}

	if (self->NPC->group)
	{
		//So they don't all speak at once...
		self->NPC->group->speechDebounceTime = level.time + Q_irand(2000, 4000);
	}
	else
	{
		TIMER_Set(self, "chatter", Q_irand(2000, 4000));
	}
	groupSpeechDebounceTime[self->client->playerTeam] = level.time + Q_irand(2000, 4000);

	if (self->NPC->blockedSpeechDebounceTime > level.time)
	{
		return;
	}

	switch (speech_type)
	{
	case SPEECH_CHASE:
		G_AddVoiceEvent(self, Q_irand(EV_CHASE1, EV_CHASE3), 20000);
		break;
	case SPEECH_CONFUSED:
		G_AddVoiceEvent(self, Q_irand(EV_CONFUSE1, EV_CONFUSE3), 20000);
		break;
	case SPEECH_COVER:
		G_AddVoiceEvent(self, Q_irand(EV_COVER1, EV_COVER5), 20000);
		break;
	case SPEECH_DETECTED:
		G_AddVoiceEvent(self, Q_irand(EV_DETECTED1, EV_DETECTED5), 20000);
		break;
	case SPEECH_GIVEUP:
		G_AddVoiceEvent(self, Q_irand(EV_GIVEUP1, EV_GIVEUP4), 20000);
		break;
	case SPEECH_LOOK:
		G_AddVoiceEvent(self, Q_irand(EV_LOOK1, EV_LOOK2), 20000);
		break;
	case SPEECH_LOST:
		G_AddVoiceEvent(self, EV_LOST1, 20000);
		break;
	case SPEECH_OUTFLANK:
		G_AddVoiceEvent(self, Q_irand(EV_OUTFLANK1, EV_OUTFLANK2), 20000);
		break;
	case SPEECH_ESCAPING:
		G_AddVoiceEvent(self, Q_irand(EV_ESCAPING1, EV_ESCAPING3), 20000);
		break;
	case SPEECH_SIGHT:
		G_AddVoiceEvent(self, Q_irand(EV_SIGHT1, EV_SIGHT3), 20000);
		break;
	case SPEECH_SOUND:
		G_AddVoiceEvent(self, Q_irand(EV_SOUND1, EV_SOUND3), 20000);
		break;
	case SPEECH_SUSPICIOUS:
		G_AddVoiceEvent(self, Q_irand(EV_SUSPICIOUS1, EV_SUSPICIOUS5), 20000);
		break;
	case SPEECH_YELL:
		G_AddVoiceEvent(self, Q_irand(EV_ANGER1, EV_ANGER3), 20000);
		break;
	case SPEECH_PUSHED:
		G_AddVoiceEvent(self, Q_irand(EV_PUSHED1, EV_PUSHED3), 20000);
		break;
	default:
		break;
	}

	self->NPC->blockedSpeechDebounceTime = level.time + 2000;
}

void ST_MarkToCover(const gentity_t* self)
{
	if (!self || !self->NPC)
	{
		return;
	}
	self->NPC->localState = LSTATE_UNDERFIRE;
	TIMER_Set(self, "attackDelay", Q_irand(500, 2500));
	ST_AggressionAdjust(self, -3);
	if (self->NPC->group && self->NPC->group->numGroup > 1)
	{
		ST_Speech(self, SPEECH_COVER, 0); //FIXME: flee sound?
	}
}

void ST_StartFlee(gentity_t* self, gentity_t* enemy, vec3_t danger_point, const int danger_level, const int min_time,
	const int max_time)
{
	if (!self || !self->NPC)
	{
		return;
	}
	G_StartFlee(self, enemy, danger_point, danger_level, min_time, max_time);
	if (self->NPC->group && self->NPC->group->numGroup > 1)
	{
		ST_Speech(self, SPEECH_COVER, 0); //FIXME: flee sound?
	}
}

/*
-------------------------
NPC_ST_Pain
-------------------------
*/

void NPC_ST_Pain(gentity_t* self, gentity_t* attacker, const int damage)
{
	self->NPC->localState = LSTATE_UNDERFIRE;

	TIMER_Set(self, "duck", -1);
	TIMER_Set(self, "hideTime", -1);
	TIMER_Set(self, "stand", 2000);

	NPC_Pain(self, attacker, damage);

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

static void ST_HoldPosition(void)
{
	if (NPCS.NPCInfo->squadState == SQUAD_RETREAT)
	{
		TIMER_Set(NPCS.NPC, "flee", -level.time);
	}
	TIMER_Set(NPCS.NPC, "verifyCP", Q_irand(1000, 3000)); //don't look for another one for a few seconds
	NPC_FreeCombatPoint(NPCS.NPCInfo->combatPoint, qtrue);
	if (!trap->ICARUS_TaskIDPending((sharedEntity_t*)NPCS.NPC, TID_MOVE_NAV))
	{
		//don't have a script waiting for me to get to my point, okay to stop trying and stand
		AI_GroupUpdateSquadstates(NPCS.NPCInfo->group, NPCS.NPC, SQUAD_STAND_AND_SHOOT);
		NPCS.NPCInfo->goalEntity = NULL;
		if (NPCS.NPC->health > 0)
		{
			G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_LOOK1, EV_LOOK2), 2000);
		}
	}
}

static void NPC_ST_SayMovementSpeech(void)
{
	if (!NPCS.NPCInfo->movementSpeech)
	{
		return;
	}
	if (NPCS.NPCInfo->group &&
		NPCS.NPCInfo->group->commander &&
		NPCS.NPCInfo->group->commander->client &&
		NPCS.NPCInfo->group->commander->client->NPC_class == CLASS_IMPERIAL &&
		!Q_irand(0, 3))
	{
		//imperial (commander) gives the order
		ST_Speech(NPCS.NPCInfo->group->commander, NPCS.NPCInfo->movementSpeech, NPCS.NPCInfo->movementSpeechChance);
	}
	else
	{
		//really don't want to say this unless we can actually get there...
		ST_Speech(NPCS.NPC, NPCS.NPCInfo->movementSpeech, NPCS.NPCInfo->movementSpeechChance);
	}

	NPCS.NPCInfo->movementSpeech = 0;
	NPCS.NPCInfo->movementSpeechChance = 0.0f;
}

static void NPC_ST_StoreMovementSpeech(const int speech, const float chance)
{
	NPCS.NPCInfo->movementSpeech = speech;
	NPCS.NPCInfo->movementSpeechChance = chance;
}

static qboolean ST_Move(void)
{
	NPCS.NPCInfo->combatMove = qtrue; //always move straight toward our goal

	const qboolean moved = NPC_MoveToGoal(qtrue);
	navInfo_t info;

	//Get the move info
	NAV_GetLastMove(&info);

	//FIXME: if we bump into another one of our guys and can't get around him, just stop!
	//If we hit our target, then stop and fire!
	if (info.flags & NIF_COLLISION)
	{
		if (info.blocker == NPCS.NPC->enemy)
		{
			ST_HoldPosition();
		}
	}

	//If our move failed, then reset
	if (moved == qfalse)
	{
		//FIXME: if we're going to a combat point, need to pick a different one
		if (!trap->ICARUS_TaskIDPending((sharedEntity_t*)NPCS.NPC, TID_MOVE_NAV))
		{
			//can't transfer movegoal or stop when a script we're running is waiting to complete
			if (info.blocker && info.blocker->NPC && NPCS.NPCInfo->group != NULL && info.blocker->NPC->group == NPCS.NPCInfo->
				group) //(NPCS.NPCInfo->aiFlags&NPCAI_BLOCKED) && NPCS.NPCInfo->group != NULL )
			{
				//dammit, something is in our way
				//see if it's one of ours
				for (int j = 0; j < NPCS.NPCInfo->group->numGroup; j++)
				{
					if (NPCS.NPCInfo->group->member[j].number == NPCS.NPCInfo->blockingEntNum)
					{
						//we're being blocked by one of our own, pass our goal onto them and I'll stand still
						ST_TransferMoveGoal(NPCS.NPC, &g_entities[NPCS.NPCInfo->group->member[j].number]);
						break;
					}
				}
			}

			ST_HoldPosition();
		}
	}
	else
	{
		//First time you successfully move, say what it is you're doing
		NPC_ST_SayMovementSpeech();
	}

	return moved;
}

/*
-------------------------
NPC_ST_SleepShuffle
-------------------------
*/

static void NPC_ST_SleepShuffle(void)
{
	//Play an awake script if we have one
	if (G_ActivateBehavior(NPCS.NPC, BSET_AWAKE))
	{
		return;
	}

	//Automate some movement and noise
	if (TIMER_Done(NPCS.NPC, "shuffleTime"))
	{
		TIMER_Set(NPCS.NPC, "shuffleTime", 4000);
		TIMER_Set(NPCS.NPC, "sleepTime", 2000);
		return;
	}

	//They made another noise while we were stirring, see if we can see them
	if (TIMER_Done(NPCS.NPC, "sleepTime"))
	{
		NPC_CheckPlayerTeamStealth();
		TIMER_Set(NPCS.NPC, "sleepTime", 2000);
	}
}

/*
-------------------------
NPC_ST_Sleep
-------------------------
*/

void NPC_BSST_Sleep(void)
{
	const int alert_event = NPC_CheckAlertEvents(qfalse, qtrue, -1, qfalse, AEL_MINOR);
	//only check sounds since we're alseep!

	//There is an event we heard
	if (alert_event >= 0)
	{
		//See if it was enough to wake us up
		if (level.alertEvents[alert_event].level == AEL_DISCOVERED && NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
		{
			gentity_t* closestPlayer = FindClosestPlayer(NPCS.NPC->client->ps.origin, NPCS.NPC->client->enemyTeam);
			if (closestPlayer)
			{
				G_SetEnemy(NPCS.NPC, closestPlayer);
				return;
			}
		}

		//Otherwise just stir a bit
		NPC_ST_SleepShuffle();
	}
}

// -----------------------------------------------------------------------------
// NPC_CheckEnemyStealth_Smart
// Enhanced stealth detection with:
//  - Slightly increased FOV
//  - Sound-based detection (footsteps, saber, blaster)
//  - Reduced detection if target is crouching *behind* the NPCS.NPC,
//    but only when farther than a minimum distance.
// -----------------------------------------------------------------------------
static qboolean NPC_CheckEnemyStealth_Smart(gentity_t* target)
{
	// Any closer than this and we definitely notice (base distance).
	float min_dist = 80.0f;

	// Already have an enemy from some other logic.
	if (NPCS.NPC->enemy != NULL)
	{
		return qtrue;
	}

	// Ignore notarget.
	if ((target->flags & FL_NOTARGET) != 0)
	{
		return qfalse;
	}

	// Dead targets are not valid enemies.
	if (target->health <= 0)
	{
		return qfalse;
	}

	// If target has saber in hand and activated, we wake up even sooner.
	if (target->client->ps.weapon == WP_SABER &&
		(!target->client->ps.saberHolstered) &&
		target->client->ps.saberInFlight == qfalse)
	{
		min_dist = 100.0f;
	}

	// Distance squared from NPCS.NPC to target.
	float target_dist_sq = DistanceSquared(target->r.currentOrigin, NPCS.NPC->r.currentOrigin);

	// If the target is this close, then wake up regardless (if we're looking).
	if ((target->client->ps.pm_flags & PMF_DUCKED) == 0 &&
		(NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES) &&
		target_dist_sq < (min_dist * min_dist) &&
		(NPC_SeesThroughWalls(NPCS.NPC) || NPC_ClearLOS4(target))) // not through a wall (DH 2026-10-24)
	{
		G_SetEnemy(NPCS.NPC, target);
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
		TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
		return qtrue;
	}

	// Base maximum view distance from NPCS.NPC stats.
	float max_view_dist = NPCS.NPCInfo->stats.visrange;

	// Out of possible visual range.
	if (target_dist_sq > (max_view_dist * max_view_dist))
	{
		return qfalse;
	}

	// Slightly increase FOV to make NPCs more aware.
	const float hfov = NPCS.NPCInfo->stats.hfov * 1.25f;
	const float vfov = NPCS.NPCInfo->stats.vfov * 1.25f;

	// Clamp FOV to reasonable limits.
	const float clamped_hfov = (hfov > 180.0f) ? 180.0f : hfov;
	const float clamped_vfov = (vfov > 180.0f) ? 180.0f : vfov;

	// Check FOV first.
	if (InFOV(target, NPCS.NPC, clamped_hfov, clamped_vfov) == qfalse)
	{
		// Might still detect via sound later.
	}

	// Clear line of sight check (eyePoint vs origin depending on lean).
	qboolean clear_los = qfalse;
	if (target->client->ps.leanofs != 0)
	{
		clear_los = (NPC_ClearLOS5(target->client->renderInfo.eyePoint) == qtrue) ? qtrue : qfalse;
	}
	else
	{
		clear_los = (NPC_ClearLOS4(target) == qtrue) ? qtrue : qfalse;
	}

	// Basic "hearing" radius for loud actions.
	const float hear_radius = 512.0f;
	const float hear_radius_sq = hear_radius * hear_radius;

	// Assess loud actions: footsteps, saber, blaster fire.
	const float target_speed = VectorLength(target->client->ps.velocity);
	const qboolean target_moving_fast = (target_speed > 120.0f) ? qtrue : qfalse;

	const qboolean saber_loud =
		(target->client->ps.weapon == WP_SABER &&
			(!target->client->ps.saberHolstered) &&
			target->client->ps.saberInFlight == qfalse)
		? qtrue : qfalse;

	const qboolean blaster_firing =
		(target->client->ps.weapon != WP_SABER &&
			target->client->ps.weaponstate == WEAPON_FIRING)
		? qtrue : qfalse;

	const qboolean loud_action =
		(target_moving_fast == qtrue || saber_loud == qtrue || blaster_firing == qtrue)
		? qtrue : qfalse;

	// If target is loud and within hearing radius, detect even without good FOV.
	if (loud_action == qtrue &&
		target_dist_sq < hear_radius_sq &&
		clear_los == qtrue &&
		(NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES))
	{
		G_SetEnemy(NPCS.NPC, target);
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
		TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(300, 1500));
		return qtrue;
	}

	// Now proceed with full visual/stealth evaluation.
	if (clear_los == qtrue)
	{
		// AT-STs basically can't miss.
		if (target->client->NPC_class == CLASS_ATST)
		{
			G_SetEnemy(NPCS.NPC, target);
			TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
			return qtrue;
		}

		vec3_t targ_org = {
			target->r.currentOrigin[0],
			target->r.currentOrigin[1],
			target->r.currentOrigin[2] + target->r.maxs[2] - 4.0f
		};

		float h_angle_perc = NPC_GetHFOVPercentage(
			targ_org,
			NPCS.NPC->client->renderInfo.eyePoint,
			NPCS.NPC->client->renderInfo.eyeAngles,
			clamped_hfov);

		float vAngle_perc = NPC_GetVFOVPercentage(
			targ_org,
			NPCS.NPC->client->renderInfo.eyePoint,
			NPCS.NPC->client->renderInfo.eyeAngles,
			clamped_vfov);

		// Scale vertically and horizontally.
		vAngle_perc *= vAngle_perc;
		h_angle_perc *= (h_angle_perc * h_angle_perc);

		// Actual distance.
		const float target_dist = Distance(target->r.currentOrigin, NPCS.NPC->r.currentOrigin);

		const int target_crouching = (target->client->pers.cmd.upmove < 0) ? 1 : 0;
		const float dist_rating = target_dist / max_view_dist;
		float speed_rating = target_speed / MAX_VIEW_SPEED;

		const float turning_rating =
			5.0f;

		const float light_level = 255 / MAX_LIGHT_INTENSITY;
		const float fov_perc = 1.0f - (h_angle_perc + vAngle_perc) * 0.5f;

		float vis_rating = 0.0f;

		// Too dark.
		if (light_level < MIN_LIGHT_THRESHOLD)
		{
			return qfalse;
		}

		// Too close? Always see them.
		if (dist_rating < DISTANCE_THRESHOLD)
		{
			G_SetEnemy(NPCS.NPC, target);
			NPCS.NPCInfo->enemyLastSeenTime = level.time;
			TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
			return qtrue;
		}

		// Out of range.
		if (dist_rating > 1.0f)
		{
			return qfalse;
		}

		// Cap speed checks.
		if (speed_rating > 1.0f)
		{
			speed_rating = 1.0f;
		}

		// Distance influence.
		const float dist_influence = DISTANCE_SCALE * (1.0f - dist_rating);

		// FOV influence.
		const float fov_influence = FOV_SCALE * (1.0f - fov_perc);

		// Light influence.
		const float light_influence = (light_level - 0.5f) * LIGHT_SCALE;

		// Base rating.
		float target_rating = dist_influence + fov_influence + light_influence;

		// Environment visibility modifiers (water/fog).
		const int contents = trap->PointContents(targ_org, target->s.number);
		if ((contents & CONTENTS_WATER) != 0)
		{
			const int myContents = trap->PointContents(NPCS.NPC->client->renderInfo.eyePoint, NPCS.NPC->s.number);
			if ((myContents & CONTENTS_WATER) == 0)
			{
				if (NPCS.NPC->client->NPC_class == CLASS_SWAMPTROOPER)
				{
					vis_rating = 0.10f;
				}
				else
				{
					vis_rating = 0.35f;
				}
			}
			else
			{
				if (NPCS.NPC->client->NPC_class != CLASS_SWAMPTROOPER)
				{
					vis_rating = 0.15f;
				}
			}
		}
		else if ((contents & CONTENTS_FOG) != 0)
		{
			vis_rating = 0.15f;
		}

		// Apply visibility penalty.
		target_rating *= (1.0f - vis_rating);

		// Motion draws the eye quickly.
		target_rating += speed_rating * SPEED_SCALE;
		target_rating += turning_rating * TURNING_SCALE;

		// ---------------------------------------------------------------------
		// NEW STEALTH LOGIC (your request)
		// Reduce detection if crouching AND behind NPCS.NPC AND farther than threshold.
		// ---------------------------------------------------------------------
		if (target_crouching != 0)
		{
			// Base crouch penalty.
			target_rating *= 0.9f;

			// Distance threshold for crouch-behind stealth.
			const float behind_crouch_distance = 96.0f;

			if (target_dist > behind_crouch_distance)
			{
				// Compute yaw difference.
				float npc_yaw = NPCS.NPC->client->ps.viewangles[YAW];

				vec3_t dir;
				VectorSubtract(target->r.currentOrigin, NPCS.NPC->r.currentOrigin, dir);
				float yaw_to_target = vectoyaw(dir);

				float yaw_diff = AngleDelta(npc_yaw, yaw_to_target);

				// If target is behind NPCS.NPC (>90 degrees off center).
				if (fabsf(yaw_diff) > 90.0f)
				{
					target_rating *= 0.85f; // 15% harder to detect.
				}

				// Deep behind (>135 degrees).
				if (fabsf(yaw_diff) > 135.0f)
				{
					target_rating *= 0.80f; // 20% harder to detect.
				}
			}
		}

		// Swamptroopers see better.
		float realize = 0.0f;
		float cautious = 0.0f;
		if (NPCS.NPC->client->NPC_class == CLASS_SWAMPTROOPER)
		{
			realize = (float)CAUTIOUS_THRESHOLD;
			cautious = (float)CAUTIOUS_THRESHOLD * 0.75f;
		}
		else
		{
			realize = (float)REALIZE_THRESHOLD;
			cautious = (float)CAUTIOUS_THRESHOLD * 0.75f;
		}

		// Immediate realization.
		if (target_rating > realize && (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES))
		{
			G_SetEnemy(NPCS.NPC, target);
			NPCS.NPCInfo->enemyLastSeenTime = level.time;
			TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
			return qtrue;
		}

		// Cautious / suspicious state.
		if (target_rating > cautious && (NPCS.NPCInfo->scriptFlags & SCF_IGNORE_ALERTS) == 0)
		{
			if (TIMER_Done(NPCS.NPC, "enemyLastVisible"))
			{
				const int look_time = Q_irand(4500, 8500);
				TIMER_Set(NPCS.NPC, "enemyLastVisible", look_time);
				ST_Speech(NPCS.NPC, SPEECH_SIGHT, 0);
				NPC_TempLookTarget(NPCS.NPC, target->s.number, look_time, look_time);
			}
			else if (TIMER_Get(NPCS.NPC, "enemyLastVisible") <= level.time + 500 &&
				(NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES))
			{
				if (NPCS.NPCInfo->rank < RANK_LT && Q_irand(0, 2) == 0)
				{
					const int interrogateTime = Q_irand(2000, 4000);
					ST_Speech(NPCS.NPC, SPEECH_SUSPICIOUS, 0);
					TIMER_Set(NPCS.NPC, "interrogating", interrogateTime);
					G_SetEnemy(NPCS.NPC, target);
					NPCS.NPCInfo->enemyLastSeenTime = level.time;
					TIMER_Set(NPCS.NPC, "attackDelay", interrogateTime);
					TIMER_Set(NPCS.NPC, "stand", interrogateTime);
				}
				else
				{
					G_SetEnemy(NPCS.NPC, target);
					NPCS.NPCInfo->enemyLastSeenTime = level.time;
					TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
					TIMER_Set(NPCS.NPC, "stand", Q_irand(500, 2500));
				}
				return qtrue;
			}

			return qfalse;
		}
	}

	return qfalse;
}

// -----------------------------------------------------------------------------
// NPC_CheckEnemyStealth
// Determines whether an NPCS.NPC should detect a potential enemy (player or NPCS.NPC)
// based on distance, FOV, LOS, light level, movement, and environment.
// NOTE: This version is a cleaned, modernized rewrite with identical behaviour.
// -----------------------------------------------------------------------------
static qboolean NPC_CheckEnemyStealth(gentity_t* target)
{
	// Any closer than 40 and we definitely notice.
	float min_dist = 40.0f;

	// In case we acquired an enemy some other way.
	if (NPCS.NPC->enemy != NULL)
	{
		return qtrue;
	}

	// Ignore notarget.
	if ((target->flags & FL_NOTARGET) != 0)
	{
		return qfalse;
	}

	// Dead targets are not valid enemies.
	if (target->health <= 0)
	{
		return qfalse;
	}

	// If target has saber in hand and activated, we wake up even sooner even if not facing him.
	if (target->client->ps.weapon == WP_SABER &&
		(!target->client->ps.saberHolstered) &&
		target->client->ps.saberInFlight == qfalse)
	{
		min_dist = 100.0f;
	}

	// Distance squared from NPCS.NPC to target.
	float target_dist = DistanceSquared(target->r.currentOrigin, NPCS.NPC->r.currentOrigin);

	// If the target is this close, then wake up regardless.
	if (((target->client->ps.pm_flags & PMF_DUCKED) == 0) && // not ducking
		((NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES) != 0) && // looking for enemies
		(target_dist < (min_dist * min_dist)) && // closer than min_dist
		(NPC_SeesThroughWalls(NPCS.NPC) || NPC_ClearLOS4(target))) // not through a wall (DH 2026-10-24)
	{
		G_SetEnemy(NPCS.NPC, target);
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
		TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
		return qtrue;
	}

	// Max view distance from NPCS.NPC stats.
	float max_view_dist = NPCS.NPCInfo->stats.visrange;

	// Out of possible visRange.
	if (target_dist > (max_view_dist * max_view_dist))
	{
		return qfalse;
	}

	// Check FOV first.
	if (InFOV(target, NPCS.NPC, NPCS.NPCInfo->stats.hfov, NPCS.NPCInfo->stats.vfov) == qfalse)
	{
		return qfalse;
	}

	// Clear line of sight: use eyePoint if leaning, otherwise origin.
	const qboolean clear_los = (target->client->ps.leanofs != 0)
		? (NPC_ClearLOS5(target->client->renderInfo.eyePoint) == qtrue ? qtrue : qfalse)
		: (NPC_ClearLOS4(target) == qtrue ? qtrue : qfalse);

	// Now check for clear line of vision.
	if (clear_los == qtrue)
	{
		// AT-STs basically can't miss.
		if (target->client->NPC_class == CLASS_ATST)
		{
			G_SetEnemy(NPCS.NPC, target);
			TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
			return qtrue;
		}

		// Target upper origin (near head).
		vec3_t targ_org = {
			target->r.currentOrigin[0],
			target->r.currentOrigin[1],
			target->r.currentOrigin[2] + target->r.maxs[2] - 4.0f
		};

		// Horizontal and vertical FOV percentages.
		float h_angle_perc = NPC_GetHFOVPercentage(
			targ_org,
			NPCS.NPC->client->renderInfo.eyePoint,
			NPCS.NPC->client->renderInfo.eyeAngles,
			NPCS.NPCInfo->stats.hfov);

		float vAngle_perc = NPC_GetVFOVPercentage(
			targ_org,
			NPCS.NPC->client->renderInfo.eyePoint,
			NPCS.NPC->client->renderInfo.eyeAngles,
			NPCS.NPCInfo->stats.vfov);

		// Scale them vertically some, and horizontally pretty harshly.
		vAngle_perc *= vAngle_perc;
		h_angle_perc *= (h_angle_perc * h_angle_perc);

		// Assess the player's current status.
		target_dist = Distance(target->r.currentOrigin, NPCS.NPC->r.currentOrigin);

		const float target_speed = VectorLength(target->client->ps.velocity);
		const int target_crouching = (target->client->pers.cmd.upmove < 0) ? 1 : 0;
		const float dist_rating = target_dist / max_view_dist;
		float speed_rating = target_speed / MAX_VIEW_SPEED;

		const float turning_rating =
			5.0f;

		const float light_level = 255 / MAX_LIGHT_INTENSITY;
		const float fov_perc = 1.0f - (h_angle_perc + vAngle_perc) * 0.5f; // average FOV factor.

		float vis_rating = 0.0f;

		// Too dark.
		if (light_level < MIN_LIGHT_THRESHOLD)
		{
			return qfalse;
		}

		// Too close? Always see them.
		if (dist_rating < DISTANCE_THRESHOLD)
		{
			G_SetEnemy(NPCS.NPC, target);
			TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
			return qtrue;
		}

		// Out of range.
		if (dist_rating > 1.0f)
		{
			return qfalse;
		}

		// Cap our speed checks.
		if (speed_rating > 1.0f)
		{
			speed_rating = 1.0f;
		}

		// Calculate the distance, FOV and light influences.
		// Visibility linearly wanes over distance.
		const float dist_influence = DISTANCE_SCALE * (1.0f - dist_rating);
		// As the percentage out of the FOV increases, perception suffers on an exponential scale.
		const float fov_influence = FOV_SCALE * (1.0f - fov_perc);
		// Lack of light hides, abundance of light exposes.
		const float light_influence = (light_level - 0.5f) * LIGHT_SCALE;

		// Calculate our base rating.
		float target_rating = dist_influence + fov_influence + light_influence;

		// Now award any final bonuses to this number (water/fog visibility).
		const int contents = trap->PointContents(targ_org, target->s.number);
		if ((contents & CONTENTS_WATER) != 0)
		{
			const int myContents = trap->PointContents(NPCS.NPC->client->renderInfo.eyePoint, NPCS.NPC->s.number);
			if ((myContents & CONTENTS_WATER) == 0)
			{
				// I'm not in water.
				if (NPCS.NPC->client->NPC_class == CLASS_SWAMPTROOPER)
				{
					// These guys can see in/through water pretty well.
					vis_rating = 0.10f; // 10% bonus.
				}
				else
				{
					vis_rating = 0.35f; // 35% bonus.
				}
			}
			else
			{
				// Else, if we're both in water.
				if (NPCS.NPC->client->NPC_class == CLASS_SWAMPTROOPER)
				{
					// I can see him just fine.
				}
				else
				{
					vis_rating = 0.15f; // 15% bonus.
				}
			}
		}
		else
		{
			// Not in water.
			if ((contents & CONTENTS_FOG) != 0)
			{
				vis_rating = 0.15f; // 15% bonus.
			}
		}

		// Apply visibility modifier.
		target_rating *= (1.0f - vis_rating);

		// Motion draws the eye quickly.
		target_rating += speed_rating * SPEED_SCALE;
		target_rating += turning_rating * TURNING_SCALE;

		// Smaller targets are harder to identify.
		if (target_crouching != 0)
		{
			target_rating *= 0.9f; // 10% harder to see.
		}

		// If he's violated the threshold, then realize him.
		float realize = 0.0f;
		float cautious = 0.0f;

		if (NPCS.NPC->client->NPC_class == CLASS_SWAMPTROOPER)
		{
			// Swamptroopers can see much better.
			realize = (float)CAUTIOUS_THRESHOLD;
			cautious = (float)CAUTIOUS_THRESHOLD * 0.75f;
		}
		else
		{
			realize = (float)REALIZE_THRESHOLD;
			cautious = (float)CAUTIOUS_THRESHOLD * 0.75f;
		}

		if ((target_rating > realize) &&
			((NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES) != 0))
		{
			G_SetEnemy(NPCS.NPC, target);
			NPCS.NPCInfo->enemyLastSeenTime = level.time;
			TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
			return qtrue;
		}

		// If he's above the caution threshold, then realize him in a few seconds unless he moves to cover.
		if ((target_rating > cautious) &&
			((NPCS.NPCInfo->scriptFlags & SCF_IGNORE_ALERTS) == 0))
		{
			if (TIMER_Done(NPCS.NPC, "enemyLastVisible"))
			{
				// If we haven't already, start the counter.
				const int look_time = Q_irand(4500, 8500);
				TIMER_Set(NPCS.NPC, "enemyLastVisible", look_time);
				ST_Speech(NPCS.NPC, SPEECH_SIGHT, 0);
				NPC_TempLookTarget(NPCS.NPC, target->s.number, look_time, look_time);
			}
			else if ((TIMER_Get(NPCS.NPC, "enemyLastVisible") <= level.time + 500) &&
				((NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES) != 0))
			{
				if ((NPCS.NPCInfo->rank < RANK_LT) && (Q_irand(0, 2) == 0))
				{
					const int interrogateTime = Q_irand(2000, 4000);
					ST_Speech(NPCS.NPC, SPEECH_SUSPICIOUS, 0);
					TIMER_Set(NPCS.NPC, "interrogating", interrogateTime);
					G_SetEnemy(NPCS.NPC, target);
					NPCS.NPCInfo->enemyLastSeenTime = level.time;
					TIMER_Set(NPCS.NPC, "attackDelay", interrogateTime);
					TIMER_Set(NPCS.NPC, "stand", interrogateTime);
				}
				else
				{
					G_SetEnemy(NPCS.NPC, target);
					NPCS.NPCInfo->enemyLastSeenTime = level.time;
					TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
					TIMER_Set(NPCS.NPC, "stand", Q_irand(500, 2500));
				}
				return qtrue;
			}

			return qfalse;
		}
	}

	return qfalse;
}

qboolean NPC_CheckPlayerTeamStealth(void)
{
	gentity_t* enemy = NULL;
	const qboolean cairn_dock1 = (Q_stricmp(level.mapname, "cairn_dock1") == 0) ? qtrue : qfalse;
	const qboolean isYavin1b = (Q_stricmp(level.mapname, "yavin1b") == 0) ? qtrue : qfalse;

	for (int i = 0; i < ENTITYNUM_WORLD; i++)
	{
		if (!g_entities[i].inuse)
			continue;
		enemy = &g_entities[i];

		if (enemy
			&& enemy->client
			&& NPC_ValidEnemy(enemy)
			&& enemy->client->playerTeam == NPCS.NPC->client->enemyTeam) // MP: only the NPC's enemy team
		{
			if (g_npcspskill.integer > 1 && cairn_dock1 == qfalse && isYavin1b == qfalse)
			{
				if (NPC_CheckEnemyStealth_Smart(enemy))
				{
					return qtrue;
				}
			}
			else
			{
				if (NPC_CheckEnemyStealth(enemy))
				{
					return qtrue;
				}
			}
		}
	}
	return qfalse;
}

static qboolean NPC_CheckEnemiesInSpotlight(void)
{
	const qboolean isYavin1b = (Q_stricmp(level.mapname, "yavin1b") == 0) ? qtrue : qfalse;
	const qboolean cairn_dock1 = (Q_stricmp(level.mapname, "cairn_dock1") == 0) ? qtrue : qfalse;

	if (!NPCS.NPC || !NPCS.NPC->client || !NPCS.NPCInfo)
	{
		return qfalse;
	}

	// Use static storage to avoid large per-call stack usage
	static int entity_list[MAX_GENTITIES];

	// Limit detection to a fixed radius to prevent map-wide aggro
	const float DETECTION_RADIUS = 1024.0f; // 1024 units is a reasonable sight/hearing range
	const float DETECTION_RADIUS_LEVEL_HARD = 2048.0f; // 2048 units is a reasonable sight/hearing range on hard difficulty

	vec3_t mins = { 0 }, maxs = { 0 };

	for (int i = 0; i < 3; i++)
	{
		if (g_npcspskill.integer <= 2 || st_in_camera || isYavin1b == qtrue || cairn_dock1 == qtrue)
		{// Normal and easy have a smaller detection radius
			mins[i] = NPCS.NPC->client->renderInfo.eyePoint[i] - DETECTION_RADIUS;
			maxs[i] = NPCS.NPC->client->renderInfo.eyePoint[i] + DETECTION_RADIUS;
		}
		else
		{
			mins[i] = NPCS.NPC->client->renderInfo.eyePoint[i] - DETECTION_RADIUS_LEVEL_HARD;
			maxs[i] = NPCS.NPC->client->renderInfo.eyePoint[i] + DETECTION_RADIUS_LEVEL_HARD;
		}
	}

	const int num_listed_entities = trap->EntitiesInBox(mins, maxs, entity_list, MAX_GENTITIES);

	gentity_t* suspect = NULL;

	for (int i = 0; i < num_listed_entities; i++)
	{
		gentity_t* enemy = &g_entities[entity_list[i]];

		if (!enemy || !enemy->inuse || !enemy->client)
		{
			continue;
		}

		if (!NPC_ValidEnemy(enemy))
		{
			continue;
		}

		if (enemy->client->playerTeam != NPCS.NPC->client->enemyTeam)
		{
			continue;
		}

		if (g_npcspskill.integer <= 2 || st_in_camera || isYavin1b == qtrue || cairn_dock1 == qtrue)
		{// Normal and easy have a smaller detection radius
			// Primary cone check with distance limit
			const float dist_sq = DistanceSquared(NPCS.NPC->client->renderInfo.eyePoint, enemy->r.currentOrigin);

			if (dist_sq <= DETECTION_RADIUS * DETECTION_RADIUS)
			{
				if (InFOV3(enemy->r.currentOrigin, NPCS.NPC->client->renderInfo.eyePoint, NPCS.NPC->client->renderInfo.eyeAngles, NPCS.NPCInfo->stats.hfov, NPCS.NPCInfo->stats.vfov))
				{
					// 16^2 fudge factor
					if (dist_sq - 256.0f <= DETECTION_RADIUS * DETECTION_RADIUS)
					{
						if (G_ClearLOS4(NPCS.NPC, enemy)) {
							G_SetEnemy(NPCS.NPC, enemy);
							TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
							return qtrue;
						}
					}
				}
			}

			// Wider "suspicion" cone with distance limit
			if (dist_sq <= DETECTION_RADIUS * DETECTION_RADIUS)
			{
				if (InFOV3(enemy->r.currentOrigin, NPCS.NPC->client->renderInfo.eyePoint, NPCS.NPC->client->renderInfo.eyeAngles, 90.0f, NPCS.NPCInfo->stats.vfov * 3.0f))
				{
					if (G_ClearLOS4(NPCS.NPC, enemy))
					{
						if (!suspect ||
							DistanceSquared(NPCS.NPC->client->renderInfo.eyePoint, enemy->r.currentOrigin) <
							DistanceSquared(NPCS.NPC->client->renderInfo.eyePoint, suspect->r.currentOrigin))
						{
							suspect = enemy;
						}
					}
				}
			}
		}
		else
		{
			// Primary cone check with distance limit
			const float dist_sq = DistanceSquared(NPCS.NPC->client->renderInfo.eyePoint, enemy->r.currentOrigin);

			if (dist_sq <= DETECTION_RADIUS_LEVEL_HARD * DETECTION_RADIUS_LEVEL_HARD)
			{
				if (InFOV3(enemy->r.currentOrigin, NPCS.NPC->client->renderInfo.eyePoint, NPCS.NPC->client->renderInfo.eyeAngles, NPCS.NPCInfo->stats.hfov, NPCS.NPCInfo->stats.vfov))
				{
					// 16^2 fudge factor
					if (dist_sq - 256.0f <= DETECTION_RADIUS_LEVEL_HARD * DETECTION_RADIUS_LEVEL_HARD)
					{
						if (G_ClearLOS4(NPCS.NPC, enemy))
						{
							G_SetEnemy(NPCS.NPC, enemy);
							TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
							return qtrue;
						}
					}
				}
			}

			// Wider "suspicion" cone with distance limit
			if (dist_sq <= DETECTION_RADIUS_LEVEL_HARD * DETECTION_RADIUS_LEVEL_HARD)
			{
				if (InFOV3(enemy->r.currentOrigin, NPCS.NPC->client->renderInfo.eyePoint, NPCS.NPC->client->renderInfo.eyeAngles, 90.0f, NPCS.NPCInfo->stats.vfov * 3.0f))
				{
					if (G_ClearLOS4(NPCS.NPC, enemy))
					{
						if (!suspect ||
							DistanceSquared(NPCS.NPC->client->renderInfo.eyePoint, enemy->r.currentOrigin) <
							DistanceSquared(NPCS.NPC->client->renderInfo.eyePoint, suspect->r.currentOrigin))
						{
							suspect = enemy;
						}
					}
				}
			}
		}
	}

	if (suspect)
	{
		const float dist_sq = DistanceSquared(NPCS.NPC->client->renderInfo.eyePoint, suspect->r.currentOrigin);
		const float vis_rng_sq = NPCS.NPCInfo->stats.visrange * NPCS.NPCInfo->stats.visrange;

		// Must still have LOS
		if (G_ClearLOS4(NPCS.NPC, suspect))
		{// Random chance to notice based on distance, even if they have LOS
			if (flrand(0.0f, vis_rng_sq) > dist_sq)
			{
				// First time noticing
				if (TIMER_Done(NPCS.NPC, "enemyLastVisible"))
				{
					TIMER_Set(NPCS.NPC, "enemyLastVisible", Q_irand(4500, 8500));
					ST_Speech(NPCS.NPC, SPEECH_SIGHT, 0);
					NPC_FacePosition(suspect->r.currentOrigin, qtrue);
				}
				// Escalate to interrogation
				else if (TIMER_Get(NPCS.NPC, "enemyLastVisible") <= level.time + 500 &&
					(NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES))
				{
					if (!Q_irand(0, 2))
					{
						TIMER_Set(NPCS.NPC, "interrogating", Q_irand(2000, 4000));
						ST_Speech(NPCS.NPC, SPEECH_SUSPICIOUS, 0);
						NPC_FacePosition(suspect->r.currentOrigin, qtrue);
					}
				}
			}
		}
	}
	return qfalse;
}

static qboolean NPC_ST_InvestigateEvent(const int event_id, const qboolean extra_suspicious)
{
	//If they've given themselves away, just take them as an enemy
	if (NPCS.NPCInfo->confusionTime < level.time)
	{
		if (level.alertEvents[event_id].level == AEL_DISCOVERED && NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
		{
			if (!level.alertEvents[event_id].owner ||
				!level.alertEvents[event_id].owner->client ||
				level.alertEvents[event_id].owner->health <= 0 ||
				level.alertEvents[event_id].owner->client->playerTeam != NPCS.NPC->client->enemyTeam)
			{
				//not an enemy
				return qfalse;
			}
			//FIXME: what if can't actually see enemy, don't know where he is... should we make them just become very alert and start looking for him?  Or just let combat AI handle this... (act as if you lost him)
			ST_Speech(NPCS.NPC, SPEECH_CHASE, 0);
			G_SetEnemy(NPCS.NPC, level.alertEvents[event_id].owner);
			NPCS.NPCInfo->enemyLastSeenTime = level.time;
			TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 1500));
			if (level.alertEvents[event_id].type == AET_SOUND)
			{
				//heard him, didn't see him, stick for a bit
				TIMER_Set(NPCS.NPC, "roamTime", Q_irand(500, 2500));
			}
			return qtrue;
		}
	}

	//don't look at the same alert twice

	if (level.alertEvents[event_id].ID == NPCS.NPCInfo->lastAlertID)
	{
		return qfalse;
	}
	NPCS.NPCInfo->lastAlertID = level.alertEvents[event_id].ID;

	if (level.alertEvents[event_id].type == AET_SIGHT)
	{
		//sight alert, check the light level
		if (level.alertEvents[event_id].light < Q_irand(ST_MIN_LIGHT_THRESHOLD, ST_MAX_LIGHT_THRESHOLD))
		{
			//below my threshhold of potentially seeing
			return qfalse;
		}
	}

	//Save the position for movement (if necessary)
	VectorCopy(level.alertEvents[event_id].position, NPCS.NPCInfo->investigateGoal);

	//First awareness of it
	NPCS.NPCInfo->investigateCount += extra_suspicious ? 2 : 1;

	//Clamp the value
	if (NPCS.NPCInfo->investigateCount > 4)
		NPCS.NPCInfo->investigateCount = 4;

	//See if we should walk over and investigate
	if (level.alertEvents[event_id].level > AEL_MINOR && NPCS.NPCInfo->investigateCount > 1 && NPCS.NPCInfo->scriptFlags &
		SCF_CHASE_ENEMIES)
	{
		//make it so they can walk right to this point and look at it rather than having to use combatPoints
		if (G_ExpandPointToBBox(NPCS.NPCInfo->investigateGoal, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, NPCS.NPC->s.number,
			NPCS.NPC->clipmask & ~CONTENTS_BODY | CONTENTS_BOTCLIP))
		{
			//we were able to doMove the investigateGoal to a point in which our bbox would fit
			//drop the goal to the ground so we can get at it
			vec3_t end;
			trace_t trace;
			VectorCopy(NPCS.NPCInfo->investigateGoal, end);
			end[2] -= 512; //FIXME: not always right?  What if it's even higher, somehow?
			trap->Trace(&trace, NPCS.NPCInfo->investigateGoal, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, end, ENTITYNUM_NONE, NPCS.NPC->clipmask & ~CONTENTS_BODY | CONTENTS_BOTCLIP, qfalse, 0, 0);
			if (trace.fraction >= 1.0f)
			{
				//too high to even bother
				//FIXME: look at them???
			}
			else
			{
				VectorCopy(trace.endpos, NPCS.NPCInfo->investigateGoal);
				NPC_SetMoveGoal(NPCS.NPC, NPCS.NPCInfo->investigateGoal, 16, qtrue, -1, NULL);
				NPCS.NPCInfo->localState = LSTATE_INVESTIGATE;
			}
		}
		else
		{
			const int id = NPC_FindCombatPoint(NPCS.NPCInfo->investigateGoal, NPCS.NPCInfo->investigateGoal, NPCS.NPCInfo->investigateGoal, CP_INVESTIGATE | CP_HAS_ROUTE, 0, -1);

			if (id != -1)
			{
				NPC_SetMoveGoal(NPCS.NPC, level.combatPoints[id].origin, 16, qtrue, id, NULL);
				NPCS.NPCInfo->localState = LSTATE_INVESTIGATE;
			}
		}
		//Say something
		//FIXME: only if have others in group... these should be responses?
		if (NPCS.NPCInfo->investigateDebounceTime + NPCS.NPCInfo->pauseTime > level.time)
		{
			//was already investigating
			if (NPCS.NPCInfo->group &&
				NPCS.NPCInfo->group->commander &&
				NPCS.NPCInfo->group->commander->client &&
				NPCS.NPCInfo->group->commander->client->NPC_class == CLASS_IMPERIAL &&
				!Q_irand(0, 3))
			{
				ST_Speech(NPCS.NPCInfo->group->commander, SPEECH_LOOK, 0); //FIXME: "I'll go check it out" type sounds
			}
			else
			{
				ST_Speech(NPCS.NPC, SPEECH_LOOK, 0); //FIXME: "I'll go check it out" type sounds
			}
		}
		else
		{
			if (level.alertEvents[event_id].type == AET_SIGHT)
			{
				ST_Speech(NPCS.NPC, SPEECH_SIGHT, 0);
			}
			else if (level.alertEvents[event_id].type == AET_SOUND)
			{
				ST_Speech(NPCS.NPC, SPEECH_SOUND, 0);
			}
		}
		//Setup the debounce info
		NPCS.NPCInfo->investigateDebounceTime = NPCS.NPCInfo->investigateCount * 5000;
		NPCS.NPCInfo->investigateSoundDebounceTime = level.time + 2000;
		NPCS.NPCInfo->pauseTime = level.time;
	}
	else
	{
		//just look?
		//Say something
		if (level.alertEvents[event_id].type == AET_SIGHT)
		{
			ST_Speech(NPCS.NPC, SPEECH_SIGHT, 0);
		}
		else if (level.alertEvents[event_id].type == AET_SOUND)
		{
			ST_Speech(NPCS.NPC, SPEECH_SOUND, 0);
		}
		//Setup the debounce info
		NPCS.NPCInfo->investigateDebounceTime = NPCS.NPCInfo->investigateCount * 1000;
		NPCS.NPCInfo->investigateSoundDebounceTime = level.time + 1000;
		NPCS.NPCInfo->pauseTime = level.time;
		VectorCopy(level.alertEvents[event_id].position, NPCS.NPCInfo->investigateGoal);

		if (NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER && !RT_Flying(NPCS.NPC))
		{
			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_GUARD_LOOKAROUND1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		}
	}

	if (level.alertEvents[event_id].level >= AEL_DANGER)
	{
		NPCS.NPCInfo->investigateDebounceTime = Q_irand(500, 2500);
	}

	//Start investigating
	NPCS.NPCInfo->tempBehavior = BS_INVESTIGATE;
	return qtrue;
}

/*
-------------------------
ST_OffsetLook
-------------------------
*/

static void ST_OffsetLook(const float offset, vec3_t out)
{
	vec3_t angles, forward, temp;

	GetAnglesForDirection(NPCS.NPC->r.currentOrigin, NPCS.NPCInfo->investigateGoal, angles);
	angles[YAW] += offset;
	AngleVectors(angles, forward, NULL, NULL);
	VectorMA(NPCS.NPC->r.currentOrigin, 64, forward, out);

	CalcEntitySpot(NPCS.NPC, SPOT_HEAD, temp);
	out[2] = temp[2];
}

/*
-------------------------
ST_LookAround
-------------------------
*/

static void ST_LookAround(void)
{
	vec3_t look_pos;
	const float perc = (float)(level.time - NPCS.NPCInfo->pauseTime) / (float)(NPCS.NPCInfo->
		investigateDebounceTime);

	//Keep looking at the spot
	if (perc < 0.25)
	{
		VectorCopy(NPCS.NPCInfo->investigateGoal, look_pos);
	}
	else if (perc < 0.5f) //Look up but straight ahead
	{
		ST_OffsetLook(0.0f, look_pos);
	}
	else if (perc < 0.75f) //Look right
	{
		ST_OffsetLook(45.0f, look_pos);
	}
	else //Look left
	{
		ST_OffsetLook(-45.0f, look_pos);
	}

	NPC_FacePosition(look_pos, qtrue);
}

/*
-------------------------
NPC_BSST_Investigate
-------------------------
*/

void NPC_BSST_Investigate(void)
{
	//get group- mainly for group speech debouncing, but may use for group scouting/investigating AI, too
	AI_GetGroup(NPCS.NPC);

	if (NPCS.NPCInfo->scriptFlags & SCF_FIRE_WEAPON)
	{
		WeaponThink();
	}

	if (NPCS.NPCInfo->confusionTime < level.time)
	{
		if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
		{
			//Look for an enemy
			if (NPC_CheckPlayerTeamStealth())
			{
				ST_Speech(NPCS.NPC, SPEECH_DETECTED, 0);
				NPCS.NPCInfo->tempBehavior = BS_DEFAULT;
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
		}
	}

	if (!(NPCS.NPCInfo->scriptFlags & SCF_IGNORE_ALERTS))
	{
		const int alert_event = NPC_CheckAlertEvents(qtrue, qtrue, NPCS.NPCInfo->lastAlertID, qfalse, AEL_MINOR);

		//There is an event to look at
		if (alert_event >= 0)
		{
			if (NPCS.NPCInfo->confusionTime < level.time)
			{
				if (NPC_CheckForDanger(alert_event))
				{
					//running like hell
					ST_Speech(NPCS.NPC, SPEECH_COVER, 0); //FIXME: flee sound?
					return;
				}
			}

			if (level.alertEvents[alert_event].ID != NPCS.NPCInfo->lastAlertID)
			{
				NPC_ST_InvestigateEvent(alert_event, qtrue);
			}
		}
	}

	//If we're done looking, then just return to what we were doing
	if (NPCS.NPCInfo->investigateDebounceTime + NPCS.NPCInfo->pauseTime < level.time)
	{
		NPCS.NPCInfo->tempBehavior = BS_DEFAULT;
		NPCS.NPCInfo->goalEntity = UpdateGoal();

		NPC_UpdateAngles(qtrue, qtrue);
		//Say something
		ST_Speech(NPCS.NPC, SPEECH_GIVEUP, 0);
		return;
	}

	//FIXME: else, look for new alerts

	//See if we're searching for the noise's origin
	if (NPCS.NPCInfo->localState == LSTATE_INVESTIGATE && NPCS.NPCInfo->goalEntity != NULL)
	{
		//See if we're there
		if (!NAV_HitNavGoal(NPCS.NPC->r.currentOrigin, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, NPCS.NPCInfo->goalEntity->r.currentOrigin, 32, FlyingCreature(NPCS.NPC) != qfalse))
		{
			NPCS.ucmd.buttons |= BUTTON_WALKING;

			//Try and doMove there
			if (NPC_MoveToGoal(qtrue))
			{
				//Bump our times
				NPCS.NPCInfo->investigateDebounceTime = NPCS.NPCInfo->investigateCount * 5000;
				NPCS.NPCInfo->pauseTime = level.time;

				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
		}

		//Otherwise we're done or have given up
		//Say something
		ST_Speech(NPCS.NPC, SPEECH_LOOK, 0.33f);
		NPCS.NPCInfo->localState = LSTATE_NONE;
	}

	//Look around
	ST_LookAround();
}

/*
-------------------------
NPC_BSST_Patrol
-------------------------
*/

void NPC_BSST_Patrol(void)
{
	if (!NPCS.NPC || !NPCS.NPCInfo || !NPCS.NPC->client)
	{
		return;
	}

	// ROCKET TROOPER SPOTLIGHT MODE
	//===========================================================
	if (NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER &&
		(NPCS.NPC->client->ps.eFlags & EF_SPOTLIGHT))
	{
		vec3_t eye_fwd, end;
		const vec3_t maxs = { 2, 2, 2 };
		const vec3_t mins = { -2, -2, -2 };
		trace_t trace;

		AngleVectors(NPCS.NPC->client->renderInfo.eyeAngles, eye_fwd, NULL, NULL);
		VectorMA(NPCS.NPC->client->renderInfo.eyePoint, NPCS.NPCInfo->stats.visrange, eye_fwd, end);

		trap->Trace(&trace, NPCS.NPC->client->renderInfo.eyePoint, mins, maxs, end, NPCS.NPC->s.number, MASK_OPAQUE | CONTENTS_BODY | CONTENTS_CORPSE, qfalse, 0, 0);

		NPCS.NPC->speed = trace.fraction * NPCS.NPCInfo->stats.visrange;

		if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
		{
			if (trace.entityNum < ENTITYNUM_WORLD)
			{
				gentity_t* enemy = &g_entities[trace.entityNum];
				if (enemy && enemy->inuse && enemy->client &&
					NPC_ValidEnemy(enemy) &&
					enemy->client->playerTeam == NPCS.NPC->client->enemyTeam)
				{
					G_SetEnemy(NPCS.NPC, enemy);
					TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
					NPC_AngerSound();
					NPC_UpdateAngles(qtrue, qtrue);
					return;
				}
			}

			if (NPC_CheckEnemiesInSpotlight())
			{
				NPC_AngerSound();
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
		}
	}
	else
	{
		// NORMAL PATROL MODE
		//===========================================================
		AI_GetGroup(NPCS.NPC);

		if (NPCS.NPCInfo->confusionTime < level.time)
		{
			if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
			{
				if (NPC_CheckPlayerTeamStealth())
				{
					NPC_AngerSound();
					NPC_UpdateAngles(qtrue, qtrue);
					return;
				}
			}
		}
	}

	// ALERT EVENT HANDLING
	//===========================================================
	if (!(NPCS.NPCInfo->scriptFlags & SCF_IGNORE_ALERTS))
	{
		const int alertEvent = NPC_CheckAlertEvents(qtrue, qtrue, -1, qfalse, AEL_MINOR);

		//There is an event to look at
		if (alertEvent >= 0)
		{
			if (NPC_CheckForDanger(alertEvent))
			{//going to run?
				ST_Speech(NPCS.NPC, SPEECH_COVER, 0);
				return;
			}
			else if (NPCS.NPC->client->NPC_class == CLASS_BOBAFETT ||
				NPCS.NPC->client->NPC_class == CLASS_MANDO)
			{
				if (!level.alertEvents[alertEvent].owner ||
					!level.alertEvents[alertEvent].owner->client ||
					level.alertEvents[alertEvent].owner->health <= 0 ||
					level.alertEvents[alertEvent].owner->client->playerTeam != NPCS.NPC->client->enemyTeam)
				{//not an enemy
					return;
				}
				ST_Speech(NPCS.NPC, SPEECH_CHASE, 0);
				G_SetEnemy(NPCS.NPC, level.alertEvents[alertEvent].owner);
				NPCS.NPCInfo->enemyLastSeenTime = level.time;
				TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
				return;
			}
			else if (NPC_ST_InvestigateEvent(alertEvent, qfalse))
			{//actually going to investigate it
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
		}
	}

	// MOVEMENT TOWARD GOAL
	//===========================================================
	if (UpdateGoal())
	{
		NPCS.ucmd.buttons |= BUTTON_WALKING;
		NPC_MoveToGoal(qtrue);
	}
	else
	{
		// IDLE LOOK-AROUND BEHAVIOUR
		//===========================================================
		if (NPCS.NPC->client->NPC_class != CLASS_IMPERIAL &&
			NPCS.NPC->client->NPC_class != CLASS_IMPWORKER)
		{
			if (TIMER_Done(NPCS.NPC, "enemyLastVisible"))
			{
				if (!Q_irand(0, 10))
				{
					NPCS.NPCInfo->desiredYaw = NPCS.NPC->s.angles[1] + Q_irand(-45, 45);
				}
				if (!Q_irand(0, 10))
				{
					NPCS.NPCInfo->desiredPitch = Q_irand(-10, 10);
				}
			}
		}
	}

	NPC_UpdateAngles(qtrue, qtrue);

	// IMPERIAL IDLE ANIMATION HACK
	//===========================================================
	if (NPCS.NPC->client->NPC_class == CLASS_IMPERIAL ||
		NPCS.NPC->client->NPC_class == CLASS_IMPWORKER)
	{
		if (NPCS.NPC->client->ps.weapon != WP_CONCUSSION)
		{
			if (NPCS.ucmd.forwardmove || NPCS.ucmd.rightmove || NPCS.ucmd.upmove)
			{
				if (!NPCS.NPC->client->ps.torsoTimer ||
					NPCS.NPC->client->ps.torsoAnim == BOTH_STAND4)
				{
					if (NPCS.ucmd.buttons & BUTTON_WALKING &&
						!(NPCS.NPCInfo->scriptFlags & SCF_RUNNING))
					{
						NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, BOTH_STAND4,
							SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						NPCS.NPC->client->ps.torsoTimer = 200;
					}
				}
			}
			else
			{
				if ((!NPCS.NPC->client->ps.torsoTimer ||
					NPCS.NPC->client->ps.torsoAnim == BOTH_STAND4) &&
					(!NPCS.NPC->client->ps.legsTimer ||
						NPCS.NPC->client->ps.legsAnim == BOTH_STAND4))
				{
					NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_STAND4,
						SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					NPCS.NPC->client->ps.torsoTimer =
						NPCS.NPC->client->ps.legsTimer = 200;
				}
			}

			if (NPCS.NPC->client->ps.weapon != WP_NONE)
			{
				ChangeWeapon(NPCS.NPC, WP_NONE);
				NPCS.NPC->client->ps.weapon = WP_NONE;
				NPCS.NPC->client->ps.weaponstate = WEAPON_READY;
			}
		}
	}
}

/*
-------------------------
ST_CheckMoveState
-------------------------
*/

static void ST_CheckMoveState(void)
{
	if (trap->ICARUS_TaskIDPending((sharedEntity_t*)NPCS.NPC, TID_MOVE_NAV))
	{
		//moving toward a goal that a script is waiting on, so don't stop for anything!
		doMove = qtrue;
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER
		&& NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		//no squad stuff
		return;
	}
	else if (NPCS.NPCInfo->squadState == SQUAD_SCOUT)
	{
		//If we're supposed to stay put, then stand there and fire
		if (TIMER_Done(NPCS.NPC, "stick") == qfalse)
		{
			doMove = qfalse;
			return;
		}

		//Otherwise, if we can see our target, just shoot
		if (enemyLOS)
		{
			if (enemyCS)
			{
				//if we're going after our enemy, we can stop now
				if (NPCS.NPCInfo->goalEntity == NPCS.NPC->enemy)
				{
					AI_GroupUpdateSquadstates(NPCS.NPCInfo->group, NPCS.NPC, SQUAD_STAND_AND_SHOOT);
					doMove = qfalse;
					return;
				}
			}
		}
		else
		{
			//Move to find our target
			faceEnemy = qfalse;
		}
	}
	//See if we're running away
	else if (NPCS.NPCInfo->squadState == SQUAD_RETREAT)
	{
		if (NPCS.NPCInfo->goalEntity)
		{
			faceEnemy = qfalse;
		}
		else
		{
			//um, lost our goal?  Just stand and shoot, then
			NPCS.NPCInfo->squadState = SQUAD_STAND_AND_SHOOT;
		}
	}
	//see if we're heading to some other combatPoint
	else if (NPCS.NPCInfo->squadState == SQUAD_TRANSITION)
	{
		//NPCS.ucmd.buttons |= BUTTON_CAREFUL;
		if (!NPCS.NPCInfo->goalEntity)
		{
			//um, lost our goal?  Just stand and shoot, then
			NPCS.NPCInfo->squadState = SQUAD_STAND_AND_SHOOT;
		}
	}
	//see if we're at point, duck and fire
	else if (NPCS.NPCInfo->squadState == SQUAD_POINT)
	{
		if (TIMER_Done(NPCS.NPC, "stick"))
		{
			AI_GroupUpdateSquadstates(NPCS.NPCInfo->group, NPCS.NPC, SQUAD_STAND_AND_SHOOT);
			return;
		}

		doMove = qfalse;
		return;
	}
	//see if we're just standing around
	else if (NPCS.NPCInfo->squadState == SQUAD_STAND_AND_SHOOT)
	{
		//from this squadState we can transition to others?
		doMove = qfalse;
		return;
	}
	//see if we're hiding
	else if (NPCS.NPCInfo->squadState == SQUAD_COVER)
	{
		//Should we duck?
		doMove = qfalse;
		return;
	}
	//see if we're just standing around
	else if (NPCS.NPCInfo->squadState == SQUAD_IDLE)
	{
		if (!NPCS.NPCInfo->goalEntity)
		{
			doMove = qfalse;
			return;
		}
	}
	else
	{
		doMove = qtrue;
	}
	//See if we're a scout

	//See if we're moving towards a goal, not the enemy
	if (NPCS.NPCInfo->goalEntity != NPCS.NPC->enemy && NPCS.NPCInfo->goalEntity != NULL)
	{
		//Did we make it?
		if (NAV_HitNavGoal(NPCS.NPC->r.currentOrigin, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, NPCS.NPCInfo->goalEntity->r.currentOrigin, 16, !!FlyingCreature(NPCS.NPC)) ||
			enemyLOS && NPCS.NPCInfo->aiFlags & NPCAI_STOP_AT_LOS && !trap->ICARUS_TaskIDPending((sharedEntity_t*)NPCS.NPC, TID_MOVE_NAV)
			)
		{
			//either hit our navgoal or our navgoal was not a crucial (scripted) one (maybe a combat point) and we're scouting and found our enemy
			int new_squad_state = SQUAD_STAND_AND_SHOOT;
			//we got where we wanted to go, set timers based on why we were running
			switch (NPCS.NPCInfo->squadState)
			{
			case SQUAD_RETREAT: //was running away
				//done fleeing, obviously
				TIMER_Set(NPCS.NPC, "duck", (NPCS.NPC->client->pers.maxHealth - NPCS.NPC->health) * 100);
				TIMER_Set(NPCS.NPC, "hideTime", Q_irand(3000, 7000));
				TIMER_Set(NPCS.NPC, "flee", -level.time);
				new_squad_state = SQUAD_COVER;
				break;
			case SQUAD_TRANSITION: //was heading for a combat point
				TIMER_Set(NPCS.NPC, "hideTime", Q_irand(2000, 4000));
				break;
			case SQUAD_SCOUT: //was running after player
				break;
			default:
				break;
			}
			AI_GroupUpdateSquadstates(NPCS.NPCInfo->group, NPCS.NPC, new_squad_state);
			NPC_ReachedGoal();
			//don't attack right away
			TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(250, 500)); //FIXME: Slant for difficulty levels
			//don't do something else just yet

			// THIS IS THE ONE TRUE PLACE WHERE ROAM TIME IS SET
			TIMER_Set(NPCS.NPC, "roamTime", Q_irand(8000, 15000)); //Q_irand( 1000, 4000 ) );
			if (Q_irand(0, 3) == 0)
			{
				TIMER_Set(NPCS.NPC, "duck", Q_irand(5000, 10000)); // just reached our goal, chance of ducking now
			}
			return;
		}

		//keep going, hold of roamTimer until we get there
		TIMER_Set(NPCS.NPC, "roamTime", Q_irand(8000, 9000));
	}
}

static void ST_ResolveBlockedShot(const int hit)
{
	int stuck_time = 0;
	//figure out how long we intend to stand here, max
	if (TIMER_Get(NPCS.NPC, "roamTime") > TIMER_Get(NPCS.NPC, "stick"))
	{
		stuck_time = TIMER_Get(NPCS.NPC, "roamTime") - level.time;
	}
	else
	{
		stuck_time = TIMER_Get(NPCS.NPC, "stick") - level.time;
	}

	if (TIMER_Done(NPCS.NPC, "duck"))
	{
		//we're not ducking
		if (AI_GroupContainsEntNum(NPCS.NPCInfo->group, hit))
		{
			const gentity_t* member = &g_entities[hit];
			if (TIMER_Done(member, "duck"))
			{
				//they aren't ducking
				if (TIMER_Done(member, "stand"))
				{
					//they're not being forced to stand
					//tell them to duck at least as long as I'm not moving
					TIMER_Set(member, "duck", stuck_time); // tell my friend to duck so I can shoot over his head
					return;
				}
			}
		}
	}
	else
	{
		//maybe we should stand
		if (TIMER_Done(NPCS.NPC, "stand"))
		{
			//stand for as long as we'll be here
			TIMER_Set(NPCS.NPC, "stand", stuck_time);
			return;
		}
	}
	//Hmm, can't resolve this by telling them to duck or telling me to stand
	//We need to doMove!
	TIMER_Set(NPCS.NPC, "roamTime", -1);
	TIMER_Set(NPCS.NPC, "stick", -1);
	TIMER_Set(NPCS.NPC, "duck", -1);
	TIMER_Set(NPCS.NPC, "attakDelay", Q_irand(1000, 3000));
}

/*
-------------------------
ST_CheckFireState
-------------------------
*/

static void ST_CheckFireState(void)
{
	if (enemyCS)
	{
		//if have a clear shot, always try
		return;
	}

	if (NPCS.NPCInfo->squadState == SQUAD_RETREAT || NPCS.NPCInfo->squadState == SQUAD_TRANSITION || NPCS.NPCInfo->squadState ==
		SQUAD_SCOUT)
	{
		//runners never try to fire at the last pos
		return;
	}

	if (!VectorCompare(NPCS.NPC->client->ps.velocity, vec3_origin))
	{
		//if moving at all, don't do this
		return;
	}

	//See if we should continue to fire on their last position
	if (!hitAlly //we're not going to hit an ally
		&& enemyInFOV //enemy is in our FOV //FIXME: or we don't have a clear LOS?
		&& NPCS.NPCInfo->enemyLastSeenTime > 0 //we've seen the enemy
		&& NPCS.NPCInfo->group //have a group
		&& (NPCS.NPCInfo->group->numState[SQUAD_RETREAT] > 0 || NPCS.NPCInfo->group->numState[SQUAD_TRANSITION] > 0 || NPCS.NPCInfo->
			group->numState[SQUAD_SCOUT] > 0)) //laying down covering fire
	{
		if (level.time - NPCS.NPCInfo->enemyLastSeenTime < 10000 && //we have seem the enemy in the last 10 seconds
			(!NPCS.NPCInfo->group || level.time - NPCS.NPCInfo->group->lastSeenEnemyTime < 10000))
			//we are not in a group or the group has seen the enemy in the last 10 seconds
		{
			if (!Q_irand(0, 10))
			{
				//Fire on the last known position
				vec3_t muzzle;
				qboolean too_close = qfalse;
				qboolean too_far = qfalse;

				CalcEntitySpot(NPCS.NPC, SPOT_HEAD, muzzle);
				if (VectorCompare(impactPos, vec3_origin))
				{
					//never checked ShotEntity this frame, so must do a trace...
					trace_t tr;
					vec3_t forward, end;
					AngleVectors(NPCS.NPC->client->ps.viewangles, forward, NULL, NULL);
					VectorMA(muzzle, 8192, forward, end);
					trap->Trace(&tr, muzzle, vec3_origin, vec3_origin, end, NPCS.NPC->s.number, MASK_SHOT, qfalse, 0, 0);
					VectorCopy(tr.endpos, impactPos);
				}

				//see if impact would be too close to me
				float dist_threshold = 16384; //default
				switch (NPCS.NPC->s.weapon)
				{
				case WP_ROCKET_LAUNCHER:
				case WP_FLECHETTE:
				case WP_THERMAL:
				case WP_TRIP_MINE:
				case WP_DET_PACK:
					dist_threshold = 65536/*256*256*/;
					break;
				case WP_REPEATER:
					if (NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)
					{
						dist_threshold = 65536/*256*256*/;
					}
					break;
				case WP_CONCUSSION:
					if (!(NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE))
					{
						dist_threshold = 65536/*256*256*/;
					}
					break;
				default:
					break;
				}

				float dist = DistanceSquared(impactPos, muzzle);

				if (dist < dist_threshold)
				{
					//impact would be too close to me
					too_close = qtrue;
				}
				else if (level.time - NPCS.NPCInfo->enemyLastSeenTime > 5000 ||
					NPCS.NPCInfo->group && level.time - NPCS.NPCInfo->group->lastSeenEnemyTime > 5000)
				{
					//we've haven't seen them in the last 5 seconds
					//see if it's too far from where he is
					dist_threshold = 65536/*256*256*/; //default
					switch (NPCS.NPC->s.weapon)
					{
					case WP_ROCKET_LAUNCHER:
					case WP_FLECHETTE:
					case WP_THERMAL:
					case WP_TRIP_MINE:
					case WP_DET_PACK:
						dist_threshold = 262144/*512*512*/;
						break;
					case WP_REPEATER:
						if (NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)
						{
							dist_threshold = 262144/*512*512*/;
						}
						break;
					case WP_CONCUSSION:
						if (!(NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE))
						{
							dist_threshold = 262144/*512*512*/;
						}
						break;
					default:
						break;
					}
					dist = DistanceSquared(impactPos, NPCS.NPCInfo->enemyLastSeenLocation);
					if (dist > dist_threshold)
					{
						//impact would be too far from enemy
						too_far = qtrue;
					}
				}

				if (!too_close && !too_far)
				{
					vec3_t angles;
					vec3_t dir;
					//okay too shoot at last pos
					VectorSubtract(NPCS.NPCInfo->enemyLastSeenLocation, muzzle, dir);
					VectorNormalize(dir);
					vectoangles(dir, angles);

					NPCS.NPCInfo->desiredYaw = angles[YAW];
					NPCS.NPCInfo->desiredPitch = angles[PITCH];

					shoot = qtrue;
					faceEnemy = qfalse;
				}
			}
		}
	}
}

static void ST_TrackEnemy(const gentity_t* self, vec3_t enemy_pos)
{
	//clear timers
	TIMER_Set(self, "attackDelay", Q_irand(1000, 2000));
	TIMER_Set(self, "stick", Q_irand(500, 1500));
	TIMER_Set(self, "stand", -1);
	TIMER_Set(self, "scoutTime", TIMER_Get(self, "stick") - level.time + Q_irand(5000, 10000));
	//leave my combat point
	NPC_FreeCombatPoint(self->NPC->combatPoint, qfalse);
	//go after his last seen pos
	NPC_SetMoveGoal(self, enemy_pos, 100.0f, qfalse, -1, NULL);
	if (Q_irand(0, 3) == 0)
	{
		NPCS.NPCInfo->aiFlags |= NPCAI_STOP_AT_LOS;
	}
}

static int ST_ApproachEnemy(const gentity_t* self)
{
	TIMER_Set(self, "attackDelay", Q_irand(250, 500));
	TIMER_Set(self, "stick", Q_irand(1000, 2000));
	TIMER_Set(self, "stand", -1);
	TIMER_Set(self, "scoutTime", TIMER_Get(self, "stick") - level.time + Q_irand(5000, 10000));
	//leave my combat point
	NPC_FreeCombatPoint(self->NPC->combatPoint, qfalse);
	//return the relevant combat point flags
	return CP_CLEAR | CP_CLOSEST;
}

static void ST_HuntEnemy(const gentity_t* self)
{
	TIMER_Set(NPCS.NPC, "stick", Q_irand(250, 1000));
	TIMER_Set(NPCS.NPC, "stand", -1);
	TIMER_Set(NPCS.NPC, "scoutTime", TIMER_Get(NPCS.NPC, "stick") - level.time + Q_irand(5000, 10000));
	//leave my combat point
	NPC_FreeCombatPoint(NPCS.NPCInfo->combatPoint, qfalse);
	//go directly after the enemy
	if (NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES)
	{
		self->NPC->goalEntity = NPCS.NPC->enemy;
	}
}

static void ST_TransferTimers(const gentity_t* self, const gentity_t* other)
{
	TIMER_Set(other, "attackDelay", TIMER_Get(self, "attackDelay") - level.time);
	TIMER_Set(other, "duck", TIMER_Get(self, "duck") - level.time);
	TIMER_Set(other, "stick", TIMER_Get(self, "stick") - level.time);
	TIMER_Set(other, "scoutTime", TIMER_Get(self, "scoutTime") - level.time);
	TIMER_Set(other, "roamTime", TIMER_Get(self, "roamTime") - level.time);
	TIMER_Set(other, "stand", TIMER_Get(self, "stand") - level.time);
	TIMER_Set(self, "attackDelay", -1);
	TIMER_Set(self, "duck", -1);
	TIMER_Set(self, "stick", -1);
	TIMER_Set(self, "scoutTime", -1);
	TIMER_Set(self, "roamTime", -1);
	TIMER_Set(self, "stand", -1);
}

void ST_TransferMoveGoal(const gentity_t* self, const gentity_t* other)
{
	if (trap->ICARUS_TaskIDPending((sharedEntity_t*)self, TID_MOVE_NAV))
	{
		//can't transfer movegoal when a script we're running is waiting to complete
		return;
	}
	if (self->NPC->combatPoint != -1)
	{
		//I've got a combatPoint I'm going to, give it to him
		self->NPC->lastFailedCombatPoint = other->NPC->combatPoint = self->NPC->combatPoint;
		self->NPC->combatPoint = -1;
	}
	else
	{
		//I must be going for a goal, give that to him instead
		if (self->NPC->goalEntity == self->NPC->tempGoal)
		{
			NPC_SetMoveGoal(other, self->NPC->tempGoal->r.currentOrigin, self->NPC->goalRadius, (qboolean)((self->NPC->tempGoal->flags & FL_NAVGOAL) != 0), -1, NULL);
		}
		else
		{
			other->NPC->goalEntity = self->NPC->goalEntity;
		}
	}
	//give him my squadstate
	AI_GroupUpdateSquadstates(self->NPC->group, other, NPCS.NPCInfo->squadState);

	//give him my timers and clear mine
	ST_TransferTimers(self, other);

	//now make me stand around for a second or two at least
	AI_GroupUpdateSquadstates(self->NPC->group, self, SQUAD_STAND_AND_SHOOT);
	TIMER_Set(self, "stand", Q_irand(1000, 3000));
}

static int ST_GetCPFlags(void)
{
	int cp_flags = 0;
	if (NPCS.NPC && NPCS.NPCInfo->group)
	{
		if (NPCS.NPC == NPCS.NPCInfo->group->commander && NPCS.NPC->client->NPC_class == CLASS_IMPERIAL)
		{
			//imperials hang back and give orders
			if (NPCS.NPCInfo->group->numGroup > 1 && Q_irand(-3, NPCS.NPCInfo->group->numGroup) > 1)
			{
				//FIXME: make sure he;s giving orders with these lines
				if (Q_irand(0, 1))
				{
					ST_Speech(NPCS.NPC, SPEECH_CHASE, 0.5);
				}
				else
				{
					ST_Speech(NPCS.NPC, SPEECH_YELL, 0.5);
				}
			}
			cp_flags = CP_CLEAR | CP_COVER | CP_AVOID | CP_SAFE | CP_RETREAT;
		}
		else if (NPCS.NPCInfo->group->morale < 0)
		{
			//hide
			cp_flags = CP_COVER | CP_AVOID | CP_SAFE | CP_RETREAT;
		}
		else if (NPCS.NPCInfo->group->morale < NPCS.NPCInfo->group->numGroup)
		{
			//morale is low for our size
			const int morale_drop = NPCS.NPCInfo->group->numGroup - NPCS.NPCInfo->group->morale;
			if (morale_drop < -6)
			{
				//flee (no clear shot needed)
				cp_flags = CP_FLEE | CP_RETREAT | CP_COVER | CP_AVOID | CP_SAFE;
			}
			else if (morale_drop < -3)
			{
				//retreat (no clear shot needed)
				cp_flags = CP_RETREAT | CP_COVER | CP_AVOID | CP_SAFE;
			}
			else if (morale_drop < 0)
			{
				//cover (no clear shot needed)
				cp_flags = CP_COVER | CP_AVOID | CP_SAFE;
			}
		}
		else
		{
			const int morale_boost = NPCS.NPCInfo->group->morale - NPCS.NPCInfo->group->numGroup;
			if (morale_boost > 20)
			{
				//charge to any one and outflank (no cover needed)
				cp_flags = CP_CLEAR | CP_FLANK | CP_APPROACH_ENEMY;
				//Saboteur_Decloak(NPCS.NPC, 2000);
			}
			else if (morale_boost > 15)
			{
				//charge to closest one (no cover needed)
				cp_flags = CP_CLEAR | CP_CLOSEST | CP_APPROACH_ENEMY;
			}
			else if (morale_boost > 10)
			{
				//charge closer (no cover needed)
				cp_flags = CP_CLEAR | CP_APPROACH_ENEMY;
			}
		}
	}
	if (!cp_flags)
	{
		//at some medium level of morale
		switch (Q_irand(0, 3))
		{
		case 0: //just take the nearest one
			cp_flags = CP_CLEAR | CP_COVER | CP_NEAREST;
			break;
		case 1: //take one closer to the enemy
			cp_flags = CP_CLEAR | CP_COVER | CP_APPROACH_ENEMY;
			break;
		case 2: //take the one closest to the enemy
			cp_flags = CP_CLEAR | CP_COVER | CP_CLOSEST | CP_APPROACH_ENEMY;
			break;
		case 3: //take the one on the other side of the enemy
			cp_flags = CP_CLEAR | CP_COVER | CP_FLANK | CP_APPROACH_ENEMY;
			break;
		default:;
		}
	}
	if (NPCS.NPC && NPCS.NPCInfo->scriptFlags & SCF_USE_CP_NEAREST)
	{
		cp_flags &= ~(CP_FLANK | CP_APPROACH_ENEMY | CP_CLOSEST);
		cp_flags |= CP_NEAREST;
	}
	return cp_flags;
}

/*
-------------------------
ST_Commander

  Make decisions about who should go where, etc.

FIXME: leader (group-decision-making) AI?
FIXME: need alternate routes!
FIXME: more group voice interaction
FIXME: work in pairs?

-------------------------
*/
static void ST_Commander(void)
{
	// Basic safety: no NPCS.NPC, no info, or no group → nothing to do
	if (!NPCS.NPC || !NPCS.NPCInfo || !NPCS.NPCInfo->group)
	{
		return;
	}

	int i = 0;
	int j = 0;
	int cp = 0;
	int cp_flags = 0;
	int squad_state = 0;
	AIGroupInfo_t* group = NPCS.NPCInfo->group;
	gentity_t* member = NULL;

	qboolean runner = qfalse;
	qboolean enemy_lost = qfalse;
	qboolean enemy_protected = qfalse;
	float avoid_dist = 0.0f;

	group->processed = qtrue;

	// Require a valid enemy with a client
	if (!group->enemy || !group->enemy->inuse || !group->enemy->client)
	{
		return;
	}

	SaveNPCGlobals();

	// If the group has not seen the enemy for 3 minutes, dissolve the group and switch to search
	if (group->lastSeenEnemyTime < level.time - 180000)
	{
		ST_Speech(NPCS.NPC, SPEECH_LOST, 0.0f);

		group->enemy->waypoint = NAV_FindClosestWaypointForEnt(group->enemy, WAYPOINT_NONE);

		for (i = 0; i < group->numGroup; i++)
		{
			const int entNum = group->member[i].number;
			if (entNum < 0 || entNum >= level.num_entities)
			{
				continue;
			}

			member = &g_entities[entNum];
			if (!member->inuse || !member->NPC)
			{
				continue;
			}

			SetNPCGlobals(member);

			if (trap->ICARUS_TaskIDPending((sharedEntity_t*)NPCS.NPC, TID_MOVE_NAV))
			{
				continue;
			}

			if (!(NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES))
			{
				continue;
			}

			G_ClearEnemy(NPCS.NPC);
			NPCS.NPC->waypoint = NAV_FindClosestWaypointForEnt(group->enemy, WAYPOINT_NONE);

			if (NPCS.NPC->waypoint == WAYPOINT_NONE)
			{
				NPCS.NPCInfo->behaviorState = BS_DEFAULT;
			}
			else if (group->enemy->waypoint == WAYPOINT_NONE ||
				trap->Nav_GetPathCost(NPCS.NPC->waypoint, group->enemy->waypoint) >= Q3_INFINITE)
			{
				NPC_BSSearchStart(NPCS.NPC->waypoint, BS_SEARCH);
			}
			else
			{
				NPC_BSSearchStart(group->enemy->waypoint, BS_SEARCH);
			}
		}

		group->enemy = NULL;
		RestoreNPCGlobals();
		return;
	}

	// Someone in the group is in a "running" state?
	if (group->numState[SQUAD_SCOUT] > 0 ||
		group->numState[SQUAD_TRANSITION] > 0 ||
		group->numState[SQUAD_RETREAT] > 0)
	{
		runner = qtrue;
	}

	// “Escaping” speech when enemy unseen for ~30 seconds
	if (group->lastSeenEnemyTime > level.time - 32000 &&
		group->lastSeenEnemyTime < level.time - 30000)
	{
		if (group->commander && !Q_irand(0, 1))
		{
			ST_Speech(group->commander, SPEECH_ESCAPING, 0.0f);
		}
		else
		{
			ST_Speech(NPCS.NPC, SPEECH_ESCAPING, 0.0f);
		}

		NPCS.NPCInfo->blockedSpeechDebounceTime = level.time + 3000;
	}

	// Enemy considered "lost" if not seen for 7 seconds
	if (group->lastSeenEnemyTime < level.time - 7000)
	{
		enemy_lost = qtrue;
	}

	// Enemy considered "protected" if no clear shot for 5 seconds
	if (group->lastClearShotTime < level.time - 5000)
	{
		enemy_protected = qtrue;
	}

	// Asynchronous group AI: process one member per frame if enabled
	int cur_member_num = 0;
	int last_member_num = 0;

	if (d_asynchronousGroupAI.integer)
	{
		group->activeMemberNum++;
		if (group->activeMemberNum >= group->numGroup)
		{
			group->activeMemberNum = 0;
		}
		cur_member_num = group->activeMemberNum;
		last_member_num = cur_member_num + 1;
	}
	else
	{
		cur_member_num = 0;
		last_member_num = group->numGroup;
	}

	for (i = cur_member_num; i < last_member_num; i++)
	{
		cp = -1;
		cp_flags = 0;
		squad_state = SQUAD_IDLE;
		avoid_dist = 0.0f;

		const int entNum = group->member[i].number;
		if (entNum < 0 || entNum >= level.num_entities)
		{
			continue;
		}

		member = &g_entities[entNum];
		if (!member->inuse || !member->NPC)
		{
			continue;
		}

		if (!member->enemy)
		{
			continue;
		}

		SetNPCGlobals(member);

		if (!NPCS.NPC || !NPCS.NPCInfo)
		{
			continue;
		}

		// Skip if currently fleeing
		if (!TIMER_Done(NPCS.NPC, "flee"))
		{
			continue;
		}

		// Skip if already moving via nav task
		if (trap->ICARUS_TaskIDPending((sharedEntity_t*)NPCS.NPC, TID_MOVE_NAV))
		{
			continue;
		}

		// If unarmed and moving to pick up an item, don't override that
		if (NPCS.NPC->s.weapon == WP_NONE &&
			NPCS.NPCInfo->goalEntity &&
			NPCS.NPCInfo->goalEntity == NPCS.NPCInfo->tempGoal &&
			NPCS.NPCInfo->goalEntity->s.eType == ET_ITEM)
		{
			continue;
		}

		// Danger check (if no officer or low rank)
		if (!group->commander ||
			!group->commander->NPC ||
			group->commander->NPC->rank < RANK_ENSIGN)
		{
			if (NPC_CheckForDanger(NPC_CheckAlertEvents(qtrue, qtrue, -1, qfalse, AEL_DANGER)))
			{
				ST_Speech(NPCS.NPC, SPEECH_COVER, 0);
				continue;
			}
		}

		// If not allowed to chase enemies, skip tactical logic
		if (!(NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES))
		{
			continue;
		}

		// Retreat / hide logic when not already in retreat state
		if (NPCS.NPCInfo->squadState != SQUAD_RETREAT)
		{
			// Unarmed: try to flee/hide if appropriate
			if (NPCS.NPC->client->ps.weapon == WP_NONE)
			{
				if (!NPCS.NPCInfo->goalEntity ||
					!NPCS.NPCInfo->goalEntity->enemy ||
					NPCS.NPCInfo->goalEntity->enemy->s.eType != ET_ITEM)
				{
					if (TIMER_Done(NPCS.NPC, "hideTime") ||
						(DistanceSquared(group->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) < 65536.0f &&
							NPC_ClearLOS4(NPCS.NPC->enemy)))
					{
						NPC_StartFlee(NPCS.NPC->enemy,
							NPCS.NPC->enemy->r.currentOrigin,
							AEL_DANGER_GREAT,
							5000,
							10000);
					}
				}
				continue;
			}

			// Armed: consider roaming or cover based on visibility and health
			if (TIMER_Done(NPCS.NPC, "roamTime") &&
				TIMER_Done(NPCS.NPC, "hideTime") &&
				NPCS.NPC->health > 10 &&
				!trap->InPVS(group->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin))
			{
				cp_flags |= CP_CLEAR | CP_COVER;
			}
			else if (NPCS.NPCInfo->localState == LSTATE_UNDERFIRE)
			{
				// Under fire: react based on enemy weapon and health
				switch (group->enemy->client->ps.weapon)
				{
				case WP_SABER:
					if (DistanceSquared(group->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) < 65536.0f)
					{
						cp_flags |= CP_AVOID_ENEMY | CP_COVER | CP_AVOID | CP_RETREAT;
						if (!group->commander ||
							!group->commander->NPC ||
							group->commander->NPC->rank < RANK_ENSIGN)
						{
							squad_state = SQUAD_RETREAT;
						}
						avoid_dist = 256.0f;
					}
					break;

				default:
				case WP_BLASTER:
					cp_flags |= CP_COVER;
					break;
				}

				if (NPCS.NPC->health <= 10)
				{
					if (!group->commander ||
						!group->commander->NPC ||
						group->commander->NPC->rank < RANK_ENSIGN)
					{
						cp_flags |= CP_FLEE | CP_AVOID | CP_RETREAT;
						squad_state = SQUAD_RETREAT;
					}
				}
			}
			else
			{
				// Not under fire: adjust based on weapon and distance
				if (trap->InPVS(NPCS.NPC->r.currentOrigin, group->enemy->r.currentOrigin))
				{
					if (NPCS.NPC->client->ps.weapon == WP_ROCKET_LAUNCHER &&
						DistanceSquared(group->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) < MIN_ROCKET_DIST_SQUARED &&
						NPCS.NPCInfo->squadState != SQUAD_TRANSITION)
					{
						cp_flags |= CP_AVOID_ENEMY | CP_CLEAR | CP_AVOID;
						avoid_dist = 256.0f;
					}
					else
					{
						switch (group->enemy->client->ps.weapon)
						{
						case WP_SABER:
							if (group->enemy->client->saber[0].blade[0].length > 0.0f)
							{
								if (DistanceSquared(group->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) < 65536.0f)
								{
									if (TIMER_Done(NPCS.NPC, "hideTime") &&
										NPCS.NPCInfo->squadState != SQUAD_TRANSITION)
									{
										cp_flags |= CP_AVOID_ENEMY | CP_CLEAR | CP_AVOID;
										avoid_dist = 256.0f;
									}
								}
							}
							break;

						default:
							break;
						}
					}
				}
			}
		}

		// If no enemy‑driven CP flags yet, run tactical / morale logic
		if (!cp_flags)
		{
			if (runner == qtrue && NPCS.NPCInfo->combatPoint != -1)
			{
				// Already has a combat point and group is in motion
				if (NPCS.NPCInfo->squadState != SQUAD_SCOUT &&
					NPCS.NPCInfo->squadState != SQUAD_TRANSITION &&
					NPCS.NPCInfo->squadState != SQUAD_RETREAT)
				{
					if (TIMER_Done(NPCS.NPC, "verifyCP") &&
						DistanceSquared(NPCS.NPC->r.currentOrigin,
							level.combatPoints[NPCS.NPCInfo->combatPoint].origin) > (64.0f * 64.0f))
					{
						cp = NPCS.NPCInfo->combatPoint;
						cp_flags |= ST_GetCPFlags();
					}
					else
					{
						TIMER_Set(NPCS.NPC, "duck", -1);
						TIMER_Set(NPCS.NPC, "attackDelay", -1);
					}
				}
				else
				{
					// If blocked, transfer move goal to the blocking member
					if (NPCS.NPCInfo->aiFlags & NPCAI_BLOCKED)
					{
						for (j = 0; j < group->numGroup; j++)
						{
							if (group->member[j].number == NPCS.NPCInfo->blockingEntNum)
							{
								ST_TransferMoveGoal(NPCS.NPC, &g_entities[group->member[j].number]);
								break;
							}
						}
					}
					continue;
				}
			}
			else
			{
				// Not currently "runner" or no combat point yet
				if (NPCS.NPCInfo->combatPoint != -1)
				{
					if (NPCS.NPCInfo->squadState != SQUAD_SCOUT &&
						NPCS.NPCInfo->squadState != SQUAD_TRANSITION &&
						NPCS.NPCInfo->squadState != SQUAD_RETREAT)
					{
						if (TIMER_Done(NPCS.NPC, "verifyCP"))
						{
							if (DistanceSquared(NPCS.NPC->r.currentOrigin,
								level.combatPoints[NPCS.NPCInfo->combatPoint].origin) > (64.0f * 64.0f))
							{
								cp = NPCS.NPCInfo->combatPoint;
								cp_flags |= ST_GetCPFlags();
							}
						}
					}
				}

				if (enemy_lost == qtrue)
				{
					// Enemy lost: track last seen position and set scout state
					if (group->numState[SQUAD_SCOUT] <= 0)
					{
						NPC_ST_StoreMovementSpeech(SPEECH_CHASE, 0.0f);
					}

					ST_TrackEnemy(NPCS.NPC, group->enemyLastSeenPos);
					AI_GroupUpdateSquadstates(group, NPCS.NPC, SQUAD_SCOUT);
					runner = qtrue;
				}
				else if (enemy_protected == qtrue)
				{
					// Enemy protected: occasionally approach
					if (TIMER_Done(NPCS.NPC, "roamTime") && !Q_irand(0, group->numGroup))
					{
						cp_flags |= ST_ApproachEnemy(NPCS.NPC);
						AI_GroupUpdateSquadstates(group, NPCS.NPC, SQUAD_SCOUT);
					}
				}
				else
				{
					// Normal tactical behavior
					if (NPCS.NPCInfo->combatPoint == -1)
					{
						cp_flags |= ST_GetCPFlags();
					}
					else if (TIMER_Done(NPCS.NPC, "roamTime"))
					{
						const int morale_delta = group->morale - group->numGroup;

						if (i == 0)
						{
							// First member: more aggressive / leading behavior
							if (morale_delta > 0 && !Q_irand(0, 4))
							{
								cp_flags |= CP_CLEAR | CP_COVER | CP_FLANK | CP_APPROACH_ENEMY;
							}
							else if (morale_delta < 0)
							{
								cp_flags |= ST_GetCPFlags();
							}
							else
							{
								TIMER_Set(NPCS.NPC, "roamTime", Q_irand(2000, 5000));
								TIMER_Set(NPCS.NPC, "stick", Q_irand(2000, 5000));
								TIMER_Set(NPCS.NPC, "duck", Q_irand(3000, 4000));
								AI_GroupUpdateSquadstates(group, NPCS.NPC, SQUAD_POINT);
							}
						}
						else if (i == group->numGroup - 1)
						{
							// Last member: more conservative / trailing behavior
							if (morale_delta < 0)
							{
								TIMER_Set(NPCS.NPC, "roamTime", Q_irand(2000, 5000));
								TIMER_Set(NPCS.NPC, "stick", Q_irand(2000, 5000));
							}
							else if (morale_delta > 0)
							{
								cp_flags |= ST_ApproachEnemy(NPCS.NPC);
								AI_GroupUpdateSquadstates(group, NPCS.NPC, SQUAD_SCOUT);
							}
							else
							{
								cp_flags |= ST_GetCPFlags();
							}
						}
						else
						{
							// Middle members: mix of behavior based on morale
							if (morale_delta < 0 || !Q_irand(0, 4))
							{
								cp_flags |= ST_GetCPFlags();
							}
							else
							{
								TIMER_Set(NPCS.NPC, "stick", Q_irand(2000, 4000));
								TIMER_Set(NPCS.NPC, "roamTime", Q_irand(2000, 4000));
							}
						}
					}

					// Idle behavior: look / duck occasionally
					if (!cp_flags)
					{
						if (NPCS.NPC->attackDebounceTime < level.time - 2000)
						{
							ST_Speech(NPCS.NPC, SPEECH_LOOK, 0.9f);
						}

						if (TIMER_Done(NPCS.NPC, "duck"))
						{
							if (TIMER_Done(NPCS.NPC, "stand"))
							{
								if (NPCS.NPCInfo->combatPoint == -1 ||
									(level.combatPoints[NPCS.NPCInfo->combatPoint].flags & CPF_DUCK))
								{
									if (!Q_irand(0, 3))
									{
										TIMER_Set(NPCS.NPC, "duck", Q_irand(1000, 3000));
									}
								}
							}
						}
					}
				}
			}
		}

		// If enemy is lost but still in same region, track directly instead of using CP
		if (enemy_lost == qtrue && NPCS.NPC->enemy != NULL &&
			qtrue == qtrue)
		{
			ST_TrackEnemy(NPCS.NPC, NPCS.NPC->enemy->r.currentOrigin);
			continue;
		}

		if (!NPCS.NPC->enemy)
		{
			continue;
		}

		// Grenade proximity check (thermal detonators)
		if (TIMER_Done(NPCS.NPC, "checkGrenadeTooCloseDebouncer"))
		{
			TIMER_Set(NPCS.NPC, "checkGrenadeTooCloseDebouncer", Q_irand(300, 600));

			vec3_t mins = { 0 };
			vec3_t maxs = { 0 };
			qboolean fled = qfalse;
			gentity_t* ent = NULL;

			static int entity_list[MAX_GENTITIES];

			for (int i1 = 0; i1 < 3; i1++)
			{
				mins[i1] = NPCS.NPC->r.currentOrigin[i1] - 200.0f;
				maxs[i1] = NPCS.NPC->r.currentOrigin[i1] + 200.0f;
			}

			const int num_listed_entities = trap->EntitiesInBox(mins, maxs, entity_list, MAX_GENTITIES);

			for (int e = 0; e < num_listed_entities; e++)
			{
				ent = &g_entities[entity_list[e]];

				if (!ent || !ent->inuse)
				{
					continue;
				}
				if (ent == NPCS.NPC)
				{
					continue;
				}
				if (ent->owner == NPCS.NPC)
				{
					continue;
				}

				if (ent->s.eType == ET_MISSILE &&
					ent->s.weapon == WP_THERMAL)
				{
					if ((!ent->owner || !OnSameTeam(ent->owner, NPCS.NPC)))
					{
						ST_Speech(NPCS.NPC, SPEECH_COVER, 0);
						NPC_StartFlee(NPCS.NPC->enemy,
							ent->r.currentOrigin,
							AEL_DANGER_GREAT,
							1000,
							2000);
						fled = qtrue;
						TIMER_Set(NPCS.NPC, "checkGrenadeTooCloseDebouncer", Q_irand(2000, 4000));
						break;
					}
				}
			}

			if (fled == qtrue)
			{
				continue;
			}
		}

		// Enemy visibility check: if we lose LOS, try to move to a clearer / covered point
		if (TIMER_Done(NPCS.NPC, "checkEnemyVisDebouncer"))
		{
			TIMER_Set(NPCS.NPC, "checkEnemyVisDebouncer", Q_irand(3000, 7000));
			if (!NPC_ClearLOS4(NPCS.NPC->enemy))
			{
				cp_flags |= CP_CLEAR | CP_COVER;
			}
		}

		// Enemy too close for comfort based on our weapon
		if (NPCS.NPC->client->NPC_class != CLASS_ASSASSIN_DROID &&
			NPCS.NPC->client->NPC_class != CLASS_DROIDEKA)
		{
			if (TIMER_Done(NPCS.NPC, "checkEnemyTooCloseDebouncer"))
			{
				TIMER_Set(NPCS.NPC, "checkEnemyTooCloseDebouncer", Q_irand(1000, 6000));

				float dist_threshold = 16384.0f; // 128^2

				switch (NPCS.NPC->s.weapon)
				{
				case WP_ROCKET_LAUNCHER:
				case WP_FLECHETTE:
				case WP_THERMAL:
				case WP_TRIP_MINE:
				case WP_DET_PACK:
					dist_threshold = 65536.0f; // 256^2
					break;

				case WP_REPEATER:
					if (NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)
					{
						dist_threshold = 65536.0f;
					}
					break;

				case WP_CONCUSSION:
					if (!(NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE))
					{
						dist_threshold = 65536.0f;
					}
					break;

				default:
					break;
				}

				if (DistanceSquared(group->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) < dist_threshold)
				{
					cp_flags |= CP_CLEAR | CP_COVER;
				}
			}
		}

		// Clear local state each loop
		NPCS.NPCInfo->localState = LSTATE_NONE;

		// Never force "nearest" here; we manage flags explicitly
		cp_flags &= ~CP_NEAREST;

		// Assign combat points if we have any flags
		if (cp_flags)
		{
			// Adjust avoidance and flags based on enemy saber usage
			if (group->enemy->client->ps.weapon == WP_SABER &&
				group->enemy->client->saber[0].blade[0].length > 0.0f)
			{
				cp_flags |= CP_AVOID_ENEMY;
				avoid_dist = 256.0f;
			}
			else
			{
				cp_flags |= CP_AVOID_ENEMY | CP_HAS_ROUTE | CP_TRYFAR;
				avoid_dist = 200.0f;
			}

			// First attempt: retry‑aware CP search
			if (cp == -1)
			{
				cp = NPC_FindCombatPointRetry(NPCS.NPC->r.currentOrigin,
					NPCS.NPC->r.currentOrigin,
					NPCS.NPC->r.currentOrigin,
					&cp_flags,
					avoid_dist,
					NPCS.NPCInfo->lastFailedCombatPoint);
			}

			// If that fails, progressively relax flags until we find something or fall back to CP_ANY
			while (cp == -1 && cp_flags != CP_ANY)
			{
				if (cp_flags & CP_INVESTIGATE)
				{
					cp_flags &= ~CP_INVESTIGATE;
				}
				else if (cp_flags & CP_SQUAD)
				{
					cp_flags &= ~CP_SQUAD;
				}
				else if (cp_flags & CP_DUCK)
				{
					cp_flags &= ~CP_DUCK;
				}
				else if (cp_flags & CP_NEAREST)
				{
					cp_flags &= ~CP_NEAREST;
				}
				else if (cp_flags & CP_FLANK)
				{
					cp_flags &= ~CP_FLANK;
				}
				else if (cp_flags & CP_SAFE)
				{
					cp_flags &= ~CP_SAFE;
				}
				else if (cp_flags & CP_CLOSEST)
				{
					cp_flags &= ~CP_CLOSEST;
					cp_flags |= CP_APPROACH_ENEMY;
				}
				else if (cp_flags & CP_APPROACH_ENEMY)
				{
					cp_flags &= ~CP_APPROACH_ENEMY;
				}
				else if (cp_flags & CP_COVER)
				{
					cp_flags &= ~CP_COVER;
					cp_flags |= CP_DUCK;
				}
				else if (cp_flags & CP_CLEAR)
				{
					cp_flags &= ~CP_CLEAR;
				}
				else if (cp_flags & CP_AVOID_ENEMY)
				{
					cp_flags &= ~CP_AVOID_ENEMY;
				}
				else if (cp_flags & CP_RETREAT)
				{
					cp_flags &= ~CP_RETREAT;
				}
				else if (cp_flags & CP_FLEE)
				{
					cp_flags &= ~CP_FLEE;
					cp_flags |= CP_COVER | CP_AVOID_ENEMY;
				}
				else if (cp_flags & CP_AVOID)
				{
					cp_flags &= ~CP_AVOID;
				}
				else
				{
					cp_flags = CP_ANY;
				}

				cp = NPC_FindCombatPoint(NPCS.NPC->r.currentOrigin, NPCS.NPC->r.currentOrigin, group->enemy->r.currentOrigin, cp_flags | CP_HAS_ROUTE, avoid_dist, -1);
			}

			// If we found a combat point, commit to it and update squad state / speech
			if (cp != -1)
			{
				runner = qtrue;

				TIMER_Set(NPCS.NPC, "roamTime", Q3_INFINITE);
				TIMER_Set(NPCS.NPC, "verifyCP", Q_irand(1000, 3000));

				NPC_SetCombatPoint(cp);
				NPC_SetMoveGoal(NPCS.NPC, level.combatPoints[cp].origin, 8, qtrue, cp, NULL);

				if (squad_state != SQUAD_IDLE)
				{
					AI_GroupUpdateSquadstates(group, NPCS.NPC, squad_state);
				}
				else if (cp_flags & CP_FLEE)
				{
					AI_GroupUpdateSquadstates(group, NPCS.NPC, SQUAD_RETREAT);
				}
				else
				{
					AI_GroupUpdateSquadstates(group, NPCS.NPC, SQUAD_TRANSITION);
				}

				// Movement / tactical speech based on CP flags
				if ((cp_flags & CP_COVER) && (cp_flags & CP_CLEAR))
				{
					if (group->numGroup > 1)
					{
						NPC_ST_StoreMovementSpeech(SPEECH_OUTFLANK, -1);
					}
				}
				else if (cp_flags & CP_FLANK)
				{
					if (group->numGroup > 1)
					{
						NPC_ST_StoreMovementSpeech(SPEECH_OUTFLANK, -1);
					}
				}
				else if ((cp_flags & CP_COVER) && !(cp_flags & CP_CLEAR))
				{
					NPC_ST_StoreMovementSpeech(SPEECH_COVER, -1);
				}
				else if (Q_irand(0, 3) == 0)
				{
					NPCS.NPCInfo->aiFlags |= NPCAI_STOP_AT_LOS;
				}
				else
				{
					if (group->numGroup > 1)
					{
						float dot = 1.0f;

						if (!Q_irand(0, 3))
						{
							vec3_t e_dir2_me;
							vec3_t e_dir2_cp;

							VectorSubtract(NPCS.NPC->r.currentOrigin, group->enemy->r.currentOrigin, e_dir2_me);
							VectorNormalize(e_dir2_me);

							VectorSubtract(level.combatPoints[cp].origin,
								group->enemy->r.currentOrigin, e_dir2_cp);
							VectorNormalize(e_dir2_cp);

							dot = DotProduct(e_dir2_me, e_dir2_cp);
						}

						if (dot < 0.4f)
						{
							NPC_ST_StoreMovementSpeech(SPEECH_OUTFLANK, -1);
						}
						else if (!Q_irand(0, 10))
						{
							NPC_ST_StoreMovementSpeech(SPEECH_YELL, 0.2f);
						}
					}
					else if ((cp_flags & CP_CLOSEST) || (cp_flags & CP_APPROACH_ENEMY))
					{
						if (group->numGroup > 1)
						{
							NPC_ST_StoreMovementSpeech(SPEECH_CHASE, 0.4f);
						}
					}
					else if (!Q_irand(0, 20))
					{
						if (Q_irand(0, 1))
						{
							NPC_ST_StoreMovementSpeech(SPEECH_OUTFLANK, -1);
						}
						else
						{
							NPC_ST_StoreMovementSpeech(SPEECH_ESCAPING, -1);
						}
					}
				}
			}
			else if (NPCS.NPCInfo->squadState == SQUAD_SCOUT)
			{
				// No CP found while scouting: fall back to hunt behavior
				ST_HuntEnemy(NPCS.NPC);
				AI_GroupUpdateSquadstates(group, NPCS.NPC, SQUAD_SCOUT);
			}
		}
	}

	RestoreNPCGlobals();
}

static qboolean Melee_CanDoGrab(void)
{
	if (NPCS.NPC->client->NPC_class == CLASS_STORMTROOPER || NPCS.NPC->client->NPC_class == CLASS_CLONETROOPER)
	{
		if (NPCS.NPC->enemy && NPCS.NPC->enemy->client)
		{
			//have a valid enemy
			if (TIMER_Done(NPCS.NPC, "grabEnemyDebounce"))
			{
				//okay to grab again
				if (NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE
					&& NPCS.NPC->enemy->client->ps.groundEntityNum != ENTITYNUM_NONE)
				{
					//me and enemy are on ground
					if (!PM_InOnGroundAnim(NPCS.NPC->enemy->client->ps.legsAnim))
					{
						if ((NPCS.NPC->client->ps.weaponTime <= 200 || NPCS.NPC->client->ps.torsoAnim == BOTH_KYLE_GRAB)
							&& !NPCS.NPC->client->ps.saberInFlight)
						{
							if (fabs(NPCS.NPC->enemy->r.currentOrigin[2] - NPCS.NPC->r.currentOrigin[2]) <= 8.0f)
							{
								//close to same level of ground
								if (DistanceSquared(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) <= 10000.0f)
								{
									return qtrue;
								}
							}
						}
					}
				}
			}
		}
	}
	return qfalse;
}

static void Melee_GrabEnemy(void)
{
	TIMER_Set(NPCS.NPC, "grabEnemyDebounce", NPCS.NPC->client->ps.torsoTimer + Q_irand(4000, 20000));
}

void NPC_BSST_Attack(void)
{
	//Don't do anything if we're hurt
	if (NPCS.NPC->painDebounceTime > level.time)
	{
		NPC_UpdateAngles(qtrue, qtrue);

		if (NPCS.NPC->client->ps.torsoAnim == BOTH_KYLE_GRAB)
		{
			//see if we grabbed enemy
			if (NPCS.NPC->client->ps.torsoTimer <= 200)
			{
				if (Melee_CanDoGrab()
					&& NPC_EnemyRangeFromBolt(0) <= 88.0f)
				{
					//grab him!
					Melee_GrabEnemy();
					return;
				}
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_KYLE_MISS, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				NPCS.NPC->client->ps.weaponTime = NPCS.NPC->client->ps.torsoTimer;
				return;
			}
		}
		else
		{
			ST_Speech(NPCS.NPC, SPEECH_COVER, 0);

			NPC_CheckEvasion();
		}
		return;
	}
	if (!st_in_camera && (TIMER_Done(NPCS.NPC, "flee") &&
		NPC_CheckForDanger(NPC_CheckAlertEvents(qtrue, qtrue, -1, qfalse, AEL_DANGER))))
	{
		ST_Speech(NPCS.NPC, SPEECH_COVER, 0);
		NPC_CheckEvasion();
	}

	//If we don't have an enemy, just idle
	if (NPC_CheckEnemyExt(qfalse) == qfalse)
	{
		NPCS.NPC->enemy = NULL;
		if (NPCS.NPC->client->playerTeam == NPCTEAM_PLAYER)
		{
			NPC_BSPatrol();
		}
		else
		{
			NPC_BSST_Patrol();
		}
		return;
	}

	// now if this timer is done and the enemy is no longer in our line of sight, try to get a new enemy later
	if (TIMER_Done(NPCS.NPC, "sje_check_enemy"))
	{
		TIMER_Set(NPCS.NPC, "sje_check_enemy", Q_irand(5000, 10000));

		if (NPCS.NPC->enemy && !NPC_ClearLOS4(NPCS.NPC->enemy) && NPCS.NPC->health > 0)
		{
			// if enemy cant be seen, try getting one later
			if (NPCS.NPC->client)
			{
				NPCS.NPC->enemy = NULL;
				if (NPCS.NPC->client->playerTeam == NPCTEAM_PLAYER)
				{
					NPC_BSPatrol();
				}
				else
				{
					NPC_BSST_Patrol();
				}
				return;
			}
			if (NPCS.NPC->client)
			{
				// guardians have a different way to find enemies. He tries to find the quest player and his allies
				for (int sje_it = 0; sje_it < level.maxclients; sje_it++)
				{
					gentity_t* allied_player = &g_entities[sje_it];

					if (allied_player && allied_player->client &&
						NPC_ClearLOS4(allied_player))
					{
						// the quest player or one of his allies. If one of them is in line of sight, choose him as enemy
						NPCS.NPC->enemy = allied_player;
					}
				}
			}
		}
	}

	//Get our group info
	if (TIMER_Done(NPCS.NPC, "interrogating"))
	{
		AI_GetGroup(NPCS.NPC); //, 45, 512, NPCS.NPC->enemy );
	}
	else
	{
		ST_Speech(NPCS.NPC, SPEECH_YELL, 0);
	}

	if (NPCS.NPCInfo->group)
	{
		//I belong to a squad of guys - we should *always* have a group
		if (!NPCS.NPCInfo->group->processed)
		{
			//I'm the first ent in my group, I'll make the command decisions
#if	AI_TIMERS
			int	startTime = GetTime(0);
#endif//	AI_TIMERS
			ST_Commander();
#if	AI_TIMERS
			int commTime = GetTime(startTime);
			if (commTime > 20)
			{
				Com_Printf(S_COLOR_RED"ERROR: Commander time: %d\n", commTime);
			}
			else if (commTime > 10)
			{
				Com_Printf(S_COLOR_YELLOW"WARNING: Commander time: %d\n", commTime);
			}
			else if (commTime > 2)
			{
				Com_Printf(S_COLOR_GREEN"Commander time: %d\n", commTime);
			}
#endif//	AI_TIMERS
		}
	}
	else if (TIMER_Done(NPCS.NPC, "flee") && NPC_CheckForDanger(NPC_CheckAlertEvents(qtrue, qtrue, -1, qfalse, AEL_DANGER)))
	{
		//not already fleeing, and going to run
		ST_Speech(NPCS.NPC, SPEECH_COVER, 0);
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	if (!NPCS.NPC->enemy)
	{
		//WTF?  somehow we lost our enemy?
		NPC_BSST_Patrol(); //FIXME: or patrol?
		return;
	}

	if (NPCS.NPCInfo->goalEntity && NPCS.NPCInfo->goalEntity != NPCS.NPC->enemy)
	{
		NPCS.NPCInfo->goalEntity = UpdateGoal();
	}

	enemyLOS = enemyCS = enemyInFOV = qfalse;
	doMove = qtrue;
	faceEnemy = qfalse;
	shoot = qfalse;
	hitAlly = qfalse;
	VectorClear(impactPos);
	enemyDist = DistanceSquared(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin);

	vec3_t enemy_dir, shoot_dir;
	VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, enemy_dir);
	VectorNormalize(enemy_dir);
	AngleVectors(NPCS.NPC->client->ps.viewangles, shoot_dir, NULL, NULL);
	const float dot = DotProduct(enemy_dir, shoot_dir);

	if (dot > 0.5f || enemyDist * (1.0f - dot) < 10000)
	{
		//enemy is in front of me or they're very close and not behind me
		enemyInFOV = qtrue;
	}

	if (enemyDist < MIN_ROCKET_DIST_SQUARED) //128
	{
		//enemy within 128
		if ((NPCS.NPC->client->ps.weapon == WP_FLECHETTE || NPCS.NPC->client->ps.weapon == WP_REPEATER) &&
			NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)
		{
			//shooting an explosive, but enemy too close, switch to primary fire
			NPCS.NPCInfo->scriptFlags &= ~SCF_ALT_FIRE;
		}
	}
	else if (enemyDist > 65536) //256 squared
	{
		if (NPCS.NPC->client->ps.weapon == WP_DISRUPTOR)
		{
			//sniping...
			if (!(NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE))
			{
				//use primary fire
				NPCS.NPCInfo->scriptFlags |= SCF_ALT_FIRE;
				//reset fire-timing variables
				NPC_ChangeWeapon(WP_DISRUPTOR);
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
		}
	}

	//can we see our target?
	if (NPC_ClearLOS4(NPCS.NPC->enemy))
	{
		AI_GroupUpdateEnemyLastSeen(NPCS.NPCInfo->group, NPCS.NPC->enemy->r.currentOrigin);
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
		enemyLOS = qtrue;

		if (NPCS.NPC->client->ps.weapon == WP_NONE)
		{
			enemyCS = qfalse; //not qtrue, but should stop us from firing
			NPC_AimAdjust(-1); //adjust aim worse longer we have no weapon
		}
		else
		{
			//can we shoot our target?
			if (enemyDist < MIN_ROCKET_DIST_SQUARED &&
				(NPCS.NPC->client->ps.weapon == WP_ROCKET_LAUNCHER
					|| NPCS.NPC->client->ps.weapon == WP_CONCUSSION && !(NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)
					|| NPCS.NPC->client->ps.weapon == WP_FLECHETTE && NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE))
			{
				enemyCS = qfalse; //not qtrue, but should stop us from firing
				hitAlly = qtrue; //us!
			}
			else if (enemyInFOV)
			{
				//if enemy is FOV, go ahead and check for shooting
				const int hit = NPC_ShotEntity(NPCS.NPC->enemy, impactPos);
				const gentity_t* hitEnt = &g_entities[hit];

				if (hit == NPCS.NPC->enemy->s.number
					|| hitEnt && hitEnt->client && hitEnt->client->playerTeam == NPCS.NPC->client->enemyTeam
					|| hitEnt && hitEnt->takedamage && (hitEnt->r.svFlags & SVF_GLASS_BRUSH || hitEnt->health < 40 ||
						NPCS.NPC
						->s.weapon == WP_EMPLACED_GUN))
				{
					//can hit enemy or enemy ally or will hit glass or other minor breakable (or in emplaced gun), so shoot anyway
					AI_GroupUpdateClearShotTime(NPCS.NPCInfo->group);
					enemyCS = qtrue;
					NPC_AimAdjust(2); //adjust aim better longer we have clear shot at enemy
					VectorCopy(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPCInfo->enemyLastSeenLocation);
				}
				else
				{
					//Hmm, have to get around this bastard
					NPC_AimAdjust(1); //adjust aim better longer we can see enemy
					ST_ResolveBlockedShot(hit);
					if (hitEnt && hitEnt->client && hitEnt->client->playerTeam == NPCS.NPC->client->playerTeam)
					{
						//would hit an ally, don't fire!!!
						hitAlly = qtrue;
					}
					else
					{
						//Check and see where our shot *would* hit... if it's not close to the enemy (within 256?), then don't fire
					}
				}
			}
			else
			{
				enemyCS = qfalse; //not qtrue, but should stop us from firing
			}
		}
	}
	else if (trap->InPVS(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin))
	{
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
		faceEnemy = qtrue;
		NPC_AimAdjust(-1); //adjust aim worse longer we cannot see enemy
	}

	if (NPCS.NPC->client->ps.weapon == WP_NONE)
	{
		faceEnemy = qfalse;
		shoot = qfalse;
	}
	else if (NPCS.NPC->client->ps.weapon == WP_MELEE)
	{
		faceEnemy = qtrue;
		shoot = qtrue;
	}
	else
	{
		if (enemyLOS)
		{
			//FIXME: no need to face enemy if we're moving to some other goal and he's too far away to shoot?
			faceEnemy = qtrue;
		}
		if (enemyCS)
		{
			shoot = qtrue;
		}
	}

	//Check for movement to take care of
	ST_CheckMoveState();

	//See if we should override shooting decision with any special considerations
	ST_CheckFireState();

	if (faceEnemy)
	{
		//face the enemy
		NPC_FaceEnemy(qtrue);
	}

	if (!(NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES))
	{
		//not supposed to chase my enemies
		if (NPCS.NPCInfo->goalEntity == NPCS.NPC->enemy)
		{
			//goal is my entity, so don't doMove
			doMove = qfalse;
		}
	}
	else if (NPCS.NPC->NPC->scriptFlags & SCF_NO_GROUPS)
	{
		NPCS.NPCInfo->goalEntity = enemyLOS ? NULL : NPCS.NPC->enemy;
	}

	if (NPCS.NPC->client->ps.weaponTime && NPCS.NPC->s.weapon == WP_ROCKET_LAUNCHER)
	{
		doMove = qfalse;
	}

	if (!NPCS.ucmd.rightmove)
	{
		//only if not already strafing for some strange reason...?
		if (!TIMER_Done(NPCS.NPC, "strafeLeft"))
		{
			NPCS.ucmd.rightmove = -127;
			//re-check the duck as we might want to be rolling
			VectorClear(NPCS.NPC->client->ps.moveDir);
			doMove = qfalse;
		}
		else if (!TIMER_Done(NPCS.NPC, "strafeRight"))
		{
			NPCS.ucmd.rightmove = 127;
			VectorClear(NPCS.NPC->client->ps.moveDir);
			doMove = qfalse;
		}
	}

	if (NPCS.NPC->client->ps.legsAnim == BOTH_GUARD_LOOKAROUND1)
	{
		//don't doMove when doing silly look around thing
		doMove = qfalse;
	}
	if (doMove)
	{
		//doMove toward goal
		if (NPCS.NPCInfo->goalEntity)
		{
			doMove = ST_Move();

			if ((NPCS.NPC->client->NPC_class != CLASS_ROCKETTROOPER || NPCS.NPC->s.weapon != WP_ROCKET_LAUNCHER || enemyDist <
				MIN_ROCKET_DIST_SQUARED)
				//rockettroopers who use rocket launchers turn around and run if you get too close (closer than 128)
				&& NPCS.ucmd.forwardmove <= -32)
			{
				//moving backwards at least 45 degrees
				if (NPCS.NPCInfo->goalEntity
					&& DistanceSquared(NPCS.NPCInfo->goalEntity->r.currentOrigin, NPCS.NPC->r.currentOrigin) >
					MIN_TURN_AROUND_DIST_SQ)
				{
					//don't stop running backwards if your goal is less than 100 away
					if (TIMER_Done(NPCS.NPC, "runBackwardsDebounce"))
					{
						//not already waiting for next run backwards
						if (!TIMER_Exists(NPCS.NPC, "runningBackwards"))
						{
							//start running backwards
							TIMER_Set(NPCS.NPC, "runningBackwards", Q_irand(500, 1000)); //Q_irand( 2000, 3500 ) );
						}
						else if (TIMER_Done2(NPCS.NPC, "runningBackwards", qtrue))
						{
							//done running backwards
							TIMER_Set(NPCS.NPC, "runBackwardsDebounce", Q_irand(3000, 5000));
						}
					}
				}
			}
			else
			{
				//not running backwards
				//TIMER_Remove( NPCS.NPC, "runningBackwards" );
			}
		}
		else
		{
			doMove = qfalse;
		}
	}

	if (!doMove)
	{
		if (NPCS.NPC->client->NPC_class != CLASS_ASSASSIN_DROID && NPCS.NPC->client->NPC_class != CLASS_DROIDEKA)
		{
			if (!TIMER_Done(NPCS.NPC, "duck"))
			{
				NPCS.ucmd.upmove = -127;
			}
		}
	}
	else
	{
		//stop ducking!
		TIMER_Set(NPCS.NPC, "duck", -1);
	}

	if (g_allowgunnerbash.integer)
	{
		// slap
		if (NPCS.NPC->client->NPC_class == CLASS_STORMCOMMANDO
			&& NPCS.NPC->client->playerTeam == NPCTEAM_ENEMY
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (enemyDist < MELEE_DIST_SQUARED
				&& !NPCS.NPC->client->ps.weaponTime //not firing
				&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
				&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
				//within 80 and in front
			{
				//enemy within 80, if very close, use melee attack to slap away
				if (TIMER_Done(NPCS.NPC, "slapattackDelay"))
				{
					//animate me
					const int swing_anim = BOTH_TUSKENLUNGE1;
					G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex("sound/chars/stofficer1/misc/victory1.mp3"));
					NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(10000, 15000));
					//delay the hurt until the proper point in the anim
					TIMER_Set(NPCS.NPC, "smackTime", 300);
					NPCS.NPCInfo->blockedDebounceTime = 0;
				}
			}
		}

		// SLAP
		if ((NPCS.NPC->client->NPC_class == CLASS_STORMTROOPER || NPCS.NPC->client->NPC_class == CLASS_REBEL)
			&& NPCS.NPC->client->playerTeam == NPCTEAM_ENEMY
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (NPCS.NPC->client->ps.torsoAnim == BOTH_TUSKENATTACK2 || NPCS.NPC->client->ps.torsoAnim == BOTH_A7_HILT)
			{
				shoot = qfalse;
				if (TIMER_Done(NPCS.NPC, "smackTime") && !NPCS.NPCInfo->blockedDebounceTime)
				{
					//time to smack
					//recheck enemyDist and InFront
					if (enemyDist < MELEE_DIST_SQUARED
						&& !NPCS.NPC->client->ps.weaponTime //not firing
						&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
						&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					{
						vec3_t smack_dir;
						VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, smack_dir);
						smack_dir[2] += 30;
						VectorNormalize(smack_dir);
						//hurt them
						G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex(va("sound/weapons/melee/punch%d", Q_irand(1, 4))));
						G_Damage(NPCS.NPC->enemy, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->r.currentOrigin,
							(g_npcspskill.integer + 1) * Q_irand(2, 5), DAMAGE_NO_KNOCKBACK, MOD_MELEE);

						WP_AbsorbKick(NPCS.NPC->enemy, NPCS.NPC, smack_dir);
						//done with the damage
						NPCS.NPCInfo->blockedDebounceTime = 1;
					}
				}
			}
			else
			{
				if (enemyDist < MELEE_DIST_SQUARED
					&& !NPCS.NPC->client->ps.weaponTime //not firing
					&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
					&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					//within 80 and in front
				{
					//enemy within 80, if very close, use melee attack to slap away
					if (TIMER_Done(NPCS.NPC, "slapattackDelay"))
					{
						//animate me
						int swing_anim = 0;
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							swing_anim = BOTH_TUSKENATTACK2;
						}
						else
						{
							swing_anim = BOTH_A7_HILT;
						}
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_OUTFLANK1, EV_OUTFLANK2), 2000);
						NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(10000, 15000));
						}
						else
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(5000, 10000));
						}
						//delay the hurt until the proper point in the anim
						TIMER_Set(NPCS.NPC, "smackTime", 300);
						NPCS.NPCInfo->blockedDebounceTime = 0;
					}
				}
			}
		}

		if ((NPCS.NPC->client->NPC_class == CLASS_STORMTROOPER || NPCS.NPC->client->NPC_class == CLASS_REBEL)
			&& NPCS.NPC->client->playerTeam == NPCTEAM_PLAYER
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (NPCS.NPC->client->ps.torsoAnim == BOTH_TUSKENATTACK2 || NPCS.NPC->client->ps.torsoAnim == BOTH_A7_HILT)
			{
				shoot = qfalse;
				if (TIMER_Done(NPCS.NPC, "smackTime") && !NPCS.NPCInfo->blockedDebounceTime)
				{
					//time to smack
					//recheck enemyDist and InFront
					if (enemyDist < MELEE_DIST_SQUARED
						&& !NPCS.NPC->client->ps.weaponTime //not firing
						&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
						&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					{
						vec3_t smack_dir;
						VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, smack_dir);
						smack_dir[2] += 30;
						VectorNormalize(smack_dir);
						//hurt them
						G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex(va("sound/weapons/melee/punch%d", Q_irand(1, 4))));
						G_Damage(NPCS.NPC->enemy, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->r.currentOrigin,
							(g_npcspskill.integer + 1) * Q_irand(2, 5), DAMAGE_NO_KNOCKBACK, MOD_MELEE);

						WP_AbsorbKick(NPCS.NPC->enemy, NPCS.NPC, smack_dir);
						//done with the damage
						NPCS.NPCInfo->blockedDebounceTime = 1;
					}
				}
			}
			else
			{
				if (enemyDist < MELEE_DIST_SQUARED
					&& !NPCS.NPC->client->ps.weaponTime //not firing
					&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
					&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					//within 80 and in front
				{
					//enemy within 80, if very close, use melee attack to slap away
					if (TIMER_Done(NPCS.NPC, "slapattackDelay"))
					{
						//animate me
						int swing_anim = 0;
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							swing_anim = BOTH_TUSKENATTACK2;
						}
						else
						{
							swing_anim = BOTH_A7_HILT;
						}
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_OUTFLANK1, EV_OUTFLANK2), 2000);
						NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(10000, 15000));
						}
						else
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(5000, 10000));
						}
						//delay the hurt until the proper point in the anim
						TIMER_Set(NPCS.NPC, "smackTime", 300);
						NPCS.NPCInfo->blockedDebounceTime = 0;
					}
				}
			}
		}

		///////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		if (NPCS.NPC->client->NPC_class == CLASS_CLONETROOPER
			&& NPCS.NPC->client->playerTeam == NPCTEAM_ENEMY
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (NPCS.NPC->client->ps.torsoAnim == BOTH_TUSKENATTACK1)
			{
				shoot = qfalse;
				if (TIMER_Done(NPCS.NPC, "smackTime") && !NPCS.NPCInfo->blockedDebounceTime)
				{
					//time to smack
					//recheck enemyDist and InFront
					if (enemyDist < MELEE_DIST_SQUARED
						&& !NPCS.NPC->client->ps.weaponTime //not firing
						&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
						&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					{
						vec3_t smack_dir;
						VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, smack_dir);
						smack_dir[2] += 30;
						VectorNormalize(smack_dir);
						//hurt them
						G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex(va("sound/weapons/melee/punch%d", Q_irand(1, 4))));
						G_Damage(NPCS.NPC->enemy, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->r.currentOrigin,
							(g_npcspskill.integer + 1) * Q_irand(2, 5), DAMAGE_NO_KNOCKBACK, MOD_MELEE);

						WP_AbsorbKick(NPCS.NPC->enemy, NPCS.NPC, smack_dir);
						//done with the damage
						NPCS.NPCInfo->blockedDebounceTime = 1;
					}
				}
			}
			else
			{
				if (enemyDist < MELEE_DIST_SQUARED
					&& !NPCS.NPC->client->ps.weaponTime //not firing
					&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
					&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					//within 80 and in front
				{
					//enemy within 80, if very close, use melee attack to slap away
					if (TIMER_Done(NPCS.NPC, "slapattackDelay"))
					{
						//animate me
						const int swing_anim = BOTH_TUSKENATTACK1;
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_OUTFLANK1, EV_OUTFLANK2), 2000);
						NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(10000, 15000));
						}
						else
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(5000, 10000));
						}
						//delay the hurt until the proper point in the anim
						TIMER_Set(NPCS.NPC, "smackTime", 300);
						NPCS.NPCInfo->blockedDebounceTime = 0;
					}
				}
			}
		}

		// SLAP
		if (NPCS.NPC->client->NPC_class == CLASS_CLONETROOPER
			&& NPCS.NPC->client->playerTeam == NPCTEAM_PLAYER
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (NPCS.NPC->client->ps.torsoAnim == BOTH_TUSKENATTACK1)
			{
				shoot = qfalse;
				if (TIMER_Done(NPCS.NPC, "smackTime") && !NPCS.NPCInfo->blockedDebounceTime)
				{
					//time to smack
					//recheck enemyDist and InFront
					if (enemyDist < MELEE_DIST_SQUARED
						&& !NPCS.NPC->client->ps.weaponTime //not firing
						&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
						&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					{
						vec3_t smack_dir;
						VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, smack_dir);
						smack_dir[2] += 30;
						VectorNormalize(smack_dir);
						//hurt them
						G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex(va("sound/weapons/melee/punch%d", Q_irand(1, 4))));
						G_Damage(NPCS.NPC->enemy, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->r.currentOrigin,
							(g_npcspskill.integer + 1) * Q_irand(2, 5), DAMAGE_NO_KNOCKBACK, MOD_MELEE);

						WP_AbsorbKick(NPCS.NPC->enemy, NPCS.NPC, smack_dir);
						//done with the damage
						NPCS.NPCInfo->blockedDebounceTime = 1;
					}
				}
			}
			else
			{
				if (enemyDist < MELEE_DIST_SQUARED
					&& !NPCS.NPC->client->ps.weaponTime //not firing
					&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
					&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					//within 80 and in front
				{
					//enemy within 80, if very close, use melee attack to slap away
					if (TIMER_Done(NPCS.NPC, "slapattackDelay"))
					{
						//animate me
						const int swing_anim = BOTH_TUSKENATTACK1;
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_OUTFLANK1, EV_OUTFLANK2), 2000);
						NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(10000, 15000));
						}
						else
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(5000, 10000));
						}
						//delay the hurt until the proper point in the anim
						TIMER_Set(NPCS.NPC, "smackTime", 300);
						NPCS.NPCInfo->blockedDebounceTime = 0;
					}
				}
			}
		}
		if (NPCS.NPC->client->NPC_class == CLASS_SBD
			&& NPCS.NPC->client->playerTeam == NPCTEAM_ENEMY
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (NPCS.NPC->client->ps.torsoAnim == BOTH_A7_SLAP_R || NPCS.NPC->client->ps.torsoAnim == BOTH_A7_SLAP_L)
			{
				shoot = qfalse;
				if (TIMER_Done(NPCS.NPC, "smackTime") && !NPCS.NPCInfo->blockedDebounceTime)
				{
					//time to smack
					//recheck enemyDist and InFront
					if (enemyDist < MELEE_DIST_SQUARED
						&& !NPCS.NPC->client->ps.weaponTime //not firing
						&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
						&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					{
						vec3_t smack_dir;
						VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, smack_dir);
						smack_dir[2] += 30;
						VectorNormalize(smack_dir);
						//hurt them
						G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex(va("sound/weapons/melee/punch%d", Q_irand(1, 4))));
						G_Damage(NPCS.NPC->enemy, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->r.currentOrigin,
							(g_npcspskill.integer + 1) * Q_irand(2, 5), DAMAGE_NO_KNOCKBACK, MOD_MELEE);

						WP_AbsorbKick(NPCS.NPC->enemy, NPCS.NPC, smack_dir);
						//done with the damage
						NPCS.NPCInfo->blockedDebounceTime = 1;
					}
				}
			}
			else
			{
				if (enemyDist < MELEE_DIST_SQUARED
					&& !NPCS.NPC->client->ps.weaponTime //not firing
					&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
					&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					//within 80 and in front
				{
					//enemy within 80, if very close, use melee attack to slap away
					if (TIMER_Done(NPCS.NPC, "slapattackDelay"))
					{
						//animate me
						int swing_anim = 0;
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							swing_anim = BOTH_A7_SLAP_R;
						}
						else
						{
							swing_anim = BOTH_A7_SLAP_L;
						}
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_OUTFLANK1, EV_OUTFLANK2), 2000);
						NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(10000, 15000));
						}
						else
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(5000, 10000));
						}
						//delay the hurt until the proper point in the anim
						TIMER_Set(NPCS.NPC, "smackTime", 300);
						NPCS.NPCInfo->blockedDebounceTime = 0;
					}
				}
			}
		}
		if (NPCS.NPC->client->NPC_class == CLASS_SBD
			&& NPCS.NPC->client->playerTeam == NPCTEAM_PLAYER
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (NPCS.NPC->client->ps.torsoAnim == BOTH_A7_SLAP_R || NPCS.NPC->client->ps.torsoAnim == BOTH_A7_SLAP_L)
			{
				shoot = qfalse;
				if (TIMER_Done(NPCS.NPC, "smackTime") && !NPCS.NPCInfo->blockedDebounceTime)
				{
					//time to smack
					//recheck enemyDist and InFront
					if (enemyDist < MELEE_DIST_SQUARED
						&& !NPCS.NPC->client->ps.weaponTime //not firing
						&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
						&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					{
						vec3_t smack_dir;
						VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, smack_dir);
						smack_dir[2] += 30;
						VectorNormalize(smack_dir);
						//hurt them
						G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex(va("sound/weapons/melee/punch%d", Q_irand(1, 4))));
						G_Damage(NPCS.NPC->enemy, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->r.currentOrigin,
							(g_npcspskill.integer + 1) * Q_irand(2, 5), DAMAGE_NO_KNOCKBACK, MOD_MELEE);

						WP_AbsorbKick(NPCS.NPC->enemy, NPCS.NPC, smack_dir);
						//done with the damage
						NPCS.NPCInfo->blockedDebounceTime = 1;
					}
				}
			}
			else
			{
				if (enemyDist < MELEE_DIST_SQUARED
					&& !NPCS.NPC->client->ps.weaponTime //not firing
					&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
					&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					//within 80 and in front
				{
					//enemy within 80, if very close, use melee attack to slap away
					if (TIMER_Done(NPCS.NPC, "slapattackDelay"))
					{
						//animate me
						int swing_anim = 0;
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							swing_anim = BOTH_A7_SLAP_R;
						}
						else
						{
							swing_anim = BOTH_A7_SLAP_L;
						}
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_OUTFLANK1, EV_OUTFLANK2), 2000);
						NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(10000, 15000));
						}
						else
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(5000, 10000));
						}
						//delay the hurt until the proper point in the anim
						TIMER_Set(NPCS.NPC, "smackTime", 300);
						NPCS.NPCInfo->blockedDebounceTime = 0;
					}
				}
			}
		}
		if (NPCS.NPC->client->NPC_class == CLASS_NOGHRI
			&& NPCS.NPC->client->playerTeam == NPCTEAM_ENEMY
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (NPCS.NPC->client->ps.torsoAnim == BOTH_TUSKENATTACK2 || NPCS.NPC->client->ps.torsoAnim == BOTH_A7_HILT)
			{
				shoot = qfalse;
				if (TIMER_Done(NPCS.NPC, "smackTime") && !NPCS.NPCInfo->blockedDebounceTime)
				{
					//time to smack
					//recheck enemyDist and InFront
					if (enemyDist < MELEE_DIST_SQUARED
						&& !NPCS.NPC->client->ps.weaponTime //not firing
						&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
						&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					{
						vec3_t smack_dir;
						VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, smack_dir);
						smack_dir[2] += 30;
						VectorNormalize(smack_dir);
						//hurt them
						G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex(va("sound/weapons/melee/punch%d", Q_irand(1, 4))));
						G_Damage(NPCS.NPC->enemy, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->r.currentOrigin,
							(g_npcspskill.integer + 1) * Q_irand(2, 5), DAMAGE_NO_KNOCKBACK, MOD_MELEE);

						WP_AbsorbKick(NPCS.NPC->enemy, NPCS.NPC, smack_dir);
						//done with the damage
						NPCS.NPCInfo->blockedDebounceTime = 1;
					}
				}
			}
			else
			{
				if (enemyDist < MELEE_DIST_SQUARED
					&& !NPCS.NPC->client->ps.weaponTime //not firing
					&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
					&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					//within 80 and in front
				{
					//enemy within 80, if very close, use melee attack to slap away
					if (TIMER_Done(NPCS.NPC, "slapattackDelay"))
					{
						//animate me
						int swing_anim = 0;
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							swing_anim = BOTH_TUSKENATTACK2;
						}
						else
						{
							swing_anim = BOTH_A7_HILT;
						}
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_OUTFLANK1, EV_OUTFLANK2), 2000);
						NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(10000, 15000));
						}
						else
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(5000, 10000));
						}
						//delay the hurt until the proper point in the anim
						TIMER_Set(NPCS.NPC, "smackTime", 300);
						NPCS.NPCInfo->blockedDebounceTime = 0;
					}
				}
			}
		}
		if (NPCS.NPC->client->NPC_class == CLASS_NOGHRI
			&& NPCS.NPC->client->playerTeam == NPCTEAM_PLAYER
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (NPCS.NPC->client->ps.torsoAnim == BOTH_TUSKENATTACK2 || NPCS.NPC->client->ps.torsoAnim == BOTH_A7_HILT)
			{
				shoot = qfalse;
				if (TIMER_Done(NPCS.NPC, "smackTime") && !NPCS.NPCInfo->blockedDebounceTime)
				{
					//time to smack
					//recheck enemyDist and InFront
					if (enemyDist < MELEE_DIST_SQUARED
						&& !NPCS.NPC->client->ps.weaponTime //not firing
						&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
						&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					{
						vec3_t smack_dir;
						VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, smack_dir);
						smack_dir[2] += 30;
						VectorNormalize(smack_dir);
						//hurt them
						G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex(va("sound/weapons/melee/punch%d", Q_irand(1, 4))));
						G_Damage(NPCS.NPC->enemy, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->r.currentOrigin,
							(g_npcspskill.integer + 1) * Q_irand(2, 5), DAMAGE_NO_KNOCKBACK, MOD_MELEE);

						WP_AbsorbKick(NPCS.NPC->enemy, NPCS.NPC, smack_dir);
						//done with the damage
						NPCS.NPCInfo->blockedDebounceTime = 1;
					}
				}
			}
			else
			{
				if (enemyDist < MELEE_DIST_SQUARED
					&& !NPCS.NPC->client->ps.weaponTime //not firing
					&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
					&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					//within 80 and in front
				{
					//enemy within 80, if very close, use melee attack to slap away
					if (TIMER_Done(NPCS.NPC, "slapattackDelay"))
					{
						//animate me
						int swing_anim = 0;
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							swing_anim = BOTH_TUSKENATTACK2;
						}
						else
						{
							swing_anim = BOTH_A7_HILT;
						}
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_OUTFLANK1, EV_OUTFLANK2), 2000);
						NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						if (NPCS.NPC->health > BLOCKPOINTS_THIRTY)
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(10000, 15000));
						}
						else
						{
							TIMER_Set(NPCS.NPC, "slapattackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(5000, 10000));
						}
						//delay the hurt until the proper point in the anim
						TIMER_Set(NPCS.NPC, "smackTime", 300);
						NPCS.NPCInfo->blockedDebounceTime = 0;
					}
				}
			}
		}

		//slap end
		//==
		if (NPCS.NPC->client->NPC_class == CLASS_IMPERIAL ||
			NPCS.NPC->client->NPC_class == CLASS_REELO ||
			NPCS.NPC->client->NPC_class == CLASS_SABOTEUR
			&& NPCS.NPC->client->playerTeam == NPCTEAM_ENEMY
			&& !NPCS.NPC->client->ps.weaponTime
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (NPCS.NPC->client->ps.torsoAnim == BOTH_A7_KICK_F
				|| NPCS.NPC->client->ps.torsoAnim == BOTH_A7_KICK_F2
				|| NPCS.NPC->client->ps.torsoAnim == BOTH_A7_KICK_B2
				|| NPCS.NPC->client->ps.torsoAnim == BOTH_A7_KICK_B)
			{
				shoot = qfalse;
				if (TIMER_Done(NPCS.NPC, "smackTime") && !NPCS.NPCInfo->blockedDebounceTime)
				{
					//time to smack
					//recheck enemyDist and InFront
					if (enemyDist < MELEE_DIST_SQUARED
						&& !NPCS.NPC->client->ps.weaponTime //not firing
						&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
						&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					{
						vec3_t smack_dir;
						VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, smack_dir);
						smack_dir[2] += 30;
						VectorNormalize(smack_dir);
						//hurt them
						G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex("sound/chars/%s/misc/pain0%d"));
						G_Damage(NPCS.NPC->enemy, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->r.currentOrigin,
							(g_npcspskill.integer + 1) * Q_irand(1, 3), DAMAGE_NO_KNOCKBACK, MOD_MELEE);

						WP_AbsorbKick(NPCS.NPC->enemy, NPCS.NPC, smack_dir);
						//done with the damage
						NPCS.NPCInfo->blockedDebounceTime = 1;
					}
				}
			}
		}

		//slap
		if (NPCS.NPC->client->NPC_class == CLASS_IMPERIAL ||
			NPCS.NPC->client->NPC_class == CLASS_REELO ||
			NPCS.NPC->client->NPC_class == CLASS_SABOTEUR
			&& NPCS.NPC->client->playerTeam == NPCTEAM_ENEMY
			&& !NPCS.NPC->client->ps.weaponTime
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (enemyDist < MELEE_DIST_SQUARED
				&& !NPCS.NPC->client->ps.weaponTime //not firing
				&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
				&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
				//within 80 and in front
			{
				//enemy within 80, if very close, use melee attack to slap away
				if (TIMER_Done(NPCS.NPC, "attackDelay"))
				{
					//animate me
					const int swing_anim = Q_irand(BOTH_A7_KICK_F, BOTH_A7_KICK_F2);
					G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex("sound/weapons/melee/kick2.mp3"));
					NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(15000, 20000));
					//delay the hurt until the proper point in the anim
					TIMER_Set(NPCS.NPC, "smackTime", 300);
					NPCS.NPCInfo->blockedDebounceTime = 0;
				}
			}
			else if (enemyDist < MELEE_DIST_SQUARED
				&& !NPCS.NPC->client->ps.weaponTime //not firing
				&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
				&& !InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, -0.25f))
				//within 80 and generally behind
			{
				//enemy within 80, if very close, use melee attack to slap away
				if (TIMER_Done(NPCS.NPC, "attackDelay"))
				{
					//animate me
					const int swing_anim = Q_irand(BOTH_A7_KICK_B2, BOTH_A7_KICK_B);
					G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex("sound/weapons/melee/kick1.mp3"));
					NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(15000, 20000));
					//delay the hurt until the proper point in the anim
					TIMER_Set(NPCS.NPC, "smackTime", 300);
					NPCS.NPCInfo->blockedDebounceTime = 0;
				}
			}
		}
		//===================================================
		if (NPCS.NPC->client->NPC_class == CLASS_TRANDOSHAN ||
			NPCS.NPC->client->NPC_class == CLASS_WEEQUAY ||
			NPCS.NPC->client->NPC_class == CLASS_RODIAN
			&& NPCS.NPC->client->playerTeam == NPCTEAM_ENEMY
			&& !NPCS.NPC->client->ps.weaponTime
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (NPCS.NPC->client->ps.torsoAnim == BOTH_MELEE_L
				|| NPCS.NPC->client->ps.torsoAnim == BOTH_MELEE_R
				|| NPCS.NPC->client->ps.torsoAnim == BOTH_MELEEUP
				|| NPCS.NPC->client->ps.torsoAnim == BOTH_MELEE3)
			{
				shoot = qfalse;
				if (TIMER_Done(NPCS.NPC, "smackTime") && !NPCS.NPCInfo->blockedDebounceTime)
				{
					//time to smack
					//recheck enemyDist and InFront
					if (enemyDist < MELEE_DIST_SQUARED
						&& !NPCS.NPC->client->ps.weaponTime //not firing
						&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
						&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					{
						vec3_t smack_dir;
						VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, smack_dir);
						smack_dir[2] += 10;
						VectorNormalize(smack_dir);
						//hurt them
						G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex("sound/chars/%s/misc/pain0%d"));
						G_Damage(NPCS.NPC->enemy, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->r.currentOrigin, (g_npcspskill.integer + 1) * 1,
							DAMAGE_NO_KNOCKBACK, MOD_MELEE);

						WP_AbsorbKick(NPCS.NPC->enemy, NPCS.NPC, smack_dir);
						//done with the damage
						NPCS.NPCInfo->blockedDebounceTime = 1;
					}
				}
			}
		}

		//slap
		if (NPCS.NPC->client->NPC_class == CLASS_TRANDOSHAN ||
			NPCS.NPC->client->NPC_class == CLASS_WEEQUAY ||
			NPCS.NPC->client->NPC_class == CLASS_RODIAN
			&& NPCS.NPC->client->playerTeam == NPCTEAM_ENEMY
			&& !NPCS.NPC->client->ps.weaponTime
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (enemyDist < MELEE_DIST_SQUARED
				&& !NPCS.NPC->client->ps.weaponTime //not firing
				&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
				&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
				//within 80 and in front
			{
				//enemy within 80, if very close, use melee attack to slap away
				if (TIMER_Done(NPCS.NPC, "attackDelay"))
				{
					//animate me
					int swing_anim = 0;
					if (NPCS.NPC->health > 50)
					{
						swing_anim = Q_irand(BOTH_MELEEUP, BOTH_MELEE3); //kick
					}
					else
					{
						swing_anim = Q_irand(BOTH_MELEE_R, BOTH_MELEE_L); //kick
					}
					G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex("sound/weapons/melee/kick2.mp3"));
					NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(15000, 20000));
					//delay the hurt until the proper point in the anim
					TIMER_Set(NPCS.NPC, "smackTime", 300);
					NPCS.NPCInfo->blockedDebounceTime = 0;
				}
			}
		}

		if (NPCS.NPC->client->NPC_class == CLASS_BESPIN_COP ||
			NPCS.NPC->client->NPC_class == CLASS_JAN ||
			NPCS.NPC->client->NPC_class == CLASS_PRISONER ||
			NPCS.NPC->client->NPC_class == CLASS_REBEL ||
			NPCS.NPC->client->NPC_class == CLASS_WOOKIE
			&& NPCS.NPC->client->playerTeam == NPCTEAM_PLAYER
			&& !PM_InKnockDown(&NPCS.NPC->client->ps)) //not knocked down ) )
		{
			if (NPCS.NPC->client->ps.torsoAnim == BOTH_A7_KICK_F
				|| NPCS.NPC->client->ps.torsoAnim == BOTH_A7_KICK_F2
				|| NPCS.NPC->client->ps.torsoAnim == BOTH_A7_KICK_B2
				|| NPCS.NPC->client->ps.torsoAnim == BOTH_A7_KICK_B)
			{
				shoot = qfalse;
				if (TIMER_Done(NPCS.NPC, "smackTime") && !NPCS.NPCInfo->blockedDebounceTime)
				{
					//time to smack
					//recheck enemyDist and InFront
					if (enemyDist < MELEE_DIST_SQUARED
						&& !NPCS.NPC->client->ps.weaponTime //not firing
						&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
						&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					{
						vec3_t smack_dir;
						VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, smack_dir);
						smack_dir[2] += 30;
						VectorNormalize(smack_dir);
						//hurt them
						G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex("sound/chars/%s/misc/pain0%d"));
						G_Damage(NPCS.NPC->enemy, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->r.currentOrigin,
							(g_npcspskill.integer + 1) * Q_irand(5, 10), DAMAGE_NO_KNOCKBACK, MOD_MELEE);

						WP_AbsorbKick(NPCS.NPC->enemy, NPCS.NPC, smack_dir);
						//done with the damage
						NPCS.NPCInfo->blockedDebounceTime = 1;
					}
				}
			}
		}

		//slap
		if (NPCS.NPC->client->NPC_class == CLASS_BESPIN_COP ||
			NPCS.NPC->client->NPC_class == CLASS_JAN ||
			NPCS.NPC->client->NPC_class == CLASS_PRISONER ||
			NPCS.NPC->client->NPC_class == CLASS_REBEL ||
			NPCS.NPC->client->NPC_class == CLASS_WOOKIE
			&& NPCS.NPC->client->playerTeam == NPCTEAM_PLAYER
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (enemyDist < MELEE_DIST_SQUARED
				&& !NPCS.NPC->client->ps.weaponTime //not firing
				&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
				&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
				//within 80 and in front
			{
				//enemy within 80, if very close, use melee attack to slap away
				if (TIMER_Done(NPCS.NPC, "attackDelay"))
				{
					//animate me
					const int swing_anim = Q_irand(BOTH_A7_KICK_F, BOTH_A7_KICK_F2);
					G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex("sound/weapons/melee/kick3.mp3"));
					NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(15000, 20000));
					//delay the hurt until the proper point in the anim
					TIMER_Set(NPCS.NPC, "smackTime", 300);
					NPCS.NPCInfo->blockedDebounceTime = 0;
				}
			}
			else if (enemyDist < MELEE_DIST_SQUARED
				&& !NPCS.NPC->client->ps.weaponTime //not firing
				&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
				&& !InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, -0.25f))
				//within 80 and generally behind
			{
				//enemy within 80, if very close, use melee attack to slap away
				if (TIMER_Done(NPCS.NPC, "attackDelay"))
				{
					//animate me
					const int swing_anim = Q_irand(BOTH_A7_KICK_B2, BOTH_A7_KICK_B);
					G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex("sound/weapons/melee/kick1.mp3"));
					NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(15000, 20000));
					//delay the hurt until the proper point in the anim
					TIMER_Set(NPCS.NPC, "smackTime", 300);
					NPCS.NPCInfo->blockedDebounceTime = 0;
				}
			}
		}

		if (NPCS.NPC->client->NPC_class == CLASS_BESPIN_COP ||
			NPCS.NPC->client->NPC_class == CLASS_JAN ||
			NPCS.NPC->client->NPC_class == CLASS_PRISONER ||
			NPCS.NPC->client->NPC_class == CLASS_REBEL ||
			NPCS.NPC->client->NPC_class == CLASS_WOOKIE
			&& !PM_InKnockDown(&NPCS.NPC->client->ps)) //not knocked down ) )
		{
			if (NPCS.NPC->client->ps.torsoAnim == BOTH_A7_KICK_F
				|| NPCS.NPC->client->ps.torsoAnim == BOTH_A7_KICK_F2
				|| NPCS.NPC->client->ps.torsoAnim == BOTH_A7_KICK_B2
				|| NPCS.NPC->client->ps.torsoAnim == BOTH_A7_KICK_B)
			{
				shoot = qfalse;
				if (TIMER_Done(NPCS.NPC, "smackTime") && !NPCS.NPCInfo->blockedDebounceTime)
				{
					//time to smack
					//recheck enemyDist and InFront
					if (enemyDist < MELEE_DIST_SQUARED
						&& !NPCS.NPC->client->ps.weaponTime //not firing
						&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
						&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
					{
						vec3_t smack_dir;
						VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, smack_dir);
						smack_dir[2] += 30;
						VectorNormalize(smack_dir);
						//hurt them
						G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex("sound/chars/%s/misc/pain0%d"));
						G_Damage(NPCS.NPC->enemy, NPCS.NPC, NPCS.NPC, smack_dir, NPCS.NPC->r.currentOrigin,
							(g_npcspskill.integer + 1) * Q_irand(5, 10), DAMAGE_NO_KNOCKBACK, MOD_MELEE);

						WP_AbsorbKick(NPCS.NPC->enemy, NPCS.NPC, smack_dir);
						//done with the damage
						NPCS.NPCInfo->blockedDebounceTime = 1;
					}
				}
			}
		}

		//slap
		if (NPCS.NPC->client->NPC_class == CLASS_BESPIN_COP ||
			NPCS.NPC->client->NPC_class == CLASS_JAN ||
			NPCS.NPC->client->NPC_class == CLASS_PRISONER ||
			NPCS.NPC->client->NPC_class == CLASS_REBEL ||
			NPCS.NPC->client->NPC_class == CLASS_WOOKIE
			&& !PM_InKnockDown(&NPCS.NPC->client->ps))
		{
			if (enemyDist < MELEE_DIST_SQUARED
				&& !NPCS.NPC->client->ps.weaponTime //not firing
				&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
				&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.3f))
				//within 80 and in front
			{
				//enemy within 80, if very close, use melee attack to slap away
				if (TIMER_Done(NPCS.NPC, "attackDelay"))
				{
					//animate me
					const int swing_anim = Q_irand(BOTH_A7_KICK_F, BOTH_A7_KICK_F2);
					G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex("sound/weapons/melee/kick3.mp3"));
					NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(15000, 20000));
					//delay the hurt until the proper point in the anim
					TIMER_Set(NPCS.NPC, "smackTime", 300);
					NPCS.NPCInfo->blockedDebounceTime = 0;
				}
			}
			else if (enemyDist < MELEE_DIST_SQUARED
				&& !NPCS.NPC->client->ps.weaponTime //not firing
				&& !PM_InKnockDown(&NPCS.NPC->client->ps) //not knocked down
				&& !InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, -0.25f))
				//within 80 and generally behind
			{
				//enemy within 80, if very close, use melee attack to slap away
				if (TIMER_Done(NPCS.NPC, "attackDelay"))
				{
					//animate me
					const int swing_anim = Q_irand(BOTH_A7_KICK_B2, BOTH_A7_KICK_B);
					G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex("sound/weapons/melee/kick1.mp3"));
					NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, swing_anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.torsoTimer + Q_irand(15000, 20000));
					//delay the hurt until the proper point in the anim
					TIMER_Set(NPCS.NPC, "smackTime", 300);
					NPCS.NPCInfo->blockedDebounceTime = 0;
				}
			}
		}
	}

	if (NPCS.NPC->client->NPC_class == CLASS_REBORN //cultist using a gun
		&& NPCS.NPCInfo->rank >= RANK_LT_COMM //commando or better
		&& NPCS.NPC->enemy->s.weapon == WP_SABER) //fighting a saber-user
	{
		//commando saboteur vs. jedi/reborn
		//see if we need to avoid their saber
		NPC_EvasionSaber();
	}

	if ( //!TIMER_Done( NPCS.NPC, "flee" ) ||
		doMove && !TIMER_Done(NPCS.NPC, "runBackwardsDebounce"))
	{
		//running away
		faceEnemy = qfalse;
	}

	//FIXME: check scf_face_move_dir here?

	if (!faceEnemy)
	{
		//we want to face in the dir we're running
		if (!doMove)
		{
			//if we haven't moved, we should look in the direction we last looked?
			VectorCopy(NPCS.NPC->client->ps.viewangles, NPCS.NPCInfo->lastPathAngles);
		}
		NPCS.NPCInfo->desiredYaw = NPCS.NPCInfo->lastPathAngles[YAW];
		NPCS.NPCInfo->desiredPitch = 0;
		NPC_UpdateAngles(qtrue, qtrue);
		if (doMove)
		{
			//don't run away and shoot
			shoot = qfalse;
		}
	}

	if (NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE)
	{
		shoot = qfalse;
	}

	if (NPCS.NPC->enemy && NPCS.NPC->enemy->enemy)
	{
		if (NPCS.NPC->enemy->s.weapon == WP_SABER && NPCS.NPC->enemy->enemy->s.weapon == WP_SABER)
		{
			//don't shoot at an enemy jedi who is fighting another jedi, for fear of injuring one or causing rogue blaster deflections (a la Obi Wan/Vader duel at end of ANH)
			shoot = qfalse;
		}
	}
	//FIXME: don't shoot right away!
	if (NPCS.NPC->client->ps.weaponTime)
	{
		if (NPCS.NPC->client->NPC_class == CLASS_SABOTEUR)
		{
			Saboteur_Decloak(NPCS.NPC, 2000);
		}
		if (NPCS.NPC->s.weapon == WP_ROCKET_LAUNCHER
			|| NPCS.NPC->s.weapon == WP_CONCUSSION && !(NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE))
		{
			if (!enemyLOS || !enemyCS)
			{
				//cancel it
				NPCS.NPC->client->ps.weaponTime = 0;
			}
			else
			{
				//delay our next attempt
				TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(3000, 5000));
			}
		}
	}
	else if (shoot)
	{
		//try to shoot if it's time
		if (NPCS.NPC->client->NPC_class == CLASS_SABOTEUR)
		{
			Saboteur_Decloak(NPCS.NPC, 2000);
		}
		if (TIMER_Done(NPCS.NPC, "attackDelay"))
		{
			if (!(NPCS.NPCInfo->scriptFlags & SCF_FIRE_WEAPON)) // we've already fired, no need to do it again here
			{
				WeaponThink();
			}
			//NASTY
			if (NPCS.NPC->s.weapon == WP_ROCKET_LAUNCHER)
			{
				if (NPCS.ucmd.buttons & BUTTON_ATTACK
					&& !doMove
					&& g_npcspskill.integer > 1
					&& !Q_irand(0, 3))
				{
					//every now and then, shoot a homing rocket
					NPCS.ucmd.buttons &= ~BUTTON_ATTACK;
					NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
					NPCS.NPC->client->ps.weaponTime = Q_irand(1000, 2500);
				}
			}
		}
	}
	else
	{
		if (NPCS.NPC->attackDebounceTime < level.time)
		{
			if (NPCS.NPC->client->NPC_class == CLASS_SABOTEUR)
			{
				Saboteur_Cloak(NPCS.NPC);
			}
		}
	}
}

static void Gunner_HoldPosition(void)
{
	// Safety: ensure NPCS.NPCInfo exists
	if (NPCS.NPCInfo == NULL)
	{
		return;
	}

	// Stop active movement toward any goal
	NPCS.NPCInfo->goalEntity = NULL;

	// Holding position means no active combat movement
	NPCS.NPCInfo->combatMove = qfalse;
}

static qboolean Gunner_Move(gentity_t* goal, const qboolean retreat)
{
	// Safety: ensure NPCS.NPC, NPCS.NPCInfo, and client exist
	if (NPCS.NPC == NULL || NPCS.NPCInfo == NULL || NPCS.NPC->client == NULL)
	{
		return qfalse;
	}

	// Safety: ensure goal exists
	if (goal == NULL)
	{
		return qfalse;
	}

	// Set the goal BEFORE moving: NPC_MoveToGoal moves toward NPCS.NPCInfo->goalEntity. It used to be set after
	// the move, so the move went to the old goal (often none) and failed, and the failure below cleared the
	// goal again - the NPCS.NPC stood still until other code happened to give it a goal.
	NPCS.NPCInfo->combatMove = qtrue;
	NPCS.NPCInfo->goalEntity = goal;

	// Attempt to move toward goal using NAV system
	const qboolean moved = (NPC_MoveToGoal(qtrue) == qtrue) ? qtrue : qfalse;

	// Retrieve last navigation info
	navInfo_t info;
	NAV_GetLastMove(&info);

	if (d_combatinfo.integer)
	{
		Com_Printf("Attempt to move toward enemy\n");
	}

	// If we collided with our enemy, stop and hold position
	const qboolean collidedWithEnemy = ((info.flags & NIF_COLLISION) != 0 && info.blocker == NPCS.NPC->enemy) ? qtrue : qfalse;

	if (collidedWithEnemy == qtrue)
	{
		Gunner_HoldPosition();
		return moved;
	}

	// If movement failed, reset goal and combat movement
	if (moved == qfalse)
	{
		Gunner_HoldPosition();
		return qfalse;
	}

	// Retreat: invert movement direction. Done last: this used to be followed by ST_Move(), which ran
	// NPC_MoveToGoal again and replaced the reversed move with a forward one, so a retreating NPCS.NPC
	// alternated between backing off and advancing (stutter).
	if (retreat == qtrue)
	{
		NPCS.ucmd.forwardmove = -NPCS.ucmd.forwardmove;
		NPCS.ucmd.rightmove = -NPCS.ucmd.rightmove;
		VectorScale(NPCS.NPC->client->ps.moveDir, -1.0f, NPCS.NPC->client->ps.moveDir);
	}

	// First successful move: say what we're doing (what ST_Move did here)
	NPC_ST_SayMovementSpeech();

	// Movement succeeded
	return qtrue;
}

static void Gunner_FaceEnemy(const qboolean do_pitch)
{
	// Safety: ensure NPCS.NPC, NPCS.NPCInfo, and enemy exist
	if (NPCS.NPC == NULL || NPCS.NPCInfo == NULL || NPCS.NPC->client == NULL ||
		NPCS.NPC->enemy == NULL || NPCS.NPC->enemy->client == NULL)
	{
		return;
	}

	vec3_t eyes;
	vec3_t enemy_eyes;
	vec3_t angles;

	// Get eye positions for NPCS.NPC and enemy
	CalcEntitySpot(NPCS.NPC, SPOT_HEAD, eyes);
	CalcEntitySpot(NPCS.NPC->enemy, SPOT_HEAD, enemy_eyes);

	// Default: face directly toward enemy
	GetAnglesForDirection(eyes, enemy_eyes, angles);

	// Special kick animations modify facing behaviour
	const int legsAnim = NPCS.NPC->client->ps.legsAnim;

	if (legsAnim == BOTH_A7_KICK_R)
	{
		// Keep enemy to the right (no angle modification needed)
	}
	else if (legsAnim == BOTH_A7_KICK_L)
	{
		// Keep enemy to the left (no angle modification needed)
	}
	else if (legsAnim == BOTH_A7_KICK_RL ||
		legsAnim == BOTH_A7_KICK_BF ||
		legsAnim == BOTH_A7_KICK_S)
	{
		// Keep enemy in front (default behaviour already does this)
	}
	else
	{
		// Normal case: point directly toward enemy
		GetAnglesForDirection(eyes, enemy_eyes, angles);
	}

	// Apply yaw
	NPCS.NPCInfo->desiredYaw = AngleNormalize360(angles[YAW]);

	// Apply pitch if requested
	if (do_pitch == qtrue)
	{
		NPCS.NPCInfo->desiredPitch = AngleNormalize360(angles[PITCH]);

		// Slight downward tilt when saber is in flight
		if (NPCS.NPC->client->ps.saberInFlight == qtrue)
		{
			NPCS.NPCInfo->desiredPitch = AngleNormalize360(NPCS.NPCInfo->desiredPitch + 10.0f);
		}
	}
}

static float GunnerDistanceToEnemy(gentity_t* self)
{
	if (self == NULL || self->enemy == NULL)
	{
		return 99999.0f; // treat as extremely far away
	}

	vec3_t diff;
	VectorSubtract(self->enemy->r.currentOrigin, self->r.currentOrigin, diff);
	return VectorLength(diff);
}

// Can we hit the enemy from here? (Same test the normal stormtrooper AI uses: the shot trace hits the
// enemy, one of the enemy's team, or something minor/breakable in between.)
static qboolean Gunner_HasClearShot(void)
{
	if (NPCS.NPC == NULL || NPCS.NPC->enemy == NULL || NPCS.NPC->client == NULL || NPCS.NPC->client->ps.weapon == WP_NONE)
	{
		return qfalse;
	}

	// Roughly facing the enemy: don't fire while still turning towards him
	if (!InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 0.7f))
	{
		return qfalse;
	}

	vec3_t impact_pos;
	const int hit = NPC_ShotEntity(NPCS.NPC->enemy, impact_pos);
	const gentity_t* hit_ent = &g_entities[hit];

	return (hit == NPCS.NPC->enemy->s.number
		|| (hit_ent->client && hit_ent->client->playerTeam == NPCS.NPC->client->enemyTeam)
		|| (hit_ent->takedamage && ((hit_ent->r.svFlags & SVF_GLASS_BRUSH) || hit_ent->health < 40))) ? qtrue : qfalse;
}

// Advanced (g_npc_is_smart) gunners shoot whenever they have a clear shot within their weapon's range,
// also while moving. They used to fire only inside 64-192 units, so at mid range they stood and waited.
static void Gunner_FireIfClearShot(const float enemy_dist)
{
	float max_fire_dist = NPCS.NPCInfo->stats.visrange;
	if (max_fire_dist > 2048.0f)
	{
		max_fire_dist = 2048.0f;
	}

	if (enemy_dist > max_fire_dist || TIMER_Done(NPCS.NPC, "attackDelay") == qfalse)
	{
		return;
	}

	if (Gunner_HasClearShot() == qtrue)
	{
		NPC_AimAdjust(2); // aim improves while we keep a clear shot, as in the normal AI
		VectorCopy(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPCInfo->enemyLastSeenLocation);
		WeaponThink();
		if (d_combatinfo.integer)
		{
			Com_Printf("Gunner fires\n");
		}
	}
	else
	{
		NPC_AimAdjust(-1);
	}
}

static void Enhanced_Gunner_SetEnemyInfo(vec3_t enemy_dest,
	vec3_t enemy_dir,
	float* enemy_dist,
	vec3_t enemy_movedir,
	float* enemy_movespeed,
	const int prediction)
{
	// Safety: ensure NPCS.NPC, NPCS.NPCInfo, and enemy exist
	if (NPCS.NPC == NULL || NPCS.NPCInfo == NULL || NPCS.NPC->enemy == NULL)
	{
		return;
	}

	// Safety: ensure output pointers are valid
	if (enemy_dist == NULL || enemy_movespeed == NULL)
	{
		return;
	}

	// Case 1: Enemy has no client (non-player entity)
	if (NPCS.NPC->enemy->client == NULL)
	{
		// Enemy is static or non-client; no velocity
		VectorClear(enemy_movedir);
		*enemy_movespeed = 0.0f;

		// Copy enemy origin and raise to head height
		VectorCopy(NPCS.NPC->enemy->r.currentOrigin, enemy_dest);
		enemy_dest[2] += NPCS.NPC->enemy->r.mins[2] + 24.0f;

		// Direction from NPCS.NPC to predicted enemy position
		VectorSubtract(enemy_dest, NPCS.NPC->r.currentOrigin, enemy_dir);

		// Normalize direction and compute distance
		*enemy_dist = VectorNormalize(enemy_dir);
		return;
	}

	// Case 2: Enemy has a client (player or NPCS.NPC)
	// Copy enemy velocity
	VectorCopy(NPCS.NPC->enemy->client->ps.velocity, enemy_movedir);

	// Normalize velocity to get movement direction and speed
	*enemy_movespeed = VectorNormalize(enemy_movedir);

	// Predict enemy position: velocity * (prediction ms)
	// Convert prediction ms to seconds: prediction * 0.001f
	const float timeScale = (*enemy_movespeed) * 0.001f * (float)(prediction);
	VectorMA(NPCS.NPC->enemy->r.currentOrigin, timeScale, enemy_movedir, enemy_dest);

	// Compute direction from NPCS.NPC to predicted enemy position
	VectorSubtract(enemy_dest, NPCS.NPC->r.currentOrigin, enemy_dir);

	// Compute distance minus muzzle offset (weapon tip). This used muzzlePoint[0], the muzzle's world X
	// coordinate, as if it were an offset, so the Distance (and advance/attack/retreat choice) depended on
	// where on the map the NPCS.NPC stood. Use the muzzle's horizontal distance from the NPCS.NPC instead.
	float muzzleReach = sqrtf(DistanceHorizontalSquared(NPCS.NPC->client->renderInfo.muzzlePoint, NPCS.NPC->r.currentOrigin));
	if (muzzleReach > 64.0f)
	{
		muzzleReach = 0.0f; // muzzle point not updated yet (still at the origin of the map)
	}
	const float muzzleOffset = muzzleReach + NPCS.NPC->r.maxs[0] * 1.5f + 16.0f;
	*enemy_dist = VectorNormalize(enemy_dir) - muzzleOffset;
}

static qboolean Gunner_Strafe(const int strafeTimeMin,
	const int strafeTimeMax,
	const int nextStrafeTimeMin,
	const int nextStrafeTimeMax,
	const qboolean walking)
{
	// Safety: ensure NPCS.NPC and NPCS.NPCInfo exist
	if (NPCS.NPC == NULL || NPCS.NPCInfo == NULL)
	{
		return qfalse;
	}

	// Both strafe timers must be done before starting a new strafe
	const qboolean leftDone = (TIMER_Done(NPCS.NPC, "strafeLeft") == qtrue) ? qtrue : qfalse;
	const qboolean rightDone = (TIMER_Done(NPCS.NPC, "strafeRight") == qtrue) ? qtrue : qfalse;

	if (leftDone == qfalse || rightDone == qfalse)
	{
		return qfalse;
	}

	qboolean strafed = qfalse;
	const int strafeTime = Q_irand(strafeTimeMin, strafeTimeMax);

	// Randomly choose left or right bias
	const qboolean chooseLeftFirst = (Q_irand(0, 1) == 1) ? qtrue : qfalse;

	if (chooseLeftFirst == qtrue)
	{
		// Try left first
		if (NPC_MoveDirClear(NPCS.ucmd.forwardmove, -127, qfalse) == qtrue)
		{
			TIMER_Set(NPCS.NPC, "strafeLeft", strafeTime);
			strafed = qtrue;
		}
		else if (NPC_MoveDirClear(NPCS.ucmd.forwardmove, 127, qfalse) == qtrue)
		{
			TIMER_Set(NPCS.NPC, "strafeRight", strafeTime);
			strafed = qtrue;
		}
	}
	else
	{
		// Try right first
		if (NPC_MoveDirClear(NPCS.ucmd.forwardmove, 127, qfalse) == qtrue)
		{
			TIMER_Set(NPCS.NPC, "strafeRight", strafeTime);
			strafed = qtrue;
		}
		else if (NPC_MoveDirClear(NPCS.ucmd.forwardmove, -127, qfalse) == qtrue)
		{
			TIMER_Set(NPCS.NPC, "strafeLeft", strafeTime);
			strafed = qtrue;
		}
	}

	// If we successfully started a strafe
	if (strafed == qtrue)
	{
		// Set cooldown before next strafe attempt
		const int nextStrafeDelay = strafeTime + Q_irand(nextStrafeTimeMin, nextStrafeTimeMax);
		TIMER_Set(NPCS.NPC, "noStrafe", nextStrafeDelay);

		// If walking flag is set, apply slow strafe timer
		if (walking == qtrue)
		{
			TIMER_Set(NPCS.NPC, "walking", strafeTime);
		}

		return qtrue;
	}

	return qfalse;
}

static void Gunner_CombatTimersUpdate(const int enemy_dist)
{
	// Safety: ensure NPCS.NPC, NPCS.NPCInfo, and enemy exist
	if (NPCS.NPC == NULL || NPCS.NPCInfo == NULL)
	{
		return;
	}

	// Handle roam/aggression timer
	if (TIMER_Done(NPCS.NPC, "roamTime") == qtrue)
	{
		// Reset roam timer
		TIMER_Set(NPCS.NPC, "roamTime", Q_irand(2000, 5000));

		// Adjust aggression based on enemy weapon and behaviour
		const qboolean hasEnemy = (NPCS.NPC->enemy != NULL) ? qtrue : qfalse;
		const qboolean enemyHasClient = (hasEnemy == qtrue && NPCS.NPC->enemy->client != NULL) ? qtrue : qfalse;

		if (hasEnemy == qtrue && enemyHasClient == qtrue)
		{
			switch (NPCS.NPC->enemy->client->ps.weapon)
			{
			case WP_SABER:
			{
				// If enemy saber is NOT active, charge harder
				const qboolean saberActive = ((!NPCS.NPC->enemy->client->ps.saberHolstered) == qtrue) ? qtrue : qfalse;

				if (saberActive == qfalse)
				{
					ST_AggressionAdjust(NPCS.NPC, 2);
				}
				else
				{
					ST_AggressionAdjust(NPCS.NPC, 1);
				}
				break;
			}

			case WP_BLASTER:
			case WP_BRYAR_PISTOL:
			case WP_DISRUPTOR:
			case WP_BOWCASTER:
			case WP_REPEATER:
			case WP_DEMP2:
			case WP_FLECHETTE:
			case WP_ROCKET_LAUNCHER:
			{
				// If enemy is not shooting at us, increase aggression
				const qboolean enemyNotShooting = (NPCS.NPC->enemy->attackDebounceTime < level.time) ? qtrue : qfalse;

				if (enemyNotShooting == qtrue)
				{
					ST_AggressionAdjust(NPCS.NPC, 1);
				}

				// If enemy is close enough that we can deflect, increase aggression
				if (enemy_dist < 256)
				{
					ST_AggressionAdjust(NPCS.NPC, 1);
				}
				break;
			}

			default:
				break;
			}
		}
	}

	// Handle strafing timers
	const qboolean noStrafeDone = (TIMER_Done(NPCS.NPC, "noStrafe") == qtrue) ? qtrue : qfalse;
	const qboolean leftDone = (TIMER_Done(NPCS.NPC, "strafeLeft") == qtrue) ? qtrue : qfalse;
	const qboolean rightDone = (TIMER_Done(NPCS.NPC, "strafeRight") == qtrue) ? qtrue : qfalse;

	if (noStrafeDone == qtrue && leftDone == qtrue && rightDone == qtrue)
	{
		// 1 in 5 chance to start strafing
		const qboolean startStrafe = (Q_irand(0, 4) == 0) ? qtrue : qfalse;

		if (startStrafe == qtrue)
		{
			// Attempt to initiate a strafe
			const qboolean strafed = (Gunner_Strafe(1000, 3000, 0, 4000, qtrue) == qtrue) ? qtrue : qfalse;

			if (strafed == qtrue)
			{
				if (d_combatinfo.integer)
				{
					Com_Printf("off strafe\n");
				}
			}
		}
		else
		{
			// Postpone strafing
			TIMER_Set(NPCS.NPC, "noStrafe", Q_irand(1000, 3000));
		}
	}
}

// Straight move towards (or away from) the enemy when the nav system has no route (e.g. maps without
// waypoints). Uses the world direction to the enemy, not our view, so it is right while we are still
// turning to face him.
static void Gunner_DirectMove(const qboolean away)
{
	vec3_t dir;
	VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, dir);
	dir[2] = 0.0f;
	if (VectorNormalize(dir) < 1.0f)
	{
		return;
	}
	if (away == qtrue)
	{
		VectorScale(dir, -1.0f, dir);
	}

	usercmd_t test = NPCS.ucmd;
	G_UcmdMoveForDir(NPCS.NPC, &test, dir);
	if (NPC_MoveDirClear(test.forwardmove, test.rightmove, qfalse) == qtrue)
	{
		NPCS.ucmd.forwardmove = test.forwardmove;
		NPCS.ucmd.rightmove = test.rightmove;
	}
}

// Movement for advanced gunners. The choice (advance, back off, or hold and strafe) is kept for a
// short time instead of being re-made every frame: re-deciding every frame is what made them stand and
// stutter. Timers "gunnerAdvance"/"gunnerRetreat"/"gunnerHold" hold the current choice.
static void Gunner_CombatDistance(const float enemy_dist)
{
	if (NPCS.NPC == NULL || NPCS.NPCInfo == NULL || NPCS.NPC->enemy == NULL)
	{
		return;
	}

	const qboolean advancing = TIMER_Done(NPCS.NPC, "gunnerAdvance") == qfalse;
	const qboolean retreating = TIMER_Done(NPCS.NPC, "gunnerRetreat") == qfalse;
	const qboolean holding = TIMER_Done(NPCS.NPC, "gunnerHold") == qfalse;

	if (!advancing && !retreating && !holding)
	{
		// Pick a new move. Aggressive NPCs (smart mode raises aggression during the fight) push in more.
		const int aggression = NPCS.NPCInfo->stats.aggression;

		if (enemy_dist > 512.0f)
		{
			TIMER_Set(NPCS.NPC, "gunnerAdvance", Q_irand(1000, 2000));
		}
		else if (enemy_dist < 128.0f && aggression < 5)
		{
			TIMER_Set(NPCS.NPC, "gunnerRetreat", Q_irand(600, 1200));
		}
		else if (Q_irand(0, 9) < aggression + 2)
		{
			TIMER_Set(NPCS.NPC, "gunnerAdvance", Q_irand(700, 1500));
		}
		else
		{
			TIMER_Set(NPCS.NPC, "gunnerHold", Q_irand(700, 1500));
		}
	}

	if (TIMER_Done(NPCS.NPC, "gunnerAdvance") == qfalse)
	{
		if (enemy_dist < 96.0f)
		{
			// Close enough: stop pushing (don't run into him), let strafing take over
			TIMER_Set(NPCS.NPC, "gunnerAdvance", 0);
			TIMER_Set(NPCS.NPC, "gunnerHold", Q_irand(500, 1000));
		}
		else if (Gunner_Move(NPCS.NPC->enemy, qfalse) == qfalse)
		{
			Gunner_DirectMove(qfalse);
		}
		if (d_combatinfo.integer)
		{
			Com_Printf("Gunner advance\n");
		}
	}
	else if (TIMER_Done(NPCS.NPC, "gunnerRetreat") == qfalse)
	{
		if (Gunner_Move(NPCS.NPC->enemy, qtrue) == qfalse)
		{
			Gunner_DirectMove(qtrue);
		}
		if (d_combatinfo.integer)
		{
			Com_Printf("Gunner back off\n");
		}
	}
	// else hold: stay put and let the strafe timers (Gunner_CombatTimersUpdate) move us sideways

	Gunner_FaceEnemy(qtrue);
	Gunner_FireIfClearShot(enemy_dist);
}

static void Enhanced_Gunner_Combat(void)
{
	// Safety: ensure NPCS.NPC, NPCS.NPCInfo, and client exist
	if (NPCS.NPC == NULL || NPCS.NPCInfo == NULL || NPCS.NPC->client == NULL)
	{
		return;
	}

	// Must have a valid enemy
	if (NPCS.NPC->enemy == NULL || NPCS.NPC->enemy->health <= 0)
	{
		return;
	}

	vec3_t enemy_dir;
	vec3_t enemy_movedir;
	vec3_t enemy_dest;
	float enemy_dist = 0.0f;
	float enemy_movespeed = 0.0f;

	// Predict enemy position 300ms into the future
	Enhanced_Gunner_SetEnemyInfo(
		enemy_dest,
		enemy_dir,
		&enemy_dist,
		enemy_movedir,
		&enemy_movespeed,
		300);

	// Update aggression, strafing, and roam timers
	Gunner_CombatTimersUpdate(enemy_dist);

	// Core combat distance logic (advance, retreat, attack)
	Gunner_CombatDistance(enemy_dist);

	// Update last seen enemy position
	const qboolean enemyHasClient = (NPCS.NPC->enemy->client != NULL) ? qtrue : qfalse;
	const qboolean bothOnGround = (enemyHasClient == qtrue &&
		NPCS.NPC->enemy->client->ps.groundEntityNum != ENTITYNUM_NONE &&
		NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE) ? qtrue : qfalse;

	if (enemyHasClient == qfalse || bothOnGround == qtrue)
	{
		VectorCopy(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPCInfo->enemyLastSeenLocation);
	}

	NPCS.NPCInfo->enemyLastSeenTime = level.time;

	// Update facing angles
	NPC_UpdateAngles(qtrue, qtrue);

	// Apply strafing/walking timers
	Jedi_TimersApply();

	// Ensure movement direction is safe (no cliffs or walls)
	const qboolean moveDirClear = (NPC_MoveDirClear(NPCS.ucmd.forwardmove, NPCS.ucmd.rightmove, qtrue) == qtrue) ?
		qtrue : qfalse;

	if (moveDirClear == qfalse)
	{
		navInfo_t info;
		NAV_GetLastMove(&info);

		// If micro-nav failed, try macro-nav
		const qboolean usingMacroNav = ((info.flags & NIF_MACRO_NAV) != 0) ? qtrue : qfalse;

		if (usingMacroNav == qfalse)
		{
			NPC_MoveToGoal(qfalse);
		}

		// Reset strafing timers
		TIMER_Set(NPCS.NPC, "strafeLeft", 0);
		TIMER_Set(NPCS.NPC, "strafeRight", 0);
	}
}

static void Enhanced_Attack(void)
{
	// Safety: ensure NPCS.NPC, NPCS.NPCInfo, and client exist before any dereference
	if (NPCS.NPC == NULL || NPCS.NPCInfo == NULL || NPCS.NPC->client == NULL)
	{
		return;
	}

	// If in pain animation, do not attack
	if (NPCS.NPC->painDebounceTime > level.time)
	{
		if (Q_irand(0, 1) == 1)
		{
			// Face enemy while in pain, if possible
			Gunner_FaceEnemy(qtrue);
		}

		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	// Handle simple gloat walk toward fallen enemy (non‑player team)
	if (NPCS.NPC->enemy != NULL)
	{
		const qboolean enemyDead = (NPCS.NPC->enemy->health <= 0) ? qtrue : qfalse;
		const qboolean weKilledEnemy = (NPCS.NPC->enemy->enemy == NPCS.NPC) ? qtrue : qfalse;
		const qboolean isGoodGuy = (NPCS.NPC->client->playerTeam == NPCTEAM_PLAYER) ? qtrue : qfalse;

		if (enemyDead == qtrue && weKilledEnemy == qtrue && isGoodGuy == qfalse)
		{
			// Keep looking for other enemies
			NPCS.NPCInfo->enemyCheckDebounceTime = 0;

			// If still in gloat time, walk toward the corpse if allowed to chase
			if (TIMER_Done(NPCS.NPC, "gloatTime") == qfalse)
			{
				const float distSq = DistanceHorizontalSquared(
					NPCS.NPC->client->renderInfo.eyePoint,
					NPCS.NPC->enemy->r.currentOrigin);

				if (distSq > 4096.0f &&
					(NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES))
				{
					NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
					Gunner_Move(NPCS.NPC->enemy, qfalse);
					NPCS.ucmd.buttons |= BUTTON_WALKING;
				}
				else
				{
					// Reached the fallen enemy; original code did nothing here
				}

				Gunner_FaceEnemy(qtrue);
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
		}
	}

	// Refresh enemy tracking
	NPC_CheckEnemy(qtrue, qtrue, qtrue);

	// Always allow combat movement when attacking
	NPCS.NPCInfo->combatMove = qtrue;

	// Core gunner combat logic (movement, strafing, firing decisions)
	Enhanced_Gunner_Combat();

	// Duck timer: force crouch while active
	if (TIMER_Done(NPCS.NPC, "duck") == qfalse)
	{
		NPCS.ucmd.upmove = -127;
	}

	// Script flag: do not fire
	if ((NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE) != 0)
	{
		NPCS.ucmd.buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_FORCEPOWER);
	}

	// Script flag: no acrobatics
	if ((NPCS.NPCInfo->scriptFlags & SCF_NO_ACROBATICS) != 0)
	{
		NPCS.ucmd.upmove = 0;
		NPCS.NPC->client->ps.fd.forceJumpCharge = 0;
	}

	// Enemy attack vocalization (anger noises) for NPCTEAM_ENEMY
	if ((NPCS.ucmd.buttons & BUTTON_ATTACK) != 0 &&
		NPCS.NPC->client->playerTeam == NPCTEAM_ENEMY)
	{
		const int angerRoll = Q_irand(0, NPCS.NPC->client->pers.maxHealth + 10);
		const qboolean moreHurt = (angerRoll > NPCS.NPC->health) ? qtrue : qfalse;
		const qboolean playSound = (Q_irand(0, 3) == 0) ? qtrue : qfalse;

		if (moreHurt == qtrue && playSound == qtrue)
		{
			G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_COMBAT1, EV_COMBAT3), 10000);
		}
	}

	// If NAV is not moving us but ucmds are, set movement speed manually
	if (VectorCompare(NPCS.NPC->client->ps.moveDir, vec3_origin) == qtrue &&
		(NPCS.ucmd.forwardmove != 0 || NPCS.ucmd.rightmove != 0))
	{
		if ((NPCS.ucmd.buttons & BUTTON_WALKING) != 0)
		{
			NPCS.NPC->client->ps.speed = NPCS.NPCInfo->stats.walkSpeed;
		}
		else
		{
			NPCS.NPC->client->ps.speed = NPCS.NPCInfo->stats.runSpeed;
		}
	}
}

static void NPC_BSST_AttackAdvanced(void)
{
	// Thin wrapper around Enhanced_Attack with evasion and post‑kill enemy swap

	if (NPCS.NPC == NULL || NPCS.NPCInfo == NULL)
	{
		return;
	}

	// Camera‑safe evasion check
	if (st_in_camera == qfalse)
	{
		NPC_CheckEvasion();
	}

	// Main enhanced combat
	Enhanced_Attack();

	// Speech handling
	npc_check_speak(NPCS.NPC);

	// After killing an enemy and not pressing attack, try to pick a new one
	if (NPCS.ucmd.buttons == 0 &&
		NPCS.NPC->enemy != NULL &&
		NPCS.NPC->enemy->health <= 0 &&
		NPCS.NPCInfo->enemyCheckDebounceTime < level.time)
	{
		gentity_t* const sav_enemy = NPCS.NPC->enemy;
		NPCS.NPC->enemy = NULL;

		// Confusion/insanity gate for enemy search
		const qboolean canSearch = (NPCS.NPCInfo->confusionTime < level.time) ? qtrue : qfalse;

		gentity_t* const new_enemy = NPC_CheckEnemy(canSearch, qfalse, qfalse);

		// Restore current enemy pointer
		NPCS.NPC->enemy = sav_enemy;

		if (new_enemy != NULL && new_enemy != sav_enemy)
		{
			// Picked up a new enemy
			NPCS.NPC->lastEnemy = NPCS.NPC->enemy;
			G_SetEnemy(NPCS.NPC, new_enemy);
		}

		// Debounce enemy re‑check
		NPCS.NPCInfo->enemyCheckDebounceTime = level.time + Q_irand(1000, 3000);
	}
}

void NPC_BSST_Default(void)
{
	if (NPCS.NPCInfo->scriptFlags & SCF_FIRE_WEAPON)
	{
		WeaponThink();
	}

	if (Q_stricmp("saboteur", NPCS.NPC->NPC_type) == 0) //Makes saboteur's cloak (MP)
	{
		if (NPCS.NPC->health <= 0 ||
			NPCS.NPC->client->ps.fd.forceGripBeingGripped > level.time ||
			NPCS.NPC->painDebounceTime > level.time)
		{
			//taking pain or being gripped
			Saboteur_Decloak(NPCS.NPC, 2000);
		}
		else if (NPCS.NPC->health > 0
			&& NPCS.NPC->client->ps.fd.forceGripBeingGripped <= level.time
			&& NPCS.NPC->painDebounceTime < level.time)
		{
			//still alive, not taking pain and not being gripped
			Saboteur_Cloak(NPCS.NPC);
		}
	}

	if (!NPCS.NPC->enemy)
	{//don't have an enemy, look for one
		NPC_BSST_Patrol();
	}
	else
	{//have an enemy
		if (NPCS.NPC->enemy->client &&
			(NPCS.NPC->enemy->client->NPC_class == CLASS_UGNAUGHT || NPCS.NPC->enemy->client->NPC_class == CLASS_JAWA) &&
			NPCS.NPC->enemy->enemy != NPCS.NPC
			&& (!NPCS.NPC->enemy->enemy ||
				!NPCS.NPC->enemy->enemy->client ||
				NPCS.NPC->enemy->enemy->client->NPC_class != CLASS_RANCOR &&
				NPCS.NPC->enemy->enemy->client->NPC_class != CLASS_WAMPA))
		{//enemy's enemy is not a client or is not a wampa or rancor (which is scarier than me)
			//they should be scared of ME and no-one else
			G_SetEnemy(NPCS.NPC->enemy, NPCS.NPC);
		}
		else
		{//have an enemy
			if (g_npcspskill.integer > 1)
			{
				if (NPC_CanUseAdvancedFighting())
				{
					TIMER_Set(NPCS.NPC, "advTactics", 2000);
					NPC_CheckGetNewWeapon();
					NPC_BSST_AttackAdvanced();
					if (d_combatinfo.integer)
					{
						Com_Printf("Using Advanced Tactics\n");
					}
				}
				else
				{
					NPC_CheckGetNewWeapon();
					NPC_BSST_Attack();
					if (d_combatinfo.integer)
					{
						Com_Printf("Using Normal Tactics\n");
					}
				}
			}
			else
			{
				NPC_CheckGetNewWeapon();
				NPC_BSST_Attack();
				if (d_combatinfo.integer)
				{
					Com_Printf("Using Basic Tactics\n");
				}
			}

			npc_check_speak(NPCS.NPC);
		}
	}
}

static void npc_check_speak(gentity_t* speaker_npc)
{
	if (g_Advancedaitalk.integer < 1)
	{
		return; // no
	}

	if (!TIMER_Done(speaker_npc, "npcCheckSpeak"))
	{
		return; // not yet...
	}

	TIMER_Set(speaker_npc, "npcCheckSpeak", 5000 + Q_irand(0, 10000));

	// MP: SP predicts the enemy 300 ms ahead (jedi_set_enemy_info); the current distance is close enough here
	const float enemy_dist = NPCS.NPC->enemy ? Distance(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin) : 0.0f;

	if (NPCS.NPC->enemy && NPC_ClearLOS4(NPCS.NPC->enemy)
		&& enemy_dist < 512 && enemy_dist > 64) // Have enemy...
	{
		if (npc_is_dark_jedi(speaker_npc))
		{
			// Do taunt/anger...
			const int voice_event = Q_irand(0, 8);

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