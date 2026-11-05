// leave this line at the top of all AI_xxxx.cpp files for PCH reasons...
#include "b_local.h"
#include "NPC_steer.h"

/*
-------------------------
Animals: SP's AI_Animal.cpp (DH 2026-11-03, NPC parity batch C)

As SP, a riderless animal vehicle (NPC.c NPC_BehaviorSet_Animal) runs this. Animals keep to packs: up to MAX_PACKS
leaders, an animal within JOIN_PACK_DISTANCE of a leader follows him (and a leader that finds another one joins his
pack), one more than LEAVE_PACK_DISTANCE away leaves; without a pack it leads its own. A follower keeps to its leader's
follow position (STEER_FollowLeader) and stops when he stops, a leader wanders: to a random neighbouring waypoint, about
without a path, or stands still a while. A charmed animal walks to what charmed it (charmedTime), a docile one
(confusionTime) stays put, a frightened one (investigateSoundDebounceTime) runs from investigateGoal. (SJE's SP also counts its insanity / dark charm times; MP has neither.)
MP has no navigator paths: the "path" is the chosen waypoint, followed with NAV_GoToPos until reached (NPC_steer.c).
-------------------------
*/

#define MAX_PACKS 10

#define LEAVE_PACK_DISTANCE 1000
#define JOIN_PACK_DISTANCE 800
#define ANIMAL_PATH_REACHED 40.0f

static gentity_t* mPacks[MAX_PACKS];
static int mNumPacks;
static int mPacksLevelStart = -1; // (level.startTime the packs belong to: emptied on a new map)

// the waypoint a leader is walking to, + 1 (0: none) (SP NAV::FindPath / HasPath / UpdatePath)
static int animalPathNode[MAX_GENTITIES];
static int animalPathFreeTime[MAX_GENTITIES];

static void NPC_AnimalPackRemove(const int i)
{
	// (ratl erase_swap)
	mPacks[i] = mPacks[mNumPacks - 1];
	mPacks[--mNumPacks] = NULL;
}

static qboolean NPC_AnimalHasPath(const gentity_t* ent)
{
	return animalPathFreeTime[ent->s.number] == ent->freetime && animalPathNode[ent->s.number] > 0
		? qtrue
		: qfalse;
}

static void NPC_AnimalClearPath(const gentity_t* ent)
{
	animalPathNode[ent->s.number] = 0;
	animalPathFreeTime[ent->s.number] = ent->freetime;
}

static void NPC_AnimalFindPath(const gentity_t* ent, const int node)
{
	animalPathNode[ent->s.number] = node >= 0 ? node + 1 : 0;
	animalPathFreeTime[ent->s.number] = ent->freetime;
}

// follows the path: qfalse when there is none (any more)
static qboolean NPC_AnimalUpdatePath(gentity_t* ent)
{
	vec3_t pos;
	if (!NAV_NodePosition(animalPathNode[ent->s.number] - 1, pos)
		|| STEER_Reached(ent, pos, ANIMAL_PATH_REACHED)
		|| !NAV_GoToPos(ent, pos, ANIMAL_PATH_REACHED))
	{
		NPC_AnimalClearPath(ent);
		return qfalse;
	}
	return qtrue;
}

