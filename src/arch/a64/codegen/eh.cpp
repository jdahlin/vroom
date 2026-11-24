/*
 * eh.cpp: exceptions in arm64's direct emitter (frontend/c/cc.h's eh; doc/PLAN-vir-eh.md E2).  In a
 * function where cleanup variables are live at a call, the call is bracketed by two labels and
 * notes the cleanups live there, as a node of a tree (the innermost variable and the node it was
 * declared in).  At the end of the body, one landing pad per node reached: it keeps the exception
 * (x0 at the pad) in a slot, runs the node's cleanup and its parents' to the outermost, then
 * _Unwind_Resume(exception).  A call in a pad (a cleanup that throws) goes to the terminate pad
 * (C++: __cxa_begin_catch, std::terminate; C: abort, as clang).  a64_eh_table writes
 * .gcc_except_table after the function (func.cpp), the encodings GCC uses on arm64 (pc-relative
 * sdata4, the personality indirect: .Lvcc_personality, codegen/unit-emit.cpp).
 * The labels are external (CODEGEN_LABEL_EXTERNAL): the record passes keep them, and a pad is
 * entered from outside the function's branches.
 */
#include "../../../codegen/codegen-internal.h"

/* a function's state, at its start (emit_function1, and again on a retry) */
VPRIV void a64_eh_function_begin(codegen_context *e)
{
    cc *c = e->compile_context;

    e->eh_site_count = e->eh_node_count = e->eh_terminate_pad = 0;
    e->eh_cleanup_limit = INT32_MAX;
    e->eh_in_pads = e->eh_cover = false;
    e->eh_function = e->unit->options->eh && e->unit->options->cfi &&
                     !CODEGEN_IS_DARWIN(c) && c->eh_resume_sym;
    /* (now: a slot taken later is given back at the end of its scope, before the pads) */
    memset(&e->eh_lsda, 0, sizeof e->eh_lsda);
    if (e->eh_function)
        e->eh_slot = a64_frame_slot_allocate(e, 8, 8),
        e->eh_selector_slot = a64_frame_slot_allocate(e, 8, 8);
}

/* the variable runs a cleanup function an exception must run too (not a VLA's stack restore) */
static bool a64_eh_cleanup_variable(cc *c, uint32_t s)
{
    return !(T(c, c->syms[s].ty)->flags & AST_TYPE_FLAG_VLA) &&
           codegen_symbol_attributes(c, s)->cleanup;
}

/* the node of the cleanups live now (0: none) */
static uint32_t a64_eh_node_current(codegen_context *e)
{
    cc *c = e->compile_context;
    uint32_t node = 0;
    int n = e->cleanup_count;

    if (e->eh_cleanup_limit < n)
        n = e->eh_cleanup_limit;
    for (int i = 0; i < n; i++) {
        uint32_t s = e->cleanups[i], k;

        if (!a64_eh_cleanup_variable(c, s))
            continue;
        for (k = e->eh_node_count; k > 0; k--)
            if (e->eh_nodes[k].symbol == s && e->eh_nodes[k].parent == node)
                break;
        if (!k) {
            if (e->eh_node_count + 1 >= e->eh_node_capacity)
                codegen_fatal_report(e, 0,
                                     "too many cleanup scopes for the "
                                     "exception table");
            k = ++e->eh_node_count;
            e->eh_nodes[k].symbol = s, e->eh_nodes[k].parent = node,
            e->eh_nodes[k].pad = 0, e->eh_nodes[k].catch_all = false;
        }
        node = k;
    }
    return node;
}

static uint32_t a64_eh_label_place(codegen_context *e)
{
    uint32_t l = a64_label_new(e);

    e->label_reference_counts[l] |= CODEGEN_LABEL_EXTERNAL;
    a64_record_emit(e, A64_OP_LABEL)->label = l;
    return l;
}

