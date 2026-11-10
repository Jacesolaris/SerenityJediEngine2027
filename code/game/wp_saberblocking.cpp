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
#include "wp_saber.h"
#include <qcommon\q_math.h>
#include <qcommon\q_shared.h>
#include "bg_public.h"
#include <qcommon/q_color.h>
#include "g_shared.h"
#include <qcommon\q_platform.h>

//////////Defines////////////////
extern qboolean PM_KickingAnim(int anim);
extern qboolean PM_SaberInNonIdleDamageMove(const playerState_t* ps);
extern qboolean InFront(vec3_t spot, vec3_t from, vec3_t from_angles, float thresh_hold = 0.0f);
extern qboolean PM_SaberInKnockaway(int move);
extern qboolean PM_SaberInBounce(const int move);
extern qboolean BG_InSlowBounce(const playerState_t* ps);
extern qboolean G_ControlledByPlayer(const gentity_t* self);
extern qboolean PM_InKnockDown(const playerState_t* ps);
extern qboolean PM_SaberInTransitionAny(int move);
extern void G_AddVoiceEvent(const gentity_t* self, int event, int speak_debounce_time);
extern qboolean npc_is_dark_jedi(const gentity_t* self);
extern saberMoveName_t pm_broken_parry_for_attack(int move);
extern qboolean PM_InGetUp(const playerState_t* ps);
extern qboolean PM_InForceGetUp(const playerState_t* ps);
extern qboolean PM_SaberInParry(int move);
extern saberMoveName_t PM_KnockawayForParry(int move);
extern int G_KnockawayForParry(int move);
extern qboolean WP_SaberLose(gentity_t* self, vec3_t throw_dir);
extern cvar_t* g_saberAutoBlocking;
extern qboolean WP_SabersCheckLock(gentity_t* ent1, gentity_t* ent2);
extern qboolean WalkCheck(const gentity_t* self);
extern qboolean PM_SuperBreakWinAnim(int anim);
extern void PM_AddFatigue(playerState_t* ps, int fatigue);
extern qboolean WP_SaberBlockNonRandom(gentity_t* self, vec3_t hitloc, qboolean missileBlock);
extern saberMoveName_t PM_BrokenParryForParry(int move);
extern void PM_AddBlockFatigue(playerState_t* ps, int fatigue);
extern int WP_ClassBlockPointCost(const gentity_t* blocker, int cost, qboolean vs_blaster);
extern void G_Stagger(gentity_t* hitEnt);
extern void g_fatigue_bp_knockaway(gentity_t* blocker);
extern void G_StaggerAttacker(gentity_t* atk);
extern void G_BounceAttacker(gentity_t* atk);
extern void WP_BlockPointsRegenerate(const gentity_t* self, const int override_amt);
extern saberMoveName_t PM_SaberBounceForAttack(int move);
extern qboolean PM_SaberInnonblockableAttack(int anim);
extern qboolean PM_SaberInSpecialAttack(int anim);
extern qboolean PM_SaberInKata(saberMoveName_t saberMove);
extern void WP_SaberClearDamageForEntNum(gentity_t* attacker, const int entityNum, const int saberNum, const int bladeNum);
extern cvar_t* d_slowmoaction;
extern void G_StartStasisEffect(const gentity_t* ent, int me_flags = 0, int length = 1000, float time_scale = 0.0f,
	int spin_time = 0);
extern void CGCam_BlockShakeSP(float intensity, int duration);
extern int G_GetParryForBlock(int block);
extern qboolean WP_SaberDisarmed(gentity_t* self, vec3_t throw_dir);
extern void WP_BlockPointsRegenerate_over_ride(const gentity_t* self, const int override_amt);
// Saber Blocks
extern qboolean WP_SaberMBlock(gentity_t* victim, gentity_t* attacker, int saberNum, int bladeNum);
extern qboolean WP_SaberParry(gentity_t* blocker, gentity_t* attacker, int saberNum, int bladeNum);
extern qboolean WP_SaberBlockedBounceBlock(gentity_t* blocker, gentity_t* attacker, int saberNum, int bladeNum);
extern qboolean WP_SaberFatiguedParry(gentity_t* blocker, gentity_t* attacker, int saberNum, int bladeNum);
void sab_beh_animate_heavy_slow_bounce_attacker(gentity_t* attacker);
extern cvar_t* g_DebugSaberCombat;
extern qboolean PM_InSaberLock(int anim);
extern void g_do_m_block_response(const gentity_t* speaker_npc_self);
extern qboolean Rosh_BeingHealed(const gentity_t* self);
extern qboolean G_InCinematicSaberAnim(const gentity_t* self);
extern qboolean PM_SuperBreakLoseAnim(int anim);
///////////Defines////////////////

//////////Actions////////////////

static void sab_beh_saber_should_be_disarmed_attacker(gentity_t* attacker, const int saberNum)
{
	if (saberNum == 0)
	{
		//can only lose right-hand saber for now
		if (!(attacker->client->ps.saber[saberNum].saberFlags & SFL_NOT_DISARMABLE))
		{
			//knocked the saber right out of his hand!
			vec3_t throw_dir = { 0, 0, 350 };

			G_Stagger(attacker);

			WP_SaberDisarmed(attacker, throw_dir);
		}
	}
}

void SabBeh_SaberShouldBeDisarmedBlocker(gentity_t* blocker, const int saberNum)
{
	if (saberNum == 0)
	{
		//can only lose right-hand saber for now
		if (!(blocker->client->ps.saber[saberNum].saberFlags & SFL_NOT_DISARMABLE))
		{
			//knocked the saber right out of his hand!
			vec3_t throw_dir = { 0, 0, 350 };

			G_Stagger(blocker);

			WP_SaberDisarmed(blocker, throw_dir);
		}
	}
}

qboolean g_accurate_blocking(const gentity_t* blocker, const gentity_t* attacker, vec3_t hit_loc)
{
	vec3_t p_angles;
	vec3_t p_right;
	vec3_t parrier_move = { 0 };
	vec3_t hit_pos;
	vec3_t hit_flat = { 0 };

	// MP uses 0.0f tolerance
	const qboolean in_front_of_me =
		InFront(attacker->client->ps.origin,
			blocker->client->ps.origin,
			blocker->client->ps.viewangles,
			0.0f);

	// ------------------------------------------------------------
	// Manual block requirement (players only)
	// ------------------------------------------------------------
	if (blocker->s.number < MAX_CLIENTS || G_ControlledByPlayer(blocker))
	{
		if (!((blocker->client->ps.ManualBlockingFlags & (1 << MBF_HOLDINGBLOCK)) != 0))
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
	if (PM_SaberInNonIdleDamageMove(&blocker->client->ps)
		|| PM_SaberInBounce(blocker->client->ps.saberMove)
		|| BG_InSlowBounce(&blocker->client->ps))
		return qfalse;

	// Cannot parry while ducked
	if (blocker->client->ps.pm_flags & PMF_DUCKED)
		return qfalse;

	// Cannot parry while knocked down
	if (PM_InKnockDown(&blocker->client->ps))
		return qfalse;

	// Held block too long → too slow to parry.
	// ManualblockStartTime is the time the block started (0 = not holding, e.g. NPCs), not a duration:
	// comparing it with 3000 made every accurate block fail once a map had run 3 seconds.
	if (blocker->client->ps.ManualblockStartTime > 0
		&& level.time - blocker->client->ps.ManualblockStartTime >= 3000)
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

	// MP uses fixed -10 vertical offset
	hit_flat[2] = hit_pos[2] - 10;

	VectorNormalize(hit_flat);

	// Player's intended parry direction (usercmd: pers.cmd is never filled in SP, so the player never got a directional parry)
	parrier_move[0] = 0;
	parrier_move[1] = blocker->client->usercmd.rightmove;
	parrier_move[2] = -blocker->client->usercmd.forwardmove;

	VectorNormalize(parrier_move);

	// ------------------------------------------------------------
	// Style-based threshold
	// ------------------------------------------------------------
	float threshold = 0.40f; // default

	switch (blocker->client->ps.saberAnimLevel)
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
	// NPC fallback (MP logic)
	// ------------------------------------------------------------
	if (blocker->NPC && !G_ControlledByPlayer(blocker))
	{
		if (NPC_PARRYRATE * g_spskill->integer > Q_irand(0, 999))
			return qtrue;
	}

	return qfalse;
}

static void sab_beh_add_mishap_attacker(gentity_t* attacker, const gentity_t* blocker, const int saberNum)
{
	if (attacker->client->ps.blockPoints <= MISHAPLEVEL_NONE)
	{
		attacker->client->ps.blockPoints = MISHAPLEVEL_NONE;
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
			if (blocker->NPC && !G_ControlledByPlayer(blocker)) //NPC only
			{
				if (!Q_irand(0, 4))
				{
					//20% chance
					sab_beh_animate_heavy_slow_bounce_attacker(attacker);
					if (d_attackinfo->integer || g_DebugSaberCombat->integer && (attacker->NPC && !G_ControlledByPlayer(attacker)))
					{
						gi.Printf(S_COLOR_YELLOW"NPC Attacker staggering\n");
					}
				}
				else
				{
					sab_beh_saber_should_be_disarmed_attacker(attacker, saberNum);
					if (d_attackinfo->integer || g_DebugSaberCombat->integer && (attacker->NPC && !G_ControlledByPlayer(attacker)))
					{
						gi.Printf(S_COLOR_RED"NPC Attacker lost his saber\n");
					}
				}
			}
			else
			{
				sab_beh_saber_should_be_disarmed_attacker(attacker, saberNum);
				if (d_attackinfo->integer || g_DebugSaberCombat->integer && (attacker->s.number < MAX_CLIENTS || G_ControlledByPlayer(attacker)))
				{
					gi.Printf(S_COLOR_RED"Player Attacker lost his saber\n");
				}
			}
			break;
		case 1:
		default: // 2026-11-10: case 2 did nothing, so a third of all full mishaps went unpunished
			sab_beh_animate_heavy_slow_bounce_attacker(attacker);
			if (d_attackinfo->integer || g_DebugSaberCombat->integer && (attacker->s.number < MAX_CLIENTS || G_ControlledByPlayer(attacker)))
			{
				gi.Printf(S_COLOR_RED"Player Attacker staggering\n");
			}
			break;
		}
	}
}

