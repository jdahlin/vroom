/* vcc code generator, arm64: register names, record emission, FP moves
 * (included at the end of codegen/codegen-internal.h) */

/* ---------------- register names ---------------- */

static inline void a64_register_name_append(tbuf *b, int r, int x)
{
	char *p;

	vtext_need(b, 4);
	p = b->p + b->n;
	p[0] = x ? 'x' : 'w';
	if (likely(r < 10)) {
		p[1] = (char)('0' + r);
		b->n += 2;
	} else if (likely(r < 31)) {
		p[1] = (char)('0' + r / 10);
		p[2] = (char)('0' + r % 10);
		b->n += 3;
	} else if (r == ZR) {
		p[1] = 'z', p[2] = 'r';
		b->n += 3;
	} else {
		if (x)
			p[0] = 's', p[1] = 'p', b->n += 2;
		else
			p[1] = 's', p[2] = 'p', b->n += 3;
	}
}

/* ---------------- record emission ---------------- */

static inline a64_record *a64_record_emit(codegen_context *e, int op)
{
	a64_record *i;

	if (unlikely(e->record_count == e->record_capacity)) {
		fputs("vcc: function too large\n", stderr);
		abort();
	}
	i = &e->records[e->record_count++];
	memset(i, 0, sizeof(*i));
	i->op = (uint8_t)op;
	return i;
}

static inline void a64_record_emit_three_registers(codegen_context *e, int op, int x, int rd, int rn, int rm)
{
	a64_record *i;

	if (!e->is_reachable)
		return;
	i = a64_record_emit(e, op);
	i->sz = (uint8_t)x, i->rd = (uint8_t)rd, i->rn = (uint8_t)rn, i->rm = (uint8_t)rm;
}

static inline void a64_record_emit_three_registers_shifted(codegen_context *e, int op, int x, int rd, int rn, int rm, int kind, int sh)
{
	a64_record *i;

	if (!e->is_reachable)
		return;
	i = a64_record_emit(e, op);
	i->sz = (uint8_t)x, i->rd = (uint8_t)rd, i->rn = (uint8_t)rn, i->rm = (uint8_t)rm;
	i->cond = (uint8_t)kind, i->sh = (uint8_t)sh;
}

static inline void a64_record_emit_four_registers(codegen_context *e, int op, int x, int rd, int rn, int rm, int ra)
{
	a64_record *i;

	if (!e->is_reachable)
		return;
	i = a64_record_emit(e, op);
	i->sz = (uint8_t)x, i->rd = (uint8_t)rd, i->rn = (uint8_t)rn, i->rm = (uint8_t)rm, i->ra = (uint8_t)ra;
}

static inline void a64_record_emit_immediate(codegen_context *e, int op, int x, int rd, int rn, int64_t imm)
{
	a64_record *i;

	if (!e->is_reachable)
		return;
	i = a64_record_emit(e, op);
	i->sz = (uint8_t)x, i->rd = (uint8_t)rd, i->rn = (uint8_t)rn;
	i->imm = imm;
	if (rn == A64_REGISTER_FRAME)
		a64_record_slot_tag(e, i, imm, -1);	/* an address: may point just past the end */
}

static inline void a64_record_emit_bitfield(codegen_context *e, int op, int x, int rd, int rn, int immr, int imms)
{
	a64_record *i;

	if (!e->is_reachable)
		return;
	i = a64_record_emit(e, op);
	i->sz = (uint8_t)x, i->rd = (uint8_t)rd, i->rn = (uint8_t)rn;
	i->imm = immr, i->sh = (uint8_t)imms;
}

static inline void a64_record_emit_move_immediate(codegen_context *e, int x, int rd, int64_t v) { a64_record_emit_immediate(e, A64_OP_MOVI, x, rd, 0, v); }
static inline void a64_record_emit_move(codegen_context *e, int x, int rd, int rn) { if (rd != rn) a64_record_emit_three_registers(e, A64_OP_MOV, x, rd, rn, 0); }

static inline void a64_record_emit_condition_set(codegen_context *e, int rd, int cond)
{
	a64_record *i;

	if (!e->is_reachable)
		return;
	i = a64_record_emit(e, A64_OP_CSINC);
	i->sz = 0, i->rd = (uint8_t)rd, i->rn = ZR, i->rm = ZR, i->cond = (uint8_t)(cond ^ 1);
}

/* the last record is `cset v.r, cond` (nothing since, no label: the flags still say it): cond, else -1.
 * The value is then 0/1 already, and a test of it can use the flags. */
static inline int a64_record_condition_set_last(codegen_context *e, const codegen_value *v)
{
	const a64_record *i;

	if (v->kind != CODEGEN_VALUE_REGISTER || !e->is_reachable || !e->record_count || e->unit->options->opt < 1)
		return -1;
	i = &e->records[e->record_count - 1];
	if (i->op != A64_OP_CSINC || i->rn != ZR || i->rm != ZR || i->rd != v->register_number)
		return -1;
	return i->cond ^ 1;
}

