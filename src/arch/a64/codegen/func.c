/* vcc code generator: functions: prologue, epilogue, finishing */
#include "../../../codegen/codegen-internal.h"

/* ---------------- functions ---------------- */

/*
 * VIR's functions (e->vlow) get two more frame shapes: a function that calls nothing and
 * does not touch x29/x30 but has slots or callee-saved registers keeps no frame record
 * (as LLVM's leaf functions: sub sp, saves from sp, add sp); and with no slot below the
 * record, sp is x29 all along and the epilogue needs no mov sp, x29.
 */
static _Thread_local struct {
	bool sleaf;		/* the first: sp-relative saves, fsz bytes below the incoming sp */
	bool nomov;		/* the second */
	int64_t fsz;
} FK;

/* where callee-saved register k of csave bytes is: base register and offset */
static inline int cs_base(void) { return FK.sleaf ? SP : R_FP; }
static inline int64_t cs_off(int32_t csave, int k) { return (FK.sleaf ? FK.fsz : 0) - csave + 8 * k; }

/* finish_function for the object backend: the same function as items */
/* ctor: 1 constructor, 2 destructor, 4 x28 is the frame base (mov x28, sp after the saves), 8 no frame (leaf) */
/* kpro: the record (NOP_PROLOG) in whose place the prologue goes, UINT32_MAX: at the top */
static void finish_direct(codegen_context *e, uint32_t s, uint32_t align, const int *cs, int ncs, int32_t csave, int64_t total, int ctor,
			  uint32_t kpro)
{
	uint32_t pre = kpro != UINT32_MAX ? kpro : 0, rest = kpro != UINT32_MAX ? kpro + 1 : 0;
	cc *c = e->compile_context;
	tbuf *b = &e->out;
	size_t start, h;
	codegen_code_header *ch;
	ins *in;
	uint32_t n = 0, sym_labels = 0;
	uint32_t tail = a64_tail_ref(e);

	b->n = (b->n + 7) & ~(size_t)7;
	start = b->n;
	codegen_symbol_header_append(e, b, s, true, "\t.text\n", align);
	for (uint32_t k = 0; k < e->record_count && !sym_labels; k++)
		sym_labels = e->records[k].op == I_RAW;
	h = codegen_item_open(e, CODEGEN_ITEM_CODE);
	ch = codegen_item_room_need(e, sizeof *ch);
	in = codegen_item_room_need(e, ((size_t)e->record_count + 2 * (size_t)ncs + 8) * sizeof(ins));
#define PUT(...) (in[n++] = (ins){ __VA_ARGS__ })
	memcpy(in, e->records, (size_t)pre * sizeof(ins));
	n = pre;
	if (ctor & 8)
		;
	else if (FK.sleaf)
		PUT(.op = I_SUBI, .sz = 1, .rd = SP, .rn = SP, .imm = FK.fsz);
	else if (PUT(.op = I_STP, .sz = 1, .rd = R_FP, .ra = R_LR, .rn = SP, .cond = 1, .imm = -16),
		 PUT(.op = I_MOV, .sz = 1, .rd = R_FP, .rn = SP), e->frame_alignment > 16) {	/* sp = (sp - frame) & -falign */
		PUT(.op = I_SUBI, .sz = 1, .rd = R_X16, .rn = SP, .imm = total - 16);
		PUT(.op = I_ANDI, .sz = 1, .rd = SP, .rn = R_X16, .imm = -(int64_t)e->frame_alignment);
	} else if (total > 16) {
		PUT(.op = I_SUBI, .sz = 1, .rd = SP, .rn = SP, .imm = total - 16);
	}
	for (int k = 0; k + 1 < ncs; k += 2)
		PUT(.op = I_STP, .sz = 1, .rd = (uint8_t)cs[k], .ra = (uint8_t)cs[k + 1], .rn = (uint8_t)cs_base(), .imm = cs_off(csave, k));
	if (ncs & 1)
		PUT(.op = I_ST, .sz = 3, .rd = (uint8_t)cs[ncs - 1], .rn = (uint8_t)cs_base(), .imm = cs_off(csave, ncs - 1));
	if (ctor & 4)
		PUT(.op = I_MOV, .sz = 1, .rd = R_BP, .rn = SP);
	memcpy(in + n, e->records + rest, (size_t)(e->record_count - rest) * sizeof(ins));
	n += e->record_count - rest;
	for (int k = 0; k + 1 < ncs; k += 2)
		PUT(.op = I_LDP, .sz = 1, .rd = (uint8_t)cs[k], .ra = (uint8_t)cs[k + 1], .rn = (uint8_t)cs_base(), .imm = cs_off(csave, k));
	if (ncs & 1)
		PUT(.op = I_LD, .sz = 3, .rd = (uint8_t)cs[ncs - 1], .rn = (uint8_t)cs_base(), .imm = cs_off(csave, ncs - 1));
	if (ctor & 8)
		;
	else if (FK.sleaf) {
		PUT(.op = I_ADDI, .sz = 1, .rd = SP, .rn = SP, .imm = FK.fsz);
	} else {
		if (!FK.nomov)
			PUT(.op = I_MOV, .sz = 1, .rd = SP, .rn = R_FP);
		PUT(.op = I_LDP, .sz = 1, .rd = R_FP, .ra = R_LR, .rn = SP, .cond = 2, .imm = 16);
	}
	if (tail)
		PUT(.op = I_TAIL, .cond = 1, .ref = tail);
	else
		PUT(.op = I_RET);
#undef PUT
	/* give back the unused room, then the inline asm text */
	b->n -= ((size_t)e->record_count + 2 * (size_t)ncs + 8 - n) * sizeof(ins);
	*ch = (codegen_code_header){ .function_id = s, .record_count = n, .raw_count = sym_labels ? (uint32_t)e->raw.n : 0, .label_count = e->label_count, .symbol_labels = sym_labels };
	if (sym_labels)
		memcpy(codegen_item_room_need(e, e->raw.n), e->raw.p, e->raw.n);
	codegen_item_close(e, h);
	h = codegen_item_open(e, CODEGEN_ITEM_FUNCTION_END);
	*(uint32_t *)codegen_item_room_need(e, sizeof(uint32_t)) = s;
	codegen_item_close(e, h);
	if (ctor & 1) {
		h = codegen_item_open(e, CODEGEN_ITEM_TEXT);
		codegen_constructor_entry_append(c, b, s, false);
		codegen_item_close(e, h);
	}
	if (ctor & 2) {
		h = codegen_item_open(e, CODEGEN_ITEM_TEXT);
		codegen_constructor_entry_append(c, b, s, true);
		codegen_item_close(e, h);
	}
	e->unit->record_total += e->record_count;
	e->unit->function_total++;
	em_switch_values_emit(e);
	codegen_chunk_add(e, (uint64_t)s << 1, start);
}

