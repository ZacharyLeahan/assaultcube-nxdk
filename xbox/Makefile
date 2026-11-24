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
include $(NXDK_DIR)/Makefile
include $(PBGL_DIR)/config_pbgl.make
main.exe: $(PBGL_LIB)

# bake.py also regenerates the binary assets referenced by assets.s.
$(CURDIR)/assets.obj: $(wildcard generated/*.bin)

$(CURDIR)/gpu_render.obj: generated/gpu.h