/* in front of a call's bl/blr: its site (index + 1), 0 when it needs none */
VPRIV uint32_t a64_eh_call_begin(codegen_context *e)
{
    uint32_t node;

    if (!e->eh_function || e->dry_run_depth || !e->is_reachable)
        return 0;
    if (e->eh_in_pads)
        node = UINT32_MAX;
    else
        node = a64_eh_node_current(e);
    /* (a call that unwinds nowhere is a call site without a pad where the personality routine of C++
       looks it up: _Unwind_Resume's, whose frame it enters again; an address in no call site terminates) */
    if (!node && !e->eh_cover)
        return 0;
    if (e->eh_site_count >= e->label_capacity / 2)
        codegen_fatal_report(e, 0, "too many calls for the exception table");
    struct codegen_eh_site *t = &e->eh_sites[e->eh_site_count++];

    t->node = node;
    t->begin = a64_eh_label_place(e);
    t->end = 0;
    return e->eh_site_count;
}

/* VIR's lowering: the node standing for the landing pad at label (made on first use) */
VPRIV uint32_t a64_eh_pad_node(codegen_context *e, uint32_t label,
                               bool catch_all)
{
    for (uint32_t k = 1; k <= e->eh_node_count; k++)
        if (e->eh_nodes[k].pad == label)
            return k;
    if (e->eh_node_count + 1 >= e->eh_node_capacity)
        codegen_fatal_report(e, 0, "too many landing pads");
    uint32_t k = ++e->eh_node_count;

    e->eh_nodes[k] =
        VCC_LITERAL(struct codegen_eh_node, .symbol = 0, .parent = 0,
                    .pad = label, .catch_all = catch_all);
    return k;
}

/* VIR's lowering: in front of an invoke's bl/blr, unwinding to node's pad */
VPRIV uint32_t a64_eh_site_begin(codegen_context *e, uint32_t node)
{
    if (e->eh_site_count >= e->label_capacity / 2)
        codegen_fatal_report(e, 0, "too many calls for the exception table");
    struct codegen_eh_site *t = &e->eh_sites[e->eh_site_count++];

    t->node = node;
    t->begin = a64_eh_label_place(e);
    t->end = 0;
    return e->eh_site_count;
}

/* after it */
VPRIV void a64_eh_call_end(codegen_context *e, uint32_t site)
{
    if (site)
        e->eh_sites[site - 1].end = a64_eh_label_place(e);
}

/* cleanup k on the normal path: the calls it makes are covered by the cleanups below it only */
VPRIV int a64_eh_cleanup_limit_set(codegen_context *e, int k)
{
    int old = e->eh_cleanup_limit;

    e->eh_cleanup_limit = k;
    return old;
}

/* fn(the exception) or fn() */
static void a64_eh_call_slot(codegen_context *e, uint32_t fn, bool argument,
                             bool noreturn)
{
    cc *c = e->compile_context;
    a64_call_argument a;
    int n = 0;

    memset(&a, 0, sizeof a);
    if (argument) {
        int r = treg(e);

        a64_record_emit_memory(e, A64_OP_LD, 3, 0, r, A64_REGISTER_FRAME,
                               e->eh_slot);
        a.value = codegen_value_register(r);
        a.type = codegen_type_primitive(AST_TYPE_LONG);
        a.slot = 1;
        n = 1;
    }
    codegen_value v =
        do_call(e, CODEGEN_REFERENCE_MAKE(CODEGEN_REFERENCE_SYMBOL, fn), NULL,
                &a, n, codegen_type_primitive(AST_TYPE_VOID), noreturn, -1);

    val_free(e, &v);
    (void)c;
}

/* a catch scope's dispatch at a landing pad: the exception pointer into the scope's variable, then the
   handlers in order, the selector against the type table; falls out of it when none matches */
static void a64_eh_dispatch(codegen_context *e, uint32_t scope)
{
    cc *c = e->compile_context;
    uint32_t n = 0;
    const uint32_t *h = codegen_catch_scope(c, scope, &n);
    codegen_lvalue var = local_lval(
        e, c->lists[codegen_symbol_attributes(c, scope)->catch_list]);
    int r = treg(e);

    a64_record_emit_memory(e, A64_OP_LD, 3, 0, r, A64_REGISTER_FRAME,
                           e->eh_slot);
    a64_record_emit_memory(e, A64_OP_ST, 3, 0, r, A64_REGISTER_FRAME,
                           var.offset);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t target = user_label(e, h[2 * i + 1]);

        if (!h[2 * i]) {
            a64_jump_emit(e, target);
            tfree(e, r);
            return;
        }
        uint32_t idx = codegen_eh_type_index(e, &e->eh_lsda, h[2 * i]);

        a64_record_emit_memory(e, A64_OP_LD, 2, 0, r, A64_REGISTER_FRAME,
                               e->eh_selector_slot);
        a64_record_emit_immediate(e, A64_OP_SUBSI, 0, ZR, r, (int64_t)idx);
        a64_branch_condition_emit(e, A64_CONDITION_EQ, target);
    }
    tfree(e, r);
}

