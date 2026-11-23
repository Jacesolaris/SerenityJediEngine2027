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
void Rancor_SetBolts(const gentity_t* self)
{
	if (self && self->client)
	{
		renderInfo_t* ri = &self->client->renderInfo;

		ri->handRBolt = trap->G2API_AddBolt(self->ghoul2, 0, "*r_hand");
		ri->handLBolt = trap->G2API_AddBolt(self->ghoul2, 0, "*l_hand");
		ri->headBolt = trap->G2API_AddBolt(self->ghoul2, 0, "*head_eyes");
		ri->torsoBolt = trap->G2API_AddBolt(self->ghoul2, 0, "jaw_bone");
	}
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

#endif

#ifndef SP_GAME // file-level declarations of the MP versions below
#endif

#ifndef SP_GAME // MP-only helpers used by the MP versions below

#endif

#ifndef SP_GAME // file-level declarations of the MP versions below
extern void G_Dismember(const gentity_t* ent, const gentity_t* enemy, vec3_t point, int limb_type);
extern void G_GetBoltPosition(gentity_t* self, int boltIndex, vec3_t pos, int modelIndex);
#endif

#ifndef SP_GAME // MP-only helpers used by the MP versions below
static void Rancor_Crush(void)
{
	if (!NPCS.NPC ||
		!NPCS.NPC->client ||
		NPCS.NPC->client->ps.groundEntityNum >= ENTITYNUM_WORLD)
	{
		//nothing to crush
		return;
	}

	gentity_t* crush = &g_entities[NPCS.NPC->client->ps.groundEntityNum];
	if (crush->inuse && crush->client && BG_AnimSetIsHumanoid(crush->localAnimIndex))
	{
		//a humanoid, smash them good.
		G_Damage(crush, NPCS.NPC, NPCS.NPC, NULL, NPCS.NPC->r.currentOrigin, 200, 0, MOD_CRUSH);
	}
}

static void Rancor_HitBreakables(vec3_t boltOrg, const int damage, const int* entNums, const int numEnts)
{
	trace_t trace;
	vec3_t from;

	if (VectorCompare(NPCS.NPC->pos3, vec3_origin))
	{
		VectorCopy(NPCS.NPC->r.currentOrigin, from);
	}
	else
	{
		VectorCopy(NPCS.NPC->pos3, from);
	}
	trap->Trace(&trace, from, vec3_origin, vec3_origin, boltOrg, NPCS.NPC->s.number, CONTENTS_SOLID | CONTENTS_BODY,
		qfalse, 0, 0);
	VectorCopy(boltOrg, NPCS.NPC->pos3);
	if (trace.entityNum < ENTITYNUM_WORLD && SHR_EntIsBreakable(trace.entityNum, NPCS.NPC))
	{
		G_Damage(&g_entities[trace.entityNum], NPCS.NPC, NPCS.NPC, vec3_origin, boltOrg, damage, 0, MOD_MELEE);
	}
	else
	{
		trap->Trace(&trace, NPCS.NPC->r.currentOrigin, vec3_origin, vec3_origin, boltOrg, NPCS.NPC->s.number,
			CONTENTS_SOLID | CONTENTS_BODY, qfalse, 0, 0);
		if (trace.entityNum < ENTITYNUM_WORLD && SHR_EntIsBreakable(trace.entityNum, NPCS.NPC))
		{
			G_Damage(&g_entities[trace.entityNum], NPCS.NPC, NPCS.NPC, vec3_origin, boltOrg, 200, 0, MOD_MELEE);
		}
	}
	for (int i = 0; i < numEnts; i++)
	{
		gentity_t* ent = &g_entities[entNums[i]];
		if (ent->inuse && !ent->client && SHR_EntIsBreakable(ent->s.number, NPCS.NPC) && !Q_irand(0, 1))
		{
			//damage breakables within range, but not as much
			G_Damage(ent, NPCS.NPC, NPCS.NPC, vec3_origin, ent->r.currentOrigin, 100, 0, MOD_MELEE);
		}
	}
}

static void Rancor_MoveStraightAt(vec3_t dest)
{
	NPC_FacePosition(dest, qfalse);
	NPCS.ucmd.buttons &= ~BUTTON_WALKING;
	NPCS.ucmd.forwardmove = 127;
	NPCS.ucmd.rightmove = 0;
}
#endif

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

// These define the working combat range for these suckers
#define MIN_DISTANCE 128
#define MIN_DISTANCE_SQR	( MIN_DISTANCE * MIN_DISTANCE )

#define MAX_DISTANCE 1024
#define MAX_DISTANCE_SQR	( MAX_DISTANCE * MAX_DISTANCE )

#define LSTATE_CLEAR 0
#define LSTATE_WAITING 1

#define SPF_RANCOR_MUTANT 1
#define SPF_RANCOR_FASTKILL 2

#ifdef SP_GAME // the MP game's G_EntIsBreakable has other parameters
extern qboolean G_EntIsBreakable(int entityNum, const gentity_t* breaker);
#endif
extern cvar_t* g_dismemberment;
extern cvar_t* g_bobaDebug;

void Rancor_Attack(float distance, qboolean do_charge, qboolean aim_at_blocked_entity);
extern void G_Knockdown(gentity_t* self, gentity_t* attacker, const vec3_t push_dir, float strength, const qboolean breakSaberLock);
extern qboolean G_DoDismemberment(gentity_t* self, vec3_t point, int mod, int hit_loc, qboolean force);
extern float NPC_EntRangeFromBolt(const gentity_t* targ_ent, int boltIndex);
#ifdef SP_GAME // MP declares its own (entity numbers)
extern int NPC_GetEntsNearBolt(gentity_t** radius_ents, float radius, int boltIndex, vec3_t bolt_org);
#endif
/*
-------------------------
NPC_Rancor_Precache
-------------------------
*/
void NPC_Rancor_Precache(void)
{
	for (int i = 1; i < 5; i++)
	{
		G_SoundIndex(va("sound/chars/rancor/snort_%d.wav", i));
	}
	G_SoundIndex("sound/chars/rancor/swipehit.wav");
	G_SoundIndex("sound/chars/rancor/chomp.wav");
}

void NPC_MutantRancor_Precache(void)
{
	G_SoundIndex("sound/chars/rancor/breath_start.wav");
	G_SoundIndex("sound/chars/rancor/breath_loop.wav");
	G_EffectIndex("mrancor/breath");
}

#ifdef SP_GAME
static qboolean Rancor_CheckAhead(vec3_t end)
{
	trace_t trace;
	int clipmask = NPCS.NPC->clipmask | CONTENTS_BOTCLIP;

	//make sure our goal isn't underground (else the trace will fail)
	const vec3_t bottom = { end[0], end[1], end[2] + NPCS.NPC->SHR_mins[2] };
	SHR_Trace(&trace, end, vec3_origin, vec3_origin, bottom, NPCS.NPC->s.number, NPCS.NPC->clipmask);
	if (trace.fraction < 1.0f)
	{
		//in the ground, raise it up
		end[2] -= NPCS.NPC->SHR_mins[2] * (1.0f - trace.fraction) - 0.125f;
	}

	SHR_Trace(&trace, NPCS.NPC->SHR_currentOrigin, NPCS.NPC->SHR_mins, NPCS.NPC->SHR_maxs, end, NPCS.NPC->s.number, clipmask);

	if (trace.startsolid && trace.SHR_contents & CONTENTS_BOTCLIP)
	{
		//started inside do not enter, so ignore them
		clipmask &= ~CONTENTS_BOTCLIP;
		SHR_Trace(&trace, NPCS.NPC->SHR_currentOrigin, NPCS.NPC->SHR_mins, NPCS.NPC->SHR_maxs, end, NPCS.NPC->s.number, clipmask);
	}
	//Do a simple check
	if (trace.allsolid == qfalse && trace.startsolid == qfalse && trace.fraction == 1.0f)
		return qtrue;

	if (trace.entityNum < ENTITYNUM_WORLD
		&& SHR_EntIsBreakable(trace.entityNum, NPCS.NPC))
	{
		//breakable brush in our way, break it
		//	NPCInfo->blockedEntity = &g_entities[trace.entityNum];
		return qtrue;
	}

	//Aw screw it, always try to go straight at him if we can at all
	if (trace.fraction >= 0.25f)
		return qtrue;

	//FIXME: if something in the way that's not the world, set blocked ent
	return qfalse;
}
#else // MP keeps its own version of this function
static qboolean Rancor_CheckAhead(vec3_t end)
{
	trace_t trace;
	vec3_t bottom;
	int clipmask = NPCS.NPC->clipmask | CONTENTS_BOTCLIP;

	//make sure our goal isn't underground (else the trace will fail)
	VectorSet(bottom, end[0], end[1], end[2] + NPCS.NPC->r.mins[2]);
	trap->Trace(&trace, end, vec3_origin, vec3_origin, bottom, NPCS.NPC->s.number, NPCS.NPC->clipmask, qfalse, 0, 0);
	if (trace.fraction < 1.0f)
	{
		//in the ground, raise it up
		end[2] -= NPCS.NPC->r.mins[2] * (1.0f - trace.fraction) - 0.125f;
	}

	trap->Trace(&trace, NPCS.NPC->r.currentOrigin, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, end, NPCS.NPC->s.number, clipmask,
		qfalse, 0, 0);

	if (trace.startsolid && trace.contents & CONTENTS_BOTCLIP)
	{
		//started inside do not enter, so ignore them
		clipmask &= ~CONTENTS_BOTCLIP;
		trap->Trace(&trace, NPCS.NPC->r.currentOrigin, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, end, NPCS.NPC->s.number,
			clipmask, qfalse, 0, 0);
	}
	//Do a simple check
	if (trace.allsolid == qfalse && trace.startsolid == qfalse && trace.fraction == 1.0f)
	{
		return qtrue;
	}
	if (trace.entityNum < ENTITYNUM_WORLD && SHR_EntIsBreakable(trace.entityNum, NPCS.NPC))
	{
		//breakable brush in our way, break it
		return qtrue;
	}
	//Aw screw it, always try to go straight at him if we can at all
	return trace.fraction >= 0.25f ? qtrue : qfalse;
}
#endif

/*
-------------------------
Rancor_Idle
-------------------------
*/
static void Rancor_Idle(void)
{
	NPCS.NPCInfo->localState = LSTATE_CLEAR;

	//If we have somewhere to go, then do that
	if (UpdateGoal())
	{
		NPCS.ucmd.buttons &= ~BUTTON_WALKING;
		NPC_MoveToGoal(qtrue);
	}
}

static qboolean Rancor_CheckRoar(gentity_t* self)
{
	if (!self->wait)
	{
		//haven't ever gotten mad yet
		self->wait = 1; //do this only once
		NPC_SetAnim(self, SETANIM_BOTH, BOTH_STAND1TO2, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		TIMER_Set(self, "rageTime", self->client->ps.SHR_legsAnimTimer);
		return qtrue;
	}
	return qfalse;
}

/*
-------------------------
Rancor_Patrol
-------------------------
*/
static void Rancor_Patrol(void)
{
	NPCS.NPCInfo->localState = LSTATE_CLEAR;

	//If we have somewhere to go, then do that
	if (UpdateGoal())
	{
		NPCS.ucmd.buttons &= ~BUTTON_WALKING;
		NPC_MoveToGoal(qtrue);
	}

	if (NPC_CheckEnemyExt(qtrue) == qfalse)
	{
		Rancor_Idle();
		return;
	}
	Rancor_CheckRoar(NPCS.NPC);
	TIMER_Set(NPCS.NPC, "lookForNewEnemy", Q_irand(5000, 15000));
}

/*
-------------------------
Rancor_Move
-------------------------
*/
#ifdef SP_GAME
void Rancor_Move(void)
{
	if (NPCS.NPCInfo->localState != LSTATE_WAITING)
	{
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
		NPCS.NPCInfo->goalRadius = NPCS.NPC->SHR_maxs[0] + MIN_DISTANCE * NPCS.NPC->s.modelScale[0]; // just get us within combat range
		const float sav_yaw = NPCS.NPCInfo->desiredYaw;
		const bool sav_walking = !!(NPCS.ucmd.buttons & BUTTON_WALKING);
		if (!NPC_MoveToGoal(qtrue))
		{
			//can't macro-nav, just head right for him
			vec3_t dest;
			VectorCopy(NPCS.NPCInfo->goalEntity->SHR_currentOrigin, dest);
			if (Rancor_CheckAhead(dest))
			{
				//use our temp move straight to goal check
				if (!sav_walking)
				{
					NPCS.ucmd.buttons &= ~BUTTON_WALKING; // Unset from MoveToGoal()
				}
				STEER::Activate(NPCS.NPC);
				STEER::Seek(NPCS.NPC, dest);
				STEER::AvoidCollisions(NPCS.NPC, NULL);
				STEER::DeActivate(NPCS.NPC, &NPCS.ucmd);
			}
			else
			{
				//all else fails, look at him
				NPCS.NPCInfo->lockedDesiredYaw = NPCS.NPCInfo->desiredYaw = sav_yaw;

				if (!NPCS.NPCInfo->blockedEntity && NPCS.NPC->enemy && gi.inPVS(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->enemy->SHR_currentOrigin))
				{
					//nothing to destroy?  just go straight at goal dest
					qboolean horz_close = qfalse;
					if (!sav_walking)
					{
						NPCS.ucmd.buttons &= ~BUTTON_WALKING; // Unset from MoveToGoal()
					}

					if (DistanceHorizontal(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin) < NPCS.NPC->SHR_maxs[0] + MIN_DISTANCE
						* NPCS.NPC->s.modelScale[0])
					{
						//close, just look at him
						horz_close = qtrue;
						NPC_FaceEnemy(qtrue);
					}
					else
					{
						//try to move  towards him
						STEER::Activate(NPCS.NPC);
						STEER::Seek(NPCS.NPC, dest);
						STEER::AvoidCollisions(NPCS.NPC, NULL);
						STEER::DeActivate(NPCS.NPC, &NPCS.ucmd);
					}
					//let him know he should attack at random out of frustration?
					if (NPCS.NPCInfo->goalEntity == NPCS.NPC->enemy)
					{
						if (TIMER_Done(NPCS.NPC, "attacking")
							&& TIMER_Done(NPCS.NPC, "frustrationAttack"))
						{
							const float enemy_dist = Distance(dest, NPCS.NPC->SHR_currentOrigin);
							if ((!horz_close || !Q_irand(0, 5))
								&& Q_irand(0, 1))
							{
								Rancor_Attack(enemy_dist, qtrue, qfalse);
							}
							else
							{
								Rancor_Attack(enemy_dist, qfalse, qfalse);
							}
							if (horz_close)
							{
								TIMER_Set(NPCS.NPC, "frustrationAttack", Q_irand(2000, 5000));
							}
							else
							{
								TIMER_Set(NPCS.NPC, "frustrationAttack", Q_irand(5000, 15000));
							}
						}
					}
				}
			}
		}
	}
}
#else // MP keeps its own version of this function
void Rancor_Move()
{
	if (NPCS.NPCInfo->localState != LSTATE_WAITING)
	{
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
		NPCS.NPCInfo->goalRadius = NPCS.NPC->maxs[0] + MIN_DISTANCE; // just get us within combat range
		const float sav_yaw = NPCS.NPCInfo->desiredYaw;

		if (!NPC_MoveToGoal(qtrue))
		{
			NPCS.NPCInfo->consecutiveBlockedMoves++;
			vec3_t dest;
			VectorCopy(NPCS.NPCInfo->goalEntity->r.currentOrigin, dest);
			if (Rancor_CheckAhead(dest))
			{
				//can't macro-nav, just head right for him (SP)
				Rancor_MoveStraightAt(dest);
				return;
			}
			//all else fails, look at him
			NPCS.NPCInfo->lockedDesiredYaw = NPCS.NPCInfo->desiredYaw = sav_yaw;

			if (!NPCS.NPCInfo->blockedEntity && NPCS.NPC->enemy && trap->InPVS(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin))
			{
				//nothing to destroy?  just go straight at goal dest
				qboolean horz_close = qfalse;

				if (DistanceHorizontal(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) < NPCS.NPC->maxs[0] + MIN_DISTANCE)
				{
					//close, just look at him
					horz_close = qtrue;
					NPC_FaceEnemy(qtrue);
				}
				else
				{
					//try to move  towards him (SP)
					horz_close = qfalse;
					Rancor_MoveStraightAt(dest);
				}
				//let him know he should attack at random out of frustration?
				if (NPCS.NPCInfo->goalEntity == NPCS.NPC->enemy)
				{
					if (TIMER_Done(NPCS.NPC, "attacking")
						&& TIMER_Done(NPCS.NPC, "frustrationAttack"))
					{
						const float enemy_dist = Distance(dest, NPCS.NPC->r.currentOrigin);

						if ((!horz_close || !Q_irand(0, 5))
							&& Q_irand(0, 1))
						{
							Rancor_Attack(enemy_dist, qtrue, qfalse);
						}
						else
						{
							Rancor_Attack(enemy_dist, qfalse, qfalse);
						}
						if (horz_close)
						{
							TIMER_Set(NPCS.NPC, "frustrationAttack", Q_irand(2000, 5000));
						}
						else
						{
							TIMER_Set(NPCS.NPC, "frustrationAttack", Q_irand(5000, 15000));
						}
					}
				}
			}
		}
		else
		{
			NPCS.NPCInfo->consecutiveBlockedMoves = 0;
		}
	}
}
#endif

//---------------------------------------------------------

#ifdef SP_GAME
void Rancor_DropVictim(gentity_t* self)
{
	if (!self)
	{
		return;
	}

	gentity_t* victim = self->activator;

	if (victim)
	{
		// Clear EF_HELD_BY_RANCOR
		if (victim->client)
		{
			victim->client->ps.eFlags &= ~EF_HELD_BY_RANCOR;
		}

		// Break reciprocal link
		victim->activator = NULL;

		// Victim is dead?
		if (victim->health <= 0)
		{
			if (victim->s.number != 0)
			{
				// Never free player
				if (self->count == 1)
				{
					// In hand → just drop them
					if (victim->client)
					{
						victim->client->ps.SHR_legsAnimTimer = 0;
						victim->client->ps.SHR_torsoAnimTimer = 0;
					}
				}
				else
				{
					G_FreeEntity(victim);
				}
			}
			else
			{
				// NPC corpse
				victim->s.eFlags |= EF_NODRAW;

				if (victim->client)
				{
					victim->client->ps.eFlags |= EF_NODRAW;
				}

				victim->clipmask &= ~CONTENTS_BODY;
			}
		}
		else
		{
			// Victim is alive
			if (victim->NPC)
			{
				// Resume thinking
				victim->NPC->nextBStateThink = level.time;
			}

			// Clear anim timers so they fall naturally
			if (victim->client)
			{
				victim->client->ps.SHR_legsAnimTimer = 0;
				victim->client->ps.SHR_torsoAnimTimer = 0;
			}
		}

		// If the dropped victim was the current enemy, clear it
		if (self->enemy == victim)
		{
			self->enemy = NULL;
		}

		// If victim was the player, apply attack debounce
		if (victim->s.number == 0)
		{
			TIMER_Set(self, "attackDebounce",
				Q_irand(1000, 2000 + (2 - SHR_SPSKILL) * 2000));
		}

		self->activator = NULL;
	}

	// Reset grab state
	self->count = 0;
}
#else // MP keeps its own version of this function
void Rancor_DropVictim(gentity_t* self)
{
	// ----------------------------------------------------------------------
	// Safety: validate self
	// ----------------------------------------------------------------------
	if (self == NULL)
	{
		Com_Printf("Rancor_DropVictim ERROR: self == NULL\n");
		return;
	}

	// ----------------------------------------------------------------------
	// If no activator, nothing to drop
	// ----------------------------------------------------------------------
	if (self->activator == NULL)
	{
		self->count = 0;
		return;
	}

	gentity_t* act = self->activator;

	// ----------------------------------------------------------------------
	// Clear client-held flags if client exists
	// ----------------------------------------------------------------------
	if (act->client != NULL)
	{
		act->client->ps.eFlags2 &= ~EF2_HELD_BY_MONSTER;
		act->client->ps.hasLookTarget = qfalse;
		act->client->ps.lookTarget = ENTITYNUM_NONE;

		act->client->ps.viewangles[ROLL] = 0;
		SetClientViewAngle(act, act->client->ps.viewangles);

		act->r.currentAngles[PITCH] = 0;
		act->r.currentAngles[ROLL] = 0;
		G_SetAngles(act, act->r.currentAngles);
	}

	// ----------------------------------------------------------------------
	// Handle dead victim
	// ----------------------------------------------------------------------
	if (act->health <= 0)
	{
		// never free player
		if (self->count == 1)
		{
			// in hand → drop normally
			if (act->client != NULL)
			{
				act->client->ps.legsTimer = 0;
				act->client->ps.torsoTimer = 0;
			}
		}
		else
		{
			// corpse inside Rancor → hide it
			if (act->client != NULL)
			{
				act->client->ps.eFlags |= EF_NODRAW;
			}
		}
	}
	else
	{
		// ------------------------------------------------------------------
		// Living victim: resume thinking and clear anim timers
		// ------------------------------------------------------------------
		if (act->NPC != NULL)
		{
			act->NPC->nextBStateThink = level.time;
		}

		if (act->client != NULL)
		{
			act->client->ps.legsTimer = 0;
			act->client->ps.torsoTimer = 0;
		}
	}

	// ----------------------------------------------------------------------
	// Clear enemy pointer if it was the activator
	// ----------------------------------------------------------------------
	if (self->enemy == act)
	{
		self->enemy = NULL;
	}

	// ----------------------------------------------------------------------
	// Player-specific cooldown
	// ----------------------------------------------------------------------
	if (act->s.number == 0)
	{
		TIMER_Set(self,
			"attackDebounce",
			Q_irand(1000, 2000 + (2 - g_npcspskill.integer) * 2000));
	}

	// ----------------------------------------------------------------------
	// Final cleanup
	// ----------------------------------------------------------------------
	self->activator = NULL;
	self->count = 0;
}
#endif

#ifdef SP_GAME
static void Rancor_Swing(const int boltIndex, const qboolean try_grab)
{
	gentity_t* radius_ents[128];
	const float radius = NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT ? 200 : 88;
	const float radius_squared = radius * radius;
	vec3_t bolt_org;
	vec3_t origin_up;

	VectorCopy(NPCS.NPC->SHR_currentOrigin, origin_up);
	origin_up[2] += NPCS.NPC->SHR_maxs[2] * 0.75f;

	const int num_ents = NPC_GetEntsNearBolt(radius_ents, radius, boltIndex, bolt_org);

	//attacking a breakable brush
	trace_t trace;

	SHR_Trace(&trace, NPCS.NPC->pos3, vec3_origin, vec3_origin, bolt_org, NPCS.NPC->s.number, CONTENTS_SOLID | CONTENTS_BODY);
#ifndef FINAL_BUILD
	if (SHR_CVAR(g_bobaDebug).integer > 0)
	{
		G_DebugLine(NPCS.NPC->pos3, bolt_org, 1000, 0x000000ff);
	}
#endif
	//remember pos3 for the trace from last hand pos to current hand pos next time
	VectorCopy(bolt_org, NPCS.NPC->pos3);
	//FIXME: also do a trace TO the bolt from where we are...?
	if (SHR_EntIsBreakable(trace.entityNum, NPCS.NPC))
	{
		G_Damage(&g_entities[trace.entityNum], NPCS.NPC, NPCS.NPC, vec3_origin, bolt_org, 100, 0, MOD_MELEE);
	}
	else
	{
		//fuck, do an actual line trace, I guess...
		SHR_Trace(&trace, origin_up, vec3_origin, vec3_origin, bolt_org, NPCS.NPC->s.number, CONTENTS_SOLID | CONTENTS_BODY);
#ifndef FINAL_BUILD
		if (SHR_CVAR(g_bobaDebug).integer > 0)
		{
			G_DebugLine(originUp, bolt_org, 1000, 0x000000ff);
		}
#endif
		if (SHR_EntIsBreakable(trace.entityNum, NPCS.NPC))
		{
			G_Damage(&g_entities[trace.entityNum], NPCS.NPC, NPCS.NPC, vec3_origin, bolt_org, 200, 0, MOD_MELEE);
		}
	}

	for (int i = 0; i < num_ents; i++)
	{
		if (!radius_ents[i]->inuse)
		{
			continue;
		}

		if (radius_ents[i] == NPCS.NPC)
		{
			//Skip the rancor ent
			continue;
		}

		if (radius_ents[i]->client == NULL)
		{
			//must be a client
			continue;
		}

		if (radius_ents[i]->client->ps.eFlags & EF_HELD_BY_RANCOR
			|| radius_ents[i]->client->ps.eFlags & EF_HELD_BY_WAMPA)
		{
			//can't be one already being held
			continue;
		}

		if (radius_ents[i]->s.eFlags & EF_NODRAW)
		{
			//not if invisible
			continue;
		}

		if (DistanceSquared(radius_ents[i]->SHR_currentOrigin, bolt_org) <= radius_squared)
		{
			if (!gi.inPVS(radius_ents[i]->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin))
			{
				//don't grab anything that's in another PVS
				continue;
			}

			if (try_grab
				&& NPCS.NPC->count != 1
				//don't have one in hand or in mouth already - FIXME: allow one in hand and any number in mouth!
				&& radius_ents[i]->client->NPC_class != CLASS_RANCOR
				&& radius_ents[i]->client->NPC_class != CLASS_GALAKMECH
				&& radius_ents[i]->client->NPC_class != CLASS_ATST
				&& radius_ents[i]->client->NPC_class != CLASS_GONK
				&& radius_ents[i]->client->NPC_class != CLASS_R2D2
				&& radius_ents[i]->client->NPC_class != CLASS_R5D2
				&& radius_ents[i]->client->NPC_class != CLASS_MARK1
				&& radius_ents[i]->client->NPC_class != CLASS_MARK2
				&& radius_ents[i]->client->NPC_class != CLASS_MOUSE
				&& radius_ents[i]->client->NPC_class != CLASS_PROBE
				&& radius_ents[i]->client->NPC_class != CLASS_SEEKER
				&& radius_ents[i]->client->NPC_class != CLASS_REMOTE
				&& radius_ents[i]->client->NPC_class != CLASS_SENTRY
				&& radius_ents[i]->client->NPC_class != CLASS_INTERROGATOR
				&& radius_ents[i]->client->NPC_class != CLASS_DROIDEKA
				&& radius_ents[i]->client->NPC_class != CLASS_VEHICLE)
			{
				//grab
				if (NPCS.NPC->count == 2)
				{
					//have one in my mouth, remove him
					TIMER_Remove(NPCS.NPC, "clearGrabbed");
					Rancor_DropVictim(NPCS.NPC);
				}
				NPCS.NPC->enemy = radius_ents[i]; //make him my new best friend
				radius_ents[i]->client->ps.eFlags |= EF_HELD_BY_RANCOR;
				radius_ents[i]->activator = NPCS.NPC;
				// kind of dumb, but when we are locked to the Rancor, we are owned by it.
				NPCS.NPC->activator = radius_ents[i]; //remember him
				NPCS.NPC->count = 1; //in my hand
				//wait to attack
				TIMER_Set(NPCS.NPC, "attacking", NPCS.NPC->client->ps.SHR_legsAnimTimer + Q_irand(500, 2500));
				if (radius_ents[i]->health > 0)
				{
					//do pain on enemy
					GEntity_PainFunc(radius_ents[i], NPCS.NPC, NPCS.NPC, radius_ents[i]->SHR_currentOrigin, 0, MOD_CRUSH);
				}
				else if (radius_ents[i]->client)
				{
					NPC_SetAnim(radius_ents[i], SETANIM_BOTH, BOTH_SWIM_IDLE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				}
			}
			else
			{
				//smack
				SHR_Sound(radius_ents[i], G_SoundIndex("sound/chars/rancor/swipehit.wav"));
				//actually push the enemy
				vec3_t push_dir;
				if (NPCS.NPC->spawnflags & SPF_RANCOR_FASTKILL
					&& radius_ents[i]->s.number >= MAX_CLIENTS)
				{
					G_Damage(radius_ents[i], NPCS.NPC, NPCS.NPC, vec3_origin, bolt_org, radius_ents[i]->health + 1000, DAMAGE_NO_KNOCKBACK | DAMAGE_NO_PROTECTION, MOD_MELEE);
				}
				vec3_t angs;
				VectorCopy(NPCS.NPC->client->ps.viewangles, angs);
				angs[YAW] += Q_flrand(25, 50);
				angs[PITCH] = Q_flrand(-25, -15);
				AngleVectors(angs, push_dir, NULL, NULL);
				if (radius_ents[i]->client->NPC_class != CLASS_RANCOR
					&& radius_ents[i]->client->NPC_class != CLASS_ATST
					&& !(radius_ents[i]->flags & FL_NO_KNOCKBACK))
				{
					G_Throw(radius_ents[i], push_dir, 250);
					if (radius_ents[i]->health > 0)
					{
						//do pain on enemy
						G_Knockdown(radius_ents[i], NPCS.NPC, push_dir, 100, qtrue);
					}
				}
			}
		}
	}
}
#else // MP keeps its own version of this function
static void Rancor_Swing(const int boltIndex, const qboolean try_grab)
{
	int radius_ent_nums[128];
	const float radius = NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT ? 200 : 88;
	const float radius_squared = radius * radius;
	vec3_t bolt_org;

	const int num_ents = NPC_GetEntsNearBolt(radius_ent_nums, radius, boltIndex, bolt_org);
	Rancor_HitBreakables(bolt_org, 100, NULL, 0); // (SP: a breakable brush in its way)

	for (int i = 0; i < num_ents; i++)
	{
		gentity_t* radius_ent = &g_entities[radius_ent_nums[i]];
		if (!radius_ent->inuse)
		{
			continue;
		}

		if (radius_ent == NPCS.NPC)
		{
			//Skip the rancor ent
			continue;
		}

		if (radius_ent->client == NULL)
		{
			//must be a client
			continue;
		}

		if (radius_ent->client->ps.eFlags2 & EF2_HELD_BY_MONSTER)
		{
			//can't be one already being held
			continue;
		}

		if (DistanceSquared(radius_ent->r.currentOrigin, bolt_org) <= radius_squared)
		{
			if (try_grab
				&& NPCS.NPC->count != 1
				//don't have one in hand or in mouth already - FIXME: allow one in hand and any number in mouth!
				&& radius_ent->client->NPC_class != CLASS_RANCOR
				&& radius_ent->client->NPC_class != CLASS_GALAKMECH
				&& radius_ent->client->NPC_class != CLASS_ATST
				&& radius_ent->client->NPC_class != CLASS_GONK
				&& radius_ent->client->NPC_class != CLASS_R2D2
				&& radius_ent->client->NPC_class != CLASS_R5D2
				&& radius_ent->client->NPC_class != CLASS_MARK1
				&& radius_ent->client->NPC_class != CLASS_MARK2
				&& radius_ent->client->NPC_class != CLASS_MOUSE
				&& radius_ent->client->NPC_class != CLASS_PROBE
				&& radius_ent->client->NPC_class != CLASS_SEEKER
				&& radius_ent->client->NPC_class != CLASS_REMOTE
				&& radius_ent->client->NPC_class != CLASS_SENTRY
				&& radius_ent->client->NPC_class != CLASS_INTERROGATOR
				&& radius_ent->client->NPC_class != CLASS_DROIDEKA
				&& radius_ent->client->NPC_class != CLASS_VEHICLE)
			{
				//grab
				if (NPCS.NPC->count == 2)
				{
					//have one in my mouth, remove him
					TIMER_Remove(NPCS.NPC, "clearGrabbed");
					Rancor_DropVictim(NPCS.NPC);
				}
				NPCS.NPC->enemy = radius_ent; //make him my new best friend
				radius_ent->client->ps.eFlags2 |= EF2_HELD_BY_MONSTER;
				radius_ent->client->ps.hasLookTarget = qtrue;
				radius_ent->client->ps.lookTarget = NPCS.NPC->s.number;
				NPCS.NPC->activator = radius_ent; //remember him
				NPCS.NPC->count = 1; //in my hand
				//wait to attack
				TIMER_Set(NPCS.NPC, "attacking", NPCS.NPC->client->ps.legsTimer + Q_irand(500, 2500));
				if (radius_ent->health > 0 && radius_ent->pain)
				{
					//do pain on enemy
					radius_ent->pain(radius_ent, NPCS.NPC, 100);
				}
				else if (radius_ent->client)
				{
					radius_ent->client->ps.forceHandExtend = HANDEXTEND_NONE;
					radius_ent->client->ps.forceHandExtendTime = 0;
					NPC_SetAnim(radius_ent, SETANIM_BOTH, BOTH_SWIM_IDLE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				}
			}
			else
			{
				//smack
				vec3_t push_dir;
				vec3_t angs;

				G_Sound(radius_ent, CHAN_AUTO, G_SoundIndex("sound/chars/rancor/swipehit.wav"));

				if (NPCS.NPC->r.svFlags & SVF_BOT)
				{
					G_Damage(radius_ent, NPCS.NPC, NPCS.NPC, vec3_origin, bolt_org, radius_ent->health + 1000, DAMAGE_NO_KNOCKBACK | DAMAGE_NO_PROTECTION, MOD_MELEE);
				}

				VectorCopy(NPCS.NPC->client->ps.viewangles, angs);
				angs[YAW] += flrand(25, 50);
				angs[PITCH] = flrand(-25, -15);
				AngleVectors(angs, push_dir, NULL, NULL);
				if (radius_ent->client->NPC_class != CLASS_RANCOR
					&& radius_ent->client->NPC_class != CLASS_ATST)
				{
					G_Damage(radius_ent, NPCS.NPC, NPCS.NPC, vec3_origin, radius_ent->r.currentOrigin, Q_irand(25, 40), DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_MELEE);
					G_Throw(radius_ent, push_dir, 250);
					if (radius_ent->health > 0)
					{
						//do pain on enemy
						G_Knockdown(radius_ent, NPCS.NPC, push_dir, 100, qtrue);
					}
				}
			}
		}
	}
}
#endif

#ifdef SP_GAME
static void Rancor_Smash(void)
{
	gentity_t* radius_ents[128];
	const float radius = NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT ? 256 : 128;
	const float half_rad_squared = radius / 2 * (radius / 2);
	const float radius_squared = radius * radius;
	vec3_t bolt_org;

	AddSoundEvent(NPCS.NPC, NPCS.NPC->SHR_currentOrigin, 1024, AEL_DANGER, qfalse, qtrue);

	const int num_ents = NPC_GetEntsNearBolt(radius_ents, radius, NPCS.NPC->handLBolt, bolt_org);

	//attacking a breakable brush
	trace_t trace;

	SHR_Trace(&trace, bolt_org, vec3_origin, vec3_origin, NPCS.NPC->pos3, NPCS.NPC->s.number, CONTENTS_SOLID | CONTENTS_BODY);
#ifndef FINAL_BUILD
	if (SHR_CVAR(g_bobaDebug).integer > 0)
	{
		G_DebugLine(NPCS.NPC->pos3, bolt_org, 1000, 0x000000ff);
	}
#endif
	//remember pos3 for the trace from last hand pos to current hand pos next time
	VectorCopy(bolt_org, NPCS.NPC->pos3);
	//FIXME: also do a trace TO the bolt from where we are...?
	if (SHR_EntIsBreakable(trace.entityNum, NPCS.NPC))
	{
		G_Damage(&g_entities[trace.entityNum], NPCS.NPC, NPCS.NPC, vec3_origin, bolt_org, 200, 0, MOD_MELEE);
	}
	else
	{
		//fuck, do an actual line trace, I guess...
		SHR_Trace(&trace, NPCS.NPC->SHR_currentOrigin, vec3_origin, vec3_origin, bolt_org, NPCS.NPC->s.number,
			CONTENTS_SOLID | CONTENTS_BODY);
#ifndef FINAL_BUILD
		if (SHR_CVAR(g_bobaDebug).integer > 0)
		{
			G_DebugLine(NPCS.NPC->SHR_currentOrigin, bolt_org, 1000, 0x000000ff);
		}
#endif
		if (SHR_EntIsBreakable(trace.entityNum, NPCS.NPC))
		{
			G_Damage(&g_entities[trace.entityNum], NPCS.NPC, NPCS.NPC, vec3_origin, bolt_org, 200, 0, MOD_MELEE);
		}
	}

	for (int i = 0; i < num_ents; i++)
	{
		if (!radius_ents[i]->inuse)
		{
			continue;
		}

		if (radius_ents[i] == NPCS.NPC)
		{
			//Skip the rancor ent
			continue;
		}

		if (radius_ents[i]->client == NULL)
		{
			//must be a client
			if (SHR_EntIsBreakable(radius_ents[i]->s.number, NPCS.NPC))
			{
				//damage breakables within range, but not as much
				if (!Q_irand(0, 1))
				{
					G_Damage(radius_ents[i], NPCS.NPC, NPCS.NPC, vec3_origin, radius_ents[i]->SHR_currentOrigin, 100, 0, MOD_MELEE);
				}
			}
			continue;
		}

		if (radius_ents[i]->client->ps.eFlags & EF_HELD_BY_RANCOR)
		{
			//can't be one being held
			continue;
		}

		if (radius_ents[i]->s.eFlags & EF_NODRAW)
		{
			//not if invisible
			continue;
		}

		const float dist_sq = DistanceSquared(radius_ents[i]->SHR_currentOrigin, bolt_org);
		if (dist_sq <= radius_squared)
		{
			if (dist_sq < half_rad_squared)
			{
				//close enough to do damage, too
				SHR_Sound(radius_ents[i], G_SoundIndex("sound/chars/rancor/swipehit.wav"));
				if (NPCS.NPC->spawnflags & SPF_RANCOR_FASTKILL
					&& radius_ents[i]->s.number >= MAX_CLIENTS)
				{
					G_Damage(radius_ents[i], NPCS.NPC, NPCS.NPC, vec3_origin, bolt_org, radius_ents[i]->health + 1000, DAMAGE_NO_KNOCKBACK | DAMAGE_NO_PROTECTION, MOD_MELEE);
				}
				else if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT) //FIXME: a flag or something would be better
				{
					//more damage
					G_Damage(radius_ents[i], NPCS.NPC, NPCS.NPC, vec3_origin, radius_ents[i]->SHR_currentOrigin, Q_irand(40, 55), DAMAGE_NO_KNOCKBACK, MOD_MELEE);
				}
				else
				{
					G_Damage(radius_ents[i], NPCS.NPC, NPCS.NPC, vec3_origin, radius_ents[i]->SHR_currentOrigin, Q_irand(10, 25), DAMAGE_NO_KNOCKBACK, MOD_MELEE);
				}
			}
			if (radius_ents[i]->health > 0
				&& radius_ents[i]->client
				&& radius_ents[i]->client->NPC_class != CLASS_RANCOR
				&& radius_ents[i]->client->NPC_class != CLASS_ATST)
			{
				if (dist_sq < half_rad_squared
					|| radius_ents[i]->client->ps.groundEntityNum != ENTITYNUM_NONE)
				{
					//within range of my fist or withing ground-shaking range and not in the air
					if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT)
					{
						G_Knockdown(radius_ents[i], NPCS.NPC, vec3_origin, 500, qtrue);
					}
					else
					{
						G_Knockdown(radius_ents[i], NPCS.NPC, vec3_origin, Q_irand(200, 350), qtrue);
					}
				}
			}
		}
	}
}
#else // MP keeps its own version of this function
static void Rancor_Smash(void)
{
	int radius_ent_nums[128];
	const float radius = NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT ? 256 : 128;
	const float half_rad_squared = radius / 2 * (radius / 2);
	const float radius_squared = radius * radius;
	vec3_t bolt_org;

	AddSoundEvent(NPCS.NPC, NPCS.NPC->r.currentOrigin, 1024, AEL_DANGER, qfalse, qtrue);

	const int num_ents = NPC_GetEntsNearBolt(radius_ent_nums, radius, NPCS.NPC->client->renderInfo.handLBolt, bolt_org);
	Rancor_HitBreakables(bolt_org, 200, radius_ent_nums, num_ents); // (SP: a breakable brush in its way)

	for (int i = 0; i < num_ents; i++)
	{
		gentity_t* radius_ent = &g_entities[radius_ent_nums[i]];
		if (!radius_ent->inuse)
		{
			continue;
		}

		if (radius_ent == NPCS.NPC)
		{
			//Skip the rancor ent
			continue;
		}

		if (radius_ent->client == NULL)
		{
			//must be a client
			continue;
		}

		if (radius_ent->client->ps.eFlags2 & EF2_HELD_BY_MONSTER)
		{
			//can't be one being held
			continue;
		}

		const float dist_sq = DistanceSquared(radius_ent->r.currentOrigin, bolt_org);
		if (dist_sq <= radius_squared)
		{
			G_Sound(radius_ent, CHAN_AUTO, G_SoundIndex("sound/chars/rancor/swipehit.wav"));
			if (dist_sq < half_rad_squared)
			{
				//close enough to do damage, too
				if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT)
				{
					//more damage (SP)
					G_Damage(radius_ent, NPCS.NPC, NPCS.NPC, vec3_origin, radius_ent->r.currentOrigin, Q_irand(40, 55), DAMAGE_NO_KNOCKBACK, MOD_MELEE);
				}
				else
				{
					G_Damage(radius_ent, NPCS.NPC, NPCS.NPC, vec3_origin, radius_ent->r.currentOrigin, Q_irand(10, 25), DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_MELEE);
				}
			}
			if (radius_ent->health > 0
				&& radius_ent->client
				&& radius_ent->client->NPC_class != CLASS_RANCOR
				&& radius_ent->client->NPC_class != CLASS_ATST)
			{
				if (dist_sq < half_rad_squared
					|| radius_ent->client->ps.groundEntityNum != ENTITYNUM_NONE)
				{
					//within range of my fist or withing ground-shaking range and not in the air
					G_Knockdown(radius_ent, NPCS.NPC, vec3_origin, NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT ? 500 : 100, qtrue);
				}
			}
		}
	}
}
#endif

#ifdef SP_GAME
static void Rancor_Bite(void)
{
	gentity_t* radius_ents[128];
	const float radius = 100;
	const float radius_squared = radius * radius;
	vec3_t bolt_org;

	const int num_ents = NPC_GetEntsNearBolt(radius_ents, radius, NPCS.NPC->gutBolt, bolt_org);

	for (int i = 0; i < num_ents; i++)
	{
		if (!radius_ents[i]->inuse)
		{
			continue;
		}

		if (radius_ents[i] == NPCS.NPC)
		{
			//Skip the rancor ent
			continue;
		}

		if (radius_ents[i]->client == NULL)
		{
			//must be a client
			continue;
		}

		if (radius_ents[i]->client->ps.eFlags & EF_HELD_BY_RANCOR)
		{
			//can't be one already being held
			continue;
		}

		if (radius_ents[i]->s.eFlags & EF_NODRAW)
		{
			//not if invisible
			continue;
		}

		if (DistanceSquared(radius_ents[i]->SHR_currentOrigin, bolt_org) <= radius_squared)
		{
			if (NPCS.NPC->spawnflags & SPF_RANCOR_FASTKILL
				&& radius_ents[i]->s.number >= MAX_CLIENTS)
			{
				G_Damage(radius_ents[i], NPCS.NPC, NPCS.NPC, vec3_origin, radius_ents[i]->SHR_currentOrigin, radius_ents[i]->health + 1000, DAMAGE_NO_KNOCKBACK | DAMAGE_NO_PROTECTION, MOD_MELEE);
			}
			else if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT)
			{
				//more damage
				G_Damage(radius_ents[i], NPCS.NPC, NPCS.NPC, vec3_origin, radius_ents[i]->SHR_currentOrigin, Q_irand(35, 50), DAMAGE_NO_KNOCKBACK, MOD_MELEE);
			}
			else
			{
				G_Damage(radius_ents[i], NPCS.NPC, NPCS.NPC, vec3_origin, radius_ents[i]->SHR_currentOrigin, Q_irand(15, 30), DAMAGE_NO_KNOCKBACK, MOD_MELEE);
			}
			if (radius_ents[i]->health <= 0 && radius_ents[i]->client)
			{
				//killed them, chance of dismembering
				if (!Q_irand(0, 1))
				{
					//bite something off
					int hit_loc = 0;
					if (SHR_CVAR(g_dismemberment).integer < 3)
					{
						hit_loc = Q_irand(HL_BACK_RT, HL_HAND_LT);
					}
					else
					{
						hit_loc = Q_irand(HL_WAIST, HL_HEAD);
					}
					if (hit_loc == HL_HEAD)
					{
						NPC_SetAnim(radius_ents[i], SETANIM_BOTH, BOTH_DEATH17, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					}
					else if (hit_loc == HL_WAIST)
					{
						NPC_SetAnim(radius_ents[i], SETANIM_BOTH, BOTH_DEATHBACKWARD2, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					}
					radius_ents[i]->client->dismembered = false;
					G_DoDismemberment(radius_ents[i], radius_ents[i]->SHR_currentOrigin, MOD_SABER, hit_loc, qtrue);
				}
			}
			SHR_Sound(radius_ents[i], G_SoundIndex("sound/chars/rancor/chomp.wav"));
		}
	}
}
#else // MP keeps its own version of this function
static void Rancor_Bite(void)
{
	int radius_ent_nums[128];
	const float radius = 100;
	const float radius_squared = radius * radius;
	vec3_t bolt_org;

	const int num_ents = NPC_GetEntsNearBolt(radius_ent_nums, radius, NPCS.NPC->client->renderInfo.crotchBolt, bolt_org);
	//was gutBolt?

	for (int i = 0; i < num_ents; i++)
	{
		gentity_t* radius_ent = &g_entities[radius_ent_nums[i]];
		if (!radius_ent->inuse)
		{
			continue;
		}

		if (radius_ent == NPCS.NPC)
		{
			//Skip the rancor ent
			continue;
		}

		if (radius_ent->client == NULL)
		{
			//must be a client
			continue;
		}

		if (radius_ent->client->ps.eFlags2 & EF2_HELD_BY_MONSTER)
		{
			//can't be one already being held
			continue;
		}

		if (DistanceSquared(radius_ent->r.currentOrigin, bolt_org) <= radius_squared)
		{
			G_Damage(radius_ent, NPCS.NPC, NPCS.NPC, vec3_origin, radius_ent->r.currentOrigin, radius_ent->health + 9999, DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_MELEE);

			if (radius_ent->health <= 0 && radius_ent->client)
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
						NPC_SetAnim(radius_ent, SETANIM_BOTH, BOTH_DEATHBACKWARD2, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					}
					G_Dismember(radius_ent, NPCS.NPC, radius_ent->r.currentOrigin, hit_loc);
				}
			}
			G_Sound(radius_ent, CHAN_AUTO, G_SoundIndex("sound/chars/rancor/chomp.wav"));
		}
	}
}
#endif

