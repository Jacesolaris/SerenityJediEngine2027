/*
===========================================================================
Pazaak board (UI)

Port of the Jedi Knight Galaxies Pazaak UI module (written by BobaFett).
The board shows the menus "sje_pazaakholo" (the game) and "sje_cardselholo" (side deck
selection) from ui/pazaak/sje_pazaak.menu and follows the "pzk" instructions of the game:

- multiplayer: the game module runs the match and sends "pzk ..." server commands, the
  cgame passes them on as the "uipzk" console command; the board answers with "~pzk ...".
- singleplayer: the match runs right here in the UI against the AI (pazaak_core), started
  by the game module with "uipzk_start"; the world is paused while the board is open.

The menu items use ownerdraw numbers PZK_OD_BASE + type * 100 + slot, and "uiScript pzk_*"
for their scripts.
===========================================================================
*/

#ifdef PZK_SP
#include "../server/exe_headers.h"
#include "ui_local.h"
#include "menudef.h"
#include "ui_shared.h"
#include "pazaak_core.h"
#include "ui/ui_pazaak.h"
#else
#include "ui_local.h"
#include "../game/pazaak_core.h"
#include "ui/ui_pazaak.h"
#endif

/*
===========================================================================
Engine glue
===========================================================================
*/

#ifdef PZK_SP
typedef const char** pzkArgs_t;
extern uiInfo_t uiInfo;
menuDef_t* Menus_FindByName(const char* p);
void Menus_CloseByName(const char* p);
void UI_ParseMenu(const char* menuFile);
qboolean CL_IsRunningInGameCinematic();
qboolean CL_InGameCinematicOnStandBy();
#define PZK_RegisterShader(n)		ui.R_RegisterShaderNoMip(n)
#define PZK_RegisterSound(n)		ui.S_RegisterSound(n)
#define PZK_SetColor(c)				ui.R_SetColor(c)
#define PZK_DrawPic(x, y, w, h, s)	ui.R_DrawStretchPic(x, y, w, h, 0, 0, 1, 1, s)
#define PZK_TextWidth(t, f, s)		((float)ui.R_Font_StrLenPixels(t, f, s))
#define PZK_DrawText(x, y, t, c, f, s)	ui.R_Font_DrawString((int)(x), (int)(y), t, c, f, -1, s)
#define PZK_Millis()				ui.Milliseconds()
#define PZK_LocalSound(s)			ui.S_StartLocalSound(s, CHAN_LOCAL_SOUND)
#define PZK_GetCatcher()			trap_Key_GetCatcher()
#define PZK_SetCatcher(c)			trap_Key_SetCatcher(c)
#define PZK_CvarSet(n, v)			ui.Cvar_Set(n, v)
#define PZK_Font()					Pazaak_Font()
#define PZK_RegisterFont(n)			ui.R_RegisterFont(n)
#define PZK_DefaultFont()			(uiInfo.uiDC.Assets.qhMediumFont)
#define PZK_FONT_SCALE				0.5f
#else
typedef char** pzkArgs_t;
extern displayContextDef_t* DC;
void UI_ParseMenu(const char* menuFile);
int MenuFontToHandle(int i_menu_font);
#define PZK_RegisterShader(n)		trap->R_RegisterShaderNoMip(n)
#define PZK_RegisterSound(n)		trap->S_RegisterSound(n)
#define PZK_SetColor(c)				trap->R_SetColor(c)
#define PZK_DrawPic(x, y, w, h, s)	trap->R_DrawStretchPic(x, y, w, h, 0, 0, 1, 1, s)
#define PZK_TextWidth(t, f, s)		((float)trap->R_Font_StrLenPixels(t, f, s))
#define PZK_DrawText(x, y, t, c, f, s)	trap->R_Font_DrawString((int)(x), (int)(y), t, c, f, -1, s)
#define PZK_Millis()				trap->Milliseconds()
#define PZK_LocalSound(s)			trap->S_StartLocalSound(s, CHAN_LOCAL_SOUND)
#define PZK_GetCatcher()			trap->Key_GetCatcher()
#define PZK_SetCatcher(c)			trap->Key_SetCatcher(c)
#define PZK_CvarSet(n, v)			trap->Cvar_Set(n, v)
#define PZK_Font()					Pazaak_Font()
#define PZK_RegisterFont(n)			trap->R_RegisterFont(n)
#define PZK_DefaultFont()			MenuFontToHandle(1)
#define PZK_FONT_SCALE				0.5f
#endif

#define PZK_Copy4(a, b) ((b)[0] = (a)[0], (b)[1] = (a)[1], (b)[2] = (a)[2], (b)[3] = (a)[3])

#ifdef PZK_SP
#define PZK_MENU_FILE	"ui/pazaak/sje_pazaak.menu"
#else
#define PZK_MENU_FILE	"ui/pazaak/sje_pazaak_mp.menu" // numbers instead of the menudef.h names, it is loaded on its own
#endif
#define PZK_MENU_BOARD	"sje_pazaakholo"
#define PZK_MENU_CARDS	"sje_cardselholo"

// the shop and the talk question (2026-10-31, at the end of the file)
static qboolean UI_Shop_OwnerDraw(int ownerDraw, float x, float y, float w, float h);
static qboolean UI_Shop_Script(const char* name, void* argsPtr);
static void UI_Shop_Process(int argc, const char** argv);
#ifdef PZK_SP
static qboolean UI_Shop_Key(int key);
#endif

static void Pazaak_SendAction(const char* text);

/*
===========================================================================
State
===========================================================================
*/

static vec4_t cardColor = { 1.0f, 1.0f, 1.0f, 0.8f };
static vec4_t cardColorStand = { 0.6f, 0.6f, 0.6f, 0.8f };
static vec4_t pzkWhite = { 1.0f, 1.0f, 1.0f, 1.0f };

static int HandSlotHover[4];
static int CardSelSlotHover[23];
static int CardSDHover[10];

// Button IDs
enum
{
	PBUTTON_ENDTURN = 0,
	PBUTTON_STAND,
	PBUTTON_FORFEIT,
	PBUTTON_EXIT,
	PBUTTON_CONTINUE
};

// Dialog types and responses
enum
{
	PDLG_OK,
	PDLG_YESNO
};

enum
{
	PDLGRESP_OK,
	PDLGRESP_YES,
	PDLGRESP_NO
};

static int activeBoard = 0; // 1 = Pazaak, 2 = Card selection
static int HowToOpen = 0;   // the "how to play" page covers the card selection
static void Pazaak_HowTo(int open);
typedef void (*PDlgCallback)(int response);

static struct
{
	int InUse;
	char line1[256];
	char line2[256];
	int type; // PDLG_xxx
	PDlgCallback callback;
} PDlgData;

typedef struct
{
	int cardid;
	int param;
} pzk_card;

typedef struct
{
	int amount; // Amount we have of this card
	int inuse;  // Amount of cards of this type in the side deck
} pzk_deckcard;

static struct
{
	int active;
	pzk_deckcard playercards[23];
	int sidedeck[10];
	int forfeitOnQuit;
	int waitingStartTime;   // 0 = don't show "waiting for opponent"
	pzk_card field[18];     // 0-8 = player, 9-17 = opponent
	pzk_card hand[8];       // 0-3 = player, 4-7 = opponent
	int turn;               // 1 = mine, 2 = opponent, 0 = intermission
	int score_player;
	int score_opponent;
	int total_player;
	int total_opponent;
	char name_player[64];
	char name_opponent[64];
	int standing_player;
	int standing_opponent;
	int dlgid;
	int timeout;            // time our timeout expires (ms), 0 if disabled
	int buttonstate;        // 0 = disabled, 1 = enabled, 2 = all but cards
	int pausedGame;         // SP: we paused the game
} PzkState;

static struct
{
	qhandle_t deckCard;
	qhandle_t plusCard;
	qhandle_t minusCard;
	qhandle_t flipPlusCard;
	qhandle_t flipMinusCard;
	qhandle_t specialCard;
	qhandle_t backSide;
} PzkShaders;

static struct
{
	sfxHandle_t drawCard;
	sfxHandle_t playCard;
	sfxHandle_t turnSwitch;
	sfxHandle_t bust;
	sfxHandle_t loseSet;
	sfxHandle_t winSet;
	sfxHandle_t tieSet;
	sfxHandle_t loseMatch;
	sfxHandle_t winMatch;
} PzkSounds;

// The board text font: "ergoec" like the Jedi Knight Galaxies board (the mods map the menu font numbers to other fonts,
// MovieDuels MP even to Aurebesh)
static int pzkFont = 0; // registered again at every start (handles change with vid_restart)

static int Pazaak_Font(void)
{
	if (!pzkFont)
	{
		pzkFont = PZK_RegisterFont("ergoec");
		if (!pzkFont)
		{
			pzkFont = PZK_DefaultFont();
		}
	}
	return pzkFont;
}

static void Pazaak_PlaySound(const sfxHandle_t s)
{
	if (s)
	{
		PZK_LocalSound(s);
	}
}

static void Pazaak_CacheShaders(void)
{
	memset(&PzkShaders, 0, sizeof(PzkShaders));
	if (DC && DC->glconfig.vidWidth > 800)
	{
		PzkShaders.backSide = PZK_RegisterShader("gfx/minigames/pazaak/cards/back_m.png");
		PzkShaders.deckCard = PZK_RegisterShader("gfx/minigames/pazaak/cards/green_m.png");
		PzkShaders.flipMinusCard = PZK_RegisterShader("gfx/minigames/pazaak/cards/multi_neg_m.png");
		PzkShaders.flipPlusCard = PZK_RegisterShader("gfx/minigames/pazaak/cards/multi_pos_m.png");
		PzkShaders.minusCard = PZK_RegisterShader("gfx/minigames/pazaak/cards/red_m.png");
		PzkShaders.plusCard = PZK_RegisterShader("gfx/minigames/pazaak/cards/blue_m.png");
		PzkShaders.specialCard = PZK_RegisterShader("gfx/minigames/pazaak/cards/yellow_m.png");
	}
	else
	{
		PzkShaders.backSide = PZK_RegisterShader("gfx/minigames/pazaak/cards/back_l.png");
		PzkShaders.deckCard = PZK_RegisterShader("gfx/minigames/pazaak/cards/green_l.png");
		PzkShaders.flipMinusCard = PZK_RegisterShader("gfx/minigames/pazaak/cards/multi_neg_l.png");
		PzkShaders.flipPlusCard = PZK_RegisterShader("gfx/minigames/pazaak/cards/multi_pos_l.png");
		PzkShaders.minusCard = PZK_RegisterShader("gfx/minigames/pazaak/cards/red_l.png");
		PzkShaders.plusCard = PZK_RegisterShader("gfx/minigames/pazaak/cards/blue_l.png");
		PzkShaders.specialCard = PZK_RegisterShader("gfx/minigames/pazaak/cards/yellow_l.png");
	}
}

static void Pazaak_CacheSounds(void)
{
	memset(&PzkSounds, 0, sizeof(PzkSounds));
	PzkSounds.drawCard = PZK_RegisterSound("sound/minigames/pazaak/draw.wav");
	PzkSounds.playCard = PZK_RegisterSound("sound/minigames/pazaak/playcard.wav");
	PzkSounds.turnSwitch = PZK_RegisterSound("sound/minigames/pazaak/turn.wav");
	PzkSounds.bust = PZK_RegisterSound("sound/minigames/pazaak/bust.wav");
	PzkSounds.loseSet = PZK_RegisterSound("sound/minigames/pazaak/loseset.wav");
	PzkSounds.winSet = PZK_RegisterSound("sound/minigames/pazaak/winset.wav");
	PzkSounds.tieSet = PZK_RegisterSound("sound/minigames/pazaak/tie.wav");
	PzkSounds.loseMatch = PZK_RegisterSound("sound/minigames/pazaak/losematch.wav");
	PzkSounds.winMatch = PZK_RegisterSound("sound/minigames/pazaak/winmatch.wav");
}

static menuDef_t* Pazaak_Menu(const int board)
{
	return Menus_FindByName(board == 1 ? PZK_MENU_BOARD : PZK_MENU_CARDS);
}

// Like Menu_ShowItemByName in ui_shared (static or with another signature in some of the mods): items by name or group
static void Pazaak_Show(menuDef_t* menu, const char* p, const qboolean show)
{
	int i = 0;
	if (!menu)
	{
		return;
	}
	for (i = 0; i < menu->itemCount; i++)
	{
		itemDef_t* item = menu->items[i];
		if (!item || !((item->window.name && !Q_stricmp(item->window.name, p)) || (item->window.group && !Q_stricmp(item->window.group, p))))
		{
			continue;
		}
		if (show)
		{
			item->window.flags |= WINDOW_VISIBLE;
		}
		else
		{
			item->window.flags &= ~(WINDOW_VISIBLE | WINDOW_HASFOCUS);
		}
	}
}

