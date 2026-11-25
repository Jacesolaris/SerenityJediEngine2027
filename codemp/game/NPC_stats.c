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

//NPC_stats.cpp
#include "b_local.h"
#include "b_public.h"
#include "anims.h"
#include "ghoul2/G2.h"
#include "g_local.h"
#include "bg_weapons.h"

extern qboolean NPCsPrecached;
qboolean NPC_AddMissingParms(const char* npc_name); // (2026-11-18)

extern qboolean WP_SaberParseParms(const char* SaberName, saberInfo_t* saber);
extern void WP_RemoveSaber(saberInfo_t* sabers, int saberNum);
void NPC_PrecacheByClassName(const char* type);

stringID_table_t TeamTable[] =
{
	ENUM2STRING(NPCTEAM_FREE),
	// caution, some code checks a team_t via "if (!team_t_varname)" so I guess this should stay as entry 0, great or what? -slc
	ENUM2STRING(NPCTEAM_ENEMY),
	ENUM2STRING(NPCTEAM_PLAYER),
	ENUM2STRING(NPCTEAM_NEUTRAL),
	// most droids are team_neutral, there are some exceptions like Probe,Seeker,Interrogator
	ENUM2STRING(NPCTEAM_SOLO),
	{ "", -1 }
};

// A team name to its NPCTEAM_ value, -1 if it is none (2026-11-19). The SP / JKO scripts and map keys name teams three
// ways: "NPCTEAM_ENEMY", "TEAM_ENEMY" (the scripts' SET_PLAYER_TEAM / SET_ENEMY_TEAM) and "enemy" (misc_sentry_turret's
// team key). TeamTable has only the first, so the others gave -1: a scripted NPC was on no team (kejim_post, bespin,
// cairn, doom scripts).
int G_NPCTeamForName(const char* name)
{
	char buf[64] = { 0 };
	int team = 0;

	if (!name || !name[0])
	{
		return -1;
	}
	team = GetIDForString(TeamTable, name);
	if (team >= 0)
	{
		return team;
	}
	if (!Q_stricmpn(name, "TEAM_", 5))
	{
		Com_sprintf(buf, sizeof buf, "NPC%s", name);
	}
	else
	{
		Com_sprintf(buf, sizeof buf, "NPCTEAM_%s", name);
	}
	return GetIDForString(TeamTable, buf);
}

// this list was made using the model directories, this MUST be in the same order as the CLASS_ enum in teams.h
stringID_table_t ClassTable[] =
{
	ENUM2STRING(CLASS_NONE), // hopefully this will never be used by an npc), just covering all bases
	ENUM2STRING(CLASS_ATST), // technically droid...
	ENUM2STRING(CLASS_BARTENDER),
	ENUM2STRING(CLASS_BESPIN_COP),
	ENUM2STRING(CLASS_CLAW),
	ENUM2STRING(CLASS_COMMANDO),
	ENUM2STRING(CLASS_DESANN),
	ENUM2STRING(CLASS_FISH),
	ENUM2STRING(CLASS_FLIER2),
	ENUM2STRING(CLASS_GALAK),
	ENUM2STRING(CLASS_GLIDER),
	ENUM2STRING(CLASS_GONK), // droid
	ENUM2STRING(CLASS_GRAN),
	ENUM2STRING(CLASS_HOWLER),
	ENUM2STRING(CLASS_RANCOR),
	ENUM2STRING(CLASS_SAND_CREATURE),
	ENUM2STRING(CLASS_WAMPA),
	ENUM2STRING(CLASS_IMPERIAL),
	ENUM2STRING(CLASS_IMPWORKER),
	ENUM2STRING(CLASS_INTERROGATOR), // droid
	ENUM2STRING(CLASS_JAN),
	ENUM2STRING(CLASS_JEDI),
	ENUM2STRING(CLASS_KYLE),
	ENUM2STRING(CLASS_LANDO),
	ENUM2STRING(CLASS_LIZARD),
	ENUM2STRING(CLASS_LUKE),
	ENUM2STRING(CLASS_MARK1), // droid
	ENUM2STRING(CLASS_MARK2), // droid
	ENUM2STRING(CLASS_GALAKMECH), // droid
	ENUM2STRING(CLASS_MINEMONSTER),
	ENUM2STRING(CLASS_MONMOTHA),
	ENUM2STRING(CLASS_MORGANKATARN),
	ENUM2STRING(CLASS_MOUSE), // droid
	ENUM2STRING(CLASS_MURJJ),
	ENUM2STRING(CLASS_PRISONER),
	ENUM2STRING(CLASS_PROBE), // droid
	ENUM2STRING(CLASS_PROTOCOL), // droid
	ENUM2STRING(CLASS_R2D2), // droid
	ENUM2STRING(CLASS_R5D2), // droid
	ENUM2STRING(CLASS_REBEL),
	ENUM2STRING(CLASS_REBORN),
	ENUM2STRING(CLASS_REELO),
	ENUM2STRING(CLASS_REMOTE),
	ENUM2STRING(CLASS_RODIAN),
	ENUM2STRING(CLASS_SEEKER), // droid
	ENUM2STRING(CLASS_SENTRY),
	ENUM2STRING(CLASS_SHADOWTROOPER),
	ENUM2STRING(CLASS_SABOTEUR),
	ENUM2STRING(CLASS_STORMTROOPER),
	ENUM2STRING(CLASS_SWAMP),
	ENUM2STRING(CLASS_SWAMPTROOPER),
	ENUM2STRING(CLASS_NOGHRI),
	ENUM2STRING(CLASS_TAVION),
	ENUM2STRING(CLASS_ALORA),
	ENUM2STRING(CLASS_TRANDOSHAN),
	ENUM2STRING(CLASS_UGNAUGHT),
	ENUM2STRING(CLASS_JAWA),
	ENUM2STRING(CLASS_WEEQUAY),
	ENUM2STRING(CLASS_TUSKEN),
	ENUM2STRING(CLASS_BOBAFETT),
	ENUM2STRING(CLASS_ROCKETTROOPER),
	ENUM2STRING(CLASS_SABER_DROID),
	ENUM2STRING(CLASS_PLAYER),
	ENUM2STRING(CLASS_ASSASSIN_DROID),
	ENUM2STRING(CLASS_HAZARD_TROOPER),
	ENUM2STRING(CLASS_VEHICLE),
	ENUM2STRING(CLASS_SBD),
	ENUM2STRING(CLASS_BATTLEDROID),
	ENUM2STRING(CLASS_DROIDEKA),
	ENUM2STRING(CLASS_MANDO),
	ENUM2STRING(CLASS_WOOKIE),
	ENUM2STRING(CLASS_CLONETROOPER),
	ENUM2STRING(CLASS_STORMCOMMANDO),
	ENUM2STRING(CLASS_VADER),
	ENUM2STRING(CLASS_SITHLORD),
	ENUM2STRING(CLASS_OBJECT),
	// SP classes (DH 2026-10-24)
	ENUM2STRING(CLASS_YODA),
	ENUM2STRING(CLASS_JANGO),
	ENUM2STRING(CLASS_JANGODUAL),
	ENUM2STRING(CLASS_GALEN),
	ENUM2STRING(CLASS_GUARD),
	ENUM2STRING(CLASS_AHSOKA),
	ENUM2STRING(CLASS_BOC),
	ENUM2STRING(CLASS_PROJECTION),
	ENUM2STRING(CLASS_JEDIMASTER),
	ENUM2STRING(CLASS_GROGU),
	ENUM2STRING(CLASS_CALONORD),
	{ "CLASS_MANDALORIAN", CLASS_MANDO },
	{ "CLASS_GALAK_MECH", CLASS_GALAKMECH },
	{ "", -1 }
};

stringID_table_t BSTable[] =
{
	ENUM2STRING(BS_DEFAULT), //# default behavior for that NPC
	ENUM2STRING(BS_ADVANCE_FIGHT), //# Advance to captureGoal and shoot enemies if you can
	ENUM2STRING(BS_SLEEP), //# Play awake script when startled by sound
	ENUM2STRING(BS_FOLLOW_LEADER), //# Follow your leader and shoot any enemies you come across
	ENUM2STRING(BS_JUMP), //# Face navgoal and jump to it.
	ENUM2STRING(BS_SEARCH),
	//# Using current waypoint as a base), search the immediate branches of waypoints for enemies
	ENUM2STRING(BS_WANDER), //# Wander down random waypoint paths
	ENUM2STRING(BS_NOCLIP), //# Moves through walls), etc.
	ENUM2STRING(BS_REMOVE), //# Waits for player to leave PVS then removes itself
	ENUM2STRING(BS_CINEMATIC), //# Does nothing but face it's angles and move to a goal if it has one
	ENUM2STRING(BS_FLEE), //# Run toward the nav goal, avoiding danger
	//the rest are internal only
	// ... but we should list them anyway, otherwise workshop behavior will screw up
	ENUM2STRING(BS_WAIT),
	ENUM2STRING(BS_STAND_GUARD),
	ENUM2STRING(BS_PATROL),
	ENUM2STRING(BS_INVESTIGATE),
	ENUM2STRING(BS_HUNT_AND_KILL),
	ENUM2STRING(BS_FOLLOW_OVERRIDE), //# Follow your leader and shoot any enemies you come across
	{ "", -1 },
};

#define stringIDExpand(str, strEnum)	{str, strEnum}, ENUM2STRING(strEnum)

stringID_table_t BSETTable[] =
{
	ENUM2STRING(BSET_SPAWN), //# script to use when first spawned
	ENUM2STRING(BSET_USE), //# script to use when used
	ENUM2STRING(BSET_AWAKE), //# script to use when awoken/startled
	ENUM2STRING(BSET_ANGER), //# script to use when aquire an enemy
	ENUM2STRING(BSET_ATTACK), //# script to run when you attack
	ENUM2STRING(BSET_VICTORY), //# script to run when you kill someone
	ENUM2STRING(BSET_LOSTENEMY), //# script to run when you can't find your enemy
	ENUM2STRING(BSET_PAIN), //# script to use when take pain
	ENUM2STRING(BSET_FLEE), //# script to use when take pain below 50% of health
	ENUM2STRING(BSET_DEATH), //# script to use when killed
	ENUM2STRING(BSET_DELAYED), //# script to run when self->delayScriptTime is reached
	ENUM2STRING(BSET_BLOCKED), //# script to run when blocked by a friendly NPC or player
	ENUM2STRING(BSET_BUMPED), //# script to run when bumped into a friendly NPC or player (can set bumpRadius)
	ENUM2STRING(BSET_STUCK), //# script to run when blocked by a wall
	ENUM2STRING(BSET_FFIRE), //# script to run when player shoots their own teammates
	ENUM2STRING(BSET_FFDEATH), //# script to run when player kills a teammate
	stringIDExpand("", BSET_INVALID),
	{ "", -1 },
};

extern stringID_table_t WPTable[];
extern stringID_table_t FPTable[];

char* TeamNames[TEAM_NUM_TEAMS] =
{
	"",
	"player",
	"enemy",
	"neutral"
};

