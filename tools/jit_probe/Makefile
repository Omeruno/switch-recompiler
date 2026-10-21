# Isolated JIT experiment. Never replaces or links into the main application.
ifndef DEVKITPRO
$(error DEVKITPRO is required)
endif
include $(DEVKITPRO)/libnx/switch_rules

TARGET := jit_probe
BUILD := build
SOURCES := .
INCLUDES := .
LIBS := -lnx
LIBDIRS := $(DEVKITPRO)/libnx
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CXXFLAGS := -g -O2 -Wall -Wextra -std=gnu++17 -ffunction-sections -fdata-sections $(ARCH) $(INCLUDE)
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $@).map
export OUTPUT := $(CURDIR)/$(TARGET)
export VPATH := $(CURDIR)
export DEPSDIR := $(CURDIR)/$(BUILD)
export OFILES := $(BUILD)/main.o
export LIBPATHS := $(DEVKITPRO)/libnx/lib

.PHONY: all clean
all: $(TARGET).nro
$(TARGET).nro: $(TARGET).elf
$(TARGET).elf: $(OFILES)
	$(CXX) $(LDFLAGS) $(OFILES) -L$(DEVKITPRO)/libnx/lib $(LIBS) -o $@
$(BUILD)/main.o: main.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I$(DEVKITPRO)/libnx/include -MMD -MP -c $< -o $@
clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).elf.map
-include $(BUILD)/main.d
