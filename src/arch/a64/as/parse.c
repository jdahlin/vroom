/* parse.c: arm64 instructions in GNU as syntax, the front half of the assembler's arm64 side: registers
 * and .req aliases, operands (registers with arrangements, immediates, memory, shifts and extends),
 * literal pools for ldr =value, and a64_as_instruction_assemble, which parses one statement into
 * A64Op records and hands them to the encoder in enc.c.
 *
 * Called by as.c's statement loop for every non-directive statement when the object's machine is arm64.
 * A symbol or label operand becomes a fixup recorded in the ElfObj (never a number here), so
 * relaxation and relocation stay in the object.  Pending literal pools live in the context
 * (a->literal_pools) and are written by .ltorg (a64_as_pools_ltorg) or at as_end (a64_as_pools_flush).  A malformed statement fails the assembly through as_error_report. */
#include "../../../as/as-internal.h"

/* ---------------------------------------------------------------- instructions */

VPRIV void as_alias_rehash(as_context *a)
{
	memset(a->alias_buckets, 0, sizeof a->alias_buckets);
	for (uint32_t i = 0; i < a->alias_count; i++) {
		uint32_t h = as_alias_hash(a->aliases[i].name.text, a->aliases[i].name.length);
		a->aliases[i].older = a->alias_buckets[h]; a->alias_buckets[h] = i + 1;
	}
}

static int reg_alias(as_context *a, const char *s, size_t n, as_text *out)
{
	if (!a->alias_count) return 0;
	for (uint32_t i = a->alias_buckets[as_alias_hash(s, n)]; i; i = a->aliases[i - 1].older) {
		const as_register_alias *x = &a->aliases[i - 1];
		if (x->name.length == n && as_char_to_lower((unsigned char)x->name.text[0]) == as_char_to_lower((unsigned char)s[0]) &&
		    !strncasecmp(x->name.text, s, n)) { *out = x->register_name; return 1; }
	}
	return 0;
}

static int parse_regnum(const char *s, size_t n, int max)
{
	if (!n || n > 2) return -1;
	int v = 0;
	for (size_t i = 0; i < n; i++) { if (!as_char_is_digit((unsigned char)s[i])) return -1; v = v * 10 + s[i] - '0'; }
	if (n == 2 && s[0] == '0') return -1;
	return v <= max ? v : -1;
}

static int arrangement(const char *s, size_t n)
{
	char b[8];
	if (n >= sizeof b) return -1;
	for (size_t i = 0; i < n; i++) b[i] = (char)as_char_to_lower((unsigned char)s[i]);
	b[n] = 0;
	static const char *t[] = { "", "8b", "16b", "4h", "8h", "2s", "4s", "1d", "2d", "1q", "b", "h", "s", "d", "q" };
	for (int i = 1; i < 15; i++) if (!strcmp(b, t[i])) return i;
	return -1;
}