static void sab_beh_add_mishap_Fake_attacker(gentity_t* attacker, const gentity_t* blocker, const int saberNum)
{
	if (attacker->client->ps.blockPoints <= MISHAPLEVEL_NONE)
	{
		attacker->client->ps.blockPoints = MISHAPLEVEL_NONE;
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
			if (blocker->NPC && !G_ControlledByPlayer(blocker)) //NPC only
			{
				if (!Q_irand(0, 4))
				{
					//20% chance
					sab_beh_saber_should_be_disarmed_attacker(attacker, saberNum);
					if (d_attackinfo->integer || g_DebugSaberCombat->integer && (attacker->NPC && !G_ControlledByPlayer(attacker)))
					{
						gi.Printf(S_COLOR_RED"NPC Attacker lost his saber\n");
					}
				}
				else
				{
					sab_beh_animate_heavy_slow_bounce_attacker(attacker);
					if (d_attackinfo->integer || g_DebugSaberCombat->integer && (attacker->NPC && !G_ControlledByPlayer(attacker)))
					{
						gi.Printf(S_COLOR_YELLOW"NPC Attacker staggering\n");
					}
				}
			}
			else
			{
				sab_beh_saber_should_be_disarmed_attacker(attacker, saberNum);
				if (d_attackinfo->integer || g_DebugSaberCombat->integer && (attacker->s.number < MAX_CLIENTS || G_ControlledByPlayer(attacker)))
				{
					gi.Printf(S_COLOR_RED"Player Attacker lost his saber\n");
				}
			}
			break;
		case 1:
		default: // 2026-11-10: case 2 did nothing, so a third of all parried feints went unpunished
			sab_beh_animate_heavy_slow_bounce_attacker(attacker);
			if (d_attackinfo->integer || g_DebugSaberCombat->integer && (attacker->s.number < MAX_CLIENTS || G_ControlledByPlayer(attacker)))
			{
				gi.Printf(S_COLOR_RED"Player Attacker staggering\n");
			}
			break;
		}
	}
}

static void sab_beh_add_mishap_blocker(gentity_t* blocker, const int saberNum)
{
	if (blocker->client->ps.blockPoints <= MISHAPLEVEL_NONE)
	{
		blocker->client->ps.blockPoints = MISHAPLEVEL_NONE;
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
			if (d_blockinfo->integer || g_DebugSaberCombat->integer)
			{
				gi.Printf(S_COLOR_RED"blocker staggering\n");
			}
			break;
		case 1:
			if (blocker->NPC && !G_ControlledByPlayer(blocker)) //NPC only
			{
				// 75% chance to disarm, 25% chance to stagger
				const int roll = Q_irand(0, 3);  // values: 0,1,2,3

				if (roll == 0)
				{
					// 25% chance
					G_Stagger(blocker);

					if (d_blockinfo->integer || g_DebugSaberCombat->integer)
					{
						gi.Printf(S_COLOR_YELLOW "NPC blocker staggering\n");
					}
				}
				else
				{
					// 75% chance
					SabBeh_SaberShouldBeDisarmedBlocker(blocker, saberNum);
					WP_BlockPointsRegenerate_over_ride(blocker, BLOCKPOINTS_FATIGUE);

					if (d_blockinfo->integer || g_DebugSaberCombat->integer)
					{
						gi.Printf(S_COLOR_RED "NPC blocker lost his saber\n");
					}
				}
			}
			else
			{
				SabBeh_SaberShouldBeDisarmedBlocker(blocker, saberNum);
				if (d_blockinfo->integer || g_DebugSaberCombat->integer)
				{
					gi.Printf(S_COLOR_RED"Player blocker lost his saber\n");
				}
			}
			break;
		default:;
		}
	}
}

////////Attacker Bounces//////////

void sab_beh_animate_heavy_slow_bounce_attacker(gentity_t* attacker)
{
	attacker->client->ps.userInt3 |= 1 << FLAG_SLOWBOUNCE;
	attacker->client->ps.userInt3 |= 1 << FLAG_OLDSLOWBOUNCE;
	G_StaggerAttacker(attacker);
}

