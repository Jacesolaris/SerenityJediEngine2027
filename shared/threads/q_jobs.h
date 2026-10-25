/*
===========================================================================
Small job system: a fixed pool of worker threads and a blocking ParallelFor.
Shared by SP + MP (SJE and MD Update 9 keep this file identical).

Header only, C++17 standard library only, no engine calls, so any module
(exe, renderer, game) can own a pool.

RULES for code run by a job (it runs on another thread):
  - no Z_Malloc / Hunk / Com_Printf / Cvar_* / traces / ICARUS / entity edits
  - only touch the data the job was given; each index must be independent
  - do engine calls (traces, cvars, printing, allocation) on the main thread
    before or after the ParallelFor

Use:
	static qjobs::Pool s_pool;
	s_pool.Start( qjobs::Pool::DefaultThreads() );   // e.g. from a cvar, 0 = off
	s_pool.ParallelFor( count, 4, [&]( int begin, int end ) {
		for ( int i = begin; i < end; i++ ) { ...independent work on item i... }
	} );
	s_pool.Stop();                                     // at shutdown

ParallelFor returns when every index is done. The calling thread works too.
With no workers (Start(0), or never started) it simply runs on the caller,
so a cvar can turn threading off without changing any calling code.
Only one ParallelFor runs at a time per pool (calls from several threads
are serialised).
===========================================================================
*/
#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "q_profile.h"

namespace qjobs {

// true on a pool's worker threads: code that also runs on the main thread can
// skip engine calls there (printing, cvars) - see the rules at the top
inline bool& WorkerThreadFlag() { thread_local bool onWorker = false; return onWorker; }
inline bool OnWorkerThread() { return WorkerThreadFlag(); }

class Pool {
public:
	Pool() = default;
	~Pool() { Stop(); }
	Pool( const Pool & ) = delete;
	Pool &operator=( const Pool & ) = delete;

	// Worker threads to use by default: the CPU's hardware threads minus one
	// (the calling thread works too), at least 0, at most kMaxThreads.
	static int DefaultThreads() {
		const int hw = static_cast<int>( std::thread::hardware_concurrency() );
		return std::max( 0, std::min( hw - 1, kMaxThreads ) );
	}

	static constexpr int kMaxThreads = 31;

	// (Re)start with this many worker threads. 0 = no workers (run on the caller).
	void Start( int numThreads ) {
		numThreads = std::max( 0, std::min( numThreads, kMaxThreads ) );
		if ( numThreads == static_cast<int>( m_workers.size() ) ) {
			return;
		}
		Stop();
		m_quit = false;
		m_workers.reserve( numThreads );
		for ( int i = 0; i < numThreads; i++ ) {
			m_workers.emplace_back( &Pool::WorkerMain, this, i );
		}
	}

	// Stop and join all workers. Safe to call more than once.
	void Stop() {
		{
			std::lock_guard<std::mutex> lock( m_mutex );
			m_quit = true;
		}
		m_wake.notify_all();
		for ( std::thread &t : m_workers ) {
			if ( t.joinable() ) {
				t.join();
			}
		}
		m_workers.clear();
	}

	int NumThreads() const { return static_cast<int>( m_workers.size() ); }

	// Call fn( begin, end ) over [0, count) in chunks of at least minChunk
	// indices, spread over the workers and the caller. Blocks until done.
	template <class F>
	void ParallelFor( const int count, const int minChunk, F &&fn ) {
		if ( count <= 0 ) {
			return;
		}
		const int threads = NumThreads() + 1;	// workers + the caller
		const int chunk = std::max( std::max( minChunk, 1 ), ( count + threads * 4 - 1 ) / ( threads * 4 ) );
		if ( threads == 1 || count <= chunk ) {
			fn( 0, count );
			return;
		}

		std::lock_guard<std::mutex> one( m_runMutex );	// one ParallelFor at a time
		Job job;
		job.count = count;
		job.chunk = chunk;
		job.ctx = &fn;
		job.call = []( void *ctx, int b, int e ) { ( *static_cast<F *>( ctx ) )( b, e ); };
		{
			std::lock_guard<std::mutex> lock( m_mutex );
			m_job = &job;
			m_generation++;
		}
		m_wake.notify_all();

		RunChunks( job );	// the caller works too

		// Wait until every chunk has finished AND no worker still holds the job
		// (a worker that took it may be about to read its counters one last
		// time). 'job' lives on this stack frame, so both must be true before
		// returning. Workers only take m_job under m_mutex, and it is cleared
		// under the same lock, so none can pick it up after this.
		std::unique_lock<std::mutex> lock( m_mutex );
		m_done.wait( lock, [&] { return job.finished.load( std::memory_order_acquire ) >= job.numChunks() && m_busy == 0; } );
		m_job = nullptr;
	}

private:
	struct Job {
		int count = 0;
		int chunk = 1;
		void *ctx = nullptr;
		void ( *call )( void *, int, int ) = nullptr;
		std::atomic<int> next{ 0 };		// next chunk to take
		std::atomic<int> finished{ 0 };	// chunks completed
		int numChunks() const { return ( count + chunk - 1 ) / chunk; }
	};

	void RunChunks( Job &job ) {
		const int total = job.numChunks();
		for ( ;; ) {
			const int c = job.next.fetch_add( 1, std::memory_order_relaxed );
			if ( c >= total ) {
				return;
			}
			const int b = c * job.chunk;
			const int e = std::min( b + job.chunk, job.count );
			job.call( job.ctx, b, e );
			if ( job.finished.fetch_add( 1, std::memory_order_acq_rel ) + 1 == total ) {
				std::lock_guard<std::mutex> lock( m_mutex );	// pairs with the caller's wait
				m_done.notify_all();
			}
		}
	}

	void WorkerMain( const int index ) {
		char name[32];
		snprintf( name, sizeof( name ), "Job worker %d", index + 1 );
		PROFILE_THREAD( name );
		( void )name;
		WorkerThreadFlag() = true;

		unsigned seen = 0;
		for ( ;; ) {
			Job *job = nullptr;
			{
				std::unique_lock<std::mutex> lock( m_mutex );
				m_wake.wait( lock, [&] { return m_quit || ( m_job && m_generation != seen ); } );
				if ( m_quit ) {
					return;
				}
				seen = m_generation;
				job = m_job;
				m_busy++;
			}
			RunChunks( *job );
			{
				std::lock_guard<std::mutex> lock( m_mutex );
				m_busy--;
			}
			m_done.notify_all();
		}
	}

	std::vector<std::thread>	m_workers;
	std::mutex					m_mutex;		// guards m_job, m_generation, m_quit, m_busy
	std::mutex					m_runMutex;		// one ParallelFor at a time
	std::condition_variable		m_wake;			// workers: a new job or quit
	std::condition_variable		m_done;			// caller: all chunks finished
	Job							*m_job = nullptr;
	unsigned					m_generation = 0;
	int							m_busy = 0;		// workers inside RunChunks
	bool						m_quit = false;
};

} // namespace qjobs
