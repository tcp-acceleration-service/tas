-include Makefile.local

##############################################################################
# Compile, link, and install flags

CPPFLAGS += -Iinclude/
CPPFLAGS += $(EXTRA_CPPFLAGS)
CFLAGS += -std=gnu99 -O3 -g -Wall -Werror -march=native -fno-omit-frame-pointer
CFLAGS += $(EXTRA_CFLAGS)
CFLAGS_SHARED += $(CFLAGS) -fPIC
LDFLAGS += -pthread -g
LDFLAGS += $(EXTRA_LDFLAGS)
LDLIBS += -lm -lpthread -lrt -ldl
LDLIBS += $(EXTRA_LDLIBS)

PREFIX ?= /usr/local
SBINDIR ?= $(PREFIX)/sbin
LIBDIR ?= $(PREFIX)/lib
INCDIR ?= $(PREFIX)/include


##############################################################################
# DPDK configuration

# Prefix for dpdk
RTE_SDK ?= /usr/
# mpdts to compile
DPDK_PMDS ?= ixgbe i40e tap virtio

DPDK_CPPFLAGS += -I$(RTE_SDK)/include -I$(RTE_SDK)/include/dpdk \
  -I$(RTE_SDK)/include/x86_64-linux-gnu/dpdk/
DPDK_LDFLAGS+= -L$(RTE_SDK)/lib/
DPDK_LDLIBS+= \
  -Wl,--whole-archive \
   $(addprefix -lrte_pmd_,$(DPDK_PMDS)) \
  -lrte_eal \
  -lrte_mempool \
  -lrte_mempool_ring \
  -lrte_hash \
  -lrte_ring \
  -lrte_kvargs \
  -lrte_ethdev \
  -lrte_mbuf \
  -lnuma \
  -lrte_bus_pci \
  -lrte_pci \
  -lrte_cmdline \
  -lrte_timer \
  -lrte_net \
  -lrte_kni \
  -lrte_bus_vdev \
  -lrte_gso \
  -Wl,--no-whole-archive \
  -ldl \
  $(EXTRA_LIBS_DPDK)


##############################################################################

include mk/recipes.mk

DEPS :=
CLEAN :=
TARGETS :=

# Subdirectories

dir := lib
include $(dir)/rules.mk

dir := tas
include $(dir)/rules.mk

dir := tools
include $(dir)/rules.mk

dir := tests
include $(dir)/rules.mk

dir := doc
include $(dir)/rules.mk


##############################################################################
# Top level targets

all: $(TARGETS)

clean:
	rm -rf $(CLEAN) $(DEPS)

install: tas/tas lib/libtas_sockets.so lib/libtas_interpose.so \
  lib/libtas.so tools/statetool
	mkdir -p $(DESTDIR)$(SBINDIR)
	cp tas/tas $(DESTDIR)$(SBINDIR)/tas
	cp tools/statetool $(DESTDIR)$(SBINDIR)/tas-statetool
	mkdir -p $(DESTDIR)$(LIBDIR)
	cp lib/libtas_interpose.so $(DESTDIR)$(LIBDIR)/libtas_interpose.so
	cp lib/libtas_sockets.so $(DESTDIR)$(LIBDIR)/libtas_sockets.so
	cp lib/libtas.so $(DESTDIR)$(LIBDIR)/libtas.so

uninstall:
	rm -f $(DESTDIR)$(SBINDIR)/tas
	rm -f $(DESTDIR)$(SBINDIR)/tas-statetool
	rm -f $(DESTDIR)$(LIBDIR)/libtas_interpose.so
	rm -f $(DESTDIR)$(LIBDIR)/libtas_sockets.so
	rm -f $(DESTDIR)$(LIBDIR)/libtas.so


.DEFAULT_GOAL := all
.PHONY: all clean

# Include dependencies
-include $(DEPS)
