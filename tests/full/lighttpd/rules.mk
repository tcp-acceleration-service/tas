include mk/subdir_pre.mk

ft_lighttpd_parentdir := $(TEST_DISTFILES)/full-lighttpd

###################################
# Build lighttpd

ft_lighttpd_ver := 1.4.55
ft_lighttpd_tar := $(ft_lighttpd_parentdir)/lighttpd-$(ft_lighttpd_ver).tar.gz
ft_lighttpd_build := $(ft_lighttpd_parentdir)/lighttpd-$(ft_lighttpd_ver)
ft_lighttpd_server := $(ft_lighttpd_build)/src/lighttpd
ft_lighttpd_config := $(d)/lighttpd.conf

# Download lighttpd tarball
$(ft_lighttpd_tar):
	mkdir -p $(dir $@)
	wget -O $@ https://download.lighttpd.net/lighttpd/releases-1.4.x/lighttpd-$(ft_lighttpd_ver).tar.gz

# Extract lighttpd tarball
$(ft_lighttpd_build): $(ft_lighttpd_tar)
	tar xf $< -C $(ft_lighttpd_parentdir)
	touch $(@)

# Build lighttpd
$(ft_lighttpd_server): $(ft_lighttpd_build)
	(cd $(ft_lighttpd_build) && ./configure --disable-ipv6 --without-bzip2)
	$(MAKE) -C $(ft_lighttpd_build)
	touch $@

###################################
# Build wrk2

ft_lighttpd_wrk_ver := 44a94c17d8e6a0bac8559b53da76848e430cb7a7
ft_lighttpd_wrk_build := $(ft_lighttpd_parentdir)/wrk2
ft_lighttpd_client := $(ft_lighttpd_parentdir)/wrk2/wrk

$(ft_lighttpd_wrk_build):
	rm -rf $@
	git clone --depth 1 https://github.com/giltene/wrk2.git $@
	cd $@ && git checkout $(ft_lighttpd_wrk_ver)
	sed -i -s 's/CFLAGS *:=.*/\0 -g/' $(ft_lighttpd_wrk_build)/Makefile

$(ft_lighttpd_client): $(ft_lighttpd_wrk_build)
	$(MAKE) -C $(ft_lighttpd_wrk_build)

###################################

tests-full-lighttpd: $(ft_lighttpd_server) $(ft_lighttpd_client)
tests-full: tests-full-lighttpd

run-tests-full-lighttpd-server: tests-full-lighttpd test-full-wrapdeps
	$(FTWRAP) -d 500 \
		-P '$(ft_lighttpd_server) -D -m $(ft_lighttpd_build)/src/.libs/\
		    -f $(ft_lighttpd_config)' \
		-c '$(ft_lighttpd_client) -t2 -c100 -d10s -R2000 \
		    http://$$TAS_IP:8000/lighttpd.conf'

run-tests-full-lighttpd-client: tests-full-lighttpd test-full-wrapdeps
	$(FTWRAP) -d 500 \
		-C '$(ft_lighttpd_server) -D -m $(ft_lighttpd_build)/src/.libs/\
		    -f $(ft_lighttpd_config)' \
		-p '$(ft_lighttpd_client) -t2 -c100 -d10s -R2000 \
		    http://$$LINUX_IP:8000/lighttpd.conf'

run-tests-full-lighttpd: run-tests-full-lighttpd-server run-tests-full-lighttpd-client
run-tests-full: run-tests-full-lighttpd

.PHONY: tests-full-lighttpd run-tests-full-lighttpd \
    run-tests-full-lighttpd-server run-tests-full-lighttpd-client

include mk/subdir_post.mk
