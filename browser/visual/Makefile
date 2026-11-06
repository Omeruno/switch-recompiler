ifndef DEVKITPRO
$(error DEVKITPRO is required)
endif
include $(DEVKITPRO)/libnx/switch_rules
TARGET := switch-visual-browser
APP_TITLE := Switch Visual Browser
APP_AUTHOR := switch-recompiler
APP_VERSION := 0.7.1
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mno-outline-atomics -mtp=soft -ftls-model=local-exec -fPIE
PORTLIBS := $(DEVKITPRO)/portlibs/switch
PORT_CFLAGS := $(shell PKG_CONFIG_LIBDIR=$(PORTLIBS)/lib/pkgconfig pkg-config --cflags sdl2 SDL2_ttf SDL2_image libwebp libcurl)
PORT_LIBS := $(shell PKG_CONFIG_LIBDIR=$(PORTLIBS)/lib/pkgconfig pkg-config --static --libs sdl2 SDL2_ttf SDL2_image libwebp libcurl)
CMAKE ?= cmake
.PHONY: all runtime assets
all: $(TARGET).nro
runtime:
	python3 tools/fetch_litehtml.py build/litehtml
	$(CMAKE) -S . -B build/switch -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/toolchain-switch.cmake -DSWITCH_VISUAL_TARGET=ON -DLITEHTML_ROOT_DIR=$(CURDIR)/build/litehtml -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build build/switch -j4
assets:
	python3 tools/prepare_romfs.py
$(TARGET).elf: runtime source/main.cpp
	$(CXX) -O2 -Wall -Wextra $(ARCH) -D__SWITCH__ -std=gnu++17 -Isource -I../source -Ibuild/litehtml/include -I$(DEVKITPRO)/libnx/include $(PORT_CFLAGS) -specs=$(DEVKITPRO)/libnx/switch.specs source/main.cpp build/switch/libvisual_core.a build/switch/litehtml/liblitehtml.a build/switch/litehtml/src/gumbo/libgumbo.a build/switch/libvisual_profile.a build/switch/libvisual_quickjs.a -L$(PORTLIBS)/lib -L$(DEVKITPRO)/libnx/lib $(PORT_LIBS) -lm -lnx -Wl,-Map,$(TARGET).elf.map -o $@
$(TARGET).nro: $(TARGET).elf $(TARGET).nacp assets
	elf2nro $< $@ --nacp=$(CURDIR)/$(TARGET).nacp --romfsdir=$(CURDIR)/build/romfs
