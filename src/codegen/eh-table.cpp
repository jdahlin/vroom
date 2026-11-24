/*
 * eh-table.cpp: the type and action tables of a function's LSDA (.gcc_except_table), shared by the
 * direct emitters and VIR's lowerings (doc/PLAN-vir-eh.md E3).  A call site's action says which catch
 * types its landing pad can handle: the chain of the handlers of the try blocks around the call,
 * innermost first, ending at the first catch (...), and a final filter 0 record when a cleanup runs on
 * the way and no catch (...) ends the chain.  [except.handle], Itanium C++ ABI 2.5.3 "Exception
 * Handling"; the encodings are GCC's (DW_EH_PE_pcrel|sdata4, the types indirect).
 */
#include "codegen-internal.h"

VPRIV uint32_t codegen_eh_type_index(codegen_context *e, codegen_eh_lsda *l,
                                     uint32_t symbol)
{
    if (symbol)
        symbol = codegen_symbol_canonical(e->compile_context, symbol);
    for (uint32_t i = 0; i < l->type_count; i++)
        if (l->types[i] == symbol)
            return i + 1;
    if (l->type_count == 63 || l->type_count == CODEGEN_EH_TYPES)
        codegen_fatal_report(e, 0, "too many exception types in one function");
    l->types[l->type_count++] = symbol;
    return l->type_count;
}

VPRIV void codegen_eh_chain_node(codegen_eh_chain *k, cc *c, uint32_t symbol)
{
    uint32_t n = 0;
    const uint32_t *h = codegen_catch_scope(c, symbol, &n);

    if (k->closed)
        return;
    if (!h) {
        k->cleanup = true;
        return;
    }
    for (uint32_t i = 0; i < n && !k->closed; i++) {
        uint32_t t = h[2 * i];

        if (t)
            t = codegen_symbol_canonical(c, t);
        for (uint32_t j = 0; j < k->count && t; j++)
            if (k->types[j] == t)
                t = UINT32_MAX; /* (an inner handler of the type comes first: this one is never reached) */
        if (t == UINT32_MAX)
            continue;
        if (k->count == CODEGEN_EH_CHAIN_TYPES)
            return;
        k->types[k->count++] = t;
        if (!t)
            k->closed = true;
    }
}

VPRIV uint32_t codegen_eh_action(codegen_context *e, codegen_eh_lsda *l,
                                 const codegen_eh_chain *k)
{
    uint32_t records = k->count + (k->cleanup && !k->closed);

    if (!records)
        return 0;
    if (k->count == 0)
        return 0; /* only a cleanup: action 0 */
    for (uint32_t i = 0; i < l->chain_count; i++) {
        const codegen_eh_chain *o = &l->chains[i];

        if (o->count == k->count && o->cleanup == k->cleanup &&
            !memcmp(o->types, k->types, k->count * sizeof k->types[0]))
            return o->offset + 1u;
    }
    if (l->chain_count == CODEGEN_EH_CHAINS)
        codegen_fatal_report(e, 0,
                             "too many exception handlers in one function");
    codegen_eh_chain *o = &l->chains[l->chain_count++];

    *o = *k;
    o->offset = (uint16_t)l->action_size;
    for (uint32_t i = 0; i < records; i++) {
        uint32_t f = 0;

        if (i < k->count)
            f = codegen_eh_type_index(e, l, k->types[i]);
        l->actions[l->action_size++] = (uint8_t)f; /* sleb128, below 64 */
        l->actions[l->action_size++] =
            i + 1 < records ? 1 : 0; /* the next record follows, or none */
    }
    return o->offset + 1u;
}

VPRIV void codegen_eh_table_begin(tbuf *b, const codegen_eh_lsda *l,
                                  uint32_t id)
{
    vtext_string_append(b, "\t.p2align\t2\n");
    if (l->type_count)
        vtext_format_append(
            b,
            ".Lvcclsda%u:\n\t.byte\t255\n\t.byte\t155\n\t.uleb128 "
            ".Lvccttb%u-.Lvccttr%u\n.Lvccttr%u:\n\t.byte\t1\n\t.uleb128 "
            ".Lvcccse%u-.Lvcccsb%u\n.Lvcccsb%u:\n",
            id, id, id, id, id, id, id);
    else
        vtext_format_append(
            b,
            ".Lvcclsda%u:\n\t.byte\t255\n\t.byte\t255\n\t.byte\t1\n\t.uleb128 "
            ".Lvcccse%u-.Lvcccsb%u\n.Lvcccsb%u:\n",
            id, id, id, id);
}

VPRIV void codegen_eh_table_end(tbuf *b, cc *c, const codegen_eh_lsda *l,
                                uint32_t id)
{
    vtext_format_append(b, ".Lvcccse%u:\n", id);
    if (!l->type_count)
        return;
    for (uint32_t i = 0; i < l->action_size; i++)
        vtext_format_append(b, "\t.byte\t%u\n", l->actions[i]);
    vtext_string_append(b, "\t.p2align\t2\n");
    /* entry i (1-based) is i words in front of the table's base: the last first */
    for (uint32_t i = l->type_count; i > 0; i--) {
        if (!l->types[i - 1])
            vtext_string_append(b, "\t.long\t0\n");
        else
            vtext_format_append(b, "\t.long\t.Lvccty%u_%u-.\n", id, i);
    }
    vtext_format_append(b, ".Lvccttb%u:\n", id);
    /* the indirect cells: the type_info's address, a data relocation */
    for (uint32_t i = 1; i <= l->type_count; i++) {
        if (!l->types[i - 1])
            continue;
        vtext_string_append(b,
                            "\t.pushsection\t.data.rel.ro,\"aw\",@progbits\n");
        vtext_format_append(b, "\t.p2align\t3\n.Lvccty%u_%u:\n\t.quad\t", id,
                            i);
        codegen_symbol_name_append(c, b, l->types[i - 1]);
        vtext_string_append(b, "\n\t.popsection\n");
    }
}