/* Try to parse a register at p (identifier [s, s+n)); fill op. */
static int parse_reg(as_context *a, const char *s, size_t n, A64Op *op, int depth)
{
	as_text al;
	const char *dot = 0;
	for (size_t i = 0; i < n; i++) if (s[i] == '.') { dot = s + i; break; }
	size_t bn = dot ? (size_t)(dot - s) : n;
	if (depth < 4 && reg_alias(a, s, bn, &al)) {
		char buf[64];
		if (al.length + (n - bn) >= sizeof buf) return 0;
		memcpy(buf, al.text, al.length); memcpy(buf + al.length, s + bn, n - bn);
		return parse_reg(a, buf, al.length + (n - bn), op, depth + 1);
	}
	char c = (char)as_char_to_lower((unsigned char)s[0]);
	memset(op, 0, sizeof *op);
	if (!dot) {
		int r, cl;
		switch (c) {	/* x0, w12, q1, v3 ... */
		case 'x': case 'w':
			if ((r = parse_regnum(s + 1, n - 1, 30)) >= 0) {
				op->kind = A64O_REG; op->cls = c == 'x' ? A64_X : A64_W; op->reg = (uint8_t)r; return 1;
			}
			break;
		case 'q': cl = A64_Q; goto fpr;
		case 'd': cl = A64_D; goto fpr;
		case 's': cl = A64_S; goto fpr;
		case 'h': cl = A64_H; goto fpr;
		case 'b': cl = A64_B;
		fpr:
			if ((r = parse_regnum(s + 1, n - 1, 31)) >= 0) { op->kind = A64O_REG; op->cls = (uint8_t)cl; op->reg = (uint8_t)r; return 1; }
			break;
		case 'v':
			if ((r = parse_regnum(s + 1, n - 1, 31)) >= 0) {
				op->kind = A64O_VREG; op->cls = A64_V; op->reg = (uint8_t)r; op->arr = A64_ARR_NONE; return 1;
			}
			break;
		case 'z': case 'p':
			if ((r = parse_regnum(s + 1, n - 1, c == 'z' ? 31 : 15)) >= 0) {
				op->kind = A64O_REG; op->cls = c == 'z' ? A64_Z : A64_P; op->reg = (uint8_t)r; return 1;
			}
			break;
		}
		if (n == 2 || n == 3) {	/* sp wsp xzr wzr fp lr */
			uint32_t k = (uint32_t)c << 16 | (uint32_t)as_char_to_lower((unsigned char)s[1]) << 8 | (n == 3 ? (uint32_t)as_char_to_lower((unsigned char)s[2]) : 0);
#define K3(x, y, z) ((uint32_t)(x) << 16 | (uint32_t)(y) << 8 | (uint32_t)(z))
			switch (k) {
			case K3('s', 'p', 0): op->kind = A64O_REG; op->cls = A64_X; op->reg = 31; op->sp = 1; return 1;
			case K3('w', 's', 'p'): op->kind = A64O_REG; op->cls = A64_W; op->reg = 31; op->sp = 1; return 1;
			case K3('x', 'z', 'r'): op->kind = A64O_REG; op->cls = A64_X; op->reg = 31; return 1;
			case K3('w', 'z', 'r'): op->kind = A64O_REG; op->cls = A64_W; op->reg = 31; return 1;
			case K3('f', 'p', 0): op->kind = A64O_REG; op->cls = A64_X; op->reg = 29; return 1;
			case K3('l', 'r', 0): op->kind = A64O_REG; op->cls = A64_X; op->reg = 30; return 1;
			}
#undef K3
		}
		return 0;
	}
	if (c == 'p' || c == 'z') {	/* SVE: p0.b, z1.d */
		int r = parse_regnum(s + 1, bn - 1, c == 'z' ? 31 : 15);
		int ar = r < 0 ? -1 : arrangement(dot + 1, n - bn - 1);
		if (ar < A64_ELB || ar > A64_ELQ) return 0;
		op->kind = A64O_REG; op->cls = c == 'z' ? A64_Z : A64_P; op->reg = (uint8_t)r; op->arr = (uint8_t)ar;
		return 1;
	}
	if (c != 'v') return 0;
	int r = parse_regnum(s + 1, bn - 1, 31);
	if (r < 0) return 0;
	int ar = arrangement(dot + 1, n - bn - 1);
	if (ar < 0) return 0;
	op->kind = A64O_VREG; op->cls = A64_V; op->reg = (uint8_t)r; op->arr = (uint8_t)ar;
	return 1;
}

static int shift_kind(const char *s, size_t n)
{
	if (n != 3 && n != 4) return -1;
	uint32_t k = 0;
	for (size_t i = 0; i < n; i++) k |= (uint32_t)as_char_to_lower((unsigned char)s[i]) << (8 * i);
#define P(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
	switch (k) {	/* the order of A64_LSL ... A64_SXTX */
	case P('l', 's', 'l', 0): return 0;
	case P('l', 's', 'r', 0): return 1;
	case P('a', 's', 'r', 0): return 2;
	case P('r', 'o', 'r', 0): return 3;
	case P('m', 's', 'l', 0): return 4;
	case P('u', 'x', 't', 'b'): return 5;
	case P('u', 'x', 't', 'h'): return 6;
	case P('u', 'x', 't', 'w'): return 7;
	case P('u', 'x', 't', 'x'): return 8;
	case P('s', 'x', 't', 'b'): return 9;
	case P('s', 'x', 't', 'h'): return 10;
	case P('s', 'x', 't', 'w'): return 11;
	case P('s', 'x', 't', 'x'): return 12;
	}
#undef P
	return -1;
}

