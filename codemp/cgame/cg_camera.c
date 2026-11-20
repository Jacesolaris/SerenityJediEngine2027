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

//Client camera controls for cinematics

#include "cg_local.h"
#include "cg_camera.h"

// this line must stay at top so the whole PCH thing works...

qboolean in_camera = qfalse;
camera_t client_camera = { 0 };
#define Vector6Copy(a,b)		((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2],(b)[3]=(a)[3])

void CGCam_FollowDisable(void);
void CGCam_TrackDisable(void);
void CGCam_DistanceDisable(void);
extern int CG_CalcFOVFromX(float fov_x);

/*
-------------------------
CGCam_Init
-------------------------
*/

void CGCam_Init(void)
{
	/* RAFIXME - need to figure out where this comes from.
	extern qboolean qbVidRestartOccured;
	if (!qbVidRestartOccured)
	{
		memset( &client_camera, 0, sizeof ( camera_t ) );
	}
	*/
}

/*
-------------------------
CGCam_Enable
-------------------------
*/
extern void CG_CalcVrect(void);

void CGCam_Enable(void)
{
	client_camera.bar_alpha = 0.0f;
	client_camera.bar_time = cg.time;

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

	//if (&g_entities[0] && g_entities[0].client)
	//{
	//	//Player zero not allowed to do anything
	//	VectorClear(g_entities[0].client->ps.velocity);
	//	g_entities[0].contents = 0;

	//	if (cg.zoomMode)
	//	{
	//		// need to shut off some form of zooming
	//		cg.zoomMode = 0;
	//	}

	//	if (g_entities[0].client->ps.saberInFlight && g_entities[0].client->ps.saber[0].Active())
	//	{//saber is out
	//		gentity_t* saberent = &g_entities[g_entities[0].client->ps.saberEntityNum];
	//		if (saberent)
	//		{
	//			WP_SaberCatch(&g_entities[0], saberent, qfalse);
	//		}
	//	}

	//	for (int i = 0; i < NUM_FORCE_POWERS; i++)
	//	{//deactivate any active force powers
	//		g_entities[0].client->ps.forcePowerDuration[i] = 0;
	//		extern void WP_ForcePowerStop(gentity_t * self, forcePowers_t forcePower);
	//		if (g_entities[0].client->ps.forcePowerDuration[i] || (g_entities[0].client->ps.forcePowersActive & (1 << i)))
	//		{
	//			WP_ForcePowerStop(&g_entities[0], (forcePowers_t)i);
	//		}
	//	}
	//}
}

/*
-------------------------
CGCam_Disable
-------------------------
*/

void CGCam_Disable(void)
{
	in_camera = qfalse;

	client_camera.bar_alpha = 1.0f;
	client_camera.bar_time = cg.time;

	client_camera.bar_alpha_source = 1.0f;
	client_camera.bar_alpha_dest = 0.0f;

	client_camera.bar_height_source = 480.0f / 10.0f;
	client_camera.bar_height_dest = 0.0f;

	client_camera.info_state |= CAMERA_BAR_FADING;

	//RAFIXME - what does this do?
	//trap_SendClientCommand( "cts" );

	//if ( cg_skippingcin.integer )
	//{//We're skipping the cinematic and it's over now
	//	gi.cvar_set("timescale", "1");
	//	gi.cvar_set("skippingCinematic", "0");
	//}

	//we just came out of camera, so update cg.refdef.vieworg out of the camera's origin so the snapshot will know our new ori
	VectorCopy(cg.predictedPlayerState.origin, cg.refdef.vieworg);
	VectorCopy(cg.predictedPlayerState.viewangles, cg.refdef.viewangles);
}

/*
-------------------------
CGCam_SetPosition
-------------------------
*/

void CGCam_SetPosition(vec3_t org)
{
	VectorCopy(org, client_camera.origin);
	VectorCopy(client_camera.origin, cg.refdef.vieworg);
}

/*
-------------------------
CGCam_Move
-------------------------
*/

void CGCam_Move(vec3_t dest, const float duration)
{
	if (client_camera.info_state & CAMERA_ROFFING)
	{
		client_camera.info_state &= ~CAMERA_ROFFING;
	}

	CGCam_TrackDisable();
	CGCam_DistanceDisable();

	if (!duration)
	{
		client_camera.info_state &= ~CAMERA_MOVING;
		CGCam_SetPosition(dest);
		return;
	}

	client_camera.info_state |= CAMERA_MOVING;

	VectorCopy(dest, client_camera.origin2);

	client_camera.move_duration = duration;
	client_camera.move_time = cg.time;
}

/*
-------------------------
CGCam_SetAngles
-------------------------
*/

void CGCam_SetAngles(vec3_t ang)
{
	VectorCopy(ang, client_camera.angles);
	VectorCopy(client_camera.angles, cg.refdef.viewangles);
}

/*
-------------------------
CGCam_Pan
-------------------------
*/

