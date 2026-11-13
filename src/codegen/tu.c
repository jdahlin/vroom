/*
 * tu.c: generate code for one translation unit (emit_tu): the emitter state, the work queue and
 * worker threads, and putting the chunks together.
 *
 * emit_tu is called by the front end (frontend/c/translation-unit.c) once the AST is complete.  It
 * runs emit_tu1 up to 8 rounds: a round that finds a function that must be inlined to fold an
 * error() call (need_fold) marks it and the unit is generated again, reusing the functions whose
 * inputs did not change (keep_make, reuse).  In a round the roots (non-static functions and objects)
 * are queued; workers (or vtask_parallel_for slots) take items, generate each into its own ecx
 * (buffers from pools: nothing is allocated per function), and note_ref queues what they reference.
 * At the end chunks are sorted by key (chunk_cmp) so the output never depends on thread timing, and
 * either written as text or replayed into the object (replay, obj.c).  Diagnostics from workers are
 * serialised with E->emu.  Errors that cannot continue longjmp to the item's e->bail and fail that
 * function only.  Called per TU on the job's thread, which may itself be one of the shared pool's.
 * Environment: VCC_EMIT_REPEAT=N generates N times for profiling.
 */
#include "emitimpl.h"
#ifdef VCC_WASM
#include "arch/wasm/as/wasmas.h"
#endif
#include "common/buffer.h"
#include "common/scoped-cleanup.h"
#include "port/task.h"

VPRIV void *reserve(size_t n) { return vreserve(n, MAP_NORESERVE, "vcc: mmap"); }