static int mod_kind(const char *s, size_t n)
{
	static const struct { const char *n; int k; } t[] = {
		{ "lo12", A64_MOD_LO12 }, { "pg_hi21", A64_MOD_PG_HI21 }, { "pg_hi21_nc", A64_MOD_PG_HI21_NC },
		{ "abs_g0", A64_MOD_ABS_G0 }, { "abs_g0_nc", A64_MOD_ABS_G0_NC }, { "abs_g1", A64_MOD_ABS_G1 },
		{ "abs_g1_nc", A64_MOD_ABS_G1_NC }, { "abs_g2", A64_MOD_ABS_G2 }, { "abs_g2_nc", A64_MOD_ABS_G2_NC },
		{ "abs_g3", A64_MOD_ABS_G3 }, { "got", A64_MOD_GOT }, { "got_lo12", A64_MOD_GOT_LO12 },
		{ "abs_g0_s", A64_MOD_ABS_G0_S }, { "abs_g1_s", A64_MOD_ABS_G1_S }, { "abs_g2_s", A64_MOD_ABS_G2_S },
		{ "tlsdesc", A64_MOD_TLSDESC }, { "tlsdesc_lo12", A64_MOD_TLSDESC_LO12 },
		{ "gottprel", A64_MOD_GOTTPREL }, { "gottprel_lo12", A64_MOD_GOTTPREL_LO12_NC },
		{ "gottprel_g1", A64_MOD_GOTTPREL_G1 }, { "gottprel_g0_nc", A64_MOD_GOTTPREL_G0_NC },
		{ "tprel_g2", A64_MOD_TPREL_G2 }, { "tprel_g1", A64_MOD_TPREL_G1 }, { "tprel_g1_nc", A64_MOD_TPREL_G1_NC },
		{ "tprel_g0", A64_MOD_TPREL_G0 }, { "tprel_g0_nc", A64_MOD_TPREL_G0_NC },
		{ "tprel_hi12", A64_MOD_TPREL_HI12 }, { "tprel_lo12", A64_MOD_TPREL_LO12 }, { "tprel_lo12_nc", A64_MOD_TPREL_LO12_NC },
		{ "dtprel_g2", A64_MOD_DTPREL_G2 }, { "dtprel_g1", A64_MOD_DTPREL_G1 }, { "dtprel_g1_nc", A64_MOD_DTPREL_G1_NC },
		{ "dtprel_g0", A64_MOD_DTPREL_G0 }, { "dtprel_g0_nc", A64_MOD_DTPREL_G0_NC },
		{ "dtprel_hi12", A64_MOD_DTPREL_HI12 }, { "dtprel_lo12", A64_MOD_DTPREL_LO12 }, { "dtprel_lo12_nc", A64_MOD_DTPREL_LO12_NC },
		{ "prel_g3", A64_MOD_PREL_G3 }, { "prel_g2", A64_MOD_PREL_G2 }, { "prel_g2_nc", A64_MOD_PREL_G2_NC },
		{ "prel_g1", A64_MOD_PREL_G1 }, { "prel_g1_nc", A64_MOD_PREL_G1_NC },
		{ "prel_g0", A64_MOD_PREL_G0 }, { "prel_g0_nc", A64_MOD_PREL_G0_NC },
		{ "vcc_tlvp", A64_MOD_TLVP }, { "vcc_tlvp_lo12", A64_MOD_TLVP_LO12 },
	};
	for (size_t i = 0; i < sizeof t / sizeof t[0]; i++) if (as_text_equals_ignore_case(s, n, t[i].n)) return t[i].k;
	return -1;
}

