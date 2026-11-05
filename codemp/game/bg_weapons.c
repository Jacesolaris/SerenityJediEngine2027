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

/// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// ///
///																																///
///																																///
///													SERENITY JEDI ENGINE														///
///										          LIGHTSABER COMBAT SYSTEM													    ///
///																																///
///						      System designed by Serenity and modded by JaceSolaris. (c) 2026 SJE   		                    ///
///								    https://www.moddb.com/mods/serenityjediengine-20											///
///																																///
/// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// ///

// bg_weapons.c -- part of bg_pmove functionality

#include "qcommon/q_shared.h"
#include "bg_public.h"
#include "bg_local.h"

#ifdef _GAME
#include "g_local.h"
#elif defined(_CGAME)
#include "cgame/cg_local.h"
#endif

// Muzzle point table...
vec3_t WP_muzzlePoint[WP_NUM_WEAPONS] =
{
	//	Fwd,	right,	up.
	{ 0, 0, 0 }, // WP_NONE,
	{ 0, 8, 0 }, // WP_STUN_BATON,
	{ 0, 8, 0 }, // WP_MELEE,
	{ 8, 16, 0 }, // WP_SABER,
	{ 12, 6, -6 }, // WP_BRYAR_PISTOL,
	{ 12, 6, -6 }, // WP_BLASTER,
	{ 12, 6, -6 }, // WP_DISRUPTOR,
	{ 12, 2, -6 }, // WP_BOWCASTER,
	{ 12, 4.5, -6 }, // WP_REPEATER,
	{ 12, 6, -6 }, // WP_DEMP2,
	{ 12, 6, -6 }, // WP_FLECHETTE,
	{ 12, 8, -4 }, // WP_ROCKET_LAUNCHER,
	{ 12, 0, -4 }, // WP_THERMAL,
	{ 12, 0, -10 }, // WP_TRIP_MINE,
	{ 12, 0, -4 }, // WP_DET_PACK,
	{ 12, 6, -6 }, // WP_CONCUSSION
	{ 12, 8, -6 }, // WP_DROIDEKA (right arm)
	{ 12, 6, -6 }, // WP_Z6_ROTARY_CANNON
	{ 12, 6, -6 }, // WP_NOGHRI_STICK
	{ 12, 6, -6 }, // WP_TUSKEN_RIFLE
	{ 12, 6, -6 }, // WP_TUSKEN_STAFF
	{ 12, 6, -6 }, // WP_BRYAR_OLD,
};

vec3_t WP_muzzlePoint2[WP_NUM_WEAPONS] =
{
	//	Fwd,	right,	up.
	{ 0, 0, 0 }, // WP_NONE,
	{ 0, 8, 0 }, // WP_STUN_BATON,
	{ 0, 8, 0 }, // WP_MELEE,
	{ 8, 16, 0 }, // WP_SABER,
	{ 12, -6, -6 }, // WP_BRYAR_PISTOL,
	{ 12, -6, -6 }, // WP_BLASTER,
	{ 12, -6, -6 }, // WP_DISRUPTOR,
	{ 12, -8, -6 }, // WP_BOWCASTER,
	{ 12, -6.5, -6 }, // WP_REPEATER,
	{ 12, -7, -6 }, // WP_DEMP2,
	{ 12, -8, -6 }, // WP_FLECHETTE,
	{ 12, -8, -4 }, // WP_ROCKET_LAUNCHER,
	{ 12, -5.5, -4 }, // WP_THERMAL,
	{ 12, 0, -10 }, // WP_TRIP_MINE,
	{ 12, 0, -4 }, // WP_DET_PACK,
	{ 12, -6, -6 }, // WP_CONCUSSION
	{ 12, -8, -6 }, // WP_DROIDEKA (left arm)
	{ 12, -6, -6 }, // WP_Z6_ROTARY_CANNON
	{ 12, -6, -6 }, // WP_NOGHRI_STICK
	{ 12, -6, -6 }, // WP_TUSKEN_RIFLE
	{ 12, -6, -6 }, // WP_TUSKEN_STAFF
	{ 12, -6, -6 }, // WP_BRYAR_OLD,
};

