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

#include "b_local.h"
#include "g_nav.h"
#include "anims.h"
#include "w_saber.h"
#include "bg_weapons.h"

extern qboolean BG_SabersOff(const playerState_t* ps);

extern void CG_DrawAlert(vec3_t origin, float rating);
extern void G_AddVoiceEvent(const gentity_t* self, int event, int speak_debounce_time);
extern void ForceJump(gentity_t* self, const usercmd_t* ucmd);
extern qboolean PM_InRoll(const playerState_t* ps);
extern void WP_ResistForcePush(gentity_t* self, const gentity_t* pusher, qboolean no_penalty);
extern qboolean G_EntIsBreakable(int entityNum);

#define	MAX_VIEW_DIST		2048
#define MAX_VIEW_SPEED		100
#define	JEDI_MAX_LIGHT_INTENSITY 64
#define	JEDI_MIN_LIGHT_THRESHOLD 10
#define	JEDI_MAX_LIGHT_THRESHOLD 50

#define	DISTANCE_SCALE		0.25f
#define	SPEED_SCALE			0.25f
#define	FOV_SCALE			0.5f
#define	LIGHT_SCALE			0.25f

#define	REALIZE_THRESHOLD	0.6f
#define CAUTIOUS_THRESHOLD	( REALIZE_THRESHOLD * 0.3 )

#define	MAX_CHECK_THRESHOLD	1

extern void NPC_ClearLookTarget(const gentity_t* self);
extern void NPC_SetLookTarget(const gentity_t* self, int entNum, int clear_time);
extern void NPC_TempLookTarget(const gentity_t* self, int lookEntNum, int minLookTime, int maxLookTime);
extern qboolean G_ExpandPointToBBox(vec3_t point, const vec3_t mins, const vec3_t maxs, int ignore, int clipmask);
extern void G_SoundOnEnt(gentity_t* ent, soundChannel_t channel, const char* sound_path);
void Boba_FireDecide(void);
extern void Player_CheckBurn(const gentity_t* self);
extern void player_Burn(const gentity_t* self);

extern gitem_t* BG_FindItemForAmmo(const ammo_t ammo);
extern void PM_AddFatigue(playerState_t* ps, int fatigue);

extern void ForceThrow(gentity_t* self, qboolean pull);
extern void ForceLightning(gentity_t* self);
extern void ForceHeal(gentity_t* self);
extern void ForceRage(gentity_t* self);
extern void ForceProtect(gentity_t* self);
extern void ForceAbsorb(gentity_t* self);
extern int WP_MissileBlockForBlock(int saber_block);
extern qboolean G_GetHitLocFromSurfName(gentity_t* ent, const char* surfName, int* hit_loc, vec3_t point, vec3_t dir,
	vec3_t blade_dir, int mod);
extern qboolean WP_ForcePowerUsable(const gentity_t* self, forcePowers_t forcePower);
extern qboolean WP_ForcePowerAvailable(const gentity_t* self, forcePowers_t forcePower, int overrideAmt);
extern void WP_ForcePowerStop(gentity_t* self, forcePowers_t forcePower);
extern void WP_DeactivateSaber(const gentity_t* self);
extern void WP_ActivateSaber(gentity_t* self);

extern qboolean PM_SaberInStart(int move);
extern qboolean PM_SaberInSpecialAttack(int anim);
extern qboolean PM_SaberInAttack(int move);
extern qboolean PM_SaberInBounce(const int move);
extern qboolean PM_SaberInParry(int move);
extern qboolean PM_SaberInKnockaway(int move);
extern qboolean PM_SaberInBrokenParry(int move);
extern qboolean PM_SaberInDeflect(int move);
extern qboolean PM_SpinningSaberAnim(int anim);
extern qboolean PM_FlippingAnim(int anim);
extern qboolean PM_RollingAnim(int anim);
extern qboolean PM_InKnockDown(const playerState_t* ps);
extern qboolean BG_InRoll(const playerState_t* ps, int anim);
extern qboolean PM_CrouchAnim(int anim);
void NPC_CheckEvasion(void);
extern void ForceDashAnimDash(gentity_t* self);

extern qboolean NPC_SomeoneLookingAtMe(gentity_t* ent);

extern int WP_GetVelocityForForceJump(const gentity_t* self, vec3_t jump_vel, const usercmd_t* ucmd);

extern void G_TestLine(vec3_t start, vec3_t end, int color, int time);

qboolean Jedi_WaitingAmbush(const gentity_t* self);
extern qboolean in_camera;

extern int bg_parryDebounce[];

static int jediSpeechDebounceTime[TEAM_NUM_TEAMS]; //used to stop several jedi from speaking all at once
//Local state enums
enum
{
	LSTATE_NONE = 0,
	LSTATE_UNDERFIRE,
	LSTATE_INVESTIGATE,
};

enum
{
	BTS_NONE,

	// Attack
	//--------
	BTS_RIFLE,
	// Uses Jedi / Seeker Movement
	BTS_MISSILE,
	// Uses Jedi / Seeker Movement
	BTS_SNIPER,
	// Uses Special Movement Internal To This File
	BTS_FLAMETHROW,
	// Locked In Place
	// Waiting
	//---------
	BTS_AMBUSHWAIT,
	// Goto CP & Wait

	BTS_MAX
};

// ---- externs for the SP port ----
extern qboolean BG_InKnockDown(int anim);
extern qboolean NPC_IsAlive(const gentity_t* self, const gentity_t* npc);
extern qboolean PM_InGetUp(const playerState_t* ps);
extern qboolean PM_SaberInMassiveBounce(int anim);
extern qboolean PM_SaberInBashedAnim(int anim);
extern qboolean PM_SaberInReturn(int move);
extern qboolean G_ValidEnemy(const gentity_t* self, const gentity_t* enemy);
extern qboolean NPC_IsOversized(const gentity_t* self);
extern qboolean IsSurrendering(const gentity_t* self);
extern qboolean IsCowering(const gentity_t* self);
extern void AddNPCBlockPointBonus(const gentity_t* self);
extern qboolean WP_AbsorbKick(gentity_t* hit_ent, gentity_t* pusher, vec3_t push_dir);
extern void ForceStasis(gentity_t* self);
extern void ForceDestruction(gentity_t* self);
extern void ForceLightningStrike(gentity_t* self);
extern void ForceFear(gentity_t* self);
extern qboolean BG_StabDownAnim(int anim);
extern int bg_parryDebounce[];
#ifndef SETANIM_AFLAG_BLOCKPACE
#define SETANIM_AFLAG_BLOCKPACE (SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD | SETANIM_FLAG_RESTART)
#endif
extern qboolean G_ControlledByPlayer(const gentity_t* self);
extern qboolean BG_IsAlreadyinTauntAnim(int anim);
extern qboolean PM_SaberInnonblockableAttack(int anim_in);
extern qboolean PM_Saberinstab(int move);
// SP's g_SerenityJediEngineMode: MP always uses mode 2 (block points)
static int jedi_engine_mode = 2;
// SP's in_camera: MP has no SP cinematic camera
static qboolean jedi_in_camera = qfalse;


// ---- SP top-level declarations (ported) ----
/*
#============================================================================
# Copyright (C) 2013 - 2018, SerenityJediEngine2027 contributors
#
# This file is part of the SerenityJediEngine2027 source code.
#
# SerenityJediEngine2027 is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, see <http://www.gnu.org/licenses/>.
#============================================================================
*/


/// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// ///
///																																///
///																																///
///													SERENITY JEDI ENGINE														///
///										          LIGHTSABER COMBAT SYSTEM													    ///
///																																///
///						      System designed by Serenity and modded by JaceSolaris. (c) 2019 SJE   		                    ///
///								    https://www.moddb.com/mods/movie-duels											///
///																																///
/// /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// ///

//Externs

//Locals

















































































/*
-------------------------
Jedi_SaberBlock

Pick proper block anim

FIXME: Based on difficulty level/enemy saber combat skill, make this decision-making more/less effective

NOTE: always blocking projectiles in this func!

-------------------------
*/

































////////////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////////////













/*
-------------------------
NPC_BSJedi_FollowLeader
-------------------------
*/









/*
-------------------------
Jedi_Attack
-------------------------
*/

// =====================================================
// Jedi_Attack Cinematic Hybrid System
// =====================================================





// =====================
// Combo Container
// =====================

typedef struct saberCombo_s
{
	qboolean          valid;
	saberMoveName_t  moves[4];
	int               length;
	qboolean          useForcePushFinisher;
} saberCombo_t;

// =====================
// Cinematic Signature Combos
// =====================

// Fast style (agile, mostly directional, occasional acrobatics)
static const saberMoveName_t s_fastSignatureCombos[][4] =
{
	// Pure directional, quick exchanges
	{ LS_A_L2R,   LS_A_R2L,   LS_A_T2B,          LS_NONE },
	{ LS_A_TL2BR, LS_A_TR2BL, LS_A_T2B,          LS_NONE },
	{ LS_A_BL2TR, LS_A_BR2TL, LS_A_T2B,          LS_NONE },

	// Directional into light acrobatic finisher
	{ LS_A_L2R,   LS_A_R2L,   LS_A_LUNGE,        LS_NONE },
	{ LS_A_TL2BR, LS_A_TR2BL, LS_A_FLIP_SLASH,   LS_NONE },

	// Slightly more cinematic: special in the middle, directional close
	{ LS_A_FLIP_STAB, LS_A_R2L, LS_A_T2B,        LS_NONE }
};

// Medium style (balanced, classic Jedi duelist)
static const saberMoveName_t s_mediumSignatureCombos[][4] =
{
	// Core directional chains
	{ LS_A_TL2BR, LS_A_L2R,   LS_A_TR2BL,        LS_NONE },
	{ LS_A_BL2TR, LS_A_BR2TL, LS_A_T2B,          LS_NONE },
	{ LS_A_L2R,   LS_A_T2B,   LS_A_R2L,          LS_NONE },

	// Directional into a controlled special finisher
	{ LS_A_TL2BR, LS_A_TR2BL, LS_SPINATTACK,     LS_NONE },
	{ LS_A_L2R,   LS_A_R2L,   LS_A_BACKFLIP_ATK, LS_NONE },

	// Slightly more cinematic: special in the middle
	{ LS_A_LUNGE, LS_A_R2L,   LS_A_T2B,          LS_NONE }
};

// Strong style (heavy, aggressive, Sith-like)
static const saberMoveName_t s_strongSignatureCombos[][4] =
{
	// Heavy directional pressure
	{ LS_A_T2B,   LS_A_BR2TL, LS_A_T2B,          LS_NONE },
	{ LS_A_R2L,   LS_A_L2R,   LS_A_T2B,          LS_NONE },
	{ LS_A_TR2BL, LS_A_TL2BR, LS_A_T2B,          LS_NONE },

	// Directional into brutal special finisher
	{ LS_A_T2B,   LS_A_BR2TL, LS_SPINATTACK,     LS_NONE },
	{ LS_A_R2L,   LS_A_T2B,   LS_A_LUNGE,        LS_NONE },

	// Cinematic: special in the middle, heavy close
	{ LS_A_FLIP_SLASH, LS_A_R2L, LS_A_T2B,       LS_NONE }
};

// Dual style (flashy, flowing, more specials but still grounded)
static const saberMoveName_t s_dualSignatureCombos[][4] =
{
	// Directional dual chains
	{ LS_A_L2R,   LS_A_R2L,   LS_A_T2B,              LS_NONE },
	{ LS_A_TL2BR, LS_A_TR2BL, LS_A_T2B,              LS_NONE },

	// Dual spin as finisher
	{ LS_A_L2R,   LS_A_R2L,   LS_DUAL_SPIN_PROTECT,  LS_NONE },
	{ LS_A_BL2TR, LS_A_BR2TL, LS_SPINATTACK_DUAL,    LS_NONE },

	// Cinematic: special in the middle
	{ LS_SPINATTACK_ALORA, LS_A_R2L, LS_A_T2B,       LS_NONE }
};

// Staff style (wide arcs, Maul-like, a bit more special-heavy)
static const saberMoveName_t s_staffSignatureCombos[][4] =
{
	// Wide directional arcs
	{ LS_A_L2R,   LS_A_R2L,   LS_A_T2B,          LS_NONE },
	{ LS_A_TL2BR, LS_A_TR2BL, LS_A_T2B,          LS_NONE },

	// Staff special as finisher
	{ LS_A_L2R,   LS_A_R2L,   LS_STAFF_SOULCAL,  LS_NONE },
	{ LS_A_T2B,   LS_A_BR2TL, LS_STAFF_SOULCAL,  LS_NONE },

	// Cinematic: special in the middle
	{ LS_STAFF_SOULCAL, LS_A_R2L, LS_A_T2B,      LS_NONE }
};

// Light cinematic pool (shorter, safer)
static const saberMoveName_t s_lightComboPool[][3] =
{
	{ LS_A_L2R,   LS_A_R2L,   LS_NONE },
	{ LS_A_TL2BR, LS_A_TR2BL, LS_NONE },
	{ LS_A_BL2TR, LS_A_BR2TL, LS_NONE },
	{ LS_A_R2L,   LS_A_T2B,   LS_NONE },
	{ LS_A_L2R,   LS_A_T2B,   LS_NONE }
};

// Heavy/aggressive pool (longer, riskier)
static const saberMoveName_t s_heavyComboPool[][4] =
{
	{ LS_A_T2B,   LS_A_BR2TL, LS_A_T2B,        LS_NONE },
	{ LS_A_R2L,   LS_A_L2R,   LS_A_T2B,        LS_NONE },
	{ LS_A_TL2BR, LS_A_TR2BL, LS_A_T2B,        LS_NONE },

	// Occasional cinematic finisher
	{ LS_A_L2R,   LS_A_R2L,   LS_SPINATTACK,   LS_NONE },
	{ LS_A_T2B,   LS_A_BR2TL, LS_A_LUNGE,      LS_NONE }
};

static const saberMoveName_t s_duelistCounterPool[][3] =
{
	{ LS_A_R2L,   LS_A_T2B,   LS_NONE },
	{ LS_A_L2R,   LS_A_T2B,   LS_NONE },
	{ LS_A_TR2BL, LS_A_TL2BR, LS_NONE },

	// Occasional stylish punish
	{ LS_A_LUNGE, LS_A_T2B,   LS_NONE }
};









// =====================
// Counterattack Handler
// =====================
#define AIFLAG_COUNTERATTACK ((1 << 20))
#define AIFLAG_HEAVYCOUNTER ((1 << 21))
#define AIFLAG_RIPOSTE ((1 << 22))
#define AIFLAG_FORCECOUNTER ((1 << 23))
#define AIFLAG_SPINCOUNTER ((1 << 24))

















// ---- MP top-level declarations ----


extern void G_Knockdown(gentity_t* self, gentity_t* attacker, const vec3_t push_dir, float strength, const qboolean breakSaberLock);


extern void ChangeWeapon(const gentity_t* ent, int new_weapon);


float G_GroundDistance(const gentity_t* self);


#define	BOBA_FLAMEDURATION			3000

typedef struct wristWeapon_s
{
	int theMissile;
	int dummyForcePower;
	int whichWeapon;
	qboolean alt_fire;
	int maxShots;
	int animTimer;
	int animDelay;
	int fireAnim;
	qboolean fullyCharged;
	qboolean leftBolt;
	qboolean hold;
} wristWeapon_t;

wristWeapon_t missileStates[4] = {
	{
		BOBA_MISSILE_ROCKET, FP_FIRST, WP_ROCKET_LAUNCHER, qfalse, 1, BOBA_FLAMEDURATION, 150, BOTH_FLAMETHROWER,
		qfalse, qtrue, qtrue
	},
	{
		BOBA_MISSILE_LASER, FP_GRIP, WP_EMPLACED_GUN, qfalse, 5, BOBA_FLAMEDURATION, 150, BOTH_FLAMETHROWER, qfalse,
		qtrue, qtrue
	},
	{ BOBA_MISSILE_DART, FP_FIRST, WP_DISRUPTOR, qfalse, 1, 1500, 200, BOTH_PULL_IMPALE_STAB, qfalse, qfalse, qtrue },
	{ BOBA_MISSILE_VIBROBLADE, FP_DRAIN, WP_MELEE, qfalse, 1, 1000, 100, BOTH_PULL_IMPALE_STAB, qfalse, qfalse, qfalse }
};

extern void WP_FireWristMissile(gentity_t* ent, vec3_t start, vec3_t dir);


////////////////////////////////////////////////////////////////////////////////////////
// Defines
////////////////////////////////////////////////////////////////////////////////////////
#define		BOBA_FLAMEDURATION			3000
#define		BOBA_FLAMETHROWRANGE		128
#define		BOBA_FLAMETHROWSIZE			40
#define		BOBA_FLAMETHROWDAMAGEMIN	1//10
#define		BOBA_FLAMETHROWDAMAGEMAX	5//40
#define		BOBA_ROCKETRANGEMIN			300
#define		BOBA_ROCKETRANGEMAX			2000


extern qboolean PM_PainAnim(int anim);


extern qboolean WP_SaberCanTurnOffSomeBlades(const saberInfo_t* saber);
extern qboolean G_ValidSaberStyle(const gentity_t* ent, int saber_style);


extern qboolean G_InGetUpAnim(const playerState_t* ps);


extern qboolean PM_InOnGroundAnim(const int anim);


extern qboolean G_CanKickEntity(const gentity_t* self, const gentity_t* target);
extern saberMoveName_t G_PickAutoKick(gentity_t* self, const gentity_t* enemy);
extern float NPC_EnemyRangeFromBolt(int boltIndex);
extern qboolean PM_SaberInTransition(int move);
extern void ForceDrain(gentity_t* self);
extern qboolean PM_CrouchAnim(int anim);
static qboolean shoot;
static float enemyDist;
#define MELEE_DIST_SQUARED 6400


extern int PM_AnimLength(const animNumber_t anim);


extern qboolean PM_InRollIgnoreTimer(const playerState_t* ps);
extern qboolean PM_InAirKickingAnim(int anim);
extern qboolean BG_StabDownAnim(int anim);
extern qboolean PM_SuperBreakLoseAnim(int anim);
extern qboolean PM_SuperBreakWinAnim(int anim);


extern qboolean FlyingCreature(const gentity_t* ent);
extern qboolean NAV_DirSafe(const gentity_t* self, vec3_t dir, float dist);
extern qboolean NAV_MoveDirSafe(const gentity_t* self, const usercmd_t* cmd, float distScale);


/*
-------------------------
Jedi_SaberBlock

Pick proper block anim

FIXME: Based on difficulty level/enemy saber combat skill, make this decision-making more/less effective

NOTE: always blocking projectiles in this func!

-------------------------
*/
extern qboolean PM_LockedAnim(int anim);
extern qboolean G_FindClosestPointOnLineSegment(const vec3_t start, const vec3_t end, const vec3_t from, vec3_t result);


extern float wp_saber_length(const gentity_t* ent);


extern float ShortestLineSegBewteen2LineSegs(vec3_t start1, vec3_t end1, vec3_t start2, vec3_t end2, vec3_t close_pnt1,
	vec3_t close_pnt2);


extern qboolean NPC_CheckFallPositionOK(const gentity_t* NPC, vec3_t position);


static qboolean enemy_in_striking_range = qfalse;


extern float WP_SpeedOfMissileForWeapon(int wp, qboolean alt_fire);


#define	APEX_HEIGHT		200.0f
#define	PARA_WIDTH		(sqrt(APEX_HEIGHT)+sqrt(APEX_HEIGHT))
#define	JUMP_SPEED		200.0f


extern void G_UcmdMoveForDir(const gentity_t* self, usercmd_t* cmd, vec3_t dir);


extern void RT_FireDecide(void);


extern qboolean G_ClearLineOfSight(const vec3_t point1, const vec3_t point2, int ignore, int clipmask);


#define TWINS_DANGER_DIST_EASY (128.0f*128.0f)
#define TWINS_DANGER_DIST_MEDIUM (192.0f*192.0f)
#define TWINS_DANGER_DIST_HARD (256.0f*256.0f)


extern void WP_Explode(gentity_t* self);


extern void NPC_BSST_Patrol(void);
extern void NPC_BSSniper_Default(void);

// ---- prototypes ----
static int jedi_get_kata_cooldown_duration(void);
static qboolean Jedi_Trainer(const gentity_t* self);
static qboolean NPC_IsJedi(const gentity_t* self);
static qboolean npc_force_master(const gentity_t* self);
static qboolean npc_is_sith_lord(const gentity_t* self);
static qboolean npc_should_not_throw_saber(const gentity_t* self);
qboolean npc_is_light_jedi(const gentity_t* self);
qboolean npc_is_dark_jedi(const gentity_t* self);
static qboolean npc_is_staff_style(const gentity_t* self);
static qboolean npc_is_dual_style(const gentity_t* self);
static qboolean npc_can_do_slap(void);
void npc_cultist_destroyer_precache(void);
void npc_shadow_trooper_precache(void);
void npc_rosh_dark_precache(void);
static void npc_sbd_precache(void);
void Jedi_ClearTimers(const gentity_t* ent);
qboolean Jedi_CultistDestroyer(const gentity_t* self);
static qboolean Jedi_CultistForceUser(const gentity_t* self);
static qboolean jedi_forbidden_kicker(const gentity_t* self);
void Jedi_PlayBlockedPushSound(const gentity_t* self);
void Jedi_PlayDeflectSound(const gentity_t* self);
void NPC_Jedi_PlayConfusionSound(const gentity_t* self);
qboolean Jedi_StopKnockdown(gentity_t* self, const vec3_t push_dir);
void NPC_TavionScepter_Precache(void);
void NPC_TavionSithSword_Precache(void);
static void Tavion_ScepterDamage(void);
static void Tavion_ScepterSlam(void);
static void Tavion_StartScepterBeam(void);
static void Tavion_StartScepterSlam(void);
static void Tavion_SithSwordRecharge(void);
static void player_Decloak(gentity_t* self);
static qboolean self_is_gunner(const gentity_t* self);
void player_Cloak(gentity_t* self);
void Jedi_Cloak(gentity_t* self);
void Jedi_Decloak(gentity_t* self);
static void Jedi_CheckCloak(void);
static void Self_CheckCloak(gentity_t* self);
static void Jedi_Aggression(const gentity_t* self, const int change);
static void Jedi_AggressionErosion(const int amt);
void NPC_Jedi_RateNewEnemy(const gentity_t* self, const gentity_t* enemy);
static void Jedi_Rage(void);
static void Jedi_RageStop(const gentity_t* self);
static qboolean Jedi_BattleTaunt(void);
static qboolean Jedi_ClearPathToSpot(vec3_t dest, const int impact_ent_num);
qboolean NPC_MoveDirClear(const int forwardmove, const int rightmove, const qboolean reset);
static void Jedi_HoldPosition(void);
static qboolean Jedi_Move(gentity_t* goal, const qboolean retreat);
static qboolean Jedi_Hunt(void);
static void Jedi_StartBackOff(void);
static void JediDirectionalDashDodge(gentity_t* NPC, gentity_t* enemy);
static void JediDirectionalDashAttack(gentity_t* NPC, gentity_t* enemy);
static qboolean Jedi_Track(void);
static qboolean Jedi_Retreat(void);
static qboolean Jedi_Advance(void);
static void Jedi_AdjustSaberAnimLevel(const gentity_t* self, int new_level);
static void Jedi_CheckDecreasesaber_anim_level(void);
static qboolean Jedi_DecideKick(void);
void Kyle_GrabEnemy(void);
static void NPC_GrabPlayer(void);
void Kyle_TryGrab(void);
static qboolean Kyle_CanDoGrab(void);
static qboolean NPC_HandleSlapMelee(gentity_t* NPC, gentity_t* enemy, int enemyDist);
static void Jedi_CombatDistance(const int enemy_dist);
static qboolean Jedi_Strafe(const int strafe_time_min, const int strafe_time_max, const int next_strafe_time_min, const int next_strafe_time_max, const qboolean walking);
static evasionType_t Jedi_CheckFlipEvasions(gentity_t* self, const float rightdot);
int Jedi_ReCalcParryTime(const gentity_t* self, const evasionType_t evasion_type);
static qboolean Jedi_SaberBusy(const gentity_t* self);
static qboolean Jedi_InNoAIAnim(const gentity_t* self);
static void Jedi_CheckJumpEvasionSafety(usercmd_t* cmd, const evasionType_t evasion_type);
evasionType_t jedi_saber_block_go(gentity_t* self, usercmd_t* cmd, vec3_t p_hitloc, vec3_t phit_dir, const gentity_t* incoming, const float dist);
static evasionType_t Jedi_CheckEvadeSpecialAttacks(void);
static qboolean Jedi_SaberBlock(void);
static qboolean Jedi_EvasionRoll(gentity_t* ai_ent);
static void Jedi_EvasionSaber(vec3_t enemy_movedir, const float enemy_dist, vec3_t enemy_dir);
static gentity_t* Jedi_FindEnemyInCone(const gentity_t* self, gentity_t* fallback, const float minDot);
static void jedi_set_enemy_info(vec3_t enemy_dest, vec3_t enemy_dir, float* enemy_dist, vec3_t enemy_movedir, float* enemy_movespeed, const int prediction);
void NPC_EvasionSaber(void);
static void Jedi_FaceEnemy(const qboolean doPitch);
static void Jedi_DebounceDirectionChanges(void);
void Jedi_TimersApply(void);
static void Jedi_CombatTimersUpdate(const int enemy_dist);
static void Jedi_CombatIdle(const int enemy_dist);
static qboolean Jedi_AttackDecide(const int enemy_dist);
static qboolean Jedi_Jump(vec3_t dest, const int goal_ent_num);
static qboolean Jedi_TryJump(const gentity_t* goal);
static qboolean Jedi_Jumping(const gentity_t* goal);
static void Jedi_CheckEnemyMovement(const float enemy_dist);
static void Jedi_CheckJumps(void);
void RT_CheckJump(void);
static void Jedi_Combat(void);
void NPC_Jedi_Pain(gentity_t* self, gentity_t* attacker, const int damage);
static qboolean Jedi_CheckDanger(void);
qboolean Jedi_CheckAmbushPlayer(void);
void Jedi_Ambush(gentity_t* self);
qboolean Jedi_WaitingAmbush(const gentity_t* self);
static void Jedi_Patrol(void);
static qboolean Jedi_CanPullBackSaber(const gentity_t* self);
void NPC_BSJedi_FollowLeader(void);
static qboolean NPC_CanDoKata(gentity_t* self);
static qboolean Jedi_CheckKataAttack(void);
static qboolean NPC_IsExcludedForGestures(gentity_t* NPC);
static qboolean NPC_CanReactToEnemy(gentity_t* NPC, gentity_t* enemy);
static void NPC_PlayGloatAndMaybeSheathe(gentity_t* NPC);
static void NPC_HandleSpeechDebounceAndIncrement(gentity_t* NPC);
static inline void JediPressAttack(void);
static inline void JediPressAltAttack(void);
static float JediDistanceToEnemy(gentity_t* self);
static qboolean JediInAttackRange(gentity_t* self, float minDist, float maxDist);
static qboolean JediHasLineOfSight(gentity_t* self);
static qboolean JediAttackCooldownReady(gentity_t* self);
static void JediSetAttackCooldown(gentity_t* self, int ms);
static qboolean Jedi_CanUseKata(gentity_t* self);
static void JediHandleSpacing(gentity_t* self);
static void JediHandleCounterattacks(gentity_t* self);
static saberCombo_t JediBuildCombo(const saberMoveName_t* moves, int maxLen);
static saberCombo_t JediChooseCombo(gentity_t* self);
static void JediExecuteCombo(gentity_t* self, const saberCombo_t* combo);
static qboolean JediShouldAttack(gentity_t* self);
static void Jedi_Attack(void);
qboolean Rosh_BeingHealed(const gentity_t* self);
static qboolean Rosh_TwinPresent(void);
static qboolean Rosh_TwinNearBy(const gentity_t* self);
qboolean Kothos_HealRosh(void);
void Kothos_PowerRosh(void);
static qboolean Kothos_Retreat(void);
static float Twins_DangerDist(void);
static qboolean Jedi_InSpecialMove(void);
void NPC_CheckEvasion(void);
void NPC_BSJedi_Default(void);
void Boba_Precache(void);
void Boba_ChangeWeapon(const int wp);
qboolean Boba_StopKnockdown(gentity_t* self, const gentity_t* pusher, const vec3_t push_dir, const qboolean force_knockdown);
static void Boba_SetJetpackAnims(const gentity_t* self);
void Boba_FlyStart(gentity_t* self);
void Boba_ForceFlyStart(gentity_t* self, const int jetTime);
void Boba_FlyStop(gentity_t* self);
qboolean Boba_Flying(const gentity_t* self);
void Boba_FlyThink(gentity_t* self);
void Boba_SetBolts(const gentity_t* self);
void Boba_JetpackDecideDefault(void);
void Boba_JetpackDecideCombat(void);
void Boba_ChooseWeapon(void);
void Boba_FireFlameThrower(gentity_t* self);
void Boba_StartFlameThrower(gentity_t* self);
void Boba_DoFlameThrower(gentity_t* self);
void Boba_DoSniper(const gentity_t* self);
void Boba_FireWristMissile(gentity_t* self, const int whichMissile);
void Boba_EndWristMissile(const gentity_t* self, const int which_missile);
void Boba_TacticsSelect();
void Boba_DoAmbushWait(gentity_t* self);
void Boba_Tactics();
float G_GroundDistance(const gentity_t* self);
void Boba_FireDecide(void);
void Sphereshield_On(gentity_t* self);
void Sphereshield_Off(gentity_t* self);
saberMoveName_t G_PickAutoMultiKick(gentity_t* self, qboolean allowSingles, qboolean storeMove);
qboolean Jedi_QuickReactions(gentity_t* self);
static qboolean PM_EvasionAnim(const int anim);
static qboolean PM_EvasionHoldAnim(const int anim);
static qboolean NPC_Should_Block(const gentity_t* npc);
static qboolean wp_saber_Off_Dash_Evasion(gentity_t* self, vec3_t hitloc);
static qboolean wp_saber_block_check_random(gentity_t* self, vec3_t hitloc);

// MP has no "being grasped" / "being drained" flag on the victim (SP EF_FORCE_GRASPED / EF_FORCE_DRAINED)
static qboolean jedi_being_grasped(const gentity_t* ent)
{
	if (!ent)
	{
		return qfalse;
	}
	for (int i = 0; i < level.num_entities; i++)
	{
		const gentity_t* holder = &g_entities[i];
		if (holder->inuse && holder->client && holder->client->graspEntityNum == ent->s.number + 1)
		{
			return qtrue;
		}
	}
	return qfalse;
}

static qboolean jedi_being_drained(const gentity_t* ent)
{
	if (!ent)
	{
		return qfalse;
	}
	for (int i = 0; i < level.num_entities; i++)
	{
		const gentity_t* drainer = &g_entities[i];
		if (drainer->inuse && drainer->client && drainer->client->ps.fd.forcePowersActive & 1 << FP_DRAIN
			&& drainer->client->ps.fd.forceDrainEntNum == ent->s.number)
		{
			return qtrue;
		}
	}
	return qfalse;
}



// Difficulty-based special attack helpers
static int jedi_get_kata_cooldown_duration(void)
{
	// Kata cooldown duration based on difficulty
	// Padawan (g_spskill 0): 10-16 seconds (more recovery time)
	// Jedi (g_spskill 1): 7-12 seconds (standard)
	// Jedi Knight / Jedi Master (g_spskill 2): 5-9 seconds (faster attacks)
	switch (g_npcspskill.integer)
	{
	case 0: // Padawan
		return Q_irand(10000, 16000);
	case 1: // Jedi
		return Q_irand(7000, 12000);
	case 2: // Jedi Knight / Jedi Master
	default:
		return Q_irand(5000, 9000);
	}
}



static qboolean Jedi_Trainer(const gentity_t* self)
{
	if (!self || !self->client)
	{
		return qfalse;
	}
	if (self->client->NPC_class == CLASS_KYLE
		&& self->s.weapon == WP_SABER
		&& Q_stricmp("JediSaberTrainer", self->NPC_type) == 0)
	{
		return qtrue;
	}
	return qfalse;
}



static qboolean NPC_IsJedi(const gentity_t* self)
{
	switch (self->client->NPC_class)
	{
	case CLASS_SITHLORD:
	case CLASS_DESANN:
	case CLASS_VADER:
	case CLASS_JEDI:
	case CLASS_KYLE:
	case CLASS_LUKE:
	case CLASS_MORGANKATARN:
	case CLASS_REBORN:
	case CLASS_SHADOWTROOPER:
	case CLASS_TAVION:
	case CLASS_ALORA:
	case CLASS_YODA:
		// Is Jedi...
		return qtrue;
	default:
		// NOT Jedi...
		break;
	}

	return qfalse;
}



static qboolean npc_force_master(const gentity_t* self)
{
	switch (self->client->NPC_class)
	{
	case CLASS_SITHLORD:
	case CLASS_REBORN:
		return qtrue;
	default:
		// NOT Jedi...
		break;
	}

	return qfalse;
}



static qboolean npc_is_sith_lord(const gentity_t* self)
{
	switch (self->client->NPC_class)
	{
	case CLASS_DESANN:
	case CLASS_TAVION:
	case CLASS_VADER:
	case CLASS_SITHLORD:
		return qtrue;
	default:
		break;
	}

	return qfalse;
}



static qboolean npc_should_not_throw_saber(const gentity_t* self)
{
	switch (self->client->NPC_class)
	{
	case CLASS_DESANN:
	case CLASS_TAVION:
	case CLASS_VADER:
	case CLASS_SITHLORD:
	case CLASS_KYLE:
	case CLASS_LUKE:
		return qtrue;
	default:
		break;
	}

	return qfalse;
}



qboolean npc_is_light_jedi(const gentity_t* self)
{
	switch (self->client->NPC_class)
	{
	case CLASS_JEDI:
	case CLASS_KYLE:
	case CLASS_LUKE:
	case CLASS_MONMOTHA:
	case CLASS_MORGANKATARN:
	case CLASS_YODA:
		// Is Jedi...
		return qtrue;
	default:
		// NOT Jedi...
		break;
	}

	return qfalse;
}



qboolean npc_is_dark_jedi(const gentity_t* self)
{
	switch (self->client->NPC_class)
	{
	case CLASS_ALORA:
	case CLASS_DESANN:
	case CLASS_REBORN:
	case CLASS_SHADOWTROOPER:
	case CLASS_TAVION:
	case CLASS_VADER:
	case CLASS_SITHLORD:
		// Is Jedi...
		return qtrue;
	default:
		// NOT Jedi...
		break;
	}

	return qfalse;
}



static qboolean npc_is_staff_style(const gentity_t* self)
{
	if (!Q_stricmp("md_magnaguard", self->NPC_type)
		|| !Q_stricmp("md_inquisitor", self->NPC_type)
		|| !Q_stricmp("md_inquisitor_rebels", self->NPC_type)
		|| !Q_stricmp("md_templeguard_inquisitor", self->NPC_type)
		|| !Q_stricmp("md_5thbrother", self->NPC_type)
		|| !Q_stricmp("md_7thsister", self->NPC_type)
		|| !Q_stricmp("md_8thbrother", self->NPC_type)
		|| !Q_stricmp("md_maul_rebels", self->NPC_type)
		|| !Q_stricmp("md_maul_rebels2", self->NPC_type)
		|| !Q_stricmp("md_maul_rebels3", self->NPC_type)
		|| !Q_stricmp("md_maul_rebels4", self->NPC_type)
		|| !Q_stricmp("md_maul_rebels5", self->NPC_type)
		|| !Q_stricmp("md_maul_rebels6", self->NPC_type)
		|| !Q_stricmp("md_jbrute", self->NPC_type)
		|| !Q_stricmp("md_templeguard", self->NPC_type)
		|| !Q_stricmp("md_sidious_tcw", self->NPC_type)
		//
		|| !Q_stricmp("md_maul_tcw_staff", self->NPC_type)
		|| !Q_stricmp("md_maul_solo", self->NPC_type)
		|| !Q_stricmp("md_maul_solo_hood", self->NPC_type)
		|| !Q_stricmp("md_mau_luke", self->NPC_type)
		|| !Q_stricmp("md_mau_luke_robed", self->NPC_type)
		|| !Q_stricmp("md_mau_luke_hooded", self->NPC_type)
		//
		|| !Q_stricmp("md_savage", self->NPC_type)
		|| !Q_stricmp("md_galen", self->NPC_type)
		|| !Q_stricmp("md_galen_jt", self->NPC_type)
		|| !Q_stricmp("md_galencjr", self->NPC_type)
		|| !Q_stricmp("md_starkiller", self->NPC_type)
		|| !Q_stricmp("md_sithstalker", self->NPC_type)
		|| !Q_stricmp("md_stk_lord", self->NPC_type)
		|| !Q_stricmp("md_stk_tat", self->NPC_type)
		|| !Q_stricmp("darthdesolous", self->NPC_type)
		|| !Q_stricmp("purge_trooper", self->NPC_type)
		|| !Q_stricmp("cal_kestis_staff", self->NPC_type)
		|| !Q_stricmp("md_2ndsister", self->NPC_type)
		|| !Q_stricmp("md_shadowguard", self->NPC_type)
		|| !Q_stricmp("md_ven_dual", self->NPC_type)
		|| !Q_stricmp("md_pguard5", self->NPC_type)
		|| !Q_stricmp("md_mau_dof", self->NPC_type)
		|| !Q_stricmp("md_mau2_dof", self->NPC_type)
		|| !Q_stricmp("md_mau3_dof", self->NPC_type)
		|| !Q_stricmp("md_mag_egg", self->NPC_type)
		|| !Q_stricmp("md_mag_ga", self->NPC_type)
		|| !Q_stricmp("md_ven_ga", self->NPC_type)
		|| !Q_stricmp("darthphobos", self->NPC_type)
		|| !Q_stricmp("md_tus5_tc", self->NPC_type)
		|| !Q_stricmp("md_sta_tfu", self->NPC_type)
		|| !Q_stricmp("md_gua1_tfu", self->NPC_type)
		|| !Q_stricmp("md_sta_cs", self->NPC_type)
		|| !Q_stricmp("md_gua_am", self->NPC_type)
		|| !Q_stricmp("md_gua2_am", self->NPC_type)
		|| !Q_stricmp("md_mag_am", self->NPC_type)
		|| !Q_stricmp("md_mag2_am", self->NPC_type)
		|| !Q_stricmp("JediF", self->NPC_type)
		|| !Q_stricmp("JediMaster", self->NPC_type)
		|| !Q_stricmp("jedi_kdm1", self->NPC_type)
		|| !Q_stricmp("jedi_tf1", self->NPC_type)
		|| !Q_stricmp("reborn_staff", self->NPC_type)
		|| !Q_stricmp("reborn_staff2", self->NPC_type)
		|| !Q_stricmp("RebornMasterStaff", self->NPC_type)
		|| !Q_stricmp("md_jed7_jt", self->NPC_type)
		|| !Q_stricmp("md_jed12_jt", self->NPC_type)
		|| !Q_stricmp("md_jed14_jt", self->NPC_type)
		|| !Q_stricmp("md_jedimaster3_jt", self->NPC_type)
		|| !Q_stricmp("md_jedimaster5_jt", self->NPC_type)
		|| !Q_stricmp("md_guard_jt", self->NPC_type)
		|| !Q_stricmp("md_guardboss_jt", self->NPC_type)
		|| !Q_stricmp("md_jedibrute_jt", self->NPC_type)
		|| !Q_stricmp("md_maul", self->NPC_type)
		|| !Q_stricmp("md_maul_robed", self->NPC_type)
		|| !Q_stricmp("md_maul_hooded", self->NPC_type)
		|| !Q_stricmp("md_maul_wots", self->NPC_type)
		|| !Q_stricmp("md_vindican", self->NPC_type)
		|| !Q_stricmp("md_vindican_hooded", self->NPC_type)
		|| !Q_stricmp("md_bandon", self->NPC_type)
		|| !Q_stricmp("md_bastila", self->NPC_type)
		|| !Q_stricmp("md_bastila_robed", self->NPC_type)
		|| !Q_stricmp("md_bastila_dark", self->NPC_type))
	{
		//staff only
		return qtrue;
	}

	return qfalse;
}



static qboolean npc_is_dual_style(const gentity_t* self)
{
	// holding two sabers (an NPCS.NPC file's saber2, or a script's SET_SABER2 - e.g. Barriss in the Clone Wars
	// mission): dual, whatever the NPCS.NPC type. The names below are only the NPCs that start with two.
	if (self->client && (self->client->saber[1].model[0] != 0))
	{
		return qtrue;
	}
	if (!Q_stricmp("md_grievous", self->NPC_type)
		|| !Q_stricmp("md_grievous4", self->NPC_type)
		|| !Q_stricmp("md_grievous_robed", self->NPC_type)
		|| !Q_stricmp("md_clone_assassin", self->NPC_type)
		|| !Q_stricmp("md_jango", self->NPC_type)
		|| !Q_stricmp("md_jango_geo", self->NPC_type)
		|| !Q_stricmp("md_ani_ep2_dual", self->NPC_type)
		|| !Q_stricmp("md_serra", self->NPC_type)
		|| !Q_stricmp("md_ahsoka", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_padawan", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_tcw", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_eva", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_totj", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_mortis", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_s7", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_s7_r", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_s7_y", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_s7_y_r", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_rebels", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_rebels_poncho", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_tm", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_h_tm", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_p_tm", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_ahs", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_h_ahs", self->NPC_type)
		|| !Q_stricmp("md_ahsoka_p_ahs", self->NPC_type)
		|| !Q_stricmp("md_ventress", self->NPC_type)
		|| !Q_stricmp("md_ven_ns", self->NPC_type)
		|| !Q_stricmp("md_ven_bh", self->NPC_type)
		|| !Q_stricmp("md_ven_dg", self->NPC_type)
		|| !Q_stricmp("md_asharad", self->NPC_type)
		|| !Q_stricmp("md_asharad_tus", self->NPC_type)
		|| !Q_stricmp("boba_fett", self->NPC_type)
		|| !Q_stricmp("boba_fett_esb", self->NPC_type)
		|| !Q_stricmp("md_pguard4", self->NPC_type)
		|| !Q_stricmp("md_grie_egg", self->NPC_type)
		|| !Q_stricmp("md_grie3_egg", self->NPC_type)
		|| !Q_stricmp("md_grie4_egg", self->NPC_type)
		|| !Q_stricmp("md_fet_ga", self->NPC_type)
		|| !Q_stricmp("md_fet2_ga", self->NPC_type)
		|| !Q_stricmp("md_fet3_ga", self->NPC_type)
		|| !Q_stricmp("md_ven2_ga", self->NPC_type)
		|| !Q_stricmp("md_ket_jt", self->NPC_type)
		|| !Q_stricmp("md_fet_ka", self->NPC_type)
		|| !Q_stricmp("md_fet2_ka", self->NPC_type)
		|| !Q_stricmp("md_clo2_rt", self->NPC_type)
		|| !Q_stricmp("md_clo5_rt", self->NPC_type)
		|| !Q_stricmp("reborn_dual", self->NPC_type)
		|| !Q_stricmp("reborn_dual2", self->NPC_type)
		|| !Q_stricmp("alora_dual", self->NPC_type)
		|| !Q_stricmp("JediTrainer", self->NPC_type)
		|| !Q_stricmp("jedi_zf2", self->NPC_type)
		|| !Q_stricmp("RebornMasterDual", self->NPC_type)
		|| !Q_stricmp("Tavion_scepter", self->NPC_type)
		|| !Q_stricmp("md_jed6_jt", self->NPC_type)
		|| !Q_stricmp("md_jed11_jt", self->NPC_type)
		|| !Q_stricmp("md_jed13_jt", self->NPC_type)
		|| !Q_stricmp("md_jediknight2_jt", self->NPC_type)
		|| !Q_stricmp("md_jediveteran2_jt", self->NPC_type)
		|| !Q_stricmp("md_jediveteran3_jt", self->NPC_type)
		|| !Q_stricmp("md_serra_jt", self->NPC_type)
		|| !Q_stricmp("md_jango_dual", self->NPC_type)
		|| !Q_stricmp("stormie_tfa_riot", self->NPC_type)
		|| !Q_stricmp("armorer", self->NPC_type)
		|| !Q_stricmp("md_malicos", self->NPC_type)
		|| !Q_stricmp("purge_trooper_dual", self->NPC_type)
		|| !Q_stricmp("md_krayt", self->NPC_type)
		|| !Q_stricmp("md_krayt_reborn", self->NPC_type)
		|| !Q_stricmp("md_boc", self->NPC_type)
		|| !Q_stricmp("md_maul_tcw_dual", self->NPC_type)
		|| !Q_stricmp("md_revanr", self->NPC_type)
		|| !Q_stricmp("md_revanr_nomask", self->NPC_type)
		|| !Q_stricmp("md_revanr_nomask_nh", self->NPC_type))
	{
		//dual only
		return qtrue;
	}

	return qfalse;
}



static qboolean npc_can_do_slap(void)
{
	if (NPCS.NPC->health <= 1
		|| PM_SaberInStart(NPCS.NPC->client->ps.saberMove)
		|| PM_SaberInTransition(NPCS.NPC->client->ps.saberMove)
		|| BG_InKnockDown(NPCS.NPC->client->ps.legsAnim)
		|| BG_InKnockDown(NPCS.NPC->client->ps.torsoAnim)
		|| PM_InRoll(&NPCS.NPC->client->ps)
		|| PM_SuperBreakLoseAnim(NPCS.NPC->client->ps.torsoAnim)
		|| PM_SuperBreakWinAnim(NPCS.NPC->client->ps.torsoAnim)
		|| PM_SaberInSpecialAttack(NPCS.NPC->client->ps.torsoAnim)
		|| PM_InSpecialJump(NPCS.NPC->client->ps.torsoAnim)
		|| PM_SaberInBounce(NPCS.NPC->client->ps.saberMove)
		|| PM_SaberInKnockaway(NPCS.NPC->client->ps.saberMove)
		|| PM_SaberInBrokenParry(NPCS.NPC->client->ps.saberMove)
		|| jedi_forbidden_kicker(NPCS.NPC)
		|| NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE
		|| NPCS.NPC->client->NPC_class == CLASS_YODA
		|| (NPCS.NPC->client->ps.fd.blockPoints < BLOCKPOINTS_FIVE)
		|| NPCS.NPC->client->ps.fd.forcePower < BLOCKPOINTS_FIVE)
	{
		return qfalse;
	}

	if (NPCS.NPC->client->NPC_class == CLASS_SITHLORD
		|| NPCS.NPC->client->NPC_class == CLASS_DESANN
		|| NPCS.NPC->client->NPC_class == CLASS_VADER
		|| NPCS.NPC->client->NPC_class == CLASS_JEDI
		|| NPCS.NPC->client->NPC_class == CLASS_KYLE
		|| NPCS.NPC->client->NPC_class == CLASS_LUKE
		|| NPCS.NPC->client->NPC_class == CLASS_MORGANKATARN
		|| NPCS.NPC->client->NPC_class == CLASS_REBORN
		|| NPCS.NPC->client->NPC_class == CLASS_SHADOWTROOPER
		|| NPCS.NPC->client->NPC_class == CLASS_TAVION
		|| NPCS.NPC->client->NPC_class == CLASS_ALORA)
	{
		return qtrue;
	}

	return qfalse;
}


void npc_cultist_destroyer_precache(void)
{
	G_SoundIndex("sound/movers/objects/green_beam_lp2.wav");
	G_EffectIndex("force/destruction_exp");
}



void npc_shadow_trooper_precache(void)
{
	RegisterItem(BG_FindItemForAmmo(AMMO_FORCE));
	G_SoundIndex("sound/chars/shadowtrooper/cloak.wav");
	G_SoundIndex("sound/chars/shadowtrooper/decloak.wav");
}



void npc_rosh_dark_precache(void)
{
	G_EffectIndex("force/kothos_recharge.efx");
	G_EffectIndex("force/kothos_beam.efx");
}



static void npc_sbd_precache(void)
{
	G_SoundIndex("sound/chars/SBD/SBDWalk2.mp3");
	G_SoundIndex("sound/chars/SBD/SBDRun.mp3");
	G_SoundIndex("sound/chars/SBD/SBDStep.mp3");
	G_SoundIndex("sound/chars/SBD/SBDStep1.mp3");
	G_SoundIndex("sound/chars/SBD/SBDStep2.mp3");
	G_SoundIndex("sound/chars/SBD/SBDStep3.wav");
}



void Jedi_ClearTimers(const gentity_t* ent)
{
	TIMER_Set(ent, "roamTime", 0);
	TIMER_Set(ent, "chatter", 0);
	TIMER_Set(ent, "strafeLeft", 0);
	TIMER_Set(ent, "strafeRight", 0);
	TIMER_Set(ent, "noStrafe", 0);
	TIMER_Set(ent, "walking", 0);
	TIMER_Set(ent, "taunting", 0);
	TIMER_Set(ent, "parryTime", 0);
	TIMER_Set(ent, "parryReCalcTime", 0);
	TIMER_Set(ent, "forceJumpChasing", 0);
	TIMER_Set(ent, "jumpChaseDebounce", 0);
	TIMER_Set(ent, "moveforward", 0);
	TIMER_Set(ent, "moveback", 0);
	TIMER_Set(ent, "movenone", 0);
	TIMER_Set(ent, "moveright", 0);
	TIMER_Set(ent, "moveleft", 0);
	TIMER_Set(ent, "movecenter", 0);
	TIMER_Set(ent, "saberLevelDebounce", 0);
	TIMER_Set(ent, "noRetreat", 0);
	TIMER_Set(ent, "holdLightning", 0);
	TIMER_Set(ent, "gripping", 0);
	TIMER_Set(ent, "draining", 0);
	TIMER_Set(ent, "noturn", 0);
	TIMER_Set(ent, "specialEvasion", 0);
	TIMER_Set(ent, "smackTime", 0);
	TIMER_Set(ent, "TalkTime", 0);
	TIMER_Set(ent, "blocking", 0);
	TIMER_Set(ent, "regenerate", 0);
	TIMER_Set(ent, "TalkTiming", 0);
	TIMER_Set(ent, "noKata", 0);
	TIMER_Set(ent, "kyleTakesSaber", 0);
	TIMER_Set(ent, "grasping", 0);
	TIMER_Set(ent, "DeactivateTime", 0);
}



qboolean Jedi_CultistDestroyer(const gentity_t* self)
{
	if (!self || !self->client)
	{
		return qfalse;
	}
	if (self->client->NPC_class == CLASS_REBORN
		&& self->s.weapon == WP_MELEE
		&& Q_stricmp("cultist_destroyer", self->NPC_type) == 0)
	{
		return qtrue;
	}
	return qfalse;
}



static qboolean Jedi_CultistForceUser(const gentity_t* self)
{
	if (!self || !self->client)
	{
		return qfalse;
	}
	if (self->client->NPC_class == CLASS_REBORN
		&& Q_stricmp("cultist_destroyer", self->NPC_type) == 0
		|| Q_stricmp("cultist_grip", self->NPC_type) == 0
		|| Q_stricmp("cultist_lightning", self->NPC_type) == 0
		|| Q_stricmp("cultist_drain", self->NPC_type) == 0)
	{
		return qtrue;
	}
	return qfalse;
}



static qboolean jedi_forbidden_kicker(const gentity_t* self)
{
	if (!self || !self->client)
	{
		return qfalse;
	}

	if (Q_stricmp("md_gorc", self->NPC_type) == 0
		|| Q_stricmp("md_maw", self->NPC_type) == 0)
	{
		return qtrue;
	}
	return qfalse;
}



void Jedi_PlayBlockedPushSound(const gentity_t* self)
{
	if (self->s.number >= 0 && self->s.number < MAX_CLIENTS)
	{
		if (!npc_is_dark_jedi(self))
		{
			G_AddVoiceEvent(self, Q_irand(EV_OUTFLANK1, EV_OUTFLANK2), 2000);
		}
		else
		{
			G_AddVoiceEvent(self, EV_PUSHFAIL, 3000);
		}
	}
	else if (self->health > 0 && self->NPC && self->NPC->blockedSpeechDebounceTime < level.time)
	{
		if (!npc_is_dark_jedi(self))
		{
			G_AddVoiceEvent(self, Q_irand(EV_OUTFLANK1, EV_OUTFLANK2), 2000);
		}
		else
		{
			G_AddVoiceEvent(self, EV_PUSHFAIL, 3000);
		}
		self->NPC->blockedSpeechDebounceTime = level.time + 3000;
	}
}



void Jedi_PlayDeflectSound(const gentity_t* self)
{
	if (self->s.number >= 0 && self->s.number < MAX_CLIENTS)
	{
		if (!npc_is_dark_jedi(self))
		{
			G_AddVoiceEvent(self, Q_irand(EV_CHASE1, EV_CHASE3), 2000);
		}
		else
		{
			G_AddVoiceEvent(self, Q_irand(EV_DEFLECT1, EV_DEFLECT3), 3000);
		}
	}
	else if (self->health > 0 && self->NPC && self->NPC->blockedSpeechDebounceTime < level.time)
	{
		if (!npc_is_dark_jedi(self))
		{
			G_AddVoiceEvent(self, Q_irand(EV_CHASE1, EV_CHASE3), 2000);
		}
		else
		{
			G_AddVoiceEvent(self, Q_irand(EV_DEFLECT1, EV_DEFLECT3), 3000);
		}
		self->NPC->blockedSpeechDebounceTime = level.time + 3000;
	}
}



void NPC_Jedi_PlayConfusionSound(const gentity_t* self)
{
	if (self->health > 0)
	{
		if (self->client
			&& (self->client->NPC_class == CLASS_ALORA
				|| self->client->NPC_class == CLASS_TAVION
				|| self->client->NPC_class == CLASS_DESANN
				|| self->client->NPC_class == CLASS_SITHLORD
				|| self->client->NPC_class == CLASS_VADER))
		{
			G_AddVoiceEvent(self, Q_irand(EV_CONFUSE1, EV_CONFUSE3), 2000);
		}
		else if (Q_irand(0, 1))
		{
			if (!npc_is_dark_jedi(self))
			{
				G_AddVoiceEvent(self, Q_irand(EV_ESCAPING1, EV_ESCAPING3), 2000);
			}
			else
			{
				G_AddVoiceEvent(self, Q_irand(EV_TAUNT1, EV_TAUNT3), 2000);
			}
		}
		else
		{
			if (!npc_is_dark_jedi(self))
			{
				G_AddVoiceEvent(self, Q_irand(EV_CONFUSE1, EV_CONFUSE3), 2000);
			}
			else
			{
				G_AddVoiceEvent(self, Q_irand(EV_GLOAT1, EV_GLOAT3), 2000);
			}
		}
	}
}



qboolean Jedi_StopKnockdown(gentity_t* self, const vec3_t push_dir)
{
	if (self->s.number < MAX_CLIENTS || !self->NPC)
	{
		//only NPCs
		return qfalse;
	}

	if (self->client->ps.fd.forcePowerLevel[FP_LEVITATION] < FORCE_LEVEL_1)
	{
		//only force-users
		return qfalse;
	}

	if (self->client->moveType == MT_FLYSWIM)
	{
		//can't knock me down when I'm flying
		return qtrue;
	}

	if (self->NPC && self->NPC->aiFlags & NPCAI_BOSS_CHARACTER)
	{
		//bosses always get out of a knockdown
	}
	else if (Q_irand(0, RANK_CAPTAIN + 5) > self->NPC->rank)
	{
		//lower their rank, the more likely they are fall down
		return qfalse;
	}

	vec3_t p_dir, fwd, right;
	const vec3_t ang = { 0, self->r.currentAngles[YAW], 0 };
	const int strafe_time = Q_irand(1000, 2000);

	AngleVectors(ang, fwd, right, NULL);
	VectorNormalize2(push_dir, p_dir);
	const float f_dot = DotProduct(p_dir, fwd);
	const float r_dot = DotProduct(p_dir, right);

	//flip or roll with it
	usercmd_t temp_cmd = { 0 };
	if (f_dot >= 0.4f)
	{
		temp_cmd.forwardmove = 127;
		TIMER_Set(self, "moveforward", strafe_time);
	}
	else if (f_dot <= -0.4f)
	{
		temp_cmd.forwardmove = -127;
		TIMER_Set(self, "moveback", strafe_time);
	}
	else if (r_dot > 0)
	{
		temp_cmd.rightmove = 127;
		TIMER_Set(self, "strafeRight", strafe_time);
		TIMER_Set(self, "strafeLeft", -1);
	}
	else
	{
		temp_cmd.rightmove = -127;
		TIMER_Set(self, "strafeLeft", strafe_time);
		TIMER_Set(self, "strafeRight", -1);
	}
	G_AddEvent(self, EV_JUMP, 0);

	if (!Q_irand(0, 1))
	{
		//flip
		self->client->ps.fd.forceJumpCharge = 280;
		ForceJump(self, &temp_cmd);
	}
	else
	{
		//roll
		TIMER_Set(self, "duck", strafe_time);
	}
	self->painDebounceTime = 0; //so we do something

	return qtrue;
}



//===============================================================================================
//TAVION BOSS
//===============================================================================================
void NPC_TavionScepter_Precache(void)
{
	//precashe tavion boss scepter effects
	G_EffectIndex("scepter/beam_warmup.efx");
	G_EffectIndex("scepter/beam.efx");
	G_EffectIndex("scepter/slam_warmup.efx");
	G_EffectIndex("scepter/slam.efx");
	G_EffectIndex("scepter/impact.efx");
	G_SoundIndex("sound/weapons/scepter/loop.wav");
	G_SoundIndex("sound/weapons/scepter/slam_warmup.wav");
	G_SoundIndex("sound/weapons/scepter/beam_warmup.wav");
}



void NPC_TavionSithSword_Precache(void)
{
	//precashe tavion boss sithsword effects
	G_EffectIndex("scepter/recharge.efx");
	G_EffectIndex("scepter/invincibility.efx");
	G_EffectIndex("scepter/sword.efx");
	G_SoundIndex("sound/weapons/scepter/recharge.wav");
}


static void Tavion_ScepterDamage(void)
{
	//Damage code for tavion's scepter weapon.
	if (!NPCS.NPC->ghoul2 || !NPCS.NPC->client->weaponGhoul2[1])
	{
		//sanity checks
		return;
	}

	if (NPCS.NPC->NPC->genericBolt1 != -1)
	{
		const int curTime = level.time;
		qboolean hit = qfalse;
		int lastHit = ENTITYNUM_NONE;

		for (int time = curTime - 25; time <= curTime + 25 && !hit; time += 25)
		{
			mdxaBone_t boltMatrix;
			vec3_t tip, dir, base, angles;
			trace_t trace;

			VectorSet(angles, 0, NPCS.NPC->r.currentAngles[YAW], 0);

			trap->G2API_GetBoltMatrix(NPCS.NPC->client->weaponGhoul2[1], 0,
				NPCS.NPC->NPC->genericBolt1,
				&boltMatrix, angles, NPCS.NPC->r.currentOrigin, time,
				NULL, NPCS.NPC->modelScale);
			BG_GiveMeVectorFromMatrix(&boltMatrix, ORIGIN, base);
			BG_GiveMeVectorFromMatrix(&boltMatrix, NEGATIVE_X, dir);
			VectorMA(base, 512, dir, tip);
			trap->Trace(&trace, base, vec3_origin, vec3_origin, tip, NPCS.NPC->s.number, MASK_SHOT, qfalse, 0, 0);
			if (trace.fraction < 1.0f)
			{
				//hit something
				gentity_t* traceEnt = &g_entities[trace.entityNum];

				//FIXME: too expensive!
				//if ( time == curTime )
				{
					//UGH
					G_PlayEffect(G_EffectIndex("scepter/impact.efx"), trace.endpos, trace.plane.normal);
				}

				if (traceEnt->takedamage
					&& trace.entityNum != lastHit
					&& (!traceEnt->client || traceEnt == NPCS.NPC->enemy || traceEnt->client->NPC_class != NPCS.NPC->
						client->NPC_class))
				{
					//smack
					const int dmg = Q_irand(10, 20) * (g_npcspskill.integer + 1); //NOTE: was 6-12
					G_Damage(traceEnt, NPCS.NPC, NPCS.NPC, vec3_origin, trace.endpos, dmg, DAMAGE_NO_KNOCKBACK,
						MOD_SABER); //MOD_MELEE );
					if (traceEnt->client)
					{
						if (!Q_irand(0, 2))
						{
							G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_CONFUSE1, EV_CONFUSE2), 10000);
						}
						else
						{
							G_AddVoiceEvent(NPCS.NPC, EV_JDETECTED3, 10000);
						}
						G_Throw(traceEnt, dir, Q_flrand(50, 80));
						if (traceEnt->health > 0 && !Q_irand(0, 2)) //FIXME: base on skill!
						{
							//do pain on enemy
							G_Knockdown(traceEnt, NPCS.NPC, dir, 300, qtrue);
						}
					}
					hit = qtrue;
					lastHit = trace.entityNum;
				}
			}
		}
	}
}



// ============================================================================
// Tavion_ScepterSlam
//
// Performs Tavion’s scepter slam attack, applying area damage, knockback,
// breakable destruction, and visual effects. Modernized to remove large stack
// allocations (fixing MSVC C6262), add safety checks, explicit qboolean usage,
// and debug prints instead of asserts.
// ============================================================================
static void Tavion_ScepterSlam(void)
{
	// ----------------------------------------------------------------------
	// Safety: validate NPC and weapon ghoul2
	// ----------------------------------------------------------------------
	if (NPCS.NPC == NULL)
	{
		Com_Printf("Tavion_ScepterSlam ERROR: NPCS.NPC == NULL\n");
		return;
	}

	if (NPCS.NPC->ghoul2 == NULL || NPCS.NPC->client == NULL)
	{
		Com_Printf("Tavion_ScepterSlam ERROR: NPC ghoul2 or client missing\n");
		return;
	}

	if (NPCS.NPC->client->weaponGhoul2[1] == NULL)
	{
		// No weapon model → cannot slam
		return;
	}

	// ----------------------------------------------------------------------
	// Add bolt
	// ----------------------------------------------------------------------
	const int boltIndex =
		trap->G2API_AddBolt(NPCS.NPC->client->weaponGhoul2[1], 0, "*weapon");

	if (boltIndex == -1)
	{
		return;
	}

	// ----------------------------------------------------------------------
	// Move large objects off the stack (fixes MSVC C6262)
	// ----------------------------------------------------------------------
	static mdxaBone_t boltMatrix;                // ~16 KB
	static int radius_ents[MAX_GENTITIES];       // 4096 bytes

	vec3_t handle = { 0 };
	vec3_t bottom = { 0 };
	vec3_t angles;
	trace_t trace;

	const float radius = 300.0f;
	const float halfRad = radius * 0.5f;

	vec3_t mins = { 0 };
	vec3_t maxs = { 0 };
	vec3_t entDir;

	// ----------------------------------------------------------------------
	// Compute bolt matrix
	// ----------------------------------------------------------------------
	VectorSet(angles, 0.0f, NPCS.NPC->r.currentAngles[YAW], 0.0f);

	trap->G2API_GetBoltMatrix(
		NPCS.NPC->ghoul2,
		2,
		boltIndex,
		&boltMatrix,
		angles,
		NPCS.NPC->r.currentOrigin,
		level.time,
		NULL,
		NPCS.NPC->modelScale);

	BG_GiveMeVectorFromMatrix(&boltMatrix, ORIGIN, handle);

	VectorCopy(handle, bottom);
	bottom[2] -= 128.0f;

	// ----------------------------------------------------------------------
	// Trace downward to find ground impact point
	// ----------------------------------------------------------------------
	trap->Trace(
		&trace,
		handle,
		vec3_origin,
		vec3_origin,
		bottom,
		NPCS.NPC->s.number,
		MASK_SHOT,
		qfalse,
		0,
		0);

	G_PlayEffect(G_EffectIndex("scepter/slam.efx"), trace.endpos, trace.plane.normal);

	// ----------------------------------------------------------------------
	// Build bounding box for radius search
	// ----------------------------------------------------------------------
	for (int i = 0; i < 3; i++)
	{
		mins[i] = trace.endpos[i] - radius;
		maxs[i] = trace.endpos[i] + radius;
	}

	// ----------------------------------------------------------------------
	// Find entities in radius
	// ----------------------------------------------------------------------
	const int num_ents = trap->EntitiesInBox(mins, maxs, radius_ents, 128);

	for (int i = 0; i < num_ents; i++)
	{
		const int entNum = radius_ents[i];

		if (entNum < 0 || entNum >= MAX_GENTITIES)
		{
			continue;
		}

		gentity_t* radius_ent = &g_entities[entNum];

		if (radius_ent->inuse == qfalse)
		{
			continue;
		}

		if (radius_ent == NPCS.NPC)
		{
			continue; // skip self
		}

		// ------------------------------------------------------------------
		// Breakables
		// ------------------------------------------------------------------
		if (radius_ent->client == NULL)
		{
			if (G_EntIsBreakable(radius_ent->s.number) == qtrue)
			{
				G_Damage(radius_ent, NPCS.NPC, NPCS.NPC,
					vec3_origin,
					radius_ent->r.currentOrigin,
					100,
					0,
					MOD_ROCKET_SPLASH);
			}
			continue;
		}

		// ------------------------------------------------------------------
		// Held entities cannot be thrown
		// ------------------------------------------------------------------
		if (radius_ent->client->ps.eFlags2 & EF2_HELD_BY_MONSTER)
		{
			continue;
		}

		// ------------------------------------------------------------------
		// Compute distance from slam point
		// ------------------------------------------------------------------
		VectorSubtract(radius_ent->r.currentOrigin, trace.endpos, entDir);
		const float dist = VectorNormalize(entDir);

		if (dist > radius)
		{
			continue;
		}

		// ------------------------------------------------------------------
		// Damage if close enough
		// ------------------------------------------------------------------
		if (dist < halfRad)
		{
			G_Damage(radius_ent, NPCS.NPC, NPCS.NPC,
				vec3_origin,
				radius_ent->r.currentOrigin,
				Q_irand(20, 30),
				DAMAGE_NO_KNOCKBACK,
				MOD_ROCKET_SPLASH);
		}

		// ------------------------------------------------------------------
		// Throw non‑heavy NPCs
		// ------------------------------------------------------------------
		if (radius_ent->client->NPC_class != CLASS_RANCOR &&
			radius_ent->client->NPC_class != CLASS_ATST)
		{
			float throwStr = 0.0f;

			if (g_npcspskill.integer > 1)
			{
				throwStr = 10.0f + (radius - dist) * 0.5f;
				if (throwStr > 150.0f)
				{
					throwStr = 150.0f;
				}
			}
			else
			{
				throwStr = 10.0f + (radius - dist) * 0.25f;
				if (throwStr > 85.0f)
				{
					throwStr = 85.0f;
				}
			}

			entDir[2] += 0.1f;
			VectorNormalize(entDir);

			G_Throw(radius_ent, entDir, throwStr);

			if (radius_ent->health > 0)
			{
				if (dist < halfRad ||
					radius_ent->client->ps.groundEntityNum != ENTITYNUM_NONE)
				{
					G_Knockdown(radius_ent, NPCS.NPC, vec3_origin, 500, qtrue);
				}
			}
		}
	}
}



static void Tavion_StartScepterBeam(void)
{
	//Activate the scepter beam for the current NPC.
	//RAFIXME:  This probably needs to be moved to client side.
	mdxaBone_t boltMatrix;
	vec3_t dir, base, angles;

	VectorSet(angles, 0, NPCS.NPC->r.currentAngles[YAW], 0);

	trap->G2API_GetBoltMatrix(NPCS.NPC->client->weaponGhoul2[1], 0, NPCS.NPC->NPC->genericBolt1, &boltMatrix, angles,
		NPCS.NPC->r.currentOrigin, level.time, NULL, NPCS.NPC->modelScale);
	BG_GiveMeVectorFromMatrix(&boltMatrix, ORIGIN, base);
	BG_GiveMeVectorFromMatrix(&boltMatrix, NEGATIVE_X, dir);

	G_PlayEffect(G_EffectIndex("scepter/beam_warmup.efx"), base, dir);
	G_SoundOnEnt(NPCS.NPC, CHAN_ITEM, "sound/weapons/scepter/beam_warmup.wav");
	NPCS.NPC->client->ps.legsTimer = NPCS.NPC->client->ps.torsoTimer = 0;
	NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_SCEPTER_START, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	NPCS.NPC->client->ps.torsoTimer += 200;
	NPCS.NPC->painDebounceTime = level.time + NPCS.NPC->client->ps.torsoTimer;
	NPCS.NPC->client->ps.pm_time = NPCS.NPC->client->ps.torsoTimer;
	NPCS.NPC->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
	VectorClear(NPCS.NPC->client->ps.velocity);
	VectorClear(NPCS.NPC->client->ps.moveDir);
}



static void Tavion_StartScepterSlam(void)
{
	mdxaBone_t boltMatrix;
	vec3_t dir, base, angles;

	VectorSet(angles, 0, NPCS.NPC->r.currentAngles[YAW], 0);

	trap->G2API_GetBoltMatrix(NPCS.NPC->client->weaponGhoul2[1], 0,
		NPCS.NPC->NPC->genericBolt1,
		&boltMatrix, angles, NPCS.NPC->r.currentOrigin, level.time,
		NULL, NPCS.NPC->modelScale);
	BG_GiveMeVectorFromMatrix(&boltMatrix, ORIGIN, base);
	BG_GiveMeVectorFromMatrix(&boltMatrix, NEGATIVE_X, dir);
	G_PlayEffect(G_EffectIndex("scepter/slam_warmup.efx"), base, dir);
	G_SoundOnEnt(NPCS.NPC, CHAN_ITEM, "sound/weapons/scepter/slam_warmup.wav");
	NPCS.NPC->client->ps.legsTimer = NPCS.NPC->client->ps.torsoTimer = 0;
	NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_TAVION_SCEPTERGROUND, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	NPCS.NPC->painDebounceTime = level.time + NPCS.NPC->client->ps.torsoTimer;
	NPCS.NPC->client->ps.pm_time = NPCS.NPC->client->ps.torsoTimer;
	NPCS.NPC->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
	VectorClear(NPCS.NPC->client->ps.velocity);
	VectorClear(NPCS.NPC->client->ps.moveDir);
	NPCS.NPC->count = 0;
}



static void Tavion_SithSwordRecharge(void)
{
	if (NPCS.NPC->client->ps.torsoAnim != BOTH_TAVION_SWORDPOWER
		&& NPCS.NPC->count
		&& TIMER_Done(NPCS.NPC, "rechargeDebounce")
		&& NPCS.NPC->client->weaponGhoul2[0])
	{
		mdxaBone_t boltMatrix;
		vec3_t dir, base, angles;

		VectorSet(angles, 0, NPCS.NPC->r.currentAngles[YAW], 0);

		NPCS.NPC->s.loopSound = G_SoundIndex("sound/weapons/scepter/recharge.wav");
		const int boltIndex = trap->G2API_AddBolt(NPCS.NPC->client->weaponGhoul2[0], 0, "*weapon");
		NPCS.NPC->client->ps.legsTimer = NPCS.NPC->client->ps.torsoTimer = 0;
		NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_TAVION_SWORDPOWER, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

		//RAFIXME:  This probably needs to be moved to client side.
		trap->G2API_GetBoltMatrix(NPCS.NPC->client->weaponGhoul2[0], 0,
			boltIndex,
			&boltMatrix, angles, NPCS.NPC->r.currentOrigin, level.time,
			NULL, NPCS.NPC->modelScale);
		BG_GiveMeVectorFromMatrix(&boltMatrix, ORIGIN, base);
		BG_GiveMeVectorFromMatrix(&boltMatrix, NEGATIVE_X, dir);
		G_PlayEffect(G_EffectIndex("scepter/recharge.efx"), base, dir);
		NPCS.NPC->painDebounceTime = level.time + NPCS.NPC->client->ps.torsoTimer;
		NPCS.NPC->client->ps.pm_time = NPCS.NPC->client->ps.torsoTimer;
		NPCS.NPC->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
		VectorClear(NPCS.NPC->client->ps.velocity);
		VectorClear(NPCS.NPC->client->ps.moveDir);
		NPCS.NPC->client->ps.powerups[PW_INVINCIBLE] = level.time + NPCS.NPC->client->ps.torsoTimer + 10000;
		G_PlayEffect(G_EffectIndex("scepter/invincibility.efx"), NPCS.NPC->r.currentOrigin, NPCS.NPC->r.currentAngles);
		TIMER_Set(NPCS.NPC, "rechargeDebounce", NPCS.NPC->client->ps.torsoTimer + 10000 + Q_irand(10000, 20000));
		NPCS.NPC->count--;
		//now you have a chance of killing her
		NPCS.NPC->flags &= ~FL_UNDYING;
	}
}



//======================================================================================
//END TAVION BOSS
//======================================================================================

static void player_Decloak(gentity_t* self)
{
	if (self && self->client)
	{
		self->flags &= ~FL_NOTARGET;
		if (self->client->ps.powerups[PW_CLOAKED])
		{
			//Uncloak
			self->client->ps.powerups[PW_CLOAKED] = 0;
			G_SoundOnEnt(self, CHAN_ITEM, "sound/chars/shadowtrooper/decloak.wav");
			NPC_SetAnim(self, SETANIM_TORSO, BOTH_FORCE_DRAIN_RELEASE, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		}
	}
}



static qboolean self_is_gunner(const gentity_t* self)
{
	switch (self->client->ps.weapon)
	{
	case WP_NOGHRI_STICK:
	case WP_TUSKEN_RIFLE:
	case WP_BLASTER:
	case WP_DISRUPTOR:
	case WP_BOWCASTER:
	case WP_REPEATER:
	case WP_Z6_ROTARY_CANNON:
	case WP_DEMP2:
	case WP_FLECHETTE:
	case WP_ROCKET_LAUNCHER:
	case WP_THERMAL:
	case WP_TRIP_MINE:
	case WP_DET_PACK:
	case WP_CONCUSSION:
	case WP_STUN_BATON:
	case WP_BRYAR_PISTOL:
		return qtrue;
	default:;
	}
	return qfalse;
}



void player_Cloak(gentity_t* self)
{
	if (self)
	{
		self->flags |= FL_NOTARGET;
		if (self->client)
		{
			if (!self->client->ps.powerups[PW_CLOAKED])
			{
				//cloak
				self->client->ps.powerups[PW_CLOAKED] = Q3_INFINITE;
				G_Sound(self, CHAN_ITEM, G_SoundIndex("sound/chars/shadowtrooper/cloak.wav"));
				self->client->ps.cloakFuel -= 15;
				NPC_SetAnim(self, SETANIM_TORSO, BOTH_FORCE_PROTECT_FAST, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			}
		}
	}
}



void Jedi_Cloak(gentity_t* self)
{
	if (self)
	{
		self->flags |= FL_NOTARGET;
		if (self->client)
		{
			if (!self->client->ps.powerups[PW_CLOAKED])
			{
				//cloak
				self->client->ps.powerups[PW_CLOAKED] = Q3_INFINITE;
				G_Sound(self, CHAN_ITEM, G_SoundIndex("sound/chars/shadowtrooper/cloak.wav"));
				self->client->ps.cloakFuel -= 15;

				if (self->client->ps.saberHolstered > 1)
				{
					NPC_SetAnim(self, SETANIM_TORSO, BOTH_FORCE_PROTECT_FAST,
						SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				}
			}
		}
	}
}



void Jedi_Decloak(gentity_t* self)
{
	if (self)
	{
		self->flags &= ~FL_NOTARGET;
		if (self->client)
		{
			if (self->client->ps.powerups[PW_CLOAKED])
			{
				//Uncloak
				self->client->ps.powerups[PW_CLOAKED] = 0;

				G_Sound(self, CHAN_ITEM, G_SoundIndex("sound/chars/shadowtrooper/decloak.wav"));

				if (!(self->r.svFlags & SVF_BOT))
				{
					NPC_SetAnim(self, SETANIM_TORSO, BOTH_FORCE_DRAIN_RELEASE,
						SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				}
			}
		}
	}
}



static void Jedi_CheckCloak(void)
{
	if (NPCS.NPC
		&& NPCS.NPC->client
		&& NPCS.NPC->client->NPC_class == CLASS_SHADOWTROOPER
		&& Q_stricmpn("shadowtrooper", NPCS.NPC->NPC_type, 13) == 0)
	{
		if ((!NPCS.NPC->client->ps.saberHolstered) ||
			NPCS.NPC->health <= 0 ||
			NPCS.NPC->client->ps.saberInFlight ||
			(NPCS.NPC->client->ps.fd.forceGripBeingGripped > level.time) ||
			jedi_being_grasped(NPCS.NPC) ||
			jedi_being_drained(NPCS.NPC) ||
			NPCS.NPC->painDebounceTime > level.time)
		{
			//can't be cloaked if saber is on, or dead or saber in flight or taking pain or being gripped
			Jedi_Decloak(NPCS.NPC);
		}
		else if (NPCS.NPC->health > 0
			&& !NPCS.NPC->client->ps.saberInFlight
			&& !((NPCS.NPC->client->ps.fd.forceGripBeingGripped > level.time))
			&& !(jedi_being_drained(NPCS.NPC))
			&& !(jedi_being_grasped(NPCS.NPC))
			&& NPCS.NPC->painDebounceTime < level.time)
		{
			//still alive, have saber in hand, not taking pain and not being gripped
			if (!PM_SaberInAttack(NPCS.NPC->client->ps.saberMove))
			{
				Jedi_Cloak(NPCS.NPC);
			}
		}
	}
}



static void Self_CheckCloak(gentity_t* self)
{
	if (self && self->client)
	{
		if (self->health <= 0 ||
			self->client->ps.saberInFlight ||
			(self->client->ps.fd.forceGripBeingGripped > level.time) ||
			jedi_being_drained(self) ||
			jedi_being_grasped(self) ||
			self->painDebounceTime > level.time)
		{
			//can't be cloaked if saber is on, or dead or saber in flight or taking pain or being gripped
			player_Decloak(self);
		}
		else if (self->health > 0
			&& !self->client->ps.saberInFlight
			&& !((self->client->ps.fd.forceGripBeingGripped > level.time))
			&& !(jedi_being_drained(self))
			&& !(jedi_being_grasped(self))
			&& self->painDebounceTime < level.time)
		{
			//still alive, have saber in hand, not taking pain and not being gripped
			player_Cloak(self);
		}
	}
}



/*
==========================================================================================
AGGRESSION
==========================================================================================
*/
static void Jedi_Aggression(const gentity_t* self, const int change)
{
	int upper_threshold = 0, lower_threshold = 0;

	self->NPC->stats.aggression += change;

	if (self->client->playerTeam == NPCTEAM_PLAYER)
	{
		//good guys are less aggressive
		upper_threshold = 10;
		lower_threshold = 5;
	}
	else
	{
		//bad guys are more aggressive
		if (npc_is_sith_lord(self))
		{
			upper_threshold = 20;
			lower_threshold = 10;
		}
		else
		{
			upper_threshold = 15;
			lower_threshold = 5;
		}
	}

	if (self->NPC->stats.aggression > upper_threshold)
	{
		self->NPC->stats.aggression = upper_threshold;
	}
	else if (self->NPC->stats.aggression < lower_threshold)
	{
		self->NPC->stats.aggression = lower_threshold;
	}
}



static void Jedi_AggressionErosion(const int amt)
{
	if (TIMER_Done(NPCS.NPC, "roamTime"))
	{
		//the longer we're not alerted and have no enemy, the more our aggression goes down
		TIMER_Set(NPCS.NPC, "roamTime", Q_irand(2000, 5000));
		Jedi_Aggression(NPCS.NPC, amt);
	}

	if (TIMER_Done(NPCS.NPC, "DeactivateTime") && NPCS.NPCInfo->stats.aggression < 10 && (!NPCS.NPC->client->ps.saberHolstered))
	{
		if (!NPC_IsAlive(NPCS.NPC, NPCS.NPC->enemy))
		{//only if our enemy is dead, otherwise we might need it again soon,turn off the saber
			WP_DeactivateSaber(NPCS.NPC);
			NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, BOTH_STAND2TO1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_VICTORY1, EV_VICTORY3), 3000);
		}
	}

	if (NPCS.NPCInfo->stats.aggression < 4
		|| NPCS.NPCInfo->stats.aggression < 6 && NPCS.NPC->client->NPC_class == CLASS_DESANN
		|| NPCS.NPCInfo->stats.aggression < 6 && NPCS.NPC->client->NPC_class == CLASS_VADER)
	{
		//turn off the saber
		WP_DeactivateSaber(NPCS.NPC);
		NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, BOTH_STAND2TO1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_VICTORY1, EV_VICTORY3), 3000);
	}
}



void NPC_Jedi_RateNewEnemy(const gentity_t* self, const gentity_t* enemy)
{
	float health_aggression = 0.0f;
	float weapon_aggression = 0.0f;

	switch (enemy->s.weapon)
	{
	case WP_SABER:
		health_aggression = (float)(self->health) / 200.0f * 6.0f;
		weapon_aggression = 7; //go after him
		break;
	case WP_BLASTER:
		if (DistanceSquared(self->r.currentOrigin, enemy->r.currentOrigin) < 65536) //256 squared
		{
			health_aggression = (float)(self->health) / 200.0f * 8.0f;
			weapon_aggression = 8; //go after him
		}
		else
		{
			health_aggression = 8.0f - (float)(self->health) / 200.0f * 8.0f;
			weapon_aggression = 2; //hang back for a second
		}
		break;
	default:
		health_aggression = (float)(self->health) / 200.0f * 8.0f;
		weapon_aggression = 6; //approach
		break;
	}
	//Average these with current aggression
	const int new_aggression = ceil(
		(health_aggression + weapon_aggression + (float)(self->NPC->stats.aggression)) / 3.0f);
	Jedi_Aggression(self, new_aggression - self->NPC->stats.aggression);

	//don't taunt right away
	TIMER_Set(self, "chatter", Q_irand(4000, 7000));
}



static void Jedi_Rage(void)
{
	Jedi_Aggression(NPCS.NPC, 10 - NPCS.NPCInfo->stats.aggression + Q_irand(-2, 2));
	TIMER_Set(NPCS.NPC, "roamTime", 0);
	TIMER_Set(NPCS.NPC, "chatter", 0);
	TIMER_Set(NPCS.NPC, "walking", 0);
	TIMER_Set(NPCS.NPC, "taunting", 0);
	TIMER_Set(NPCS.NPC, "jumpChaseDebounce", 0);
	TIMER_Set(NPCS.NPC, "movenone", 0);
	TIMER_Set(NPCS.NPC, "movecenter", 0);
	TIMER_Set(NPCS.NPC, "noturn", 0);
	TIMER_Set(NPCS.NPC, "TalkTime", 0);
	TIMER_Set(NPCS.NPC, "TalkTiming", 0);
	ForceRage(NPCS.NPC);
}



static void Jedi_RageStop(const gentity_t* self)
{
	if (self->NPC)
	{
		//calm down and back off
		TIMER_Set(self, "roamTime", 0);
		Jedi_Aggression(self, Q_irand(-5, 0));
	}
}



/*
==========================================================================================
SPEAKING
==========================================================================================
*/

static qboolean Jedi_BattleTaunt(void)
{
	if (TIMER_Done(NPCS.NPC, "chatter")
		&& !Q_irand(0, 3)
		&& NPCS.NPCInfo->blockedSpeechDebounceTime < level.time
		&& jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] < level.time)
	{
		int event = -1;
		if (NPCS.NPC->enemy
			&& NPCS.NPC->enemy->client
			&& (NPCS.NPC->enemy->client->NPC_class == CLASS_RANCOR
				|| NPCS.NPC->enemy->client->NPC_class == CLASS_WAMPA
				|| NPCS.NPC->enemy->client->NPC_class == CLASS_SAND_CREATURE))
		{
			//never taunt these mindless creatures
			//NOTE: howlers?  tusken?  etc?  Only reborn?
		}
		else
		{
			if (NPCS.NPC->client->playerTeam == NPCTEAM_PLAYER
				&& NPCS.NPC->enemy && NPCS.NPC->enemy->client && NPCS.NPC->enemy->client->NPC_class == CLASS_JEDI)
			{
				//a jedi fighting a jedi - training
				if (NPCS.NPC->client->NPC_class == CLASS_JEDI && NPCS.NPCInfo->rank == RANK_COMMANDER)
				{
					//only trainer taunts
					event = EV_TAUNT1;
				}
			}
			else
			{
				//reborn or a jedi fighting an enemy
				event = Q_irand(EV_TAUNT1, EV_TAUNT3);
			}
			if (event != -1)
			{
				G_AddVoiceEvent(NPCS.NPC, event, 3000);
				jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] = NPCS.NPCInfo->blockedSpeechDebounceTime = level.time + 6000;
				if (NPCS.NPCInfo->aiFlags & NPCAI_ROSH)
				{
					TIMER_Set(NPCS.NPC, "chatter", Q_irand(15000, 20000));
				}
				else
				{
					TIMER_Set(NPCS.NPC, "chatter", Q_irand(12000, 15000));
				}

				if (NPCS.NPC->enemy &&
					NPCS.NPC->enemy->NPC &&
					NPCS.NPC->enemy->s.weapon == WP_SABER &&
					NPCS.NPC->enemy->client && (NPCS.NPC->enemy->client->NPC_class == CLASS_JEDI))
				{
					G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_TAUNT1, EV_TAUNT3), 2000);
				}
				return qtrue;
			}
		}
	}
	return qfalse;
}



/*
==========================================================================================
MOVEMENT
==========================================================================================
*/
static qboolean Jedi_ClearPathToSpot(vec3_t dest, const int impact_ent_num)
{
	trace_t trace;
	vec3_t mins, end, dir;
	float drop = 0.0f;

	//Offset the step height
	VectorSet(mins, NPCS.NPC->r.mins[0], NPCS.NPC->r.mins[1], NPCS.NPC->r.mins[2] + STEPSIZE);

	trap->Trace(&trace, NPCS.NPC->r.currentOrigin, mins, NPCS.NPC->r.maxs, dest, NPCS.NPC->s.number, NPCS.NPC->clipmask, qfalse, 0, 0);

	//Do a simple check
	if (trace.allsolid || trace.startsolid)
	{
		//inside solid
		return qfalse;
	}

	if (trace.fraction < 1.0f)
	{
		//hit something
		if (impact_ent_num != ENTITYNUM_NONE && trace.entityNum == impact_ent_num)
		{
			//hit what we're going after
			return qtrue;
		}
		return qfalse;
	}

	//otherwise, clear path in a straight line.
	//Now at intervals of my size, go along the trace and trace down STEPSIZE to make sure there is a solid floor.
	VectorSubtract(dest, NPCS.NPC->r.currentOrigin, dir);
	const float dist = VectorNormalize(dir);
	if (dest[2] > NPCS.NPC->r.currentOrigin[2])
	{
		//going up, check for steps
		drop = STEPSIZE;
	}
	else
	{
		//going down or level, check for moderate drops
		drop = 64;
	}
	for (float i = NPCS.NPC->r.maxs[0] * 2; i < dist; i += NPCS.NPC->r.maxs[0] * 2)
	{
		vec3_t start;
		VectorMA(NPCS.NPC->r.currentOrigin, i, dir, start);
		VectorCopy(start, end);
		end[2] -= drop;
		trap->Trace(&trace, start, mins, NPCS.NPC->r.maxs, end, NPCS.NPC->s.number, NPCS.NPC->clipmask, qfalse, 0, 0); //NPCS.NPC->r.mins?
		if (trace.fraction < 1.0f || trace.allsolid || trace.startsolid)
		{
			//good to go
			continue;
		}
		//no floor here! (or a long drop?)
		return qfalse;
	}
	//we made it!
	return qtrue;
}



qboolean NPC_MoveDirClear(const int forwardmove, const int rightmove, const qboolean reset)
{
	vec3_t forward, right, test_pos, angles = { 0 }, mins;
	trace_t trace;
	float bottom_max = -STEPSIZE * 4 - 1;

	if (!forwardmove && !rightmove)
	{
		//not even moving
		return qtrue;
	}

	if (NPCS.ucmd.upmove > 0 || NPCS.NPC->client->ps.fd.forceJumpCharge)
	{
		//Going to jump
		return qtrue;
	}

	if (NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		//in the air
		return qtrue;
	}

	VectorCopy(NPCS.NPC->r.mins, mins);
	mins[2] += STEPSIZE;
	angles[PITCH] = angles[ROLL] = 0;
	angles[YAW] = NPCS.NPC->client->ps.viewangles[YAW]; //Add NPCS.ucmd.angles[YAW]?
	AngleVectors(angles, forward, right, NULL);
	const float fwd_dist = (float)(forwardmove) / 2.0f;
	const float rt_dist = (float)(rightmove) / 2.0f;
	VectorMA(NPCS.NPC->r.currentOrigin, fwd_dist, forward, test_pos);
	VectorMA(test_pos, rt_dist, right, test_pos);
	trap->Trace(&trace, NPCS.NPC->r.currentOrigin, mins, NPCS.NPC->r.maxs, test_pos, NPCS.NPC->s.number, NPCS.NPC->clipmask | CONTENTS_BOTCLIP, qfalse, 0, 0);
	if (trace.allsolid || trace.startsolid)
	{
		if (reset)
		{
			trace.fraction = 1.0f;
		}
		VectorCopy(test_pos, trace.endpos);
	}
	if (trace.fraction < 0.6)
	{
		//Going to bump into something very close, don't move, just turn
		if (NPCS.NPC->enemy && trace.entityNum == NPCS.NPC->enemy->s.number || NPCS.NPCInfo->goalEntity && trace.entityNum ==
			NPCS.NPCInfo->goalEntity->s.number)
		{
			//okay to bump into enemy or goal
			return qtrue;
		}
		if (reset)
		{
			//actually want to screw with the NPCS.ucmd
			NPCS.ucmd.forwardmove = 0;
			NPCS.ucmd.rightmove = 0;
			VectorClear(NPCS.NPC->client->ps.moveDir);
		}
		return qfalse;
	}

	if (NPCS.NPCInfo->goalEntity)
	{
		qboolean enemy_in_air = qfalse;

		if (NPCS.NPC->NPC->goalEntity->client)
		{
			if (NPCS.NPC->NPC->goalEntity->client->ps.groundEntityNum == ENTITYNUM_NONE)
			{
				enemy_in_air = qtrue;
			}
		}

		if (NPCS.NPCInfo->goalEntity->r.currentOrigin[2] < NPCS.NPC->r.currentOrigin[2])
		{
			//goal is below me, okay to step off at least that far plus stepheight
			if (!enemy_in_air)
			{
				bottom_max += NPCS.NPCInfo->goalEntity->r.currentOrigin[2] - NPCS.NPC->r.currentOrigin[2];
			}
		}
	}
	VectorCopy(trace.endpos, test_pos);
	test_pos[2] += bottom_max;

	trap->Trace(&trace, trace.endpos, mins, NPCS.NPC->r.maxs, test_pos, NPCS.NPC->s.number, NPCS.NPC->clipmask, qfalse, 0, 0);

	if (trace.allsolid || trace.startsolid)
	{
		//Not going off a cliff
		//Com_Printf( "%d walk off cliff okay (droptrace in solid)\n", level.time );
		return qtrue;
	}

	if (trace.fraction < 1.0)
	{
		//Not going off a cliff
		return qtrue;
	}

	//going to fall at least bottom_max, don't move, just turn... is this bad, though?  What if we want them to drop off?
	if (reset)
	{
		//actually want to screw with the NPCS.ucmd
		NPCS.ucmd.forwardmove *= -1.0; //= 0;
		NPCS.ucmd.rightmove *= -1.0; //= 0;
		VectorScale(NPCS.NPC->client->ps.moveDir, -1, NPCS.NPC->client->ps.moveDir);
	}
	return qfalse;
}



/*
-------------------------
Jedi_HoldPosition
-------------------------
*/

static void Jedi_HoldPosition(void)
{
	NPCS.NPCInfo->goalEntity = NULL;
}



/*
-------------------------
Jedi_Move
-------------------------
*/

static qboolean Jedi_Move(gentity_t* goal, const qboolean retreat)
{
	const qboolean moved = NPC_MoveToGoal(qtrue);
	navInfo_t info;

	NPCS.NPCInfo->combatMove = qtrue;
	NPCS.NPCInfo->goalEntity = goal;

	if (retreat)
	{
		NPCS.ucmd.forwardmove *= -1;
		NPCS.ucmd.rightmove *= -1;
		VectorScale(NPCS.NPC->client->ps.moveDir, -1, NPCS.NPC->client->ps.moveDir);
	}

	//Get the move info
	NAV_GetLastMove(&info);

	//If we hit our target, then stop and fire!
	if (info.flags & NIF_COLLISION && info.blocker == NPCS.NPC->enemy)
	{
		Jedi_HoldPosition();
	}

	//If our move failed, then reset
	if (moved == qfalse)
	{
		Jedi_HoldPosition();
	}
	return moved;
}



static qboolean Jedi_Hunt(void)
{
	if (NPCS.NPCInfo->stats.aggression > 1)
	{
		//approach enemy
		NPCS.NPCInfo->combatMove = qtrue;
		if (!(NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES))
		{
			NPC_UpdateAngles(qtrue, qtrue);
			return qtrue;
		}
		if (NPCS.NPCInfo->goalEntity == NULL)
		{
			//hunt
			NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
		}
		else
		{
			//hunt
			NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
			NPCS.NPCInfo->goalRadius = 40.0f;
		}

		if (NPC_MoveToGoal(qfalse))
		{
			NPC_UpdateAngles(qtrue, qtrue);
			return qtrue;
		}
	}
	return qfalse;
}



// ---------------------------------------------------------
// RETREAT MODE STARTS HERE
// ---------------------------------------------------------
static void Jedi_StartBackOff(void)
{
	TIMER_Set(NPCS.NPC, "roamTime", -level.time);
	TIMER_Set(NPCS.NPC, "strafeLeft", -level.time);
	TIMER_Set(NPCS.NPC, "strafeRight", -level.time);
	TIMER_Set(NPCS.NPC, "walking", -level.time);
	TIMER_Set(NPCS.NPC, "moveforward", -level.time);
	TIMER_Set(NPCS.NPC, "movenone", -level.time);
	TIMER_Set(NPCS.NPC, "moveright", -level.time);
	TIMER_Set(NPCS.NPC, "moveleft", -level.time);
	TIMER_Set(NPCS.NPC, "movecenter", -level.time);
	TIMER_Set(NPCS.NPC, "moveback", 1000);
	NPCS.ucmd.forwardmove = -127;
	NPCS.ucmd.rightmove = 0;
	NPCS.ucmd.upmove = 0;
	if (d_JediAI.integer || d_combatinfo.integer || g_DebugSaberCombat.integer)
	{
		Com_Printf("%s backing off from spin attack!\n", NPCS.NPC->NPC_type);
	}
	TIMER_Set(NPCS.NPC, "specialEvasion", 1000);
	TIMER_Set(NPCS.NPC, "noRetreat", -level.time);

	if (PM_PainAnim(NPCS.NPC->client->ps.legsAnim))
	{
		NPCS.NPC->client->ps.legsTimer = 0;
	}
	VectorClear(NPCS.NPC->client->ps.moveDir);
}

static void JediDirectionalDashDodge(gentity_t* NPC, gentity_t* enemy)
{
	// -------------------------------------------------
	// DETERMINE DODGE DIRECTION BASED ON ATTACK ANGLE
	// -------------------------------------------------
	vec3_t toEnemy;
	VectorSubtract(enemy->r.currentOrigin, NPC->r.currentOrigin, toEnemy);
	VectorNormalize(toEnemy);

	vec3_t fwd, right;
	AngleVectors(NPC->client->ps.viewangles, fwd, right, NULL);

	float rightdot = DotProduct(toEnemy, right);

	if (rightdot > 0.3f)      // left dodge
		NPCS.ucmd.rightmove = -64;
	else if (rightdot < -0.3f) // right dodge
		NPCS.ucmd.rightmove = 64;
	else                // backward dodge
		NPCS.ucmd.forwardmove = -64;   // enemy in front ? backward dodge ONLY

	// -------------------------------------------------
	// VELOCITY BURST
	// -------------------------------------------------

	if (NPC->client->ps.groundEntityNum != ENTITYNUM_NONE)
	{
		NPC->client->ps.velocity[0] = NPC->client->ps.velocity[0] * 2.0f;
		NPC->client->ps.velocity[1] = NPC->client->ps.velocity[1] * 2.0f;

		// Kill any forward velocity (never dash into enemy)
		float forwardVel = DotProduct(NPC->client->ps.velocity, fwd);
		if (forwardVel > 0.0f)
		{
			vec3_t forwardPush;
			VectorScale(fwd, forwardVel, forwardPush);
			VectorSubtract(NPC->client->ps.velocity, forwardPush, NPC->client->ps.velocity);
		}
		// -------------------------------------------------
		// DASH ANIMATION + SOUND
		// -------------------------------------------------

		NPC->client->pers.cmd.rightmove = NPCS.ucmd.rightmove;
		NPC->client->pers.cmd.forwardmove = NPCS.ucmd.forwardmove;

		// play dash anim (reads pers.cmd)
		ForceDashAnimDash(NPC);
		G_SoundOnEnt(NPC, CHAN_BODY, "sound/weapons/force/dash.mp3");

		// ensure saber state is restored to "ready"
		// set both current and next to avoid races with other logic
		NPC->client->ps.saberMove = LS_READY;
	}
}


// ---------------------------------------------------------
// RETREAT MODE ENDS HERE
// ---------------------------------------------------------

static void JediDirectionalDashAttack(gentity_t* NPC, gentity_t* enemy)
{
	// Safety
	if (NPC == NULL || enemy == NULL || NPC->client == NULL)
	{
		return;
	}
	if (PM_InKnockDown(&NPC->client->ps))
	{
		return;
	}
	if (PM_InRoll(&NPC->client->ps))
	{
		return;
	}
	if (PM_InGetUp(&NPC->client->ps))
	{
		return;
	}

	// -------------------------------------------------
	// DETERMINE ATTACK DIRECTION (TOWARD ENEMY)
	// -------------------------------------------------
	vec3_t toEnemy;
	VectorSubtract(enemy->r.currentOrigin, NPC->r.currentOrigin, toEnemy);
	VectorNormalize(toEnemy);

	vec3_t fwd, right;
	AngleVectors(NPC->client->ps.viewangles, fwd, right, NULL);

	// Forward dash only (attack intent)
	NPCS.ucmd.forwardmove = 127;   // max forward input
	NPCS.ucmd.rightmove = 0;

	// -------------------------------------------------
	// VELOCITY BURST TOWARD ENEMY
	// -------------------------------------------------
	if (NPC->client->ps.groundEntityNum != ENTITYNUM_NONE)
	{
		// Strong forward push
		vec3_t dashVel;
		VectorScale(toEnemy, 600.0f, dashVel);   // adjust strength as needed

		NPC->client->ps.velocity[0] = dashVel[0];
		NPC->client->ps.velocity[1] = dashVel[1];
		// keep Z velocity unchanged

		// -------------------------------------------------
		// DASH ANIMATION + SOUND
		// -------------------------------------------------
		NPC->client->pers.cmd.forwardmove = NPCS.ucmd.forwardmove;
		NPC->client->pers.cmd.rightmove = NPCS.ucmd.rightmove;

		ForceDashAnimDash(NPC);
		G_SoundOnEnt(NPC, CHAN_BODY, "sound/weapons/force/dash.mp3");

		// Saber ready for attack
		NPC->client->ps.saberMove = LS_READY;
	}
}



static qboolean Jedi_Track(void)
{
	//if we're at all interested in fighting, go after him
	if (NPCS.NPCInfo->stats.aggression > 1)
	{//approach enemy
		NPCS.NPCInfo->combatMove = qtrue;
		NPC_SetMoveGoal(NPCS.NPC, NPCS.NPCInfo->enemyLastSeenLocation, 16, qtrue, -1, NULL);
		if (NPC_MoveToGoal(qfalse))
		{
			NPC_UpdateAngles(qtrue, qtrue);
			return qtrue;
		}
	}
	return qfalse;
}



static qboolean Jedi_Retreat(void)
{
	if (!TIMER_Done(NPCS.NPC, "noRetreat"))
	{
		//don't actually move
		return qfalse;
	}
	return Jedi_Move(NPCS.NPC->enemy, qtrue);
}



static qboolean Jedi_Advance(void)
{
	if (NPCS.NPCInfo->aiFlags & NPCAI_HEAL_ROSH)
	{
		return qfalse;
	}
	if (!NPCS.NPC->client->ps.saberInFlight)
	{
		WP_ActivateSaber(NPCS.NPC);
	}
	return Jedi_Move(NPCS.NPC->enemy, qfalse);
}


static void Jedi_AdjustSaberAnimLevel(const gentity_t* self, int new_level)
{
	if (!self || !self->client)
	{
		return;
	}
	//override stance with special saber style if we're using special sabers
	if (self->client->saber[0].model[0] && self->client->saber[1].model[0]
		&& !G_ValidSaberStyle(self, SS_DUAL))
	{
		self->client->ps.fd.saberAnimLevel = SS_DUAL;
		return;
	}
	if (self->client->saber[0].numBlades > 1
		&& WP_SaberCanTurnOffSomeBlades(&self->client->saber[0])
		&& !G_ValidSaberStyle(self, SS_STAFF))
	{
		self->client->ps.fd.saberAnimLevel = SS_STAFF;
		return;
	}

	if (self->client->playerTeam == NPCTEAM_ENEMY)
	{
		if (!Q_stricmp("cultist_saber_all", self->NPC_type)
			|| !Q_stricmp("cultist_saber_all_throw", self->NPC_type)
			|| !Q_stricmp("md_maul_tcw", self->NPC_type)
			|| !Q_stricmp("md_maul_cyber_tcw", self->NPC_type))
		{
			//use any, regardless of rank, etc.
		}
		else if (!Q_stricmp("cultist_saber", self->NPC_type)
			|| !Q_stricmp("cultist_saber_throw", self->NPC_type))
		{
			//fast only
			self->client->ps.fd.saberAnimLevel = SS_FAST;
		}
		else if (!Q_stricmp("cultist_saber_med", self->NPC_type)
			|| !Q_stricmp("cultist_saber_med_throw", self->NPC_type))
		{
			//med only
			self->client->ps.fd.saberAnimLevel = SS_MEDIUM;
		}
		else if (!Q_stricmp("cultist_saber_strong", self->NPC_type)
			|| !Q_stricmp("cultist_saber_strong_throw", self->NPC_type))
		{
			//strong only
			self->client->ps.fd.saberAnimLevel = SS_STRONG;
		}
		else if (!Q_stricmp("md_grievous", self->NPC_type)
			|| !Q_stricmp("md_grievous4", self->NPC_type)
			|| !Q_stricmp("md_grievous_robed", self->NPC_type)
			|| !Q_stricmp("md_clone_assassin", self->NPC_type)
			|| !Q_stricmp("md_jango", self->NPC_type)
			|| !Q_stricmp("md_jango_geo", self->NPC_type)
			|| !Q_stricmp("md_ani_ep2_dual", self->NPC_type)
			|| !Q_stricmp("md_serra", self->NPC_type)
			|| !Q_stricmp("md_ahsoka_rebels", self->NPC_type)
			|| !Q_stricmp("md_ahsoka", self->NPC_type)
			|| !Q_stricmp("md_ahsoka_s7", self->NPC_type)
			|| !Q_stricmp("md_ventress", self->NPC_type)
			|| !Q_stricmp("md_ven_ns", self->NPC_type)
			|| !Q_stricmp("md_ven_bh", self->NPC_type)
			|| !Q_stricmp("md_ven_dg", self->NPC_type)
			|| !Q_stricmp("md_asharad", self->NPC_type)
			|| !Q_stricmp("md_asharad_tus", self->NPC_type)
			|| !Q_stricmp("boba_fett", self->NPC_type)
			|| !Q_stricmp("boba_fett_esb", self->NPC_type)
			|| !Q_stricmp("md_pguard4", self->NPC_type)
			|| !Q_stricmp("md_grie_egg", self->NPC_type)
			|| !Q_stricmp("md_grie3_egg", self->NPC_type)
			|| !Q_stricmp("md_grie4_egg", self->NPC_type)
			|| !Q_stricmp("md_fet_ga", self->NPC_type)
			|| !Q_stricmp("md_fet2_ga", self->NPC_type)
			|| !Q_stricmp("md_fet3_ga", self->NPC_type)
			|| !Q_stricmp("md_ven2_ga", self->NPC_type)
			|| !Q_stricmp("md_ket_jt", self->NPC_type)
			|| !Q_stricmp("md_fet_ka", self->NPC_type)
			|| !Q_stricmp("md_fet2_ka", self->NPC_type)
			|| !Q_stricmp("md_clo2_rt", self->NPC_type)
			|| !Q_stricmp("md_clo5_rt", self->NPC_type)
			|| !Q_stricmp("reborn_dual", self->NPC_type)
			|| !Q_stricmp("reborn_dual2", self->NPC_type)
			|| !Q_stricmp("alora_dual", self->NPC_type)
			|| !Q_stricmp("JediTrainer", self->NPC_type)
			|| !Q_stricmp("jedi_zf2", self->NPC_type)
			|| !Q_stricmp("RebornMasterDual", self->NPC_type)
			|| !Q_stricmp("Tavion_scepter", self->NPC_type)
			|| !Q_stricmp("md_jed6_jt", self->NPC_type)
			|| !Q_stricmp("md_jed11_jt", self->NPC_type)
			|| !Q_stricmp("md_jed13_jt", self->NPC_type)
			|| !Q_stricmp("md_jediknight2_jt", self->NPC_type)
			|| !Q_stricmp("md_jediveteran2_jt", self->NPC_type)
			|| !Q_stricmp("md_jediveteran3_jt", self->NPC_type)
			|| !Q_stricmp("md_serra_jt", self->NPC_type))
		{
			//dual only
			self->client->ps.fd.saberAnimLevel = SS_DUAL;
		}
		else if (!Q_stricmp("md_magnaguard", self->NPC_type)
			|| !Q_stricmp("md_inquisitor", self->NPC_type)
			|| !Q_stricmp("md_5thbrother", self->NPC_type)
			|| !Q_stricmp("md_7thsister", self->NPC_type)
			|| !Q_stricmp("md_8thbrother", self->NPC_type)
			|| !Q_stricmp("md_maul_rebels", self->NPC_type)
			|| !Q_stricmp("md_maul_rebels2", self->NPC_type)
			|| !Q_stricmp("md_maul_rebels3", self->NPC_type)
			|| !Q_stricmp("md_maul_rebels4", self->NPC_type)
			|| !Q_stricmp("md_maul_rebels5", self->NPC_type)
			|| !Q_stricmp("md_maul_rebels6", self->NPC_type)
			|| !Q_stricmp("md_jbrute", self->NPC_type)
			|| !Q_stricmp("md_templeguard", self->NPC_type)
			|| !Q_stricmp("md_sidious_tcw", self->NPC_type)
			|| !Q_stricmp("md_maul_tcw_staff", self->NPC_type)
			|| !Q_stricmp("md_savage", self->NPC_type)
			|| !Q_stricmp("md_galen", self->NPC_type)
			|| !Q_stricmp("md_galen_jt", self->NPC_type)
			|| !Q_stricmp("md_galencjr", self->NPC_type)
			|| !Q_stricmp("md_starkiller", self->NPC_type)
			|| !Q_stricmp("md_sithstalker", self->NPC_type)
			|| !Q_stricmp("md_stk_lord", self->NPC_type)
			|| !Q_stricmp("md_stk_tat", self->NPC_type)
			|| !Q_stricmp("darthdesolous", self->NPC_type)
			|| !Q_stricmp("purge_trooper", self->NPC_type)
			|| !Q_stricmp("cal_kestis_staff", self->NPC_type)
			|| !Q_stricmp("md_2ndsister", self->NPC_type)
			|| !Q_stricmp("md_shadowguard", self->NPC_type)
			|| !Q_stricmp("md_ven_dual", self->NPC_type)
			|| !Q_stricmp("md_pguard5", self->NPC_type)
			|| !Q_stricmp("md_mau_dof", self->NPC_type)
			|| !Q_stricmp("md_mau2_dof", self->NPC_type)
			|| !Q_stricmp("md_mau3_dof", self->NPC_type)
			|| !Q_stricmp("md_mag_egg", self->NPC_type)
			|| !Q_stricmp("md_mag_ga", self->NPC_type)
			|| !Q_stricmp("md_ven_ga", self->NPC_type)
			|| !Q_stricmp("md_mau_luke", self->NPC_type)
			|| !Q_stricmp("darthphobos", self->NPC_type)
			|| !Q_stricmp("md_tus5_tc", self->NPC_type)
			|| !Q_stricmp("md_sta_tfu", self->NPC_type)
			|| !Q_stricmp("md_gua1_tfu", self->NPC_type)
			|| !Q_stricmp("md_sta_cs", self->NPC_type)
			|| !Q_stricmp("md_gua_am", self->NPC_type)
			|| !Q_stricmp("md_gua2_am", self->NPC_type)
			|| !Q_stricmp("md_mag_am", self->NPC_type)
			|| !Q_stricmp("md_mag2_am", self->NPC_type)
			|| !Q_stricmp("JediF", self->NPC_type)
			|| !Q_stricmp("JediMaster", self->NPC_type)
			|| !Q_stricmp("jedi_kdm1", self->NPC_type)
			|| !Q_stricmp("jedi_tf1", self->NPC_type)
			|| !Q_stricmp("reborn_staff", self->NPC_type)
			|| !Q_stricmp("reborn_staff2", self->NPC_type)
			|| !Q_stricmp("RebornMasterStaff", self->NPC_type)
			|| !Q_stricmp("md_jed7_jt", self->NPC_type)
			|| !Q_stricmp("md_jed12_jt", self->NPC_type)
			|| !Q_stricmp("md_jed14_jt", self->NPC_type)
			|| !Q_stricmp("md_jedimaster3_jt", self->NPC_type)
			|| !Q_stricmp("md_jedimaster5_jt", self->NPC_type)
			|| !Q_stricmp("md_guard_jt", self->NPC_type)
			|| !Q_stricmp("md_guardboss_jt", self->NPC_type)
			|| !Q_stricmp("md_jedibrute_jt", self->NPC_type)
			|| !Q_stricmp("md_maul", self->NPC_type)
			|| !Q_stricmp("md_maul_robed", self->NPC_type)
			|| !Q_stricmp("md_maul_hooded", self->NPC_type)
			|| !Q_stricmp("md_maul_wots", self->NPC_type))
		{
			//staff only
			self->client->ps.fd.saberAnimLevel = SS_STAFF;
		}
		else
		{
			//use any, regardless of rank, etc.
		}
	}
	else
	{
		if (!Q_stricmp("cultist_saber_all", self->NPC_type)
			|| !Q_stricmp("cultist_saber_all_throw", self->NPC_type)
			|| !Q_stricmp("md_maul_tcw", self->NPC_type)
			|| !Q_stricmp("md_maul_cyber_tcw", self->NPC_type))
		{
			//use any, regardless of rank, etc.
		}
		else if (!Q_stricmp("cultist_saber", self->NPC_type)
			|| !Q_stricmp("cultist_saber_throw", self->NPC_type))
		{
			//fast only
			self->client->ps.fd.saberAnimLevel = SS_FAST;
		}
		else if (!Q_stricmp("cultist_saber_med", self->NPC_type)
			|| !Q_stricmp("cultist_saber_med_throw", self->NPC_type))
		{
			//med only
			self->client->ps.fd.saberAnimLevel = SS_MEDIUM;
		}
		else if (!Q_stricmp("cultist_saber_strong", self->NPC_type)
			|| !Q_stricmp("cultist_saber_strong_throw", self->NPC_type))
		{
			//strong only
			self->client->ps.fd.saberAnimLevel = SS_STRONG;
		}
		else if (!Q_stricmp("md_grievous", self->NPC_type)
			|| !Q_stricmp("md_grievous4", self->NPC_type)
			|| !Q_stricmp("md_grievous_robed", self->NPC_type)
			|| !Q_stricmp("md_clone_assassin", self->NPC_type)
			|| !Q_stricmp("md_jango", self->NPC_type)
			|| !Q_stricmp("md_jango_geo", self->NPC_type)
			|| !Q_stricmp("md_ani_ep2_dual", self->NPC_type)
			|| !Q_stricmp("md_serra", self->NPC_type)
			|| !Q_stricmp("md_ahsoka_rebels", self->NPC_type)
			|| !Q_stricmp("md_ahsoka", self->NPC_type)
			|| !Q_stricmp("md_ahsoka_s7", self->NPC_type)
			|| !Q_stricmp("md_ventress", self->NPC_type)
			|| !Q_stricmp("md_ven_ns", self->NPC_type)
			|| !Q_stricmp("md_ven_bh", self->NPC_type)
			|| !Q_stricmp("md_ven_dg", self->NPC_type)
			|| !Q_stricmp("md_asharad", self->NPC_type)
			|| !Q_stricmp("md_asharad_tus", self->NPC_type)
			|| !Q_stricmp("boba_fett", self->NPC_type)
			|| !Q_stricmp("boba_fett_esb", self->NPC_type)
			|| !Q_stricmp("md_pguard4", self->NPC_type)
			|| !Q_stricmp("md_grie_egg", self->NPC_type)
			|| !Q_stricmp("md_grie3_egg", self->NPC_type)
			|| !Q_stricmp("md_grie4_egg", self->NPC_type)
			|| !Q_stricmp("md_fet_ga", self->NPC_type)
			|| !Q_stricmp("md_fet2_ga", self->NPC_type)
			|| !Q_stricmp("md_fet3_ga", self->NPC_type)
			|| !Q_stricmp("md_ven2_ga", self->NPC_type)
			|| !Q_stricmp("md_ket_jt", self->NPC_type)
			|| !Q_stricmp("md_fet_ka", self->NPC_type)
			|| !Q_stricmp("md_fet2_ka", self->NPC_type)
			|| !Q_stricmp("md_clo2_rt", self->NPC_type)
			|| !Q_stricmp("md_clo5_rt", self->NPC_type)
			|| !Q_stricmp("reborn_dual", self->NPC_type)
			|| !Q_stricmp("reborn_dual2", self->NPC_type)
			|| !Q_stricmp("alora_dual", self->NPC_type)
			|| !Q_stricmp("JediTrainer", self->NPC_type)
			|| !Q_stricmp("jedi_zf2", self->NPC_type)
			|| !Q_stricmp("RebornMasterDual", self->NPC_type)
			|| !Q_stricmp("Tavion_scepter", self->NPC_type)
			|| !Q_stricmp("md_jed6_jt", self->NPC_type)
			|| !Q_stricmp("md_jed11_jt", self->NPC_type)
			|| !Q_stricmp("md_jed13_jt", self->NPC_type)
			|| !Q_stricmp("md_jediknight2_jt", self->NPC_type)
			|| !Q_stricmp("md_jediveteran2_jt", self->NPC_type)
			|| !Q_stricmp("md_jediveteran3_jt", self->NPC_type)
			|| !Q_stricmp("md_serra_jt", self->NPC_type))
		{
			//dual only
			self->client->ps.fd.saberAnimLevel = SS_DUAL;
		}
		else if (!Q_stricmp("md_magnaguard", self->NPC_type)
			|| !Q_stricmp("md_inquisitor", self->NPC_type)
			|| !Q_stricmp("md_5thbrother", self->NPC_type)
			|| !Q_stricmp("md_7thsister", self->NPC_type)
			|| !Q_stricmp("md_8thbrother", self->NPC_type)
			|| !Q_stricmp("md_maul_rebels", self->NPC_type)
			|| !Q_stricmp("md_maul_rebels2", self->NPC_type)
			|| !Q_stricmp("md_maul_rebels3", self->NPC_type)
			|| !Q_stricmp("md_maul_rebels4", self->NPC_type)
			|| !Q_stricmp("md_maul_rebels5", self->NPC_type)
			|| !Q_stricmp("md_maul_rebels6", self->NPC_type)
			|| !Q_stricmp("md_jbrute", self->NPC_type)
			|| !Q_stricmp("md_templeguard", self->NPC_type)
			|| !Q_stricmp("md_sidious_tcw", self->NPC_type)
			|| !Q_stricmp("md_maul_tcw_staff", self->NPC_type)
			|| !Q_stricmp("md_savage", self->NPC_type)
			|| !Q_stricmp("md_galen", self->NPC_type)
			|| !Q_stricmp("md_galen_jt", self->NPC_type)
			|| !Q_stricmp("md_galencjr", self->NPC_type)
			|| !Q_stricmp("md_starkiller", self->NPC_type)
			|| !Q_stricmp("md_sithstalker", self->NPC_type)
			|| !Q_stricmp("md_stk_lord", self->NPC_type)
			|| !Q_stricmp("md_stk_tat", self->NPC_type)
			|| !Q_stricmp("darthdesolous", self->NPC_type)
			|| !Q_stricmp("purge_trooper", self->NPC_type)
			|| !Q_stricmp("cal_kestis_staff", self->NPC_type)
			|| !Q_stricmp("md_2ndsister", self->NPC_type)
			|| !Q_stricmp("md_shadowguard", self->NPC_type)
			|| !Q_stricmp("md_ven_dual", self->NPC_type)
			|| !Q_stricmp("md_pguard5", self->NPC_type)
			|| !Q_stricmp("md_mau_dof", self->NPC_type)
			|| !Q_stricmp("md_mau2_dof", self->NPC_type)
			|| !Q_stricmp("md_mau3_dof", self->NPC_type)
			|| !Q_stricmp("md_mag_egg", self->NPC_type)
			|| !Q_stricmp("md_mag_ga", self->NPC_type)
			|| !Q_stricmp("md_ven_ga", self->NPC_type)
			|| !Q_stricmp("md_mau_luke", self->NPC_type)
			|| !Q_stricmp("darthphobos", self->NPC_type)
			|| !Q_stricmp("md_tus5_tc", self->NPC_type)
			|| !Q_stricmp("md_sta_tfu", self->NPC_type)
			|| !Q_stricmp("md_gua1_tfu", self->NPC_type)
			|| !Q_stricmp("md_sta_cs", self->NPC_type)
			|| !Q_stricmp("md_gua_am", self->NPC_type)
			|| !Q_stricmp("md_gua2_am", self->NPC_type)
			|| !Q_stricmp("md_mag_am", self->NPC_type)
			|| !Q_stricmp("md_mag2_am", self->NPC_type)
			|| !Q_stricmp("JediF", self->NPC_type)
			|| !Q_stricmp("JediMaster", self->NPC_type)
			|| !Q_stricmp("jedi_kdm1", self->NPC_type)
			|| !Q_stricmp("jedi_tf1", self->NPC_type)
			|| !Q_stricmp("reborn_staff", self->NPC_type)
			|| !Q_stricmp("reborn_staff2", self->NPC_type)
			|| !Q_stricmp("RebornMasterStaff", self->NPC_type)
			|| !Q_stricmp("md_jed7_jt", self->NPC_type)
			|| !Q_stricmp("md_jed12_jt", self->NPC_type)
			|| !Q_stricmp("md_jed14_jt", self->NPC_type)
			|| !Q_stricmp("md_jedimaster3_jt", self->NPC_type)
			|| !Q_stricmp("md_jedimaster5_jt", self->NPC_type)
			|| !Q_stricmp("md_guard_jt", self->NPC_type)
			|| !Q_stricmp("md_guardboss_jt", self->NPC_type)
			|| !Q_stricmp("md_jedibrute_jt", self->NPC_type)
			|| !Q_stricmp("md_maul", self->NPC_type)
			|| !Q_stricmp("md_maul_robed", self->NPC_type)
			|| !Q_stricmp("md_maul_hooded", self->NPC_type)
			|| !Q_stricmp("md_maul_wots", self->NPC_type))
		{
			//staff only
			self->client->ps.fd.saberAnimLevel = SS_STAFF;
		}
		else
		{
			//use any, regardless of rank, etc.
		}
	}
	if (!G_ValidSaberStyle(self, new_level))
	{
		for (int count = SS_FAST; count < SS_STAFF; count++)
		{
			new_level++;
			if (new_level > SS_STAFF)
			{
				new_level = SS_FAST;
			}

			if (G_ValidSaberStyle(self, new_level))
			{
				break;
			}
		}
	}

	//set stance
	self->client->ps.fd.saberAnimLevel = new_level;

	if (d_JediAI.integer || g_DebugSaberCombat.integer)
	{
		switch (self->client->ps.fd.saberAnimLevel)
		{
		case SS_FAST:
			Com_Printf(S_COLOR_GREEN"%s Saber Attack Set: fast\n", self->NPC_type);
			break;
		case SS_MEDIUM:
			Com_Printf(S_COLOR_YELLOW"%s Saber Attack Set: medium\n", self->NPC_type);
			break;
		case SS_STRONG:
			Com_Printf(S_COLOR_RED"%s Saber Attack Set: strong\n", self->NPC_type);
			break;
		case SS_DESANN:
		case SS_BLASTER: // SS_BLASTER (2026-10-14)
			Com_Printf(S_COLOR_RED"%s Saber Attack Set: desann\n", self->NPC_type);
			break;
		case SS_TAVION:
			Com_Printf(S_COLOR_RED"%s Saber Attack Set: tavion\n", self->NPC_type);
			break;
		case SS_DUAL:
			Com_Printf(S_COLOR_CYAN"%s Saber Attack Set: dual\n", self->NPC_type);
			break;
		case SS_STAFF:
			Com_Printf(S_COLOR_ORANGE"%s Saber Attack Set: staff\n", self->NPC_type);
			break;
		default:;
		}
	}
}



static void Jedi_CheckDecreasesaber_anim_level(void)
{
	if (NPCS.NPC->client->ps.fd.saberAnimLevel == SS_DUAL || NPCS.NPC->client->ps.fd.saberAnimLevel == SS_STAFF)
	{
		return;
	}

	if (!NPCS.NPC->client->ps.weaponTime && !(NPCS.ucmd.buttons & (BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_FORCEPOWER)))
	{
		//not attacking
		if (TIMER_Done(NPCS.NPC, "saberLevelDebounce") && !Q_irand(0, 10))
		{
			if ((NPCS.NPC->client->ps.fd.saberAnimLevel == SS_STRONG
				|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_MEDIUM
				|| (NPCS.NPC->client->ps.fd.saberAnimLevel == SS_DESANN || NPCS.NPC->client->ps.fd.saberAnimLevel == SS_BLASTER) // SS_BLASTER (2026-10-14)
				|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_FAST
				|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_TAVION)
				&& NPCS.NPC->client->ps.fd.saberAnimLevel != SS_STAFF
				&& NPCS.NPC->client->ps.fd.saberAnimLevel != SS_DUAL
				&& !npc_is_staff_style(NPCS.NPC)
				&& !npc_is_dual_style(NPCS.NPC))
			{
				//my saber is not in a parrying position
				Jedi_AdjustSaberAnimLevel(NPCS.NPC, Q_irand(SS_FAST, SS_TAVION));
			}
			TIMER_Set(NPCS.NPC, "saberLevelDebounce", Q_irand(3000, 10000));
		}
	}
	else
	{
		TIMER_Set(NPCS.NPC, "saberLevelDebounce", Q_irand(1000, 5000));
	}
}



static qboolean Jedi_DecideKick(void)
{
	if (PM_InKnockDown(&NPCS.NPC->client->ps))
	{
		return qfalse;
	}
	if (PM_InRoll(&NPCS.NPC->client->ps))
	{
		return qfalse;
	}
	if (PM_InGetUp(&NPCS.NPC->client->ps))
	{
		return qfalse;
	}
	if (NPCS.NPC->enemy->s.number < MAX_CLIENTS)
	{
		return qfalse;
	}
	if (jedi_forbidden_kicker(NPCS.NPC))
	{
		//never kick
		return qfalse;
	}
	if (Q_irand(0, RANK_CAPTAIN + 5) > NPCS.NPCInfo->rank)
	{
		//low chance, based on rank
		return qfalse;
	}
	if (Q_irand(0, 10) > NPCS.NPCInfo->stats.aggression)
	{
		//the madder the better
		return qfalse;
	}
	if (!TIMER_Done(NPCS.NPC, "kickDebounce"))
	{
		//just did one
		return qfalse;
	}
	if (NPCS.NPC->client->ps.weapon == WP_SABER)
	{
		if (NPCS.NPC->client->saber[0].saberFlags & SFL_NO_KICKS)
		{
			return qfalse;
		}
		if (jedi_forbidden_kicker(NPCS.NPC))
		{
			return qfalse;
		}
		if ((NPCS.NPC->client->saber[1].model[0] != 0) && NPCS.NPC->client->saber[1].saberFlags & SFL_NO_KICKS)
		{
			return qfalse;
		}
		if (NPCS.NPC->client->saber[0].saberFlags & SFL_NOT_THROWABLE)
		{
			return qfalse;
		}
		if (PM_SaberInMassiveBounce(NPCS.NPC->client->ps.torsoAnim) ||
			PM_SaberInBounce(NPCS.NPC->client->ps.saberMove) ||
			PM_SaberInBashedAnim(NPCS.NPC->client->ps.torsoAnim) ||
			PM_SaberInAttack(NPCS.NPC->client->ps.saberMove) ||
			PM_SaberInStart(NPCS.NPC->client->ps.saberMove) ||
			PM_SaberInReturn(NPCS.NPC->client->ps.saberMove))
		{
			return qfalse;
		}
	}
	//go for it!
	return qtrue;
}



/*
==========================================================================================
KYLE GRAPPLE CODE
==========================================================================================
*/

void Kyle_GrabEnemy(void)
{
	TIMER_Set(NPCS.NPC, "grabEnemyDebounce", NPCS.NPC->client->ps.torsoTimer + Q_irand(4000, 20000));
}



static void NPC_GrabPlayer(void)
{
	// SP grabs the player with a saber-lock grab (LOCK_KYLE_GRAB1/2), which MP's saber locks don't have
	Kyle_GrabEnemy();
	TIMER_Set(NPCS.NPC, "NPC_GrabPlayerDebounce", NPCS.NPC->client->ps.torsoTimer + Q_irand(20000, 30000));
}




void Kyle_TryGrab(void)
{
	//have the kyle boss NPC attempt a grab.
	NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_KYLE_GRAB, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	NPCS.NPC->client->ps.torsoTimer += 200;
	NPCS.NPC->client->ps.weaponTime = NPCS.NPC->client->ps.torsoTimer;
	NPCS.NPC->client->ps.saberMove = LS_READY;
	VectorClear(NPCS.NPC->client->ps.velocity);
	VectorClear(NPCS.NPC->client->ps.moveDir);
	NPCS.ucmd.rightmove = NPCS.ucmd.forwardmove = NPCS.ucmd.upmove = 0;
	NPCS.NPC->painDebounceTime = level.time + NPCS.NPC->client->ps.torsoTimer;
	NPCS.NPC->client->ps.saberHolstered = 2;
}



static qboolean Kyle_CanDoGrab(void)
{
	if (NPCS.NPC->client->NPC_class == CLASS_KYLE)
	{
		if (NPCS.NPC->client->ps.fd.forcePower > BLOCKPOINTS_FULL
			&& NPCS.NPC->client->ps.fd.blockPoints > BLOCKPOINTS_FULL
			&& NPCS.NPC->client->ps.saberFatigueChainCount < MISHAPLEVEL_LIGHT)
		{
			//Boss Kyle
			if (NPCS.NPC->enemy && NPCS.NPC->enemy->client)
			{
				//have a valid enemy
				if (TIMER_Done(NPCS.NPC, "NPC_GrabPlayerDebounce"))
				{
					//okay to grab again
					if (NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE
						&& NPCS.NPC->enemy->client->ps.groundEntityNum != ENTITYNUM_NONE)
					{
						//me and enemy are on ground
						if (!PM_InOnGroundAnim(NPCS.NPC->enemy->client->ps.legsAnim))
						{
							if ((NPCS.NPC->client->ps.weaponTime <= 200 || NPCS.NPC->client->ps.torsoAnim == BOTH_KYLE_GRAB)
								&& !NPCS.NPC->client->ps.saberInFlight)
							{
								if (fabs(NPCS.NPC->enemy->r.currentOrigin[2] - NPCS.NPC->r.currentOrigin[2]) <= 8.0f)
								{
									//close to same level of ground
									if (DistanceSquared(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) <= 10000.0f)
									{
										return qtrue;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	else
	{
		if (NPCS.NPC->client->NPC_class == CLASS_KYLE && NPCS.NPC->spawnflags & 1)
		{
			//Boss Kyle
			if (NPCS.NPC->enemy && NPCS.NPC->enemy->client)
			{
				//have a valid enemy
				if (TIMER_Done(NPCS.NPC, "grabEnemyDebounce"))
				{
					//okay to grab again
					if (NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE
						&& NPCS.NPC->enemy->client->ps.groundEntityNum != ENTITYNUM_NONE)
					{
						//me and enemy are on ground
						if (!PM_InOnGroundAnim(NPCS.NPC->enemy->client->ps.legsAnim))
						{
							if ((NPCS.NPC->client->ps.weaponTime <= 200 || NPCS.NPC->client->ps.torsoAnim == BOTH_KYLE_GRAB)
								&& !NPCS.NPC->client->ps.saberInFlight)
							{
								if (fabs(NPCS.NPC->enemy->r.currentOrigin[2] - NPCS.NPC->r.currentOrigin[2]) <= 8.0f)
								{
									//close to same level of ground
									if (DistanceSquared(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) <= 10000.0f)
									{
										return qtrue;
									}
								}
							}
						}
					}
				}
			}
		}
	}
	return qfalse;
}


/*
===========================
NPC_HandleSlapMelee

Handles all slap/kick melee logic.
Returns:
	qtrue  = slap logic consumed the frame
	qfalse = continue normal combat logic
===========================
*/
static qboolean NPC_HandleSlapMelee(gentity_t* NPC, gentity_t* enemy, int enemyDist)
{
	playerState_t* ps = &NPC->client->ps;

	if (enemy == NULL)
	{
		return qfalse;
	}

	qboolean in_melee_range = (qboolean)
		((enemyDist <= MELEE_DIST_SQUARED) &&
			(ps->weaponTime == 0) &&
			(PM_InKnockDown(ps) == qfalse));

	qboolean enemy_in_front = (qboolean)
		InFront(enemy->r.currentOrigin, NPC->r.currentOrigin, ps->viewangles, 0.3f);

	qboolean enemy_behind = (qboolean)
		(!enemy_in_front &&
			!InFront(enemy->r.currentOrigin, NPC->r.currentOrigin, ps->viewangles, 0.25f));

	// ------------------------------------------------------------
	// 3. can_slap
	// ------------------------------------------------------------
	qboolean can_slap = (qboolean)
		(g_npcspskill.integer > 1 &&
			!jedi_in_camera &&
			npc_can_do_slap() &&
			in_melee_range &&
			!(NPCS.ucmd.buttons & (BUTTON_ATTACK |
				BUTTON_ALT_ATTACK |
				BUTTON_FORCEPOWER |
				BUTTON_USE |
				BUTTON_BLOCK |
				BUTTON_FORCE_LIGHTNING |
				BUTTON_FORCEGRIP)) &&
			(PM_InKnockDown(ps) == qfalse) &&
			(Q_irand(0, 3) == 0));

	if (can_slap == qfalse)
	{
		return qfalse;
	}

	// ------------------------------------------------------------
	// 4. Already in slap/kick animation? Handle hit timing
	// ------------------------------------------------------------
	int anim = ps->torsoAnim;

	qboolean in_slap_anim = (qboolean)
		(anim == BOTH_A7_SLAP_R ||
			anim == BOTH_A7_SLAP_L ||
			anim == BOTH_A7_KICK_B ||
			anim == BOTH_SWEEP_KICK ||
			anim == BOTH_A7_HILT);

	if (in_slap_anim == qtrue)
	{
		if (TIMER_Done(NPC, "smackTime") == qtrue &&
			NPCS.NPCInfo->blockedDebounceTime == 0)
		{
			if (in_melee_range == qtrue && enemy_in_front == qtrue)
			{
				vec3_t smack_dir;
				VectorSubtract(enemy->r.currentOrigin, NPC->r.currentOrigin, smack_dir);
				smack_dir[2] += 20;
				VectorNormalize(smack_dir);

				// Punch sound
				G_Sound(enemy, CHAN_AUTO, G_SoundIndex(va("sound/weapons/melee/punch%d", Q_irand(1, 4))));

				// Pain sound
				G_Sound(enemy, CHAN_AUTO, G_SoundIndex(va("sound/chars/%s/misc/pain0%d",
						enemy->NPC_type, Q_irand(1, 3))));

				G_Damage(enemy, NPC, NPC, smack_dir, NPC->r.currentOrigin,
					(g_npcspskill.integer + 1) * Q_irand(2, 5),
					DAMAGE_NO_KNOCKBACK, MOD_MELEE);

				WP_AbsorbKick(enemy, NPC, smack_dir);

				NPCS.NPCInfo->blockedDebounceTime = 1;
			}
		}

		return qtrue;
	}

	// ------------------------------------------------------------
	// 5. Not in slap anim — decide which melee attack to start
	// ------------------------------------------------------------
	if (in_melee_range == qfalse)
	{
		return qfalse;
	}

	// FRONT SLAP
	if (enemy_in_front == qtrue)
	{
		if (TIMER_Done(NPC, "attackDelay") == qtrue)
		{
			int swing_anim =
				(NPC->health > BLOCKPOINTS_HALF)
				? Q_irand(BOTH_A7_SLAP_L, BOTH_A7_SLAP_R)
				: BOTH_A7_HILT;

			G_Sound(enemy, CHAN_AUTO, G_SoundIndex(va("sound/weapons/melee/swing%d", Q_irand(1, 4))));
			G_AddVoiceEvent(NPC, Q_irand(EV_COMBAT1, EV_COMBAT3), 12000);

			NPC_SetAnim(NPC, SETANIM_BOTH, swing_anim,
				SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

			if (ps->fd.blockPoints > BLOCKPOINTS_THIRTY)
			{
				TIMER_Set(NPC, "attackDelay",
					ps->torsoTimer + Q_irand(20000, 30000));
			}
			else
			{
				TIMER_Set(NPC, "attackDelay",
					ps->torsoTimer + Q_irand(5000, 10000));
			}

			TIMER_Set(NPC, "smackTime", 300);
			NPCS.NPCInfo->blockedDebounceTime = 0;

			return qtrue;
		}
	}
	// BACK KICK
	else if (enemy_behind == qtrue)
	{
		if (TIMER_Done(NPC, "attackDelay") == qtrue)
		{
			int swing_anim = Q_irand(BOTH_A7_KICK_B, BOTH_SWEEP_KICK);

			G_Sound(enemy, CHAN_AUTO, G_SoundIndex(va("sound/weapons/melee/punch%d", Q_irand(1, 4))));
			G_AddVoiceEvent(NPC, Q_irand(EV_COMBAT1, EV_COMBAT3), 12000);

			NPC_SetAnim(NPC, SETANIM_BOTH, swing_anim,
				SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

			if (NPC->health > BLOCKPOINTS_HALF)
			{
				TIMER_Set(NPC, "attackDelay",
					ps->torsoTimer + Q_irand(15000, 20000));
			}
			else
			{
				TIMER_Set(NPC, "attackDelay",
					ps->torsoTimer + Q_irand(5000, 10000));
			}

			TIMER_Set(NPC, "smackTime", 300);
			NPCS.NPCInfo->blockedDebounceTime = 0;

			return qtrue;
		}
	}

	return qfalse;     // no slap action taken
}



static void Jedi_CombatDistance(const int enemy_dist)
{
	playerState_t* ps = &NPCS.NPC->client->ps;
	gentity_t* enemy = NPCS.NPC->enemy;

	// ------------------------------------------------------------
	// 1. Destroyer logic
	// ------------------------------------------------------------
	if (Jedi_CultistDestroyer(NPCS.NPC))
	{
		Jedi_Advance();
		ps->speed = NPCS.NPCInfo->stats.runSpeed;
		NPCS.ucmd.buttons &= ~BUTTON_WALKING;
		return;
	}

	// ------------------------------------------------------------
	// 2. Back off from spin attacks
	// ------------------------------------------------------------
	if (enemy_dist < 128 &&
		enemy &&
		enemy->client &&
		(enemy->client->ps.torsoAnim == BOTH_SPINATTACK6 ||
			enemy->client->ps.torsoAnim == BOTH_SPINATTACK7 ||
			enemy->client->ps.torsoAnim == BOTH_SPINATTACKGRIEVOUS))
	{
		if (Q_irand(-3, NPCS.NPCInfo->rank) > RANK_CREWMAN)
		{
			Jedi_StartBackOff();
			return;
		}
	}

	// ------------------------------------------------------------
	// 3. retreat from kata or saber throw return
	// ------------------------------------------------------------
	if (enemy_dist < 128 &&
		!jedi_in_camera &&
		enemy &&
		enemy->client &&
		ps->weapon == WP_SABER &&
		enemy->s.weapon == WP_SABER &&
		!PM_SaberInMassiveBounce(ps->torsoAnim) &&
		!PM_SaberInBounce(ps->torsoAnim) &&
		!PM_SaberInBashedAnim(ps->torsoAnim) &&
		!PM_InKnockDown(&NPCS.NPC->client->ps) &&
		InFront(enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, ps->viewangles, 0.7f) &&
		(PM_SaberInKata((saberMoveName_t)enemy->client->ps.saberMove) ||
			(ps->weapon == WP_SABER && (ps->saberInFlight || ps->saberEntityState == SES_RETURNING))))
	{
		// Reset dash count when cooldown expires
		if (TIMER_Done(NPCS.NPC, "DashOutTime"))
		{
			NPCS.NPC->Dash_NPC_Count = 0;
		}

		// Allow up to 2 dashes
		if (NPCS.NPC->Dash_NPC_Count < 2 && NPCS.NPC->client->ps.fd.forcePower >= 50)
		{
			NPCS.NPC->client->ps.fd.forcePower -= 35;
			JediDirectionalDashDodge(NPCS.NPC, enemy);
			NPCS.NPC->Dash_NPC_Count++;

			// If we just used the 2nd dash → start cooldown
			if (NPCS.NPC->Dash_NPC_Count == 2)
			{
				TIMER_Set(NPCS.NPC, "DashOutTime", Q_irand(5000, 10000));
			}

			return;
		}
		else
		{
			// No more dashes → retreat
			Jedi_StartBackOff();
			return;
		}
	}

	// ------------------------------------------------------------
	// 4. Regenerate block points (Serenity mode 2)
	// ------------------------------------------------------------
	if (enemy_dist > 256 &&
		!jedi_in_camera &&
		TIMER_Done(NPCS.NPC, "regenerate") &&
		NPC_IsAlive(NPCS.NPC, enemy) &&
		enemy &&
		enemy->client &&
		ps->weapon == WP_SABER &&
		enemy->s.weapon == WP_SABER &&
		ps->weaponTime <= 0 &&
		!Jedi_SaberBusy(NPCS.NPC))
	{
		if (ps->fd.blockPoints < BLOCKPOINTS_THIRTY)
		{
			AddNPCBlockPointBonus(NPCS.NPC);

			if (NPCS.NPC->client->NPC_class == CLASS_SITHLORD ||
				NPCS.NPC->client->NPC_class == CLASS_DESANN ||
				NPCS.NPC->client->NPC_class == CLASS_VADER ||
				NPCS.NPC->client->NPC_class == CLASS_LUKE)
			{
				NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, BOTH_FORCEHEAL_QUICK,
					SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			}
			else
			{
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_MEDITATE_SABER,
					SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			}

			TIMER_Set(NPCS.NPC, "regenerate", Q_irand(15000, 25000));
			ps->powerups[PW_MEDITATE] = level.time + ps->torsoTimer + 3000;
		}
	}

	// ------------------------------------------------------------
	// 5. Heal logic
	// ------------------------------------------------------------
	if (TIMER_Done(NPCS.NPC, "heal") &&
		enemy_dist > 256 &&
		!jedi_in_camera &&
		!Jedi_SaberBusy(NPCS.NPC) &&
		NPC_IsAlive(NPCS.NPC, enemy) &&
		ps->fd.forcePowerLevel[FP_HEAL] > 0 &&
		NPCS.NPC->health > 0 &&
		ps->weaponTime <= 0 &&
		ps->fd.forcePower >= 25 &&
		ps->fd.forcePowerDebounce[FP_HEAL] <= level.time &&
		WP_ForcePowerUsable(NPCS.NPC, FP_HEAL) &&
		NPCS.NPC->health <= ps->stats[STAT_MAX_HEALTH] / 3 &&
		NPCS.NPC->client->NPC_class != CLASS_OBJECT &&
		Q_irand(0, 2) == 2)
	{
		ForceHeal(NPCS.NPC);
		TIMER_Set(NPCS.NPC, "heal", Q_irand(10000, 15000));

		if (!enemy)
		{
			Jedi_AggressionErosion(-1);
		}
	}

	// ------------------------------------------------------------
	// 6. Saber users walk at close range
	// ------------------------------------------------------------
	if (!jedi_in_camera &&
		enemy &&
		enemy_dist < 100 &&
		NPC_IsAlive(NPCS.NPC, enemy) &&
		enemy->s.weapon == WP_SABER &&
		ps->weapon == WP_SABER &&
		InFront(enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, ps->viewangles, 0.9f))
	{
		if (enemy_dist < 60 ||
			(enemy_dist < 70 && ps->speed == NPCS.NPCInfo->stats.walkSpeed))
		{
			ps->speed = NPCS.NPCInfo->stats.walkSpeed;
			NPCS.ucmd.buttons |= BUTTON_WALKING;
		}
	}
	// ------------------------------------------------------------
	// 7. Saber style switching
	// ------------------------------------------------------------
	if (!jedi_in_camera &&
		enemy &&
		enemy_dist < 200 &&
		NPC_IsAlive(NPCS.NPC, enemy) &&
		enemy->s.weapon == WP_SABER &&
		ps->weapon == WP_SABER)
	{
		if (NPCS.NPC->saberPowerTime < level.time)
		{
			NPCS.NPC->saberPower = (qboolean)(Q_irand(1, 10) <= 5);
			NPCS.NPC->saberPowerTime = level.time + Q_irand(3000, 15000);
		}

		if (ps->fd.saberAnimLevel != SS_STAFF &&
			ps->fd.saberAnimLevel != SS_DUAL)
		{
			if (enemy->client->ps.fd.blockPoints > BLOCKPOINTS_MISSILE &&
				ps->fd.forcePowerLevel[FP_SABER_OFFENSE] > FORCE_LEVEL_2)
			{
				if (ps->fd.saberAnimLevel != SS_STRONG &&
					(ps->fd.saberAnimLevel != SS_DESANN && ps->fd.saberAnimLevel != SS_BLASTER) && // SS_BLASTER (2026-10-14)
					NPCS.NPC->saberPower)
				{
					Jedi_AdjustSaberAnimLevel(NPCS.NPC, Q_irand(SS_DESANN, SS_STRONG));
				}
			}
			else if (enemy->client->ps.fd.blockPoints > BLOCKPOINTS_FOURTY &&
				ps->fd.forcePowerLevel[FP_SABER_OFFENSE] > FORCE_LEVEL_1)
			{
				if (ps->fd.saberAnimLevel != SS_MEDIUM)
				{
					Jedi_AdjustSaberAnimLevel(NPCS.NPC, SS_MEDIUM);
				}
			}
			else
			{
				if (ps->fd.saberAnimLevel != SS_FAST &&
					ps->fd.saberAnimLevel != SS_TAVION)
				{
					Jedi_AdjustSaberAnimLevel(NPCS.NPC, Q_irand(SS_DESANN, SS_FAST));
				}
			}
		}
	}

	// -----------------------------------------
	// Slap/kick
	// -----------------------------------------
	if (NPC_HandleSlapMelee(NPCS.NPC, enemy, enemy_dist) == qtrue)
	{
		// Doing a slap/kick
		return;
	}

	// -----------------------------------------
	// Force power movement locks
	// -----------------------------------------

	if ((NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_GRIP)) &&
		NPCS.NPC->client->ps.fd.forcePowerLevel[FP_GRIP] > FORCE_LEVEL_1)
	{
		// When gripping, don't move — but DO NOT exit the function
		return;
	}

	if (!TIMER_Done(NPCS.NPC, "gripping"))
	{
		TIMER_Set(NPCS.NPC, "gripping", -level.time);
		TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(0, 1000));
	}

	if ((NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_GRASP)) &&
		NPCS.NPC->client->ps.fd.forcePowerLevel[FP_GRASP] > FORCE_LEVEL_1)
	{
		return;
	}

	if (!TIMER_Done(NPCS.NPC, "grasping"))
	{
		TIMER_Set(NPCS.NPC, "grasping", -level.time);
		TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(0, 1000));
	}

	if ((NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_DRAIN)) &&
		NPCS.NPC->client->ps.fd.forcePowerLevel[FP_DRAIN] > FORCE_LEVEL_1)
	{
		return;
	}

	if (!TIMER_Done(NPCS.NPC, "draining"))
	{
		TIMER_Set(NPCS.NPC, "draining", -level.time);
		TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(0, 1000));
	}

	if (NPCS.NPC->client->NPC_class == CLASS_BOBAFETT ||
		NPCS.NPC->client->NPC_class == CLASS_MANDO)
	{
		if (!TIMER_Done(NPCS.NPC, "flameTime"))
		{
			if (enemy_dist > 50)
			{
				Jedi_Advance();
			}
			else if (enemy_dist <= 0)
			{
				Jedi_Retreat();
			}
		}
		else if (enemy_dist < 200)
		{
			Jedi_Retreat();
		}
		else if (enemy_dist > 1024)
		{
			Jedi_Advance();
		}
		else
		{
			Jedi_Patrol();
		}
	}
	else if (NPCS.NPC->client->ps.legsAnim == BOTH_ALORA_SPIN_THROW_MD2)
	{
		//don't move at all when alora is doing her spin throw attack.
	}
	else if (NPCS.NPC->client->ps.legsAnim == BOTH_ALORA_SPIN_THROW)
	{
		// Don't move at all — but DO NOT exit the function
	}
	else if (NPCS.NPC->client->ps.torsoAnim == BOTH_KYLE_GRAB)
	{
		if (NPCS.NPC->client->ps.torsoTimer <= 200)
		{
			if (Kyle_CanDoGrab() &&
				NPC_EnemyRangeFromBolt(NPCS.NPC->client->renderInfo.handRBolt) <= 72.0f)
			{
				if (NPCS.NPCInfo->aiFlags & NPCAI_BOSS_CHARACTER)
				{
					NPC_GrabPlayer();
				}
				else
				{
					Kyle_GrabEnemy();
				}
				return;
			}

			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_KYLE_MISS,
				SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			NPCS.NPC->client->ps.weaponTime = NPCS.NPC->client->ps.torsoTimer;
			return;
		}

		// Just sit here
		return;
	}
	else if (NPCS.NPC->client->ps.saberInFlight &&
		!PM_SaberInBrokenParry(NPCS.NPC->client->ps.saberMove) &&
		NPCS.NPC->client->ps.saberBlocked != BLOCKED_PARRY_BROKEN)
	{
		if (enemy_dist < NPCS.NPC->client->ps.saberEntityDist)
		{
			Jedi_Retreat();
		}
		else if (enemy_dist > NPCS.NPC->client->ps.saberEntityDist &&
			enemy_dist > 100)
		{
			Jedi_Advance();
		}

		if (NPCS.NPC->client->ps.weapon == WP_SABER &&
			NPCS.NPC->client->ps.saberEntityState == SES_LEAVING &&
			NPCS.NPC->client->ps.fd.forcePowerLevel[FP_SABERTHROW] > FORCE_LEVEL_1 &&
			!(NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_SPEED)) &&
			!(NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_LIGHTNING)) &&
			!(NPCS.NPC->client->ps.saberEventFlags & SEF_INWATER))
		{
			NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
		}

		return;
	}
	else if (!TIMER_Done(NPCS.NPC, "taunting"))
	{
		if (enemy_dist <= 64)
		{
			NPCS.ucmd.buttons |= BUTTON_ATTACK;

			if (!NPCS.NPC->client->ps.saberInFlight)
			{
				WP_ActivateSaber(NPCS.NPC);
			}

			TIMER_Set(NPCS.NPC, "taunting", -level.time);
		}
		else if (NPCS.NPC->client->ps.torsoAnim == BOTH_GESTURE1 &&
			NPCS.NPC->client->ps.torsoTimer < 2000)
		{
			if (!NPCS.NPC->client->ps.saberInFlight)
			{
				WP_ActivateSaber(NPCS.NPC);
			}
		}

		return;
	}
	else if (NPCS.NPC->client->ps.saberEventFlags & SEF_LOCK_WON)
	{
		if (enemy_dist > 0)
		{
			Jedi_Advance();
		}

		if (enemy_dist > 128)
		{
			NPCS.NPC->client->ps.saberEventFlags &= ~SEF_LOCK_WON;
		}

		if (NPCS.NPC->enemy->painDebounceTime + 2000 < level.time)
		{
			NPCS.NPC->client->ps.saberEventFlags &= ~SEF_LOCK_WON;
		}

		TIMER_Set(NPCS.NPC, "strafeLeft", -1);
		TIMER_Set(NPCS.NPC, "strafeRight", -1);

		return;
	}
	else if (NPCS.NPC->enemy->client &&
		NPCS.NPC->enemy->s.weapon == WP_SABER &&
		NPCS.NPC->enemy->client->ps.saberLockTime > level.time &&
		NPCS.NPC->client->ps.saberLockTime < level.time)
	{
		if (enemy_dist < 64)
		{
			Jedi_Retreat();
		}

		return;
	}
	else if (NPCS.NPC->enemy->s.weapon == WP_TURRET &&
		!Q_stricmp("PAS", NPCS.NPC->enemy->classname) &&
		NPCS.NPC->enemy->s.apos.trType == TR_STATIONARY)
	{
		if (enemy_dist > forcePushPullRadius[FORCE_LEVEL_1] - 16)
		{
			Jedi_Advance();
		}

		int testlevel = (NPCS.NPC->client->ps.fd.forcePowerLevel[FP_PUSH] < FORCE_LEVEL_1) ? FORCE_LEVEL_1 : NPCS.NPC->client->ps.fd.forcePowerLevel[FP_PUSH];

		if (enemy_dist < forcePushPullRadius[testlevel] - 16)
		{
			if (InFront(NPCS.NPC->enemy->r.currentOrigin,
				NPCS.NPC->client->renderInfo.eyePoint,
				NPCS.NPC->client->renderInfo.eyeAngles,
				0.6f))
			{
				ForceThrow(NPCS.NPC, qfalse);
			}
		}

		return;
	}
	else if (enemy_dist <= 64
		&& (NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE
			|| !Q_stricmp("Yoda", NPCS.NPC->NPC_type)
			&& !Q_irand(0, 10)))
	{
		// can't use saber and they're in striking range
		if (!Q_irand(0, 5) &&
			InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin,
				NPCS.NPC->client->ps.viewangles, 0.2f))
		{
			if ((NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE ||
				NPCS.NPC->client->pers.maxHealth - NPCS.NPC->health > NPCS.NPC->client->pers.maxHealth * 0.25f)
				&& WP_ForcePowerUsable(NPCS.NPC, FP_DRAIN)
				&& !Q_irand(0, 2))
			{
				TIMER_Set(NPCS.NPC, "draining", 3000);
				TIMER_Set(NPCS.NPC, "attackDelay", 3000);
				Jedi_Advance();
				return;
			}

			if (Jedi_DecideKick())
			{
				if (G_PickAutoMultiKick(NPCS.NPC, qfalse, qtrue) != LS_NONE
					|| (G_CanKickEntity(NPCS.NPC, NPCS.NPC->enemy) &&
						G_PickAutoKick(NPCS.NPC, NPCS.NPC->enemy) != LS_NONE))
				{
					TIMER_Set(NPCS.NPC, "kickDebounce", Q_irand(8000, 15000));
					return;
				}
			}
			ForceThrow(NPCS.NPC, qfalse);
		}

		Jedi_Retreat();
	}
	else if (enemy_dist <= 64
		&& NPCS.NPC->client->pers.maxHealth - NPCS.NPC->health > NPCS.NPC->client->pers.maxHealth * 0.25f
		&& (NPCS.NPC->client->ps.fd.forcePowersKnown & (1 << FP_DRAIN))
		&& WP_ForcePowerAvailable(NPCS.NPC, FP_DRAIN, 20)
		&& !Q_irand(0, 10)
		&& InFront(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin,
			NPCS.NPC->client->ps.viewangles, 0.2f))
	{
		TIMER_Set(NPCS.NPC, "draining", 3000);
		TIMER_Set(NPCS.NPC, "attackDelay", 3000);
		Jedi_Advance();
		return;
	}

	else if (enemy_dist <= -16)
	{
		if (!Q_irand(0, 30) && Kyle_CanDoGrab())
		{
			Kyle_TryGrab();
		}

		if ((NPCS.NPC->client->NPC_class == CLASS_TAVION && NPCS.NPC->client->weaponGhoul2[1] != NULL) && !Q_irand(0, 20))
		{
			Tavion_StartScepterSlam();
		}

		if (Jedi_DecideKick())
		{
			if (G_PickAutoMultiKick(NPCS.NPC, qfalse, qtrue) != LS_NONE
				|| (G_CanKickEntity(NPCS.NPC, NPCS.NPC->enemy) &&
					G_PickAutoKick(NPCS.NPC, NPCS.NPC->enemy) != LS_NONE))
			{
				TIMER_Set(NPCS.NPC, "kickDebounce", Q_irand(8000, 15000));
			}
		}

		Jedi_Retreat();
	}
	else if (enemy_dist <= 0)
	{
		if (NPCS.NPCInfo->stats.aggression < 4)
		{
			if (!Q_irand(0, 30) && Kyle_CanDoGrab())
			{
				Kyle_TryGrab();
			}

			if ((NPCS.NPC->client->NPC_class == CLASS_TAVION && NPCS.NPC->client->weaponGhoul2[1] != NULL) && !Q_irand(0, 20))
			{
				Tavion_StartScepterSlam();
			}

			if (Jedi_DecideKick())
			{
				if (G_PickAutoMultiKick(NPCS.NPC, qfalse, qtrue) != LS_NONE
					|| (G_CanKickEntity(NPCS.NPC, NPCS.NPC->enemy) &&
						G_PickAutoKick(NPCS.NPC, NPCS.NPC->enemy) != LS_NONE))
				{
					TIMER_Set(NPCS.NPC, "kickDebounce", Q_irand(8000, 15000));
				}
			}

			Jedi_Retreat();
		}
	}
	else if (enemy_dist > 256)
	{
		qboolean used_force = qfalse;

		if (NPCS.NPCInfo->stats.aggression < Q_irand(0, 20)
			&& NPCS.NPC->health < NPCS.NPC->client->pers.maxHealth * 0.75f
			&& !Q_irand(0, 2))
		{
			if (NPCS.NPC->enemy &&
				NPCS.NPC->enemy->s.number < MAX_CLIENTS &&
				NPCS.NPC->client->NPC_class != CLASS_KYLE &&
				(NPCS.NPCInfo->aiFlags & NPCAI_BOSS_CHARACTER ||
					NPCS.NPC->client->NPC_class == CLASS_SHADOWTROOPER) &&
				Q_irand(0, 3 - g_npcspskill.integer))
			{
				// bosses do this less
			}
			else if (NPCS.NPC->client->saber[0].type == SABER_SITH_SWORD &&
				NPCS.NPC->client->weaponGhoul2[0] != NULL)
			{
				Tavion_SithSwordRecharge();
				used_force = qtrue;
			}
			else if ((NPCS.NPC->client->ps.fd.forcePowersKnown & (1 << FP_HEAL)) &&
				!(NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_HEAL)) &&
				Q_irand(0, 1))
			{
				ForceHeal(NPCS.NPC);
				used_force = qtrue;

				if (NPC_IsJedi(NPCS.NPC))
				{
					G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_GLOAT1, EV_GLOAT3), 5000 + Q_irand(0, 15000));
				}
			}
			else if ((ps->fd.forcePowersKnown & 1 << FP_TELEPATHY) != 0
				&& (ps->fd.forcePowersActive & 1 << FP_TELEPATHY) == 0
				&& !Q_irand(0, 2))
			{
				ForceTelepathy(NPCS.NPC);
				used_force = qtrue;
			}
			else if ((NPCS.NPC->client->ps.fd.forcePowersKnown & (1 << FP_PROTECT)) &&
				!(NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_PROTECT)) &&
				Q_irand(0, 1))
			{
				ForceProtect(NPCS.NPC);
				used_force = qtrue;
			}
			else if ((NPCS.NPC->client->ps.fd.forcePowersKnown & (1 << FP_ABSORB)) &&
				!(NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_ABSORB)) &&
				Q_irand(0, 1))
			{
				ForceAbsorb(NPCS.NPC);
				used_force = qtrue;
			}
			else if ((NPCS.NPC->client->ps.fd.forcePowersKnown & (1 << FP_RAGE)) &&
				!(NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_RAGE)) &&
				Q_irand(0, 1))
			{
				Jedi_Rage();
				used_force = qtrue;
			}
		}
		if (enemy_dist > 384)
		{
			if (!Q_irand(0, 10) &&
				NPCS.NPCInfo->blockedSpeechDebounceTime < level.time &&
				jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] < level.time)
			{
				if (NPC_ClearLOS4(NPCS.NPC->enemy))
				{
					G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_JCHASE1, EV_JCHASE3), 3000);
				}

				jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] =
					NPCS.NPCInfo->blockedSpeechDebounceTime = level.time + 3000;
			}
		}

		if (NPCS.NPCInfo->stats.aggression > 0)
		{
			if (!used_force)
			{
				if (NPCS.NPC->enemy && NPCS.NPC->enemy->client &&
					(NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_SPINATTACK6 ||
						NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_SPINATTACK7))
				{
					// stay put
				}
				else
				{
					Jedi_Advance();
				}
			}
		}
	}
	else if (enemy_dist > 50)
	{
		if (enemy != NULL &&
			enemy->client != NULL &&
			(((enemy->client->ps.fd.forceGripBeingGripped > level.time)) != 0 ||
				(jedi_being_grasped(enemy)) != 0 ||
				(jedi_being_drained(enemy)) != 0))
		{
			//They're being gripped, rush them!
			if (NPCS.NPC->enemy->client->ps.groundEntityNum != ENTITYNUM_NONE)
			{
				//they're on the ground, so advance
				if (TIMER_Done(NPCS.NPC, "parryTime") || NPCS.NPCInfo->rank > RANK_LT)
				{
					//not parrying
					if (enemy_dist > 200 || !(NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE))
					{
						//far away or allowed to use saber
						Jedi_Advance();
					}
				}
			}
			if (enemy_dist > 128 && (NPCS.NPCInfo->rank >= RANK_LT_JG || WP_ForcePowerUsable(NPCS.NPC, FP_SABERTHROW))
				&& !Q_irand(0, 5)
				&& !(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_SPEED)
				&& !(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_LIGHTNING)
				&& !(NPCS.NPC->client->ps.saberEventFlags & SEF_INWATER)) //saber not in water
			{
				//throw saber
				NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
			}
		}
		else if (NPCS.NPC->enemy && NPCS.NPC->enemy->client && jedi_being_grasped(NPCS.NPC->enemy))
		{
			//They're being gripped, rush them!
			if (NPCS.NPC->enemy->client->ps.groundEntityNum != ENTITYNUM_NONE)
			{
				//they're on the ground, so advance
				if (TIMER_Done(NPCS.NPC, "parryTime") || NPCS.NPCInfo->rank > RANK_LT)
				{
					//not parrying
					if (enemy_dist > 200 || !(NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE))
					{
						//far away or allowed to use saber
						Jedi_Advance();
					}
				}
			}
			if (enemy_dist > 128 &&
				(NPCS.NPCInfo->rank >= RANK_LT_JG || WP_ForcePowerUsable(NPCS.NPC, FP_SABERTHROW))
				&& !Q_irand(0, 5)
				&& !(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_SPEED)
				&& !(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_LIGHTNING)
				&& !(NPCS.NPC->client->ps.saberEventFlags & SEF_INWATER)) //saber not in water
			{
				//throw saber
				NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
			}
		}
		else if (NPCS.NPC->enemy && NPCS.NPC->enemy->client && //valid enemy
			NPCS.NPC->enemy->client->ps.saberInFlight && (NPCS.NPC->enemy->client->ps.saberHolstered < 2) && //enemy throwing saber
			!NPCS.NPC->client->ps.weaponTime && //I'm not busy
			WP_ForcePowerAvailable(NPCS.NPC, FP_GRIP, 0) && //I can use the power
			!Q_irand(0, 10) && //don't do it all the time, averages to 1 check a second
			Q_irand(0, 6) < g_npcspskill.integer && //more likely on harder diff
			Q_irand(RANK_CIVILIAN, RANK_CAPTAIN) < NPCS.NPCInfo->rank //more likely against harder enemies
			&& InFOV3(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 20, 30))
		{
			//They're throwing their saber, grip them!
			//taunt
			if (TIMER_Done(NPCS.NPC, "chatter") && jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] < level.time && NPCS.NPCInfo->
				blockedSpeechDebounceTime < level.time)
			{
				G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_TAUNT1, EV_TAUNT3), 13000);
				jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] = NPCS.NPCInfo->blockedSpeechDebounceTime = level.time +
					3000;
				if (NPCS.NPCInfo->aiFlags & NPCAI_ROSH)
				{
					TIMER_Set(NPCS.NPC, "chatter", 16000);
				}
				else
				{
					TIMER_Set(NPCS.NPC, "chatter", 13000);
				}
			}

			//grip
			TIMER_Set(NPCS.NPC, "gripping", 3000);
			TIMER_Set(NPCS.NPC, "attackDelay", 3000);
		}
		else if (NPCS.NPC->enemy && NPCS.NPC->enemy->client && //valid enemy
			NPCS.NPC->enemy->client->ps.saberInFlight && (NPCS.NPC->enemy->client->ps.saberHolstered < 2) && //enemy throwing saber
			!NPCS.NPC->client->ps.weaponTime && //I'm not busy
			WP_ForcePowerAvailable(NPCS.NPC, FP_GRASP, 0) && //I can use the power
			!Q_irand(0, 10) && //don't do it all the time, averages to 1 check a second
			Q_irand(0, 6) < g_npcspskill.integer && //more likely on harder diff
			Q_irand(RANK_CIVILIAN, RANK_CAPTAIN) < NPCS.NPCInfo->rank //more likely against harder enemies
			&& InFOV3(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 20, 30))
		{
			//They're throwing their saber, grip them!
			//taunt
			if (TIMER_Done(NPCS.NPC, "chatter") && jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] < level.time && NPCS.NPCInfo->
				blockedSpeechDebounceTime < level.time)
			{
				G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_TAUNT1, EV_TAUNT3), 13000);
				jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] = NPCS.NPCInfo->blockedSpeechDebounceTime = level.time +
					3000;
				if (NPCS.NPCInfo->aiFlags & NPCAI_ROSH)
				{
					TIMER_Set(NPCS.NPC, "chatter", 16000);
				}
				else
				{
					TIMER_Set(NPCS.NPC, "chatter", 13000);
				}
			}

			//grip
			TIMER_Set(NPCS.NPC, "grasping", 3000);
			TIMER_Set(NPCS.NPC, "attackDelay", 3000);
		}
		else
		{
			if (enemy && enemy->client &&
				(enemy->client->ps.fd.forcePowersActive & 1 << FP_GRIP ||
					enemy->client->ps.fd.forcePowersActive & 1 << FP_GRASP))
			{
				//They're choking someone, probably an ally, run at them and do some sort of attack
				if (NPCS.NPC->enemy->client->ps.groundEntityNum != ENTITYNUM_NONE)
				{
					//they're on the ground, so advance
					if (TIMER_Done(NPCS.NPC, "parryTime") || NPCS.NPCInfo->rank > RANK_LT)
					{
						//not parrying
						if (enemy_dist > 200 || !(NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE))
						{
							//far away or allowed to use saber
							Jedi_Advance();
						}
					}
				}
			}
			else if (NPCS.NPC->enemy && NPCS.NPC->enemy->client && NPCS.NPC->enemy->client->ps.fd.forcePowersActive & 1 << FP_GRASP)
			{
				//They're choking someone, probably an ally, run at them and do some sort of attack
				if (NPCS.NPC->enemy->client->ps.groundEntityNum != ENTITYNUM_NONE)
				{
					//they're on the ground, so advance
					if (TIMER_Done(NPCS.NPC, "parryTime") || NPCS.NPCInfo->rank > RANK_LT)
					{
						//not parrying
						if (enemy_dist > 200 || !(NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE))
						{
							//far away or allowed to use saber
							Jedi_Advance();
						}
					}
				}
			}

			if ((NPCS.NPC->client->NPC_class == CLASS_KYLE || NPCS.NPC->client->NPC_class == CLASS_YODA)
				&& NPCS.NPC->spawnflags & 1
				&& (enemy && enemy->client && !enemy->client->ps.saberInFlight)
				&& TIMER_Done(NPCS.NPC, "kyleTakesSaber")
				&& !Q_irand(0, 20))
			{
				ForceThrow(NPCS.NPC, qtrue);
			}
			else if ((NPCS.NPC->client->NPC_class == CLASS_TAVION && NPCS.NPC->client->weaponGhoul2[1] != NULL) && !Q_irand(0, 20))
			{
				Tavion_StartScepterBeam();
				return;
			}
			else
			{
				int chance_scale = 0;
				if ((NPCS.NPC->client->NPC_class == CLASS_KYLE
					|| NPCS.NPC->client->NPC_class == CLASS_YODA) && NPCS.NPC->spawnflags & 1)
				{
					chance_scale = 4;
				}
				else if (enemy
					&& enemy->s.number < MAX_CLIENTS
					&& (NPCS.NPCInfo->aiFlags & NPCAI_BOSS_CHARACTER
						|| NPCS.NPC->client->NPC_class == CLASS_SHADOWTROOPER))
				{
					chance_scale = 8 - g_npcspskill.integer * 2;
				}
				else if (NPCS.NPC->client->NPC_class == CLASS_DESANN)
				{
					chance_scale = 1;
				}
				else if (NPCS.NPC->client->NPC_class == CLASS_VADER)
				{
					chance_scale = 1;
				}
				else if (NPCS.NPCInfo->rank == RANK_ENSIGN)
				{
					chance_scale = 2;
				}
				else if (NPCS.NPCInfo->rank >= RANK_LT_JG)
				{
					chance_scale = 5;
				}
				if (chance_scale
					&& (enemy_dist > Q_irand(100, 200)
						|| (NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE)
						|| (!Q_stricmp("Yoda", NPCS.NPC->NPC_type) && !Q_irand(0, 3)))
					&& enemy_dist < 500
					&& ((Q_irand(0, chance_scale * 10) < 5)
						|| (NPCS.NPC->enemy
							&& NPCS.NPC->enemy->client
							&& NPCS.NPC->enemy->client->ps.weapon != WP_SABER
							&& !Q_irand(0, chance_scale))))
				{
					//else, randomly try some kind of attack every now and then
					if ((NPCS.NPCInfo->rank == RANK_ENSIGN //old reborn crap
						|| NPCS.NPCInfo->rank > RANK_LT_JG) //old reborn crap
						&& (!Q_irand(0, 1) || NPCS.NPC->s.weapon != WP_SABER))
					{
						if (WP_ForcePowerUsable(NPCS.NPC, FP_PULL) && !Q_irand(0, 2))
						{
							ForceThrow(NPCS.NPC, qtrue);
							//maybe strafe too?
							TIMER_Set(NPCS.NPC, "duck", enemy_dist * 3);
							if (Q_irand(0, 1))
							{
								NPCS.ucmd.buttons |= BUTTON_ATTACK;
							}
						} /////////  lightning ////////////////////////////
						else if (WP_ForcePowerUsable(NPCS.NPC, FP_LIGHTNING)
							&& (NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE &&
								npc_is_sith_lord(NPCS.NPC) ||
								// these always use lightning, others half the time ("== 0" was missing: the chain was always qtrue)
								(Q_stricmp("md_snoke_cin", NPCS.NPC->NPC_type) == 0
									|| Q_stricmp("md_snoke", NPCS.NPC->NPC_type) == 0
									|| Q_stricmp("md_palpatine", NPCS.NPC->NPC_type) == 0
									|| Q_stricmp("md_mother_talzin", NPCS.NPC->NPC_type) == 0
									|| Q_stricmp("md_sidious_ep2", NPCS.NPC->NPC_type) == 0
									|| Q_stricmp("md_sidious", NPCS.NPC->NPC_type) == 0
									|| Q_stricmp("md_sidious_ep3_red", NPCS.NPC->NPC_type) == 0
									|| Q_stricmp("md_pal_mof", NPCS.NPC->NPC_type) == 0
									|| Q_stricmp("md_emperor", NPCS.NPC->NPC_type) == 0
									|| Q_stricmp("md_emperor_fas", NPCS.NPC->NPC_type) == 0
									|| Q_stricmp("md_emperor_ros", NPCS.NPC->NPC_type) == 0
									|| Q_stricmp("md_emperor_ros_blind", NPCS.NPC->NPC_type) == 0
									|| Q_stricmp("cultist_lightning", NPCS.NPC->NPC_type) == 0) ||
								Q_irand(0, 1)))
						{
							ForceLightning(NPCS.NPC);
							if (NPCS.NPC->client->ps.fd.forcePowerLevel[FP_LIGHTNING] > FORCE_LEVEL_1)
							{
								NPCS.NPC->client->ps.weaponTime = Q_irand(1000, 3000 + g_npcspskill.integer * 500);
								TIMER_Set(NPCS.NPC, "holdLightning", NPCS.NPC->client->ps.weaponTime);
							}
							TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.weaponTime);
						} ///////////// drain /////////////////////////
						else if (NPCS.NPC->health < NPCS.NPC->client->pers.maxHealth * 0.75f
							&& Q_irand(FORCE_LEVEL_0, NPCS.NPC->client->ps.fd.forcePowerLevel[FP_DRAIN]) > FORCE_LEVEL_1
							&& WP_ForcePowerUsable(NPCS.NPC, FP_DRAIN)
							&& (NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE && Q_stricmp("cultist_drain", NPCS.NPC->NPC_type) ||
								Q_irand(0, 1)))
						{
							ForceDrain(NPCS.NPC);
							NPCS.NPC->client->ps.weaponTime = Q_irand(1000, 3000 + g_npcspskill.integer * 500);
							TIMER_Set(NPCS.NPC, "draining", NPCS.NPC->client->ps.weaponTime);
							TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.weaponTime);
						}
						////////////////////  New ForcePowers  ///////////////////////////

						////////////////////  FP_FEAR  //////////////////////////////////////
						else if (WP_ForcePowerUsable(NPCS.NPC, FP_FEAR) && Q_irand(0, 2)
							&& NPCS.NPC->enemy && NPCS.NPC->enemy->client && NPCS.NPC->enemy->client->frozenTime < level.time)
						{
							ForceFear(NPCS.NPC);
							TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.weaponTime);
						}

						/////////////////////  FP_LIGHTNING_STRIKE ////////////////////////////////
						else if (WP_ForcePowerUsable(NPCS.NPC, FP_LIGHTNING_STRIKE)
							&& Q_irand(0, 1) && NPCS.NPC->enemy && NPCS.NPC->enemy->client && NPCS.NPC->enemy->client->frozenTime <
							level.time)
						{
							ForceLightningStrike(NPCS.NPC);
							TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.weaponTime);
						}

						//////////////////////  FP_DESTRUCTION  ////////////////////////////////////
						else if (WP_ForcePowerUsable(NPCS.NPC, FP_DESTRUCTION)
							&& Q_irand(0, 1) && NPCS.NPC->enemy && NPCS.NPC->enemy->client && NPCS.NPC->enemy->client->frozenTime <
							level.time)
						{
							ForceDestruction(NPCS.NPC);
							TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.weaponTime);
						}

						//////////////////////  FP_STASIS  ////////////////////////////////////
						else if (WP_ForcePowerUsable(NPCS.NPC, FP_STASIS) && Q_irand(0, 1) && NPCS.NPC->enemy && NPCS.NPC->enemy->
							client && NPCS.NPC->enemy->client->frozenTime < level.time)
						{
							ForceStasis(NPCS.NPC);
							TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.weaponTime);
						}

						//////////////////////  FP_STASIS  ////////////////////////////////////
						else if (WP_ForcePowerUsable(NPCS.NPC, FP_STASIS) && Q_irand(0, 1) && NPCS.NPC->enemy && NPCS.NPC->enemy->
							client && NPCS.NPC->enemy->client->frozenTime < level.time)
						{
							ForceStasis(NPCS.NPC);
							TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.weaponTime);
						}

						//////////////////////  FP_GRASP  ////////////////////////////////////
						else if (WP_ForcePowerUsable(NPCS.NPC, FP_GRASP)
							&& NPCS.NPC->enemy && InFOV3(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 20, 30)
							&& NPCS.NPCInfo->stats.aggression > Q_irand(5, 15)
							&& NPCS.NPC->health < NPCS.NPC->client->pers.maxHealth * 0.75f
							&& !Q_irand(0, 2))
						{
							//taunt
							if (TIMER_Done(NPCS.NPC, "chatter") && jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] < level.
								time && NPCS.NPCInfo->blockedSpeechDebounceTime < level.time)
							{
								G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_TAUNT1, EV_TAUNT3), 13000);
								jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] = NPCS.NPCInfo->blockedSpeechDebounceTime =
									level.time + 3000;
								if (NPCS.NPCInfo->aiFlags & NPCAI_ROSH)
								{
									TIMER_Set(NPCS.NPC, "chatter", 16000);
								}
								else
								{
									TIMER_Set(NPCS.NPC, "chatter", 13000);
								}
							}

							//grip
							TIMER_Set(NPCS.NPC, "grasping", 3000);
							TIMER_Set(NPCS.NPC, "attackDelay", 3000);
						}
						////////////////////  New ForcePowers end ///////////////////////////

						//////////////// FP_GRIP /////////////////////////

						else if (WP_ForcePowerUsable(NPCS.NPC, FP_GRIP)
							&& NPCS.NPC->enemy && InFOV3(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 20, 30))
						{
							//taunt
							if (TIMER_Done(NPCS.NPC, "chatter") && jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] < level.
								time && NPCS.NPCInfo->blockedSpeechDebounceTime < level.time)
							{
								G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_TAUNT1, EV_TAUNT3), 13000);
								jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] = NPCS.NPCInfo->blockedSpeechDebounceTime =
									level.time + 3000;
								if (NPCS.NPCInfo->aiFlags & NPCAI_ROSH)
								{
									TIMER_Set(NPCS.NPC, "chatter", 16000);
								}
								else
								{
									TIMER_Set(NPCS.NPC, "chatter", 13000);
								}
							}

							//grip
							TIMER_Set(NPCS.NPC, "gripping", 3000);
							TIMER_Set(NPCS.NPC, "attackDelay", 3000);
						}
						else
						{
							if (!npc_should_not_throw_saber(NPCS.NPC) &&
								NPCS.NPC->client->ps.fd.forcePower > BLOCKPOINTS_KNOCKAWAY &&
								enemy_dist > 128 && WP_ForcePowerUsable(NPCS.NPC, FP_SABERTHROW)
								&& !(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_SPEED)
								&& !(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_LIGHTNING)
								&& !(NPCS.NPC->client->ps.saberEventFlags & SEF_INWATER)) //saber not in water
							{
								//throw saber
								NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
							}
						}
					}
					else
					{
						if (!npc_should_not_throw_saber(NPCS.NPC) &&
							NPCS.NPC->client->ps.fd.forcePower > BLOCKPOINTS_KNOCKAWAY &&
							enemy_dist > 128 && (NPCS.NPCInfo->rank >= RANK_LT_JG
								|| WP_ForcePowerUsable(NPCS.NPC, FP_SABERTHROW))
							&& !(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_SPEED)
							&& !(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_LIGHTNING)
							&& !(NPCS.NPC->client->ps.saberEventFlags & SEF_INWATER)) //saber not in water
						{
							//throw saber
							NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
						}
					}
				}
				//see if we should advance now
				else if (NPCS.NPCInfo->stats.aggression > 5)
				{
					//approach enemy
					if (TIMER_Done(NPCS.NPC, "parryTime") || NPCS.NPCInfo->rank > RANK_LT)
					{
						//not parrying
						if (!NPCS.NPC->enemy || !NPCS.NPC->enemy->client || NPCS.NPC->enemy->client->ps.groundEntityNum != ENTITYNUM_NONE)
						{
							//they're on the ground, so advance
							if (enemy_dist > 200 || !(NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE))
							{
								//far away or allowed to use saber
								Jedi_Advance();
							}
						}
					}
				}
				else
				{
					//maintain this distance?
					//walk?
				}
			}
		}
	}
	else
	{
		if (!Q_irand(0, 30) && Kyle_CanDoGrab())
		{
			Kyle_TryGrab();
			return;
		}

		if (NPCS.NPCInfo->stats.aggression < 4)
		{
			if (Jedi_DecideKick())
			{
				if (G_PickAutoMultiKick(NPCS.NPC, qfalse, qtrue) != LS_NONE
					|| (G_CanKickEntity(NPCS.NPC, NPCS.NPC->enemy) &&
						G_PickAutoKick(NPCS.NPC, NPCS.NPC->enemy) != LS_NONE))
				{
					TIMER_Set(NPCS.NPC, "kickDebounce", Q_irand(8000, 15000));
					return;
				}
			}

			Jedi_Retreat();
		}
		else if (NPCS.NPCInfo->stats.aggression > 5)
		{
			if (enemy_dist > 0 && !(NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE))
			{
				if (TIMER_Done(NPCS.NPC, "parryTime") || NPCS.NPCInfo->rank > RANK_LT)
				{
					if (!NPCS.NPC->enemy->client ||
						NPCS.NPC->enemy->client->ps.groundEntityNum != ENTITYNUM_NONE)
					{
						Jedi_Advance();
					}
				}
			}
		}
	}

	// really mad → rage
	if (enemy_dist > 1024 &&
		NPCS.NPCInfo->stats.aggression > Q_irand(5, 15) &&
		NPCS.NPC->health < NPCS.NPC->client->pers.maxHealth * 0.75f &&
		!Q_irand(0, 2))
	{
		if ((NPCS.NPC->client->ps.fd.forcePowersKnown & (1 << FP_RAGE)) &&
			!(NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_RAGE)))
		{
			Jedi_Rage();
		}
	}
}



static qboolean Jedi_Strafe(const int strafe_time_min, const int strafe_time_max, const int next_strafe_time_min,
	const int next_strafe_time_max, const qboolean walking)
{
	if (Jedi_CultistDestroyer(NPCS.NPC))
	{
		//never strafe
		return qfalse;
	}
	if (NPCS.NPC->client->ps.saberEventFlags & SEF_LOCK_WON && NPCS.NPC->enemy && NPCS.NPC->enemy->painDebounceTime > level.time)
	{
		//don't strafe if pressing the advantage of winning a saberLock
		return qfalse;
	}
	if (TIMER_Done(NPCS.NPC, "strafeLeft") && TIMER_Done(NPCS.NPC, "strafeRight"))
	{
		qboolean strafed = qfalse;

		const int strafe_time = Q_irand(strafe_time_min, strafe_time_max);

		if (Q_irand(0, 1))
		{
			if (NPC_MoveDirClear(NPCS.ucmd.forwardmove, -127, qfalse))
			{
				TIMER_Set(NPCS.NPC, "strafeLeft", strafe_time);
				strafed = qtrue;
			}
			else if (NPC_MoveDirClear(NPCS.ucmd.forwardmove, 127, qfalse))
			{
				TIMER_Set(NPCS.NPC, "strafeRight", strafe_time);
				strafed = qtrue;
			}
		}
		else
		{
			if (NPC_MoveDirClear(NPCS.ucmd.forwardmove, 127, qfalse))
			{
				TIMER_Set(NPCS.NPC, "strafeRight", strafe_time);
				strafed = qtrue;
			}
			else if (NPC_MoveDirClear(NPCS.ucmd.forwardmove, -127, qfalse))
			{
				TIMER_Set(NPCS.NPC, "strafeLeft", strafe_time);
				strafed = qtrue;
			}
		}

		if (strafed)
		{
			TIMER_Set(NPCS.NPC, "noStrafe", strafe_time + Q_irand(next_strafe_time_min, next_strafe_time_max));
			if (walking)
			{
				//should be a slow strafe
				TIMER_Set(NPCS.NPC, "walking", strafe_time);
			}
			return qtrue;
		}
	}
	return qfalse;
}



static evasionType_t Jedi_CheckFlipEvasions(gentity_t* self, const float rightdot)
{
	if (!self)
	{
		return EVASION_NONE;
	}

	// No acrobatics flag
	if (self->NPC && (self->NPC->scriptFlags & SCF_NO_ACROBATICS))
	{
		return EVASION_NONE;
	}

	if (self->client)
	{
		// Bounty hunters / mandos can't flip
		if (self->client->NPC_class == CLASS_BOBAFETT ||
			self->client->NPC_class == CLASS_MANDO)
		{
			return EVASION_NONE;
		}

		// No fancy dodges when raging
		if (self->client->ps.fd.forceRageRecoveryTime > level.time ||
			(self->client->ps.fd.forcePowersActive & (1 << FP_RAGE)))
		{
			return EVASION_NONE;
		}
	}

	// Wall-run flip off the wall if already wall-running
	if (self->client &&
		(self->client->ps.legsAnim == BOTH_WALL_RUN_LEFT ||
			self->client->ps.legsAnim == BOTH_WALL_RUN_RIGHT))
	{
		vec3_t right;
		const vec3_t fwdAngles = { 0, self->client->ps.viewangles[YAW], 0 };
		int anim = -1;

		AngleVectors(fwdAngles, NULL, right, NULL);

		const float animLength = BG_AnimLength(self->localAnimIndex, (animNumber_t)(self->client->ps.legsAnim));

		if (self->client->ps.legsAnim == BOTH_WALL_RUN_LEFT && rightdot < 0.0f)
		{
			// running on a wall to the left, attack from the left
			if (animLength - self->client->ps.legsTimer > 400 &&
				self->client->ps.legsTimer > 400)
			{
				anim = BOTH_WALL_RUN_LEFT_FLIP;
			}
		}
		else if (self->client->ps.legsAnim == BOTH_WALL_RUN_RIGHT && rightdot > 0.0f)
		{
			// running on a wall to the right, attack from the right
			if (animLength - self->client->ps.legsTimer > 400 &&
				self->client->ps.legsTimer > 400)
			{
				anim = BOTH_WALL_RUN_RIGHT_FLIP;
			}
		}

		if (anim != -1)
		{
			// flip off the wall
			if (anim == BOTH_WALL_RUN_LEFT_FLIP)
			{
				self->client->ps.velocity[0] *= 0.5f;
				self->client->ps.velocity[1] *= 0.5f;
				VectorMA(self->client->ps.velocity, 150.0f, right, self->client->ps.velocity);
			}
			else if (anim == BOTH_WALL_RUN_RIGHT_FLIP)
			{
				self->client->ps.velocity[0] *= 0.5f;
				self->client->ps.velocity[1] *= 0.5f;
				VectorMA(self->client->ps.velocity, -150.0f, right, self->client->ps.velocity);
			}

			int parts = SETANIM_LEGS;
			if (!self->client->ps.weaponTime)
			{
				parts = SETANIM_BOTH;
			}

			NPC_SetAnim(self, parts, anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			self->client->ps.pm_flags |= PMF_JUMPING | PMF_SLOW_MO_FALL;
			G_AddEvent(self, EV_JUMP, 0);

			return EVASION_OTHER;
		}
	}
	// Cartwheels / wall-runs / wall-flips
	else if (self->client &&
		self->NPC && // needed for self->NPC->rank below
		self->client->NPC_class != CLASS_DESANN &&
		self->client->NPC_class != CLASS_VADER &&
		Q_irand(0, 1) &&
		!PM_InRoll(&self->client->ps) &&
		!PM_InKnockDown(&self->client->ps) &&
		!PM_SaberInSpecialAttack(self->client->ps.torsoAnim) &&
		(self->NPC->rank == RANK_CREWMAN || self->NPC->rank >= RANK_LT))
	{
		vec3_t fwd, right, traceto;
		const vec3_t fwdAngles = { 0, self->client->ps.viewangles[YAW], 0 };
		const vec3_t maxs = { self->r.maxs[0], self->r.maxs[1], 24.0f };
		const vec3_t mins = { self->r.mins[0], self->r.mins[1], self->r.mins[2] + STEPSIZE };
		trace_t trace;

		AngleVectors(fwdAngles, fwd, right, NULL);

		int parts = SETANIM_BOTH;
		int anim = 0;
		float speed = 0.0f;
		float check_dist = 0.0f;
		qboolean allow_cart_wheels = qtrue;

		if (self->client->ps.weapon == WP_SABER)
		{
			if (self->client->saber[0].saberFlags & SFL_NO_CARTWHEELS)
			{
				allow_cart_wheels = qfalse;
			}
			else if ((self->client->saber[1].model[0] != 0) &&
				(self->client->saber[1].saberFlags & SFL_NO_CARTWHEELS))
			{
				allow_cart_wheels = qfalse;
			}
		}
		if (self->client->ps.fd.forcePowerLevel[FP_LEVITATION] < FORCE_LEVEL_1 || self->NPC->stats.move < 4)
		{
			//can't do no-handed cartwheels without a bit of force jumping ability or teh moves
			allow_cart_wheels = qfalse;
		}

		if (PM_SaberInAttack(self->client->ps.saberMove) ||
			PM_SaberInStart(self->client->ps.saberMove))
		{
			parts = SETANIM_LEGS;
		}

		if (rightdot >= 0.0f)
		{
			if (Q_irand(0, 1))
			{
				anim = BOTH_ARIAL_LEFT;
			}
			else
			{
				anim = BOTH_CARTWHEEL_LEFT;
			}
			check_dist = -128.0f;
			speed = -200.0f;
		}
		else
		{
			if (Q_irand(0, 1))
			{
				anim = BOTH_ARIAL_RIGHT;
			}
			else
			{
				anim = BOTH_CARTWHEEL_RIGHT;
			}
			check_dist = 128.0f;
			speed = 200.0f;
		}

		// trace in the direction we want to go
		VectorMA(self->r.currentOrigin, check_dist, right, traceto);
		trap->Trace(&trace, self->r.currentOrigin, mins, maxs, traceto, self->s.number, CONTENTS_SOLID | CONTENTS_MONSTERCLIP | CONTENTS_BOTCLIP, qfalse, 0, 0);

		if (trace.fraction >= 1.0f &&
			self->client->NPC_class == CLASS_ALORA &&
			allow_cart_wheels)
		{
			// clear ? do cartwheel/arial
			NPC_SetAnim(self, parts, anim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			self->client->ps.weaponTime = self->client->ps.legsTimer;

			vec3_t vec_out, jumpRt;
			VectorCopy(self->client->ps.viewangles, vec_out);
			vec_out[PITCH] = vec_out[ROLL] = 0.0f;

			AngleVectors(vec_out, NULL, jumpRt, NULL);
			VectorScale(jumpRt, speed, self->client->ps.velocity);

			self->client->ps.fd.forceJumpCharge = 0;
			self->client->ps.velocity[2] = 200.0f;
			self->client->ps.fd.forceJumpZStart = self->r.currentOrigin[2];
			self->client->ps.pm_flags |= PMF_JUMPING;

			if (self->client->NPC_class == CLASS_BOBAFETT ||
				self->client->NPC_class == CLASS_MANDO ||
				(self->client->NPC_class == CLASS_REBORN && self->s.weapon != WP_SABER))
			{
				G_AddEvent(self, EV_JUMP, 0);
			}
			else
			{
				if (self->client->ps.fd.forcePowerLevel[FP_LEVITATION] < FORCE_LEVEL_3)
				{
					G_SoundOnEnt(self, CHAN_BODY, "sound/weapons/force/jumpsmall.mp3");
				}
				else
				{
					G_SoundOnEnt(self, CHAN_BODY, "sound/weapons/force/jump.mp3");
				}
			}

			return EVASION_CARTWHEEL;
		}

		if (!(trace.contents & CONTENTS_BOTCLIP))
		{
			// hit a wall, not a do-not-enter brush
			vec3_t ideal_normal;
			VectorSubtract(self->r.currentOrigin, traceto, ideal_normal);
			VectorNormalize(ideal_normal);

			const gentity_t* traceEnt = &g_entities[trace.entityNum];
			if ((trace.entityNum < ENTITYNUM_WORLD && traceEnt && traceEnt->s.solid != SOLID_BMODEL) ||
				DotProduct(trace.plane.normal, ideal_normal) > 0.7f)
			{
				float best_check_dist = 0.0f;

				if (DotProduct(self->client->ps.velocity, fwd) < 200.0f)
				{
					// not running forward very fast
					if (trace.fraction * check_dist <= 32.0f)
					{
						best_check_dist = check_dist;
					}
					else
					{
						// too far from that wall, check the other side
						check_dist *= -1.0f;
						VectorMA(self->r.currentOrigin, check_dist, right, traceto);

						trap->Trace(&trace, self->r.currentOrigin, mins, maxs, traceto, self->s.number, CONTENTS_SOLID | CONTENTS_MONSTERCLIP | CONTENTS_BOTCLIP, qfalse, 0, 0);

						if (trace.fraction * check_dist <= 32.0f)
						{
							best_check_dist = check_dist;
						}
						else
						{
							// neither side has a wall within 32
							return EVASION_NONE;
						}
					}
				}

				// Try wall run
				if (best_check_dist != 0.0f)
				{
					qboolean allow_wall_runs = qtrue;

					if (self->client->ps.weapon == WP_SABER)
					{
						if (self->client->saber[0].saberFlags & SFL_NO_WALL_RUNS)
						{
							allow_wall_runs = qfalse;
						}
						else if ((self->client->saber[1].model[0] != 0) &&
							(self->client->saber[1].saberFlags & SFL_NO_WALL_RUNS))
						{
							allow_wall_runs = qfalse;
						}
					}

					if (allow_wall_runs)
					{
						if (best_check_dist > 0.0f)
						{
							anim = BOTH_WALL_RUN_RIGHT;
						}
						else
						{
							anim = BOTH_WALL_RUN_LEFT;
						}

						self->client->ps.velocity[2] = forceJumpStrength[FORCE_LEVEL_2] / 2.25f;

						int set_anim_parts = SETANIM_LEGS;
						if (!self->client->ps.weaponTime)
						{
							set_anim_parts = SETANIM_BOTH;
						}

						NPC_SetAnim(self, set_anim_parts, anim,
							SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

						self->client->ps.fd.forceJumpZStart = self->r.currentOrigin[2];
						self->client->ps.pm_flags |= PMF_JUMPING | PMF_SLOW_MO_FALL;

						if (self->client->NPC_class == CLASS_BOBAFETT ||
							self->client->NPC_class == CLASS_MANDO ||
							(self->client->NPC_class == CLASS_REBORN && self->s.weapon != WP_SABER))
						{
							G_AddEvent(self, EV_JUMP, 0);
						}
						else
						{
							if (self->client->ps.fd.forcePowerLevel[FP_LEVITATION] < FORCE_LEVEL_3)
							{
								G_SoundOnEnt(self, CHAN_BODY, "sound/weapons/force/jumpsmall.mp3");
							}
							else
							{
								G_SoundOnEnt(self, CHAN_BODY, "sound/weapons/force/jump.mp3");
							}
						}

						return EVASION_OTHER;
					}
				}
			}
		}
	}

	return EVASION_NONE;
}



int Jedi_ReCalcParryTime(const gentity_t* self, const evasionType_t evasion_type)
{
	if (!self->client)
	{
		return 0;
	}
	if ((self->s.number < MAX_CLIENTS))
	{
		//player blocks how he wants
	}
	else if (self->NPC)
	{
		int base_time = 0;

		if (evasion_type == EVASION_DODGE)
		{
			base_time = self->client->ps.torsoTimer;
		}
		else if (evasion_type == EVASION_CARTWHEEL)
		{
			base_time = self->client->ps.torsoTimer;
		}
		else if (self->client->ps.saberInFlight)
		{
			base_time = Q_irand(1, 3) * 50;
		}
		else
		{
			if (qtrue)
			{
				switch (g_npcspskill.integer)
				{
				case 0:
					base_time = 400;
					break;
				case 1:
					base_time = 200;
					break;
				case 2:
				default:
					base_time = 100;
					break;
				}
			}

			base_time += 400 * Q_irand(1, 2);

			if (evasion_type == EVASION_DUCK || evasion_type == EVASION_DUCK_PARRY)
			{
				base_time += 250; //300;//100;
			}
			else if (evasion_type == EVASION_JUMP || evasion_type == EVASION_JUMP_PARRY)
			{
				base_time += 400; //500;//50;
			}
			else if (evasion_type == EVASION_OTHER)
			{
				base_time += 50; //100;
			}
			else if (evasion_type == EVASION_FJUMP)
			{
				base_time += 300; //400;//100;
			}
		}

		return base_time;
	}
	return 0;
}



static qboolean Jedi_SaberBusy(const gentity_t* self)
{
	if (self->client->ps.torsoTimer > 300
		&& (PM_SaberInAttack(self->client->ps.saberMove) && self->client->ps.fd.saberAnimLevel == SS_STRONG
			|| PM_SpinningSaberAnim(self->client->ps.torsoAnim)
			|| PM_SaberInSpecialAttack(self->client->ps.torsoAnim)
			|| PM_SaberInBrokenParry(self->client->ps.saberMove)
			|| PM_FlippingAnim(self->client->ps.torsoAnim)
			|| PM_RollingAnim(self->client->ps.torsoAnim)))
	{
		//my saber is not in a parrying position
		return qtrue;
	}
	return qfalse;
}



static qboolean Jedi_InNoAIAnim(const gentity_t* self)
{
	if (!self || !self->client)
	{
		//wtf???
		return qtrue;
	}

	if (NPCS.NPCInfo->rank >= RANK_COMMANDER)
	{
		//boss-level guys can multitask, the rest need to chill out during special moves
		return qfalse;
	}

	if (PM_KickingAnim(NPCS.NPC->client->ps.legsAnim)
		|| BG_StabDownAnim(NPCS.NPC->client->ps.legsAnim)
		|| PM_InAirKickingAnim(NPCS.NPC->client->ps.legsAnim)
		|| PM_InRollIgnoreTimer(&NPCS.NPC->client->ps)
		|| PM_SaberInKata((saberMoveName_t)(NPCS.NPC->client->ps.saberMove))
		|| PM_SuperBreakWinAnim(NPCS.NPC->client->ps.torsoAnim)
		|| PM_SuperBreakLoseAnim(NPCS.NPC->client->ps.torsoAnim))
	{
		return qtrue;
	}

	switch (self->client->ps.legsAnim)
	{
	case BOTH_BUTTERFLY_LEFT:
	case BOTH_BUTTERFLY_RIGHT:
	case BOTH_BUTTERFLY_FL1:
	case BOTH_BUTTERFLY_FR1:
	case BOTH_FLIP_F:
	case BOTH_FLIP_B:
	case BOTH_FLIP_L:
	case BOTH_FLIP_R:
	case BOTH_DODGE_FL:
	case BOTH_DODGE_FR:
	case BOTH_DODGE_BL:
	case BOTH_DODGE_BR:
	case BOTH_DODGE_L:
	case BOTH_DODGE_R:
	case BOTH_DODGE_HOLD_FL:
	case BOTH_DODGE_HOLD_FR:
	case BOTH_DODGE_HOLD_BL:
	case BOTH_DODGE_HOLD_BR:
	case BOTH_DODGE_HOLD_L:
	case BOTH_DODGE_HOLD_R:
	case BOTH_FORCEWALLRUNFLIP_START:
	case BOTH_JUMPATTACK6:
	case BOTH_JUMPATTACK7:
	case BOTH_GRIEVOUS_LUNGE:
	case BOTH_JUMPFLIPSLASHDOWN1:
	case BOTH_JUMPFLIPSTABDOWN:
	case BOTH_FORCELEAP2_T__B_:
	case BOTH_FORCELEAP_PALP:
	case BOTH_ROLL_STAB:
	case BOTH_SPINATTACK6:
	case BOTH_SPINATTACK7:
	case BOTH_SPINATTACKGRIEVOUS:
	case BOTH_PULL_IMPALE_STAB:
	case BOTH_PULL_IMPALE_SWING:
	case BOTH_A6_FB:
	case BOTH_A6_LR:
	case BOTH_A7_HILT:
	case BOTH_SLAP_R:
	case BOTH_SLAP_L:
	case BOTH_DASH_L:
	case BOTH_DASH_R:
	case BOTH_SMACK_R:
	case BOTH_SMACK_L:
		return qtrue;
	default:;
	}
	return qfalse;
}



static void Jedi_CheckJumpEvasionSafety(usercmd_t* cmd, const evasionType_t evasion_type)
{
	if (evasion_type != EVASION_OTHER //not a FlipEvasion, which does it's own safety checks
		&& NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE)
	{
		//on terra firma right now
		if (NPCS.NPC->client->ps.velocity[2] > 0
			|| NPCS.NPC->client->ps.fd.forceJumpCharge
			|| cmd->upmove > 0)
		{
			//going to jump
			if (!NAV_MoveDirSafe(NPCS.NPC, cmd, NPCS.NPC->client->ps.speed * 10.0f))
			{
				//we can't jump in the dir we're pushing in
				//cancel the evasion
				NPCS.NPC->client->ps.velocity[2] = NPCS.NPC->client->ps.fd.forceJumpCharge = 0;
				cmd->upmove = 0;
				if (d_JediAI.integer || d_combatinfo.integer || g_DebugSaberCombat.integer)
				{
					Com_Printf(S_COLOR_RED"jump not safe, cancelling!");
				}
			}
			else if (NPCS.NPC->client->ps.velocity[0] || NPCS.NPC->client->ps.velocity[1])
			{
				//sliding
				vec3_t jump_dir;
				const float jump_dist = VectorNormalize2(NPCS.NPC->client->ps.velocity, jump_dir);
				if (!NAV_DirSafe(NPCS.NPC, jump_dir, jump_dist))
				{
					//this jump combined with our momentum would send us into a do not enter brush, so cancel it
					//cancel the evasion
					NPCS.NPC->client->ps.velocity[2] = NPCS.NPC->client->ps.fd.forceJumpCharge = 0;
					cmd->upmove = 0;
					if (d_JediAI.integer || d_combatinfo.integer || g_DebugSaberCombat.integer)
					{
						Com_Printf(S_COLOR_RED"jump not safe, cancelling!\n");
					}
				}
			}
			if (d_JediAI.integer || d_combatinfo.integer || g_DebugSaberCombat.integer)
			{
				Com_Printf(S_COLOR_GREEN"jump checked, is safe\n");
			}
		}
	}
}


evasionType_t jedi_saber_block_go(gentity_t* self, usercmd_t* cmd, vec3_t p_hitloc, vec3_t phit_dir,
	const gentity_t* incoming,
	const float dist) //dist = 0.0f
{
	vec3_t hitloc, hitdir, diff, fwdangles = { 0, 0, 0 }, right;
	int duck_chance = 0;
	int dodge_anim = -1;
	qboolean saber_busy = qfalse;
	evasionType_t evasion_type = EVASION_NONE;

	if (!self || !self->client)
	{
		return EVASION_NONE;
	}

	if (PM_LockedAnim(self->client->ps.torsoAnim)
		&& self->client->ps.torsoTimer)
	{
		//Never interrupt these...
		return EVASION_NONE;
	}
	if (PM_InSpecialJump(self->client->ps.legsAnim)
		&& PM_SaberInSpecialAttack(self->client->ps.torsoAnim))
	{
		return EVASION_NONE;
	}

	if (Jedi_InNoAIAnim(self))
	{
		return EVASION_NONE;
	}

	if (!incoming)
	{
		VectorCopy(p_hitloc, hitloc);
		VectorCopy(phit_dir, hitdir);

		if (self->client->ps.saberInFlight)
		{
			//DOH!  do non-saber evasion!
			saber_busy = qtrue;
		}
		else
		{
			saber_busy = Jedi_SaberBusy(self);
		}
	}
	else
	{
		if (incoming->s.weapon == WP_SABER)
		{
			//flying lightsaber, face it!
		}
		VectorCopy(incoming->r.currentOrigin, hitloc);
		VectorNormalize2(incoming->s.pos.trDelta, hitdir);
	}

	VectorSubtract(hitloc, self->client->renderInfo.eyePoint, diff);
	diff[2] = 0;
	fwdangles[1] = self->client->ps.viewangles[1];
	AngleVectors(fwdangles, NULL, right, NULL);

	const float rightdot = DotProduct(right, diff);
	const float zdiff = hitloc[2] - self->client->renderInfo.eyePoint[2];

	qboolean do_dodge = qfalse;
	qboolean always_dodge_or_roll = qfalse;
	qboolean npc_can_dodge = qfalse;

	if (self->client->NPC_class == CLASS_BOBAFETT
		|| self->client->NPC_class == CLASS_MANDO)
	{
		return EVASION_NONE;
	}
	else
	{
		if ((self->client->NPC_class == CLASS_REBORN && self->s.weapon != WP_SABER)
			|| (self->client->NPC_class == CLASS_SITHLORD && self->s.weapon != WP_SABER))
		{
			saber_busy = qtrue;
			always_dodge_or_roll = qtrue;
		}

		//see if we can dodge if need-be
		if (((dist > 16 && (Q_irand(0, 2) || saber_busy))
			|| self->client->ps.saberInFlight
			|| !(!self->client->ps.saberHolstered)
			|| (self->client->NPC_class == CLASS_REBORN && self->s.weapon != WP_SABER)
			|| (self->client->NPC_class == CLASS_SITHLORD && self->s.weapon != WP_SABER)))
		{
			//either it will miss by a bit (and 25% chance) OR our saber is not in-hand OR saber is off

			if (self->NPC && (self->NPC->rank == RANK_CREWMAN || self->NPC->rank >= RANK_LT_JG))
			{
				//acrobat or fencer or above
				if (self->client->ps.groundEntityNum != ENTITYNUM_NONE && //on the ground
					!(self->client->ps.pm_flags & PMF_DUCKED) && cmd->upmove >= 0 && TIMER_Done(self, "duck")
					//not ducking
					&& !PM_InRoll(&self->client->ps) //not rolling
					&& !PM_InKnockDown(&self->client->ps) //not knocked down
					&& (self->client->ps.saberInFlight
						|| (self->client->NPC_class == CLASS_REBORN && self->s.weapon != WP_SABER)
						|| (self->client->NPC_class == CLASS_SITHLORD && self->s.weapon != WP_SABER)
						|| (!PM_SaberInAttack(self->client->ps.saberMove) //not attacking
							&& !PM_SaberInStart(self->client->ps.saberMove) //not starting an attack
							&& !PM_SpinningSaberAnim(self->client->ps.torsoAnim) //not in a saber spin
							&& !PM_SaberInSpecialAttack(self->client->ps.torsoAnim)))) //not in a special attack
				{
					//need to check all these because it overrides both torso and legs with the dodge
					do_dodge = qtrue;
				}
			}
		}
	}

	if ((self->client->NPC_class == CLASS_REBORN && self->s.weapon != WP_SABER)
		|| (self->client->NPC_class == CLASS_SITHLORD && self->s.weapon != WP_SABER)
		|| self->client->NPC_class == CLASS_BOBAFETT
		|| self->client->NPC_class == CLASS_MANDO
		|| (self->s.weapon == WP_SABER && !(!self->client->ps.saberHolstered))
		|| (self->s.weapon == WP_SABER && self->client->ps.saberInFlight))
	{
		npc_can_dodge = qtrue;
	}

	// ============================
	// SECTION 2 — Upper-Tier Evasion Logic (Enhanced Blocking Version)
	// ============================

	qboolean do_roll = qfalse;

	// Reduce roll frequency dramatically
	if ((self->client->NPC_class == CLASS_BOBAFETT || self->client->NPC_class == CLASS_MANDO //boba fett
		|| (self->client->NPC_class == CLASS_REBORN && self->s.weapon != WP_SABER) //non-saber reborn (cultist)
		|| (self->client->NPC_class == CLASS_SITHLORD && self->s.weapon != WP_SABER))
		&& !Q_irand(0, 5))
	{
		do_roll = qtrue;
	}

	// Cinematic + aggressive: bias away from dodge unless truly needed
	/*if (Jedi_ShouldDodge(self, saber_busy))
	{
		do_dodge = qtrue;
	}*/

	// Figure out what quadrant the block was in.
	if (d_JediAI.integer || g_DebugSaberCombat.integer)
	{
		Com_Printf("(%d) evading attack from height %4.2f, zdiff: %4.2f, rightdot: %4.2f\n", level.time,
			hitloc[2] - self->r.absmin[2], zdiff, rightdot);
	}

	if (zdiff >= -5)
	{
		if (incoming || !saber_busy || always_dodge_or_roll)
		{
			// ============================
			// RIGHT-SIDE ATTACK
			// ============================
			if (rightdot > 12 || (rightdot > 3 && zdiff < 5)
				|| (!incoming && fabs(hitdir[2]) < 0.25f))
			{
				// Prefer blocking over dodging
				if (self->s.weapon == WP_SABER && (!self->client->ps.saberHolstered)
					&& !self->client->ps.saberInFlight)
				{
					wp_saber_block_check_random(self, hitloc);
					evasion_type = EVASION_PARRY;
				}
				else
				{
					// Saber is in flight ? try dash evasion
					if (self->client->ps.saberInFlight)
					{
						if (wp_saber_Off_Dash_Evasion(self, hitloc))
						{
							evasion_type = EVASION_DASH;
							return EVASION_DASH;
						}
					}

					// fallback dodge only if absolutely needed
					if (do_dodge && Q_irand(0, 3) && npc_can_dodge)
					{
						dodge_anim = BOTH_DODGE_L;
						evasion_type = EVASION_DODGE;
					}
				}

				// small duck chance preserved
				if (self->client->ps.groundEntityNum != ENTITYNUM_NONE && !NPC_IsOversized(self))
				{
					if (zdiff > 5)
					{
						TIMER_Start(self, "duck", Q_irand(500, 1500));
						evasion_type = EVASION_DUCK_PARRY;
					}
					else
					{
						duck_chance = 4; // slightly reduced
					}
				}

				if (d_JediAI.integer || g_DebugSaberCombat.integer)
				{
					Com_Printf("UR block\n");
				}
			}

			// ============================
			// LEFT-SIDE ATTACK
			// ============================
			else if (rightdot < -12 || (rightdot < -3 && zdiff < 5)
				|| (!incoming && fabs(hitdir[2]) < 0.25f))
			{
				// Prefer blocking
				if (self->s.weapon == WP_SABER && (!self->client->ps.saberHolstered)
					&& !self->client->ps.saberInFlight)
				{
					wp_saber_block_check_random(self, hitloc);
					evasion_type = EVASION_PARRY;
				}
				else
				{
					// Saber is in flight ? try dash evasion
					if (self->client->ps.saberInFlight)
					{
						if (wp_saber_Off_Dash_Evasion(self, hitloc))
						{
							evasion_type = EVASION_DASH;
							return EVASION_DASH;
						}
					}

					if (do_dodge && Q_irand(0, 3) && npc_can_dodge)
					{
						dodge_anim = BOTH_DODGE_R;
						evasion_type = EVASION_DODGE;
					}
				}

				if (self->client->ps.groundEntityNum != ENTITYNUM_NONE && !NPC_IsOversized(self))
				{
					if (zdiff > 5)
					{
						TIMER_Start(self, "duck", Q_irand(500, 1500));
						evasion_type = EVASION_DUCK_PARRY;
					}
					else
					{
						duck_chance = 4;
					}
				}

				if (d_JediAI.integer || g_DebugSaberCombat.integer)
				{
					Com_Printf("UL block\n");
				}
			}

			// ============================
			// FRONT / OVERHEAD ATTACK
			// ============================
			else
			{
				// Strong preference for parry
				if (self->s.weapon == WP_SABER && (!self->client->ps.saberHolstered)
					&& !self->client->ps.saberInFlight)
				{
					wp_saber_block_check_random(self, hitloc);
					evasion_type = EVASION_PARRY;
				}
				else
				{
					// Saber is in flight ? try dash evasion
					if (self->client->ps.saberInFlight)
					{
						if (wp_saber_Off_Dash_Evasion(self, hitloc))
						{
							evasion_type = EVASION_DASH;
							return EVASION_DASH;
						}
					}

					// fallback roll (20% chance)
					if (NPCS.NPCInfo->stats.evasion >= 3 && Q_irand(0, 4) == 0)
					{
						dodge_anim = BOTH_ROLL_B;
						self->client->pers.cmd.forwardmove = -127;
						G_SoundOnEnt(self, CHAN_BODY, "sound/player/roll1.wav");
						evasion_type = EVASION_DODGE;
					}
				}

				if (self->client->ps.groundEntityNum != ENTITYNUM_NONE)
				{
					duck_chance = 3; // reduced
				}

				if (d_JediAI.integer || g_DebugSaberCombat.integer)
				{
					Com_Printf("TOP block\n");
				}
			}
		}
	}
	// ============================
	// SECTION 3 — Mid-Tier Evasion Logic (Cinematic Jedi Master)
	// ============================

	else if (zdiff > -22)
	{
		// --------------------------------
		// Slightly low attack ? possible duck
		// --------------------------------
		if (zdiff < -10)
		{
			if (!NPC_IsOversized(self))
			{
				if (self->client->ps.groundEntityNum != ENTITYNUM_NONE)
				{
					TIMER_Start(self, "duck", Q_irand(500, 1500));
					evasion_type = EVASION_DUCK;

					if (d_JediAI.integer || g_DebugSaberCombat.integer)
					{
						Com_Printf("[BLOCK] Mid Low Duck\n");
					}
				}
			}
		}

		// --------------------------------
		// Determine dodge allowance based on skill level
		// --------------------------------
		int mode = g_npcspskill.integer;
		qboolean allowDodge = qtrue;

		switch (mode)
		{
		case 0: // vanilla
			allowDodge = (Q_irand(0, 10) == 0) ? qtrue : qfalse;  // ~9% chance to dodge
			break;

		case 1: // strong blocker
			allowDodge = (Q_irand(0, 4) == 0) ? qtrue : qfalse;   // 20% chance to dodge
			break;

		case 2: // Jedi Master
			allowDodge = qtrue;
			break;
		}

		// --------------------------------
		// Can we dodge or parry?
		// --------------------------------
		if (incoming || !saber_busy || always_dodge_or_roll)
		{
			// ============================
		 // MID — RIGHT-SIDE ATTACK
		 // ============================
			if (rightdot > 8 || (rightdot > 3 && zdiff < -11)
				|| (!incoming && fabs(hitdir[2]) < 0.25f))
			{
				// 1. Prefer parry
				if (self->s.weapon == WP_SABER && (!self->client->ps.saberHolstered)
					&& !self->client->ps.saberInFlight)
				{
					wp_saber_block_check_random(self, hitloc);
					evasion_type = EVASION_PARRY;

					if (d_JediAI.integer || g_DebugSaberCombat.integer)
					{
						Com_Printf("[BLOCK] Mid Right Parry\n");
					}
				}
				// 2. Saber in flight ? dash immediately
				else if (self->client->ps.saberInFlight)
				{
					if (wp_saber_Off_Dash_Evasion(self, hitloc))
					{
						evasion_type = EVASION_DASH;
						return EVASION_DASH;
					}
				}
				// 3. Dodge
				else if (do_dodge && allowDodge && npc_can_dodge)
				{
					dodge_anim = BOTH_DODGE_L;
					evasion_type = EVASION_DODGE;

					if (d_JediAI.integer)
					{
						Com_Printf("[DODGE] Mid Right Emergency Dodge Left\n");
					}
				}
				// 4. Roll
				else if (NPCS.NPCInfo->stats.evasion >= 3 && allowDodge)
				{
					dodge_anim = BOTH_ROLL_L;
					self->client->pers.cmd.rightmove = -127;
					G_SoundOnEnt(self, CHAN_BODY, "sound/player/roll1.wav");
					evasion_type = EVASION_DODGE;

					if (d_JediAI.integer)
					{
						Com_Printf("[ROLL] Mid Right Emergency Roll Left\n");
					}
				}
			}

			// ============================
			// MID — LEFT-SIDE ATTACK
			// ============================
			else if (rightdot < -8 || (rightdot < -3 && zdiff < -11)
				|| (!incoming && fabs(hitdir[2]) < 0.25f))
			{
				// Prefer parry
				if (self->s.weapon == WP_SABER && (!self->client->ps.saberHolstered)
					&& !self->client->ps.saberInFlight)
				{
					wp_saber_block_check_random(self, hitloc);
					evasion_type = EVASION_PARRY;

					if (d_JediAI.integer || g_DebugSaberCombat.integer)
					{
						Com_Printf("[BLOCK] Mid Left Parry\n");
					}
				}
				// 2. Saber in flight ? dash immediately
				else if (self->client->ps.saberInFlight)
				{
					if (wp_saber_Off_Dash_Evasion(self, hitloc))
					{
						evasion_type = EVASION_DASH;
						return EVASION_DASH;
					}
				}
				else if (do_dodge && allowDodge && npc_can_dodge)
				{
					dodge_anim = BOTH_DODGE_R;
					evasion_type = EVASION_DODGE;

					if (d_JediAI.integer)
					{
						Com_Printf("[DODGE] Mid Left Emergency Dodge Right\n");
					}
				}
				else if (NPCS.NPCInfo->stats.evasion >= 3 && allowDodge)
				{
					dodge_anim = BOTH_ROLL_R;
					self->client->pers.cmd.rightmove = 127;
					G_SoundOnEnt(self, CHAN_BODY, "sound/player/roll1.wav");
					evasion_type = EVASION_DODGE;

					if (d_JediAI.integer)
					{
						Com_Printf("[ROLL] Mid Left Emergency Roll Right\n");
					}
				}
			}

			// ============================
			// MID — FRONT / OVERHEAD ATTACK
			// ============================
			else
			{
				// Prefer parry
				if (self->s.weapon == WP_SABER && (!self->client->ps.saberHolstered)
					&& !self->client->ps.saberInFlight)
				{
					wp_saber_block_check_random(self, hitloc);
					evasion_type = EVASION_PARRY;

					if (d_JediAI.integer || g_DebugSaberCombat.integer)
					{
						Com_Printf("[BLOCK] Mid Front Parry\n");
					}
				}
				// 2. Saber in flight ? dash immediately
				else if (self->client->ps.saberInFlight)
				{
					if (wp_saber_Off_Dash_Evasion(self, hitloc))
					{
						evasion_type = EVASION_DASH;
						return EVASION_DASH;
					}
				}
				else if (NPCS.NPCInfo->stats.evasion >= 3 && allowDodge)
				{
					dodge_anim = BOTH_ROLL_B;
					self->client->pers.cmd.forwardmove = -127;
					G_SoundOnEnt(self, CHAN_BODY, "sound/player/roll1.wav");
					evasion_type = EVASION_DODGE;

					if (d_JediAI.integer)
					{
						Com_Printf("[ROLL] Mid Front Emergency Roll Back\n");
					}
				}
			}
		}
	}// ============================
	 // SECTION 4 — Low / Very-Low Evasion Logic (Cinematic Jedi Master)
	 // ============================

	else
	{
		// --------------------------------
		// Determine dodge allowance based on skill level
		// --------------------------------
		int mode = g_npcspskill.integer;
		qboolean allowDodge = qtrue;

		switch (mode)
		{
		case 0: // vanilla
			allowDodge = (Q_irand(0, 10) == 0) ? qtrue : qfalse; // ~9% chance to dodge
			break;

		case 1: // strong blocker
			allowDodge = (Q_irand(0, 4) == 0) ? qtrue : qfalse;   // 20% chance to dodge
			break;

		case 2: // Jedi Master
			allowDodge = qtrue;
			break;
		}

		// --------------------------------
		// Very low attacks or saber busy ? jump or parry
		// --------------------------------
		if (saber_busy || (zdiff < -36 && (zdiff < -44 || !Q_irand(0, 2))))
		{
			// If airborne ? duck to lift legs
			if (self->client->ps.groundEntityNum == ENTITYNUM_NONE)
			{
				TIMER_Start(self, "duck", Q_irand(500, 1500));
				evasion_type = EVASION_DUCK;

				if ((incoming && self->s.weapon == WP_SABER && (!self->client->ps.saberHolstered)
					&& !self->client->ps.saberInFlight)
					|| !saber_busy)
				{
					wp_saber_block_check_random(self, hitloc);
					evasion_type = EVASION_DUCK_PARRY;

					if (d_JediAI.integer || g_DebugSaberCombat.integer)
					{
						Com_Printf("[BLOCK] Low Air Duck-Parry\n");
					}
				}
			}
			else
			{
				// Prefer parry
				if (self->s.weapon == WP_SABER && (!self->client->ps.saberHolstered)
					&& !self->client->ps.saberInFlight)
				{
					wp_saber_block_check_random(self, hitloc);
					evasion_type = EVASION_PARRY;

					if (d_JediAI.integer || g_DebugSaberCombat.integer)
					{
						Com_Printf("[BLOCK] Low Parry\n");
					}
				}
				else if (NPCS.NPCInfo->stats.evasion >= 3 && allowDodge)
				{
					// Emergency roll if saber cannot block
					if (rightdot > 0)
					{
						dodge_anim = BOTH_ROLL_L;
						self->client->pers.cmd.rightmove = -127;
						G_SoundOnEnt(self, CHAN_BODY, "sound/player/roll1.wav");
						evasion_type = EVASION_DODGE;

						if (d_JediAI.integer)
						{
							Com_Printf("[ROLL] Low Emergency Roll Left\n");
						}
					}
					else
					{
						dodge_anim = BOTH_ROLL_R;
						self->client->pers.cmd.rightmove = 127;
						G_SoundOnEnt(self, CHAN_BODY, "sound/player/roll1.wav");
						evasion_type = EVASION_DODGE;

						if (d_JediAI.integer)
						{
							Com_Printf("[ROLL] Low Emergency Roll Right\n");
						}
					}
				}

				// Flip evasions (rare cinematic flips)
				if ((evasion_type = Jedi_CheckFlipEvasions(self, rightdot)) != EVASION_NONE)
				{
					

					if (d_JediAI.integer)
					{
						Com_Printf("[DODGE] Low Flip Evasion\n");
					}
				}
				else if ((incoming && self->s.weapon == WP_SABER && (!self->client->ps.saberHolstered)
					&& !self->client->ps.saberInFlight)
					|| !saber_busy)
				{
					// fallback parry
					wp_saber_block_check_random(self, hitloc);
					evasion_type = EVASION_PARRY;

					if (d_JediAI.integer || g_DebugSaberCombat.integer)
					{
						if (rightdot >= 0)
						{
							Com_Printf("[BLOCK] Low Right Parry\n");
						}
						else
						{
							Com_Printf("[BLOCK] Low Left Parry\n");
						}
					}
				}
				else
				{
					// fallback roll if saber cannot block
					if (NPCS.NPCInfo->stats.evasion >= 3 && allowDodge)
					{
						if (rightdot > 0)
						{
							dodge_anim = BOTH_ROLL_L;
							self->client->pers.cmd.rightmove = -127;
							G_SoundOnEnt(self, CHAN_BODY, "sound/player/roll1.wav");
							evasion_type = EVASION_DODGE;

							if (d_JediAI.integer)
							{
								Com_Printf("[ROLL] Low Fallback Roll Left\n");
							}
						}
						else
						{
							dodge_anim = BOTH_ROLL_R;
							self->client->pers.cmd.rightmove = 127;
							G_SoundOnEnt(self, CHAN_BODY, "sound/player/roll1.wav");
							evasion_type = EVASION_DODGE;

							if (d_JediAI.integer)
							{
								Com_Printf("[ROLL] Low Fallback Roll Right\n");
							}
						}
					}
				}
			}
		}
		else
		{
			// --------------------------------
			// Normal low parry (preferred)
			// --------------------------------
			if ((incoming && self->s.weapon == WP_SABER && (!self->client->ps.saberHolstered)
				&& !self->client->ps.saberInFlight)
				|| !saber_busy)
			{
				wp_saber_block_check_random(self, hitloc);
				evasion_type = EVASION_PARRY;

				if (d_JediAI.integer || g_DebugSaberCombat.integer)
				{
					if (rightdot >= 0)
					{
						Com_Printf("[BLOCK] Low Right Parry\n");
					}
					else
					{
						Com_Printf("[BLOCK] Low Left Parry\n");
					}
				}

				// Thrown saber case
				if (incoming && incoming->s.weapon == WP_SABER)
				{
					if (self->s.weapon == WP_SABER && (!self->client->ps.saberHolstered)
						&& !self->client->ps.saberInFlight)
					{
						wp_saber_block_check_random(self, hitloc);
						evasion_type = EVASION_PARRY;

						if (d_JediAI.integer)
						{
							Com_Printf("[BLOCK] Low Thrown Saber Parry\n");
						}
					}
					else if (NPCS.NPCInfo->stats.evasion >= 3 && allowDodge)
					{
						if (rightdot > 0)
						{
							dodge_anim = BOTH_ROLL_L;
							self->client->pers.cmd.rightmove = -127;
							G_SoundOnEnt(self, CHAN_BODY, "sound/player/roll1.wav");
							evasion_type = EVASION_DODGE;

							if (d_JediAI.integer)
							{
								Com_Printf("[ROLL] Low Thrown Saber Roll Left\n");
							}
						}
						else
						{
							dodge_anim = BOTH_ROLL_R;
							self->client->pers.cmd.rightmove = 127;
							G_SoundOnEnt(self, CHAN_BODY, "sound/player/roll1.wav");
							evasion_type = EVASION_DODGE;

							if (d_JediAI.integer)
							{
								Com_Printf("[ROLL] Low Thrown Saber Roll Right\n");
							}
						}
					}
				}
			}
		}
	}

	// ============================
	// SECTION 5 — Finalization & Post-Evasion Handling
	// ============================

	if (evasion_type == EVASION_NONE)
	{
		return EVASION_NONE;
	}

	// ======================================================================
	// see if it's okay to jump
	// ======================================================================
	Jedi_CheckJumpEvasionSafety(cmd, evasion_type);

	// ======================================================================
	// stop taunting / gripping / draining
	// ======================================================================
	TIMER_Set(self, "taunting", 0);

	// stop gripping
	TIMER_Set(self, "grasping", -level.time);
	WP_ForcePowerStop(self, FP_GRASP);

	TIMER_Set(self, "gripping", -level.time);
	WP_ForcePowerStop(self, FP_GRIP);

	// stop draining
	TIMER_Set(self, "draining", -level.time);
	WP_ForcePowerStop(self, FP_DRAIN);

	// ======================================================================
	// dodge animation handling
	// ======================================================================
	if (dodge_anim != -1)
	{
		// dodged
		evasion_type = EVASION_DODGE;

		NPC_SetAnim(self, SETANIM_BOTH, dodge_anim,
			SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

		self->client->ps.weaponTime = self->client->ps.torsoTimer;

		// force them to stop moving in this case
		self->client->ps.pm_time = self->client->ps.torsoTimer;

		// dodged, not block
		self->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;

		
	}
	else
	{
		// ==================================================================
		// duck chance
		// ==================================================================
		if (duck_chance)
		{
			if (!Q_irand(0, duck_chance))
			{
				TIMER_Start(self, "duck", Q_irand(500, 1500));

				if (evasion_type == EVASION_PARRY)
				{
					evasion_type = EVASION_DUCK_PARRY;
				}
				else
				{
					evasion_type = EVASION_DUCK;
				}
			}
		}

		// ==================================================================
		// incoming projectile / saber block timing
		// ==================================================================
		if (incoming)
		{
			self->client->ps.saberBlocked =
				WP_MissileBlockForBlock(self->client->ps.saberBlocked);

			self->client->ps.weaponTime = Q_irand(300, 600);
		}
	}

	// ======================================================================
	// recalc parry timing
	// ======================================================================
	const int parry_re_calc_time = Jedi_ReCalcParryTime(self, evasion_type);

	if (self->client->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE]
		< level.time + parry_re_calc_time)
	{
		self->client->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE] =
			level.time + parry_re_calc_time;
	}

	return evasion_type;
}



static evasionType_t Jedi_CheckEvadeSpecialAttacks(void)
{
	if (!NPCS.NPC
		|| !NPCS.NPC->client)
	{
		return EVASION_NONE;
	}

	if (!NPCS.NPC->enemy
		|| NPCS.NPC->enemy->health <= 0
		|| !NPCS.NPC->enemy->client)
	{
		//don't keep blocking him once he's dead (or if not a client)
		return EVASION_NONE;
	}

	if (NPCS.NPC->enemy->s.number >= MAX_CLIENTS)
	{
		//only do these against player
		return EVASION_NONE;
	}

	if (NPCS.NPC->health <= 10)
	{
		//only do these against player
		return EVASION_NONE;
	}

	if (!TIMER_Done(NPCS.NPC, "specialEvasion"))
	{
		//still evading from last time
		return EVASION_NONE;
	}

	if (NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_SPINATTACK6
		|| NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_SPINATTACK7
		|| NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_SPINATTACKGRIEVOUS)
	{
		//back away from these
		if (NPCS.NPCInfo->aiFlags & NPCAI_BOSS_CHARACTER
			|| NPCS.NPC->client->NPC_class == CLASS_SHADOWTROOPER
			|| NPCS.NPC->client->NPC_class == CLASS_ALORA
			|| Q_irand(0, NPCS.NPCInfo->rank) > RANK_LT_JG)
		{
			//see if we should back off
			if (InFront(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->enemy->r.currentAngles, 0.0f))
			{
				//facing me
				float min_safe_dist_sq = NPCS.NPC->r.maxs[0] * 1.5f + NPCS.NPC->enemy->r.maxs[0] * 1.5f + NPCS.NPC->enemy->client->saber[0].blade[0].length + 24.0f;
				min_safe_dist_sq *= min_safe_dist_sq;
				if (DistanceSquared(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin) < min_safe_dist_sq)
				{
					//back off!
					Jedi_StartBackOff();
					return EVASION_OTHER;
				}
			}
		}
	}
	else
	{
		//check some other attacks?
		//check roll-stab
		if (NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_ROLL_STAB
			|| (NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_ROLL_F
				|| NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_ROLL_F1 ||
				NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_ROLL_F2) && (NPCS.NPC->enemy->client->pers.cmd.buttons &
					BUTTON_ATTACK || NPCS.NPC->enemy->client->ps.pm_flags & PMF_ATTACK_HELD))
		{
			//either already in a roll-stab or may go into one
			if (NPCS.NPCInfo->aiFlags & NPCAI_BOSS_CHARACTER
				|| NPCS.NPC->client->NPC_class == CLASS_SHADOWTROOPER
				|| NPCS.NPC->client->NPC_class == CLASS_ALORA
				|| Q_irand(-3, NPCS.NPCInfo->rank) > RANK_LT_JG)
			{
				//see if we should evade
				vec3_t yawOnlyAngles = { 0, NPCS.NPC->enemy->r.currentAngles[YAW], 0 };
				if (InFront(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin, yawOnlyAngles, 0.25f))
				{
					//facing me
					float min_safe_dist_sq = NPCS.NPC->r.maxs[0] * 1.5f + NPCS.NPC->enemy->r.maxs[0] * 1.5f + NPCS.NPC->enemy->client->saber[0].blade[0].length + 24.0f;
					min_safe_dist_sq *= min_safe_dist_sq;
					const float dist_sq = DistanceSquared(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin);
					if (dist_sq < min_safe_dist_sq)
					{
						//evade!
						qboolean do_jump = (qboolean)(NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_ROLL_STAB ||
							dist_sq
							< 3000.0f); //not much time left, just jump!
						if (NPCS.NPCInfo->scriptFlags & SCF_NO_ACROBATICS
							|| !do_jump)
						{
							//roll?
							vec3_t enemy_right, dir2_me;

							AngleVectors(yawOnlyAngles, NULL, enemy_right, NULL);
							VectorSubtract(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin, dir2_me);
							VectorNormalize(dir2_me);
							const float dot = DotProduct(enemy_right, dir2_me);

							NPCS.ucmd.forwardmove = 0;
							TIMER_Start(NPCS.NPC, "duck", Q_irand(500, 1500));
							NPCS.ucmd.upmove = -127;
							//NOTE: this *assumes* I'm facing him!
							if (dot > 0)
							{
								//I'm to his right
								if (!NPC_MoveDirClear(0, -127, qfalse))
								{
									//fuck, jump instead
									do_jump = qtrue;
								}
								else
								{
									TIMER_Start(NPCS.NPC, "strafeLeft", Q_irand(500, 1500));
									TIMER_Set(NPCS.NPC, "strafeRight", 0);
									NPCS.ucmd.rightmove = -127;
									if (d_JediAI.integer || d_combatinfo.integer)
									{
										Com_Printf("%s rolling left from roll-stab!\n", NPCS.NPC->NPC_type);
									}
									if (NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE)
									{
										//fuck it, just force it
										NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ROLL_L,
											SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
										G_AddEvent(NPCS.NPC, EV_ROLL, 0);
										NPCS.NPC->client->ps.saberMove = LS_NONE;
									}
								}
							}
							else
							{
								//I'm to his left
								if (!NPC_MoveDirClear(0, 127, qfalse))
								{
									//fuck, jump instead
									do_jump = qtrue;
								}
								else
								{
									TIMER_Start(NPCS.NPC, "strafeRight", Q_irand(500, 1500));
									TIMER_Set(NPCS.NPC, "strafeLeft", 0);
									NPCS.ucmd.rightmove = 127;
									if (d_JediAI.integer || d_combatinfo.integer)
									{
										Com_Printf("%s rolling right from roll-stab!\n", NPCS.NPC->NPC_type);
									}
									if (NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE)
									{
										//fuck it, just force it
										NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ROLL_R,
											SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
										G_AddEvent(NPCS.NPC, EV_ROLL, 0);
										NPCS.NPC->client->ps.saberMove = LS_NONE;
									}
								}
							}
							if (!do_jump)
							{
								TIMER_Set(NPCS.NPC, "specialEvasion", 3000);
								return EVASION_DUCK;
							}
						}
						//didn't roll, do jump
						if (NPCS.NPC->s.weapon != WP_SABER
							|| NPCS.NPCInfo->aiFlags & NPCAI_BOSS_CHARACTER
							|| NPCS.NPC->client->NPC_class == CLASS_SHADOWTROOPER
							|| NPCS.NPC->client->NPC_class == CLASS_ALORA
							|| Q_irand(-3, NPCS.NPCInfo->rank) > RANK_CREWMAN)
						{
							//superjump
							NPCS.NPC->client->ps.fd.forceJumpCharge = 320; //FIXME: calc this intelligently
							if (Q_irand(0, 2))
							{
								//make it a backflip
								NPCS.ucmd.forwardmove = -127;
								TIMER_Set(NPCS.NPC, "roamTime", -level.time);
								TIMER_Set(NPCS.NPC, "strafeLeft", -level.time);
								TIMER_Set(NPCS.NPC, "strafeRight", -level.time);
								TIMER_Set(NPCS.NPC, "walking", -level.time);
								TIMER_Set(NPCS.NPC, "moveforward", -level.time);
								TIMER_Set(NPCS.NPC, "movenone", -level.time);
								TIMER_Set(NPCS.NPC, "moveright", -level.time);
								TIMER_Set(NPCS.NPC, "moveleft", -level.time);
								TIMER_Set(NPCS.NPC, "movecenter", -level.time);
								TIMER_Set(NPCS.NPC, "moveback", Q_irand(500, 1000));
								if (d_JediAI.integer || d_combatinfo.integer)
								{
									Com_Printf("%s backflipping from roll-stab!\n", NPCS.NPC->NPC_type);
								}
							}
							else
							{
								if (d_JediAI.integer || d_combatinfo.integer)
								{
									Com_Printf("%s force-jumping over roll-stab!\n", NPCS.NPC->NPC_type);
								}
							}
							TIMER_Set(NPCS.NPC, "specialEvasion", 3000);
							return EVASION_FJUMP;
						}
						//normal jump
						NPCS.ucmd.upmove = 127;
						if (d_JediAI.integer || d_combatinfo.integer)
						{
							Com_Printf("%s jumping over roll-stab!\n", NPCS.NPC->NPC_type);
						}
						TIMER_Set(NPCS.NPC, "specialEvasion", 2000);
						return EVASION_JUMP;
					}
				}
			}
		}
	}
	return EVASION_NONE;
}


static qboolean Jedi_SaberBlock(void)
{
	vec3_t hitloc, saber_tip_old, saber_tip, top, bottom, axis_point, saber_point, dir;
	vec3_t point_dir, base_dir, tip_dir;
	const vec3_t saber_maxs = { 4, 4, 4 };
	const vec3_t saber_mins = { -4, -4, -4 };
	float base_dir_perc = 0.0f;
	float dist = 0.0f, best_dist = Q3_INFINITE;
	int closest_saber_num = 0, closest_blade_num = 0;

	if (!TIMER_Done(NPCS.NPC, "parryReCalcTime"))
	{
		//can't do our own re-think of which parry to use yet
		return qfalse;
	}

	if (NPCS.NPC->client->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE] > level.time)
	{
		//can't move the saber to another position yet
		return qfalse;
	}

	if (NPCS.NPC->enemy->health <= 0 || !NPCS.NPC->enemy->client)
	{
		//don't keep blocking him once he's dead (or if not a client)
		return qfalse;
	}

	for (int saber_num = 0; saber_num < MAX_SABERS; saber_num++)
	{
		for (int bladeNum = 0; bladeNum < NPCS.NPC->enemy->client->saber[saber_num].numBlades; bladeNum++)
		{
			if (NPCS.NPC->enemy->client->saber[saber_num].type != SABER_NONE
				&& NPCS.NPC->enemy->client->saber[saber_num].blade[bladeNum].length > 0)
			{
				//valid saber and this blade is on
				VectorMA(NPCS.NPC->enemy->client->saber[saber_num].blade[bladeNum].muzzlePointOld,
					NPCS.NPC->enemy->client->saber[saber_num].blade[bladeNum].length,
					NPCS.NPC->enemy->client->saber[saber_num].blade[bladeNum].muzzleDirOld, saber_tip_old);
				VectorMA(NPCS.NPC->enemy->client->saber[saber_num].blade[bladeNum].muzzlePoint,
					NPCS.NPC->enemy->client->saber[saber_num].blade[bladeNum].length,
					NPCS.NPC->enemy->client->saber[saber_num].blade[bladeNum].muzzleDir, saber_tip);

				VectorCopy(NPCS.NPC->r.currentOrigin, top);
				top[2] = NPCS.NPC->r.absmax[2];
				VectorCopy(NPCS.NPC->r.currentOrigin, bottom);
				bottom[2] = NPCS.NPC->r.absmin[2];

				dist = ShortestLineSegBewteen2LineSegs(
					NPCS.NPC->enemy->client->saber[saber_num].blade[bladeNum].muzzlePoint, saber_tip, bottom, top,
					saber_point, axis_point);
				if (dist < best_dist)
				{
					best_dist = dist;
					closest_saber_num = saber_num;
					closest_blade_num = bladeNum;
				}
			}
		}
	}

	if (best_dist > NPCS.NPC->r.maxs[0] * 5)
	{
		if (d_JediAI.integer || d_combatinfo.integer || g_DebugSaberCombat.integer)
		{
			Com_Printf(S_COLOR_RED"enemy saber dist: %4.2f\n", best_dist);
		}
		TIMER_Set(NPCS.NPC, "parryTime", -1);
		return qfalse;
	}

	dist = best_dist;

	if (d_JediAI.integer || d_combatinfo.integer || g_DebugSaberCombat.integer)
	{
		Com_Printf(S_COLOR_GREEN"enemy saber dist: %4.2f\n", dist);
	}

	//now use the closest blade for my evasion check
	VectorMA(NPCS.NPC->enemy->client->saber[closest_saber_num].blade[closest_blade_num].muzzlePointOld,
		NPCS.NPC->enemy->client->saber[closest_saber_num].blade[closest_blade_num].length,
		NPCS.NPC->enemy->client->saber[closest_saber_num].blade[closest_blade_num].muzzleDirOld, saber_tip_old);
	VectorMA(NPCS.NPC->enemy->client->saber[closest_saber_num].blade[closest_blade_num].muzzlePoint,
		NPCS.NPC->enemy->client->saber[closest_saber_num].blade[closest_blade_num].length,
		NPCS.NPC->enemy->client->saber[closest_saber_num].blade[closest_blade_num].muzzleDir, saber_tip);

	VectorCopy(NPCS.NPC->r.currentOrigin, top);
	top[2] = NPCS.NPC->r.absmax[2];
	VectorCopy(NPCS.NPC->r.currentOrigin, bottom);
	bottom[2] = NPCS.NPC->r.absmin[2];

	dist = ShortestLineSegBewteen2LineSegs(
		NPCS.NPC->enemy->client->saber[closest_saber_num].blade[closest_blade_num].muzzlePoint, saber_tip, bottom, top,
		saber_point, axis_point);
	VectorSubtract(saber_point, NPCS.NPC->enemy->client->saber[closest_saber_num].blade[closest_blade_num].muzzlePoint,
		point_dir);
	const float point_dist = VectorLength(point_dir);

	if (NPCS.NPC->enemy->client->saber[closest_saber_num].blade[closest_blade_num].length <= 0)
	{
		base_dir_perc = 0.5f;
	}
	else
	{
		base_dir_perc = point_dist / NPCS.NPC->enemy->client->saber[closest_saber_num].blade[closest_blade_num].length;
	}
	VectorSubtract(NPCS.NPC->enemy->client->saber[closest_saber_num].blade[closest_blade_num].muzzlePoint,
		NPCS.NPC->enemy->client->saber[closest_saber_num].blade[closest_blade_num].muzzlePointOld, base_dir);
	VectorSubtract(saber_tip, saber_tip_old, tip_dir);
	VectorScale(base_dir, base_dir_perc, base_dir);
	VectorMA(base_dir, 1.0f - base_dir_perc, tip_dir, dir);
	VectorMA(saber_point, 200, dir, hitloc);

	//get the actual point of impact
	trace_t tr;

	trap->Trace(&tr, saber_point, saber_mins, saber_maxs, hitloc, NPCS.NPC->enemy->s.number, CONTENTS_BODY, qfalse, 0, 0);

	if (tr.allsolid || tr.startsolid || tr.fraction >= 1.0f)
	{
		vec3_t saber_hit_point;
		//estimate
		vec3_t dir2_me;
		VectorSubtract(axis_point, saber_point, dir2_me);
		dist = VectorNormalize(dir2_me);

		if (DotProduct(dir, dir2_me) < 0.2f)
		{
			//saber is not swinging in my direction
			TIMER_Set(NPCS.NPC, "parryTime", -1);
			return qfalse;
		}
		ShortestLineSegBewteen2LineSegs(saber_point, hitloc, bottom, top, saber_hit_point, hitloc);
	}
	else
	{
		VectorCopy(tr.endpos, hitloc);
	}

	

	evasionType_t evasion_type;

	if ((evasion_type = jedi_saber_block_go(NPCS.NPC, &NPCS.ucmd, hitloc, dir, NULL, dist)) != EVASION_NONE)
	{
		if (NPCS.NPC->client->NPC_class == CLASS_BOBAFETT
			|| NPCS.NPC->client->NPC_class == CLASS_MANDO)
		{
			jedi_dodge_evasion(NPCS.NPC, NULL, &tr, HL_NONE);
		}
		else
		{
			//did some sort of evasion
			if (evasion_type != EVASION_DODGE)
			{
				//(not dodge)
				if (!NPCS.NPC->client->ps.saberInFlight)
				{
					//make sure saber is on
					WP_ActivateSaber(NPCS.NPC);
				}

				//debounce our parry recalc time
				const int parry_re_calc_time = Jedi_ReCalcParryTime(NPCS.NPC, evasion_type);

				TIMER_Set(NPCS.NPC, "parryReCalcTime", Q_irand(0, parry_re_calc_time));

				if (d_JediAI.integer)
				{
					Com_Printf("Keep parry choice until: %d\n", level.time + parry_re_calc_time);
				}

				//determine how long to hold this anim
				if (TIMER_Done(NPCS.NPC, "parryTime"))
				{
					if (g_npcspskill.integer >= 1)
					{
						TIMER_Set(NPCS.NPC, "parryTime", Q_irand(1, 2) * parry_re_calc_time);
					}
					else
					{
						if (NPCS.NPC->client->NPC_class == CLASS_TAVION
							|| NPCS.NPC->client->NPC_class == CLASS_SHADOWTROOPER
							|| NPCS.NPC->client->NPC_class == CLASS_ALORA)
						{
							TIMER_Set(NPCS.NPC, "parryTime", Q_irand(parry_re_calc_time / 2, parry_re_calc_time * 1.5));
						}
						else
						{
							//others hold it longer
							TIMER_Set(NPCS.NPC, "parryTime", Q_irand(1, 2) * parry_re_calc_time);
						}
					}
				}
			}
			else
			{
				//dodged
				int dodge_time = NPCS.NPC->client->ps.torsoTimer;
				if (NPCS.NPCInfo->rank > RANK_LT_JG &&
					(NPCS.NPC->client->NPC_class != CLASS_DESANN &&
						NPCS.NPC->client->NPC_class != CLASS_VADER))
				{
					//higher-level guys can dodge faster
					dodge_time -= 200;
				}
				TIMER_Set(NPCS.NPC, "parryReCalcTime", dodge_time);
				TIMER_Set(NPCS.NPC, "parryTime", dodge_time);
			}
		}
	}
	if (evasion_type != EVASION_DUCK_PARRY
		&& evasion_type != EVASION_JUMP_PARRY
		&& evasion_type != EVASION_JUMP
		&& evasion_type != EVASION_DUCK
		&& evasion_type != EVASION_FJUMP
		&& WP_ForcePowerUsable(NPCS.NPC, FP_LEVITATION))
	{
		if (Jedi_CheckEvadeSpecialAttacks() != EVASION_NONE)
		{
			//got a new evasion!
			//see if it's okay to jump
			Jedi_CheckJumpEvasionSafety(&NPCS.ucmd, evasion_type);
		}
	}
	return qtrue;
}


static qboolean Jedi_EvasionRoll(gentity_t* ai_ent)
{
	if (!ai_ent->enemy->client)
	{
		ai_ent->npc_roll_start = qfalse;
		return qfalse;
	}
	if (ai_ent->client->NPC_class == CLASS_STORMTROOPER)
	{
		ai_ent->npc_roll_start = qfalse;
		return qfalse;
	}
	if (ai_ent->enemy->client
		&& ai_ent->enemy->s.weapon == WP_SABER
		&& ai_ent->enemy->client->ps.saberLockTime > level.time)
	{
		//don't try to block/evade an enemy who is in a saberLock
		ai_ent->npc_roll_start = qfalse;
		return qfalse;
	}
	if (ai_ent->client->ps.saberEventFlags & SEF_LOCK_WON && ai_ent->enemy->painDebounceTime > level.time)
	{
		//pressing the advantage of winning a saber lock
		ai_ent->npc_roll_start = qfalse;
		return qfalse;
	}

	if (ai_ent->npc_roll_time >= level.time)
	{
		// Already in a roll...
		ai_ent->npc_roll_start = qfalse;
		return qfalse;
	}

	// Init...
	ai_ent->npc_roll_direction = EVASION_ROLL_DIR_NONE;
	ai_ent->npc_roll_start = qfalse;

	qboolean can_roll_back = qfalse;
	qboolean can_roll_left = qfalse;
	qboolean can_roll_right = qfalse;

	trace_t tr;
	vec3_t fwd, right, up, start, end;
	AngleVectors(ai_ent->r.currentAngles, fwd, right, up);

	VectorSet(start, ai_ent->r.currentOrigin[0], ai_ent->r.currentOrigin[1], ai_ent->r.currentOrigin[2] + 24.0);
	VectorMA(start, -128, fwd, end);
	trap->Trace(&tr, start, NULL, NULL, end, ai_ent->s.number, MASK_NPCSOLID, qfalse, 0, 0);

	if (tr.fraction == 1.0 && !NPC_CheckFallPositionOK(ai_ent, end))
	{
		// We can roll back...
		can_roll_back = qtrue;
	}

	VectorMA(start, -128, right, end);
	trap->Trace(&tr, start, NULL, NULL, end, ai_ent->s.number, MASK_NPCSOLID, qfalse, 0, 0);

	if (tr.fraction == 1.0 && !NPC_CheckFallPositionOK(ai_ent, end))
	{
		// We can roll back...
		can_roll_left = qtrue;
	}

	VectorMA(start, 128, right, end);
	trap->Trace(&tr, start, NULL, NULL, end, ai_ent->s.number, MASK_NPCSOLID, qfalse, 0, 0);

	if (tr.fraction == 1.0 && !NPC_CheckFallPositionOK(ai_ent, end))
	{
		// We can roll back...
		can_roll_right = qtrue;
	}

	if (can_roll_back && can_roll_left && can_roll_right)
	{
		const int choice = Q_irand(0, 2);

		switch (choice)
		{
		case 2:
			ai_ent->npc_roll_time = level.time + 5000;
			ai_ent->npc_roll_start = qtrue;
			ai_ent->npc_roll_direction = EVASION_ROLL_DIR_RIGHT;
			break;
		case 1:
			ai_ent->npc_roll_time = level.time + 5000;
			ai_ent->npc_roll_start = qtrue;
			ai_ent->npc_roll_direction = EVASION_ROLL_DIR_LEFT;
			break;
		default:
			ai_ent->npc_roll_time = level.time + 5000;
			ai_ent->npc_roll_start = qtrue;
			ai_ent->npc_roll_direction = EVASION_ROLL_DIR_BACK;
			break;
		}

		return qtrue;
	}
	if (can_roll_back && can_roll_left)
	{
		const int choice = Q_irand(0, 1);

		switch (choice)
		{
		case 1:
			ai_ent->npc_roll_time = level.time + 5000;
			ai_ent->npc_roll_start = qtrue;
			ai_ent->npc_roll_direction = EVASION_ROLL_DIR_LEFT;
			break;
		default:
			ai_ent->npc_roll_time = level.time + 5000;
			ai_ent->npc_roll_start = qtrue;
			ai_ent->npc_roll_direction = EVASION_ROLL_DIR_BACK;
			break;
		}

		return qtrue;
	}
	if (can_roll_back && can_roll_right)
	{
		const int choice = Q_irand(0, 1);

		switch (choice)
		{
		case 1:
			ai_ent->npc_roll_time = level.time + 5000;
			ai_ent->npc_roll_start = qtrue;
			ai_ent->npc_roll_direction = EVASION_ROLL_DIR_RIGHT;
			break;
		default:
			ai_ent->npc_roll_time = level.time + 5000;
			ai_ent->npc_roll_start = qtrue;
			ai_ent->npc_roll_direction = EVASION_ROLL_DIR_BACK;
			break;
		}

		return qtrue;
	}
	if (can_roll_left && can_roll_right)
	{
		const int choice = Q_irand(0, 1);

		switch (choice)
		{
		case 1:
			ai_ent->npc_roll_time = level.time + 5000;
			ai_ent->npc_roll_start = qtrue;
			ai_ent->npc_roll_direction = EVASION_ROLL_DIR_RIGHT;
			break;
		default:
			ai_ent->npc_roll_time = level.time + 5000;
			ai_ent->npc_roll_start = qtrue;
			ai_ent->npc_roll_direction = EVASION_ROLL_DIR_LEFT;
			break;
		}

		return qtrue;
	}

	if (can_roll_left)
	{
		ai_ent->npc_roll_time = level.time + 5000;
		ai_ent->npc_roll_start = qtrue;
		ai_ent->npc_roll_direction = EVASION_ROLL_DIR_LEFT;
		return qtrue;
	}
	if (can_roll_right)
	{
		ai_ent->npc_roll_time = level.time + 5000;
		ai_ent->npc_roll_start = qtrue;
		ai_ent->npc_roll_direction = EVASION_ROLL_DIR_RIGHT;
		return qtrue;
	}
	if (can_roll_back)
	{
		ai_ent->npc_roll_time = level.time + 5000;
		ai_ent->npc_roll_start = qtrue;
		ai_ent->npc_roll_direction = EVASION_ROLL_DIR_BACK;
		return qtrue;
	}
	return qfalse;
}



/*
-------------------------
Jedi_EvasionSaber

defend if other is using saber and attacking me!
-------------------------
*/
static void Jedi_EvasionSaber(vec3_t enemy_movedir, const float enemy_dist, vec3_t enemy_dir)
{
	vec3_t dir_enemy2_me;
	int evasion_chance = 30; //only step aside 30% if he's moving at me but not attacking
	qboolean enemy_attacking = qfalse;
	qboolean throwing_saber = qfalse;
	qboolean shooting_lightning = qfalse;

	if (!NPCS.NPC->enemy->client)
	{
		return;
	}
	if (NPCS.NPC->enemy->client
		&& NPCS.NPC->enemy->s.weapon == WP_SABER
		&& NPCS.NPC->enemy->client->ps.saberLockTime > level.time)
	{
		//don't try to block/evade an enemy who is in a saberLock
		return;
	}
	if (NPCS.NPC->client->ps.saberEventFlags & SEF_LOCK_WON
		&& NPCS.NPC->enemy->painDebounceTime > level.time)
	{
		//pressing the advantage of winning a saber lock
		return;
	}

	if (NPCS.NPC->enemy->client->ps.saberInFlight && !TIMER_Done(NPCS.NPC, "taunting"))
	{
		//if he's throwing his saber, stop taunting
		TIMER_Set(NPCS.NPC, "taunting", -level.time);
		if (!NPCS.NPC->client->ps.saberInFlight)
		{
			WP_ActivateSaber(NPCS.NPC);
		}
	}

	if (TIMER_Done(NPCS.NPC, "parryTime"))
	{
		if (NPCS.NPC->client->ps.saberBlocked != BLOCKED_ATK_BOUNCE &&
			NPCS.NPC->client->ps.saberBlocked != BLOCKED_PARRY_BROKEN)
		{
			//wasn't blocked myself
			NPCS.NPC->client->ps.saberBlocked = BLOCKED_NONE;
		}
	}

	if (NPCS.NPC->enemy->client->ps.weaponTime &&
		NPCS.NPC->enemy->client->ps.weaponstate == WEAPON_FIRING)
	{
		if ((!NPCS.NPC->client->ps.saberInFlight ||
			(NPCS.NPC->client->saber[1].model[0] != 0) &&
			(NPCS.NPC->client->ps.saberHolstered == 0)) &&
			Jedi_SaberBlock())
		{
			return;
		}
	}
	else if (Jedi_CheckEvadeSpecialAttacks() != EVASION_NONE)
	{
		return;
	}

	VectorSubtract(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin, dir_enemy2_me);
	VectorNormalize(dir_enemy2_me);

	if (NPCS.NPC->enemy->client->ps.weaponTime && NPCS.NPC->enemy->client->ps.weaponstate == WEAPON_FIRING)
	{
		//enemy is attacking
		enemy_attacking = qtrue;
		evasion_chance = 90;
	}

	if (NPCS.NPC->enemy->client->ps.fd.forcePowersActive & 1 << FP_LIGHTNING
		|| NPCS.NPC->enemy->client->ps.fd.forcePowersActive & 1 << FP_LIGHTNING_STRIKE)
	{
		//enemy is shooting lightning
		enemy_attacking = qtrue;
		shooting_lightning = qtrue;
		evasion_chance = 50;
	}

	if (NPCS.NPC->enemy->client->ps.saberInFlight
		&& NPCS.NPC->enemy->client->ps.saberEntityNum != ENTITYNUM_NONE
		&& NPCS.NPC->enemy->client->ps.saberEntityState != SES_RETURNING)
	{
		//enemy is shooting lightning
		enemy_attacking = qtrue;
		throwing_saber = qtrue;
	}

	//FIXME: this needs to take skill and rank(reborn type) into account much more
	if (Q_irand(0, 100) < evasion_chance)
	{
		//check to see if he's coming at me
		float facing_amt = 0.0f;
		if (VectorCompare(enemy_movedir, vec3_origin) || shooting_lightning || throwing_saber)
		{
			//he's not moving (or he's using a ranged attack), see if he's facing me
			vec3_t enemy_fwd;
			AngleVectors(NPCS.NPC->enemy->client->ps.viewangles, enemy_fwd, NULL, NULL);
			facing_amt = DotProduct(enemy_fwd, dir_enemy2_me);
		}
		else
		{
			//he's moving
			facing_amt = DotProduct(enemy_movedir, dir_enemy2_me);
		}

		if (flrand(0.25, 1) < facing_amt)
		{
			//coming at/facing me!
			int which_defense = 0;

			if (NPCS.NPC->client->ps.weaponTime ||
				NPCS.NPC->client->ps.saberInFlight ||
				(NPCS.NPC->client->NPC_class == CLASS_BOBAFETT ||
					NPCS.NPC->client->NPC_class == CLASS_MANDO) ||
				NPCS.NPC->client->NPC_class == CLASS_REBORN &&
				NPCS.NPC->s.weapon != WP_SABER ||
				NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER)
			{
				//I'm attacking or recovering from a parry, can only try to strafe/jump right now
				if (Q_irand(0, 10) < NPCS.NPCInfo->stats.aggression)
				{
					return;
				}
				which_defense = 100;
			}
			else
			{
				if (shooting_lightning)
				{
					//check for lightning attack
					//only valid defense is strafe and/or jump
					which_defense = 100;
					if (NPCS.NPC->s.weapon == WP_SABER)
					{
						Jedi_SaberBlock();
					}
					else
					{
						//already chose one
					}
				}
				else if (throwing_saber)
				{
					//he's thrown his saber!  See if it's coming at me
					vec3_t saber_dir2_me;
					vec3_t saber_move_dir;
					const gentity_t* saber = &g_entities[NPCS.NPC->enemy->client->ps.saberEntityNum];
					VectorSubtract(NPCS.NPC->r.currentOrigin, saber->r.currentOrigin, saber_dir2_me);
					const float sabers_dist = VectorNormalize(saber_dir2_me);
					VectorCopy(saber->s.pos.trDelta, saber_move_dir);
					VectorNormalize(saber_move_dir);
					if (!Q_irand(0, 3))
					{
						Jedi_Aggression(NPCS.NPC, 1);
					}
					if (DotProduct(saber_move_dir, saber_dir2_me) > 0.5)
					{
						//it's heading towards me
						if (sabers_dist < 100)
						{
							//it's close
							which_defense = Q_irand(3, 6);
						}
						else if (sabers_dist < 200)
						{
							//got some time, yet, try pushing
							which_defense = Q_irand(0, 8);
						}
					}
				}
				if (which_defense)
				{
					if (NPCS.NPC->s.weapon == WP_SABER)
					{
						Jedi_SaberBlock();
					}
					else
					{
						Jedi_EvasionRoll(NPCS.NPC);
					}
				}
				else if (enemy_dist > 80 || !enemy_attacking)
				{
					//he's pretty far, or not swinging, just strafe
					if (VectorCompare(enemy_movedir, vec3_origin))
					{
						//if he's not moving, not swinging and far enough away, no evasion necc.
						return;
					}
					if (Q_irand(0, 10) < NPCS.NPCInfo->stats.aggression)
					{
						return;
					}
					which_defense = 100;
				}
				else
				{
					//he's getting close and swinging at me
					vec3_t fwd;
					//see if I'm facing him
					AngleVectors(NPCS.NPC->client->ps.viewangles, fwd, NULL, NULL);
					if (DotProduct(enemy_dir, fwd) < 0.5)
					{
						//I'm not really facing him, best option is to strafe
						which_defense = Q_irand(5, 16);
					}
					else if (enemy_dist < 56)
					{
						//he's very close, maybe we should be more inclined to block or throw
						which_defense = Q_irand(Q_min(NPCS.NPCInfo->stats.aggression, 24), 24);
					}
					else
					{
						which_defense = Q_irand(2, 16);
					}
				}
			}

			if (which_defense >= 4 && which_defense <= 12)
			{
				//would try to block
				if (NPCS.NPC->client->ps.saberInFlight)
				{
					//can't, saber in not in hand, so fall back to strafe/jump
					which_defense = 100;
				}
			}

			switch (which_defense)
			{
			case 0:
			case 1:
			case 2:
			case 3:
				if (Jedi_DecideKick())
				{
					if (G_PickAutoMultiKick(NPCS.NPC, qfalse, qtrue) != LS_NONE
						|| (G_CanKickEntity(NPCS.NPC, NPCS.NPC->enemy) &&
							G_PickAutoKick(NPCS.NPC, NPCS.NPC->enemy) != LS_NONE))
					{
						TIMER_Set(NPCS.NPC, "kickDebounce", Q_irand(8000, 15000));
					}
				}
				else if ((NPCS.NPCInfo->rank == RANK_ENSIGN || NPCS.NPCInfo->rank > RANK_LT_JG) && TIMER_Done(NPCS.NPC, "parryTime"))
				{
					ForceThrow(NPCS.NPC, qfalse);
				}
				else if (!Jedi_SaberBlock())
				{
					Jedi_EvasionRoll(NPCS.NPC);
				}
				break;
			case 4:
			case 5:
			case 6:
			case 7:
			case 8:
			case 9:
			case 10:
			case 11:
			case 12:
				if (NPCS.NPC->s.weapon == WP_SABER)
				{
					Jedi_SaberBlock();
				}
				else
				{
					Jedi_EvasionRoll(NPCS.NPC);
				}
				break;
			default:
				if (!Q_irand(0, 5) || !Jedi_Strafe(300, 1000, 0, 1000, qfalse))
				{
					if (shooting_lightning || throwing_saber || enemy_dist < 80)
					{
						if (shooting_lightning || !Q_irand(0, 2) && NPCS.NPCInfo->stats.aggression < 4 && TIMER_Done(
							NPCS.NPC, "parryTime"))
						{
							if ((NPCS.NPCInfo->rank == RANK_ENSIGN || NPCS.NPCInfo->rank > RANK_LT_JG) && !shooting_lightning &&
								Q_irand(0, 2))
							{
								ForceThrow(NPCS.NPC, qfalse);
							}
							else if ((NPCS.NPCInfo->rank == RANK_CREWMAN || NPCS.NPCInfo->rank > RANK_LT_JG)
								&& !(NPCS.NPCInfo->scriptFlags & SCF_NO_ACROBATICS)
								&& NPCS.NPC->client->ps.fd.forceRageRecoveryTime < level.time
								&& !(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_RAGE)
								&& !PM_InKnockDown(&NPCS.NPC->client->ps))
							{
								if (NPCS.NPC->s.weapon != WP_SABER)
								{
									//i dont have a saber can only try to jump right now
									NPCS.NPC->client->ps.fd.forceJumpCharge = 480;
									TIMER_Set(NPCS.NPC, "jumpChaseDebounce", Q_irand(10000, 15000));

									if (Q_irand(0, 2))
									{
										NPCS.ucmd.forwardmove = 127;
										VectorClear(NPCS.NPC->client->ps.moveDir);
									}
									else
									{
										NPCS.ucmd.forwardmove = -127;
										VectorClear(NPCS.NPC->client->ps.moveDir);
									}
								}
								else
								{
									if (NPCS.NPC->s.weapon == WP_SABER)
									{
										Jedi_SaberBlock();
									}
									else
									{
										Jedi_EvasionRoll(NPCS.NPC);
									}
								}
							}
							else
							{
								if (!Jedi_SaberBlock())
								{
									Jedi_EvasionRoll(NPCS.NPC);
								}
							}
						}
						else if (enemy_attacking && NPCS.NPC->s.weapon == WP_SABER)
						{
							Jedi_SaberBlock();
						}
						else
						{
							Jedi_EvasionRoll(NPCS.NPC);
						}
					}
					else if (!Jedi_SaberBlock())
					{
						Jedi_EvasionRoll(NPCS.NPC);
					}
				}
				else if (!Jedi_SaberBlock())
				{
					Jedi_EvasionRoll(NPCS.NPC);
				}
				else
				{
					//strafed
					if (d_JediAI.integer || d_combatinfo.integer)
					{
						Com_Printf("def strafe\n");
					}
					if (!(NPCS.NPCInfo->scriptFlags & SCF_NO_ACROBATICS)
						&& NPCS.NPC->client->ps.fd.forceRageRecoveryTime < level.time
						&& !(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_RAGE)
						&& (NPCS.NPCInfo->rank == RANK_CREWMAN || NPCS.NPCInfo->rank > RANK_LT_JG)
						&& !PM_InKnockDown(&NPCS.NPC->client->ps) && !Q_irand(0, 5))
					{
						if (NPCS.NPC->client->NPC_class == CLASS_BOBAFETT || NPCS.NPC->client->NPC_class == CLASS_MANDO ||
							NPCS.NPC->client->NPC_class == CLASS_REBORN && NPCS.NPC->s.weapon != WP_SABER
							|| NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER)
						{
							NPCS.NPC->client->ps.fd.forceJumpCharge = 280;
						}
						else
						{
							if (NPCS.NPC->s.weapon == WP_SABER)
							{
								Jedi_SaberBlock();
							}
							else
							{
								NPCS.NPC->client->ps.fd.forceJumpCharge = 320;
							}
						}
						TIMER_Set(NPCS.NPC, "jumpChaseDebounce", Q_irand(2000, 5000));
					}
					else if (!Jedi_SaberBlock())
					{
						Jedi_EvasionRoll(NPCS.NPC);
					}
				}
				break;
			}
			TIMER_Set(NPCS.NPC, "walking", -level.time);
			TIMER_Set(NPCS.NPC, "taunting", -level.time);
		}
	}
}



/*
==========================================================================================
INTERNAL AI ROUTINES
==========================================================================================
*/
static gentity_t* Jedi_FindEnemyInCone(const gentity_t* self, gentity_t* fallback, const float minDot)
{
	// --- Early out: no client, no viewangles, no cone ---
	if (!self || !self->client)
	{
		return fallback;
	}

	// --- Forward direction ---
	vec3_t forward;
	AngleVectors(self->client->ps.viewangles, forward, NULL, NULL);

	// --- Search bounds (1024 radius cube) ---
	vec3_t mins = { 0 };
	vec3_t maxs = { 0 };

	for (int i = 0; i < 3; i++)
	{
		mins[i] = self->r.currentOrigin[i] - 1024.0f;
		maxs[i] = self->r.currentOrigin[i] + 1024.0f;
	}

	// --- Large array moved static to avoid stack bloat ---
	static int entity_list[MAX_GENTITIES];

	const int numListed = trap->EntitiesInBox(mins, maxs, entity_list, MAX_GENTITIES);

	// Track best enemy found
	gentity_t* bestEnemy = fallback;
	float bestDist = Q3_INFINITE;

	// --- Iterate through all entities in the box ---
	for (int e = 0; e < numListed; e++)
	{
		gentity_t* check = &g_entities[entity_list[e]];

		if (!check)
		{
			continue;
		}
		if (check == self)
		{
			continue;
		}
		if (check->inuse == qfalse)
		{
			continue;
		}
		if (!check->client)
		{
			continue;
		}
		if (check->client->playerTeam != self->client->enemyTeam)
		{
			continue;
		}
		if (check->health <= 0)
		{
			continue;
		}

		// --- PVS check ---
		if (trap->InPVS(check->r.currentOrigin, self->r.currentOrigin) == qfalse)
		{
			if (g_npcspskill.integer > 1)
			{
				const float range = (float)(g_npc_is_smart_range.integer);

				const float dist_sq = (float)(DistanceSquared(self->r.currentOrigin, check->r.currentOrigin));

				if (dist_sq <= range * range)
				{
					// Generate a suspicious sight event so NPCs can react even without PVS/LOS
					vec3_t sightPos;
					VectorCopy(self->r.currentOrigin, sightPos);

					AddSightEvent(check, sightPos, range, AEL_SUSPICIOUS, 0.0f);

					// Fall through and allow further processing
				}
				else
				{
					continue;
				}
			}
			else
			{
				continue;
			}
		}

		// --- Direction + distance ---
		vec3_t dir;
		VectorSubtract(check->r.currentOrigin, self->r.currentOrigin, dir);
		const float dist = VectorNormalize(dir);

		// --- Cone check ---
		if (DotProduct(dir, forward) < minDot)
		{
			continue;
		}

		// --- Line of sight check ---
		trace_t tr;
		trap->Trace(&tr, self->r.currentOrigin, vec3_origin, vec3_origin, check->r.currentOrigin, self->s.number, MASK_SHOT, qfalse, 0, 0);

		if (tr.fraction < 1.0f && tr.entityNum != check->s.number)
		{
			continue;
		}

		// --- Closest enemy wins ---
		if (dist < bestDist)
		{
			bestDist = dist;
			bestEnemy = check;
		}
	}

	return bestEnemy;
}



static void jedi_set_enemy_info(vec3_t enemy_dest, vec3_t enemy_dir, float* enemy_dist, vec3_t enemy_movedir,
	float* enemy_movespeed, const int prediction)
{
	if (!NPCS.NPC || !NPCS.NPC->enemy)
	{
		//no valid enemy
		return;
	}
	if (!NPCS.NPC->enemy->client)
	{
		VectorClear(enemy_movedir);
		*enemy_movespeed = 0;
		VectorCopy(NPCS.NPC->enemy->r.currentOrigin, enemy_dest);
		enemy_dest[2] += NPCS.NPC->enemy->r.mins[2] + 24; //get it's origin to a height I can work with
		VectorSubtract(enemy_dest, NPCS.NPC->r.currentOrigin, enemy_dir);
		//FIXME: enemy_dist calc needs to include all blade lengths, and include distance from hand to start of blade....
		*enemy_dist = VectorNormalize(enemy_dir); // - (NPCS.NPC->client->ps.saberLengthMax + NPCS.NPC->r.maxs[0]*1.5 + 16);
	}
	else
	{
		//see where enemy is headed
		VectorCopy(NPCS.NPC->enemy->client->ps.velocity, enemy_movedir);
		*enemy_movespeed = VectorNormalize(enemy_movedir);
		//figure out where he'll be, say, 3 frames from now
		VectorMA(NPCS.NPC->enemy->r.currentOrigin, *enemy_movespeed * 0.001 * prediction, enemy_movedir, enemy_dest);
		//figure out what dir the enemy's estimated position is from me and how far from the tip of my saber he is
		VectorSubtract(enemy_dest, NPCS.NPC->r.currentOrigin, enemy_dir); //NPCS.NPC->client->renderInfo.muzzlePoint
		//FIXME: enemy_dist calc needs to include all blade lengths, and include distance from hand to start of blade....
		*enemy_dist = VectorNormalize(enemy_dir) - (NPCS.NPC->client->saber[0].blade[0].lengthMax + NPCS.NPC->r.maxs[0] * 1.5 + 16);
	}
	//init this to qfalse
	enemy_in_striking_range = qfalse;
	if (*enemy_dist <= 0.0f)
	{
		enemy_in_striking_range = qtrue;
	}
	else
	{
		//if he's too far away, see if he's at least facing us or coming towards us
		if (*enemy_dist <= 32.0f)
		{
			//has to be facing us
			vec3_t e_angles = { 0, NPCS.NPC->r.currentAngles[YAW], 0 };
			if (InFOV3(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin, e_angles, 30, 90))
			{
				//in striking range
				enemy_in_striking_range = qtrue;
			}
		}
		if (*enemy_dist >= 64.0f)
		{
			//we have to be approaching each other
			float v_dot = 0.0f;

			if (!VectorCompare(NPCS.NPC->client->ps.velocity, vec3_origin))
			{
				//I am moving, see if I'm moving toward the enemy
				vec3_t e_dir;
				VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, e_dir);
				VectorNormalize(e_dir);
				v_dot = DotProduct(e_dir, NPCS.NPC->client->ps.velocity);
			}
			else if (NPCS.NPC->enemy->client && !VectorCompare(NPCS.NPC->enemy->client->ps.velocity, vec3_origin))
			{
				//I'm not moving, but the enemy is, see if he's moving towards me
				vec3_t me_dir;
				VectorSubtract(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin, me_dir);
				VectorNormalize(me_dir);
				v_dot = DotProduct(me_dir, NPCS.NPC->enemy->client->ps.velocity);
			}
			else
			{
				//neither of us is moving, below check will fail, so just return
				return;
			}
			if (v_dot >= *enemy_dist)
			{
				//moving towards each other
				enemy_in_striking_range = qtrue;
			}
		}
	}
}



void NPC_EvasionSaber(void)
{
	if (NPCS.ucmd.upmove <= 0 //not jumping
		&& (!NPCS.ucmd.upmove || !NPCS.ucmd.rightmove)) //either just ducking or just strafing (i.e.: not rolling
	{
		//see if we need to avoid their saber
		vec3_t enemy_dir, enemy_movedir, enemy_dest;
		float enemy_dist = 0.0f, enemy_movespeed = 0.0f;
		//set enemy
		jedi_set_enemy_info(enemy_dest, enemy_dir, &enemy_dist, enemy_movedir, &enemy_movespeed, 300);
		Jedi_EvasionSaber(enemy_movedir, enemy_dist, enemy_dir);
	}
}


// ============================================================================
// Jedi_FaceEnemy
//
// Updates desired yaw/pitch to face the enemy. Modernized to eliminate
// uninitialized 'angles' (MSVC C6001), add safety checks, explicit qboolean
// usage, and debug prints instead of asserts.
// ============================================================================
static void Jedi_FaceEnemy(const qboolean doPitch)
{
	vec3_t enemy_eyes;
	vec3_t eyes;
	vec3_t angles;

	// ----------------------------------------------------------------------
	// Safety: validate NPC and enemy
	// ----------------------------------------------------------------------
	if (NPCS.NPC == NULL)
	{
		Com_Printf("Jedi_FaceEnemy ERROR: NPCS.NPC == NULL\n");
		return;
	}

	if (NPCS.NPC->enemy == NULL)
	{
		Com_Printf("Jedi_FaceEnemy ERROR: NPCS.NPC->enemy == NULL\n");
		return;
	}

	if (NPCS.NPC->client == NULL)
	{
		Com_Printf("Jedi_FaceEnemy ERROR: NPCS.NPC->client == NULL\n");
		return;
	}

	// ----------------------------------------------------------------------
	// Initialize angles to current facing (fixes MSVC C6001)
	// ----------------------------------------------------------------------
	VectorCopy(NPCS.NPC->client->ps.viewangles, angles);

	// ----------------------------------------------------------------------
	// Grip override: freeze facing direction
	// ----------------------------------------------------------------------
	if ((NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_GRIP)) &&
		NPCS.NPC->client->ps.fd.forcePowerLevel[FP_GRIP] > FORCE_LEVEL_1)
	{
		NPCS.NPCInfo->desiredPitch = NPCS.NPC->client->ps.viewangles[PITCH];
		NPCS.NPCInfo->desiredYaw = NPCS.NPC->client->ps.viewangles[YAW];
		return;
	}

	// ----------------------------------------------------------------------
	// Compute eye positions
	// ----------------------------------------------------------------------
	CalcEntitySpot(NPCS.NPC, SPOT_HEAD, eyes);
	CalcEntitySpot(NPCS.NPC->enemy, SPOT_HEAD, enemy_eyes);

	// ----------------------------------------------------------------------
	// Boba/Mando leading logic
	// ----------------------------------------------------------------------
	if ((NPCS.NPC->client->NPC_class == CLASS_BOBAFETT ||
		NPCS.NPC->client->NPC_class == CLASS_MANDO ||
		NPCS.NPC->client->pers.botclass == BCLASS_BOBAFETT ||
		NPCS.NPC->client->pers.botclass == BCLASS_MANDOLORIAN1 ||
		NPCS.NPC->client->pers.botclass == BCLASS_MANDOLORIAN2) &&
		TIMER_Done(NPCS.NPC, "flameTime") == qtrue &&
		NPCS.NPC->s.weapon != WP_NONE &&
		NPCS.NPC->s.weapon != WP_DISRUPTOR &&
		(NPCS.NPC->s.weapon != WP_ROCKET_LAUNCHER ||
			!(NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)) &&
		NPCS.NPC->s.weapon != WP_THERMAL &&
		NPCS.NPC->s.weapon != WP_TRIP_MINE &&
		NPCS.NPC->s.weapon != WP_DET_PACK &&
		NPCS.NPC->s.weapon != WP_STUN_BATON &&
		NPCS.NPC->s.weapon != WP_MELEE)
	{
		// Lead target when injured
		if (NPCS.NPC->health < NPCS.NPC->client->pers.maxHealth * 0.5f)
		{
			const float missileSpeed =
				WP_SpeedOfMissileForWeapon(NPCS.NPC->s.weapon,
					(NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE));

			if (missileSpeed > 0.0f)
			{
				float eDist = Distance(eyes, enemy_eyes);
				eDist /= missileSpeed;

				VectorMA(enemy_eyes,
					eDist * flrand(0.95f, 1.25f),
					NPCS.NPC->enemy->client->ps.velocity,
					enemy_eyes);
			}
		}
	}

	// ----------------------------------------------------------------------
	// Determine facing direction based on animation state
	// ----------------------------------------------------------------------
	if (!NPCS.NPC->client->ps.saberInFlight &&
		(NPCS.NPC->client->ps.legsAnim == BOTH_A2_STABBACK1 ||
			NPCS.NPC->client->ps.legsAnim == BOTH_A2_STABBACK1B ||
			NPCS.NPC->client->ps.legsAnim == BOTH_CROUCHATTACKBACK1 ||
			NPCS.NPC->client->ps.legsAnim == BOTH_ATTACK_BACK))
	{
		// Point away
		GetAnglesForDirection(enemy_eyes, eyes, angles);
	}
	else if (NPCS.NPC->client->ps.legsAnim == BOTH_A7_KICK_R)
	{
		// keep enemy to right (fallback angles already initialized)
	}
	else if (NPCS.NPC->client->ps.legsAnim == BOTH_A7_KICK_L)
	{
		// keep enemy to left (fallback angles already initialized)
	}
	else if (NPCS.NPC->client->ps.legsAnim == BOTH_A7_KICK_RL ||
		NPCS.NPC->client->ps.legsAnim == BOTH_A7_KICK_BF ||
		NPCS.NPC->client->ps.legsAnim == BOTH_A7_KICK_S)
	{
		// ??? (fallback angles already initialized)
	}
	else
	{
		// Point toward enemy
		GetAnglesForDirection(eyes, enemy_eyes, angles);
	}

	// ----------------------------------------------------------------------
	// Apply desired yaw/pitch
	// ----------------------------------------------------------------------
	NPCS.NPCInfo->desiredYaw = AngleNormalize360(angles[YAW]);

	if (doPitch == qtrue)
	{
		NPCS.NPCInfo->desiredPitch = AngleNormalize360(angles[PITCH]);

		if (NPCS.NPC->client->ps.saberInFlight)
		{
			NPCS.NPCInfo->desiredPitch += 10.0f;
		}
	}
}



static void Jedi_DebounceDirectionChanges(void)
{
	if (NPCS.ucmd.forwardmove > 0)
	{
		if (!TIMER_Done(NPCS.NPC, "moveback") || !TIMER_Done(NPCS.NPC, "movenone"))
		{
			NPCS.ucmd.forwardmove = 0;
			//now we have to normalize the total movement again
			if (NPCS.ucmd.rightmove > 0)
			{
				NPCS.ucmd.rightmove = 127;
			}
			else if (NPCS.ucmd.rightmove < 0)
			{
				NPCS.ucmd.rightmove = -127;
			}
			VectorClear(NPCS.NPC->client->ps.moveDir);
			TIMER_Set(NPCS.NPC, "moveback", -level.time);
			if (TIMER_Done(NPCS.NPC, "movenone"))
			{
				TIMER_Set(NPCS.NPC, "movenone", Q_irand(1000, 2000));
			}
		}
		else if (TIMER_Done(NPCS.NPC, "moveforward"))
		{
			//FIXME: should be if it's zero?
			if (TIMER_Done(NPCS.NPC, "lastmoveforward"))
			{
				const int hold_dir_time = Q_irand(500, 2000);
				TIMER_Set(NPCS.NPC, "moveforward", hold_dir_time);
				//so we don't keep doing this over and over again - new nav stuff makes them coast to a stop, so they could be just slowing down from the last "moveback" timer's ending...
				TIMER_Set(NPCS.NPC, "lastmoveforward", hold_dir_time + Q_irand(1000, 2000));
			}
		}
		else
		{
			//NOTE: edge checking should stop me if this is bad... but what if it sends us colliding into the enemy?
			//if being forced to move forward, do a full-speed moveforward
			NPCS.ucmd.forwardmove = 127;
			VectorClear(NPCS.NPC->client->ps.moveDir);
		}
	}
	else if (NPCS.ucmd.forwardmove < 0)
	{
		if (!TIMER_Done(NPCS.NPC, "moveforward") || !TIMER_Done(NPCS.NPC, "movenone"))
		{
			NPCS.ucmd.forwardmove = 0;
			//now we have to normalize the total movement again
			if (NPCS.ucmd.rightmove > 0)
			{
				NPCS.ucmd.rightmove = 127;
			}
			else if (NPCS.ucmd.rightmove < 0)
			{
				NPCS.ucmd.rightmove = -127;
			}
			VectorClear(NPCS.NPC->client->ps.moveDir);
			TIMER_Set(NPCS.NPC, "moveforward", -level.time);
			if (TIMER_Done(NPCS.NPC, "movenone"))
			{
				TIMER_Set(NPCS.NPC, "movenone", Q_irand(1000, 2000));
			}
		}
		else if (TIMER_Done(NPCS.NPC, "moveback"))
		{
			//FIXME: should be if it's zero?
			if (TIMER_Done(NPCS.NPC, "lastmoveback"))
			{
				const int hold_dir_time = Q_irand(500, 2000);
				TIMER_Set(NPCS.NPC, "moveback", hold_dir_time);
				//so we don't keep doing this over and over again - new nav stuff makes them coast to a stop, so they could be just slowing down from the last "moveback" timer's ending...
				TIMER_Set(NPCS.NPC, "lastmoveback", hold_dir_time + Q_irand(1000, 2000));
			}
		}
		else
		{
			//NOTE: edge checking should stop me if this is bad...
			//if being forced to move back, do a full-speed moveback
			NPCS.ucmd.forwardmove = -127;
			VectorClear(NPCS.NPC->client->ps.moveDir);
		}
	}
	else if (!TIMER_Done(NPCS.NPC, "moveforward"))
	{
		//NOTE: edge checking should stop me if this is bad... but what if it sends us colliding into the enemy?
		NPCS.ucmd.forwardmove = 127;
		VectorClear(NPCS.NPC->client->ps.moveDir);
	}
	else if (!TIMER_Done(NPCS.NPC, "moveback"))
	{
		//NOTE: edge checking should stop me if this is bad...
		NPCS.ucmd.forwardmove = -127;
		VectorClear(NPCS.NPC->client->ps.moveDir);
	}
	//Time-debounce changes in right/left dir
	if (NPCS.ucmd.rightmove > 0)
	{
		if (!TIMER_Done(NPCS.NPC, "moveleft") || !TIMER_Done(NPCS.NPC, "movecenter"))
		{
			NPCS.ucmd.rightmove = 0;
			//now we have to normalize the total movement again
			if (NPCS.ucmd.forwardmove > 0)
			{
				NPCS.ucmd.forwardmove = 127;
			}
			else if (NPCS.ucmd.forwardmove < 0)
			{
				NPCS.ucmd.forwardmove = -127;
			}
			VectorClear(NPCS.NPC->client->ps.moveDir);
			TIMER_Set(NPCS.NPC, "moveleft", -level.time);
			if (TIMER_Done(NPCS.NPC, "movecenter"))
			{
				TIMER_Set(NPCS.NPC, "movecenter", Q_irand(1000, 2000));
			}
		}
		else if (TIMER_Done(NPCS.NPC, "moveright"))
		{
			//FIXME: should be if it's zero?
			if (TIMER_Done(NPCS.NPC, "lastmoveright"))
			{
				const int hold_dir_time = Q_irand(250, 1500);
				TIMER_Set(NPCS.NPC, "moveright", hold_dir_time);
				//so we don't keep doing this over and over again - new nav stuff makes them coast to a stop, so they could be just slowing down from the last "moveback" timer's ending...
				TIMER_Set(NPCS.NPC, "lastmoveright", hold_dir_time + Q_irand(1000, 2000));
			}
		}
		else
		{
			//NOTE: edge checking should stop me if this is bad...
			//if being forced to move back, do a full-speed moveright
			NPCS.ucmd.rightmove = 127;
			VectorClear(NPCS.NPC->client->ps.moveDir);
		}
	}
	else if (NPCS.ucmd.rightmove < 0)
	{
		if (!TIMER_Done(NPCS.NPC, "moveright") || !TIMER_Done(NPCS.NPC, "movecenter"))
		{
			NPCS.ucmd.rightmove = 0;
			//now we have to normalize the total movement again
			if (NPCS.ucmd.forwardmove > 0)
			{
				NPCS.ucmd.forwardmove = 127;
			}
			else if (NPCS.ucmd.forwardmove < 0)
			{
				NPCS.ucmd.forwardmove = -127;
			}
			VectorClear(NPCS.NPC->client->ps.moveDir);
			TIMER_Set(NPCS.NPC, "moveright", -level.time);
			if (TIMER_Done(NPCS.NPC, "movecenter"))
			{
				TIMER_Set(NPCS.NPC, "movecenter", Q_irand(1000, 2000));
			}
		}
		else if (TIMER_Done(NPCS.NPC, "moveleft"))
		{
			//FIXME: should be if it's zero?
			if (TIMER_Done(NPCS.NPC, "lastmoveleft"))
			{
				const int hold_dir_time = Q_irand(250, 1500);
				TIMER_Set(NPCS.NPC, "moveleft", hold_dir_time);
				//so we don't keep doing this over and over again - new nav stuff makes them coast to a stop, so they could be just slowing down from the last "moveback" timer's ending...
				TIMER_Set(NPCS.NPC, "lastmoveleft", hold_dir_time + Q_irand(1000, 2000));
			}
		}
		else
		{
			//NOTE: edge checking should stop me if this is bad...
			//if being forced to move back, do a full-speed moveleft
			NPCS.ucmd.rightmove = -127;
			VectorClear(NPCS.NPC->client->ps.moveDir);
		}
	}
	else if (!TIMER_Done(NPCS.NPC, "moveright"))
	{
		//NOTE: edge checking should stop me if this is bad...
		NPCS.ucmd.rightmove = 127;
		VectorClear(NPCS.NPC->client->ps.moveDir);
	}
	else if (!TIMER_Done(NPCS.NPC, "moveleft"))
	{
		//NOTE: edge checking should stop me if this is bad...
		NPCS.ucmd.rightmove = -127;
		VectorClear(NPCS.NPC->client->ps.moveDir);
	}
}



void Jedi_TimersApply(void)
{
	//use careful anim/slower movement if not already moving
	if (!NPCS.ucmd.forwardmove && !TIMER_Done(NPCS.NPC, "walking"))
	{
		NPCS.ucmd.buttons |= BUTTON_WALKING;
	}

	if (!TIMER_Done(NPCS.NPC, "taunting"))
	{
		NPCS.ucmd.buttons |= BUTTON_WALKING;
	}

	if (!NPCS.ucmd.rightmove)
	{
		//only if not already strafing
		if (!TIMER_Done(NPCS.NPC, "strafeLeft"))
		{
			if (NPCS.NPCInfo->desiredYaw > NPCS.NPC->client->ps.viewangles[YAW] + 60)
			{
				//we want to turn left, don't apply the strafing
			}
			else
			{
				//go ahead and strafe left
				NPCS.ucmd.rightmove = -127;
				VectorClear(NPCS.NPC->client->ps.moveDir);
			}
		}
		else if (!TIMER_Done(NPCS.NPC, "strafeRight"))
		{
			if (NPCS.NPCInfo->desiredYaw < NPCS.NPC->client->ps.viewangles[YAW] - 60)
			{
				//we want to turn right, don't apply the strafing
			}
			else
			{
				//go ahead and strafe left
				NPCS.ucmd.rightmove = 127;
				VectorClear(NPCS.NPC->client->ps.moveDir);
			}
		}
	}

	Jedi_DebounceDirectionChanges();

	if (!TIMER_Done(NPCS.NPC, "gripping"))
	{
		NPCS.ucmd.buttons |= BUTTON_FORCEGRIP;
	}

	if (!TIMER_Done(NPCS.NPC, "grasping"))
	{
	}

	if (!TIMER_Done(NPCS.NPC, "draining"))
	{
		NPCS.ucmd.buttons |= BUTTON_FORCE_DRAIN;
	}

	if (!TIMER_Done(NPCS.NPC, "holdLightning"))
	{
		NPCS.ucmd.buttons |= BUTTON_FORCE_LIGHTNING;
	}
}



static void Jedi_CombatTimersUpdate(const int enemy_dist)
{
	if (Jedi_CultistDestroyer(NPCS.NPC) == qtrue)
	{
		// Cultist destroyers just get pushed into max aggression and bail
		Jedi_Aggression(NPCS.NPC, 5);
		return;
	}

	// BLOCK STANCE OVERRIDE
	// If the NPCS.NPC is in manual block stance, they must face their enemy.
	if ((NPCS.NPC->client->ps.ManualBlockingFlags & (1 << MBF_NPCBLOCKSTANCE)) != 0)
	{
		// Always face the enemy while blocking
		if (NPCS.NPC->enemy != NULL)
		{
			Jedi_FaceEnemy(qtrue);
			NPC_UpdateAngles(qtrue, qtrue);
		}
	}

	//===START MISSING CODE (restored / cleaned movement logic)===========================
	// Periodic roam/aggression adjustment based on enemy weapon and distance
	if (TIMER_Done(NPCS.NPC, "roamTime") == qtrue)
	{
		TIMER_Set(NPCS.NPC, "roamTime", Q_irand(2000, 5000));

		// Adjust aggression depending on what the enemy is doing
		if (NPCS.NPC->enemy != NULL && NPCS.NPC->enemy->client != NULL)
		{
			switch (NPCS.NPC->enemy->client->ps.weapon)
			{
			case WP_SABER:
				if ((!NPCS.NPC->enemy->client->ps.saberHolstered) == qfalse)
				{
					// Enemy is standing around with saber off: charge harder
					Jedi_Aggression(NPCS.NPC, 3);
				}
				else
				{
					// Saber on: moderate aggression
					Jedi_Aggression(NPCS.NPC, 2);
				}
				break;

			case WP_BLASTER:
			case WP_BRYAR_PISTOL:
			case WP_DISRUPTOR:
			case WP_BOWCASTER:
			case WP_REPEATER:
			case WP_Z6_ROTARY_CANNON:
			case WP_DEMP2:
			case WP_FLECHETTE:
			case WP_ROCKET_LAUNCHER:
			case WP_CONCUSSION:
			case WP_DROIDEKA:
			case WP_NOGHRI_STICK:
			case WP_TUSKEN_RIFLE:
				// Blaster logic:
				// 1) If they're not currently shooting at us, we can push in.
				if (NPCS.NPC->enemy->attackDebounceTime < level.time)
				{
					Jedi_Aggression(NPCS.NPC, 2);
				}
				// 2) If they're already close, we also push in.
				if (enemy_dist < 256)
				{
					Jedi_Aggression(NPCS.NPC, 2);
				}
				break;

			default:
				break;
			}
		}
	}

	// Always apply forward pressure when not attacking
	if (enemy_dist > 160)
	{
		if (d_JediAI.integer != 0)
		{
			Com_Printf("NPCS.NPC %s advancing (enemy_dist=%d)\n", NPCS.NPC->targetname, enemy_dist);
		}
		Jedi_Advance();

		if (NPCS.NPC->enemy != NULL)
		{
			Jedi_FaceEnemy(qtrue);
			NPC_UpdateAngles(qtrue, qtrue);
		}
	}
	else
	{
		// Mid/close?range behaviour is still gated by the noStrafe timer
		if (TIMER_Done(NPCS.NPC, "noStrafe") == qtrue)
		{
			TIMER_Set(NPCS.NPC, "noStrafe", Q_irand(1000, 2000));

			// Mid-range: controlled advance
			if (enemy_dist > 80)
			{
				NPCS.ucmd.forwardmove = 48;
			}
			// Close range: light pressure + strafing
			else
			{
				if (!Q_irand(0, 4))
				{//start a strafe
					if (Jedi_Strafe(1000, 3000, 0, 4000, qtrue))
					{
						if (d_JediAI.integer)
						{
							Com_Printf("off strafe\n");
						}
					}
				}
				else
				{//postpone any strafing for a while
					NPCS.ucmd.forwardmove = 48;
				}
			}

			// Always face the enemy while advancing/adjusting, if we have one
			if (NPCS.NPC->enemy != NULL)
			{
				Jedi_FaceEnemy(qtrue);
				NPC_UpdateAngles(qtrue, qtrue);
			}
		}
	}
	//===END MISSING CODE=================================================================

	// Saber event handling: adjust aggression and saber style based on combat outcomes
	if (NPCS.NPC->client->ps.saberEventFlags != 0)
	{
		int newFlags = NPCS.NPC->client->ps.saberEventFlags;

		// Parried
		if ((NPCS.NPC->client->ps.saberEventFlags & SEF_PARRIED) != 0)
		{
			TIMER_Set(NPCS.NPC, "parryTime", -1);

			if (NPCS.NPC->enemy != NULL &&
				(NPCS.NPC->enemy->client == NULL ||
					PM_SaberInKnockaway(NPCS.NPC->enemy->client->ps.saberMove) == qtrue))
			{
				// Enemy is in a bad state: advance
				Jedi_Aggression(NPCS.NPC, 2);

				if ((NPCS.NPC->client->ps.fd.saberAnimLevel == SS_STRONG
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_MEDIUM
					|| (NPCS.NPC->client->ps.fd.saberAnimLevel == SS_DESANN || NPCS.NPC->client->ps.fd.saberAnimLevel == SS_BLASTER) // SS_BLASTER (2026-10-14)
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_FAST
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_TAVION)
					&& npc_is_staff_style(NPCS.NPC) == qfalse
					&& npc_is_dual_style(NPCS.NPC) == qfalse)
				{
					// Use a faster attack style after a good parry
					Jedi_AdjustSaberAnimLevel(NPCS.NPC, NPCS.NPC->client->ps.fd.saberAnimLevel - 1);
				}
			}
			else
			{
				if (Q_irand(0, 1) == 0)
				{
					Jedi_Aggression(NPCS.NPC, -1);
				}

				if (Q_irand(0, 1) == 0)
				{
					if ((NPCS.NPC->client->ps.fd.saberAnimLevel == SS_STRONG
						|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_MEDIUM
						|| (NPCS.NPC->client->ps.fd.saberAnimLevel == SS_DESANN || NPCS.NPC->client->ps.fd.saberAnimLevel == SS_BLASTER) // SS_BLASTER (2026-10-14)
						|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_FAST
						|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_TAVION)
						&& npc_is_staff_style(NPCS.NPC) == qfalse
						&& npc_is_dual_style(NPCS.NPC) == qfalse)
					{
						Jedi_AdjustSaberAnimLevel(NPCS.NPC, NPCS.NPC->client->ps.fd.saberAnimLevel - 1);
					}
				}
			}

			if (d_JediAI.integer != 0 || d_combatinfo.integer != 0 || g_DebugSaberCombat.integer != 0)
			{
				Com_Printf("(%d) PARRY: aggression %d, no parry until %d\n",
					level.time, NPCS.NPCInfo->stats.aggression, level.time + 100);
			}

			newFlags &= ~SEF_PARRIED;
		}

		// Hit enemy
		if (NPCS.NPC->client->ps.weaponTime == 0 &&
			(NPCS.NPC->client->ps.saberEventFlags & SEF_HITENEMY) != 0)
		{
			if (Q_irand(0, 1) == 0)
			{
				Jedi_Aggression(NPCS.NPC, -1);

				if (d_JediAI.integer != 0)
				{
					Com_Printf("(%d) HIT: agg %d\n", level.time, NPCS.NPCInfo->stats.aggression);
				}

				if (Q_irand(0, 3) == 0 &&
					NPCS.NPCInfo->blockedSpeechDebounceTime < level.time &&
					jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] < level.time &&
					NPCS.NPC->painDebounceTime < level.time - 1000)
				{
					if (!npc_is_dark_jedi(NPCS.NPC))
					{
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_ANGER1, EV_ANGER3), 2000);
					}
					else
					{
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_GLOAT1, EV_GLOAT3), 10000);
					}
					jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] = NPCS.NPCInfo->blockedSpeechDebounceTime = level.time + 13000;
				}
			}

			if (Q_irand(0, 2) == 0)
			{
				if ((NPCS.NPC->client->ps.fd.saberAnimLevel == SS_STRONG
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_MEDIUM
					|| (NPCS.NPC->client->ps.fd.saberAnimLevel == SS_DESANN || NPCS.NPC->client->ps.fd.saberAnimLevel == SS_BLASTER) // SS_BLASTER (2026-10-14)
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_FAST
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_TAVION)
					&& npc_is_staff_style(NPCS.NPC) == qfalse
					&& npc_is_dual_style(NPCS.NPC) == qfalse)
				{
					// After a successful hit, sometimes go to a heavier style
					Jedi_AdjustSaberAnimLevel(NPCS.NPC, NPCS.NPC->client->ps.fd.saberAnimLevel + 1);
				}
			}

			newFlags &= ~SEF_HITENEMY;
		}

		// Blocked while attacking
		if ((NPCS.NPC->client->ps.saberEventFlags & SEF_BLOCKED) != 0)
		{
			if (PM_SaberInBrokenParry(NPCS.NPC->client->ps.saberMove) == qtrue ||
				NPCS.NPC->client->ps.saberBlocked == BLOCKED_PARRY_BROKEN)
			{
				if (NPCS.NPC->client->ps.saberInFlight == qtrue)
				{
					// Lost saber as well
					Jedi_Aggression(NPCS.NPC, -5);
				}
				else
				{
					Jedi_Aggression(NPCS.NPC, -2);
				}

				if ((NPCS.NPC->client->ps.fd.saberAnimLevel == SS_STRONG
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_MEDIUM
					|| (NPCS.NPC->client->ps.fd.saberAnimLevel == SS_DESANN || NPCS.NPC->client->ps.fd.saberAnimLevel == SS_BLASTER) // SS_BLASTER (2026-10-14)
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_FAST
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_TAVION)
					&& npc_is_staff_style(NPCS.NPC) == qfalse
					&& npc_is_dual_style(NPCS.NPC) == qfalse)
				{
					Jedi_AdjustSaberAnimLevel(NPCS.NPC, NPCS.NPC->client->ps.fd.saberAnimLevel + 1);
				}

				if (d_JediAI.integer != 0 || d_combatinfo.integer != 0 || g_DebugSaberCombat.integer != 0)
				{
					Com_Printf("(%d) KNOCK-BLOCKED: aggression %d\n",
						level.time, NPCS.NPCInfo->stats.aggression);
				}
			}
			else
			{
				if (Q_irand(0, 2) == 0)
				{
					Jedi_Aggression(NPCS.NPC, -1);

					if (d_JediAI.integer != 0 || d_combatinfo.integer != 0 || g_DebugSaberCombat.integer != 0)
					{
						Com_Printf("(%d) BLOCKED: aggression %d\n",
							level.time, NPCS.NPCInfo->stats.aggression);
					}
				}

				if (Q_irand(0, 1) == 0)
				{
					if ((NPCS.NPC->client->ps.fd.saberAnimLevel == SS_STRONG
						|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_MEDIUM
						|| (NPCS.NPC->client->ps.fd.saberAnimLevel == SS_DESANN || NPCS.NPC->client->ps.fd.saberAnimLevel == SS_BLASTER) // SS_BLASTER (2026-10-14)
						|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_FAST
						|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_TAVION)
						&& npc_is_staff_style(NPCS.NPC) == qfalse
						&& npc_is_dual_style(NPCS.NPC) == qfalse)
					{
						Jedi_AdjustSaberAnimLevel(NPCS.NPC, NPCS.NPC->client->ps.fd.saberAnimLevel + 1);
					}
				}
			}

			newFlags &= ~SEF_BLOCKED;
		}

		// Deflected a shot
		if ((NPCS.NPC->client->ps.saberEventFlags & SEF_DEFLECTED) != 0)
		{
			newFlags &= ~SEF_DEFLECTED;

			if (Q_irand(0, 3) == 0)
			{
				if ((NPCS.NPC->client->ps.fd.saberAnimLevel == SS_STRONG
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_MEDIUM
					|| (NPCS.NPC->client->ps.fd.saberAnimLevel == SS_DESANN || NPCS.NPC->client->ps.fd.saberAnimLevel == SS_BLASTER) // SS_BLASTER (2026-10-14)
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_FAST
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_TAVION)
					&& npc_is_staff_style(NPCS.NPC) == qfalse
					&& npc_is_dual_style(NPCS.NPC) == qfalse)
				{
					Jedi_AdjustSaberAnimLevel(NPCS.NPC, NPCS.NPC->client->ps.fd.saberAnimLevel - 1);
				}
			}
		}

		// Hit a wall
		if ((NPCS.NPC->client->ps.saberEventFlags & SEF_HITWALL) != 0)
		{
			newFlags &= ~SEF_HITWALL;
		}

		// Hit some other damageable object
		if ((NPCS.NPC->client->ps.saberEventFlags & SEF_HITOBJECT) != 0)
		{
			if (Q_irand(0, 3) == 0)
			{
				if ((NPCS.NPC->client->ps.fd.saberAnimLevel == SS_STRONG
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_MEDIUM
					|| (NPCS.NPC->client->ps.fd.saberAnimLevel == SS_DESANN || NPCS.NPC->client->ps.fd.saberAnimLevel == SS_BLASTER) // SS_BLASTER (2026-10-14)
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_FAST
					|| NPCS.NPC->client->ps.fd.saberAnimLevel == SS_TAVION)
					&& npc_is_staff_style(NPCS.NPC) == qfalse
					&& npc_is_dual_style(NPCS.NPC) == qfalse)
				{
					Jedi_AdjustSaberAnimLevel(NPCS.NPC, NPCS.NPC->client->ps.fd.saberAnimLevel - 1);
				}
			}

			newFlags &= ~SEF_HITOBJECT;
		}

		// Commit updated saber event flags
		NPCS.NPC->client->ps.saberEventFlags = newFlags;
	}
}



static void Jedi_CombatIdle(const int enemy_dist)
{
	if (!TIMER_Done(NPCS.NPC, "parryTime"))
	{
		return;
	}
	if (NPCS.NPC->client->ps.saberInFlight)
	{
		//don't do this idle stuff if throwing saber
		return;
	}
	if (NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_RAGE
		|| NPCS.NPC->client->ps.fd.forceRageRecoveryTime > level.time)
	{
		//never taunt while raging or recovering from rage
		return;
	}
	if ((NPCS.NPC->client->NPC_class == CLASS_TAVION && NPCS.NPC->client->weaponGhoul2[1] != NULL))
	{
		//never taunt when holding scepter
		return;
	}
	if (NPCS.NPC->client->saber[0].type == SABER_SITH_SWORD)
	{
		//never taunt when holding sith sword
		return;
	}
	if (enemy_dist >= 64)
	{
		const int chance = 20;

		if (Q_irand(2, chance) < NPCS.NPCInfo->stats.aggression)
		{
			if (TIMER_Done(NPCS.NPC, "chatter"))
			{
				if (enemy_dist > 200
					&& (NPCS.NPC->client->NPC_class != CLASS_BOBAFETT && NPCS.NPC->client->NPC_class != CLASS_MANDO)
					&& (NPCS.NPC->client->NPC_class != CLASS_REBORN || NPCS.NPC->s.weapon == WP_SABER)
					&& (NPCS.NPC->client->NPC_class != CLASS_SITHLORD || NPCS.NPC->s.weapon == WP_SABER)
					&& NPCS.NPC->client->NPC_class != CLASS_ROCKETTROOPER
					&& (!NPCS.NPC->client->ps.saberHolstered)
					&& !Q_irand(0, 5))
				{
					//taunt even more, turn off the saber
					if (NPCS.NPC->client->ps.fd.saberAnimLevel != SS_STAFF &&
						NPCS.NPC->client->ps.fd.saberAnimLevel != SS_DUAL)
					{
						//those taunts leave saber on
						WP_DeactivateSaber(NPCS.NPC);
					}
					NPCS.NPCInfo->stats.aggression = 3;
					if (NPCS.NPC->client->playerTeam != NPCTEAM_PLAYER && !Q_irand(0, 1))
					{
						TIMER_Set(NPCS.NPC, "chatter", Q_irand(12000, 15000));
						TIMER_Set(NPCS.NPC, "taunting", 12500);
					}
					else
					{
						Jedi_BattleTaunt();
						TIMER_Set(NPCS.NPC, "taunting", Q_irand(12000, 15000));
					}
				}
				else if (Jedi_BattleTaunt())
				{
					//taunt
				}
			}
		}
	}
}



static qboolean Jedi_AttackDecide(const int enemy_dist)
{
	if (!TIMER_Done(NPCS.NPC, "allyJediDelay"))
	{
		return qfalse;
	}

	// New: never touch NPCS.NPC->enemy if it's NULL
	if (!NPCS.NPC->enemy)
	{
		return qfalse;
	}

	if (Jedi_CultistDestroyer(NPCS.NPC))
	{
		//destroyer
		if (enemy_dist <= 32)
		{
			//go boom!
			NPCS.NPC->flags |= FL_GODMODE;
			NPCS.NPC->takedamage = qfalse;

			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_FORCE_RAGE, SETANIM_FLAG_HOLD | SETANIM_FLAG_OVERRIDE);
			NPCS.NPC->client->ps.fd.forcePowersActive |= 1 << FP_RAGE;
			NPCS.NPC->painDebounceTime = NPCS.NPC->useDebounceTime = level.time + NPCS.NPC->client->ps.torsoTimer;
			return qtrue;
		}
		return qfalse;
	}

	// Guard all client access
	if (NPCS.NPC->enemy->client &&
		NPCS.NPC->enemy->s.weapon == WP_SABER &&
		NPCS.NPC->enemy->client->ps.saberLockTime > level.time &&
		NPCS.NPC->client->ps.saberLockTime < level.time)
	{
		//enemy is in a saberLock and we are not
		return qfalse;
	}

	if (NPCS.NPC->client->ps.saberEventFlags & SEF_LOCK_WON)
	{
		//we won a saber lock, press the advantage with an attack!
		int chance = 0;
		if (NPCS.NPCInfo->aiFlags & NPCAI_BOSS_CHARACTER)
		{
			//desann and luke
			chance = 20;
		}
		else if (NPCS.NPC->client->NPC_class == CLASS_TAVION
			|| NPCS.NPC->client->NPC_class == CLASS_ALORA)
		{
			//tavion
			chance = 10;
		}
		else if (NPCS.NPC->client->NPC_class == CLASS_SHADOWTROOPER)
		{
			//shadowtrooper
			chance = 10;
		}
		else if (NPCS.NPC->client->NPC_class == CLASS_REBORN)
		{
			//fencer
			chance = 5;
		}
		else
		{
			chance = NPCS.NPCInfo->rank;
		}

		if (Q_irand(0, 30) < chance)
		{
			//based on skill with some randomness
			NPCS.NPC->client->ps.saberEventFlags &= ~SEF_LOCK_WON; //clear this now that we are using the opportunity
			TIMER_Set(NPCS.NPC, "noRetreat", Q_irand(500, 2000));
			NPCS.NPC->client->ps.weaponTime = NPCS.NPCInfo->shotTime = NPCS.NPC->attackDebounceTime = 0;
			NPCS.NPC->client->ps.saberBlocked = BLOCKED_NONE;
			WeaponThink();
			return qtrue;
		}
	}

	if (g_npcspskill.integer > 1 &&
		NPCS.NPC->client->ps.fd.forcePowerLevel[FP_SABER_OFFENSE] > FORCE_LEVEL_1 &&
		NPCS.NPC->client->ps.saberFatigueChainCount < MISHAPLEVEL_HEAVY &&
		NPCS.NPC->enemy &&
		NPCS.NPC->enemy->client &&
		NPCS.NPC->enemy->client->ps.fd.blockPoints > BLOCKPOINTS_HALF)
	{// smart NPCs can try to attack right out of a parry or knockaway if their enemy has high blockpoints
		if ((PM_SaberInParry(NPCS.NPC->client->ps.saberMove) ||
			PM_SaberInKnockaway(NPCS.NPC->client->ps.saberMove)) &&
			!npc_is_staff_style(NPCS.NPC) &&
			!npc_is_dual_style(NPCS.NPC) &&
			NPCS.NPC->client->ps.saberBlocked != BLOCKED_PARRY_BROKEN)
		{
			// try to attack straight from a parry
			NPCS.NPC->client->ps.weaponTime = 0;
			NPCS.NPCInfo->shotTime = 0;
			NPCS.NPC->attackDebounceTime = 0;
			NPCS.NPC->client->ps.saberBlocked = BLOCKED_NONE;
			Jedi_AdjustSaberAnimLevel(NPCS.NPC, Q_irand(SS_FAST, SS_MEDIUM)); // cinematic, harder hit to chew through block points
			WeaponThink();
			return qtrue;
		}
	}

	//try to hit them if we can
	if (!enemy_in_striking_range)
	{
		return qfalse;
	}

	if (!TIMER_Done(NPCS.NPC, "parryTime"))
	{
		return qfalse;
	}

	if (NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE)
	{
		//not allowed to attack
		return qfalse;
	}

	if (!(NPCS.ucmd.buttons & BUTTON_ATTACK)
		&& !(NPCS.ucmd.buttons & BUTTON_ALT_ATTACK)
		&& !(NPCS.ucmd.buttons & BUTTON_FORCEPOWER))
	{
		//not already attacking
		if (CanShoot(NPCS.NPC->enemy, NPCS.NPC))
		{
			if (NPCS.NPC->s.weapon == WP_SABER)
			{
				//Try to attack
				Jedi_FaceEnemy(qtrue);

				if (!PM_SaberInAttack(NPCS.NPC->client->ps.saberMove) && !Jedi_SaberBusy(NPCS.NPC))
				{
					if (NPCS.NPC->health <= 30
						|| NPCS.NPC->client->ps.fd.forcePower < BLOCKPOINTS_THIRTY
						|| NPCS.NPC->client->ps.fd.blockPoints < BLOCKPOINTS_THIRTY)
					{
						// Back away while attacking...
						const int rand = irand(0, 100);

						if (rand < 20)
						{
							NPCS.NPC->client->pers.cmd.rightmove = 64;
						}
						else if (rand < 40)
						{
							NPCS.NPC->client->pers.cmd.rightmove = -64;
						}

						WeaponThink();

						if (rand > 90)
						{
							// Do a lunge, etc occasionally...
							NPCS.NPC->client->pers.cmd.upmove = -127;
							NPCS.NPC->client->pers.cmd.buttons |= BUTTON_ATTACK;
						}
					}
					else
					{
						const int rand = irand(0, 100);

						Jedi_Advance();

						if (rand < 20)
						{
							NPCS.NPC->client->pers.cmd.rightmove = 64;
						}
						else if (rand < 40)
						{
							NPCS.NPC->client->pers.cmd.rightmove = -64;
						}

						WeaponThink();

						if (rand > 90
							&& NPCS.NPC->client->ps.fd.blockPoints < BLOCKPOINTS_HALF)
						{
							// Do a lunge, etc occasionally...
							NPCS.NPC->client->pers.cmd.upmove = -127;
							NPCS.NPC->client->pers.cmd.buttons |= BUTTON_ATTACK;
						}
					}
				}
				else
				{
					//Try to attack
					WeaponThink();
				}
			}
			else
			{
				//Try to attack
				if (NPCS.NPC->health <= 30
					|| NPCS.NPC->client->ps.fd.forcePower < BLOCKPOINTS_THIRTY
					|| NPCS.NPC->client->ps.fd.blockPoints < BLOCKPOINTS_THIRTY
					|| NPCS.NPC->client->ps.BlasterAttackChainCount > BLASTERMISHAPLEVEL_HEAVYER)
				{
					// Back away while attacking...
					const int rand = irand(0, 100);

					if (!NPC_IsJedi(NPCS.NPC) && NPCS.NPC->client->ps.weapon != WP_SABER)
					{
						// non Jedi handle their own attack/retreats...
						Jedi_Retreat();
					}

					if (npc_force_master(NPCS.NPC) && NPCS.NPC->client->ps.weapon != WP_SABER)
					{
						// non Jedi handle their own attack/retreats...
						Jedi_Retreat();
					}

					if (rand < 20)
					{
						NPCS.NPC->client->pers.cmd.rightmove = 64;
					}
					else if (rand < 40)
					{
						NPCS.NPC->client->pers.cmd.rightmove = -64;
					}

					WeaponThink();
				}
			}
		}
		else
		{
			//Try to attack
			WeaponThink();
		}
	}

	if (NPCS.ucmd.buttons & BUTTON_ATTACK && !NPC_Jumping())
	{
		//attacking
		if (!NPCS.ucmd.rightmove)
		{
			//not already strafing
			if (!Q_irand(0, 3))
			{
				//25% chance of doing this
				vec3_t right, dir2enemy;

				AngleVectors(NPCS.NPC->r.currentAngles, NULL, right, NULL);
				VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentAngles, dir2enemy);
				if (DotProduct(right, dir2enemy) > 0)
				{
					//he's to my right, strafe left
					if (NPC_MoveDirClear(NPCS.ucmd.forwardmove, -127, qfalse))
					{
						NPCS.ucmd.rightmove = -127;
						VectorClear(NPCS.NPC->client->ps.moveDir);
					}
				}
				else
				{
					//he's to my left, strafe right
					if (NPC_MoveDirClear(NPCS.ucmd.forwardmove, 127, qfalse))
					{
						NPCS.ucmd.rightmove = 127;
						VectorClear(NPCS.NPC->client->ps.moveDir);
					}
				}
			}
		}
		return qtrue;
	}

	return qfalse;
}



static qboolean Jedi_Jump(vec3_t dest, const int goal_ent_num)
{
	if (qtrue)
	{
		float shot_speed = 300, best_impact_dist = Q3_INFINITE; //fireSpeed,
		vec3_t shot_vel, fail_case;
		trace_t trace;
		trajectory_t tr = { 0 };
		int hit_count = 0;
		const int max_hits = 7;
		vec3_t bottom;

		while (hit_count < max_hits)
		{
			vec3_t target_dir;
			VectorSubtract(dest, NPCS.NPC->r.currentOrigin, target_dir);
			const float target_dist = VectorNormalize(target_dir);

			VectorScale(target_dir, shot_speed, shot_vel);
			float travel_time = target_dist / shot_speed;
			shot_vel[2] += travel_time * 0.5 * NPCS.NPC->client->ps.gravity;

			if (!hit_count)
			{
				//save the first one as the worst case scenario
				VectorCopy(shot_vel, fail_case);
			}

			if (qtrue)
			{
				vec3_t last_pos;
				const int time_step = 500;
				//do a rough trace of the path
				qboolean blocked = qfalse;

				VectorCopy(NPCS.NPC->r.currentOrigin, tr.trBase);
				VectorCopy(shot_vel, tr.trDelta);
				tr.trType = TR_GRAVITY;
				tr.trTime = level.time;
				travel_time *= 1000.0f;
				VectorCopy(NPCS.NPC->r.currentOrigin, last_pos);

				for (int elapsed_time = time_step; elapsed_time < floor(travel_time) + time_step; elapsed_time +=
					time_step)
				{
					vec3_t test_pos;
					if ((float)(elapsed_time) > travel_time)
					{
						//cap it
						elapsed_time = floor(travel_time);
					}
					BG_EvaluateTrajectory(&tr, level.time + elapsed_time, test_pos);
					if (test_pos[2] < last_pos[2])
					{
						//going down, ignore botclip
						trap->Trace(&trace, last_pos, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, test_pos, NPCS.NPC->s.number, NPCS.NPC->clipmask, qfalse, 0, 0);
					}
					else
					{
						//going up, check for botclip
						trap->Trace(&trace, last_pos, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, test_pos, NPCS.NPC->s.number, NPCS.NPC->clipmask | CONTENTS_BOTCLIP, qfalse, 0, 0);
					}

					if (trace.allsolid || trace.startsolid)
					{
						blocked = qtrue;
						break;
					}
					if (trace.fraction < 1.0f)
					{
						//hit something
						if (trace.entityNum == goal_ent_num)
						{
							break;
						}
						if (trace.contents & CONTENTS_BOTCLIP)
						{
							//hit a do-not-enter brush
							blocked = qtrue;
							break;
						}
						if (trace.plane.normal[2] > 0.7 && DistanceSquared(trace.endpos, dest) < 4096)
							//hit within 64 of desired location, should be okay
						{
							//close enough!
							break;
						}
						const float impact_dist = DistanceSquared(trace.endpos, dest);
						if (impact_dist < best_impact_dist)
						{
							best_impact_dist = impact_dist;
							VectorCopy(shot_vel, fail_case);
						}
						blocked = qtrue;
						break;
					}
					if (elapsed_time == floor(travel_time))
					{
						//reached end, all clear
						if (trace.fraction >= 1.0f)
						{
							VectorCopy(trace.endpos, bottom);
							bottom[2] -= 128;
							trap->Trace(&trace, trace.endpos, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, bottom, NPCS.NPC->s.number, NPCS.NPC->clipmask, qfalse, 0, 0);
							if (trace.fraction >= 1.0f)
							{
								//would fall too far
								blocked = qtrue;
							}
						}
						break;
					}
					//all clear, try next slice
					VectorCopy(test_pos, last_pos);
				}
				if (blocked)
				{
					hit_count++;
					shot_speed = 300 + (hit_count - 2) * 100;
					if (hit_count >= 2)
					{
						//skip 300 since that was the first value we tested
						shot_speed += 100;
					}
				}
				else
				{
					//made it!
					break;
				}
			}
		}

		if (hit_count >= max_hits)
		{
			VectorCopy(fail_case, NPCS.NPC->client->ps.velocity);
		}
		VectorCopy(shot_vel, NPCS.NPC->client->ps.velocity);
	}
	return qtrue;
}



static qboolean Jedi_TryJump(const gentity_t* goal)
{
	if (NPCS.NPCInfo->scriptFlags & SCF_NO_ACROBATICS)
	{
		return qfalse;
	}
	if (TIMER_Done(NPCS.NPC, "jumpChaseDebounce"))
	{
		if (!goal->client || goal->client->ps.groundEntityNum != ENTITYNUM_NONE)
		{
			if (!PM_InKnockDown(&NPCS.NPC->client->ps) && !PM_InRoll(&NPCS.NPC->client->ps))
			{
				//enemy is on terra firma
				vec3_t goal_diff;
				VectorSubtract(goal->r.currentOrigin, NPCS.NPC->r.currentOrigin, goal_diff);
				const float goal_z_diff = goal_diff[2];
				goal_diff[2] = 0;
				const float goal_xy_dist = VectorNormalize(goal_diff);
				if (goal_xy_dist < 550 && goal_z_diff > -400)
				{
					qboolean debounce = qfalse;
					if (NPCS.NPC->health < 150 && (NPCS.NPC->health < 30 && goal_z_diff < 0 || goal_z_diff < -128))
					{
						//don't jump, just walk off... doesn't help with ledges, though
						debounce = qtrue;
					}
					else if (goal_z_diff < 32 && goal_xy_dist < 200)
					{
						//what is their ideal jump height?
						NPCS.ucmd.upmove = 127;
						debounce = qtrue;
					}
					else
					{
						if (goal_z_diff > 0 || goal_xy_dist > 128)
						{
							//Fake a force-jump
							//Screw it, just do my own calc & throw
							vec3_t dest;
							VectorCopy(goal->r.currentOrigin, dest);
							if (goal == NPCS.NPC->enemy)
							{
								int side_try = 0;
								while (side_try < 10)
								{
									//FIXME: make it so it doesn't try the same spot again?
									if (Q_irand(0, 1))
									{
										dest[0] += NPCS.NPC->enemy->r.maxs[0] * 1.25;
									}
									else
									{
										dest[0] += NPCS.NPC->enemy->r.mins[0] * 1.25;
									}
									if (Q_irand(0, 1))
									{
										dest[1] += NPCS.NPC->enemy->r.maxs[1] * 1.25;
									}
									else
									{
										dest[1] += NPCS.NPC->enemy->r.mins[1] * 1.25;
									}
									trace_t trace;
									vec3_t bottom;
									VectorCopy(dest, bottom);
									bottom[2] -= 128;
									trap->Trace(&trace, dest, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, bottom, goal->s.number, NPCS.NPC->clipmask, qfalse, 0, 0);
									if (trace.fraction < 1.0f)
									{
										//hit floor, okay to land here
										break;
									}
									side_try++;
								}
								if (side_try >= 10)
								{
									//screw it, just jump right at him?
									VectorCopy(goal->r.currentOrigin, dest);
								}
							}
							if (Jedi_Jump(dest, goal->s.number))
							{
								{
									int jump_anim = 0;
									if ((NPCS.NPC->client->NPC_class == CLASS_BOBAFETT ||
										NPCS.NPC->client->NPC_class == CLASS_MANDO)
										|| (NPCS.NPCInfo->rank != RANK_CREWMAN && NPCS.NPCInfo->rank <= RANK_LT_JG))
									{
										//can't do acrobatics
										jump_anim = BOTH_FORCEJUMP1;
									}
									else
									{
										jump_anim = BOTH_FLIP_F;
									}
									NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, jump_anim,
										SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
								}

								NPCS.NPC->client->ps.fd.forceJumpZStart = NPCS.NPC->r.currentOrigin[2];
								NPCS.NPC->client->ps.pm_flags |= PMF_JUMPING;

								NPCS.NPC->client->ps.weaponTime = NPCS.NPC->client->ps.torsoTimer;
								NPCS.NPC->client->ps.fd.forcePowersActive |= 1 << FP_LEVITATION;

								if (NPCS.NPC->client->NPC_class == CLASS_BOBAFETT ||
									NPCS.NPC->client->NPC_class == CLASS_MANDO)
								{
									G_SoundOnEnt(NPCS.NPC, CHAN_ITEM, "sound/boba/jeton.wav");
									NPCS.NPC->client->jetPackTime = level.time + Q_irand(1000, 3000);
								}
								else
								{
									if (NPCS.NPC->client->ps.fd.forcePowerLevel[FP_LEVITATION] < FORCE_LEVEL_3)
									{
										//short burst
										G_SoundOnEnt(NPCS.NPC, CHAN_BODY, "sound/weapons/force/jumpsmall.mp3");
									}
									else
									{
										//holding it
										G_SoundOnEnt(NPCS.NPC, CHAN_BODY, "sound/weapons/force/jump.mp3");
									}
								}

								TIMER_Set(NPCS.NPC, "forceJumpChasing", Q_irand(2000, 3000));
								debounce = qtrue;
							}
						}
					}
					if (debounce)
					{
						//Don't jump again for another 2 to 5 seconds
						TIMER_Set(NPCS.NPC, "jumpChaseDebounce", Q_irand(2000, 5000));
						NPCS.ucmd.forwardmove = 127;
						VectorClear(NPCS.NPC->client->ps.moveDir);
						TIMER_Set(NPCS.NPC, "duck", -level.time);
						return qtrue;
					}
				}
			}
		}
	}
	return qfalse;
}



static qboolean Jedi_Jumping(const gentity_t* goal)
{
	if (!TIMER_Done(NPCS.NPC, "forceJumpChasing") && goal)
	{
		//force-jumping at the enemy
		if (!(NPCS.NPC->client->ps.pm_flags & PMF_JUMPING))
		{
			//landed
			TIMER_Set(NPCS.NPC, "forceJumpChasing", 0);
		}
		else
		{
			NPC_FaceEntity(goal, qtrue);
			return qtrue;
		}
	}
	return qfalse;
}


static void Jedi_CheckEnemyMovement(const float enemy_dist)
{
	if (!NPCS.NPC->enemy || !NPCS.NPC->enemy->client)
	{
		return;
	}

	if (!(NPCS.NPCInfo->aiFlags & NPCAI_BOSS_CHARACTER))
	{
		if (PM_KickingAnim(NPCS.NPC->enemy->client->ps.legsAnim)
			&& NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE
			//FIXME: I'm relatively close to him
			&& (NPCS.NPC->enemy->client->ps.legsAnim == BOTH_A7_KICK_RL
				|| NPCS.NPC->enemy->client->ps.legsAnim == BOTH_A7_KICK_BF
				|| NPCS.NPC->enemy->client->ps.legsAnim == BOTH_A7_KICK_S
				|| NPCS.NPC->enemy->enemy && NPCS.NPC->enemy->enemy == NPCS.NPC)
			)
		{
			//run into the kick!
			NPCS.ucmd.forwardmove = NPCS.ucmd.rightmove = NPCS.ucmd.upmove = 0;
			VectorClear(NPCS.NPC->client->ps.moveDir);
			Jedi_Advance();
		}
		else if (NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_A7_HILT
			|| NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_SLAP_R
			|| NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_SLAP_L
			&& NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE)
		{
			//run into the hilt bash
			//FIXME : only if in front!
			NPCS.ucmd.forwardmove = NPCS.ucmd.rightmove = NPCS.ucmd.upmove = 0;
			VectorClear(NPCS.NPC->client->ps.moveDir);
			Jedi_Advance();
		}
		else if ((NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_A6_FB
			|| NPCS.NPC->enemy->client->ps.torsoAnim == BOTH_A6_LR)
			&& NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE)
		{
			//run into the attack
			//FIXME : only if on R/L or F/B?
			NPCS.ucmd.forwardmove = NPCS.ucmd.rightmove = NPCS.ucmd.upmove = 0;
			VectorClear(NPCS.NPC->client->ps.moveDir);
			Jedi_Advance();
		}
		else if (NPCS.NPC->enemy->enemy && NPCS.NPC->enemy->enemy == NPCS.NPC)
		{
			//enemy is mad at *me*
			if (NPCS.NPC->enemy->client->ps.legsAnim == BOTH_JUMPFLIPSLASHDOWN1
				|| NPCS.NPC->enemy->client->ps.legsAnim == BOTH_JUMPFLIPSTABDOWN
				|| NPCS.NPC->enemy->client->ps.legsAnim == BOTH_FLIP_ATTACK7)
			{
				//enemy is flipping over me
				if (Q_irand(0, NPCS.NPCInfo->rank) < RANK_LT)
				{
					//be nice and stand still for him...
					NPCS.ucmd.forwardmove = NPCS.ucmd.rightmove = NPCS.ucmd.upmove = 0;
					VectorClear(NPCS.NPC->client->ps.moveDir);
					NPCS.NPC->client->ps.fd.forceJumpCharge = 0;
					TIMER_Set(NPCS.NPC, "strafeLeft", -1);
					TIMER_Set(NPCS.NPC, "strafeRight", -1);
					TIMER_Set(NPCS.NPC, "noStrafe", Q_irand(500, 1000));
					TIMER_Set(NPCS.NPC, "movenone", Q_irand(500, 1000));
					TIMER_Set(NPCS.NPC, "movecenter", Q_irand(500, 1000));
				}
			}
			else if (NPCS.NPC->enemy->client->ps.legsAnim == BOTH_WALL_FLIP_BACK1
				|| NPCS.NPC->enemy->client->ps.legsAnim == BOTH_WALL_FLIP_RIGHT
				|| NPCS.NPC->enemy->client->ps.legsAnim == BOTH_WALL_FLIP_LEFT
				|| NPCS.NPC->enemy->client->ps.legsAnim == BOTH_WALL_RUN_LEFT_FLIP
				|| NPCS.NPC->enemy->client->ps.legsAnim == BOTH_WALL_RUN_RIGHT_FLIP)
			{
				//he's flipping off a wall
				if (NPCS.NPC->enemy->client->ps.groundEntityNum == ENTITYNUM_NONE)
				{
					//still in air
					if (enemy_dist < 256)
					{
						//close
						if (Q_irand(0, NPCS.NPCInfo->rank) < RANK_LT)
						{
							//be nice and stand still for him...
							//stop current movement
							NPCS.ucmd.forwardmove = NPCS.ucmd.rightmove = NPCS.ucmd.upmove = 0;
							VectorClear(NPCS.NPC->client->ps.moveDir);
							NPCS.NPC->client->ps.fd.forceJumpCharge = 0;
							TIMER_Set(NPCS.NPC, "strafeLeft", -1);
							TIMER_Set(NPCS.NPC, "strafeRight", -1);
							TIMER_Set(NPCS.NPC, "noStrafe", Q_irand(500, 1000));
							TIMER_Set(NPCS.NPC, "noturn", Q_irand(250, 500) * (3 - g_npcspskill.integer));

							vec3_t enemyFwd, dest, dir;

							VectorCopy(NPCS.NPC->enemy->client->ps.velocity, enemyFwd);
							VectorNormalize(enemyFwd);
							VectorMA(NPCS.NPC->enemy->r.currentOrigin, -64, enemyFwd, dest);
							VectorSubtract(dest, NPCS.NPC->r.currentOrigin, dir);
							if (VectorNormalize(dir) > 32)
							{
								G_UcmdMoveForDir(NPCS.NPC, &NPCS.ucmd, dir);
							}
							else
							{
								TIMER_Set(NPCS.NPC, "movenone", Q_irand(500, 1000));
								TIMER_Set(NPCS.NPC, "movecenter", Q_irand(500, 1000));
							}
						}
					}
				}
			}
			else if (NPCS.NPC->enemy->client->ps.legsAnim == BOTH_A2_STABBACK1 ||
				NPCS.NPC->enemy->client->ps.legsAnim == BOTH_A2_STABBACK1B)
			{
				//he's stabbing backwards
				if (enemy_dist < 256 && enemy_dist > 64)
				{
					//close
					if (!InFront(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->enemy->r.currentAngles, 0.0f))
					{
						//behind him
						if (!Q_irand(0, NPCS.NPCInfo->rank))
						{
							//be nice and stand still for him...
							//stop current movement
							NPCS.ucmd.forwardmove = NPCS.ucmd.rightmove = NPCS.ucmd.upmove = 0;
							VectorClear(NPCS.NPC->client->ps.moveDir);
							NPCS.NPC->client->ps.fd.forceJumpCharge = 0;
							TIMER_Set(NPCS.NPC, "strafeLeft", -1);
							TIMER_Set(NPCS.NPC, "strafeRight", -1);
							TIMER_Set(NPCS.NPC, "noStrafe", Q_irand(500, 1000));

							vec3_t enemyFwd, dest, dir;

							AngleVectors(NPCS.NPC->enemy->r.currentAngles, enemyFwd, NULL, NULL);
							VectorMA(NPCS.NPC->enemy->r.currentOrigin, -32, enemyFwd, dest);
							VectorSubtract(dest, NPCS.NPC->r.currentOrigin, dir);
							if (VectorNormalize(dir) > 64)
							{
								G_UcmdMoveForDir(NPCS.NPC, &NPCS.ucmd, dir);
							}
							else
							{
								TIMER_Set(NPCS.NPC, "movenone", Q_irand(500, 1000));
								TIMER_Set(NPCS.NPC, "movecenter", Q_irand(500, 1000));
							}
						}
					}
				}
			}
		}
	}
}



static void Jedi_CheckJumps(void)
{
	if (NPCS.NPCInfo->scriptFlags & SCF_NO_ACROBATICS)
	{
		NPCS.NPC->client->ps.fd.forceJumpCharge = 0;
		NPCS.ucmd.upmove = 0;
		return;
	}
	vec3_t jump_vel = { 0, 0, 0 };

	if (NPCS.NPC->client->ps.fd.forceJumpCharge)
	{
		WP_GetVelocityForForceJump(NPCS.NPC, jump_vel, &NPCS.ucmd);
	}
	else if (NPCS.ucmd.upmove > 0)
	{
		VectorCopy(NPCS.NPC->client->ps.velocity, jump_vel);
		jump_vel[2] = JUMP_VELOCITY;
	}
	else
	{
		return;
	}
	if (!jump_vel[0] && !jump_vel[1])
	{
		return;
	}
	//Now predict where this is going
	//in steps, keep evaluating the trajectory until the new z pos is <= than current z pos, trace down from there
	trace_t trace;
	trajectory_t tr = { 0 };
	vec3_t last_pos, bottom;

	VectorCopy(NPCS.NPC->r.currentOrigin, tr.trBase);
	VectorCopy(jump_vel, tr.trDelta);
	tr.trType = TR_GRAVITY;
	tr.trTime = level.time;
	VectorCopy(NPCS.NPC->r.currentOrigin, last_pos);

	//This may be kind of wasteful, especially on long throws... use larger steps?  Divide the travelTime into a certain hard number of slices?  Trace just to apex and down?
	for (int elapsed_time = 500; elapsed_time <= 4000; elapsed_time += 500)
	{
		vec3_t test_pos;
		BG_EvaluateTrajectory(&tr, level.time + elapsed_time, test_pos);
		if (test_pos[2] < last_pos[2])
		{
			//going down, don't check for BOTCLIP
			trap->Trace(&trace, last_pos, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, test_pos, NPCS.NPC->s.number, NPCS.NPC->clipmask, qfalse, 0, 0); //FIXME: include CONTENTS_BOTCLIP?
		}
		else
		{
			//going up, check for BOTCLIP
			trap->Trace(&trace, last_pos, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, test_pos, NPCS.NPC->s.number, NPCS.NPC->clipmask | CONTENTS_BOTCLIP, qfalse, 0, 0);
		}
		if (trace.allsolid || trace.startsolid)
		{
			goto jump_unsafe;
		}
		if (trace.fraction < 1.0f)
		{
			//hit something
			if (trace.contents & CONTENTS_BOTCLIP)
			{
				//hit a do-not-enter brush
				goto jump_unsafe;
			}
			//FIXME: trace through func_glass?
			break;
		}
		VectorCopy(test_pos, last_pos);
	}
	//okay, reached end of jump, now trace down from here for a floor
	VectorCopy(trace.endpos, bottom);
	if (bottom[2] > NPCS.NPC->r.currentOrigin[2])
	{
		//only care about dist down from current height or lower
		bottom[2] = NPCS.NPC->r.currentOrigin[2];
	}
	else if (NPCS.NPC->r.currentOrigin[2] - bottom[2] > 400)
	{
		//whoa, long drop, don't do it!
		//probably no floor at end of jump, so don't jump
		goto jump_unsafe;
	}
	bottom[2] -= 128;
	trap->Trace(&trace, trace.endpos, NPCS.NPC->r.mins, NPCS.NPC->r.maxs, bottom, NPCS.NPC->s.number, NPCS.NPC->clipmask, qfalse, 0, 0);
	if (trace.allsolid || trace.startsolid || trace.fraction < 1.0f)
	{
		//hit ground!
		if (trace.entityNum < ENTITYNUM_WORLD)
		{
			//landed on an ent
			const gentity_t* groundEnt = &g_entities[trace.entityNum];
			if (groundEnt->r.svFlags & SVF_GLASS_BRUSH)
			{
				//don't land on breakable glass!
				goto jump_unsafe;
			}
		}
		return;
	}
jump_unsafe:
	//probably no floor at end of jump, so don't jump
	NPCS.NPC->client->ps.fd.forceJumpCharge = 0;
	NPCS.ucmd.upmove = 0;
}


void RT_CheckJump(void)
{
	int jump_ent_num = ENTITYNUM_NONE;
	vec3_t jump_pos = { 0, 0, 0 };

	if (!NPCS.NPCInfo->goalEntity)
	{
		if (NPCS.NPC->enemy)
		{
			//FIXME: debounce this?
			if (TIMER_Done(NPCS.NPC, "roamTime")
				&& Q_irand(0, 9))
			{
				//okay to try to find another spot to be
				int cp_flags = CP_CLEAR | CP_HAS_ROUTE; //must have a clear shot at enemy
				const float enemy_dist_sq = DistanceHorizontalSquared(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin);
				//FIXME: base these ranges on weapon
				if (enemy_dist_sq > 2048 * 2048)
				{
					//hmm, close in?
					cp_flags |= CP_APPROACH_ENEMY;
				}
				else if (enemy_dist_sq < 256 * 256)
				{
					//back off!
					cp_flags |= CP_RETREAT;
				}
				int send_flags = cp_flags;
				int cp = NPC_FindCombatPointRetry(NPCS.NPC->r.currentOrigin,
					NPCS.NPC->r.currentOrigin,
					NPCS.NPC->r.currentOrigin,
					&send_flags,
					256,
					NPCS.NPCInfo->lastFailedCombatPoint);
				if (cp == -1)
				{
					//try again, no route needed since we can rocket-jump to it!
					cp_flags &= ~CP_HAS_ROUTE;
					cp = NPC_FindCombatPointRetry(NPCS.NPC->r.currentOrigin,
						NPCS.NPC->r.currentOrigin,
						NPCS.NPC->r.currentOrigin,
						&cp_flags,
						256,
						NPCS.NPCInfo->lastFailedCombatPoint);
				}
				if (cp != -1)
				{
					NPC_SetMoveGoal(NPCS.NPC, level.combatPoints[cp].origin, 8, qtrue, cp, NULL);
				}
				else
				{
					jump_ent_num = NPCS.NPC->enemy->s.number;
					VectorCopy(NPCS.NPC->enemy->r.currentOrigin, jump_pos);
				}
				TIMER_Set(NPCS.NPC, "roamTime", Q_irand(3000, 12000));
			}
			else
			{
				jump_ent_num = NPCS.NPC->enemy->s.number;
				VectorCopy(NPCS.NPC->enemy->r.currentOrigin, jump_pos);
			}
		}
		else
		{
			return;
		}
	}
	else
	{
		jump_ent_num = NPCS.NPCInfo->goalEntity->s.number;
		VectorCopy(NPCS.NPCInfo->goalEntity->r.currentOrigin, jump_pos);
	}
	vec3_t vec2_goal;
	VectorSubtract(jump_pos, NPCS.NPC->r.currentOrigin, vec2_goal);
	if (fabs(vec2_goal[2]) < 32)
	{
		//not a big height diff, see how far it is
		vec2_goal[2] = 0;
		if (VectorLengthSquared(vec2_goal) < 256 * 256)
		{
			//too close!  Don't rocket-jump to it...
			return;
		}
	}
	//If we can't get straight at him
	if (!Jedi_ClearPathToSpot(jump_pos, jump_ent_num))
	{
		//hunt him down
		if ((NPC_ClearLOS4(NPCS.NPC->enemy) || NPCS.NPCInfo->enemyLastSeenTime > level.time - 500)
			&& InFOV3(jump_pos, NPCS.NPC->r.currentOrigin, NPCS.NPC->client->ps.viewangles, 20, 60))
		{
			
		}

		if (Jedi_Hunt() && !(NPCS.NPCInfo->aiFlags & NPCAI_BLOCKED))
		{
			//can macro-navigate to him
			return;
		}
		//try to find a waypoint that can see enemy, jump from there
		
	}
}


static void Jedi_Combat(void)
{
	vec3_t enemy_dir, enemy_movedir, enemy_dest;
	float enemy_dist = 0.0f, enemy_movespeed = 0.0f;

	// See where enemy will be 300 ms from now
	jedi_set_enemy_info(enemy_dest, enemy_dir, &enemy_dist, enemy_movedir, &enemy_movespeed, 300);

	// ----------------------------------------------------------------------
	// GLOBAL BLOCK STANCE OVERRIDE
	// If this NPCS.NPC should be blocking, hold position and face the enemy.
	// This MUST run before any movement / spacing logic.
	// ----------------------------------------------------------------------
	if (NPC_Should_Block(NPCS.NPC) == qtrue)
	{
		// Mark stance flag
		if ((NPCS.NPC->client->ps.ManualBlockingFlags & (1 << MBF_NPCBLOCKSTANCE)) == 0)
		{
			NPCS.NPC->client->ps.ManualBlockingFlags |= (1 << MBF_NPCBLOCKSTANCE);
		}

		// Face the enemy while blocking
		if (NPCS.NPC->enemy != NULL)
		{
			Jedi_FaceEnemy(qtrue);
			NPC_UpdateAngles(qtrue, qtrue);
		}
	}
	else
	{
		// Not blocking this frame ? ensure stance flag is clear
		NPCS.NPC->client->ps.ManualBlockingFlags &= ~(1 << MBF_NPCBLOCKSTANCE);

		if (enemy_dist < 150.0f && (NPCS.NPC->client->ps.ManualBlockingFlags & (1 << MBF_NPCBLOCKING)) == 0)
		{
			NPCS.NPC->client->ps.ManualBlockingFlags |= (1 << MBF_NPCBLOCKING);
		}
	}

	if (NPC_Jumping())
	{
		// I'm in the middle of a jump, so just see if I should attack
		Jedi_AttackDecide(enemy_dist);
		return;
	}

	if (TIMER_Done(NPCS.NPC, "allyJediDelay"))
	{
		if ((!(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_GRIP) ||
			NPCS.NPC->client->ps.fd.forcePowerLevel[FP_GRIP] < FORCE_LEVEL_2)
			&& (!(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_GRASP) ||
				NPCS.NPC->client->ps.fd.forcePowerLevel[FP_GRASP] < FORCE_LEVEL_2))
		{
			// not gripping
			// If we can't get straight at him
			if (Jedi_ClearPathToSpot(enemy_dest, NPCS.NPC->enemy->s.number) == qfalse)
			{
				// hunt him down
				if ((NPC_ClearLOS4(NPCS.NPC->enemy) == qtrue ||
					NPCS.NPCInfo->enemyLastSeenTime > level.time - 500) &&
					NPC_FaceEnemy(qtrue))
				{
					if (NPCS.NPC->client != NULL &&
						(NPCS.NPC->client->NPC_class == CLASS_BOBAFETT ||
							NPCS.NPC->client->NPC_class == CLASS_MANDO))
					{
						Boba_FireDecide();
					}
				}

				// Check for evasion
				if (TIMER_Done(NPCS.NPC, "parryTime"))
				{
					// finished parrying
					if (NPCS.NPC->client->ps.saberBlocked != BLOCKED_ATK_BOUNCE &&
						NPCS.NPC->client->ps.saberBlocked != BLOCKED_PARRY_BROKEN)
					{
						// wasn't blocked myself
						NPCS.NPC->client->ps.saberBlocked = BLOCKED_NONE;
					}
				}

				if (Jedi_Hunt() == qtrue && (NPCS.NPCInfo->aiFlags & NPCAI_BLOCKED) == 0)
				{
					// can macro-navigate to him
					if (enemy_dist < 384.0f &&
						Q_irand(0, 10) == 0 &&
						NPCS.NPCInfo->blockedSpeechDebounceTime < level.time &&
						jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] < level.time &&
						NPC_ClearLOS4(NPCS.NPC->enemy) == qfalse)
					{
						if (!npc_is_dark_jedi(NPCS.NPC))
						{
							G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_GIVEUP1, EV_GIVEUP4), 3000);
						}
						else
						{
							G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_JLOST1, EV_JLOST3), 3000);
						}
						jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] =
							NPCS.NPCInfo->blockedSpeechDebounceTime = level.time + 3000;
					}

					if (NPCS.NPC->client != NULL &&
						(NPCS.NPC->client->NPC_class == CLASS_BOBAFETT ||
							NPCS.NPC->client->NPC_class == CLASS_MANDO))
					{
						Boba_FireDecide();
					}

					return;
				}
				else if (Jedi_Track())
				{
					return;
				}
				if (NPCS.NPC->s.weapon == WP_SABER && (g_npcspskill.integer != 0))
				{
					if (NPCS.NPCInfo->aiFlags & NPCAI_BLOCKED)
					{//been blocked for a little while, try something else
						//try to jump to the blockedTargetPosition
						gentity_t* temp_goal = G_Spawn(); //ugh, this is NOT good...?
						G_SetOrigin(temp_goal, NPCS.NPCInfo->blockedDest);
						trap->LinkEntity((sharedEntity_t*)temp_goal);
						if (Jedi_TryJump(temp_goal))
						{
							//going to jump to the dest
							G_FreeEntity(temp_goal);
							return;
						}
						G_FreeEntity(temp_goal);
					}
					
				}
			}
		}
		// else, we can see him or we can't track him at all

		// every few seconds, decide if we should advance or retreat
		Jedi_CombatTimersUpdate((int)enemy_dist);

		// maintain a distance from enemy appropriate for our aggression level
		Jedi_CombatDistance((int)enemy_dist);
	}

	if (NPCS.NPC->client->NPC_class != CLASS_BOBAFETT &&
		NPCS.NPC->client->NPC_class != CLASS_MANDO)
	{
		// Update our seen enemy position
		if ((NPCS.NPC->enemy->client == NULL ||
			NPCS.NPC->enemy->client->ps.groundEntityNum != ENTITYNUM_NONE) &&
			NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE)
		{
			VectorCopy(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPCInfo->enemyLastSeenLocation);
		}
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
	}

	// Turn to face the enemy
	if (TIMER_Done(NPCS.NPC, "noturn") && NPC_Jumping() == qfalse)
	{
		Jedi_FaceEnemy(qtrue);
	}
	NPC_UpdateAngles(qtrue, qtrue);

	// Check for evasion
	if (TIMER_Done(NPCS.NPC, "parryTime"))
	{
		// finished parrying
		if (NPCS.NPC->client->ps.saberBlocked != BLOCKED_ATK_BOUNCE &&
			NPCS.NPC->client->ps.saberBlocked != BLOCKED_PARRY_BROKEN)
		{
			// wasn't blocked myself
			NPCS.NPC->client->ps.saberBlocked = BLOCKED_NONE;
		}
	}

	if (NPCS.NPC->enemy->s.weapon == WP_SABER)
	{
		Jedi_EvasionSaber(enemy_movedir, enemy_dist, enemy_dir);
	}
	else if (!jedi_in_camera)
	{
		NPC_CheckEvasion();

		if (!npc_is_dark_jedi(NPCS.NPC))
		{
			G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_DEFLECT1, EV_DEFLECT3), 2000);
		}
		else
		{
			G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_TAUNT1, EV_TAUNT3), 10000);
		}
	}

	Jedi_TimersApply();

	if (TIMER_Done(NPCS.NPC, "allyJediDelay"))
	{
		if ((!NPCS.NPC->client->ps.saberInFlight ||
			NPCS.NPC->client->ps.fd.saberAnimLevel == SS_DUAL && (NPCS.NPC->client->ps.saberHolstered == 0))
			&& (!(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_GRIP) || NPCS.NPC->client->ps.fd.forcePowerLevel[FP_GRIP] < FORCE_LEVEL_2)
			&& (!(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_GRASP) || NPCS.NPC->client->ps.fd.forcePowerLevel[FP_GRASP] < FORCE_LEVEL_2))
		{//not throwing saber or using force grip
			//see if we can attack
			if (Jedi_AttackDecide(enemy_dist) == qfalse)
			{// not attacking, decide what else to do
				qboolean attacked = qfalse;

				if (NPCS.NPC->enemy
					&& NPCS.NPC->client->ps.weaponTime <= 0
					&& NPC_IsAlive(NPCS.NPC, NPCS.NPC->enemy)
					&& Distance(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) <= 64
					&& (NPCS.NPC->client->ps.weapon == WP_SABER)
					&& NPCS.NPC->next_kick_time <= level.time
					&& NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE
					&& !PM_InKnockDown(&NPCS.NPC->client->ps) //lying on the ground or getting up: finish that first
					&& !PM_InGetUp(&NPCS.NPC->client->ps)
					&& !PM_InRoll(&NPCS.NPC->client->ps)
					&& irand(0, 100) > 75)
				{// Close range - switch to melee... KICK!
					if (d_JediAI.integer || g_DebugSaberCombat.integer)
					{
						Com_Printf(S_COLOR_RED"Debug: NPCS.NPC kicking...\n");
					}
					NPCS.NPCInfo->scriptFlags &= ~SCF_ALT_FIRE;
					int desiredAnim = 0;

					switch (irand(0, 4))
					{
					case 1: desiredAnim = BOTH_A7_KICK_F; break;
					case 2: desiredAnim = BOTH_A7_SLAP_L; break;
					case 3: desiredAnim = BOTH_A7_SLAP_R; break;
					case 4: desiredAnim = BOTH_A7_HILT; break;
					default: desiredAnim = BOTH_A7_KICK_F; break;
					}

					NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, desiredAnim, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

					if (!npc_is_dark_jedi(NPCS.NPC))
					{
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_COMBAT1, EV_COMBAT3), 2000);
					}
					else
					{
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_TAUNT1, EV_TAUNT3), 10000);
					}

					NPCS.NPC->next_kick_time = level.time + 15000;

					if (NPC_IsAlive(NPCS.NPC, NPCS.NPC->enemy) && irand(0, 100) <= 25)
					{// 25% of the time, knock them over...
						vec3_t	smackDir;
						VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, smackDir);
						smackDir[2] += 30;
						VectorNormalize(smackDir);

						//hurt them
						G_Sound(NPCS.NPC->enemy, CHAN_AUTO, G_SoundIndex("sound/chars/%s/misc/pain0%d"));

						//throw them
						G_Throw(NPCS.NPC->enemy, smackDir, flrand(10, 20));

						WP_AbsorbKick(NPCS.NPC->enemy, NPCS.NPC, smackDir);
						//done with the damage
						NPCS.NPCInfo->blockedDebounceTime = 1;
						if (d_JediAI.integer || g_DebugSaberCombat.integer)
						{
							Com_Printf(S_COLOR_RED"Debug: NPCS.NPC kicking yeet...\n");
						}
					}

					attacked = qtrue;
				}
				else
				{//decide whether to advance or retreat
					if (NPCS.NPC->enemy
						&& NPCS.NPC->client->ps.weaponTime <= 0
						&& NPC_IsAlive(NPCS.NPC, NPCS.NPC->enemy)
						&& (NPCS.NPC->s.weapon == WP_SABER) && (g_npcspskill.integer != 0))
					{
						if (TIMER_Done(NPCS.NPC, "DashInTime") &&
							Distance(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) > 128 &&
							Distance(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) < 256 &&
							NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE &&
							PM_InKnockDown(&NPCS.NPC->client->ps) == qfalse)
						{//try to dash to TargetPosition
							JediDirectionalDashAttack(NPCS.NPC, NPCS.NPC->enemy);
							TIMER_Set(NPCS.NPC, "DashInTime", Q_irand(3000, 6000));
							if (d_JediAI.integer || g_DebugSaberCombat.integer)
							{
								Com_Printf(S_COLOR_RED"Debug: NPCS.NPC dashing to player...\n");
							}
						}
					}
					Jedi_CombatIdle(enemy_dist);

					if (!npc_is_dark_jedi(NPCS.NPC))
					{
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_OUTFLANK1, EV_OUTFLANK2), 2000);
					}
					else
					{
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_TAUNT1, EV_TAUNT3), 10000);
					}
				}

				if (!attacked)
				{//if we didn't do a special melee attack, see if we want to do a regular attack
					attacked = Jedi_AttackDecide(enemy_dist);
				}
			}
			else
			{
				//stop taunting
				TIMER_Set(NPCS.NPC, "taunting", -level.time);
			}
		}
		else
		{
			//we're gripping or throwing our saber, so don't do anything but maybe taunt
		}

		if (NPCS.NPC->client->NPC_class == CLASS_BOBAFETT ||
			NPCS.NPC->client->NPC_class == CLASS_MANDO)
		{
			Boba_FireDecide();
		}
		else if (NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER)
		{
			RT_FireDecide();
		}
	}

	// Check for certain enemy special moves
	Jedi_CheckEnemyMovement(enemy_dist);
	// Make sure that we don't jump off ledges over long drops
	Jedi_CheckJumps();

	// Just make sure we don't strafe into walls or off cliffs
	if (VectorCompare(NPCS.NPC->client->ps.moveDir, vec3_origin) &&
		NPC_MoveDirClear(NPCS.ucmd.forwardmove, NPCS.ucmd.rightmove, qtrue) == qfalse)
	{
		// uh-oh, we are going to fall or hit something
		navInfo_t info;
		NAV_GetLastMove(&info);

		if ((info.flags & NIF_MACRO_NAV) == 0)
		{
			// micro-navigation told us to step off a ledge, try using macronav for now
			NPC_MoveToGoal(qfalse);
		}

		// reset the timers
		TIMER_Set(NPCS.NPC, "strafeLeft", 0);
		TIMER_Set(NPCS.NPC, "strafeRight", 0);
	}
}



/*
==========================================================================================
EXTERNALLY CALLED BEHAVIOR STATES
==========================================================================================
*/

/*
-------------------------
NPC_Jedi_Pain
-------------------------
*/

void NPC_Jedi_Pain(gentity_t* self, gentity_t* attacker, const int damage)
{
	// MP pain callbacks only get attacker and damage; the rest comes from G_Damage's globals
	const int mod = gPainMOD;
	vec3_t point;
	VectorCopy(gPainPoint, point);

	if (attacker->s.weapon == WP_SABER)
	{
		//back off
		TIMER_Set(self, "parryTime", -1);
		if (self->client->NPC_class == CLASS_DESANN ||
			self->client->NPC_class == CLASS_VADER ||
			!Q_stricmp("Yoda", self->NPC_type) ||
			!Q_stricmp("md_Yoda", self->NPC_type))
		{
			//less for Desann
			self->client->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE] = level.time + (3 - g_npcspskill.integer) * 50;
		}
		else if (self->client->NPC_class == CLASS_SITHLORD)
		{
			//less for Desann
			self->client->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE] = level.time + (3 - g_npcspskill.integer) * 50;
		}
		else if (self->client->NPC_class == CLASS_VADER)
		{
			//less for Desann
			self->client->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE] = level.time + (3 - g_npcspskill.integer) * 50;
		}
		else if (self->NPC->rank >= RANK_LT_JG)
		{
			self->client->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE] = level.time + (3 - g_npcspskill.integer) * 100; //300
		}
		else
		{
			self->client->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE] = level.time + (3 - g_npcspskill.integer) * 200; //500
		}

		if (!Q_irand(0, 3))
		{
			//ouch... maybe switch up which saber power level we're using
			if ((self->client->ps.fd.saberAnimLevel == SS_STRONG
				|| self->client->ps.fd.saberAnimLevel == SS_MEDIUM
				|| (self->client->ps.fd.saberAnimLevel == SS_DESANN || self->client->ps.fd.saberAnimLevel == SS_BLASTER) // SS_BLASTER (2026-10-14)
				|| self->client->ps.fd.saberAnimLevel == SS_FAST
				|| self->client->ps.fd.saberAnimLevel == SS_TAVION)
				&& !npc_is_staff_style(self)
				&& !npc_is_dual_style(self))
			{
				//my saber is not in a parrying position
				Jedi_AdjustSaberAnimLevel(self, Q_irand(SS_FAST, SS_STRONG));
			}
		}

		if (!Q_irand(0, 1))
		{
			Jedi_Aggression(self, -1);
		}
		if (d_JediAI.integer || d_combatinfo.integer || g_DebugSaberCombat.integer)
		{
			Com_Printf("(%d) PAIN: aggression %d, no parry until %d\n", level.time, self->NPC->stats.aggression,
				level.time + 500);
		}
		//for testing only
		// Figure out what quadrant the hit was in.
		if (d_JediAI.integer || d_combatinfo.integer || g_DebugSaberCombat.integer)
		{
			vec3_t diff, fwdangles = { 0 }, right;

			VectorSubtract(point, self->client->renderInfo.eyePoint, diff);
			diff[2] = 0;
			fwdangles[1] = self->client->ps.viewangles[1];
			AngleVectors(fwdangles, NULL, right, NULL);
			const float rightdot = DotProduct(right, diff);
			const float zdiff = point[2] - self->client->renderInfo.eyePoint[2];

			Com_Printf("(%d) saber hit at height %4.2f, zdiff: %4.2f, rightdot: %4.2f\n", level.time,
				point[2] - self->r.absmin[2], zdiff, rightdot);
		}
	}
	else
	{
		//attack
		Jedi_Aggression(self, 2);
	}

	self->NPC->enemyCheckDebounceTime = 0;

	WP_ForcePowerStop(self, FP_GRIP);
	WP_ForcePowerStop(self, FP_GRASP);

	NPC_Pain(self, attacker, damage);

	if (!damage && self->health > 0)
	{
		if (!npc_is_dark_jedi(self))
		{
			G_AddVoiceEvent(self, Q_irand(EV_PUSHED1, EV_PUSHED3), 2000);
		}
		else
		{
			G_AddVoiceEvent(self, Q_irand(EV_ANGER1, EV_ANGER3), 2000);
		}
	}

	//drop me from the ceiling if I'm on it
	if (Jedi_WaitingAmbush(self))
	{
		self->client->noclip = qfalse;
	}
	if (self->client->ps.legsAnim == BOTH_CEILING_CLING)
	{
		NPC_SetAnim(self, SETANIM_LEGS, BOTH_CEILING_DROP, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	}
	if (self->client->ps.torsoAnim == BOTH_CEILING_CLING)
	{
		NPC_SetAnim(self, SETANIM_TORSO, BOTH_CEILING_DROP, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	}

	//check special defenses
	if (attacker
		&& attacker->client
		&& !OnSameTeam(self, attacker))
	{
		//hit by a client
		//FIXME: delay this until *after* the pain anim?
		if (mod == MOD_FORCE_DARK
			|| mod == MOD_FORCE_LIGHTNING)
		{
			//see if we should turn on absorb
			if ((self->client->ps.fd.forcePowersKnown & 1 << FP_ABSORB) != 0
				&& (self->client->ps.fd.forcePowersActive & 1 << FP_ABSORB) == 0)
			{
				//know absorb and not already using it
				if (attacker->s.number >= MAX_CLIENTS //enemy is an NPCS.NPC
					|| Q_irand(0, g_npcspskill.integer + 1)) //enemy is player
				{
					if (Q_irand(0, self->NPC->rank) > RANK_ENSIGN)
					{
						if (!Q_irand(0, 5))
						{
							ForceAbsorb(self);
						}
					}
				}
			}
		}
		else if (damage > Q_irand(5, 20))
		{
			//respectable amount of normal damage
			if ((self->client->ps.fd.forcePowersKnown & 1 << FP_PROTECT) != 0
				&& (self->client->ps.fd.forcePowersActive & 1 << FP_PROTECT) == 0)
			{
				//know protect and not already using it
				if (attacker->s.number >= MAX_CLIENTS //enemy is an NPCS.NPC
					|| Q_irand(0, g_npcspskill.integer + 1)) //enemy is player
				{
					if (Q_irand(0, self->NPC->rank) > RANK_ENSIGN)
					{
						if (!Q_irand(0, 1))
						{
							if (attacker->s.number < MAX_CLIENTS
								&& (self->NPC->aiFlags & NPCAI_BOSS_CHARACTER
									|| self->client->NPC_class == CLASS_SHADOWTROOPER)
								&& Q_irand(0, 6 - g_npcspskill.integer))
							{
							}
							else
							{
								ForceProtect(self);
							}
						}
					}
				}
			}
		}
	}
}



static qboolean Jedi_CheckDanger(void)
{
	const int alert_event = NPC_CheckAlertEvents(qtrue, qtrue, -1, qfalse, AEL_MINOR);
	if (level.alertEvents[alert_event].level >= AEL_DANGER)
	{
		//run away!
		if (!level.alertEvents[alert_event].owner
			|| !level.alertEvents[alert_event].owner->client
			|| level.alertEvents[alert_event].owner != NPCS.NPC && level.alertEvents[alert_event].owner->client->playerTeam
			!=
			NPCS.NPC->client->playerTeam)
		{
			//no owner
			return qfalse;
		}
		G_SetEnemy(NPCS.NPC, level.alertEvents[alert_event].owner);
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
		TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
		return qtrue;
	}
	return qfalse;
}



qboolean Jedi_CheckAmbushPlayer(void)
{
	float target_dist = 0.0f;

	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		gentity_t* player = &g_entities[i];

		if (!player || !player->client)
		{
			continue;
		}

		if (!NPC_ValidEnemy(player))
		{
			continue;
		}

		if (NPCS.NPC->client->ps.powerups[PW_CLOAKED] || !NPC_SomeoneLookingAtMe(NPCS.NPC))
			//rwwFIXMEFIXME: Need to pay attention to who is under crosshair for each player or something.
		{
			//if I'm not cloaked and the player's crosshair is on me, I will wake up, otherwise do this stuff down here...
			if (!trap->InPVS(player->r.currentOrigin, NPCS.NPC->r.currentOrigin))
			{
				//must be in same room
				continue;
			}
			if (!NPCS.NPC->client->ps.powerups[PW_CLOAKED])
			{
				NPC_SetLookTarget(NPCS.NPC, 0, 0);
			}
			const float zDiff = NPCS.NPC->r.currentOrigin[2] - player->r.currentOrigin[2];
			if (zDiff <= 0 || zDiff > 512)
			{
				//never ambush if they're above me or way way below me
				continue;
			}

			//If the target is this close, then wake up regardless
			if ((target_dist = DistanceHorizontalSquared(player->r.currentOrigin, NPCS.NPC->r.currentOrigin)) > 4096)
			{
				//closer than 64 - always ambush
				if (target_dist > 147456)
				{
					//> 384, not close enough to ambush
					continue;
				}
				//Check FOV first
				if (NPCS.NPC->client->ps.powerups[PW_CLOAKED])
				{
					if (InFOV(player, NPCS.NPC, 30, 90) == qfalse)
					{
						continue;
					}
				}
				else
				{
					if (InFOV(player, NPCS.NPC, 45, 90) == qfalse)
					{
						continue;
					}
				}
			}

			if (!NPC_ClearLOS4(player))
			{
				continue;
			}
		}

		//Got him, return true;
		G_SetEnemy(NPCS.NPC, player);
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
		TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
		return qtrue;
	}

	//Didn't get anyone.
	return qfalse;
}



void Jedi_Ambush(gentity_t* self)
{
	self->client->noclip = qfalse;
	self->client->ps.pm_flags |= PMF_JUMPING | PMF_SLOW_MO_FALL;
	NPC_SetAnim(self, SETANIM_BOTH, BOTH_CEILING_DROP, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	self->client->ps.weaponTime = NPCS.NPC->client->ps.torsoTimer;
	if (self->client->NPC_class != CLASS_BOBAFETT && self->client->NPC_class != CLASS_MANDO &&
		self->client->NPC_class != CLASS_ROCKETTROOPER)
	{
		WP_ActivateSaber(self);
	}
	Jedi_Decloak(self);
	G_AddVoiceEvent(self, Q_irand(EV_ANGER1, EV_ANGER3), 10000);
}



qboolean Jedi_WaitingAmbush(const gentity_t* self)
{
	if (self->spawnflags & JSF_AMBUSH && self->client->noclip)
	{
		return qtrue;
	}
	return qfalse;
}



/*
-------------------------
Jedi_Patrol
-------------------------
*/

static void Jedi_Patrol(void)
{
	NPCS.NPC->client->ps.saberBlocked = BLOCKED_NONE;

	// AMBUSH WAITING STATE
	if (Jedi_WaitingAmbush(NPCS.NPC) == qtrue)
	{
		NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_CEILING_CLING,
			SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

		if ((NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES) != 0)
		{
			if (Jedi_CheckAmbushPlayer() == qtrue ||
				Jedi_CheckDanger() == qtrue)
			{
				Jedi_Ambush(NPCS.NPC);
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
		}
	}
	else if ((NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES) != 0)
	{
		// NORMAL PATROL ENEMY SCAN
		gentity_t* best_enemy = NULL;
		float best_enemy_dist = Q3_INFINITE;

		for (int i = 0; i < ENTITYNUM_WORLD; i++)
		{
			gentity_t* enemy = &g_entities[i];

			if (enemy == NULL || enemy->client == NULL)
			{
				continue;
			}
			if (NPC_ValidEnemy(enemy) == qfalse)
			{
				continue;
			}

			//
			// PVS CHECK WITH OPTIONAL INVESTIGATION FALLBACK
			//
			if (trap->InPVS(NPCS.NPC->r.currentOrigin, enemy->r.currentOrigin) == qfalse)
			{
				if (g_npcspskill.integer != 0)
				{
					const float range = (float)g_npc_is_smart_range.integer;

					const float dist_sq = (float)DistanceSquared(NPCS.NPC->r.currentOrigin, enemy->r.currentOrigin);

					if (dist_sq <= range * range)
					{
						vec3_t sightPos;
						VectorCopy(NPCS.NPC->r.currentOrigin, sightPos);

						AddSightEvent(
							enemy,
							sightPos,
							range,
							AEL_SUSPICIOUS,
							0.0f
						);

						// Continue evaluating this enemy
					}
					else
					{
						continue;
					}
				}
				else
				{
					continue;
				}
			}

			// Enemy is potentially visible
			const float enemy_dist = DistanceSquared(NPCS.NPC->r.currentOrigin, enemy->r.currentOrigin);

			if (enemy->s.number < MAX_CLIENTS || enemy_dist < best_enemy_dist)
			{
				// Close enough or sufficiently investigated
				if (enemy_dist < (220.0f * 220.0f) ||
					(NPCS.NPCInfo->investigateCount >= 3 &&
						(!NPCS.NPC->client->ps.saberHolstered) == qtrue))
				{
					G_SetEnemy(NPCS.NPC, enemy);
					NPCS.NPCInfo->stats.aggression = 3;
					break;
				}

				// Saber throw threat detection
				if (enemy->client->ps.saberInFlight == qtrue &&
					(!enemy->client->ps.saberHolstered) == qtrue)
				{
					vec3_t saber_dir2_me;
					vec3_t saber_move_dir;

					const gentity_t* saber = &g_entities[enemy->client->ps.saberEntityNum];

					VectorSubtract(NPCS.NPC->r.currentOrigin, saber->r.currentOrigin, saber_dir2_me);
					const float sabers_dist = VectorNormalize(saber_dir2_me);

					VectorCopy(saber->s.pos.trDelta, saber_move_dir);
					VectorNormalize(saber_move_dir);

					if (DotProduct(saber_move_dir, saber_dir2_me) > 0.5f &&
						sabers_dist < 200.0f)
					{
						G_SetEnemy(NPCS.NPC, enemy);
						NPCS.NPCInfo->stats.aggression = 3;
						break;
					}
				}

				best_enemy_dist = enemy_dist;
				best_enemy = enemy;
			}
		}

		// If no enemy set yet
		if (NPCS.NPC->enemy == NULL)
		{
			if (best_enemy == NULL)
			{
				Jedi_AggressionErosion(-1);
			}
			else
			{
				if (NPC_ClearLOS4(best_enemy) == qtrue)
				{
					if ((NPCS.NPCInfo->aiFlags & NPCAI_NO_JEDI_DELAY) != 0)
					{
						if (DistanceHorizontalSquared(NPCS.NPC->r.currentOrigin, best_enemy->r.currentOrigin) <
							(1024.0f * 1024.0f))
						{
							G_SetEnemy(NPCS.NPC, best_enemy);
							NPCS.NPCInfo->stats.aggression = 20;
						}
					}
					else if (best_enemy->s.number >= MAX_CLIENTS)
					{
						G_SetEnemy(NPCS.NPC, best_enemy);
						NPCS.NPCInfo->stats.aggression = 3;
					}
					else if (NPCS.NPC->client->NPC_class != CLASS_BOBAFETT &&
						NPCS.NPC->client->NPC_class != CLASS_MANDO)
					{
						// Player detection stages
						if (TIMER_Done(NPCS.NPC, "watchTime") == qtrue)
						{
							if (TIMER_Get(NPCS.NPC, "watchTime") == -1)
							{
								TIMER_Set(NPCS.NPC, "watchTime", Q_irand(3000, 5000));
								goto finish;
							}

							if (NPCS.NPCInfo->investigateCount == 0)
							{
								G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_JDETECTED1, EV_JDETECTED3), 3000);
							}

							NPCS.NPCInfo->investigateCount++;
							TIMER_Set(NPCS.NPC, "watchTime", Q_irand(4000, 10000));
						}

						if (best_enemy_dist < (440.0f * 440.0f) ||
							NPCS.NPCInfo->investigateCount >= 2)
						{
							NPC_FaceEntity(best_enemy, qtrue);

							if (best_enemy_dist < (330.0f * 330.0f))
							{
								if (NPCS.NPC->client->ps.saberInFlight == qfalse)
								{
									WP_ActivateSaber(NPCS.NPC);
								}
							}
						}
						else if (best_enemy_dist < (550.0f * 550.0f) ||
							NPCS.NPCInfo->investigateCount == 1)
						{
							if (TIMER_Done(NPCS.NPC, "watchTime") == qtrue)
							{
								NPC_FaceEntity(best_enemy, qtrue);
							}
						}
						else
						{
							NPC_SetLookTarget(NPCS.NPC, best_enemy->s.number, 0);
						}
					}
				}
				else if (TIMER_Done(NPCS.NPC, "watchTime") == qtrue)
				{
					NPC_ClearLookTarget(NPCS.NPC);
				}
			}
		}
	}

finish:

	// Move toward patrol goal
	if (UpdateGoal())
	{
		NPCS.ucmd.buttons |= BUTTON_WALKING;
		NPC_MoveToGoal(qtrue);
	}

	NPC_UpdateAngles(qtrue, qtrue);

	if (NPCS.NPC->enemy != NULL)
	{
		NPCS.NPCInfo->enemyCheckDebounceTime =
			level.time + Q_irand(3000, 10000);
	}
}



static qboolean Jedi_CanPullBackSaber(const gentity_t* self)
{
	if (self->client->ps.saberBlocked == BLOCKED_PARRY_BROKEN && !TIMER_Done(self, "parryTime"))
	{
		return qfalse;
	}

	if (self->NPC && (self->NPC->aiFlags & NPCAI_BOSS_CHARACTER))
	{
		return qtrue;
	}

	if (self->painDebounceTime > level.time)
	{
		return qfalse;
	}

	return qtrue;
}



/*
-------------------------
NPC_BSJedi_FollowLeader
-------------------------
*/
void NPC_BSJedi_FollowLeader(void)
{
	NPCS.NPC->client->ps.saberBlocked = BLOCKED_NONE;
	if (!NPCS.NPC->enemy)
	{
		Jedi_AggressionErosion(-1);
	}

	//did we drop our saber?  If so, go after it!
	if (NPCS.NPC->client->ps.saberInFlight)
	{
		//saber is not in hand
		if (NPCS.NPC->client->ps.saberEntityNum < ENTITYNUM_NONE && NPCS.NPC->client->ps.saberEntityNum > 0)
			//player is 0
		{
			//
			if (g_entities[NPCS.NPC->client->ps.saberEntityNum].s.pos.trType == TR_STATIONARY)
			{
				//fell to the ground, try to pick it up...
				if (Jedi_CanPullBackSaber(NPCS.NPC))
				{
					//FIXME: if it's on the ground and we just pulled it back to us, should we
					//		stand still for a bit to make sure it gets to us...?
					//		otherwise we could end up running away from it while it's on its
					//		way back to us and we could lose it again.
					NPCS.NPC->client->ps.saberBlocked = BLOCKED_NONE;
					NPCS.NPCInfo->goalEntity = &g_entities[NPCS.NPC->client->ps.saberEntityNum];
					NPCS.ucmd.buttons &= ~BUTTON_ALT_ATTACK;
					NPCS.ucmd.buttons |= BUTTON_ATTACK;
					if (NPCS.NPC->enemy && NPCS.NPC->enemy->health > 0)
					{
						//get our saber back NOW!
						if (!NPC_MoveToGoal(qtrue)) //Jedi_Move( NPCInfo->goalEntity, qfalse );
						{
							//can't nav to it, try jumping to it
							NPC_FaceEntity(NPCS.NPCInfo->goalEntity, qtrue);
							if (!Jedi_TryJump(NPCS.NPCInfo->goalEntity))
							{
								Jedi_Move(NPCS.NPCInfo->goalEntity, qfalse);
							}
						}
						NPC_UpdateAngles(qtrue, qtrue);
						return;
					}
				}
			}
		}
	}

	if (NPCS.NPCInfo->goalEntity)
	{
		trace_t trace;

		if (Jedi_Jumping(NPCS.NPCInfo->goalEntity))
		{
			//in mid-jump
			return;
		}

		if (!NAV_CheckAhead(NPCS.NPC, NPCS.NPCInfo->goalEntity->r.currentOrigin, &trace,
			NPCS.NPC->clipmask & ~CONTENTS_BODY | CONTENTS_BOTCLIP))
		{
			//can't get straight to him
			if (NPC_ClearLOS4(NPCS.NPCInfo->goalEntity) && NPC_FaceEntity(NPCS.NPCInfo->goalEntity, qtrue))
			{
				//no line of sight
				if (Jedi_TryJump(NPCS.NPCInfo->goalEntity))
				{
					//started a jump
					return;
				}
				Jedi_Move(NPCS.NPCInfo->goalEntity, qfalse);
				return;
			}
		}
		if (NPCS.NPCInfo->aiFlags & NPCAI_BLOCKED)
		{
			//try to jump to the blockedDest
			if (fabs(NPCS.NPCInfo->blockedDest[2] - NPCS.NPC->r.currentOrigin[2]) > 64)
			{
				gentity_t* tempGoal = G_Spawn(); //ugh, this is NOT good...?
				G_SetOrigin(tempGoal, NPCS.NPCInfo->blockedDest);
				trap->LinkEntity((sharedEntity_t*)tempGoal);
				TIMER_Set(NPCS.NPC, "jumpChaseDebounce", -1);
				if (Jedi_TryJump(tempGoal))
				{
					//going to jump to the dest
					G_FreeEntity(tempGoal);
					return;
				}
				G_FreeEntity(tempGoal);
			}
		}
	}
	//try normal movement
	NPC_BSFollowLeader();

	if (!NPCS.NPC->enemy &&
		NPCS.NPC->health < NPCS.NPC->client->pers.maxHealth &&
		(NPCS.NPC->client->ps.fd.forcePowersKnown & 1 << FP_HEAL) != 0 &&
		(NPCS.NPC->client->ps.fd.forcePowersActive & 1 << FP_HEAL) == 0 &&
		TIMER_Done(NPCS.NPC, "FollowHealDebouncer"))
	{
		if (Q_irand(0, 3) == 0)
		{
			TIMER_Set(NPCS.NPC, "FollowHealDebouncer", Q_irand(12000, 18000));
			ForceHeal(NPCS.NPC);

			if (NPC_IsJedi(NPCS.NPC))
			{
				// Do deflect taunt...
				G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_GLOAT1, EV_GLOAT3), 5000 + Q_irand(0, 15000));
			}
		}
		else
		{
			TIMER_Set(NPCS.NPC, "FollowHealDebouncer", Q_irand(1000, 2000));
		}
	}
}



// return qtrue if NPCS.NPC is allowed to perform a kata now
static qboolean NPC_CanDoKata(gentity_t* self)
{
	if (!self || !self->client || !self->NPC)
	{
		return qfalse;
	}

	// only high rank NPCs allowed
	if (self->NPC->rank < RANK_LT_COMM)
	{
		return qfalse;
	}

	// Cooldown
	if (!TIMER_Done(NPCS.NPC, "noKata"))
	{
		return qfalse;
	}

	// require a valid enemy with client state
	gentity_t* enemy = self->enemy;
	if (!enemy || !enemy->client)
	{
		return qfalse;
	}

	return qtrue;
}



static qboolean Jedi_CheckKataAttack(void)
{
	if (!TIMER_Done(NPCS.NPC, "KataTime"))
	{
		//still doin kata from last time
		return qfalse;
	}
	if (NPC_CanDoKata(NPCS.NPC))
	{
		//only top-level guys and bosses do this
		if (NPCS.ucmd.buttons & BUTTON_ATTACK)
		{
			//attacking
			if (g_saberNewControlScheme.integer
				&& !(NPCS.ucmd.buttons & BUTTON_FORCEPOWER)
				|| !g_saberNewControlScheme.integer
				&& !(NPCS.ucmd.buttons & BUTTON_ALT_ATTACK))
			{
				//not already going to do a kata move somehow
				if (NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE)
				{
					//on the ground
					if (NPCS.ucmd.upmove <= 0 && NPCS.NPC->client->ps.fd.forceJumpCharge <= 0)
					{
						//not going to try to jump
						// --- Special attack frequency modifier ---
						float freqMod = 1.0f;
						if (g_npcSpecialAttackFreq.value > 0.0f)
						{
							freqMod = g_npcSpecialAttackFreq.value;
						}
						// Clamp to [0.01, 10.0] for sanity
						if (freqMod < 0.01f) freqMod = 0.01f;
						if (freqMod > 10.0f) freqMod = 10.0f;

						qboolean doSpecial = qfalse;
						if (Q_irand(0, g_npcspskill.integer + 1) && !Q_irand(0, 9))
						{
							// Default chance
							if (freqMod >= 1.0f || flrand(0.0f, 1.0f) < freqMod)
							{
								doSpecial = qtrue;
							}
						}
						else if (freqMod < 1.0f && flrand(0.0f, 1.0f) < freqMod)
						{
							// If freqMod is low, allow a rare special even if the above failed
							doSpecial = qtrue;
						}
						if (doSpecial)
						{
							NPCS.ucmd.upmove = 0;
							VectorClear(NPCS.NPC->client->ps.moveDir);
							if (g_saberNewControlScheme.integer)
							{
								NPCS.ucmd.buttons |= BUTTON_FORCEPOWER;
							}
							else
							{
								NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
							}
							return qtrue;
						}
					}
				}
			}
		}
		TIMER_Set(NPCS.NPC, "KataTime", Q_irand(5000, 7000));
	}
	return qfalse;
}



// Helper: return qtrue if the NPC class is one of the excluded large/animal/boss types
static qboolean NPC_IsExcludedForGestures(gentity_t* NPC)
{
	if (!NPC || !NPC->client)
	{
		return qfalse;
	}

	switch (NPC->client->NPC_class)
	{
	case CLASS_RANCOR:
	case CLASS_WAMPA:
	case CLASS_SAND_CREATURE:
	case CLASS_DESANN:
		return qtrue;
	default:
		return qfalse;
	}
}



// Helper: return qtrue if both NPC and enemy are on the ground and NPC is in a neutral state
static qboolean NPC_CanReactToEnemy(gentity_t* NPC, gentity_t* enemy)
{
	if (!NPC || !NPC->client || !enemy || !enemy->client)
	{
		return qfalse;
	}

	// must both be on ground
	if (NPC->client->ps.groundEntityNum == ENTITYNUM_NONE ||
		enemy->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		return qfalse;
	}

	// NPC must not be attacking or in special saber animations
	if (PM_SaberInAttack(NPC->client->ps.saberMove) ||
		PM_SaberInStart(NPC->client->ps.saberMove) ||
		PM_SpinningSaberAnim(NPC->client->ps.torsoAnim) ||
		PM_SaberInSpecialAttack(NPC->client->ps.torsoAnim))
	{
		return qfalse;
	}

	// enemy must be in front of NPC
	if (!InFront(enemy->r.currentOrigin, NPC->r.currentOrigin, NPC->client->ps.viewangles, 0.7f))
	{
		return qfalse;
	}

	return qtrue;
}



// Helper: play gloat voice and optionally deactivate saber
static void NPC_PlayGloatAndMaybeSheathe(gentity_t* NPC)
{
	if (!NPC || !NPC->client)
	{
		return;
	}

	if (NPC->s.weapon == WP_SABER)
	{
		WP_DeactivateSaber(NPC);
	}

	G_AddVoiceEvent(NPC, Q_irand(EV_GLOAT1, EV_GLOAT3), Q_irand(12000, 15000));
}



static void NPC_HandleSpeechDebounceAndIncrement(gentity_t* NPC)
{
	if (!NPC || !NPC->client)
	{
		return;
	}

	if (!TIMER_Done(NPC, "talkDebounce") || Q_irand(0, 10))
	{
		return;
	}

	if (NPCS.NPCInfo->enemyCheckDebounceTime >= 8)
	{
		return;
	}

	int speech = -1;
	switch (NPCS.NPCInfo->enemyCheckDebounceTime)
	{
	case 0:
	case 1:
	case 2:
		speech = EV_TAUNT1 + NPCS.NPCInfo->enemyCheckDebounceTime;
		break;
	case 3:
	case 4:
	case 5:
		speech = EV_GLOAT1 + (NPCS.NPCInfo->enemyCheckDebounceTime - 3);
		break;
	case 6:
	case 7:
		speech = EV_COMBAT1 + (NPCS.NPCInfo->enemyCheckDebounceTime - 6);
		break;
	default:
		break;
	}

	NPCS.NPCInfo->enemyCheckDebounceTime++;

	if (speech != -1)
	{
		G_AddVoiceEvent(NPC, speech, Q_irand(3000, 5000));
		TIMER_Set(NPC, "talkDebounce", Q_irand(5000, 7000));
	}
}

// Assumes these globals/types exist in your codebase:
// - gentity_t, playerState_t, npcInfo_t *NPCS.NPCInfo, gentity_t *NPCS.NPC
// - usercmd_t NPCS.ucmd;
// - qboolean jedi_in_camera;
// - TIMER_Set, TIMER_Done, Jedi_Move, Jedi_FaceEnemy, NPC_UpdateAngles
// - ForceThrow, PM_SaberInBrokenParry, PM_SaberInSpecialAttack,
//   PM_SpinningSaberAnim, PM_SaberInTransition, InFront, etc.

// =====================
// Input Helpers
// =====================

static inline void JediPressAttack(void)
{
	NPCS.ucmd.buttons |= BUTTON_ATTACK;
}



static inline void JediPressAltAttack(void)
{
	NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
}


// =====================
// Distance Helpers
// =====================

static float JediDistanceToEnemy(gentity_t* self)
{
	if (!self || !self->enemy)
	{
		return 99999.0f;
	}

	vec3_t diff;
	VectorSubtract(self->enemy->r.currentOrigin, self->r.currentOrigin, diff);
	return VectorLength(diff);
}



static qboolean JediInAttackRange(gentity_t* self, float minDist, float maxDist)
{
	const float dist = JediDistanceToEnemy(self);
	return (dist >= minDist && dist <= maxDist) ? qtrue : qfalse;
}



static qboolean JediHasLineOfSight(gentity_t* self)
{
	if (!self || !self->enemy)
	{
		return qfalse;
	}

	vec3_t enemyOrg;
	vec3_t selfOrg;
	vec3_t viewAngles;

	VectorCopy(self->enemy->r.currentOrigin, enemyOrg);
	VectorCopy(self->r.currentOrigin, selfOrg);
	VectorCopy(self->client->ps.viewangles, viewAngles);

	return InFront(enemyOrg, selfOrg, viewAngles, 0.5f);
}



// =====================
// Cooldown Helpers
// =====================

static qboolean JediAttackCooldownReady(gentity_t* self)
{
	return TIMER_Done(self, "cinematicAttackCooldown") ? qtrue : qfalse;
}



static void JediSetAttackCooldown(gentity_t* self, int ms)
{
	TIMER_Set(self, "cinematicAttackCooldown", ms);
}



// =====================
// Kata Helpers
// =====================

static qboolean Jedi_CanUseKata(gentity_t* self)
{
	const playerState_t* ps = &self->client->ps;

	// Must not be defending
	if (ps->saberBlocked != BLOCKED_NONE)
	{
		return qfalse;
	}

	// Must not already be in a committed saber move
	if (ps->saberMove != LS_NONE && !PM_SaberInTransition(ps->saberMove))
	{
		return qfalse;
	}

	// Must have some Force
	if (ps->fd.forcePower < 20)
	{
		return qfalse;
	}

	// Must be aggressive enough
	if (NPCS.NPCInfo->stats.aggression < 5)
	{
		return qfalse;
	}

	// Must have some Distance (no point-blank katas)
	if (JediDistanceToEnemy(self) < 200.0f)
	{
		return qfalse;
	}

	// Cooldown
	if (!TIMER_Done(NPCS.NPC, "noKata"))
	{
		return qfalse;
	}

	return qtrue;
}



// =====================
// Spacing Handler
// =====================

static void JediHandleSpacing(gentity_t* self)
{
	if (!self || !self->enemy)
	{
		return;
	}

	const float dist = JediDistanceToEnemy(self);
	const qboolean airborne = (qboolean)(self->client->ps.groundEntityNum == ENTITYNUM_NONE);

	// =====================================================
	// 1. Long-range: Jump-in attacks (350+ units)
	// =====================================================
	if (dist > 350.0f)
	{
		// Do NOT allow jump-ins during cinematic camera
		if (!jedi_in_camera)
		{
			// Only attempt a jump-in every 2–4 seconds
			if (level.time >= self->NPC->jumpTime)
			{
				// 1 in 20 chance per attempt
				if (!Q_irand(0, 19))
				{
					Jedi_FaceEnemy(qtrue);
					NPC_UpdateAngles(qtrue, qtrue);

					// Jump forward
					NPCS.ucmd.upmove = 127;
					NPCS.ucmd.forwardmove = 127;

					// Only trigger saber special once airborne
					if (self->client->ps.groundEntityNum == ENTITYNUM_NONE)
					{
						// Only allow these if kata conditions are met
						if (Jedi_CanUseKata(self))
						{
							switch (Q_irand(0, 2))
							{
							case 0:
								self->client->ps.saberMove = LS_A_JUMP_T__B_; // DFA
								break;
							case 1:
								self->client->ps.saberMove = LS_A_LUNGE;      // Lunge
								break;
							case 2:
								self->client->ps.saberMove = LS_SPINATTACK;   // Spin
								break;
							}
							TIMER_Set(NPCS.NPC, "noKata", jedi_get_kata_cooldown_duration());
						}
					}

					// Set next allowed jump-in time (2–4 seconds)
					self->NPC->jumpTime = level.time + Q_irand(2000, 4000);

					return;
				}
			}
		}

		// Normal chase (also used during _camera)
		NPCS.NPCInfo->goalEntity = self->enemy;
		Jedi_Move(self->enemy, qfalse);
		NPCS.ucmd.forwardmove = 127;
		Jedi_FaceEnemy(qtrue);
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	// =====================================================
	// 2. Mid-range: Dash-in lunges (200–350 units)
	// =====================================================
	if (dist > 200.0f)
	{
		if (level.time >= self->NPC->jumpTime)
		{
			// 1 in 10 chance per attempt
			if (!Q_irand(0, 9))
			{
				Jedi_FaceEnemy(qtrue);
				NPC_UpdateAngles(qtrue, qtrue);

				// Burst forward
				NPCS.ucmd.forwardmove = 127;

				if (airborne && !jedi_in_camera)
				{
					// Only allow this special if kata is allowed
					if (Jedi_CanUseKata(self))
					{
						self->client->ps.saberMove = LS_A_LUNGE;
						self->client->ps.weaponTime = 400;
						TIMER_Set(NPCS.NPC, "noKata", jedi_get_kata_cooldown_duration());
					}
				}

				// Set next allowed jump-in time (2–4 seconds)
				self->NPC->jumpTime = level.time + Q_irand(2000, 4000);

				return;
			}
		}

		// Normal chase
		NPCS.NPCInfo->goalEntity = self->enemy;
		Jedi_Move(self->enemy, qfalse);
		NPCS.ucmd.forwardmove = 127;
		Jedi_FaceEnemy(qtrue);
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	// =====================================================
	// 3. Close-range: Circle-strafe approach (80–200 units)
	// =====================================================
	vec3_t pvel;
	if (self->enemy->client)
	{
		VectorCopy(self->enemy->client->ps.velocity, pvel);
	}
	else
	{
		VectorClear(pvel); // or copy from self->enemy->s.pos.trDelta if appropriate
	}

	float pSpeed = VectorLength(pvel);

	if (pSpeed > 50.0f)
	{
		// Player strafing right
		if (pvel[1] > 80)
		{
			NPCS.ucmd.rightmove = -64;
			NPCS.ucmd.forwardmove = 48;
			Jedi_FaceEnemy(qtrue);
			NPC_UpdateAngles(qtrue, qtrue);
			return;
		}

		// Player strafing left
		if (pvel[1] < -80)
		{
			NPCS.ucmd.rightmove = 64;
			NPCS.ucmd.forwardmove = 48;
			Jedi_FaceEnemy(qtrue);
			NPC_UpdateAngles(qtrue, qtrue);
			return;
		}

		// Player backing up
		if (pvel[0] < -80)
		{
			NPCS.ucmd.forwardmove = 127;
			Jedi_FaceEnemy(qtrue);
			NPC_UpdateAngles(qtrue, qtrue);
			return;
		}

		// Player rushing forward
		if (pvel[0] > 80)
		{
			NPCS.ucmd.rightmove = (Q_irand(0, 1) ? 64 : -64);
			Jedi_FaceEnemy(qtrue);
			NPC_UpdateAngles(qtrue, qtrue);
			return;
		}
	}

	// =====================================================
	// 4. Too close: back off slightly
	// =====================================================
	if (dist < 48.0f)
	{
		NPCS.ucmd.forwardmove = -32;
		return;
	}

	// =====================================================
	// 5. Ideal range — do nothing
	// =====================================================
	// Let attack logic take over
}


static void JediHandleCounterattacks(gentity_t* self)
{
	if (!self || !self->enemy || !self->client)
		return;

	saberBlockedType_t blockType = (saberBlockedType_t)self->client->ps.saberBlocked;

	// Use existing saberBlockingTime as the cooldown
	if (self->client->ps.saberBlockingTime > level.time)
		return;

	switch (blockType)
	{
	case BLOCKED_BOUNCE_MOVE:
	case BLOCKED_ATK_BOUNCE:
		self->NPC->counterFlags |= AIFLAG_COUNTERATTACK;
		break;

	case BLOCKED_PARRY_BROKEN:
		self->NPC->counterFlags |= AIFLAG_HEAVYCOUNTER;
		self->NPC->counterFlags |= AIFLAG_RIPOSTE;
		break;

	case BLOCKED_UPPER_RIGHT:
	case BLOCKED_UPPER_LEFT:
	case BLOCKED_LOWER_RIGHT:
	case BLOCKED_LOWER_LEFT:
	case BLOCKED_TOP:
		self->NPC->counterFlags |= AIFLAG_COUNTERATTACK;
		break;

	case BLOCKED_UPPER_RIGHT_PROJ:
	case BLOCKED_UPPER_LEFT_PROJ:
	case BLOCKED_LOWER_RIGHT_PROJ:
	case BLOCKED_LOWER_LEFT_PROJ:
	case BLOCKED_TOP_PROJ:
		self->NPC->counterFlags |= AIFLAG_FORCECOUNTER;
		break;

	case BLOCKED_BACK:
		self->NPC->counterFlags |= AIFLAG_SPINCOUNTER;
		break;

	default:
		return;
	}

	// Reuse saberBlockingTime as the counter cooldown
	self->client->ps.saberBlockingTime = level.time + 500;
}



static saberCombo_t JediBuildCombo(const saberMoveName_t* moves, int maxLen)
{
	saberCombo_t combo = { 0 };
	combo.valid = qtrue;
	combo.useForcePushFinisher = qfalse;

	for (int i = 0; i < maxLen; i++)
	{
		combo.moves[i] = moves[i];
		if (moves[i] == LS_NONE)
		{
			combo.length = i;
			return combo;
		}
	}

	combo.length = maxLen;
	return combo;
}



// =====================
// Combo Selection Logic
// =====================

static saberCombo_t JediChooseCombo(gentity_t* self)
{
	saberCombo_t combo = { 0 };
	combo.valid = qfalse;
	combo.length = 0;
	combo.useForcePushFinisher = qfalse;

	if (jedi_in_camera)
	{
		return combo; // don't do this in camera mode
	}

	if (!self || !self->enemy)
	{
		return combo;
	}

	// =====================
	// Counterattack Override
	// =====================
	if (self->NPC->counterFlags & AIFLAG_HEAVYCOUNTER)
	{
		const saberMoveName_t* chosen =
			s_heavyComboPool[Q_irand(0, ARRAY_LEN(s_heavyComboPool) - 1)];

		self->NPC->counterFlags &= ~(AIFLAG_HEAVYCOUNTER | AIFLAG_RIPOSTE |
			AIFLAG_COUNTERATTACK | AIFLAG_FORCECOUNTER |
			AIFLAG_SPINCOUNTER);

		return JediBuildCombo(chosen, 4);
	}

	if (self->NPC->counterFlags & AIFLAG_RIPOSTE)
	{
		const saberMoveName_t* chosen =
			s_duelistCounterPool[Q_irand(0, ARRAY_LEN(s_duelistCounterPool) - 1)];

		self->NPC->counterFlags &= ~(AIFLAG_HEAVYCOUNTER | AIFLAG_RIPOSTE |
			AIFLAG_COUNTERATTACK | AIFLAG_FORCECOUNTER |
			AIFLAG_SPINCOUNTER);

		return JediBuildCombo(chosen, 3);
	}

	if (self->NPC->counterFlags & AIFLAG_COUNTERATTACK)
	{
		const saberMoveName_t* chosen =
			s_lightComboPool[Q_irand(0, ARRAY_LEN(s_lightComboPool) - 1)];

		self->NPC->counterFlags &= ~(AIFLAG_HEAVYCOUNTER | AIFLAG_RIPOSTE |
			AIFLAG_COUNTERATTACK | AIFLAG_FORCECOUNTER |
			AIFLAG_SPINCOUNTER);

		return JediBuildCombo(chosen, 3);
	}

	if (self->NPC->counterFlags & AIFLAG_FORCECOUNTER)
	{
		self->NPC->counterFlags &= ~(AIFLAG_HEAVYCOUNTER | AIFLAG_RIPOSTE |
			AIFLAG_COUNTERATTACK | AIFLAG_FORCECOUNTER |
			AIFLAG_SPINCOUNTER);

		combo.valid = qtrue;
		combo.length = 1;
		combo.moves[0] = LS_NONE;
		combo.useForcePushFinisher = qtrue;
		return combo;
	}

	if (self->NPC->counterFlags & AIFLAG_SPINCOUNTER)
	{
		self->NPC->counterFlags &= ~(AIFLAG_HEAVYCOUNTER | AIFLAG_RIPOSTE |
			AIFLAG_COUNTERATTACK | AIFLAG_FORCECOUNTER |
			AIFLAG_SPINCOUNTER);

		combo.valid = qtrue;
		combo.length = 1;
		combo.moves[0] = LS_SPINATTACK;
		return combo;
	}

	// =====================
	// Normal Combo Logic
	// =====================

	const int style = self->client->ps.fd.saberAnimLevel;

	// 70% chance signature combo, 30% chance pool combo
	const qboolean useSignature = (Q_irand(0, 9) < 7) ? qtrue : qfalse;

	const saberMoveName_t* chosen = NULL;
	int maxLen = 0;

	// -------------------------
	// Signature combos
	// -------------------------
	if (useSignature)
	{
		switch (style)
		{
		case SS_FAST:
		case SS_TAVION:
			chosen = s_fastSignatureCombos[Q_irand(0, ARRAY_LEN(s_fastSignatureCombos) - 1)];
			maxLen = 4;
			break;

		case SS_MEDIUM:
			chosen = s_mediumSignatureCombos[Q_irand(0, ARRAY_LEN(s_mediumSignatureCombos) - 1)];
			maxLen = 4;
			break;

		case SS_STRONG:
		case SS_DESANN:
		case SS_BLASTER: // SS_BLASTER (2026-10-14)
			chosen = s_strongSignatureCombos[Q_irand(0, ARRAY_LEN(s_strongSignatureCombos) - 1)];
			maxLen = 4;
			break;

		case SS_DUAL:
			chosen = s_dualSignatureCombos[Q_irand(0, ARRAY_LEN(s_dualSignatureCombos) - 1)];
			maxLen = 4;
			break;

		case SS_STAFF:
			chosen = s_staffSignatureCombos[Q_irand(0, ARRAY_LEN(s_staffSignatureCombos) - 1)];
			maxLen = 4;
			break;

		default:
			chosen = s_mediumSignatureCombos[Q_irand(0, ARRAY_LEN(s_mediumSignatureCombos) - 1)];
			maxLen = 4;
			break;
		}
	}
	else
	{
		// -------------------------
		// Pool combos
		// -------------------------

		// 20% chance heavy pool, 20% chance duelist counter pool, 60% light pool
		const int roll = Q_irand(0, 9);

		if (roll < 2)
		{
			chosen = s_heavyComboPool[Q_irand(0, ARRAY_LEN(s_heavyComboPool) - 1)];
			maxLen = 4;
		}
		else if (roll < 4)
		{
			chosen = s_duelistCounterPool[Q_irand(0, ARRAY_LEN(s_duelistCounterPool) - 1)];
			maxLen = 3;
		}
		else
		{
			chosen = s_lightComboPool[Q_irand(0, ARRAY_LEN(s_lightComboPool) - 1)];
			maxLen = 3;
		}
	}

	if (!chosen)
	{
		return combo;
	}

	// -------------------------
	// Copy moves into combo struct
	// -------------------------
	for (int i = 0; i < maxLen; i++)
	{
		combo.moves[i] = chosen[i];

		if (chosen[i] == LS_NONE)
		{
			maxLen = i;
			break;
		}
	}

	combo.length = maxLen;
	combo.valid = (maxLen > 0) ? qtrue : qfalse;

	if (!combo.valid)
	{
		return combo;
	}

	// -------------------------
	// Optional Force Push finisher
	// -------------------------
	if (combo.length >= 2 &&
		self->client->ps.fd.forcePowerLevel[FP_PUSH] >= FORCE_LEVEL_1 &&
		self->client->ps.fd.forcePower >= 10 &&
		!Q_irand(0, 5)) // ~1 in 6 combos get a Force Push finisher
	{
		combo.useForcePushFinisher = qtrue;
	}

	return combo;
}



// =====================
// Combo Execution Logic
// =====================

static void JediExecuteCombo(gentity_t* self, const saberCombo_t* combo)
{
	if (!self || !combo->valid || combo->length <= 0)
	{
		return;
	}

	// Execute each move in the combo
	for (int i = 0; i < combo->length; i++)
	{
		saberMoveName_t move = combo->moves[i];
		if (move == LS_NONE)
		{
			break;
		}

		self->client->ps.saberMove = move;
		JediPressAttack();
	}

	// Optional Force Push finisher
	if (combo->useForcePushFinisher)
	{
		if (self->client->ps.fd.forcePower >= 10 &&
			self->client->ps.fd.forcePowerLevel[FP_PUSH] >= FORCE_LEVEL_1)
		{
			ForceThrow(self, qfalse);
		}
	}

	// Apply cooldown so we don't spam combos
	JediSetAttackCooldown(self, Q_irand(40000, 90000));
}



// =====================
// Should the Jedi attempt a cinematic combo?
// =====================

static qboolean JediShouldAttack(gentity_t* self)
{
	if (!self || !self->enemy)
	{
		return qfalse;
	}

	// If too far away, move toward the enemy even in cinematic mode
	if (NPCS.NPC->enemy)
	{
		float dist = JediDistanceToEnemy(NPCS.NPC);

		if (dist > 192.0f)
		{
			// Move forward to close the gap
			NPCS.ucmd.forwardmove = 64;

			// Face the enemy
			Jedi_FaceEnemy(qtrue);
			NPC_UpdateAngles(qtrue, qtrue);
		}
	}

	// Do not override special saber states
	if (PM_SaberInBrokenParry(self->client->ps.saberMove) ||
		PM_SaberInSpecialAttack(self->client->ps.torsoAnim) ||
		PM_SpinningSaberAnim(self->client->ps.torsoAnim))
	{
		return qfalse;
	}

	// Must be on the ground
	if (self->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		return qfalse;
	}

	// Enemy must also be on the ground (prevents weird mid-air combos)
	if (self->enemy->client &&
		self->enemy->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		return qfalse;
	}

	// Must have line of sight and be facing the enemy
	if (!JediHasLineOfSight(self))
	{
		return qfalse;
	}

	// Preferred cinematic combo range
	// (tight enough to hit, far enough to not clip)
	if (!JediInAttackRange(self, 64.0f, 192.0f))
	{
		return qfalse;
	}

	// Cooldown prevents spam
	if (!JediAttackCooldownReady(self))
	{
		return qfalse;
	}

	// Optional: avoid combos if stunned, pushed, or recovering
	if (self->client->ps.weaponTime > 0)
	{
		return qfalse;
	}

	return qtrue;
}



static void Jedi_Attack(void)
{
	const int curmove = NPCS.NPC->client->ps.saberMove;

	// If in pain animation, do not attack
	if (NPCS.NPC->painDebounceTime > level.time)
	{
		if (Q_irand(0, 1))
		{
			Jedi_FaceEnemy(qtrue);
		}

		NPC_UpdateAngles(qtrue, qtrue);

		// Handle Kyle grab sequence
		if (NPCS.NPC->client->ps.torsoAnim == BOTH_KYLE_GRAB)
		{
			if (NPCS.NPC->client->ps.torsoTimer <= 200)
			{
				if (Kyle_CanDoGrab() &&
					NPC_EnemyRangeFromBolt(NPCS.NPC->client->renderInfo.handRBolt) < 88.0f)
				{
					// Perform grab
					if (g_npcspskill.integer >= 1)
					{
						NPC_GrabPlayer();
					}
					else
					{
						Kyle_GrabEnemy();
					}
					return;
				}

				// Missed grab
				NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_KYLE_MISS,
					SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				NPCS.NPC->client->ps.weaponTime = NPCS.NPC->client->ps.torsoTimer;
				return;
			}
		}
		return;
	}

	// Handle saber lock behavior
	if (NPCS.NPC->client->ps.saberLockTime > level.time)
	{
		// Rare chance to Force Push out of a losing lock
		if (NPCS.NPC->client->ps.fd.forcePowerLevel[FP_PUSH] > FORCE_LEVEL_2 &&
			NPCS.NPC->client->ps.saberLockTime < level.time + 5000 &&
			!Q_irand(0, 10))
		{
			if (!Jedi_Trainer(NPCS.NPC) || g_saberLockCinematicCamera.integer < 1)
			{
				ForceThrow(NPCS.NPC, qfalse);
			}
		}
		else
		{
			float chance = 0.0f;

			// Strong pushers
			if (NPCS.NPC->client->NPC_class == CLASS_DESANN ||
				NPCS.NPC->client->NPC_class == CLASS_SITHLORD ||
				NPCS.NPC->client->NPC_class == CLASS_VADER ||
				NPCS.NPC->client->NPC_class == CLASS_YODA ||
				!Q_stricmp("MD_Yoda", NPCS.NPC->NPC_type) ||
				!Q_stricmp("md_yoda_ep2", NPCS.NPC->NPC_type) ||
				!Q_stricmp("md_yod_mof", NPCS.NPC->NPC_type) ||
				!Q_stricmp("md_yoda_gd", NPCS.NPC->NPC_type) ||
				!Q_stricmp("Yoda", NPCS.NPC->NPC_type) ||
				!Q_stricmp("md_grogu", NPCS.NPC->NPC_type))
			{
				chance = (g_npcspskill.integer ? 4.0f : 3.0f);
			}
			// Mid-tier pushers
			else if (NPCS.NPC->client->NPC_class == CLASS_TAVION ||
				NPCS.NPC->client->NPC_class == CLASS_SHADOWTROOPER ||
				NPCS.NPC->client->NPC_class == CLASS_ALORA ||
				(NPCS.NPC->client->NPC_class == CLASS_KYLE && (NPCS.NPC->spawnflags & 1)))
			{
				chance = 2.0f + g_npcspskill.value;
			}
			else if (NPCS.NPC->client->NPC_class == CLASS_REBORN)
			{
				chance = 1.5f + g_npcspskill.value;
			}
			// Rank-based pushers
			else
			{
				const float max_chance = (float)(RANK_LT) / 2.0f + 3.0f; //5?
				if (!g_npcspskill.value)
				{
					chance = (float)(NPCS.NPCInfo->rank) / 2.0f;
				}
				else
				{
					chance = (float)(NPCS.NPCInfo->rank) / 2.0f + 1.0f;
				}
				if (chance > max_chance)
				{
					chance = max_chance;
				}
			}

			// Boss modifiers
			if (NPCS.NPCInfo->aiFlags & NPCAI_BOSS_CHARACTER)
			{
				chance += Q_irand(0, 2);
			}
			else if (NPCS.NPCInfo->aiFlags & NPCAI_SUBBOSS_CHARACTER)
			{
				chance += Q_irand(-1, 1);
			}

			// Attempt to push back in saber lock
			if (flrand(-4.0f, chance) >= 0.0f &&
				!(NPCS.NPC->client->ps.pm_flags & PMF_ATTACK_HELD))
			{
				NPCS.ucmd.buttons |= BUTTON_ATTACK;
			}
		}

		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	// If we dropped our saber, go after it
	if (NPCS.NPC->client->ps.saberInFlight)
	{
		// Saber is not in hand
		if (NPCS.NPC->client->ps.saberEntityNum > 0 &&
			NPCS.NPC->client->ps.saberEntityNum < ENTITYNUM_NONE) // player is 0
		{
			if (g_entities[NPCS.NPC->client->ps.saberEntityNum].s.pos.trType == TR_STATIONARY)
			{
				// Saber fell to the ground, try to pull it back
				if (Jedi_CanPullBackSaber(NPCS.NPC))
				{
					NPCS.NPC->client->ps.saberBlocked = BLOCKED_NONE;
					NPCS.NPCInfo->goalEntity = &g_entities[NPCS.NPC->client->ps.saberEntityNum];
					NPCS.ucmd.buttons |= BUTTON_ATTACK;

					if (NPCS.NPC->enemy && NPCS.NPC->enemy->health > 0)
					{
						// Get our saber back now
						Jedi_Move(NPCS.NPCInfo->goalEntity, qfalse);
						NPC_UpdateAngles(qtrue, qtrue);

						if (NPCS.NPC->enemy->s.weapon == WP_SABER)
						{
							// Continue evasion while retrieving saber
							vec3_t enemy_dir, enemy_movedir, enemy_dest;
							float enemy_dist = 0.0f, enemy_movespeed = 0.0f;

							jedi_set_enemy_info(
								enemy_dest,
								enemy_dir,
								&enemy_dist,
								enemy_movedir,
								&enemy_movespeed,
								300);

							Jedi_EvasionSaber(enemy_movedir, enemy_dist, enemy_dir);
						}
						return;
					}
				}
				else
				{
					if (NPCS.NPC->enemy && NPCS.NPC->enemy->health > 0)
					{
						if (NPCS.NPC->enemy->s.weapon == WP_SABER)
						{
							//be sure to continue evasion even if can't pull back saber yet
							vec3_t enemy_dir, enemy_movedir, enemy_dest;
							float enemy_dist = 0.0f, enemy_movespeed = 0.0f;
							jedi_set_enemy_info(enemy_dest, enemy_dir, &enemy_dist, enemy_movedir, &enemy_movespeed, 300);
							Jedi_EvasionSaber(enemy_movedir, enemy_dist, enemy_dir);
						}
					}
				}
			}
			if (NPCS.NPC->enemy && NPCS.NPC->enemy->health > 0)
			{
				if (NPCS.NPC->enemy->s.weapon == WP_SABER)
				{
					//be sure to continue evasion even if can't pull back saber yet
					vec3_t enemy_dir, enemy_movedir, enemy_dest;
					float enemy_dist = 0.0f, enemy_movespeed = 0.0f;
					jedi_set_enemy_info(enemy_dest, enemy_dir, &enemy_dist, enemy_movedir, &enemy_movespeed, 300);
					Jedi_EvasionSaber(enemy_movedir, enemy_dist, enemy_dir);
				}
			}
		}
	}

	if (NPCS.NPC->enemy)
	{
		// Enemy is dead, and we killed them
		if (NPCS.NPC->enemy->health <= 0 &&
			NPCS.NPC->enemy->enemy == NPCS.NPC &&
			(NPCS.NPC->client->playerTeam != NPCTEAM_PLAYER ||
				(NPCS.NPC->client->NPC_class == CLASS_KYLE || NPCS.NPC->client->NPC_class == CLASS_YODA &&
					(NPCS.NPC->spawnflags & 1) && (NPCS.NPC->enemy && NPCS.NPC->enemy->s.number < MAX_CLIENTS))))
		{
			// Keep looking for other enemies
			NPCS.NPCInfo->enemyCheckDebounceTime = 0;

			// Non-saber users that may gloat
			if (NPCS.NPC->client->NPC_class == CLASS_BOBAFETT ||
				NPCS.NPC->client->NPC_class == CLASS_MANDO ||
				(NPCS.NPC->client->NPC_class == CLASS_REBORN && NPCS.NPC->s.weapon != WP_SABER) ||
				NPCS.NPC->client->NPC_class == CLASS_ROCKETTROOPER)
			{
				if (NPCS.NPCInfo->walkDebounceTime < level.time && NPCS.NPCInfo->walkDebounceTime >= 0)
				{
					TIMER_Set(NPCS.NPC, "gloatTime", 10000);
					NPCS.NPCInfo->walkDebounceTime = -1;
				}

				if (!TIMER_Done(NPCS.NPC, "gloatTime"))
				{
					// Walk toward the fallen enemy if allowed to chase
					if (DistanceHorizontalSquared(NPCS.NPC->client->renderInfo.eyePoint,
						NPCS.NPC->enemy->r.currentOrigin) > 4096 &&
						(NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES)) // 64 squared
					{
						NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
						Jedi_Move(NPCS.NPC->enemy, qfalse);
						NPCS.ucmd.buttons |= BUTTON_WALKING;
					}
					else
					{
						TIMER_Set(NPCS.NPC, "gloatTime", 0);
					}
				}
				else if (NPCS.NPCInfo->walkDebounceTime == -1)
				{
					NPCS.NPCInfo->walkDebounceTime = -2;
					G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_VICTORY1, EV_VICTORY3), 3000);
					jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] = level.time + 3000;
					NPCS.NPCInfo->desiredPitch = 0;
					NPCS.NPCInfo->goalEntity = NULL;
				}

				Jedi_FaceEnemy(qtrue);
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}

			// Clear parry timer and saber block state
			if (!TIMER_Done(NPCS.NPC, "parryTime"))
			{
				TIMER_Set(NPCS.NPC, "parryTime", -1);
				NPCS.NPC->client->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE] = level.time + 500;
			}

			NPCS.NPC->client->ps.saberBlocked = BLOCKED_NONE;

			if ((!NPCS.NPC->client->ps.saberHolstered) || NPCS.NPC->client->ps.saberInFlight)
			{
				// Saber is still on (or in flight), erode aggression and keep facing enemy
				Jedi_AggressionErosion(-2);

				if (!(!NPCS.NPC->client->ps.saberHolstered) && !NPCS.NPC->client->ps.saberInFlight)
				{
					// Saber turned off in hand, gloat
					G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_VICTORY1, EV_VICTORY3), 6000);
					jediSpeechDebounceTime[NPCS.NPC->client->playerTeam] = level.time + 6000;
					NPCS.NPCInfo->desiredPitch = 0;
					NPCS.NPCInfo->goalEntity = NULL;
				}

				TIMER_Set(NPCS.NPC, "gloatTime", 10000);
			}

			if ((!NPCS.NPC->client->ps.saberHolstered) ||
				NPCS.NPC->client->ps.saberInFlight ||
				!TIMER_Done(NPCS.NPC, "gloatTime"))
			{
				// Keep walking toward the fallen enemy if allowed to chase
				if (DistanceHorizontalSquared(NPCS.NPC->client->renderInfo.eyePoint,
					NPCS.NPC->enemy->r.currentOrigin) > 4096 &&
					(NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES)) // 64 squared
				{
					NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
					Jedi_Move(NPCS.NPC->enemy, qfalse);
					NPCS.ucmd.buttons |= BUTTON_WALKING;
				}
				else
				{
					// Reached the body; optionally heal or recharge
					if (NPCS.NPC->health < NPCS.NPC->client->pers.maxHealth)
					{
						if (NPCS.NPC->client->saber[0].type == SABER_SITH_SWORD &&
							NPCS.NPC->client->weaponGhoul2[0] != NULL)
						{
							Tavion_SithSwordRecharge();
						}
						else if ((NPCS.NPC->client->ps.fd.forcePowersKnown & (1 << FP_HEAL)) &&
							!(NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_HEAL)))
						{
							ForceHeal(NPCS.NPC);

							if (NPC_IsJedi(NPCS.NPC))
							{
								// Do deflect taunt...
								G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_GLOAT1, EV_GLOAT3), 5000 + Q_irand(0, 15000));
							}
						}
					}
				}

				Jedi_FaceEnemy(qtrue);
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
		}
	}

	// If we do not have a valid enemy, handle special cases and idle
	if (NPCS.NPC->enemy &&
		NPCS.NPC->enemy->s.weapon == WP_TURRET &&
		!Q_stricmp("PAS", NPCS.NPC->enemy->classname))
	{
		if (NPCS.NPC->enemy->count <= 0)
		{
			// Turret is out of ammo
			if (NPCS.NPC->enemy->activator && NPC_ValidEnemy(NPCS.NPC->enemy->activator))
			{
				gentity_t* turretOwner = NPCS.NPC->enemy->activator;
				G_ClearEnemy(NPCS.NPC);
				G_SetEnemy(NPCS.NPC, turretOwner);
			}
			else
			{
				G_ClearEnemy(NPCS.NPC);
			}
		}
	}
	else if (NPCS.NPC->enemy &&
		NPCS.NPC->enemy->NPC &&
		(NPCS.NPC->enemy->NPC->charmedTime > level.time))
	{
		// Enemy was charmed
		if (OnSameTeam(NPCS.NPC, NPCS.NPC->enemy))
		{
			// Now on our team
			G_ClearEnemy(NPCS.NPC);
		}
	}

	if (NPCS.NPC->client->playerTeam == NPCTEAM_ENEMY &&
		NPCS.NPC->client->enemyTeam == NPCTEAM_PLAYER &&
		NPCS.NPC->enemy &&
		NPCS.NPC->enemy->client &&
		NPCS.NPC->enemy->client->playerTeam != NPCS.NPC->client->enemyTeam &&
		OnSameTeam(NPCS.NPC, NPCS.NPC->enemy) &&
		!(NPCS.NPC->r.svFlags & SVF_LOCKEDENEMY))
	{
		// An evil Jedi somehow got another evil NPCS.NPC as an enemy; likely charm expired
		if (!NPC_ValidEnemy(NPCS.NPC->enemy))
		{
			G_ClearEnemy(NPCS.NPC);
		}
	}

	NPC_CheckEnemy(qtrue, qtrue, qtrue);

	if (!NPCS.NPC->enemy)
	{
		NPCS.NPC->client->ps.saberBlocked = BLOCKED_NONE;

		if (NPCS.NPCInfo->tempBehavior == BS_HUNT_AND_KILL)
		{
			// Lost the target, revert to default behavior
			NPCS.NPCInfo->tempBehavior = BS_DEFAULT;
			NPC_UpdateAngles(qtrue, qtrue);
			return;
		}

		Jedi_Patrol(); // Previously called Idle
		return;
	}

	// Always face enemy if we have one
	NPCS.NPCInfo->combatMove = qtrue;

	// Track the enemy and attempt to kill them
	Jedi_Combat();

	if (!(NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES) ||
		((NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_HEAL)) &&
			NPCS.NPC->client->ps.fd.forcePowerLevel[FP_HEAL] < FORCE_LEVEL_2))
	{
		// Do not move while healing or when not allowed to chase
		NPCS.ucmd.forwardmove = 0;
		NPCS.ucmd.rightmove = 0;

		if (NPCS.ucmd.upmove > 0)
		{
			NPCS.ucmd.upmove = 0;
		}

		NPCS.NPC->client->ps.fd.forceJumpCharge = 0;
		VectorClear(NPCS.NPC->client->ps.moveDir);
	}

	// While in the air, clear movement to avoid jump issues
	if (NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		NPCS.ucmd.forwardmove = 0;
		NPCS.ucmd.rightmove = 0;
		VectorClear(NPCS.NPC->client->ps.moveDir);
	}

	if (!TIMER_Done(NPCS.NPC, "duck"))
	{
		NPCS.ucmd.upmove = -127;
	}

	if ((NPCS.NPCInfo->aiFlags & NPCAI_BOSS_CHARACTER || NPCS.NPCInfo->aiFlags & NPCAI_SUBBOSS_CHARACTER) ||
		NPCS.NPC->client->NPC_class != CLASS_BOBAFETT &&
		NPCS.NPC->client->NPC_class != CLASS_MANDO &&
		(NPCS.NPC->client->NPC_class != CLASS_REBORN || NPCS.NPC->s.weapon == WP_SABER) &&
		(NPCS.NPC->client->NPC_class != CLASS_SITHLORD || NPCS.NPC->s.weapon == WP_SABER) &&
		NPCS.NPC->client->NPC_class != CLASS_ROCKETTROOPER)
	{
		if (PM_SaberInBrokenParry(NPCS.NPC->client->ps.saberMove) ||
			NPCS.NPC->client->ps.saberBlocked == BLOCKED_PARRY_BROKEN)
		{
			// Do not pull saber while being blocked
			NPCS.ucmd.buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_FORCEPOWER | BUTTON_KICK | BUTTON_BLOCK);
		}
	}

	if ((NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE) || // Not allowed to attack
		(((NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_HEAL)) &&
			NPCS.NPC->client->ps.fd.forcePowerLevel[FP_HEAL] < FORCE_LEVEL_3)) ||
		((NPCS.NPC->client->ps.saberEventFlags & SEF_INWATER) &&
			!NPCS.NPC->client->ps.saberInFlight)) // Saber in water
	{
		NPCS.ucmd.buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK | BUTTON_FORCEPOWER);
	}

	if (NPCS.NPCInfo->scriptFlags & SCF_NO_ACROBATICS)
	{
		NPCS.ucmd.upmove = 0;
		NPCS.NPC->client->ps.fd.forceJumpCharge = 0;
	}

	if (NPCS.NPC->client->NPC_class != CLASS_BOBAFETT &&
		NPCS.NPC->client->NPC_class != CLASS_MANDO &&
		(NPCS.NPC->client->NPC_class != CLASS_REBORN || NPCS.NPC->s.weapon == WP_SABER) &&
		(NPCS.NPC->client->NPC_class != CLASS_SITHLORD || NPCS.NPC->s.weapon == WP_SABER) &&
		NPCS.NPC->client->NPC_class != CLASS_ROCKETTROOPER)
	{
		Jedi_CheckDecreasesaber_anim_level();
	}    // Enemy attack vocalization (anger noises)
	if ((NPCS.ucmd.buttons & BUTTON_ATTACK) && NPCS.NPC->client->playerTeam == NPCTEAM_ENEMY)
	{
		if (Q_irand(0, NPCS.NPC->client->ps.fd.saberAnimLevel) > 0 &&
			Q_irand(0, NPCS.NPC->client->pers.maxHealth + 10) > NPCS.NPC->health &&
			!Q_irand(0, 3))
		{
			// More hurt + stronger attack = more likely to play anger sound
			G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_COMBAT1, EV_COMBAT3), 10000);
		}
	}

	// Kata attacks override everything else
	if (Jedi_CheckKataAttack())
	{
		// Doing a kata attack
	}
	else
	{
		// Special combat behavior for saber-using NPCs
		if (NPCS.NPC->client->NPC_class != CLASS_BOBAFETT &&
			NPCS.NPC->client->NPC_class != CLASS_MANDO &&
			(NPCS.NPC->client->NPC_class != CLASS_REBORN || NPCS.NPC->s.weapon == WP_SABER) &&
			(NPCS.NPC->client->NPC_class != CLASS_SITHLORD || NPCS.NPC->s.weapon == WP_SABER) &&
			NPCS.NPC->client->NPC_class != CLASS_ROCKETTROOPER)
		{
			// High-tier NPCs may activate Force Speed if the player does
			if (NPCS.NPC->client->NPC_class == CLASS_TAVION ||
				NPCS.NPC->client->NPC_class == CLASS_YODA ||
				NPCS.NPC->client->NPC_class == CLASS_SHADOWTROOPER ||
				NPCS.NPC->client->NPC_class == CLASS_ALORA ||
				(g_npcspskill.integer &&
					(NPCS.NPC->client->NPC_class == CLASS_DESANN ||
						NPCS.NPC->client->NPC_class == CLASS_SITHLORD ||
						NPCS.NPC->client->NPC_class == CLASS_VADER ||
						NPCS.NPCInfo->rank >= Q_irand(RANK_CREWMAN, RANK_CAPTAIN))))
			{
				if (NPCS.NPC->enemy &&
					NPCS.NPC->enemy->s.number < MAX_CLIENTS &&
					NPCS.NPC->enemy->client &&
					(NPCS.NPC->enemy->client->ps.fd.forcePowersActive & (1 << FP_SPEED)) &&
					!(NPCS.NPC->client->ps.fd.forcePowersActive & (1 << FP_SPEED)))
				{
					int chance = 0;
					switch (g_npcspskill.integer)
					{
					case 0:
						chance = 9;
						break;
					case 1:
						chance = 3;
						break;
					case 2:
						chance = 1;
						break;
					default:
						break;
					}

					if (!Q_irand(0, chance))
					{
						ForceSpeed(NPCS.NPC, 0);
					}
				}
			}
		}

		// Alora special movement and attacks
		if (NPCS.NPC->client->NPC_class == CLASS_ALORA && !npc_is_sith_lord(NPCS.NPC))
		{
			// Special dual saber throw
			if (NPCS.ucmd.buttons & BUTTON_ALT_ATTACK)
			{
				if (NPCS.NPC->client->ps.fd.saberAnimLevel == SS_DUAL &&
					!NPCS.NPC->client->ps.saberInFlight)
				{
					if (Distance(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) >= 120)
					{
						NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_ALORA_SPIN_THROW, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						NPCS.NPC->client->ps.weaponTime = NPCS.NPC->client->ps.torsoTimer;
					}
				}
			}
			// Alora flip-toward-player behavior
			else if (NPCS.NPC->enemy
				&& NPCS.ucmd.forwardmove > 0
				&& fabs((float)(NPCS.ucmd.rightmove)) < 32
				&& !(NPCS.ucmd.buttons & BUTTON_WALKING)
				&& !(NPCS.ucmd.buttons & BUTTON_ATTACK)
				&& NPCS.NPC->client->ps.saberMove == LS_READY
				&& NPCS.NPC->client->ps.legsAnim == BOTH_RUN_DUAL)
			{
				if (Distance(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) > 80)
				{
					if (NPCS.NPC->client->ps.legsAnim == BOTH_FLIP_F
						|| NPCS.NPC->client->ps.legsAnim == BOTH_ALORA_FLIP_1_MD2
						|| NPCS.NPC->client->ps.legsAnim == BOTH_ALORA_FLIP_2_MD2
						|| NPCS.NPC->client->ps.legsAnim == BOTH_ALORA_FLIP_3_MD2
						|| NPCS.NPC->client->ps.legsAnim == BOTH_ALORA_FLIP_1
						|| NPCS.NPC->client->ps.legsAnim == BOTH_ALORA_FLIP_2
						|| NPCS.NPC->client->ps.legsAnim == BOTH_ALORA_FLIP_3)
					{
						if (NPCS.NPC->client->ps.legsTimer <= 200 && Q_irand(0, 2))
						{
							NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, Q_irand(BOTH_ALORA_FLIP_1, BOTH_ALORA_FLIP_3), SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
						}
					}
					else if (!Q_irand(0, 6))
					{
						NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, Q_irand(BOTH_ALORA_FLIP_1, BOTH_ALORA_FLIP_3), SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					}
				}
			}
		}
	}

	/* -------------------------------------------------------------------------
	   Main cleaned interaction logic
	   - Call this where your original block ran.
	   - Preserves original animations, voice events and timing.
	   ------------------------------------------------------------------------- */
	if (NPCS.NPC && NPCS.NPC->enemy && NPCS.NPC->enemy->s.number < MAX_CLIENTS)
	{
		gentity_t* enemy = NPCS.NPC->enemy;

		// quick exclusion by class
		if (NPC_IsExcludedForGestures(NPCS.NPC))
		{
			// excluded classes do nothing here
		}
		else if (NPC_CanReactToEnemy(NPCS.NPC, enemy) && IsSurrendering(enemy))
		{
			// Enemy is surrendering: gloat and sheathe saber if needed
			NPC_PlayGloatAndMaybeSheathe(NPCS.NPC);
			return;
		}
		else if (NPC_CanReactToEnemy(NPCS.NPC, enemy) && IsCowering(enemy))
		{
			// Enemy is cowering: gloat and sheathe saber if needed
			NPC_PlayGloatAndMaybeSheathe(NPCS.NPC);
			return;
		}
		else if (NPC_CanReactToEnemy(NPCS.NPC, enemy) && (enemy->client->ps.communicatingflags & (1 << CF_RESPECTING)))
		{
			// Enemy is showing respect: NPCS.NPC bows or handsignal and may speak
			if (NPCS.NPC->s.weapon == WP_SABER)
			{
				WP_DeactivateSaber(NPCS.NPC);
				NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, BOTH_BOW, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

				NPC_HandleSpeechDebounceAndIncrement(NPCS.NPC);
			}
			else
			{
				NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, TORSO_HANDSIGNAL3, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

				NPC_HandleSpeechDebounceAndIncrement(NPCS.NPC);
			}
		}
		else if (NPC_CanReactToEnemy(NPCS.NPC, enemy) && (enemy->client->ps.communicatingflags & (1 << CF_GESTURING)))
		{
			// Enemy gestured: return gesture or taunt depending on saber and saber level
			if (NPCS.NPC->s.weapon == WP_SABER)
			{
				switch (NPCS.NPC->client->ps.fd.saberAnimLevel)
				{
				case SS_FAST:
				case SS_TAVION:
				case SS_MEDIUM:
				case SS_STRONG:
				case SS_DESANN:
				case SS_BLASTER: // SS_BLASTER (2026-10-14)
					NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, BOTH_ENGAGETAUNT, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					break;
				case SS_DUAL:
					WP_ActivateSaber(NPCS.NPC);
					NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, BOTH_DUAL_TAUNT, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					break;
				case SS_STAFF:
					WP_ActivateSaber(NPCS.NPC);
					NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, BOTH_STAFF_TAUNT, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					break;
				default:
					break;
				}

				NPC_HandleSpeechDebounceAndIncrement(NPCS.NPC);
			}
			else
			{
				NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, TORSO_HANDSIGNAL3, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
				NPC_HandleSpeechDebounceAndIncrement(NPCS.NPC);
			}
		}
	}

	// =====================
	// Dynamic Cinematic/Raven Switching
	// =====================
	if (!NPCS.NPC || !NPCS.NPC->client)
	{
		return;
	}

	// Conditions that *allow* cinematic mode
	qboolean cinematicAllowed = qfalse;

	if (g_npcspskill.integer >= 1)
	{
		// 1. Low health increases chance of cinematic mode
		if (NPCS.NPC->health < (NPCS.NPC->client->pers.maxHealth * 0.35f))
		{
			if (!Q_irand(0, 2)) // ~33% chance
			{
				cinematicAllowed = qtrue;
			}
		}

		// 2. Low blockpoints increases chance of cinematic mode
		if (NPCS.NPC->client->ps.fd.blockPoints < 30)
		{
			if (!Q_irand(0, 1)) // ~50% chance
			{
				cinematicAllowed = qtrue;
			}
		}

		// =====================
		// Cinematic Mode
		// =====================
		if (cinematicAllowed && JediShouldAttack(NPCS.NPC))
		{
			JediHandleSpacing(NPCS.NPC);
			JediHandleCounterattacks(NPCS.NPC);

			saberCombo_t combo = JediChooseCombo(NPCS.NPC);

			if (combo.valid)
			{
				JediExecuteCombo(NPCS.NPC, &combo);

				// IMPORTANT:
			// Do NOT return here.
			// We want Raven movement/pathing to continue.
			}
		}
	}

	// =====================
	// Otherwise: Raven fallback logic continues below
	// =====================

	// If NAV is not moving us but ucmds are, set movement speed manually
	if (VectorCompare(NPCS.NPC->client->ps.moveDir, vec3_origin) &&
		(NPCS.ucmd.forwardmove || NPCS.ucmd.rightmove))
	{
		if (NPCS.ucmd.buttons & BUTTON_WALKING)
		{
			NPCS.NPC->client->ps.speed = NPCS.NPCInfo->stats.walkSpeed;
		}
		else
		{
			NPCS.NPC->client->ps.speed = NPCS.NPCInfo->stats.runSpeed;
		}
	}

	if (PM_SaberInStart(curmove) || PM_SaberInTransition(curmove))
	{
		NPCS.ucmd.buttons |= BUTTON_ATTACK;
	}
}



//added all SP code used for the Rosh boss fight
qboolean Rosh_BeingHealed(const gentity_t* self)
{
	if (self
		&& self->NPC
		&& self->client
		&& self->NPC->aiFlags & NPCAI_ROSH
		&& self->flags & FL_UNDYING
		&& (self->health == 1 //need healing
			|| self->client->ps.powerups[PW_INVINCIBLE] > level.time)) //being healed
	{
		return qtrue;
	}
	return qfalse;
}



static qboolean Rosh_TwinPresent(void)
{
	const gentity_t* foundTwin = G_Find(NULL, FOFS(NPC_type), "DKothos");
	if (!foundTwin
		|| foundTwin->health < 0)
	{
		foundTwin = G_Find(NULL, FOFS(NPC_type), "VKothos");
	}
	if (!foundTwin
		|| foundTwin->health < 0)
	{
		//oh well, both twins are dead...
		return qfalse;
	}
	return qtrue;
}



static qboolean Rosh_TwinNearBy(const gentity_t* self)
{
	const gentity_t* foundTwin = G_Find(NULL, FOFS(NPC_type), "DKothos");
	if (!foundTwin
		|| foundTwin->health < 0)
	{
		foundTwin = G_Find(NULL, FOFS(NPC_type), "VKothos");
	}
	if (!foundTwin
		|| foundTwin->health < 0)
	{
		//oh well, both twins are dead...
		return qfalse;
	}
	if (self->client
		&& foundTwin->client)
	{
		if (Distance(self->r.currentOrigin, foundTwin->r.currentOrigin) <= 512.0f
			&& G_ClearLineOfSight(self->client->renderInfo.eyePoint, foundTwin->client->renderInfo.eyePoint,
				foundTwin->s.number, MASK_OPAQUE))
		{
			//make them look charge me for a bit while I do this
			TIMER_Set(self, "chargeMeUp", Q_irand(2000, 4000));
			return qtrue;
		}
	}
	return qfalse;
}


qboolean Kothos_HealRosh(void)
{
	//this NPC attempts to heal the heal target (the rosh boss normally).
	if (NPCS.NPC->client
		&& NPCS.NPC->client->leader
		&& NPCS.NPC->client->leader->client)
	{
		if (DistanceSquared(NPCS.NPC->client->leader->r.currentOrigin, NPCS.NPC->r.currentOrigin) <= 256 * 256
			&& G_ClearLineOfSight(NPCS.NPC->client->leader->client->renderInfo.eyePoint,
				NPCS.NPC->client->renderInfo.eyePoint, NPCS.NPC->s.number, MASK_OPAQUE))
		{
			NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, BOTH_FORCE_2HANDEDLIGHTNING_HOLD,
				SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			NPCS.NPC->client->ps.torsoTimer = 1000;

			if (NPCS.NPC->ghoul2)
			{
				mdxaBone_t boltMatrix;
				vec3_t fxOrg, fx_dir, angles;

				VectorSet(angles, 0, NPCS.NPC->r.currentAngles[YAW], 0);

				trap->G2API_GetBoltMatrix(NPCS.NPC->ghoul2, 0,
					Q_irand(0, 1),
					&boltMatrix, angles, NPCS.NPC->r.currentOrigin, level.time,
					NULL, NPCS.NPC->modelScale);
				BG_GiveMeVectorFromMatrix(&boltMatrix, ORIGIN, fxOrg);
				VectorSubtract(NPCS.NPC->client->leader->r.currentOrigin, fxOrg, fx_dir);
				VectorNormalize(fx_dir);
				G_PlayEffect(G_EffectIndex("force/kothos_beam.efx"), fxOrg, fx_dir);
			}

			NPCS.NPC->client->leader->health += Q_irand(1 + g_npcspskill.integer * 2, 4 + g_npcspskill.integer * 3);
			//from 1-5 to 4-10
			if (NPCS.NPC->client->leader->client)
			{
				if (NPCS.NPC->client->leader->client->ps.legsAnim == BOTH_FORCEHEAL_START
					&& NPCS.NPC->client->leader->health >= NPCS.NPC->client->leader->client->pers.maxHealth)
				{
					//let him get up now
					NPC_SetAnim(NPCS.NPC->client->leader, SETANIM_BOTH, BOTH_FORCEHEAL_STOP,
						SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
					NPCS.NPC->client->leader->flags &= ~FL_GODMODE;
					NPCS.NPC->client->leader->client->ps.powerups[PW_INVINCIBLE] = level.time + NPCS.NPC->client->leader
						->client->ps.torsoTimer;
					NPCS.NPC->client->leader->NPC->ignorePain = qfalse;
					NPCS.NPC->client->leader->health = NPCS.NPC->client->leader->client->pers.maxHealth;
				}
				else
				{
					//start effect?
					NPCS.NPC->client->leader->flags |= FL_GODMODE;
					NPCS.NPC->client->leader->client->ps.powerups[PW_INVINCIBLE] = level.time + 500;
				}
			}
			//decrement
			NPCS.NPC->count--;
			if (!NPCS.NPC->count)
			{
				TIMER_Set(NPCS.NPC, "healRoshDebounce", Q_irand(5000, 10000));
				NPCS.NPC->count = 100;
			}
			//now protect me, too
			if (g_npcspskill.integer)
			{
				//not on easy
				NPCS.NPC->client->leader->flags |= FL_GODMODE;
				NPCS.NPC->client->ps.powerups[PW_INVINCIBLE] = level.time + 500;
			}
			return qtrue;
		}
	}
	return qfalse;
}



void Kothos_PowerRosh(void)
{
	if (NPCS.NPC->client
		&& NPCS.NPC->client->leader)
	{
		if (Distance(NPCS.NPC->client->leader->r.currentOrigin, NPCS.NPC->r.currentOrigin) <= 512.0f
			&& G_ClearLineOfSight(NPCS.NPC->client->leader->client->renderInfo.eyePoint,
				NPCS.NPC->client->renderInfo.eyePoint, NPCS.NPC->s.number, MASK_OPAQUE))
		{
			NPC_FaceEntity(NPCS.NPC->client->leader, qtrue);
			NPC_SetAnim(NPCS.NPC, SETANIM_TORSO, BOTH_FORCELIGHTNING_HOLD, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			NPCS.NPC->client->ps.torsoTimer = 500;
			if (NPCS.NPC->client->leader->client)
			{
				//hmm, give him some force?
				NPCS.NPC->client->leader->client->ps.fd.forcePower++;
			}
		}
	}
}



static qboolean Kothos_Retreat(void)
{
	if (NPCS.NPCInfo->aiFlags & NPCAI_BLOCKED)
	{
		if (level.time - NPCS.NPCInfo->blockedDebounceTime > 1000)
		{
			return qfalse;
		}
	}
	return qtrue;
}



static float Twins_DangerDist(void)
{
	switch (g_npcspskill.integer)
	{
	case 0:
		return TWINS_DANGER_DIST_EASY;
	case 1:
		return TWINS_DANGER_DIST_MEDIUM;
	case 2:
	default:
		return TWINS_DANGER_DIST_HARD;
	}
}


static qboolean Jedi_InSpecialMove(void)
{
	if (NPCS.NPC->client->ps.torsoAnim == BOTH_KYLE_PA_1
		|| NPCS.NPC->client->ps.torsoAnim == BOTH_KYLE_PA_2
		|| NPCS.NPC->client->ps.torsoAnim == BOTH_KYLE_PA_3
		|| NPCS.NPC->client->ps.torsoAnim == BOTH_PLAYER_PA_1
		|| NPCS.NPC->client->ps.torsoAnim == BOTH_PLAYER_PA_2
		|| NPCS.NPC->client->ps.torsoAnim == BOTH_PLAYER_PA_3
		|| NPCS.NPC->client->ps.torsoAnim == BOTH_FORCE_DRAIN_GRAB_END
		|| NPCS.NPC->client->ps.torsoAnim == BOTH_FORCE_DRAIN_GRABBED)
	{
		NPC_UpdateAngles(qtrue, qtrue);
		return qtrue;
	}

	if (Jedi_InNoAIAnim(NPCS.NPC))
	{
		//in special anims, don't do force powers or attacks, just face the enemy
		if (NPCS.NPC->enemy)
		{
			NPC_FaceEnemy(qtrue);
		}
		else
		{
			NPC_UpdateAngles(qtrue, qtrue);
		}
		return qtrue;
	}

	if (NPCS.NPC->client->ps.torsoAnim == BOTH_FORCE_DRAIN_GRAB_START
		|| NPCS.NPC->client->ps.torsoAnim == BOTH_FORCE_DRAIN_GRAB_HOLD)
	{
		if (!TIMER_Done(NPCS.NPC, "draining"))
		{
			//FIXME: what do we do if we ran out of power?  NPC's can't?
			//FIXME: don't keep turning to face enemy or we'll end up spinning around
			NPCS.ucmd.buttons |= BUTTON_FORCE_DRAIN;
		}
		NPC_UpdateAngles(qtrue, qtrue);
		return qtrue;
	}

	if (NPCS.NPC->client->ps.torsoAnim == BOTH_TAVION_SWORDPOWER)
	{
		NPCS.NPC->health += Q_irand(1, 2);
		if (NPCS.NPC->health > NPCS.NPC->client->ps.stats[STAT_MAX_HEALTH])
		{
			NPCS.NPC->health = NPCS.NPC->client->ps.stats[STAT_MAX_HEALTH];
		}
		NPC_UpdateAngles(qtrue, qtrue);
		return qtrue;
	}

	if (NPCS.NPC->client->ps.torsoAnim == BOTH_SCEPTER_START)
	{
		//going into the scepter laser attack
		if (NPCS.NPC->client->ps.torsoTimer <= 100)
		{
			//go into the hold
			NPCS.NPC->s.loopSound = G_SoundIndex("sound/weapons/scepter/loop.wav");
			//G_StopEffect(G_EffectIndex("scepter/beam.efx"), NPCS.NPC->weaponModel[1], NPCS.NPC->genericBolt1, NPCS.NPC->s.number);
			NPCS.NPC->client->ps.legsTimer = NPCS.NPC->client->ps.torsoTimer = 0;
			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_SCEPTER_HOLD, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			NPCS.NPC->client->ps.torsoTimer += 200;
			NPCS.NPC->painDebounceTime = level.time + NPCS.NPC->client->ps.torsoTimer;
			NPCS.NPC->client->ps.pm_time = NPCS.NPC->client->ps.torsoTimer;
			NPCS.NPC->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
			VectorClear(NPCS.NPC->client->ps.velocity);
			VectorClear(NPCS.NPC->client->ps.moveDir);
		}
		if (NPCS.NPC->enemy)
		{
			NPC_FaceEnemy(qtrue);
		}
		else
		{
			NPC_UpdateAngles(qtrue, qtrue);
		}
		return qtrue;
	}
	if (NPCS.NPC->client->ps.torsoAnim == BOTH_SCEPTER_HOLD)
	{
		//in the scepter laser attack
		if (NPCS.NPC->client->ps.torsoTimer <= 100)
		{
			NPCS.NPC->s.loopSound = 0;
			//G_StopEffect(G_EffectIndex("scepter/beam.efx"), NPCS.NPC->weaponModel[1], NPCS.NPC->genericBolt1, NPC->s.number);
			NPCS.NPC->client->ps.legsTimer = NPCS.NPC->client->ps.torsoTimer = 0;
			NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_SCEPTER_STOP, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
			NPCS.NPC->painDebounceTime = level.time + NPCS.NPC->client->ps.torsoTimer;
			NPCS.NPC->client->ps.pm_time = NPCS.NPC->client->ps.torsoTimer;
			NPCS.NPC->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
			VectorClear(NPCS.NPC->client->ps.velocity);
			VectorClear(NPCS.NPC->client->ps.moveDir);
		}
		else
		{
			Tavion_ScepterDamage();
		}
		if (NPCS.NPC->enemy)
		{
			NPC_FaceEnemy(qtrue);
		}
		else
		{
			NPC_UpdateAngles(qtrue, qtrue);
		}
		return qtrue;
	}
	if (NPCS.NPC->client->ps.torsoAnim == BOTH_SCEPTER_STOP)
	{
		//coming out of the scepter laser attack
		if (NPCS.NPC->enemy)
		{
			NPC_FaceEnemy(qtrue);
		}
		else
		{
			NPC_UpdateAngles(qtrue, qtrue);
		}
		return qtrue;
	}
	if (NPCS.NPC->client->ps.torsoAnim == BOTH_TAVION_SCEPTERGROUND)
	{
		//specter slamming
		if (NPCS.NPC->client->ps.torsoTimer <= 1200
			&& !NPCS.NPC->count)
		{
			Tavion_ScepterSlam();
			NPCS.NPC->count = 1;
		}
		NPC_UpdateAngles(qtrue, qtrue);
		return qtrue;
	}

	if (Jedi_CultistDestroyer(NPCS.NPC))
	{
		if (!NPCS.NPC->takedamage)
		{
			//ready to explode
			if (NPCS.NPC->useDebounceTime <= level.time)
			{
				//this should damage everyone - FIXME: except other destroyers?
				NPCS.NPC->client->playerTeam = NPCTEAM_FREE; //FIXME: will this destroy wampas, tusken & rancors?
				NPCS.NPC->splashDamage = 200; // rough match to SP
				NPCS.NPC->splashRadius = 512; // see above
				WP_Explode(NPCS.NPC);
				return qtrue;
			}
			if (NPCS.NPC->enemy)
			{
				NPC_FaceEnemy(qfalse);
			}
			return qtrue;
		}
	}

	if (NPCS.NPC->client->NPC_class == CLASS_REBORN)
	{
		//reborn!
		if (NPCS.NPCInfo->aiFlags & NPCAI_HEAL_ROSH)
		{
			//NPC heals Rosh
			if (!NPCS.NPC->client->leader)
			{
				//find Rosh
				NPCS.NPC->client->leader = G_Find(NULL, FOFS(NPC_type), "rosh_dark");
			}
			if (NPCS.NPC->client->leader)
			{
				//have our heal target
				qboolean helpingRosh = qfalse;
				NPCS.NPC->flags |= FL_LOCK_PLAYER_WEAPONS;
				NPCS.NPC->client->leader->flags |= FL_UNDYING;

				if (NPCS.NPC->client->leader->client)
				{
					NPCS.NPC->client->leader->client->ps.fd.forcePowersKnown |= FORCE_POWERS_ROSH_FROM_TWINS;
				}
				if (NPCS.NPC->client->leader->client
					&& NPCS.NPC->client->leader->client->ps.legsAnim == BOTH_FORCEHEAL_START
					&& TIMER_Done(NPCS.NPC, "healRoshDebounce"))
				{
					//our heal target is needing a heal
					if (Kothos_HealRosh())
					{
						helpingRosh = qtrue;
					}
					else
					{
						//not able to heal, probably not close enough, try to follow him to
						//get closer
						NPC_BSJedi_FollowLeader();
						NPC_UpdateAngles(qtrue, qtrue);
						return qtrue;
					}
				}

				if (helpingRosh)
				{
					//if we're healing Rosh, quit using force powers and face him.
					WP_ForcePowerStop(NPCS.NPC, FP_LIGHTNING);
					WP_ForcePowerStop(NPCS.NPC, FP_DRAIN);
					WP_ForcePowerStop(NPCS.NPC, FP_GRIP);
					NPC_FaceEntity(NPCS.NPC->client->leader, qtrue);
					return qtrue;
				}
				if (NPCS.NPC->enemy && DistanceSquared(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin) <
					Twins_DangerDist())
				{
					//enemies to attack and close enough to our heal target.
					if (NPCS.NPC->enemy && Kothos_Retreat())
					{
						//not blocked in movement
						NPC_FaceEnemy(qtrue);
						if (TIMER_Done(NPCS.NPC, "attackDelay"))
						{
							//attack!
							if (NPCS.NPC->painDebounceTime > level.time //just get hurt
								|| NPCS.NPC->health < 100 && Q_irand(-20, (g_npcspskill.integer + 1) * 10) > 0
								//injured
								|| !Q_irand(0, 80 - g_npcspskill.integer * 20)) //just randomly
							{
								switch (Q_irand(0, 7 + g_npcspskill.integer)) //on easy: no lightning
								{
								case 0:
								case 1:
								case 2:
								case 3:
									//Push the bastard.
									ForceThrow(NPCS.NPC, qfalse);
									NPCS.NPC->client->ps.weaponTime = Q_irand(1000, 3000) + (2 - g_npcspskill.integer) *
										1000;
									if (NPCS.NPC->painDebounceTime <= level.time
										&& NPCS.NPC->health >= 100)
									{
										//have an attack delay if this attack isn't the result
										//of injury
										TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.weaponTime);
									}
									break;
								case 4:
								case 5:
									NPCS.NPC->client->ps.weaponTime = Q_irand(3000, 6000) + (2 - g_npcspskill.integer) *
										2000;
									TIMER_Set(NPCS.NPC, "draining", NPCS.NPC->client->ps.weaponTime);
									if (NPCS.NPC->painDebounceTime <= level.time
										&& NPCS.NPC->health >= 100)
									{
										//have an attack delay if this attack isn't the result
										//of injury
										TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.weaponTime);
									}
									break;
								case 6:
								case 7:
									//try to grip them
									if (NPCS.NPC->enemy && InFOV3(NPCS.NPC->enemy->r.currentOrigin,
										NPCS.NPC->r.currentOrigin,
										NPCS.NPC->client->ps.viewangles, 20, 30))
									{
										NPCS.NPC->client->ps.weaponTime = Q_irand(3000, 6000) + (2 - g_npcspskill.
											integer) * 2000;
										TIMER_Set(NPCS.NPC, "gripping", 3000);
										if (NPCS.NPC->painDebounceTime <= level.time
											&& NPCS.NPC->health >= 100)
										{
											//have an attack delay if this attack isn't the result
											//of injury
											TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.weaponTime);
										}
									}
									break;
								case 8:
								case 9:
								default:
									//fly their ass.
									ForceLightning(NPCS.NPC);
									if (NPCS.NPC->client->ps.fd.forcePowerLevel[FP_LIGHTNING] > FORCE_LEVEL_1)
									{
										NPCS.NPC->client->ps.weaponTime = Q_irand(3000, 6000) + (2 - g_npcspskill.
											integer) * 2000;
										TIMER_Set(NPCS.NPC, "holdLightning", NPCS.NPC->client->ps.weaponTime);
									}
									if (NPCS.NPC->painDebounceTime <= level.time
										&& NPCS.NPC->health >= 100)
									{
										TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPC->client->ps.weaponTime);
									}
									break;
								}
							}
						}
						else
						{
							NPCS.NPC->flags &= ~FL_LOCK_PLAYER_WEAPONS;
						}
						Jedi_TimersApply();
						return qtrue;
					}
					NPCS.NPC->flags &= ~FL_LOCK_PLAYER_WEAPONS;
				}
				else if (!G_ClearLOS4(NPCS.NPC, NPCS.NPC->client->leader)
					|| DistanceSquared(NPCS.NPC->r.currentOrigin, NPCS.NPC->client->leader->r.currentOrigin) > 512 *
					512)
				{
					//can't see Rosh or too far away, catch up with him
					if (!TIMER_Done(NPCS.NPC, "attackDelay"))
					{
						NPCS.NPC->flags &= ~FL_LOCK_PLAYER_WEAPONS;
					}
					NPC_BSJedi_FollowLeader();
					NPC_UpdateAngles(qtrue, qtrue);
					return qtrue;
				}
				else
				{
					//no enemies and close to our heal target
					if (!TIMER_Done(NPCS.NPC, "attackDelay"))
					{
						NPCS.NPC->flags &= ~FL_LOCK_PLAYER_WEAPONS;
					}

					NPC_FaceEnemy(qtrue);
					return qtrue;
				}
			}
			NPC_UpdateAngles(qtrue, qtrue);
		}
		else if (NPCS.NPCInfo->aiFlags & NPCAI_ROSH)
		{
			//I'm rosh!
			if (NPCS.NPC->flags & FL_UNDYING)
			{
				//Vil and/or Dasariah still around to heal me
				if (NPCS.NPC->health == 1 //need healing
					|| NPCS.NPC->flags & FL_GODMODE
					|| NPCS.NPC->client->ps.powerups[PW_INVINCIBLE] > level.time) //being healed
				{
					//FIXME: custom anims
					if (Rosh_TwinPresent())
					{
						//twins are still alive
						if (!NPCS.NPC->client->ps.weaponTime)
						{
							//not attacking
							if (NPCS.NPC->client->ps.legsAnim != BOTH_FORCEHEAL_START
								&& NPCS.NPC->client->ps.legsAnim != BOTH_FORCEHEAL_STOP)
							{
								//get down and wait for Vil or Dasariah to help us
								NPCS.NPC->client->ps.legsTimer = NPCS.NPC->client->ps.torsoTimer = 0;
								NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_FORCEHEAL_START,
									SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
								NPCS.NPC->client->ps.torsoTimer = NPCS.NPC->client->ps.legsTimer = -1;
								//hold the animation
								NPCS.NPC->client->ps.saberHolstered = 2; //turn saber off.
								NPCS.NPCInfo->ignorePain = qtrue;
							}
						}
						NPCS.NPC->client->ps.saberBlocked = BLOCKED_NONE;
						NPCS.NPC->client->ps.saberMove /* not sure what this is = NPC->client->ps.saberMoveNext*/ =
							LS_NONE;
						NPCS.NPC->painDebounceTime = level.time + 500;
						NPCS.NPC->client->ps.pm_time = 500;
						NPCS.NPC->client->ps.pm_flags |= PMF_TIME_KNOCKBACK;
						VectorClear(NPCS.NPC->client->ps.velocity);
						VectorClear(NPCS.NPC->client->ps.moveDir);
						return qtrue;
					}
				}
			}
		}
	}

	if (PM_SuperBreakWinAnim(NPCS.NPC->client->ps.torsoAnim))
	{
		//striking down our saberlock victim.
		NPC_FaceEnemy(qtrue);
		if (NPCS.NPC->client->ps.groundEntityNum != ENTITYNUM_NONE)
		{
			//on the ground, don't move
			VectorClear(NPCS.NPC->client->ps.velocity);
		}
		VectorClear(NPCS.NPC->client->ps.moveDir);
		NPCS.ucmd.rightmove = NPCS.ucmd.forwardmove = NPCS.ucmd.upmove = 0;
		return qtrue;
	}
	return qfalse;
}



// ============================================================================
// NPC_CheckEvasion
//
// Evaluates whether the NPC should evade based on enemy weapon, direction,
// missile threats, and animation state. Modernized to eliminate large stack
// allocations (fixing MSVC C6262), add safety checks, explicit qboolean usage,
// and debug prints instead of asserts.
// ============================================================================
void NPC_CheckEvasion(void)
{
	vec3_t enemy_dir, enemy_movedir, enemy_dest;
	float enemy_dist = 0.0f;
	float enemy_movespeed = 0.0f;

	// Camera mode disables AI evasion
	if (jedi_in_camera == qtrue)
	{
		return;
	}

	// Validate enemy pointer
	if (NPCS.NPC->enemy == NULL ||
		NPCS.NPC->enemy->inuse == qfalse ||
		(NPCS.NPC->enemy->NPC != NULL && NPCS.NPC->enemy->health <= 0))
	{
		return;
	}

	// Only certain classes are allowed to evade
	switch (NPCS.NPC->client->NPC_class)
	{
	case CLASS_BARTENDER:
	case CLASS_BESPIN_COP:
	case CLASS_CLAW:
	case CLASS_COMMANDO:
	case CLASS_GALAK:
	case CLASS_GRAN:
	case CLASS_IMPERIAL:
	case CLASS_IMPWORKER:
	case CLASS_JAN:
	case CLASS_LANDO:
	case CLASS_MONMOTHA:
	case CLASS_PRISONER:
	case CLASS_PROTOCOL:
	case CLASS_REBEL:
	case CLASS_REELO:
	case CLASS_RODIAN:
	case CLASS_TRANDOSHAN:
	case CLASS_WEEQUAY:
	case CLASS_BOBAFETT:
	case CLASS_MANDO:
	case CLASS_SABOTEUR:
	case CLASS_STORMTROOPER:
	case CLASS_CLONETROOPER:
	case CLASS_STORMCOMMANDO:
	case CLASS_SWAMPTROOPER:
	case CLASS_NOGHRI:
	case CLASS_UGNAUGHT:
	case CLASS_WOOKIE:
		break;

	default:
		return;
	}

	// Predict enemy position 300ms ahead
	jedi_set_enemy_info(enemy_dest, enemy_dir, &enemy_dist, enemy_movedir, &enemy_movespeed, 300);

	// Saber enemy ? use saber evasion
	if (NPCS.NPC->enemy->s.weapon == WP_SABER)
	{
		Jedi_EvasionSaber(enemy_movedir, enemy_dist, enemy_dir);
		return;
	}

	// Non-saber enemy
	if (NPCS.NPC->enemy->client != NULL)
	{
		vec3_t shot_dir;
		vec3_t ang;

		VectorSubtract(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin, shot_dir);
		vectoangles(shot_dir, ang);

		// Enemy firing directly at us
		if (NPCS.NPC->enemy->client->ps.weaponstate == WEAPON_FIRING &&
			in_field_of_vision(NPCS.NPC->enemy->client->ps.viewangles, 90, ang) == qtrue)
		{
			if (NPCS.NPC->enemy->s.weapon == WP_SABER)
			{
				Jedi_EvasionSaber(enemy_movedir, enemy_dist, enemy_dir);
			}
			else
			{
				NPC_StartFlee(NPCS.NPC->enemy, NPCS.NPC->enemy->r.currentOrigin, AEL_DANGER, 1000, 3000);
			}
			return;
		}

		// Enemy looking toward us
		if (in_field_of_vision(NPCS.NPC->enemy->client->ps.viewangles, 60, ang) == qtrue)
		{
			if (NPCS.NPC->enemy->s.weapon == WP_SABER)
			{
				Jedi_EvasionSaber(enemy_movedir, enemy_dist, enemy_dir);
			}
			else
			{
				NPC_StartFlee(NPCS.NPC->enemy, NPCS.NPC->enemy->r.currentOrigin, AEL_DANGER, 1000, 3000);
			}
			return;
		}

		// --- Missile scan ---
		static int entity_list[MAX_GENTITIES];
		vec3_t mins = { 0 };
		vec3_t maxs = { 0 };

		for (int e = 0; e < 3; e++)
		{
			mins[e] = NPCS.NPC->r.currentOrigin[e] - 256.0f;
			maxs[e] = NPCS.NPC->r.currentOrigin[e] + 256.0f;
		}

		const int num_ents = trap->EntitiesInBox(mins, maxs, entity_list, MAX_GENTITIES);

		for (int i = 0; i < num_ents; i++)
		{
			const gentity_t* missile = &g_entities[entity_list[i]];

			if (missile == NULL)
			{
				continue;
			}
			if (missile->inuse == qfalse)
			{
				continue;
			}

			if (missile->s.eType == ET_MISSILE)
			{
				// Missile incoming ? evade
				if (NPCS.NPC->enemy->s.weapon == WP_SABER)
				{
					Jedi_EvasionSaber(enemy_movedir, enemy_dist, enemy_dir);
				}
				else
				{
					NPC_StartFlee(NPCS.NPC->enemy, NPCS.NPC->enemy->r.currentOrigin, AEL_DANGER, 1000, 3000);
				}
				return;
			}
		}
	}
}


void NPC_BSJedi_Default(void)
{
	if (Jedi_InSpecialMove())
	{
		return;
	}

	Jedi_CheckCloak();

	// SP (SJE modes): saber NPCs always have at least level 3 saber defense
	if (NPCS.NPC->s.weapon == WP_SABER && NPCS.NPC->client->ps.fd.forcePowerLevel[FP_SABER_DEFENSE] < FORCE_LEVEL_3)
	{
		NPCS.NPC->client->ps.fd.forcePowerLevel[FP_SABER_DEFENSE] = FORCE_LEVEL_3;
	}

	if (!NPCS.NPC->enemy)
	{
		//don't have an enemy, look for one
		if (!NPC_IsJedi(NPCS.NPC))
		{
			NPC_BSST_Patrol();
		}
		else
		{
			Jedi_Patrol();
		}
	}
	else //if ( NPC->enemy )
	{
		//have an enemy
		if (Jedi_WaitingAmbush(NPCS.NPC))
		{
			//we were still waiting to drop down - must have had enemy set on me outside my AI
			Jedi_Ambush(NPCS.NPC);
		}

		if (Jedi_CultistDestroyer(NPCS.NPC) && !NPCS.NPCInfo->charmedTime)
		{
			//destroyer
			//permanent effect
			NPCS.NPCInfo->charmedTime = Q3_INFINITE;
			NPCS.NPC->client->ps.fd.forcePowersActive |= 1 << FP_RAGE;
			NPCS.NPC->client->ps.fd.forcePowerDuration[FP_RAGE] = Q3_INFINITE;
			NPCS.NPC->s.loopSound = G_SoundIndex("sound/movers/objects/green_beam_lp2.wav");
		}

		if (NPCS.NPC->client->NPC_class == CLASS_BOBAFETT)
		{
			if (NPCS.NPC->enemy->enemy != NPCS.NPC && NPCS.NPC->health == NPCS.NPC->client->pers.maxHealth &&
				DistanceSquared(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin) > 800 * 800)
			{
				NPCS.NPCInfo->scriptFlags |= SCF_ALT_FIRE;
				Boba_ChangeWeapon(WP_DISRUPTOR);
				NPC_BSSniper_Default();
				return;
			}
		}

		if (NPCS.NPC->enemy->s.weapon != WP_SABER)
		{
			// Normal non-jedi enemy
			if (TIMER_Done(NPCS.NPC, "heal")
				&& !Jedi_SaberBusy(NPCS.NPC)
				&& NPCS.NPC->client->ps.fd.forcePowerLevel[FP_HEAL] > 0
				&& NPCS.NPC->health > 0
				&& NPCS.NPC->client->ps.weaponTime <= 0
				&& NPCS.NPC->client->ps.fd.forcePower >= 25
				&& NPCS.NPC->client->ps.fd.forcePowerDebounce[FP_HEAL] <= level.time
				&& WP_ForcePowerUsable(NPCS.NPC, FP_HEAL)
				&& NPCS.NPC->health <= NPCS.NPC->client->ps.stats[STAT_MAX_HEALTH] / 3
				&& Q_irand(0, 2) == 2)
			{
				// Try to heal...
				ForceHeal(NPCS.NPC);
				TIMER_Set(NPCS.NPC, "heal", Q_irand(5000, 10000));

				if (NPC_IsJedi(NPCS.NPC))
				{
					// Do deflect taunt...
					G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_GLOAT1, EV_GLOAT3), 5000 + Q_irand(0, 15000));
				}
			}
			else
			{
				// Check for an evasion method...
				if (!in_camera)
				{
					NPC_CheckEvasion();
				}

				if (NPC_IsJedi(NPCS.NPC))
				{
					// Do deflect taunt...
					G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_DEFLECT1, EV_DEFLECT3), 5000 + Q_irand(0, 15000));
				}
			}

			Jedi_Attack();
		}
		else
		{
			// Jedi/Sith.
			if (TIMER_Done(NPCS.NPC, "TalkTime"))
			{
				if (npc_is_dark_jedi(NPCS.NPC))
				{
					// Do taunt/anger...
					const int call_out = Q_irand(0, 3);

					switch (call_out)
					{
					case 3:
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_JCHASE1, EV_JCHASE3), 5000 + Q_irand(0, 15000));
						break;
					case 2:
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_COMBAT1, EV_COMBAT3), 5000 + Q_irand(0, 15000));
						break;
					case 1:
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_ANGER1, EV_ANGER1), 5000 + Q_irand(0, 15000));
						break;
					default:
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_TAUNT1, EV_TAUNT3), 5000 + Q_irand(0, 15000));
						break;
					}
				}
				else if (npc_is_light_jedi(NPCS.NPC))
				{
					// Do taunt...
					const int call_out = Q_irand(0, 2);

					switch (call_out)
					{
					case 2:
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_JCHASE1, EV_JCHASE3), 5000 + Q_irand(0, 15000));
						break;
					case 1:
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_COMBAT1, EV_COMBAT3), 5000 + Q_irand(0, 15000));
						break;
					default:
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_TAUNT1, EV_TAUNT3), 5000 + Q_irand(0, 15000));
					}
				}
				else
				{
					// Do taunt/anger...
					const int call_out = Q_irand(0, 3);

					switch (call_out)
					{
					case 3:
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_JCHASE1, EV_JCHASE3), 5000 + Q_irand(0, 15000));
						break;
					case 2:
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_COMBAT1, EV_COMBAT3), 5000 + Q_irand(0, 15000));
						break;
					case 1:
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_ANGER1, EV_ANGER1), 5000 + Q_irand(0, 15000));
						break;
					default:
						G_AddVoiceEvent(NPCS.NPC, Q_irand(EV_TAUNT1, EV_TAUNT3), 5000 + Q_irand(0, 15000));
						break;
					}
				}
				TIMER_Set(NPCS.NPC, "TalkTime", 5000);
			}

			Jedi_Attack();
		}

		//if we have multiple-jedi combat, probably need to keep checking (at certain debounce intervals) for a better (closer, more active) enemy and switch if needbe...
		if ((!NPCS.ucmd.buttons && !NPCS.NPC->client->ps.fd.forcePowersActive || NPCS.NPC->enemy && NPCS.NPC->enemy->
			health <= 0) && NPCS.NPCInfo->enemyCheckDebounceTime < level.time)
		{
			//not doing anything (or walking toward a vanquished enemy - fixme: always taunt the player?), not using force powers and it's time to look again
			//FIXME: build a list of all local enemies (since we have to find best anyway) for other AI factors- like when to use group attacks, determine when to change tactics, when surrounded, when blocked by another in the enemy group, etc.  Should we build this group list or let the enemies maintain their own list and we just access it?
			gentity_t* sav_enemy = NPCS.NPC->enemy; //FIXME: what about NPC->lastEnemy?

			NPCS.NPC->enemy = NULL;
			gentity_t* newEnemy = NPC_CheckEnemy(NPCS.NPCInfo->confusionTime < level.time, qfalse, qfalse);
			NPCS.NPC->enemy = sav_enemy;
			if (newEnemy && newEnemy != sav_enemy)
			{
				//picked up a new enemy!
				NPCS.NPC->lastEnemy = NPCS.NPC->enemy;
				G_SetEnemy(NPCS.NPC, newEnemy);
			}
			NPCS.NPCInfo->enemyCheckDebounceTime = level.time + Q_irand(1000, 3000);
		}
	}
	if (NPCS.NPC->client->saber[0].type == SABER_SITH_SWORD
		&& NPCS.NPC->client->weaponGhoul2[0])
	{
		//have sith sword, think about healing.
		if (NPCS.NPC->health < 100
			&& !Q_irand(0, 20))
		{
			Tavion_SithSwordRecharge();
		}
	}
}



void Boba_Precache(void)
{
	G_SoundIndex("sound/jetpack/ignite.wav");
	G_SoundIndex("sound/jetpack/idle.wav");
	G_SoundIndex("sound/jetpack/thrust.wav");
	G_SoundIndex("sound/chars/boba/bf_land.wav");
	G_SoundIndex("sound/weapons/boba/bf_flame.mp3");
	G_SoundIndex("sound/player/footsteps/boot1");
	G_SoundIndex("sound/player/footsteps/boot2");
	G_SoundIndex("sound/player/footsteps/boot3");
	G_SoundIndex("sound/player/footsteps/boot4");
	G_SoundIndex("sound/boba/jeton.wav");
	G_SoundIndex("sound/boba/jethover.wav");
	G_SoundIndex("sound/effects/combustfire.mp3");
	G_EffectIndex("boba/jet");
	G_EffectIndex("effects/flamethrower/flamethrower_mp.efx");
	G_EffectIndex("volumetric/black_smoke");
	G_EffectIndex("chunks/dustFall");
	G_EffectIndex("boba/fthrw");
	G_EffectIndex("flamethrower/flamethrower_mp");
	G_EffectIndex("flamethrower/flame_impact");
}


void Boba_ChangeWeapon(const int wp)
{
	if (NPCS.NPC->s.weapon == wp)
	{
		return;
	}
	NPC_ChangeWeapon(wp);
	G_AddEvent(NPCS.NPC, EV_GENERAL_SOUND, G_SoundIndex("sound/weapons/change.wav"));
}



qboolean Boba_StopKnockdown(gentity_t* self, const gentity_t* pusher, const vec3_t push_dir,
	const qboolean force_knockdown)
	//forceKnockdown = qfalse
{
	vec3_t pDir, fwd, right, ang;

	if (self->client->NPC_class != CLASS_BOBAFETT
		|| self->client->pers.botclass != BCLASS_BOBAFETT
		|| self->client->pers.botclass != BCLASS_MANDOLORIAN1
		|| self->client->pers.botclass != BCLASS_MANDOLORIAN2)
	{
		return qfalse;
	}

	if (self->client->ps.eFlags2 & EF2_FLYING)
	{
		//can't knock me down when I'm flying
		return qtrue;
	}

	VectorSet(ang, 0, self->r.currentAngles[YAW], 0);
	const int strafeTime = Q_irand(1000, 2000);

	AngleVectors(ang, fwd, right, NULL);
	VectorNormalize2(push_dir, pDir);
	const float fDot = DotProduct(pDir, fwd);
	const float rDot = DotProduct(pDir, right);

	if (Q_irand(0, 2))
	{
		//flip or roll with it
		usercmd_t temp_Cmd = { 0 };
		if (fDot >= 0.4f)
		{
			temp_Cmd.forwardmove = 127;
			TIMER_Set(self, "moveforward", strafeTime);
		}
		else if (fDot <= -0.4f)
		{
			temp_Cmd.forwardmove = -127;
			TIMER_Set(self, "moveback", strafeTime);
		}
		else if (rDot > 0)
		{
			temp_Cmd.rightmove = 127;
			TIMER_Set(self, "strafeRight", strafeTime);
			TIMER_Set(self, "strafeLeft", -1);
		}
		else
		{
			temp_Cmd.rightmove = -127;
			TIMER_Set(self, "strafeLeft", strafeTime);
			TIMER_Set(self, "strafeRight", -1);
		}
		G_AddEvent(self, EV_JUMP, 0);
		if (!Q_irand(0, 1))
		{
			//flip
			self->client->ps.fd.forceJumpCharge = 280; //FIXME: calc this intelligently?
			ForceJump(self, &temp_Cmd);
		}
		else
		{
			//roll
			TIMER_Set(self, "duck", strafeTime);
		}
		self->painDebounceTime = 0; //so we do something
	}
	else if (!Q_irand(0, 1) && force_knockdown)
	{
		//resist
		WP_ResistForcePush(self, pusher, qtrue);
	}
	else
	{
		//fall down
		return qfalse;
	}

	return qtrue;
}



static void Boba_SetJetpackAnims(const gentity_t* self)
{
	vec3_t angs, mvnt;
	int forwardmove = 0, rightmove = 0;

	if (!self)
		return;

	if (!(self->client->ps.eFlags2 & EF2_FLYING))
		return;

	if (!BG_AnimSetIsHumanoid(self->localAnimIndex))
		return;

	if (self->client->ps.legsTimer > 0)
		return;

	if (VectorLengthSquared(NPCS.NPC->client->ps.velocity) < 2048)
		return;

	vectoangles(NPCS.NPC->client->ps.velocity, angs);
	angs[YAW] -= NPCS.NPC->client->ps.viewangles[YAW];
	AngleVectors(angs, mvnt, NULL, NULL);

	if (mvnt[0] > 0)
	{
		rightmove = 1;
	}
	else if (mvnt[0] < 0)
	{
		rightmove = -1;
	}

	if (mvnt[1] > 0)
	{
		forwardmove = 1;
	}
	else if (mvnt[1] < 0)
	{
		forwardmove = -1;
	}
}



void Boba_FlyStart(gentity_t* self)
{
	//switch to seeker AI for a while
	if (TIMER_Done(self, "jetRecharge"))
	{
		self->client->ps.gravity = 0;
		if (self->NPC)
		{
			self->NPC->aiFlags |= NPCAI_CUSTOM_GRAVITY;
		}
		self->client->ps.eFlags2 |= EF2_FLYING; //moveType = MT_FLYSWIM;
		self->client->jetPackTime = level.time + Q_irand(3000, 10000);
		//take-off sound
		G_SoundOnEnt(self, CHAN_ITEM, "sound/chars/boba/bf_blast-off.mp3");
		//jet loop sound
		self->s.loopSound = G_SoundIndex("sound/chars/boba/jethover.wav");
		if (self->NPC)
		{
			self->count = Q3_INFINITE; // SEEKER shot ammo count
		}
	}
}



void Boba_ForceFlyStart(gentity_t* self, const int jetTime)
{
	//switch to seeker AI for a while
	self->client->ps.gravity = 0;
	if (self->NPC)
	{
		self->NPC->aiFlags |= NPCAI_CUSTOM_GRAVITY;
		self->NPC->scriptFlags &= ~SCF_CROUCHED;
		self->NPC->scriptFlags &= ~SCF_RUNNING;
		self->NPC->scriptFlags &= ~SCF_WALKING;
		self->NPC->scriptFlags &= ~SCF_FORCED_MARCH;
	}
	self->client->ps.eFlags2 |= EF2_FLYING;
	self->client->jetPackTime = level.time + jetTime;
	//take-off sound
	G_Sound(self, CHAN_ITEM, G_SoundIndex("sound/chars/boba/bf_blast-off.mp3"));
	if (self->NPC)
	{
		self->count = Q3_INFINITE; // SEEKER shot ammo count
	}
}



void Boba_FlyStop(gentity_t* self)
{
	self->client->ps.gravity = g_gravity.value;
	if (self->NPC)
	{
		self->NPC->aiFlags &= ~NPCAI_CUSTOM_GRAVITY;
	}
	self->client->ps.eFlags2 &= ~EF2_FLYING;
	self->client->jetPackTime = 0;
	//stop jet loop sound
	self->s.loopSound = 0;
	if (self->NPC)
	{
		self->count = 0; // SEEKER shot ammo count
		TIMER_Set(self, "jetRecharge", Q_irand(1000, 5000));
		TIMER_Set(self, "jumpChaseDebounce", Q_irand(500, 2000));
	}
}



qboolean Boba_Flying(const gentity_t* self)
{
	return self->client->ps.eFlags2 & EF2_FLYING;
}



void Boba_FlyThink(gentity_t* self)
{
	if (Boba_Flying(self))
		if (self->client->jetPackTime < level.time)
		{
			Boba_FlyStop(self);
		}
}



void Boba_SetBolts(const gentity_t* self)
{
	if (self && self->client)
	{
		self->client->renderInfo.handLBolt = trap->G2API_AddBolt(self->ghoul2, 0, "*l_hand");
		self->client->renderInfo.handRBolt = trap->G2API_AddBolt(self->ghoul2, 0, "*r_hand");
	}
}


void Boba_JetpackDecideDefault(void)
{
	const float gDist = G_GroundDistance(NPCS.NPC);

	if (!NPCS.NPC->client->playerLeader)
	{
		if (!NPCS.NPC->enemy)
		{
			Boba_FlyThink(NPCS.NPC);
		}
		return;
	}

	if (NPCS.NPC->client->playerLeader->client->jetPackOn && NPCS.NPCInfo->defaultBehavior == BS_FOLLOW_LEADER)
	{
		if (TIMER_Done(NPCS.NPC, "jetpackGoGo"))
		{
			const int r = Q_irand(4000, 6500);

			if (NPCS.NPC->NPC->shouldJetOn)
			{
				NPCS.ucmd.rightmove = NPCS.ucmd.forwardmove = 0;
				NPCS.ucmd.upmove = 127;
				Boba_ForceFlyStart(NPCS.NPC, r);
				NPCS.NPC->NPC->shouldJetOn = qfalse;
			}
			else
			{
				NPCS.NPC->client->jetPackTime = level.time + r;
			}

			TIMER_Set(NPCS.NPC, "jetpackGoGo", r - FRAMETIME * 2);
		}
	}
	else
	{
		if (NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE
			&& NPCS.NPC->client->ps.fd.forceJumpZStart
			&& !PM_FlippingAnim(NPCS.NPC->client->ps.legsAnim)
			&& !Q_irand(0, 3)
			&& !Boba_Flying(NPCS.NPC))
		{
			//take off
			Boba_FlyStart(NPCS.NPC);
		}
		else if (gDist > 512 && !Boba_Flying(NPCS.NPC))
		{
			Boba_ForceFlyStart(NPCS.NPC, Q_irand(2500, 5000));
		}
		Boba_FlyThink(NPCS.NPC);
	}
}



void Boba_JetpackDecideCombat(void)
{
	const float gDist = G_GroundDistance(NPCS.NPC);

	if (NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE
		&& NPCS.NPC->client->ps.fd.forceJumpZStart
		&& !PM_FlippingAnim(NPCS.NPC->client->ps.legsAnim)
		&& !Boba_Flying(NPCS.NPC))
	{
		//take off
		Boba_FlyStart(NPCS.NPC);
	}
	else if (gDist > 512 && !Boba_Flying(NPCS.NPC))
	{
		Boba_ForceFlyStart(NPCS.NPC, Q_irand(2000, 10000) * NPCS.NPCInfo->stats.move);
	}

	Boba_FlyThink(NPCS.NPC);
}



void Boba_ChooseWeapon(void)
{
	if (TIMER_Done(NPCS.NPC, "nextWeaponDelay"))
	{
		if (Q_irand(0, 1))
		{
			NPCS.NPCInfo->scriptFlags &= ~SCF_ALT_FIRE;
		}
		else
		{
			NPCS.NPCInfo->scriptFlags |= SCF_ALT_FIRE;
		}

		while (1)
		{
			const int nw = Q_irand(0, WP_NUM_WEAPONS - 1);

			if (HaveWeapon(nw))
			{
				const int hbw = NPC_HaveBetterWeapon(nw);
				if (hbw == 2 || hbw == 1 && Q_irand(0, 8))
					continue;

				Boba_ChangeWeapon(nw);
				break;
			}
		}

		TIMER_Set(NPCS.NPC, "nextWeaponDelay", Q_irand(5000, 12000));
	}
}



void Boba_FireFlameThrower(gentity_t* self)
{
	const int damage = Q_irand(8, 12);
	trace_t tr;
	mdxaBone_t boltMatrix;
	vec3_t start, end, dir;
	const vec3_t trace_maxs = { 4, 4, 4 };
	const vec3_t trace_mins = { -4, -4, -4 };

	trap->G2API_GetBoltMatrix(self->ghoul2, 0, self->client->renderInfo.handLBolt,
		&boltMatrix, self->r.currentAngles, self->r.currentOrigin, level.time,
		NULL, self->modelScale);

	BG_GiveMeVectorFromMatrix(&boltMatrix, ORIGIN, start);
	BG_GiveMeVectorFromMatrix(&boltMatrix, NEGATIVE_Y, dir);
	VectorMA(start, 128, dir, end);

	trap->Trace(&tr, start, trace_mins, trace_maxs, end, self->s.number, MASK_SHOT, qfalse, 0, 0);

	gentity_t* traceEnt = &g_entities[tr.entityNum];

	if (tr.entityNum < ENTITYNUM_WORLD && traceEnt->takedamage)
	{
		G_Damage(traceEnt, self, self, dir, tr.endpos, damage,
			DAMAGE_NO_ARMOR | DAMAGE_NO_KNOCKBACK | DAMAGE_IGNORE_TEAM, MOD_BURNING);
		G_Throw(traceEnt, dir, 30);

		if (traceEnt->health > 0 && traceEnt->painDebounceTime > level.time)
		{
			G_Throw(traceEnt, dir, 30);

			if (damage && traceEnt->client)
			{
				G_PlayBoltedEffect(G_EffectIndex("flamethrower/flame_impact"), traceEnt, "thoracic");

				if (!PM_InRoll(&traceEnt->client->ps)
					&& !PM_InKnockDown(&traceEnt->client->ps))
				{
					G_SetAnim(traceEnt, &self->client->pers.cmd, SETANIM_TORSO, BOTH_FACEPROTECT,
						SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD, 0);
				}
			}
			player_Burn(traceEnt);
		}
		else
		{
			Player_CheckBurn(traceEnt);
		}
	}
}



void Boba_StartFlameThrower(gentity_t* self)
{
	const int flameTime = 4000; //Q_irand( 1000, 3000 );
	mdxaBone_t boltMatrix;
	vec3_t org, dir;

	self->client->ps.torsoTimer = flameTime; //+1000;
	if (self->NPC)
	{
		TIMER_Set(self, "nextAttackDelay", flameTime);
		TIMER_Set(self, "walking", 0);
	}
	TIMER_Set(self, "flameTime", flameTime);
	G_SoundOnEnt(self, CHAN_WEAPON, "sound/effects/combustfire.mp3");

	trap->G2API_GetBoltMatrix(NPCS.NPC->ghoul2, 0, NPCS.NPC->client->renderInfo.handLBolt, &boltMatrix,
		NPCS.NPC->r.currentAngles,
		NPCS.NPC->r.currentOrigin, level.time, NULL, NPCS.NPC->modelScale);

	BG_GiveMeVectorFromMatrix(&boltMatrix, ORIGIN, org);
	BG_GiveMeVectorFromMatrix(&boltMatrix, NEGATIVE_Y, dir);

	G_PlayEffectID(G_EffectIndex("flamethrower/flamethrower_mp"), org, dir);
}



void Boba_DoFlameThrower(gentity_t* self)
{
	if (self->client->ps.jetpackFuel < 10)
	{
		return;
	}
	NPC_SetAnim(self, SETANIM_TORSO, BOTH_FLAMETHROWER, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	if (TIMER_Done(self, "nextAttackDelay") && TIMER_Done(self, "flameTime"))
	{
		Boba_StartFlameThrower(self);
	}
	Boba_FireFlameThrower(self);
}



void Boba_DoSniper(const gentity_t* self)
{
	if (TIMER_Done(self, "PickNewSniperPoint"))
	{
		TIMER_Set(self, "PickNewSniperPoint", Q_irand(15000, 25000));
		const int SniperPoint = NPC_FindCombatPoint(NPCS.NPC->r.currentOrigin, 0, NPCS.NPC->r.currentOrigin,
			CP_SNIPE | CP_CLEAR | CP_HAS_ROUTE | CP_TRYFAR | CP_HORZ_DIST_COLL,
			0, -1);
		if (SniperPoint != -1)
		{
			NPC_SetCombatPoint(SniperPoint);
			NPC_SetMoveGoal(NPCS.NPC, level.combatPoints[SniperPoint].origin, 16, qtrue, SniperPoint, NULL);
		}
	}

	// combatPoint is -1 when no sniper point was found (e.g. maps without combat points)
	if (NPCS.NPCInfo->combatPoint >= 0 && NPCS.NPCInfo->combatPoint < level.numCombatPoints &&
		Distance(NPCS.NPC->r.currentOrigin, level.combatPoints[NPCS.NPCInfo->combatPoint].origin) < 50.0f)
	{
		Boba_FireDecide();
	}

	NPC_FaceEnemy(qtrue);
	NPC_UpdateAngles(qtrue, qtrue);
}


void Boba_FireWristMissile(gentity_t* self, const int whichMissile)
{
	static int shotsFired = 0; //only 5 shots allowed for wristlaser; only 1 for missile launcher or dart

	if (self->s.number >= MAX_CLIENTS)
	{
		return;
	}

	if (!self->client)
	{
		return;
	}

	if (self->health <= 0)
	{
		return;
	}

	if (in_camera)
	{
		return;
	}

	const int dummyForcePower = missileStates[whichMissile].dummyForcePower;

	if (!self->client->ps.fd.forcePowerDuration[dummyForcePower])
	{
		NPC_SetAnim(self, SETANIM_TORSO, missileStates[whichMissile].fireAnim,
			SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
		self->client->ps.torsoTimer = missileStates[whichMissile].animTimer;
		self->client->ps.fd.forcePowerDuration[dummyForcePower] = 1;
		shotsFired = 0;
	}

	if (self->client->ps.torsoTimer > missileStates[whichMissile].animTimer - missileStates[whichMissile].animDelay)
	{
		return;
	}

	if (self->client->ps.weaponTime > 0)
	{
		return;
	}

	if (missileStates[whichMissile].hold)
	{
		if (self->client->ps.torsoTimer < 150)
		{
			self->client->ps.fd.forcePowerDuration[dummyForcePower] = 150;
			return;
		}
	}
	else
	{
		if (self->client->ps.torsoTimer < 50)
		{
			self->client->ps.fd.forcePowerDuration[dummyForcePower] = 0;
			self->client->ps.torsoTimer = 0;
			return;
		}
	}

	const int oldWeapon = self->s.weapon;

	self->s.weapon = missileStates[whichMissile].whichWeapon;
	qboolean alt_fire = missileStates[whichMissile].alt_fire;
	if (missileStates[whichMissile].fullyCharged)
	{
		self->client->ps.weaponChargeTime = 0;
	}

	const int addTime = weaponData[self->s.weapon].fireTime;
	self->client->ps.weaponTime += addTime;
	self->client->ps.lastShotTime = level.time;

	mdxaBone_t boltMatrix;
	vec3_t muzzlePoint;
	vec3_t muzzleDir;

	//trap->G2API_GetBoltMatrix(self->ghoul2, 0, self->client->renderInfo.handLBolt, &boltMatrix, self->r.currentAngles, self->r.currentOrigin, level.time, NULL, self->modelScale);
	trap->G2API_GetBoltMatrix(self->ghoul2, 0,
		missileStates[whichMissile].leftBolt
		? self->genericBolt3
		: self->client->renderInfo.handLBolt, &boltMatrix, self->r.currentAngles,
		self->r.currentOrigin, level.time, NULL, self->modelScale);

	BG_GiveMeVectorFromMatrix(&boltMatrix, ORIGIN, muzzlePoint);
	BG_GiveMeVectorFromMatrix(&boltMatrix, NEGATIVE_Y, muzzleDir);
	// work the matrix axis stuff into the original axis and origins used.

	VectorCopy(muzzlePoint, self->client->renderInfo.muzzlePoint);
	VectorCopy(muzzleDir, self->client->renderInfo.muzzleDir);

	if (shotsFired >= missileStates[whichMissile].maxShots)
	{
		vec3_t ORIGIN = { 0, 0, 0 };
		G_PlayEffectID(G_EffectIndex("repeater/muzzle_smoke"), muzzlePoint, ORIGIN);
		self->client->ps.userInt3 &= ~(1 << FLAG_WRISTBLASTER);
		//play smoke
		return;
	}
	vec3_t ORIGIN = { 0, 0, 0 };
	G_PlayEffectID(G_EffectIndex("blaster/muzzle_flash"), muzzlePoint, ORIGIN);
	self->client->ps.userInt3 |= 1 << FLAG_WRISTBLASTER;

	WP_FireWristMissile(self, muzzlePoint, muzzleDir);
	shotsFired++;

	self->s.weapon = oldWeapon;
}



void Boba_EndWristMissile(const gentity_t* self, const int which_missile)
{
	if (missileStates[which_missile].hold)
	{
		self->client->ps.fd.forcePowerDuration[missileStates[which_missile].dummyForcePower] = 0;
		self->client->ps.torsoTimer = 0;
	}
}


void Boba_TacticsSelect()
{
	// Don't Change Tactics For A Little While
	//------------------------------------------
	TIMER_Set(NPCS.NPC, "Boba_TacticsSelect", Q_irand(8000, 15000));
	int nextState = NPCS.NPCInfo->localState;

	// Get Some Data That Will Help With The Selection Of The Next Tactic
	//--------------------------------------------------------------------
	const float enemyDistance = Distance(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin);

	// Enemy Is Really Close
	//-----------------------
	if (NPCS.NPC->enemy->health <= 0) // SJE: was "!health > 0" - only true at exactly 0 health (SP: !enemy_alive)
	{
		nextState = BTS_RIFLE;
	}
	else if (enemyDistance < BOBA_FLAMETHROWRANGE)
	{
		// If It's Been Long Enough Since Our Last Flame Blast, Try To Torch The Enemy
		//-----------------------------------------------------------------------------
		if (TIMER_Done(NPCS.NPC, "nextFlameDelay"))
		{
			nextState = BTS_FLAMETHROW;
		}

		// Otherwise, He's Probably Too Close, So Try To Get Clear Of Him
		//----------------------------------------------------------------
		else
		{
			nextState = BTS_RIFLE;
		}
	}

	// Recently Saw The Enemy, Time For Some Good Ole Fighten!
	//---------------------------------------------------------
	else if (level.time - NPCS.NPCInfo->enemyLastSeenTime < 10000)
	{
		// At First, Boba will prefer to use his blaster against the player, but
		//  the more times he is driven away (NPC->count), he will be less likely to
		//  choose the blaster, and more likely to go for the missile launcher
		nextState = !(enemyDistance > BOBA_ROCKETRANGEMIN && enemyDistance < BOBA_ROCKETRANGEMAX) || // SJE: was "!enemyDistance > MIN" (always false) - SP: !enemy_in_rocket_range ||
			Q_irand(0, NPCS.NPC->count) < 1
			? BTS_RIFLE
			: BTS_MISSILE;
	}

	// Hmmm...  Havn't Seen The Player In A While, We Might Want To Try Something Sneaky
	//-----------------------------------------------------------------------------------
	else
	{
		int SnipePointsNear = qfalse; // TODO
		const int AmbushPointNear = qfalse; // TODO

		if (Q_irand(0, NPCS.NPC->count) > 0)
		{
			const int SniperPoint = NPC_FindCombatPoint(NPCS.NPC->r.currentOrigin, 0, NPCS.NPC->r.currentOrigin,
				CP_SNIPE | CP_CLEAR | CP_HAS_ROUTE | CP_TRYFAR |
				CP_HORZ_DIST_COLL, 0, -1);
			if (SniperPoint != -1)
			{
				NPC_SetCombatPoint(SniperPoint);
				NPC_SetMoveGoal(NPCS.NPC, level.combatPoints[SniperPoint].origin, 8, qtrue, SniperPoint, NULL);
				TIMER_Set(NPCS.NPC, "PickNewSniperPoint", Q_irand(15000, 25000));
				SnipePointsNear = qtrue;
			}
		}

		if (SnipePointsNear && TIMER_Done(NPCS.NPC, "Boba_NoSniperTime"))
		{
			TIMER_Set(NPCS.NPC, "Boba_NoSniperTime", 120000); // Don't snipe again for a while
			TIMER_Set(NPCS.NPC, "Boba_TacticsSelect", Q_irand(35000, 45000)); // More patience here
			nextState = BTS_SNIPER;
		}
		else
		{
			nextState = !(enemyDistance > BOBA_ROCKETRANGEMIN && enemyDistance < BOBA_ROCKETRANGEMAX) || // SJE: was "!enemyDistance > MIN" (always false) - SP: !enemy_in_rocket_range ||
				Q_irand(0, NPCS.NPC->count) < 1
				? BTS_RIFLE
				: BTS_MISSILE;
		}
	}

	// The Next State Has Been Selected, Now Change Weapon If Necessary
	//------------------------------------------------------------------
	if (nextState != NPCS.NPCInfo->localState)
	{
		NPCS.NPCInfo->localState = nextState;
		switch (NPCS.NPCInfo->localState)
		{
		case BTS_FLAMETHROW:
			Boba_ChangeWeapon(WP_NONE);
			Boba_DoFlameThrower(NPCS.NPC);
			break;

		case BTS_RIFLE:
			Boba_ChangeWeapon(WP_BLASTER);
			break;

		case BTS_MISSILE:
			Boba_ChangeWeapon(WP_ROCKET_LAUNCHER);
			break;

		case BTS_SNIPER:
			Boba_ChangeWeapon(WP_DISRUPTOR);
			break;

		case BTS_AMBUSHWAIT:
			Boba_ChangeWeapon(WP_NONE);
			break;
		default:;
		}
	}
}



void Boba_DoAmbushWait(gentity_t* self)
{}



void Boba_Tactics()
{
	if (!NPCS.NPC->enemy)
	{
		return;
	}

	// Think About Changing Tactics
	//------------------------------
	if (TIMER_Done(NPCS.NPC, "Boba_TacticsSelect"))
	{
		Boba_TacticsSelect();
	}

	// These Tactics Require Seeker & Jedi Movement
	//----------------------------------------------
	if (!NPCS.NPCInfo->localState ||
		NPCS.NPCInfo->localState == BTS_RIFLE ||
		NPCS.NPCInfo->localState == BTS_MISSILE)
	{
		return;
	}

	// Flame Thrower - Locked In Place
	//---------------------------------
	if (NPCS.NPCInfo->localState == BTS_FLAMETHROW)
	{
		Boba_DoFlameThrower(NPCS.NPC);
	}

	// Sniper - Move Around, And Take Shots
	//--------------------------------------
	else if (NPCS.NPCInfo->localState == BTS_SNIPER)
	{
		Boba_DoSniper(NPCS.NPC);
	}

	// Ambush Wait
	//------------
	else if (NPCS.NPCInfo->localState == BTS_AMBUSHWAIT)
	{
		Boba_DoAmbushWait(NPCS.NPC);
	}

	NPC_FacePosition(NPCS.NPC->enemy->r.currentOrigin, qtrue);
	NPC_UpdateAngles(qtrue, qtrue);
}



float G_GroundDistance(const gentity_t* self)
{
	if (!self)
	{
		//wtf?!!
		return Q3_INFINITE;
	}
	trace_t tr;
	vec3_t down;

	VectorCopy(self->r.currentOrigin, down);

	down[2] -= 4096;

	trap->Trace(&tr, self->client->ps.origin, self->r.mins, self->r.maxs, down, self->s.clientNum, MASK_SOLID, qfalse,
		0, 0);

	VectorSubtract(self->r.currentOrigin, tr.endpos, down);

	return VectorLength(down);
}



void Boba_FireDecide(void)
{
	qboolean enemyLOS = qfalse, enemyCS = qfalse, enemyInFOV = qfalse;
	qboolean shoot = qfalse, hitAlly = qfalse;
	vec3_t impactPos, enemyDir, shootDir;

	if (NPCS.NPC->client->ps.groundEntityNum == ENTITYNUM_NONE
		&& NPCS.NPC->client->ps.fd.forceJumpZStart
		&& !PM_FlippingAnim(NPCS.NPC->client->ps.legsAnim)
		&& !Q_irand(0, 10))
	{
		//take off
		Boba_FlyStart(NPCS.NPC);
	}

	if (!NPCS.NPC->enemy)
	{
		return;
	}

	Boba_JetpackDecideCombat();

	if (NPCS.NPC->enemy->s.weapon == WP_SABER)
	{
		NPCS.NPCInfo->scriptFlags &= ~SCF_ALT_FIRE;
		Boba_ChangeWeapon(WP_ROCKET_LAUNCHER);
	}
	else
	{
		if (NPCS.NPC->health < NPCS.NPC->client->pers.maxHealth * 0.5f)
		{
			NPCS.NPCInfo->scriptFlags |= SCF_ALT_FIRE;
			Boba_ChangeWeapon(WP_BLASTER);
			NPCS.NPCInfo->burstMin = 3;
			NPCS.NPCInfo->burstMean = 12;
			NPCS.NPCInfo->burstMax = 20;
			NPCS.NPCInfo->burstSpacing = Q_irand(300, 750); //attack debounce
		}
		else
		{
			NPCS.NPCInfo->scriptFlags &= ~SCF_ALT_FIRE;
			Boba_ChangeWeapon(WP_BLASTER);
		}
	}

	VectorClear(impactPos);
	const float enemyDist = DistanceSquared(NPCS.NPC->r.currentOrigin, NPCS.NPC->enemy->r.currentOrigin);

	VectorSubtract(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin, enemyDir);
	VectorNormalize(enemyDir);
	AngleVectors(NPCS.NPC->client->ps.viewangles, shootDir, NULL, NULL);
	const float dot = DotProduct(enemyDir, shootDir);
	if (dot > 0.5f || enemyDist * (1.0f - dot) < 10000)
	{
		//enemy is in front of me or they're very close and not behind me
		enemyInFOV = qtrue;
	}

	if (enemyDist < 128 * 128 && enemyInFOV || !TIMER_Done(NPCS.NPC, "flameTime"))
	{
		//flamethrower
		Boba_DoFlameThrower(NPCS.NPC);
		enemyCS = qfalse;
		shoot = qfalse;
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
		NPCS.ucmd.buttons &= ~(BUTTON_ATTACK | BUTTON_ALT_ATTACK);
	}
	else if (enemyDist < MIN_ROCKET_DIST_SQUARED) //128
	{
		//enemy within 128
		if ((NPCS.NPC->client->ps.weapon == WP_FLECHETTE || NPCS.NPC->client->ps.weapon == WP_REPEATER) &&
			NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)
		{
			//shooting an explosive, but enemy too close, switch to primary fire
			NPCS.NPCInfo->scriptFlags &= ~SCF_ALT_FIRE;
			//FIXME: we can never go back to alt-fire this way since, after this, we don't know if we were initially supposed to use alt-fire or not...
		}
	}
	else if (enemyDist > 65536) //256 squared
	{
		if (NPCS.NPC->client->ps.weapon == WP_DISRUPTOR)
		{
			//sniping... should be assumed
			if (!(NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE))
			{
				//use primary fire
				NPCS.NPCInfo->scriptFlags |= SCF_ALT_FIRE;
				//reset fire-timing variables
				NPC_ChangeWeapon(WP_DISRUPTOR);
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
		}
	}

	//can we see our target?
	if (TIMER_Done(NPCS.NPC, "nextAttackDelay") && TIMER_Done(NPCS.NPC, "flameTime"))
	{
		if (NPC_ClearLOS4(NPCS.NPC->enemy))
		{
			NPCS.NPCInfo->enemyLastSeenTime = level.time;
			enemyLOS = qtrue;

			if (NPCS.NPC->client->ps.weapon == WP_NONE)
			{
				enemyCS = qfalse; //not true, but should stop us from firing
			}
			else
			{
				//can we shoot our target?
				if ((NPCS.NPC->client->ps.weapon == WP_ROCKET_LAUNCHER || NPCS.NPC->client->ps.weapon == WP_FLECHETTE
					&& NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE) && enemyDist < MIN_ROCKET_DIST_SQUARED) //128*128
				{
					enemyCS = qfalse; //not true, but should stop us from firing
					hitAlly = qtrue; //us!
					//FIXME: if too close, run away!
				}
				else if (enemyInFOV)
				{
					//if enemy is FOV, go ahead and check for shooting
					const int hit = NPC_ShotEntity(NPCS.NPC->enemy, impactPos);
					const gentity_t* hitEnt = &g_entities[hit];

					if (hit == NPCS.NPC->enemy->s.number
						|| hitEnt && hitEnt->client && hitEnt->client->playerTeam == NPCS.NPC->client->enemyTeam
						|| hitEnt && hitEnt->takedamage && (hitEnt->r.svFlags & SVF_GLASS_BRUSH || hitEnt->health <
							40 || NPCS.NPC->s.weapon == WP_EMPLACED_GUN))
					{
						//can hit enemy or enemy ally or will hit glass or other minor breakable (or in emplaced gun), so shoot anyway
						enemyCS = qtrue;
						//NPC_AimAdjust( 2 );//adjust aim better longer we have clear shot at enemy
						VectorCopy(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPCInfo->enemyLastSeenLocation);
					}
					else
					{
						//Hmm, have to get around this bastard
						//NPC_AimAdjust( 1 );//adjust aim better longer we can see enemy
						if (hitEnt && hitEnt->client && hitEnt->client->playerTeam == NPCS.NPC->client->playerTeam)
						{
							//would hit an ally, don't fire!!!
							hitAlly = qtrue;
						}
						else
						{
							//Check and see where our shot *would* hit... if it's not close to the enemy (within 256?), then don't fire
						}
					}
				}
				else
				{
					enemyCS = qfalse; //not true, but should stop us from firing
				}
			}
		}
		else if (trap->InPVS(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin))
		{
			NPCS.NPCInfo->enemyLastSeenTime = level.time;
		}

		if (NPCS.NPC->client->ps.weapon == WP_NONE)
		{
			shoot = qfalse;
		}
		else
		{
			if (enemyCS)
			{
				shoot = qtrue;
			}
		}

		if (!enemyCS)
		{
			//if have a clear shot, always try
			//See if we should continue to fire on their last position
			if (!hitAlly //we're not going to hit an ally
				&& enemyInFOV //enemy is in our FOV //FIXME: or we don't have a clear LOS?
				&& NPCS.NPCInfo->enemyLastSeenTime > 0) //we've seen the enemy
			{
				if (level.time - NPCS.NPCInfo->enemyLastSeenTime < 10000)
					//we have seem the enemy in the last 10 seconds
				{
					if (!Q_irand(0, 10))
					{
						//Fire on the last known position
						vec3_t muzzle;
						qboolean tooClose = qfalse;
						qboolean tooFar = qfalse;

						CalcEntitySpot(NPCS.NPC, SPOT_HEAD, muzzle);
						if (VectorCompare(impactPos, vec3_origin))
						{
							//never checked ShotEntity this frame, so must do a trace...
							trace_t tr;
							//vec3_t	mins = {-2,-2,-2}, maxs = {2,2,2};
							vec3_t forward, end;
							AngleVectors(NPCS.NPC->client->ps.viewangles, forward, NULL, NULL);
							VectorMA(muzzle, 8192, forward, end);
							trap->Trace(&tr, muzzle, vec3_origin, vec3_origin, end, NPCS.NPC->s.number, MASK_SHOT,
								qfalse, 0, 0);
							VectorCopy(tr.endpos, impactPos);
						}

						//see if impact would be too close to me
						float distThreshold = 16384/*128*128*/; //default
						switch (NPCS.NPC->s.weapon)
						{
						case WP_ROCKET_LAUNCHER:
						case WP_FLECHETTE:
						case WP_THERMAL:
						case WP_TRIP_MINE:
						case WP_DET_PACK:
							distThreshold = 65536/*256*256*/;
							break;
						case WP_REPEATER:
							if (NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)
							{
								distThreshold = 65536/*256*256*/;
							}
							break;
						default:
							break;
						}

						float dist = DistanceSquared(impactPos, muzzle);

						if (dist < distThreshold)
						{
							//impact would be too close to me
							tooClose = qtrue;
						}
						else if (level.time - NPCS.NPCInfo->enemyLastSeenTime > 5000 ||
							NPCS.NPCInfo->group && level.time - NPCS.NPCInfo->group->lastSeenEnemyTime > 5000)
						{
							//we've haven't seen them in the last 5 seconds
							//see if it's too far from where he is
							distThreshold = 65536/*256*256*/; //default
							switch (NPCS.NPC->s.weapon)
							{
							case WP_ROCKET_LAUNCHER:
							case WP_FLECHETTE:
							case WP_THERMAL:
							case WP_TRIP_MINE:
							case WP_DET_PACK:
								distThreshold = 262144/*512*512*/;
								break;
							case WP_REPEATER:
								if (NPCS.NPCInfo->scriptFlags & SCF_ALT_FIRE)
								{
									distThreshold = 262144/*512*512*/;
								}
								break;
							default:
								break;
							}
							dist = DistanceSquared(impactPos, NPCS.NPCInfo->enemyLastSeenLocation);
							if (dist > distThreshold)
							{
								//impact would be too far from enemy
								tooFar = qtrue;
							}
						}

						if (!tooClose && !tooFar)
						{
							vec3_t angles;
							vec3_t dir;
							//okay too shoot at last pos
							VectorSubtract(NPCS.NPCInfo->enemyLastSeenLocation, muzzle, dir);
							VectorNormalize(dir);
							vectoangles(dir, angles);

							NPCS.NPCInfo->desiredYaw = angles[YAW];
							NPCS.NPCInfo->desiredPitch = angles[PITCH];

							shoot = qtrue;
						}
					}
				}
			}
		}

		//FIXME: don't shoot right away!
		if (NPCS.NPC->client->ps.weaponTime > 0)
		{
			if (NPCS.NPC->s.weapon == WP_ROCKET_LAUNCHER)
			{
				if (!enemyLOS || !enemyCS)
				{
					//cancel it
					NPCS.NPC->client->ps.weaponTime = 0;
				}
				else
				{
					//delay our next attempt
					TIMER_Set(NPCS.NPC, "nextAttackDelay", Q_irand(500, 1000));
				}
			}
		}
		else if (shoot)
		{
			//try to shoot if it's time
			if (TIMER_Done(NPCS.NPC, "nextAttackDelay"))
			{
				if (!(NPCS.NPCInfo->scriptFlags & SCF_FIRE_WEAPON)) // we've already fired, no need to do it again here
				{
					WeaponThink();
				}
				//NASTY
				if (NPCS.NPC->s.weapon == WP_ROCKET_LAUNCHER
					&& NPCS.ucmd.buttons & BUTTON_ATTACK
					&& !Q_irand(0, 3))
				{
					//every now and then, shoot a homing rocket
					NPCS.ucmd.buttons &= ~BUTTON_ATTACK;
					NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
					NPCS.NPC->client->ps.weaponTime = Q_irand(500, 1500);
				}
			}
		}
	}
}



void Sphereshield_On(gentity_t* self)
{//cloak this entity
	if (!self || !self->client)
		return;

	// cooldown check
	if (!TIMER_Done(self, "sphereshield_cd"))
	{
		// still cooling down, ignore request
		return;
	}

	if (!self->client->ps.powerups[PW_SPHERESHIELDED])
	{//cloak
		self->client->ps.powerups[PW_SPHERESHIELDED] = Q3_INFINITE;
		G_SoundOnEnt(self, CHAN_ITEM, "sound/barrier/barrier_on.mp3");
		NPC_SetAnim(self, SETANIM_TORSO, BOTH_FORCE_PROTECT_FAST, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	}
}



void Sphereshield_Off(gentity_t* self)
{//decloak this entity
	if (!self || !self->client)
		return;

	if (self->client->ps.powerups[PW_SPHERESHIELDED])
	{//Uncloak
		self->client->ps.powerups[PW_SPHERESHIELDED] = 0;
		G_SoundOnEnt(self, CHAN_ITEM, "sound/barrier/barrier_off.mp3");
		NPC_SetAnim(self, SETANIM_TORSO, BOTH_FORCE_DRAIN_RELEASE, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);

		// start cooldown so it can't be immediately re-enabled
		TIMER_Set(self, "sphereshield_cd", Q_irand(10000, 15000));
	}
}



saberMoveName_t G_PickAutoMultiKick(gentity_t* self, qboolean allowSingles, qboolean storeMove)
{
	return LS_NONE;
}



qboolean Jedi_QuickReactions(gentity_t* self)
{
	if (NPC_IsJedi(self))
	{
		return qtrue;
	}
	return qfalse;
}



static qboolean PM_EvasionAnim(const int anim)
{
	switch (anim)
	{
	case BOTH_DODGE_FL: //# lean-dodge forward left
	case BOTH_DODGE_FR: //# lean-dodge forward right
	case BOTH_DODGE_BL: //# lean-dodge backwards left
	case BOTH_DODGE_BR: //# lean-dodge backwards right
	case BOTH_DODGE_L: //# lean-dodge left
	case BOTH_DODGE_R: //# lean-dodge right
	case BOTH_HOP_L: //# lean-dodge left
	case BOTH_HOP_R: //# lean-dodge right
	case BOTH_CROUCHDODGE: //# lean-dodge right
	case BOTH_DODGE_B:
		return qtrue;
	default:
		return PM_EvasionHoldAnim(anim);
	}
	//return qfalse;
}



static qboolean PM_EvasionHoldAnim(const int anim)
{
	switch (anim)
	{
	case BOTH_DODGE_HOLD_FL:
	case BOTH_DODGE_HOLD_FR:
	case BOTH_DODGE_HOLD_BL:
	case BOTH_DODGE_HOLD_BR:
	case BOTH_DODGE_HOLD_L:
	case BOTH_DODGE_HOLD_R:
	case BOTH_HOP_L: //# lean-dodge left
	case BOTH_HOP_R: //# lean-dodge right
	case BOTH_CROUCHDODGE: //# lean-dodge right
	case BOTH_DODGE_B:
		return qtrue;
	default:;
	}
	return qfalse;
}



static qboolean NPC_Should_Block(const gentity_t* npc)
{
	// Safety
	if (npc == NULL || npc->client == NULL)
	{
		return qfalse;
	}

	// Players and player‑controlled entities never use NPCS.NPC block logic
	if (npc->s.number < MAX_CLIENTS || G_ControlledByPlayer(npc) == qtrue)
	{
		return qfalse;
	}

	// No blocking during cinematic camera
	if (jedi_in_camera == qtrue)
	{
		return qfalse;
	}

	// Must be a saber user
	if (npc->client->ps.weapon != WP_SABER)
	{
		return qfalse;
	}

	// Saber must be active and not thrown
	if ((!npc->client->ps.saberHolstered) == qfalse ||
		npc->client->ps.saberInFlight == qtrue)
	{
		return qfalse;
	}

	// Cannot block while force‑affected
	if ((jedi_being_drained(npc)) ||
		((npc->client->ps.fd.forceGripBeingGripped > level.time)))
	{
		return qfalse;
	}

	// Cannot block during taunts
	if (BG_IsAlreadyinTauntAnim(npc->client->ps.torsoAnim) == qtrue)
	{
		return qfalse;
	}

	// Cannot block during non‑blockable saber attacks
	if (PM_SaberInnonblockableAttack(npc->client->ps.torsoAnim) == qtrue)
	{
		return qfalse;
	}

	// Cannot block while knocked down, rolling, special jumps, massive bounce, etc.
	if (npc->health <= 1 ||
		BG_InKnockDown(npc->client->ps.legsAnim) == qtrue ||
		BG_InKnockDown(npc->client->ps.torsoAnim) == qtrue ||
		PM_InRoll(&npc->client->ps) == qtrue ||
		PM_SuperBreakLoseAnim(npc->client->ps.torsoAnim) == qtrue ||
		PM_SuperBreakWinAnim(npc->client->ps.torsoAnim) == qtrue ||
		PM_SaberInSpecialAttack(npc->client->ps.torsoAnim) == qtrue ||
		PM_SaberInMassiveBounce(npc->client->ps.torsoAnim) == qtrue ||
		PM_SaberInBashedAnim(npc->client->ps.torsoAnim) == qtrue ||
		PM_Saberinstab(npc->client->ps.saberMove) == qtrue ||
		PM_InSpecialJump(npc->client->ps.torsoAnim) == qtrue ||
		npc->client->ps.groundEntityNum == ENTITYNUM_NONE)
	{
		return qfalse;
	}

	// Must have an enemy to block
	if (npc->enemy == NULL)
	{
		return qfalse;
	}

	// Distance‑based block stance logic
	// Block stance only at combat range (ported from MovieDuels): the distance left once the NPCS.NPC's
	// reach is taken off, as the Jedi AI measures it (<= 0 means the enemy is within saber reach).
	const float reach = npc->client->saber[0].blade[0].lengthMax + npc->r.maxs[0] * 1.5f + 16.0f;
	const float gap = Distance(npc->r.currentOrigin, npc->enemy->r.currentOrigin) - reach;

	// Tunable thresholds
	const float BLOCK_ENGAGE_GAP = 0.0f;    // enter block: enemy within reach
	const float BLOCK_RELEASE_GAP = 32.0f;  // exit block: a little beyond reach, so it doesn't flicker

	// Current stance flag
	const qboolean blockActive =
		((npc->client->ps.ManualBlockingFlags & (1 << MBF_NPCBLOCKSTANCE)) != 0 ? qtrue : qfalse);

	// ENTER BLOCK STANCE
	if (blockActive == qfalse && gap <= BLOCK_ENGAGE_GAP)
	{
		return qtrue;
	}

	// EXIT BLOCK STANCE
	if (blockActive == qtrue && gap >= BLOCK_RELEASE_GAP)
	{
		return qfalse;
	}

	// If already blocking and still within the window → stay blocking
	return blockActive;
}



static qboolean wp_saber_Off_Dash_Evasion(gentity_t* self, vec3_t hitloc)
{
	if (!self || !self->client)
	{
		return qfalse;
	}

	// Compute direction from attacker to defender
	vec3_t diff;
	VectorSubtract(hitloc, self->client->renderInfo.eyePoint, diff);
	diff[2] = 0.0f;
	VectorNormalize(diff);

	// Determine if attack is in front
	const qboolean in_front =
		InFront(hitloc, self->client->ps.origin, self->client->ps.viewangles, 0.3f);

	// Compute right vector for side‑dodge logic
	vec3_t fwdangles = { 0.0f, self->client->ps.viewangles[1], 0.0f };
	vec3_t right;
	AngleVectors(fwdangles, NULL, right, NULL);

	const float rightdot = DotProduct(right, diff);
	const float zdiff = hitloc[2] - self->client->renderInfo.eyePoint[2];

	// Movement direction for the dash
	vec3_t dir;
	AngleVectors(self->client->ps.viewangles, dir, NULL, NULL);

	// Cannot dash if in a restricted state
	if (PM_SaberInStart(self->client->ps.saberMove) ||
		PM_SaberInTransition(self->client->ps.saberMove) ||
		BG_InKnockDown(self->client->ps.legsAnim) ||
		BG_InKnockDown(self->client->ps.torsoAnim) ||
		PM_InRoll(&self->client->ps) ||
		PM_SuperBreakWinAnim(self->client->ps.torsoAnim) ||
		PM_SaberInSpecialAttack(self->client->ps.torsoAnim) ||
		PM_InSpecialJump(self->client->ps.torsoAnim) ||
		self->client->ps.groundEntityNum == ENTITYNUM_NONE ||
		self->client->ps.fd.forcePower < BLOCKPOINTS_HALF ||
		self->client->ps.fd.blockPoints < BLOCKPOINTS_HALF)
	{
		return qfalse;
	}

	// ============================
	// DASH ANIMATION SELECTION
	// ============================

	if (!in_front)
	{
		// Attack from behind → forward dash
		NPC_SetAnim(self, SETANIM_TORSO, BOTH_DASH_F, SETANIM_AFLAG_PACE);
	}
	else if (zdiff > -5.0f)
	{
		// Horizontal attacks
		if (rightdot > 0.3f)
			NPC_SetAnim(self, SETANIM_TORSO, BOTH_DASH_L, SETANIM_AFLAG_PACE);
		else if (rightdot < -0.3f)
			NPC_SetAnim(self, SETANIM_TORSO, BOTH_DASH_R, SETANIM_AFLAG_PACE);
		else
			NPC_SetAnim(self, SETANIM_TORSO, BOTH_DASH_B, SETANIM_AFLAG_PACE);
	}
	else if (zdiff > -22.0f)
	{
		// Mid‑height attacks
		if (rightdot > 0.1f)
			NPC_SetAnim(self, SETANIM_TORSO, BOTH_DASH_L, SETANIM_AFLAG_PACE);
		else if (rightdot < -0.1f)
			NPC_SetAnim(self, SETANIM_TORSO, BOTH_DASH_R, SETANIM_AFLAG_PACE);
		else
			NPC_SetAnim(self, SETANIM_TORSO, BOTH_DASH_B, SETANIM_AFLAG_PACE);
	}
	else
	{
		// Low attacks
		if (rightdot >= 0.0f)
			NPC_SetAnim(self, SETANIM_TORSO, BOTH_DASH_L, SETANIM_AFLAG_PACE);
		else
			NPC_SetAnim(self, SETANIM_TORSO, BOTH_DASH_R, SETANIM_AFLAG_PACE);
	}

	// ============================
	// APPLY DASH VELOCITY
	// ============================

	// Proper dash impulse (instead of multiplying existing velocity)
	VectorScale(dir, 400.0f, self->client->ps.velocity);

	// ============================
	// SOUND (with spam protection)
	// ============================

	if (self->client->ps.fd.forcePowerDebounce[FP_SPEED] < level.time)
	{
		G_SoundOnEnt(self, CHAN_BODY, "sound/weapons/force/dash.mp3");
		self->client->ps.fd.forcePowerDebounce[FP_SPEED] = level.time + 200;
	}

	// ============================
	// NPCS.NPC parry recalculation
	// ============================

	if (self->s.number >= MAX_CLIENTS && !G_ControlledByPlayer(self))
	{
		const int parry_re_calc_time = Jedi_ReCalcParryTime(self, EVASION_PARRY);
		if (self->client->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE] <
			level.time + parry_re_calc_time)
		{
			self->client->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE] =
				level.time + parry_re_calc_time;
		}
	}

	// Mark dashing
	self->client->ps.communicatingflags |= (1 << CF_DASHING);

	return qtrue;
}



static qboolean wp_saber_block_check_random(gentity_t* self, vec3_t hitloc)
{
	vec3_t diff, fwdangles = { 0, 0, 0 }, right;
	const qboolean inFront = InFront(hitloc, self->client->ps.origin, self->client->ps.viewangles, -0.7f);

	VectorSubtract(hitloc, self->client->renderInfo.eyePoint, diff);
	diff[2] = 0;
	VectorNormalize(diff);
	fwdangles[1] = self->client->ps.viewangles[1];
	// Ultimately we might care if the shot was ahead or behind, but for now, just quadrant is fine.
	AngleVectors(fwdangles, NULL, right, NULL);

	const float rightdot = DotProduct(right, diff);
	const float zdiff = hitloc[2] - self->client->renderInfo.eyePoint[2];

	if (self->client->ps.weaponstate == WEAPON_DROPPING ||
		self->client->ps.weaponstate == WEAPON_RAISING)
	{
		return qfalse;
	}
	if (PM_SuperBreakLoseAnim(self->client->ps.torsoAnim)
		|| PM_SuperBreakWinAnim(self->client->ps.torsoAnim))
	{
		return qfalse;
	}

	if (PM_SaberInMassiveBounce(self->client->ps.torsoAnim) || PM_SaberInBashedAnim(self->client->ps.torsoAnim))
	{
		return qfalse;
	}

	if (!inFront && self->client->ps.fd.forcePowerLevel[FP_SABER_DEFENSE] >= FORCE_LEVEL_1)
	{
		switch (self->client->ps.fd.saberAnimLevel)
		{
		case SS_STAFF:
			NPC_SetAnim(self, SETANIM_TORSO, BOTH_P7_S1_B_, SETANIM_AFLAG_PACE);
			break;
		case SS_DUAL:
			NPC_SetAnim(self, SETANIM_TORSO, BOTH_P6_S1_B_, SETANIM_AFLAG_PACE);
			break;
		default:
			NPC_SetAnim(self, SETANIM_TORSO, BOTH_P1_S1_B_, SETANIM_AFLAG_PACE);
			break;
		}
	}
	else if (zdiff > -5)
	{
		if (rightdot > 0.3)
		{
			self->client->ps.saberBlocked = BLOCKED_UPPER_RIGHT;
		}
		else if (rightdot < -0.3)
		{
			self->client->ps.saberBlocked = BLOCKED_UPPER_LEFT;
		}
		else
		{
			self->client->ps.saberBlocked = BLOCKED_TOP;
		}
	}
	else if (zdiff > -22)
	{
		if (zdiff < -10)
		{
			//NPCS.NPC should duck, but NPCS.NPC should never get here
		}
		if (rightdot > 0.1)
		{
			self->client->ps.saberBlocked = BLOCKED_UPPER_RIGHT;
		}
		else if (rightdot < -0.1)
		{
			self->client->ps.saberBlocked = BLOCKED_UPPER_LEFT;
		}
		else
		{
			self->client->ps.saberBlocked = BLOCKED_TOP;
		}
	}
	else
	{
		if (rightdot >= 0)
		{
			self->client->ps.saberBlocked = BLOCKED_LOWER_RIGHT;
		}
		else
		{
			self->client->ps.saberBlocked = BLOCKED_LOWER_LEFT;
		}
	}

	if (self->s.number >= MAX_CLIENTS && !G_ControlledByPlayer(self) && self->client->ps.saberBlocked != BLOCKED_NONE)
	{
		const int parry_re_calc_time = Jedi_ReCalcParryTime(self, EVASION_PARRY);
		if (self->client->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE] < level.time + parry_re_calc_time)
		{
			self->client->ps.fd.forcePowerDebounce[FP_SABER_DEFENSE] = level.time + parry_re_calc_time;
		}
	}

	self->client->ps.userInt3 &= ~(1 << FLAG_PREBLOCK);
	return qtrue;
}
