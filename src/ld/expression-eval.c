/* Evaluation of linker-script expressions with lld's ExprValue rules. */

/* ---------------- expressions: evaluation (lld ExprValue rules) ---------------- */

static uint64_t ld_integer_log2_ceil(uint64_t x)
{
	if (x <= 1)
		return 0;
	return 64 - __builtin_clzll(x - 1);
}
static uint64_t ld_alignment_check(uint64_t a)
{
	if (a == 0)
		a = 1;
	if (a & (a - 1)) {
		if (in_layout)
			ld_error_report("alignment must be power of 2");
		return 1;
	}
	return a;
}

static uint64_t ld_headers_size(void);
static ld_expression_value ld_expression_eval(ld_expression *e);

static void ld_expression_values_move_absolute_right(ld_expression_value *a, ld_expression_value *b)
{
	if (a->kind == LD_VALUE_KIND_ABSOLUTE || (a->fabs && !ld_expression_value_is_absolute(*b))) {
		ld_expression_value t = *a;
		*a = *b;
		*b = t;
	}
	if (!ld_expression_value_is_absolute(*b) && in_layout)
		ld_error_report("at least one side of the expression must be absolute");
}
static ld_expression_value ld_expression_value_add(ld_expression_value a, ld_expression_value b)
{
	ld_expression_values_move_absolute_right(&a, &b);
	return ld_expression_value_in_section_make(a, a.fabs, ld_expression_value_section_offset(a) + ld_expression_value_resolve(b));
}
static ld_expression_value ld_expression_value_subtract(ld_expression_value a, ld_expression_value b)
{
	if (!ld_expression_value_is_absolute(a) && !ld_expression_value_is_absolute(b))
		return ld_expression_value_absolute_make(ld_expression_value_resolve(a) - ld_expression_value_resolve(b));
	return ld_expression_value_in_section_make(a, false, ld_expression_value_section_offset(a) - ld_expression_value_resolve(b));
}
static ld_expression_value ld_expression_value_bitwise(ld_expression_value a, ld_expression_value b, char op)
{
	ld_expression_values_move_absolute_right(&a, &b);
	uint64_t x = ld_expression_value_resolve(a), y = ld_expression_value_resolve(b);
	uint64_t r = op == '&' ? x & y : op == '|' ? x | y : x ^ y;
	return ld_expression_value_in_section_make(a, a.fabs, r - ld_expression_value_section_address(a));
}
static ld_expression_value ld_expression_binary_eval(const char *op, ld_expression_value l, ld_expression_value r)
{
	uint64_t x, y;
	if (!strcmp(op, "+")) return ld_expression_value_add(l, r);
	if (!strcmp(op, "-")) return ld_expression_value_subtract(l, r);
	if (!strcmp(op, "&") || !strcmp(op, "|") || !strcmp(op, "^")) return ld_expression_value_bitwise(l, r, op[0]);
	x = ld_expression_value_resolve(l);
	y = ld_expression_value_resolve(r);
	if (!strcmp(op, "*")) return ld_expression_value_absolute_make(x * y);
	if (!strcmp(op, "/")) {
		if (!y) { if (in_layout) ld_error_report("division by zero"); return ld_expression_value_absolute_make(0); }
		return ld_expression_value_absolute_make(x / y);
	}
	if (!strcmp(op, "%")) {
		if (!y) { if (in_layout) ld_error_report("modulo by zero"); return ld_expression_value_absolute_make(0); }
		return ld_expression_value_absolute_make(x % y);
	}
	if (!strcmp(op, "<<")) return ld_expression_value_absolute_make(x << (y % 64));
	if (!strcmp(op, ">>")) return ld_expression_value_absolute_make(x >> (y % 64));
	if (!strcmp(op, "<")) return ld_expression_value_absolute_make(x < y);
	if (!strcmp(op, ">")) return ld_expression_value_absolute_make(x > y);
	if (!strcmp(op, "<=")) return ld_expression_value_absolute_make(x <= y);
	if (!strcmp(op, ">=")) return ld_expression_value_absolute_make(x >= y);
	if (!strcmp(op, "==")) return ld_expression_value_absolute_make(x == y);
	if (!strcmp(op, "!=")) return ld_expression_value_absolute_make(x != y);
	if (!strcmp(op, "&&")) return ld_expression_value_absolute_make(x && y);
	if (!strcmp(op, "||")) return ld_expression_value_absolute_make(x || y);
	ld_error_report("bad operator %s", op);
	return ld_expression_value_absolute_make(0);
}

