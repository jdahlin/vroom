/* vcc preprocessor: header cache: may an #include be a region, and does an entry's recorded input match this TU */
#include "pp-internal.h"

/* can an #include here be a region? */
VPRIV bool pp_header_cache_region_is_allowed(const pp *p)
{
	return p->header_cache_on && p->options_signature != PP_HEADER_CACHE_NONE && p->recording.level_count < PP_HEADER_CACHE_LEVEL_MAX && p->context_depth == p->file_context_count && !p->scratch_token_count && !p->owed_spacing &&
	       !p->argument_preexpansion && !p->collecting_arguments && !p->in_directive;
}

/* exclusions along a path of child entries: reads of these ids are not inputs */
typedef struct pp_header_cache_exclusion_set {
	const uint32_t *ex;
	uint32_t n;
} pp_header_cache_exclusion_set;

static bool pp_header_cache_exclusion_set_has(const pp_header_cache_exclusion_set *xs, int lex_character_next, uint32_t id)
{
	for (int f = 0; f < lex_character_next; f++) {
		uint32_t lo = 0, hi = xs[f].n;

		while (lo < hi) {
			uint32_t m = (lo + hi) / 2;

			if (xs[f].ex[m] < id)
				lo = m + 1;
			else
				hi = m;
		}
		if (lo < xs[f].n && xs[f].ex[lo] == id)
			return true;
	}
	return false;
}

/* the search directories e depended on (with its children) are not this TU's: its own lookups must give the same answers */
static inline bool pp_header_cache_search_directories_differ(const pp *p, const pp_header_cache_entry *e)
{
	return (int)e->search_directory_count > p->options->ndirs || p->search_path_signatures[e->search_directory_count] != e->search_path_signature;
}

/* e's own reads (guards first) hold in this state, except excluded ids */
static bool pp_header_cache_own_reads_valid(pp *p, const pp_header_cache_entry *e, const pp_header_cache_exclusion_set *xs, int lex_character_next)
{
	PP_HEADER_CACHE_VECTOR_FOR(e->guard_reads, pp_header_cache_read, r)
		if ((pp_macro_table_get(p, r->id) != 0) != (r->value != 0) && !(lex_character_next && pp_header_cache_exclusion_set_has(xs, lex_character_next, r->id)))
			return false;
	PP_HEADER_CACHE_VECTOR_FOR(e->undefined_reads, uint32_t, r)
		if (pp_macro_table_get(p, *r) && !(lex_character_next && pp_header_cache_exclusion_set_has(xs, lex_character_next, *r)))
			return false;
	PP_HEADER_CACHE_VECTOR_FOR(e->value_reads, pp_header_cache_read, r) {
		uintptr_t c = pp_macro_table_get(p, r->id) & ~(uintptr_t)1, v = r->value;

		if ((v == PP_HEADER_CACHE_DEFINED ? !c : c != v) && !(lex_character_next && pp_header_cache_exclusion_set_has(xs, lex_character_next, r->id)))
			return false;
	}
	return !e->lookup_count || !pp_header_cache_search_directories_differ(p, e) || pp_header_cache_lookups_match(p, e);
}

static bool pp_header_cache_tree_valid(pp *p, const pp_header_cache_entry *e, pp_header_cache_exclusion_set *xs, int lex_character_next);

/* e's child entries' reads hold (a verdict without exclusions is memoised per pp_header_cache_entry_serve) */
static bool pp_header_cache_children_valid(pp *p, const pp_header_cache_entry *e, pp_header_cache_exclusion_set *xs, int lex_character_next)
{
	for (uint32_t i = 0; i < e->child_count; i++) {
		const pp_header_cache_child *k = &e->children[i];
		bool ok;

		if (!lex_character_next && !k->exclusion_count) {
			uint32_t h = (uint32_t)((((uintptr_t)k->e >> 4) * 0x9E3779B97F4A7C15ull) >> 52);

			if (p->child_verdicts[h].e == k->e && p->child_verdicts[h].gen == p->child_verdict_generation) {
				ok = p->child_verdicts[h].ok;
			} else {
				ok = pp_header_cache_tree_valid(p, k->e, xs, 0);
				p->child_verdicts[h].e = k->e, p->child_verdicts[h].gen = p->child_verdict_generation, p->child_verdicts[h].ok = ok;
			}
		} else {
			int m = lex_character_next;

			if (k->exclusion_count && lex_character_next < PP_HEADER_CACHE_LEVEL_MAX)
				xs[m++] = (pp_header_cache_exclusion_set){ k->exclusions, k->exclusion_count };
			ok = pp_header_cache_tree_valid(p, k->e, xs, m);
		}
		if (!ok)
			return false;
	}
	return true;
}

static bool pp_header_cache_tree_valid(pp *p, const pp_header_cache_entry *e, pp_header_cache_exclusion_set *xs, int lex_character_next)
{
	return pp_header_cache_own_reads_valid(p, e, xs, lex_character_next) && pp_header_cache_children_valid(p, e, xs, lex_character_next);
}

VPRIV bool pp_header_cache_entry_is_valid(pp *p, const pp_header_cache_entry *e)
{
	pp_header_cache_exclusion_set xs[PP_HEADER_CACHE_LEVEL_MAX + 1];

	if (e->include_level >= 0 && e->include_level != p->file_context_count)
		return false;
	PP_HEADER_CACHE_VECTOR_FOR(e->guard_reads, pp_header_cache_read, r)
		if ((pp_macro_table_get(p, r->id) != 0) != (r->value != 0))
			return false;
	for (uint32_t i = 0; i < e->once_mark_count; i++)
		if ((pp_once_file_find(p, e->once_marks[i].f) >= 0) != (bool)e->once_marks[i].was_present)
			return false;
	PP_HEADER_CACHE_VECTOR_FOR(e->undefined_reads, uint32_t, r)
		if (pp_macro_table_get(p, *r))
			return false;
	PP_HEADER_CACHE_VECTOR_FOR(e->value_reads, pp_header_cache_read, r) {
		uintptr_t c = pp_macro_table_get(p, r->id) & ~(uintptr_t)1, v = r->value;

		if (v == PP_HEADER_CACHE_DEFINED ? !c : c != v)
			return false;
	}
	if (e->lookup_count && pp_header_cache_search_directories_differ(p, e) && !pp_header_cache_lookups_match(p, e))
		return false;
	if (!pp_header_cache_children_valid(p, e, xs, 0))
		return false;	/* (before the costly counter-name lookups) */
	for (uint32_t i = 0; i < e->counter_name_read_count; i++) {
		const pp_header_cache_counter_name_read *r = &e->counter_name_reads[i];
		char b[4096 + 40];
		int dl = pp_unsigned_format(b + r->off, p->counter_value + r->relative);
		size_t n = r->off + (size_t)dl + (r->len - r->off - r->digit_length);
		uint32_t id;

		memcpy(b, r->s, r->off);
		memcpy(b + r->off + dl, r->s + r->off + r->digit_length, r->len - r->off - r->digit_length);
		memset(b + n, 0, 16);
		id = vintern_find(b, n, vintern_hash(b, n));
		if (id && pp_macro_table_get(p, id))
			return false;
	}
	return true;
}
