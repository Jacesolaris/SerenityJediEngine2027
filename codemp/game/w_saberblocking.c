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
#include "bg_local.h"
#include "w_saber.h"
#include "ai_main.h"
#include <qcommon\q_color.h>
#include "g_public.h"
#include <qcommon\q_shared.h>
#include <qcommon\q_math.h>
#include <qcommon\q_platform.h>
#include "bg_public.h"

//////////Defines////////////////
extern qboolean PM_SaberInNonIdleDamageMove(const playerState_t* ps, int AnimIndex);
extern qboolean PM_SaberInBounce(const int move);
extern qboolean BG_InSlowBounce(const playerState_t* ps);
extern bot_state_t* botstates[MAX_CLIENTS];
extern qboolean PM_SaberInTransitionAny(int move);
extern qboolean PM_SuperBreakWinAnim(int anim);
extern qboolean WalkCheck(const gentity_t* self);
extern qboolean WP_SabersCheckLock(gentity_t* ent1, gentity_t* ent2);
extern void PM_AddFatigue(playerState_t* ps, int fatigue);
extern void G_AddVoiceEvent(const gentity_t* self, int event, int speak_debounce_time);
extern qboolean npc_is_dark_jedi(const gentity_t* self);
extern saberMoveName_t PM_BrokenParryForParry(int move);
extern saberMoveName_t pm_broken_parry_for_attack(int move);
extern qboolean PM_InGetUp(const playerState_t* ps);
extern qboolean PM_InForceGetUp(const playerState_t* ps);
extern qboolean G_ControlledByPlayer(const gentity_t* self);
extern void WP_BlockPointsRegenerate(const gentity_t* self, const int override_amt);
extern void PM_AddBlockFatigue(playerState_t* ps, int fatigue);
extern int WP_ClassBlockPointCost(const gentity_t* blocker, int cost, qboolean vs_blaster);
extern saberMoveName_t pm_block_the_attack(int move);
extern int g_block_the_attack(int move);
extern saberMoveName_t PM_SaberBounceForAttack(int move);
extern void G_Stagger(gentity_t* hitEnt);
extern void g_fatigue_bp_knockaway(gentity_t* blocker);
extern qboolean PM_SuperBreakLoseAnim(int anim);
extern qboolean WP_ButterFingers(gentity_t* saberent, gentity_t* saber_owner, const gentity_t* other, const trace_t* tr);
extern qboolean PM_SaberInnonblockableAttack(int anim);
extern qboolean PM_SaberInSpecialAttack(int anim);
extern int G_GetParryForBlock(int block);
extern qboolean WP_SaberMBlockDirection(gentity_t* self, vec3_t hitloc, const qboolean missileBlock);
extern qboolean WP_SaberBlockNonRandom(gentity_t* self, vec3_t hitloc, qboolean missileBlock);
extern qboolean WP_SaberBouncedSaberDirection(gentity_t* self, vec3_t hitloc, qboolean missileBlock);
extern qboolean WP_SaberFatiguedParryDirection(gentity_t* self, vec3_t hitloc, qboolean missileBlock);
extern void WP_BlockPointsRegenerate_over_ride(const gentity_t* self, const int override_amt);
void sab_beh_animate_heavy_slow_bounce_attacker(gentity_t* attacker);
extern void G_StaggerAttacker(gentity_t* atk);
extern void G_BounceAttacker(gentity_t* atk);
extern void WP_SaberClearDamageForEntNum(gentity_t* attacker, const int entityNum, const int saberNum, const int bladeNum);
extern void g_do_m_block_response(const gentity_t* speaker_npc_self);
extern qboolean Rosh_BeingHealed(const gentity_t* self);
//////////Defines////////////////

static void sab_beh_saber_should_be_disarmed_attacker(gentity_t* attacker, const gentity_t* blocker)
{
	static trace_t tr;

	if (!(attacker->client->saber[0].saberFlags & SFL_NOT_DISARMABLE))
	{
		G_Stagger(attacker);

		WP_ButterFingers(&g_entities[attacker->client->ps.saberEntityNum], attacker, blocker, &tr);
	}
}

static void SabBeh_SaberShouldBeDisarmedBlocker(gentity_t* blocker, const gentity_t* attacker)
{
	static trace_t tr;

	if (!(blocker->client->saber[0].saberFlags & SFL_NOT_DISARMABLE))
	{
		G_Stagger(blocker);

		WP_ButterFingers(&g_entities[blocker->client->ps.saberEntityNum], blocker, attacker, &tr);
	}
}

qboolean g_accurate_blocking(const gentity_t* blocker, const gentity_t* attacker, vec3_t hit_loc)
{
	vec3_t p_angles, p_right;
	vec3_t parrier_move = { 0 };
	vec3_t hit_pos, hit_flat = { 0 };

	// Slight tolerance so attacks slightly off-center can still be blocked
	const qboolean in_front_of_me =
		InFront(attacker->client->ps.origin,
			blocker->client->ps.origin,
			blocker->client->ps.viewangles,
			0.3f);

	// Player must be holding block (NPCs exempt)
	if (!(blocker->r.svFlags & SVF_BOT))
	{
		if (!(((blocker->client->ps.ManualBlockingFlags & (1 << MBF_HOLDINGBLOCK)) != 0)))
			return qfalse;
	}

	// Cannot block from behind
	if (!in_front_of_me)
		return qfalse;

	// Already in knockaway → allow continued parry
	if (PM_SaberInKnockaway(blocker->client->ps.saberMove))
		return qtrue;

	// Cannot parry while kicking
	if (PM_KickingAnim(blocker->client->ps.legsAnim))
		return qfalse;

	// Cannot parry while transitioning or bouncing
	if (PM_SaberInNonIdleDamageMove(&blocker->client->ps, blocker->localAnimIndex) ||
		PM_SaberInBounce(blocker->client->ps.saberMove) ||
		BG_InSlowBounce(&blocker->client->ps))
		return qfalse;

	// Cannot parry while ducked
	if (blocker->client->ps.pm_flags & PMF_DUCKED)
		return qfalse;

	// Cannot parry while knocked down
	if (PM_InKnockDown(&blocker->client->ps))
		return qfalse;

	// Held block too long → too slow to parry
	if (level.time - blocker->client->ps.ManualblockStartTime >= 3000)
		return qfalse;

	// ------------------------------------------------------------
	// Directional parry correctness
	// ------------------------------------------------------------

	// Vector from blocker to hit location
	VectorSubtract(hit_loc, blocker->client->ps.origin, hit_pos);

	// Blocker's right vector (yaw only)
	VectorSet(p_angles, 0, blocker->client->ps.viewangles[YAW], 0);
	AngleVectors(p_angles, NULL, p_right, NULL);

	// Flatten hit into blocker's local plane
	hit_flat[0] = 0;
	hit_flat[1] = DotProduct(p_right, hit_pos);
	hit_flat[2] = hit_pos[2] - (attacker->client->ps.viewheight * 0.25f);
	VectorNormalize(hit_flat);

	// Player's intended parry direction
	parrier_move[0] = 0;
	parrier_move[1] = blocker->client->pers.cmd.rightmove;
	parrier_move[2] = -blocker->client->pers.cmd.forwardmove;

	if (VectorLength(parrier_move) < 0.1f)
		return qfalse; // no directional input → no parry

	VectorNormalize(parrier_move);

	// ------------------------------------------------------------
	// Style-based threshold
	// ------------------------------------------------------------
	float threshold = 0.40f; // MP default

	switch (blocker->client->ps.fd.saberAnimLevel)
	{
	case SS_FAST:
		threshold = 0.55f;
		break;

	case SS_STRONG:
		threshold = 0.35f;
		break;

	default:
		threshold = 0.40f;
		break;
	}

	const float block_dot = DotProduct(hit_flat, parrier_move);

	if (block_dot >= threshold)
		return qtrue;

	// ------------------------------------------------------------
	// NPC fallback
	// ------------------------------------------------------------
	if (blocker->r.svFlags & SVF_BOT)
	{
		if (BOT_PARRYRATE * botstates[blocker->s.number]->settings.skill > Q_irand(0, 999))
			return qtrue;
	}

	return qfalse;
}

static void sab_beh_add_mishap_attacker(gentity_t* attacker, const gentity_t* blocker)
{
	if (attacker->client->ps.fd.blockPoints <= MISHAPLEVEL_NONE)
	{
		attacker->client->ps.fd.blockPoints = MISHAPLEVEL_NONE;
	}
	else if (attacker->client->ps.saberFatigueChainCount <= MISHAPLEVEL_NONE)
	{
		attacker->client->ps.saberFatigueChainCount = MISHAPLEVEL_NONE;
	}
	else
	{
		//overflowing causes a full mishap.
		const int rand_num = Q_irand(0, 2);

		switch (rand_num)
		{
		case 0:
			if (blocker->r.svFlags & SVF_BOT) //NPC only
			{
				if (!Q_irand(0, 4))
				{
					//20% chance
					sab_beh_animate_heavy_slow_bounce_attacker(attacker);
					if (d_attackinfo.integer || g_DebugSaberCombat.integer && attacker->r.svFlags & SVF_BOT)
					{
						Com_Printf(S_COLOR_YELLOW"NPC Attacker staggering\n");
					}
				}
				else
				{
					sab_beh_saber_should_be_disarmed_attacker(attacker, blocker);
					if (d_attackinfo.integer || g_DebugSaberCombat.integer && attacker->r.svFlags & SVF_BOT)
					{
						Com_Printf(S_COLOR_RED"NPC Attacker lost his saber\n");
					}
				}
			}
			else
			{
				sab_beh_saber_should_be_disarmed_attacker(attacker, blocker);
				if (d_attackinfo.integer || g_DebugSaberCombat.integer && !(attacker->r.svFlags & SVF_BOT))
				{
					Com_Printf(S_COLOR_RED"Player Attacker lost his saber\n");
				}
			}
			break;
		case 1:
		default: // 2026-11-10: case 2 did nothing, so a third of all full mishaps went unpunished
			sab_beh_animate_heavy_slow_bounce_attacker(attacker);
			if (d_attackinfo.integer || g_DebugSaberCombat.integer && !(attacker->r.svFlags & SVF_BOT))
			{
				Com_Printf(S_COLOR_RED"Player Attacker staggering\n");
			}
			break;
		}
	}
}

static void sab_beh_add_mishap_Fake_attacker(gentity_t* attacker, const gentity_t* blocker)
{
	if (attacker->client->ps.fd.blockPoints <= MISHAPLEVEL_NONE)
	{
		attacker->client->ps.fd.blockPoints = MISHAPLEVEL_NONE;
	}
	else if (attacker->client->ps.saberFatigueChainCount <= MISHAPLEVEL_NONE)
	{
		attacker->client->ps.saberFatigueChainCount = MISHAPLEVEL_NONE;
	}
	else
	{
		//overflowing causes a full mishap.
		const int rand_num = Q_irand(0, 2);

		switch (rand_num)
		{
		case 0:
			if (blocker->r.svFlags & SVF_BOT) //NPC only
			{
				if (!Q_irand(0, 4))
				{
					//20% chance
					sab_beh_saber_should_be_disarmed_attacker(attacker, blocker);
					if (d_attackinfo.integer || g_DebugSaberCombat.integer && attacker->r.svFlags & SVF_BOT)
					{
						Com_Printf(S_COLOR_RED"NPC Attacker lost his saber\n");
					}
				}
				else
				{
					sab_beh_animate_heavy_slow_bounce_attacker(attacker);
					if (d_attackinfo.integer || g_DebugSaberCombat.integer && attacker->r.svFlags & SVF_BOT)
					{
						Com_Printf(S_COLOR_YELLOW"NPC Attacker staggering\n");
					}
				}
			}
			else
			{
				sab_beh_saber_should_be_disarmed_attacker(attacker, blocker);
				if (d_attackinfo.integer || g_DebugSaberCombat.integer && !(attacker->r.svFlags & SVF_BOT))
				{
					Com_Printf(S_COLOR_RED"Player Attacker lost his saber\n");
				}
			}
			break;
		case 1:
		default: // 2026-11-10: case 2 did nothing, so a third of all parried feints went unpunished
			sab_beh_animate_heavy_slow_bounce_attacker(attacker);
			if (d_attackinfo.integer || g_DebugSaberCombat.integer && !(attacker->r.svFlags & SVF_BOT))
			{
				Com_Printf(S_COLOR_RED"Player Attacker staggering\n");
			}
			break;
		}
	}
}

