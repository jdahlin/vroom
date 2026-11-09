/* SVE/SVE2 instructions (#included by enc.c before a64_asm): the forms
 * clang generates.  k_sve() returns 1 when the mnemonic/operands are not
 * an SVE form it knows, so a64_asm goes on with its tables. */

int a64_svepat(const char *s, size_t n)
{
	static const struct { const char *n; uint8_t v; } t[] = {
		{ "pow2", 0 }, { "vl1", 1 }, { "vl2", 2 }, { "vl3", 3 }, { "vl4", 4 }, { "vl5", 5 },
		{ "vl6", 6 }, { "vl7", 7 }, { "vl8", 8 }, { "vl16", 9 }, { "vl32", 10 }, { "vl64", 11 },
		{ "vl128", 12 }, { "vl256", 13 }, { "mul4", 29 }, { "mul3", 30 }, { "all", 31 },
	};
	if (n < 3 || n > 5) return -1;
	char c0 = (char)(s[0] | 32);
	if (c0 != 'v' && c0 != 'p' && c0 != 'm' && c0 != 'a') return -1;
	for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
		if (strlen(t[i].n) == n && !strncasecmp(s, t[i].n, n)) return t[i].v;
	return -1;
}

static int sz_(const A64Op *o) { return o->arr >= A64_ELB && o->arr <= A64_ELQ ? (int)(o->arr - A64_ELB) : -1; }
static int isz(const A64Op *o) { return o->kind == A64O_REG && o->cls == A64_Z && o->arr; }	/* Zn.T */
static int iszb(const A64Op *o) { return o->kind == A64O_REG && o->cls == A64_Z && !o->arr; }	/* Zn */
static int isp(const A64Op *o) { return o->kind == A64O_REG && o->cls == A64_P && o->arr; }	/* Pn.T */
static int ispg(const A64Op *o, int q) { return o->kind == A64O_REG && o->cls == A64_P && !o->arr && o->pq == q; }	/* Pg[/z|/m] */
static int zsame(const A64Op *a, const A64Op *b) { return a->arr == b->arr; }

/* shift immediates: tsz:imm3 as a 7-bit value */
static int sh7(int esz, int64_t sh, int right, unsigned *v)
{
	int es = 8 << esz;
	if (right ? (sh < 1 || sh > es) : (sh < 0 || sh >= es)) return -1;
	*v = (unsigned)(right ? 2 * es - sh : es + sh);
	return 0;
}

/* element-replicated bitmask immediate -> N:immr:imms */
static int sve_logimm(int esz, int64_t v)
{
	int es = 8 << esz;
	uint64_t x = (uint64_t)v;
	if (es < 64) {
		uint64_t m = (1ull << es) - 1, hi = x & ~m;
		if (hi && hi != (~0ull & ~m)) return -1;	/* must fit the element (or be its sign extension) */
		x &= m;
		for (int w = es; w < 64; w *= 2) x |= x << w;
	}
	return a64_logimm(x, 1);
}

/* dup/cpy immediate: imm8 with optional lsl #8 -> sh<<8 | imm8, else -1 */
static int sve_dupimm(int esz, int64_t v, const A64Op *shop)
{
	int es = 8 << esz;
	if (shop) {
		if (shop->shift != A64_LSL || (shop->imm != 0 && shop->imm != 8) || (shop->imm == 8 && esz == 0)) return -1;
		if (v < -128 || v > 255) return -1;
		return (int)(shop->imm ? 0x100 : 0) | (int)(v & 0xff);
	}
	if (es < 64) {
		int64_t lim = 1ll << es;
		if (v >= lim || v < -lim / 2) return -1;
		if (v >= lim / 2) v -= lim;
	}
	if (esz == 0) return v >= -128 && v <= 255 ? (int)(v & 0xff) : -1;
	if (v >= -128 && v <= 127) return (int)(v & 0xff);
	if (!(v & 0xff) && v >= -32768 && v <= 32512) return 0x100 | (int)((v >> 8) & 0xff);
	return -1;
}

/* add/sub immediate: unsigned imm8 with optional lsl #8 */
static int sve_addimm(int esz, int64_t v, const A64Op *shop)
{
	if (shop) {
		if (shop->shift != A64_LSL || (shop->imm != 0 && shop->imm != 8) || (shop->imm == 8 && esz == 0)) return -1;
		if (v < 0 || v > 255) return -1;
		return (int)(shop->imm ? 0x100 : 0) | (int)v;
	}
	if (v >= 0 && v <= 255) return (int)v;
	if (esz && !(v & 0xff) && v > 0 && v <= 65280) return 0x100 | (int)(v >> 8);
	return -1;
}

static int fimmv(const A64Op *o, double *d)
{
	if (o->kind == A64O_FIMM) { *d = o->fimm; return 1; }
	if (o->kind == A64O_IMM && !o->hassym && !o->mod) { *d = (double)o->imm; return 1; }
	return 0;
}

typedef struct { const char *n; uint32_t b; } SveOp;
static int sveop(const SveOp *t, size_t nt, const char *mn, size_t n, uint32_t *b)
{
	for (size_t i = 0; i < nt; i++) if (strlen(t[i].n) == n && !memcmp(t[i].n, mn, n)) { *b = t[i].b; return 1; }
	return 0;
}
#define SVEOP(t) sveop(t, sizeof t / sizeof t[0], mn, n, &b)

/* contiguous load dtype (msz, esz, signed) -> 4-bit dtype, -1 */
static int ld_dtype(int msz, int esz, int sgn)
{
	static const int8_t u[4][4] = { { 0, 1, 2, 3 }, { -1, 5, 6, 7 }, { -1, -1, 10, 11 }, { -1, -1, -1, 15 } };
	static const int8_t s[3][4] = { { -1, 14, 13, 12 }, { -1, -1, 9, 8 }, { -1, -1, -1, 4 } };
	if (esz < 0 || esz > 3) return -1;
	if (!sgn) return u[msz][esz];
	return msz < 3 ? s[msz][esz] : -1;
}

