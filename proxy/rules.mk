include mk/subdir_pre.mk

objs_proxy := guest_main.o host_main.o channel.o shmring.o
objs_host := ivshmem.o

PROXY_OBJS := $(addprefix $(d)/, \
  $(objs_proxy) \
  $(addprefix host/, $(objs_host)))

exec := $(d)/host_main

PROXY_CPPFLAGS := -Iinclude/ -Ilib/tas/include -I/lib/tas
PROXY_CFLAGS := $()

$(PROXY_OBJS): CPPFLAGS += $(PROXY_CPPFLAGS)
$(PROXY_OBJS): CFLAGS += $(PROXY_CFLAGS)

$(exec): $(PROXY_OBJS)

DEPS += $(PROXY_OBJS:.o=.d)
CLEAN += $(PROXY_OBJS) $(exec)
TARGETS += $(exec) $(LIB_UTILS_OBJS)

include mk/subdir_post.mk