// Like Menu_ClearFocus in ui_shared (static there)
static void Pazaak_ClearFocus(menuDef_t* menu)
{
	int i = 0;
	if (!menu)
	{
		return;
	}
	for (i = 0; i < menu->itemCount; i++)
	{
		if (menu->items[i])
		{
			menu->items[i]->window.flags &= ~WINDOW_HASFOCUS;
		}
	}
}

/*
===========================================================================
Dialogs
===========================================================================
*/

static void Pazaak_Dialog_Show(const char* line1, const char* line2, const int type, const PDlgCallback callback)
{
	menuDef_t* menu = NULL;
	if (!callback || !activeBoard)
	{
		return;
	}
	if (HowToOpen)
	{
		Pazaak_HowTo(0); // the dialog shows over the card selection
	}

	PDlgData.InUse = 1;
	Q_strncpyz(PDlgData.line1, line1 ? line1 : "", sizeof(PDlgData.line1));
	Q_strncpyz(PDlgData.line2, line2 ? line2 : "", sizeof(PDlgData.line2));
	PDlgData.type = type;
	PDlgData.callback = callback;

	menu = Pazaak_Menu(activeBoard);
	if (!menu)
	{
		return;
	}
	if (activeBoard == 1)
	{
		Pazaak_Show(menu, "buttons", qtrue);
		Pazaak_Show(menu, "buttons2", qtrue);
		Pazaak_Show(menu, "buttons_hover", qfalse);
		Pazaak_Show(menu, "buttons_hover2", qfalse);
	}
	else
	{
		Pazaak_Show(menu, "buttons", qtrue);
		Pazaak_Show(menu, "buttons_hover", qfalse);
	}
	Pazaak_ClearFocus(menu);

	Pazaak_Show(menu, "dialog_txt", qtrue);
	Pazaak_Show(menu, type == PDLG_OK ? "dialog_ok" : "dialog_yesno", qtrue);

	if (activeBoard == 1)
	{
		Pazaak_Show(menu, "buttons_hitzone", qfalse);
		Pazaak_Show(menu, "buttons_forfeit_hitzone", qfalse);
		Pazaak_Show(menu, "buttons_hand", qfalse);
	}
	else
	{
		Pazaak_Show(menu, "card_buttons", qfalse);
		Pazaak_Show(menu, "sd_buttons", qfalse);
		Pazaak_Show(menu, "buttons_hitzone", qfalse);
	}
}

static void Pazaak_ApplyButtonState(menuDef_t* menu)
{
	int i = 0;
	switch (PzkState.buttonstate)
	{
	case 0: // Disable all
		Pazaak_Show(menu, "buttons_hand", qfalse);
		Pazaak_Show(menu, "buttons_hitzone", qfalse);
		Pazaak_Show(menu, "buttons_hover", qfalse);
		Pazaak_Show(menu, "buttons", qtrue);
		for (i = 0; i < 4; i++)
		{
			HandSlotHover[i] = 0;
		}
		break;
	case 1: // Enable all
		for (i = 0; i < 4; i++)
		{
			if (PzkState.hand[i].cardid != PZCARD_NONE)
			{
				Pazaak_Show(menu, va("button_hand%i", i + 1), qtrue);
			}
		}
		Pazaak_Show(menu, "buttons_hitzone", qtrue);
		break;
	case 2: // Enable all but the cards
		Pazaak_Show(menu, "buttons_hand", qfalse);
		Pazaak_Show(menu, "buttons_hitzone", qtrue);
		for (i = 0; i < 4; i++)
		{
			HandSlotHover[i] = 0;
		}
		break;
	default:
		break;
	}
}

// The buttons of the card selection: Exit / Continue / How to play, the cards he has, his side deck
static void Pazaak_ShowCardSelButtons(menuDef_t* menu)
{
	int i = 0;

	Pazaak_Show(menu, "buttons_hitzone", qtrue);

	Pazaak_Show(menu, "card_buttons", qfalse);
	for (i = 0; i < 23; i++)
	{
		if (PzkState.playercards[i].amount > 0)
		{
			Pazaak_Show(menu, va("card_B%i", i), qtrue);
		}
	}

	Pazaak_Show(menu, "sd_buttons", qfalse);
	for (i = 0; i < 10; i++)
	{
		if (PzkState.sidedeck[i] >= PZCARD_PLUS_1)
		{
			Pazaak_Show(menu, va("sd_B%i", i), qtrue);
		}
	}
}

static void Pazaak_Dialog_Close(void)
{
	menuDef_t* menu = NULL;
	if (!activeBoard)
	{
		return;
	}

	PDlgData.InUse = 0;

	menu = Pazaak_Menu(activeBoard);
	if (!menu)
	{
		return;
	}
	Pazaak_Show(menu, "dialog_ok", qfalse);
	Pazaak_Show(menu, "dialog_yesno", qfalse);
	Pazaak_Show(menu, "dialog_txt", qfalse);
	Pazaak_Show(menu, "dialog", qfalse);

	if (activeBoard == 1)
	{
		Pazaak_Show(menu, "buttons_forfeit_hitzone", qtrue);
		Pazaak_ApplyButtonState(menu);
	}
	else
	{
		Pazaak_ShowCardSelButtons(menu);
	}
}

// The "how to play" page: hides the card selection under it (and its buttons) while it is open
static void Pazaak_HowTo(const int open)
{
	menuDef_t* menu = Pazaak_Menu(2);
	if (!menu)
	{
		HowToOpen = 0;
		return;
	}
	if (open)
	{
		if (HowToOpen || PDlgData.InUse || activeBoard != 2)
		{
			return;
		}
		HowToOpen = 1;
		Pazaak_Show(menu, "none", qfalse); // the board and the cards
		Pazaak_Show(menu, "buttons", qfalse);
		Pazaak_Show(menu, "buttons_hover", qfalse);
		Pazaak_Show(menu, "buttons_hitzone", qfalse);
		Pazaak_Show(menu, "card_buttons", qfalse);
		Pazaak_Show(menu, "sd_buttons", qfalse);
		Pazaak_Show(menu, "howto", qtrue);
		Pazaak_Show(menu, "howto_hover", qfalse);
		Pazaak_Show(menu, "howto_hitzone", qtrue);
	}
	else
	{
		if (!HowToOpen)
		{
			return;
		}
		HowToOpen = 0;
		Pazaak_Show(menu, "howto", qfalse);
		Pazaak_Show(menu, "howto_hover", qfalse);
		Pazaak_Show(menu, "howto_hitzone", qfalse);
		Pazaak_Show(menu, "none", qtrue);
		Pazaak_Show(menu, "buttons", qtrue);
		Pazaak_Show(menu, "buttons_hover", qfalse);
		Pazaak_ShowCardSelButtons(menu);
	}
	Pazaak_ClearFocus(menu);
}

static void Pazaak_Close(void)
{
	Menus_CloseByName(PZK_MENU_BOARD);
	Menus_CloseByName(PZK_MENU_CARDS);
	activeBoard = 0;
}

/*
===========================================================================
Cards
===========================================================================
*/

static qhandle_t Pazaak_GetCardShader(const int id, const int param)
{
	if (id == PZCARD_BACK)
	{
		return PzkShaders.backSide;
	}
	if (id >= PZCARD_NORMAL_1 && id <= PZCARD_NORMAL_10)
	{
		return PzkShaders.deckCard;
	}
	if (id >= PZCARD_PLUS_1 && id <= PZCARD_PLUS_6)
	{
		return PzkShaders.plusCard;
	}
	if (id >= PZCARD_MINUS_1 && id <= PZCARD_MINUS_6)
	{
		return PzkShaders.minusCard;
	}
	if (id >= PZCARD_FLIP_1 && id <= PZCARD_FLIP_6)
	{
		return param == 1 || param == 3 ? PzkShaders.flipMinusCard : PzkShaders.flipPlusCard;
	}
	if (id >= PZCARD_TIEBREAKER && id <= PZCARD_3N6)
	{
		return PzkShaders.specialCard;
	}
	return 0;
}

// Value of a card in neutral state (card selection screen)
static const char* Pazaak_GetNeutralCardValue(const int id)
{
	if (id >= PZCARD_NORMAL_1 && id <= PZCARD_NORMAL_10)
	{
		return va("%i", id - PZCARD_NORMAL_1 + 1);
	}
	if (id >= PZCARD_PLUS_1 && id <= PZCARD_PLUS_6)
	{
		return va("+%i", id - PZCARD_PLUS_1 + 1);
	}
	if (id >= PZCARD_MINUS_1 && id <= PZCARD_MINUS_6)
	{
		return va("-%i", id - PZCARD_MINUS_1 + 1);
	}
	if (id >= PZCARD_FLIP_1 && id <= PZCARD_FLIP_6)
	{
		return va("+/-%i", id - PZCARD_FLIP_1 + 1);
	}
	switch (id)
	{
	case PZCARD_TIEBREAKER: return "+/-1T";
	case PZCARD_DOUBLE: return "D";
	case PZCARD_FLIP12: return "+/-1/2";
	case PZCARD_2N4: return "2&4";
	case PZCARD_3N6: return "3&6";
	default: return "";
	}
}

static const char* Pazaak_GetCardValue(const int id, const int param)
{
	if (id >= PZCARD_NORMAL_1 && id <= PZCARD_NORMAL_10)
	{
		return va(param ? "-%i" : "%i", id - PZCARD_NORMAL_1 + 1);
	}
	if (id >= PZCARD_PLUS_1 && id <= PZCARD_PLUS_6)
	{
		return va(param ? "-%i" : "+%i", id - PZCARD_PLUS_1 + 1);
	}
	if (id >= PZCARD_MINUS_1 && id <= PZCARD_MINUS_6)
	{
		return va(param ? "+%i" : "-%i", id - PZCARD_MINUS_1 + 1);
	}
	if (id >= PZCARD_FLIP_1 && id <= PZCARD_FLIP_6)
	{
		return va(param == 1 || param == 2 ? "-%i" : "+%i", id - PZCARD_FLIP_1 + 1);
	}
	switch (id)
	{
	case PZCARD_TIEBREAKER:
		return param ? "-1T" : "+1T";
	case PZCARD_DOUBLE:
		return param ? va("%i", param) : "D";
	case PZCARD_FLIP12:
		switch (param)
		{
		case 0: return "+1";
		case 1: return "-1";
		case 2: return "+2";
		case 3: return "-2";
		default: return "";
		}
	case PZCARD_2N4:
		return param ? "0" : "2&4";
	case PZCARD_3N6:
		return param ? "0" : "3&6";
	default:
		return "";
	}
}

static int Pazaak_CanCardFlip(const int id)
{
	return (id >= PZCARD_FLIP_1 && id <= PZCARD_FLIP_6) || id == PZCARD_TIEBREAKER || id == PZCARD_FLIP12;
}

static int Pazaak_CanCardFlipValue(const int id)
{
	return id == PZCARD_FLIP12;
}

static void Pazaak_FlipHandCard(const int slot)
{
	int param = 0;
	const int id = PzkState.hand[slot - 1].cardid;
	if (!PzkState.active || slot < 1 || slot > 4)
	{
		return;
	}
	param = PzkState.hand[slot - 1].param;
	if ((id >= PZCARD_FLIP_1 && id <= PZCARD_FLIP_6) || id == PZCARD_TIEBREAKER)
	{
		param = param ? 0 : 1;
	}
	else if (id == PZCARD_FLIP12)
	{
		param ^= 1;
	}
	PzkState.hand[slot - 1].param = param;
}

static void Pazaak_FlipHandCardValue(const int slot)
{
	if (!PzkState.active || slot < 1 || slot > 4)
	{
		return;
	}
	if (PzkState.hand[slot - 1].cardid == PZCARD_FLIP12)
	{
		PzkState.hand[slot - 1].param ^= 2;
	}
}

/*
===========================================================================
Owner draws
===========================================================================
*/

static void Pazaak_DrawCardText(const char* text, const float x, const float y, const float w, const float h)
{
	const float width = PZK_TextWidth(text, PZK_Font(), 1.0f) * PZK_FONT_SCALE;
	PZK_DrawText(x + w / 4 + (w / 4 - width / 2), y + h * 0.25f, text, pzkWhite, PZK_Font(), PZK_FONT_SCALE);
}

static void SJE_Pazaak_DrawCardSlot(const int slot, const float x, const float y, const float w, const float h)
{
	qhandle_t sh = 0;
	if (!PzkState.active || slot < 1 || slot > 18)
	{
		return;
	}
	if (slot > 9)
	{
		PZK_SetColor(PzkState.standing_opponent ? cardColorStand : cardColor);
	}
	else
	{
		PZK_SetColor(PzkState.standing_player ? cardColorStand : cardColor);
	}
	sh = Pazaak_GetCardShader(PzkState.field[slot - 1].cardid, PzkState.field[slot - 1].param);
	if (!sh)
	{
		PZK_SetColor(NULL);
		return;
	}
	PZK_DrawPic(x, y, w, h, sh);
	PZK_SetColor(NULL);
	Pazaak_DrawCardText(Pazaak_GetCardValue(PzkState.field[slot - 1].cardid, PzkState.field[slot - 1].param), x, y, w, h);
}

