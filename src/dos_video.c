/*
SDLPoP, a port/conversion of the DOS game Prince of Persia.
Copyright (C) 2013-2025  Dávid Nagy

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/*
 * Screen output for the MS-DOS build (DJGPP + SDL3's DOS port).
 *
 * The other ports draw the 320x200 game screen with an SDL renderer. On DOS
 * there is only the software renderer, and it cannot drive an 8-bit VGA
 * mode, so here the game screen goes straight onto the window surface:
 *
 *   hicolor VESA mode (15/16/24/32 bit): the 320x200 RGB surface is blitted,
 *     scaled by a whole number and centred (or stretched to fill the screen
 *     when use_correct_aspect_ratio is on).
 *   VGA mode 13h (320x200, 256 colours): every frame gets its own palette.
 *     The game seldom shows more than 256 colours at once (it was a 256
 *     colour VGA game), so most frames are exact; when lighting or an
 *     overlay pushes past 256, the 256 most used colours are kept and the
 *     rest map to the nearest of those.
 *
 * Which mode: the PRINCE_VIDEO environment variable (e.g. SET PRINCE_VIDEO=
 * 640x480x16, or 320x200x8 for mode 13h), otherwise 320x200 in the most
 * colours the card has, otherwise the smallest larger hicolor mode,
 * otherwise mode 13h.
 */
#ifdef __DJGPP__

#include "common.h"

static SDL_Rect dos_dest;            // where the 320x200 picture lands on the screen
static bool dos_indexed;

// ---------------------------------------------------------------- mode choice
static int bpp_rank(SDL_PixelFormat f) {
	switch (SDL_BITSPERPIXEL(f)) {
		case 32: return 4;
		case 16: return 3;
		case 15: return 2;
		case 24: return 1;          // 3-byte pixels: slow to convert to
		default: return 0;
	}
}

static const SDL_DisplayMode* dos_pick_mode(void) {
	int count = 0;
	SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(SDL_GetPrimaryDisplay(), &count);
	if (modes == NULL || count == 0) return NULL;

	int want_w = 0, want_h = 0, want_bpp = 0;
	const char* env = getenv("PRINCE_VIDEO");
	if (env != NULL) sscanf(env, "%dx%dx%d", &want_w, &want_h, &want_bpp);

	const SDL_DisplayMode* best = NULL;
	long best_score = -1;
	for (int i = 0; i < count; ++i) {
		const SDL_DisplayMode* m = modes[i];
		if (m->w < 320 || m->h < 200) continue;
		int bpp = SDL_BITSPERPIXEL(m->format);
		bool indexed = SDL_ISPIXELFORMAT_INDEXED(m->format);
		if (indexed && (m->w != 320 || m->h != 200)) continue; // only mode 13h of the 8-bit modes
		long score;
		if (want_w > 0) {
			// As asked, as closely as possible.
			score = 100000 - labs((long)m->w * m->h - (long)want_w * want_h) / 16;
			if (m->w == want_w && m->h == want_h) score += 100000;
			if (want_bpp != 0 && bpp == want_bpp) score += 50000;
			if (want_bpp == 0 && !indexed) score += 1000 + bpp_rank(m->format);
		} else {
			// Hicolor first, then the smallest picture (least to copy), then the most colours.
			score = indexed ? 0 : 1000000;
			score += 100000 - ((long)m->w * m->h) / 16;
			score += bpp_rank(m->format);
		}
		if (score > best_score) { best_score = score; best = m; }
	}
	// The array is SDL's; the modes stay valid until the video subsystem quits.
	static SDL_DisplayMode chosen;
	if (best) chosen = *best;
	SDL_free(modes);
	return best ? &chosen : NULL;
}

