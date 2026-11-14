/* Omit an x86 leaf frame only when no emitted operation depends on it.
   Fold the decision into the existing two-address traversal: no extra scan. */
#include "x64-internal.h"

VPRIV bool x64_framefree_candidate(codegen_context *e)
{
	return e->unit->options->omit_frame_pointer && !e->unit->cleanups_disabled && !e->x64_context->interrupt &&
	       !e->walk_state->frame_offset && e->walk_state->frame_alignment <= 16 && !e->walk_state->stack_pointer_needed && !e->has_calls;
}

VPRIV bool x64_framefree_record(codegen_context *e, const record *i)
{
	record_effects f = x64_record_effects(e, i);
	record_register_mask frame = (UINT64_C(1) << RBP) | (UINT64_C(1) << RSP);

	/* Includes frame/return-address builtins, stack arguments, calls, stack
	   adjustments and opaque asm. Unknown opcodes have conservative effects.
	   Two-address expansion only adds moves among these same registers. */
	return !((f.used | f.defines) & frame) && !(f.flags & RECORD_EFFECT_CALL);
}
