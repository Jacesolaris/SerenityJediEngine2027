// leave this line at the top of all AI_xxxx.cpp files for PCH reasons...
#include "b_local.h"

/*
-------------------------
Tusken raiders: SP's AI_Tusken.cpp (DH 2026-11-03, NPC parity batch 4)

As SP's NPC_RunBehavior, an NPC with the tusken rifle or the tusken staff runs this (a tusken rifle with SCF_ALT_FIRE
is a sniper, NPC.c). Without an enemy it patrols: looks for enemies, turns to look at what it heard or saw, follows its
path. With one it taunts now and then from a distance (BOTH_TUSKENTAUNT1), closes in when allowed to chase, and attacks
in reach: out of strike range the staff lunges (alt attack), the rifle shoots (alt attack), close in the staff swings
and the rifle clubs with the butt (attack). A jawa it fights makes the jawa fight back, and a player close by is
preferred over the jawa. The swing damage is the weapon's own (g_weapon.c, SP's Tusken_StaffTracenew), not traced here
again.
-------------------------
*/

static float tuskenEnemyDist;
extern void npc_check_speak(gentity_t* speaker_npc); // NPC_AI_Stormtrooper.c (SP: idle combat chatter, g_Advancedaitalk)

/*
-------------------------
Tusken_HoldPosition
-------------------------
*/
static void Tusken_HoldPosition(void)
{
	NPC_FreeCombatPoint(NPCS.NPCInfo->combatPoint, qtrue);
	NPCS.NPCInfo->goalEntity = NULL;
}

/*
-------------------------
Tusken_Move
-------------------------
*/
static qboolean Tusken_Move(void)
{
	NPCS.NPCInfo->combatMove = qtrue; //always move straight toward our goal

	const qboolean moved = NPC_MoveToGoal(qtrue);

	//If our move failed, then reset
	if (moved == qfalse)
	{
		//couldn't get to enemy, just hang here
		Tusken_HoldPosition();
	}
	return moved;
}