static inline void a64_record_emit_memory(codegen_context *e, int op, int sz, int ext, int rd, int base, int64_t off)
{
	a64_record *i;

	if (!e->is_reachable)
		return;
	i = a64_record_emit(e, op);
	i->sz = (uint8_t)sz, i->cond = (uint8_t)ext, i->rd = (uint8_t)rd, i->rn = (uint8_t)base;
	i->imm = off;
	if (base == A64_REGISTER_FRAME)
		a64_record_slot_tag(e, i, off, (int64_t)1 << sz);
}

static inline void a64_record_emit_symbol(codegen_context *e, int op, int sz, int ext, int rd, int rn, uint32_t ref, int64_t off)
{
	a64_record *i;

	if (!e->is_reachable)
		return;
	i = a64_record_emit(e, op);
	i->sz = (uint8_t)sz, i->cond = (uint8_t)ext, i->rd = (uint8_t)rd, i->rn = (uint8_t)rn;
	i->ref = ref, i->off = (int32_t)off;
}

/* ---------------- floating point ---------------- */

/*
 * float and double values live in general registers as their bit patterns
 * (a float zero-extended); an operation moves them to the scratch FP/SIMD
 * registers v16-v18 with fmov, works there and moves the result back.  So
 * registers, spills, loads, stores and calls stay integer code.  Constants
 * are folded with the host's IEEE arithmetic.  long double is double on
 * Darwin; _Float16 and 128-bit long double are not supported.
 */
static inline void a64_record_emit_word(codegen_context *e, uint32_t w)
{
	if (e->is_reachable)
		a64_record_emit(e, A64_OP_WORD)->imm = w;
}

static inline uint32_t a64_word_fmov_to_vector(int d, int vr, int r) { return (d ? 0x9e670000u : 0x1e270000u) | (unsigned)r << 5 | (unsigned)vr; }	/* fmov v, x/w */
static inline uint32_t a64_word_fmov_from_vector(int d, int r, int vr) { return (d ? 0x9e660000u : 0x1e260000u) | (unsigned)vr << 5 | (unsigned)r; }	/* fmov x/w, v */

/* fmov v<vr>.d[1], x<r> / fmov x<r>, v<vr>.d[1] */
static inline uint32_t a64_word_fmov_high_to_vector(int vr, int r) { return 0x9eaf0000u | (unsigned)r << 5 | (unsigned)vr; }
static inline uint32_t a64_word_fmov_high_from_vector(int r, int vr) { return 0x9eae0000u | (unsigned)vr << 5 | (unsigned)r; }

/* Address-valued arbitrary-width integers, canonical little-endian limbs. */
VPRIV codegen_value a64_bigconst(codegen_context *e, ast_qualified_type t, const uint32_t *words, i128 small);
VPRIV codegen_value a64_bigbin(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b);
VPRIV codegen_value a64_bigcmp(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b);
VPRIV codegen_value a64_bigcvt(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to);
VPRIV codegen_value a64_bigbool(codegen_context *e, codegen_value v, ast_qualified_type t);
VPRIV codegen_value a64_bigcopy(codegen_context *e, codegen_value v, ast_qualified_type t);

/*
 * VCC_REC_STATS: what each record clean-up pass of finish_function does, per TU, for the
 * functions that came through VIR's lowering and through the direct emitter (opt.c rp_snap,
 * rp_count, rp_print).  The names are those of doc/quality/VIR-MIGRATION.md's table.
 */
enum {
	A64_RECORD_PASS_FUNCTION,	/* the functions that reached the clean-ups (-O1 and up) */
	A64_RECORD_PASS_UNREACHABLE, A64_RECORD_PASS_FALLTHROUGH, A64_RECORD_PASS_LIVENESS, A64_RECORD_PASS_SPILL_PAIRS, A64_RECORD_PASS_COPIES_FIRST, A64_RECORD_PASS_SLOTS, A64_RECORD_PASS_EXTENSIONS,
	A64_RECORD_PASS_COPIES_SECOND, A64_RECORD_PASS_DEAD, A64_RECORD_PASS_FOLD, A64_RECORD_PASS_FLAGS, A64_RECORD_PASS_REGISTERS_USED, A64_RECORD_PASS_JUMP_TABLES_EXPAND, A64_RECORD_PASS_FRAMELESS,
	A64_RECORD_PASS_TAIL_EXPAND, A64_RECORD_PASS_FRAME_REWRITE, A64_RECORD_PASS_TEST_BRANCH_RELAX,
	A64_RECORD_PASS_COUNT
};
VPRIV void rp_snap(codegen_context *e);
VPRIV void rp_count(codegen_context *e, int pass);
VPRIV void rp_print(codegen_unit *E);
/* a counter increment behind the statistics flag: without the switch, one untaken branch */
#define A64_RECORD_PASS_BEGIN(e) do { if (unlikely((e)->unit->record_statistics)) rp_snap(e); } while (0)
#define A64_RECORD_PASS_END(e, p) do { if (unlikely((e)->unit->record_statistics)) rp_count(e, p); } while (0)
