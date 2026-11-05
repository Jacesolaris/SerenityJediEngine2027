// leave this line at the top of all AI_xxxx.cpp files for PCH reasons...
#include "b_local.h"

/*
-------------------------
Hazard trooper squads: SP's AI_HazardTrooper.cpp (DH 2026-11-03, NPC parity batch 5)

An NPC with a weapon and SCF_NO_GROUPS (the hazard troopers, spawnflag 4096, ICARUS SET_NO_GROUPS) is a trooper
(NPC_IsTrooper, NPC.c). Troopers of a team who can see each other within 1000 form a troop of up to 7 (the highest rank
leads; troops within 500 merge). The troop scans for a target (sight within visrange, weighted by facing; hearing within
earshot, louder when he runs or carries a lit saber) and the leader gives orders: while the target was seen in the last
2 s they attack - in a line (up to 4), flanking (more than 4: the rest take flanking combat points) or surrounding when
not in formation - else they advance - regroup when spread out, move up in cover / formation, search where he was last
seen. Each trooper goes to his ordered spot, gets out of the way of the others, shoots when he has a clear shot (aiming
lower when close), smacks away an enemy closer than 40, and asks a trooper in his line of fire to kneel. With no target
the stormtrooper AI runs. The leader calls the orders (hand signals and voice lines).
C port: the troop pool, the per-trooper troop / kneel time / last move time are kept here (SP had them in gNPC_t /
gentity_t); the troops update once per frame from the first trooper that thinks; dead or freed troopers drop out on that
update (SP: NPC_LeaveTroop in player_die). SP's STEER / NAV moves are MP's move goal (NPC_MoveToGoal) and
G_UcmdMoveForDir.
-------------------------
*/

#define MAX_TROOPS				32
#define MAX_ENTS_PER_TROOP		7
#define MAX_TROOP_JOIN_DIST2	1000000 //1000 units
#define MAX_TROOP_MERGE_DIST2	250000 //500 units
#define TARGET_POS_VISITED		10000 //100 units

extern qboolean NPC_IsTrooper(const gentity_t* self);

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

typedef enum
{
	TS_NONE = 0,

	TS_ADVANCE,
	TS_ADVANCE_REGROUP,
	TS_ADVANCE_SEARCH,
	TS_ADVANCE_COVER,
	TS_ADVANCE_FORMATION,

	TS_ATTACK,
	TS_ATTACK_LINE,
	TS_ATTACK_FLANK,
	TS_ATTACK_SURROUND,
	TS_ATTACK_COVER,

	TS_MAX
} troopState_t;

typedef struct
{
	vec3_t position;
	int combatPoint;
} actorOrder_t;

typedef struct
{
	qboolean inUse;
	int team;
	qboolean reform;
	float formSpacingFwd;
	float formSpacingRight;

	gentity_t* actors[MAX_ENTS_PER_TROOP];
	int numActors;
	actorOrder_t orders[MAX_ENTS_PER_TROOP];

	gentity_t* target;
	qboolean targetVisible;
	int targetVisibleStartTime;
	int targetVisibleStopTime;
	vec3_t targetVisiblePosition;
	int targetIndex;
	int targetLastKnownTime;
	vec3_t targetLastKnownPosition;
	qboolean targetLastKnownPositionVisited;

	troopState_t state;
	vec3_t formHead, formFwd, formRight;
} troop_t;

static troop_t troops[MAX_TROOPS]; // a troop's handle is its index + 1 (0 = none)
static int trooperTroop[MAX_GENTITIES];
static int trooperKneelTime[MAX_GENTITIES];
static int trooperLastMoveTime[MAX_GENTITIES];
static int troopLevel = -1;
static int troopUpdateTime = -1;

static troop_t* Troop_Get(const int handle)
{
	return handle > 0 && handle <= MAX_TROOPS && troops[handle - 1].inUse ? &troops[handle - 1] : NULL;
}

