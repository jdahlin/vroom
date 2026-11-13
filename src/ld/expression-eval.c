/* Evaluation of linker-script expressions with lld's ExprValue rules. */

/* ---------------- expressions: evaluation (lld ExprValue rules) ---------------- */

static uint64_t log2ceil(uint64_t x)
{
	if (x <= 1)
		return 0;
	return 64 - __builtin_clzll(x - 1);
}
static uint64_t chkalign(uint64_t a)
{
	if (a == 0)
		a = 1;
	if (a & (a - 1)) {
		if (in_layout)
			error("alignment must be power of 2");
		return 1;
	}
	return a;
}

static uint64_t header_size(void);
static EV eval(Expr *e);

static void move_abs_right(EV *a, EV *b)
{
	if (a->kind == EV_ABS || (a->fabs && !ev_isabs(*b))) {
		EV t = *a;
		*a = *b;
		*b = t;
	}
	if (!ev_isabs(*b) && in_layout)
		error("at least one side of the expression must be absolute");
}
static EV ev_add(EV a, EV b)
{
	move_abs_right(&a, &b);
	return evsec(a, a.fabs, ev_secoff(a) + ev_value(b));
}
static EV ev_sub(EV a, EV b)
{
	if (!ev_isabs(a) && !ev_isabs(b))
		return evabs(ev_value(a) - ev_value(b));
	return evsec(a, false, ev_secoff(a) - ev_value(b));
}
static EV ev_bit(EV a, EV b, char op)
{
	move_abs_right(&a, &b);
	uint64_t x = ev_value(a), y = ev_value(b);
	uint64_t r = op == '&' ? x & y : op == '|' ? x | y : x ^ y;
	return evsec(a, a.fabs, r - ev_secaddr(a));
}
static EV binop(const char *op, EV l, EV r)
{
	uint64_t x, y;
	if (!strcmp(op, "+")) return ev_add(l, r);
	if (!strcmp(op, "-")) return ev_sub(l, r);
	if (!strcmp(op, "&") || !strcmp(op, "|") || !strcmp(op, "^")) return ev_bit(l, r, op[0]);
	x = ev_value(l);
	y = ev_value(r);
	if (!strcmp(op, "*")) return evabs(x * y);
	if (!strcmp(op, "/")) {
		if (!y) { if (in_layout) error("division by zero"); return evabs(0); }
		return evabs(x / y);
	}
	if (!strcmp(op, "%")) {
		if (!y) { if (in_layout) error("modulo by zero"); return evabs(0); }
		return evabs(x % y);
	}
	if (!strcmp(op, "<<")) return evabs(x << (y % 64));
	if (!strcmp(op, ">>")) return evabs(x >> (y % 64));
	if (!strcmp(op, "<")) return evabs(x < y);
	if (!strcmp(op, ">")) return evabs(x > y);
	if (!strcmp(op, "<=")) return evabs(x <= y);
	if (!strcmp(op, ">=")) return evabs(x >= y);
	if (!strcmp(op, "==")) return evabs(x == y);
	if (!strcmp(op, "!=")) return evabs(x != y);
	if (!strcmp(op, "&&")) return evabs(x && y);
	if (!strcmp(op, "||")) return evabs(x || y);
	error("bad operator %s", op);
	return evabs(0);
}

static EV dot_ev(void)
{
	EV v = {.kind = EV_OSEC, .idx = cur_osec, .val = dot - osec_addr(cur_osec), .align = 1};
	return v;
}

static VVEC(const char *) assert_fail;	/* recorded failures of the current pass */

static EV eval(Expr *e)
{
	EV v;
	switch (e->op) {
	case E_NUM: return evabs(e->num);
	case E_SYM: return gsym_ev(e->sym);
	case E_DOT: return dot_ev();
	case E_ADDR:
		return (EV){.kind = EV_OSEC, .idx = e->sym, .val = 0, .align = 1};
	case E_LOADADDR: return evabs(osecs.v[e->sym].va);	/* LMA == VMA here */
	case E_SIZEOF: return evabs(osecs.v[e->sym].size);
	case E_ALIGNOF: return evabs(osecs.v[e->sym].addralign);
	case E_ALIGN1: return evabs(alignup(dot, chkalign(ev_value(eval(e->a)))));
	case E_ALIGN2:
		v = eval(e->a);
		v.align = chkalign(ev_value(eval(e->b)));
		return v;
	case E_ABSOLUTE:
		v = eval(e->a);
		v.fabs = 1;
		return v;
	case E_BIN: return binop(e->bop, eval(e->a), eval(e->b));
	case E_NEG: return evabs(-ev_value(eval(e->a)));
	case E_NOT: return evabs(~ev_value(eval(e->a)));
	case E_LNOT: return evabs(!ev_value(eval(e->a)));
	case E_TERN: return ev_value(eval(e->a)) ? eval(e->b) : eval(e->c);
	case E_ASSERT:
		if (!ev_value(eval(e->a)))
			VVEC_PUSH(assert_fail, e->msg, alloc);
		return evabs(dot);
	case E_DEFINED: {
		G *s = &gsyms[e->sym];
		return evabs(s->state == GS_OBJ || s->state == GS_SCRIPT);
	}
	case E_MAX: case E_MIN: {
		uint64_t a = ev_value(eval(e->a)), b = ev_value(eval(e->b));
		return evabs(e->op == E_MAX ? (a > b ? a : b) : (a < b ? a : b));
	}
	case E_LOG2CEIL: return evabs(log2ceil(ev_value(eval(e->a))));
	case E_SIZEOF_HEADERS: return evabs(header_size());
	}
	return evabs(0);
}