weaponData_t weaponData[WP_NUM_WEAPONS] =
{
	{
		// WP_NONE
		//		"No Weapon",			//	char	classname[32];		// Spawning name
		AMMO_NONE, //	int		ammoIndex;			// Index to proper ammo slot
		0, //	int		ammoLow;			// Count when ammo is low
		0, //	int		energyPerShot;		// Amount of energy used per shot
		0, //	int		fireTime;			// Amount of time between firings
		0, //	int		range;				// Range of weapon
		0, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		0, //	int		altFireTime;		// Amount of time between alt-firings
		0, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_STUN_BATON
		//		"Stun Baton",			//	char	classname[32];		// Spawning name
		AMMO_NONE, //	int		ammoIndex;			// Index to proper ammo slot
		5, //	int		ammoLow;			// Count when ammo is low
		0, //	int		energyPerShot;		// Amount of energy used per shot
		400, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		0, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		3000, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_MELEE
		//		"Melee",			//	char	classname[32];		// Spawning name
		AMMO_NONE, //	int		ammoIndex;			// Index to proper ammo slot
		5, //	int		ammoLow;			// Count when ammo is low
		0, //	int		energyPerShot;		// Amount of energy used per shot
		400, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		0, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		400, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_SABER,
		//		"Lightsaber",			//	char	classname[32];		// Spawning name
		AMMO_NONE, //	int		ammoIndex;			// Index to proper ammo slot
		5, //	int		ammoLow;			// Count when ammo is low
		0, //	int		energyPerShot;		// Amount of energy used per shot
		100, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		0, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		100, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_BRYAR_PISTOL,
		//		"Bryar Pistol",			//	char	classname[32];		// Spawning name
		AMMO_BLASTER, //	int		ammoIndex;			// Index to proper ammo slot
		0, //15,						//	int		ammoLow;			// Count when ammo is low
		0, //2,						//	int		energyPerShot;		// Amount of energy used per shot
		800, //400,					//	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		0, //2,						//	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		800, //400,					//	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //200,					//	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //1,						//int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0, //1500					//	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_BLASTER
		//		"E11 Blaster Rifle",	//	char	classname[32];		// Spawning name
		AMMO_BLASTER, //	int		ammoIndex;			// Index to proper ammo slot
		5, //	int		ammoLow;			// Count when ammo is low
		2, //	int		energyPerShot;		// Amount of energy used per shot
		350, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		3, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		150, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_DISRUPTOR
		//		"Tenloss Disruptor Rifle",//	char	classname[32];		// Spawning name
		AMMO_POWERCELL, //	int		ammoIndex;			// Index to proper ammo slot
		5, //	int		ammoLow;			// Count when ammo is low
		5, //	int		energyPerShot;		// Amount of energy used per shot
		600, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		6, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		1300, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		200, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		3, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		1700 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_BOWCASTER
		//		"Wookiee Bowcaster",		//	char	classname[32];		// Spawning name
		AMMO_POWERCELL, //	int		ammoIndex;			// Index to proper ammo slot
		5, //	int		ammoLow;			// Count when ammo is low
		5, //	int		energyPerShot;		// Amount of energy used per shot
		1000, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		5, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		750, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		400, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		5, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		1700, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_REPEATER
		//		"Imperial Heavy Repeater",//	char	classname[32];		// Spawning name
		AMMO_METAL_BOLTS, //	int		ammoIndex;			// Index to proper ammo slot
		5, //	int		ammoLow;			// Count when ammo is low
		1, //	int		energyPerShot;		// Amount of energy used per shot
		100, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		15, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		800, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_DEMP2
		//		"DEMP2",				//	char	classname[32];		// Spawning name
		AMMO_POWERCELL, //	int		ammoIndex;			// Index to proper ammo slot
		5, //	int		ammoLow;			// Count when ammo is low
		8, //	int		energyPerShot;		// Amount of energy used per shot
		500, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		6, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		900, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		250, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		3, //	int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		2100 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_FLECHETTE
		//		"Golan Arms Flechette",	//	char	classname[32];		// Spawning name
		AMMO_METAL_BOLTS, //	int		ammoIndex;			// Index to proper ammo slot
		5, //	int		ammoLow;			// Count when ammo is low
		10, //	int		energyPerShot;		// Amount of energy used per shot
		700, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		15, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		800, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_ROCKET_LAUNCHER
		//		"Merr-Sonn Missile System",	//	char	classname[32];		// Spawning name
		AMMO_ROCKETS, //	int		ammoIndex;			// Index to proper ammo slot
		5, //	int		ammoLow;			// Count when ammo is low
		1, //	int		energyPerShot;		// Amount of energy used per shot
		900, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		2, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		1200, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_THERMAL
		//		"Thermal Detonator",	//	char	classname[32];		// Spawning name
		AMMO_THERMAL, //	int		ammoIndex;			// Index to proper ammo slot
		0, //	int		ammoLow;			// Count when ammo is low
		1, //	int		energyPerShot;		// Amount of energy used per shot
		800, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		1, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		400, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_TRIP_MINE
		//		"Trip Mine",			//	char	classname[32];		// Spawning name
		AMMO_TRIPMINE, //	int		ammoIndex;			// Index to proper ammo slot
		0, //	int		ammoLow;			// Count when ammo is low
		1, //	int		energyPerShot;		// Amount of energy used per shot
		800, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		1, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		400, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_DET_PACK
		//		"Det Pack",				//	char	classname[32];		// Spawning name
		AMMO_DETPACK, //	int		ammoIndex;			// Index to proper ammo slot
		0, //	int		ammoLow;			// Count when ammo is low
		1, //	int		energyPerShot;		// Amount of energy used per shot
		800, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		0, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		400, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_CONCUSSION
		//		"Concussion Rifle",		//	char	classname[32];		// Spawning name
		AMMO_METAL_BOLTS, //	int		ammoIndex;			// Index to proper ammo slot
		40, //	int		ammoLow;			// Count when ammo is low
		40, //	int		energyPerShot;		// Amount of energy used per shot
		800, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		50, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		1200, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //	int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_DROIDEKA (from MovieDuels SP's weapons.dat)
		//		"Droideka",		//	char	classname[32];		// Spawning name
		AMMO_BLASTER, //	int		ammoIndex;			// Index to proper ammo slot
		15, //	int		ammoLow;			// Count when ammo is low
		0, //	int		energyPerShot;		// Amount of energy used per shot
		325, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		0, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		250, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //	int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_Z6_ROTARY_CANNON (from MovieDuels MP)
		//		"Z-6 Rotary Cannon",	//	char	classname[32];		// Spawning name
		AMMO_METAL_BOLTS, //	int		ammoIndex;			// Index to proper ammo slot
		15, //	int		ammoLow;			// Count when ammo is low
		5, //	int		energyPerShot;		// Amount of energy used per shot
		300, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		5, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		150, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //	int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_NOGHRI_STICK (from SJE SP's weapons.dat; the alt fire is the stick swing)
		//		"Noghri Stick",	//	char	classname[32];		// Spawning name
		AMMO_POWERCELL, //	int		ammoIndex;			// Index to proper ammo slot
		5, //	int		ammoLow;			// Count when ammo is low
		1, //	int		energyPerShot;		// Amount of energy used per shot
		600, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		0, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		1000, //	int		altFireTime;		// Amount of time between alt-firings
		64, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //	int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_TUSKEN_RIFLE (from SJE SP's weapons.dat; the primary fire shoots, the alt fire is the butt bash and costs no ammo)
		//		"Tusken Rifle",	//	char	classname[32];		// Spawning name
		AMMO_POWERCELL, //	int		ammoIndex;			// Index to proper ammo slot
		15, //	int		ammoLow;			// Count when ammo is low
		3, //	int		energyPerShot;		// Amount of energy used per shot
		1000, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		0, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		600, //	int		altFireTime;		// Amount of time between alt-firings
		64, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //	int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_TUSKEN_STAFF (from SJE SP's weapons.dat: no ammo)
		//		"Tusken Staff",	//	char	classname[32];		// Spawning name
		AMMO_NONE, //	int		ammoIndex;			// Index to proper ammo slot
		5, //	int		ammoLow;			// Count when ammo is low
		0, //	int		energyPerShot;		// Amount of energy used per shot
		1000, //	int		fireTime;			// Amount of time between firings
		78, //	int		range;				// Range of weapon
		0, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		1000, //	int		altFireTime;		// Amount of time between alt-firings
		96, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //	int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_BRYAR_OLD,
		//		"Bryar Pistol",			//	char	classname[32];		// Spawning name
		AMMO_BLASTER, //	int		ammoIndex;			// Index to proper ammo slot
		15, //	int		ammoLow;			// Count when ammo is low
		2, //	int		energyPerShot;		// Amount of energy used per shot
		200, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		2, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		200, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		200, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		1, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		1500 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_EMPLCACED_GUN
		//		"Emplaced Gun",			//	char	classname[32];		// Spawning name
		/*AMMO_BLASTER*/0, //	int		ammoIndex;			// Index to proper ammo slot
		/*5*/0, //	int		ammoLow;			// Count when ammo is low
		/*2*/0, //	int		energyPerShot;		// Amount of energy used per shot
		100, //	int		fireTime;			// Amount of time between firings
		8192, //	int		range;				// Range of weapon
		/*3*/0, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		100, //	int		altFireTime;		// Amount of time between alt-firings
		8192, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	},
	{
		// WP_TURRET - NOTE NOT ACTUALLY USEABLE BY PLAYER!
		//		"Emplaced Gun",			//	char	classname[32];		// Spawning name
		/*AMMO_BLASTER*/0, //	int		ammoIndex;			// Index to proper ammo slot
		/*5*/0, //	int		ammoLow;			// Count when ammo is low
		/*2*/0, //	int		energyPerShot;		// Amount of energy used per shot
		0, //	int		fireTime;			// Amount of time between firings
		0, //	int		range;				// Range of weapon
		/*3*/0, //	int		altEnergyPerShot;	// Amount of energy used for alt-fire
		0, //	int		altFireTime;		// Amount of time between alt-firings
		0, //	int		altRange;			// Range of alt-fire
		0, //	int		chargeSubTime;		// ms interval for subtracting ammo during charge
		0, //	int		altChargeSubTime;	// above for secondary
		0, //	int		chargeSub;			// amount to subtract during charge on each interval
		0, //int		altChargeSub;		// above for secondary
		0, //	int		maxCharge;			// stop subtracting once charged for this many ms
		0 //	int		altMaxCharge;		// above for secondary
	}
};

