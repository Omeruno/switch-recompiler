ifndef DEVKITPRO
$(error DEVKITPRO is required)
endif
include $(DEVKITPRO)/libnx/switch_rules
TARGET := switch-engine-probe
APP_TITLE := Switch Engine Probe
APP_AUTHOR := switch-recompiler
APP_VERSION := 0.1.1
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mno-outline-atomics -mtp=soft -ftls-model=local-exec -fPIE
CMAKE ?= cmake
.PHONY: all runtime clean
all: $(TARGET).nro
runtime:
	python3 tools/fetch_wamr.py build/wamr
	python3 tools/embed_fixtures.py
	$(CMAKE) -S . -B build/switch -DCMAKE_TOOLCHAIN_FILE=$(CURDIR)/toolchain-switch.cmake -DSWITCH_ENGINE_TARGET=ON -DWAMR_ROOT_DIR=$(CURDIR)/build/wamr -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build build/switch -j4
$(TARGET).elf: runtime src/switch_main.c
	$(CC) -O2 -Wall -Wextra -D__SWITCH__ $(ARCH) -Isrc -I$(DEVKITPRO)/libnx/include -specs=$(DEVKITPRO)/libnx/switch.specs src/switch_main.c build/switch/libengine_probe.a build/switch/libengine_runtime.a -L$(DEVKITPRO)/libnx/lib -lm -lnx -Wl,-Map,$(TARGET).elf.map -o $@
$(TARGET).nro: $(TARGET).elf $(TARGET).nacp
	elf2nro $< $@ --nacp=$(CURDIR)/$(TARGET).nacp
clean:
	python3 -c "import shutil,pathlib; shutil.rmtree('build',ignore_errors=True); [p.unlink(missing_ok=True) for p in pathlib.Path('.').glob('switch-engine-probe.*') if p.suffix in ('.elf','.nacp','.nro','.map')]"
