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

#include "g_local.h"

//==========================================================

/*QUAKED target_give (1 0 0) (-8 -8 -8) (8 8 8)
Gives the activator all the items pointed to.
*/
void Use_Target_Give(const gentity_t* self, gentity_t* other, gentity_t* activator)
{
	trace_t trace;

	if (!activator->client)
	{
		return;
	}

	if (!self->target)
	{
		return;
	}

	memset(&trace, 0, sizeof trace);
	gentity_t* t = NULL;
	while ((t = G_Find(t, FOFS(targetname), self->target)) != NULL)
	{
		if (!t->item)
		{
			continue;
		}
		Touch_Item(t, activator, &trace);

		// make sure it isn't going to respawn or show any events
		t->nextthink = 0;
		trap->UnlinkEntity((sharedEntity_t*)t);
	}
}

void SP_target_give(gentity_t* ent)
{
	ent->use = Use_Target_Give;
}

//==========================================================

/*QUAKED target_remove_powerups (1 0 0) (-8 -8 -8) (8 8 8)
takes away all the activators powerups.
Used to drop flight powerups into death puts.
*/
static void Use_target_remove_powerups(gentity_t* ent, gentity_t* other, const gentity_t* activator)
{
	if (!activator->client)
	{
		return;
	}

	if (activator->client->ps.powerups[PW_REDFLAG])
	{
		Team_ReturnFlag(TEAM_RED);
	}
	else if (activator->client->ps.powerups[PW_BLUEFLAG])
	{
		Team_ReturnFlag(TEAM_BLUE);
	}
	else if (activator->client->ps.powerups[PW_NEUTRALFLAG])
	{
		Team_ReturnFlag(TEAM_FREE);
	}

	memset(activator->client->ps.powerups, 0, sizeof activator->client->ps.powerups);
}

void SP_target_remove_powerups(gentity_t* ent)
{
	ent->use = Use_target_remove_powerups;
}

//==========================================================

/*QUAKED target_delay (1 0 0) (-8 -8 -8) (8 8 8) NO_RETRIGGER

NO_RETRIGGER - Keeps the delay from resetting the time if it is
activated again while it is counting down to an event.

"wait" seconds to pause before firing targets.
"random" delay variance, total delay = delay +/- random seconds
*/
void Think_Target_Delay(gentity_t* ent)
{
	G_UseTargets(ent, ent->activator);
}

void Use_Target_Delay(gentity_t* ent, gentity_t* other, gentity_t* activator)
{
	if (ent->nextthink > level.time && ent->spawnflags & 1)
	{
		//Leave me alone, I am thinking.
		return;
	}
	G_ActivateBehavior(ent, BSET_USE);
	ent->nextthink = level.time + (ent->wait + ent->random * Q_flrand(-1.0f, 1.0f)) * 1000;
	ent->think = Think_Target_Delay;
	ent->activator = activator;
}

void SP_target_delay(gentity_t* ent)
{
	// check delay for backwards compatability
	if (!G_SpawnFloat("delay", "0", &ent->wait))
	{
		G_SpawnFloat("wait", "1", &ent->wait);
	}

	if (!ent->wait)
	{
		ent->wait = 1;
	}
	ent->use = Use_Target_Delay;
}

//==========================================================

/*QUAKED target_score (1 0 0) (-8 -8 -8) (8 8 8)
"count" number of points to add, default 1

The activator is given this many points.
*/
void Use_Target_Score(const gentity_t* ent, gentity_t* other, const gentity_t* activator)
{
	AddScore(activator, ent->count);
}

void SP_target_score(gentity_t* ent)
{
	if (!ent->count)
	{
		ent->count = 1;
	}
	ent->use = Use_Target_Score;
}

//==========================================================

/*QUAKED target_print (1 0 0) (-8 -8 -8) (8 8 8) redteam blueteam private
"message"	text to print
"wait"		don't fire off again if triggered within this many milliseconds ago
If "private", only the activator gets the message.  If no checks, all clients get the message.
*/
void Use_Target_Print(gentity_t* ent, gentity_t* other, const gentity_t* activator)
{
	if (!ent || !ent->inuse)
	{
		Com_Printf("ERROR: Bad ent in Use_Target_Print");
		return;
	}

	if (ent->wait)
	{
		if (ent->genericValue14 >= level.time)
		{
			return;
		}
		ent->genericValue14 = level.time + ent->wait;
	}

	// (the debug build's checks - no activator: nothing printed; two prints within 5 s: ERR_DROP - are gone: the SP
	// maps' scripts fire prints with no activator and close together, and a mission's messages must show)

	G_ActivateBehavior(ent, BSET_USE);
	if (!ent->message || !ent->message[0])
	{
		return; // nothing to print
	}
	if (ent->spawnflags & 4)
	{
		//private, to one client only
		if (!activator || !activator->inuse)
		{
			Com_Printf("ERROR: Bad activator in Use_Target_Print");
		}
		if (activator && activator->client)
		{
			//make sure there's a valid client ent to send it to
			if (ent->message[0] == '@' && ent->message[1] != '@')
			{
				trap->SendServerCommand(activator - g_entities, va("cps \"%s\"", ent->message));
			}
			else
			{
				trap->SendServerCommand(activator - g_entities, va("cp \"%s\"", ent->message));
			}
		}
		//NOTE: change in functionality - if there *is* no valid client ent, it won't send it to anyone at all
		return;
	}

	if (ent->spawnflags & 3)
	{
		if (ent->spawnflags & 1)
		{
			if (ent->message[0] == '@' && ent->message[1] != '@')
			{
				G_TeamCommand(TEAM_RED, va("cps \"%s\"", ent->message));
			}
			else
			{
				G_TeamCommand(TEAM_RED, va("cp \"%s\"", ent->message));
			}
		}
		if (ent->spawnflags & 2)
		{
			if (ent->message[0] == '@' && ent->message[1] != '@')
			{
				G_TeamCommand(TEAM_BLUE, va("cps \"%s\"", ent->message));
			}
			else
			{
				G_TeamCommand(TEAM_BLUE, va("cp \"%s\"", ent->message));
			}
		}
		return;
	}

	if (ent->message[0] == '@' && ent->message[1] != '@')
	{
		trap->SendServerCommand(-1, va("cps \"%s\"", ent->message));
	}
	else
	{
		trap->SendServerCommand(-1, va("cp \"%s\"", ent->message));
	}
}

void SP_target_print(gentity_t* ent)
{
	ent->use = Use_Target_Print;
}

//==========================================================

/*QUAKED target_speaker (1 0 0) (-8 -8 -8) (8 8 8) looped-on looped-off global activator
"noise"		wav file to play

A global sound will play full volume throughout the level.
Activator sounds will play on the player that activated the target.
Global and activator sounds can't be combined with looping.
Normal sounds play each time the target is used.
Looped sounds will be toggled by use functions.
Multiple identical looping sounds will just increase volume without any speed cost.
"wait" : Seconds between auto triggerings, 0 = don't auto trigger
"random"	wait variance, default is 0
*/
void Use_Target_Speaker(gentity_t* ent, gentity_t* other, gentity_t* activator)
{
	G_ActivateBehavior(ent, BSET_USE);

	if (ent->spawnflags & 3)
	{
		// looping sound toggles
		if (ent->s.loopSound)
		{
			ent->s.loopSound = 0; // turn it off
			ent->s.loopIsSoundset = qfalse;
			ent->s.trickedentindex = 1;
		}
		else
		{
			ent->s.loopSound = ent->noise_index; // start it
			ent->s.loopIsSoundset = qfalse;
			ent->s.trickedentindex = 0;
		}
	}
	else
	{
		// normal sound
		if (ent->spawnflags & 8)
		{
			G_AddEvent(activator, EV_GENERAL_SOUND, ent->noise_index);
		}
		else if (ent->spawnflags & 4)
		{
			G_AddEvent(ent, EV_GLOBAL_SOUND, ent->noise_index);
		}
		else
		{
			G_AddEvent(ent, EV_GENERAL_SOUND, ent->noise_index);
		}
	}
}

void SP_target_speaker(gentity_t* ent)
{
	char buffer[MAX_QPATH];
	char* s = NULL;

	G_SpawnFloat("wait", "0", &ent->wait);
	G_SpawnFloat("random", "0", &ent->random);

	if (G_SpawnString("soundSet", "", &s))
	{
		// this is a sound set
		ent->s.soundSetIndex = G_SoundSetIndex(s);
		ent->s.eFlags = EF_PERMANENT;
		VectorCopy(ent->s.origin, ent->s.pos.trBase);
		trap->LinkEntity((sharedEntity_t*)ent);
		return;
	}

	if (!G_SpawnString("noise", "NOSOUND", &s))
	{
		trap->Error(ERR_DROP, "target_speaker without a noise key at %s", vtos(ent->s.origin));
	}

	// force all client reletive sounds to be "activator" speakers that
	// play on the entity that activates it
	if (s[0] == '*')
	{
		ent->spawnflags |= 8;
	}

	Q_strncpyz(buffer, s, sizeof buffer);

	ent->noise_index = G_SoundIndex(buffer);

	// a repeating speaker can be done completely client side
	ent->s.eType = ET_SPEAKER;
	ent->s.eventParm = ent->noise_index;
	ent->s.frame = ent->wait * 10;
	ent->s.clientNum = ent->random * 10;

	// check for prestarted looping sound
	if (ent->spawnflags & 1)
	{
		ent->s.loopSound = ent->noise_index;
		ent->s.loopIsSoundset = qfalse;
	}

	ent->use = Use_Target_Speaker;

	if (ent->spawnflags & 4)
	{
		ent->r.svFlags |= SVF_BROADCAST;
	}

	VectorCopy(ent->s.origin, ent->s.pos.trBase);

	// must link the entity so we get areas and clusters so
	// the server can determine who to send updates to
	trap->LinkEntity((sharedEntity_t*)ent);
}

//==========================================================