static void sab_beh_animate_small_bounce(gentity_t* attacker)
{
	if (attacker->NPC && !G_ControlledByPlayer(attacker)) //NPC only
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

////////Blocker Bounces//////////

void sab_beh_animate_slow_bounce_blocker(gentity_t* blocker)
{
	blocker->client->ps.userInt3 |= 1 << FLAG_SLOWBOUNCE;
	blocker->client->ps.userInt3 |= 1 << FLAG_OLDSLOWBOUNCE;

	G_AddEvent(blocker, Q_irand(EV_PUSHED1, EV_PUSHED3), 0);

	blocker->client->ps.saberBounceMove = PM_BrokenParryForParry(G_GetParryForBlock(blocker->client->ps.saberBlocked));
	blocker->client->ps.saberBlocked = BLOCKED_PARRY_BROKEN;
}

////////Bounces//////////

static qboolean sab_beh_attack_blocked(gentity_t* attacker, gentity_t* blocker, const int saberNum, const qboolean force_mishap)
{
	//if the attack is blocked -(Im the attacker)
	const qboolean m_blocking = blocker->client->ps.ManualBlockingFlags & 1 << MBF_PERFECTBLOCKING ? qtrue : qfalse;
	//perfect Blocking (Timed Block)

	if (attacker->client->ps.saberFatigueChainCount >= MISHAPLEVEL_MAX)
	{
		//hard mishap.

		if (attacker->NPC && !G_ControlledByPlayer(attacker)) //NPC only
		{
			if (!Q_irand(0, 4))
			{
				//20% chance
				sab_beh_add_mishap_attacker(attacker, blocker, saberNum);
			}
			else
			{
				sab_beh_animate_heavy_slow_bounce_attacker(attacker);
			}
			if (d_attackinfo->integer || g_DebugSaberCombat->integer)
			{
				gi.Printf(S_COLOR_GREEN"Attacker npc is fatigued\n");
			}

			attacker->client->ps.saberFatigueChainCount = MISHAPLEVEL_MIN;
		}
		else
		{
			if (d_attackinfo->integer || g_DebugSaberCombat->integer)
			{
				gi.Printf(S_COLOR_GREEN"Attacker player is fatigued\n");
			}
			sab_beh_add_mishap_attacker(attacker, blocker, saberNum);
		}
		return qtrue;
	}
	if (attacker->client->ps.saberFatigueChainCount >= MISHAPLEVEL_HUDFLASH)
	{
		//slow bounce
		if (attacker->s.number < MAX_CLIENTS || G_ControlledByPlayer(attacker))
		{
			sab_beh_animate_heavy_slow_bounce_attacker(attacker);
		}
		else
		{
			sab_beh_animate_small_bounce(attacker);
		}

		if (attacker->NPC && !G_ControlledByPlayer(attacker)) //NPC only
		{
			attacker->client->ps.saberFatigueChainCount = MISHAPLEVEL_LIGHT;
		}

		if (d_attackinfo->integer || g_DebugSaberCombat->integer)
		{
			if (attacker->s.number < MAX_CLIENTS || G_ControlledByPlayer(attacker))
			{
				gi.Printf(S_COLOR_GREEN"player attack stagger\n");
			}
			else
			{
				gi.Printf(S_COLOR_GREEN"npc attack stagger\n");
			}
		}
		return qtrue;
	}
	if (attacker->client->ps.saberFatigueChainCount >= MISHAPLEVEL_LIGHT)
	{
		//slow bounce
		sab_beh_animate_small_bounce(attacker);

		if (d_attackinfo->integer || g_DebugSaberCombat->integer)
		{
			if (attacker->s.number < MAX_CLIENTS || G_ControlledByPlayer(attacker))
			{
				gi.Printf(S_COLOR_GREEN"player light blocked bounce\n");
			}
			else
			{
				gi.Printf(S_COLOR_GREEN"npc light blocked bounce\n");
			}
		}
		return qtrue;
	}
	if (force_mishap)
	{
		//two attacking sabers bouncing off each other
		sab_beh_animate_small_bounce(attacker);
		sab_beh_animate_small_bounce(blocker);

		if (d_attackinfo->integer || g_DebugSaberCombat->integer)
		{
			if (attacker->s.number < MAX_CLIENTS || G_ControlledByPlayer(attacker))
			{
				gi.Printf(S_COLOR_GREEN"player two attacking sabers bouncing off each other\n");
			}
			else
			{
				gi.Printf(S_COLOR_GREEN"npc two attacking sabers bouncing off each other\n");
			}
		}
		return qtrue;
	}
	if (!m_blocking)
	{
		if (d_attackinfo->integer || g_DebugSaberCombat->integer)
		{
			if (attacker->s.number < MAX_CLIENTS || G_ControlledByPlayer(attacker))
			{
				gi.Printf(S_COLOR_GREEN"player blocked bounce\n");
			}
			else
			{
				gi.Printf(S_COLOR_GREEN"npc blocked bounce\n");
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
	else if (self->client->ps.saberFatigueChainCount >= MISHAPLEVEL_OVERLOAD)
	{
		self->client->ps.saberFatigueChainCount = MISHAPLEVEL_MAX;
	}
}

//////////Actions////////////////

/////////Functions//////////////

// 2026-11-10 (audit fix): this took one saberNum, the saber of the fighter that made the hit, and used it for both
// fighters (disarm + mishap). Now each side gets its own: the hitter's saberNum and 0 (main saber) for the other one.
static qboolean sab_beh_attack_vs_attack(gentity_t* attacker, gentity_t* blocker, const int attacker_saber, const int blocker_saber)
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

		if (WP_SabersCheckLock(attacker, blocker))
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
		if (WP_SabersCheckLock(blocker, attacker))
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
		if (WP_SabersCheckLock(attacker, blocker))
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
	else if (PM_SaberInKata(static_cast<saberMoveName_t>(attacker->client->ps.saberMove)))
	{
		sab_beh_add_balance(attacker, MPCOST_PARRIED);
		//set otherOwner
		sab_beh_add_balance(blocker, -MPCOST_PARRIED);

		if (blocker->client->ps.blockPoints < BLOCKPOINTS_TEN)
		{
			//Low points = bad blocks
			SabBeh_SaberShouldBeDisarmedBlocker(blocker, blocker_saber);
			WP_BlockPointsRegenerate_over_ride(blocker, BLOCKPOINTS_FATIGUE);
		}
		else
		{
			//Low points = bad blocks
			G_Stagger(blocker);
			PM_AddBlockFatigue(&blocker->client->ps, WP_ClassBlockPointCost(blocker, BLOCKPOINTS_TEN, qfalse));
		}
	}
	else if (PM_SaberInKata(static_cast<saberMoveName_t>(blocker->client->ps.saberMove)))
	{
		sab_beh_add_balance(attacker, -MPCOST_PARRIED);
		//set otherOwner
		sab_beh_add_balance(blocker, MPCOST_PARRIED);

		if (attacker->client->ps.blockPoints < BLOCKPOINTS_TEN)
		{
			//Low points = bad blocks
			sab_beh_saber_should_be_disarmed_attacker(attacker, attacker_saber);
			WP_BlockPointsRegenerate_over_ride(attacker, BLOCKPOINTS_FATIGUE);
		}
		else
		{
			//Low points = bad blocks
			G_Stagger(attacker);
			PM_AddBlockFatigue(&attacker->client->ps, WP_ClassBlockPointCost(attacker, BLOCKPOINTS_TEN, qfalse)); // 2026-11-10: class cost, as the other kata branch (was a flat 10)
		}
	}
	else
	{
		//either both are faking or neither is faking.  Either way, it's canceled out
		//set self
		sab_beh_add_balance(attacker, MPCOST_PARRIED);
		//set otherOwner
		sab_beh_add_balance(blocker, MPCOST_PARRIED);

		sab_beh_attack_blocked(attacker, blocker, attacker_saber, qtrue);

		sab_beh_attack_blocked(blocker, attacker, blocker_saber, qtrue);
	}
	return qtrue;
}

// Saber AI tactics (g_saberAITactics 1, SJE SP, 2026-11-09): feints and taunts for saber NPCs, used against the player
// now and then, never spammed. AI_Jedi.cpp (kicks / slaps / taunts) and bg_pmove.cpp (feints) call these.
extern cvar_t* g_saberAITactics;
extern bool in_camera;
extern qboolean PM_SaberInStart(int move);
extern qboolean PM_SaberInAttack(int move);
static int saber_feint_next[MAX_GENTITIES];
static int saber_taunt_want[MAX_GENTITIES];

static qboolean WP_SaberAIIsPlayer(const gentity_t* ent)
{
	return ent && ent->client && (ent->s.number < MAX_CLIENTS || G_ControlledByPlayer(ent)) ? qtrue : qfalse;
}

// Is he turtling: guard up (holding block, or in a parry) and not swinging?
qboolean WP_SaberAIEnemyTurtling(const gentity_t* enemy)
{
	if (!enemy || !enemy->client || enemy->health <= 0)
	{
		return qfalse;
	}
	const playerState_t* ps = &enemy->client->ps;
	if (PM_SaberInAttack(ps->saberMove) || PM_SaberInStart(ps->saberMove))
	{
		return qfalse;
	}
	return (ps->ManualBlockingFlags & (1 << MBF_HOLDINGBLOCK | 1 << MBF_HOLDINGBLOCKANDATTACK | 1 << MBF_NPCBLOCKING))
		|| PM_SaberInParry(ps->saberMove) ? qtrue : qfalse;
}

// Feint: a saber NPC whose swing wind-up (LS_S_*) is ending, against a player who turtles within 128 units, sometimes
// cuts the swing and comes in from another side (PM_DoFake, as the player's attack + use fake; it costs the NPC one
// mishap level like any fake). Chance 25% (Padawan) .. 55% (Jedi Master); a feint can only follow 15 s (Padawan) ..
// 9 s (Jedi Master) after the last one, and a failed roll waits 1.5 s before it is rolled again.
qboolean WP_SaberAIFeintWanted(gentity_t* npc)
{
	if (!g_saberAITactics || !g_saberAITactics->integer || in_camera || !npc || !npc->client || !npc->NPC
		|| WP_SaberAIIsPlayer(npc) || npc->s.number >= MAX_GENTITIES)
	{
		return qfalse;
	}
	const gentity_t* enemy = npc->enemy;
	if (!WP_SaberAIIsPlayer(enemy) || !WP_SaberAIEnemyTurtling(enemy)
		|| DistanceSquared(npc->currentOrigin, enemy->currentOrigin) > 128.0f * 128.0f)
	{
		return qfalse;
	}
	int& next = saber_feint_next[npc->s.number];
	if (next > level.time + 15000)
	{
		next = 0; // stale (level restart / savegame)
	}
	if (next > level.time)
	{
		return qfalse;
	}
	const int skill = g_spskill->integer < 0 ? 0 : g_spskill->integer > 3 ? 3 : g_spskill->integer;
	if (Q_irand(0, 99) >= 25 + 10 * skill)
	{
		next = level.time + 1500;
		return qfalse;
	}
	next = level.time + 15000 - 2000 * skill;
	if (d_blockinfo->integer || g_DebugSaberCombat->integer)
	{
		gi.Printf(S_COLOR_YELLOW"Saber AI: %s feints\n", npc->NPC_type);
	}
	return qtrue;
}

// Taunt wish: set when an NPC perfect-parries the player or breaks his guard; AI_Jedi (Jedi_TacticalTaunt) taunts
// within 3 s if it has room for it.
qboolean WP_SaberAITauntWanted(const gentity_t* npc)
{
	if (!npc || npc->s.number < 0 || npc->s.number >= MAX_GENTITIES)
	{
		return qfalse;
	}
	const int t = saber_taunt_want[npc->s.number];
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
	if (g_saberAITactics && g_saberAITactics->integer && npc && npc->NPC && !WP_SaberAIIsPlayer(npc)
		&& WP_SaberAIIsPlayer(victim) && npc->s.number < MAX_GENTITIES)
	{
		saber_taunt_want[npc->s.number] = level.time + 3000;
	}
}

// Perfect parry round 1 (g_saberPerfectParry 1, SJE SP, 2026-11-09; DH: a perfect parry costs the attacker block
// points, never shield).
extern cvar_t* g_saberPerfectParry;
extern qboolean PM_InKnockDown(const playerState_t* ps);

// Is ent's riposte swing running (saberRiposteTime < 0, set in PM_SetSaberMove)? A riposte can't be perfect-parried.
static qboolean WP_SaberRiposteSwingActive(const gentity_t* ent)
{
	return g_saberPerfectParry && g_saberPerfectParry->integer && ent && ent->client
		&& ent->client->ps.saberRiposteTime < 0 && -ent->client->ps.saberRiposteTime > level.time ? qtrue : qfalse;
}

// Heavy swings (Strong, Desann, staff, two-handed sabers) cost the attacker more when perfect-parried.
static qboolean WP_SaberHeavySwing(const gentity_t* ent)
{
	switch (ent->client->ps.saberAnimLevel)
	{
	case SS_STRONG:
	case SS_DESANN:
	case SS_STAFF:
		return qtrue;
	default:
		return ent->client->ps.saber[0].saberFlags & SFL_TWO_HANDED ? qtrue : qfalse;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// Saber combat aim round 1 (2026-11-10, DH: cinematic duels that need timing; no swing spam, no block turtling;
// NPCs / bots must not swing spam, a perfect-blocked NPC / bot is staggered and loses more block points).
// ---------------------------------------------------------------------------------------------------------------
extern void CGCam_Shake(float intensity, int duration);

#define SABER_AI_PERFECTBLOCKED_BP	30	// block points a perfect-blocked NPC loses (was 20, players lose 10)
#define SABER_UNBLOCKABLE_BLOCK_BP	25	// block points a fighter loses blocking an unblockable attack (kata) without a perfect block
#define SABER_UNBLOCKABLE_PB_FATIGUE	8	// fatigue an attacker gets when his unblockable attack (kata) is perfect blocked (item 318: was 5)
#define SABER_UNBLOCKABLE_PB_FP		25	// stamina (force power) an attacker loses when his kata is perfect blocked (item 318)
#define SABER_RIPOSTE_BLOCK_BP		5	// extra block points a blocked riposte costs the blocker

static int saber_stagger_frame[MAX_GENTITIES];

static qboolean WP_SaberIsAI(const gentity_t* ent)
{
	return ent && ent->client && ent->NPC && !G_ControlledByPlayer(ent) ? qtrue : qfalse;
}

static qboolean WP_SaberIsPlayer(const gentity_t* ent)
{
	return ent && ent->client && (ent->s.number < MAX_CLIENTS || G_ControlledByPlayer(ent)) ? qtrue : qfalse;
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

// Guard break moment: the player broke a guard (0 BP stagger or disarm): short slow motion (d_slowmoaction) and a
// camera shake. The player's own guard broken: the shake only.
static void WP_SaberGuardBreakMoment(const gentity_t* breaker, const gentity_t* victim)
{
	if (WP_SaberIsPlayer(breaker))
	{
		G_StartStasisEffect(breaker, MEF_NO_SPIN, 200, 0.3f, 0); // the climax of the duel: with or without d_slowmoaction
		CGCam_Shake(0.6f, 250);
	}
	else if (WP_SaberIsPlayer(victim))
	{
		CGCam_Shake(0.6f, 250);
	}
}

// Perfect block mash lockout (2026-11-10): a timed press (block held + attack pressed) opens the perfect block
// window (g_active.cpp). A press that parried nothing locks the window out for 500 ms after its window ended; a press
// in that time restarts the lockout. So mashing never gets a window, one well timed press does.
#define SABER_PERFECTBLOCK_LOCKOUT	500
static int saber_pb_press_time[MAX_GENTITIES];
static int saber_pb_window[MAX_GENTITIES];
static qboolean saber_pb_landed[MAX_GENTITIES];

// g_active.cpp: a new timed press. qtrue: open the perfect block window (window ms).
qboolean WP_SaberPerfectBlockPress(const gentity_t* ent, const int window)
{
	if (!ent)
	{
		return qfalse;
	}
	const int n = ent->s.number;
	const qboolean locked = !saber_pb_landed[n] && saber_pb_press_time[n] > 0
		&& level.time < saber_pb_press_time[n] + saber_pb_window[n] + SABER_PERFECTBLOCK_LOCKOUT
		&& saber_pb_press_time[n] <= level.time ? qtrue : qfalse;

	saber_pb_press_time[n] = level.time;
	saber_pb_window[n] = window;
	saber_pb_landed[n] = qfalse;

	if (locked && (d_blockinfo->integer || g_DebugSaberCombat->integer))
	{
		gi.Printf(S_COLOR_ORANGE"Perfect block press too soon after a missed one: no window\n");
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

// A perfect-blocked NPC (DH): always the massive stagger; worn out (fatigue 13+) a 1 in 3 chance to lose the saber
// instead. Players keep the heavy slow bounce (fatigue 13+: a mishap).
static void WP_SaberAIPerfectBlocked(gentity_t* attacker, const gentity_t* blocker, const int saberNum)
{
	if (attacker->client->ps.saberFatigueChainCount >= MISHAPLEVEL_THIRTEEN && !Q_irand(0, 2))
	{
		sab_beh_saber_should_be_disarmed_attacker(attacker, saberNum);
		WP_SaberGuardBreakMoment(blocker, attacker);
		if (d_attackinfo->integer || g_DebugSaberCombat->integer)
		{
			gi.Printf(S_COLOR_RED"Perfect blocked worn out NPC lost his saber\n");
		}
	}
	else
	{
		WP_SaberStaggerOnce(attacker);
		attacker->client->ps.userInt3 |= 1 << FLAG_MBLOCKBOUNCE;
		if (d_attackinfo->integer || g_DebugSaberCombat->integer)
		{
			gi.Printf(S_COLOR_YELLOW"Perfect blocked NPC staggered\n");
		}
	}
}

// Called after a perfect block has taken its block points from the attacker (every perfect block branch here):
// a heavy swing costs 5 BP more (15 instead of 10 for players), an attacker left with 0 BP has his guard broken
// (stagger; with 0 BP his next blocks fail), and the blocker gets a 500 ms riposte window (PM_SetSaberMove).
static void WP_SaberPerfectParryBonus(gentity_t* blocker, gentity_t* attacker)
{
	WP_SaberPerfectBlockLanded(blocker); // the press parried something: no mash lockout
	if (!g_saberPerfectParry || !g_saberPerfectParry->integer || !blocker->client || !attacker->client)
	{
		return;
	}
	if (WP_SaberHeavySwing(attacker))
	{
		PM_AddBlockFatigue(&attacker->client->ps, BLOCKPOINTS_FIVE);
	}
	if (attacker->client->ps.blockPoints <= 0 && WP_SaberStaggerOnce(attacker))
	{
		WP_SaberGuardBreakMoment(blocker, attacker);
		if (d_blockinfo->integer || g_DebugSaberCombat->integer)
		{
			gi.Printf(S_COLOR_CYAN"Perfect parry: guard break, attacker out of block points\n");
		}
	}
	blocker->client->ps.saberRiposteTime = level.time + 500;
	WP_SaberAITauntSet(blocker, attacker); // an NPC that perfect-parried the player may taunt him (g_saberAITactics)
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
			if (!(attacker->client->ps.saber[0].saberFlags & SFL_NOT_DISARMABLE) && !attacker->client->ps.saberInFlight)
			{
				sab_beh_saber_should_be_disarmed_attacker(attacker, 0); // stagger + saber knocked out of his hand
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
			if (d_attackinfo->integer || g_DebugSaberCombat->integer)
			{
				gi.Printf(S_COLOR_CYAN"Unblockable attack perfect blocked: attacker disarmed + staggered, +8 fatigue, -25 stamina\n");
			}
		}
		else
		{
			//This must be Unblockable
			if (d_attackinfo->integer || g_DebugSaberCombat->integer)
			{
				gi.Printf(S_COLOR_MAGENTA"Attacker must be Unblockable\n");
			}
			attacker->client->ps.saberEventFlags &= ~SEF_BLOCKED;
		}
	}
	else if (PM_SaberInNonIdleDamageMove(&blocker->client->ps))
	{
		//and blocker is attacking
		if (d_attackinfo->integer || g_DebugSaberCombat->integer)
		{
			gi.Printf(S_COLOR_YELLOW"Both Attacker and Blocker are now attacking\n");
		}

		sab_beh_attack_vs_attack(blocker, attacker, 0, saberNum); // blocker first, as before; saberNum is the attacker's saber
	}
	else if (PM_SuperBreakWinAnim(attacker->client->ps.torsoAnim))
	{
		//attacker was attempting a superbreak and he hit someone who could block the move, rail him for screwing up.
		sab_beh_add_balance(attacker, MPCOST_PARRIED);

		sab_beh_animate_heavy_slow_bounce_attacker(attacker);

		sab_beh_add_balance(blocker, -MPCOST_PARRIED);
		if (d_attackinfo->integer || g_DebugSaberCombat->integer)
		{
			gi.Printf(S_COLOR_YELLOW"Attacker Super break win / fail\n");
		}
	}
	else if (atkfake)
	{
		//attacker faked but it was blocked here
		if (m_blocking || npc_blocking)
		{
			//defender parried the attack fake.
			WP_SaberPerfectBlockLanded(blocker); // no mash lockout (2026-11-10)
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
			sab_beh_add_mishap_Fake_attacker(attacker, blocker, saberNum);

			if ((d_attackinfo->integer || g_DebugSaberCombat->integer) && !PM_InSaberLock(attacker->client->ps.torsoAnim))
			{
				gi.Printf(S_COLOR_YELLOW"Attackers Attack Fake was P-Blocked\n");
			}
		}
		else
		{
			//otherwise, the defender stands a good chance of having his defensive broken.
			sab_beh_add_balance(attacker, -MPCOST_PARRIED);

			if (WP_SabersCheckLock(attacker, blocker))
			{
				attacker->client->ps.userInt3 |= 1 << FLAG_SABERLOCK_ATTACKER;
				attacker->client->ps.saberBlocked = BLOCKED_NONE;
				blocker->client->ps.saberBlocked = BLOCKED_NONE;
			}

			if (d_attackinfo->integer || g_DebugSaberCombat->integer)
			{
				gi.Printf(S_COLOR_YELLOW"Attacker forced a saberlock\n");
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
				if (npc_blocking && blocker->client->ps.blockPoints >= BLOCKPOINTS_MISSILE
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

				if (attacker->s.number < MAX_CLIENTS || G_ControlledByPlayer(attacker))
				{
					CGCam_BlockShakeSP(0.45f, 100);
				}
			}
			else
			{
				attacker->client->ps.userInt3 |= 1 << FLAG_PARRIED;
			}

			if (!m_blocking && saber_stagger_frame[attacker->s.number] != level.time) // not when this clash staggered him (2026-11-10)
			{
				sab_beh_attack_blocked(attacker, blocker, saberNum, qfalse);
			}

			sab_beh_add_balance(blocker, -MPCOST_PARRIED);

			if (d_attackinfo->integer || g_DebugSaberCombat->integer)
			{
				gi.Printf(S_COLOR_YELLOW"Attackers Attack was Blocked\n");
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
					if (d_attackinfo->integer || g_DebugSaberCombat->integer)
					{
						gi.Printf(S_COLOR_YELLOW"Attack an Unblockable attack\n");
					}
				}
				else
				{
					sab_beh_attack_blocked(attacker, blocker, saberNum, qtrue);

					G_Stagger(blocker);

					if (d_attackinfo->integer || g_DebugSaberCombat->integer)
					{
						gi.Printf(S_COLOR_ORANGE"Attacker All the rest of the types of contact\n");
					}
				}
			}
		}
	}
	return qtrue;
}

// Small helpers to remove repeated code
static inline void SB_ParrySuccess(gentity_t* blocker, gentity_t* attacker,
	int saberNum, int bladeNum)
{
	blocker->client->ps.saberEventFlags |= SEF_PARRIED;
	attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
	WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);
}

static inline void SB_DebugPrint(const char* msg, gentity_t* blocker)
{
	if ((d_blockinfo->integer || g_DebugSaberCombat->integer) &&
		(blocker->s.number < MAX_CLIENTS || G_ControlledByPlayer(blocker)))
	{
		gi.Printf("%s%s\n", S_COLOR_CYAN, msg);
	}
}

void WP_SaberWearOnBlock(gentity_t* blocker, const gentity_t* attacker);

// Adaptive duel AI (g_saberAIAdapt 1, SJE SP, 2026-11-09): saber NPCs remember the player's last 6 basic swing
// directions. When the player starts a swing he has already used at least twice in those 6, a blocking NPC may read
// it and perfect-block it (the same perfect block as the player's, with the bounce and the block point cost). The
// chance grows with every repeat and with the difficulty (g_spskill), up to 60%; mixing up the swings avoids it.
// Each NPC can do this once every 6 s (Padawan) to 3 s (Jedi Master), so it is never spammed.
// Riposte swings are never read.
extern cvar_t* g_saberAIAdapt;
extern cvar_t* g_spskill;
static constexpr int SABER_ADAPT_HISTORY = 6;
static int saber_adapt_hist[MAX_CLIENTS][SABER_ADAPT_HISTORY];
static int saber_adapt_read_time[MAX_CLIENTS];
// per NPC: no read perfect block before this time (not spammed: 6 s on Padawan down to 3 s on Jedi Master)
static int saber_adapt_npc_next[MAX_GENTITIES];

// PM_SetSaberMove: the player (or a player-slot client) starts a new attack
void WP_SaberAIAdaptRecord(const gentity_t* ent, const int move)
{
	if (!g_saberAIAdapt || !g_saberAIAdapt->integer || !ent || ent->s.number < 0 || ent->s.number >= MAX_CLIENTS)
	{
		return;
	}
	const int n = ent->s.number;
	saber_adapt_read_time[n] = 0;
	if (move < LS_A_TL2BR || move > LS_A_T2B)
	{
		return; // only the 7 basic swing directions
	}
	int repeats = 0;
	for (int i = 0; i < SABER_ADAPT_HISTORY; i++)
	{
		if (saber_adapt_hist[n][i] == move)
		{
			repeats++;
		}
	}
	for (int i = SABER_ADAPT_HISTORY - 1; i > 0; i--)
	{
		saber_adapt_hist[n][i] = saber_adapt_hist[n][i - 1];
	}
	saber_adapt_hist[n][0] = move;
	if (repeats < 2)
	{
		return;
	}
	int chance = (repeats - 1) * (10 + 5 * g_spskill->integer);
	if (chance > 60)
	{
		chance = 60;
	}
	if (Q_irand(0, 99) < chance)
	{
		saber_adapt_read_time[n] = level.time + 1500;
	}
	if (g_DebugSaberCombat->integer)
	{
		gi.Printf(S_COLOR_YELLOW"Adaptive AI: swing %d used %d times in the last %d, %d%% read chance -> %s\n", move, repeats,
			SABER_ADAPT_HISTORY, chance, saber_adapt_read_time[n] ? "READ" : "not read");
	}
}

// Has a blocking saber NPC read this swing of attacker? Used once: the read is spent by the perfect block.
static qboolean WP_SaberAIAdaptRead(const gentity_t* blocker, const gentity_t* attacker)
{
	if (!g_saberAIAdapt || !g_saberAIAdapt->integer || !blocker->NPC || G_ControlledByPlayer(blocker)
		|| attacker->s.number < 0 || attacker->s.number >= MAX_CLIENTS || WP_SaberRiposteSwingActive(attacker))
	{
		return qfalse;
	}
	if (saber_adapt_read_time[attacker->s.number] <= level.time)
	{
		return qfalse;
	}
	const int b = blocker->s.number;
	if (b < 0 || b >= MAX_GENTITIES)
	{
		return qfalse;
	}
	if (saber_adapt_npc_next[b] > level.time && saber_adapt_npc_next[b] - level.time <= 6000)
	{
		return qfalse; // this NPC read a swing a moment ago (the 6000 check drops stale times after a map / load)
	}
	saber_adapt_read_time[attacker->s.number] = 0;
	saber_adapt_npc_next[b] = level.time + 6000 - 1000 * g_spskill->integer;
	return qtrue;
}

qboolean sab_beh_block_vs_attack(gentity_t* blocker, gentity_t* attacker, const int saberNum, const int bladeNum, vec3_t hit_loc)
{
	// Early exits
	if (!blocker || !blocker->client || !attacker) {
		return qfalse;
	}

	if (Rosh_BeingHealed(blocker)) {
		return qfalse;
	}

	if (G_InCinematicSaberAnim(blocker)) {
		return qfalse;
	}

	if (PM_SuperBreakLoseAnim(blocker->client->ps.torsoAnim) ||
		PM_SuperBreakWinAnim(blocker->client->ps.torsoAnim)) {
		return qfalse;
	}
	WP_SaberWearOnBlock(blocker, attacker); // a breakable staff wears down
	//-(Im the blocker)
	const qboolean accurate_parry = g_accurate_blocking(blocker, attacker, hit_loc); // Perfect Normal Blocking
	const qboolean blocking = ((blocker->client->ps.ManualBlockingFlags & 1 << MBF_HOLDINGBLOCK) != 0) ? qtrue : qfalse;	//Normal Blocking
	// a riposte swing can't be perfect-parried (g_saberPerfectParry, 2026-11-09)
	const qboolean m_blocking = ((blocker->client->ps.ManualBlockingFlags & 1 << MBF_PERFECTBLOCKING) != 0) && !WP_SaberRiposteSwingActive(attacker) ? qtrue : qfalse;	//perfect Blocking
	const qboolean is_holding_block_button_and_attack = ((blocker->client->ps.ManualBlockingFlags & 1 << MBF_HOLDINGBLOCKANDATTACK) != 0) ? qtrue : qfalse;	//Active Blocking
	const qboolean npc_blocking = ((blocker->client->ps.ManualBlockingFlags & 1 << MBF_NPCBLOCKING) != 0) ? qtrue : qfalse;	//Active NPC Blocking

	// ============================================================
	// UNBLOCKABLE ATTACK BRANCH
	// ============================================================
	if (!PM_SaberInnonblockableAttack(attacker->client->ps.torsoAnim))
	{ //if the attack is blockable, then check for accurate parry
		if (blocker->client->ps.blockPoints <= BLOCKPOINTS_FATIGUE) // blocker has less than 20BP
		{//Low points = bad blocks
			if (blocker->client->ps.blockPoints <= BLOCKPOINTS_TEN) // blocker has less than 10BP
			{//Low points = bad blocks
				//Low points = bad blocks
				if (blocker->NPC && !G_ControlledByPlayer(blocker)) //NPC only
				{
					sab_beh_add_mishap_blocker(blocker, saberNum);
				}
				else
				{
					SabBeh_SaberShouldBeDisarmedBlocker(blocker, saberNum);
				}
				WP_SaberGuardBreakMoment(attacker, blocker); // 2026-11-10

				// The blocker recovers a little if he still holds his saber (a disarm already gives the disarmed
				// fighter these points). This gave them to the attacker whenever the attacker was an NPC.
				if (!blocker->client->ps.saberInFlight)
				{
					WP_BlockPointsRegenerate(blocker, BLOCKPOINTS_FATIGUE);
				}

				if ((d_blockinfo->integer || g_DebugSaberCombat->integer) && blocker->s.number < MAX_CLIENTS ||
					G_ControlledByPlayer(blocker))
				{
					gi.Printf(S_COLOR_CYAN"Blocker was disarmed with very low bp, recharge bp 20bp\n");
				}

				//just so blocker knows that he has parried the attacker
				blocker->client->ps.saberEventFlags |= SEF_PARRIED;
				//just so attacker knows that he was blocked
				attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
				WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);
			}
			else
			{
				//Low points = bad blocks
				g_fatigue_bp_knockaway(blocker);

				PM_AddBlockFatigue(&blocker->client->ps, WP_ClassBlockPointCost(blocker, BLOCKPOINTS_DANGER, qfalse));

				if ((d_blockinfo->integer || g_DebugSaberCombat->integer) && (blocker->s.number < MAX_CLIENTS || G_ControlledByPlayer(blocker)))
				{
					gi.Printf(S_COLOR_CYAN"Blocker stagger drain 4 bp\n");
				}

				//just so blocker knows that he has parried the attacker
				blocker->client->ps.saberEventFlags |= SEF_PARRIED;
				//just so attacker knows that he was blocked
				attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
				//since it was parried, take away any damage done
				WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);
			}
		}
		else
		{
			//just block it //jacesolaris
			// Adaptive duel AI (g_saberAIAdapt, 2026-11-09): a blocking saber NPC that read this swing perfect-blocks it
			const qboolean npc_read = (npc_blocking || accurate_parry) && WP_SaberAIAdaptRead(blocker, attacker) ? qtrue : qfalse;

			if (is_holding_block_button_and_attack || npc_read) //Holding Block Button + attack button
			{//Active Blocking
				//perfect Blocking
				if (m_blocking || npc_read) // A perfectly timed block
				{//Parry the attack
					WP_SaberMBlock(blocker, attacker, saberNum, bladeNum);

					if (WP_SaberIsAI(attacker))
					{// 2026-11-10 (DH): a perfect-blocked NPC is staggered
						WP_SaberAIPerfectBlocked(attacker, blocker, saberNum);
					}
					else if (attacker->client->ps.saberFatigueChainCount >= MISHAPLEVEL_THIRTEEN)
					{
						sab_beh_add_mishap_attacker(attacker, blocker, saberNum);
					}
					else
					{
						sab_beh_animate_heavy_slow_bounce_attacker(attacker);
						attacker->client->ps.userInt3 |= 1 << FLAG_MBLOCKBOUNCE;
					}

					blocker->client->ps.userInt3 |= 1 << FLAG_PERFECTBLOCK;

					if (blocker->NPC && !G_ControlledByPlayer(blocker)) //NPC only
					{
						g_do_m_block_response(blocker);
					}

					if (blocker->s.number < MAX_CLIENTS || G_ControlledByPlayer(blocker))
					{
						if (d_slowmoaction->integer)
						{
							G_StartStasisEffect(blocker, MEF_NO_SPIN, 200, 0.3f, 0);
						}
						CGCam_BlockShakeSP(0.45f, 100);
					}

					G_Sound(blocker, G_SoundIndex(va("sound/weapons/saber/saber_perfectblock%d.mp3", Q_irand(1, 3))));
					//G_PlayEffect("saber_goodparry", hit_loc);

					if ((d_blockinfo->integer || g_DebugSaberCombat->integer) && blocker->s.number < MAX_CLIENTS
						|| G_ControlledByPlayer(blocker))
					{
						gi.Printf(S_COLOR_CYAN"Blocker Perfect blocked reward 15\n");
					}

					//just so blocker knows that he has parried the attacker
					blocker->client->ps.saberEventFlags |= SEF_PARRIED;
					//just so attacker knows that he was blocked
					attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
					//since it was parried, take away any damage done
					WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);

					WP_BlockPointsRegenerate_over_ride(blocker, BLOCKPOINTS_FIFTEEN);//SAC Reward blocker
					sab_beh_add_balance(blocker, -MPCOST_MBLOCKED); //SAC Reward blocker

					if (attacker->NPC && !G_ControlledByPlayer(attacker)) //NPC only
					{
						PM_AddBlockFatigue(&attacker->client->ps, SABER_AI_PERFECTBLOCKED_BP); //BP Punish Attacker (2026-11-10, DH: 30, was 20)
						sab_beh_add_balance(attacker, MPCOST_MBLOCKED); //SAC Punish attacker (bug fix 2026-11-10: was BLOCKPOINTS_TEN, a block point value: +10 of the 15 fatigue levels)
					}
					else
					{
						PM_AddBlockFatigue(&attacker->client->ps, BLOCKPOINTS_TEN); //BP Punish Attacker
						sab_beh_add_balance(attacker, MPCOST_MBLOCKED); //SAC Punish attacker
					}
					WP_SaberPerfectParryBonus(blocker, attacker); // g_saberPerfectParry (2026-11-09)
				}
				else
				{
					//Spamming block + attack buttons
					if (blocker->client->ps.blockPoints <= BLOCKPOINTS_HALF)
					{
						WP_SaberFatiguedParry(blocker, attacker, saberNum, bladeNum);
					}
					else if (attacker->client->ps.saberAnimLevel == SS_DESANN ||
						attacker->client->ps.saberAnimLevel == SS_BLASTER ||
						attacker->client->ps.saberAnimLevel == SS_STRONG) // SS_BLASTER (2026-10-14)
					{
						WP_SaberFatiguedParry(blocker, attacker, saberNum, bladeNum);
					}
					else
					{
						WP_SaberParry(blocker, attacker, saberNum, bladeNum);
					}

					if (attacker->NPC && !G_ControlledByPlayer(attacker)) //NPC only
					{
						sab_beh_add_balance(attacker, MPCOST_MBLOCKED); //SAC Punish attacker
					}

					PM_AddBlockFatigue(&blocker->client->ps, WP_ClassBlockPointCost(blocker, BLOCKPOINTS_FIVE, qfalse));

					if (blocker->s.number < MAX_CLIENTS || G_ControlledByPlayer(blocker))
					{
						CGCam_BlockShakeSP(0.45f, 100);
					}

					if ((d_blockinfo->integer || g_DebugSaberCombat->integer) && blocker->s.number < MAX_CLIENTS
						|| G_ControlledByPlayer(blocker))
					{
						gi.Printf(S_COLOR_CYAN"Blocker Spamming block + attack cost 5\n");
					}

					//just so blocker knows that he has parried the attacker
					blocker->client->ps.saberEventFlags |= SEF_PARRIED;
					//just so attacker knows that he was blocked
					attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
					//since it was parried, take away any damage done
					WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);
				}
			}
			else if (blocking && !is_holding_block_button_and_attack) //Holding block button only (spamming block)
			{
				if (blocker->client->ps.blockPoints <= BLOCKPOINTS_HALF)
				{
					WP_SaberFatiguedParry(blocker, attacker, saberNum, bladeNum);
				}
				else if (attacker->client->ps.saberAnimLevel == SS_DESANN ||
					attacker->client->ps.saberAnimLevel == SS_BLASTER || // SS_BLASTER (2026-10-14)
					attacker->client->ps.saberAnimLevel == SS_STRONG)
				{
					WP_SaberFatiguedParry(blocker, attacker, saberNum, bladeNum);
				}
				else
				{
					WP_SaberBlockedBounceBlock(blocker, attacker, saberNum, bladeNum);
				}

				if (blocker->NPC && !G_ControlledByPlayer(blocker)) //NPC only
				{
					//
				}
				else
				{
					PM_AddBlockFatigue(&blocker->client->ps, WP_ClassBlockPointCost(blocker, BLOCKPOINTS_FIVE, qfalse));
				}

				if (attacker->NPC && !G_ControlledByPlayer(attacker)) //NPC only
				{
					if (attacker->client->ps.blockPoints <= BLOCKPOINTS_FATIGUE)
					{
						WP_BlockPointsRegenerate(attacker, BLOCKPOINTS_FATIGUE);
					}
					else
					{
						WP_BlockPointsRegenerate(attacker, BLOCKPOINTS_TEN);
					}
				}

				if (blocker->s.number < MAX_CLIENTS || G_ControlledByPlayer(blocker))
				{
					CGCam_BlockShakeSP(0.45f, 100);
				}
				if ((d_blockinfo->integer || g_DebugSaberCombat->integer) && blocker->s.number < MAX_CLIENTS ||
					G_ControlledByPlayer(blocker))
				{
					gi.Printf(S_COLOR_CYAN"Blocker Holding block button only (spamming block) cost 5\n");
				}

				//just so blocker knows that he has parried the attacker
				blocker->client->ps.saberEventFlags |= SEF_PARRIED;
				//just so attacker knows that he was blocked
				attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
				//since it was parried, take away any damage done
				WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);
			}
			else if ((accurate_parry || npc_blocking)) //Other types and npc,s
			{
				if (attacker->client->ps.saberAnimLevel == SS_DESANN ||
					attacker->client->ps.saberAnimLevel == SS_BLASTER ||
					attacker->client->ps.saberAnimLevel == SS_STRONG) // SS_BLASTER (2026-10-14)
				{
					WP_SaberFatiguedParry(blocker, attacker, saberNum, bladeNum);

					// 2026-11-10 bug fix: blocking a heavy style cost nothing here. Now it costs the normal block
					// cost for the blocker's block points plus 2.
					int heavy_cost;
					if (WP_SaberIsAI(blocker))
					{
						heavy_cost = blocker->client->ps.blockPoints <= BLOCKPOINTS_FOURTY ? BLOCKPOINTS_FIVE
							: blocker->client->ps.blockPoints <= BLOCKPOINTS_MISSILE ? BLOCKPOINTS_DANGER : BLOCKPOINTS_THREE;
					}
					else
					{
						heavy_cost = blocker->client->ps.blockPoints <= BLOCKPOINTS_FOURTY ? BLOCKPOINTS_FAIL : BLOCKPOINTS_THREE;
					}
					PM_AddBlockFatigue(&blocker->client->ps, WP_ClassBlockPointCost(blocker, heavy_cost + 2, qfalse));

					if (d_blockinfo->integer || g_DebugSaberCombat->integer)
					{
						gi.Printf(S_COLOR_CYAN"Blocked a heavy style, cost %d\n", heavy_cost + 2);
					}
				}
				else if (blocker->client->ps.blockPoints <= BLOCKPOINTS_MISSILE)
				{
					if (blocker->client->ps.blockPoints <= BLOCKPOINTS_FOURTY)
					{
						WP_SaberFatiguedParry(blocker, attacker, saberNum, bladeNum);

						if ((d_blockinfo->integer || g_DebugSaberCombat->integer) && (blocker->NPC && !
							G_ControlledByPlayer(blocker)))
						{
							gi.Printf(S_COLOR_CYAN"NPC Fatigued Parry\n");
						}

						PM_AddBlockFatigue(&blocker->client->ps, WP_ClassBlockPointCost(blocker, WP_SaberIsAI(blocker) ? BLOCKPOINTS_FIVE : BLOCKPOINTS_FAIL, qfalse));
					}
					else
					{
						WP_SaberParry(blocker, attacker, saberNum, bladeNum);

						if ((d_blockinfo->integer || g_DebugSaberCombat->integer) && (blocker->NPC && !
							G_ControlledByPlayer(blocker)))
						{
							gi.Printf(S_COLOR_CYAN"NPC normal Parry\n");
						}

						PM_AddBlockFatigue(&blocker->client->ps, WP_ClassBlockPointCost(blocker, WP_SaberIsAI(blocker) ? BLOCKPOINTS_DANGER : BLOCKPOINTS_THREE, qfalse));
					}
				}
				else
				{ //good parry
					WP_SaberMBlock(blocker, attacker, saberNum, bladeNum);

					if (blocker->NPC && !G_ControlledByPlayer(blocker)) //NPC only
					{
						g_do_m_block_response(blocker);
						PM_AddBlockFatigue(&blocker->client->ps, WP_ClassBlockPointCost(blocker, BLOCKPOINTS_THREE, qfalse)); // 2026-11-10: NPC block cost 3 / 4 / 5 as his block points fall (was 10 / 3 / 2)
					}
					else
					{
						PM_AddBlockFatigue(&blocker->client->ps, WP_ClassBlockPointCost(blocker, BLOCKPOINTS_THREE, qfalse));
					}

					if ((d_blockinfo->integer || g_DebugSaberCombat->integer) && (blocker->NPC && !
						G_ControlledByPlayer(blocker)))
					{
						gi.Printf(S_COLOR_CYAN"NPC good Parry\n");
					}
				}

				G_Sound(blocker, G_SoundIndex(va("sound/weapons/saber/saber_goodparry%d.mp3", Q_irand(1, 3))));

				//just so blocker knows that he has parried the attacker
				blocker->client->ps.saberEventFlags |= SEF_PARRIED;
				//just so attacker knows that he was blocked
				attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
				//since it was parried, take away any damage done
				WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);
			}
			else
			{
				sab_beh_add_mishap_blocker(blocker, saberNum);

				if (blocker->NPC && !G_ControlledByPlayer(blocker)) //NPC only
				{
					//
				}
				else
				{
					PM_AddBlockFatigue(&blocker->client->ps, WP_ClassBlockPointCost(blocker, BLOCKPOINTS_TEN, qfalse));
				}
				if ((d_blockinfo->integer || g_DebugSaberCombat->integer) && blocker->s.number < MAX_CLIENTS ||
					G_ControlledByPlayer(blocker))
				{
					gi.Printf(S_COLOR_CYAN"Blocker Not holding block drain 10\n");
				}
			}
		}

		// 2026-11-10: a blocked riposte (the 500 ms counter after a perfect parry) costs the blocker 5 block points more
		if (WP_SaberRiposteSwingActive(attacker) && (blocker->client->ps.saberEventFlags & SEF_PARRIED))
		{
			PM_AddBlockFatigue(&blocker->client->ps, SABER_RIPOSTE_BLOCK_BP);
			if (d_blockinfo->integer || g_DebugSaberCombat->integer)
			{
				gi.Printf(S_COLOR_CYAN"Blocked a riposte, extra cost 5\n");
			}
		}
	}
	else
	{//if the attack is unblockable, then only perfect block can save the day
		//perfect Blocking
		if (m_blocking) // A perfectly timed block
		{
			if (blocker->s.number < MAX_CLIENTS || G_ControlledByPlayer(blocker))
			{
				if (d_slowmoaction->integer)
				{
					G_StartStasisEffect(blocker, MEF_NO_SPIN, 200, 0.3f, 0);
				}
				CGCam_BlockShakeSP(0.45f, 100);
			}

			G_Sound(blocker, G_SoundIndex(va("sound/weapons/saber/saber_perfectblock%d.mp3", Q_irand(1, 3))));

			if (d_blockinfo->integer && blocker->s.number < MAX_CLIENTS || G_ControlledByPlayer(blocker))
			{
				gi.Printf(S_COLOR_CYAN"Blocker Perfect blocked an Unblockable attack reward 15\n");
			}

			blocker->client->ps.saberEventFlags |= SEF_PARRIED;
			attacker->client->ps.saberEventFlags |= SEF_BLOCKED;
			WP_SaberClearDamageForEntNum(attacker, blocker->s.number, saberNum, bladeNum);

			WP_BlockPointsRegenerate_over_ride(blocker, BLOCKPOINTS_FIFTEEN);
			PM_AddBlockFatigue(&attacker->client->ps, WP_SaberIsAI(attacker) ? SABER_AI_PERFECTBLOCKED_BP : BLOCKPOINTS_TEN); //BP Punish Attacker (NPC 30, 2026-11-10)
			sab_beh_add_balance(blocker, -MPCOST_MBLOCKED); //SAC Reward blocker
			sab_beh_add_balance(attacker, MPCOST_MBLOCKED); //SAC Punish attacker
			WP_SaberPerfectParryBonus(blocker, attacker); // g_saberPerfectParry (2026-11-09)
			// the attacker's massive stagger and stamina loss: sab_beh_attack_vs_block
		}
		else
		{
			//This must be Unblockable
			if (blocker->client->ps.blockPoints < BLOCKPOINTS_TEN)
			{
				//Low points = bad blocks
				SabBeh_SaberShouldBeDisarmedBlocker(blocker, saberNum);
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
				PM_AddBlockFatigue(&blocker->client->ps, WP_ClassBlockPointCost(blocker, SABER_UNBLOCKABLE_BLOCK_BP, qfalse));
				if (WP_SaberIsPlayer(blocker))
				{
					CGCam_Shake(0.6f, 250);
				}
			}
			if (d_blockinfo->integer || g_DebugSaberCombat->integer)
			{
				gi.Printf(S_COLOR_MAGENTA"Blocker can not block Unblockable\n");
			}
			blocker->client->ps.saberEventFlags &= ~SEF_PARRIED;
		}
	}
	return qtrue;
}

