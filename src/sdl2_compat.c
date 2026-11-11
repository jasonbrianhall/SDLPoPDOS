/*
SDLPoP, a port/conversion of the DOS game Prince of Persia.
Copyright (C) 2013-2025  Dávid Nagy

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/

// The SDL2 audio callback device (SDL_OpenAudio & co.), on an SDL3 audio stream.
#define SDL2_COMPAT_IMPL
#include "sdl2_compat.h"
#include <stdlib.h>

int sdl2compat_logical_w, sdl2compat_logical_h;
bool sdl2compat_integer_scale;

static SDL_AudioStream* sdl2_stream;
static SDL2_AudioSpec sdl2_spec;
static Uint8* sdl2_buf;
static int sdl2_buf_len;

// SDL3 asks for "additional_amount" more bytes; feed it in chunks of the
// buffer size the game asked for, as SDL2 would have.
static void SDLCALL sdl2_stream_cb(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount) {
	(void)userdata; (void)total_amount;
	while (additional_amount > 0) {
		sdl2_spec.callback(sdl2_spec.userdata, sdl2_buf, sdl2_buf_len);
		SDL_PutAudioStreamData(stream, sdl2_buf, sdl2_buf_len);
		additional_amount -= sdl2_buf_len;
	}
}

int SDL2_OpenAudio(SDL2_AudioSpec* desired, SDL2_AudioSpec* obtained) {
	if (sdl2_stream) return SDL_SetError("Audio device is already opened"), -1;
	if (!SDL_WasInit(SDL_INIT_AUDIO) && !(SDL_InitSubSystem)(SDL_INIT_AUDIO)) return -1;
	SDL_AudioSpec spec;
	spec.format = desired->format;
	spec.channels = desired->channels;
	spec.freq = desired->freq;
	sdl2_spec = *desired;
	sdl2_spec.silence = desired->format == SDL_AUDIO_U8 ? 0x80 : 0;
	if (sdl2_spec.samples == 0) sdl2_spec.samples = 1024;
	sdl2_buf_len = sdl2_spec.samples * desired->channels * SDL_AUDIO_BYTESIZE(desired->format);
	sdl2_spec.size = (Uint32)sdl2_buf_len;
	sdl2_buf = (Uint8*)malloc(sdl2_buf_len);
	if (!sdl2_buf) return SDL_OutOfMemory(), -1;
	sdl2_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, sdl2_stream_cb, NULL);
	if (!sdl2_stream) {
		free(sdl2_buf); sdl2_buf = NULL;
		return -1;
	}
	desired->silence = sdl2_spec.silence;
	desired->size = sdl2_spec.size;
	if (obtained) *obtained = sdl2_spec;
	return 0;
}

void SDL2_PauseAudio(int pause_on) {
	if (!sdl2_stream) return;
	if (pause_on) SDL_PauseAudioStreamDevice(sdl2_stream);
	else SDL_ResumeAudioStreamDevice(sdl2_stream);
}

void SDL2_LockAudio(void)   { if (sdl2_stream) SDL_LockAudioStream(sdl2_stream); }
void SDL2_UnlockAudio(void) { if (sdl2_stream) SDL_UnlockAudioStream(sdl2_stream); }

void SDL2_CloseAudio(void) {
	if (!sdl2_stream) return;
	SDL_DestroyAudioStream(sdl2_stream);
	sdl2_stream = NULL;
	free(sdl2_buf); sdl2_buf = NULL;
}
