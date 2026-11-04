# War Table - Nintendo DS homebrew ROM (B01 scaffold)
# Modules: boot loop input sim render save audio ui queue
# Targets: clean | rom | budget | run | include-graph | regen | test-host |
#          balance |
#          memory | memory-check | rtk | help
# Toolchain: Windows devkitARM driven through WSL interop (see tools/setup.sh).
# History: MemPalace wing openfront-war-table via tools/memory.sh.
# Token compression: RTK via tools/rtk.sh (see .rtk/filters.toml).

# All flags passed to the compiler/linker use Windows-style paths (C:/...)
# because the devkitARM .exe drivers run as a Windows process. The .exe files
# themselves are addressed via /mnt/c (WSL style) so they execute in interop.
WINROOT    := C:/devkitPro
WINROOT_IX := /mnt/c/devkitPro
WINBIN     := C:/devkitPro/winbin
WINBIN_IX  := $(WINROOT_IX)/winbin

CALICO  := $(WINROOT)/calico
LIBNDS  := $(WINROOT)/libnds
DEVPARM := $(WINROOT)/devkitARM

CC      := $(WINBIN_IX)/gcc
CXX     := $(WINBIN_IX)/g++
AS      := $(WINBIN_IX)/as
LD      := $(WINBIN_IX)/ld
AR      := $(WINBIN_IX)/ar
OBJCOPY := $(WINBIN_IX)/objcopy
GRIT    := $(WINBIN_IX)/grit
NDSTOOL := $(WINBIN_IX)/ndstool

TARGET := wartable
BUILD  := build
DATA   := data
MODULES := boot loop input sim render save audio ui queue
GFXTOOLS := $(WINBIN_IX)/grit

TYPEFLAGS := \
	-BC:/devkitPro/winbin \
	-BC:/devkitPro/devkitARM/libexec/gcc/arm-none-eabi/16.1.0/ \
	-BC:/devkitPro/devkitARM/lib/gcc/arm-none-eabi/16.1.0/ \
	--sysroot=C:/devkitPro/devkitARM/arm-none-eabi \
	-isystemC:/devkitPro/devkitARM/lib/gcc/arm-none-eabi/16.1.0/include

ARCH := -march=armv5te -mtune=arm946e-s -mthumb

CFLAGS := \
	-fdata-sections -ffunction-sections $(ARCH) \
	$(TYPEFLAGS) \
	-IC:/devkitPro/libnds/include \
	-IC:/devkitPro/calico/include \
	-IC:/Users/black/Documents/labs/openfront/game/include \
	-I$(CURDIR) \
	-I$(CURDIR)/include \
	-Ibuild/gfx \
	-Ibuild/art \
	-Iart/ds \
	-DARM9 -D__NDS__ \
	-Wall -O2 -g -c

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions

LDFLAGS := \
	$(ARCH) $(TYPEFLAGS) \
	-LC:/devkitPro/devkitARM/arm-none-eabi/lib/thumb \
	-LC:/devkitPro/calico/lib \
	-LC:/devkitPro/libnds/lib \
	-specs=C:/devkitPro/calico/share/ds9.specs

# Each link owns its map file: rom -> wartable.map, poc-rom -> poc map,
# so `make budget` never reads the bench's 48 KB pool as game RAM.
ROM_LDFLAGS  := $(LDFLAGS) -Wl,-Map,$(TARGET).map
POC_LDFLAGS  := $(LDFLAGS) -Wl,-Map,poc_bench.map

LIBS := -lnds9 -lcalico_ds9 -lfat -lcalico_ds9

