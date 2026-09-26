BITS 32

SECTION .text

GLOBAL syscall_stub
GLOBAL jump_to_app
GLOBAL exec_back
EXTERN syscall_dispatch

; the int 0x80 gate. apps are ring 3 so traps land on the tss stack
syscall_stub:
    pushad
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    push esp
    call syscall_dispatch
    add esp, 4
    ; popad would clobber the result in eax
    mov [g_sys_ret], eax
    popad
    mov eax, [g_sys_ret]
    ; iret never restores the data segments
    cmp dword [esp + 4], 0x1b
    jne .to_kernel
    push eax
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    pop eax
.to_kernel:
    iret

jump_to_app:
    push ebp
    push edi
    push esi
    push ebx
    ; [esp]=ebx [4]=esi [8]=edi [12]=ebp [16]=ret [20]=entry [24]=esp
    mov edx, .resume
    mov [g_exec_ctx_ret], edx
    mov edx, esp
    mov [g_exec_ctx_esp], edx
    mov eax, [esp + 20]
    mov ecx, [esp + 24]
    push dword 0x23 ; user ss
    push ecx ; user esp
    pushfd
    or dword [esp], 0x200 ; IF on so irqs still tick during apps
    push dword 0x1b ; user cs
    push eax ; eip
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    iret
.resume:
    pop ebx
    pop esi
    pop edi
    pop ebp
    ret

; exec_back(int code) -> back to the shells old frame
exec_back:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov esp, [g_exec_ctx_esp]
    jmp [g_exec_ctx_ret]

SECTION .data
align 4
GLOBAL g_exec_ctx_esp
GLOBAL g_exec_ctx_ret
GLOBAL g_sys_ret
g_exec_ctx_esp: dd 0
g_exec_ctx_ret: dd 0
g_sys_ret: dd 0

SECTION .note.GNU-stack noalloc noexec nowrite progbits
