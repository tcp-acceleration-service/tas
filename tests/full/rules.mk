include mk/subdir_pre.mk

FULLTEST_OBJS := tests/full/tas_linux.o tests/full/fulltest.o
FTWRAP_OBJS := tests/full/wrapper.o tests/full/fulltest.o

FTWRAP := tests/full/wrapper

# full unit tests
FULLTESTS := \
  tests/full/tas_linux


tests/full/%.o: CPPFLAGS+=-Ilib/tas/include
tests/full/tas_linux: tests/full/tas_linux.o tests/full/fulltest.o lib/libtas.so

tests-full: $(FULLTESTS) $(FTWRAP)

$(FTWRAP): $(FTWRAP_OBJS)

test-full-wrapdeps: $(FTWRAP) lib/libtas_interpose.so tas/tas

# run full tests that run full TAS
run-tests-full-simple: $(FULLTESTS) tas/tas
	tests/full/tas_linux

run-tests-full: run-tests-full-simple

#########################

dir := $(d)/memcached
include $(dir)/rules.mk

dir := $(d)/redis
include $(dir)/rules.mk

dir := $(d)/nodejs
include $(dir)/rules.mk

#########################

DEPS += $(FULLTEST_OBJS:.o=.d) $(FTWRAP_OBJS:.o=.d)
CLEAN += $(FULLTEST_OBJS) $(FTWRAP_OBJS) $(FULLTESTS) $(FTWRAP)

.PHONY: tests-full run-tests-full run-tests-full-simple test-full-wrapdeps

include mk/subdir_post.mk
