include mk/subdir_pre.mk

LIB_TAS_OBJS := $(addprefix $(d)/, \
  init.o init_pxy.o kernel.o kernel_pxy.o conn.o connect.o)
LIB_TAS_SOBJS := $(LIB_TAS_OBJS:.o=.shared.o)

LIB_TAS_CPPFLAGS := -I$(d)/include/ -Itas/include/

$(LIB_TAS_OBJS): CPPFLAGS += $(LIB_TAS_CPPFLAGS)
$(LIB_TAS_SOBJS): CPPFLAGS += $(LIB_TAS_CPPFLAGS)

lib/libtas.so: $(LIB_TAS_SOBJS) $(LIB_UTILS_SOBJS)

DEPS += $(LIB_TAS_OBJS:.o=.d) $(LIB_TAS_SOBJS:.o=.d)
CLEAN += $(LIB_TAS_OBJS) $(LIB_TAS_SOBJS) lib/libtas.so
TARGETS += lib/libtas.so

include mk/subdir_post.mk
