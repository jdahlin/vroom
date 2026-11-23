/*
 * Exceptions in the VIR builder (SPEC-vir.md 8.8, doc/PLAN-vir-eh.md E5).  In a function where the
 * walker keeps an exception table (walk.h eh_fn: cleanup variables, -fexceptions), a call made while
 * cleanups are live ends its block with an invoke: the normal block continues the walk, the unwind
 * edge goes to the landing pad of the walker's cleanup node live there (walk_eh_call_node), made on
 * first use.  At the end of the body, virbuild_eh_pads builds those pads as x86-64's direct emitter
 * does (arch/x64/codegen/eh.cpp): the landingpad keeps the exception in a variable, each node's
 * cleanup runs and then its parent's, to _Unwind_Resume (resume).  A cleanup's call there unwinds
 * to the terminate pad (catch-all: C++'s __cxa_begin_catch and std::terminate, C's abort).
 *
 * The pads run after the cleanup variables' scopes have ended, when the builder's slot for a
 * variable's frame offset may be another object's (X2): each node keeps the slot its variable had
 * when the node's first call was built, and a pad's cleanup finds that one.
 */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "preir.h"

/* at the start of a function's build: nothing kept from the last one */
VPRIV void virbuild_eh_function_begin(codegen_context *e)
{
    virbuild_context *V =
        virbuild_context_get(e); /* (made on a thread's first function) */

    if (V->eh_used) {
        memset(V->eh_pad_blocks, 0, sizeof V->eh_pad_blocks);
        memset(V->eh_node_slots, 0, sizeof V->eh_node_slots);
    }
    V->eh_terminate_block = 0;
    V->eh_used = false;
}

/* a cleanup node's variables: their slots, now, while they are in scope */
static void virbuild_eh_node_slots_note(codegen_context *e, uint32_t node)
{
    virbuild_context *V = e->vir_builder;
    walk_state *W = e->walk_state;

    for (uint32_t m = node; m && m < VIRBUILD_EH_NODES && !V->eh_node_slots[m];
         m = W->cleanup_nodes[m].parent) {
        int64_t rel;
        uint32_t s = virbuild_slot_find(
            e, walk_local_slot(e, W->cleanup_nodes[m].symbol), 1, &rel);

        V->eh_node_slots[m] = s + 1;
    }
}

/* the block a call made now unwinds to (VIRBUILD_BLOCK_NONE: it is a plain call) */
VPRIV uint32_t virbuild_eh_unwind_block(codegen_context *e)
{
    virbuild_context *V = e->vir_builder;
    uint32_t node = walk_eh_call_node(e);

    if (!node)
        return VIRBUILD_BLOCK_NONE;
    V->eh_used = true;
    if (node == UINT32_MAX) {
        if (!V->eh_terminate_block) {
            V->eh_terminate_block = virbuild_block_new(e);
            V->blocks[V->eh_terminate_block].is_landing_pad = 1;
        }
        return V->eh_terminate_block;
    }
    if (node >= VIRBUILD_EH_NODES)
        virbuild_fail(e, VIRBUILD_FAILURE(exceptions));
    if (!V->eh_pad_blocks[node]) {
        V->eh_pad_blocks[node] = virbuild_block_new(e);
        V->blocks[V->eh_pad_blocks[node]].is_landing_pad = 1;
    }
    virbuild_eh_node_slots_note(e, node);
    return V->eh_pad_blocks[node];
}

/* in a landing pad (the current block): its landingpad (clause: 0 cleanup, else catch-all), the exception */
static uint32_t virbuild_eh_landingpad(codegen_context *e, bool catch_all)
{
    uint32_t clauses[3] = {1, 0, 0};

    if (catch_all)
        clauses[0] = 0, clauses[1] = 1;
    uint32_t list = virbuild_extra_append(e, clauses, 2 + clauses[1]);
    uint32_t lp = virbuild_insn_append(e, VIR_OP_LANDINGPAD, VIR_TYPE_MULTI, 0,
                                       list, 0, 0);

    return virbuild_insn_append(e, VIR_OP_PROJ, VIR_TYPE_PTR, 0, lp, 0, 0);
}

