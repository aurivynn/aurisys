ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    .text : ALIGN(4K)
    {
        *(.text*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata*)
    }

    .data : ALIGN(4K)
    {
        *(.data*)
    }

    .init_array : ALIGN(4)
    {
        __init_array_start = .;
        KEEP(*(.init_array*))
        __init_array_end = .;
    }

    .bss : ALIGN(4K)
    {
        __bss_start = .;
        *(COMMON)
        *(.bss*)
        . = ALIGN(16);
        __bss_end = .;
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.rel.eh_frame*)
    }
}