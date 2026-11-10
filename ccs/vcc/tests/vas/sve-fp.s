	fadd z0.h, z1.h, z2.h
	fsub z0.d, z1.d, z2.d
	fmul z0.s, z1.s, z2.s
	frecps z0.s, z1.s, z2.s
	fadd z0.s, p0/m, z0.s, z1.s
	fdiv z0.d, p1/m, z0.d, z2.d
	fdivr z0.h, p1/m, z0.h, z2.h
	fmaxnm z3.s, p2/m, z3.s, z4.s
	fabd z3.d, p2/m, z3.d, z4.d
	fadd z0.s, p0/m, z0.s, #0.5
	fadd z0.s, p0/m, z0.s, #1.0
	fmul z0.d, p0/m, z0.d, #2.0
	fmax z0.h, p0/m, z0.h, #0.0
	fminnm z0.s, p0/m, z0.s, #1.0
	fmla z0.s, p0/m, z1.s, z2.s
	fmls z0.d, p0/m, z1.d, z2.d
	fnmla z0.h, p0/m, z1.h, z2.h
	fmad z0.s, p0/m, z1.s, z2.s
	fnmsb z0.d, p0/m, z1.d, z2.d
	fabs z0.s, p0/m, z1.s
	fneg z0.d, p0/m, z1.d
	fsqrt z0.h, p0/m, z1.h
	frintm z0.s, p0/m, z1.s
	frinti z0.d, p0/m, z1.d
	frecpe z0.s, z1.s
	frsqrte z0.d, z1.d
	fcvt z0.h, p0/m, z1.s
	fcvt z0.d, p0/m, z1.s
	fcvt z0.s, p0/m, z1.d
	fcmgt p0.d, p1/z, z2.d, z3.d
	fcmle p0.s, p1/z, z2.s, z3.s
	fcmuo p0.h, p1/z, z2.h, z3.h
	faclt p0.s, p1/z, z2.s, z3.s
	fcmeq p0.s, p1/z, z2.s, #0.0
	fcmle p0.d, p1/z, z2.d, #0.0
	faddv s0, p1, z2.s
	fmaxnmv d0, p1, z2.d
	fminv h0, p1, z2.h
	fadda s0, p1, s0, z2.s
	fadda d3, p1, d3, z2.d
	fmov z0.s, #1.0
	fmov z0.d, #-0.125
	fmov z0.h, #0.0
	fdup z1.s, #2.5
	fmov z0.s, p1/m, #3.0
	fmov z0.d, p1/m, #0.0
	mov z0.s, s1
	mov z0.d, d31
	mov z0.b, b2
	mov z0.q, q3
	mov z0.s, z1.s[3]
	dup z0.d, z1.d[7]
	mov z0.b, z1.b[63]
	fmla z0.h, z1.h, z2.h[0]
	fmla z0.h, z1.h, z7.h[7]
	fmla z0.s, z1.s, z7.s[3]
	fmla z0.d, z1.d, z15.d[1]
	fmls z0.s, z1.s, z2.s[0]
	fmul z0.h, z1.h, z7.h[7]
	fmul z0.s, z1.s, z7.s[3]
	fmul z0.d, z1.d, z15.d[1]
	sdot z0.s, z1.b, z2.b
	udot z0.d, z1.h, z2.h
	sdot z0.s, z1.b, z7.b[3]
	udot z0.d, z1.h, z15.h[1]
	mul z0.h, z1.h, z7.h[7]
	mul z0.s, z1.s, z7.s[3]
	mul z0.d, z1.d, z15.d[1]
	mla z0.h, z1.h, z7.h[7]
	mla z0.s, z1.s, z7.s[3]
	mls z0.d, z1.d, z15.d[1]
	fadd z0.h, p0/m, z0.h, #0.5