/*QUAKED target_laser (0 .5 .8) (-8 -8 -8) (8 8 8) START_ON
When triggered, fires a laser.  You can either set a target or a direction.
*/
void target_laser_think(gentity_t* ent)
{
	vec3_t end;
	trace_t tr;

	// if pointed at another entity, set movedir to point at it
	if (ent->enemy)
	{
		vec3_t point;
		VectorMA(ent->enemy->s.origin, 0.5, ent->enemy->r.mins, point);
		VectorMA(point, 0.5, ent->enemy->r.maxs, point);
		VectorSubtract(point, ent->s.origin, ent->movedir);
		VectorNormalize(ent->movedir);
	}

	// fire forward and see what we hit
	VectorMA(ent->s.origin, 2048, ent->movedir, end);

	trap->Trace(&tr, ent->s.origin, NULL, NULL, end, ent->s.number, CONTENTS_SOLID | CONTENTS_BODY | CONTENTS_CORPSE,
		qfalse, 0, 0);

	if (tr.entityNum)
	{
		// hurt it if we can
		G_Damage(&g_entities[tr.entityNum], ent, ent->activator, ent->movedir,
			tr.endpos, ent->damage, DAMAGE_NO_KNOCKBACK, MOD_TARGET_LASER);
	}

	VectorCopy(tr.endpos, ent->s.origin2);

	trap->LinkEntity((sharedEntity_t*)ent);
	ent->nextthink = level.time + FRAMETIME;
}

static void target_laser_on(gentity_t* self)
{
	if (!self->activator)
		self->activator = self;
	target_laser_think(self);
}

static void target_laser_off(gentity_t* self)
{
	trap->UnlinkEntity((sharedEntity_t*)self);
	self->nextthink = 0;
}

void target_laser_use(gentity_t* self, gentity_t* other, gentity_t* activator)
{
	self->activator = activator;
	if (self->nextthink > 0)
		target_laser_off(self);
	else
		target_laser_on(self);
}

void target_laser_start(gentity_t* self)
{
	self->s.eType = ET_BEAM;

	if (self->target)
	{
		gentity_t* ent = G_Find(NULL, FOFS(targetname), self->target);
		if (!ent)
		{
			trap->Print("%s at %s: %s is a bad target\n", self->classname, vtos(self->s.origin), self->target);
		}
		self->enemy = ent;
	}
	else
	{
		G_SetMovedir(self->s.angles, self->movedir);
	}

	self->use = target_laser_use;
	self->think = target_laser_think;

	if (!self->damage)
	{
		self->damage = 1;
	}

	if (self->spawnflags & 1)
		target_laser_on(self);
	else
		target_laser_off(self);
}

void SP_target_laser(gentity_t* self)
{
	// let everything else get spawned before we start firing
	self->think = target_laser_start;
	self->nextthink = level.time + FRAMETIME;
}

//==========================================================

void target_teleporter_use(gentity_t* self, gentity_t* other, gentity_t* activator)
{
	if (!activator->client)
		return;

	G_ActivateBehavior(self, BSET_USE);

	gentity_t* dest = G_PickTarget(self->target);
	if (!dest)
	{
		trap->Print("Couldn't find teleporter destination\n");
		return;
	}

	TeleportPlayer(activator, dest->s.origin, dest->s.angles);
}

/*QUAKED target_teleporter (1 0 0) (-8 -8 -8) (8 8 8)
The activator will be teleported away.
*/
void SP_target_teleporter(gentity_t* self)
{
	if (!self->targetname)
		trap->Print("untargeted %s at %s\n", self->classname, vtos(self->s.origin));

	self->use = target_teleporter_use;
}

//==========================================================

/*QUAKED target_relay (.5 .5 .5) (-8 -8 -8) (8 8 8) RED_ONLY BLUE_ONLY RANDOM x x x x INACTIVE
This doesn't perform any actions except fire its targets.
The activator can be forced to be from a certain team.
if RANDOM is checked, only one of the targets will be fired, not all of them

  INACTIVE  Can't be used until activated

wait - set to -1 to use it only once
*/
void target_relay_use(gentity_t* self, gentity_t* other, gentity_t* activator)
{
	if (self->spawnflags & 1 && activator->client
		&& activator->client->sess.sessionTeam != TEAM_RED)
	{
		return;
	}
	if (self->spawnflags & 2 && activator->client
		&& activator->client->sess.sessionTeam != TEAM_BLUE)
	{
		return;
	}

	if (self->flags & FL_INACTIVE)
	{
		//set by target_deactivate
		return;
	}

	const qboolean ranscript = G_ActivateBehavior(self, BSET_USE);
	if (self->wait == -1)
	{
		//never use again
		if (ranscript)
		{
			//crap, can't remove!
			self->use = NULL;
		}
		else
		{
			//remove
			self->think = G_FreeEntity;
			self->nextthink = level.time + FRAMETIME;
		}
	}
	if (self->spawnflags & 4)
	{
		gentity_t* ent = G_PickTarget(self->target);
		if (ent && ent->use)
		{
			GlobalUse(ent, self, activator);
		}
		return;
	}
	G_UseTargets(self, activator);
}

void SP_target_relay(gentity_t* self)
{
	self->use = target_relay_use;
	if (self->spawnflags & 128)
	{
		self->flags |= FL_INACTIVE;
	}
}

//==========================================================

/*QUAKED target_kill (.5 .5 .5) (-8 -8 -8) (8 8 8)
Kills the activator.
*/
void target_kill_use(gentity_t* self, gentity_t* other, gentity_t* activator)
{
	G_ActivateBehavior(self, BSET_USE);
	G_Damage(activator, NULL, NULL, NULL, NULL, 100000, DAMAGE_NO_PROTECTION, MOD_TELEFRAG);
}

void SP_target_kill(gentity_t* self)
{
	self->use = target_kill_use;
}

/*QUAKED target_position (0 0.5 0) (-4 -4 -4) (4 4 4)
Used as a positional target for in-game calculation, like jumppad targets.
*/
void SP_target_position(gentity_t* self)
{
	G_SetOrigin(self, self->s.origin);
}

/*QUAKED target_location (0 0.5 0) (-8 -8 -8) (8 8 8)
Set "message" to the name of this location.
Set "count" to 0-7 for color.
0:white 1:red 2:green 3:yellow 4:blue 5:cyan 6:magenta 7:white

Closest target_location in sight used for the location, if none
in site, closest in distance
*/
void SP_target_location(gentity_t* self)
{
	if (self->targetname && self->targetname[0])
	{
		SP_target_position(self);
		return;
	}
	static qboolean didwarn = qfalse;
	if (!self->message)
	{
		trap->Print("target_location with no message at %s\n", vtos(self->s.origin));
		G_FreeEntity(self);
		return;
	}

	if (level.locations.num >= MAX_LOCATIONS)
	{
		if (!didwarn)
		{
			trap->Print("Maximum target_locations hit (%d)! Remaining locations will be removed.\n", MAX_LOCATIONS);
			didwarn = qtrue;
		}
		G_FreeEntity(self);
		return;
	}

	VectorCopy(self->s.origin, level.locations.data[level.locations.num].origin);
	Q_strncpyz(level.locations.data[level.locations.num].message, self->message,
		sizeof level.locations.data[level.locations.num].message);
	level.locations.data[level.locations.num].count = Com_Clampi(0, 7, self->count);

	level.locations.num++;

	G_FreeEntity(self);
}

/*QUAKED target_counter (1.0 0 0) (-4 -4 -4) (4 4 4) x x x x x x x INACTIVE
Acts as an intermediary for an action that takes multiple inputs.

INACTIVE cannot be used until used by a target_activate

target2 - what the counter should fire each time it's incremented and does NOT reach it's count

After the counter has been triggered "count" times (default 2), it will fire all of it's targets and remove itself.

bounceCount - number of times the counter should reset to it's full count when it's done
*/
extern void G_DebugPrint(int level, const char* format, ...);

void target_counter_use(gentity_t* self, gentity_t* other, gentity_t* activator)
{
	if (self->count == 0)
	{
		return;
	}

	//trap->Printf("target_counter %s used by %s, entnum %d\n", self->targetname, activator->targetname, activator->s.number );
	self->count--;

	if (activator)
	{
		G_DebugPrint(WL_VERBOSE, "target_counter %s used by %s (%d/%d)\n", self->targetname, activator->targetname,
			self->genericValue1 - self->count, self->genericValue1);
	}

	if (self->count)
	{
		if (self->target2)
		{
			G_UseTargets2(self, activator, self->target2);
		}
		return;
	}

	G_ActivateBehavior(self, BSET_USE);

	if (self->spawnflags & 128)
	{
		self->flags |= FL_INACTIVE;
	}

	self->activator = activator;
	G_UseTargets(self, activator);

	if (self->count == 0)
	{
		if (self->bounceCount == 0)
		{
			return;
		}
		self->count = self->genericValue1;
		if (self->bounceCount > 0)
		{
			//-1 means bounce back forever
			self->bounceCount--;
		}
	}
}

void SP_target_counter(gentity_t* self)
{
	self->wait = -1;
	if (!self->count)
	{
		self->count = 2;
	}
	//if ( self->bounceCount > 0 )//let's always set this anyway
	{
		//we will reset when we use up our count, remember our initial count
		self->genericValue1 = self->count;
	}

	self->use = target_counter_use;
}

/*QUAKED target_random (.5 .5 .5) (-4 -4 -4) (4 4 4) USEONCE
Randomly fires off only one of it's targets each time used

USEONCE	set to never fire again
*/

void target_random_use(gentity_t* self, gentity_t* other, gentity_t* activator)
{
	int t_count = 0;
	gentity_t* t = NULL;

	//trap->Printf("target_random %s used by %s (entnum %d)\n", self->targetname, activator->targetname, activator->s.number );
	G_ActivateBehavior(self, BSET_USE);

	if (self->spawnflags & 1)
	{
		self->use = 0;
	}

	while ((t = G_Find(t, FOFS(targetname), self->target)) != NULL)
	{
		if (t != self)
		{
			t_count++;
		}
	}

	if (!t_count)
	{
		return;
	}

	if (t_count == 1)
	{
		G_UseTargets(self, activator);
		return;
	}

	//FIXME: need a seed
	const int pick = Q_irand(1, t_count);
	t_count = 0;
	while ((t = G_Find(t, FOFS(targetname), self->target)) != NULL)
	{
		if (t != self)
		{
			t_count++;
		}
		else
		{
			continue;
		}

		if (t == self)
		{
			//				trap->Printf ("WARNING: Entity used itself.\n");
		}
		else if (t_count == pick)
		{
			if (t->use != NULL) // check can be omitted
			{
				GlobalUse(t, self, activator);
				return;
			}
		}

		if (!self->inuse)
		{
			Com_Printf("entity was removed while using targets\n");
			return;
		}
	}
}

