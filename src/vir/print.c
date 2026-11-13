/*
 * print.c: VIR's text form, printing (SPEC-vir.md 11).
 *
 * The text is the reference everyone reads and the tests compare (src/test/vir): constants inline
 * with a `:type` suffix exactly where the position does not fix the type (11.5), values and blocks
 * named v0.. and b0.. in order of appearance (11.6), instruction flags in the order of 11.3.
 * Printing and parsing are inverses: printing the parse of a text gives the text back
 * (`virtest` and bench/vir-test.py check the round trip), so a change here is a change of the
 * format and goes with parse.c and the spec.  Printing reads the opcode table (opcode-table.c) and never
 * changes the function.
 */
#include "vir-internal.h"

typedef struct vir_print_state {
	vbuffer *buffer;
	const vir_unit *unit;
	const vir_function *function;
	const uint32_t *names;	/* value index -> N of vN (VIR_BLOCK_NONE: none) */
} vir_print_state;

static const char *const vir_target_names[VIR_TARGET_COUNT] = { "none", "x86_64-linux", "arm64-linux", "arm64-darwin" };

const char *vir_target_name(uint32_t t) { return t < VIR_TARGET_COUNT ? vir_target_names[t] : "none"; }

static void vir_print_append(vir_print_state *p, const char *s) { vbuffer_string_append(p->buffer, s); }

__attribute__((format(printf, 2, 3)))
static void vir_print_append_format(vir_print_state *p, const char *fmt, ...)
{
	char t[512];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vsnprintf(t, sizeof(t), fmt, ap);
	va_end(ap);
	vbuffer_append(p->buffer, t, n < (int)sizeof(t) ? (size_t)n : sizeof(t) - 1);
}

/* ---------------- numbers and strings ---------------- */

static void vir_print_hexfloat_format(char *o, uint64_t bits, int ebits, int mbits)
{
	uint64_t mant = bits & (((uint64_t)1 << mbits) - 1);
	int e = (int)((bits >> mbits) & (((uint64_t)1 << ebits) - 1)), sign = (int)(bits >> (mbits + ebits)) & 1;
	int bias = (1 << (ebits - 1)) - 1;

	if (e == 0 && mant == 0) {
		sprintf(o, "%s0x0p+0", sign ? "-" : "");
	} else if (e == 0 || e == (1 << ebits) - 1) {
		sprintf(o, "bits:0x%llx", (unsigned long long)bits);
	} else {
		int nd = (mbits + 3) / 4;
		char digs[20];

		mant <<= nd * 4 - mbits;
		for (int i = nd - 1; i >= 0; i--, mant >>= 4)
			digs[i] = "0123456789abcdef"[mant & 15];
		digs[nd] = 0;
		while (nd && digs[nd - 1] == '0')
			digs[--nd] = 0;
		sprintf(o, "%s0x1%s%s" "p%+d", sign ? "-" : "", nd ? "." : "", digs, e - bias);
	}
}

static void vir_print_string_append(vir_print_state *p, const char *s)
{
	vbuffer_append(p->buffer, "\"", 1);
	for (const unsigned char *c = (const unsigned char *)s; *c; c++) {
		if (*c == '"' || *c == '\\')
			vir_print_append_format(p, "\\%c", *c);
		else if (*c == '\n')
			vir_print_append(p, "\\n");
		else if (*c == '\t')
			vir_print_append(p, "\\t");
		else if (*c >= 32 && *c < 127)
			vbuffer_append(p->buffer, c, 1);
		else
			vir_print_append_format(p, "\\%03o", *c);
	}
	vbuffer_append(p->buffer, "\"", 1);
}

static void vir_print_bytes_append(vir_print_state *p, const char *s, uint64_t n)
{
	vbuffer_append(p->buffer, "\"", 1);
	for (uint64_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];

		if (c == '"' || c == '\\')
			vir_print_append_format(p, "\\%c", c);
		else if (c == '\n')
			vir_print_append(p, "\\n");
		else if (c == '\t')
			vir_print_append(p, "\\t");
		else if (c >= 32 && c < 127)
			vbuffer_append(p->buffer, &c, 1);
		else
			vir_print_append_format(p, "\\%03o", c);
	}
	vbuffer_append(p->buffer, "\"", 1);
}

