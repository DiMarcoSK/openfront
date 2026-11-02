# War Table - Nintendo DS homebrew ROM (B01 scaffold)
# Modules: boot loop input sim render save audio ui
# Targets: clean | rom | budget | run | include-graph | help
# Toolchain: Windows devkitARM driven through WSL interop (see tools/setup.sh).

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
MODULES := boot loop input sim render save audio ui
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

LIBS := -lnds9 -lcalico_ds9

SRCS := $(foreach m,$(MODULES),$(wildcard src/$(m)/*.c))
OBJS := $(patsubst src/%.c, $(BUILD)/src/%.o, $(SRCS))
GFX_SRC := $(sort $(wildcard data/*.png))
GFX_OBJS := $(patsubst $(DATA)/%.png, $(BUILD)/gfx/%.o, $(GFX_SRC))

.PHONY: all clean rom budget run include-graph help setup
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

include-graph:
	@python3 tools/check_modules.py

run: $(TARGET).nds
	RUN=melonds ./tools/run.sh $(TARGET).nds

setup:
	bash tools/setup.sh

help:
	@echo "make rom | make clean | make budget | make run | make include-graph"
