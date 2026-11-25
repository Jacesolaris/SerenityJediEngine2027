#include "qcommon/q_shared.h"
#include "g_camera.h"
#include "g_local.h"
#include "g_roff.h"

//prototypes
void EnablePlayerCameraPos(gentity_t* player);

qboolean in_camera = qfalse;
camera_t client_camera = { 0 };

extern int g_TimeSinceLastFrame;

vec3_t camerapos;
vec3_t cameraang;

// camera track and path (2026-11-19: the server's copy of the camera follows them too, so the players, who are held
// at the camera, see the right part of the map; the client draws the camera, cg_camera.c)
#define GCAM_TRACK_POINTS	24
static vec3_t gcam_trackOrg[GCAM_TRACK_POINTS];
static float gcam_trackRadius[GCAM_TRACK_POINTS];
static float gcam_trackSpeed[GCAM_TRACK_POINTS];
static int gcam_trackCount = 0;
static int gcam_trackLoop = -1;
static int gcam_trackIndex = -1;
static int gcam_roffNum = 0; // G_LoadRoffs' id (0 none)
static void GCam_TrackUpdate(void);
static void GCam_Roff(void);

void GCam_Update(void)
{
	int i = 0;
	qboolean checkFollow = qfalse;

	// Apply new roff data to the camera as needed (2026-11-19)
	if (client_camera.info_state & CAMERA_ROFFING)
	{
		GCam_Roff();
	}

	//Check for roffing angles
	if (client_camera.info_state & CAMERA_ROFFING && !(client_camera.info_state & CAMERA_FOLLOWING))
	{
		if (client_camera.info_state & CAMERA_CUT)
		{
			// we're doing a cut, so just go to the new angles. none of this hifalutin lerping business.
			for (i = 0; i < 3; i++)
			{
				cameraang[i] = AngleNormalize360(client_camera.angles[i] + client_camera.angles2[i]);
			}
		}
		else
		{
			for (i = 0; i < 3; i++)
			{
				cameraang[i] = client_camera.angles[i] + client_camera.angles2[i] / client_camera.pan_duration * (level.
					time - client_camera.pan_time);
			}
		}
	}
	else if (client_camera.info_state & CAMERA_PANNING)
	{
		if (client_camera.info_state & CAMERA_CUT)
		{
			// we're doing a cut, so just go to the new angles. none of this hifalutin lerping business.
			for (i = 0; i < 3; i++)
			{
				cameraang[i] = AngleNormalize360(client_camera.angles[i] + client_camera.angles2[i]);
			}
		}
		else
		{
			//Note: does not actually change the camera's angles until the pan time is done!
			if (client_camera.pan_time + client_camera.pan_duration < level.time)
			{
				//finished panning (as SP: the end angles are kept, they used to snap back, 2026-11-16)
				for (i = 0; i < 3; i++)
				{
					client_camera.angles[i] = AngleNormalize360(client_camera.angles[i] + client_camera.angles2[i]);
				}

				client_camera.info_state &= ~CAMERA_PANNING;
				VectorCopy(client_camera.angles, cameraang);
			}
			else
			{
				//still panning
				for (i = 0; i < 3; i++)
				{
					//NOTE: does not store the resultant angle in client_camera.angles until pan is done
					cameraang[i] = client_camera.angles[i] + client_camera.angles2[i] / client_camera.pan_duration * (
						level.time - client_camera.pan_time);
				}
			}
		}
	}
	else
	{
		checkFollow = qtrue;
	}

	//Check for movement
	if (client_camera.info_state & CAMERA_MOVING)
	{
		//NOTE: does not actually move the camera until the movement time is done!
		if (client_camera.move_time + client_camera.move_duration < level.time)
		{
			VectorCopy(client_camera.origin2, client_camera.origin);
			client_camera.info_state &= ~CAMERA_MOVING;
			VectorCopy(client_camera.origin, camerapos);
		}
		else
		{
			if (client_camera.info_state & CAMERA_CUT)
			{
				// we're doing a cut, so just go to the new origin. none of this fancypants lerping stuff.
				for (i = 0; i < 3; i++)
				{
					camerapos[i] = client_camera.origin2[i];
				}
			}
			else
			{
				for (i = 0; i < 3; i++)
				{
					camerapos[i] = client_camera.origin[i] + (client_camera.origin2[i] - client_camera.origin[i]) /
						client_camera.move_duration * (level.time - client_camera.move_time);
				}
			}
		}
	}
	else if (client_camera.info_state & CAMERA_TRACKING)
	{
		// (2026-11-19: a camera track, after the move)
		GCam_TrackUpdate();
		VectorCopy(client_camera.origin, camerapos);
	}

	if (checkFollow)
	{
		if (client_camera.info_state & CAMERA_FOLLOWING)
		{
			//This needs to be done after camera movement
			GCam_FollowUpdate();
		}
		VectorCopy(client_camera.angles, cameraang);
	}
}

