XBE_TITLE = AssaultCube
GEN_XISO = assaultcube.iso
NXDK_SDL = y
NXDK_CXX = y
NXDK_CXXFLAGS += -O2 -Wall -fno-rtti
NXDK_DIR ?= $(CURDIR)/../../nxdk
SRCS = $(addprefix $(CURDIR)/,platform_nxdk.c match.cpp navigation.c render.c assets.s)
NXDK_CFLAGS += -O2 -Wall
include $(NXDK_DIR)/Makefile

# bake.py also regenerates the binary assets referenced by assets.s.
$(CURDIR)/assets.obj: $(wildcard generated/*.bin)
