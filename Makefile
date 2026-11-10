# =======================================================================
# Makefile — Certinho / Pestinha
#
# Targets:
#   make                   Linux, SDL2  → ./pestinha
#   make windows           Windows .exe → ./pestinha.exe  (cross MinGW)
#   make debug             Linux SDL2 + AddressSanitizer
#   make headless          sem display  → ./pestinha
#   make clean
#
# Dependências Linux:
#   sudo apt install gcc libsdl2-dev
#
# Dependências Windows (cross-compile no Linux):
#   sudo apt install mingw-w64
#   Baixar SDL2-devel-X.X.X-mingw.tar.gz em https://libsdl.org/download-2.0.php
#   Extrair e apontar SDL_WIN abaixo para a pasta x86_64-w64-mingw32/
# =======================================================================

CC      = gcc
TARGET  = pestinha

# Pasta do SDL2 para MinGW — ajuste para o caminho no seu sistema
# Exemplo: SDL_WIN = $(HOME)/sdl2-mingw/x86_64-w64-mingw32
SDL_WIN ?= $(HOME)/sdl2-mingw/x86_64-w64-mingw32

CFLAGS_BASE = -std=c11 -Wall -Wextra -O2 -ffast-math -I.

# =======================================================================
# Fontes comuns (sem o backend gráfico — escolhido por target)
# =======================================================================

SRCS_CORE = \
    pestinha.c      \
    neuro.c         \
    gfx.c           \
    render_null.c   \
    behavior.c      \
    body.c          \
    conditioning.c  \
    events.c        \
    fx.c            \
    homeostasis.c   \
    memory.c        \
    sensors.c       \
    ui.c            \
    world.c

# =======================================================================
# Linux — SDL2
# =======================================================================

SRCS_LINUX = $(SRCS_CORE) render_sdl2.c

CFLAGS_LINUX = $(CFLAGS_BASE) $(shell sdl2-config --cflags)
LIBS_LINUX   = $(shell sdl2-config --libs) -lm

all: $(TARGET)

$(TARGET): $(SRCS_LINUX)
	$(CC) $(CFLAGS_LINUX) -o $@ $^ $(LIBS_LINUX)

# =======================================================================
# Debug — Linux SDL2 + sanitizers
# =======================================================================

debug: CFLAGS_LINUX += -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
debug: LIBS_LINUX   += -fsanitize=address,undefined
debug: $(TARGET)

# =======================================================================
# Windows .exe — cross-compile com MinGW
# =======================================================================

CC_WIN   = x86_64-w64-mingw32-gcc
TARGET_WIN = pestinha.exe

SRCS_WIN = $(SRCS_CORE) render_sdl2.c

CFLAGS_WIN = -std=c11 -Wall -Wextra -O2 -ffast-math \
             -I. -I$(SDL_WIN)/include/SDL2 \
             -DWIN32 -D_WIN32

LIBS_WIN = -L$(SDL_WIN)/lib \
           -lSDL2 -lSDL2main \
           -lm -mwindows \
           -static-libgcc

windows: $(SRCS_WIN)
	$(CC_WIN) $(CFLAGS_WIN) -o $(TARGET_WIN) $^ $(LIBS_WIN)
	@echo ""
	@echo ">>> Copie $(SDL_WIN)/bin/SDL2.dll para a mesma pasta do .exe"

# =======================================================================
# Headless — sem display, útil para Valgrind/Massif
# =======================================================================

SRCS_HEADLESS = $(SRCS_CORE)
CFLAGS_HEADLESS = $(CFLAGS_BASE) -DPESTINHA_HEADLESS
LIBS_HEADLESS = -lm

headless:
	$(CC) $(CFLAGS_HEADLESS) -o $(TARGET) $(SRCS_HEADLESS) $(LIBS_HEADLESS)

# =======================================================================
# Massif
# =======================================================================

massif: headless
	valgrind --tool=massif --pages-as-heap=yes \
	         --massif-out-file=massif.out ./$(TARGET)
	ms_print massif.out | head -80

# =======================================================================

clean:
	rm -f *.o $(TARGET) $(TARGET_WIN) massif.out

.PHONY: all debug windows headless massif clean