static void SJE_Pazaak_DrawSelCardSlot(const int slot, const float x, const float y, const float w, const float h)
{
	qhandle_t sh = 0;
	vec4_t color = { 0 };
	const char* text = NULL;

	if (!PzkState.active || slot < 0 || slot > 22 || !PzkState.playercards[slot].amount)
	{
		return;
	}

	PZK_Copy4(cardColor, color);
	if (CardSelSlotHover[slot])
	{
		color[3] *= 0.8f + 0.2f * (float)fabs(sin((float)PZK_Millis() / 200.f));
	}
	PZK_SetColor(color);

	sh = Pazaak_GetCardShader(slot + PZCARD_PLUS_1, 0);
	if (!sh)
	{
		PZK_SetColor(NULL);
		return;
	}
	PZK_DrawPic(x, y, w, h, sh);
	PZK_SetColor(NULL);
	Pazaak_DrawCardText(Pazaak_GetNeutralCardValue(slot + PZCARD_PLUS_1), x, y, w, h);

	text = va("%i", PzkState.playercards[slot].amount - PzkState.playercards[slot].inuse);
	PZK_DrawText(x + 7, y - 2, text, pzkWhite, PZK_Font() | 0x80000000, PZK_FONT_SCALE); // with drop shadow
}

static void SJE_Pazaak_DrawSideDeckSlot(const int slot, const float x, const float y, const float w, const float h)
{
	qhandle_t sh = 0;
	vec4_t color = { 0 };

	if (!PzkState.active || slot < 0 || slot > 9)
	{
		return;
	}

	PZK_Copy4(cardColor, color);
	if (CardSDHover[slot])
	{
		color[3] *= 0.8f + 0.2f * (float)fabs(sin((float)PZK_Millis() / 200.f));
	}
	PZK_SetColor(color);

	sh = Pazaak_GetCardShader(PzkState.sidedeck[slot], 0);
	if (!sh)
	{
		PZK_SetColor(NULL);
		return;
	}
	PZK_DrawPic(x, y, w, h, sh);
	PZK_SetColor(NULL);
	Pazaak_DrawCardText(Pazaak_GetNeutralCardValue(PzkState.sidedeck[slot]), x, y, w, h);
}

static void SJE_Pazaak_DrawHandSlot(const int slot, const float x, const float y, const float w, const float h)
{
	vec4_t color = { 0 };
	qhandle_t sh = 0;

	if (!PzkState.active || slot < 1 || slot > 8)
	{
		return;
	}

	PZK_Copy4(cardColor, color);
	if (slot < 5 && HandSlotHover[slot - 1])
	{
		color[3] *= 0.8f + 0.2f * (float)fabs(sin((float)PZK_Millis() / 200.f));
	}
	PZK_SetColor(color);
	sh = Pazaak_GetCardShader(PzkState.hand[slot - 1].cardid, PzkState.hand[slot - 1].param);
	if (!sh)
	{
		PZK_SetColor(NULL);
		return;
	}
	PZK_DrawPic(x, y, w, h, sh);
	PZK_SetColor(NULL);
	Pazaak_DrawCardText(Pazaak_GetCardValue(PzkState.hand[slot - 1].cardid, PzkState.hand[slot - 1].param), x, y, w, h);
}

static void SJE_Pazaak_DrawNames(const int player, float x, const float y)
{
	const char* text = NULL;
	char cleanText[64];
	vec4_t shadow = { 0.15f, 0.15f, 0.15f, 1.0f };

	if (!PzkState.active)
	{
		return;
	}
	text = player == 1 ? PzkState.name_player : PzkState.name_opponent;
	if (player == 2)
	{
		x -= PZK_TextWidth(text, PZK_Font(), 1.0f) * PZK_FONT_SCALE;
	}
	Q_strncpyz(cleanText, text, sizeof(cleanText));
	Q_StripColor(cleanText);
	PZK_DrawText(x - 0.6f, y - 0.75f, cleanText, shadow, PZK_Font(), PZK_FONT_SCALE * 1.02f);
	PZK_DrawText(x, y, text, pzkWhite, PZK_Font(), PZK_FONT_SCALE);
}

static void SJE_Pazaak_DrawPoints(const int player, float x, const float y)
{
	const char* text = NULL;

	if (!PzkState.active)
	{
		return;
	}
	if (player == 1)
	{
		text = PzkState.standing_player ? va("( %i )", PzkState.total_player) : va("( ^5%i ^7)", PzkState.total_player);
		x -= PZK_TextWidth(text, PZK_Font(), 1.0f) * PZK_FONT_SCALE;
	}
	else
	{
		text = PzkState.standing_opponent ? va("( %i )", PzkState.total_opponent) : va("( ^5%i ^7)", PzkState.total_opponent);
	}
	PZK_DrawText(x, y, text, pzkWhite, PZK_Font(), PZK_FONT_SCALE);
}

static void SJE_Pazaak_DrawDialog(const int line, float x, const float y, const float w)
{
	const char* text = NULL;
	vec4_t shadow = { 0.0f, 0.0f, 0.0f, 0.2f };
	float width = 0.0f;

	if (!PDlgData.InUse)
	{
		return;
	}
	text = line == 1 ? PDlgData.line1 : PDlgData.line2;
	width = PZK_TextWidth(text, PZK_Font(), 1.0f) * PZK_FONT_SCALE;
	x += w / 2 - width / 2;
	PZK_DrawText(x + 1, y + 1, text, shadow, PZK_Font(), PZK_FONT_SCALE);
	PZK_DrawText(x, y, text, pzkWhite, PZK_Font(), PZK_FONT_SCALE);
}

static void SJE_Pazaak_DrawTimeout(float x, const float y, const float w)
{
	const char* text = NULL;
	float width = 0.0f;
	int tr = 0;

	if (!PzkState.active || !PzkState.timeout)
	{
		return;
	}
	tr = (int)ceil((float)(PzkState.timeout - PZK_Millis()) / 1000.0f);
	if (tr < 0)
	{
		tr = 0;
	}
	text = tr <= 3 ? va("^1%i", tr) : va("%i", tr);
	width = PZK_TextWidth(text, PZK_Font(), 1.0f) * 0.6f;
	x += w / 2 - width / 2;
	PZK_DrawText(x, y, text, pzkWhite, PZK_Font(), 0.6f);
}

static void SJE_Pazaak_DrawWaiting(const float x, const float y, const float w, const float h, const qhandle_t shader)
{
	vec4_t color = { 1.0f, 1.0f, 1.0f, 0.0f };
	if (!PzkState.active || !PzkState.waitingStartTime || !shader)
	{
		return;
	}
	color[3] = (float)fabs(sin((float)(PzkState.waitingStartTime - PZK_Millis()) / 400.0f));
	PZK_SetColor(color);
	PZK_DrawPic(x, y, w, h, shader);
	PZK_SetColor(NULL);
}

// The "how to play" page of the card selection: '#' lines are headings, the others are wrapped to the width
static const char* const pzkHowTo[] =
{
	"#Goal",
	"End each set closer to 20 than your opponent without going over. The first player to win 3 sets wins the match.",
	"#Your side deck",
	"Click 10 of your cards to put them in your side deck, then press Continue. At the start of the match 4 of them "
	"are dealt into your hand at random. They must last the whole match: a card you play is gone.",
	"#Your turn",
	"A card from 1 to 10 is dealt onto your board. You may then play one card from your hand. Press End Turn to go "
	"on, or Stand to keep your total for the rest of the set.",
	"#Winning a set",
	"Over 20 at the end of your turn and you lose the set. Fill your board with 9 cards without going over and you "
	"win it. When both players stand, the higher total wins. A tie has no winner, unless a tiebreaker card was played.",
	"#The cards",
	"Blue cards add, red cards subtract, and the +/- cards can be played either way. Gold cards: +/-1T is a "
	"tiebreaker, D doubles your last card, +/-1/2 counts as 1 or 2. The 24 card flips the sign of the 2s and 4s on "
	"your board, the 36 card those of the 3s and 6s."
};

#define PZK_HOWTO_SCALE		0.47f	// body text
#define PZK_HOWTO_LINE		12.0f
#define PZK_HOWTO_HSCALE	0.54f	// headings
#define PZK_HOWTO_HLINE		14.5f

#define PZK_HOWTO_WORDGAP	3.0f	// extra space between words (the font's space is narrow)

// Draws (or only measures) the words of text[0..len) from x, with wider spaces; returns their width
static float SJE_Pazaak_HowToWords(const char* text, const int len, const float x, const float y, float* color,
	const float scale, const qboolean draw)
{
	const int font = PZK_Font();
	const float gap = PZK_TextWidth(" ", font, 1.0f) * scale + PZK_HOWTO_WORDGAP;
	char word[128];
	float width = 0.0f;
	int i = 0, words = 0;

	while (i < len)
	{
		int n = 0;
		while (i < len && text[i] == ' ')
		{
			i++;
		}
		if (i >= len)
		{
			break;
		}
		while (i + n < len && text[i + n] != ' ' && n < (int)sizeof(word) - 1)
		{
			n++;
		}
		memcpy(word, text + i, n);
		word[n] = 0;
		if (words++)
		{
			width += gap;
		}
		if (draw)
		{
			PZK_DrawText(x + width, y, word, color, font | 0x80000000, scale); // with a drop shadow
		}
		width += PZK_TextWidth(word, font, 1.0f) * scale;
		i += n;
	}
	return width;
}

static void SJE_Pazaak_DrawHowTo(const float x, float y, const float w)
{
	static vec4_t heading = { 0.35f, 0.8f, 1.0f, 1.0f };
	unsigned i = 0;

	// the title
	{
		const char* title = "HOW TO PLAY";
		const int len = (int)strlen(title);
		const float width = SJE_Pazaak_HowToWords(title, len, 0.0f, 0.0f, heading, 0.75f, qfalse);
		SJE_Pazaak_HowToWords(title, len, x + w / 2 - width / 2, y, heading, 0.75f, qtrue);
		y += 24.0f;
	}

	for (i = 0; i < sizeof(pzkHowTo) / sizeof(pzkHowTo[0]); i++)
	{
		const char* p = pzkHowTo[i];
		if (*p == '#')
		{
			y += 4.0f;
			SJE_Pazaak_HowToWords(p + 1, (int)strlen(p + 1), x, y, heading, PZK_HOWTO_HSCALE, qtrue);
			y += PZK_HOWTO_HLINE;
			continue;
		}
		// the paragraph, as many words on a line as fit in the width
		while (*p)
		{
			int len = 0, fit = 0;
			while (p[len])
			{
				int end = len;
				while (p[end] == ' ')
				{
					end++;
				}
				while (p[end] && p[end] != ' ')
				{
					end++;
				}
				if (fit && SJE_Pazaak_HowToWords(p, end, 0.0f, 0.0f, pzkWhite, PZK_HOWTO_SCALE, qfalse) > w)
				{
					break;
				}
				fit = end;
				len = end;
			}
			if (!fit)
			{
				fit = len;
			}
			SJE_Pazaak_HowToWords(p, fit, x, y, pzkWhite, PZK_HOWTO_SCALE, qtrue);
			y += PZK_HOWTO_LINE;
			p += fit;
			while (*p == ' ')
			{
				p++;
			}
		}
	}
}
qboolean UI_Pazaak_OwnerDraw(const int ownerDraw, const float x, const float y, const float w, const float h, const qhandle_t shader)
{
	int type = 0, id = 0;
	if (UI_Shop_OwnerDraw(ownerDraw, x, y, w, h))
	{
		return qtrue; // the shop (3000+)
	}
	if (ownerDraw < PZK_OD_BASE || ownerDraw >= PZK_OD_BASE + 1000)
	{
		return qfalse;
	}
	type = (ownerDraw - PZK_OD_BASE) / 100;
	id = (ownerDraw - PZK_OD_BASE) % 100;

	switch (type)
	{
	case PZK_OD_BCARD: SJE_Pazaak_DrawCardSlot(id, x, y, w, h); break;
	case PZK_OD_BHAND: SJE_Pazaak_DrawHandSlot(id, x, y, w, h); break;
	case PZK_OD_NAMES: SJE_Pazaak_DrawNames(id, x, y); break;
	case PZK_OD_POINTS: SJE_Pazaak_DrawPoints(id, x, y); break;
	case PZK_OD_DLGTEXT: SJE_Pazaak_DrawDialog(id, x, y, w); break;
	case PZK_OD_TIMEOUT: SJE_Pazaak_DrawTimeout(x, y, w); break;
	case PZK_OD_SELCARD: SJE_Pazaak_DrawSelCardSlot(id, x, y, w, h); break;
	case PZK_OD_SELSD: SJE_Pazaak_DrawSideDeckSlot(id, x, y, w, h); break;
	case PZK_OD_WAITING: SJE_Pazaak_DrawWaiting(x, y, w, h, shader); break;
	case PZK_OD_HOWTO: SJE_Pazaak_DrawHowTo(x, y, w); break;
	default: break;
	}
	return qtrue;
}