// Make the window (the whole screen) in the chosen mode.
SDL_Window* dos_create_window(const char* title) {
	const SDL_DisplayMode* mode = dos_pick_mode();
	int w = mode ? mode->w : 320;
	int h = mode ? mode->h : 200;
	SDL_Window* window = SDL_CreateWindow(title, w, h, SDL_WINDOW_FULLSCREEN);
	if (window == NULL) return NULL;
	if (mode) SDL_SetWindowFullscreenMode(window, mode);
	SDL_SyncWindow(window);
	SDL_Surface* screen = SDL_GetWindowSurface(window);
	if (screen == NULL) {
		printf("dos_create_window: SDL_GetWindowSurface: %s\n", SDL_GetError());
		return window;
	}
	dos_indexed = SDL_ISPIXELFORMAT_INDEXED(screen->format);
	if (dos_indexed) {
		// The window surface SDL gives us has no palette of its own: share the one of the
		// driver's surface behind it, which is the one that goes to the VGA DAC.
		SDL_Surface* internal = (SDL_Surface*)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
		                                                             "SDL.internal.window.surface", NULL);
		SDL_Palette* pal = internal ? SDL_GetSurfacePalette(internal) : NULL;
		if (pal) SDL_SetSurfacePalette(screen, pal);
		else if (!SDL_GetSurfacePalette(screen)) SDL_CreateSurfacePalette(screen);
	}
	printf("Video: %dx%d, %d bits%s\n", screen->w, screen->h, SDL_BITSPERPIXEL(screen->format),
	       dos_indexed ? " (VGA mode 13h, palette per frame)" : "");
	return window;
}

static void dos_layout(SDL_Surface* screen) {
	if (use_correct_aspect_ratio && !dos_indexed && screen->h * 320 >= screen->w * 240) {
		// Fill a 4:3 area, the way a VGA monitor shows 320x200.
		int w = screen->w, h = screen->w * 3 / 4;
		if (h > screen->h) { h = screen->h; w = h * 4 / 3; }
		dos_dest.w = w; dos_dest.h = h;
	} else {
		int scale = SDL_min(screen->w / 320, screen->h / 200);
		if (scale < 1) scale = 1;
		dos_dest.w = 320 * scale; dos_dest.h = 200 * scale;
	}
	dos_dest.x = (screen->w - dos_dest.w) / 2;
	dos_dest.y = (screen->h - dos_dest.h) / 2;
}

// ---------------------------------------------------------------- mode 13h
// Colours (24-bit) -> palette index, open addressing.
#define HASH_SIZE 4096
static Uint32 hash_key[HASH_SIZE];
static Uint16 hash_val[HASH_SIZE];   // index into colours[]
static Uint32 colours[320 * 200];
static Uint32 counts[320 * 200];
static Uint8 cache15[32768];         // 15-bit colour -> nearest index, for frames with too many colours
static Uint8 cache15_valid[32768 / 8];

static inline unsigned hash_rgb(Uint32 c) { return (c * 2654435761u) >> 20; }

static int nearest(const SDL_Color* pal, int n, int r, int g, int b) {
	int best = 0; long best_d = 1L << 30;
	for (int i = 0; i < n; ++i) {
		int dr = r - pal[i].r, dg = g - pal[i].g, db = b - pal[i].b;
		long d = 3L * dr * dr + 4L * dg * dg + 2L * db * db;
		if (d < best_d) { best_d = d; best = i; if (d == 0) break; }
	}
	return best;
}