static void HT_Speech(const gentity_t* self, const int speechType, const float failChance)
{
	if (Q_flrand(0.0f, 1.0f) < failChance)
	{
		return;
	}

	if (failChance >= 0)
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
	}

	TIMER_Set(self, "chatter", Q_irand(2000, 4000));

	if (self->NPC->blockedSpeechDebounceTime > level.time)
	{
		return;
	}

	switch (speechType)
	{
	case SPEECH_CHASE:
		G_AddVoiceEvent(self, Q_irand(EV_CHASE1, EV_CHASE3), 2000);
		break;
	case SPEECH_CONFUSED:
		G_AddVoiceEvent(self, Q_irand(EV_CONFUSE1, EV_CONFUSE3), 2000);
		break;
	case SPEECH_COVER:
		G_AddVoiceEvent(self, Q_irand(EV_COVER1, EV_COVER5), 2000);
		break;
	case SPEECH_DETECTED:
		G_AddVoiceEvent(self, Q_irand(EV_DETECTED1, EV_DETECTED5), 2000);
		break;
	case SPEECH_GIVEUP:
		G_AddVoiceEvent(self, Q_irand(EV_GIVEUP1, EV_GIVEUP4), 2000);
		break;
	case SPEECH_LOOK:
		G_AddVoiceEvent(self, Q_irand(EV_LOOK1, EV_LOOK2), 2000);
		break;
	case SPEECH_LOST:
		G_AddVoiceEvent(self, EV_LOST1, 2000);
		break;
	case SPEECH_OUTFLANK:
		G_AddVoiceEvent(self, Q_irand(EV_OUTFLANK1, EV_OUTFLANK2), 2000);
		break;
	case SPEECH_ESCAPING:
		G_AddVoiceEvent(self, Q_irand(EV_ESCAPING1, EV_ESCAPING3), 2000);
		break;
	case SPEECH_SIGHT:
		G_AddVoiceEvent(self, Q_irand(EV_SIGHT1, EV_SIGHT3), 2000);
		break;
	case SPEECH_SOUND:
		G_AddVoiceEvent(self, Q_irand(EV_SOUND1, EV_SOUND3), 2000);
		break;
	case SPEECH_SUSPICIOUS:
		G_AddVoiceEvent(self, Q_irand(EV_SUSPICIOUS1, EV_SUSPICIOUS5), 2000);
		break;
	case SPEECH_YELL:
		G_AddVoiceEvent(self, Q_irand(EV_ANGER1, EV_ANGER3), 2000);
		break;
	case SPEECH_PUSHED:
		G_AddVoiceEvent(self, Q_irand(EV_PUSHED1, EV_PUSHED3), 2000);
		break;
	default:
		break;
	}

	self->NPC->blockedSpeechDebounceTime = level.time + 2000;
}

// ------------------------------------ the troop ------------------------------------

static void Troop_Initialize(troop_t* troop)
{
	memset(troop, 0, sizeof * troop);
	troop->inUse = qtrue;
	troop->state = TS_NONE;
	for (int i = 0; i < MAX_ENTS_PER_TROOP; i++)
	{
		troop->orders[i].combatPoint = -1;
	}
}

static int Troop_Handle(const troop_t* troop)
{
	return (int)(troop - troops) + 1;
}

static float Troop_DistanceSq(const troop_t* troop, const gentity_t* ent)
{
	if (!ent || !troop->numActors || !troop->actors[0])
	{
		return 0.0f;
	}
	return DistanceSquared(ent->r.currentOrigin, troop->actors[0]->r.currentOrigin);
}

static void Troop_SwapActors(troop_t* troop, const int a, const int b)
{
	gentity_t* tmp = troop->actors[a];
	troop->actors[a] = troop->actors[b];
	troop->actors[b] = tmp;
}

// MakeActorLeader - Move A Given Index To A Leader Position
static void Troop_MakeActorLeader(troop_t* troop, const int index)
{
	if (!troop->numActors || index < 0 || index >= troop->numActors)
	{
		return;
	}

	gentity_t* oldLeader = troop->actors[0];
	if (oldLeader && oldLeader->client)
	{
		oldLeader->client->leader = NULL;
	}
	if (index != 0)
	{
		Troop_SwapActors(troop, index, 0);
	}

	gentity_t* newLeader = troop->actors[0];
	if (newLeader && newLeader->client)
	{
		newLeader->client->leader = newLeader;

		if (newLeader->client->NPC_class == CLASS_HAZARD_TROOPER)
		{
			troop->formSpacingFwd = 75.0f;
			troop->formSpacingRight = 50.0f;
		}
		else
		{
			troop->formSpacingFwd = 75.0f;
			troop->formSpacingRight = 20.0f;
		}
	}
}

// AddActor - Adds a new actor to the troop & automatically promote to leader
static void Troop_AddActor(troop_t* troop, gentity_t* actor)
{
	if (!actor || !actor->NPC || !actor->client || troop->numActors >= MAX_ENTS_PER_TROOP)
	{
		return;
	}

	trooperTroop[actor->s.number] = Troop_Handle(troop);
	troop->actors[troop->numActors++] = actor;
	troop->reform = qtrue;

	if (troop->numActors == 1 || actor->NPC->rank > troop->actors[0]->NPC->rank)
	{
		Troop_MakeActorLeader(troop, troop->numActors - 1);
	}

	if (!troop->team)
	{
		troop->team = actor->client->playerTeam;
	}
}

// RemoveActor - Removes an actor from the troop & automatically promote leader
static void Troop_RemoveActor(troop_t* troop, const gentity_t* actor)
{
	int bestNewLeader = -1;
	troop->reform = qtrue;

	for (int i = 0; i < troop->numActors;)
	{
		if (troop->actors[i] == actor)
		{
			// (erase_swap)
			troop->actors[i] = troop->actors[--troop->numActors];
			troop->actors[troop->numActors] = NULL;
			if (i == 0 && troop->numActors)
			{
				bestNewLeader = 0;
			}
			continue;
		}
		if (bestNewLeader >= 0
			&& troop->actors[i] && troop->actors[bestNewLeader]
			&& troop->actors[i]->NPC && troop->actors[bestNewLeader]->NPC
			&& troop->actors[i]->NPC->rank > troop->actors[bestNewLeader]->NPC->rank)
		{
			bestNewLeader = i;
		}
		i++;
	}

	if (troop->numActors && bestNewLeader >= 0)
	{
		Troop_MakeActorLeader(troop, bestNewLeader);
	}
	if (!actor)
	{
		return; // (an empty slot)
	}
	if (actor->client && actor->client->leader == actor)
	{
		((gentity_t*)actor)->client->leader = NULL;
	}
	trooperTroop[actor->s.number] = 0;
}