void CGCam_Pan(vec3_t dest, vec3_t panDirection, const float duration)
{
	float delta2 = 0.0f;

	CGCam_FollowDisable();
	CGCam_DistanceDisable();

	if (!duration)
	{
		CGCam_SetAngles(dest);
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
	client_camera.pan_time = cg.time;
}

/*
-------------------------
CGCam_SetRoll
-------------------------
*/

void CGCam_SetRoll(const float roll)
{
	client_camera.angles[2] = roll;
}

/*
-------------------------
CGCam_Roll
-------------------------
*/

void CGCam_Roll(const float dest, const float duration)
{
	if (!duration)
	{
		CGCam_SetRoll(dest);
		return;
	}

	//FIXME/NOTE: this will override current panning!!!
	client_camera.info_state |= CAMERA_PANNING;

	VectorCopy(client_camera.angles, client_camera.angles2);
	client_camera.angles2[2] = AngleDelta(dest, client_camera.angles[2]);

	client_camera.pan_duration = duration;
	client_camera.pan_time = cg.time;
}

/*
-------------------------
CGCam_SetFOV
-------------------------
*/

void CGCam_SetFOV(const float FOV)
{
	client_camera.FOV = FOV;
}

/*
-------------------------
CGCam_Zoom
-------------------------
*/

void CGCam_Zoom(const float FOV, const float duration)
{
	if (!duration)
	{
		CGCam_SetFOV(FOV);
		return;
	}
	client_camera.info_state |= CAMERA_ZOOMING;

	client_camera.FOV_time = cg.time;
	client_camera.FOV2 = FOV;

	client_camera.FOV_duration = duration;
}

void CGCam_Zoom2(const float FOV, const float FOV2, const float duration)
{
	if (!duration)
	{
		CGCam_SetFOV(FOV2);
		return;
	}
	client_camera.info_state |= CAMERA_ZOOMING;

	client_camera.FOV_time = cg.time;
	client_camera.FOV = FOV;
	client_camera.FOV2 = FOV2;

	client_camera.FOV_duration = duration;
}

void CGCam_ZoomAccel(const float initialFOV, const float fovVelocity, const float fovAccel, const float duration)
{
	if (!duration)
	{
		return;
	}
	client_camera.info_state |= CAMERA_ACCEL;

	client_camera.FOV_time = cg.time;
	client_camera.FOV2 = initialFOV;
	client_camera.FOV_vel = fovVelocity;
	client_camera.FOV_acc = fovAccel;

	client_camera.FOV_duration = duration;
}

/*
-------------------------
CGCam_Fade
-------------------------
*/

void CGCam_SetFade(vec4_t dest)
{
	//Instant completion
	client_camera.info_state &= ~CAMERA_FADING;
	client_camera.fade_duration = 0;
	Vector6Copy(dest, client_camera.fade_source);
	Vector6Copy(dest, client_camera.fade_color);
}

/*
-------------------------
CGCam_Fade
-------------------------
*/

void CGCam_Fade(vec4_t source, vec4_t dest, const float duration)
{
	if (!duration)
	{
		CGCam_SetFade(dest);
		return;
	}

	Vector6Copy(source, client_camera.fade_source);
	Vector6Copy(dest, client_camera.fade_dest);

	client_camera.fade_duration = duration;
	client_camera.fade_time = cg.time;

	client_camera.info_state |= CAMERA_FADING;
}

/*
-------------------------
Camera track (2026-11-19)

SP's track follows a chain of path_corners, which only the server has in MP, so the server sends the chain
(g_ICARUScb.c ICam_Track): "track <speed> <initLerp> <count> <loop> <x y z radius speed>...", loop the point
the last one leads back to (-1 none). The camera then heads from point to point as SP's CGCam_TrackEntUpdate does.
-------------------------
*/
#define CAM_TRACK_POINTS	24

static vec3_t cam_trackOrg[CAM_TRACK_POINTS];
static float cam_trackRadius[CAM_TRACK_POINTS];
static float cam_trackSpeed[CAM_TRACK_POINTS];
static int cam_trackCount = 0;
static int cam_trackLoop = -1;
static int cam_trackIndex = -1;

static void CGCam_TrackPoints(const float speed, const int initLerp)
{
	CGCam_TrackDisable();

	if (cam_trackCount <= 0)
	{
		return; // turned off
	}

	//NOTE: if this interrupts a move before it's done, need to copy the cg.refdef.vieworg to the camera.origin!
	client_camera.info_state |= CAMERA_TRACKING;
	client_camera.info_state &= ~CAMERA_MOVING;

	cam_trackIndex = 0;
	client_camera.trackEntNum = 0;
	client_camera.initSpeed = speed / 10.0f;
	client_camera.speed = speed;
	client_camera.nextTrackEntUpdateTime = cg.time;
	client_camera.trackInitLerp = initLerp ? qtrue : qfalse;

	// (heads for the first point; SP left trackToOrg where it was)
	VectorCopy(cam_trackOrg[0], client_camera.trackToOrg);

	//Set a moveDir
	VectorSubtract(cam_trackOrg[0], client_camera.origin, client_camera.moveDir);

	if (!client_camera.trackInitLerp)
	{
		//want to snap to first position
		VectorCopy(cam_trackOrg[0], client_camera.origin);

		//Set new moveDir if the track has a next point
		if (cam_trackCount > 1)
		{
			VectorSubtract(cam_trackOrg[1], client_camera.origin, client_camera.moveDir);
		}
	}

	VectorNormalize(client_camera.moveDir);
}

static void CGCam_TrackParse(const char* s)
{
	float speed = 0.0f;
	int initLerp = 0, count = 0, loop = -1, used = 0;

	if (sscanf(s, "%*s %f %d %d %d%n", &speed, &initLerp, &count, &loop, &used) < 4)
	{
		Com_Printf("CG_CameraParse WARNING: bad 'track' format\n");
		return;
	}
	s += used;

	if (count > CAM_TRACK_POINTS)
	{
		count = CAM_TRACK_POINTS;
	}
	cam_trackCount = 0;
	for (int i = 0; i < count; i++)
	{
		if (sscanf(s, "%f %f %f %f %f%n", &cam_trackOrg[i][0], &cam_trackOrg[i][1], &cam_trackOrg[i][2],
			&cam_trackRadius[i], &cam_trackSpeed[i], &used) < 5)
		{
			break;
		}
		s += used;
		cam_trackCount++;
	}
	cam_trackLoop = loop >= 0 && loop < cam_trackCount ? loop : -1;

	CGCam_TrackPoints(speed, initLerp);
}

void CGCam_FollowDisable(void)
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

void CGCam_TrackDisable(void)
{
	client_camera.info_state &= ~CAMERA_TRACKING;
	client_camera.trackEntNum = ENTITYNUM_WORLD;
	cam_trackIndex = -1;
}

void CGCam_DistanceDisable(void)
{
	client_camera.distance = 0;
}

/*
-------------------------
CGCam_Follow
-------------------------
*/

void CGCam_Follow(int cameraGroup[MAX_CAMERA_GROUP_SUBJECTS], const float speed, const float initLerp)
{
	//Clear any previous
	CGCam_FollowDisable();

	if (cameraGroup[0] == -2)
	{
		//only wanted to disable follow mode
		return;
	}

	if (!cameraGroup || !cameraGroup[0])
	{
		return;
	}

	if (Q_stricmp("none", (char*)cameraGroup) == 0)
	{
		//Turn off all aiming
		return;
	}

	if (Q_stricmp("NULL", (char*)cameraGroup) == 0)
	{
		//Turn off all aiming
		return;
	}

	//NOTE: if this interrupts a pan before it's done, need to copy the cg.refdef.viewAngles to the camera.angles!
	client_camera.info_state |= CAMERA_FOLLOWING;
	client_camera.info_state &= ~CAMERA_PANNING;

	//len = strlen(cameraGroup);
	//strncpy( client_camera.cameraGroup, cameraGroup, sizeof(client_camera.cameraGroup) );
	//NULL terminate last char in case they type a name too long
	//client_camera.cameraGroup[len] = 0;

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

/*
-------------------------
CGCam_Track
-------------------------
*/
//void CGCam_Track( char *trackName, float speed, float duration )
//void CGCam_Track( const char *trackName, float speed, float initLerp )
void CGCam_Track(const int trackNum, const float speed, const float init_lerp)
{
	//gentity_t	*trackEnt = NULL;
	const centity_t* trackEnt = &cg_entities[trackNum];

	CGCam_TrackDisable();

	if (trackNum == ENTITYNUM_WORLD)
	{
		//turn off tracking
		return;
	}

	/*
	if(Q_stricmp("none", (char *)trackName) == 0)
	{//turn off tracking
		return;
	}

	//NOTE: if this interrupts a move before it's done, need to copy the cg.refdef.vieworg to the camera.origin!
	//This will find a path_corner now, not a misc_camera_track
	trackEnt = G_Find(NULL, FOFS(targetname), (char *)trackName);

	if ( !trackEnt )
	{
		gi.Printf(S_COLOR_RED"ERROR: %s camera track target not found\n", trackName);
		return;
	}
	*/

	client_camera.info_state |= CAMERA_TRACKING;
	client_camera.info_state &= ~CAMERA_MOVING;

	//client_camera.trackEntNum = trackEnt->s.number;
	client_camera.trackEntNum = trackNum;
	client_camera.initSpeed = speed / 10.0f;
	client_camera.speed = speed;
	client_camera.nextTrackEntUpdateTime = cg.time;

	if (init_lerp)
	{
		client_camera.trackInitLerp = qtrue;
	}
	else
	{
		client_camera.trackInitLerp = qfalse;
	}
	/*
	if ( client_camera.info_state & CAMERA_FOLLOWING )
	{//Used to snap angles?  Do what...?
	}
	*/

	//Set a moveDir
	VectorSubtract(trackEnt->currentState.origin, client_camera.origin, client_camera.moveDir);

	if (!client_camera.trackInitLerp)
	{
		//want to snap to first position
		//Snap to trackEnt's origin
		//VectorCopy( trackEnt->currentOrigin, client_camera.origin );
		VectorCopy(trackEnt->currentState.origin, client_camera.origin);

		//Set new moveDir if trackEnt has a next path_corner
		//Possible that track has no next point, in which case we won't be moving anyway

		// Set new moveDir if trackEnt has a next path_corner
		//Possible that track has no next point, in which case we won't be moving anyway
		/*if ( trackEnt->target && trackEnt->target[0] )
		{
			gentity_t *newTrackEnt = G_Find( NULL, FOFS(targetname), trackEnt->target );
			if ( newTrackEnt )
			{
				VectorSubtract( newTrackEnt->currentOrigin, client_camera.origin, client_camera.moveDir );
			}
		}*/
	}

	VectorNormalize(client_camera.moveDir);
}

/*
-------------------------
CGCam_Distance
-------------------------
*/

void CGCam_Distance(const float distance, const float initLerp)
{
	client_camera.distance = distance;

	if (initLerp)
	{
		client_camera.distanceInitLerp = qtrue;
	}
	else
	{
		client_camera.distanceInitLerp = qfalse;
	}
}

//========================================================================================

static void CGCam_FollowUpdate(void)
{
	vec3_t center, dir, cameraAngles, vec, focus[MAX_CAMERA_GROUP_SUBJECTS] = { 0 }; //No more than 16 subjects in a cameraGroup
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
			break; // (the end of the group: the unused slots used to be entity 0, the player at the camera, 2026-11-16)
		}
		centity_t* fromCent = &cg_entities[client_camera.cameraGroup[i]];
		if (!fromCent->currentValid)
		{
			continue; // (not in this snapshot)
		}

		qboolean focused = qfalse;

		if ((fromCent->currentState.eType == ET_PLAYER
			|| fromCent->currentState.eType == ET_NPC
			|| fromCent->currentState.number < MAX_CLIENTS)
			&& client_camera.cameraGroupTag && client_camera.cameraGroupTag[0])
		{
			const int newBolt = trap->G2API_AddBolt(&fromCent->ghoul2, 0, client_camera.cameraGroupTag);
			if (newBolt != -1)
			{
				mdxaBone_t boltMatrix;

				trap->G2API_GetBoltMatrix(&fromCent->ghoul2, 0, newBolt, &boltMatrix, fromCent->turAngles,
					fromCent->lerpOrigin, cg.time, cgs.game_models, fromCent->modelScale);
				BG_GiveMeVectorFromMatrix(&boltMatrix, ORIGIN, focus[num_subjects]);

				focused = qtrue;
			}
		}
		if (!focused)
		{
			if (fromCent->currentState.pos.trType != TR_STATIONARY)
			{
				//use interpolated origin?
				if (VectorCompare(vec3_origin, fromCent->lerpOrigin))
				{
					//we can't cheat like we could for SP
					continue;
				}
				VectorCopy(fromCent->lerpOrigin, focus[num_subjects]);
			}
			else
			{
				VectorCopy(fromCent->currentState.origin, focus[num_subjects]);
			}
			if (fromCent->currentState.eType == ET_PLAYER
				|| fromCent->currentState.eType == ET_NPC
				|| fromCent->currentState.number < MAX_CLIENTS)
			{
				//Track to their eyes - FIXME: maybe go off a tag?
				//focus[num_subjects][2] += from->client->ps.viewheight;
				//wing it
				focus[num_subjects][2] += DEFAULT_VIEWHEIGHT;
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
		CG_Printf(S_COLOR_RED"ERROR: Camera Focus unable to locate cameragroup: %s\n", client_camera.cameraGroup);
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

	/*
	if ( client_camera.cameraGroup && client_camera.cameraGroup[0] )
	{
		//Stay centered in my cameraGroup, if I have one
		while( NULL != (from = G_Find(from, FOFS(cameraGroup), client_camera.cameraGroup)))
		{
			/*
			if ( from->s.number == client_camera.aimEntNum )
			{//This is the misc_camera_focus, we'll be removing this ent altogether eventually
				continue;
			}
			*/
			/*
			if ( num_subjects >= MAX_CAMERA_GROUP_SUBJECTS )
			{
				gi.Printf(S_COLOR_RED"ERROR: Too many subjects in shot composition %s", client_camera.cameraGroup);
				break;
			}

			fromCent = &cg_entities[from->s.number];
			if ( !fromCent )
			{
				continue;
			}

			focused = qfalse;
			if ( from->client && client_camera.cameraGroupTag && client_camera.cameraGroupTag[0] && fromCent->gent->ghoul2.size() )
			{
				int newBolt = gi.G2API_AddBolt( &fromCent->gent->ghoul2[from->playerModel], client_camera.cameraGroupTag );
				if ( newBolt != -1 )
				{
					mdxaBone_t	boltMatrix;
					vec3_t	fromAngles = {0,from->client->ps.legsYaw,0};

					gi.G2API_GetBoltMatrix( fromCent->gent->ghoul2, from->playerModel, newBolt, &boltMatrix, fromAngles, fromCent->lerpOrigin, cg.time, cgs.model_draw, fromCent->currentState.modelScale );
					gi.G2API_GiveMeVectorFromMatrix( boltMatrix, ORIGIN, focus[num_subjects] );

					focused = qtrue;
				}
			}
			if ( !focused )
			{
				if ( from->s.pos.trType != TR_STATIONARY )
		//				if ( from->s.pos.trType == TR_INTERPOLATE )
				{//use interpolated origin?
					if ( !VectorCompare( vec3_origin, fromCent->lerpOrigin ) )
					{//hunh?  Somehow we've never seen this gentity on the client, so there is no lerpOrigin, so cheat over to the game and use the currentOrigin
						VectorCopy( from->currentOrigin, focus[num_subjects] );
					}
					else
					{
						VectorCopy( fromCent->lerpOrigin, focus[num_subjects] );
					}
				}
				else
				{
					VectorCopy(from->currentOrigin, focus[num_subjects]);
				}
				//FIXME: make a list here of their s.numbers instead so we can do other stuff with the list below
				if ( from->client )
				{//Track to their eyes - FIXME: maybe go off a tag?
					//FIXME:
					//Based on FOV and distance to subject from camera, pick the point that
					//keeps eyes 3/4 up from bottom of screen... what about bars?
					focus[num_subjects][2] += from->client->ps.viewheight;
				}
			}
			if ( client_camera.cameraGroupZOfs )
			{
				focus[num_subjects][2] += client_camera.cameraGroupZOfs;
			}
			num_subjects++;
		}

		if ( !num_subjects )	// Bad cameragroup
		{
		#ifndef FINAL_BUILD
			gi.Printf(S_COLOR_RED"ERROR: Camera Focus unable to locate cameragroup: %s\n", client_camera.cameraGroup);
		#endif
			return;
		}

		//Now average all points
		VectorCopy( focus[0], center );
		for( i = 1; i < num_subjects; i++ )
		{
			VectorAdd( focus[i], center, center );
		}
		VectorScale( center, 1.0f/((float)num_subjects), center );
		}
		else
		{
		return;
		}
		*/

		//Need to set a speed to keep a distance from
		//the subject- fixme: only do this if have a distance
		//set
	VectorSubtract(client_camera.subjectPos, center, vec);
	client_camera.subjectSpeed = VectorLengthSquared(vec) * 100.0f / cg.frametime;

	/*
	if ( !cg_skippingcin.integer )
	{
		Com_Printf( S_COLOR_RED"org: %s\n", vtos(center) );
	}
	*/
	VectorCopy(center, client_camera.subjectPos);

	// from where the camera is now (2026-11-16): cg.refdef.vieworg is only set from client_camera.origin after this
	// (CGCam_Update), so a camera that was not moving aimed from (0,0,0) and never faced the actors
	{
		vec3_t cam_org;
		if (client_camera.info_state & CAMERA_MOVING)
		{
			VectorCopy(cg.refdef.vieworg, cam_org); // (set by the move above)
		}
		else
		{
			VectorCopy(client_camera.origin, cam_org);
		}
		VectorSubtract(center, cam_org, dir);
	}
	//can't use client_camera.origin because it's not updated until the end of the move.

	//Get desired angle
	vectoangles(dir, cameraAngles);

	if (client_camera.followInitLerp)
	{
		//Lerping
		const float frac = cg.frametime / 100.0f * client_camera.followSpeed / 100.f;
		for (i = 0; i < 3; i++)
		{
			cameraAngles[i] = AngleNormalize180(cameraAngles[i]);
			cameraAngles[i] = AngleNormalize180(
				client_camera.angles[i] + frac * AngleNormalize180(cameraAngles[i] - client_camera.angles[i]));
			cameraAngles[i] = AngleNormalize180(cameraAngles[i]);
		}
#if 0
		Com_Printf("%s\n", vtos(cameraAngles));
#endif
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

	//Point camera to lerp angles
	/*
	if ( !cg_skippingcin.integer )
	{
		Com_Printf( "ang: %s\n", vtos(cameraAngles) );
	}
	*/
	VectorCopy(cameraAngles, client_camera.angles);
}

static void CGCam_TrackEntUpdate(void)
{
	// SP's, on the points the server sent (CGCam_TrackParse, 2026-11-19: SP walks the path_corners)
	qboolean reached = qfalse;
	int next = -1;

	if (cam_trackIndex >= 0 && cam_trackIndex < cam_trackCount)
	{
		//We're already heading to a path_corner
		vec3_t vec;
		VectorSubtract(cam_trackOrg[cam_trackIndex], client_camera.origin, vec);
		if (VectorLengthSquared(vec) < 256) //16 squared
		{
			reached = qtrue;
		}
	}

	if (reached)
	{
		next = cam_trackIndex + 1;
		if (next >= cam_trackCount)
		{
			next = cam_trackLoop; // the last one leads back round, or the track ends
		}
		if (next < 0)
		{
			//stop thinking if this is the last one
			CGCam_TrackDisable();
			return;
		}

		if (cam_trackRadius[next] < 0)
		{
			//Don't bother trying to maintain a radius
			client_camera.distance = 0;
			client_camera.speed = client_camera.initSpeed;
		}
		else if (cam_trackRadius[next] > 0)
		{
			client_camera.distance = cam_trackRadius[next];
		}

		if (cam_trackSpeed[next] < 0)
		{
			//go back to our default speed
			client_camera.speed = client_camera.initSpeed;
		}
		else if (cam_trackSpeed[next] > 0)
		{
			client_camera.speed = cam_trackSpeed[next] / 10.0f;
		}

		//Update will lerp this
		client_camera.info_state |= CAMERA_TRACKING;
		cam_trackIndex = next;
		client_camera.trackEntNum = next;
		VectorCopy(cam_trackOrg[next], client_camera.trackToOrg);
	}

	client_camera.nextTrackEntUpdateTime = cg.time + 100;
}

static void CGCam_TrackUpdate(void)
{
	vec3_t goalVec, curVec, trackPos;

	if (client_camera.nextTrackEntUpdateTime <= cg.time)
	{
		CGCam_TrackEntUpdate();
	}

	VectorSubtract(client_camera.trackToOrg, client_camera.origin, goalVec);
	float goal_dist = VectorNormalize(goalVec);
	if (goal_dist > 100)
	{
		goal_dist = 100;
	}
	else if (goal_dist < 10)
	{
		goal_dist = 10;
	}

	if (client_camera.distance && client_camera.info_state & CAMERA_FOLLOWING)
	{
		vec3_t vec;
		if (!client_camera.distanceInitLerp)
		{
			VectorSubtract(client_camera.origin, client_camera.subjectPos, vec);
			VectorNormalize(vec);
			//FIXME: use client_camera.moveDir here?
			VectorMA(client_camera.subjectPos, client_camera.distance, vec, client_camera.origin);
			//Snap to first time only
			client_camera.distanceInitLerp = qtrue;
			return;
		}
		if (client_camera.subjectSpeed > 0.05f)
		{
			float adjust = 0.0f;
			VectorSubtract(client_camera.subjectPos, client_camera.origin, vec);
			const float dist = VectorNormalize(vec);
			const float dot = DotProduct(goalVec, vec);

			if (dist > client_camera.distance)
			{
				//too far away
				if (dot > 0)
				{
					//Camera is moving toward the subject
					adjust = dist - client_camera.distance; //Speed up
				}
				else if (dot < 0)
				{
					//Camera is moving away from the subject
					adjust = (dist - client_camera.distance) * -1.0f; //Slow down
				}
			}
			else if (dist < client_camera.distance)
			{
				//too close
				if (dot > 0)
				{
					//Camera is moving toward the subject
					adjust = (client_camera.distance - dist) * -1.0f; //Slow down
				}
				else if (dot < 0)
				{
					//Camera is moving away from the subject
					adjust = client_camera.distance - dist; //Speed up
				}
			}

			//Speed of the focus + our error
			//desiredSpeed = aimCent->gent->speed + (adjust * cg.frametime/100.0f);//cg.frameInterpolation);
			const float desiredSpeed = adjust; // * cg.frametime/100.0f);//cg.frameInterpolation);

			//self->moveInfo.speed = desiredSpeed;

			//Don't change speeds faster than 10 every 10th of a second
			const float max_allowed_accel = MAX_ACCEL_PER_FRAME * (cg.frametime / 100.0f);

			if (!client_camera.subjectSpeed)
			{
				//full stop
				client_camera.speed = desiredSpeed;
			}
			else if (client_camera.speed - desiredSpeed > max_allowed_accel)
			{
				//new speed much slower, slow down at max accel
				client_camera.speed -= max_allowed_accel;
			}
			else if (desiredSpeed - client_camera.speed > max_allowed_accel)
			{
				//new speed much faster, speed up at max accel
				client_camera.speed += max_allowed_accel;
			}
			else
			{
				//remember this speed
				client_camera.speed = desiredSpeed;
			}

			//Com_Printf("Speed: %4.2f (%4.2f)\n", self->moveInfo.speed, aimCent->gent->speed);
		}
	}
	else
	{
		//slowDown = qtrue;
	}

	//FIXME: this probably isn't right, round it out more
	VectorScale(goalVec, cg.frametime / 100.0f, goalVec);
	VectorScale(client_camera.moveDir, (100.0f - cg.frametime) / 100.0f, curVec);
	VectorAdd(goalVec, curVec, client_camera.moveDir);
	VectorNormalize(client_camera.moveDir);
	VectorMA(client_camera.origin, client_camera.speed * cg.frametime / 100.0f, client_camera.moveDir, trackPos);

	//FIXME: Implement
	//Need to find point on camera's path that is closest to the desired distance from subject
	//OR: Need to intelligently pick this desired distance based on framing...
	VectorCopy(trackPos, client_camera.origin);
}

//=========================================================================================

/*
-------------------------
CGCam_UpdateBarFade
-------------------------
*/

static void CGCam_UpdateBarFade(void)
{
	if (client_camera.bar_time + BAR_DURATION < cg.time)
	{
		client_camera.bar_alpha = client_camera.bar_alpha_dest;
		client_camera.info_state &= ~CAMERA_BAR_FADING;
		client_camera.bar_height = client_camera.bar_height_dest;
	}
	else
	{
		client_camera.bar_alpha = client_camera.bar_alpha_source + (client_camera.bar_alpha_dest - client_camera.
			bar_alpha_source) / BAR_DURATION * (cg.time - client_camera.bar_time);
		client_camera.bar_height = client_camera.bar_height_source + (client_camera.bar_height_dest - client_camera.
			bar_height_source) / BAR_DURATION * (cg.time - client_camera.bar_time);
	}
}

/*
-------------------------
CGCam_UpdateFade
-------------------------
*/

void CGCam_UpdateFade(void)
{
	if (client_camera.info_state & CAMERA_FADING)
	{
		if (client_camera.fade_time + client_camera.fade_duration < cg.time)
		{
			Vector6Copy(client_camera.fade_dest, client_camera.fade_color);
			client_camera.info_state &= ~CAMERA_FADING;
		}
		else
		{
			for (int i = 0; i < 4; i++)
			{
				client_camera.fade_color[i] = client_camera.fade_source[i] + (client_camera.fade_dest[i] -
					client_camera.fade_source[i]) / client_camera.fade_duration * (cg.time - client_camera.fade_time);
			}
		}
	}
}

/*
-------------------------
CGCam_Update
-------------------------
*/
static void CGCam_Roff(void);

void CGCam_Update(void)
{
	int i = 0;
	qboolean checkFollow = qfalse;
	qboolean checkTrack = qfalse;

	// Apply new roff data to the camera as needed
	if (client_camera.info_state & CAMERA_ROFFING)
	{
		CGCam_Roff();
	}

	//Check for a zoom
	if (client_camera.info_state & CAMERA_ACCEL)
	{
		// x = x0 + vt + 0.5*a*t*t
		float actualFOV_X = client_camera.FOV;
		const float t = (cg.time - client_camera.FOV_time) * 0.001; // mult by 0.001 cuz otherwise t is too darned big
		const float fovDuration = client_camera.FOV_duration;

		if (client_camera.FOV_time + fovDuration < cg.time)
		{
			client_camera.info_state &= ~CAMERA_ACCEL;
		}
		else
		{
			const float sanityMax = 180;
			const float sanityMin = 1;
			const float initialPosVal = client_camera.FOV2;
			const float velVal = client_camera.FOV_vel;
			const float accVal = client_camera.FOV_acc;
			const float initialPos = initialPosVal;
			const float vel = velVal * t;
			const float acc = 0.5 * accVal * t * t;

			actualFOV_X = initialPos + vel + acc;

			if (actualFOV_X < sanityMin)
			{
				actualFOV_X = sanityMin;
			}
			else if (actualFOV_X > sanityMax)
			{
				actualFOV_X = sanityMax;
			}
			client_camera.FOV = actualFOV_X;
		}
		CG_CalcFOVFromX(actualFOV_X);
	}
	else if (client_camera.info_state & CAMERA_ZOOMING)
	{
		float actualFOV_X = 0.0f;

		if (client_camera.FOV_time + client_camera.FOV_duration < cg.time)
		{
			actualFOV_X = client_camera.FOV = client_camera.FOV2;
			client_camera.info_state &= ~CAMERA_ZOOMING;
		}
		else
		{
			actualFOV_X = client_camera.FOV + (client_camera.FOV2 - client_camera.FOV) / client_camera.FOV_duration
				* (cg.time - client_camera.FOV_time);
		}
		CG_CalcFOVFromX(actualFOV_X);
	}
	else
	{
		CG_CalcFOVFromX(client_camera.FOV);
	}

	//Check for roffing angles
	if (client_camera.info_state & CAMERA_ROFFING && !(client_camera.info_state & CAMERA_FOLLOWING))
	{
		if (client_camera.info_state & CAMERA_CUT)
		{
			// we're doing a cut, so just go to the new angles. none of this hifalutin lerping business.
			for (i = 0; i < 3; i++)
			{
				cg.refdef.viewangles[i] = AngleNormalize360(client_camera.angles[i] + client_camera.angles2[i]);
			}
		}
		else
		{
			for (i = 0; i < 3; i++)
			{
				cg.refdef.viewangles[i] = client_camera.angles[i] + client_camera.angles2[i] / client_camera.
					pan_duration * (cg.time - client_camera.pan_time);
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
				cg.refdef.viewangles[i] = AngleNormalize360(client_camera.angles[i] + client_camera.angles2[i]);
			}
		}
		else
		{
			//Note: does not actually change the camera's angles until the pan time is done!
			if (client_camera.pan_time + client_camera.pan_duration < cg.time)
			{
				//finished panning
				for (i = 0; i < 3; i++)
				{
					client_camera.angles[i] = AngleNormalize360(client_camera.angles[i] + client_camera.angles2[i]);
				}

				client_camera.info_state &= ~CAMERA_PANNING;
				VectorCopy(client_camera.angles, cg.refdef.viewangles);
			}
			else
			{
				//still panning
				for (i = 0; i < 3; i++)
				{
					//NOTE: does not store the resultant angle in client_camera.angles until pan is done
					cg.refdef.viewangles[i] = client_camera.angles[i] + client_camera.angles2[i] / client_camera.
						pan_duration * (cg.time - client_camera.pan_time);
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
		if (client_camera.move_time + client_camera.move_duration < cg.time)
		{
			VectorCopy(client_camera.origin2, client_camera.origin);
			client_camera.info_state &= ~CAMERA_MOVING;
			VectorCopy(client_camera.origin, cg.refdef.vieworg);
		}
		else
		{
			if (client_camera.info_state & CAMERA_CUT)
			{
				// we're doing a cut, so just go to the new origin. none of this fancypants lerping stuff.
				for (i = 0; i < 3; i++)
				{
					cg.refdef.vieworg[i] = client_camera.origin2[i];
				}
			}
			else
			{
				for (i = 0; i < 3; i++)
				{
					cg.refdef.vieworg[i] = client_camera.origin[i] + (client_camera.origin2[i] - client_camera.origin[
						i]) / client_camera.move_duration * (cg.time - client_camera.move_time);
				}
			}
		}
	}
	else
	{
		checkTrack = qtrue;
	}

	if (checkFollow)
	{
		if (client_camera.info_state & CAMERA_FOLLOWING)
		{
			//This needs to be done after camera movement
			CGCam_FollowUpdate();
		}
		VectorCopy(client_camera.angles, cg.refdef.viewangles);
	}

	if (checkTrack)
	{
		if (client_camera.info_state & CAMERA_TRACKING)
		{
			//This has to run AFTER Follow if the camera is following a cameraGroup
			CGCam_TrackUpdate();
		}

		VectorCopy(client_camera.origin, cg.refdef.vieworg);
	}

	//Bar fading
	if (client_camera.info_state & CAMERA_BAR_FADING)
	{
		CGCam_UpdateBarFade();
	}

	//Normal fading - separate call because can finish after camera is disabled
	CGCam_UpdateFade();

	//Update shaking if there's any
	CGCam_UpdateShake(cg.refdef.vieworg, cg.refdef.viewangles);
	AnglesToAxis(cg.refdef.viewangles, cg.refdef.viewaxis);
}

/*
-------------------------
CGCam_DrawWideScreen
-------------------------
*/

void CGCam_DrawWideScreen(void)
{
	//Only draw if visible
	if (client_camera.bar_alpha)
	{
		CGCam_UpdateBarFade();

		if (cg_drawwidescreenmodemp.integer)
		{
			//
		}
		else
		{
			vec4_t modulate = { 0 };
			modulate[0] = modulate[1] = modulate[2] = 0.0f;
			modulate[3] = client_camera.bar_alpha;

			CG_FillRect(cg.refdef.x, cg.refdef.y, 640, client_camera.bar_height, modulate);
			CG_FillRect(cg.refdef.x, cg.refdef.y + 480 - client_camera.bar_height, 640, client_camera.bar_height,
				modulate);
		}
	}

	//NOTENOTE: Camera always draws the fades unless the alpha is 0
	if (client_camera.fade_color[3] == 0.0f)
		return;

	CG_FillRect(cg.refdef.x, cg.refdef.y, 640, 480, client_camera.fade_color);
}

/*
-------------------------
CGCam_RenderScene
-------------------------
*/
void CGCam_RenderScene(void)
{
	memset(&cg.refdef, 0, sizeof cg.refdef);
	CGCam_Update();
	CG_CalcVrect();
}

/*
-------------------------
CGCam_Shake
-------------------------
*/

//This replaces the MP version of the same function.  It's basically the same thing with a
//different struct used.
void CGCam_Shake(float intensity, int duration)
{
	if (intensity > MAX_SHAKE_INTENSITY)
		intensity = MAX_SHAKE_INTENSITY;

	client_camera.shake_intensity = intensity;
	client_camera.shake_duration = duration;
	client_camera.shake_start = cg.time;
#ifdef _IMMERSION
	// FIX ME: This is far too weak... but I don't want it to interfere with other effects.
	cgi_FF_Shake(int(intensity * 625), duration);	// 625 = (10000 / MAX_SHAKE_INTENSITY)
#endif // _IMMERSION
}

void CGCam_BlockShake(float intensity, int duration)
{
	if (intensity > MAX_BLOCKSHAKE_INTENSITY)
	{
		intensity = MAX_BLOCKSHAKE_INTENSITY;
	}

	client_camera.shake_intensity = intensity;
	client_camera.shake_duration = duration;
	client_camera.shake_start = cg.time;
#ifdef _IMMERSION
	cgi_FF_Shake(int(intensity * 625), duration);
#endif
}

/*
-------------------------
CGCam_UpdateShake

This doesn't actually affect the camera's info, but passed information instead
-------------------------
*/

void CGCam_UpdateShake(vec3_t origin, vec3_t angles)
{
	int i = 0;
	vec3_t moveDir = { 0.0f, 0.0f, 0.0f };

	if (client_camera.shake_duration <= 0)
		return;

	if (cg.time > client_camera.shake_start + client_camera.shake_duration)
	{
		client_camera.shake_intensity = 0;
		client_camera.shake_duration = 0;
		client_camera.shake_start = 0;
		return;
	}

	//intensity_scale now also takes into account FOV with 90.0 as normal
	const float intensity_scale = 1.0f - (float)(cg.time - client_camera.shake_start) / (float)client_camera.
		shake_duration
		* ((
			client_camera.FOV + client_camera.FOV2) / 2.0f / 90.0f);

	const float intensity = client_camera.shake_intensity * intensity_scale;

	for (i = 0; i < 3; i++)
	{
		moveDir[i] = crandoms() * intensity;
	}

	//FIXME: Lerp

	//Move the camera
	VectorAdd(origin, moveDir, origin);

	for (i = 0; i < 2; i++) // Don't do ROLL
		moveDir[i] = crandoms() * intensity;

	//FIXME: Lerp

	//Move the angles
	VectorAdd(angles, moveDir, angles);
}

void CGCam_Smooth(const float intensity, const int duration)
{
	client_camera.smooth_active = qfalse; // means smooth_origin and angles are valid
	if (intensity > 1.0f || intensity == 0.0f || duration < 1)
	{
		client_camera.info_state &= ~CAMERA_SMOOTHING;
		return;
	}
	client_camera.info_state |= CAMERA_SMOOTHING;
	client_camera.smooth_intensity = intensity;
	client_camera.smooth_duration = duration;
	client_camera.smooth_start = cg.time;
}

void CGCam_UpdateSmooth(vec3_t origin)
{
	if (!(client_camera.info_state & CAMERA_SMOOTHING) || cg.time > client_camera.smooth_start + client_camera.
		smooth_duration)
	{
		client_camera.info_state &= ~CAMERA_SMOOTHING;
		return;
	}
	if (!client_camera.smooth_active)
	{
		client_camera.smooth_active = qtrue;
		VectorCopy(origin, client_camera.smooth_origin);
		return;
	}
	float factor = client_camera.smooth_intensity;
	if (client_camera.smooth_duration > 200 && cg.time > client_camera.smooth_start + client_camera.smooth_duration -
		100)
	{
		factor += (1.0f - client_camera.smooth_intensity) *
			(100.0f - (client_camera.smooth_start + client_camera.smooth_duration - cg.time)) / 100.0f;
	}

	for (int i = 0; i < 3; i++)
	{
		client_camera.smooth_origin[i] *= 1.0f - factor;
		client_camera.smooth_origin[i] += factor * origin[i];
		origin[i] = client_camera.smooth_origin[i];
	}
}

static void CGCam_NotetrackProcessFov(const char* addlArg)
{
	int a = 0;

	if (!addlArg || !addlArg[0])
	{
		Com_Printf("camera roff 'fov' notetrack missing fov argument\n", addlArg);
		return;
	}
	if (isdigit(addlArg[a]))
	{
		char t[64];
		// "fov <new fov>"
		int d = 0;
		const int tsize = 64;

		memset(t, 0, tsize * sizeof(char));
		while (addlArg[a] && d < tsize)
		{
			t[d++] = addlArg[a++];
		}
		// now the contents of t represent our desired fov
		const float newFov = atof(t);
		/* RAFIXME - more debugger cvar stuff
		#ifndef FINAL_BUILD
				if (cg_roffdebug.integer)
				{
					if (fabs(cg_roffval1.value) > 0.001f)
					{
						newFov = cg_roffval1.value;
					}
				}
		#endif
				if (cg_roffdebug.integer)
				{
					Com_Printf("notetrack: 'fov %2.2f' on frame %d\n", newFov, client_camera.roff_frame);
				}
		*/
		CGCam_Zoom(newFov, 0);
	}
}

static void CGCam_NotetrackProcessFovZoom(const char* addlArg)
{
	int a = 0;
	float begin_fov = 0.0f;
	char t[64];
	int d = 0;
	const int tsize = 64;

	if (!addlArg || !addlArg[0])
	{
		Com_Printf("camera roff 'fovzoom' notetrack missing arguments\n", addlArg);
		return;
	}
	//
	// "fovzoom <begin fov> <end fov> <time>"
	//

	memset(t, 0, tsize * sizeof(char));
	while (addlArg[a] && !isspace(addlArg[a]) && d < tsize)
	{
		t[d++] = addlArg[a++];
	}
	if (!isdigit(t[0]))
	{
		// assume a non-number here means we should start from our current fov
		begin_fov = client_camera.FOV;
	}
	else
	{
		// now the contents of t represent our beginning fov
		begin_fov = atof(t);
	}

	// eat leading whitespace
	while (addlArg[a] && addlArg[a] == ' ')
	{
		a++;
	}
	if (addlArg[a])
	{
		float fov_time = 0.0f;
		d = 0;
		memset(t, 0, tsize * sizeof(char));
		while (addlArg[a] && !isspace(addlArg[a]) && d < tsize)
		{
			t[d++] = addlArg[a++];
		}
		// now the contents of t represent our end fov
		const float endFOV = atof(t);

		// eat leading whitespace
		while (addlArg[a] && addlArg[a] == ' ')
		{
			a++;
		}
		if (addlArg[a])
		{
			d = 0;
			memset(t, 0, tsize * sizeof(char));
			while (addlArg[a] && !isspace(addlArg[a]) && d < tsize)
			{
				t[d++] = addlArg[a++];
			}
			// now the contents of t represent our time
			fov_time = atof(t);
		}
		else
		{
			Com_Printf("camera roff 'fovzoom' notetrack missing 'time' argument\n", addlArg);
			return;
		}
		/* RAFIXME - more debugger cvar stuff
		#ifndef FINAL_BUILD
				if (cg_roffdebug.integer)
				{
					if (fabs(cg_roffval1.value) > 0.001f)
					{
						beginFOV = cg_roffval1.value;
					}
					if (fabs(cg_roffval2.value) > 0.001f)
					{
						endFOV = cg_roffval2.value;
					}
					if (fabs(cg_roffval3.value) > 0.001f)
					{
						fovTime = cg_roffval3.value;
					}
				}
		#endif
				if (cg_roffdebug.integer)
				{
					Com_Printf("notetrack: 'fovzoom %2.2f %2.2f %5.1f' on frame %d\n", beginFOV, endFOV, fovTime, client_camera.roff_frame);
				}
		*/
		CGCam_Zoom2(begin_fov, endFOV, fov_time);
	}
	else
	{
		Com_Printf("camera roff 'fovzoom' notetrack missing 'end fov' argument\n", addlArg);
	}
}

static void CGCam_NotetrackProcessFovAccel(const char* addlArg)
{
	char t[64];
	int a = 0;
	float beginFOV = 0.0f;
	int d = 0;
	const int tsize = 64;

	if (!addlArg || !addlArg[0])
	{
		Com_Printf("camera roff 'fovaccel' notetrack missing arguments\n", addlArg);
		return;
	}
	//
	// "fovaccel <begin fov> <fov delta> <fov delta2> <time>"
	//
	// where 'begin fov' is initial position, 'fov delta' is velocity, and 'fov delta2' is acceleration.

	memset(t, 0, tsize * sizeof(char));
	while (addlArg[a] && !isspace(addlArg[a]) && d < tsize)
	{
		t[d++] = addlArg[a++];
	}
	if (!isdigit(t[0]))
	{
		// assume a non-number here means we should start from our current fov
		beginFOV = client_camera.FOV;
	}
	else
	{
		// now the contents of t represent our beginning fov
		beginFOV = atof(t);
	}

	// eat leading whitespace
	while (addlArg[a] && addlArg[a] == ' ')
	{
		a++;
	}
	if (addlArg[a])
	{
		d = 0;
		memset(t, 0, tsize * sizeof(char));
		while (addlArg[a] && !isspace(addlArg[a]) && d < tsize)
		{
			t[d++] = addlArg[a++];
		}
		// now the contents of t represent our delta
		const float fov_delta = atof(t);

		// eat leading whitespace
		while (addlArg[a] && addlArg[a] == ' ')
		{
			a++;
		}
		if (addlArg[a])
		{
			float fov_time = 0.0f;
			d = 0;
			memset(t, 0, tsize * sizeof(char));
			while (addlArg[a] && !isspace(addlArg[a]) && d < tsize)
			{
				t[d++] = addlArg[a++];
			}
			// now the contents of t represent our fovDelta2
			const float fov_delta2 = atof(t);

			// eat leading whitespace
			while (addlArg[a] && addlArg[a] == ' ')
			{
				a++;
			}
			if (addlArg[a])
			{
				d = 0;
				memset(t, 0, tsize * sizeof(char));
				while (addlArg[a] && !isspace(addlArg[a]) && d < tsize)
				{
					t[d++] = addlArg[a++];
				}
				// now the contents of t represent our time
				fov_time = atof(t);
			}
			else
			{
				Com_Printf("camera roff 'fovaccel' notetrack missing 'time' argument\n", addlArg);
				return;
			}
			/* RAFIXME - debugger cvar
			if (cg_roffdebug.integer)
			{
				Com_Printf("notetrack: 'fovaccel %2.2f %3.5f %3.5f %d' on frame %d\n", beginFOV, fovDelta, fovDelta2, fovTime, client_camera.roff_frame);
			}
			*/
			CGCam_ZoomAccel(beginFOV, fov_delta, fov_delta2, fov_time);
		}
		else
		{
			Com_Printf("camera roff 'fovaccel' notetrack missing 'delta2' argument\n", addlArg);
		}
	}
	else
	{
		Com_Printf("camera roff 'fovaccel' notetrack missing 'delta' argument\n", addlArg);
	}
}

// 3/18/03 kef -- blatantly thieved from G_RoffNotetrackCallback
static void CG_RoffNotetrackCallback(const char* notetrack)
{
	int i = 0;
	char type[256] = { 0 };
	char addlArg[512] = { 0 };
	int addlArgs = 0;

	if (!notetrack)
	{
		return;
	}

	//notetrack = "fov 65";

	while (notetrack[i] && notetrack[i] != ' ')
	{
		type[i] = notetrack[i];
		i++;
	}

	type[i] = '\0';

	//if (notetrack[i] != ' ')
	//{ //didn't pass in a valid notetrack type, or forgot the argument for it
	//	return;
	//}

	/*	i++;

		while (notetrack[i] && notetrack[i] != ' ')
		{
			if (notetrack[i] != '\n' && notetrack[i] != '\r')
			{ //don't read line ends for an argument
				argument[r] = notetrack[i];
				r++;
			}
			i++;
		}
		argument[r] = '\0';
		if (!r)
		{
			return;
		}
	*/

	if (notetrack[i] == ' ')
	{
		//additional arguments...
		addlArgs = 1;

		i++;
		int r = 0;
		while (notetrack[i])
		{
			addlArg[r] = notetrack[i];
			r++;
			i++;
		}
		addlArg[r] = '\0';
	}

	if (strcmp(type, "cut") == 0)
	{
		client_camera.info_state |= CAMERA_CUT;

		// this is just a really hacky way of getting a cut and a fov command on the same frame
		if (addlArgs)
		{
			CG_RoffNotetrackCallback(addlArg);
		}
	}
	else if (strcmp(type, "fov") == 0)
	{
		if (addlArgs)
		{
			CGCam_NotetrackProcessFov(addlArg);
			return;
		}
		Com_Printf("camera roff 'fov' notetrack missing fov argument\n", addlArg);
	}
	else if (strcmp(type, "fovzoom") == 0)
	{
		if (addlArgs)
		{
			CGCam_NotetrackProcessFovZoom(addlArg);
			return;
		}
		Com_Printf("camera roff 'fovzoom' notetrack missing 'begin fov' argument\n", addlArg);
	}
	else if (strcmp(type, "fovaccel") == 0)
	{
		if (addlArgs)
		{
			CGCam_NotetrackProcessFovAccel(addlArg);
			return;
		}
		Com_Printf("camera roff 'fovaccel' notetrack missing 'begin fov' argument\n", addlArg);
	}
}

/*
-------------------------
Camera path: a ROFF the camera plays (2026-11-19)

SP's camera( PATH, "<roff>" ). The cgame reads the .rof itself (scriptsmp/<roff>.rof, as the game's G_LoadRoffs),
with its note tracks (cut, fov, fovzoom, fovaccel: CG_RoffNotetrackCallback).
-------------------------
*/
#define CAM_ROFFS			4
#define CAM_ROFF_FRAMES		1000
#define CAM_ROFF_NOTES		64
#define CAM_ROFF_NOTE_LEN	128
#define CAM_ROFF_FILE_SIZE	131072

typedef struct
{
	char name[MAX_QPATH];
	int type; // 1 old, 2 new (note tracks)
	int frames;
	int frameTime;
	vec3_t org[CAM_ROFF_FRAMES];
	vec3_t ang[CAM_ROFF_FRAMES];
	int startNote[CAM_ROFF_FRAMES];
	int numNotes;
	char notes[CAM_ROFF_NOTES][CAM_ROFF_NOTE_LEN];
} camRoff_t;

static camRoff_t cam_roffs[CAM_ROFFS];
static int cam_numRoffs = 0;
static int cam_roffNum = -1; // the one playing

typedef struct
{
	char mHeader[4];
	int mVersion;
	float mCount;
} camRoffHdr1_t;

typedef struct
{
	char mHeader[4];
	int mVersion;
	int mCount;
	int mFrameRate;
	int mNumNotes;
} camRoffHdr2_t;

typedef struct
{
	vec3_t origin_delta;
	vec3_t rotate_delta;
} camRoffData1_t;

typedef struct
{
	vec3_t origin_delta;
	vec3_t rotate_delta;
	int mStartNote, mNumNotes;
} camRoffData2_t;

static int CGCam_LoadRoff(const char* roffName)
{
	static char data[CAM_ROFF_FILE_SIZE];
	char file[MAX_QPATH];
	fileHandle_t f = 0;
	camRoff_t* roff;
	int i;

	Com_sprintf(file, sizeof file, "%s/%s.rof", Q3_SCRIPT_DIR, roffName);

	for (i = 0; i < cam_numRoffs; i++)
	{
		if (!Q_stricmp(cam_roffs[i].name, file))
		{
			return i;
		}
	}

	const int len = trap->FS_Open(file, &f, FS_READ);
	if (len <= 0 || !f)
	{
		Com_Printf(S_COLOR_RED "Could not open camera .ROF file '%s'\n", file);
		return -1;
	}
	if (len >= CAM_ROFF_FILE_SIZE || len < (int)sizeof(camRoffHdr1_t))
	{
		Com_Printf(S_COLOR_RED "Camera .ROF file '%s': bad size\n", file);
		trap->FS_Close(f);
		return -1;
	}
	trap->FS_Read(data, len, f);
	trap->FS_Close(f);

	// the slot: a new one, or the oldest goes (only one plays at a time)
	if (cam_numRoffs < CAM_ROFFS)
	{
		i = cam_numRoffs++;
	}
	else
	{
		i = cam_roffNum == 0 ? 1 : 0;
	}
	roff = &cam_roffs[i];
	memset(roff, 0, sizeof * roff);

	const camRoffHdr1_t* h1 = (const camRoffHdr1_t*)data;
	if (strncmp(h1->mHeader, "ROFF", 4))
	{
		Com_Printf(S_COLOR_RED "Invalid camera roff format '%s'\n", file);
		roff->name[0] = '\0';
		return -1;
	}

	if (h1->mVersion == 1 && h1->mCount > 0.0f)
	{
		const camRoffData1_t* d = (const camRoffData1_t*)&h1[1];
		int count = (int)h1->mCount;

		if ((int)sizeof * h1 + count * (int)sizeof * d > len)
		{
			count = (len - (int)sizeof * h1) / (int)sizeof * d;
		}
		if (count > CAM_ROFF_FRAMES)
		{
			count = CAM_ROFF_FRAMES;
		}
		roff->type = 1;
		roff->frameTime = 100; // old ones have a set frame time
		roff->frames = count;
		for (int k = 0; k < count; k++)
		{
			VectorCopy(d[k].origin_delta, roff->org[k]);
			VectorCopy(d[k].rotate_delta, roff->ang[k]);
			roff->startNote[k] = -1;
		}
	}
	else if (h1->mVersion == 2 && ((const camRoffHdr2_t*)data)->mCount > 0)
	{
		const camRoffHdr2_t* h2 = (const camRoffHdr2_t*)data;
		const camRoffData2_t* d = (const camRoffData2_t*)&h2[1];
		int count = h2->mCount;
		int stored = count;

		if ((int)sizeof * h2 + count * (int)sizeof * d > len)
		{
			count = stored = (len - (int)sizeof * h2) / (int)sizeof * d;
		}
		if (stored > CAM_ROFF_FRAMES)
		{
			stored = CAM_ROFF_FRAMES;
		}
		roff->type = 2;
		roff->frameTime = h2->mFrameRate >= 50 ? h2->mFrameRate : 50;
		roff->frames = stored;
		for (int k = 0; k < stored; k++)
		{
			VectorCopy(d[k].origin_delta, roff->org[k]);
			VectorCopy(d[k].rotate_delta, roff->ang[k]);
			roff->startNote[k] = d[k].mNumNotes > 0 ? d[k].mStartNote : -1;
		}

		// the notes follow the frames, each ends with a 0
		const char* n = (const char*)&d[count];
		const char* end = data + len;
		for (int k = 0; k < h2->mNumNotes && k < CAM_ROFF_NOTES && n < end; k++)
		{
			Q_strncpyz(roff->notes[k], n, CAM_ROFF_NOTE_LEN);
			roff->numNotes++;
			while (n < end && *n)
			{
				n++;
			}
			n++;
		}
	}
	else
	{
		Com_Printf(S_COLOR_RED "Invalid camera roff format '%s'\n", file);
		return -1;
	}

	Q_strncpyz(roff->name, file, sizeof roff->name);
	return i;
}

static void CGCam_StopRoff(void)
{
	client_camera.info_state &= ~CAMERA_ROFFING;
	client_camera.info_state &= ~CAMERA_MOVING;
	cam_roffNum = -1;
}

/*
-------------------------
CGCam_StartRoff

Sets up the camera to use
a rof file
-------------------------
*/

void CGCam_StartRoff(const char* roff)
{
	CGCam_FollowDisable();
	CGCam_TrackDisable();

	const int num = CGCam_LoadRoff(roff);
	if (num < 0)
	{
		// The load failed so don't turn on the roff playback...
		Com_Printf(S_COLOR_RED "ROFF camera playback failed\n");
		return;
	}

	// Set up the roff state info..we'll hijack the moving and panning code until told otherwise
	client_camera.info_state |= CAMERA_MOVING;
	client_camera.info_state |= CAMERA_PANNING;
	client_camera.info_state |= CAMERA_ROFFING;

	cam_roffNum = num;
	Q_strncpyz(client_camera.sRoff, roff, sizeof client_camera.sRoff);
	client_camera.roff_frame = 0;
	client_camera.next_roff_time = cg.time; // I can work right away
}

/*
------------------------------------------------------
CGCam_Roff

Applies the sampled roff data to the camera and does
the lerping itself (SP's)
------------------------------------------------------
*/

static void CGCam_Roff(void)
{
	while (client_camera.next_roff_time <= cg.time)
	{
		if (cam_roffNum < 0 || cam_roffNum >= cam_numRoffs)
		{
			CGCam_StopRoff();
			return;
		}

		const camRoff_t* roff = &cam_roffs[cam_roffNum];
		const int frame = client_camera.roff_frame;
		vec3_t org, ang;

		if (frame >= roff->frames)
		{
			CGCam_StopRoff();
			return;
		}

		VectorCopy(roff->org[frame], org);
		VectorCopy(roff->ang[frame], ang);

		if (roff->type == 2)
		{
			// since we just hit a new frame, clear our CUT flag
			client_camera.info_state &= ~CAMERA_CUT;

			if (roff->startNote[frame] >= 0 && roff->startNote[frame] < roff->numNotes)
			{
				CG_RoffNotetrackCallback(roff->notes[roff->startNote[frame]]);
			}
		}

		// Yeah, um, I guess this just has to be negated?
		ang[ROLL] = -ang[ROLL];

		if (frame)
		{
			// Don't mess with angles if we are following
			if (!(client_camera.info_state & CAMERA_FOLLOWING))
			{
				VectorAdd(client_camera.angles, client_camera.angles2, client_camera.angles);
			}

			VectorCopy(client_camera.origin2, client_camera.origin);
		}

		// Don't mess with angles if we are following
		if (!(client_camera.info_state & CAMERA_FOLLOWING))
		{
			VectorCopy(ang, client_camera.angles2);
			client_camera.pan_time = cg.time;
			client_camera.pan_duration = roff->frameTime;
		}

		VectorAdd(client_camera.origin, org, client_camera.origin2);

		client_camera.move_time = cg.time;
		client_camera.move_duration = roff->frameTime;

		if (++client_camera.roff_frame >= roff->frames)
		{
			CGCam_StopRoff();
			return;
		}

		// Check back in frameTime to get the next roff entry
		client_camera.next_roff_time += roff->frameTime;
	}
}

void CMD_CGCam_Disable(void)
{
	vec4_t fade = { 0, 0, 0, 0 };

	CGCam_Disable();
	CGCam_SetFade(fade);
}

void CG_CameraParse(void)
{
	int   int_data = 0;
	vec3_t vector_data = { 0 };
	float float_data = 0.0f, float2_data = 0.0f;

	const char* o = CG_ConfigString(CS_CAMERA);
	if (!o || !o[0])
	{
		Com_Printf("CG_CameraParse WARNING: empty CS_CAMERA string\n");
		return;
	}

	if (strncmp("enable", o, 6) == 0)
	{
		CGCam_Enable();
	}
	else if (strncmp("move", o, 4) == 0)
	{
		if (sscanf(o, "%*s %f %f %f %f",
			&vector_data[0], &vector_data[1], &vector_data[2], &float_data) == 4)
		{
			CGCam_Move(vector_data, float_data);
		}
		else
		{
			Com_Printf("CG_CameraParse WARNING: bad 'move' format\n");
		}
	}
	else if (strncmp("pan", o, 3) == 0)
	{
		vec3_t vector2_data = { 0 };
		if (sscanf(o, "%*s %f %f %f %f %f %f %f",
			&vector_data[0], &vector_data[1], &vector_data[2],
			&vector2_data[0], &vector2_data[1], &vector2_data[2],
			&float_data) == 7)
		{
			CGCam_Pan(vector_data, vector2_data, float_data);
		}
		else
		{
			Com_Printf("CG_CameraParse WARNING: bad 'pan' format\n");
		}
	}
	else if (strncmp("fade", o, 4) == 0)
	{
		vec4_t color = { 0 }, color2 = { 0 };
		if (sscanf(o, "%*s %f %f %f %f %f %f %f %f %f",
			&color[0], &color[1], &color[2], &color[3],
			&color2[0], &color2[1], &color2[2], &color2[3],
			&float_data) == 9)
		{
			CGCam_Fade(color, color2, float_data);
		}
		else
		{
			Com_Printf("CG_CameraParse WARNING: bad 'fade' format\n");
		}
	}
	else if (strncmp("zoom", o, 4) == 0)
	{
		if (sscanf(o, "%*s %f %f", &float_data, &float2_data) == 2)
		{
			CGCam_Zoom(float_data, float2_data);
		}
		else
		{
			Com_Printf("CG_CameraParse WARNING: bad 'zoom' format\n");
		}
	}
	else if (strncmp("disable", o, 7) == 0)
	{
		CGCam_Disable();
	}
	else if (strncmp("shake", o, 5) == 0)
	{
		if (sscanf(o, "%*s %f %i", &float_data, &int_data) == 2)
		{
			CGCam_Shake(float_data, int_data);
		}
		else
		{
			Com_Printf("CG_CameraParse WARNING: bad 'shake' format\n");
		}
	}
	else if (strncmp("follow", o, 6) == 0)
	{
		int CGroup[16] = { 0 };
		if (sscanf(o,
			"%*s %i %i %i %i %i %i %i %i %i %i %i %i %i %i %i %i %f %f",
			&CGroup[0], &CGroup[1], &CGroup[2], &CGroup[3],
			&CGroup[4], &CGroup[5], &CGroup[6], &CGroup[7],
			&CGroup[8], &CGroup[9], &CGroup[10], &CGroup[11],
			&CGroup[12], &CGroup[13], &CGroup[14], &CGroup[15],
			&float_data, &float2_data) == 18)
		{
			CGCam_Follow(CGroup, float_data, float2_data);
		}
		else
		{
			Com_Printf("CG_CameraParse WARNING: bad 'follow' format\n");
		}
	}
	else if (strncmp("roll", o, 4) == 0)
	{
		// (2026-11-19: camera( ROLL ), camera( TRACK ), camera( DISTANCE ), camera( PATH ), ICam_Roll...)
		if (sscanf(o, "%*s %f %f", &float_data, &float2_data) == 2)
		{
			CGCam_Roll(float_data, float2_data);
		}
	}
	else if (strncmp("track", o, 5) == 0)
	{
		CGCam_TrackParse(o);
	}
	else if (strncmp("distance", o, 8) == 0)
	{
		if (sscanf(o, "%*s %f %f", &float_data, &float2_data) == 2)
		{
			CGCam_Distance(float_data, float2_data);
		}
	}
	else if (strncmp("roff ", o, 5) == 0)
	{
		CGCam_StartRoff(o + 5);
	}
	else
	{
		Com_Printf("Bad CS_CAMERA configstring in CG_CameraParse().\n");
	}
}

//actually do camera fade
void CGCam_DoFade(void)
{
	if (client_camera.fade_color[0] != 0 || client_camera.fade_color[1] != 0
		|| client_camera.fade_color[2] != 0 || client_camera.fade_color[3] != 0)
	{
		CG_DrawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_WIDTH * SCREEN_HEIGHT, client_camera.fade_color);
	}
}
/*
-------------------------
The server-side cutscene camera (2026-11-16, DH)

In MP the camera runs on the server (g_camera.c): every player is moved onto it, made invisible and frozen, and
CS_CAMERA carries the camera commands ("enable", "move", "pan", ... "disable"). The cgame's own camera (in_camera,
CGCam_*) is not used, so this tells the cgame a cutscene is on: no HUD, no selection, no prediction.
-------------------------
*/
static int cg_serverCameraStart;

qboolean CG_InServerCamera(void)
{
	static qboolean was_on;
	const char* o = CG_ConfigString(CS_CAMERA);
	// (and the server holds the player on it: invisible, g_camera.c EnablePlayerCameraPos)
	const qboolean on = o && o[0] && Q_strncmp(o, "disable", 7) && cg.snap && cg.snap->ps.eFlags & EF_NODRAW
		? qtrue : qfalse;

	if (on && !was_on)
	{
		cg_serverCameraStart = cg.time;
	}
	was_on = on;
	return on;
}

// the letterbox bars over a cutscene, sliding in (as SP; cg_drawwidescreenmodemp 1 = none)
void CG_DrawServerCameraBars(void)
{
	float f, h;
	vec4_t black = { 0.0f, 0.0f, 0.0f, 1.0f };

	if (cg_drawwidescreenmodemp.integer)
	{
		return;
	}
	f = (float)(cg.time - cg_serverCameraStart) / 1000.0f;
	if (f < 0.0f)
	{
		f = 0.0f;
	}
	if (f > 1.0f)
	{
		f = 1.0f;
	}
	h = 48.0f * f;
	black[3] = f;
	CG_FillRect(0, 0, 640, h, black);
	CG_FillRect(0, 480 - h, 640, h, black);
	trap->R_SetColor(NULL);
}

// the weapon / force / inventory selection and the wheel do nothing during a cutscene (their releases still work)
qboolean CG_ServerCameraBlocksCommand(const char* cmd)
{
	static const char* blocked[] = { "md_wheel_open", "forcenext", "forceprev", "invnext", "invprev", "weapnext",
		"weapprev", "weapon", "weaponclean" };
	size_t i;

	if (!CG_InServerCamera())
	{
		return qfalse;
	}
	if (cmd[0] == '+' && (Q_stristr(cmd, "wheel") || !Q_stricmpn(cmd, "+force_", 7)))
	{
		return qtrue;
	}
	for (i = 0; i < ARRAY_LEN(blocked); i++)
	{
		if (!Q_stricmp(cmd, blocked[i]))
		{
			return qtrue;
		}
	}
	return qfalse;
}