static void present_indexed(SDL_Surface* src, SDL_Surface* screen) {
	static SDL_Color pal[256];
	static Uint8 index_of[320 * 200]; // per distinct colour

	memset(hash_key, 0xFF, sizeof(hash_key));
	int n = 0;
	const Uint8* row = (const Uint8*)src->pixels;
	static Uint16 slot_of_pixel[320 * 200];
	for (int y = 0; y < 200; ++y, row += src->pitch) {
		const Uint8* p = row;
		Uint32 last_c = 0xFFFFFFFFu; Uint16 last_slot = 0;
		for (int x = 0; x < 320; ++x, p += 3) {
			Uint32 c = (Uint32)p[0] << 16 | (Uint32)p[1] << 8 | p[2]; // RGB24: R, G, B in memory
			if (c == last_c) { ++counts[last_slot]; slot_of_pixel[y * 320 + x] = last_slot; continue; }
			unsigned h = hash_rgb(c);
			while (hash_key[h] != 0xFFFFFFFFu && hash_key[h] != c) h = (h + 1) & (HASH_SIZE - 1);
			if (hash_key[h] == 0xFFFFFFFFu) {
				if (n >= HASH_SIZE * 3 / 4) { // absurdly many colours: stop counting new ones
					h = hash_rgb(colours[0]);
					while (hash_key[h] != colours[0]) h = (h + 1) & (HASH_SIZE - 1);
				} else {
					hash_key[h] = c; hash_val[h] = (Uint16)n; colours[n] = c; counts[n] = 0; ++n;
				}
			}
			last_c = c; last_slot = hash_val[h];
			++counts[last_slot];
			slot_of_pixel[y * 320 + x] = last_slot;
		}
	}

	int ncol;
	if (n <= 256) {
		for (int i = 0; i < n; ++i) {
			pal[i].r = colours[i] >> 16; pal[i].g = colours[i] >> 8; pal[i].b = colours[i]; pal[i].a = 255;
			index_of[i] = (Uint8)i;
		}
		ncol = n;
	} else {
		// Keep the 256 most used colours (partial selection), map the rest.
		static Uint16 order[320 * 200];
		for (int i = 0; i < n; ++i) order[i] = (Uint16)i;
		for (int k = 0; k < 256; ++k) {
			int m = k;
			for (int i = k + 1; i < n; ++i) if (counts[order[i]] > counts[order[m]]) m = i;
			Uint16 t = order[k]; order[k] = order[m]; order[m] = t;
			Uint32 c = colours[order[k]];
			pal[k].r = c >> 16; pal[k].g = c >> 8; pal[k].b = c; pal[k].a = 255;
			index_of[order[k]] = (Uint8)k;
		}
		memset(cache15_valid, 0, sizeof(cache15_valid));
		for (int i = 256; i < n; ++i) {
			Uint32 c = colours[order[i]];
			unsigned k15 = ((c >> 9) & 0x7C00) | ((c >> 6) & 0x03E0) | ((c >> 3) & 0x001F);
			if (!(cache15_valid[k15 >> 3] & (1 << (k15 & 7)))) {
				cache15[k15] = (Uint8)nearest(pal, 256, (c >> 16) & 255, (c >> 8) & 255, c & 255);
				cache15_valid[k15 >> 3] |= (Uint8)(1 << (k15 & 7));
			}
			index_of[order[i]] = cache15[k15];
		}
		ncol = 256;
	}
	// Only touch the palette (and so the VGA DAC, which waits for the vertical retrace) when it changed.
	static SDL_Color last_pal[256];
	static int last_ncol = -1;
	SDL_Palette* screen_pal = SDL_GetSurfacePalette(screen);
	if (screen_pal && (ncol != last_ncol || memcmp(pal, last_pal, ncol * sizeof(SDL_Color)) != 0)) {
		SDL_SetPaletteColors(screen_pal, pal, 0, ncol);
		memcpy(last_pal, pal, ncol * sizeof(SDL_Color));
		last_ncol = ncol;
	}

	Uint8* dst = (Uint8*)screen->pixels;
	for (int y = 0; y < 200; ++y, dst += screen->pitch) {
		const Uint16* s = &slot_of_pixel[y * 320];
		for (int x = 0; x < 320; ++x) dst[x] = index_of[s[x]];
	}
}

// ---------------------------------------------------------------- hicolor
// SDL's general blitter takes ~80 ms a frame to turn RGB24 into 16-bit pixels on
// a Pentium; these loops do the usual screen formats directly, with whole-number
// scaling, in a few ms.
static Uint32 tab_r[256], tab_g[256], tab_b[256];
static SDL_PixelFormat tab_format = SDL_PIXELFORMAT_UNKNOWN;

static bool fast_format(SDL_PixelFormat f) {
	return f == SDL_PIXELFORMAT_RGB565 || f == SDL_PIXELFORMAT_XRGB1555 ||
	       f == SDL_PIXELFORMAT_XRGB8888 || f == SDL_PIXELFORMAT_BGR24 || f == SDL_PIXELFORMAT_RGB24;
}

static void build_tables(SDL_PixelFormat f) {
	if (f == tab_format) return;
	tab_format = f;
	for (int i = 0; i < 256; ++i) {
		switch (f) {
			case SDL_PIXELFORMAT_RGB565:   tab_r[i] = (i >> 3) << 11; tab_g[i] = (i >> 2) << 5; tab_b[i] = i >> 3; break;
			case SDL_PIXELFORMAT_XRGB1555: tab_r[i] = (i >> 3) << 10; tab_g[i] = (i >> 3) << 5; tab_b[i] = i >> 3; break;
			default:                       tab_r[i] = i << 16; tab_g[i] = i << 8; tab_b[i] = i; break;
		}
	}
}