//------------------------------
#ifdef SP_GAME // MP's TossClientItems returns nothing
extern gentity_t* TossClientItems(gentity_t* self);
#endif

#ifdef SP_GAME
void Rancor_Attack(const float distance, const qboolean do_charge, const qboolean aim_at_blocked_entity)
{
	// Only begin a new attack if not already attacking and debounce expired
	if (!TIMER_Exists(NPCS.NPC, "attacking") && TIMER_Done(NPCS.NPC, "attackDebounce"))
	{
		// ---------------------------------------------------------
		// CASE: NPC->count == 2 → already swallowed victim
		// ---------------------------------------------------------
		if (NPCS.NPC->count == 2 && NPCS.NPC->activator)
		{
			// No special behaviour here
		}
		// ---------------------------------------------------------
		// CASE: NPC->count == 1 → holding victim
		// ---------------------------------------------------------
		else if (NPCS.NPC->count == 1 && NPCS.NPC->activator)
		{
			// Quick bite or full eat
			if ((!(NPCS.NPC->spawnflags & SPF_RANCOR_FASTKILL) || NPCS.NPC->activator->s.number < MAX_CLIENTS)
				&& NPCS.NPC->activator->health > 0
				&& Q_irand(0, 1))
			{
				// Quick bite
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ATTACK1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				TIMER_Set(NPCS.NPC, "attack_dmg", 450);
			}
			else
			{
				// Full eat
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ATTACK3, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				TIMER_Set(NPCS.NPC, "attack_dmg", 900);

				// Victim scream
				if (NPCS.NPC->activator->health > 0 && NPCS.NPC->activator->client)
				{
					G_AddEvent(NPCS.NPC->activator, Q_irand(EV_DEATH1, EV_DEATH3), 0);
					NPC_SetAnim(NPCS.NPC->activator, SETANIM_TORSO, BOTH_FALLDEATH1,
						SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

					if (NPCS.NPC->activator->NPC)
					{
						TossClientItems(NPCS.NPC);
						NPCS.NPC->activator->NPC->nextBStateThink = Q3_INFINITE;
					}
				}
			}
		}
		// ---------------------------------------------------------
		// CASE: Charge attacks
		// ---------------------------------------------------------
		else if (NPCS.NPC->enemy && NPCS.NPC->enemy->health > 0 && do_charge == qtrue)
		{
			if (!Q_irand(0, 3))
			{
				// Charge attack
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ATTACK5, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				TIMER_Set(NPCS.NPC, "attack_dmg", 1250);

				if (NPCS.NPC->enemy && NPCS.NPC->enemy->s.number == 0)
				{
					TIMER_Set(NPCS.NPC, "attackDebounce",
						NPCS.NPC->client->ps.SHR_legsAnimTimer +
						Q_irand(1000, 2000 + (2 - SHR_SPSKILL) * 2000));
				}
			}
			else if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT)
			{
				// Breath attack
				int breath_anim = BOTH_ATTACK4;
				const gentity_t* check_ent = NULL;
				vec3_t center;

				if (NPCS.NPC->enemy && NPCS.NPC->enemy->inuse)
				{
					check_ent = NPCS.NPC->enemy;
					VectorCopy(NPCS.NPC->enemy->SHR_currentOrigin, center);
				}
				else if (NPCS.NPCInfo->blockedEntity && NPCS.NPCInfo->blockedEntity->inuse)
				{
					check_ent = NPCS.NPCInfo->blockedEntity;

					if (VectorCompare(NPCS.NPCInfo->blockedEntity->s.origin, vec3_origin))
					{
						VectorAdd(NPCS.NPCInfo->blockedEntity->SHR_mins, NPCS.NPCInfo->blockedEntity->SHR_maxs, center);
						VectorScale(center, 0.5f, center);
					}
					else
					{
						VectorCopy(NPCS.NPCInfo->blockedEntity->s.origin, center);
					}
				}

				if (check_ent)
				{
					const float z_height_relative = center[2] - NPCS.NPC->SHR_currentOrigin[2];

					if (z_height_relative >= 128.0f * NPCS.NPC->s.modelScale[2])
					{
						breath_anim = BOTH_ATTACK7;
					}
					else if (z_height_relative >= 64.0f * NPCS.NPC->s.modelScale[2])
					{
						breath_anim = BOTH_ATTACK6;
					}
				}

				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, breath_anim,
					SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

				G_PlayEffect(G_EffectIndex("mrancor/breath"), NPCS.NPC->playerModel, NPCS.NPC->gutBolt,
					NPCS.NPC->s.number, NPCS.NPC->SHR_currentOrigin,
					NPCS.NPC->client->ps.SHR_legsAnimTimer - 500, qfalse);

				TIMER_Set(NPCS.NPC, "breathAttack", NPCS.NPC->client->ps.SHR_legsAnimTimer - 500);
				SHR_SoundOnEnt(NPCS.NPC, CHAN_WEAPON, "sound/chars/rancor/breath_start.wav");
				NPCS.NPC->s.loopSound = G_SoundIndex("sound/chars/rancor/breath_loop.wav");

				if (NPCS.NPC->enemy && NPCS.NPC->enemy->s.number == 0)
				{
					TIMER_Set(NPCS.NPC, "attackDebounce",
						NPCS.NPC->client->ps.SHR_legsAnimTimer +
						Q_irand(1000, 2000 + (2 - SHR_SPSKILL) * 2000));
				}
			}
			else
			{
				// Melee charge
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_MELEE2, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				TIMER_Set(NPCS.NPC, "attack_dmg", 1250);

				vec3_t fwd;
				const vec3_t yaw_ang = { 0, NPCS.NPC->client->ps.viewangles[YAW], 0 };
				AngleVectors(yaw_ang, fwd, NULL, NULL);

				VectorScale(fwd, distance * 1.5f, NPCS.NPC->client->ps.velocity);
				NPCS.NPC->client->ps.velocity[2] = 150;
				NPCS.NPC->client->ps.groundEntityNum = ENTITYNUM_NONE;

				if (NPCS.NPC->enemy && NPCS.NPC->enemy->s.number == 0)
				{
					TIMER_Set(NPCS.NPC, "attackDebounce",
						NPCS.NPC->client->ps.SHR_legsAnimTimer +
						Q_irand(2000, 4000 + (2 - SHR_SPSKILL) * 2000));
				}
			}
		}
		// ---------------------------------------------------------
		// Smash attack
		// ---------------------------------------------------------
		else if (!Q_irand(0, 1))
		{
			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_MELEE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			TIMER_Set(NPCS.NPC, "attack_dmg", 900);
			VectorCopy(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->pos3);
		}
		// ---------------------------------------------------------
		// Grab attack
		// ---------------------------------------------------------
		else if ((NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT)
			|| distance >= NPCS.NPC->SHR_maxs[0] + MIN_DISTANCE * NPCS.NPC->s.modelScale[0] - 64.0f)
		{
			int grabAnim = BOTH_ATTACK2;
			const gentity_t* check_ent = NULL;
			vec3_t center;

			if ((aim_at_blocked_entity == qfalse || !NPCS.NPCInfo->blockedEntity)
				&& NPCS.NPC->enemy && NPCS.NPC->enemy->inuse)
			{
				check_ent = NPCS.NPC->enemy;
				VectorCopy(NPCS.NPC->enemy->SHR_currentOrigin, center);
			}
			else if (NPCS.NPCInfo->blockedEntity && NPCS.NPCInfo->blockedEntity->inuse)
			{
				check_ent = NPCS.NPCInfo->blockedEntity;

				if (VectorCompare(NPCS.NPCInfo->blockedEntity->s.origin, vec3_origin))
				{
					VectorAdd(NPCS.NPCInfo->blockedEntity->SHR_mins, NPCS.NPCInfo->blockedEntity->SHR_maxs, center);
					VectorScale(center, 0.5f, center);
				}
				else
				{
					VectorCopy(NPCS.NPCInfo->blockedEntity->s.origin, center);
				}
			}

			if (check_ent)
			{
				const float z_height_relative = center[2] - NPCS.NPC->SHR_currentOrigin[2];

				if (z_height_relative >= 128.0f * NPCS.NPC->s.modelScale[2])
				{
					grabAnim = BOTH_ATTACK11;
				}
				else if (z_height_relative >= 64.0f * NPCS.NPC->s.modelScale[2])
				{
					grabAnim = BOTH_ATTACK10;
				}
			}

			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, grabAnim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			TIMER_Set(NPCS.NPC, "attack_dmg", 800);

			if (NPCS.NPC->enemy && NPCS.NPC->enemy->s.number == 0)
			{
				TIMER_Set(NPCS.NPC, "attackDebounce",
					NPCS.NPC->client->ps.SHR_legsAnimTimer +
					Q_irand(2000, 4000 + (2 - SHR_SPSKILL) * 2000));
			}

			VectorCopy(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->pos3);
		}
		else
		{
			// Back up
			NPCS.ucmd.forwardmove = -64;
			return;
		}

		// Mark attack as active
		TIMER_Set(NPCS.NPC, "attacking",
			NPCS.NPC->client->ps.SHR_legsAnimTimer + Q_flrand(0.0f, 1.0f) * 200);
	}

	// ---------------------------------------------------------
	// DELAYED DAMAGE PHASE
	// ---------------------------------------------------------

	if (TIMER_Done2(NPCS.NPC, "attack_dmg", qtrue))
	{
		float player_dist = 0.0f;

		switch (NPCS.NPC->client->ps.legsAnim)
		{
		case BOTH_MELEE1:
			Rancor_Smash();
			player_dist = NPC_EntRangeFromBolt(player, NPCS.NPC->handLBolt);

			if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT)
			{
				if (player_dist < 512)
				{
					CGCam_Shake(1.0f * player_dist / 256.0f, 1000);
				}
			}
			else
			{
				if (player_dist < 256)
				{
					CGCam_Shake(1.0f * player_dist / 128.0f, 1000);
				}
			}
			break;

		case BOTH_MELEE2:
			Rancor_Bite();
			TIMER_Set(NPCS.NPC, "attack_dmg2", 450);
			break;

		case BOTH_ATTACK1:
			if (NPCS.NPC->count == 1 && NPCS.NPC->activator)
			{
				if ((NPCS.NPC->spawnflags & SPF_RANCOR_FASTKILL)
					&& NPCS.NPC->activator->s.number >= MAX_CLIENTS)
				{
					G_Damage(NPCS.NPC->activator, NPCS.NPC, NPCS.NPC, vec3_origin,
						NPCS.NPC->activator->SHR_currentOrigin,
						NPCS.NPC->activator->health + 1000,
						DAMAGE_NO_KNOCKBACK | DAMAGE_NO_PROTECTION,
						MOD_MELEE);
				}
				else if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT)
				{
					G_Damage(NPCS.NPC->activator, NPCS.NPC, NPCS.NPC, vec3_origin,
						NPCS.NPC->activator->SHR_currentOrigin,
						Q_irand(55, 70),
						DAMAGE_NO_KNOCKBACK,
						MOD_MELEE);
				}
				else
				{
					G_Damage(NPCS.NPC->activator, NPCS.NPC, NPCS.NPC, vec3_origin,
						NPCS.NPC->activator->SHR_currentOrigin,
						Q_irand(25, 40),
						DAMAGE_NO_KNOCKBACK,
						MOD_MELEE);
				}

				if (NPCS.NPC->activator->health <= 0)
				{
					if (SHR_CVAR(g_dismemberment).integer >= 3)
					{
						NPCS.NPC->activator->client->dismembered = qfalse;
						G_DoDismemberment(NPCS.NPC->activator,
							NPCS.NPC->activator->SHR_currentOrigin,
							MOD_SABER,
							HL_HEAD,
							qtrue);
					}

					NPC_SetAnim(NPCS.NPC->activator,
						SETANIM_BOTH,
						BOTH_SWIM_IDLE1,
						SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				}

				SHR_Sound(NPCS.NPC->activator,
					G_SoundIndex("sound/chars/rancor/chomp.wav"));
			}
			break;

		case BOTH_ATTACK2:
		case BOTH_ATTACK10:
		case BOTH_ATTACK11:
			Rancor_Swing(NPCS.NPC->handRBolt, qtrue);
			break;

		case BOTH_ATTACK3:
			if (NPCS.NPC->count == 1 && NPCS.NPC->activator)
			{
				// SAFE FIX: enemy may be NULL → guard it
				if (NPCS.NPC->activator->client)
				{
					NPCS.NPC->activator->client->dismembered = qfalse;

					if (NPCS.NPC->enemy != NULL)
					{
						G_DoDismemberment(NPCS.NPC->activator,
							NPCS.NPC->enemy->SHR_currentOrigin,
							MOD_SABER,
							HL_WAIST,
							qtrue);
					}
				}

				if (NPCS.NPC->enemy != NULL)
				{
					G_Damage(NPCS.NPC->activator, NPCS.NPC, NPCS.NPC, vec3_origin,
						NPCS.NPC->activator->SHR_currentOrigin,
						NPCS.NPC->enemy->health + 1000,
						DAMAGE_NO_PROTECTION | DAMAGE_NO_ARMOR |
						DAMAGE_NO_KNOCKBACK | DAMAGE_NO_HIT_LOC,
						MOD_MELEE,
						HL_NONE);
				}
				else
				{
					// Fallback: kill activator cleanly
					G_Damage(NPCS.NPC->activator, NPCS.NPC, NPCS.NPC, vec3_origin,
						NPCS.NPC->activator->SHR_currentOrigin,
						NPCS.NPC->activator->health + 1000,
						DAMAGE_NO_PROTECTION | DAMAGE_NO_ARMOR |
						DAMAGE_NO_KNOCKBACK | DAMAGE_NO_HIT_LOC,
						MOD_MELEE,
						HL_NONE);
				}

				if (NPCS.NPC->activator->client)
				{
					NPC_SetAnim(NPCS.NPC->activator,
						SETANIM_BOTH,
						BOTH_SWIM_IDLE1,
						SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				}

				TIMER_Set(NPCS.NPC, "attack_dmg2", 1350);
				SHR_Sound(NPCS.NPC->activator,
					G_SoundIndex("sound/chars/rancor/swipehit.wav"));
				G_AddEvent(NPCS.NPC->activator, EV_JUMP, NPCS.NPC->activator->health);
			}
			break;

		default:
			break;
		}
	}
	// ---------------------------------------------------------
	// SECONDARY DAMAGE PHASE
	// ---------------------------------------------------------
	else if (TIMER_Done2(NPCS.NPC, "attack_dmg2", qtrue))
	{
		switch (NPCS.NPC->client->ps.legsAnim)
		{
		case BOTH_MELEE2:
			Rancor_Bite();
			break;

		case BOTH_ATTACK3:
			if (NPCS.NPC->count == 1 && NPCS.NPC->activator)
			{
				SHR_Sound(NPCS.NPC->activator,
					G_SoundIndex("sound/chars/rancor/chomp.wav"));

				if (NPCS.NPC->activator->health > 0)
				{
					NPCS.NPC->activator->client->dismembered = qfalse;

					if (NPCS.NPC->enemy != NULL)
					{
						G_DoDismemberment(NPCS.NPC->activator,
							NPCS.NPC->enemy->SHR_currentOrigin,
							MOD_SABER,
							HL_WAIST,
							qtrue);

						G_Damage(NPCS.NPC->activator, NPCS.NPC, NPCS.NPC, vec3_origin,
							NPCS.NPC->activator->SHR_currentOrigin,
							NPCS.NPC->enemy->health + 1000,
							DAMAGE_NO_PROTECTION | DAMAGE_NO_ARMOR |
							DAMAGE_NO_KNOCKBACK | DAMAGE_NO_HIT_LOC,
							MOD_MELEE,
							HL_NONE);
					}
					else
					{
						// Fallback kill
						G_Damage(NPCS.NPC->activator, NPCS.NPC, NPCS.NPC, vec3_origin,
							NPCS.NPC->activator->SHR_currentOrigin,
							NPCS.NPC->activator->health + 1000,
							DAMAGE_NO_PROTECTION | DAMAGE_NO_ARMOR |
							DAMAGE_NO_KNOCKBACK | DAMAGE_NO_HIT_LOC,
							MOD_MELEE,
							HL_NONE);
					}

					NPC_SetAnim(NPCS.NPC->activator,
						SETANIM_BOTH,
						BOTH_SWIM_IDLE1,
						SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

					G_AddEvent(NPCS.NPC->activator, EV_JUMP, NPCS.NPC->activator->health);
				}

				NPCS.NPC->count = 2;
				TIMER_Set(NPCS.NPC, "clearGrabbed", 2600);
			}
			break;

		default:
			break;
		}
	}

	// Clear attacking flag when timer expires
	TIMER_Done2(NPCS.NPC, "attacking", qtrue);
}
#else // MP keeps its own version of this function
void Rancor_Attack(const float distance, const qboolean do_charge, const qboolean aim_at_blocked_entity)
{
	if (!TIMER_Exists(NPCS.NPC, "attacking")
		&& TIMER_Done(NPCS.NPC, "attackDebounce"))
	{
		if (NPCS.NPC->count == 2 && NPCS.NPC->activator)
		{
		}
		else if (NPCS.NPC->count == 1 && NPCS.NPC->activator)
		{
			//holding enemy
			if (NPCS.NPC->activator->health > 0 && Q_irand(0, 1))
			{
				//quick bite
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ATTACK1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				TIMER_Set(NPCS.NPC, "attack_dmg", 450);
			}
			else
			{
				//full eat
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ATTACK3, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				TIMER_Set(NPCS.NPC, "attack_dmg", 900);
				//Make victim scream in fright
				if (NPCS.NPC->activator->health > 0 && NPCS.NPC->activator->client)
				{
					G_AddEvent(NPCS.NPC->activator, Q_irand(EV_DEATH1, EV_DEATH3), 0);
					NPC_SetAnim(NPCS.NPC->activator, SETANIM_TORSO, BOTH_FALLDEATH1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

					if (NPCS.NPC->activator->NPC)
					{
						//no more thinking for you
						TossClientItems(NPCS.NPC);
						NPCS.NPC->activator->NPC->nextBStateThink = Q3_INFINITE;
					}
				}
			}
		}
		else if (NPCS.NPC->enemy->health > 0 && do_charge)
		{
			//charge
			if (!Q_irand(0, 3))
			{
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ATTACK5, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				TIMER_Set(NPCS.NPC, "attack_dmg", 1250);

				if (NPCS.NPC->enemy && NPCS.NPC->enemy->s.number == 0)
				{
					//don't attack the player again for a bit
					TIMER_Set(NPCS.NPC, "attackDebounce", NPCS.NPC->client->ps.legsTimer + Q_irand(2000, 4000 + (2 - g_npcspskill.integer) * 2000));
				}
			}
			else if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT)
			{
				// Breath attack (SP): aimed up at a high enemy (ATTACK6 / 7); the damage is Rancor_FireBreathAttack
				int breathAnim = BOTH_ATTACK4;
				const gentity_t* check_ent = NULL;
				vec3_t center;

				if (NPCS.NPC->enemy && NPCS.NPC->enemy->inuse)
				{
					check_ent = NPCS.NPC->enemy;
					VectorCopy(NPCS.NPC->enemy->r.currentOrigin, center);
				}
				else if (NPCS.NPCInfo->blockedEntity && NPCS.NPCInfo->blockedEntity->inuse)
				{
					check_ent = NPCS.NPCInfo->blockedEntity;
					if (VectorCompare(NPCS.NPCInfo->blockedEntity->s.origin, vec3_origin))
					{
						VectorAdd(NPCS.NPCInfo->blockedEntity->r.absmin, NPCS.NPCInfo->blockedEntity->r.absmax, center);
						VectorScale(center, 0.5f, center);
					}
					else
					{
						VectorCopy(NPCS.NPCInfo->blockedEntity->s.origin, center);
					}
				}
				if (check_ent)
				{
					const float z_rel = center[2] - NPCS.NPC->r.currentOrigin[2];
					if (z_rel >= 128.0f * NPCS.NPC->modelScale[2])
					{
						breathAnim = BOTH_ATTACK7;
					}
					else if (z_rel >= 64.0f * NPCS.NPC->modelScale[2])
					{
						breathAnim = BOTH_ATTACK6;
					}
				}

				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, breathAnim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				TIMER_Set(NPCS.NPC, "breathAttack", NPCS.NPC->client->ps.legsTimer - 500);
				TIMER_Set(NPCS.NPC, "breathFx", 0);
				G_SoundOnEnt(NPCS.NPC, CHAN_WEAPON, "sound/chars/rancor/breath_start.wav");
				NPCS.NPC->s.loopSound = G_SoundIndex("sound/chars/rancor/breath_loop.wav");

				if (NPCS.NPC->enemy && NPCS.NPC->enemy->s.number == 0)
				{
					//don't attack the player again for a bit
					TIMER_Set(NPCS.NPC, "attackDebounce", NPCS.NPC->client->ps.legsTimer + Q_irand(2000, 4000 + (2 - g_npcspskill.integer) * 2000));
				}
			}
			else
			{
				//charge
				vec3_t fwd, yaw_ang;
				VectorSet(yaw_ang, 0, NPCS.NPC->client->ps.viewangles[YAW], 0);
				AngleVectors(yaw_ang, fwd, NULL, NULL);
				VectorScale(fwd, distance * 1.5f, NPCS.NPC->client->ps.velocity);
				NPCS.NPC->client->ps.velocity[2] = 150;
				NPCS.NPC->client->ps.groundEntityNum = ENTITYNUM_NONE;

				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_MELEE2, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				TIMER_Set(NPCS.NPC, "attack_dmg", 1250);

				if (NPCS.NPC->enemy && NPCS.NPC->enemy->s.number == 0)
				{
					//don't attack the player again for a bit
					TIMER_Set(NPCS.NPC, "attackDebounce", NPCS.NPC->client->ps.legsTimer + Q_irand(2000, 4000 + (2 - g_npcspskill.integer) * 2000));
				}
			}
		}
		else if (!Q_irand(0, 1))
		{
			//smash
			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_MELEE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			TIMER_Set(NPCS.NPC, "attack_dmg", 1000);
			VectorCopy(NPCS.NPC->r.currentOrigin, NPCS.NPC->pos3); // (SP: the hand's sweep for breakables starts here)
		}
		else if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT || distance >= NPCS.NPC->maxs[0] + MIN_DISTANCE - 64.0f)
		{
			//try to grab
			int grabAnim = BOTH_ATTACK2;
			const gentity_t* check_ent = NULL;
			vec3_t center;
			if ((!aim_at_blocked_entity || !NPCS.NPCInfo->blockedEntity) && NPCS.NPC->enemy && NPCS.NPC->enemy->inuse)
			{
				check_ent = NPCS.NPC->enemy;
				VectorCopy(NPCS.NPC->enemy->r.currentOrigin, center);
			}
			else if (NPCS.NPCInfo->blockedEntity && NPCS.NPCInfo->blockedEntity->inuse)
			{
				check_ent = NPCS.NPCInfo->blockedEntity;
				//if it has an origin brush, use it...
				if (VectorCompare(NPCS.NPCInfo->blockedEntity->s.origin, vec3_origin))
				{
					//no origin brush, calc center
					VectorAdd(NPCS.NPCInfo->blockedEntity->mins, NPCS.NPCInfo->blockedEntity->maxs, center);
					VectorScale(center, 0.5f, center);
				}
				else
				{
					//use origin brush as center
					VectorCopy(NPCS.NPCInfo->blockedEntity->s.origin, center);
				}
			}
			if (check_ent)
			{
				const float z_height_relative = center[2] - NPCS.NPC->r.currentOrigin[2];
				if (z_height_relative >= 128.0f)
				{
					grabAnim = BOTH_ATTACK11;
				}
				else if (z_height_relative >= 64.0f)
				{
					grabAnim = BOTH_ATTACK10;
				}
			}
			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, grabAnim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			TIMER_Set(NPCS.NPC, "attack_dmg", 800);
			if (NPCS.NPC->enemy && NPCS.NPC->enemy->s.number == 0)
			{
				//don't attack the player again for a bit
				TIMER_Set(NPCS.NPC, "attackDebounce", NPCS.NPC->client->ps.legsTimer + Q_irand(2000, 4000 + (2 - g_npcspskill.integer) * 2000));
			}
			//init pos3 for the trace from last hand pos to current hand pos
			VectorCopy(NPCS.NPC->r.currentOrigin, NPCS.NPC->pos3);
		}
		else
		{
			//try to grab
			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ATTACK2, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			TIMER_Set(NPCS.NPC, "attack_dmg", 1000);
		}

		TIMER_Set(NPCS.NPC, "attacking", NPCS.NPC->client->ps.legsTimer + Q_flrand(0.0f, 1.0f) * 200);
	}

	// Need to do delayed damage since the attack animations encapsulate multiple mini-attacks

	if (TIMER_Done2(NPCS.NPC, "attack_dmg", qtrue))
	{
		vec3_t shake_pos;
		switch (NPCS.NPC->client->ps.legsAnim)
		{
		case BOTH_MELEE1:
			Rancor_Smash();
			G_GetBoltPosition(NPCS.NPC, NPCS.NPC->client->renderInfo.handLBolt, shake_pos, 0);
			//G_ScreenShake(shake_pos, NULL, 2.0f, 1000, qfalse);
			CGCam_BlockShakeMP(shake_pos, NULL, 0.25f, 100);
			break;
		case BOTH_MELEE2:
			Rancor_Bite();
			TIMER_Set(NPCS.NPC, "attack_dmg2", 450);
			//G_ScreenShake(shake_pos, NULL, 0.5f, 1000, qfalse);
			CGCam_BlockShakeMP(shake_pos, NULL, 0.25f, 100);
			break;
		case BOTH_ATTACK1:
			if (NPCS.NPC->count == 1 && NPCS.NPC->activator)
			{
				G_Damage(NPCS.NPC->activator, NPCS.NPC, NPCS.NPC, vec3_origin, NPCS.NPC->activator->r.currentOrigin, Q_irand(50, 80), DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK, MOD_MELEE);

				if (NPCS.NPC->activator->health <= 0)
				{
					//killed him
					//make it look like we bit his head off
					G_Dismember(NPCS.NPC->activator, NPCS.NPC, NPCS.NPC->activator->r.currentOrigin, G2_MODELPART_HEAD);
					NPCS.NPC->activator->client->ps.forceHandExtend = HANDEXTEND_NONE;
					NPCS.NPC->activator->client->ps.forceHandExtendTime = 0;
					NPC_SetAnim(NPCS.NPC->activator, SETANIM_BOTH, BOTH_SWIM_IDLE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				}
				G_Sound(NPCS.NPC->activator, CHAN_AUTO, G_SoundIndex("sound/chars/rancor/chomp.wav"));
			}
			break;
		case BOTH_ATTACK2:
		case BOTH_ATTACK10:
		case BOTH_ATTACK11:
			//try to grab
			Rancor_Swing(NPCS.NPC->client->renderInfo.handRBolt, qtrue);
			G_Sound(NPCS.NPC->activator, CHAN_AUTO, G_SoundIndex(va("sound/chars/rancor/swipe%d.wav", Q_irand(1, 3))));
			break;
		case BOTH_ATTACK3:
			if (NPCS.NPC->count == 1 && NPCS.NPC->activator)
			{
				//cut in half
				if (NPCS.NPC->activator->client)
				{
					G_Dismember(NPCS.NPC->activator, NPCS.NPC, NPCS.NPC->activator->r.currentOrigin, G2_MODELPART_WAIST);
				}
				//KILL
				G_Damage(NPCS.NPC->activator, NPCS.NPC, NPCS.NPC, vec3_origin, NPCS.NPC->activator->r.currentOrigin, NPCS.NPC->enemy->health + 9999, DAMAGE_NO_PROTECTION | DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK | DAMAGE_NO_HIT_LOC, MOD_MELEE);

				if (NPCS.NPC->activator->client)
				{
					NPCS.NPC->activator->client->ps.forceHandExtend = HANDEXTEND_NONE;
					NPCS.NPC->activator->client->ps.forceHandExtendTime = 0;
					NPC_SetAnim(NPCS.NPC->activator, SETANIM_BOTH, BOTH_SWIM_IDLE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				}
				TIMER_Set(NPCS.NPC, "attack_dmg2", 1350);
				G_Sound(NPCS.NPC->activator, CHAN_AUTO, G_SoundIndex("sound/chars/rancor/swipehit.wav"));
				G_AddEvent(NPCS.NPC->activator, EV_JUMP, NPCS.NPC->activator->health);
			}
			break;
		default:;
		}
	}
	else if (TIMER_Done2(NPCS.NPC, "attack_dmg2", qtrue))
	{
		switch (NPCS.NPC->client->ps.legsAnim)
		{
		case BOTH_MELEE1:
			break;
		case BOTH_MELEE2:
			Rancor_Bite();
			break;
		case BOTH_ATTACK1:
			break;
		case BOTH_ATTACK2:
			break;
		case BOTH_ATTACK3:
			if (NPCS.NPC->count == 1 && NPCS.NPC->activator)
			{
				//swallow victim
				G_Sound(NPCS.NPC->activator, CHAN_AUTO, G_SoundIndex("sound/chars/rancor/chomp.wav"));
				//FIXME: sometimes end up with a live one in our mouths?
				//just make sure they're dead
				if (NPCS.NPC->activator->health > 0)
				{
					//cut in half
					G_Dismember(NPCS.NPC->activator, NPCS.NPC, NPCS.NPC->activator->r.currentOrigin, G2_MODELPART_WAIST);
					//KILL
					G_Damage(NPCS.NPC->activator, NPCS.NPC, NPCS.NPC, vec3_origin, NPCS.NPC->activator->r.currentOrigin, NPCS.NPC->enemy->health + 9999, DAMAGE_NO_PROTECTION | DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK | DAMAGE_NO_HIT_LOC, MOD_MELEE);
					NPCS.NPC->activator->client->ps.forceHandExtend = HANDEXTEND_NONE;
					NPCS.NPC->activator->client->ps.forceHandExtendTime = 0;
					NPC_SetAnim(NPCS.NPC->activator, SETANIM_BOTH, BOTH_SWIM_IDLE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					G_AddEvent(NPCS.NPC->activator, EV_JUMP, NPCS.NPC->activator->health);
				}
				if (NPCS.NPC->activator->client)
				{
					NPCS.NPC->activator->client->ps.eFlags |= EF_NODRAW;
				}
				NPCS.NPC->count = 2;
				TIMER_Set(NPCS.NPC, "clearGrabbed", 2600);
			}
			break;
		default:;
		}
	}

	// Just using this to remove the attacking flag at the right time
	TIMER_Done2(NPCS.NPC, "attacking", qtrue);
}
#endif

//----------------------------------
#ifdef SP_GAME
static void Rancor_Combat(void)
{
	if (NPCS.NPC->count)
	{
		//holding my enemy
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
		if (TIMER_Done2(NPCS.NPC, "takingPain", qtrue))
		{
			NPCS.NPCInfo->localState = LSTATE_CLEAR;
		}
		else if (NPCS.NPC->spawnflags & SPF_RANCOR_FASTKILL
			&& NPCS.NPC->activator
			&& NPCS.NPC->activator->s.number >= MAX_CLIENTS)
		{
			Rancor_Attack(0, qfalse, qfalse);
		}
		else if (NPCS.NPC->useDebounceTime >= level.time
			&& NPCS.NPC->activator)
		{
			//just sniffing the guy
			if (NPCS.NPC->useDebounceTime <= level.time + 100
				&& NPCS.NPC->client->ps.legsAnim != BOTH_HOLD_DROP)
			{
				//just about done, drop him
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_HOLD_DROP, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				TIMER_Set(NPCS.NPC, "attacking",
					NPCS.NPC->client->ps.SHR_legsAnimTimer + Q_irand(500, 1000) * (3 - SHR_SPSKILL));
			}
		}
		else
		{
			if (!NPCS.NPC->useDebounceTime
				&& NPCS.NPC->activator
				&& NPCS.NPC->activator->s.number < MAX_CLIENTS)
			{
				//first time I pick the player, just sniff them
				if (TIMER_Done(NPCS.NPC, "attacking"))
				{
					//ready to attack
					NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_HOLD_SNIFF, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					NPCS.NPC->useDebounceTime = level.time + NPCS.NPC->client->ps.SHR_legsAnimTimer + Q_irand(500, 2000);
				}
			}
			else
			{
				Rancor_Attack(0, qfalse, qfalse);
			}
		}
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	NPCS.NPCInfo->goalRadius = NPCS.NPC->SHR_maxs[0] + MAX_DISTANCE * NPCS.NPC->s.modelScale[0]; // just get us within combat range

	// If we cannot see our target or we have somewhere to go, then do that
	if (!SHR_ClearLOSEnt(NPCS.NPC->enemy) || UpdateGoal())
	{
		NPCS.NPCInfo->combatMove = qtrue;
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;

		Rancor_Move();
		return;
	}

	NPCS.NPCInfo->enemyLastSeenTime = level.time;
	// Sometimes I have problems with facing the enemy I'm attacking, so force the issue so I don't look dumb
	NPC_FaceEnemy(qtrue);

	const float distance = Distance(NPCS.NPC->SHR_currentOrigin, NPCS.NPC->enemy->SHR_currentOrigin);

	auto advance = distance > NPCS.NPC->SHR_maxs[0] + MIN_DISTANCE * NPCS.NPC->s.modelScale[0] ? qtrue : qfalse;
	qboolean do_charge = qfalse;

	if (advance)
	{
		//have to get closer
		if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT
			&& (!NPCS.NPC->enemy || !NPCS.NPC->enemy->client))
		{
			//don't do breath attack vs. bbrushes
		}
		else
		{
			vec3_t yawOnlyAngles = { 0, NPCS.NPC->SHR_currentAngles[YAW], 0 };
			if (NPCS.NPC->enemy->health > 0
				&& fabs(distance - 250.0f * NPCS.NPC->s.modelScale[0]) <= 80.0f * NPCS.NPC->s.modelScale[0]
				&& SHR_InFOVSpot(NPCS.NPC->enemy->SHR_currentOrigin, NPCS.NPC->SHR_currentOrigin, yawOnlyAngles, 30, 30))
			{
				int chance = 9;
				if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT)
				{
					//higher chance of doing breath attack
					chance = 5 - SHR_SPSKILL;
				}
				if (!Q_irand(0, chance))
				{
					//go for the charge
					do_charge = qtrue;
					advance = qfalse;
				}
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
			Rancor_Move();
		}
	}
	else
	{
		Rancor_Attack(distance, do_charge, qfalse);
	}
}
#else // MP keeps its own version of this function
static void Rancor_Combat(void)
{
	if (NPCS.NPC->count)
	{
		//holding my enemy
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
		if (TIMER_Done2(NPCS.NPC, "takingPain", qtrue))
		{
			NPCS.NPCInfo->localState = LSTATE_CLEAR;
		}
		else if (NPCS.NPC->r.svFlags & SVF_BOT)
		{
			Rancor_Attack(0, qfalse, qfalse);
		}
		else if (NPCS.NPC->useDebounceTime >= level.time
			&& NPCS.NPC->activator)
		{
			//just sniffing the guy
			//if (NPCS.NPC->useDebounceTime <= level.time + 100
			//	&& NPCS.NPC->client->ps.legsAnim != BOTH_HOLD_DROP)
			//{
			//	//just about done, drop him
			//	NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_HOLD_DROP, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			//	TIMER_Set(NPCS.NPC, "attacking", NPCS.NPC->client->ps.legsTimer + Q_irand(500, 1000) * (3 - g_npcspskill.integer));
			//}
		}
		else
		{
			if (!NPCS.NPC->useDebounceTime
				&& NPCS.NPC->activator
				&& NPCS.NPC->activator->s.number < MAX_CLIENTS)
			{
				//first time I pick the player, just sniff them
				if (TIMER_Done(NPCS.NPC, "attacking"))
				{
					//ready to attack
					NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_HOLD_SNIFF, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					G_Sound(NPCS.NPC, CHAN_AUTO, G_SoundIndex(va("sound/chars/rancor/snort_%d.wav", Q_irand(1, 3))));
					NPCS.NPC->useDebounceTime = level.time + NPCS.NPC->client->ps.legsTimer + Q_irand(500, 2000);
				}
			}
			else
			{
				Rancor_Attack(0, qfalse, qfalse);
			}
		}
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	NPCS.NPCInfo->goalRadius = NPCS.NPC->maxs[0] + MAX_DISTANCE * MIN_DISTANCE; // just get us within combat range
	//NPCS.NPCInfo->goalRadius = MIN_DISTANCE; //MAX_DISTANCE;	// just get us within combat range

	// If we cannot see our target or we have somewhere to go, then do that
	if (!NPC_ClearLOS4(NPCS.NPC->enemy) || UpdateGoal())
	{
		NPCS.NPCInfo->combatMove = qtrue;
		NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;

		Rancor_Move();
		return;
	}

	NPCS.NPCInfo->enemyLastSeenTime = level.time;

	// Sometimes I have problems with facing the enemy I'm attacking, so force the issue so I don't look dumb
	NPC_FaceEnemy(qtrue);

	const float distance = Distance(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin);
	qboolean advance = distance > NPCS.NPC->r.maxs[0] + MIN_DISTANCE ? qtrue : qfalse;
	qboolean do_charge = qfalse;

	if (advance)
	{
		//have to get closer
		vec3_t yawOnlyAngles;
		VectorSet(yawOnlyAngles, 0, NPCS.NPC->r.currentAngles[YAW], 0);
		if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT && !NPCS.NPC->enemy->client)
		{
			//don't do breath attack vs. bbrushes
		}
		else if (NPCS.NPC->enemy->health > 0
			&& fabs(distance - 250) <= 80
			&& InFOV3(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, yawOnlyAngles, 30, 30))
		{
			//higher chance of doing breath attack (SP)
			const int chance = NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT ? 5 - g_npcspskill.integer : 9;
			if (!Q_irand(0, chance))
			{
				//go for the charge
				do_charge = qtrue;
				advance = qfalse;
			}
		}
	}

	if (advance || NPCS.NPCInfo->localState == LSTATE_WAITING && TIMER_Done(NPCS.NPC, "attacking"))
		// waiting monsters can't attack
	{
		if (TIMER_Done2(NPCS.NPC, "takingPain", qtrue))
		{
			NPCS.NPCInfo->localState = LSTATE_CLEAR;
		}
		else
		{
			Rancor_Move();
		}
	}
	else
	{
		Rancor_Attack(distance, do_charge, qfalse);
	}
}
#endif

/*
-------------------------
NPC_Rancor_Pain
-------------------------
*/
void NPC_Rancor_Pain(SHR_PAIN_PARAMS)
{
	// Track whether attacker is another Rancor
	qboolean hit_by_rancor = qfalse;

	// If NPC struct exists and pain is ignored, do nothing
	if (self->NPC && self->NPC->ignorePain == qtrue)
	{
		return;
	}

	// Breath attack cannot be interrupted
	if (!TIMER_Done(self, "breathAttack"))
	{
		return;
	}

	// Clear confusion timer
	TIMER_Remove(self, "confusionTime");

	// Check if attacker is a Rancor
	if (other && other->client && other->client->NPC_class == CLASS_RANCOR)
	{
		hit_by_rancor = qtrue;
	}

	// Evaluate switching enemies
	if (other &&
		other->inuse == qtrue &&
		other != self->enemy &&
		!(other->flags & FL_NOTARGET))
	{
		if (!self->count)
		{
			// Conditions for switching enemy
			if ((!other->s.number && !Q_irand(0, 3)) ||
				!self->enemy ||
				(self->enemy && self->enemy->health == 0) ||
				(self->enemy && self->enemy->client &&
					self->enemy->client->NPC_class == CLASS_RANCOR) ||
				(!Q_irand(0, 4) &&
					DistanceSquared(other->SHR_currentOrigin, self->SHR_currentOrigin) <
					(self->enemy ? DistanceSquared(self->enemy->SHR_currentOrigin,
						self->SHR_currentOrigin) : 99999999.0f)))
			{
				// Switch enemy
				self->lastEnemy = self->enemy;
				G_SetEnemy(self, other);

				if (self->enemy != self->lastEnemy)
				{
					self->useDebounceTime = 0;
				}

				TIMER_Set(self, "lookForNewEnemy", Q_irand(5000, 15000));

				if (hit_by_rancor == qtrue)
				{
					TIMER_Set(self, "rancorInfight", Q_irand(2000, 5000));
				}
			}
		}
	}

	// Conditions for playing pain animation
	if ((hit_by_rancor == qtrue ||
		(self->count == 1 && self->activator && !Q_irand(0, 4)) ||
		(Q_irand(0, 200) < damage)) &&
		self->client->ps.legsAnim != BOTH_STAND1TO2 &&
		TIMER_Done(self, "takingPain"))
	{
		// If roaring, skip pain
		if (!Rancor_CheckRoar(self))
		{
			// Cannot interrupt major attack animations
			if (self->client->ps.legsAnim != BOTH_MELEE1 &&
				self->client->ps.legsAnim != BOTH_MELEE2 &&
				self->client->ps.legsAnim != BOTH_ATTACK2 &&
				self->client->ps.legsAnim != BOTH_ATTACK10 &&
				self->client->ps.legsAnim != BOTH_ATTACK11)
			{
				// Only react to pain if healthy or hit by another Rancor
				if (self->health > 100 || hit_by_rancor == qtrue)
				{
					TIMER_Remove(self, "attacking");

					// FIX: Guard self->NPC before dereferencing
					if (self->NPC != NULL)
					{
						VectorCopy(self->NPC->lastPathAngles, self->s.angles);
					}
					else
					{
						Com_Printf(S_COLOR_YELLOW
							"NPC_Rancor_Pain: self->NPC was NULL, "
							"skipping lastPathAngles copy\n");
					}

					// Choose pain animation based on whether holding a victim
					if (self->count == 1)
					{
						NPC_SetAnim(self, SETANIM_BOTH, BOTH_PAIN2,
							SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					}
					else
					{
						NPC_SetAnim(self, SETANIM_BOTH, BOTH_PAIN1,
							SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					}

					TIMER_Set(self, "takingPain",
						self->client->ps.SHR_legsAnimTimer +
						Q_irand(0, 500 * (2 - SHR_SPSKILL)));

					// Set local state if NPC struct exists
					if (self->NPC != NULL)
					{
						self->NPC->localState = LSTATE_WAITING;
					}
				}
			}
		}
	}
}

static void Rancor_CheckDropVictim(void)
{
	if (NPCS.NPC->spawnflags & SPF_RANCOR_FASTKILL
		&& NPCS.NPC->activator->s.number >= MAX_CLIENTS)
	{
		return;
	}
	const vec3_t mins = { NPCS.NPC->activator->SHR_mins[0] - 1, NPCS.NPC->activator->SHR_mins[1] - 1, 0 };
	const vec3_t maxs = { NPCS.NPC->activator->SHR_maxs[0] + 1, NPCS.NPC->activator->SHR_maxs[1] + 1, 1 };
	const vec3_t start = {
		NPCS.NPC->activator->SHR_currentOrigin[0], NPCS.NPC->activator->SHR_currentOrigin[1], NPCS.NPC->activator->SHR_absmin[2]
	};
	const vec3_t end = {
		NPCS.NPC->activator->SHR_currentOrigin[0], NPCS.NPC->activator->SHR_currentOrigin[1], NPCS.NPC->activator->SHR_absmax[2] - 1
	};
	trace_t trace;
	SHR_Trace(&trace, start, mins, maxs, end, NPCS.NPC->activator->s.number, NPCS.NPC->activator->clipmask);
	if (!trace.allsolid && !trace.startsolid && trace.fraction >= 1.0f)
	{
		Rancor_DropVictim(NPCS.NPC);
	}
}

#ifdef SP_GAME
static qboolean Rancor_AttackBBrush(void)
{
	trace_t trace;
	vec3_t center;
	vec3_t dir2_brush;
	const float check_dist = 64.0f; //32.0f;

	if (VectorCompare(NPCS.NPCInfo->blockedEntity->s.origin, vec3_origin))
	{
		//no origin brush, calc center
		VectorAdd(NPCS.NPCInfo->blockedEntity->SHR_mins, NPCS.NPCInfo->blockedEntity->SHR_maxs, center);
		VectorScale(center, 0.5f, center);
	}
	else
	{
		VectorCopy(NPCS.NPCInfo->blockedEntity->s.origin, center);
	}
	if (NAVDEBUG_showCollision)
	{
		CG_DrawEdge(NPCS.NPC->SHR_currentOrigin, center, EDGE_IMPACT_POSSIBLE);
	}
	center[2] = NPCS.NPC->SHR_currentOrigin[2]; //we can't fly, so let's ignore z diff
	NPC_FacePosition(center, qfalse);
	//see if we're close to it
	VectorSubtract(center, NPCS.NPC->SHR_currentOrigin, dir2_brush);
	const float brush_size = ((NPCS.NPCInfo->blockedEntity->SHR_maxs[0] - NPCS.NPCInfo->blockedEntity->SHR_mins[0]) * 0.5f + (NPCS.NPCInfo->blockedEntity->SHR_maxs[1] - NPCS.NPCInfo->blockedEntity->SHR_mins[1]) * 0.5f) * 0.5f;
	const float dist2_brush = VectorNormalize(dir2_brush) - NPCS.NPC->SHR_maxs[0] - brush_size;
	if (dist2_brush < MIN_DISTANCE * NPCS.NPC->s.modelScale[0])
	{
		//close enough to just hit it
		trace.fraction = 0.0f;
		trace.entityNum = NPCS.NPCInfo->blockedEntity->s.number;
	}
	else
	{
		vec3_t end;
		VectorMA(NPCS.NPC->SHR_currentOrigin, check_dist, dir2_brush, end);
		SHR_Trace(&trace, NPCS.NPC->SHR_currentOrigin, NPCS.NPC->SHR_mins, NPCS.NPC->SHR_maxs, end, NPCS.NPC->s.number, NPCS.NPC->clipmask);
		if (trace.allsolid || trace.startsolid)
		{
			//wtf?
			NPCS.NPCInfo->blockedEntity = NULL;
			return qfalse;
		}
	}
	if (trace.fraction >= 1.0f //too far away
		|| trace.entityNum != NPCS.NPCInfo->blockedEntity->s.number) //OR blocked by something else
	{
		//keep moving towards it
		NPCS.ucmd.buttons &= ~BUTTON_WALKING; // Unset from MoveToGoal()
		STEER::Activate(NPCS.NPC);
		STEER::Seek(NPCS.NPC, center);
		STEER::AvoidCollisions(NPCS.NPC, NULL);
		STEER::DeActivate(NPCS.NPC, &NPCS.ucmd);
	}
	else if (trace.entityNum == NPCS.NPCInfo->blockedEntity->s.number)
	{
		//close enough, smash it!
		Rancor_Attack(trace.fraction * check_dist, qfalse, qtrue); //FIXME: maybe charge at it, smash through?
		TIMER_Remove(NPCS.NPC, "attackDebounce"); //don't wait on these
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
	}
	else
	{
		//Com_Printf( S_COLOR_RED"RANCOR cannot reach intended breakable %s, blocked by %s\n", NPC->blockedEntity->targetname, g_entities[trace.entityNum].classname );
		if (SHR_EntIsBreakable(trace.entityNum, NPCS.NPC))
		{
			//oh, well, smash that, then
			//G_SetEnemy( NPC, &g_entities[trace.entityNum] );
			gentity_t* prevblockedEnt = NPCS.NPCInfo->blockedEntity;
			NPCS.NPCInfo->blockedEntity = &g_entities[trace.entityNum];
			Rancor_Attack(trace.fraction * check_dist, qfalse, qtrue); //FIXME: maybe charge at it, smash through?
			TIMER_Remove(NPCS.NPC, "attackDebounce"); //don't wait on these
			NPCS.NPCInfo->enemyLastSeenTime = level.time;
			NPCS.NPCInfo->blockedEntity = prevblockedEnt;
		}
		else
		{
			NPCS.NPCInfo->blockedEntity = NULL;
			return qfalse;
		}
	}
	return qtrue;
}
#else // MP keeps its own version of this function
static qboolean Rancor_AttackBBrush(void)
{
	trace_t trace;
	vec3_t center, dir2Brush;
	gentity_t* brush = NPCS.NPCInfo->blockedEntity;
	const float checkDist = 64.0f;

	if (VectorCompare(brush->s.origin, vec3_origin))
	{
		//no origin brush, calc center
		VectorAdd(brush->r.absmin, brush->r.absmax, center);
		VectorScale(center, 0.5f, center);
	}
	else
	{
		VectorCopy(brush->s.origin, center);
	}
	center[2] = NPCS.NPC->r.currentOrigin[2]; //we can't fly, so let's ignore z diff
	NPC_FacePosition(center, qfalse);
	//see if we're close to it
	VectorSubtract(center, NPCS.NPC->r.currentOrigin, dir2Brush);
	const float brushSize = ((brush->r.absmax[0] - brush->r.absmin[0]) * 0.5f
		+ (brush->r.absmax[1] - brush->r.absmin[1]) * 0.5f) * 0.5f;
	const float dist2Brush = VectorNormalize(dir2Brush) - NPCS.NPC->r.maxs[0] - brushSize;
	if (dist2Brush < MIN_DISTANCE)
	{
		//close enough to just hit it
		trace.fraction = 0.0f;
		trace.entityNum = brush->s.number;
	}
	else
	{
		vec3_t end;
		VectorMA(NPCS.NPC->r.currentOrigin, checkDist, dir2Brush, end);
		trap->Trace(&trace, NPCS.NPC->r.currentOrigin, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, end, NPCS.NPC->s.number,
			NPCS.NPC->clipmask, qfalse, 0, 0);
		if (trace.allsolid || trace.startsolid)
		{
			//wtf?
			NPCS.NPCInfo->blockedEntity = NULL;
			return qfalse;
		}
	}
	if (trace.fraction >= 1.0f //too far away
		|| trace.entityNum != brush->s.number && !SHR_EntIsBreakable(trace.entityNum, NPCS.NPC)) //OR blocked by something else
	{
		if (trace.fraction >= 1.0f)
		{
			//keep moving towards it
			Rancor_MoveStraightAt(center);
			return qtrue;
		}
		NPCS.NPCInfo->blockedEntity = NULL;
		return qfalse;
	}
	if (trace.entityNum == brush->s.number)
	{
		//close enough, smash it!
		Rancor_Attack(trace.fraction * checkDist, qfalse, qtrue);
	}
	else
	{
		//oh, well, smash that, then
		NPCS.NPCInfo->blockedEntity = &g_entities[trace.entityNum];
		Rancor_Attack(trace.fraction * checkDist, qfalse, qtrue);
		NPCS.NPCInfo->blockedEntity = brush;
	}
	TIMER_Remove(NPCS.NPC, "attackDebounce"); //don't wait on these
	NPCS.NPCInfo->enemyLastSeenTime = level.time;
	return qtrue;
}
#endif

#ifdef SP_GAME
static void Rancor_FireBreathAttack(void)
{
	const int damage = Q_irand(10, 15);
	trace_t tr;
	mdxaBone_t boltMatrix;
	vec3_t start, end, dir;
	const vec3_t trace_maxs = { 4, 4, 4 };
	const vec3_t trace_mins = { -4, -4, -4 };
	const vec3_t ranc_angles = { 0, NPCS.NPC->client->ps.viewangles[YAW], 0 };

	SHR_GetBoltMatrix(NPCS.NPC, NPCS.NPC->gutBolt, boltMatrix, ranc_angles, NPCS.NPC->SHR_currentOrigin);

	SHR_GiveMeVector(boltMatrix, ORIGIN, start);
	SHR_GiveMeVector(boltMatrix, NEGATIVE_Z, dir);
	VectorMA(start, 512, dir, end);

	SHR_Trace(&tr, start, trace_mins, trace_maxs, end, NPCS.NPC->s.number, MASK_SHOT);

	gentity_t* traceEnt = &g_entities[tr.entityNum];
	if (tr.entityNum < ENTITYNUM_WORLD
		&& traceEnt->takedamage
		&& traceEnt->client)
	{
		//breath attack only does damage to living things
		G_Damage(traceEnt, NPCS.NPC, NPCS.NPC, dir, tr.endpos, damage * 2,
			DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK | DAMAGE_NO_HIT_LOC | DAMAGE_IGNORE_TEAM, MOD_LAVA, HL_NONE);
	}
	if (tr.fraction < 1.0f)
	{
		//hit something, do radius damage
		G_RadiusDamage(tr.endpos, NPCS.NPC, damage, 250, NPCS.NPC, MOD_LAVA);
	}
}
#else // MP keeps its own version of this function
static void Rancor_FireBreathAttack(void)
{
	const int damage = Q_irand(10, 15);
	trace_t tr;
	mdxaBone_t boltMatrix;
	vec3_t start, end, dir;
	const vec3_t traceMaxs = { 4, 4, 4 };
	const vec3_t traceMins = { -4, -4, -4 };
	const vec3_t rancAngles = { 0, NPCS.NPC->client->ps.viewangles[YAW], 0 };
	const int mouthBolt = trap->G2API_AddBolt(NPCS.NPC->ghoul2, 0, "*head_mouth");

	if (TIMER_Done(NPCS.NPC, "breathFx"))
	{
		G_PlayBoltedEffect(G_EffectIndex("mrancor/breath"), NPCS.NPC, "*head_mouth");
		TIMER_Set(NPCS.NPC, "breathFx", 280);
	}
	if (mouthBolt < 0
		|| !trap->G2API_GetBoltMatrix(NPCS.NPC->ghoul2, 0, mouthBolt, &boltMatrix, rancAngles, NPCS.NPC->r.currentOrigin,
			level.time, NULL, NPCS.NPC->modelScale))
	{
		return;
	}
	BG_GiveMeVectorFromMatrix(&boltMatrix, ORIGIN, start);
	BG_GiveMeVectorFromMatrix(&boltMatrix, NEGATIVE_Z, dir);
	VectorMA(start, 512, dir, end);

	trap->Trace(&tr, start, traceMins, traceMaxs, end, NPCS.NPC->s.number, MASK_SHOT, qfalse, 0, 0);

	if (tr.entityNum < ENTITYNUM_WORLD
		&& g_entities[tr.entityNum].takedamage
		&& g_entities[tr.entityNum].client)
	{
		//breath attack only does damage to living things
		G_Damage(&g_entities[tr.entityNum], NPCS.NPC, NPCS.NPC, dir, tr.endpos, damage * 2,
			DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK | DAMAGE_NO_HIT_LOC | DAMAGE_IGNORE_TEAM, MOD_LAVA);
	}
	if (tr.fraction < 1.0f)
	{
		//hit something, do radius damage
		g_radius_damage(tr.endpos, NPCS.NPC, damage, 250, NPCS.NPC, NPCS.NPC, MOD_LAVA);
	}
}
#endif

#ifdef SP_GAME
static void Rancor_CheckAnimDamage(void)
{
	if (NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK2
		|| NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK10
		|| NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK11)
	{
		if (NPCS.NPC->client->ps.SHR_legsAnimTimer >= 1200 && NPCS.NPC->client->ps.SHR_legsAnimTimer <= 1350)
		{
			if (Q_irand(0, 2))
			{
				Rancor_Swing(NPCS.NPC->handRBolt, qfalse);
			}
			else
			{
				Rancor_Swing(NPCS.NPC->handRBolt, qtrue);
			}
		}
		else if (NPCS.NPC->client->ps.SHR_legsAnimTimer >= 1100 && NPCS.NPC->client->ps.SHR_legsAnimTimer <= 1550)
		{
			Rancor_Swing(NPCS.NPC->handRBolt, qtrue);
		}
	}
	else if (NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK5)
	{
		if (NPCS.NPC->client->ps.SHR_legsAnimTimer >= 750 && NPCS.NPC->client->ps.SHR_legsAnimTimer <= 1300)
		{
			Rancor_Swing(NPCS.NPC->handLBolt, qfalse);
		}
		else if (NPCS.NPC->client->ps.SHR_legsAnimTimer >= 1700 && NPCS.NPC->client->ps.SHR_legsAnimTimer <= 2300)
		{
			Rancor_Swing(NPCS.NPC->handRBolt, qfalse);
		}
	}
}
#else // MP keeps its own version of this function
static void Rancor_CheckAnimDamage()
{
	if (NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK2
		|| NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK10
		|| NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK11)
	{
		if (NPCS.NPC->client->ps.legsTimer >= 1200 && NPCS.NPC->client->ps.legsTimer <= 1350)
		{
			if (Q_irand(0, 2))
			{
				Rancor_Swing(NPCS.NPC->client->renderInfo.handRBolt, qfalse);
			}
			else
			{
				Rancor_Swing(NPCS.NPC->client->renderInfo.handRBolt, qtrue);
			}
		}
		else if (NPCS.NPC->client->ps.legsTimer >= 1100 && NPCS.NPC->client->ps.legsTimer <= 1550)
		{
			Rancor_Swing(NPCS.NPC->client->renderInfo.handRBolt, qtrue);
		}
	}
	else if (NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK5)
	{
		if (NPCS.NPC->client->ps.legsTimer >= 750 && NPCS.NPC->client->ps.legsTimer <= 1300)
		{
			Rancor_Swing(NPCS.NPC->client->renderInfo.handLBolt, qfalse);
		}
		else if (NPCS.NPC->client->ps.legsTimer >= 1700 && NPCS.NPC->client->ps.legsTimer <= 2300)
		{
			Rancor_Swing(NPCS.NPC->client->renderInfo.handRBolt, qfalse);
		}
	}
}
#endif

/*
-------------------------
NPC_BSRancor_Default
-------------------------
*/
#ifdef SP_GAME
void NPC_BSRancor_Default(void)
{
	AddSightEvent(NPCS.NPC, NPCS.NPC->SHR_currentOrigin, 1024, AEL_DANGER_GREAT, 50);

	if (NPCS.NPCInfo->blockedEntity && TIMER_Done(NPCS.NPC, "blockedEntityIgnore"))
	{
		if (!TIMER_Exists(NPCS.NPC, "blockedEntityTimeOut"))
		{
			TIMER_Set(NPCS.NPC, "blockedEntityTimeOut", 5000);
		}
		else if (TIMER_Done(NPCS.NPC, "blockedEntityTimeOut"))
		{
			TIMER_Remove(NPCS.NPC, "blockedEntityTimeOut");
			TIMER_Set(NPCS.NPC, "blockedEntityIgnore", 25000);
			NPCS.NPCInfo->blockedEntity = NULL;
		}
	}
	else
	{
		TIMER_Remove(NPCS.NPC, "blockedEntityTimeOut");
		TIMER_Remove(NPCS.NPC, "blockedEntityIgnore");
	}

	Rancor_CheckAnimDamage();

	if (!TIMER_Done(NPCS.NPC, "breathAttack"))
	{
		//doing breath attack, just do damage
		Rancor_FireBreathAttack();
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}
	if (NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK4
		|| NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK6
		|| NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK7)
	{
		G_StopEffect(G_EffectIndex("mrancor/breath"), NPCS.NPC->playerModel, NPCS.NPC->gutBolt, NPCS.NPC->s.number);
		NPCS.NPC->s.loopSound = 0;
	}

	if (TIMER_Done2(NPCS.NPC, "clearGrabbed", qtrue))
	{
		Rancor_DropVictim(NPCS.NPC);
	}
	else if ((NPCS.NPC->client->ps.legsAnim == BOTH_PAIN2 || NPCS.NPC->client->ps.legsAnim == BOTH_HOLD_DROP)
		&& NPCS.NPC->count == 1
		&& NPCS.NPC->activator)
	{
		Rancor_CheckDropVictim();
	}
	if (!TIMER_Done(NPCS.NPC, "rageTime"))
	{
		//do nothing but roar first time we see an enemy
		AddSoundEvent(NPCS.NPC, NPCS.NPC->SHR_currentOrigin, 4096, AEL_DANGER_GREAT, qfalse, qfalse);
		NPC_FaceEnemy(qtrue);
		return;
	}

	if (NPCS.NPCInfo->localState == LSTATE_WAITING
		&& TIMER_Done2(NPCS.NPC, "takingPain", qtrue))
	{
		//was not doing anything because we were taking pain, but pain is done now, so clear it...
		NPCS.NPCInfo->localState = LSTATE_CLEAR;
	}

	if (!TIMER_Done(NPCS.NPC, "confusionTime"))
	{
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	if (NPCS.NPC->enemy)
	{
		if (NPCS.NPC->enemy->client //enemy is a client
			&& (NPCS.NPC->enemy->client->NPC_class == CLASS_UGNAUGHT || NPCS.NPC->enemy->client->NPC_class == CLASS_JAWA)
			//enemy is a lowly jawa or ugnaught
			&& NPCS.NPC->enemy->enemy != NPCS.NPC //enemy's enemy is not me
			&& (!NPCS.NPC->enemy->enemy || !NPCS.NPC->enemy->enemy->client || NPCS.NPC->enemy->enemy->client->NPC_class !=
				CLASS_RANCOR)) //enemy's enemy is not a client or is not a rancor (which is as scary as me anyway)
		{
			//they should be scared of ME and no-one else
			G_SetEnemy(NPCS.NPC->enemy, NPCS.NPC);
		}
		if (TIMER_Done(NPCS.NPC, "angrynoise"))
		{
			SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, va("sound/chars/rancor/anger%d.wav", Q_irand(1, 3)));

			TIMER_Set(NPCS.NPC, "angrynoise", Q_irand(5000, 10000));
		}
		else
		{
			AddSoundEvent(NPCS.NPC, NPCS.NPC->SHR_currentOrigin, 1024, AEL_DANGER_GREAT, qfalse, qfalse);
		}
		if (NPCS.NPC->count == 2 && NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK3)
		{
			//we're still chewing our enemy up
			NPC_UpdateAngles(qtrue, qtrue);
			return;
		}
		//else, if he's in our hand, we eat, else if he's on the ground, we keep attacking his dead body for a while
		if (NPCS.NPC->enemy->client && NPCS.NPC->enemy->client->NPC_class == CLASS_RANCOR)
		{
			//got mad at another Rancor, look for a valid enemy
			if (TIMER_Done(NPCS.NPC, "rancorInfight"))
			{
				NPC_CheckEnemyExt(qtrue);
			}
		}
		else if (!NPCS.NPC->count)
		{
			if (NPCS.NPCInfo->blockedEntity)
			{
				//something in our way
				if (!NPCS.NPCInfo->blockedEntity->inuse)
				{
					//was destroyed
					NPCS.NPCInfo->blockedEntity = NULL;
				}
				else
				{
					//a breakable?
					if (SHR_EntIsBreakable(NPCS.NPCInfo->blockedEntity->s.number, NPCS.NPC))
					{
						//breakable brush
						if (!Rancor_AttackBBrush())
						{
							//didn't move inside that func, so call move here...?
							Rancor_Move();
						}
						NPC_UpdateAngles(qtrue, qtrue);
						return;
					}
					//if it's a client and in our way, get mad at it!
					if (NPCS.NPCInfo->blockedEntity != NPCS.NPC->enemy
						&& NPCS.NPCInfo->blockedEntity->client
						&& NPC_ValidEnemy(NPCS.NPCInfo->blockedEntity)
						&& !Q_irand(0, 9))
					{
						G_SetEnemy(NPCS.NPC, NPCS.NPCInfo->blockedEntity);
						//look again in 2-5 secs
						TIMER_Set(NPCS.NPC, "lookForNewEnemy", Q_irand(2000, 5000));
						NPCS.NPCInfo->blockedEntity = NULL;
					}
				}
			}
			if (NPC_ValidEnemy(NPCS.NPC->enemy) == qfalse)
			{
				TIMER_Remove(NPCS.NPC, "lookForNewEnemy"); //make them look again right now
				if (!NPCS.NPC->enemy->inuse
					|| level.time - NPCS.NPC->enemy->s.time > Q_irand(10000, 15000)
					|| NPCS.NPC->spawnflags & SPF_RANCOR_FASTKILL) //don't linger on dead bodies
				{
					//it's been a while since the enemy died, or enemy is completely gone, get bored with him
					if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT
						&& player && player->health >= 0)
					{
						//all else failing, always go after the player
						NPCS.NPC->lastEnemy = NPCS.NPC->enemy;
						G_SetEnemy(NPCS.NPC, player);
						if (NPCS.NPC->enemy != NPCS.NPC->lastEnemy)
						{
							//clear this so that we only sniff the player the first time we pick them up
							NPCS.NPC->useDebounceTime = 0;
						}
					}
					else
					{
						NPCS.NPC->enemy = NULL;
						Rancor_Patrol();
						NPC_UpdateAngles(qtrue, qtrue);
						return;
					}
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
		}
		Rancor_Combat();
		if (TIMER_Done(NPCS.NPC, "attacking")
			&& TIMER_Done(NPCS.NPC, "takingpain")
			&& TIMER_Done(NPCS.NPC, "confusionDebounce")
			&& NPCS.NPCInfo->localState == LSTATE_CLEAR
			&& !NPCS.NPC->count)
		{
			//not busy
			if (!NPCS.ucmd.forwardmove
				&& !NPCS.ucmd.rightmove
				&& VectorCompare(NPCS.NPC->client->ps.moveDir, vec3_origin))
			{
				//not moving
				if (level.time - NPCS.NPCInfo->enemyLastSeenTime > 5000)
				{
					//haven't seen an enemy in a while
					if (!Q_irand(0, 20))
					{
						if (Q_irand(0, 1))
						{
							NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_GUARD_IDLE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						}
						else
						{
							NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_GUARD_LOOKAROUND1,
								SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						}
						TIMER_Set(NPCS.NPC, "confusionTime", NPCS.NPC->client->ps.SHR_legsAnimTimer);
						TIMER_Set(NPCS.NPC, "confusionDebounce", NPCS.NPC->client->ps.SHR_legsAnimTimer + Q_irand(4000, 8000));
					}
				}
			}
		}
	}
	else
	{
		if (TIMER_Done(NPCS.NPC, "idlenoise"))
		{
			SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, va("sound/chars/rancor/snort_%d.wav", Q_irand(1, 4)));

			TIMER_Set(NPCS.NPC, "idlenoise", Q_irand(2000, 4000));
			AddSoundEvent(NPCS.NPC, NPCS.NPC->SHR_currentOrigin, 512, AEL_DANGER, qfalse, qfalse);
		}
		if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
		{
			Rancor_Patrol();
			if (!NPCS.NPC->enemy && NPCS.NPC->wait)
			{
				//we've been mad before and can't find an enemy
				if (NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT
					&& player && player->health >= 0)
				{
					//all else failing, always go after the player
					NPCS.NPC->lastEnemy = NPCS.NPC->enemy;
					G_SetEnemy(NPCS.NPC, player);
					if (NPCS.NPC->enemy != NPCS.NPC->lastEnemy)
					{
						//clear this so that we only sniff the player the first time we pick them up
						NPCS.NPC->useDebounceTime = 0;
					}
				}
			}
		}
		else
		{
			Rancor_Idle();
		}
	}

	NPC_UpdateAngles(qtrue, qtrue);
}
#else // MP keeps its own version of this function
void NPC_BSRancor_Default(void)
{
	AddSightEvent(NPCS.NPC, NPCS.NPC->r.currentOrigin, 1024, AEL_DANGER_GREAT, 50);

	Rancor_Crush();

	NPCS.NPC->client->ps.eFlags2 &= ~(EF2_USE_ALT_ANIM | EF2_GENERIC_NPC_FLAG);
	if (NPCS.NPC->count)
	{
		//holding someone
		NPCS.NPC->client->ps.eFlags2 |= EF2_USE_ALT_ANIM;
		if (NPCS.NPC->count == 2)
		{
			//in my mouth
			NPCS.NPC->client->ps.eFlags2 |= EF2_GENERIC_NPC_FLAG;
		}
	}
	else
	{
		NPCS.NPC->client->ps.eFlags2 &= ~(EF2_USE_ALT_ANIM | EF2_GENERIC_NPC_FLAG);
	}

	if (NPCS.NPCInfo->blockedEntity && TIMER_Done(NPCS.NPC, "blockedEntityIgnore"))
	{
		if (!TIMER_Exists(NPCS.NPC, "blockedEntityTimeOut"))
		{
			TIMER_Set(NPCS.NPC, "blockedEntityTimeOut", 5000);
		}
		else if (TIMER_Done(NPCS.NPC, "blockedEntityTimeOut"))
		{
			TIMER_Remove(NPCS.NPC, "blockedEntityTimeOut");
			TIMER_Set(NPCS.NPC, "blockedEntityIgnore", 25000);
			NPCS.NPCInfo->blockedEntity = NULL;
		}
	}
	else
	{
		TIMER_Remove(NPCS.NPC, "blockedEntityTimeOut");
		TIMER_Remove(NPCS.NPC, "blockedEntityIgnore");
	}

	Rancor_CheckAnimDamage();

	if (!TIMER_Done(NPCS.NPC, "breathAttack"))
	{
		//doing breath attack, just do damage
		Rancor_FireBreathAttack();
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}
	if (NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK4
		|| NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK6
		|| NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK7)
	{
		NPCS.NPC->s.loopSound = 0; // (SP stopped the looping effect here too; MP's runs out by itself)
	}

	if (TIMER_Done2(NPCS.NPC, "clearGrabbed", qtrue))
	{
		Rancor_DropVictim(NPCS.NPC);
	}
	else if (NPCS.NPC->client->ps.legsAnim == BOTH_PAIN2 /*|| NPCS.NPC->client->ps.legsAnim == BOTH_HOLD_DROP*/
		&& NPCS.NPC->count == 1
		&& NPCS.NPC->activator)
	{
		Rancor_CheckDropVictim();
	}
	if (!TIMER_Done(NPCS.NPC, "rageTime"))
	{
		//do nothing but roar first time we see an enemy
		AddSoundEvent(NPCS.NPC, NPCS.NPC->r.currentOrigin, 4094, AEL_DANGER_GREAT, qfalse, qfalse);
		//G_Sound(NPCS.NPC, CHAN_AUTO, G_SoundIndex("sound/chars/rancor/rancor_roar_02.mp3"));
		NPC_FaceEnemy(qtrue);
		return;
	}
	if (!TIMER_Done(NPCS.NPC, "roartime"))
	{
		G_Sound(NPCS.NPC, CHAN_VOICE, G_SoundIndex(va("sound/chars/rancor/rancor_roar_02.mp3")));
	}

	if (NPCS.NPCInfo->localState == LSTATE_WAITING
		&& TIMER_Done2(NPCS.NPC, "takingPain", qtrue))
	{
		//was not doing anything because we were taking pain, but pain is done now, so clear it...
		NPCS.NPCInfo->localState = LSTATE_CLEAR;
	}

	if (!TIMER_Done(NPCS.NPC, "confusionTime"))
	{
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	if (NPCS.NPC->enemy)
	{
		if (TIMER_Done(NPCS.NPC, "angrynoise"))
		{
			G_Sound(NPCS.NPC, CHAN_AUTO, G_SoundIndex(va("sound/chars/rancor/misc/anger%d.wav", Q_irand(1, 3))));

			TIMER_Set(NPCS.NPC, "angrynoise", Q_irand(5000, 10000));
		}
		else
		{
			AddSoundEvent(NPCS.NPC, NPCS.NPC->r.currentOrigin, 1024, AEL_DANGER_GREAT, qfalse, qfalse);
		}
		if (NPCS.NPC->count == 2 && NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK3)
		{
			//we're still chewing our enemy up
			NPC_UpdateAngles(qtrue, qtrue);
			return;
		}
		//else, if he's in our hand, we eat, else if he's on the ground, we keep attacking his dead body for a while
		if (NPCS.NPC->enemy->client && NPCS.NPC->enemy->client->NPC_class == CLASS_RANCOR)
		{
			//got mad at another Rancor, look for a valid enemy
			if (TIMER_Done(NPCS.NPC, "rancorInfight"))
			{
				NPC_CheckEnemyExt(qtrue);
			}
		}
		else if (!NPCS.NPC->count)
		{
			if (NPCS.NPCInfo->blockedEntity)
			{
				//something in our way
				if (!NPCS.NPCInfo->blockedEntity->inuse)
				{
					//was destroyed
					NPCS.NPCInfo->blockedEntity = NULL;
				}
				else
				{
					//a breakable?
					if (SHR_EntIsBreakable(NPCS.NPCInfo->blockedEntity->s.number, NPCS.NPC))
					{
						//breakable brush
						if (!Rancor_AttackBBrush())
						{
							//didn't move inside that func, so call move here...?
							Rancor_Move();
						}
						NPC_UpdateAngles(qtrue, qtrue);
						return;
					}
					//if it's a client and in our way, get mad at it!
					if (NPCS.NPCInfo->blockedEntity != NPCS.NPC->enemy
						&& NPCS.NPCInfo->blockedEntity->client
						&& NPC_ValidEnemy(NPCS.NPCInfo->blockedEntity)
						&& !Q_irand(0, 9))
					{
						G_SetEnemy(NPCS.NPC, NPCS.NPCInfo->blockedEntity);
						//look again in 2-5 secs
						TIMER_Set(NPCS.NPC, "lookForNewEnemy", Q_irand(2000, 5000));
						NPCS.NPCInfo->blockedEntity = NULL;
					}
				}
			}
			if (ValidEnemy(NPCS.NPC->enemy) == qfalse)
			{
				TIMER_Remove(NPCS.NPC, "lookForNewEnemy"); //make them look again right now
				if (!NPCS.NPC->enemy->inuse || level.time - NPCS.NPC->enemy->s.time > Q_irand(10000, 15000))
				{
					//it's been a while since the enemy died, or enemy is completely gone, get bored with him
					gentity_t* player = NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT
						? FindClosestPlayer(NPCS.NPC->r.currentOrigin, NPCS.NPC->client->enemyTeam)
						: NULL;
					if (player && player->health > 0)
					{
						//all else failing, always go after the player
						NPCS.NPC->lastEnemy = NPCS.NPC->enemy;
						G_SetEnemy(NPCS.NPC, player);
						if (NPCS.NPC->enemy != NPCS.NPC->lastEnemy)
						{
							//clear this so that we only sniff the player the first time we pick them up
							NPCS.NPC->useDebounceTime = 0;
						}
					}
					else
					{
						NPCS.NPC->enemy = NULL;
						Rancor_Patrol();
						NPC_UpdateAngles(qtrue, qtrue);
						return;
					}
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
		Rancor_Combat();

		if (TIMER_Done(NPCS.NPC, "attacking")
			&& TIMER_Done(NPCS.NPC, "takingpain")
			&& TIMER_Done(NPCS.NPC, "confusionDebounce")
			&& NPCS.NPCInfo->localState == LSTATE_CLEAR
			&& !NPCS.NPC->count)
		{
			//not busy
			if (!NPCS.ucmd.forwardmove
				&& !NPCS.ucmd.rightmove
				&& VectorCompare(NPCS.NPC->client->ps.moveDir, vec3_origin))
			{
				//not moving
				if (level.time - NPCS.NPCInfo->enemyLastSeenTime > 5000)
				{
					//haven't seen an enemy in a while
					if (!Q_irand(0, 20))
					{
						if (Q_irand(0, 1))
						{
							NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_GUARD_IDLE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						}
						else
						{
							NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_GUARD_LOOKAROUND1,
								SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						}
						TIMER_Set(NPCS.NPC, "confusionTime", NPCS.NPC->client->ps.legsTimer);
						TIMER_Set(NPCS.NPC, "confusionDebounce", NPCS.NPC->client->ps.legsTimer + Q_irand(4000, 8000));
					}
				}
			}
		}
	}
	else
	{
		if (TIMER_Done(NPCS.NPC, "idlenoise"))
		{
			G_Sound(NPCS.NPC, CHAN_AUTO, G_SoundIndex(va("sound/chars/rancor/snort_%d.wav", Q_irand(1, 2))));

			TIMER_Set(NPCS.NPC, "idlenoise", Q_irand(2000, 4000));
			AddSoundEvent(NPCS.NPC, NPCS.NPC->r.currentOrigin, 512, AEL_DANGER, qfalse, qfalse);
		}
		if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
		{
			Rancor_Patrol();
			if (!NPCS.NPC->enemy && NPCS.NPC->wait && NPCS.NPC->spawnflags & SPF_RANCOR_MUTANT)
			{
				//we've been mad before and can't find an enemy: all else failing, always go after the player
				gentity_t* player = FindClosestPlayer(NPCS.NPC->r.currentOrigin, NPCS.NPC->client->enemyTeam);
				if (player && player->health > 0)
				{
					NPCS.NPC->lastEnemy = NPCS.NPC->enemy;
					G_SetEnemy(NPCS.NPC, player);
					if (NPCS.NPC->enemy != NPCS.NPC->lastEnemy)
					{
						//clear this so that we only sniff the player the first time we pick them up
						NPCS.NPC->useDebounceTime = 0;
					}
				}
			}
		}
		else
		{
			Rancor_Idle();
		}
	}

	NPC_UpdateAngles(qtrue, qtrue);
}
#endif