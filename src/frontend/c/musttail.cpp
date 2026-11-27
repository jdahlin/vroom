/*
 * clang's musttail (__attribute__((musttail)), [[clang::musttail]]) on a return statement: the call
 * returned must be a tail call.  The rules are clang's (SemaStmt's checkMustTailAttr); code
 * generation guarantees the jump or gives an error (codegen/musttail.cpp).
 */
#include "cc-internal.h"

/* after the attribute list a, in a statement: musttail applies to the return statement that follows */
CC_PRIVATE void cc_musttail_attribute_note(cc *c, const attrs *a)
{
    if (!a->musttail)
        return;
    if (cc_token_is_keyword(c, KW_return) ||
        cc_token_is_keyword(c, KW___attribute__) ||
        cc_token_is_keyword(c, KW___attribute) ||
        cc_token_at_standard_attribute(c)) {
        c->musttail_loc = a->musttail;
        return;
    }
    cc_report_error(c, a->musttail,
                    "'musttail' attribute only applies to return statements");
}

/* clang's hasSimilarType, as far as it matters here: qualifiers aside at every level */
static bool cc_musttail_types_similar(cc *c, ast_qualified_type a,
                                      ast_qualified_type b)
{
    a = AST_QUALIFIED_TYPE_UNQUALIFIED(a);
    b = AST_QUALIFIED_TYPE_UNQUALIFIED(b);
    if (a == b)
        return true;
    if (AST_TYPE_KIND(c, a) == AST_TYPE_POINTER &&
        AST_TYPE_KIND(c, b) == AST_TYPE_POINTER)
        return cc_musttail_types_similar(c, AST_TYPE(c, a)->base,
                                         AST_TYPE(c, b)->base);
    return cc_type_is_compatible(c, a, b);
}

static const char *cc_musttail_ordinal_suffix(uint32_t n)
{
    if (n % 100 >= 11 && n % 100 <= 13)
        return "th";
    switch (n % 10) {
    case 1:
        return "st";
    case 2:
        return "nd";
    case 3:
        return "rd";
    default:
        return "th";
    }
}

/* the difference between the caller's and the callee's signature, as clang's note says it
   (false: none) */
static bool cc_musttail_signature_differs(cc *c, ast_qualified_type caller,
                                          ast_qualified_type callee, char *note,
                                          size_t capacity)
{
    const ctype *x = AST_TYPE(c, caller), *y = AST_TYPE(c, callee);
    char expected[256];

    if (!cc_musttail_types_similar(c, x->base, y->base)) {
        snprintf(expected, sizeof expected, "%s", tyname(c, x->base));
        snprintf(note, capacity,
                 "target function has different return type ('%s' expected but "
                 "has '%s')",
                 expected, tyname(c, y->base));
        return true;
    }
    if (x->n != y->n) {
        snprintf(note, capacity,
                 "target function has different number of parameters (expected "
                 "%u but has %u)",
                 x->n, y->n);
        return true;
    }
    for (uint32_t i = 0; i < x->n; i++) {
        ast_qualified_type p = c->params[x->aux + i], q = c->params[y->aux + i];

        if (cc_musttail_types_similar(c, p, q))
            continue;
        /* (clang's "expected" here is the target's) */
        snprintf(expected, sizeof expected, "%s", tyname(c, q));
        snprintf(note, capacity,
                 "target function has type mismatch at %u%s parameter "
                 "(expected '%s' but has '%s')",
                 i + 1, cc_musttail_ordinal_suffix(i + 1), expected,
                 tyname(c, p));
        return true;
    }
    return false;
}

/* a variable with a cleanup is in scope: it would have to run after the call */
static bool cc_musttail_cleanup_in_scope(cc *c, uint32_t *loc)
{
    for (uint32_t i = c->nsstack; i-- > 0;) {
        const sym *y = &c->syms[c->sstack[i]];

        if (y->kind == AST_SYMBOL_VARIABLE && y->attrs &&
            c->attr[y->attrs].cleanup) {
            *loc = y->loc;
            return true;
        }
    }
    return false;
}

