/*
 * tail.c: sibling calls (tail calls).  Two ways in: em_tail_candidate, a body that is exactly
 * `return f(args);` forwarding to a direct call, which the direct emitters emit without a frame;
 * and em_tail_sites_find, the calls in tail position of any function whose body takes no address
 * in its frame (the prepass's CODEGEN_NODE_FRAME_EXPOSED; tail-borrow.cpp), which their call lowering jumps to.
 * VIR finds its own on its instructions (codegen/vir/lowering-driver.cpp).
 *
 * em_tail_caller_ok and em_tail_target_ok are the option and attribute rules (-O1 or more, sibling
 * calls not disabled, not instrumented, no interrupt, error or ifunc function, no attribute that
 * disables tail calls or returns twice, and no setjmp-family target), and em_tail_candidate adds the
 * shape: no variadics, the arguments fit the target's register arguments (maxargs), word-sized
 * integer or pointer types.  The target must separately reject ABI and frame states it cannot do;
 * default WebAssembly has no tail call and does not use this.  Called by arm64's and x86-64's tail
 * passes and, for the two rules, by arm64's VIR lowering (arch/a64/vir).
 */
#include "../codegen-internal.h"

static bool em_tail_word(cc *c, ast_qualified_type t)
{
    uint64_t size = codegen_type_size(c, t);

    return !(AST_QUALIFIED_TYPE_QUALIFIERS(t) & AST_QUALIFIER_VOLATILE) &&
           !(T(c, t)->flags & (AST_TYPE_FLAG_ATOMIC | AST_TYPE_FLAG_VLA)) &&
           !codegen_type_is_bitint(c, t) &&
           (codegen_type_is_pointer(c, t) || codegen_type_is_integer(c, t)) &&
           (size == 4 || size == 8);
}

/* the caller's options and attributes allow sibling calls */
VPRIV bool em_tail_caller_ok(codegen_context *e, uint32_t fn)
{
    return e->unit->options->opt && !e->unit->cleanups_disabled &&
           e->unit->options->sibling_calls >= 0 &&
           !e->unit->options->instrument &&
           !codegen_symbol_attributes(e->compile_context, fn)->interrupt &&
           !(codegen_symbol_attributes(e->compile_context, fn)->flags &
             (CC_ATTRIBUTE_FLAG_TAIL_DISABLE |
              CC_ATTRIBUTE_FLAG_TAIL_RETURNS_TWICE |
              CC_ATTRIBUTE_FLAG_OPTNONE));
}

/* function s may be a sibling call's target (as for its attributes) */
VPRIV bool em_tail_target_ok(codegen_context *e, uint32_t s)
{
    cc *c = e->compile_context;
    const sym *target = &c->syms[s];

    if (target->kind != AST_SYMBOL_FUNCTION ||
        (target->flags & AST_SYMBOL_FLAG_NORETURN) ||
        codegen_symbol_attributes(c, s)->error_fn ||
        codegen_symbol_attributes(c, s)->ifunc ||
        codegen_symbol_attributes(c, s)->interrupt ||
        (codegen_symbol_attributes(c, s)->flags &
         (CC_ATTRIBUTE_FLAG_TAIL_RETURNS_TWICE |
          CC_ATTRIBUTE_FLAG_TAIL_NOT_CALLED)))
        return false;
    return !em_tail_returns_twice(e, s);
}

/* s returns twice: such a function keeps the frame it was called from */
VPRIV bool em_tail_returns_twice(codegen_context *e, uint32_t s)
{
    cc *c = e->compile_context;
    const sym *target = &c->syms[s];

    if (target->kind != AST_SYMBOL_FUNCTION)
        return false;
    /* The standard nonlocal-jump entry points are special even when a caller
	   supplies a plain prototype without the usual returns_twice attribute. */
    return (codegen_symbol_attributes(c, s)->flags &
            CC_ATTRIBUTE_FLAG_TAIL_RETURNS_TWICE) ||
           CODEGEN_IDENTIFIER_IS(target->name, "setjmp") ||
           CODEGEN_IDENTIFIER_IS(target->name, "_setjmp") ||
           CODEGEN_IDENTIFIER_IS(target->name, "sigsetjmp") ||
           CODEGEN_IDENTIFIER_IS(target->name, "__sigsetjmp");
}

