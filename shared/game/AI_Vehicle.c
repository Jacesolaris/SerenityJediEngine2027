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

// AI vehicle pilots for both games (NPCs with SCF_PILOT: get on the nearest swoop and ride it at their enemy).
// SP's version is C++ on SP's navigator; MP's follows it in C on NPC_steer.c. Was code/game/AI_Vehicle.cpp (MP had none).

#include "game/g_npcshared.h"

#ifdef SP_GAME

////////////////////////////////////////////////////////////////////////////////////////
// RAVEN SOFTWARE - STAR WARS: JK III
//  (c) 2002 Activision
//
// April 3, 2003 - This file has been commandeered for use by AI vehicle pilots.
//
////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////
// Includes
////////////////////////////////////////////////////////////////////////////////////////
#include "game/b_local.h"
#include "game/anims.h"
#include "game/g_navigator.h"
#include "game/g_vehicles.h"
#include "game/g_functions.h"
#if !defined(RATL_VECTOR_VS_INC)
#include "Ratl/vector_vs.h"
#endif

////////////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////////////
constexpr auto MAX_VEHICLES_REGISTERED = 100;

constexpr auto ATTACK_FWD = 0.95f;
constexpr auto ATTACK_SIDE = 0.20f;
constexpr auto AIM_SIDE = 0.60f;
constexpr auto FUTURE_PRED_DIST = 20.0f;
constexpr auto FUTURE_SIDE_DIST = 60.0f;
constexpr auto ATTACK_FLANK_SLOWING = 1000.0f;
constexpr auto RAM_DIST = 150.0f;
constexpr auto MIN_STAY_VIEWABLE_TIME = 20000;

////////////////////////////////////////////////////////////////////////////////////////
// Externs
////////////////////////////////////////////////////////////////////////////////////////
extern Vehicle_t* G_IsRidingVehicle(const gentity_t* pEnt);
extern void G_SoundAtSpot(vec3_t org, int sound_index, qboolean broadcast);
extern void CG_DrawEdge(vec3_t start, vec3_t end, int type);

