BITS 32

SECTION .text

GLOBAL _start
EXTERN main
EXTERN exit
EXTERN __init_array_start
EXTERN __init_array_end

_start:
    mov esi, __init_array_start
    mov edi, __init_array_end
.ctor_loop:
    cmp esi, edi
    jae .ctors_done
    lodsd
    call eax
    jmp .ctor_loop
.ctors_done:
    lea ebx, [esp + 4]
    mov ecx, [esp]
    push ebx
    push ecx
    call main
    push eax
    call exit

SECTION .note.GNU-stack noalloc noexec nowrite progbits
