# Project: SDLPoP

CC = gcc
RM = rm -f

HFILES = common.h config.h data.h proto.h types.h sdl2_compat.h
OBJ = main.o data.o seg000.o seg001.o seg002.o seg003.o seg004.o seg005.o seg006.o seg007.o seg008.o seg009.o seqtbl.o replay.o options.o lighting.o screenshot.o menu.o midi.o opl3.o stb_vorbis.o sdl2_compat.o dos_video.o
BIN = ../prince

OS      := $(shell uname)

CPPFLAGS += -Wall -D_GNU_SOURCE=1
CFLAGS += -std=c99 -O2

# SDL3 (3.4 or newer); SDL_image is not needed. For MS-DOS see ../msdos/Makefile.
LIBS := $(shell pkg-config --libs sdl3)
CFLAGS += $(shell pkg-config --cflags sdl3)

ifeq ($(OS),Darwin)
CPPFLAGS += -D_THREAD_SAFE -DOSX
endif

all: $(BIN)

clean:
	$(RM) $(OBJ) $(BIN)

install:
	./install.sh

uninstall:
	./uninstall.sh

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o $@ $(LIBS) -lm

%.o: %.c $(HFILES)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -c $<

.PHONY: all clean

