CROSS ?= arm-linux-gnueabihf-
CC := $(CROSS)gcc
# CC2 Control's version lives in VERSION and the community firmware release in FIRMWARE_VERSION; nothing else writes them.
VERSION := $(shell cat VERSION)
FIRMWARE_VERSION := $(shell cat FIRMWARE_VERSION)
CFLAGS := -Os -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -DCC2_CONTROL_VERSION=\"$(VERSION)\" -DCC2_COMMUNITY_FIRMWARE_VERSION=\"$(FIRMWARE_VERSION)\" -pthread $(EXTRA_CFLAGS)
LDFLAGS := -Wl,--gc-sections -s -static -pthread

# Host sanitizer builds, e.g. `make clean test CROSS= CC=gcc SANITIZE=address,undefined`
# or SANITIZE=thread. Sanitizers need debug info and a dynamically linked binary.
ifneq ($(SANITIZE),)
SANITIZE_FLAGS := -fsanitize=$(SANITIZE) -fno-sanitize-recover=all -fno-omit-frame-pointer -g -O1
CFLAGS += $(SANITIZE_FLAGS)
LDFLAGS := -pthread
endif

BUILD := build
DIST := dist/cc2-control
TARGET := $(BUILD)/cc2-control
UDS_PROBE := $(BUILD)/cc2-uds-probe

.PHONY: all clean dist uds-probe test web

all: dist

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): src/main.c src/canvas_eject.h src/gcode_download.h src/history.h src/plates.h src/spools.h src/recovery.h src/mqtt.c src/mqtt.h src/console.c src/console.h src/control.c src/control.h src/http_guard.h src/panda.c src/panda.h src/uds.c src/uds.h VERSION FIRMWARE_VERSION | $(BUILD)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections src/main.c src/mqtt.c src/console.c src/control.c src/panda.c src/uds.c -o $@ $(LDFLAGS) -lm

$(UDS_PROBE): tools/uds_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $< -o $@ $(LDFLAGS)

uds-probe: $(UDS_PROBE)

# Rebuild the committed UI (web/index.html and web/locales) from web-src/. Needs Node; `dist` does not,
# it packages the committed build.
web:
	cd web-src && npm ci && npm run build

dist: $(TARGET)
	mkdir -p $(DIST)/web/locales
	cp $(TARGET) $(DIST)/cc2-control
	cp web/index.html $(DIST)/web/index.html
	cp web/locales/*.json $(DIST)/web/locales/
	cp scripts/start.sh $(DIST)/start.sh
	cp scripts/launch.sh $(DIST)/launch.sh
	cp scripts/cc2-control.init $(DIST)/cc2-control.init
	cp config/cc2-control.conf.example $(DIST)/cc2-control.conf.example

# Host-side tests. Each test is a script, run like builder/current/prepare.py
# runs them. The runtime tests start the binary, so build it for this machine:
#   make clean test CROSS= CC=gcc
# Tests that compile their own C harness receive the same extra and sanitizer
# flags through CC2_TEST_CFLAGS.
test: $(TARGET)
	@set -e; for t in tests/test_*.py; do echo "== $$t"; CC2_TEST_CFLAGS="$(EXTRA_CFLAGS) $(SANITIZE_FLAGS)" python3 $$t $(TARGET); done

clean:
	rm -rf $(BUILD) dist