void GCam_FollowUpdate(void)
{
	vec3_t center = { 0 }, dir = { 0 }, cameraAngles = { 0 }, vec = { 0 }, focus[MAX_CAMERA_GROUP_SUBJECTS] = { 0 }; //No more than 16 subjects in a cameraGroup
	//centity_t	*fromCent = NULL;
	int num_subjects = 0, i = 0;

	if (client_camera.cameraGroup[0] == -1)
	{
		//follow disabled
		return;
	}

	for (i = 0; i < MAX_CAMERA_GROUP_SUBJECTS; i++)
	{
		if (client_camera.cameraGroup[i] < 0 || client_camera.cameraGroup[i] >= ENTITYNUM_WORLD)
		{
			break; // (the end of the group, 2026-11-16)
		}
		gentity_t* from = &g_entities[client_camera.cameraGroup[i]];
		if (!from->inuse)
		{
			continue;
		}

		qboolean focused = qfalse;

		if (from->client // the bolt needs its view angles and origin (C6011)
			&& (from->s.eType == ET_PLAYER
			|| from->s.eType == ET_NPC
			|| from->s.number < MAX_CLIENTS)
			&& client_camera.cameraGroupTag && client_camera.cameraGroupTag[0])
		{
			const int newBolt = trap->G2API_AddBolt(&from->ghoul2, 0, client_camera.cameraGroupTag);
			if (newBolt != -1)
			{
				mdxaBone_t boltMatrix = { 0 };
				vec3_t angle = { 0 };

				VectorSet(angle, 0, from->client->ps.viewangles[YAW], 0);

				trap->G2API_GetBoltMatrix(&from->ghoul2, 0, newBolt, &boltMatrix, angle, from->client->ps.origin,
					level.time, NULL, from->modelScale);
				BG_GiveMeVectorFromMatrix(&boltMatrix, ORIGIN, focus[num_subjects]);

				focused = qtrue;
			}
		}
		if (!focused)
		{
			VectorCopy(from->r.currentOrigin, focus[num_subjects]);

			if (from->s.eType == ET_PLAYER
				|| from->s.eType == ET_NPC
				|| from->s.number < MAX_CLIENTS)
			{
				//Track to their eyes (as SP)
				if (from->client)
				{
					focus[num_subjects][2] += from->client->ps.viewheight;
				}
			}
		}
		if (client_camera.cameraGroupZOfs)
		{
			focus[num_subjects][2] += client_camera.cameraGroupZOfs;
		}
		num_subjects++;
	}

	if (!num_subjects) // Bad cameragroup
	{
#ifndef FINAL_BUILD
		G_Printf(S_COLOR_RED"ERROR: Camera Focus unable to locate cameragroup: %s\n", client_camera.cameraGroup);
#endif
		return;
	}

	//Now average all points
	VectorCopy(focus[0], center);
	for (i = 1; i < num_subjects; i++)
	{
		VectorAdd(focus[i], center, center);
	}
	VectorScale(center, 1.0f / (float)num_subjects, center);

	//Need to set a speed to keep a distance from
	//the subject- fixme: only do this if have a distance
	//set
	VectorSubtract(client_camera.subjectPos, center, vec);
	client_camera.subjectSpeed = VectorLengthSquared(vec) * 100.0f / g_TimeSinceLastFrame;
	VectorCopy(center, client_camera.subjectPos);

	VectorSubtract(center, camerapos, dir);
	//can't use client_camera.origin because it's not updated until the end of the move.

	//Get desired angle
	vectoangles(dir, cameraAngles);

	if (client_camera.followInitLerp)
	{
		//Lerping
		const float frac = g_TimeSinceLastFrame / 100.0f * client_camera.followSpeed / 100.f;
		for (i = 0; i < 3; i++)
		{
			cameraAngles[i] = AngleNormalize180(cameraAngles[i]);
			cameraAngles[i] = AngleNormalize180(
				client_camera.angles[i] + frac * AngleNormalize180(cameraAngles[i] - client_camera.angles[i]));
			cameraAngles[i] = AngleNormalize180(cameraAngles[i]);
		}
	}
	else
	{
		//Snapping, should do this first time if follow_lerp_to_start_duration is zero
		//will lerp from this point on
		client_camera.followInitLerp = qtrue;
		for (i = 0; i < 3; i++)
		{
			//normalize so that when we start lerping, it doesn't freak out
			cameraAngles[i] = AngleNormalize180(cameraAngles[i]);
		}
		//So tracker doesn't move right away thinking the first angle change
		//is the subject moving... FIXME: shouldn't set this until lerp done OR snapped?
		client_camera.subjectSpeed = 0;
	}
	VectorCopy(cameraAngles, client_camera.angles);
}

