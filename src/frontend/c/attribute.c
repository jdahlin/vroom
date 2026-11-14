/* vcc front end: attribute tables, parsing, merging, typo correction and asm labels */
#include "cc-internal.h"

/* ---------------- attributes ---------------- */

enum {
	CC_ATTRIBUTE_NONE, CC_ATTRIBUTE_INTERRUPT, CC_ATTRIBUTE_MS_STRUCT, CC_ATTRIBUTE_ALIGNED, CC_ATTRIBUTE_PACKED, CC_ATTRIBUTE_SECTION, CC_ATTRIBUTE_MODE, CC_ATTRIBUTE_CLEANUP, CC_ATTRIBUTE_ALIAS, CC_ATTRIBUTE_WEAK, CC_ATTRIBUTE_USED, CC_ATTRIBUTE_UNUSED,
	CC_ATTRIBUTE_NORETURN, CC_ATTRIBUTE_ALWAYS_INLINE, CC_ATTRIBUTE_NOINLINE, CC_ATTRIBUTE_GNU_INLINE, CC_ATTRIBUTE_TRANSPARENT_UNION, CC_ATTRIBUTE_VISIBILITY,
	CC_ATTRIBUTE_CONSTRUCTOR, CC_ATTRIBUTE_DESTRUCTOR, CC_ATTRIBUTE_COUNTED_BY, CC_ATTRIBUTE_VECTOR_SIZE, CC_ATTRIBUTE_COLD, CC_ATTRIBUTE_NONSTRING, CC_ATTRIBUTE_MAY_ALIAS,
	CC_ATTRIBUTE_WEAKREF, CC_ATTRIBUTE_NAKED, CC_ATTRIBUTE_NOCF, CC_ATTRIBUTE_ERROR, CC_ATTRIBUTE_OVERLOADABLE, CC_ATTRIBUTE_NOINSTR, CC_ATTRIBUTE_TLS_MODEL, CC_ATTRIBUTE_EXT_VECTOR, CC_ATTRIBUTE_DEPRECATED, CC_ATTRIBUTE_WUR, CC_ATTRIBUTE_NODISCARD, CC_ATTRIBUTE_IFUNC, CC_ATTRIBUTE_NONNULL, CC_ATTRIBUTE_FORMAT, CC_ATTRIBUTE_FORMAT_ARG,
	CC_ATTRIBUTE_PURE, CC_ATTRIBUTE_CONST, CC_ATTRIBUTE_UNAVAILABLE, CC_ATTRIBUTE_TARGET, CC_ATTRIBUTE_RETURNS_TWICE, CC_ATTRIBUTE_DISABLE_TAIL_CALLS, CC_ATTRIBUTE_NOT_TAIL_CALLED, CC_ATTRIBUTE_OPTNONE,
	/* from here on only where clang knows them for the target (cc_attribute_is_known): else "unknown attribute" */
	CC_ATTRIBUTE_GCC_STRUCT, CC_ATTRIBUTE_MS_ABI, CC_ATTRIBUTE_SYSV_ABI, CC_ATTRIBUTE_DLLIMPORT, CC_ATTRIBUTE_DLLEXPORT,
	CC_ATTRIBUTE_CDECL, CC_ATTRIBUTE_STDCALL, CC_ATTRIBUTE_FASTCALL, CC_ATTRIBUTE_THISCALL, CC_ATTRIBUTE_VECTORCALL, CC_ATTRIBUTE_PASCAL, CC_ATTRIBUTE_REGCALL,
	CC_ATTRIBUTE_COUNT
};

