include mk/subdir_pre.mk

ft_netperf_parentdir := $(TEST_DISTFILES)/full-netperf

###################################
# Build netperf

ft_netperf_ver := 2.7.0
ft_netperf_tar := $(ft_netperf_parentdir)/netperf-$(ft_netperf_ver).tar.gz
ft_netperf_build := $(ft_netperf_parentdir)/netperf-netperf-$(ft_netperf_ver)
ft_netperf_server := $(ft_netperf_build)/src/netserver
ft_netperf_client := $(ft_netperf_build)/src/netperf

# Download netperf tarball
$(ft_netperf_tar):
	mkdir -p $(dir $@)
	wget -O $@ https://github.com/HewlettPackard/netperf/archive/refs/tags/netperf-$(ft_netperf_ver).tar.gz

# Extract netperf tarball
$(ft_netperf_build): $(ft_netperf_tar)
	tar xf $< -C $(ft_netperf_parentdir)
	touch $(@)

# Build netperf
$(ft_netperf_server) $(ft_netperf_client): $(ft_netperf_build)
	(cd $(ft_netperf_build) && ./configure CFLAGS=-fcommon)
	$(MAKE) -C $(ft_netperf_build)
	touch $(ft_netperf_server) $(ft_netperf_client)

###################################

tests-full-netperf: $(ft_netperf_server) $(ft_netperf_client)
tests-full: tests-full-netperf

run-tests-full-netperf-server: tests-full-netperf test-full-wrapdeps
	$(FTWRAP) -d 500 \
		-P '$(ft_netperf_server)' \
		-c '$(ft_netperf_client) -H $$TAS_IP -l 100 -t TCP_STREAM'

run-tests-full-netperf-client: tests-full-netperf test-full-wrapdeps
	$(FTWRAP) -d 500 \
		-C '$(ft_netperf_server)' \
		-p '$(ft_netperf_client) -H $$LINUX_IP -l 100 -t TCP_STREAM'

run-tests-full-netperf: run-tests-full-netperf-server run-tests-full-netperf-client
run-tests-full: run-tests-full-netperf

.PHONY: tests-full-netperf run-tests-full-netperf \
    run-tests-full-netperf-server run-tests-full-netperf-client

include mk/subdir_post.mk