trace_t mPilotViewTrace;
int mPilotViewTraceCount;
int mActivePilotCount;
ratl::vector_vs<gentity_t*, MAX_VEHICLES_REGISTERED> mRegistered;

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
void Pilot_Reset()
{
	mPilotViewTraceCount = 0;
	mActivePilotCount = 0;
	mRegistered.clear();
}

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
int Pilot_ActivePilotCount()
{
	return mActivePilotCount;
}

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
void Pilot_Update()
{
	mActivePilotCount = 0;
	mRegistered.clear();
	for (int i = 0; i < ENTITYNUM_WORLD; i++)
	{
		if (g_entities[i].inuse &&
			g_entities[i].client &&
			g_entities[i].NPC &&
			g_entities[i].NPC->greetEnt &&
			g_entities[i].NPC->greetEnt->owner == &g_entities[i]
			)
		{
			mActivePilotCount++;
		}
		if (g_entities[i].inuse &&
			g_entities[i].client &&
			g_entities[i].m_pVehicle &&
			!g_entities[i].owner &&
			g_entities[i].health > 0 &&
			g_entities[i].m_pVehicle->m_pVehicleInfo->type == VH_SPEEDER &&
			!mRegistered.full())
		{
			mRegistered.push_back(&g_entities[i]);
		}
	}

	if (player &&
		player->inuse &&
		TIMER_Done(player, "FlybySoundArchitectureDebounce"))
	{
		TIMER_Set(player, "FlybySoundArchitectureDebounce", 300);

		const Vehicle_t* p_veh = G_IsRidingVehicle(player);

		if (p_veh &&
			(p_veh->m_pVehicleInfo->soundFlyBy || p_veh->m_pVehicleInfo->soundFlyBy2) &&
			//fabsf(p_veh->m_pParentEntity->currentAngles[2])<15.0f &&
			VectorLength(p_veh->m_pParentEntity->client->ps.velocity) > 500.0f)
		{
			vec3_t projectedPosition = { 0 };
			vec3_t projectedDirection = { 0 };
			vec3_t projectedRight = { 0 };
			vec3_t anglesNoRoll = { 0 };

			VectorCopy(p_veh->m_pParentEntity->currentAngles, anglesNoRoll);
			anglesNoRoll[2] = 0;
			AngleVectors(anglesNoRoll, projectedDirection, projectedRight, nullptr);

			VectorMA(player->currentOrigin, 1.2f, p_veh->m_pParentEntity->client->ps.velocity, projectedPosition);
			VectorMA(projectedPosition, Q_flrand(-200.0f, 200.0f), projectedRight, projectedPosition);

			gi.trace(&mPilotViewTrace,
				player->currentOrigin,
				nullptr,
				nullptr,
				projectedPosition,
				player->s.number,
				MASK_SHOT, static_cast<EG2_Collision>(0), 0);

			if (mPilotViewTrace.allsolid == qfalse &&
				mPilotViewTrace.startsolid == qfalse &&
				mPilotViewTrace.fraction < 0.99f &&
				mPilotViewTrace.plane.normal[2] < 0.5f &&
				DotProduct(projectedDirection, mPilotViewTrace.plane.normal) < -0.5f)
			{
				TIMER_Set(player, "FlybySoundArchitectureDebounce", Q_irand(1000, 2000));

				int soundFlyBy = p_veh->m_pVehicleInfo->soundFlyBy;
				if (p_veh->m_pVehicleInfo->soundFlyBy2 && (!soundFlyBy || !Q_irand(0, 1)))
				{
					soundFlyBy = p_veh->m_pVehicleInfo->soundFlyBy2;
				}
				G_SoundAtSpot(mPilotViewTrace.endpos, soundFlyBy, qtrue);
			}
			else
			{
				//	CG_DrawEdge(player->currentOrigin, mPilotViewTrace.endpos, EDGE_IMPACT_SAFE);
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
bool Pilot_AnyVehiclesRegistered()
{
	return !mRegistered.empty();
}

////////////////////////////////////////////////////////////////////////////////////////
// Vehicle Registration
//
// Any vehicles that can be ridden by NPCs should be registered here
//
////////////////////////////////////////////////////////////////////////////////////////
void Vehicle_Register(gentity_t* ent)
{}

////////////////////////////////////////////////////////////////////////////////////////
// Vehicle Remove From The List Of Valid
////////////////////////////////////////////////////////////////////////////////////////
void Vehicle_Remove(gentity_t* ent)
{}

////////////////////////////////////////////////////////////////////////////////////////
// Vehicle_Find
//
// Will look through all registered vehicles and choose the closest one that the given
// entity can get to.
//
////////////////////////////////////////////////////////////////////////////////////////
static gentity_t* Vehicle_Find(gentity_t* ent)
{
	gentity_t* closest = nullptr;
	float closestDist = 0;

	for (int i = 0; i < mRegistered.size(); i++)
	{
		if (!mRegistered[i]->owner)
		{
			const float curDist = Distance(mRegistered[i]->currentOrigin, ent->currentOrigin);
			if (curDist < 1000 && (!closest || curDist < closestDist))
			{
				if (NAV::InSameRegion(ent, mRegistered[i]))
				{
					closest = mRegistered[i];
					closestDist = curDist;
				}
			}
		}
	}

	return closest;
}

void Pilot_Update_Enemy();
void Pilot_Steer_Vehicle();
void Pilot_Goto_Vehicle();

////////////////////////////////////////////////////////////////////////////////////////
// Pilot_MasterUpdate() - Master think function for Pilot NPCs
//
// Will return true if the character is either driving a vehicle or on his way to get
// onto one.
////////////////////////////////////////////////////////////////////////////////////////
bool Pilot_MasterUpdate()
{
	if (!NPC->enemy)
	{
		// If Still On A Vehicle, Jump Off
		//---------------------------------
		if (NPCInfo->greetEnt)
		{
			ucmd.upmove = 127;

			if (NPCInfo->greetEnt && NPCInfo->greetEnt->m_pVehicle && (level.time < NPCInfo->confusionTime || level.time
				< NPCInfo->insanityTime))
			{
				Vehicle_t* p_veh = NPCInfo->greetEnt->m_pVehicle;
				if (!(p_veh->m_ulFlags & VEH_OUTOFCONTROL))
				{
					gentity_t* parent = p_veh->m_pParentEntity;
					const float CurSpeed = VectorLength(parent->client->ps.velocity);
					p_veh->m_pVehicleInfo->StartDeathDelay(p_veh, 10000);
					p_veh->m_ulFlags |= VEH_OUTOFCONTROL;
					VectorScale(parent->client->ps.velocity, 1.25f, parent->pos3);
					if (CurSpeed < p_veh->m_pVehicleInfo->speedMax)
					{
						VectorNormalize(parent->pos3);
						if (fabsf(parent->pos3[2]) < 0.25f)
						{
							VectorScale(parent->pos3, p_veh->m_pVehicleInfo->speedMax * 1.25f, parent->pos3);
						}
						else
						{
							VectorScale(parent->client->ps.velocity, 1.25f, parent->pos3);
						}
					}
				}
			}

			if (NPCInfo->greetEnt->owner == NPC)
			{
				return true;
			}
			NPCInfo->greetEnt = nullptr;
		}

		// Otherwise Nothing To See Here
		//-------------------------------
		return false;
	}

	// If We Already Have A Target Vehicle, Make Sure It Is Still Valid
	//------------------------------------------------------------------
	if (NPCInfo->greetEnt)
	{
		if (!NPCInfo->greetEnt->inuse ||
			!NPCInfo->greetEnt->m_pVehicle ||
			!NPCInfo->greetEnt->m_pVehicle->m_pVehicleInfo)
		{
			NPCInfo->greetEnt = Vehicle_Find(NPC);
		}
		else
		{
			if (NPCInfo->greetEnt->owner && NPCInfo->greetEnt->owner != NPC)
			{
				NPCInfo->greetEnt = Vehicle_Find(NPC);
			}
		}
	}

	// If We Have An Enemy, Try To Find A Vehicle Nearby
	//---------------------------------------------------
	else
	{
		NPCInfo->greetEnt = Vehicle_Find(NPC);
	}

	// If No Vehicle Available, Continue As Usual
	//--------------------------------------------
	if (!NPCInfo->greetEnt)
	{
		return false;
	}

	if (NPCInfo->greetEnt->owner == NPC)
	{
		Pilot_Steer_Vehicle();
	}
	else
	{
		Pilot_Goto_Vehicle();
	}

	Pilot_Update_Enemy();
	return true;
}

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
void Pilot_Update_Enemy()
{
	if (!TIMER_Exists(NPC, "PilotRemoveTime"))
	{
		TIMER_Set(NPC, "PilotRemoveTime", MIN_STAY_VIEWABLE_TIME);
	}

	if (TIMER_Done(NPC, "NextPilotCheckEnemyTime"))
	{
		TIMER_Set(NPC, "NextPilotCheckEnemyTime", Q_irand(1000, 2000));
		if (NPC->enemy && Distance(NPC->currentOrigin, NPC->enemy->currentOrigin) > 1000.0f)
		{
			mPilotViewTraceCount++;
			gi.trace(&mPilotViewTrace,
				NPC->currentOrigin,
				nullptr,
				nullptr,
				NPC->enemy->currentOrigin,
				NPC->s.number,
				MASK_SHOT,
				static_cast<EG2_Collision>(0), 0);

			if (mPilotViewTrace.allsolid == qfalse &&
				mPilotViewTrace.startsolid == qfalse &&
				(mPilotViewTrace.entityNum == NPC->enemy->s.number || mPilotViewTrace.entityNum == NPC->enemy->s.
					m_iVehicleNum))
			{
				TIMER_Set(NPC, "PilotRemoveTime", MIN_STAY_VIEWABLE_TIME);
			}
		}
		else
		{
			TIMER_Set(NPC, "PilotRemoveTime", MIN_STAY_VIEWABLE_TIME);
		}
	}

	if (TIMER_Done(NPC, "PilotRemoveTime"))
	{
		if (NPCInfo->greetEnt->owner == NPC)
		{
			NPCInfo->greetEnt->e_ThinkFunc = thinkF_G_FreeEntity;
			NPCInfo->greetEnt->nextthink = level.time;
		}
		NPC->e_ThinkFunc = thinkF_G_FreeEntity;
		NPC->nextthink = level.time;
	}
}

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
void Pilot_Goto_Vehicle()
{
	STEER::Activate(NPC);
	{
		if (STEER::Reached(NPC, NPCInfo->greetEnt, 80.0f))
		{
			NPC_Use(NPCInfo->greetEnt, NPC, NPC);
		}
		else if (NAV::OnNeighboringPoints(NPC, NPCInfo->greetEnt))
		{
			STEER::Persue(NPC, NPCInfo->greetEnt, 50.0f, 0.0f, 30.0f, 0.0f, true);
		}
		else
		{
			if (!NAV::GoTo(NPC, NPCInfo->greetEnt))
			{
				STEER::Stop(NPC);
			}
		}
	}
	STEER::AvoidCollisions(NPC);
	STEER::DeActivate(NPC, &ucmd);
	NPC_UpdateAngles(qtrue, qtrue);
}

extern bool VEH_StartStrafeRam(Vehicle_t* p_veh, bool Right);

////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////
void Pilot_Steer_Vehicle()
{
	if (!NPC->enemy || !NPC->enemy->client)
	{
		return;
	}

	// SETUP
	//=======
	// Setup Actor Data
	//------------------
	const CVec3 ActorPos(NPC->currentOrigin);
	CVec3 ActorAngles(NPC->currentAngles);
	ActorAngles[2] = 0.0f;

	Vehicle_t* ActorVeh = (NPCInfo->greetEnt != NULL) ? NPCInfo->greetEnt->m_pVehicle : NULL;
	const bool ActorInTurbo = (ActorVeh != NULL && ActorVeh->m_iTurboTime > level.time);
	const float ActorSpeed = (ActorVeh != NULL)
		? VectorLength(ActorVeh->m_pParentEntity->client->ps.velocity)
		: NPC->client->ps.speed;

	// If my vehicle is spinning out of control, just hold on
	if (ActorVeh != NULL && (ActorVeh->m_ulFlags & VEH_OUTOFCONTROL))
	{
		if (NPC->client->ps.weapon != WP_NONE)
		{
			NPC_ChangeWeapon(WP_NONE);
		}
		ucmd.buttons &= ~BUTTON_ATTACK;
		ucmd.buttons &= ~BUTTON_ALT_ATTACK;
		return;
	}

	CVec3 ActorDirection{};
	AngleVectors(ActorAngles.v, ActorDirection.v, NULL, NULL);

	CVec3 ActorFuturePos(ActorPos);
	ActorFuturePos.ScaleAdd(ActorDirection, FUTURE_PRED_DIST);

	bool ActorDoTurbo = false;
	bool ActorAccelerate = false;
	bool ActorAimAtTarget = true;

	// Setup Enemy Data
	//------------------
	const CVec3 EnemyPos(NPC->enemy->currentOrigin);
	CVec3 EnemyAngles(NPC->enemy->currentAngles);
	EnemyAngles[2] = 0.0f;

	const Vehicle_t* EnemyVeh = (NPC->enemy->s.m_iVehicleNum != 0)
		? g_entities[NPC->enemy->s.m_iVehicleNum].m_pVehicle
		: NULL;

	const bool EnemyInTurbo = (EnemyVeh != NULL && EnemyVeh->m_iTurboTime > level.time);
	const float EnemySpeed = (EnemyVeh != NULL)
		? EnemyVeh->m_pParentEntity->client->ps.speed
		: NPC->enemy->resultspeed;

	const bool EnemySlideBreak = (EnemyVeh != NULL &&
		((EnemyVeh->m_ulFlags & VEH_SLIDEBREAKING) || (EnemyVeh->m_ulFlags & VEH_STRAFERAM)));

	const bool EnemyDead = (NPC->enemy->health <= 0);

	const bool ActorFlank =
		(NPCInfo->lastAvoidSteerSideDebouncer > level.time) &&
		(EnemyVeh != NULL) &&
		(EnemySpeed > 10.0f);

	CVec3 EnemyDirection{};
	CVec3 EnemyRight{};
	AngleVectors(EnemyAngles.v, EnemyDirection.v, EnemyRight.v, NULL);

	CVec3 EnemyFuturePos(EnemyPos);
	EnemyFuturePos.ScaleAdd(EnemyDirection, FUTURE_PRED_DIST);

	const ESide EnemySide = ActorPos.LRTest(EnemyPos, EnemyFuturePos);

	CVec3 EnemyFlankPos(EnemyFuturePos);
	EnemyFlankPos.ScaleAdd(EnemyRight,
		(EnemySide == Side_Right) ? FUTURE_SIDE_DIST : -FUTURE_SIDE_DIST);

	// Setup Move And Aim Directions
	//-------------------------------
	CVec3 MoveDirection(ActorFlank ? EnemyFlankPos : EnemyFuturePos);
	MoveDirection -= ActorPos;
	const float MoveDistance = MoveDirection.SafeNorm();
	const float MoveAccuracy = MoveDirection.Dot(ActorDirection);

	CVec3 AimDirection(EnemyPos);
	AimDirection -= ActorPos;
	const float AimDistance = AimDirection.SafeNorm();
	const float AimAccuracy = AimDirection.Dot(ActorDirection);

	if (!ActorFlank && TIMER_Done(NPC, "FlankAttackCheck"))
	{
		TIMER_Set(NPC, "FlankAttackCheck", Q_irand(1000, 3000));
		if (MoveDistance < 4000.0f && Q_irand(0, 1) == 0)
		{
			NPCInfo->lastAvoidSteerSideDebouncer = level.time + Q_irand(8000, 14000);
		}
	}

	// Fly By Sounds
	//---------------
	if (ActorVeh != NULL && ActorVeh->m_pVehicleInfo != NULL &&
		TIMER_Done(NPC, "FlybySoundDebouncer") &&
		(ActorVeh->m_pVehicleInfo->soundFlyBy || ActorVeh->m_pVehicleInfo->soundFlyBy2) &&
		EnemyVeh != NULL &&
		MoveDistance < 800.0f &&
		ActorSpeed > 500.0f)
	{
		if (EnemySpeed < 100.0f ||
			(ActorDirection.Dot(EnemyDirection) * (MoveDistance / 800.0f)) < -0.5f)
		{
			TIMER_Set(NPC, "FlybySoundDebouncer", 2000);

			int soundFlyBy = ActorVeh->m_pVehicleInfo->soundFlyBy;
			if (ActorVeh->m_pVehicleInfo->soundFlyBy2 &&
				(!soundFlyBy || !Q_irand(0, 1)))
			{
				soundFlyBy = ActorVeh->m_pVehicleInfo->soundFlyBy2;
			}

			if (soundFlyBy)
			{
				G_Sound(ActorVeh->m_pParentEntity, soundFlyBy);
			}
		}
	}

	// FLY PAST BEHAVIOR
	//===================
	if (EnemySlideBreak || !TIMER_Done(NPC, "MinHoldDirectionTime"))
	{
		if (TIMER_Done(NPC, "MinHoldDirectionTime"))
		{
			TIMER_Set(NPC, "MinHoldDirectionTime", 500);
		}

		ActorAccelerate = true;
		ActorAimAtTarget = false;
		ucmd.buttons &= ~BUTTON_VEH_SPEED;
	}
	// FLANKING BEHAVIOR
	//===================
	else if (ActorFlank)
	{
		ActorAccelerate = true;
		ActorDoTurbo = (MoveDistance > 2500.0f) || EnemyInTurbo;
		ucmd.buttons |= BUTTON_VEH_SPEED;

		if (ActorVeh != NULL && ActorVeh->m_pVehicleInfo != NULL)
		{
			NPC->client->ps.speed =
				ActorVeh->m_pVehicleInfo->speedMax * (ActorInTurbo ? 1.35f : 1.15f);
		}

		if (MoveDistance < ATTACK_FLANK_SLOWING)
		{
			NPC->client->ps.speed *= (MoveDistance / ATTACK_FLANK_SLOWING);
			NPC->client->ps.speed += EnemySpeed;

			if (NPC->client->ps.speed < 5.0f && EnemySpeed < 5.0f)
			{
				NPC->client->ps.speed = EnemySpeed;
			}

			if (MoveAccuracy < 0.0f)
			{
				NPC->client->ps.speed *= (MoveAccuracy + 1.0f);
			}

			MoveDirection *= (MoveDistance / ATTACK_FLANK_SLOWING);
			EnemyDirection *= (1.0f - (MoveDistance / ATTACK_FLANK_SLOWING));
			MoveDirection += EnemyDirection;

			if (ActorVeh != NULL && TIMER_Done(NPC, "RamCheck"))
			{
				TIMER_Set(NPC, "RamCheck", Q_irand(1000, 3000));
				if (MoveDistance < RAM_DIST && Q_irand(0, 2) == 0)
				{
					VEH_StartStrafeRam(ActorVeh, (EnemySide == Side_Left));
				}
			}
		}
	}
	// NORMAL CHASE BEHAVIOR
	//=======================
	else
	{
		if (!EnemyVeh && AimAccuracy > 0.99f && MoveDistance < 500.0f && !EnemyDead)
		{
			ActorAccelerate = true;
			ActorDoTurbo = false;
		}
		else
		{
			ActorAccelerate =
				((MoveDistance > 500.0f && EnemySpeed > 20.0f) || (MoveDistance > 1000.0f));
			ActorDoTurbo =
				(MoveDistance > 3000.0f && EnemySpeed > 20.0f);
		}

		ucmd.buttons &= ~BUTTON_VEH_SPEED;
	}

	// APPLY RESULTS
	//=======================
	// Decide Turbo
	if (ActorDoTurbo || ActorInTurbo)
	{
		ucmd.buttons |= BUTTON_ALT_ATTACK;
	}
	else
	{
		ucmd.buttons &= ~BUTTON_ALT_ATTACK;
	}

	// Decide Acceleration
	ucmd.forwardmove = ActorAccelerate ? 127 : 0;

	// Decide To Shoot (DH 2026-11-23: a pilot fires his vehicle's guns, never his own weapon - SP drew a blaster to
	// shoot to the side)
	ucmd.buttons &= ~BUTTON_ATTACK;
	ucmd.rightmove = 0;

	if (NPC->client->ps.weapon != WP_NONE)
	{
		NPC_ChangeWeapon(WP_NONE);
	}

	if (AimDistance < 2000.0f && !EnemyDead && AimAccuracy > ATTACK_FWD)
	{
		ucmd.buttons |= BUTTON_ATTACK;
	}
	// Aim At Target
	if (ActorAimAtTarget)
	{
		constexpr float ActorYawOffset = 0.0f;
		MoveDirection.VecToAng();
		NPCInfo->desiredPitch = AngleNormalize360(MoveDirection[PITCH]);
		NPCInfo->desiredYaw = AngleNormalize360(MoveDirection[YAW] + ActorYawOffset);
	}

	NPC_UpdateAngles(qtrue, qtrue);
}

#else // MP game

// SP's pilot AI (above) for the MP game, so the Missions' swoop riders (t2_trip) get on swoops and chase the players as
// in SP (DH 2026-11-23). It follows the SP functions one by one; where MP differs:
// - the rider of a vehicle is its m_pVehicle->m_pPilot (SP: vehicle->owner), and a pilot gets on by Board() (SP: NPC_Use)
// - MP has no SP navigator regions (NAV::InSameRegion): a swoop counts as reachable when it is in the pilot's PVS, and a
//   pilot who has not got to his swoop in PILOT_REACH_TIME gives that one up for PILOT_SKIP_TIME
// - the way to the swoop uses NPC_steer.c (STEER_ / NAV_), and a clear straight line when the map has no waypoints
// - MP's speeders have no strafe ram, so the flank run leaves that out; SP's BUTTON_VEH_SPEED (the bike takes the
//   pilot's speed) is Pilot_MatchSpeed (SpeederNPC.c)
// - the fly-by sounds for the player play for every player riding a speeder

#include "game/NPC_steer.h"

#define MAX_VEHICLES_REGISTERED 100

#define ATTACK_FWD				0.95f
#define ATTACK_SIDE				0.20f
#define AIM_SIDE				0.60f
#define FUTURE_PRED_DIST		20.0f
#define FUTURE_SIDE_DIST		60.0f
#define ATTACK_FLANK_SLOWING	1000.0f
#define MIN_STAY_VIEWABLE_TIME	20000

#define PILOT_REACH_TIME		10000	// MP: a pilot who cannot get to his swoop in this long...
#define PILOT_SKIP_TIME			15000	// ...leaves that one alone for this long

#define PILOT_SIDE_NONE		0	// SP ESide (Ravl/CVec.h)
#define PILOT_SIDE_LEFT		1
#define PILOT_SIDE_RIGHT	2

static int mPilotViewTraceCount;
static int mActivePilotCount;
static gentity_t* mRegistered[MAX_VEHICLES_REGISTERED];
static int mRegisteredCount;

static int pilotFlankTime[MAX_GENTITIES]; // SP NPCInfo->lastAvoidSteerSideDebouncer
static qboolean pilotMatchSpeed[MAX_GENTITIES]; // SP BUTTON_VEH_SPEED
static int pilotSkipVeh[MAX_GENTITIES]; // the swoop a pilot gave up on (+1)...
static int pilotSkipTime[MAX_GENTITIES]; // ...until then

static qboolean Pilot_Riding(const gentity_t* veh, const gentity_t* pilot)
{
	return veh && veh->m_pVehicle && veh->m_pVehicle->m_pPilot == (bgEntity_t*)pilot ? qtrue : qfalse;
}

void Pilot_Reset(void)
{
	mPilotViewTraceCount = 0;
	mActivePilotCount = 0;
	mRegisteredCount = 0;
	memset(pilotFlankTime, 0, sizeof pilotFlankTime);
	memset(pilotMatchSpeed, 0, sizeof pilotMatchSpeed);
	memset(pilotSkipVeh, 0, sizeof pilotSkipVeh);
	memset(pilotSkipTime, 0, sizeof pilotSkipTime);
}

int Pilot_ActivePilotCount(void)
{
	return mActivePilotCount;
}

// SpeederNPC.c: an AI pilot sets his bike's speed himself this frame (SP BUTTON_VEH_SPEED)
qboolean Pilot_MatchSpeed(const gentity_t* pilot)
{
	return pilot && pilot->s.number >= MAX_CLIENTS && pilot->s.number < MAX_GENTITIES && pilot->NPC && pilot->inuse
		&& pilotMatchSpeed[pilot->s.number] ? qtrue : qfalse;
}

// SpeederNPC.c: an AI pilot points his bike where he looks, moving or not, as in SP (MP turns a bike by its speed)
qboolean Pilot_AISteers(const bgEntity_t* pilot)
{
	const gentity_t* ent = (const gentity_t*)pilot;
	return ent && ent->s.number >= MAX_CLIENTS && ent->inuse && ent->NPC && ent->NPC->scriptFlags & SCF_PILOT ? qtrue : qfalse;
}

static int Pilot_FlyBySound(const vehicleInfo_t* info)
{
	int soundFlyBy = info->soundFlyBy;
	if (info->soundFlyBy2 && (!soundFlyBy || !Q_irand(0, 1)))
	{
		soundFlyBy = info->soundFlyBy2;
	}
	return soundFlyBy;
}

void Pilot_Update(void)
{
	mActivePilotCount = 0;
	mRegisteredCount = 0;
	for (int i = 0; i < ENTITYNUM_WORLD; i++)
	{
		gentity_t* ent = &g_entities[i];
		if (!ent->inuse || !ent->client)
		{
			continue;
		}
		if (ent->NPC &&
			ent->NPC->greetEnt &&
			Pilot_Riding(ent->NPC->greetEnt, ent))
		{
			mActivePilotCount++;
		}
		if (ent->s.eType == ET_NPC &&
			ent->m_pVehicle &&
			ent->m_pVehicle->m_pVehicleInfo &&
			!ent->m_pVehicle->m_pPilot &&
			ent->health > 0 &&
			ent->m_pVehicle->m_pVehicleInfo->type == VH_SPEEDER &&
			mRegisteredCount < MAX_VEHICLES_REGISTERED)
		{
			mRegistered[mRegisteredCount++] = ent;
		}
	}

	// fly-by sounds when a player races past a wall (SP: the player)
	for (int i = 0; i < level.maxclients; i++)
	{
		gentity_t* player = &g_entities[i];
		if (!player->inuse || !player->client || player->client->pers.connected != CON_CONNECTED
			|| !player->client->ps.m_iVehicleNum || !TIMER_Done(player, "FlybySoundArchitectureDebounce"))
		{
			continue;
		}
		TIMER_Set(player, "FlybySoundArchitectureDebounce", 300);

		const gentity_t* vehEnt = &g_entities[player->client->ps.m_iVehicleNum];
		const Vehicle_t* p_veh = vehEnt->inuse ? vehEnt->m_pVehicle : NULL;

		if (p_veh && p_veh->m_pVehicleInfo && vehEnt->client &&
			(p_veh->m_pVehicleInfo->soundFlyBy || p_veh->m_pVehicleInfo->soundFlyBy2) &&
			VectorLength(vehEnt->client->ps.velocity) > 500.0f)
		{
			vec3_t projectedPosition = { 0 };
			vec3_t projectedDirection = { 0 };
			vec3_t projectedRight = { 0 };
			vec3_t anglesNoRoll = { 0 };
			trace_t tr = { 0 };

			VectorCopy(vehEnt->r.currentAngles, anglesNoRoll);
			anglesNoRoll[2] = 0;
			AngleVectors(anglesNoRoll, projectedDirection, projectedRight, NULL);

			VectorMA(player->r.currentOrigin, 1.2f, vehEnt->client->ps.velocity, projectedPosition);
			VectorMA(projectedPosition, Q_flrand(-200.0f, 200.0f), projectedRight, projectedPosition);

			trap->Trace(&tr, player->r.currentOrigin, NULL, NULL, projectedPosition, player->s.number, MASK_SHOT,
				qfalse, 0, 0);

			if (tr.allsolid == qfalse &&
				tr.startsolid == qfalse &&
				tr.fraction < 0.99f &&
				tr.plane.normal[2] < 0.5f &&
				DotProduct(projectedDirection, tr.plane.normal) < -0.5f)
			{
				TIMER_Set(player, "FlybySoundArchitectureDebounce", Q_irand(1000, 2000));
				const int soundFlyBy = Pilot_FlyBySound(p_veh->m_pVehicleInfo);
				if (soundFlyBy)
				{
					G_SoundAtLoc(tr.endpos, CHAN_AUTO, soundFlyBy);
				}
			}
		}
	}
}

qboolean Pilot_AnyVehiclesRegistered(void)
{
	return mRegisteredCount ? qtrue : qfalse;
}

void Vehicle_Register(gentity_t* ent)
{
}

void Vehicle_Remove(gentity_t* ent)
{
}

// the closest free swoop the given entity can get to
static gentity_t* Vehicle_Find(gentity_t* ent)
{
	gentity_t* closest = NULL;
	float closestDist = 0;

	for (int i = 0; i < mRegisteredCount; i++)
	{
		gentity_t* veh = mRegistered[i];
		if (!veh->inuse || !veh->m_pVehicle || veh->m_pVehicle->m_pPilot || veh->m_pVehicle->m_iBoarding > 0)
		{
			continue;
		}
		if (pilotSkipVeh[ent->s.number] == veh->s.number + 1 && pilotSkipTime[ent->s.number] > level.time)
		{
			continue; // MP: one he could not get to
		}
		const float curDist = Distance(veh->r.currentOrigin, ent->r.currentOrigin);
		if (curDist < 1000 && (!closest || curDist < closestDist))
		{
			if (trap->InPVS(ent->r.currentOrigin, veh->r.currentOrigin)) // MP: SP NAV::InSameRegion
			{
				closest = veh;
				closestDist = curDist;
			}
		}
	}

	return closest;
}

// MP: a new swoop to go to gets PILOT_REACH_TIME
static gentity_t* Pilot_PickVehicle(void)
{
	gentity_t* veh = Vehicle_Find(NPCS.NPC);
	if (veh && veh != NPCS.NPCInfo->greetEnt)
	{
		TIMER_Set(NPCS.NPC, "PilotReachVehicle", PILOT_REACH_TIME);
	}
	return veh;
}

static void Pilot_Update_Enemy(void);
static void Pilot_Steer_Vehicle(void);
static void Pilot_Goto_Vehicle(void);

// Master think function for Pilot NPCs: true if the character is either driving a vehicle or on his way to get onto one
qboolean Pilot_MasterUpdate(void)
{
	gentity_t* npc = NPCS.NPC;
	gNPC_t* info = NPCS.NPCInfo;

	pilotMatchSpeed[npc->s.number] = qfalse;

	if (!npc->enemy)
	{
		// If Still On A Vehicle, Jump Off
		//---------------------------------
		if (info->greetEnt)
		{
			NPCS.ucmd.upmove = 127;

			if (info->greetEnt->m_pVehicle && level.time < info->confusionTime)
			{
				Vehicle_t* p_veh = info->greetEnt->m_pVehicle;
				if (!(p_veh->m_ulFlags & VEH_OUTOFCONTROL))
				{
					gentity_t* parent = (gentity_t*)p_veh->m_pParentEntity;
					const float CurSpeed = VectorLength(parent->client->ps.velocity);
					p_veh->m_pVehicleInfo->StartDeathDelay(p_veh, 10000);
					p_veh->m_ulFlags |= VEH_OUTOFCONTROL;
					VectorScale(parent->client->ps.velocity, 1.25f, parent->pos3);
					if (CurSpeed < p_veh->m_pVehicleInfo->speedMax)
					{
						VectorNormalize(parent->pos3);
						if (fabsf(parent->pos3[2]) < 0.25f)
						{
							VectorScale(parent->pos3, p_veh->m_pVehicleInfo->speedMax * 1.25f, parent->pos3);
						}
						else
						{
							VectorScale(parent->client->ps.velocity, 1.25f, parent->pos3);
						}
					}
				}
			}

			if (Pilot_Riding(info->greetEnt, npc))
			{
				return qtrue;
			}
			info->greetEnt = NULL;
		}

		// Otherwise Nothing To See Here
		//-------------------------------
		return qfalse;
	}

	// If We Already Have A Target Vehicle, Make Sure It Is Still Valid
	//------------------------------------------------------------------
	if (info->greetEnt)
	{
		if (!info->greetEnt->inuse ||
			!info->greetEnt->m_pVehicle ||
			!info->greetEnt->m_pVehicle->m_pVehicleInfo)
		{
			info->greetEnt = Pilot_PickVehicle();
		}
		else if (info->greetEnt->m_pVehicle->m_pPilot && !Pilot_Riding(info->greetEnt, npc))
		{
			info->greetEnt = Pilot_PickVehicle();
		}
		else if (!Pilot_Riding(info->greetEnt, npc) && TIMER_Done(npc, "PilotReachVehicle"))
		{
			// MP: could not get to it
			pilotSkipVeh[npc->s.number] = info->greetEnt->s.number + 1;
			pilotSkipTime[npc->s.number] = level.time + PILOT_SKIP_TIME;
			info->greetEnt = Pilot_PickVehicle();
		}
	}

	// If We Have An Enemy, Try To Find A Vehicle Nearby
	//---------------------------------------------------
	else
	{
		info->greetEnt = Pilot_PickVehicle();
	}

	// If No Vehicle Available, Continue As Usual
	//--------------------------------------------
	if (!info->greetEnt)
	{
		return qfalse;
	}

	if (Pilot_Riding(info->greetEnt, npc))
	{
		Pilot_Steer_Vehicle();
	}
	else
	{
		Pilot_Goto_Vehicle();
	}

	Pilot_Update_Enemy();
	return qtrue;
}

static void Pilot_Update_Enemy(void)
{
	gentity_t* npc = NPCS.NPC;

	if (!TIMER_Exists(npc, "PilotRemoveTime"))
	{
		TIMER_Set(npc, "PilotRemoveTime", MIN_STAY_VIEWABLE_TIME);
	}

	if (TIMER_Done(npc, "NextPilotCheckEnemyTime"))
	{
		TIMER_Set(npc, "NextPilotCheckEnemyTime", Q_irand(1000, 2000));
		if (npc->enemy && Distance(npc->r.currentOrigin, npc->enemy->r.currentOrigin) > 1000.0f)
		{
			trace_t tr = { 0 };
			mPilotViewTraceCount++;
			trap->Trace(&tr, npc->r.currentOrigin, NULL, NULL, npc->enemy->r.currentOrigin, npc->s.number, MASK_SHOT,
				qfalse, 0, 0);

			if (tr.allsolid == qfalse &&
				tr.startsolid == qfalse &&
				(tr.entityNum == npc->enemy->s.number || tr.entityNum == npc->enemy->s.m_iVehicleNum))
			{
				TIMER_Set(npc, "PilotRemoveTime", MIN_STAY_VIEWABLE_TIME);
			}
		}
		else
		{
			TIMER_Set(npc, "PilotRemoveTime", MIN_STAY_VIEWABLE_TIME);
		}
	}

	if (TIMER_Done(npc, "PilotRemoveTime"))
	{
		if (Pilot_Riding(NPCS.NPCInfo->greetEnt, npc))
		{
			NPCS.NPCInfo->greetEnt->think = G_FreeEntity;
			NPCS.NPCInfo->greetEnt->nextthink = level.time;
		}
		npc->think = G_FreeEntity;
		npc->nextthink = level.time;
	}
}

static void Pilot_Goto_Vehicle(void)
{
	gentity_t* npc = NPCS.NPC;
	gentity_t* veh = NPCS.NPCInfo->greetEnt;

	STEER_Activate(npc);
	{
		if (STEER_Reached(npc, veh->r.currentOrigin, 80.0f))
		{
			veh->m_pVehicle->m_pVehicleInfo->Board(veh->m_pVehicle, (bgEntity_t*)npc); // SP: NPC_Use
		}
		else if (NAV_OnNeighboringPoints(NAV_NearestNode(npc), NAV_NearestNode(veh)))
		{
			STEER_Persue(npc, veh, 50.0f);
		}
		else if (!NAV_GoToPos(npc, veh->r.currentOrigin, 80.0f))
		{
			// MP: no way along waypoints (or none on the map): straight there when nothing is in the way
			trace_t tr = { 0 };
			trap->Trace(&tr, npc->r.currentOrigin, npc->r.mins, npc->r.maxs, veh->r.currentOrigin, npc->s.number,
				MASK_NPCSOLID & ~CONTENTS_BODY, qfalse, 0, 0);
			if (!tr.startsolid && !tr.allsolid && (tr.fraction == 1.0f || tr.entityNum == veh->s.number))
			{
				STEER_Persue(npc, veh, 50.0f);
			}
			else
			{
				STEER_Stop(npc);
			}
		}
	}
	STEER_AvoidCollisions(npc, npc->client->leader);
	STEER_DeActivate(npc, &NPCS.ucmd);
	NPC_UpdateAngles(qtrue, qtrue);
}

static int Pilot_LRTest(const vec3_t p, const vec3_t start, const vec3_t end) // SP CVec3::LRTest
{
	const float area = start[0] * end[1] - start[1] * end[0] +
		(end[0] * p[1] - p[0] * end[1]) +
		(p[0] * start[1] - start[0] * p[1]);
	if (area > 0.0f)
	{
		return PILOT_SIDE_LEFT;
	}
	if (area < 0.0f)
	{
		return PILOT_SIDE_RIGHT;
	}
	return PILOT_SIDE_NONE;
}

static void Pilot_Steer_Vehicle(void)
{
	gentity_t* npc = NPCS.NPC;
	gNPC_t* info = NPCS.NPCInfo;

	if (!npc->enemy || !npc->enemy->client || !npc->enemy->inuse)
	{
		return;
	}
	if (!info->greetEnt || !info->greetEnt->m_pVehicle)
	{
		return;
	}

	// SETUP
	//=======
	vec3_t ActorPos = { 0 }, ActorAngles = { 0 };
	VectorCopy(npc->r.currentOrigin, ActorPos);
	VectorCopy(npc->r.currentAngles, ActorAngles);
	ActorAngles[2] = 0.0f;

	Vehicle_t* ActorVeh = info->greetEnt->m_pVehicle;
	if (!ActorVeh->m_pParentEntity || !ActorVeh->m_pVehicleInfo)
	{
		return;
	}
	const gentity_t* ActorVehEnt = (gentity_t*)ActorVeh->m_pParentEntity;

	const qboolean ActorInTurbo = ActorVeh->m_iTurboTime > level.time ? qtrue : qfalse;
	const float ActorSpeed = VectorLength(ActorVehEnt->client->ps.velocity);

	// If my vehicle is spinning out of control, just hold on
	if (ActorVeh->m_ulFlags & VEH_OUTOFCONTROL)
	{
		if (npc->client->ps.weapon != WP_NONE)
		{
			NPC_ChangeWeapon(WP_NONE);
		}
		NPCS.ucmd.buttons &= ~BUTTON_ATTACK;
		NPCS.ucmd.buttons &= ~BUTTON_ALT_ATTACK;
		return;
	}

	vec3_t ActorDirection = { 0 };
	AngleVectors(ActorAngles, ActorDirection, NULL, NULL);

	vec3_t ActorFuturePos = { 0 };
	VectorMA(ActorPos, FUTURE_PRED_DIST, ActorDirection, ActorFuturePos);

	qboolean ActorDoTurbo = qfalse;
	qboolean ActorAccelerate = qfalse;
	qboolean ActorAimAtTarget = qtrue;

	// Setup Enemy Data
	//------------------
	vec3_t EnemyPos = { 0 }, EnemyAngles = { 0 };
	VectorCopy(npc->enemy->r.currentOrigin, EnemyPos);
	VectorCopy(npc->enemy->r.currentAngles, EnemyAngles);
	EnemyAngles[2] = 0.0f;

	const Vehicle_t* EnemyVeh = NULL;
	const int enemyVehNum = npc->enemy->client->ps.m_iVehicleNum;
	if (enemyVehNum > 0 && enemyVehNum < ENTITYNUM_WORLD && g_entities[enemyVehNum].inuse
		&& npc->enemy->s.NPC_class != CLASS_VEHICLE)
	{
		EnemyVeh = g_entities[enemyVehNum].m_pVehicle;
	}

	const qboolean EnemyInTurbo = EnemyVeh && EnemyVeh->m_iTurboTime > level.time ? qtrue : qfalse;
	const float EnemySpeed = EnemyVeh
		? ((gentity_t*)EnemyVeh->m_pParentEntity)->client->ps.speed
		: STEER_ResultSpeed(npc->enemy);

	const qboolean EnemySlideBreak = EnemyVeh && EnemyVeh->m_ulFlags & VEH_SLIDEBREAKING ? qtrue : qfalse;

	const qboolean EnemyDead = npc->enemy->health <= 0 ? qtrue : qfalse;

	const qboolean ActorFlank = pilotFlankTime[npc->s.number] > level.time && EnemyVeh && EnemySpeed > 10.0f
		? qtrue : qfalse;

	vec3_t EnemyDirection = { 0 }, EnemyRight = { 0 };
	AngleVectors(EnemyAngles, EnemyDirection, EnemyRight, NULL);

	vec3_t EnemyFuturePos = { 0 };
	VectorMA(EnemyPos, FUTURE_PRED_DIST, EnemyDirection, EnemyFuturePos);

	const int EnemySide = Pilot_LRTest(ActorPos, EnemyPos, EnemyFuturePos);

	vec3_t EnemyFlankPos = { 0 };
	VectorMA(EnemyFuturePos, EnemySide == PILOT_SIDE_RIGHT ? FUTURE_SIDE_DIST : -FUTURE_SIDE_DIST, EnemyRight,
		EnemyFlankPos);

	// Setup Move And Aim Directions
	//-------------------------------
	vec3_t MoveDirection = { 0 };
	VectorSubtract(ActorFlank ? EnemyFlankPos : EnemyFuturePos, ActorPos, MoveDirection);
	const float MoveDistance = VectorNormalize(MoveDirection);
	const float MoveAccuracy = DotProduct(MoveDirection, ActorDirection);

	vec3_t AimDirection = { 0 };
	VectorSubtract(EnemyPos, ActorPos, AimDirection);
	const float AimDistance = VectorNormalize(AimDirection);
	const float AimAccuracy = DotProduct(AimDirection, ActorDirection);

	if (!ActorFlank && TIMER_Done(npc, "FlankAttackCheck"))
	{
		TIMER_Set(npc, "FlankAttackCheck", Q_irand(1000, 3000));
		if (MoveDistance < 4000.0f && Q_irand(0, 1) == 0)
		{
			pilotFlankTime[npc->s.number] = level.time + Q_irand(8000, 14000);
		}
	}

	// Fly By Sounds
	//---------------
	if ((ActorVeh->m_pVehicleInfo->soundFlyBy || ActorVeh->m_pVehicleInfo->soundFlyBy2) &&
		TIMER_Done(npc, "FlybySoundDebouncer") &&
		EnemyVeh && MoveDistance < 800.0f && ActorSpeed > 500.0f)
	{
		if (EnemySpeed < 100.0f ||
			DotProduct(ActorDirection, EnemyDirection) * (MoveDistance / 800.0f) < -0.5f)
		{
			TIMER_Set(npc, "FlybySoundDebouncer", 2000);
			const int soundFlyBy = Pilot_FlyBySound(ActorVeh->m_pVehicleInfo);
			if (soundFlyBy)
			{
				G_Sound((gentity_t*)ActorVeh->m_pParentEntity, CHAN_AUTO, soundFlyBy);
			}
		}
	}

	// FLY PAST BEHAVIOR
	//===================
	if (EnemySlideBreak || !TIMER_Done(npc, "MinHoldDirectionTime"))
	{
		if (TIMER_Done(npc, "MinHoldDirectionTime"))
		{
			TIMER_Set(npc, "MinHoldDirectionTime", 500); // Hold For At Least 500 ms
		}
		ActorAccelerate = qtrue; // Go
		ActorAimAtTarget = qfalse; // Don't Alter Our Aim Direction
	}
	// FLANKING BEHAVIOR
	//===================
	else if (ActorFlank)
	{
		ActorAccelerate = qtrue;
		ActorDoTurbo = MoveDistance > 2500.0f || EnemyInTurbo ? qtrue : qfalse;
		pilotMatchSpeed[npc->s.number] = qtrue; // SP: BUTTON_VEH_SPEED

		npc->client->ps.speed = ActorVeh->m_pVehicleInfo->speedMax * (ActorInTurbo ? 1.35f : 1.15f);

		if (MoveDistance < ATTACK_FLANK_SLOWING)
		{
			npc->client->ps.speed *= MoveDistance / ATTACK_FLANK_SLOWING;
			npc->client->ps.speed += EnemySpeed;

			if (npc->client->ps.speed < 5.0f && EnemySpeed < 5.0f)
			{
				npc->client->ps.speed = EnemySpeed;
			}

			if (MoveAccuracy < 0.0f)
			{
				npc->client->ps.speed *= MoveAccuracy + 1.0f;
			}

			VectorScale(MoveDirection, MoveDistance / ATTACK_FLANK_SLOWING, MoveDirection);
			VectorMA(MoveDirection, 1.0f - MoveDistance / ATTACK_FLANK_SLOWING, EnemyDirection, MoveDirection);
			// (SP: a strafe ram now and then; MP's speeders have none)
		}
	}
	// NORMAL CHASE BEHAVIOR
	//=======================
	else
	{
		if (!EnemyVeh && AimAccuracy > 0.99f && MoveDistance < 500.0f && !EnemyDead)
		{
			ActorAccelerate = qtrue;
			ActorDoTurbo = qfalse;
		}
		else
		{
			ActorAccelerate = MoveDistance > 500.0f && EnemySpeed > 20.0f || MoveDistance > 1000.0f ? qtrue : qfalse;
			ActorDoTurbo = MoveDistance > 3000.0f && EnemySpeed > 20.0f ? qtrue : qfalse;
		}
	}

	// APPLY RESULTS
	//=======================
	// Decide Turbo
	if (ActorDoTurbo || ActorInTurbo)
	{
		NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
	}
	else
	{
		NPCS.ucmd.buttons &= ~BUTTON_ALT_ATTACK;
	}

	// Decide Acceleration
	NPCS.ucmd.forwardmove = ActorAccelerate ? 127 : 0;

	// Decide To Shoot (DH 2026-11-23: a pilot fires his vehicle's guns, never his own weapon - SP drew a blaster to
	// shoot to the side)
	NPCS.ucmd.buttons &= ~BUTTON_ATTACK;
	NPCS.ucmd.rightmove = 0;

	if (npc->client->ps.weapon != WP_NONE)
	{
		NPC_ChangeWeapon(WP_NONE);
	}

	if (AimDistance < 2000.0f && !EnemyDead && AimAccuracy > ATTACK_FWD)
	{
		NPCS.ucmd.buttons |= BUTTON_ATTACK;
	}
	// Aim At Target
	if (ActorAimAtTarget)
	{
		vec3_t moveAngles = { 0 };
		vectoangles(MoveDirection, moveAngles);
		info->desiredPitch = AngleNormalize360(moveAngles[PITCH]);
		info->desiredYaw = AngleNormalize360(moveAngles[YAW]);
	}

	NPC_UpdateAngles(qtrue, qtrue);
}

#endif