/////////Functions//////////////

/////////////////////// 20233 new build ////////////////////////////////

/*
-------------------------
Breakable saber staffs (saber wear)

A saber with brokenSaber1 in its .sab (Academy_staff_1, dual_1) wears down in a fight and breaks into its
brokenSaber1 (right hand) / brokenSaber2 (left hand; "none": one half). Wear 0-100 in ps.stats[STAT_SABER_WEAR]
(the HUD tints the saber style icon by it). g_saberBreaking: 0 off, 1 NPCs only, 2 everyone. Boss NPCs never break.
Wear: a hit on the hilt 20; blocking a heavy attack (strong / desann / tavion style, a special, unblockable) 4, medium
2, fast 1; a perfect block 0. x2 with no block points, x2 the attacker's Sith sword, x1.5 the attacker's saber
damageScale > 1, x1.5 the attacker's Force Rage. Recovers 1 a second after 8 s without wear, but once under 25 left
(cracked, wear > 75) never back above 25. At 100 it breaks at the end of the swing: sparks, the pieces, a stagger -
or from a heavy hit a knockdown and the right-hand half knocked away (Force pull / pick it up for dual again). Saber
damage is halved for 1.5 s after a break.
-------------------------
*/
#define SABERWEAR_BREAK			100
#define SABERWEAR_BREAK_HEAVY	101
#define SABERWEAR_CRACKED		75
#define SABERWEAR_RECOVER_DELAY	8000

