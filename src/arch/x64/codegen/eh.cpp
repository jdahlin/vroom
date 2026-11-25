/*
 * eh.cpp: C++ exceptions in the direct emitter (frontend/c/cc.h's eh).  A function with cleanup variables
 * gets an exception table: x64_call brackets every call with marks (.Lvcceh<fn>_<n>) and notes the
 * innermost cleanup node live there; here, before the return label (jumped over), one landing pad per
 * node reached: it keeps the exception (in %rax at the pad) in a slot, then runs the node's cleanup and
 * its enclosing nodes' to the outermost, then _Unwind_Resume(exception).  x64_eh_table (print.c) writes
 * .gcc_except_table, each call a call site, its landing pad that node's (0: none), action 0: a cleanup.
 */
#include "x64-internal.h"

/* a catch scope's dispatch at a landing pad: the exception pointer into the scope's variable, then
   the handlers in order, the selector against the type table; falls out of it when none matches */
static void x64_eh_dispatch(codegen_context *e, uint32_t scope, int64_t exc,
                            int64_t selector)
{
    cc *c = e->compile_context;
    walk_state *W = e->walk_state;
    uint32_t n = 0;
    const uint32_t *h = codegen_catch_scope(c, scope, &n);
    codegen_lvalue
        ex = {.kind = CODEGEN_LVALUE_FRAME, .align = 8, .offset = exc},
        sl = {.kind = CODEGEN_LVALUE_FRAME, .align = 8, .offset = selector},
        var = {
            .kind = CODEGEN_LVALUE_FRAME,
            .align = 8,
            .offset = walk_local_slot(
                e, c->lists[codegen_symbol_attributes(c, scope)->catch_list])};
    int r = x64_treg(e);

    x64_load(e, r, &ex, 8, false);
    x64_store(e, r, &var, 8);
    x64_tfree(e, r);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t target = walk_user_label_find(e, h[2 * i + 1]);

        if (!h[2 * i]) {
            x64_jump(e, target);
            return;
        }
        uint32_t idx = codegen_eh_type_index(e, &W->eh_lsda, h[2 * i]);

        r = x64_treg(e);
        x64_load(e, r, &sl, 4, false);
        x64_cmpbr(e, LEX_TOKEN_EQUAL_EQUAL, 4, false,
                  VCC_LITERAL(codegen_value, .kind = CODEGEN_VALUE_REGISTER,
                              .register_number = (uint8_t)r),
                  codegen_value_constant(idx), target);
        x64_tfree(e, r);
    }
}

VPRIV void x64_eh_pads(codegen_context *e)
{
    walk_state *W = e->walk_state;
    cc *c = e->compile_context;
    uint32_t n = W->cleanup_node_count, *run, resume;
    uint8_t *need;
    bool any = false, catches = false;

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
    int64_t slot = walk_slot_permanent(e, 8, 8), selector = 0;
    codegen_lvalue sl = {
        .kind = CODEGEN_LVALUE_FRAME, .align = 8, .offset = slot};

    for (uint32_t m = 1; m <= n; m++) {
        uint32_t count;

        if (need[m] &&
            codegen_catch_scope(c, W->cleanup_nodes[m].symbol, &count))
            catches = true;
    }
    if (catches)
        selector = walk_slot_permanent(e, 8, 8);
    codegen_lvalue ss = {
        .kind = CODEGEN_LVALUE_FRAME, .align = 8, .offset = selector};

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
        if (catches)
            x64_store(e, RDX, &ss, 8);
        x64_jump(e, run[m]);
    }
    /* each node's cleanup, then its parent's (the outermost's: resume) */
    for (uint32_t m = n; m >= 1; m--) {
        if (!need[m])
            continue;
        uint32_t p = W->cleanup_nodes[m].parent;

        uint32_t count;

        walk_label_place(e, run[m]);
        if (codegen_catch_scope(c, W->cleanup_nodes[m].symbol, &count)) {
            x64_eh_dispatch(e, W->cleanup_nodes[m].symbol, slot, selector);
        } else {
            W->eh_in_pads = 2;
            walk_cleanup_call(e, W->cleanup_nodes[m].symbol);
            W->eh_in_pads = 1;
        }
        x64_jump(e, p && p <= n && need[p] ? run[p] : resume);
    }
    walk_label_place(e, resume);
    walk_call_slot_argument(e, c->eh_resume_sym, slot);
    x64_record_emit(e, X64_OP_UD2); /* (_Unwind_Resume does not return) */
    /* the terminate pad, of a cleanup's call that throws: catch it, std::terminate() ([except.terminate]);
       C: abort(), without __cxa_begin_catch (as clang) */
    if (c->eh_terminate_sym) {
        W->eh_term = ++W->eh_marks;
        e->is_reachable = true;
        x64_record_emit(e, X64_OP_EHMARK)->label = W->eh_term;
        x64_store(e, RAX, &sl, 8);
        if (c->eh_begin_catch_sym)
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
    for (uint32_t k = 0; k < W->eh_nsites; k++) {
        uint32_t m = W->eh_sites[k].node;

        if (m ==
            UINT32_MAX) { /* (a call in a landing pad: the terminate pad) */
            if (W->eh_term)
                return true;
        } else if (m && W->eh_pad[m])
            return true;
    }
    return false;
}

