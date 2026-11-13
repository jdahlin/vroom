/* vcc front end: __builtin_os_log_format and _buffer_size (Apple's os/trace_base.h); see builtin-table.c for where a builtin's other facts live */
#include "cc-internal.h"

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
enum { CC_BUILTIN_OS_LOG_MAX = 64 };
typedef struct { uint8_t desc, size; int arg; uint64_t imm; } cc_builtin_os_log_item;

static int cc_builtin_os_log_layout(cc *c, const char *f, size_t len, const uint32_t *args, unsigned nargs,
                        cc_builtin_os_log_item *it, uint32_t loc)
{
	int n = 0;
	unsigned next = 0;
	const char *e = f + len;

#define CC_BUILTIN_OS_LOG_ADD(D, ARG, IMM, SZ) do { if (n == CC_BUILTIN_OS_LOG_MAX) { cc_report_error(c, loc, "os_log() format has too many arguments"); return -1; } \
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
					cc_report_error(c, loc, "mask type size must be between 1-byte and 8-bytes");
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
		/* AST_NODE$: the argument of this conversion */
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
			cc_report_error(c, loc, "os_log() '%%n' format specifier is not allowed");
			return -1;
		case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'c': case 'C': case 'p':
		case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A': case 'D': case 'O': case 'U':
			kind = 0; break;
		default:
			continue;	/* not a conversion: nothing (clang) */
		}
		if (mask)
			CC_BUILTIN_OS_LOG_ADD(0x70, -1, maskv, 8);
		if (warg >= 0 && (unsigned)warg < nargs)
			CC_BUILTIN_OS_LOG_ADD(0, warg, 0, 0);
		if (kind == 2 || kind == 3) {
			if (parg >= 0 && (unsigned)parg < nargs)
				CC_BUILTIN_OS_LOG_ADD(0x10, parg, 0, 0);
			else if (parg == -1)
				CC_BUILTIN_OS_LOG_ADD(0x10 | flags, -1, prec, 4);
		} else if (parg >= 0 && (unsigned)parg < nargs)
			CC_BUILTIN_OS_LOG_ADD(0, parg, 0, 0);
		if (kind == 6) {
			CC_BUILTIN_OS_LOG_ADD(0x60 | flags, -1, 0, 0);
			continue;
		}
		int a = pos >= 0 ? pos : (int)next++;
		if ((unsigned)a < nargs)
			CC_BUILTIN_OS_LOG_ADD(kind << 4 | flags, a, 0, 0);
	}
#undef CC_BUILTIN_OS_LOG_ADD
	for (int i = 0; i < n; i++)
		if (it[i].arg >= 0)
			it[i].size = (uint8_t)cc_type_size(c, AST_NODE(args[it[i].arg])->ty);
	return n;
}

/* an argument after default promotions (its type gives the item size) */
static uint32_t cc_builtin_os_log_argument(cc *c, uint32_t a)
{
	a = cc_node_rvalue(c, cc_conversion_decay(c, a));
	if (AST_TYPE_KIND(c, AST_NODE(a)->ty) == TY_FLOAT)
		return cc_cast_insert(c, a, cc_type_basic(TY_DOUBLE), CK_F2F);
	if (cc_type_is_integer(c, AST_NODE(a)->ty))
		return cc_conversion_apply(c, a, cc_type_promote(c, AST_NODE(a)->ty));
	return a;
}

