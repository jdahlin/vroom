ENTRY(_start)
SECTIONS {
	. = 0x40080000;
	.text : { *(.text.start) *(.text*) }
	.rodata : { *(.rodata*) }
	.init_array : { __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .; }
	.fini_array : { __fini_array_start = .; KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array)) __fini_array_end = .; }
	.data : { *(.data*) }
	.bss : { *(.bss*) *(COMMON) }
	. = ALIGN(16);
	__heap_start = .;
	/DISCARD/ : { *(.note*) *(.comment) }
}
