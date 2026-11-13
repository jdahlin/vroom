/* vcc preprocessor: header cache: recording what a region reads and writes while it runs */
#include "pp-internal.h"

/* a child entry wrote id: not the level's own write any more */
static inline void pp_header_cache_touch_write_child(pp *p, uint32_t id)
{
	bool isnew;
	pp_header_cache_touch_record *t = pp_header_cache_touch(p, id, &isnew);

	if (t)
		t->flags = (t->flags | PP_HEADER_CACHE_TOUCH_WRITE) & ~(uint32_t)PP_HEADER_CACHE_TOUCH_OWN;
}

VPRIV uint64_t *pp_header_cache_stamp_slot(pp *p, uint32_t id)
{
	uint64_t **pg = &p->recording_stamps[id >> PP_MACRO_PAGE_BITS];

	if (unlikely(!*pg)) {
		*pg = varena_alloc(&p->permanent_region, PP_MACRO_PAGE_SIZE * sizeof(uint64_t));
		memset(*pg, 0, PP_MACRO_PAGE_SIZE * sizeof(uint64_t));
	}
	return &(*pg)[id & (PP_MACRO_PAGE_SIZE - 1)];
}

/* a read of id (entry e) while recording; def: only definedness matters */
VPRIV __attribute__((noinline)) void pp_header_cache_read_record(pp *p, uint32_t id, uintptr_t e, int def)
{
	e &= ~(uintptr_t)1;
	pp_header_cache_touch_read(p, id, def ? (e ? PP_HEADER_CACHE_DEFINED : 0) : e, def == 2 ? PP_HEADER_CACHE_TOUCH_GUARD : 0);
}

VPRIV uint16_t pp_header_cache_counter_provenance_new(pp *p, uint32_t val, uint32_t off, uint32_t len, uint16_t next)
{
	if (p->recording.counter_provenance_count >= PP_HEADER_CACHE_COUNTER_PROVENANCE_MAX || len > 0xffff) {
		PP_HEADER_CACHE_REGION_REJECT(p);
		return 0;
	}
	p->recording.counter_provenance[++p->recording.counter_provenance_count] = (pp_header_cache_counter_provenance){ val, off, (uint16_t)len, next };
	return (uint16_t)p->recording.counter_provenance_count;
}

/* chain a shifted by d, followed by chain tail (shared) */
VPRIV uint16_t pp_header_cache_counter_provenance_chain(pp *p, uint16_t a, uint32_t d, uint16_t tail)
{
	if (!a)
		return tail;
	{
		const pp_header_cache_counter_provenance c = p->recording.counter_provenance[a];
		uint16_t n = pp_header_cache_counter_provenance_chain(p, c.next_run, d, tail);

		return pp_header_cache_counter_provenance_new(p, c.value, c.offset + d, c.length, n);
	}
}

/* a read of an identifier with __COUNTER__ digits: at replay its name differs */
VPRIV __attribute__((noinline)) void pp_header_cache_counter_read_record(pp *p, const ptok *t, uintptr_t e)
{
	const pp_header_cache_counter_provenance *c = &p->recording.counter_provenance[t->ctr];
	char *d;

	if (e || c->next_run || p->recording.counter_read_count >= PP_HEADER_CACHE_COUNTER_READ_MAX || t->len > 4096) {
		PP_HEADER_CACHE_REGION_REJECT(p);	/* such a name defined as a macro, or two runs: give up */
		return;
	}
	d = varena_alloc(&p->permanent_region, t->len + 1);
	memcpy(d, t->s, t->len);	/* pasted: the spelling is clean */
	p->recording.counter_reads[p->recording.counter_read_count++] = (pp_header_cache_counter_read){ d, t->len, c->offset, c->length, c->value };
}

/* #define / #undef / pop_macro */
VPRIV void pp_macro_define_record(pp *p, uint32_t id, const pp_macro *m)
{
	if (unlikely(p->recording.on))
		pp_header_cache_touch_write(p, id);
	pp_macro_table_set(p, id, m);
}

