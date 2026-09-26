BITS 32

SECTION .text

GLOBAL _start
EXTERN kernel_main
EXTERN __bss_start
EXTERN __bss_end
EXTERN __init_array_start
EXTERN __init_array_end

_start:
    cld
    mov     edi, __bss_start
    mov     ecx, __bss_end
    sub     ecx, edi
    xor     eax, eax
    rep stosb
    mov     esp, kernel_stack_top
    mov     [saved_bootinfo], ebx

    mov     esi, __init_array_start
    mov     edi, __init_array_end
.ctor_loop:
    cmp     esi, edi
    jae     .ctors_done
    lodsd
    call    eax
    jmp     .ctor_loop
.ctors_done:

    push    dword [saved_bootinfo]
    call    kernel_main
    add     esp, 4

.halt:
    cli
    hlt
    jmp     .halt

SECTION .data
align 4
saved_bootinfo: dd 0

SECTION .bss
align 16
kernel_stack:
    resb 16384
kernel_stack_top: