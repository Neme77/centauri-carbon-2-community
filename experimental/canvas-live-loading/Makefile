CROSS ?= arm-linux-gnueabihf-
QEMU ?= qemu-arm
BUILD ?= build
ARM_FLAGS = -std=c11 -Wall -Wextra -Werror -marm -mcpu=cortex-a7 -mfpu=neon-vfpv4

.PHONY: all test
all: $(BUILD)/libcc2-canvas-live-loading.so

$(BUILD):
	mkdir -p "$@"

$(BUILD)/libcc2-canvas-live-loading.so: native_module.c native_arm.S ../../cc2-control/src/sha256.h | $(BUILD)
	$(CROSS)gcc $(ARM_FLAGS) -Os -fPIC -shared -Wl,-z,defs native_module.c native_arm.S -o "$@"

$(BUILD)/test_native: native_module.c test_native.c native_arm.S test_native_arm.S | $(BUILD)
	$(CROSS)gcc $(ARM_FLAGS) -O2 -static -Wl,--section-start=.native_calls=0x005e0254 native_arm.S test_native_arm.S test_native.c -o "$@"

test: $(BUILD)/test_native
	$(QEMU) "$<"
