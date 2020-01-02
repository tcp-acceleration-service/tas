include mk/subdir_pre.mk

tools := tracetool statetool scaletool
execs := $(addprefix $(d)/, $(tools))
TOOLS_OBJS := $(addsuffix .o,$(execs))

$(TOOLS_OBJS): CPPFLAGS += -Ilib/tas/include

tools/statetool: tools/statetool.o lib/libtas.so
tools/scaletool: tools/scaletool.o lib/libtas.so

DEPS += $(TOOLS_OBJS:.o=.d)
CLEAN += $(TOOLS_OBJS) $(execs)
TARGETS += $(execs)

include mk/subdir_post.mk