static void sab_beh_add_mishap_blocker(gentity_t* blocker, const gentity_t* attacker)
{
	if (blocker->client->ps.fd.blockPoints <= MISHAPLEVEL_NONE)
	{
		blocker->client->ps.fd.blockPoints = MISHAPLEVEL_NONE;
	}
	else if (blocker->client->ps.saberFatigueChainCount <= MISHAPLEVEL_NONE)
	{
		blocker->client->ps.saberFatigueChainCount = MISHAPLEVEL_NONE;
	}
	else
	{
		//overflowing causes a full mishap.
		const int rand_num = Q_irand(0, 2);

		switch (rand_num)
		{
		case 0:
			G_Stagger(blocker);
			if (d_blockinfo.integer || g_DebugSaberCombat.integer)
				Com_Printf(S_COLOR_RED "blocker staggering\n");
			break;
		case 1:
			if (blocker->r.svFlags & SVF_BOT)
			{
				// 75% chance to disarm, 25% chance to stagger
				const int roll = Q_irand(0, 3);  // values: 0,1,2,3

				if (roll == 0)
				{
					// 25% chance
					G_Stagger(blocker);

					if (d_blockinfo.integer || g_DebugSaberCombat.integer)
					{
						Com_Printf(S_COLOR_RED "NPC blocker staggering\n");
					}
				}
				else
				{
					// 75% chance
					SabBeh_SaberShouldBeDisarmedBlocker(blocker, attacker);
					WP_BlockPointsRegenerate_over_ride(blocker, BLOCKPOINTS_FATIGUE);

					if (d_blockinfo.integer || g_DebugSaberCombat.integer)
					{
						Com_Printf(S_COLOR_RED "NPC blocker lost his saber\n");
					}
				}
			}
			else
			{
				SabBeh_SaberShouldBeDisarmedBlocker(blocker, attacker);

				if (d_blockinfo.integer || g_DebugSaberCombat.integer)
				{
					Com_Printf(S_COLOR_RED "Player blocker lost his saber\n");
				}
			}
			break;
		default:;
		}
	}
}

////////Bounces//////////

void sab_beh_animate_heavy_slow_bounce_attacker(gentity_t* attacker)
{
	G_StaggerAttacker(attacker);
	attacker->client->ps.userInt3 |= 1 << FLAG_SLOWBOUNCE;
	attacker->client->ps.userInt3 |= 1 << FLAG_OLDSLOWBOUNCE;
}

static void sab_beh_animate_small_bounce(gentity_t* attacker)
{
	if (attacker->r.svFlags & SVF_BOT) //NPC only
	{
		attacker->client->ps.userInt3 |= 1 << FLAG_SLOWBOUNCE;
		attacker->client->ps.userInt3 |= 1 << FLAG_OLDSLOWBOUNCE;
		G_BounceAttacker(attacker);
	}
	else
	{
		attacker->client->ps.userInt3 |= 1 << FLAG_SLOWBOUNCE;
		attacker->client->ps.saberBounceMove = LS_D1_BR + (saberMoveData[attacker->client->ps.saberMove].startQuad - Q_BR);
		attacker->client->ps.saberBlocked = BLOCKED_ATK_BOUNCE;
	}
}

static void sab_beh_animate_heavy_slow_bounce_blocker(gentity_t* blocker, gentity_t* attacker)
{
	blocker->client->ps.userInt3 |= 1 << FLAG_SLOWBOUNCE;
	blocker->client->ps.userInt3 |= 1 << FLAG_OLDSLOWBOUNCE;

	G_AddEvent(blocker, Q_irand(EV_PUSHED1, EV_PUSHED3), 0);
	G_AddEvent(attacker, Q_irand(EV_DEFLECT1, EV_DEFLECT3), 0);

	blocker->client->ps.saberBounceMove = pm_broken_parry_for_attack(blocker->client->ps.saberMove);
	blocker->client->ps.saberBlocked = BLOCKED_PARRY_BROKEN;
}

void sab_beh_animate_slow_bounce_blocker(gentity_t* blocker)
{
	blocker->client->ps.userInt3 |= 1 << FLAG_SLOWBOUNCE;
	blocker->client->ps.userInt3 |= 1 << FLAG_OLDSLOWBOUNCE;

	G_AddEvent(blocker, Q_irand(EV_PUSHED1, EV_PUSHED3), 0);

	blocker->client->ps.saberBounceMove = PM_BrokenParryForParry(G_GetParryForBlock(blocker->client->ps.saberBlocked));
	blocker->client->ps.saberBlocked = BLOCKED_PARRY_BROKEN;
}

////////Bounces//////////

static qboolean sab_beh_attack_blocked(gentity_t* attacker, gentity_t* blocker, const qboolean force_mishap)
{
	//if the attack is blocked -(Im the attacker)
	const qboolean m_blocking = blocker->client->ps.ManualBlockingFlags & 1 << MBF_PERFECTBLOCKING ? qtrue : qfalse;
	//perfect Blocking (Timed Block)

	if (attacker->client->ps.saberFatigueChainCount >= MISHAPLEVEL_MAX)
	{
		//hard mishap.

		if (attacker->r.svFlags & SVF_BOT) //NPC only
		{
			if (!Q_irand(0, 4))
			{
				//20% chance
				sab_beh_add_mishap_attacker(attacker, blocker);
			}
			else
			{
				sab_beh_animate_heavy_slow_bounce_attacker(attacker);
			}
			if (d_attackinfo.integer || g_DebugSaberCombat.integer)
			{
				Com_Printf(S_COLOR_GREEN"Attacker npc is fatigued\n");
			}

			attacker->client->ps.saberFatigueChainCount = MISHAPLEVEL_MIN;
		}
		else
		{
			if (d_attackinfo.integer || g_DebugSaberCombat.integer)
			{
				Com_Printf(S_COLOR_GREEN"Attacker player is fatigued\n");
			}
			sab_beh_add_mishap_attacker(attacker, blocker);
		}
		return qtrue;
	}
	if (attacker->client->ps.saberFatigueChainCount >= MISHAPLEVEL_HUDFLASH)
	{
		//slow bounce
		if (!(attacker->r.svFlags & SVF_BOT))
		{
			sab_beh_animate_heavy_slow_bounce_attacker(attacker);
		}
		else
		{
			sab_beh_animate_small_bounce(attacker);
		}

		if (attacker->r.svFlags & SVF_BOT) //NPC only
		{
			attacker->client->ps.saberFatigueChainCount = MISHAPLEVEL_LIGHT;
		}

		if (d_attackinfo.integer || g_DebugSaberCombat.integer)
		{
			if (!(attacker->r.svFlags & SVF_BOT))
			{
				Com_Printf(S_COLOR_GREEN"player attack stagger\n");
			}
			else
			{
				Com_Printf(S_COLOR_GREEN"npc attack stagger\n");
			}
		}
		return qtrue;
	}
	if (attacker->client->ps.saberFatigueChainCount >= MISHAPLEVEL_LIGHT)
	{
		//slow bounce
		sab_beh_animate_small_bounce(attacker);

		if (d_attackinfo.integer || g_DebugSaberCombat.integer)
		{
			if (!(attacker->r.svFlags & SVF_BOT))
			{
				Com_Printf(S_COLOR_GREEN"player light blocked bounce\n");
			}
			else
			{
				Com_Printf(S_COLOR_GREEN"npc light blocked bounce\n");
			}
		}
		return qtrue;
	}
	if (force_mishap)
	{
		//two attacking sabers bouncing off each other
		sab_beh_animate_small_bounce(attacker);
		sab_beh_animate_small_bounce(blocker);

		if (d_attackinfo.integer || g_DebugSaberCombat.integer)
		{
			if (!(attacker->r.svFlags & SVF_BOT))
			{
				Com_Printf(S_COLOR_GREEN"player two attacking sabers bouncing off each other\n");
			}
			else
			{
				Com_Printf(S_COLOR_GREEN"npc two attacking sabers bouncing off each other\n");
			}
		}
		return qtrue;
	}
	if (!m_blocking)
	{
		if (d_attackinfo.integer || g_DebugSaberCombat.integer)
		{
			if (!(attacker->r.svFlags & SVF_BOT))
			{
				Com_Printf(S_COLOR_GREEN"player blocked bounce\n");
			}
			else
			{
				Com_Printf(S_COLOR_GREEN"npc blocked bounce\n");
			}
		}
		sab_beh_animate_small_bounce(attacker);
	}
	return qtrue;
}

static void sab_beh_add_balance(const gentity_t* self, int amount)
{
	if (!WalkCheck(self))
	{
		//running or moving very fast, can't balance as well
		if (amount > 0)
		{
			amount *= 2;
		}
		else
		{
			amount = amount * .5f;
		}
	}

	self->client->ps.saberFatigueChainCount += amount;

	if (self->client->ps.saberFatigueChainCount < MISHAPLEVEL_NONE)
	{
		self->client->ps.saberFatigueChainCount = MISHAPLEVEL_NONE;
	}
	else if (self->client->ps.saberFatigueChainCount > MISHAPLEVEL_OVERLOAD)
	{
		self->client->ps.saberFatigueChainCount = MISHAPLEVEL_MAX;
	}
}

//////////Actions////////////////

// 2026-11-10 (DH: MP had saber locks a little too often): g_saberLockRandomNess as in SJE SP, 0 = rarely (default),
// 1 = sometimes, 2 = often. A feint caught by the other blade: 1 in 4 / 1 in 2 / always (it was always). A plain clash:
// never / g_saberLockFactor in 20 (as before) / twice that.
qboolean WP_SaberLockChance(const qboolean fake_lock)
{
	const int how_often = g_saberLockRandomNess.integer;

	if (g_debugSaberLocks.integer)
	{
		return qtrue;
	}
	if (fake_lock)
	{
		if (how_often >= 2)
		{
			return qtrue;
		}
		return Q_irand(0, how_often >= 1 ? 1 : 3) == 0 ? qtrue : qfalse;
	}
	if (how_often <= 0)
	{
		return qfalse;
	}
	return Q_irand(1, 20) < (g_saberLockFactor.integer - 1) * (how_often >= 2 ? 2 : 1) + 1 ? qtrue : qfalse;
}

/////////Functions//////////////

// 2026-11-11 (DH): players pay less block points for blocking saber attacks (NPCs and bots pay the full cost). Used for
// every block point cost of the blocker in sab_beh_attack_vs_attack and sab_beh_block_vs_attack. 1.0f = as before.
#define SABER_PLAYER_BLOCK_COST_SCALE	0.5f