ammoData_t ammoData[AMMO_MAX] =
{
	{
		// AMMO_NONE
		//		"",				//	char	icon[32];	// Name of ammo icon file
		0 //	int		max;		// Max amount player can hold of ammo
	},
	{
		// AMMO_FORCE
		//		"",				//	char	icon[32];	// Name of ammo icon file
		100 //	int		max;		// Max amount player can hold of ammo
	},
	{
		// AMMO_BLASTER
		//		"",				//	char	icon[32];	// Name of ammo icon file
		300 //	int		max;		// Max amount player can hold of ammo
	},
	{
		// AMMO_POWERCELL
		//		"",				//	char	icon[32];	// Name of ammo icon file
		300 //	int		max;		// Max amount player can hold of ammo
	},
	{
		// AMMO_METAL_BOLTS
		//		"",				//	char	icon[32];	// Name of ammo icon file
		400 //	int		max;		// Max amount player can hold of ammo
	},
	{
		// AMMO_ROCKETS
		//		"",				//	char	icon[32];	// Name of ammo icon file
		5 //	int		max;		// Max amount player can hold of ammo
	},
	{
		// AMMO_EMPLACED
		//		"",				//	char	icon[32];	// Name of ammo icon file
		800 //	int		max;		// Max amount player can hold of ammo
	},
	{
		// AMMO_THERMAL
		//		"",				//	char	icon[32];	// Name of ammo icon file
		5 //	int		max;		// Max amount player can hold of ammo
	},
	{
		// AMMO_TRIPMINE
		//		"",				//	char	icon[32];	// Name of ammo icon file
		10 //	int		max;		// Max amount player can hold of ammo
	},
	{
		// AMMO_DETPACK
		//		"",				//	char	icon[32];	// Name of ammo icon file
		10 //	int		max;		// Max amount player can hold of ammo
	}
};

