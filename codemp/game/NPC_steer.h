// MP steering layer: a C port of the SP STEER / NAV functions the SP AI uses (g_navigator.cpp), DH 2026-11-03.
// See NPC_steer.c.
#pragma once

void STEER_Activate(gentity_t* actor);
void STEER_DeActivate(gentity_t* actor, usercmd_t* ucmd);
qboolean STEER_Active(const gentity_t* actor);

float STEER_Stop(gentity_t* actor);
float STEER_Seek(gentity_t* actor, const vec3_t pos, float slowingDistance, float weight, float desiredSpeed);
float STEER_Flee(gentity_t* actor, const vec3_t pos, float weight);
float STEER_Persue(gentity_t* actor, const gentity_t* target, float slowingDistance);
float STEER_Evade(gentity_t* actor, const gentity_t* target);
float STEER_Separation(gentity_t* actor, float scale);
float STEER_Wander(gentity_t* actor);
float STEER_FollowLeader(gentity_t* actor, gentity_t* leader, float dist);
float STEER_AvoidCollisions(gentity_t* actor, const gentity_t* leader);

qboolean STEER_Reached(const gentity_t* actor, const vec3_t target, float targetRadius);
qboolean STEER_GoToPos(gentity_t* actor, const vec3_t position, float reachedRadius, qboolean avoidCollisions);
qboolean STEER_GoToEnt(gentity_t* actor, const gentity_t* target, float reachedRadius, qboolean avoidCollisions);

int STEER_LastMoveTime(const gentity_t* ent); // the last time it moved (SP gentity_t lastMoveTime)
const float* STEER_FollowPos(const gentity_t* leader); // where followers of this leader go (SP followPos)
float STEER_ResultSpeed(const gentity_t* ent); // its speed this frame (SP resultspeed)

// waypoint graph helpers (SP NAV::), on MP's navigator (trap->Nav_*); WAYPOINT_NONE when there is no nav
int NAV_NearestNode(gentity_t* actor);
int NAV_ChooseFarthestNeighbor(gentity_t* actor, const vec3_t target, float maxSafeDot);
int NAV_ChooseRandomNeighbor(int node);
qboolean NAV_NodePosition(int node, vec3_t out);
qboolean NAV_OnNeighboringPoints(int nodeA, int nodeB);
qboolean NAV_GoToPos(gentity_t* actor, const vec3_t position, float radius); // path there (NPC_MoveToGoal)
qboolean NAV_SafePathExists(const vec3_t start, const vec3_t stop, const vec3_t danger, float dangerDist);
