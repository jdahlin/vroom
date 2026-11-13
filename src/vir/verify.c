/*
 * The verifier (SPEC-vir.md 12): every rule S1-S5, T1-T6, C1-C7, D1, A1-A4,
 * F1-F2, M1, N1, each failure reported with its rule id, the function, the
 * block and the instruction as `@f bN.I: RULE: message` (I: the index in
 * the block).
 *
 * Phase 1 needs no graph (structure, types, flags, signatures); it stops
 * there when the blocks do not tile the function or a reference is out of
 * range, since the graph rules read them.  Phase 2 builds the graph and the
 * dominators and checks C, D and N.
 */
#include "vir-internal.h"

typedef struct vir_verify_state {
	vir_function *function;
	const vir_unit *unit;
	vbuffer *output;
	uint32_t error_maximum, error_count;
	uint32_t *value_blocks;		/* value -> its block (VIR_BLOCK_NONE: a constant) */
	bool is_stopped;		/* phase 1 found what makes the graph unusable */
} vir_verify_state;

static void vir_verify_fail(vir_verify_state *v, const char *rule, uint32_t blk, uint32_t ins, const char *fmt, ...)
	__attribute__((format(printf, 5, 6)));
static void vir_verify_fail(vir_verify_state *v, const char *rule, uint32_t blk, uint32_t ins, const char *fmt, ...)
{
	va_list ap;
	char t[300];

	if (v->error_count++ >= v->error_maximum)
		return;
	vbuffer_format_append(v->output, "@%s", vir_function_name(v->unit, v->function));
	if (blk != VIR_BLOCK_NONE && ins != VIR_BLOCK_NONE)
		vbuffer_format_append(v->output, " b%u.%u", blk, ins);
	else if (blk != VIR_BLOCK_NONE)
		vbuffer_format_append(v->output, " b%u", blk);
	vbuffer_format_append(v->output, ": %s: ", rule);
	va_start(ap, fmt);
	vsnprintf(t, sizeof(t), fmt, ap);
	va_end(ap);
	vbuffer_string_append(v->output, t);
	vbuffer_string_append(v->output, "\n");
}

const char *vir_function_name(const vir_unit *u, const vir_function *f)
{
	return u && f->symbol < u->symbol_count ? u->symbols[f->symbol].name : "?";
}

static const char *vir_verify_type_name(uint32_t ty) { return ty < VIR_TYPE_COUNT ? vir_type_names[ty] : "?"; }
static const char *vir_verify_opcode_name(const vir_insn *in) { return vir_opcode_table[in->op].name ? vir_opcode_table[in->op].name : "?"; }

static bool vir_verify_type_is_value(uint32_t ty) { return (ty >= VIR_TYPE_I32 && ty <= VIR_TYPE_PTR) || vir_type_is_vector(ty); }

/* does a value of type ty satisfy operand kind k of an instruction whose result is rty? */
static bool vir_verify_operand_type_is_ok(uint32_t k, uint32_t ty, uint32_t rty)
{
	switch (k) {
	case VIR_OPERAND_KIND_T: return ty == rty;
	case VIR_OPERAND_KIND_I: return ty == VIR_TYPE_I32 || ty == VIR_TYPE_I64;
	case VIR_OPERAND_KIND_I32: return ty == VIR_TYPE_I32;
	case VIR_OPERAND_KIND_I64: case VIR_OPERAND_KIND_CONSTV: return ty == VIR_TYPE_I64;
	case VIR_OPERAND_KIND_PTR: return ty == VIR_TYPE_PTR;
	case VIR_OPERAND_KIND_F32: return ty == VIR_TYPE_F32;
	case VIR_OPERAND_KIND_F64: return ty == VIR_TYPE_F64;
	case VIR_OPERAND_KIND_F: return ty == VIR_TYPE_F32 || ty == VIR_TYPE_F64;
	case VIR_OPERAND_KIND_ANY: return vir_verify_type_is_value(ty);
	case VIR_OPERAND_KIND_X: return ty == VIR_TYPE_I32 || ty == VIR_TYPE_I64 || ty == VIR_TYPE_PTR;
	case VIR_OPERAND_KIND_FP: return ty == VIR_TYPE_F32 || ty == VIR_TYPE_F64;
	case VIR_OPERAND_KIND_VI: return vir_type_is_vector(rty) && ty == vir_type_vector_integer(rty);
	case VIR_OPERAND_KIND_VF: return vir_type_is_vector(rty) && ty == vir_type_vector_float(rty);
	case VIR_OPERAND_KIND_VLANES: return vir_type_is_vector(ty) && vir_type_element(ty) == rty;
	case VIR_OPERAND_KIND_VRED: return vir_type_is_vector(ty) && vir_type_element(ty) == rty && ty == vir_type_vector_integer(ty);
	case VIR_OPERAND_KIND_ELEM: return vir_type_is_vector(rty) && ty == vir_type_element(rty);
	case VIR_OPERAND_KIND_MULTI: return ty == VIR_TYPE_MULTI;
	case VIR_OPERAND_KIND_BITC:
		return (rty == VIR_TYPE_I32 && ty == VIR_TYPE_F32) || (rty == VIR_TYPE_F32 && ty == VIR_TYPE_I32) ||
		       (rty == VIR_TYPE_I64 && ty == VIR_TYPE_F64) || (rty == VIR_TYPE_F64 && ty == VIR_TYPE_I64);
	}
	return false;
}

static bool vir_verify_operand_kind_is_value(uint32_t k) { return (k >= VIR_OPERAND_KIND_T && k <= VIR_OPERAND_KIND_BITC) || k == VIR_OPERAND_KIND_CONSTV; }

/* ---------------- signatures ---------------- */

/* are the words of a signature at off well formed? (reports nothing) */
static const char *vir_verify_signature_check(const vir_function *f, uint32_t off)
{
	uint32_t ng;

	if (off == 0 || off >= f->extra_count)
		return "the signature offset is out of range";
	ng = VIR_SIGNATURE_GROUP_COUNT(f->extra[off]);
	if ((uint64_t)off + 3 + 2 * (uint64_t)ng > f->extra_count)
		return "the signature is cut off";
	for (uint32_t g = 0; g <= ng; g++) {
		uint32_t w0 = f->extra[off + 1 + 2 * g], w1 = f->extra[off + 2 + 2 * g], k = VIR_GROUP_KIND(w0), n = VIR_GROUP_PIECE_COUNT(w0);

		if (k == VIR_GROUP_REG) {
			if (n < 1 || n > 4)
				return "a register group has 1 to 4 pieces";
			for (uint32_t i = 0; i < 4; i++)
				if ((i < n) != (VIR_GROUP_CLASS(w0, i) != VIR_CLASS_NONE) || VIR_GROUP_CLASS(w0, i) > VIR_CLASS_F64)
					return "the piece classes do not match the piece count";
		} else if (k == VIR_GROUP_MEM) {
			if (n || VIR_GROUP_SIZE(w1) == 0)
				return "a memory group has a size and no pieces";
		} else if (k != VIR_GROUP_NONE && k != VIR_GROUP_SRET) {
			return "unknown group kind";
		}
		if (k == VIR_GROUP_SRET && g > 0)
			return "sret is only the result group";
		if (g == 0 && k == VIR_GROUP_MEM)
			return "a result is not a memory group";
	}
	return NULL;
}

