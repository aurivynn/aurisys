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
FS_IMG := $(BUILD)/fs.img
DISK_SECTORS := 32768
PART_LBA := 2048
PART_SECTORS := 30720

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
LDFLAGS := -m elf_i386 -nostdlib -T src/kernel/linker.ld --no-warn-rwx-segments

KERNEL_CPP := $(wildcard src/kernel/*.cpp src/kernel/*/*.cpp)
INCLUDES := $(shell find src/include -name '*.h' | sort)
KERNEL_OBJS := $(BUILD)/entry.o $(BUILD)/font.o $(BUILD)/isr_stubs.o $(BUILD)/syscall_stub.o \
               $(BUILD)/switch.o \
               $(patsubst src/kernel/%.cpp,$(BUILD)/%.o,$(KERNEL_CPP))

APPFLAGS := $(KFLAGS) -I src/apps
APP_NAMES := cat df echo fault heap help hexdump ls mem mkdir panic ps rm spin uptime write
APP_SRCS := $(addprefix src/apps/,$(addsuffix .cpp,$(APP_NAMES)))
APP_OBJS := $(addprefix $(BUILD)/app/,$(addsuffix .o,$(APP_NAMES)))
APP_RT_OBJS := $(BUILD)/app/rt/crt0.o $(BUILD)/app/rt/lib.o $(BUILD)/app/rt/print.o \
               $(BUILD)/app/rt/mem.o $(BUILD)/app/rt/str.o $(BUILD)/app/rt/alloc.o
APP_BINS := $(addprefix $(BUILD)/rootfs/bin/,$(APP_NAMES))

COMPILE_DB_SRC := tools/gen_compile_db/main.cpp
COMPILE_DB_BIN := $(BUILD)/gen_compile_db

.PHONY: all compile-db run run-headless debug size fs-check clean

.SECONDARY: $(APP_OBJS) $(APP_RT_OBJS)

all: $(IMAGE) compile-db

$(IMAGE): $(STAGE1) $(STAGE2) $(KERNEL_HDR) $(KERNEL_BIN) $(FS_IMG)
	dd if=/dev/zero of=$@ bs=512 count=$(DISK_SECTORS) status=none
	dd if=$(STAGE1) of=$@ bs=512 conv=notrunc status=none
	dd if=$(STAGE2) of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=$(KERNEL_HDR) of=$@ bs=512 seek=9 conv=notrunc status=none
	dd if=$(KERNEL_BIN) of=$@ bs=512 seek=10 conv=notrunc status=none
	printf '\000\000\000\000\203\000\000\000\000\010\000\000\000\170\000\000' | dd of=$@ bs=1 seek=446 conv=notrunc status=none
	dd if=$(FS_IMG) of=$@ bs=512 seek=$(PART_LBA) conv=notrunc status=none

$(FS_IMG): $(APP_BINS) | $(BUILD)
	rm -f $@
	mke2fs -t ext4 -q -F -b 4096 \
		-O ^64bit,^metadata_csum,^has_journal,^resize_inode,^orphan_file,^uninit_bg \
		-d $(BUILD)/rootfs $@ 15M

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

$(BUILD)/syscall_stub.o: src/kernel/arch/syscall.asm | $(BUILD)
	$(NASM) $(AFLAGS) src/kernel/arch/syscall.asm -o $@

$(BUILD)/switch.o: src/kernel/arch/switch.asm | $(BUILD)
	$(NASM) $(AFLAGS) src/kernel/arch/switch.asm -o $@

$(COMPILE_DB_BIN): $(COMPILE_DB_SRC) | $(BUILD)
	$(HOST_CXX) -std=c++17 -O2 -Wall -o $@ $<

$(BUILD)/font.o: src/kernel/fonts/font.asm src/kernel/fonts/font8x16.bin | $(BUILD)
	$(NASM) $(AFLAGS) -I src/kernel/fonts src/kernel/fonts/font.asm -o $@

$(BUILD)/app/rt/%.o: src/kernel/lib/%.cpp $(INCLUDES) | $(BUILD)
	@mkdir -p $(@D)
	$(CXX) $(APPFLAGS) -c $< -o $@

$(BUILD)/app/rt/lib.o: src/apps/lib.cpp src/apps/lib.h $(INCLUDES) | $(BUILD)
	@mkdir -p $(@D)
	$(CXX) $(APPFLAGS) -c $< -o $@

$(BUILD)/app/rt/alloc.o: src/apps/alloc.cpp $(INCLUDES) | $(BUILD)
	@mkdir -p $(@D)
	$(CXX) $(APPFLAGS) -c $< -o $@

$(BUILD)/app/rt/crt0.o: src/apps/crt0.asm | $(BUILD)
	@mkdir -p $(@D)
	$(NASM) $(AFLAGS) src/apps/crt0.asm -o $@


$(BUILD)/rootfs/bin/%: src/apps/%.cpp src/apps/lib.h src/apps/linker.ld $(APP_RT_OBJS) | $(BUILD)
	@mkdir -p $(@D) $(BUILD)/app
	$(CXX) $(APPFLAGS) -c $< -o $(BUILD)/app/$*.o
	@nm $(BUILD)/app/$*.o | grep -qw main || \
		{ echo "ERROR: $* exports no unmangled 'main'"; exit 1; }
	$(LD) -m elf_i386 -T src/apps/linker.ld -o $@ $(APP_RT_OBJS) $(BUILD)/app/$*.o
	@chmod +x $@

$(BUILD)/%.o: src/kernel/%.cpp $(INCLUDES) | $(BUILD)
	@mkdir -p $(@D)
	$(CXX) $(KFLAGS) -c $< -o $@

$(KERNEL_ELF): $(KERNEL_OBJS) src/kernel/linker.ld | $(BUILD)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_OBJS)

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@n=$$(stat -c %s $@); test $$n -le 65024 || { echo "ERROR: kernel is $$n bytes, over the 65024 cap"; \
		exit 1; }

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

fs-check: $(IMAGE) $(FS_IMG)
	@echo "== host ext4 cross-check =="
	@dd if=$(IMAGE) bs=512 skip=$(PART_LBA) count=$(PART_SECTORS) status=none | cmp - $(FS_IMG) || \
		{ echo "ERROR: partition region != fs.img"; exit 1; }
	@echo "partition at lba $(PART_LBA) is byte-identical to fs.img"
	@dumpe2fs -h $(FS_IMG) 2>/dev/null | grep -E "Filesystem features|Block size|Block count|Inode count|Free blocks|Free inodes"
	@echo "fs.img is clean ext4"

size: $(KERNEL_BIN)
	@n=$$(stat -c %s $(KERNEL_BIN)); \
	echo "kernel.bin: $$n bytes (cap 65024, the loader ceiling, $$((65024 - n)) to spare)"; \
	test $$n -le 65024 || { echo "kernel too big"; exit 1; }

clean:
	rm -rf $(BUILD)