static void GCam_FollowDisable(void)
{
	client_camera.info_state &= ~CAMERA_FOLLOWING;
	//client_camera.cameraGroup[0] = 0;
	for (int i = 0; i < MAX_CAMERA_GROUP_SUBJECTS; i++)
	{
		client_camera.cameraGroup[i] = -1;
	}
	client_camera.cameraGroupZOfs = 0;
	client_camera.cameraGroupTag[0] = 0;
}

static void GCam_TrackDisable(void)
{
	client_camera.info_state &= ~CAMERA_TRACKING;
	client_camera.trackEntNum = ENTITYNUM_WORLD;
	gcam_trackIndex = -1;
}

static void GCam_DistanceDisable(void)
{
	client_camera.distance = 0;
}

void GCam_SetPosition(vec3_t org)
{
	VectorCopy(org, client_camera.origin);
	VectorCopy(client_camera.origin, camerapos);
}

void GCam_Move(vec3_t dest, const float duration)
{
	if (client_camera.info_state & CAMERA_ROFFING)
	{
		client_camera.info_state &= ~CAMERA_ROFFING;
	}

	GCam_TrackDisable();
	GCam_DistanceDisable();

	if (!duration)
	{
		client_camera.info_state &= ~CAMERA_MOVING;
		GCam_SetPosition(dest);
		return;
	}

	client_camera.info_state |= CAMERA_MOVING;

	VectorCopy(dest, client_camera.origin2);

	client_camera.move_duration = duration;
	client_camera.move_time = level.time;
}

void GCam_Follow(int cameraGroup[MAX_CAMERA_GROUP_SUBJECTS], const float speed, const float initLerp)
{
	//Clear any previous
	GCam_FollowDisable();

	if (cameraGroup[0] == -2)
	{
		//only wanted to disable follow mode
		return;
	}

	//NOTE: if this interrupts a pan before it's done, need to copy the cg.refdef.viewAngles to the camera.angles!
	client_camera.info_state |= CAMERA_FOLLOWING;
	client_camera.info_state &= ~CAMERA_PANNING;

	for (int len = 0; len < MAX_CAMERA_GROUP_SUBJECTS; len++)
	{
		client_camera.cameraGroup[len] = cameraGroup[len];
	}

	if (speed)
	{
		client_camera.followSpeed = speed;
	}
	else
	{
		client_camera.followSpeed = 100.0f;
	}

	if (initLerp)
	{
		client_camera.followInitLerp = qtrue;
	}
	else
	{
		client_camera.followInitLerp = qfalse;
	}
}

