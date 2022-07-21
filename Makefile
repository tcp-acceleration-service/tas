-include Makefile.local

##############################################################################
# Compile, link, and install flags

CPPFLAGS += -Iinclude/
CPPFLAGS += $(EXTRA_CPPFLAGS)
CFLAGS += -std=gnu99 -O3 -g -Wall -march=native -fno-omit-frame-pointer
CFLAGS += -Wno-address-of-packed-member
CFLAGS += $(EXTRA_CFLAGS)
CFLAGS_SHARED += $(CFLAGS) -fPIC
LDFLAGS += -pthread -g
LDFLAGS += $(EXTRA_LDFLAGS)
LDLIBS += -lm -lpthread -lrt -ldl
LDLIBS += $(EXTRA_LDLIBS)

PKG_CONFIG ?= pkg-config
PREFIX ?= /usr/local
SBINDIR ?= $(PREFIX)/sbin
LIBDIR ?= $(PREFIX)/lib
INCDIR ?= $(PREFIX)/include


##############################################################################
# DPDK configuration

DPDK_CPPFLAGS ?= $(shell $(PKG_CONFIG) --cflags libdpdk)
DPDK_LDLIBS ?= $(shell $(PKG_CONFIG) --static --libs libdpdk)

##############################################################################

include mk/recipes.mk

DEPS :=
CLEAN :=
DISTCLEAN :=
TARGETS :=

# Subdirectories

dir := lib
include $(dir)/rules.mk

dir := tas
include $(dir)/rules.mk

dir := proxy
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

distclean:
	rm -rf $(DISTCLEAN) $(CLEAN) $(DEPS)

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
.PHONY: all distclean clean install uninstall

# Include dependencies
-include $(DEPS)
