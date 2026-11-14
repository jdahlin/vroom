/*
 * references.c: the reference log: which symbols and strings a function needs emitted.
 *
 * note_ref is called by the walker, the lowering and the data emitter whenever generated code or data
 * mentions a symbol; claim_ref logs it (rlog, so a later round can reuse the function) and, for a
 * static function, a static object, a string or a weak reference, queues it once for emission
 * (E->claimed, strdone and extref are atomic exchange flags: the first thread to claim queues it).
 * This is how lazily emitted statics work: a static function nobody references is never generated.
 * A referenced function whose body was never parsed is an internal error.  Nothing is noted when
 * the code is unreachable (e->reach false) or a dry run (e->dry).
 */
#include "emitimpl.h"

VPRIV void note_ref(ecx *e, uint32_t ref)
{
	if (!e->reach || e->dry)
		return;
	claim_ref(e, ref);
}

VPRIV void claim_ref(ecx *e, uint32_t ref)
{
	emitter *E = e->E;
	cc *c = e->c;

	rlog(e, RL_REF, ref);
	if (RKIND(ref) == RK_STR) {
		uint32_t i = RIDX(ref);

		if (!atomic_load_explicit(&E->strdone[i], memory_order_relaxed) &&
		    !atomic_exchange(&E->strdone[i], 1))
			enqueue(E, 0x80000000u | i);
		return;
	}
	if (RKIND(ref) != RK_SYM)
		return;
	{
		uint32_t s = canon_sym(c, RIDX(ref));
		const sym *y = &c->syms[s];

		if (unlikely((y->flags & SF_WEAK) && !(y->flags & SF_DEFINED)) &&
		    !atomic_load_explicit(&E->extref[s], memory_order_relaxed) &&
		    !atomic_exchange(&E->extref[s], 1))
			e->weakref[e->nweakref++] = s;
		if (atomic_load_explicit(&E->claimed[s], memory_order_relaxed))
			return;
		if (y->kind == S_FUNC) {
			if (unlikely(y->flags & SF_LAZY))
				efatal(e, y->loc, "internal error: '%s' is referenced but its body was never parsed",
				       vintern_ents[y->name].s);
			if (!(y->flags & SF_DEFINED) || !y->body || (y->sc != SC_STATIC && !(y->flags & SF_LINKONCE) && !(WASM(c) && inline_only(y))))
				return;
		} else if (y->kind == S_VAR) {
			bool local_static = y->depth > 0 && y->sc == SC_STATIC;
			bool file_static = y->depth == 0 && (y->sc == SC_STATIC || (y->flags & SF_LINKONCE));

			if (!(local_static || file_static || !y->name))
				return;
			if (!(y->flags & (SF_DEFINED | SF_TENTATIVE)) && !local_static && y->name)
				return;
		} else {
			return;
		}
		if (unlikely(sym_attrs(c, s)->alias))
			return;	/* a .set in the tail (emit_tu keeps its target), not storage */
		if (!atomic_exchange(&E->claimed[s], 1))
			enqueue(E, s);
	}
}

VPRIV void walk_fn(ecx *e, uint32_t s)
{
	rlog(e, RL_WALK, s);
	ensure_walked(e->E, e->c, s);
}