void SP_target_random(gentity_t* self)
{
	self->use = target_random_use;
}

int numNewICARUSEnts = 0;

extern qboolean G_MissionReplayBlocksScript(const char* script);
extern const char* G_MissionCutsceneSeen(void);

// Missions: the last script a target_scriptrunner ran on each entity (its activator), so a cutscene that script starts
// can be told apart (g_ICARUScb.c G_MissionCutsceneMessage)
static char missionScriptRun[MAX_GENTITIES][MAX_QPATH];
static int missionScriptRunLevel[MAX_GENTITIES];

void G_MissionNoteScript(const int entNum, const char* script)
{
	if (entNum < 0 || entNum >= MAX_GENTITIES || !script)
	{
		return;
	}
	Q_strncpyz(missionScriptRun[entNum], script, sizeof missionScriptRun[entNum]);
	missionScriptRunLevel[entNum] = level.startTime;
}

const char* G_MissionScriptRunOn(const int entNum)
{
	if (entNum < 0 || entNum >= MAX_GENTITIES || missionScriptRunLevel[entNum] != level.startTime)
	{
		return "";
	}
	return missionScriptRun[entNum];
}

void scriptrunner_run(gentity_t* ent)
{
	if (ent->count != -1)
	{
		if (ent->count <= 0)
		{
			ent->use = 0;
			ent->behaviorSet[BSET_USE] = NULL;
			return;
		}
		--ent->count;
	}

	if (ent->behaviorSet[BSET_USE])
	{
		if (ent->spawnflags & 1)
		{
			if (!ent->activator)
			{
				if (developer.integer)
				{
					Com_Printf("target_scriptrunner tried to run on invalid entity!\n");
				}
				return;
			}

			if (!trap->ICARUS_IsInitialized(ent->s.number))
			{
				//Need to be initialized through ICARUS
				if (!ent->activator->script_targetname || !ent->activator->script_targetname[0])
				{
					//We don't have a script_targetname, so create a new one
					ent->activator->script_targetname = va("newICARUSEnt%d", numNewICARUSEnts++);
				}

				if (trap->ICARUS_ValidEnt((sharedEntity_t*)ent->activator))
				{
					trap->ICARUS_InitEnt((sharedEntity_t*)ent->activator);
				}
				else
				{
					if (developer.integer)
					{
						Com_Printf("target_scriptrunner tried to run on invalid ICARUS activator!\n");
					}
					return;
				}
			}

			if (developer.integer)
			{
				Com_Printf("target_scriptrunner running %s on activator %s\n", ent->behaviorSet[BSET_USE],
					ent->activator->targetname);
			}

			G_MissionNoteScript(ent->activator->s.number, ent->behaviorSet[BSET_USE]);
			if (G_MissionReplayBlocksScript(ent->behaviorSet[BSET_USE]))
			{
				return; // (Missions: a replayed map's first cutscene does not run again, g_ICARUScb.c)
			}

			//make the code handle the case of the scripts directory already being given
			if (strncmp(ent->behaviorSet[BSET_USE], va("%s/", Q3_SCRIPT_DIR), 8) == 0)
			{
				//already has script directory specified.
				trap->ICARUS_RunScript((sharedEntity_t*)ent->activator, ent->behaviorSet[BSET_USE]);
			}
			else
			{
				trap->ICARUS_RunScript((sharedEntity_t*)ent->activator,
					va("%s/%s", Q3_SCRIPT_DIR, ent->behaviorSet[BSET_USE]));
			}
		}
		else
		{
			if (developer.integer && ent->activator)
			{
				Com_Printf("target_scriptrunner %s used by %s\n", ent->targetname, ent->activator->targetname);
			}
			G_ActivateBehavior(ent, BSET_USE);
		}
	}

	if (ent->wait)
	{
		ent->nextthink = level.time + ent->wait;
	}
}

void target_scriptrunner_use(gentity_t* self, gentity_t* other, gentity_t* activator)
{
	if (self->nextthink > level.time)
	{
		return;
	}

	self->activator = activator;
	self->enemy = other;
	if (self->delay)
	{
		//delay before firing scriptrunner
		self->think = scriptrunner_run;
		self->nextthink = level.time + self->delay;
	}
	else
	{
		scriptrunner_run(self);
	}
}

/*QUAKED target_scriptrunner (1 0 0) (-4 -4 -4) (4 4 4) runonactivator x x x x x x INACTIVE
--- SPAWNFLAGS ---
runonactivator - Will run the script on the entity that used this or tripped the trigger that used this
INACTIVE - start off

----- KEYS ------
Usescript - Script to run when used
count - how many times to run, -1 = infinite.  Default is once
wait - can't be used again in this amount of seconds (Default is 1 second if it's multiple-use)
delay - how long to wait after use to run script

*/
void SP_target_scriptrunner(gentity_t* self)
{
	float v = 0.0f;
	if (self->spawnflags & 128)
	{
		self->flags |= FL_INACTIVE;
	}

	if (!self->count)
	{
		self->count = 1; //default 1 use only
	}
	// FIXME: this is a hack... because delay is read in as an int, so I'm bypassing that because it's too late in the project to change it and I want to be able to set less than a second delays
	// no one should be setting a radius on a scriptrunner, if they are this would be bad, take this out for the next project
	v = 0.0f;
	G_SpawnFloat("delay", "0", &v);
	self->delay = v * 1000; //sec to ms
	self->wait *= 1000; //sec to ms

	G_SetOrigin(self, self->s.origin);
	self->use = target_scriptrunner_use;
}

static void G_SetActiveState(const char* targetstring, const qboolean act_state)
{
	gentity_t* target = NULL;
	while (NULL != (target = G_Find(target, FOFS(targetname), targetstring)))
	{
		target->flags = act_state ? target->flags & ~FL_INACTIVE : target->flags | FL_INACTIVE;
	}
}

#define ACT_ACTIVE		qtrue
#define ACT_INACTIVE	qfalse

void target_activate_use(gentity_t* self, gentity_t* other, gentity_t* activator)
{
	G_ActivateBehavior(self, BSET_USE);

	G_SetActiveState(self->target, ACT_ACTIVE);
}

void target_deactivate_use(gentity_t* self, gentity_t* other, gentity_t* activator)
{
	G_ActivateBehavior(self, BSET_USE);

	G_SetActiveState(self->target, ACT_INACTIVE);
}

//FIXME: make these apply to doors, etc too?
/*QUAKED target_activate (1 0 0) (-4 -4 -4) (4 4 4)
Will set the target(s) to be usable/triggerable
*/
void SP_target_activate(gentity_t* self)
{
	G_SetOrigin(self, self->s.origin);
	self->use = target_activate_use;
}

/*QUAKED target_deactivate (1 0 0) (-4 -4 -4) (4 4 4)
Will set the target(s) to be non-usable/triggerable
*/
void SP_target_deactivate(gentity_t* self)
{
	G_SetOrigin(self, self->s.origin);
	self->use = target_deactivate_use;
}

extern void ExitLevel(void);

/*
-------------------------
Missions (GT_SINGLE_PLAYER): the campaign goes on from map to map (DH 2026-11-02)

A target_level_change always goes to the next map (g_autoMapCycle is not looked at). A "+menu" mapname (the SP mission
select / tier menus of Jedi Academy) is never opened: the next map comes from ext_data/mission_order.dat, a line
"<map> <next map> [spawn target]" per map, which also overrides any other map change it lists. The spawn target (the
entity's "target", as SP's G_ChangeMap) is kept in g_missionSpawnTarget / g_missionSpawnMap, and each player who came
along starts the new map there (G_MissionSpawnPoint). Each player's loadout (weapons, ammo, force powers, health,
armor, items) goes along in the cvar g_missionLoadout<client slot> and is given back on his first spawn on the new map
(G_MissionApplyLoadout), bots too. Not on a respawn after a death.
-------------------------
*/
#define MISSION_ORDER_FILE		"ext_data/mission_order.dat"
#define MISSION_ORDER_MAXSIZE	8192
#define MISSION_LOADOUT_CVAR	"g_missionLoadout%i"

extern qboolean SPSpawnpointCheck(vec3_t spawnloc);
qboolean CheckforGoodSpawnPoint(vec3_t location, qboolean playersolidcheck);

// the next map (and spawn target) for this map in the mission order file; qfalse if the file does not list it
static qboolean G_MissionOrderNext(const char* mapname, char* next, const int nextSize, char* spawnTarget,
	const int targetSize)
{
	static char buf[MISSION_ORDER_MAXSIZE];
	fileHandle_t f = 0;
	const int len = trap->FS_Open(MISSION_ORDER_FILE, &f, FS_READ);

	next[0] = 0;
	spawnTarget[0] = 0;
	if (!f)
	{
		return qfalse;
	}
	if (len <= 0 || len >= (int)sizeof buf)
	{
		trap->FS_Close(f);
		Com_Printf(S_COLOR_YELLOW "Missions: %s is empty or too big\n", MISSION_ORDER_FILE);
		return qfalse;
	}
	trap->FS_Read(buf, len, f);
	buf[len] = 0;
	trap->FS_Close(f);

	const char* p = buf;
	while (1)
	{
		const char* token = COM_ParseExt(&p, qtrue);
		if (!token[0])
		{
			return qfalse;
		}
		if (Q_stricmp(token, mapname))
		{
			SkipRestOfLine(&p);
			continue;
		}
		token = COM_ParseExt(&p, qfalse);
		Q_strncpyz(next, token, nextSize);
		token = COM_ParseExt(&p, qfalse);
		Q_strncpyz(spawnTarget, token, targetSize);
		return next[0] ? qtrue : qfalse;
	}
}

