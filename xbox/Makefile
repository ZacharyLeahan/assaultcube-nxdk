XBE_TITLE = AssaultCube
GEN_XISO = assaultcube.iso
NXDK_SDL = y
NXDK_CXX = y
NXDK_CXXFLAGS += -O2 -Wall -fno-rtti
NXDK_DIR ?= $(CURDIR)/../../nxdk
SRCS = $(addprefix $(CURDIR)/,platform_nxdk.c ambience.c controllers.c match.cpp grenade.cpp navigation.c shot_light.c prop_depth.c gpu_render.c assets.s)
NXDK_CFLAGS += -O2 -Wall
SDL_AUDIO_DIR := $(NXDK_DIR)/lib/sdl/SDL2/src/audio/xbox
SDL_AUDIO_SOURCE := $(SDL_AUDIO_DIR)/SDL_xboxaudio.c
# Older nxdk SDL forces 1024 frames even when the title requests a smaller buffer.
ifneq ($(shell grep -F '_this->spec.samples = SAMPLE_FRAME_COUNT;' "$(SDL_AUDIO_SOURCE)"),)
SRCS += $(CURDIR)/generated/xbox_audio.c
NXDK_CFLAGS += -I$(SDL_AUDIO_DIR)
endif
PBGL_DIR := $(CURDIR)/third_party/pbgl
NXDK_CFLAGS += -I$(PBGL_DIR)/include
ifeq ($(AIM_TRACE),1)
SDL_XBOX_DIR := $(NXDK_DIR)/lib/sdl/SDL2/src/joystick/xbox
SRCS += $(CURDIR)/generated/aim_driver.c
NXDK_CFLAGS += -I$(SDL_XBOX_DIR) -I$(NXDK_DIR)/lib/usb/libusbohci/inc -I$(NXDK_DIR)/lib/usb/libusbohci_xbox
endif
include $(CURDIR)/lan/enet.mk
include $(NXDK_DIR)/Makefile
include $(PBGL_DIR)/config_pbgl.make
main.exe: $(PBGL_LIB)

$(CURDIR)/generated/xbox_audio.c: $(SDL_AUDIO_SOURCE) Makefile
	@sed 's/_this->spec.samples = SAMPLE_FRAME_COUNT;/\/\* AssaultCube: keep the requested SDL buffer size. \*\//' "$<" > "$@"

# bake.py also regenerates the binary assets referenced by assets.s.
$(CURDIR)/assets.obj: $(wildcard generated/*.bin)

$(CURDIR)/gpu_render.obj: generated/gpu.h generated/water.h
$(addprefix $(CURDIR)/,gpu_render.obj shot_light.obj): shot_light.h
$(addprefix $(CURDIR)/,platform_nxdk.obj gpu_render.obj): system_menu.h gpu_render.h
$(CURDIR)/ambience.obj: generated/ambience.h
$(CURDIR)/navigation.obj: generated/ladders.h generated/clips.h generated/corners.h
$(CURDIR)/prop_depth.obj: generated/prop_bounds.h
$(addprefix $(CURDIR)/,prop_depth.obj gpu_render.obj match.obj): geometry.h
$(addprefix $(CURDIR)/,match.obj grenade.obj): grenade.h
$(CURDIR)/grenade.obj: generated/water.h navigation.h geometry.h match.h
$(CURDIR)/match.obj: generated/starts.h generated/items.h generated/water.h generated/bot_head.h generated/bot_skill.h generated/bot_names.h
$(addprefix $(CURDIR)/,platform_nxdk.obj match.obj gpu_render.obj navigation.obj shot_light.obj prop_depth.obj lan/lan.obj): match.h
$(addprefix $(CURDIR)/,platform_nxdk.obj match.obj gpu_render.obj lan/lan.obj): lan/lan.h
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

$(CURDIR)/generated/lan/%.c: $(ENET_DIR)/%.c
	@mkdir -p "$(@D)"
	@cp "$<" "$@"

$(ENET_SRC:.c=.obj): NXDK_CFLAGS += -include $(CURDIR)/lan/enet_platform.h
