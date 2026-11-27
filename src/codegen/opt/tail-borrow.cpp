/*
 * tail-borrow.c: whether a function keeps an address it is passed.  A sibling call needs that no
 * address in the caller's frame is live (the prepass's CODEGEN_NODE_FRAME_EXPOSED); passing
 * `&local` to a callee that only reads and writes through it during the call leaves none, whether
 * that callee is inlined or called.  The proof is on the callee's source: every use of the
 * parameter must be one of those listed here, anything else (stored, returned, compared, cast to
 * an integer, an asm or builtin operand, an unknown node) counts as kept.  The body must be the
 * one that runs: not a weak function, nor an external one another module can interpose.
 *
 * A "derived" expression has the parameter's address or one inside its object; a "rooted" lvalue
 * is an object (part) that a derived expression points to.
 */
#include "../codegen-internal.h"

enum { EM_BORROW_DEPTH_MAX = 8 };

typedef struct em_borrow {
    cc *c;
    uint32_t parameter;
    int depth;
    bool *
        returns; /* set when the callee returns a derived value; NULL: it may not */
    uint32_t body; /* the function's */
    bool
        table; /* parameter is not the pointer but a local array holding such pointers */
} em_borrow;

static bool em_borrow_derived(em_borrow *B, uint32_t n);
static int em_borrow_memory_kind(cc *c, uint32_t callee, uint32_t ref);
static bool em_borrow_ok(em_borrow *B, uint32_t n);
static bool em_borrow_effect(em_borrow *B, uint32_t n);
static bool em_borrow_call(em_borrow *B, uint32_t n, bool dropped);
static bool em_borrow_rooted(em_borrow *B, uint32_t n);
static bool em_borrow_parts(em_borrow *B, uint32_t n);
static bool em_parameter_borrows(cc *c, uint32_t fn, uint32_t i, int depth,
                                 bool *returns);

/* a direct call's value is derived: an argument is, and its callee may return it */
static bool em_borrow_call_derived(em_borrow *B, uint32_t n)
{
    cc *c = B->c;
    const node *x = N(n);
    uint32_t ref = 0, callee = codegen_call_direct_callee(c, x->a, &ref);

    if (!callee || em_borrow_memory_kind(c, callee, ref))
        return false;
    for (uint32_t i = 0; i < x->c; i++) {
        bool returned = false;

        if (em_borrow_derived(B, c->lists[x->b + i]) &&
            em_parameter_borrows(c, callee, i, B->depth + 1, &returned) &&
            returned)
            return true;
    }
    return false;
}

/* n is an element of the tracked table */
static bool em_borrow_element(const em_borrow *B, uint32_t n)
{
    cc *c = B->c;
    const node *x = N(n);

    return B->table && x->op == AST_NODE_INDEX &&
           N(x->a)->op == AST_NODE_CAST &&
           N(x->a)->aux == AST_CAST_ARRAY_DECAY &&
           N(N(x->a)->a)->op == AST_NODE_VARIABLE &&
           N(N(x->a)->a)->a == B->parameter;
}

static bool em_borrow_derived(em_borrow *B, uint32_t n)
{
    cc *c = B->c;
    const node *x;

    if (!n)
        return false;
    x = N(n);
    if (em_borrow_element(B, n))
        return true;
    switch (x->op) {
    case AST_NODE_VARIABLE:
        return !B->table && x->a == B->parameter;
    case AST_NODE_CAST:
        if (x->aux == AST_CAST_NOP || x->aux == AST_CAST_POINTER_TO_POINTER)
            return em_borrow_derived(B, x->a);
        if (x->aux == AST_CAST_ARRAY_DECAY || x->aux == AST_CAST_VECTOR_ADDRESS)
            return em_borrow_rooted(B, x->a);
        return false;
    case AST_NODE_ADDRESS:
        return em_borrow_rooted(B, x->a);
    case AST_NODE_BINARY: /* pointer arithmetic */
        return (x->aux == LEX_TOKEN_PLUS || x->aux == LEX_TOKEN_MINUS) &&
               codegen_type_is_pointer(c, x->ty) &&
               (em_borrow_derived(B, x->a) || em_borrow_derived(B, x->b));
    case AST_NODE_CONDITIONAL:
        return em_borrow_derived(B, x->b) || em_borrow_derived(B, x->c);
    case AST_NODE_COMMA:
        return em_borrow_derived(B, x->b);
    case AST_NODE_CALL:
        return em_borrow_call_derived(B, n);
    default:
        return false;
    }
}

