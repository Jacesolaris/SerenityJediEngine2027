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

#include "g_local.h"
#include "b_local.h"
#include "g_functions.h"
#include "wp_saber.h"
#include "w_local.h"

//---------------------------------------------------------------------------------------------------------------------
// Grenades (2026-10-31, DH: the SJE grenades and grenade cooking, SJE SP + MP; see grenadeKind_t in weapons.h)
//
// Grenade types are WP_THERMAL weapon variants (ext_data/sje_weaponvariants.dat, "grenade <kind>"). The type is picked
// when the grenade is thrown (G_GrenadeVariant): the player's pick (grenade command), an NPC's own (by its NPC type,
// some keep the stock thermal). The thrown grenade carries it in s.weaponVariant (its own model and blast effect), and
// G_GrenadeBlast does the blast: damage, radius and a status effect on everyone in reach (fire, poison, freeze, stun,
// flash, smoke, bacta heal). g_weaponVariants 0 = stock thermals only.
//
// Cooking (g_grenadeCook 1, the player): hold the throw past a full wind-up (TD_VELOCITY ms) and the fuse starts
// burning in the hand (the pin sound). The thrown grenade blows that much sooner; the warning beep comes in the hand,
// and held too long it goes off in the hand. Quick and full-strength throws keep the old 2 s fuse.
//---------------------------------------------------------------------------------------------------------------------
extern cvar_t* g_weaponVariants;
extern cvar_t* g_grenadeCook;
extern void CG_GrenadeFlash(int duration);
extern void player_Freeze(const gentity_t* self);
extern qboolean G_ControlledByPlayer(const gentity_t* self);
extern Vehicle_t* G_IsRidingVehicle(const gentity_t* pEnt);

#define GRENADE_WARN_TIME	800		// the warning beep before the blast (thermalDetonatorExplode)
#define GRENADE_DOT_TICK	500		// burn / poison damage every ...ms

// default ms of each kind's status effect (the variant's "effecttime" overrides), and its share of the thermal's damage
static const int grenadeKindTime[GK_NUM_KINDS] = { 0, 5000, 4000, 3000, 4000, 0, 8000, 0 };
static const float grenadeKindDamage[GK_NUM_KINDS] = { 1.0f, 0.6f, 0.1f, 0.3f, 0.1f, 0.0f, 0.15f, 0.0f };

static unsigned G_GrenadeHash(const char* name)
{
	unsigned h = 5381;

	while (name && *name)
	{
		const unsigned char c = static_cast<unsigned char>(*name);
		h = h * 33 + static_cast<unsigned>(c >= 'A' && c <= 'Z' ? c + 32 : c);
		name++;
	}
	return h;
}

// the grenade type someone throws, a WP_THERMAL variant; 0 = the stock thermal
static int G_GrenadeVariant(const gentity_t* ent)
{
	if (!ent || !ent->client || !g_weaponVariants->integer)
	{
		return 0;
	}
	if (BG_GetWeaponVariant(ent->client->ps.weaponVariant, WP_THERMAL))
	{
		return ent->client->ps.weaponVariant; // the weaponvariant cheat on a thermal
	}
	if (!ent->NPC || ent->s.number == 0)
	{
		return BG_GetWeaponVariant(ent->grenadeKind, WP_THERMAL) ? ent->grenadeKind : 0; // the player's pick
	}
	if (ent->client->ps.weapon == WP_SABER || BG_HasWeapon(&ent->client->ps, WP_SABER))
	{
		return 0; // saber users never (DH)
	}

	// an NPC: its own type, by NPC type and entity number; one slot more than there are types = the stock thermal
	const int n = BG_WeaponVariantCount(WP_THERMAL);
	if (!n)
	{
		return 0;
	}
	int pick = static_cast<int>((G_GrenadeHash(ent->NPC_type) + static_cast<unsigned>(ent->s.number) * 7u) % static_cast<unsigned>(n + 1));
	for (int v = 1; v < bgNumWeaponVariants; v++)
	{
		if (bgWeaponVariants[v].baseWeapon == WP_THERMAL && bgWeaponVariants[v].npcUse && pick-- == 0)
		{
			return v;
		}
	}
	return 0;
}