/* the value types a group's words must have, in order: appends to ty[]; returns the count */
static uint32_t vir_verify_group_types(uint32_t w0, uint8_t *ty, bool sret_first)
{
	uint32_t n = 0;

	switch (VIR_GROUP_KIND(w0)) {
	case VIR_GROUP_REG:
		for (uint32_t i = 0; i < VIR_GROUP_PIECE_COUNT(w0); i++)
			ty[n++] = (uint8_t)(VIR_GROUP_CLASS(w0, i) == VIR_CLASS_F32 ? VIR_TYPE_F32 : VIR_GROUP_CLASS(w0, i) == VIR_CLASS_F64 ? VIR_TYPE_F64 : 0xfe);
		break;
	case VIR_GROUP_MEM:
		ty[n++] = VIR_TYPE_PTR;
		break;
	case VIR_GROUP_SRET:
		if (sret_first)
			ty[n++] = VIR_TYPE_PTR;
		break;
	}
	return n;
}

/* 0xfe: a full register, i64 or ptr */
static bool vir_verify_word_type_is_ok(uint32_t want, uint32_t have)
{
	return want == 0xfe ? have == VIR_TYPE_I64 || have == VIR_TYPE_PTR : want == have;
}

/* ---------------- exposure (6.4) ---------------- */

/* Backward exposure edges: a derived value -> each pointer it depends on.
 * A single traversal classifies every slot, including cycles and backedges. */
typedef struct vir_verify_exposure_edge { uint32_t source, next; } vir_verify_exposure_edge;
typedef struct vir_verify_exposure_graph { void *memory_values; uint32_t *head, *queue; vir_verify_exposure_edge *edge; uint8_t *slots; } vir_verify_exposure_graph;
enum { VIR_VERIFY_EXPOSURE_SAFE, VIR_VERIFY_EXPOSURE_SINK, VIR_VERIFY_EXPOSURE_FLOW };

/* Classify an operand use exactly as the exposure contract in 6.4. For a
 * propagation, out is the derived value; for a branch it is its parameter. */
static int vir_verify_exposure_use(vir_function *f, const vir_insn *x, uint32_t ins, uint32_t pos, uint32_t *out)
{
	switch (x->op) {
	case VIR_OP_LOAD: case VIR_OP_ALOAD: case VIR_OP_ASTORE: case VIR_OP_AXCHG: case VIR_OP_AXADD: case VIR_OP_ACAS:
	case VIR_OP_VA_START: case VIR_OP_VA_ARG: case VIR_OP_MEMSET:
		return pos == 0 ? VIR_VERIFY_EXPOSURE_SAFE : VIR_VERIFY_EXPOSURE_SINK;
	case VIR_OP_STORE:
		return pos == 1 ? VIR_VERIFY_EXPOSURE_SINK : VIR_VERIFY_EXPOSURE_SAFE;
	case VIR_OP_MEMCPY: case VIR_OP_PSUB: case VIR_OP_ICMP:
		return pos > 1 ? VIR_VERIFY_EXPOSURE_SINK : VIR_VERIFY_EXPOSURE_SAFE;
	case VIR_OP_PTRADD:
		if (pos != 0) return VIR_VERIFY_EXPOSURE_SAFE;
		*out = ins;
		return VIR_VERIFY_EXPOSURE_FLOW;
	case VIR_OP_SELECT:
		if (pos == 0) return VIR_VERIFY_EXPOSURE_SINK;
		*out = ins;
		return VIR_VERIFY_EXPOSURE_FLOW;
	case VIR_OP_BR:
		if (x->a >= f->block_count || pos >= f->blocks[x->a].parameter_count) return VIR_VERIFY_EXPOSURE_SAFE;
		*out = f->blocks[x->a].first + pos;
		return *out < f->insn_count ? VIR_VERIFY_EXPOSURE_FLOW : VIR_VERIFY_EXPOSURE_SAFE;
	default:
		return VIR_VERIFY_EXPOSURE_SINK;
	}
}

static void vir_verify_exposure_enqueue(uint8_t *mark, uint32_t *queue, uint32_t *tail, uint32_t v)
{
	if (!mark[v]) {
		mark[v] = 1;
		queue[(*tail)++] = v;
	}
}

/* Build the propagation edges in two passes so one checked allocation holds
 * all scratch. Allocation or representation failure rejects verification;
 * it can never turn an unknown exposure into an accepted claim. */
static bool vir_verify_exposure_build(vir_function *f, uint8_t *mark, vir_verify_exposure_graph *g)
{
	size_t nedges = 0, heads, edges, total;
	uint32_t at = 0, first = 0, last = 0;

	memset(g, 0, sizeof(*g));
	for (uint32_t i = f->constant_count; i < f->insn_count; i++) {
		vir_insn x = f->insns[i];
		vir_operand_iterator it;
		uint32_t *p, out;

		if (vir_opcode_is_constant(x.op)) continue;
		vir_operand_iterator_init(&it, f, &x, false);
		while ((p = vir_operand_iterator_next(&it))) {
			uint32_t pos = x.op == VIR_OP_BR ? (uint32_t)(p - &f->extra[x.c]) : it.pos;
			if (*p < f->insn_count && vir_verify_exposure_use(f, &x, i, pos, &out) == VIR_VERIFY_EXPOSURE_FLOW && ++nedges >= UINT32_MAX)
				return false;
		}
	}
	if (__builtin_mul_overflow((size_t)f->insn_count, sizeof(*g->head), &heads) ||
	    __builtin_mul_overflow(nedges, sizeof(*g->edge), &edges) ||
	    __builtin_add_overflow(heads, heads, &total) ||
	    __builtin_add_overflow(total, edges, &total) ||
	    __builtin_add_overflow(total, (size_t)f->slot_count, &total)) return false;
	g->memory_values = malloc(total);
	if (!g->memory_values) return false;
	g->head = g->memory_values;
	g->queue = g->head + f->insn_count;
	g->edge = (vir_verify_exposure_edge *)(g->queue + f->insn_count);
	g->slots = (uint8_t *)(g->edge + nedges);
	memset(g->head, 0xff, heads);
	memset(g->slots, 0, f->slot_count);
	memset(mark, 0, f->insn_count);
	for (uint32_t i = f->constant_count; i < f->insn_count; i++) {
		vir_insn x = f->insns[i];
		vir_operand_iterator it;
		uint32_t *p, out;

		if (vir_opcode_is_constant(x.op)) continue;
		vir_operand_iterator_init(&it, f, &x, false);
		while ((p = vir_operand_iterator_next(&it))) {
			uint32_t v = *p, pos = x.op == VIR_OP_BR ? (uint32_t)(p - &f->extra[x.c]) : it.pos;
			int kind;

			if (v >= f->insn_count) continue;
			kind = vir_verify_exposure_use(f, &x, i, pos, &out);
			if (kind == VIR_VERIFY_EXPOSURE_SINK) vir_verify_exposure_enqueue(mark, g->queue, &last, v);
			else if (kind == VIR_VERIFY_EXPOSURE_FLOW) {
				g->edge[at] = (vir_verify_exposure_edge){ v, g->head[out] };
				g->head[out] = at++;
			}
		}
	}
	while (first < last) {
		uint32_t v = g->queue[first++];

		for (uint32_t i = g->head[v]; i != UINT32_MAX; i = g->edge[i].next)
			vir_verify_exposure_enqueue(mark, g->queue, &last, g->edge[i].source);
	}
	for (uint32_t i = 1; i < f->constant_count; i++)
		if (mark[i] && f->insns[i].op == VIR_OP_SLOTADDR && f->insns[i].a < f->slot_count)
			g->slots[f->insns[i].a] = 1;
	return true;
}

