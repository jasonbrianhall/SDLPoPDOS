/*
SDLPoP, a port/conversion of the DOS game Prince of Persia.
Copyright (C) 2013-2025  Dávid Nagy

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

/*
 * sdl2_compat.h -- the slice of the SDL2 API that SDLPoP uses, on top of SDL3.
 *
 * SDL3 runs on MS-DOS (DJGPP, 32-bit DPMI: VESA/VGA video, Sound Blaster,
 * keyboard, mouse, gameport), SDL2 does not. Rather than rewrite every call
 * in the game, the SDL2 names are mapped here; the few places where the
 * semantics differ (events, renderer setup, audio device, surface formats)
 * were changed in the game code itself.
 *
 * Every SDL2 function that returned 0 on success returns an int 0 here too,
 * so the game's "!= 0 means error" checks keep working.
 */
#ifndef SDL2_COMPAT_H
#define SDL2_COMPAT_H

/* Our own SDL2 names below, not SDL3's "renamed" error markers. */
#define SDL_oldnames_h_
#include <SDL3/SDL.h>
#include <string.h>

/* ---------------------------------------------------------------- basics */
#define SDL_TRUE  true
#define SDL_FALSE false
#define SDL_ENABLE  1
#define SDL_DISABLE 0
#define SDL_bool bool
#define SDL_INIT_GAMECONTROLLER SDL_INIT_GAMEPAD
#define SDL_INIT_TIMER 0
#define SDL_INIT_NOPARACHUTE 0
#define SDL_NUM_SCANCODES SDL_SCANCODE_COUNT
#define KMOD_SHIFT SDL_KMOD_SHIFT
#define KMOD_CTRL  SDL_KMOD_CTRL
#define KMOD_ALT   SDL_KMOD_ALT

static inline int SDL2_rc(bool ok) { return ok ? 0 : -1; }

#define SDL_Init(f)            SDL2_rc(SDL_Init(f))
#define SDL_InitSubSystem(f)   SDL2_rc(SDL_InitSubSystem(f))

typedef struct SDL_version { Uint8 major, minor, patch; } SDL_version;
#undef SDL_VERSION
#define SDL_VERSION(v) do { (v)->major = SDL_MAJOR_VERSION; (v)->minor = SDL_MINOR_VERSION; (v)->patch = SDL_MICRO_VERSION; } while (0)
static inline void SDL2_GetVersion(SDL_version* v) {
	int n = SDL_GetVersion();
	v->major = (Uint8)SDL_VERSIONNUM_MAJOR(n);
	v->minor = (Uint8)SDL_VERSIONNUM_MINOR(n);
	v->patch = (Uint8)SDL_VERSIONNUM_MICRO(n);
}
#define SDL_GetVersion SDL2_GetVersion
#undef SDL_VERSION_ATLEAST
#define SDL_VERSION_ATLEAST(X, Y, Z) 1

/* ---------------------------------------------------------------- surfaces */
#define SDL_SRCCOLORKEY true
#define SDL_FreeSurface SDL_DestroySurface
#define SDL_SetColorKey(s, f, k)          SDL2_rc(SDL_SetSurfaceColorKey(s, f, k))
#define SDL_SetSurfaceAlphaMod(s, a)      SDL2_rc(SDL_SetSurfaceAlphaMod(s, a))
#define SDL_SetSurfaceBlendMode(s, m)     SDL2_rc(SDL_SetSurfaceBlendMode(s, m))
#define SDL_SetPaletteColors(p, c, f, n)  SDL2_rc(SDL_SetPaletteColors(p, c, f, n))
#define SDL_SetSurfacePalette(s, p)       SDL2_rc(SDL_SetSurfacePalette(s, p))
#define SDL_LockSurface(s)                SDL2_rc(SDL_LockSurface(s))
#define SDL_BlitSurface(s, sr, d, dr)     SDL2_rc(SDL_BlitSurface(s, sr, d, dr))
#define SDL_BlitScaled(s, sr, d, dr)      SDL2_rc(SDL_BlitSurfaceScaled(s, sr, d, dr, SDL_SCALEMODE_NEAREST))
#define SDL_FillRect(s, r, c)             SDL2_rc(SDL_FillSurfaceRect(s, r, c))
#define SDL_ConvertSurfaceFormat(s, f, flags) SDL_ConvertSurface(s, f)

