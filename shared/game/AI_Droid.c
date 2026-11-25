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

#ifndef SP_GAME // file-level declarations of the MP versions below

#endif

//static void R5D2_LookAround( void );
float NPC_GetPainChance(const gentity_t* self, int damage);

#define TURN_OFF 0x00000100

//Local state enums
enum
{
	LSTATE_NONE = 0,
	LSTATE_BACKINGUP,
	LSTATE_SPINNING,
	LSTATE_PAIN,
	LSTATE_DROP
};

/*
-------------------------
R2D2_PartsMove
-------------------------
*/
#ifdef SP_GAME
static void R2D2_PartsMove(void)
{
	// Front 'eye' lense
	if (TIMER_Done(NPCS.NPC, "eyeDelay"))
	{
		NPCS.NPC->pos1[1] = AngleNormalize360(NPCS.NPC->pos1[1]);

		NPCS.NPC->pos1[0] += Q_irand(-20, 20); // Roll
		NPCS.NPC->pos1[1] = Q_irand(-20, 20);
		NPCS.NPC->pos1[2] = Q_irand(-20, 20);

		if (NPCS.NPC->genericBone1)
		{
			gi.G2API_SetBoneAnglesIndex(&NPCS.NPC->ghoul2[NPCS.NPC->playerModel], NPCS.NPC->genericBone1, NPCS.NPC->pos1,
				BONE_ANGLES_POSTMULT, POSITIVE_X, NEGATIVE_Y, NEGATIVE_Z, NULL, 0, 0);
		}
		TIMER_Set(NPCS.NPC, "eyeDelay", Q_irand(100, 1000));
	}
}
#else // MP keeps its own version of this function
static void R2D2_PartsMove(void)
{
	// Front 'eye' lense
	if (TIMER_Done(NPCS.NPC, "eyeDelay"))
	{
		NPCS.NPC->pos1[1] = AngleNormalize360(NPCS.NPC->pos1[1]);

		NPCS.NPC->pos1[0] += Q_irand(-20, 20); // Roll
		NPCS.NPC->pos1[1] = Q_irand(-20, 20);
		NPCS.NPC->pos1[2] = Q_irand(-20, 20);

		/*
		if (NPC->genericBone1)
		{
			trap->G2API_SetBoneAnglesIndex( &NPC->ghoul2[NPC->playerModel], NPC->genericBone1, NPC->pos1, BONE_ANGLES_POSTMULT, POSITIVE_X, NEGATIVE_Y, NEGATIVE_Z, NULL );
		}
		*/
		NPC_SetBoneAngles(NPCS.NPC, "f_eye", NPCS.NPC->pos1);

		TIMER_Set(NPCS.NPC, "eyeDelay", Q_irand(100, 1000));
	}
}
#endif

/*
-------------------------
NPC_BSDroid_Idle
-------------------------
*/
static void Droid_Idle(void)
{
	//	VectorCopy( NPCInfo->investigateGoal, lookPos );

	//	NPC_FacePosition( lookPos , qtrue);
}

/*
-------------------------
R2D2_TurnAnims
-------------------------
*/
static void R2D2_TurnAnims(void)
{
	const float turndelta = AngleDelta(NPCS.NPC->SHR_currentAngles[YAW], NPCS.NPCInfo->desiredYaw);

	if (fabs(turndelta) > 20 && (NPCS.NPC->client->NPC_class == CLASS_R2D2 || NPCS.NPC->client->NPC_class == CLASS_R5D2))
	{
		const int anim = NPCS.NPC->client->ps.legsAnim;
		if (turndelta < 0)
		{
			if (anim != BOTH_TURN_LEFT1)
			{
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_TURN_LEFT1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			}
		}
		else
		{
			if (anim != BOTH_TURN_RIGHT1)
			{
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_TURN_RIGHT1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			}
		}
	}
	else
	{
		NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_RUN1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	}
}

