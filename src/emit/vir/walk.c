/*
 * The walker compiled a second time, against the VIR builder (SPEC-vir.md
 * X1): every wk_ function is vw_wk_..., every LO(name) is vw_name
 * (emit/vir/build.c).  Part of the emit unity TU, after the first
 * instantiation; unprefix.h gives the later files the first one's names.
 */
#define WK_PREFIX vw_
#define LO_PREFIX vw_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "../walk/expr.c"
#include "../walk/stmt.c"
#include "../walk/switch-values.c"
#include "../walk/sdivconst.c"
#include "../walk/sdivpow2.c"
#include "../walk/udivconst.c"
#include "../walk/unprefix.h"
