/* vcc front end: attribute tables, parsing, merging, typo correction and asm labels */
#include "ccimpl.h"

/* ---------------- attributes ---------------- */

enum {
	A_NONE, A_INTERRUPT, A_MS_STRUCT, A_ALIGNED, A_PACKED, A_SECTION, A_MODE, A_CLEANUP, A_ALIAS, A_WEAK, A_USED, A_UNUSED,
	A_NORETURN, A_ALWAYS_INLINE, A_NOINLINE, A_GNU_INLINE, A_TRANSPARENT_UNION, A_VISIBILITY,
	A_CONSTRUCTOR, A_DESTRUCTOR, A_COUNTED_BY, A_VECTOR_SIZE, A_COLD, A_NONSTRING, A_MAY_ALIAS,
	A_WEAKREF, A_NAKED, A_NOCF, A_ERROR, A_OVERLOADABLE, A_NOINSTR, A_TLS_MODEL, A_EXT_VECTOR, A_DEPRECATED, A_WUR, A_NODISCARD, A_IFUNC, A_NONNULL, A_FORMAT, A_FORMAT_ARG,
	A_PURE, A_CONST, A_UNAVAILABLE, A_TARGET, A_RETURNS_TWICE, A_DISABLE_TAIL_CALLS, A_NOT_TAIL_CALLED, A_OPTNONE,
	A_COUNT
};