/* ---------------- phase 1: per instruction ---------------- */

/* the words of the list of in that hold values, with a bounds check: reports T2 once */
static bool vir_verify_lists_in_range_check(vir_verify_state *v, uint32_t b, uint32_t i, const vir_insn *in)
{
	vir_function *f = v->function;
	uint32_t op = in->op, ne = f->extra_count;
	bool vir_alloc_check_failure_add = false;

	switch (op) {
	case VIR_OP_CALL: case VIR_OP_RET: vir_alloc_check_failure_add = in->c == 0 || in->c >= ne || (uint64_t)in->c + 1 + f->extra[in->c] > ne; break;
	case VIR_OP_BR: {
		uint32_t np = in->a < f->block_count ? f->blocks[in->a].parameter_count : 0;

		vir_alloc_check_failure_add = np && (in->c == 0 || (uint64_t)in->c + np > ne);
		break;
	}
	case VIR_OP_SWITCH: {
		uint32_t ty = in->a < f->insn_count ? f->insns[in->a].type : 0, st = ty == VIR_TYPE_I64 ? 5 : 3;

		vir_alloc_check_failure_add = in->c == 0 || in->c >= ne || (uint64_t)in->c + 1 + (uint64_t)f->extra[in->c] * st > ne;
		break;
	}
	case VIR_OP_IBR: vir_alloc_check_failure_add = in->c == 0 || in->c >= ne || (uint64_t)in->c + 1 + f->extra[in->c] > ne; break;
	case VIR_OP_ASM: case VIR_OP_ASMGOTO:
		vir_alloc_check_failure_add = in->b == 0 || (uint64_t)in->b + 2 > ne ||
		      (uint64_t)in->b + 2 + f->extra[in->b] + 2 * (uint64_t)f->extra[in->b + 1] > ne;
		if (!vir_alloc_check_failure_add && op == VIR_OP_ASMGOTO)
			vir_alloc_check_failure_add = in->c == 0 || in->c >= ne || (uint64_t)in->c + 2 + f->extra[in->c] > ne;
		break;
	case VIR_OP_VA_ARG: vir_alloc_check_failure_add = in->b == 0 || (uint64_t)in->b + 2 > ne; break;
	}
	if (vir_alloc_check_failure_add) {
		vir_verify_fail(v, "T2", b, i, "%s: an operand list is out of range", vir_verify_opcode_name(in));
		v->is_stopped = true;
	}
	return !vir_alloc_check_failure_add;
}