/*
-------------------------
Droid_Patrol
-------------------------
*/
static void Droid_Patrol(void)
{
	NPCS.NPC->pos1[1] = AngleNormalize360(NPCS.NPC->pos1[1]);

	if (NPCS.NPC->client && NPCS.NPC->client->NPC_class != CLASS_GONK)
	{
		R2D2_PartsMove(); // Get his eye moving.
		R2D2_TurnAnims();
	}

	//If we have somewhere to go, then do that
	if (UpdateGoal())
	{
		NPCS.ucmd.buttons |= BUTTON_WALKING;
		NPC_MoveToGoal(qtrue);

		if (NPCS.NPC->client && NPCS.NPC->client->NPC_class == CLASS_MOUSE)
		{
			NPCS.NPCInfo->desiredYaw += sin(level.time * .5) * 25; // Weaves side to side a little

			if (TIMER_Done(NPCS.NPC, "patrolNoise"))
			{
				SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, va("sound/chars/mouse/misc/mousego%d.wav", Q_irand(1, 3)));

				TIMER_Set(NPCS.NPC, "patrolNoise", Q_irand(2000, 4000));
			}
		}
		else if (NPCS.NPC->client && NPCS.NPC->client->NPC_class == CLASS_R2D2)
		{
			if (TIMER_Done(NPCS.NPC, "patrolNoise"))
			{
				SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, va("sound/chars/r2d2/misc/r2d2talk0%d.wav", Q_irand(1, 3)));

				TIMER_Set(NPCS.NPC, "patrolNoise", Q_irand(2000, 4000));
			}
		}
		else if (NPCS.NPC->client && NPCS.NPC->client->NPC_class == CLASS_R5D2)
		{
			if (TIMER_Done(NPCS.NPC, "patrolNoise"))
			{
				SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, va("sound/chars/r5d2/misc/r5talk%d.wav", Q_irand(1, 4)));

				TIMER_Set(NPCS.NPC, "patrolNoise", Q_irand(2000, 4000));
			}
		}
		if (NPCS.NPC->client && NPCS.NPC->client->NPC_class == CLASS_GONK)
		{
			if (TIMER_Done(NPCS.NPC, "patrolNoise"))
			{
				SHR_SoundOnEnt(NPCS.NPC, CHAN_AUTO, va("sound/chars/gonk/misc/gonktalk%d.wav", Q_irand(1, 2)));

				TIMER_Set(NPCS.NPC, "patrolNoise", Q_irand(2000, 4000));
			}
		}
		//		else
		//		{
		//			R5D2_LookAround();
		//		}
	}

	NPC_UpdateAngles(qtrue, qtrue);
}

/*
-------------------------
Droid_Run
-------------------------
*/
static void Droid_Run(void)
{
	R2D2_PartsMove();

	if (NPCS.NPCInfo->localState == LSTATE_BACKINGUP)
	{
		NPCS.ucmd.forwardmove = -127;
		NPCS.NPCInfo->desiredYaw += 5;

		NPCS.NPCInfo->localState = LSTATE_NONE; // So he doesn't constantly backup.
	}
	else
	{
		NPCS.ucmd.forwardmove = 64;
		//If we have somewhere to go, then do that
		if (UpdateGoal())
		{
			if (NPC_MoveToGoal(qfalse))
			{
				NPCS.NPCInfo->desiredYaw += sin(level.time * .5) * 5; // Weaves side to side a little
			}
		}
	}

	NPC_UpdateAngles(qtrue, qtrue);
}

/*
-------------------------
void Droid_Spin( void )
-------------------------
*/
static void Droid_Spin(void)
{
	const vec3_t dir = { 0, 0, 1 };

	R2D2_TurnAnims();

	// Head is gone, spin and spark
	if (NPCS.NPC->client->NPC_class == CLASS_R5D2)
	{
		// No head?
		if (SHR_GetSurfaceRenderStatus(NPCS.NPC, "head"))
		{
			if (TIMER_Done(NPCS.NPC, "smoke") && !TIMER_Done(NPCS.NPC, "droidsmoketotal"))
			{
				TIMER_Set(NPCS.NPC, "smoke", 100);
				SHR_PlayEffect("volumetric/droid_smoke", NPCS.NPC->SHR_currentOrigin, dir);
			}

			if (TIMER_Done(NPCS.NPC, "droidspark"))
			{
				TIMER_Set(NPCS.NPC, "droidspark", Q_irand(100, 500));
				SHR_PlayEffect("sparks/spark", NPCS.NPC->SHR_currentOrigin, dir);
			}

			NPCS.ucmd.forwardmove = Q_irand(-64, 64);

			if (TIMER_Done(NPCS.NPC, "roam"))
			{
				TIMER_Set(NPCS.NPC, "roam", Q_irand(250, 1000));
				NPCS.NPCInfo->desiredYaw = Q_irand(0, 360); // Go in random directions
			}
		}
		else
		{
			if (TIMER_Done(NPCS.NPC, "roam"))
			{
				NPCS.NPCInfo->localState = LSTATE_NONE;
			}
			else
			{
				NPCS.NPCInfo->desiredYaw = AngleNormalize360(NPCS.NPCInfo->desiredYaw + 40); // Spin around
			}
		}
	}
	else
	{
		if (TIMER_Done(NPCS.NPC, "roam"))
		{
			NPCS.NPCInfo->localState = LSTATE_NONE;
		}
		else
		{
			NPCS.NPCInfo->desiredYaw = AngleNormalize360(NPCS.NPCInfo->desiredYaw + 40); // Spin around
		}
	}

	NPC_UpdateAngles(qtrue, qtrue);
}

