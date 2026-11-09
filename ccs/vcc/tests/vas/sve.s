	rdvl x8, #-2
	rdvl x12, #2
	incb x8, all, mul #2
	ldr z0, [x13]
	ldr z1, [x13, #1, mul vl]
	incb x13, all, mul #2
	str z0, [x14]
	str z1, [x14, #1, mul vl]
	incb x14, all, mul #2
	rdvl x11, #2
	mov z0.b, w1
	str z0, [x12]
	str z0, [x12, #1, mul vl]
	rdvl x9, #-2
	rdvl x8, #2
	decb x11
	ldr z0, [x10]
	ldr z1, [x10, #-1, mul vl]
	decb x12, all, mul #2
	decb x10, all, mul #2
	str z0, [x11]
	str z1, [x11, #-1, mul vl]
	decb x11, all, mul #2
	mov z2.d, #0x7fffffffffffffff
	decb x9
	ldr z0, [x12]
	rev z0.b, z0.b
	rev z1.b, z1.b
	str z1, [x13, #1, mul vl]
	rev z2.b, z2.b
	index z0.d, #0, x8
	index z2.d, #0, x8
	index z0.s, #0, #1
	movprfx z1, z0
	add z1.s, z1.s, #1
	movprfx z2, z0
	add z2.s, z2.s, #5
	movprfx z3, z0
	add z3.s, z3.s, #9
	cnth x15
	mov z0.d, x8
	ptrue p0.d
	index z3.s, w14, #1
	mov z1.d, x16
	cmphi p1.d, p0/z, z0.d, z1.d
	mov z0.s, w16
	mov z1.s, w16
	uzp1 p0.s, p1.s, p1.s
	ptrue p1.s
	ptest p1, p0.b
	mov z2.d, z3.d
	inch x17
	add z3.s, z3.s, z1.s
	add z0.s, z2.s, z0.s
	incw x17
	whilels p1.s, xzr, x17
	lastb w17, p1, z0.s
	mul z1.s, z1.s, #10
	mul z0.s, z0.s, #3
	mul z1.s, z1.s, #3
	index z1.b, #1, #1
	ptrue p0.b
	ld4b { z0.b - z3.b }, p0/z, [x14]
	ld4b { z4.b - z7.b }, p0/z, [x14, #4, mul vl]
	mov z16.d, z3.d
	mov z17.d, z2.d
	st4b { z16.b - z19.b }, p0, [x14]
	st4b { z0.b - z3.b }, p0, [x14, #4, mul vl]
	addvl x14, x14, #8
	and z0.h, z0.h, #0x3
	ptrue p0.h, vl8
	asrd z0.h, p0/m, z0.h, #2
	asrd z1.h, p0/m, z1.h, #2
	mul z1.h, z1.h, #4
	mov z0.h, w8
	umulh z0.h, z1.h, z0.h
	mul z0.h, z0.h, #3
	smulh z0.h, z1.h, z0.h
	umulh z0.h, z0.h, z1.h
	umulh z1.h, z0.h, z1.h
	smulh z0.h, z0.h, z1.h
	smulh z1.h, z0.h, z1.h
	mul z0.h, z0.h, #7
	index z0.b, #1, #1
	mov z0.s, w8
	ldr z2, [x9, #1, mul vl]
	dech x10
	smulh z1.s, z1.s, z0.s
	smulh z2.s, z2.s, z0.s
	usra z1.s, z1.s, #31
	usra z2.s, z2.s, #31
	umulh z1.s, z1.s, z0.s
	umulh z2.s, z2.s, z0.s
	lsr z1.s, z1.s, #1
	lsr z2.s, z2.s, #1
	asr z3.s, z1.s, #2
	asr z4.s, z2.s, #2
	usra z3.s, z1.s, #31
	lsr z1.s, z1.s, #2
	asr z3.s, z1.s, #3
	umulh z3.s, z1.s, z0.s
	sub z1.s, z1.s, z3.s
	sub z2.s, z2.s, z4.s
	smulh z3.s, z1.s, z0.s
	index z0.s, w8, #1
	ptrue p2.s
	ld1w { z2.s }, p2/z, [x19, x8, lsl #2]
	ld1w { z3.s }, p2/z, [x20, x8, lsl #2]
	ld1w { z4.s }, p2/z, [x21, x8, lsl #2]
	cmpne p1.s, p2/z, z5.s, z3.s
	cmpne p0.s, p2/z, z4.s, z2.s
	sel p0.b, p0, p0.b, p1.b
	ptest p2, p0.b
	incw x8
	cmpne p1.s, p2/z, z4.s, z2.s
	rdvl x8, #-1
	cmpne p1.b, p0/z, z0.b, #0
	incb x9
	ptrue p0.s, vl4
	sdivr z2.s, p0/m, z2.s, z3.s
	sdiv z0.s, p0/m, z0.s, z1.s
	addvl sp, sp, #-1
	dupm z3.b, #0x20
	index z0.h, #0, w8
	mov z2.h, w9
	mov z1.h, w8
	add z5.h, z5.h, #16384
	add z1.h, z0.h, z1.h
	add z2.h, z0.h, z2.h
	add z4.h, z0.h, z4.h
	index z0.d, #0, #1
	cntw x8
	mov z2.d, #-255
	mov z16.d, x8
	mov z3.d, #16384
	ptrue p3.d
	ptrue p0.s
	incd z1.d
	mla z4.d, p3/m, z1.d, z2.d
	mla z5.d, p3/m, z0.d, z2.d
	ld1h { z6.d }, p3/z, [x19, x8, lsl #1]
	ld1h { z7.d }, p3/z, [x9, #1, mul vl]
	cmpne p1.d, p3/z, z5.d, z6.d
	cmpne p2.d, p3/z, z4.d, z7.d
	uzp1 p1.s, p1.s, p2.s
	ptest p0, p1.b
	add z0.d, z0.d, z16.d
	add z1.d, z1.d, z16.d
	str z16, [x29, #-1, mul vl]
	mov z2.d, #255
	ldr z16, [x29, #-1, mul vl]
	mov z3.d, #64
	addvl sp, sp, #1
	index z0.s, #-1, w8
	and z0.s, z0.s, #0x3
	asrd z0.s, p0/m, z0.s, #2
	asrd z1.s, p0/m, z1.s, #2
	mov z0.s, #1
	st1b { z0.b }, p0, [x9, x8]
	cntw x9
	mov z1.s, #240
	sub z3.s, z3.s, #8
	cmphi p1.s, p0/z, z1.s, z3.s
	mov z4.s, p1/z, #1
	add z0.s, z0.s, z2.s
	cntw x12
	ldr z1, [x14, #-1, mul vl]
	decw x15
	str z1, [x13, #-1, mul vl]
	index z1.s, #1, #1
	add z2.d, z2.d, #2
	add z1.d, z1.d, #4
	add z2.d, z2.d, #6
	cnth x9
	ptrue p0.h
	ld1b { z1.h }, p0/z, [x11]
	and z1.h, z1.h, #0xff
	cmpne p1.h, p0/z, z1.h, z0.h
	inch x11
	ld1b { z1.h }, p0/z, [x13, x12]
	inch x10
	mov z0.b, #-86
	cmpne p1.b, p0/z, z1.b, z0.b
	incb x11
	ld1b { z1.b }, p0/z, [x11, x10]
	incb x8
	ptrue p0.b, vl16
	and z0.b, z0.b, #0x1
	andv b0, p0, z0.b
	orv b0, p0, z0.b
	cnth x8
	ld1b { z0.h }, p0/z, [x11]
	ld1b { z1.h }, p0/z, [x11, x12]
	and z0.h, z0.h, #0xff
	cmpne p1.h, p0/z, z0.h, z1.h
	ld1b { z0.h }, p0/z, [x13]
	cmpne p1.b, p0/z, z0.b, z1.b
	ld1b { z0.b }, p0/z, [x13, x10]
	ld1b { z1.b }, p0/z, [x14, x12]
	ldr z3, [x11, #1, mul vl]
	decb x18
	uaddv d0, p0, z0.h
	index z3.s, w0, #-1
	mov z1.s, #3
	sub z4.s, z3.s, z0.s
	mad z3.s, p0/m, z1.s, z2.s
	dech x13
	mla z5.s, p0/m, z4.s, z1.s
	rev z3.s, z3.s
	rev z5.s, z5.s
	index z0.s, #4, #-4
	mov z0.d, #0xffffffff
	dech x9
	ld2w { z0.s, z1.s }, p0/z, [x8]
	cmpne p1.s, p0/z, z0.s, #-1
	cmpne p2.s, p0/z, z1.s, #0
	sel p1.b, p1, p1.b, p2.b
	decw x9
	cntd x9
	lsl z3.d, p0/m, z3.d, z0.d
	ld1d { z4.d }, p0/z, [x9, x8, lsl #3]
	cmpne p1.d, p0/z, z3.d, z4.d
	incd x8
	add z0.d, z0.d, z1.d
	index z4.b, #0, #1
	mov z6.b, w9
	ptrue p1.b
	incw z1.s
	incw z2.s, all, mul #2
	incw z3.s, all, mul #2
	and z7.s, z7.s, #0x1
	and z16.s, z16.s, #0x1
	cmpeq p2.s, p0/z, z16.s, #0
	cmpeq p3.s, p0/z, z7.s, #0
	uzp1 z7.h, z19.h, z20.h
	uzp1 z16.h, z17.h, z18.h
	cmpeq p4.s, p0/z, z22.s, #0
	uzp1 z7.b, z7.b, z16.b
	and z16.b, z16.b, #0x6
	uzp1 p2.h, p2.h, p3.h
	uzp1 z19.h, z19.h, z20.h
	add z4.b, z4.b, z6.b
	uzp1 p3.h, p4.h, p5.h
	lsl z7.b, z7.b, #1
	lsl z18.b, z16.b, #1
	uzp1 z17.b, z19.b, z17.b
	uzp1 p2.b, p2.b, p3.b
	eor z7.b, z7.b, #0xfe
	orr z18.b, z18.b, #0x1
	sel z16.b, p2, z16.b, z17.b
	mov z7.b, p2/m, z18.b
	subr z16.b, z16.b, #0
	st1b { z7.b }, p1, [x9, x8]
	st1b { z16.b }, p1, [x10, x8]
	index z3.h, w9, #1
	whilels p0.h, xzr, x9
	lastb w9, p0, z0.h
	mov z4.s, #-24
	index z1.s, #0, #1
	cmphi p1.s, p0/z, z4.s, z0.s
	sub z0.s, z0.s, #144
	cmplo p2.s, p0/z, z0.s, #112
	mov z6.s, p1/m, #8
	add z6.s, p2/m, z6.s, z5.s
	asrd z1.b, p0/m, z1.b, #7
	subr z1.b, z1.b, #0
	lsr z3.d, p0/m, z3.d, z0.d
	index z1.d, #0, #1
	lsr z3.d, p0/m, z3.d, z1.d
	asr z3.d, p0/m, z3.d, z1.d
	index z0.s, w9, w8
	udivr z0.s, p0/m, z0.s, z1.s
	mov z18.b, w8
	incd z2.d, all, mul #2
	incd z4.d, all, mul #4
	incd z3.d, all, mul #2
	uzp1 z19.s, z0.s, z1.s
	uzp1 z20.s, z2.s, z3.s
	uzp1 z21.s, z4.s, z6.s
	add z16.b, z16.b, z18.b
	uzp1 z19.b, z19.b, z20.b
	add z19.b, z19.b, #1
	cmpne p1.h, p0/z, z0.h, #0
	mls z2.s, p0/m, z4.s, z1.s
	mls z3.s, p0/m, z5.s, z1.s
	mls z2.s, p0/m, z6.s, z1.s
	mov z1.s, #-3
	mla z2.s, p0/m, z4.s, z1.s
	mla z3.s, p0/m, z5.s, z1.s
	mul z3.s, z3.s, #-3
	mul z4.s, z4.s, #-3
	mov z1.s, #-18
	mul z3.s, z3.s, #-18
	msb z3.s, p2/m, z1.s, z4.s
	ld1d { z4.d }, p0/z, [x28, x5, lsl #3]
	decw x27
	st1d { z4.d }, p0, [x28, x6, lsl #3]
	ld1d { z4.d }, p0/z, [x28, x6, lsl #3]
	st1d { z4.d }, p0, [x28, x7, lsl #3]
	st1d { z4.d }, p0, [x27, x20, lsl #3]
	index z1.s, w10, #-1
	rev z1.s, z1.s
	ld1w { z3.d }, p0/z, [x10]
	ld1w { z4.d }, p0/z, [x10, #1, mul vl]
	uzp1 p2.s, p3.s, p2.s
	index z0.s, #1, #-1
	sub z1.h, z1.h, z2.h
	sub z0.h, z0.h, z3.h
	index z1.d, x10, x10
	index z0.d, #0, x9
	mov z0.b, #38
	cmpne p0.b, p1/z, z1.b, z0.b
	mov z0.b, #54
	ld1rd { z1.d }, p0/z, [x8]
	ld1rd { z2.d }, p0/z, [x8, #8]
	ld1rd { z3.d }, p0/z, [x8, #16]
	lsl z5.d, p0/m, z5.d, z0.d
	asr z7.d, p0/m, z7.d, z0.d
	lsr z17.d, p0/m, z17.d, z0.d
	sel p1.b, p1, p1.b, p3.b
	decd x9
	index z0.h, #0, #1
	ucvtf z4.s, p0/m, z0.s
	ucvtf z3.s, p0/m, z3.s
	index z0.s, #0, w20
	ptrue p0.s, vl2
	eor z6.s, z6.s, #0x3
	eor z3.s, z3.s, #0x3
	eor z1.s, z1.s, #0x3
	eor z3.d, z5.d, z4.d
	eor3 z2.d, z2.d, z1.d, z3.d
	eorv s1, p1, z2.s
	eorv s1, p0, z1.s
	uaddv d0, p0, z0.s
	index z0.s, #0, w8
	index z3.s, #-1, #-1
	sub z5.s, z5.s, #31
	index z0.d, x10, x9
	index z1.d, #-1, x11
	index z2.d, #-2, x12
	index z6.d, x10, x9
	mov z0.d, #0x8000000000000000
	and z1.d, z1.d, #0x1
	ld4b { z0.b - z3.b }, p0/z, [x15]
	ld4b { z4.b - z7.b }, p0/z, [x15, #4, mul vl]
	addvl x15, x15, #8
	cmplo p1.d, p0/z, z0.d, #16
	cmplo p2.d, p0/z, z1.d, #16
	cmplo p3.d, p0/z, z16.d, #16
	st1w { z4.s }, p1, [x10]
	st1w { z7.s }, p2, [x10, #1, mul vl]
	mov z0.s, #0x80000000
	mov z1.s, #0x80000000
	mov z2.s, #0x80000000
	and z19.d, z4.d, z7.d
	and z20.d, z16.d, z7.d
	cmpeq p1.s, p0/z, z6.s, z19.s
	and z21.d, z17.d, z7.d
	cmpeq p2.s, p0/z, z6.s, z20.s
	mov z0.s, p1/m, z4.s
	cmpeq p3.s, p0/z, z6.s, z21.s
	mov z1.s, p2/m, z16.s
	mov z2.s, p3/m, z17.s
	smax z0.s, p0/m, z0.s, z1.s
	smax z0.s, p0/m, z0.s, z2.s
	smax z0.s, p0/m, z0.s, z3.s
	smaxv s0, p0, z0.s
	incd z3.d
	st1w { z4.s }, p1, [x9]
	st1w { z7.s }, p2, [x9, #1, mul vl]
	sdiv z2.s, p0/m, z2.s, z6.s
	sdiv z0.s, p0/m, z0.s, z2.s
	umax z0.b, p0/m, z0.b, z1.b
	umax z2.b, p0/m, z2.b, z3.b
	and z6.s, z6.s, #0x7ffffffe
	and z5.s, z5.s, #0x7ffffffe
	eor3 z3.d, z3.d, z6.d, z2.d
	eor3 z5.d, z5.d, z7.d, z4.d
	cmpne p1.b, p0/z, z0.b, #1
	index z0.d, #0, #5
	ld1h { z0.h }, p0/z, [x9, x10, lsl #1]
	mov z1.d, #0xfffffffc
	and z16.d, z16.d, #0x7
	lsl z6.d, z0.d, #1
	lsl z5.d, z5.d, #1
	and z7.d, z7.d, #0x7
	ucvtf z16.d, p0/m, z16.d
	scvtf z17.d, p0/m, z17.s
	ucvtf z6.d, p0/m, z6.d
	ucvtf z7.d, p0/m, z7.d
	scvtf z18.d, p0/m, z18.s
	cmpne p1.s, p0/z, z0.s, #0
	xar z1.d, z1.d, z0.d, #8
	xar z2.d, z2.d, z0.d, #8
	xar z1.s, z1.s, z0.s, #8
	xar z2.s, z2.s, z0.s, #8
	xar z3.s, z3.s, z0.s, #8
	xar z4.h, z4.h, z0.h, #9
	xar z5.h, z5.h, z0.h, #9
	xar z3.h, z3.h, z0.h, #9
	xar z4.b, z4.b, z0.b, #5
	xar z1.b, z1.b, z0.b, #5
	xar z5.b, z5.b, z0.b, #5
	orv s0, p0, z0.s
	cmphs p3.d, p0/z, z20.d, z1.d
	cmphs p4.d, p0/z, z22.d, z1.d
	cmphi p1.d, p0/z, z1.d, z20.d
	cmphi p2.d, p0/z, z1.d, z22.d
	cmphs p9.d, p0/z, z20.d, z2.d
	add z5.s, p1/m, z5.s, z19.s
	add z7.s, p2/m, z7.s, z19.s
	and p5.b, p4/z, p9.b, p5.b
	and p3.b, p7/z, p3.b, p6.b
	bic p6.b, p4/z, p8.b, p5.b
	bic p4.b, p7/z, p10.b, p3.b
	uaddv d1, p0, z1.s
	uaddv d2, p0, z2.s
	whilelo p0.s, xzr, x9
	st1w { z0.s }, p0, [x10, x8, lsl #2]
	whilelo p0.s, x8, x9
	udiv z0.s, p0/m, z0.s, z1.s
	index z0.s, #4, #-1
	index z0.d, #7, #-7
	index z2.d, #5, #-5
	mov z0.b, #120
	addvl x13, x13, #16
	whilelo p0.b, xzr, x13
	mov p0.b, p0/m, p0.b
	whilelo p0.b, xzr, x14
	whilelo p1.b, xzr, x7
	mov p1.b, p1/m, p1.b
	mul z0.d, z1.d, z0.d
	ld1rd { z0.d }, p0/z, [x0]
	decd x11
	mul z1.d, z1.d, z0.d
	mul z2.d, z2.d, z0.d
	index z0.d, #-1, #1
	and z1.s, z1.s, #0xff
	and z2.s, z2.s, #0xff
	orr z1.d, z1.d, z0.d
	orr z2.d, z2.d, z0.d
	add z3.b, z0.b, z1.b
	ptrue p1.h
	incd z2.d
	ld1b { z7.d }, p0/z, [x10, x9]
	ld1b { z5.d }, p0/z, [x11, #2, mul vl]
	ld1b { z6.d }, p0/z, [x11, #3, mul vl]
	ld1b { z16.d }, p0/z, [x11, #1, mul vl]
	uzp1 p2.h, p3.h, p2.h
	add z0.h, z1.h, z2.h
	sub z3.d, z4.d, z5.d
	uqsub z3.b, z4.b, z5.b
	add z0.s, z0.s, #256
	add z0.h, z0.h, #255
	sub z0.s, z0.s, #1, lsl #8
	subr z0.b, z0.b, #3
	smax z1.s, z1.s, #-5
	umin z1.h, z1.h, #200
	mul z2.d, z2.d, #-128
	orr z0.s, z0.s, #0xff00
	eor z0.h, z0.h, #0x8000
	and z0.b, z0.b, #0x7f
	dupm z1.s, #0xffff0000
	mov z0.h, #-32768
	mov z0.s, #256
	mov z0.d, #0xff00
	mov z0.b, #255
	mov z0.h, #1, lsl #8
	dup z1.s, w3
	mov z2.d, sp
	mov z3.s, p1/z, #-1
	mov z3.h, p1/m, #2, lsl #8
	mov z4.s, p2/m, w5
	mov z5.d, z6.d
	mov z7.b, p3/m, z8.b
	asr z0.b, z1.b, #8
	lsl z0.d, z1.d, #63
	lsr z0.h, z1.h, #1
	asr z0.s, p0/m, z0.s, #32
	lsl z0.b, p0/m, z0.b, #7
	asrd z0.d, p0/m, z0.d, #64
	lsr z1.d, p1/m, z1.d, z2.d
	sdivr z1.s, p1/m, z1.s, z2.s
	umulh z1.b, p1/m, z1.b, z2.b
	mls z1.h, p1/m, z2.h, z3.h
	msb z1.d, p1/m, z2.d, z3.d
	cmplt p1.s, p2/z, z3.s, z4.s
	cmpls p1.b, p2/z, z3.b, z4.b
	cmphs p1.h, p2/z, z3.h, #127
	cmple p1.d, p2/z, z3.d, #-16
	ptrue p0.d, pow2
	ptrue p0.s, mul3
	ptrues p0.b
	cntb x0
	cntd x1, vl16
	cntw x2, all, mul #16
	incd z0.d, vl2, mul #3
	dech z1.h
	decw x3, mul4
	whilelt p0.b, w1, w2
	whilehi p0.d, x1, x2
	index z0.d, x1, #-16
	index z0.b, w1, w2
	addpl x0, sp, #-32
	addvl sp, x1, #31
	ptest p1, p2.b
	orr p0.b, p1/z, p2.b, p3.b
	nand p0.b, p1/z, p2.b, p3.b
	ands p0.b, p1/z, p2.b, p3.b
	mov p0.b, p1.b
	mov p0.b, p1/z, p2.b
	not p0.b, p1/z, p2.b
	sel p0.b, p1, p2.b, p3.b
	zip1 p0.h, p1.h, p2.h
	trn2 z0.s, z1.s, z2.s
	rev p0.d, p1.d
	rev z0.h, z1.h
	movprfx z0.s, p0/z, z1.s
	movprfx z0.d, p0/m, z1.d
	saddv d0, p1, z2.b
	uminv h0, p1, z2.h
	lasta x0, p1, z2.d
	lastb s0, p1, z2.s
	fcvtzs z0.s, p0/m, z1.d
	ucvtf z0.h, p0/m, z1.s
	abs z0.s, p0/m, z1.s
	sxtw z0.d, p0/m, z1.d
	usra z0.d, z1.d, #64
	srsra z0.b, z1.b, #1
	bcax z0.d, z0.d, z1.d, z2.d
	bsl z0.d, z0.d, z1.d, z2.d
	xar z0.d, z0.d, z1.d, #1
	tbl z0.b, { z1.b }, z2.b
	ld1sb { z0.h }, p0/z, [x0, #-8, mul vl]
	ld1sw { z0.d }, p0/z, [x0, x1, lsl #2]
	ld1h { z0.s }, p0/z, [x0, x1, lsl #1]
	ld1d { z0.d }, p0/z, [x0, #7, mul vl]
	ld1rb { z0.h }, p0/z, [x0, #63]
	ld1rw { z0.s }, p0/z, [x0, #252]
	ld1rsh { z0.d }, p0/z, [x0, #2]
	st1b { z0.d }, p0, [x0, x1]
	st1h { z0.s }, p0, [x0, #-1, mul vl]
	st1d { z0.d }, p0, [x0]
	ld2w { z0.s, z1.s }, p0/z, [x0, #-16, mul vl]
	ld3h { z0.h - z2.h }, p0/z, [x0, x1, lsl #1]
	ld4d { z0.d, z1.d, z2.d, z3.d }, p0/z, [x0]
	st2b { z30.b, z31.b }, p0, [x0, #14, mul vl]
	st4w { z0.s - z3.s }, p0, [x0, x1, lsl #2]