// RegisterTarget - Records That the target is seen, when and where
static void Troop_RegisterTarget(troop_t* troop, gentity_t* target, const int index, const qboolean visible)
{
	if (!target || !troop->numActors || !troop->actors[0])
	{
		return;
	}

	if (!troop->target)
	{
		HT_Speech(troop->actors[0], SPEECH_DETECTED, 0);
	}
	else if (level.time - troop->targetLastKnownTime > 8000)
	{
		HT_Speech(troop->actors[0], SPEECH_SIGHT, 0);
	}

	if (visible)
	{
		troop->targetVisibleStopTime = level.time;
		if (!troop->targetVisible)
		{
			troop->targetVisibleStartTime = level.time;
		}
		CalcEntitySpot(target, SPOT_HEAD, troop->targetVisiblePosition);
		troop->targetVisiblePosition[2] -= 10.0f;
	}

	troop->target = target;
	troop->targetVisible = visible;
	troop->targetIndex = index;
	troop->targetLastKnownTime = level.time;
	VectorCopy(target->r.currentOrigin, troop->targetLastKnownPosition);
	troop->targetLastKnownPositionVisited = qfalse;
}

static qboolean Troop_TargetLastKnownPositionVisited(troop_t* troop)
{
	if (!troop->targetLastKnownPositionVisited && troop->numActors && troop->actors[0])
	{
		const float dist = DistanceSquared(troop->targetLastKnownPosition, troop->actors[0]->r.currentOrigin);
		troop->targetLastKnownPositionVisited = dist < TARGET_POS_VISITED ? qtrue : qfalse;
	}
	return troop->targetLastKnownPositionVisited;
}

static float Troop_ClampScale(float val)
{
	if (val > 1.0f)
	{
		val = 1.0f;
	}
	if (val < 0.0f)
	{
		val = 0.0f;
	}
	return val;
}

static qboolean Troop_LitSaber(const gentity_t* target)
{
	return target->client && target->client->ps.weapon == WP_SABER && !target->client->ps.saberHolstered ? qtrue : qfalse;
}

static float Troop_TargetVisibility(const gentity_t* target)
{
	float scale = 0.8f;
	if (Troop_LitSaber(target))
	{
		scale += 0.1f;
	}
	return Troop_ClampScale(scale);
}

static float Troop_TargetNoiseLevel(const gentity_t* target)
{
	float scale = 0.1f;
	if (g_speed.integer > 0 && target->client)
	{
		scale += VectorLength(target->client->ps.velocity) / (float)g_speed.integer;
	}
	if (Troop_LitSaber(target))
	{
		scale += 0.2f;
	}
	return Troop_ClampScale(scale);
}

// Scan For Enemies
static void Troop_ScanForTarget(troop_t* troop, const int scannerIndex)
{
	if (!troop->numActors || scannerIndex < 0 || scannerIndex >= troop->numActors)
	{
		return;
	}

	gentity_t* scanner = troop->actors[scannerIndex];
	if (!scanner || !scanner->NPC)
	{
		return;
	}

	int targetIndex = 0;
	int targetStop = ENTITYNUM_WORLD;
	const float scannerMaxViewDist = scanner->NPC->stats.visrange;
	const float scannerMaxHearDist = scanner->NPC->stats.earshot;
	vec3_t scannerFwd;
	AngleVectors(scanner->r.currentAngles, scannerFwd, NULL, NULL);

	if (troop->target)
	{
		targetIndex = troop->targetIndex;
		targetStop = troop->targetIndex + 1;
	}

	SaveNPCGlobals();
	SetNPCGlobals(scanner);

	for (; targetIndex < targetStop; targetIndex++)
	{
		gentity_t* target = &g_entities[targetIndex];
		vec3_t targetPos, targetDirection;

		if (!target->inuse || !NPC_ValidEnemy(target))
		{
			continue;
		}
		VectorCopy(target->r.currentOrigin, targetPos);
		VectorSubtract(targetPos, scanner->r.currentOrigin, targetDirection);
		const float targetDistance = VectorNormalize(targetDirection);

		// SEE
		if (targetDistance < scannerMaxViewDist)
		{
			const float targetVisibility = Troop_TargetVisibility(target) * DotProduct(targetDirection, scannerFwd);
			if (targetVisibility > 0.1f && NPC_ClearLOS(scanner->client->renderInfo.eyePoint, targetPos))
			{
				Troop_RegisterTarget(troop, target, targetIndex, qtrue);
				RestoreNPCGlobals();
				return;
			}
		}

		// HEAR
		if (targetDistance < scannerMaxHearDist)
		{
			const float targetNoiseLevel = Troop_TargetNoiseLevel(target) * (1.0f - targetDistance / scannerMaxHearDist);
			if (targetNoiseLevel > 0.3f)
			{
				Troop_RegisterTarget(troop, target, targetIndex, qfalse);
				RestoreNPCGlobals();
				return;
			}
		}
	}

	RestoreNPCGlobals();
}