extern cvar_t* g_saberBreaking;
extern qboolean WP_SaberDoBreak(gentity_t* ent);
extern qboolean WP_SaberLose(gentity_t* self, vec3_t throw_dir);
extern void G_Knockdown(gentity_t* self, gentity_t* attacker, const vec3_t push_dir, float strength, qboolean breakSaberLock);
extern void G_Stagger(gentity_t* hitEnt);
extern qboolean PM_SaberInStart(int move);
extern qboolean PM_SaberInTransition(int move);
extern qboolean PM_SaberInAttack(int move);

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
	if (!ent || !ent->client || ent->health <= 0 || !g_saberBreaking || g_saberBreaking->integer <= 0)
	{
		return qfalse;
	}
	if (g_saberBreaking->integer == 1 && ent->s.number < MAX_CLIENTS)
	{
		return qfalse;
	}
	if (ent->client->ps.weapon != WP_SABER || ent->client->ps.dualSabers)
	{
		return qfalse;
	}
	if (!ent->client->ps.saber[0].brokenSaber1 || !ent->client->ps.saber[0].brokenSaber1[0]
		|| !Q_stricmp("none", ent->client->ps.saber[0].brokenSaber1))
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
		if (attacker->client->ps.saber[0].type == SABER_SITH_SWORD)
		{
			amount *= 2.0f;
		}
		if (attacker->client->ps.saber[0].damageScale > 1.0f)
		{
			amount *= 1.5f;
		}
		if (attacker->client->ps.forcePowersActive & 1 << FP_RAGE)
		{
			amount *= 1.5f;
		}
	}
	if (ps->blockPoints <= BLOCKPOINTS_FATIGUE)
	{
		amount *= 2.0f;
	}
	int wear = ps->stats[STAT_SABER_WEAR];
	if (wear < SABERWEAR_BREAK)
	{
		wear += static_cast<int>(ceilf(amount));
	}
	if (wear >= SABERWEAR_BREAK)
	{
		wear = heavy || wear == SABERWEAR_BREAK_HEAVY ? SABERWEAR_BREAK_HEAVY : SABERWEAR_BREAK;
	}
	ps->stats[STAT_SABER_WEAR] = wear;
	ps->stats[STAT_SABER_WEAR_TIMER] = SABERWEAR_RECOVER_DELAY;
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
		&& bps->blockPoints > BLOCKPOINTS_FATIGUE)
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
		switch (aps->saberAnimLevel)
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