// ---------------------------------------------------------------------------------------------------------------
// Weapon variants (2026-10-31, from Jedi Knight Galaxies): see bg_weapons.h. Loaded by the game and the cgame from
// the same file, so the indexes (the order in the file) match on both sides.
// ---------------------------------------------------------------------------------------------------------------
weaponVariant_t bgWeaponVariants[MAX_WEAPON_VARIANTS];
int bgNumWeaponVariants = 1;

static const struct
{
	const char* name;
	int weapon;
} wvWeaponNames[] =
{
	{ "WP_BRYAR_PISTOL", WP_BRYAR_PISTOL },
	{ "WP_BLASTER", WP_BLASTER },
	{ "WP_DISRUPTOR", WP_DISRUPTOR },
	{ "WP_BOWCASTER", WP_BOWCASTER },
	{ "WP_REPEATER", WP_REPEATER },
	{ "WP_DEMP2", WP_DEMP2 },
	{ "WP_FLECHETTE", WP_FLECHETTE },
	{ "WP_ROCKET_LAUNCHER", WP_ROCKET_LAUNCHER },
	{ "WP_CONCUSSION", WP_CONCUSSION },
	{ "WP_THERMAL", WP_THERMAL },
};

// the pistols share their variants (the old bryar WP_BRYAR_OLD uses the bryar pistol's)
int BG_WeaponVariantFamily(const int weapon)
{
	if (weapon == WP_BRYAR_OLD)
	{
		return WP_BRYAR_PISTOL;
	}
	return weapon;
}