SRCS := $(foreach m,$(MODULES),$(wildcard src/$(m)/*.c))
# B19 (ADR-0007): the POC pool engine + its bench ROM are excluded from
# the shipped ROM - the game's battle slice stays the measured aggregate
# engine until the pool layer lands, and the 48 KB bench pool never
# taxes RAM. `make poc` (host sweep) and `make poc-rom` (bench ROM)
# build them explicitly.
SRCS := $(filter-out src/sim/poc_%.c,$(SRCS))
OBJS := $(patsubst src/%.c, $(BUILD)/src/%.o, $(SRCS))
# data/font.png is the B01 placeholder face: the art font (art/ds/obj)
# supersedes it and the two would collide on the grit symbol fontTiles.
GFX_SRC := $(filter-out data/font.png,$(sort $(wildcard data/*.png)))
GFX_OBJS := $(patsubst $(DATA)/%.png, $(BUILD)/gfx/%.o, $(GFX_SRC))
# Art pass: every asset tools/art/export.py wrote to art/ds is compiled by
# grit into the ROM (render.c indexes them through art_assets.h).
ART      := art/ds
ART_MOCKUPS := battle_mock battle_bottom_desert battle_bottom_highlands                battle_bottom_plains title_bottom recruit_bottom result_bottom
ART_PNGS := $(filter-out $(foreach m,$(ART_MOCKUPS),$(ART)/bg/$(m).png),               $(sort $(wildcard $(ART)/bg/*.png) $(wildcard $(ART)/obj/*.png)))
ART_C    := $(patsubst %.png,$(BUILD)/art/%.c,$(notdir $(ART_PNGS)))
ART_OBJS := $(ART_C:.c=.o)
vpath %.png $(ART)/bg $(ART)/obj

.DEFAULT_GOAL := rom

# ---------------------------------------------------------------------------
# Translation: src -> build/src, data -> build/gfx
# ---------------------------------------------------------------------------
$(BUILD)/src/%.o: src/%.c
	@echo "  CC:" $<
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

$(BUILD)/gfx/%.o: $(DATA)/%.png $(DATA)/%.grit
	@echo "  GRIT:" $(notdir $<)
	@mkdir -p $(dir $@)
	cd $(BUILD)/gfx && $(GRIT) ../../$(DATA)/$(notdir $<) -o $(basename $(notdir $<)).c
	$(CC) $(CFLAGS) $(BUILD)/gfx/$*.c -o $@

$(BUILD)/art/%.c: %.png
	@mkdir -p $(BUILD)/art
	@cp $(<:.png=.grit) $(BUILD)/art/ && cp $< $(BUILD)/art/
	cd $(BUILD)/art && $(GRIT) $(notdir $<) -o $*.c >/dev/null

$(BUILD)/art/%.o: $(BUILD)/art/%.c
	$(CC) $(CFLAGS) $< -o $@

# render.c includes art_assets.h, which includes every grit header.
$(BUILD)/src/render/render.o: $(ART_C) $(ART)/art_assets_game.h $(ART)/art_index.h $(ART)/art_palettes.h $(ART)/art_terrain.h

# ---------------------------------------------------------------------------
# Link and package
# ---------------------------------------------------------------------------
$(TARGET).elf: $(OBJS) $(GFX_OBJS) $(ART_OBJS)
	@echo "  LINK:" $@
	$(CXX) $(ROM_LDFLAGS) $(OBJS) $(GFX_OBJS) $(ART_OBJS) $(LIBS) -o $@

$(TARGET).nds: $(TARGET).elf
	@echo "  NDS:" $@
	$(NDSTOOL) -c $@ -9 $< \
	  -7 C:/devkitPro/calico/bin/ds7_maine.elf \
	  -b C:/devkitPro/calico/share/nds-icon.bmp \
	  "WAR TABLE;devkitPro;"

# ---------------------------------------------------------------------------
# Phases
# ---------------------------------------------------------------------------
rom: $(TARGET).nds

clean:
	@echo "clean ..."
	@rm -rf $(BUILD) build/poc $(TARGET).elf $(TARGET).nds $(TARGET).map $(TARGET).lst \
	        poc_bench.elf poc_bench.nds poc_bench.map

budget:
	./tools/check_budget.sh

regen:
	python3 tools/gen_tokens.py
	python3 tools/check_modules.py

test-host:
	bash tests/host/run.sh

# ci: the blocking job - rom (0 warnings), gates, host suites, budget,
# contrast, POC and grayscale in one exit code (B25/B26).
ci:
	bash tools/ci.sh

# B05 remainder targets -----------------------------------------------------
# validate: 8/8 WCAG pairs from tokens.json (visual-contract table).
validate:
	python3 tools/contrast.py

# cycles: builds+runs the ops probe then gates cycles/unit and per-frame.
cycles:
	python3 tools/check_cycles.py

# balance: the battle balance metrics (GAME_DESIGN 16) - every front x
# formation x policy over 400 seeds through the real sim; the gate keeps
# "Challenging, Never Frustrating" (mortal fronts winnable when prepared,
# no god trivial, none impossible, every god's counter worth playing).
balance:
	@mkdir -p tests/host/bin
	gcc -O2 -Wall -Wextra -std=c11 -Iinclude -Isrc -Itests/host -Isrc/save 	    tests/host/balance_probe.c src/queue/queue.c src/loop/trace.c 	    src/input/input.c src/sim/sim.c src/sim/field.c src/sim/war.c src/save/save.c src/save/slots.c 	    src/save/crc32.c src/save/status.c -o tests/host/bin/balance_probe
	./tests/host/bin/balance_probe --gate | tail -8

# poc: the ADR-0007 unit-cap POC sweep (B19, spec-04 T015). Builds the
# host sweep bench, runs the 2,000-6,000 sweep and enforces the
# measured gate; the selftest proves the gate bites before it matters.
poc:
	gcc -Wall -Wextra -std=c11 -Iinclude -Isrc -Isrc/sim -Itests/host \
	    tests/host/poc_sweep.c src/sim/poc_pool.c \
	    -o tests/host/bin/poc_sweep
	./tests/host/bin/poc_sweep
	python3 tools/check_poc.py --selftest
	python3 tools/check_poc.py

# poc-rom: the hardware instrument (ARM9 bench ROM, TIMER1:TIMER0 @
# 33.514 MHz) for the emulator timing + flashcart POC run (ADR-0007
# arbiter). NOT the shipped game; see docs/poc-battle.md for the run.
poc-rom:
	@mkdir -p build/poc
	$(CC) $(CFLAGS) -I$(CURDIR)/src/sim src/sim/poc_pool.c -o build/poc/pool.o
	$(CC) $(CFLAGS) -I$(CURDIR)/src/sim src/sim/poc_bench_main.c -o build/poc/bench.o
	$(CXX) $(POC_LDFLAGS) build/poc/pool.o build/poc/bench.o -lnds9 -lcalico_ds9 -o poc_bench.elf
	$(NDSTOOL) -c poc_bench.nds -9 poc_bench.elf \
	  -7 C:/devkitPro/calico/bin/ds7_maine.elf \
	  -b C:/devkitPro/calico/share/nds-icon.bmp \
	  "POC BENCH;devkitPro;"

# Grayscale fixture for the tier band (B17 tier-band state colours).
grayscale:
	python3 tools/grayscale.py

# profiles: emulator save profiles sanity (ROM + 512 B EEPROM .sav).
profiles:
	bash tools/emu_profiles.sh check

include-graph:
	@python3 tools/check_modules.py

run: $(TARGET).nds
	RUN=melonds ./tools/run.sh $(TARGET).nds

# Project memory. `make memory` mines cases/ + game/ into MemPalace and
# `make memory-check` prints the top recall so the agent knows it works.
memory:
	bash tools/memory.sh mine

memory-check:
	bash tools/memory.sh check

# RTK shim: every CLI can be routed through the token compressor, and the
# project filters in .rtk/filters.toml carry the WAR TABLE specifics.
rtk-%:
	bash tools/rtk.sh $(*) $(RTK_ARGS)

.PHONY: all clean rom budget run include-graph regen test-host ci validate grayscale cycles poc poc-rom profiles memory memory-check rtk help setup
.DEFAULT_GOAL := rom

setup:
	bash tools/setup.sh


# ---------------------------------------------------------------------------
# Help
# ---------------------------------------------------------------------------
help:
	@echo "War Table make targets:"
	@echo "  rom            build the .nds (default)"
	@echo "  clean          rm build/ + .elf + .nds + .map"
	@echo "  budget         print ROM/RAM/VRAM and enforce the ceilings"
	@echo "  run            boot wartable.nds in melonDS (RUN=melonds)"
	@echo "  test-host      gcc host suites (queue, save, title ... battle, fuzz)"
	@echo "  ci             the blocking job: every gate in one exit code"
	@echo "  validate       8/8 WCAG contrast pairs from tokens.json (B05)"
	@echo "  grayscale      tier-band grayscale fixture (B17)"
	@echo "  cycles         cycle harness: ops counters -> cycles/unit gate (B05)"
	@echo "  poc            ADR-0007 unit-cap POC sweep + gate (B19)"
	@echo "  poc-rom        build poc_bench.nds: the hardware POC instrument"
	@echo "  profiles       emulator save profiles: DLDI + EEPROM 512 B (B05)"
	@echo "  include-graph  module isolation + palette gates"
	@echo "  regen          rewrite tokens.h and re-run the gates"
	@echo "  memory         mine cases/ + game/ into MemPalace"
	@echo "  memory-check   print the top 3 War Table recall hits"
	@echo "  rtk-<cmd>      run RTK <cmd> through tools/rtk.sh"
	@echo "  setup          one-time seeding of C:/devkitPro/winbin"
