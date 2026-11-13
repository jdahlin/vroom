#ifndef VCC_EMIT_H
#define VCC_EMIT_H
/*
 * vcc code generator: arm64 code straight from frontend/c/'s AST (no IR).
 * See codegen/emitimpl.h.  Called by cc_run() when cc_opts.emit is set.
 */
#include "../frontend/c/cc.h"

/* generate the TU; returns the number of errors */
int emit_tu(cc *c, const cc_opts *o);

#endif
