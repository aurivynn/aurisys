NASM := nasm
QEMU := qemu-system-i386
BUILD := build
HOST_CXX := g++

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
KERNEL_OBJS := $(BUILD)/entry.o $(BUILD)/font.o $(BUILD)/isr_stubs.o \
               $(patsubst src/kernel/%.cpp,$(BUILD)/%.o,$(KERNEL_CPP))

BOOT_TEST_SRC := tools/boot-test/main.cpp
BOOT_TEST_BIN := $(BUILD)/boot-test
COMPILE_DB_SRC := tools/gen_compile_db/main.cpp
COMPILE_DB_BIN := $(BUILD)/gen_compile_db

.PHONY: all compile-db run run-headless debug test size clean

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

$(BUILD)/isr_stubs.o: src/kernel/arch/isr.asm | $(BUILD)
	$(NASM) $(AFLAGS) src/kernel/arch/isr.asm -o $@

$(BOOT_TEST_BIN): $(BOOT_TEST_SRC) | $(BUILD)
	$(HOST_CXX) -std=c++17 -O2 -Wall -Wextra -o $@ $<

$(COMPILE_DB_BIN): $(COMPILE_DB_SRC) | $(BUILD)
	$(HOST_CXX) -std=c++17 -O2 -Wall -o $@ $<

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

compile-db: $(COMPILE_DB_BIN)
	$(COMPILE_DB_BIN) "$(CXX)" "$(KFLAGS)"

run: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -vga std -serial stdio -no-reboot

run-headless: $(IMAGE)
	$(QEMU) -display none -drive format=raw,file=$(IMAGE) -vga std -serial stdio -no-reboot

debug: $(IMAGE)
	$(QEMU) -drive format=raw,file=$(IMAGE) -vga std -serial stdio -no-reboot -s -S

test: $(IMAGE) $(BOOT_TEST_BIN)
	$(BOOT_TEST_BIN)
	@echo "BOOT TEST PASSED"

size: $(KERNEL_BIN)
	@echo "kernel.bin: $$(stat -c %s $(KERNEL_BIN)) bytes (cap 60000)"
	@test $$(stat -c %s $(KERNEL_BIN)) -le 60000 || { echo "kernel too big"; exit 1; }

clean:
	rm -rf $(BUILD) serial.log