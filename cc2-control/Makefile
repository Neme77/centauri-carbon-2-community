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
UPDATE_WORKER := $(BUILD)/cc2-update-worker
HTTPS_DOWNLOAD := $(BUILD)/cc2-https-download
TLS := $(BUILD)/tls/mbedtls-3.6.7
UDS_PROBE := $(BUILD)/cc2-uds-probe

.PHONY: all clean dist uds-probe test web

all: dist

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): src/external_feed_state.h src/external_filament.h src/bed_drying.h src/backup.h src/backup_format.h src/experiments.h src/experiments_state.h src/app_update_catalog.h src/app_updates.h src/main.c src/gcode_metadata.h src/gcode_download.h src/history.h src/plates.h src/spools.h src/canvas_unload.h src/sha256.h src/recovery.h src/mqtt.c src/mqtt.h src/console.c src/console.h src/control.c src/control.h src/http_guard.h src/panda.c src/panda.h src/uds.c src/uds.h src/network_status.c src/network_status_http.h src/connections_http.h src/status_core.h $(wildcard src/connections/*.h) VERSION FIRMWARE_VERSION | $(BUILD)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections src/main.c src/mqtt.c src/console.c src/control.c src/panda.c src/uds.c src/network_status.c -DCC2_NETWORK_STATUS_LIBRARY -o $@ $(LDFLAGS) -lm

$(UPDATE_WORKER): updater/verify.c updater/trusted-key.h updater/trusted-base.h third-party/monocypher/monocypher.c third-party/monocypher/monocypher.h third-party/monocypher/monocypher-ed25519.c third-party/monocypher/monocypher-ed25519.h VERSION FIRMWARE_VERSION | $(BUILD)
	$(CC) $(CFLAGS) -Ithird-party/monocypher -Iupdater -ffunction-sections -fdata-sections updater/verify.c third-party/monocypher/monocypher.c third-party/monocypher/monocypher-ed25519.c -o $@ $(LDFLAGS)

$(BUILD)/tls/.ready: updater/prepare-tls.py third-party/mbedtls-3.6.7.tar.bz2 | $(BUILD)
	python3 updater/prepare-tls.py

$(HTTPS_DOWNLOAD): updater/download.c $(BUILD)/tls/.ready
	$(MAKE) -C $(TLS)/library CC=$(CC) AR=$(CROSS)ar CFLAGS="-Os -ffunction-sections -fdata-sections" static
	$(CC) $(CFLAGS) -I$(TLS)/include updater/download.c $(TLS)/library/libmbedtls.a $(TLS)/library/libmbedx509.a $(TLS)/library/libmbedcrypto.a -o $@ $(LDFLAGS)

$(UDS_PROBE): tools/uds_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $< -o $@ $(LDFLAGS)

uds-probe: $(UDS_PROBE)

# Rebuild the committed UI (web/index.html and web/locales) from web-src/. Needs Node; `dist` does not,
# it packages the committed build.
web:
	cd web-src && npm ci && npm run build

dist: $(TARGET) $(UPDATE_WORKER) $(HTTPS_DOWNLOAD)
	mkdir -p $(DIST)/web/locales
	cp $(TARGET) $(DIST)/cc2-control
	cp $(UPDATE_WORKER) $(DIST)/cc2-update-worker
	cp $(HTTPS_DOWNLOAD) $(DIST)/cc2-https-download
	cp updater/ca-bundle.pem $(DIST)/ca-bundle.pem
	cp scripts/update-runner.sh $(DIST)/update-runner.sh
	cp updater/trusted-key.json $(DIST)/updater-trusted-key.json
	cat third-party/monocypher/LICENCE.md third-party/mbedtls-3.6.7.tar.LICENSE third-party/HTTPS-NOTICE.md > $(DIST)/THIRD_PARTY_NOTICES.txt
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
test: $(TARGET) $(UPDATE_WORKER) $(HTTPS_DOWNLOAD)
	@set -e; for t in tests/test_*.py; do echo "== $$t"; CC2_TEST_CFLAGS="$(EXTRA_CFLAGS) $(SANITIZE_FLAGS)" python3 $$t $(TARGET); done

clean:
	rm -rf $(BUILD) dist