static qboolean G_GrenadeIsDroid(const gentity_t* ent)
{
	switch (ent->client->NPC_class)
	{
	case CLASS_ATST:
	case CLASS_GONK:
	case CLASS_INTERROGATOR:
	case CLASS_MARK1:
	case CLASS_MARK2:
	case CLASS_MOUSE:
	case CLASS_PROBE:
	case CLASS_PROTOCOL:
	case CLASS_R2D2:
	case CLASS_R5D2:
	case CLASS_REMOTE:
	case CLASS_SEEKER:
	case CLASS_SENTRY:
	case CLASS_SBD:
	case CLASS_BATTLEDROID:
	case CLASS_DROIDEKA:
	case CLASS_ASSASSIN_DROID:
	case CLASS_SABER_DROID:
	case CLASS_VEHICLE:
		return qtrue;
	default:
		return qfalse;
	}
}

// fire or poison: damage every GRENADE_DOT_TICK ms until the time runs out (a new dose restarts the clock)
static void G_GrenadeDot(gentity_t* victim, const gentity_t* attacker, const int kind, const int time, const int damage)
{
	victim->grenadeDotKind = kind;
	victim->grenadeDotEnd = level.time + time;
	victim->grenadeDotDamage = damage;
	victim->grenadeDotAttacker = attacker ? attacker->s.number : ENTITYNUM_WORLD;
	if (victim->grenadeDotNext < level.time)
	{
		victim->grenadeDotNext = level.time + GRENADE_DOT_TICK;
	}
}

// what a grenade type does to someone in its blast (alive, in sight of it)
static void G_GrenadeStatus(gentity_t* victim, gentity_t* attacker, const int kind, const int time, const vec3_t org)
{
	gclient_t* cl = victim->client;

	switch (kind)
	{
	case GK_FIRE:
		if (victim->waterlevel < 2 && !G_GrenadeIsDroid(victim))
		{
			G_GrenadeDot(victim, attacker, GK_FIRE, time, 4);
		}
		break;

	case GK_POISON:
		if (!G_GrenadeIsDroid(victim))
		{
			G_GrenadeDot(victim, attacker, GK_POISON, time, 3);
		}
		break;

	case GK_FREEZE:
		if (cl->NPC_class != CLASS_VEHICLE && cl->NPC_class != CLASS_RANCOR && cl->NPC_class != CLASS_ATST
			&& !G_IsRidingVehicle(victim))
		{
			cl->ps.stasisTime = level.time + time; // held like Force Stasis
			VectorClear(cl->ps.velocity);
			player_Freeze(victim);
			G_PlayEffect("cryoban/stuck", victim->currentOrigin);
		}
		break;

	case GK_STUN:
	{
		vec3_t dir;

		VectorSubtract(victim->currentOrigin, org, dir);
		dir[2] = 0.0f;
		VectorNormalize(dir);
		if (!G_GrenadeIsDroid(victim) && cl->NPC_class != CLASS_RANCOR && !G_IsRidingVehicle(victim))
		{
			G_Knockdown(victim, attacker, dir, 300, qtrue);
		}
		cl->ps.powerups[PW_STUNNED] = level.time + time; // NPCs can't fire, the stun look
		cl->ps.powerups[PW_SHOCKED] = level.time + (time < 2000 ? time : 2000);
	}
	break;

	case GK_FLASH:
	{
		// blinded: fully when looking at the blast, a third as long when looking away
		vec3_t fwd, to;

		AngleVectors(cl->ps.viewangles, fwd, nullptr, nullptr);
		VectorSubtract(org, victim->currentOrigin, to);
		VectorNormalize(to);
		const int t = DotProduct(fwd, to) > 0.3f ? time : time / 3;

		if (victim->s.number == 0 || G_ControlledByPlayer(victim))
		{
			CG_GrenadeFlash(t); // the player's screen whites out
		}
		else if (victim->NPC && !G_GrenadeIsDroid(victim))
		{
			victim->NPC->confusionTime = level.time + t; // can't find anyone
			cl->ps.powerups[PW_STUNNED] = level.time + t; // and can't fire
		}
	}
	break;

	case GK_HEAL:
		if (victim == attacker || (attacker && OnSameTeam(victim, attacker)))
		{
			const int max_health = cl->ps.stats[STAT_MAX_HEALTH] > 0 ? cl->ps.stats[STAT_MAX_HEALTH] : victim->max_health;

			victim->health += 50;
			if (victim->health > max_health)
			{
				victim->health = max_health;
			}
			cl->ps.stats[STAT_HEALTH] = victim->health;
			victim->grenadeDotEnd = 0; // bacta cures fire and poison
			G_PlayEffect("force/heal", victim->currentOrigin);
		}
		break;

	default:
		break;
	}
}

