/* vcc preprocessor: header cache: serving a valid entry, as tokens to pp_token_next or as text to pp_text_render */
#include "pp-internal.h"

static void pp_header_cache_serve_push(pp *p, const pp_header_cache_entry *e, uint32_t base, unsigned cbase, uint8_t top, uint8_t norec)
{
	pp_context *c = pp_context_push(p);

	c->type = PP_CONTEXT_SERVED;
	c->stored_tokens = e->own_tokens;
	c->tokens = e->escape_tokens;
	c->source = pp_shared_base();
	c->count = (uint32_t)e->own_length;
	c->location = base;
	c->entry = e;
	c->next_segment = c->next_counter_run = 0;
	c->counter_base = cbase;
	c->top_level = top;
	c->unrecorded = norec;
}

/* a child entry's tokens go before the next own token: pushed */
VPRIV bool pp_header_cache_serve_child(pp *p, pp_context *c)
{
	const pp_header_cache_entry *e = c->entry;
	const pp_header_cache_segment *g;

	if (c->next_segment >= e->segment_count || e->segments[c->next_segment].at != c->index)
		return false;
	g = &e->segments[c->next_segment++];
	pp_header_cache_serve_push(p, g->child, c->location + g->location_base_delta, c->counter_base + g->counter_delta, 0, c->unrecorded);
	return true;
}

/* __COUNTER__ digits in the spelling of own token k: this TU's values */
static __attribute__((noinline)) void pp_header_cache_counter_spell(pp *p, const pp_header_cache_entry *e, uint32_t *ci, uint32_t k, unsigned cbase, ptok *t)
{
	char *b = varena_alloc(&p->permanent_region, t->len + 24 * 4 + 32), *w = b;
	uint32_t at = 0;

	while (*ci < e->counter_run_count && e->counter_runs[*ci].off == k) {
		const pp_header_cache_counter_run *h = &e->counter_runs[(*ci)++];
		uint32_t bo = h->len >> 16, bl = h->len & 0xffff;

		memcpy(w, t->s + at, bo - at);
		w += bo - at;
		w += pp_unsigned_format(w, cbase + h->relative);
		at = bo + bl;
	}
	memcpy(w, t->s + at, t->len - at);
	w += t->len - at;
	memset(w, 0, 16);
	t->s = b;
	t->len = (uint32_t)(w - b);
	if (t->kind == LEX_TOKEN_IDENTIFIER)
		t->id = vintern_copy(b, t->len);
}

VPRIV void pp_header_cache_serve_token(pp *p, pp_context *c, ptok *t)
{
	const pp_header_cache_entry *e = c->entry;

	pp_header_cache_token_load(t, &c->stored_tokens[c->index], c->tokens, c->source, c->location);
	t->ctr = 0;
	p->token_unrecorded = c->unrecorded;
	if (t->kind == LEX_TOKEN_IDENTIFIER)
		t->flags |= PP_TOKEN_FLAG_NO_EXPAND;	/* handed out as recorded: expanded already */
	if (unlikely(c->next_counter_run < e->counter_run_count && e->counter_runs[c->next_counter_run].off == c->index))
		pp_header_cache_counter_spell(p, e, &c->next_counter_run, c->index, c->counter_base, t);
	c->index++;
}

/* ---------------- lazy bodies (pp.h) ---------------- */

int pp_lazy_body_skip(pp *p, const ptok *lb, pp_lazy *z)
{
	pp_context *c = &p->contexts[p->context_depth - 1];
	const pp_header_cache_entry *e;
	uint32_t k, lo, hi;

	if (c->type != PP_CONTEXT_SERVED || !c->index || lb->kind != LEX_TOKEN_LEFT_BRACE || p->owed_spacing || (p->recording.on && !c->unrecorded))
		return 0;
	e = c->entry;
	k = c->index - 1;
	if (e->own_tokens[k].kind != LEX_TOKEN_LEFT_BRACE || !e->own_tokens[k].loc || lb->loc != c->location + e->own_tokens[k].loc - 1)
		return 0;
	lo = 0, hi = e->brace_pair_count;
	while (lo < hi) {
		uint32_t m = (lo + hi) / 2;

		if (e->brace_pairs[2 * m] < k)
			lo = m + 1;
		else
			hi = m;
	}
	if (lo >= e->brace_pair_count || e->brace_pairs[2 * lo] != k)
		return 0;
	*z = (pp_lazy){ e, k, e->brace_pairs[2 * lo + 1], c->location, c->counter_base, c->next_counter_run, k, 0, 0 };
	z->rloc = e->own_tokens[z->close].loc ? c->location + e->own_tokens[z->close].loc - 1 : 0;
	z->rflags = e->own_tokens[z->close].flags;
	c->index = z->close + 1;
	while (c->next_counter_run < e->counter_run_count && e->counter_runs[c->next_counter_run].off <= z->close)
		c->next_counter_run++;
	p->tokens_delivered += z->close - k;
	return 1;
}