static ld_expression_value ld_dot_expression_value(void)
{
	ld_expression_value v = {.kind = LD_VALUE_KIND_OUTPUT_SECTION, .idx = cur_osec, .val = dot - ld_output_section_address(cur_osec), .align = 1};
	return v;
}

static VVEC(const char *) assert_fail;	/* recorded failures of the current pass */

static ld_expression_value ld_expression_eval(ld_expression *e)
{
	ld_expression_value v;
	switch (e->op) {
	case LD_EXPRESSION_NUMBER: return ld_expression_value_absolute_make(e->num);
	case LD_EXPRESSION_SYMBOL: return ld_global_symbol_expression_value(e->sym);
	case LD_EXPRESSION_DOT: return ld_dot_expression_value();
	case LD_EXPRESSION_ADDRESS:
		return (ld_expression_value){.kind = LD_VALUE_KIND_OUTPUT_SECTION, .idx = e->sym, .val = 0, .align = 1};
	case LD_EXPRESSION_LOAD_ADDRESS: return ld_expression_value_absolute_make(osecs.v[e->sym].va);	/* LMA == VMA here */
	case LD_EXPRESSION_SIZEOF: return ld_expression_value_absolute_make(osecs.v[e->sym].size);
	case LD_EXPRESSION_ALIGNOF: return ld_expression_value_absolute_make(osecs.v[e->sym].addralign);
	case LD_EXPRESSION_ALIGN_ONE_ARGUMENT: return ld_expression_value_absolute_make(ld_value_align_up(dot, ld_alignment_check(ld_expression_value_resolve(ld_expression_eval(e->a)))));
	case LD_EXPRESSION_ALIGN_TWO_ARGUMENTS:
		v = ld_expression_eval(e->a);
		v.align = ld_alignment_check(ld_expression_value_resolve(ld_expression_eval(e->b)));
		return v;
	case LD_EXPRESSION_ABSOLUTE:
		v = ld_expression_eval(e->a);
		v.fabs = 1;
		return v;
	case LD_EXPRESSION_BINARY: return ld_expression_binary_eval(e->bop, ld_expression_eval(e->a), ld_expression_eval(e->b));
	case LD_EXPRESSION_NEGATE: return ld_expression_value_absolute_make(-ld_expression_value_resolve(ld_expression_eval(e->a)));
	case LD_EXPRESSION_BITWISE_NOT: return ld_expression_value_absolute_make(~ld_expression_value_resolve(ld_expression_eval(e->a)));
	case LD_EXPRESSION_LOGICAL_NOT: return ld_expression_value_absolute_make(!ld_expression_value_resolve(ld_expression_eval(e->a)));
	case LD_EXPRESSION_TERNARY: return ld_expression_value_resolve(ld_expression_eval(e->a)) ? ld_expression_eval(e->b) : ld_expression_eval(e->c);
	case LD_EXPRESSION_ASSERT:
		if (!ld_expression_value_resolve(ld_expression_eval(e->a)))
			VVEC_PUSH(assert_fail, e->msg, ld_region_alloc);
		return ld_expression_value_absolute_make(dot);
	case LD_EXPRESSION_DEFINED: {
		ld_global_symbol *s = &gsyms[e->sym];
		return ld_expression_value_absolute_make(s->state == LD_SYMBOL_STATE_OBJECT || s->state == LD_SYMBOL_STATE_SCRIPT);
	}
	case LD_EXPRESSION_MAX: case LD_EXPRESSION_MIN: {
		uint64_t a = ld_expression_value_resolve(ld_expression_eval(e->a)), b = ld_expression_value_resolve(ld_expression_eval(e->b));
		return ld_expression_value_absolute_make(e->op == LD_EXPRESSION_MAX ? (a > b ? a : b) : (a < b ? a : b));
	}
	case LD_EXPRESSION_LOG2CEIL: return ld_expression_value_absolute_make(ld_integer_log2_ceil(ld_expression_value_resolve(ld_expression_eval(e->a))));
	case LD_EXPRESSION_SIZEOF_HEADERS: return ld_expression_value_absolute_make(ld_headers_size());
	}
	return ld_expression_value_absolute_make(0);
}