static void WP_SaberBlockerBlockPoints(gentity_t* blocker, const int cost)
{
	int final_cost = cost;

	if (blocker->s.number >= 0 && blocker->s.number < MAX_CLIENTS && !(blocker->r.svFlags & SVF_BOT))
	{
		final_cost = (int)(cost * SABER_PLAYER_BLOCK_COST_SCALE + 0.5f);
		if (cost > 0 && final_cost < 1)
		{
			final_cost = 1;
		}
	}
	PM_AddBlockFatigue(&blocker->client->ps, final_cost);
}

static qboolean sab_beh_attack_vs_attack(gentity_t* attacker, gentity_t* blocker)
{
	//set the saber behavior for two attacking blades hitting each other
	const qboolean atkfake = attacker->client->ps.userInt3 & 1 << FLAG_ATTACKFAKE ? qtrue : qfalse;
	const qboolean otherfake = blocker->client->ps.userInt3 & 1 << FLAG_ATTACKFAKE ? qtrue : qfalse;

	if (atkfake && !otherfake)
	{
		//self is solo faking
		//set self
		sab_beh_add_balance(attacker, MPCOST_PARRIED);
		//set otherOwner

		if (WP_SaberLockChance(qtrue) && WP_SabersCheckLock(attacker, blocker))
		{
			attacker->client->ps.userInt3 |= 1 << FLAG_SABERLOCK_ATTACKER;
			attacker->client->ps.saberBlocked = BLOCKED_NONE;
			blocker->client->ps.saberBlocked = BLOCKED_NONE;
		}
		sab_beh_add_balance(blocker, -MPCOST_PARRIED);
	}
	else if (!atkfake && otherfake)
	{
		//only otherOwner is faking
		//set self
		if (WP_SaberLockChance(qtrue) && WP_SabersCheckLock(blocker, attacker))
		{
			attacker->client->ps.saberBlocked = BLOCKED_NONE;
			blocker->client->ps.userInt3 |= 1 << FLAG_SABERLOCK_ATTACKER;
			blocker->client->ps.saberBlocked = BLOCKED_NONE;
		}
		sab_beh_add_balance(attacker, -MPCOST_PARRIED);
		//set otherOwner
		sab_beh_add_balance(blocker, MPCOST_PARRIED);
	}
	else if (atkfake && otherfake)
	{
		//both faking
		//set self
		if (WP_SaberLockChance(qtrue) && WP_SabersCheckLock(attacker, blocker))
		{
			attacker->client->ps.userInt3 |= 1 << FLAG_SABERLOCK_ATTACKER;
			attacker->client->ps.saberBlocked = BLOCKED_NONE;

			blocker->client->ps.userInt3 |= 1 << FLAG_SABERLOCK_ATTACKER;
			blocker->client->ps.saberBlocked = BLOCKED_NONE;
		}
		sab_beh_add_balance(attacker, MPCOST_PARRIED);
		//set otherOwner
		sab_beh_add_balance(blocker, MPCOST_PARRIED);
	}
	else if (PM_SaberInKata(attacker->client->ps.saberMove))
	{
		sab_beh_add_balance(attacker, MPCOST_PARRIED);
		//set otherOwner
		sab_beh_add_balance(blocker, -MPCOST_PARRIED);

		if (blocker->client->ps.fd.blockPoints < BLOCKPOINTS_TEN)
		{
			//Low points = bad blocks
			SabBeh_SaberShouldBeDisarmedBlocker(blocker, attacker);
			WP_BlockPointsRegenerate_over_ride(blocker, BLOCKPOINTS_FATIGUE);
		}
		else
		{
			//Low points = bad blocks
			G_Stagger(blocker);
			WP_SaberBlockerBlockPoints(blocker, WP_ClassBlockPointCost(blocker, BLOCKPOINTS_TEN, qfalse));
		}
	}
	else if (PM_SaberInKata(blocker->client->ps.saberMove))
	{
		sab_beh_add_balance(attacker, -MPCOST_PARRIED);
		//set otherOwner
		sab_beh_add_balance(blocker, MPCOST_PARRIED);

		if (attacker->client->ps.fd.blockPoints < BLOCKPOINTS_TEN)
		{
			//Low points = bad blocks
			sab_beh_saber_should_be_disarmed_attacker(attacker, blocker);
			WP_BlockPointsRegenerate_over_ride(attacker, BLOCKPOINTS_FATIGUE);
		}
		else
		{
			//Low points = bad blocks
			G_Stagger(attacker);
			PM_AddBlockFatigue(&attacker->client->ps, WP_ClassBlockPointCost(attacker, BLOCKPOINTS_TEN, qfalse)); // 2026-11-10 (audit fix): class cost, as the other kata branch (was a flat 10)
		}
	}
	else
	{
		//either both are faking or neither is faking.  Either way, it's cancelled out
		//set self
		sab_beh_add_balance(attacker, MPCOST_PARRIED);
		//set otherOwner
		sab_beh_add_balance(blocker, MPCOST_PARRIED);

		sab_beh_attack_blocked(attacker, blocker, qtrue);

		sab_beh_attack_blocked(blocker, attacker, qtrue);
	}
	return qtrue;
}

// Saber combat refinement (SJE SP item 287, 2026-11-09), MP port: the same perfect parry / riposte / adaptive AI /
// feints / taunts as SJE SP. MP "AI" is a saber NPC or a bot, a "player" is a human client.
extern qboolean PM_SaberInStart(int move);
extern qboolean PM_SaberInAttack(int move);
extern qboolean PM_SaberInParry(int move);
extern qboolean PM_InKnockDown(const playerState_t* ps);
static int saber_feint_next[MAX_GENTITIES];
static int saber_taunt_want[MAX_GENTITIES];

static qboolean WP_SaberAIIsHuman(const gentity_t* ent)
{
	return ent && ent->client && ent->s.number >= 0 && ent->s.number < MAX_CLIENTS && !(ent->r.svFlags & SVF_BOT)
		&& !ent->NPC ? qtrue : qfalse;
}

static qboolean WP_SaberAIIsAI(const gentity_t* ent)
{
	return ent && ent->client && ((ent->NPC && !G_ControlledByPlayer(ent)) || (ent->r.svFlags & SVF_BOT)) ? qtrue : qfalse;
}

// 0 (easy) .. 3 (hardest): NPCs g_npcspskill, bots their skill (1-5)
static int WP_SaberAISkill(const gentity_t* ent)
{
	int skill = g_npcspskill.integer;
	if (ent && (ent->r.svFlags & SVF_BOT) && ent->s.number >= 0 && ent->s.number < MAX_CLIENTS && botstates[ent->s.number])
	{
		skill = (int)botstates[ent->s.number]->settings.skill - 2;
	}
	return skill < 0 ? 0 : skill > 3 ? 3 : skill;
}

// Is he turtling: guard up (holding block, or in a parry) and not swinging?
qboolean WP_SaberAIEnemyTurtling(const gentity_t* enemy)
{
	const playerState_t* ps;
	if (!enemy || !enemy->client || enemy->health <= 0)
	{
		return qfalse;
	}
	ps = &enemy->client->ps;
	if (PM_SaberInAttack(ps->saberMove) || PM_SaberInStart(ps->saberMove))
	{
		return qfalse;
	}
	return (ps->ManualBlockingFlags & (1 << MBF_HOLDINGBLOCK | 1 << MBF_HOLDINGBLOCKANDATTACK | 1 << MBF_NPCBLOCKING))
		|| PM_SaberInParry(ps->saberMove) ? qtrue : qfalse;
}

// Feint (g_saberAITactics): a saber NPC or bot whose swing wind-up (LS_S_*) is ending, against a player who turtles
// within 128 units, sometimes cuts the swing and comes in from another side (PM_DoFake, as the player's attack + use fake;
// it costs the NPC one mishap level like any fake). Chance 25% (easy) .. 55% (hardest); a feint can only follow 15 s ..
// 9 s after the last one, and a failed roll waits 1.5 s before it is rolled again. Bots too since 2026-11-09 (their
// enemy is the bot AI's current enemy).
qboolean WP_SaberAIFeintWanted(gentity_t* npc)
{
	const gentity_t* enemy;
	int skill;
	if (!g_saberAITactics.integer || !npc || !npc->client || npc->s.number < 0 || npc->s.number >= MAX_GENTITIES)
	{
		return qfalse;
	}
	if (npc->r.svFlags & SVF_BOT)
	{
		enemy = npc->s.number < MAX_CLIENTS && botstates[npc->s.number] ? botstates[npc->s.number]->currentEnemy : NULL;
	}
	else if (npc->NPC && !G_ControlledByPlayer(npc))
	{
		enemy = npc->enemy;
	}
	else
	{
		return qfalse;
	}
	if (!enemy || !WP_SaberAIIsHuman(enemy) || !WP_SaberAIEnemyTurtling(enemy)
		|| DistanceSquared(npc->r.currentOrigin, enemy->r.currentOrigin) > 128.0f * 128.0f)
	{
		return qfalse;
	}
	if (saber_feint_next[npc->s.number] > level.time + 15000)
	{
		saber_feint_next[npc->s.number] = 0; // stale (map restart)
	}
	if (saber_feint_next[npc->s.number] > level.time)
	{
		return qfalse;
	}
	skill = WP_SaberAISkill(npc);
	if (Q_irand(0, 99) >= 25 + 10 * skill)
	{
		saber_feint_next[npc->s.number] = level.time + 1500;
		return qfalse;
	}
	saber_feint_next[npc->s.number] = level.time + 15000 - 2000 * skill;
	if (d_blockinfo.integer || g_DebugSaberCombat.integer)
	{
		Com_Printf(S_COLOR_YELLOW"Saber AI: %s feints\n", npc->NPC ? npc->NPC_type : npc->client->pers.netname);
	}
	return qtrue;
}

// Taunt wish: set when an NPC perfect-parries a player or breaks his guard; NPC_AI_Jedi (Jedi_TacticalTaunt) taunts
// within 3 s if it has room for it.
qboolean WP_SaberAITauntWanted(const gentity_t* npc)
{
	int t;
	if (!npc || npc->s.number < 0 || npc->s.number >= MAX_GENTITIES)
	{
		return qfalse;
	}
	t = saber_taunt_want[npc->s.number];
	return t > level.time && t <= level.time + 3000 ? qtrue : qfalse;
}

void WP_SaberAITauntClear(const gentity_t* npc)
{
	if (npc && npc->s.number >= 0 && npc->s.number < MAX_GENTITIES)
	{
		saber_taunt_want[npc->s.number] = 0;
	}
}

static void WP_SaberAITauntSet(const gentity_t* npc, const gentity_t* victim)
{
	if (g_saberAITactics.integer && npc && npc->NPC && !G_ControlledByPlayer(npc) && WP_SaberAIIsHuman(victim)
		&& npc->s.number >= 0 && npc->s.number < MAX_GENTITIES)
	{
		saber_taunt_want[npc->s.number] = level.time + 3000;
	}
}

// Is ent's riposte swing running (FLAG_RIPOSTE, set in PM_SetSaberMove)? A riposte can't be perfect-parried.
static qboolean WP_SaberRiposteSwingActive(const gentity_t* ent)
{
	return g_saberPerfectParry.integer && ent && ent->client && (ent->client->ps.userInt3 & 1 << FLAG_RIPOSTE) ? qtrue : qfalse;
}