typedef struct a64_fallthrough_debug {
	codegen_context *e;
	uint32_t cursor;
} a64_fallthrough_debug;

static void a64_fallthrough_remap(void *ctx, uint32_t old, uint32_t next)
{
	a64_fallthrough_debug *d = ctx;

	codegen_debug_rows_remap(d->e, &d->cursor, old, next);
}

static void drop_fallthrough(codegen_context *e)
{
	static const record_layout layout = {
		sizeof(ins), offsetof(ins, op), offsetof(ins, label), I_B, I_LABEL,
	};
	a64_fallthrough_debug d = { e, 0 };

	e->record_count = record_fallthrough_drop(e->records, e->record_count, &layout, e->debug_row_count ? a64_fallthrough_remap : NULL, &d);
}

static const struct codegen_jump_table *jt_of(const codegen_context *e, uint32_t tab)
{
	for (uint32_t t = 0; t < e->jump_table_count; t++)
		if (e->jump_tables[t].table_label == tab)
			return &e->jump_tables[t];
	abort();
}

/*
 * I_JT -> adr x16, table; add x17, x16, idx, lsl 2; ldrsw x17, [x17]; add x16, x16, x17;
 * br x16, then the table in place: its label and one word per entry, target - table in
 * bytes (no relocation, in .text as clang does on Darwin).  A function with inline asm
 * assembles the words as text (the labels are named .LBB symbols there).
 */
static void expand_tables(codegen_context *e, uint32_t fid)
{
	uint64_t total = e->record_count;
	bool raw = false;

	if (!e->jump_table_count)
		return;
	for (uint32_t k = 0; k < e->record_count; k++) {
		raw |= e->records[k].op == I_RAW;
		if (e->records[k].op == I_JT)
			total += 5 + jt_of(e, e->records[k].label)->entry_count;
	}
	raw &= codegen_context_is_direct(e);
	if (total > e->record_capacity)
		codegen_fatal_report(e, 0, "function too large");
	for (uint32_t r = 0, j = 0, extra = 0; r < e->debug_row_count; r++) {
		for (; j < e->debug_rows[r].record_index && j < e->record_count; j++)
			if (e->records[j].op == I_JT)
				extra += 5 + jt_of(e, e->records[j].label)->entry_count;
		e->debug_rows[r].record_index += extra;
	}
	uint32_t d = (uint32_t)total;
	for (uint32_t k = e->record_count; k-- > 0; ) {
		ins x = e->records[k];

		if (x.op != I_JT) {
			e->records[--d] = x;
			continue;
		}
		const struct codegen_jump_table *j = jt_of(e, x.label);

		d -= 6 + j->entry_count;
		ins *o = &e->records[d];
		o[0] = (ins){ .op = I_ADR, .rd = R_X16, .label = x.label };
		o[1] = (ins){ .op = I_ADD, .sz = 1, .rd = R_X17, .rn = R_X16, .rm = x.rd, .sh = 2, .cond = x.cond ? 8 + 2 : 0 };
		o[2] = (ins){ .op = I_LD, .sz = 2, .cond = 1, .rd = R_X17, .rn = R_X17 };
		o[3] = (ins){ .op = I_ADD, .sz = 1, .rd = R_X16, .rn = R_X16, .rm = R_X17 };
		o[4] = (ins){ .op = I_BR, .rn = R_X16 };
		o[5] = (ins){ .op = I_LABEL, .label = j->table_label };
		for (uint32_t n = 0; n < j->entry_count; n++) {
			uint32_t target = e->jump_table_labels[j->first_entry + n];

			if (raw) {
				char b[64];
				int len = snprintf(b, sizeof b, "\t.word\t.LBB%x_%x-.LBB%x_%x", fid, target, fid, j->table_label);

				o[6 + n] = (ins){ .op = I_RAW, .off = (int32_t)e->raw.n, .ref = (uint32_t)len };
				vtext_append(&e->raw, b, (size_t)len);
			} else {
				o[6 + n] = (ins){ .op = I_JTE, .off = (int32_t)target, .ref = j->table_label };
			}
		}
	}
	e->record_count = (uint32_t)total;
}

/*
 * tbz/tbnz reach +-32 KB: one whose label may be farther (word offsets from a64_words, with
 * room for the printer's and the object writer's own relaxation of b.cond) becomes the
 * inverted test over a b, as LLVM's BranchRelaxation does.
 */
