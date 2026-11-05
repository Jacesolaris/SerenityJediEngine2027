// leave this line at the top of all AI_xxxx.cpp files for PCH reasons...
#include "b_local.h"
#include "NPC_steer.h"
#include "g_nav.h"

/*
-------------------------
MP steering layer (DH 2026-11-03)

A C port of the SP STEER / NAV functions (g_navigator.cpp) that SP's flee and animal AI use, so they can run in MP.
As SP: STEER_Activate starts a frame of steering for an NPC (its position, velocity, max speed - run or walk -, the
clients around it), each steering call adds a force towards / away from something (Seek, Flee, Persue, Evade,
Separation, Wander, FollowLeader, AvoidCollisions), and STEER_DeActivate caps that force, applies it to the velocity
and turns the result into the NPC's move direction, yaw and usercmd (forward / right / walking), and into
NPCAI_BLOCKED when collision avoidance found something in the way. The per-NPC data lives here (SP kept some in
gentity_t: lastMoveTime, followPos, resultspeed, avoid sides).
SP's own navigator (paths of nodes, floating / jump edges) does not exist in MP; the NAV_ helpers use MP's waypoint
graph (trap->Nav_*) and NPC_MoveToGoal for paths instead, and give WAYPOINT_NONE / qfalse on maps without nav.
-------------------------
*/

#define STEER_MAX_NEIGHBORS	8
#define STEER_NEIGHBOR_RANGE	64.0f
#define STEER_MAX_FORCE		150.0f
#define STEER_SIDE_LOCKED	2000
#define STEER_MIN_WALK_NORMAL	0.7f // (bg_local.h MIN_WALK_NORMAL)

extern qboolean G_EntIsBreakable(int entityNum);

typedef enum
{
	STEER_SIDE_NONE,
	STEER_SIDE_LEFT,
	STEER_SIDE_RIGHT
} steerSide_t;

typedef struct
{
	qboolean active;
	vec3_t position, velocity, steering, desiredVelocity, seekLocation;
	float speed, maxSpeed, radius;
	int ignoreEntity;
	qboolean blocked;
	int blockedEntity;
	vec3_t blockedPosition;
	gentity_t* neighbors[STEER_MAX_NEIGHBORS];
	int numNeighbors;
} steerUser_t;

typedef struct
{
	int lastMoveTime;
	vec3_t lastPos; // (where it was at lastPosTime: SP's pmove resultspeed)
	int lastPosTime;
	vec3_t followPos;
	int followPosRecalcTime;
	float resultSpeed;
	steerSide_t avoidSide;
	steerSide_t lastAvoidSteerSide;
	int lastAvoidSteerSideDebouncer;
	int freeTime; // (the entity this data belongs to - its slot's freetime -: reset when the slot is reused)
} steerEntData_t;

static steerUser_t steerUsers[MAX_GENTITIES];
static steerEntData_t steerEnt[MAX_GENTITIES];
static trace_t steerMoveTrace;

static steerEntData_t* STEER_EntData(const gentity_t* ent)
{
	steerEntData_t* d = &steerEnt[ent->s.number];
	if (d->freeTime != ent->freetime)
	{
		memset(d, 0, sizeof * d);
		d->freeTime = ent->freetime;
		d->avoidSide = STEER_SIDE_LEFT;
		VectorCopy(ent->r.currentOrigin, d->followPos);
		VectorCopy(ent->r.currentOrigin, d->lastPos);
		d->lastPosTime = level.time;
		d->lastMoveTime = level.time;
	}
	return d;
}

int STEER_LastMoveTime(const gentity_t* ent)
{
	return STEER_EntData(ent)->lastMoveTime;
}

const float* STEER_FollowPos(const gentity_t* leader)
{
	return STEER_EntData(leader)->followPos;
}

float STEER_ResultSpeed(const gentity_t* ent)
{
	return ent->client ? VectorLength(ent->client->ps.velocity) : 0.0f;
}

static float STEER_Truncate(vec3_t v, const float maxLen)
{
	const float len = VectorLength(v);
	if (len > maxLen && len > 0.0f)
	{
		VectorScale(v, maxLen / len, v);
		return maxLen;
	}
	return len;
}