/* the landing pads, at the end of the body (in front of the return label, jumped over) */
VPRIV void a64_eh_pads(codegen_context *e)
{
    cc *c = e->compile_context;
    uint32_t n = e->eh_node_count, resume;
    bool any = false;

    if (!e->eh_function || !n)
        return;
    for (uint32_t k = 0; k < e->eh_site_count; k++)
        for (uint32_t m = e->eh_sites[k].node;
             m && m <= n && !e->eh_nodes[m].pad; m = e->eh_nodes[m].parent)
            e->eh_nodes[m].pad = 1, any = true; /* (needed: its label below) */
    if (!any)
        return;
    uint32_t return_label = e->function_instance->return_label;

    if (e->is_reachable)
        a64_jump_emit(e, return_label);
    e->eh_in_pads = true;
    e->known_values_valid = 0;
    /* the pads read the cleanup variables, whose scopes have ended: their temporaries go above
       every slot the body used */
    e->frame_offset = e->frame_maximum;
    int64_t slot = e->eh_slot;
    uint32_t *run = (uint32_t *)calloc(n + 1, sizeof *run);

    for (uint32_t m = 1; m <= n; m++)
        if (e->eh_nodes[m].pad)
            run[m] = a64_label_new(e);
    resume = a64_label_new(e);
    /* the pads: the exception kept, then the node's cleanups */
    for (uint32_t m = 1; m <= n; m++) {
        if (!e->eh_nodes[m].pad)
            continue;
        e->is_reachable = true;
        e->eh_nodes[m].pad = a64_eh_label_place(e);
        a64_record_emit_memory(e, A64_OP_ST, 3, 0, 0, A64_REGISTER_FRAME, slot);
        a64_record_emit_memory(e, A64_OP_ST, 3, 0, 1, A64_REGISTER_FRAME,
                               e->eh_selector_slot);
        a64_jump_emit(e, run[m]);
    }
    /* each node's cleanup, then its parent's (the outermost's: resume) */
    for (uint32_t m = n; m >= 1; m--) {
        if (!e->eh_nodes[m].pad)
            continue;
        uint32_t p = e->eh_nodes[m].parent, next = resume;

        if (p && p <= n && e->eh_nodes[p].pad)
            next = run[p];
        e->label_reference_counts[run[m]]++;
        deflabel(e, run[m]);
        {
            uint32_t count;

            if (codegen_catch_scope(c, e->eh_nodes[m].symbol, &count))
                a64_eh_dispatch(e, e->eh_nodes[m].symbol);
            else
                a64_cleanup_run(e, e->eh_nodes[m].symbol);
        }
        a64_jump_emit(e, next);
    }
    e->label_reference_counts[resume]++;
    deflabel(e, resume);
    e->eh_in_pads =
        false; /* (as clang: _Unwind_Resume and the terminate pad's calls have no pad) */
    e->eh_cover = true;
    a64_eh_call_slot(e, c->eh_resume_sym, true, true);
    a64_record_emit(e, A64_OP_BRK); /* (_Unwind_Resume does not return) */
    /* the terminate pad, of a cleanup's call that throws ([except.terminate]) */
    if (c->eh_terminate_sym) {
        e->is_reachable = true;
        e->eh_terminate_pad = a64_eh_label_place(e);
        a64_record_emit_memory(e, A64_OP_ST, 3, 0, 0, A64_REGISTER_FRAME, slot);
        if (c->eh_begin_catch_sym)
            a64_eh_call_slot(e, c->eh_begin_catch_sym, true, false);
        a64_eh_call_slot(e, c->eh_terminate_sym, false, true);
        a64_record_emit(e, A64_OP_BRK);
    }
    e->is_reachable = false;
    e->eh_in_pads = false;
    e->eh_cover = false;
    free(run);
}

