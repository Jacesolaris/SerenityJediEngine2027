/*
===========================================================================
Render thread (r_smp): runs the renderer back end on its own thread while the
main thread runs the game, cgame and the renderer front end for the next frame.
Shared by SP + MP (SJE and MD Update 9 keep this file identical).

Header only, C++17 standard library only, no engine calls. The renderer gives
it two functions:
	exec( cmds )        runs one render command list (RB_ExecuteRenderCommands)
	makeCurrent( on )   makes the GL context current (on) / not current (off)
	                    on the calling thread

The GL context belongs to one thread at a time:
	Submit( cmds )  main thread: waits until the last list is drawn, gives the
	                context to the render thread and starts drawing 'cmds'.
	                Returns at once; 'cmds' must stay untouched until the next
	                Submit or Sync returns.
	Sync()          main thread: waits until the render thread is idle and
	                takes the context back, so the main thread may call GL
	                itself (texture uploads, screenshots, vid_restart...).
	                Cheap when the main thread already has the context.

Errors: an exception thrown by exec (the renderer turns ri.Error on the render
thread into one) stops that command list; TakeError() returns it on the main
thread at the next Submit / Sync so the caller can raise it there.
===========================================================================
*/
#pragma once

#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>

#include "q_profile.h"

namespace qjobs {

// true on the render thread: code shared by both threads can tell where it runs
inline bool& RenderThreadFlag() { thread_local bool onRender = false; return onRender; }
inline bool OnRenderThread() { return RenderThreadFlag(); }

// thrown by the renderer's ri.Error wrapper on the render thread
struct RenderThreadError {
	int level;
	char message[1024];
};

class RenderThread {
public:
	using ExecFn = void ( * )( const void *cmds );
	using MakeCurrentFn = bool ( * )( bool on );

	RenderThread() = default;
	~RenderThread() { Stop(); }
	RenderThread( const RenderThread & ) = delete;
	RenderThread &operator=( const RenderThread & ) = delete;

	// main thread, with the GL context current on it. false = could not start (stay single threaded)
	bool Start( ExecFn exec, MakeCurrentFn makeCurrent ) {
		if ( m_thread.joinable() ) {
			return true;
		}
		m_exec = exec;
		m_makeCurrent = makeCurrent;
		m_owner = OWNER_MAIN;
		m_quit = false;
		m_busy = false;
		m_releaseReq = false;
		m_cmds = nullptr;
		m_hasError = false;
		try {
			m_thread = std::thread( &RenderThread::ThreadMain, this );
		} catch ( ... ) {
			return false;
		}
		return true;
	}

	// main thread: stop the thread; the context ends up current on the main thread
	void Stop() {
		if ( !m_thread.joinable() ) {
			return;
		}
		Sync();
		{
			std::lock_guard<std::mutex> lock( m_mutex );
			m_quit = true;
		}
		m_wake.notify_all();
		m_thread.join();
	}

	bool Running() const { return m_thread.joinable(); }

	// main thread: draw 'cmds' on the render thread (see the top)
	void Submit( const void *cmds ) {
		std::unique_lock<std::mutex> lock( m_mutex );
		WaitIdleLocked( lock );
		if ( m_owner == OWNER_MAIN ) {
			m_makeCurrent( false );
			m_owner = OWNER_NONE;
		}
		m_cmds = cmds;
		m_busy = true;
		lock.unlock();
		m_wake.notify_all();
	}

	// main thread: wait for the render thread and take the GL context (see the top)
	void Sync() {
		std::unique_lock<std::mutex> lock( m_mutex );
		if ( m_owner == OWNER_MAIN && !m_busy ) {
			return;
		}
		WaitIdleLocked( lock );
		if ( m_owner == OWNER_RENDER ) {
			m_releaseReq = true;
			m_wake.notify_all();
			m_done.wait( lock, [&] { return !m_releaseReq; } );
		}
		if ( m_owner != OWNER_MAIN ) {
			m_makeCurrent( true );
			m_owner = OWNER_MAIN;
		}
	}

	// main thread: wait until the last list is drawn (the GL context stays where it is)
	void WaitIdle() {
		std::unique_lock<std::mutex> lock( m_mutex );
		WaitIdleLocked( lock );
	}

	// main thread: is the render thread drawing right now?
	bool Busy() {
		std::lock_guard<std::mutex> lock( m_mutex );
		return m_busy;
	}

	// main thread, after Submit / Sync: an error the last command list stopped on
	bool TakeError( RenderThreadError &out ) {
		std::lock_guard<std::mutex> lock( m_mutex );
		if ( !m_hasError ) {
			return false;
		}
		out = m_error;
		m_hasError = false;
		return true;
	}

private:
	enum owner_t { OWNER_NONE, OWNER_MAIN, OWNER_RENDER };

	void WaitIdleLocked( std::unique_lock<std::mutex> &lock ) {
		if ( m_busy ) {
			PROFILE_ZONE( "Wait for render thread" );
			m_done.wait( lock, [&] { return !m_busy; } );
		}
	}

	void ThreadMain() {
		PROFILE_THREAD( "Render thread" );
		RenderThreadFlag() = true;

		std::unique_lock<std::mutex> lock( m_mutex );
		for ( ;; ) {
			m_wake.wait( lock, [&] { return m_quit || m_releaseReq || m_busy; } );
			if ( m_releaseReq ) {
				if ( m_owner == OWNER_RENDER ) {
					m_makeCurrent( false );
					m_owner = OWNER_NONE;
				}
				m_releaseReq = false;
				m_done.notify_all();
				continue;
			}
			if ( m_quit ) {
				if ( m_owner == OWNER_RENDER ) {
					m_makeCurrent( false );
					m_owner = OWNER_NONE;
				}
				return;
			}

			// m_busy: draw m_cmds
			RenderThreadError err{};
			bool failed = false;
			if ( m_owner != OWNER_RENDER ) {
				if ( m_makeCurrent( true ) ) {
					m_owner = OWNER_RENDER;
				} else {
					err.level = 0;	// ERR_FATAL
					strcpy( err.message, "render thread: could not make the GL context current" );
					failed = true;
				}
			}
			const void *cmds = m_cmds;
			lock.unlock();
			if ( !failed ) {
				try {
					m_exec( cmds );
				} catch ( const RenderThreadError &e ) {
					err = e;
					failed = true;
				} catch ( ... ) {
					err.level = 0;	// ERR_FATAL
					strcpy( err.message, "render thread: unknown exception" );
					failed = true;
				}
			}
			lock.lock();
			if ( failed && !m_hasError ) {
				m_error = err;
				m_hasError = true;
			}
			m_busy = false;
			m_done.notify_all();
		}
	}

	std::thread					m_thread;
	std::mutex					m_mutex;		// guards everything below
	std::condition_variable		m_wake;			// render thread: work, release or quit
	std::condition_variable		m_done;			// main thread: list drawn / context released
	ExecFn						m_exec = nullptr;
	MakeCurrentFn				m_makeCurrent = nullptr;
	const void					*m_cmds = nullptr;
	owner_t						m_owner = OWNER_MAIN;
	bool						m_busy = false;
	bool						m_releaseReq = false;
	bool						m_quit = false;
	bool						m_hasError = false;
	RenderThreadError			m_error{};
};

} // namespace qjobs
