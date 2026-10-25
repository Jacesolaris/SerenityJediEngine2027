/*
===========================================================================
Copyright (C) 2000 - 2013, Raven Software, Inc.
Copyright (C) 2001 - 2013, Activision, Inc.
Copyright (C) 2005 - 2015, ioquake3 contributors
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

#pragma once

#include "../rd-common/tr_public.h"
#include "../rd-common/tr_font.h"

extern refimport_t ri;

/*
================================================================================
Noise Generation
================================================================================
*/
// Initialize the noise generator.
void R_NoiseInit();

// Get random 4-component vector.
float R_NoiseGet4f(const float x, const float y, const float z, const float t);

// Get the noise time.
float GetNoiseTime(int t);

/*
================================================================================
 Image Loading
================================================================================
*/
// Initialize the image loader.
void R_ImageLoader_Init();

typedef void (*ImageLoaderFn)(const char* filename, byte** pic, int* width, int* height);

// Adds a new image loader to handle a new image type. The extension should not
// begin with a period (a full stop).
qboolean R_ImageLoader_Add(const char* extension, ImageLoaderFn imageLoader);

// Load an image from file.
void R_LoadImage(const char* shortname, byte** pic, int* width, int* height);

// Load raw image data from TGA image.
void LoadTGA(const char* name, byte** pic, int* width, int* height);

// Load raw image data from JPEG image.
void LoadJPG(const char* filename, unsigned char** pic, int* width, int* height);

// Load raw image data from PNG image.
void LoadPNG(const char* filename, byte** data, int* width, int* height);

//Load raw image data from JPEG input (save game screenshots).
void LoadJPGFromBuffer(byte* inputBuffer, size_t len, byte** pic, int* width, int* height);

// Worker-safe JPG decode (tr_image_jpg.cpp) for the image prefetch: RGBA pixels exactly as LoadJPG
// makes them, but in malloc() memory (free() them), no engine calls, nothing printed. Returns false on
// any error or libjpeg warning: the caller then loads the file the normal way, which prints as before.
bool R_DecodeJPGThreadSafe(const byte* data, size_t len, byte** pic, int* width, int* height);
// Worker-safe PNG decode (tr_image_png.cpp) for the image prefetch: RGBA pixels exactly as LoadPNG, malloc'd
// (free()), silent; false on anything unusual (LoadPNG then loads that file the normal way)
bool R_DecodePNGThreadSafe(const byte* data, size_t len, byte** pic, int* width, int* height);

// Image prefetch (tr_image_load.cpp). Remembers the JPGs each map loads, in order (imagecache/*.txt).
// On the next load of that map they are read ahead and decoded on the render job workers in batches,
// and R_LoadImage takes the finished pixels instead of decoding them itself. Only the decode moves:
// every file is still read through the normal file system, so the image chosen never changes.
// Off when r_threads is 0. Main thread only.
void R_ImagePrefetch_Begin(const char* mapName, const char* rendererTag, qboolean loadStart = qfalse);	// RE_RegisterMedia_LevelLoadBegin (qtrue) / RE_LoadWorldMap
void R_ImagePrefetch_End(qboolean saveList);								// RE_EndRegistration (qtrue)
void R_ImagePrefetch_RendererShutdown(qboolean levelChange);				// RE_Shutdown: a map load's renderer flush keeps it

// The renderer's job pool (tr_ghoul2.cpp): started with r_threads workers, or nullptr when r_threads
// is 0 so callers run serially. Main thread only.
namespace qjobs { class Pool; }
qjobs::Pool* R_RenderJobPool();

/*
================================================================================
 Image saving
================================================================================
*/
// Convert raw image data to JPEG format and store in buffer.
size_t RE_SaveJPGToBuffer(byte* buffer, size_t bufSize, int quality, int image_width, int image_height, byte* image_buffer, int padding, bool flip_vertical);
size_t RE_SaveVideoJPGToBuffer(byte* buffer, size_t bufSize, int quality, int image_width, int image_height, byte* image_buffer, int padding);

// video recording frames compressed on worker threads (tr_video_encoder.cpp)
byte* R_VideoEncoderBegin(size_t bytes);
void R_VideoEncoderSubmit(int width, int height, int padding, int quality, qboolean motionJpeg, void (*write)(const byte*, int));
void R_VideoEncoderFlush(void (*write)(const byte*, int), qboolean all);
void R_VideoEncoderShutdown(void);
// video frames read back asynchronously through two pixel buffer objects (tr_video_encoder.cpp): each frame is picked up on
// the next capture, so glReadPixels does not stall the render thread. The renderer passes its GL buffer functions
// (vanilla loads the ARB ones itself, rend2 has its own); without them the frame is read synchronously as before.
#ifdef _WIN32
#define VIDEO_GLAPI __stdcall
#else
#define VIDEO_GLAPI
#endif
struct videoReadbackGL_t
{
	void (VIDEO_GLAPI* GenBuffers)(int n, unsigned int* buffers) = nullptr;
	void (VIDEO_GLAPI* DeleteBuffers)(int n, const unsigned int* buffers) = nullptr;
	void (VIDEO_GLAPI* BindBuffer)(unsigned int target, unsigned int buffer) = nullptr;
	void (VIDEO_GLAPI* BufferData)(unsigned int target, ptrdiff_t size, const void* data, unsigned int usage) = nullptr;
	void (VIDEO_GLAPI* GetBufferSubData)(unsigned int target, ptrdiff_t offset, ptrdiff_t size, void* data) = nullptr;
	void (VIDEO_GLAPI* ReadPixels)(int x, int y, int width, int height, unsigned int format, unsigned int type, void* pixels) = nullptr;
};
void R_VideoCaptureFrame(const videoReadbackGL_t& gl, int width, int height, int padwidth, int quality, qboolean motionJpeg,
	void (*gammaCorrect)(byte*, int), void (*write)(const byte*, int));
void R_VideoCaptureFlush(const videoReadbackGL_t& gl, void (*write)(const byte*, int));
void R_VideoCaptureShutdown(const videoReadbackGL_t& gl);

// Save raw image data as JPEG image file.
void RE_SaveJPG(const char* filename, int quality, int image_width, int image_height, byte* image_buffer, int padding);

// Save raw image data as PNG image file.
int RE_SavePNG(const char* filename, const byte* buf, size_t width, size_t height, int byte_depth);

void* R_Malloc(const int iSize, const memtag_t eTag, const qboolean bZeroit = qfalse);
void R_Free(void* pvAddress);
int R_MemSize(const memtag_t eTag);
void R_MorphMallocTag(void* pvBuffer, const memtag_t eDesiredTag);
void* R_Hunk_Alloc(const int iSize, const qboolean bZeroit = qtrue);