const weaponVariant_t* BG_GetWeaponVariant(const int variant, const int weapon)
{
	if (variant <= 0 || variant >= bgNumWeaponVariants || weapon <= WP_NONE || weapon == WP_SABER)
	{
		return NULL;
	}
	if (bgWeaponVariants[variant].baseWeapon != BG_WeaponVariantFamily(weapon))
	{
		return NULL;
	}
	return &bgWeaponVariants[variant];
}

int BG_WeaponVariantByName(const char* name)
{
	if (!name || !name[0])
	{
		return 0;
	}
	for (int i = 1; i < bgNumWeaponVariants; i++)
	{
		if (!Q_stricmp(bgWeaponVariants[i].name, name))
		{
			return i;
		}
	}
	return 0;
}

int BG_WeaponVariantCount(const int weapon)
{
	int n = 0;
	for (int i = 1; i < bgNumWeaponVariants; i++)
	{
		if (bgWeaponVariants[i].baseWeapon == BG_WeaponVariantFamily(weapon) && bgWeaponVariants[i].npcUse)
		{
			n++;
		}
	}
	return n;
}

int BG_WeaponVariantFireTime(const int variant, const int weapon, const int fire_time, const qboolean alt_fire)
{
	const weaponVariant_t* wv = BG_GetWeaponVariant(variant, weapon);

	if (!wv || fire_time <= 0)
	{
		return fire_time;
	}
	const int t = (int)(fire_time * wv->fire[alt_fire ? 1 : 0].fireTimeScale);
	return t < 50 ? 50 : t;
}

