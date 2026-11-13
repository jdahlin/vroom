ENTRY(_start)
SECTIONS {
	. = 0x40080000;
	.text : { *(.text.start) *(.text*) *(.init.text*) }
	.rodata : { *(.rodata*) }
	.data : { *(.data*) *(.init.data*) }
	.bss : { *(.bss*) *(COMMON) }
	/DISCARD/ : { *(.note*) *(.comment) *(.discard*) }
}
