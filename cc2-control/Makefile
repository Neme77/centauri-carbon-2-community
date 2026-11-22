CROSS ?= arm-linux-gnueabihf-
CC := $(CROSS)gcc
# CC2 Control's version lives in VERSION and the community firmware release in FIRMWARE_VERSION; nothing else writes them.
VERSION := $(shell cat VERSION)
FIRMWARE_VERSION := $(shell cat FIRMWARE_VERSION)
CFLAGS := -Os -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -D_FILE_OFFSET_BITS=64 -DCC2_CONTROL_VERSION=\"$(VERSION)\" -DCC2_COMMUNITY_FIRMWARE_VERSION=\"$(FIRMWARE_VERSION)\" -pthread $(EXTRA_CFLAGS)
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

.PHONY: all clean dist uds-probe test test-app-update-boot test-native-payload web

all: dist

$(BUILD):
	mkdir -p $(BUILD)

# The backend is one link of these sources. The headers they include are not listed by hand: the compiler writes them
# to $(BACKEND_DEPS) (-MM: user headers only, -MP: a deleted header does not stop make) and make includes that file.
# The list is made before the program is linked, and the program depends on it:
# - a missing or older list is made first and then the program is rebuilt, so an existing binary from before this rule
#   (no list yet) is not trusted, and a deleted list repairs itself;
# - the list is a target of its own rule, with the headers it names as prerequisites (-MT twice), so a changed header
#   makes the list again before the program is checked;
# - if making the list fails, the program is not linked, and the next build tries again.
# The list comes from a separate -MM run because -MMD -MF over several sources keeps only the last source's headers.
# BACKEND_DEFS belongs to both runs.
BACKEND_SOURCES := src/main.c src/mqtt.c src/console.c src/control.c src/panda.c src/uds.c src/network_status.c
BACKEND_DEFS := -DCC2_NETWORK_STATUS_LIBRARY
BACKEND_DEPS := $(BUILD)/cc2-control.d

$(BACKEND_DEPS): $(BACKEND_SOURCES) VERSION FIRMWARE_VERSION | $(BUILD)
	$(CC) $(CFLAGS) $(BACKEND_DEFS) -MM -MP -MT $(TARGET) -MT $@ $(BACKEND_SOURCES) > $@.tmp.$$$$ && mv $@.tmp.$$$$ $@ || { rm -f $@.tmp.$$$$; exit 1; }

$(TARGET): $(BACKEND_SOURCES) VERSION FIRMWARE_VERSION $(BACKEND_DEPS) | $(BUILD)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(BACKEND_SOURCES) $(BACKEND_DEFS) -o $@ $(LDFLAGS) -lm

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
# These integrations need a root-owned fixture or an actual packaged payload;
# CI invokes both explicitly rather than passing the app ELF as a payload.
TESTS ?= $(filter-out tests/test_rfid_app_boot.py tests/test_native_signed_payload.py,$(wildcard tests/test_*.py))

test-app-update-boot:
	python3 tests/test_rfid_app_boot.py

test-native-payload:
	@test -n "$(PAYLOAD)" || { echo 'Set PAYLOAD to the packaged common tar.gz'; exit 1; }
	python3 tests/test_native_signed_payload.py "$(PAYLOAD)"

test: $(TARGET) $(UPDATE_WORKER) $(HTTPS_DOWNLOAD)
	@set -e; for t in $(TESTS); do echo "== $$t"; CC2_TEST_CFLAGS="$(EXTRA_CFLAGS) $(SANITIZE_FLAGS)" python3 $$t $(TARGET); done

clean:
	rm -rf $(BUILD) dist

# The list is read only by goals that build the backend (and by a plain `make`). Not when `clean` is among the goals
# (`make clean test`: the list would be made first and then deleted by clean; the build after clean makes it again through
# its own rule), and not for goals that build nothing: the CI runs `sudo make test-app-update-boot`, and reading the list
# there would make a root-owned build/ that the next `make clean` of the normal user cannot remove.
BACKEND_GOALS := all dist test $(TARGET)
ifeq ($(filter clean,$(MAKECMDGOALS)),)
ifneq ($(if $(MAKECMDGOALS),$(filter $(BACKEND_GOALS),$(MAKECMDGOALS)),all),)
-include $(BACKEND_DEPS)
endif
endif