/* the call node under casts that keep the register's value, else 0 */
static uint32_t em_tail_call_under(cc *c, uint32_t n)
{
    while (n && N(n)->op == AST_NODE_CAST &&
           (N(n)->aux == AST_CAST_NOP ||
            N(n)->aux == AST_CAST_POINTER_TO_POINTER ||
            N(n)->aux == AST_CAST_VOID))
        n = N(n)->a;
    if (!n || N(n)->op != AST_NODE_CALL || N(n)->aux == AST_CALL_MUSTTAIL)
        return 0;
    return n;
}

static void em_tail_site_add(codegen_context *e, uint32_t call)
{
    if (call &&
        e->tail_site_count < sizeof e->tail_sites / sizeof *e->tail_sites)
        e->tail_sites[e->tail_site_count++] = call;
}

/* the statement n of a function returning rt; last: after it the function returns at once */
static void em_tail_statement(codegen_context *e, uint32_t n,
                              ast_qualified_type rt, bool last)
{
    cc *c = e->compile_context;
    const node *x;
    bool is_void = KIND(c, rt) == AST_TYPE_VOID;

    if (!n)
        return;
    x = N(n);
    switch (x->op) {
    case AST_NODE_BLOCK:
        for (uint32_t i = 0; i < x->c; i++) {
            bool then_returns = last;

            if (i + 1 < x->c) {
                const node *next = N(c->lists[x->b + i + 1]);

                then_returns =
                    is_void && next->op == AST_NODE_RETURN && !next->a;
            }
            em_tail_statement(e, c->lists[x->b + i], rt, then_returns);
        }
        return;
    case AST_NODE_IF:
        em_tail_statement(e, x->b, rt, last);
        em_tail_statement(e, x->c, rt, last);
        return;
    case AST_NODE_WHILE:
    case AST_NODE_DO:
    case AST_NODE_SWITCH:
        em_tail_statement(e, x->b, rt, false);
        return;
    case AST_NODE_FOR:
        em_tail_statement(e, c->lists[x->c + 1], rt, false);
        return;
    case AST_NODE_CASE:
    case AST_NODE_DEFAULT:
    case AST_NODE_LABEL:
        em_tail_statement(e, x->c, rt, last);
        return;
    case AST_NODE_RETURN: {
        uint32_t call = em_tail_call_under(c, x->a);

        if (!call)
            return;
        ast_qualified_type t = N(call)->ty;

        /* the callee leaves the result where this function's caller wants it */
        if (AST_QUALIFIED_TYPE_UNQUALIFIED(t) ==
                AST_QUALIFIED_TYPE_UNQUALIFIED(rt) ||
            (codegen_type_is_pointer(c, t) && codegen_type_is_pointer(c, rt)) ||
            (is_void && KIND(c, t) == AST_TYPE_VOID))
            em_tail_site_add(e, call);
        return;
    }
    case AST_NODE_EXPRESSION: {
        uint32_t call = last && is_void ? em_tail_call_under(c, x->a) : 0;

        if (!call)
            return;
        ast_qualified_type t = N(call)->ty;

        /* a dropped result: not one left on the x87 stack or in memory */
        if (KIND(c, t) == AST_TYPE_VOID || codegen_type_is_pointer(c, t) ||
            (codegen_type_is_integer(c, t) && codegen_type_size(c, t) <= 8))
            em_tail_site_add(e, call);
        return;
    }
    default:
        return;
    }
}

/* the calls in tail position of function fn, for the direct emitters: `return f(...);`, and in a
   void function a call after which it returns */
VPRIV void em_tail_sites_find(codegen_context *e, uint32_t fn)
{
    cc *c = e->compile_context;

    e->tail_site_count = 0;
    e->sibling = false;
    if (!em_tail_caller_ok(e, fn))
        return;
    codegen_function_walk_ensure(e->unit, c, fn);
    if (e->unit->node_flags[c->syms[fn].body] & CODEGEN_NODE_FRAME_EXPOSED)
        return;
    em_tail_statement(e, c->syms[fn].body, T(c, c->syms[fn].ty)->base, true);
}

