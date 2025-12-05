PROG = axis_is_poc

# Core objects (always compiled)
# Note: Paho MQTT and libjpeg must be cross-compiled for aarch64 and bundled with ACAP
CORE_OBJS = main.o core.o vdo_handler.o larod_handler.o dlpu_basic.o cJSON.o \
            ACAP.o MQTT.o CERTS.o module_utils.o

# Detection module (always included)
# Frame publisher module (for cloud integration)
MODULE_OBJS = detection_module.o frame_publisher.o

# All objects
OBJS = $(CORE_OBJS) $(MODULE_OBJS)

# Packages - use pkg-config when available
PKGS = gio-2.0 vdostream larod

# Compiler and linker flags
CFLAGS += -Wall -Wextra -O2
CFLAGS += $(shell PKG_CONFIG_PATH=/opt/axis/acapsdk/sysroots/aarch64/usr/lib/pkgconfig pkg-config --cflags $(PKGS) 2>/dev/null || echo "")
CFLAGS += -I/opt/axis/acapsdk/sysroots/aarch64/usr/include/glib-2.0
CFLAGS += -I/opt/axis/acapsdk/sysroots/aarch64/usr/lib/glib-2.0/include
# System libraries from ACAP SDK
LDLIBS += -lvdostream -llarod -lgio-2.0 -lgobject-2.0 -lglib-2.0 -lfcgi -laxevent -lcurl -lm

# External libraries (built in Dockerfile)
# Using static linking to avoid runtime dependency issues
LDLIBS += -lpaho-mqtt3a -ljpeg -lssl -lcrypto -lpthread
# Note: Paho MQTT C and libjpeg-turbo are built from source in Dockerfile

all: $(PROG)
	@echo "==================================="
	@echo "Build complete: $(PROG)"
	@echo "Base Module with:"
	@echo "  - Core: VDO, Larod, DLPU, MQTT"
	@echo "  - Detection: YOLOv5n (always enabled)"
	@echo "  - Framework: Ready for custom modules"
	@echo "==================================="

$(PROG): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(PROG) *.o *.eap

.PHONY: all clean