// a move trace of the actor's box (lifted by a step) to pos: qtrue = clear
static qboolean STEER_MoveTrace(const gentity_t* actor, const vec3_t pos)
{
	vec3_t mins;
	VectorCopy(actor->r.mins, mins);
	mins[2] += STEPSIZE;
	trap->Trace(&steerMoveTrace, actor->r.currentOrigin, mins, actor->r.maxs, pos, actor->s.number,
		actor->clipmask & ~CONTENTS_BODY, qfalse, 0, 0);
	if (steerMoveTrace.allsolid || steerMoveTrace.startsolid)
	{
		steerMoveTrace.fraction = 0.0f;
		return qfalse;
	}
	if (steerMoveTrace.fraction < 1.0f)
	{
		// something in the way: a client counts (the AI steers round it), and so does the world
		trace_t clients;
		trap->Trace(&clients, actor->r.currentOrigin, mins, actor->r.maxs, pos, actor->s.number, actor->clipmask, qfalse, 0,
			0);
		steerMoveTrace = clients;
		return qfalse;
	}
	// the world is clear, now the clients
	trap->Trace(&steerMoveTrace, actor->r.currentOrigin, mins, actor->r.maxs, pos, actor->s.number, actor->clipmask,
		qfalse, 0, 0);
	return steerMoveTrace.fraction >= 1.0f && !steerMoveTrace.startsolid ? qtrue : qfalse;
}

qboolean STEER_Active(const gentity_t* actor)
{
	return actor && steerUsers[actor->s.number].active ? qtrue : qfalse;
}

void STEER_Activate(gentity_t* actor)
{
	static int touch[MAX_GENTITIES]; // static: 16 KB off the stack (game code runs on one thread)
	vec3_t mins, maxs;

	if (!actor || !actor->client || !actor->NPC)
	{
		return;
	}
	steerUser_t* su = &steerUsers[actor->s.number];
	memset(su, 0, sizeof * su);
	su->active = qtrue;

	// SP sets lastMoveTime in pmove when the result speed is over 5: the same from how far it got since the last frame
	steerEntData_t* d = STEER_EntData(actor);
	if (level.time > d->lastPosTime)
	{
		if (Distance(d->lastPos, actor->r.currentOrigin) / (level.time - d->lastPosTime) * 1000.0f > 5.0f)
		{
			d->lastMoveTime = level.time;
		}
		VectorCopy(actor->r.currentOrigin, d->lastPos);
		d->lastPosTime = level.time;
	}

	VectorCopy(actor->r.currentOrigin, su->position);
	VectorCopy(actor->client->ps.velocity, su->velocity);
	su->speed = VectorLength(su->velocity);
	su->maxSpeed = actor->NPC->stats.runSpeed;
	if (!(actor->NPC->scriptFlags & SCF_RUNNING)
		&& (actor->NPC->scriptFlags & SCF_WALKING || actor->NPC->aiFlags & NPCAI_WALKING
			|| NPCS.ucmd.buttons & BUTTON_WALKING))
	{
		su->maxSpeed = actor->NPC->stats.walkSpeed;
	}
	if (su->maxSpeed <= 0.0f)
	{
		su->maxSpeed = 150.0f;
	}
	su->radius = RadiusFromBounds(actor->r.mins, actor->r.maxs);
	su->ignoreEntity = ENTITYNUM_NONE;
	su->blockedEntity = ENTITYNUM_NONE;

	// the clients around it
	const float range = su->radius + STEER_NEIGHBOR_RANGE;
	for (int i = 0; i < 3; i++)
	{
		const float r = i == 2 && actor->client->moveType != MT_FLYSWIM ? su->radius * 2.0f : range;
		mins[i] = su->position[i] - r;
		maxs[i] = su->position[i] + r;
	}
	const int num = trap->EntitiesInBox(mins, maxs, touch, MAX_GENTITIES);
	for (int i = 0; i < num && su->numNeighbors < STEER_MAX_NEIGHBORS; i++)
	{
		gentity_t* nbr = &g_entities[touch[i]];
		if (nbr == actor || nbr == actor->enemy || !nbr->inuse || !nbr->client || nbr->health <= 0)
		{
			continue;
		}
		su->neighbors[su->numNeighbors++] = nbr;
	}

	VectorClear(su->steering);
	VectorClear(actor->client->ps.moveDir);
}

