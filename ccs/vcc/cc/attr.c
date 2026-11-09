/* vcc front end: attributes (GNU, C23, asm labels) */
#include "ccimpl.h"

/* ---------------- attributes ---------------- */

enum {
	A_NONE, A_ALIGNED, A_PACKED, A_SECTION, A_MODE, A_CLEANUP, A_ALIAS, A_WEAK, A_USED, A_UNUSED,
	A_NORETURN, A_ALWAYS_INLINE, A_NOINLINE, A_GNU_INLINE, A_TRANSPARENT_UNION, A_VISIBILITY,
	A_CONSTRUCTOR, A_DESTRUCTOR, A_COUNTED_BY, A_VECTOR_SIZE, A_COLD, A_NONSTRING, A_MAY_ALIAS,
	A_WEAKREF, A_NAKED, A_NOCF, A_ERROR, A_OVERLOADABLE, A_NOINSTR, A_TLS_MODEL,
	A_COUNT
};

static const char *const attr_names[A_COUNT] = {
	[A_ALIGNED] = "aligned", [A_PACKED] = "packed", [A_SECTION] = "section", [A_MODE] = "mode",
	[A_CLEANUP] = "cleanup", [A_ALIAS] = "alias", [A_WEAK] = "weak", [A_USED] = "used",
	[A_UNUSED] = "unused", [A_NORETURN] = "noreturn", [A_ALWAYS_INLINE] = "always_inline",
	[A_NOINLINE] = "noinline", [A_GNU_INLINE] = "gnu_inline", [A_TRANSPARENT_UNION] = "transparent_union",
	[A_VISIBILITY] = "visibility", [A_CONSTRUCTOR] = "constructor", [A_DESTRUCTOR] = "destructor",
	[A_COUNTED_BY] = "counted_by", [A_VECTOR_SIZE] = "vector_size", [A_COLD] = "cold",
	[A_NONSTRING] = "nonstring", [A_MAY_ALIAS] = "may_alias", [A_WEAKREF] = "weakref",
	[A_NOINSTR] = "no_instrument_function", [A_NAKED] = "naked", [A_NOCF] = "nocf_check", [A_ERROR] = "error",
	[A_OVERLOADABLE] = "overloadable", [A_TLS_MODEL] = "tls_model",
};

static uint32_t *attr_dir[NDIR];

VPRIV void init_attrs(void)
{
	static bool done;

	if (__atomic_load_n(&done, __ATOMIC_ACQUIRE))
		return;
	for (int i = 1; i < A_COUNT; i++) {
		char b[2][64];
		size_t l = strlen(attr_names[i]);

		memset(b, 0, sizeof(b));
		memcpy(b[0], attr_names[i], l);
		b[1][0] = b[1][1] = '_';
		memcpy(b[1] + 2, attr_names[i], l);
		b[1][l + 2] = b[1][l + 3] = '_';
		for (int k = 0; k < 2; k++) {
			uint32_t id = vintern_copy(b[k], k ? l + 4 : l);

			if (!attr_dir[id >> DIR_BITS])
				attr_dir[id >> DIR_BITS] = calloc(DIR_PAGE, sizeof(uint32_t));
			attr_dir[id >> DIR_BITS][id & (DIR_PAGE - 1)] = (uint32_t)i;
		}
	}
	__atomic_store_n(&done, true, __ATOMIC_RELEASE);
}

static int attr_of(uint32_t id)
{
	uint32_t *pg = attr_dir[id >> DIR_BITS];

	return pg ? (int)pg[id & (DIR_PAGE - 1)] : 0;
}

VPRIV void skip_balanced(cc *c)
{
	int depth = 0;

	do {
		if (is(c, T_LPAREN))
			depth++;
		else if (is(c, T_RPAREN))
			depth--;
		else if (is(c, T_EOF))
			fatal(c, tok(c)->loc, "unexpected end of file in attribute");
		next(c);
	} while (depth > 0);
}

VPRIV uint32_t string_index(cc *c)
{
	uint32_t n;

	if (!is_strtok(tok(c)->kind)) {
		cc_error(c, tok(c)->loc, "expected string literal");
		return 0;
	}
	n = string_lit(c);
	return N(n)->a + 1;
}