// Heavy swings (Strong, Desann, staff, two-handed sabers) cost the attacker more when perfect-parried.
static qboolean WP_SaberHeavySwing(const gentity_t* ent)
{
	switch (ent->client->ps.fd.saberAnimLevel)
	{
	case SS_STRONG:
	case SS_DESANN:
	case SS_STAFF:
		return qtrue;
	default:
		return ent->client->saber[0].saberFlags & SFL_TWO_HANDED ? qtrue : qfalse;
	}
}

// Called after a perfect block has taken its block points from the attacker (every perfect block branch here):
// a heavy swing costs 5 BP more, an attacker left with 0 BP has his guard broken (stagger; with 0 BP his next blocks
// fail), and the blocker gets a 500 ms riposte window (FLAG_RIPOSTEREADY: PM_SetSaberMove turns the next attack into
// the riposte; G_SaberRiposteUpdate ends the window / swing on time).
// ---------------------------------------------------------------------------------------------------------------
// Saber combat aim round 1 (2026-11-10, as SP; DH: cinematic duels that need timing; no swing spam, no block
// turtling; NPCs / bots must not swing spam, a perfect-blocked NPC / bot is staggered and loses more block points).
// ---------------------------------------------------------------------------------------------------------------
#define SABER_AI_PERFECTBLOCKED_BP	30	// block points a perfect-blocked NPC / bot loses (was 20, players lose 10)
#define SABER_UNBLOCKABLE_BLOCK_BP	25	// block points a fighter loses blocking an unblockable attack (kata) without a perfect block
#define SABER_UNBLOCKABLE_PB_FATIGUE	8	// fatigue an attacker gets when his unblockable attack (kata) is perfect blocked (item 318: was 5)
#define SABER_UNBLOCKABLE_PB_FP		25	// stamina (force power) an attacker loses when his kata is perfect blocked (item 318)
#define SABER_RIPOSTE_BLOCK_BP		5	// extra block points a blocked riposte costs the blocker

static int saber_stagger_frame[MAX_GENTITIES];

static qboolean WP_SaberIsPlayer(const gentity_t* ent)
{
	return ent && ent->client && ent->s.number >= 0 && ent->s.number < MAX_CLIENTS && !(ent->r.svFlags & SVF_BOT) ? qtrue : qfalse;
}

// The massive stagger (G_Stagger), at most once a frame: block_vs_attack and attack_vs_block both run for one clash.
static qboolean WP_SaberStaggerOnce(gentity_t* ent)
{
	if (!ent || !ent->client || ent->health <= 0 || ent->client->ps.saberInFlight || PM_InKnockDown(&ent->client->ps))
	{
		return qfalse;
	}
	if (saber_stagger_frame[ent->s.number] == level.time)
	{
		return qtrue;
	}
	saber_stagger_frame[ent->s.number] = level.time;
	G_Stagger(ent);
	return qtrue;
}

// Guard break moment: a camera shake for the player who broke a guard (0 BP stagger or disarm) or whose guard was
// broken. (SP also slows time for 200 ms; MP can't slow one client's time.)
static void WP_SaberGuardBreakMoment(gentity_t* breaker, gentity_t* victim)
{
	if (WP_SaberIsPlayer(breaker))
	{
		CGCam_BlockShakeMP(breaker->s.origin, breaker, 0.6f, 250);
	}
	else if (WP_SaberIsPlayer(victim))
	{
		CGCam_BlockShakeMP(victim->s.origin, victim, 0.6f, 250);
	}
}

// Perfect block mash lockout (2026-11-10): a timed press (block held + attack pressed) opens the perfect block window
// (g_active.c). A press that parried nothing locks the window out for 500 ms after its window ended; a press in that
// time restarts the lockout. So mashing never gets a window, one well timed press does.
#define SABER_PERFECTBLOCK_LOCKOUT	500
static int saber_pb_press_time[MAX_GENTITIES];
static int saber_pb_window[MAX_GENTITIES];
static qboolean saber_pb_landed[MAX_GENTITIES];

// g_active.c: a new timed press. qtrue: open the perfect block window (window ms).
qboolean WP_SaberPerfectBlockPress(const gentity_t* ent, const int window)
{
	int n;
	qboolean locked;
	if (!ent)
	{
		return qfalse;
	}
	n = ent->s.number;
	locked = !saber_pb_landed[n] && saber_pb_press_time[n] > 0
		&& level.time < saber_pb_press_time[n] + saber_pb_window[n] + SABER_PERFECTBLOCK_LOCKOUT
		&& saber_pb_press_time[n] <= level.time ? qtrue : qfalse;

	saber_pb_press_time[n] = level.time;
	saber_pb_window[n] = window;
	saber_pb_landed[n] = qfalse;

	if (locked && (d_blockinfo.integer || g_DebugSaberCombat.integer))
	{
		Com_Printf(S_COLOR_ORANGE"Perfect block press too soon after a missed one: no window\n");
	}
	return locked ? qfalse : qtrue;
}

static void WP_SaberPerfectBlockLanded(const gentity_t* blocker)
{
	if (blocker)
	{
		saber_pb_landed[blocker->s.number] = qtrue;
	}
}

// Bots (2026-11-10, as the saber NPCs; DH: a blocked bot may not swing again before the player has finished his block
// animation cycle): sab_beh_attack_vs_block notes when a swing was blocked; for 1.5 s after that, while the blocker is
// still in his parry, knockaway, reflect or block bounce (or a block is about to start), bot_update_input (ai_main.c)
// takes the bot's attack away.
extern qboolean PM_SaberInKnockaway(int move);
extern qboolean PM_SaberInReflect(int move);
static int saber_blocked_time[MAX_GENTITIES];
static int saber_blocked_by[MAX_GENTITIES];

static void WP_SaberAIBlockedNote(const gentity_t* attacker, const gentity_t* blocker)
{
	if (attacker && blocker)
	{
		saber_blocked_time[attacker->s.number] = level.time;
		saber_blocked_by[attacker->s.number] = blocker->s.number;
	}
}

qboolean WP_SaberBotBlockedWait(const gentity_t* bot)
{
	const gentity_t* blocker;
	const playerState_t* eps;
	int n;
	if (!bot || !bot->client)
	{
		return qfalse;
	}
	n = bot->s.number;
	if (saber_blocked_time[n] <= 0 || saber_blocked_time[n] > level.time || level.time - saber_blocked_time[n] > 1500)
	{
		return qfalse;
	}
	blocker = &g_entities[saber_blocked_by[n]];
	if (!blocker->inuse || !blocker->client || blocker->health <= 0)
	{
		return qfalse;
	}
	eps = &blocker->client->ps;
	if (eps->saberBlocked != BLOCKED_NONE && eps->saberBlocked != BLOCKED_PARRY_BROKEN)
	{//his block is about to start
		return qtrue;
	}
	return PM_SaberInParry(eps->saberMove) || PM_SaberInKnockaway(eps->saberMove)
		|| PM_SaberInReflect(eps->saberMove) || PM_SaberInBounce(eps->saberMove) ? qtrue : qfalse;
}

// A perfect-blocked NPC / bot (DH): always the massive stagger; worn out (fatigue 13+) a 1 in 3 chance to lose the
// saber instead. Players keep the heavy slow bounce (fatigue 13+: a mishap).
static void WP_SaberAIPerfectBlocked(gentity_t* attacker, gentity_t* blocker)
{
	if (attacker->client->ps.saberFatigueChainCount >= MISHAPLEVEL_THIRTEEN && !Q_irand(0, 2))
	{
		sab_beh_saber_should_be_disarmed_attacker(attacker, blocker);
		WP_SaberGuardBreakMoment(blocker, attacker);
		if (d_attackinfo.integer || g_DebugSaberCombat.integer)
		{
			Com_Printf(S_COLOR_RED"Perfect blocked worn out NPC / bot lost his saber\n");
		}
	}
	else
	{
		WP_SaberStaggerOnce(attacker);
		attacker->client->ps.userInt3 |= 1 << FLAG_MBLOCKBOUNCE;
		if (d_attackinfo.integer || g_DebugSaberCombat.integer)
		{
			Com_Printf(S_COLOR_YELLOW"Perfect blocked NPC / bot staggered\n");
		}
	}
}

static void WP_SaberPerfectParryBonus(gentity_t* blocker, gentity_t* attacker)
{
	WP_SaberPerfectBlockLanded(blocker); // the press parried something: no mash lockout
	if (!g_saberPerfectParry.integer || !blocker->client || !attacker->client)
	{
		return;
	}
	if (WP_SaberHeavySwing(attacker))
	{
		PM_AddBlockFatigue(&attacker->client->ps, BLOCKPOINTS_FIVE);
	}
	if (attacker->client->ps.fd.blockPoints <= 0 && WP_SaberStaggerOnce(attacker))
	{
		WP_SaberGuardBreakMoment(blocker, attacker);
		if (d_blockinfo.integer || g_DebugSaberCombat.integer)
		{
			Com_Printf(S_COLOR_CYAN"Perfect parry: guard break, attacker out of block points\n");
		}
	}
	blocker->client->ps.saberRiposteTime = level.time + 500;
	blocker->client->ps.userInt3 |= 1 << FLAG_RIPOSTEREADY;
	blocker->client->ps.userInt3 &= ~(1 << FLAG_RIPOSTE);
	WP_SaberAITauntSet(blocker, attacker); // an NPC that perfect-parried a player may taunt him (g_saberAITactics)
}

// Every frame after Pmove (g_active.c ClientThink_real): the riposte window lasts 500 ms, the riposte swing 1.5 s at most.
void G_SaberRiposteUpdate(gentity_t* ent)
{
	playerState_t* ps;
	if (!ent || !ent->client)
	{
		return;
	}
	ps = &ent->client->ps;
	if (!g_saberPerfectParry.integer)
	{
		ps->userInt3 &= ~(1 << FLAG_RIPOSTEREADY | 1 << FLAG_RIPOSTE);
		ps->saberRiposteTime = 0;
		return;
	}
	if (ps->userInt3 & 1 << FLAG_RIPOSTEREADY)
	{
		if (ps->saberRiposteTime <= level.time)
		{
			ps->userInt3 &= ~(1 << FLAG_RIPOSTEREADY);
			ps->saberRiposteTime = 0;
		}
	}
	else if (ps->userInt3 & 1 << FLAG_RIPOSTE)
	{
		if (ps->saberRiposteTime >= 0)
		{
			ps->saberRiposteTime = -(level.time + 1500); // the riposte swing started this frame
		}
		else if (-ps->saberRiposteTime <= level.time)
		{
			ps->userInt3 &= ~(1 << FLAG_RIPOSTE);
			ps->saberRiposteTime = 0;
		}
	}
	else
	{
		ps->saberRiposteTime = 0;
	}
}