/* ---------------- signatures and groups ---------------- */

void vir_print_group(vbuffer *b, uint32_t w0, uint32_t w1)
{
	switch (VIR_GROUP_KIND(w0)) {
	case VIR_GROUP_NONE: vbuffer_string_append(b, "none"); break;
	case VIR_GROUP_SRET: vbuffer_string_append(b, "sret"); break;
	case VIR_GROUP_MEM: vbuffer_format_append(b, "mem %u align %u", VIR_GROUP_SIZE(w1), 1u << VIR_GROUP_ALIGN_LOG2(w1)); break;
	default:
		for (uint32_t i = 0; i < VIR_GROUP_PIECE_COUNT(w0) && i < 4; i++) {
			uint32_t c = VIR_GROUP_CLASS(w0, i);

			vbuffer_format_append(b, "%s%s", i ? ":" : "", c == VIR_CLASS_INT ? "int" : c == VIR_CLASS_F32 ? "f32" : c == VIR_CLASS_F64 ? "f64" : "?");
		}
		break;
	}
}

void vir_print_signature(vbuffer *b, const uint32_t *w)
{
	uint32_t ng = VIR_SIGNATURE_GROUP_COUNT(w[0]);

	vbuffer_string_append(b, "(");
	for (uint32_t i = 0; i < ng; i++) {
		if (i)
			vbuffer_string_append(b, ", ");
		vir_print_group(b, w[3 + 2 * i], w[4 + 2 * i]);
	}
	if (VIR_SIGNATURE_IS_VARIADIC(w[0]))
		vbuffer_string_append(b, ng ? ", ..." : "...");
	vbuffer_string_append(b, ") -> ");
	vir_print_group(b, w[1], w[2]);
}

static uint32_t vir_print_signature_words(const uint32_t *w) { return 3 + 2 * VIR_SIGNATURE_GROUP_COUNT(w[0]); }

/* the unit's index of the signature at extra offset off, or ~0 */
static uint32_t vir_print_signature_index(const vir_print_state *p, uint32_t off)
{
	const vir_function *f = p->function;
	const vir_unit *u = p->unit;

	if (!u || off == 0 || off >= f->extra_count || (uint64_t)off + vir_print_signature_words(&f->extra[off]) > f->extra_count)
		return VIR_BLOCK_NONE;
	for (uint32_t i = 0; i < u->signature_count; i++)
		if (u->signatures[i].count == vir_print_signature_words(&f->extra[off]) &&
		    !memcmp(&u->signature_words[u->signatures[i].offset], &f->extra[off], (size_t)u->signatures[i].count * 4))
			return i;
	return VIR_BLOCK_NONE;
}

/* ---------------- operands ---------------- */

static void vir_print_symbol_append(vir_print_state *p, uint32_t s, int64_t add)
{
	if (p->unit && s < p->unit->symbol_count)
		vir_print_append_format(p, "@%s", p->unit->symbols[s].name);
	else
		vir_print_append_format(p, "@s%u", s);
	if (add)
		vir_print_append_format(p, "%+lld", (long long)add);
}

static void vir_print_constant_append(vir_print_state *p, const vir_insn *c)
{
	uint64_t v = (uint64_t)c->b | (uint64_t)c->c << 32;
	char t[48];

	switch (c->op) {
	case VIR_OP_ICONST:
		if (c->type == VIR_TYPE_I32)
			vir_print_append_format(p, "%d", (int32_t)c->b);
		else if (c->type == VIR_TYPE_PTR)
			vir_print_append_format(p, "0x%llx", (unsigned long long)v);
		else
			vir_print_append_format(p, "%lld", (long long)v);
		break;
	case VIR_OP_FCONST:
		if (c->type == VIR_TYPE_F32)
			vir_print_hexfloat_format(t, c->b, 8, 23);
		else
			vir_print_hexfloat_format(t, v, 11, 52);
		vir_print_append(p, t);
		break;
	case VIR_OP_SYMADDR: vir_print_symbol_append(p, c->a, (int64_t)v); break;
	case VIR_OP_SLOTADDR:
		vir_print_append_format(p, "$%u", c->a);
		if (v)
			vir_print_append_format(p, "%+lld", (long long)v);
		break;
	case VIR_OP_LABELADDR: vir_print_append_format(p, "&b%u", c->a); break;
	}
}

