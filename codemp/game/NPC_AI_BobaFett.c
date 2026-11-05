// leave this line at the top of all AI_xxxx.cpp files for PCH reasons...
#include "b_local.h"

/*
-------------------------
Bounty hunters: SP's AI_BobaFett.cpp update / flee / respawn (DH 2026-11-03, NPC parity batch 3)

Boba Fett runs this (MD 9: also Jango and dual-pistol Jango) as in SP's NPC_RunBehavior: Boba_Update first every frame (he keeps
track of his enemy even out of sight, a jump may turn into a rocket fly, and when he is badly hurt he runs for a flee
point). While fleeing (surrenderTime) Boba_Flee hides him (SVF_NOCLIENT) once he is out of sight or at the flee point,
spooks his enemy with falling dust and footsteps, then brings him back at full health at a combat point near where the
enemy is heading. Otherwise Boba_TacticsLocked: MP's Boba_Tactics (NPC_AI_Jedi.c: flame thrower, sniper, ambush), and
the Jedi / Seeker AI only when those tactics need their movement. Maps without combat points: no flee point, no
respawn, he just fights on (as SP). The wrist weapons and flame thrower of a player bounty hunter stay MP's.
-------------------------
*/

extern void Boba_Tactics(void);
extern void Boba_FlyStart(gentity_t* self);
extern void G_SoundAtLoc(vec3_t loc, int channel, int soundIndex);

// as NPC_AI_Jedi.c's bounty hunter tactics states (must match)
enum
{
	BOBA_TS_NONE,
	BOBA_TS_RIFLE,
	BOBA_TS_MISSILE,
	BOBA_TS_SNIPER,
	BOBA_TS_FLAMETHROW,
	BOBA_TS_AMBUSHWAIT
};

static qboolean bobaHadDeathScript;
static vec3_t bobaFootStepLoc;
static int bobaFootStepCount;
static vec3_t averageEnemyDirection;
static int averageEnemyDirectionSamples;
static int bobaLevel = -1;

static void Boba_Printf(const char* format, ...)
{
	va_list argptr;
	char text[1024];

	if (trap->Cvar_VariableIntegerValue("developer") < 2)
	{
		return;
	}
	va_start(argptr, format);
	Q_vsnprintf(text, sizeof text, format, argptr);
	va_end(argptr);
	Com_Printf("[BOBA %8d] %s\n", level.time, text);
}

// the bounty hunter globals belong to this map start (SP resets them in Boba_Precache)
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

qboolean Boba_Class(const gentity_t* self)
{
	return self && self->client && self->client->NPC_class == CLASS_BOBAFETT ? qtrue : qfalse; // (SJE: Jango has no bounty hunter AI in MP)
}

static qboolean Boba_CanSeeEnemy(const gentity_t* self)
{
	return level.time - self->NPC->enemyLastSeenTime < 1000 ? qtrue : qfalse;
}

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

// Called From NPC_Pain(): hurt by a saber, time to try something new
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

// a jump to a spot (as NPC_move.c's NPC_TryJump3, which is local there)
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

// Always Called First, Before Any Other Thinking
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

// If returns true, Jedi and Seeker AI not used
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

// SP's Boba_Tactics: MP's (NPC_AI_Jedi.c), and qtrue when the tactic needs no Jedi / Seeker movement
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