qboolean sab_beh_attack_vs_block(gentity_t* attacker, gentity_t* blocker, const int saberNum, const int bladeNum, vec3_t hit_loc)
{
	//if the attack is blocked -(Im the attacker)
	const qboolean accurate_parry = g_accurate_blocking(blocker, attacker, hit_loc); // Perfect Normal Blocking
	const qboolean blocking = ((blocker->client->ps.ManualBlockingFlags & 1 << MBF_HOLDINGBLOCK) != 0) ? qtrue : qfalse;	//Normal Blocking (just holding block button)
	// a riposte swing can't be perfect-parried (g_saberPerfectParry, 2026-11-09)
	const qboolean m_blocking = ((blocker->client->ps.ManualBlockingFlags & 1 << MBF_PERFECTBLOCKING) != 0) && !WP_SaberRiposteSwingActive(attacker) ? qtrue : qfalse; //perfect Blocking (Timed Block)
	const qboolean is_holding_block_button_and_attack = ((blocker->client->ps.ManualBlockingFlags & 1 << MBF_HOLDINGBLOCKANDATTACK) != 0) ? qtrue : qfalse; //Active Blocking (Holding Block button + Attack button)
	const qboolean npc_blocking = ((blocker->client->ps.ManualBlockingFlags & 1 << MBF_NPCBLOCKING) != 0) ? qtrue : qfalse; //(Npc Blocking function)

	const qboolean atkfake = ((attacker->client->ps.userInt3 & 1 << FLAG_ATTACKFAKE) != 0) ? qtrue : qfalse;

	if (PM_SaberInnonblockableAttack(attacker->client->ps.torsoAnim))
	{
		//perfect Blocking
		if (m_blocking) // A perfectly timed block
		{
			// 2026-11-10 (DH): a perfect-blocked unblockable attack (kata) gives the attacker the massive stagger and a
			// big stamina (fatigue) loss; item 318: he is disarmed too. sab_beh_block_vs_attack ran the perfect parry bonus.
			// item 318 (DH): the attacker is disarmed (a saber that can't be disarmed: the stagger only)
			if (!(attacker->client->saber[0].saberFlags & SFL_NOT_DISARMABLE) && !attacker->client->ps.saberInFlight)
			{
				sab_beh_saber_should_be_disarmed_attacker(attacker, blocker); // stagger + saber knocked out of his hand
			}
			else
			{
				WP_SaberStaggerOnce(attacker);
			}
			PM_AddFatigue(&attacker->client->ps, SABER_UNBLOCKABLE_PB_FP); // high stamina loss
			attacker->client->ps.saberFatigueChainCount += SABER_UNBLOCKABLE_PB_FATIGUE;
			if (attacker->client->ps.saberFatigueChainCount > MISHAPLEVEL_MAX)
			{
				attacker->client->ps.saberFatigueChainCount = MISHAPLEVEL_MAX;
			}
			//just so attacker knows that he was blocked
			attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
			//since it was parried, take away any damage done
			WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);
			PM_AddBlockFatigue(&attacker->client->ps, BLOCKPOINTS_TEN); //BP Punish Attacker
			WP_SaberAIBlockedNote(attacker, blocker);
			if (d_attackinfo.integer || g_DebugSaberCombat.integer)
			{
				Com_Printf(S_COLOR_CYAN"Unblockable attack perfect blocked: attacker disarmed + staggered, +8 fatigue, -25 stamina\n");
			}
		}
		else
		{
			//This must be Unblockable
			if (d_attackinfo.integer || g_DebugSaberCombat.integer)
			{
				Com_Printf(S_COLOR_MAGENTA"Attacker must be Unblockable\n");
			}
			attacker->client->ps.saberEventFlags &= ~SEF_BLOCKED;
		}
	}
	else if (PM_SaberInNonIdleDamageMove(&blocker->client->ps, blocker->localAnimIndex))
	{
		//and blocker is attacking
		if ((d_attackinfo.integer || g_DebugSaberCombat.integer) && !(blocker->r.svFlags & SVF_BOT))
		{
			Com_Printf(S_COLOR_YELLOW"Both Attacker and Blocker are now attacking\n");
		}

		sab_beh_attack_vs_attack(blocker, attacker);
	}
	else if (PM_SuperBreakWinAnim(attacker->client->ps.torsoAnim))
	{
		//attacker was attempting a superbreak and he hit someone who could block the move, rail him for screwing up.
		sab_beh_add_balance(attacker, MPCOST_PARRIED);

		sab_beh_animate_heavy_slow_bounce_attacker(attacker);

		sab_beh_add_balance(blocker, -MPCOST_PARRIED);
		if ((d_attackinfo.integer || g_DebugSaberCombat.integer) && !(blocker->r.svFlags & SVF_BOT))
		{
			Com_Printf(S_COLOR_YELLOW"Attacker Super break win / fail\n");
		}
	}
	else if (atkfake)
	{
		//attacker faked but it was blocked here
		if (m_blocking || npc_blocking)
		{
			//defender parried the attack fake.
			WP_SaberPerfectBlockLanded(blocker); // no mash lockout (2026-11-10)
			WP_SaberAIBlockedNote(attacker, blocker);
			sab_beh_add_balance(attacker, MPCOST_PARRIED_ATTACKFAKE);

			if (npc_blocking)
			{
				attacker->client->ps.userInt3 |= 1 << FLAG_BLOCKED;
			}
			else
			{
				attacker->client->ps.userInt3 |= 1 << FLAG_PARRIED;
			}

			sab_beh_add_balance(blocker, MPCOST_PARRYING_ATTACKFAKE);
			sab_beh_add_mishap_Fake_attacker(attacker, blocker);

			if ((d_attackinfo.integer || g_DebugSaberCombat.integer) && !(blocker->r.svFlags & SVF_BOT))
			{
				Com_Printf(S_COLOR_YELLOW"Attackers Attack Fake was P-Blocked\n");
			}
		}
		else
		{
			//otherwise, the defender stands a good chance of having his defensive broken.
			sab_beh_add_balance(attacker, -MPCOST_PARRIED);

			if (WP_SaberLockChance(qtrue) && WP_SabersCheckLock(attacker, blocker))
			{
				attacker->client->ps.userInt3 |= 1 << FLAG_SABERLOCK_ATTACKER;
				attacker->client->ps.saberBlocked = BLOCKED_NONE;
				blocker->client->ps.saberBlocked = BLOCKED_NONE;
			}

			if ((d_attackinfo.integer || g_DebugSaberCombat.integer) && !(blocker->r.svFlags & SVF_BOT))
			{
				Com_Printf(S_COLOR_YELLOW"Attacker forced a saberlock\n");
			}
		}
	}
	else
	{
		//standard attack.
		if (accurate_parry || blocking || m_blocking || is_holding_block_button_and_attack || npc_blocking) // All types of active blocking
		{
			if (m_blocking || is_holding_block_button_and_attack || npc_blocking)
			{
				if (npc_blocking && blocker->client->ps.fd.blockPoints >= BLOCKPOINTS_MISSILE
					&& attacker->client->ps.saberFatigueChainCount >= MISHAPLEVEL_HUDFLASH
					&& saber_stagger_frame[attacker->s.number] != level.time // already staggered by this clash (2026-11-10)
					&& !Q_irand(0, 4))
				{
					//20% chance
					sab_beh_animate_heavy_slow_bounce_attacker(attacker);
					attacker->client->ps.userInt3 |= 1 << FLAG_MBLOCKBOUNCE;
				}
				else
				{
					attacker->client->ps.userInt3 |= 1 << FLAG_BLOCKED;
				}

				if (!(attacker->r.svFlags & SVF_BOT))
				{
					CGCam_BlockShakeMP(attacker->s.origin, attacker, 0.45f, 100);
				}
			}
			else
			{
				attacker->client->ps.userInt3 |= 1 << FLAG_PARRIED;
			}

			if (!m_blocking && saber_stagger_frame[attacker->s.number] != level.time) // not when this clash staggered him (2026-11-10)
			{
				sab_beh_attack_blocked(attacker, blocker, qfalse);
			}

			sab_beh_add_balance(blocker, -MPCOST_PARRIED);
			WP_SaberAIBlockedNote(attacker, blocker); // 2026-11-10: bots wait for his block to finish

			if ((d_attackinfo.integer || g_DebugSaberCombat.integer) && !(blocker->r.svFlags & SVF_BOT))
			{
				Com_Printf(S_COLOR_YELLOW"Attackers Attack was Blocked\n");
			}
		}
		else
		{
			//Backup in case i missed some

			if (!m_blocking)
			{
				if (PM_SaberInnonblockableAttack(blocker->client->ps.torsoAnim))
				{
					sab_beh_animate_heavy_slow_bounce_attacker(attacker);

					sab_beh_add_balance(blocker, -MPCOST_PARRIED);
					if ((d_attackinfo.integer || g_DebugSaberCombat.integer))
					{
						Com_Printf(S_COLOR_ORANGE"Attack an Unblockable attack\n");
					}
				}
				else
				{
					sab_beh_attack_blocked(attacker, blocker, qtrue);

					G_Stagger(blocker);

					if ((d_attackinfo.integer || g_DebugSaberCombat.integer))
					{
						Com_Printf(S_COLOR_ORANGE"Attacker All the rest of the types of contact\n");
					}
				}
			}
		}
	}
	return qtrue;
}

void WP_SaberWearOnBlock(gentity_t* blocker, const gentity_t* attacker);

// Adaptive duel AI (g_saberAIAdapt 1, 2026-11-09, as SJE SP): saber NPCs and bots remember each player's last 6 basic
// swing directions. When a player starts a swing he has already used at least twice in those 6, a blocking NPC / bot
// may read it and perfect-block it (the same perfect block as a player's, with the bounce and the block point cost).
// The chance grows with every repeat and with the difficulty, up to 60%; mixing up the swings avoids it. Each NPC / bot
// can do this once every 6 s (easy) to 3 s (hardest), so it is never spammed. Riposte swings are never read.
#define SABER_ADAPT_HISTORY 6
static int saber_adapt_hist[MAX_CLIENTS][SABER_ADAPT_HISTORY];
static int saber_adapt_read_time[MAX_CLIENTS];
static int saber_adapt_npc_next[MAX_GENTITIES];

// PM_SetSaberMove (bg_saber.c, game side): a player starts a new attack
void WP_SaberAIAdaptRecord(const int clientNum, const int move)
{
	int repeats = 0, chance, i;
	if (!g_saberAIAdapt.integer || clientNum < 0 || clientNum >= MAX_CLIENTS || !WP_SaberAIIsHuman(&g_entities[clientNum]))
	{
		return;
	}
	saber_adapt_read_time[clientNum] = 0;
	if (move < LS_A_TL2BR || move > LS_A_T2B)
	{
		return; // only the 7 basic swing directions
	}
	for (i = 0; i < SABER_ADAPT_HISTORY; i++)
	{
		if (saber_adapt_hist[clientNum][i] == move)
		{
			repeats++;
		}
	}
	for (i = SABER_ADAPT_HISTORY - 1; i > 0; i--)
	{
		saber_adapt_hist[clientNum][i] = saber_adapt_hist[clientNum][i - 1];
	}
	saber_adapt_hist[clientNum][0] = move;
	if (repeats < 2)
	{
		return;
	}
	chance = (repeats - 1) * (10 + 5 * 2); // the reader's own skill is applied in WP_SaberAIAdaptRead
	if (chance > 60)
	{
		chance = 60;
	}
	if (Q_irand(0, 99) < chance)
	{
		saber_adapt_read_time[clientNum] = level.time + 1500;
	}
	if (g_DebugSaberCombat.integer)
	{
		Com_Printf(S_COLOR_YELLOW"Adaptive AI: swing %d used %d times in the last %d, %d%% read chance -> %s\n", move, repeats,
			SABER_ADAPT_HISTORY, chance, saber_adapt_read_time[clientNum] ? "READ" : "not read");
	}
}

