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

// Filename:-	tr_font.h
//
// font support

// This file is shared in the single and multiplayer codebases, so be CAREFUL WHAT YOU ADD/CHANGE!!!!!
// (one copy in shared/rd-common since 2026-11-11; SP-only / MP-only lines are under RD_FONT_SP)

#pragma once

#include "qcommon/q_shared.h"

#if defined(_JK2EXE) || defined(REND2_SP)
#define RD_FONT_SP	// SP exe and SP renderers
#endif

void R_ShutdownFonts();
void R_InitFonts();
int RE_RegisterFont(const char* psName);
#ifndef RD_FONT_SP
float RE_Font_StrLenPixelsNew(const char* psText, int iFontHandle, float fScale = 1.0f);
#endif
int RE_Font_StrLenPixels(const char* psText, int iFontHandle, float fScale = 1.0f);
int RE_Font_StrLenChars(const char* psText);
int RE_Font_HeightPixels(int iFontHandle, float fScale = 1.0f);
void RE_Font_DrawString(int ox, int oy, const char* psText, const float* rgba, int iFontHandle, int iMaxPixelWidth, float fScale = 1.0f);

// Dammit, I can't use this more elegant form because of !@#@!$%% VM code... (can't alter passed in ptrs, only contents of)
//
//unsigned int AnyLanguage_ReadCharFromString( const char **ppsText, qboolean *pbIsTrailingPunctuation = NULL);
//
// so instead we have to use this messier method...
//
#ifdef RD_FONT_SP
unsigned int AnyLanguage_ReadCharFromString(char* psText, int* piAdvanceCount, qboolean* pbIsTrailingPunctuation = nullptr);
unsigned int AnyLanguage_ReadCharFromString(char** psText, qboolean* pbIsTrailingPunctuation = nullptr);
#else
unsigned int AnyLanguage_ReadCharFromString(const char* psText, int* piAdvanceCount, qboolean* pbIsTrailingPunctuation = nullptr);
#endif

qboolean Language_IsAsian();
qboolean Language_UsesSpaces();

// end
