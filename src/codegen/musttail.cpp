/*
 * musttail (AST_CALL_MUSTTAIL): the call a return statement returns is a jump at every level, in the
 * direct emitters and VIR.  The front end has checked clang's rules (matching signatures, nothing
 * to destroy); here and in the targets' call lowering, a case with no guarantee is an error, never
 * an ordinary call.  A function with such a call is not inlined (prepass.cpp).
 */
#include "codegen-internal.h"

VPRIV __attribute__((noreturn)) void codegen_musttail_refuse(codegen_context *e,
                                                             const char *why)
{
    codegen_fatal_report(
        e, e->current_location,
        "cannot guarantee the tail call 'musttail' requires: %s", why);
}

/* the call node call in function (the caller's symbol) */
VPRIV void codegen_musttail_check(codegen_context *e, uint32_t call,
                                  uint32_t function)
{
    cc *c = e->compile_context;
    uint32_t callee = N(call)->a;

    e->current_location = N(call)->loc; /* (the errors' place) */
    if (WASM(c))
        codegen_musttail_refuse(e, "WebAssembly");
    if (CODEGEN_IS_WIN64(c))
        codegen_musttail_refuse(e, "Windows x64");
    if (e->unit->options->instrument &&
        !(codegen_symbol_attributes(c, function) &&
          codegen_symbol_attributes(c, function)->noinstr))
        codegen_musttail_refuse(e, "-finstrument-functions");
    while (N(callee)->op == AST_NODE_CAST)
        callee = N(callee)->a;
    if (N(callee)->op == AST_NODE_VARIABLE &&
        c->syms[N(callee)->a].kind == AST_SYMBOL_BUILTIN)
        codegen_musttail_refuse(e, "a builtin function");
}