/* an operand: a value name or an inline constant; typed: the suffix is required */
static void vir_print_value_append(vir_print_state *p, uint32_t v, bool typed)
{
	const vir_function *f = p->function;

	if (v >= f->insn_count) {
		vir_print_append_format(p, "v?%u", v);
	} else if (vir_opcode_is_constant(f->insns[v].op)) {
		vir_print_constant_append(p, &f->insns[v]);
		if (typed)
			vir_print_append_format(p, ":%s", f->insns[v].type < VIR_TYPE_COUNT ? vir_type_names[f->insns[v].type] : "?");
	} else if (p->names[v] == VIR_BLOCK_NONE) {
		vir_print_append_format(p, "v?%u", v);
	} else {
		vir_print_append_format(p, "v%u", p->names[v]);
	}
}

static bool vir_print_value_is_constant(const vir_print_state *p, uint32_t v) { return v < p->function->insn_count && vir_opcode_is_constant(p->function->insns[v].op); }

/* operand k (0 a, 1 b, 2 c) of in: does the text need the type suffix? */
static bool vir_print_type_is_needed(const vir_print_state *p, const vir_insn *in, uint32_t k)
{
	uint32_t other = k == 0 ? in->b : in->a;

	if (vir_operand_type_fixed(in->op, in->type, k) != VIR_TYPE_VOID)
		return false;
	if ((in->op == VIR_OP_ICMP || in->op == VIR_OP_FCMP) && !vir_print_value_is_constant(p, other))
		return false;
	return true;
}

static void vir_print_operand_append(vir_print_state *p, const vir_insn *in, uint32_t k)
{
	uint32_t v = k == 0 ? in->a : k == 1 ? in->b : in->c;

	vir_print_value_append(p, v, vir_print_value_is_constant(p, v) && vir_print_type_is_needed(p, in, k));
}

/* ---------------- instructions ---------------- */

static void vir_print_flags_append(vir_print_state *p, const vir_insn *in)
{
	uint32_t fl = in->flags, op = in->op;

	switch (op) {
	case VIR_OP_ADD: case VIR_OP_SUB: case VIR_OP_MUL: case VIR_OP_SHL: case VIR_OP_NEG:
		if (fl & VIR_INSN_FLAG_NSW) vir_print_append(p, " nsw");
		if (fl & VIR_INSN_FLAG_NUW) vir_print_append(p, " nuw");
		break;
	case VIR_OP_SDIV: case VIR_OP_UDIV: case VIR_OP_LSHR: case VIR_OP_ASHR:
		if (fl & VIR_INSN_FLAG_EXACT) vir_print_append(p, " exact");
		break;
	case VIR_OP_CLZ: case VIR_OP_CTZ:
		if (fl & VIR_INSN_FLAG_NZ) vir_print_append(p, " nz");
		break;
	case VIR_OP_FMA:
		if (fl & VIR_INSN_FLAG_NEGP) vir_print_append(p, " negp");
		if (fl & VIR_INSN_FLAG_NEGC) vir_print_append(p, " negc");
		break;
	case VIR_OP_LOAD: case VIR_OP_STORE: case VIR_OP_ALOAD: case VIR_OP_ASTORE: case VIR_OP_AXCHG: case VIR_OP_AXADD: case VIR_OP_ACAS:
		if (fl & VIR_INSN_FLAG_VOL) vir_print_append(p, " volatile");
		if (VIR_INSN_FLAG_ALIGN_LOG2(fl)) vir_print_append_format(p, " align %u", 1u << VIR_INSN_FLAG_ALIGN_LOG2(fl));
		if (op == VIR_OP_LOAD && in->b) vir_print_append_format(p, " tag %u", in->b);
		if (op == VIR_OP_STORE && in->c) vir_print_append_format(p, " tag %u", in->c);
		break;
	case VIR_OP_MEMCPY: case VIR_OP_MEMSET:
		if (VIR_INSN_FLAG_ALIGN_LOG2(fl)) vir_print_append_format(p, " align %u", 1u << VIR_INSN_FLAG_ALIGN_LOG2(fl));
		if (op == VIR_OP_MEMCPY && VIR_INSN_FLAG_SOURCE_ALIGN_LOG2(fl) != VIR_INSN_FLAG_ALIGN_LOG2(fl)) vir_print_append_format(p, " align %u", 1u << VIR_INSN_FLAG_SOURCE_ALIGN_LOG2(fl));
		break;
	case VIR_OP_CALL:
		if (fl & VIR_INSN_FLAG_CALL_NORETURN) vir_print_append(p, " noreturn");
		if (fl & VIR_INSN_FLAG_CALL_PURE) vir_print_append(p, " pure");
		if (fl & VIR_INSN_FLAG_CALL_CONST) vir_print_append(p, " const");
		break;
	case VIR_OP_BRIF:
		if (fl & VIR_INSN_FLAG_LIKELY) vir_print_append(p, " likely");
		if (fl & VIR_INSN_FLAG_UNLIKELY) vir_print_append(p, " unlikely");
		break;
	}
}

