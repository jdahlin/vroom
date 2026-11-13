/*
 * references.c: the reference log: which symbols and strings a function needs emitted.
 *
 * note_ref is called by the walker, the lowering and the data emitter whenever generated code or data
 * mentions a symbol; codegen_reference_claim logs it (rlog, so a later round can reuse the function) and, for a
 * static function, a static object, a string or a weak reference, queues it once for emission
 * (E->claimed, strdone and extref are atomic exchange flags: the first thread to claim queues it).
 * This is how lazily emitted statics work: a static function nobody references is never generated.
 * A referenced function whose body was never parsed is an internal error.  Nothing is noted when
 * the code is unreachable (e->reach false) or a dry run (e->dry).
 */
#include "codegen-internal.h"

VPRIV void codegen_reference_note(codegen_context *e, uint32_t ref)
{
	if (!e->reach || e->dry)
		return;
	codegen_reference_claim(e, ref);
}

VPRIV void codegen_reference_claim(codegen_context *e, uint32_t ref)
{
	codegen_unit *E = e->E;
	cc *c = e->c;

	codegen_log_append(e, CODEGEN_LOG_REFERENCE, ref);
	if (CODEGEN_REFERENCE_KIND(ref) == CODEGEN_REFERENCE_STRING) {
		uint32_t i = CODEGEN_REFERENCE_INDEX(ref);

		if (!atomic_load_explicit(&E->strdone[i], memory_order_relaxed) &&
		    !atomic_exchange(&E->strdone[i], 1))
			codegen_work_enqueue(E, 0x80000000u | i);
		return;
	}
	if (CODEGEN_REFERENCE_KIND(ref) != CODEGEN_REFERENCE_SYMBOL)
		return;
	{
		uint32_t s = codegen_symbol_canonical(c, CODEGEN_REFERENCE_INDEX(ref));
		const sym *y = &c->syms[s];

		if (unlikely((y->flags & AST_SYMBOL_FLAG_WEAK) && !(y->flags & AST_SYMBOL_FLAG_DEFINED)) &&
		    !atomic_load_explicit(&E->extref[s], memory_order_relaxed) &&
		    !atomic_exchange(&E->extref[s], 1))
			e->weakref[e->nweakref++] = s;
		if (atomic_load_explicit(&E->claimed[s], memory_order_relaxed))
			return;
		if (y->kind == AST_SYMBOL_FUNCTION) {
			if (unlikely(y->flags & AST_SYMBOL_FLAG_LAZY))
				codegen_fatal_report(e, y->loc, "internal error: '%s' is referenced but its body was never parsed",
				       vintern_ents[y->name].s);
			if (!(y->flags & AST_SYMBOL_FLAG_DEFINED) || !y->body || (y->sc != AST_STORAGE_STATIC && !(WASM(c) && codegen_symbol_is_inline_only(y))))
				return;
		} else if (y->kind == AST_SYMBOL_VARIABLE) {
			bool local_static = y->depth > 0 && y->sc == AST_STORAGE_STATIC;
			bool file_static = y->depth == 0 && y->sc == AST_STORAGE_STATIC;

			if (!(local_static || file_static || !y->name))
				return;
			if (!(y->flags & (AST_SYMBOL_FLAG_DEFINED | AST_SYMBOL_FLAG_TENTATIVE)) && !local_static && y->name)
				return;
		} else {
			return;
		}
		if (unlikely(codegen_symbol_attributes(c, s)->alias))
			return;	/* a .set in the tail (codegen_unit_emit keeps its target), not storage */
		if (!atomic_exchange(&E->claimed[s], 1))
			codegen_work_enqueue(E, s);
	}
}

VPRIV void codegen_function_prepass_request(codegen_context *e, uint32_t s)
{
	codegen_log_append(e, CODEGEN_LOG_WALK, s);
	codegen_function_walk_ensure(e->E, e->c, s);
}