// a player in the game (not a spectator), whose loadout goes along / is saved
static qboolean G_MissionInGame(const gentity_t* ent)
{
	return ent->inuse && ent->client && ent->client->pers.connected == CON_CONNECTED
		&& ent->client->sess.sessionTeam != TEAM_SPECTATOR ? qtrue : qfalse;
}

// a player's loadout as text: <map> weapons weapons2 forceKnown health armor items item weapon ammo... forceLevels...
static void G_MissionLoadoutString(const gentity_t* ent, const char* map, char* buf, const int bufSize)
{
	const playerState_t* ps = &ent->client->ps;
	Q_strncpyz(buf, va("%s %i %i %i %i %i %i %i %i", map, ps->stats[STAT_WEAPONS], ps->stats[STAT_WEAPONS2],
		ps->fd.forcePowersKnown, ent->health > 0 ? ent->health : 0, ps->stats[STAT_ARMOR],
		ps->stats[STAT_HOLDABLE_ITEMS], ps->stats[STAT_HOLDABLE_ITEM], ps->weapon), bufSize);
	for (int a = 0; a < AMMO_MAX; a++)
	{
		Q_strcat(buf, bufSize, va(" %i", ps->ammo[a]));
	}
	for (int fp = 0; fp < NUM_FORCE_POWERS; fp++)
	{
		Q_strcat(buf, bufSize, va(" %i", ps->fd.forcePowerLevel[fp]));
	}
}

// each player's loadout goes along to the next map (given back by G_MissionApplyLoadout)
static void G_MissionSaveLoadouts(const char* nextMap)
{
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		const gentity_t* ent = &g_entities[i];
		char buf[MAX_CVAR_VALUE_STRING];

		if (!G_MissionInGame(ent))
		{
			trap->Cvar_Set(va(MISSION_LOADOUT_CVAR, i), "");
			continue;
		}
		G_MissionLoadoutString(ent, nextMap, buf, sizeof buf);
		trap->Cvar_Set(va(MISSION_LOADOUT_CVAR, i), buf);
	}
}

/*
-------------------------
Missions checkpoints (DH 2026-11-02, Phase 3)

One save per campaign in the mod's home folder: missions/<campaign>.sav (ja, jo, or the map name for other maps).
Written at every checkpoint (target_autosave) and at every level change: the map, the checkpoint spot (none: the map
start / the level change's spawn target), each player's loadout by client slot and the map's objectives. A checkpoint
restarts its map fresh at that spot (doors / kills before it are not replayed), each player with the loadout he had
there: when the mission fails (SET_MISSIONFAILED), when the whole team is down (g_missionWipeRestart 1, the default),
or to continue a saved mission (g_missionContinue 1 / ja / jo, then start any mission map). ICARUS variables are not
saved: the map starts fresh, its scripts set their own variables again.
-------------------------
*/
#define MISSION_SAVE_MAXSIZE	16384

extern int G_MissionObjectivesString(char* buf, int bufSize);

// the campaign a map belongs to: its save file's name
const char* G_MissionCampaign(const char* map)
{
	static const char* jaPrefix[] = { "academy", "hoth", "kor", "t1_", "t2_", "t3_", "taspir", "vjun", "yavin1", "yavin2", NULL };
	static const char* joMaps[] = { "kejim_post", "kejim_base", "artus_mine", "artus_detention", "artus_topside", "valley",
		"yavin_temple", "yavin_trial", "ns_streets", "ns_hideout", "ns_starpad", "bespin_undercity", "bespin_streets",
		"bespin_platform", "cairn_bay", "cairn_assembly", "cairn_reactor", "cairn_dock1", "doom_comm", "doom_detention",
		"doom_shields", "yavin_swamp", "yavin_canyon", "yavin_courtyard", "yavin_final", NULL };

	for (int i = 0; joMaps[i]; i++)
	{
		if (!Q_stricmp(map, joMaps[i]))
		{
			return "jo";
		}
	}
	for (int i = 0; jaPrefix[i]; i++)
	{
		if (!Q_stricmpn(map, jaPrefix[i], strlen(jaPrefix[i])))
		{
			return "ja";
		}
	}
	return map;
}

// the text of a campaign's save; qfalse if there is none
qboolean G_MissionSaveText(const char* campaign, char* buf, const int bufSize)
{
	fileHandle_t f = 0;
	const int len = trap->FS_Open(va("missions/%s.sav", campaign), &f, FS_READ);

	buf[0] = 0;
	if (!f)
	{
		return qfalse;
	}
	if (len > 0 && len < bufSize)
	{
		trap->FS_Read(buf, len, f);
		buf[len] = 0;
	}
	trap->FS_Close(f);
	return buf[0] ? qtrue : qfalse;
}

// write the campaign save: the map, the checkpoint spot (hasSpot) or spawn target, the loadouts, the objectives
static void G_MissionWriteSave(const char* map, const qboolean hasSpot, const vec3_t spot, const float yaw,
	const char* target)
{
	static char buf[MISSION_SAVE_MAXSIZE];
	char mapname[MAX_QPATH], loadout[MAX_CVAR_VALUE_STRING];
	fileHandle_t f = 0;

	if (level.gametype != GT_SINGLE_PLAYER)
	{
		return;
	}
	trap->Cvar_VariableStringBuffer("mapname", mapname, sizeof mapname);
	Q_strncpyz(buf, va("map %s\n", map), sizeof buf);
	if (hasSpot)
	{
		Q_strcat(buf, sizeof buf, va("spot %.1f %.1f %.1f %.1f\n", spot[0], spot[1], spot[2], yaw));
	}
	if (target && target[0])
	{
		Q_strcat(buf, sizeof buf, va("target %s\n", target));
	}
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		if (G_MissionInGame(&g_entities[i]))
		{
			G_MissionLoadoutString(&g_entities[i], map, loadout, sizeof loadout);
			Q_strcat(buf, sizeof buf, va("load %i %s\n", i, loadout));
		}
	}
	if (!Q_stricmp(map, mapname))
	{
		// (this map's first cutscene, which a restart does not play again: g_ICARUScb.c)
		const char* seen = G_MissionCutsceneSeen();
		if (seen[0])
		{
			Q_strcat(buf, sizeof buf, va("cutscene %s\n", seen));
		}
		// (the objectives of this map; a new map starts with none)
		const int len = strlen(buf);
		G_MissionObjectivesString(buf + len, sizeof buf - len);
	}

	trap->FS_Open(va("missions/%s.sav", G_MissionCampaign(map)), &f, FS_WRITE);
	if (!f)
	{
		Com_Printf(S_COLOR_YELLOW "Missions: could not write missions/%s.sav\n", G_MissionCampaign(map));
		return;
	}
	trap->FS_Write(buf, strlen(buf), f);
	trap->FS_Close(f);
	Com_Printf("Missions: checkpoint saved (missions/%s.sav, %s)\n", G_MissionCampaign(map), map);
}

// a checkpoint was reached (Use_Autosave): saved, so a restart / continue starts here
void G_MissionCheckpointReached(const vec3_t spot, const float yaw)
{
	char mapname[MAX_QPATH];

	trap->Cvar_VariableStringBuffer("mapname", mapname, sizeof mapname);
	G_MissionWriteSave(mapname, qtrue, spot, yaw, "");
}

// load a campaign's save: set everything up for its map (loadouts, checkpoint, spawn target), then load that map
qboolean G_MissionLoadSave(const char* campaign)
{
	static char buf[MISSION_SAVE_MAXSIZE];
	char map[MAX_QPATH] = { 0 }, target[MAX_QPATH] = { 0 }, spot[128] = { 0 }, cutscene[MAX_CVAR_VALUE_STRING] = { 0 };

	if (!G_MissionSaveText(campaign, buf, sizeof buf))
	{
		return qfalse;
	}
	for (int i = 0; i < MAX_CLIENTS; i++)
	{
		trap->Cvar_Set(va(MISSION_LOADOUT_CVAR, i), "");
	}

	const char* p = buf;
	while (1)
	{
		const char* token = COM_ParseExt(&p, qtrue);
		if (!token[0])
		{
			break;
		}
		if (!Q_stricmp(token, "map"))
		{
			Q_strncpyz(map, COM_ParseExt(&p, qfalse), sizeof map);
		}
		else if (!Q_stricmp(token, "target"))
		{
			Q_strncpyz(target, COM_ParseExt(&p, qfalse), sizeof target);
		}
		else if (!Q_stricmp(token, "cutscene"))
		{
			// <map> <script> <skip script>
			while ((token = COM_ParseExt(&p, qfalse))[0])
			{
				Q_strcat(cutscene, sizeof cutscene, cutscene[0] ? va(" %s", token) : token);
			}
		}
		else if (!Q_stricmp(token, "spot"))
		{
			spot[0] = 0;
			for (int i = 0; i < 4; i++)
			{
				Q_strcat(spot, sizeof spot, va("%s ", COM_ParseExt(&p, qfalse)));
			}
		}
		else if (!Q_stricmp(token, "load"))
		{
			char loadout[MAX_CVAR_VALUE_STRING] = { 0 };
			const int slot = atoi(COM_ParseExt(&p, qfalse));
			while ((token = COM_ParseExt(&p, qfalse))[0])
			{
				Q_strcat(loadout, sizeof loadout, loadout[0] ? va(" %s", token) : token);
			}
			if (slot >= 0 && slot < MAX_CLIENTS)
			{
				trap->Cvar_Set(va(MISSION_LOADOUT_CVAR, slot), loadout);
			}
		}
		else
		{
			SkipRestOfLine(&p); // (obj lines: read by the objectives at the map start)
		}
	}
	if (!map[0])
	{
		return qfalse;
	}

	trap->Cvar_Set("g_missionSpawnMap", map);
	trap->Cvar_Set("g_missionSpawnTarget", target);
	trap->Cvar_Set("g_missionCheckpoint", spot);
	trap->Cvar_Set("g_missionCheckpointMap", spot[0] ? map : "");
	trap->Cvar_Set("g_missionRestoreMap", map); // (the objectives come back from the save: g_ICARUScb.c)
	if (cutscene[0])
	{
		trap->Cvar_Set("g_missionCutsceneSeen", cutscene); // (its first cutscene does not play again: g_ICARUScb.c)
	}
	trap->Cvar_Set("g_missionReplay", "1");
	trap->SendServerCommand(-1, va("cp \"" S_COLOR_CYAN "Back to the last checkpoint (%s)...\n\"", map));
	trap->SendConsoleCommand(EXEC_APPEND, va("map %s\n", map));
	Com_Printf("Missions: loading missions/%s.sav (%s)\n", campaign, map);
	return qtrue;
}