static const char *vir_print_condition_name(uint32_t op, uint32_t cc, char *t)
{
	if ((op == VIR_OP_ICMP || op == VIR_OP_VICMP) && cc < 10)
		return vir_integer_condition_names[cc];
	if ((op == VIR_OP_FCMP || op == VIR_OP_VFCMP) && cc < 8)
		return vir_float_condition_names[cc];
	sprintf(t, "cc%u", cc);
	return t;
}

static void vir_print_group_append(vir_print_state *p, uint32_t off)
{
	const vir_function *f = p->function;

	if (off && (uint64_t)off + 2 <= f->extra_count)
		vir_print_group(p->buffer, f->extra[off], f->extra[off + 1]);
	else
		vir_print_append(p, "?");
}

static void vir_print_list_values_append(vir_print_state *p, uint32_t base, uint32_t n, const uint32_t *tys)
{
	const vir_function *f = p->function;

	for (uint32_t i = 0; i < n; i++) {
		uint32_t v = (uint64_t)base + i < f->extra_count ? f->extra[base + i] : 0;

		if (i)
			vir_print_append(p, ", ");
		vir_print_value_append(p, v, vir_print_value_is_constant(p, v) && !(tys && tys[i]));
	}
}

static void vir_print_insn_append(vir_print_state *p, const vir_insn *in, uint32_t idx)
{
	const vir_opcode_info *oi = &vir_opcode_table[in->op];
	const vir_function *f = p->function;
	char t[16];
	uint32_t ne = f->extra_count, nop = 0;

	vir_print_append(p, "  ");
	if (!oi->name) {
		vir_print_append_format(p, "op%u", in->op);
		return;
	}
	if (in->type != VIR_TYPE_VOID && p->names[idx] != VIR_BLOCK_NONE)
		vir_print_append_format(p, "v%u:%s = ", p->names[idx], in->type < VIR_TYPE_COUNT ? vir_type_names[in->type] : "?");
	switch (oi->form) {
	case VIR_FORM_CC:
		vir_print_append_format(p, "%s.%s ", oi->name, vir_print_condition_name(in->op, in->flags & 15, t));
		vir_print_operand_append(p, in, 0);
		vir_print_append(p, ", ");
		vir_print_operand_append(p, in, 1);
		return;
	case VIR_FORM_EXT:
		vir_print_append_format(p, "ext.%u%c ", 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags), in->flags & VIR_INSN_FLAG_MS ? 's' : 'u');
		vir_print_operand_append(p, in, 0);
		return;
	case VIR_FORM_LOAD:
		vir_print_append_format(p, "load.%u", 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags));
		if (in->type == VIR_TYPE_I32 || in->type == VIR_TYPE_I64 || (in->flags & VIR_INSN_FLAG_MS))
			vir_print_append_format(p, "%c", in->flags & VIR_INSN_FLAG_MS ? 's' : 'u');
		vir_print_append(p, " ");
		vir_print_operand_append(p, in, 0);
		vir_print_flags_append(p, in);
		return;
	case VIR_FORM_STORE:
		vir_print_append_format(p, "store.%u ", 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags));
		vir_print_operand_append(p, in, 0);
		vir_print_append(p, ", ");
		vir_print_operand_append(p, in, 1);
		vir_print_flags_append(p, in);
		return;
	case VIR_FORM_ATOM:
		vir_print_append_format(p, "%s.%u ", oi->name, 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags));
		for (uint32_t k = 0; k < 3 && oi->operand_kinds[k] != VIR_OPERAND_KIND_NONE; k++) {
			if (k)
				vir_print_append(p, ", ");
			vir_print_operand_append(p, in, k);
		}
		vir_print_flags_append(p, in);
		return;
	case VIR_FORM_MEMCPY:
		vir_print_append_format(p, "%s ", oi->name);
		vir_print_operand_append(p, in, 0);
		vir_print_append(p, ", ");
		vir_print_operand_append(p, in, 1);
		vir_print_append(p, ", ");
		vir_print_operand_append(p, in, 2);
		vir_print_flags_append(p, in);
		return;
	case VIR_FORM_PARAM:
		if (in->a == VIR_GROUP_RESULT)
			vir_print_append_format(p, "param 0xffff.%u", in->b);
		else
			vir_print_append_format(p, "param %u.%u", in->a, in->b);
		return;
	case VIR_FORM_TLS:
		vir_print_append_format(p, "tlsaddr ");
		vir_print_symbol_append(p, in->a, 0);
		vir_print_append_format(p, ", %u", in->b);
		return;
	case VIR_FORM_VAARG:
		vir_print_append(p, "va_arg ");
		vir_print_operand_append(p, in, 0);
		vir_print_append(p, ", ");
		vir_print_group_append(p, in->b);
		return;
	case VIR_FORM_CALL: {
		uint32_t si = vir_print_signature_index(p, in->b), n = in->c < ne ? f->extra[in->c] : 0;

		vir_print_append(p, "call ");
		vir_print_operand_append(p, in, 0);
		if (si != VIR_BLOCK_NONE)
			vir_print_append_format(p, " %%s%u (", si);
		else
			vir_print_append(p, " %?? (");
		if ((uint64_t)in->c + 1 + n > ne)
			n = 0;
		vir_print_list_values_append(p, in->c + 1, n, NULL);
		vir_print_append(p, ")");
		vir_print_flags_append(p, in);
		return;
	}
	case VIR_FORM_ASM: case VIR_FORM_ASMGOTO: {
		uint32_t no = 0, ni = 0, b = in->b;

		vir_print_append_format(p, "%s !%u outs(", oi->name, in->a);
		if ((uint64_t)b + 2 <= ne) {
			no = f->extra[b];
			ni = f->extra[b + 1];
		}
		if ((uint64_t)b + 2 + no + 2 * (uint64_t)ni > ne)
			no = ni = 0;
		for (uint32_t i = 0; i < no; i++)
			vir_print_append_format(p, "%s%u", i ? ", " : "", f->extra[b + 2 + i]);
		vir_print_append(p, ") ins(");
		for (uint32_t i = 0; i < ni; i++) {
			uint32_t d = f->extra[b + 2 + no + 2 * i], v = f->extra[b + 3 + no + 2 * i];

			vir_print_append_format(p, "%s%u ", i ? ", " : "", d);
			vir_print_value_append(p, v, vir_print_value_is_constant(p, v));
		}
		vir_print_append(p, ")");
		if (oi->form == VIR_FORM_ASMGOTO) {
			uint32_t n = in->c < ne ? f->extra[in->c] : 0;

			if ((uint64_t)in->c + 2 + n > ne)
				n = 0, nop = 1;
			vir_print_append_format(p, " -> b%u [", nop ? 0 : f->extra[in->c + 1]);
			for (uint32_t i = 0; i < n; i++)
				vir_print_append_format(p, "%sb%u", i ? ", " : "", f->extra[in->c + 2 + i]);
			vir_print_append(p, "]");
		}
		return;
	}
	case VIR_FORM_BR: {
		uint32_t n = in->a < f->block_count ? f->blocks[in->a].parameter_count : 0;

		vir_print_append_format(p, "br b%u", in->a);
		if (n && (uint64_t)in->c + n <= ne) {
			vir_print_append(p, "(");
			for (uint32_t i = 0; i < n; i++) {
				uint32_t v = f->extra[in->c + i], want = f->blocks[in->a].first + i < f->insn_count ?
					f->insns[f->blocks[in->a].first + i].type : 0;
				bool typed = vir_print_value_is_constant(p, v) && (!want || want >= VIR_TYPE_COUNT || f->insns[v].type != want);

				if (i)
					vir_print_append(p, ", ");
				vir_print_value_append(p, v, typed);
			}
			vir_print_append(p, ")");
		}
		return;
	}
	case VIR_FORM_SWITCH: {
		uint32_t n = in->c < ne ? f->extra[in->c] : 0, ty = in->a < f->insn_count ? f->insns[in->a].type : 0;
		uint32_t st = ty == VIR_TYPE_I64 ? 5 : 3;

		if ((uint64_t)in->c + 1 + (uint64_t)n * st > ne)
			n = 0;
		vir_print_append(p, "switch ");
		vir_print_operand_append(p, in, 0);
		vir_print_append_format(p, ", b%u [", in->b);
		for (uint32_t i = 0; i < n; i++) {
			const uint32_t *w = &f->extra[in->c + 1 + i * st];
			uint64_t lo, hi;
			bool wide = ty == VIR_TYPE_I64;

			lo = wide ? (uint64_t)w[0] | (uint64_t)w[1] << 32 : w[0];
			hi = wide ? (uint64_t)w[2] | (uint64_t)w[3] << 32 : w[1];
			if (i)
				vir_print_append(p, ", ");
			if (wide)
				vir_print_append_format(p, "%lld", (long long)lo);
			else
				vir_print_append_format(p, "%d", (int32_t)lo);
			if (hi != lo) {
				if (wide)
					vir_print_append_format(p, "..%lld", (long long)hi);
				else
					vir_print_append_format(p, "..%d", (int32_t)hi);
			}
			vir_print_append_format(p, ": b%u", w[st - 1]);
		}
		vir_print_append(p, "]");
		return;
	}
	case VIR_FORM_IBR: {
		uint32_t n = in->c < ne ? f->extra[in->c] : 0;

		if ((uint64_t)in->c + 1 + n > ne)
			n = 0;
		vir_print_append(p, "ibr ");
		vir_print_operand_append(p, in, 0);
		vir_print_append(p, " [");
		for (uint32_t i = 0; i < n; i++)
			vir_print_append_format(p, "%sb%u", i ? ", " : "", f->extra[in->c + 1 + i]);
		vir_print_append(p, "]");
		return;
	}
	case VIR_FORM_RET: {
		uint32_t n = in->c < ne ? f->extra[in->c] : 0;

		if ((uint64_t)in->c + 1 + n > ne)
			n = 0;
		vir_print_append(p, "ret");
		if (n)
			vir_print_append(p, " ");
		vir_print_list_values_append(p, in->c + 1, n, NULL);
		return;
	}
	case VIR_FORM_NONE:
		vir_print_append(p, oi->name);
		return;
	default:
		break;
	}
	/* VIR_FORM_STD: the operands the table lists, in order */
	vir_print_append(p, oi->name);
	for (uint32_t k = 0; k < 3; k++) {
		uint32_t kd = oi->operand_kinds[k], v = k == 0 ? in->a : k == 1 ? in->b : in->c;

		if (kd == VIR_OPERAND_KIND_NONE)
			continue;
		vir_print_append(p, nop++ ? ", " : " ");
		if (kd == VIR_OPERAND_KIND_IMM)
			vir_print_append_format(p, "%u", v);
		else if (kd == VIR_OPERAND_KIND_BLK)
			vir_print_append_format(p, "b%u", v);
		else
			vir_print_operand_append(p, in, k);
	}
	vir_print_flags_append(p, in);
}

