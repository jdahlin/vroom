/*
 * unit-emit.c: generate code for one translation unit (codegen_unit_emit): the emitter state, the work queue and
 * worker threads, and putting the chunks together.
 *
 * codegen_unit_emit is called by the front end (frontend/c/translation-unit.c) once the AST is complete.  It
 * runs codegen_unit_round_emit up to 8 rounds: a round that finds a function that must be inlined to fold an
 * error() call (need_fold) marks it and the unit is generated again, reusing the functions whose
 * inputs did not change (codegen_keep_make, reuse).  In a round the roots (non-static functions and objects)
 * are queued; workers (or vtask_parallel_for slots) take items, generate each into its own ecx
 * (buffers from pools: nothing is allocated per function), and note_ref queues what they reference.
 * At the end chunks are sorted by key (codegen_chunk_compare) so the output never depends on thread timing, and
 * either written as text or replayed into the object (replay, obj.c).  Diagnostics from workers are
 * serialised with E->emu.  Errors that cannot continue longjmp to the item's e->bail and fail that
 * function only.  Called per TU on the job's thread, which may itself be one of the shared pool's.
 * Environment: VCC_EMIT_REPEAT=N generates N times for profiling.
 */
#include "codegen-internal.h"
#ifdef VCC_WASM
#include "arch/wasm/as/wasmas.h"
#endif
#include "common/buffer.h"
#include "common/scoped-cleanup.h"
#include "port/task.h"

VPRIV void *codegen_memory_reserve(size_t n) { return vreserve(n, MAP_NORESERVE, "vcc: mmap"); }

VPRIV double codegen_clock_seconds(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

const uint8_t codegen_temporary_registers[CODEGEN_TEMPORARY_COUNT] = { 9, 10, 11, 12, 13, 14, 15, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28 };

__attribute__((format(printf, 3, 4)))
VPRIV void codegen_diagnostic_report(codegen_context *e, uint32_t loc, const char *fmt, ...)
{
	diag_report_site_pc = __builtin_return_address(0);
	va_list ap;

	VLOCK(e->unit->diagnostic_mutex);
	va_start(ap, fmt);
	diag_report_va(e->compile_context->p, loc, "note", fmt, ap);
	va_end(ap);
}

__attribute__((format(printf, 3, 4)))
VPRIV void codegen_error_report(codegen_context *e, uint32_t loc, const char *fmt, ...)
{
	diag_report_site_pc = __builtin_return_address(0);
	va_list ap;

	if (e->vir_try) {	/* the VIR walker: the function falls back (codegen/vir/) */
		if (!e->vir_failure_reason)
			e->vir_failure_reason = fmt;
		return;
	}
	{
		VLOCK(e->unit->diagnostic_mutex);
		va_start(ap, fmt);
		diag_report_va(e->compile_context->p, loc, "error", fmt, ap);
		va_end(ap);
	}
	e->unit->errors++;
}

__attribute__((format(printf, 3, 4), noreturn))
VPRIV void codegen_fatal_report(codegen_context *e, uint32_t loc, const char *fmt, ...)
{
	diag_report_site_pc = __builtin_return_address(0);
	va_list ap;

	if (e->vir_try) {	/* the VIR walker: no diagnostic, the function falls back (codegen/vir/) */
		if (!e->vir_failure_reason)
			e->vir_failure_reason = fmt;
		_longjmp(e->bail_jump, 1);
	}
	{	/* (the lock is dropped before the longjmp: cleanups do not run on it) */
		VLOCK(e->unit->diagnostic_mutex);
		va_start(ap, fmt);
		diag_report_va(e->compile_context->p, loc, "error", fmt, ap);
		va_end(ap);
	}
	e->unit->errors++;
	_longjmp(e->bail_jump, 1);
}

/* ---------------- the work queue and threads ---------------- */

VPRIV void codegen_work_enqueue(codegen_unit *E, uint32_t item)
{
	VLOCK(E->queue_mutex);
	E->queue[E->queue_tail++] = item;
	pthread_cond_signal(&E->queue_condition);
}

/* the regions of a ctx[i] (codegen_context_init takes a set, ecx_free gives it back) */
/* a quoted assembler string */
static void codegen_string_quote_append(tbuf *b, const char *s)
{
	vtext_char_append(b, '"');
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			vtext_char_append(b, '\\');
		vtext_char_append(b, *s);
	}
	vtext_char_append(b, '"');
}

static vjob_region_pool codegen_context_pool = VJOB_REGION_POOL_INIT(16);