static const char *const cc_attribute_names[CC_ATTRIBUTE_COUNT] = {
	[CC_ATTRIBUTE_INTERRUPT] = "interrupt",
	[CC_ATTRIBUTE_MS_STRUCT] = "ms_struct", [CC_ATTRIBUTE_ALIGNED] = "aligned", [CC_ATTRIBUTE_PACKED] = "packed", [CC_ATTRIBUTE_SECTION] = "section", [CC_ATTRIBUTE_MODE] = "mode",
	[CC_ATTRIBUTE_CLEANUP] = "cleanup", [CC_ATTRIBUTE_ALIAS] = "alias", [CC_ATTRIBUTE_WEAK] = "weak", [CC_ATTRIBUTE_USED] = "used",
	[CC_ATTRIBUTE_UNUSED] = "unused", [CC_ATTRIBUTE_NORETURN] = "noreturn", [CC_ATTRIBUTE_ALWAYS_INLINE] = "always_inline",
	[CC_ATTRIBUTE_NOINLINE] = "noinline", [CC_ATTRIBUTE_GNU_INLINE] = "gnu_inline", [CC_ATTRIBUTE_TRANSPARENT_UNION] = "transparent_union",
	[CC_ATTRIBUTE_VISIBILITY] = "visibility", [CC_ATTRIBUTE_CONSTRUCTOR] = "constructor", [CC_ATTRIBUTE_DESTRUCTOR] = "destructor",
	[CC_ATTRIBUTE_COUNTED_BY] = "counted_by", [CC_ATTRIBUTE_VECTOR_SIZE] = "vector_size", [CC_ATTRIBUTE_COLD] = "cold",
	[CC_ATTRIBUTE_NONSTRING] = "nonstring", [CC_ATTRIBUTE_MAY_ALIAS] = "may_alias", [CC_ATTRIBUTE_WEAKREF] = "weakref",
	[CC_ATTRIBUTE_NOINSTR] = "no_instrument_function", [CC_ATTRIBUTE_NAKED] = "naked", [CC_ATTRIBUTE_NOCF] = "nocf_check", [CC_ATTRIBUTE_ERROR] = "error",
	[CC_ATTRIBUTE_OVERLOADABLE] = "overloadable", [CC_ATTRIBUTE_TLS_MODEL] = "tls_model", [CC_ATTRIBUTE_EXT_VECTOR] = "ext_vector_type",
	[CC_ATTRIBUTE_TARGET] = "target", [CC_ATTRIBUTE_PURE] = "pure", [CC_ATTRIBUTE_CONST] = "const", [CC_ATTRIBUTE_UNAVAILABLE] = "unavailable",
	[CC_ATTRIBUTE_RETURNS_TWICE] = "returns_twice", [CC_ATTRIBUTE_DISABLE_TAIL_CALLS] = "disable_tail_calls",
	[CC_ATTRIBUTE_NOT_TAIL_CALLED] = "not_tail_called", [CC_ATTRIBUTE_OPTNONE] = "optnone",
	[CC_ATTRIBUTE_GCC_STRUCT] = "gcc_struct", [CC_ATTRIBUTE_MS_ABI] = "ms_abi", [CC_ATTRIBUTE_SYSV_ABI] = "sysv_abi", [CC_ATTRIBUTE_DLLIMPORT] = "dllimport",
	[CC_ATTRIBUTE_DLLEXPORT] = "dllexport", [CC_ATTRIBUTE_CDECL] = "cdecl", [CC_ATTRIBUTE_STDCALL] = "stdcall", [CC_ATTRIBUTE_FASTCALL] = "fastcall",
	[CC_ATTRIBUTE_THISCALL] = "thiscall", [CC_ATTRIBUTE_VECTORCALL] = "vectorcall", [CC_ATTRIBUTE_PASCAL] = "pascal", [CC_ATTRIBUTE_REGCALL] = "regcall",
	[CC_ATTRIBUTE_DEPRECATED] = "deprecated", [CC_ATTRIBUTE_WUR] = "warn_unused_result", [CC_ATTRIBUTE_NODISCARD] = "nodiscard", [CC_ATTRIBUTE_IFUNC] = "ifunc", [CC_ATTRIBUTE_NONNULL] = "nonnull", [CC_ATTRIBUTE_FORMAT] = "format", [CC_ATTRIBUTE_FORMAT_ARG] = "format_arg",
};

static uint32_t *cc_attribute_directory[CC_DIRECTORY_COUNT];

CC_PRIVATE void cc_attribute_table_init(void)
{
	static bool done;

	if (__atomic_load_n(&done, __ATOMIC_ACQUIRE))
		return;
	for (int i = 1; i < CC_ATTRIBUTE_COUNT; i++) {
		char b[2][64];
		size_t l = strlen(cc_attribute_names[i]);

		memset(b, 0, sizeof(b));
		memcpy(b[0], cc_attribute_names[i], l);
		b[1][0] = b[1][1] = '_';
		memcpy(b[1] + 2, cc_attribute_names[i], l);
		b[1][l + 2] = b[1][l + 3] = '_';
		for (int k = 0; k < 2; k++) {
			uint32_t id = vintern_copy(b[k], k ? l + 4 : l);

			if (!cc_attribute_directory[id >> CC_DIRECTORY_BITS])
				cc_attribute_directory[id >> CC_DIRECTORY_BITS] = calloc(CC_DIRECTORY_PAGE, sizeof(uint32_t));
			cc_attribute_directory[id >> CC_DIRECTORY_BITS][id & (CC_DIRECTORY_PAGE - 1)] = (uint32_t)i;
		}
	}
	__atomic_store_n(&done, true, __ATOMIC_RELEASE);
}

static int cc_attribute_lookup(uint32_t id)
{
	uint32_t *pg = cc_attribute_directory[id >> CC_DIRECTORY_BITS];

	return pg ? (int)pg[id & (CC_DIRECTORY_PAGE - 1)] : 0;
}

