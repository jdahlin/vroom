/*
 * known-bits.c: which bits of an integer expression are known (kbits), and whether a constant fits
 * an inline-asm immediate constraint letter (imm_fits: arm64's I, J, K, L, M, N).
 *
 * kbits fills a known-mask and the values of those bits; it looks through casts, constants, comma,
 * conditional, bitwise operators and shifts, and is conservative: a bit not proven is not known.  Its
 * only user is arm64's inline-asm operand selection (arch/a64/codegen/asm.c), which also calls
 * imm_fits; imm_fits is the arm64 meaning of the letters (logimm_ok for the logical-immediate ones).
 */
#include "emitimpl.h"

VPRIV bool imm_fits(int ch, i128 v)
{
	switch (ch) {
	case 'I': return v >= 0 && v <= 4095;
	case 'J': return v >= -4095 && v <= 0;
	case 'K': return logimm_ok((uint64_t)v, 0) && v == (i128)(uint32_t)v;
	case 'L': return logimm_ok((uint64_t)v, 1);
	case 'M': return v == (i128)(uint32_t)v;
	case 'N': return true;
	case 'i': case 'n': case 's': return true;
	default: return false;
	}
}

VPRIV void kbits(ecx *e, uint32_t n, uint64_t *k, uint64_t *v)
{
	cc *c = e->c;
	const node *x = N(n);
	int w = is_integer(c, x->ty) ? (int)tsize(c, x->ty) * 8 : 0;
	uint64_t wm = w >= 64 ? ~0ull : w ? (1ull << w) - 1 : 0;
	uint64_t ka, va, kb, vb;

	*k = *v = 0;
	if (!w || w > 64)
		return;
	switch (x->op) {
	case N_NUM:
		*k = ~0ull;
		*v = (uint64_t)c->ipool[x->a];
		break;
	case N_COMMA:
		kbits(e, x->b, k, v);
		break;
	case N_CAST: {
		const node *a = N(x->a);
		int aw = is_integer(c, a->ty) ? (int)tsize(c, a->ty) * 8 : 0;

		if (x->aux != CK_NOP && x->aux != CK_INT && x->aux != CK_BOOL)
			return;
		kbits(e, x->a, &ka, &va);
		if (!aw || aw > 64)
			return;
		if (x->aux == CK_BOOL) {
			uint64_t am = aw >= 64 ? ~0ull : (1ull << aw) - 1;

			*k = ~1ull;	/* 0 or 1 */
			if (va & ka & am)
				*k = ~0ull, *v = 1;
			else if ((ka & am) == am)
				*k = ~0ull;
			break;
		}
		if (aw < 64) {
			/* the bits above the source: zero, or copies of its sign bit */
			uint64_t hi = ~((1ull << aw) - 1), sb = 1ull << (aw - 1);

			ka &= ~hi, va &= ~hi;
			if (!is_signed(c, a->ty))
				ka |= hi;
			else if (ka & sb)
				ka |= hi, va |= va & sb ? hi : 0;
		}
		*k = ka, *v = va;
		break;
	}
	case N_UNARY:
		if (x->aux != LEX_TOKEN_TILDE)
			return;
		kbits(e, x->a, &ka, &va);
		*k = ka, *v = ~va & ka;
		break;
	case N_BINARY:
		kbits(e, x->a, &ka, &va);
		kbits(e, x->b, &kb, &vb);
		switch (x->aux) {
		case LEX_TOKEN_AMPERSAND:	/* known where both are known or either is a known 0 */
			*k = (ka & kb) | (ka & ~va) | (kb & ~vb);
			*v = va & vb;
			break;
		case LEX_TOKEN_PIPE:	/* known where both are known or either is a known 1 */
			*k = (ka & kb) | (ka & va) | (kb & vb);
			*v = va | vb;
			break;
		case LEX_TOKEN_CARET:
			*k = ka & kb;
			*v = (va ^ vb) & *k;
			break;
		case LEX_TOKEN_LESS_LESS:
		case LEX_TOKEN_GREATER_GREATER: {
			int bw = (int)tsize(c, N(x->b)->ty) * 8;
			uint64_t s = vb;

			if (~kb & (bw >= 64 ? ~0ull : (1ull << bw) - 1))
				return;	/* an unknown shift count */
			if (bw < 64 && is_signed(c, N(x->b)->ty) && (s >> (bw - 1) & 1))
				return;	/* negative */
			if (s >= (uint64_t)w)
				return;
			if (x->aux == LEX_TOKEN_LESS_LESS) {
				*k = ka << s | ((1ull << s) - 1);
				*v = va << s;
			} else {
				/* the left operand extended to 64 bits: zeros, or copies of its sign bit */
				uint64_t sb = 1ull << (w - 1);

				ka &= wm, va &= ka;
				if (!is_signed(c, x->ty))
					ka |= ~wm;
				else if (ka & sb)
					ka |= ~wm, va |= va & sb ? ~wm : 0;
				if (is_signed(c, x->ty)) {	/* arithmetic: the top bit's knowledge moves down */
					*k = (uint64_t)((int64_t)ka >> s);
					*v = (uint64_t)((int64_t)va >> s);
				} else {
					*k = ka >> s | (s ? ~(~0ull >> s) : 0);
					*v = va >> s;
				}
			}
			break;
		}
		case LEX_TOKEN_PLUS:
		case LEX_TOKEN_MINUS:
		case LEX_TOKEN_STAR:
			if ((ka & wm) != wm || (kb & wm) != wm)
				return;
			*k = ~0ull;
			*v = x->aux == LEX_TOKEN_PLUS ? va + vb : x->aux == LEX_TOKEN_MINUS ? va - vb : va * vb;
			break;
		default:
			return;
		}
		break;
	case N_COND:
		if (!x->b)
			return;
		kbits(e, x->a, &ka, &va);
		{
			const node *cn = N(x->a);
			int cw = is_integer(c, cn->ty) ? (int)tsize(c, cn->ty) * 8 : 0;
			uint64_t cm = cw >= 64 ? ~0ull : cw ? (1ull << cw) - 1 : 0;

			if (cm && (va & ka & cm)) {
				kbits(e, x->b, k, v);
				break;
			}
			if (cm && (ka & cm) == cm) {
				kbits(e, x->c, k, v);
				break;
			}
		}
		kbits(e, x->b, &ka, &va);
		kbits(e, x->c, &kb, &vb);
		*k = ka & kb & ~(va ^ vb);
		*v = va & *k;
		break;
	default:
		return;
	}
	*k &= wm, *v &= *k;
}
