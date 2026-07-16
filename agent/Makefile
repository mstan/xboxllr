XBE_TITLE = xlle-agent
GEN_XISO = $(XBE_TITLE).iso
SRCS = $(CURDIR)/main.c

# nxdk lives beside the framework repo in the workspace (out of tree; see docs/INPUTS.md).
NXDK_DIR ?= $(CURDIR)/../../refs/nxdk
NXDK_NET = y

include $(NXDK_DIR)/Makefile
