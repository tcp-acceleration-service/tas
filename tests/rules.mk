include mk/subdir_pre.mk

# simple test programs
TESTS_NONE := \
  tests/usocket_epoll_eof \
  tests/usocket_shutdown \

# simple test programs linking against libtas
TESTS_LIBTAS := \
  tests/lowlevel \
  tests/lowlevel_echo \
  tests/bench_ll_echo \

# simple test programs linking against libtas_sockets
TESTS_SOCKETS := \
  tests/usocket_accept \
  tests/usocket_connect \
  tests/usocket_accrx \
  tests/usocket_conntx \
  tests/usocket_conntx_large \
  tests/usocket_move \

# automated unittests
TESTS_AUTO := \
  tests/libtas/tas_ll \
  tests/libtas/tas_sockets \
  tests/tas_unit/fastpath

# full unit tests
TESTS_AUTO_FULL := \
  tests/full/tas_linux \

TESTS := $(TESTS_NONE) $(TESTS_LIBTAS) $(TESTS_SOCKETS) $(TESTS_AUTO) \
  $(TESTS_AUTO_FULL)
TEST_OBJS := $(addsuffix .o, $(TESTS)) \
  tests/testutils.o tests/libtas/harness.o tests/full/fulltest.o

# tests linking against libtas
$(TESTS_LIBTAS): CPPFLAGS += -Ilib/tas/include/
$(foreach t,$(TESTS_LIBTAS),$(eval $(t): $(t).o lib/libtas.so))

# tests linking against libsockets
$(TESTS_SOCKETS): CPPFLAGS += -Ilib/sockets/include/
$(foreach t,$(TESTS_SOCKETS),$(eval $(t): $(t).o lib/libtas_sockets.so))


tests/libtas/tas_ll: CPPFLAGS += -Ilib/tas/include/
tests/libtas/tas_ll: tests/libtas/tas_ll.o tests/libtas/harness.o \
  tests/testutils.o lib/libtas.so

tests/libtas/tas_sockets: CPPFLAGS += -Ilib/sockets/include/
tests/libtas/tas_sockets: tests/libtas/tas_sockets.o tests/libtas/harness.o \
  tests/testutils.o lib/libtas_sockets.so

tests/tas_unit/fastpath: CPPFLAGS+= -Itas/include $(DPDK_CPPFLAGS)
tests/tas_unit/fastpath: CFLAGS+= $(DPDK_CFLAGS)
tests/tas_unit/fastpath: LDFLAGS+= $(DPDK_LDFLAGS)
tests/tas_unit/fastpath: LDLIBS+= -lrte_eal
tests/tas_unit/fastpath: tests/tas_unit/fastpath.o tests/testutils.o \
  tas/fast/fast_flows.o

tests/full/%.o: CPPFLAGS+=-Ilib/tas/include
tests/full/tas_linux: tests/full/tas_linux.o tests/full/fulltest.o lib/libtas.so

# build tests
tests: $(TESTS)

# run all simple testcases
run-tests: $(TESTS_AUTO)
	tests/libtas/tas_ll
	tests/libtas/tas_sockets
	tests/tas_unit/fastpath

# run full tests that run full TAS
run-tests-full: $(TESTS_AUTO_FULL) tas/tas
	tests/full/tas_linux

DEPS += $(TEST_OBJS:.o=.d)
CLEAN += $(TEST_OBJS) $(TESTS)

.PHONY: tests run-tests run-tests-full

include mk/subdir_post.mk