////////////////////////////////////////////////////////////////////////////////////////
// Update The Packs, Delete Dead Leaders, Join / Split Packs, Find MY Leader
////////////////////////////////////////////////////////////////////////////////////////
static gentity_t* NPC_AnimalUpdateLeader(void)
{
	// NPC must exist and must have a client
	if (!NPCS.NPC || !NPCS.NPC->client)
	{
		return NULL;
	}
	if (mPacksLevelStart != level.startTime)
	{
		memset(mPacks, 0, sizeof mPacks);
		mNumPacks = 0;
		mPacksLevelStart = level.startTime;
	}

	gentity_t* closestLeader = NULL;
	float closestDist = 0.0f;
	int myLeaderNum = -1;

	for (int i = 0; i < mNumPacks;)
	{
		gentity_t* ent = mPacks[i];

		// Remove dead or invalid leaders
		if (!ent || !ent->inuse || !ent->client || ent->health <= 0)
		{
			if (NPCS.NPC->client->leader == ent)
			{
				NPCS.NPC->client->leader = NULL;
			}

			NPC_AnimalPackRemove(i);
			continue; // do NOT increment i after erase_swap
		}

		// Skip self
		if (ent == NPCS.NPC)
		{
			myLeaderNum = i;
			i++;
			continue;
		}

		// Compute distance
		const float dist = Distance(ent->r.currentOrigin, NPCS.NPC->r.currentOrigin);

		if (!closestLeader || dist < closestDist)
		{
			closestLeader = ent;
			closestDist = dist;
		}

		i++;
	}

	// Join a nearby leader
	if (closestLeader && closestDist < JOIN_PACK_DISTANCE)
	{
		// If I was a leader, remove myself from the pack list
		if (NPCS.NPC->client->leader == NPCS.NPC && myLeaderNum >= 0 && myLeaderNum < mNumPacks)
		{
			NPC_AnimalPackRemove(myLeaderNum);
		}

		NPCS.NPC->client->leader = closestLeader;
	}

	// If I have a leader, validate it
	if (NPCS.NPC->client->leader)
	{
		gentity_t* L = NPCS.NPC->client->leader;

		// Leader dead or removed?
		if (!L->inuse || L->health <= 0)
		{
			NPCS.NPC->client->leader = NULL;
		}
		// Leader has a leader? Follow the chain
		else if (L->client && L->client->leader && L->client->leader != L)
		{
			NPCS.NPC->client->leader = L->client->leader;
		}
		// Leader too far away?
		else if (Distance(L->r.currentOrigin, NPCS.NPC->r.currentOrigin) > LEAVE_PACK_DISTANCE)
		{
			NPCS.NPC->client->leader = NULL;
		}
	}
	else
	{
		// No leader - become one
		if (mNumPacks < MAX_PACKS)
		{
			NPCS.NPC->client->leader = NPCS.NPC;
			mPacks[mNumPacks++] = NPCS.NPC;
		}
	}

	return NPCS.NPC->client->leader;
}