void pp_lazy_body_token_get(pp *p, pp_lazy *z, ptok *t)
{
	const pp_header_cache_entry *e = z->e;
	uint32_t k = z->k++;

	pp_header_cache_token_load(t, &e->own_tokens[k], e->escape_tokens, pp_shared_base(), z->base);
	if (t->kind == LEX_TOKEN_IDENTIFIER)
		t->flags |= PP_TOKEN_FLAG_NO_EXPAND;
	while (z->ci < e->counter_run_count && e->counter_runs[z->ci].off < k)
		z->ci++;
	if (unlikely(z->ci < e->counter_run_count && e->counter_runs[z->ci].off == k))
		pp_header_cache_counter_spell(p, e, &z->ci, k, z->cbase, t);
}
/* e's delta: its children's in order, then its own */
static void pp_header_cache_delta_apply(pp *p, const pp_header_cache_entry *e)
{
	for (uint32_t i = 0; i < e->child_count; i++)
		pp_header_cache_delta_apply(p, e->children[i].e);
	PP_HEADER_CACHE_VECTOR_FOR(e->writes, pp_header_cache_read, x)
		pp_macro_table_set(p, x->id, (const pp_macro *)x->value);
}

static void pp_header_cache_entry_apply(pp *p, const pp_header_cache_entry *e)
{
	unsigned c0 = p->counter_value;
	struct pp_recording *r = &p->recording;
	pp_header_cache_level *L = r->level_count ? &r->levels[r->level_count - 1] : NULL;

	p->header_cache_last_entry = e;

	if (p->output_mode == 2) {
		if (e->has_content) {
			/*
			 * Recording: the entry's tokens are a segment of the enclosing
			 * levels, which publish skips; reserve their positions instead of
			 * copying every token (pp_header_cache_token_record was ~13% of vccd).
			 */
			bool reserve = false;

			if (L) {
				if (r->segment_count < PP_HEADER_CACHE_SEGMENT_MAX && r->token_count + e->total_length < PP_HEADER_CACHE_TOKEN_MAX) {
					r->segments[r->segment_count++] = (pp_header_cache_recorded_segment){ r->token_count, r->token_count + e->total_length, e, c0, p->next_location_base };
					r->token_count += e->total_length;
					reserve = true;
				} else {
					PP_HEADER_CACHE_REGION_REJECT(p);
				}
			}
			pp_header_cache_serve_push(p, e, p->next_location_base, c0, 1, reserve);
		}
	} else if (e->has_content) {
		size_t st;

		if (L)
			pp_header_cache_text_start(p, e->first_at_line_start);
		pp_output_reserve(p, e->total_length + 16 * (size_t)e->counter_run_count);
		st = p->output_length;
		pp_header_cache_text_serve(p, e, c0);
		p->output_column = e->end_column;
		p->directive_on_line = e->directive_on_line_at_end;
		p->previous_kind = e->previous_kind, p->previous_is_wide_prefix = e->previous_is_wide_prefix, p->previous_after_period = e->previous_after_period, p->previous_end = NULL;
		if (L) {
			if (r->segment_count < PP_HEADER_CACHE_SEGMENT_MAX)
				r->segments[r->segment_count++] = (pp_header_cache_recorded_segment){ st, p->output_length, e, c0, 0 };
			else
				PP_HEADER_CACHE_REGION_REJECT(p);
		}
	}
	if (p->output_mode != 2 && p->output_text_mode)
		pp_output_file_change(p, pp_file_context_current(p), 0);
	if (L) {
		/* the entry's inputs and writes are the enclosing level's too */
		for (int i = 0; i < r->level_count; i++) {
			if (e->include_level >= 0)
				r->levels[i].uses_level = 1;
			if (r->levels[i].ndirs < e->search_directory_count)
				r->levels[i].ndirs = e->search_directory_count;
		}
		pp_header_cache_child_attach(p, e);	/* (its reads and writes stay in it) */
		for (uint32_t i = 0; i < e->once_mark_count; i++) {
			if (e->once_marks[i].was_present && pp_once_file_find(p, e->once_marks[i].f) >= (int)L->once_files_at_start)
				continue;	/* marked by this level itself */
			if (r->once_mark_count < PP_HEADER_CACHE_ONCE_MARK_MAX)
				r->once_marks[r->once_mark_count++] = e->once_marks[i];
			else
				PP_HEADER_CACHE_REGION_REJECT(p);
		}
		for (uint32_t i = 0; i < e->counter_name_read_count; i++) {
			const pp_header_cache_counter_name_read *c = &e->counter_name_reads[i];

			if (r->counter_read_count < PP_HEADER_CACHE_COUNTER_READ_MAX)
				r->counter_reads[r->counter_read_count++] = (pp_header_cache_counter_read){ c->s, c->len, c->off, c->digit_length, c0 + c->relative };
			else
				PP_HEADER_CACHE_REGION_REJECT(p);
		}
	}
	for (uint32_t i = 0; i < e->file_count; i++) {
		const pp_header_cache_file *h = &e->files[i];
		pp_file_entry *d = &p->files[p->file_count++];

		d->location_base = p->next_location_base + h->record_base;
		d->diagnostic_state = p->diagnostic_current;
		d->lex_file = h->f;
		d->skeleton = h->fi;
		d->name = h->name;
		d->name_length = h->namelen;
		d->spelled_path = h->spath;
		d->is_system = h->sys;
	}
	p->next_location_base += e->token_span;
	pp_header_cache_delta_apply(p, e);
	for (uint32_t i = 0; i < e->once_addition_count; i++)
		pp_once_file_add(p, e->once_additions[i]);
	p->counter_value += e->counter_delta;
	if (p->output_mode != 2 || !e->has_content)
		p->owed_spacing = e->owed_spacing_at_end;	/* (tokens: when the replay is done) */
	atomic_fetch_add_explicit(&pp_header_cache_statistics.hit, 1, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.bytes_replayed, e->total_length, memory_order_relaxed);
}

