;; protected mode shit

BITS 16
ORG 0x8000

BOOTINFO equ 0x4000
BOOTINFO_MAGIC equ 0x41555249 ; "AURI"

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000
    mov [boot_drive], dl

    call serial_init
    mov si, msg_banner
    call serial_puts

    call vbe_find_mode
    jc .fatal_vbe

    ; print framebuffer info
    mov si, msg_fb
    call serial_puts
    mov eax, [fb_addr]
    call print_hex32
    mov si, msg_pitch
    call serial_puts
    movzx eax, word [fb_pitch]
    call print_hex32
    call serial_newline

    ; e820 memory map
    call e820_detect
    mov si, msg_e820
    call serial_puts
    movzx eax, word [mem_count]
    call print_hex32
    call serial_newline

    call store_bootinfo

    call load_kernel
    jc  .fatal_kernel

    call enable_a20
    mov si, msg_a20
    call serial_puts

    cli
    lgdt [gdt_desc]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pmode32

.fatal_kernel:
    mov si, msg_krn_fatal
    call serial_puts
    jmp .halt
.fatal_vbe:
    mov si, msg_fatal_vbe
    call serial_puts
.halt:
    hlt
    jmp .halt

;; com1 serial helpers
serial_init:
    push dx
    push ax
    mov dx, 0x3FB
    mov al, 0x80 ; DLAB on
    out dx, al
    mov dx, 0x3F8
    mov ax, 0x0001 ; divisor = 1 -> 115200 baud
    out dx, al
    mov al, ah ; divisor high byte = 0
    mov dx, 0x3F9
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03 ; 8N1, DLAB off
    out dx, al
    pop ax
    pop dx
    ret

serial_putc:                    ; AL = char
    push dx
    push ax
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20               ; THR empty?
    jz  .wait
    mov dx, 0x3F8
    pop ax
    out dx, al
    pop dx
    ret

serial_puts: ; DS:SI = NUL-terminated string
    lodsb
    test al, al
    jz  .done
    call serial_putc
    jmp serial_puts
.done:
    ret

serial_newline:
    push ax
    mov al, 13
    call serial_putc
    mov al, 10
    call serial_putc
    pop ax
    ret

; print EAX as 8 hex digits
print_hex32:
    push eax
    push ecx
    mov ecx, 8
.loop:
    rol eax, 4
    push eax
    and al, 0x0F
    cmp al, 10
    jb  .digit
    add al, 'A' - 10
    jmp .put
.digit:
    add al, '0'
.put:
    call serial_putc
    pop eax
    dec ecx
    jnz .loop
    pop ecx
    pop eax
    ret

vbe_find_mode:
    mov ax, 0x4f00
    mov di, 0x1000
    mov es, ax
    xor ax, ax
    mov es, ax
    mov dword [0x1000], 0x32454256 ;VBE2
    mov ax, 0x4F00
    int 0x10
    cmp ax, 0x004F
    jne .fallback
    cmp dword [0x1000], 0x41534556 ;VESA
    jne .fallback

    ; mode list
    mov si, [0x1000 + 0x0E]
    mov bx, [0x1000 + 0x10]
    mov es, bx
.loop:
    mov cx, [es:si]
    cmp cx, 0xFFFF
    je .fallback

    push es
    push si
    xor ax, ax
    mov es, ax
    mov di, 0x2000
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .next 

    ; mode attributes
    mov ax, [0x2000 + 0x00]
    test al, 0x81
    jz .next
    mov ax, [0x2000 + 0x12] ; x
    cmp ax, 1280
    jne .next
    mov ax, [0x2000 + 0x14] ; y
    cmp ax, 960
    jne .next
    mov al, [0x2000 + 0x19] ; bpp
    cmp al, 32
    jne .next

    ; found
    mov [vbe_mode], cx
    mov eax, [0x2000 + 0x28] ; linear framebuffer
    mov [fb_addr], eax
    mov ax, [0x2000 + 0x32] ; linear bytes per scanline
    mov [fb_pitch], ax
    pop si
    pop es

    ; set the mode
    mov ax, 0x4F02
    mov bx, cx
    or  bx, 0x4000
    int 0x10
    cmp ax, 0x004F
    jne .fallback ; set failed

    mov si, msg_mode_bios
    call serial_puts
    clc
    ret

