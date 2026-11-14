#ifndef VCC_PP_HEADER_CACHE_INTERNAL_H
#define VCC_PP_HEADER_CACHE_INTERNAL_H
/* the header cache's types: shared by the header-cache-*.c files, included by pp-internal.h (struct pp holds some of them) */
/* ---------------- header cache: types ---------------- */

/*
 * The header cache (step 2.4; -E text only for now).  Every #include is a
 * region.  While a region runs for the first time its inputs are recorded
 * exactly:
 *
 *   - every macro table entry it reads before writing it: the value at
 *     region entry, or only whether it was defined (#ifdef, defined(),
 *     include guards).  Definitions are shared pointers (see pmacro), so
 *     "same definition" is pointer equality;
 *   - #pragma once membership of the files it would enter;
 *   - names pasted from __COUNTER__ that it looks up (must be undefined);
 *
 * and its effects: the final value of every macro it wrote, the files it
 * marked once, the files it entered (dependencies, locations), the counter
 * delta, and its output text.  Uses of __COUNTER__ are relative: digit runs
 * are tracked through paste and stringify (ptok.ctr) and rewritten on
 * replay.  A region's text is a rope: its own text with the entries of the
 * nested regions it contains spliced in, so each header's text is stored
 * once however deep it is included.
 *
 * Regions nest; recording keeps one level per open region.  A read is
 * recorded in the innermost level; a level's records are merged into its
 * parent when it ends (and a replayed entry's into the level it is
 * replayed in).  A later TU with the same search path signature reaching
 * the same #include (file, search dir, spelling, nesting level, system
 * header or not) replays an entry whose inputs all match the current state
 * instead of running the header.  Regions that print diagnostics, change
 * #line state, use push/pop_macro, __DATE__/__TIME__/__TIMESTAMP__/
 * __BASE_FILE__ or __COUNTER__ in a directive are not cached.  Entries are
 * immutable and shared by all threads.
 */
#define PP_HEADER_CACHE_DEFINED ((uintptr_t)1)	/* read: only "defined" was read, and it was */
#define PP_HEADER_CACHE_NONE 0xffffffffu
#define PP_HEADER_CACHE_LEVEL_MAX 64

typedef struct pp_header_cache_read {
	uint32_t id, _pad;
	uintptr_t value;		/* value at entry (enabled), or PP_HEADER_CACHE_DEFINED */
} pp_header_cache_read;

typedef struct pp_header_cache_once_mark {
	const lex_file *f;
	uint32_t was_present;
} pp_header_cache_once_mark;

typedef struct pp_header_cache_file {
	const lex_file *f;
	const pp_skeleton *fi;
	const char *name, *spath;
	uint32_t namelen, record_base;
	uint8_t sys, guard_open;
} pp_header_cache_file;

/* __COUNTER__ digits: in a token's spelling (cprov) or in an entry's own text (hctr) */
typedef struct pp_header_cache_counter_provenance {
	uint32_t value;
	uint32_t offset;
	uint16_t length, next_run;	/* next run in the same token (a chain, increasing off) */
} pp_header_cache_counter_provenance;

typedef struct pp_header_cache_counter_run {
	uint32_t off, len, relative;	/* own-text offset; value - counter at entry */
} pp_header_cache_counter_run;	/* tokens: off = own-token index, len = byte offset << 16 | length */

/* a lookup of an identifier pasted from __COUNTER__ (it was undefined) */
typedef struct pp_header_cache_counter_name_read {
	const char *s;
	uint32_t len, off, digit_length, relative;
} pp_header_cache_counter_name_read;

typedef struct pp_header_cache_entry pp_header_cache_entry;

typedef struct pp_header_cache_child {
	const pp_header_cache_entry *e;
	const uint32_t *exclusions;
	uint32_t exclusion_count;
} pp_header_cache_child;

/*
 * A stored token (12 bytes; ptok is 24).  LEX_TOKEN_IDENTIFIER: sp is the id and the
 * spelling the id's.  Others: sp - 1 is the spelling's offset / 16 in the
 * shared region (pp_header_cache_blob_get: stored once).  PP_HEADER_CACHE_TOKEN_ESCAPE: sp indexes the entry's
 * escapes (full ptoks: spliced identifiers, long spellings, odd ids).
 */
typedef struct pp_header_cache_token {
	uint32_t spelling;
	uint32_t loc;		/* offset from the base + 1, 0: none */
	uint8_t kind, flags;
	uint16_t len;
} pp_header_cache_token;
#define PP_HEADER_CACHE_TOKEN_ESCAPE 128

