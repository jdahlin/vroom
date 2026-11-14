/* vcc preprocessor: header cache: building a region's entry when it ends and publishing it */
#include "pp-internal.h"

static void *pp_header_cache_copy(pp *p, const void *s, size_t n)
{
	void *d = n ? pp_header_cache_allocate(p, n) : NULL;

	if (n)
		memcpy(d, s, n);
	return d;
}

static const char *pp_header_cache_string_share(pp *p, const char *s, size_t n)
{
	const char *sb = pp_shared_base();

	if (s >= sb && s < sb + (1ull << 34) && !s[n])
		return s;	/* already shared (a replayed entry's): immortal */
	return pp_header_cache_blob_get(p, s, n);	/* NUL-terminated, shared */
}

/* pp_header_cache_blob_get of a token's spelling, through a small cache of recent ones */
static const char *pp_header_cache_spelling_blob(pp *p, const char *s, uint32_t len, uint8_t kind)
{
	uint64_t w;
	uint32_t h;
	const char *d;

	if (kind >= LEX_TOKEN_LEFT_SQUARE && kind < LEX_TOKEN_KIND_COUNT && len <= 4) {
		/* punctuators: one spelling per kind (digraphs aside) */
		uint32_t x, y, mk = len == 4 ? ~0u : (1u << (8 * len)) - 1;

		d = p->punctuator_blobs[kind];
		memcpy(&x, s, 4);	/* (readable past the end) */
		if (d) {
			memcpy(&y, d, 4);
			if (((const pp_header_cache_chunk *)(const void *)(d - offsetof(pp_header_cache_chunk, d)))->n == len && !((x ^ y) & mk))
				return d;
		}
		return p->punctuator_blobs[kind] = pp_header_cache_blob_get(p, s, len);
	}
	memcpy(&w, s, 8);	/* (spellings are readable 16 bytes past their end) */
	if (len < 8)
		w &= ~(~0ull << (8 * len));
	h = (uint32_t)(((w ^ len) * 0x9E3779B97F4A7C15ull) >> 52);
	d = p->blob_cache[h];
	if (d && ((const pp_header_cache_chunk *)(const void *)(d - offsetof(pp_header_cache_chunk, d)))->n == len && !memcmp(d, s, len))
		return d;
	d = pp_header_cache_blob_get(p, s, len);
	p->blob_cache[h] = d;
	return d;
}