/* SDL_CreateRGBSurface(flags, w, h, depth, Rmask, Gmask, Bmask, Amask) */
static inline SDL_Surface* SDL_CreateRGBSurface(Uint32 flags, int w, int h, int depth,
                                                Uint32 r, Uint32 g, Uint32 b, Uint32 a) {
	(void)flags;
	SDL_PixelFormat fmt = (depth == 8) ? SDL_PIXELFORMAT_INDEX8 : SDL_GetPixelFormatForMasks(depth, r, g, b, a);
	if (fmt == SDL_PIXELFORMAT_UNKNOWN) {
		/* SDL2 accepted all-zero masks and picked a default. */
		fmt = depth == 24 ? SDL_PIXELFORMAT_RGB24 : SDL_PIXELFORMAT_XRGB8888;
	}
	SDL_Surface* s = SDL_CreateSurface(w, h, fmt);
	if (s && SDL_ISPIXELFORMAT_INDEXED(fmt)) SDL_CreateSurfacePalette(s);
	return s;
}

/* Surface format helpers: in SDL2 surface->format was a struct pointer. */
#define SDL_MapRGB_S(surf, r, g, b)      SDL_MapSurfaceRGB(surf, r, g, b)
#define SDL_MapRGBA_S(surf, r, g, b, a)  SDL_MapSurfaceRGBA(surf, r, g, b, a)
#define SURFACE_PALETTE(surf)            SDL_GetSurfacePalette(surf)
#define SURFACE_BPP(surf)                SDL_BITSPERPIXEL((surf)->format)
#define SURFACE_BYTESPP(surf)            SDL_BYTESPERPIXEL((surf)->format)

/* ---------------------------------------------------------------- PNG (SDL_image) */
#define IMG_Load(file)         SDL_LoadPNG(file)
#define IMG_Load_RW(rw, close) SDL_LoadPNG_IO(rw, close)
#define IMG_SavePNG(s, file)   SDL2_rc(SDL_SavePNG(s, file))
#define IMG_GetError           SDL_GetError

/* ---------------------------------------------------------------- I/O */
#define SDL_RWops SDL_IOStream
#define SDL_RWFromConstMem(p, n) SDL_IOFromConstMem(p, (size_t)(n))
#define SDL_RWFromMem(p, n)      SDL_IOFromMem(p, (size_t)(n))
#define SDL_RWFromFile           SDL_IOFromFile
#define SDL_RWtell               SDL_TellIO
#define SDL_RWclose(rw)          SDL2_rc(SDL_CloseIO(rw))
static inline size_t SDL_RWread(SDL_IOStream* rw, void* p, size_t size, size_t n) {
	if (size == 0 || n == 0) return 0;
	return SDL_ReadIO(rw, p, size * n) / size;
}
static inline size_t SDL_RWwrite(SDL_IOStream* rw, const void* p, size_t size, size_t n) {
	if (size == 0 || n == 0) return 0;
	return SDL_WriteIO(rw, p, size * n) / size;
}

/* ---------------------------------------------------------------- events */
#define SDL_KEYDOWN                SDL_EVENT_KEY_DOWN
#define SDL_KEYUP                  SDL_EVENT_KEY_UP
#define SDL_TEXTINPUT              SDL_EVENT_TEXT_INPUT
#define SDL_USEREVENT              SDL_EVENT_USER
#define SDL_QUIT                   SDL_EVENT_QUIT
#define SDL_MOUSEBUTTONDOWN        SDL_EVENT_MOUSE_BUTTON_DOWN
#define SDL_MOUSEWHEEL             SDL_EVENT_MOUSE_WHEEL
#define SDL_JOYAXISMOTION          SDL_EVENT_JOYSTICK_AXIS_MOTION
#define SDL_JOYBUTTONDOWN          SDL_EVENT_JOYSTICK_BUTTON_DOWN
#define SDL_JOYBUTTONUP            SDL_EVENT_JOYSTICK_BUTTON_UP
#define SDL_CONTROLLERAXISMOTION   SDL_EVENT_GAMEPAD_AXIS_MOTION
#define SDL_CONTROLLERBUTTONDOWN   SDL_EVENT_GAMEPAD_BUTTON_DOWN
#define SDL_CONTROLLERBUTTONUP     SDL_EVENT_GAMEPAD_BUTTON_UP
#define SDL_CONTROLLERDEVICEADDED  SDL_EVENT_GAMEPAD_ADDED
#define SDL_CONTROLLERDEVICEREMOVED SDL_EVENT_GAMEPAD_REMOVED
static inline int SDL2_PushEvent(SDL_Event* e) { return SDL_PushEvent(e) ? 1 : 0; }
static inline int SDL2_PollEvent(SDL_Event* e) { return SDL_PollEvent(e) ? 1 : 0; }
#define SDL_PushEvent SDL2_PushEvent
#define SDL_PollEvent SDL2_PollEvent
#define SDL_GetKeyboardState(n)    ((const Uint8*)SDL_GetKeyboardState(n))

