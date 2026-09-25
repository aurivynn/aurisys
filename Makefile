NASM := nasm
QEMU := qemu-system-i386
BUILD := build

IMAGE := $(BUILD)/aurisys.img
STAGE1 := $(BUILD)/stage1.bin
STAGE2 := $(BUILD)/stage2.bin

.PHONY: all run run-headless debug clean

all: $(IMAGE)

$(BUILD):
	@mkdir -p $(BUILD)

$(STAGE1): boot/boot.asm | $(BUILD)
	$(NASM) -f bin boot/boot.asm -o $@
	@test "$$(od -An -tx1 -N2 -j510 $@ | tr -d ' \n')" = "55aa" || { echo "ERROR: stage1 must be 512 bytes ending in 0xAA55"; exit 1; }

$(STAGE2): boot/stage2.asm | $(BUILD)
	$(NASM) -f bin boot/stage2.asm -o $@
	@test $$(stat -c %s $@) -le 4096 || { echo "ERROR: stage2 exceeds 8 sectors (4096 bytes)"; exit 1; }

$(IMAGE): $(STAGE1) $(STAGE2)
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=$(STAGE1) of=$@ bs=512 conv=notrunc status=none
	dd if=$(STAGE2) of=$@ bs=512 seek=1 conv=notrunc status=none

run: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -vga std -serial stdio -no-reboot

run-headless: $(IMAGE)
	$(QEMU) -display none -drive format=raw,file=$(IMAGE) -vga std -serial stdio -no-reboot

debug: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -vga std -serial stdio -no-reboot -s -S

clean:
	rm -rf $(BUILD)