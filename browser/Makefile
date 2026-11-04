# Independent HTTPS prototype; it does not link into the x86 runtime.
ifndef DEVKITPRO
$(error DEVKITPRO is required)
endif
include $(DEVKITPRO)/libnx/switch_rules

TARGET := switch-browser
BUILD := build
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -O2 -Wall -Wextra -ffunction-sections -fdata-sections $(ARCH)
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -Wl,-Map,$(TARGET).elf.map
PORTLIBS := $(DEVKITPRO)/portlibs/switch
OFILES := $(BUILD)/main.o $(BUILD)/page.o

.PHONY: all clean
all: $(TARGET).nro
$(TARGET).nro: $(TARGET).elf
$(TARGET).elf: $(OFILES)
	$(CC) $(LDFLAGS) $(OFILES) -L$(PORTLIBS)/lib -L$(DEVKITPRO)/libnx/lib -lcurl -lz -lnx -o $@
$(BUILD)/%.o: source/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(DEVKITPRO)/libnx/include -I$(PORTLIBS)/include -MMD -MP -c $< -o $@
clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).elf.map
-include $(OFILES:.o=.d)