/* a call site's action: its node's chain of catch types, a cleanup flag */
static uint32_t x64_eh_site_action(codegen_context *e, uint32_t node)
{
    walk_state *W = e->walk_state;
    cc *c = e->compile_context;
    codegen_eh_chain k;

    memset(&k, 0, sizeof k);
    if (W->eh_vir)
        return W->eh_action[node];
    if (node == UINT32_MAX) { /* (the terminate pad: catch (...)) */
        k.types[k.count++] = 0;
        k.closed = true;
    } else
        for (uint32_t m = node; m; m = W->cleanup_nodes[m].parent)
            codegen_eh_chain_node(&k, c, W->cleanup_nodes[m].symbol);
    return codegen_eh_action(e, &W->eh_lsda, &k);
}

VPRIV void x64_eh_table(codegen_context *e, tbuf *b, uint32_t s)
{
    walk_state *W = e->walk_state;
    cc *c = e->compile_context;
    uint32_t f = W->function_symbol;
    uint32_t action[WALK_EH_SITES], pads[WALK_EH_SITES];
    bool term = W->eh_term && W->eh_seen && W->eh_seen[W->eh_term];

    for (uint32_t k = 0; k < W->eh_nsites; k++) {
        uint32_t m = W->eh_sites[k].node, pad = m == UINT32_MAX
                                                    ? (term ? W->eh_term : 0)
                                                : m ? W->eh_pad[m]
                                                    : 0;

        /* (a call a later pass removed: not a call site; a pad that is gone: none) */
        if (!W->eh_seen || !W->eh_seen[W->eh_sites[k].begin] ||
            !W->eh_seen[W->eh_sites[k].end])
            pad = 0, m = 0;
        if (pad && !W->eh_seen[pad])
            pad = 0;
        pads[k] = pad;
        action[k] = pad ? x64_eh_site_action(e, m) : 0;
    }
    codegen_eh_table_begin(b, &W->eh_lsda, f);
    /* every address of the function is in the table: C++'s personality
       routine terminates at a call in none.  Between the sites listed, one
       without a pad (a throw outside every try, which the VIR lowering
       gives no site) */
    uint32_t after = 0;
    auto gap = [&](uint32_t until) {
        if (after)
            vtext_format_append(b, "\t.uleb128 .Lvcceh%u_%u-", f, after);
        else
            vtext_string_append(b, "\t.uleb128 0\n\t.uleb128 ");
        if (after) {
            codegen_symbol_name_append(c, b, s);
            vtext_string_append(b, "\n\t.uleb128 ");
        }
        if (until)
            vtext_format_append(b, ".Lvcceh%u_%u-", f, until);
        else
            vtext_format_append(b, ".Lfunc_end%u-", s);
        if (after)
            vtext_format_append(b, ".Lvcceh%u_%u", f, after);
        else
            codegen_symbol_name_append(c, b, s);
        vtext_string_append(b, "\n\t.byte\t0\n\t.uleb128 0\n");
    };
    for (uint32_t k = 0; k < W->eh_nsites; k++) {
        /* (a call a later pass removed: not a call site) */
        if (!W->eh_seen || !W->eh_seen[W->eh_sites[k].begin] ||
            !W->eh_seen[W->eh_sites[k].end])
            continue;
        gap(W->eh_sites[k].begin);
        after = W->eh_sites[k].end;
        vtext_format_append(b, "\t.uleb128 .Lvcceh%u_%u-", f,
                            W->eh_sites[k].begin);
        codegen_symbol_name_append(c, b, s);
        vtext_format_append(b, "\n\t.uleb128 .Lvcceh%u_%u-.Lvcceh%u_%u\n", f,
                            W->eh_sites[k].end, f, W->eh_sites[k].begin);
        if (pads[k]) {
            vtext_format_append(b, "\t.uleb128 .Lvcceh%u_%u-", f, pads[k]);
            codegen_symbol_name_append(c, b, s);
            vtext_char_append(b, '\n');
        } else
            vtext_string_append(b, "\t.byte\t0\n");
        vtext_format_append(b, "\t.uleb128 %u\n", action[k]);
    }
    gap(0);
    codegen_eh_table_end(b, c, &W->eh_lsda, f);
}