// thermalDetonatorExplode: the blast of a grenade type; qfalse = a stock thermal (the old blast)
static qboolean G_GrenadeBlast(gentity_t* ent)
{
	static gentity_t* victims[MAX_GENTITIES];
	const weaponVariant_t* wv = BG_GetWeaponVariant(ent->s.weaponVariant, WP_THERMAL);

	if (!wv)
	{
		return qfalse;
	}
	const int kind = wv->grenadeKind >= 0 && wv->grenadeKind < GK_NUM_KINDS ? wv->grenadeKind : GK_FRAG;
	const float radius = weaponData[WP_THERMAL].splashRadius * wv->radiusScale;
	const int damage = static_cast<int>(weaponData[WP_THERMAL].splashDamage * wv->fire[ent->alt_fire ? 1 : 0].damageScale
		* grenadeKindDamage[kind]);
	const int time = wv->effectTime > 0 ? wv->effectTime : grenadeKindTime[kind];
	gentity_t* attacker = ent->owner;
	vec3_t org;

	VectorSet(org, ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2] + 8);
	ent->takedamage = qfalse; // don't allow double deaths!

	G_PlayEffect(wv->fire[0].missFx[0] ? wv->fire[0].missFx : "thermal/explosion", ent->currentOrigin);
	if (kind == GK_FRAG || kind == GK_FIRE || kind == GK_STUN)
	{
		G_PlayEffect("thermal/shockwave", ent->currentOrigin);
	}
	if (damage > 0)
	{
		G_RadiusDamage(ent->currentOrigin, attacker, damage, radius, nullptr, kind == GK_POISON ? MOD_GAS : MOD_EXPLOSIVE_SPLASH);
	}

	if (kind != GK_FRAG && kind != GK_SMOKE)
	{
		const int count = G_RadiusList(org, radius, ent, qtrue, victims);

		for (int i = 0; i < count; i++)
		{
			gentity_t* victim = victims[i];

			if (victim && victim->client && victim->health > 0 && CanDamage(victim, org))
			{
				G_GrenadeStatus(victim, attacker, kind, time, org);
			}
		}
	}

	G_FreeEntity(ent);
	return qtrue;
}

// how long the player has been cooking the grenade in hand (the hold past a full wind-up), ms; 0 = not cooking
static int G_GrenadeCookTime(const gentity_t* ent)
{
	if (!g_grenadeCook->integer || !ent || !ent->client || ent->s.number != 0 || ent->client->ps.weaponChargeTime <= 0)
	{
		return 0; // the player only: NPCs throw at once
	}
	const int cook = level.time - ent->client->ps.weaponChargeTime - TD_VELOCITY;
	if (cook <= 0)
	{
		return 0;
	}
	return cook < TD_TIME + GRENADE_WARN_TIME ? cook : TD_TIME + GRENADE_WARN_TIME;
}

// held too long: it goes off in the hand (uses up the grenade)
static void G_GrenadeBlowInHand(gentity_t* ent)
{
	gclient_t* cl = ent->client;
	const int ammo_index = weaponData[WP_THERMAL].ammoIndex;

	ent->grenadeCookStage = 0;
	gentity_t* bolt = WP_DropThermal(ent);

	if (cl->ps.ammo[ammo_index] > 0)
	{
		cl->ps.ammo[ammo_index] -= weaponData[WP_THERMAL].energyPerShot > 0 ? weaponData[WP_THERMAL].energyPerShot : 1;
		if (cl->ps.ammo[ammo_index] < 0)
		{
			cl->ps.ammo[ammo_index] = 0;
		}
	}
	cl->ps.weaponstate = WEAPON_READY;
	cl->ps.weaponChargeTime = 0;
	cl->ps.weaponTime = 1000;

	if (bolt)
	{
		VectorClear(bolt->s.pos.trDelta);
		bolt->count = 1; // no second warning
		bolt->e_ThinkFunc = thinkF_thermalDetonatorExplode;
		bolt->nextthink = level.time;
	}
}

