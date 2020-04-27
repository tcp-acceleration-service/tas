include mk/subdir_pre.mk

ft_memcached_dir := $(d)
ft_memcached_parentdir := $(TEST_DISTFILES)/full-memcached
ft_memcached_prefix_rel := $(ft_memcached_parentdir)/prefix
ft_memcached_prefix := $(abspath $(ft_memcached_prefix_rel))

###################################
# Build libevent

ft_memcached_lev_ver := 2.1.11-stable
ft_memcached_lev_tar := $(ft_memcached_parentdir)/libevent-$(ft_memcached_lev_ver).tar.gz
ft_memcached_lev_build := $(ft_memcached_parentdir)/libevent-$(ft_memcached_lev_ver)
ft_memcached_lev_dep := $(ft_memcached_prefix_rel)/lib/libevent.a

# Download libevent tarball
$(ft_memcached_lev_tar):
	mkdir -p $(dir $@)
	wget -O $@ https://github.com/libevent/libevent/releases/download/release-$(ft_memcached_lev_ver)/libevent-$(ft_memcached_lev_ver).tar.gz

# Extract libevent tarball
$(ft_memcached_lev_build): $(ft_memcached_lev_tar)
	tar xf $< -C $(ft_memcached_parentdir)

# Build libevent
$(ft_memcached_lev_dep): $(ft_memcached_lev_build)
	cd $(ft_memcached_lev_build) && \
	  ./configure --disable-openssl --prefix=$(ft_memcached_prefix) \
	    --enable-static --disable-shared CFLAGS='-g' LDFLAGS='-g'
	$(MAKE) -C $(ft_memcached_lev_build) install

###################################
# Build libmemcached (for client)

ft_memcached_lm_ver := 1.0.18
ft_memcached_lm_tar := $(ft_memcached_parentdir)/libmemcached-$(ft_memcached_lm_ver).tar.gz
ft_memcached_lm_build := $(ft_memcached_parentdir)/libmemcached-$(ft_memcached_lm_ver)
ft_memcached_lm_bin := $(ft_memcached_prefix_rel)/bin/memslap

# Download libmemcached tarball
$(ft_memcached_lm_tar):
	mkdir -p $(dir $@)
	wget -O $@ https://launchpad.net/libmemcached/1.0/$(ft_memcached_lm_ver)/+download/libmemcached-$(ft_memcached_lm_ver).tar.gz

# Extract memcached tarball
$(ft_memcached_lm_build): $(ft_memcached_lm_tar)
	tar xf $< -C $(ft_memcached_parentdir)

# Build memcached
$(ft_memcached_lm_bin): $(ft_memcached_lm_build) $(ft_memcached_lev_dep)
	cd $(ft_memcached_lm_build) && \
	  ./configure --enable-memaslap --disable-dtrace \
	    --enable-static --disable-shared \
	    --prefix=$(ft_memcached_prefix) CXXFLAGS='-g -fpermissive' \
	    CFLAGS='-g -fpermissive' \
	    CPPFLAGS='-g -I$(ft_memcached_prefix)/include -DSIGSEGV_NO_AUTO_INIT'\
	    LDFLAGS=-L$(ft_memcached_prefix)/lib
	$(MAKE) -C $(ft_memcached_lm_build) install

###################################
# Build memcached

ft_memcached_ver := 1.5.21
ft_memcached_tar := $(ft_memcached_parentdir)/memcached-$(ft_memcached_ver).tar.gz
ft_memcached_build := $(ft_memcached_parentdir)/memcached-$(ft_memcached_ver)
ft_memcached_bin := $(ft_memcached_prefix_rel)/bin/memcached

# Download memcached tarball
$(ft_memcached_tar):
	mkdir -p $(dir $@)
	wget -O $@ http://www.memcached.org/files/memcached-$(ft_memcached_ver).tar.gz

# Extract memcached tarball
$(ft_memcached_build): $(ft_memcached_tar)
	tar xf $< -C $(ft_memcached_parentdir)
	touch $@

# Build memcached
$(ft_memcached_bin): $(ft_memcached_build) $(ft_memcached_lev_dep)
	cd $(ft_memcached_build) && \
	  ./configure --disable-docs --prefix=$(ft_memcached_prefix) \
	      --with-libevent=$(ft_memcached_prefix) \
	      CFLAGS='-g' LDFLAGS='-g'
	$(MAKE) -C $(ft_memcached_build) install

###################################

tests-full: $(ft_memcached_bin) $(ft_memcached_lm_bin)

# Here memcached runs in TAS and the client on Linux
run-tests-full-memcached-server: $(ft_memcached_bin) $(ft_memcached_lm_bin) test-full-wrapdeps
	$(FTWRAP) -d 1000 \
		-P '$(ft_memcached_bin) -U 0 -u root' \
		-c '$(ft_memcached_lm_bin) --servers=$$TAS_IP --tcp-nodelay \
		--execute-number=500'

# Here the client runs in Linux
run-tests-full-memcached-client: $(ft_memcached_bin) $(ft_memcached_lm_bin) test-full-wrapdeps
	$(FTWRAP) -d 1000 \
		-C '$(ft_memcached_bin) -U 0 -u root' \
		-p '$(ft_memcached_lm_bin) --servers=$$LINUX_IP --tcp-nodelay \
		--execute-number=500'

# Here the client runs in TAS
run-tests-full-memcached-clientnb: $(ft_memcached_bin) $(ft_memcached_lm_bin) test-full-wrapdeps
	$(FTWRAP) -d 1000 \
		-C '$(ft_memcached_bin) -U 0 -u root' \
		-p '$(ft_memcached_lm_bin) --servers=$$LINUX_IP --tcp-nodelay \
		--execute-number=500 --non-blocking'


run-tests-full-memcached: run-tests-full-memcached-server run-tests-full-memcached-client run-tests-full-memcached-clientnb
run-tests-full: run-tests-full-memcached


.PHONY: run-tests-full-memcached

include mk/subdir_post.mk
