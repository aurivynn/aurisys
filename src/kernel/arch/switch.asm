BITS 32

extern g_current
extern task_switch_now
global task_switch_asm
global task_suspend_asm
global task_resume_user

section .text

task_switch_asm:
    mov eax, [esp + 12]
    test eax, eax
    jz .kernel
    mov esp, [esp + 4]
    jmp task_resume_user
.kernel:
    mov edx, [esp + 16] ; the slot with the saved registers
    mov eax, [esp + 8] ; the esp to carry on from
    mov esp, eax
    mov ebx, [edx + 0]
    mov esi, [edx + 4]
    mov edi, [edx + 8]
    mov ebp, [edx + 12]
    sti
    ret ; pops the return address the suspend recorded

task_suspend_asm:
    mov ecx, [esp + 4] ; the slot
    mov [ecx + 20], esp ; entry esp
    push ebx
    push esi
    push edi
    push ebp
    mov [ecx + 0], ebx
    mov [ecx + 4], esi
    mov [ecx + 8], edi
    mov [ecx + 12], ebp
    mov edx, [esp + 16] ; our return address
    mov [ecx + 16], edx
    call task_switch_now

    pop ebp
    pop edi
    pop esi
    pop ebx
    sti
    ret

task_resume_user:
    popad ; the eight registers, esp drops the saved slot
    add esp, 8

    push eax
    mov ax, 0x23
    mov ds, ax
    mov es, ax
    pop eax
    
    iret
