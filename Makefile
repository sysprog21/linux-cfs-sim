.PHONY: all clean
TARGET = cfs-sim
all: $(TARGET)

include common.mk

CFLAGS = -I.
CFLAGS += -O2 -g
CFLAGS += -std=gnu11 -Wall

LDFLAGS = -lpthread

# standard build rules
.SUFFIXES: .o .c
.c.o:
	$(VECHO) "  CC\t$@\n"
	$(Q)$(CC) -o $@ $(CFLAGS) -c -MMD -MF $@.d $<

OBJS = \
	util.o \
	cfs-sim.o

deps += $(OBJS:%.o=%.o.d)

$(TARGET): $(OBJS)
	$(VECHO) "  LD\t$@\n"
	$(Q)$(CC) -o $@ $^ $(LDFLAGS)

clean:
	$(VECHO) "  Cleaning...\n"
	$(Q)$(RM) $(TARGET) $(OBJS) $(deps)

-include $(deps)