// Has a blocking saber NPC / bot read this swing of attacker? Used once: the read is spent by the perfect block.
static qboolean WP_SaberAIAdaptRead(const gentity_t* blocker, const gentity_t* attacker)
{
	int b, skill;
	if (!g_saberAIAdapt.integer || !WP_SaberAIIsAI(blocker) || !WP_SaberAIIsHuman(attacker)
		|| WP_SaberRiposteSwingActive(attacker))
	{
		return qfalse;
	}
	if (saber_adapt_read_time[attacker->s.number] <= level.time)
	{
		return qfalse;
	}
	b = blocker->s.number;
	if (b < 0 || b >= MAX_GENTITIES)
	{
		return qfalse;
	}
	if (saber_adapt_npc_next[b] > level.time && saber_adapt_npc_next[b] - level.time <= 6000)
	{
		return qfalse; // this NPC / bot read a swing a moment ago (the 6000 check drops stale times after a map restart)
	}
	skill = WP_SaberAISkill(blocker);
	if (skill < 2 && Q_irand(0, 3) >= 2 + skill)
	{
		return qfalse; // easier readers miss some reads (SP scales the read chance by skill: 10 + 5 x skill per repeat)
	}
	saber_adapt_read_time[attacker->s.number] = 0;
	saber_adapt_npc_next[b] = level.time + 6000 - 1000 * skill;
	return qtrue;
}

qboolean sab_beh_block_vs_attack(
	gentity_t* blocker,
	gentity_t* attacker,
	const int saberNum,
	const int bladeNum,
	vec3_t hit_loc
)
{
	// Early exits
	if (!blocker || !blocker->client || !attacker) {
		return qfalse;
	}

	if (Rosh_BeingHealed(blocker)) {
		return qfalse;
	}

	if (PM_SuperBreakLoseAnim(blocker->client->ps.torsoAnim) ||
		PM_SuperBreakWinAnim(blocker->client->ps.torsoAnim)) {
		return qfalse;
	}

	WP_SaberWearOnBlock(blocker, attacker); // breakable saber staffs

	//-(Im the blocker)
	const qboolean accurate_parry =
		g_accurate_blocking(blocker, attacker, hit_loc);

	const qboolean blocking = ((blocker->client->ps.ManualBlockingFlags & (1 << MBF_HOLDINGBLOCK)) != 0) ? qtrue : qfalse;

	// a riposte swing can't be perfect-parried (g_saberPerfectParry, 2026-11-09)
	const qboolean m_blocking = ((blocker->client->ps.ManualBlockingFlags & (1 << MBF_PERFECTBLOCKING)) != 0) && !WP_SaberRiposteSwingActive(attacker) ? qtrue : qfalse;

	const qboolean is_holding_block_button_and_attack = ((blocker->client->ps.ManualBlockingFlags & (1 << MBF_HOLDINGBLOCKANDATTACK)) != 0) ? qtrue : qfalse;

	const qboolean npc_blocking = ((blocker->client->ps.ManualBlockingFlags & (1 << MBF_NPCBLOCKING)) != 0) ? qtrue : qfalse;

	// ------------------------------------------------------------
	// NON‑UNBLOCKABLE ATTACKS
	// ------------------------------------------------------------
	if (!PM_SaberInnonblockableAttack(attacker->client->ps.torsoAnim))
	{
		// --------------------------------------------------------
		// LOW BP (<= 20)
		// --------------------------------------------------------
		if (blocker->client->ps.fd.blockPoints <= BLOCKPOINTS_FATIGUE)
		{
			// Very low BP (<= 10)
			if (blocker->client->ps.fd.blockPoints <= BLOCKPOINTS_TEN)
			{
				if (blocker->r.svFlags & SVF_BOT)
				{
					sab_beh_add_mishap_blocker(blocker, attacker);
				}
				else
				{
					SabBeh_SaberShouldBeDisarmedBlocker(blocker, attacker);
				}
				WP_SaberGuardBreakMoment(attacker, blocker); // 2026-11-10

				// The blocker recovers a little if he still holds his saber (a disarm already gives the disarmed
				// fighter these points). 2026-11-10 bug fix (as SP): this gave them to the attacker whenever he was a bot.
				if (!blocker->client->ps.saberInFlight)
				{
					WP_BlockPointsRegenerate(blocker, BLOCKPOINTS_FATIGUE);
				}

				if ((d_blockinfo.integer || g_DebugSaberCombat.integer) &&
					!(blocker->r.svFlags & SVF_BOT))
				{
					Com_Printf(
						S_COLOR_CYAN
						"Blocker was disarmed with very low bp, recharge bp 20bp\n"
					);
				}

				blocker->client->ps.saberEventFlags |= SEF_PARRIED;
				attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
				WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);
			}
			else
			{
				g_fatigue_bp_knockaway(blocker);
				WP_SaberBlockerBlockPoints(blocker, WP_ClassBlockPointCost(blocker, BLOCKPOINTS_DANGER, qfalse));

				if ((d_blockinfo.integer || g_DebugSaberCombat.integer) &&
					!(blocker->r.svFlags & SVF_BOT))
				{
					Com_Printf(S_COLOR_CYAN "Blocker stagger drain 4 bp\n");
				}

				blocker->client->ps.saberEventFlags |= SEF_PARRIED;
				attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
				WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);
			}
		}

		// --------------------------------------------------------
		// NORMAL BP (> 20)
		// --------------------------------------------------------
		else
		{
			// ----------------------------------------------------
			// ACTIVE BLOCK (block + attack)
			// ----------------------------------------------------
			// Adaptive duel AI (g_saberAIAdapt, 2026-11-09): a blocking saber NPC / bot that read this swing perfect-blocks it
			const qboolean npc_read = WP_SaberAIAdaptRead(blocker, attacker);

			if (is_holding_block_button_and_attack || npc_read)
			{
				// PERFECT BLOCK
				if (m_blocking || npc_read)
				{
					WP_SaberMBlockDirection(blocker, hit_loc, qfalse);

					if (WP_SaberAIIsAI(attacker))
					{// 2026-11-10 (DH): a perfect-blocked NPC / bot is staggered
						WP_SaberAIPerfectBlocked(attacker, blocker);
					}
					else if (attacker->client->ps.saberFatigueChainCount >= MISHAPLEVEL_THIRTEEN)
					{
						sab_beh_add_mishap_attacker(attacker, blocker);
					}
					else
					{
						sab_beh_animate_heavy_slow_bounce_attacker(attacker);
						attacker->client->ps.userInt3 |= (1 << FLAG_MBLOCKBOUNCE);
					}

					blocker->client->ps.userInt3 |= (1 << FLAG_PERFECTBLOCK);

					if (blocker->r.svFlags & SVF_BOT)
					{
						g_do_m_block_response(blocker);
					}

					if (!(blocker->r.svFlags & SVF_BOT))
					{
						CGCam_BlockShakeMP(blocker->s.origin, blocker, 0.45f, 100);
					}

					G_Sound(
						blocker,
						CHAN_AUTO,
						G_SoundIndex(va("sound/weapons/saber/saber_perfectblock%d.mp3",
							Q_irand(1, 3)))
					);

					if ((d_blockinfo.integer || g_DebugSaberCombat.integer) &&
						!(blocker->r.svFlags & SVF_BOT))
					{
						Com_Printf(S_COLOR_CYAN "Blocker Perfect blocked reward 15\n");
					}

					blocker->client->ps.saberEventFlags |= SEF_PARRIED;
					attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
					WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);

					WP_BlockPointsRegenerate_over_ride(blocker, BLOCKPOINTS_FIFTEEN);//SAC Reward blocker
					sab_beh_add_balance(blocker, -MPCOST_MBLOCKED); //SAC Reward blocker

					if (WP_SaberAIIsAI(attacker))
					{
						PM_AddBlockFatigue(&attacker->client->ps, SABER_AI_PERFECTBLOCKED_BP); //BP Punish Attacker (2026-11-10, DH: 30, was 20; NPCs too)
						sab_beh_add_balance(attacker, MPCOST_MBLOCKED); //SAC Punish attacker (bug fix 2026-11-10: was BLOCKPOINTS_TEN, a block point value: +10 of the 15 fatigue levels)
					}
					else
					{
						PM_AddBlockFatigue(&attacker->client->ps, BLOCKPOINTS_TEN); //BP Punish Attacker
						sab_beh_add_balance(attacker, MPCOST_MBLOCKED); //SAC Punish attacker
					}
					WP_SaberPerfectParryBonus(blocker, attacker); // g_saberPerfectParry (2026-11-09)
				}

				// ------------------------------------------------
				// SPAM BLOCK + ATTACK
				// ------------------------------------------------
				else
				{
					if (blocker->client->ps.fd.blockPoints <= BLOCKPOINTS_HALF)
					{
						WP_SaberFatiguedParryDirection(blocker, hit_loc, qfalse);
					}
					else if ((attacker->client->ps.fd.saberAnimLevel == SS_DESANN || attacker->client->ps.fd.saberAnimLevel == SS_BLASTER) ||
						attacker->client->ps.fd.saberAnimLevel == SS_STRONG)
					{
						WP_SaberFatiguedParryDirection(blocker, hit_loc, qfalse);
					}
					else
					{
						WP_SaberBlockNonRandom(blocker, hit_loc, qfalse);
					}

					if (attacker->r.svFlags & SVF_BOT)
					{ // SS_BLASTER (2026-10-14)
						sab_beh_add_balance(attacker, MPCOST_MBLOCKED); //SAC Punish attacker
					}

					WP_SaberBlockerBlockPoints(blocker, WP_ClassBlockPointCost(blocker, BLOCKPOINTS_FIVE, qfalse));

					if (!(blocker->r.svFlags & SVF_BOT))
					{
						CGCam_BlockShakeMP(blocker->s.origin, blocker, 0.45f, 100);
					}

					if ((d_blockinfo.integer || g_DebugSaberCombat.integer) &&
						!(blocker->r.svFlags & SVF_BOT))
					{
						Com_Printf(S_COLOR_CYAN "Blocker Spamming block + attack cost 5\n");
					}

					blocker->client->ps.saberEventFlags |= SEF_PARRIED;
					attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
					WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);
				}
			}

			// ----------------------------------------------------
			// HOLDING BLOCK ONLY (spamming block)
			// ----------------------------------------------------
			else if (blocking && !is_holding_block_button_and_attack)
			{
				if (blocker->client->ps.fd.blockPoints <= BLOCKPOINTS_HALF)
				{
					WP_SaberFatiguedParryDirection(blocker, hit_loc, qfalse);
				}
				else if ((attacker->client->ps.fd.saberAnimLevel == SS_DESANN || attacker->client->ps.fd.saberAnimLevel == SS_BLASTER) ||
					attacker->client->ps.fd.saberAnimLevel == SS_STRONG)
				{
					WP_SaberFatiguedParryDirection(blocker, hit_loc, qfalse);
				}
				else
				{
					WP_SaberBouncedSaberDirection(blocker, hit_loc, qfalse);
				}

				if (!(blocker->r.svFlags & SVF_BOT))
				{
					CGCam_BlockShakeMP(blocker->s.origin, blocker, 0.45f, 100);
				}

				if (!(blocker->r.svFlags & SVF_BOT))
				{
					WP_SaberBlockerBlockPoints(blocker, WP_ClassBlockPointCost(blocker, BLOCKPOINTS_FIVE, qfalse));
				} // SS_BLASTER (2026-10-14)

				if (attacker->NPC && !G_ControlledByPlayer(attacker)) //NPC only
				{
					if (attacker->client->ps.fd.blockPoints <= BLOCKPOINTS_FATIGUE)
					{
						WP_BlockPointsRegenerate(attacker, BLOCKPOINTS_FATIGUE);
					}
					else
					{
						WP_BlockPointsRegenerate(attacker, BLOCKPOINTS_TEN);
					}
				}

				if ((d_blockinfo.integer || g_DebugSaberCombat.integer) &&
					!(blocker->r.svFlags & SVF_BOT))
				{
					Com_Printf(
						S_COLOR_CYAN
						"Blocker Holding block button only (spamming block) cost 5\n"
					);
				}

				blocker->client->ps.saberEventFlags |= SEF_PARRIED;
				attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
				WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);
			}

			// ----------------------------------------------------
			// ACCURATE PARRY / NPC PARRY
			// ----------------------------------------------------
			else if (accurate_parry || npc_blocking)
			{
				if ((attacker->client->ps.fd.saberAnimLevel == SS_DESANN || attacker->client->ps.fd.saberAnimLevel == SS_BLASTER) ||
					attacker->client->ps.fd.saberAnimLevel == SS_STRONG)
				{
					int heavy_cost;
					WP_SaberFatiguedParryDirection(blocker, hit_loc, qfalse);

					// 2026-11-10 bug fix: blocking a heavy style cost nothing here. Now it costs the normal block
					// cost for the blocker's block points plus 2.
					if (WP_SaberAIIsAI(blocker))
					{
						heavy_cost = blocker->client->ps.fd.blockPoints <= BLOCKPOINTS_FOURTY ? BLOCKPOINTS_FIVE
							: blocker->client->ps.fd.blockPoints <= BLOCKPOINTS_MISSILE ? BLOCKPOINTS_DANGER : BLOCKPOINTS_THREE;
					}
					else
					{
						heavy_cost = blocker->client->ps.fd.blockPoints <= BLOCKPOINTS_FOURTY ? BLOCKPOINTS_FAIL : BLOCKPOINTS_THREE;
					}
					WP_SaberBlockerBlockPoints(blocker, WP_ClassBlockPointCost(blocker, heavy_cost + 2, qfalse));

					if (d_blockinfo.integer || g_DebugSaberCombat.integer)
					{
						Com_Printf(S_COLOR_CYAN"Blocked a heavy style, cost %d\n", heavy_cost + 2);
					}
				}
				else if (blocker->client->ps.fd.blockPoints <= BLOCKPOINTS_MISSILE)
				{
					if (blocker->client->ps.fd.blockPoints <= BLOCKPOINTS_FOURTY)
					{
						WP_SaberFatiguedParryDirection(blocker, hit_loc, qfalse);

						if ((d_blockinfo.integer || g_DebugSaberCombat.integer) &&
							!(blocker->r.svFlags & SVF_BOT))
						{
							Com_Printf(S_COLOR_CYAN "NPC Fatigued Parry\n");
						}

						WP_SaberBlockerBlockPoints(blocker, WP_ClassBlockPointCost(blocker, WP_SaberAIIsAI(blocker) ? BLOCKPOINTS_FIVE : BLOCKPOINTS_FAIL, qfalse)); // 2026-11-10: NPC / bot 5
					}
					else
					{
						WP_SaberBlockNonRandom(blocker, hit_loc, qfalse);

						if ((d_blockinfo.integer || g_DebugSaberCombat.integer) &&
							!(blocker->r.svFlags & SVF_BOT))
						{
							Com_Printf(S_COLOR_CYAN "NPC normal Parry\n");
						}

						WP_SaberBlockerBlockPoints(blocker, WP_ClassBlockPointCost(blocker, WP_SaberAIIsAI(blocker) ? BLOCKPOINTS_DANGER : BLOCKPOINTS_THREE, qfalse)); // 2026-11-10: NPC / bot 4
					}
				}
				else
				{
					WP_SaberMBlockDirection(blocker, hit_loc, qfalse);

					if (blocker->r.svFlags & SVF_BOT)
					{
						g_do_m_block_response(blocker);
					}

					if ((d_blockinfo.integer || g_DebugSaberCombat.integer) &&
						!(blocker->r.svFlags & SVF_BOT))
					{
						Com_Printf(S_COLOR_CYAN "NPC good Parry\n");
					} // SS_BLASTER (2026-10-14)

					if (blocker->r.svFlags & SVF_BOT)
					{
						if (blocker->client->ps.fd.blockPoints <= BLOCKPOINTS_FULL)
						{
							WP_BlockPointsRegenerate(blocker, BLOCKPOINTS_TEN);
						}
					}
					else
					{
						WP_SaberBlockerBlockPoints(blocker, WP_ClassBlockPointCost(blocker, BLOCKPOINTS_THREE, qfalse));
					}
				}

				G_Sound(
					blocker,
					CHAN_AUTO,
					G_SoundIndex(va("sound/weapons/saber/saber_goodparry%d.mp3",
						Q_irand(1, 3))));

				if ((d_blockinfo.integer || g_DebugSaberCombat.integer) &&
					!(blocker->r.svFlags & SVF_BOT))
				{
					Com_Printf(S_COLOR_CYAN "Blocker Other types of block and npc,s\n");
				}

				blocker->client->ps.saberEventFlags |= SEF_PARRIED;
				attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
				WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);
			}

			// ----------------------------------------------------
			// FAILED BLOCK (not holding block)
			// ----------------------------------------------------
			else
			{
				sab_beh_add_mishap_blocker(blocker, attacker);

				if (!(blocker->r.svFlags & SVF_BOT))
				{
					WP_SaberBlockerBlockPoints(blocker, WP_ClassBlockPointCost(blocker, BLOCKPOINTS_TEN, qfalse));
				}

				if ((d_blockinfo.integer || g_DebugSaberCombat.integer) &&
					!(blocker->r.svFlags & SVF_BOT))
				{
					Com_Printf(S_COLOR_CYAN "Blocker Not holding block drain 10\n");
				}
			}
		}

		// 2026-11-10: a blocked riposte (the counter after a perfect parry) costs the blocker 5 block points more
		if (WP_SaberRiposteSwingActive(attacker) && (blocker->client->ps.saberEventFlags & SEF_PARRIED))
		{
			WP_SaberBlockerBlockPoints(blocker, SABER_RIPOSTE_BLOCK_BP);
			if (d_blockinfo.integer || g_DebugSaberCombat.integer)
			{
				Com_Printf(S_COLOR_CYAN"Blocked a riposte, extra cost 5\n");
			}
		}
	}

	// ------------------------------------------------------------
	// UNBLOCKABLE ATTACKS
	// ------------------------------------------------------------
	else
	{
		if (m_blocking) // Perfect block vs unblockable
		{
			if (!(blocker->r.svFlags & SVF_BOT))
			{
				CGCam_BlockShakeMP(blocker->s.origin, blocker, 0.45f, 100);
			}

			blocker->client->ps.userInt3 |= (1 << FLAG_PERFECTBLOCK);

			G_Sound(
				blocker,
				CHAN_AUTO,
				G_SoundIndex(va("sound/weapons/saber/saber_perfectblock%d.mp3",
					Q_irand(1, 3)))
			);

			if ((d_blockinfo.integer || g_DebugSaberCombat.integer) &&
				!(blocker->r.svFlags & SVF_BOT))
			{
				Com_Printf(
					S_COLOR_MAGENTA
					"Blocker Perfect blocked an Unblockable attack reward 15\n"
				);
			}

			blocker->client->ps.saberEventFlags |= SEF_PARRIED;
			attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
			WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);

			WP_BlockPointsRegenerate_over_ride(blocker, BLOCKPOINTS_FIFTEEN);
			PM_AddBlockFatigue(&attacker->client->ps, WP_SaberAIIsAI(attacker) ? SABER_AI_PERFECTBLOCKED_BP : BLOCKPOINTS_TEN); //BP Punish Attacker (NPC / bot 30, 2026-11-10)
			sab_beh_add_balance(blocker, -MPCOST_MBLOCKED); //SAC Reward blocker
			sab_beh_add_balance(attacker, MPCOST_MBLOCKED); //SAC Punish attacker
			WP_SaberPerfectParryBonus(blocker, attacker); // g_saberPerfectParry (2026-11-09)
			// the attacker's massive stagger and stamina loss: sab_beh_attack_vs_block
		}
		else
		{
			if (blocker->client->ps.fd.blockPoints < BLOCKPOINTS_TEN)
			{
				SabBeh_SaberShouldBeDisarmedBlocker(blocker, attacker);
				WP_BlockPointsRegenerate_over_ride(blocker, BLOCKPOINTS_FATIGUE);
				WP_SaberGuardBreakMoment(attacker, blocker);
			}
			else
			{
				// 2026-11-10 (DH): blocking an unblockable attack (kata) without a perfect block gives the blocker
				// the massive stagger and a big block point loss (was a knockaway and 10)
				if (!WP_SaberStaggerOnce(blocker))
				{
					g_fatigue_bp_knockaway(blocker);
				}
				WP_SaberBlockerBlockPoints(blocker, WP_ClassBlockPointCost(blocker, SABER_UNBLOCKABLE_BLOCK_BP, qfalse));
				if (WP_SaberIsPlayer(blocker))
				{
					CGCam_BlockShakeMP(blocker->s.origin, blocker, 0.6f, 250);
				}
			}

			if (d_blockinfo.integer || g_DebugSaberCombat.integer)
			{
				Com_Printf(S_COLOR_MAGENTA "Blocker can not block Unblockable\n");
			}

			blocker->client->ps.saberEventFlags &= ~SEF_PARRIED;
		}
	}

	return qtrue;
}

