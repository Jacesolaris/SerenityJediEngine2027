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
#define RD_PNG_ALLOC_PIXELS(n)	R_Malloc(n, TAG_TEMP_PNG, qfalse)
#define RD_PNG_FREE_PIXELS(p)	R_Free(p)
#define RD_PNG_ALLOC_ROWS(n)	R_Malloc(n, TAG_TEMP_PNG, qfalse)
#define RD_PNG_FREE_ROWS(p)	R_Free(p)
#else	// MP renderers
#define RD_PNG_ALLOC_PIXELS(n)	ri.Z_Malloc(n, TAG_TEMP_PNG, qfalse, 4)
#define RD_PNG_FREE_PIXELS(p)	ri.Z_Free(p)
#define RD_PNG_ALLOC_ROWS(n)	ri.Hunk_AllocateTempMemory(n)
#define RD_PNG_FREE_ROWS(p)	ri.Hunk_FreeTempMemory(p)
#endif
#include "q_profile.h"

#include "rd-common/tr_common.h"
#include <png.h>
#include <qcommon\q_shared.h>
#include "rd-common/tr_public.h"
#include <pngconf.h>
#include <qcommon\q_platform.h>
#include <csetjmp>
#include <cstdint>
#include <string.h>
#include <pnglibconf.h>

static void user_write_data(const png_structp png_ptr, const png_bytep data, const png_size_t length) {
	const fileHandle_t fp = *static_cast<fileHandle_t*>(png_get_io_ptr(png_ptr));
	ri.FS_Write(data, length, fp);
}
static void user_flush_data(png_structp png_ptr)
{
	//TODO: ri->FS_Flush?
}

int RE_SavePNG(const char* filename, const byte* buf, const size_t width, const size_t height, const int byte_depth) {
	fileHandle_t fp = 0;
	png_structp png_ptr = nullptr;
	png_infop info_ptr = nullptr;
	unsigned int y = 0;
	/* "status" contains the return value of this function. At first
	it is set to a value which means 'failure'. When the routine
	has finished its work, it is set to a value which means
	'success'. */
	int status = -1;
	/* The following number is set by trial and error only. I cannot
	see where it it is documented in the libpng manual.
	*/
	constexpr int depth = 8;

	fp = ri.FS_FOpenFileWrite(filename, qtrue);
	if (!fp) {
		goto fopen_failed;
	}

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (png_ptr == nullptr) {
		goto png_create_write_struct_failed;
	}

	info_ptr = png_create_info_struct(png_ptr);
	if (info_ptr == nullptr) {
		goto png_create_info_struct_failed;
	}

	/* Set up error handling. */

	if (setjmp(png_jmpbuf(png_ptr))) {
		goto png_failure;
	}

	/* Set image attributes. */

	png_set_IHDR(png_ptr,
		info_ptr,
		width,
		height,
		depth,
		PNG_COLOR_TYPE_RGB,
		PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT,
		PNG_FILTER_TYPE_DEFAULT);

	/* Initialize rows of PNG. */

	const auto row_pointers = static_cast<png_byte**>(png_malloc(png_ptr, height * sizeof(png_byte*)));
	for (y = 0; y < height; ++y) {
		auto row = static_cast<png_byte*>(png_malloc(png_ptr, sizeof(uint8_t) * width * byte_depth));
		row_pointers[height - y - 1] = row;
		for (unsigned int x = 0; x < width; ++x) {
			const byte* px = buf + (width * y + x) * 3;
			*row++ = px[0];
			*row++ = px[1];
			*row++ = px[2];
		}
	}

	/* Write the image data to "fp". */

//	png_init_io (png_ptr, fp);
	png_set_write_fn(png_ptr, &fp, user_write_data, user_flush_data);
	png_set_rows(png_ptr, info_ptr, row_pointers);
	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, nullptr);

	/* The routine has successfully written the file, so we set
	"status" to a value which indicates success. */

	status = 0;

	for (y = 0; y < height; y++) {
		png_free(png_ptr, row_pointers[y]);
	}
	png_free(png_ptr, row_pointers);

