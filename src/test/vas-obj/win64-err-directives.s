	.text
	.type	foo,@function
	.size	foo, 4
	.hidden	foo
	.protected foo
	.internal foo
	.local	foo
	.cfi_startproc
	.cfi_endproc
	.symver	foo, foo@V1
	.weakref a1, foo
	.section .x,"ax",@progbits
	.section .y,"aw"
	.section .z,"dr",bogus,sym
	.seh_proc foo
	.seh_endproc
	.seh_pushreg %rax
	.ident	"x"
	.pushsection .q,"dr"
	.popsection
	.subsection 1
	.previous
	.gnu_attribute 4,1
	.rodata
	.bss
	.linkonce associative
	.lcomm lc, 8, 4
	.comm cc, 8, 3
	.loc 1 1
	.text
foo:
	ret
