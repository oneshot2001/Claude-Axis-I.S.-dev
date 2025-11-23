PROG = axis_is_poc

# Module feature flags (can be overridden: make ENABLE_LPR=0 ENABLE_OCR=0)
ENABLE_LPR ?= 1
ENABLE_OCR ?= 1

# Core objects (always compiled)
CORE_OBJS = main.o core.o module_utils.o vdo_handler.o larod_handler.o dlpu_basic.o \
            ACAP.o MQTT.o CERTS.o cJSON.o

# Detection module (always included)
MODULE_OBJS = detection_module.o

# Conditional modules
ifeq ($(ENABLE_LPR),1)
    MODULE_OBJS += lpr_module.o
    CFLAGS += -DMODULE_LPR
endif

ifeq ($(ENABLE_OCR),1)
    MODULE_OBJS += ocr_module.o
    CFLAGS += -DMODULE_OCR
endif

# All objects
OBJS = $(CORE_OBJS) $(MODULE_OBJS)

# Packages
PKGS = gio-2.0 vdostream larod

# Compiler and linker flags
CFLAGS += -Wall -Wextra -O2 $(shell pkg-config --cflags $(PKGS))
LDFLAGS += $(shell pkg-config --libs $(PKGS)) -lpaho-mqtt3a -lm

# Add libcurl if any AI modules are enabled
ifneq ($(ENABLE_LPR)$(ENABLE_OCR),00)
    LDFLAGS += -lcurl -ljpeg
endif

all: $(PROG)
	@echo "==================================="
	@echo "Build complete: $(PROG)"
	@echo "Modules enabled:"
	@echo "  - Detection: YES (always)"
	@echo "  - LPR:       $(if $(filter 1,$(ENABLE_LPR)),YES,NO)"
	@echo "  - OCR:       $(if $(filter 1,$(ENABLE_OCR)),YES,NO)"
	@echo "==================================="

$(PROG): $(OBJS)
	$(CC) $^ $(LDFLAGS) -o $@

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	rm -f $(PROG) *.o *.eap

.PHONY: all clean
