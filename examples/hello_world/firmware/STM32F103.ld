MEMORY {
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 128K
    SRAM (rwx)  : ORIGIN = 0x20000000, LENGTH = 20K
}

SECTIONS {
    .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
    .text       : { *(.text*) }           > FLASH
    .rodata     : { *(.rodata*) }         > FLASH
}
