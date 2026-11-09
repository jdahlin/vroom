/* vcc front end: constant evaluation (integer constant expressions and foldable ones, as clang) */
#include "ccimpl.h"

VPRIV i128 trunc_to(cc *c, i128 v, qty t)
{
	int k = KIND(c, t);
	uint64_t sz;

	if (k == TY_BOOL)
		return v != 0;
	if (k == TY_PTR)
		return (i128)(uint64_t)v;
	sz = ty_size(c, t);
	if (sz >= 16)
		return is_signed(c, t) ? v : (i128)(u128)v;
	{
		unsigned bits = (unsigned)sz * 8;
		u128 m = ((u128)1 << bits) - 1;
		u128 x = (u128)v & m;

		if (is_signed(c, t) && (x >> (bits - 1)) & 1)
			return (i128)(x | ~m);
		return (i128)x;
	}
}

static uint64_t elem_size(cc *c, qty ptr)
{
	qty b = T(c, ptr)->base;
	int k = KIND(c, b);

	if (k == TY_VOID || k == TY_FUNC)
		return 1;
	return ty_size(c, b);
}

static bool eval_addr(cc *c, uint32_t n, cval *v);

VPRIV const field *node_field(cc *c, const node *x)
{
	return &c->tags[x->b].f[x->c];
}

static bool eval_addr(cc *c, uint32_t n, cval *v)
{
	const node *x = N(n);

	memset(v, 0, sizeof(*v));
	switch (x->op) {
	case N_VAR: {
		const sym *s = &c->syms[x->a];

		if (s->kind == S_FUNC || (s->kind == S_VAR && (s->flags & SF_GLOBAL || s->sc == SC_STATIC))) {
			v->kind = CV_ADDR;
			v->sym = x->a;
			return true;
		}
		return false;
	}
	case N_STR:
		v->kind = CV_ADDR;
		v->str = x->a + 1;
		return true;
	case N_COMPLIT:
		if (c->syms[x->b].flags & SF_GLOBAL || c->syms[x->b].sc == SC_STATIC) {
			v->kind = CV_ADDR;
			v->sym = x->b;
			return true;
		}
		return false;
	case N_MEMBER: {
		const field *f = node_field(c, x);

		if (f->bitfield || !eval_addr(c, x->a, v))
			return false;
		v->i += (i128)(f->off / 8);
		v->ice = false;
		return true;
	}
	case N_INDEX: {
		cval b;

		if (!eval(c, x->a, v) || !eval(c, x->b, &b) || b.kind != CV_INT)
			return false;
		if (v->kind != CV_ADDR && v->kind != CV_INT)
			return false;
		v->i += b.i * (i128)elem_size(c, N(x->a)->ty);
		v->ice = false;
		return true;
	}
	case N_DEREF:
		if (!eval(c, x->a, v))
			return false;
		v->ice = false;
		return v->kind == CV_ADDR || v->kind == CV_INT;
	default:
		return false;
	}
}

static bool is_flt_lit(cc *c, uint32_t n)
{
	while (N(n)->op == N_CAST && N(n)->aux == CK_NOP)
		n = N(n)->a;
	return N(n)->op == N_FNUM;
}

/* binary128 folding: a value is exact at that precision (ld) or a double or an integer */
static void set_q(cval *v, u128 q)
{
	v->kind = CV_FLT;
	v->ld = true;
	v->q = q;
	v->f = q_to_d(q);
}
static u128 get_q(const cval *v, bool sgn)
{
	return v->kind == CV_INT ? q_of_i(v->i, sgn) : v->ld ? v->q : q_of_d(v->f);
}
/* nonzero, as a condition */
static bool cv_nz(const cval *v) { return v->ld ? q_nz(v->q) : v->f != 0; }
/* an address that may be null: a weak symbol's (clang: Decl::isWeak) */
static bool cv_weak(const cc *c, const cval *v) { return v->kind == CV_ADDR && v->sym && (c->syms[v->sym].flags & SF_WEAK); }

VPRIV int clz128(u128 x, unsigned bits)
{
	int n = 0;

	for (int b = (int)bits - 1; b >= 0 && !((x >> b) & 1); b--)
		n++;
	return n;
}

