	.data
	.rept 70000
	.quad ext
	.endr
	.section .x,"dr"
	.rept 65535
	.long ext
	.endr
	.section .y,"dr"
	.rept 65534
	.long ext
	.endr
