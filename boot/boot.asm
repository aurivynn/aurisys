;; bootloader. for aurisys.

BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    mov [boot_drive], dl ; bios passes boot drive in DL

    call serial_init
    mov si, msg_stage1
    call serial_puts

    ; check int13h support
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .no_ext
    cmp bx, 0xAA55
    jne .no_ext

    ;; read stage2. lba 1 8 sectors to physical 0x8000
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc .disk_error

    mov si, msg_ok
    call serial_puts
    jmp 0x0000:0x8000

;;jkhas dfghjklads fg
.no_ext:
    mov si, msg_no_ext
    call serial_puts
    jmp .halt
.disk_error:
    mov si, msg_disk
    call serial_puts
.halt:
    hlt
    jmp .halt

serial_init:
    push dx
    push ax
    mov dx, 0x3FB
    mov al, 0x80 ; DLAB
    out dx, al
    mov dx, 0x3F8
    mov ax, 0x0001 ; divisor = 1
    out dx, al
    mov al, ah
    mov dx, 0x3F9
    out dx, al
    mov dx, 0x3FB
    mov al, 0x03 ; 8N1 dlab 0
    out dx, al
    pop ax
    pop dx
    ret

serial_putc:
    push dx
    push ax
.wait:
    mov dx, 0x3FD
    in al, dx
    test al, 0x20 ; thhr empty?
    jz .wait
    mov dx, 0x3f8
    pop ax
    out dx, al
    pop dx
    ret

serial_puts:
    lodsb
    test al, al
    jz .done
    call serial_putc
    jmp serial_puts
.done:
    ret

;; data
dap:
    db 0x10
    db 0x00
    dw 8
    dw 0x8000
    dw 0x0000
    dq 1

boot_drive: db 0

msg_stage1: db "AURISYS stage1 up. loading stage2", 13, 10, 0
msg_ok: db "AURISYS stage1: stage2 loaded, jmping", 13, 10, 0
msg_no_ext: db "AURISYS FATAL: in13h lba extension missing", 13, 10, 0
msg_disk: db "AURISYS FATAL: STAGE2 disk read failed", 13, 10, 0

TIMES 510-($-$$) db 0
dw 0xAA55