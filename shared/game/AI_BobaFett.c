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

#ifndef SP_GAME // MP-only helpers used by the MP versions below
qboolean Boba_Class(const gentity_t* self)
{
	return self && self->client && self->client->NPC_class == CLASS_BOBAFETT ? qtrue : qfalse; // (SJE: Jango has no bounty hunter AI in MP)
}
#endif

#ifndef SP_GAME // MP: bounty hunter tactics states, as NPC_AI_Jedi.c's (must match)
enum
{
	BOBA_TS_NONE,
	BOBA_TS_RIFLE,
	BOBA_TS_MISSILE,
	BOBA_TS_SNIPER,
	BOBA_TS_FLAMETHROW,
	BOBA_TS_AMBUSHWAIT
};
#endif

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

#ifndef SP_GAME // file-level declarations of the MP versions below
extern void Boba_Tactics(void);
#endif

#ifndef SP_GAME // MP-only helpers used by the MP versions below
qboolean Boba_TacticsLocked(void)
{
	if (!NPCS.NPC->enemy)
	{
		return qfalse;
	}
	Boba_Tactics();
	return NPCS.NPCInfo->localState != BOBA_TS_NONE
		&& NPCS.NPCInfo->localState != BOBA_TS_RIFLE
		&& NPCS.NPCInfo->localState != BOBA_TS_MISSILE ? qtrue : qfalse;
}
#endif

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

#ifndef SP_GAME // file-level declarations of the MP versions below
static int bobaLevel = -1;
#endif

#ifndef SP_GAME // file-level declarations of the MP versions below
static vec3_t averageEnemyDirection;
static vec3_t bobaFootStepLoc;
static int bobaFootStepCount;
static qboolean bobaHadDeathScript;
static int averageEnemyDirectionSamples;
#endif

#ifndef SP_GAME // MP-only helpers used by the MP versions below
static void Boba_LevelCheck(void)
{
	if (bobaLevel != level.startTime)
	{
		bobaLevel = level.startTime;
		bobaHadDeathScript = qfalse;
		bobaFootStepCount = 0;
		averageEnemyDirectionSamples = 0;
		VectorClear(averageEnemyDirection);
	}
}

static qboolean Boba_TryJumpTo(const vec3_t pos)
{
	trace_t tr;
	vec3_t groundTest;

	if (NPCS.NPCInfo->jumpNextCheckTime > level.time || NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		return qfalse;
	}
	NPCS.NPCInfo->jumpNextCheckTime = level.time + Q_irand(1000, 2000);
	VectorCopy(pos, NPCS.NPCInfo->jumpDest);

	// can't jump at a point in the air
	VectorCopy(pos, groundTest);
	groundTest[2] += NPCS.NPC->r.mins[2] * 3;
	trap->Trace(&tr, NPCS.NPCInfo->jumpDest, vec3_origin, vec3_origin, groundTest, NPCS.NPC->s.number,
		NPCS.NPC->clipmask, qfalse, 0, 0);
	if (tr.fraction >= 1.0f)
	{
		return qfalse;
	}
	NPCS.NPCInfo->jumpTarget = NULL;
	NPCS.NPCInfo->jumpMaxXYDist = 750;
	NPCS.NPCInfo->jumpMazZDist = -450;
	NPCS.NPCInfo->jumpTime = 0;
	NPCS.NPCInfo->jumpBackupTime = 0;
	return NPC_TryJump();
}
#endif

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

////////////////////////////////////////////////////////////////////////////////////////
// RAVEN SOFTWARE - STAR WARS: JK II
//  (c) 2002 Activision
//
// Boba Fett
// ---------
// Ah yes, this file is pretty messy.  I've tried to move everything in here, but in fact
// a lot of his AI occurs in the seeker and jedi AI files.  Some of these functions
//
//
//
////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////
// Forward References Of Functions
////////////////////////////////////////////////////////////////////////////////////////
void Boba_Precache(void);
void Mando_Precache(void);
void Boba_DustFallNear(const vec3_t origin, int dustcount);
void Boba_ChangeWeapon(int wp);
qboolean Boba_StopKnockdown(gentity_t* self, const gentity_t* pusher, const vec3_t push_dir,
	qboolean force_knockdown);
extern qboolean PM_InKnockDown(const playerState_t* ps);
extern qboolean PM_InRoll(const playerState_t* ps);

// Flight Related Functions (also used by Rocket Trooper)
//--------------------------------------------------------
qboolean Boba_Flying(const gentity_t* self);
void Boba_FlyStart(gentity_t* self);
void Boba_FlyStop(gentity_t* self);

// Called From NPC_Pain()
//-----------------------------
void Boba_Pain(gentity_t* self, int mod);

// Local: Flame Thrower Weapon
//-----------------------------
void Boba_FireFlameThrower(gentity_t* self);
void Boba_StopFlameThrower(const gentity_t* self);
void Boba_StartFlameThrower(gentity_t* self);
void Boba_DoFlameThrower(gentity_t* self);

// Local: Other Tactics
//----------------------
void Boba_DoAmbushWait(gentity_t* self);
void Boba_DoSniper(const gentity_t* self);

// Local: Respawning
//-------------------
#ifdef SP_GAME
bool Boba_Respawn(void);
#else
qboolean Boba_Respawn(void);
#endif

// Called From Within AI_Jedi && AI_Seeker
//-----------------------------------------
void Boba_Fire(void);
void Boba_FireDecide(void);

// Local: Called From Tactics()
//----------------------------
void Boba_TacticsSelect(void);
#ifdef SP_GAME
bool Boba_CanSeeEnemy(const gentity_t* self);
#else
qboolean Boba_CanSeeEnemy(const gentity_t* self);
#endif

// Called From NPC_RunBehavior()
//-------------------------------
void Boba_Update(void); // Always Called First, Before Any Other Thinking
#ifdef SP_GAME // MP: void Boba_Tactics in NPC_AI_Jedi.c
bool Boba_Tactics(void); // If returns true, Jedi and Seeker AI not used
#endif
#ifdef SP_GAME
bool Boba_Flee(void); // If returns true, Jedi and Seeker AI not used
#else
qboolean Boba_Flee(void);
#endif

////////////////////////////////////////////////////////////////////////////////////////
// External Functions
////////////////////////////////////////////////////////////////////////////////////////
extern void G_SoundAtSpot(vec3_t org, int sound_index, qboolean broadcast);
extern void ChangeWeapon(const gentity_t* ent, int new_weapon);
extern void WP_ResistForcePush(gentity_t* self, const gentity_t* pusher, qboolean no_penalty);
extern void ForceJump(gentity_t* self, const usercmd_t* ucmd);
extern void G_Knockdown(gentity_t* self, gentity_t* attacker, const vec3_t push_dir, float strength, const qboolean breakSaberLock);

extern void CG_DrawEdge(vec3_t start, vec3_t end, int type);
extern void Player_CheckBurn(const gentity_t* self);
extern void player_Burn(const gentity_t* self);
////////////////////////////////////////////////////////////////////////////////////////
// External Data
////////////////////////////////////////////////////////////////////////////////////////
extern cvar_t* g_bobaDebug;

////////////////////////////////////////////////////////////////////////////////////////
// Boba Debug Output
////////////////////////////////////////////////////////////////////////////////////////
#ifndef FINAL_BUILD
#if !defined(CTYPE_H_INC)
#define CTYPE_H_INC
#endif

#if !defined(STDARG_H_INC)
#define STDARG_H_INC
#endif

#if !defined(STDIO_H_INC)
#define STDIO_H_INC
#endif
static void	Boba_Printf(const char* format, ...)
{
	if (SHR_CVAR(g_bobaDebug).integer == 0)
	{
		return;
	}

	static char		string[2][1024];	// in case this is called by nested functions
	static int		index = 0;
	static char		nFormat[300];
	char* buf;

	// Tack On The Standard Format Around The Given Format
	//-----------------------------------------------------
	Com_sprintf(nFormat, sizeof(nFormat), "[BOBA %8d] %s\n", level.time, format);

	// Resolve Remaining Elipsis Parameters Into Newly Formated String
	//-----------------------------------------------------------------
	buf = string[index & 1];
	index++;

	va_list		argptr;
	va_start(argptr, format);
	Q_vsnprintf(buf, sizeof(*string), nFormat, argptr);
	va_end(argptr);

	// Print It To Debug Output Console
	//----------------------------------
	gi.Printf(buf);
}
#else
static void Boba_Printf(const char* format, ...)
{}
#endif

////////////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////////////
#define BOBA_FLAMEDURATION 3000
#define BOBA_FLAMETHROWRANGE 128
#define BOBA_FLAMETHROWSIZE 40
#define BOBA_FLAMETHROWDAMAGEMIN 1 //10;
#define BOBA_FLAMETHROWDAMAGEMAX 2 //40;
#define BOBA_ROCKETRANGEMIN 300
#define BOBA_ROCKETRANGEMAX 2000

////////////////////////////////////////////////////////////////////////////////////////
// Global Data
////////////////////////////////////////////////////////////////////////////////////////
qboolean BobaHadDeathScript = qfalse;
qboolean BobaActive = qfalse;
vec3_t BobaFootStepLoc;
int BobaFootStepCount = 0;

vec3_t AverageEnemyDirection;
int AverageEnemyDirectionSamples;

////////////////////////////////////////////////////////////////////////////////////////
// Enums
////////////////////////////////////////////////////////////////////////////////////////
enum EBobaTacticsState
{
	BTS_NONE,

	// Attack
	//--------
	BTS_RIFLE,
	// Uses Jedi / Seeker Movement
	BTS_MISSILE,
	// Uses Jedi / Seeker Movement
	BTS_SNIPER,
	// Uses Special Movement Internal To This File
	BTS_FLAMETHROW,
	// Locked In Place

