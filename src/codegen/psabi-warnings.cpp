/*
 * -Wpsabi: x86-64 System V passes a 32-byte vector in AVX registers and a
 * 64-byte one in AVX-512 registers only when the feature is enabled, so code
 * compiled with and without it disagrees.  clang warns where it generates IR
 * for such a definition or direct call; this is that set, found by one walk of
 * the AST before any emitter runs, so it is the same at every -O level and for
 * both emitters, and in source order.
 *
 * As clang: a definition warns once for the result and once for the first wide
 * parameter, a call once (the first wide argument, else the result); internal
 * and always_inline functions are exempt, indirect calls are not checked.
 * Unlike clang, a call in unreachable code is still reported.
 *
 * C++'s lowered AST has no source locations: the lowering notes them
 * (ast-builder.h), a site without one is silent, and the report is C++'s.
 */
#include "codegen-internal.h"
#include "frontend/c/ast-builder.h"

typedef struct codegen_psabi {
    cc *c;
    uint8_t *queued; /* per symbol */
    uint32_t *queue, count;
    uint32_t function; /* being walked, 0: a variable's initializer */
    int errors;
} codegen_psabi;

/* the feature a vector of type t needs to travel in registers, 0: none */
static int codegen_psabi_feature(cc *c, ast_qualified_type t)
{
    if (!codegen_type_is_vector(c, t))
        return 0;
    uint64_t size = codegen_type_size(c, t);

    if (size > 32)
        return X86_FEATURE_AVX512F;
    if (size > 16)
        return X86_FEATURE_AVX;
    return 0;
}

/* where to report the site: as noted, else (C) at fallback; 0: nowhere */
static uint32_t codegen_psabi_location(cc *c, int kind, uint32_t key,
                                       uint32_t fallback)
{
    for (const struct cc_psabi_site *r = c->psabi_sites; r; r = r->next)
        if (r->kind == (uint32_t)kind && r->key == key)
            return r->loc;
    if (c->built)
        return 0;
    return fallback;
}

/* C++'s report, the type as clang spells one without an alias */
static void codegen_psabi_report_built(codegen_psabi *P, uint32_t loc,
                                       const char *what, ast_qualified_type t,
                                       const char *feature)
{
    cc *c = P->c;
    const ctype *vector = T(c, t);
    char text[238];

    snprintf(text, sizeof text,
             "AVX vector %s of type '%s' (vector of %u '%s' values) without "
             "'%s' enabled changes the ABI",
             what, cc_type_diagnostic_name(c, t), (unsigned)vector->n,
             cc_type_diagnostic_name(c, vector->base), feature);
    if (c->psabi_report(c->psabi_context, loc, text))
        P->errors++;
}

static void codegen_psabi_report(codegen_psabi *P, uint32_t loc,
                                 const char *what, ast_qualified_type t,
                                 int feature)
{
    cc *c = P->c;
    diag_info d = {"warning", loc, NULL, 0, NULL, 0, NULL, 0};
    const char *name = "avx";

    if (!loc)
        return;
    if (feature == X86_FEATURE_AVX512F)
        name = "avx512f";
    if (c->built) {
        codegen_psabi_report_built(P, loc, what, t, name);
        return;
    }
    if (diag_warning_level(c->p, "psabi", true, NULL, loc) == DIAG_LEVEL_ERROR)
        P->errors++;
    diag_warning_report(c->p, "psabi", true, NULL, &d,
                        "AVX vector %s of type '%s' without '%s' enabled "
                        "changes the ABI",
                        what, cc_type_diagnostic_name(c, t), name);
}

