/*
===========================================================================
Pazaak - singleplayer game module side

The match itself runs in the UI (ui_pazaak.cpp) against the AI, the world is paused
meanwhile. This file starts it and settles it. Everyone at the match first sits down to meditate
(BOTH_MEDITATE), facing the opponent, and is invulnerable until it is over, then stands up again.

The Play Pazaak key ("pazaak") does one of two things, depending on what is in the crosshair:
1. nobody: a match against a nameless AI.
2. an NPC: he is challenged. If he is close enough (PZK_CHALLENGE_RANGE, "too far away" otherwise),
   not hostile and not busy fighting, he accepts, sits down with you and is the opponent on the
   board: really the AI plays with his name, he only sits there (his AI rests meanwhile, NPC.cpp).
   An NPC who can't sit (a droid, an animal) still lends his name to the AI.
- "pazaak_result": sent back by the UI when the match is over
No match during a cutscene (camera, a script holding the player, a video): "Pazaak is unavailable".

Scripts (ICARUS, see Q3_Interface.cpp and PAZAAK_SCRIPTING.txt):
- SET_PAZAAK_PLAY "<targetname>": a match between the player and that NPC (he sits down too, no distance
  rule), or with the AI under that name if no entity has it ("" or "AI": a nameless AI)
- SET_PAZAAK_WAGER "<credits>": the wager of the next SET_PAZAAK_PLAY match (taken from PERS_CREDITS)
- SET_PAZAAK_ALLOWED "true" / "false": whether the player's Play Pazaak key works (g_pazaakAllowed)
- SET_PAZAAK_END "<script>": the script the entity that set it runs when the next match is over (once;
  "NULL" clears it); pazaak_result is already set then
- when a match is over: the global float "pazaak_result" (if the script declared it) becomes
  1 = the player won, 2 = the opponent won, 0 = called off, then the signal "pazaak_done" is set

Challenges before a map's own scripts (PZK_CHALLENGE_FILE, e.g. the bar of ns_streets): when a listed script is
about to start, it is held back, once per map load, and
the UI asks "challenge him to a pazaak match? Yes / No" (cinematic bars, the game paused, the cursor on:
ui_pazaak.cpp "uipzk_challenge"). The answer comes back as "pazaak_challenge 1/0":
- No: the held script runs as normal
- Yes: a bar match: the player stands (BOTH_STAND1IDLE1, no meditating), the opponent stays as he is (his AI
  rests), the sound is not muted; when it is over the held script runs
Two ways a script starts, two ways to hold it:
- RunScript (a trigger's or an entity's own script; G_Pazaak_InterceptScript from Q3_Interface.cpp): it is
  not run, and run later (as SET_PAZAAK_END after a match)
- an ICARUS run( ) inside a running script (G_Pazaak_HoldRun from icarus/Sequencer.cpp CheckRun): the sequencer
  waits on the signal "pazaak_challenge" in front of the run, which is given after the answer (or the match),
  so the script goes on exactly there (e.g. after Kyle's line, before the patrons answer)
g_pazaakScriptLog 1 prints every script that starts, to find the name of the one to list.
===========================================================================
*/

#include "g_local.h"
#include "g_pazaak.h"
#include "b_local.h"
#include "Q3_Interface.h"
#include "../icarus/IcarusImplementation.h"

#define PZK_LOOK_RANGE		2048	// who is in the crosshair (further away: nobody, the AI)
#define PZK_CHALLENGE_RANGE	256		// an NPC must be this close to be challenged
#define PZK_SIT_TIME	1500	// time to sit down before the board opens, if the anim does not tell
#define PZK_ENEMY_RANGE	1024	// no match while an enemy who is after the player is this close (and in his PVS)

extern bool in_camera;
extern qboolean player_locked;
extern cvar_t* g_skippingcin;
extern qboolean PM_InKnockDown(const playerState_t* ps);
extern qboolean PM_HasAnimation(const gentity_t* ent, int animation);

static int pzkWager = 0;            // credits taken for the running match
static int pzkOpponent = -1;        // NPC entity number
static qboolean pzkRunning = qfalse;
static int pzkStartAt = 0;          // time the board opens (0 = open, or nothing pending)
static char pzkName[64];
static qboolean pzkHadGod = qfalse; // the player had god mode before the match
static qboolean pzkNPCSits = qfalse;     // the opponent NPC sits at the match
static qboolean pzkNPCHadGod = qfalse;   // he had god mode before it
static int pzkScriptWager = 0;      // SET_PAZAAK_WAGER: the wager of the next SET_PAZAAK_PLAY match
static int pzkEndScriptEnt = -1;    // SET_PAZAAK_END: who runs pzkEndScript when the next match is over (-1 = nobody)
static char pzkEndScript[MAX_QPATH];

static cvar_t* g_pazaakAllowed;     // SET_PAZAAK_ALLOWED: 0 = the Play Pazaak key does nothing (saved with the game)

// Challenges before a map's own scripts (see the top of the file)
#define PZK_CHALLENGE_FILE	"ext_data/sje_pazaak_challenges.dat"
#define PZK_MAX_CHALLENGES	16
#define PZK_BAR_RANGE		384	// "*": the opponent is the NPC nearest the player within this

struct pzkChallenge_t
{
	char script[MAX_QPATH];   // without "scripts/" and the extension
	char opponent[64];        // targetname, or "*": the NPC nearest the player
	int wager;
	qboolean asked;           // once per map load
};

static pzkChallenge_t pzkChallenges[PZK_MAX_CHALLENGES];
static int pzkNumChallenges = 0;
static char pzkChallengeMap[MAX_QPATH];  // the map the list was read for ("" = read it again)
static int pzkPendingEnt = -1;           // the held script's entity (-1 = no question open)
static char pzkPendingScript[MAX_QPATH];
static int pzkPendingOpponent = -1;
static int pzkPendingWager = 0;
static qboolean pzkScriptBypass = qfalse; // running the held script ourselves
static qboolean pzkBarMode = qfalse;      // the running match is a bar match (standing, sound on)
static qboolean pzkPendingSignal = qfalse; // the question holds an ICARUS run( ) (answered with the signal)
static qboolean pzkSignalAfterMatch = qfalse; // give the signal when the running match is over
#define PZK_HOLD_SIGNAL	"pazaak_challenge"
static cvar_t* g_pazaakScriptLog;         // 1: print every script that starts

static void Pzk_RunHeldScript(int entNum, const char* script);
// the shop (2026-10-31, at the end of the file)
static void G_Shop_Init();
static qboolean Shop_ClientCommand(gentity_t* ent, const char* cmd);
static qboolean Shop_AskBarChallenge(const gentity_t* npc, int wager);
static const char* Shop_BusyReason(const gentity_t* ent);

void G_Pazaak_Init()
{
	pzkWager = 0;
	pzkOpponent = -1;
	pzkRunning = qfalse;
	pzkStartAt = 0;
	pzkHadGod = qfalse;
	pzkNPCSits = qfalse;
	pzkNPCHadGod = qfalse;
	pzkScriptWager = 0;
	pzkEndScriptEnt = -1;
	pzkEndScript[0] = 0;
	pzkNumChallenges = 0;
	pzkChallengeMap[0] = 0;
	pzkPendingEnt = -1;
	pzkPendingScript[0] = 0;
	pzkPendingOpponent = -1;
	pzkScriptBypass = qfalse;
	pzkBarMode = qfalse;
	pzkPendingSignal = qfalse;
	pzkSignalAfterMatch = qfalse;
	g_pazaakAllowed = gi.cvar("g_pazaakAllowed", "1", CVAR_SAVEGAME);
	g_pazaakScriptLog = gi.cvar("g_pazaakScriptLog", "0", 0);
	G_Shop_Init();
}

static void Pzk_Print(const gentity_t* ent, const char* text)
{
	gi.SendServerCommand(ent->s.number, va("print \"%s\n\"", text));
}

static void Pzk_CenterPrint(const gentity_t* ent, const char* text)
{
	gi.SendServerCommand(ent->s.number, va("cp \"%s\"", text));
}

static const char* Pzk_NPCName(const gentity_t* npc)
{
	if (npc->fullName && npc->fullName[0])
	{
		return npc->fullName;
	}
	if (npc->NPC_type && npc->NPC_type[0])
	{
		return npc->NPC_type;
	}
	return "AI";
}

// A cutscene runs (camera, a script holds the player, or it is being skipped)
static qboolean Pzk_InCutscene()
{
	return in_camera || player_locked || g_skippingcin && g_skippingcin->integer ? qtrue : qfalse;
}

// An enemy who is after the player is close by (the match would make him a sitting, invulnerable target)
static qboolean Pzk_EnemyNear(const gentity_t* ent)
{
	// static: too big for the stack (C6262); filled by EntitiesInBox before it is read
	static gentity_t* list[MAX_GENTITIES];
	vec3_t mins{}, maxs{};

	for (int i = 0; i < 3; i++)
	{
		mins[i] = ent->currentOrigin[i] - PZK_ENEMY_RANGE;
		maxs[i] = ent->currentOrigin[i] + PZK_ENEMY_RANGE;
	}
	const int num = gi.EntitiesInBox(mins, maxs, list, MAX_GENTITIES);
	for (int i = 0; i < num; i++)
	{
		const gentity_t* other = list[i];
		if (other && other != ent && other->client && other->NPC && other->health > 0 && other->enemy == ent
			&& gi.inPVS(other->currentOrigin, ent->currentOrigin))
		{
			return qtrue;
		}
	}
	return qfalse;
}

static const char pzkNoGround[] = "You need solid ground to sit down."; // Pzk_BusyReason; a jump doesn't close the shop

