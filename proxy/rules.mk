include mk/subdir_pre.mk

objs_proxy := channel.o shmring.o
objs_host := host.o ivshmem.o
objs_guest := guest.o ivshmem.o vfio.o flextcp.o

PROXY_HOST_OBJS := $(addprefix $(d)/, \
  $(objs_proxy) \
  $(addprefix host/, $(objs_host)))

PROXY_GUEST_OBJS := $(addprefix $(d)/, \
  $(objs_proxy) \
  $(addprefix guest/, $(objs_guest)))

host := $(d)/host/host
guest := $(d)/guest/guest

PROXY_HOST_CPPFLAGS := -Iinclude/ -Itas/include/ -Ilib/tas/include/
PROXY_HOST_CFLAGS := $()

PROXY_GUEST_CPPFLAGS := -Iinclude/ -Itas/include/ -Ilib/tas/include/
PROXY_GUEST_CFLAGS := $()

$(PROXY_HOST_OBJS): CPPFLAGS += $(PROXY_HOST_CPPFLAGS)
$(PROXY_HOST_OBJS): CFLAGS += $(PROXY_HOST_CFLAGS)

$(PROXY_GUEST_OBJS): CPPFLAGS += $(PROXY_GUEST_CPPFLAGS)
$(PROXY_GUEST_OBJS): CFLAGS += $(PROXY_GUEST_CFLAGS)

$(host): $(PROXY_HOST_OBJS) $(LIB_UTILS_OBJS) $(LIB_TAS_OBJS)
$(guest): $(PROXY_GUEST_OBJS) $(LIB_UTILS_OBJS) $(LIB_TAS_OBJS)

DEPS += $(PROXY_HOST_OBJS:.o=.d)
DEPS += $(PROXY_GUEST_OBJS:.o=.d)
CLEAN += $(PROXY_HOST_OBJS) $(PROXY_GUEST_OBJS) $(host) $(guest)
TARGETS += $(host) $(guest)

include mk/subdir_post.mk