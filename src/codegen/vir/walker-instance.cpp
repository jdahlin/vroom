/*
 * The walker compiled a second time, against the VIR builder (SPEC-vir.md
 * X1): every walk_ function is virbuild_walk_..., every WALK_LOWER(name) is virbuild_function_name_get
 * (codegen/vir/preir.h).  Part of the codegen unity TU, after the first
 * instantiation; unprefix.h gives the later files the first one's names.
 */
#define WALK_PREFIX virbuild_
#define WALK_LOWER_PREFIX virbuild_
#include "../walk/walk.h"
#include "../walk/proto.h"
#include "../walk/expression.cpp"
#include "../walk/statement.cpp"
#include "../walk/switch-values.cpp"
#include "../walk/division-signed-constant.cpp"
#include "../walk/division-signed-power-of-two.cpp"
#include "../walk/division-unsigned-constant.cpp"
#include "../walk/unprefix.h"