VPRIV int mode_kind(cc *c, uint32_t id, bool sgn)
{
	int l;
	const char *s = idname(id, &l);
	char b[16] = { 0 };

	if (l > 2 && s[0] == '_' && s[1] == '_')
		s += 2, l -= 4;
	if (l <= 0 || l >= 15)
		return 0;
	memcpy(b, s, (size_t)l);
	(void)c;
	if (!strcmp(b, "QI")) return sgn ? TY_SCHAR : TY_UCHAR;
	if (!strcmp(b, "HI")) return sgn ? TY_SHORT : TY_USHORT;
	if (!strcmp(b, "SI")) return sgn ? TY_INT : TY_UINT;
	if (!strcmp(b, "DI")) return CC_I64(c, !sgn);
	if (!strcmp(b, "word") || !strcmp(b, "pointer")) return sgn ? TY_LONG : TY_ULONG;	/* the pointer's width */
	if (!strcmp(b, "TI")) return sgn ? TY_INT128 : TY_UINT128;
	return 0;
}

/* GNU __attribute__((...)) and C23 [[...]] (gnu::name is the GNU attribute) */
__attribute__((noinline)) VPRIV void attribute_list_(cc *c, attrs *a)
{
	for (;;) {
		bool std = at_std_attr(c);
		int close = std ? T_RSQUARE : T_RPAREN;
		const char *cs = std ? "']'" : "')'";

		if (!std && !iskw(c, KW___attribute__) && !iskw(c, KW___attribute))
			break;
		next(c);
		if (std) {
			next(c);
		} else {
			expect(c, T_LPAREN, "'('");
			expect(c, T_LPAREN, "'('");
		}
		while (!is(c, close)) {
			ptok *t = tok(c);
			int an;
			uint32_t loc = t->loc;

			if (accept(c, T_COMMA))
				continue;
			if (t->kind != T_IDENT && !(std && kwof(t)))
				fatal(c, loc, "expected attribute name");
			if (std && peek(c, 1)->kind == T_COLONCOLON) {
				/* vendor::name: gnu and clang name GNU attributes, others are ignored */
				const vident *ns = &vintern_ents[t->id];
				bool gnu = (ns->len == 3 && !memcmp(ns->s, "gnu", 3)) || (ns->len == 7 && !memcmp(ns->s, "__gnu__", 7)) ||
					   (ns->len == 5 && !memcmp(ns->s, "clang", 5));

				next(c);
				next(c);
				t = tok(c);
				loc = t->loc;
				if (t->kind != T_IDENT && !kwof(t))
					fatal(c, loc, "expected attribute name");
				if (!gnu) {
					next(c);
					if (is(c, T_LPAREN))
						skip_balanced(c);
					continue;
				}
			}
			an = attr_of(t->id);
			if (!an && kwof(t) == KW_const)
				an = A_NONE;
			next(c);
			switch (an) {
			case A_ALIGNED:
				if (accept(c, T_LPAREN)) {
					i128 v;

					const_expr(c, &v, NULL);
					expect(c, T_RPAREN, "')'");
					if (v <= 0 || (v & (v - 1)))
						cc_error(c, loc, "requested alignment is not a power of 2");
					else if ((uint32_t)v > a->aligned)
						a->aligned = (uint32_t)v;
				} else if (16 > a->aligned) {
					a->aligned = 16;
				}
				continue;
			case A_SECTION:
			case A_ALIAS:
			case A_VISIBILITY:
				expect(c, T_LPAREN, "'('");
				{
					uint32_t sloc = tok(c)->loc;
					uint32_t s = string_index(c);

					if (an == A_SECTION && c->o->target == PP_DARWIN && s && !strchr(c->strs[s - 1].s, ','))
						cc_error(c, sloc, "argument to 'section' attribute is not valid for this target: "
							 "mach-o section specifier requires a segment and section separated by a comma");
					if (an == A_SECTION)
						a->section = s;
					else if (an == A_ALIAS)
						a->alias = s;
					else
						a->visibility = s;
				}
				expect(c, T_RPAREN, "')'");
				continue;
			case A_TLS_MODEL:
				expect(c, T_LPAREN, "'('");
				{
					uint32_t loc = tok(c)->loc;

					a->tlsmodel = (uint8_t)vcc_tls_model(c->strs[string_index(c) - 1].s);
					if (!a->tlsmodel)
						cc_error(c, loc, "tls_model must be \"global-dynamic\", \"local-dynamic\", \"initial-exec\" or \"local-exec\"");
				}
				expect(c, T_RPAREN, "')'");
				continue;
			case A_MODE:
				expect(c, T_LPAREN, "'('");
				if (is(c, T_IDENT))
					a->mode = tok(c)->id;
				next(c);
				expect(c, T_RPAREN, "')'");
				continue;
			case A_CLEANUP:
				expect(c, T_LPAREN, "'('");
				if (is(c, T_IDENT)) {
					uint32_t s = lookup(c, tok(c)->id);

					if (!s || c->syms[s].kind != S_FUNC)
						cc_error(c, tok(c)->loc, "'cleanup' argument is not a function");
					a->cleanup = s;
					if (s)
						c->syms[s].flags |= SF_REF;
				}
				next(c);
				expect(c, T_RPAREN, "')'");
				continue;
			case A_COUNTED_BY:
				expect(c, T_LPAREN, "'('");
				if (is(c, T_IDENT))
					a->counted_by = tok(c)->id;
				next(c);
				expect(c, T_RPAREN, "')'");
				continue;
			case A_VECTOR_SIZE: {
				i128 v;

				expect(c, T_LPAREN, "'('");
				const_expr(c, &v, NULL);
				expect(c, T_RPAREN, "')'");
				if (v <= 0 || (v & (v - 1)) || v > 1024)
					cc_error(c, loc, "invalid vector size");
				else
					a->vecsize = (uint32_t)v;
				continue;
			}
			case A_PACKED: a->packed = 1; break;
			case A_WEAK: a->weak = 1; break;
			case A_USED: a->used = 1; break;
			case A_UNUSED: a->unused = 1; break;
			case A_NORETURN: a->noreturn = 1; break;
			case A_ALWAYS_INLINE: a->always_inline = 1; break;
			case A_NOINLINE: a->noinline = 1; break;
			case A_NOINSTR: a->noinstr = 1; break;
			case A_OVERLOADABLE: a->overloadable = 1; break;
			case A_GNU_INLINE: a->gnu_inline = 1; break;
			case A_TRANSPARENT_UNION: a->transparent = 1; break;
			case A_CONSTRUCTOR:
			case A_DESTRUCTOR:
				if (an == A_CONSTRUCTOR)
					a->constructor = 1;
				else
					a->destructor = 1;
				if (is(c, T_LPAREN)) {
					/* the priority: lower runs first */
					i128 v;

					next(c);
					const_expr(c, &v, NULL);
					expect(c, T_RPAREN, "')'");
					if (v < 0 || v > 65535)
						cc_error(c, loc, "'%s' priority out of range", an == A_CONSTRUCTOR ? "constructor" : "destructor");
					else
						a->prio = (uint16_t)v;
				}
				continue;
			case A_COLD: a->cold = 1; break;
			case A_NONSTRING: a->nonstring = 1; break;
			case A_MAY_ALIAS: a->may_alias = 1; break;
			case A_ERROR:	/* a call left after folding is a build error */
				a->error_fn = 1;
				if (is(c, T_LPAREN) && is_strtok(peek(c, 1)->kind)) {
					next(c);
					a->errmsg = string_index(c);
					expect(c, T_RPAREN, "')'");
				}
				break;
			default: break;
			}
			if (is(c, T_LPAREN))
				skip_balanced(c);
		}
		expect(c, close, cs);
		expect(c, close, cs);
	}
}