/* tokens [L->text0, end) minus the children's: e's own tokens */
static void pp_header_cache_tokens_collect(pp *p, const pp_header_cache_level *L, size_t end, pp_header_cache_entry *e)
{
	struct pp_recording *r = &p->recording;
	uint32_t ns = r->segment_count - L->segments_at_start, nc = r->counter_position_count - L->counter_positions_at_start;
	size_t own = end - L->text_at_start, w = 0, at = L->text_at_start, skipped = 0;
	pp_header_cache_segment *sg = pp_header_cache_allocate(p, (ns ? ns : 1) * sizeof(pp_header_cache_segment));
	pp_header_cache_counter_run *ct = pp_header_cache_allocate(p, (nc ? nc : 1) * sizeof(pp_header_cache_counter_run));
	uint32_t ci = L->counter_positions_at_start;
	pp_header_cache_token *tk;
	ptok *xt = NULL;
	uint32_t lex_character_next = 0, xcap = 0;
	const char *sb = pp_shared_base();

	for (uint32_t i = L->segments_at_start; i < r->segment_count; i++)
		own -= r->segments[i].end - r->segments[i].start;
	tk = varena_alloc(&p->temporary_region, (own ? own : 1) * sizeof(pp_header_cache_token));
	for (uint32_t i = L->segments_at_start; i <= r->segment_count; i++) {
		size_t to = i < r->segment_count ? r->segments[i].start : end;

		for (; ci < r->counter_position_count && r->counter_positions[ci].position < to; ci++)
			ct[ci - L->counter_positions_at_start] = (pp_header_cache_counter_run){ (uint32_t)(r->counter_positions[ci].position - L->text_at_start - skipped), r->counter_positions[ci].len,
						 r->counter_positions[ci].val - L->counter_at_start };
		for (; at < to; at++, w++) {
			ptok t = r->tokens[at];
			pp_header_cache_token *h = &tk[w];

			t.loc = t.loc ? t.loc - L->location_base_at_start + 1 : 0;
			t.ctr = 0;
			*h = (pp_header_cache_token){ 0, t.loc, t.kind, t.flags, (uint16_t)t.len };
			/* (a clean identifier is spelled as its id: --vcc-tokhash checks both) */
			if (t.kind == LEX_TOKEN_IDENTIFIER && t.id && !(t.flags & LEX_TOKEN_FLAG_SPLICED) && t.len <= 0xffff) {
				h->spelling = t.id;
				continue;
			}
			if (t.kind != LEX_TOKEN_IDENTIFIER && !t.id && t.len <= 0xffff && !(t.flags & PP_HEADER_CACHE_TOKEN_ESCAPE)) {
				const char *d = pp_header_cache_spelling_blob(p, t.s, t.len, t.kind);

				if (!sb)
					sb = pp_shared_base();
				h->spelling = (uint32_t)((size_t)(d - sb) >> 4) + 1;
				continue;
			}
			/* escape: the full token, its spelling stored */
			if (lex_character_next == xcap) {
				ptok *n2 = varena_alloc(&p->temporary_region, (xcap = xcap ? 2 * xcap : 16) * sizeof(ptok));

				if (lex_character_next)
					memcpy(n2, xt, lex_character_next * sizeof(ptok));
				xt = n2;
			}
			t.s = pp_header_cache_blob_get(p, t.s, t.len);
			xt[lex_character_next] = t;
			*h = (pp_header_cache_token){ lex_character_next++, t.loc, t.kind, PP_HEADER_CACHE_TOKEN_ESCAPE, 0 };
		}
		if (i < r->segment_count) {
			sg[i - L->segments_at_start] = (pp_header_cache_segment){ (uint32_t)w, r->segments[i].counter_at_start - L->counter_at_start, r->segments[i].child,
						r->segments[i].location_base_at_start - L->location_base_at_start };
			skipped += r->segments[i].end - r->segments[i].start;
			at = r->segments[i].end;
		}
	}
	{
		/* outermost brace pairs with no child spliced in (pp_lazy_body_skip) */
		uint32_t nb = 0, depth = 0, open = 0, si = 0, *br;

		for (size_t k = 0; k < w; k++)
			nb += tk[k].kind == LEX_TOKEN_LEFT_BRACE;
		br = pp_header_cache_allocate(p, (nb ? nb : 1) * 2 * sizeof(uint32_t));
		nb = 0;
		for (size_t k = 0; k < w; k++) {
			if (tk[k].kind == LEX_TOKEN_LEFT_BRACE) {
				if (!depth++)
					open = (uint32_t)k;
			} else if (tk[k].kind == LEX_TOKEN_RIGHT_BRACE && depth && !--depth) {
				while (si < ns && sg[si].at <= open)
					si++;
				if (si >= ns || sg[si].at > k)
					br[2 * nb] = open, br[2 * nb + 1] = (uint32_t)k, nb++;
			}
		}
		e->brace_pairs = br;
		e->brace_pair_count = nb;
	}
	e->own_tokens = pp_header_cache_blob_get(p, tk, own * sizeof(pp_header_cache_token));	/* (16-aligned) */
	e->escape_tokens = lex_character_next ? pp_header_cache_copy(p, xt, lex_character_next * sizeof(ptok)) : NULL;
	e->own_length = own;
	e->total_length = end - L->text_at_start;
	e->segments = sg;
	e->segment_count = ns;
	e->counter_runs = ct;
	e->counter_run_count = nc;
	e->has_content = 1;
}