// this list was made using the model directories, this MUST be in the same order as the CLASS_ enum in teams.h
char* ClassNames[CLASS_NUM_CLASSES] =
{
	"", // class none
	"atst",
	"bartender",
	"bespin_cop",
	"claw",
	"commando",
	"desann",
	"fish",
	"flier2",
	"galak",
	"glider",
	"gonk",
	"gran",
	"howler",
	"imperial",
	"impworker",
	"interrogator",
	"jan",
	"jedi",
	"kyle",
	"lando",
	"lizard",
	"luke",
	"mark1",
	"mark2",
	"galak_mech",
	"minemonster",
	"monmotha",
	"morgankatarn",
	"mouse",
	"murjj",
	"prisoner",
	"probe",
	"protocol",
	"r2d2",
	"r5d2",
	"rebel",
	"reborn",
	"reelo",
	"remote",
	"rodian",
	"seeker",
	"sentry",
	"shadowtrooper",
	"stormtrooper",
	"swamp",
	"swamptrooper",
	"tavion",
	"alora",
	"trandoshan",
	"ugnaught",
	"weequay",
	"bobafett",
	"rockettrooper",
	"saberdroid",
	"assassindroid",
	"hazardtrooper",
	"vehicle",
	"rancor",
	"sandcreature",
	"wampa",
};

/*
NPC_ReactionTime
*/
//FIXME use grandom in here
int NPC_ReactionTime(void)
{
	return 200 * (6 - NPCS.NPCInfo->stats.reactions);
}

extern qboolean BG_ParseLiteral(const char** data, const char* string);

//#define MAX_NPC_DATA_SIZE 0x100000// 1 MB was running until 27/08/2026
#define MAX_NPC_DATA_SIZE 0x200000// 2 MB // this will run 400 NPCs,
//#define MAX_NPC_DATA_SIZE 0x400000 // 4 MB  // this will run 800 NPCs, 4 MB is more than enough for any NPC data file
//#define MAX_NPC_DATA_SIZE 0x800000 // 8 MB Absolut max can cause a crash if exceeded, but 4 MB is more than enough for any NPC data file

char NPCParms[MAX_NPC_DATA_SIZE];

static rank_t TranslateRankName(const char* name)
{
	if (!Q_stricmp(name, "civilian"))
	{
		return RANK_CIVILIAN;
	}

	if (!Q_stricmp(name, "crewman"))
	{
		return RANK_CREWMAN;
	}

	if (!Q_stricmp(name, "ensign"))
	{
		return RANK_ENSIGN;
	}

	if (!Q_stricmp(name, "ltjg"))
	{
		return RANK_LT_JG;
	}

	if (!Q_stricmp(name, "lt"))
	{
		return RANK_LT;
	}

	if (!Q_stricmp(name, "ltcomm"))
	{
		return RANK_LT_COMM;
	}

	if (!Q_stricmp(name, "commander"))
	{
		return RANK_COMMANDER;
	}

	if (!Q_stricmp(name, "captain"))
	{
		return RANK_CAPTAIN;
	}

	return RANK_CIVILIAN;
}

static int MoveTypeNameToEnum(const char* name)
{
	if (!Q_stricmp("runjump", name))
	{
		return MT_RUNJUMP;
	}
	if (!Q_stricmp("walk", name))
	{
		return EF_WALK;
	}
	if (!Q_stricmp("flyswim", name))
	{
		return EF2_FLYING;
	}
	if (!Q_stricmp("static", name))
	{
		return MT_STATIC;
	}

	return MT_STATIC;
}

extern saber_colors_t TranslateSaberColor(const char* name);

#ifdef CONVENIENT_ANIMATION_FILE_DEBUG_THING
void SpewDebugStuffToFile(animation_t* anims)
{
	char BGPAFtext[40000];
	fileHandle_t f;
	int i = 0;

	trap->FS_Open("file_of_debug_stuff_MP.txt", &f, FS_WRITE);

	if (!f)
	{
		return;
	}

	BGPAFtext[0] = 0;

	while (i < MAX_ANIMATIONS)
	{
		strcat(BGPAFtext, va("%i %i\n", i, anims[i].frameLerp));
		i++;
	}

	trap->FS_Write(BGPAFtext, strlen(BGPAFtext), f);
	trap->FS_Close(f);
}
#endif

static qboolean G_ParseAnimFileSet(const char* filename, const char* animCFG, int* animFileIndex)
{
	*animFileIndex = bg_parse_animation_file(filename, NULL, qfalse);
	//if it's humanoid we should have it cached and return it, if it is not it will be loaded (unless it's also cached already)

	if (*animFileIndex == -1)
	{
		return qfalse;
	}
	return qtrue;
}

void NPC_PrecacheAnimationCFG(const char* npc_type)
{
#if 0
	char	filename[MAX_QPATH];
	const char* token;
	const char* value;
	const char* p;
	int		junk;

	if (!Q_stricmp("random", NPC_type))
	{//sorry, can't precache a random just yet
		return;
	}

	p = NPCParms;
	COM_BeginParseSession(NPCFile);

	// look for the right NPC
	while (p)
	{
		token = COM_ParseExt(&p, qtrue);
		if (token[0] == 0)
			return;

		if (!Q_stricmp(token, NPC_type))
		{
			break;
		}

		SkipBracedSection(&p, 0);
	}

	if (!p)
	{
		return;
	}

	if (BG_ParseLiteral(&p, "{"))
	{
		return;
	}

	// parse the NPC info block
	while (1)
	{
		token = COM_ParseExt(&p, qtrue);
		if (!token[0])
		{
			Com_Printf(S_COLOR_RED"ERROR: unexpected EOF while parsing '%s'\n", NPC_type);
			return;
		}

		if (!Q_stricmp(token, "}"))
		{
			break;
		}

		// legsmodel
		if (!Q_stricmp(token, "legsmodel"))
		{
			if (COM_ParseString(&p, &value))
			{
				continue;
			}
			//must copy data out of this pointer into a different part of memory because the funcs we're about to call will call COM_ParseExt
			Q_strncpyz(filename, value, sizeof(filename));
			G_ParseAnimFileSet(filename, filename, &junk);
			return;
		}

		// playerModel
		if (!Q_stricmp(token, "playerModel"))
		{
			if (COM_ParseString(&p, &value))
			{
				continue;
			}
		}
	}
#endif
}

extern int NPC_WeaponsForTeam(const npcteam_t team, const int spawnflags, const char* NPC_type);

static void NPC_PrecacheWeapons(team_t playerTeam, int spawnflags, char* NPCtype)
{
	int weapons = NPC_WeaponsForTeam(playerTeam, spawnflags, NPCtype);

	for (int curWeap = WP_SABER; curWeap < WP_NUM_WEAPONS; curWeap++)
	{
		if (WP_MASK_HAS(weapons, curWeap))
		{
			RegisterItem(BG_FindItemForWeapon(curWeap));
		}
	}

#if 0
	int weapons = NPC_WeaponsForTeam(playerTeam, spawnflags, NPCtype);
	gitem_t* item;
	for (int curWeap = WP_SABER; curWeap < WP_NUM_WEAPONS; curWeap++)
	{
		if ((WP_MASK_HAS(weapons, curWeap)))
		{
			item = FindItemForWeapon(((weapon_t)(curWeap)));	//precache the weapon
			CG_RegisterItemSounds((item - bg_itemlist));
			CG_RegisterItemVisuals((item - bg_itemlist));
			//precache the in-hand/in-world ghoul2 weapon model

			char weaponModel[64];

			strcpy(weaponModel, weaponData[curWeap].weaponMdl);
			if (char* spot = strstr(weaponModel, ".md3")) {
				*spot = 0;
				spot = strstr(weaponModel, "_w");//i'm using the in view weapon array instead of scanning the item list, so put the _w back on
				if (!spot) {
					strcat(weaponModel, "_w");
				}
				strcat(weaponModel, ".glm");	//and change to ghoul2
			}
			trap->G2API_PrecacheGhoul2Model(weaponModel); // correct way is item->world_model
		}
	}
#endif
}