void STEER_DeActivate(gentity_t* actor, usercmd_t* ucmd)
{
	if (!STEER_Active(actor))
	{
		return;
	}
	steerUser_t* su = &steerUsers[actor->s.number];
	steerEntData_t* d = STEER_EntData(actor);

	// truncate the steering and apply it to the velocity
	const float newtons = STEER_Truncate(su->steering, STEER_MAX_FORCE);
	if (newtons > 1E-10)
	{
		VectorAdd(su->velocity, su->steering, su->velocity);
		su->speed = STEER_Truncate(su->velocity, su->maxSpeed);
	}
	if (su->speed < 10.0f)
	{
		su->speed = 0.0f;
	}

	// velocity to move direction, angles and the usercmd
	if (actor->NPC->jumpState != JS_JUMPING && actor->NPC->jumpState != JS_LANDING)
	{
		vec3_t moveDir, angles;
		VectorCopy(su->velocity, moveDir);
		VectorCopy(actor->NPC->lastPathAngles, angles);
		if (su->speed > 0.0f && VectorLengthSquared(moveDir) > 0.0f)
		{
			VectorNormalize(moveDir);
			vectoangles(su->velocity, angles);
		}
		else
		{
			VectorClear(moveDir);
		}

		actor->NPC->aiFlags |= NPCAI_NO_SLOWDOWN;
		VectorCopy(moveDir, actor->client->ps.moveDir);
		VectorCopy(angles, actor->NPC->lastPathAngles);
		actor->NPC->desiredPitch = 0.0f;
		actor->NPC->desiredYaw = AngleNormalize360(angles[YAW]);

		if (su->speed > 0.0f)
		{
			vec3_t forward, right;
			AngleVectors(actor->r.currentAngles, forward, right, NULL);
			ucmd->forwardmove = (signed char)floorf(Com_Clamp(-127.0f, 127.0f, DotProduct(forward, moveDir) * 127.0f));
			ucmd->rightmove = (signed char)floorf(Com_Clamp(-127.0f, 127.0f, DotProduct(right, moveDir) * 127.0f));
			ucmd->upmove = 0;
			if (su->speed < actor->NPC->stats.walkSpeed + 5.0f)
			{
				ucmd->buttons |= BUTTON_WALKING;
			}
			else
			{
				ucmd->buttons &= ~BUTTON_WALKING;
			}
			if (actor->client->moveType == MT_FLYSWIM)
			{
				ucmd->forwardmove = 0;
				ucmd->rightmove = 0;
				VectorCopy(su->velocity, actor->client->ps.velocity);
			}
		}
		else
		{
			ucmd->forwardmove = 0;
			ucmd->rightmove = 0;
			ucmd->upmove = 0;
			if (actor->client->moveType == MT_FLYSWIM)
			{
				VectorClear(actor->client->ps.velocity);
			}
		}

		// blocked?
		if (su->blocked)
		{
			if (!(actor->NPC->aiFlags & NPCAI_BLOCKED))
			{
				actor->NPC->aiFlags |= NPCAI_BLOCKED;
				actor->NPC->blockedDebounceTime = level.time;
			}
			actor->NPC->blockedEntity = su->blockedEntity != ENTITYNUM_NONE ? &g_entities[su->blockedEntity] : NULL;
			VectorCopy(su->blockedPosition, actor->NPC->blockedDest);
		}
		else if (actor->NPC->aiFlags & NPCAI_BLOCKED)
		{
			actor->NPC->aiFlags &= ~NPCAI_BLOCKED;
			actor->NPC->blockedDebounceTime = 0;
			actor->NPC->blockedEntity = NULL;
		}
	}
	d->resultSpeed = su->speed;
	su->active = qfalse;
}

float STEER_Stop(gentity_t* actor)
{
	if (!STEER_Active(actor))
	{
		return 0.0f;
	}
	steerUser_t* su = &steerUsers[actor->s.number];
	VectorClear(su->desiredVelocity);
	VectorSubtract(su->steering, su->velocity, su->steering);
	return 0.0f;
}

float STEER_Seek(gentity_t* actor, const vec3_t pos, const float slowingDistance, const float weight,
	const float desiredSpeed)
{
	vec3_t delta;
	if (!STEER_Active(actor))
	{
		return 0.0f;
	}
	steerUser_t* su = &steerUsers[actor->s.number];

	VectorCopy(pos, su->seekLocation);
	VectorSubtract(pos, su->position, su->desiredVelocity);
	if (fabsf(su->desiredVelocity[2]) < 10.0f)
	{
		//If The Difference In Height Is Small Enough, Just Kill It
		su->desiredVelocity[2] = 0.0f;
	}
	const float dist = VectorNormalize(su->desiredVelocity);
	if (dist > 0.0f)
	{
		float speed = desiredSpeed != 0.0f ? desiredSpeed : su->maxSpeed;
		if (slowingDistance != 0.0f && dist < slowingDistance)
		{
			speed *= dist / slowingDistance;
		}
		VectorScale(su->desiredVelocity, speed, su->desiredVelocity);
	}
	else
	{
		VectorClear(su->desiredVelocity);
	}
	VectorSubtract(su->desiredVelocity, su->velocity, delta);
	VectorMA(su->steering, weight, delta, su->steering);
	return dist;
}

