/* vcc code generator: instruction records, byte buffers, immediates, register names */
#include "emitimpl.h"

const char cond_name[][8] = { "eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al" };

VPRIV void tb_init(tbuf *b, size_t cap)
{
	b->p = reserve(cap);
	b->n = 0;
	b->cap = cap;
}

/* a buffer in a reused region (arena.h) */
VPRIV void tb_reg(tbuf *b, vmreg *r, size_t cap, const char *name)
{
	b->p = vmreg_get(r, cap, name);
	b->n = 0;
	b->cap = cap;
}

VPRIV void tb_u_slow(tbuf *b, uint64_t v)
{
	char t[24];
	int i = 24;

	do
		t[--i] = (char)('0' + v % 10);
	while ((v /= 10));
	tb_put(b, t + i, (size_t)(24 - i));
}

VPRIV void tb_i(tbuf *b, int64_t v)
{
	if (v < 0) {
		tb_c(b, '-');
		tb_u(b, (uint64_t)0 - (uint64_t)v);
	} else {
		tb_u(b, (uint64_t)v);
	}
}

VPRIV void tb_hex(tbuf *b, uint64_t v)
{
	char t[20];
	int i = 20;

	do
		t[--i] = "0123456789abcdef"[v & 15];
	while ((v >>= 4));
	t[--i] = 'x';
	t[--i] = '0';
	tb_put(b, t + i, (size_t)(20 - i));
}

VPRIV void tb_f(tbuf *b, const char *fmt, ...)
{
	va_list ap;
	int n;

	tb_need(b, 512);
	va_start(ap, fmt);
	n = vsnprintf(b->p + b->n, b->cap - b->n, fmt, ap);
	va_end(ap);
	b->n += (size_t)n;
}

/* ---------------- immediates ---------------- */

/* AArch64 logical immediate: is v encodable for this width? */
VPRIV bool logimm_ok(uint64_t v, int x)
{
	unsigned size = 64;

	if (!x)
		v = (v & 0xffffffffu) | (v << 32);
	if (v == 0 || v == ~(uint64_t)0)
		return false;
	/* smallest repeating element */
	while (size > 2) {
		unsigned h = size / 2;
		uint64_t m = (h == 64 ? ~(uint64_t)0 : (((uint64_t)1 << h) - 1));

		if (((v >> h) & m) != (v & m))
			break;
		size = h;
	}
	{
		uint64_t m = size == 64 ? ~(uint64_t)0 : (((uint64_t)1 << size) - 1);
		uint64_t e = v & m;
		/* e must be a rotated run of ones */
		uint64_t t = e;

		for (unsigned r = 0; r < size; r++) {
			/* contiguous ones starting at bit 0: t & (t + 1) == 0 */
			if ((t & (t + 1)) == 0)
				return true;
			t = ((t >> 1) | ((t & 1) << (size - 1))) & m;
		}
		return false;
	}
}