.next:
    pop si
    pop es
    add si, 2
    jmp .loop

.fallback:
    ; ID -> 0xB0C2
    mov dx, 0x01CE
    mov ax, 0x0000
    out dx, ax
    mov dx, 0x01CF
    mov ax, 0xB0C2
    out dx, ax
    ; XRES = 1280
    mov dx, 0x01CE
    mov ax, 0x0001
    out dx, ax
    mov dx, 0x01CF
    mov ax, 1280
    out dx, ax
    ; YRES = 960
    mov dx, 0x01CE
    mov ax, 0x0002
    out dx, ax
    mov dx, 0x01CF
    mov ax, 960
    out dx, ax
    ; BPP = 32
    mov dx, 0x01CE
    mov ax, 0x0003
    out dx, ax
    mov dx, 0x01CF
    mov ax, 32
    out dx, ax
    ; ENABLE = LFB_ENABLED (0x40) | ENABLED (0x01)
    mov dx, 0x01CE
    mov ax, 0x0004
    out dx, ax
    mov dx, 0x01CF
    mov ax, 0x0041
    out dx, ax

    call pci_find_stdvga
    jnc .fbok
    mov ax, 0x4F01
    mov cx, 0x0112 ; 640x480x16
    xor ax, ax
    mov es, ax
    mov di, 0x2000
    mov ax, 0x4F01
    int 0x10
    cmp ax, 0x004F
    jne .fatal_nofb
    mov ax, [0x2000 + 0x00]
    test al, 0x81
    jz  .fatal_nofb
    mov eax, [0x2000 + 0x28]
    mov [fb_addr], eax
.fbok:
    mov word [vbe_mode], 0
    mov dword [fb_pitch], 5120 ; 1280 * 4 bytes per scanline

    mov si, msg_mode_dispi
    call serial_puts
    clc
    ret

.fatal_nofb:
    mov si, msg_fatal_nofb
    call serial_puts
    stc
    ret

; pci scan
pci_find_stdvga:
    push ecx
    push edx
    push eax
    xor ecx, ecx ; device slot 0..31
.slot:
    mov eax, 0x80000000 ; config enable | bus 0
    mov edx, ecx
    shl edx, 11
    or  eax, edx
    mov dx, 0x0CF8
    out dx, eax
    mov dx, 0x0CFC
    in  eax, dx
    cmp ax, 0x1234 ; vendor
    jne .next
    shr eax, 16
    cmp ax, 0x1111 ; device
    jne .next
    ; found: read BAR0
    mov eax, 0x80000000
    mov edx, ecx
    shl edx, 11
    or  eax, edx
    or  eax, 0x10
    mov dx, 0x0CF8
    out dx, eax
    mov dx, 0x0CFC
    in  eax, dx
    and eax, 0xFFFFFFF0 ; mask low flags bits
    mov [fb_addr], eax
    clc
    jmp .done
.next:
    inc ecx
    cmp ecx, 32
    jb  .slot
    stc ; not found
.done:
    pop eax
    pop edx
    pop ecx
    ret

;e820
e820_detect:
    push es
    push edi
    push ecx
    push edx
    push eax
    xor ax, ax
    mov es, ax
    mov word [mem_count], 0
    mov word [e820_ptr], 0x3000
    xor ebx, ebx
    mov edx, 0x534D4150 ; "SMAP"
.loop:
    mov eax, 0xE820
    mov ecx, 20
    mov di, [e820_ptr]
    int 0x15
    jc  .done
    cmp eax, 0x534D4150
    jne .done
    add word [e820_ptr], 20
    inc word [mem_count]
    cmp word [mem_count], 32 ; cap at 32 entries
    jae .done
    test ebx, ebx
    jnz .loop
