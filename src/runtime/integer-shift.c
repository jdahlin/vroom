/*
 * 128-bit shifts, libgcc's "Integer library routines":
 *
 *   __ashlti3(a, b)  a << b              __lshrti3(a, b)  a >> b, logical
 *   __ashrti3(a, b)  a >> b, arithmetic (the sign bit is copied in)
 *
 * b is in [0, 127]; anything else is undefined, as for C's operators (here
 * only its low 7 bits count).  Written on the 64-bit halves: a compiler
 * lowers a 128-bit shift by a variable count to these very functions.
 */
#include "runtime-internal.h"

vccrt_i128 __ashlti3(vccrt_i128 a, int b)
{
	vccrt_words w = { .signed_all = a };
	unsigned count = (unsigned)b & 127;
	if (count >= 64) {
		w.half.high = w.half.low << (count - 64);
		w.half.low = 0;
	} else if (count) {
		w.half.high = w.half.high << count | w.half.low >> (64 - count);
		w.half.low <<= count;
	}
	return w.signed_all;
}

vccrt_i128 __lshrti3(vccrt_i128 a, int b)
{
	vccrt_words w = { .signed_all = a };
	unsigned count = (unsigned)b & 127;
	if (count >= 64) {
		w.half.low = w.half.high >> (count - 64);
		w.half.high = 0;
	} else if (count) {
		w.half.low = w.half.low >> count | w.half.high << (64 - count);
		w.half.high >>= count;
	}
	return w.signed_all;
}

vccrt_i128 __ashrti3(vccrt_i128 a, int b)
{
	vccrt_words w = { .signed_all = a };
	unsigned count = (unsigned)b & 127;
	vccrt_i64 high = (vccrt_i64)w.half.high;
	if (count >= 64) {
		w.half.low = (vccrt_u64)(high >> (count - 64));
		w.half.high = (vccrt_u64)(high >> 63);
	} else if (count) {
		w.half.low = w.half.low >> count | w.half.high << (64 - count);
		w.half.high = (vccrt_u64)(high >> count);
	}
	return w.signed_all;
}