void GCam_Enable(void)
{
	client_camera.bar_alpha = 0.0f;
	client_camera.bar_time = level.time;

	client_camera.bar_alpha_source = 0.0f;
	client_camera.bar_alpha_dest = 1.0f;

	client_camera.bar_height_source = 0.0f;
	client_camera.bar_height_dest = 480.0f / 10.0f;
	client_camera.bar_height = 0.0f;

	client_camera.info_state |= CAMERA_BAR_FADING;

	client_camera.FOV = CAMERA_DEFAULT_FOV;
	client_camera.FOV2 = CAMERA_DEFAULT_FOV;

	in_camera = qtrue;

	client_camera.next_roff_time = 0;
}

void GCam_Disable(void)
{
	//disable the server side part of the camera stuff.
	in_camera = qfalse;

	client_camera.bar_alpha = 1.0f;
	client_camera.bar_time = level.time;

	client_camera.bar_alpha_source = 1.0f;
	client_camera.bar_alpha_dest = 0.0f;

	client_camera.bar_height_source = 480.0f / 10.0f;
	client_camera.bar_height_dest = 0.0f;

	client_camera.info_state |= CAMERA_BAR_FADING;

	DisablePlayerCameraPos();
}

void GCam_SetAngles(vec3_t ang)
{
	VectorCopy(ang, client_camera.angles);
	VectorCopy(client_camera.angles, cameraang);
}

void GCam_Pan(vec3_t dest, vec3_t panDirection, const float duration)
{
	float delta2 = 0.0f;

	GCam_FollowDisable();
	GCam_DistanceDisable();

	if (!duration)
	{
		GCam_SetAngles(dest);
		client_camera.info_state &= ~CAMERA_PANNING;
		return;
	}

	//FIXME: make the dest an absolute value, and pass in a
	//panDirection as well.  If a panDirection's axis value is
	//zero, find the shortest difference for that axis.
	//Store the delta in client_camera.angles2.
	for (int i = 0; i < 3; i++)
	{
		dest[i] = AngleNormalize360(dest[i]);
		const float delta1 = dest[i] - AngleNormalize360(client_camera.angles[i]);
		if (delta1 < 0)
		{
			delta2 = delta1 + 360;
		}
		else
		{
			delta2 = delta1 - 360;
		}
		if (!panDirection[i])
		{
			//Didn't specify a direction, pick shortest
			if (Q_fabs(delta1) < Q_fabs(delta2))
			{
				client_camera.angles2[i] = delta1;
			}
			else
			{
				client_camera.angles2[i] = delta2;
			}
		}
		else if (panDirection[i] < 0)
		{
			if (delta1 < 0)
			{
				client_camera.angles2[i] = delta1;
			}
			else if (delta1 > 0)
			{
				client_camera.angles2[i] = delta2;
			}
			else
			{
				//exact
				client_camera.angles2[i] = 0;
			}
		}
		else if (panDirection[i] > 0)
		{
			if (delta1 > 0)
			{
				client_camera.angles2[i] = delta1;
			}
			else if (delta1 < 0)
			{
				client_camera.angles2[i] = delta2;
			}
			else
			{
				//exact
				client_camera.angles2[i] = 0;
			}
		}
	}

	client_camera.info_state |= CAMERA_PANNING;

	client_camera.pan_duration = duration;
	client_camera.pan_time = level.time;
}

/*==================================
Player Positional Stuff
====================================*/

typedef struct
{
	vec3_t origin;
	vec3_t viewangles;
	qboolean inuse;
} playerPos_t;

//stores the original positions of all the players so the game can move them around for cutscenes.
playerPos_t playerCamPos[MAX_CLIENTS];

//while in a cutscene have all the player origins/angles on the camera's.  we do this to
//make the game render the scenes for the camera correctly.
void UpdatePlayerCameraPos(void)
{
	if (!in_camera)
	{
		return;
	}

	GCam_Update();
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		gentity_t* player = &g_entities[i];
		if (!player || !player->client || !player->inuse
			|| player->client->pers.connected != CON_CONNECTED)
		{
			//player not ingame
			continue;
		}

		if (!playerCamPos[i].inuse)
		{
			//player hasn't been snapped to camera yet
			EnablePlayerCameraPos(player);
		}

		//move the player origin/angles to the camera's
		VectorCopy(camerapos, player->client->ps.origin);
		VectorCopy(cameraang, player->client->ps.viewangles);
	}
}

