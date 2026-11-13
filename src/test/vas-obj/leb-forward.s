// .uleb128/.sleb128 of label differences not known yet: relaxed as MC
	.data
.La:
	.uleb128 .Lb-.La, 300
	.sleb128 .La-.Lb
	.space 120
	.uleb128 .Lc-.Lb
.Lb:
	.space 3
	.uleb128 .Lc-.La
	.p2align 3
	.byte 1
.Lc:
	.sleb128 .Lb-.Lc