// every think (from G_WeaponVariantThink): cooking the grenade in hand, and burning / poisoned people
void G_GrenadeClientThink(gentity_t* ent)
{
	gclient_t* cl = ent->client;

	if (!cl)
	{
		return;
	}

	// cooking
	if (cl->ps.weapon == WP_THERMAL && (cl->ps.weaponstate == WEAPON_CHARGING || cl->ps.weaponstate == WEAPON_CHARGING_ALT)
		&& ent->health > 0)
	{
		const int cook = G_GrenadeCookTime(ent);

		if (cook > 0 && ent->grenadeCookStage < 1)
		{
			ent->grenadeCookStage = 1;
			G_SoundOnEnt(ent, CHAN_AUTO, "sound/weapons/grenade_cook.mp3"); // the pin
		}
		if (cook >= TD_TIME && ent->grenadeCookStage < 2)
		{
			ent->grenadeCookStage = 2;
			G_Sound(ent, G_SoundIndex("sound/weapons/thermal/warning.wav"));
		}
		if (cook >= TD_TIME + GRENADE_WARN_TIME)
		{
			G_GrenadeBlowInHand(ent);
		}
	}
	else
	{
		ent->grenadeCookStage = 0;
	}

	// burning / poisoned
	if (ent->grenadeDotEnd)
	{
		if (ent->health <= 0 || level.time >= ent->grenadeDotEnd || (ent->grenadeDotKind == GK_FIRE && ent->waterlevel >= 2))
		{
			ent->grenadeDotEnd = 0; // over, dead, or water put the fire out
		}
		else if (level.time >= ent->grenadeDotNext)
		{
			gentity_t* attacker = ent->grenadeDotAttacker >= 0 && ent->grenadeDotAttacker < ENTITYNUM_WORLD
				&& g_entities[ent->grenadeDotAttacker].inuse ? &g_entities[ent->grenadeDotAttacker] : nullptr;
			vec3_t org;

			ent->grenadeDotNext = level.time + GRENADE_DOT_TICK;
			VectorSet(org, ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2] + 20);
			G_PlayEffect(ent->grenadeDotKind == GK_FIRE ? "player/fire" : "player/poison", org);
			G_Damage(ent, attacker, attacker, nullptr, ent->currentOrigin, ent->grenadeDotDamage,
				DAMAGE_NO_KNOCKBACK | DAMAGE_NO_ARMOR, ent->grenadeDotKind == GK_FIRE ? MOD_BURNING : MOD_GAS);
		}
	}
}

//---------------------
//	Thermal Detonator
//---------------------

