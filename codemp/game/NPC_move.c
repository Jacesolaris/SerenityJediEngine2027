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

//
// NPC_move.cpp
//
#include "b_local.h"
#include "g_nav.h"
#include "anims.h"
#include "bg_public.h"
#include <qcommon\q_platform.h>
#include "g_local.h"
#include <qcommon\q_math.h>
#include <qcommon\q_shared.h>

qboolean G_BoundsOverlap(const vec3_t mins1, const vec3_t maxs1, const vec3_t mins2, const vec3_t maxs2);
int NAV_Steer(gentity_t* self, vec3_t dir, float distance);
extern int GetTime(int lastTime);

navInfo_t frameNavInfo;
extern qboolean FlyingCreature(const gentity_t* ent);

extern qboolean PM_InKnockDown(const playerState_t* ps);
qboolean NPC_CheckFallPositionOK(const gentity_t* NPC, vec3_t position);
static qboolean NPC_TryJump_Final();
extern void G_DrawEdge(vec3_t start, vec3_t end, int type);

void npc_conversation_animation(void)
{
	// Random animation selection (1–10)
	const int randAnim = Q_irand(1, 10);

	switch (randAnim)
	{
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
		NPC_SetAnim(NPCS.NPC,
			SETANIM_TORSO,
			BOTH_TALK2,
			SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		break;

	case 6:
	case 7:
	case 8:
	case 9:
	default:
		NPC_SetAnim(NPCS.NPC,
			SETANIM_TORSO,
			BOTH_TALK1,
			SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		break;
	}
}

static qboolean NPC_Jump(vec3_t dest, const int goal_ent_num)
{
	//FIXME: if land on enemy, knock him down & jump off again
	float best_impact_dist = Q3_INFINITE; //fireSpeed,
	const float min_shot_speed = 30.0f, max_shot_speed = 500.0f;
	qboolean below_blocked = qfalse, above_blocked = qfalse;
	vec3_t target_dir, shot_vel;
	trace_t trace;
	trajectory_t tr;
	int hit_count = 0, above_tries = 0, below_tries = 0;
	const int max_hits = 10;
	vec3_t bottom;

	VectorSubtract(dest, NPCS.NPC->r.currentOrigin, target_dir);
	const float targetDist = VectorNormalize(target_dir);
	//make our shotSpeed reliant on the distance
	float originalShotSpeed = targetDist; //DistanceHorizontal( dest, NPC->currentOrigin )/2.0f;
	if (originalShotSpeed > max_shot_speed)
	{
		originalShotSpeed = max_shot_speed;
	}
	else if (originalShotSpeed < min_shot_speed)
	{
		originalShotSpeed = min_shot_speed;
	}
	float shot_speed = originalShotSpeed;

	while (hit_count < max_hits)
	{
		vec3_t failCase;
		VectorScale(target_dir, shot_speed, shot_vel);
		float travelTime = targetDist / shot_speed;
		shot_vel[2] += travelTime * 0.5 * NPCS.NPC->client->ps.gravity;

		if (!hit_count)
		{
			//save the first one as the worst case scenario
			VectorCopy(shot_vel, failCase);
		}

		if (1) //tracePath )
		{
			vec3_t last_pos;
			const int time_step = 250;
			//do a rough trace of the path
			qboolean blocked = qfalse;

			VectorCopy(NPCS.NPC->r.currentOrigin, tr.trBase);
			VectorCopy(shot_vel, tr.trDelta);
			tr.trType = TR_GRAVITY;
			tr.trTime = level.time;
			travelTime *= 1000.0f;
			VectorCopy(NPCS.NPC->r.currentOrigin, last_pos);

			//This may be kind of wasteful, especially on long throws... use larger steps?  Divide the travelTime into a certain hard number of slices?  Trace just to apex and down?
			for (int elapsed_time = time_step; elapsed_time < floor(travelTime) + time_step; elapsed_time += time_step)
			{
				vec3_t test_pos;
				if ((float)elapsed_time > travelTime)
				{
					//cap it
					elapsed_time = floor(travelTime);
				}
				BG_EvaluateTrajectory(&tr, level.time + elapsed_time, test_pos);
				//FUCK IT, always check for do not enter...
				trap->Trace(&trace, last_pos, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, test_pos, NPCS.NPC->s.number,
					NPCS.NPC->clipmask | CONTENTS_BOTCLIP, qfalse, 0, 0);

				if (trace.allsolid || trace.startsolid)
				{
					//started in solid
					if (NAVDEBUG_showCollision)
					{
						G_DrawEdge(last_pos, trace.endpos, EDGE_RED_TWOSECOND);
					}
					return qfalse; //you're hosed, dude
				}
				if (trace.fraction < 1.0f)
				{
					//hit something
					if (NAVDEBUG_showCollision)
					{
						G_DrawEdge(last_pos, trace.endpos, EDGE_RED_TWOSECOND); // TryJump
					}
					if (trace.entityNum == goal_ent_num)
					{
						//hit the enemy, that's bad!
						blocked = qtrue;
						break;
					}
					if (trace.contents & CONTENTS_BOTCLIP)
					{
						//hit a do-not-enter brush
						blocked = qtrue;
						break;
					}
					if (trace.plane.normal[2] > 0.7 && DistanceSquared(trace.endpos, dest) < 4096)
						//hit within 64 of desired location, should be okay
					{
						//close enough!
						break;
					}
					//FIXME: maybe find the extents of this brush and go above or below it on next try somehow?
					const float impactDist = DistanceSquared(trace.endpos, dest);
					if (impactDist < best_impact_dist)
					{
						best_impact_dist = impactDist;
						VectorCopy(shot_vel, failCase);
					}
					blocked = qtrue;
					break;
				}
				if (NAVDEBUG_showCollision)
				{
					G_DrawEdge(last_pos, test_pos, EDGE_WHITE_TWOSECOND); // TryJump
				}
				if (elapsed_time == floor(travelTime))
				{
					//reached end, all clear
					if (trace.fraction >= 1.0f)
					{
						//hmm, make sure we'll land on the ground...
						//FIXME: do we care how far below ourselves or our dest we'll land?
						VectorCopy(trace.endpos, bottom);
						bottom[2] -= 128;
						trap->Trace(&trace, trace.endpos, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, bottom,
							NPCS.NPC->s.number, NPCS.NPC->clipmask, qfalse, 0, 0);
						if (trace.fraction >= 1.0f || !NPC_CheckFallPositionOK(NPCS.NPC, trace.endpos))
						{
							//would fall too far
							blocked = qtrue;
						}
					}
					break;
				}
				//all clear, try next slice
				VectorCopy(test_pos, last_pos);
			}
			if (blocked)
			{
				const float speedStep = 50.0f;
				//hit something, adjust speed (which will change arc)
				hit_count++;
				//alternate back and forth between trying an arc slightly above or below the ideal
				if (hit_count % 2 && !below_blocked)
				{
					//odd
					below_tries++;
					shot_speed = originalShotSpeed - below_tries * speedStep;
				}
				else if (!above_blocked)
				{
					//even
					above_tries++;
					shot_speed = originalShotSpeed + above_tries * speedStep;
				}
				else
				{
					//can't go any higher or lower
					hit_count = max_hits;
					break;
				}
				if (shot_speed > max_shot_speed)
				{
					shot_speed = max_shot_speed;
					above_blocked = qtrue;
				}
				else if (shot_speed < min_shot_speed)
				{
					shot_speed = min_shot_speed;
					below_blocked = qtrue;
				}
			}
			else
			{
				//made it!
				break;
			}
		}
	}

	if (hit_count >= max_hits)
	{
		//NOTE: worst case scenario, use the one that impacted closest to the target (or just use the first try...?)
		return qfalse;
	}
	VectorCopy(shot_vel, NPCS.NPC->client->ps.velocity);
	return qtrue;
}

#define NPC_JUMP_PREP_BACKUP_DIST 34.0f

trace_t mJumpTrace;

static qboolean NPC_CanTryJump()
{
	if (!(NPCS.NPCInfo->scriptFlags & SCF_NAV_CAN_JUMP) || // Can't Jump
		NPCS.NPCInfo->scriptFlags & SCF_NO_ACROBATICS || // If Can't Jump At All
		level.time < NPCS.NPCInfo->jumpBackupTime || // If Backing Up, Don't Try The Jump Again
		level.time < NPCS.NPCInfo->jumpNextCheckTime || // Don't Even Try To Jump Again For This Amount Of Time
		NPCS.NPCInfo->jumpTime || // Don't Jump If Already Going
		PM_InKnockDown(&NPCS.NPC->client->ps) || // Don't Jump If In Knockdown
		BG_InRoll(&NPCS.NPC->client->ps, NPCS.NPC->client->ps.legsAnim) || // ... Or Roll
		NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		return qfalse;
	}
	return qtrue;
}

qboolean NPC_TryJump(void);

static qboolean NPC_TryJump3(const vec3_t pos, const float max_xy_dist, const float max_z_diff)
{
	if (NPC_CanTryJump())
	{
		NPCS.NPCInfo->jumpNextCheckTime = level.time + Q_irand(1000, 2000);

		VectorCopy(pos, NPCS.NPCInfo->jumpDest);

		// Can't Try To Jump At A Point In The Air
		//-----------------------------------------
		{
			vec3_t groundTest;
			VectorCopy(pos, groundTest);
			groundTest[2] += NPCS.NPC->r.mins[2] * 3;
			trap->Trace(&mJumpTrace, NPCS.NPCInfo->jumpDest, vec3_origin, vec3_origin,
				groundTest, NPCS.NPC->s.number, NPCS.NPC->clipmask, qfalse, 0, 0);
			if (mJumpTrace.fraction >= 1.0f)
			{
				return qfalse; //no ground = no jump
			}
		}
		NPCS.NPCInfo->jumpTarget = 0;
		NPCS.NPCInfo->jumpMaxXYDist = max_xy_dist
			? max_xy_dist
			: NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER
			? 1200
			: 750;
		NPCS.NPCInfo->jumpMazZDist = max_z_diff
			? max_z_diff
			: NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER
			? -1000
			: -450;
		NPCS.NPCInfo->jumpTime = 0;
		NPCS.NPCInfo->jumpBackupTime = 0;
		return NPC_TryJump();
	}
	return qfalse;
}

static qboolean NPC_TryJump2(gentity_t* goal, const float max_xy_dist, const float max_z_diff)
{
	if (NPC_CanTryJump())
	{
		NPCS.NPCInfo->jumpNextCheckTime = level.time + Q_irand(1000, 3000);

		// Can't Jump At Targets In The Air
		//---------------------------------
		if (goal->client && goal->client->ps.groundEntityNum == ENTITYNUM_NONE)
		{
			return qfalse;
		}
		VectorCopy(goal->r.currentOrigin, NPCS.NPCInfo->jumpDest);
		NPCS.NPCInfo->jumpTarget = goal;
		NPCS.NPCInfo->jumpMaxXYDist = max_xy_dist
			? max_xy_dist
			: NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER
			? 1200
			: 750;
		NPCS.NPCInfo->jumpMazZDist = max_z_diff
			? max_z_diff
			: NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER
			? -1000
			: -400;
		NPCS.NPCInfo->jumpTime = 0;
		NPCS.NPCInfo->jumpBackupTime = 0;
		return NPC_TryJump();
	}
	return qfalse;
}

void NPC_JumpSound();
void NPC_JumpAnimation();

qboolean NPC_TryJump(void)
{
	//
	vec3_t targetDirection;

	// Get The Direction And Distances To The Target
	//-----------------------------------------------
	VectorSubtract(NPCS.NPCInfo->jumpDest, NPCS.NPC->r.currentOrigin, targetDirection);
	targetDirection[2] = 0.0f;
	const float targetDistanceXY = VectorNormalize(targetDirection);
	const float targetDistanceZ = NPCS.NPCInfo->jumpDest[2] - NPCS.NPC->r.currentOrigin[2];

	if (targetDistanceXY > NPCS.NPCInfo->jumpMaxXYDist ||
		targetDistanceZ < NPCS.NPCInfo->jumpMazZDist)
	{
		return qfalse;
	}

	// Test To See If There Is A Wall Directly In Front Of Actor, If So, Backup Some
	//-------------------------------------------------------------------------------
	if (TIMER_Done(NPCS.NPC, "jumpBackupDebounce"))
	{
		vec3_t actorProjectedTowardTarget;
		VectorMA(NPCS.NPC->r.currentOrigin, NPC_JUMP_PREP_BACKUP_DIST, targetDirection, actorProjectedTowardTarget);
		trap->Trace(&mJumpTrace, NPCS.NPC->r.currentOrigin, vec3_origin, vec3_origin, actorProjectedTowardTarget,
			NPCS.NPC->s.number, NPCS.NPC->clipmask, qfalse, 0, 0);
		if (mJumpTrace.fraction < 1.0f ||
			mJumpTrace.allsolid ||
			mJumpTrace.startsolid)
		{
			if (NAVDEBUG_showCollision)
			{
				G_DrawEdge(NPCS.NPC->r.currentOrigin, actorProjectedTowardTarget, EDGE_RED_TWOSECOND);
			}

			NPCS.NPCInfo->jumpBackupTime = level.time + 1000;
			TIMER_Set(NPCS.NPC, "jumpBackupDebounce", 5000);
			return qtrue;
		}
	}

	const qboolean WithinForceJumpRange = fabs(targetDistanceZ) > 0 || targetDistanceXY > 128;

	if (!WithinForceJumpRange)
	{
		return qfalse;
	}

	// If There Is Any Chance That This Jump Will Land On An Enemy, Try 8 Different Traces Around The Target
	//-------------------------------------------------------------------------------------------------------
	if (NPCS.NPCInfo->jumpTarget)
	{
		//racc - We're jumping to an entity
		const float minSafeRadius = NPCS.NPC->r.maxs[0] * 1.5f + NPCS.NPCInfo->jumpTarget->r.maxs[0] * 1.5f;
		const float minSafeRadiusSq = minSafeRadius * minSafeRadius;

		if (DistanceSquared(NPCS.NPCInfo->jumpDest, NPCS.NPCInfo->jumpTarget->r.currentOrigin) < minSafeRadiusSq)
		{
			vec3_t startPos;
			vec3_t floorPos;
			VectorCopy(NPCS.NPCInfo->jumpDest, startPos);

			floorPos[2] = NPCS.NPCInfo->jumpDest[2] + (NPCS.NPC->r.mins[2] - 32);

			for (int sideTryCount = 0; sideTryCount < 8; sideTryCount++)
			{
				NPCS.NPCInfo->jumpSide++;
				if (NPCS.NPCInfo->jumpSide > 7)
				{
					NPCS.NPCInfo->jumpSide = 0;
				}

				switch (NPCS.NPCInfo->jumpSide)
				{
				case 0:
					NPCS.NPCInfo->jumpDest[0] = startPos[0] + minSafeRadius;
					NPCS.NPCInfo->jumpDest[1] = startPos[1];
					break;
				case 1:
					NPCS.NPCInfo->jumpDest[0] = startPos[0] + minSafeRadius;
					NPCS.NPCInfo->jumpDest[1] = startPos[1] + minSafeRadius;
					break;
				case 2:
					NPCS.NPCInfo->jumpDest[0] = startPos[0];
					NPCS.NPCInfo->jumpDest[1] = startPos[1] + minSafeRadius;
					break;
				case 3:
					NPCS.NPCInfo->jumpDest[0] = startPos[0] - minSafeRadius;
					NPCS.NPCInfo->jumpDest[1] = startPos[1] + minSafeRadius;
					break;
				case 4:
					NPCS.NPCInfo->jumpDest[0] = startPos[0] - minSafeRadius;
					NPCS.NPCInfo->jumpDest[1] = startPos[1];
					break;
				case 5:
					NPCS.NPCInfo->jumpDest[0] = startPos[0] - minSafeRadius;
					NPCS.NPCInfo->jumpDest[1] = startPos[1] - minSafeRadius;
					break;
				case 6:
					NPCS.NPCInfo->jumpDest[0] = startPos[0];
					NPCS.NPCInfo->jumpDest[1] = startPos[1] - minSafeRadius;
					break;
				case 7:
					NPCS.NPCInfo->jumpDest[0] = startPos[0] + minSafeRadius;
					NPCS.NPCInfo->jumpDest[1] = startPos[1] -= minSafeRadius;
					break;
				default:;
				}

				floorPos[0] = NPCS.NPCInfo->jumpDest[0];
				floorPos[1] = NPCS.NPCInfo->jumpDest[1];

				trap->Trace(&mJumpTrace, NPCS.NPCInfo->jumpDest, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, floorPos,
					NPCS.NPCInfo->jumpTarget ? NPCS.NPCInfo->jumpTarget->s.number : NPCS.NPC->s.number,
					NPCS.NPC->clipmask | CONTENTS_BOTCLIP, qfalse, 0, 0);
				if (mJumpTrace.fraction < 1.0f &&
					!mJumpTrace.allsolid &&
					!mJumpTrace.startsolid)
				{
					break;
				}

				if (NAVDEBUG_showCollision)
				{
					G_DrawEdge(NPCS.NPCInfo->jumpDest, floorPos, EDGE_RED_TWOSECOND);
				}
			}

			// If All Traces Failed, Just Try Going Right Back At The Target Location
			//------------------------------------------------------------------------
			if (mJumpTrace.fraction >= 1.0f ||
				mJumpTrace.allsolid ||
				mJumpTrace.startsolid)
			{
				VectorCopy(startPos, NPCS.NPCInfo->jumpDest);
			}
		}
	}

	// Now, Actually Try The Jump To The Dest Target
	//-----------------------------------------------
	if (NPC_Jump(NPCS.NPCInfo->jumpDest,
		NPCS.NPCInfo->jumpTarget ? NPCS.NPCInfo->jumpTarget->s.number : NPCS.NPC->s.number))
	{
		// We Made IT!
		//-------------
		NPC_JumpAnimation();
		NPC_JumpSound();

		NPCS.NPC->client->ps.fd.forceJumpZStart = NPCS.NPC->r.currentOrigin[2];
		NPCS.NPC->client->ps.weaponTime = NPCS.NPC->client->ps.torsoTimer;
		NPCS.NPC->client->ps.fd.forcePowersActive |= 1 << FP_LEVITATION;
		NPCS.ucmd.forwardmove = 0;
		NPCS.NPCInfo->jumpTime = 1;

		VectorClear(NPCS.NPC->client->ps.moveDir);
		TIMER_Set(NPCS.NPC, "duck", -level.time);

		return qtrue;
	}
	return qfalse;
}

qboolean NPC_TryJump_Pos(const vec3_t pos, const float max_xy_dist, const float max_z_diff)
{
	if (NPC_CanTryJump())
	{
		NPCS.NPCInfo->jumpNextCheckTime = level.time + Q_irand(1000, 2000);

		VectorCopy(pos, NPCS.NPCInfo->jumpDest);

		// Can't Try To Jump At A Point In The Air
		//-----------------------------------------
		{
			vec3_t groundTest;
			VectorCopy(pos, groundTest);
			groundTest[2] += NPCS.NPC->r.mins[2] * 3;
			trap->Trace(&mJumpTrace, NPCS.NPCInfo->jumpDest, vec3_origin, vec3_origin, groundTest, NPCS.NPC->s.number,
				NPCS.NPC->clipmask, qfalse, 0, 0);
			if (mJumpTrace.fraction >= 1.0f)
			{
				return qfalse; //no ground = no jump
			}
		}
		NPCS.NPCInfo->jumpTarget = 0;
		NPCS.NPCInfo->jumpMaxXYDist = max_xy_dist
			? max_xy_dist
			: NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER
			? 1200
			: 750;
		NPCS.NPCInfo->jumpMazZDist = max_z_diff
			? max_z_diff
			: NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER
			? -1000
			: -450;
		NPCS.NPCInfo->jumpTime = 0;
		NPCS.NPCInfo->jumpBackupTime = 0;
		return NPC_TryJump_Final();
	}
	return qfalse;
}

qboolean NPC_TryJump_Gent(gentity_t* goal, const float max_xy_dist, const float max_z_diff)
{
	if (NPC_CanTryJump())
	{
		NPCS.NPCInfo->jumpNextCheckTime = level.time + Q_irand(1000, 3000);

		// Can't Jump At Targets In The Air
		//---------------------------------
		if (goal->client && goal->client->ps.groundEntityNum == ENTITYNUM_NONE)
		{
			return qfalse;
		}
		VectorCopy(goal->r.currentOrigin, NPCS.NPCInfo->jumpDest);
		NPCS.NPCInfo->jumpTarget = goal;
		NPCS.NPCInfo->jumpMaxXYDist = max_xy_dist
			? max_xy_dist
			: NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER
			? 1200
			: 750;
		NPCS.NPCInfo->jumpMazZDist = max_z_diff
			? max_z_diff
			: NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER
			? -1000
			: -400;
		NPCS.NPCInfo->jumpTime = 0;
		NPCS.NPCInfo->jumpBackupTime = 0;
		return NPC_TryJump_Final();
	}
	return qfalse;
}

//do animation for jump
void NPC_JumpAnimation()
{
	int jumpAnim = BOTH_JUMP1;

	// SJE SP (DH 2026-11-03, NPC parity batch D): mandos, sith lords and gun-carrying clone troopers can't flip either,
	// and a saber-carrying one jumps with BOTH_FORCEJUMP2
	if (NPCS.NPC->client->NPC_class == CLASS_BOBAFETT || NPCS.NPC->client->NPC_class == CLASS_MANDO
		|| (NPCS.NPC->client->NPC_class == CLASS_REBORN || NPCS.NPC->client->NPC_class == CLASS_SITHLORD
			|| NPCS.NPC->client->NPC_class == CLASS_CLONETROOPER && NPCS.NPC->s.weapon != WP_SABER)
		|| NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER
		|| NPCS.NPCInfo->rank != RANK_CREWMAN && NPCS.NPCInfo->rank <= RANK_LT_JG)
	{
		//can't do acrobatics
		if (NPCS.NPC->s.weapon == WP_SABER) //saber out
		{
			jumpAnim = BOTH_FORCEJUMP2;
		}
		else
		{
			jumpAnim = BOTH_FORCEJUMP1;
		}
	}
	else if (NPCS.NPC->client->NPC_class != CLASS_HOWLER)
	{
		if (NPCS.NPC->client->NPC_class == CLASS_ALORA && Q_irand(0, 3))
		{
			jumpAnim = Q_irand(BOTH_ALORA_FLIP_1, BOTH_ALORA_FLIP_3);
		}
		else
		{
			jumpAnim = BOTH_FLIP_F;
		}
	}
	NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, jumpAnim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
}

//make sound for jump
extern void G_SoundOnEnt(gentity_t* ent, soundChannel_t channel, const char* sound_path);
extern void JET_FlyStart(gentity_t* self);

void NPC_JumpSound()
{
	if (NPCS.NPC->client->NPC_class == CLASS_HOWLER)
	{
		//FIXME: can I delay the actual jump so that it matches the anim...?
	}
	else if (NPCS.NPC->client->NPC_class == CLASS_BOBAFETT
		|| NPCS.NPC->client->NPC_class == CLASS_MANDO
		|| NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER)
	{
		JET_FlyStart(NPCS.NPC);
	}
	else
	{
		G_SoundOnEnt(NPCS.NPC, CHAN_BODY, "sound/weapons/force/jump.wav");
	}
}

qboolean NPC_Jumping()
{
	//checks to see if we're jumping
	if (NPCS.NPCInfo->jumpTime)
	{
		if (NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE)
		{
			//landed
			NPCS.NPCInfo->jumpTime = 0;
		}
		else
		{
			NPC_FacePosition(NPCS.NPCInfo->jumpDest, qtrue);
			return qtrue;
		}
	}
	return qfalse;
}

qboolean NPC_JumpBackingUp()
{
	//check for and back up before a large jump if we're supposed to.
	if (NPCS.NPCInfo->jumpBackupTime)
	{
		if (level.time < NPCS.NPCInfo->jumpBackupTime)
		{
			NPC_FacePosition(NPCS.NPCInfo->jumpDest, qtrue);
			NPC_UpdateAngles(qfalse, qtrue);
			return qtrue;
		}

		NPCS.NPCInfo->jumpBackupTime = 0;
		return NPC_TryJump();
	}
	return qfalse;
}

qboolean NPC_TryJump_Final()
{
	vec3_t targetDirection;

	// Get The Direction And Distances To The Target
	//-----------------------------------------------
	VectorSubtract(NPCS.NPCInfo->jumpDest, NPCS.NPC->r.currentOrigin, targetDirection);
	targetDirection[2] = 0.0f;
	const float targetDistanceXY = VectorNormalize(targetDirection);
	const float targetDistanceZ = NPCS.NPCInfo->jumpDest[2] - NPCS.NPC->r.currentOrigin[2];

	if (targetDistanceXY > NPCS.NPCInfo->jumpMaxXYDist ||
		targetDistanceZ < NPCS.NPCInfo->jumpMazZDist)
	{
		return qfalse;
	}

	// Test To See If There Is A Wall Directly In Front Of Actor, If So, Backup Some
	//-------------------------------------------------------------------------------
	if (TIMER_Done(NPCS.NPC, "jumpBackupDebounce"))
	{
		vec3_t actorProjectedTowardTarget;
		VectorMA(NPCS.NPC->r.currentOrigin, NPC_JUMP_PREP_BACKUP_DIST, targetDirection, actorProjectedTowardTarget);
		trap->Trace(&mJumpTrace, NPCS.NPC->r.currentOrigin, vec3_origin, vec3_origin, actorProjectedTowardTarget,
			NPCS.NPC->s.number, NPCS.NPC->clipmask, qfalse, 0, 0);
		if (mJumpTrace.fraction < 1.0f ||
			mJumpTrace.allsolid ||
			mJumpTrace.startsolid)
		{
			if (NAVDEBUG_showCollision)
			{
				G_DrawEdge(NPCS.NPC->r.currentOrigin, actorProjectedTowardTarget, EDGE_RED_TWOSECOND); // TryJump
			}

			NPCS.NPCInfo->jumpBackupTime = level.time + 1000;
			TIMER_Set(NPCS.NPC, "jumpBackupDebounce", 5000);
			return qtrue;
		}
	}

	const qboolean WithinForceJumpRange = (float)fabs(targetDistanceZ) > 0 || targetDistanceXY > 128;

	if (!WithinForceJumpRange)
	{
		return qfalse;
	}

	// If There Is Any Chance That This Jump Will Land On An Enemy, Try 8 Different Traces Around The Target
	//-------------------------------------------------------------------------------------------------------
	if (NPCS.NPCInfo->jumpTarget)
	{
		const float minSafeRadius = NPCS.NPC->r.maxs[0] * 1.5f + NPCS.NPCInfo->jumpTarget->r.maxs[0] * 1.5f;
		const float minSafeRadiusSq = minSafeRadius * minSafeRadius;

		if (DistanceSquared(NPCS.NPCInfo->jumpDest, NPCS.NPCInfo->jumpTarget->r.currentOrigin) < minSafeRadiusSq)
		{
			vec3_t startPos;
			vec3_t floorPos;
			VectorCopy(NPCS.NPCInfo->jumpDest, startPos);

			floorPos[2] = NPCS.NPCInfo->jumpDest[2] + (NPCS.NPC->r.mins[2] - 32);

			for (int sideTryCount = 0; sideTryCount < 8; sideTryCount++)
			{
				NPCS.NPCInfo->jumpSide++;
				if (NPCS.NPCInfo->jumpSide > 7)
				{
					NPCS.NPCInfo->jumpSide = 0;
				}

				switch (NPCS.NPCInfo->jumpSide)
				{
				case 0:
					NPCS.NPCInfo->jumpDest[0] = startPos[0] + minSafeRadius;
					NPCS.NPCInfo->jumpDest[1] = startPos[1];
					break;
				case 1:
					NPCS.NPCInfo->jumpDest[0] = startPos[0] + minSafeRadius;
					NPCS.NPCInfo->jumpDest[1] = startPos[1] + minSafeRadius;
					break;
				case 2:
					NPCS.NPCInfo->jumpDest[0] = startPos[0];
					NPCS.NPCInfo->jumpDest[1] = startPos[1] + minSafeRadius;
					break;
				case 3:
					NPCS.NPCInfo->jumpDest[0] = startPos[0] - minSafeRadius;
					NPCS.NPCInfo->jumpDest[1] = startPos[1] + minSafeRadius;
					break;
				case 4:
					NPCS.NPCInfo->jumpDest[0] = startPos[0] - minSafeRadius;
					NPCS.NPCInfo->jumpDest[1] = startPos[1];
					break;
				case 5:
					NPCS.NPCInfo->jumpDest[0] = startPos[0] - minSafeRadius;
					NPCS.NPCInfo->jumpDest[1] = startPos[1] - minSafeRadius;
					break;
				case 6:
					NPCS.NPCInfo->jumpDest[0] = startPos[0];
					NPCS.NPCInfo->jumpDest[1] = startPos[1] - minSafeRadius;
					break;
				case 7:
					NPCS.NPCInfo->jumpDest[0] = startPos[0] + minSafeRadius;
					NPCS.NPCInfo->jumpDest[1] = startPos[1] -= minSafeRadius;
					break;
				default:;
				}

				floorPos[0] = NPCS.NPCInfo->jumpDest[0];
				floorPos[1] = NPCS.NPCInfo->jumpDest[1];

				trap->Trace(&mJumpTrace, NPCS.NPCInfo->jumpDest, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, floorPos,
					NPCS.NPCInfo->jumpTarget ? NPCS.NPCInfo->jumpTarget->s.number : NPCS.NPC->s.number,
					NPCS.NPC->clipmask | CONTENTS_BOTCLIP, qfalse, 0, 0);
				if (mJumpTrace.fraction < 1.0f &&
					!mJumpTrace.allsolid &&
					!mJumpTrace.startsolid)
				{
					break;
				}

				if (NAVDEBUG_showCollision)
				{
					G_DrawEdge(NPCS.NPCInfo->jumpDest, floorPos, EDGE_RED_TWOSECOND);
				}
			}

			// If All Traces Failed, Just Try Going Right Back At The Target Location
			//------------------------------------------------------------------------
			if (mJumpTrace.fraction >= 1.0f ||
				mJumpTrace.allsolid ||
				mJumpTrace.startsolid)
			{
				VectorCopy(startPos, NPCS.NPCInfo->jumpDest);
			}
		}
	}

	// Now, Actually Try The Jump To The Dest Target
	//-----------------------------------------------
	if (NPC_Jump(NPCS.NPCInfo->jumpDest,
		NPCS.NPCInfo->jumpTarget ? NPCS.NPCInfo->jumpTarget->s.number : NPCS.NPC->s.number))
	{
		// We Made IT!
		//-------------
		NPC_JumpAnimation();
		NPC_JumpSound();

		NPCS.NPC->client->ps.fd.forceJumpZStart = NPCS.NPC->r.currentOrigin[2];
		NPCS.NPC->client->ps.weaponTime = NPCS.NPC->client->ps.torsoTimer;
		NPCS.NPC->client->ps.fd.forcePowersActive |= 1 << FP_LEVITATION;
		NPCS.ucmd.forwardmove = 0;
		NPCS.NPCInfo->jumpTime = 1;

		VectorClear(NPCS.NPC->client->ps.moveDir);
		TIMER_Set(NPCS.NPC, "duck", -level.time);

		return qtrue;
	}
	return qfalse;
}

/*
-------------------------
NPC_ClearPathToGoal
-------------------------
*/

qboolean NPC_ClearPathToGoal(gentity_t* goal)
{
	trace_t trace;

	//FIXME: What does do about area portals?  THIS IS BROKEN
	//if ( trap->inPVS( NPC->r.currentOrigin, goal->r.currentOrigin ) == qfalse )
	//	return qfalse;

	//Look ahead and see if we're clear to move to our goal position
	if (NAV_CheckAhead(NPCS.NPC, goal->r.currentOrigin, &trace, NPCS.NPC->clipmask & ~CONTENTS_BODY | CONTENTS_BOTCLIP))
	{
		//VectorSubtract( goal->r.currentOrigin, NPC->r.currentOrigin, dir );
		return qtrue;
	}

	if (!FlyingCreature(NPCS.NPC))
	{
		//See if we're too far above
		if (fabs(NPCS.NPC->r.currentOrigin[2] - goal->r.currentOrigin[2]) > 48)
			return qfalse;
	}

	//This is a work around
	const float radius = NPCS.NPC->r.maxs[0] > NPCS.NPC->r.maxs[1] ? NPCS.NPC->r.maxs[0] : NPCS.NPC->r.maxs[1];
	const float dist = Distance(NPCS.NPC->r.currentOrigin, goal->r.currentOrigin);
	const float tFrac = 1.0f - radius / dist;

	if (trace.fraction >= tFrac)
		return qtrue;

	//See if we're looking for a navgoal
	if (goal->flags & FL_NAVGOAL)
	{
		//Okay, didn't get all the way there, let's see if we got close enough:
		if (NAV_HitNavGoal(trace.endpos, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, goal->r.currentOrigin,
			NPCS.NPCInfo->goalRadius, FlyingCreature(NPCS.NPC)))
		{
			//VectorSubtract(goal->r.currentOrigin, NPC->r.currentOrigin, dir);
			return qtrue;
		}
	}

	return qfalse;
}

qboolean NAV_DirSafe(const gentity_t* self, vec3_t dir, const float dist)
{
	//check to see if this NPC can move in the given direction and distance.
	vec3_t mins, end;
	trace_t trace;

	VectorMA(self->r.currentOrigin, dist, dir, end);

	//Offset the step height
	VectorSet(mins, self->r.mins[0], self->r.mins[1], self->r.mins[2] + STEPSIZE);

	trap->Trace(&trace, self->r.currentOrigin, mins, self->r.maxs, end, self->s.number, CONTENTS_BOTCLIP, qfalse, 0, 0);

	//Do a simple check
	if (trace.allsolid == qfalse && trace.startsolid == qfalse && trace.fraction == 1.0f)
	{
		return qtrue;
	}

	return qfalse;
}

/*
-------------------------
NPC_CheckCombatMove
-------------------------
*/

static QINLINE qboolean NPC_CheckCombatMove(void)
{
	//return NPCInfo->combatMove;
	if (NPCS.NPCInfo->goalEntity && NPCS.NPC->enemy && NPCS.NPCInfo->goalEntity == NPCS.NPC->enemy || NPCS.NPCInfo->
		combatMove)
	{
		return qtrue;
	}

	if (NPCS.NPCInfo->goalEntity && NPCS.NPCInfo->watchTarget)
	{
		if (NPCS.NPCInfo->goalEntity != NPCS.NPCInfo->watchTarget)
		{
			return qtrue;
		}
	}

	return qfalse;
}

/*
-------------------------
NPC_LadderMove
-------------------------
*/

static void NPC_LadderMove(vec3_t dir)
{
	if (dir[2] > 0 || dir[2] < 0 && NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		//Set our movement direction
		NPCS.ucmd.upmove = dir[2] > 0 ? 127 : -127;

		//Don't move around on XY
		NPCS.ucmd.forwardmove = NPCS.ucmd.rightmove = 0;
	}
}

/*
-------------------------
NPC_GetMoveInformation
-------------------------
*/

static QINLINE qboolean NPC_GetMoveInformation(vec3_t dir, float* distance)
{
	//NOTENOTE: Use path stacks!

	//Make sure we have somewhere to go
	if (NPCS.NPCInfo->goalEntity == NULL)
	{
		return qfalse;
	}

	if (PM_InLedgeMove(NPCS.NPC->client->ps.legsAnim))
	{
		NPCS.ucmd.forwardmove = 64;
	}

	//Get our move info
	VectorSubtract(NPCS.NPCInfo->goalEntity->r.currentOrigin, NPCS.NPC->r.currentOrigin, dir);
	*distance = VectorNormalize(dir);

	VectorCopy(NPCS.NPCInfo->goalEntity->r.currentOrigin, NPCS.NPCInfo->blockedDest);

	return qtrue;
}

/*
-------------------------
NAV_GetLastMove
-------------------------
*/

void NAV_GetLastMove(navInfo_t* info)
{
	*info = frameNavInfo;
}

/*
-------------------------
NPC_GetMoveDirection
-------------------------
*/

qboolean NPC_GetMoveDirection(vec3_t out, float* distance)
{
	vec3_t angles;

	//Clear the struct
	memset(&frameNavInfo, 0, sizeof frameNavInfo);

	//Get our movement, if any
	if (NPC_GetMoveInformation(frameNavInfo.direction, &frameNavInfo.distance) == qfalse)
		return qfalse;

	//Setup the return value
	*distance = frameNavInfo.distance;

	//For starters
	VectorCopy(frameNavInfo.direction, frameNavInfo.pathDirection);

	//If on a ladder, move appropriately
	if (NPCS.NPC->watertype & CONTENTS_LADDER)
	{
		NPC_LadderMove(frameNavInfo.direction);
		return qtrue;
	}

	if (PM_InLedgeMove(NPCS.NPC->client->ps.legsAnim))
	{
		NPCS.ucmd.forwardmove = 64;
	}

	//Attempt a straight move to goal
	if (NPC_ClearPathToGoal(NPCS.NPCInfo->goalEntity) == qfalse)
	{
		//See if we're just stuck
		if (NAV_MoveToGoal(NPCS.NPC, &frameNavInfo) == WAYPOINT_NONE)
		{
			//Can't reach goal, just face
			vectoangles(frameNavInfo.direction, angles);
			NPCS.NPCInfo->desiredYaw = AngleNormalize360(angles[YAW]);
			VectorCopy(frameNavInfo.direction, out);
			*distance = frameNavInfo.distance;
			return qfalse;
		}

		frameNavInfo.flags |= NIF_MACRO_NAV;
	}

	//Avoid any collisions on the way
	if (NAV_AvoidCollision(NPCS.NPC, NPCS.NPCInfo->goalEntity, &frameNavInfo) == qfalse)
	{
		//FIXME: Emit a warning, this is a worst case scenario
		//FIXME: if we have a clear path to our goal (exluding bodies), but then this
		//			check (against bodies only) fails, shouldn't we fall back
		//			to macro navigation?  Like so:
		if (!(frameNavInfo.flags & NIF_MACRO_NAV))
		{
			//we had a clear path to goal and didn't try macro nav, but can't avoid collision so try macro nav here
			//See if we're just stuck
			if (NAV_MoveToGoal(NPCS.NPC, &frameNavInfo) == WAYPOINT_NONE)
			{
				//Can't reach goal, just face
				vectoangles(frameNavInfo.direction, angles);
				NPCS.NPCInfo->desiredYaw = AngleNormalize360(angles[YAW]);
				VectorCopy(frameNavInfo.direction, out);
				*distance = frameNavInfo.distance;
				return qfalse;
			}

			frameNavInfo.flags |= NIF_MACRO_NAV;
		}
	}

	//Setup the return values
	VectorCopy(frameNavInfo.direction, out);
	*distance = frameNavInfo.distance;

	return qtrue;
}

/*
-------------------------
NPC_GetMoveDirectionAltRoute
-------------------------
*/
extern int NAVNEW_MoveToGoal(gentity_t* self, navInfo_t* info);
// no SP nav route: follow the bot route instead (NPC_AI_Path.c, Warzone port)
extern qboolean NPC_WPRoute_Direction(gentity_t* self, const vec3_t goal, vec3_t dir);
extern qboolean NAVNEW_AvoidCollision(gentity_t* self, gentity_t* goal, navInfo_t* info, qboolean setBlockedInfo,
	int blockedMovesLimit);

static qboolean NPC_GetMoveDirectionAltRoute(vec3_t out, float* distance, const qboolean tryStraight)
{
	vec3_t angles;

	NPCS.NPCInfo->aiFlags &= ~NPCAI_BLOCKED;

	//Clear the struct
	memset(&frameNavInfo, 0, sizeof frameNavInfo);

	//Get our movement, if any
	if (NPC_GetMoveInformation(frameNavInfo.direction, &frameNavInfo.distance) == qfalse)
		return qfalse;

	//Setup the return value
	*distance = frameNavInfo.distance;

	//For starters
	VectorCopy(frameNavInfo.direction, frameNavInfo.pathDirection);

	//If on a ladder, move appropriately
	if (NPCS.NPC->watertype & CONTENTS_LADDER)
	{
		NPC_LadderMove(frameNavInfo.direction);
		return qtrue;
	}

	//Attempt a straight move to goal
	if (!tryStraight || NPC_ClearPathToGoal(NPCS.NPCInfo->goalEntity) == qfalse)
	{
		//blocked
		//Can't get straight to goal, use macro nav
		if (NAVNEW_MoveToGoal(NPCS.NPC, &frameNavInfo) == WAYPOINT_NONE
			&& !NPC_WPRoute_Direction(NPCS.NPC, NPCS.NPCInfo->goalEntity->r.currentOrigin, frameNavInfo.direction))
		{
			//Can't reach goal, just face
			vectoangles(frameNavInfo.direction, angles);
			NPCS.NPCInfo->desiredYaw = AngleNormalize360(angles[YAW]);
			VectorCopy(frameNavInfo.direction, out);
			*distance = frameNavInfo.distance;
			return qfalse;
		}
		//else we are on our way
		frameNavInfo.flags |= NIF_MACRO_NAV;
	}
	else
	{
		//we have no architectural problems, see if there are ents inthe way and try to go around them
		//not blocked
		if (d_altRoutes.integer)
		{
			//try macro nav
			navInfo_t tempInfo;
			memcpy(&tempInfo, &frameNavInfo, sizeof tempInfo);
			if (NAVNEW_AvoidCollision(NPCS.NPC, NPCS.NPCInfo->goalEntity, &tempInfo, qtrue, 5) == qfalse)
			{
				//revert to macro nav
				//Can't get straight to goal, dump tempInfo and use macro nav
				if (NAVNEW_MoveToGoal(NPCS.NPC, &frameNavInfo) == WAYPOINT_NONE
					&& !NPC_WPRoute_Direction(NPCS.NPC, NPCS.NPCInfo->goalEntity->r.currentOrigin, frameNavInfo.direction))
				{
					//Can't reach goal, just face
					vectoangles(frameNavInfo.direction, angles);
					NPCS.NPCInfo->desiredYaw = AngleNormalize360(angles[YAW]);
					VectorCopy(frameNavInfo.direction, out);
					*distance = frameNavInfo.distance;
					return qfalse;
				}
				//else we are on our way
				frameNavInfo.flags |= NIF_MACRO_NAV;
			}
			else
			{
				//otherwise, either clear or can avoid
				memcpy(&frameNavInfo, &tempInfo, sizeof frameNavInfo);
			}
		}
		else
		{
			//OR: just give up
			if (NAVNEW_AvoidCollision(NPCS.NPC, NPCS.NPCInfo->goalEntity, &frameNavInfo, qtrue, 30) == qfalse)
			{
				//give up
				return qfalse;
			}
		}
	}

	//Setup the return values
	VectorCopy(frameNavInfo.direction, out);
	*distance = frameNavInfo.distance;

	return qtrue;
}

extern qboolean NPC_MoveDirClear(int forwardmove, int rightmove, qboolean reset);
extern qboolean G_EntIsBreakable(int entityNum);

qboolean NPC_EntityIsBreakable(const gentity_t* ent)
{
	if (ent
		&& ent->inuse
		&& ent->takedamage
		&& ent->classname
		&& ent->classname[0]
		&& ent->s.eType != ET_INVISIBLE
		&& ent->s.eType != ET_NPC
		&& ent->s.eType != ET_PLAYER
		&& !ent->client
		&& G_EntIsBreakable(ent->s.number)
		&& !EntIsGlass(ent)
		&& ent->health > 0
		&& !(ent->r.svFlags & SVF_PLAYER_USABLE))
	{
		return qtrue;
	}

	return qfalse;
}

qboolean NPC_IsAlive(const gentity_t* self, const gentity_t* npc)
{
	if (!NPCS.NPC)
	{
		return qfalse;
	}

	if (self && self->client && NPC_EntityIsBreakable(NPCS.NPC) && NPCS.NPC->health > 0)
	{
		return qtrue;
	}

	if (NPCS.NPC->s.eType == ET_NPC || NPCS.NPC->s.eType == ET_PLAYER)
	{
		if (NPCS.NPC->client && NPCS.NPC->client->ps.pm_type == PM_SPECTATOR)
		{
			return qfalse;
		}

		if (NPCS.NPC->health <= 0 && (NPCS.NPC->client && NPCS.NPC->client->ps.stats[STAT_HEALTH] <= 0))
		{
			return qfalse;
		}
	}
	else
	{
		return qfalse;
	}

	return qtrue;
}

extern qboolean NPC_MoveDirClear(int forwardmove, int rightmove, qboolean reset);
extern void G_DebugPrint(int level, const char* format, ...);

// Move an NPC or bot in a given direction using usercmd logic.
// Behaviour preserved; only safety and structure improved.
static qboolean SJE_UcmdMoveForDir(const gentity_t* self, usercmd_t* cmd,
	vec3_t dir, qboolean walk, vec3_t dest)
{
	vec3_t forward, right;

	// SAFETY FIX: self and self->client must exist
	if (self == NULL || self->client == NULL)
	{
		G_DebugPrint(WL_WARNING, "SJE_UcmdMoveForDir: self or self->client was NULL\n");
		return qfalse;
	}

	const float walkSpeed = 63.0f;
	const gentity_t* aiEnt = self;

	// Water combat logic (behaviour preserved)
	if (self->waterlevel > 0 &&
		self->enemy &&
		self->enemy->client &&
		NPC_IsAlive(self, self->enemy) == qtrue)
	{
		// NPC checks water level to avoid drowning while attacking
	}

	// Jedi always walk when in saber combat
	if (self->client->ps.weapon == WP_SABER &&
		self->enemy &&
		self->enemy->client &&
		self->enemy->client->ps.weapon == WP_SABER &&
		Distance(self->r.currentOrigin, self->enemy->r.currentOrigin) < 110.0f)
	{
		walk = qtrue;
	}

	if (walk == qtrue)
	{
		cmd->buttons |= BUTTON_WALKING;
	}

	// Safe: self->client is guaranteed non‑NULL above
	AngleVectors(self->client->ps.viewangles, forward, right, NULL);

	cmd->upmove = 0;

	// Flatten vectors
	dir[2] = 0;
	VectorNormalize(dir);

	forward[2] = 0;
	VectorNormalize(forward);

	right[2] = 0;
	VectorNormalize(right);

	// NPCs cheat and store moveDir directly
	VectorCopy(dir, self->client->ps.moveDir);

	// Compute movement magnitude
	const float speed = (walk == qtrue) ? walkSpeed : 127.0f;
	const float forwardmove = speed * DotProduct(forward, dir);
	const float rightmove = speed * DotProduct(right, dir);

	// Optimize magnitude for maximum speed
	if (Q_fabs(forwardmove) > Q_fabs(rightmove))
	{
		const float highestforward = (forwardmove < 0.0f) ? -speed : speed;
		const float highestright = highestforward * rightmove / forwardmove;

		cmd->forwardmove = ClampChar(highestforward);
		cmd->rightmove = ClampChar(highestright);
	}
	else
	{
		const float highestright = (rightmove < 0.0f) ? -speed : speed;
		const float highestforward = highestright * forwardmove / rightmove;

		cmd->forwardmove = ClampChar(highestforward);
		cmd->rightmove = ClampChar(highestright);
	}

	// Swimming → always go upward
	if (self->waterlevel > 0)
	{
		cmd->upmove = 127.0f;
	}

	// Ground movement safety check
	if (self->client->ps.groundEntityNum != ENTITYNUM_NONE &&
		NPC_MoveDirClear(cmd->forwardmove, cmd->rightmove, qfalse) == qfalse)
	{
		cmd->forwardmove = 0;
		cmd->rightmove = 0;
		return qfalse;
	}

	// Bot AI movement (ET_PLAYER)
	if (aiEnt->s.eType == ET_PLAYER)
	{
		vec3_t viewAngles;
		vectoangles(dir, viewAngles);
		trap->EA_View(aiEnt->s.number, viewAngles);

		const qboolean walkingCmd =
			(aiEnt->client && (aiEnt->client->pers.cmd.buttons & BUTTON_WALKING)) ? qtrue : qfalse;

		if (walkingCmd == qtrue)
		{
			trap->EA_Action(aiEnt->s.number, 0x0080000);
			trap->EA_Move(aiEnt->s.number, dir, 100);
		}
		else
		{
			trap->EA_Move(aiEnt->s.number, dir, 200);
		}

		// Strafe / jump logic
		if (self->bot_strafe_jump_timer > level.time)
		{
			trap->EA_Jump(aiEnt->s.number);
		}
		else if (self->bot_strafe_left_timer > level.time)
		{
			trap->EA_MoveLeft(aiEnt->s.number);
		}
		else if (self->bot_strafe_right_timer > level.time)
		{
			trap->EA_MoveRight(aiEnt->s.number);
		}
	}

	return qtrue;
}

void G_UcmdMoveForDir(const gentity_t* self, usercmd_t* cmd, vec3_t dir)
{
	vec3_t forward, right;

	AngleVectors(self->r.currentAngles, forward, right, NULL);

	dir[2] = 0;
	VectorNormalize(dir);
	//NPCs cheat and store this directly because converting movement into a ucmd loses precision
	VectorCopy(dir, self->client->ps.moveDir);

	float fDot = DotProduct(forward, dir) * 127.0f;
	float rDot = DotProduct(right, dir) * 127.0f;
	//Must clamp this because DotProduct is not guaranteed to return a number within -1 to 1, and that would be bad when we're shoving this into a signed byte
	if (fDot > 127.0f)
	{
		fDot = 127.0f;
	}
	if (fDot < -127.0f)
	{
		fDot = -127.0f;
	}
	if (rDot > 127.0f)
	{
		rDot = 127.0f;
	}
	if (rDot < -127.0f)
	{
		rDot = -127.0f;
	}
	cmd->forwardmove = floor(fDot);
	cmd->rightmove = floor(rDot);

	qboolean walk = qfalse;

	// (not last frame's BUTTON_WALKING from pers.cmd: ClientThink sets it on every standing / slowing NPC, so it latched
	// on and a chasing NPC moved at walkSpeed with the run anim - "NPCs move very slowly". SP has no such carry-over.)

	// enemy first: an NPC following its leader has no enemy (NULL dereference crash)
	if (self->enemy
		&& self->enemy->client
		&& self->enemy->client->ps.weapon == WP_SABER
		&& Distance(self->r.currentOrigin, self->enemy->r.currentOrigin) < 110.0)
	{
		walk = qtrue;
	}

	if (walk)
	{
		cmd->buttons |= BUTTON_WALKING;
	}

	//SJE_UcmdMoveForDir(self, &self->client->pers.cmd, dir, walk, dest);
}

/*
-------------------------
NPC_MoveToGoal

  Now assumes goal is goalEntity, was no reason for it to be otherwise
-------------------------
*/
#if	AI_TIMERS
extern int navTime;
#endif//	AI_TIMERS
qboolean NPC_MoveToGoal(const qboolean tryStraight)
{
	float distance = 0.0f;
	vec3_t dir = { 0.0f, 0.0f, 0.0f }; // SJE: not every path of the move-direction call sets it

#if	AI_TIMERS
	int	startTime = GetTime(0);
#endif//	AI_TIMERS
	//If taking full body pain, don't move
	if (PM_InKnockDown(&NPCS.NPC->client->ps) || NPCS.NPC->s.legsAnim >= BOTH_PAIN1 && NPCS.NPC->s.legsAnim <=
		BOTH_PAIN18)
	{
		return qtrue;
	}

	//Get our movement direction
#if 1
	if (NPC_GetMoveDirectionAltRoute(dir, &distance, tryStraight) == qfalse)
#else
	if (NPC_GetMoveDirection(dir, &distance) == qfalse)
#endif
		return qfalse;

	NPCS.NPCInfo->distToGoal = distance;

	//Convert the move to angles
	vectoangles(dir, NPCS.NPCInfo->lastPathAngles);
	if (NPCS.ucmd.buttons & BUTTON_WALKING)
	{
		NPCS.NPC->client->ps.speed = NPCS.NPCInfo->stats.walkSpeed;
	}
	else
	{
		NPCS.NPC->client->ps.speed = NPCS.NPCInfo->stats.runSpeed;
	}

	//FIXME: still getting ping-ponging in certain cases... !!!  Nav/avoidance error?  WTF???!!!
	//If in combat move, then move directly towards our goal
	if (NPC_CheckCombatMove())
	{
		//keep current facing
		G_UcmdMoveForDir(NPCS.NPC, &NPCS.ucmd, dir);
	}
	else
	{
		//face our goal
		//FIXME: strafe instead of turn if change in dir is small and temporary
		NPCS.NPCInfo->desiredPitch = 0.0f;
		NPCS.NPCInfo->desiredYaw = AngleNormalize360(NPCS.NPCInfo->lastPathAngles[YAW]);

		//Pitch towards the goal and also update if flying or swimming
		if (NPCS.NPC->client->ps.eFlags2 & EF2_FLYING) //moveType == MT_FLYSWIM )
		{
			NPCS.NPCInfo->desiredPitch = AngleNormalize360(NPCS.NPCInfo->lastPathAngles[PITCH]);

			if (dir[2])
			{
				float scale = dir[2] * distance;
				if (scale > 64)
				{
					scale = 64;
				}
				else if (scale < -64)
				{
					scale = -64;
				}
				NPCS.NPC->client->ps.velocity[2] = scale;
			}
		}

		//Set any final info
		NPCS.ucmd.forwardmove = 127;
	}

#if	AI_TIMERS
	navTime += GetTime(startTime);
#endif//	AI_TIMERS
	return qtrue;
}

/*
-------------------------
void NPC_SlideMoveToGoal( void )

  Now assumes goal is goalEntity, if want to use tempGoal, you set that before calling the func
-------------------------
*/
qboolean NPC_SlideMoveToGoal(void)
{
	const float saveYaw = NPCS.NPC->client->ps.viewangles[YAW];

	NPCS.NPCInfo->combatMove = qtrue;

	const qboolean ret = NPC_MoveToGoal(qtrue);

	NPCS.NPCInfo->desiredYaw = saveYaw;

	return ret;
}

/*
-------------------------
NPC_ApplyRoff
-------------------------
*/

void NPC_ApplyRoff(void)
{
	BG_PlayerStateToEntityState(&NPCS.NPC->client->ps, &NPCS.NPC->s, qfalse);
	// use the precise origin for linking
	trap->LinkEntity((sharedEntity_t*)NPCS.NPC);
}

qboolean NPC_IsJetpacking(const gentity_t* self)
{
	if (self->s.eFlags & EF_JETPACK_ACTIVE || self->s.eFlags & EF_JETPACK_FLAMING || self->s.eFlags & EF3_JETPACK_HOVER)
	{
		return qtrue;
	}

	return qfalse;
}

qboolean NPC_CheckFallPositionOK(const gentity_t* NPC, vec3_t position)
{
	trace_t tr;
	vec3_t testPos, downPos;
	vec3_t mins, maxs;

	if (NPC_IsJetpacking(NPC))
	{
		return qtrue;
	}

	VectorSet(mins, -8, -8, -1);
	VectorSet(maxs, 8, 8, 1);

	VectorCopy(position, testPos);
	VectorCopy(position, downPos);

	downPos[2] -= 96.0;

	if (NPC->s.groundEntityNum < ENTITYNUM_MAX_NORMAL)
		downPos[2] -= 192.0;

	testPos[2] += 48.0;

	trap->Trace(&tr, testPos, mins, maxs, downPos, NPC->s.number, MASK_PLAYERSOLID, 0, 0, 0);

	if (tr.entityNum != ENTITYNUM_NONE)
	{
		return qtrue;
	}
	if (tr.fraction == 1.0f)
	{
		return qfalse;
	}

	return qtrue;
}

/*
-------------------------
NPC_ObstacleClimb

Obstacle climb (DH 2026-11-12, g_obstacleClimb 1): an NPC who is trying to get somewhere but has stood pressed against
something for OC_STUCK_TIME looks at what is in his way:
- a ledge he can get onto (a plain hop up to OC_HOP_HEIGHT, higher with force jump, by his force jump level) he jumps
  straight up beside it and, once his feet are over the top, on over the edge (straight up first, so he never touches
  the wall on the way up and the wall grab / rebound never starts);
- a wall too high, a ledge with no room or no floor on top, or a class that can't jump (droids, beasts, ATST...,
  SCF_NO_ACROBATICS): he steps round it to the side that is open, or backs off a little for the steering to try again.
People in the way are left to the normal avoidance, he only climbs toward where he is going (not a sideways strafe in a
fight), and never in the air, swimming, on a ladder, flying or while busy with a knockdown or roll.
Called once a think, just before the NPCS.ucmd goes to ClientThink.
-------------------------
*/

#define OC_STEP 18.0f // pmove step height: anything lower he walks up
#define OC_STUCK_SPEED 60.0f // a moving NPC slower than this is pressed against something
#define OC_STUCK_TIME 600 // ms pressed against it before he does something about it
#define OC_HOP_HEIGHT 46.0f // highest ledge a plain jump gets onto (31 up + a step)
#define OC_PROBE_DIST 24.0f // how far ahead he looks for what blocks him
#define OC_PUSH_SPEED 200.0f // speed over the edge once his feet are above it
#define OC_CLIMB_TIMEOUT 1800

enum
{
	OC_NONE,
	OC_CLIMB,
	OC_SIDESTEP,
	OC_BACKOFF
};

// OC_PROBE_* results: g_local.h


typedef struct
{
	int lastSeen;
	int stuckSince;
	int mode;
	int modeStart;
	int modeEnd;
	int nextTry;
	int climbFails; // climbs in a row that didn't get him up
	int roundTries; // side steps / back offs in a row
	int lastEnd;
	vec3_t dir;
	float topZ;
	qboolean pushed;
} obstacleClimb_t;

static obstacleClimb_t oc_state[MAX_GENTITIES];

static qboolean OC_ClassCanJump(const gentity_t* ent)
{
	if (BG_AnimSetCantJump(ent->localAnimIndex))
	{
		return qfalse; // no jump anim on this skeleton (as pmove)
	}
	switch (ent->client->NPC_class)
	{
	case CLASS_ATST:
	case CLASS_FISH:
	case CLASS_FLIER2:
	case CLASS_GLIDER:
	case CLASS_GONK:
	case CLASS_RANCOR:
	case CLASS_SAND_CREATURE:
	case CLASS_INTERROGATOR:
	case CLASS_LIZARD:
	case CLASS_MARK1:
	case CLASS_MARK2:
	case CLASS_GALAKMECH:
	case CLASS_MINEMONSTER:
	case CLASS_MOUSE:
	case CLASS_MURJJ:
	case CLASS_PROBE:
	case CLASS_PROTOCOL:
	case CLASS_R2D2:
	case CLASS_R5D2:
	case CLASS_REMOTE:
	case CLASS_SEEKER:
	case CLASS_SENTRY:
	case CLASS_SWAMP:
	case CLASS_VEHICLE:
	case CLASS_SBD:
	case CLASS_BATTLEDROID:
	case CLASS_DROIDEKA:
	case CLASS_OBJECT:
	case CLASS_PROJECTION:
		return qfalse;
	default:
		return qtrue;
	}
}

// highest ledge (feet to top) he can get onto
static float OC_MaxClimb(const gentity_t* ent)
{
	if (!OC_ClassCanJump(ent) || !ent->NPC || (ent->NPC->scriptFlags & SCF_NO_ACROBATICS) != 0)
	{
		return 0.0f;
	}
	if ((ent->client->ps.fd.forcePowersKnown & (1 << FP_LEVITATION)) == 0)
	{
		return OC_HOP_HEIGHT;
	}
	// force jump heights (96 / 192 / 384) + a step, less a margin
	const int level = ent->client->ps.fd.forcePowerLevel[FP_LEVITATION];
	if (level >= 3)
	{
		return 392.0f;
	}
	if (level == 2)
	{
		return 200.0f;
	}
	if (level == 1)
	{
		return 104.0f;
	}
	return OC_HOP_HEIGHT;
}

static void OC_Trace(trace_t* tr, const gentity_t* ent, const vec3_t start, const vec3_t mins, const vec3_t maxs,
	const vec3_t end, const int mask)
{
	trap->Trace(tr, start, mins, maxs, end, ent->s.number, mask, qfalse, 0, 0);
}

// is there ground within dist below pos (so a side step or back off doesn't walk him off a cliff)?
qboolean OC_GroundBelow(const gentity_t* ent, const vec3_t pos, const float dist)
{
	trace_t tr;
	vec3_t end;
	VectorCopy(pos, end);
	end[2] -= dist;
	OC_Trace(&tr, ent, pos, ent->r.mins, ent->r.maxs, end, ent->clipmask | CONTENTS_BOTCLIP);
	return !tr.startsolid && !tr.allsolid && tr.fraction < 1.0f ? qtrue : qfalse;
}

// what blocks him in dir; for a ledge he can get onto (no higher than max_climb) *top_z is its top (feet height)
int OC_Probe(const gentity_t* ent, const vec3_t dir, const float max_climb, float* top_z)
{
	trace_t tr;
	vec3_t mins, start, end;
	const float* org = ent->r.currentOrigin;
	const int mask = ent->clipmask | CONTENTS_BOTCLIP; // a do-not-enter brush is a wall too
	const float feet = org[2] + ent->r.mins[2];

	// something above step height right in front of him?
	VectorCopy(ent->r.mins, mins);
	mins[2] += OC_STEP + 1.0f;
	if (mins[2] >= ent->r.maxs[2])
	{
		mins[2] = ent->r.maxs[2] - 1.0f;
	}
	VectorMA(org, OC_PROBE_DIST, dir, end);
	OC_Trace(&tr, ent, org, mins, ent->r.maxs, end, mask);
	if (tr.startsolid || tr.allsolid || tr.fraction >= 1.0f)
	{
		return OC_PROBE_CLEAR;
	}
	if (tr.entityNum >= 0 && tr.entityNum < ENTITYNUM_WORLD && g_entities[tr.entityNum].client)
	{
		return OC_PROBE_PERSON;
	}
	if (max_climb <= OC_STEP)
	{
		return OC_PROBE_WALL;
	}
	const float wall_dist = tr.fraction * OC_PROBE_DIST;

	// how high can he rise where he stands?
	VectorCopy(org, end);
	end[2] += max_climb + 4.0f;
	OC_Trace(&tr, ent, org, ent->r.mins, ent->r.maxs, end, mask);
	if (tr.startsolid || tr.allsolid)
	{
		return OC_PROBE_WALL;
	}
	const float rise = tr.endpos[2] - org[2];

	// lowest height from which he can move across onto a floor on top
	for (int h = 32; h <= (int)(rise); h += 24)
	{
		VectorCopy(org, start);
		start[2] += (float)(h);
		VectorMA(start, wall_dist + ent->r.maxs[0] + 8.0f, dir, end);
		OC_Trace(&tr, ent, start, ent->r.mins, ent->r.maxs, end, mask);
		if (tr.startsolid || tr.allsolid || tr.fraction < 1.0f)
		{
			continue; // the wall goes higher still
		}
		VectorCopy(end, start);
		end[2] = org[2] + OC_STEP;
		OC_Trace(&tr, ent, start, ent->r.mins, ent->r.maxs, end, mask);
		if (tr.startsolid || tr.allsolid || tr.fraction >= 1.0f || tr.plane.normal[2] < 0.7f)
		{
			return OC_PROBE_WALL; // nothing to stand on up there (a rail with a drop behind it), or too steep
		}
		*top_z = tr.endpos[2] + ent->r.mins[2];
		const float dz = *top_z - feet;
		return dz > OC_STEP && dz <= max_climb ? OC_PROBE_LEDGE : OC_PROBE_WALL;
	}
	return OC_PROBE_WALL;
}

// an open side (left or right of dir, either first) he can step to, without a drop
qboolean OC_FindSideStep(const gentity_t* ent, const vec3_t dir, vec3_t out)
{
	trace_t tr;
	vec3_t mins, side, end, start;
	const float* org = ent->r.currentOrigin;
	const int mask = ent->clipmask | CONTENTS_BOTCLIP;
	const float first = Q_irand(0, 1) ? 1.0f : -1.0f;

	VectorCopy(ent->r.mins, mins);
	mins[2] += OC_STEP + 1.0f;
	if (mins[2] >= ent->r.maxs[2])
	{
		mins[2] = ent->r.maxs[2] - 1.0f;
	}
	for (int i = 0; i < 2; i++)
	{
		const float s = i ? -first : first;
		VectorSet(side, -dir[1] * s, dir[0] * s, 0.0f);
		VectorMA(org, 64.0f, side, end);
		OC_Trace(&tr, ent, org, mins, ent->r.maxs, end, mask);
		if (tr.startsolid || tr.allsolid || tr.fraction < 0.5f)
		{
			continue;
		}
		VectorCopy(tr.endpos, start);
		if (!OC_GroundBelow(ent, start, 48.0f))
		{
			continue;
		}
		VectorMA(start, 48.0f, dir, end);
		OC_Trace(&tr, ent, start, mins, ent->r.maxs, end, mask);
		if (tr.startsolid || tr.allsolid || tr.fraction < 1.0f)
		{
			continue;
		}
		VectorCopy(side, out);
		return qtrue;
	}
	return qfalse;
}

static void OC_EndMode(obstacleClimb_t* oc)
{
	oc->mode = OC_NONE;
	oc->stuckSince = 0;
	oc->lastEnd = level.time;
	oc->nextTry = level.time + 300;
}

// which way he is trying to go (flat); qfalse if he isn't trying to go anywhere
static qboolean OC_WantDir(vec3_t out)
{
	const playerState_t* ps = &NPCS.NPC->client->ps;

	if (ps->speed > 0.0f && (NPCS.ucmd.forwardmove || NPCS.ucmd.rightmove)
		&& (ps->moveDir[0] != 0.0f || ps->moveDir[1] != 0.0f))
	{
		VectorSet(out, ps->moveDir[0], ps->moveDir[1], 0.0f);
	}
	else if (NPCS.ucmd.forwardmove || NPCS.ucmd.rightmove)
	{
		vec3_t forward, right;
		AngleVectors(NPCS.NPC->r.currentAngles, forward, right, NULL);
		VectorScale(forward, NPCS.ucmd.forwardmove, out);
		VectorMA(out, NPCS.ucmd.rightmove, right, out);
		out[2] = 0.0f;
	}
	else
	{
		return qfalse;
	}
	return VectorNormalize(out) > 0.1f ? qtrue : qfalse;
}

void NPC_ObstacleClimb(void)
{
	if (!g_obstacleClimb.integer || !NPCS.NPC || !NPCS.NPC->client || !NPCS.NPCInfo)
	{
		return;
	}
	obstacleClimb_t* oc = &oc_state[NPCS.NPC->s.number];
	if (!oc->lastSeen || level.time < oc->lastSeen || level.time - oc->lastSeen > 1000)
	{
		memset(oc, 0, sizeof(*oc)); // new NPC in this slot, or a new level / load
	}
	oc->lastSeen = level.time;

	playerState_t* ps = &NPCS.NPC->client->ps;
	const qboolean on_ground = ps->groundEntityNum != ENTITYNUM_NONE ? qtrue : qfalse;

	if (NPCS.NPC->health <= 0 || ps->pm_type != PM_NORMAL
		|| NPCS.NPC->waterlevel >= 2 || NPCS.NPC->watertype & CONTENTS_LADDER
		|| ps->eFlags2 & (EF2_FLYING | EF2_HELD_BY_MONSTER) || NPCS.NPCInfo->scriptFlags & SCF_NAV_CAN_FLY
		|| NPC_IsJetpacking(NPCS.NPC) || NPCS.NPC->s.m_iVehicleNum || ps->emplacedIndex
		|| PM_InKnockDown(ps) || BG_InRoll(ps, ps->legsAnim)
		|| NPCS.NPCInfo->jumpTime || NPCS.NPCInfo->jumpBackupTime > level.time) // his own jump code is busy
	{
		oc->mode = OC_NONE;
		oc->stuckSince = 0;
		return;
	}

	const float feet = NPCS.NPC->r.currentOrigin[2] + NPCS.NPC->r.mins[2];

	switch (oc->mode)
	{
	case OC_CLIMB:
		if (level.time - oc->modeStart > OC_CLIMB_TIMEOUT || (on_ground && level.time - oc->modeStart > 250))
		{
			oc->climbFails = feet < oc->topZ - 4.0f ? oc->climbFails + 1 : 0; // up there, or back down where he started
			OC_EndMode(oc);
			return;
		}
		if (!oc->pushed)
		{
			if (feet >= oc->topZ + 1.0f)
			{
				// feet over the top: on over the edge
				ps->velocity[0] = oc->dir[0] * OC_PUSH_SPEED;
				ps->velocity[1] = oc->dir[1] * OC_PUSH_SPEED;
				oc->pushed = qtrue;
			}
			else
			{
				// still rising: straight up, no steering into the wall
				NPCS.ucmd.forwardmove = 0;
				NPCS.ucmd.rightmove = 0;
				NPCS.ucmd.upmove = 0;
				VectorClear(ps->moveDir);
				ps->velocity[0] = 0.0f;
				ps->velocity[1] = 0.0f;
				return;
			}
		}
		{
			vec3_t dir;
			VectorCopy(oc->dir, dir);
			G_UcmdMoveForDir(NPCS.NPC, &NPCS.ucmd, dir);
			NPCS.ucmd.upmove = 0;
		}
		return;

	case OC_SIDESTEP:
	case OC_BACKOFF:
		if (level.time >= oc->modeEnd)
		{
			OC_EndMode(oc);
			return;
		}
		{
			vec3_t dir;
			VectorCopy(oc->dir, dir);
			G_UcmdMoveForDir(NPCS.NPC, &NPCS.ucmd, dir);
			NPCS.ucmd.upmove = 0;
			if (ps->speed < (float)NPCS.NPCInfo->stats.walkSpeed)
			{
				ps->speed = (float)NPCS.NPCInfo->stats.walkSpeed;
			}
		}
		return;

	default:
		break;
	}

	// pressed against something?
	vec3_t want;
	const float hspeed = sqrtf(ps->velocity[0] * ps->velocity[0] + ps->velocity[1] * ps->velocity[1]);
	if (!on_ground || hspeed >= OC_STUCK_SPEED || !OC_WantDir(want))
	{
		oc->stuckSince = 0;
		if (level.time - oc->lastEnd > 4000)
		{
			oc->climbFails = 0; // he has been moving freely a while
			oc->roundTries = 0;
		}
		return;
	}
	if (!oc->stuckSince)
	{
		oc->stuckSince = level.time;
	}
	if (level.time - oc->stuckSince < OC_STUCK_TIME || level.time < oc->nextTry)
	{
		return;
	}
	oc->stuckSince = 0;
	oc->nextTry = level.time + 500;

	// climb only toward where he is going, and not after two climbs that didn't get him up
	float max_climb = oc->climbFails < 2 ? OC_MaxClimb(NPCS.NPC) : 0.0f;
	if (max_climb > 0.0f && NPCS.NPCInfo->goalEntity && NPCS.NPCInfo->goalEntity != NPCS.NPC)
	{
		vec3_t to_goal;
		VectorSubtract(NPCS.NPCInfo->goalEntity->r.currentOrigin, NPCS.NPC->r.currentOrigin, to_goal);
		to_goal[2] = 0.0f;
		if (VectorNormalize(to_goal) > 1.0f && DotProduct(to_goal, want) < 0.3f)
		{
			max_climb = 0.0f;
		}
	}

	float top_z = 0.0f;
	const int probe = OC_Probe(NPCS.NPC, want, max_climb, &top_z);
	if (probe == OC_PROBE_CLEAR || probe == OC_PROBE_PERSON)
	{
		return;
	}
	if (probe == OC_PROBE_LEDGE && ps->gravity > 0)
	{
		// jump straight up, high enough for his feet to clear the top
		const float apex = top_z + 12.0f - feet;
		ps->velocity[0] = 0.0f;
		ps->velocity[1] = 0.0f;
		ps->velocity[2] = sqrtf(2.0f * (float)ps->gravity * apex);
		ps->fd.forceJumpZStart = NPCS.NPC->r.currentOrigin[2];
		ps->pm_flags |= PMF_JUMPING;
		if (top_z - feet > OC_HOP_HEIGHT)
		{
			ps->fd.forcePowersActive |= 1 << FP_LEVITATION;
			NPC_JumpAnimation();
			G_SoundOnEnt(NPCS.NPC, CHAN_BODY, "sound/weapons/force/jump.wav");
		}
		else
		{
			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_JUMP1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			G_AddEvent(NPCS.NPC, EV_JUMP, 0);
		}
		NPCS.ucmd.forwardmove = 0;
		NPCS.ucmd.rightmove = 0;
		NPCS.ucmd.upmove = 0;
		VectorClear(ps->moveDir);
		VectorCopy(want, oc->dir);
		oc->topZ = top_z;
		oc->pushed = qfalse;
		oc->mode = OC_CLIMB;
		oc->modeStart = level.time;
		return;
	}

	// can't get over it: round it, or back off for the steering to try again
	vec3_t side;
	if (OC_FindSideStep(NPCS.NPC, want, side))
	{
		VectorCopy(side, oc->dir);
		oc->mode = OC_SIDESTEP;
		oc->modeStart = level.time;
		oc->modeEnd = level.time + Q_irand(500, 800);
	}
	else
	{
		vec3_t back;
		VectorMA(NPCS.NPC->r.currentOrigin, -32.0f, want, back);
		if (OC_GroundBelow(NPCS.NPC, back, 48.0f))
		{
			VectorScale(want, -1.0f, oc->dir);
			oc->mode = OC_BACKOFF;
			oc->modeStart = level.time;
			oc->modeEnd = level.time + 400;
		}
	}
	if (++oc->roundTries >= 5)
	{
		// he keeps ending up against it: leave it to his normal navigation for a while
		oc->nextTry = level.time + 5000;
		oc->roundTries = 0;
		oc->climbFails = 0;
	}
}