static bool em_borrow_rooted(em_borrow *B, uint32_t n)
{
    cc *c = B->c;
    const node *x;

    if (!n)
        return false;
    x = N(n);
    switch (x->op) {
    case AST_NODE_DEREFERENCE:
        return em_borrow_derived(B, x->a);
    case AST_NODE_MEMBER:
        return em_borrow_rooted(B, x->a);
    case AST_NODE_INDEX:
        return em_borrow_derived(B, x->a) || em_borrow_rooted(B, x->a);
    case AST_NODE_CAST:
        return x->aux == AST_CAST_NOP && em_borrow_rooted(B, x->a);
    case AST_NODE_CONDITIONAL:
        return em_borrow_rooted(B, x->b) || em_borrow_rooted(B, x->c);
    case AST_NODE_COMMA:
        return em_borrow_rooted(B, x->b);
    default:
        return false;
    }
}

/* the uses inside a derived expression or rooted lvalue n, which is itself used as allowed */
static bool em_borrow_parts(em_borrow *B, uint32_t n)
{
    cc *c = B->c;
    const node *x = N(n);

    if (em_borrow_element(B, n))
        return em_borrow_ok(B, x->b);
    switch (x->op) {
    case AST_NODE_VARIABLE:
        return true;
    case AST_NODE_CAST:
    case AST_NODE_ADDRESS:
    case AST_NODE_DEREFERENCE:
    case AST_NODE_MEMBER:
        if (em_borrow_derived(B, x->a) || em_borrow_rooted(B, x->a))
            return em_borrow_parts(B, x->a);
        return em_borrow_ok(B, x->a);
    case AST_NODE_INDEX:
    case AST_NODE_BINARY: {
        uint32_t side[2] = {x->a, x->b};

        for (int i = 0; i < 2; i++) {
            bool inside =
                em_borrow_derived(B, side[i]) || em_borrow_rooted(B, side[i]);

            if (inside) {
                if (!em_borrow_parts(B, side[i]))
                    return false;
            } else if (!em_borrow_ok(B, side[i]))
                return false;
        }
        return true;
    }
    case AST_NODE_CONDITIONAL:
    case AST_NODE_COMMA: {
        uint32_t side[2] = {x->b, x->c};
        int sides = 2;

        if (x->op == AST_NODE_COMMA)
            side[0] = x->b, sides = 1;
        if (x->op == AST_NODE_COMMA && !em_borrow_effect(B, x->a))
            return false;
        if (x->op == AST_NODE_CONDITIONAL && (!x->b || !em_borrow_ok(B, x->a)))
            return false;
        for (int i = 0; i < sides; i++) {
            bool inside =
                em_borrow_derived(B, side[i]) || em_borrow_rooted(B, side[i]);

            if (inside) {
                if (!em_borrow_parts(B, side[i]))
                    return false;
            } else if (!em_borrow_ok(B, side[i]))
                return false;
        }
        return true;
    }
    case AST_NODE_CALL:
        return em_borrow_call(B, n, false);
    default:
        return false;
    }
}

/* n's value, if derived, is dropped */
static bool em_borrow_discarded(em_borrow *B, uint32_t n)
{
    if (n && em_borrow_derived(B, n))
        return em_borrow_parts(B, n);
    return em_borrow_ok(B, n);
}

/* the C library's memcpy, memmove and memset, by name or as builtins: they keep neither
   pointer, and return the first.  0: another function */