static inline bool pp_memory_is_translation_unit(const pp *p, const char *s)
{
	const uint8_t *u = (const uint8_t *)s;

	return (u >= p->temporary_region.base && u < p->temporary_region.base + p->temporary_region.cap) || (u >= p->permanent_region.base && u < p->permanent_region.base + p->permanent_region.cap);
}

/* a token pp_token_next hands out while recording */
VPRIV void pp_header_cache_token_record(pp *p, const ptok *t)
{
	struct pp_recording *r = &p->recording;
	const pp_header_cache_level *L = &r->levels[r->level_count - 1];
	ptok *d;

	if (t->kind == LEX_TOKEN_EOF)
		return;
	if (unlikely(r->token_count >= PP_HEADER_CACHE_TOKEN_MAX || (t->loc && t->loc < L->location_base_at_start))) {
		PP_HEADER_CACHE_REGION_REJECT(p);
		return;
	}
	d = &r->tokens[r->token_count++];
	*d = *t;
	if (pp_memory_is_translation_unit(p, t->s)) {
		if (r->spelling_bytes + t->len + 16 > PP_HEADER_CACHE_SPELLING_BYTES_MAX) {
			PP_HEADER_CACHE_REGION_REJECT(p);
			return;
		}
		d->s = r->spellings + r->spelling_bytes;
		memcpy(r->spellings + r->spelling_bytes, t->s, t->len);
		memset(r->spellings + r->spelling_bytes + t->len, 0, 16);
		r->spelling_bytes += t->len + 16;
	}
	if (unlikely(t->ctr)) {
		if (t->flags & LEX_TOKEN_FLAG_SPLICED)
			PP_HEADER_CACHE_REGION_REJECT(p);
		for (uint16_t q = t->ctr; q; q = r->counter_provenance[q].next_run) {
			const pp_header_cache_counter_provenance *c = &r->counter_provenance[q];

			if (r->counter_position_count < PP_HEADER_CACHE_COUNTER_PROVENANCE_MAX)
				r->counter_positions[r->counter_position_count++] = (pp_header_cache_counter_position){ r->token_count - 1, (uint32_t)c->offset << 16 | c->length, c->value };
			else
				PP_HEADER_CACHE_REGION_REJECT(p);
		}
	}
}

/* ---------------- entries as trees of child entries ---------------- */

typedef struct pp_header_cache_exclusion_run {
	const uint32_t *ex;
	uint32_t n;
} pp_header_cache_exclusion_run;

static bool pp_header_cache_child_exclusion_has(const pp_header_cache_exclusion_run *xs, int lex_character_next, uint32_t id)
{
	for (int f = 0; f < lex_character_next; f++)
		for (uint32_t i = 0; i < xs[f].n; i++)	/* (short, sorted) */
			if (xs[f].ex[i] >= id) {
				if (xs[f].ex[i] == id)
					return true;
				break;
			}
	return false;
}

/* id was written by the innermost level so far */
static inline bool pp_header_cache_level_wrote(pp *p, uint32_t id)
{
	const uint64_t *st = pp_header_cache_stamp_slot(p, id);

	return (uint32_t)(*st >> 32) == p->recording.levels[p->recording.level_count - 1].serial && (p->recording.touches[(uint32_t)*st].flags & PP_HEADER_CACHE_TOUCH_WRITE);
}

