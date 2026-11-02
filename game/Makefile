# War Table - Nintendo DS homebrew ROM (B01 scaffold)
# Modules: boot loop input sim render save audio ui queue
# Targets: clean | rom | budget | run | include-graph | regen | test-host |
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
	-DARM9 -D__NDS__ \
	-Wall -O2 -g -c

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions

LDFLAGS := \
	$(ARCH) $(TYPEFLAGS) \
	-LC:/devkitPro/devkitARM/arm-none-eabi/lib/thumb \
	-LC:/devkitPro/calico/lib \
	-LC:/devkitPro/libnds/lib \
	-specs=C:/devkitPro/calico/share/ds9.specs \
	-Wl,-Map,$(TARGET).map

LIBS := -lnds9 -lcalico_ds9 -lfat -lcalico_ds9

SRCS := $(foreach m,$(MODULES),$(wildcard src/$(m)/*.c))
OBJS := $(patsubst src/%.c, $(BUILD)/src/%.o, $(SRCS))
GFX_SRC := $(sort $(wildcard data/*.png))
GFX_OBJS := $(patsubst $(DATA)/%.png, $(BUILD)/gfx/%.o, $(GFX_SRC))

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

# ---------------------------------------------------------------------------
# Link and package
# ---------------------------------------------------------------------------
$(TARGET).elf: $(OBJS) $(GFX_OBJS)
	@echo "  LINK:" $@
	$(CXX) $(LDFLAGS) $(OBJS) $(GFX_OBJS) $(LIBS) -o $@

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
	@rm -rf $(BUILD) $(TARGET).elf $(TARGET).nds $(TARGET).map $(TARGET).lst

budget:
	./tools/check_budget.sh

regen:
	python3 tools/gen_tokens.py
	python3 tools/check_modules.py

test-host:
	bash tests/host/run.sh

# B05 remainder targets -----------------------------------------------------
# validate: 8/8 WCAG pairs from tokens.json (visual-contract table).
validate:
	python3 tools/contrast.py

# cycles: builds+runs the ops probe then gates cycles/unit and per-frame.
cycles:
	python3 tools/check_cycles.py

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

.PHONY: all clean rom budget run include-graph regen test-host validate cycles profiles memory memory-check rtk help setup
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
	@echo "  test-host      gcc unit tests for the B03 + B06 modules"
	@echo "  validate       8/8 WCAG contrast pairs from tokens.json (B05)"
	@echo "  cycles         cycle harness: ops counters -> cycles/unit gate (B05)"
	@echo "  profiles       emulator save profiles: DLDI + EEPROM 512 B (B05)"
	@echo "  include-graph  module isolation + palette gates"
	@echo "  regen          rewrite tokens.h and re-run the gates"
	@echo "  memory         mine cases/ + game/ into MemPalace"
	@echo "  memory-check   print the top 3 War Table recall hits"
	@echo "  rtk-<cmd>      run RTK <cmd> through tools/rtk.sh"
	@echo "  setup          one-time seeding of C:/devkitPro/winbin"