static inline void pp_header_cache_token_load(ptok *t, const pp_header_cache_token *h, const ptok *xtk, const char *sbase, uint32_t base)
{
	if (__builtin_expect(h->flags & PP_HEADER_CACHE_TOKEN_ESCAPE, 0)) {
		*t = xtk[h->spelling];
	} else {
		t->kind = h->kind;
		t->flags = h->flags;
		t->len = h->len;
		t->ctr = 0;
		if (h->kind == LEX_TOKEN_IDENTIFIER) {
			t->id = h->spelling;
			t->s = vintern_ents[h->spelling].s;
		} else {
			t->id = 0;
			t->s = sbase + ((size_t)(h->spelling - 1) << 4);
		}
	}
	t->loc = h->loc ? base + h->loc - 1 : 0;
}

typedef struct pp_header_cache_segment {
	uint32_t at;		/* own-text offset (tokens: own-token index) the child goes before */
	uint32_t counter_delta;	/* child's counter at entry - ours */
	const pp_header_cache_entry *child;
	uint32_t location_base_delta;		/* tokens: child's location base - ours */
} pp_header_cache_segment;

/*
 * Arrays of reads and writes are stored as chunks deduplicated across all
 * entries (an enclosing header's arrays repeat its children's runs).  The
 * cuts are content defined (by id), so the same run of ids cuts the same
 * way wherever it occurs.
 */
typedef struct pp_header_cache_chunk {
	uint64_t h;
	uint32_t n, element_size;	/* elements, element size (1: a blob of n bytes) */
	_Alignas(16) char d[];
} pp_header_cache_chunk;

typedef struct pp_header_cache_vector {
	const pp_header_cache_chunk *const *chunks;
	uint32_t chunk_count, n;		/* chunks, elements */
} pp_header_cache_vector;