// mission failed / team down: back to the last checkpoint after delay ms (G_MissionRestartThink)
static int missionRestartTime, missionRestartLevel;

void G_MissionRestartSoon(const int delay)
{
	if (level.gametype != GT_SINGLE_PLAYER || missionRestartLevel == level.startTime && missionRestartTime)
	{
		return;
	}
	missionRestartTime = level.time + delay;
	missionRestartLevel = level.startTime;
}

void G_MissionRestartThink(void)
{
	char mapname[MAX_QPATH];
	static char buf[MISSION_SAVE_MAXSIZE];

	if (missionRestartLevel != level.startTime || !missionRestartTime || level.time < missionRestartTime)
	{
		return;
	}
	missionRestartTime = 0;
	trap->Cvar_VariableStringBuffer("mapname", mapname, sizeof mapname);

	// the save of this campaign, if it is for this map; else this map from its start
	const char* campaign = G_MissionCampaign(mapname);
	if (G_MissionSaveText(campaign, buf, sizeof buf))
	{
		const char* p = buf;
		const char* token = COM_ParseExt(&p, qtrue);
		if (!Q_stricmp(token, "map") && !Q_stricmp(COM_ParseExt(&p, qfalse), mapname) && G_MissionLoadSave(campaign))
		{
			return;
		}
	}
	trap->Cvar_Set("g_missionReplay", "1"); // (the map's first cutscene does not play again: g_ICARUScb.c)
	trap->SendServerCommand(-1, "cp \"" S_COLOR_CYAN "Restarting the mission...\n\"");
	trap->SendConsoleCommand(EXEC_APPEND, "map_restart 0\n");
}

// g_missionContinue (1 = this map's campaign, or ja / jo / a save name): at the first player of a mission map, that
// save is loaded (its map, checkpoint and loadouts); then the cvar goes back to 0
void G_MissionCheckContinue(void)
{
	static int doneLevel = -1;
	char value[MAX_QPATH], mapname[MAX_QPATH];

	if (level.gametype != GT_SINGLE_PLAYER || doneLevel == level.startTime)
	{
		return;
	}
	doneLevel = level.startTime;
	trap->Cvar_VariableStringBuffer("g_missionContinue", value, sizeof value);
	if (!value[0] || !Q_stricmp(value, "0"))
	{
		return;
	}
	trap->Cvar_Set("g_missionContinue", "0");
	trap->Cvar_VariableStringBuffer("mapname", mapname, sizeof mapname);
	if (!G_MissionLoadSave(!Q_stricmp(value, "1") ? G_MissionCampaign(mapname) : value))
	{
		Com_Printf(S_COLOR_YELLOW "Missions: no saved mission to continue (%s)\n", value);
	}
}

// the respawn spot of the checkpoint a restart / continue brings the players back to (once per map start)
static gentity_t* G_MissionCheckpointSpot(const vec3_t origin, const float yaw)
{
	static gentity_t* spotEnt = NULL;
	static int spotLevel = -1;
	gentity_t* other = NULL;

	if (spotLevel == level.startTime && spotEnt && spotEnt->inuse)
	{
		return spotEnt;
	}
	spotLevel = level.startTime;
	while ((other = G_Find(other, FOFS(classname), "info_player_deathmatch")) != NULL)
	{
		other->genericValue1++; // (as Use_Autosave: the checkpoint is the respawn spot from now on)
	}
	spotEnt = G_Spawn();
	if (spotEnt)
	{
		spotEnt->classname = "info_player_deathmatch";
		VectorCopy(origin, spotEnt->s.origin);
		VectorSet(spotEnt->s.angles, 0, yaw, 0);
		spotEnt->genericValue1 = 0;
	}
	return spotEnt;
}

// the loadout this player brought along to this map, or qfalse (buf gets the rest after the map name)
static qboolean G_MissionLoadoutFor(const int clientNum, char* buf, const int bufSize)
{
	char mapname[MAX_QPATH];
	const char* p = buf;

	trap->Cvar_VariableStringBuffer(va(MISSION_LOADOUT_CVAR, clientNum), buf, bufSize);
	if (!buf[0])
	{
		return qfalse;
	}
	trap->Cvar_VariableStringBuffer("mapname", mapname, sizeof mapname);
	const char* token = COM_ParseExt(&p, qfalse);
	return Q_stricmp(token, mapname) == 0 ? qtrue : qfalse;
}

// a free spot for a player at a spawn spot or around it, on the floor, that the spot can see (all the players come in
// at the same place); qfalse if there is none
static qboolean G_MissionFreeSpotNear(const gentity_t* ent, const vec3_t center, vec3_t out)
{
	static const float radii[] = { 0.0f, 48.0f, 96.0f, 144.0f, 192.0f };
	static const vec3_t mins = { -15.0f, -15.0f, DEFAULT_MINS_2 };
	static const vec3_t maxs = { 15.0f, 15.0f, DEFAULT_MAXS_2 };

	for (int r = 0; r < 5; r++)
	{
		for (int k = 0; k < (r ? 8 : 1); k++)
		{
			const float yaw = DEG2RAD(k * 45.0f + ent->s.number * 15.0f);
			vec3_t loc, eye;
			trace_t tr;

			VectorCopy(center, loc);
			loc[0] += cos(yaw) * radii[r];
			loc[1] += sin(yaw) * radii[r];
			if (r)
			{
				VectorCopy(center, eye);
				eye[2] += 16.0f;
				loc[2] += 16.0f;
				trap->Trace(&tr, eye, NULL, NULL, loc, ent->s.number, MASK_SOLID, qfalse, 0, 0);
				loc[2] -= 16.0f;
				if (tr.fraction < 1.0f)
				{
					continue;
				}
			}
			if (!CheckforGoodSpawnPoint(loc, qtrue)) // (dropped to the floor; not over a drop)
			{
				continue;
			}
			trap->Trace(&tr, loc, mins, maxs, loc, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0);
			if (tr.startsolid || tr.allsolid) // (a wall or another player)
			{
				continue;
			}
			VectorCopy(loc, out);
			return qtrue;
		}
	}
	return qfalse;
}

// Missions: the spot a player who came along from the last map starts this map at (its spawn target), or NULL
gentity_t* G_MissionSpawnPoint(const gentity_t* ent, vec3_t origin, vec3_t angles)
{
	char buf[MAX_CVAR_VALUE_STRING];
	char mapname[MAX_QPATH], spawnMap[MAX_QPATH], target[MAX_QPATH];

	if (level.gametype != GT_SINGLE_PLAYER || !G_MissionLoadoutFor(ent->s.number, buf, sizeof buf))
	{
		return NULL;
	}
	trap->Cvar_VariableStringBuffer("mapname", mapname, sizeof mapname);

	// a restart / continue: back at the saved checkpoint
	trap->Cvar_VariableStringBuffer("g_missionCheckpointMap", spawnMap, sizeof spawnMap);
	if (spawnMap[0] && !Q_stricmp(spawnMap, mapname))
	{
		char cp[128];
		vec3_t cpOrigin;
		float cpYaw = 0.0f;
		trap->Cvar_VariableStringBuffer("g_missionCheckpoint", cp, sizeof cp);
		if (sscanf(cp, "%f %f %f %f", &cpOrigin[0], &cpOrigin[1], &cpOrigin[2], &cpYaw) == 4)
		{
			gentity_t* cpSpot = G_MissionCheckpointSpot(cpOrigin, cpYaw);
			if (cpSpot && G_MissionFreeSpotNear(ent, cpOrigin, origin))
			{
				VectorSet(angles, 0, cpYaw, 0);
				return cpSpot;
			}
		}
	}

	trap->Cvar_VariableStringBuffer("g_missionSpawnMap", spawnMap, sizeof spawnMap);
	trap->Cvar_VariableStringBuffer("g_missionSpawnTarget", target, sizeof target);
	if (!target[0] || Q_stricmp(spawnMap, mapname))
	{
		return NULL;
	}

	gentity_t* spot = G_Find(NULL, FOFS(targetname), target);
	if (!spot)
	{
		Com_Printf(S_COLOR_YELLOW "Missions: no spawn target \"%s\" on %s\n", target, mapname);
		return NULL;
	}
	if (!G_MissionFreeSpotNear(ent, spot->s.origin, origin))
	{
		return NULL;
	}
	VectorCopy(spot->s.angles, angles);
	return spot;
}

// Missions: a player who would come in on top of another at the SP start gets a free spot around it (DH 2026-11-02:
// several players join at once, the SP maps have one start), no per-map spawn files needed
void G_MissionSpreadSpawn(const gentity_t* ent, const gentity_t* spot, vec3_t origin)
{
	static const vec3_t mins = { -15.0f, -15.0f, DEFAULT_MINS_2 };
	static const vec3_t maxs = { 15.0f, 15.0f, DEFAULT_MAXS_2 };
	trace_t tr;
	vec3_t loc;

	if (level.gametype != GT_SINGLE_PLAYER || !spot)
	{
		return;
	}
	trap->Trace(&tr, origin, mins, maxs, origin, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0);
	if (!tr.startsolid && !tr.allsolid)
	{
		return; // (free)
	}
	if (G_MissionFreeSpotNear(ent, spot->s.origin, loc))
	{
		VectorCopy(loc, origin);
	}
}