/*
===========================================================================
Menu scripts ("uiScript pzk_*")
===========================================================================
*/

static void Pazaak_Dlg_Null(int response);

static void Pazaak_Dlg_Exit(const int response)
{
	Pazaak_Dialog_Close();
	if (response == PDLGRESP_YES)
	{
		Pazaak_SendAction("quit");
	}
}

static void Pazaak_Dlg_Forfeit(const int response)
{
	Pazaak_Dialog_Close();
	if (response == PDLGRESP_YES)
	{
		Pazaak_SendAction("forfeit");
	}
}

static void Pazaak_Dlg_Null(const int response)
{
	(void)response;
	Pazaak_Dialog_Close();
	PzkState.dlgid = 0;
}

static void Pazaak_Dlg_Ok(const int response)
{
	(void)response;
	Pazaak_Dialog_Close();
	PzkState.dlgid = 0;
	Pazaak_SendAction("acceptdlg");
}

static void Pazaak_ConfirmSideDeckDialog(const int response)
{
	menuDef_t* menu = NULL;

	Pazaak_Dialog_Close();
	PzkState.dlgid = 0;
	if (response != PDLGRESP_YES)
	{
		return;
	}
	menu = Pazaak_Menu(2);
	if (!menu)
	{
		return;
	}
	Pazaak_Show(menu, "buttons", qtrue);
	Pazaak_Show(menu, "buttons_hover", qfalse);
	Pazaak_Show(menu, "card_buttons", qfalse);
	Pazaak_Show(menu, "sd_buttons", qfalse);
	Pazaak_Show(menu, "buttons_hitzone", qfalse);

	Pazaak_SendAction(va("setsd %i %i %i %i %i %i %i %i %i %i",
		PzkState.sidedeck[0], PzkState.sidedeck[1], PzkState.sidedeck[2], PzkState.sidedeck[3], PzkState.sidedeck[4],
		PzkState.sidedeck[5], PzkState.sidedeck[6], PzkState.sidedeck[7], PzkState.sidedeck[8], PzkState.sidedeck[9]));
	PzkState.waitingStartTime = PZK_Millis();
}

static void Pazaak_ConfirmSideDeck(void)
{
	int i = 0;
	for (i = 0; i < 10; i++)
	{
		if (PzkState.sidedeck[i] < PZCARD_PLUS_1)
		{
			break;
		}
	}
	PzkState.dlgid = PDLGID_ANY;
	if (i < 10)
	{
		Pazaak_Dialog_Show("Your side-deck is incomplete.", "You need to pick 10 cards to play.", PDLG_OK, Pazaak_Dlg_Null);
		return;
	}
	Pazaak_Dialog_Show("Do you wish to play with this side deck?", NULL, PDLG_YESNO, Pazaak_ConfirmSideDeckDialog);
}

static void Pazaak_ShowQuitDialog(void)
{
	PzkState.dlgid = PDLGID_ANY;
	if (PzkState.forfeitOnQuit)
	{
		Pazaak_Dialog_Show("If you quit now you will lose your wager", "Are you sure you wish to quit?", PDLG_YESNO, Pazaak_Dlg_Exit);
	}
	else
	{
		Pazaak_Dialog_Show("Are you sure you wish to quit?", NULL, PDLG_YESNO, Pazaak_Dlg_Exit);
	}
}

static void Pazaak_ShowForfeitDialog(void)
{
	PzkState.dlgid = PDLGID_ANY;
	Pazaak_Dialog_Show("If you forfeit you will lose your wager.", "Are you sure you wish to forfeit?", PDLG_YESNO, Pazaak_Dlg_Forfeit);
}

void UI_Pazaak_OnEsc(void)
{
	if (HowToOpen)
	{
		Pazaak_HowTo(0); // back to the card selection
	}
	else if (PDlgData.InUse)
	{
		// Ok-only dialog: as if Ok was clicked; yes/no: as if No was clicked
		PDlgData.callback(PDlgData.type == PDLG_OK ? PDLGRESP_OK : PDLGRESP_NO);
	}
	else if (activeBoard == 1)
	{
		Pazaak_ShowForfeitDialog();
	}
	else if (activeBoard == 2 && !PzkState.waitingStartTime)
	{
		Pazaak_ShowQuitDialog();
	}
}

qboolean UI_Pazaak_Active(void)
{
	return PzkState.active && activeBoard ? qtrue : qfalse;
}

qboolean UI_Pazaak_Script(const char* name, void* argsPtr)
{
	pzkArgs_t args = (pzkArgs_t)argsPtr;
	int a = 0, b = 0;
	menuDef_t* menu = NULL;

	if (!Q_stricmpn(name, "shop_", 5))
	{
		return UI_Shop_Script(name, argsPtr); // the shop and the talk question
	}
	if (Q_stricmpn(name, "pzk_", 4))
	{
		return qfalse;
	}

	if (!Q_stricmp(name, "pzk_handhover"))
	{
		if (Int_Parse(args, &a) && Int_Parse(args, &b) && a >= 1 && a <= 4)
		{
			HandSlotHover[a - 1] = b;
		}
	}
	else if (!Q_stricmp(name, "pzk_usecard"))
	{
		if (!Int_Parse(args, &a) || a < 1 || a > 4 || PzkState.hand[a - 1].cardid == PZCARD_NONE)
		{
			return qtrue;
		}
		Pazaak_SendAction(va("usecard %i %i", a, PzkState.hand[a - 1].param));
		PzkState.hand[a - 1].cardid = PZCARD_NONE;
		menu = Pazaak_Menu(1);
		if (menu)
		{
			Pazaak_Show(menu, va("button_hand%i", a), qfalse);
		}
	}
	else if (!Q_stricmp(name, "pzk_btnpress"))
	{
		if (!Int_Parse(args, &a))
		{
			return qtrue;
		}
		switch (a)
		{
		case PBUTTON_ENDTURN: Pazaak_SendAction("endturn"); break;
		case PBUTTON_STAND: Pazaak_SendAction("stand"); break;
		case PBUTTON_FORFEIT: Pazaak_ShowForfeitDialog(); break;
		case PBUTTON_EXIT: Pazaak_ShowQuitDialog(); break;
		case PBUTTON_CONTINUE: Pazaak_ConfirmSideDeck(); break;
		default: break;
		}
	}
	else if (!Q_stricmp(name, "pzk_dlgbutton"))
	{
		if (Int_Parse(args, &a) && a >= PDLGRESP_OK && a <= PDLGRESP_NO && PDlgData.InUse)
		{
			PDlgData.callback(a);
		}
	}
	else if (!Q_stricmp(name, "pzk_flip"))
	{
		if (Int_Parse(args, &a) && Int_Parse(args, &b) && b >= 1 && b <= 4)
		{
			if (a == 1)
			{
				Pazaak_FlipHandCard(b);
			}
			else if (a == 2)
			{
				Pazaak_FlipHandCardValue(b);
			}
		}
	}
	else if (!Q_stricmp(name, "pzk_onesc"))
	{
		UI_Pazaak_OnEsc();
	}
	else if (!Q_stricmp(name, "pzk_howto"))
	{
		if (Int_Parse(args, &a))
		{
			Pazaak_HowTo(a);
		}
	}
	else if (!Q_stricmp(name, "pzk_cardhover"))
	{
		if (Int_Parse(args, &a) && Int_Parse(args, &b) && a >= 0 && a <= 22)
		{
			CardSelSlotHover[a] = b;
		}
	}
	else if (!Q_stricmp(name, "pzk_selectcard"))
	{
		int i = 0;
		if (!Int_Parse(args, &a) || a < 0 || a > 22)
		{
			return qtrue;
		}
		for (i = 0; i < 10; i++)
		{
			if (PzkState.sidedeck[i] < PZCARD_PLUS_1)
			{
				break;
			}
		}
		if (i == 10)
		{
			PzkState.dlgid = 0;
			Pazaak_Dialog_Show("Your side deck is full!", "Please remove a card first.", PDLG_OK, Pazaak_Dlg_Null);
			return qtrue;
		}
		if (!PzkState.playercards[a].amount || PzkState.playercards[a].amount == PzkState.playercards[a].inuse)
		{
			return qtrue;
		}
		PzkState.sidedeck[i] = a + PZCARD_PLUS_1;
		PzkState.playercards[a].inuse++;
		menu = Pazaak_Menu(2);
		if (menu)
		{
			Pazaak_Show(menu, va("sd_B%i", i), qtrue);
		}
		CardSDHover[i] = 0;
		Pazaak_PlaySound(PzkSounds.turnSwitch); // the card goes into the side deck
	}
	else if (!Q_stricmp(name, "pzk_sdhover"))
	{
		if (Int_Parse(args, &a) && Int_Parse(args, &b) && a >= 0 && a <= 9)
		{
			CardSDHover[a] = b;
		}
	}
	else if (!Q_stricmp(name, "pzk_removesd"))
	{
		int card = 0;
		if (!Int_Parse(args, &a) || a < 0 || a > 9)
		{
			return qtrue;
		}
		card = PzkState.sidedeck[a];
		PzkState.sidedeck[a] = PZCARD_NONE;
		if (card >= PZCARD_PLUS_1 && card <= PZCARD_3N6)
		{
			PzkState.playercards[card - PZCARD_PLUS_1].inuse--;
		}
		menu = Pazaak_Menu(2);
		if (menu)
		{
			Pazaak_Show(menu, va("sd_B%i", a), qfalse);
		}
	}
	else if (!Q_stricmp(name, "pzk_clearfocus"))
	{
		Pazaak_ClearFocus(Pazaak_Menu(1));
		Pazaak_ClearFocus(Pazaak_Menu(2));
	}
	return qtrue;
}

/*
===========================================================================
Instructions from the game ("pzk ...")
===========================================================================
*/

static void Pazaak_Reset(void)
{
	int i = 0;

	memset(&PzkState, 0, sizeof(PzkState));
	memset(&PDlgData, 0, sizeof(PDlgData));
	HowToOpen = 0;
	for (i = 0; i < 18; i++)
	{
		PzkState.field[i].cardid = PZCARD_NONE;
	}
	for (i = 0; i < 8; i++)
	{
		PzkState.hand[i].cardid = PZCARD_NONE;
	}
	for (i = 0; i < 4; i++)
	{
		HandSlotHover[i] = 0;
	}
	for (i = 0; i < 10; i++)
	{
		PzkState.sidedeck[i] = PZCARD_NONE;
		CardSDHover[i] = 0;
	}
	for (i = 0; i < 23; i++)
	{
		CardSelSlotHover[i] = 0;
	}
	pzkFont = 0;
	Pazaak_CacheShaders();
	Pazaak_CacheSounds();
}

static void Pazaak_OpenBoard(const int board)
{
	menuDef_t* menu = NULL;

	Pazaak_HowTo(0); // the card selection opens without the "how to play" page
	Menus_CloseAll();
	menu = Menus_ActivateByName(board == 1 ? PZK_MENU_BOARD : PZK_MENU_CARDS);
	if (menu)
	{
		PZK_SetCatcher(PZK_GetCatcher() | KEYCATCH_UI);
		PZK_CvarSet("s_pazaakMute", "1"); // the engine plays only interface sounds now (snd_dma.cpp)
#ifdef PZK_SP
		// The world waits while we play (like every other in-game menu)
		if (!PzkState.pausedGame)
		{
			PzkState.pausedGame = 1;
			PZK_CvarSet("cl_paused", "1");
		}
#endif
	}
	Pazaak_ClearFocus(menu);
	activeBoard = board;
}

// Reads "argv[*i]" as an int and advances; 0 when missing
static int Pazaak_Int(const int argc, const char** argv, int* i, int* out)
{
	if (*i >= argc)
	{
		return 0;
	}
	*out = atoi(argv[(*i)++]);
	return 1;
}