static void codegen_context_init(codegen_context *e, codegen_unit *E, vjob_region *r)
{
	cc *c = E->compile_context;

	memset(e, 0, sizeof(*e));
	e->unit = E;
	e->compile_context = c;
	e->record_capacity = 1u << 24;
	e->records = vjob_region_acquire(&r[0], (size_t)e->record_capacity * sizeof(ins), "emit.in");
	vtext_region_init(&e->raw, &r[1], (size_t)1 << 28, "emit.raw");
	vtext_region_init(&e->out, &r[2], (size_t)1 << 32, "emit.out");
	vtext_region_init(&e->data, &r[3], (size_t)1 << 28, "emit.data");
	e->relocations = vjob_region_acquire(&r[4], (size_t)1 << 26, "emit.rel");
	e->bindings = vjob_region_acquire(&r[5], (size_t)c->nsym * sizeof(codegen_binding) + 4096, "emit.b");
	e->label_map = vjob_region_acquire(&r[6], (size_t)c->nlabel * sizeof(codegen_label) + 4096, "emit.lm");
	e->case_labels = vjob_region_acquire(&r[7], (size_t)c->nnode * sizeof(uint32_t) + 4096, "emit.caselbl");
	e->label_capacity = 1u << 22;
	e->label_reference_counts = vjob_region_acquire(&r[8], (size_t)e->label_capacity * sizeof(uint32_t), "emit.lrefc");
	e->label_known_values_index = vjob_region_acquire(&r[9], (size_t)e->label_capacity * sizeof(uint32_t), "emit.lkvi");
	e->known_value_states = vjob_region_acquire(&r[10], (size_t)CODEGEN_KNOWN_VALUES_CAPACITY * sizeof(codegen_known_values), "emit.kvs");
	e->cases = vjob_region_acquire(&r[11], (size_t)1 << 24, "emit.cases");
	e->weak_references = vjob_region_acquire(&r[12], (size_t)c->nsym * sizeof(uint32_t) + 4096, "emit.weakref");
	e->round_log = vjob_region_acquire(&r[13], CODEGEN_LOG_CAPACITY * sizeof(uint32_t), "emit.rlog");
	e->slot_allocations = vjob_region_acquire(&r[14], (size_t)CODEGEN_SLOT_ALLOCATION_CAPACITY * (sizeof(codegen_slot_allocation) + sizeof(uint32_t)), "emit.sla");
	e->live_slots = (uint32_t *)(e->slot_allocations + CODEGEN_SLOT_ALLOCATION_CAPACITY);
	/* nesting: reserved, touched only as deep as it goes (bounded by the parser's stack guard) */
	e->loops = vjob_region_acquire(&r[15], (size_t)CODEGEN_NESTING_CAPACITY * (sizeof(codegen_loop) + sizeof(uint32_t)), "emit.loops");
	e->cleanups = (uint32_t *)(e->loops + CODEGEN_NESTING_CAPACITY);
}

/* (after the stitch: the chunks point into e->out) */
static void ecx_free(codegen_context *e, vjob_region *r)
{
	em_switch_values_reset(e);
	cc *c = e->compile_context;

	/* written before read (put, newlabel, the case collection, ...): handed back as is */
	vjob_region_release(&r[0], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[1], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[2], e->out.n);
	vjob_region_release(&r[3], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[4], VJOB_REGION_SCRATCH);
	/* per sym / label, found zero */
	vjob_region_release(&r[5], (size_t)c->nsym * sizeof(codegen_binding) + 4096);
	vjob_region_release(&r[6], (size_t)c->nlabel * sizeof(codegen_label) + 4096);
	vjob_region_release(&r[7], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[8], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[9], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[10], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[11], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[12], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[13], VJOB_REGION_SCRATCH);	/* (copied by codegen_keep_make before this) */
	vjob_region_release(&r[14], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[15], VJOB_REGION_SCRATCH);
	vjob_region_pool_give(&codegen_context_pool, r);
#ifdef VCC_WASM
	wasm_ecx_free(e);
#endif
	walk_context_free(e);
	virbuild_walk_context_free(e);
}

/* a function the previous round generated, if nothing it asked about has been marked since */
static bool codegen_round_chunk_reuse(codegen_context *e, uint32_t s)
{
	const codegen_keep *K = e->unit->previous_keep;
	const codegen_keep_entry *k;
	tbuf *b = &e->out;
	size_t start;

	if (!K || s >= K->index_count || !K->indexes[s])
		return false;
	k = &K->entries[K->indexes[s] - 1];
	for (uint32_t j = 0; j < k->log_count; j++)
		if (k->log[2 * j] == CODEGEN_LOG_DEPENDENCY && e->unit->inline_everywhere[k->log[2 * j + 1]])
			return false;
	for (uint32_t j = 0; j < k->log_count; j++) {
		uint32_t v = k->log[2 * j + 1];

		switch (k->log[2 * j]) {
		case CODEGEN_LOG_REFERENCE: codegen_reference_claim(e, v); break;
		case CODEGEN_LOG_DEPENDENCY: codegen_log_append(e, CODEGEN_LOG_DEPENDENCY, v); break;
		case CODEGEN_LOG_WALK: codegen_function_prepass_request(e, v); break;
		}
	}
	b->n = (b->n + 7) & ~(size_t)7;
	start = b->n;
	memcpy(codegen_item_room_need(e, k->length), k->text, k->length);
	codegen_chunk_add(e, k->key, start);
	return true;
}

static void codegen_work_item_run(codegen_context *e, uint32_t it)
{
	cc *c = e->compile_context;

	e->round_log_item_start = e->round_log_count;
	if (it & 0x80000000u) {
		codegen_string_literal_emit(e, it & 0x7fffffffu);
		return;
	}
	if (WASM(c) && c->syms[it].kind == AST_SYMBOL_FUNCTION) {
		wasm_emit_function(e, it);
	} else if (c->syms[it].kind == AST_SYMBOL_FUNCTION && CODEGEN_IS_X64(c)) {
		if (!virbuild_function_try(e, it))
			walk_function_emit(e, it);
	} else if (c->syms[it].kind == AST_SYMBOL_FUNCTION) {
		if (!codegen_round_chunk_reuse(e, it) && !virbuild_function_try(e, it))
			emit_function(e, it);
	} else {
		codegen_data_object_emit(e, it);
	}
}

static void *codegen_worker_run(void *arg)
{
	codegen_context *e = arg;
	codegen_unit *E = e->unit;

	pthread_mutex_lock(&E->queue_mutex);
	for (;;) {
		uint32_t it;

		while (E->queue_head == E->queue_tail && E->busy_count > 0)
			pthread_cond_wait(&E->queue_condition, &E->queue_mutex);
		if (E->queue_head == E->queue_tail)
			break;
		it = E->queue[E->queue_head++];
		E->busy_count++;
		pthread_mutex_unlock(&E->queue_mutex);
		codegen_work_item_run(e, it);
		pthread_mutex_lock(&E->queue_mutex);
		E->busy_count--;
		if (E->busy_count == 0 && E->queue_head == E->queue_tail)
			pthread_cond_broadcast(&E->queue_condition);
	}
	pthread_cond_broadcast(&E->queue_condition);
	pthread_mutex_unlock(&E->queue_mutex);
	return NULL;
}

