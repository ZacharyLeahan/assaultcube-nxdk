ENET_DIR := $(CURDIR)/../source/enet
ENET_SRC := $(addprefix $(CURDIR)/generated/lan/,callbacks.c host.c list.c packet.c peer.c protocol.c)
SRCS += $(ENET_SRC) $(CURDIR)/lan/enet_socket.c
NXDK_NET = y
NXDK_CFLAGS += -I$(ENET_DIR)/include
NXDK_CXXFLAGS += -I$(ENET_DIR)/include
SRCS += $(CURDIR)/lan/lan.cpp