void NPC_Precache(gentity_t* spawner)
{
	npcteam_t playerTeam = NPCTEAM_FREE;
	const char* token = NULL;
	const char* value = NULL;
	const char* p = NULL;
	char* patch = NULL;
	qboolean md3Model = qfalse;
	char playerModel[MAX_QPATH] = { 0 };
	char customSkin[MAX_QPATH] = { 0 };
	char sessionName[MAX_QPATH + 15] = { 0 };
	qboolean added = qfalse;

	if (!Q_stricmp("random", spawner->NPC_type))
	{
		//sorry, can't precache a random just yet
		return;
	}
	strcpy(customSkin, "default");

	p = NPCParms;
	Com_sprintf(sessionName, sizeof sessionName, "NPC_Precache(%s)", spawner->NPC_type);
	COM_BeginParseSession(sessionName);

	// look for the right NPC
	while (p)
	{
		token = COM_ParseExt(&p, qtrue);
		if (token[0] == 0)
		{
			// not in NPCParms: maybe in SP's NPCs (2026-11-18)
			if (!added && NPC_AddMissingParms(spawner->NPC_type))
			{
				added = qtrue;
				p = NPCParms;
				COM_BeginParseSession(sessionName);
				continue;
			}
			return;
		}

		if (!Q_stricmp(token, spawner->NPC_type))
		{
			break;
		}

		SkipBracedSection(&p, 0);
	}

	if (!p)
	{
		return;
	}

	if (BG_ParseLiteral(&p, "{"))
	{
		return;
	}

	// parse the NPC info block
	while (1)
	{
		char sound[MAX_QPATH] = { 0 };
		token = COM_ParseExt(&p, qtrue);
		if (!token[0])
		{
			Com_Printf(S_COLOR_RED"ERROR: unexpected EOF while parsing '%s'\n", spawner->NPC_type);
			return;
		}

		if (!Q_stricmp(token, "}"))
		{
			break;
		}

		// headmodel
		if (!Q_stricmp(token, "headmodel"))
		{
			if (COM_ParseString(&p, &value))
			{
				continue;
			}

			if (!Q_stricmp("none", value))
			{
			}
			else
			{
				//Q_strncpyz( ri->headModelName, value, sizeof(ri->headModelName), qtrue);
			}
			md3Model = qtrue;
			continue;
		}

		// torsomodel
		if (!Q_stricmp(token, "torsomodel"))
		{
			if (COM_ParseString(&p, &value))
			{
				continue;
			}

			if (!Q_stricmp("none", value))
			{
			}
			else
			{
				//Q_strncpyz( ri->torsoModelName, value, sizeof(ri->torsoModelName), qtrue);
			}
			md3Model = qtrue;
			continue;
		}

		// legsmodel
		if (!Q_stricmp(token, "legsmodel"))
		{
			if (COM_ParseString(&p, &value))
			{
				continue;
			}
			Q_strncpyz(playerModel, value, sizeof playerModel);
			md3Model = qtrue;
			continue;
		}

		// playerModel
		if (!Q_stricmp(token, "playerModel"))
		{
			if (COM_ParseString(&p, &value))
			{
				continue;
			}
			Q_strncpyz(playerModel, value, sizeof playerModel);
			md3Model = qfalse;
			continue;
		}

		// customSkin
		if (!Q_stricmp(token, "customSkin"))
		{
			if (COM_ParseString(&p, &value))
			{
				continue;
			}
			Q_strncpyz(customSkin, value, sizeof customSkin);
			continue;
		}

		// playerTeam
		if (!Q_stricmp(token, "playerTeam"))
		{
			char tk[4096] = { 0 };

			if (COM_ParseString(&p, &value))
			{
				continue;
			}
			Com_sprintf(tk, sizeof tk, "NPC%s", token);
			playerTeam = (npcteam_t)(team_t)GetIDForString(TeamTable, tk);
			continue;
		}

		// snd
		if (!Q_stricmp(token, "snd"))
		{
			if (COM_ParseString(&p, &value))
			{
				continue;
			}
			if (!(spawner->r.svFlags & SVF_NO_BASIC_SOUNDS))
			{
				Q_strncpyz(sound, value, sizeof sound);
				patch = strstr(sound, "/");
				if (patch)
				{
					*patch = 0;
				}
				spawner->s.csSounds_Std = G_SoundIndex(va("*$%s", sound));
			}
			continue;
		}

		// sndcombat
		if (!Q_stricmp(token, "sndcombat"))
		{
			if (COM_ParseString(&p, &value))
			{
				continue;
			}
			if (!(spawner->r.svFlags & SVF_NO_COMBAT_SOUNDS))
			{
				//FIXME: store this in some sound field or parse in the soundTable like the animTable...
				Q_strncpyz(sound, value, sizeof sound);
				patch = strstr(sound, "/");
				if (patch)
				{
					*patch = 0;
				}
				spawner->s.csSounds_Combat = G_SoundIndex(va("*$%s", sound));
			}
			continue;
		}

		// sndextra
		if (!Q_stricmp(token, "sndextra"))
		{
			if (COM_ParseString(&p, &value))
			{
				continue;
			}
			if (!(spawner->r.svFlags & SVF_NO_EXTRA_SOUNDS))
			{
				//FIXME: store this in some sound field or parse in the soundTable like the animTable...
				Q_strncpyz(sound, value, sizeof sound);
				patch = strstr(sound, "/");
				if (patch)
				{
					*patch = 0;
				}
				spawner->s.csSounds_Extra = G_SoundIndex(va("*$%s", sound));
			}
			continue;
		}

		// sndjedi
		if (!Q_stricmp(token, "sndjedi"))
		{
			if (COM_ParseString(&p, &value))
			{
				continue;
			}
			if (!(spawner->r.svFlags & SVF_NO_EXTRA_SOUNDS))
			{
				//FIXME: store this in some sound field or parse in the soundTable like the animTable...
				Q_strncpyz(sound, value, sizeof sound);
				patch = strstr(sound, "/");
				if (patch)
				{
					*patch = 0;
				}
				spawner->s.csSounds_Jedi = G_SoundIndex(va("*$%s", sound));
			}
			continue;
		}

		// sndcallout
		if (!Q_stricmp(token, "sndcallout"))
		{
			if (COM_ParseString(&p, &value))
			{
				continue;
			}
			if (!(spawner->r.svFlags & SVF_NO_EXTRA_SOUNDS))
			{
				//FIXME: store this in some sound field or parse in the soundTable like the animTable...
				Q_strncpyz(sound, value, sizeof sound);
				patch = strstr(sound, "/");
				if (patch)
				{
					*patch = 0;
				}
				spawner->s.csSounds_Custom = G_SoundIndex(va("*$%s", sound));
			}
			continue;
		}

		if (!Q_stricmp(token, "weapon"))
		{
			if (COM_ParseString(&p, &value))
			{
				continue;
			}

			const int curWeap = GetIDForString(WPTable, value);

			if (curWeap > WP_NONE && curWeap < WP_NUM_WEAPONS)
			{
				RegisterItem(BG_FindItemForWeapon(curWeap));
			}
		}
	}

	// If we're not a vehicle, then an error here would be valid...
	if (!spawner->client || spawner->client->NPC_class != CLASS_VEHICLE)
	{
		//adding in md3 NPC support

		if (md3Model)
		{
			//Com_sprintf(modelName, sizeof(modelName), "models/players/%s/lower.md3", playerModel);
		}
		else
		{
			char modelName[MAX_QPATH] = { 0 };
			//if we have a model/skin then index them so they'll be registered immediately
			//when the client gets a configstring update.
			Com_sprintf(modelName, sizeof modelName, "models/players/%s/model.glm", playerModel);
			if (customSkin[0])
			{
				//append it after a *
				strcat(modelName, va("*%s", customSkin));
			}
			G_model_index(modelName);
		}
	}

	//precache this NPC's possible weapons
	NPC_PrecacheWeapons(playerTeam, spawner->spawnflags, spawner->NPC_type);

	// Anything else special about them
	NPC_PrecacheByClassName(spawner->NPC_type);
}

static void NPC_BuildRandom()
{}

#define TURN_ON				0x00000000
#define TURN_OFF			0x00000100
extern void SetupGameGhoul2Model(gentity_t* ent, char* modelname, char* skinName);