/*
-------------------------
NPC_BSAnimal_Default
-------------------------
*/
void NPC_BSAnimal_Default(void)
{
	if (!NPCS.NPC || !NPCS.NPC->client)
	{
		return;
	}

	// Update The Leader
	//-------------------
	gentity_t* leader = NPC_AnimalUpdateLeader();

	// (SP worked out the closest threat here too, but never used it)
	NPC_CheckAlertEvents(qtrue, qtrue, -1, qfalse, AEL_MINOR);

	const qboolean evadeThreat = level.time < NPCS.NPCInfo->investigateSoundDebounceTime ? qtrue : qfalse;
	const qboolean charmedDocile = level.time < NPCS.NPCInfo->confusionTime ? qtrue : qfalse;
	const qboolean charmedApproach = level.time < NPCS.NPCInfo->charmedTime ? qtrue : qfalse;

	STEER_Activate(NPCS.NPC);
	{
		// Charmed Approach - Walk TOWARD The Threat Location
		//----------------------------------------------------
		if (charmedApproach)
		{
			NAV_GoToPos(NPCS.NPC, NPCS.NPCInfo->investigateGoal, ANIMAL_PATH_REACHED);
		}

		// Charmed Docile - Stay Put
		//---------------------------
		else if (charmedDocile)
		{
			NPC_AnimalClearPath(NPCS.NPC);
			STEER_Stop(NPCS.NPC);
		}

		// Run Away From This Threat
		//---------------------------
		else if (evadeThreat)
		{
			NPC_AnimalClearPath(NPCS.NPC);
			STEER_Flee(NPCS.NPC, NPCS.NPCInfo->investigateGoal, 1.0f);
		}

		// Normal Behavior
		//-----------------
		else
		{
			// Follow Our Pack Leader!
			//-------------------------
			if (leader && leader != NPCS.NPC)
			{
				const float followDist = 100.0f;
				const float curDist = Distance(NPCS.NPC->r.currentOrigin, STEER_FollowPos(leader));

				// Update The Leader's Follow Position
				//-------------------------------------
				STEER_FollowLeader(NPCS.NPC, leader, followDist);

				const qboolean inSeekRange = curDist < followDist * 10.0f ? qtrue : qfalse;
				const qboolean leaderStop = level.time - STEER_LastMoveTime(leader) > 500 ? qtrue : qfalse;

				// If Close Enough, Dump Any Existing Path
				//-----------------------------------------
				if (inSeekRange)
				{
					NPC_AnimalClearPath(NPCS.NPC);

					// If The Leader Isn't Moving, Stop
					//----------------------------------
					if (leaderStop)
					{
						STEER_Stop(NPCS.NPC);
					}

					// Otherwise, Try To Get To The Follow Position
					//----------------------------------------------
					else
					{
						STEER_Seek(NPCS.NPC, STEER_FollowPos(leader), fabsf(followDist) / 2.0f/*slowing distance*/,
							1.0f/*wight*/,
							STEER_ResultSpeed(leader));
					}
				}

				// Otherwise, Get A Path To The Follow Position
				//----------------------------------------------
				else
				{
					NAV_GoToPos(NPCS.NPC, STEER_FollowPos(leader), ANIMAL_PATH_REACHED);
				}
				STEER_Separation(NPCS.NPC, 4.0f);
				STEER_AvoidCollisions(NPCS.NPC, leader);
			}

			// Leader AI - Basically Wander
			//------------------------------
			else
			{
				// Are We Doing A Path?
				//----------------------
				qboolean hasPath = NPC_AnimalHasPath(NPCS.NPC);
				if (hasPath)
				{
					hasPath = NPC_AnimalUpdatePath(NPCS.NPC); // Follow The Path
				}

				if (!hasPath)
				{
					// If Debounce Time Has Expired, Choose A New Sub State
					//------------------------------------------------------
					if (NPCS.NPCInfo->investigateDebounceTime < level.time)
					{
						// Clear Out Flags From The Previous Substate
						//--------------------------------------------
						NPCS.NPCInfo->aiFlags &= ~NPCAI_OFF_PATH;
						NPCS.NPCInfo->aiFlags &= ~NPCAI_WALKING;

						// Pick Another Spot
						//-------------------
						const int NEXTSUBSTATE = Q_irand(0, 10);

						const qboolean randomPathNode = NEXTSUBSTATE < 8 ? qtrue : qfalse; //(NEXTSUBSTATE<9);
						const qboolean pathlessWander = NEXTSUBSTATE < 9 ? qtrue : qfalse; //false;

						// Random Path Node
						//------------------
						if (randomPathNode)
						{
							// Sometimes, Walk
							//-----------------
							if (Q_irand(0, 1) == 0)
							{
								NPCS.NPCInfo->aiFlags |= NPCAI_WALKING;
							}

							NPCS.NPCInfo->investigateDebounceTime = level.time + Q_irand(3000, 10000);
							NPC_AnimalFindPath(NPCS.NPC, NAV_ChooseRandomNeighbor(NAV_NearestNode(NPCS.NPC)));
						}

						// Pathless Wandering
						//--------------------
						else if (pathlessWander)
						{
							// Sometimes, Walk
							//-----------------
							if (Q_irand(0, 1) == 0)
							{
								NPCS.NPCInfo->aiFlags |= NPCAI_WALKING;
							}

							NPCS.NPCInfo->investigateDebounceTime = level.time + Q_irand(3000, 10000);
							NPCS.NPCInfo->aiFlags |= NPCAI_OFF_PATH;
						}

						// Just Stand Here
						//-----------------
						else
						{
							NPCS.NPCInfo->investigateDebounceTime = level.time + Q_irand(2000, 6000);
							NPC_SetAnim(NPCS.NPC, SETANIM_BOTH,
								Q_irand(0, 1) == 0 ? BOTH_GUARD_LOOKAROUND1 : BOTH_GUARD_IDLE1,
								SETANIM_FLAG_NORMAL);
						}
					}

					// Ok, So We Don't Have A Path, And Debounce Time Is Still Active, So We Are Either Wandering Or Looking Around
					//--------------------------------------------------------------------------------------------------------------
					else
					{
						if (NPCS.NPCInfo->aiFlags & NPCAI_OFF_PATH)
						{
							STEER_Wander(NPCS.NPC);
							STEER_AvoidCollisions(NPCS.NPC, NULL);
						}
						else
						{
							STEER_Stop(NPCS.NPC);
						}
					}
				}
			}
		}
	}
	STEER_DeActivate(NPCS.NPC, &NPCS.ucmd);

	NPC_UpdateAngles(qtrue, qtrue);
}