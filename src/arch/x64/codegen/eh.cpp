/*
 * eh.cpp: C++ exceptions in the direct emitter (frontend/c/cc.h's eh).  A function with cleanup variables
 * gets an exception table: x64_call brackets every call with marks (.Lvcceh<fn>_<n>) and notes the
 * innermost cleanup node live there; here, before the return label (jumped over), one landing pad per
 * node reached: it keeps the exception (in %rax at the pad) in a slot, then runs the node's cleanup and
 * its enclosing nodes' to the outermost, then _Unwind_Resume(exception).  x64_eh_table (print.c) writes
 * .gcc_except_table, each call a call site, its landing pad that node's (0: none), action 0: a cleanup.
 */
#include "x64-internal.h"

VPRIV void x64_eh_pads(codegen_context *e)
{
    walk_state *W = e->walk_state;
    cc *c = e->compile_context;
    uint32_t n = W->cleanup_node_count, *run, resume;
    uint8_t *need;
    bool any = false;

    if (!W->eh_fn || W->eh_overflow || !c->eh_resume_sym)
        return;
    need = (uint8_t *)calloc(n + 1, 1),
    run = (uint32_t *)calloc(n + 1, sizeof *run);
    for (uint32_t k = 0; k < W->eh_nsites; k++)
        for (uint32_t m = W->eh_sites[k].node; m && m <= n && !need[m];
             m = W->cleanup_nodes[m].parent)
            need[m] = 1, any = true;
    memset(W->eh_pad, 0, (n + 1) * sizeof *W->eh_pad);
    if (!any) {
        free(need), free(run);
        return;
    }
    if (e->is_reachable)
        x64_jump(e, W->return_label);
    W->eh_in_pads = 1, W->eh_term = 0;
    int64_t slot = walk_slot_permanent(e, 8, 8);
    codegen_lvalue sl = {
        .kind = CODEGEN_LVALUE_FRAME, .align = 8, .offset = slot};

    for (uint32_t m = 1; m <= n; m++)
        if (need[m])
            run[m] = walk_label_new(e);
    resume = walk_label_new(e);
    /* the pads: the exception kept, then the node's cleanups */
    for (uint32_t m = 1; m <= n; m++) {
        if (!need[m])
            continue;
        W->eh_pad[m] = ++W->eh_marks;
        e->is_reachable = true;
        x64_record_emit(e, X64_OP_EHMARK)->label = W->eh_pad[m];
        x64_store(e, RAX, &sl, 8);
        x64_jump(e, run[m]);
    }
    /* each node's cleanup, then its parent's (the outermost's: resume) */
    for (uint32_t m = n; m >= 1; m--) {
        if (!need[m])
            continue;
        uint32_t p = W->cleanup_nodes[m].parent;

        walk_label_place(e, run[m]);
        W->eh_in_pads = 2;
        walk_cleanup_call(e, W->cleanup_nodes[m].symbol);
        W->eh_in_pads = 1;
        x64_jump(e, p && p <= n && need[p] ? run[p] : resume);
    }
    walk_label_place(e, resume);
    walk_call_slot_argument(e, c->eh_resume_sym, slot);
    x64_record_emit(e, X64_OP_UD2); /* (_Unwind_Resume does not return) */
    /* the terminate pad, of a cleanup's call that throws: catch it, std::terminate() ([except.terminate]) */
    if (c->eh_begin_catch_sym && c->eh_terminate_sym) {
        W->eh_term = ++W->eh_marks;
        e->is_reachable = true;
        x64_record_emit(e, X64_OP_EHMARK)->label = W->eh_term;
        x64_store(e, RAX, &sl, 8);
        walk_call_slot_argument(e, c->eh_begin_catch_sym, slot);
        {
            walk_argument none;
            codegen_value r;

            memset(&none, 0, sizeof none);
            codegen_reference_note(
                e, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL,
                                          c->eh_terminate_sym));
            em_abi(e, c->syms[c->eh_terminate_sym].ty);
            r = x64_call(e,
                         CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL,
                                                c->eh_terminate_sym),
                         NULL, NULL, 0, false, NULL,
                         codegen_type_primitive(AST_TYPE_VOID));
            walk_value_free(e, &r);
        }
        x64_record_emit(e, X64_OP_UD2);
    }
    e->is_reachable = false;
    W->eh_in_pads = 0;
    free(need), free(run);
}