/////////Functions//////////////
//
/////////////////////// 2026 new build ////////////////////////////////

/*
-------------------------
Breakable saber staffs (saber wear) - the same rules as SP

A saber with brokenSaber1 in its .sab (Academy_staff_1, dual_1) wears down in a fight and breaks into its
brokenSaber1 (right hand) / brokenSaber2 (left hand; "none": one half). Wear 0-100 in ps.stats[STAT_SABER_WEAR]
(the HUD tints the saber style icon by it). g_saberBreaking: 0 off, 1 NPCs only, 2 everyone. Boss NPCs never break.
Wear: a hit on the hilt 20; blocking a heavy attack (strong / desann / tavion style, a special, unblockable) 4, medium
2, fast 1; a perfect block 0. x2 with no block points, x2 the attacker's Sith sword, x1.5 the attacker's saber
damageScale > 1, x1.5 the attacker's Force Rage. Recovers 1 a second after 8 s without wear, but once under 25 left
(cracked, wear > 75) never back above 25. At 100 it breaks at the end of the swing: sparks, the pieces, a stagger -
or from a heavy hit a knockdown and the right-hand half knocked away (Force pull / pick it up for dual again). Saber
damage is halved for 1.5 s after a break. A player gets the staff back when they respawn (userinfo is not changed).
-------------------------
*/
#define SABERWEAR_BREAK			100
#define SABERWEAR_BREAK_HEAVY	101
#define SABERWEAR_CRACKED		75
#define SABERWEAR_RECOVER_DELAY	8000

extern void G_Knockdown(gentity_t* self, gentity_t* attacker, const vec3_t push_dir, float strength, qboolean breakSaberLock);
extern qboolean WP_saberKnockOutOfHand(gentity_t* saberent, gentity_t* saber_owner, vec3_t velocity);
extern qboolean PM_SaberInStart(int move);
extern qboolean PM_SaberInTransition(int move);
extern qboolean PM_SaberInAttack(int move);
extern qboolean BG_SabersOff(const playerState_t* ps);
extern qboolean G_SetSaber(const gentity_t* ent, int saberNum, const char* saber_name, qboolean siege_override);
extern void WP_SetSaber(int entNum, saberInfo_t* sabers, int saberNum, const char* saber_name);
extern void G_SaberModelSetupAll(const gentity_t* ent);