void UI_Pazaak_Process(const int argc, const char** argv)
{
	int i = 0, j = 0, temp = 0;
	menuDef_t* menu = NULL;

	while (i < argc)
	{
		const char* token = argv[i++];
		if (!token[0])
		{
			break;
		}

		if (!Q_stricmp(token, "start"))
		{
			Pazaak_Reset();
			if (!Menus_FindByName(PZK_MENU_BOARD))
			{
				UI_ParseMenu(PZK_MENU_FILE); // not in the menu lists of the mods, load it on demand
			}
			PzkState.active = 1;
			activeBoard = 0;
			continue;
		}
		if (!PzkState.active)
		{
			return;
		}
		if (!Q_stricmp(token, "stop"))
		{
#ifdef PZK_SP
			const int paused = PzkState.pausedGame;
#endif
			const int wasOpen = activeBoard;
			Pazaak_Close();
			Pazaak_Reset();
			if (wasOpen)
			{
				PZK_SetCatcher(PZK_GetCatcher() & ~KEYCATCH_UI); // a match called off before the board opened leaves other menus alone
			}
			PZK_CvarSet("s_pazaakMute", "0");
#ifdef PZK_SP
			if (paused)
			{
				PZK_CvarSet("cl_paused", "0");
			}
#endif
			continue;
		}
		if (!Q_stricmp(token, "gtg"))
		{
			Pazaak_OpenBoard(1);
			continue;
		}
		if (!Q_stricmp(token, "gtc"))
		{
			Pazaak_OpenBoard(2);
			continue;
		}
		if (!Q_stricmp(token, "sn"))
		{
			Q_strncpyz(PzkState.name_player, i < argc ? argv[i] : "", sizeof(PzkState.name_player));
			i++;
			Q_strncpyz(PzkState.name_opponent, i < argc ? argv[i] : "", sizeof(PzkState.name_opponent));
			i++;
			continue;
		}
		if (!Q_stricmp(token, "st"))
		{
			if (!Pazaak_Int(argc, argv, &i, &PzkState.turn))
			{
				return;
			}
			menu = Pazaak_Menu(1);
			if (menu)
			{
				Pazaak_Show(menu, "turns", qfalse);
				if (PzkState.turn == 1)
				{
					Pazaak_Show(menu, "turn_p", qtrue);
				}
				else if (PzkState.turn == 2)
				{
					Pazaak_Show(menu, "turn_o", qtrue);
				}
			}
			Pazaak_PlaySound(PzkSounds.turnSwitch);
			continue;
		}
		if (!Q_stricmp(token, "sto"))
		{
			if (!Pazaak_Int(argc, argv, &i, &temp))
			{
				return;
			}
			PzkState.timeout = temp ? PZK_Millis() + temp * 1000 : 0;
			continue;
		}
		if (!Q_stricmp(token, "spt"))
		{
			if (!Pazaak_Int(argc, argv, &i, &temp))
			{
				return;
			}
			if (temp == 1)
			{
				if (!Pazaak_Int(argc, argv, &i, &PzkState.total_player))
				{
					return;
				}
				if (PzkState.total_player > 20)
				{
					Pazaak_PlaySound(PzkSounds.bust);
				}
			}
			else
			{
				if (!Pazaak_Int(argc, argv, &i, &PzkState.total_opponent))
				{
					return;
				}
				if (PzkState.total_opponent > 20)
				{
					Pazaak_PlaySound(PzkSounds.bust);
				}
			}
			continue;
		}
		if (!Q_stricmp(token, "cf"))
		{
			// Clear the field for a new set: cards, points, turn, stand states
			for (j = 0; j < 18; j++)
			{
				PzkState.field[j].cardid = PZCARD_NONE;
				PzkState.field[j].param = 0;
			}
			PzkState.turn = 0;
			PzkState.total_player = 0;
			PzkState.total_opponent = 0;
			PzkState.standing_player = 0;
			PzkState.standing_opponent = 0;
			menu = Pazaak_Menu(1);
			if (menu)
			{
				Pazaak_Show(menu, "turns", qfalse);
			}
			continue;
		}
		if (!Q_stricmp(token, "ch"))
		{
			for (j = 0; j < 8; j++)
			{
				PzkState.hand[j].cardid = PZCARD_NONE;
				PzkState.hand[j].param = 0;
			}
			menu = Pazaak_Menu(1);
			if (menu)
			{
				Pazaak_Show(menu, "buttons_hand", qfalse);
				Pazaak_Show(menu, "flip", qfalse);
			}
			continue;
		}
		if (!Q_stricmp(token, "sc"))
		{
			int isNew = 0;
			if (!Pazaak_Int(argc, argv, &i, &temp) || temp < 1 || temp > 18)
			{
				return;
			}
			temp--;
			isNew = PzkState.field[temp].cardid < 0;
			if (!Pazaak_Int(argc, argv, &i, &PzkState.field[temp].cardid) || !Pazaak_Int(argc, argv, &i, &PzkState.field[temp].param))
			{
				return;
			}
			if (isNew)
			{
				if (PzkState.field[temp].cardid >= PZCARD_NORMAL_1 && PzkState.field[temp].cardid <= PZCARD_NORMAL_10)
				{
					Pazaak_PlaySound(PzkSounds.drawCard);
				}
				else if (PzkState.field[temp].cardid >= PZCARD_PLUS_1 && PzkState.field[temp].cardid <= PZCARD_3N6)
				{
					Pazaak_PlaySound(PzkSounds.playCard);
				}
			}
			continue;
		}
		if (!Q_stricmp(token, "ss"))
		{
			if (!Pazaak_Int(argc, argv, &i, &temp))
			{
				return;
			}
			if (!Pazaak_Int(argc, argv, &i, temp == 1 ? &PzkState.standing_player : &PzkState.standing_opponent))
			{
				return;
			}
			continue;
		}
		if (!Q_stricmp(token, "ssc"))
		{
			if (!Pazaak_Int(argc, argv, &i, &temp))
			{
				return;
			}
			menu = Pazaak_Menu(1);
			if (temp == 1)
			{
				if (!Pazaak_Int(argc, argv, &i, &PzkState.score_player))
				{
					return;
				}
				if (menu)
				{
					Pazaak_Show(menu, "score_p1", PzkState.score_player > 0 ? qtrue : qfalse);
					Pazaak_Show(menu, "score_p2", PzkState.score_player > 1 ? qtrue : qfalse);
					Pazaak_Show(menu, "score_p3", PzkState.score_player > 2 ? qtrue : qfalse);
				}
			}
			else
			{
				if (!Pazaak_Int(argc, argv, &i, &PzkState.score_opponent))
				{
					return;
				}
				if (menu)
				{
					Pazaak_Show(menu, "score_o1", PzkState.score_opponent > 0 ? qtrue : qfalse);
					Pazaak_Show(menu, "score_o2", PzkState.score_opponent > 1 ? qtrue : qfalse);
					Pazaak_Show(menu, "score_o3", PzkState.score_opponent > 2 ? qtrue : qfalse);
				}
			}
			continue;
		}
		if (!Q_stricmp(token, "shcs"))
		{
			// 4 hand cards (ID, param) and a bit mask of the opponent's cards
			int flips = 0;
			for (j = 0; j < 4; j++)
			{
				if (!Pazaak_Int(argc, argv, &i, &PzkState.hand[j].cardid) || !Pazaak_Int(argc, argv, &i, &PzkState.hand[j].param))
				{
					return;
				}
			}
			if (!Pazaak_Int(argc, argv, &i, &temp))
			{
				return;
			}
			for (j = 0; j < 4; j++)
			{
				PzkState.hand[j + 4].cardid = temp & 1 << j ? PZCARD_BACK : PZCARD_NONE;
			}
			menu = Pazaak_Menu(1);
			if (!menu)
			{
				continue;
			}
			Pazaak_Show(menu, "flip", qfalse);
			for (j = 0; j < 4; j++)
			{
				if (Pazaak_CanCardFlip(PzkState.hand[j].cardid))
				{
					flips |= 1;
					Pazaak_Show(menu, va("flip_s%i", j + 1), qtrue);
				}
				if (Pazaak_CanCardFlipValue(PzkState.hand[j].cardid))
				{
					flips |= 2;
					Pazaak_Show(menu, va("flip_v%i", j + 1), qtrue);
				}
			}
			if (flips & 1)
			{
				Pazaak_Show(menu, "flip_sd", qtrue);
			}
			if (flips & 2)
			{
				Pazaak_Show(menu, "flip_vd", qtrue);
			}
			continue;
		}
		if (!Q_stricmp(token, "shc"))
		{
			if (!Pazaak_Int(argc, argv, &i, &temp) || temp < 1 || temp > 8)
			{
				return;
			}
			temp--;
			if (!Pazaak_Int(argc, argv, &i, &PzkState.hand[temp].cardid) || !Pazaak_Int(argc, argv, &i, &PzkState.hand[temp].param))
			{
				return;
			}
			menu = Pazaak_Menu(1);
			if (!menu || temp > 3)
			{
				continue;
			}
			if (Pazaak_CanCardFlip(PzkState.hand[temp].cardid))
			{
				Pazaak_Show(menu, va("flip_s%i", temp + 1), qtrue);
				Pazaak_Show(menu, "flip_sd", qtrue);
			}
			else
			{
				Pazaak_Show(menu, va("flip_s%i", temp + 1), qfalse);
			}
			if (Pazaak_CanCardFlipValue(PzkState.hand[temp].cardid))
			{
				Pazaak_Show(menu, va("flip_v%i", temp + 1), qtrue);
				Pazaak_Show(menu, "flip_vd", qtrue);
			}
			else
			{
				Pazaak_Show(menu, va("flip_v%i", temp + 1), qfalse);
			}
			continue;
		}
		if (!Q_stricmp(token, "db"))
		{
			if (!Pazaak_Int(argc, argv, &i, &PzkState.buttonstate))
			{
				return;
			}
			menu = Pazaak_Menu(1);
			if (!menu)
			{
				continue;
			}
			if (!PDlgData.InUse)
			{
				Pazaak_ApplyButtonState(menu);
			}
			// Reset the focus so buttons that just came back react to the mouse
			Pazaak_ClearFocus(menu);
			if (DC)
			{
				Display_MouseMove(NULL, (int)DC->cursorx, (int)DC->cursory);
			}
			continue;
		}
		if (!Q_stricmp(token, "sd"))
		{
			if (!Pazaak_Int(argc, argv, &i, &PzkState.dlgid))
			{
				return;
			}
			switch (PzkState.dlgid)
			{
			case PDLGID_WONSET:
				Pazaak_PlaySound(PzkSounds.winSet);
				Pazaak_Dialog_Show("You won the set.", NULL, PDLG_OK, Pazaak_Dlg_Ok);
				break;
			case PDLGID_LOSTSET:
				Pazaak_PlaySound(PzkSounds.loseSet);
				Pazaak_Dialog_Show("The opponent wins the set.", NULL, PDLG_OK, Pazaak_Dlg_Ok);
				break;
			case PDLGID_TIESET:
				Pazaak_PlaySound(PzkSounds.tieSet);
				Pazaak_Dialog_Show("The set is tied.", NULL, PDLG_OK, Pazaak_Dlg_Ok);
				break;
			case PDLGID_WONMATCH:
				Pazaak_PlaySound(PzkSounds.winMatch);
				Pazaak_Dialog_Show("You have defeated your opponent.", NULL, PDLG_OK, Pazaak_Dlg_Ok);
				break;
			case PDLGID_LOSTMATCH:
				Pazaak_PlaySound(PzkSounds.loseMatch);
				Pazaak_Dialog_Show("You have been defeated.", NULL, PDLG_OK, Pazaak_Dlg_Ok);
				break;
			case PDLGID_WINFORFEIT:
				Pazaak_PlaySound(PzkSounds.winMatch);
				Pazaak_Dialog_Show("Your opponent has forfeited the match.", NULL, PDLG_OK, Pazaak_Dlg_Ok);
				break;
			case PDLGID_LOSEFORFEIT:
				Pazaak_PlaySound(PzkSounds.loseMatch);
				Pazaak_Dialog_Show("You have forfeited the match.", NULL, PDLG_OK, Pazaak_Dlg_Ok);
				break;
			case PDLGID_LOSEQUIT:
				Pazaak_PlaySound(PzkSounds.loseMatch);
				Pazaak_Dialog_Show("You have quit the match.", NULL, PDLG_OK, Pazaak_Dlg_Ok);
				break;
			default:
				break;
			}
			continue;
		}
		if (!Q_stricmp(token, "cd"))
		{
			if (PzkState.dlgid != 0) // only one of ours, not the forfeit question
			{
				Pazaak_Dialog_Close();
			}
			continue;
		}
		if (!Q_stricmp(token, "sdc"))
		{
			Pazaak_HowTo(0); // the card buttons come back
			menu = Pazaak_Menu(2);
			if (menu)
			{
				Pazaak_Show(menu, "card_buttons", qfalse);
			}
			for (j = 0; j < 23; j++)
			{
				if (!Pazaak_Int(argc, argv, &i, &PzkState.playercards[j].amount))
				{
					return;
				}
				if (menu && PzkState.playercards[j].amount > 0)
				{
					Pazaak_Show(menu, va("card_B%i", j), qtrue);
				}
				PzkState.playercards[j].inuse = 0;
			}
			for (j = 0; j < 10; j++)
			{
				if (PzkState.sidedeck[j] >= PZCARD_PLUS_1 && PzkState.sidedeck[j] <= PZCARD_3N6)
				{
					PzkState.playercards[PzkState.sidedeck[j] - PZCARD_PLUS_1].inuse++;
				}
			}
			continue;
		}
		if (!Q_stricmp(token, "ssd"))
		{
			Pazaak_HowTo(0); // the side deck buttons come back
			menu = Pazaak_Menu(2);
			if (menu)
			{
				Pazaak_Show(menu, "sd_buttons", qfalse);
			}
			for (j = 0; j < 10; j++)
			{
				if (!Pazaak_Int(argc, argv, &i, &PzkState.sidedeck[j]))
				{
					return;
				}
				if (menu && PzkState.sidedeck[j] >= PZCARD_PLUS_1)
				{
					Pazaak_Show(menu, va("sd_B%i", j), qtrue);
				}
			}
			for (j = 0; j < 23; j++)
			{
				PzkState.playercards[j].inuse = 0;
			}
			for (j = 0; j < 10; j++)
			{
				if (PzkState.sidedeck[j] >= PZCARD_PLUS_1 && PzkState.sidedeck[j] <= PZCARD_3N6)
				{
					PzkState.playercards[PzkState.sidedeck[j] - PZCARD_PLUS_1].inuse++;
				}
			}
			continue;
		}
		if (!Q_stricmp(token, "foq"))
		{
			if (!Pazaak_Int(argc, argv, &i, &PzkState.forfeitOnQuit))
			{
				return;
			}
			continue;
		}
		if (!Q_stricmp(token, "tsdn"))
		{
			// Time ran out on the card selection: send what we have
			Pazaak_HowTo(0);
			menu = Pazaak_Menu(2);
			if (menu)
			{
				Pazaak_Show(menu, "buttons", qtrue);
				Pazaak_Show(menu, "buttons_hover", qfalse);
				Pazaak_Show(menu, "card_buttons", qfalse);
				Pazaak_Show(menu, "sd_buttons", qfalse);
				Pazaak_Show(menu, "buttons_hitzone", qfalse);
			}
			Pazaak_SendAction(va("setsdt %i %i %i %i %i %i %i %i %i %i",
				PzkState.sidedeck[0], PzkState.sidedeck[1], PzkState.sidedeck[2], PzkState.sidedeck[3], PzkState.sidedeck[4],
				PzkState.sidedeck[5], PzkState.sidedeck[6], PzkState.sidedeck[7], PzkState.sidedeck[8], PzkState.sidedeck[9]));
			PzkState.waitingStartTime = PZK_Millis();
			continue;
		}
		// Unknown instruction: ignore the rest
		return;
	}
}