//---------------------------------------------------------
void thermalDetonatorExplode(gentity_t* ent)
//---------------------------------------------------------
{
	const int thermal_effect = Q_irand(0, 3);

	if (ent->s.eFlags & EF_HELD_BY_SAND_CREATURE)
	{
		ent->takedamage = qfalse; // don't allow double deaths!

		G_Damage(ent->activator, ent, ent->owner, vec3_origin, ent->currentOrigin, weaponData[WP_THERMAL].altDamage, 0,
			MOD_EXPLOSIVE);
		G_PlayEffect("thermal/explosion", ent->currentOrigin);
		G_PlayEffect("thermal/shockwave", ent->currentOrigin);

		G_FreeEntity(ent);
	}
	else if (!ent->count)
	{
		G_Sound(ent, G_SoundIndex("sound/weapons/thermal/warning.wav"));
		ent->count = 1;
		ent->nextthink = level.time + 800;
		ent->svFlags |= SVF_BROADCAST; //so everyone hears/sees the explosion?
	}
	else
	{
		vec3_t pos;

		if (G_GrenadeBlast(ent))
		{
			return; // a grenade type (2026-10-31): its own blast and status effect
		}

		VectorSet(pos, ent->currentOrigin[0], ent->currentOrigin[1], ent->currentOrigin[2] + 8);

		ent->takedamage = qfalse; // don't allow double deaths!

		G_RadiusDamage(ent->currentOrigin, ent->owner, weaponData[WP_THERMAL].splashDamage,
			weaponData[WP_THERMAL].splashRadius, nullptr, MOD_EXPLOSIVE_SPLASH);

		switch (thermal_effect)
		{
		case 3:
			G_PlayEffect("thermal/explosion", ent->currentOrigin);
			break;
		case 2:
			G_PlayEffect("cryoban/explosion", ent->currentOrigin);
			break;
		case 1:
			G_PlayEffect("flash/faceflash", ent->currentOrigin);
			break;
		default:
			G_PlayEffect("smoke/smokegrenade", ent->currentOrigin);
			break;
		}

		G_PlayEffect("thermal/shockwave", ent->currentOrigin);

		G_FreeEntity(ent);
	}
}

//-------------------------------------------------------------------------------------------------------------
void thermal_die(gentity_t* self, gentity_t* inflictor, gentity_t* attacker, int damage, int mod, int dflags,
	int hit_loc)
	//-------------------------------------------------------------------------------------------------------------
{
	thermalDetonatorExplode(self);
}

//---------------------------------------------------------
qboolean WP_LobFire(const gentity_t* self, vec3_t start, vec3_t target, vec3_t mins, vec3_t maxs, const int clipmask,
	vec3_t velocity, const qboolean trace_path, const int ignore_ent_num, const int enemy_num,
	float min_speed, float max_speed, float ideal_speed, const qboolean must_hit)
	//---------------------------------------------------------
{
	constexpr float speed_inc = 100;
	float best_impact_dist = Q3_INFINITE; //fireSpeed,
	vec3_t shot_vel, fail_case = { 0.0f };
	trace_t trace;
	trajectory_t tr{};
	int hit_count = 0;
	constexpr int max_hits = 7;

	if (!ideal_speed)
	{
		ideal_speed = 300;
	}
	else if (ideal_speed < speed_inc)
	{
		ideal_speed = speed_inc;
	}
	float shotSpeed = ideal_speed;
	const int skipNum = (ideal_speed - speed_inc) / speed_inc;
	if (!min_speed)
	{
		min_speed = 100;
	}
	if (!max_speed)
	{
		max_speed = 900;
	}
	while (hit_count < max_hits)
	{
		vec3_t target_dir;
		VectorSubtract(target, start, target_dir);
		const float target_dist = VectorNormalize(target_dir);

		VectorScale(target_dir, shotSpeed, shot_vel);
		float travelTime = target_dist / shotSpeed;
		shot_vel[2] += travelTime * 0.5 * g_gravity->value;

		if (!hit_count)
		{
			//save the first (ideal) one as the failCase (fallback value)
			if (!must_hit)
			{
				//default is fine as a return value
				VectorCopy(shot_vel, fail_case);
			}
		}

		if (trace_path)
		{
			vec3_t last_pos;
			constexpr int time_step = 500;
			//do a rough trace of the path
			qboolean blocked = qfalse;

			VectorCopy(start, tr.trBase);
			VectorCopy(shot_vel, tr.trDelta);
			tr.trType = TR_GRAVITY;
			tr.trTime = level.time;
			travelTime *= 1000.0f;
			VectorCopy(start, last_pos);

			//This may be kind of wasteful, especially on long throws... use larger steps?  Divide the travelTime into a certain hard number of slices?  Trace just to apex and down?
			for (int elapsed_time = time_step; elapsed_time < floor(travelTime) + time_step; elapsed_time += time_step)
			{
				vec3_t test_pos;
				if (static_cast<float>(elapsed_time) > travelTime)
				{
					//cap it
					elapsed_time = floor(travelTime);
				}
				EvaluateTrajectory(&tr, level.time + elapsed_time, test_pos);
				gi.trace(&trace, last_pos, mins, maxs, test_pos, ignore_ent_num, clipmask, static_cast<EG2_Collision>(0),
					0);

				if (trace.allsolid || trace.startsolid)
				{
					blocked = qtrue;
					break;
				}
				if (trace.fraction < 1.0f)
				{
					//hit something
					if (trace.entityNum == enemy_num)
					{
						//hit the enemy, that's perfect!
						break;
					}
					if (trace.plane.normal[2] > 0.7 && DistanceSquared(trace.endpos, target) < 4096)
						//hit within 64 of desired location, should be okay
					{
						//close enough!
						break;
					}
					//FIXME: maybe find the extents of this brush and go above or below it on next try somehow?
					const float impact_dist = DistanceSquared(trace.endpos, target);
					if (impact_dist < best_impact_dist)
					{
						best_impact_dist = impact_dist;
						VectorCopy(shot_vel, fail_case);
					}
					blocked = qtrue;
					//see if we should store this as the failCase
					if (trace.entityNum < ENTITYNUM_WORLD)
					{
						//hit an ent
						const gentity_t* traceEnt = &g_entities[trace.entityNum];
						if (traceEnt && traceEnt->takedamage && !OnSameTeam(self, traceEnt))
						{
							//hit something breakable, so that's okay
							//we haven't found a clear shot yet so use this as the failcase
							VectorCopy(shot_vel, fail_case);
						}
					}
					break;
				}
				if (elapsed_time == floor(travelTime))
				{
					//reached end, all clear
					break;
				}
				//all clear, try next slice
				VectorCopy(test_pos, last_pos);
			}
			if (blocked)
			{
				//hit something, adjust speed (which will change arc)
				hit_count++;
				shotSpeed = ideal_speed + (hit_count - skipNum) * speed_inc; //from min to max (skipping ideal)
				if (hit_count >= skipNum)
				{
					//skip ideal since that was the first value we tested
					shotSpeed += speed_inc;
				}
			}
			else
			{
				//made it!
				break;
			}
		}
		else
		{
			//no need to check the path, go with first calc
			break;
		}
	}

	if (hit_count >= max_hits)
	{
		//NOTE: worst case scenario, use the one that impacted closest to the target (or just use the first try...?)
		try
		{
			//assert((failCase[0] + failCase[1] + failCase[2]) > 0.0f);
			VectorCopy(fail_case, velocity);
			return qfalse;
		}
		catch (...)
		{
			gi.Printf(S_COLOR_RED "ERROR: You just tried to crash the game! Shame on you!" S_COLOR_WHITE "\n");
		}
	}
	VectorCopy(shot_vel, velocity);
	return qtrue;
}

