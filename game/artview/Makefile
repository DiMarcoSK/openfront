# artview - War Table art gallery ROM. Same toolchain recipe as ../Makefile
# (Windows devkitARM through WSL interop); compiles every asset that
# tools/art/export.py wrote to ../art/ds with grit, links one .nds.
#   make            build artview.nds
#   make run        boot it in melonDS
#   make art        re-run the Python art export first
#   make SCENE=n    artview_sN.nds, booting straight into scene n (captures)

WINBIN_IX := /mnt/c/devkitPro/winbin
CC      := $(WINBIN_IX)/gcc
CXX     := $(WINBIN_IX)/g++
GRIT    := $(WINBIN_IX)/grit
NDSTOOL := $(WINBIN_IX)/ndstool

TARGET := artview
BUILD  := build
MAIN_O := $(BUILD)/main.o
ifneq ($(SCENE),)
TARGET := artview_s$(SCENE)
MAIN_O := $(BUILD)/main_s$(SCENE).o
SCENE_DEF := -DSTART_SCENE=$(SCENE)
endif
ART    := ../art/ds

TYPEFLAGS := \
	-BC:/devkitPro/winbin \
	-BC:/devkitPro/devkitARM/libexec/gcc/arm-none-eabi/16.1.0/ \
	-BC:/devkitPro/devkitARM/lib/gcc/arm-none-eabi/16.1.0/ \
	--sysroot=C:/devkitPro/devkitARM/arm-none-eabi \
	-isystemC:/devkitPro/devkitARM/lib/gcc/arm-none-eabi/16.1.0/include

ARCH := -march=armv5te -mtune=arm946e-s -mthumb

CFLAGS := -fdata-sections -ffunction-sections $(ARCH) $(TYPEFLAGS) \
	-IC:/devkitPro/libnds/include -IC:/devkitPro/calico/include \
	-I$(BUILD)/gfx -I$(ART) -DARM9 -D__NDS__ -Wall -O2 -c

LDFLAGS := $(ARCH) $(TYPEFLAGS) \
	-LC:/devkitPro/devkitARM/arm-none-eabi/lib/thumb \
	-LC:/devkitPro/calico/lib -LC:/devkitPro/libnds/lib \
	-specs=C:/devkitPro/calico/share/ds9.specs -Wl,-Map,$(TARGET).map

LIBS := -lnds9 -lcalico_ds9

PNGS := $(sort $(wildcard $(ART)/bg/*.png) $(wildcard $(ART)/obj/*.png))
GFX_C := $(patsubst %.png,$(BUILD)/gfx/%.c,$(notdir $(PNGS)))
GFX_O := $(GFX_C:.c=.o)

.DEFAULT_GOAL := $(TARGET).nds
vpath %.png $(ART)/bg $(ART)/obj

$(BUILD)/gfx/%.c: %.png
	@mkdir -p $(BUILD)/gfx
	@cp $(<:.png=.grit) $(BUILD)/gfx/ && cp $< $(BUILD)/gfx/
	cd $(BUILD)/gfx && $(GRIT) $(notdir $<) -o $*.c >/dev/null

$(BUILD)/gfx/%.o: $(BUILD)/gfx/%.c
	$(CC) $(CFLAGS) $< -o $@

$(MAIN_O): main.c $(GFX_C) $(ART)/art_assets.h $(ART)/art_index.h $(ART)/art_palettes.h
	$(CC) $(CFLAGS) $(SCENE_DEF) main.c -o $@

$(TARGET).elf: $(MAIN_O) $(GFX_O)
	$(CXX) $(LDFLAGS) $^ $(LIBS) -o $@

$(TARGET).nds: $(TARGET).elf
	$(NDSTOOL) -c $@ -9 $< -7 C:/devkitPro/calico/bin/ds7_maine.elf \
	  -b C:/devkitPro/calico/share/nds-icon.bmp "WAR TABLE ART;gallery;devkitPro"

art:
	cd ../.. && python3 game/tools/art/export.py

run: $(TARGET).nds
	RUN=melonds ../tools/run.sh $(TARGET).nds

clean:
	rm -rf $(BUILD) artview*.elf artview*.nds artview*.map

.PHONY: art run clean