static int em_borrow_memory_kind(cc *c, uint32_t callee, uint32_t ref)
{
    int kind = 0;

    if (!callee) {
        if (ref && CODEGEN_REFERENCE_KIND(ref) == CODEGEN_REFERENCE_NAME)
            kind = cc_builtin_library_kind(CODEGEN_REFERENCE_INDEX(ref));
    } else if (!(c->syms[callee].flags & AST_SYMBOL_FLAG_DEFINED) &&
               c->syms[callee].sc != AST_STORAGE_STATIC) {
        if (CODEGEN_IDENTIFIER_IS(c->syms[callee].name, "memcpy") ||
            CODEGEN_IDENTIFIER_IS(c->syms[callee].name, "memmove"))
            kind = CC_LIBRARY_KIND_MEMCPY;
        if (CODEGEN_IDENTIFIER_IS(c->syms[callee].name, "memset"))
            kind = CC_LIBRARY_KIND_MEMSET;
    }
    if (kind == CC_LIBRARY_KIND_MEMCPY || kind == CC_LIBRARY_KIND_MEMMOVE ||
        kind == CC_LIBRARY_KIND_MEMSET)
        return kind;
    return 0;
}

static bool em_borrow_call(em_borrow *B, uint32_t n, bool dropped)
{
    cc *c = B->c;
    const node *x = N(n);
    uint32_t ref = 0, callee = codegen_call_direct_callee(c, x->a, &ref);
    int kind = em_borrow_memory_kind(c, callee, ref);

    if (!em_borrow_ok(B, x->a))
        return false;
    for (uint32_t i = 0; i < x->c; i++) {
        uint32_t a = c->lists[x->b + i];
        bool borrowed = false;

        if (!em_borrow_derived(B, a)) {
            if (!em_borrow_ok(B, a))
                return false;
            continue;
        }
        if (kind)
            borrowed = i == 1 || (i == 0 && dropped);
        else if (callee) {
            bool returned = false;

            /* (a returned address: the call is then derived itself, and its own use decides) */
            borrowed =
                em_parameter_borrows(c, callee, i, B->depth + 1, &returned);
        }
        if (!borrowed || !em_borrow_parts(B, a))
            return false;
    }
    return true;
}

/* an expression whose value nothing reads */
static bool em_borrow_effect(em_borrow *B, uint32_t n)
{
    cc *c = B->c;

    if (!n)
        return true;
    if (N(n)->op == AST_NODE_CALL)
        return em_borrow_call(B, n, true);
    if (N(n)->op == AST_NODE_CAST && N(n)->aux == AST_CAST_VOID)
        return em_borrow_effect(B, N(n)->a);
    return em_borrow_discarded(B, n);
}

static bool em_borrow_list(em_borrow *B, uint32_t first, uint32_t count,
                           uint32_t step, uint32_t at)
{
    cc *c = B->c;

    for (uint32_t i = 0; i < count; i++)
        if (!em_borrow_ok(B, c->lists[first + step * i + at]))
            return false;
    return true;
}

/* the declaration of a local that takes a derived value: a pointer or reference, or an array
   of them, whose own uses keep it inside the function too */
static bool em_borrow_declaration(em_borrow *B, const node *x)
{
    cc *c = B->c;
    const sym *y = &c->syms[x->a];
    const node *init;
    bool table = false;

    if (y->kind == AST_SYMBOL_VARIABLE &&
        (T(c, y->ty)->flags & AST_TYPE_FLAG_VLA))
        return false;
    if (!x->b)
        return true;
    init = N(x->b);
    if (init->op == AST_NODE_INITIALIZER && init->aux == 1) {
        for (uint32_t i = 0; i < init->c; i++) {
            uint32_t value = c->lists[init->b + 3 * i + 2];
            bool inside = em_borrow_derived(B, value);

            table |= inside;
            if (inside) {
                if (!em_borrow_parts(B, value))
                    return false;
            } else if (!em_borrow_ok(B, value))
                return false;
        }
        if (!table)
            return true;
    } else if (!em_borrow_derived(B, x->b))
        return em_borrow_ok(B, x->b);
    else if (!codegen_type_is_pointer(c, y->ty) || !em_borrow_parts(B, x->b))
        return false;
    if (y->kind != AST_SYMBOL_VARIABLE || y->sc == AST_STORAGE_STATIC ||
        y->sc == AST_STORAGE_EXTERN || y->depth == 0 ||
        codegen_symbol_attributes(c, x->a)->cleanup ||
        B->depth >= EM_BORROW_DEPTH_MAX)
        return false;

    em_borrow A = {c, x->a, B->depth + 1, B->returns, B->body, table};

    return em_borrow_ok(&A, B->body);
}