float STEER_Flee(gentity_t* actor, const vec3_t pos, const float weight)
{
	vec3_t delta;
	if (!STEER_Active(actor))
	{
		return 0.0f;
	}
	steerUser_t* su = &steerUsers[actor->s.number];

	VectorSubtract(su->position, pos, su->desiredVelocity);
	su->desiredVelocity[2] = 0.0f; // (MP: no flying away on the ground)
	const float dist = VectorNormalize(su->desiredVelocity);
	VectorScale(su->desiredVelocity, su->maxSpeed, su->desiredVelocity);
	VectorSubtract(su->desiredVelocity, su->velocity, delta);
	VectorMA(su->steering, weight, delta, su->steering);
	VectorAdd(pos, su->desiredVelocity, su->seekLocation);
	return dist;
}

// a target's position projected by his velocity (SP Persue / Evade)
static void STEER_ProjectTarget(const steerUser_t* su, const gentity_t* target, vec3_t out)
{
	VectorCopy(target->r.currentOrigin, out);
	if (target->client)
	{
		vec3_t vel;
		const float distToTarget = Distance(out, su->position);
		VectorCopy(target->client->ps.velocity, vel);
		if (VectorNormalize(vel) > 0.0f)
		{
			VectorMA(out, distToTarget + 5.0f, vel, out);
		}
	}
}

float STEER_Persue(gentity_t* actor, const gentity_t* target, const float slowingDistance)
{
	vec3_t projected;
	if (!STEER_Active(actor) || !target)
	{
		return 0.0f;
	}
	STEER_ProjectTarget(&steerUsers[actor->s.number], target, projected);
	return STEER_Seek(actor, projected, slowingDistance, 1.0f, 0.0f);
}

float STEER_Evade(gentity_t* actor, const gentity_t* target)
{
	vec3_t projected;
	if (!STEER_Active(actor) || !target)
	{
		return 0.0f;
	}
	STEER_ProjectTarget(&steerUsers[actor->s.number], target, projected);
	return STEER_Flee(actor, projected, 1.0f);
}

float STEER_Separation(gentity_t* actor, const float scale)
{
	if (!STEER_Active(actor))
	{
		return 0.0f;
	}
	steerUser_t* su = &steerUsers[actor->s.number];
	for (int i = 0; i < su->numNeighbors; i++)
	{
		if (su->neighbors[i]->s.number > actor->s.number)
		{
			vec3_t nbrToAct;
			VectorSubtract(su->position, su->neighbors[i]->r.currentOrigin, nbrToAct);
			float d = VectorLengthSquared(nbrToAct);
			if (d > 1.0f)
			{
				d = 1.0f / d;
				VectorMA(su->steering, d * (su->maxSpeed * 10.0f) * scale, nbrToAct, su->steering);
			}
		}
	}
	return 0.0f;
}

float STEER_Wander(gentity_t* actor)
{
	vec3_t dir, pos;
	if (!STEER_Active(actor))
	{
		return 0.0f;
	}
	steerUser_t* su = &steerUsers[actor->s.number];

	VectorSet(dir, 1.0f, 0.0f, 0.0f);
	if (su->speed > 0.1f)
	{
		vec3_t angles;
		vectoangles(su->velocity, angles);
		angles[YAW] += Q_irand(-5, 5); // (SP turned the roll: a slight change of course)
		AngleVectors(angles, dir, NULL, NULL);
	}
	VectorMA(su->position, 70.0f, dir, pos);
	STEER_Seek(actor, pos, 0.0f, 1.0f, 0.0f);
	return 0.0f;
}