	// Waiting
	//---------
	BTS_AMBUSHWAIT,
	// Goto CP & Wait

	BTS_MAX
};

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
extern void Saber_SithSwordPrecache(void);

#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_Precache(void)
{
	G_SoundIndex("sound/chars/boba/bf_blast-off.wav");
	G_SoundIndex("sound/chars/boba/bf_jetpack_lp.wav");
	G_SoundIndex("sound/chars/boba/bf_land.wav");
	G_SoundIndex("sound/weapons/boba/bf_flame.mp3");
	G_SoundIndex("sound/player/footsteps/boot1");
	G_SoundIndex("sound/player/footsteps/boot2");
	G_SoundIndex("sound/player/footsteps/boot3");
	G_SoundIndex("sound/player/footsteps/boot4");
	G_EffectIndex("boba/jetSP");
	G_EffectIndex("boba/fthrw");
	G_EffectIndex("volumetric/black_smoke");
	G_EffectIndex("chunks/dustFall");
	G_EffectIndex("flamethrower/flamethrower_sp");
	G_EffectIndex("flamethrower/flame_impact");

	AverageEnemyDirectionSamples = 0;
	VectorClear(AverageEnemyDirection);
	BobaHadDeathScript = qfalse;
	BobaActive = qtrue;
	BobaFootStepCount = 0;

	RegisterItem(SHR_FindItemForWeapon(WP_EMPLACED_GUN)); //precache the weapon
#ifdef SP_GAME // MP has no Sith sword
	Saber_SithSwordPrecache();
#endif
}
#endif

void Mando_Precache(void)
{
	G_SoundIndex("sound/chars/boba/bf_blast-off.wav");
	G_SoundIndex("sound/chars/boba/bf_jetpack_lp.wav");
	G_SoundIndex("sound/chars/boba/bf_land.wav");
	G_SoundIndex("sound/weapons/boba/bf_flame.mp3");
	G_SoundIndex("sound/player/footsteps/boot1");
	G_SoundIndex("sound/player/footsteps/boot2");
	G_SoundIndex("sound/player/footsteps/boot3");
	G_SoundIndex("sound/player/footsteps/boot4");
	G_EffectIndex("boba/jetSP");
	G_EffectIndex("boba/fthrw");
	G_EffectIndex("volumetric/black_smoke");
	G_EffectIndex("chunks/dustFall");
	G_EffectIndex("flamethrower/flamethrower_sp");
	G_EffectIndex("flamethrower/flame_impact");

	AverageEnemyDirectionSamples = 0;
	VectorClear(AverageEnemyDirection);
	BobaActive = qtrue;
	BobaFootStepCount = 0;

	RegisterItem(SHR_FindItemForWeapon(WP_EMPLACED_GUN)); //precache the weapon
#ifdef SP_GAME // MP has no Sith sword
	Saber_SithSwordPrecache();
#endif
}

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME
void Boba_DustFallNear(const vec3_t origin, const int dustcount)
{
	if (!BobaActive)
	{
		return;
	}

	trace_t test_trace;
	vec3_t test_direction{};
	vec3_t test_start_pos;

	VectorCopy(origin, test_start_pos);
	for (int i = 0; i < dustcount; i++)
	{
		vec3_t test_end_pos;
		test_direction[0] = Q_flrand(0.0f, 1.0f) * 2.0f - 1.0f;
		test_direction[1] = Q_flrand(0.0f, 1.0f) * 2.0f - 1.0f;
		test_direction[2] = 1.0f;

		VectorMA(origin, 1000.0f, test_direction, test_end_pos);
		SHR_Trace(&test_trace, origin, NULL, NULL, test_end_pos, player && player->inuse ? 0 : ENTITYNUM_NONE,
			MASK_SHOT);

		if (!test_trace.startsolid &&
			!test_trace.allsolid &&
			test_trace.fraction > 0.1f &&
			test_trace.fraction < 0.9f)
		{
			SHR_PlayEffect("chunks/dustFall", test_trace.endpos, test_trace.plane.normal);
		}
	}
}
#else // MP keeps its own version of this function
void Boba_DustFallNear(const vec3_t origin, const int dustcount)
{
	trace_t testTrace;
	vec3_t testDirection, testEndPos, start;

	VectorCopy(origin, start);
	for (int i = 0; i < dustcount; i++)
	{
		testDirection[0] = Q_flrand(0.0f, 1.0f) * 2.0f - 1.0f;
		testDirection[1] = Q_flrand(0.0f, 1.0f) * 2.0f - 1.0f;
		testDirection[2] = 1.0f;

		VectorMA(start, 1000.0f, testDirection, testEndPos);
		trap->Trace(&testTrace, start, NULL, NULL, testEndPos, ENTITYNUM_NONE, MASK_SHOT, qfalse, 0, 0);

		if (!testTrace.startsolid && !testTrace.allsolid && testTrace.fraction > 0.1f && testTrace.fraction < 0.9f)
		{
			G_PlayEffectID(G_EffectIndex("chunks/dustFall"), testTrace.endpos, testTrace.plane.normal);
		}
	}
}
#endif

