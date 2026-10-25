/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
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

#pragma once

// Filename:-	bg_weapons.h
//
// This crosses both client and server.  It could all be crammed into bg_public, but isolation of this type of data is best.

typedef enum
{
	WP_NONE,

	WP_STUN_BATON,
	WP_MELEE,
	WP_SABER,
	WP_BRYAR_PISTOL,
	WP_BLASTER,
	WP_DISRUPTOR,
	WP_BOWCASTER,
	WP_REPEATER,
	WP_DEMP2,
	WP_FLECHETTE,
	WP_ROCKET_LAUNCHER,
	WP_THERMAL,
	WP_TRIP_MINE,
	WP_DET_PACK,
	WP_CONCUSSION,
	WP_DROIDEKA, // the droideka's twin arm blasters (from MovieDuels SP), for droideka NPCs
	WP_Z6_ROTARY_CANNON, // the Z-6 rotary cannon (from MovieDuels): before WP_BRYAR_OLD so it stays player usable (LAST_USEABLE_WEAPON);
	// BRYAR_OLD, EMPLACED_GUN and TURRET moved up one (g_weaponDisable bits for them shift too)
	WP_NOGHRI_STICK, // the noghri stick (from SJE SP): a poison dart that leaves a gas cloud, the alt fire swings the stick;
	// also before WP_BRYAR_OLD so it stays player usable (BRYAR_OLD, EMPLACED_GUN and TURRET moved up one more)
	WP_TUSKEN_RIFLE, // the tusken rifle (from SJE SP): the primary fire is a swing with the butt, the alt fire the shot
	WP_TUSKEN_STAFF, // the tusken staff (gaffi stick, from SJE SP): swings only, the alt fire is the lunge
	// both before WP_BRYAR_OLD too (BRYAR_OLD, EMPLACED_GUN and TURRET moved up two more)

	WP_BRYAR_OLD,
	WP_EMPLACED_GUN,
	WP_TURRET,

	WP_NUM_WEAPONS
} weapon_t;

//anything > this will be considered not player useable
#define LAST_USEABLE_WEAPON			WP_BRYAR_OLD

// owned weapons (2026-10-25, widened past 32 weapons): STAT_WEAPONS holds weapons 0-31, STAT_WEAPONS2 weapons 32-63
// (the engine sends both in 32 bits, msg.cpp). Every owned-weapon bit goes through these: never 1 << weapon on
// STAT_WEAPONS, that is undefined from weapon 32 on.
#define WP_STAT_INDEX(w)			((w) < 32 ? STAT_WEAPONS : STAT_WEAPONS2)
#define WP_STAT_BIT(w)				(1u << ((unsigned)(w) & 31u))
#define BG_HasWeapon(stats, w)		(((unsigned)(stats)[WP_STAT_INDEX(w)] & WP_STAT_BIT(w)) != 0)
#define BG_GiveWeapon(stats, w)		((stats)[WP_STAT_INDEX(w)] |= (int)WP_STAT_BIT(w))
#define BG_TakeWeapon(stats, w)		((stats)[WP_STAT_INDEX(w)] &= ~(int)WP_STAT_BIT(w))
#define BG_ClearWeapons(stats)		((stats)[STAT_WEAPONS] = 0, (stats)[STAT_WEAPONS2] = 0)
#define BG_SetOnlyWeapon(stats, w)	(BG_ClearWeapons(stats), BG_GiveWeapon(stats, w))
#define BG_HasAnyWeapon(stats)		((stats)[STAT_WEAPONS] != 0 || (stats)[STAT_WEAPONS2] != 0)
#define WP_MASK_HAS(mask, w)		((w) < 32 && ((unsigned)(mask) & (1u << ((unsigned)(w) & 31u))) != 0) // a 32 bit weapon mask
#define WP_MASK_ADD(mask, w)		((w) < 32 ? ((mask) |= (int)(1u << ((unsigned)(w) & 31u))) : (mask)) // (g_weaponDisable, siege class, NPC): weapons 32+ never in one
#define WP_MELEEONLY		524283
#define WP_SABERSONLY		524279
#define WP_MELEESABERS		524275
#define WP_NOEXPLOS			28672

#define WP_ALLDISABLED		524287
#define FP_ALLDISABLED		262143

typedef enum //# ammo_e
{
	AMMO_NONE,
	AMMO_FORCE,
	// AMMO_PHASER
	AMMO_BLASTER,
	// AMMO_STARFLEET,
	AMMO_POWERCELL,
	// AMMO_ALIEN,
	AMMO_METAL_BOLTS,
	AMMO_ROCKETS,
	AMMO_EMPLACED,
	AMMO_THERMAL,
	AMMO_TRIPMINE,
	AMMO_DETPACK,
	AMMO_MAX
} ammo_t;

typedef struct weaponData_s
{
	//	char	classname[32];		// Spawning name

	int ammoIndex; // Index to proper ammo slot
	int ammoLow; // Count when ammo is low

	int energyPerShot; // Amount of energy used per shot
	int fireTime; // Amount of time between firings
	int range; // Range of weapon

	int altEnergyPerShot; // Amount of energy used for alt-fire
	int altFireTime; // Amount of time between alt-firings
	int altRange; // Range of alt-fire

	int chargeSubTime; // ms interval for subtracting ammo during charge
	int altChargeSubTime; // above for secondary

	int chargeSub; // amount to subtract during charge on each interval
	int altChargeSub; // above for secondary

	int maxCharge; // stop subtracting once charged for this many ms
	int altMaxCharge; // above for secondary
} weaponData_t;

typedef struct ammoData_s
{
	//	char	icon[32];	// Name of ammo icon file
	int max; // Max amount player can hold of ammo
} ammoData_t;

extern weaponData_t weaponData[WP_NUM_WEAPONS];
extern ammoData_t ammoData[AMMO_MAX];

// Specific weapon information

#define FIRST_WEAPON		WP_BRYAR_PISTOL		// this is the first weapon for next and prev weapon switching
#define MAX_PLAYER_WEAPONS	WP_BRYAR_OLD	// this is the max you can switch to and get with the give all.

#define DEFAULT_SHOTGUN_SPREAD	700
#define DEFAULT_SHOTGUN_COUNT	11

#define	LIGHTNING_RANGE		768