png_failure:
png_create_info_struct_failed:
	png_destroy_write_struct(&png_ptr, &info_ptr);
png_create_write_struct_failed:
	ri.FS_FCloseFile(fp);
fopen_failed:
	return status;
}

void user_read_data(png_structp png_ptr, png_bytep data, png_size_t length);
static void png_print_error(png_structp png_ptr, const png_const_charp err)
{
	ri.Printf(PRINT_ERROR, "%s\n", err);
}

static void png_print_warning(png_structp png_ptr, const png_const_charp warning)
{
	ri.Printf(PRINT_WARNING, "%s\n", warning);
}

static bool IsPowerOfTwo(const int i) { return (i & i - 1) == 0; }

struct PNGFileReader
{
	PNGFileReader(char* buf, const size_t size) : buf(buf), size(size), offset(0), png_ptr(nullptr), info_ptr(nullptr) {}
	~PNGFileReader()
	{
		ri.FS_FreeFile(buf);

		if (png_ptr != nullptr)
		{
			// Frees the read struct and (if created) the info struct.
			// png_destroy_info_struct alone leaked the read struct for every PNG loaded.
			png_destroy_read_struct(&png_ptr, info_ptr != nullptr ? &info_ptr : nullptr, nullptr);
		}
	}

	int read(byte** data, int* width, int* height)
	{
		// Setup the pointers
		*data = nullptr;
		*width = 0;
		*height = 0;

		// Make sure we're actually reading PNG data.
		constexpr int signature_len = 8;

		if (size < signature_len)
		{
			ri.Printf(PRINT_ERROR, "PNG file is too small to be valid.\n");
			return 0;
		}

		byte ident[signature_len];
		memcpy(ident, buf, signature_len);

		if (!png_check_sig(ident, signature_len))
		{
			ri.Printf(PRINT_ERROR, "PNG signature not found in given image.");
			return 0;
		}

		png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, png_print_error, png_print_warning);
		if (png_ptr == nullptr)
		{
			ri.Printf(PRINT_ERROR, "Could not allocate enough memory to load the image.");
			return 0;
		}

		info_ptr = png_create_info_struct(png_ptr);
		if (setjmp(png_jmpbuf(png_ptr)))
		{
			return 0;
		}

		// We've read the signature
		offset += signature_len;

		// Setup reading information, and read header
		png_set_read_fn(png_ptr, this, &user_read_data);
#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
		// This generic "ignore all, except required chunks" requires 1.6.0 or newer"
		png_set_keep_unknown_chunks(png_ptr, PNG_HANDLE_CHUNK_NEVER, nullptr, -1);
#endif
		png_set_sig_bytes(png_ptr, signature_len);
		png_read_info(png_ptr, info_ptr);

		png_uint_32 width32 = 0;
		png_uint_32 height32 = 0;
		int depth = 0;
		int colortype = 0;

		png_get_IHDR(png_ptr, info_ptr, &width32, &height32, &depth, &colortype, nullptr, nullptr, nullptr);

		// While modern OpenGL can handle non-PoT textures, it's faster to handle only PoT
		// so that the graphics driver doesn't have to fiddle about with the texture when uploading.
		if (!IsPowerOfTwo(width32) || !IsPowerOfTwo(height32))
		{
			ri.Printf(PRINT_ERROR, "Width or height is not a power-of-two.\n");
			return 0;
		}

		// This function is equivalent to using what used to be LoadPNG32. LoadPNG8 also existed,
		// but this only seemed to be used by the RMG system which does not work in JKA. If this
		// does need to be re-implemented, then colortype should be PNG_COLOR_TYPE_PALETTE or
		// PNG_COLOR_TYPE_GRAY.
		if (colortype != PNG_COLOR_TYPE_RGB && colortype != PNG_COLOR_TYPE_RGBA)
		{
			ri.Printf(PRINT_ERROR, "Image is not 24-bit or 32-bit.");
			return 0;
		}

