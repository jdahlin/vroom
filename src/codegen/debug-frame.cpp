/*
 * debug-frame.cpp: -g -O0: where each named local and parameter of a function is.  The walker
 * notes them as it gives them frame slots; VIR's lowerings move them to their own slots
 * (virbuild_debug_locals_place); the frame's rewrite of the records fixes the register they are
 * offsets from.  debug-unit.cpp writes them.
 */
#include "codegen-internal.h"

VPRIV void codegen_debug_local_add(codegen_context *e, uint32_t s,
                                   int64_t offset, bool is_placed)
{
    /* (C++'s this has no name in its symbol, only for -g) */
    if (!e->unit->debug_frames ||
        (!e->compile_context->syms[s].name &&
         !codegen_symbol_attributes(e->compile_context, s)->debug_name))
        return;
    if (e->debug_local_count == e->debug_local_capacity) {
        e->debug_local_capacity = 2 * e->debug_local_capacity + 32;
        e->debug_locals = (codegen_debug_local *)realloc(
            e->debug_locals, e->debug_local_capacity * sizeof *e->debug_locals);
        if (!e->debug_locals)
            vout_of_memory();
    }
    codegen_debug_local *l = &e->debug_locals[e->debug_local_count++];

    l->symbol = s;
    l->offset = offset;
    l->is_placed = is_placed;
}

VPRIV void codegen_debug_frame_store(codegen_context *e, uint32_t function,
                                     int frame_register, int64_t delta)
{
    codegen_debug_frame **slot;
    codegen_debug_frame *f;

    if (!e->unit->debug_frames)
        return;
    slot = &e->unit->debug_frames[function];
    free(*slot);
    f = (codegen_debug_frame *)malloc(sizeof *f +
                                      e->debug_local_count * sizeof *f->locals);
    if (!f)
        vout_of_memory();
    f->frame_register = (uint8_t)frame_register;
    f->count = 0;
    for (uint32_t i = 0; i < e->debug_local_count; i++) {
        codegen_debug_local l = e->debug_locals[i];

        if (!l.is_placed)
            continue;
        l.offset += delta;
        f->locals[f->count++] = l;
    }
    *slot = f;
}
