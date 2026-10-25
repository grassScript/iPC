CC      := gcc
NASM    := nasm
PYTHON  := python3

CFLAGS  := -m32 -ffreestanding -O2 -nostdlib -fno-pie -fno-stack-protector -fno-builtin -mno-sse -mno-mmx -Wall -Wextra -Isrc
LDFLAGS := -m elf_i386 -T src/linker.ld --oformat binary

BUILD_DIR := build
IMAGE     := $(BUILD_DIR)/ios_boot.img

.PHONY: all clean run

all: $(IMAGE)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

src/apple_logo.h src/font8x8.h src/cursor.h src/wallpaper_icon.h src/settings_icon.h src/close_icon.h src/wallpapers.h: apple.png cur.png wallpaper_icon.bmp settings_icon.bmp close.bmp wall1.png wall2.png tools/build_logo.py
	$(PYTHON) tools/build_logo.py apple.png src/apple_logo.h src/font8x8.h cur.png src/cursor.h

$(BUILD_DIR)/boot.bin: src/boot.asm | $(BUILD_DIR)
	$(NASM) -f bin $< -o $@

$(BUILD_DIR)/entry.o: src/entry.asm | $(BUILD_DIR)
	$(NASM) -f elf32 $< -o $@

$(BUILD_DIR)/kernel.o: src/kernel.c src/apple_logo.h src/font8x8.h src/cursor.h src/wallpaper_icon.h src/settings_icon.h src/close_icon.h src/wallpapers.h src/disasm.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.bin: $(BUILD_DIR)/entry.o $(BUILD_DIR)/kernel.o src/linker.ld
	ld $(LDFLAGS) $(BUILD_DIR)/entry.o $(BUILD_DIR)/kernel.o -o $@

$(IMAGE): $(BUILD_DIR)/boot.bin $(BUILD_DIR)/kernel.bin
	@test $$(stat -c%s $(BUILD_DIR)/kernel.bin) -le 65536 || { echo "kernel.bin > 64KB: boot.asm loads only 128 sectors, the OS will not come up"; exit 1; }
	dd if=/dev/zero of=$@ bs=512 count=192 status=none
	dd if=$(BUILD_DIR)/boot.bin of=$@ conv=notrunc bs=512 count=1 seek=0 status=none
	dd if=$(BUILD_DIR)/kernel.bin of=$@ conv=notrunc bs=512 seek=1 status=none

run: $(IMAGE)
	qemu-system-x86_64 -drive format=raw,file=$(IMAGE)

clean:
	rm -rf $(BUILD_DIR) src/apple_logo.h src/font8x8.h src/cursor.h src/wallpaper_icon.h src/settings_icon.h src/close_icon.h src/wallpapers.h


