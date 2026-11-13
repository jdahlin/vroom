/* Evaluation of linker-script expressions with lld's ExprValue rules. */

#include "ld-internal.h"

/* ---------------- expressions: evaluation (lld ExprValue rules) ---------------- */

static uint64_t ld_integer_log2_ceil(uint64_t x)
{
	if (x <= 1)
		return 0;
	return 64 - __builtin_clzll(x - 1);
}
static uint64_t ld_alignment_check(struct ld_context *ctx, uint64_t a)
{
	if (a == 0)
		a = 1;
	if (a & (a - 1)) {
		if (ctx->layout.in_layout)
			ld_error_report(ctx, "alignment must be power of 2");
		return 1;
	}
	return a;
}


static void ld_expression_values_move_absolute_right(struct ld_context *ctx, ld_expression_value *a, ld_expression_value *b)
{
	if (a->kind == LD_VALUE_KIND_ABSOLUTE || (a->force_absolute && !ld_expression_value_is_absolute(*b))) {
		ld_expression_value t = *a;
		*a = *b;
		*b = t;
	}
	if (!ld_expression_value_is_absolute(*b) && ctx->layout.in_layout)
		ld_error_report(ctx, "at least one side of the expression must be absolute");
}
VPRIV ld_expression_value ld_expression_value_add(struct ld_context *ctx, ld_expression_value a, ld_expression_value b)
{
	ld_expression_values_move_absolute_right(ctx, &a, &b);
	return ld_expression_value_in_section_make(a, a.force_absolute, ld_expression_value_section_offset(ctx, a) + ld_expression_value_resolve(ctx, b));
}
VPRIV ld_expression_value ld_expression_value_subtract(struct ld_context *ctx, ld_expression_value a, ld_expression_value b)
{
	if (!ld_expression_value_is_absolute(a) && !ld_expression_value_is_absolute(b))
		return ld_expression_value_absolute_make(ld_expression_value_resolve(ctx, a) - ld_expression_value_resolve(ctx, b));
	return ld_expression_value_in_section_make(a, false, ld_expression_value_section_offset(ctx, a) - ld_expression_value_resolve(ctx, b));
}
static ld_expression_value ld_expression_value_bitwise(struct ld_context *ctx, ld_expression_value a, ld_expression_value b, char op)
{
	ld_expression_values_move_absolute_right(ctx, &a, &b);
	uint64_t x = ld_expression_value_resolve(ctx, a), y = ld_expression_value_resolve(ctx, b);
	uint64_t r = op == '&' ? x & y : op == '|' ? x | y : x ^ y;
	return ld_expression_value_in_section_make(a, a.force_absolute, r - ld_expression_value_section_address(ctx, a));
}
static ld_expression_value ld_expression_binary_eval(struct ld_context *ctx, const char *op, ld_expression_value l, ld_expression_value r)
{
	uint64_t x, y;
	if (!strcmp(op, "+")) return ld_expression_value_add(ctx, l, r);
	if (!strcmp(op, "-")) return ld_expression_value_subtract(ctx, l, r);
	if (!strcmp(op, "&") || !strcmp(op, "|") || !strcmp(op, "^")) return ld_expression_value_bitwise(ctx, l, r, op[0]);
	x = ld_expression_value_resolve(ctx, l);
	y = ld_expression_value_resolve(ctx, r);
	if (!strcmp(op, "*")) return ld_expression_value_absolute_make(x * y);
	if (!strcmp(op, "/")) {
		if (!y) { if (ctx->layout.in_layout) ld_error_report(ctx, "division by zero"); return ld_expression_value_absolute_make(0); }
		return ld_expression_value_absolute_make(x / y);
	}
	if (!strcmp(op, "%")) {
		if (!y) { if (ctx->layout.in_layout) ld_error_report(ctx, "modulo by zero"); return ld_expression_value_absolute_make(0); }
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
	ld_error_report(ctx, "bad operator %s", op);
	return ld_expression_value_absolute_make(0);
}

VPRIV ld_expression_value ld_dot_expression_value(struct ld_context *ctx)
{
	ld_expression_value v = {.kind = LD_VALUE_KIND_OUTPUT_SECTION, .index = ctx->layout.current_output_section, .value = ctx->layout.dot - ld_output_section_address(ctx, ctx->layout.current_output_section), .alignment = 1};
	return v;
}


VPRIV ld_expression_value ld_expression_eval(struct ld_context *ctx, ld_expression *e)
{
	ld_expression_value v;
	switch (e->operation) {
	case LD_EXPRESSION_NUMBER: return ld_expression_value_absolute_make(e->number);
	case LD_EXPRESSION_SYMBOL: return ld_global_symbol_expression_value(ctx, e->symbol);
	case LD_EXPRESSION_DOT: return ld_dot_expression_value(ctx);
	case LD_EXPRESSION_ADDRESS:
		return (ld_expression_value){.kind = LD_VALUE_KIND_OUTPUT_SECTION, .index = e->symbol, .value = 0, .alignment = 1};
	case LD_EXPRESSION_LOAD_ADDRESS: return ld_expression_value_absolute_make(ctx->script.sections.v[e->symbol].address);	/* LMA == VMA here */
	case LD_EXPRESSION_SIZEOF: return ld_expression_value_absolute_make(ctx->script.sections.v[e->symbol].size);
	case LD_EXPRESSION_ALIGNOF: return ld_expression_value_absolute_make(ctx->script.sections.v[e->symbol].address_alignment);
	case LD_EXPRESSION_ALIGN_ONE_ARGUMENT: return ld_expression_value_absolute_make(ld_value_align_up(ctx->layout.dot, ld_alignment_check(ctx, ld_expression_value_resolve(ctx, ld_expression_eval(ctx, e->first_operand)))));
	case LD_EXPRESSION_ALIGN_TWO_ARGUMENTS:
		v = ld_expression_eval(ctx, e->first_operand);
		v.alignment = ld_alignment_check(ctx, ld_expression_value_resolve(ctx, ld_expression_eval(ctx, e->second_operand)));
		return v;
	case LD_EXPRESSION_ABSOLUTE:
		v = ld_expression_eval(ctx, e->first_operand);
		v.force_absolute = 1;
		return v;
	case LD_EXPRESSION_BINARY: return ld_expression_binary_eval(ctx, e->binary_operator, ld_expression_eval(ctx, e->first_operand), ld_expression_eval(ctx, e->second_operand));
	case LD_EXPRESSION_NEGATE: return ld_expression_value_absolute_make(-ld_expression_value_resolve(ctx, ld_expression_eval(ctx, e->first_operand)));
	case LD_EXPRESSION_BITWISE_NOT: return ld_expression_value_absolute_make(~ld_expression_value_resolve(ctx, ld_expression_eval(ctx, e->first_operand)));
	case LD_EXPRESSION_LOGICAL_NOT: return ld_expression_value_absolute_make(!ld_expression_value_resolve(ctx, ld_expression_eval(ctx, e->first_operand)));
	case LD_EXPRESSION_TERNARY: return ld_expression_value_resolve(ctx, ld_expression_eval(ctx, e->first_operand)) ? ld_expression_eval(ctx, e->second_operand) : ld_expression_eval(ctx, e->third_operand);
	case LD_EXPRESSION_ASSERT:
		if (!ld_expression_value_resolve(ctx, ld_expression_eval(ctx, e->first_operand)))
			VVEC_PUSH(ctx->script.assert_failures, e->message, ld_region_alloc);
		return ld_expression_value_absolute_make(ctx->layout.dot);
	case LD_EXPRESSION_DEFINED: {
		ld_global_symbol *s = &ctx->symbols.globals[e->symbol];
		return ld_expression_value_absolute_make(s->state == LD_SYMBOL_STATE_OBJECT || s->state == LD_SYMBOL_STATE_SCRIPT);
	}
	case LD_EXPRESSION_MAX: case LD_EXPRESSION_MIN: {
		uint64_t a = ld_expression_value_resolve(ctx, ld_expression_eval(ctx, e->first_operand)), b = ld_expression_value_resolve(ctx, ld_expression_eval(ctx, e->second_operand));
		return ld_expression_value_absolute_make(e->operation == LD_EXPRESSION_MAX ? (a > b ? a : b) : (a < b ? a : b));
	}
	case LD_EXPRESSION_LOG2CEIL: return ld_expression_value_absolute_make(ld_integer_log2_ceil(ld_expression_value_resolve(ctx, ld_expression_eval(ctx, e->first_operand))));
	case LD_EXPRESSION_SIZEOF_HEADERS: return ld_expression_value_absolute_make(ld_headers_size(ctx));
	}
	return ld_expression_value_absolute_make(0);
}