// a saber hit on the hilt (G_GetHitLocFromSurfName): qtrue when it was a breakable saber's hilt
qboolean WP_SaberWearHiltHit(gentity_t* ent, const char* surfName, const saberType_t saberType)
{
	if (!surfName || (Q_stricmpn("w_", surfName, 2) && Q_stricmpn("saber", surfName, 5) && Q_stricmp("cylinder01", surfName)))
	{
		return qfalse;
	}
	if (!WP_SaberBreakable(ent))
	{
		return qfalse;
	}
	WP_SaberWearAdd(ent, nullptr, saberType == SABER_SITH_SWORD ? 40.0f : 20.0f, qtrue);
	return qtrue;
}

static void WP_SaberWearBreak(gentity_t* ent, const qboolean heavy)
{
	gclient_t* client = ent->client;
	vec3_t fwd, push;

	client->ps.stats[STAT_SABER_WEAR] = 0;
	client->ps.stats[STAT_SABER_WEAR_TIMER] = 0;

	G_PlayEffect("sparks/spark_explosion", client->renderInfo.handRPoint);
	G_Sound(ent, G_SoundIndex("sound/weapons/saber/saberoffquick.mp3"));

	if (!WP_SaberDoBreak(ent))
	{
		return;
	}
	client->ps.stats[STAT_SABER_BREAK_SAFE] = level.time + 1500;

	AngleVectors(client->ps.viewangles, fwd, nullptr, nullptr);
	fwd[2] = 0;
	VectorNormalize(fwd);
	VectorScale(fwd, -1.0f, push);
	if (client->ps.groundEntityNum != ENTITYNUM_NONE)
	{
		if (heavy)
		{
			// knocked down, the right-hand half knocked away
			G_Knockdown(ent, ent, push, 300, qtrue);
			if (client->ps.dualSabers)
			{
				WP_SaberLose(ent, nullptr);
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
		gi.SendServerCommand(ent->s.number, "cp \"Your saber staff broke!\"");
	}
}

// every client think: recovery, the cracked sparks, the break when it is due
void WP_SaberWearThink(gentity_t* ent, const int msec)
{
	if (!ent || !ent->client)
	{
		return;
	}
	playerState_t* ps = &ent->client->ps;
	if (!WP_SaberBreakable(ent))
	{
		ps->stats[STAT_SABER_WEAR] = 0;
		ps->stats[STAT_SABER_WEAR_TIMER] = 0;
		return;
	}
	int wear = ps->stats[STAT_SABER_WEAR];
	if (wear >= SABERWEAR_BREAK)
	{
		// at the end of the swing (not while it is thrown, holstered or locked)
		if (ps->saberInFlight || !ps->SaberActive() || ps->saberLockTime > level.time
			|| PM_SaberInStart(ps->saberMove) || PM_SaberInTransition(ps->saberMove) || PM_SaberInAttack(ps->saberMove))
		{
			return;
		}
		WP_SaberWearBreak(ent, wear == SABERWEAR_BREAK_HEAVY ? qtrue : qfalse);
		return;
	}
	if (wear > SABERWEAR_CRACKED && ps->SaberActive() && Q_irand(0, 1500) < msec)
	{
		G_PlayEffect("sparks/spark_nosnd", ent->client->renderInfo.handRPoint);
	}
	if (wear > 0)
	{
		ps->stats[STAT_SABER_WEAR_TIMER] -= msec;
		if (ps->stats[STAT_SABER_WEAR_TIMER] <= 0)
		{
			const int wear_floor = wear > SABERWEAR_CRACKED ? SABERWEAR_CRACKED + 1 : 0;
			if (wear > wear_floor)
			{
				ps->stats[STAT_SABER_WEAR] = wear - 1;
			}
			ps->stats[STAT_SABER_WEAR_TIMER] = 1000;
		}
	}
}