extern void WP_SaberUpdateSelf(gentity_t* ent);
extern void WP_SaberRemoveG2Model(gentity_t* saberent);

void EnablePlayerCameraPos(gentity_t* player)
{
	//set this player it be in camera mode

	if (!player || !player->client || !player->inuse || player->s.number >= MAX_CLIENTS) // playerCamPos has MAX_CLIENTS entries
	{
		//bad player entity
		return;
	}

	//turn off zoomMode
	player->client->ps.zoomMode = 0;

	//holster sabers
	player->client->ps.saberHolstered = 2;

	if (player->client->ps.saberInFlight && player->client->ps.saberEntityNum)
	{
		//saber is out
		gentity_t* saberent = &g_entities[player->client->ps.saberEntityNum];
		if (saberent)
		{
			//force the weapon back to our hand.
			saberent->genericValue5 = 0;
			saberent->think = WP_SaberUpdateSelf;
			saberent->nextthink = level.time;
			WP_SaberRemoveG2Model(saberent);

			player->client->ps.saberInFlight = qfalse;
			player->client->ps.saberEntityState = 0;
			player->client->ps.saberThrowDelay = level.time + 500;
			player->client->ps.saberCanThrow = qfalse;
		}
	}

	for (int x = 0; x < NUM_FORCE_POWERS; x++)
	{
		//deactivate any active force powers
		player->client->ps.fd.forcePowerDuration[x] = 0;
		if (player->client->ps.fd.forcePowerDuration[x] || player->client->ps.fd.forcePowersActive & 1 << x)
		{
			WP_ForcePowerStop(player, x);
		}
	}

	VectorClear(player->client->ps.velocity);

	//make invisible
	player->s.eFlags |= EF_NODRAW;
	player->client->ps.eFlags |= EF_NODRAW;

	//go noclip
	player->client->noclip = qtrue;

	//save our current position to the array
	VectorCopy(player->client->ps.origin, playerCamPos[player->s.number].origin);
	VectorCopy(player->client->ps.viewangles, playerCamPos[player->s.number].viewangles);
	playerCamPos[player->s.number].inuse = qtrue;
}

extern qboolean SPSpawnpointCheck(vec3_t spawnloc);

void DisablePlayerCameraPos(void)
{
	//disable the player camera position code.

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (!playerCamPos[i].inuse)
		{
			//player's camera position was never set, just move on.
			continue;
		}

		//since we're going to be done with the player positional data no matter what after this,
		//say that we're done with it.
		playerCamPos[i].inuse = qfalse;

		gentity_t* player = &g_entities[i];
		if (!player || !player->client || !player->inuse
			|| player->client->pers.connected != CON_CONNECTED)
		{
			//player not ingame
			continue;
		}

		//make solid and make visible
		player->client->noclip = qfalse;
		player->s.eFlags &= ~EF_NODRAW;
		player->client->ps.eFlags &= ~EF_NODRAW;

		//check our respawn point
		if (!SPSpawnpointCheck(playerCamPos[i].origin))
		{
			//couldn't spawn in our original area, kill them.
			G_Damage(player, NULL, NULL, NULL, NULL, 100000, DAMAGE_NO_PROTECTION, MOD_TELEFRAG);
			continue;
		}

		//flip the teleport flag so this dude doesn't client lerp
		int flags = player->client->ps.eFlags & EF_TELEPORT_BIT;
		flags ^= EF_TELEPORT_BIT;
		player->client->ps.eFlags = flags;

		//restore view angle
		SetClientViewAngle(player, playerCamPos[i].viewangles);

		//found good spot, move them there
		G_SetOrigin(player, playerCamPos[i].origin);
		VectorCopy(playerCamPos[i].origin, player->client->ps.origin);

		trap->LinkEntity((sharedEntity_t*)player);
	}
}

