/*
===========================================================================
Profiler markers (Tracy). Shared by SP + MP, engine + renderers + game modules.

Build with the CMake option UseTracy=ON to turn these on; otherwise every
macro below compiles to nothing, so they cost nothing in a normal build.

  PROFILE_FRAME()           end of one engine frame (once per Com_Frame)
  PROFILE_ZONE( "Name" )    times the rest of the enclosing { } block (C++ only)
  PROFILE_THREAD( "Name" )  names the calling thread in the viewer
  PROFILE_TEXT( str )       attaches a string (an image or shader name) to the
                            enclosing PROFILE_ZONE (put it right after it)
  PROFILE_ZONE_IF( "Name", cond )  like PROFILE_ZONE, but only recorded when
                            cond is true (several on one block = categories;
                            put each on its own line)
  PROFILE_ENABLED           1 in a Tracy build, else 0 (for #if around
                            values only the markers use)

Viewer: tracy-profiler.exe of the SAME version as lib/tracy (v0.14.1).
Start the game, then press Connect in the viewer. With TRACY_ON_DEMAND the
game only records while the viewer is connected.
===========================================================================
*/
#pragma once

#if defined( TRACY_ENABLE ) && defined( __cplusplus )

#include "tracy/Tracy.hpp"
#include <cstring>

#define PROFILE_ENABLED				1
#define PROFILE_CAT_( a, b )		a##b
#define PROFILE_CAT( a, b )			PROFILE_CAT_( a, b )
#define PROFILE_FRAME()				FrameMark
#define PROFILE_ZONE( name )		ZoneScopedN( name )
#define PROFILE_ZONE_IF( name, cond )	ZoneNamedN( PROFILE_CAT( profZone_, __LINE__ ), name, ( cond ) )
#define PROFILE_THREAD( name )		tracy::SetThreadName( name )
#define PROFILE_TEXT( str )			do { const char *profText_ = ( str ); if ( profText_ ) { ZoneText( profText_, strlen( profText_ ) ); } } while ( 0 )

#else

#define PROFILE_ENABLED				0
#define PROFILE_FRAME()				do {} while ( 0 )
#define PROFILE_ZONE( name )		do {} while ( 0 )
#define PROFILE_ZONE_IF( name, cond )	do {} while ( 0 )
#define PROFILE_THREAD( name )		do {} while ( 0 )
#define PROFILE_TEXT( str )			do {} while ( 0 )

#endif