static qboolean Troop_InFormation(const troop_t* troop)
{
	if (troop->numActors <= 1)
	{
		return qtrue;
	}

	const float maxActorRange = (troop->numActors / 2.0f + 2.0f) * troop->formSpacingFwd;
	const float maxActorRangeSq = maxActorRange * maxActorRange;

	for (int actorIndex = 1; actorIndex < troop->numActors; actorIndex++)
	{
		if (troop->actors[actorIndex] && Troop_DistanceSq(troop, troop->actors[actorIndex]) > maxActorRangeSq)
		{
			return qfalse;
		}
	}
	return qtrue;
}

// LeaderIssueAndUpdateOrders
static void Troop_LeaderIssueAndUpdateOrders(troop_t* troop, const troopState_t nextState)
{
	const int actorCount = troop->numActors;
	trace_t trace;
	const vec3_t up = { 0, 0, 1 };

	if (!actorCount || !troop->actors[0])
	{
		return;
	}

	// Assign closest actors to order positions
	for (int orderIndex = 1; orderIndex < actorCount; orderIndex++)
	{
		if (troop->orders[orderIndex].combatPoint != -1)
		{
			continue;
		}

		int closestActorIndex = orderIndex;
		float closestActorDistance = DistanceSquared(troop->orders[orderIndex].position,
			troop->actors[orderIndex]->r.currentOrigin);

		for (int actorIndex = orderIndex + 1; actorIndex < actorCount; actorIndex++)
		{
			if (!troop->actors[actorIndex])
			{
				continue;
			}
			const float currentDistance = DistanceSquared(troop->orders[orderIndex].position,
				troop->actors[actorIndex]->r.currentOrigin);
			if (currentDistance < closestActorDistance)
			{
				closestActorDistance = currentDistance;
				closestActorIndex = actorIndex;
			}
		}
		if (orderIndex != closestActorIndex)
		{
			Troop_SwapActors(troop, orderIndex, closestActorIndex);
		}
	}

	// Copy orders to actors
	for (int actorIndex = 1; actorIndex < actorCount; actorIndex++)
	{
		if (troop->actors[actorIndex])
		{
			VectorCopy(troop->orders[actorIndex].position, troop->actors[actorIndex]->pos1);
		}
	}

	gentity_t* leader = troop->actors[0];

	// Phase I: voice/anim
	if (nextState != troop->state)
	{
		switch (nextState)
		{
		case TS_ADVANCE_SEARCH:
			HT_Speech(leader, SPEECH_LOOK, 0);
			break;
		case TS_ADVANCE_COVER:
			HT_Speech(leader, SPEECH_COVER, 0);
			NPC_SetAnim(leader, SETANIM_TORSO, TORSO_HANDSIGNAL4, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLDLESS);
			break;
		case TS_ADVANCE_FORMATION:
			HT_Speech(leader, SPEECH_ESCAPING, 0);
			break;
		case TS_ATTACK_LINE:
			HT_Speech(leader, SPEECH_CHASE, 0);
			NPC_SetAnim(leader, SETANIM_TORSO, TORSO_HANDSIGNAL1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLDLESS);
			break;
		case TS_ATTACK_FLANK:
			HT_Speech(leader, SPEECH_OUTFLANK, 0);
			NPC_SetAnim(leader, SETANIM_TORSO, TORSO_HANDSIGNAL3, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLDLESS);
			break;
		case TS_ATTACK_SURROUND:
			HT_Speech(leader, SPEECH_GIVEUP, 0);
			NPC_SetAnim(leader, SETANIM_TORSO, TORSO_HANDSIGNAL2, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLDLESS);
			break;
		case TS_ATTACK_COVER:
			HT_Speech(leader, SPEECH_COVER, 0);
			break;
		default:
			break;
		}
	}
	else if (nextState > TS_ATTACK && !troop->reform)
	{
		return;
	}

	// Phase II: formation vectors (SP: towards the leader's next path point, else the target's last known spot)
	VectorCopy(leader->r.currentOrigin, troop->formHead);
	VectorSubtract(troop->targetLastKnownPosition, troop->formHead, troop->formFwd);
	troop->formFwd[2] = 0.0f;
	VectorScale(troop->formFwd, -1.0f, troop->formFwd);
	VectorNormalize(troop->formFwd);

	CrossProduct(troop->formFwd, up, troop->formRight);

	VectorScale(troop->formFwd, troop->formSpacingFwd, troop->formFwd);
	VectorScale(troop->formRight, troop->formSpacingRight, troop->formRight);

	if (nextState > TS_ATTACK)
	{
		if (!troop->reform)
		{
			const int fwdNum = actorCount / 2 + 1;
			for (int i = 0; i < fwdNum; i++)
			{
				VectorSubtract(troop->formHead, troop->formFwd, troop->formHead);
			}
		}

		VectorCopy(troop->formHead, troop->orders[0].position);
		trap->Trace(&trace, leader->r.currentOrigin, leader->r.mins, leader->r.maxs, troop->orders[0].position,
			leader->s.number, leader->clipmask, qfalse, 0, 0);
		if (trace.fraction < 1.0f)
		{
			VectorCopy(trace.endpos, troop->orders[0].position);
		}
	}
	else
	{
		VectorCopy(troop->targetLastKnownPosition, troop->orders[0].position);
	}
	VectorCopy(troop->orders[0].position, leader->pos1);

	// Phase III: per-actor orders
	for (int actorIndex = 1; actorIndex < actorCount; actorIndex++)
	{
		gentity_t* actor = troop->actors[actorIndex];
		if (!actor || !actor->NPC)
		{
			continue;
		}

		SaveNPCGlobals();
		SetNPCGlobals(actor);

		actorOrder_t* order = &troop->orders[actorIndex];
		const float fwdScale = (float)((actorIndex + 1) / 2);
		const float sideScale = actorIndex % 2 == 0 ? -1.0f : 1.0f;

		if (actor->NPC->combatPoint != -1)
		{
			NPC_FreeCombatPoint(actor->NPC->combatPoint, qfalse);
			actor->NPC->combatPoint = -1;
		}

		VectorCopy(troop->formHead, order->position);
		order->combatPoint = -1;

		if (nextState == TS_ATTACK_FLANK && actorIndex >= 4)
		{
			int cpFlags = CP_HAS_ROUTE | CP_AVOID_ENEMY | CP_CLEAR | CP_COVER | CP_FLANK | CP_APPROACH_ENEMY;
			order->combatPoint = NPC_FindCombatPointRetry(actor->r.currentOrigin, actor->r.currentOrigin,
				actor->r.currentOrigin, &cpFlags, 128.0f, 0);
			if (order->combatPoint != -1 && cpFlags & CP_CLEAR)
			{
				VectorCopy(level.combatPoints[order->combatPoint].origin, order->position);
				NPC_SetCombatPoint(order->combatPoint);
			}
			else
			{
				order->combatPoint = -1;
				VectorMA(order->position, fwdScale, troop->formFwd, order->position);
				VectorMA(order->position, sideScale, troop->formRight, order->position);
			}
		}
		else if (nextState != TS_ADVANCE)
		{
			// regroup, search, formation, cover / line, flank (the first 4), surround, cover
			VectorMA(order->position, fwdScale, troop->formFwd, order->position);
			VectorMA(order->position, sideScale, troop->formRight, order->position);
		}

		if (nextState >= TS_ATTACK)
		{
			vec3_t orderUp;
			VectorCopy(order->position, orderUp);
			orderUp[2] += 10.0f;
			trap->Trace(&trace, order->position, actor->r.mins, actor->r.maxs, orderUp, actor->s.number,
				CONTENTS_SOLID | CONTENTS_TERRAIN | CONTENTS_MONSTERCLIP | CONTENTS_BOTCLIP, qfalse, 0, 0);
			if (trace.startsolid || trace.allsolid)
			{
				int cpFlags = CP_HAS_ROUTE | CP_AVOID_ENEMY | CP_CLEAR | CP_COVER | CP_FLANK | CP_APPROACH_ENEMY;
				order->combatPoint = NPC_FindCombatPointRetry(actor->r.currentOrigin, actor->r.currentOrigin,
					actor->r.currentOrigin, &cpFlags, 128.0f, 0);
				if (order->combatPoint != -1)
				{
					VectorCopy(level.combatPoints[order->combatPoint].origin, order->position);
					NPC_SetCombatPoint(order->combatPoint);
				}
				else
				{
					VectorCopy(troop->orders[0].position, order->position);
				}
			}
		}

		RestoreNPCGlobals();
	}

	troop->reform = qfalse;
	troop->state = nextState;
}