static void vir_verify_insn_check(vir_verify_state *v, uint32_t b, uint32_t i, uint32_t idx)
{
	vir_function *f = v->function;
	vir_insn *in = &f->insns[idx];
	const vir_opcode_info *oi = &vir_opcode_table[in->op];
	uint32_t op = in->op, opty[3] = { 0, 0, 0 };

	/* T1 */
	if (op >= VIR_OP_TARGET_FIRST) {
		vir_verify_fail(v, "T1", b, i, "opcode %u is reserved for target instructions", op);
		return;
	}
	if (!oi->name) {
		vir_verify_fail(v, "T1", b, i, "%u is not an opcode", op);
		return;
	}
	if (in->type >= VIR_TYPE_COUNT || !(oi->type_mask >> in->type & 1)) {
		vir_verify_fail(v, "T1", b, i, "%s cannot have result type %s", oi->name, vir_verify_type_name(in->type));
		return;
	}
	if (!vir_verify_lists_in_range_check(v, b, i, in))
		return;
	/* T2: value operands exist and have the type the table requires */
	for (uint32_t k = 0; k < 3; k++) {
		uint32_t kd = oi->operand_kinds[k], val = k == 0 ? in->a : k == 1 ? in->b : in->c;

		if (kd == VIR_OPERAND_KIND_NONE) {
			if (val) {
				vir_verify_fail(v, "T2", b, i, "%s: operand %c must be 0", oi->name, 'a' + k);
				return;
			}
			continue;
		}
		if (!vir_verify_operand_kind_is_value(kd))
			continue;
		if (val == 0 || val >= f->insn_count || (!vir_verify_type_is_value(f->insns[val].type) && f->insns[val].type != VIR_TYPE_MULTI)) {
			vir_verify_fail(v, "T2", b, i, "%s: operand %c is not a value", oi->name, 'a' + k);
			return;
		}
		opty[k] = f->insns[val].type;
		if (!vir_verify_operand_type_is_ok(kd, opty[k], in->type)) {
			vir_verify_fail(v, "T2", b, i, "%s: operand %c has type %s", oi->name, 'a' + k, vir_verify_type_name(opty[k]));
			return;
		}
	}
	if ((op == VIR_OP_ICMP || op == VIR_OP_FCMP) && opty[0] != opty[1]) {
		vir_verify_fail(v, "T2", b, i, "%s: the operands have different types (%s, %s)", oi->name, vir_verify_type_name(opty[0]),
		      vir_verify_type_name(opty[1]));
		return;
	}
	/* the list operands exist and are values (their types: A1, A2, C3, A4) */
	{
		vir_insn x = *in;
		vir_operand_iterator it;
		uint32_t *p;

		vir_operand_iterator_init(&it, f, &x, false);
		while ((p = vir_operand_iterator_next(&it)))
			if (it.pos == 3 && (*p == 0 || *p >= f->insn_count || !vir_verify_type_is_value(f->insns[*p].type))) {
				vir_verify_fail(v, "T2", b, i, "%s: an operand in a list is not a value", oi->name);
				return;
			}
	}
	if ((op == VIR_OP_VINSERT && in->c >= (in->type == VIR_TYPE_I32X4 || in->type == VIR_TYPE_F32X4 ? 4u : 2u)) ||
	    (op == VIR_OP_VEXTRACT && in->b >= (in->type == VIR_TYPE_I32 || in->type == VIR_TYPE_F32 ? 4u : 2u))) {
		vir_verify_fail(v, "T2", b, i, "%s: lane index out of range", oi->name);
		return;
	}
	/* T3: flags */
	if (in->flags & ~oi->flags_mask) {
		vir_verify_fail(v, "T3", b, i, "%s: flag bits 0x%x are not allowed", oi->name, in->flags & ~oi->flags_mask);
		return;
	}
	if (op == VIR_OP_VICMP && (in->flags & 15) > 9) {
		vir_verify_fail(v, "T3", b, i, "vicmp: condition code %u is not allowed", in->flags & 15);
		return;
	}
	if (op == VIR_OP_VFCMP && (in->flags & 15) > 7) {
		vir_verify_fail(v, "T3", b, i, "vfcmp: condition code %u is not allowed", in->flags & 15);
		return;
	}
	if (op == VIR_OP_ICMP && ((in->flags & 15) > 9 || (opty[0] == VIR_TYPE_PTR && (in->flags & 15) >= 2 && (in->flags & 15) <= 5))) {
		vir_verify_fail(v, "T3", b, i, "icmp: condition code %u is not allowed%s", in->flags & 15,
		      opty[0] == VIR_TYPE_PTR ? " on ptr" : "");
		return;
	}
	if (op == VIR_OP_FCMP && (in->flags & 15) > 7) {
		vir_verify_fail(v, "T3", b, i, "fcmp: condition code %u is not allowed", in->flags & 15);
		return;
	}
	/* T4: widths */
	{
		uint32_t w = 1u << VIR_INSN_FLAG_WIDTH_LOG2(in->flags), vt = 0;
		bool vir_alloc_checker = false;

		if (op == VIR_OP_LOAD || op == VIR_OP_ALOAD || op == VIR_OP_AXCHG || op == VIR_OP_AXADD || op == VIR_OP_ACAS)
			vt = in->type, vir_alloc_checker = true;
		else if (op == VIR_OP_STORE || op == VIR_OP_ASTORE)
			vt = opty[1], vir_alloc_checker = true;
		else if (op == VIR_OP_EXT)
			vt = opty[0], vir_alloc_checker = true;
		if (vir_alloc_checker) {
			uint32_t sz = vir_type_size(vir_type_element(vt));	/* (a vector's width is its lanes') */
			bool fp = vt == VIR_TYPE_F32 || vt == VIR_TYPE_F64 || vt == VIR_TYPE_PTR || vir_type_is_vector(vt);

			if ((fp && w != sz) || (!fp && w > sz)) {
				vir_verify_fail(v, "T4", b, i, "%s: width %u does not fit %s", oi->name, w, vir_verify_type_name(vt));
				return;
			}
			if (op == VIR_OP_LOAD && (in->flags & VIR_INSN_FLAG_MS) && (vt == VIR_TYPE_F32 || vt == VIR_TYPE_F64 || vt == VIR_TYPE_PTR || vir_type_is_vector(vt))) {
				vir_verify_fail(v, "T4", b, i, "load: sign extension of %s", vir_verify_type_name(vt));
				return;
			}
		}
	}
	/* T5 */
	if (op == VIR_OP_MEMCPY || op == VIR_OP_MEMSET) {
		if (f->insns[in->c].op != VIR_OP_ICONST) {
			vir_verify_fail(v, "T5", b, i, "%s: the size is not a constant", oi->name);
			return;
		}
	}
	/* T6 */
	if (op == VIR_OP_PROJ) {
		const vir_insn *pr = &f->insns[in->a];
		uint32_t n = 0;
		uint8_t pt[8];

		if (pr->op == VIR_OP_CALL) {
			uint32_t off = pr->b;
			const char *e = vir_verify_signature_check(f, off);

			if (e) {
				vir_verify_fail(v, "T6", b, i, "proj: the producer's signature is bad");
				return;
			}
			n = vir_verify_group_types(f->extra[off + 1], pt, false);
			if (n > 4)
				n = 4;
			if (in->b >= n) {
				vir_verify_fail(v, "T6", b, i, "proj: index %u, the result has %u pieces", in->b, n);
				return;
			}
			if (!vir_verify_word_type_is_ok(pt[in->b], in->type)) {
				vir_verify_fail(v, "T6", b, i, "proj: the piece is %s, not %s",
				      pt[in->b] == 0xfe ? "i64 or ptr" : vir_verify_type_name(pt[in->b]), vir_verify_type_name(in->type));
				return;
			}
		} else if ((pr->op == VIR_OP_ASM || pr->op == VIR_OP_ASMGOTO) && pr->b < f->extra_count) {
			uint32_t no = f->extra[pr->b];

			if (in->b >= no) {
				vir_verify_fail(v, "T6", b, i, "proj: index %u, the result has %u pieces", in->b, no);
				return;
			}
		} else {
			vir_verify_fail(v, "T6", b, i, "proj: the producer is %s, not a call or asm", vir_verify_opcode_name(pr));
			return;
		}
	}
}

/* ---------------- phase 2 ---------------- */

static bool vir_verify_asm_constraint_is_memory(const char *c)
{
	for (; *c; c++)
		if (*c == 'm' || *c == 'o' || *c == 'V' || *c == 'Q')
			return true;
	return false;
}

static void vir_verify_asm_check(vir_verify_state *v, uint32_t b, uint32_t i, const vir_insn *in)
{
	vir_function *f = v->function;
	const vir_unit_asm *a;
	uint32_t no = f->extra[in->b], ni = f->extra[in->b + 1], memout = 0, regout = 0, nops;

	if (!v->unit)
		return;
	if (in->a >= v->unit->asm_count) {
		vir_verify_fail(v, "A4", b, i, "asm template !%u does not exist", in->a);
		return;
	}
	a = &v->unit->asms[in->a];
	nops = a->output_count + a->input_count;
	for (uint32_t k = 0; k < a->output_count; k++)
		vir_verify_asm_constraint_is_memory(a->outputs[k]) ? memout++ : regout++;
	if (no != regout || ni != a->input_count + memout) {
		vir_verify_fail(v, "A4", b, i, "asm !%u has %u register outputs and %u inputs, the instruction %u and %u", in->a, regout,
		      a->input_count + memout, no, ni);
		return;
	}
	if (no == 0 ? in->type != VIR_TYPE_VOID : no == 1 ? !vir_verify_type_is_value(in->type) : in->type != VIR_TYPE_MULTI) {
		vir_verify_fail(v, "A4", b, i, "asm with %u outputs has result type %s", no, vir_verify_type_name(in->type));
		return;
	}
	for (uint32_t k = 0; k < ni; k++) {
		uint32_t d = f->extra[in->b + 2 + no + 2 * k], val = f->extra[in->b + 3 + no + 2 * k];
		const char *c = d < a->output_count ? a->outputs[d] : d < nops ? a->inputs[d - a->output_count] : NULL;

		if (!c) {
			vir_verify_fail(v, "A4", b, i, "operand %u does not exist in the template", d);
			return;
		}
		if (vir_verify_asm_constraint_is_memory(c) && val < f->insn_count && f->insns[val].type != VIR_TYPE_PTR) {
			vir_verify_fail(v, "A4", b, i, "memory operand %u has a value of type %s, not ptr", d, vir_verify_type_name(f->insns[val].type));
			return;
		}
	}
	if (in->op == VIR_OP_ASMGOTO) {
		if (f->extra[in->c] != a->labels)
			vir_verify_fail(v, "A4", b, i, "asmgoto lists %u labels, the template has %u", f->extra[in->c], a->labels);
	} else if (a->labels) {
		vir_verify_fail(v, "A4", b, i, "asm !%u has labels: it must be asmgoto", in->a);
	}
}

