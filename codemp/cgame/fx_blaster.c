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

// Blaster Weapon

#include "cg_local.h"

/*
-------------------------
FX_ShotTailScale

As SP (FX_BlasterProjectileThink): for a shot's first 75 msec (just fired, or bounced) the direction given to its
effect is scaled down, which shortens the effect's tail, so it doesn't reach back through the shooter from the
muzzle (Jace 2026-10-29)
-------------------------
*/
void FX_ShotTailScale(const centity_t* cent, vec3_t forward)
{
	int dif = cg.time - cent->currentState.pos.trTime;

	if (dif < 75)
	{
		if (dif < 0)
		{
			dif = 0;
		}
		VectorScale(forward, (float)dif / 75.0f * 0.95f + 0.05f, forward);
	}
}

/*
-------------------------
FX_BlasterProjectileThink
-------------------------
*/

void FX_BlasterProjectileThink(centity_t* cent, const struct weaponInfo_s* weapon)
{
	vec3_t forward;

	if (VectorNormalize2(cent->currentState.pos.trDelta, forward) == 0.0f)
	{
		forward[2] = 1.0f;
	}
	FX_ShotTailScale(cent, forward);

	trap->FX_PlayEffectID(cgs.effects.blasterShotEffect, cent->lerpOrigin, forward, -1, -1, qfalse);
}

/*
-------------------------
FX_BlasterAltFireThink
-------------------------
*/
void FX_BlasterAltFireThink(centity_t* cent, const struct weaponInfo_s* weapon)
{
	vec3_t forward;

	if (VectorNormalize2(cent->currentState.pos.trDelta, forward) == 0.0f)
	{
		forward[2] = 1.0f;
	}
	FX_ShotTailScale(cent, forward);

	trap->FX_PlayEffectID(cgs.effects.blasterShotEffect, cent->lerpOrigin, forward, -1, -1, qfalse);
}

/*
-------------------------
FX_DroidekaProjectileThink

the droideka's bolts (WP_DROIDEKA, from MovieDuels SP)
-------------------------
*/
void FX_DroidekaProjectileThink(centity_t* cent, const struct weaponInfo_s* weapon)
{
	vec3_t forward;

	if (VectorNormalize2(cent->currentState.pos.trDelta, forward) == 0.0f)
	{
		forward[2] = 1.0f;
	}
	FX_ShotTailScale(cent, forward);

	trap->FX_PlayEffectID(cgs.effects.droidekaShotEffect, cent->lerpOrigin, forward, -1, -1, qfalse);
}

/*
-------------------------
FX_CloneProjectileThink / FX_CloneWeaponHitWall / FX_CloneWeaponHitPlayer

the Z-6 rotary cannon's bolts (WP_Z6_ROTARY_CANNON, from MovieDuels)
-------------------------
*/
void FX_CloneProjectileThink(centity_t* cent, const struct weaponInfo_s* weapon)
{
	vec3_t forward;

	if (VectorNormalize2(cent->currentState.pos.trDelta, forward) == 0.0f)
	{
		forward[2] = 1.0f;
	}
	FX_ShotTailScale(cent, forward);

	trap->FX_PlayEffectID(cgs.effects.cloneShotEffect, cent->lerpOrigin, forward, -1, -1, qfalse);
}

void FX_CloneWeaponHitWall(vec3_t origin, vec3_t normal)
{
	trap->FX_PlayEffectID(cgs.effects.cloneWallImpactEffect, origin, normal, -1, -1, qfalse);
}

/*
-------------------------
FX_NoghriShotProjectileThink / FX_NoghriShotWeaponHitWall / FX_NoghriShotWeaponHitPlayer

the noghri stick's darts (WP_NOGHRI_STICK, from SJE SP's FX_NoghriShot.cpp)
-------------------------
*/
void FX_NoghriShotProjectileThink(centity_t* cent, const struct weaponInfo_s* weapon)
{
	vec3_t forward;

	if (VectorNormalize2(cent->currentState.pos.trDelta, forward) == 0.0f)
	{
		forward[2] = 1.0f;
	}
	FX_ShotTailScale(cent, forward);

	// as SP: shorten the tail for a moment after it was fired so it does not clip so harshly
	int dif = cg.time - cent->currentState.pos.trTime;

	if (dif < 75)
	{
		if (dif < 0)
		{
			dif = 0;
		}

		VectorScale(forward, dif / 75.0f * 0.95f + 0.05f, forward);
	}

	trap->FX_PlayEffectID(cgs.effects.noghriShotEffect, cent->lerpOrigin, forward, -1, -1, qfalse);
}

void FX_NoghriShotWeaponHitWall(vec3_t origin, vec3_t normal)
{
	trap->FX_PlayEffectID(cgs.effects.noghriImpactEffect, origin, normal, -1, -1, qfalse); // SP has no wall impact either
}

