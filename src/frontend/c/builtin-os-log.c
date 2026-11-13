/* vcc front end: __builtin_os_log_format and _buffer_size (Apple's os/trace_base.h); see builtin-table.c for where a builtin's other facts live */
#include "ccimpl.h"

/* ---- __builtin_os_log_format / _buffer_size (Apple's os/trace_base.h) ----
 * clang's OSLog.cpp, checked byte for byte against clang: a buffer of
 *   summary (1: private item, 2: non-scalar item), item count,
 *   per item: descriptor (kind << 4 | flags), data size, data.
 * Kinds: 0 scalar, 1 count, 2 string, 3 pointer (%P), 4 object (%@),
 * 6 errno (%m, no data), 7 mask (%{mask.X}: X's bytes, 8 wide).
 * Flags from %{...}: private 1, public 2, sensitive 5; private or sensitive
 * wins over public.  A width '*' is a scalar item without flags; a
 * precision of %s/%P is a count item ('*': the argument, without flags; a
 * constant: the item's flags), of any other conversion a '*' is a scalar.
 * The data is the argument after default promotions; a conversion without
 * an argument has no item, an argument without a conversion is not evaluated. */
enum { OSL_MAX = 64 };
typedef struct { uint8_t desc, size; int arg; uint64_t imm; } oslog_item;

static int oslog_layout(cc *c, const char *f, size_t len, const uint32_t *args, unsigned nargs,
                        oslog_item *it, uint32_t loc)
{
	int n = 0;
	unsigned next = 0;
	const char *e = f + len;

#define OSL_ADD(D, ARG, IMM, SZ) do { if (n == OSL_MAX) { cc_error(c, loc, "os_log() format has too many arguments"); return -1; } \
	it[n].desc = (uint8_t)(D), it[n].arg = (ARG), it[n].imm = (IMM), it[n].size = (uint8_t)(SZ); n++; } while (0)
	for (const char *p = f; p < e; p++) {
		if (*p != '%')
			continue;
		p++;
		unsigned flags = 0, priv = 0, pub = 0;
		bool mask = false;
		uint64_t maskv = 0;
		if (p < e && *p == '{') {
			const char *q = ++p;
			while (p < e && *p != '}')
				p++;
			for (const char *a = q; a < p;) {
				const char *z = a;
				while (z < p && *z != ',')
					z++;
				const char *s = a, *t = z;
				while (s < t && (*s == ' ' || *s == '\t')) s++;
				while (t > s && (t[-1] == ' ' || t[-1] == '\t')) t--;
				size_t l = (size_t)(t - s);
				if (l == 6 && !memcmp(s, "public", 6)) pub = 1;
				else if (l == 7 && !memcmp(s, "private", 7)) priv |= 1;
				else if (l == 9 && !memcmp(s, "sensitive", 9)) priv |= 5;
				else if (l > 5 && !memcmp(s, "mask.", 5) && l - 5 <= 8) {
					mask = true, maskv = 0;
					for (size_t i = 5; i < l; i++)
						maskv |= (uint64_t)(uint8_t)s[i] << 8 * (i - 5);
				} else if (l >= 5 && !memcmp(s, "mask.", 5)) {
					cc_error(c, loc, "mask type size must be between 1-byte and 8-bytes");
					return -1;
				}
				a = z < p ? z + 1 : z;
			}
			if (p < e)
				p++;
			flags = priv ? priv : pub ? 2 : 0;
		}
		if (p >= e)
			break;
		if (*p == '%')
			continue;
		/* N$: the argument of this conversion */
		int pos = -1;
		{
			const char *q = p;
			unsigned v = 0;
			while (q < e && *q >= '0' && *q <= '9') v = v * 10 + (unsigned)(*q++ - '0');
			if (q > p && q < e && *q == '$' && v) pos = (int)v - 1, p = q + 1;
		}
		while (p < e && strchr("-+ #0'", *p))
			p++;
		int warg = -2, parg = -2;	/* -2: none, -1: constant, else the argument */
		uint64_t prec = 0;
		if (p < e && *p == '*') {
			p++;
			unsigned v = 0;
			const char *q = p;
			while (q < e && *q >= '0' && *q <= '9') v = v * 10 + (unsigned)(*q++ - '0');
			warg = q > p && q < e && *q == '$' && v ? (p = q + 1, (int)v - 1) : (int)next++;
		} else
			while (p < e && *p >= '0' && *p <= '9') p++;
		if (p < e && *p == '.') {
			p++;
			if (p < e && *p == '*') {
				p++;
				unsigned v = 0;
				const char *q = p;
				while (q < e && *q >= '0' && *q <= '9') v = v * 10 + (unsigned)(*q++ - '0');
				parg = q > p && q < e && *q == '$' && v ? (p = q + 1, (int)v - 1) : (int)next++;
			} else {
				parg = -1;
				while (p < e && *p >= '0' && *p <= '9') prec = prec * 10 + (uint64_t)(*p++ - '0');
			}
		}
		while (p < e && strchr("hlLqjzt", *p))
			p++;
		if (p >= e)
			break;
		char cv = *p;
		int kind;
		switch (cv) {
		case 's': case 'S': kind = 2; break;
		case 'P': kind = 3; break;
		case '@': kind = 4; break;
		case 'm': kind = 6; break;
		case 'n':
			cc_error(c, loc, "os_log() '%%n' format specifier is not allowed");
			return -1;
		case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'c': case 'C': case 'p':
		case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': case 'D': case 'O': case 'U':
			kind = 0; break;
		default:
			continue;	/* not a conversion: nothing (clang) */
		}
		if (mask)
			OSL_ADD(0x70, -1, maskv, 8);
		if (warg >= 0 && (unsigned)warg < nargs)
			OSL_ADD(0, warg, 0, 0);
		if (kind == 2 || kind == 3) {
			if (parg >= 0 && (unsigned)parg < nargs)
				OSL_ADD(0x10, parg, 0, 0);
			else if (parg == -1)
				OSL_ADD(0x10 | flags, -1, prec, 4);
		} else if (parg >= 0 && (unsigned)parg < nargs)
			OSL_ADD(0, parg, 0, 0);
		if (kind == 6) {
			OSL_ADD(0x60 | flags, -1, 0, 0);
			continue;
		}
		int a = pos >= 0 ? pos : (int)next++;
		if ((unsigned)a < nargs)
			OSL_ADD(kind << 4 | flags, a, 0, 0);
	}
