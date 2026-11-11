SDLPoP for MS-DOS
=================

This is SDLPoP -- the open-source port of Prince of Persia -- built back for
MS-DOS as a 32-bit protected-mode (DPMI) program, with SDLPoP's additions
intact: the in-game menu with settings, quicksave/quickload, replays,
lighting, colored torches, the bug fixes and gameplay options in SDLPOP.INI,
mods, PNG/OGG resources and screenshots.

It is the normal SDLPoP source, on SDL3, whose DOS port provides the video
(VESA and VGA), the Sound Blaster, the keyboard, the mouse and a gameport
joystick.


Running it
----------

Copy the PRINCE folder (from `make dist`, or prince-dos.zip) anywhere and run
PRINCE.EXE. The DPMI host (CWSDPMI) is built into the EXE; under Windows 9x or
with HDPMI32 loaded, that one is used instead.

    C:\PRINCE> PRINCE.EXE
    C:\PRINCE> PRINCE.EXE 7          start at level 7 (see SDLPoP's README)
    C:\PRINCE> PRINCE.EXE > LOG.TXT  keep the messages (video mode, sound card)

Requirements:

  * 486DX (or a 486SX with a 487) or better. For full speed: a Pentium; in
    DOSBox, about 60000 cycles is enough (`cycles=max` is fine).
  * 8 MB of RAM or more.
  * VGA. A VESA BIOS (or UniVBE) gives hicolor modes; without one the game
    runs in VGA mode 13h, 256 colours.
  * Optional: Sound Blaster (1.x/2.0, Pro, 16) or compatible, with the BLASTER
    environment variable set (e.g. `SET BLASTER=A220 I7 D1 H5 T6`); music
    plays on the card's OPL2/OPL3 FM chip, like the original game.
  * Optional: mouse driver (for the menus), gameport joystick.


Video modes
-----------

By default the game picks 320x200 in the most colours the card offers (the
game's own resolution, so no scaling), then the smallest larger hicolor mode
(640x400, 640x480, ...; the picture is doubled and centred), and if there is
no VESA hicolor mode at all, VGA mode 13h.

In mode 13h each frame gets its own 256-colour palette. The game rarely shows
more than 256 colours at once, so this is usually exact; with lighting on, or
under the translucent menu, the most used 256 colours are kept.

To choose, set PRINCE_VIDEO to WIDTHxHEIGHTxBITS before starting:

    SET PRINCE_VIDEO=320x200x8      VGA mode 13h (fastest on old machines)
    SET PRINCE_VIDEO=320x200x16     VESA 320x200, 65536 colours
    SET PRINCE_VIDEO=640x480x16     VESA 640x480, picture doubled
    SET PRINCE_VIDEO=640x480x32

With `use_correct_aspect_ratio = true` in SDLPOP.INI, a hicolor mode with a
4:3 shape (e.g. 640x480) is filled the way a VGA monitor shows 320x200.


Sound
-----

Digitized sounds go through the Sound Blaster's DSP (SDL3 configures it from
BLASTER). The music, as in the original game, is FM: it goes straight to the
OPL2/OPL3 chip at port 388h, which needs no CPU time for synthesis. If no OPL
chip is found the music is emulated in software, which needs a fast CPU; set
`PRINCE_OPL=0` to force that.


Files
-----

DOS file names are 8.3. The game's own files fit; QUICKSAVE.SAV becomes
QUICKSAV.SAV, screenshots are SCREENSH\SHOT0000.PNG, ..., and replay names are
up to 8 characters unless a long file name driver (e.g. DOSLFN) is loaded.
Mods go in MODS\<name>, as on other systems (folder names up to 8 characters).


Keys
----

The same as SDLPoP elsewhere (see SDLPoP's README): arrows/numeric keypad,
Shift; Esc for the menu; F6/F9 quicksave/quickload; Ctrl+Q quits; Ctrl+A
restarts the level; Ctrl+R returns to the title; Ctrl+Tab records a replay;
F12 takes a screenshot. Alt+Enter does nothing (always full screen).


Building
--------

Everything is in this folder; no CMake, no sources are downloaded. The compiler
is a DJGPP cross compiler on the PATH (`i586-pc-msdosdjgpp-gcc`; prebuilt ones:
https://github.com/andrewwutw/build-djgpp/releases) when there is one,
otherwise the `djfdyuruiry/djgpp` Docker image (pulled automatically). Force
either with `make DOCKER=1` / `make DOCKER=0`. Plus GNU make.

    cd msdos
    make            # SDL3 (into build/libSDL3.a), the game, prince.exe with CWSDPMI inside,
                    # and data\, SDLPOP.INI, mods\, replays\ copied next to it
    make run        # play it in DOSBox, this folder as C:
    make dist       # build/dist/PRINCE: just the game files, to copy to a DOS machine
    make clean      # leaves SDLPOP.INI and your saves alone

After `make`, this folder is a working game: run PRINCE.EXE here (DOSBox,
a shared drive, ...). For a real DOS machine, copy prince.exe, data\ and
SDLPOP.INI (or use `make dist`); SDL3\, cwsdpmi\ and build\ are only for building.

`make msdos_original` makes a prince.exe that needs CWSDPMI.EXE (copied next
to it) or another DPMI host.

What is here:

  * SDL3/ -- the complete SDL3 source (https://libsdl.org, commit 4ca1b1c),
    which includes the DOS port. The Makefile compiles the files SDL's own
    build uses for DJGPP.
  * sdl3-config/ -- SDL_build_config.h and SDL_revision.h, which SDL's CMake
    would generate; made once for DJGPP.
  * cwsdpmi/ -- CWSDPMI r7 by Charles W. Sandmann (binaries, source and
    documentation), the DPMI host. CWSDSTUB.EXE is put in front of the game.

How it fits together:

  * src/sdl2_compat.h, src/sdl2_compat.c -- SDLPoP was written for SDL2; SDL3
    is the one that runs on DOS. These map the SDL2 calls the game uses onto
    SDL3 (including the SDL2-style audio callback). The whole fork now builds
    with SDL3 on every system (src/Makefile, src/CMakeLists.txt).
  * src/dos_video.c -- on DOS there is no SDL renderer: the 320x200 game
    screen goes straight to the screen surface (fast converters for 15/16/24/
    32-bit modes; per-frame palette for mode 13h).
  * src/midi.c -- on DOS the music goes to the real OPL chip.
  * main.c includes SDL_main.h, which on DOS sets up the near pointers SDL needs.