static const char *const attr_names[A_COUNT] = {
	[A_INTERRUPT] = "interrupt",
	[A_MS_STRUCT] = "ms_struct", [A_ALIGNED] = "aligned", [A_PACKED] = "packed", [A_SECTION] = "section", [A_MODE] = "mode",
	[A_CLEANUP] = "cleanup", [A_ALIAS] = "alias", [A_WEAK] = "weak", [A_USED] = "used",
	[A_UNUSED] = "unused", [A_NORETURN] = "noreturn", [A_ALWAYS_INLINE] = "always_inline",
	[A_NOINLINE] = "noinline", [A_GNU_INLINE] = "gnu_inline", [A_TRANSPARENT_UNION] = "transparent_union",
	[A_VISIBILITY] = "visibility", [A_CONSTRUCTOR] = "constructor", [A_DESTRUCTOR] = "destructor",
	[A_COUNTED_BY] = "counted_by", [A_VECTOR_SIZE] = "vector_size", [A_COLD] = "cold",
	[A_NONSTRING] = "nonstring", [A_MAY_ALIAS] = "may_alias", [A_WEAKREF] = "weakref",
	[A_NOINSTR] = "no_instrument_function", [A_NAKED] = "naked", [A_NOCF] = "nocf_check", [A_ERROR] = "error",
	[A_OVERLOADABLE] = "overloadable", [A_TLS_MODEL] = "tls_model", [A_EXT_VECTOR] = "ext_vector_type",
	[A_TARGET] = "target", [A_PURE] = "pure", [A_CONST] = "const", [A_UNAVAILABLE] = "unavailable",
	[A_RETURNS_TWICE] = "returns_twice", [A_DISABLE_TAIL_CALLS] = "disable_tail_calls",
	[A_NOT_TAIL_CALLED] = "not_tail_called", [A_OPTNONE] = "optnone",
	[A_DEPRECATED] = "deprecated", [A_WUR] = "warn_unused_result", [A_NODISCARD] = "nodiscard", [A_IFUNC] = "ifunc", [A_NONNULL] = "nonnull", [A_FORMAT] = "format", [A_FORMAT_ARG] = "format_arg",
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

VPRIV uint32_t skip_balanced(cc *c)
{
	int depth = 0;
	uint32_t last;

	do {	/* (...), [...] or {...} (an initializer's braces) */
		int k = tok(c)->kind;

		last = tok(c)->loc;
		if (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_LEFT_SQUARE || k == LEX_TOKEN_LEFT_BRACE)
			depth++;
		else if (k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_RIGHT_SQUARE || k == LEX_TOKEN_RIGHT_BRACE)
			depth--;
		else if (is(c, LEX_TOKEN_EOF))
			fatal(c, tok(c)->loc, "unexpected end of file in attribute");
		next(c);
	} while (depth > 0);
	return last;
}

/* a string literal argument; attr: the attribute's name (0: none).  On an error the
   argument is skipped up to its ')', so the caller's expect() does not report again */
VPRIV uint32_t string_index(cc *c, uint32_t attr)
{
	uint32_t n;

	if (!is_strtok(tok(c)->kind)) {
		if (attr) {
			int l;
			const char *s = idname(attr, &l);	/* as written: clang keeps '__alias__' */

			cc_error(c, tok(c)->loc, "expected string literal as argument of '%.*s' attribute", l, s);
			for (int d = 0; !is(c, LEX_TOKEN_EOF) && !is(c, LEX_TOKEN_SEMICOLON) && (d || !is(c, LEX_TOKEN_RIGHT_PAREN)); next(c))
				d += is(c, LEX_TOKEN_LEFT_PAREN) - is(c, LEX_TOKEN_RIGHT_PAREN);
		} else {
			cc_error(c, tok(c)->loc, "expected string literal");
		}
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
	if (!strcmp(b, "SF")) return TY_FLOAT;
	if (!strcmp(b, "DF")) return TY_DOUBLE;
	if (!strcmp(b, "QI")) return sgn ? TY_SCHAR : TY_UCHAR;
	if (!strcmp(b, "HI")) return sgn ? TY_SHORT : TY_USHORT;
	if (!strcmp(b, "SI")) return sgn ? TY_INT : TY_UINT;
	if (!strcmp(b, "DI")) return CC_I64(c, !sgn);
	if (!strcmp(b, "word") || !strcmp(b, "pointer")) return sgn ? TY_LONG : TY_ULONG;	/* the pointer's width */
	if (!strcmp(b, "TI")) return sgn ? TY_INT128 : TY_UINT128;
	return 0;
}

#include "attrnames.inc"
#include "attrnames-x86.inc"

/* x86-64: name in one of its sorted lists (gen-x86data.sh) */
static bool attr_x86(const char *const *tab, size_t n, const char *k, size_t kn)
{
	size_t lo = 0, hi = n;

	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int r = strncmp(tab[mid], k, kn);

		if (!r && tab[mid][kn])
			r = 1;
		if (!r)
			return true;
		if (r < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return false;
}

/* name without __x__, as clang normalises it */
static void attr_norm(const char **s, size_t *n)
{
	if (*n >= 4 && (*s)[0] == '_' && (*s)[1] == '_' && (*s)[*n - 2] == '_' && (*s)[*n - 1] == '_')
		*s += 2, *n -= 4;
}

/* scope of a [[...]] attribute as clang prints it: "" (none), "gnu::", "clang::" */
static const char *const attr_scopes[] = { "", "gnu::", "clang::" };

/*
 * whether clang knows attribute s for the target: GNU syntax (scope < 0, its
 * __has_attribute), or [[scope::s]] (__has_c_attribute)
 */
static bool attr_known(cc *c, int scope, const char *s, size_t n)
{
	int bit = 1 << (c->o->target == PP_WASM32 ? PP_WASM64 : c->o->target);
	size_t lo = 0, hi = scope < 0 ? sizeof(gnu_attrs) / sizeof(gnu_attrs[0]) : sizeof(c_attrs) / sizeof(c_attrs[0]);
	char k[160];

	attr_norm(&s, &n);
	if (scope >= 0) {
		int l = snprintf(k, sizeof(k), "%s%.*s", attr_scopes[scope], (int)n, s);

		if (l < 0 || (size_t)l >= sizeof(k))
			return false;
		if (c->o->target == PP_LINUX_X86_64)
			return attr_x86(c_attrs_x86, sizeof(c_attrs_x86) / sizeof(*c_attrs_x86), k, (size_t)l);
		while (lo < hi) {
			size_t mid = (lo + hi) / 2;
			int r = strcmp(c_attrs[mid].name, k);

			if (!r)
				return c_attrs[mid].on & bit;
			if (r < 0)
				lo = mid + 1;
			else
				hi = mid;
		}
		return false;
	}
	if (c->o->target == PP_LINUX_X86_64)
		return attr_x86(gnu_attrs_x86, sizeof(gnu_attrs_x86) / sizeof(*gnu_attrs_x86), s, n);
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int r = strncmp(gnu_attrs[mid].name, s, n);

		if (!r && gnu_attrs[mid].name[n])
			r = 1;
		if (!r)
			return gnu_attrs[mid].on & bit;
		if (r < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return false;
}

/* Levenshtein distance (clang's StringRef::edit_distance with replacements) */
static unsigned edit_distance(const char *a, size_t na, const char *b, size_t nb)
{
	unsigned row[128];

	if (nb >= 128)
		return ~0u;
	for (size_t j = 0; j <= nb; j++)
		row[j] = (unsigned)j;
	for (size_t i = 1; i <= na; i++) {
		unsigned prev = row[0];

		row[0] = (unsigned)i;
		for (size_t j = 1; j <= nb; j++) {
			unsigned old = row[j], v = prev + (a[i - 1] != b[j - 1]);

			if (row[j - 1] + 1 < v)
				v = row[j - 1] + 1;
			if (old + 1 < v)
				v = old + 1;
			row[j] = v;
			prev = old;
		}
	}
	return row[nb];
}

/*
 * clang's tryGetCorrectedAttrName with its SimpleTypoCorrection: the first
 * nearest spelling within (n + 2) / 3 edits, candidates whose length differs
 * by d skipped unless n / d >= 3; then only if clang knows it.
 */
static const char *attr_correction(cc *c, int scope, const char *s, size_t n)
{
	unsigned max, best;
	const char *bn = NULL;

	attr_norm(&s, &n);
	max = (unsigned)(n + 2) / 3, best = max + 1;
	for (size_t i = 0; i < sizeof(attr_spellings) / sizeof(attr_spellings[0]); i++) {
		size_t cn = strlen(attr_spellings[i]), ld = cn > n ? cn - n : n - cn;
		unsigned d;

		if (ld && n / ld < 3)
			continue;
		d = edit_distance(s, n, attr_spellings[i], cn);
		if (d < best)
			best = d, bn = attr_spellings[i];
	}
	if (!bn || !best || !attr_known(c, scope, bn, strlen(bn)))
		return NULL;
	return bn;
}

/* GNU __attribute__((...)) and C23 [[...]] (gnu::name is the GNU attribute) */
__attribute__((noinline)) VPRIV void attribute_list_(cc *c, attrs *a)
{
	for (;;) {
		bool std = at_std_attr(c);
		int close = std ? LEX_TOKEN_RIGHT_SQUARE : LEX_TOKEN_RIGHT_PAREN;
		const char *cs = std ? "']'" : "')'";

		if (!std && !iskw(c, KW___attribute__) && !iskw(c, KW___attribute))
			break;
		bool srcbr = std && !(tok(c)->flags & PP_TOKEN_FLAG_FROM_MACRO);	/* [[ written in the source */

		next(c);
		if (std) {
			next(c);
		} else {
			expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
			expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
		}
		while (!is(c, close)) {
			ptok *t = tok(c);
			int an, scope = std ? 0 : -1;	/* attr_scopes index; -1: GNU syntax */
			uint32_t loc = t->loc, sloc = t->loc, sid = 0, aid;

			if (accept(c, LEX_TOKEN_COMMA))
				continue;
			if (std && t->kind == LEX_TOKEN_NUMBER && t->len == 1 && t->s[0] == '1' && (t->flags & PP_TOKEN_FLAG_FROM_MACRO) &&
			    peek(c, 1)->kind == LEX_TOKEN_COLON_COLON && srcbr && pp_location_spells(c->p, loc, "__clang__", 9)) {
				/* [[__clang__::x]] as written (not through a macro): clang warns and takes it as clang:: */
				diag_span r = { loc, pp_token_end_find(c->p, loc) };
				diag_fixit fx = { loc, pp_token_end_find(c->p, loc), "_Clang", DIAG_APPLICABILITY_NAME };	/* (__clang__ itself) */

				diag_info d = { "warning", loc, &r, 1, &fx, 1, "1", 1 };	/* the note at the macro's body */

				diag_warning_report(c->p, "ignored-attributes", true, NULL, &d,
					 "'__clang__' is a predefined macro name, not an attribute scope specifier; did you mean '_Clang' instead?");
				t->kind = LEX_TOKEN_IDENTIFIER, t->id = vintern_cstr("_Clang");
			}
			if (t->kind != LEX_TOKEN_IDENTIFIER && !(std && kwof(t)))
				expect(c, close, cs);	/* clang: expected ']' or ')' */
			if (std && peek(c, 1)->kind == LEX_TOKEN_COLON_COLON) {
				/* vendor::name: gnu and clang name GNU attributes, others are ignored */
				const vident *ns = &vintern_ents[t->id];
				bool cl = (ns->len == 5 && !memcmp(ns->s, "clang", 5)) || (ns->len == 6 && !memcmp(ns->s, "_Clang", 6));
				bool gnu = (ns->len == 3 && !memcmp(ns->s, "gnu", 3)) || (ns->len == 7 && !memcmp(ns->s, "__gnu__", 7)) || cl;

				sloc = t->loc, sid = t->id;
				scope = cl ? 2 : gnu ? 1 : 3;

				next(c);
				next(c);
				t = tok(c);
				loc = t->loc;
				if (t->kind != LEX_TOKEN_IDENTIFIER && !kwof(t))
					fatal(c, loc, "expected attribute name");
				if (!gnu) {	/* other vendors: ignored, with clang's warning */
					uint32_t end = t->loc;
					const vident *sn = &vintern_ents[sid], *nm = &vintern_ents[t->id];

					next(c);
					if (is(c, LEX_TOKEN_LEFT_PAREN))
						end = skip_balanced(c);
					{
						diag_span r = { sloc, pp_token_end_find(c->p, end) };

						cc_warnx(c, "unknown-attributes", true, NULL, sloc, &r, 1, NULL, 0,
							 "unknown attribute '%.*s::%.*s' ignored", (int)sn->len, sn->s, (int)nm->len, nm->s);
					}
					continue;
				}
			}
			aid = t->id;	/* t is gone after next() */
			an = attr_of(aid);
			if (!an && kwof(t) == KW_const)
				an = A_NONE;
			const uint32_t tid = t->id;	/* t is the queue head: stale after next() */

			next(c);
			switch (an) {
			case A_ALIGNED:
				if (accept(c, LEX_TOKEN_LEFT_PAREN)) {
					i128 v;

					const_expr(c, &v, NULL);
					expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
					if (v <= 0 || (v & (v - 1)))
						cc_error(c, loc, "requested alignment is not a power of 2");
					else if ((uint32_t)v > a->aligned)
						a->aligned = (uint32_t)v;
				} else if (16 > a->aligned) {
					a->aligned = 16;
				}
				continue;
			case A_TARGET: {
				expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				uint32_t ts = string_index(c, aid);
				expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				target_attr(c, a, ts, loc);
				continue;
			}
			case A_IFUNC:
				a->ifunc = 1;
				an = A_ALIAS;
				__attribute__((fallthrough));
			case A_SECTION:
			case A_ALIAS:
			case A_VISIBILITY:
				expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				{
					uint32_t sloc = tok(c)->loc;
					uint32_t s = string_index(c, aid);

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
				expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				continue;
			case A_TLS_MODEL: {
				if (!accept(c, LEX_TOKEN_LEFT_PAREN)) {
					cc_error(c, loc, "'tls_model' attribute takes one argument");
					continue;
				}
				uint32_t argloc = tok(c)->loc, si = 0;
				bool empty = is(c, LEX_TOKEN_RIGHT_PAREN);

				if (!empty)
					si = string_index(c, aid);
				if (empty || is(c, LEX_TOKEN_COMMA)) {
					cc_error(c, loc, "'tls_model' attribute takes one argument");
					while (!is(c, LEX_TOKEN_RIGHT_PAREN) && !is(c, LEX_TOKEN_EOF) && !is(c, LEX_TOKEN_SEMICOLON)) {
						if (is(c, LEX_TOKEN_LEFT_PAREN)) skip_balanced(c);
						else next(c);
					}
				} else if (si) {
					uint8_t model = (uint8_t)vcc_tls_model(c->strs[si - 1].s);

					if (!model)
						cc_error(c, argloc, "tls_model must be \"global-dynamic\", \"local-dynamic\", \"initial-exec\" or \"local-exec\"");
					else
						a->tlsmodel = model, a->tlsloc = loc;
				}
				expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				continue;
			}
			case A_MODE:
				expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				if (is(c, LEX_TOKEN_IDENTIFIER))
					a->mode = tok(c)->id;
				next(c);
				expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				continue;
			case A_CLEANUP:
				expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				if (is(c, LEX_TOKEN_IDENTIFIER)) {
					uint32_t s = lookup(c, tok(c)->id);

					if (!s || c->syms[s].kind != S_FUNC)
						cc_error(c, tok(c)->loc, "'cleanup' argument is not a function");
					a->cleanup = s;
					if (s)
						c->syms[s].flags |= SF_REF | SF_USED;
				}
				next(c);
				expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				continue;
			case A_COUNTED_BY:
				expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				if (is(c, LEX_TOKEN_IDENTIFIER))
					a->counted_by = tok(c)->id;
				next(c);
				expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				continue;
			case A_PURE:
			case A_CONST:	/* (-Wunused-value: a discarded call) */
				a->pure = an == A_PURE ? 1 : 2;
				break;
			case A_VECTOR_SIZE: {
				i128 v;

				expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				const_expr(c, &v, NULL);
				expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				if (v <= 0 || (v & (v - 1)) || v > 1024)
					cc_error(c, loc, "invalid vector size");
				else
					a->vecsize = (uint32_t)v;
				continue;
			}
			case A_DEPRECATED:	/* deprecated or deprecated("msg"[, "replacement"]) */
			case A_UNAVAILABLE:	/* unavailable or unavailable("msg") */
				a->depr = loc, a->deprmsg = 0, a->unavail = an == A_UNAVAILABLE;
				if (is(c, LEX_TOKEN_LEFT_PAREN) && is_strtok(peek(c, 1)->kind)) {
					next(c);
					a->deprmsg = string_index(c, aid);
					if (accept(c, LEX_TOKEN_COMMA) && is_strtok(tok(c)->kind))
						string_index(c, aid);	/* (the replacement, for a fix-it) */
					expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				}
				break;
			case A_WUR: a->wur = 1; break;
			case A_NONNULL:	/* nonnull or nonnull(i, ...) */
				if (!accept(c, LEX_TOKEN_LEFT_PAREN)) {
					a->nonnull |= 1;
					continue;
				}
				if (accept(c, LEX_TOKEN_RIGHT_PAREN)) {
					a->nonnull |= 1;
					continue;
				}
				for (;;) {
					i128 v;

					const_expr(c, &v, NULL);
					if (v >= 1 && v <= 31)
						a->nonnull |= 1u << v;
					if (!accept(c, LEX_TOKEN_COMMA))
						break;
				}
				expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				continue;
			case A_FORMAT_ARG: {	/* format_arg(i): the call returns its i-th argument's format */
				i128 v = 0;

				expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				const_expr(c, &v, NULL);
				expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				a->fmtarg = v >= 1 && v <= 255 ? (uint8_t)v : 0;
				continue;
			}
			case A_FORMAT: {	/* format(archetype, fmt, first): printf checked by -Wformat */
				uint32_t arch = 0;
				i128 fi = 0, fa = 0;

				expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				if (is(c, LEX_TOKEN_IDENTIFIER)) {
					size_t l = idlen_(tok(c)->id);
					const char *n = idstr_(tok(c)->id);

					if ((l == 6 && !memcmp(n, "printf", 6)) || (l == 10 && !memcmp(n, "__printf__", 10)) ||
					    (l == 10 && !memcmp(n, "gnu_printf", 10)) || (l == 14 && !memcmp(n, "__gnu_printf__", 14)))
						arch = 1;
					next(c);
				}
				if (accept(c, LEX_TOKEN_COMMA)) {
					const_expr(c, &fi, NULL);
					if (accept(c, LEX_TOKEN_COMMA))
						const_expr(c, &fa, NULL);
				}
				expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				a->fmt = arch && fi >= 1 && fi <= 255 && fa >= 0 && fa <= 255 ? arch << 16 | (uint32_t)fi << 8 | (uint32_t)fa : 0;
				continue;
			}
			case A_NODISCARD: a->wur = 2; break;
			case A_EXT_VECTOR: {	/* ext_vector_type(N): N elements (typedefs only) */
				i128 v;

				expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				const_expr(c, &v, NULL);
				expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				if (v <= 0 || v > 4096)
					cc_error(c, loc, "invalid vector size");
				else
					a->extvec = (uint32_t)v;
				continue;
			}
			case A_MS_STRUCT: a->ms_struct = 1; break;
			case A_PACKED: a->packed = 1; break;
			case A_WEAK: a->weak = 1; break;
			case A_USED: a->used = 1; break;
			case A_UNUSED: a->unused = 1; break;
			case A_NORETURN: a->noreturn = 1; break;
			case A_ALWAYS_INLINE: a->always_inline = 1; break;
			case A_INTERRUPT: a->interrupt = 1; a->noinline = 1; break;
			case A_NOINLINE: a->noinline = 1; break;
			case A_NOINSTR: a->noinstr = 1; break;
			case A_RETURNS_TWICE: a->flags |= AF_TAIL_RETURNS_TWICE; break;
			case A_DISABLE_TAIL_CALLS: a->flags |= AF_TAIL_DISABLE; break;
			case A_NOT_TAIL_CALLED: a->flags |= AF_TAIL_NOT_CALLED; break;
			case A_OPTNONE: a->flags |= AF_OPTNONE; break;
			case A_OVERLOADABLE: a->overloadable = 1; break;
			case A_GNU_INLINE: a->gnu_inline = 1; break;
			case A_TRANSPARENT_UNION: a->transparent = 1; break;
			case A_CONSTRUCTOR:
			case A_DESTRUCTOR:
				if (an == A_CONSTRUCTOR)
					a->constructor = 1;
				else
					a->destructor = 1;
				if (is(c, LEX_TOKEN_LEFT_PAREN)) {
					/* the priority: lower runs first */
					i128 v;

					next(c);
					const_expr(c, &v, NULL);
					expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
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
				if (is(c, LEX_TOKEN_LEFT_PAREN) && is_strtok(peek(c, 1)->kind)) {
					next(c);
					a->errmsg = string_index(c, aid);
					expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				}
				break;
			default: break;
			}
			{
				uint32_t end = loc;
				const vident *nm = &vintern_ents[tid];

				if (is(c, LEX_TOKEN_LEFT_PAREN))
					end = skip_balanced(c);
				if (an == A_NONE && !attr_known(c, scope, nm->s, nm->len)) {
					/* [[gnu::x]]: from the scope, printed as gnu:: (also for __gnu__::) */
					const char *sp = scope > 0 ? attr_scopes[scope] : "";
					diag_span r = { sloc, pp_token_end_find(c->p, end) };
					const char *fix = attr_correction(c, scope, nm->s, nm->len);
					const char *ns = nm->s;
					size_t nn = nm->len;

					if (scope >= 0)	/* [[__x__]] prints as x, __attribute__((__x__)) as written */
						attr_norm(&ns, &nn);

					if (fix) {
						char fb[160];
						diag_fixit fx = { sloc, pp_token_end_find(c->p, loc), fb, 0 };

						snprintf(fb, sizeof(fb), "%s%s", sp, fix);
						cc_warnx(c, "unknown-attributes", true, NULL, sloc, &r, 1, &fx, 1,
							 "unknown attribute '%s%.*s' ignored; did you mean '%s'?", sp, (int)nn, ns, fb);
					} else {
						cc_warnx(c, "unknown-attributes", true, NULL, sloc, &r, 1, NULL, 0,
							 "unknown attribute '%s%.*s' ignored", sp, (int)nn, ns);
					}
				}
			}
		}
		expect(c, close, cs);
		expect(c, close, cs);
	}
}

/* the 0/1 flags packed .. noinstr are 18 contiguous bytes after the u32 fields */
_Static_assert(offsetof(attrs, packed) % sizeof(uint32_t) == 0 &&
	       offsetof(attrs, noinstr) == offsetof(attrs, packed) + 17 && offsetof(attrs, pure) == offsetof(attrs, noinstr) + 1 &&
	       offsetof(attrs, unavail) == offsetof(attrs, pure) + 1, "attrs layout");

VPRIV void merge_attrs(attrs *to, const attrs *from)
{
	if (attrs_empty(from))
		return;
	if (from->x86_target) to->x86_features = from->x86_features, to->x86_target = 1;
	if (from->aligned > to->aligned)
		to->aligned = from->aligned;
	if (from->interrupt) to->interrupt = from->interrupt;
	if (from->ms_struct) to->ms_struct = from->ms_struct;
	if (from->section) to->section = from->section;
	if (from->alias) to->alias = from->alias;
	if (from->asmname) to->asmname = from->asmname;
	if (from->cleanup) to->cleanup = from->cleanup;
	if (from->mode) to->mode = from->mode;
	if (from->vecsize) to->vecsize = from->vecsize;
	if (from->extvec) to->extvec = from->extvec;
	if (from->depr) to->depr = from->depr, to->deprmsg = from->deprmsg, to->unavail = from->unavail;
	if (from->wur) to->wur = from->wur;
	if (from->ifunc) to->ifunc = from->ifunc;
	to->nonnull |= from->nonnull;
	if (from->fmt) to->fmt = from->fmt;
	if (from->fmtarg) to->fmtarg = from->fmtarg;
	if (from->counted_by) to->counted_by = from->counted_by;
	if (from->visibility) to->visibility = from->visibility;
	if (from->errmsg) to->errmsg = from->errmsg;
	if (from->prio) to->prio = from->prio;
	if (from->tlsmodel) to->tlsmodel = from->tlsmodel, to->tlsloc = from->tlsloc;
	if (from->pure) to->pure = from->pure;
	to->flags |= from->flags;
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
	if (kw == KW_asm)
		EXT_WARN_MS(c, "asm", "language-extension-token", w_ped, tok(c)->loc, "extension used");
	next(c);
	expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
	a->asmname = string_index(c, 0);
	expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
}