// SP FollowLeader: (re)computes the leader's follow position behind him; the follower then seeks it
float STEER_FollowLeader(gentity_t* actor, gentity_t* leader, const float dist)
{
	if (!STEER_Active(actor) || !leader || !leader->client)
	{
		return 0.0f;
	}
	steerUser_t* su = &steerUsers[actor->s.number];
	steerEntData_t* ld = STEER_EntData(leader);
	const float leaderSpeed = STEER_ResultSpeed(leader);
	const int timeRemaining = ld->followPosRecalcTime - level.time;

	if (timeRemaining < 0 || leaderSpeed > 0.0f && timeRemaining > 1000)
	{
		vec3_t leaderDir, followPos, angles;
		VectorSet(angles, 0, leader->r.currentAngles[YAW], 0);
		AngleVectors(angles, leaderDir, NULL, NULL);
		if (!actor->enemy && !leader->enemy)
		{
			VectorSubtract(leader->r.currentOrigin, su->position, leaderDir);
			VectorNormalize(leaderDir);
		}
		VectorScale(leaderDir, -1.0f * (fabsf(dist) + su->radius), followPos);
		VectorAdd(followPos, leader->r.currentOrigin, followPos);

		STEER_MoveTrace(leader, followPos);
		if (steerMoveTrace.fraction > 0.1f)
		{
			VectorMA(steerMoveTrace.endpos, su->radius, leaderDir, ld->followPos);
		}

		float maxSpeed = g_speed.value;
		if (leaderSpeed > maxSpeed)
		{
			maxSpeed = leaderSpeed;
		}
		const float speedScale = maxSpeed > 0.0f ? 1.0f - leaderSpeed / maxSpeed : 0.0f;
		ld->followPosRecalcTime = level.time + Q_irand(50, 500) + (int)(speedScale * Q_irand(3000, 8000))
			+ (!actor->enemy && !leader->enemy ? Q_irand(8000, 15000) : 0);
	}
	return 0.0f;
}

// SP TestCollision: is the projected position (front or side) safe? if not, steer round what is there
static qboolean STEER_TestCollision(gentity_t* actor, steerUser_t* su, const vec3_t projectVelocity, const float projectSpeed,
	const vec3_t projectPos, const qboolean side, const float weight)
{
	steerEntData_t* d = STEER_EntData(actor);
	qboolean safe = STEER_MoveTrace(actor, projectPos);
	const int contactNum = steerMoveTrace.entityNum;
	vec3_t contactNormal, contactPoint;

	if (contactNum != ENTITYNUM_NONE && contactNum != ENTITYNUM_WORLD)
	{
		if (contactNum == su->ignoreEntity)
		{
			safe = qtrue; // The Ignore Entity Is Safe
		}
		if (g_entities[contactNum].classname && !Q_stricmp(g_entities[contactNum].classname, "func_door"))
		{
			safe = qtrue; // Doors Are Always Safe
		}
		if (actor->NPC->aiFlags & NPCAI_NAV_THROUGH_BREAKABLES && G_EntIsBreakable(contactNum))
		{
			safe = qtrue;
		}
	}
	VectorCopy(steerMoveTrace.plane.normal, contactNormal);
	VectorCopy(steerMoveTrace.endpos, contactPoint);

	if (!safe && !side)
	{
		if (contactNum != ENTITYNUM_WORLD && contactNum != ENTITYNUM_NONE && g_entities[contactNum].client)
		{
			const gentity_t* contact = &g_entities[contactNum];
			const float contactSpeed = VectorLength(contact->client->ps.velocity);
			if (contactSpeed > 0.01f)
			{
				// he is moving: maybe just slow down some and stay behind him
				if (contactSpeed < projectSpeed)
				{
					vec3_t myDir, contactDir, delta;
					VectorCopy(projectVelocity, myDir);
					VectorNormalize(myDir);
					VectorCopy(contact->client->ps.velocity, contactDir);
					VectorNormalize(contactDir);
					const float similarity = fabsf(DotProduct(myDir, contactDir));
					if (similarity > 0.5f)
					{
						VectorCopy(su->velocity, su->desiredVelocity);
						STEER_Truncate(su->desiredVelocity, contactSpeed);
						VectorSubtract(su->desiredVelocity, projectVelocity, delta);
						VectorMA(su->steering, similarity, delta, su->steering);
						su->ignoreEntity = contactNum;
						safe = qtrue;
					}
				}
			}
			else if (su->seekLocation[0] > contact->r.absmin[0] && su->seekLocation[0] < contact->r.absmax[0]
				&& su->seekLocation[1] > contact->r.absmin[1] && su->seekLocation[1] < contact->r.absmax[1]
				&& su->seekLocation[2] > contact->r.absmin[2] && su->seekLocation[2] < contact->r.absmax[2])
			{
				// he stands on where we want to go: give up and stop for now
				VectorSubtract(su->steering, projectVelocity, su->steering);
				su->ignoreEntity = contactNum;
				safe = qtrue;
			}
		}

		// Ignore Shallow Slopes
		if (!safe && contactNormal[2] > 0.0f && contactNormal[2] < STEER_MIN_WALK_NORMAL)
		{
			safe = qtrue;
		}

		if (!safe)
		{
			vec3_t projectDir, along;
			const vec3_t up = { 0, 0, 1 };
			VectorCopy(projectVelocity, projectDir);
			VectorNormalize(projectDir);
			CrossProduct(contactNormal, up, along); // a vector along the wall

			if (d->lastAvoidSteerSide != STEER_SIDE_NONE && d->lastAvoidSteerSideDebouncer < level.time)
			{
				d->lastAvoidSteerSide = STEER_SIDE_NONE;
				d->lastAvoidSteerSideDebouncer = level.time + Q_irand(500, STEER_SIDE_LOCKED);
			}
			if (d->lastAvoidSteerSide == STEER_SIDE_RIGHT
				|| d->lastAvoidSteerSide == STEER_SIDE_NONE && DotProduct(along, projectDir) < 0.0f)
			{
				VectorScale(along, -1.0f, along);
				d->lastAvoidSteerSide = STEER_SIDE_RIGHT;
			}
			else
			{
				d->lastAvoidSteerSide = STEER_SIDE_LEFT;
			}
			along[2] = 0.0f;
			VectorMA(contactPoint, projectSpeed * weight * 0.5f, along, contactPoint);
			STEER_Seek(actor, contactPoint, weight, 1.0f, 0.0f); // (SP passed the weight as the slowing distance)
			su->blocked = qtrue;
			su->blockedEntity = contactNum;
			VectorCopy(su->seekLocation, su->blockedPosition);
		}
		else
		{
			d->lastAvoidSteerSide = STEER_SIDE_NONE;
		}
	}
	return safe ? qfalse : qtrue;
}