void UpdatePlayerCameraOrigin(const gentity_t* ent, vec3_t newPos)
{
	//this alters the position at which the player will return to after the camera commands are over.
	if (!in_camera || ent->s.number >= MAX_CLIENTS || !playerCamPos[ent->s.number].inuse)
	{
		//not using camera or not able to use camera
		return;
	}

	VectorCopy(newPos, playerCamPos[ent->s.number].origin);
}

void UpdatePlayerCameraAngle(const gentity_t* ent, vec3_t newAngle)
{
	//this function alters the angle at which the player will return to after the camera commands are over.
	if (!in_camera || ent->s.number >= MAX_CLIENTS || !playerCamPos[ent->s.number].inuse)
	{
		//not using camera or not able to use camera
		return;
	}

	VectorCopy(newAngle, playerCamPos[ent->s.number].viewangles);
}

/*
-------------------------
Camera track and path (2026-11-19)

camera( TRACK, "<first path_corner>", speed, initLerp ) and camera( PATH, "<roff>" ), as SP's cgame
(cg_camera.cpp CGCam_Track / CGCam_StartRoff), on the server's copy of the camera: the players are held at it
(UpdatePlayerCameraPos), so what they are sent follows it. The client runs its own (cg_camera.c).
-------------------------
*/
void GCam_TrackPoints(const vec3_t* org, const float* radius, const float* speed, const int count, const int loop,
	const float trackSpeed, const float initLerp)
{
	GCam_TrackDisable();

	if (count <= 0)
	{
		return; // turned off
	}

	gcam_trackCount = count > GCAM_TRACK_POINTS ? GCAM_TRACK_POINTS : count;
	for (int i = 0; i < gcam_trackCount; i++)
	{
		VectorCopy(org[i], gcam_trackOrg[i]);
		gcam_trackRadius[i] = radius[i];
		gcam_trackSpeed[i] = speed[i];
	}
	gcam_trackLoop = loop >= 0 && loop < gcam_trackCount ? loop : -1;
	gcam_trackIndex = 0;

	client_camera.info_state |= CAMERA_TRACKING;
	client_camera.info_state &= ~CAMERA_MOVING;
	client_camera.trackEntNum = 0;
	client_camera.initSpeed = trackSpeed / 10.0f;
	client_camera.speed = trackSpeed;
	client_camera.nextTrackEntUpdateTime = level.time;
	client_camera.trackInitLerp = initLerp ? qtrue : qfalse;

	VectorCopy(gcam_trackOrg[0], client_camera.trackToOrg);
	VectorSubtract(gcam_trackOrg[0], client_camera.origin, client_camera.moveDir);

	if (!client_camera.trackInitLerp)
	{
		//want to snap to first position
		VectorCopy(gcam_trackOrg[0], client_camera.origin);
		if (gcam_trackCount > 1)
		{
			VectorSubtract(gcam_trackOrg[1], client_camera.origin, client_camera.moveDir);
		}
	}
	VectorNormalize(client_camera.moveDir);
	VectorCopy(client_camera.origin, camerapos);
}

void GCam_Distance(const float distance, const float initLerp)
{
	client_camera.distance = distance;
	client_camera.distanceInitLerp = initLerp ? qtrue : qfalse;
}

static void GCam_TrackPointUpdate(void)
{
	qboolean reached = qfalse;

	if (gcam_trackIndex >= 0 && gcam_trackIndex < gcam_trackCount)
	{
		vec3_t vec = { 0 };
		VectorSubtract(gcam_trackOrg[gcam_trackIndex], client_camera.origin, vec);
		if (VectorLengthSquared(vec) < 256) //16 squared
		{
			reached = qtrue;
		}
	}

	if (reached)
	{
		int next = gcam_trackIndex + 1;
		if (next >= gcam_trackCount)
		{
			next = gcam_trackLoop;
		}
		if (next < 0)
		{
			GCam_TrackDisable(); // the last one
			return;
		}
		if (gcam_trackRadius[next] < 0)
		{
			client_camera.distance = 0;
			client_camera.speed = client_camera.initSpeed;
		}
		else if (gcam_trackRadius[next] > 0)
		{
			client_camera.distance = gcam_trackRadius[next];
		}
		if (gcam_trackSpeed[next] < 0)
		{
			client_camera.speed = client_camera.initSpeed;
		}
		else if (gcam_trackSpeed[next] > 0)
		{
			client_camera.speed = gcam_trackSpeed[next] / 10.0f;
		}
		gcam_trackIndex = next;
		client_camera.trackEntNum = next;
		VectorCopy(gcam_trackOrg[next], client_camera.trackToOrg);
	}

	client_camera.nextTrackEntUpdateTime = level.time + 100;
}