static void tbz_relax(codegen_context *e)
{
	enum { NEAR = 6000 };	/* words, of the 8191 tbz reaches */
	uint32_t m = 0, n = e->record_count, *at, total = 0;

	for (uint32_t k = 0; k < n; k++)
		m += e->records[k].op == I_TBZ || e->records[k].op == I_TBNZ;
	if (!m)
		return;
	at = malloc(((size_t)n + 1 + e->label_count) * sizeof *at);
	if (!at)
		abort();
	uint32_t *lat = at + n + 1;

	for (uint32_t k = 0; k < n; k++) {
		at[k] = total;
		if (e->records[k].op == I_LABEL)
			lat[e->records[k].label] = total;
		total += a64_words(&e->records[k]);
		if (total > (1u << 30))
			total = 1u << 30;
	}
	at[n] = total;
	if ((uint64_t)n + 2ull * m > e->record_capacity) {
		fputs("vcc: function too large\n", stderr);
		abort();
	}
	uint32_t w = n + 2 * m;

	for (uint32_t k = n; k-- > 0;) {
		ins t = e->records[k];
		int64_t d;

		if (t.op != I_TBZ && t.op != I_TBNZ) {
			e->records[--w] = t;
			continue;
		}
		d = (int64_t)lat[t.label] - at[k];
		if (total < (1u << 17) && d > -NEAR && d < NEAR) {
			e->records[--w] = t;
			continue;
		}
		uint32_t l = newlabel(e);

		for (uint32_t r = 0; r < e->debug_row_count; r++)	/* (-g rows after it move by the two records) */
			if (e->debug_rows[r].record_index > k)
				e->debug_rows[r].record_index += 2;
		e->records[--w] = (ins){ .op = I_LABEL, .label = l };
		e->records[--w] = (ins){ .op = I_B, .label = t.label };
		t.op = t.op == I_TBZ ? I_TBNZ : I_TBZ;
		t.label = l;
		e->label_reference_counts[l] = 1;
		e->records[--w] = t;
	}
	free(at);
	memmove(e->records, e->records + w, (size_t)(n + 2 * m - w) * sizeof(ins));
	e->record_count = n + 2 * m - w;
}

/* I_NOP.cond in a sibling call's epilogue with unwind tables: the printer's .cfi rows */
enum { NOP_CFI_REMEMBER = 0xc1, NOP_CFI_EPILOGUE, NOP_CFI_RESTORE };

/* VIR's sibling calls in the body (I_TAIL cond 2, 3): the epilogue, then the branch; with
 * unwind tables between .cfi_remember_state and .cfi_restore_state (as clang) */
static void tail_expand(codegen_context *e, const int *cs, int ncs, int32_t csave, bool leaf, bool cfi)
{
	uint32_t m = 0, add = (uint32_t)(ncs + 1) / 2 + 7;
	/* a return (cond 4) gets its own epilogue up to this many instructions, ret included */
	bool dup = (ncs + 1) / 2 + (leaf ? 0 : FK.sleaf || FK.nomov ? 1 : 2) + 1 <= 6;

	for (uint32_t k = 0; k < e->record_count; k++)
		m += e->records[k].op == I_TAIL && e->records[k].cond >= 2;
	if (!m)
		return;
	cfi &= !leaf;	/* (no frame: the CFA stays sp) */
	if ((uint64_t)e->record_count + (uint64_t)m * add > e->record_capacity) {
		fputs("vcc: function too large\n", stderr);
		abort();
	}
	uint32_t n = e->record_count + m * add, w = n;

	for (uint32_t k = e->record_count; k-- > 0;) {
		ins t = e->records[k], seq[20];
		uint32_t q = 0;

		if (t.op != I_TAIL || t.cond < 2) {
			e->records[--w] = t;
			continue;
		}
		if (t.sh) {	/* in front of the prologue (shrink-wrapping): nothing to restore */
			e->records[--w] = t.cond == 2 ? (ins){ .op = I_TAIL, .cond = 1, .ref = t.ref } :
				     t.cond == 4 ? (ins){ .op = I_RET } : (ins){ .op = I_BR, .rn = R_X16 };
			if (t.cond == 3 && t.rn != R_X16)
				e->records[--w] = (ins){ .op = I_MOV, .sz = 1, .rd = R_X16, .rn = t.rn };
			continue;
		}
		if (t.cond == 4) {
			uint32_t j = k + 1;

			while (j < e->record_count && e->records[j].op == I_LABEL)
				j++;
			if (j == e->record_count)	/* the last record: falls into the shared epilogue */
				continue;
			if (!dup) {
				e->records[--w] = (ins){ .op = I_B, .label = t.label };
				continue;
			}
		}
		if (t.cond == 3 && t.rn != R_X16)
			seq[q++] = (ins){ .op = I_MOV, .sz = 1, .rd = R_X16, .rn = t.rn };
		if (cfi)
			seq[q++] = (ins){ .op = I_NOP, .cond = NOP_CFI_REMEMBER };
		for (int j = 0; j + 1 < ncs; j += 2)
			seq[q++] = (ins){ .op = I_LDP, .sz = 1, .rd = (uint8_t)cs[j], .ra = (uint8_t)cs[j + 1], .rn = (uint8_t)cs_base(),
					  .imm = cs_off(csave, j) };
		if (ncs & 1)
			seq[q++] = (ins){ .op = I_LD, .sz = 3, .rd = (uint8_t)cs[ncs - 1], .rn = (uint8_t)cs_base(), .imm = cs_off(csave, ncs - 1) };
		if (leaf)
			;
		else if (FK.sleaf) {
			seq[q++] = (ins){ .op = I_ADDI, .sz = 1, .rd = SP, .rn = SP, .imm = FK.fsz };
		} else {
			if (!FK.nomov)
				seq[q++] = (ins){ .op = I_MOV, .sz = 1, .rd = SP, .rn = R_FP };
			seq[q++] = (ins){ .op = I_LDP, .sz = 1, .rd = R_FP, .ra = R_LR, .rn = SP, .cond = 2, .imm = 16 };
		}
		if (cfi)
			seq[q++] = (ins){ .op = I_NOP, .cond = NOP_CFI_EPILOGUE };
		seq[q++] = t.cond == 2 ? (ins){ .op = I_TAIL, .cond = 1, .ref = t.ref } :
			   t.cond == 4 ? (ins){ .op = I_RET } : (ins){ .op = I_BR, .rn = R_X16 };
		if (cfi)
			seq[q++] = (ins){ .op = I_NOP, .cond = NOP_CFI_RESTORE };
		w -= q;
		memcpy(&e->records[w], seq, q * sizeof(ins));
	}
	/* (w > 0: the room not used, at the front) */
	memmove(e->records, e->records + w, (size_t)(n - w) * sizeof(ins));
	e->record_count = n - w;
}