/* one slot of the drain loop (vtask_parallel_for item) */
static void codegen_drain_slot_run(void *arg, size_t i)
{
	codegen_worker_run(&((codegen_context *)arg)[i]);
}

static int codegen_chunk_compare(const void *a, const void *b)
{
	const codegen_chunk *x = a, *y = b;

	return x->key < y->key ? -1 : x->key > y->key;
}

static bool codegen_symbol_is_root(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];
	const attrs *a = codegen_symbol_attributes(c, s);

	if (y->depth != 0 && !(y->kind == AST_SYMBOL_VARIABLE && y->sc == AST_STORAGE_STATIC && a->used))
		return false;
	if (a->alias)
		return false;	/* .set in the tail; its target is a root (codegen_unit_emit) */
	if (y->kind == AST_SYMBOL_FUNCTION) {
		if (!(y->flags & AST_SYMBOL_FLAG_DEFINED) || !y->body)
			return false;
		if ((y->flags & AST_SYMBOL_FLAG_GNU_INLINE) && !(y->flags & AST_SYMBOL_FLAG_INLINE_WITHOUT_EXTERN))
			return false;	/* GNU 'extern inline': clang's isInlineDefinitionExternallyVisible */
		if (y->sc == AST_STORAGE_STATIC)
			return a->used || a->constructor || a->destructor;
		if ((y->flags & AST_SYMBOL_FLAG_INLINE) && !(y->flags & (AST_SYMBOL_FLAG_GNU_INLINE | AST_SYMBOL_FLAG_EXTERNAL_DECLARATION)) && y->sc != AST_STORAGE_EXTERN)
			return false;	/* C99 inline definition */
		return true;
	}
	if (y->kind == AST_SYMBOL_VARIABLE) {
		if (!(y->flags & (AST_SYMBOL_FLAG_DEFINED | AST_SYMBOL_FLAG_TENTATIVE)))
			return false;
		if (!y->name || y->sc == AST_STORAGE_REGISTER)
			return false;	/* global register variables have no storage */
		if (y->sc == AST_STORAGE_STATIC)
			return a->used;
		return true;
	}
	return false;
}

__attribute__((format(printf, 2, 3)))
static void codegen_assembler_error_report(cc *c, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	diag_report_va(c->p, 0, "error", fmt, ap);
	va_end(ap);
}

/* the object's machine and format: x86-64 COFF for Windows (PLAN-win64.md WIN5), else ELF */
static void codegen_object_target_set(cc *c, ElfObj *obj)
{
	elf_set_machine(obj, CODEGEN_IS_X64(c) ? EM_X86_64 : EM_AARCH64);
	obj_set_format(obj, CODEGEN_IS_WIN64(c) ? OBJ_COFF : OBJ_ELF);
}

/* the object backend: header text, chunks (items), tail text */
static vjob_region_pool codegen_commit_pool = VJOB_REGION_POOL_INIT(3);

static int codegen_chunks_commit(cc *c, const cc_options *o, const char *hp, size_t hn, const char *tp, size_t tn, const codegen_chunk *ch, uint32_t nch)
{
	/* per function, written before read: reserved once per thread */
	static _Thread_local uint32_t *wb, *lsym, *lser, ser;
	static _Thread_local codegen_commit_label *lab;
	static _Thread_local codegen_commit_pending_branch *pend;
	static _Thread_local codegen_commit_patch *pt;
	size_t need = (size_t)c->nsym + c->nstr + c->nlabel + 192;
	codegen_commit_context R = { .compile_context = c, .assembler = o->as, .object = o->obj };
	vjob_region *rr = vjob_region_pool_take(&codegen_commit_pool);
	uint32_t *cache;
	int r = 0;

	R.label_word_offsets = vjob_region_acquire(&rr[0], (size_t)1 << 32, "emit.loff");	/* labels of one function */
	vtext_region_init(&R.name_buffer, &rr[1], 1 << 20, "emit.nb");
	cache = vjob_region_acquire(&rr[2], need * sizeof(uint32_t), "emit.rcache");
	R.symbol_cache = cache;
	R.string_cache = cache + c->nsym + 64;
	R.label_cache = R.string_cache + c->nstr + 64;
	if (!wb) {
		wb = codegen_memory_reserve((size_t)1 << 32);		/* words of one function (<= 6 per record) */
		lsym = codegen_memory_reserve((size_t)1 << 32);
		lser = codegen_memory_reserve((size_t)1 << 32);
		lab = codegen_memory_reserve((size_t)1 << 34);		/* per label */
		pend = codegen_memory_reserve((size_t)1 << 34);	/* per branch record */
		pt = codegen_memory_reserve((size_t)1 << 34);		/* fixups of one function (<= 1 per record) */
	}
	R.word_buffer = wb;
	R.label_symbols = lsym;
	R.label_serials = lser;
	R.labels = lab;
	R.pending = pend;
	R.serial = ser;
	R.patches = pt;
	codegen_object_target_set(c, R.object);
	as_begin(R.assembler, R.object, "<vcc>");
	if (as_snippet_assemble(R.assembler, hp, hn))
		r = -1;
	for (uint32_t i = 0; !r && i < nch; i++)
		r = codegen_commit_chunk(&R, ch[i].text, ch[i].length);
	ser = R.serial;
	if (!r && (as_snippet_assemble(R.assembler, tp, tn) || as_end(R.assembler)))
		r = -1;
	vjob_region_release(&rr[0], VJOB_REGION_SCRATCH);
	vjob_region_release(&rr[1], VJOB_REGION_SCRATCH);
	vjob_region_release(&rr[2], need * sizeof(uint32_t));
	vjob_region_pool_give(&codegen_commit_pool, rr);
	if (r || R.object->has_err || as_is_failed(R.assembler)) {
		codegen_assembler_error_report(c, "assembler: %s", as_is_failed(R.assembler) ? as_message_get(R.assembler) : R.object->err);
		return -1;
	}
	return 0;
}

