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

#include "qcommon/q_shared.h"

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

// ---------------------------------------------------------------------------------------------------------------
// Weapon variants (2026-10-31, the Jedi Knight Galaxies way): many guns on the few base weapons. A variant keeps its
// base weapon's behaviour (missiles, alt fire, AI, ammo) but has its own world model, HUD icon, fire sounds, muzzle,
// shot and impact effects, and damage / fire rate / shot speed scales. They come from ext_data/sje_weaponvariants.dat
// (made from the original .wpn files). ps.weaponVariant / s.weaponVariant hold the index (0 = the stock weapon); it is
// only used while the holder's weapon is the variant's base weapon. Gunner NPCs and bots get one each
// (G_WeaponVariantThink), the least used of their weapon's variants; saber users never do.
// ---------------------------------------------------------------------------------------------------------------
#define MAX_WEAPON_VARIANTS		256		// index 0 = none (the stock weapon); networked in 8 bits
#define MAX_WEAPON_VARIANT_SOUNDS	4
#define WEAPON_VARIANT_FILE		"ext_data/sje_weaponvariants.dat"

typedef struct weaponVariantFire_s
{
	char sound[MAX_WEAPON_VARIANT_SOUNDS][MAX_QPATH]; // fire sounds, one picked at random
	int numSounds;
	char muzzleFx[MAX_QPATH]; // effects (FX_RegisterEffect names, no "effects/" and no ".efx")
	char shotFx[MAX_QPATH]; // the flying shot
	char shotModel[MAX_QPATH]; // optional md3 on the flying shot (rockets)
	char shotSound[MAX_QPATH]; // optional loop on the flying shot
	char missFx[MAX_QPATH]; // the shot hits a wall
	char hitFx[MAX_QPATH]; // the shot hits a body
	float damageScale; // x the base weapon's damage
	float fireTimeScale; // x the base weapon's time between shots
	float speedScale; // x the base weapon's shot speed
} weaponVariantFire_t;

typedef struct weaponVariant_s
{
	char name[64]; // the variant's name (the original classname), e.g. "carbine_E-11"
	char displayName[64]; // e.g. "E-11 Carbine"
	int baseWeapon; // weapon_t
	qboolean npcUse; // issued to gunner NPCs and bots (1, default)
	char worldModel[MAX_QPATH]; // .glm with a *flash bolt
	char icon[MAX_QPATH]; // HUD icon shader
	char selectSound[MAX_QPATH];
	weaponVariantFire_t fire[2]; // 0 = primary, 1 = alt (copied from the primary when the file has no alt values)
	int clipShots; // AI reload (2026-10-31): shots an NPC/bot fires before it reloads (0 = the base weapon's default)
	int reloadTime; // ms the reload takes (0 = the base weapon's default)
	int grenadeKind; // grenades (2026-10-31, base WP_THERMAL): GK_*, what the blast does besides damage
	float radiusScale; // grenades: blast radius, 1 = the stock thermal's
	int effectTime; // grenades: ms the status effect lasts (burn, poison, freeze, stun, blind), 0 = the kind's default
	int price; // the shop (2026-10-31): credits for the gun look (with the gun and ammo), grenades: a pack of 3; 0 = default
} weaponVariant_t;

// ---------------------------------------------------------------------------------------------------------------
// Grenades (2026-10-31, DH: the SJE grenades + cooking): grenade types are WP_THERMAL variants ("grenade <kind>" in
// the variant file). The kind decides the status effect on everyone in the blast (see G_GrenadeBlast).
// ---------------------------------------------------------------------------------------------------------------
typedef enum
{
	GK_FRAG, // damage only (the stock thermal)
	GK_FIRE, // sets people on fire (damage over time, water puts it out)
	GK_FREEZE, // frozen solid (cryoban)
	GK_STUN, // knocked down and shocked
	GK_FLASH, // blinded (white screen for players, NPCs can't fight)
	GK_SMOKE, // a smoke cloud, no damage
	GK_POISON, // poison gas (damage over time, droids immune)
	GK_HEAL, // bacta: heals allies, cures fire and poison
	GK_NUM_KINDS
} grenadeKind_t;

extern weaponVariant_t bgWeaponVariants[MAX_WEAPON_VARIANTS];
extern int bgNumWeaponVariants; // the number of slots in use, slot 0 included (1 = no variants loaded)

void BG_LoadWeaponVariants(void);
const weaponVariant_t* BG_GetWeaponVariant(int variant, int weapon); // NULL unless variant is loaded and its base is weapon
int BG_WeaponVariantByName(const char* name); // 0 when not found
int BG_WeaponVariantCount(int weapon); // how many npc-use variants this base weapon has
int BG_WeaponVariantFireTime(int variant, int weapon, int fire_time, qboolean alt_fire);
int BG_WeaponVariantFamily(int weapon); // the base weapon a variant names for this weapon (WP_BRYAR_OLD -> WP_BRYAR_PISTOL)

// ---------------------------------------------------------------------------------------------------------------
// AI clips and reloads (2026-10-31, g_aiReload): gunner NPCs and bots count their shots and stop to reload when the
// clip is empty (BOTH_RELOAD, reload sound, duck or run to cover, a callout). Clip and reload time come from the
// weapon variant (the gun's clip size and reload time) or, for stock guns, from these defaults. 0 = no clip.
// ---------------------------------------------------------------------------------------------------------------
int BG_WeaponClipShots(int variant, int weapon);
int BG_WeaponReloadTime(int variant, int weapon);
int BG_GrenadeKindByName(const char* name); // "frag", "fire"... -> GK_*, GK_FRAG when unknown

// ---------------------------------------------------------------------------------------------------------------
// The shop (2026-10-31, DH): players buy the gun looks (weapon variants) and grenade types with credits earned for
// kills (g_pazaak shop code). SHOP_OWNED_WORDS: the bits of the owned variants; SHOP_MAX_WEAPONS: one equipped look
// per weapon number. Shop items past the variants: SHOP_ITEM_AMMO (refill), SHOP_ITEM_BACTA.
// ---------------------------------------------------------------------------------------------------------------
#define SHOP_OWNED_WORDS	(MAX_WEAPON_VARIANTS / 32)
#define SHOP_MAX_WEAPONS	64
#define SHOP_ITEM_AMMO		1000
#define SHOP_ITEM_BACTA		1001
int BG_WeaponVariantPrice(int variant); // credits (the file's price, or a default by base weapon)
