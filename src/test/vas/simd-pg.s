// AdvSIMD forms added for PostgreSQL's NEON code (vcc's arm_neon.h); every
// arrangement the instruction takes.  bench/vas-enc.py $PWD/src/test/vas
// checks vas's encodings against llvm-mc.
	uqsub	v0.8b, v1.8b, v2.8b
	uqsub	v0.16b, v1.16b, v2.16b
	uqsub	v3.4h, v4.4h, v5.4h
	uqsub	v3.8h, v4.8h, v5.8h
	uqsub	v6.2s, v7.2s, v8.2s
	uqsub	v6.4s, v7.4s, v8.4s
	uqsub	v9.2d, v10.2d, v31.2d
	umin	v0.8b, v1.8b, v2.8b
	umin	v0.16b, v1.16b, v2.16b
	umin	v0.4h, v1.4h, v2.4h
	umin	v0.8h, v1.8h, v2.8h
	umin	v0.2s, v1.2s, v2.2s
	umin	v0.4s, v1.4s, v2.4s
	umax	v0.8b, v1.8b, v2.8b
	umax	v0.16b, v1.16b, v2.16b
	umax	v0.8h, v1.8h, v2.8h
	umax	v30.4s, v29.4s, v28.4s
	cnt	v0.8b, v1.8b
	cnt	v31.16b, v17.16b
	uaddlp	v0.4h, v1.8b
	uaddlp	v0.8h, v1.16b
	uaddlp	v0.2s, v1.4h
	uaddlp	v0.4s, v1.8h
	uaddlp	v0.1d, v1.2s
	uaddlp	v0.2d, v1.4s
	uadalp	v0.4h, v1.8b
	uadalp	v0.8h, v1.16b
	uadalp	v0.2s, v1.4h
	uadalp	v0.4s, v1.8h
	uadalp	v0.1d, v1.2s
	uadalp	v2.2d, v3.4s
	addp	d0, v1.2d
	addp	d31, v30.2d
	addp	v0.8b, v1.8b, v2.8b
	addp	v0.16b, v1.16b, v2.16b
	addp	v0.4h, v1.4h, v2.4h
	addp	v0.8h, v1.8h, v2.8h
	addp	v0.2s, v1.2s, v2.2s
	addp	v0.4s, v1.4s, v2.4s
	addp	v0.2d, v1.2d, v2.2d
	umaxv	b0, v1.16b
	umaxv	s0, v1.4s
	addv	b0, v1.8b
	addv	h0, v1.8h
	cmeq	v0.4s, v1.4s, v2.4s
	zip1	v0.16b, v1.16b, v2.16b
	dup	v0.4s, w1