/* Text input is switched on once, when the window is made (SDL2 had it on
 * by default; the game relies on it for '+' and '-'). */
#define SDL_StopTextInput()        ((void)0)
#define SDL_SetTextInputRect(r)    ((void)(r))

/* ---------------------------------------------------------------- game controllers */
#define SDL_GameController                 SDL_Gamepad
#define SDL_GameControllerOpen             SDL_OpenGamepad
#define SDL_GameControllerClose            SDL_CloseGamepad
#define SDL_GameControllerFromInstanceID   SDL_GetGamepadFromID
#define SDL_GameControllerAddMappingsFromFile SDL_AddGamepadMappingsFromFile
#define SDL_GameControllerRumble           SDL_RumbleGamepad
#define SDL_JoystickOpen                   SDL_OpenJoystick
#define SDL_JoystickRumble                 SDL_RumbleJoystick
#define SDL_CONTROLLER_AXIS_LEFTX          SDL_GAMEPAD_AXIS_LEFTX
#define SDL_CONTROLLER_AXIS_LEFTY          SDL_GAMEPAD_AXIS_LEFTY
#define SDL_CONTROLLER_AXIS_RIGHTX         SDL_GAMEPAD_AXIS_RIGHTX
#define SDL_CONTROLLER_AXIS_RIGHTY         SDL_GAMEPAD_AXIS_RIGHTY
#define SDL_CONTROLLER_AXIS_TRIGGERLEFT    SDL_GAMEPAD_AXIS_LEFT_TRIGGER
#define SDL_CONTROLLER_AXIS_TRIGGERRIGHT   SDL_GAMEPAD_AXIS_RIGHT_TRIGGER
#define SDL_CONTROLLER_BUTTON_A            SDL_GAMEPAD_BUTTON_SOUTH
#define SDL_CONTROLLER_BUTTON_B            SDL_GAMEPAD_BUTTON_EAST
#define SDL_CONTROLLER_BUTTON_X            SDL_GAMEPAD_BUTTON_WEST
#define SDL_CONTROLLER_BUTTON_Y            SDL_GAMEPAD_BUTTON_NORTH
#define SDL_CONTROLLER_BUTTON_BACK         SDL_GAMEPAD_BUTTON_BACK
#define SDL_CONTROLLER_BUTTON_START        SDL_GAMEPAD_BUTTON_START
#define SDL_CONTROLLER_BUTTON_DPAD_UP      SDL_GAMEPAD_BUTTON_DPAD_UP
#define SDL_CONTROLLER_BUTTON_DPAD_DOWN    SDL_GAMEPAD_BUTTON_DPAD_DOWN
#define SDL_CONTROLLER_BUTTON_DPAD_LEFT    SDL_GAMEPAD_BUTTON_DPAD_LEFT
#define SDL_CONTROLLER_BUTTON_DPAD_RIGHT   SDL_GAMEPAD_BUTTON_DPAD_RIGHT
static inline int SDL_NumJoysticks(void) {
	int n = 0;
	SDL_JoystickID* ids = SDL_GetJoysticks(&n);
	SDL_free(ids);
	return n;
}
/* SDL2 took device indices; SDL3 takes instance IDs. */
static inline SDL_JoystickID SDL2_joystick_id(int index) {
	int n = 0;
	SDL_JoystickID* ids = SDL_GetJoysticks(&n);
	SDL_JoystickID id = (ids && index < n) ? ids[index] : 0;
	SDL_free(ids);
	return id;
}
#define SDL_IsGameController(i)    SDL_IsGamepad(SDL2_joystick_id(i))
#define SDL_HapticOpen(i)          SDL_OpenHaptic(SDL2_haptic_id(i))
#define SDL_HapticRumbleInit       SDL_InitHapticRumble
#define SDL_HapticRumblePlay       SDL_PlayHapticRumble
static inline SDL_HapticID SDL2_haptic_id(int index) {
	int n = 0;
	SDL_HapticID* ids = SDL_GetHaptics(&n);
	SDL_HapticID id = (ids && index < n) ? ids[index] : 0;
	SDL_free(ids);
	return id;
}

