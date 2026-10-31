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

// Filename:-	weapons.h
//
// Note that this is now included from both server and game modules, so don't include any other header files
//	within this one that might break stuff...

#ifndef __WEAPONS_H__
#define __WEAPONS_H__

#include "../qcommon/q_shared.h"

using weapon_t = enum //# weapon_e
{
	WP_NONE,

	// Player weapons
	WP_SABER,
	// player and NPC weapon
	WP_BLASTER_PISTOL,
	// player and NPC weapon
	WP_BLASTER,
	// player and NPC weapon
	WP_DISRUPTOR,
	// player and NPC weapon
	WP_BOWCASTER,
	// NPC weapon - player can pick this up, but never starts with them
	WP_REPEATER,
	// NPC weapon - player can pick this up, but never starts with them
	WP_DEMP2,
	// NPC weapon - player can pick this up, but never starts with them
	WP_FLECHETTE,
	// NPC weapon - player can pick this up, but never starts with them
	WP_ROCKET_LAUNCHER,
	// NPC weapon - player can pick this up, but never starts with them
	WP_THERMAL,
	// player and NPC weapon
	WP_TRIP_MINE,
	// NPC weapon - player can pick this up, but never starts with them
	WP_DET_PACK,
	// NPC weapon - player can pick this up, but never starts with them
	WP_CONCUSSION,
	// NPC weapon - player can pick this up, but never starts with them

	//extras
	WP_MELEE,
	// player and NPC weapon - Any ol' melee attack

	//when in atst
	WP_ATST_MAIN,
	WP_ATST_SIDE,

	// These can never be gotten directly by the player
	WP_STUN_BATON,
	// stupid weapon, should remove

	//NPC weapons
	WP_BRYAR_PISTOL,
	//KYLE WEAPON - player can pick this up, but never starts with them

	WP_SBD_PISTOL,
	//SBD WEAPON
	WP_WRIST_BLASTER,

	WP_EMPLACED_GUN,
	WP_DROIDEKA,

	WP_BOT_LASER,
	// Probe droid	- Laser blast

	WP_TURRET,
	// turret guns

	WP_TIE_FIGHTER,

	WP_RAPID_FIRE_CONC,

	WP_JAWA,
	WP_TUSKEN_RIFLE,
	WP_TUSKEN_STAFF,
	WP_SCEPTER,
	WP_NOGHRI_STICK,
	WP_Z6_ROTARY_CANNON,	// index 32: owned through STAT_WEAPONS2 (BG_HasWeapon / BG_GiveWeapon)

	//# #eol
	WP_NUM_WEAPONS
};

// reward sounds
using reward_t = enum
{
	REWARD_BAD,

	REWARD_IMPRESSIVE,
	REWARD_EXCELLENT,
	REWARD_DENIED,
	REWARD_GAUNTLET
};

#define FIRST_WEAPON		WP_SABER		// this is the first weapon for next and prev weapon switching
#define MAX_PLAYER_WEAPONS	WP_SBD_PISTOL	// this is the max you can switch to and get with the give all. - FIXME: it's actually this one *minus* one... why?

#define FIRST_VEHICLEWEAPON		WP_BLASTER_PISTOL

extern qboolean playerUsableWeapons[WP_NUM_WEAPONS];

// AMMO_NONE must be first and AMMO_MAX must be last, cause weapon load validates based off of these vals
using ammo_t = enum //# ammo_e
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
};

using weaponData_t = struct weaponData_s
{
	char classname[32]; // Spawning name
	char weaponMdl[64]; // Weapon Model
	char firingSnd[64]; // Sound made when fired
	char altFiringSnd[64]; // Sound made when alt-fired
	//	char	flashSnd[64];		// Sound made by flash
	//	char	altFlashSnd[64];	// Sound made by an alt-flash
	char stopSnd[64]; // Sound made when weapon stops firing
	char chargeSnd[64]; // sound to start when the weapon initiates the charging sequence
	char altChargeSnd[64]; // alt sound to start when the weapon initiates the charging sequence
	char selectSnd[64]; // the sound to play when this weapon gets selected

	int ammoIndex; // Index to proper ammo slot
	int ammoLow; // Count when ammo is low

	int energyPerShot; // Amount of energy used per shot
	int fireTime; // Amount of time between firings
	int range; // Range of weapon

	int altEnergyPerShot; // Amount of energy used for alt-fire
	int altFireTime; // Amount of time between alt-firings
	int altRange; // Range of alt-fire

	char weaponIcon[64]; // Name of weapon icon file
	int numBarrels; // how many barrels should we expect for this weapon?

	char missileMdl[64]; // Missile Model
	char missileSound[64]; // Missile flight sound
	char stasismissileSound[64]; // Missile flight sound
	float missileDlight; // what is says
	vec3_t missileDlightColor; // ditto

	char alt_missileMdl[64]; // Missile Model
	char alt_missileSound[64]; // Missile sound
	float alt_missileDlight; // what is says
	vec3_t alt_missileDlightColor; // ditto

	char missilehit_sound[64]; // Missile impact sound
	char altmissilehit_sound[64]; // alt Missile impact sound
	void* func;
	void* altfunc;

	char mMuzzleEffect[64];
	int mMuzzleEffectID;
	char mAltMuzzleEffect[64];
	int mAltMuzzleEffectID;

	char mOverloadMuzzleEffect[64];
	int mOverloadMuzzleEffectID;

	int damage;
	int altDamage;
	int splashDamage;
	int altSplashDamage;
	float splashRadius;
	float altSplashRadius;
};