/* immediate / expression operand, optionally with :mod: */
static int parse_imm(as_context *a, const char **pp, const char *e, A64Op *op)
{
	const char *p = as_whitespace_skip(*pp, e);
	op->kind = A64O_IMM;
	if (p < e && *p == '#') { op->hash = 1; p = as_whitespace_skip(p + 1, e); }
	if (p < e && *p == ':') {
		const char *s = ++p;
		while (p < e && *p != ':') p++;
		int m = mod_kind(s, (size_t)(p - s));
		if (m < 0 || p >= e) return as_error_report(a, "invalid relocation specifier");
		op->mod = (uint8_t)m;
		p++;
	}
	/* floating-point immediate */
	{
		const char *q = p;
		if (q < e && (*q == '-' || *q == '+')) q++;
		const char *d = q;
		while (q < e && as_char_is_digit((unsigned char)*q)) q++;
		if (q > d && q < e && (*q == '.' || ((*q == 'e' || *q == 'E') && !(d[0] == '0' && q - d == 1)))) {
			if (*q == '.' || *q == 'e' || *q == 'E') {
				char tmp[64]; size_t k = (size_t)(e - p) < 63 ? (size_t)(e - p) : 63;
				memcpy(tmp, p, k); tmp[k] = 0;
				char *end;
				op->fimm = strtod(tmp, &end);
				op->kind = A64O_FIMM;
				*pp = p + (end - tmp);
				return 0;
			}
		}
		/* a plain decimal number on its own: what expr() would fold it to */
		const char *r = as_whitespace_skip(q, e);
		if (d == p && q > d && q - d <= 18 && !(d[0] == '0' && q - d > 1) && (r == e || *r == ',' || *r == ']')) {
			int64_t x = 0;
			for (const char *t = d; t < q; t++) x = x * 10 + (*t - '0');
			op->v = (as_value){ 0, 0, x };
			op->imm = x;
			op->hassym = 0;
			*pp = q;
			return 0;
		}
	}
	as_value v;
	if (as_expression_value_parse(a, &p, e, &v)) return -1;
	as_value_fold(a, &v);
	op->v = v;
	op->imm = v.c;
	op->hassym = v.add || v.sub;
	*pp = p;
	return 0;
}

static int parse_mem(as_context *a, const char **pp, const char *e, A64Op *op)
{
	const char *p = as_whitespace_skip(*pp + 1, e);
	const char *s = p;
	while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
	A64Op r;
	if (!parse_reg(a, s, (size_t)(p - s), &r, 0) || r.kind != A64O_REG || (r.cls != A64_X && !(r.cls == A64_Z && r.arr)))
		return as_error_report(a, "invalid operand for instruction");
	memset(op, 0, sizeof *op);
	op->kind = A64O_MEM; op->reg = r.reg; op->mode = A64M_OFF;
	if (r.cls == A64_Z) { op->cls = A64_Z; op->arr = r.arr; }	/* SVE [Zn.T{, #imm}] */
	p = as_whitespace_skip(p, e);
	if (p < e && *p == ',') {
		p = as_whitespace_skip(p + 1, e);
		s = p;
		while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
		if (p > s && parse_reg(a, s, (size_t)(p - s), &r, 0) && r.kind == A64O_REG) {
			op->mode = A64M_REG; op->index = r.reg; op->idxcls = r.cls; op->shift = A64_LSL;
			if (r.cls == A64_Z) op->idx = r.arr;	/* SVE [Xn, Zm.T{, ext}] */
			p = as_whitespace_skip(p, e);
			if (p < e && *p == ',') {
				p = as_whitespace_skip(p + 1, e);
				s = p;
				while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
				int k = shift_kind(s, (size_t)(p - s));
				if (k < 0) return as_error_report(a, "expected extend or shift");
				op->shift = (uint8_t)k;
				op->hasamt = 2;		/* extend given */
				p = as_whitespace_skip(p, e);
				if (p < e && *p == '#') {
					int64_t amt;
					p++;
					if (as_expression_absolute_parse(a, &p, e, &amt)) return -1;
					op->imm = amt; op->hasamt = 3;
				}
			}
		} else {
			p = s;
			A64Op im; memset(&im, 0, sizeof im);
			if (parse_imm(a, &p, e, &im)) return -1;
			op->imm = im.imm; op->v = im.v; op->hassym = im.hassym; op->mod = im.mod; op->hasamt = 1;
			p = as_whitespace_skip(p, e);
			if (p < e && *p == ',') {	/* SVE: #imm, mul vl */
				const char *q = as_whitespace_skip(p + 1, e);
				if (e - q >= 3 && as_text_equals_ignore_case(q, 3, "mul")) {
					q = as_whitespace_skip(q + 3, e);
					if (e - q >= 2 && as_text_equals_ignore_case(q, 2, "vl")) { op->mulvl = 1; p = q + 2; }
				}
			}
		}
	}
	p = as_whitespace_skip(p, e);
	if (p >= e || *p != ']') return as_error_report(a, "expected ']'");
	p = as_whitespace_skip(p + 1, e);
	if (p < e && *p == '!') { op->mode = A64M_PRE; p++; }
	*pp = p;
	return 0;
}