// Missions: the most players (humans + bots) in a mission, g_missionMaxPlayers (default 4, 0 = no limit); qtrue if
// this client may not join the game now
qboolean G_MissionFull(const int clientNum)
{
	static vmCvar_t maxPlayers;
	static qboolean registered = qfalse;
	int inGame = 0;

	if (level.gametype != GT_SINGLE_PLAYER)
	{
		return qfalse;
	}
	if (!registered)
	{
		trap->Cvar_Register(&maxPlayers, "g_missionMaxPlayers", "4", CVAR_ARCHIVE | CVAR_SERVERINFO);
		registered = qtrue;
	}
	trap->Cvar_Update(&maxPlayers);
	if (maxPlayers.integer <= 0)
	{
		return qfalse;
	}
	for (int i = 0; i < level.maxclients; i++)
	{
		const gclient_t* cl = &level.clients[i];
		if (i != clientNum && cl->pers.connected != CON_DISCONNECTED && cl->sess.sessionTeam != TEAM_SPECTATOR)
		{
			inGame++;
		}
	}
	return inGame >= maxPlayers.integer ? qtrue : qfalse;
}

// Missions: the loadout a player brought from the last map, given back on his first spawn on this one, then cleared
void G_MissionApplyLoadout(gentity_t* ent)
{
	char buf[MAX_CVAR_VALUE_STRING];
	int v[8] = { 0 };
	playerState_t* ps = &ent->client->ps;
	const char* p = buf;

	if (level.gametype != GT_SINGLE_PLAYER || ent->client->sess.sessionTeam == TEAM_SPECTATOR)
	{
		return;
	}
	const qboolean found = G_MissionLoadoutFor(ent->s.number, buf, sizeof buf);
	if (buf[0])
	{
		trap->Cvar_Set(va(MISSION_LOADOUT_CVAR, ent->s.number), ""); // once (a stale one from another map goes too)
	}
	if (!found)
	{
		return;
	}

	COM_ParseExt(&p, qfalse); // (the map name)
	for (int i = 0; i < 8; i++)
	{
		v[i] = atoi(COM_ParseExt(&p, qfalse));
	}
	ps->stats[STAT_WEAPONS] |= v[0];
	ps->stats[STAT_WEAPONS2] |= v[1];
	ps->fd.forcePowersKnown |= v[2];
	if (v[3] > 0)
	{
		ent->health = ps->stats[STAT_HEALTH] = v[3] > ps->stats[STAT_MAX_HEALTH] ? ps->stats[STAT_MAX_HEALTH] : v[3];
	}
	ps->stats[STAT_ARMOR] = v[4];
	ps->stats[STAT_HOLDABLE_ITEMS] |= v[5];
	if (v[6] && !ps->stats[STAT_HOLDABLE_ITEM])
	{
		ps->stats[STAT_HOLDABLE_ITEM] = v[6];
	}
	for (int a = 0; a < AMMO_MAX; a++)
	{
		const int n = atoi(COM_ParseExt(&p, qfalse));
		if (n > ps->ammo[a])
		{
			ps->ammo[a] = n;
		}
	}
	for (int fp = 0; fp < NUM_FORCE_POWERS; fp++)
	{
		const int n = atoi(COM_ParseExt(&p, qfalse));
		if (n > ps->fd.forcePowerLevel[fp])
		{
			ps->fd.forcePowerLevel[fp] = n;
		}
	}
	if (v[7] > WP_NONE && v[7] < WP_NUM_WEAPONS && BG_HasWeapon(ps->stats, v[7]))
	{
		ps->weapon = v[7];
		ps->weaponstate = WEAPON_READY;
	}
}

// Missions: on to the next map of the campaign (see above); qfalse if there is none to go to
static qboolean G_MissionLevelChange(const gentity_t* self)
{
	char mapname[MAX_QPATH], next[MAX_QPATH], spawnTarget[MAX_QPATH];

	trap->Cvar_VariableStringBuffer("mapname", mapname, sizeof mapname);
	if (!G_MissionOrderNext(mapname, next, sizeof next, spawnTarget, sizeof spawnTarget))
	{
		if (!self->message || !self->message[0] || self->message[0] == '+')
		{
			Com_Printf(S_COLOR_YELLOW "Missions: no next map for %s (\"%s\") in %s\n", mapname,
				self->message ? self->message : "", MISSION_ORDER_FILE);
			return qfalse;
		}
		Q_strncpyz(next, self->message, sizeof next);
		Q_strncpyz(spawnTarget, self->target ? self->target : "", sizeof spawnTarget);
	}

	trap->Cvar_Set("g_missionSpawnMap", next);
	trap->Cvar_Set("g_missionSpawnTarget", spawnTarget);
	trap->Cvar_Set("g_missionCheckpointMap", "");
	G_MissionSaveLoadouts(next);
	G_MissionWriteSave(next, qfalse, vec3_origin, 0.0f, spawnTarget); // (a continue starts the next map)
	trap->Cvar_Set("nextmap", va("map %s", next));
	ExitLevel();
	return qtrue;
}

void target_level_change_use(gentity_t* self, gentity_t* other, gentity_t* activator)
{
	vmCvar_t mapname;
	G_ActivateBehavior(self, BSET_USE);

	//Add this map's name to the tiers_complete cvar
	trap->Cvar_Register(&mapname, "mapname", "", CVAR_SERVERINFO | CVAR_ROM);

	strcpy(tiers_complete.string, va("%s|%s", tiers_complete.string, mapname.string));

	if (level.gametype == GT_SINGLE_PLAYER)
	{
		// (never a menu in a mission: with no next map, the server's map cycle)
		if (!G_MissionLevelChange(self))
		{
			ExitLevel();
		}
		return;
	}

	if (g_autoMapCycle.integer)
	{
		//cycle to whatever this map is supposed to go to.
		if (self->message[0] == '+')
		{
			//a menu, open it
			char* s = self->message;
			s++;

			trap->SendServerCommand(-1, va("briefmenu %s", s));
		}
		else
		{
			//a menu, open it
			trap->Cvar_Set("nextmap", va("map %s", self->message));
			ExitLevel();
		}
	}
	else
	{
		//otherwise, just go to the next map in the mapcycle
		ExitLevel();
	}
}

/*QUAKED target_level_change (1 0 0) (-4 -4 -4) (4 4 4)
"mapname" - Name of map to change to
*/
void SP_target_level_change(gentity_t* self)
{
	char* s = NULL;
	vmCvar_t mapname;

	trap->Cvar_Register(&mapname, "mapname", "", CVAR_SERVERINFO | CVAR_ROM);

	G_SpawnString("mapname", "", &s);
	self->message = G_NewString(s);

	//set custom storyhead varible or use the mapname by default.
	G_SpawnString("storyhead", mapname.string, &s);
	trap->Cvar_Set("storyhead", s);

	//set up tier_storyinfo.
	G_SpawnString("tier_storyinfo", "++", &s);
	if (s[0] == '+' && s[1] == '+')
	{
		//increment tier_storyinfo
		trap->Cvar_Set("tier_storyinfo", va("%i", tier_storyinfo.integer + 1));
	}
	else
	{
		//override with new value
		trap->Cvar_Set("tier_storyinfo", s);
	}

	if (!self->message || !self->message[0])
	{
		trap->Error(ERR_DROP, "target_level_change with no mapname!\n");
		return;
	}

	G_SetOrigin(self, self->s.origin);
	self->use = target_level_change_use;
}

void target_play_music_use(gentity_t* self, gentity_t* other, gentity_t* activator)
{
	G_ActivateBehavior(self, BSET_USE);
	trap->SetConfigstring(CS_MUSIC, self->message);
}

/*QUAKED target_play_music (1 0 0) (-4 -4 -4) (4 4 4)
target_play_music
Plays the requested music files when this target is used.

"targetname"
"music"		music WAV or MP3 file ( music/introfile.mp3 [optional]  music/loopfile.mp3 )

If an intro file and loop file are specified, the intro plays first, then the looping
portion will start and loop indefinetly.  If no introfile is entered, only the loopfile
will play.
*/
void SP_target_play_music(gentity_t* self)
{
	char* s = NULL;

	G_SetOrigin(self, self->s.origin);
	if (!G_SpawnString("music", "", &s))
	{
		trap->Error(ERR_DROP, "target_play_music without a music key at %s", vtos(self->s.origin));
	}

	self->message = G_NewString(s);

	self->use = target_play_music_use;
}

static void Use_Secret(gentity_t* ent, gentity_t* other, gentity_t* activator)
{
	if (!activator || !activator->client)
	{
		trap->Error(ERR_DROP, "Error: Non-Player activated target_secret\n.");
		return;
	}

	ent->count--;

	trap->SendServerCommand(-1, va("cp \"%s^7 discovered a secret area.\n\"", activator->client->pers.netname));
	G_Sound(activator, CHAN_AUTO, G_SoundIndex("sound/interface/secret_area"));

	if (ent->count == 1)
	{
		trap->SendServerCommand(-1, "cp \"^1One secret remains.\n\"");
	}
	else if (!ent->count)
	{
		trap->SendServerCommand(-1, "cp \"^1All secrets have been found!\n\"");
		ent->use = 0;
	}
	else
	{
		trap->SendServerCommand(-1, va("cp \"^4%i / %i^1 Secrets Found\n\"", ent->genericValue1 - ent->count,
			ent->genericValue1));
	}
}

/*QUAKED target_secret - keeps count of the number of secret areas left undiscovered
on the map.
count - number of secret areas on map
*/
void SP_target_secret(gentity_t* self)
{
	self->use = Use_Secret;
	self->genericValue1 = self->count;
	G_SoundIndex("sound/interface/secret_area");
}

/*
check to see if this is a good spawnpoint if not, return false.  In addition, move the
spawn location vector down to the ground.
*/
qboolean CheckforGoodSpawnPoint(vec3_t location, const qboolean playersolidcheck)
{
	trace_t tr;
	vec3_t locend;
	const vec3_t playerMins = { -15, -15, DEFAULT_MINS_2 };
	const vec3_t playerMaxs = { 15, 15, DEFAULT_MAXS_2 };

	VectorCopy(location, locend);
	locend[2] -= 900;
	location[2] += 9;

	if (playersolidcheck)
	{
		//use a playersolid check
		trap->Trace(&tr, location, playerMins, playerMaxs, locend, -1, MASK_PLAYERSOLID, qfalse, 0, 0);
	}
	else
	{
		//otherwise just check solid
		trap->Trace(&tr, location, playerMins, playerMaxs, locend, -1, MASK_SOLID, qfalse, 0, 0);
	}

	if (tr.startsolid || tr.fraction == 1)
	{
		//either inside a solid or over a cliff
		return qfalse;
	}

	VectorCopy(tr.endpos, location);

	location[2] += 9;
	return qtrue;
}