CC_PRIVATE uint32_t cc_builtin_os_log_build(cc *c, int id, uint32_t loc)
{
	cc_id_list args;
	cc_builtin_arguments_parse(c, &args);
	unsigned first = id == BI_OS_LOG_FORMAT ? 1 : 0;
	if (args.n < first + 1) {
		cc_builtin_arity_check(c, loc, first + 1, &args);
		return cc_node_error_new(c, loc);
	}
	uint32_t s = args.v[first];
	while (AST_NODE(s)->op == N_CAST)
		s = AST_NODE(s)->a;
	if (AST_NODE(s)->op != N_STR) {
		cc_report_error(c, AST_NODE(args.v[first])->loc, "os_log() format must be a string literal");
		return cc_node_error_new(c, loc);
	}
	const char *f = c->strs[AST_NODE(s)->a].s;
	uint32_t *va = args.v + first + 1;
	unsigned nva = args.n - first - 1;
	for (unsigned i = 0; i < nva; i++)
		va[i] = cc_builtin_os_log_argument(c, va[i]);
	cc_builtin_os_log_item it[CC_BUILTIN_OS_LOG_MAX];
	int n = cc_builtin_os_log_layout(c, f, strlen(f), va, nva, it, loc);
	if (n < 0)
		return cc_node_error_new(c, loc);
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
		return cc_node_integer_new(c, size, c->t_size, loc);

	/* ({ unsigned char *p = buf; p[0] = summary; ...; *(AST_TYPE *)(p + k) = arg; ...; (void *)p; }) */
	cc_id_list body;
	cc_id_list_init(&body);
	qty u8p = cc_type_pointer(c, cc_type_basic(TY_UCHAR));
	uint32_t buf = cc_node_rvalue(c, cc_conversion_decay(c, args.v[0]));
	if (!cc_type_is_pointer(c, AST_NODE(buf)->ty)) {
		cc_report_error(c, AST_NODE(args.v[0])->loc, "os_log() buffer must be a pointer");
		return cc_node_error_new(c, loc);
	}
	uint32_t p = cc_lowering_local_new(c, &body, u8p, cc_cast_insert(c, buf, u8p, CK_NOP), loc);
#define CC_BUILTIN_OS_LOG_AT(OFF) cc_dereference_build(c, cc_binary_build(c, LEX_TOKEN_PLUS, cc_node_rvalue(c, p), cc_node_integer_new(c, (OFF), cc_type_basic(TY_LONG), loc), loc), loc)
#define CC_BUILTIN_OS_LOG_BYTE(OFF, V) cc_lowering_statement_append(c, &body, cc_assignment_build(c, 0, CC_BUILTIN_OS_LOG_AT(OFF), cc_node_integer_new(c, (V), cc_type_basic(TY_UCHAR), loc), loc), loc)
	CC_BUILTIN_OS_LOG_BYTE(0, summary);
	CC_BUILTIN_OS_LOG_BYTE(1, (unsigned)n);
	uint64_t off = 2;
	for (int i = 0; i < n; i++) {
		CC_BUILTIN_OS_LOG_BYTE(off, it[i].desc);
		CC_BUILTIN_OS_LOG_BYTE(off + 1, it[i].size);
		off += 2;
		if (it[i].size) {
			uint32_t v = it[i].arg >= 0 ? va[it[i].arg]
			           : cc_node_integer_new(c, (i128)it[i].imm, it[i].size == 8 ? cc_type_basic(TY_ULLONG) : cc_type_basic(TY_UINT), loc);
			qty t = AST_NODE(v)->ty;
			uint32_t at = cc_binary_build(c, LEX_TOKEN_PLUS, cc_node_rvalue(c, p), cc_node_integer_new(c, (i128)off, cc_type_basic(TY_LONG), loc), loc);
			uint32_t dst = cc_dereference_build(c, cc_cast_insert(c, at, cc_type_pointer(c, QUNQ(t)), CK_NOP), loc);
			cc_lowering_statement_append(c, &body, cc_assignment_build(c, 0, dst, v, loc), loc);
			off += it[i].size;
		}
	}
	/* an argument without a conversion is not evaluated (clang) */
#undef CC_BUILTIN_OS_LOG_BYTE
#undef CC_BUILTIN_OS_LOG_AT
	qty vp = cc_type_pointer(c, cc_type_basic(TY_VOID));
	cc_lowering_statement_append(c, &body, cc_cast_insert(c, cc_node_rvalue(c, p), vp, CK_NOP), loc);
	uint32_t block = cc_statement_new(c, N_BLOCK, loc, 0, cc_id_list_freeze(c, &body), body.n);
	uint32_t out = cc_node_new(c, N_STMTEXPR, vp, loc);
	AST_NODE(out)->a = block;
	return out;
}
