include mk/subdir_pre.mk

ft_memcached_parentdir := $(d)/
ft_memcached_prefix_rel := $(d)/prefix
ft_memcached_prefix := $(realpath $(d))/prefix

DISTCLEAN += $(ft_memcached_prefix)

###################################
# Build libevent

ft_memcached_lev_ver := 2.1.11-stable
ft_memcached_lev_tar := $(d)/libevent-$(ft_memcached_lev_ver).tar.gz
ft_memcached_lev_build := $(d)/libevent-$(ft_memcached_lev_ver)
ft_memcached_lev_dep := $(ft_memcached_prefix_rel)/lib/libevent.a

# Download libevent tarball
$(ft_memcached_lev_tar):
	wget -O $@ https://github.com/libevent/libevent/releases/download/release-$(ft_memcached_lev_ver)/libevent-$(ft_memcached_lev_ver).tar.gz

# Extract libevent tarball
$(ft_memcached_lev_build): $(ft_memcached_lev_tar)
	tar xf $< -C $(ft_memcached_parentdir)

# Build libevent
$(ft_memcached_lev_dep): $(ft_memcached_lev_build)
	cd $(ft_memcached_lev_build) && \
	  ./configure --disable-openssl --prefix=$(ft_memcached_prefix) \
	    --enable-static --disable-shared
	$(MAKE) -C $(ft_memcached_lev_build) install

DISTCLEAN += $(ft_memcached_lev_build) $(ft_memcached_lev_tar)

###################################
# Build libmemcached (for client)

ft_memcached_lm_ver := 1.0.18
ft_memcached_lm_tar := $(d)/libmemcached-$(ft_memcached_lm_ver).tar.gz
ft_memcached_lm_build := $(d)/libmemcached-$(ft_memcached_lm_ver)
ft_memcached_lm_bin := $(ft_memcached_prefix_rel)/bin/memaslap

# Download libmemcached tarball
$(ft_memcached_lm_tar):
	wget -O $@ https://launchpad.net/libmemcached/1.0/$(ft_memcached_lm_ver)/+download/libmemcached-$(ft_memcached_lm_ver).tar.gz

# Extract memcached tarball
$(ft_memcached_lm_build): $(ft_memcached_lm_tar)
	tar xf $< -C $(ft_memcached_parentdir)

# Build memcached
$(ft_memcached_lm_bin): $(ft_memcached_lm_build) $(ft_memcached_lev_dep)
	cd $(ft_memcached_lm_build) && \
	  ./configure --enable-memaslap --disable-dtrace \
	    --enable-static --disable-shared \
	    --prefix=$(ft_memcached_prefix) CXXFLAGS=-fpermissive \
	    CPPFLAGS=-I$(ft_memcached_prefix)/include \
	    LDFLAGS=-L$(ft_memcached_prefix)/lib
	$(MAKE) -C $(ft_memcached_lm_build) install

DISTCLEAN += $(ft_memcached_lm_build) $(ft_memcached_lm_tar)

###################################
# Build memcached

ft_memcached_ver := 1.5.21
ft_memcached_tar := $(d)/memcached-$(ft_memcached_ver).tar.gz
ft_memcached_build := $(d)/memcached-$(ft_memcached_ver)
ft_memcached_bin := $(ft_memcached_prefix_rel)/bin/memcached

# Download memcached tarball
$(ft_memcached_tar):
	wget -O $@ http://www.memcached.org/files/memcached-$(ft_memcached_ver).tar.gz

# Extract memcached tarball
$(ft_memcached_build): $(ft_memcached_tar)
	tar xf $< -C $(ft_memcached_parentdir)

# Build memcached
$(ft_memcached_bin): $(ft_memcached_build) $(ft_memcached_lev_dep)
	cd $(ft_memcached_build) && \
	  ./configure --disable-docs --prefix=$(ft_memcached_prefix) \
	      --with-libevent=$(ft_memcached_prefix)
	$(MAKE) -C $(ft_memcached_build) install

DISTCLEAN += $(ft_memcached_build) $(ft_memcached_tar)

###################################

# Here memcached runs in TAS and the client on Linux
run-tests-full-memcached-server: $(ft_memcached_bin) $(ft_memcached_lm_bin) $(FTWRAP)
	$(FTWRAP) -d 1000 \
		-P '$(ft_memcached_bin) -U 0 -u root' \
		-c '$(ft_memcached_lm_bin) -s $$TAS_IP -t 10s \
		      -F $(ft_memcached_parentdir)/memaslap.cnf'

# Here the client runs in Linux
run-tests-full-memcached-client: $(ft_memcached_bin) $(ft_memcached_lm_bin) $(FTWRAP)
	$(FTWRAP) -d 1000 \
		-C '$(ft_memcached_bin) -U 0 -u root' \
		-p '$(ft_memcached_lm_bin) -s $$LINUX_IP -t 10s \
		      -F $(ft_memcached_parentdir)/memaslap.cnf'

run-tests-full-memcached: run-tests-full-memcached-server run-tests-full-memcached-client
run-tests-full: run-tests-full-memcached


.PHONY: run-tests-full-memcached

include mk/subdir_post.mk
