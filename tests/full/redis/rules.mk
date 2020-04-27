include mk/subdir_pre.mk

ft_redis_parentdir := $(TEST_DISTFILES)/full-redis

###################################
# Build redis

ft_redis_ver := 5.0.8
ft_redis_tar := $(ft_redis_parentdir)/redis-$(ft_redis_ver).tar.gz
ft_redis_build := $(ft_redis_parentdir)/redis-$(ft_redis_ver)
ft_redis_server := $(ft_redis_build)/src/redis-server
ft_redis_client := $(ft_redis_build)/src/redis-benchmark
ft_redis_config := $(ft_redis_parentdir)/redis.tas.conf

# Download redis tarball
$(ft_redis_tar):
	mkdir -p $(dir $@)
	wget -O $@ http://download.redis.io/releases/redis-${ft_redis_ver}.tar.gz

# Extract redis tarball
$(ft_redis_build): $(ft_redis_tar)
	tar xf $< -C $(ft_redis_parentdir)
	touch $(@)

# Build redis
$(ft_redis_build)/build_success: $(ft_redis_build)
	$(MAKE) -C $(ft_redis_build) clean
	$(MAKE) -C $(ft_redis_build) CFLAGS=-g CXXFLAGS=-g
	touch $(ft_redis_build)/redis.conf
	touch $@

$(ft_redis_server) $(ft_redis_client) $(ft_redis_build)/redis.conf: $(ft_redis_build)/build_success
	true

# Generate tas-specific config file
$(ft_redis_config): $(ft_redis_build)/redis.conf
	sed \
	  -e 's/^bind .*/bind 0.0.0.0/' \
	  -e 's:^dir .*:dir /tmp/:' \
	  -e '/^save .*/d' \
	  $< >$@

###################################

tests-full-redis: $(ft_redis_server) $(ft_redis_client) $(ft_redis_config)
tests-full: tests-full-redis

# Here redis runs in TAS and the client on Linux
run-tests-full-redis-server: tests-full-redis test-full-wrapdeps
	$(FTWRAP) -d 1000 \
		-P '$(ft_redis_server) $(ft_redis_config)' \
		-c '$(ft_redis_client) -h $$TAS_IP -n 100 -c 1'

# Here the client runs in Linux
run-tests-full-redis-client: tests-full-redis test-full-wrapdeps
		$(FTWRAP) -d 1000 \
		-C '$(ft_redis_server) $(ft_redis_config)' \
		-p '$(ft_redis_client) -h $$LINUX_IP -n 100 -c 1'

run-tests-full-redis: run-tests-full-redis-server run-tests-full-redis-client
run-tests-full: run-tests-full-redis

.PHONY: tests-full-redis run-tests-full-redis run-tests-full-redis-server run-tests-full-redis-client

include mk/subdir_post.mk