// Update - primary "think"
static void Troop_Update(troop_t* troop)
{
	if (!troop->numActors)
	{
		return;
	}

	Troop_ScanForTarget(troop, 0);
	if (!troop->target)
	{
		return;
	}

	troopState_t nextState = troop->state;
	const int timeSinceLastSeen = level.time - troop->targetVisibleStopTime;

	if (timeSinceLastSeen < 2000)
	{
		// Attack
		if (troop->state < TS_ATTACK)
		{
			if (Troop_InFormation(troop))
			{
				nextState = troop->numActors > 4 ? TS_ATTACK_FLANK : TS_ATTACK_LINE;
			}
			else
			{
				nextState = TS_ATTACK_SURROUND; // (SP: TS_ATTACK_COVER when enough cover is near, never so far)
			}
		}
	}
	else if (!Troop_InFormation(troop))
	{
		nextState = TS_ADVANCE_REGROUP;
	}
	else if (Troop_TargetLastKnownPositionVisited(troop))
	{
		nextState = TS_ADVANCE_SEARCH;
	}
	else
	{
		nextState = timeSinceLastSeen < 10000 ? TS_ADVANCE_COVER : TS_ADVANCE_FORMATION;
	}

	Troop_LeaderIssueAndUpdateOrders(troop, nextState);
}