/* operand nop of mnemonic mn (lowercase) can only be a label or address */
static int label_pos(const char *mn, size_t n, int nop, const A64Op *op)
{
	(void)op;
	if (nop == 2 && ((n == 3 && !memcmp(mn, "adr", 3)) || (n == 4 && !memcmp(mn, "adrp", 4))))
		return 1;
	if (nop == 1 && ((n == 1 && mn[0] == 'b') || (n == 2 && !memcmp(mn, "bl", 2)) || (n >= 3 && mn[0] == 'b' && mn[1] == '.')))
		return 1;
	if (nop == 2 && ((n == 3 && !memcmp(mn, "cbz", 3)) || (n == 4 && !memcmp(mn, "cbnz", 4))))
		return 1;
	if (nop == 3 && ((n == 3 && !memcmp(mn, "tbz", 3)) || (n == 4 && !memcmp(mn, "tbnz", 4))))
		return 1;
	return 0;
}

/* mnemonics whose operands can be names (sysregs, barriers, conditions...);
 * m is lowercase */
static int names_ops(const char *m, size_t n)
{
#define N(s) (n == sizeof(s) - 1 && !memcmp(m, s, n))
	switch (m[0]) {
	case 'a': return N("at");
	case 'b': return N("bti");
	case 'c': return N("csel") || N("csinc") || N("csinv") || N("csneg") || N("cset") || N("csetm") ||
			 N("cinc") || N("cinv") || N("cneg") || N("ccmp") || N("ccmn") || N("clrex");
	case 'd': return N("dmb") || N("dsb") || N("dc");
	case 'f': return N("fcsel") || N("fccmp") || N("fccmpe");
	case 'h': return N("hint");
	case 'i': return N("isb") || N("ic");
	case 'm': return N("msr") || N("mrs");
	case 'p': return N("prfm") || N("prfum") || N("psb") || N("prfb") || N("prfh") || N("prfw") || N("prfd");
	case 's': return N("sys") || N("sysl") || N("sb");
	case 't': return N("tlbi") || N("tsb");
	}
	return 0;
#undef N
}

typedef struct PoolS { uint32_t sec, label; as_value v; uint32_t size; } Pool;

static uint32_t pool_entry(as_context *a, as_value v, uint32_t size)
{
	uint32_t sec = elf_frag(a->object, a->fragment_current)->sec;
	int plain = (v.add && !v.sub && !v.c) || (!v.add && !v.sub);
	if (plain)
		for (uint32_t i = 0; i < a->literal_pool_count; i++) {
			Pool *q = &a->literal_pools[i];
			if (q->sec == sec && q->size == size && q->v.add == v.add && q->v.sub == v.sub && q->v.c == v.c) return q->label;
		}
	if (a->literal_pool_count == a->literal_pool_capacity) {
		uint32_t nc = a->literal_pool_capacity ? a->literal_pool_capacity * 2 : 64;
		Pool *t = as_region_alloc(&a->definitions, sizeof *t * nc);
		if (a->literal_pool_count) memcpy(t, a->literal_pools, sizeof *t * a->literal_pool_count);
		a->literal_pools = t; a->literal_pool_capacity = nc;
	}
	Pool *q = &a->literal_pools[a->literal_pool_count++];
	q->sec = sec; q->label = as_symbol_temporary(a); q->v = v; q->size = size;
	return q->label;
}

