	.text
f:
	.seh_proc f
	.seh_stackalloc 0
	.seh_stackalloc 12
	.seh_setframe %rbp, 8
	.seh_setframe %rbp, 256
	.seh_setframe %rbp, 16
	.seh_setframe %rbp, 32
	.seh_savereg %rsi, 4
	.seh_savexmm %xmm6, 8
	.seh_pushreg %rsi
	.seh_pushframe
	.seh_startepilogue
	.seh_endepilogue
	.seh_pushreg %xmm0
	.seh_savexmm %rax, 16
	.seh_pushreg 16
	.seh_handler h
	.seh_handler h, @foo
	.seh_proc g
	.seh_endproc
	.seh_endproc