float STEER_AvoidCollisions(gentity_t* actor, const gentity_t* leader)
{
	vec3_t projSteering, projVelocity, projectFwd, projectSide, sideVel, angles, right;
	if (!STEER_Active(actor))
	{
		return 0.0f;
	}
	steerUser_t* su = &steerUsers[actor->s.number];
	steerEntData_t* d = STEER_EntData(actor);

	// Simulate The Results Of Any Current Steering To The Velocity
	VectorCopy(su->steering, projSteering);
	VectorCopy(su->velocity, projVelocity);
	float projSpeed = su->speed;
	if (STEER_Truncate(projSteering, STEER_MAX_FORCE) > 1E-10)
	{
		VectorAdd(projVelocity, projSteering, projVelocity);
		projSpeed = STEER_Truncate(projVelocity, su->maxSpeed);
	}

	// Select An Ignore Entity
	su->ignoreEntity = ENTITYNUM_NONE;
	if (actor->NPC->behaviorState != BS_CINEMATIC)
	{
		if (actor->enemy)
		{
			su->ignoreEntity = actor->enemy->s.number;
		}
		else if (leader)
		{
			su->ignoreEntity = leader->s.number;
		}
	}

	if (projSpeed > 0.01f)
	{
		vectoangles(projVelocity, angles);
		AngleVectors(angles, NULL, right, NULL);
		VectorCopy(projVelocity, sideVel);
		VectorMA(sideVel, d->avoidSide == STEER_SIDE_LEFT ? -40.0f : 40.0f, right, sideVel);
		VectorMA(su->position, 1.0f, projVelocity, projectFwd);
		VectorMA(su->position, 0.3f, sideVel, projectSide);

		const qboolean hitFront = STEER_TestCollision(actor, su, projVelocity, projSpeed, projectFwd, qfalse, 1.0f);
		const qboolean hitSide = STEER_TestCollision(actor, su, projVelocity, projSpeed, projectSide, qtrue, 0.5f);
		if (!hitSide)
		{
			// If The Side Is Clear, Try The Other Side
			d->avoidSide = d->avoidSide == STEER_SIDE_LEFT ? STEER_SIDE_RIGHT : STEER_SIDE_LEFT;
		}
		if (hitFront || hitSide)
		{
			return projSpeed;
		}
	}
	return 0.0f;
}

qboolean STEER_Reached(const gentity_t* actor, const vec3_t target, const float targetRadius)
{
	if (!actor)
	{
		return qfalse;
	}
	if (DistanceSquared(target, actor->r.currentOrigin) < targetRadius * targetRadius)
	{
		return qtrue;
	}
	return target[0] > actor->r.absmin[0] && target[0] < actor->r.absmax[0]
		&& target[1] > actor->r.absmin[1] && target[1] < actor->r.absmax[1]
		&& target[2] > actor->r.absmin[2] && target[2] < actor->r.absmax[2] ? qtrue : qfalse;
}

