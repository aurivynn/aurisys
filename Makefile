NASM := nasm
QEMU := qemu-system-i386
BUILD := build

IMAGE := $(BUILD)/aurisys.img
STAGE1 := $(BUILD)/stage1.bin
STAGE2 := $(BUILD)/stage2.bin
KERNEL_ELF := $(BUILD)/kernel.elf
KERNEL_BIN := $(BUILD)/kernel.bin
KERNEL_HDR := $(BUILD)/kernel.hdr

CXX := $(shell command -v clang++ >/dev/null 2>&1 && echo "clang++ --target=i686-elf")
ifeq ($(CXX),)
  CXX := $(shell command -v i686-elf-g++ >/dev/null 2>&1 && echo i686-elf-g++)
endif
ifeq ($(CXX),)
  CXX := g++ -m32
endif

LD := $(shell command -v ld.lld >/dev/null 2>&1 && echo ld.lld)
ifeq ($(LD),)
  LD := $(shell command -v i686-elf-ld >/dev/null 2>&1 && echo i686-elf-ld)
endif
ifeq ($(LD),)
  LD := ld
endif

OBJCOPY := $(shell command -v llvm-objcopy >/dev/null 2>&1 && echo llvm-objcopy || echo objcopy)

KFLAGS := -ffreestanding -fno-pie -fno-pic -fno-stack-protector -fno-builtin \
          -fno-exceptions -fno-rtti -fno-threadsafe-statics \
          -march=i686 -mgeneral-regs-only -std=c++17 -O2 -Wall \
          -I src/include
AFLAGS := -f elf32
LDFLAGS := -m elf_i386 -nostdlib -T src/kernel/linker.ld

KERNEL_CPP := $(wildcard src/kernel/*.cpp src/kernel/*/*.cpp)
INCLUDES := $(wildcard src/include/*.h)
KERNEL_OBJS := $(BUILD)/entry.o $(BUILD)/font.o \
               $(patsubst src/kernel/%.cpp,$(BUILD)/%.o,$(KERNEL_CPP))

.PHONY: all compile-db run run-headless debug test clean

all: $(IMAGE) compile-db

$(IMAGE): $(STAGE1) $(STAGE2) $(KERNEL_HDR) $(KERNEL_BIN)
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=$(STAGE1) of=$@ bs=512 conv=notrunc status=none
	dd if=$(STAGE2) of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(KERNEL_HDR) of=$@ bs=512 seek=9 conv=notrunc status=none
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=10 conv=notrunc status=none

$(BUILD):
	@mkdir -p $(BUILD)

$(STAGE1): src/boot/boot.asm | $(BUILD)
	$(NASM) -f bin src/boot/boot.asm -o $@
	@test "$$(od -An -tx1 -N2 -j510 $@ | tr -d ' \n')" = "55aa" || { echo "ERROR: stage1 must be 512 bytes ending in 0xAA55"; exit 1; }

$(STAGE2): src/boot/stage2.asm | $(BUILD)
	$(NASM) -f bin src/boot/stage2.asm -o $@
	@test $$(stat -c %s $@) -le 4096 || { echo "ERROR: stage2 exceeds 8 sectors (4096 bytes)"; exit 1; }

$(BUILD)/entry.o: src/kernel/arch/entry.asm | $(BUILD)
	$(NASM) $(AFLAGS) src/kernel/arch/entry.asm -o $@

$(BUILD)/font.o: src/kernel/fonts/font.asm src/kernel/fonts/font8x16.bin | $(BUILD)
	$(NASM) $(AFLAGS) -I src/kernel/fonts src/kernel/fonts/font.asm -o $@

$(BUILD)/%.o: src/kernel/%.cpp $(INCLUDES) | $(BUILD)
	@mkdir -p $(@D)
	$(CXX) $(KFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS) src/kernel/linker.ld | $(BUILD)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@test $$(stat -c %s $@) -le 60000 || { echo "ERROR: kernel bigger than 56KB low-memory load buffer"; exit 1; }

$(KERNEL_HDR): $(KERNEL_BIN)
	python3 -c "import struct; d=open('$(KERNEL_BIN)','rb').read(); h=struct.pack('<III',0x4B525541,len(d),0x100000); open('$@','wb').write(h.ljust(512,b'\x00'))"

compile-db:
	python3 tools/gen_compile_db.py "$(CXX)" "$(KFLAGS)"

run: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -vga std -serial stdio -no-reboot

run-headless: $(IMAGE)
	$(QEMU) -display none -drive format=raw,file=$(IMAGE) -vga std -serial stdio -no-reboot

debug: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -vga std -serial stdio -no-reboot -s -S

test: $(IMAGE)
	rm -f serial.log
	timeout --signal=KILL 20 $(QEMU) -display none -drive format=raw,file=$(IMAGE) \
		-vga std -serial file:serial.log -monitor none -no-reboot || true
	@grep -q "1280x960 MODE OK" serial.log || { echo "VBE 1280x960 check FAILED"; cat serial.log; exit 1; }
	@grep -q "AURISYS: all tests passed" serial.log || { echo "KERNEL TESTS FAILED"; cat serial.log; exit 1; }
	@echo "BOOT TEST PASSED"

clean:
	rm -rf $(BUILD) serial.log