/* e's reads (all of them, as inputs) that the innermost level wrote already: into rec.child_exclusions */
static void pp_header_cache_child_exclusions_collect(pp *p, const pp_header_cache_entry *e, pp_header_cache_exclusion_run *xs, int lex_character_next)
{
	struct pp_recording *r = &p->recording;

#define KX(id) do { uint32_t id_ = (id); if (pp_header_cache_level_wrote(p, id_) && !(lex_character_next && pp_header_cache_child_exclusion_has(xs, lex_character_next, id_))) { \
		if (r->child_exclusion_count < PP_HEADER_CACHE_CHILD_EXCLUSION_MAX) r->child_exclusions[r->child_exclusion_count++] = id_; else PP_HEADER_CACHE_REGION_REJECT(p); } } while (0)
	PP_HEADER_CACHE_VECTOR_FOR(e->guard_reads, pp_header_cache_read, x)
		KX(x->id);
	PP_HEADER_CACHE_VECTOR_FOR(e->undefined_reads, uint32_t, x)
		KX(*x);
	PP_HEADER_CACHE_VECTOR_FOR(e->value_reads, pp_header_cache_read, x)
		KX(x->id);
#undef KX
	for (uint32_t i = 0; i < e->child_count; i++) {
		int m = lex_character_next;

		if (e->children[i].exclusion_count && lex_character_next < PP_HEADER_CACHE_LEVEL_MAX)
			xs[m++] = (pp_header_cache_exclusion_run){ e->children[i].exclusions, e->children[i].exclusion_count };
		pp_header_cache_child_exclusions_collect(p, e->children[i].e, xs, m);
	}
}

/* every id e (with its children) writes: the innermost level's, not its own */
static void pp_header_cache_child_writes_mark(pp *p, const pp_header_cache_entry *e)
{
	for (uint32_t i = 0; i < e->child_count; i++)
		pp_header_cache_child_writes_mark(p, e->children[i].e);
	PP_HEADER_CACHE_VECTOR_FOR(e->writes, pp_header_cache_read, x)
		pp_header_cache_touch_write_child(p, x->id);
}

static int pp_uint32_compare(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

	return x < y ? -1 : x > y;
}

/* e becomes a child entry of the innermost level (replayed or recorded in it) */
VPRIV void pp_header_cache_child_attach(pp *p, const pp_header_cache_entry *e)
{
	struct pp_recording *r = &p->recording;
	pp_header_cache_exclusion_run xs[PP_HEADER_CACHE_LEVEL_MAX + 1];
	uint32_t x0 = r->child_exclusion_count, n, w = 0;

	if (r->child_count >= PP_HEADER_CACHE_CHILD_MAX) {
		PP_HEADER_CACHE_REGION_REJECT(p);
		return;
	}
	pp_header_cache_child_exclusions_collect(p, e, xs, 0);
	n = r->child_exclusion_count - x0;
	if (n > 1) {
		qsort(r->child_exclusions + x0, n, sizeof(uint32_t), pp_uint32_compare);
		for (uint32_t i = 0; i < n; i++)
			if (!w || r->child_exclusions[x0 + i] != r->child_exclusions[x0 + w - 1])
				r->child_exclusions[x0 + w++] = r->child_exclusions[x0 + i];
		n = w;
		r->child_exclusion_count = x0 + n;
	}
	r->children[r->child_count++] = (pp_header_cache_recorded_child){ e, x0, n };
	pp_header_cache_child_writes_mark(p, e);
}
_Thread_local ptok *pp_recording_spare_tokens;
_Thread_local char *pp_recording_spare_spellings;
_Thread_local pp_recording_spare_buffer_set pp_recording_spare_buffers;