/* what the next round may reuse: this round's functions (their chunks and logs, copied) */
static void codegen_keep_free(codegen_keep *K)
{
	if (K->indexes) {
		munmap(K->indexes, K->index_count * sizeof(uint32_t));
		munmap(K->entries, K->entry_count * sizeof(codegen_keep_entry) + 64);
		munmap(K->bytes, K->byte_count + 64);
		munmap(K->logs, K->log_count * sizeof(uint32_t) + 64);
	}
	memset(K, 0, sizeof(*K));
}

static void codegen_keep_make(codegen_keep *K, cc *c, const codegen_unit *E)
{
	size_t nb = 0, nl = 0, ob = 0, ol = 0;
	uint32_t ne = 0;

	memset(K, 0, sizeof(*K));
	for (uint32_t i = 0; i < E->chunk_count; i++) {
		const codegen_chunk *ch = &E->chunks[i];

		if (ch->key >> 62 || (ch->key & 1))
			continue;	/* strings, objects: generated again */
		nb += (ch->length + 7) & ~(size_t)7;
		nl += 2 * (size_t)ch->log_count;
		ne++;
	}
	K->index_count = c->nsym;
	K->indexes = codegen_memory_reserve(K->index_count * sizeof(uint32_t));
	K->entry_count = ne;
	K->entries = codegen_memory_reserve(ne * sizeof(codegen_keep_entry) + 64);
	K->byte_count = nb;
	K->bytes = codegen_memory_reserve(nb + 64);
	K->log_count = nl;
	K->logs = codegen_memory_reserve(nl * sizeof(uint32_t) + 64);
	ne = 0;
	for (uint32_t i = 0; i < E->chunk_count; i++) {
		const codegen_chunk *ch = &E->chunks[i];
		codegen_keep_entry *k;

		if (ch->key >> 62 || (ch->key & 1))
			continue;
		k = &K->entries[ne++];
		k->key = ch->key;
		k->text = K->bytes + ob;
		k->length = ch->length;
		memcpy(K->bytes + ob, ch->text, ch->length);
		ob += (ch->length + 7) & ~(size_t)7;
		k->log = K->logs + ol;
		k->log_count = ch->log_count;
		memcpy(K->logs + ol, ch->log, 2 * (size_t)ch->log_count * sizeof(uint32_t));
		ol += 2 * (size_t)ch->log_count;
		K->indexes[ch->key >> 1] = ne;
	}
}

/* codegen_unit_round_emit's per-TU tables (E.*) and text buffers */
static vjob_region_pool codegen_round_pool = VJOB_REGION_POOL_INIT(18);

/*
 * Windows x64: the .refptr.NAME pointers the code read (sym_win64_indirect), in symbol order, each
 * in its own discard COMDAT as clang writes them (every TU that needs one has its copy; the linker
 * keeps one); the MinGW runtime's pseudo-relocations fix one up when NAME lives in a DLL.
 */
static void win64_tail(cc *c, tbuf *b, _Atomic uint8_t *refptr)
{
	for (uint32_t s = 0; refptr && s < c->nsym; s++)
		if (atomic_load_explicit(&refptr[s], memory_order_relaxed)) {
			vtext_string_append(b, "\t.section\t.rdata$.refptr.");
			codegen_symbol_name_append(c, b, s);
			vtext_string_append(b, ",\"dr\",discard,.refptr.");
			codegen_symbol_name_append(c, b, s);
			vtext_string_append(b, "\n\t.p2align\t3\n\t.globl\t.refptr.");
			codegen_symbol_name_append(c, b, s);
			vtext_string_append(b, "\n.refptr.");
			codegen_symbol_name_append(c, b, s);
			vtext_string_append(b, ":\n\t.quad\t");
			codegen_symbol_name_append(c, b, s);
			vtext_char_append(b, '\n');
		}
}