void FX_NoghriShotWeaponHitPlayer(vec3_t origin, vec3_t normal)
{
	trap->FX_PlayEffectID(cgs.effects.noghriImpactEffect, origin, normal, -1, -1, qfalse);
}

/*
-------------------------
FX_TuskenShotProjectileThink / FX_TuskenShotWeaponHitWall / FX_TuskenShotWeaponHitPlayer

the tusken rifle's shot (WP_TUSKEN_RIFLE, from SJE SP's FX_TuskenShot.cpp)
-------------------------
*/
void FX_TuskenShotProjectileThink(centity_t* cent, const struct weaponInfo_s* weapon)
{
	vec3_t forward;

	if (VectorNormalize2(cent->currentState.pos.trDelta, forward) == 0.0f)
	{
		forward[2] = 1.0f;
	}
	FX_ShotTailScale(cent, forward);

	// as SP: shorten the tail for a moment after it was fired so it does not clip so harshly
	int dif = cg.time - cent->currentState.pos.trTime;

	if (dif < 75)
	{
		if (dif < 0)
		{
			dif = 0;
		}

		VectorScale(forward, dif / 75.0f * 0.95f + 0.05f, forward);
	}

	trap->FX_PlayEffectID(cgs.effects.tuskenShotEffect, cent->lerpOrigin, forward, -1, -1, qfalse);
}

void FX_TuskenShotWeaponHitWall(vec3_t origin, vec3_t normal)
{
	trap->FX_PlayEffectID(cgs.effects.tuskenHitWallEffect, origin, normal, -1, -1, qfalse);
}

void FX_TuskenShotWeaponHitPlayer(vec3_t origin, vec3_t normal)
{
	trap->FX_PlayEffectID(cgs.effects.tuskenHitEffect, origin, normal, -1, -1, qfalse);
}

void FX_CloneWeaponHitPlayer(vec3_t origin, vec3_t normal, qboolean humanoid)
{
	trap->FX_PlayEffectID(humanoid ? cgs.effects.cloneFleshImpactEffect : cgs.effects.cloneWallImpactEffect, origin, normal,
		-1, -1, qfalse);
}

/*
-------------------------
FX_BlasterWeaponHitWall
-------------------------
*/
void FX_BlasterWeaponHitWall(vec3_t origin, vec3_t normal)
{
	trap->FX_PlayEffectID(cgs.effects.blasterWallImpactEffect, origin, normal, -1, -1, qfalse);
}

/*
-------------------------
FX_BlasterWeaponHitPlayer
-------------------------
*/
void FX_BlasterWeaponHitPlayer(vec3_t origin, vec3_t normal, const qboolean humanoid)
{
	if (humanoid)
	{
		trap->FX_PlayEffectID(cgs.effects.blasterFleshImpactEffect, origin, normal, -1, -1, qfalse);
	}
	else
	{
		trap->FX_PlayEffectID(cgs.effects.blasterDroidImpactEffect, origin, normal, -1, -1, qfalse);
	}
}

/////////////// eweb

/*
-------------------------
FX_ewebProjectileThink
-------------------------
*/

void FX_EwebProjectileThink(centity_t* cent, const struct weaponInfo_s* weapon)
{
	vec3_t forward;

	if (VectorNormalize2(cent->currentState.pos.trDelta, forward) == 0.0f)
	{
		forward[2] = 1.0f;
	}
	FX_ShotTailScale(cent, forward);

	trap->FX_PlayEffectID(cgs.effects.ewebShotEffect, cent->lerpOrigin, forward, -1, -1, qfalse);
}

/*
-------------------------
FX_ewebAltFireThink
-------------------------
*/
void FX_EwebAltFireThink(centity_t* cent, const struct weaponInfo_s* weapon)
{
	vec3_t forward;

	if (VectorNormalize2(cent->currentState.pos.trDelta, forward) == 0.0f)
	{
		forward[2] = 1.0f;
	}
	FX_ShotTailScale(cent, forward);

	trap->FX_PlayEffectID(cgs.effects.ewebShotEffect, cent->lerpOrigin, forward, -1, -1, qfalse);
}

/*
-------------------------
FX_ewebWeaponHitWall
-------------------------
*/
void FX_EwebWeaponHitWall(vec3_t origin, vec3_t normal)
{
	trap->FX_PlayEffectID(cgs.effects.ewebWallImpactEffect, origin, normal, -1, -1, qfalse);
}

/*
-------------------------
FX_ewebWeaponHitPlayer
-------------------------
*/
void FX_EwebWeaponHitPlayer(vec3_t origin, vec3_t normal, const qboolean humanoid)
{
	if (humanoid)
	{
		trap->FX_PlayEffectID(cgs.effects.ewebFleshImpactEffect, origin, normal, -1, -1, qfalse);
	}
	else
	{
		trap->FX_PlayEffectID(cgs.effects.ewebDroidImpactEffect, origin, normal, -1, -1, qfalse);
	}
}