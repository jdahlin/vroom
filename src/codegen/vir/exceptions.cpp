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

/* the variable a node's pad works on: a cleanup variable, or a catch scope's exception variable */
static uint32_t virbuild_eh_node_variable(codegen_context *e, uint32_t node)
{
    cc *c = e->compile_context;
    uint32_t s = e->walk_state->cleanup_nodes[node].symbol;

    if (codegen_symbol_attributes(c, s)->catch_count)
        return c->lists[codegen_symbol_attributes(c, s)->catch_list];
    return s;
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
            e, walk_local_slot(e, virbuild_eh_node_variable(e, m)), 1, &rel);

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

/* in a landing pad (the current block): its landingpad with the clauses of chain k (the catch types, a cleanup
   flag), the exception, and with catch scopes the selector in its variable.  Returns the exception. */
static uint32_t virbuild_eh_landingpad(codegen_context *e,
                                       const codegen_eh_chain *k, bool selector)
{
    uint32_t clauses[2 + CODEGEN_EH_CHAIN_TYPES];

    clauses[0] = k->cleanup;
    clauses[1] = k->count;
    for (uint32_t i = 0; i < k->count; i++)
        clauses[2 + i] = k->types[i]
                             ? CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL,
                                                      k->types[i]) +
                                   1
                             : 0;
    uint32_t list = virbuild_extra_append(e, clauses, 2 + k->count);
    uint32_t lp = virbuild_insn_append(e, VIR_OP_LANDINGPAD, VIR_TYPE_MULTI, 0,
                                       list, 0, 0);

    if (selector)
        virbuild_variable_write(
            e, VIRBUILD_SELECTOR_VARIABLE, e->vir_builder->current_block,
            virbuild_insn_append(e, VIR_OP_PROJ, VIR_TYPE_I32, 0, lp, 1, 0));
    return virbuild_insn_append(e, VIR_OP_PROJ, VIR_TYPE_PTR, 0, lp, 0, 0);
}

/* the clauses of the pad of walker node m: every catch type of the scopes around it (to the first catch (...)), a
   cleanup flag when a cleanup runs on the way */
static void virbuild_eh_chain(codegen_context *e, uint32_t m,
                              codegen_eh_chain *k)
{
    walk_state *W = e->walk_state;

    memset(k, 0, sizeof *k);
    for (; m; m = W->cleanup_nodes[m].parent)
        codegen_eh_chain_node(k, e->compile_context,
                              W->cleanup_nodes[m].symbol);
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
    bool any = false, catches = false;

    if (!V->eh_used)
        return;
    if (n >= VIRBUILD_EH_NODES)
        virbuild_fail(e, VIRBUILD_FAILURE(exceptions));
    memset(run, 0, (n + 1) * sizeof *run);
    for (uint32_t m = 1; m <= n; m++)
        if (V->eh_pad_blocks[m])
            for (uint32_t k = m; k && k <= n && !run[k];
                 k = W->cleanup_nodes[k].parent) {
                uint32_t count;
                const uint32_t *h =
                    codegen_catch_scope(c, W->cleanup_nodes[k].symbol, &count);
                bool closes = false;

                run[k] = virbuild_block_new(e), any = true;
                if (h)
                    catches = true;
                for (uint32_t i = 0; h && i < count; i++)
                    if (!h[2 * i])
                        closes =
                            true; /* (a catch (...): nothing outside it is reached) */
                if (closes)
                    break;
            }
    if (e->is_reachable)
        virbuild_jump(e, W->return_label);
    W->eh_in_pads = 1;
    resume = virbuild_block_new(e);
    /* the pads: the exception kept, then the node's cleanups */
    for (uint32_t m = 1; m <= n; m++) {
        if (!V->eh_pad_blocks[m])
            continue;
        codegen_eh_chain chain;

        V->current_block = V->eh_pad_blocks[m];
        V->store_count = 0;
        e->is_reachable = true;
        virbuild_eh_chain(e, m, &chain);
        virbuild_variable_write(e, VIRBUILD_EXCEPTION_VARIABLE,
                                V->current_block,
                                virbuild_eh_landingpad(e, &chain, catches));
        virbuild_branch_emit(e, run[m]);
    }
    /* each node's cleanup, then its parent's (the outermost's: resume) */
    for (uint32_t m = n; any && m >= 1; m--) {
        if (!run[m])
            continue;
        uint32_t p = W->cleanup_nodes[m].parent, next = resume, count = 0;
        const uint32_t *h =
            codegen_catch_scope(c, W->cleanup_nodes[m].symbol, &count);

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

        if (h) { /* a catch scope: the exception to its variable, then each handler whose type the selector names */
            codegen_lvalue var = {
                .kind = CODEGEN_LVALUE_FRAME,
                .align = 8,
                .offset = walk_local_slot(e, virbuild_eh_node_variable(e, m))};
            int r = virbuild_treg(e);
            bool all = false;

            virbuild_register_write(
                e, r,
                virbuild_variable_read(e, VIRBUILD_EXCEPTION_VARIABLE, run[m],
                                       VIR_TYPE_PTR));
            virbuild_store(e, r, &var, 8);
            virbuild_tfree(e, r);
            for (uint32_t i = 0; i < count && !all; i++) {
                uint32_t target = walk_user_label_find(e, h[2 * i + 1]);

                if (!h[2 * i]) {
                    virbuild_jump(e, target);
                    all = true;
                    break;
                }
                uint32_t id = virbuild_insn_append(
                    e, VIR_OP_EHTYPEID, VIR_TYPE_I32, 0,
                    CODEGEN_REFERENCE_MAKE(
                        CODEGEN_REFERENCE_SYMBOL,
                        codegen_symbol_canonical(c, h[2 * i])),
                    0, 0);
                uint32_t sel = virbuild_value_convert(
                    e,
                    virbuild_variable_read(e, VIRBUILD_SELECTOR_VARIABLE,
                                           V->current_block, VIR_TYPE_I32),
                    VIR_TYPE_I32);
                uint32_t cond =
                    virbuild_insn_append(e, VIR_OP_ICMP, VIR_TYPE_I32,
                                         virbuild_integer_condition_code(
                                             LEX_TOKEN_EQUAL_EQUAL, false),
                                         sel, id, 0);

                virbuild_branch_if(e, cond, target, true);
            }
            if (V->active_slot_count == depth)
                V->active_slot_count--;
            if (!all)
                virbuild_branch_emit(e, next);
            continue;
        }
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
        codegen_eh_chain all;

        memset(&all, 0, sizeof all);
        all.types[all.count++] = 0;
        all.closed = true;
        uint32_t x = virbuild_eh_landingpad(e, &all, catches);

        if (c->eh_begin_catch_sym)
            virbuild_eh_runtime_call(e, c->eh_begin_catch_sym, x);
        virbuild_eh_runtime_call(e, c->eh_terminate_sym, 0);
        virbuild_block_terminate(e, VIR_OP_TRAP, 0, 0, 0, 0);
    }
    e->is_reachable = false;
    W->eh_in_pads = 0;
}