static int codegen_unit_round_emit(cc *c, const cc_options *o, bool write_out, uint8_t *mf, bool *again, bool last,
		    const codegen_keep *K, codegen_keep *next)
{
	codegen_unit E;
	int nthr = o->nthreads > 0 ? o->nthreads : 1;
	codegen_context *ctx;
	double t0 = codegen_clock_seconds(), t1, t2;
	tbuf hdr;
	int errors;
	vjob_region *er = vjob_region_pool_take(&codegen_round_pool), *xr[64];

	memset(&E, 0, sizeof(E));
	E.compile_context = c;
	E.options = o;
	E.inline_everywhere = mf;
	E.previous_keep = K;
	E.claimed = vjob_region_acquire(&er[0], c->nsym + 64, "emit.claimed");
	E.symbol_referenced = vjob_region_acquire(&er[1], c->nsym + 64, "emit.extref");
	E.refptr = CODEGEN_IS_WIN64(c) ? calloc(c->nsym + 64, 1) : NULL;	/* (Windows only: the .refptr. stubs) */
	E.symbol_flags = vjob_region_acquire(&er[2], c->nsym + 64, "emit.sflags");
	E.float_facts = vjob_region_acquire(&er[14], c->nsym + 64, "emit.ffacts");
	E.strings_done = vjob_region_acquire(&er[3], c->nstr + 64, "emit.strdone");
	E.node_flags = vjob_region_acquire(&er[4], c->nnode + 64, "emit.nflags");
	E.label_flags = vjob_region_acquire(&er[5], c->nlabel + 64, "emit.lref");
	E.label_cleanup_depth = vjob_region_acquire(&er[6], (size_t)c->nlabel * 2 + 64, "emit.lcdepth");
	E.queue = vjob_region_acquire(&er[7], ((size_t)c->nsym + c->nstr + 64) * sizeof(uint32_t), "emit.q");
	E.chunks = vjob_region_acquire(&er[8], ((size_t)c->nsym + c->nstr + c->ntlasm + 64) * sizeof(codegen_chunk), "emit.chunks");
	pthread_mutex_init(&E.queue_mutex, NULL);
	pthread_cond_init(&E.queue_condition, NULL);
	pthread_mutex_init(&E.chunk_mutex, NULL);
	pthread_mutex_init(&E.diagnostic_mutex, NULL);
	pthread_mutex_init(&E.debug_mutex, NULL);
	E.walked = vjob_region_acquire(&er[9], c->nsym + 64, "emit.walked");
	E.function_node_counts = vjob_region_acquire(&er[10], (c->nsym + 64) * sizeof(uint16_t), "emit.fnsize");
	E.constant_written_values = vjob_region_acquire(&er[11], (c->nsym + 64) * sizeof(i128), "emit.cwval");
	E.local_use_weights = vjob_region_acquire(&er[15], (c->nsym + 64) * sizeof(uint16_t), "emit.luse");
	E.local_needs_memory = vjob_region_acquire(&er[16], c->nsym + 64, "emit.lnoreg");
	E.function_local_span = vjob_region_acquire(&er[17], (c->nsym + 64) * 2 * sizeof(uint32_t), "emit.lspan");
	{
		const char *k = getenv("VCC_LREGS");

		E.local_register_count = k ? atoi(k) : CODEGEN_LOCAL_REGISTERS_DEFAULT;
		if (E.local_register_count < 0 || E.local_register_count > CODEGEN_LOCAL_REGISTER_MAX)
			E.local_register_count = CODEGEN_LOCAL_REGISTER_MAX;
	}
	E.stats = getenv("VCC_EMIT_STATS") != NULL;
	E.vir_statistics = getenv("VCC_VIR_STATS") != NULL;
	E.record_statistics = getenv("VCC_REC_STATS") != NULL;
	E.cleanups_disabled = getenv("VCC_NOOPT") != NULL;
	/* wasm64: objects from the text, through arch/wasm/as */
	E.object_text = getenv("VCC_OBJ_TEXT") != NULL || WASM(c);
	/* -g: the line table is the assembler's, from the printer's .loc */
	if (o->debug && !CODEGEN_IS_DARWIN(c) && !WASM(c) && !CODEGEN_IS_X64(c))
		E.object_text = true;
	/* unwind tables: .eh_frame is the assembler's, from the printer's .cfi_* */
	if (o->cfi && !CODEGEN_IS_DARWIN(c) && !WASM(c) && !CODEGEN_IS_X64(c))
		E.object_text = true;
	/* Lazy walks own only their function's automatic variables. Globals and
	   static objects can be modified by other functions (including ones not
	   emitted here); publish conservative immutable facts before workers start.
	   In particular, never infer a TU-wide constant from one function's writes. */
	for (uint32_t s = 1; s < c->nsym; s++)
		if (c->syms[s].kind == AST_SYMBOL_VARIABLE && !codegen_symbol_is_local_variable(c, s)) {
			E.symbol_flags[s] = CODEGEN_SYMBOL_WRITTEN | CODEGEN_SYMBOL_ARRAY_ESCAPES;
			E.float_facts[s] = CODEGEN_FLOAT_FACT_BAD;
		}
	/* VIR: the functions called once, which its walker inlines whatever their size */
	if (!WASM(c) && virbuild_is_enabled(o))
		E.once = codegen_function_once_flags(c);
	if (E.once)
		codegen_function_inline_sizes(&E, c, WALK_INLINE_VIR_MAX);
	/* roots */
	for (uint32_t i = 0; i < c->ntoplevel; i++) {
		uint32_t s = c->toplevel[i];

		if (codegen_symbol_is_root(c, s) && !E.claimed[s]) {
			E.claimed[s] = 1;
			E.queue[E.queue_tail++] = s;
		}
		if (unlikely(codegen_symbol_attributes(c, s)->alias)) {
			/* an alias keeps its target, static or not */
			uint32_t t = cc_symbol_global_find(c, c->strs[codegen_symbol_attributes(c, s)->alias - 1].s);

			if (t && !E.claimed[t] && (c->syms[t].flags & (AST_SYMBOL_FLAG_DEFINED | AST_SYMBOL_FLAG_TENTATIVE)) &&
			    (c->syms[t].kind == AST_SYMBOL_FUNCTION ? c->syms[t].body != 0 : true)) {
				E.claimed[t] = 1;
				E.queue[E.queue_tail++] = t;
			}
		}
	}
	t1 = codegen_clock_seconds();
	if (nthr > 64)
		nthr = 64;
	ctx = calloc((size_t)nthr, sizeof(codegen_context));
	for (int i = 0; i < nthr; i++)
		codegen_context_init(&ctx[i], &E, xr[i] = vjob_region_pool_take(&codegen_context_pool));
	/* the drain loop on nthr slots, slot i with ctx[i], on the shared pool
	 * (port/task.c): helpers are idle pool threads only, under the process'
	 * budget, so inside a full vpp --batch or vccd the slots run here one after
	 * the other (a slot that starts late finds the queue empty and returns).
	 * Pool threads have one stack size (as main's, or vccd's worker stack), so
	 * a function nests as deep, and compiles the same, on any of them; their
	 * thread-local scratch (optimizer, VIR) lives as long as they do instead of
	 * being leaked by a thread per round. */
	vtask_parallel_for((size_t)nthr, nthr, codegen_drain_slot_run, ctx, 0);
	t2 = codegen_clock_seconds();
	free(E.once);free(E.inline_parameter_reads);
	E.once = E.inline_parameter_reads = NULL;
	free(E.call_edges), free(E.call_edge_offsets), free(E.call_edge_counts), free(E.recursion_groups), free(E.inline_sizes);
	E.call_edges = NULL; E.call_edge_offsets = E.call_edge_counts = E.recursion_groups = NULL, E.inline_sizes = NULL;
	/* stitch */
	qsort(E.chunks, E.chunk_count, sizeof(codegen_chunk), codegen_chunk_compare);
	vtext_region_init(&hdr, &er[12], (size_t)1 << 26, "emit.hdr");
	if (WASM(c))
		wasm_header(c, &hdr, ctx, nthr);
	else if (CODEGEN_IS_X64(c) && (o->xcf & 1))
		/* -fcf-protection=branch: GNU_PROPERTY_X86_FEATURE_1_AND = IBT, as clang's start of file */
		vtext_string_append(&hdr, "\t.section\t.note.gnu.property,\"a\",@note\n\t.p2align\t3, 0x0\n\t.long\t4\n\t.long\t16\n"
			   "\t.long\t5\n\t.asciz\t\"GNU\"\n\t.long\t3221225474\n\t.long\t4\n\t.long\t1\n\t.p2align\t3, 0x0\n\t.text\n");
	else
		vtext_string_append(&hdr, "\t.text\n");
	bool dbg = o->debug && !CODEGEN_IS_DARWIN(c) && !WASM(c);
	if (dbg) {
		vtext_string_append(&hdr, ".Ldbg_text0:\n\t.file\t0 ");
		codegen_string_quote_append(&hdr, o->dbg_dir ? o->dbg_dir : ".");
		vtext_char_append(&hdr, ' ');
		codegen_string_quote_append(&hdr, o->dbg_name ? o->dbg_name : "<stdin>");
		vtext_char_append(&hdr, '\n');
		for (uint32_t i = 0; i < E.debug_file_count; i++) {
			vtext_format_append(&hdr, "\t.file\t%u ", i + 1);
			codegen_string_quote_append(&hdr, E.debug_files[i]);
			vtext_char_append(&hdr, '\n');
			free(E.debug_files[i]);
		}
		free(E.debug_files);
	}
	for (uint32_t i = 0; i < c->ntlasm; i++) {
		const ast_asm_statement *a = &c->asms[N(c->tlasm[i])->a];

		vtext_string_append(&hdr, c->strs[a->tmpl - 1].s);
		vtext_string_append(&hdr, WASM(c) ? "\n" : "\n\t.text\n");
	}
	{
		tbuf tail;
		struct iovec *iov = malloc(((size_t)E.chunk_count + 2) * sizeof(struct iovec));
		int k = 0;

		vtext_region_init(&tail, &er[13], (size_t)1 << 26, "emit.tail");
		{
			/* .weak for weak undefined syms, in symbol order whatever the threads did */
			uint32_t nw = 0, *w;

			for (int t = 0; t < nthr; t++)
				nw += ctx[t].weak_reference_count;
			w = malloc((nw + 1) * sizeof(uint32_t));
			nw = 0;
			for (int t = 0; t < nthr; t++)
				for (uint32_t j = 0; j < ctx[t].weak_reference_count; j++)
					w[nw++] = ctx[t].weak_references[j];
			qsort(w, nw, sizeof(uint32_t), codegen_u32_compare);
			for (uint32_t j = 0; j < nw; j++)
				if (!E.claimed[w[j]] && c->syms[w[j]].name && !codegen_symbol_attributes(c, w[j])->alias) {
					vtext_string_append(&tail, "\t.weak\t");
					codegen_symbol_name_append(c, &tail, w[j]);
					vtext_char_append(&tail, '\n');
				}
			free(w);
		}
		for (uint32_t ti = 0; ti < c->ntoplevel; ti++) {
			uint32_t s = c->toplevel[ti];
			const sym *y = &c->syms[s];

			/* aliases */
			if (y->depth == 0 && codegen_symbol_attributes(c, s)->alias && y->name) {
				const attrs *a = codegen_symbol_attributes(c, s);

				/* Darwin: clang's (#pragma weak) aliases are local, .weak_reference on a definition */
				/* clang: an ifunc is global even when static */
				vtext_string_append(&tail, CODEGEN_IS_DARWIN(c) ? "" : (y->flags & AST_SYMBOL_FLAG_WEAK) ? "\t.weak\t" : y->sc == AST_STORAGE_STATIC && !a->ifunc ? "" : "\t.globl\t");
				if (!CODEGEN_IS_DARWIN(c) && (y->sc != AST_STORAGE_STATIC || (y->flags & AST_SYMBOL_FLAG_WEAK) || a->ifunc)) {
					codegen_symbol_name_append(c, &tail, s);
					vtext_char_append(&tail, '\n');
				}
				if (!CODEGEN_IS_WIN64(c)) {	/* (COFF: no .type) */
					vtext_string_append(&tail, "\t.type\t");
					codegen_symbol_name_append(c, &tail, s);
					vtext_string_append(&tail, a->ifunc ? ",@gnu_indirect_function\n" : y->kind == AST_SYMBOL_FUNCTION ? ",@function\n" : ",@object\n");
				}
				vtext_string_append(&tail, "\t.set\t");
				codegen_symbol_name_append(c, &tail, s);
				vtext_string_append(&tail, CODEGEN_IS_DARWIN(c) ? ", _" : ", ");	/* (an assembler name: Mach-O's prefix) */
				vtext_string_append(&tail, c->strs[a->alias - 1].s);
				vtext_char_append(&tail, '\n');
			}
		}
		if (dbg) {
			/* one compile unit over .text: producer, C11, name, its line table, dir, pc range */
			/* (x86: .word is 2 bytes there, so .short/.long/.quad) */
			bool x = CODEGEN_IS_X64(c), w = CODEGEN_IS_WIN64(c);	/* (COFF: "dr" sections, section-relative offsets) */

			vtext_string_append(&tail, w ? "\t.text\n.Ldbg_text1:\n"
				    "\t.section\t.debug_abbrev,\"dr\"\n.Ldbg_abbrev:\n"
				    "\t.byte\t1, 17, 0, 37, 8, 19, 5, 3, 8, 16, 23, 27, 8, 17, 1, 18, 6, 0, 0, 0\n"
				    "\t.section\t.debug_info,\"dr\"\n" : "\t.text\n.Ldbg_text1:\n"
				    "\t.section\t.debug_abbrev,\"\",@progbits\n.Ldbg_abbrev:\n"
				    "\t.byte\t1, 17, 0, 37, 8, 19, 5, 3, 8, 16, 23, 27, 8, 17, 1, 18, 6, 0, 0, 0\n"
				    "\t.section\t.debug_info,\"\",@progbits\n");
			vtext_string_append(&tail, w ? "\t.long\t.Ldbg_cu_end-.Ldbg_cu_start\n.Ldbg_cu_start:\n"
				        "\t.short\t5\n\t.byte\t1, 8\n\t.secrel32\t.Ldbg_abbrev\n\t.byte\t1\n\t.asciz\t\"vcc\"\n"
				        "\t.short\t29\n\t.asciz\t" : x ? "\t.long\t.Ldbg_cu_end-.Ldbg_cu_start\n.Ldbg_cu_start:\n"
				        "\t.short\t5\n\t.byte\t1, 8\n\t.long\t.Ldbg_abbrev\n\t.byte\t1\n\t.asciz\t\"vcc\"\n"
				        "\t.short\t29\n\t.asciz\t" :
				        "\t.word\t.Ldbg_cu_end-.Ldbg_cu_start\n.Ldbg_cu_start:\n"
				        "\t.hword\t5\n\t.byte\t1, 8\n\t.word\t.Ldbg_abbrev\n\t.byte\t1\n\t.asciz\t\"vcc\"\n"
				        "\t.hword\t29\n\t.asciz\t");
			codegen_string_quote_append(&tail, o->dbg_name ? o->dbg_name : "<stdin>");
			vtext_string_append(&tail, w ? "\n\t.secrel32\t.Lline_table_start0\n\t.asciz\t" :
				    x ? "\n\t.long\t.Lline_table_start0\n\t.asciz\t" : "\n\t.word\t.Lline_table_start0\n\t.asciz\t");
			codegen_string_quote_append(&tail, o->dbg_dir ? o->dbg_dir : ".");
			vtext_string_append(&tail, x ? "\n\t.quad\t.Ldbg_text0\n\t.long\t.Ldbg_text1-.Ldbg_text0\n.Ldbg_cu_end:\n" :
				        "\n\t.xword\t.Ldbg_text0\n\t.word\t.Ldbg_text1-.Ldbg_text0\n.Ldbg_cu_end:\n");
			vtext_string_append(&tail, w ? "\t.section\t.debug_line,\"dr\"\n.Lline_table_start0:\n" :
				    "\t.section\t.debug_line,\"\",@progbits\n.Lline_table_start0:\n");
		}
		if (WASM(c))
			wasm_tail(c, &tail, E.claimed);
		else if (CODEGEN_IS_WIN64(c))
			win64_tail(c, &tail, E.refptr);
		else
			vtext_string_append(&tail, "\t.section\t\".note.GNU-stack\",\"\",@progbits\n");
		iov[k++] = (struct iovec){ hdr.p, hdr.n };
		for (uint32_t i = 0; i < E.chunk_count; i++)
			iov[k++] = (struct iovec){ (void *)E.chunks[i].text, E.chunks[i].length };
		iov[k++] = (struct iovec){ tail.p, tail.n };
		*again = E.newly_marked_count != 0 && !last;
		if (*again && next && !E.errors)
			codegen_keep_make(next, c, &E);
		if (!E.errors && write_out && !*again && o->emit == 2) {
			if (E.object_text) {
				/* the text, gathered once, through the assembler */
				static _Thread_local tbuf all;
				const char *msg;

				if (!all.p)
					vtext_init(&all, (size_t)1 << 32);
				all.n = 0;
				for (int i = 0; i < k; i++)
					vtext_append(&all, iov[i].iov_base, iov[i].iov_len);
#ifdef VCC_WASM
				if (WASM(c)) {
					uint8_t *ob;
					size_t on;
					char err[512];

					if (wasm_assemble(all.p, all.n, o->target == PP_WASM32, &ob, &on, err, sizeof err)) {
						codegen_assembler_error_report(c, "assembler: %s", err);
						E.errors++;
					} else {
						o->wasm_obj->n = 0;
						vbuffer_append(o->wasm_obj, ob, on);
						free(ob);
					}
				} else
#endif
				if (codegen_object_target_set(c, o->obj),
					   as_run(o->as, o->obj, all.p, all.n, "<vcc>", &msg)) {
					codegen_assembler_error_report(c, "assembler: %s", msg);
					E.errors++;
				}
			} else if (codegen_chunks_commit(c, o, hdr.p, hdr.n, tail.p, tail.n, E.chunks, E.chunk_count)) {
				/* object: the items replayed into the TU's assembler and ElfObj */
				E.errors++;
			}
		} else if (!E.errors && write_out && !*again) {
			static _Thread_local tbuf all, mo;

			if (CODEGEN_IS_DARWIN(c)) {
				/* -S for Mach-O: the text in Apple's dialect (darwin.c) */
				if (!all.p) {
					vtext_init(&all, (size_t)1 << 32);
					vtext_init(&mo, (size_t)1 << 32);
				}
				all.n = mo.n = 0;
				for (int i = 0; i < k; i++)
					vtext_append(&all, iov[i].iov_base, iov[i].iov_len);
				darwin_text(&mo, all.p, all.n);
				iov[0] = (struct iovec){ mo.p, mo.n };
				k = 1;
			}
			for (int i = 0; i < k;) {
				int m = k - i > 1024 ? 1024 : k - i;
				ssize_t w = writev(o->outfd, iov + i, m);

				if (w < 0) {
					perror("vcc: write");
					E.errors++;
					break;
				}
				/* partial writes: finish the rest one by one */
				{
					size_t want = 0;

					for (int j = i; j < i + m; j++)
						want += iov[j].iov_len;
					if ((size_t)w < want) {
						size_t done = (size_t)w;

						for (int j = i; j < i + m; j++) {
							size_t l = iov[j].iov_len;

							if (done >= l) {
								done -= l;
								continue;
							}
							if (write(o->outfd, (char *)iov[j].iov_base + done, l - done) < 0)
								break;
							done = 0;
						}
					}
				}
				i += m;
			}
		}
		free(iov);
		if (getenv("VCC_EMIT_STATS")) {
			size_t bytes = hdr.n + tail.n;

			for (uint32_t i = 0; i < E.chunk_count; i++)
				bytes += E.chunks[i].length;
			fprintf(stderr, "vcc emit: %llu functions, %llu objects, %llu inlined, %llu instructions (%.1f KB code); "
				"setup %.2f ms, emit %.2f ms (%d threads; %.2f ms printing, %.2f ms prepass over %llu functions), %.1f MB text; %.0f MB/s machine code\n",
				(unsigned long long)E.function_total, (unsigned long long)E.object_total, (unsigned long long)E.inline_total,
				(unsigned long long)E.record_total, E.record_total * 4 / 1024.0, (t1 - t0) * 1e3, (t2 - t1) * 1e3, nthr,
				E.print_nanoseconds / 1e6, E.walk_nanoseconds / 1e6, (unsigned long long)E.walk_total, bytes / 1e6, E.record_total * 4 / 1e6 / (t2 - t1));
		}
		if (E.vir_statistics && write_out && !*again)	/* (the final round's: objects and text alike) */
			virbuild_statistics_print(&E);
		vjob_region_release(&er[13], tail.n);
	}
	if (E.record_statistics)
		rp_print(&E);
	vjob_region_release(&er[12], hdr.n);
	for (int i = 0; i < nthr; i++)
		ecx_free(&ctx[i], xr[i]);
	free(ctx);
	errors = E.errors;
	vjob_region_release(&er[0], c->nsym + 64);
	vjob_region_release(&er[1], c->nsym + 64);
	vjob_region_release(&er[2], c->nsym + 64);
	vjob_region_release(&er[3], c->nstr + 64);
	vjob_region_release(&er[4], c->nnode + 64);
	vjob_region_release(&er[5], c->nlabel + 64);
	vjob_region_release(&er[6], (size_t)c->nlabel * 2 + 64);
	vjob_region_release(&er[7], VJOB_REGION_SCRATCH);	/* the queue and the chunks: written before read */
	vjob_region_release(&er[8], VJOB_REGION_SCRATCH);
	vjob_region_release(&er[9], c->nsym + 64);
	vjob_region_release(&er[10], (c->nsym + 64) * sizeof(uint16_t));
	vjob_region_release(&er[11], (c->nsym + 64) * sizeof(i128));
	vjob_region_release(&er[14], c->nsym + 64);
	vjob_region_release(&er[15], (c->nsym + 64) * sizeof(uint16_t));
	vjob_region_release(&er[16], c->nsym + 64);
	vjob_region_release(&er[17], (c->nsym + 64) * 2 * sizeof(uint32_t));
	free((void *)E.refptr);
	vjob_region_pool_give(&codegen_round_pool, er);
	return errors;
}

