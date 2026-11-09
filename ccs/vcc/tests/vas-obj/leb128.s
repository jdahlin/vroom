// .uleb128/.sleb128 with constants and backward label differences
	.text
.La:
	nop
	nop
.Lb:
	.section .debug_loclists,"",@progbits
	.uleb128 .Lb-.La
	.sleb128 -129, 63, 64
	.uleb128 300
