/*
===========================================================================
Copyright (C) 2026, SerenityJediEngine2027 contributors

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

/*
tr_smp.cpp - r_smp: the vanilla renderer's back end on its own thread (SP + MP).

r_smp 0 (default, latched): nothing here runs and the renderer works exactly as before.

r_smp 1: RE_EndFrame hands the frame's command list to the render thread and returns at
once, so the game, cgame and the front end of the next frame run while it is drawn. There
are two backEndData buffers: the front end fills one while the back end draws the other.
Everything the back end needs from the front end is in the buffer it draws (entities,
draw surfaces, polys, the commands, a copy of the frame's state in backEndData->smp, and
ghoul2 surfaces already skinned by the front end - R_G2SmpPrepareDrawSurfs).

The GL context is current on one thread at a time (q_renderthread.h). Main thread code that
calls GL must call R_SyncRenderThread() first (R_IssuePendingRenderCommands does).

On the render thread, RI.Printf is queued and printed by the main thread, and RI.Error stops
the command list; the main thread raises the error at its next sync or frame.
*/

#ifdef _JK2EXE
#include "server/exe_headers.h"
#endif
#include "rd-vanilla/tr_local.h"
#include "q_renderthread.h"

#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// SJE's renderers have 'refimport_t ri', MD's MP renderer 'refimport_t* ri': the same code for both
template <class T> static T& SMP_Deref(T& r) { return r; }
template <class T> static T& SMP_Deref(T* r) { return *r; }
#define RI SMP_Deref(ri)

// SP and MP Cvar_Get differ: MP takes a description
#ifdef _JK2EXE
#define SMP_CVAR_DESC
#else
#define SMP_CVAR_DESC , nullptr
#endif

cvar_t* r_smp;

static qjobs::RenderThread* s_rt = nullptr;		// heap: stopped in R_SmpShutdown, never by a static destructor
static backEndData_t* s_smpData[SMP_FRAMES] = {};
static int s_smpFrame = 0;
static cvar_t* s_fullscreen = nullptr;			// the window's cvars: their changes are done on the main thread
static cvar_t* s_swapInterval = nullptr;

// GL context hand-over
#ifdef _WIN32
static HDC s_hdc = nullptr;
static HGLRC s_hglrc = nullptr;

static bool SMP_MakeCurrent(const bool on)
{
	return wglMakeCurrent(on ? s_hdc : nullptr, on ? s_hglrc : nullptr) != FALSE;
}
#endif

// console and errors from the render thread
static void (QDECL* s_realPrintf)(int printLevel, const char* fmt, ...) = nullptr;
static void (QDECL* s_realError)(int errorLevel, const char* fmt, ...) NORETURN_PTR = nullptr;
static std::mutex s_printMutex;
static std::vector<std::pair<int, std::string>> s_prints;

static void SMP_FlushPrints()
{
	std::vector<std::pair<int, std::string>> prints;
	{
		std::lock_guard<std::mutex> lock(s_printMutex);
		prints.swap(s_prints);
	}
	for (const auto& p : prints)
	{
		s_realPrintf(p.first, "%s", p.second.c_str());
	}
}

static void QDECL SMP_Printf(const int printLevel, const char* fmt, ...)
{
	va_list argptr;
	char text[4096];
	va_start(argptr, fmt);
	Q_vsnprintf(text, sizeof text, fmt, argptr);
	va_end(argptr);

	if (qjobs::OnRenderThread())
	{
		std::lock_guard<std::mutex> lock(s_printMutex);
		if (s_prints.size() < 256)	// a message every frame must not grow this for ever
		{
			s_prints.emplace_back(printLevel, text);
		}
		return;
	}
	SMP_FlushPrints();
	s_realPrintf(printLevel, "%s", text);
}