using ammoData_t = struct ammoData_s
{
	char icon[64]; // Name of ammo icon file
	int max; // Max amount player can hold of ammo
};

#define RUNNING_SPREAD			1.7f
#define WALKING_SPREAD			1.4f

#define CLONECOMMANDO_MAIN_SPREAD			0.1f
#define CLONECOMMANDO_ALT_SPREAD			0.1f
#define CLONECOMMANDO_NPC_SPREAD			0.1f
#define CLONECOMMANDO_VELOCITY				3500
#define CLONECOMMANDO_NPC_VEL_CUT			0.5f
#define CLONECOMMANDO_NPC_HARD_VEL_CUT		0.7f
#define CLONECOMMANDO_DAMAGE				15
#define	CLONECOMMANDO_NPC_DAMAGE_EASY		10
#define	CLONECOMMANDO_NPC_DAMAGE_NORMAL		13
#define	CLONECOMMANDO_NPC_DAMAGE_HARD		15
#define CLONERIFLE_SPREAD				0.1f//1.2f
#define CLONERIFLE_VELOCITY			2300
#define CLONERIFLE_DAMAGE				20

// Bryar Pistol
//--------
#define BRYAR_PISTOL_VEL			2000
#define BRYAR_PISTOL_DAMAGE			15
#define BRYAR_CHARGE_UNIT			200.0f	// bryar charging gives us one more unit every 200ms--if you change this, you'll have to do the same in bg_pmove

// E11 Blaster
//---------
#define BLASTER_MAIN_SPREAD			0.5f
#define BLASTER_ALT_SPREAD			1.0f
#define BLASTER_NPC_SPREAD			0.75f
#define BLASTER_VELOCITY			2300
#define BLASTER_NPC_VEL_CUT			0.5f
#define BLASTER_NPC_HARD_VEL_CUT	0.7f
#define BLASTER_DAMAGE				25
#define	BLASTER_NPC_DAMAGE_EASY		8
#define	BLASTER_NPC_DAMAGE_NORMAL	20
#define	BLASTER_NPC_DAMAGE_HARD		35
#define	SJE_BLASTER_NPC_DAMAGE_HARD		50

// Tenloss Disruptor
//----------
#define DISRUPTOR_MAIN_DAMAGE			30
#define DISRUPTOR_NPC_MAIN_DAMAGE_EASY	5
#define DISRUPTOR_NPC_MAIN_DAMAGE_MEDIUM	10
#define DISRUPTOR_NPC_MAIN_DAMAGE_HARD	15

#define DISRUPTOR_ALT_DAMAGE			900
#define DISRUPTOR_NPC_ALT_DAMAGE_EASY	15
#define DISRUPTOR_NPC_ALT_DAMAGE_MEDIUM	25
#define DISRUPTOR_NPC_ALT_DAMAGE_HARD	30
#define DISRUPTOR_ALT_TRACES			3		// can go through a max of 3 entities
#define DISRUPTOR_CHARGE_UNIT			150.0f	// distruptor charging gives us one more unit every 150ms--if you change this, you'll have to do the same in bg_pmove

// Wookie Bowcaster
//----------
#define	BOWCASTER_DAMAGE			30
#define	BOWCASTER_VELOCITY			1600
#define	BOWCASTER_NPC_DAMAGE_EASY	12
#define	BOWCASTER_NPC_DAMAGE_NORMAL	24
#define	BOWCASTER_NPC_DAMAGE_HARD	40
#define BOWCASTER_SPLASH_DAMAGE		0
#define BOWCASTER_SPLASH_RADIUS		0
#define BOWCASTER_SIZE				2

#define BOWCASTER_ALT_SPREAD		0.5f
#define BOWCASTER_VEL_RANGE			0.3f
#define BOWCASTER_CHARGE_UNIT		200.0f	// bowcaster charging gives us one more unit every 200ms--if you change this, you'll have to do the same in bg_pmove