/* does a call result of type ty fit the result group w0? */
static bool vir_verify_result_type_is_ok(uint32_t w0, uint32_t ty)
{
	uint32_t k = VIR_GROUP_KIND(w0), np = VIR_GROUP_PIECE_COUNT(w0);

	if (k == VIR_GROUP_REG && np == 1)
		return VIR_GROUP_CLASS(w0, 0) == VIR_CLASS_INT ? ty == VIR_TYPE_I64 || ty == VIR_TYPE_PTR : VIR_GROUP_CLASS(w0, 0) == VIR_CLASS_F32 ? ty == VIR_TYPE_F32 : ty == VIR_TYPE_F64;
	if (k == VIR_GROUP_REG)
		return ty == VIR_TYPE_MULTI;
	return ty == VIR_TYPE_VOID;
}

static void vir_verify_call_check(vir_verify_state *v, uint32_t b, uint32_t i, const vir_insn *in)
{
	vir_function *f = v->function;
	const char *e = vir_verify_signature_check(f, in->b);
	uint32_t off = in->b, ng, n, expect = 0;
	uint8_t want[1024];

	if (e) {
		vir_verify_fail(v, "A1", b, i, "call: %s", e);
		return;
	}
	ng = VIR_SIGNATURE_GROUP_COUNT(f->extra[off]);
	n = f->extra[in->c];
	if (!vir_verify_result_type_is_ok(f->extra[off + 1], in->type)) {
		vir_verify_fail(v, "A1", b, i, "call: the result type %s does not fit the result group", vir_verify_type_name(in->type));
		return;
	}
	if (VIR_GROUP_KIND(f->extra[off + 1]) == VIR_GROUP_SRET)
		want[expect++] = VIR_TYPE_PTR;
	for (uint32_t g = 0; g < ng; g++) {
		uint8_t pt[8];
		uint32_t m = vir_verify_group_types(f->extra[off + 3 + 2 * g], pt, false);

		if (expect + m > sizeof(want)) {
			vir_verify_fail(v, "A1", b, i, "call: too many argument words for the checker");
			return;
		}
		for (uint32_t k = 0; k < m; k++)
			want[expect++] = pt[k];
	}
	if (n != expect) {
		vir_verify_fail(v, "A1", b, i, "call: the signature takes %u words, the call passes %u", expect, n);
		return;
	}
	for (uint32_t k = 0; k < n; k++) {
		uint32_t val = f->extra[in->c + 1 + k];

		if (!vir_verify_word_type_is_ok(want[k], f->insns[val].type)) {
			vir_verify_fail(v, "A1", b, i, "call: argument %u has type %s, the signature wants %s", k, vir_verify_type_name(f->insns[val].type),
			      want[k] == 0xfe ? "i64 or ptr" : vir_verify_type_name(want[k]));
			return;
		}
	}
}

static void vir_verify_return_check(vir_verify_state *v, uint32_t b, uint32_t i, const vir_insn *in)
{
	vir_function *f = v->function;
	uint32_t n = f->extra[in->c], m;
	uint8_t pt[8];

	if (vir_verify_signature_check(f, f->signature)) {
		vir_verify_fail(v, "A2", b, i, "ret: the function's signature is bad");
		return;
	}
	m = vir_verify_group_types(f->extra[f->signature + 1], pt, false);
	if (n != m) {
		vir_verify_fail(v, "A2", b, i, "ret: the result group has %u pieces, ret returns %u values", m, n);
		return;
	}
	for (uint32_t k = 0; k < n; k++) {
		uint32_t val = f->extra[in->c + 1 + k];

		if (!vir_verify_word_type_is_ok(pt[k], f->insns[val].type)) {
			vir_verify_fail(v, "A2", b, i, "ret: value %u has type %s, the result wants %s", k, vir_verify_type_name(f->insns[val].type),
			      pt[k] == 0xfe ? "i64 or ptr" : vir_verify_type_name(pt[k]));
			return;
		}
	}
}

static void vir_verify_switch_check(vir_verify_state *v, uint32_t b, uint32_t i, const vir_insn *in)
{
	vir_function *f = v->function;
	uint32_t ty = f->insns[in->a].type, st = ty == VIR_TYPE_I64 ? 5 : 3, n = f->extra[in->c];
	uint64_t prev_hi = 0;

	for (uint32_t k = 0; k < n; k++) {
		const uint32_t *w = &f->extra[in->c + 1 + (size_t)k * st];
		uint64_t lo = ty == VIR_TYPE_I64 ? (uint64_t)w[0] | (uint64_t)w[1] << 32 : w[0];
		uint64_t hi = ty == VIR_TYPE_I64 ? (uint64_t)w[2] | (uint64_t)w[3] << 32 : w[1];

		if (lo > hi) {
			vir_verify_fail(v, "C5", b, i, "switch: case %u has lo above hi", k);
			return;
		}
		if (k && lo <= prev_hi) {
			vir_verify_fail(v, "C5", b, i, "switch: case %u overlaps or is out of order", k);
			return;
		}
		prev_hi = hi;
	}
}

/* the dominance and use rules for one instruction */
static void vir_verify_uses_check(vir_verify_state *v, uint32_t b, uint32_t i, uint32_t idx)
{
	vir_function *f = v->function;
	vir_insn x = f->insns[idx];
	vir_operand_iterator it;
	uint32_t *p;

	if (f->blocks[b].immediate_dominator == VIR_BLOCK_NONE)
		return;
	vir_operand_iterator_init(&it, f, &x, false);
	while ((p = vir_operand_iterator_next(&it))) {
		uint32_t d = *p, db;

		if (d == 0 || d >= f->insn_count || vir_opcode_is_constant(f->insns[d].op))
			continue;
		db = v->value_blocks[d];
		if (db == VIR_BLOCK_NONE || f->blocks[db].immediate_dominator == VIR_BLOCK_NONE)
			continue;
		if (f->insns[d].op == VIR_OP_ASMGOTO) {	/* only blocks the terminator's block strictly dominates */
			if (db == b || !vir_block_dominates(f, db, b))
				vir_verify_fail(v, "D1", b, i, "%s uses the result of an asmgoto in a block that b%u does not strictly dominate",
				      vir_verify_opcode_name(&x), db);
		} else if (db == b) {
			if (d >= idx)
				vir_verify_fail(v, "D1", b, i, "%s uses a value defined later in the block", vir_verify_opcode_name(&x));
		} else if (!vir_block_dominates(f, db, b)) {
			vir_verify_fail(v, "D1", b, i, "%s uses a value of b%u, which does not dominate b%u", vir_verify_opcode_name(&x), db, b);
		} else {
			continue;
		}
		return;
	}
}