/* ---------------- functions and units ---------------- */

static void vir_print_function_append(vir_print_state *p, const vir_function *f)
{
	const vir_unit *u = p->unit;
	uint32_t si = vir_print_signature_index(p, f->signature);

	vir_print_append_format(p, "func ");
	vir_print_symbol_append(p, f->symbol, 0);
	if (si != VIR_BLOCK_NONE)
		vir_print_append_format(p, " %%s%u", si);
	else
		vir_print_append(p, " %??");
	if (f->flags & VIR_FUNCTION_FLAG_NOFP) vir_print_append(p, " nofp");
	if (f->flags & VIR_FUNCTION_FLAG_FENV) vir_print_append(p, " fenv");
	if (f->flags & VIR_FUNCTION_FLAG_NULLINVALID) vir_print_append(p, " nullinvalid");
	if (f->flags & VIR_FUNCTION_FLAG_NORETURN) vir_print_append(p, " noreturn");
	if (f->flags & VIR_FUNCTION_FLAG_LOCAL) vir_print_append(p, " local");
	if (f->flags & VIR_FUNCTION_FLAG_EXPOSURE) vir_print_append(p, " exposure");
	vir_print_append(p, " {\n");
	(void)u;
	for (uint32_t i = 0; i < f->slot_count; i++) {
		const vir_slot *s = &f->slot[i];

		vir_print_append_format(p, "  slot $%u size %llu align %u", i, (unsigned long long)s->size, s->align);
		if (s->hint != INT64_MIN)
			vir_print_append_format(p, " hint %lld", (long long)s->hint);
		if (s->flags & VIR_SLOT_FLAG_PARAM)
			vir_print_append(p, " param");
		if (s->flags & VIR_SLOT_FLAG_EXPOSED)
			vir_print_append(p, " exposed");
		vir_print_append(p, "\n");
	}
	for (uint32_t b = 0; b < f->block_count; b++) {
		const vir_block *k = &f->blocks[b];
		uint32_t np = 0;

		vir_print_append_format(p, "b%u", b);
		for (uint32_t i = k->first; i < k->first + k->insn_count && i < f->insn_count; i++) {
			if (vir_opcode_is_constant(f->insns[i].op))
				continue;
			if (f->insns[i].op != VIR_OP_BPARAM || np >= k->parameter_count)
				break;
			vir_print_append(p, np++ ? ", " : "(");
			vir_print_append_format(p, "v%u:%s", p->names[i], f->insns[i].type < VIR_TYPE_COUNT ? vir_type_names[f->insns[i].type] : "?");
		}
		if (np)
			vir_print_append(p, ")");
		if (k->flags & VIR_BLOCK_FLAG_LOOP) vir_print_append(p, " loop");
		if (k->flags & VIR_BLOCK_FLAG_ADDR) vir_print_append(p, " addr");
		if (k->flags & VIR_BLOCK_FLAG_COLD) vir_print_append(p, " cold");
		vir_print_append(p, ":\n");
		{
			uint32_t skip = np;

			for (uint32_t i = k->first; i < k->first + k->insn_count && i < f->insn_count; i++) {
				if (vir_opcode_is_constant(f->insns[i].op))
					continue;
				if (skip) {
					skip--;
					continue;
				}
				vir_print_insn_append(p, &f->insns[i], i);
				vir_print_append(p, "\n");
			}
		}
	}
	vir_print_append(p, "}\n");
}