/* for (const T *x ...) over the elements of v; break leaves only the inner loop */
#define PP_HEADER_CACHE_VECTOR_FOR(v, T, x) \
	for (uint32_t hvi_ = 0; hvi_ < (v).chunk_count; hvi_++) \
		for (const T *x = (const T *)(const void *)(v).chunks[hvi_]->d, *x##_e = x + (v).chunks[hvi_]->n; x < x##_e; x++)

/*
 * An include lookup an entry made itself that depended on the search
 * directories (used > 0).  The entry's ndirs/psig say the directories it
 * depended on were the same; where they differ, the entry still holds when
 * each such lookup gives the same answer here (pp_header_cache_lookups_match): PostgreSQL
 * puts a per-target directory first, which every lookup passes through.
 */
typedef struct pp_header_cache_lookup {
	const char *name;	/* as written */
	const char *from;	/* the includer's spath (NULL: no includer) */
	const char *path;	/* the answer's spelling (NULL: not found) */
	const lex_file *f;
	int32_t from_search_directory, search_directory;
	uint32_t name_length, from_directory_length;
	uint8_t angled, next, from_builtin, is_system;
} pp_header_cache_lookup;

struct pp_header_cache_entry {
	pp_header_cache_entry *next_variant;
	pp_header_cache_vector guard_reads;		/* inputs: include guards (v: PP_HEADER_CACHE_DEFINED or 0); checked first */
	pp_header_cache_vector undefined_reads;		/* inputs: ids read while undefined (uint32_t) */
	pp_header_cache_vector value_reads;		/* inputs: other reads */
	pp_header_cache_vector writes;		/* delta: (id, new value) */
	const pp_header_cache_once_mark *once_marks;
	const lex_file *const *once_additions;
	const pp_header_cache_file *files;
	const char *own_text;	/* own text */
	const pp_header_cache_token *own_tokens;		/* token entries: own tokens */
	const ptok *escape_tokens;	/* their escapes (PP_HEADER_CACHE_TOKEN_ESCAPE) */
	const pp_header_cache_counter_run *counter_runs;
	const pp_header_cache_counter_name_read *counter_name_reads;
	const pp_header_cache_segment *segments;
	/*
	 * Child entries in order: their reads are this entry's inputs except
	 * the ids this entry wrote before the child (ex, sorted), and their
	 * deltas apply before this entry's own (dl: the ids whose last write was
	 * this entry's own).  Reads and writes are stored once, in the entry
	 * that made them.
	 */
	const struct pp_header_cache_child *children;
	uint32_t child_count;
	const pp_header_cache_lookup *lookups;	/* its own lookups that depended on the search directories */
	uint32_t lookup_count;
	size_t own_length, total_length;	/* own text (tokens); with the children */
	int32_t include_level;		/* __INCLUDE_LEVEL__ used: the include depth it ran at (-1: not used) */
	uint32_t search_directory_count, search_path_signature;	/* leading search directories its lookups depended on; their id */
	const uint32_t *brace_pairs;	/* token entries: outermost '{' '}' pairs of the own tokens with no child
				   inside, (open, close) own-token indices, by open (pp_lazy_body_skip) */
	uint32_t brace_pair_count;
	uint32_t once_mark_count, once_addition_count, file_count, counter_run_count, counter_name_read_count, segment_count, token_span, counter_delta;
	uint8_t has_content, first_at_line_start, end_column, owed_spacing_at_end;
	uint8_t directive_on_line_at_end;
	uint8_t previous_kind, previous_is_wide_prefix, previous_after_period;	/* the printer's last token at the end (pp.pk...) */
};

typedef struct pp_header_cache_key {
	uint64_t hash;
	const lex_file *f;
	const char *spath;
	uint32_t signature, file_depth;
	int32_t search_directory;
	uint8_t is_system;
	_Atomic(pp_header_cache_entry *) head;
	_Atomic uint32_t variant_count;
} pp_header_cache_key;

/* recording, per TU */
enum { PP_HEADER_CACHE_TOUCH_READ = 1, PP_HEADER_CACHE_TOUCH_WRITE = 2, PP_HEADER_CACHE_TOUCH_GUARD = 4 /* the read was an include guard check */,
       PP_HEADER_CACHE_TOUCH_OWN = 8 /* the last write was the level's own (not a child entry's) */ };

typedef struct pp_header_cache_touch_record {	/* a level's first touch of an id */
	uint32_t id, flags;	/* HT_* */
	uintptr_t v;		/* PP_HEADER_CACHE_TOUCH_READ: value at entry or PP_HEADER_CACHE_DEFINED */
	uint64_t previous_stamp;		/* the id's stamp before (the enclosing levels') */
} pp_header_cache_touch_record;

typedef struct pp_header_cache_counter_position {		/* __COUNTER__ digits in the output */
	size_t position;
	uint32_t len, val;
} pp_header_cache_counter_position;

typedef struct pp_header_cache_counter_read {		/* hcread while recording: absolute value */
	const char *s;
	uint32_t len, off, dlen, val;
} pp_header_cache_counter_read;

typedef struct pp_header_cache_recorded_segment {		/* a child entry's text (tokens) in the output */
	size_t start, end;
	const pp_header_cache_entry *child;
	uint32_t counter_at_start;
	uint32_t location_base_at_start;		/* tokens: its location base */
} pp_header_cache_recorded_segment;

typedef struct pp_header_cache_level {
	pp_header_cache_key *key;
	uint32_t serial;
	int depth, errors_at_start, warnings_at_start, text_started;
	uint32_t touches_at_start, once_marks_at_start, counter_positions_at_start, counter_reads_at_start, segments_at_start;	/* starts in the stacks below */
	uint32_t files_at_start, location_base_at_start, once_files_at_start, line_marks_at_start, pushed_macros_at_start;
	unsigned counter_at_start;
	size_t text_at_start;
	uint8_t first_sol, uses_level;
	uint32_t nfile, ndirs;
	uint32_t children_at_start, child_exclusions_at_start;	/* starts in rec.children, rec.child_exclusions */
	uint32_t lookups_at_start;		/* start in rec.lookups */
} pp_header_cache_level;

typedef struct pp_header_cache_recorded_child {		/* a child entry of a level while recording */
	const pp_header_cache_entry *e;
	uint32_t exclusions_start, exclusion_count;	/* its exclusions in rec.child_exclusions */
} pp_header_cache_recorded_child;

#define PP_HEADER_CACHE_TOUCH_MAX (1u << 24)

#define PP_HEADER_CACHE_COUNTER_PROVENANCE_MAX 65535u

#define PP_HEADER_CACHE_COUNTER_READ_MAX 65536u

/* ---------------- header cache: shared table ---------------- */

typedef struct {
	_Atomic unsigned long hit, miss, rec, uncacheable, full, bytes_replayed, bytes_rec, mem;
	_Atomic unsigned long nru, nrv, ndl, nfe, nvalid, nchecked;
	_Atomic unsigned long b_rd, b_dl, b_once, b_fe, b_text, b_seg, b_ent;	/* bytes stored, by kind */
} pp_header_cache_statistics_counters;

/* ---------------- header cache: regions ---------------- */

#define PP_HEADER_CACHE_VARIANT_MAX 64	/* entries per key */
#define PP_HEADER_CACHE_ONCE_MARK_MAX (1u << 20)
#define PP_HEADER_CACHE_SEGMENT_MAX (1u << 20)

#define PP_HEADER_CACHE_TOKEN_MAX (1u << 24)
#define PP_HEADER_CACHE_SPELLING_BYTES_MAX ((size_t)1 << 28)
#define PP_HEADER_CACHE_CHILD_MAX (1u << 20)
#define PP_HEADER_CACHE_CHILD_EXCLUSION_MAX (1u << 22)
#define PP_HEADER_CACHE_LOOKUP_MAX (1u << 16)

typedef struct {
	pp_header_cache_touch_record *t;
	pp_header_cache_once_mark *ord;
	pp_header_cache_counter_position *ctr;
	pp_header_cache_counter_read *crd;
	pp_header_cache_recorded_segment *seg;
	pp_header_cache_recorded_child *kid;
	uint32_t *kex;
	pp_header_cache_lookup *lk;
	pp_header_cache_counter_provenance *cp;
} pp_recording_spare_buffer_set;

#endif
