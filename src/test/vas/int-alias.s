	sxtb w0, w1
	sxtb x0, w1
	sxth w2, w3
	sxth x2, w3
	sxtw x4, w5
	uxtb w6, w7
	uxth w8, w9
	uxtb x6, w7
	uxth x8, w9
	uxtw x4, w5
	cls w0, w1
	cls x0, x1
	clz x2, x3
	rbit w4, w5
	rev16 w0, w1
	rev32 x0, x1
	rev64 x0, x1
	ngc w0, w1
	ngc x0, x1
	ngcs w0, w1
	ngcs x0, x1
	adc w0, w1, w2
	adcs x0, x1, x2
	sbc w0, w1, w2
	sbcs x0, x1, x2
	mneg w0, w1, w2
	mneg x0, x1, x2
	smnegl x0, w1, w2
	umnegl x0, w1, w2
	smull x0, w1, w2
	umull x0, w1, w2
	smaddl x0, w1, w2, x3
	umsubl x0, w1, w2, x3
	smulh x0, x1, x2
	umulh x0, x1, x2
	sdiv w0, w1, w2
	udiv x0, x1, x2
	cneg w0, w1, eq
	cinc x0, x1, ne
	cinv w0, w1, lt
	cset w0, ge
	csetm x0, hi
	csinc w0, w1, w2, lo
	csinv x0, x1, x2, mi
	csneg w0, w1, w2, pl
	ccmp w0, #3, #4, ne
	ccmn x0, x1, #0, eq
	bfi w0, w1, #3, #4
	bfxil x0, x1, #5, #6
	sbfiz w0, w1, #2, #3
	ubfiz x0, x1, #7, #8
	sbfx w0, w1, #2, #3
	ubfx x0, x1, #7, #8
	bfc w0, #3, #4
	extr w0, w1, w2, #5
	ror x0, x1, #7
	asr w0, w1, #3
	lsl x0, x1, #60
	lsr w0, w1, #31
	tst w0, #0xff
	tst x0, x1, lsl #3
	mvn w0, w1
	mvn x0, x1, ror #3
	neg w0, w1, lsl #2
	negs x0, x1
	cmn w0, #12
	cmp x0, x1, sxtw #2
	abs w0, w1
	abs x0, x1
	cnt w0, w1
	ctz x0, x1
	smax w0, w1, w2
	smin x0, x1, #-3
	umax w0, w1, #200
	umin x0, x1, x2
	cnt v0.8b, v1.8b
	umax v0.4s, v1.4s, v2.4s
	umin v0.8b, v1.8b, v2.8b
	rev64 v0.4s, v1.4s
	smax w0,w1,#127
	umax x0,x1,#255
	bfc x0, #60, #4