#define FLAG_TELETOSAVE		1
extern qboolean SPSpawnpointCheck(vec3_t spawnloc);

void Use_Autosave(gentity_t* ent, gentity_t* other, const gentity_t* activator)
{
	// (no activator - a script or a relay fired it: the checkpoint is where the target_autosave stands)

	gentity_t* oldspawn = NULL;
	vec3_t spawnloc;
	vec3_t spawnang;

	if (activator && activator->client && activator->NPC)
	{
		// NPCs should not trigger autosaves
		return;
	}

	if (activator && activator->client && activator->health > 0)
	{
		VectorCopy(activator->client->ps.origin, spawnloc);
		VectorSet(spawnang, 0, activator->client->ps.viewangles[YAW], 0);
	}
	else
	{
		VectorCopy(ent->s.origin, spawnloc);
		VectorSet(spawnang, 0, ent->s.angles[YAW], 0);
	}

	// (as JACoop: a checkpoint is on the floor, not where its activator was in the air)
	{
		const vec3_t box_mins = { -15.0f, -15.0f, -24.0f };
		const vec3_t box_maxs = { 15.0f, 15.0f, 40.0f };
		vec3_t up, down;
		trace_t tr;
		VectorCopy(spawnloc, up);
		up[2] += 8.0f;
		VectorCopy(spawnloc, down);
		down[2] -= 512.0f;
		trap->Trace(&tr, up, box_mins, box_maxs, down, activator ? activator->s.number : ENTITYNUM_NONE,
			MASK_PLAYERSOLID, qfalse, 0, 0);
		if (!tr.startsolid && !tr.allsolid && tr.fraction < 1.0f && tr.plane.normal[2] >= 0.7f)
		{
			VectorCopy(tr.endpos, spawnloc);
		}
	}

	while ((oldspawn = G_Find(oldspawn, FOFS(classname), "info_player_deathmatch")) != NULL)
	{
		// increase the other spawn's sequential number
		oldspawn->genericValue1++;
	}

	if (!CheckforGoodSpawnPoint(spawnloc, qfalse))
	{
		trap->SendServerCommand(-1, "cp \"^1Bad spawnpoint in Use_Autosave()..\n\"");
	}

	gentity_t* spawn = G_Spawn();

	if (spawn)
	{
		spawn->classname = "info_player_deathmatch";
		VectorCopy(spawnang, spawn->s.angles);
		VectorCopy(spawnloc, spawn->s.origin);
	}

	if (activator && activator->client && activator->client->pers.netname)
	{
		trap->SendServerCommand(-1,
			va("cp \"%s^1 Reached Checkpoint.\n\"", activator->client->pers.netname));
	}
	else
	{
		trap->SendServerCommand(-1, "cp \"^1Reached Checkpoint.\n\"");
	}

	if (ent->spawnflags & FLAG_TELETOSAVE)
	{
		// teleport all players to the savepoint

		for (int counter = 0; counter < MAX_CLIENTS; counter++)
		{
			gentity_t* player = &g_entities[counter];

			if (!player->inuse ||
				!player->client ||
				player->client->pers.connected != CON_CONNECTED ||
				player->client->sess.sessionTeam == TEAM_SPECTATOR ||
				player->health <= 0 ||
				(activator && activator->client && activator->s.number == player->s.number))
			{
				continue;
			}

			if (SPSpawnpointCheck(spawnloc))
			{
				// flip teleport flag to prevent client lerp
				int flags = player->client->ps.eFlags & EF_TELEPORT_BIT;
				flags ^= EF_TELEPORT_BIT;
				player->client->ps.eFlags = flags;

				// set origin
				G_SetOrigin(player, spawnloc);
				VectorCopy(spawnloc, player->client->ps.origin);

				// set angles
				SetClientViewAngle(player, spawnang);

				trap->LinkEntity((sharedEntity_t*)player);
			}
			else
			{
				Com_Printf("Warning: Client %i couldn't spawn at savegame.\n",
					player->s.number);
			}
		}
	}

	if (level.gametype == GT_SINGLE_PLAYER)
	{
		// Missions: the checkpoint is saved (a restart / continue starts here)
		extern void G_MissionCheckpointReached(const vec3_t spot, float yaw);
		G_MissionCheckpointReached(spawnloc, spawnang[YAW]);
	}

	ent->touch = 0;
	ent->use = 0;
}

/*QUAKED target_autosave (1 0 0) (-4 -4 -4) (4 4 4)
SP Behavior - Auto save the game in two frames.
Make sure it won't trigger during dialogue or cinematic or it will stutter!

MP Behavior - Now used to create sequencial created spawnpoints for CoOp mode.  This will create
create a new spawnpoint at the current location and then increase the spawn sequencial
counter for the other spawns.  This only works for the CoOp gametype.

Use spawnflag = 1 to teleport all the other players to the autosave point when the autosave
is triggered.
*/
void SP_target_autosave(gentity_t* self)
{
	self->use = Use_Autosave;
	//need classname so we can save/delete/load this entity.
	self->classname = "target_autosave";
}
/*QUAKED target_gravity_change (1 0 0) (-4 -4 -4) (4 4 4) GLOBAL
SP entity (JO doom_shields), for Missions. "gravity" - Normal = 800.
GLOBAL - the gravity of the whole map until it ends (not g_gravity, so a mission restart or the next map are
normal again). Without GLOBAL (no campaign map uses it) it does nothing in MP: client gravity is set every frame.
*/
static void target_gravity_change_use(gentity_t* self, gentity_t* other, gentity_t* activator)
{
	G_ActivateBehavior(self, BSET_USE);

	if (self->spawnflags & 1)
	{
		level.missionGravitySet = qtrue;
		level.missionGravity = self->speed;
	}
}

void SP_target_gravity_change(gentity_t* self)
{
	G_SetOrigin(self, self->s.origin);
	G_SpawnFloat("gravity", "0", &self->speed);
	self->use = target_gravity_change_use;
}

/*
============================================================================
Missions: downed / revive (Claude 2026-11-11, from JACoop's design; g_missionRevive, default 1)

A lethal hit puts a player down instead of killing him, while another player in the game is still up: he lies on the
ground at 1 health, damage can't finish him, enemies leave him, he can't move or fight. A teammate who stands next to
him and holds USE for 3 s brings him back with 40 % of his max health. Unhelped for 60 s (or holding JUMP for 2 s to
give up) he really dies and respawns at the checkpoint as before. Falls, crushers, drowning, lava, trigger_hurt and
suicide still kill at once. When nobody is up any more, everyone down really dies, so G_MissionWipeCheck (whole team
down) takes the mission back to the last checkpoint as before.
============================================================================
*/
#define MISSION_DOWN_BLEED		60000	// ms down before he dies
#define MISSION_DOWN_REVIVE		3000	// ms of USE held next to him
#define MISSION_DOWN_RANGE		96.0f	// how close the reviver must be
#define MISSION_DOWN_GIVEUP		2000	// ms of JUMP held to give up

typedef struct
{
	int downTime;		// level.time he went down, 0 = up
	int reviver;		// client reviving him, -1 = none
	int reviveStart;	// level.time that reviver started
	int giveUpStart;	// level.time he started holding JUMP, 0 = not
	int nextNote;		// next progress print
	vec3_t safeOrigin;	// last ground he stood on (pit rescue)
	float safeYaw;
	int safeTime;		// 0 = none yet
} missionDown_t;

static missionDown_t missionDown[MAX_CLIENTS];
static int missionDownLevel = -1;
static vmCvar_t g_missionRevive;
static vmCvar_t g_missionPitRescue;

static void G_MissionDownInit(void)
{
	static qboolean registered = qfalse;
	if (!registered)
	{
		trap->Cvar_Register(&g_missionRevive, "g_missionRevive", "1", CVAR_ARCHIVE);
		trap->Cvar_Register(&g_missionPitRescue, "g_missionPitRescue", "1", CVAR_ARCHIVE);
		registered = qtrue;
	}
	trap->Cvar_Update(&g_missionRevive);
	trap->Cvar_Update(&g_missionPitRescue);
	if (missionDownLevel != level.startTime)
	{
		missionDownLevel = level.startTime;
		memset(missionDown, 0, sizeof missionDown);
		for (int i = 0; i < MAX_CLIENTS; i++)
		{
			missionDown[i].reviver = -1;
		}
	}
}

qboolean G_MissionIsDowned(const int clientNum)
{
	if (level.gametype != GT_SINGLE_PLAYER || clientNum < 0 || clientNum >= MAX_CLIENTS)
	{
		return qfalse;
	}
	G_MissionDownInit();
	return missionDown[clientNum].downTime ? qtrue : qfalse;
}

// ClientSpawn: a (re)spawned player is up
void G_MissionClearDown(const int clientNum)
{
	if (clientNum < 0 || clientNum >= MAX_CLIENTS)
	{
		return;
	}
	G_MissionDownInit();
	if (missionDown[clientNum].downTime)
	{
		g_entities[clientNum].flags &= ~FL_NOTARGET;
	}
	memset(&missionDown[clientNum], 0, sizeof missionDown[clientNum]);
	missionDown[clientNum].reviver = -1;
}

// a player in the game who is up (alive and not down)
static qboolean G_MissionPlayerUp(const gentity_t* ent)
{
	return G_MissionInGame(ent) && ent->health > 0 && ent->client->ps.pm_type != PM_DEAD
		&& !missionDown[ent->s.number].downTime ? qtrue : qfalse;
}

static qboolean G_MissionSomeoneElseUp(const int clientNum)
{
	for (int i = 0; i < level.maxclients; i++)
	{
		if (i != clientNum && G_MissionPlayerUp(&g_entities[i]))
		{
			return qtrue;
		}
	}
	return qfalse;
}

