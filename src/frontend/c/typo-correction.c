/* vcc front end: "did you mean" for undeclared identifiers and keywords (clang's typo correction) */
#include "cc-internal.h"

/* Levenshtein distance, giving up above max */
static int cc_typo_edit_distance(const char *a, int na, const char *b, int nb, int max)
{
	int row[nb + 1];

	if (na - nb > max || nb - na > max)
		return max + 1;
	for (int j = 0; j <= nb; j++)
		row[j] = j;
	for (int i = 1; i <= na; i++) {
		int diag = row[0], best;

		row[0] = best = i;
		for (int j = 1; j <= nb; j++) {
			int up = row[j], v = diag + (a[i - 1] != b[j - 1]);

			if (up + 1 < v)
				v = up + 1;
			if (row[j - 1] + 1 < v)
				v = row[j - 1] + 1;
			diag = up, row[j] = v;
			if (v < best)
				best = v;
		}
		if (best > max)
			return max + 1;
	}
	return row[nb];
}

/* keywords clang offers as corrections (Sema's CTypeSpecs) where a type may stand,
   and at the start of a statement the statement keywords too */
static const uint16_t cc_typo_keywords_type[] = {
	KW_char, KW_const, KW_double, KW_enum, KW_float, KW_int, KW_long, KW_short, KW_signed,
	KW_struct, KW_union, KW_unsigned, KW_void, KW_volatile, KW__Complex, KW_extern, KW_inline,
	KW_static, KW_typedef, KW_restrict, KW__Bool, KW_bool,
};
static const uint16_t cc_typo_keywords_statement[] = {
	KW_return, KW_goto, KW_else, KW_if, KW_for, KW_while, KW_do, KW_switch, KW_break, KW_continue,
	KW_case, KW_default,
};

static bool cc_typo_keyword_is_offered(cc *c, uint16_t k)
{
	if (!cc_keyword_map[k])	/* bool before C23 */
		return false;
	if (k == KW_break)
		return c->in_loop || c->in_switch;
	if (k == KW_continue)
		return c->in_loop;
	if (k == KW_case || k == KW_default)
		return c->in_switch;
	return true;
}

/* clang's typo correction: the visible symbol closest to id (edit distance at most
   (len + 2) / 3), of a type name (types) or of anything else, or with kws (KWS_*)
   a keyword closer than any symbol (*kw); rejected, as clang does, when the name is
   under three times as long as the distance.  Returns the symbol, 0 if none */
static uint32_t cc_typo_symbol_find(cc *c, uint32_t id, bool types, int kws, uint32_t *kw)
{
	int n, max, best;
	const char *s = cc_identifier_name(id, &n);
	uint32_t pick = 0, kpick = 0;
	int lim = pp_option_spell_limit(c->p);

	if (kw)
		*kw = 0;
	/* clang tries 50 corrections per TU (failed ones count): each scans every symbol */
	if (lim == -2 || (lim != -1 && c->ntypo++ >= (uint32_t)(lim ? lim : 50)))
		return 0;
	max = best = (n + 2) / 3;
	for (uint32_t i = 1; i < c->nsym; i++) {
		const sym *y = &c->syms[i];
		int m, d;
		const char *t;

		if (!y->name || y->name == id || (y->kind == S_TYPEDEF) != types || y->kind == S_BUILTIN)
			continue;
		t = cc_identifier_name(y->name, &m);
		if (m > 1 && t[0] == '_' && (t[1] == '_' || (t[1] >= 'A' && t[1] <= 'Z')))
			continue;
		if (cc_symbol_lookup(c, y->name) != i)
			continue;
		d = cc_typo_edit_distance(s, n, t, m, max);
		if (d <= max && (!pick || d < best))
			pick = i, best = d;
	}
	for (int l = 0; kws && l < 2; l++) {
		const uint16_t *v = l ? cc_typo_keywords_statement : cc_typo_keywords_type;
		size_t nv = l ? sizeof(cc_typo_keywords_statement) / sizeof(cc_typo_keywords_statement[0]) : sizeof(cc_typo_keywords_type) / sizeof(cc_typo_keywords_type[0]);

		if (l && !(kws & CC_TYPO_KEYWORDS_STATEMENT))
			break;
		for (size_t i = 0; i < nv; i++) {
			int m, d;
			const char *t;

			if (!cc_typo_keyword_is_offered(c, v[i]))
				continue;
			t = cc_identifier_name(v[i], &m);
			d = cc_typo_edit_distance(s, n, t, m, max);
			if (d <= max && (!(pick || kpick) || d < best))
				kpick = v[i], pick = 0, best = d;
		}
	}
	if ((pick || kpick) && best > 0 && n / best < 3)
		pick = kpick = 0;
	if (kw)
		*kw = kpick;
	return pick;
}

/* the keyword id is a typo of at the start of a statement (CC_TYPO_KEYWORDS_STATEMENT) or where a type
   may stand (CC_TYPO_KEYWORDS_TYPE), if a keyword is its best correction; 0 if not */
CC_PRIVATE uint32_t cc_typo_keyword_find(cc *c, uint32_t id, int kws)
{
	uint32_t kw;

	if (tok(c)->flags & PP_TOKEN_FLAG_FROM_MACRO)
		return 0;
	cc_typo_symbol_find(c, id, kws == CC_TYPO_KEYWORDS_TYPE, kws, &kw);
	return kw;
}