.done:
    pop eax
    pop edx
    pop ecx
    pop edi
    pop es
    ret

; a20
enable_a20:
    ; BIOS INT15h AX=2401
    mov ax, 0x2401
    int 0x15
    call a20_check
    jnc .done
    ; port 0x92
    in al, 0x92
    or al, 0x02
    and al, 0xFE ; keep reset bit clear
    out 0x92, al
    call a20_check
    jnc .done
    ; keyboard controller
    mov al, 0xD1
    out 0x64, al
    call kbd_wait
    mov al, 0xDF
    out 0x60, al
    call kbd_wait
    call a20_check
.done:
    ret

kbd_wait: ; wait for keyboard controller input buffer empty
    push ax
    mov ax, 0xFFFF
.loop:
    in al, 0x64
    test al, 0x02
    jz  .ready
    dec ax
    jnz .loop
.ready:
    pop ax
    ret

; returns CF=0 if A20 is on
a20_check:
    push es
    push ds
    push ax
    push cx
    xor ax, ax
    mov es, ax
    mov ax, 0xFFFF
    mov ds, ax
    mov ax, [es:0x0000] ; save word at physical 0
    push ax
    mov word [es:0x0000], 0xA1B2
    mov word [ds:0x0010], 0xA1B2 ; write to physical 0x100000
    mov cx, [es:0x0000] ; read physical 0 again
    pop ax
    mov [es:0x0000], ax ; restore
    cmp cx, 0xA1B2
    je  .wrapped
    clc
    jmp .done
.wrapped:
    stc
.done:
    pop cx
    pop ax
    pop ds
    pop es
    ret

;gdt
align 8
gdt:
    dq 0x0000000000000000 ; null
    db 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x9A, 0xCF, 0x00 ; code  sel 0x08
    db 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x92, 0xCF, 0x00 ; data  sel 0x10
gdt_end:
gdt_desc:
    dw gdt_end - gdt - 1
    dd gdt ; linear address (0x8000+off)

; bootinfo
store_bootinfo:
    push ax
    push si
    push di
    push cx
    xor ax, ax
    mov es, ax
    mov ds, ax
    mov dword [BOOTINFO + 0x00], BOOTINFO_MAGIC
    mov ax, [vbe_mode]
    mov [BOOTINFO + 0x04], ax
    mov eax, [fb_addr]
    mov [BOOTINFO + 0x08], eax
    mov eax, [fb_pitch]
    mov [BOOTINFO + 0x0C], eax
    mov word [BOOTINFO + 0x10], 1280
    mov word [BOOTINFO + 0x12], 960
    mov word [BOOTINFO + 0x14], 32
    mov ax, [mem_count]
    mov [BOOTINFO + 0x18], ax
    mov word [BOOTINFO + 0x1A], 0
    ; copy E820 entries (20 bytes each) from 0x3000 to 0x401C
    mov si, 0x3000
    mov di, 0x401C
    mov cx, [mem_count]
    test cx, cx
    jz  .done
    cld
.copy:
    push cx
    mov cx, 10 ; 20 bytes = 10 words
    rep movsw
    pop cx
    loop .copy
.done:
    pop cx
    pop di
    pop si
    pop ax
    ret

; load the kernel: header at LBA 9, kernel at LBA 10+ into 0x10000
load_kernel:
    mov dx, 0x1000
    mov es, dx
    mov si, dap_khdr
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc  .err_disk
    mov dx, 0x1000
    mov es, dx
    cmp dword [es:0x0000], 0x4B525541 ; "AURK"
    jne .err_magic
    mov eax, [es:0x0004] ; kernel size in bytes
    test eax, eax
    jz  .err_magic
    mov [kernel_size], eax
    add eax, 511 ; sectors = ceil(size / 512)
    shr eax, 9
    cmp eax, 127 ; buffer is 0x10000..0x20000 minus header
    ja  .err_big
    mov word [dap_kern + 2], ax
    mov si, dap_kern
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc  .err_disk
    mov si, msg_kernel
    call serial_puts
    mov eax, [kernel_size]
    call print_hex32
    mov si, msg_crlf
    call serial_puts
    clc
    ret