#undef OSL_ADD
	for (int i = 0; i < n; i++)
		if (it[i].arg >= 0)
			it[i].size = (uint8_t)ty_size(c, N(args[it[i].arg])->ty);
	return n;
}

/* an argument after default promotions (its type gives the item size) */
static uint32_t oslog_arg(cc *c, uint32_t a)
{
	a = rvalue(c, decay(c, a));
	if (KIND(c, N(a)->ty) == TY_FLOAT)
		return cast_node(c, a, prim(TY_DOUBLE), CK_F2F);
	if (is_integer(c, N(a)->ty))
		return convert(c, a, promote(c, N(a)->ty));
	return a;
}

VPRIV uint32_t os_log_call(cc *c, int id, uint32_t loc)
{
	vec args;
	args_list(c, &args);
	unsigned first = id == BI_OS_LOG_FORMAT ? 1 : 0;
	if (args.n < first + 1) {
		arity(c, loc, first + 1, &args);
		return err_node(c, loc);
	}
	uint32_t s = args.v[first];
	while (N(s)->op == N_CAST)
		s = N(s)->a;
	if (N(s)->op != N_STR) {
		cc_error(c, N(args.v[first])->loc, "os_log() format must be a string literal");
		return err_node(c, loc);
	}
	const char *f = c->strs[N(s)->a].s;
	uint32_t *va = args.v + first + 1;
	unsigned nva = args.n - first - 1;
	for (unsigned i = 0; i < nva; i++)
		va[i] = oslog_arg(c, va[i]);
	oslog_item it[OSL_MAX];
	int n = oslog_layout(c, f, strlen(f), va, nva, it, loc);
	if (n < 0)
		return err_node(c, loc);
	uint64_t size = 2;
	uint8_t summary = 0;
	for (int i = 0; i < n; i++) {
		size += 2u + it[i].size;
		if (it[i].desc & 1)
			summary |= 1;
		if (it[i].desc >> 4)
			summary |= 2;
	}
	if (id == BI_OS_LOG_SIZE)
		return mk_int(c, size, c->t_size, loc);

	/* ({ unsigned char *p = buf; p[0] = summary; ...; *(T *)(p + k) = arg; ...; (void *)p; }) */
	vec body;
	vinit(&body);
	qty u8p = ty_ptr(c, prim(TY_UCHAR));
	uint32_t buf = rvalue(c, decay(c, args.v[0]));
	if (!is_ptr(c, N(buf)->ty)) {
		cc_error(c, N(args.v[0])->loc, "os_log() buffer must be a pointer");
		return err_node(c, loc);
	}
	uint32_t p = bitcount_local(c, &body, u8p, cast_node(c, buf, u8p, CK_NOP), loc);
#define OSL_AT(OFF) deref(c, binop(c, T_PLUS, rvalue(c, p), mk_int(c, (OFF), prim(TY_LONG), loc), loc), loc)
#define OSL_BYTE(OFF, V) bitcount_statement(c, &body, assign(c, 0, OSL_AT(OFF), mk_int(c, (V), prim(TY_UCHAR), loc), loc), loc)
	OSL_BYTE(0, summary);
	OSL_BYTE(1, (unsigned)n);
	uint64_t off = 2;
	for (int i = 0; i < n; i++) {
		OSL_BYTE(off, it[i].desc);
		OSL_BYTE(off + 1, it[i].size);
		off += 2;
		if (it[i].size) {
			uint32_t v = it[i].arg >= 0 ? va[it[i].arg]
			           : mk_int(c, (i128)it[i].imm, it[i].size == 8 ? prim(TY_ULLONG) : prim(TY_UINT), loc);
			qty t = N(v)->ty;
			uint32_t at = binop(c, T_PLUS, rvalue(c, p), mk_int(c, (i128)off, prim(TY_LONG), loc), loc);
			uint32_t dst = deref(c, cast_node(c, at, ty_ptr(c, QUNQ(t)), CK_NOP), loc);
			bitcount_statement(c, &body, assign(c, 0, dst, v, loc), loc);
			off += it[i].size;
		}
	}
	/* an argument without a conversion is not evaluated (clang) */
#undef OSL_BYTE
#undef OSL_AT
	qty vp = ty_ptr(c, prim(TY_VOID));
	bitcount_statement(c, &body, cast_node(c, rvalue(c, p), vp, CK_NOP), loc);
	uint32_t block = mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, &body), body.n);
	uint32_t out = mk(c, N_STMTEXPR, vp, loc);
	N(out)->a = block;
	return out;
}
