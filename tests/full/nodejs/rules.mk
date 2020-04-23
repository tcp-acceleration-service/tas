include mk/subdir_pre.mk

ft_nodejs_parentdir := $(TEST_DISTFILES)/full-nodejs

###################################
# Build nodejs

ft_nodejs_ver := 12.16.2
ft_nodejs_tar := $(ft_nodejs_parentdir)/node-x$(ft_nodejs_ver).tar.gz
ft_nodejs_build := $(ft_nodejs_parentdir)/node-v$(ft_nodejs_ver)
ft_nodejs_prefix := $(ft_nodejs_parentdir)/prefix
ft_nodejs_server := $(ft_nodejs_prefix)/bin/node
ft_nodejs_config := $(d)/index.js

# Download nodejs tarball
$(ft_nodejs_tar):
	mkdir -p $(dir $@)
	wget -O $@ https://nodejs.org/dist/v$(ft_nodejs_ver)/node-v$(ft_nodejs_ver).tar.gz

# Extract nodejs tarball
$(ft_nodejs_build): $(ft_nodejs_tar)
	tar xf $< -C $(ft_nodejs_parentdir)
	touch $@

# Build nodejs
$(ft_nodejs_server): $(ft_nodejs_build)
	cd $(ft_nodejs_build) && ./configure \
	  --prefix=$(abspath $(ft_nodejs_prefix)) \
	  --v8-non-optimized-debug --without-ssl --without-npm \
	  --debug-node
	$(MAKE) -C $(ft_nodejs_build) install

# Generate tas-specific config file
$(ft_nodejs_config): $(ft_nodejs_build)/nodejs.conf
	sed \
	  -e 's/^bind .*/bind 0.0.0.0/' \
	  -e 's:^dir .*:dir /tmp/:' \
	  -e '/^save .*/d' \
	  $< >$@

###################################
# Build wrk2

ft_nodejs_wrk_ver := 44a94c17d8e6a0bac8559b53da76848e430cb7a7
ft_nodejs_wrk_build := $(ft_nodejs_parentdir)/wrk2
ft_nodejs_client := $(ft_nodejs_parentdir)/wrk2/wrk

$(ft_nodejs_wrk_build):
	rm -rf $@
	git clone --depth 1 https://github.com/giltene/wrk2.git $@
	cd $@ && git checkout $(ft_nodejs_wrk_ver)
	sed -i -s 's/CFLAGS *:=.*/\0 -g/' $(ft_nodejs_wrk_build)/Makefile

$(ft_nodejs_client): $(ft_nodejs_wrk_build)
	$(MAKE) -C $(ft_nodejs_wrk_build)

###################################

# if NODE_SYSTEM is set, we don't compile or own node instance
ifeq ($(NODE_SYSTEM),y)
  ft_nodejs_server := node
else
tests-full-nodejs: $(ft_nodejs_server)
endif

tests-full-nodejs: $(ft_nodejs_client)
#tests-full: tests-full-nodejs

# Here nodejs runs in TAS and the client on Linux
run-tests-full-nodejs-server: tests-full-nodejs test-full-wrapdeps
	$(FTWRAP) -d 1000 \
		-P '$(ft_nodejs_server) $(ft_nodejs_config)' \
		-c '$(ft_nodejs_client) -t2 -c100 -d10s -R2000 http://$$TAS_IP:3000'

# Here the client runs in Linux
run-tests-full-nodejs-client: tests-full-nodejs test-full-wrapdeps
		$(FTWRAP) -d 1000 \
		-C '$(ft_nodejs_server) $(ft_nodejs_config)' \
		-p '$(ft_nodejs_client) -t2 -c100 -d10s -R2000 http://$$LINUX_IP:3000'

run-tests-full-nodejs: run-tests-full-nodejs-server run-tests-full-nodejs-client
#run-tests-full: run-tests-full-nodejs

.PHONY: tests-full-nodejs run-tests-full-nodejs run-tests-full-nodejs-server run-tests-full-nodejs-client

include mk/subdir_post.mk