/* the region's file was just entered (top of the stack) */
VPRIV void pp_header_cache_region_begin(pp *p, pp_header_cache_key *k)
{
	struct pp_recording *r = &p->recording;
	pp_header_cache_level *L;

	if (!r->touches && pp_recording_spare_buffers.t) {
		/* this thread's last pp's buffers: counts are reset below, contents need no clearing */
		r->touches = pp_recording_spare_buffers.t, r->once_marks = pp_recording_spare_buffers.ord, r->counter_positions = pp_recording_spare_buffers.ctr;
		r->counter_reads = pp_recording_spare_buffers.crd, r->segments = pp_recording_spare_buffers.seg, r->counter_provenance = pp_recording_spare_buffers.cp;
		r->children = pp_recording_spare_buffers.kid, r->child_exclusions = pp_recording_spare_buffers.kex, r->lookups = pp_recording_spare_buffers.lk;
		pp_recording_spare_buffers.t = NULL;
	}
	if (!r->touches) {
		r->touches = pp_address_space_reserve(PP_HEADER_CACHE_TOUCH_MAX * sizeof(pp_header_cache_touch_record));
		r->once_marks = pp_address_space_reserve(PP_HEADER_CACHE_ONCE_MARK_MAX * sizeof(pp_header_cache_once_mark));
		r->counter_positions = pp_address_space_reserve(PP_HEADER_CACHE_COUNTER_PROVENANCE_MAX * sizeof(pp_header_cache_counter_position));
		r->counter_reads = pp_address_space_reserve(PP_HEADER_CACHE_COUNTER_READ_MAX * sizeof(pp_header_cache_counter_read));
		r->segments = pp_address_space_reserve(PP_HEADER_CACHE_SEGMENT_MAX * sizeof(pp_header_cache_recorded_segment));
		r->counter_provenance = pp_address_space_reserve((PP_HEADER_CACHE_COUNTER_PROVENANCE_MAX + 1) * sizeof(pp_header_cache_counter_provenance));
		r->children = pp_address_space_reserve(PP_HEADER_CACHE_CHILD_MAX * sizeof(pp_header_cache_recorded_child));
		r->child_exclusions = pp_address_space_reserve(PP_HEADER_CACHE_CHILD_EXCLUSION_MAX * sizeof(uint32_t));
		r->lookups = pp_address_space_reserve(PP_HEADER_CACHE_LOOKUP_MAX * sizeof(pp_header_cache_lookup));
	}
	if (p->output_mode == 2 && !r->tokens) {
		if (pp_recording_spare_tokens) {
			r->tokens = pp_recording_spare_tokens, r->spellings = pp_recording_spare_spellings;
			pp_recording_spare_tokens = NULL;
		} else {
			r->tokens = pp_address_space_reserve(PP_HEADER_CACHE_TOKEN_MAX * sizeof(ptok));
			r->spellings = pp_address_space_reserve(PP_HEADER_CACHE_SPELLING_BYTES_MAX);
		}
	}
	if (!r->level_count) {
		r->touch_count = r->once_mark_count = r->counter_position_count = r->counter_read_count = r->segment_count = r->counter_provenance_count = 0;
		r->child_count = r->child_exclusion_count = 0;
		r->lookup_count = 0;
		r->token_count = 0;
		r->spelling_bytes = 0;
		r->uncacheable_levels = 0;
	}
	L = &r->levels[r->level_count++];
	r->on = 1;
	L->key = k;
	L->serial = ++r->serial;
	L->depth = p->context_depth;
	L->touches_at_start = r->touch_count;
	L->once_marks_at_start = r->once_mark_count;
	L->counter_positions_at_start = r->counter_position_count;
	L->counter_reads_at_start = r->counter_read_count;
	L->segments_at_start = r->segment_count;
	L->children_at_start = r->child_count;
	L->child_exclusions_at_start = r->child_exclusion_count;
	L->lookups_at_start = r->lookup_count;
	L->files_at_start = p->contexts[p->context_depth - 1].file_index;
	L->location_base_at_start = p->files[L->files_at_start].location_base;
	L->once_files_at_start = p->once_file_count;
	L->line_marks_at_start = p->line_mark_count;
	L->pushed_macros_at_start = p->pushed_macro_count;
	L->errors_at_start = p->error_count;
	L->warnings_at_start = p->warning_count;
	L->counter_at_start = p->counter_value;
	L->text_started = p->output_mode == 2;	/* tokens: positions count from here */
	L->text_at_start = r->token_count;
	L->uses_level = 0;
	L->ndirs = 0;
	L->nfile = p->file_context_count - 1;	/* before the region's file */
	atomic_fetch_add_explicit(&pp_header_cache_statistics.miss, 1, memory_order_relaxed);
}