static void QDECL SMP_Error(const int errorLevel, const char* fmt, ...)
{
	va_list argptr;
	char text[1024];
	va_start(argptr, fmt);
	Q_vsnprintf(text, sizeof text, fmt, argptr);
	va_end(argptr);

	if (qjobs::OnRenderThread())
	{
		// stop this command list; the main thread raises it (SMP_RaiseError)
		qjobs::RenderThreadError e{};
		e.level = errorLevel;
		Q_strncpyz(e.message, text, sizeof e.message);
		throw e;
	}
	s_realError(errorLevel, "%s", text);
}

// main thread: an error the render thread stopped on
static void SMP_RaiseError()
{
	qjobs::RenderThreadError e{};
	if (s_rt && s_rt->TakeError(e))
	{
		s_realError(e.level, "%s", e.message);
	}
}

// render thread: draw one submitted frame (a backEndData), reading the front end state from its copy
static void SMP_Exec(const void* data)
{
	const auto bd = static_cast<const backEndData_t*>(data);
	backEnd.smp = &bd->smp;
	try
	{
		RB_ExecuteRenderCommands(bd->commands.cmds);
	}
	catch (...)
	{
		backEnd.smp = nullptr;
		throw;
	}
	backEnd.smp = nullptr;
}

qboolean R_SmpActive()
{
	return static_cast<qboolean>(s_rt != nullptr);
}

qboolean R_SmpOnRenderThread()
{
	return static_cast<qboolean>(qjobs::OnRenderThread());
}

int R_SmpFrame()
{
	return s_smpFrame;
}

void R_SmpInit()
{
	if (s_rt || !r_smp || !r_smp->integer)
	{
		return;
	}
#ifdef _WIN32
	s_hdc = wglGetCurrentDC();
	s_hglrc = wglGetCurrentContext();
	if (!s_hdc || !s_hglrc)
	{
		RI.Printf(PRINT_ALL, "r_smp: no current GL context, render thread not started\n");
		return;
	}

	// the second frame buffer, the same size and kind of memory as the first
	s_smpData[0] = backEndData;
	s_smpData[1] = R_SmpAllocBackEndData();
	if (!s_smpData[1])
	{
		RI.Printf(PRINT_ALL, "r_smp: out of memory, render thread not started\n");
		return;
	}
	s_smpFrame = 0;

	s_fullscreen = RI.Cvar_Get("r_fullscreen", "0", 0 SMP_CVAR_DESC);
	s_swapInterval = RI.Cvar_Get("r_swapInterval", "0", 0 SMP_CVAR_DESC);

	s_realPrintf = RI.Printf;
	s_realError = RI.Error;
	RI.Printf = SMP_Printf;
	RI.Error = SMP_Error;

	s_rt = new qjobs::RenderThread;
	if (!s_rt->Start(SMP_Exec, SMP_MakeCurrent))
	{
		delete s_rt;
		s_rt = nullptr;
		RI.Printf = s_realPrintf;
		RI.Error = s_realError;
		RI.Printf(PRINT_ALL, "r_smp: could not start the render thread\n");
		return;
	}
	R_G2SmpNewFrame(s_smpFrame);
	RI.Printf(PRINT_ALL, "r_smp: render thread started\n");
#else
	RI.Printf(PRINT_ALL, "r_smp: not supported on this platform\n");
#endif
}

void R_SmpShutdown()
{
	if (!s_rt)
	{
		return;
	}
	s_rt->Stop();	// the GL context is current on the main thread again
	SMP_FlushPrints();
	qjobs::RenderThreadError e{};
	const bool failed = s_rt->TakeError(e);
	delete s_rt;
	s_rt = nullptr;
	RI.Printf = s_realPrintf;
	RI.Error = s_realError;
	if (failed)
	{
		RI.Printf(PRINT_ALL, "r_smp: render thread error at shutdown: %s\n", e.message);
	}

	// back to one buffer: the first (its memory goes with the rest of the renderer's)
	backEndData = s_smpData[0];
	s_smpData[0] = s_smpData[1] = nullptr;
	s_smpFrame = 0;
	R_G2SmpNewFrame(0);
}