/* every use of the parameter in n keeps the address inside the call */
static bool em_borrow_ok(em_borrow *B, uint32_t n)
{
    cc *c = B->c;
    const node *x;

    if (!n)
        return true;
    x = N(n);
    switch (x->op) {
    case AST_NODE_NUMBER:
    case AST_NODE_FLOAT_NUMBER:
    case AST_NODE_BITINT_NUMBER:
    case AST_NODE_STRING:
    case AST_NODE_ZERO:
    case AST_NODE_BREAK:
    case AST_NODE_CONTINUE:
    case AST_NODE_GOTO:
        return true;
    case AST_NODE_VARIABLE:
        return x->a != B->parameter;
    case AST_NODE_DEREFERENCE:
    case AST_NODE_MEMBER:
    case AST_NODE_INDEX:
        if (em_borrow_rooted(B, n)) /* a load: the value is not derived */
            return em_borrow_parts(B, n);
        if (x->op == AST_NODE_INDEX)
            return em_borrow_ok(B, x->a) && em_borrow_ok(B, x->b);
        return em_borrow_ok(B, x->a);
    case AST_NODE_ADDRESS:
        return !em_borrow_rooted(B, x->a) && em_borrow_ok(B, x->a);
    case AST_NODE_CAST:
        if (x->aux == AST_CAST_VOID)
            return em_borrow_effect(B, x->a);
        if ((x->aux == AST_CAST_ARRAY_DECAY ||
             x->aux == AST_CAST_VECTOR_ADDRESS) &&
            em_borrow_rooted(B, x->a))
            return false;
        return em_borrow_ok(B, x->a);
    case AST_NODE_CALL: /* (a derived result is not to be used here) */
        return !em_borrow_call_derived(B, n) && em_borrow_call(B, n, false);
    case AST_NODE_BUILTIN:
        return em_borrow_list(B, x->b, x->c, 1, 0);
    case AST_NODE_CALL_INTO: /* the callee sees the object's address */
        return !em_borrow_rooted(B, x->a) && em_borrow_ok(B, x->a) &&
               em_borrow_ok(B, x->b);
    case AST_NODE_ASSIGN:
    case AST_NODE_PRE_INCREMENT:
    case AST_NODE_PRE_DECREMENT:
    case AST_NODE_POST_INCREMENT:
    case AST_NODE_POST_DECREMENT:
        if (em_borrow_rooted(B, x->a)) { /* a store through it */
            if (!em_borrow_parts(B, x->a))
                return false;
        } else if (!em_borrow_ok(B, x->a))
            return false;
        if (x->op == AST_NODE_ASSIGN)
            return em_borrow_ok(B, x->b);
        return true;
    case AST_NODE_UNARY:
    case AST_NODE_STATEMENT_EXPRESSION:
    case AST_NODE_COMPOUND_LITERAL:
        return em_borrow_ok(B, x->a);
    case AST_NODE_RETURN:
        if (!em_borrow_derived(B, x->a))
            return em_borrow_ok(B, x->a);
        if (!B->returns)
            return false;
        *B->returns = true;
        return em_borrow_parts(B, x->a);
    case AST_NODE_BINARY:
    case AST_NODE_LOGICAL_AND:
    case AST_NODE_LOGICAL_OR:
    case AST_NODE_COMPLEX:
    case AST_NODE_WHILE:
    case AST_NODE_DO:
    case AST_NODE_SWITCH:
        return em_borrow_ok(B, x->a) && em_borrow_ok(B, x->b);
    case AST_NODE_COMMA:
        return em_borrow_effect(B, x->a) && em_borrow_ok(B, x->b);
    case AST_NODE_CONDITIONAL:
    case AST_NODE_IF:
        return em_borrow_ok(B, x->a) && em_borrow_ok(B, x->b) &&
               em_borrow_ok(B, x->c);
    case AST_NODE_FOR:
        return em_borrow_ok(B, x->a) && em_borrow_ok(B, x->b) &&
               em_borrow_ok(B, c->lists[x->c]) &&
               em_borrow_ok(B, c->lists[x->c + 1]);
    case AST_NODE_CASE:
    case AST_NODE_DEFAULT:
    case AST_NODE_LABEL:
        return em_borrow_ok(B, x->c);
    case AST_NODE_EXPRESSION:
        return em_borrow_effect(B, x->a);
    case AST_NODE_DECLARATION:
        return em_borrow_declaration(B, x);
    case AST_NODE_BLOCK:
        return em_borrow_list(B, x->b, x->c, 1, 0);
    case AST_NODE_INITIALIZER:
        if (x->aux == 1)
            return em_borrow_list(B, x->b, x->c, 3, 2);
        return em_borrow_list(B, x->b, x->c, 1, 0);
    default:
        return false;
    }
}