void vir_print_function(vbuffer *b, const vir_unit *u, const vir_function *f)
{
	vir_print_state p = { b, u, f, f->scratch };
	uint32_t *nm = f->scratch, n = 0;

	/* names: v0.. in order of appearance */
	for (uint32_t i = 0; i < f->insn_count; i++)
		nm[i] = VIR_BLOCK_NONE;
	for (uint32_t k = 0; k < f->block_count; k++)
		for (uint32_t i = f->blocks[k].first; i < f->blocks[k].first + f->blocks[k].insn_count && i < f->insn_count; i++)
			if (!vir_opcode_is_constant(f->insns[i].op) && f->insns[i].type != VIR_TYPE_VOID)
				nm[i] = n++;
	vir_print_function_append(&p, f);
}

static void vir_print_items_append(vir_print_state *p, const vir_unit *u, const vir_unit_symbol *s)
{
	for (uint32_t i = 0; i < s->item_count; i++) {
		const vir_item *it = &u->item[s->item_first + i];

		vir_print_append(p, i ? ", " : " ");
		if (it->type == 0xff) {
			vir_print_append_format(p, "zero %llu", (unsigned long long)it->value);
		} else if (it->type == 0xfe) {
			vir_print_append(p, "bytes ");
			vir_print_bytes_append(p, it->bytes, it->value);
		} else if (it->type == VIR_TYPE_I32) {
			vir_print_append_format(p, "i32 %d", (int32_t)it->value);
		} else if (it->type == VIR_TYPE_I64) {
			vir_print_append_format(p, "i64 %lld", (long long)it->value);
		} else if (it->type == VIR_TYPE_F32 || it->type == VIR_TYPE_F64) {
			char t[48];

			vir_print_hexfloat_format(t, it->value, it->type == VIR_TYPE_F32 ? 8 : 11, it->type == VIR_TYPE_F32 ? 23 : 52);
			vir_print_append_format(p, "%s %s", vir_type_names[it->type], t);
		} else if (it->symbol) {
			vir_print_append(p, "ptr ");
			vir_print_symbol_append(p, it->symbol - 1, (int64_t)it->value);
		} else {
			vir_print_append_format(p, "ptr 0x%llx", (unsigned long long)it->value);
		}
	}
}