/* the 0/1 flags packed .. noinstr are 18 contiguous bytes after the u32 fields */
_Static_assert(offsetof(attrs, packed) == 11 * sizeof(uint32_t) &&
	       offsetof(attrs, noinstr) == offsetof(attrs, packed) + 17, "attrs layout");

VPRIV void merge_attrs(attrs *to, const attrs *from)
{
	if (attrs_empty(from))
		return;
	if (from->aligned > to->aligned)
		to->aligned = from->aligned;
	if (from->section) to->section = from->section;
	if (from->alias) to->alias = from->alias;
	if (from->asmname) to->asmname = from->asmname;
	if (from->cleanup) to->cleanup = from->cleanup;
	if (from->mode) to->mode = from->mode;
	if (from->vecsize) to->vecsize = from->vecsize;
	if (from->counted_by) to->counted_by = from->counted_by;
	if (from->visibility) to->visibility = from->visibility;
	if (from->errmsg) to->errmsg = from->errmsg;
	if (from->prio) to->prio = from->prio;
	if (from->tlsmodel) to->tlsmodel = from->tlsmodel;
	/* the 0/1 flags packed .. noinstr: or them bytewise */
	{
		char *t = (char *)&to->packed;
		const char *f = (const char *)&from->packed;
		uint64_t a, b;

		memcpy(&a, t, 8), memcpy(&b, f, 8), a |= b, memcpy(t, &a, 8);
		memcpy(&a, t + 8, 8), memcpy(&b, f + 8, 8), a |= b, memcpy(t + 8, &a, 8);
		t[16] |= f[16], t[17] |= f[17];
	}
}

/* GNU asm("label") after a declarator */
VPRIV void asm_label(cc *c, attrs *a)
{
	int kw = kwof(tok(c));

	if (kw != KW_asm && kw != KW___asm__ && kw != KW___asm)
		return;
	next(c);
	expect(c, T_LPAREN, "'('");
	a->asmname = string_index(c);
	expect(c, T_RPAREN, "')'");
}