VPRIV double now(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

const uint8_t tempregs[NTEMP] = { 9, 10, 11, 12, 13, 14, 15, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28 };

__attribute__((format(printf, 3, 4)))
VPRIV void ediag(ecx *e, uint32_t loc, const char *fmt, ...)
{
	diag_report_site_pc = __builtin_return_address(0);
	va_list ap;

	VLOCK(e->E->emu);
	va_start(ap, fmt);
	diag_report_va(e->c->p, loc, "note", fmt, ap);
	va_end(ap);
}

__attribute__((format(printf, 3, 4)))
VPRIV void eerror(ecx *e, uint32_t loc, const char *fmt, ...)
{
	diag_report_site_pc = __builtin_return_address(0);
	va_list ap;

	if (e->vtry) {	/* the VIR walker: the function falls back (codegen/vir/) */
		if (!e->vfail)
			e->vfail = fmt;
		return;
	}
	{
		VLOCK(e->E->emu);
		va_start(ap, fmt);
		diag_report_va(e->c->p, loc, "error", fmt, ap);
		va_end(ap);
	}
	e->E->errors++;
}

__attribute__((format(printf, 3, 4), noreturn))
VPRIV void efatal(ecx *e, uint32_t loc, const char *fmt, ...)
{
	diag_report_site_pc = __builtin_return_address(0);
	va_list ap;

	if (e->vtry) {	/* the VIR walker: no diagnostic, the function falls back (codegen/vir/) */
		if (!e->vfail)
			e->vfail = fmt;
		_longjmp(e->bail, 1);
	}
	{	/* (the lock is dropped before the longjmp: cleanups do not run on it) */
		VLOCK(e->E->emu);
		va_start(ap, fmt);
		diag_report_va(e->c->p, loc, "error", fmt, ap);
		va_end(ap);
	}
	e->E->errors++;
	_longjmp(e->bail, 1);
}

/* ---------------- the work queue and threads ---------------- */

VPRIV void enqueue(emitter *E, uint32_t item)
{
	VLOCK(E->mu);
	E->q[E->qt++] = item;
	pthread_cond_signal(&E->cv);
}

/* the regions of a ctx[i] (ecx_init takes a set, ecx_free gives it back) */
/* a quoted assembler string */
static void dbg_str(tbuf *b, const char *s)
{
	vtext_char_append(b, '"');
	for (; *s; s++) {
		if (*s == '"' || *s == '\\')
			vtext_char_append(b, '\\');
		vtext_char_append(b, *s);
	}
	vtext_char_append(b, '"');
}

static vjob_region_pool ecxpool = VJOB_REGION_POOL_INIT(16);

static void ecx_init(ecx *e, emitter *E, vjob_region *r)
{
	cc *c = E->c;

	memset(e, 0, sizeof(*e));
	e->E = E;
	e->c = c;
	e->capin = 1u << 24;
	e->in = vjob_region_acquire(&r[0], (size_t)e->capin * sizeof(ins), "emit.in");
	vtext_region_init(&e->raw, &r[1], (size_t)1 << 28, "emit.raw");
	vtext_region_init(&e->out, &r[2], (size_t)1 << 32, "emit.out");
	vtext_region_init(&e->data, &r[3], (size_t)1 << 28, "emit.data");
	e->rel = vjob_region_acquire(&r[4], (size_t)1 << 26, "emit.rel");
	e->b = vjob_region_acquire(&r[5], (size_t)c->nsym * sizeof(bind) + 4096, "emit.b");
	e->lm = vjob_region_acquire(&r[6], (size_t)c->nlabel * sizeof(lbl) + 4096, "emit.lm");
	e->caselbl = vjob_region_acquire(&r[7], (size_t)c->nnode * sizeof(uint32_t) + 4096, "emit.caselbl");
	e->caplabel = 1u << 22;
	e->lrefc = vjob_region_acquire(&r[8], (size_t)e->caplabel * sizeof(uint32_t), "emit.lrefc");
	e->lkvi = vjob_region_acquire(&r[9], (size_t)e->caplabel * sizeof(uint32_t), "emit.lkvi");
	e->kvs = vjob_region_acquire(&r[10], (size_t)CAPKVS * sizeof(kvst), "emit.kvs");
	e->cases = vjob_region_acquire(&r[11], (size_t)1 << 24, "emit.cases");
	e->weakref = vjob_region_acquire(&r[12], (size_t)c->nsym * sizeof(uint32_t) + 4096, "emit.weakref");
	e->rlog = vjob_region_acquire(&r[13], RLOG_CAP * sizeof(uint32_t), "emit.rlog");
	e->sla = vjob_region_acquire(&r[14], (size_t)CAPSLA * (sizeof(slotal) + sizeof(uint32_t)), "emit.sla");
	e->sstk = (uint32_t *)(e->sla + CAPSLA);
	/* nesting: reserved, touched only as deep as it goes (bounded by the parser's stack guard) */
	e->loops = vjob_region_acquire(&r[15], (size_t)CAPNEST * (sizeof(loopctx) + sizeof(uint32_t)), "emit.loops");
	e->clean = (uint32_t *)(e->loops + CAPNEST);
}

/* (after the stitch: the chunks point into e->out) */
static void ecx_free(ecx *e, vjob_region *r)
{
	em_switch_values_reset(e);
	cc *c = e->c;

	/* written before read (put, newlabel, the case collection, ...): handed back as is */
	vjob_region_release(&r[0], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[1], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[2], e->out.n);
	vjob_region_release(&r[3], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[4], VJOB_REGION_SCRATCH);
	/* per sym / label, found zero */
	vjob_region_release(&r[5], (size_t)c->nsym * sizeof(bind) + 4096);
	vjob_region_release(&r[6], (size_t)c->nlabel * sizeof(lbl) + 4096);
	vjob_region_release(&r[7], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[8], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[9], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[10], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[11], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[12], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[13], VJOB_REGION_SCRATCH);	/* (copied by keep_make before this) */
	vjob_region_release(&r[14], VJOB_REGION_SCRATCH);
	vjob_region_release(&r[15], VJOB_REGION_SCRATCH);
	vjob_region_pool_give(&ecxpool, r);
#ifdef VCC_WASM
	wasm_ecx_free(e);
#endif
	wk_ecx_free(e);
	virbuild_wk_ecx_free(e);
}

/* a function the previous round generated, if nothing it asked about has been marked since */
static bool reuse(ecx *e, uint32_t s)
{
	const keep *K = e->E->K;
	const kent *k;
	tbuf *b = &e->out;
	size_t start;

	if (!K || s >= K->nidx || !K->idx[s])
		return false;
	k = &K->ent[K->idx[s] - 1];
	for (uint32_t j = 0; j < k->nlog; j++)
		if (k->log[2 * j] == RL_DEP && e->E->mf[k->log[2 * j + 1]])
			return false;
	for (uint32_t j = 0; j < k->nlog; j++) {
		uint32_t v = k->log[2 * j + 1];

		switch (k->log[2 * j]) {
		case RL_REF: claim_ref(e, v); break;
		case RL_DEP: rlog(e, RL_DEP, v); break;
		case RL_WALK: walk_fn(e, v); break;
		}
	}
	b->n = (b->n + 7) & ~(size_t)7;
	start = b->n;
	memcpy(it_room(e, k->n), k->p, k->n);
	out_chunk(e, k->key, start);
	return true;
}

static void do_item(ecx *e, uint32_t it)
{
	cc *c = e->c;

	e->rlog0 = e->nrlog;
	if (it & 0x80000000u) {
		emit_string(e, it & 0x7fffffffu);
		return;
	}
	if (WASM(c) && c->syms[it].kind == S_FUNC) {
		wasm_emit_function(e, it);
	} else if (c->syms[it].kind == S_FUNC && X86(c)) {
		if (!virbuild_function_try(e, it))
			wk_function(e, it);
	} else if (c->syms[it].kind == S_FUNC) {
		if (!reuse(e, it) && !virbuild_function_try(e, it))
			emit_function(e, it);
	} else {
		emit_object(e, it);
	}
}

static void *worker(void *arg)
{
	ecx *e = arg;
	emitter *E = e->E;

	pthread_mutex_lock(&E->mu);
	for (;;) {
		uint32_t it;

		while (E->qh == E->qt && E->busy > 0)
			pthread_cond_wait(&E->cv, &E->mu);
		if (E->qh == E->qt)
			break;
		it = E->q[E->qh++];
		E->busy++;
		pthread_mutex_unlock(&E->mu);
		do_item(e, it);
		pthread_mutex_lock(&E->mu);
		E->busy--;
		if (E->busy == 0 && E->qh == E->qt)
			pthread_cond_broadcast(&E->cv);
	}
	pthread_cond_broadcast(&E->cv);
	pthread_mutex_unlock(&E->mu);
	return NULL;
}

/* one slot of the drain loop (vtask_parallel_for item) */
static void emit_slot(void *arg, size_t i)
{
	worker(&((ecx *)arg)[i]);
}

static int chunk_cmp(const void *a, const void *b)
{
	const chunk *x = a, *y = b;

	return x->key < y->key ? -1 : x->key > y->key;
}

static bool is_root(cc *c, uint32_t s)
{
	const sym *y = &c->syms[s];
	const attrs *a = sym_attrs(c, s);

	if (y->depth != 0 && !(y->kind == S_VAR && y->sc == SC_STATIC && a->used))
		return false;
	if (a->alias)
		return false;	/* .set in the tail; its target is a root (emit_tu) */
	if (y->kind == S_FUNC) {
		if (!(y->flags & SF_DEFINED) || !y->body)
			return false;
		if ((y->flags & SF_GNU_INLINE) && !(y->flags & SF_INLINE_NOEXTERN))
			return false;	/* GNU 'extern inline': clang's isInlineDefinitionExternallyVisible */
		if (y->sc == SC_STATIC)
			return a->used || a->constructor || a->destructor;
		if ((y->flags & SF_INLINE) && !(y->flags & (SF_GNU_INLINE | SF_EXTDECL)) && y->sc != SC_EXTERN)
			return false;	/* C99 inline definition */
		return true;
	}
	if (y->kind == S_VAR) {
		if (!(y->flags & (SF_DEFINED | SF_TENTATIVE)))
			return false;
		if (!y->name || y->sc == SC_REGISTER)
			return false;	/* global register variables have no storage */
		if (y->sc == SC_STATIC)
			return a->used;
		return true;
	}
	return false;
}

__attribute__((format(printf, 2, 3)))
static void as_error(cc *c, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	diag_report_va(c->p, 0, "error", fmt, ap);
	va_end(ap);
}

/* the object backend: header text, chunks (items), tail text */
static vjob_region_pool rpool = VJOB_REGION_POOL_INIT(3);

static int replay(cc *c, const cc_opts *o, const char *hp, size_t hn, const char *tp, size_t tn, const chunk *ch, uint32_t nch)
{
	/* per function, written before read: reserved once per thread */
	static _Thread_local uint32_t *wb, *lsym, *lser, ser;
	static _Thread_local rlab *lab;
	static _Thread_local rpend *pend;
	static _Thread_local rpatch *pt;
	size_t need = (size_t)c->nsym + c->nstr + c->nlabel + 192;
	rctx R = { .c = c, .as = o->as, .o = o->obj };
	vjob_region *rr = vjob_region_pool_take(&rpool);
	uint32_t *cache;
	int r = 0;

	R.loff = vjob_region_acquire(&rr[0], (size_t)1 << 32, "emit.loff");	/* labels of one function */
	vtext_region_init(&R.nb, &rr[1], 1 << 20, "emit.nb");
	cache = vjob_region_acquire(&rr[2], need * sizeof(uint32_t), "emit.rcache");
	R.symc = cache;
	R.strc = cache + c->nsym + 64;
	R.lblc = R.strc + c->nstr + 64;
	if (!wb) {
		wb = reserve((size_t)1 << 32);		/* words of one function (<= 6 per record) */
		lsym = reserve((size_t)1 << 32);
		lser = reserve((size_t)1 << 32);
		lab = reserve((size_t)1 << 34);		/* per label */
		pend = reserve((size_t)1 << 34);	/* per branch record */
		pt = reserve((size_t)1 << 34);		/* fixups of one function (<= 1 per record) */
	}
	R.wb = wb;
	R.lsym = lsym;
	R.lser = lser;
	R.lab = lab;
	R.pend = pend;
	R.ser = ser;
	R.pt = pt;
	elf_set_machine(R.o, X86(c) ? EM_X86_64 : EM_AARCH64);
	as_begin(R.as, R.o, "<vcc>");
	if (as_snippet_assemble(R.as, hp, hn))
		r = -1;
	for (uint32_t i = 0; !r && i < nch; i++)
		r = r_chunk(&R, ch[i].p, ch[i].n);
	ser = R.ser;
	if (!r && (as_snippet_assemble(R.as, tp, tn) || as_end(R.as)))
		r = -1;
	vjob_region_release(&rr[0], VJOB_REGION_SCRATCH);
	vjob_region_release(&rr[1], VJOB_REGION_SCRATCH);
	vjob_region_release(&rr[2], need * sizeof(uint32_t));
	vjob_region_pool_give(&rpool, rr);
	if (r || R.o->has_err || as_is_failed(R.as)) {
		as_error(c, "assembler: %s", as_is_failed(R.as) ? as_message_get(R.as) : R.o->err);
		return -1;
	}
	return 0;
}

/* what the next round may reuse: this round's functions (their chunks and logs, copied) */
static void keep_free(keep *K)
{
	if (K->idx) {
		munmap(K->idx, K->nidx * sizeof(uint32_t));
		munmap(K->ent, K->nent * sizeof(kent) + 64);
		munmap(K->bytes, K->nbytes + 64);
		munmap(K->logs, K->nlogs * sizeof(uint32_t) + 64);
	}
	memset(K, 0, sizeof(*K));
}

static void keep_make(keep *K, cc *c, const emitter *E)
{
	size_t nb = 0, nl = 0, ob = 0, ol = 0;
	uint32_t ne = 0;

	memset(K, 0, sizeof(*K));
	for (uint32_t i = 0; i < E->nchunk; i++) {
		const chunk *ch = &E->chunks[i];

		if (ch->key >> 62 || (ch->key & 1))
			continue;	/* strings, objects: generated again */
		nb += (ch->n + 7) & ~(size_t)7;
		nl += 2 * (size_t)ch->nlog;
		ne++;
	}
	K->nidx = c->nsym;
	K->idx = reserve(K->nidx * sizeof(uint32_t));
	K->nent = ne;
	K->ent = reserve(ne * sizeof(kent) + 64);
	K->nbytes = nb;
	K->bytes = reserve(nb + 64);
	K->nlogs = nl;
	K->logs = reserve(nl * sizeof(uint32_t) + 64);
	ne = 0;
	for (uint32_t i = 0; i < E->nchunk; i++) {
		const chunk *ch = &E->chunks[i];
		kent *k;

		if (ch->key >> 62 || (ch->key & 1))
			continue;
		k = &K->ent[ne++];
		k->key = ch->key;
		k->p = K->bytes + ob;
		k->n = ch->n;
		memcpy(K->bytes + ob, ch->p, ch->n);
		ob += (ch->n + 7) & ~(size_t)7;
		k->log = K->logs + ol;
		k->nlog = ch->nlog;
		memcpy(K->logs + ol, ch->log, 2 * (size_t)ch->nlog * sizeof(uint32_t));
		ol += 2 * (size_t)ch->nlog;
		K->idx[ch->key >> 1] = ne;
	}
}

/* emit_tu1's per-TU tables (E.*) and text buffers */
static vjob_region_pool erpool = VJOB_REGION_POOL_INIT(18);

static int emit_tu1(cc *c, const cc_opts *o, bool write_out, uint8_t *mf, bool *again, bool last,
		    const keep *K, keep *next)
{
	emitter E;
	int nthr = o->nthreads > 0 ? o->nthreads : 1;
	ecx *ctx;
	double t0 = now(), t1, t2;
	tbuf hdr;
	int errors;
	vjob_region *er = vjob_region_pool_take(&erpool), *xr[64];

	memset(&E, 0, sizeof(E));
	E.c = c;
	E.o = o;
	E.mf = mf;
	E.K = K;
	E.claimed = vjob_region_acquire(&er[0], c->nsym + 64, "emit.claimed");
	E.extref = vjob_region_acquire(&er[1], c->nsym + 64, "emit.extref");
	E.sflags = vjob_region_acquire(&er[2], c->nsym + 64, "emit.sflags");
	E.ffacts = vjob_region_acquire(&er[14], c->nsym + 64, "emit.ffacts");
	E.strdone = vjob_region_acquire(&er[3], c->nstr + 64, "emit.strdone");
	E.nflags = vjob_region_acquire(&er[4], c->nnode + 64, "emit.nflags");
	E.lref = vjob_region_acquire(&er[5], c->nlabel + 64, "emit.lref");
	E.lcdepth = vjob_region_acquire(&er[6], (size_t)c->nlabel * 2 + 64, "emit.lcdepth");
	E.q = vjob_region_acquire(&er[7], ((size_t)c->nsym + c->nstr + 64) * sizeof(uint32_t), "emit.q");
	E.chunks = vjob_region_acquire(&er[8], ((size_t)c->nsym + c->nstr + c->ntlasm + 64) * sizeof(chunk), "emit.chunks");
	pthread_mutex_init(&E.mu, NULL);
	pthread_cond_init(&E.cv, NULL);
	pthread_mutex_init(&E.cmu, NULL);
	pthread_mutex_init(&E.emu, NULL);
	pthread_mutex_init(&E.dmu, NULL);
	E.walked = vjob_region_acquire(&er[9], c->nsym + 64, "emit.walked");
	E.fnsize = vjob_region_acquire(&er[10], (c->nsym + 64) * sizeof(uint16_t), "emit.fnsize");
	E.cwval = vjob_region_acquire(&er[11], (c->nsym + 64) * sizeof(i128), "emit.cwval");
	E.luse = vjob_region_acquire(&er[15], (c->nsym + 64) * sizeof(uint16_t), "emit.luse");
	E.lnoreg = vjob_region_acquire(&er[16], c->nsym + 64, "emit.lnoreg");
	E.lspan = vjob_region_acquire(&er[17], (c->nsym + 64) * 2 * sizeof(uint32_t), "emit.lspan");
	{
		const char *k = getenv("VCC_LREGS");

		E.nlreg = k ? atoi(k) : LREGS_DEFAULT;
		if (E.nlreg < 0 || E.nlreg > MAXLREG)
			E.nlreg = MAXLREG;
	}
	E.stats = getenv("VCC_EMIT_STATS") != NULL;
	E.vstats = o->vir_aid.stats;
	virbuild_pass_switch_read(&E.vpass, o->optimize.vir ? o->vir_aid.passes : NULL);
	if (o->optimize.vir && o->vir_aid.functions)
		virbuild_function_list_read(&E, o->vir_aid.functions);
	E.rstats = getenv("VCC_REC_STATS") != NULL;
	E.noopt = getenv("VCC_NOOPT") != NULL;
	/* wasm64: objects from the text, through arch/wasm/as */
	E.objtext = getenv("VCC_OBJ_TEXT") != NULL || WASM(c);
	/* -g: the line table is the assembler's, from the printer's .loc */
	if (o->debug && !DARWIN(c) && !WASM(c) && !X86(c))
		E.objtext = true;
	/* unwind tables: .eh_frame is the assembler's, from the printer's .cfi_* */
	if (o->cfi && !DARWIN(c) && !WASM(c) && !X86(c))
		E.objtext = true;
	/* Lazy walks own only their function's automatic variables. Globals and
	   static objects can be modified by other functions (including ones not
	   emitted here); publish conservative immutable facts before workers start.
	   In particular, never infer a TU-wide constant from one function's writes. */
	for (uint32_t s = 1; s < c->nsym; s++)
		if (c->syms[s].kind == S_VAR && !is_local_var(c, s)) {
			E.sflags[s] = SA_WRITTEN | SA_AESC;
			E.ffacts[s] = FF_BAD;
		}
	/* VIR: the functions called once, which its walker inlines whatever their size */
	if (!WASM(c) && virbuild_is_enabled(o))
		E.once = fn_once(c);
	if (E.once)
		fn_isize(&E, c, WK_INLINE_VIR);
	/* roots */
	for (uint32_t i = 0; i < c->ntoplevel; i++) {
		uint32_t s = c->toplevel[i];

		if (is_root(c, s) && !E.claimed[s]) {
			E.claimed[s] = 1;
			E.q[E.qt++] = s;
		}
		if (unlikely(sym_attrs(c, s)->alias)) {
			/* an alias keeps its target, static or not */
			uint32_t t = cc_global_named(c, c->strs[sym_attrs(c, s)->alias - 1].s);

			if (t && !E.claimed[t] && (c->syms[t].flags & (SF_DEFINED | SF_TENTATIVE)) &&
			    (c->syms[t].kind == S_FUNC ? c->syms[t].body != 0 : true)) {
				E.claimed[t] = 1;
				E.q[E.qt++] = t;
			}
		}
	}
	t1 = now();
	if (nthr > 64)
		nthr = 64;
	ctx = calloc((size_t)nthr, sizeof(ecx));
	for (int i = 0; i < nthr; i++)
		ecx_init(&ctx[i], &E, xr[i] = vjob_region_pool_take(&ecxpool));
	/* the drain loop on nthr slots, slot i with ctx[i], on the shared pool
	 * (port/task.c): helpers are idle pool threads only, under the process'
	 * budget, so inside a full vpp --batch or vccd the slots run here one after
	 * the other (a slot that starts late finds the queue empty and returns).
	 * Pool threads have one stack size (as main's, or vccd's worker stack), so
	 * a function nests as deep, and compiles the same, on any of them; their
	 * thread-local scratch (optimizer, VIR) lives as long as they do instead of
	 * being leaked by a thread per round. */
	vtask_parallel_for((size_t)nthr, nthr, emit_slot, ctx, 0);
	t2 = now();
	free(E.once);free(E.inl_read);
	E.once = E.inl_read = NULL;
	free(E.cg), free(E.cgoff), free(E.cgn), free(E.scc), free(E.isz);
	E.cg = NULL; E.cgoff = E.cgn = E.scc = NULL, E.isz = NULL;
	/* stitch */
	qsort(E.chunks, E.nchunk, sizeof(chunk), chunk_cmp);
	vtext_region_init(&hdr, &er[12], (size_t)1 << 26, "emit.hdr");
	if (WASM(c))
		wasm_header(c, &hdr, ctx, nthr);
	else if (X86(c) && (o->xcf & 1))
		/* -fcf-protection=branch: GNU_PROPERTY_X86_FEATURE_1_AND = IBT, as clang's start of file */
		vtext_string_append(&hdr, "\t.section\t.note.gnu.property,\"a\",@note\n\t.p2align\t3, 0x0\n\t.long\t4\n\t.long\t16\n"
			   "\t.long\t5\n\t.asciz\t\"GNU\"\n\t.long\t3221225474\n\t.long\t4\n\t.long\t1\n\t.p2align\t3, 0x0\n\t.text\n");
	else
		vtext_string_append(&hdr, "\t.text\n");
	bool dbg = o->debug && !DARWIN(c) && !WASM(c);
	if (dbg) {
		vtext_string_append(&hdr, ".Ldbg_text0:\n\t.file\t0 ");
		dbg_str(&hdr, o->dbg_dir ? o->dbg_dir : ".");
		vtext_char_append(&hdr, ' ');
		dbg_str(&hdr, o->dbg_name ? o->dbg_name : "<stdin>");
		vtext_char_append(&hdr, '\n');
		for (uint32_t i = 0; i < E.ndfile; i++) {
			vtext_format_append(&hdr, "\t.file\t%u ", i + 1);
			dbg_str(&hdr, E.dfile[i]);
			vtext_char_append(&hdr, '\n');
			free(E.dfile[i]);
		}
		free(E.dfile);
	}
	for (uint32_t i = 0; i < c->ntlasm; i++) {
		const casm *a = &c->asms[N(c->tlasm[i])->a];

		vtext_string_append(&hdr, c->strs[a->tmpl - 1].s);
		vtext_string_append(&hdr, WASM(c) ? "\n" : "\n\t.text\n");
	}
	{
		tbuf tail;
		struct iovec *iov = malloc(((size_t)E.nchunk + 2) * sizeof(struct iovec));
		int k = 0;

		vtext_region_init(&tail, &er[13], (size_t)1 << 26, "emit.tail");
		{
			/* .weak for weak undefined syms, in symbol order whatever the threads did */
			uint32_t nw = 0, *w;

			for (int t = 0; t < nthr; t++)
				nw += ctx[t].nweakref;
			w = malloc((nw + 1) * sizeof(uint32_t));
			nw = 0;
			for (int t = 0; t < nthr; t++)
				for (uint32_t j = 0; j < ctx[t].nweakref; j++)
					w[nw++] = ctx[t].weakref[j];
			qsort(w, nw, sizeof(uint32_t), u32_cmp);
			for (uint32_t j = 0; j < nw; j++)
				if (!E.claimed[w[j]] && c->syms[w[j]].name && !sym_attrs(c, w[j])->alias) {
					vtext_string_append(&tail, "\t.weak\t");
					sym_name(c, &tail, w[j]);
					vtext_char_append(&tail, '\n');
				}
			free(w);
		}
		for (uint32_t ti = 0; ti < c->ntoplevel; ti++) {
			uint32_t s = c->toplevel[ti];
			const sym *y = &c->syms[s];

			/* aliases */
			if (y->depth == 0 && sym_attrs(c, s)->alias && y->name) {
				const attrs *a = sym_attrs(c, s);

				/* Darwin: clang's (#pragma weak) aliases are local, .weak_reference on a definition */
				/* clang: an ifunc is global even when static */
				vtext_string_append(&tail, DARWIN(c) ? "" : (y->flags & SF_WEAK) ? "\t.weak\t" : y->sc == SC_STATIC && !a->ifunc ? "" : "\t.globl\t");
				if (!DARWIN(c) && (y->sc != SC_STATIC || (y->flags & SF_WEAK) || a->ifunc)) {
					sym_name(c, &tail, s);
					vtext_char_append(&tail, '\n');
				}
				vtext_string_append(&tail, "\t.type\t");
				sym_name(c, &tail, s);
				vtext_string_append(&tail, a->ifunc ? ",@gnu_indirect_function\n" : y->kind == S_FUNC ? ",@function\n" : ",@object\n");
				vtext_string_append(&tail, "\t.set\t");
				sym_name(c, &tail, s);
				vtext_string_append(&tail, DARWIN(c) ? ", _" : ", ");	/* (an assembler name: Mach-O's prefix) */
				vtext_string_append(&tail, c->strs[a->alias - 1].s);
				vtext_char_append(&tail, '\n');
			}
		}
		if (dbg) {
			/* one compile unit over .text: producer, C11, name, its line table, dir, pc range */
			/* (x86: .word is 2 bytes there, so .short/.long/.quad) */
			bool x = X86(c);

			vtext_string_append(&tail, "\t.text\n.Ldbg_text1:\n"
				    "\t.section\t.debug_abbrev,\"\",@progbits\n.Ldbg_abbrev:\n"
				    "\t.byte\t1, 17, 0, 37, 8, 19, 5, 3, 8, 16, 23, 27, 8, 17, 1, 18, 6, 0, 0, 0\n"
				    "\t.section\t.debug_info,\"\",@progbits\n");
			vtext_string_append(&tail, x ? "\t.long\t.Ldbg_cu_end-.Ldbg_cu_start\n.Ldbg_cu_start:\n"
				        "\t.short\t5\n\t.byte\t1, 8\n\t.long\t.Ldbg_abbrev\n\t.byte\t1\n\t.asciz\t\"vcc\"\n"
				        "\t.short\t29\n\t.asciz\t" :
				        "\t.word\t.Ldbg_cu_end-.Ldbg_cu_start\n.Ldbg_cu_start:\n"
				        "\t.hword\t5\n\t.byte\t1, 8\n\t.word\t.Ldbg_abbrev\n\t.byte\t1\n\t.asciz\t\"vcc\"\n"
				        "\t.hword\t29\n\t.asciz\t");
			dbg_str(&tail, o->dbg_name ? o->dbg_name : "<stdin>");
			vtext_string_append(&tail, x ? "\n\t.long\t.Lline_table_start0\n\t.asciz\t" : "\n\t.word\t.Lline_table_start0\n\t.asciz\t");
			dbg_str(&tail, o->dbg_dir ? o->dbg_dir : ".");
			vtext_string_append(&tail, x ? "\n\t.quad\t.Ldbg_text0\n\t.long\t.Ldbg_text1-.Ldbg_text0\n.Ldbg_cu_end:\n" :
				        "\n\t.xword\t.Ldbg_text0\n\t.word\t.Ldbg_text1-.Ldbg_text0\n.Ldbg_cu_end:\n");
			vtext_string_append(&tail, "\t.section\t.debug_line,\"\",@progbits\n.Lline_table_start0:\n");
		}
		if (WASM(c))
			wasm_tail(c, &tail, E.claimed);
		else
			vtext_string_append(&tail, "\t.section\t\".note.GNU-stack\",\"\",@progbits\n");
		iov[k++] = (struct iovec){ hdr.p, hdr.n };
		for (uint32_t i = 0; i < E.nchunk; i++)
			iov[k++] = (struct iovec){ (void *)E.chunks[i].p, E.chunks[i].n };
		iov[k++] = (struct iovec){ tail.p, tail.n };
		*again = E.newmf != 0 && !last;
		if (*again && next && !E.errors)
			keep_make(next, c, &E);
		if (!E.errors && write_out && !*again && o->emit == 2) {
			if (E.objtext) {
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
						as_error(c, "assembler: %s", err);
						E.errors++;
					} else {
						o->wasm_obj->n = 0;
						vbuffer_append(o->wasm_obj, ob, on);
						free(ob);
					}
				} else
#endif
				if (elf_set_machine(o->obj, X86(c) ? EM_X86_64 : EM_AARCH64),
					   as_run(o->as, o->obj, all.p, all.n, "<vcc>", &msg)) {
					as_error(c, "assembler: %s", msg);
					E.errors++;
				}
			} else if (replay(c, o, hdr.p, hdr.n, tail.p, tail.n, E.chunks, E.nchunk)) {
				/* object: the items replayed into the TU's assembler and ElfObj */
				E.errors++;
			}
		} else if (!E.errors && write_out && !*again) {
			static _Thread_local tbuf all, mo;

			if (DARWIN(c)) {
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

			for (uint32_t i = 0; i < E.nchunk; i++)
				bytes += E.chunks[i].n;
			fprintf(stderr, "vcc emit: %llu functions, %llu objects, %llu inlined, %llu instructions (%.1f KB code); "
				"setup %.2f ms, emit %.2f ms (%d threads; %.2f ms printing, %.2f ms prepass over %llu functions), %.1f MB text; %.0f MB/s machine code\n",
				(unsigned long long)E.nfn, (unsigned long long)E.nobj, (unsigned long long)E.ninl,
				(unsigned long long)E.nins, E.nins * 4 / 1024.0, (t1 - t0) * 1e3, (t2 - t1) * 1e3, nthr,
				E.print_ns / 1e6, E.walk_ns / 1e6, (unsigned long long)E.nwalk, bytes / 1e6, E.nins * 4 / 1e6 / (t2 - t1));
		}
		if (E.vstats && write_out && !*again)	/* (the final round's: objects and text alike) */
			virbuild_statistics_print(&E);
		vjob_region_release(&er[13], tail.n);
	}
	if (E.rstats)
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
	vjob_region_pool_give(&erpool, er);
	return errors;
}