static void pp_header_cache_miss_explain(const pp *p, const pp_header_cache_key *k, const pp_header_cache_entry *e)
{
	char b[512];
	int n = 0;

	PP_HEADER_CACHE_VECTOR_FOR(e->guard_reads, pp_header_cache_read, x)
		if (!n && (pp_macro_table_get(p, x->id) != 0) != (x->value != 0)) {
			const vident *d = &vintern_ents[x->id];

			n = snprintf(b, sizeof(b), "hc-miss %s: guard %.*s %s\n", k->spath, (int)d->len, d->s,
				     x->value ? "def/undef" : "undef/def");
		}
	for (uint32_t i = 0; i < e->once_mark_count && !n; i++)
		if ((pp_once_file_find(p, e->once_marks[i].f) >= 0) != (bool)e->once_marks[i].was_present)
			n = snprintf(b, sizeof(b), "hc-miss %s: once %s\n", k->spath, e->once_marks[i].f->path);
	PP_HEADER_CACHE_VECTOR_FOR(e->undefined_reads, uint32_t, x)
		if (!n && pp_macro_table_get(p, *x)) {
			const vident *d = &vintern_ents[*x];

			n = snprintf(b, sizeof(b), "hc-miss %s: %.*s undef/def\n", k->spath, (int)d->len, d->s);
		}
	PP_HEADER_CACHE_VECTOR_FOR(e->value_reads, pp_header_cache_read, x) {
		uintptr_t c = pp_macro_table_get(p, x->id) & ~(uintptr_t)1, v = x->value;

		if (!n && (v == PP_HEADER_CACHE_DEFINED ? !c : c != v)) {
			const vident *d = &vintern_ents[x->id];

			n = snprintf(b, sizeof(b), "hc-miss %s: %.*s %s/%s\n", k->spath, (int)d->len, d->s,
				     v == PP_HEADER_CACHE_DEFINED ? "def" : "val", c ? "def" : "undef");
		}
	}
	if (!n)
		n = snprintf(b, sizeof(b), "hc-miss %s: counter name or a child entry\n", k->spath);
	(void)!write(2, b, (size_t)n);
}

/* replay a matching entry for this #include; true if done */
VPRIV bool pp_header_cache_entry_serve(pp *p, pp_header_cache_key *k)
{
	static _Atomic int dbg_ = -1;
	int dbg;
	const pp_header_cache_entry *h = atomic_load_explicit(&k->head, memory_order_acquire);

	p->child_verdict_generation++;	/* (child entry verdicts are for one state) */
	for (const pp_header_cache_entry *e = h; e; e = e->next_variant)
		if (pp_header_cache_entry_is_valid(p, e)) {
			pp_header_cache_entry_apply(p, e);
			return true;
		}
	if ((dbg = atomic_load_explicit(&dbg_, memory_order_relaxed)) < 0)
		atomic_store_explicit(&dbg_, dbg = getenv("VPP_HC_DEBUG") != NULL, memory_order_relaxed);
	if (dbg && h)
		pp_header_cache_miss_explain(p, k, h);
	return false;
}
/* an entry's text, children spliced in, __COUNTER__ values from base */
VPRIV void pp_header_cache_text_serve(pp *p, const pp_header_cache_entry *e, unsigned base)
{
	uint32_t pos = 0, ci = 0;

	for (uint32_t si = 0; si <= e->segment_count; si++) {
		uint32_t to = si < e->segment_count ? e->segments[si].at : (uint32_t)e->own_length;

		while (ci < e->counter_run_count && e->counter_runs[ci].off < to) {
			const pp_header_cache_counter_run *c = &e->counter_runs[ci++];

			pp_output_append(p, e->own_text + pos, c->off - pos);
			pp_output_reserve(p, 24);
			p->output_length += (size_t)pp_unsigned_format(p->output + p->output_length, base + c->relative);
			pos = c->off + c->len;
		}
		pp_output_append(p, e->own_text + pos, to - pos);
		pos = to;
		if (si < e->segment_count)
			pp_header_cache_text_serve(p, e->segments[si].child, base + e->segments[si].counter_delta);
	}
}