/* the body is the one calls reach: internal linkage, or C++'s inline functions and template
   instances, whose definitions are the same everywhere ([basic.def.odr]); not a weak one that
   asks to be replaced (the C++ front end marks those noinline too).  C's external inline may
   have another definition in another unit */
static bool em_borrow_definition_is_final(const sym *y)
{
    if (y->sc == AST_STORAGE_STATIC)
        return true;
    if ((y->flags & AST_SYMBOL_FLAG_WEAK) &&
        (y->flags & AST_SYMBOL_FLAG_NOINLINE))
        return false;
    return y->flags & AST_SYMBOL_FLAG_LINKONCE;
}

/* parameter i of fn holds an address only during the call; with returns, fn may also return a
   derived value, which *returns then tells */
static bool em_parameter_borrows(cc *c, uint32_t fn, uint32_t i, int depth,
                                 bool *returns)
{
    const sym *y = &c->syms[fn];
    const node *body;

    if (depth > EM_BORROW_DEPTH_MAX || y->kind != AST_SYMBOL_FUNCTION ||
        !(y->flags & AST_SYMBOL_FLAG_DEFINED) || !y->body ||
        !em_borrow_definition_is_final(y) ||
        codegen_symbol_attributes(c, fn)->ifunc ||
        (T(c, y->ty)->flags &
         (AST_TYPE_FLAG_VARIADIC | AST_TYPE_FLAG_NO_PROTOTYPE)))
        return false;
    body = N(y->body);
    if (i >= body->aux)
        return false;

    em_borrow B = {c, c->lists[body->a + i], depth, returns, y->body, false};

    if (!codegen_type_is_pointer(c, c->syms[B.parameter].ty) ||
        codegen_symbol_attributes(c, B.parameter)->cleanup)
        return false;
    return em_borrow_ok(&B, y->body);
}

/* call argument i of a direct call to fn: an address it holds only during the call.  Returns
   the node that takes the address (an address-of or a decay), else 0 */
VPRIV uint32_t em_argument_borrowed(cc *c, uint32_t fn, uint32_t i,
                                    uint32_t arg)
{
    uint32_t n = arg;

    while (
        N(n)->op == AST_NODE_CAST &&
        (N(n)->aux == AST_CAST_NOP || N(n)->aux == AST_CAST_POINTER_TO_POINTER))
        n = N(n)->a;
    if (N(n)->op != AST_NODE_ADDRESS &&
        !(N(n)->op == AST_NODE_CAST && (N(n)->aux == AST_CAST_ARRAY_DECAY ||
                                        N(n)->aux == AST_CAST_VECTOR_ADDRESS)))
        return 0;
    if (!em_parameter_borrows(c, fn, i, 0, NULL))
        return 0;
    return n;
}
