CROSS ?= arm-linux-gnueabihf-
CC := $(CROSS)gcc
CFLAGS := -Os -std=c11 -Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L -pthread
LDFLAGS := -Wl,--gc-sections -s -static -pthread

BUILD := build
DIST := dist/cc2-control
TARGET := $(BUILD)/cc2-control
UDS_PROBE := $(BUILD)/cc2-uds-probe

.PHONY: all clean dist uds-probe

all: dist

$(BUILD):
	mkdir -p $(BUILD)

$(TARGET): src/main.c src/mqtt.c src/mqtt.h src/console.c src/console.h src/control.c src/control.h src/panda.c src/panda.h | $(BUILD)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections src/main.c src/mqtt.c src/console.c src/control.c src/panda.c -o $@ $(LDFLAGS) -lm

$(UDS_PROBE): tools/uds_probe.c | $(BUILD)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $< -o $@ $(LDFLAGS)

uds-probe: $(UDS_PROBE)

dist: $(TARGET)
	mkdir -p $(DIST)/web
	cp $(TARGET) $(DIST)/cc2-control
	cp web/index.html $(DIST)/web/index.html
	cp scripts/start.sh $(DIST)/start.sh
	cp scripts/launch.sh $(DIST)/launch.sh
	cp scripts/cc2-control.init $(DIST)/cc2-control.init
	cp config/cc2-control.conf.example $(DIST)/cc2-control.conf.example

clean:
	rm -rf $(BUILD) dist