/*
-------------------------
NPC_BSTusken_Patrol
-------------------------
*/
static void NPC_BSTusken_Patrol(void)
{
	if (NPCS.NPCInfo->confusionTime < level.time)
	{
		//Look for any enemies
		if (NPCS.NPCInfo->scriptFlags & SCF_LOOK_FOR_ENEMIES)
		{
			if (NPC_CheckPlayerTeamStealth())
			{
				NPC_AngerSound();
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
		}

		if (!(NPCS.NPCInfo->scriptFlags & SCF_IGNORE_ALERTS))
		{
			//Is there danger nearby
			const int alertEvent = NPC_CheckAlertEvents(qtrue, qtrue, -1, qfalse, AEL_SUSPICIOUS);
			if (NPC_CheckForDanger(alertEvent))
			{
				NPC_UpdateAngles(qtrue, qtrue);
				return;
			}
			//check for other alert events
			if (alertEvent >= 0)
			{
				if (level.alertEvents[alertEvent].level == AEL_DISCOVERED)
				{
					if (level.alertEvents[alertEvent].owner &&
						level.alertEvents[alertEvent].owner->client &&
						level.alertEvents[alertEvent].owner->health >= 0 &&
						level.alertEvents[alertEvent].owner->client->playerTeam == NPCS.NPC->client->enemyTeam)
					{
						//an enemy
						G_SetEnemy(NPCS.NPC, level.alertEvents[alertEvent].owner);
						TIMER_Set(NPCS.NPC, "attackDelay", Q_irand(500, 2500));
					}
				}
				else
				{
					//Save the position for movement (if necessary)
					VectorCopy(level.alertEvents[alertEvent].position, NPCS.NPCInfo->investigateGoal);
					NPCS.NPCInfo->investigateDebounceTime = level.time + Q_irand(500, 1000);
					if (level.alertEvents[alertEvent].level == AEL_SUSPICIOUS)
					{
						//suspicious looks longer
						NPCS.NPCInfo->investigateDebounceTime += Q_irand(500, 2500);
					}
				}
			}

			if (NPCS.NPCInfo->investigateDebounceTime > level.time)
			{
				//NOTE: stops walking or doing anything else below
				vec3_t dir, angles;

				VectorSubtract(NPCS.NPCInfo->investigateGoal, NPCS.NPC->client->renderInfo.eyePoint, dir);
				vectoangles(dir, angles);

				const float oYaw = NPCS.NPCInfo->desiredYaw;
				const float oPitch = NPCS.NPCInfo->desiredPitch;
				NPCS.NPCInfo->desiredYaw = angles[YAW];
				NPCS.NPCInfo->desiredPitch = angles[PITCH];

				NPC_UpdateAngles(qtrue, qtrue);

				NPCS.NPCInfo->desiredYaw = oYaw;
				NPCS.NPCInfo->desiredPitch = oPitch;
				return;
			}
		}
	}

	//If we have somewhere to go, then do that
	if (UpdateGoal())
	{
		NPCS.ucmd.buttons |= BUTTON_WALKING;
		NPC_MoveToGoal(qtrue);
	}

	NPC_UpdateAngles(qtrue, qtrue);
}

static void NPC_Tusken_Taunt(void)
{
	NPC_SetAnim(NPCS.NPC, SETANIM_BOTH, BOTH_TUSKENTAUNT1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	TIMER_Set(NPCS.NPC, "taunting", NPCS.NPC->client->ps.torsoTimer);
	TIMER_Set(NPCS.NPC, "duck", -1);
}

/*
-------------------------
NPC_BSTusken_Attack
-------------------------
*/
static void NPC_BSTusken_Attack(void)
{
	// IN PAIN
	if (NPCS.NPC->painDebounceTime > level.time)
	{
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	// IN FLEE
	if (TIMER_Done(NPCS.NPC, "flee") && NPC_CheckForDanger(NPC_CheckAlertEvents(qtrue, qtrue, -1, qfalse, AEL_DANGER)))
	{
		NPC_UpdateAngles(qtrue, qtrue);
		return;
	}

	// UPDATE OUR ENEMY
	if (NPC_CheckEnemyExt(qfalse) == qfalse || !NPCS.NPC->enemy)
	{
		NPC_BSTusken_Patrol();
		return;
	}
	tuskenEnemyDist = Distance(NPCS.NPC->enemy->r.currentOrigin, NPCS.NPC->r.currentOrigin);

	// Is The Current Enemy A Jawa?
	if (NPCS.NPC->enemy->client && NPCS.NPC->enemy->client->NPC_class == CLASS_JAWA)
	{
		// Make Sure His Enemy Is Me
		if (NPCS.NPC->enemy->enemy != NPCS.NPC)
		{
			G_SetEnemy(NPCS.NPC->enemy, NPCS.NPC);
		}

		// Should We Forget About Our Current Enemy And Go After A Player? (SP: the player, close and in the same region)
		gentity_t* closest = FindClosestPlayer(NPCS.NPC->r.currentOrigin, NPCS.NPC->client->enemyTeam);
		if (closest && closest != NPCS.NPC->enemy
			&& Distance(closest->r.currentOrigin, NPCS.NPC->r.currentOrigin) < 130.0f
			&& trap->InPVS(closest->r.currentOrigin, NPCS.NPC->r.currentOrigin))
		{
			G_SetEnemy(NPCS.NPC, closest);
		}
	}

	// Update Our Last Seen Time
	if (NPC_ClearLOS4(NPCS.NPC->enemy))
	{
		NPCS.NPCInfo->enemyLastSeenTime = level.time;
	}

	// Check To See If We Are In Attack Range
	const float boundsMin = NPCS.NPC->r.maxs[0] + NPCS.NPC->enemy->r.maxs[0];
	const float lungeRange = boundsMin + 65.0f;
	const float strikeRange = boundsMin + 40.0f;
	const qboolean meleeRange = tuskenEnemyDist < lungeRange ? qtrue : qfalse;
	const qboolean meleeWeapon = NPCS.NPC->client->ps.weapon != WP_TUSKEN_RIFLE ? qtrue : qfalse;
	const qboolean canSeeEnemy = level.time - NPCS.NPCInfo->enemyLastSeenTime < 3000 ? qtrue : qfalse;

	// Check To Start Taunting
	if (canSeeEnemy && !meleeRange && TIMER_Done(NPCS.NPC, "tuskenTauntCheck"))
	{
		TIMER_Set(NPCS.NPC, "tuskenTauntCheck", Q_irand(2000, 6000));
		if (!Q_irand(0, 3))
		{
			NPC_Tusken_Taunt();
		}
	}

	if (TIMER_Done(NPCS.NPC, "taunting"))
	{
		// Should I Attack?
		if (meleeRange || !meleeWeapon && canSeeEnemy)
		{
			if (!(NPCS.NPCInfo->scriptFlags & SCF_FIRE_WEAPON) // If This Flag Is On, It Calls Attack From Elsewhere
				&& !(NPCS.NPCInfo->scriptFlags & SCF_DONT_FIRE) // If This Flag Is On, Don't Fire At All
				&& TIMER_Done(NPCS.NPC, "attackDelay"))
			{
				NPCS.ucmd.buttons &= ~BUTTON_ALT_ATTACK;

				// If Not In Strike Range, Do Lunge, Or If We Don't Have The Staff, Just Shoot Normally
				if (tuskenEnemyDist > strikeRange)
				{
					NPCS.ucmd.buttons |= BUTTON_ALT_ATTACK;
				}

				WeaponThink();
				TIMER_Set(NPCS.NPC, "attackDelay", NPCS.NPCInfo->shotTime - level.time);
			}

			if (!TIMER_Done(NPCS.NPC, "duck"))
			{
				NPCS.ucmd.upmove = -127;
			}
		}
		// Or Should I Move?
		else if (NPCS.NPCInfo->scriptFlags & SCF_CHASE_ENEMIES)
		{
			NPCS.NPCInfo->goalEntity = NPCS.NPC->enemy;
			NPCS.NPCInfo->goalRadius = lungeRange;
			Tusken_Move();
		}
	}

	// UPDATE ANGLES
	if (canSeeEnemy)
	{
		NPC_FaceEnemy(qtrue);
	}
	NPC_UpdateAngles(qtrue, qtrue);
}

void NPC_BSTusken_Default(void)
{
	if (NPCS.NPCInfo->scriptFlags & SCF_FIRE_WEAPON)
	{
		WeaponThink();
	}

	if (!NPCS.NPC->enemy)
	{
		//don't have an enemy, look for one
		NPC_BSTusken_Patrol();
	}
	else
	{
		//have an enemy
		NPC_BSTusken_Attack();

		npc_check_speak(NPCS.NPC); // (SP)
	}
}