/* emitAll: per section in first-use order, align naturally, label, value */
VPRIV int a64_as_pools_flush(as_context *a)
{
	for (uint32_t i = 0; i < a->literal_pool_count; i++) {
		uint32_t sec = a->literal_pools[i].sec;
		int seen = 0;
		for (uint32_t j = 0; j < i; j++) if (a->literal_pools[j].sec == sec) seen = 1;
		if (seen) continue;
		a->fragment_current = sec;
		for (uint32_t j = i; j < a->literal_pool_count; j++) {
			Pool *q = &a->literal_pools[j];
			if (q->sec != sec) continue;
			elf_align(a->object, sec, q->size == 8 ? 3 : 2, 0, 0, 0);
			elf_define(a->object, q->label, sec, elf_frag(a->object, sec)->size);
			as_value v = q->v;
			if (as_value_emit(a, (int)q->size, &v)) return -1;
		}
	}
	a->literal_pool_count = 0;
	return 0;
}

/* .ltorg: the current section's literal pool, here */
VPRIV int a64_as_pools_ltorg(as_context *a)
{
	uint32_t sec = elf_frag(a->object, a->fragment_current)->sec, k = 0;
	for (uint32_t j = 0; j < a->literal_pool_count; j++) {
		Pool *q = &a->literal_pools[j];
		if (q->sec != sec) { a->literal_pools[k++] = *q; continue; }
		elf_align(a->object, a->fragment_current, q->size == 8 ? 3 : 2, 0, 0, 0);
		elf_define(a->object, q->label, a->fragment_current, elf_frag(a->object, a->fragment_current)->size);
		as_value v = q->v;
		if (as_value_emit(a, (int)q->size, &v)) return -1;
	}
	a->literal_pool_count = k;
	return 0;
}

int as_trace_level;	/* print each instruction statement and its encoding (testing) */