static void virbuild_eh_runtime_call(codegen_context *e, uint32_t fn,
                                     uint32_t exception)
{
    uint32_t ref = CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, fn);

    codegen_reference_note(e, ref);
    if (exception) { /* fn(exception), through a slot as the walker passes arguments */
        int64_t slot = walk_slot_permanent(e, 8, 8);
        codegen_lvalue l = {
            .kind = CODEGEN_LVALUE_FRAME, .align = 8, .offset = slot};
        int r = virbuild_treg(e);

        virbuild_register_write(e, r, exception);
        virbuild_store(e, r, &l, 8);
        virbuild_tfree(e, r);
        walk_call_slot_argument(e, fn, slot);
        return;
    }
    codegen_value v = virbuild_call(e, ref, NULL, NULL, 0, false, NULL,
                                    codegen_type_primitive(AST_TYPE_VOID));
    (void)v;
}

/* the landing pads, at the end of the body (in front of the return label, which the body jumps to) */
VPRIV void virbuild_eh_pads(codegen_context *e)
{
    virbuild_context *V = e->vir_builder;
    walk_state *W = e->walk_state;
    cc *c = e->compile_context;
    uint32_t n = W->cleanup_node_count, run[VIRBUILD_EH_NODES], resume;
    bool any = false;

    if (!V->eh_used)
        return;
    if (n >= VIRBUILD_EH_NODES)
        virbuild_fail(e, VIRBUILD_FAILURE(exceptions));
    memset(run, 0, (n + 1) * sizeof *run);
    for (uint32_t m = 1; m <= n; m++)
        if (V->eh_pad_blocks[m])
            for (uint32_t k = m; k && k <= n && !run[k];
                 k = W->cleanup_nodes[k].parent)
                run[k] = virbuild_block_new(e), any = true;
    if (e->is_reachable)
        virbuild_jump(e, W->return_label);
    W->eh_in_pads = 1;
    resume = virbuild_block_new(e);
    /* the pads: the exception kept, then the node's cleanups */
    for (uint32_t m = 1; m <= n; m++) {
        if (!V->eh_pad_blocks[m])
            continue;
        V->current_block = V->eh_pad_blocks[m];
        V->store_count = 0;
        e->is_reachable = true;
        virbuild_variable_write(e, VIRBUILD_EXCEPTION_VARIABLE,
                                V->current_block,
                                virbuild_eh_landingpad(e, false));
        virbuild_branch_emit(e, run[m]);
    }
    /* each node's cleanup, then its parent's (the outermost's: resume) */
    for (uint32_t m = n; any && m >= 1; m--) {
        if (!run[m])
            continue;
        uint32_t p = W->cleanup_nodes[m].parent, next = resume;

        if (p && p <= n && run[p])
            next = run[p];
        V->current_block = run[m];
        V->store_count = 0;
        e->is_reachable = true;
        /* the variable's own slot, whatever owns its offset now */
        VIRBUILD_ARRAY_GROW(V->active_slots, V->active_slot_count,
                            V->active_slot_capacity, 1);
        V->active_slots[V->active_slot_count++] = V->eh_node_slots[m] - 1;
        uint32_t depth = V->active_slot_count;

        W->eh_in_pads = 2;
        walk_cleanup_call(e, W->cleanup_nodes[m].symbol);
        W->eh_in_pads = 1;
        if (V->active_slot_count == depth)
            V->active_slot_count--;
        virbuild_branch_emit(e, next);
    }
    V->current_block = resume;
    V->store_count = 0;
    e->is_reachable = true;
    virbuild_block_terminate(e, VIR_OP_RESUME,
                             virbuild_variable_read(e,
                                                    VIRBUILD_EXCEPTION_VARIABLE,
                                                    resume, VIR_TYPE_PTR),
                             0, 0, 0);
    /* the terminate pad, of a cleanup's call that throws ([except.terminate]) */
    if (V->eh_terminate_block) {
        V->current_block = V->eh_terminate_block;
        V->store_count = 0;
        e->is_reachable = true;
        uint32_t x = virbuild_eh_landingpad(e, true);

        if (c->eh_begin_catch_sym)
            virbuild_eh_runtime_call(e, c->eh_begin_catch_sym, x);
        virbuild_eh_runtime_call(e, c->eh_terminate_sym, 0);
        virbuild_block_terminate(e, VIR_OP_TRAP, 0, 0, 0, 0);
    }
    e->is_reachable = false;
    W->eh_in_pads = 0;
}