static int sve_ldst(Ctx *c, const char *mn, size_t n)
{
	const A64Op *o = c->op;
	int load = mn[0] == 'l', nr, sgn = 0, msz, rep = 0;
	const char *p = mn + 2;
	if (n < 4 || (mn[1] != 'd' && mn[1] != 't')) return 1;
	if (!(load ? mn[1] == 'd' : mn[1] == 't')) return 1;
	if (*p < '1' || *p > '4') return 1;
	nr = *p++ - '0';
	if (load && nr == 1 && *p == 'r') { rep = 1; p++; }
	if (load && *p == 's') { sgn = 1; p++; }
	switch (*p++) { case 'b': msz = 0; break; case 'h': msz = 1; break; case 'w': msz = 2; break; case 'd': msz = 3; break; default: return 1; }
	if (p != mn + n) return 1;
	if (c->n != 3 || o[0].kind != A64O_LIST || o[0].cls != A64_Z) return 1;
	if (o[0].nregs != nr || (load ? !ispg(&o[1], 1) : !ispg(&o[1], 0)) || o[2].kind != A64O_MEM) BADOPS;
	const A64Op *mm = &o[2];
	int esz = sz_(&o[0]);
	unsigned zt = R(&o[0]), pg = R(&o[1]), rn = mm->reg;
	if (mm->mode == A64M_OFF) {
		if (mm->hassym || mm->mod) BADOPS;
		int64_t im = mm->imm;
		if (rep) {
			int dt = ld_dtype(msz, esz, sgn);
			if (dt < 0 || mm->mulvl || im % (1 << msz) || im < 0 || im / (1 << msz) > 63) BADOPS;
			*c->out = 0x84408000u | (unsigned)(dt >> 2) << 23 | (unsigned)(im >> msz) << 16 | (unsigned)(dt & 3) << 13 | pg << 10 | rn << 5 | zt;
			return 0;
		}
		if (im && !mm->mulvl) BADOPS;
		if (im % nr || im / nr < -8 || im / nr > 7) BADOPS;
		unsigned i4 = (unsigned)(im / nr) & 15;
		if (nr == 1) {
			if (load) {
				int dt = ld_dtype(msz, esz, sgn);
				if (dt < 0) BADOPS;
				*c->out = 0xa400a000u | (unsigned)dt << 21 | i4 << 16 | pg << 10 | rn << 5 | zt;
			} else {
				if (esz < msz || esz > 3) BADOPS;
				*c->out = 0xe400e000u | (unsigned)msz << 23 | (unsigned)esz << 21 | i4 << 16 | pg << 10 | rn << 5 | zt;
			}
		} else {
			if (esz != msz || sgn) BADOPS;
			*c->out = (load ? 0xa400e000u : 0xe410e000u) | (unsigned)msz << 23 | (unsigned)(nr - 1) << 21 | i4 << 16 | pg << 10 | rn << 5 | zt;
		}
		return 0;
	}
	if (mm->mode != A64M_REG || rep || mm->idxcls != A64_X || mm->index == 31) BADOPS;
	if (msz ? !(mm->hasamt == 3 && mm->shift == A64_LSL && mm->imm == msz) : mm->hasamt != 0) BADOPS;
	unsigned rm = mm->index;
	if (nr == 1) {
		if (load) {
			int dt = ld_dtype(msz, esz, sgn);
			if (dt < 0) BADOPS;
			*c->out = 0xa4004000u | (unsigned)dt << 21 | rm << 16 | pg << 10 | rn << 5 | zt;
		} else {
			if (esz < msz || esz > 3) BADOPS;
			*c->out = 0xe4004000u | (unsigned)msz << 23 | (unsigned)esz << 21 | rm << 16 | pg << 10 | rn << 5 | zt;
		}
	} else {
		if (esz != msz || sgn) BADOPS;
		*c->out = (load ? 0xa400c000u : 0xe4006000u) | (unsigned)msz << 23 | (unsigned)(nr - 1) << 21 | rm << 16 | pg << 10 | rn << 5 | zt;
	}
	return 0;
}

#define OUT2(v) do { *c->out = (v); return 0; } while (0)
/* gathers/scatters, first-fault, non-fault, non-temporal, ld1rq, prf[bhwd]:
 * returns 1 when the mnemonic is none of these */
static int sve_ldst2(Ctx *c, const char *mn, size_t n)
{
	const A64Op *o = c->op;
	int load, ff = 0, nf = 0, nt = 0, rq = 0, sgn = 0, msz;
	const char *p = mn;
	if (n >= 4 && !memcmp(mn, "prf", 3)) {
		msz = mn[3] == 'b' ? 0 : mn[3] == 'h' ? 1 : mn[3] == 'w' ? 2 : mn[3] == 'd' ? 3 : -1;
		if (n != 4 || msz < 0) return 1;
		if (c->n != 3 || o[0].kind != A64O_NAME || !ispg(&o[1], 0) || o[2].kind != A64O_MEM || o[2].cls == A64_Z) BADOPS;
		static const char *const ops[] = { "pldl1keep", "pldl1strm", "pldl2keep", "pldl2strm", "pldl3keep", "pldl3strm", 0, 0,
						   "pstl1keep", "pstl1strm", "pstl2keep", "pstl2strm", "pstl3keep", "pstl3strm" };
		unsigned op = 16;
		for (unsigned i = 0; i < 14; i++) if (ops[i] && strlen(ops[i]) == o[0].nlen && !strncasecmp(ops[i], o[0].name, o[0].nlen)) op = i;
		if (op == 16) BADOPS;
		const A64Op *mm = &o[2];
		unsigned f = R(&o[1]) << 10 | (unsigned)mm->reg << 5 | op;
		if (mm->mode == A64M_OFF) {
			if (mm->hassym || mm->mod || (mm->imm && !mm->mulvl) || mm->imm < -32 || mm->imm > 31) BADOPS;
			OUT2(0x85c00000u | ((unsigned)mm->imm & 0x3f) << 16 | (unsigned)msz << 13 | f);
		}
		if (mm->mode != A64M_REG || mm->idxcls != A64_X || mm->index == 31) BADOPS;
		if (msz ? !(mm->hasamt == 3 && mm->shift == A64_LSL && mm->imm == msz) : mm->hasamt != 0) BADOPS;
		OUT2(0x8400c000u | (unsigned)msz << 23 | (unsigned)mm->index << 16 | f);
	}
	if (n < 4 || (memcmp(mn, "ld", 2) && memcmp(mn, "st", 2))) return 1;
	load = mn[0] == 'l';
	p += 2;
	if (load && !memcmp(p, "ff", 2)) { ff = 1; p += 2; }
	else if (load && !memcmp(p, "nf", 2)) { nf = 1; p += 2; }
	else if (!memcmp(p, "nt", 2)) { nt = 1; p += 2; }
	if (*p++ != '1') return 1;
	if (load && !ff && !nf && !nt && p[0] == 'r' && p[1] == 'q') { rq = 1; p += 2; }
	if (load && *p == 's' && !nt && !rq) { sgn = 1; p++; }
	switch (*p++) { case 'b': msz = 0; break; case 'h': msz = 1; break; case 'w': msz = 2; break; case 'd': msz = 3; break; default: return 1; }
	if (p != mn + n) return 1;
	if (c->n != 3 || o[0].kind != A64O_LIST || o[0].cls != A64_Z || o[0].nregs != 1 || o[2].kind != A64O_MEM) return 1;
	const A64Op *mm = &o[2];
	int gather = mm->cls == A64_Z || (mm->mode == A64M_REG && mm->idxcls == A64_Z);
	if (!gather && !ff && !nf && !nt && !rq) return 1;	/* plain contiguous: sve_ldst */
	if (load ? !ispg(&o[1], 1) : !ispg(&o[1], 0)) BADOPS;
	int esz = sz_(&o[0]);
	unsigned f = R(&o[1]) << 10 | (unsigned)mm->reg << 5 | R(&o[0]);
	if (gather) {
		if (nf || nt || rq) BADOPS;	/* SVE2 ldnt1 gathers: not yet */
		if (esz != 2 && esz != 3) BADOPS;
		if (msz > esz || (sgn && msz == esz)) BADOPS;
		unsigned u = load && !sgn, d = esz == 3;
		if (mm->cls == A64_Z) {	/* [Zn.T{, #imm}] */
			if (mm->arr != o[0].arr || mm->mode != A64M_OFF || mm->hassym || mm->mod || mm->mulvl) BADOPS;
			int64_t im = mm->imm;
			if (im < 0 || im % (1 << msz) || (im >> msz) > 31) BADOPS;
			unsigned i5 = (unsigned)(im >> msz) << 16;
			if (load) OUT2((d ? 0xc4208000u : 0x84208000u) | (unsigned)msz << 23 | i5 | u << 14 | (unsigned)ff << 13 | f);
			OUT2((d ? 0xe440a000u : 0xe460a000u) | (unsigned)msz << 23 | i5 | f);
		}
		/* [Xn, Zm.T{, uxtw|sxtw|lsl #msz}] */
		if (mm->idx != o[0].arr) BADOPS;
		unsigned zm = (unsigned)mm->index << 16, sc = 0, xs = 0, ext = 0;
		if (mm->hasamt >= 2) {
			if (mm->shift == A64_UXTW || mm->shift == A64_SXTW) { ext = 1; xs = mm->shift == A64_SXTW; }
			else if (mm->shift != A64_LSL || !d) BADOPS;
			if (mm->hasamt == 3) { if (mm->imm != msz || !msz) BADOPS; sc = 1; }
			else if (mm->shift == A64_LSL) BADOPS;
		}
		if (!d && !ext) BADOPS;
		if (load) {
			uint32_t b = !d ? 0x84000000u : ext ? 0xc4000000u : 0xc4408000u;
			OUT2(b | (unsigned)msz << 23 | (d && !ext ? 0 : xs << 22) | sc << 21 | zm | u << 14 | (unsigned)ff << 13 | f);
		}
		uint32_t b = !d ? 0xe4408000u : ext ? 0xe4008000u : 0xe400a000u;
		OUT2(b | (unsigned)msz << 23 | sc << 21 | zm | (ext ? xs << 14 : 0) | f);
	}
	/* contiguous variants */
	if (mm->cls == A64_Z) BADOPS;
	int dt = sgn || (!nt && !rq) ? ld_dtype(msz, esz, sgn) : -1;
	if ((nt || rq) && esz != msz) BADOPS;
	if (!nt && !rq && dt < 0) BADOPS;
	if (mm->mode == A64M_OFF) {
		if (mm->hassym || mm->mod) BADOPS;
		int64_t im = mm->imm;
		if (rq) {
			if (mm->mulvl || im % 16 || im < -128 || im > 112) BADOPS;
			OUT2(0xa4002000u | (unsigned)msz << 23 | ((unsigned)(im / 16) & 15) << 16 | f);
		}
		if ((im && !mm->mulvl) || im < -8 || im > 7) BADOPS;
		unsigned i4 = ((unsigned)im & 15) << 16;
		if (nf) OUT2(0xa410a000u | (unsigned)dt << 21 | i4 | f);
		if (nt) OUT2((load ? 0xa400e000u : 0xe410e000u) | (unsigned)msz << 23 | i4 | f);
		if (ff) {	/* ldff1 [Xn] is [Xn, xzr] */
			if (im) BADOPS;
			OUT2(0xa4006000u | (unsigned)dt << 21 | 31u << 16 | f);
		}
		BADOPS;
	}
	if (nf || mm->mode != A64M_REG || mm->idxcls != A64_X || (mm->index == 31 && !ff)) BADOPS;
	if (msz ? !(mm->hasamt == 3 && mm->shift == A64_LSL && mm->imm == msz) : mm->hasamt != 0) BADOPS;
	unsigned rm = (unsigned)mm->index << 16;
	if (rq) OUT2(0xa4000000u | (unsigned)msz << 23 | rm | f);
	if (nt) OUT2((load ? 0xa400c000u : 0xe4006000u) | (unsigned)msz << 23 | rm | f);
	OUT2(0xa4006000u | (unsigned)dt << 21 | rm | f);	/* ldff1 */
}
#undef OUT2

