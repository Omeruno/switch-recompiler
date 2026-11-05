# Independent HTTPS prototype; it does not link into the x86 runtime.
ifndef DEVKITPRO
$(error DEVKITPRO is required)
endif
include $(DEVKITPRO)/libnx/switch_rules

TARGET := switch-browser
BUILD := build
APP_TITLE := Switch Browser
APP_AUTHOR := switch-recompiler
APP_VERSION := 0.2.0
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -O2 -Wall -Wextra -ffunction-sections -fdata-sections $(ARCH)
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs $(ARCH) -Wl,-Map,$(TARGET).elf.map
PORTLIBS := $(DEVKITPRO)/portlibs/switch
QJS_DIR := third_party/quickjs
QJS_PARTS := quickjs dtoa libregexp libunicode cutils
QJS_OFILES := $(addprefix $(BUILD)/qjs_,$(addsuffix .o,$(QJS_PARTS)))
OFILES := $(BUILD)/main.o $(BUILD)/page.o $(BUILD)/js_runtime.o $(QJS_OFILES)
PKG_CONFIG_LIBDIR := $(PORTLIBS)/lib/pkgconfig
PORT_CFLAGS := $(shell PKG_CONFIG_LIBDIR=$(PKG_CONFIG_LIBDIR) pkg-config --cflags sdl2 SDL2_ttf libcurl)
PORT_LIBS := $(shell PKG_CONFIG_LIBDIR=$(PKG_CONFIG_LIBDIR) pkg-config --static --libs sdl2 SDL2_ttf libcurl)

.PHONY: all clean
all: $(TARGET).nro
$(TARGET).nro: $(TARGET).elf $(TARGET).nacp
	elf2nro $< $@ --nacp=$(CURDIR)/$(TARGET).nacp --romfsdir=$(CURDIR)/romfs
$(TARGET).elf: $(OFILES)
	$(CC) $(LDFLAGS) $(OFILES) -L$(PORTLIBS)/lib -L$(DEVKITPRO)/libnx/lib $(PORT_LIBS) -lm -lnx -o $@
$(BUILD)/%.o: source/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(DEVKITPRO)/libnx/include -isystem $(QJS_DIR) $(PORT_CFLAGS) -MMD -MP -c $< -o $@
$(BUILD)/qjs_%.o: $(QJS_DIR)/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fwrapv -D_GNU_SOURCE -D__SWITCH__ -DCONFIG_VERSION='"2026-06-04"' -I$(QJS_DIR) -MMD -MP -c $< -o $@
clean:
	rm -rf $(BUILD) $(TARGET).elf $(TARGET).nro $(TARGET).nacp $(TARGET).elf.map
-include $(OFILES:.o=.d)
