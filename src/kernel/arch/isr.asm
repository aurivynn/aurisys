BITS 32

extern isr_common

section .text

%macro isr_no_err 1
global isr%1
isr%1:
	push dword 0
	push dword %1
	jmp isr_common_entry
%endmacro

%macro isr_with_err 1
global isr%1
isr%1:
	push dword %1
	jmp isr_common_entry
%endmacro

%macro irq_stub 2
global irq%1
irq%1:
	push dword 0
	push dword %2
	jmp isr_common_entry
%endmacro

; cpu exceptions, 0..31
isr_no_err 0
isr_no_err 1
isr_no_err 2
isr_no_err 3
isr_no_err 4
isr_no_err 5
isr_no_err 6
isr_no_err 7
isr_with_err 8 ; double fault
isr_no_err 9
isr_with_err 10 ; invalid tss
isr_with_err 11 ; segment not present
isr_with_err 12 ; stack fault
isr_with_err 13 ; general protection
isr_with_err 14 ; page fault
isr_no_err 15
isr_no_err 16
isr_with_err 17 ; alignment check
isr_no_err 18
isr_no_err 19
isr_no_err 20
isr_no_err 21
isr_no_err 22
isr_no_err 23
isr_no_err 24
isr_no_err 25
isr_no_err 26
isr_no_err 27
isr_no_err 28
isr_no_err 29
isr_no_err 30
isr_no_err 31

; pic irqs, 0..15 -> vectors 32..47
irq_stub 0, 32
irq_stub 1, 33
irq_stub 2, 34
irq_stub 3, 35
irq_stub 4, 36
irq_stub 5, 37
irq_stub 6, 38
irq_stub 7, 39
irq_stub 8, 40
irq_stub 9, 41
irq_stub 10, 42
irq_stub 11, 43
irq_stub 12, 44
irq_stub 13, 45
irq_stub 14, 46
irq_stub 15, 47

isr_common_entry:
	pushad
	push es
	push ds
	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov eax, esp
	add eax, 8 ; skip es+ds, point at the Registers
	push eax
	call isr_common
	add esp, 4
	pop ds
	pop es
	popad
	add esp, 8 ; drop vector + err code
	iret

section .rodata
align 4
global isr_stubs
isr_stubs:
	dd isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
	dd isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
	dd isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
	dd isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
global irq_stubs
irq_stubs:
	dd irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
	dd irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15