CC_PRIVATE uint32_t cc_token_skip_balanced(cc *c)
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
		else if (cc_token_is(c, LEX_TOKEN_EOF))
			cc_report_fatal(c, tok(c)->loc, "unexpected end of file in attribute");
		next(c);
	} while (depth > 0);
	return last;
}

/* a string literal argument; attr: the attribute's name (0: none).  On an error the
   argument is skipped up to its ')', so the caller's cc_token_expect() does not report again */
CC_PRIVATE uint32_t cc_attribute_string_parse(cc *c, uint32_t attr)
{
	uint32_t n;

	if (!cc_token_kind_is_string(tok(c)->kind)) {
		if (attr) {
			int l;
			const char *s = cc_identifier_name(attr, &l);	/* as written: clang keeps '__alias__' */

			cc_report_error(c, tok(c)->loc, "expected string literal as argument of '%.*s' attribute", l, s);
			for (int d = 0; !cc_token_is(c, LEX_TOKEN_EOF) && !cc_token_is(c, LEX_TOKEN_SEMICOLON) && (d || !cc_token_is(c, LEX_TOKEN_RIGHT_PAREN)); next(c))
				d += cc_token_is(c, LEX_TOKEN_LEFT_PAREN) - cc_token_is(c, LEX_TOKEN_RIGHT_PAREN);
		} else {
			cc_report_error(c, tok(c)->loc, "expected string literal");
		}
		return 0;
	}
	n = cc_literal_string_parse(c);
	return AST_NODE(n)->a + 1;
}

CC_PRIVATE int cc_attribute_mode_kind(cc *c, uint32_t id, bool sgn)
{
	int l;
	const char *s = cc_identifier_name(id, &l);
	char b[16] = { 0 };

	if (l > 2 && s[0] == '_' && s[1] == '_')
		s += 2, l -= 4;
	if (l <= 0 || l >= 15)
		return 0;
	memcpy(b, s, (size_t)l);
	(void)c;
	if (!strcmp(b, "SF")) return AST_TYPE_FLOAT;
	if (!strcmp(b, "DF")) return AST_TYPE_DOUBLE;
	if (!strcmp(b, "QI")) return sgn ? AST_TYPE_SIGNED_CHAR : AST_TYPE_UNSIGNED_CHAR;
	if (!strcmp(b, "HI")) return sgn ? AST_TYPE_SHORT : AST_TYPE_UNSIGNED_SHORT;
	if (!strcmp(b, "SI")) return sgn ? AST_TYPE_INT : AST_TYPE_UNSIGNED_INT;
	if (!strcmp(b, "DI")) return CC_I64(c, !sgn);
	if (!strcmp(b, "word") || !strcmp(b, "pointer")) return CC_INTPTR(c, !sgn);	/* the pointer's width */
	if (!strcmp(b, "TI")) return sgn ? AST_TYPE_INT128 : AST_TYPE_UINT128;
	return 0;
}

#include "attrnames.inc"
#include "attrnames-x86.inc"
#include "attrnames-win64.inc"

/* x86-64: name in one of its sorted lists (gen-x86data.sh, gen-win64data.sh) */
static bool cc_attribute_x86_is_known(const char *const *tab, size_t n, const char *k, size_t kn)
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
static void cc_attribute_name_normalize(const char **s, size_t *n)
{
	if (*n >= 4 && (*s)[0] == '_' && (*s)[1] == '_' && (*s)[*n - 2] == '_' && (*s)[*n - 1] == '_')
		*s += 2, *n -= 4;
}

/* scope of a [[...]] attribute as clang prints it: "" (none), "gnu::", "clang::" */
static const char *const cc_attribute_scopes[] = { "", "gnu::", "clang::" };

/*
 * whether clang knows attribute s for the target: GNU syntax (scope < 0, its
 * __has_attribute), or [[scope::s]] (__has_c_attribute)
 */