// Heavy Repeater
//----------
#define REPEATER_SPREAD				0.3f
#define REPEATER_NPC_SPREAD			0.7f
#define	REPEATER_DAMAGE				10
#define	REPEATER_VELOCITY			2000
#define	REPEATER_NPC_DAMAGE_EASY	2
#define	REPEATER_NPC_DAMAGE_NORMAL	4
#define	REPEATER_NPC_DAMAGE_HARD	10

#define REPEATER_ALT_SIZE				3	// half of bbox size
#define	REPEATER_ALT_DAMAGE				60
#define REPEATER_ALT_SPLASH_DAMAGE		60
#define REPEATER_ALT_SPLASH_RADIUS		128
#define	REPEATER_ALT_VELOCITY			1100
#define	REPEATER_ALT_NPC_DAMAGE_EASY	15
#define	REPEATER_ALT_NPC_DAMAGE_NORMAL	30
#define	REPEATER_ALT_NPC_DAMAGE_HARD	45

//  Z-6 Rotary Cannon (from MovieDuels)
//---------
#define Z6_ROTARY_CANNON_SPREAD				1.1f
#define Z6_ROTARY_CANNON_NPC_SPREAD			1.2f
#define Z6_ROTARY_CANNON_VELOCITY			2200
#define Z6_ROTARY_CANNON_DAMAGE				12
#define Z6_ROTARY_CANNON_NPC_VEL_CUT		0.7f
#define Z6_ROTARY_CANNON_NPC_HARD_VEL_CUT	0.5f
#define Z6_ROTARY_CANNON_NPC_DAMAGE_EASY	8
#define Z6_ROTARY_CANNON_NPC_DAMAGE_NORMAL	10
#define Z6_ROTARY_CANNON_NPC_DAMAGE_HARD	12

// DEMP2
//----------
#define	DEMP2_DAMAGE				35
#define	DEMP2_VELOCITY				2000
#define	DEMP2_NPC_DAMAGE_EASY		6
#define	DEMP2_NPC_DAMAGE_NORMAL		12
#define	DEMP2_NPC_DAMAGE_HARD		20
#define	DEMP2_SIZE					2		// half of bbox size

#define DEMP2_ALT_DAMAGE			15
#define DEMP2_CHARGE_UNIT			500.0f	// demp2 charging gives us one more unit every 500ms--if you change this, you'll have to do the same in bg_pmove

#define DEMP2_ALT_RANGE				4096
#define DEMP2_ALT_SPLASHRADIUS		256

// Golan Arms Flechette
//---------
#define FLECHETTE_SHOTS				6
#define FLECHETTE_SPREAD			0.5f
#define FLECHETTE_DAMAGE			5
#define FLECHETTE_VEL				3100
#define FLECHETTE_SIZE				1

#define FLECHETTE_ALT_DAMAGE		20
#define FLECHETTE_ALT_SPLASH_DAM	20
#define FLECHETTE_ALT_SPLASH_RAD	128

// NOT CURRENTLY USED
#define FLECHETTE_MINE_RADIUS_CHECK		200
#define FLECHETTE_MINE_VEL				1000
#define FLECHETTE_MINE_DAMAGE			100
#define FLECHETTE_MINE_SPLASH_DAMAGE	200
#define FLECHETTE_MINE_SPLASH_RADIUS	200

// Personal Rocket Launcher
//---------
#define	ROCKET_VELOCITY				1200
#define	ROCKET_DAMAGE				800
#define	ROCKET_SPLASH_DAMAGE		350
#define	ROCKET_SPLASH_RADIUS		256
#define ROCKET_NPC_DAMAGE_EASY		20
#define ROCKET_NPC_DAMAGE_NORMAL	40
#define ROCKET_NPC_DAMAGE_HARD		60
#define ROCKET_SIZE					3

#define	ROCKET_ALT_VELOCITY			(ROCKET_VELOCITY*0.5)
#define ROCKET_ALT_THINK_TIME		100

// Concussion Rifle
//---------
//primary
#define	CONC_VELOCITY				1500
#define	CONC_DAMAGE					150
#define CONC_NPC_SPREAD				0.5f
#define	CONC_NPC_DAMAGE_EASY		40
#define	CONC_NPC_DAMAGE_NORMAL		80
#define	CONC_NPC_DAMAGE_HARD		100
#define	CONC_SPLASH_DAMAGE			50
#define	CONC_SPLASH_RADIUS			300
//alt
#define CONC_ALT_DAMAGE				225
#define CONC_ALT_NPC_DAMAGE_EASY	20
#define CONC_ALT_NPC_DAMAGE_MEDIUM	35
#define CONC_ALT_NPC_DAMAGE_HARD	50

// Emplaced Gun
//--------------
#define EMPLACED_VEL				6000	// very fast
#define EMPLACED_DAMAGE				150		// and very damaging
#define EMPLACED_SIZE				5		// make it easier to hit things

