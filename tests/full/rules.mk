include mk/subdir_pre.mk

FULLTEST_OBJS := tests/full/tas_linux.o tests/full/fulltest.o

# full unit tests
FULLTESTS := \
  tests/full/tas_linux


tests/full/%.o: CPPFLAGS+=-Ilib/tas/include
tests/full/tas_linux: tests/full/tas_linux.o tests/full/fulltest.o lib/libtas.so

tests: $(FULLTESTS)

# run full tests that run full TAS
run-tests-full: $(FULLTESTS) tas/tas
	tests/full/tas_linux


DEPS += $(FULLTEST_OBJS:.o=.d)
CLEAN += $(FULLTEST_OBJS) $(FULLTESTS)

.PHONY: run-tests-full

include mk/subdir_post.mk