qboolean NPC_ParseParms(const char* npc_name, gentity_t* npc)
{
	const char* value = NULL;
	const char* p = NULL;
	int n = 0;
	float f = 0.0f;
	char playerModel[MAX_QPATH] = { 0 };
	char customSkin[MAX_QPATH] = { 0 };
	renderInfo_t* ri = &npc->client->renderInfo;
	gNPCstats_t* stats = NULL;
	qboolean md3Model = qtrue;
	char surfOff[1024] = { 0 };
	char surfOn[1024] = { 0 };
	qboolean parsingPlayer = qfalse;
	vec3_t playerMins = { 0 };
	vec3_t playerMaxs = { 0 };
	int npcSaber1 = 0;
	int npcSaber2 = 0;

	VectorSet(playerMins, -15, -15, DEFAULT_MINS_2);
	VectorSet(playerMaxs, 15, 15, DEFAULT_MAXS_2);

	strcpy(customSkin, "default");
	if (!npc_name || !npc_name[0])
	{
		npc_name = "Player";
	}

	if (!npc->s.number && npc->client != NULL)
	{
		//player, only want certain data
		parsingPlayer = qtrue;
	}

	if (npc->NPC)
	{
		stats = &npc->NPC->stats;
		// fill in defaults
		stats->sex = SEX_MALE;

		stats->aggression = 5;
		stats->aim = 5;
		stats->earshot = 2048;
		stats->evasion = 5;
		stats->hfov = 140;
		stats->intelligence = 5;
		stats->move = 5;
		stats->reactions = 5;
		stats->vfov = 120;
		stats->vigilance = 0.1f;
		stats->visrange = 2048;
		stats->health = 0;
		stats->yawSpeed = 190;
		stats->walkSpeed = 90;
		stats->runSpeed = 300;
		stats->acceleration = 25; //Increase/descrease speed this much per frame (20fps)
	}
	else
	{
		stats = NULL;
	}
	memset(surfOff, 0, sizeof surfOff);
	memset(surfOn, 0, sizeof surfOn);

	ri->headYawRangeLeft = 80;
	ri->headYawRangeRight = 80;
	ri->headPitchRangeUp = 45;
	ri->headPitchRangeDown = 45;
	ri->torsoYawRangeLeft = 60;
	ri->torsoYawRangeRight = 60;
	ri->torsoPitchRangeUp = 30;
	ri->torsoPitchRangeDown = 50;

	VectorCopy(playerMins, npc->r.mins);
	VectorCopy(playerMaxs, npc->r.maxs);
	npc->client->ps.crouchheight = CROUCH_MAXS_2;
	npc->client->ps.standheight = DEFAULT_MAXS_2;

	npc->client->dismemberProbHead = 100;
	npc->client->dismemberProbArms = 100;
	npc->client->dismemberProbHands = 100;
	npc->client->dismemberProbWaist = 100;
	npc->client->dismemberProbLegs = 100;

	npc->client->ps.customRGBA[0] = 255;
	npc->client->ps.customRGBA[1] = 255;
	npc->client->ps.customRGBA[2] = 255;
	npc->client->ps.customRGBA[3] = 255;

	if (!Q_stricmp("random", npc_name))
	{
		//Randomly assemble a starfleet guy
		NPC_BuildRandom();
	}
	else
	{
		char session_name[MAX_QPATH + 17] = { 0 };
		char* patch = NULL;
		const char* token = NULL;
		int fp = 0;
		qboolean added = qfalse;

		p = NPCParms;
		Com_sprintf(session_name, sizeof session_name, "NPC_ParseParms(%s)", npc_name);
		COM_BeginParseSession(session_name);

		// look for the right NPC
		while (p)
		{
			token = COM_ParseExt(&p, qtrue);
			if (token[0] == 0)
			{
				// not in NPCParms: maybe in SP's NPCs (2026-11-18)
				if (!added && NPC_AddMissingParms(npc_name))
				{
					added = qtrue;
					p = NPCParms;
					COM_BeginParseSession(session_name);
					continue;
				}
				return qfalse;
			}

			if (!Q_stricmp(token, npc_name))
			{
				break;
			}

			SkipBracedSection(&p, 0);
		}
		if (!p)
		{
			return qfalse;
		}

		if (BG_ParseLiteral(&p, "{"))
		{
			return qfalse;
		}

		// parse the NPC info block
		while (1)
		{
			char sound[MAX_QPATH] = { 0 };
			token = COM_ParseExt(&p, qtrue);
			if (!token[0])
			{
				Com_Printf(S_COLOR_RED"ERROR: unexpected EOF while parsing '%s'\n", npc_name);
				return qfalse;
			}

			if (!Q_stricmp(token, "}"))
			{
				break;
			}
			//===MODEL PROPERTIES===========================================================
			// custom color
			if (!Q_stricmp(token, "customRGBA"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (!Q_stricmp(value, "random"))
				{
					npc->client->ps.customRGBA[0] = Q_irand(0, 255);
					npc->client->ps.customRGBA[1] = Q_irand(0, 255);
					npc->client->ps.customRGBA[2] = Q_irand(0, 255);
					npc->client->ps.customRGBA[3] = 255;
				}
				else if (!Q_stricmp(value, "random1"))
				{
					npc->client->ps.customRGBA[3] = 255;
					switch (Q_irand(0, 5))
					{
					default:
					case 0:
						npc->client->ps.customRGBA[0] = 127;
						npc->client->ps.customRGBA[1] = 153;
						npc->client->ps.customRGBA[2] = 255;
						break;
					case 1:
						npc->client->ps.customRGBA[0] = 177;
						npc->client->ps.customRGBA[1] = 29;
						npc->client->ps.customRGBA[2] = 13;
						break;
					case 2:
						npc->client->ps.customRGBA[0] = 47;
						npc->client->ps.customRGBA[1] = 90;
						npc->client->ps.customRGBA[2] = 40;
						break;
					case 3:
						npc->client->ps.customRGBA[0] = 181;
						npc->client->ps.customRGBA[1] = 207;
						npc->client->ps.customRGBA[2] = 255;
						break;
					case 4:
						npc->client->ps.customRGBA[0] = 138;
						npc->client->ps.customRGBA[1] = 83;
						npc->client->ps.customRGBA[2] = 0;
						break;
					case 5:
						npc->client->ps.customRGBA[0] = 254;
						npc->client->ps.customRGBA[1] = 199;
						npc->client->ps.customRGBA[2] = 14;
						break;
					}
				}
				else if (!Q_stricmp(value, "jedi_hf"))
				{
					npc->client->ps.customRGBA[3] = 255;
					switch (Q_irand(0, 7))
					{
					default:
					case 0: //red1
						npc->client->ps.customRGBA[0] = 165;
						npc->client->ps.customRGBA[1] = 48;
						npc->client->ps.customRGBA[2] = 21;
						break;
					case 1: //yellow1
						npc->client->ps.customRGBA[0] = 254;
						npc->client->ps.customRGBA[1] = 230;
						npc->client->ps.customRGBA[2] = 132;
						break;
					case 2: //bluegray
						npc->client->ps.customRGBA[0] = 181;
						npc->client->ps.customRGBA[1] = 207;
						npc->client->ps.customRGBA[2] = 255;
						break;
					case 3: //pink
						npc->client->ps.customRGBA[0] = 233;
						npc->client->ps.customRGBA[1] = 183;
						npc->client->ps.customRGBA[2] = 208;
						break;
					case 4: //lt blue
						npc->client->ps.customRGBA[0] = 161;
						npc->client->ps.customRGBA[1] = 226;
						npc->client->ps.customRGBA[2] = 240;
						break;
					case 5: //blue
						npc->client->ps.customRGBA[0] = 101;
						npc->client->ps.customRGBA[1] = 159;
						npc->client->ps.customRGBA[2] = 255;
						break;
					case 6: //orange
						npc->client->ps.customRGBA[0] = 255;
						npc->client->ps.customRGBA[1] = 157;
						npc->client->ps.customRGBA[2] = 114;
						break;
					case 7: //violet
						npc->client->ps.customRGBA[0] = 216;
						npc->client->ps.customRGBA[1] = 160;
						npc->client->ps.customRGBA[2] = 255;
						break;
					}
				}
				else if (!Q_stricmp(value, "jedi_hm"))
				{
					npc->client->ps.customRGBA[3] = 255;
					switch (Q_irand(0, 7))
					{
					default:
					case 0: //yellow
						npc->client->ps.customRGBA[0] = 252;
						npc->client->ps.customRGBA[1] = 243;
						npc->client->ps.customRGBA[2] = 180;
						break;
					case 1: //blue
						npc->client->ps.customRGBA[0] = 69;
						npc->client->ps.customRGBA[1] = 109;
						npc->client->ps.customRGBA[2] = 255;
						break;
					case 2: //gold
						npc->client->ps.customRGBA[0] = 254;
						npc->client->ps.customRGBA[1] = 197;
						npc->client->ps.customRGBA[2] = 73;
						break;
					case 3: //orange
						npc->client->ps.customRGBA[0] = 178;
						npc->client->ps.customRGBA[1] = 78;
						npc->client->ps.customRGBA[2] = 18;
						break;
					case 4: //bluegreen
						npc->client->ps.customRGBA[0] = 112;
						npc->client->ps.customRGBA[1] = 153;
						npc->client->ps.customRGBA[2] = 161;
						break;
					case 5: //blue2
						npc->client->ps.customRGBA[0] = 123;
						npc->client->ps.customRGBA[1] = 182;
						npc->client->ps.customRGBA[2] = 255;
						break;
					case 6: //green2
						npc->client->ps.customRGBA[0] = 0;
						npc->client->ps.customRGBA[1] = 88;
						npc->client->ps.customRGBA[2] = 105;
						break;
					case 7: //violet
						npc->client->ps.customRGBA[0] = 138;
						npc->client->ps.customRGBA[1] = 0;
						npc->client->ps.customRGBA[2] = 0;
						break;
					}
				}
				else if (!Q_stricmp(value, "jedi_kdm"))
				{
					npc->client->ps.customRGBA[3] = 255;
					switch (Q_irand(0, 8))
					{
					default:
					case 0: //blue
						npc->client->ps.customRGBA[0] = 85;
						npc->client->ps.customRGBA[1] = 120;
						npc->client->ps.customRGBA[2] = 255;
						break;
					case 1: //violet
						npc->client->ps.customRGBA[0] = 173;
						npc->client->ps.customRGBA[1] = 142;
						npc->client->ps.customRGBA[2] = 219;
						break;
					case 2: //brown1
						npc->client->ps.customRGBA[0] = 254;
						npc->client->ps.customRGBA[1] = 197;
						npc->client->ps.customRGBA[2] = 73;
						break;
					case 3: //orange
						npc->client->ps.customRGBA[0] = 138;
						npc->client->ps.customRGBA[1] = 83;
						npc->client->ps.customRGBA[2] = 0;
						break;
					case 4: //gold
						npc->client->ps.customRGBA[0] = 254;
						npc->client->ps.customRGBA[1] = 199;
						npc->client->ps.customRGBA[2] = 14;
						break;
					case 5: //blue2
						npc->client->ps.customRGBA[0] = 68;
						npc->client->ps.customRGBA[1] = 194;
						npc->client->ps.customRGBA[2] = 217;
						break;
					case 6: //red1
						npc->client->ps.customRGBA[0] = 170;
						npc->client->ps.customRGBA[1] = 3;
						npc->client->ps.customRGBA[2] = 30;
						break;
					case 7: //yellow1
						npc->client->ps.customRGBA[0] = 225;
						npc->client->ps.customRGBA[1] = 226;
						npc->client->ps.customRGBA[2] = 144;
						break;
					case 8: //violet2
						npc->client->ps.customRGBA[0] = 167;
						npc->client->ps.customRGBA[1] = 202;
						npc->client->ps.customRGBA[2] = 255;
						break;
					}
				}
				else if (!Q_stricmp(value, "jedi_rm"))
				{
					npc->client->ps.customRGBA[3] = 255;
					switch (Q_irand(0, 8))
					{
					default:
					case 0: //blue
						npc->client->ps.customRGBA[0] = 127;
						npc->client->ps.customRGBA[1] = 153;
						npc->client->ps.customRGBA[2] = 255;
						break;
					case 1: //green1
						npc->client->ps.customRGBA[0] = 208;
						npc->client->ps.customRGBA[1] = 249;
						npc->client->ps.customRGBA[2] = 85;
						break;
					case 2: //blue2
						npc->client->ps.customRGBA[0] = 181;
						npc->client->ps.customRGBA[1] = 207;
						npc->client->ps.customRGBA[2] = 255;
						break;
					case 3: //gold
						npc->client->ps.customRGBA[0] = 138;
						npc->client->ps.customRGBA[1] = 83;
						npc->client->ps.customRGBA[2] = 0;
						break;
					case 4: //gold
						npc->client->ps.customRGBA[0] = 224;
						npc->client->ps.customRGBA[1] = 171;
						npc->client->ps.customRGBA[2] = 44;
						break;
					case 5: //green2
						npc->client->ps.customRGBA[0] = 49;
						npc->client->ps.customRGBA[1] = 155;
						npc->client->ps.customRGBA[2] = 131;
						break;
					case 6: //red1
						npc->client->ps.customRGBA[0] = 163;
						npc->client->ps.customRGBA[1] = 79;
						npc->client->ps.customRGBA[2] = 17;
						break;
					case 7: //violet2
						npc->client->ps.customRGBA[0] = 148;
						npc->client->ps.customRGBA[1] = 104;
						npc->client->ps.customRGBA[2] = 228;
						break;
					case 8: //green3
						npc->client->ps.customRGBA[0] = 138;
						npc->client->ps.customRGBA[1] = 136;
						npc->client->ps.customRGBA[2] = 0;
						break;
					}
				}
				else if (!Q_stricmp(value, "jedi_tf"))
				{
					npc->client->ps.customRGBA[3] = 255;
					switch (Q_irand(0, 5))
					{
					default:
					case 0: //green1
						npc->client->ps.customRGBA[0] = 255;
						npc->client->ps.customRGBA[1] = 235;
						npc->client->ps.customRGBA[2] = 100;
						break;
					case 1: //blue1
						npc->client->ps.customRGBA[0] = 62;
						npc->client->ps.customRGBA[1] = 155;
						npc->client->ps.customRGBA[2] = 255;
						break;
					case 2: //red1
						npc->client->ps.customRGBA[0] = 255;
						npc->client->ps.customRGBA[1] = 110;
						npc->client->ps.customRGBA[2] = 120;
						break;
					case 3: //purple
						npc->client->ps.customRGBA[0] = 180;
						npc->client->ps.customRGBA[1] = 150;
						npc->client->ps.customRGBA[2] = 255;
						break;
					case 4: //flesh
						npc->client->ps.customRGBA[0] = 255;
						npc->client->ps.customRGBA[1] = 200;
						npc->client->ps.customRGBA[2] = 212;
						break;
					case 5: //base
						npc->client->ps.customRGBA[0] = 255;
						npc->client->ps.customRGBA[1] = 255;
						npc->client->ps.customRGBA[2] = 255;
						break;
					}
				}
				else if (!Q_stricmp(value, "jedi_zf"))
				{
					npc->client->ps.customRGBA[3] = 255;
					switch (Q_irand(0, 7))
					{
					default:
					case 0: //red1
						npc->client->ps.customRGBA[0] = 204;
						npc->client->ps.customRGBA[1] = 19;
						npc->client->ps.customRGBA[2] = 21;
						break;
					case 1: //orange1
						npc->client->ps.customRGBA[0] = 255;
						npc->client->ps.customRGBA[1] = 107;
						npc->client->ps.customRGBA[2] = 40;
						break;
					case 2: //pink1
						npc->client->ps.customRGBA[0] = 255;
						npc->client->ps.customRGBA[1] = 148;
						npc->client->ps.customRGBA[2] = 155;
						break;
					case 3: //gold
						npc->client->ps.customRGBA[0] = 255;
						npc->client->ps.customRGBA[1] = 164;
						npc->client->ps.customRGBA[2] = 59;
						break;
					case 4: //violet1
						npc->client->ps.customRGBA[0] = 216;
						npc->client->ps.customRGBA[1] = 160;
						npc->client->ps.customRGBA[2] = 255;
						break;
					case 5: //blue1
						npc->client->ps.customRGBA[0] = 101;
						npc->client->ps.customRGBA[1] = 159;
						npc->client->ps.customRGBA[2] = 255;
						break;
					case 6: //blue2
						npc->client->ps.customRGBA[0] = 161;
						npc->client->ps.customRGBA[1] = 226;
						npc->client->ps.customRGBA[2] = 240;
						break;
					case 7: //blue3
						npc->client->ps.customRGBA[0] = 37;
						npc->client->ps.customRGBA[1] = 155;
						npc->client->ps.customRGBA[2] = 181;
						break;
					}
				}
				else
				{
					npc->client->ps.customRGBA[0] = atoi(value);

					if (COM_ParseInt(&p, &n))
					{
						continue;
					}
					npc->client->ps.customRGBA[1] = n;

					if (COM_ParseInt(&p, &n))
					{
						continue;
					}
					npc->client->ps.customRGBA[2] = n;

					if (COM_ParseInt(&p, &n))
					{
						continue;
					}
					npc->client->ps.customRGBA[3] = n;
				}
				continue;
			}

			// headmodel
			if (!Q_stricmp(token, "headmodel"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}

				if (!Q_stricmp("none", value))
				{
					//Zero the head clamp range so the torso & legs don't lag behind
					ri->headYawRangeLeft =
						ri->headYawRangeRight =
						ri->headPitchRangeUp =
						ri->headPitchRangeDown = 0;
				}
				continue;
			}

			// torsomodel
			if (!Q_stricmp(token, "torsomodel"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}

				if (!Q_stricmp("none", value))
				{
					//Zero the torso clamp range so the legs don't lag behind
					ri->torsoYawRangeLeft =
						ri->torsoYawRangeRight =
						ri->torsoPitchRangeUp =
						ri->torsoPitchRangeDown = 0;
				}
				continue;
			}

			// legsmodel
			if (!Q_stricmp(token, "legsmodel"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				//md3 models use their legsmodel as their player model
				Q_strncpyz(playerModel, value, sizeof playerModel);
				continue;
			}

			// playerModel
			if (!Q_stricmp(token, "playerModel"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				Q_strncpyz(playerModel, value, sizeof playerModel);
				md3Model = qfalse;
				continue;
			}

			// customSkin
			if (!Q_stricmp(token, "customSkin"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				Q_strncpyz(customSkin, value, sizeof customSkin);
				continue;
			}

			// surfOff
			if (!Q_stricmp(token, "surfOff"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (surfOff[0])
				{
					Q_strcat(surfOff, sizeof surfOff, ",");
					Q_strcat(surfOff, sizeof surfOff, value);
				}
				else
				{
					Q_strncpyz(surfOff, value, sizeof surfOff);
				}
				continue;
			}

			// surfOn
			if (!Q_stricmp(token, "surfOn"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (surfOn[0])
				{
					Q_strcat(surfOn, sizeof surfOn, ",");
					Q_strcat(surfOn, sizeof surfOn, value);
				}
				else
				{
					Q_strncpyz(surfOn, value, sizeof surfOn);
				}
				continue;
			}

			//headYawRangeLeft
			if (!Q_stricmp(token, "headYawRangeLeft"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				ri->headYawRangeLeft = n;
				continue;
			}

			//headYawRangeRight
			if (!Q_stricmp(token, "headYawRangeRight"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				ri->headYawRangeRight = n;
				continue;
			}

			//headPitchRangeUp
			if (!Q_stricmp(token, "headPitchRangeUp"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				ri->headPitchRangeUp = n;
				continue;
			}

			//headPitchRangeDown
			if (!Q_stricmp(token, "headPitchRangeDown"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				ri->headPitchRangeDown = n;
				continue;
			}

			//torsoYawRangeLeft
			if (!Q_stricmp(token, "torsoYawRangeLeft"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				ri->torsoYawRangeLeft = n;
				continue;
			}

			//torsoYawRangeRight
			if (!Q_stricmp(token, "torsoYawRangeRight"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				ri->torsoYawRangeRight = n;
				continue;
			}

			//torsoPitchRangeUp
			if (!Q_stricmp(token, "torsoPitchRangeUp"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				ri->torsoPitchRangeUp = n;
				continue;
			}

			//torsoPitchRangeDown
			if (!Q_stricmp(token, "torsoPitchRangeDown"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				ri->torsoPitchRangeDown = n;
				continue;
			}

			// Uniform XYZ scale
			if (!Q_stricmp(token, "scale"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				if (n != 100)
				{
					npc->client->ps.iModelScale = n; //so the client knows
					if (n >= 1024)
					{
						Com_Printf("WARNING: MP does not support scaling up to or over 1024%\n");
						n = 1023;
					}

					npc->modelScale[0] = npc->modelScale[1] = npc->modelScale[2] = n / 100.0f;
				}
				continue;
			}

			//X scale
			if (!Q_stricmp(token, "scaleX"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				if (n != 100)
				{
					Com_Printf("MP doesn't support xyz scaling, use 'scale'.\n");
					//NPC->s.modelScale[0] = n/100.0f;
				}
				continue;
			}

			//Y scale
			if (!Q_stricmp(token, "scaleY"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				if (n != 100)
				{
					Com_Printf("MP doesn't support xyz scaling, use 'scale'.\n");
					//NPC->s.modelScale[1] = n/100.0f;
				}
				continue;
			}

			//Z scale
			if (!Q_stricmp(token, "scaleZ"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				if (n != 100)
				{
					Com_Printf("MP doesn't support xyz scaling, use 'scale'.\n");
					//	NPC->s.modelScale[2] = n/100.0f;
				}
				continue;
			}

			//===AI STATS=====================================================================
			if (!parsingPlayer)
			{
				// aggression
				if (!Q_stricmp(token, "aggression"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (n < 1 || n > 5)
					{
						Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->aggression = n;
					}
					continue;
				}

				// aim
				if (!Q_stricmp(token, "aim"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (n < 1 || n > 5)
					{
						Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->aim = n;
					}
					continue;
				}

				// earshot
				if (!Q_stricmp(token, "earshot"))
				{
					if (COM_ParseFloat(&p, &f))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (f < 0.0f)
					{
						Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->earshot = f;
					}
					continue;
				}

				// evasion
				if (!Q_stricmp(token, "evasion"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (n < 1 || n > 5)
					{
						Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->evasion = n;
					}
					continue;
				}

				// hfov
				if (!Q_stricmp(token, "hfov"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (n < 30 || n > 180)
					{
						Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->hfov = n; // / 2;	//FIXME: Why was this being done?!
					}
					continue;
				}

				// intelligence
				if (!Q_stricmp(token, "intelligence"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (n < 1 || n > 5)
					{
						Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->intelligence = n;
					}
					continue;
				}

				// move
				if (!Q_stricmp(token, "move"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (n < 1 || n > 5)
					{
						Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->move = n;
					}
					continue;
				}

				// reactions
				if (!Q_stricmp(token, "reactions"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (n < 1 || n > 5)
					{
						Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->reactions = n;
					}
					continue;
				}

				// shootDistance
				if (!Q_stricmp(token, "shootDistance"))
				{
					if (COM_ParseFloat(&p, &f))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (f < 0.0f)
					{
						Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->shootDistance = f;
					}
					continue;
				}

				// vfov
				if (!Q_stricmp(token, "vfov"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (n < 30 || n > 180)
					{
						Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->vfov = n / 2;
					}
					continue;
				}

				// vigilance
				if (!Q_stricmp(token, "vigilance"))
				{
					if (COM_ParseFloat(&p, &f))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (f < 0.0f)
					{
						Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->vigilance = f;
					}
					continue;
				}

				// visrange
				if (!Q_stricmp(token, "visrange"))
				{
					if (COM_ParseFloat(&p, &f))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (f < 0.0f)
					{
						Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->visrange = f;
					}
					continue;
				}

				// rank
				if (!Q_stricmp(token, "rank"))
				{
					if (COM_ParseString(&p, &value))
					{
						continue;
					}
					if (npc->NPC)
					{
						npc->NPC->rank = TranslateRankName(value);
					}
					continue;
				}
			}

			// health
			if (!Q_stricmp(token, "health"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				if (npc->NPC)
				{
					stats->health = n;
				}
				else if (parsingPlayer)
				{
					npc->client->ps.stats[STAT_MAX_HEALTH] = npc->client->pers.maxHealth = n;
				}
				continue;
			}

			// fullName
			if (!Q_stricmp(token, "fullName"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				npc->fullName = G_NewString(value);
				continue;
			}

			// playerTeam
			if (!Q_stricmp(token, "playerTeam"))
			{
				char tk[4096] = { 0 }; //rww - hackilicious!

				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				Com_sprintf(tk, sizeof tk, "NPC%s", token);
				npc->client->playerTeam = npc->s.teamowner = (team_t)GetIDForString(TeamTable, tk);
				continue;
			}

			// enemyTeam
			if (!Q_stricmp(token, "enemyTeam"))
			{
				char tk[4096] = { 0 }; //rww - hackilicious!

				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				Com_sprintf(tk, sizeof tk, "NPC%s", token);
				npc->client->enemyTeam = (npcteam_t)GetIDForString(TeamTable, tk);
				continue;
			}

			// class
			if (!Q_stricmp(token, "class"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				npc->client->NPC_class = (class_t)GetIDForString(ClassTable, value);
				npc->s.NPC_class = npc->client->NPC_class;
				//we actually only need this value now, but at the moment I don't feel like changing the 200+ references to client->NPC_class.

				// No md3's for vehicles.
				if (npc->client->NPC_class == CLASS_VEHICLE)
				{
					if (!npc->m_pVehicle)
					{
						//you didn't spawn this guy right!
						Com_Printf(
							S_COLOR_RED
							"ERROR: Tried to spawn a vehicle NPC (%s) without using NPC_Vehicle or 'NPC spawn vehicle <vehiclename>'!!!  Bad, bad, bad!  Shame on you!\n",
							npc_name);
						return qfalse;
					}
					md3Model = qfalse;
				}

				continue;
			}

			// dismemberment probability for head
			if (!Q_stricmp(token, "dismemberProbHead"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				if (npc->NPC)
				{
					//	NPC->client->dismemberProbHead = n;
					//rwwFIXMEFIXME: support for this?
				}
				continue;
			}

			// dismemberment probability for arms
			if (!Q_stricmp(token, "dismemberProbArms"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				if (npc->NPC)
				{
					//	NPC->client->dismemberProbArms = n;
				}
				continue;
			}

			// dismemberment probability for hands
			if (!Q_stricmp(token, "dismemberProbHands"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				if (npc->NPC)
				{
					//	NPC->client->dismemberProbHands = n;
				}
				continue;
			}

			// dismemberment probability for waist
			if (!Q_stricmp(token, "dismemberProbWaist"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				if (npc->NPC)
				{
					//	NPC->client->dismemberProbWaist = n;
				}
				continue;
			}

			// dismemberment probability for legs
			if (!Q_stricmp(token, "dismemberProbLegs"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				if (n < 0)
				{
					Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
					continue;
				}
				if (npc->NPC)
				{
					//	NPC->client->dismemberProbLegs = n;
				}
				continue;
			}

			//===MOVEMENT STATS============================================================

			if (!Q_stricmp(token, "width"))
			{
				if (COM_ParseInt(&p, &n))
				{
					continue;
				}

				npc->r.mins[0] = npc->r.mins[1] = -n;
				npc->r.maxs[0] = npc->r.maxs[1] = n;
				continue;
			}

			if (!Q_stricmp(token, "height"))
			{
				if (COM_ParseInt(&p, &n))
				{
					continue;
				}
				if (npc->client->NPC_class == CLASS_VEHICLE
					&& npc->m_pVehicle
					&& npc->m_pVehicle->m_pVehicleInfo
					&& npc->m_pVehicle->m_pVehicleInfo->type == VH_FIGHTER)
				{
					//a flying vehicle's origin must be centered in bbox and it should spawn on the ground
					//trace_t		tr;
					//vec3_t		bottom;
					//float		adjust = 32.0f;
					npc->r.maxs[2] = npc->client->ps.standheight = n / 2.0f;
					npc->r.mins[2] = -npc->r.maxs[2];
					npc->s.origin[2] += DEFAULT_MINS_2 - npc->r.mins[2] + 0.125f;
					VectorCopy(npc->s.origin, npc->client->ps.origin);
					VectorCopy(npc->s.origin, npc->r.currentOrigin);
					G_SetOrigin(npc, npc->s.origin);
					trap->LinkEntity((sharedEntity_t*)npc);
				}
				else
				{
					npc->r.mins[2] = DEFAULT_MINS_2; //Cannot change
					npc->r.maxs[2] = npc->client->ps.standheight = n + DEFAULT_MINS_2;
				}
				npc->radius = n;
				continue;
			}

			if (!Q_stricmp(token, "crouchheight"))
			{
				if (COM_ParseInt(&p, &n))
				{
					continue;
				}

				npc->client->ps.crouchheight = n + DEFAULT_MINS_2;
				continue;
			}

			if (!parsingPlayer)
			{
				if (!Q_stricmp(token, "movetype"))
				{
					if (COM_ParseString(&p, &value))
					{
						continue;
					}
					if (Q_stricmp("flyswim", value) == 0)
					{
						npc->client->ps.eFlags2 |= EF2_FLYING;
					}
					npc->client->moveType = (movetype_t)MoveTypeNameToEnum(value);
					continue;
				}

				// yawSpeed
				if (!Q_stricmp(token, "yawSpeed"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (n <= 0)
					{
						Com_Printf("bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->yawSpeed = (float)n;
					}
					continue;
				}

				// walkSpeed
				if (!Q_stricmp(token, "walkSpeed"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (n < 0)
					{
						Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->walkSpeed = n;
					}
					continue;
				}

				//runSpeed
				if (!Q_stricmp(token, "runSpeed"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (n < 0)
					{
						Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->runSpeed = n;
					}
					continue;
				}

				//acceleration
				if (!Q_stricmp(token, "acceleration"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (n < 0)
					{
						Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						stats->acceleration = n;
					}
					continue;
				}
				//sex - skip in MP
				if (!Q_stricmp(token, "sex"))
				{
					if (COM_ParseString(&p, &value))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (value[0] == 'm')
					{
						//male
						stats->sex = SEX_MALE;
					}
					else if (value[0] == 'n')
					{
						//neutral
						stats->sex = SEX_NEUTRAL;
					}
					else if (value[0] == 'a')
					{
						//asexual?
						stats->sex = SEX_NEUTRAL;
					}
					else if (value[0] == 'f')
					{
						//female
						stats->sex = SEX_FEMALE;
					}
					else if (value[0] == 's')
					{
						//shemale?
						stats->sex = SEX_SHEMALE;
					}
					else if (value[0] == 'h')
					{
						//hermaphrodite?
						stats->sex = SEX_SHEMALE;
					}
					else if (value[0] == 't')
					{
						//transsexual/transvestite?
						stats->sex = SEX_SHEMALE;
					}
					continue;
				}
				//===MISC===============================================================================
				// default behavior
				if (!Q_stricmp(token, "behavior"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (n < BS_DEFAULT || n >= NUM_BSTATES)
					{
						Com_Printf(S_COLOR_YELLOW"WARNING: bad %s in NPC '%s'\n", token, npc_name);
						continue;
					}
					if (npc->NPC)
					{
						npc->NPC->defaultBehavior = (bState_t)n;
					}
					continue;
				}
			}

			// snd
			if (!Q_stricmp(token, "snd"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (!(npc->r.svFlags & SVF_NO_BASIC_SOUNDS))
				{
					//FIXME: store this in some sound field or parse in the soundTable like the animTable...
					Q_strncpyz(sound, value, sizeof sound);
					patch = strstr(sound, "/");
					if (patch)
					{
						*patch = 0;
					}
					//	ci->customBasicSoundDir = G_NewString( sound )
				}
				continue;
			}

			// sndcombat
			if (!Q_stricmp(token, "sndcombat"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (!(npc->r.svFlags & SVF_NO_COMBAT_SOUNDS))
				{
					//FIXME: store this in some sound field or parse in the soundTable like the animTable...
					Q_strncpyz(sound, value, sizeof sound);
					patch = strstr(sound, "/");
					if (patch)
					{
						*patch = 0;
					}
					//	ci->customCombatSoundDir = G_NewString( sound );
				}
				continue;
			}

			// sndextra
			if (!Q_stricmp(token, "sndextra"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (!(npc->r.svFlags & SVF_NO_EXTRA_SOUNDS))
				{
					//FIXME: store this in some sound field or parse in the soundTable like the animTable...
					Q_strncpyz(sound, value, sizeof sound);
					patch = strstr(sound, "/");
					if (patch)
					{
						*patch = 0;
					}
					//	ci->customExtraSoundDir = G_NewString( sound );
				}
				continue;
			}

			// sndjedi
			if (!Q_stricmp(token, "sndjedi"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (!(npc->r.svFlags & SVF_NO_EXTRA_SOUNDS))
				{
					//FIXME: store this in some sound field or parse in the soundTable like the animTable...
					Q_strncpyz(sound, value, sizeof sound);
					patch = strstr(sound, "/");
					if (patch)
					{
						*patch = 0;
					}
					//ci->customJediSoundDir = G_NewString( sound );
				}
				continue;
			}

			// sndcallout
			if (!Q_stricmp(token, "sndcallout"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (!(npc->r.svFlags & SVF_NO_EXTRA_SOUNDS))
				{
					//FIXME: store this in some sound field or parse in the soundTable like the animTable...
					Q_strncpyz(sound, value, sizeof sound);
					patch = strstr(sound, "/");
					if (patch)
					{
						*patch = 0;
					}
					//ci->customJediSoundDir = G_NewString(sound);
				}
				continue;
			}

			//New NPC/jedi stats:
			//starting weapon
			if (!Q_stricmp(token, "weapon"))
			{
				int weap = 0;

				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				//FIXME: need to precache the weapon, too?  (in above func)
				weap = GetIDForString(WPTable, value);

				if (weap == WP_STUN_BATON)
				{
					//  EEEWWWWW!!!!!
					weap = WP_BLASTER;
				}
				if (weap >= WP_NONE && weap <= WP_NUM_WEAPONS)
				{
					npc->client->ps.weapon = weap;
					BG_GiveWeapon(npc->client->ps.stats, npc->client->ps.weapon);
					if (weap > WP_NONE)
					{
						npc->client->ps.ammo[weaponData[npc->client->ps.weapon].ammoIndex] = 100; //FIXME: max ammo!
					}
				}
				continue;
			}

			if (!parsingPlayer)
			{
				//alt_fire
				if (!Q_stricmp(token, "alt_fire"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (npc->NPC)
					{
						if (n != 0)
						{
							npc->NPC->scriptFlags |= SCF_ALT_FIRE;
						}
					}
					continue;
				}
				if (!Q_stricmp(token, "altFire"))
				{
					if (COM_ParseInt(&p, &n))
					{
						SkipRestOfLine(&p);
						continue;
					}
					if (npc->NPC)
					{
						if (n != 0)
						{
							npc->NPC->scriptFlags |= SCF_ALT_FIRE;
						}
					}
					continue;
				}
				//Other unique behaviors/numbers that are currently hardcoded?
			}

			//force powers
			fp = GetIDForString(FPTable, token);
			if (fp >= FP_FIRST && fp < NUM_FORCE_POWERS)
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//FIXME: need to precache the fx, too?  (in above func)
				//cap
				if (n > 5)
				{
					n = 5;
				}
				else if (n < 0)
				{
					n = 0;
				}
				if (n)
				{
					//set
					npc->client->ps.fd.forcePowersKnown |= 1 << fp;
				}
				else
				{
					//clear
					npc->client->ps.fd.forcePowersKnown &= ~(1 << fp);
				}
				npc->client->ps.fd.forcePowerLevel[fp] = n;
				continue;
			}

			//max force power
			if (!Q_stricmp(token, "forcePowerMax"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				npc->client->ps.fd.forcePowerMax = n;
				continue;
			}

			//force regen rate - default is 100ms
			if (!Q_stricmp(token, "forceRegenRate"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
				}
				//NPC->client->ps.forcePowerRegenRate = n;
				continue;
			}

			if (!Q_stricmp(token, "BlockPointRegenRate"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
				}
				//NPC->client->ps.BlockPointRegenRate = n;
				continue;
			}

			//force regen amount - default is 1 (points per second)
			if (!Q_stricmp(token, "forceRegenAmount"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
				}
				//NPC->client->ps.forcePowerRegenAmount = n;
				continue;
			}

			//have a sabers.cfg and just name your saber in your NPCs.cfg/ICARUS script
			//saber name
			if (!Q_stricmp(token, "saber"))
			{
				char* saber_name = NULL;

				if (COM_ParseString(&p, &value))
				{
					continue;
				}

				saber_name = (char*)BG_TempAlloc(4096); //G_NewString( value );
				strcpy(saber_name, value);

				WP_SaberParseParms(saber_name, &npc->client->saber[0]);
				npcSaber1 = G_model_index(va("@%s", saber_name));

				BG_TempFree(4096);
				continue;
			}

			//second saber name
			if (!Q_stricmp(token, "saber2"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}

				if (!(npc->client->saber[0].saberFlags & SFL_TWO_HANDED))
				{
					//can't use a second saber if first one is a two-handed saber...?
					char* saber_name = BG_TempAlloc(4096); //G_NewString( value );
					strcpy(saber_name, value);

					WP_SaberParseParms(saber_name, &npc->client->saber[1]);
					if (npc->client->saber[1].saberFlags & SFL_TWO_HANDED)
					{
						//tsk tsk, can't use a twoHanded saber as second saber
						WP_RemoveSaber(npc->client->saber, 1);
					}
					else
					{
						//NPC->client->ps.dualSabers = qtrue;
						npcSaber2 = G_model_index(va("@%s", saber_name));
					}
					BG_TempFree(4096);
				}
				continue;
			}

			// saberColor
			if (!Q_stricmp(token, "saberColor"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					saber_colors_t color = TranslateSaberColor(value);
					for (n = 0; n < MAX_BLADES; n++)
					{
						npc->client->saber[0].blade[n].color = color;
						npc->s.boltToPlayer = npc->s.boltToPlayer & 0x38; //(111000)
						npc->s.boltToPlayer += color + 1;
					}
				}
				continue;
			}

			if (!Q_stricmp(token, "saberColor2"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[0].blade[1].color = TranslateSaberColor(value);
				}
				continue;
			}

			if (!Q_stricmp(token, "saberColor3"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[0].blade[2].color = TranslateSaberColor(value);
				}
				continue;
			}

			if (!Q_stricmp(token, "saberColor4"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[0].blade[3].color = TranslateSaberColor(value);
				}
				continue;
			}

			if (!Q_stricmp(token, "saberColor5"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[0].blade[4].color = TranslateSaberColor(value);
				}
				continue;
			}

			if (!Q_stricmp(token, "saberColor6"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[0].blade[5].color = TranslateSaberColor(value);
				}
				continue;
			}

			if (!Q_stricmp(token, "saberColor7"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[0].blade[6].color = TranslateSaberColor(value);
				}
				continue;
			}

			if (!Q_stricmp(token, "saberColor8"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[0].blade[7].color = TranslateSaberColor(value);
				}
				continue;
			}

			if (!Q_stricmp(token, "saber2Color"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					saber_colors_t color = TranslateSaberColor(value);
					for (n = 0; n < MAX_BLADES; n++)
					{
						npc->client->saber[1].blade[n].color = color;
						npc->s.boltToPlayer = npc->s.boltToPlayer & 0x7; //(000111)
						npc->s.boltToPlayer += ((color + 1) << 3);
					}
				}
				continue;
			}

			if (!Q_stricmp(token, "saber2Color2"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[1].blade[1].color = TranslateSaberColor(value);
				}
				continue;
			}

			if (!Q_stricmp(token, "saber2Color3"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[1].blade[2].color = TranslateSaberColor(value);
				}
				continue;
			}

			if (!Q_stricmp(token, "saber2Color4"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[1].blade[3].color = TranslateSaberColor(value);
				}
				continue;
			}

			if (!Q_stricmp(token, "saber2Color5"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[1].blade[4].color = TranslateSaberColor(value);
				}
				continue;
			}

			if (!Q_stricmp(token, "saber2Color6"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[1].blade[5].color = TranslateSaberColor(value);
				}
				continue;
			}

			if (!Q_stricmp(token, "saber2Color7"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[1].blade[6].color = TranslateSaberColor(value);
				}
				continue;
			}

			if (!Q_stricmp(token, "saber2Color8"))
			{
				if (COM_ParseString(&p, &value))
				{
					continue;
				}
				if (npc->client)
				{
					npc->client->saber[1].blade[7].color = TranslateSaberColor(value);
				}
				continue;
			}

			//saber length
			if (!Q_stricmp(token, "saberLength"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}

				for (n = 0; n < MAX_BLADES; n++)
				{
					npc->client->saber[0].blade[n].lengthMax = f;
				}
				continue;
			}

			if (!Q_stricmp(token, "saberLength2"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[0].blade[1].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saberLength3"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[0].blade[2].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saberLength4"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[0].blade[3].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saberLength5"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[0].blade[4].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saberLength6"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[0].blade[5].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saberLength7"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[0].blade[6].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saberLength8"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[0].blade[7].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Length"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[1].blade[0].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Length2"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[1].blade[1].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Length3"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[1].blade[2].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Length4"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[1].blade[3].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Length5"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[1].blade[4].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Length6"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[1].blade[5].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Length7"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[1].blade[6].lengthMax = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Length8"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 4.0f)
				{
					f = 4.0f;
				}
				npc->client->saber[1].blade[7].lengthMax = f;
				continue;
			}

			//saber radius
			if (!Q_stricmp(token, "saberRadius"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				for (n = 0; n < MAX_BLADES; n++)
				{
					npc->client->saber[0].blade[n].radius = f;
				}
				continue;
			}

			if (!Q_stricmp(token, "saberRadius2"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[0].blade[1].radius = f;
				continue;
			}

			if (!Q_stricmp(token, "saberRadius3"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[0].blade[2].radius = f;
				continue;
			}

			if (!Q_stricmp(token, "saberRadius4"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[0].blade[3].radius = f;
				continue;
			}

			if (!Q_stricmp(token, "saberRadius5"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[0].blade[4].radius = f;
				continue;
			}

			if (!Q_stricmp(token, "saberRadius6"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[0].blade[5].radius = f;
				continue;
			}

			if (!Q_stricmp(token, "saberRadius7"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[0].blade[6].radius = f;
				continue;
			}

			if (!Q_stricmp(token, "saberRadius8"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[0].blade[7].radius = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Radius"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				for (n = 0; n < MAX_BLADES; n++)
				{
					npc->client->saber[1].blade[n].radius = f;
				}
				continue;
			}

			if (!Q_stricmp(token, "saber2Radius2"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[1].blade[1].radius = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Radius3"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[1].blade[2].radius = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Radius4"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[1].blade[3].radius = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Radius5"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[1].blade[4].radius = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Radius6"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[1].blade[5].radius = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Radius7"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[1].blade[6].radius = f;
				continue;
			}

			if (!Q_stricmp(token, "saber2Radius8"))
			{
				if (COM_ParseFloat(&p, &f))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap
				if (f < 0.25f)
				{
					f = 0.25f;
				}
				npc->client->saber[1].blade[7].radius = f;
				continue;
			}

			//ADD:
			//saber sounds (on, off, loop)
			//loop sound (like Vader's breathing or droid bleeps, etc.)

			//starting saber style
			if (!Q_stricmp(token, "saberStyle"))
			{
				if (COM_ParseInt(&p, &n))
				{
					SkipRestOfLine(&p);
					continue;
				}
				//cap - as SP (2026-10-15): every style up to SS_BLASTER; each line adds a known style
				// (client->saberStylesKnown, G_ValidSaberStyle) and the last one is the starting style
				if (n > SS_BLASTER)
				{
					n = SS_BLASTER;
				}
				else if (n < SS_FAST)
				{
					n = SS_FAST;
				}
				npc->client->saberStylesKnown |= 1 << n;
				npc->client->ps.fd.saberAnimLevel = n;
				continue;
			}

			if (!parsingPlayer)
			{
				Com_Printf("WARNING: unknown keyword '%s' while parsing '%s'\n", token, npc_name);
			}
			SkipRestOfLine(&p);
		}
	}

	/*
	Ghoul2 Insert Start
	*/
	if (!md3Model)
	{
		qboolean setTypeBack = qfalse;

		if (Q_stricmp("player", playerModel) == 0)
		{
			//set up the playerModel to be the playerModel of the entity with the player script_targetname
			// (a Force Projection copy: its caster, w_force.c)
			extern gentity_t* g_projectionCaster;
			gentity_t* player = g_projectionCaster ? g_projectionCaster : G_Find(NULL, FOFS(script_targetname), "player");
			char* s = NULL;

			if (!player)
			{
				//no players have spawned in yet.  As such, wing it by using jan
				int scan = 0, rgb[3] = { 255, 255, 255 };
				char* s2 = NULL;
				Q_strncpyz(playerModel, g_spmodel.string, sizeof playerModel);
				s2 = va("%s", g_spmodelrgb.string);
				scan = sscanf(s2, "%i %i %i", &rgb[0], &rgb[1], &rgb[2]);
				*s2 = '\0';
				if (scan != 3)
				{
					rgb[0] = 255;
					rgb[1] = 255;
					rgb[2] = 255;
				}
				if (rgb[0] + rgb[1] + rgb[2] < 100) // too dark
					rgb[0] = rgb[1] = rgb[2] = 255;
				npc->client->ps.customRGBA[0] = rgb[0];
				npc->client->ps.customRGBA[1] = rgb[1];
				npc->client->ps.customRGBA[2] = rgb[2];
			}
			else
			{
				char userinfo[MAX_INFO_STRING] = { 0 };
				trap->GetUserinfo(player->s.number, userinfo, sizeof userinfo);
				Q_strncpyz(playerModel, Info_ValueForKey(userinfo, "model"), sizeof playerModel);
				//copy over the customRGB data
				npc->client->ps.customRGBA[0] = player->client->ps.customRGBA[0];
				npc->client->ps.customRGBA[1] = player->client->ps.customRGBA[1];
				npc->client->ps.customRGBA[2] = player->client->ps.customRGBA[2];
			}

			//parse custom skin if there is one.
			s = Q_strrchr(playerModel, '/');
			if (s)
			{
				//model has custom skin
				s++;
				strcpy(customSkin, s);
				s--;
				*s = '\0';
			}
		}

		if (npcSaber1 == 0)
		{
			//use "kyle" for a default then
			npcSaber1 = G_model_index("@Kyle");
			WP_SaberParseParms(DEFAULT_SABER, &npc->client->saber[0]);
		}

		npc->s.npcSaber1 = npcSaber1;
		npc->s.npcSaber2 = npcSaber2;

		if (!customSkin[0])
		{
			strcpy(customSkin, "default");
		}

		if (npc->client && npc->client->NPC_class == CLASS_VEHICLE)
		{
			//vehicles want their names fed in as models
			//we put the $ in front to indicate a name and not a model
			strcpy(playerModel, va("$%s", npc_name));
		}
		if (npc->client)
		{
			Q_strncpyz(npc->client->modelname, playerModel, sizeof npc->client->modelname);	// G_SaberBlasterKyleModel
		}
		SetupGameGhoul2Model(npc, playerModel, customSkin);

		if (!npc->NPC_type)
		{
			//just do this for now so NPC_Precache can see the name.
			npc->NPC_type = (char*)npc_name;
			setTypeBack = qtrue;
		}

		NPC_Precache(npc); //this will just soundindex some values for sounds on the client,

		if (setTypeBack)
		{
			//don't want this being set if we aren't ready yet.
			npc->NPC_type = NULL;
		}
	}
	else
	{
		npc->s.modelIndex = G_model_index(va("models/players/%s/lower.md3", playerModel));
	}

	return qtrue;
}

#define NPC_MP_FOLDER "ext_data/mpnpcs"
#define NPC_SP_OUTCAST_FOLDER "ext_data/jkonpcs" // SJE SP's Jedi Outcast (and Dark Forces) NPCs
#define NPC_SP_FOLDERS "ext_data/npcs", "ext_data/jkonpcs"
char npcParseBuffer[MAX_NPC_DATA_SIZE];

/*
-------------------------
SP's NPC definitions in MP (2026-11-18, DH: kejim_post as it plays in SP)

MP's NPCs come from NPC_MP_FOLDER only. On a Jedi Outcast map in a mission (g_missionSPNPCs 1; 2: in any game on
those maps; 0: never) NPC_SP_OUTCAST_FOLDER, the folder SP plays Jedi Outcast with, is read first, so its NPCs are
SP's (the first one of a name is the one used): SJE's mpnpcs stormtroopers have 10 health (SP's 60), its "mouse" is a
red saber Reborn (the kejim_post mouse droid), its imperials and probes have other guns and voices. NPC_MP_FOLDER keeps
its room: what of the SP folder doesn't fit is left out. An NPC type not in NPCParms at all (an SP cutscene actor,
MD's _jk2 troopers) is looked for in NPC_SP_FOLDERS when one is spawned, and added (NPC_AddMissingParms).
-------------------------
*/
// one folder's .npc files added to NPCParms[totallen], up to room bytes (must_fit: an error if one doesn't fit, as
// before; else it is left out); returns the new length
static int NPC_LoadParmsFolder(const char* folder, int totallen, const int room, const qboolean must_fit)
{
	// static: too big for the stack, and BG_Alloc is never freed: asked for on every call, this ran the pool dry
	static char npcExtensionListBuf[16384];
	int npcExtFNLen = 0;
	char* marker = NPCParms + totallen;
	char* holdChar = npcExtensionListBuf;
	const int fileCnt = trap->FS_GetFileList(folder, ".npc", npcExtensionListBuf, sizeof npcExtensionListBuf);
	int i = 0;

	*marker = 0;
	for (i = 0; i < fileCnt; i++, holdChar += npcExtFNLen + 1)
	{
		fileHandle_t f = 0;
		int len = 0;

		npcExtFNLen = strlen(holdChar);

		len = trap->FS_Open(va("%s/%s", folder, holdChar), &f, FS_READ);
		if (len == -1 || !f)
		{
			Com_Printf("NPC_LoadParms: error reading file %s\n", holdChar);
			continue;
		}

		// Check space
		if (totallen + len + 1 >= room || len >= MAX_NPC_DATA_SIZE)
		{
			trap->FS_Close(f);
			if (must_fit)
			{
				trap->Error(ERR_DROP,
					"NPC_LoadParms: ran out of space before reading %s\n(you must make the .npc files smaller)",
					holdChar);
			}
			Com_Printf(S_COLOR_YELLOW "NPC_LoadParms: no room for %s/%s, left out\n", folder, holdChar);
			continue;
		}

		// Read file
		trap->FS_Read(npcParseBuffer, len, f);
		npcParseBuffer[len] = 0;
		trap->FS_Close(f);

		// Compress
		len = COM_Compress(npcParseBuffer);

		// Append to NPCParms
		strcat(marker, npcParseBuffer);
		strcat(marker, "\n");
		len++;

		totallen += len;
		marker = NPCParms + totallen;
	}
	return totallen;
}

// the space a folder's .npc files take (uncompressed)
static int NPC_ParmsFolderSize(const char* folder)
{
	static char npcSizeListBuf[16384];
	int npcExtFNLen = 0, size = 0, i = 0;
	char* holdChar = npcSizeListBuf;
	const int fileCnt = trap->FS_GetFileList(folder, ".npc", npcSizeListBuf, sizeof npcSizeListBuf);

	for (i = 0; i < fileCnt; i++, holdChar += npcExtFNLen + 1)
	{
		fileHandle_t f = 0;
		const int len = trap->FS_Open(va("%s/%s", folder, holdChar), &f, FS_READ);

		npcExtFNLen = strlen(holdChar);
		if (f)
		{
			trap->FS_Close(f);
		}
		if (len > 0)
		{
			size += len + 1;
		}
	}
	return size;
}

extern qboolean G_IsOutcastSPMap(const char* mapname);

// the SP folder whose NPCs come first on this map (NULL: none)
static const char* NPC_MissionSPFolder(void)
{
	char mapname[MAX_QPATH] = { 0 };

	if (!NPC_SP_OUTCAST_FOLDER[0] || g_missionSPNPCs.integer <= 0
		|| (g_missionSPNPCs.integer < 2 && level.gametype != GT_SINGLE_PLAYER))
	{
		return NULL;
	}
	trap->Cvar_VariableStringBuffer("mapname", mapname, sizeof mapname);
	return G_IsOutcastSPMap(mapname) ? NPC_SP_OUTCAST_FOLDER : NULL;
}

static int npcParmsLen; // NPCParms' length
static char npcMissing[64][MAX_QPATH]; // types not found in NPC_SP_FOLDERS either
static int npcNumMissing;

// Load NPC extension files from NPC_MP_FOLDER (after SP's on a Jedi Outcast mission) into NPCParms.
void NPC_LoadParms(void)
{
	const char* sp_folder = NPC_MissionSPFolder();
	int totallen = 0;

	NPCParms[0] = 0;
	npcNumMissing = 0;
	if (sp_folder)
	{
		const int room = MAX_NPC_DATA_SIZE - NPC_ParmsFolderSize(NPC_MP_FOLDER) - 1;

		totallen = NPC_LoadParmsFolder(sp_folder, 0, room, qfalse);
		Com_Printf("NPC_LoadParms: a Jedi Outcast mission: %s's NPCs first (g_missionSPNPCs)\n", sp_folder);
	}
	npcParmsLen = NPC_LoadParmsFolder(NPC_MP_FOLDER, totallen, MAX_NPC_DATA_SIZE, qtrue);
}

// an NPC type NPCParms doesn't have, looked for in SP's NPC folders and added to NPCParms; qtrue: it's there now
qboolean NPC_AddMissingParms(const char* npc_name)
{
	static const char* folders[] = { NPC_SP_FOLDERS };
	static char npcMissingListBuf[16384];
	int i = 0, k = 0;

	if (!npc_name || !npc_name[0] || !Q_stricmp(npc_name, "random"))
	{
		return qfalse;
	}
	for (i = 0; i < npcNumMissing; i++)
	{
		if (!Q_stricmp(npcMissing[i], npc_name))
		{
			return qfalse; // looked for already
		}
	}

	for (k = 0; k < (int)ARRAY_LEN(folders); k++)
	{
		int npcExtFNLen = 0;
		char* holdChar = npcMissingListBuf;
		const int fileCnt = trap->FS_GetFileList(folders[k], ".npc", npcMissingListBuf, sizeof npcMissingListBuf);

		for (i = 0; i < fileCnt; i++, holdChar += npcExtFNLen + 1)
		{
			fileHandle_t f = 0;
			const int len = trap->FS_Open(va("%s/%s", folders[k], holdChar), &f, FS_READ);
			const char* p = NULL;

			npcExtFNLen = strlen(holdChar);
			if (!f)
			{
				continue;
			}
			if (len <= 0 || len >= MAX_NPC_DATA_SIZE)
			{
				trap->FS_Close(f);
				continue;
			}
			trap->FS_Read(npcParseBuffer, len, f);
			npcParseBuffer[len] = 0;
			trap->FS_Close(f);
			COM_Compress(npcParseBuffer);

			p = npcParseBuffer;
			COM_BeginParseSession("NPC_AddMissingParms");
			while (p)
			{
				const char* token = COM_ParseExt(&p, qtrue);
				const char* block = NULL;

				if (!token[0])
				{
					break;
				}
				if (Q_stricmp(token, npc_name))
				{
					SkipBracedSection(&p, 0);
					continue;
				}
				// found it: "name { ... }" added to NPCParms
				block = p;
				SkipBracedSection(&p, 0);
				if (p && p > block)
				{
					const int name_len = strlen(npc_name);
					const int block_len = p - block;

					if (npcParmsLen + name_len + block_len + 3 >= MAX_NPC_DATA_SIZE)
					{
						Com_Printf(S_COLOR_YELLOW "NPC_AddMissingParms: no room for NPC '%s'\n", npc_name);
						return qfalse;
					}
					NPCParms[npcParmsLen++] = '\n';
					memcpy(NPCParms + npcParmsLen, npc_name, name_len);
					npcParmsLen += name_len;
					memcpy(NPCParms + npcParmsLen, block, block_len);
					npcParmsLen += block_len;
					NPCParms[npcParmsLen++] = '\n';
					NPCParms[npcParmsLen] = 0;
					Com_Printf("NPC '%s' isn't in %s: using %s/%s\n", npc_name, NPC_MP_FOLDER, folders[k], holdChar);
					return qtrue;
				}
				break;
			}
		}
	}

	if (npcNumMissing < (int)ARRAY_LEN(npcMissing))
	{
		Q_strncpyz(npcMissing[npcNumMissing++], npc_name, sizeof npcMissing[0]);
	}
	return qfalse;
}

extern void npc_shadow_trooper_precache(void);
extern void NPC_Gonk_Precache(void);
extern void NPC_Mouse_Precache(void);
extern void NPC_Seeker_Precache(void);
extern void NPC_Remote_Precache(void);
extern void NPC_R2D2_Precache(void);
extern void NPC_R5D2_Precache(void);
extern void NPC_Probe_Precache(void);
extern void NPC_Interrogator_Precache(void);
extern void NPC_MineMonster_Precache(void);
extern void NPC_Howler_Precache(void);
extern void NPC_Rancor_Precache(void);
extern void NPC_MutantRancor_Precache(void);
extern void NPC_MutantRancor_Precache(void);
extern void NPC_Wampa_Precache(void);
extern void NPC_ATST_Precache(void);
extern void NPC_Sentry_Precache(void);
extern void NPC_Mark1_Precache(void);
extern void NPC_Mark2_Precache(void);
extern void NPC_Protocol_Precache(void);
extern void Boba_Precache(void);
extern void RT_Precache(void);
extern void SandCreature_Precache(void);
extern void NPC_TavionScepter_Precache(void);
extern void NPC_TavionSithSword_Precache(void);
extern void npc_rosh_dark_precache(void);
extern void NPC_Tusken_Precache(void);
extern void NPC_Saboteur_Precache(void);
extern void npc_cultist_destroyer_precache(void);
extern void NPC_GalakMech_Precache(void);

static void NPC_Jawa_Precache(void)
{
	for (int i = 1; i < 7; i++)
	{
		G_SoundIndex(va("sound/chars/jawa/misc/chatter%d.wav", i));
	}
	G_SoundIndex("sound/chars/jawa/misc/ooh-tee-nee.wav");
}

void NPC_Tusken_Precache(void)
{
	for (int i = 1; i < 5; i++)
	{
		G_SoundIndex(va("sound/weapons/tusken_staff/stickhit%d.wav", i));
	}
}

void NPC_PrecacheByClassName(const char* type)
{
	//Precashes the extra fx/effects/etc used by special NPCs
	if (!type || !type[0])
	{
		return;
	}

	if (!Q_stricmp("gonk", type))
	{
		NPC_Gonk_Precache();
	}
	else if (!Q_stricmp("mouse", type))
	{
		NPC_Mouse_Precache();
	}
	else if (!Q_stricmpn("r2d2", type, 4))
	{
		NPC_R2D2_Precache();
	}
	else if (!Q_stricmp("atst", type))
	{
		NPC_ATST_Precache();
	}
	else if (!Q_stricmpn("r5d2", type, 4))
	{
		NPC_R5D2_Precache();
	}
	else if (!Q_stricmp("mark1", type))
	{
		NPC_Mark1_Precache();
	}
	else if (!Q_stricmp("mark2", type))
	{
		NPC_Mark2_Precache();
	}
	else if (!Q_stricmp("interrogator", type))
	{
		NPC_Interrogator_Precache();
	}
	else if (!Q_stricmp("probe", type))
	{
		NPC_Probe_Precache();
	}
	else if (!Q_stricmp("seeker", type))
	{
		NPC_Seeker_Precache();
	}
	else if (!Q_stricmpn("remote", type, 6))
	{
		NPC_Remote_Precache();
	}
	else if (!Q_stricmpn("shadowtrooper", type, 13))
	{
		npc_shadow_trooper_precache();
	}
	else if (!Q_stricmp("minemonster", type))
	{
		NPC_MineMonster_Precache();
	}
	else if (!Q_stricmp("howler", type))
	{
		NPC_Howler_Precache();
	}
	else if (!Q_stricmp("sentry", type))
	{
		NPC_Sentry_Precache();
	}
	else if (!Q_stricmpn("protocol", type, 8))
	{
		NPC_Protocol_Precache();
	}
	else if (!Q_stricmp("galak_mech", type))
	{
		NPC_GalakMech_Precache();
	}
	else if (!Q_stricmp("wampa", type))
	{
		NPC_Wampa_Precache();
	}
	else if (!Q_stricmp("rancor", type))
	{
		NPC_Rancor_Precache();
	}
	else if (!Q_stricmp("mutant_rancor", type))
	{
		NPC_Rancor_Precache();
		NPC_MutantRancor_Precache(); // (SP: breath attack)
	}
	else if (!Q_stricmp("sand_creature", type))
	{
		SandCreature_Precache();
	}
	else if (!Q_stricmp("boba_fett", type))
	{
		Boba_Precache();
	}
	else if (!Q_stricmp("rockettrooper2", type))
	{
		RT_Precache();
	}
	else if (!Q_stricmp("rockettrooper2Officer", type))
	{
		RT_Precache();
	}
	else if (!Q_stricmp("tavion_scepter", type))
	{
		NPC_TavionScepter_Precache();
	}
	else if (!Q_stricmp("tavion_sith_sword", type))
	{
		NPC_TavionSithSword_Precache();
	}
	else if (!Q_stricmp("rosh_dark", type))
	{
		npc_rosh_dark_precache();
	}
	else if (!Q_stricmpn("tusken", type, 6))
	{
		NPC_Tusken_Precache();
	}
	else if (!Q_stricmpn("saboteur", type, 8))
	{
		NPC_Saboteur_Precache();
	}
	else if (!Q_stricmp("cultist_destroyer", type))
	{
		npc_cultist_destroyer_precache();
	}
	else if (!Q_stricmpn("jawa", type, 4))
	{
		NPC_Jawa_Precache();
	}
}