// SP SafeToGoTo, without SP's node safe radius: close enough, or a clear straight move (checked every 1.5 s, trusted 2 s)
static qboolean STEER_SafeToGoTo(gentity_t* actor, const vec3_t targetPosition)
{
	const float dist = Distance(actor->r.currentOrigin, targetPosition);
	if (dist < 110.0f && fabsf(targetPosition[2] - actor->r.currentOrigin[2]) < 50.0f)
	{
		return qtrue;
	}
	if (dist < 400.0f)
	{
		if (!TIMER_Done(actor, "SafeToGoToDURATION"))
		{
			return qtrue;
		}
		if (TIMER_Done(actor, "SafeToGoToCHECK"))
		{
			TIMER_Set(actor, "SafeToGoToCHECK", 1500);
			if (STEER_MoveTrace(actor, targetPosition))
			{
				TIMER_Set(actor, "SafeToGoToDURATION", 2000);
				return qtrue;
			}
		}
	}
	return qfalse;
}

qboolean STEER_GoToPos(gentity_t* actor, const vec3_t position, const float reachedRadius, const qboolean avoidCollisions)
{
	if (!STEER_Active(actor))
	{
		return qfalse;
	}
	if (STEER_Reached(actor, position, reachedRadius))
	{
		STEER_Stop(actor);
		return qtrue;
	}
	if (!STEER_SafeToGoTo(actor, position))
	{
		return qfalse;
	}
	STEER_Seek(actor, position, reachedRadius * 2.0f, 1.0f, 0.0f);
	if (avoidCollisions)
	{
		STEER_AvoidCollisions(actor, actor->client->leader);
	}
	return qtrue;
}

qboolean STEER_GoToEnt(gentity_t* actor, const gentity_t* target, const float reachedRadius, const qboolean avoidCollisions)
{
	if (!STEER_Active(actor))
	{
		return qfalse;
	}
	if (!target)
	{
		STEER_Stop(actor);
		return qtrue;
	}
	if (STEER_Reached(actor, target->r.currentOrigin, reachedRadius))
	{
		STEER_Stop(actor);
		return qtrue;
	}
	if (!STEER_SafeToGoTo(actor, target->r.currentOrigin))
	{
		return qfalse;
	}
	STEER_Persue(actor, target, reachedRadius * 4.0f);
	if (avoidCollisions)
	{
		STEER_AvoidCollisions(actor, actor->client->leader);
	}
	return qtrue;
}

// ------------------------------------ waypoint graph ------------------------------------

int NAV_NearestNode(gentity_t* actor)
{
	if (!actor)
	{
		return WAYPOINT_NONE;
	}
	if (actor->waypoint == WAYPOINT_NONE)
	{
		actor->waypoint = NAV_GetNearestNode(actor, actor->lastWaypoint);
	}
	return actor->waypoint;
}

qboolean NAV_NodePosition(const int node, vec3_t out)
{
	if (node == WAYPOINT_NONE || node < 0)
	{
		return qfalse;
	}
	trap->Nav_GetNodePosition(node, out);
	return qtrue;
}

qboolean NAV_OnNeighboringPoints(const int nodeA, const int nodeB)
{
	if (nodeA == WAYPOINT_NONE || nodeB == WAYPOINT_NONE || nodeA < 0 || nodeB < 0)
	{
		return qfalse;
	}
	if (nodeA == nodeB)
	{
		return qtrue;
	}
	const int numEdges = trap->Nav_GetNodeNumEdges(nodeA);
	for (int i = 0; i < numEdges; i++)
	{
		if (trap->Nav_GetNodeEdge(nodeA, i) == nodeB)
		{
			return qtrue;
		}
	}
	return qfalse;
}

int NAV_ChooseRandomNeighbor(const int node)
{
	if (node == WAYPOINT_NONE || node < 0)
	{
		return WAYPOINT_NONE;
	}
	const int numEdges = trap->Nav_GetNodeNumEdges(node);
	if (numEdges <= 0)
	{
		return WAYPOINT_NONE;
	}
	return trap->Nav_GetNodeEdge(node, Q_irand(0, numEdges - 1));
}