//---------------------------------------------------------
void WP_ThermalThink(gentity_t* ent)
//---------------------------------------------------------
{
	qboolean blow = qfalse;

	// Thermal detonators for the player do occasional radius checks and blow up if there are entities in the blast radius
	//	This is done so that the main fire is actually useful as an attack.  We explode anyway after delay expires.

	if (ent->s.eFlags & EF_HELD_BY_SAND_CREATURE)
	{
		//blow once creature is underground (done with anim)
		//FIXME: chance of being spit out?  Especially if lots of delay left...
		ent->e_TouchFunc = touchF_NULL; //don't impact on anything
		if (!ent->activator
			|| !ent->activator->client
			|| !ent->activator->client->ps.legsAnimTimer)
		{
			//either something happened to the sand creature or it's done with it's attack anim
			//blow!
			ent->e_ThinkFunc = thinkF_thermalDetonatorExplode;
			ent->nextthink = level.time + Q_irand(50, 2000);
		}
		else
		{
			//keep checking
			ent->nextthink = level.time + TD_THINK_TIME;
		}
		return;
	}
	if (ent->delay > level.time)
	{
		//	Finally, we force it to bounce at least once before doing the special checks, otherwise it's just too easy for the player?
		if (ent->has_bounced)
		{
			const int count = G_RadiusList(ent->currentOrigin, TD_TEST_RAD, ent, qtrue, ent_list);

			for (int i = 0; i < count; i++)
			{
				if (ent_list[i]->s.number == 0)
				{
					// avoid deliberately blowing up next to the player, no matter how close any enemy is..
					//	...if the delay time expires though, there is no saving the player...muwhaaa haa ha
					blow = qfalse;
					break;
				}
				if (ent_list[i]->client
					&& ent_list[i]->client->NPC_class != CLASS_SAND_CREATURE //ignore sand creatures
					&& ent_list[i]->health > 0)
				{
					//FIXME! sometimes the ent_list order changes, so we should make sure that the player isn't anywhere in this list
					blow = qtrue;
				}
			}
		}
	}
	else
	{
		// our death time has arrived, even if nothing is near us
		blow = qtrue;
	}

	if (blow)
	{
		ent->e_ThinkFunc = thinkF_thermalDetonatorExplode;
		ent->nextthink = level.time + 50;
	}
	else
	{
		// we probably don't need to do this thinking logic very often...maybe this is fast enough?
		ent->nextthink = level.time + TD_THINK_TIME;
	}
}