/*
===========================================================================
Transport
===========================================================================
*/

// Splits a line into words ("quoted words" stay together)
static int Pazaak_Tokenize(char* line, const char** argv, const int maxArgs)
{
	int argc = 0;
	char* p = line;
	while (*p && argc < maxArgs)
	{
		while (*p == ' ' || *p == '\t')
		{
			p++;
		}
		if (!*p)
		{
			break;
		}
		if (*p == '"')
		{
			p++;
			argv[argc++] = p;
			while (*p && *p != '"')
			{
				p++;
			}
		}
		else
		{
			argv[argc++] = p;
			while (*p && *p != ' ' && *p != '\t')
			{
				p++;
			}
		}
		if (*p)
		{
			*p++ = 0;
		}
	}
	return argc;
}

#ifndef PZK_SP

// Multiplayer: answers go to the game module as the "~pzk" client command
static void Pazaak_SendAction(const char* text)
{
	trap->Cmd_ExecuteText(EXEC_APPEND, va("~pzk %s\n", text));
}

// The "uipzk" console command (the cgame passes the "pzk" server commands on with it)
qboolean UI_Pazaak_ConsoleCommand(const char* cmd)
{
	static char args[64][128];
	const char* argv[64] = { 0 };
	int argc = 0, i = 0;

	if (Q_stricmp(cmd, "uipzk"))
	{
		return qfalse;
	}
	argc = trap->Cmd_Argc() - 1;
	if (argc > 64)
	{
		argc = 64;
	}
	for (i = 0; i < argc; i++)
	{
		trap->Cmd_Argv(i + 1, args[i], sizeof(args[i]));
		argv[i] = args[i];
	}
	if (argc > 0 && !Q_stricmp(argv[0], "shop"))
	{
		UI_Shop_Process(argc - 1, argv + 1); // "pzk shop <open|state|close> ..." (g_pazaak.c shop code)
		return qtrue;
	}
	if (argc > 0 && !Q_stricmp(argv[0], "talk"))
	{
		UI_Shop_Process(argc, argv); // "pzk talk <question> ..."
		return qtrue;
	}
	UI_Pazaak_Process(argc, argv);
	return qtrue;
}

#else

/*
===========================================================================
Singleplayer: the match runs here, against the AI
===========================================================================
*/

static pzkGame_t pzkLocal;
static int pzkLocalOpponent = -1; // entity number of the NPC we play against (-1 = none)
static int pzkLocalWager = 0;
static char pzkActions[16][256];
static int pzkNumActions = 0;

static void Pazaak_SendAction(const char* text)
{
	// Run on the next frame, not inside the menu script that sent it
	if (pzkNumActions < 16)
	{
		Q_strncpyz(pzkActions[pzkNumActions++], text, sizeof(pzkActions[0]));
	}
}

static void Pazaak_LocalSend(pzkGame_t* game, const int pid, const char* text)
{
	char line[1024];
	const char* argv[96];
	int argc;
	(void)game;

	if (pid != 1)
	{
		return;
	}
	Q_strncpyz(line, text, sizeof(line));
	argc = Pazaak_Tokenize(line, argv, 96);
	UI_Pazaak_Process(argc, argv);
}

static void Pazaak_LocalFinish(pzkGame_t* game, const int winnerPid)
{
	(void)game;
	// Tell the game module (credits for a wager, the NPC's reaction...)
	ui.Cmd_ExecuteText(EXEC_APPEND, va("pazaak_result %i %i %i\n", winnerPid, pzkLocalOpponent, pzkLocalWager));
	pzkLocalOpponent = -1;
	pzkLocalWager = 0;
}

// "uipzk_start <opponent name> <opponent entity number> <wager>" from the game module
static void Pazaak_StartLocal(const char* opponentName, const int opponentEnt, const int wager)
{
	int cards[PZK_NUM_SIDECARDS];
	int aiDeck[PZK_SIDEDECK_SIZE];
	const char* err;
	char playerName[64];

	// A video started while the player sat down: no match now, he stands up again
	if (CL_IsRunningInGameCinematic() || CL_InGameCinematicOnStandBy())
	{
		ui.Cmd_ExecuteText(EXEC_APPEND, "pazaak_result 0 unavailable\n");
		return;
	}
	if (pzkLocal.inUse)
	{
		Pzk_Abort(&pzkLocal);
	}
	pzkNumActions = 0;

	Pzk_Init(&pzkLocal, Pazaak_LocalSend, Pazaak_LocalFinish, (unsigned int)PZK_Millis() * 2654435761u + 1u);
	ui.Cvar_VariableStringBuffer("name", playerName, sizeof(playerName));
	if (!playerName[0])
	{
		Q_strncpyz(playerName, "Player", sizeof(playerName));
	}
	Pzk_SetPlayer(&pzkLocal, 1, playerName, 0);
	Pzk_SetPlayer(&pzkLocal, 2, opponentName && opponentName[0] ? opponentName : "AI", 1);

	Pzk_DefaultCards(cards);
	Pzk_SetCards(&pzkLocal, 1, cards);
	Pzk_SetCards(&pzkLocal, 2, cards);
	Pzk_DefaultAISideDeck(aiDeck);
	Pzk_SetSideDeck(&pzkLocal, 2, aiDeck);
	Pzk_ShowCardSelection(&pzkLocal, 1);

	pzkLocalOpponent = opponentEnt;
	pzkLocalWager = wager;

	err = Pzk_StartGame(&pzkLocal, PZK_Millis());
	if (err)
	{
		Com_Printf("Could not start pazaak: %s\n", err);
		pzkLocal.inUse = 0;
		ui.Cmd_ExecuteText(EXEC_APPEND, "pazaak_result 0\n"); // the game module lets the player stand up again
	}
	else if (wager > 0)
	{
		PzkState.forfeitOnQuit = 1;
	}
}

qboolean UI_Pazaak_ConsoleCommand(const char* cmd)
{
	if (!Q_stricmp(cmd, "uipzk_start"))
	{
		Pazaak_StartLocal(Cmd_Argv(1), Cmd_Argc() > 2 ? atoi(Cmd_Argv(2)) : -1, Cmd_Argc() > 3 ? atoi(Cmd_Argv(3)) : 0);
		return qtrue;
	}
	if (!Q_stricmp(cmd, "uipzk_action"))
	{
		// A board answer typed or scripted ("uipzk_action stand"), like "~pzk" in multiplayer
		char text[256];
		int i;
		text[0] = 0;
		for (i = 1; i < Cmd_Argc(); i++)
		{
			Q_strcat(text, sizeof(text), va(i > 1 ? " %s" : "%s", Cmd_Argv(i)));
		}
		if (pzkLocal.inUse && text[0])
		{
			Pazaak_SendAction(text);
		}
		return qtrue;
	}
	if (!Q_stricmp(cmd, "uipzk_stop"))
	{
		if (pzkLocal.inUse)
		{
			Pzk_Abort(&pzkLocal);
		}
		return qtrue;
	}
	return qfalse;
}

// Every UI frame: the queued board actions and the match timers
void UI_Pazaak_Frame(void)
{
	int i;
	const int now = PZK_Millis();

	if (!pzkLocal.inUse)
	{
		pzkNumActions = 0;
		return;
	}
	for (i = 0; i < pzkNumActions && pzkLocal.inUse; i++)
	{
		char line[256];
		const char* argv[16];
		int argc;
		Q_strncpyz(line, pzkActions[i], sizeof(line));
		argc = Pazaak_Tokenize(line, argv, 16);
		Pzk_Command(&pzkLocal, 1, argc, argv, now);
	}
	pzkNumActions = 0;
	Pzk_Frame(&pzkLocal, now);
}

#endif

/*
===========================================================================
The shop and the talk question (2026-10-31, DH: buy the SJE guns like pazaak is played; SJE SP + MP)

The game decides (g_pazaak shop code), this shows it:
- the shop menu "sje_shop" (ui/pazaak/sje_shop.menu, MP sje_shop_mp.menu): tabs Pistols / Rifles / Heavy /
  Grenades / Supplies, 9 rows a page (icon, name, price, owned / in use), the credits and the last message.
  The list comes from ext_data/sje_weaponvariants.dat, read here in the same order as the game reads it (the row's
  variant number is what "shopbuy" sends). Supplies: an ammo refill, a bacta canister / medpac.
- the talk question "sje_talk" (ui/pazaak/sje_talk.menu, MP sje_talk_mp.menu), the pazaak challenge look:
  "Challenge to pazaak" / "Buy items" / "Leave" (keys 1 2 3, escape = leave in SP), answered with "shoptalk N".
The game's words: SP the console command "uishop <what> ...", MP "uipzk shop <what> ..." / "uipzk talk ..." (the
"pzk" server command, passed on by the cgame):
- open / state <credits> <owned bits> <equipped bits> <ammo price> <bacta price> "<message>"
- talk "<question>" <pazaak allowed>
- close
SP pauses the world for the talk question only: the shop needs the game to answer the buys.
===========================================================================
*/

#define SHOP_FILE			"ext_data/sje_weaponvariants.dat"
#ifdef PZK_SP
#define SHOP_MENU_FILE		"ui/pazaak/sje_shop.menu"
#define SHOP_TALK_FILE		"ui/pazaak/sje_talk.menu"
#else
#define SHOP_MENU_FILE		"ui/pazaak/sje_shop_mp.menu"
#define SHOP_TALK_FILE		"ui/pazaak/sje_talk_mp.menu"
#endif
#define SHOP_MENU			"sje_shop"
#define SHOP_TALK_MENU		"sje_talk"
#define SHOP_OD_BASE		3000	// ownerdraws: + type * 100 + slot
#define SHOP_OD_HEADER		0		// 0 = title, 1 = credits, 2 = message, 3 = page
#define SHOP_OD_TAB			1		// 0..SHOP_NUM_CATS - 1
#define SHOP_OD_ROW			2		// 0..SHOP_ROWS - 1
#define SHOP_ROWS			9
#define SHOP_NUM_CATS		6
#define SHOP_CAT_ITEMS		5		// the holdable items (2026-11-02)
#define SHOP_CAT_SUPPLIES	4
#define SHOP_MAX_ITEMS		256
#define SHOP_ITEM_AMMO_ID	1000	// = SHOP_ITEM_AMMO / SHOP_ITEM_BACTA of the game
#define SHOP_ITEM_BACTA_ID	1001
#define SHOP_ITEM_HOLD_ID	1100	// + index: the holdable items (= SHOP_ITEM_HOLD of the game, in its table's order)
#define SHOP_MAX_HOLD		16