static void codegen_psabi_definition(codegen_psabi *P, uint32_t s)
{
    cc *c = P->c;
    const sym *y = &c->syms[s];
    x86_features have = cc_x86_features_get(c, s);

    if (y->sc == AST_STORAGE_STATIC)
        return;
    ast_qualified_type result = T(c, y->ty)->base;
    int feature = codegen_psabi_feature(c, result);

    if (feature && !x86_feature_has(have, (unsigned)feature))
        codegen_psabi_report(
            P, codegen_psabi_location(c, AST_BUILDER_PSABI_RESULT, s, y->loc),
            "return", result, feature);
    const node *body = N(y->body);

    for (uint32_t i = 0; i < body->aux; i++) {
        uint32_t p = c->lists[body->a + i];
        const sym *parameter = &c->syms[p];

        feature = codegen_psabi_feature(c, parameter->ty);
        if (feature && !x86_feature_has(have, (unsigned)feature)) {
            codegen_psabi_report(
                P,
                codegen_psabi_location(c, AST_BUILDER_PSABI_PARAMETER, p,
                                       parameter->loc),
                "argument", parameter->ty, feature);
            break;
        }
    }
}

static void codegen_psabi_call(codegen_psabi *P, uint32_t n)
{
    cc *c = P->c;
    const node *x = N(n);
    uint32_t reference,
        callee = codegen_call_direct_callee(c, x->a, &reference);
    const node *name = N(x->a);

    while (name->op == AST_NODE_CAST)
        name = N(name->a);
    uint32_t loc =
        codegen_psabi_location(c, AST_BUILDER_PSABI_CALL, n, name->loc);

    /* an indirect call is not checked: C++ notes a site for a virtual one */
    if (!callee && !c->built)
        return;
    x86_features caller_has = cc_x86_features_get(c, P->function);
    x86_features callee_has = caller_has;

    if (callee) {
        callee = codegen_symbol_canonical(c, callee);
        if (c->syms[callee].sc == AST_STORAGE_STATIC ||
            (c->syms[callee].flags & AST_SYMBOL_FLAG_ALWAYS_INLINE))
            return;
        callee_has = cc_x86_features_get(c, callee);
    }
    for (uint32_t i = 0; i <= x->c; i++) {
        ast_qualified_type t = x->ty; /* (after the arguments: the result) */
        const char *what = "return";

        if (i < x->c) {
            t = N(c->lists[x->b + i])->ty;
            what = "argument";
        }
        int feature = codegen_psabi_feature(c, t);

        /* one side having the feature is an error, the lowering's */
        if (feature && !x86_feature_has(caller_has, (unsigned)feature) &&
            !x86_feature_has(callee_has, (unsigned)feature)) {
            codegen_psabi_report(P, loc, what, t, feature);
            return;
        }
    }
}

static void codegen_psabi_queue(codegen_psabi *P, uint32_t s)
{
    cc *c = P->c;

    s = codegen_symbol_canonical(c, s);
    if (P->queued[s] || c->syms[s].kind != AST_SYMBOL_FUNCTION ||
        !(c->syms[s].flags & AST_SYMBOL_FLAG_DEFINED) || !c->syms[s].body)
        return;
    P->queued[s] = 1;
    P->queue[P->count++] = s;
}