#define PP_ROUND_UP_16(n) (((size_t)(n) + 15) & ~(size_t)15)
/* bytes an entry holds before sharing, by kind (VPP_HC_DEBUG; VPP_HC_PUB=1: a line per entry) */
static void pp_header_cache_bytes_account(const pp_header_cache_entry *e, const pp_header_cache_key *k)
{
	static _Atomic int pub_ = -1;
	size_t rd = PP_ROUND_UP_16(e->guard_reads.n * sizeof(pp_header_cache_read)) + PP_ROUND_UP_16(e->undefined_reads.n * 4) + PP_ROUND_UP_16(e->value_reads.n * sizeof(pp_header_cache_read)) +
		    PP_ROUND_UP_16(e->counter_name_read_count * sizeof(pp_header_cache_counter_name_read));
	size_t dl = PP_ROUND_UP_16(e->writes.n * sizeof(pp_header_cache_read)), on = PP_ROUND_UP_16(e->once_mark_count * sizeof(pp_header_cache_once_mark)) + PP_ROUND_UP_16(e->once_addition_count * 8);
	size_t fe = PP_ROUND_UP_16(e->file_count * sizeof(pp_header_cache_file)), tx, sg = PP_ROUND_UP_16(e->segment_count * sizeof(pp_header_cache_segment)) + PP_ROUND_UP_16(e->counter_run_count * sizeof(pp_header_cache_counter_run));
	int pub;

	for (uint32_t i = 0; i < e->file_count; i++)
		fe += PP_ROUND_UP_16(e->files[i].namelen + 1) + PP_ROUND_UP_16(strlen(e->files[i].spath) + 1);
	tx = e->own_tokens ? PP_ROUND_UP_16(e->own_length * sizeof(*e->own_tokens)) : PP_ROUND_UP_16(e->own_length + 1);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_rd, rd, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_dl, dl, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_once, on, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_fe, fe, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_text, tx, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_seg, sg, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.b_ent, PP_ROUND_UP_16(sizeof(pp_header_cache_entry)), memory_order_relaxed);
	if ((pub = atomic_load_explicit(&pub_, memory_order_relaxed)) < 0)
		atomic_store_explicit(&pub_, pub = getenv("VPP_HC_PUB") != NULL, memory_order_relaxed);
	if (pub) {
		char b[600];
		int n = snprintf(b, sizeof(b), "hc-pub %s %zu %zu %zu %zu %zu %zu\n", k->spath, rd, dl, on, fe, tx, sg);

		(void)!write(2, b, (size_t)n);
	}
}

