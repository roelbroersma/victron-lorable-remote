ENTRY(loader_main)
MEMORY { RAM (rwx) : ORIGIN = 0x20000000, LENGTH = 16K }
SECTIONS {
 .text : { KEEP(*(.text.entry)) *(.text*) *(.rodata*) } > RAM
 .data : { *(.data*) } > RAM
 .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
 /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.eh_frame*) }
}
