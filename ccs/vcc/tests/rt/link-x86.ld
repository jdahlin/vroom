ENTRY(_start)
SECTIONS {
	. = 0x401000;	/* (each segment on its own pages: their permissions differ) */
	.text : { *(.text.start) *(.text*) }
	. = ALIGN(4096);
	.rodata : { *(.rodata*) }
	.eh_frame : { *(.eh_frame) }
	. = ALIGN(4096);
	.init_array : { __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .; }
	.fini_array : { __fini_array_start = .; KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array)) __fini_array_end = .; }
	.data : { *(.data*) }
	.got : { *(.got) }
	. = ALIGN(4096);	/* (.got is RELRO: what follows is a segment of its own, whose first page the kernel maps anew) */
	.tdata : { __tdata_start = .; *(.tdata*) }
	.tbss : { *(.tbss*) }
	__tdata_size = SIZEOF(.tdata);
	/* the thread pointer is the block's end rounded to its alignment (variant 2): the block starts this far below it */
	__tls_size = ALIGN(SIZEOF(.tdata) + SIZEOF(.tbss), MAX(ALIGNOF(.tdata), ALIGNOF(.tbss)));
	.bss : { *(.bss*) *(COMMON) }
	. = ALIGN(16);
	__heap_start = .;
	/DISCARD/ : { *(.note.GNU-stack) *(.comment) }
}