/* ---------------------------------------------------------------- window / renderer */
#define SDL_WINDOW_FULLSCREEN_DESKTOP SDL_WINDOW_FULLSCREEN
#define SDL_WINDOW_ALLOW_HIGHDPI      SDL_WINDOW_HIGH_PIXEL_DENSITY
#define SDL_SetWindowFullscreen(w, f) SDL2_rc(SDL_SetWindowFullscreen(w, (f) != 0))
static inline int SDL2_ShowCursor(int on) { return (on ? SDL_ShowCursor() : SDL_HideCursor()) ? on : -1; }
#define SDL_ShowCursor SDL2_ShowCursor
#define SDL_UpdateTexture(t, r, p, pitch) SDL2_rc(SDL_UpdateTexture(t, r, p, pitch))
#define SDL_SetRenderTarget(r, t)     SDL2_rc(SDL_SetRenderTarget(r, t))
#define SDL_RenderClear(r)            SDL2_rc(SDL_RenderClear(r))
#define SDL_RenderCopy(r, t, s, d)    SDL2_rc(SDL_RenderTexture(r, t, NULL, NULL))
#define SDL_RenderPresent(r)          ((void)SDL_RenderPresent(r))

/* SDL2 kept "logical size" and "integer scale" as two settings; SDL3 has one
 * presentation mode. Remember both and apply them together. */
extern int sdl2compat_logical_w, sdl2compat_logical_h;
extern bool sdl2compat_integer_scale;
static inline void SDL2_apply_presentation(SDL_Renderer* r) {
	if (sdl2compat_logical_w <= 0) return;
	SDL_SetRenderLogicalPresentation(r, sdl2compat_logical_w, sdl2compat_logical_h,
		sdl2compat_integer_scale ? SDL_LOGICAL_PRESENTATION_INTEGER_SCALE : SDL_LOGICAL_PRESENTATION_LETTERBOX);
}
static inline int SDL_RenderSetLogicalSize(SDL_Renderer* r, int w, int h) {
	sdl2compat_logical_w = w; sdl2compat_logical_h = h;
	SDL2_apply_presentation(r);
	return 0;
}
static inline void SDL_RenderGetLogicalSize(SDL_Renderer* r, int* w, int* h) {
	(void)r; *w = sdl2compat_logical_w; *h = sdl2compat_logical_h;
}
static inline int SDL_RenderSetIntegerScale(SDL_Renderer* r, bool on) {
	sdl2compat_integer_scale = on;
	SDL2_apply_presentation(r);
	return 0;
}
#define SDL_GetRendererOutputSize(r, w, h) SDL2_rc(SDL_GetCurrentRenderOutputSize(r, w, h))

/* The texture filtering hint became a per-texture property. */
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"

/* ---------------------------------------------------------------- audio (SDL2 callback device) */
#define AUDIO_U8     SDL_AUDIO_U8
#define AUDIO_S16SYS SDL_AUDIO_S16
typedef void (*SDL2_AudioCallback)(void* userdata, Uint8* stream, int len);
typedef struct SDL2_AudioSpec {
	int freq;
	SDL_AudioFormat format;
	Uint8 channels;
	Uint8 silence;
	Uint16 samples;
	Uint32 size;
	SDL2_AudioCallback callback;
	void* userdata;
} SDL2_AudioSpec;
#ifndef SDL2_COMPAT_IMPL
#define SDL_AudioSpec SDL2_AudioSpec
#endif
int  SDL2_OpenAudio(SDL2_AudioSpec* desired, SDL2_AudioSpec* obtained);
void SDL2_PauseAudio(int pause_on);
void SDL2_LockAudio(void);
void SDL2_UnlockAudio(void);
void SDL2_CloseAudio(void);
#define SDL_OpenAudio   SDL2_OpenAudio
#define SDL_PauseAudio  SDL2_PauseAudio
#define SDL_LockAudio   SDL2_LockAudio
#define SDL_UnlockAudio SDL2_UnlockAudio
#define SDL_CloseAudio  SDL2_CloseAudio

/* ---------------------------------------------------------------- misc */
#define SDL_SwapLE16 SDL_Swap16LE
#define SDL_SwapLE32 SDL_Swap32LE
#define SDL_SwapBE16 SDL_Swap16BE
#define SDL_SwapBE32 SDL_Swap32BE
#define SDL_SetClipRect(s, r) SDL_SetSurfaceClipRect(s, r)
#define SDL_ShowSimpleMessageBox(f, t, m, w) SDL2_rc(SDL_ShowSimpleMessageBox(f, t, m, w))

#endif /* SDL2_COMPAT_H */