// Why the player can't start a match now (nullptr: he can)
static const char* Pzk_BusyReason(const gentity_t* ent)
{
	const playerState_t* ps = &ent->client->ps;
	const int flags = ps->eFlags | ent->s.eFlags;

	if (Pzk_InCutscene())
	{
		return "A cutscene is playing.";
	}
	if (ent->s.m_iVehicleNum || ps->m_iVehicleNum)
	{
		return "Not while riding a vehicle.";
	}
	if (flags & EF_LOCKED_TO_WEAPON || ps->viewEntity > 0 && ps->viewEntity < ENTITYNUM_WORLD)
	{
		return "Not while controlling something else.";
	}
	if (flags & (EF_HELD_BY_RANCOR | EF_HELD_BY_WAMPA | EF_HELD_BY_SAND_CREATURE | EF_FORCE_GRIPPED | EF_FORCE_DRAINED)
		|| ps->saberLockTime > level.time || PM_InKnockDown(ps))
	{
		return "Not in the middle of a fight.";
	}
	if (ps->groundEntityNum == ENTITYNUM_NONE || ent->waterlevel >= 2)
	{
		return pzkNoGround;
	}
	if (Pzk_EnemyNear(ent))
	{
		return "Enemies are nearby.";
	}
	return nullptr;
}

// On screen, also during a cutscene camera (cgame "pzkcp")
static void Pzk_Unavailable(const gentity_t* ent, const char* reason)
{
	gi.SendServerCommand(ent->s.number, va("pzkcp \"Pazaak is unavailable at this time.\n%s\"", reason ? reason : ""));
}

static qboolean Pzk_CanPlay(const gentity_t* ent)
{
	if (!ent || !ent->client || ent->health <= 0 || ent->client->ps.pm_type == PM_DEAD)
	{
		return qfalse;
	}
	return qtrue;
}

// Turns to face the other one (yaw only), before sitting down: the meditate pose then holds the view
static void Pzk_Face(gentity_t* ent, const gentity_t* other)
{
	vec3_t dir, angles;

	if (!other)
	{
		return;
	}
	VectorSubtract(other->currentOrigin, ent->currentOrigin, dir);
	dir[2] = 0;
	if (VectorLength(dir) < 1.0f)
	{
		return;
	}
	vectoangles(dir, angles);
	angles[PITCH] = 0;
	angles[ROLL] = 0;
	if (ent->NPC)
	{
		ent->NPC->lockedDesiredYaw = ent->NPC->desiredYaw = angles[YAW];
		ent->NPC->lockedDesiredPitch = ent->NPC->desiredPitch = 0;
	}
	SetClientViewAngle(ent, angles);
}