static void present_fast(SDL_Surface* src, SDL_Surface* screen) {
	build_tables(screen->format);
	int scale = dos_dest.w / 320;
	int bpp = SDL_BYTESPERPIXEL(screen->format);
	const Uint8* srow = (const Uint8*)src->pixels;
	Uint8* drow = (Uint8*)screen->pixels + dos_dest.y * screen->pitch + dos_dest.x * bpp;
	int line_bytes = 320 * scale * bpp;
	for (int y = 0; y < 200; ++y, srow += src->pitch) {
		const Uint8* p = srow;
		Uint8* d = drow;
		switch (screen->format) {
			case SDL_PIXELFORMAT_RGB565:
			case SDL_PIXELFORMAT_XRGB1555: {
				Uint16* o = (Uint16*)d;
				for (int x = 0; x < 320; ++x, p += 3) {
					Uint16 v = (Uint16)(tab_r[p[0]] | tab_g[p[1]] | tab_b[p[2]]);
					for (int k = 0; k < scale; ++k) *o++ = v;
				}
			} break;
			case SDL_PIXELFORMAT_XRGB8888: {
				Uint32* o = (Uint32*)d;
				for (int x = 0; x < 320; ++x, p += 3) {
					Uint32 v = tab_r[p[0]] | tab_g[p[1]] | tab_b[p[2]];
					for (int k = 0; k < scale; ++k) *o++ = v;
				}
			} break;
			case SDL_PIXELFORMAT_BGR24: // B, G, R in memory
				for (int x = 0; x < 320; ++x, p += 3) {
					for (int k = 0; k < scale; ++k) { d[0] = p[2]; d[1] = p[1]; d[2] = p[0]; d += 3; }
				}
				break;
			default: // RGB24, same as the game screen
				if (scale == 1) memcpy(d, p, 320 * 3);
				else for (int x = 0; x < 320; ++x, p += 3)
					for (int k = 0; k < scale; ++k) { d[0] = p[0]; d[1] = p[1]; d[2] = p[2]; d += 3; }
				break;
		}
		for (int k = 1; k < scale; ++k) memcpy(drow + k * screen->pitch, drow, line_bytes);
		drow += scale * screen->pitch;
	}
}

// ---------------------------------------------------------------- present
void dos_present(SDL_Window* window, SDL_Surface* src) {
	SDL_Surface* screen = SDL_GetWindowSurface(window);
	if (screen == NULL) return;
	if (dos_indexed && screen->w == 320 && screen->h == 200 && src->format == SDL_PIXELFORMAT_RGB24) {
		dos_dest.x = dos_dest.y = 0; dos_dest.w = 320; dos_dest.h = 200;
		present_indexed(src, screen);
	} else {
		static int last_w, last_h, last_aspect = -1;
		if (screen->w != last_w || screen->h != last_h || last_aspect != use_correct_aspect_ratio) {
			last_w = screen->w; last_h = screen->h; last_aspect = use_correct_aspect_ratio;
			SDL_FillSurfaceRect(screen, NULL, 0);
			dos_layout(screen);
		}
		SDL_SetSurfaceBlendMode(src, SDL_BLENDMODE_NONE);
		bool whole_scale = dos_dest.w % 320 == 0 && dos_dest.h % 200 == 0 && dos_dest.w / 320 == dos_dest.h / 200;
		if (whole_scale && src->format == SDL_PIXELFORMAT_RGB24 && fast_format(screen->format)) {
			present_fast(src, screen);
		} else if (dos_dest.w == 320 && dos_dest.h == 200) {
			SDL_BlitSurface(src, NULL, screen, &dos_dest);
		} else {
			SDL_BlitSurfaceScaled(src, NULL, screen, &dos_dest, SDL_SCALEMODE_NEAREST);
		}
	}
	SDL_UpdateWindowSurface(window);
}

// Mouse position (window pixels) -> game screen (0..319, 0..199).
void dos_window_to_game(float wx, float wy, int* gx, int* gy) {
	if (dos_dest.w <= 0 || dos_dest.h <= 0) { *gx = (int)wx; *gy = (int)wy; return; }
	*gx = (int)((wx - dos_dest.x) * 320 / dos_dest.w);
	*gy = (int)((wy - dos_dest.y) * 200 / dos_dest.h);
}

#endif // __DJGPP__