// SP ChooseFarthestNeighbor: of the actor's node and its neighbours, the one farthest from target (and not past it)
int NAV_ChooseFarthestNeighbor(gentity_t* actor, const vec3_t target, const float maxSafeDot)
{
	vec3_t curPos, nbrPos, actorToTgt;
	const int cur = NAV_NearestNode(actor);

	if (cur == WAYPOINT_NONE || cur < 0)
	{
		return WAYPOINT_NONE;
	}
	VectorSubtract(target, actor->r.currentOrigin, actorToTgt);
	const float actorToTgtDist = VectorNormalize(actorToTgt);

	trap->Nav_GetNodePosition(cur, curPos);
	int best = WAYPOINT_NONE;
	float bestDist = 0.0f;
	const float curDist = Distance(target, curPos);
	if (curDist > actorToTgtDist && Distance(actor->r.currentOrigin, curPos) > 300.0f)
	{
		best = cur;
		bestDist = curDist;
	}

	const int numEdges = trap->Nav_GetNodeNumEdges(cur);
	for (int i = 0; i < numEdges; i++)
	{
		vec3_t actorToNbr;
		const int nbr = trap->Nav_GetNodeEdge(cur, i);
		trap->Nav_GetNodePosition(nbr, nbrPos);
		const float nbrDist = Distance(target, nbrPos);
		VectorSubtract(nbrPos, actor->r.currentOrigin, actorToNbr);
		VectorNormalize(actorToNbr);
		if (DotProduct(actorToNbr, actorToTgt) > maxSafeDot)
		{
			continue; // (towards the danger)
		}
		if (nbrDist > actorToTgtDist && (best == WAYPOINT_NONE || nbrDist > bestDist))
		{
			best = nbr;
			bestDist = nbrDist;
		}
	}
	return best;
}

// SP NAV::GoTo: the way there along the waypoint graph, as a steering force. MP's NPC_MoveToGoal finds the direction (it
// leaves it in lastPathAngles); its goal, the temp goal and its usercmd are put back as they were (STEER_DeActivate makes
// the usercmd)
qboolean NAV_GoToPos(gentity_t* actor, const vec3_t position, const float radius)
{
	vec3_t pos, savedOrigin, dir;
	if (!STEER_Active(actor) || actor != NPCS.NPC || !NPCS.NPCInfo->tempGoal)
	{
		return qfalse;
	}
	gentity_t* tempGoal = NPCS.NPCInfo->tempGoal;
	gentity_t* savedGoal = NPCS.NPCInfo->goalEntity;
	const float savedRadius = NPCS.NPCInfo->goalRadius;
	const float savedDistToGoal = NPCS.NPCInfo->distToGoal;
	const qboolean savedCombatMove = NPCS.NPCInfo->combatMove;
	const usercmd_t savedCmd = NPCS.ucmd;
	VectorCopy(tempGoal->r.currentOrigin, savedOrigin);
	const int savedWaypoint = tempGoal->waypoint;
	const int savedCombatPoint = tempGoal->combatPoint;
	const int savedFlags = tempGoal->flags;
	gentity_t* savedEnemy = tempGoal->enemy;

	VectorCopy(position, pos);
	NPC_SetMoveGoal(actor, pos, (int)radius, qtrue, -1, NULL);
	const qboolean moved = NPC_MoveToGoal(qtrue);
	AngleVectors(NPCS.NPCInfo->lastPathAngles, dir, NULL, NULL);

	NPCS.ucmd = savedCmd;
	NPCS.NPCInfo->combatMove = savedCombatMove;
	NPCS.NPCInfo->distToGoal = savedDistToGoal;
	NPCS.NPCInfo->goalEntity = savedGoal;
	NPCS.NPCInfo->goalRadius = savedRadius;
	VectorCopy(savedOrigin, tempGoal->r.currentOrigin);
	tempGoal->waypoint = savedWaypoint;
	tempGoal->combatPoint = savedCombatPoint;
	tempGoal->flags = savedFlags;
	tempGoal->enemy = savedEnemy;
	trap->LinkEntity((sharedEntity_t*)tempGoal);

	if (!moved)
	{
		return qfalse;
	}
	dir[2] = 0.0f;
	if (VectorNormalize(dir) <= 0.0f)
	{
		return qfalse;
	}
	VectorMA(actor->r.currentOrigin, 64.0f, dir, pos);
	STEER_Seek(actor, pos, 0.0f, 1.0f, 0.0f);
	STEER_AvoidCollisions(actor, actor->client->leader);
	return qtrue;
}

// SP SafePathExists: does the straight way from start to stop keep dangerDist away from danger?
qboolean NAV_SafePathExists(const vec3_t start, const vec3_t stop, const vec3_t danger, const float dangerDist)
{
	vec3_t dir, toDanger, closest;
	VectorSubtract(stop, start, dir);
	const float len = VectorNormalize(dir);
	VectorSubtract(danger, start, toDanger);
	float t = DotProduct(toDanger, dir);
	if (t < 0.0f)
	{
		t = 0.0f;
	}
	else if (t > len)
	{
		t = len;
	}
	VectorMA(start, t, dir, closest);
	return Distance(closest, danger) > dangerDist ? qtrue : qfalse;
}