static void vir_verify_graph_check(vir_verify_state *v)
{
	vir_function *f = v->function;
	vir_context *c = vir_context_get();
	uint32_t *mark = c->bs[0];	/* a stamp per block (the successors are in sc, so bs[0] is free) */

	/* C1 */
	if (f->blocks[0].parameter_count)
		vir_verify_fail(v, "C1", 0, VIR_BLOCK_NONE, "block 0 has parameters");
	if (f->blocks[0].predecessor_count)
		vir_verify_fail(v, "C1", 0, VIR_BLOCK_NONE, "block 0 has predecessors");
	for (uint32_t b = 1; b < f->block_count; b++)
		if (f->blocks[b].immediate_dominator == VIR_BLOCK_NONE)
			vir_verify_fail(v, "C1", b, VIR_BLOCK_NONE, "unreachable from block 0");
	/* C2 */
	for (uint32_t b = 1; b < f->block_count; b++)
		if (f->blocks[b].immediate_dominator != VIR_BLOCK_NONE && f->blocks[b].immediate_dominator > b)
			vir_verify_fail(v, "C2", b, VIR_BLOCK_NONE, "comes before its immediate dominator b%u", f->blocks[b].immediate_dominator);
	for (uint32_t b = 0; b < f->block_count; b++) {
		const vir_block *k = &f->blocks[b];

		for (uint32_t i = 0; i < k->insn_count; i++) {
			uint32_t idx = k->first + i;
			vir_insn x = f->insns[idx];
			vir_operand_iterator it;
			uint32_t *p;

			if (!vir_opcode_table[x.op].name || x.op >= VIR_OP_TARGET_FIRST)
				continue;
			/* C3 */
			if (x.op == VIR_OP_BR && x.a < f->block_count) {
				const vir_block *t = &f->blocks[x.a];

				for (uint32_t j = 0; j < t->parameter_count; j++) {
					uint32_t val = f->extra[x.c + j], want = f->insns[t->first + j].type;

					if (f->insns[val].type != want) {
						vir_verify_fail(v, "C3", b, i, "br b%u: argument %u has type %s, the parameter %s", x.a, j,
						      vir_verify_type_name(f->insns[val].type), vir_verify_type_name(want));
						break;
					}
				}
			}
			/* C4, C5, C6, C7 */
			if (x.op == VIR_OP_BRIF || x.op == VIR_OP_SWITCH || x.op == VIR_OP_IBR || x.op == VIR_OP_ASMGOTO) {
				vir_operand_iterator_init(&it, f, &x, true);
				while ((p = vir_operand_iterator_next(&it)))
					if (*p < f->block_count && f->blocks[*p].parameter_count) {
						vir_verify_fail(v, "C4", b, i, "%s targets b%u, which has parameters", vir_verify_opcode_name(&x), *p);
						break;
					}
			}
			if (x.op == VIR_OP_SWITCH)
				vir_verify_switch_check(v, b, i, &x);
			if (x.op == VIR_OP_IBR) {
				uint32_t n = f->extra[x.c], vir_alloc_stamp_next = idx + 1;
				bool vir_alloc_check_failure_add = false;

				for (uint32_t j = 0; j < f->block_count; j++)
					mark[j] = 0;
				for (uint32_t j = 0; j < n && !vir_alloc_check_failure_add; j++) {
					uint32_t t = f->extra[x.c + 1 + j];

					mark[t] = vir_alloc_stamp_next;
					if (!(f->blocks[t].flags & VIR_BLOCK_FLAG_ADDR)) {
						vir_verify_fail(v, "C6", b, i, "ibr lists b%u, which has no VB_ADDR", t);
						vir_alloc_check_failure_add = true;
					}
				}
				for (uint32_t j = 0; j < f->block_count && !vir_alloc_check_failure_add; j++)
					if ((f->blocks[j].flags & VIR_BLOCK_FLAG_ADDR) && mark[j] != vir_alloc_stamp_next) {
						vir_verify_fail(v, "C6", b, i, "ibr does not list b%u, whose address is taken", j);
						vir_alloc_check_failure_add = true;
					}
			}
			if (x.op == VIR_OP_ASMGOTO) {
				vir_operand_iterator_init(&it, f, &x, true);
				while ((p = vir_operand_iterator_next(&it))) {
					const vir_block *t = &f->blocks[*p];
					bool vir_alloc_check_failure_add = false;

					for (uint32_t j = 0; j < t->predecessor_count; j++)
						if (f->extra[t->predecessor_first + j] != b)
							vir_alloc_check_failure_add = true;
					if (vir_alloc_check_failure_add || !t->predecessor_count) {
						vir_verify_fail(v, "C7", b, i, "asmgoto target b%u has another predecessor", *p);
						break;
					}
				}
			}
			/* A3, A4, D1 */
			if (x.op == VIR_OP_CALL && (x.flags & VIR_INSN_FLAG_CALL_NORETURN) &&
			    (i + 1 >= k->insn_count || f->insns[idx + 1].op != VIR_OP_TRAP))
				vir_verify_fail(v, "A3", b, i, "a noreturn call is not followed by trap");
			if (x.op == VIR_OP_CALL)
				vir_verify_call_check(v, b, i, &x);
			if (x.op == VIR_OP_RET)
				vir_verify_return_check(v, b, i, &x);
			if (x.op == VIR_OP_ASM || x.op == VIR_OP_ASMGOTO)
				vir_verify_asm_check(v, b, i, &x);
			vir_verify_uses_check(v, b, i, idx);
		}
	}
	/* C6 also: a labeladdr must name a VIR_BLOCK_FLAG_ADDR block (S2 reports that) */
}

static void vir_verify_misc_check(vir_verify_state *v)
{
	vir_function *f = v->function;
	vir_context *c = vir_context_get();
	uint8_t *mark = (uint8_t *)c->auxiliary;

	/* F1 */
	if (f->flags & VIR_FUNCTION_FLAG_NOFP)
		for (uint32_t b = 0; b < f->block_count; b++)
			for (uint32_t i = 0; i < f->blocks[b].insn_count; i++) {
				const vir_insn *x = &f->insns[f->blocks[b].first + i];

				if (x->type == VIR_TYPE_F32 || x->type == VIR_TYPE_F64) {
					vir_verify_fail(v, "F1", b, i, "%s has type %s in a nofp function", vir_verify_opcode_name(x), vir_verify_type_name(x->type));
					return;
				}
			}
	for (uint32_t i = 1; i < f->constant_count && (f->flags & VIR_FUNCTION_FLAG_NOFP); i++)
		if (f->insns[i].op == VIR_OP_FCONST) {
			vir_verify_fail(v, "F1", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "fconst in a nofp function");
			break;
		}
	/* F2 */
	if (!(f->flags & VIR_FUNCTION_FLAG_VARIADIC))
		for (uint32_t b = 0; b < f->block_count; b++)
			for (uint32_t i = 0; i < f->blocks[b].insn_count; i++)
				if (f->insns[f->blocks[b].first + i].op == VIR_OP_VA_START) {
					vir_verify_fail(v, "F2", b, i, "va_start in a function that is not variadic");
					return;
				}
	(void)mark;
}

