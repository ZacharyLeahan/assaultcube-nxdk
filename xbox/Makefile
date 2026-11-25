XBE_TITLE = AssaultCube
GEN_XISO = assaultcube.iso
NXDK_SDL = y
NXDK_CXX = y
NXDK_CXXFLAGS += -O2 -Wall -fno-rtti
NXDK_DIR ?= $(CURDIR)/../../nxdk
SRCS = $(addprefix $(CURDIR)/,platform_nxdk.c controllers.c match.cpp navigation.c gpu_render.c assets.s)
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

$(CURDIR)/gpu_render.obj: generated/gpu.h

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