void R_SyncRenderThread()
{
	if (!s_rt || qjobs::OnRenderThread())
	{
		return;
	}
	s_rt->Sync();
	SMP_FlushPrints();
	// an error the render thread stopped on is raised at the end of the frame (R_SmpWaitIdle), not here
	// in the middle of making a shader or an image
}

// video map shaders (cinematics on surfaces): the back end only binds their texture and asks for
// the next picture; the main thread decodes and uploads it before the next frame
static std::atomic<unsigned> s_wantCinematics{ 0 };

void R_SmpWantCinematic(const int handle)
{
	if (handle >= 0 && handle < 32)
	{
		s_wantCinematics.fetch_or(1u << handle, std::memory_order_relaxed);
	}
}

void R_SmpRunCinematics()
{
	if (!s_rt)
	{
		return;
	}
	const unsigned want = s_wantCinematics.exchange(0, std::memory_order_relaxed);
	if (!want)
	{
		return;
	}
	R_SyncRenderThread();	// RE_UploadCinematic uploads here
	for (int h = 0; h < 32; h++)
	{
		if (want & 1u << h)
		{
			RI.CIN_RunCinematic(h);
			RI.CIN_UploadCinematic(h);
		}
	}
}

static int s_lastBackEndMsec = 0;

// main thread: wait until the last frame is drawn (its performance counters can be read)
void R_SmpWaitIdle()
{
	if (!s_rt)
	{
		return;
	}
	s_rt->WaitIdle();
	SMP_FlushPrints();
	SMP_RaiseError();
	s_lastBackEndMsec = backEnd.pc.msec;
	backEnd.pc.msec = 0;
}

int R_SmpLastBackEndMsec()
{
	return s_lastBackEndMsec;
}

// main thread: wait until the render thread is idle, without taking the GL context (for front end
// code that borrows back end state, such as SurfIsOffscreen with tess)
void R_SmpWaitRenderThread()
{
	if (!s_rt || qjobs::OnRenderThread())
	{
		return;
	}
	s_rt->WaitIdle();
	SMP_FlushPrints();
}

// main thread, end of a frame: let the back end draw 'cmds' on its own and move the front end
// to the other buffer. The other buffer's last frame is finished (Submit waits for it).
static bool s_forceMainFrame = false;

void R_SmpForceMainThreadFrame()
{
	s_forceMainFrame = true;
}

// render thread, RB_SwapBuffers: only the swap. WIN_Present also makes window changes (fullscreen,
// swap interval) that must happen on the main thread: R_SmpEndFrame draws those frames there.
void R_SmpPresent()
{
	PROFILE_ZONE("Present (swap buffers, render thread)");
#ifdef _WIN32
	SwapBuffers(s_hdc);
#endif
}

void R_SmpEndFrame(const void* cmds)
{
	// A window change (fullscreen toggle, swap interval) is made by WIN_Present, and a video capture
	// writes the file with the client: draw such a frame on the main thread, which presents with
	// WIN_Present. Changes made while the render thread draws are picked up by the next frame.
	if ((s_fullscreen && s_fullscreen->modified) || (s_swapInterval && s_swapInterval->modified) || s_forceMainFrame)
	{
		s_forceMainFrame = false;
		R_SyncRenderThread();
		SMP_RaiseError();
		RB_ExecuteRenderCommands(cmds);
		R_G2SmpNewFrame(s_smpFrame);	// drawn: this buffer's surfaces and memory can be reused
		return;
	}

	s_rt->WaitIdle();
	SMP_FlushPrints();
	SMP_RaiseError();

	R_SmpCaptureFrameState(&backEndData->smp);
	s_rt->Submit(backEndData);	// SMP_Exec draws its commands

	s_smpFrame ^= 1;
	backEndData = s_smpData[s_smpFrame];
	R_G2SmpNewFrame(s_smpFrame);
}