/* vcc front end: constant folding of builtins (cc_builtin_classify_type, cc_builtin_fold); see builtin-table.c for where a builtin's other facts live */
#include "cc-internal.h"

static int cc_builtin_classify_type(cc *c, qty t)
{
	switch (AST_TYPE_KIND(c, t)) {
	case TY_VOID: return 0;
	case TY_BOOL: return 4;
	case TY_ENUM: return 3;
	case TY_PTR: return 5;
	case TY_STRUCT: return 12;
	case TY_UNION: return 13;
	case TY_ARRAY: return 5;
	case TY_FUNC: return 5;
	default:
		if (cc_type_is_float(c, t))
			return 8;
		return 1;
	}
}

CC_PRIVATE bool cc_builtin_fold(cc *c, const node *x, cval *v)
{
	const uint32_t *arg = &c->lists[x->b];
	cval a;

	v->kind = CV_INT;
	v->ice = true;
	switch (x->aux) {
	case BI_OBJECT_SIZE: case BI_DYNAMIC_OBJECT_SIZE: {
		cc_object_bound b;
		if (!cc_constant_eval(c, arg[1], &a) || a.kind != CV_INT)
			return false;
		if (cc_builtin_object_size_pointer_bound(c, arg[0], &b, 0, (unsigned)a.i)) {
			const i128 end = (a.i & 1) && (b.whole < 0 || b.sub < b.whole) ? b.sub : b.whole;
			v->i = end < 0 ? (a.i & 2 ? 0 : (i128)(uint64_t)-1) :
			       b.offset < 0 || b.offset > end ? 0 : end - b.offset;
		} else
			v->i = a.i & 2 ? 0 : (i128)(uint64_t)-1;
		v->ice = false;
		return true;
	}
	case BI_CONSTANT_P:
		if (cc_constant_eval(c, arg[0], &a) && (a.kind == CV_INT || a.kind == CV_FLT || (a.kind == CV_ADDR && a.str && a.i == 0)))
			v->i = 1;
		else
			v->i = 0;
		return true;
	case BI_EXPECT:
	case BI_EXPECT_PROB:
		if (!cc_constant_eval(c, arg[0], v))
			return false;
		v->ice = v->ice && v->kind == CV_INT;
		return true;
	case BI_CLASSIFY_TYPE:
		v->i = cc_builtin_classify_type(c, AST_NODE(arg[0])->ty);
		return true;
	case BI_STRLEN: {
		uint32_t s = arg[0];

		while (AST_NODE(s)->op == N_CAST)
			s = AST_NODE(s)->a;
		if (AST_NODE(s)->op != N_STR)
			return false;
		v->i = (i128)strlen(c->strs[AST_NODE(s)->a].s);
		v->ice = true;	/* clang: an integer constant expression (the kernel's __is_constexpr
				   picks __builtin_strlen over __fortify_strlen in static initializers);
				   the library strlen("...") only folds (const.c) */
		return true;
	}
	case BI_HUGE_VAL: case BI_HUGE_VALF: case BI_INF: case BI_INFF:
		v->kind = CV_FLT, v->f = __builtin_inf(), v->ice = false;
		return true;
	case BI_NAN: case BI_NANF: case BI_NANS: {
		uint32_t n = arg[0];
		while (AST_NODE(n)->op == N_CAST)
			n = AST_NODE(n)->a;
		if (AST_NODE(n)->op != N_STR)
			return false;
		const char *text = c->strs[AST_NODE(n)->a].s;
		int p = AST_TYPE_KIND(c, x->ty) == TY_FLOAT16 ? 10 : AST_TYPE_KIND(c, x->ty) == TY_FLOAT ? 23 : AST_TYPE_KIND(c, x->ty) == TY_DOUBLE ? 52 :
		        c->o->target == PP_DARWIN ? 52 : c->o->target == PP_LINUX_X86_64 ? 63 : 112;
		u128 payload = 0;
		unsigned base = 10;
		bool valid = true;
		if (text[0] == '0') {
			base = 8;
			if (text[1] == 'x' || text[1] == 'X')
				base = 16, text += 2;
			if (base == 16 && !*text)
				valid = false;
		}
		for (; *text; text++) {
			unsigned digit = *text >= '0' && *text <= '9' ? (unsigned)(*text - '0') :
			                 *text >= 'a' && *text <= 'f' ? (unsigned)(*text - 'a' + 10) :
			                 *text >= 'A' && *text <= 'F' ? (unsigned)(*text - 'A' + 10) : base;
			if (digit >= base || payload > (UINT64_MAX - digit) / base) { valid = false; break; }
			payload = payload * base + digit;
		}
		if (!valid)
			return false;
		payload &= (((u128)1 << (p - 1)) - 1);
		if (x->aux == BI_NANS) {
			if (!payload)
				payload = (u128)1 << (p - 2);
		} else
			payload |= (u128)1 << (p - 1);
		v->q = (u128)0x7fff << 112 | payload << (112 - p);
		v->kind = CV_FLT, v->ld = true, v->f = vsoftfloat_to_double(v->q), v->ice = false;
		return true;
	}
	case BI_ISNAN: case BI_ISINF: case BI_ISINF_SIGN: case BI_ISFINITE: case BI_ISNORMAL: case BI_SIGNBIT: {
		if (cc_builtin_has_effects(c, arg[0], true) || !cc_constant_eval(c, arg[0], &a) || a.kind != CV_FLT)
			return false;
		u128 q = a.ld ? a.q : vsoftfloat_of_double(a.f);
		unsigned exponent = (unsigned)(q >> 112) & 0x7fff;
		int kind = AST_TYPE_KIND(c, AST_NODE(arg[0])->ty);
		unsigned minimum = kind == TY_FLOAT16 ? 16383 - 14 : kind == TY_FLOAT || kind == TY_BFLOAT16 ? 16383 - 126 :
		                   kind == TY_DOUBLE || c->o->target == PP_DARWIN ? 16383 - 1022 : 1;
		switch (x->aux) {
		case BI_ISNAN: v->i = vsoftfloat_is_nan(q); break;
		case BI_ISINF: v->i = vsoftfloat_is_infinity(q); break;
		case BI_ISINF_SIGN: v->i = vsoftfloat_is_infinity(q) ? (q >> 127 ? -1 : 1) : 0; break;
		case BI_ISFINITE: v->i = exponent != 0x7fff; break;
		case BI_ISNORMAL: v->i = exponent >= minimum && exponent != 0x7fff; break;
		default: v->i = q >> 127; break;
		}
		v->ice = false;
		return true;
	}
	case BI_ISGREATER: case BI_ISGREATEREQUAL: case BI_ISLESS: case BI_ISLESSEQUAL:
	case BI_ISLESSGREATER: case BI_ISUNORDERED: {
		cval b;
		if (x->c != 2 || cc_builtin_has_effects(c, arg[0], true) || cc_builtin_has_effects(c, arg[1], true) ||
		    !cc_constant_eval(c, arg[0], &a) || !cc_constant_eval(c, arg[1], &b) ||
		    (a.kind != CV_FLT && a.kind != CV_INT) || (b.kind != CV_FLT && b.kind != CV_INT))
			return false;
		int p = 0;
		for (unsigned i = 0; i < 2; i++) if (cc_type_is_float(c, AST_NODE(arg[i])->ty)) {
			int kind = AST_TYPE_KIND(c, AST_NODE(arg[i])->ty);
			int bits = kind == TY_BFLOAT16 ? 7 : kind == TY_FLOAT16 ? 10 : kind == TY_FLOAT ? 23 :
			           kind == TY_DOUBLE || c->o->target == PP_DARWIN ? 52 : cc_float_precision_bits(c, AST_NODE(arg[i])->ty);
			if (bits > p) p = bits;
		}
		if (!p) return false;
		u128 l = a.kind == CV_FLT ? (a.ld ? a.q : vsoftfloat_of_double(a.f)) : vsoftfloat_of_integer_with_precision(a.i, cc_type_is_signed(c, AST_NODE(arg[0])->ty), p);
		u128 r = b.kind == CV_FLT ? (b.ld ? b.q : vsoftfloat_of_double(b.f)) : vsoftfloat_of_integer_with_precision(b.i, cc_type_is_signed(c, AST_NODE(arg[1])->ty), p);
		int order = vsoftfloat_compare(l, r);
		switch (x->aux) {
		case BI_ISGREATER: v->i = order == 1; break;
		case BI_ISGREATEREQUAL: v->i = order == 1 || order == 0; break;
		case BI_ISLESS: v->i = order == -1; break;
		case BI_ISLESSEQUAL: v->i = order == -1 || order == 0; break;
		case BI_ISLESSGREATER: v->i = order == -1 || order == 1; break;
		default: v->i = order == 2; break;
		}
		v->ice = false;
		return true;
	}
	case BI_ISSIGNALING:
		if (!cc_constant_eval(c, arg[0], &a) || a.kind != CV_FLT)
			return false;
		v->i = a.ld ? vsoftfloat_is_nan(a.q) && !(a.q & ((u128)1 << 111)) : 0;
		v->ice = false;
		return true;
	case BI_CLZ: case BI_CLZL: case BI_CLZLL: case BI_CTZ: case BI_CTZL: case BI_CTZLL:
	case BI_FFS: case BI_FFSL: case BI_FFSLL: case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
	case BI_PARITY: case BI_PARITYL: case BI_PARITYLL: case BI_BSWAP16: case BI_BSWAP32: case BI_BSWAP64:
	case BI_CLRSB: case BI_CLRSBL: case BI_CLRSBLL: {
		qty at;
		unsigned bits;
		u128 u;

		if (!cc_constant_eval(c, arg[0], &a) || a.kind != CV_INT)
			return false;
		at = AST_NODE(arg[0])->ty;
		bits = (unsigned)cc_type_size(c, at) * 8;
		u = (u128)cc_constant_truncate(c, a.i, cc_type_unsigned(c, at));
		v->ice = a.ice;
		switch (x->aux) {
		case BI_CLZ: case BI_CLZL: case BI_CLZLL:
			if (!u)
				return false;
			v->i = cc_count_leading_zeros_128(u, bits);
			break;
		case BI_CTZ: case BI_CTZL: case BI_CTZLL:
			if (!u)
				return false;
			v->i = __builtin_ctzll((uint64_t)u);
			break;
		case BI_FFS: case BI_FFSL: case BI_FFSLL:
			v->i = u ? __builtin_ctzll((uint64_t)u) + 1 : 0;
			break;
		case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
			v->i = __builtin_popcountll((uint64_t)u);
			break;
		case BI_PARITY: case BI_PARITYL: case BI_PARITYLL:
			v->i = __builtin_popcountll((uint64_t)u) & 1;
			break;
		case BI_CLRSB: case BI_CLRSBL: case BI_CLRSBLL: {
			u128 s = (u >> (bits - 1)) & 1 ? ~u & (((u128)1 << bits) - 1) : u;

			v->i = cc_count_leading_zeros_128(s, bits) - 1;
			break;
		}
		case BI_BSWAP16: v->i = __builtin_bswap16((uint16_t)u); break;
		case BI_BSWAP32: v->i = __builtin_bswap32((uint32_t)u); break;
		case BI_BSWAP64: v->i = (i128)__builtin_bswap64((uint64_t)u); break;
		}
		v->i = cc_constant_truncate(c, v->i, x->ty);
		return true;
	}
	default:
		return false;
	}
}