#ifdef SP_GAME // SP only: the MP game has no version of this
void Do_DustFallNear(const vec3_t origin, const int dustcount)
{
	trace_t test_trace;
	vec3_t test_direction{};
	vec3_t test_start_pos;

	VectorCopy(origin, test_start_pos);
	for (int i = 0; i < dustcount; i++)
	{
		vec3_t testEndPos;
		test_direction[0] = Q_flrand(0.0f, 1.0f) * 2.0f - 1.0f;
		test_direction[1] = Q_flrand(0.0f, 1.0f) * 2.0f - 1.0f;
		test_direction[2] = 1.0f;

		VectorMA(origin, 1000.0f, test_direction, testEndPos);
		SHR_Trace(&test_trace, origin, NULL, NULL, testEndPos, player && player->inuse ? 0 : ENTITYNUM_NONE,
			MASK_SHOT);

		if (!test_trace.startsolid &&
			!test_trace.allsolid &&
			test_trace.fraction > 0.1f &&
			test_trace.fraction < 0.9f)
		{
			SHR_PlayEffect("chunks/dustFall", test_trace.endpos, test_trace.plane.normal);
		}
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
// This is just a super silly wrapper around NPC_Change Weapon
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_ChangeWeapon(const int wp)
{
	if (NPCS.NPC->s.weapon == wp)
	{
		return;
	}
	NPC_ChangeWeapon(wp);
	G_AddEvent(NPCS.NPC, EV_GENERAL_SOUND, G_SoundIndex("sound/weapons/change.wav"));
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
// Choose an "anti-knockdown" response
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // SP only: the MP game has no version of this
qboolean Boba_StopKnockdown(gentity_t* self, const gentity_t* pusher, const vec3_t push_dir, const qboolean force_knockdown)
{
	if (self->client->NPC_class != CLASS_BOBAFETT && self->client->NPC_class != CLASS_COMMANDO)
	{
		return qfalse;
	}

	if (self->client->moveType == MT_FLYSWIM)
	{
		//can't knock me down when I'm flying
		return qtrue;
	}

	vec3_t p_dir, fwd, right;
	const vec3_t ang = { 0, self->SHR_currentAngles[YAW], 0 };
	const int strafe_time = Q_irand(1000, 2000);

	AngleVectors(ang, fwd, right, NULL);
	VectorNormalize2(push_dir, p_dir);
	const float f_dot = DotProduct(p_dir, fwd);
	const float r_dot = DotProduct(p_dir, right);

	if (Q_irand(0, 2))
	{
		//flip or roll with it
		usercmd_t temp_Cmd{};
		if (f_dot >= 0.4f)
		{
			temp_Cmd.forwardmove = 127;
			TIMER_Set(self, "moveforward", strafe_time);
		}
		else if (f_dot <= -0.4f)
		{
			temp_Cmd.forwardmove = -127;
			TIMER_Set(self, "moveback", strafe_time);
		}
		else if (r_dot > 0)
		{
			temp_Cmd.rightmove = 127;
			TIMER_Set(self, "strafeRight", strafe_time);
			TIMER_Set(self, "strafeLeft", -1);
		}
		else
		{
			temp_Cmd.rightmove = -127;
			TIMER_Set(self, "strafeLeft", strafe_time);
			TIMER_Set(self, "strafeRight", -1);
		}
		G_AddEvent(self, EV_JUMP, 0);
		if (!Q_irand(0, 1))
		{
			//flip
			self->client->ps.forceJumpCharge = 280; //FIXME: calc this intelligently?
			ForceJump(self, &temp_Cmd);
		}
		else
		{
			//roll
			TIMER_Set(self, "duck", strafe_time);
		}
		self->painDebounceTime = 0; //so we do something
	}
	else if (!Q_irand(0, 1) && force_knockdown)
	{
		//resist
		WP_ResistForcePush(self, pusher, qtrue);
	}
	else
	{
		//fall down
		return qfalse;
	}

	return qtrue;
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
// Is this entity flying
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // SP only: the MP game has no version of this
qboolean Boba_Flying(const gentity_t* self)
{
	assert(
		self && self->client && (self->client->NPC_class == CLASS_BOBAFETT || self->client->NPC_class == CLASS_MANDO));
	return (qboolean)(self->client->moveType == MT_FLYSWIM);
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME
bool Boba_CanSeeEnemy(const gentity_t* self)
{
	assert(
		self && self->NPC && self->client && (self->client->NPC_class == CLASS_BOBAFETT || self->client->NPC_class ==
			CLASS_MANDO || self->client->NPC_class == CLASS_COMMANDO));
	return level.time - self->NPC->enemyLastSeenTime < 1000;
}
#else // MP keeps its own version of this function
static qboolean Boba_CanSeeEnemy(const gentity_t* self)
{
	return level.time - self->NPC->enemyLastSeenTime < 1000 ? qtrue : qfalse;
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // pain callbacks run outside the NPC's think: use self, not the NPC globals
void Boba_Pain(gentity_t* self, const int mod)
{
	if (!self->NPC)
	{
		return;
	}
	if (mod == MOD_SABER && !(self->NPC->aiFlags & NPCAI_FLAMETHROW))
	{
		TIMER_Set(self, "Boba_TacticsSelect", 0); // Hurt By The Saber, Time To Try Something New
	}
	if (self->NPC->aiFlags & NPCAI_FLAMETHROW)
	{
		NPC_SetAnim(self, SETANIM_TORSO, BOTH_FLAMETHROWER, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		self->client->ps.SHR_torsoAnimTimer = level.time - TIMER_Get(self, "falmeTime");
	}
}
#else // MP calls this for every NPC that gets hurt (NPC_Pain), outside any NPC think
void Boba_Pain(gentity_t* self, const int mod)
{
	if (!self->NPC || !Boba_Class(self))
	{
		return;
	}
	if (mod == MOD_SABER && TIMER_Done(self, "flameTime"))
	{
		TIMER_Set(self, "Boba_TacticsSelect", 0);
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_FlyStart(gentity_t* self)
{
	//switch to seeker AI for a while
	if (TIMER_Done(self, "jetRecharge")
		&& !Boba_Flying(self))
	{
		self->client->ps.gravity = 0;
		self->SHR_svFlags |= SVF_CUSTOM_GRAVITY;
		self->client->moveType = MT_FLYSWIM;
		//start jet effect
		self->client->jetPackTime = level.time + Q_irand(3000, 10000);
		if (self->genericBolt1 != -1)
		{
			G_PlayEffect(G_EffectIndex("boba/jetSP"), self->playerModel, self->genericBolt1, self->s.number,
				self->SHR_currentOrigin, qtrue, qtrue);
		}
		if (self->genericBolt2 != -1)
		{
			G_PlayEffect(G_EffectIndex("boba/jetSP"), self->playerModel, self->genericBolt2, self->s.number,
				self->SHR_currentOrigin, qtrue, qtrue);
		}

		//take-off sound
		SHR_SoundOnEnt(self, CHAN_ITEM, "sound/chars/boba/bf_blast-off.wav");
		//jet loop sound
		self->s.loopSound = G_SoundIndex("sound/chars/boba/bf_jetpack_lp.wav");
		if (self->NPC)
		{
			self->count = Q3_INFINITE; // SEEKER shot ammo count
		}
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_FlyStop(gentity_t* self)
{
	self->client->ps.gravity = SHR_CVAR(g_gravity).value;
	self->SHR_svFlags &= ~SVF_CUSTOM_GRAVITY;
	self->client->moveType = MT_RUNJUMP;
	//Stop effect
	self->client->jetPackTime = 0;
	if (self->genericBolt1 != -1)
	{
		G_StopEffect("boba/jetSP", self->playerModel, self->genericBolt1, self->s.number);
	}
	if (self->genericBolt2 != -1)
	{
		G_StopEffect("boba/jetSP", self->playerModel, self->genericBolt2, self->s.number);
	}

	//stop jet loop sound
	SHR_SoundOnEnt(self, CHAN_ITEM, "sound/chars/boba/bf_land.wav");

	self->s.loopSound = 0;
	if (self->NPC)
	{
		self->count = 0; // SEEKER shot ammo count
		TIMER_Set(self, "jetRecharge", Q_irand(1000, 5000));
		TIMER_Set(self, "jumpChaseDebounce", Q_irand(500, 2000));
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
// This func actually does the damage inflicting traces
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_FireFlameThrower(gentity_t* self)
{
	trace_t tr;
	vec3_t start, end, dir;
	CVec3 trace_mins(self->SHR_mins);
	CVec3 trace_maxs(self->SHR_maxs);
	const int damage = BOBA_FLAMETHROWDAMAGEMIN;

	AngleVectors(self->SHR_currentAngles, dir, NULL, NULL);
	dir[2] = 0.0f;
	VectorCopy(self->SHR_currentOrigin, start);
	trace_mins *= 0.5f;
	trace_maxs *= 0.5f;
	start[2] += 40.0f;

	VectorMA(start, 150.0f, dir, end);

	if (SHR_CVAR(g_bobaDebug).integer)
	{
		CG_DrawEdge(start, end, EDGE_IMPACT_POSSIBLE);
	}
	SHR_Trace(&tr, start, self->SHR_mins, self->SHR_maxs, end, self->s.number, MASK_SHOT);

	gentity_t* traceEnt = &g_entities[tr.entityNum];

	if (tr.entityNum < ENTITYNUM_WORLD && traceEnt->takedamage)
	{
		G_Damage(traceEnt, self, self, dir, tr.endpos, damage,
			DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK | DAMAGE_NO_HIT_LOC | DAMAGE_IGNORE_TEAM, MOD_BURNING, HL_NONE);

		if (traceEnt->health > 0 && traceEnt->painDebounceTime > level.time)
		{
			G_Throw(traceEnt, dir, 30);

			if (traceEnt->client)
			{
				if (traceEnt->ghoul2.size())
				{
					if (traceEnt->chestBolt != -1)
					{
						G_PlayEffect(G_EffectIndex("flamethrower/flame_impact"), traceEnt->playerModel,
							traceEnt->chestBolt, traceEnt->s.number, traceEnt->SHR_currentOrigin, 3000, qtrue);
					}
				}
				if (!PM_InRoll(&traceEnt->client->ps)
					&& !PM_InKnockDown(&traceEnt->client->ps))
				{
					NPC_SetAnim(traceEnt, SETANIM_TORSO, BOTH_FACEPROTECT, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				}
			}
			player_Burn(traceEnt);
		}
		else
		{
			Player_CheckBurn(traceEnt);
		}
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_StopFlameThrower(const gentity_t* self)
{
	if (self->s.number < MAX_CLIENTS)
	{
		self->client->ps.SHR_torsoAnimTimer = 0;
		G_StopEffect(G_EffectIndex("flamethrower/flamethrower_sp"), self->playerModel, self->genericBolt3, self->s.number);
		if (self->client->flamethrowerOn)
		{
			self->client->flamethrowerOn = qfalse;
		}
		return;
	}
	if (NPCS.NPCInfo->aiFlags & NPCAI_FLAMETHROW)
	{
		self->NPC->aiFlags &= ~NPCAI_FLAMETHROW;
		self->client->ps.SHR_torsoAnimTimer = 0;

		TIMER_Set(self, "flameTime", 0);
		TIMER_Set(self, "nextAttackDelay", 0);
		TIMER_Set(self, "Boba_TacticsSelect", 0);

		G_StopEffect(G_EffectIndex("flamethrower/flamethrower_sp"), self->playerModel, self->genericBolt3, self->s.number);

		Boba_Printf("FlameThrower OFF");
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_StartFlameThrower(gentity_t* self)
{
	if (!(NPCS.NPCInfo->aiFlags & NPCAI_FLAMETHROW))
	{
		NPC_SetAnim(self, SETANIM_TORSO, BOTH_FLAMETHROWER, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

		self->NPC->aiFlags |= NPCAI_FLAMETHROW;
		self->client->ps.SHR_torsoAnimTimer = BOBA_FLAMEDURATION + 500; //Give it a little extra time to make sure the animation doesn't end before the effect does

		TIMER_Set(self, "flameTime", BOBA_FLAMEDURATION);
		TIMER_Set(self, "nextAttackDelay", BOBA_FLAMEDURATION);
		TIMER_Set(self, "nextFlameDelay", BOBA_FLAMEDURATION * 2);
		TIMER_Set(self, "Boba_TacticsSelect", BOBA_FLAMEDURATION);

		SHR_SoundOnEnt(self, CHAN_WEAPON, "sound/weapons/boba/bf_flame.mp3");
		G_PlayEffect(G_EffectIndex("flamethrower/flamethrower_sp"), self->playerModel, self->genericBolt3, self->s.number,
			self->s.origin, 1);

		Boba_Printf("FlameThrower ON");
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
extern qboolean G_ControlledByPlayer(const gentity_t* self);

#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_DoFlameThrower(gentity_t* self)
{
	if (self->client->ps.jetpackFuel < 10)
	{
		return;
	}
	if (self->s.number < MAX_CLIENTS)
	{
		if (self->client)
		{
			if (!self->client->ps.forcePowerDuration[FP_LIGHTNING])
			{
				NPC_SetAnim(self, SETANIM_TORSO, BOTH_FLAMETHROWER, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				self->client->ps.SHR_torsoAnimTimer = BOBA_FLAMEDURATION + 500; //Give it a little extra time to make sure the animation doesn't end before the effect does
				SHR_SoundOnEnt(self, CHAN_WEAPON, "sound/weapons/boba/bf_flame.mp3");
				G_PlayEffect(G_EffectIndex("flamethrower/flamethrower_sp"), self->playerModel, self->genericBolt3,
					self->s.number, self->s.origin, 1);
				self->client->ps.forcePowerDuration[FP_LIGHTNING] = 1;

				if (self->s.number < MAX_CLIENTS || G_ControlledByPlayer(self))
				{
					//take more if they're burning
					self->client->ps.jetpackFuel -= 4;
				}
			}
			Boba_FireFlameThrower(self);
		}
		return;
	}
	if (!(NPCS.NPCInfo->aiFlags & NPCAI_FLAMETHROW) && TIMER_Done(self, "nextAttackDelay"))
	{
		Boba_StartFlameThrower(self);
	}

	if (NPCS.NPCInfo->aiFlags & NPCAI_FLAMETHROW)
	{
		Boba_FireFlameThrower(self);
	}
}
#endif

#define MANDO_FLAMEDURATION 20000

#ifdef SP_GAME // SP only: the MP game has no version of this
void Mando_DoFlameThrower(gentity_t* self)
{
	if (self->s.number < MAX_CLIENTS)
	{
		if (self->client)
		{
			if (!self->client->ps.forcePowerDuration[FP_LIGHTNING])
			{
				NPC_SetAnim(self, SETANIM_TORSO, BOTH_FLAMETHROWER,
					SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART);
				self->client->ps.SHR_torsoAnimTimer = MANDO_FLAMEDURATION;
				self->client->flamethrowerOn = qtrue;
				SHR_SoundOnEnt(self, CHAN_WEAPON, "sound/weapons/boba/bf_flame.mp3");
				G_PlayEffect(G_EffectIndex("flamethrower/flamethrower_sp"), self->playerModel, self->genericBolt3,
					self->s.number, self->s.origin, 1);
				self->client->ps.forcePowerDuration[FP_LIGHTNING] = 1;

				if (self->s.number < MAX_CLIENTS || G_ControlledByPlayer(self))
				{
					//take more if they're burning
					self->client->ps.jetpackFuel -= 15;
				}
			}
			Boba_FireFlameThrower(self);
		}
	}
}
#endif

#ifdef SP_GAME // only the SP wrist missile code uses it
wristWeapon_t missileStates[4] =
{
	{
		BOBA_MISSILE_ROCKET, FP_FIRST, WP_ROCKET_LAUNCHER, qfalse, 1, BOBA_FLAMEDURATION, 150, BOTH_FLAMETHROWER,
		qfalse, qtrue, qtrue
	},
	{
		BOBA_MISSILE_LASER, FP_GRIP, WP_WRIST_BLASTER, qfalse, 5, BOBA_FLAMEDURATION, 150, BOTH_FLAMETHROWER, qfalse,
		qtrue, qtrue
	},
	{ BOBA_MISSILE_DART, FP_FIRST, WP_DISRUPTOR, qfalse, 1, 1500, 200, BOTH_FLAMETHROWER, qfalse, qfalse, qtrue },
	{ BOBA_MISSILE_VIBROBLADE, FP_DRAIN, WP_MELEE, qfalse, 1, 1000, 100, BOTH_PULL_IMPALE_STAB, qfalse, qfalse, qfalse }
};
#endif

#ifdef SP_GAME // SP only: the MP game has no version of this
static void Boba_VibrobladePunch(gentity_t* self)
{
	const int dummy_force_power = FP_DRAIN;
	if (!self->client->ps.forcePowerDuration[dummy_force_power])
	{
		NPC_SetAnim(self, SETANIM_TORSO, BOTH_PULL_IMPALE_STAB, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		self->client->ps.SHR_torsoAnimTimer = 1000;
		self->client->ps.forcePowerDuration[dummy_force_power] = 1;
		SHR_SoundOnEnt(self, CHAN_WEAPON, va("sound/weapons/sword/swing%d.wav", Q_irand(1, 4)));
	}

	if (self->client->ps.SHR_torsoAnimTimer > 900)
	{
		return;
	}

	if (self->client->ps.weaponTime > 0)
	{
		return;
	}

	if (self->client->ps.SHR_torsoAnimTimer < 50)
	{
		self->client->ps.forcePowerDuration[dummy_force_power] = 0;
		self->client->ps.SHR_torsoAnimTimer = 0;
		return;
	}

	mdxaBone_t boltMatrix;
	vec3_t muzzlePoint;
	vec3_t muzzle_dir;
	SHR_GetBoltMatrix(self, self->handRBolt, boltMatrix, self->SHR_currentAngles, self->SHR_currentOrigin);
	// work the matrix axis stuff into the original axis and origins used.
	SHR_GiveMeVector(boltMatrix, ORIGIN, muzzlePoint);
	SHR_GiveMeVector(boltMatrix, NEGATIVE_Y, muzzle_dir);

	VectorCopy(muzzlePoint, self->client->renderInfo.muzzlePoint);
	VectorCopy(muzzle_dir, self->client->renderInfo.muzzleDir);

	const int old_weapon = self->s.weapon;
	self->s.weapon = WP_MELEE;

	self->client->ps.weaponTime += self->client->ps.SHR_torsoAnimTimer;

	FireWeapon(self, qfalse);

	self->s.weapon = old_weapon;
}
#endif

#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_FireWristMissile(gentity_t* self, const int whichMissile)
{
	static int shots_fired = 0; //only 5 shots allowed for wristlaser; only 1 for missile launcher or dart

	if (whichMissile == BOBA_MISSILE_VIBROBLADE)
	{
		Boba_VibrobladePunch(self);
		return;
	}

	if (self->s.number >= MAX_CLIENTS)
	{
		return;
	}

	if (!self->client)
	{
		return;
	}

	if (self->health <= 0)
	{
		return;
	}

	if (self->client->ps.leanofs)
	{
		return;
	}

	if (cg.zoomMode || in_camera)
	{
		return;
	}

	const int dummy_force_power = missileStates[whichMissile].dummyForcePower;

	if (!self->client->ps.forcePowerDuration[dummy_force_power])
	{
		NPC_SetAnim(self, SETANIM_TORSO, missileStates[whichMissile].fireAnim,
			SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		self->client->ps.SHR_torsoAnimTimer = missileStates[whichMissile].animTimer;
		self->client->ps.forcePowerDuration[dummy_force_power] = 1;
		shots_fired = 0;
	}

	if (self->client->ps.SHR_torsoAnimTimer > missileStates[whichMissile].animTimer - missileStates[whichMissile].animDelay)
	{
		return;
	}

	if (self->client->ps.weaponTime > 0)
	{
		return;
	}

	if (missileStates[whichMissile].hold)
	{
		if (self->client->ps.SHR_torsoAnimTimer < 150)
		{
			self->client->ps.forcePowerDuration[dummy_force_power] = 150;
			return;
		}
	}
	else
	{
		if (self->client->ps.SHR_torsoAnimTimer < 50)
		{
			self->client->ps.forcePowerDuration[dummy_force_power] = 0;
			self->client->ps.SHR_torsoAnimTimer = 0;
			return;
		}
	}

	if (shots_fired >= missileStates[whichMissile].maxShots)
	{
		const vec3_t origin = { 0, 0, 0 };
		G_PlayEffect(G_EffectIndex("repeater/muzzle_smoke"), self->playerModel,
			missileStates[whichMissile].leftBolt ? self->genericBolt3 : self->handRBolt, self->s.number,
			origin);
		//play smoke
		return;
	}

	const int old_weapon = self->s.weapon;

	self->s.weapon = missileStates[whichMissile].whichWeapon;
	const qboolean alt_fire = missileStates[whichMissile].alt_fire;
	if (missileStates[whichMissile].fullyCharged)
	{
		self->client->ps.weaponChargeTime = 0;
	}

	const int add_time = weaponData[self->s.weapon].fireTime;
	self->client->ps.weaponTime += add_time;
	self->client->ps.lastShotTime = level.time;

	const char* effect = NULL;

	const weaponData_t* w_data = &weaponData[self->s.weapon];

	// Try and get a default muzzle so we have one to fall back on
	if (w_data->mMuzzleEffect[0])
	{
		effect = &w_data->mMuzzleEffect[0];
	}

	if (alt_fire)
	{
		// We're alt-firing, so see if we need to override with a custom alt-fire effect
		if (w_data->mAltMuzzleEffect[0])
		{
			effect = &w_data->mAltMuzzleEffect[0];
		}
	}

	if (effect)
	{
		mdxaBone_t boltMatrix;
		vec3_t muzzlePoint;
		vec3_t muzzle_dir;
		SHR_GetBoltMatrix(self, missileStates[whichMissile].leftBolt ? self->genericBolt3 : self->handRBolt, boltMatrix, self->SHR_currentAngles, self->SHR_currentOrigin);
		// work the matrix axis stuff into the original axis and origins used.
		SHR_GiveMeVector(boltMatrix, ORIGIN, muzzlePoint);
		SHR_GiveMeVector(boltMatrix, NEGATIVE_Y, muzzle_dir);

		VectorCopy(muzzlePoint, self->client->renderInfo.muzzlePoint);
		VectorCopy(muzzle_dir, self->client->renderInfo.muzzleDir);

		G_PlayEffect(effect, muzzlePoint, muzzle_dir);
	}

	FireWeapon(self, alt_fire);
	shots_fired++;

	self->s.weapon = old_weapon;
}
#endif

#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_EndWristMissile(const gentity_t* self, const int which_missile)
{
	if (missileStates[which_missile].hold)
	{
		self->client->ps.forcePowerDuration[missileStates[which_missile].dummyForcePower] = 0;
		self->client->ps.SHR_torsoAnimTimer = 0;
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // MP has its own in NPC_AI_Jedi.c
void Boba_DoAmbushWait(gentity_t* self)
{}

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_DoSniper(const gentity_t* self)
{
	if (TIMER_Done(NPCS.NPC, "PickNewSniperPoint"))
	{
		TIMER_Set(NPCS.NPC, "PickNewSniperPoint", Q_irand(15000, 25000));
		const int sniper_point = NPC_FindCombatPoint(NPCS.NPC->SHR_currentOrigin, NULL, NPCS.NPC->SHR_currentOrigin,
			CP_SNIPE | CP_CLEAR | CP_HAS_ROUTE | CP_TRYFAR | CP_HORZ_DIST_COLL,
			0, -1);
		if (sniper_point != -1)
		{
			NPC_SetCombatPoint(sniper_point);
			NPC_SetMoveGoal(NPCS.NPC, level.combatPoints[sniper_point].origin, 20, qtrue, sniper_point, NULL);
		}
	}

	// combatPoint is -1 when no sniper point was found (e.g. maps without combat points)
	if (NPCS.NPCInfo->combatPoint >= 0 && NPCS.NPCInfo->combatPoint < level.numCombatPoints &&
		Distance(NPCS.NPC->SHR_currentOrigin, level.combatPoints[NPCS.NPCInfo->combatPoint].origin) < 50.0f)
	{
		Boba_FireDecide();
	}

	const bool is_on_a_path = !!NPC_MoveToGoal(qtrue);

	// Resolve Blocked Problems
	//--------------------------
	if (NPCS.NPCInfo->aiFlags & NPCAI_BLOCKED &&
		NPCS.NPC->client->moveType != MT_FLYSWIM &&
		level.time - NPCS.NPCInfo->blockedDebounceTime > 3000
		)
	{
		Boba_Printf("BLOCKED: Attempting Jump");
		if (is_on_a_path)
		{
			if (!NPC_TryJump(NPCS.NPCInfo->blockedTargetPosition))
			{
				Boba_Printf("  Failed");
			}
		}
	}

	NPC_FaceEnemy(qtrue);
	NPC_UpdateAngles(qtrue, qtrue);
}
#endif
#endif

////////////////////////////////////////////////////////////////////////////////////////
// Call This function to make Boba actually shoot his current weapon
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_Fire(void)
{
	WeaponThink();

	// If Actually Fired, Decide To Apply Alt Fire And Calc Next Attack Delay
	//------------------------------------------------------------------------
	if (NPCS.ucmd.buttons & BUTTON_ATTACK)
	{
		switch (NPCS.NPC->s.weapon)
		{
		case WP_ROCKET_LAUNCHER:
			TIMER_Set(NPCS.NPC, "nextAttackDelay", Q_irand(1000, 2000));

			// Occasionally Shoot A Homing Missile
			//-------------------------------------
			if (!Q_irand(0, 3))
			{
				NPCS.ucmd.buttons &= ~BUTTON_ATTACK;
				NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
				NPCS.NPC->client->fireDelay = Q_irand(1000, 3000);
			}
			break;

		case WP_DISRUPTOR:
			TIMER_Set(NPCS.NPC, "nextAttackDelay", Q_irand(1000, 4000));

			// Occasionally Alt-Fire
			//-----------------------
			if (!Q_irand(0, 3))
			{
				NPCS.ucmd.buttons &= ~BUTTON_ATTACK;
				NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
				NPCS.NPC->client->fireDelay = Q_irand(1000, 3000);
			}
			break;

		case WP_BLASTER:

			if (TIMER_Done(NPCS.NPC, "nextBlasterAltFireDecide"))
			{
				if (Q_irand(0, NPCS.NPC->count * 2 + 3) > 2)
				{
					TIMER_Set(NPCS.NPC, "nextBlasterAltFireDecide", Q_irand(3000, 8000));
					if (!(NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE))
					{
						Boba_Printf("ALT FIRE On");
						NPCS.NPCInfo->scriptFlags |= SCF_ALT_FIRE;
						NPC_ChangeWeapon(WP_BLASTER); // Update Delay Timers
					}
				}
				else
				{
					TIMER_Set(NPCS.NPC, "nextBlasterAltFireDecide", Q_irand(2000, 5000));
					if (NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)
					{
						Boba_Printf("ALT FIRE Off");
						NPCS.NPCInfo->scriptFlags &= ~SCF_ALT_FIRE;
						NPC_ChangeWeapon(WP_BLASTER); // Update Delay Timers
					}
				}
			}

			// Occasionally Alt Fire
			//-----------------------
			if (NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)
			{
				NPCS.ucmd.buttons &= ~BUTTON_ATTACK;
				NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
			}
			break;
		default:;
		}
	}
}
#endif

static qboolean isBobaClass(void)
{
	if (NPCS.NPC->client->NPC_class == CLASS_BOBAFETT || NPCS.NPC->client->NPC_class == CLASS_MANDO)
		return qtrue;

	return qfalse;
}

////////////////////////////////////////////////////////////////////////////////////////
// Call this function to see if Fett should fire his current weapon
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_FireDecide(void)
{
	// Any Reason Not To Shoot?
	//--------------------------
	if (!NPCS.NPC || // Only NPCs
		!NPCS.NPC->client || // Only Clients
		!isBobaClass() || // Only Boba
		!NPCS.NPC->enemy || // Only If There Is An Enemy
		NPCS.NPC->s.weapon == WP_NONE || // Only If Using A Valid Weapon
		!TIMER_Done(NPCS.NPC, "nextAttackDelay") || // Only If Ready To Shoot Again
		!Boba_CanSeeEnemy(NPCS.NPC) // Only If Enemy Recently Seen
		)
	{
		return;
	}

	// Now Check Weapon Specific Parameters To See If We Should Shoot Or Not
	//-----------------------------------------------------------------------
	switch (NPCS.NPC->s.weapon)
	{
	case WP_ROCKET_LAUNCHER:
		if (Distance(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->enemy->SHR_currentOrigin) > 400.0f)
		{
			Boba_Fire();
		}
		break;

	case WP_DISRUPTOR:
		if (Distance(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->enemy->SHR_currentOrigin) > 200.0f)
		{
			Boba_Fire();
		}
		break;

	case WP_BLASTER:
		Boba_Fire();
		break;
	default:;
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
// Tactics avaliable to Boba Fett:
// --------------------------------
//	BTS_RIFLE,			// Uses Jedi / Seeker Movement
//	BTS_MISSILE,		// Uses Jedi / Seeker Movement
//	BTS_SNIPER,			// Uses Special Movement Internal To This File
//	BTS_FLAMETHROW,		// Locked In Place
//	BTS_AMBUSHWAIT,		// Goto CP & Wait
//
//
// Weapons available to Boba Fett:
// --------------------------------
//	WP_NONE   (Flame Thrower)
//	WP_ROCKET_LAUNCHER
//	WP_BLASTER
//	WP_DISRUPTOR
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // SP only: the MP game has no version of this
void Boba_TacticsSelect(void)
{
	// Don't Change Tactics For A Little While
	//------------------------------------------
	TIMER_Set(NPCS.NPC, "Boba_TacticsSelect", Q_irand(8000, 15000));
	int next_state = 0;

	// Get Some Data That Will Help With The Selection Of The Next Tactic
	//--------------------------------------------------------------------
	const bool enemy_alive = NPCS.NPC->enemy->health > 0;
	const float enemy_distance = Distance(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->enemy->SHR_currentOrigin);
	const bool enemy_in_flame_range = enemy_distance < BOBA_FLAMETHROWRANGE;
	const bool enemy_in_rocket_range = enemy_distance > BOBA_ROCKETRANGEMIN && enemy_distance < BOBA_ROCKETRANGEMAX;
	const bool enemy_recently_seen = Boba_CanSeeEnemy(NPCS.NPC);

	// Enemy Is Really Close
	//-----------------------
	if (!enemy_alive)
	{
		next_state = BTS_RIFLE;
	}
	else if (enemy_in_flame_range)
	{
		// If It's Been Long Enough Since Our Last Flame Blast, Try To Torch The Enemy
		//-----------------------------------------------------------------------------
		if (TIMER_Done(NPCS.NPC, "nextFlameDelay") && (NPCS.NPC->client->NPC_class == CLASS_BOBAFETT || NPCS.NPC->client->NPC_class ==
			CLASS_MANDO))
		{
			next_state = BTS_FLAMETHROW;
		}

		// Otherwise, He's Probably Too Close, So Try To Get Clear Of Him
		//----------------------------------------------------------------
		else
		{
			next_state = BTS_RIFLE;
		}
	}

	// Recently Saw The Enemy, Time For Some Good Ole Fighten!
	//---------------------------------------------------------
	else if (enemy_recently_seen)
	{
		// At First, Boba will prefer to use his blaster against the player, but
		//  the more times he is driven away (NPC->count), he will be less likely to
		//  choose the blaster, and more likely to go for the missile launcher
		next_state = !enemy_in_rocket_range || Q_irand(0, NPCS.NPC->count) < 1 ? BTS_RIFLE : BTS_MISSILE;
	}

	// Hmmm...  Haven't Seen The Player In A While, We Might Want To Try Something Sneaky
	//-----------------------------------------------------------------------------------
	else
	{
		bool snipe_points_near = false; // TODO

		if (Q_irand(0, NPCS.NPC->count) > 0)
		{
			const int sniper_point = NPC_FindCombatPoint(NPCS.NPC->SHR_currentOrigin, NULL, NPCS.NPC->SHR_currentOrigin,
				CP_SNIPE | CP_CLEAR | CP_HAS_ROUTE | CP_TRYFAR |
				CP_HORZ_DIST_COLL, 0, -1);
			if (sniper_point != -1)
			{
				NPC_SetCombatPoint(sniper_point);
				NPC_SetMoveGoal(NPCS.NPC, level.combatPoints[sniper_point].origin, 20, qtrue, sniper_point, NULL);
				TIMER_Set(NPCS.NPC, "PickNewSniperPoint", Q_irand(15000, 25000));
				snipe_points_near = true;
			}
		}

		if (snipe_points_near && TIMER_Done(NPCS.NPC, "Boba_NoSniperTime"))
		{
			TIMER_Set(NPCS.NPC, "Boba_NoSniperTime", 120000); // Don't snipe again for a while
			TIMER_Set(NPCS.NPC, "Boba_TacticsSelect", Q_irand(35000, 45000)); // More patience here
			next_state = BTS_SNIPER;
		}
		else
		{
			next_state = !enemy_in_rocket_range || Q_irand(0, NPCS.NPC->count) < 1 ? BTS_RIFLE : BTS_MISSILE;
		}
	}

	// The Next State Has Been Selected, Now Change Weapon If Necessary
	//------------------------------------------------------------------
	if (next_state != NPCS.NPCInfo->localState)
	{
		NPCS.NPCInfo->localState = next_state;
		switch (NPCS.NPCInfo->localState)
		{
		case BTS_FLAMETHROW:
			Boba_Printf("NEW TACTIC: Flame Thrower");
			Boba_ChangeWeapon(WP_NONE);
			Boba_DoFlameThrower(NPCS.NPC);
			break;

		case BTS_RIFLE:
			Boba_Printf("NEW TACTIC: Rifle");
			Boba_ChangeWeapon(WP_BLASTER);
			break;

		case BTS_MISSILE:
			Boba_Printf("NEW TACTIC: Rocket Launcher");
			Boba_ChangeWeapon(WP_ROCKET_LAUNCHER);
			break;

		case BTS_SNIPER:
			Boba_Printf("NEW TACTIC: Sniper");
			Boba_ChangeWeapon(WP_DISRUPTOR);
			break;

		case BTS_AMBUSHWAIT:
			Boba_Printf("NEW TACTIC: Ambush");
			Boba_ChangeWeapon(WP_NONE);
			break;
		default:;
		}
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
// Tactics
//
// This function is called right after Update()
// If returns true, Jedi and Seeker AI not used for movement
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME // SP only: the MP game has no version of this
bool Boba_Tactics(void)
{
	if (!NPCS.NPC->enemy)
	{
		return false;
	}

	// Think About Changing Tactics
	//------------------------------
	if (TIMER_Done(NPCS.NPC, "Boba_TacticsSelect"))
	{
		Boba_TacticsSelect();
	}

	// These Tactics Require Seeker & Jedi Movement
	//----------------------------------------------
	if (!NPCS.NPCInfo->localState ||
		NPCS.NPCInfo->localState == BTS_RIFLE ||
		NPCS.NPCInfo->localState == BTS_MISSILE)
	{
		return false;
	}

	// Flame Thrower - Locked In Place
	//---------------------------------
	if (NPCS.NPCInfo->localState == BTS_FLAMETHROW)
	{
		Boba_DoFlameThrower(NPCS.NPC);
	}

	// Sniper - Move Around, And Take Shots
	//--------------------------------------
	else if (NPCS.NPCInfo->localState == BTS_SNIPER)
	{
		Boba_DoSniper(NPCS.NPC);
	}

	// Ambush Wait
	//------------
	else if (NPCS.NPCInfo->localState == BTS_AMBUSHWAIT)
	{
		Boba_DoAmbushWait(NPCS.NPC);
	}

	NPC_FacePosition(NPCS.NPC->enemy->SHR_currentOrigin, qtrue);
	NPC_UpdateAngles(qtrue, qtrue);

	return true; // Do Not Use Normal Jedi Or Seeker Movement
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME
bool Boba_Respawn(void)
{
	int cp = -1;

	// Try To Predict Where The Enemy Is Going
	//-----------------------------------------
	if (AverageEnemyDirectionSamples && NPCS.NPC->behaviorSet[BSET_DEATH] == NULL)
	{
		vec3_t end_pos;
		VectorMA(NPCS.NPC->enemy->SHR_currentOrigin, 1000.0f / (float)(AverageEnemyDirectionSamples),
			AverageEnemyDirection, end_pos);
		cp = NPC_FindCombatPoint(end_pos, NULL, end_pos, CP_FLEE | CP_TRYFAR | CP_HORZ_DIST_COLL, 0, -1);
		Boba_Printf("Attempting Predictive Spawn Point");
	}

	// If That Failed, Try To Go Directly To The Enemy
	//-------------------------------------------------
	if (cp == -1)
	{
		cp = NPC_FindCombatPoint(NPCS.NPC->enemy->SHR_currentOrigin, NULL, NPCS.NPC->enemy->SHR_currentOrigin,
			CP_FLEE | CP_TRYFAR | CP_HORZ_DIST_COLL, 0, -1);
		Boba_Printf("Attempting Closest Current Spawn Point");
	}

	// If We've Found One, Go There
	//------------------------------
	if (cp != -1)
	{
		NPC_SetCombatPoint(cp);
		NPCS.NPCInfo->surrenderTime = 0;
		NPCS.NPC->health = SHR_MAXHEALTH(NPCS.NPC);
		NPCS.NPC->SHR_svFlags &= ~SVF_NOCLIENT;
		NPCS.NPC->count++; // This is the number of times spawned
		G_SetOrigin(NPCS.NPC, level.combatPoints[cp].origin);

		AverageEnemyDirectionSamples = 0;
		VectorClear(AverageEnemyDirection);

		Boba_Printf("Found Spawn Point (%d)", cp);
		return true;
	}

	assert(0); // Yea, that's bad...
	Boba_Printf("FAILED TO FIND SPAWN POINT");
	return false;
}
#else // MP keeps its own version of this function
static qboolean Boba_Respawn(void)
{
	int cp = -1;

	if (!NPCS.NPC->enemy)
	{
		return qfalse;
	}

	// Try To Predict Where The Enemy Is Going
	if (averageEnemyDirectionSamples && !NPCS.NPC->behaviorSet[BSET_DEATH])
	{
		vec3_t endPos;
		VectorMA(NPCS.NPC->enemy->r.currentOrigin, 1000.0f / (float)averageEnemyDirectionSamples,
			averageEnemyDirection, endPos);
		cp = NPC_FindCombatPoint(endPos, NULL, endPos, CP_FLEE | CP_TRYFAR | CP_HORZ_DIST_COLL, 0, -1);
		Boba_Printf("Attempting Predictive Spawn Point");
	}

	// If That Failed, Try To Go Directly To The Enemy
	if (cp == -1)
	{
		cp = NPC_FindCombatPoint(NPCS.NPC->enemy->r.currentOrigin, NULL, NPCS.NPC->enemy->r.currentOrigin,
			CP_FLEE | CP_TRYFAR | CP_HORZ_DIST_COLL, 0, -1);
		Boba_Printf("Attempting Closest Current Spawn Point");
	}

	if (cp == -1)
	{
		Boba_Printf("FAILED TO FIND SPAWN POINT");
		return qfalse;
	}

	NPC_SetCombatPoint(cp);
	NPCS.NPCInfo->surrenderTime = 0;
	NPCS.NPC->health = NPCS.NPC->client->ps.stats[STAT_MAX_HEALTH];
	NPCS.NPC->client->ps.stats[STAT_HEALTH] = NPCS.NPC->health;
	NPCS.NPC->r.svFlags &= ~SVF_NOCLIENT;
	NPCS.NPC->count++; // This is the number of times spawned
	G_SetOrigin(NPCS.NPC, level.combatPoints[cp].origin);
	VectorCopy(level.combatPoints[cp].origin, NPCS.NPC->client->ps.origin);
	NPCS.NPC->client->ps.eFlags ^= EF_TELEPORT_BIT;
	trap->LinkEntity((sharedEntity_t*)NPCS.NPC);

	averageEnemyDirectionSamples = 0;
	VectorClear(averageEnemyDirection);

	Boba_Printf("Found Spawn Point (%d)", cp);
	return qtrue;
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME
void Boba_Update(void)
{
	// Never Forget The Player... Never.
	//-----------------------------------
	if (player && player->inuse && !NPCS.NPC->enemy && NPCS.NPC->client->NPC_class != CLASS_MANDO)
	{
		if (!Q_stricmp("bobafett", NPCS.NPC->targetname) || !Q_stricmp("bobafett1", NPCS.NPC->targetname))
		{
			G_SetEnemy(NPCS.NPC, player);
		}
		NPCS.NPC->SHR_svFlags |= SVF_LOCKEDENEMY; // Don't forget about the enemy once you've found him
	}

	// Hey, This Is Boba, He Tests The Trace All The Time
	//----------------------------------------------------
	if (NPCS.NPC->enemy)
	{
		if (!(NPCS.NPC->SHR_svFlags & SVF_NOCLIENT))
		{
			trace_t test_trace;
			vec3_t eyes;
			CalcEntitySpot(NPCS.NPC, SPOT_HEAD_LEAN, eyes);
			SHR_Trace(&test_trace, eyes, NULL, NULL, NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->s.number, MASK_SHOT);

			const bool was_seen = Boba_CanSeeEnemy(NPCS.NPC);

			if (!test_trace.startsolid &&
				!test_trace.allsolid &&
				test_trace.entityNum == NPCS.NPC->enemy->s.number)
			{
				NPCS.NPCInfo->enemyLastSeenTime = level.time;
				NPCS.NPCInfo->enemyLastHeardTime = level.time;
				VectorCopy(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPCInfo->enemyLastSeenLocation);
				VectorCopy(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPCInfo->enemyLastHeardLocation);
			}
			else if (NPCS.NPC->enemy && gi.inPVS(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin))
			{
				NPCS.NPCInfo->enemyLastHeardTime = level.time;
				VectorCopy(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPCInfo->enemyLastHeardLocation);
			}

			if (SHR_CVAR(g_bobaDebug).integer)
			{
				const bool now_seen = Boba_CanSeeEnemy(NPCS.NPC);
				if (!was_seen && now_seen)
				{
					Boba_Printf("Enemy Seen");
				}
				if (was_seen && !now_seen)
				{
					Boba_Printf("Enemy Lost");
				}
				CG_DrawEdge(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->enemy->SHR_currentOrigin,
					now_seen ? EDGE_IMPACT_SAFE : EDGE_IMPACT_POSSIBLE);
			}
		}

		if (!NPCS.NPCInfo->surrenderTime && NPCS.NPC->client->NPC_class != CLASS_MANDO)
		{
			if (level.time - NPCS.NPCInfo->enemyLastSeenTime > 20000 && TIMER_Done(NPCS.NPC, "TooLongGoneRespawn"))
			{
				TIMER_Set(NPCS.NPC, "TooLongGoneRespawn", 30000); // Give him some time to get to you before trying again
				Boba_Printf("Gone Too Long, Attempting Respawn Even Though Not Hiding");
				Boba_Respawn();
			}
		}
	}

	// Make Sure He Always Appears In The Last Area With Full Health When His Death Script Is Turned On
	//--------------------------------------------------------------------------------------------------
	if (!BobaHadDeathScript && NPCS.NPC->behaviorSet[BSET_DEATH] != NULL && NPCS.NPC->client->NPC_class != CLASS_MANDO)
	{
		if (!NPCS.NPC->enemy || gi.inPVS(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin))
		{
			Boba_Printf("Attempting Final Battle Spawn...");
			if (Boba_Respawn())
			{
				BobaHadDeathScript = qtrue;
			}
			else
			{
				Boba_Printf("Failed");
			}
		}
	}

	// Don't Forget To Turn Off That Flame Thrower, Mr. Fett - You're Waisting Precious Natural Gases
	//------------------------------------------------------------------------------------------------
	if (NPCS.NPCInfo->aiFlags & NPCAI_FLAMETHROW && TIMER_Done(NPCS.NPC, "flameTime"))
	{
		Boba_StopFlameThrower(NPCS.NPC);
	}

	// Occasionally A Jump Turns Into A Rocket Fly
	//---------------------------------------------
	if (NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE
		&& NPCS.NPC->client->ps.forceJumpZStart
		&& !Q_irand(0, 10))
	{
		//take off
		Boba_FlyStart(NPCS.NPC);
	}

	// If Hurting, Try To Run Away
	//-----------------------------
	if (!NPCS.NPCInfo->surrenderTime && NPCS.NPC->health < SHR_MAXHEALTH(NPCS.NPC) / 10)
	{
		Boba_Printf("Time To Surrender, Searching For Flee Point");

		// Find The Closest Flee Point That I Can Get To
		//-----------------------------------------------
		const int cp = NPC_FindCombatPoint(NPCS.NPC->SHR_currentOrigin, NULL, NPCS.NPC->SHR_currentOrigin,
			CP_FLEE | CP_HAS_ROUTE | CP_TRYFAR | CP_HORZ_DIST_COLL, 0, -1);
		if (cp != -1)
		{
			NPC_SetCombatPoint(cp);
			NPC_SetMoveGoal(NPCS.NPC, level.combatPoints[cp].origin, 8, qtrue, cp, NULL);
			if (NPCS.NPC->count < 6)
			{
				NPCS.NPCInfo->surrenderTime = level.time + Q_irand(5000, 10000) + 1000 * (6 - NPCS.NPC->count);
			}
			else
			{
				NPCS.NPCInfo->surrenderTime = level.time + Q_irand(5000, 10000);
			}
		}
		else
		{
			Boba_Printf("  Failure");
		}
	}
}
#else // MP keeps its own version of this function
void Boba_Update(void)
{
	Boba_LevelCheck();

	// Never Forget The Player... Never. (SP: the scripted "bobafett"; in MP the closest player)
	if (!NPCS.NPC->enemy && NPCS.NPC->client->NPC_class == CLASS_BOBAFETT && NPCS.NPC->targetname
		&& (!Q_stricmp("bobafett", NPCS.NPC->targetname) || !Q_stricmp("bobafett1", NPCS.NPC->targetname)))
	{
		gentity_t* closest = FindClosestPlayer(NPCS.NPC->r.currentOrigin, NPCS.NPC->client->enemyTeam);
		if (closest)
		{
			G_SetEnemy(NPCS.NPC, closest);
			NPCS.NPCInfo->aiFlags2 |= NPCAI2_LOCKEDENEMY; // Don't forget about the enemy once you've found him
		}
	}

	// Hey, This Is Boba, He Tests The Trace All The Time
	if (NPCS.NPC->enemy)
	{
		if (!(NPCS.NPC->r.svFlags & SVF_NOCLIENT))
		{
			trace_t testTrace;
			vec3_t eyes;
			CalcEntitySpot(NPCS.NPC, SPOT_HEAD_LEAN, eyes);
			trap->Trace(&testTrace, eyes, NULL, NULL, NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->s.number, MASK_SHOT,
				qfalse, 0, 0);

			if (!testTrace.startsolid && !testTrace.allsolid && testTrace.entityNum == NPCS.NPC->enemy->s.number)
			{
				NPCS.NPCInfo->enemyLastSeenTime = level.time;
				NPCS.NPCInfo->enemyLastHeardTime = level.time;
				VectorCopy(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPCInfo->enemyLastSeenLocation);
				VectorCopy(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPCInfo->enemyLastHeardLocation);
			}
			else if (trap->InPVS(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin))
			{
				NPCS.NPCInfo->enemyLastHeardTime = level.time;
				VectorCopy(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPCInfo->enemyLastHeardLocation);
			}
		}

		if (!NPCS.NPCInfo->surrenderTime)
		{
			if (level.time - NPCS.NPCInfo->enemyLastSeenTime > 20000 && TIMER_Done(NPCS.NPC, "TooLongGoneRespawn"))
			{
				TIMER_Set(NPCS.NPC, "TooLongGoneRespawn", 30000); // Give him some time to get to you before trying again
				Boba_Printf("Gone Too Long, Attempting Respawn Even Though Not Hiding");
				Boba_Respawn();
			}
		}

		// Make Sure He Always Appears In The Last Area With Full Health When His Death Script Is Turned On
		if (!bobaHadDeathScript && NPCS.NPC->behaviorSet[BSET_DEATH] && NPCS.NPC->enemy
			&& !trap->InPVS(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin))
		{
			Boba_Printf("Attempting Final Battle Spawn...");
			if (Boba_Respawn())
			{
				bobaHadDeathScript = qtrue;
			}
		}
	}

	// Occasionally A Jump Turns Into A Rocket Fly
	if (NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE
		&& NPCS.NPC->client->ps.fd.forceJumpZStart
		&& !Q_irand(0, 10))
	{
		Boba_FlyStart(NPCS.NPC);
	}

	// If Hurting, Try To Run Away
	if (!NPCS.NPCInfo->surrenderTime && NPCS.NPC->health > 0
		&& NPCS.NPC->health < NPCS.NPC->client->ps.stats[STAT_MAX_HEALTH] / 10)
	{
		Boba_Printf("Time To Surrender, Searching For Flee Point");

		// Find The Closest Flee Point That I Can Get To
		const int cp = NPC_FindCombatPoint(NPCS.NPC->r.currentOrigin, NULL, NPCS.NPC->r.currentOrigin,
			CP_FLEE | CP_HAS_ROUTE | CP_TRYFAR | CP_HORZ_DIST_COLL, 0, -1);
		if (cp != -1)
		{
			NPC_SetCombatPoint(cp);
			NPC_SetMoveGoal(NPCS.NPC, level.combatPoints[cp].origin, 8, qtrue, cp, NULL);
			if (NPCS.NPC->count < 6)
			{
				NPCS.NPCInfo->surrenderTime = level.time + Q_irand(5000, 10000) + 1000 * (6 - NPCS.NPC->count);
			}
			else
			{
				NPCS.NPCInfo->surrenderTime = level.time + Q_irand(5000, 10000);
			}
		}
		else
		{
			Boba_Printf("  Failure");
		}
	}
}
#endif

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
#ifdef SP_GAME
bool Boba_Flee(void)
{
	const bool enemy_recently_seen = level.time - NPCS.NPCInfo->enemyLastSeenTime < 10000;
	// combatPoint is -1 when no escape point was found (e.g. maps without combat points)
	const bool has_escape_point = NPCS.NPCInfo->combatPoint >= 0 && NPCS.NPCInfo->combatPoint < level.numCombatPoints;
	const bool reached_escape_point = has_escape_point &&
		Distance(level.combatPoints[NPCS.NPCInfo->combatPoint].origin, NPCS.NPC->SHR_currentOrigin) < 50.0f;
	const bool has_been_gone_enough = level.time > NPCS.NPCInfo->surrenderTime || level.time - NPCS.NPCInfo->enemyLastSeenTime >
		400000;

	// Is It Time To Come Back For Some More?
	//----------------------------------------
	if (!enemy_recently_seen || reached_escape_point && NPCS.NPC->client->NPC_class != CLASS_MANDO)
	{
		NPCS.NPC->SHR_svFlags |= SVF_NOCLIENT;
		if (has_been_gone_enough)
		{
			if (level.time - NPCS.NPCInfo->enemyLastSeenTime > 400000)
			{
				Boba_Printf("  Gone Too Long, Attempting Respawn");
			}

			if (Boba_Respawn())
			{
				return true;
			}
		}
		else if (reached_escape_point && NPCS.NPCInfo->surrenderTime - level.time > 3000)
		{
			if (TIMER_Done(NPCS.NPC, "SpookPlayerTimer"))
			{
				vec3_t test_direction{};
				TIMER_Set(NPCS.NPC, "SpookPlayerTimer", Q_irand(2000, 10000));
				switch (Q_irand(0, 1))
				{
				case 0:
					Boba_Printf("SPOOK: Dust");
					Boba_DustFallNear(NPCS.NPC->enemy->SHR_currentOrigin, Q_irand(1, 2));
					break;

				case 1:
					Boba_Printf("SPOOK: Footsteps");
					test_direction[0] = Q_flrand(0.0f, 1.0f) * 0.5f - 1.0f;
					test_direction[0] += test_direction[0] > 0.0f ? 0.5f : -0.5f;
					test_direction[1] = Q_flrand(0.0f, 1.0f) * 0.5f - 1.0f;
					test_direction[1] += test_direction[1] > 0.0f ? 0.5f : -0.5f;
					test_direction[2] = 1.0f;
					VectorMA(NPCS.NPC->enemy->SHR_currentOrigin, 400.0f, test_direction, BobaFootStepLoc);

					BobaFootStepCount = Q_irand(3, 8);
					break;
				default:;
				}
			}

			if (BobaFootStepCount && TIMER_Done(NPCS.NPC, "BobaFootStepFakeTimer"))
			{
				TIMER_Set(NPCS.NPC, "BobaFootStepFakeTimer", Q_irand(300, 800));
				BobaFootStepCount--;
				G_SoundAtSpot(BobaFootStepLoc, G_SoundIndex(va("sound/player/footsteps/boot%d", Q_irand(1, 4))), qtrue);
			}

			if (TIMER_Done(NPCS.NPC, "ResampleEnemyDirection") && NPCS.NPC->enemy->resultspeed > 10.0f)
			{
				TIMER_Set(NPCS.NPC, "ResampleEnemyDirection", Q_irand(500, 1000));
				AverageEnemyDirectionSamples++;

				vec3_t moveDir;
				VectorCopy(NPCS.NPC->enemy->client->ps.velocity, moveDir);
				VectorNormalize(moveDir);

				VectorAdd(AverageEnemyDirection, moveDir, AverageEnemyDirection);
			}

			if (SHR_CVAR(g_bobaDebug).integer && AverageEnemyDirectionSamples)
			{
				vec3_t end_pos;
				VectorMA(NPCS.NPC->enemy->SHR_currentOrigin, 500.0f / (float)(AverageEnemyDirectionSamples),
					AverageEnemyDirection, end_pos);
				CG_DrawEdge(NPCS.NPC->enemy->SHR_currentOrigin, end_pos, EDGE_IMPACT_POSSIBLE);
			}
		}
	}
	else
	{
		NPCS.NPCInfo->surrenderTime += 100;
	}

	// Finish The Flame Thrower First...
	//-----------------------------------
	if (NPCS.NPCInfo->aiFlags & NPCAI_FLAMETHROW)
	{
		Boba_DoFlameThrower(NPCS.NPC);
		NPC_FacePosition(NPCS.NPC->enemy->SHR_currentOrigin, qtrue);
		NPC_UpdateAngles(qtrue, qtrue);
		return true;
	}

	const bool is_on_a_path = !!NPC_MoveToGoal(qtrue);
	if (!reached_escape_point &&
		NPCS.NPCInfo->aiFlags & NPCAI_BLOCKED &&
		NPCS.NPC->client->moveType != MT_FLYSWIM &&
		level.time - NPCS.NPCInfo->blockedDebounceTime > 1000
		)
	{
		if (has_escape_point && !Boba_CanSeeEnemy(NPCS.NPC) &&
			Distance(NPCS.NPC->SHR_currentOrigin, level.combatPoints[NPCS.NPCInfo->combatPoint].origin) < 200)
		{
			Boba_Printf("BLOCKED: Just Teleporting There");
			G_SetOrigin(NPCS.NPC, level.combatPoints[NPCS.NPCInfo->combatPoint].origin);
		}
		else
		{
			Boba_Printf("BLOCKED: Attempting Jump");

			if (is_on_a_path)
			{
				if (NPC_TryJump(NPCS.NPCInfo->blockedTargetPosition))
				{
				}
				else
				{
					Boba_Printf("  Failed");
				}
			}
			else if (enemy_recently_seen)
			{
				if (NPC_TryJump(NPCS.NPCInfo->enemyLastSeenLocation))
				{
				}
				else
				{
					Boba_Printf("  Failed");
				}
			}
		}
	}

	NPC_UpdateAngles(qtrue, qtrue);
	return true;
}
#else // MP keeps its own version of this function
qboolean Boba_Flee(void)
{
	const qboolean enemyRecentlySeen = level.time - NPCS.NPCInfo->enemyLastSeenTime < 10000 ? qtrue : qfalse;
	// combatPoint is -1 when no escape point was found (e.g. maps without combat points)
	const qboolean hasEscapePoint = NPCS.NPCInfo->combatPoint >= 0
		&& NPCS.NPCInfo->combatPoint < level.numCombatPoints ? qtrue : qfalse;
	const qboolean reachedEscapePoint = hasEscapePoint
		&& Distance(level.combatPoints[NPCS.NPCInfo->combatPoint].origin, NPCS.NPC->r.currentOrigin) < 50.0f
		? qtrue : qfalse;
	const qboolean hasBeenGoneEnough = level.time > NPCS.NPCInfo->surrenderTime
		|| level.time - NPCS.NPCInfo->enemyLastSeenTime > 400000 ? qtrue : qfalse;

	if (!NPCS.NPC->enemy)
	{
		// (nobody left to come back for)
		NPCS.NPCInfo->surrenderTime = 0;
		NPCS.NPC->r.svFlags &= ~SVF_NOCLIENT;
		return qfalse;
	}

	// Is It Time To Come Back For Some More?
	if (!enemyRecentlySeen || reachedEscapePoint)
	{
		NPCS.NPC->r.svFlags |= SVF_NOCLIENT;
		if (hasBeenGoneEnough)
		{
			if (level.time - NPCS.NPCInfo->enemyLastSeenTime > 400000)
			{
				Boba_Printf("  Gone Too Long, Attempting Respawn");
			}

			if (Boba_Respawn())
			{
				return qtrue;
			}
		}
		else if (reachedEscapePoint && NPCS.NPCInfo->surrenderTime - level.time > 3000)
		{
			if (TIMER_Done(NPCS.NPC, "SpookPlayerTimer"))
			{
				vec3_t testDirection;
				TIMER_Set(NPCS.NPC, "SpookPlayerTimer", Q_irand(2000, 10000));
				if (!Q_irand(0, 1))
				{
					Boba_Printf("SPOOK: Dust");
					Boba_DustFallNear(NPCS.NPC->enemy->r.currentOrigin, Q_irand(1, 2));
				}
				else
				{
					Boba_Printf("SPOOK: Footsteps");
					testDirection[0] = Q_flrand(0.0f, 1.0f) * 0.5f - 1.0f;
					testDirection[0] += testDirection[0] > 0.0f ? 0.5f : -0.5f;
					testDirection[1] = Q_flrand(0.0f, 1.0f) * 0.5f - 1.0f;
					testDirection[1] += testDirection[1] > 0.0f ? 0.5f : -0.5f;
					testDirection[2] = 1.0f;
					VectorMA(NPCS.NPC->enemy->r.currentOrigin, 400.0f, testDirection, bobaFootStepLoc);

					bobaFootStepCount = Q_irand(3, 8);
				}
			}

			if (bobaFootStepCount && TIMER_Done(NPCS.NPC, "BobaFootStepFakeTimer"))
			{
				TIMER_Set(NPCS.NPC, "BobaFootStepFakeTimer", Q_irand(300, 800));
				bobaFootStepCount--;
				G_SoundAtLoc(bobaFootStepLoc, CHAN_BODY, G_SoundIndex(va("sound/player/footsteps/boot%d", Q_irand(1, 4))));
			}

			if (TIMER_Done(NPCS.NPC, "ResampleEnemyDirection") && NPCS.NPC->enemy->client
				&& VectorLength(NPCS.NPC->enemy->client->ps.velocity) > 10.0f)
			{
				vec3_t moveDir;
				TIMER_Set(NPCS.NPC, "ResampleEnemyDirection", Q_irand(500, 1000));
				averageEnemyDirectionSamples++;

				VectorCopy(NPCS.NPC->enemy->client->ps.velocity, moveDir);
				VectorNormalize(moveDir);
				VectorAdd(averageEnemyDirection, moveDir, averageEnemyDirection);
			}
		}
	}
	else
	{
		NPCS.NPCInfo->surrenderTime += 100;
	}

	// Finish The Flame Thrower First...
	if (!TIMER_Done(NPCS.NPC, "flameTime"))
	{
		NPC_FacePosition(NPCS.NPC->enemy->r.currentOrigin, qtrue);
		NPC_UpdateAngles(qtrue, qtrue);
		return qtrue;
	}

	const qboolean isOnAPath = NPC_MoveToGoal(qtrue);
	if (!reachedEscapePoint
		&& NPCS.NPCInfo->aiFlags & NPCAI_BLOCKED
		&& NPCS.NPC->client->NPC_class != CLASS_VEHICLE
		&& level.time - NPCS.NPCInfo->blockedDebounceTime > 1000)
	{
		if (hasEscapePoint && !Boba_CanSeeEnemy(NPCS.NPC)
			&& Distance(NPCS.NPC->r.currentOrigin, level.combatPoints[NPCS.NPCInfo->combatPoint].origin) < 200)
		{
			Boba_Printf("BLOCKED: Just Teleporting There");
			G_SetOrigin(NPCS.NPC, level.combatPoints[NPCS.NPCInfo->combatPoint].origin);
			VectorCopy(level.combatPoints[NPCS.NPCInfo->combatPoint].origin, NPCS.NPC->client->ps.origin);
			trap->LinkEntity((sharedEntity_t*)NPCS.NPC);
		}
		else
		{
			Boba_Printf("BLOCKED: Attempting Jump");
			if (isOnAPath)
			{
				Boba_TryJumpTo(NPCS.NPCInfo->blockedDest);
			}
			else if (enemyRecentlySeen)
			{
				Boba_TryJumpTo(NPCS.NPCInfo->enemyLastSeenLocation);
			}
		}
	}

	NPC_UpdateAngles(qtrue, qtrue);
	return qtrue;
}
#endif