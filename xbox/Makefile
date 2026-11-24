XBE_TITLE = AssaultCube
GEN_XISO = assaultcube.iso
NXDK_SDL = y
NXDK_DIR ?= $(CURDIR)/../../nxdk
SRCS = $(CURDIR)/main.c $(CURDIR)/assets.s
NXDK_CFLAGS += -O2 -Wall
include $(NXDK_DIR)/Makefile

# bake.py also regenerates the binary assets referenced by assets.s.
$(CURDIR)/assets.obj: $(wildcard generated/*.bin)