		// Read the png data
		if (colortype == PNG_COLOR_TYPE_RGB)
		{
			// Expand RGB -> RGBA
			png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);
		}

		// interlaced PNGs: let libpng combine the passes (stops the 'Interlace handling should be turned on' warning)
		// 16-bit PNGs: 8 bits per channel. The buffer below is 4 bytes a pixel, so a 16-bit file (8 bytes a pixel)
		// was written past its end - heap corruption.
		if (depth == 16)
		{
			png_set_strip_16(png_ptr);
		}
		png_set_interlace_handling(png_ptr);

		png_read_update_info(png_ptr, info_ptr);

		// We always assume there are 4 channels. RGB channels are expanded to RGBA when read.
		const auto temp_data = static_cast<byte*>(RD_PNG_ALLOC_PIXELS(width32 * height32 * 4));
		if (!temp_data)
		{
			ri.Printf(PRINT_ERROR, "Could not allocate enough memory to load the image.");
			return 0;
		}

		// Dynamic array of row pointers, with 'height' elements, initialized to NULL.
		const auto row_pointers = static_cast<byte**>(RD_PNG_ALLOC_ROWS(sizeof(byte*) * height32));
		if (!row_pointers)
		{
			ri.Printf(PRINT_ERROR, "Could not allocate enough memory to load the image.");

			RD_PNG_FREE_PIXELS(temp_data);

			return 0;
		}

		// Re-set the jmp so that these new memory allocations can be reclaimed
		if (setjmp(png_jmpbuf(png_ptr)))
		{
			RD_PNG_FREE_ROWS(row_pointers);
			RD_PNG_FREE_PIXELS(temp_data);
			return 0;
		}

		for (unsigned int i = 0, j = 0; i < height32; i++, j += 4)
		{
			row_pointers[i] = temp_data + j * width32;
		}

		png_read_image(png_ptr, row_pointers);

		// Finish reading
		png_read_end(png_ptr, nullptr);

		RD_PNG_FREE_ROWS(row_pointers);

		// Finally assign all the parameters
		*data = temp_data;
		*width = width32;
		*height = height32;

		return 1;
	}

	// Returns false (and copies nothing) if the request runs past the end of the file.
	bool ReadBytes(void* dest, const size_t len)
	{
		if (len > size - offset)
		{
			return false;
		}
		memcpy(dest, buf + offset, len);
		offset += len;
		return true;
	}

private:
	char* buf;
	size_t size;
	size_t offset;
	png_structp png_ptr;
	png_infop info_ptr;
};

void user_read_data(const png_structp png_ptr, const png_bytep data, const png_size_t length) {
	const png_voidp r = png_get_io_ptr(png_ptr);
	const auto reader = static_cast<PNGFileReader*>(r);
	if (!reader->ReadBytes(data, length))
	{
		// Truncated/corrupt file: png_error does not return, it longjmps to read()'s error handler.
		png_error(png_ptr, "PNG data ends unexpectedly");
	}
}

// Loads a PNG image from file.
void LoadPNG(const char* filename, byte** data, int* width, int* height)
{
	char* buf = nullptr;
	const int len = ri.FS_ReadFile(filename, reinterpret_cast<void**>(&buf));
	if (len < 0 || buf == nullptr)
	{
		return;
	}
	PROFILE_ZONE("Load: decode PNG");	// after the read: a name with no file is not a decode

	PNGFileReader reader(buf, static_cast<size_t>(len));
	reader.read(data, width, height);
}

/*
=================
R_DecodePNGThreadSafe

The image prefetch's PNG decode (tr_image_load.cpp), safe on the render job workers: the same libpng calls and
transforms as PNGFileReader::read above (so the same RGBA pixels), but from memory, into malloc'd memory, and
silent - nothing here touches the engine (no prints, no Z_Malloc/Hunk memory, no file system). Any error, any
libpng warning or anything read() would refuse returns false, and LoadPNG then loads that file the normal way
(printing exactly as before). libpng keeps all its state in the png_struct, so one decode per thread is safe.
=================
*/
namespace {
	struct pngMemSource_t
	{
		const byte* data;
		size_t size;
		size_t offset;
		bool warned;
	};