static void vir_verify_slots_check(vir_verify_state *v)
{
	vir_function *f = v->function;
	uint8_t *mark = (uint8_t *)vir_context_get()->auxiliary;
	vir_verify_exposure_graph graph = {0};
	bool have_slotaddr = false;

	if ((f->flags & VIR_FUNCTION_FLAG_EXPOSURE) && f->slot_count)
		for (uint32_t i = 1; i < f->constant_count; i++)
			if (f->insns[i].op == VIR_OP_SLOTADDR) { have_slotaddr = true; break; }
	if (have_slotaddr && !vir_verify_exposure_build(f, mark, &graph)) {
		vir_verify_fail(v, "M1", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "cannot analyze slot exposure: too many uses or out of memory");
		return;
	}
	for (uint32_t s = 0; s < f->slot_count; s++) {
		const vir_slot *sl = &f->slot[s];

		if (!sl->align || (sl->align & (sl->align - 1))) {
			vir_verify_fail(v, "M1", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "slot $%u: the alignment %u is not a power of two", s, sl->align);
			continue;
		}
		if (sl->size >= ((uint64_t)1 << 40)) {
			vir_verify_fail(v, "M1", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "slot $%u: the size %llu is too large", s, (unsigned long long)sl->size);
			continue;
		}
		if (f->flags & VIR_FUNCTION_FLAG_EXPOSURE) {
			bool e = have_slotaddr && graph.slots[s], claim = (sl->flags & VIR_SLOT_FLAG_EXPOSED) != 0;

			if (e != claim)
				vir_verify_fail(v, "M1", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "slot $%u is %sexposed, the function says %sexposed", s,
				      e ? "" : "not ", claim ? "" : "not ");
		}
	}
	free(graph.memory_values);
}

/* N1: canonical form (2.4) */
static void vir_verify_canonical_check(vir_verify_state *v)
{
	vir_function *f = v->function;
	uint32_t nxt = 1;
	bool order_bad = false;

	for (uint32_t b = 0; b < f->block_count; b++)
		for (uint32_t i = 0; i < f->blocks[b].insn_count; i++) {
			uint32_t idx = f->blocks[b].first + i;
			vir_insn x = f->insns[idx];
			vir_operand_iterator it;
			uint32_t *p;

			if (x.op == VIR_OP_NOP) {
				vir_verify_fail(v, "N1", b, i, "a nop in the block region");
				return;
			}
			vir_operand_iterator_init(&it, f, &x, false);
			while ((p = vir_operand_iterator_next(&it))) {
				uint32_t d = *p;

				if (d == 0 || d >= f->constant_count)
					continue;
				if (d == nxt)
					nxt++;
				else if (d > nxt && !order_bad) {
					vir_verify_fail(v, "N1", b, i, "constant %u is used before constant %u: not in order of first use", d, nxt);
					order_bad = true;
				}
			}
		}
	if (!order_bad && nxt < f->constant_count)
		vir_verify_fail(v, "N1", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "%u constants are not used", f->constant_count - nxt);
}