/*
Pit rescue (JACoop's, g_missionPitRescue, default 1): where each player last stood on solid ground, alive, up and
nearly still, so a fall into a pit (or a trigger_hurt) puts him down back there instead of killing him. The spot is
his own and a few seconds old at most, so a teammate can walk to it and revive him.
*/
static qboolean G_MissionOnSolidGround(const gentity_t* ent)
{
	trace_t tr;
	vec3_t end;

	VectorCopy(ent->r.currentOrigin, end);
	end[2] -= 24.0f;
	trap->Trace(&tr, ent->r.currentOrigin, ent->r.mins, ent->r.maxs, end, ent->s.number, ent->clipmask, qfalse, 0, 0);
	return !tr.startsolid && !tr.allsolid && tr.fraction < 1.0f && tr.plane.normal[2] >= 0.7f ? qtrue : qfalse;
}

static void G_MissionRememberSafeGround(const gentity_t* ent)
{
	missionDown_t* d = &missionDown[ent->s.number];

	if (!G_MissionOnSolidGround(ent)
		|| VectorLengthSquared(ent->client->ps.velocity) > 40.0f * 40.0f	// a jump, a slide: wait for him to settle
		|| ent->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)
		|| ent->client->ps.emplacedIndex // on an emplaced gun
		|| ent->client->ps.m_iVehicleNum)
	{
		return;
	}
	VectorCopy(ent->r.currentOrigin, d->safeOrigin);
	d->safeYaw = ent->client->ps.viewangles[YAW];
	d->safeTime = level.time;
}

static void G_MissionPitRescue(gentity_t* ent, const missionDown_t* d)
{
	trace_t tr;
	vec3_t org, end, angles;

	// on the floor under the spot
	VectorCopy(d->safeOrigin, org);
	VectorCopy(org, end);
	end[2] -= 256.0f;
	trap->Trace(&tr, org, ent->r.mins, ent->r.maxs, end, ent->s.number, MASK_PLAYERSOLID, qfalse, 0, 0);
	if (!tr.startsolid && !tr.allsolid && tr.fraction < 1.0f)
	{
		VectorCopy(tr.endpos, org);
	}

	G_SetOrigin(ent, org);
	VectorCopy(org, ent->client->ps.origin);
	VectorClear(ent->client->ps.velocity);
	ent->client->ps.pm_time = 0;
	ent->client->ps.pm_flags &= ~PMF_TIME_KNOCKBACK;
	VectorSet(angles, 0, d->safeYaw, 0);
	SetClientViewAngle(ent, angles);
	ent->client->ps.groundEntityNum = ENTITYNUM_WORLD;
	ent->client->ps.eFlags ^= EF_TELEPORT_BIT; // a jump, not a slide up from the bottom of the pit
	trap->LinkEntity((sharedEntity_t*)ent);
}

/*
G_Damage, when a hit would kill targ (or targ is already down): qtrue = he stays down at 1 health instead.
*/
qboolean G_MissionDownPlayer(gentity_t* targ, const int mod)
{
	if (level.gametype != GT_SINGLE_PLAYER || !targ || !targ->client || targ->NPC || targ->s.number >= MAX_CLIENTS
		|| !G_MissionInGame(targ) || targ->client->ps.pm_type == PM_DEAD)
	{
		return qfalse;
	}
	G_MissionDownInit();
	missionDown_t* d = &missionDown[targ->s.number];
	if (d->downTime)
	{
		return qtrue; // already down: damage doesn't finish him (bleeding out, giving up or a team wipe do)
	}
	if (!g_missionRevive.integer || targ->client->ps.m_iVehicleNum)
	{
		return qfalse;
	}
	switch (mod)
	{
	case MOD_WATER:
	case MOD_SLIME:
	case MOD_LAVA:
	case MOD_CRUSH:
	case MOD_TELEFRAG:
	case MOD_SUICIDE:
		return qfalse; // drowned, burnt, crushed, "kill": straight to death
	default:
		break;
	}
	if (!G_MissionSomeoneElseUp(targ->s.number))
	{
		return qfalse; // the last one up: the team is down
	}

	// a fall into a pit (or a trigger_hurt): back on the last ground he stood on, down there (pit rescue)
	const qboolean pit = mod == MOD_TRIGGER_HURT || mod == MOD_FALLING && !G_MissionOnSolidGround(targ) ? qtrue : qfalse;
	if (pit)
	{
		if (!g_missionPitRescue.integer || !d->safeTime)
		{
			return qfalse;
		}
		G_MissionPitRescue(targ, d);
	}

	d->downTime = level.time;
	d->reviver = -1;
	d->reviveStart = 0;
	d->giveUpStart = 0;
	d->nextNote = level.time + 5000;
	targ->flags |= FL_NOTARGET;
	targ->client->ps.fd.forcePowersActive = 0;
	trap->SendServerCommand(-1, va("cp \"%s" S_COLOR_RED " is down!\n" S_COLOR_WHITE "Stand next to him and hold USE to revive.\n\"",
		targ->client->pers.netname));
	return qtrue;
}

static void G_MissionReallyDie(gentity_t* ent)
{
	missionDown[ent->s.number].downTime = 0;
	missionDown[ent->s.number].reviver = -1;
	ent->flags &= ~FL_NOTARGET;
	ent->health = 0;
	ent->client->ps.stats[STAT_HEALTH] = 0;
	player_die(ent, ent, ent, 100000, MOD_SUICIDE);
}

/*
ClientThink, every player in Missions: a downed player's controls, pose, bleed-out and give-up; a player who is up
reviving the downed teammate he stands next to.
*/
void G_MissionDownThink(gentity_t* ent, usercmd_t* ucmd)
{
	if (level.gametype != GT_SINGLE_PLAYER || !ent->client || ent->s.number >= MAX_CLIENTS || ent->NPC)
	{
		return;
	}
	G_MissionDownInit();
	missionDown_t* d = &missionDown[ent->s.number];

	if (d->downTime)
	{
		if (ent->client->ps.pm_type == PM_DEAD || ent->health <= 0 || !G_MissionInGame(ent))
		{
			G_MissionClearDown(ent->s.number);
			return;
		}

		const qboolean jumping = ucmd->upmove > 0 ? qtrue : qfalse;
		if (jumping && !d->giveUpStart)
		{
			d->giveUpStart = level.time;
		}
		else if (!jumping)
		{
			d->giveUpStart = 0;
		}

		if (!G_MissionSomeoneElseUp(ent->s.number) || level.time - d->downTime > MISSION_DOWN_BLEED
			|| d->giveUpStart && level.time - d->giveUpStart > MISSION_DOWN_GIVEUP)
		{
			G_MissionReallyDie(ent);
			return;
		}

		// lying there: no moving, fighting or using
		ucmd->forwardmove = 0;
		ucmd->rightmove = 0;
		ucmd->upmove = 0;
		ucmd->buttons = 0;
		ent->health = 1;
		ent->client->ps.stats[STAT_HEALTH] = 1;
		ent->client->ps.velocity[0] = ent->client->ps.velocity[1] = 0;
		// on the ground: MP's knockdown (pmove holds BOTH_KNOCKDOWN1) kept going while he is down
		ent->client->ps.forceHandExtend = HANDEXTEND_KNOCKDOWN;
		ent->client->ps.forceHandExtendTime = level.time + 500;
		ent->client->ps.forceDodgeAnim = 0;

		if (level.time >= d->nextNote && d->reviver < 0)
		{
			d->nextNote = level.time + 5000;
			trap->SendServerCommand(ent->s.number, va("cp \"" S_COLOR_RED "You are down (%i s).\n" S_COLOR_WHITE
				"A teammate can revive you. Hold JUMP to give up.\n\"",
				(MISSION_DOWN_BLEED - (level.time - d->downTime) + 999) / 1000));
		}
		return;
	}

	// up: reviving?
	if (!G_MissionPlayerUp(ent))
	{
		return;
	}
	G_MissionRememberSafeGround(ent);
	int target = -1;
	if (ucmd->buttons & BUTTON_USE)
	{
		float best = MISSION_DOWN_RANGE;
		for (int i = 0; i < level.maxclients; i++)
		{
			const gentity_t* other = &g_entities[i];
			if (i == ent->s.number || !missionDown[i].downTime || !G_MissionInGame(other))
			{
				continue;
			}
			const float dist = Distance(ent->client->ps.origin, other->client->ps.origin);
			if (dist < best)
			{
				best = dist;
				target = i;
			}
		}
	}

	// let go of anyone else this player was reviving
	for (int i = 0; i < level.maxclients; i++)
	{
		if (missionDown[i].reviver == ent->s.number && i != target)
		{
			missionDown[i].reviver = -1;
			missionDown[i].reviveStart = 0;
		}
	}
	if (target < 0)
	{
		return;
	}

	missionDown_t* td = &missionDown[target];
	gentity_t* downed = &g_entities[target];
	if (td->reviver != ent->s.number)
	{
		if (td->reviver >= 0)
		{
			return; // someone else is on it
		}
		td->reviver = ent->s.number;
		td->reviveStart = level.time;
		td->nextNote = 0;
	}
	if (level.time - td->reviveStart >= MISSION_DOWN_REVIVE)
	{
		int health = downed->client->ps.stats[STAT_MAX_HEALTH] * 40 / 100;
		if (health < 1)
		{
			health = 1;
		}
		G_MissionClearDown(target);
		downed->health = health;
		downed->client->ps.stats[STAT_HEALTH] = health;
		// gets up (the knockdown's getup anim)
		downed->client->ps.forceHandExtend = HANDEXTEND_KNOCKDOWN;
		downed->client->ps.forceDodgeAnim = 1;
		downed->client->ps.forceHandExtendTime = level.time + 1000;
		trap->SendServerCommand(-1, va("cp \"%s" S_COLOR_GREEN " revived %s" S_COLOR_GREEN ".\n\"",
			ent->client->pers.netname, downed->client->pers.netname));
		return;
	}
	if (level.time >= td->nextNote)
	{
		td->nextNote = level.time + 500;
		const int left = (MISSION_DOWN_REVIVE - (level.time - td->reviveStart) + 999) / 1000;
		trap->SendServerCommand(ent->s.number, va("cp \"Reviving %s" S_COLOR_WHITE "... %i\n\"", downed->client->pers.netname, left));
		trap->SendServerCommand(target, va("cp \"%s" S_COLOR_WHITE " is reviving you... %i\n\"", ent->client->pers.netname, left));
	}
}