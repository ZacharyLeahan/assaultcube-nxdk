XBE_TITLE = AssaultCube
GEN_XISO = assaultcube.iso
NXDK_SDL = y
NXDK_CXX = y
NXDK_CXXFLAGS += -O2 -Wall -fno-rtti
NXDK_DIR ?= $(CURDIR)/../../nxdk
SRCS = $(addprefix $(CURDIR)/,platform_nxdk.c ambience.c controllers.c match.cpp navigation.c shot_light.c prop_depth.c gpu_render.c assets.s)
NXDK_CFLAGS += -O2 -Wall
PBGL_DIR := $(CURDIR)/third_party/pbgl
NXDK_CFLAGS += -I$(PBGL_DIR)/include
ifeq ($(AIM_TRACE),1)
SDL_XBOX_DIR := $(NXDK_DIR)/lib/sdl/SDL2/src/joystick/xbox
SRCS += $(CURDIR)/generated/aim_driver.c
NXDK_CFLAGS += -I$(SDL_XBOX_DIR) -I$(NXDK_DIR)/lib/usb/libusbohci/inc -I$(NXDK_DIR)/lib/usb/libusbohci_xbox
endif
include $(NXDK_DIR)/Makefile
include $(PBGL_DIR)/config_pbgl.make
main.exe: $(PBGL_LIB)

# bake.py also regenerates the binary assets referenced by assets.s.
$(CURDIR)/assets.obj: $(wildcard generated/*.bin)

$(CURDIR)/gpu_render.obj: generated/gpu.h generated/water.h
$(addprefix $(CURDIR)/,gpu_render.obj shot_light.obj): shot_light.h
$(CURDIR)/ambience.obj: generated/ambience.h
$(CURDIR)/navigation.obj: generated/ladders.h generated/clips.h generated/corners.h
$(CURDIR)/prop_depth.obj: generated/prop_bounds.h
$(addprefix $(CURDIR)/,prop_depth.obj gpu_render.obj match.obj): geometry.h
$(CURDIR)/match.obj: generated/starts.h generated/items.h generated/water.h generated/bot_head.h
$(addprefix $(CURDIR)/,platform_nxdk.obj match.obj gpu_render.obj navigation.obj shot_light.obj): match.h
$(addprefix $(CURDIR)/,match.obj gpu_render.obj navigation.obj): navigation.h
$(addprefix $(CURDIR)/,controllers.obj platform_nxdk.obj): controllers.h

# Relink when switching between the instrumented and stock SDL driver.
INPUT_MODE := generated/input-mode-$(if $(filter 1,$(AIM_TRACE)),trace,play)
ifeq ($(wildcard $(INPUT_MODE)),)
.PHONY: main.exe
endif
all: $(INPUT_MODE)
$(INPUT_MODE): $(OUTPUT_DIR)/default.xbe
	@rm -f generated/input-mode-play generated/input-mode-trace
	@touch $@

ifeq ($(AIM_TRACE),1)
$(CURDIR)/generated/aim_driver.c: tests/aim_driver.py $(SDL_XBOX_DIR)/SDL_xboxjoystick.c
	python $< $(SDL_XBOX_DIR)/SDL_xboxjoystick.c $@
endif