/* the labels the records still have (a pass may have removed a call, or code it found dead) */
static uint8_t *a64_eh_labels_present(const codegen_context *e)
{
    uint8_t *seen = (uint8_t *)calloc(e->label_count + 1, 1);

    for (uint32_t k = 0; k < e->record_count; k++)
        if (e->records[k].opcode == A64_OP_LABEL &&
            e->records[k].label <= e->label_count)
            seen[e->records[k].label] = 1;
    return seen;
}

/* the function has a table: some call has a pad (func.cpp's .cfi_personality, .cfi_lsda) */
VPRIV bool a64_eh_wanted(const codegen_context *e)
{
    bool any = false;

    if (!e->eh_function || !e->eh_site_count)
        return false;
    uint8_t *seen = a64_eh_labels_present(e);

    for (uint32_t k = 0; k < e->eh_site_count && !any; k++) {
        const struct codegen_eh_site *t = &e->eh_sites[k];
        uint32_t pad = e->eh_terminate_pad;

        if (t->node != UINT32_MAX)
            pad = e->eh_nodes[t->node].pad;
        any = pad && seen[pad] && seen[t->begin] && t->end && seen[t->end];
    }
    free(seen);
    return any;
}

/* the function's exception table, after its code: .cfi_lsda named it */
VPRIV void a64_eh_table(codegen_context *e, tbuf *b, uint32_t s)
{
    cc *c = e->compile_context;
    uint8_t *seen = a64_eh_labels_present(e);
    bool term = e->eh_terminate_pad && seen[e->eh_terminate_pad];
    uint32_t *action = (uint32_t *)calloc(e->eh_site_count + 1, sizeof *action),
             *pads = (uint32_t *)calloc(e->eh_site_count + 1, sizeof *pads);

    for (uint32_t k = 0; k < e->eh_site_count; k++) {
        const struct codegen_eh_site *t = &e->eh_sites[k];
        bool in_pad = t->node == UINT32_MAX;
        uint32_t pad = 0;
        codegen_eh_chain chain;

        memset(&chain, 0, sizeof chain);
        if (!seen[t->begin] || !t->end || !seen[t->end])
            continue;
        if (in_pad && term)
            pad = e->eh_terminate_pad;
        else if (!in_pad)
            pad = e->eh_nodes[t->node].pad;
        if (pad && !seen[pad])
            pad = 0;
        pads[k] = pad;
        if (!pad)
            continue;
        if (in_pad || e->eh_nodes[t->node].catch_all) {
            chain.types[chain.count++] = 0; /* (catch (...)) */
            chain.closed = true;
        } else
            for (uint32_t m = t->node; m; m = e->eh_nodes[m].parent)
                codegen_eh_chain_node(&chain, c, e->eh_nodes[m].symbol);
        action[k] = codegen_eh_action(e, &e->eh_lsda, &chain);
    }
    vtext_string_append(b,
                        "\t.pushsection\t.gcc_except_table,\"a\",@progbits\n");
    codegen_eh_table_begin(b, &e->eh_lsda, s);
    for (uint32_t k = 0; k < e->eh_site_count; k++) {
        const struct codegen_eh_site *t = &e->eh_sites[k];

        if (!seen[t->begin] || !t->end || !seen[t->end])
            continue;
        vtext_string_append(b, "\t.uleb128 ");
        a64_print_label(b, s, t->begin);
        vtext_char_append(b, '-');
        codegen_symbol_name_append(c, b, s);
        vtext_string_append(b, "\n\t.uleb128 ");
        a64_print_label(b, s, t->end);
        vtext_char_append(b, '-');
        a64_print_label(b, s, t->begin);
        vtext_char_append(b, '\n');
        if (pads[k]) {
            vtext_string_append(b, "\t.uleb128 ");
            a64_print_label(b, s, pads[k]);
            vtext_char_append(b, '-');
            codegen_symbol_name_append(c, b, s);
            vtext_char_append(b, '\n');
        } else
            vtext_string_append(b, "\t.byte\t0\n");
        vtext_format_append(b, "\t.uleb128 %u\n", action[k]);
    }
    codegen_eh_table_end(b, c, &e->eh_lsda, s);
    vtext_string_append(b, "\t.popsection\n");
    free(seen), free(action), free(pads);
}