VPRIV void finish_function(codegen_context *e, uint32_t s)
{
	cc *c = e->compile_context;
	tbuf *b = &e->out;
	size_t start = b->n;
	int cs[16], ncs = 0;
	int32_t csave;
	int64_t locals, outs, total;
	const attrs *a = codegen_symbol_attributes(c, s);
	uint32_t align = 4;

	bool bp = e->frame_alignment > 16 && e->dynamic_frame, leaf = false;

	FK.sleaf = FK.nomov = false;

	if (e->unit->options->opt >= 1) {	/* -O1 and up */
		unsigned fl;
		uint64_t u;
		const uint32_t csm = (1u << 29) - (1u << 19);	/* x19-x28 */

		if (unlikely(e->unit->record_statistics))
			rp_count(e, RP_FUNC);
		if (!e->unit->cleanups_disabled) {
			RP_BEGIN(e);
			a64_unreachable(e);
			RP_END(e, RP_UNREACHABLE);
		}
		RP_BEGIN(e);
		drop_fallthrough(e);
		RP_END(e, RP_FALLTHROUGH);
		if (!e->unit->cleanups_disabled)
			opt_function(e);
		/*
		 * What the records still need: callee-saved registers the clean-ups
		 * left unused (register variables, saved temporaries) are not saved,
		 * a frame no slot is referenced in has no locals, and a function
		 * that needs nothing of a frame has none (as clang: leaf frame
		 * pointers omitted).
		 */
		RP_BEGIN(e);
		u = opt_regs(e, &fl);
		RP_END(e, RP_OPT_REGS);
		if (!(fl & OPF_ALL)) {
			e->callee_saved_used &= ((uint32_t)u & csm) | ~csm;
			if (!(fl & OPF_FRAME) && !e->dynamic_frame && e->frame_alignment <= 16)
				e->frame_maximum = 0;
			leaf = !(fl & (OPF_CALL | OPF_FRAME)) && !e->dynamic_frame && !bp && e->frame_alignment <= 16 && !e->outgoing_argument_bytes_max &&
			       !(e->callee_saved_used & csm) && !(u & ((1ull << 29) | (1ull << 30)));
			FK.sleaf = !leaf && e->vir_lowering && !(fl & OPF_CALL) && !e->dynamic_frame && !bp && e->frame_alignment <= 16 && !e->outgoing_argument_bytes_max &&
				   !(u & ((1ull << 29) | (1ull << 30)));
		}
	}

	RP_BEGIN(e);
	expand_tables(e, s);
	RP_END(e, RP_EXPAND_TABLES);
	/*
	 * Frame below x29: callee saves, locals, outgoing arguments.  Slots are
	 * addressed from sp, so with locals over-aligned (falign > 16) the
	 * prologue rounds sp down to falign and locals/outs are multiples of
	 * it: the top of the locals is then falign-aligned too.  A frame
	 * addressed from x29 (VLA, alloca: sp moves) uses x28 instead, set to
	 * the realigned sp in the prologue; x28 has been kept out of the
	 * temporaries since the first over-aligned slot (value.c slot()).
	 */
	if (bp) {
		if (e->callee_saved_used & (1u << R_BP))
			codegen_fatal_report(e, c->syms[s].loc, "over-aligned local in a function with a VLA or alloca that needs every temporary register");
		e->callee_saved_used |= 1u << R_BP;
	}
	for (int r = 19; r <= 28; r++)
		if (e->callee_saved_used & (1u << r))
			cs[ncs++] = r;
	csave = (ncs * 8 + 15) & ~15;
	if ((uint64_t)e->frame_maximum > (uint64_t)INT64_MAX - e->frame_alignment ||
	    (uint64_t)e->outgoing_argument_bytes_max > (uint64_t)INT64_MAX - e->frame_alignment)
		codegen_fatal_report(e, 0, "stack frame too large");
	locals = (int64_t)(((uint64_t)e->frame_maximum + e->frame_alignment - 1) & ~(uint64_t)(e->frame_alignment - 1));
	outs = (int64_t)(((uint64_t)e->outgoing_argument_bytes_max + e->frame_alignment - 1) & ~(uint64_t)(e->frame_alignment - 1));
	if (locals > INT64_MAX - outs - csave - 16)
		codegen_fatal_report(e, 0, "stack frame too large");
	total = 16 + csave + locals + outs;
	FK.fsz = total - 16;
	if (FK.sleaf && FK.fsz > 496)	/* (ldp/stp from sp reach 504) */
		FK.sleaf = false;
	FK.nomov = e->vir_lowering && !leaf && !FK.sleaf && total == 16 && !e->dynamic_frame && !bp && e->frame_alignment <= 16;
	/* shrink-wrapping: VIR's lowering marked where the prologue may go (NOP_PROLOG); it goes
	 * there when there is a plain frame to set up and the records in front need none of it */
	uint32_t kpro = UINT32_MAX;

	RP_BEGIN(e);
	for (uint32_t k = 0; k < e->record_count; k++)
		if (e->records[k].op == I_NOP && e->records[k].cond == NOP_PROLOG) {
			if (!leaf && !bp && !e->dynamic_frame && e->frame_alignment <= 16 && !e->unit->options->debug && !(e->unit->options->cfi && !CODEGEN_IS_DARWIN(c)) &&
			    opt_noframe(e, k))
				kpro = k;
			else
				e->records[k].cond = 0;
			break;
		}
	RP_END(e, RP_NOFRAME);
	RP_BEGIN(e);
	if (kpro == UINT32_MAX)
		for (uint32_t k = 0; k < e->record_count; k++)
			if (e->records[k].op == I_TAIL)
				e->records[k].sh = 0;
	tail_expand(e, cs, ncs, csave, leaf, e->unit->options->cfi && !CODEGEN_IS_DARWIN(c));
	RP_END(e, RP_TAIL_EXPAND);
	if (kpro != UINT32_MAX)
		for (kpro = 0; e->records[kpro].op != I_NOP || e->records[kpro].cond != NOP_PROLOG; kpro++)
			;
	/* rewrite frame-slot references */
	RP_BEGIN(e);
	for (uint32_t k = 0; k < e->record_count; k++) {
		ins *i = &e->records[k];

		if (i->op == I_ADDI && i->rn == SP && i->cond == 0xee) {
			i->imm += outs;
			i->cond = 0;
			continue;
		}
		if (i->rn != FRAME)
			continue;
		i->rm = i->ra = 0;	/* slot tags */
		if (i->op != I_LD && i->op != I_ST)
			i->cond = i->sh = 0;
		if (bp) {
			i->rn = R_BP;
			i->imm = i->imm - csave + (total - 16);
		} else if (e->dynamic_frame) {
			i->rn = R_FP;
			i->imm = i->imm - csave;
		} else {
			i->rn = SP;
			i->imm = i->imm - csave + (total - 16);
		}
	}
	RP_END(e, RP_FRAME_REWRITE);
	if (a->aligned > align)
		align = a->aligned;
	if (c->syms[s].align > align)
		align = c->syms[s].align;
	RP_BEGIN(e);
	tbz_relax(e);
	RP_END(e, RP_TBZ_RELAX);
	if (codegen_context_is_direct(e)) {
		finish_direct(e, s, align, cs, ncs, csave, total, (a->constructor != 0) | (a->destructor != 0) << 1 | bp << 2 | leaf << 3,
			      kpro);
		return;
	}
	codegen_symbol_header_append(e, b, s, true, "\t.text\n", align);
	uint32_t dj = 0;	/* -g: the next row */

	if (unlikely(e->unit->options->debug) && !(leaf && !ncs && !bp)) {
		/* (none without prologue instructions, as clang) the function's own line, then its first statement's with prologue_end (as clang) */
		uint32_t f, l, cl;

		/* clang: the line of the body's '{', column 0 */
		if (codegen_debug_location_get(e, c->syms[s].body ? N(c->syms[s].body)->loc : c->syms[s].loc, &f, &l, &cl))
			vtext_format_append(b, "\t.loc\t%u %u 0\n", f, l);
	}
	/* prologue; with unwind tables the CFA is x29 + 16 once the frame is set up (as clang) */
	bool cfi = e->unit->options->cfi && !CODEGEN_IS_DARWIN(c);

	if (cfi)
		vtext_string_append(b, "\t.cfi_startproc\n");
	/* text has no relaxation (the object writer's r_relax): in a function that
	 * may pass b.cond/cbz's +-1 MB, every one is the inverted branch over a b */
	bool far = e->record_count > (1u << 17);

	for (uint32_t k = 0; k <= e->record_count; k++) {
		if (k == (kpro != UINT32_MAX ? kpro : 0)) {	/* the prologue: in front of the body, or at the save point */
			if (leaf)
				;
			else if (FK.sleaf) {
				ins i = { .op = I_SUBI, .sz = 1, .rd = SP, .rn = SP, .imm = FK.fsz };

				print_ins(c, b, &i, s, e->raw.p);
				if (cfi)
					vtext_format_append(b, "\t.cfi_def_cfa_offset %d\n", (int)FK.fsz);
			} else if (vtext_string_append(b, cfi ? "\tstp\tx29, x30, [sp, #-16]!\n\t.cfi_def_cfa_offset 16\n\tmov\tx29, sp\n"
					      "\t.cfi_def_cfa w29, 16\n\t.cfi_offset w30, -8\n\t.cfi_offset w29, -16\n" :
					      "\tstp\tx29, x30, [sp, #-16]!\n\tmov\tx29, sp\n"), e->frame_alignment > 16) {
				ins i = { .op = I_SUBI, .sz = 1, .rd = R_X16, .rn = SP, .imm = total - 16 };
				ins a = { .op = I_ANDI, .sz = 1, .rd = SP, .rn = R_X16, .imm = -(int64_t)e->frame_alignment };

				print_ins(c, b, &i, s, e->raw.p);
				print_ins(c, b, &a, s, e->raw.p);
			} else if (total > 16) {
				ins i = { .op = I_SUBI, .sz = 1, .rd = SP, .rn = SP, .imm = total - 16 };

				print_ins(c, b, &i, s, e->raw.p);
			}
			for (int j = 0; j + 1 < ncs; j += 2)
				vtext_format_append(b, "\tstp\tx%d, x%d, [%s, #%d]\n", cs[j], cs[j + 1], FK.sleaf ? "sp" : "x29", (int)cs_off(csave, j));
			if (ncs & 1)
				vtext_format_append(b, "\tstr\tx%d, [%s, #%d]\n", cs[ncs - 1], FK.sleaf ? "sp" : "x29", (int)cs_off(csave, ncs - 1));
			if (cfi)
				for (int j = 0; j < ncs; j++)
					vtext_format_append(b, "\t.cfi_offset w%d, %d\n", cs[j], -csave + 8 * j - (FK.sleaf ? 0 : 16));
			if (bp)
				vtext_string_append(b, "\tmov\tx28, sp\n");
		}
		if (k == e->record_count)
			break;
		if (k == kpro)
			continue;	/* (the marker) */
		const ins *i = &e->records[k];

		for (; dj < e->debug_row_count && e->debug_rows[dj].record_index == k; dj++)
			if (dj + 1 == e->debug_row_count || e->debug_rows[dj + 1].record_index != k)	/* the last of several at one place */
				vtext_format_append(b, dj ? "\t.loc\t%u %u %u\n" : "\t.loc\t%u %u %u prologue_end\n",
				     e->debug_rows[dj].file, e->debug_rows[dj].line, e->debug_rows[dj].column);
		if (far && ((i->op == I_BC && i->cond < 14) || i->op == I_CBZ || i->op == I_CBNZ)) {
			ins n = *i;

			if (n.op == I_BC)
				n.cond ^= 1;
			else
				n.op = n.op == I_CBZ ? I_CBNZ : I_CBZ;
			n.label = 0;
			print_ins(c, b, &n, s, e->raw.p);
			{
				/* the printed label is the last token on the line: point it at 1f */
				size_t l = b->n - 1;

				while (l && b->p[l - 1] != '\t' && b->p[l - 1] != ' ')
					l--;
				b->n = l;
				vtext_string_append(b, "1f\n\tb\t");
				p_lbl(b, s, i->label);
				vtext_string_append(b, "\n1:\n");
			}
			continue;
		}
		if (i->op == I_NOP && cfi && i->cond >= NOP_CFI_REMEMBER) {
			if (i->cond == NOP_CFI_REMEMBER) {
				vtext_string_append(b, "\t.cfi_remember_state\n");
			} else if (i->cond == NOP_CFI_RESTORE) {
				vtext_string_append(b, "\t.cfi_restore_state\n");
			} else {
				for (int k = 0; k < ncs; k++)
					vtext_format_append(b, "\t.cfi_restore w%d\n", cs[k]);
				if (!leaf && !FK.sleaf)
					vtext_string_append(b, "\t.cfi_restore w29\n\t.cfi_restore w30\n");
				vtext_string_append(b, "\t.cfi_def_cfa wsp, 0\n");
			}
			continue;
		}
		print_ins(c, b, i, s, e->raw.p);
	}
	for (; dj < e->debug_row_count; dj++)
		if (dj + 1 == e->debug_row_count)
			vtext_format_append(b, dj ? "\t.loc\t%u %u %u\n" : "\t.loc\t%u %u %u prologue_end\n",
			     e->debug_rows[dj].file, e->debug_rows[dj].line, e->debug_rows[dj].column);
	/* epilogue */
	for (int k = 0; k + 1 < ncs; k += 2)
		vtext_format_append(b, "\tldp\tx%d, x%d, [%s, #%d]\n", cs[k], cs[k + 1], FK.sleaf ? "sp" : "x29", (int)cs_off(csave, k));
	if (ncs & 1)
		vtext_format_append(b, "\tldr\tx%d, [%s, #%d]\n", cs[ncs - 1], FK.sleaf ? "sp" : "x29", (int)cs_off(csave, ncs - 1));
	uint32_t tail = a64_tail_ref(e);

	if (leaf)
		;
	else if (FK.sleaf) {
		ins i = { .op = I_ADDI, .sz = 1, .rd = SP, .rn = SP, .imm = FK.fsz };

		print_ins(c, b, &i, s, e->raw.p);
	} else
		vtext_string_append(b, FK.nomov ? "\tldp\tx29, x30, [sp], #16\n" : "\tmov\tsp, x29\n\tldp\tx29, x30, [sp], #16\n");
	if (tail) {
		if (cfi) {
			for (int k = 0; k < ncs; k++)
				vtext_format_append(b, "\t.cfi_restore w%d\n", cs[k]);
			if (!leaf && !FK.sleaf)
				vtext_string_append(b, "\t.cfi_restore w29\n\t.cfi_restore w30\n");
			vtext_string_append(b, "\t.cfi_def_cfa wsp, 0\n");
		}
		ins i = { .op = I_TAIL, .cond = 1, .ref = tail };

		print_ins(c, b, &i, s, e->raw.p);
	} else {
		vtext_string_append(b, "\tret\n");
	}
	if (cfi)
		vtext_string_append(b, "\t.cfi_endproc\n");
	vtext_string_append(b, ".Lfe");
	vtext_unsigned_append(b, s);
	vtext_string_append(b, ":\n\t.size\t");
	codegen_symbol_name_append(c, b, s);
	vtext_string_append(b, ", .Lfe");
	vtext_unsigned_append(b, s);
	vtext_char_append(b, '-');
	codegen_symbol_name_append(c, b, s);
	vtext_char_append(b, '\n');
	if (a->constructor) {
		codegen_constructor_entry_append(c, b, s, false);
	}
	if (a->destructor) {
		codegen_constructor_entry_append(c, b, s, true);
	}
	e->unit->record_total += e->record_count;
	e->unit->function_total++;
	em_switch_values_emit(e);
	codegen_chunk_add(e, (uint64_t)s << 1, start);
}