/* the calls under n, operands first, and the functions it refers to */
static void codegen_psabi_walk(codegen_psabi *P, uint32_t n)
{
    cc *c = P->c;

    if (!n)
        return;
    const node *x = N(n);

    switch (x->op) {
    case AST_NODE_VARIABLE:
        codegen_psabi_queue(P, x->a);
        break;
    case AST_NODE_CALL:
    case AST_NODE_BUILTIN:
        if (x->op == AST_NODE_CALL)
            codegen_psabi_walk(P, x->a);
        for (uint32_t i = 0; i < x->c; i++)
            codegen_psabi_walk(P, c->lists[x->b + i]);
        if (x->op == AST_NODE_CALL && P->function)
            codegen_psabi_call(P, n);
        break;
    case AST_NODE_INITIALIZER:
        for (uint32_t i = 0; i < x->c; i++) {
            if (x->aux == 1)
                codegen_psabi_walk(P, c->lists[x->b + 3 * i + 2]);
            else
                codegen_psabi_walk(P, c->lists[x->b + i]);
        }
        break;
    case AST_NODE_BLOCK:
        for (uint32_t i = 0; i < x->c; i++)
            codegen_psabi_walk(P, c->lists[x->b + i]);
        break;
    case AST_NODE_FOR:
        codegen_psabi_walk(P, x->a);
        codegen_psabi_walk(P, x->b);
        codegen_psabi_walk(P, c->lists[x->c + 1]);
        codegen_psabi_walk(P, c->lists[x->c]);
        break;
    case AST_NODE_ASM: {
        const ast_asm_statement *a = &c->asms[x->a];

        for (uint32_t i = 0; i < a->nout + a->nin; i++)
            codegen_psabi_walk(P, a->ops[i].expr);
        break;
    }
    case AST_NODE_DECLARATION:
        codegen_psabi_walk(P, x->b);
        break;
    case AST_NODE_CONDITIONAL:
    case AST_NODE_IF:
        codegen_psabi_walk(P, x->a);
        codegen_psabi_walk(P, x->b);
        codegen_psabi_walk(P, x->c);
        break;
    case AST_NODE_INDEX:
    case AST_NODE_BINARY:
    case AST_NODE_LOGICAL_AND:
    case AST_NODE_LOGICAL_OR:
    case AST_NODE_COMMA:
    case AST_NODE_COMPLEX:
    case AST_NODE_ASSIGN:
    case AST_NODE_WHILE:
    case AST_NODE_SWITCH:
        codegen_psabi_walk(P, x->a);
        codegen_psabi_walk(P, x->b);
        break;
    case AST_NODE_DO:
        codegen_psabi_walk(P, x->b);
        codegen_psabi_walk(P, x->a);
        break;
    case AST_NODE_CASE:
    case AST_NODE_DEFAULT:
    case AST_NODE_LABEL:
        codegen_psabi_walk(P, x->c);
        break;
    case AST_NODE_MEMBER:
    case AST_NODE_CAST:
    case AST_NODE_UNARY:
    case AST_NODE_ADDRESS:
    case AST_NODE_DEREFERENCE:
    case AST_NODE_PRE_INCREMENT:
    case AST_NODE_PRE_DECREMENT:
    case AST_NODE_POST_INCREMENT:
    case AST_NODE_POST_DECREMENT:
    case AST_NODE_SIZEOF_VLA:
    case AST_NODE_STATEMENT_EXPRESSION:
    case AST_NODE_COMPOUND_LITERAL:
    case AST_NODE_VA_ARG:
    case AST_NODE_EXPRESSION:
    case AST_NODE_RETURN:
    case AST_NODE_GOTO_INDIRECT:
        codegen_psabi_walk(P, x->a);
        break;
    default:
        break;
    }
}

static bool codegen_psabi_has_wide_vector(cc *c)
{
    for (uint32_t i = 1; i < c->nty; i++)
        if (c->ty[i].kind == AST_TYPE_VECTOR && c->ty[i].size > 16)
            return true;
    return false;
}

/* the number of warnings that -Werror made errors */
VPRIV int codegen_psabi_warn(cc *c)
{
    /* a TU without a vector over 16 bytes has nothing to warn about */
    if (!CODEGEN_IS_X64(c) || CODEGEN_IS_WIN64(c) ||
        !codegen_psabi_has_wide_vector(c))
        return 0;
    codegen_psabi P = {c, NULL, NULL, 0, 0, 0};

    P.queued = (uint8_t *)calloc(c->nsym, 1);
    P.queue = (uint32_t *)malloc(c->nsym * sizeof *P.queue);
    if (!P.queued || !P.queue)
        vout_of_memory();
    /* what is emitted whether used or not, in order; then what that refers to */
    for (uint32_t i = 0; i < c->ntoplevel; i++) {
        uint32_t s = c->toplevel[i];

        if (!codegen_symbol_is_root(c, s))
            continue;
        if (c->syms[s].kind == AST_SYMBOL_FUNCTION)
            codegen_psabi_queue(&P, s);
        else
            codegen_psabi_walk(&P, c->syms[s].body);
    }
    for (uint32_t i = 0; i < P.count; i++) {
        P.function = P.queue[i];
        codegen_psabi_definition(&P, P.function);
        codegen_psabi_walk(&P, c->syms[P.function].body);
    }
    free(P.queued);
    free(P.queue);
    return P.errors;
}