uint32_t vir_verify(vir_function *f, const vir_unit *u, vbuffer *out, uint32_t max)
{
	vir_verify_state v = { f, u, out, max, 0, f->scratch, false };
	bool tiled = true;
	uint32_t expect;

	/* S1 */
	if (f->version != 1)
		vir_verify_fail(&v, "S1", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "the version is %u, not 1", f->version);
	if (!f->insn_count || f->insns[0].op != VIR_OP_NOP || f->insns[0].type || f->insns[0].a || f->insns[0].b || f->insns[0].c) {
		vir_verify_fail(&v, "S1", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "index 0 is not a nop");
		return v.error_count;
	}
	if (f->constant_count < 1 || f->constant_count > f->insn_count) {
		vir_verify_fail(&v, "S1", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "the constant region ends at %u (nins %u): not the layout of 2.3", f->constant_count,
		      f->insn_count);
		return v.error_count;
	}
	if (!f->block_count) {
		vir_verify_fail(&v, "S3", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "the function has no blocks");
		return v.error_count;
	}
	/* S2: the constant region */
	vir_constant_table_reset(vir_context_get());
	for (uint32_t i = 1; i < f->constant_count; i++) {
		const vir_insn *c = &f->insns[i];
		uint32_t slot = 0;
		const char *why = NULL;

		if (!vir_opcode_is_constant(c->op)) {
			vir_verify_fail(&v, "S2", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "index %u in the constant region is %s", i,
			      vir_opcode_table[c->op].name ? vir_opcode_table[c->op].name : "not an opcode");
			continue;
		}
		if (c->flags)
			why = "flags are set";
		switch (c->op) {
		case VIR_OP_ICONST:
			if (c->type != VIR_TYPE_I32 && c->type != VIR_TYPE_I64 && c->type != VIR_TYPE_PTR) why = "iconst has type i32, i64 or ptr";
			else if (c->type == VIR_TYPE_I32 && c->c) why = "an i32 iconst has c 0";
			else if (c->a) why = "iconst has a 0";
			break;
		case VIR_OP_FCONST:
			if (c->type != VIR_TYPE_F32 && c->type != VIR_TYPE_F64) why = "fconst has type f32 or f64";
			else if (c->type == VIR_TYPE_F32 && c->c) why = "an f32 fconst has c 0";
			else if (c->a) why = "fconst has a 0";
			break;
		case VIR_OP_SYMADDR:
			if (c->type != VIR_TYPE_PTR) why = "symaddr has type ptr";
			else if (u && c->a >= u->symbol_count) why = "the symbol does not exist";
			break;
		case VIR_OP_SLOTADDR:
			if (c->type != VIR_TYPE_PTR) why = "slotaddr has type ptr";
			else if (c->a >= f->slot_count) why = "the slot does not exist";
			break;
		case VIR_OP_LABELADDR:
			if (c->type != VIR_TYPE_PTR) why = "labeladdr has type ptr";
			else if (c->a >= f->block_count) why = "the block does not exist";
			else if (!(f->blocks[c->a].flags & VIR_BLOCK_FLAG_ADDR)) why = "the block has no VB_ADDR";
			else if (c->b || c->c) why = "labeladdr has b and c 0";
			break;
		}
		if (why) {
			vir_verify_fail(&v, "S2", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "constant %u (%s): %s", i, vir_opcode_table[c->op].name, why);
			continue;
		}
		if (vir_constant_find(vir_context_get(), f, c->op, c->type, c->a, c->b, c->c, &slot)) {
			vir_verify_fail(&v, "S2", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "constant %u (%s) is a duplicate", i, vir_opcode_table[c->op].name);
		} else {
			vir_context *x = vir_context_get();

			x->ctab[slot].gen = x->gen;
			x->ctab[slot].val = i;
		}
	}
	vir_constant_table_rebuild(f);
	/* S3: the blocks tile the block region */
	expect = f->constant_count;
	for (uint32_t b = 0; b < f->block_count; b++) {
		const vir_block *k = &f->blocks[b];

		if (k->first != expect || (uint64_t)k->first + k->insn_count > f->insn_count) {
			vir_verify_fail(&v, "S3", b, VIR_BLOCK_NONE, "the block starts at %u, not at %u (or runs past the function)", k->first,
			      expect);
			tiled = false;
			break;
		}
		if (k->insn_count < 1) {
			vir_verify_fail(&v, "S3", b, VIR_BLOCK_NONE, "the block is empty");
			tiled = false;
			break;
		}
		expect += k->insn_count;
	}
	if (tiled && expect != f->insn_count) {
		vir_verify_fail(&v, "S3", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "the blocks end at %u, the function at %u", expect, f->insn_count);
		tiled = false;
	}
	if (!tiled)
		return v.error_count;
	for (uint32_t b = 0; b < f->block_count; b++) {
		const vir_block *k = &f->blocks[b];

		for (uint32_t i = 0; i < k->insn_count; i++) {
			const vir_insn *x = &f->insns[k->first + i];
			bool last = i + 1 == k->insn_count;

			v.value_blocks[k->first + i] = b;
			if (vir_opcode_is_constant(x->op)) {
				vir_verify_fail(&v, "S1", b, i, "a constant in the block region");
				v.is_stopped = true;
			} else if (vir_opcode_is_terminator(x->op) != last) {
				vir_verify_fail(&v, "S3", b, i, last ? "the block does not end in a terminator" :
				      "a terminator before the end of the block");
				v.is_stopped = true;
			}
		}
	}
	if (v.is_stopped)
		return v.error_count;
	for (uint32_t i = 0; i < f->constant_count; i++)
		v.value_blocks[i] = VIR_BLOCK_NONE;
	/* S4, S5, T, per instruction */
	for (uint32_t b = 0; b < f->block_count; b++) {
		const vir_block *k = &f->blocks[b];

		for (uint32_t i = 0; i < k->insn_count; i++) {
			const vir_insn *x = &f->insns[k->first + i];

			if (k->parameter_count > k->insn_count) {
				vir_verify_fail(&v, "S4", b, VIR_BLOCK_NONE, "the block has %u parameters and %u instructions", k->parameter_count, k->insn_count);
				break;
			}
			if ((i < k->parameter_count) != (x->op == VIR_OP_BPARAM)) {
				if (i < k->parameter_count)
					vir_verify_fail(&v, "S4", b, i, "%s where a block parameter belongs", vir_verify_opcode_name(x));
				else
					vir_verify_fail(&v, "S4", b, i, "a block parameter after the first instruction");
				continue;
			}
			if (x->op == VIR_OP_PARAM) {
				const char *e = vir_verify_signature_check(f, f->signature);
				uint32_t ng, w0 = 0, w1 = 0;
				bool ok = false;

				if (b != 0) {
					vir_verify_fail(&v, "S5", b, i, "param outside block 0");
					continue;
				}
				if (e) {
					vir_verify_fail(&v, "S5", b, i, "the function's signature is bad: %s", e);
					continue;
				}
				ng = VIR_SIGNATURE_GROUP_COUNT(f->extra[f->signature]);
				if (x->a == VIR_GROUP_RESULT) {
					w0 = f->extra[f->signature + 1];
					ok = VIR_GROUP_KIND(w0) == VIR_GROUP_SRET && x->b == 0 && x->type == VIR_TYPE_PTR;
					if (!ok)
						vir_verify_fail(&v, "S5", b, i, "param 0xffff is the sret pointer: piece 0, type ptr, sret result group");
				} else if (x->a >= ng) {
					vir_verify_fail(&v, "S5", b, i, "group %u does not exist (%u groups)", x->a, ng);
				} else {
					uint32_t cl;

					w0 = f->extra[f->signature + 3 + 2 * x->a];
					w1 = f->extra[f->signature + 4 + 2 * x->a];
					(void)w1;
					if (VIR_GROUP_KIND(w0) == VIR_GROUP_REG) {
						cl = x->b < VIR_GROUP_PIECE_COUNT(w0) ? VIR_GROUP_CLASS(w0, x->b) : 0;
						ok = cl && (cl == VIR_CLASS_INT ? x->type == VIR_TYPE_I64 || x->type == VIR_TYPE_PTR :
							    cl == VIR_CLASS_F32 ? x->type == VIR_TYPE_F32 : x->type == VIR_TYPE_F64);
						if (!ok)
							vir_verify_fail(&v, "S5", b, i, "piece %u of group %u does not exist or has type %s", x->b, x->a,
							      vir_verify_type_name(x->type));
					} else if (VIR_GROUP_KIND(w0) == VIR_GROUP_MEM) {
						ok = x->b == 0 && x->type == VIR_TYPE_PTR;
						if (!ok)
							vir_verify_fail(&v, "S5", b, i, "a memory group's param is piece 0 of type ptr");
					} else {
						vir_verify_fail(&v, "S5", b, i, "group %u passes nothing", x->a);
					}
				}
				if (!ok)
					continue;
			}
			vir_verify_insn_check(&v, b, i, k->first + i);
		}
	}
	if (v.is_stopped)
		return v.error_count;
	/* the graph: block targets in range (T2), then C, D, vir_allocator */
	for (uint32_t b = 0; b < f->block_count; b++) {
		const vir_insn *x = &f->insns[f->blocks[b].first + f->blocks[b].insn_count - 1];
		vir_insn y = *x;
		vir_operand_iterator it;
		uint32_t *p;

		vir_operand_iterator_init(&it, f, &y, true);
		while ((p = vir_operand_iterator_next(&it)))
			if (*p >= f->block_count) {
				vir_verify_fail(&v, "T2", b, f->blocks[b].insn_count - 1, "%s: block %u does not exist", vir_verify_opcode_name(x), *p);
				return v.error_count;
			}
	}
	if (!vir_dominators_build(f)) {
		vir_verify_fail(&v, "T2", VIR_BLOCK_NONE, VIR_BLOCK_NONE, "the control-flow graph cannot be built");
		return v.error_count;
	}
	vir_verify_graph_check(&v);
	vir_verify_misc_check(&v);
	vir_verify_slots_check(&v);
	if (!v.error_count)
		vir_verify_canonical_check(&v);
	return v.error_count;
}