/*
 * The function's own scalar locals and params whose address is never
 * needed, most used first (loop depth counts 8x a level), live in
 * callee-saved registers x28 down for the whole function: no slot, a read
 * is a mov.  Those registers are then not expression temporaries here.
 */
static void choose_lregs(codegen_context *e, uint32_t s, int max)
{
	unsigned k;
	uint32_t cand[CODEGEN_LOCAL_REGISTER_MAX];

	e->local_register_candidate_count = 0;
	if (!max || e->is_variadic || e->unit->options->opt < 1)
		return;
	k = em_select_locals(e, s, cand, (unsigned)max, em_local_gpr_type);
	for (unsigned i = 0; i < k; i++) {
		int r = 28 - i;

		e->local_register_symbols[i] = cand[i];
		e->local_register_numbers[i] = (uint8_t)r;
		e->temporaries_in_use |= 1u << (16 - i);	/* tempregs[16 - i] == x(28 - i) */
		e->temporaries_reserved |= 1u << (16 - i);
		e->callee_saved_used |= 1u << r;
	}
	e->local_register_candidate_count = k;
}

/* true: out of temporaries with register variables, generate it again without */
static bool emit_function1(codegen_context *e, uint32_t s, int nlreg)
{
	em_switch_values_reset(e);
	cc *c = e->compile_context;
	const sym *y = &c->syms[s];
	const node *body = N(y->body);
	const ctype *ft = T(c, y->ty);
	ast_qualified_type rty = ft->base;
	codegen_function_instance F;
	int ngrn = 0, nsaa = 0;

	codegen_function_prepass_request(e, s);
	e->record_count = 0;
	e->debug_row_count = 0;
	e->raw.n = 0;
	e->label_count = 0;
	e->known_value_count = 0, e->known_values_valid = 0, e->known_value_state_count = 0, e->array_element_count = 0;
	e->frame_offset = e->frame_maximum = e->outgoing_argument_bytes_max = e->compound_literal_top = 0;
	e->slot_allocation_count = e->live_slot_count = 0;
	e->frame_alignment = 16, e->temporaries_reserved = 0, e->vla_size_count = 0;
	e->dynamic_frame = e->has_calls = false;
	e->temporaries_in_use = 0;
	e->callee_saved_used = 0;
	e->loop_count = e->cleanup_count = 0;
	e->case_count = 0;
	e->jump_table_count = e->jump_table_label_count = 0;
	e->is_reachable = true;
	/* a retry (longjmp 2, value.c) or an efatal out of dry()'s gen leaves it raised:
	 * this attempt, and every later function of this thread, would claim nothing */
	e->dry_run_depth = 0;
	e->top_symbol = s;
	e->asm_statement_count = 0;
	e->inline_depth = 0;
	e->is_variadic = (ft->flags & AST_TYPE_FLAG_VARIADIC) != 0;
	choose_lregs(e, s, nlreg);
	memset(&F, 0, sizeof(F));
	F.function = s;
	F.instance = ++e->next_instance;
	F.return_type = rty;
	F.return_label = newlabel(e);
	e->function_instance = &F;
	switch (_setjmp(e->bail_jump)) {
	case 0:
		break;
	case 2:
		e->function_instance = NULL;
		return true;
	default:
		e->function_instance = NULL;
		return false;
	}
	if (is_sret(c, rty)) {
		F.return_slot = slot(e, 8, 8);
		mem(e, I_ST, 3, 0, 8, FRAME, F.return_slot);
	}
	/* parameters */
	e->next_vector_register = 0;
	for (uint32_t i = 0; i < body->aux; i++) {
		uint32_t p = c->lists[body->a + i];
		ast_qualified_type pt = c->syms[p].ty;
		a64_abi_argument ab;
		codegen_binding *bd = codegen_symbol_binding_get(e, p);
		uint64_t sz = codegen_type_size(c, pt);

		/* a K&R definition without a prototype gets a float as a double (default promotions) */
		bool kfl = (T(c, c->syms[s].ty)->flags & AST_TYPE_FLAG_NO_PROTOTYPE) && KIND(c, pt) == AST_TYPE_FLOAT;

		classify(e, kfl ? codegen_type_primitive(AST_TYPE_DOUBLE) : pt, &ngrn, &nsaa, &ab);
		if (kfl && ab.kind == A64_ABI_ARGUMENT_VECTOR_REGISTER) {
			word(e, 0x1e624000u | (unsigned)ab.register_number << 5 | ab.register_number);	/* fcvt s, d */
			ab.size = 4;
		}
		bd->instance = F.instance;
		bd->register_number = 0xff;
		if ((ab.kind == A64_ABI_ARGUMENT_REGISTER || ab.kind == A64_ABI_ARGUMENT_STACK) && lreg_of(e, p) >= 0) {
			int r = lreg_of(e, p);

			bd->kind = CODEGEN_BINDING_LOCAL_REGISTER;
			bd->known_value_index = 0;
			bd->value = codegen_value_register(r);
			if (ab.kind == A64_ABI_ARGUMENT_REGISTER)
				extend(e, r, ab.register_number, (int)sz, codegen_type_is_signed(c, pt));	/* AAPCS64: high bits unspecified */
			else
				mem(e, I_LD, log2sz(sz), sz < 8 && codegen_type_is_signed(c, pt) ? 1 : 0, r, R_FP, 16 + ab.stack_offset);
			continue;
		}
		switch (ab.kind) {
		case A64_ABI_ARGUMENT_IGNORE:
			bd->kind = CODEGEN_BINDING_SLOT;
			bd->slot = slot(e, 8, 8);
			break;
		case A64_ABI_ARGUMENT_BOOL_LANES: {
			bd->kind = CODEGEN_BINDING_SLOT;
			bd->slot = slot(e, sz, 16);
			int acc = treg(e), r = treg(e);
			for (unsigned k = 0; k < T(c, pt)->n; k++) {
				if (!(k % 8)) movi(e, 1, acc, 0);
				if (k < ab.register_count) mov(e, 0, r, ab.register_number + k);
				else mem(e, I_LD, 0, 0, r, R_FP, 16 + ab.stack_offset + (k - ab.register_count) * 8);
				opbf(e, I_UBFM, 1, r, r, 0, 0);
				op3s(e, I_ORR, 1, acc, acc, r, 0, k % 8);
				if (k % 8 == 7) mem(e, I_ST, 0, 0, acc, FRAME, bd->slot + k / 8);
			}
			tfree(e, acc), tfree(e, r);
			break;
		}
		case A64_ABI_ARGUMENT_REGISTER:
			bd->kind = CODEGEN_BINDING_SLOT;
			bd->slot = slot(e, (sz + 7) & ~(uint64_t)7, codegen_type_alignment(c, pt) > 8 ? 16 : 8);
			for (int k = 0; k < ab.register_count; k++)
				mem(e, I_ST, codegen_type_is_aggregate(c, pt) || sz == 16 ? 3 : log2sz(sz), 0, ab.register_number + k, FRAME, bd->slot + 8 * k);
			break;
		case A64_ABI_ARGUMENT_STACK:
			bd->kind = CODEGEN_BINDING_SLOT;
			bd->slot = slot(e, (sz + 7) & ~(uint64_t)7, codegen_type_alignment(c, pt) > 8 ? 16 : 8);
			if (CODEGEN_IS_DARWIN(c) && sz < 8 && !codegen_type_is_aggregate(c, pt)) {	/* Apple: packed */
				mem(e, I_LD, log2sz(sz), 0, R_X16, R_FP, 16 + ab.stack_offset);
				mem(e, I_ST, log2sz(sz), 0, R_X16, FRAME, bd->slot);
				break;
			}
			for (int k = 0; k < ab.register_count; k++) {
				int lg = ab.float_member_size && ab.size - 8 * k < 8 ? 2 : 3;	/* an Apple float HFA's last part */

				mem(e, I_LD, lg, 0, R_X16, R_FP, 16 + ab.stack_offset + 8 * k);
				mem(e, I_ST, lg, 0, R_X16, FRAME, bd->slot + 8 * k);
			}
			break;
		case A64_ABI_ARGUMENT_REFERENCE_REGISTER:
			bd->kind = CODEGEN_BINDING_INDIRECT;
			bd->slot = slot(e, 8, 8);
			mem(e, I_ST, 3, 0, ab.register_number, FRAME, bd->slot);
			break;
		case A64_ABI_ARGUMENT_REFERENCE_STACK:
			bd->kind = CODEGEN_BINDING_INDIRECT;
			bd->slot = slot(e, 8, 8);
			mem(e, I_LD, 3, 0, R_X16, R_FP, 16 + ab.stack_offset);
			mem(e, I_ST, 3, 0, R_X16, FRAME, bd->slot);
			break;
		case A64_ABI_ARGUMENT_VECTOR_REGISTER:
			bd->kind = CODEGEN_BINDING_SLOT;
			bd->slot = sz == 16 ? slot(e, 16, 16) : slot(e, 8, 8);
			vr_store(e, (int)ab.size, ab.register_number, FRAME, bd->slot);
			break;
		case A64_ABI_ARGUMENT_HFA:
			bd->kind = CODEGEN_BINDING_SLOT;
			bd->slot = slot(e, (sz + 7) & ~(uint64_t)7, codegen_type_alignment(c, pt) > 8 ? 16 : 8);
			for (int k = 0; k < ab.register_count; k++)
				vr_store(e, ab.float_member_size, ab.register_number + k, FRAME, bd->slot + ab.float_member_size * k);
			break;
		}
	}
	for (uint32_t i = 0; i < body->aux; i++) {	/* int (*a)[n]: n as on entry */
		ast_qualified_type pt = c->syms[c->lists[body->a + i]].ty;

		if (unlikely(codegen_type_is_variably_modified(c, pt)))
			vla_fix(e, pt, true);
	}
	if (e->is_variadic && CODEGEN_IS_DARWIN(c)) {
		e->named_general_register_count = ngrn;
		e->named_stack_bytes = nsaa;
	} else if (e->is_variadic) {
		e->named_general_register_count = ngrn;
		e->named_stack_bytes = nsaa;
		e->variadic_register_save_slot = slot(e, 64, 16);
		for (int r = ngrn; r < 8; r++)
			mem(e, I_ST, 3, 0, r, FRAME, e->variadic_register_save_slot + 8 * r);
		if (c->o->general_regs_only) {
			/* as clang: no FP/SIMD save area (the kernel may not touch v regs),
			   __vr_offs 0 and __vr_top = __gr_top */
			e->named_vector_register_count = 8;
			e->variadic_vector_save_slot = e->variadic_register_save_slot - 64;
		} else {
			/* v0-v7: 16 bytes each (the low 8 hold a float or double) */
			e->named_vector_register_count = e->next_vector_register;
			e->variadic_vector_save_slot = slot(e, 128, 16);
			for (int r = e->next_vector_register; r < 8; r++)
				vr_store(e, 16, r, FRAME, e->variadic_vector_save_slot + 16 * r);
		}
	}
	gen_stmt(e, y->body);
	/* falling off the end of main returns 0 */
	if (e->is_reachable && CODEGEN_IDENTIFIER_IS(y->name, "main"))
		movi(e, 0, 0, 0);
	deflabel(e, F.return_label);
	e->function_instance = NULL;
	if (unlikely(e->unit->stats)) {
		double t = codegen_clock_seconds();

		finish_function(e, s);
		e->unit->print_nanoseconds += (uint64_t)((codegen_clock_seconds() - t) * 1e9);
	} else {
		finish_function(e, s);
	}
	return false;
}

VPRIV void emit_function(codegen_context *e, uint32_t s)
{
	/* out of temporaries: again without register variables and with more of them free in inlined calls */
	e->inline_free_temporaries = CODEGEN_INLINE_FREE_FAST;
	if (emit_function1(e, s, e->unit->local_register_count)) {
		e->inline_free_temporaries = CODEGEN_INLINE_FREE_SAFE;
		if (emit_function1(e, s, 0)) {
			e->spill = 1;	/* still out: spill (expr.c gen_binary) */
			emit_function1(e, s, 0);
		}
	}
	e->local_register_candidate_count = 0, e->temporaries_reserved = 0, e->inline_free_temporaries = 0, e->spill = 0;	/* out of a function: no register variables */
}