// AI clips and reloads (2026-10-31, see bg_weapons.h): the shots an NPC/bot fires before it must reload
int BG_WeaponClipShots(const int variant, const int weapon)
{
	const weaponVariant_t* wv = BG_GetWeaponVariant(variant, weapon);

	if (wv && wv->clipShots > 0)
	{
		return wv->clipShots;
	}
	switch (weapon)
	{
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
		return 16;
	case WP_BLASTER:
		return 30;
	case WP_DISRUPTOR:
		return 8;
	case WP_BOWCASTER:
		return 15;
	case WP_REPEATER:
		return 50;
	case WP_DEMP2:
		return 20;
	case WP_FLECHETTE:
		return 10;
	case WP_ROCKET_LAUNCHER:
		return 2;
	case WP_CONCUSSION:
		return 6;
	case WP_Z6_ROTARY_CANNON:
		return 100;
	default:
		return 0; // no clip: melee, sabers, grenades, mines, droid and vehicle guns
	}
}

// AI clips and reloads: how long the reload takes, ms
int BG_WeaponReloadTime(const int variant, const int weapon)
{
	const weaponVariant_t* wv = BG_GetWeaponVariant(variant, weapon);

	if (wv && wv->reloadTime > 0)
	{
		return wv->reloadTime;
	}
	switch (weapon)
	{
	case WP_BRYAR_PISTOL:
	case WP_BRYAR_OLD:
		return 1200;
	case WP_DISRUPTOR:
	case WP_FLECHETTE:
		return 2200;
	case WP_REPEATER:
	case WP_ROCKET_LAUNCHER:
		return 3000;
	case WP_CONCUSSION:
		return 2500;
	case WP_Z6_ROTARY_CANNON:
		return 3500;
	default:
		return 2000;
	}
}

// grenades (2026-10-31): the "grenade" key of a WP_THERMAL variant
// the shop (2026-10-31): what a variant costs (grenades: a pack of 3)
int BG_WeaponVariantPrice(const int variant)
{
	if (variant <= 0 || variant >= bgNumWeaponVariants)
	{
		return 0;
	}
	if (bgWeaponVariants[variant].price > 0)
	{
		return bgWeaponVariants[variant].price;
	}
	return bgWeaponVariants[variant].baseWeapon == WP_THERMAL ? 150 : 300;
}

int BG_GrenadeKindByName(const char* name)
{
	static const char* kinds[GK_NUM_KINDS] = { "frag", "fire", "freeze", "stun", "flash", "smoke", "poison", "heal" };

	for (int i = 0; name && i < GK_NUM_KINDS; i++)
	{
		if (!Q_stricmp(name, kinds[i]))
		{
			return i;
		}
	}
	return GK_FRAG;
}

#if defined(_GAME) || defined(_CGAME)
#define MAX_WEAPON_VARIANT_FILE (1024 * 1024)

static float WV_ClampScale(const float f)
{
	if (f < 0.25f)
	{
		return 0.25f;
	}
	if (f > 4.0f)
	{
		return 4.0f;
	}
	return f;
}