/*
 * VCC_EMIT_REPEAT=N: generate N times over the same AST (profiling), write once.
 * A round that marks functions for inlining (need_fold) is generated again.
 */
extern _Atomic unsigned long long driver_emit_nanoseconds;

int emit_tu(cc *c, const cc_opts *o)
{
	if (!sve_codegen_ok(c) || !bitint_codegen_ok(c))
		return 1;

	double t0 = now();
	vperf v0 = vperformance_now();
	const char *r = getenv("VCC_EMIT_REPEAT");
	int n = r ? atoi(r) : 1, err = 0;
	static vjob_region_pool mfpool = VJOB_REGION_POOL_INIT(1);
	vjob_region *mfr = vjob_region_pool_take(&mfpool);
	uint8_t *mf = vjob_region_acquire(mfr, c->nsym + 64, "emit.mf");
	bool again = false;
	keep K[2];

	memset(K, 0, sizeof(K));
	for (int i = 1; i < n; i++)
		if (emit_tu1(c, o, false, mf, &again, false, NULL, NULL))
			break;
	for (int round = 0; round < 8; round++) {
		keep *prev = &K[round & 1], *next = &K[(round + 1) & 1];

		keep_free(next);
		err = emit_tu1(c, o, true, mf, &again, round == 7, round ? prev : NULL, next);
		if (err || !again)
			break;
	}
	keep_free(&K[0]);
	keep_free(&K[1]);
	vjob_region_release(mfr, c->nsym + 64);
	vjob_region_pool_give(&mfpool, mfr);
	driver_emit_nanoseconds += (unsigned long long)((now() - t0) * 1e9);
	driver_phase_add(DRIVER_PHASE_EMIT, v0);
	return err;
}