// Saber off, sit down to meditate (g_active.cpp holds the pose while there is no input), invulnerable.
// Returns whether he had god mode before.
static qboolean Pzk_SitDown(gentity_t* ent, const gentity_t* opponent)
{
	Pzk_Face(ent, opponent);
	if (ent->client->ps.SaberActive())
	{
		G_Sound(ent, ent->client->ps.saber[0].soundOff);
		ent->client->ps.SaberDeactivate();
	}
	VectorClear(ent->client->ps.velocity);
	NPC_SetAnim(ent, SETANIM_BOTH, BOTH_MEDITATE, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	const qboolean hadGod = ent->flags & FL_GODMODE ? qtrue : qfalse;
	ent->flags |= FL_GODMODE;
	return hadGod;
}

// Stand up again, no longer invulnerable
static void Pzk_StandUp(gentity_t* ent, const qboolean hadGod)
{
	if (!hadGod)
	{
		ent->flags &= ~FL_GODMODE;
	}
	if (ent->client && ent->client->ps.legsAnim == BOTH_MEDITATE && ent->health > 0)
	{
		NPC_SetAnim(ent, SETANIM_BOTH, BOTH_MEDITATE_END, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	}
}

// How long the sitting down takes (the length of BOTH_MEDITATE, which then holds its last frame)
static int Pzk_SitTime(const gentity_t* ent)
{
	const int t = ent->client->ps.legsAnim == BOTH_MEDITATE ? ent->client->ps.legsAnimTimer : 0;
	return t >= 300 && t <= 4000 ? t : PZK_SIT_TIME;
}

// Whether this NPC can sit at the board: a humanoid with the meditate anim
static qboolean Pzk_NPCCanSit(const gentity_t* npc)
{
	return npc && npc->client && npc->NPC && npc->health > 0 && PM_HasAnimation(npc, BOTH_MEDITATE) ? qtrue : qfalse;
}

// The opponent NPC of the match is sitting there (NPC.cpp lets his AI rest)
qboolean G_Pazaak_IsNPCPlaying(const gentity_t* ent)
{
	return pzkRunning && (pzkNPCSits || pzkBarMode) && ent && ent->s.number == pzkOpponent ? qtrue : qfalse;
}

// npc: the opponent (his name is used; npcSits: he sits down too), or nullptr: the AI as name
static void Pzk_Start(gentity_t* player, gentity_t* npc, const int wager, const qboolean npcSits, const char* name)
{
	Q_strncpyz(pzkName, npc ? Pzk_NPCName(npc) : name && name[0] ? name : "AI", sizeof(pzkName));
	IIcarusInterface* icarus = IIcarusInterface::GetIcarus();
	if (icarus)
	{
		static_cast<CIcarus*>(icarus)->ClearSignal("pazaak_done"); // an old one nobody waited for
	}
	pzkRunning = qtrue;
	pzkOpponent = npc ? npc->s.number : -1;
	pzkWager = wager;
	pzkHadGod = Pzk_SitDown(player, npc);
	int sit = Pzk_SitTime(player);
	pzkNPCSits = npc && npcSits ? qtrue : qfalse;
	if (pzkNPCSits)
	{
		pzkNPCHadGod = Pzk_SitDown(npc, player);
		if (Pzk_SitTime(npc) > sit)
		{
			sit = Pzk_SitTime(npc);
		}
	}
	// The board opens once they sit (G_Pazaak_RunFrame): BOTH_MEDITATE is the sitting down, then it holds
	pzkStartAt = level.time + sit;
}

// Tells the scripts how it went: the global float "pazaak_result" (if declared) and the signal "pazaak_done"
static void Pzk_TellScripts(const int winner)
{
	Quake3Game()->SetDeclaredFloat("pazaak_result", static_cast<float>(winner));
	IIcarusInterface* icarus = IIcarusInterface::GetIcarus();
	if (icarus)
	{
		static_cast<CIcarus*>(icarus)->Signal("pazaak_done");
	}
}

// SET_PAZAAK_END has a script run once the match is over (after pazaak_result is set)
static void Pzk_RunEndScript()
{
	const int entNum = pzkEndScriptEnt;
	char script[MAX_QPATH];

	if (entNum < 0 || !pzkEndScript[0])
	{
		return;
	}
	Q_strncpyz(script, pzkEndScript, sizeof(script));
	pzkEndScriptEnt = -1; // once: the script may set the next one
	pzkEndScript[0] = 0;
	Pzk_RunHeldScript(entNum, script);
}

// Lets a script held by G_Pazaak_HoldRun go on (its sequencer waits on PZK_HOLD_SIGNAL)
static void Pzk_ReleaseHeldRun()
{
	IIcarusInterface* icarus = IIcarusInterface::GetIcarus();
	if (icarus)
	{
		static_cast<CIcarus*>(icarus)->Signal(PZK_HOLD_SIGNAL);
	}
}

// The match is over (winner: 1 = player, 2 = opponent, 0 = called off): stand up, settle the wager
static void Pzk_Result(gentity_t* ent, const int winner)
{
	pzkRunning = qfalse;
	pzkStartAt = 0;
	if (pzkBarMode)
	{// a bar match: he only stood there
		if (!pzkHadGod)
		{
			ent->flags &= ~FL_GODMODE;
		}
		if (ent->client && ent->client->ps.legsAnim == BOTH_STAND1IDLE1)
		{
			ent->client->ps.legsAnimTimer = 0;
			ent->client->ps.torsoAnimTimer = 0;
		}
		pzkBarMode = qfalse;
	}
	else
	{
		Pzk_StandUp(ent, pzkHadGod);
	}
	pzkHadGod = qfalse;
	if (pzkNPCSits && pzkOpponent >= 0 && pzkOpponent < ENTITYNUM_WORLD)
	{
		gentity_t* npc = &g_entities[pzkOpponent];
		if (npc->inuse && npc->client)
		{
			Pzk_StandUp(npc, pzkNPCHadGod);
		}
	}
	pzkNPCSits = qfalse;
	pzkNPCHadGod = qfalse;
	if (ent->client && pzkWager > 0)
	{
		if (winner == 1)
		{
			ent->client->ps.persistant[PERS_CREDITS] += pzkWager * 2;
			Pzk_Print(ent, va("^5Pazaak:^7 you win %i credits.", pzkWager * 2));
		}
		else if (winner == 0)
		{
			ent->client->ps.persistant[PERS_CREDITS] += pzkWager; // called off: money back
		}
		else
		{
			Pzk_Print(ent, va("^5Pazaak:^7 you lose %i credits.", pzkWager));
		}
	}
	else if (winner == 1)
	{
		Pzk_Print(ent, "^5Pazaak:^7 you win the match.");
	}
	else if (winner == 2)
	{
		Pzk_Print(ent, "^5Pazaak:^7 you lose the match.");
	}
	pzkWager = 0;
	pzkOpponent = -1;
	Pzk_TellScripts(winner);
	Pzk_RunEndScript();
	if (pzkSignalAfterMatch)
	{// a bar match asked from inside a script: it goes on now
		pzkSignalAfterMatch = qfalse;
		Pzk_ReleaseHeldRun();
	}
}

// Every frame: open the board once they sit
void G_Pazaak_RunFrame()
{
	if (!pzkStartAt)
	{
		return;
	}
	if (!Pzk_CanPlay(&g_entities[0]))
	{
		// He died while sitting down: no match
		Pzk_Result(&g_entities[0], 0);
		return;
	}
	if (Pzk_InCutscene())
	{
		// A cutscene started while he sat down: no match now
		Pzk_Unavailable(&g_entities[0], "A cutscene is playing.");
		Pzk_Result(&g_entities[0], 0);
		return;
	}
	if (pzkNPCSits && (pzkOpponent < 0 || !Pzk_CanPlay(&g_entities[pzkOpponent])))
	{
		// The opponent died (or went away) while sitting down
		Pzk_Result(&g_entities[0], 0);
		return;
	}
	if (level.time >= pzkStartAt)
	{
		pzkStartAt = 0;
		gi.SendConsoleCommand(va("uipzk_start \"%s\" %i %i %i\n", pzkName, pzkOpponent, pzkWager, pzkBarMode ? 1 : 0)); // 1: no mute
	}
}

// A wager (credits taken now) and the match; qfalse if he can't pay
static qboolean Pzk_TakeWager(gentity_t* player, const int wager)
{
	if (wager <= 0)
	{
		return qtrue;
	}
	if (player->client->ps.persistant[PERS_CREDITS] < wager)
	{
		Pzk_Print(player, va("^5Pazaak:^7 you need %i credits to play.", wager));
		return qfalse;
	}
	player->client->ps.persistant[PERS_CREDITS] -= wager;
	Pzk_Print(player, va("^5Pazaak:^7 you wager %i credits.", wager));
	return qtrue;
}

qboolean G_Pazaak_StartVsNPC(gentity_t* player, gentity_t* npc, const int wager)
{
	if (!Pzk_CanPlay(player) || pzkRunning)
	{
		return qfalse;
	}
	const char* busy = Pzk_BusyReason(player);
	if (busy)
	{
		Pzk_Unavailable(player, busy);
		return qfalse;
	}
	if (!Pzk_TakeWager(player, wager))
	{
		return qfalse;
	}
	Pzk_Start(player, npc, wager > 0 ? wager : 0, Pzk_NPCCanSit(npc), nullptr);
	return qtrue;
}

// SET_PAZAAK_END (Q3_Interface.cpp): entNum runs this script when the next match is over ("NULL" or "": none)
void G_Pazaak_SetEndScript(const int entNum, const char* script)
{
	if (!script || !script[0] || !Q_stricmp(script, "NULL"))
	{
		pzkEndScriptEnt = -1;
		pzkEndScript[0] = 0;
		return;
	}
	pzkEndScriptEnt = entNum;
	Q_strncpyz(pzkEndScript, script, sizeof(pzkEndScript));
}

// SET_PAZAAK_WAGER (Q3_Interface.cpp)
void G_Pazaak_SetScriptWager(const int wager)
{
	pzkScriptWager = wager > 0 ? wager : 0;
}

// SET_PAZAAK_PLAY (Q3_Interface.cpp): a match the script wants, no distance or "enemies near" rules.
// who: the targetname of the opponent NPC, or the name the AI plays under ("" / "AI": nameless).
void G_Pazaak_StartScripted(const char* who)
{
	gentity_t* player = &g_entities[0];
	gentity_t* npc = nullptr;
	const int wager = pzkScriptWager;

	pzkScriptWager = 0;
	if (who && who[0] && Q_stricmp(who, "AI"))
	{
		npc = G_Find(nullptr, FOFS(targetname), who);
		if (npc && (!npc->client || npc->health <= 0))
		{
			npc = nullptr;
		}
	}
	if (!Pzk_CanPlay(player) || pzkRunning)
	{
		Quake3Game()->DebugPrint(IGameInterface::WL_WARNING, "SET_PAZAAK_PLAY: the player can't play now\n");
		Pzk_TellScripts(0);
		Pzk_RunEndScript();
		return;
	}
	if (Pzk_InCutscene())
	{
		Quake3Game()->DebugPrint(IGameInterface::WL_WARNING, "SET_PAZAAK_PLAY: a cutscene is running (camera or player locked), no match\n");
		Pzk_Unavailable(player, "A cutscene is playing.");
		Pzk_TellScripts(0);
		Pzk_RunEndScript();
		return;
	}
	if (!Pzk_TakeWager(player, wager))
	{
		Pzk_TellScripts(0);
		Pzk_RunEndScript();
		return;
	}
	Pzk_Start(player, npc, wager, Pzk_NPCCanSit(npc), who);
}

// ---------------------------------------------------------------------------------------------------------------
// Challenges before a map's own scripts
// ---------------------------------------------------------------------------------------------------------------

// A script name as listed: no "scripts/" (or "scriptsjk2/"), no extension, '/' slashes
static void Pzk_ScriptName(const char* in, char* out, const int size)
{
	char tmp[MAX_QPATH];
	Q_strncpyz(tmp, in, sizeof(tmp));
	for (char* c = tmp; *c; c++)
	{
		if (*c == '\\')
		{
			*c = '/';
		}
	}
	const char* s = tmp;
	if (!Q_stricmpn(s, "scripts/", 8))
	{
		s += 8;
	}
	else if (!Q_stricmpn(s, "scriptsjk2/", 11))
	{
		s += 11; // the JKO scripts (com_outcast)
	}
	Q_strncpyz(out, s, size);
	char* dot = strrchr(out, '.');
	if (dot && !strchr(dot, '/'))
	{
		*dot = 0;
	}
}

// PZK_CHALLENGE_FILE, the lines for this map: <map> <script> [opponent targetname or *] [wager]
static void Pzk_LoadChallenges()
{
	char* buf = nullptr;

	if (pzkChallengeMap[0] && !Q_stricmp(pzkChallengeMap, level.mapname))
	{
		return;
	}
	Q_strncpyz(pzkChallengeMap, level.mapname[0] ? level.mapname : "-", sizeof(pzkChallengeMap));
	pzkNumChallenges = 0;
	const long len = gi.FS_ReadFile(PZK_CHALLENGE_FILE, reinterpret_cast<void**>(&buf));
	if (len <= 0 || !buf)
	{
		return;
	}
	COM_ParseSession session; // COM_ParseExt needs a parse session (ended when this goes out of scope)
	const char* p = buf;
	while (*p && pzkNumChallenges < PZK_MAX_CHALLENGES)
	{
		// one line at a time ("//" comments are skipped by the parser)
		char line[256]{};
		int n = 0;
		while (*p && *p != '\n' && n < static_cast<int>(sizeof(line)) - 1)
		{
			line[n++] = *p++;
		}
		line[n] = 0;
		while (*p && *p != '\n')
		{
			p++; // a line too long: the rest is ignored
		}
		if (*p == '\n')
		{
			p++;
		}

		char map[MAX_QPATH], script[MAX_QPATH], opponent[64];
		const char* l = line;
		Q_strncpyz(map, COM_ParseExt(&l, qfalse), sizeof(map));
		Q_strncpyz(script, COM_ParseExt(&l, qfalse), sizeof(script));
		Q_strncpyz(opponent, COM_ParseExt(&l, qfalse), sizeof(opponent));
		const int wager = atoi(COM_ParseExt(&l, qfalse));
		if (!map[0] || !script[0] || Q_stricmp(map, level.mapname))
		{
			continue;
		}
		pzkChallenge_t* c = &pzkChallenges[pzkNumChallenges++];
		Pzk_ScriptName(script, c->script, sizeof(c->script));
		Q_strncpyz(c->opponent, opponent[0] ? opponent : "*", sizeof(c->opponent));
		c->wager = wager > 0 ? wager : 0;
		c->asked = qfalse;
	}
	gi.FS_FreeFile(buf);
}

// The opponent of a challenge: the named NPC, or ("*") the script's own NPC, else the living NPC nearest the
// player (not after him) within PZK_BAR_RANGE
static gentity_t* Pzk_ChallengeOpponent(const pzkChallenge_t* c, const gentity_t* owner, const gentity_t* player)
{
	if (c->opponent[0] && Q_stricmp(c->opponent, "*"))
	{
		gentity_t* npc = G_Find(nullptr, FOFS(targetname), c->opponent);
		return npc && npc->client && npc->health > 0 ? npc : nullptr;
	}
	if (owner && owner->client && owner->NPC && owner->health > 0)
	{
		return const_cast<gentity_t*>(owner);
	}
	gentity_t* best = nullptr;
	float bestDist = PZK_BAR_RANGE;
	for (int i = 1; i < globals.num_entities; i++)
	{
		gentity_t* ent = &g_entities[i];
		if (!ent->inuse || !ent->client || !ent->NPC || ent->health <= 0 || ent->enemy == player)
		{
			continue;
		}
		const float dist = Distance(ent->currentOrigin, player->currentOrigin);
		if (dist < bestDist)
		{
			bestDist = dist;
			best = ent;
		}
	}
	return best;
}

// Runs the held script ourselves (not held back again)
static void Pzk_RunHeldScript(const int entNum, const char* script)
{
	if (entNum < 0 || entNum >= ENTITYNUM_WORLD || !script[0] || !g_entities[entNum].inuse)
	{
		return;
	}
	pzkScriptBypass = qtrue;
	Quake3Game()->RunScript(&g_entities[entNum], script);
	pzkScriptBypass = qfalse;
}

// A listed script is about to start: ask the question (qtrue: hold it). bySignal: it is an ICARUS run( ),
// answered with the signal; else a RunScript, run again after the answer
static qboolean Pzk_TryChallenge(const gentity_t* ent, const char* script, const qboolean bySignal)
{
	char name[MAX_QPATH];

	if (pzkScriptBypass || !ent || !script || !script[0])
	{
		return qfalse;
	}
	Pzk_ScriptName(script, name, sizeof(name));
	if (g_pazaakScriptLog && g_pazaakScriptLog->integer)
	{
		Com_Printf(S_COLOR_CYAN "Pazaak script log:" S_COLOR_WHITE " %s %s runs \"%s\" (map %s)\n", ent->classname ? ent->classname : "?",
			ent->targetname ? ent->targetname : "", name, level.mapname);
	}
	Pzk_LoadChallenges();
	if (!pzkNumChallenges || pzkPendingEnt >= 0 || pzkRunning)
	{
		return qfalse;
	}
	for (int i = 0; i < pzkNumChallenges; i++)
	{
		pzkChallenge_t* c = &pzkChallenges[i];
		if (c->asked || Q_stricmp(c->script, name))
		{
			continue;
		}
		c->asked = qtrue;
		gentity_t* player = &g_entities[0];
		if (!Pzk_CanPlay(player) || Pzk_InCutscene() || g_pazaakAllowed && !g_pazaakAllowed->integer)
		{
			return qfalse; // no question now: the script runs as normal
		}
		const gentity_t* npc = Pzk_ChallengeOpponent(c, ent, player);
		pzkPendingEnt = ent->s.number;
		Q_strncpyz(pzkPendingScript, script, sizeof(pzkPendingScript));
		pzkPendingOpponent = npc ? npc->s.number : -1;
		pzkPendingWager = c->wager;
		pzkPendingSignal = bySignal;
		if (bySignal)
		{
			IIcarusInterface* icarus = IIcarusInterface::GetIcarus();
			if (icarus)
			{
				static_cast<CIcarus*>(icarus)->ClearSignal(PZK_HOLD_SIGNAL); // an old one
			}
		}

		if (Shop_AskBarChallenge(npc, c->wager))
		{
			return qtrue; // DH: the bar asks pazaak / buy items / leave, like talking to anyone (g_shop 0: yes / no)
		}
		char who[64];
		Q_strncpyz(who, npc && npc->fullName && npc->fullName[0] ? npc->fullName : "this character", sizeof(who));
		for (char* q = who; *q; q++)
		{
			if (*q == '"')
			{
				*q = '\'';
			}
		}
		const char* text = c->wager > 0
			? va("Do you want to challenge %s to a pazaak match for %i credits?", who, c->wager)
			: va("Do you want to challenge %s to a pazaak match?", who);
		gi.SendConsoleCommand(va("uipzk_challenge \"%s\"\n", text));
		return qtrue;
	}
	return qfalse;
}

// Q3_Interface.cpp RunScript: qtrue = held back for the challenge question (it runs after the answer)
qboolean G_Pazaak_InterceptScript(const gentity_t* ent, const char* script)
{
	return Pzk_TryChallenge(ent, script, qfalse);
}

// icarus/Sequencer.cpp CheckRun, a run( ) inside a running script of entity entNum: true = the sequencer waits on
// the signal PZK_HOLD_SIGNAL first (given after the answer, or after the match)
bool G_Pazaak_HoldRun(const int entNum, const char* script)
{
	if (entNum < 0 || entNum >= ENTITYNUM_WORLD || !g_entities[entNum].inuse)
	{
		return false;
	}
	return Pzk_TryChallenge(&g_entities[entNum], script, qtrue) ? true : false;
}

// A bar match: standing, the opponent stays as he is, sound on (the UI); qfalse if it can't start
static qboolean Pzk_StartBar(gentity_t* player, gentity_t* npc, const int wager)
{
	if (!Pzk_CanPlay(player) || pzkRunning || Pzk_InCutscene())
	{
		return qfalse;
	}
	if (!Pzk_TakeWager(player, wager))
	{
		return qfalse;
	}
	Q_strncpyz(pzkName, npc ? Pzk_NPCName(npc) : "AI", sizeof(pzkName));
	IIcarusInterface* icarus = IIcarusInterface::GetIcarus();
	if (icarus)
	{
		static_cast<CIcarus*>(icarus)->ClearSignal("pazaak_done");
	}
	pzkRunning = qtrue;
	pzkBarMode = qtrue;
	pzkOpponent = npc ? npc->s.number : -1;
	pzkWager = wager;
	pzkNPCSits = qfalse;
	pzkNPCHadGod = qfalse;
	Pzk_Face(player, npc);
	if (player->client->ps.SaberActive())
	{
		G_Sound(player, player->client->ps.saber[0].soundOff);
		player->client->ps.SaberDeactivate();
	}
	VectorClear(player->client->ps.velocity);
	NPC_SetAnim(player, SETANIM_BOTH, BOTH_STAND1IDLE1, SETANIM_FLAG_OVERRIDE | SETANIM_FLAG_HOLD);
	pzkHadGod = player->flags & FL_GODMODE ? qtrue : qfalse;
	player->flags |= FL_GODMODE;
	pzkStartAt = level.time + 100; // no sitting down: the board opens straight away (G_Pazaak_RunFrame)
	return qtrue;
}

// "pazaak_challenge 1/0": the answer to the question (ui_pazaak.cpp)
static void Pzk_ChallengeAnswer(gentity_t* player, const qboolean yes)
{
	char script[MAX_QPATH];

	if (pzkPendingEnt < 0)
	{
		return;
	}
	const int owner = pzkPendingEnt;
	Q_strncpyz(script, pzkPendingScript, sizeof(script));
	const int opponent = pzkPendingOpponent;
	const int wager = pzkPendingWager;
	const qboolean bySignal = pzkPendingSignal;
	pzkPendingSignal = qfalse;
	pzkPendingEnt = -1;
	pzkPendingScript[0] = 0;
	pzkPendingOpponent = -1;
	pzkPendingWager = 0;

	if (yes)
	{
		gentity_t* npc = opponent >= 0 && opponent < ENTITYNUM_WORLD ? &g_entities[opponent] : nullptr;
		if (npc && (!npc->inuse || !npc->client || npc->health <= 0))
		{
			npc = nullptr;
		}
		if (Pzk_StartBar(player, npc, wager))
		{
			if (bySignal)
			{
				pzkSignalAfterMatch = qtrue; // the held script goes on when the match is over
			}
			else
			{
				G_Pazaak_SetEndScript(owner, script); // the held script runs when the match is over
			}
			return;
		}
		Pzk_Unavailable(player, "");
	}
	if (bySignal)
	{
		Pzk_ReleaseHeldRun();
	}
	else
	{
		Pzk_RunHeldScript(owner, script);
	}
}

// The entity in the player's crosshair (up to PZK_LOOK_RANGE away)
static gentity_t* Pzk_LookTarget(const gentity_t* ent)
{
	vec3_t start, end, fwd;
	trace_t tr;

	VectorCopy(ent->client->ps.origin, start);
	start[2] += ent->client->ps.viewheight;
	AngleVectors(ent->client->ps.viewangles, fwd, nullptr, nullptr);
	VectorMA(start, PZK_LOOK_RANGE, fwd, end);
	gi.trace(&tr, start, nullptr, nullptr, end, ent->s.number, MASK_SHOT, static_cast<EG2_Collision>(0), 0);
	if (tr.entityNum >= ENTITYNUM_WORLD || tr.entityNum == ent->s.number)
	{
		return nullptr;
	}
	return &g_entities[tr.entityNum];
}

// Why this NPC won't sit down with the player now (nullptr: he will)
static const char* Pzk_NPCBusy(const gentity_t* npc, const gentity_t* player)
{
	if (npc->enemy == player || npc->client->playerTeam == TEAM_ENEMY)
	{
		return "refuses to play Pazaak with you.";
	}
	if (npc->enemy && npc->enemy->health > 0)
	{
		return "is busy fighting and cannot respond.";
	}
	if (npc->client->ps.groundEntityNum == ENTITYNUM_NONE || npc->waterlevel >= 2 || npc->client->ps.m_iVehicleNum
		|| npc->svFlags & SVF_ICARUS_FREEZE || player->client->ps.viewEntity == npc->s.number)
	{
		return "is busy and cannot respond.";
	}
	return nullptr;
}

qboolean G_Pazaak_ClientCommand(gentity_t* ent, const char* cmd)
{
	if (Shop_ClientCommand(ent, cmd))
	{
		return qtrue; // shop, shopbuy, shoptalk, shopcredits
	}
	if (!Q_stricmp(cmd, "pazaak"))
	{
		if (!Pzk_CanPlay(ent))
		{
			Pzk_Unavailable(ent, "");
			return qtrue;
		}
		if (pzkRunning)
		{
			return qtrue; // already sitting down for one
		}
		if (g_pazaakAllowed && !g_pazaakAllowed->integer)
		{
			Pzk_Unavailable(ent, ""); // a script switched it off (SET_PAZAAK_ALLOWED)
			return qtrue;
		}
		const char* busy = Pzk_BusyReason(ent);
		if (busy)
		{
			Pzk_Unavailable(ent, busy);
			return qtrue;
		}
		gentity_t* target = Pzk_LookTarget(ent);
		if (target && target->client && target->NPC && target->health > 0)
		{
			// 2. An NPC: challenge him
			const char* name = Pzk_NPCName(target);
			if (Distance(ent->currentOrigin, target->currentOrigin) > PZK_CHALLENGE_RANGE)
			{
				Pzk_CenterPrint(ent, va("%s^7 is too far away.\nYou must be closer to challenge someone to Pazaak.", name));
				return qtrue;
			}
			const char* npcBusy = Pzk_NPCBusy(target, ent);
			if (npcBusy)
			{
				Pzk_CenterPrint(ent, va("You have challenged %s^7 to a Pazaak game.\n%s^7 %s", name, name, npcBusy));
				Pzk_Print(ent, va("^5Pazaak:^7 %s^7 %s", name, npcBusy));
				return qtrue;
			}
			Pzk_CenterPrint(ent, va("You have challenged %s^7 to a Pazaak game.\n%s^7 accepts.", name, name));
			Pzk_Print(ent, va("^5Pazaak:^7 You have challenged %s^7 to a Pazaak game. %s^7 accepts.", name, name));
			Pzk_Start(ent, target, 0, Pzk_NPCCanSit(target), nullptr);
			return qtrue;
		}
		// 1. Nobody: the AI
		Pzk_Start(ent, nullptr, 0, qfalse, nullptr);
		return qtrue;
	}
	if (!Q_stricmp(cmd, "pazaak_challenge"))
	{
		// pazaak_challenge <1 = yes, 0 = no>: the answer to the question before a map's script
		Pzk_ChallengeAnswer(ent, atoi(gi.argv(1)) ? qtrue : qfalse);
		return qtrue;
	}
	if (!Q_stricmp(cmd, "pazaak_result"))
	{
		// pazaak_result <winner: 1 = player, 2 = opponent, 0 = called off> [unavailable]
		const int winner = atoi(gi.argv(1));
		if (!pzkRunning)
		{
			return qtrue;
		}
		if (!Q_stricmp(gi.argv(2), "unavailable"))
		{
			Pzk_Unavailable(ent, "A video is playing."); // it started while he sat down
		}
		Pzk_Result(ent, winner);
		return qtrue;
	}
	return qfalse;
}

/*
===========================================================================
The shop (2026-10-31, DH: buy the SJE guns like pazaak is played; SJE SP + MP, MP twin in codemp/game/g_pazaak.c)

Credits: ps.persistant[PERS_CREDITS] (the pazaak wagers use them too). A new game starts with g_shopStartCredits
(1000). Kills earn them like the award streaks: an enemy killed by the player gives g_shopKillCredits (25) times the
kills in a row (each within g_shopStreakTime ms of the last, up to g_shopStreakMax times), double for a saber user;
dying costs g_shopDeathLoss percent. Saved with the game (pers) and carried over level changes (the cvar
sje_playersave_shop, like sje_playersave_weapons2: the exe's playersave string has no room for it).

What is bought: the gun looks (weapon variants, ext_data/sje_weaponvariants.dat, "price"): the first buy gives the
base gun with ammo and the look, an owned look is put on again for free; grenade types: a pack of 3 thermals of that
type (owning the type is what lets the "grenade" command pick it); an ammo refill; a bacta canister. Guns picked up
from dead enemies carry their look (the dropped item's s.weaponVariant): picking one up owns it too.

The menu runs in the UI (ui_pazaak.cpp shop code): "shop" (the Shop menu key) opens it with "uishop open ...", the
UI answers with "shopbuy <variant | SHOP_ITEM_AMMO | SHOP_ITEM_BACTA>" and gets "uishop state ..." back. The world is
paused while it is open.

Talking (DH): using a friendly NPC (who shows the use icon, G_Shop_CanTalk; NPCs a script made usable keep their own
use) asks, in the pazaak challenge style, "Challenge to pazaak" / "Buy items" / "Leave": "uishop talk ...", answered
with "shoptalk 1 / 2 / 0". g_npcTalk 0 = the old use.
===========================================================================
*/

extern cvar_t* g_cheats;
extern cvar_t* g_weaponVariants;
extern void CG_ChangeWeapon(int num);
extern void G_CreateG2AttachedWeaponModel(gentity_t* ent, const char* ps_weapon_model, int bolt_num, int weapon_num);
extern void Add_Ammo(const gentity_t* ent, int weapon, int count);

static cvar_t* g_shop;              // 0 = no shop and no credits for kills
static cvar_t* g_shopStartCredits;
static cvar_t* g_shopKillCredits;
static cvar_t* g_shopStreakTime;
static cvar_t* g_shopStreakMax;
static cvar_t* g_shopDeathLoss;     // percent of the credits lost on dying
static cvar_t* g_shopAmmoPrice;
static cvar_t* g_shopBactaPrice;
static cvar_t* g_npcTalk;           // using a friendly NPC asks pazaak / shop
static int shopTalkNPC = -1;        // the NPC of the open talk question (-1 = none)
static qboolean shopIsOpen = qfalse; // the shop menu is open (the world runs on: it closes when he is attacked)
static int shopOpenHealth = 0;
static int shopNextCheck = 0;
static qboolean shopTalkBar = qfalse; // the open talk question is a bar challenge (Pzk_TryChallenge)
static qboolean shopBarHeld = qfalse; // the bar's held script waits until the shop menu is closed
static int shopTalkFrom = 0;          // nobody can be talked to before this time (kejim_post: Jan, vjun1: Kyle, the first 30 s)

#define SHOP_MAX_CREDITS	9999999
#define SHOP_PACK			3		// thermals in a grenade pack

// ---------------------------------------------------------------------------------------------------------------
// Holdable items in the shop (2026-11-02, DH): the "Items" tab, bought like a pickup (up to the pickup limit).
// id = SHOP_ITEM_HOLD + index; one price cvar each. The UI gets "price:count:max,..." in this table's order
// (ui_pazaak.cpp shopHoldNames). Bacta stays on the Supplies tab.
// ---------------------------------------------------------------------------------------------------------------
#define SHOP_ITEM_HOLD		1100

typedef struct
{
	const char* cvarName;
	const char* defPrice;
	int inv; // INV_*
	int max;
	const char* name;
	cvar_t* price;
} shopHoldable_t;

static shopHoldable_t shopHoldables[] = {
	{ "g_shopPriceBinoculars", "100", INV_ELECTROBINOCULARS, 1, "electrobinoculars", nullptr },
	{ "g_shopPriceGoggles", "150", INV_LIGHTAMP_GOGGLES, 1, "light amp goggles", nullptr },
	{ "g_shopPriceSeeker", "400", INV_SEEKER, 5, "seeker drone", nullptr },
	{ "g_shopPriceSentry", "500", INV_SENTRY, 5, "sentry gun", nullptr },
};
constexpr int SHOP_NUM_HOLDABLES = static_cast<int>(sizeof shopHoldables / sizeof shopHoldables[0]);

static int Shop_HoldPrice(const int i)
{
	return shopHoldables[i].price && shopHoldables[i].price->integer > 0 ? shopHoldables[i].price->integer : 0;
}

// "price:count:max,..." for the UI's Items tab
static const char* Shop_HoldList(const gentity_t* ent)
{
	static char list[256];
	list[0] = 0;
	for (int i = 0; i < SHOP_NUM_HOLDABLES; i++)
	{
		char one[32];
		Com_sprintf(one, sizeof one, "%s%i:%i:%i", i ? "," : "", Shop_HoldPrice(i),
			ent->client->ps.inventory[shopHoldables[i].inv], shopHoldables[i].max);
		Q_strcat(list, sizeof list, one);
	}
	return list;
}

static void G_Shop_Init()
{
	g_shop = gi.cvar("g_shop", "1", CVAR_ARCHIVE);
	g_shopStartCredits = gi.cvar("g_shopStartCredits", "1000", CVAR_ARCHIVE);
	g_shopKillCredits = gi.cvar("g_shopKillCredits", "25", CVAR_ARCHIVE);
	g_shopStreakTime = gi.cvar("g_shopStreakTime", "5000", CVAR_ARCHIVE);
	g_shopStreakMax = gi.cvar("g_shopStreakMax", "5", CVAR_ARCHIVE);
	g_shopDeathLoss = gi.cvar("g_shopDeathLoss", "10", CVAR_ARCHIVE);
	g_shopAmmoPrice = gi.cvar("g_shopAmmoPrice", "150", CVAR_ARCHIVE);
	g_shopBactaPrice = gi.cvar("g_shopBactaPrice", "200", CVAR_ARCHIVE);
	for (auto& h : shopHoldables)
	{
		h.price = gi.cvar(h.cvarName, h.defPrice, CVAR_ARCHIVE);
	}
	g_npcTalk = gi.cvar("g_npcTalk", "1", CVAR_ARCHIVE);
	shopTalkNPC = -1;
	shopIsOpen = qfalse;
	shopTalkBar = qfalse;
	shopBarHeld = qfalse;
	// DH: on kejim_post Jan can't be talked to for the first 30 seconds (the opening)
	// DH 2026-11-01: vjun1 too (Kyle stands in front of the player after the opening cutscene)
	shopTalkFrom = !Q_stricmp(level.mapname, "kejim_post") || !Q_stricmp(level.mapname, "vjun1") ? level.time + 30000 : 0;
}

// The shop menu of a bar challenge closed: the held script goes on now
static void Shop_ReleaseBar(gentity_t* ent)
{
	if (shopBarHeld)
	{
		shopBarHeld = qfalse;
		Pzk_ChallengeAnswer(ent, qfalse);
	}
}

// g_client.cpp: the credits of a new game
int G_Shop_StartCredits()
{
	return g_shopStartCredits && g_shopStartCredits->integer > 0 ? g_shopStartCredits->integer : 0;
}

static void Shop_Print(const gentity_t* ent, const char* text)
{
	gi.SendServerCommand(ent->s.number, va("print \"%s\n\"", text));
}

static qboolean Shop_Owns(const gclient_t* cl, const int v)
{
	return v > 0 && v < MAX_WEAPON_VARIANTS && cl->pers.shopOwned[v >> 5] & 1u << (v & 31) ? qtrue : qfalse;
}

// g_cmds.cpp (grenade): the player owns this variant (any, with g_shop 0)
qboolean G_Shop_Owns(const gentity_t* ent, const int variant)
{
	if (!ent || !ent->client)
	{
		return qfalse;
	}
	return !g_shop->integer || Shop_Owns(ent->client, variant) ? qtrue : qfalse;
}

// Owns it and uses it on its weapon (grenades: the type he throws). Also the weaponvariant cheat (g_cmds.cpp).
void G_Shop_GiveVariant(gentity_t* ent, const int v)
{
	if (!ent || !ent->client || v <= 0 || v >= bgNumWeaponVariants)
	{
		return;
	}
	gclient_t* cl = ent->client;
	const int weapon = bgWeaponVariants[v].baseWeapon;
	cl->pers.shopOwned[v >> 5] |= 1u << (v & 31);
	if (weapon == WP_THERMAL)
	{
		ent->grenadeKind = v;
	}
	else if (weapon > WP_NONE && weapon < SHOP_MAX_WEAPONS)
	{
		cl->pers.shopEquip[weapon] = v;
	}
}

// The weaponvariant cheat with 0: the stock look on this weapon
void G_Shop_EquipStock(gentity_t* ent, const int weapon)
{
	if (!ent || !ent->client)
	{
		return;
	}
	if (weapon == WP_THERMAL)
	{
		ent->grenadeKind = 0;
	}
	else
	{
		const int family = BG_WeaponVariantFamily(weapon);
		if (family > WP_NONE && family < SHOP_MAX_WEAPONS)
		{
			ent->client->pers.shopEquip[family] = 0;
		}
	}
}

static void Shop_AddCredits(gentity_t* ent, const int n)
{
	int* c = &ent->client->ps.persistant[PERS_CREDITS];
	*c += n;
	if (*c < 0)
	{
		*c = 0;
	}
	else if (*c > SHOP_MAX_CREDITS)
	{
		*c = SHOP_MAX_CREDITS;
	}
}

// The level change keeps it all in sje_playersave_shop (read by G_Shop_RestoreFromPrevLevel)
static void Shop_KeepForNextLevel(const gentity_t* ent)
{
	static unsigned int lastSum = 0;
	const gclient_t* cl = ent->client;
	unsigned int sum = 2166136261u;

	sum = (sum ^ static_cast<unsigned int>(cl->ps.persistant[PERS_CREDITS])) * 16777619u;
	sum = (sum ^ static_cast<unsigned int>(ent->grenadeKind)) * 16777619u;
	for (int i = 0; i < SHOP_OWNED_WORDS; i++)
	{
		sum = (sum ^ static_cast<unsigned int>(cl->pers.shopOwned[i])) * 16777619u;
	}
	for (int i = 0; i < SHOP_MAX_WEAPONS; i++)
	{
		sum = (sum ^ static_cast<unsigned int>(cl->pers.shopEquip[i])) * 16777619u;
	}
	if (sum == lastSum)
	{
		return;
	}
	lastSum = sum;

	char s[MAX_STRING_CHARS];
	Com_sprintf(s, sizeof s, "%i %i", cl->ps.persistant[PERS_CREDITS], ent->grenadeKind);
	for (int i = 0; i < SHOP_OWNED_WORDS; i++)
	{
		Q_strcat(s, sizeof s, va(" %i", cl->pers.shopOwned[i]));
	}
	for (int i = 0; i < SHOP_MAX_WEAPONS; i++)
	{
		Q_strcat(s, sizeof s, cl->pers.shopEquip[i] ? va(" %i", cl->pers.shopEquip[i]) : " 0");
	}
	gi.cvar_set("sje_playersave_shop", s);
}

// g_client.cpp Player_RestoreFromPrevLevel: a level change, the shop state of the last level
void G_Shop_RestoreFromPrevLevel(gentity_t* ent)
{
	char s[MAX_STRING_CHARS];
	int vals[2 + SHOP_OWNED_WORDS + SHOP_MAX_WEAPONS];
	int n = 0;

	if (!ent || !ent->client)
	{
		return;
	}
	gi.Cvar_VariableStringBuffer("sje_playersave_shop", s, sizeof s);
	for (const char* tok = strtok(s, " "); tok && n < static_cast<int>(ARRAY_LEN(vals)); tok = strtok(nullptr, " "))
	{
		vals[n++] = atoi(tok);
	}
	if (n != static_cast<int>(ARRAY_LEN(vals)))
	{
		return; // nothing kept (or an old format): the level's own start
	}
	gclient_t* cl = ent->client;
	cl->ps.persistant[PERS_CREDITS] = vals[0] < 0 ? 0 : vals[0];
	ent->grenadeKind = vals[1];
	for (int i = 0; i < SHOP_OWNED_WORDS; i++)
	{
		cl->pers.shopOwned[i] = vals[2 + i];
	}
	for (int i = 0; i < SHOP_MAX_WEAPONS; i++)
	{
		cl->pers.shopEquip[i] = vals[2 + SHOP_OWNED_WORDS + i];
	}
	cl->pers.shopInit = 1;
}

// g_weapon.cpp G_WeaponVariantThink, every frame of the player: the owned look on the gun in hand
void G_Shop_PlayerThink(gentity_t* ent)
{
	gclient_t* cl = ent->client;

	if (!cl)
	{
		return;
	}
	if (!cl->pers.shopInit)
	{
		cl->pers.shopInit = 1; // g_client.cpp gave the starting credits
	}

	const int weapon = cl->ps.weapon;
	int want = 0;
	if (g_weaponVariants->integer && bgNumWeaponVariants > 1 && weapon > WP_NONE && weapon != WP_SABER)
	{
		if (weapon == WP_THERMAL)
		{
			want = ent->grenadeKind;
			if (!BG_GetWeaponVariant(want, WP_THERMAL) || !G_Shop_Owns(ent, want))
			{
				want = 0;
			}
		}
		else
		{
			const int family = BG_WeaponVariantFamily(weapon);
			want = family > WP_NONE && family < SHOP_MAX_WEAPONS ? cl->pers.shopEquip[family] : 0;
			if (!BG_GetWeaponVariant(want, weapon))
			{
				want = 0;
			}
		}
	}
	if (cl->ps.weaponVariant != want)
	{
		cl->ps.weaponVariant = want;
		if (ent->weaponModel[0] > 0 && ent->ghoul2.size() && weapon > WP_NONE && weapon != WP_SABER)
		{
			// the gun in the hand changes to the look (or back to the stock one)
			gi.G2API_RemoveGhoul2Model(ent->ghoul2, ent->weaponModel[0]);
			ent->weaponModel[0] = -1;
			G_CreateG2AttachedWeaponModel(ent, weaponData[weapon].weaponMdl, ent->handRBolt, 0);
		}
	}
	Shop_KeepForNextLevel(ent);

	// the shop menu is open and the world runs on: it closes when he gets hurt, or as soon as the pazaak rules would
	// not let it open (an enemy comes for him, a fight, a cutscene...; a jump doesn't count)
	if (shopIsOpen && level.time >= shopNextCheck)
	{
		shopNextCheck = level.time + 500;
		const char* busy = Shop_BusyReason(ent);
		if (busy == pzkNoGround)
		{
			busy = nullptr;
		}
		if (ent->health <= 0 || ent->health < shopOpenHealth || busy)
		{
			shopIsOpen = qfalse;
			gi.SendConsoleCommand("uishop close\n");
			gi.SendServerCommand(ent->s.number, ent->health > 0 && ent->health >= shopOpenHealth && busy && busy[0]
				? va("cp \"The shop closed.\n%s\"", busy) : "cp \"The shop closed: you are under attack.\"");
			Shop_ReleaseBar(ent);
		}
		shopOpenHealth = ent->health;
	}
}

// cg_holster.cpp: the look (weapon variant) of a gun the player carries but doesn't hold, the one he owns and uses on
// that weapon (0 = the stock gun)
int G_Shop_HolsterLook(const gentity_t* ent, const int weapon)
{
	if (!ent || ent->s.number != 0 || !ent->client || !g_weaponVariants || !g_weaponVariants->integer
		|| bgNumWeaponVariants <= 1 || weapon <= WP_NONE || weapon == WP_SABER || weapon == WP_THERMAL)
	{
		return 0;
	}
	const int family = BG_WeaponVariantFamily(weapon);
	const int look = family > WP_NONE && family < SHOP_MAX_WEAPONS ? ent->client->pers.shopEquip[family] : 0;
	return BG_GetWeaponVariant(look, weapon) ? look : 0;
}

// g_combat.cpp player_die: credits for the player's kills, the loss when he dies
void G_Shop_Died(gentity_t* victim, gentity_t* attacker)
{
	if (!g_shop || !g_shop->integer || !victim || !victim->client)
	{
		return;
	}
	gentity_t* player = &g_entities[0];
	if (!player->client)
	{
		return;
	}
	if (victim == player)
	{
		const int loss = player->client->ps.persistant[PERS_CREDITS] * (g_shopDeathLoss->integer > 0 ? g_shopDeathLoss->integer : 0) / 100;
		player->client->pers.shopStreak = 0;
		if (loss > 0)
		{
			Shop_AddCredits(player, -loss);
			Shop_Print(player, va("^1-%i credits^7 (killed)", loss));
		}
		return;
	}
	if (attacker && !attacker->client && attacker->owner == player)
	{
		attacker = player; // his turret, mine...
	}
	if (attacker != player || victim->client->NPC_class == CLASS_VEHICLE || g_shopKillCredits->integer <= 0)
	{
		return;
	}
	const team_t team = victim->client->playerTeam;
	if ((team == player->client->playerTeam || team == TEAM_NEUTRAL) && victim->enemy != player)
	{
		return; // friends and bystanders earn nothing
	}

	gclient_t* cl = player->client;
	const int max = g_shopStreakMax->integer > 1 ? g_shopStreakMax->integer : 1;
	if (cl->pers.shopStreak > 0 && level.time - cl->pers.shopStreakTime <= g_shopStreakTime->integer
		&& level.time >= cl->pers.shopStreakTime)
	{
		cl->pers.shopStreak++;
	}
	else
	{
		cl->pers.shopStreak = 1;
	}
	cl->pers.shopStreakTime = level.time;

	const int streak = cl->pers.shopStreak < max ? cl->pers.shopStreak : max;
	int reward = g_shopKillCredits->integer * streak;
	if (BG_HasWeapon(&victim->client->ps, WP_SABER))
	{
		reward *= 2; // a saber user
	}
	Shop_AddCredits(player, reward);
	if (cl->pers.shopStreak > 1)
	{
		Shop_Print(player, va("^3+%i credits^7 (%i kills in a row)", reward, cl->pers.shopStreak));
	}
	else
	{
		Shop_Print(player, va("^3+%i credits", reward));
	}
}

// Why the player can't shop now (nullptr: he can): the pazaak rules (DH 2026-11-02: the shop's are the same)
static const char* Shop_BusyReason(const gentity_t* ent)
{
	if (!Pzk_CanPlay(ent))
	{
		return "";
	}
	if (pzkRunning)
	{
		return "You are playing pazaak.";
	}
	return Pzk_BusyReason(ent);
}

// What the UI shows: "uishop <what> <credits> <owned bits> <equipped bits> "<message>"" (64 hex digits each)
static void Shop_Send(const gentity_t* ent, const char* what, const char* message)
{
	const gclient_t* cl = ent->client;
	unsigned int equipped[SHOP_OWNED_WORDS] = {};
	char owned[SHOP_OWNED_WORDS * 8 + 1], equip[SHOP_OWNED_WORDS * 8 + 1];
	char msg[128];

	// first: the caller's message is often a va() string, and the va() calls below write over it
	Q_strncpyz(msg, message ? message : "", sizeof msg);
	for (int i = 0; i < SHOP_MAX_WEAPONS; i++)
	{
		const int v = cl->pers.shopEquip[i];
		if (v > 0 && v < MAX_WEAPON_VARIANTS)
		{
			equipped[v >> 5] |= 1u << (v & 31);
		}
	}
	if (ent->grenadeKind > 0 && ent->grenadeKind < MAX_WEAPON_VARIANTS)
	{
		equipped[ent->grenadeKind >> 5] |= 1u << (ent->grenadeKind & 31);
	}
	owned[0] = equip[0] = 0;
	for (int i = 0; i < SHOP_OWNED_WORDS; i++)
	{
		Q_strcat(owned, sizeof owned, va("%08x", static_cast<unsigned int>(cl->pers.shopOwned[i])));
		Q_strcat(equip, sizeof equip, va("%08x", equipped[i]));
	}
	for (char* q = msg; *q; q++)
	{
		if (*q == '"')
		{
			*q = '\'';
		}
	}
	gi.SendConsoleCommand(va("uishop %s %i %s %s %i %i \"%s\" \"%s\"\n", what, cl->ps.persistant[PERS_CREDITS], owned, equip,
		g_shopAmmoPrice->integer > 0 ? g_shopAmmoPrice->integer : 0, g_shopBactaPrice->integer > 0 ? g_shopBactaPrice->integer : 0, msg, Shop_HoldList(ent)));
}

static void Shop_Open(gentity_t* ent, const char* message)
{
	if (!g_shop->integer)
	{
		Shop_Print(ent, "The shop is switched off (g_shop 0).");
		return;
	}
	const char* busy = Shop_BusyReason(ent);
	if (busy)
	{
		gi.SendServerCommand(ent->s.number, va("cp \"The shop is unavailable at this time.\n%s\"", busy));
		return;
	}
	Shop_Send(ent, "open", message);
	shopIsOpen = qtrue;
	shopOpenHealth = ent->health;
	shopNextCheck = level.time + 500;
}

// The ammo of every gun he carries, to the full (qfalse: nothing to fill)
static qboolean Shop_FillAmmo(const gentity_t* ent)
{
	qboolean filled = qfalse;
	playerState_t* ps = &ent->client->ps;

	for (int w = WP_NONE + 1; w < WP_NUM_WEAPONS; w++)
	{
		if (w == WP_SABER || w == WP_MELEE || w == WP_STUN_BATON || !BG_HasWeapon(ps, w))
		{
			continue;
		}
		const int a = weaponData[w].ammoIndex;
		if (a <= AMMO_NONE || a >= AMMO_MAX || a == AMMO_FORCE)
		{
			continue;
		}
		if (ps->ammo[a] < ammoData[a].max)
		{
			ps->ammo[a] = ammoData[a].max;
			filled = qtrue;
		}
	}
	return filled;
}

// A holdable item from the Items tab (index into shopHoldables)
static void Shop_BuyHoldable(gentity_t* ent, const int i)
{
	gclient_t* cl = ent->client;
	const shopHoldable_t* h = &shopHoldables[i];
	const int price = Shop_HoldPrice(i);

	if (cl->ps.persistant[PERS_CREDITS] < price)
	{
		Shop_Send(ent, "state", va("You need %i credits for the %s.", price, h->name));
		return;
	}
	if (cl->ps.inventory[h->inv] >= h->max)
	{
		Shop_Send(ent, "state", h->max > 1 ? va("You can't carry more than %i.", h->max) : "You already have one.");
		return;
	}
	cl->ps.stats[STAT_ITEMS] |= 1 << h->inv;
	cl->ps.inventory[h->inv]++;
	Shop_AddCredits(ent, -price);
	Shop_Send(ent, "state", va("Bought the %s.", h->name));
}

static void Shop_Buy(gentity_t* ent, const int item)
{
	gclient_t* cl = ent->client;
	int* credits = &cl->ps.persistant[PERS_CREDITS];

	if (!g_shop->integer || Shop_BusyReason(ent))
	{
		return;
	}
	if (item >= SHOP_ITEM_HOLD && item < SHOP_ITEM_HOLD + SHOP_NUM_HOLDABLES)
	{
		Shop_BuyHoldable(ent, item - SHOP_ITEM_HOLD);
		return;
	}
	if (item == SHOP_ITEM_AMMO)
	{
		const int price = g_shopAmmoPrice->integer > 0 ? g_shopAmmoPrice->integer : 0;
		if (*credits < price)
		{
			Shop_Send(ent, "state", va("You need %i credits for an ammo refill.", price));
			return;
		}
		if (!Shop_FillAmmo(ent))
		{
			Shop_Send(ent, "state", "Your ammo is full.");
			return;
		}
		Shop_AddCredits(ent, -price);
		Shop_Send(ent, "state", "Ammo refilled.");
		return;
	}
	if (item == SHOP_ITEM_BACTA)
	{
		const int price = g_shopBactaPrice->integer > 0 ? g_shopBactaPrice->integer : 0;
		if (*credits < price)
		{
			Shop_Send(ent, "state", va("You need %i credits for a bacta canister.", price));
			return;
		}
		if (cl->ps.inventory[INV_BACTA_CANISTER] >= 5)
		{
			Shop_Send(ent, "state", "You can't carry more bacta.");
			return;
		}
		cl->ps.inventory[INV_BACTA_CANISTER]++;
		Shop_AddCredits(ent, -price);
		Shop_Send(ent, "state", "Bought a bacta canister.");
		return;
	}
	if (item <= 0 || item >= bgNumWeaponVariants)
	{
		return;
	}

	const weaponVariant_t* wv = &bgWeaponVariants[item];
	const int weapon = wv->baseWeapon;
	const int price = BG_WeaponVariantPrice(item);

	if (weapon <= WP_NONE || weapon >= SHOP_MAX_WEAPONS || weapon == WP_SABER)
	{
		return;
	}
	if (weapon == WP_THERMAL)
	{
		// a pack of grenades of that type
		if (*credits < price)
		{
			Shop_Send(ent, "state", va("You need %i credits for %s.", price, wv->displayName));
			return;
		}
		const int a = weaponData[WP_THERMAL].ammoIndex;
		if (BG_HasWeapon(&cl->ps, WP_THERMAL) && cl->ps.ammo[a] >= ammoData[a].max && Shop_Owns(cl, item))
		{
			Shop_Send(ent, "state", "You can't carry more grenades.");
			return;
		}
		Shop_AddCredits(ent, -price);
		G_Shop_GiveVariant(ent, item);
		Add_Ammo(ent, WP_THERMAL, SHOP_PACK); // gives the thermal too
		Shop_Send(ent, "state", va("Bought %i x %s.", SHOP_PACK, wv->displayName));
		return;
	}
	if (Shop_Owns(cl, item) && BG_HasWeapon(&cl->ps, weapon))
	{
		// owned, and he has the gun: put the look on again, free
		G_Shop_GiveVariant(ent, item);
		Shop_Send(ent, "state", va("Using the %s.", wv->displayName));
		return;
	}
	if (*credits < price)
	{
		Shop_Send(ent, "state", va("You need %i credits for the %s.", price, wv->displayName));
		return;
	}
	Shop_AddCredits(ent, -price);
	G_Shop_GiveVariant(ent, item);
	BG_GiveWeapon(&cl->ps, weapon);
	{
		const int a = weaponData[weapon].ammoIndex;
		if (a > AMMO_NONE && a < AMMO_MAX && cl->ps.ammo[a] < ammoData[a].max / 2)
		{
			cl->ps.ammo[a] = ammoData[a].max / 2;
		}
	}
	if (cl->ps.weapon != weapon && cl->ps.weapon != WP_SABER)
	{
		CG_ChangeWeapon(weapon); // in hand, to see it
	}
	Shop_Send(ent, "state", va("Bought the %s.", wv->displayName));
}

// g_items.cpp Pickup_Weapon: a gun dropped by someone carries his look (s.weaponVariant): the player owns it now
void G_Shop_PickupWeapon(gentity_t* player, const gentity_t* item)
{
	if (!player || player->s.number != 0 || !player->client || !item || !item->item || !g_shop || !g_shop->integer)
	{
		return;
	}
	const int v = item->s.weaponVariant;
	const int weapon = item->item->giTag;
	if (weapon == WP_THERMAL || !BG_GetWeaponVariant(v, weapon))
	{
		return;
	}
	const qboolean had = Shop_Owns(player->client, v);
	G_Shop_GiveVariant(player, v);
	if (!had)
	{
		Shop_Print(player, va("^3You picked up the %s.", bgWeaponVariants[v].displayName));
	}
}

// Classes that don't talk (droids, animals, monsters, vehicles)
static qboolean Shop_SilentClass(const int c)
{
	switch (c)
	{
	case CLASS_VEHICLE:
	case CLASS_GONK:
	case CLASS_MOUSE:
	case CLASS_R2D2:
	case CLASS_R5D2:
	case CLASS_PROBE:
	case CLASS_SEEKER:
	case CLASS_REMOTE:
	case CLASS_SENTRY:
	case CLASS_INTERROGATOR:
	case CLASS_MARK1:
	case CLASS_MARK2:
	case CLASS_ATST:
	case CLASS_RANCOR:
	case CLASS_WAMPA:
	case CLASS_SAND_CREATURE:
	case CLASS_HOWLER:
	case CLASS_MINEMONSTER:
	case CLASS_SWAMP:
		return qtrue;
	default:
		return qfalse;
	}
}

// g_utils.cpp CanUseInfrontOf (the use icon) and TryUse: this friendly NPC can be talked to now. The one check for both,
// so the icon only shows when the talk would open: the NPC's own (team, alive, not fighting, not busy, the pazaak
// challenge rules: Pzk_NPCBusy) and the player's pazaak rules (no fight, no enemy close, solid ground...: Shop_BusyReason)
qboolean G_Shop_CanTalk(const gentity_t* player, const gentity_t* npc)
{
	if (!g_npcTalk || !g_npcTalk->integer || !player || player->s.number != 0 || !player->client || !npc || !npc->NPC
		|| !npc->client || npc->health <= 0 || npc->client->ps.pm_type >= PM_DEAD || Pzk_InCutscene()
		|| level.time < shopTalkFrom)
	{
		return qfalse;
	}
	if (npc->client->playerTeam != player->client->playerTeam && npc->client->playerTeam != TEAM_NEUTRAL)
	{
		return qfalse;
	}
	if (npc->NPC->scriptFlags & SCF_NO_RESPONSE || npc->svFlags & SVF_ICARUS_FREEZE
		|| npc->enemy && npc->enemy->health > 0 || Shop_SilentClass(npc->client->NPC_class))
	{
		return qfalse;
	}
	if (Pzk_NPCBusy(npc, player) || Shop_BusyReason(player))
	{
		return qfalse;
	}
	return qtrue;
}

// g_utils.cpp TryUse: the talk question (qtrue: asked)
qboolean G_Shop_TryTalk(gentity_t* player, const gentity_t* npc)
{
	if (!G_Shop_CanTalk(player, npc) || shopTalkNPC >= 0 || pzkRunning || pzkPendingEnt >= 0)
	{
		return qfalse;
	}
	char who[64];
	Q_strncpyz(who, Pzk_NPCName(npc), sizeof who);
	for (char* q = who; *q; q++)
	{
		if (*q == '"')
		{
			*q = '\'';
		}
	}
	shopTalkNPC = npc->s.number;
	gi.SendConsoleCommand(va("uishop talk \"You are talking to %s^7. What do you want to do?\" %i\n", who,
		g_pazaakAllowed && !g_pazaakAllowed->integer ? 0 : 1));
	return qtrue;
}

// Pzk_TryChallenge (a bar challenge, e.g. ns_streets): the talk question instead of the yes / no one (qtrue: asked).
// The held script goes on after the match, straight away (leave), or when the shop menu closes (buy items).
static qboolean Shop_AskBarChallenge(const gentity_t* npc, const int wager)
{
	if (!g_shop || !g_shop->integer || !g_npcTalk || !g_npcTalk->integer || shopTalkNPC >= 0 || shopIsOpen)
	{
		return qfalse;
	}
	char who[64];
	Q_strncpyz(who, npc ? Pzk_NPCName(npc) : "this character", sizeof who);
	for (char* q = who; *q; q++)
	{
		if (*q == '"')
		{
			*q = '\'';
		}
	}
	shopTalkBar = qtrue;
	shopTalkNPC = npc ? npc->s.number : ENTITYNUM_NONE;
	const char* text = wager > 0
		? va("You are talking to %s^7. Play pazaak for %i credits, or buy items?", who, wager)
		: va("You are talking to %s^7. What do you want to do?", who);
	gi.SendConsoleCommand(va("uishop talk \"%s\" 1\n", text));
	return qtrue;
}

// "shoptalk <1 = pazaak, 2 = shop, 0 = leave>"
static void Shop_TalkAnswer(gentity_t* ent, const int answer)
{
	if (shopTalkBar)
	{
		// a bar challenge: 1 = the bar match (Pzk_ChallengeAnswer yes), 2 = the shop, the held script waits for it
		shopTalkBar = qfalse;
		const int n = shopTalkNPC;
		shopTalkNPC = -1;
		if (answer == 2)
		{
			const gentity_t* npc = n >= 0 && n < ENTITYNUM_WORLD && g_entities[n].inuse && g_entities[n].client ? &g_entities[n] : nullptr;
			Shop_Open(ent, npc ? va("%s^7 shows you the goods.", Pzk_NPCName(npc)) : "");
			if (shopIsOpen)
			{
				shopBarHeld = qtrue;
				return;
			}
		}
		Pzk_ChallengeAnswer(ent, answer == 1 ? qtrue : qfalse);
		return;
	}
	const int n = shopTalkNPC;
	shopTalkNPC = -1;
	if (n < 0 || n >= ENTITYNUM_WORLD)
	{
		return;
	}
	gentity_t* npc = &g_entities[n];
	if (!npc->inuse || !npc->client || npc->health <= 0)
	{
		return;
	}
	if (answer == 2)
	{
		Shop_Open(ent, va("%s^7 shows you the goods.", Pzk_NPCName(npc)));
		return;
	}
	if (answer != 1)
	{
		return;
	}
	// challenge him, like the Play Pazaak key on him (G_Pazaak_ClientCommand)
	if (!Pzk_CanPlay(ent) || pzkRunning)
	{
		return;
	}
	if (g_pazaakAllowed && !g_pazaakAllowed->integer)
	{
		Pzk_Unavailable(ent, "");
		return;
	}
	const char* busy = Pzk_BusyReason(ent);
	if (busy)
	{
		Pzk_Unavailable(ent, busy);
		return;
	}
	const char* name = Pzk_NPCName(npc);
	const char* npcBusy = Pzk_NPCBusy(npc, ent);
	if (npcBusy)
	{
		Pzk_CenterPrint(ent, va("You have challenged %s^7 to a Pazaak game.\n%s^7 %s", name, name, npcBusy));
		return;
	}
	Pzk_CenterPrint(ent, va("You have challenged %s^7 to a Pazaak game.\n%s^7 accepts.", name, name));
	Pzk_Print(ent, va("^5Pazaak:^7 You have challenged %s^7 to a Pazaak game. %s^7 accepts.", name, name));
	Pzk_Start(ent, npc, 0, Pzk_NPCCanSit(npc), nullptr);
}

// The shop's commands (G_Pazaak_ClientCommand passes them on): shop, shopbuy, shoptalk, shopcredits (cheat)
static qboolean Shop_ClientCommand(gentity_t* ent, const char* cmd)
{
	if (!ent || !ent->client || ent->s.number != 0)
	{
		return qfalse;
	}
	if (!Q_stricmp(cmd, "shop"))
	{
		Shop_Open(ent, "");
		return qtrue;
	}
	if (!Q_stricmp(cmd, "shopbuy"))
	{
		Shop_Buy(ent, atoi(gi.argv(1)));
		return qtrue;
	}
	if (!Q_stricmp(cmd, "shoptalk"))
	{
		Shop_TalkAnswer(ent, atoi(gi.argv(1)));
		return qtrue;
	}
	if (!Q_stricmp(cmd, "shopclosed"))
	{
		shopIsOpen = qfalse; // the player closed the menu
		Shop_ReleaseBar(ent);
		return qtrue;
	}
	if (!Q_stricmp(cmd, "shopcredits"))
	{
		// shopcredits [amount] (cheat): shows, or sets, the credits
		if (gi.argc() > 1)
		{
			if (!g_cheats || !g_cheats->integer)
			{
				Shop_Print(ent, "Cheats are not enabled on this server.");
				return qtrue;
			}
			ent->client->ps.persistant[PERS_CREDITS] = 0;
			Shop_AddCredits(ent, atoi(gi.argv(1)));
		}
		Shop_Print(ent, va("Credits: %i", ent->client->ps.persistant[PERS_CREDITS]));
		return qtrue;
	}
	return qfalse;
}