// one "key value" line of a variant block; alt = the alt fire's values ("alt" in front of the key)
static void WV_ParseKey(weaponVariant_t* wv, const char* key, const char* value, qboolean* has_alt)
{
	weaponVariantFire_t* fire = &wv->fire[0];

	if (!Q_stricmpn(key, "alt", 3))
	{
		fire = &wv->fire[1];
		key += 3;
		*has_alt = qtrue;
	}

	if (!Q_stricmp(key, "name"))
	{
		Q_strncpyz(wv->displayName, value, sizeof wv->displayName);
	}
	else if (!Q_stricmp(key, "base"))
	{
		for (int i = 0; i < (int)ARRAY_LEN(wvWeaponNames); i++)
		{
			if (!Q_stricmp(wvWeaponNames[i].name, value))
			{
				wv->baseWeapon = wvWeaponNames[i].weapon;
				break;
			}
		}
	}
	else if (!Q_stricmp(key, "npc"))
	{
		wv->npcUse = atoi(value) ? qtrue : qfalse;
	}
	else if (!Q_stricmp(key, "worldmodel"))
	{
		Q_strncpyz(wv->worldModel, value, sizeof wv->worldModel);
	}
	else if (!Q_stricmp(key, "icon"))
	{
		Q_strncpyz(wv->icon, value, sizeof wv->icon);
	}
	else if (!Q_stricmp(key, "selectsound"))
	{
		Q_strncpyz(wv->selectSound, value, sizeof wv->selectSound);
	}
	else if (!Q_stricmp(key, "firesound"))
	{
		if (fire->numSounds < MAX_WEAPON_VARIANT_SOUNDS)
		{
			Q_strncpyz(fire->sound[fire->numSounds], value, sizeof fire->sound[0]);
			fire->numSounds++;
		}
	}
	else if (!Q_stricmp(key, "muzzlefx"))
	{
		Q_strncpyz(fire->muzzleFx, value, sizeof fire->muzzleFx);
	}
	else if (!Q_stricmp(key, "shotfx"))
	{
		Q_strncpyz(fire->shotFx, value, sizeof fire->shotFx);
	}
	else if (!Q_stricmp(key, "shotmodel"))
	{
		Q_strncpyz(fire->shotModel, value, sizeof fire->shotModel);
	}
	else if (!Q_stricmp(key, "shotsound"))
	{
		Q_strncpyz(fire->shotSound, value, sizeof fire->shotSound);
	}
	else if (!Q_stricmp(key, "missfx"))
	{
		Q_strncpyz(fire->missFx, value, sizeof fire->missFx);
	}
	else if (!Q_stricmp(key, "hitfx"))
	{
		Q_strncpyz(fire->hitFx, value, sizeof fire->hitFx);
	}
	else if (!Q_stricmp(key, "damagescale"))
	{
		fire->damageScale = WV_ClampScale(atof(value));
	}
	else if (!Q_stricmp(key, "firetimescale"))
	{
		fire->fireTimeScale = WV_ClampScale(atof(value));
	}
	else if (!Q_stricmp(key, "speedscale"))
	{
		fire->speedScale = WV_ClampScale(atof(value));
	}
	else if (!Q_stricmp(key, "clip"))
	{
		wv->clipShots = atoi(value) < 0 ? 0 : atoi(value) > 500 ? 500 : atoi(value);
	}
	else if (!Q_stricmp(key, "reload"))
	{
		wv->reloadTime = atoi(value) < 0 ? 0 : atoi(value) > 10000 ? 10000 : atoi(value);
	}
	else if (!Q_stricmp(key, "grenade"))
	{
		wv->grenadeKind = BG_GrenadeKindByName(value);
	}
	else if (!Q_stricmp(key, "radius"))
	{
		wv->radiusScale = WV_ClampScale(atof(value));
	}
	else if (!Q_stricmp(key, "effecttime"))
	{
		wv->effectTime = atoi(value) < 0 ? 0 : atoi(value) > 30000 ? 30000 : atoi(value);
	}
	else if (!Q_stricmp(key, "price"))
	{
		wv->price = atoi(value) < 0 ? 0 : atoi(value) > 30000 ? 30000 : atoi(value);
	}
}