static qboolean WP_SaberWearBoss(const gentity_t* ent)
{
	if (ent->s.number < MAX_CLIENTS)
	{
		return qfalse;
	}
	switch (ent->client->NPC_class)
	{
	case CLASS_DESANN:
	case CLASS_TAVION:
	case CLASS_LUKE:
	case CLASS_KYLE:
	case CLASS_ALORA:
	case CLASS_VADER:
	case CLASS_YODA:
		return qtrue;
	default:
		return qfalse;
	}
}

// this one's saber can wear down and break
static qboolean WP_SaberBreakable(const gentity_t* ent)
{
	if (!ent || !ent->client || ent->health <= 0 || g_saberBreaking.integer <= 0)
	{
		return qfalse;
	}
	if (g_saberBreaking.integer == 1 && ent->s.number < MAX_CLIENTS)
	{
		return qfalse;
	}
	if (ent->client->ps.weapon != WP_SABER || ent->client->saber[1].model[0])
	{
		return qfalse;
	}
	if (!ent->client->saber[0].brokenSaber1[0])
	{
		return qfalse;
	}
	return WP_SaberWearBoss(ent) ? qfalse : qtrue;
}

static void WP_SaberWearAdd(gentity_t* victim, const gentity_t* attacker, float amount, const qboolean heavy)
{
	if (!WP_SaberBreakable(victim))
	{
		return;
	}
	playerState_t* ps = &victim->client->ps;
	if (ps->saberInFlight || ps->saberLockTime > level.time)
	{
		return;
	}
	if (attacker && attacker->client)
	{
		if (attacker->client->saber[0].type == SABER_SITH_SWORD)
		{
			amount *= 2.0f;
		}
		if (attacker->client->saber[0].damageScale > 1.0f)
		{
			amount *= 1.5f;
		}
		if (attacker->client->ps.fd.forcePowersActive & 1 << FP_RAGE)
		{
			amount *= 1.5f;
		}
	}
	if (ps->fd.blockPoints <= BLOCKPOINTS_FATIGUE)
	{
		amount *= 2.0f;
	}
	int wear = ps->stats[STAT_SABER_WEAR];
	if (wear < SABERWEAR_BREAK)
	{
		wear += (int)(amount + 0.999f);
	}
	if (wear >= SABERWEAR_BREAK)
	{
		wear = heavy || wear == SABERWEAR_BREAK_HEAVY ? SABERWEAR_BREAK_HEAVY : SABERWEAR_BREAK;
	}
	ps->stats[STAT_SABER_WEAR] = wear;
	victim->client->saberWearTimer = SABERWEAR_RECOVER_DELAY;
}

// the blocker's saber takes the attack (sab_beh_block_vs_attack)
void WP_SaberWearOnBlock(gentity_t* blocker, const gentity_t* attacker)
{
	if (!attacker || !attacker->client || !WP_SaberBreakable(blocker))
	{
		return;
	}
	const playerState_t* bps = &blocker->client->ps;
	const playerState_t* aps = &attacker->client->ps;
	const qboolean unblockable = PM_SaberInnonblockableAttack(aps->torsoAnim);

	if (!unblockable
		&& bps->ManualBlockingFlags & 1 << MBF_PERFECTBLOCKING
		&& bps->ManualBlockingFlags & 1 << MBF_HOLDINGBLOCKANDATTACK
		&& bps->fd.blockPoints > BLOCKPOINTS_FATIGUE)
	{
		return; // a perfect block: no wear
	}
	float amount = 0.0f;
	qboolean heavy = qfalse;
	if (unblockable || PM_SaberInSpecialAttack(aps->torsoAnim))
	{
		amount = 4.0f;
		heavy = qtrue;
	}
	else
	{
		switch (aps->fd.saberAnimLevel)
		{
		case SS_STRONG:
		case SS_DESANN:
		case SS_BLASTER: // SS_BLASTER (2026-10-14)
		case SS_TAVION:
			amount = 4.0f;
			heavy = qtrue;
			break;
		case SS_MEDIUM:
			amount = 2.0f;
			break;
		default:
			amount = 1.0f;
			break;
		}
	}
	WP_SaberWearAdd(blocker, attacker, amount, heavy);
}

// a saber trace just hit victim's ghoul2 (G_G2TraceCollide): wear if it hit the hilt of a breakable saber
void WP_SaberWearHiltTrace(gentity_t* victim, const gentity_t* attacker)
{
	char surf_name[MAX_QPATH] = { 0 };

	if (!victim || !victim->client || !victim->ghoul2 || !attacker || !attacker->client || attacker == victim)
	{
		return;
	}
	gclient_t* client = victim->client;
	if (client->g2LastSurfaceTime != level.time || client->g2LastSurfaceModel <= G2MODEL_PLAYER
		|| client->saberWearHiltDebounce > level.time || !WP_SaberBreakable(victim))
	{
		return;
	}
	surf_name[0] = 0;
	trap->G2API_GetSurfaceName(victim->ghoul2, client->g2LastSurfaceHit, client->g2LastSurfaceModel, surf_name);
	if (!surf_name[0] || (Q_stricmpn("w_", surf_name, 2) && Q_stricmpn("saber", surf_name, 5) && Q_stricmp("cylinder01", surf_name)))
	{
		return;
	}
	client->saberWearHiltDebounce = level.time + 500;
	WP_SaberWearAdd(victim, attacker, attacker->client->saber[0].type == SABER_SITH_SWORD ? 40.0f : 20.0f, qtrue);
}

// swap the staff for its broken pieces: brokenSaber1 in the right hand, brokenSaber2 in the left, dual style
static qboolean WP_SaberWearSwap(gentity_t* ent)
{
	gclient_t* client = ent->client;
	char piece1[SABER_NAME_LENGTH];
	char piece2[SABER_NAME_LENGTH];
	const saber_colors_t color1 = client->saber[0].blade[0].color;
	const saber_colors_t color2 = client->saber[0].numBlades > 1 ? client->saber[0].blade[1].color : color1;

	Q_strncpyz(piece1, client->saber[0].brokenSaber1, sizeof piece1);
	Q_strncpyz(piece2, client->saber[0].brokenSaber2[0] ? client->saber[0].brokenSaber2 : "none", sizeof piece2);
	if (!piece1[0])
	{
		return qfalse;
	}

	if (ent->s.number < MAX_CLIENTS)
	{
		// pers.saber1/2 + the configstring: every client sees the pieces (the colours are the player's color1/color2)
		G_SetSaber(ent, 0, piece1, qtrue);
		G_SetSaber(ent, 1, piece2, qtrue);
		client_userinfo_changed(ent->s.number);
	}
	else
	{
		WP_SetSaber(ent->s.number, client->saber, 0, piece1);
		WP_SetSaber(ent->s.number, client->saber, 1, piece2);
		ent->s.npcSaber1 = G_model_index(va("@%s", piece1));
		ent->s.npcSaber2 = client->saber[1].model[0] ? G_model_index(va("@%s", piece2)) : 0;
	}
	for (int i = 0; i < MAX_BLADES; i++)
	{
		client->saber[0].blade[i].color = color1;
		client->saber[1].blade[i].color = color2;
	}
	G_SaberModelSetupAll(ent);

	if (client->saber[1].model[0])
	{
		client->ps.fd.saber_anim_levelBase = client->ps.fd.saberAnimLevel = client->ps.fd.saberDrawAnimLevel = SS_DUAL;
		client->saberCycleQueue = SS_DUAL;
	}
	return qtrue;
}

static void WP_SaberWearBreak(gentity_t* ent, const qboolean heavy)
{
	gclient_t* client = ent->client;
	vec3_t fwd, push, org, up = { 0, 0, 1 };

	client->ps.stats[STAT_SABER_WEAR] = 0;
	client->saberWearTimer = 0;

	VectorCopy(client->lastSaberBase_Always, org);
	G_PlayEffectID(G_EffectIndex("sparks/spark_explosion"), org, up);
	G_Sound(ent, CHAN_AUTO, G_SoundIndex("sound/weapons/saber/saberoffquick.mp3"));

	if (!WP_SaberWearSwap(ent))
	{
		return;
	}
	client->saberBreakSafeTime = level.time + 1500;

	AngleVectors(client->ps.viewangles, fwd, NULL, NULL);
	fwd[2] = 0;
	VectorNormalize(fwd);
	VectorScale(fwd, -1.0f, push);
	if (client->ps.groundEntityNum != ENTITYNUM_NONE)
	{
		if (heavy)
		{
			// knocked down, the right-hand half knocked away
			G_Knockdown(ent, ent, push, 300, qtrue);
			if (client->saber[1].model[0] && client->ps.saberEntityNum)
			{
				vec3_t throw_vel;
				VectorScale(push, 150.0f, throw_vel);
				throw_vel[2] = 350.0f;
				WP_saberKnockOutOfHand(&g_entities[client->ps.saberEntityNum], ent, throw_vel);
			}
		}
		else
		{
			VectorMA(client->ps.velocity, 150.0f, push, client->ps.velocity);
			G_Stagger(ent);
		}
	}
	else
	{
		VectorMA(client->ps.velocity, heavy ? 300.0f : 150.0f, push, client->ps.velocity);
	}
	if (ent->s.number < MAX_CLIENTS)
	{
		trap->SendServerCommand(ent->s.number, "cp \"Your saber staff broke!\"");
	}
}

// every client think: recovery, the cracked sparks, the break when it is due
void WP_SaberWearThink(gentity_t* ent, const int msec)
{
	if (!ent || !ent->client)
	{
		return;
	}
	gclient_t* client = ent->client;
	playerState_t* ps = &client->ps;
	if (!WP_SaberBreakable(ent))
	{
		ps->stats[STAT_SABER_WEAR] = 0;
		client->saberWearTimer = 0;
		return;
	}
	const int wear = ps->stats[STAT_SABER_WEAR];
	if (wear >= SABERWEAR_BREAK)
	{
		// at the end of the swing (not while it is thrown, holstered or locked)
		if (ps->saberInFlight || BG_SabersOff(ps) || ps->saberLockTime > level.time
			|| PM_SaberInStart(ps->saberMove) || PM_SaberInTransition(ps->saberMove) || PM_SaberInAttack(ps->saberMove))
		{
			return;
		}
		WP_SaberWearBreak(ent, wear == SABERWEAR_BREAK_HEAVY ? qtrue : qfalse);
		return;
	}
	if (wear > SABERWEAR_CRACKED && !BG_SabersOff(ps) && Q_irand(0, 1500) < msec)
	{
		vec3_t org, up = { 0, 0, 1 };
		VectorCopy(client->lastSaberBase_Always, org);
		G_PlayEffectID(G_EffectIndex("sparks/spark_nosnd"), org, up);
	}
	if (wear > 0)
	{
		client->saberWearTimer -= msec;
		if (client->saberWearTimer <= 0)
		{
			const int wear_floor = wear > SABERWEAR_CRACKED ? SABERWEAR_CRACKED + 1 : 0;
			if (wear > wear_floor)
			{
				ps->stats[STAT_SABER_WEAR] = wear - 1;
			}
			client->saberWearTimer = 1000;
		}
	}
}