/* p: the statement from its mnemonic, which is wn bytes long */
VPRIV int a64_as_instruction_assemble(as_context *a, const char *p, const char *e, size_t wn)
{
	const char *p0 = p;
	const char *s = p;
	p += wn;
	char mn[32];
	size_t mlen = wn;
	if (mlen >= sizeof mn) return as_error_report(a, "invalid instruction mnemonic");
	for (size_t i = 0; i < mlen; i++) mn[i] = (char)as_char_to_lower((unsigned char)s[i]);
	mn[mlen] = 0;
	int names = names_ops(mn, mlen);
	A64Op ops[AS_OPERANDS_MAX];
	int nop = 0;
	uint32_t w;
	A64Fix fx; fx.type = 0;
	p = as_whitespace_skip(p, e);
	/* SME ldr/str za[Wv, #imm], [Xn{, #imm, mul vl}] */
	if (mlen == 3 && (!memcmp(mn, "ldr", 3) || !memcmp(mn, "str", 3)) && e - p > 3 && (p[0] | 32) == 'z' && (p[1] | 32) == 'a' && p[2] == '[') {
		char buf[96], tail[16] = "";
		unsigned v, i1, n, i2 = 0;
		size_t l = (size_t)(e - p) < sizeof buf - 1 ? (size_t)(e - p) : sizeof buf - 1;
		memcpy(buf, p, l); buf[l] = 0;
		for (char *q = buf; *q; q++) *q = (char)as_char_to_lower((unsigned char)*q);
		int k = sscanf(buf, "za[w%u , #%u ] , [x%u , #%u , mul vl%15s", &v, &i1, &n, &i2, tail);
		if (k < 3 && sscanf(buf, "za[w%u , %u ] , [x%u", &v, &i1, &n) == 3) k = 3;
		if (k < 3 || v < 12 || v > 15 || i1 > 15 || n > 30 || (k >= 4 && i2 != i1))
			return as_error_report(a, "invalid za operand");
		w = (mn[0] == 'l' ? 0xe1000000u : 0xe1200000u) | (v - 12) << 13 | n << 5 | i1;
		goto put;
	}
	while (p < e) {
		if (nop >= AS_OPERANDS_MAX) return as_error_report(a, "too many operands");
		A64Op *op = &ops[nop++];
		memset(op, 0, sizeof *op);
		char c = *p;
		if (c == '[') {
			if (parse_mem(a, &p, e, op)) return -1;
		} else if (c == '{') {
			p = as_whitespace_skip(p + 1, e);
			op->kind = A64O_LIST;
			int first = 1;
			for (;;) {
				const char *t = p;
				while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
				A64Op r;
				if (!parse_reg(a, t, (size_t)(p - t), &r, 0) || !(r.kind == A64O_VREG || (r.kind == A64O_REG && r.cls == A64_Z && r.arr))) return as_error_report(a, "vector register expected");
				if (first) { op->reg = r.reg; op->arr = r.arr; op->cls = r.kind == A64O_REG ? A64_Z : A64_V; op->nregs = 1; first = 0; }
				else op->nregs++;
				p = as_whitespace_skip(p, e);
				if (p < e && *p == '-') {
					p = as_whitespace_skip(p + 1, e);
					t = p;
					while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
					A64Op r2;
					if (!parse_reg(a, t, (size_t)(p - t), &r2, 0) || r2.kind != (op->cls == A64_Z ? A64O_REG : A64O_VREG)) return as_error_report(a, "vector register expected");
					op->nregs = (uint8_t)(((r2.reg - op->reg) & 31) + 1);
					p = as_whitespace_skip(p, e);
				}
				if (p < e && *p == ',') { p = as_whitespace_skip(p + 1, e); continue; }
				break;
			}
			if (p >= e || *p != '}') return as_error_report(a, "'}' expected");
			p = as_whitespace_skip(p + 1, e);
			if (p < e && *p == '[') {
				int64_t ix;
				p++;
				if (as_expression_absolute_parse(a, &p, e, &ix)) return -1;
				p = as_whitespace_skip(p, e);
				if (p >= e || *p != ']') return as_error_report(a, "']' expected");
				p++;
				op->idx = (uint8_t)ix; op->hasidx = 1;
			}
		} else if (as_char_class[(uint8_t)c] & AS_CHAR_OPERAND_START) {	/* # : ( - + ~ digit */
			if (parse_imm(a, &p, e, op)) return -1;
		} else if (as_char_is_identifier_start((unsigned char)c)) {
			const char *t = p;
			while (p < e && as_char_is_identifier((unsigned char)*p)) p++;
			size_t tn = (size_t)(p - t);
			int k;
			const char *q = as_whitespace_skip(p, e);
			if (parse_reg(a, t, tn, op, 0) && label_pos(mn, mlen, nop, op)) {
				/* a symbol named like a register (v1, x1, q0): adr/adrp take
				 * it as the symbol, branches an SVE name (z3, p1: LLVM) and,
				 * beyond LLVM, any register name (a C function d1 or x1) */
				memset(op, 0, sizeof *op);
				p = t;
				if (parse_imm(a, &p, e, op)) return -1;
			} else if (op->kind) {
				if (op->kind == A64O_REG && q < e && *q == '!') { op->wb = 1; p = q + 1; }
				else if (op->kind == A64O_REG && op->cls == A64_P && q + 1 < e && *q == '/' && ((q[1] | 32) == 'z' || (q[1] | 32) == 'm')) {
					op->pq = (q[1] | 32) == 'z' ? 1 : 2; p = q + 2;
				}
				else if (op->kind == A64O_REG && op->cls == A64_Z && op->arr && q < e && *q == '[') {	/* SVE Zn.T[i] */
					int64_t ix;
					q++;
					if (as_expression_absolute_parse(a, &q, e, &ix)) return -1;
					q = as_whitespace_skip(q, e);
					if (q >= e || *q != ']' || ix < 0 || ix > 63) return as_error_report(a, "']' expected");
					op->idx = (uint8_t)ix; op->hasidx = 1;
					p = q + 1;
				}
				else if (op->kind == A64O_VREG && q < e && *q == '[') {
					int64_t ix;
					q++;
					if (as_expression_absolute_parse(a, &q, e, &ix)) return -1;
					q = as_whitespace_skip(q, e);
					if (q >= e || *q != ']') return as_error_report(a, "']' expected");
					op->kind = A64O_VELEM; op->idx = (uint8_t)ix; op->hasidx = 1;
					p = q + 1;
				}
			} else if ((k = shift_kind(t, tn)) >= 0 && (q >= e || *q == '#' || *q == ',' || *q == '(' || as_char_is_digit((unsigned char)*q))) {
				op->kind = A64O_SHIFT; op->shift = (uint8_t)k;
				p = q;
				if (p < e && *p != ',') {
					int64_t amt;
					if (*p == '#') p++;
					if (as_expression_absolute_parse(a, &p, e, &amt)) return -1;
					op->imm = amt; op->hasamt = 1;
				}
			} else if (tn == 3 && as_text_equals_ignore_case(t, 3, "mul") && q < e && *q == '#') {	/* SVE: mul #imm */
				int64_t amt;
				p = q + 1;
				if (as_expression_absolute_parse(a, &p, e, &amt)) return -1;
				op->kind = A64O_SHIFT; op->shift = A64_MUL; op->imm = amt; op->hasamt = 1;
			} else if ((names || (nop >= 2 && a64_svepat(t, tn) >= 0)) && (q >= e || *q == ',')) {
				op->kind = A64O_NAME; op->name = t; op->nlen = (uint32_t)tn;
			} else {
				p = t;
				if (parse_imm(a, &p, e, op)) return -1;
			}
		} else if (c == '=') {
			p++;
			if (parse_imm(a, &p, e, op)) return -1;
			op->kind = A64O_IMM; op->hash = 2;	/* =expr */
		} else if (c == '"' || c == '.') {
			if (parse_imm(a, &p, e, op)) return -1;
		} else return as_error_report(a, "unexpected token in operand");
		p = as_whitespace_skip(p, e);
		if (p >= e) break;
		if (*p != ',') return as_error_report(a, "unexpected token in argument list");
		p = as_whitespace_skip(p + 1, e);
	}
	/* ldr Rt, =expr: movz if it fits, else a constant-pool entry */
	if (nop == 2 && ops[1].kind == A64O_IMM && ops[1].hash == 2 && !strcmp(mn, "ldr") && ops[0].kind == A64O_REG) {
		int x = ops[0].cls == A64_X;
		if (!ops[1].hassym) {
			uint64_t imm = (uint64_t)ops[1].imm;
			unsigned sh = 0, maxsh = x ? 48 : 16;
			while (imm > 0xffff && __builtin_ctzll(imm) >= 16) { sh += 16; imm >>= 16; }
			if (sh <= maxsh && imm <= 0xffff) {
				memcpy(mn, "movz", 5); mlen = 4;
				ops[1].imm = (int64_t)imm; ops[1].v.c = (int64_t)imm; ops[1].hash = 1;
				if (sh) { memset(&ops[2], 0, sizeof ops[2]); ops[2].kind = A64O_SHIFT; ops[2].shift = A64_LSL; ops[2].imm = sh; ops[2].hasamt = 1; nop = 3; }
				goto enc;
			}
		}
		uint32_t lab = pool_entry(a, ops[1].v, x ? 8 : 4);
		ops[1].v = (as_value){ lab, 0, 0 }; ops[1].hassym = 1; ops[1].hash = 0; ops[1].imm = 0;
	}
enc:;
	const char *msg = 0;
	if (a64_asm(mn, mlen, ops, nop, &w, &fx, &msg)) {
		if (as_trace_level != 2)
			return as_error_report(a, "%s: %s", mn, msg ? msg : "invalid instruction");
		/* --trace-all: report and go on (finding every unsupported form at once) */
		printf("FAIL %s\t%.*s\n", msg ? msg : "invalid", (int)(e - p0), p0);
		w = 0, fx.type = 0;
	}
put:;
	ElfObj *o = a->object;
	elf_code(o, a->fragment_current);
	uint64_t off = elf_frag(o, a->fragment_current)->size;
	uint8_t *q = elf_grow(o, a->fragment_current, 4);
	q[0] = (uint8_t)w; q[1] = (uint8_t)(w >> 8); q[2] = (uint8_t)(w >> 16); q[3] = (uint8_t)(w >> 24);
	if (fx.type) elf_fixup(o, a->fragment_current, off, q, fx.type, fx.v);
	if (as_trace_level) printf("%08x %d\t%.*s\n", w, fx.type != 0, (int)(e - p0), p0);
	return 0;
}