/*
 * VCC_EMIT_REPEAT=N: generate N times over the same AST (profiling), write once.
 * A round that marks functions for inlining (need_fold) is generated again.
 */
extern _Atomic unsigned long long driver_emit_nanoseconds;

int codegen_unit_emit(cc *c, const cc_options *o)
{
	if (!codegen_target_supports_sve(c) || !codegen_target_supports_bitint(c))
		return 1;

	double t0 = codegen_clock_seconds();
	vperf v0 = vperformance_now();
	const char *r = getenv("VCC_EMIT_REPEAT");
	int n = r ? atoi(r) : 1, err = 0;
	static vjob_region_pool mfpool = VJOB_REGION_POOL_INIT(1);
	vjob_region *mfr = vjob_region_pool_take(&mfpool);
	uint8_t *mf = vjob_region_acquire(mfr, c->nsym + 64, "emit.mf");
	bool again = false;
	codegen_keep K[2];

	memset(K, 0, sizeof(K));
	for (int i = 1; i < n; i++)
		if (codegen_unit_round_emit(c, o, false, mf, &again, false, NULL, NULL))
			break;
	for (int round = 0; round < 8; round++) {
		codegen_keep *prev = &K[round & 1], *next = &K[(round + 1) & 1];

		codegen_keep_free(next);
		err = codegen_unit_round_emit(c, o, true, mf, &again, round == 7, round ? prev : NULL, next);
		if (err || !again)
			break;
	}
	codegen_keep_free(&K[0]);
	codegen_keep_free(&K[1]);
	vjob_region_release(mfr, c->nsym + 64);
	vjob_region_pool_give(&mfpool, mfr);
	driver_emit_nanoseconds += (unsigned long long)((codegen_clock_seconds() - t0) * 1e9);
	driver_phase_add(DRIVER_PHASE_EMIT, v0);
	return err;
}
