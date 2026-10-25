/*
===========================================================================
Copyright (C) 1999 - 2005, Id Software, Inc.
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

#if defined(_JK2EXE) || defined(REND2_SP)	// SP renderers
#include "server/exe_headers.h"
#endif
#include "q_profile.h"

#include "rd-common/tr_common.h"
#include "q_jobs.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// refimport access and the memory LoadJPG returns its pixels in (the prefetch must match)
#define PF_RI ri.
#if defined(_JK2EXE) || defined(REND2_SP)	// SP renderers
#define PF_ALLOC(bytes) R_Malloc((bytes), TAG_TEMP_WORKSPACE, qfalse)
#else
#define PF_ALLOC(bytes) Z_Malloc((bytes), TAG_TEMP_WORKSPACE, qfalse)
#endif

constexpr int MAX_IMAGE_LOADERS = 10;
struct ImageLoaderMap
{
	const char* extension;
	ImageLoaderFn loader;
} imageLoaders[MAX_IMAGE_LOADERS];
int numImageLoaders;

/*
=================
Finds the image loader associated with the given extension.
=================
*/
const ImageLoaderMap* FindImageLoader(const char* extension)
{
	for (int i = 0; i < numImageLoaders; i++)
	{
		if (Q_stricmp(extension, imageLoaders[i].extension) == 0)
		{
			return &imageLoaders[i];
		}
	}

	return nullptr;
}

/*
=================
Adds a new image loader to load the specified image file extension.
The 'extension' string should not begin with a period (full stop).
=================
*/
qboolean R_ImageLoader_Add(const char* extension, const ImageLoaderFn imageLoader)
{
	if (numImageLoaders >= MAX_IMAGE_LOADERS)
	{
		ri.Printf(PRINT_DEVELOPER, "R_AddImageLoader: Cannot add any more image loaders (maximum %d).\n", MAX_IMAGE_LOADERS);
		return qfalse;
	}

	if (FindImageLoader(extension) != nullptr)
	{
		ri.Printf(PRINT_DEVELOPER, "R_AddImageLoader: Image loader already exists for extension \"%s\".\n", extension);
		return qfalse;
	}

	ImageLoaderMap* newImageLoader = &imageLoaders[numImageLoaders];
	newImageLoader->extension = extension;
	newImageLoader->loader = imageLoader;

	numImageLoaders++;

	return qtrue;
}

/*
=================
Initializes the image loader, and adds the built-in
image loaders
=================
*/
void R_ImageLoader_Init()
{
	Com_Memset(imageLoaders, 0, sizeof imageLoaders);
	numImageLoaders = 0;

	R_ImageLoader_Add("jpg", LoadJPG);
	R_ImageLoader_Add("png", LoadPNG);
	R_ImageLoader_Add("tga", LoadTGA);
}

