PROG = axis_is_poc

# Core objects (always compiled)
CORE_OBJS = main.o core.o module_utils.o vdo_handler.o larod_handler.o dlpu_basic.o \
            ACAP.o MQTT.o CERTS.o cJSON.o

# Detection module (always included)
# Frame publisher module (for cloud integration)
MODULE_OBJS = detection_module.o frame_publisher.o

# All objects
OBJS = $(CORE_OBJS) $(MODULE_OBJS)

# Packages
PKGS = gio-2.0 vdostream larod

# Compiler and linker flags
CFLAGS += -Wall -Wextra -O2 $(shell pkg-config --cflags $(PKGS))
LDFLAGS += $(shell pkg-config --libs $(PKGS)) -lpaho-mqtt3a -lm -ljpeg

all: $(PROG)
	@echo "==================================="
	@echo "Build complete: $(PROG)"
	@echo "Base Module with:"
	@echo "  - Core: VDO, Larod, DLPU, MQTT"
	@echo "  - Detection: YOLOv5n (always enabled)"
	@echo "  - Framework: Ready for custom modules"
	@echo "==================================="

$(PROG): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(PROG) *.o *.eap

.PHONY: all clean