static void GCam_TrackUpdate(void)
{
	vec3_t goalVec = { 0 }, curVec = { 0 };
	float ft = (float)g_TimeSinceLastFrame;

	if (client_camera.nextTrackEntUpdateTime <= level.time)
	{
		GCam_TrackPointUpdate();
	}
	if (!(client_camera.info_state & CAMERA_TRACKING))
	{
		return;
	}
	if (ft <= 0.0f)
	{
		return;
	}
	if (ft > 100.0f)
	{
		ft = 100.0f;
	}

	// (SP's steering; its keeping a distance from a followed subject is the client's alone)
	VectorSubtract(client_camera.trackToOrg, client_camera.origin, goalVec);
	VectorNormalize(goalVec);
	VectorScale(goalVec, ft / 100.0f, goalVec);
	VectorScale(client_camera.moveDir, (100.0f - ft) / 100.0f, curVec);
	VectorAdd(goalVec, curVec, client_camera.moveDir);
	VectorNormalize(client_camera.moveDir);
	VectorMA(client_camera.origin, client_camera.speed * ft / 100.0f, client_camera.moveDir, client_camera.origin);
}

void GCam_PlayRoff(const char* roff)
{
	GCam_FollowDisable();
	GCam_TrackDisable();

	gcam_roffNum = G_LoadRoffs(roff);
	if (!gcam_roffNum)
	{
		return;
	}

	client_camera.info_state |= CAMERA_MOVING;
	client_camera.info_state |= CAMERA_PANNING;
	client_camera.info_state |= CAMERA_ROFFING;

	Q_strncpyz(client_camera.sRoff, roff, sizeof client_camera.sRoff);
	client_camera.roff_frame = 0;
	client_camera.next_roff_time = level.time;
}

static void GCam_StopRoff(void)
{
	client_camera.info_state &= ~CAMERA_ROFFING;
	client_camera.info_state &= ~CAMERA_MOVING;
	gcam_roffNum = 0;
}

static void GCam_Roff(void)
{
	while (client_camera.next_roff_time <= level.time)
	{
		if (gcam_roffNum <= 0 || gcam_roffNum > num_roffs)
		{
			GCam_StopRoff();
			return;
		}

		const roff_list_t* roff = &roffs[gcam_roffNum - 1];
		const int frame = client_camera.roff_frame;
		vec3_t org = { 0 }, ang = { 0 };

		if (frame >= roff->frames || frame >= MAXNUMDATANEW)
		{
			GCam_StopRoff();
			return;
		}

		VectorCopy(roff->data[frame].origin_delta, org);
		VectorCopy(roff->data[frame].rotate_delta, ang);
		ang[ROLL] = -ang[ROLL];

		if (frame)
		{
			if (!(client_camera.info_state & CAMERA_FOLLOWING))
			{
				VectorAdd(client_camera.angles, client_camera.angles2, client_camera.angles);
			}
			VectorCopy(client_camera.origin2, client_camera.origin);
		}

		if (!(client_camera.info_state & CAMERA_FOLLOWING))
		{
			VectorCopy(ang, client_camera.angles2);
			client_camera.pan_time = level.time;
			client_camera.pan_duration = roff->mFrameTime;
		}

		VectorAdd(client_camera.origin, org, client_camera.origin2);

		client_camera.move_time = level.time;
		client_camera.move_duration = roff->mFrameTime;

		if (++client_camera.roff_frame >= roff->frames)
		{
			GCam_StopRoff();
			return;
		}

		client_camera.next_roff_time += roff->mFrameTime;
	}
}