.err_disk:
    mov si, msg_krn_disk
    call serial_puts
    stc
    ret
.err_magic:
    mov si, msg_krn_magic
    call serial_puts
    stc
    ret
.err_big:
    mov si, msg_krn_big
    call serial_puts
    stc
    ret

; 32 bit
BITS 32

pmode32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x7000
    cld

    sti
    cli

    mov esi, msg_pmode
    call serial_puts_32

    ; copy the kernel from the low load buffer to 0x100000 (0x10000 holds the header, kernel image starts at 0x10200)
    mov esi, 0x10200
    mov edi, 0x100000
    mov ecx, [kernel_size]
    rep movsb

    mov esi, msg_jump
    call serial_puts_32
    mov ebx, BOOTINFO
    jmp 0x08:0x100000 ; far jump into the kernel

.halt:
    hlt
    jmp .halt

; helpers
serial_putc_32:
    push dx
    push ax
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20
    jz .wait
    mov dx, 0x3F8
    pop ax
    out dx, al
    pop dx
    ret

serial_puts_32:
    lodsb
    test al, al
    jz .done
    call serial_putc_32
    jmp serial_puts_32
.done:
    ret

; data
BITS 16
align 4
vbe_mode: dw 0
fb_addr: dd 0
fb_pitch: dd 0
mem_count: dw 0
e820_ptr: dw 0x3000
boot_drive: db 0

; kernel load DAPs
align 4
dap_khdr:
    db 0x10, 0x00 ; DAP size, reserved
    dw 1 ; count = 1 sector (header)
    dw 0x0000 ; offset
    dw 0x1000 ; segment -> 0x00010000
    dq 9 ; LBA 9 (kernel header)
dap_kern:
    db 0x10, 0x00
    dw 0 ; count filled in at runtime
    dw 0x0200 ; offset -> 0x10000 + 512
    dw 0x1000 ; segment -> 0x00010000
    dq 10 ; LBA 10 (kernel image)
kernel_size: dd 0

msg_banner: db "AURISYS stage2 up (real mode)", 13, 10, 0
msg_mode_bios: db "AURISYS stage2: 1280x960 MODE OK (VBE BIOS path)", 13, 10, 0
msg_mode_dispi: db "AURISYS stage2: 1280x960 MODE OK (Bochs-VBE registers)", 13, 10, 0
msg_fb: db "AURISYS VBE: LFB=0x", 0
msg_pitch: db " pitch=0x", 0
msg_e820: db "AURISYS E820: ", 0
msg_a20: db "AURISYS A20: enabled", 13, 10, 0
msg_pmode: db "AURISYS stage2: protected mode OK", 13, 10, 0
msg_kernel: db "AURISYS kernel: loaded size=0x", 0
msg_jump: db "AURISYS stage2: jumping to kernel @ 0x100000", 13, 10, 0
msg_crlf: db 13, 10, 0
msg_fatal_vbe:  db "AURISYS FATAL: could not set 1280x960x32 mode", 13, 10, 0
msg_fatal_nofb: db "AURISYS FATAL: framebuffer address unknown", 13, 10, 0
msg_krn_disk:  db "AURISYS FATAL: disk read error loading kernel", 13, 10, 0
msg_krn_magic: db "AURISYS FATAL: bad kernel header magic (LBA 9)", 13, 10, 0
msg_krn_big:   db "AURISYS FATAL: kernel too big for 0x10000 buffer", 13, 10, 0
msg_krn_fatal: db "AURISYS FATAL: kernel load failed", 13, 10, 0