// MergeInto - Merges all actors into another troop
static void Troop_MergeInto(troop_t* troop, troop_t* other)
{
	for (int i = 0; i < troop->numActors; i++)
	{
		gentity_t* actor = troop->actors[i];
		if (!actor || !actor->NPC || !actor->client)
		{
			continue;
		}
		actor->client->leader = NULL;
		trooperTroop[actor->s.number] = 0;
		Troop_AddActor(other, actor);
	}
	troop->numActors = 0;

	if (!other->target && troop->target)
	{
		other->target = troop->target;
		other->targetIndex = troop->targetIndex;
		VectorCopy(troop->targetLastKnownPosition, other->targetLastKnownPosition);
		other->targetLastKnownPositionVisited = troop->targetLastKnownPositionVisited;
		other->targetLastKnownTime = troop->targetLastKnownTime;
		other->targetVisibleStartTime = troop->targetVisibleStartTime;
		other->targetVisibleStopTime = troop->targetVisibleStopTime;
		other->targetVisible = troop->targetVisible;
		VectorCopy(troop->targetVisiblePosition, other->targetVisiblePosition);
		Troop_LeaderIssueAndUpdateOrders(other, troop->state);
	}
}

static gentity_t* Troop_TooCloseToTroopMember(const troop_t* troop, const gentity_t* actor)
{
	for (int i = 0; i < troop->numActors; i++)
	{
		if (!troop->actors[i])
		{
			continue;
		}
		if (actor == troop->actors[i])
		{
			return NULL;
		}
		if (Distance(actor->r.currentOrigin, troop->actors[i]->r.currentOrigin) < troop->formSpacingFwd * 0.5f)
		{
			return troop->actors[i];
		}
	}
	return NULL;
}

// a level start forgets every troop; once per frame: dead / freed troopers drop out, then each troop thinks (SP
// Troop_Reset / NPC_LeaveTroop / Troop_Update)
static void Troop_FrameCheck(void)
{
	if (troopLevel != level.startTime)
	{
		troopLevel = level.startTime;
		troopUpdateTime = -1;
		memset(troops, 0, sizeof troops);
		memset(trooperTroop, 0, sizeof trooperTroop);
		memset(trooperKneelTime, 0, sizeof trooperKneelTime);
		memset(trooperLastMoveTime, 0, sizeof trooperLastMoveTime);
	}
	if (troopUpdateTime == level.time)
	{
		return;
	}
	troopUpdateTime = level.time;

	for (int t = 0; t < MAX_TROOPS; t++)
	{
		troop_t* troop = &troops[t];
		if (!troop->inUse)
		{
			continue;
		}
		for (int i = troop->numActors - 1; i >= 0; i--)
		{
			const gentity_t* actor = troop->actors[i];
			if (!actor || !actor->inuse || !actor->NPC || !actor->client || actor->health <= 0
				|| trooperTroop[actor->s.number] != t + 1)
			{
				Troop_RemoveActor(troop, actor);
			}
		}
		if (troop->target && (!troop->target->inuse || troop->target->health <= 0))
		{
			troop->target = NULL;
		}
		if (!troop->numActors)
		{
			troop->inUse = qfalse;
			continue;
		}
		Troop_Update(troop);
	}
}

// Join a troop / a leader merges troops
static void Trooper_UpdateTroop(gentity_t* actor)
{
	trace_t trace;

	if (!trooperTroop[actor->s.number])
	{
		// Try To Join A Troop
		float closestDist = 0;
		troop_t* closestTroop = NULL;

		for (int t = 0; t < MAX_TROOPS; t++)
		{
			troop_t* troop = &troops[t];
			if (!troop->inUse || troop->team != actor->client->playerTeam || !troop->numActors
				|| troop->numActors >= MAX_ENTS_PER_TROOP)
			{
				continue;
			}
			const float curDist = Troop_DistanceSq(troop, actor);
			if (curDist < MAX_TROOP_JOIN_DIST2 && (!closestDist || curDist < closestDist))
			{
				// Only Join A Troop If You Can See The Leader
				trap->Trace(&trace, actor->r.currentOrigin, actor->r.mins, actor->r.maxs,
					troop->actors[0]->r.currentOrigin, actor->s.number,
					CONTENTS_SOLID | CONTENTS_TERRAIN | CONTENTS_MONSTERCLIP | CONTENTS_BOTCLIP, qfalse, 0, 0);
				if (!trace.allsolid && !trace.startsolid
					&& (trace.fraction >= 1.0f || trace.entityNum == troop->actors[0]->s.number))
				{
					closestDist = curDist;
					closestTroop = troop;
				}
			}
		}

		if (closestTroop)
		{
			Troop_AddActor(closestTroop, actor);
		}
		else
		{
			// If We Couldn't Find One, Create A New Troop
			for (int t = 0; t < MAX_TROOPS; t++)
			{
				if (!troops[t].inUse)
				{
					Troop_Initialize(&troops[t]);
					Troop_AddActor(&troops[t], actor);
					break;
				}
			}
		}
	}
	else if (actor->client->leader == actor)
	{
		// If This Is A Leader, Then He Is Responsible For Merging Troops
		troop_t* mine = Troop_Get(trooperTroop[actor->s.number]);
		float closestDist = 0;
		troop_t* closestTroop = NULL;

		for (int t = 0; t < MAX_TROOPS && mine; t++)
		{
			troop_t* troop = &troops[t];
			if (!troop->inUse || troop == mine || !troop->numActors || troop->team != mine->team
				|| troop->numActors + mine->numActors > MAX_ENTS_PER_TROOP)
			{
				continue;
			}
			const float curDist = Troop_DistanceSq(troop, actor);
			if (curDist < MAX_TROOP_MERGE_DIST2 && (!closestDist || curDist < closestDist))
			{
				closestDist = curDist;
				closestTroop = troop;
			}
		}

		if (closestTroop)
		{
			Troop_MergeInto(mine, closestTroop);
			mine->inUse = qfalse;
		}
	}
}