/* the operand e of a return statement with musttail at attribute_loc: checked, and its call marked */
CC_PRIVATE void cc_musttail_return_check(cc *c, uint32_t e, uint32_t loc,
                                         uint32_t attribute_loc)
{
    uint32_t cleanup_loc = 0;

    if (e && AST_NODE_IS_ERROR(e))
        return;
    if (e && AST_NODE(e)->op == AST_NODE_BUILTIN) {
        cc_report_error(c, loc,
                        "'musttail' attribute: cannot guarantee a tail call to "
                        "a builtin function");
        return;
    }
    if (!e || AST_NODE(e)->op != AST_NODE_CALL) {
        cc_report_error(
            c, loc,
            "'musttail' attribute requires that the return value is "
            "the result of a function call");
        return;
    }
    if (!c->fn)
        return;

    uint32_t function = AST_NODE(e)->a;
    ast_qualified_type callee = AST_NODE(function)->ty;
    ast_qualified_type caller = c->syms[c->fn].ty;

    if (AST_TYPE_KIND(c, callee) == AST_TYPE_POINTER)
        callee = AST_TYPE(c, callee)->base;
    if (AST_TYPE_KIND(c, callee) != AST_TYPE_FUNCTION)
        return;

    uint32_t flags = AST_TYPE(c, caller)->flags | AST_TYPE(c, callee)->flags;

    if (flags & AST_TYPE_FLAG_VARIADIC) {
        cc_report_error(
            c, loc,
            "'musttail' attribute may not be used with variadic functions");
        return;
    }
    if (flags & AST_TYPE_FLAG_NO_PROTOTYPE) {
        cc_report_error(c, loc,
                        "'musttail' attribute requires that both caller and "
                        "callee functions have a prototype");
        return;
    }
    if (cc_musttail_cleanup_in_scope(c, &cleanup_loc)) {
        cc_report_error(
            c, loc, "cannot perform a tail call from this return statement");
        cc_report_note(
            c, cleanup_loc,
            "jump exits scope of variable with __attribute__((cleanup))");
        return;
    }

    uint32_t named = function, target = 0;

    while (AST_NODE(named)->op == AST_NODE_CAST ||
           AST_NODE(named)->op == AST_NODE_DEREFERENCE)
        named = AST_NODE(named)->a;
    if (AST_NODE(named)->op == AST_NODE_VARIABLE)
        target = AST_NODE(named)->a;

    char note[640], name[256];

    snprintf(name, sizeof name, "function");
    if (target)
        snprintf(name, sizeof name, "function '%.*s'",
                 CC_IDENTIFIER(c->syms[target].name));
    if (target && c->syms[target].kind == AST_SYMBOL_BUILTIN) {
        cc_report_error(c, loc,
                        "'musttail' attribute: cannot guarantee a tail call to "
                        "a builtin function");
        return;
    }
    if (cc_func_abi(c, caller) != cc_func_abi(c, callee)) {
        cc_report_error(c, loc,
                        "cannot perform a tail call to %s because it uses an "
                        "incompatible calling convention",
                        name);
        cc_report_note(c, attribute_loc,
                       "tail call required by 'musttail' attribute here");
        return;
    }
    if (cc_musttail_signature_differs(c, caller, callee, note, sizeof note)) {
        cc_report_error(c, loc,
                        "cannot perform a tail call to %s because its "
                        "signature is incompatible with the calling function",
                        name);
        if (target)
            cc_report_note(c, c->syms[target].loc, "%s", note);
        else
            cc_report_note(c, loc, "%s", note);
        cc_report_note(c, attribute_loc,
                       "tail call required by 'musttail' attribute here");
        return;
    }
    AST_NODE(e)->aux = AST_CALL_MUSTTAIL;
}

/* C++'s lowering: mark the call whose value the return statement's operand is (through the casts
   that change no bits, a reference's address of what it names and a comma's left operand);
   false: it is no call */
bool ast_builder_musttail(cc *c, uint32_t value)
{
    while (value) {
        const node *x = AST_NODE(value);

        if (x->op == AST_NODE_CALL) {
            AST_NODE(value)->aux = AST_CALL_MUSTTAIL;
            return true;
        }
        if (x->op == AST_NODE_CAST &&
            (x->aux == AST_CAST_NOP || x->aux == AST_CAST_POINTER_TO_POINTER ||
             x->aux == AST_CAST_VOID)) {
            value = x->a;
        } else if (x->op == AST_NODE_ADDRESS &&
                   AST_NODE(x->a)->op == AST_NODE_DEREFERENCE) {
            value = AST_NODE(x->a)->a;
        } else if (x->op == AST_NODE_COMMA) {
            value = x->b;
        } else {
            return false;
        }
    }
    return false;
}