/*
================================================================================
 Image prefetch (declared in tr_common.h)

 Each map load remembers the JPGs and PNGs it decoded, in order, in imagecache/<map>_<renderer>.txt.
 The next load of that map reads that list. When R_LoadImage asks for a listed image, it and the next listed
 files are read here (main thread, the normal file system) and decoded by background decode threads while the
 load carries on (uploads, shaders, models); R_LoadImage then takes the pixels, waiting only if that one is still
 being decoded (background since 23/10/2026; before, each batch was decoded with the main thread waiting). A listed file is read through FS_ReadFile under the very name LoadJPG/LoadPNG would use,
 in the same load (the file system does not change during it), so the image chosen can never
 change - only where the decode runs. Anything unusual (decode error, libjpeg/libpng warning, list out of date,
 too much waiting) falls back to LoadJPG/LoadPNG exactly as before. r_threads 0 = off.
 During a load, a name that found no image is remembered and not looked for again (23/10/2026): rend2 asks for
 <image>_nh, _n and _specGloss for every lit stage, each tried as .jpg, .png and .tga, and most do not exist -
 each miss searches every pk3 and opens the file in every game folder. The file system does not change during
 a load, so asking again would give the same answer. Cleared at the end of every load.
 Everything here runs on the main thread except PF_DecodeThread (R_DecodeJPGThreadSafe / R_DecodePNGThreadSafe).
 (PNGs added 23/10/2026: lists written before that hold JPGs only and still work.)
================================================================================
*/
namespace {

constexpr int		PF_AHEAD_FILES = 64;					// listed files kept read ahead of the one asked for, decoding in the background
constexpr size_t	PF_AHEAD_BYTES = 16u * 1024 * 1024;		// compressed bytes allowed read ahead and not yet taken
constexpr size_t	PF_READY_BYTES = 192u * 1024 * 1024;	// decoded pixels allowed to wait (older leftovers dropped first)
constexpr int		PF_MAX_LIST = 16384;
constexpr int		PF_MAX_DECODERS = 8;

// One per list entry. 'state', 'pic', 'width', 'height' change under s_dec.mutex (the decode threads write them);
// 'data', 'len', 'png' are set by the main thread before the entry is queued.
enum pfSlotState_t : unsigned char
{
	PF_UNREAD,	// not read yet
	PF_QUEUED,	// read, waiting for or being decoded by a decode thread
	PF_DONE,	// decoded: pic (or nullptr if the decode failed)
	PF_GONE		// taken, dropped, failed or unreadable: LoadJPG / LoadPNG handle it
};

struct pfSlot_t
{
	pfSlotState_t	state = PF_UNREAD;
	bool			png = false;	// PNG (by its listed name), else JPG
	byte*			data = nullptr;	// the file (malloc'd), until a decode thread takes it
	size_t			len = 0;
	byte*			pic = nullptr;	// malloc'd RGBA from R_DecodeJPGThreadSafe / R_DecodePNGThreadSafe
	int				width = 0;
	int				height = 0;
};

struct pfState_t
{
	bool										active = false;	// between Begin and End
	bool										keepOnce = false;	// started at the start of a load: survives that load's renderer flush
	std::string									listPath;
	std::vector<std::string>					list;			// the last load's JPGs and PNGs, in order
	std::unordered_map<std::string, int>		where;			// key -> index in list
	std::vector<pfSlot_t>						slots;			// one per list entry (never resized while decode threads run)
	std::vector<char>							read;			// main thread: entry already read (or given up)
	size_t										aheadBytes = 0;	// main thread: compressed bytes read and not yet taken or dropped
	size_t										readyBytes = 0;	// s_dec.mutex: decoded pixels waiting
	std::vector<std::string>					record;			// this load's JPGs and PNGs, in order
	std::unordered_set<std::string>				recorded;
	int											taken = 0;
	int											readCalls = 0;	// read-ahead calls that read something
	int											failed = 0;		// s_dec.mutex: decodes that failed (LoadJPG/LoadPNG did them)
	int											dropped = 0;	// decoded but never asked for in time
	double										waitSeconds = 0.0;	// main thread waiting for a decode to finish
	std::unordered_set<std::string>				missing;		// names that found no image in this load
	int											missLooked = 0;	// lookups that found nothing
	int											missSkipped = 0;	// lookups not repeated thanks to 'missing'
	double										missSeconds = 0.0;	// time spent in the lookups that found nothing
};

pfState_t s_pf;

// The background decode threads. They live from the first file read in a load until R_ImagePrefetch_End,
// and touch nothing but the slots they are given (no engine calls, no Z_Malloc, no printing).
struct pfDecoder_t
{
	std::mutex					mutex;
	std::condition_variable		wake;	// work queued, or quit
	std::condition_variable		done;	// a slot finished decoding
	std::deque<int>				queue;	// slot indexes waiting for a decode thread
	std::vector<std::thread>	threads;
	bool						quit = false;
};

pfDecoder_t s_dec;

std::string PF_Key(const char* name)
{
	std::string k(name);
	for (char& c : k)
	{
		c = (c == '\\') ? '/' : static_cast<char>(tolower(static_cast<unsigned char>(c)));
	}
	return k;
}

bool PF_Decode(const pfSlot_t& slot, const byte* data, const size_t len, byte** pic, int* width, int* height)
{
	return slot.png ? R_DecodePNGThreadSafe(data, len, pic, width, height) : R_DecodeJPGThreadSafe(data, len, pic, width, height);
}

// s_dec.mutex held: a decode finished
void PF_Finish(pfSlot_t& slot, const bool ok, byte* pic, const int width, const int height)
{
	slot.state = PF_DONE;
	if (ok)
	{
		slot.pic = pic;
		slot.width = width;
		slot.height = height;
		s_pf.readyBytes += static_cast<size_t>(width) * height * 4;
	}
	else
	{
		s_pf.failed++;
	}
}

void PF_DecodeThread(const int number)
{
	char name[32];
	snprintf(name, sizeof(name), "Image decode %d", number + 1);
	PROFILE_THREAD(name);
	(void)name;
	qjobs::WorkerThreadFlag() = true;

	std::unique_lock<std::mutex> lock(s_dec.mutex);
	for (;;)
	{
		s_dec.wake.wait(lock, [] { return s_dec.quit || !s_dec.queue.empty(); });
		if (s_dec.queue.empty())
		{
			return;	// quit (End empties the queue first)
		}
		const int index = s_dec.queue.front();
		s_dec.queue.pop_front();
		pfSlot_t& slot = s_pf.slots[index];
		byte* data = slot.data;
		const size_t len = slot.len;
		slot.data = nullptr;
		lock.unlock();

		byte* pic = nullptr;
		int width = 0;
		int height = 0;
		bool ok;
		{
			PROFILE_ZONE("Load: image prefetch decode (background)");
			ok = PF_Decode(slot, data, len, &pic, &width, &height);
		}
		free(data);

		lock.lock();
		PF_Finish(slot, ok, pic, width, height);
		s_dec.done.notify_all();
	}
}

// main thread: the decode threads for this load (as many as the render job pool has workers, at most 8)
bool PF_StartDecoders()
{
	if (!s_dec.threads.empty())
	{
		return true;
	}
	qjobs::Pool* pool = R_RenderJobPool();
	if (!pool)
	{
		return false;
	}
	const int count = (std::max)(1, (std::min)(pool->NumThreads(), PF_MAX_DECODERS));
	s_dec.quit = false;
	try
	{
		for (int i = 0; i < count; i++)
		{
			s_dec.threads.emplace_back(PF_DecodeThread, i);
		}
	}
	catch (...)
	{
		// could not start a thread: the ones that did start still work; none = decode on the main thread
	}
	return !s_dec.threads.empty();
}

// main thread: stop the decode threads, freeing what was still waiting for one
void PF_StopDecoders()
{
	{
		std::lock_guard<std::mutex> lock(s_dec.mutex);
		for (const int index : s_dec.queue)
		{
			pfSlot_t& slot = s_pf.slots[index];
			free(slot.data);
			slot.data = nullptr;
			slot.state = PF_GONE;
		}
		s_dec.queue.clear();
		s_dec.quit = true;
	}
	s_dec.wake.notify_all();
	for (std::thread& t : s_dec.threads)
	{
		if (t.joinable())
		{
			t.join();
		}
	}
	s_dec.threads.clear();
	s_dec.quit = false;
}

// main thread: free the decoded pixels nobody asked for that sit before 'cursor' (the load has moved on)
void PF_DropBefore(const int cursor)
{
	std::lock_guard<std::mutex> lock(s_dec.mutex);
	for (int i = 0; i < cursor && s_pf.readyBytes > PF_READY_BYTES / 2; i++)
	{
		pfSlot_t& slot = s_pf.slots[i];
		if (slot.state == PF_DONE)
		{
			if (slot.pic)
			{
				s_pf.readyBytes -= static_cast<size_t>(slot.width) * slot.height * 4;
				free(slot.pic);
				slot.pic = nullptr;
				s_pf.dropped++;
			}
			slot.state = PF_GONE;
			s_pf.aheadBytes -= (std::min)(s_pf.aheadBytes, slot.len);
		}
	}
}

// main thread: read list entry 'index' (the normal file system) and hand it to a decode thread
void PF_ReadOne(const int index)
{
	s_pf.read[index] = 1;
	pfSlot_t& slot = s_pf.slots[index];
	void* buf = nullptr;
	const long len = PF_RI FS_ReadFile(s_pf.list[index].c_str(), &buf);
	byte* copy = nullptr;
	if (len > 0 && buf)
	{
		copy = static_cast<byte*>(malloc(static_cast<size_t>(len)));
		if (copy)
		{
			memcpy(copy, buf, static_cast<size_t>(len));
		}
	}
	if (buf)
	{
		PF_RI FS_FreeFile(buf);
	}
	if (!copy)
	{
		std::lock_guard<std::mutex> lock(s_dec.mutex);
		slot.state = PF_GONE;	// gone since the list was written: LoadJPG will see the same
		return;
	}
	const std::string& listed = s_pf.list[index];
	slot.png = listed.size() > 4 && !Q_stricmp(listed.c_str() + listed.size() - 4, ".png");
	slot.len = static_cast<size_t>(len);
	s_pf.aheadBytes += slot.len;

	if (!PF_StartDecoders())
	{
		// no decode thread: decode here (what LoadJPG would have done, just from memory)
		byte* pic = nullptr;
		int width = 0;
		int height = 0;
		const bool ok = PF_Decode(slot, copy, slot.len, &pic, &width, &height);
		free(copy);
		std::lock_guard<std::mutex> lock(s_dec.mutex);
		PF_Finish(slot, ok, pic, width, height);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(s_dec.mutex);
		slot.data = copy;
		slot.state = PF_QUEUED;
		s_dec.queue.push_back(index);
	}
	s_dec.wake.notify_one();
}

// main thread: make sure entry 'cursor' is read, and keep the next listed files read ahead (decoding in the background)
void PF_ReadAhead(const int cursor)
{
	PROFILE_ZONE("Load: image prefetch read");
	size_t ready;
	{
		std::lock_guard<std::mutex> lock(s_dec.mutex);
		ready = s_pf.readyBytes;
	}
	if (ready > PF_READY_BYTES)
	{
		PF_DropBefore(cursor);
		std::lock_guard<std::mutex> lock(s_dec.mutex);
		ready = s_pf.readyBytes;
	}
	bool any = false;
	const int count = static_cast<int>(s_pf.list.size());
	for (int i = cursor; i < count && i < cursor + PF_AHEAD_FILES; i++)
	{
		if (s_pf.read[i])
		{
			continue;
		}
		if (i != cursor && (s_pf.aheadBytes >= PF_AHEAD_BYTES || ready > PF_READY_BYTES))
		{
			break;	// enough waiting: the one asked for is always read
		}
		PF_ReadOne(i);
		any = true;
	}
	if (any)
	{
		s_pf.readCalls++;
	}
}

// pixels for this JPG / PNG if the prefetch has them (waiting for its decode if it is still running), in the same
// kind of memory LoadJPG returns (LoadPNG's is the same kind; only the tag differs)
bool PF_Take(const char* name, byte** pic, int* width, int* height)
{
	if (!s_pf.active || s_pf.list.empty())
	{
		return false;
	}
	const auto w = s_pf.where.find(PF_Key(name));
	if (w == s_pf.where.end())
	{
		return false;	// not listed
	}
	const int index = w->second;
	PF_ReadAhead(index);

	byte* src = nullptr;
	int srcWidth = 0;
	int srcHeight = 0;
	size_t len = 0;
	{
		std::unique_lock<std::mutex> lock(s_dec.mutex);
		pfSlot_t& slot = s_pf.slots[index];
		if (slot.state == PF_QUEUED)
		{
			PROFILE_ZONE("Load: image prefetch wait");
			const auto start = std::chrono::steady_clock::now();
			s_dec.done.wait(lock, [&slot] { return slot.state != PF_QUEUED; });
			s_pf.waitSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		}
		if (slot.state != PF_DONE)
		{
			return false;	// unreadable, dropped, or already taken once (loaded twice)
		}
		src = slot.pic;
		srcWidth = slot.width;
		srcHeight = slot.height;
		len = slot.len;
		slot.pic = nullptr;
		slot.state = PF_GONE;
		if (src)
		{
			s_pf.readyBytes -= static_cast<size_t>(srcWidth) * srcHeight * 4;
		}
	}
	s_pf.aheadBytes -= (std::min)(s_pf.aheadBytes, len);
	if (!src)
	{
		return false;	// the decode failed (an error or warning): LoadJPG / LoadPNG load it and print as before
	}

	const size_t bytes = static_cast<size_t>(srcWidth) * srcHeight * 4;
	byte* out = static_cast<byte*>(PF_ALLOC(static_cast<int>(bytes)));
	memcpy(out, src, bytes);
	free(src);
	*pic = out;
	*width = srcWidth;
	*height = srcHeight;
	s_pf.taken++;
	return true;
}

void PF_Record(const char* name)
{
	if (!s_pf.active || static_cast<int>(s_pf.record.size()) >= PF_MAX_LIST)
	{
		return;
	}
	if (s_pf.recorded.insert(PF_Key(name)).second)
	{
		s_pf.record.emplace_back(name);
	}
}

// R_LoadImage's call to an image loader: JPGs and PNGs go through the prefetch, and during a load a name that
// found no image is not looked for again
void PF_Load(const ImageLoaderFn loader, const char* name, byte** pic, int* width, int* height)
{
	if (!s_pf.active)
	{
		loader(name, pic, width, height);
		return;
	}
	std::string key = PF_Key(name);
	if (s_pf.missing.count(key))
	{
		s_pf.missSkipped++;
		return;
	}
	const bool prefetchable = loader == LoadJPG || loader == LoadPNG;
	const auto start = std::chrono::steady_clock::now();
	if (!prefetchable || !PF_Take(name, pic, width, height))
	{
		loader(name, pic, width, height);
	}
	if (*pic)
	{
		if (prefetchable)
		{
			PF_Record(name);
		}
		return;
	}
	s_pf.missing.insert(std::move(key));
	s_pf.missLooked++;
	s_pf.missSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

void R_ImagePrefetch_End(const qboolean saveList)
{
	PF_StopDecoders();	// first: nothing may touch the slots after this

	// A load that decoded under half of the list found most images already in memory (the same map again, or
	// one sharing its textures): keep the old list, which still covers a load from cold.
	if (s_pf.active && saveList && !s_pf.record.empty() && s_pf.record != s_pf.list && s_pf.record.size() * 2 >= s_pf.list.size())
	{
		std::string text;
		for (const std::string& name : s_pf.record)
		{
			text += name;
			text += '\n';
		}
		PF_RI FS_WriteFile(s_pf.listPath.c_str(), text.data(), static_cast<int>(text.size()));
	}
	if (s_pf.active)
	{
		PF_RI Printf(PRINT_DEVELOPER, "Image prefetch: %d of %d JPGs/PNGs decoded in the background (%d failed and %d unused left to LoadJPG/LoadPNG, %.2f s waited)\n",
			s_pf.taken, static_cast<int>(s_pf.record.size()), s_pf.failed, s_pf.dropped, s_pf.waitSeconds);
		PF_RI Printf(PRINT_DEVELOPER, "Image prefetch: %d image names found no file (%.2f s), %d repeat lookups skipped\n",
			s_pf.missLooked, s_pf.missSeconds, s_pf.missSkipped);
	}
	for (pfSlot_t& slot : s_pf.slots)
	{
		free(slot.pic);
		free(slot.data);
	}
	s_pf = pfState_t();
}

// Called at the start of a map load (RE_RegisterMedia_LevelLoadBegin, loadStart qtrue - SP: before the game spawns
// its entities, whose models load many of the map's images) and again when the world loads (RE_LoadWorldMap - MP
// clients joining a server never get the first call). The second call for the same list keeps the one already
// running; loadStart always starts afresh.
void R_ImagePrefetch_Begin(const char* mapName, const char* rendererTag, const qboolean loadStart)
{
	if (!mapName || !mapName[0] || !R_RenderJobPool())
	{
		R_ImagePrefetch_End(qfalse);
		return;	// r_threads 0: off
	}

	// imagecache/<map without maps/ and .bsp>_<renderer>.txt
	char base[MAX_QPATH];
	COM_StripExtension(mapName, base, sizeof(base));
	const char* b = base;
	if (!Q_stricmpn(b, "maps/", 5) || !Q_stricmpn(b, "maps\\", 5))
	{
		b += 5;
	}
	std::string file(b);
	file = file.substr(0, file.find_first_of("$ "));	// SP "map name$spawntarget": the map name only
	for (char& c : file)
	{
		if (c == '/' || c == '\\' || c == ':')
		{
			c = '_';
		}
	}
	const std::string listPath = "imagecache/" + file + "_" + rendererTag + ".txt";
	if (!loadStart && s_pf.active && s_pf.listPath == listPath)
	{
		return;	// already running for this load
	}
	R_ImagePrefetch_End(qfalse);	// anything left from an earlier load
	s_pf.listPath = listPath;
	s_pf.active = true;
	s_pf.keepOnce = loadStart != qfalse;

	void* buf = nullptr;
	const long len = PF_RI FS_ReadFile(s_pf.listPath.c_str(), &buf);
	if (len > 0 && buf)
	{
		const char* p = static_cast<const char*>(buf);
		const char* end = p + len;
		while (p < end && static_cast<int>(s_pf.list.size()) < PF_MAX_LIST)
		{
			const char* e = p;
			while (e < end && *e != '\n' && *e != '\r')
			{
				e++;
			}
			if (e > p && e - p < MAX_QPATH)
			{
				std::string name(p, e);
				if (s_pf.where.emplace(PF_Key(name.c_str()), static_cast<int>(s_pf.list.size())).second)
				{
					s_pf.list.push_back(std::move(name));
					s_pf.read.push_back(0);
					s_pf.slots.emplace_back();
				}
			}
			p = e + 1;
		}
	}
	if (buf)
	{
		PF_RI FS_FreeFile(buf);
	}
	PF_RI Printf(PRINT_DEVELOPER, "Image prefetch: %d images listed in %s\n", static_cast<int>(s_pf.list.size()), s_pf.listPath.c_str());
}

// RE_Shutdown. A map load shuts the renderer down (CL_MapLoading -> CL_FlushMemory, levelChange = not destroying the
// window, not restarting) just after RE_RegisterMedia_LevelLoadBegin started the prefetch, and the game then spawns
// its entities, loading many of the map's images: keep the prefetch (and the names found missing) for that load, once.
// It holds no renderer memory (malloc'd pixels, file names), and the job pool restarts when next used. Anything else -
// a vid_restart, quitting, a second flush (an error) - frees it as before.
void R_ImagePrefetch_RendererShutdown(const qboolean levelChange)
{
	if (levelChange && s_pf.active && s_pf.keepOnce)
	{
		s_pf.keepOnce = false;
		return;
	}
	R_ImagePrefetch_End(qfalse);
}

/*
=================
Loads any of the supported image types into a cannonical
32 bit format.
=================
*/
void R_LoadImage(const char* shortname, byte** pic, int* width, int* height) {
	PROFILE_ZONE("Load: image read + decode");
	PROFILE_TEXT(shortname);
	*pic = nullptr;
	*width = 0;
	*height = 0;

	// Try loading the image with the original extension (if possible).
	const char* extension = COM_GetExtension(shortname);
	const ImageLoaderMap* imageLoader = FindImageLoader(extension);
	if (imageLoader != nullptr)
	{
		PF_Load(imageLoader->loader, shortname, pic, width, height);
		if (*pic)
		{
			return;
		}
	}

	// Loop through all the image loaders trying to load this image.
	char extensionlessName[MAX_QPATH];
	COM_StripExtension(shortname, extensionlessName, sizeof extensionlessName);
	for (int i = 0; i < numImageLoaders; i++)
	{
		const ImageLoaderMap* tryLoader = &imageLoaders[i];
		if (tryLoader == imageLoader)
		{
			// Already tried this one.
			continue;
		}

		const char* name = va("%s.%s", extensionlessName, tryLoader->extension);
		PF_Load(tryLoader->loader, name, pic, width, height);
		if (*pic)
		{
			return;
		}
	}
}