/* the entry for the innermost level, which ends at output offset end */
static pp_header_cache_entry *pp_header_cache_entry_publish(pp *p, const pp_header_cache_level *L, size_t end)
{
	struct pp_recording *r = &p->recording;
	pp_header_cache_entry *e = pp_header_cache_allocate(p, sizeof(*e));
	uint32_t nrg = 0, nru = 0, nrv = 0, ndl = 0, *ru;
	pp_header_cache_read *rg, *rv, *dl;
	pp_header_cache_file *fe;

	memset(e, 0, sizeof(*e));
	for (uint32_t i = L->touches_at_start; i < r->touch_count; i++) {
		const pp_header_cache_touch_record *t = &r->touches[i];

		if (t->flags & PP_HEADER_CACHE_TOUCH_READ) {
			if ((t->flags & PP_HEADER_CACHE_TOUCH_GUARD) && (t->v == PP_HEADER_CACHE_DEFINED || !t->v))
				nrg++;
			else if (t->v)
				nrv++;
			else
				nru++;
		}
		ndl += (t->flags & PP_HEADER_CACHE_TOUCH_OWN) != 0;
	}
	/* scratch in tmp: the stored form is the shared chunks */
	rg = varena_alloc(&p->temporary_region, (nrg ? nrg : 1) * sizeof(*rg));
	ru = varena_alloc(&p->temporary_region, (nru ? nru : 1) * sizeof(*ru));
	rv = varena_alloc(&p->temporary_region, (nrv ? nrv : 1) * sizeof(*rv));
	dl = varena_alloc(&p->temporary_region, (ndl ? ndl : 1) * sizeof(*dl));
	nrg = nru = nrv = ndl = 0;
	for (uint32_t i = L->touches_at_start; i < r->touch_count; i++) {
		const pp_header_cache_touch_record *t = &r->touches[i];

		if (t->flags & PP_HEADER_CACHE_TOUCH_READ) {
			if ((t->flags & PP_HEADER_CACHE_TOUCH_GUARD) && (t->v == PP_HEADER_CACHE_DEFINED || !t->v))
				rg[nrg++] = (pp_header_cache_read){ t->id, 0, t->v };
			else if (t->v)
				rv[nrv++] = (pp_header_cache_read){ t->id, 0, t->v };
			else
				ru[nru++] = t->id;
		}
		if (t->flags & PP_HEADER_CACHE_TOUCH_OWN)	/* (a child's last write is in the child) */
			dl[ndl++] = (pp_header_cache_read){ t->id, 0, pp_macro_table_get(p, t->id) & ~(uintptr_t)1 };
	}
	atomic_fetch_add_explicit(&pp_header_cache_statistics.nru, nru, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.nrv, nrv, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.ndl, ndl, memory_order_relaxed);
	atomic_fetch_add_explicit(&pp_header_cache_statistics.nfe, p->file_count - L->files_at_start, memory_order_relaxed);
	e->guard_reads = pp_header_cache_vector_store(p, rg, nrg, sizeof(*rg));
	e->undefined_reads = pp_header_cache_vector_store(p, ru, nru, sizeof(*ru));
	e->value_reads = pp_header_cache_vector_store(p, rv, nrv, sizeof(*rv));
	e->writes = pp_header_cache_vector_store(p, dl, ndl, sizeof(*dl));
	{
		uint32_t nk = r->child_count - L->children_at_start;
		pp_header_cache_child *kd = pp_header_cache_allocate(p, (nk ? nk : 1) * sizeof(pp_header_cache_child));

		for (uint32_t i = 0; i < nk; i++) {
			const pp_header_cache_recorded_child *k = &r->children[L->children_at_start + i];

			kd[i] = (pp_header_cache_child){ k->e, k->exclusion_count ? pp_header_cache_copy(p, r->child_exclusions + k->exclusions_start, k->exclusion_count * sizeof(uint32_t)) : NULL, k->exclusion_count };
		}
		e->children = kd;
		e->child_count = nk;
	}
	e->lookup_count = r->lookup_count - L->lookups_at_start;
	if (e->lookup_count) {
		pp_header_cache_lookup *lk = pp_header_cache_allocate(p, e->lookup_count * sizeof(pp_header_cache_lookup));

		for (uint32_t i = 0; i < e->lookup_count; i++) {
			const pp_header_cache_lookup *l = &r->lookups[L->lookups_at_start + i];

			lk[i] = *l;
			lk[i].name = pp_header_cache_string_share(p, l->name, l->name_length);
			lk[i].from = l->from ? pp_header_cache_string_share(p, l->from, strlen(l->from)) : NULL;
			lk[i].path = l->path ? pp_header_cache_string_share(p, l->path, strlen(l->path)) : NULL;
		}
		e->lookups = lk;
	}
	e->once_marks = pp_header_cache_copy(p, r->once_marks + L->once_marks_at_start, (r->once_mark_count - L->once_marks_at_start) * sizeof(pp_header_cache_once_mark));
	e->once_mark_count = r->once_mark_count - L->once_marks_at_start;
	e->once_additions = pp_header_cache_copy(p, p->once_files + L->once_files_at_start, (p->once_file_count - L->once_files_at_start) * sizeof(*p->once_files));
	e->once_addition_count = p->once_file_count - L->once_files_at_start;
	e->file_count = p->file_count - L->files_at_start;
	fe = pp_header_cache_allocate(p, (e->file_count ? e->file_count : 1) * sizeof(pp_header_cache_file));
	for (uint32_t i = 0; i < e->file_count; i++) {
		const pp_file_entry *f = &p->files[L->files_at_start + i];

		fe[i] = (pp_header_cache_file){ f->lex_file, f->skeleton, pp_header_cache_string_share(p, f->name, f->name_length), pp_header_cache_string_share(p, f->spelled_path, strlen(f->spelled_path)),
				 f->name_length, f->location_base - L->location_base_at_start, f->is_system, f->guard_open };
	}
	e->files = fe;
	e->token_span = p->next_location_base - L->location_base_at_start;
	e->counter_delta = p->counter_value - L->counter_at_start;
	e->owed_spacing_at_end = p->owed_spacing;
	e->include_level = L->uses_level ? (int32_t)L->nfile : -1;
	e->search_directory_count = L->ndirs;
	e->search_path_signature = p->search_path_signatures[L->ndirs];
	{
		pp_header_cache_counter_name_read *cr = pp_header_cache_allocate(p, (r->counter_read_count - L->counter_reads_at_start ? r->counter_read_count - L->counter_reads_at_start : 1) * sizeof(pp_header_cache_counter_name_read));

		for (uint32_t i = L->counter_reads_at_start; i < r->counter_read_count; i++) {
			const pp_header_cache_counter_read *c = &r->counter_reads[i];

			cr[i - L->counter_reads_at_start] = (pp_header_cache_counter_name_read){ pp_header_cache_string_share(p, c->s, c->len), c->len, c->off, c->dlen, c->val - L->counter_at_start };
		}
		e->counter_name_reads = cr;
		e->counter_name_read_count = r->counter_read_count - L->counter_reads_at_start;
	}
	if (p->output_mode == 2) {
		if (end > L->text_at_start)
			pp_header_cache_tokens_collect(p, L, end, e);
	} else if (L->text_started) {
		/* own text: the output minus the children's texts */
		uint32_t ns = r->segment_count - L->segments_at_start, nc = r->counter_position_count - L->counter_positions_at_start;
		size_t own = end - L->text_at_start;
		char *t;
		pp_header_cache_segment *sg = pp_header_cache_allocate(p, (ns ? ns : 1) * sizeof(pp_header_cache_segment));
		pp_header_cache_counter_run *ct = pp_header_cache_allocate(p, (nc ? nc : 1) * sizeof(pp_header_cache_counter_run));
		size_t w = 0, at = L->text_at_start, skipped = 0;
		uint32_t ci = L->counter_positions_at_start;

		for (uint32_t i = L->segments_at_start; i < r->segment_count; i++)
			own -= r->segments[i].end - r->segments[i].start;
		t = varena_alloc(&p->temporary_region, own + 1);
		for (uint32_t i = L->segments_at_start; i <= r->segment_count; i++) {
			size_t to = i < r->segment_count ? r->segments[i].start : end;

			for (; ci < r->counter_position_count && r->counter_positions[ci].position < to; ci++)
				ct[ci - L->counter_positions_at_start] = (pp_header_cache_counter_run){ (uint32_t)(r->counter_positions[ci].position - L->text_at_start - skipped), r->counter_positions[ci].len,
							 r->counter_positions[ci].val - L->counter_at_start };
			memcpy(t + w, p->output + at, to - at);
			w += to - at;
			if (i < r->segment_count) {
				sg[i - L->segments_at_start] = (pp_header_cache_segment){ (uint32_t)w, r->segments[i].counter_at_start - L->counter_at_start, r->segments[i].child, 0 };
				skipped += r->segments[i].end - r->segments[i].start;
				at = r->segments[i].end;
			}
		}
		e->own_text = pp_header_cache_blob_get(p, t, own);	/* variants often print the same */
		e->own_length = own;
		e->total_length = end - L->text_at_start;
		e->segments = sg;
		e->segment_count = ns;
		e->counter_runs = ct;
		e->counter_run_count = nc;
		e->has_content = 1;
		e->first_at_line_start = L->first_sol;
		e->end_column = p->output_column != 0;
		e->directive_on_line_at_end = p->directive_on_line;
		e->previous_kind = p->previous_kind, e->previous_is_wide_prefix = p->previous_is_wide_prefix, e->previous_after_period = p->previous_after_period;
	}
	pp_header_cache_bytes_account(e, L->key);
	return e;
}

