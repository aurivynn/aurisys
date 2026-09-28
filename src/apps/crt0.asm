BITS 32

SECTION .text

GLOBAL _start
EXTERN main
EXTERN exit
EXTERN environ
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
    mov ecx, [esp] ; argc
    lea eax, [esp + 8] ; past argc and past the argv null
    mov ebx, ecx
    shl ebx, 2
    add eax, ebx ; envp
    mov [environ], eax
    lea ebx, [esp + 4] ; argv
    push ebx
    push ecx
    call main
    push eax
    call exit

SECTION .note.GNU-stack noalloc noexec nowrite progbits