	void PNG_TS_Read(const png_structp png_ptr, const png_bytep out, const png_size_t length)
	{
		auto* src = static_cast<pngMemSource_t*>(png_get_io_ptr(png_ptr));
		if (length > src->size - src->offset)
		{
			png_error(png_ptr, "PNG data ends unexpectedly");	// longjmps to the decode's error handler
		}
		memcpy(out, src->data + src->offset, length);
		src->offset += length;
	}

	void PNG_TS_Error(const png_structp png_ptr, png_const_charp)
	{
		// silent: straight back to the setjmp in R_DecodePNGThreadSafe (if this returned, libpng would print to stderr)
		png_longjmp(png_ptr, 1);
	}

	void PNG_TS_Warning(const png_structp png_ptr, png_const_charp)
	{
		static_cast<pngMemSource_t*>(png_get_error_ptr(png_ptr))->warned = true;	// LoadPNG would print it: let it
	}
}

bool R_DecodePNGThreadSafe(const byte* data, const size_t len, byte** pic, int* width, int* height)
{
	*pic = nullptr;
	*width = 0;
	*height = 0;

	constexpr int signature_len = 8;
	if (!data || len < signature_len || png_sig_cmp(const_cast<png_bytep>(data), 0, signature_len) != 0)
	{
		return false;
	}

	pngMemSource_t src = { data, len, signature_len, false };
	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, &src, PNG_TS_Error, PNG_TS_Warning);
	if (!png_ptr)
	{
		return false;
	}
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr)
	{
		png_destroy_read_struct(&png_ptr, nullptr, nullptr);
		return false;
	}

	byte* volatile pixels = nullptr;
	png_bytep* volatile rows = nullptr;
	if (setjmp(png_jmpbuf(png_ptr)))
	{
		free(rows);
		free(pixels);
		png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
		return false;
	}

	png_set_read_fn(png_ptr, &src, PNG_TS_Read);
#ifdef PNG_HANDLE_AS_UNKNOWN_SUPPORTED
	png_set_keep_unknown_chunks(png_ptr, PNG_HANDLE_CHUNK_NEVER, nullptr, -1);
#endif
	png_set_sig_bytes(png_ptr, signature_len);
	png_read_info(png_ptr, info_ptr);

	png_uint_32 width32 = 0;
	png_uint_32 height32 = 0;
	int depth = 0;
	int colortype = 0;
	png_get_IHDR(png_ptr, info_ptr, &width32, &height32, &depth, &colortype, nullptr, nullptr, nullptr);

	// the same checks as read(): power-of-two sizes (as an int, like IsPowerOfTwo), 24 or 32 bit only
	const int w = static_cast<int>(width32);
	const int h = static_cast<int>(height32);
	if ((w & w - 1) != 0 || (h & h - 1) != 0 || (colortype != PNG_COLOR_TYPE_RGB && colortype != PNG_COLOR_TYPE_RGBA))
	{
		png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
		return false;
	}
	if (colortype == PNG_COLOR_TYPE_RGB)
	{
		png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);
	}
	if (depth == 16)
	{
		png_set_strip_16(png_ptr);	// as read(): 8 bits per channel (16-bit PNGs used to overflow the pixel buffer)
	}
	png_set_interlace_handling(png_ptr);
	png_read_update_info(png_ptr, info_ptr);
	if (png_get_rowbytes(png_ptr, info_ptr) != static_cast<size_t>(width32) * 4)
	{
		png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
		return false;
	}

	pixels = static_cast<byte*>(malloc(static_cast<size_t>(width32 * height32 * 4)));
	rows = static_cast<png_bytep*>(malloc(sizeof(png_bytep) * (height32 ? height32 : 1)));
	if (!pixels || !rows)
	{
		free(rows);
		free(pixels);
		png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
		return false;
	}
	for (unsigned int i = 0, j = 0; i < height32; i++, j += 4)
	{
		rows[i] = pixels + j * width32;
	}
	png_read_image(png_ptr, rows);
	png_read_end(png_ptr, nullptr);
	free(rows);
	rows = nullptr;
	png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);

	if (src.warned)
	{
		free(pixels);
		return false;
	}
	*pic = pixels;
	*width = w;
	*height = h;
	return true;
}