void vir_print_unit(vbuffer *b, const vir_unit *u)
{
	vir_print_state p = { b, u, NULL, NULL };

	vir_print_append_format(&p, "vir %u\ntarget %s\n", u->version, vir_target_name(u->target));
	for (uint32_t i = 0; i < u->symbol_count; i++)
		if (u->symbols[i].kind == VIR_UNIT_ITEM_EXTERN)
			vir_print_append_format(&p, "extern @%s\n", u->symbols[i].name);
	for (uint32_t i = 0; i < u->symbol_count; i++) {
		const vir_unit_symbol *s = &u->symbols[i];

		if (s->kind != VIR_UNIT_ITEM_DATA && s->kind != VIR_UNIT_ITEM_TDATA)
			continue;
		vir_print_append_format(&p, "%s @%s align %u = {", s->kind == VIR_UNIT_ITEM_TDATA ? "tdata" : "data", s->name, s->align);
		vir_print_items_append(&p, u, s);
		vir_print_append(&p, " }\n");
	}
	for (uint32_t i = 0; i < u->signature_count; i++) {
		vir_print_append_format(&p, "sig %%s%u = ", i);
		vir_print_signature(b, &u->signature_words[u->signatures[i].offset]);
		vir_print_append(&p, "\n");
	}
	for (uint32_t i = 0; i < u->asm_count; i++) {
		const vir_unit_asm *a = &u->asms[i];

		vir_print_append_format(&p, "asm !%u ", i);
		vir_print_string_append(&p, a->template);
		vir_print_append(&p, " outs(");
		for (uint32_t k = 0; k < a->output_count; k++) {
			vir_print_append(&p, k ? ", " : "");
			vir_print_string_append(&p, a->outputs[k]);
		}
		vir_print_append(&p, ") ins(");
		for (uint32_t k = 0; k < a->input_count; k++) {
			vir_print_append(&p, k ? ", " : "");
			vir_print_string_append(&p, a->inputs[k]);
		}
		vir_print_append(&p, ") clobber(");
		for (uint32_t k = 0; k < a->clobber_count; k++) {
			vir_print_append(&p, k ? ", " : "");
			vir_print_string_append(&p, a->clobbers[k]);
		}
		vir_print_append(&p, ")");
		if (a->labels)
			vir_print_append_format(&p, " labels %u", a->labels);
		vir_print_append(&p, "\n");
	}
	for (uint32_t i = 0; i < u->function_count; i++) {
		vir_function f;

		vir_function_load(&f, u, i);
		vir_print_append(&p, "\n");
		vir_print_function(b, u, &f);
	}
}