/* id is a keyword in C23 (or in the GNU modes) but not in this -std: what to use instead */
static const char *cc_typo_c23_word(cc *c, uint32_t id)
{
	static const struct { const char *w, *h; } t[] = {
		{ "alignas", "'alignas' is a keyword in C23: before it, use '_Alignas' or include <stdalign.h>" },
		{ "alignof", "'alignof' is a keyword in C23: before it, use '_Alignof' or include <stdalign.h>" },
		{ "bool", "'bool' is a keyword in C23: before it, use '_Bool' or include <stdbool.h>" },
		{ "constexpr", "'constexpr' is a keyword in C23 (-std=c23): before it, use 'static const' or an enum constant" },
		{ "false", "'false' is a keyword in C23: before it, include <stdbool.h>" },
		{ "nullptr", "'nullptr' is a keyword in C23 (-std=c23): before it, use NULL from <stddef.h>" },
		{ "static_assert", "'static_assert' is a keyword in C23: before it, use '_Static_assert' or include <assert.h>" },
		{ "thread_local", "'thread_local' is a keyword in C23: before it, use '_Thread_local' or include <threads.h>" },
		{ "true", "'true' is a keyword in C23: before it, include <stdbool.h>" },
		{ "typeof", "'typeof' is a keyword in C23 and the GNU modes (-std=gnu17): in ISO C17, use '__typeof__'" },
		{ "typeof_unqual", "'typeof_unqual' is a keyword in C23: before it, use '__typeof_unqual__'" },
	};
	int m;
	const char *n;

	if (c->o->std >= 23)
		return NULL;
	n = cc_identifier_name(id, &m);
	for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
		if ((int)strlen(t[i].w) == m && !memcmp(t[i].w, n, (size_t)m))
			return t[i].h;
	return NULL;
}

CC_PRIVATE void cc_typo_c23_word_help(cc *c, diag_record *d, uint32_t id)
{
	const char *h = cc_typo_c23_word(c, id);

	if (h)
		diag_record_help_set(c->p, d, "%s", h);
}

/* "<msg> 'id'; did you mean 'x'?" with the fix-it and the "declared here" note,
   or just "<msg> 'id'"; returns the correction, a symbol or with kws (KWS_*) and kw
   a keyword in *kw */
CC_PRIVATE uint32_t cc_undeclared_report(cc *c, const char *msg, uint32_t id, uint32_t loc, bool types, int kws, uint32_t *kw)
{
	/* clang does not correct a name from a macro expansion (and says where it was spelled) */
	bool mac = (tok(c)->loc == loc ? tok(c)->flags : c->prevflags) & PP_TOKEN_FLAG_FROM_MACRO;
	uint32_t k = 0, s = mac ? 0 : cc_typo_symbol_find(c, id, types, kw ? kws : 0, &k);
	diag_span r = { loc, pp_token_end_find(c->p, loc) };
	bool tn = msg[1] == 'n';	/* "unknown type name", else "use of undeclared identifier" */

	if (kw)
		*kw = k;
	if (mac) {
		int m;
		const char *t = cc_identifier_name(id, &m);
		diag_info d = { "error", loc, &r, 1, NULL, 0, t, (uint32_t)m };

		cc_report_error_limit(c), c->errors++, c->ndiag++;
		diag_report_extended(c->p, &d, tn ? "unknown type name '%.*s'" : "use of undeclared identifier '%.*s'", CC_IDENTIFIER(id));
		return 0;
	}

	if (s || k) {
		int m;
		const char *t = cc_identifier_name(s ? c->syms[s].name : k, &m);
		char *fs = cc_heap_allocate(c, (size_t)m + 1);
		diag_fixit fx;

		memcpy(fs, t, m), fs[m] = 0;
		fx = (diag_fixit){ r.b, r.e, fs, DIAG_APPLICABILITY_MAYBE };
		report(c, "error", loc, &r, 1, &fx, 1, tn ? "unknown type name '%.*s'; did you mean '%s'?"
		       : "use of undeclared identifier '%.*s'; did you mean '%s'?", CC_IDENTIFIER(id), fs);
		if (s)
			cc_report_note(c, c->syms[s].loc, "'%s' declared here", fs);
	} else {
		/* clang: a caret alone under an unknown type name, a range under an undeclared identifier */
		const char *h = cc_library_header(c, id), *w = cc_typo_c23_word(c, id);

		if (h || w) {
			diag_record *d;

			cc_report_error_limit(c), c->errors++, c->ndiag++;
			d = diag_record_begin(c->p, DIAG_LEVEL_ERROR, loc, tn ? "unknown type name '%.*s'" : "use of undeclared identifier '%.*s'",
					  CC_IDENTIFIER(id));
			if (!tn)
				diag_record_label_add(c->p, d, r.b, r.e, NULL);
			if (w)
				diag_record_help_set(c->p, d, "%s", w);
			else
				diag_record_help_set(c->p, d, "'%.*s' is declared in <%s>", CC_IDENTIFIER(id), h);
			if (h)
				cc_library_include_fixit(c, d, loc, h);
			diag_record_emit(c->p, d);
		} else
			report(c, "error", loc, &r, tn ? 0 : 1, NULL, 0, tn ? "unknown type name '%.*s'"
			       : "use of undeclared identifier '%.*s'", CC_IDENTIFIER(id));
	}
	return s;
}

CC_PRIVATE bool cc_token_is_typedef_name(cc *c, const ptok *t)
{
	if (t->kind != LEX_TOKEN_IDENTIFIER || cc_token_keyword(t))
		return false;
	return cc_symbol_is_typedef_identifier(c, t->id);
}