typedef struct
{
	int id; // variant number (or SHOP_ITEM_*_ID)
	int cat;
	int price; // 0: the game's default (the menu shows the default too)
	char name[64];
	char icon[MAX_QPATH];
	qhandle_t iconShader;
	int iconTried;
} shopItem_t;

static shopItem_t shopItems[SHOP_MAX_ITEMS];
static int shopNumItems = -1; // -1 = the file was not read yet
static int shopOpen = 0, shopTalkOpen = 0;
static int shopCat = 0, shopPage = 0, shopHover = -1, shopTabHover = -1;
// The holdable items of the Items tab (2026-11-02), in the order of the game's shopHoldables table (g_pazaak)
#ifdef PZK_SP
static const char* shopHoldNames[][2] = {
	{ "Electrobinoculars", "gfx/hud/i_icon_zoom" },
	{ "Light amp goggles", "gfx/hud/i_icon_goggles" },
	{ "Seeker drone", "gfx/hud/i_icon_seeker" },
	{ "Sentry gun", "gfx/hud/i_icon_sentrygun" }
};
#else
static const char* shopHoldNames[][2] = {
	{ "Seeker drone", "gfx/hud/i_icon_seeker" },
	{ "Portable shield", "gfx/hud/i_icon_shieldwall" },
	{ "Big bacta", "gfx/hud/i_icon_big_bacta" },
	{ "Binoculars", "gfx/hud/i_icon_zoom" },
	{ "Sentry gun", "gfx/hud/i_icon_sentrygun" },
	{ "Jetpack", "gfx/hud/i_icon_jetpack" },
	{ "Health dispenser", "gfx/hud/i_icon_health_disp" },
	{ "Ammo dispenser", "gfx/hud/i_icon_ammo_disp" },
	{ "E-Web", "gfx/hud/i_icon_eweb" },
	{ "Cloak", "gfx/hud/i_icon_cloak" }
};
#endif
#define SHOP_NUM_HOLD		((int)(sizeof(shopHoldNames) / sizeof(shopHoldNames[0])))
static int shopHoldPrice[SHOP_MAX_HOLD], shopHoldCount[SHOP_MAX_HOLD], shopHoldMax[SHOP_MAX_HOLD];
static int shopCredits = 0, shopAmmoPrice = 0, shopBactaPrice = 0;
static unsigned int shopOwnedBits[8], shopEquipBits[8];
static char shopMessage[128];
static qhandle_t shopWhite = 0;
#ifndef PZK_SP
static char shopFileBuf[262144];
#endif

static void Shop_SendCmd(const char* text);

static const char* shopCatNames[SHOP_NUM_CATS] = { "Pistols", "Rifles", "Heavy", "Grenades", "Supplies", "Items" };

// The tab of a base weapon name of the file ("WP_BLASTER"...), -1 = not sold
static int Shop_CatOf(const char* base)
{
	static const char* cats[][2] = {
		{ "WP_BRYAR_PISTOL", "0" },{ "WP_BLASTER_PISTOL", "0" },{ "WP_BRYAR_OLD", "0" },
		{ "WP_BLASTER", "1" },{ "WP_DISRUPTOR", "1" },{ "WP_BOWCASTER", "1" },
		{ "WP_REPEATER", "2" },{ "WP_DEMP2", "2" },{ "WP_FLECHETTE", "2" },{ "WP_ROCKET_LAUNCHER", "2" },
		{ "WP_CONCUSSION", "2" },{ "WP_THERMAL", "3" }
	};
	int i;
	for (i = 0; i < (int)(sizeof(cats) / sizeof(cats[0])); i++)
	{
		if (!Q_stricmp(base, cats[i][0]))
		{
			return cats[i][1][0] - '0';
		}
	}
	return -1;
}

// A small tokenizer for the variant file: words, "quoted strings", { }, // and /* */ comments
static const char* Shop_Token(const char** p, char* out, const int size)
{
	const char* s = *p;
	int n = 0;

	out[0] = 0;
	for (;;)
	{
		while (*s && (unsigned char)*s <= ' ')
		{
			s++;
		}
		if (s[0] == '/' && s[1] == '/')
		{
			while (*s && *s != '\n')
			{
				s++;
			}
			continue;
		}
		if (s[0] == '/' && s[1] == '*')
		{
			s += 2;
			while (*s && !(s[0] == '*' && s[1] == '/'))
			{
				s++;
			}
			if (*s)
			{
				s += 2;
			}
			continue;
		}
		break;
	}
	if (!*s)
	{
		*p = s;
		return NULL;
	}
	if (*s == '"')
	{
		s++;
		while (*s && *s != '"')
		{
			if (n < size - 1)
			{
				out[n++] = *s;
			}
			s++;
		}
		if (*s)
		{
			s++;
		}
	}
	else
	{
		while (*s && (unsigned char)*s > ' ')
		{
			if (n < size - 1)
			{
				out[n++] = *s;
			}
			s++;
		}
	}
	out[n] = 0;
	*p = s;
	return out;
}

static void Shop_ParseFile(const char* text)
{
	const char* p = text;
	char tok[MAX_QPATH], key[64];
	int variant = 0;

	while (Shop_Token(&p, tok, sizeof(tok)))
	{
		// <classname> { key value ... }: every block is a variant, numbered from 1 in file order (as the game does)
		char base[64];
		shopItem_t item;

		if (!Shop_Token(&p, tok, sizeof(tok)) || Q_stricmp(tok, "{"))
		{
			break;
		}
		variant++;
		memset(&item, 0, sizeof(item));
		base[0] = 0;
		item.id = variant;
		while (Shop_Token(&p, key, sizeof(key)) && Q_stricmp(key, "}"))
		{
			if (!Shop_Token(&p, tok, sizeof(tok)))
			{
				break;
			}
			if (!Q_stricmp(key, "name"))
			{
				Q_strncpyz(item.name, tok, sizeof(item.name));
			}
			else if (!Q_stricmp(key, "base"))
			{
				Q_strncpyz(base, tok, sizeof(base));
			}
			else if (!Q_stricmp(key, "icon"))
			{
				Q_strncpyz(item.icon, tok, sizeof(item.icon));
			}
			else if (!Q_stricmp(key, "price"))
			{
				item.price = atoi(tok);
			}
		}
		item.cat = Shop_CatOf(base);
		if (item.cat < 0 || variant >= 256 || shopNumItems >= SHOP_MAX_ITEMS - 2 - SHOP_NUM_HOLD)
		{
			continue;
		}
		if (item.price <= 0)
		{
			item.price = item.cat == 3 ? 150 : 300; // BG_WeaponVariantPrice's default
		}
		if (!item.name[0])
		{
			Q_strncpyz(item.name, "?", sizeof(item.name));
		}
		shopItems[shopNumItems++] = item;
	}
}

static void Shop_LoadItems(void)
{
	shopItem_t* it;

	if (shopNumItems >= 0)
	{
		return;
	}
	shopNumItems = 0;
	{
#ifdef PZK_SP
		char* buf = NULL;
		const int len = ui.FS_ReadFile(SHOP_FILE, (void**)&buf);
		if (len > 0 && buf)
		{
			Shop_ParseFile(buf);
		}
		if (buf)
		{
			ui.FS_FreeFile(buf);
		}
#else
		fileHandle_t f = 0;
		const int len = trap->FS_Open(SHOP_FILE, &f, FS_READ);
		if (len > 0 && len < (int)sizeof(shopFileBuf) && f)
		{
			trap->FS_Read(shopFileBuf, len, f);
			shopFileBuf[len] = 0;
			Shop_ParseFile(shopFileBuf);
		}
		if (f)
		{
			trap->FS_Close(f);
		}
#endif
	}
	// supplies
	it = &shopItems[shopNumItems++];
	memset(it, 0, sizeof(*it));
	it->id = SHOP_ITEM_AMMO_ID;
	it->cat = SHOP_CAT_SUPPLIES;
	Q_strncpyz(it->name, "Ammo refill (all your guns)", sizeof(it->name));
	Q_strncpyz(it->icon, "gfx/hud/i_icon_ammo", sizeof(it->icon));
	it = &shopItems[shopNumItems++];
	memset(it, 0, sizeof(*it));
	it->id = SHOP_ITEM_BACTA_ID;
	it->cat = SHOP_CAT_SUPPLIES;
#ifdef PZK_SP
	Q_strncpyz(it->name, "Bacta canister", sizeof(it->name));
	Q_strncpyz(it->icon, "gfx/hud/i_icon_bacta", sizeof(it->icon));
#else
	Q_strncpyz(it->name, "Medpac", sizeof(it->name));
	Q_strncpyz(it->icon, "gfx/hud/i_icon_medkit", sizeof(it->icon));
#endif
	// the holdable items (2026-11-02)
	{
		int h;
		for (h = 0; h < SHOP_NUM_HOLD; h++)
		{
			it = &shopItems[shopNumItems++];
			memset(it, 0, sizeof(*it));
			it->id = SHOP_ITEM_HOLD_ID + h;
			it->cat = SHOP_CAT_ITEMS;
			Q_strncpyz(it->name, shopHoldNames[h][0], sizeof(it->name));
			Q_strncpyz(it->icon, shopHoldNames[h][1], sizeof(it->icon));
		}
	}
}

static int Shop_CatCount(const int cat)
{
	int i, n = 0;
	for (i = 0; i < shopNumItems; i++)
	{
		if (shopItems[i].cat == cat)
		{
			n++;
		}
	}
	return n;
}

static int Shop_NumPages(void)
{
	const int n = Shop_CatCount(shopCat);
	return n > 0 ? (n + SHOP_ROWS - 1) / SHOP_ROWS : 1;
}

// The item on this row of the page (NULL: empty row)
static shopItem_t* Shop_RowItem(const int row)
{
	int i, n = 0;
	const int want = shopPage * SHOP_ROWS + row;

	for (i = 0; i < shopNumItems; i++)
	{
		if (shopItems[i].cat != shopCat)
		{
			continue;
		}
		if (n++ == want)
		{
			return &shopItems[i];
		}
	}
	return NULL;
}

static int Shop_Bit(const unsigned int* bits, const int v)
{
	return v > 0 && v < 256 && bits[v >> 5] & 1u << (v & 31) ? 1 : 0;
}

static int Shop_ItemPrice(const shopItem_t* it)
{
	if (it->id == SHOP_ITEM_AMMO_ID)
	{
		return shopAmmoPrice;
	}
	if (it->id >= SHOP_ITEM_HOLD_ID && it->id < SHOP_ITEM_HOLD_ID + SHOP_NUM_HOLD)
	{
		return shopHoldPrice[it->id - SHOP_ITEM_HOLD_ID];
	}	if (it->id == SHOP_ITEM_BACTA_ID)
	{
		return shopBactaPrice;
	}
	return it->price;
}