// ------------------------------------ the trooper ------------------------------------

static qboolean Trooper_UpdateSmackAway(gentity_t* actor, gentity_t* target)
{
	if (actor->client->ps.legsAnim == BOTH_MELEE1)
	{
		if (TIMER_Done(actor, "Trooper_SmackAway"))
		{
			vec3_t actorToTgt;
			VectorSubtract(target->r.currentOrigin, actor->r.currentOrigin, actorToTgt);
			if (VectorNormalize(actorToTgt) < 100.0f)
			{
				G_Throw(target, actorToTgt, 200.0f);
			}
		}
		return qtrue;
	}
	return qfalse;
}

static void Trooper_SmackAway(gentity_t* actor)
{
	if (actor->client->ps.legsAnim != BOTH_MELEE1)
	{
		NPC_SetAnim(actor, SETANIM_BOTH, BOTH_MELEE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		TIMER_Set(actor, "Trooper_SmackAway", actor->client->ps.torsoTimer / 4);
	}
}

static qboolean Trooper_Kneeling(const gentity_t* actor)
{
	return actor->NPC->aiFlags & NPCAI_KNEEL || actor->client->ps.legsAnim == BOTH_STAND_TO_KNEEL ? qtrue : qfalse;
}

static void Trooper_KneelDown(gentity_t* actor)
{
	if (!Trooper_Kneeling(actor) && level.time > trooperKneelTime[actor->s.number])
	{
		NPC_SetAnim(actor, SETANIM_BOTH, BOTH_STAND_TO_KNEEL, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		actor->NPC->aiFlags |= NPCAI_KNEEL;
		trooperKneelTime[actor->s.number] = level.time + Q_irand(3000, 6000);
	}
}

static void Trooper_StandUp(gentity_t* actor, const qboolean always)
{
	if (Trooper_Kneeling(actor) && (always || level.time > trooperKneelTime[actor->s.number]))
	{
		actor->NPC->aiFlags &= ~NPCAI_KNEEL;
		NPC_SetAnim(actor, SETANIM_BOTH, BOTH_KNEEL_TO_STAND, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		trooperKneelTime[actor->s.number] = level.time + Q_irand(3000, 6000);
	}
}

static int Trooper_CanHitTarget(gentity_t* actor, const gentity_t* target, const troop_t* troop,
	float* muzzleToTargetDistance, vec3_t muzzleToTarget)
{
	trace_t tr;
	vec3_t muzzlePoint, muzzleDirection;

	CalcEntitySpot(actor, SPOT_WEAPON, muzzlePoint);
	VectorSubtract(troop->targetVisiblePosition, muzzlePoint, muzzleToTarget);
	*muzzleToTargetDistance = VectorNormalize(muzzleToTarget);

	AngleVectors(actor->r.currentAngles, muzzleDirection, NULL, NULL);

	// Aiming In The Right Direction?
	if (DotProduct(muzzleDirection, muzzleToTarget) > 0.95f)
	{
		// Clear Line Of Sight To Target?
		trap->Trace(&tr, muzzlePoint, NULL, NULL, troop->targetVisiblePosition, actor->s.number, MASK_SHOT, qfalse, 0,
			0);
		if (tr.startsolid || tr.allsolid)
		{
			return ENTITYNUM_NONE;
		}
		if (tr.entityNum == target->s.number || tr.fraction > 0.9f)
		{
			return target->s.number;
		}
		return tr.entityNum;
	}
	return ENTITYNUM_NONE;
}

// Run The Per Trooper Update
static void Trooper_Think(gentity_t* actor)
{
	troop_t* troop = Troop_Get(trooperTroop[actor->s.number]);
	gentity_t* target = troop ? troop->target : NULL;

	if (!target)
	{
		NPC_BSST_Default();
		return;
	}

	G_SetEnemy(actor, target);

	qboolean atPos = DistanceHorizontal(actor->r.currentOrigin, actor->pos1) < 10.0f ? qtrue : qfalse;
	int traceTgt = ENTITYNUM_NONE;
	qboolean traced = qfalse;
	qboolean inSmackAway = qfalse;
	float muzzleToTargetDistance = 0.0f;
	vec3_t muzzleToTarget = { 0, 0, 0 };

	if (actor->NPC->combatPoint != -1)
	{
		traceTgt = Trooper_CanHitTarget(actor, target, troop, &muzzleToTargetDistance, muzzleToTarget);
		traced = qtrue;
		if (traceTgt == target->s.number)
		{
			atPos = qtrue;
		}
	}

	// Smack!
	if (Trooper_UpdateSmackAway(actor, target))
	{
		traced = qtrue;
		atPos = qtrue;
		inSmackAway = qtrue;
	}

	// If Too Close To A Troop Member, Get Out Of His Way
	const gentity_t* fleeFrom = Troop_TooCloseToTroopMember(troop, actor);
	if (fleeFrom)
	{
		vec3_t away;
		VectorSubtract(actor->r.currentOrigin, fleeFrom->r.currentOrigin, away);
		away[2] = 0;
		VectorNormalize(away);
		G_UcmdMoveForDir(actor, &NPCS.ucmd, away);
		atPos = qfalse;
	}
	else if (atPos)
	{
		// If In Position, Stop Moving
		NPCS.NPCInfo->goalEntity = NULL;
		NPCS.ucmd.forwardmove = NPCS.ucmd.rightmove = 0;
	}
	else
	{
		// Otherwise, Try To Get To Position
		Trooper_StandUp(actor, qtrue);
		NPC_SetMoveGoal(actor, actor->pos1, 10, qtrue, -1, NULL);
		NPCS.NPCInfo->goalEntity = NPCS.NPCInfo->tempGoal;
		NPCS.NPCInfo->goalRadius = 10;
		const qboolean moveSuccess = NPC_MoveToGoal(qtrue);
		if (moveSuccess && (NPCS.ucmd.forwardmove || NPCS.ucmd.rightmove))
		{
			trooperLastMoveTime[actor->s.number] = level.time;
		}

		// If No Way To Get To Position, Stay Here
		if (!moveSuccess || level.time - trooperLastMoveTime[actor->s.number] > 4000)
		{
			atPos = qtrue;
		}
	}

	// If There And Target Was Recently Visible
	if (atPos && level.time - troop->targetVisibleStopTime < 1500)
	{
		if (!traced)
		{
			traceTgt = Trooper_CanHitTarget(actor, target, troop, &muzzleToTargetDistance, muzzleToTarget);
		}

		if (traceTgt == target->s.number)
		{
			// Shoot!
			WeaponThink();
		}
		else if (!inSmackAway)
		{
			if (Trooper_Kneeling(actor))
			{
				// Otherwise, If Kneeling, Get Up!
				Trooper_StandUp(actor, qfalse);
			}
			else if (muzzleToTargetDistance < 40.0f)
			{
				// If The Enemy Is Close Enough, Smack Him Away
				Trooper_SmackAway(actor);
			}
			else if (traceTgt != ENTITYNUM_NONE
				&& traceTgt != ENTITYNUM_WORLD
				&& g_entities[traceTgt].client
				&& g_entities[traceTgt].NPC
				&& g_entities[traceTgt].client->playerTeam == actor->client->playerTeam
				&& NPC_IsTrooper(&g_entities[traceTgt])
				&& VectorLength(g_entities[traceTgt].client->ps.velocity) < 1.0f
				&& !(g_entities[traceTgt].NPC->aiFlags & NPCAI_KNEEL))
			{
				// If We Would Hit A Friend, Ask Him To Kneel
				Trooper_KneelDown(&g_entities[traceTgt]);
			}
		}

		// Convert To Angles And Set That As Our Desired Look Direction
		vec3_t angles;
		if (muzzleToTargetDistance > 100)
		{
			vectoangles(muzzleToTarget, angles);
		}
		else
		{
			vec3_t aim;
			VectorCopy(troop->targetVisiblePosition, aim);
			aim[2] -= 20.0f; // Aim Lower
			VectorSubtract(aim, actor->r.currentOrigin, aim);
			VectorNormalize(aim);
			vectoangles(aim, angles);
		}
		NPCS.NPCInfo->desiredYaw = angles[YAW];
		NPCS.NPCInfo->desiredPitch = angles[PITCH];
	}

	NPC_UpdateFiringAngles(qtrue, qtrue);
	NPC_UpdateAngles(qtrue, qtrue);

	if (Trooper_Kneeling(actor))
	{
		NPCS.ucmd.upmove = -127; // Set Crouch Height
	}
}

// NPC.c NPC_BehaviorSet_Trooper
void NPC_BehaviorSet_HazardTroop(const int b_state)
{
	Troop_FrameCheck();
	Trooper_UpdateTroop(NPCS.NPC);
	switch (b_state)
	{
	case BS_INVESTIGATE:
		NPC_BSST_Investigate();
		break;
	case BS_SLEEP:
		NPC_BSST_Sleep();
		break;
	default:
		Trooper_Think(NPCS.NPC);
		break;
	}
}