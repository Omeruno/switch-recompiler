ifeq ($(strip $(DEVKITPRO)),)
$(error Set DEVKITPRO to your devkitPro installation)
endif

include $(DEVKITPRO)/libnx/switch_rules

TARGET := switch-browser
BUILD := build
SOURCES := source
INCLUDES := source
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
export CFLAGS := -O2 -Wall -Wextra -ffunction-sections $(ARCH) $(INCLUDE)
export LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -Wl,-Map,$(notdir $*.map)
export LIBS := -lnx
export LIBDIRS := $(LIBNX)
APP_TITLE := Switch Browser Probe
APP_AUTHOR := switch-recompiler
APP_VERSION := 0.1.0

ifneq ($(BUILD),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
export OFILES := $(CFILES:.c=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
.PHONY: all clean $(BUILD)
all: $(BUILD)
$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
clean:
	@rm -rf $(BUILD) $(TARGET).nro $(TARGET).elf $(TARGET).nacp
else
.PHONY: all
all: $(OUTPUT).nro
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).elf: $(OFILES)
$(OFILES): $(HFILES)
-include $(DEPSDIR)/*.d
endif