static void Shop_ReadBits(const char* hex, unsigned int* bits)
{
	int i, j;
	memset(bits, 0, sizeof(unsigned int) * 8);
	for (i = 0; i < 8 && hex; i++)
	{
		unsigned int w = 0;
		for (j = 0; j < 8; j++)
		{
			const char c = hex[i * 8 + j];
			if (!c)
			{
				return;
			}
			w = w << 4 | (unsigned int)(c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0');
		}
		bits[i] = w;
	}
}

static void Shop_CloseMenu(const int tellGame)
{
	if (!shopOpen)
	{
		return;
	}
	shopOpen = 0;
	shopHover = shopTabHover = -1;
	Menus_CloseByName(SHOP_MENU);
	PZK_SetCatcher(PZK_GetCatcher() & ~KEYCATCH_UI);
	if (tellGame)
	{
		Shop_SendCmd("shopclosed\n");
	}
}

static void Shop_OpenMenu(void)
{
	menuDef_t* menu = Menus_FindByName(SHOP_MENU);
	if (!menu)
	{
		UI_ParseMenu(SHOP_MENU_FILE); // not in the menu lists, load it on demand
		menu = Menus_FindByName(SHOP_MENU);
	}
	if (!menu)
	{
		Com_Printf("Shop: could not load %s\n", SHOP_MENU_FILE);
		return;
	}
	if (!shopOpen)
	{
		shopPage = 0;
	}
	Menus_ActivateByName(SHOP_MENU);
	PZK_SetCatcher(PZK_GetCatcher() | KEYCATCH_UI);
	shopOpen = 1;
}

static void Shop_TalkClose(void)
{
	if (!shopTalkOpen)
	{
		return;
	}
	shopTalkOpen = 0;
	Menus_CloseByName(SHOP_TALK_MENU);
	PZK_SetCatcher(PZK_GetCatcher() & ~KEYCATCH_UI);
#ifdef PZK_SP
	PZK_CvarSet("cl_paused", "0");
#endif
}

static void Shop_SendCmd(const char* text)
{
#ifdef PZK_SP
	ui.Cmd_ExecuteText(EXEC_APPEND, text);
#else
	trap->Cmd_ExecuteText(EXEC_APPEND, text);
#endif
}

static void Shop_TalkAnswer(const int answer)
{
	if (!shopTalkOpen)
	{
		return;
	}
	Shop_TalkClose();
	Shop_SendCmd(va("shoptalk %i\n", answer));
}

static void Shop_TalkOpen(const char* text)
{
	menuDef_t* menu = Menus_FindByName(SHOP_TALK_MENU);
	if (!menu)
	{
		UI_ParseMenu(SHOP_TALK_FILE);
		menu = Menus_FindByName(SHOP_TALK_MENU);
	}
	if (!menu)
	{
		Com_Printf("Shop: could not load %s\n", SHOP_TALK_FILE);
		Shop_SendCmd("shoptalk 0\n");
		return;
	}
	PZK_CvarSet("ui_shopTalk", text && text[0] ? text : "What do you want to do?");
	Menus_ActivateByName(SHOP_TALK_MENU);
	PZK_SetCatcher(PZK_GetCatcher() | KEYCATCH_UI);
#ifdef PZK_SP
	PZK_CvarSet("cl_paused", "1"); // the world waits for the answer
#endif
	shopTalkOpen = 1;
}

// "price:count:max,..." of the holdable items, in the game's table order (2026-11-02)
static void Shop_ReadHoldables(const char* s)
{
	int h;
	for (h = 0; h < SHOP_NUM_HOLD; h++)
	{
		shopHoldPrice[h] = shopHoldCount[h] = 0;
		shopHoldMax[h] = 1;
		if (!s || !*s)
		{
			continue;
		}
		shopHoldPrice[h] = atoi(s);
		while (*s && *s != ':' && *s != ',')
		{
			s++;
		}
		if (*s == ':')
		{
			s++;
			shopHoldCount[h] = atoi(s);
			while (*s && *s != ':' && *s != ',')
			{
				s++;
			}
		}
		if (*s == ':')
		{
			s++;
			shopHoldMax[h] = atoi(s);
			while (*s && *s != ',')
			{
				s++;
			}
		}
		if (*s == ',')
		{
			s++;
		}
	}
}

// The game's words: argv[0] = open / state / talk / close
static void UI_Shop_Process(const int argc, const char** argv)
{
	if (argc < 1)
	{
		return;
	}
	if (!Q_stricmp(argv[0], "talk"))
	{
		Shop_TalkOpen(argc > 1 ? argv[1] : "");
		return;
	}
	if (!Q_stricmp(argv[0], "close"))
	{
		Shop_CloseMenu(0);
		Shop_TalkClose();
		return;
	}
	if (Q_stricmp(argv[0], "open") && Q_stricmp(argv[0], "state"))
	{
		return;
	}
	Shop_LoadItems();
	shopCredits = argc > 1 ? atoi(argv[1]) : 0;
	Shop_ReadBits(argc > 2 ? argv[2] : "", shopOwnedBits);
	Shop_ReadBits(argc > 3 ? argv[3] : "", shopEquipBits);
	shopAmmoPrice = argc > 4 ? atoi(argv[4]) : 0;
	shopBactaPrice = argc > 5 ? atoi(argv[5]) : 0;
	Q_strncpyz(shopMessage, argc > 6 ? argv[6] : "", sizeof(shopMessage));
	Shop_ReadHoldables(argc > 7 ? argv[7] : ""); // the Items tab (2026-11-02)
	if (!Q_stricmp(argv[0], "open"))
	{
		Shop_OpenMenu();
	}
}

/*
--------------------------
Drawing
--------------------------
*/

static void Shop_Fill(const float x, const float y, const float w, const float h, const float* color)
{
	if (!shopWhite)
	{
		shopWhite = PZK_RegisterShader("white");
	}
	PZK_SetColor(color);
	PZK_DrawPic(x, y, w, h, shopWhite);
	PZK_SetColor(NULL);
}

static void Shop_Text(const float x, const float y, const char* text, const float* color, const float scale, const int align)
{
	const int font = PZK_DefaultFont();
	float tx = x;
	if (align)
	{
		const float w = PZK_TextWidth(text, font, 1.0f) * scale;
		tx = align == 1 ? x - w * 0.5f : x - w;
	}
	PZK_DrawText(tx, y, text, (float*)color, font, scale);
}

static void Shop_DrawRow(const int row, const float x, const float y, const float w, const float h)
{
	static const float bg[4] = { 0.10f, 0.14f, 0.20f, 0.85f };
	static const float bgHover[4] = { 0.25f, 0.35f, 0.50f, 0.95f };
	static const float white[4] = { 1, 1, 1, 1 };
	static const float gold[4] = { 1.0f, 0.8f, 0.2f, 1 };
	static const float green[4] = { 0.4f, 1.0f, 0.4f, 1 };
	static const float grey[4] = { 0.6f, 0.6f, 0.6f, 1 };
	static const float red[4] = { 1.0f, 0.4f, 0.35f, 1 };
	shopItem_t* it = Shop_RowItem(row);
	const char* status;
	const float* statusColor;
	int price;

	if (!it)
	{
		return;
	}
	Shop_Fill(x, y, w, h, row == shopHover ? bgHover : bg);
	if (!it->iconTried)
	{
		it->iconTried = 1;
		it->iconShader = it->icon[0] ? PZK_RegisterShader(it->icon) : 0;
	}
	if (it->iconShader)
	{
		PZK_DrawPic(x + 4, y + 2, (h - 4) * 2.0f, h - 4, it->iconShader);
	}
	Shop_Text(x + 12 + (h - 4) * 2.0f, y + h * 0.5f - 8, it->name, white, 0.55f, 0);

	price = Shop_ItemPrice(it);
	if (it->id >= SHOP_ITEM_HOLD_ID && it->id < SHOP_ITEM_HOLD_ID + SHOP_NUM_HOLD)
	{
		// a holdable item (2026-11-02): how many he carries; full = he can't buy more
		const int hi = it->id - SHOP_ITEM_HOLD_ID;
		if (shopHoldCount[hi] >= shopHoldMax[hi])
		{
			status = shopHoldMax[hi] > 1 ? va("Full (%i)", shopHoldCount[hi]) : "Owned";
			statusColor = grey;
		}
		else
		{
			status = shopHoldCount[hi] > 0 ? va("%i cr (have %i)", price, shopHoldCount[hi]) : va("%i cr", price);
			statusColor = shopCredits >= price ? gold : red;
		}
		Shop_Text(x + w - 8, y + h * 0.5f - 8, status, statusColor, 0.55f, 2);
		return;
	}
	if (it->id < SHOP_ITEM_AMMO_ID && it->cat != 3 && Shop_Bit(shopEquipBits, it->id))
	{
		status = "In use";
		statusColor = green;
	}
	else if (it->id < SHOP_ITEM_AMMO_ID && it->cat != 3 && Shop_Bit(shopOwnedBits, it->id))
	{
		status = "Owned: use";
		statusColor = grey;
	}
	else
	{
		status = va("%i cr%s", price, it->cat == 3 ? " (x3)" : "");
		statusColor = shopCredits >= price ? gold : red;
		if (it->cat == 3 && Shop_Bit(shopEquipBits, it->id))
		{
			status = va("%i cr (x3) - throwing", price);
		}
	}
	Shop_Text(x + w - 8, y + h * 0.5f - 8, status, statusColor, 0.55f, 2);
}

static qboolean UI_Shop_OwnerDraw(const int ownerDraw, const float x, const float y, const float w, const float h)
{
	static const float white[4] = { 1, 1, 1, 1 };
	static const float gold[4] = { 1.0f, 0.8f, 0.2f, 1 };
	static const float tabBg[4] = { 0.10f, 0.14f, 0.20f, 0.85f };
	static const float tabOn[4] = { 0.30f, 0.45f, 0.65f, 1.0f };
	static const float tabHover[4] = { 0.20f, 0.30f, 0.45f, 0.95f };
	const int type = (ownerDraw - SHOP_OD_BASE) / 100;
	const int id = (ownerDraw - SHOP_OD_BASE) % 100;

	if (ownerDraw < SHOP_OD_BASE || ownerDraw >= SHOP_OD_BASE + 1000)
	{
		return qfalse;
	}
	Shop_LoadItems();
	switch (type)
	{
	case SHOP_OD_HEADER:
		if (id == 0)
		{
			Shop_Text(x, y, "Shop", gold, 0.8f, 0);
		}
		else if (id == 1)
		{
			Shop_Text(x + w, y, va("Credits: %i", shopCredits), gold, 0.7f, 2);
		}
		else if (id == 2)
		{
			Shop_Text(x, y, shopMessage, white, 0.55f, 0);
		}
		else if (id == 3)
		{
			Shop_Text(x + w * 0.5f, y, va("Page %i / %i", shopPage + 1, Shop_NumPages()), white, 0.55f, 1);
		}
		break;
	case SHOP_OD_TAB:
		if (id >= 0 && id < SHOP_NUM_CATS)
		{
			Shop_Fill(x, y, w, h, id == shopCat ? tabOn : id == shopTabHover ? tabHover : tabBg);
			Shop_Text(x + w * 0.5f, y + h * 0.5f - 8, shopCatNames[id], id == shopCat ? gold : white, SHOP_NUM_CATS > 6 ? 0.5f : 0.6f, 1);
		}
		break;
	case SHOP_OD_ROW:
		if (id >= 0 && id < SHOP_ROWS)
		{
			Shop_DrawRow(id, x, y, w, h);
		}
		break;
	default:
		break;
	}
	return qtrue;
}

/*
--------------------------
Scripts ("uiScript shop_*") and keys
--------------------------
*/

static qboolean UI_Shop_Script(const char* name, void* argsPtr)
{
	pzkArgs_t args = (pzkArgs_t)argsPtr;
	int a = 0, b = 0;

	if (!Q_stricmp(name, "shop_cat"))
	{
		if (Int_Parse(args, &a) && a >= 0 && a < SHOP_NUM_CATS)
		{
			shopCat = a;
			shopPage = 0;
		}
	}
	else if (!Q_stricmp(name, "shop_page"))
	{
		if (Int_Parse(args, &a))
		{
			shopPage += a;
			if (shopPage >= Shop_NumPages())
			{
				shopPage = 0;
			}
			else if (shopPage < 0)
			{
				shopPage = Shop_NumPages() - 1;
			}
		}
	}
	else if (!Q_stricmp(name, "shop_buy"))
	{
		if (Int_Parse(args, &a) && a >= 0 && a < SHOP_ROWS)
		{
			const shopItem_t* it = Shop_RowItem(a);
			if (it)
			{
				Shop_SendCmd(va("shopbuy %i\n", it->id));
			}
		}
	}
	else if (!Q_stricmp(name, "shop_hover"))
	{
		if (Int_Parse(args, &a) && Int_Parse(args, &b))
		{
			if (b)
			{
				shopHover = a;
			}
			else if (shopHover == a)
			{
				shopHover = -1;
			}
		}
	}
	else if (!Q_stricmp(name, "shop_tabhover"))
	{
		if (Int_Parse(args, &a) && Int_Parse(args, &b))
		{
			if (b)
			{
				shopTabHover = a;
			}
			else if (shopTabHover == a)
			{
				shopTabHover = -1;
			}
		}
	}
	else if (!Q_stricmp(name, "shop_close"))
	{
		Shop_CloseMenu(1);
	}
	else if (!Q_stricmp(name, "shop_talk"))
	{
		if (Int_Parse(args, &a))
		{
			Shop_TalkAnswer(a);
		}
	}
	else
	{
		return qfalse;
	}
	return qtrue;
}

#ifdef PZK_SP
// Keys while the talk question or the shop is open (qtrue = used): talk 1 2 3, escape (SP, UI_Pazaak_ChallengeKey)
static qboolean UI_Shop_Key(const int key)
{
	if (shopTalkOpen)
	{
		if (key == '1')
		{
			Shop_TalkAnswer(1);
			return qtrue;
		}
		if (key == '2')
		{
			Shop_TalkAnswer(2);
			return qtrue;
		}
		if (key == '3' || key == A_ESCAPE)
		{
			Shop_TalkAnswer(0);
			return qtrue;
		}
		return qfalse;
	}
	if (shopOpen && key == A_ESCAPE)
	{
		Shop_CloseMenu(1);
		return qtrue;
	}
	return qfalse;
}
#endif