/* the innermost region's file context is being popped */
VPRIV void pp_header_cache_region_end(pp *p)
{
	struct pp_recording *r = &p->recording;
	pp_header_cache_level *L = &r->levels[r->level_count - 1], *P;
	pp_header_cache_key *k = L->key;
	pp_header_cache_entry *e = NULL;
	bool linked = false;
	size_t sz;

	if (r->level_count - 1 < r->uncacheable_levels || p->error_count != L->errors_at_start || p->warning_count != L->warnings_at_start || p->line_mark_count != L->line_marks_at_start ||
	    p->pushed_macro_count != L->pushed_macros_at_start || p->once_file_count < L->once_files_at_start) {
		if (r->level_count - 1 >= r->uncacheable_levels)
			r->uncacheable_levels = r->level_count;	/* the enclosing regions saw it too */
		atomic_fetch_add_explicit(&pp_header_cache_statistics.uncacheable, 1, memory_order_relaxed);
		if (getenv("VPP_HC_DEBUG")) {
			char b[512];
			int n = snprintf(b, sizeof(b), "hc-bad %s: line %d errors %d warns %d nlm %u\n", k->spath,
					 r->uncacheable_line, p->error_count - L->errors_at_start, p->warning_count - L->warnings_at_start, p->line_mark_count - L->line_marks_at_start);

			(void)!write(2, b, (size_t)n);
		}
	} else {
		sz = (r->touch_count - L->touches_at_start) * 16 + (p->file_count - L->files_at_start) * (sizeof(pp_header_cache_file) + 64) + sizeof(pp_header_cache_entry);
		if (pp_header_cache_memory_limit && atomic_load_explicit(&pp_header_cache_statistics.mem, memory_order_relaxed) + sz > pp_header_cache_memory_limit) {
			/* full: the enclosing entries would need this one as a child */
			atomic_fetch_add_explicit(&pp_header_cache_statistics.full, 1, memory_order_relaxed);
			r->uncacheable_levels = r->level_count;
		} else {
			p->entry_bytes = 0;
			e = pp_header_cache_entry_publish(p, L, p->output_mode == 2 ? r->token_count : p->output_length);
			atomic_fetch_add_explicit(&pp_header_cache_statistics.mem, p->entry_bytes, memory_order_relaxed);
			atomic_fetch_add_explicit(&pp_header_cache_statistics.bytes_rec, e->own_length, memory_order_relaxed);
			if (atomic_load_explicit(&k->variant_count, memory_order_relaxed) >= PP_HEADER_CACHE_VARIANT_MAX) {
				/* not listed (too many variants), but a child of the enclosing entries */
				atomic_fetch_add_explicit(&pp_header_cache_statistics.full, 1, memory_order_relaxed);
			} else {
				linked = true;
				atomic_fetch_add_explicit(&pp_header_cache_statistics.rec, 1, memory_order_relaxed);
				atomic_fetch_add_explicit(&k->variant_count, 1, memory_order_relaxed);
				e->next_variant = atomic_load_explicit(&k->head, memory_order_relaxed);
				while (!atomic_compare_exchange_weak_explicit(&k->head, &e->next_variant, e, memory_order_release,
									      memory_order_relaxed))
					;
			}
		}
	}
	if (r->level_count == 1 && p->top_region_recording) {
		pp_top_event_record(p, linked ? e : NULL, 0, 0, 1, 0);
		p->top_region_recording = 0;
	}
	r->level_count--;
	if (r->uncacheable_levels > r->level_count)
		r->uncacheable_levels = r->level_count;
	if (!r->level_count) {
		r->on = 0;
		return;
	}
	/* the level's touches end (its reads and writes are in e, which the enclosing level gets as a child) */
	P = &r->levels[r->level_count - 1];
	for (uint32_t i = r->touch_count; i-- > L->touches_at_start;)
		*pp_header_cache_stamp_slot(p, r->touches[i].id) = r->touches[i].previous_stamp;
	r->touch_count = L->touches_at_start;
	r->child_count = L->children_at_start;
	r->child_exclusion_count = L->child_exclusions_at_start;
	r->lookup_count = L->lookups_at_start;	/* (they are in e, or the enclosing levels are not cacheable either) */
	{
		uint32_t w = L->once_marks_at_start;

		for (uint32_t i = L->once_marks_at_start; i < r->once_mark_count; i++)
			if (!r->once_marks[i].was_present || pp_once_file_find(p, r->once_marks[i].f) < (int)P->once_files_at_start)
				r->once_marks[w++] = r->once_marks[i];
		r->once_mark_count = w;
	}
	if (e) {
		/* the level's own counter digits and children are inside its entry */
		r->counter_position_count = L->counter_positions_at_start;
		r->segment_count = L->segments_at_start;
		if (e->has_content)
			r->segments[r->segment_count++] = (pp_header_cache_recorded_segment){ L->text_at_start, p->output_mode == 2 ? r->token_count : p->output_length, e, L->counter_at_start,
						     L->location_base_at_start };
		pp_header_cache_child_attach(p, e);
	}
}