/*
-------------------------
NPC_BSDroid_Pain
-------------------------
*/
void NPC_Droid_Pain(SHR_PAIN_PARAMS)
{
	// SAFETY CHECKS
	if (!self || !self->client || !self->NPC)
		return;

	int anim = 0;
	float pain_chance = 0.0f;

	if (self->NPC->ignorePain)
	{
		return;
	}

	VectorCopy(self->NPC->lastPathAngles, self->s.angles);

	if (self->client->NPC_class == CLASS_R5D2)
	{
		pain_chance = NPC_GetPainChance(self, damage);

		// Put it in pain
		if (SHR_PAIN_MOD == MOD_DEMP2 || SHR_PAIN_MOD == MOD_DEMP2_ALT || Q_flrand(0.0f, 1.0f) < pain_chance)
		{
			// Health is between 0-30 or was hit by a DEMP2 so pop his head
			if (self->health < 30 || SHR_PAIN_MOD == MOD_DEMP2 || SHR_PAIN_MOD == MOD_DEMP2_ALT)
			{
				if (!(self->spawnflags & 2)) // Doesn't have to ALWAYSDIE
				{
					if (self->NPC->localState != LSTATE_SPINNING &&
						!SHR_GetSurfaceRenderStatus(self, "head"))
					{
						SHR_SetSurfaceOnOff(self, "head", TURN_OFF);

						SHR_PlayEffectUp("chunks/r5d2head", self->SHR_currentOrigin);

						SHR_SetShocked(self, level.time + 3000);

						TIMER_Set(self, "droidsmoketotal", 5000);
						TIMER_Set(self, "droidspark", 100);
						self->NPC->localState = LSTATE_SPINNING;
					}
				}
			}
			else
			{
				anim = self->client->ps.legsAnim;

				if (anim == BOTH_STAND2) // On two legs?
				{
					anim = BOTH_PAIN1;
				}
				else // On three legs
				{
					anim = BOTH_PAIN2;
				}

				NPC_SetAnim(self, SETANIM_BOTH, anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

				self->NPC->localState = LSTATE_SPINNING;
				TIMER_Set(self, "roam", Q_irand(1000, 2000));
			}
		}
	}
	else if (self->client->NPC_class == CLASS_MOUSE)
	{
		if (SHR_PAIN_MOD == MOD_DEMP2 || SHR_PAIN_MOD == MOD_DEMP2_ALT)
		{
			self->NPC->localState = LSTATE_SPINNING;
			SHR_SetShocked(self, level.time + 3000);
		}
		else
		{
			self->NPC->localState = LSTATE_BACKINGUP;
		}

		self->NPC->scriptFlags &= ~SCF_LOOK_FOR_ENEMIES;
	}
	else if (self->client->NPC_class == CLASS_R2D2)
	{
		pain_chance = NPC_GetPainChance(self, damage);

		if (SHR_PAIN_MOD == MOD_DEMP2 || SHR_PAIN_MOD == MOD_DEMP2_ALT || Q_flrand(0.0f, 1.0f) < pain_chance)
		{
			anim = self->client->ps.legsAnim;

			if (anim == BOTH_STAND2)
			{
				anim = BOTH_PAIN1;
			}
			else
			{
				anim = BOTH_PAIN2;
			}

			NPC_SetAnim(self, SETANIM_BOTH, anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

			self->NPC->localState = LSTATE_SPINNING;
			TIMER_Set(self, "roam", Q_irand(1000, 2000));
		}
	}
	else if (self->client->NPC_class == CLASS_INTERROGATOR &&
		(SHR_PAIN_MOD == MOD_DEMP2 || SHR_PAIN_MOD == MOD_DEMP2_ALT) && other)
	{
		vec3_t dir = { 0 };

		VectorSubtract(self->SHR_currentOrigin, other->SHR_currentOrigin, dir);
		VectorNormalize(dir);

		VectorMA(self->client->ps.velocity, 550, dir, self->client->ps.velocity);
		self->client->ps.velocity[2] -= 127;
	}

	NPC_Pain(SHR_PAIN_PASS);
}

/*
-------------------------
Droid_Pain
-------------------------
*/
static void Droid_Pain(void)
{
	if (TIMER_Done(NPCS.NPC, "droidpain")) //He's done jumping around
	{
		NPCS.NPCInfo->localState = LSTATE_NONE;
	}
}

/*
-------------------------
NPC_Mouse_Precache
-------------------------
*/
void NPC_Mouse_Precache(void)
{
	for (int i = 1; i < 4; i++)
	{
		G_SoundIndex(va("sound/chars/mouse/misc/mousego%d.wav", i));
	}

	G_EffectIndex("env/small_explode");
	G_SoundIndex("sound/chars/mouse/misc/death1");
	G_SoundIndex("sound/chars/mouse/misc/mouse_lp");
}

/*
-------------------------
NPC_R5D2_Precache
-------------------------
*/
void NPC_R5D2_Precache(void)
{
	for (int i = 1; i < 5; i++)
	{
		G_SoundIndex(va("sound/chars/r5d2/misc/r5talk%d.wav", i));
	}
	G_SoundIndex("sound/chars/mark2/misc/mark2_explo"); // ??
	G_SoundIndex("sound/chars/r2d2/misc/r2_move_lp2.wav");
	G_EffectIndex("env/med_explode");
	G_EffectIndex("volumetric/droid_smoke");
	G_EffectIndex("chunks/r5d2head");
}

/*
-------------------------
NPC_R2D2_Precache
-------------------------
*/
void NPC_R2D2_Precache(void)
{
	for (int i = 1; i < 4; i++)
	{
		G_SoundIndex(va("sound/chars/r2d2/misc/r2d2talk0%d.wav", i));
	}
	G_SoundIndex("sound/chars/mark2/misc/mark2_explo"); // ??
	G_SoundIndex("sound/chars/r2d2/misc/r2_move_lp.wav");
	G_EffectIndex("env/med_explode");
}

/*
-------------------------
NPC_Gonk_Precache
-------------------------
*/
void NPC_Gonk_Precache(void)
{
	G_SoundIndex("sound/chars/gonk/misc/gonktalk1.wav");
	G_SoundIndex("sound/chars/gonk/misc/gonktalk2.wav");

	G_SoundIndex("sound/chars/gonk/misc/death1.wav");
	G_SoundIndex("sound/chars/gonk/misc/death2.wav");
	G_SoundIndex("sound/chars/gonk/misc/death3.wav");

	G_EffectIndex("env/med_explode");
}

/*
-------------------------
NPC_Protocol_Precache
-------------------------
*/
void NPC_Protocol_Precache(void)
{
	G_SoundIndex("sound/chars/mark2/misc/mark2_explo");
	G_EffectIndex("env/med_explode");
}

/*
static void R5D2_OffsetLook( float offset, vec3_t out )
{
	vec3_t	angles, forward, temp;

	GetAnglesForDirection( NPC->currentOrigin, NPCInfo->investigateGoal, angles );
	angles[YAW] += offset;
	AngleVectors( angles, forward, NULL, NULL );
	VectorMA( NPC->currentOrigin, 64, forward, out );

	CalcEntitySpot( NPC, SPOT_HEAD, temp );
	out[2] = temp[2];
}
*/

/*
-------------------------
R5D2_LookAround
-------------------------
*/
/*
static void R5D2_LookAround( void )
{
	vec3_t	lookPos;
	float	perc = (float) ( level.time - NPCInfo->pauseTime ) / (float) NPCInfo->investigateDebounceTime;

	//Keep looking at the spot
	if ( perc < 0.25 )
	{
		VectorCopy( NPCInfo->investigateGoal, lookPos );
	}
	else if ( perc < 0.5f )		//Look up but straight ahead
	{
		R5D2_OffsetLook( 0.0f, lookPos );
	}
	else if ( perc < 0.75f )	//Look right
	{
		R5D2_OffsetLook( 45.0f, lookPos );
	}
	else	//Look left
	{
		R5D2_OffsetLook( -45.0f, lookPos );
	}

	NPC_FacePosition( lookPos , qtrue);
}

*/

/*
-------------------------
NPC_BSDroid_Default
-------------------------
*/
void NPC_BSDroid_Default(void)
{
	if (NPCS.NPCInfo->localState == LSTATE_SPINNING)
	{
		Droid_Spin();
	}
	else if (NPCS.NPCInfo->localState == LSTATE_PAIN)
	{
		Droid_Pain();
	}
	else if (NPCS.NPCInfo->localState == LSTATE_DROP)
	{
		NPC_UpdateAngles(qtrue, qtrue);
		NPCS.ucmd.upmove = Q_flrand(-1.0f, 1.0f) * 64;
	}
	else if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
	{
		Droid_Patrol();
	}
	else
	{
		Droid_Run();
	}
}