void BG_LoadWeaponVariants(void)
{
	static char buf[MAX_WEAPON_VARIANT_FILE];
	fileHandle_t f = 0;

	memset(bgWeaponVariants, 0, sizeof bgWeaponVariants);
	bgNumWeaponVariants = 1;

	const int len = trap->FS_Open(WEAPON_VARIANT_FILE, &f, FS_READ);
	if (!f || len <= 0)
	{
		if (f)
		{
			trap->FS_Close(f);
		}
		return; // no file: stock weapons only
	}
	if (len >= MAX_WEAPON_VARIANT_FILE)
	{
		trap->FS_Close(f);
		Com_Printf(S_COLOR_YELLOW "WARNING: %s is too big (%d bytes, max %d)\n", WEAPON_VARIANT_FILE, len, MAX_WEAPON_VARIANT_FILE);
		return;
	}
	trap->FS_Read(buf, len, f);
	trap->FS_Close(f);
	buf[len] = 0;

	const char* p = buf;
	COM_BeginParseSession(WEAPON_VARIANT_FILE);

	while (p)
	{
		char name[64];
		const char* token = COM_ParseExt(&p, qtrue);

		if (!token[0])
		{
			break;
		}
		Q_strncpyz(name, token, sizeof name);

		token = COM_ParseExt(&p, qtrue);
		if (Q_stricmp(token, "{"))
		{
			Com_Printf(S_COLOR_YELLOW "WARNING: %s: '{' expected after %s\n", WEAPON_VARIANT_FILE, name);
			break;
		}

		if (bgNumWeaponVariants >= MAX_WEAPON_VARIANTS)
		{
			Com_Printf(S_COLOR_YELLOW "WARNING: %s: more than %d variants, the rest are skipped\n", WEAPON_VARIANT_FILE, MAX_WEAPON_VARIANTS - 1);
			SkipBracedSection(&p, 1);
			break;
		}

		weaponVariant_t* wv = &bgWeaponVariants[bgNumWeaponVariants];
		qboolean has_alt = qfalse;

		memset(wv, 0, sizeof * wv);
		Q_strncpyz(wv->name, name, sizeof wv->name);
		Q_strncpyz(wv->displayName, name, sizeof wv->displayName);
		wv->npcUse = qtrue;
		wv->fire[0].damageScale = wv->fire[0].fireTimeScale = wv->fire[0].speedScale = 1.0f;
		wv->radiusScale = 1.0f; // grenades: the stock blast radius
		wv->fire[1].damageScale = wv->fire[1].fireTimeScale = wv->fire[1].speedScale = 0.0f; // 0 = take the primary's

		while (1)
		{
			char key[64];

			token = COM_ParseExt(&p, qtrue);
			if (!token[0])
			{
				break;
			}
			if (!Q_stricmp(token, "}"))
			{
				break;
			}
			Q_strncpyz(key, token, sizeof key);
			token = COM_ParseExt(&p, qfalse);
			WV_ParseKey(wv, key, token, &has_alt);
		}

		// the alt fire: whatever the file left empty comes from the primary
		{
			weaponVariantFire_t* alt = &wv->fire[1];
			const weaponVariantFire_t* pri = &wv->fire[0];

			if (!alt->numSounds)
			{
				memcpy(alt->sound, pri->sound, sizeof alt->sound);
				alt->numSounds = pri->numSounds;
			}
			if (!alt->muzzleFx[0]) Q_strncpyz(alt->muzzleFx, pri->muzzleFx, sizeof alt->muzzleFx);
			if (!alt->shotFx[0]) Q_strncpyz(alt->shotFx, pri->shotFx, sizeof alt->shotFx);
			if (!alt->shotModel[0]) Q_strncpyz(alt->shotModel, pri->shotModel, sizeof alt->shotModel);
			if (!alt->shotSound[0]) Q_strncpyz(alt->shotSound, pri->shotSound, sizeof alt->shotSound);
			if (!alt->missFx[0]) Q_strncpyz(alt->missFx, pri->missFx, sizeof alt->missFx);
			if (!alt->hitFx[0]) Q_strncpyz(alt->hitFx, pri->hitFx, sizeof alt->hitFx);
			if (alt->damageScale <= 0.0f) alt->damageScale = pri->damageScale;
			if (alt->fireTimeScale <= 0.0f) alt->fireTimeScale = pri->fireTimeScale;
			if (alt->speedScale <= 0.0f) alt->speedScale = pri->speedScale;
		}
		(void)has_alt;

		if (wv->baseWeapon <= WP_NONE || wv->baseWeapon == WP_SABER || !wv->worldModel[0])
		{
			Com_Printf(S_COLOR_YELLOW "WARNING: %s: variant %s has no gun base weapon or no worldmodel, skipped\n", WEAPON_VARIANT_FILE, name);
			continue;
		}
		bgNumWeaponVariants++;
	}

	Com_Printf("Weapon variants: %d loaded from %s\n", bgNumWeaponVariants - 1, WEAPON_VARIANT_FILE);
}
#else
void BG_LoadWeaponVariants(void)
{}
#endif