/* the function's exception table, after its code: .cfi_lsda named it (print.c, with .cfi_personality) */
VPRIV bool x64_eh_wanted(codegen_context *e)
{
    walk_state *W = e->walk_state;

    if (!W || !W->eh_fn || W->eh_overflow)
        return false;
    for (uint32_t k = 0; k < W->eh_nsites; k++)
        if (W->eh_sites[k].node && W->eh_pad[W->eh_sites[k].node])
            return true;
    return false;
}

VPRIV void x64_eh_table(codegen_context *e, tbuf *b, uint32_t s)
{
    walk_state *W = e->walk_state;
    cc *c = e->compile_context;
    uint32_t f = W->function_symbol;

    bool term = W->eh_term && W->eh_seen && W->eh_seen[W->eh_term];

    vtext_string_append(b, "\t.p2align\t2\n");
    if (term) /* (a type table: one entry, 0, catch (...): the terminate pad's handler) */
        vtext_format_append(
            b,
            ".Lvcclsda%u:\n\t.byte\t255\n\t.byte\t155\n\t.uleb128 "
            ".Lvccttb%u-.Lvccttr%u\n.Lvccttr%u:\n\t.byte\t1\n\t.uleb128 "
            ".Lvcccse%u-.Lvcccsb%u\n.Lvcccsb%u:\n",
            f, f, f, f, f, f, f);
    else
        vtext_format_append(
            b,
            ".Lvcclsda%u:\n\t.byte\t255\n\t.byte\t255\n\t.byte\t1\n\t.uleb128 "
            ".Lvcccse%u-.Lvcccsb%u\n.Lvcccsb%u:\n",
            f, f, f, f);
    for (uint32_t k = 0; k < W->eh_nsites; k++) {
        uint32_t m = W->eh_sites[k].node, pad = m == UINT32_MAX
                                                    ? (term ? W->eh_term : 0)
                                                : m ? W->eh_pad[m]
                                                    : 0;
        bool catch_all = m == UINT32_MAX && pad;

        /* (a call a later pass removed: not a call site; a pad that is gone: none) */
        if (!W->eh_seen || !W->eh_seen[W->eh_sites[k].begin] ||
            !W->eh_seen[W->eh_sites[k].end])
            continue;
        if (pad && !W->eh_seen[pad])
            pad = 0;
        vtext_format_append(b, "\t.uleb128 .Lvcceh%u_%u-", f,
                            W->eh_sites[k].begin);
        codegen_symbol_name_append(c, b, s);
        vtext_format_append(b, "\n\t.uleb128 .Lvcceh%u_%u-.Lvcceh%u_%u\n", f,
                            W->eh_sites[k].end, f, W->eh_sites[k].begin);
        if (pad) {
            vtext_format_append(b, "\t.uleb128 .Lvcceh%u_%u-", f, pad);
            codegen_symbol_name_append(c, b, s);
            vtext_char_append(b, '\n');
        } else
            vtext_string_append(b, "\t.byte\t0\n");
        vtext_string_append(
            b,
            catch_all
                ? "\t.byte\t1\n"
                : "\t.byte\t0\n"); /* (action 0: a cleanup; 1: the catch (...) record) */
    }
    vtext_format_append(b, ".Lvcccse%u:\n", f);
    if (term) /* the action record (filter 1: the type table's entry, no next), then the type table */
        vtext_format_append(
            b,
            "\t.byte\t1\n\t.byte\t0\n\t.p2align\t2\n\t.long\t0\n.Lvccttb%u:\n",
            f);
}
