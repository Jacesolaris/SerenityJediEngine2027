/*
===========================================================================
Copyright (C) 2026, SerenityJediEngine2027 / MovieDuels contributors

This file is part of the SerenityJediEngine2027 / MovieDuels source code.

This program is free software; you can redistribute it and/or modify it
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

// NPC_AI_Path.c - MP NPC route finding over the bot waypoints (2026-11-02, Warzone port: UQ1's idea of NPCs
// using the bot routes, own code).
//
// Most MP maps have no SP navigation waypoints, so NAVNEW_MoveToGoal finds no route and an NPC that cannot
// walk straight to its goal just stands and faces it. When that happens NPC_GetMoveDirectionAltRoute
// (NPC_move.c) asks NPC_WPRoute_Direction, which runs A* over the bot route of the map (botroutes/<map>.wnt,
// gWPArray) and steers the NPC along it, waypoint by waypoint. Maps with SP nav keep using SP nav first.
//
// g_npcWaypointNav: 1 = on (default), 0 = off (old behaviour).
// Only the main thread; no engine calls besides traces; paths are kept per entity in this file.

#include "b_local.h"
#include "g_nav.h"

// bot route (ai_wpnav.c)
extern wpobject_t* gWPArray[MAX_WPARRAY_SIZE];
extern int gWPNum;
extern void LoadPath_ThisLevel(void);

#define NPCPATH_MAX_NODES		64		// waypoints kept per NPC (replanned when used up)
#define NPCPATH_REPLAN_TIME		3000	// ms before a path is planned again anyway
#define NPCPATH_GOAL_MOVED		160.0f	// replan when the goal moved this far from where we planned to
#define NPCPATH_REACHED_XY		48.0f	// waypoint reached when this close (flat distance) ...
#define NPCPATH_REACHED_Z		72.0f	// ... and this close in height
#define NPCPATH_NEAR_RANGE		768.0f	// start / end waypoint must be within this range and visible
#define NPCPATH_STUCK_TIME		1500	// no progress for this long: replan and avoid the waypoint
#define NPCPATH_FAIL_RETRY		1000	// after a failed plan, try again this much later
#define NPCPATH_HEAP_SIZE		(MAX_WPARRAY_SIZE * 4)	// open list (a node can be in it more than once)

typedef struct npcPath_s
{
	short nodes[NPCPATH_MAX_NODES];	// waypoint indices, start first
	int numNodes;
	int pos;						// next waypoint to walk to
	vec3_t goal;					// goal position the path was planned for
	int planTime;					// level.time of the plan
	int failTime;					// level.time a plan failed (no retry before NPCPATH_FAIL_RETRY)
	int progressTime;				// last time we got closer to the current waypoint
	float bestDist;					// closest we got to the current waypoint
	int avoidNode;					// waypoint we got stuck on, left out of the next plan
} npcPath_t;

static npcPath_t s_npcPath[MAX_GENTITIES];

// A* scratch over the waypoints (one search at a time)
static float s_astarG[MAX_WPARRAY_SIZE];
static int s_astarParent[MAX_WPARRAY_SIZE];
static int s_astarStamp[MAX_WPARRAY_SIZE];		// == s_astarSearch: node seen in this search
static qboolean s_astarClosed[MAX_WPARRAY_SIZE];
static int s_astarHeap[NPCPATH_HEAP_SIZE + 1];	// node indices, 1-based binary heap on f
static float s_astarF[MAX_WPARRAY_SIZE];
static int s_astarSearch = 0;

static int NPC_WPRoute_Enabled(void)
{
	static vmCvar_t g_npcWaypointNav;
	static qboolean registered = qfalse;
	if (!registered)
	{
		trap->Cvar_Register(&g_npcWaypointNav, "g_npcWaypointNav", "1", CVAR_ARCHIVE);
		registered = qtrue;
	}
	else
	{
		trap->Cvar_Update(&g_npcWaypointNav);
	}
	return g_npcWaypointNav.integer;
}

// G_InitBots does not load the bot route in Missions (or with bot_enable 0): load it once per map when needed
static qboolean NPC_WPRoute_Ready(void)
{
	static int s_loadTriedMap = -1;
	if (gWPNum > 0)
	{
		return qtrue;
	}
	if (s_loadTriedMap != level.startTime)
	{
		s_loadTriedMap = level.startTime;
		LoadPath_ThisLevel();
	}
	return gWPNum > 0 ? qtrue : qfalse;
}

static qboolean NPC_WPRoute_Valid(const int wp)
{
	return wp >= 0 && wp < gWPNum && wp < MAX_WPARRAY_SIZE && gWPArray[wp] && gWPArray[wp]->inuse ? qtrue : qfalse;
}

// nearest waypoint within range that the box can reach in a straight line
static int NPC_WPRoute_Nearest(const gentity_t* self, const vec3_t org, const int ignoreEnt, const int avoid)
{
	enum { NUM_CANDIDATES = 6 };
	int cand[NUM_CANDIDATES];
	float candDist[NUM_CANDIDATES];
	int numCand = 0;
	int i;
	vec3_t mins, maxs;

	for (i = 0; i < gWPNum; i++)
	{
		float d;
		int k;
		if (i == avoid || !NPC_WPRoute_Valid(i))
		{
			continue;
		}
		d = DistanceSquared(org, gWPArray[i]->origin);
		if (d > NPCPATH_NEAR_RANGE * NPCPATH_NEAR_RANGE)
		{
			continue;
		}
		// keep the closest few, sorted
		if (numCand == NUM_CANDIDATES && d >= candDist[NUM_CANDIDATES - 1])
		{
			continue;
		}
		k = numCand < NUM_CANDIDATES ? numCand++ : NUM_CANDIDATES - 1;
		while (k > 0 && candDist[k - 1] > d)
		{
			cand[k] = cand[k - 1];
			candDist[k] = candDist[k - 1];
			k--;
		}
		cand[k] = i;
		candDist[k] = d;
	}

	// a slim box (step height off the floor) so small ledges and stairs don't block the check
	VectorSet(mins, -8.0f, -8.0f, -8.0f);
	VectorSet(maxs, 8.0f, 8.0f, 8.0f);
	for (i = 0; i < numCand; i++)
	{
		trace_t tr;
		vec3_t start;
		VectorCopy(org, start);
		start[2] += 16.0f;
		trap->Trace(&tr, start, mins, maxs, gWPArray[cand[i]]->origin, ignoreEnt, self->clipmask & ~CONTENTS_BODY, qfalse, 0, 0);
		if (!tr.startsolid && !tr.allsolid && tr.fraction >= 1.0f)
		{
			return cand[i];
		}
	}
	return -1;
}

static void NPC_WPRoute_HeapPush(int* heapSize, const int node)
{
	int i = ++(*heapSize);
	while (i > 1 && s_astarF[s_astarHeap[i / 2]] > s_astarF[node])
	{
		s_astarHeap[i] = s_astarHeap[i / 2];
		i /= 2;
	}
	s_astarHeap[i] = node;
}

static int NPC_WPRoute_HeapPop(int* heapSize)
{
	const int top = s_astarHeap[1];
	const int last = s_astarHeap[(*heapSize)--];
	int i = 1;
	while (2 * i <= *heapSize)
	{
		int child = 2 * i;
		if (child < *heapSize && s_astarF[s_astarHeap[child + 1]] < s_astarF[s_astarHeap[child]])
		{
			child++;
		}
		if (s_astarF[last] <= s_astarF[s_astarHeap[child]])
		{
			break;
		}
		s_astarHeap[i] = s_astarHeap[child];
		i = child;
	}
	s_astarHeap[i] = last;
	return top;
}

// A* from start to end; fills path (start first, at most maxNodes) and returns its length, 0 = no route
static int NPC_WPRoute_AStar(const int start, const int end, const int jumpLevel, const int avoid, short* path, const int maxNodes)
{
	int heapSize = 0;
	int node;
	int count, i;
	static int s_chain[MAX_WPARRAY_SIZE];

	if (++s_astarSearch <= 0)
	{
		// wrapped: clear the stamps
		memset(s_astarStamp, 0, sizeof s_astarStamp);
		s_astarSearch = 1;
	}

	s_astarStamp[start] = s_astarSearch;
	s_astarClosed[start] = qfalse;
	s_astarG[start] = 0.0f;
	s_astarParent[start] = -1;
	s_astarF[start] = Distance(gWPArray[start]->origin, gWPArray[end]->origin);
	NPC_WPRoute_HeapPush(&heapSize, start);

	node = -1;
	while (heapSize > 0)
	{
		const wpobject_t* wp;
		int n;
		node = NPC_WPRoute_HeapPop(&heapSize);
		if (s_astarClosed[node])
		{
			continue;	// an older, worse copy
		}
		if (node == end)
		{
			break;
		}
		s_astarClosed[node] = qtrue;
		wp = gWPArray[node];

		for (n = 0; n < wp->neighbornum && n < MAX_NEIGHBOR_SIZE; n++)
		{
			const int next = wp->neighbors[n].num;
			float g;
			if (next == avoid || !NPC_WPRoute_Valid(next))
			{
				continue;
			}
			if (wp->neighbors[n].forceJumpTo > jumpLevel)
			{
				continue;	// needs a stronger force jump than we have
			}
			if (s_astarStamp[next] == s_astarSearch && s_astarClosed[next])
			{
				continue;
			}
			g = s_astarG[node] + Distance(wp->origin, gWPArray[next]->origin);
			if (s_astarStamp[next] != s_astarSearch || g < s_astarG[next])
			{
				s_astarStamp[next] = s_astarSearch;
				s_astarClosed[next] = qfalse;
				s_astarG[next] = g;
				s_astarParent[next] = node;
				s_astarF[next] = g + Distance(gWPArray[next]->origin, gWPArray[end]->origin);
				if (heapSize < NPCPATH_HEAP_SIZE)
				{
					NPC_WPRoute_HeapPush(&heapSize, next);
				}
			}
		}
	}

	if (node != end)
	{
		return 0;
	}

	// walk back from the end, then keep the first maxNodes from the start
	count = 0;
	for (i = end; i >= 0 && count < MAX_WPARRAY_SIZE; i = s_astarParent[i])
	{
		s_chain[count++] = i;
	}
	if (count > maxNodes)
	{
		// chain is end..start: the start side is at the back
		for (i = 0; i < maxNodes; i++)
		{
			path[i] = (short)s_chain[count - 1 - i];
		}
		return maxNodes;
	}
	for (i = 0; i < count; i++)
	{
		path[i] = (short)s_chain[count - 1 - i];
	}
	return count;
}

static qboolean NPC_WPRoute_Plan(const gentity_t* self, npcPath_t* p, const vec3_t goal)
{
	const int jumpLevel = self->client ? self->client->ps.fd.forcePowerLevel[FP_LEVITATION] : 0;
	const int start = NPC_WPRoute_Nearest(self, self->r.currentOrigin, self->s.number, p->avoidNode);
	const int end = NPC_WPRoute_Nearest(self, goal, self->s.number, -1);

	p->numNodes = 0;
	p->pos = 0;
	p->planTime = level.time;
	VectorCopy(goal, p->goal);
	p->progressTime = level.time;
	p->bestDist = Q3_INFINITE;

	if (start < 0 || end < 0)
	{
		p->failTime = level.time;
		return qfalse;
	}
	if (start == end)
	{
		p->nodes[0] = (short)start;
		p->numNodes = 1;
		return qtrue;
	}
	p->numNodes = NPC_WPRoute_AStar(start, end, jumpLevel, p->avoidNode, p->nodes, NPCPATH_MAX_NODES);
	if (!p->numNodes)
	{
		p->failTime = level.time;
		return qfalse;
	}
	return qtrue;
}

/*
NPC_WPRoute_Direction

Called when SP nav has no route to the goal. On success writes the direction to walk this frame (towards
the next waypoint on the route) and returns qtrue; qfalse = no route either (the NPC faces its goal as before).
*/
qboolean NPC_WPRoute_Direction(gentity_t* self, const vec3_t goal, vec3_t dir)
{
	npcPath_t* p;
	vec3_t toNode = { 0.0f, 0.0f, 0.0f };
	float flatDist = 0.0f; // (set by the route walk below whenever a waypoint is left to head for)

	if (!self || !self->client || self->s.number < 0 || self->s.number >= MAX_GENTITIES
		|| !NPC_WPRoute_Enabled() || !NPC_WPRoute_Ready())
	{
		return qfalse;
	}
	if (self->client->ps.eFlags2 & EF2_FLYING)
	{
		return qfalse;	// fliers go straight
	}

	p = &s_npcPath[self->s.number];
	if (p->planTime > level.time || p->failTime > level.time)
	{
		// left over from an earlier map
		memset(p, 0, sizeof * p);
		p->avoidNode = -1;
	}

	// plan again when needed
	if (p->numNodes <= 0 || p->pos >= p->numNodes
		|| level.time - p->planTime > NPCPATH_REPLAN_TIME
		|| DistanceSquared(goal, p->goal) > NPCPATH_GOAL_MOVED * NPCPATH_GOAL_MOVED)
	{
		if (p->failTime && level.time - p->failTime < NPCPATH_FAIL_RETRY)
		{
			return qfalse;
		}
		if (p->planTime == 0)
		{
			p->avoidNode = -1;
		}
		if (!NPC_WPRoute_Plan(self, p, goal))
		{
			return qfalse;
		}
		p->failTime = 0;
	}

	// walk the route: skip waypoints we have reached
	while (p->pos < p->numNodes)
	{
		const wpobject_t* wp = gWPArray[p->nodes[p->pos]];
		VectorSubtract(wp->origin, self->r.currentOrigin, toNode);
		flatDist = sqrtf(toNode[0] * toNode[0] + toNode[1] * toNode[1]);
		if (flatDist < NPCPATH_REACHED_XY && fabsf(toNode[2]) < NPCPATH_REACHED_Z)
		{
			p->pos++;
			p->bestDist = Q3_INFINITE;
			p->progressTime = level.time;
			continue;
		}
		break;
	}
	if (p->pos >= p->numNodes)
	{
		// at the last waypoint: head straight for the goal (NAV avoidance takes it from here)
		VectorSubtract(goal, self->r.currentOrigin, dir);
		dir[2] = 0.0f;
		return VectorNormalize(dir) > 0.0f ? qtrue : qfalse;
	}

	// stuck on this waypoint? replan without it
	if (flatDist < p->bestDist - 8.0f)
	{
		p->bestDist = flatDist;
		p->progressTime = level.time;
	}
	else if (level.time - p->progressTime > NPCPATH_STUCK_TIME)
	{
		p->avoidNode = p->nodes[p->pos];
		p->numNodes = 0;
		p->planTime = level.time;
		return qfalse;
	}

	// jump up to waypoints that are higher (force jump if the route needs it, bots do the same)
	if (toNode[2] > 40.0f && flatDist < 160.0f && self->client->ps.groundEntityNum != ENTITYNUM_NONE
		&& self->NPC && !(self->NPC->scriptFlags & SCF_NO_ACROBATICS))
	{
		NPCS.ucmd.upmove = 127;
	}

	VectorCopy(toNode, dir);
	dir[2] = 0.0f;
	if (VectorNormalize(dir) <= 0.0f)
	{
		return qfalse;
	}
	if (p->avoidNode >= 0 && level.time - p->planTime > NPCPATH_REPLAN_TIME)
	{
		p->avoidNode = -1;	// forget the bad waypoint after a while
	}
	return qtrue;
}