// ATST Main Gun
//--------------
#define ATST_MAIN_VEL				4000	//
#define ATST_MAIN_DAMAGE			25		//
#define ATST_MAIN_SIZE				3		// make it easier to hit things

// ATST Side Gun
//---------------
#define ATST_SIDE_MAIN_DAMAGE				75
#define ATST_SIDE_MAIN_VELOCITY				1300
#define ATST_SIDE_MAIN_NPC_DAMAGE_EASY		30
#define ATST_SIDE_MAIN_NPC_DAMAGE_NORMAL	40
#define ATST_SIDE_MAIN_NPC_DAMAGE_HARD		50
#define ATST_SIDE_MAIN_SIZE					4
#define ATST_SIDE_MAIN_SPLASH_DAMAGE		10	// yeah, pretty small, either zero out or make it worth having?
#define ATST_SIDE_MAIN_SPLASH_RADIUS		16	// yeah, pretty small, either zero out or make it worth having?

#define ATST_SIDE_ALT_VELOCITY				1100
#define ATST_SIDE_ALT_NPC_VELOCITY			600
#define ATST_SIDE_ALT_DAMAGE				130

#define ATST_SIDE_ROCKET_NPC_DAMAGE_EASY	30
#define ATST_SIDE_ROCKET_NPC_DAMAGE_NORMAL	50
#define ATST_SIDE_ROCKET_NPC_DAMAGE_HARD	90

#define	ATST_SIDE_ALT_SPLASH_DAMAGE			130
#define	ATST_SIDE_ALT_SPLASH_RADIUS			200
#define ATST_SIDE_ALT_ROCKET_SIZE			5
#define ATST_SIDE_ALT_ROCKET_SPLASH_SCALE	0.5f	// scales splash for NPC's

// Stun Baton
//--------------
#define STUN_BATON_DAMAGE			22
#define STUN_BATON_ALT_DAMAGE		22
#define STUN_BATON_RANGE			25

// Laser Trip Mine
//--------------
#define LT_DAMAGE			150
#define LT_SPLASH_RAD		256.0f
#define LT_SPLASH_DAM		90

#define LT_VELOCITY			250.0f
#define LT_ALT_VELOCITY		1000.0f

#define PROX_MINE_RADIUS_CHECK		190

#define LT_SIZE				3.0f
#define LT_ALT_TIME			2000
#define	LT_ACTIVATION_DELAY	1000
#define	LT_DELAY_TIME		50

// Thermal Detonator
//--------------
#define TD_DAMAGE			500
#define TD_NPC_DAMAGE_CUT	0.6f	// NPC thrown dets deliver only 60% of the damage that a player thrown one does
#define TD_SPLASH_RAD		300
#define TD_SPLASH_DAM		250
#define TD_VELOCITY			900
#define TD_MIN_CHARGE		0.15f
#define TD_TIME				2000
#define TD_THINK_TIME		300		// don't think too often?
#define TD_TEST_RAD			(TD_SPLASH_RAD * 0.8f) // no sense in auto-blowing up if exactly on the radius edge--it would hardly do any damage

#define TD_ALT_DAMAGE		500
#define TD_ALT_SPLASH_RAD	256
#define TD_ALT_SPLASH_DAM	250
#define TD_ALT_VELOCITY		600
#define TD_ALT_MIN_CHARGE	0.15f
#define TD_ALT_TIME			3000

// Tusken Rifle Shot
//--------------
#define TUSKEN_RIFLE_VEL			3000	// fast
#define TUSKEN_RIFLE_DAMAGE_EASY	20		// damaging
#define TUSKEN_RIFLE_DAMAGE_MEDIUM	30		// very damaging
#define TUSKEN_RIFLE_DAMAGE_HARD	50		// extremely damaging

// ---------------------------------------------------------------------------------------------------------------
// Weapon variants (2026-10-31, the Jedi Knight Galaxies way): many guns on the few base weapons. A variant keeps its
// base weapon's behaviour (missiles, alt fire, AI, ammo) but has its own world model, HUD icon, fire sounds, muzzle,
// shot and impact effects, and damage / fire rate / shot speed scales. They come from ext_data/sje_weaponvariants.dat
// (made from the original .wpn files). ps.weaponVariant / s.weaponVariant hold the index (0 = the stock weapon); it is
// only used while the holder's weapon is the variant's base weapon. Gunner NPCs and bots get one each
// (G_WeaponVariantThink, NPCs only in SP), the least used of their weapon's variants; saber users never do.
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
int BG_WeaponVariantFamily(int weapon); // the base weapon a variant names for this weapon (SP: WP_BRYAR_PISTOL -> WP_BLASTER_PISTOL)

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

#endif//#ifndef __WEAPONS_H__