//---------------------------------------------------------
gentity_t* WP_FireThermalDetonator(gentity_t* ent, const qboolean alt_fire)
//---------------------------------------------------------
{
	if (ent == NULL)
	{
		Com_Printf(S_COLOR_YELLOW "WP_FireThermalDetonator: NULL ent\n");
		return NULL;
	}

	vec3_t dir, start;
	float damageScale = 1.0f;

	VectorCopy(forward_vec, dir);
	VectorCopy(muzzle, start);

	gentity_t* bolt = G_Spawn();
	if (bolt == NULL)
	{
		Com_Printf(S_COLOR_YELLOW "WP_FireThermalDetonator: G_Spawn returned NULL\n");
		return NULL;
	}

	bolt->classname = "thermal_detonator";

	if (ent->s.number != 0)
	{
		damageScale = TD_NPC_DAMAGE_CUT;
	}

	// grenade cooking (2026-10-31): the time the player held it past a full wind-up comes off the fuse
	const int cook = G_GrenadeCookTime(ent);

	if (alt_fire == qfalse && ent->s.number == 0)
	{
		bolt->e_ThinkFunc = thinkF_WP_ThermalThink;
		bolt->nextthink = level.time + TD_THINK_TIME;
		bolt->delay = level.time + TD_TIME - cook;
	}
	else
	{
		bolt->e_ThinkFunc = thinkF_thermalDetonatorExplode;
		bolt->nextthink = level.time + TD_TIME - cook;
	}
	if (cook >= TD_TIME)
	{
		// the warning already beeped in the hand: straight to the blast
		bolt->count = 1;
		bolt->e_ThinkFunc = thinkF_thermalDetonatorExplode;
		bolt->nextthink = level.time + (TD_TIME + GRENADE_WARN_TIME - cook > 50 ? TD_TIME + GRENADE_WARN_TIME - cook : 50);
	}
	if (ent->client)
	{
		ent->grenadeCookStage = 0;
	}

	bolt->mass = 10;

	VectorSet(bolt->mins, -4.0f, -4.0f, -4.0f);
	VectorSet(bolt->maxs, 4.0f, 4.0f, 4.0f);

	bolt->clipmask = MASK_SHOT;
	bolt->clipmask &= ~CONTENTS_CORPSE;
	bolt->contents = CONTENTS_SHOTCLIP;
	bolt->takedamage = qtrue;
	bolt->health = 15;
	bolt->e_DieFunc = dieF_thermal_die;

	WP_TraceSetStart(ent, start);

	float chargeAmount = 1.0f;

	if (ent->client != NULL)
	{
		chargeAmount = level.time - ent->client->ps.weaponChargeTime;
	}

	chargeAmount = chargeAmount / (float)TD_VELOCITY;

	if (chargeAmount > 1.0f)
	{
		chargeAmount = 1.0f;
	}
	else if (chargeAmount < TD_MIN_CHARGE)
	{
		chargeAmount = TD_MIN_CHARGE;
	}

	float thrown_speed = TD_VELOCITY;
	const qboolean this_is_a_shooter =
		(Q_stricmp("misc_weapon_shooter", ent->classname) == 0) ? qtrue : qfalse;

	if (this_is_a_shooter == qtrue)
	{
		if (ent->delay != 0)
		{
			thrown_speed = ent->delay;
		}
	}

	bolt->s.pos.trType = TR_GRAVITY;
	bolt->owner = ent;

	VectorScale(dir, thrown_speed * chargeAmount, bolt->s.pos.trDelta);

	if (ent->health > 0)
	{
		bolt->s.pos.trDelta[2] += 120.0f;

		const qboolean has_npc = (ent->NPC != NULL) ? qtrue : qfalse;

		if (((has_npc == qtrue) || ((ent->s.number != 0) && (this_is_a_shooter == qtrue))) &&
			ent->enemy != NULL)
		{
			vec3_t target;
			VectorCopy(ent->enemy->currentOrigin, target);

			if (target[2] <= start[2])
			{
				vec3_t vec;
				VectorSubtract(target, start, vec);
				VectorNormalize(vec);
				VectorMA(target, Q_flrand(0.0f, -32.0f), vec, target);
			}

			float aim = 6.0f;
			if (has_npc == qtrue)
			{
				aim = ent->NPC->currentAim;
			}

			target[0] += Q_flrand(-5.0f, 5.0f) + Q_flrand(-1.0f, 1.0f) * (6.0f - aim) * 2.0f;
			target[1] += Q_flrand(-5.0f, 5.0f) + Q_flrand(-1.0f, 1.0f) * (6.0f - aim) * 2.0f;
			target[2] += Q_flrand(-5.0f, 5.0f) + Q_flrand(-1.0f, 1.0f) * (6.0f - aim) * 2.0f;

			WP_LobFire(ent, start, target,
				bolt->mins, bolt->maxs, bolt->clipmask,
				bolt->s.pos.trDelta, qtrue,
				ent->s.number, ent->enemy->s.number);
		}
		else if (this_is_a_shooter == qtrue &&
			ent->target != NULL &&
			!VectorCompare(ent->pos1, vec3_origin))
		{
			WP_LobFire(ent, start, ent->pos1,
				bolt->mins, bolt->maxs, bolt->clipmask,
				bolt->s.pos.trDelta, qtrue,
				ent->s.number,
				(ent->enemy != NULL) ? ent->enemy->s.number : ENTITYNUM_NONE);
		}
	}

	if (alt_fire == qtrue)
	{
		bolt->alt_fire = qtrue;
	}
	else
	{
		bolt->s.eFlags |= EF_BOUNCE_HALF;
	}

	bolt->s.loopSound = G_SoundIndex("sound/weapons/thermal/thermloop.wav");

	bolt->damage = weaponData[WP_THERMAL].damage * damageScale;
	bolt->dflags = 0;
	bolt->splashDamage = weaponData[WP_THERMAL].splashDamage * damageScale;
	bolt->splashRadius = weaponData[WP_THERMAL].splashRadius;

	bolt->s.eType = ET_MISSILE;
	bolt->svFlags = SVF_USE_CURRENT_ORIGIN;
	bolt->s.weapon = WP_THERMAL;
	bolt->s.weaponVariant = G_GrenadeVariant(ent); // grenades (2026-10-31): the grenade type (its model, blast, effect)

	if (alt_fire == qtrue)
	{
		bolt->methodOfDeath = MOD_THERMAL_ALT;
		bolt->splashMethodOfDeath = MOD_THERMAL_ALT;
	}
	else
	{
		bolt->methodOfDeath = MOD_THERMAL;
		bolt->splashMethodOfDeath = MOD_THERMAL;
	}

	bolt->s.pos.trTime = level.time;
	VectorCopy(start, bolt->s.pos.trBase);

	SnapVector(bolt->s.pos.trDelta);
	VectorCopy(start, bolt->currentOrigin);
	VectorCopy(start, bolt->pos2);

	return bolt;
}

//---------------------------------------------------------
gentity_t* WP_DropThermal(gentity_t* ent)
//---------------------------------------------------------
{
	AngleVectors(ent->client->ps.viewangles, forward_vec, vright_vec, up);
	CalcEntitySpot(ent, SPOT_WEAPON, muzzle);
	return WP_FireThermalDetonator(ent, qfalse);
}