static int k_sve(Ctx *c, const char *mn, size_t n)
{
	const A64Op *o = c->op;
	int k = c->n;
	uint32_t b;
#define MN(s) (n == sizeof s - 1 && !memcmp(mn, s, sizeof s - 1))
#define OUT(v) do { *c->out = (v); return 0; } while (0)
	if ((k && o[0].kind == A64O_LIST && o[0].cls == A64_Z) || (n == 4 && mn[0] == 'p')) {
		int r = sve_ldst2(c, mn, n);
		if (r != 1) return r;
		if (k && o[0].kind == A64O_LIST) return sve_ldst(c, mn, n);
	}

	/* element counts: cnt[bhwd] / inc / dec (scalar or vector), pattern, mul #imm */
	if (n == 4 && (!memcmp(mn, "cnt", 3) || !memcmp(mn, "inc", 3) || !memcmp(mn, "dec", 3)) && strchr("bhwd", mn[3])) {
		int es = mn[3] == 'b' ? 0 : mn[3] == 'h' ? 1 : mn[3] == 'w' ? 2 : 3;
		int pat = 31, mul = 1, i = 1;
		if (k < 1 || k > 3) BADOPS;
		if (i < k && o[i].kind == A64O_NAME) { if ((pat = a64_svepat(o[i].name, o[i].nlen)) < 0) BADOPS; i++; }
		if (i < k && o[i].kind == A64O_SHIFT && o[i].shift == A64_MUL) { mul = (int)o[i].imm; i++; }
		if (i != k || mul < 1 || mul > 16) BADOPS;
		unsigned f = (unsigned)es << 22 | (unsigned)(mul - 1) << 16 | (unsigned)pat << 5 | R(&o[0]);
		if (isx(&o[0]) && !o[0].sp) {
			if (mn[0] == 'c') OUT(0x0420e000u | f);
			OUT((mn[0] == 'i' ? 0x0430e000u : 0x0430e400u) | f);
		}
		if (mn[0] != 'c' && es && isz(&o[0]) && sz_(&o[0]) == es)
			OUT((mn[0] == 'i' ? 0x0030c000u : 0x0030c400u) | 0x04000000u | f);
		BADOPS;
	}
	if (MN("addvl") || MN("addpl")) {
		if (k != 3 || !isx(&o[0]) || !isx(&o[1]) || (o[0].reg == 31 && !o[0].sp) || (o[1].reg == 31 && !o[1].sp) ||
		    !iscimm(&o[2]) || o[2].imm < -32 || o[2].imm > 31) BADOPS;
		OUT((MN("addvl") ? 0x04205000u : 0x04605000u) | R(&o[1]) << 16 | ((unsigned)o[2].imm & 0x3f) << 5 | R(&o[0]));
	}
	if (MN("ptrue") || MN("ptrues")) {
		int pat = 31;
		if (k < 1 || k > 2 || !isp(&o[0]) || sz_(&o[0]) > 3) BADOPS;
		if (k == 2) { if (o[1].kind != A64O_NAME || (pat = a64_svepat(o[1].name, o[1].nlen)) < 0) BADOPS; }
		OUT((MN("ptrue") ? 0x2518e000u : 0x2519e000u) | (unsigned)sz_(&o[0]) << 22 | (unsigned)pat << 5 | R(&o[0]));
	}
	static const SveOp whiles[] = {
		{ "whilege", 0x25200000 }, { "whilegt", 0x25200010 }, { "whilelt", 0x25200400 }, { "whilele", 0x25200410 },
		{ "whilehs", 0x25200800 }, { "whilehi", 0x25200810 }, { "whilelo", 0x25200c00 }, { "whilels", 0x25200c10 },
	};
	if (SVEOP(whiles)) {
		if (k != 3 || !isp(&o[0]) || sz_(&o[0]) > 3 || !gpr_nosp(&o[1]) || !gpr_nosp(&o[2]) || o[1].cls != o[2].cls) BADOPS;
		OUT(b | (unsigned)sz_(&o[0]) << 22 | R(&o[2]) << 16 | (o[1].cls == A64_X ? 1u << 12 : 0) | R(&o[1]) << 5 | R(&o[0]));
	}
	if (MN("index")) {
		if (k != 3 || !isz(&o[0]) || sz_(&o[0]) > 3) BADOPS;
		int es = sz_(&o[0]);
		unsigned f = (unsigned)es << 22 | R(&o[0]);
		int rc = es == 3 ? A64_X : A64_W;
		int i1 = iscimm(&o[1]), i2 = iscimm(&o[2]);
		if (i1 && (o[1].imm < -16 || o[1].imm > 15)) BADOPS;
		if (i2 && (o[2].imm < -16 || o[2].imm > 15)) BADOPS;
		if (!i1 && !(gpr_nosp(&o[1]) && o[1].cls == rc)) BADOPS;
		if (!i2 && !(gpr_nosp(&o[2]) && o[2].cls == rc)) BADOPS;
		unsigned a = i1 ? (unsigned)o[1].imm & 31 : R(&o[1]), bb = i2 ? (unsigned)o[2].imm & 31 : R(&o[2]);
		OUT((i1 ? (i2 ? 0x04204000u : 0x04204800u) : (i2 ? 0x04204400u : 0x04204c00u)) | f | bb << 16 | a << 5);
	}
	if (MN("cntp") && k == 3 && isx(&o[0]) && !o[0].sp && ispg(&o[1], 0) && isp(&o[2]) && sz_(&o[2]) <= 3)
		OUT(0x25208000u | (unsigned)sz_(&o[2]) << 22 | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
	if ((MN("incp") || MN("decp")) && k == 2 && isx(&o[0]) && !o[0].sp && isp(&o[1]) && sz_(&o[1]) <= 3)
		OUT((MN("incp") ? 0x252c8800u : 0x252d8800u) | (unsigned)sz_(&o[1]) << 22 | R(&o[1]) << 5 | R(&o[0]));
	if (MN("ptest")) {
		if (k != 2 || !ispg(&o[0], 0) || !isp(&o[1]) || sz_(&o[1]) != 0) BADOPS;
		OUT(0x2550c000u | R(&o[0]) << 10 | R(&o[1]) << 5);
	}
	if (MN("movprfx")) {
		if (k == 2 && iszb(&o[0]) && iszb(&o[1])) OUT(0x0420bc00u | R(&o[1]) << 5 | R(&o[0]));
		if (k == 3 && isz(&o[0]) && o[1].kind == A64O_REG && o[1].cls == A64_P && !o[1].arr && o[1].pq && isz(&o[2]) && zsame(&o[0], &o[2]) && sz_(&o[0]) <= 3)
			OUT(0x04102000u | (unsigned)sz_(&o[0]) << 22 | (o[1].pq == 2 ? 1u << 16 : 0) | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
		BADOPS;
	}

	/* predicate destinations */
	if (k >= 1 && isp(&o[0])) {
		static const SveOp plog[] = {
			{ "and", 0x25004000 }, { "bic", 0x25004010 }, { "eor", 0x25004200 }, { "sel", 0x25004210 },
			{ "ands", 0x25404000 }, { "bics", 0x25404010 }, { "eors", 0x25404200 },
			{ "orr", 0x25804000 }, { "orn", 0x25804010 }, { "nor", 0x25804200 }, { "nand", 0x25804210 },
			{ "orrs", 0x25c04000 }, { "orns", 0x25c04010 }, { "nors", 0x25c04200 }, { "nands", 0x25c04210 },
		};
		static const SveOp pperm[] = {
			{ "zip1", 0x05204000 }, { "zip2", 0x05204400 }, { "uzp1", 0x05204800 }, { "uzp2", 0x05204c00 },
			{ "trn1", 0x05205000 }, { "trn2", 0x05205400 },
		};
		static const SveOp cmpv[] = {
			{ "cmphs", 0x24000000 }, { "cmphi", 0x24000010 }, { "cmpge", 0x24008000 }, { "cmpgt", 0x24008010 },
			{ "cmpeq", 0x2400a000 }, { "cmpne", 0x2400a010 },
			{ "cmpls", 0x80000000 | 0x24000000 }, { "cmplo", 0x80000000 | 0x24000010 },
			{ "cmple", 0x80000000 | 0x24008000 }, { "cmplt", 0x80000000 | 0x24008010 },
		};
		static const SveOp cmpi[] = {
			{ "cmpge", 0x25000000 }, { "cmpgt", 0x25000010 }, { "cmplt", 0x25002000 }, { "cmple", 0x25002010 },
			{ "cmpeq", 0x25008000 }, { "cmpne", 0x25008010 },
			{ "cmphs", 0x80000000 | 0x24200000 }, { "cmphi", 0x80000000 | 0x24200010 },
			{ "cmplo", 0x80000000 | 0x24202000 }, { "cmpls", 0x80000000 | 0x24202010 },
		};
		int es = sz_(&o[0]);
		if (k == 4 && ispg(&o[1], 1) && isz(&o[2]) && sz_(&o[2]) == es && es <= 3) {
			if (isz(&o[3]) && zsame(&o[2], &o[3]) && SVEOP(cmpv)) {
				const A64Op *zn = &o[2], *zm = &o[3];
				if (b & 0x80000000u) { zn = &o[3]; zm = &o[2]; b &= 0x7fffffffu; }
				OUT(b | (unsigned)es << 22 | R(zm) << 16 | R(&o[1]) << 10 | R(zn) << 5 | R(&o[0]));
			}
			if (iscimm(&o[3]) && SVEOP(cmpi)) {
				int64_t v = o[3].imm;
				if (b & 0x80000000u) {
					if (v < 0 || v > 127) BADOPS;
					OUT((b & 0x7fffffffu) | (unsigned)es << 22 | (unsigned)v << 14 | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
				}
				if (v < -16 || v > 15) BADOPS;
				OUT(b | (unsigned)es << 22 | ((unsigned)v & 31) << 16 | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
			}
		}
		static const SveOp fcmv[] = {
			{ "fcmge", 0x65004000 }, { "fcmgt", 0x65004010 }, { "fcmeq", 0x65006000 }, { "fcmne", 0x65006010 },
			{ "fcmuo", 0x6500c000 }, { "facge", 0x6500c010 }, { "facgt", 0x6500e010 },
			{ "fcmle", 0x80000000 | 0x65004000 }, { "fcmlt", 0x80000000 | 0x65004010 },
			{ "facle", 0x80000000 | 0x6500c010 }, { "faclt", 0x80000000 | 0x6500e010 },
		};
		static const SveOp fcm0[] = {
			{ "fcmge", 0x65102000 }, { "fcmgt", 0x65102010 }, { "fcmlt", 0x65112000 }, { "fcmle", 0x65112010 },
			{ "fcmeq", 0x65122000 }, { "fcmne", 0x65132000 },
		};
		if (k == 4 && ispg(&o[1], 1) && isz(&o[2]) && sz_(&o[2]) == es && es >= 1 && es <= 3) {
			double d;
			if (isz(&o[3]) && zsame(&o[2], &o[3]) && SVEOP(fcmv)) {
				const A64Op *zn = &o[2], *zm = &o[3];
				if (b & 0x80000000u) { zn = &o[3]; zm = &o[2]; b &= 0x7fffffffu; }
				OUT(b | (unsigned)es << 22 | R(zm) << 16 | R(&o[1]) << 10 | R(zn) << 5 | R(&o[0]));
			}
			if (fimmv(&o[3], &d) && d == 0 && SVEOP(fcm0))
				OUT(b | (unsigned)es << 22 | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
		}
		if (es == 0 && k == 4 && isp(&o[2]) && isp(&o[3]) && !sz_(&o[2]) && !sz_(&o[3]) && SVEOP(plog)) {
			if (MN("sel") ? !ispg(&o[1], 0) : !ispg(&o[1], 1)) BADOPS;
			OUT(b | R(&o[3]) << 16 | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
		}
		static const SveOp brk[] = { { "brka", 0x25104000 }, { "brkb", 0x25904000 }, { "brkas", 0x25504000 }, { "brkbs", 0x25d04000 } };
		if (es == 0 && k == 3 && o[1].kind == A64O_REG && o[1].cls == A64_P && !o[1].arr && o[1].pq && isp(&o[2]) && !sz_(&o[2]) && SVEOP(brk)) {
			if (o[1].pq == 2 && n == 5) BADOPS;	/* brkas/brkbs: zeroing only */
			OUT(b | R(&o[1]) << 10 | R(&o[2]) << 5 | (o[1].pq == 2 ? 0x10u : 0) | R(&o[0]));
		}
		static const SveOp brkp[] = { { "brkpa", 0x2500c000 }, { "brkpb", 0x2500c010 }, { "brkpas", 0x2540c000 }, { "brkpbs", 0x2540c010 } };
		if (es == 0 && k == 4 && ispg(&o[1], 1) && isp(&o[2]) && isp(&o[3]) && !sz_(&o[2]) && !sz_(&o[3]) && SVEOP(brkp))
			OUT(b | R(&o[3]) << 16 | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
		if (MN("mov") || MN("movs") || MN("not") || MN("nots")) {
			int s = mn[n - 1] == 's' ? 0x400000 : 0;
			if (es) BADOPS;
			if (MN("mov") && k == 2 && isp(&o[1]) && !sz_(&o[1]))	/* orr Pd, Pn/z, Pn, Pn */
				OUT(0x25804000u | R(&o[1]) << 16 | R(&o[1]) << 10 | R(&o[1]) << 5 | R(&o[0]));
			if (k == 3 && isp(&o[2]) && !sz_(&o[2]) && o[1].kind == A64O_REG && o[1].cls == A64_P && !o[1].arr) {
				if (mn[0] == 'n' && o[1].pq == 1) OUT(0x25004200u | s | R(&o[1]) << 16 | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
				if (mn[0] == 'm' && o[1].pq == 1) OUT(0x25004000u | s | R(&o[2]) << 16 | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
				if (MN("mov") && o[1].pq == 2) OUT(0x25004210u | R(&o[0]) << 16 | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
			}
			BADOPS;
		}
		if (k == 3 && isp(&o[1]) && isp(&o[2]) && sz_(&o[1]) == es && sz_(&o[2]) == es && es <= 3 && SVEOP(pperm))
			OUT(b | (unsigned)es << 22 | R(&o[2]) << 16 | R(&o[1]) << 5 | R(&o[0]));
		if (MN("rev") && k == 2 && isp(&o[1]) && sz_(&o[1]) == es && es <= 3)
			OUT(0x05344000u | (unsigned)es << 22 | R(&o[1]) << 5 | R(&o[0]));
		return 1;
	}

	/* reductions and last element: Vd/Rd, Pg, Zn.T */
	static const SveOp fred[] = { { "faddv", 0x65002000 }, { "fmaxnmv", 0x65042000 }, { "fminnmv", 0x65052000 },
				      { "fmaxv", 0x65062000 }, { "fminv", 0x65072000 } };
	if (k == 3 && ispg(&o[1], 0) && isz(&o[2]) && sz_(&o[2]) >= 1 && sz_(&o[2]) <= 3 && SVEOP(fred)) {
		int es = sz_(&o[2]);
		if (!isreg(&o[0]) || o[0].cls != (es == 1 ? A64_H : es == 2 ? A64_S : A64_D)) BADOPS;
		OUT(b | (unsigned)es << 22 | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
	}
	if (MN("fadda") && k == 4 && ispg(&o[1], 0) && isz(&o[3]) && sz_(&o[3]) >= 1 && sz_(&o[3]) <= 3) {
		int es = sz_(&o[3]), want = es == 1 ? A64_H : es == 2 ? A64_S : A64_D;
		if (!isreg(&o[0]) || o[0].cls != want || !isreg(&o[2]) || o[2].cls != want || R(&o[0]) != R(&o[2])) BADOPS;
		OUT(0x65182000u | (unsigned)es << 22 | R(&o[1]) << 10 | R(&o[3]) << 5 | R(&o[0]));
	}
	static const SveOp red[] = {
		{ "saddv", 0x04002000 }, { "uaddv", 0x04012000 }, { "smaxv", 0x04082000 }, { "umaxv", 0x04092000 },
		{ "sminv", 0x040a2000 }, { "uminv", 0x040b2000 }, { "orv", 0x04182000 }, { "eorv", 0x04192000 },
		{ "andv", 0x041a2000 },
	};
	if (k == 3 && ispg(&o[1], 0) && isz(&o[2]) && sz_(&o[2]) <= 3) {
		int es = sz_(&o[2]);
		if (SVEOP(red)) {
			int want = MN("saddv") || MN("uaddv") ? A64_D : es == 0 ? A64_B : es == 1 ? A64_H : es == 2 ? A64_S : A64_D;
			if (!isreg(&o[0]) || o[0].cls != want) BADOPS;
			OUT(b | (unsigned)es << 22 | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
		}
		if (MN("lasta") || MN("lastb")) {
			if (gpr_nosp(&o[0]) && o[0].cls == (es == 3 ? A64_X : A64_W))
				OUT((MN("lasta") ? 0x0520a000u : 0x0521a000u) | (unsigned)es << 22 | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
			int want = es == 0 ? A64_B : es == 1 ? A64_H : es == 2 ? A64_S : A64_D;
			if (isreg(&o[0]) && o[0].cls == want)
				OUT((MN("lasta") ? 0x05228000u : 0x05238000u) | (unsigned)es << 22 | R(&o[1]) << 10 | R(&o[2]) << 5 | R(&o[0]));
			BADOPS;
		}
	}

	if (!(k >= 2 && isz(&o[0]))) return 1;
	int es = sz_(&o[0]);
	unsigned zd = R(&o[0]);

	if (n >= 2 && mn[0] == 'f' && es >= 1 && es <= 3) {
		static const SveOp fp3[] = { { "fadd", 0x65000000 }, { "fsub", 0x65000400 }, { "fmul", 0x65000800 },
					     { "ftsmul", 0x65000c00 }, { "frecps", 0x65001800 }, { "frsqrts", 0x65001c00 } };
		static const SveOp fpp[] = {
			{ "fadd", 0x65008000 }, { "fsub", 0x65018000 }, { "fmul", 0x65028000 }, { "fsubr", 0x65038000 },
			{ "fmaxnm", 0x65048000 }, { "fminnm", 0x65058000 }, { "fmax", 0x65068000 }, { "fmin", 0x65078000 },
			{ "fabd", 0x65088000 }, { "fscale", 0x65098000 }, { "fmulx", 0x650a8000 }, { "fdivr", 0x650c8000 },
			{ "fdiv", 0x650d8000 },
		};
		static const SveOp fpi[] = {	/* #imm: two choices, i1 */
			{ "fadd", 0x65188000 }, { "fsub", 0x65198000 }, { "fmul", 0x651a8000 }, { "fsubr", 0x651b8000 },
			{ "fmaxnm", 0x651c8000 }, { "fminnm", 0x651d8000 }, { "fmax", 0x651e8000 }, { "fmin", 0x651f8000 },
		};
		static const SveOp fma[] = { { "fmla", 0x65200000 }, { "fmls", 0x65202000 }, { "fnmla", 0x65204000 }, { "fnmls", 0x65206000 },
					     { "fmad", 0x65208000 }, { "fmsb", 0x6520a000 }, { "fnmad", 0x6520c000 }, { "fnmsb", 0x6520e000 } };
		static const SveOp fun[] = {
			{ "fabs", 0x041ca000 }, { "fneg", 0x041da000 },
			{ "frintn", 0x6500a000 }, { "frintp", 0x6501a000 }, { "frintm", 0x6502a000 }, { "frintz", 0x6503a000 },
			{ "frinta", 0x6504a000 }, { "frintx", 0x6506a000 }, { "frinti", 0x6507a000 },
			{ "frecpx", 0x650ca000 }, { "fsqrt", 0x650da000 },
		};
		static const SveOp fu2[] = { { "frecpe", 0x650e3000 }, { "frsqrte", 0x650f3000 } };
		double d;
		if (k == 3 && isz(&o[1]) && isz(&o[2]) && !o[2].hasidx && zsame(&o[0], &o[1]) && zsame(&o[0], &o[2]) && SVEOP(fp3))
			OUT(b | (unsigned)es << 22 | R(&o[2]) << 16 | R(&o[1]) << 5 | zd);
		if (k == 2 && isz(&o[1]) && zsame(&o[0], &o[1]) && SVEOP(fu2))
			OUT(b | (unsigned)es << 22 | R(&o[1]) << 5 | zd);
		if (k == 4 && ispg(&o[1], 2) && isz(&o[2]) && zsame(&o[0], &o[2])) {
			if (R(&o[2]) == zd && isz(&o[3]) && zsame(&o[0], &o[3]) && SVEOP(fpp))
				OUT(b | (unsigned)es << 22 | R(&o[1]) << 10 | R(&o[3]) << 5 | zd);
			if (R(&o[2]) == zd && fimmv(&o[3], &d) && SVEOP(fpi)) {
				int mul = MN("fmul"), mm = mn[1] == 'm' && !mul;	/* fmax/fmin(nm): 0.0/1.0 */
				int i1 = mul ? (d == 2.0 ? 1 : d == 0.5 ? 0 : -1) : mm ? (d == 1.0 ? 1 : d == 0.0 ? 0 : -1) : (d == 1.0 ? 1 : d == 0.5 ? 0 : -1);
				if (i1 < 0) BADOPS;
				OUT(b | (unsigned)es << 22 | R(&o[1]) << 10 | (unsigned)i1 << 5 | zd);
			}
			if (isz(&o[3]) && zsame(&o[0], &o[3]) && SVEOP(fma))	/* fmla: Zm, Zn; fmad: Za, Zm */
				OUT(b | (unsigned)es << 22 | R(&o[3]) << 16 | R(&o[1]) << 10 | R(&o[2]) << 5 | zd);
		}
		if (k == 3 && ispg(&o[1], 2) && isz(&o[2])) {
			if (zsame(&o[0], &o[2]) && SVEOP(fun)) OUT(b | (unsigned)es << 22 | R(&o[1]) << 10 | R(&o[2]) << 5 | zd);
			if (MN("fcvt")) {
				static const struct { uint8_t d, s; uint32_t b; } fc[] = {
					{ 1, 2, 0x6588a000 }, { 2, 1, 0x6589a000 }, { 1, 3, 0x65c8a000 },
					{ 3, 1, 0x65c9a000 }, { 2, 3, 0x65caa000 }, { 3, 2, 0x65cba000 } };
				for (size_t i = 0; i < sizeof fc / sizeof fc[0]; i++)
					if (fc[i].d == es && fc[i].s == sz_(&o[2])) OUT(fc[i].b | R(&o[1]) << 10 | R(&o[2]) << 5 | zd);
				BADOPS;
			}
		}
		if ((MN("fmov") || MN("fdup")) && k == 2 && fimmv(&o[1], &d)) {
			unsigned i8;
			if (d == 0 && MN("fmov")) OUT(0x2538c000u | (unsigned)es << 22 | zd);	/* dup #0 */
			if (fp_imm8(d, &i8)) BADOPS;
			OUT(0x2539c000u | (unsigned)es << 22 | i8 << 5 | zd);
		}
		if ((MN("fmov") || MN("fcpy")) && k == 3 && ispg(&o[1], 2) && fimmv(&o[2], &d)) {
			unsigned i8;
			if (d == 0 && MN("fmov")) OUT(0x05104000u | (unsigned)es << 22 | R(&o[1]) << 16 | zd);	/* cpy #0, /m */
			if (fp_imm8(d, &i8)) BADOPS;
			OUT(0x0510c000u | (unsigned)es << 22 | R(&o[1]) << 16 | i8 << 5 | zd);
		}
	}
	/* by element: Zd.T, Zn.T, Zm.T[i] (fmla/fmls/fmul, mla/mls/mul), and sdot/udot */
	static const SveOp byel[] = { { "fmla", 0x64200000 }, { "fmls", 0x64200400 }, { "fmul", 0x64202000 },
				      { "mla", 0x44200800 }, { "mls", 0x44200c00 }, { "mul", 0x4420f800 } };
	if (k == 3 && isz(&o[1]) && zsame(&o[0], &o[1]) && o[2].kind == A64O_REG && o[2].cls == A64_Z && o[2].hasidx &&
	    o[2].arr == o[0].arr && es >= 1 && es <= 3 && SVEOP(byel)) {
		unsigned i = o[2].idx, zm = R(&o[2]);
		if (es == 1) { if (i > 7 || zm > 7) BADOPS; OUT(b | (i >> 2) << 22 | (i & 3) << 19 | zm << 16 | R(&o[1]) << 5 | zd); }
		if (es == 2) { if (i > 3 || zm > 7) BADOPS; OUT(b | 0x800000u | i << 19 | zm << 16 | R(&o[1]) << 5 | zd); }
		if (i > 1 || zm > 15) BADOPS;
		OUT(b | 0xc00000u | i << 20 | zm << 16 | R(&o[1]) << 5 | zd);
	}
	if ((MN("sdot") || MN("udot")) && k == 3 && (es == 2 || es == 3) && isz(&o[1]) && sz_(&o[1]) == es - 2 &&
	    o[2].kind == A64O_REG && o[2].cls == A64_Z && o[2].arr == o[1].arr) {
		unsigned u = MN("udot") ? 0x400u : 0, d = es == 3, zm = R(&o[2]), i = o[2].idx;
		if (!o[2].hasidx) OUT(0x44800000u | d << 22 | zm << 16 | u | R(&o[1]) << 5 | zd);
		if (d ? (i > 1 || zm > 15) : (i > 3 || zm > 7)) BADOPS;
		OUT((d ? 0x44e00000u | i << 20 : 0x44a00000u | i << 19) | zm << 16 | u | R(&o[1]) << 5 | zd);
	}
	/* mov Zd.T, Vn | Zn.T[i]: dup (indexed) */
	if ((MN("mov") || MN("dup")) && k == 2 && es <= 4 &&
	    ((isreg(&o[1]) && o[1].cls == (es == 0 ? A64_B : es == 1 ? A64_H : es == 2 ? A64_S : es == 3 ? A64_D : A64_Q) && MN("mov")) ||
	     (o[1].kind == A64O_REG && o[1].cls == A64_Z && o[1].hasidx && o[1].arr == o[0].arr))) {
		unsigned i = o[1].hasidx ? o[1].idx : 0;
		if (i >= (64u >> es)) BADOPS;
		unsigned v = (i << (es + 1) | 1u << es) & 0x7f;
		OUT(0x05202000u | (v >> 5) << 22 | (v & 31) << 16 | R(&o[1]) << 5 | zd);
	}

	/* predicated: Zdn, Pg/m, Zdn, Zm | #imm */
	static const SveOp pbin[] = {
		{ "add", 0x04000000 }, { "sub", 0x04010000 }, { "subr", 0x04030000 },
		{ "smax", 0x04080000 }, { "umax", 0x04090000 }, { "smin", 0x040a0000 }, { "umin", 0x040b0000 },
		{ "sabd", 0x040c0000 }, { "uabd", 0x040d0000 },
		{ "mul", 0x04100000 }, { "smulh", 0x04120000 }, { "umulh", 0x04130000 },
		{ "sdiv", 0x04140000 }, { "udiv", 0x04150000 }, { "sdivr", 0x04160000 }, { "udivr", 0x04170000 },
		{ "orr", 0x04180000 }, { "eor", 0x04190000 }, { "and", 0x041a0000 }, { "bic", 0x041b0000 },
		{ "asr", 0x04108000 }, { "lsr", 0x04118000 }, { "lsl", 0x04138000 },
		{ "asrr", 0x04148000 }, { "lsrr", 0x04158000 }, { "lslr", 0x04178000 },
	};
	static const SveOp pshi[] = { { "asr", 0x04008000 }, { "lsr", 0x04018000 }, { "lsl", 0x04038000 }, { "asrd", 0x04048000 },
				      { "sqshl", 0x04068000 }, { "uqshl", 0x04078000 }, { "srshr", 0x040c8000 }, { "urshr", 0x040d8000 } };
	static const SveOp mla[] = { { "mla", 0x04004000 }, { "mls", 0x04006000 }, { "mad", 0x0400c000 }, { "msb", 0x0400e000 } };
	if (k == 4 && ispg(&o[1], 2) && es <= 3) {
		if (isz(&o[2]) && zsame(&o[0], &o[2]) && R(&o[2]) == zd && isz(&o[3]) && zsame(&o[0], &o[3]) && SVEOP(pbin))
			OUT(b | (unsigned)es << 22 | R(&o[1]) << 10 | R(&o[3]) << 5 | zd);
		if (isz(&o[2]) && zsame(&o[0], &o[2]) && R(&o[2]) == zd && iscimm(&o[3]) && SVEOP(pshi)) {
			unsigned v;
			if (sh7(es, o[3].imm, !(MN("lsl") || MN("sqshl") || MN("uqshl")), &v)) BADOPS;
			OUT(b | (v >> 5) << 22 | R(&o[1]) << 10 | (v & 31) << 5 | zd);
		}
		if (isz(&o[2]) && isz(&o[3]) && zsame(&o[0], &o[2]) && zsame(&o[0], &o[3]) && SVEOP(mla)) {
			int ma = mn[1] == 'a' || mn[1] == 's';	/* mad/msb: Zm, Za */
			OUT(b | (unsigned)es << 22 | R(&o[ma ? 2 : 3]) << 16 | R(&o[1]) << 10 | R(&o[ma ? 3 : 2]) << 5 | zd);
		}
	}

	/* conversions: Zd, Pg/m, Zn */
	if (k == 3 && ispg(&o[1], 2) && isz(&o[2])) {
		static const struct { const char *n; uint8_t d, s; uint32_t b; } cv[] = {
			{ "scvtf", 1, 1, 0x6552a000 }, { "scvtf", 1, 2, 0x6554a000 }, { "scvtf", 1, 3, 0x6556a000 },
			{ "scvtf", 2, 2, 0x6594a000 }, { "scvtf", 3, 2, 0x65d0a000 }, { "scvtf", 2, 3, 0x65d4a000 }, { "scvtf", 3, 3, 0x65d6a000 },
			{ "ucvtf", 1, 1, 0x6553a000 }, { "ucvtf", 1, 2, 0x6555a000 }, { "ucvtf", 1, 3, 0x6557a000 },
			{ "ucvtf", 2, 2, 0x6595a000 }, { "ucvtf", 3, 2, 0x65d1a000 }, { "ucvtf", 2, 3, 0x65d5a000 }, { "ucvtf", 3, 3, 0x65d7a000 },
			{ "fcvtzs", 1, 1, 0x655aa000 }, { "fcvtzs", 2, 1, 0x655ca000 }, { "fcvtzs", 3, 1, 0x655ea000 },
			{ "fcvtzs", 2, 2, 0x659ca000 }, { "fcvtzs", 3, 2, 0x65dca000 }, { "fcvtzs", 2, 3, 0x65d8a000 }, { "fcvtzs", 3, 3, 0x65dea000 },
			{ "fcvtzu", 1, 1, 0x655ba000 }, { "fcvtzu", 2, 1, 0x655da000 }, { "fcvtzu", 3, 1, 0x655fa000 },
			{ "fcvtzu", 2, 2, 0x659da000 }, { "fcvtzu", 3, 2, 0x65dda000 }, { "fcvtzu", 2, 3, 0x65d9a000 }, { "fcvtzu", 3, 3, 0x65dfa000 },
		};
		for (size_t i = 0; i < sizeof cv / sizeof cv[0]; i++)
			if (strlen(cv[i].n) == n && !memcmp(cv[i].n, mn, n) && cv[i].d == es && cv[i].s == sz_(&o[2]))
				OUT(cv[i].b | R(&o[1]) << 10 | R(&o[2]) << 5 | zd);
		static const SveOp un[] = {
			{ "abs", 0x0416a000 }, { "neg", 0x0417a000 }, { "cls", 0x0418a000 }, { "clz", 0x0419a000 },
			{ "cnt", 0x041aa000 }, { "cnot", 0x041ba000 }, { "not", 0x041ea000 },
			{ "sxtb", 0x0410a000 }, { "uxtb", 0x0411a000 }, { "sxth", 0x0412a000 }, { "uxth", 0x0413a000 },
			{ "sxtw", 0x0414a000 }, { "uxtw", 0x0415a000 }, { "rbit", 0x05278000 },
			{ "revb", 0x05248000 }, { "revh", 0x05258000 }, { "revw", 0x05268000 },
		};
		if (zsame(&o[0], &o[2]) && es <= 3 && SVEOP(un))
			OUT(b | (unsigned)es << 22 | R(&o[1]) << 10 | R(&o[2]) << 5 | zd);
	}

	/* unpredicated three vectors */
	static const SveOp v3[] = {
		{ "add", 0x04200000 }, { "sub", 0x04200400 }, { "sqadd", 0x04201000 }, { "uqadd", 0x04201400 },
		{ "sqsub", 0x04201800 }, { "uqsub", 0x04201c00 },
		{ "mul", 0x04206000 }, { "pmul", 0x04206400 }, { "smulh", 0x04206800 }, { "umulh", 0x04206c00 },
		{ "zip1", 0x05206000 }, { "zip2", 0x05206400 }, { "uzp1", 0x05206800 }, { "uzp2", 0x05206c00 },
		{ "trn1", 0x05207000 }, { "trn2", 0x05207400 },
	};
	static const SveOp v3d[] = { { "and", 0x04203000 }, { "orr", 0x04603000 }, { "eor", 0x04a03000 }, { "bic", 0x04e03000 } };
	if (k == 3 && isz(&o[1]) && isz(&o[2]) && !o[2].hasidx && es <= 3) {
		if (zsame(&o[0], &o[1]) && zsame(&o[0], &o[2]) && SVEOP(v3))
			OUT(b | (unsigned)es << 22 | R(&o[2]) << 16 | R(&o[1]) << 5 | zd);
		if (SVEOP(v3d)) OUT(b | R(&o[2]) << 16 | R(&o[1]) << 5 | zd);
	}
	if (MN("tbl") && k == 3 && o[1].kind == A64O_LIST && o[1].cls == A64_Z && o[1].nregs == 1 && o[1].arr == o[0].arr &&
	    isz(&o[2]) && zsame(&o[0], &o[2]) && es <= 3)
		OUT(0x05203000u | (unsigned)es << 22 | R(&o[2]) << 16 | R(&o[1]) << 5 | zd);
	static const SveOp v4d[] = { { "eor3", 0x04203800 }, { "bcax", 0x04603800 }, { "bsl", 0x04203c00 },
				     { "bsl1n", 0x04603c00 }, { "bsl2n", 0x04a03c00 }, { "nbsl", 0x04e03c00 } };
	if (k == 4 && isz(&o[1]) && R(&o[1]) == zd && isz(&o[2]) && isz(&o[3]) && SVEOP(v4d))
		OUT(b | R(&o[2]) << 16 | R(&o[3]) << 5 | zd);
	if (MN("xar") && k == 4 && isz(&o[1]) && R(&o[1]) == zd && isz(&o[2]) && iscimm(&o[3]) && es <= 3) {
		unsigned v;
		if (sh7(es, o[3].imm, 1, &v)) BADOPS;
		OUT(0x04203400u | (v >> 5) << 22 | (v & 31) << 16 | R(&o[2]) << 5 | zd);
	}

	/* vector and immediate */
	static const SveOp sra[] = { { "ssra", 0x4500e000 }, { "usra", 0x4500e400 }, { "srsra", 0x4500e800 }, { "ursra", 0x4500ec00 } };
	static const SveOp shi[] = { { "asr", 0x04209000 }, { "lsr", 0x04209400 }, { "lsl", 0x04209c00 } };
	static const SveOp addi[] = { { "add", 0x2520c000 }, { "sub", 0x2521c000 }, { "subr", 0x2523c000 },
				      { "sqadd", 0x2524c000 }, { "uqadd", 0x2525c000 }, { "sqsub", 0x2526c000 }, { "uqsub", 0x2527c000 } };
	static const SveOp mmi[] = { { "smax", 0x2528c000 }, { "umax", 0x2529c000 }, { "smin", 0x252ac000 }, { "umin", 0x252bc000 } };
	static const SveOp logi[] = { { "orr", 0x05000000 }, { "eor", 0x05400000 }, { "and", 0x05800000 } };
	if ((k == 3 || k == 4) && isz(&o[1]) && zsame(&o[0], &o[1]) && iscimm(&o[2]) && es <= 3) {
		const A64Op *shop = k == 4 ? &o[3] : 0;
		if (shop && shop->kind != A64O_SHIFT) BADOPS;
		int64_t v = o[2].imm;
		unsigned sv;
		if (!shop && SVEOP(shi)) { if (sh7(es, v, !MN("lsl"), &sv)) BADOPS; OUT(b | (sv >> 5) << 22 | (sv & 31) << 16 | R(&o[1]) << 5 | zd); }
		if (!shop && SVEOP(sra)) { if (sh7(es, v, 1, &sv)) BADOPS; OUT(b | (sv >> 5) << 22 | (sv & 31) << 16 | R(&o[1]) << 5 | zd); }
		if (R(&o[1]) != zd) BADOPS;
		if (SVEOP(addi)) { int e = sve_addimm(es, v, shop); if (e < 0) BADOPS; OUT(b | (unsigned)es << 22 | (unsigned)e << 5 | zd); }
		if (shop) BADOPS;
		if (SVEOP(mmi)) {
			int u = MN("umax") || MN("umin");
			if (u ? (v < 0 || v > 255) : (v < -128 || v > 127)) BADOPS;
			OUT(b | (unsigned)es << 22 | ((unsigned)v & 0xff) << 5 | zd);
		}
		if (MN("mul")) { if (v < -128 || v > 127) BADOPS; OUT(0x2530c000u | (unsigned)es << 22 | ((unsigned)v & 0xff) << 5 | zd); }
		if (SVEOP(logi)) { int e = sve_logimm(es, v); if (e < 0) BADOPS; OUT(b | (unsigned)e << 5 | zd); }
	}

	if (MN("splice") && k == 4 && ispg(&o[1], 0) && isz(&o[2]) && R(&o[2]) == zd && isz(&o[3]) && zsame(&o[0], &o[2]) && zsame(&o[0], &o[3]) && es <= 3)
		OUT(0x052c8000u | (unsigned)es << 22 | R(&o[1]) << 10 | R(&o[3]) << 5 | zd);
	static const SveOp unpk[] = { { "sunpklo", 0x05303800 }, { "sunpkhi", 0x05313800 }, { "uunpklo", 0x05323800 }, { "uunpkhi", 0x05333800 } };
	if (k == 2 && isz(&o[1]) && es >= 1 && es <= 3 && sz_(&o[1]) == es - 1 && SVEOP(unpk))
		OUT(b | (unsigned)es << 22 | R(&o[1]) << 5 | zd);
	if (MN("sel") && k == 4 && ispg(&o[1], 0) && isz(&o[2]) && isz(&o[3]) && zsame(&o[0], &o[2]) && zsame(&o[0], &o[3]) && es <= 3)
		OUT(0x0520c000u | (unsigned)es << 22 | R(&o[3]) << 16 | R(&o[1]) << 10 | R(&o[2]) << 5 | zd);
	if (MN("rev") && k == 2 && isz(&o[1]) && zsame(&o[0], &o[1]) && es <= 3)
		OUT(0x05383800u | (unsigned)es << 22 | R(&o[1]) << 5 | zd);
	if (MN("dupm") && k == 2 && iscimm(&o[1]) && es <= 3) {
		int e = sve_logimm(es, o[1].imm);
		if (e < 0) BADOPS;
		OUT(0x05c00000u | (unsigned)e << 5 | zd);
	}
	if ((MN("mov") || MN("dup")) && es <= 3) {
		if (k == 2 && isgpr(&o[1]) && o[1].cls == (es == 3 ? A64_X : A64_W))	/* dup Zd, Rn|SP */
			OUT(0x05203800u | (unsigned)es << 22 | R(&o[1]) << 5 | zd);
		if ((k == 2 || k == 3) && iscimm(&o[1])) {
			const A64Op *shop = k == 3 ? &o[2] : 0;
			if (shop && shop->kind != A64O_SHIFT) BADOPS;
			int e = sve_dupimm(es, o[1].imm, shop);
			if (e >= 0) OUT(0x2538c000u | (unsigned)es << 22 | (unsigned)(e >> 8) << 13 | (unsigned)(e & 0xff) << 5 | zd);
			if (MN("mov") && !shop && (e = sve_logimm(es, o[1].imm)) >= 0) OUT(0x05c00000u | (unsigned)e << 5 | zd);
			BADOPS;
		}
		if (MN("mov") && k == 2 && isz(&o[1]) && zsame(&o[0], &o[1]))	/* orr Zd.d, Zn.d, Zn.d */
			OUT(0x04603000u | R(&o[1]) << 16 | R(&o[1]) << 5 | zd);
		if (MN("mov") && (k == 3 || k == 4) && o[1].kind == A64O_REG && o[1].cls == A64_P && !o[1].arr && o[1].pq) {
			if (isz(&o[2]) && zsame(&o[0], &o[2]) && o[1].pq == 2 && k == 3)	/* sel Zd, Pg, Zn, Zd */
				OUT(0x0520c000u | (unsigned)es << 22 | zd << 16 | R(&o[1]) << 10 | R(&o[2]) << 5 | zd);
			if (isgpr(&o[2]) && o[1].pq == 2 && k == 3 && o[2].cls == (es == 3 ? A64_X : A64_W))	/* cpy Zd, Pg/m, Rn|SP */
				OUT(0x0528a000u | (unsigned)es << 22 | R(&o[1]) << 10 | R(&o[2]) << 5 | zd);
			if (iscimm(&o[2])) {	/* cpy Zd, Pg/zm, #imm */
				const A64Op *shop = k == 4 ? &o[3] : 0;
				if (shop && shop->kind != A64O_SHIFT) BADOPS;
				int e = sve_dupimm(es, o[2].imm, shop);
				if (e < 0) BADOPS;
				OUT(0x05100000u | (unsigned)es << 22 | R(&o[1]) << 16 | (o[1].pq == 2 ? 1u << 14 : 0) |
				    (unsigned)(e >> 8) << 13 | (unsigned)(e & 0xff) << 5 | zd);
			}
		}
		BADOPS;
	}
	return 1;
#undef MN
#undef OUT
}

/* any SVE operand, or a mnemonic only SVE has */
static int sve_insn(const char *mn, size_t n, const A64Op *op, int nop)
{
	for (int i = 0; i < nop && i < 2; i++)	/* an SVE form has its Z/P operand first or second */
		if ((op[i].kind == A64O_REG || op[i].kind == A64O_LIST) && (op[i].cls == A64_Z || op[i].cls == A64_P)) return 1;
	return (n == 4 && !memcmp(mn, "prf", 3) && nop == 3 && op[0].kind == A64O_NAME && op[2].kind == A64O_MEM && op[1].kind == A64O_REG && op[1].cls == A64_P) ||
	       (n == 5 && (!memcmp(mn, "addvl", 5) || !memcmp(mn, "addpl", 5))) ||
	       (n == 4 && (!memcmp(mn, "cnt", 3) || !memcmp(mn, "inc", 3) || !memcmp(mn, "dec", 3)) && strchr("bhwd", mn[3]));
}
