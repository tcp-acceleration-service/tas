include mk/subdir_pre.mk

objs_top := tas.o config.o shm.o blocking.o
objs_sp := kernel.o packetmem.o appif.o appif_connect.o appif_ctx.o \
 nicif.o cc.o tcp.o arp.o routing.o kni.o
objs_fp := fastemu.o network.o qman.o trace.o \
 fast_kernel.o fast_appctx.o fast_flows.o

TAS_OBJS := $(addprefix $(d)/, \
  $(objs_top) \
  $(addprefix slow/, $(objs_sp)) \
  $(addprefix fast/, $(objs_fp)))

exec := $(d)/tas

TAS_CPPFLAGS := -Iinclude/ -I$(d)/include/ $(DPDK_CPPFLAGS)
TAS_CFLAGS := $(DPDK_CFLAGS)

$(TAS_OBJS): CPPFLAGS += $(TAS_CPPFLAGS)
$(TAS_OBJS): CFLAGS += $(TAS_CFLAGS)

$(exec): LDFLAGS += $(DPDK_LDFLAGS)
$(exec): LDLIBS += $(DPDK_LDLIBS) 
$(exec): $(TAS_OBJS) $(LIB_UTILS_OBJS) $(LIB_TAS_OBJS)

DEPS += $(TAS_OBJS:.o=.d)
CLEAN += $(TAS_OBJS) $(exec)
TARGETS += $(exec)

include mk/subdir_post.mk