static bool cc_attribute_is_known(cc *c, int scope, const char *s, size_t n)
{
	int bit = 1 << (c->o->target == PP_WASM32 ? PP_WASM64 : c->o->target);
	size_t lo = 0, hi = scope < 0 ? sizeof(cc_attribute_gnu_names) / sizeof(cc_attribute_gnu_names[0]) : sizeof(cc_attribute_c_names) / sizeof(cc_attribute_c_names[0]);
	char k[160];

	cc_attribute_name_normalize(&s, &n);
	if (scope >= 0) {
		int l = snprintf(k, sizeof(k), "%s%.*s", cc_attribute_scopes[scope], (int)n, s);

		if (l < 0 || (size_t)l >= sizeof(k))
			return false;
		if (CC_WIN64(c))
			return cc_attribute_x86_is_known(c_attrs_win64, sizeof(c_attrs_win64) / sizeof(*c_attrs_win64), k, (size_t)l);
		if (CC_X86_64(c))
			return cc_attribute_x86_is_known(cc_attribute_c_x86_names, sizeof(cc_attribute_c_x86_names) / sizeof(*cc_attribute_c_x86_names), k, (size_t)l);
		while (lo < hi) {
			size_t mid = (lo + hi) / 2;
			int r = strcmp(cc_attribute_c_names[mid].name, k);

			if (!r)
				return cc_attribute_c_names[mid].on & bit;
			if (r < 0)
				lo = mid + 1;
			else
				hi = mid;
		}
		return false;
	}
	if (CC_WIN64(c))
		return cc_attribute_x86_is_known(gnu_attrs_win64, sizeof(gnu_attrs_win64) / sizeof(*gnu_attrs_win64), s, n);
	if (CC_X86_64(c))
		return cc_attribute_x86_is_known(cc_attribute_gnu_x86_names, sizeof(cc_attribute_gnu_x86_names) / sizeof(*cc_attribute_gnu_x86_names), s, n);
	while (lo < hi) {
		size_t mid = (lo + hi) / 2;
		int r = strncmp(cc_attribute_gnu_names[mid].name, s, n);

		if (!r && cc_attribute_gnu_names[mid].name[n])
			r = 1;
		if (!r)
			return cc_attribute_gnu_names[mid].on & bit;
		if (r < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	return false;
}

/* Levenshtein distance (clang's StringRef::cc_attribute_edit_distance with replacements) */
static unsigned cc_attribute_edit_distance(const char *a, size_t na, const char *b, size_t nb)
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
static const char *cc_attribute_name_correction(cc *c, int scope, const char *s, size_t n)
{
	unsigned max, best;
	const char *bn = NULL;

	cc_attribute_name_normalize(&s, &n);
	max = (unsigned)(n + 2) / 3, best = max + 1;
	for (size_t i = 0; i < sizeof(cc_attribute_spellings) / sizeof(cc_attribute_spellings[0]); i++) {
		size_t cn = strlen(cc_attribute_spellings[i]), ld = cn > n ? cn - n : n - cn;
		unsigned d;

		if (ld && n / ld < 3)
			continue;
		d = cc_attribute_edit_distance(s, n, cc_attribute_spellings[i], cn);
		if (d < best)
			best = d, bn = cc_attribute_spellings[i];
	}
	if (!bn || !best || !cc_attribute_is_known(c, scope, bn, strlen(bn)))
		return NULL;
	return bn;
}

/* GNU __attribute__((...)) and C23 [[...]] (gnu::name is the GNU attribute) */
__attribute__((noinline)) CC_PRIVATE void cc_attribute_list_parse_slow(cc *c, attrs *a)
{
	for (;;) {
		bool std = cc_token_at_standard_attribute(c);
		int close = std ? LEX_TOKEN_RIGHT_SQUARE : LEX_TOKEN_RIGHT_PAREN;
		const char *cs = std ? "']'" : "')'";

		if (!std && !cc_token_is_keyword(c, KW___attribute__) && !cc_token_is_keyword(c, KW___attribute))
			break;
		bool srcbr = std && !(tok(c)->flags & PP_TOKEN_FLAG_FROM_MACRO);	/* [[ written in the source */

		next(c);
		if (std) {
			next(c);
		} else {
			cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
			cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
		}
		while (!cc_token_is(c, close)) {
			ptok *t = tok(c);
			int an, scope = std ? 0 : -1;	/* cc_attribute_scopes index; -1: GNU syntax */
			uint32_t loc = t->loc, sloc = t->loc, sid = 0, aid;

			if (cc_token_accept(c, LEX_TOKEN_COMMA))
				continue;
			if (std && t->kind == LEX_TOKEN_NUMBER && t->len == 1 && t->s[0] == '1' && (t->flags & PP_TOKEN_FLAG_FROM_MACRO) &&
			    cc_token_peek(c, 1)->kind == LEX_TOKEN_COLON_COLON && srcbr && pp_location_spells(c->p, loc, "__clang__", 9)) {
				/* [[__clang__::x]] as written (not through a macro): clang warns and takes it as clang:: */
				diag_span r = { loc, pp_token_end_find(c->p, loc) };
				diag_fixit fx = { loc, pp_token_end_find(c->p, loc), "_Clang", DIAG_APPLICABILITY_NAME };	/* (__clang__ itself) */

				diag_info d = { "warning", loc, &r, 1, &fx, 1, "1", 1 };	/* the note at the macro's body */

				diag_warning_report(c->p, "ignored-attributes", true, NULL, &d,
					 "'__clang__' is a predefined macro name, not an attribute scope specifier; did you mean '_Clang' instead?");
				t->kind = LEX_TOKEN_IDENTIFIER, t->id = vintern_cstr("_Clang");
			}
			if (t->kind != LEX_TOKEN_IDENTIFIER && !(std && cc_token_keyword(t)))
				cc_token_expect(c, close, cs);	/* clang: expected ']' or ')' */
			if (std && cc_token_peek(c, 1)->kind == LEX_TOKEN_COLON_COLON) {
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
				if (t->kind != LEX_TOKEN_IDENTIFIER && !cc_token_keyword(t))
					cc_report_fatal(c, loc, "expected attribute name");
				if (!gnu) {	/* other vendors: ignored, with clang's warning */
					uint32_t end = t->loc;
					const vident *sn = &vintern_ents[sid], *nm = &vintern_ents[t->id];

					next(c);
					if (cc_token_is(c, LEX_TOKEN_LEFT_PAREN))
						end = cc_token_skip_balanced(c);
					{
						diag_span r = { sloc, pp_token_end_find(c->p, end) };

						cc_report_warning(c, "unknown-attributes", true, NULL, sloc, &r, 1, NULL, 0,
							 "unknown attribute '%.*s::%.*s' ignored", (int)sn->len, sn->s, (int)nm->len, nm->s);
					}
					continue;
				}
			}
			aid = t->id;	/* t is gone after next() */
			an = cc_attribute_lookup(aid);
			if (!an && cc_token_keyword(t) == KW_const)
				an = CC_ATTRIBUTE_NONE;
			const uint32_t tid = t->id;	/* t is the queue head: stale after next() */

			if (an >= CC_ATTRIBUTE_GCC_STRUCT && !cc_attribute_is_known(c, scope, vintern_ents[tid].s, vintern_ents[tid].len))
				an = CC_ATTRIBUTE_NONE;	/* (dllimport on Linux, ms_abi on arm64: clang's unknown attribute) */
			next(c);
			switch (an) {
			case CC_ATTRIBUTE_ALIGNED:
				if (cc_token_accept(c, LEX_TOKEN_LEFT_PAREN)) {
					i128 v;

					cc_constant_expression_parse(c, &v, NULL);
					cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
					if (v <= 0 || (v & (v - 1)))
						cc_report_error(c, loc, "requested alignment is not a power of 2");
					else if ((uint32_t)v > a->aligned)
						a->aligned = (uint32_t)v;
				} else if (16 > a->aligned) {
					a->aligned = 16;
				}
				continue;
			case CC_ATTRIBUTE_TARGET: {
				cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				uint32_t ts = cc_attribute_string_parse(c, aid);
				cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				cc_x86_target_attribute_parse(c, a, ts, loc);
				continue;
			}
			case CC_ATTRIBUTE_IFUNC:
				a->ifunc = 1;
				an = CC_ATTRIBUTE_ALIAS;
				__attribute__((fallthrough));
			case CC_ATTRIBUTE_SECTION:
			case CC_ATTRIBUTE_ALIAS:
			case CC_ATTRIBUTE_VISIBILITY:
				cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				{
					uint32_t sloc = tok(c)->loc;
					uint32_t s = cc_attribute_string_parse(c, aid);

					if (an == CC_ATTRIBUTE_SECTION && c->o->target == PP_DARWIN && s && !strchr(c->strs[s - 1].s, ','))
						cc_report_error(c, sloc, "argument to 'section' attribute is not valid for this target: "
							 "mach-o section specifier requires a segment and section separated by a comma");
					if (an == CC_ATTRIBUTE_SECTION)
						a->section = s;
					else if (an == CC_ATTRIBUTE_ALIAS)
						a->alias = s;
					else
						a->visibility = s;
				}
				cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				continue;
			case CC_ATTRIBUTE_TLS_MODEL: {
				if (!cc_token_accept(c, LEX_TOKEN_LEFT_PAREN)) {
					cc_report_error(c, loc, "'tls_model' attribute takes one argument");
					continue;
				}
				uint32_t argloc = tok(c)->loc, si = 0;
				bool empty = cc_token_is(c, LEX_TOKEN_RIGHT_PAREN);

				if (!empty)
					si = cc_attribute_string_parse(c, aid);
				if (empty || cc_token_is(c, LEX_TOKEN_COMMA)) {
					cc_report_error(c, loc, "'tls_model' attribute takes one argument");
					while (!cc_token_is(c, LEX_TOKEN_RIGHT_PAREN) && !cc_token_is(c, LEX_TOKEN_EOF) && !cc_token_is(c, LEX_TOKEN_SEMICOLON)) {
						if (cc_token_is(c, LEX_TOKEN_LEFT_PAREN)) cc_token_skip_balanced(c);
						else next(c);
					}
				} else if (si) {
					uint8_t model = (uint8_t)cc_tls_model_parse(c->strs[si - 1].s);

					if (!model)
						cc_report_error(c, argloc, "tls_model must be \"global-dynamic\", \"local-dynamic\", \"initial-exec\" or \"local-exec\"");
					else
						a->tlsmodel = model, a->tlsloc = loc;
				}
				cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				continue;
			}
			case CC_ATTRIBUTE_MODE:
				cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				if (cc_token_is(c, LEX_TOKEN_IDENTIFIER))
					a->mode = tok(c)->id;
				next(c);
				cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				continue;
			case CC_ATTRIBUTE_CLEANUP:
				cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				if (cc_token_is(c, LEX_TOKEN_IDENTIFIER)) {
					uint32_t s = cc_symbol_lookup(c, tok(c)->id);

					if (!s || c->syms[s].kind != AST_SYMBOL_FUNCTION)
						cc_report_error(c, tok(c)->loc, "'cleanup' argument is not a function");
					a->cleanup = s;
					if (s)
						c->syms[s].flags |= AST_SYMBOL_FLAG_REFERENCED | AST_SYMBOL_FLAG_USED;
				}
				next(c);
				cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				continue;
			case CC_ATTRIBUTE_COUNTED_BY:
				cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				if (cc_token_is(c, LEX_TOKEN_IDENTIFIER))
					a->counted_by = tok(c)->id;
				next(c);
				cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				continue;
			case CC_ATTRIBUTE_PURE:
			case CC_ATTRIBUTE_CONST:	/* (-Wunused-value: a discarded call) */
				a->pure = an == CC_ATTRIBUTE_PURE ? 1 : 2;
				break;
			case CC_ATTRIBUTE_VECTOR_SIZE: {
				i128 v;

				cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				cc_constant_expression_parse(c, &v, NULL);
				cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				if (v <= 0 || (v & (v - 1)) || v > 1024)
					cc_report_error(c, loc, "invalid vector size");
				else
					a->vecsize = (uint32_t)v;
				continue;
			}
			case CC_ATTRIBUTE_DEPRECATED:	/* deprecated or deprecated("msg"[, "replacement"]) */
			case CC_ATTRIBUTE_UNAVAILABLE:	/* unavailable or unavailable("msg") */
				a->depr = loc, a->deprmsg = 0, a->unavail = an == CC_ATTRIBUTE_UNAVAILABLE;
				if (cc_token_is(c, LEX_TOKEN_LEFT_PAREN) && cc_token_kind_is_string(cc_token_peek(c, 1)->kind)) {
					next(c);
					a->deprmsg = cc_attribute_string_parse(c, aid);
					if (cc_token_accept(c, LEX_TOKEN_COMMA) && cc_token_kind_is_string(tok(c)->kind))
						cc_attribute_string_parse(c, aid);	/* (the replacement, for a fix-it) */
					cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				}
				break;
			case CC_ATTRIBUTE_WUR: a->wur = 1; break;
			case CC_ATTRIBUTE_NONNULL:	/* nonnull or nonnull(i, ...) */
				if (!cc_token_accept(c, LEX_TOKEN_LEFT_PAREN)) {
					a->nonnull |= 1;
					continue;
				}
				if (cc_token_accept(c, LEX_TOKEN_RIGHT_PAREN)) {
					a->nonnull |= 1;
					continue;
				}
				for (;;) {
					i128 v;

					cc_constant_expression_parse(c, &v, NULL);
					if (v >= 1 && v <= 31)
						a->nonnull |= 1u << v;
					if (!cc_token_accept(c, LEX_TOKEN_COMMA))
						break;
				}
				cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				continue;
			case CC_ATTRIBUTE_FORMAT_ARG: {	/* format_arg(i): the call returns its i-th argument's format */
				i128 v = 0;

				cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				cc_constant_expression_parse(c, &v, NULL);
				cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				a->fmtarg = v >= 1 && v <= 255 ? (uint8_t)v : 0;
				continue;
			}
			case CC_ATTRIBUTE_FORMAT: {	/* format(archetype, fmt, first): printf checked by -Wformat */
				uint32_t arch = 0;
				i128 fi = 0, fa = 0;

				cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				if (cc_token_is(c, LEX_TOKEN_IDENTIFIER)) {
					size_t l = cc_identifier_length(tok(c)->id);
					const char *n = cc_identifier_string(tok(c)->id);

					if ((l == 6 && !memcmp(n, "printf", 6)) || (l == 10 && !memcmp(n, "__printf__", 10)) ||
					    (l == 10 && !memcmp(n, "gnu_printf", 10)) || (l == 14 && !memcmp(n, "__gnu_printf__", 14)))
						arch = 1;
					next(c);
				}
				if (cc_token_accept(c, LEX_TOKEN_COMMA)) {
					cc_constant_expression_parse(c, &fi, NULL);
					if (cc_token_accept(c, LEX_TOKEN_COMMA))
						cc_constant_expression_parse(c, &fa, NULL);
				}
				cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				a->fmt = arch && fi >= 1 && fi <= 255 && fa >= 0 && fa <= 255 ? arch << 16 | (uint32_t)fi << 8 | (uint32_t)fa : 0;
				continue;
			}
			case CC_ATTRIBUTE_NODISCARD: a->wur = 2; break;
			case CC_ATTRIBUTE_EXT_VECTOR: {	/* ext_vector_type(AST_NODE): AST_NODE elements (typedefs only) */
				i128 v;

				cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
				cc_constant_expression_parse(c, &v, NULL);
				cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				if (v <= 0 || v > 4096)
					cc_report_error(c, loc, "invalid vector size");
				else
					a->extvec = (uint32_t)v;
				continue;
			}
			case CC_ATTRIBUTE_MS_STRUCT:
			case CC_ATTRIBUTE_GCC_STRUCT:
				if (an == CC_ATTRIBUTE_MS_STRUCT ? a->gcc_struct : a->ms_struct) {
					cc_report_error(c, loc, "'%s' and '%s' attributes are not compatible", an == CC_ATTRIBUTE_MS_STRUCT ? "ms_struct" : "gcc_struct",
						 an == CC_ATTRIBUTE_MS_STRUCT ? "gcc_struct" : "ms_struct");
					cc_report_note(c, an == CC_ATTRIBUTE_MS_STRUCT ? a->gcc_struct : a->ms_struct, "conflicting attribute is here");
				} else if (an == CC_ATTRIBUTE_MS_STRUCT) {
					a->ms_struct = loc;
				} else {
					a->gcc_struct = loc;
				}
				break;
			case CC_ATTRIBUTE_MS_ABI:
			case CC_ATTRIBUTE_SYSV_ABI:
				a->fnabi = an == CC_ATTRIBUTE_MS_ABI ? CC_FNABI_MS : CC_FNABI_SYSV, a->fnabi_loc = loc;
				break;
			case CC_ATTRIBUTE_DLLIMPORT: if (a->dll != CC_DLLEXPORT) a->dll = CC_DLLIMPORT; break;	/* (dllexport wins, as clang) */
			case CC_ATTRIBUTE_DLLEXPORT: a->dll = CC_DLLEXPORT; break;
			case CC_ATTRIBUTE_STDCALL: case CC_ATTRIBUTE_FASTCALL: case CC_ATTRIBUTE_THISCALL: case CC_ATTRIBUTE_PASCAL:
				/* x86-64: 32-bit conventions; Windows ignores all but pascal silently (clang) */
				if (an == CC_ATTRIBUTE_PASCAL || !CC_WIN64(c)) {
					const vident *nm = &vintern_ents[tid];
					diag_span r = { loc, pp_token_end_find(c->p, loc) };

					cc_report_warning(c, "ignored-attributes", true, NULL, loc, &r, 1, NULL, 0,
						 "'%.*s' calling convention is not supported for this target", (int)nm->len, nm->s);
				}
				break;
			case CC_ATTRIBUTE_CDECL: case CC_ATTRIBUTE_VECTORCALL: case CC_ATTRIBUTE_REGCALL: break;	/* x86-64: the target's own convention */
			case CC_ATTRIBUTE_PACKED: a->packed = 1; break;
			case CC_ATTRIBUTE_WEAK: a->weak = 1; break;
			case CC_ATTRIBUTE_USED: a->used = 1; break;
			case CC_ATTRIBUTE_UNUSED: a->unused = 1; break;
			case CC_ATTRIBUTE_NORETURN: a->noreturn = 1; break;
			case CC_ATTRIBUTE_ALWAYS_INLINE: a->always_inline = 1; break;
			case CC_ATTRIBUTE_INTERRUPT: a->interrupt = 1; a->noinline = 1; break;
			case CC_ATTRIBUTE_NOINLINE: a->noinline = 1; break;
			case CC_ATTRIBUTE_NOINSTR: a->noinstr = 1; break;
			case CC_ATTRIBUTE_RETURNS_TWICE: a->flags |= CC_ATTRIBUTE_FLAG_TAIL_RETURNS_TWICE; break;
			case CC_ATTRIBUTE_DISABLE_TAIL_CALLS: a->flags |= CC_ATTRIBUTE_FLAG_TAIL_DISABLE; break;
			case CC_ATTRIBUTE_NOT_TAIL_CALLED: a->flags |= CC_ATTRIBUTE_FLAG_TAIL_NOT_CALLED; break;
			case CC_ATTRIBUTE_OPTNONE: a->flags |= CC_ATTRIBUTE_FLAG_OPTNONE; break;
			case CC_ATTRIBUTE_OVERLOADABLE: a->overloadable = 1; break;
			case CC_ATTRIBUTE_GNU_INLINE: a->gnu_inline = 1; break;
			case CC_ATTRIBUTE_TRANSPARENT_UNION: a->transparent = 1; break;
			case CC_ATTRIBUTE_CONSTRUCTOR:
			case CC_ATTRIBUTE_DESTRUCTOR:
				if (an == CC_ATTRIBUTE_CONSTRUCTOR)
					a->constructor = 1;
				else
					a->destructor = 1;
				if (cc_token_is(c, LEX_TOKEN_LEFT_PAREN)) {
					/* the priority: lower runs first */
					i128 v;

					next(c);
					cc_constant_expression_parse(c, &v, NULL);
					cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
					if (v < 0 || v > 65535)
						cc_report_error(c, loc, "'%s' priority out of range", an == CC_ATTRIBUTE_CONSTRUCTOR ? "constructor" : "destructor");
					else
						a->prio = (uint16_t)v;
				}
				continue;
			case CC_ATTRIBUTE_COLD: a->cold = 1; break;
			case CC_ATTRIBUTE_NONSTRING: a->nonstring = 1; break;
			case CC_ATTRIBUTE_MAY_ALIAS: a->may_alias = 1; break;
			case CC_ATTRIBUTE_ERROR:	/* a call left after folding is a build error */
				a->error_fn = 1;
				if (cc_token_is(c, LEX_TOKEN_LEFT_PAREN) && cc_token_kind_is_string(cc_token_peek(c, 1)->kind)) {
					next(c);
					a->errmsg = cc_attribute_string_parse(c, aid);
					cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
				}
				break;
			default: break;
			}
			{
				uint32_t end = loc;
				const vident *nm = &vintern_ents[tid];

				if (cc_token_is(c, LEX_TOKEN_LEFT_PAREN))
					end = cc_token_skip_balanced(c);
				if (an == CC_ATTRIBUTE_NONE && !cc_attribute_is_known(c, scope, nm->s, nm->len)) {
					/* [[gnu::x]]: from the scope, printed as gnu:: (also for __gnu__::) */
					const char *sp = scope > 0 ? cc_attribute_scopes[scope] : "";
					diag_span r = { sloc, pp_token_end_find(c->p, end) };
					const char *fix = cc_attribute_name_correction(c, scope, nm->s, nm->len);
					const char *ns = nm->s;
					size_t nn = nm->len;

					if (scope >= 0)	/* [[__x__]] prints as x, __attribute__((__x__)) as written */
						cc_attribute_name_normalize(&ns, &nn);

					if (fix) {
						char fb[160];
						diag_fixit fx = { sloc, pp_token_end_find(c->p, loc), fb, 0 };

						snprintf(fb, sizeof(fb), "%s%s", sp, fix);
						cc_report_warning(c, "unknown-attributes", true, NULL, sloc, &r, 1, &fx, 1,
							 "unknown attribute '%s%.*s' ignored; did you mean '%s'?", sp, (int)nn, ns, fb);
					} else {
						cc_report_warning(c, "unknown-attributes", true, NULL, sloc, &r, 1, NULL, 0,
							 "unknown attribute '%s%.*s' ignored", sp, (int)nn, ns);
					}
				}
			}
		}
		cc_token_expect(c, close, cs);
		cc_token_expect(c, close, cs);
	}
}

/* the 0/1 flags packed .. noinstr are 18 contiguous bytes after the u32 fields */
_Static_assert(offsetof(attrs, packed) % sizeof(uint32_t) == 0 &&
	       offsetof(attrs, noinstr) == offsetof(attrs, packed) + 17 && offsetof(attrs, pure) == offsetof(attrs, noinstr) + 1 &&
	       offsetof(attrs, unavail) == offsetof(attrs, pure) + 1, "attrs layout");

CC_PRIVATE void cc_attribute_merge(attrs *to, const attrs *from)
{
	if (cc_attribute_is_empty(from))
		return;
	if (from->x86_target) to->x86_features = from->x86_features, to->x86_target = 1;
	if (from->aligned > to->aligned)
		to->aligned = from->aligned;
	if (from->interrupt) to->interrupt = from->interrupt;
	if (from->ms_struct) to->ms_struct = from->ms_struct;
	if (from->gcc_struct) to->gcc_struct = from->gcc_struct;
	if (from->fnabi) to->fnabi = from->fnabi, to->fnabi_loc = from->fnabi_loc;
	if (from->dll > to->dll) to->dll = from->dll;
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
CC_PRIVATE void cc_asm_label_parse(cc *c, attrs *a)
{
	int kw = cc_token_keyword(tok(c));

	if (kw != KW_asm && kw != KW___asm__ && kw != KW___asm)
		return;
	if (kw == KW_asm)
		CC_REPORT_EXTENSION_MICROSOFT(c, "asm", "language-extension-token", cc_warning_group_pedantic, tok(c)->loc, "extension used");
	next(c);
	cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
	a->asmname = cc_attribute_string_parse(c, 0);
	cc_token_expect(c, LEX_TOKEN_RIGHT_PAREN, "')'");
}
