#ifndef VCC_EMIT_H
#define VCC_EMIT_H
/*
 * vcc code generator: arm64 code straight from frontend/c/'s AST (no IR).
 * See codegen/codegen-internal.h.  Called by cc_translation_unit_run() when cc_options.emit is set.
 */
#include "frontend/c/cc.h"

/* generate the TU; returns the number of errors */
int codegen_unit_emit(cc *c, const cc_options *o);

#endif