VPRIV bool eval(cc *c, uint32_t n, cval *v)
{
	const node *x = N(n);
	cval a, b;

	memset(v, 0, sizeof(*v));
	switch (x->op) {
	case N_NUM:
		v->kind = CV_INT;
		v->i = c->ipool[x->a];
		v->ice = true;
		return true;
	case N_FNUM:
		v->kind = CV_FLT;
		v->f = c->fpool[x->a];
		if (is_q(c, x->ty))
			v->q = fnum_q(c, x->a), v->ld = true;
		v->ice = true;	/* only as the operand of a cast; arithmetic clears it */
		return true;
	case N_VAR: {
		/* a const integer variable with a constant initializer folds (not an ICE) */
		const sym *s = &c->syms[x->a];

		if (s->kind == S_VAR && (QQ(s->ty) & (Q_CONST | Q_VOLATILE)) == Q_CONST && is_integer(c, s->ty) &&
		    s->body && eval(c, s->body, v) && v->kind == CV_INT) {
			v->ice = false;
			return true;
		}
		return false;
	}
	case N_CAST: {
		qty to = x->ty;

		switch (x->aux) {
		case CK_DECAY:
		case CK_FDECAY:
			return eval_addr(c, x->a, v);
		case CK_VOID:
			return false;
		case CK_NULL:
			v->kind = CV_INT;
			v->ice = eval(c, x->a, &a) && a.ice;
			return true;
		case CK_UNION:
			return false;
		}
		if (!eval(c, x->a, &a))
			return false;
		*v = a;
		switch (x->aux) {
		case CK_NOP:
			if (a.kind == CV_INT && !is_ptr(c, to))
				v->i = trunc_to(c, a.i, to);
			return true;
		case CK_INT:
			if (a.kind == CV_ADDR && a.sublabel && ty_size(c, to) >= 4)
				return true;	/* a label difference, as a 4- or 8-byte word */
			if (a.kind != CV_INT)
				return false;
			v->i = trunc_to(c, a.i, to);
			return true;
		case CK_BOOL:
			v->kind = CV_INT;
			if (a.kind == CV_INT)
				v->i = a.i != 0;
			else if (a.kind == CV_FLT)
				v->i = cv_nz(&a), v->ice = is_flt_lit(c, x->a);
			else if (cv_weak(c, &a))
				return false;
			else
				v->i = 1, v->ice = false;	/* address of an object is never null */
			v->sym = v->str = v->label = 0;
			return true;
		case CK_I2F:
			if (a.kind != CV_INT)
				return false;
			v->kind = CV_FLT;
			v->ice = false;
			if (is_q(c, to)) {
				set_q(v, q_of_i(a.i, is_signed(c, N(x->a)->ty)));
				return true;
			}
			v->f = is_signed(c, N(x->a)->ty) ? (double)a.i : (double)(u128)a.i;
			if (KIND(c, to) == TY_FLOAT)
				v->f = is_signed(c, N(x->a)->ty) ? (double)(float)a.i : (double)(float)(u128)a.i;	/* one rounding */
			return true;
		case CK_F2I:
			if (a.kind != CV_FLT)
				return false;
			v->kind = CV_INT;
			v->i = trunc_to(c, q_to_int(get_q(&a, true), (int)ty_size(c, to) * 8, is_signed(c, to)), to);
			v->ld = false;
			v->ice = is_flt_lit(c, x->a);
			return true;
		case CK_F2F:
			if (a.kind != CV_FLT)
				return false;
			v->ice = false;
			if (is_q(c, to))
				set_q(v, get_q(&a, true));
			else if (a.ld && KIND(c, to) == TY_FLOAT)
				v->f = q_to_f(a.q), v->ld = false;	/* one rounding */
			else if (KIND(c, to) == TY_FLOAT)
				v->f = (float)a.f;
			else
				v->ld = false;	/* f already holds the nearest double */
			return true;
		case CK_P2I:
			v->ice = false;
			if (a.kind == CV_INT)
				v->i = trunc_to(c, a.i, to);
			else if (ty_size(c, to) < 8)
				return false;
			return true;
		case CK_I2P:
			if (a.kind == CV_INT)
				v->i = (i128)(uint64_t)a.i;
			/* (void *)ICE stays an ICE for null pointer constant purposes */
			return true;
		case CK_P2P:
			return true;
		}
		return false;
	}
	case N_UNARY:
		if (!eval(c, x->a, &a))
			return false;
		*v = a;
		if (a.kind == CV_FLT) {
			v->ice = false;
			if (x->aux == T_MINUS)
				v->f = -a.f, v->q = a.q ^ Q_SIGN;
			else if (x->aux == T_EXCLAIM)
				v->kind = CV_INT, v->i = !cv_nz(&a), v->ld = false;
			return true;
		}
		if (a.kind == CV_ADDR) {
			if (x->aux == T_EXCLAIM && !cv_weak(c, &a)) {
				v->kind = CV_INT, v->i = 0, v->ice = false, v->sym = v->str = v->label = 0;
				return true;
			}
			return false;
		}
		switch (x->aux) {
		case T_MINUS: v->i = trunc_to(c, -a.i, x->ty); break;
		case T_TILDE: v->i = trunc_to(c, ~a.i, x->ty); break;
		case T_EXCLAIM: v->i = a.i == 0; break;
		}
		return true;
	case N_COMPLIT:	/* (T){ v } of scalar type: its value */
		if (!is_scalar(c, x->ty) || !x->a)
			return false;
		if (N(x->a)->op == N_INIT)
			return false;
		if (!eval(c, x->a, v))
			return false;
		v->ice = false;
		return true;
	case N_CALL: {	/* strlen("literal"): clang folds the library builtin */
		uint32_t f = x->a, s;
		int l;
		const char *nm;

		while (N(f)->op == N_CAST || N(f)->op == N_ADDR)
			f = N(f)->a;
		if (N(f)->op != N_VAR || x->c != 1 || c->syms[N(f)->a].kind != S_FUNC || c->syms[N(f)->a].sc == SC_STATIC)
			return false;
		nm = idname(c->syms[N(f)->a].name, &l);
		if (l != 6 || memcmp(nm, "strlen", 6))
			return false;
		s = c->lists[x->b];
		while (N(s)->op == N_CAST)
			s = N(s)->a;
		if (N(s)->op != N_STR || c->strs[N(s)->a].esz != 1)
			return false;
		v->kind = CV_INT;
		v->ice = false;
		v->i = (i128)strlen(c->strs[N(s)->a].s);
		return true;
	}
	case N_BINARY: {
		int op = x->aux;
		qty lt = N(x->a)->ty, rt;
		bool ok;

		if (!eval(c, x->a, &a) || !eval(c, x->b, &b))
			return false;
		rt = N(x->b)->ty;
		v->kind = CV_INT;
		v->ice = a.ice && b.ice;
		/* pointer arithmetic and comparisons */
		if (is_ptr(c, lt) || is_ptr(c, rt) || a.kind == CV_ADDR || b.kind == CV_ADDR) {
			v->ice = false;
			if (is_ptr(c, x->ty)) {	/* ptr +- int */
				const cval *pv = is_ptr(c, lt) ? &a : &b, *iv = is_ptr(c, lt) ? &b : &a;
				uint64_t es = elem_size(c, is_ptr(c, lt) ? lt : rt);

				if (iv->kind != CV_INT)
					return false;
				*v = *pv;
				v->ice = false;
				v->i += (op == T_MINUS ? -iv->i : iv->i) * (i128)es;
				return true;
			}
			if (op == T_MINUS && is_ptr(c, lt) && is_ptr(c, rt) && a.kind == CV_ADDR && b.kind == CV_ADDR &&
			    a.label && b.label && a.label != b.label && !a.sublabel && !b.sublabel) {
				/* &&l1 - &&l0 */
				*v = a;
				v->ice = false;
				v->sublabel = b.label;
				v->i = a.i - b.i;
				return true;
			}
			if (op == T_MINUS && is_ptr(c, lt) && is_ptr(c, rt)) {
				if (a.kind != b.kind || a.sym != b.sym || a.str != b.str || a.label != b.label)
					return false;
				v->i = (a.i - b.i) / (i128)elem_size(c, lt);
				return true;
			}
			/* an address cast to a 64-bit integer, plus or minus an integer: a relocation */
			if ((op == T_PLUS || op == T_MINUS) && ty_size(c, x->ty) == 8 &&
			    ((a.kind == CV_ADDR && b.kind == CV_INT) || (op == T_PLUS && a.kind == CV_INT && b.kind == CV_ADDR))) {
				const cval *pv = a.kind == CV_ADDR ? &a : &b, *iv = a.kind == CV_ADDR ? &b : &a;
				i128 d = op == T_MINUS ? -iv->i : iv->i;

				*v = *pv;
				v->ice = false;
				v->i += d;
				return true;
			}
			/* comparison */
			if (a.kind == CV_ADDR || b.kind == CV_ADDR) {
				bool same = a.kind == b.kind && a.sym == b.sym && a.str == b.str && a.label == b.label;

				if (!same) {
					/* &obj vs null */
					if (((a.kind == CV_INT && a.i == 0) || (b.kind == CV_INT && b.i == 0)) && !cv_weak(c, &a) && !cv_weak(c, &b)) {
						if (op == T_EQUALEQUAL) { v->i = 0; return true; }
						if (op == T_EXCLAIMEQ) { v->i = 1; return true; }
					}
					return false;
				}
			}
			switch (op) {
			case T_EQUALEQUAL: v->i = a.i == b.i; return true;
			case T_EXCLAIMEQ: v->i = a.i != b.i; return true;
			case T_LESS: v->i = (u128)a.i < (u128)b.i; return true;
			case T_LESSEQ: v->i = (u128)a.i <= (u128)b.i; return true;
			case T_GREATER: v->i = (u128)a.i > (u128)b.i; return true;
			case T_GREATEREQ: v->i = (u128)a.i >= (u128)b.i; return true;
			case T_AMP: case T_PIPE: case T_CARET: case T_PLUS: case T_MINUS:
				if (a.kind == CV_INT && b.kind == CV_INT)
					break;	/* integers cast from pointers */
				return false;
			default:
				return false;
			}
		}
		if ((a.kind == CV_FLT || b.kind == CV_FLT) &&
		    (a.ld || b.ld || is_q(c, x->ty) || is_q(c, N(x->a)->ty) || is_q(c, N(x->b)->ty))) {
			u128 l = get_q(&a, is_signed(c, N(x->a)->ty)), r = get_q(&b, is_signed(c, N(x->b)->ty));
			int k = q_cmp(l, r);

			v->ice = false;
			v->ld = false;
			switch (op) {
			case T_PLUS: set_q(v, q_add(l, r)); break;
			case T_MINUS: set_q(v, q_sub(l, r)); break;
			case T_STAR: set_q(v, q_mul(l, r)); break;
			case T_SLASH: set_q(v, q_div(l, r)); break;
			case T_LESS: v->i = k == -1; break;
			case T_LESSEQ: v->i = k == -1 || k == 0; break;
			case T_GREATER: v->i = k == 1; break;
			case T_GREATEREQ: v->i = k == 1 || k == 0; break;
			case T_EQUALEQUAL: v->i = k == 0; break;
			case T_EXCLAIMEQ: v->i = k != 0; break;
			default: return false;
			}
			if (v->kind != CV_FLT)
				v->kind = CV_INT;
			return true;
		}
		if (a.kind == CV_FLT || b.kind == CV_FLT) {
			double l = a.kind == CV_FLT ? a.f : (double)a.i, r = b.kind == CV_FLT ? b.f : (double)b.i;

			v->ice = false;
			switch (op) {
			case T_PLUS: v->kind = CV_FLT, v->f = l + r; break;
			case T_MINUS: v->kind = CV_FLT, v->f = l - r; break;
			case T_STAR: v->kind = CV_FLT, v->f = l * r; break;
			case T_SLASH: v->kind = CV_FLT, v->f = l / r; break;
			case T_LESS: v->i = l < r; break;
			case T_LESSEQ: v->i = l <= r; break;
			case T_GREATER: v->i = l > r; break;
			case T_GREATEREQ: v->i = l >= r; break;
			case T_EQUALEQUAL: v->i = l == r; break;
			case T_EXCLAIMEQ: v->i = l != r; break;
			default: return false;
			}
			if (v->kind == CV_FLT && KIND(c, x->ty) == TY_FLOAT)
				v->f = (float)v->f;
			return true;
		}
		{
			qty ot = N(x->a)->ty;	/* operands are converted to the common type */
			bool sg = is_signed(c, ot);
			i128 l = a.i, r = b.i;
			uint64_t bits = ty_size(c, ot) * 8;

			ok = true;
			switch (op) {
			case T_PLUS: v->i = l + r; break;
			case T_MINUS: v->i = l - r; break;
			case T_STAR: v->i = sg ? l * r : (i128)((u128)l * (u128)r); break;
			case T_SLASH:
			case T_PERCENT:
				if (r == 0)
					return false;
				if (sg)
					v->i = op == T_SLASH ? l / r : l % r;
				else
					v->i = (i128)(op == T_SLASH ? (u128)trunc_to(c, l, unsigned_of(c, ot)) / (u128)trunc_to(c, r, unsigned_of(c, ot))
								    : (u128)trunc_to(c, l, unsigned_of(c, ot)) % (u128)trunc_to(c, r, unsigned_of(c, ot)));
				break;
			case T_LESSLESS:
				if (r < 0 || (uint64_t)r >= ty_size(c, x->ty) * 8)
					return false;
				v->i = (i128)((u128)l << r);
				break;
			case T_GREATERGREATER:
				if (r < 0 || (uint64_t)r >= ty_size(c, x->ty) * 8)
					return false;
				v->i = is_signed(c, x->ty) ? l >> r : (i128)((u128)trunc_to(c, l, unsigned_of(c, x->ty)) >> r);
				break;
			case T_AMP: v->i = l & r; break;
			case T_PIPE: v->i = l | r; break;
			case T_CARET: v->i = l ^ r; break;
			case T_LESS: v->i = sg ? l < r : (u128)trunc_to(c, l, unsigned_of(c, ot)) < (u128)trunc_to(c, r, unsigned_of(c, ot)); break;
			case T_LESSEQ: v->i = sg ? l <= r : (u128)trunc_to(c, l, unsigned_of(c, ot)) <= (u128)trunc_to(c, r, unsigned_of(c, ot)); break;
			case T_GREATER: v->i = sg ? l > r : (u128)trunc_to(c, l, unsigned_of(c, ot)) > (u128)trunc_to(c, r, unsigned_of(c, ot)); break;
			case T_GREATEREQ: v->i = sg ? l >= r : (u128)trunc_to(c, l, unsigned_of(c, ot)) >= (u128)trunc_to(c, r, unsigned_of(c, ot)); break;
			case T_EQUALEQUAL: v->i = trunc_to(c, l, ot) == trunc_to(c, r, ot); break;
			case T_EXCLAIMEQ: v->i = trunc_to(c, l, ot) != trunc_to(c, r, ot); break;
			default: ok = false;
			}
			(void)bits;
			if (!ok)
				return false;
			if (!is_ptr(c, x->ty))
				v->i = trunc_to(c, v->i, x->ty);
			return true;
		}
	}
	case N_LOGAND:
	case N_LOGOR: {
		bool t;

		if (!eval(c, x->a, &a))
			return false;
		if (cv_weak(c, &a))
			return false;
		t = a.kind == CV_INT ? a.i != 0 : a.kind == CV_FLT ? cv_nz(&a) : true;
		v->kind = CV_INT;
		if (a.kind == CV_ADDR)
			a.ice = false;
		if ((x->op == N_LOGAND && !t) || (x->op == N_LOGOR && t)) {
			v->i = x->op == N_LOGOR;
			v->ice = a.ice;
			return true;
		}
		if (!eval(c, x->b, &b) || cv_weak(c, &b))
			return false;
		v->i = b.kind == CV_INT ? b.i != 0 : b.kind == CV_FLT ? cv_nz(&b) : 1;
		v->ice = a.ice && b.ice && b.kind != CV_ADDR;
		return true;
	}
	case N_COND: {
		bool t, ice;

		if (!eval(c, x->a, &a))
			return false;
		if (cv_weak(c, &a))
			return false;
		t = a.kind == CV_INT ? a.i != 0 : a.kind == CV_FLT ? cv_nz(&a) : true;
		ice = a.ice && a.kind != CV_ADDR;
		if (!x->b) {	/* a ?: c */
			if (t) {
				*v = a;
				return true;
			}
			if (!eval(c, x->c, v))
				return false;
			v->ice = v->ice && ice;
			return true;
		}
		if (!eval(c, t ? x->b : x->c, v))
			return false;
		v->ice = v->ice && ice;
		return true;
	}
	case N_COMMA:
		if (!eval(c, x->b, v))
			return false;
		v->ice = false;
		return true;
	case N_ADDR:
		if (N(x->a)->op == N_LABELADDR)
			return eval(c, x->a, v);
		return eval_addr(c, x->a, v);
	case N_LABELADDR:
		v->kind = CV_ADDR;
		v->label = x->a + 1;
		return true;
	case N_BUILTIN:
		return eval_builtin(c, x, v);
	default:
		return false;
	}
}

/* is n a null pointer constant (C11 6.3.2.3p3, clang: ICE or (void *)ICE) */
VPRIV bool is_npc(cc *c, uint32_t n)
{
	const node *x = N(n);
	cval v;

	if (is_integer(c, x->ty))
		return eval(c, n, &v) && v.kind == CV_INT && v.ice && v.i == 0;
	if (is_ptr(c, x->ty) && x->op == N_CAST && (x->aux == CK_I2P || x->aux == CK_NULL || x->aux == CK_P2P)) {
		qty pt = T(c, x->ty)->base;

		if (KIND(c, pt) != TY_VOID || QQ(pt))
			return false;
		if (x->aux == CK_P2P)
			return is_npc(c, x->a);
		return eval(c, n, &v) && v.kind == CV_INT && v.ice && v.i == 0;
	}
	return false;
}

/* int value of a constant expression (array sizes, widths, cases, enums) */
VPRIV bool const_int(cc *c, uint32_t n, i128 *out)
{
	cval v;

	if (!eval(c, n, &v) || v.kind != CV_INT)
		return false;
	*out = v.i;
	return true;
}