VPRIV bool em_tail_site(const codegen_context *e, uint32_t call)
{
    for (uint32_t i = 0; i < e->tail_site_count; i++)
        if (e->tail_sites[i] == call)
            return true;
    return false;
}

/* The target must separately reject unsupported ABI/frame states. Capability
   is deliberately absent for default WebAssembly, whose MVP has no tail call. */
VPRIV bool em_tail_candidate(codegen_context *e, uint32_t fn, uint32_t expr,
                             uint32_t maxargs, uint32_t *callee)
{
    cc *c = e->compile_context;
    const sym *caller = &c->syms[fn];
    const ctype *ft = T(c, caller->ty);
    const node *body = N(caller->body), *ret, *call, *fdec, *name;
    const sym *target;
    const ctype *ct;
    uint32_t s;

    if (!em_tail_caller_ok(e, fn) || !expr || body->op != AST_NODE_BLOCK ||
        body->c != 1 || body->aux > maxargs ||
        (ft->flags & (AST_TYPE_FLAG_VARIADIC | AST_TYPE_FLAG_NO_PROTOTYPE)))
        return false;
    ret = N(c->lists[body->b]);
    if (ret->op != AST_NODE_RETURN || ret->a != expr ||
        N(expr)->op != AST_NODE_CALL || N(expr)->aux == AST_CALL_MUSTTAIL)
        return false;
    call = N(expr);
    if (call->c > maxargs ||
        AST_QUALIFIED_TYPE_UNQUALIFIED(call->ty) !=
            AST_QUALIFIED_TYPE_UNQUALIFIED(ft->base) ||
        (KIND(c, ft->base) != AST_TYPE_VOID && !em_tail_word(c, ft->base)))
        return false;
    /* Exactly the function-to-pointer decay, with no function-pointer casts. */
    fdec = N(call->a);
    if (fdec->op != AST_NODE_CAST || fdec->aux != AST_CAST_FUNCTION_DECAY ||
        N(fdec->a)->op != AST_NODE_VARIABLE)
        return false;
    name = N(fdec->a);
    s = codegen_symbol_canonical(c, name->a);
    target = &c->syms[s];
    if ((target->flags &
         (AST_SYMBOL_FLAG_ALWAYS_INLINE | AST_SYMBOL_FLAG_INLINE)) ||
        !em_tail_target_ok(e, s))
        return false;
    ct = T(c, target->ty);
    if (ct->kind != AST_TYPE_FUNCTION ||
        (ct->flags & (AST_TYPE_FLAG_VARIADIC | AST_TYPE_FLAG_NO_PROTOTYPE)) ||
        ct->n != call->c ||
        AST_QUALIFIED_TYPE_UNQUALIFIED(ct->base) !=
            AST_QUALIFIED_TYPE_UNQUALIFIED(ft->base))
        return false;
    for (uint32_t i = 0; i < body->aux; i++) {
        uint32_t p = c->lists[body->a + i];
        const sym *param = &c->syms[p];

        /* No parameter's address, cleanup, volatile access or dynamic type may
		   outlive this frame. There are no local declarations in this body. */
        if (!em_tail_word(c, param->ty) ||
            codegen_type_is_variably_modified(c, param->ty) ||
            !em_local_eligible(e, p))
            return false;
    }
    for (uint32_t i = 0; i < call->c; i++) {
        const node *a = N(c->lists[call->b + i]);
        ast_qualified_type pt = c->params[ct->aux + i];
        bool formal = false;

        if (!em_tail_word(c, pt) || AST_QUALIFIED_TYPE_UNQUALIFIED(a->ty) !=
                                        AST_QUALIFIED_TYPE_UNQUALIFIED(pt))
            return false;
        if (a->op == AST_NODE_NUMBER ||
            (a->op == AST_NODE_UNARY && a->aux == LEX_TOKEN_MINUS &&
             N(a->a)->op == AST_NODE_NUMBER))
            continue;
        if (a->op != AST_NODE_VARIABLE)
            return false;
        for (uint32_t j = 0; j < body->aux; j++)
            formal |= a->a == c->lists[body->a + j];
        if (!formal)
            return false;
    }
    *callee = s;
    return true;
}
