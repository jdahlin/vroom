/* vcc front end: builtins with a C body (doc/PLAN-builtins.md, "Executable builtin bodies").
 *
 * A builtin whose behaviour is ordinary C over other builtins has no id of its own, no case in the
 * front end and no arm in any code generator: its body is C text here, bound to its spellings below.
 * A call is replaced by the typed AST of the body, parsed with the C parser at the call (the
 * tokens go through the lazy-body token window, so nothing is preprocessed and a user's macros
 * cannot reach the body), before constant evaluation and optimization.  A translation unit that
 * does not call such a builtin pays nothing: the text is lexed and parsed only at a call.
 *
 * The parameters are names the parser resolves to AST_SYMBOL_BODY_ARGUMENT symbols; each
 * stands for the call's argument, converted to the parameter's type.  An argument that is a
 * constant (or, when the body writes through a pointer, only a constant) is substituted as it is;
 * anything else is evaluated once, in order, into a temporary before the body (the sequences of
 * cc_lowering_once), so a call on constants folds as the body's expression does.  The type name
 * `__T` in a body is the type the binding fixes (the builtin's result type, or the type of the first
 * argument), so one body serves the typed variants (addc, addcl, addcll ...) and the generic ones. */
#include "cc-internal.h"

enum {
    BODY_T_RESULT, /* __T: the builtin's result type (the signature's first letter) */
    BODY_T_ARGUMENT /* __T: the first argument's type, unqualified; __N: its precision in bits (an int constant) */
};

typedef struct cc_body_binding {
    const char *spelling;
    const char *
        params; /* one letter each: T the argument converted to __T, F a floating argument, U an unsigned integer argument (__T is its type), P a pointer (not converted) */
    const char *names; /* the parameters' names in the body, space separated */
    const char
        *text; /* the body: one expression, a statement expression for more */
    uint8_t type; /* BODY_T_* */
    bool
        stores; /* the body writes through a pointer: only constants are substituted */
    bool
        a64_native; /* the arm64 code generators lower it themselves, in one adds/adcs: not substituted there */
} cc_body_binding;

/* x + y + carry-in; the carry out is that of either addition (clang's two uadd.with.overflow) */
#define BODY_ADDC                                                              \
    "({ __T __r = (__T)(__x + __y); __T __s = (__T)(__r + __ci); "             \
    "*__co = (__T)((__r < __x) | (__s < __r)); __s; })"
#define BODY_SUBC                                                              \
    "({ __T __r = (__T)(__x - __y); __T __s = (__T)(__r - __ci); "             \
    "*__co = (__T)((__x < __y) | (__r < __ci)); __s; })"
/* (a -Wfloat-equal, if vcc gets one, must stay quiet for the text of a body) */
#define BODY_ISZERO "__x == 0"
#define BODY_COUNT_ONES "(unsigned)__builtin_popcountg(__x)"
#define BODY_TRAILING_ZEROS "(unsigned)__builtin_ctzg(__x, __N)"
#define BODY_ISSUBNORMAL                                                       \
    "__x != 0 && __builtin_isfinite(__x) && !__builtin_isnormal(__x)"

#define ADDC(name)                                                             \
    {name, "TTTP", "__x __y __ci __co", BODY_ADDC, BODY_T_RESULT, true, true}
#define SUBC(name)                                                             \
    {name, "TTTP", "__x __y __ci __co", BODY_SUBC, BODY_T_RESULT, true, true}

static const cc_body_binding cc_body_bindings[] = {
    ADDC("__builtin_addcb"),
    ADDC("__builtin_addcs"),
    ADDC("__builtin_addc"),
    ADDC("__builtin_addcl"),
    ADDC("__builtin_addcll"),
    SUBC("__builtin_subcb"),
    SUBC("__builtin_subcs"),
    SUBC("__builtin_subc"),
    SUBC("__builtin_subcl"),
    SUBC("__builtin_subcll"),
    {"__builtin_iszero", "F", "__x", BODY_ISZERO, BODY_T_ARGUMENT, false,
     false},
    {"__builtin_stdc_count_ones", "U", "__x", BODY_COUNT_ONES, BODY_T_ARGUMENT,
     false, false},
    {"__builtin_stdc_trailing_zeros", "U", "__x", BODY_TRAILING_ZEROS,
     BODY_T_ARGUMENT, false, false},
    {"__builtin_issubnormal", "F", "__x", BODY_ISSUBNORMAL, BODY_T_ARGUMENT,
     false, false},
};

CC_PRIVATE const cc_body_binding *cc_builtin_body_find(const cc *c,
                                                       const cc_builtin *b)
{
    if (!b->body)
        return NULL;
    for (size_t i = 0; i < sizeof cc_body_bindings / sizeof *cc_body_bindings;
         i++) {
        const cc_body_binding *k = &cc_body_bindings[i];

        if (strcmp(k->spelling, b->name))
            continue;
        if (k->a64_native &&
            (c->o->target == PP_LINUX_AARCH64 || c->o->target == PP_DARWIN))
            return NULL;
        return k;
    }
    return NULL;
}

CC_PRIVATE unsigned cc_builtin_body_arity(const cc_body_binding *k)
{
    return (unsigned)strlen(k->params);
}

enum { BODY_TEXT_MAX = 400, BODY_PARAMS_MAX = 6 };

/* the body's tokens, appended to the lazy-body token array: [*first, c->nlzt) */
static void cc_body_tokens_append(cc *c, const char *text, uint32_t loc,
                                  uint32_t *first)
{
    char src[BODY_TEXT_MAX + 160];
    lex_token toks[BODY_TEXT_MAX + 2];
    uint64_t scratch[512];
    size_t n = strlen(text);
    uint32_t count;

    if (n >= BODY_TEXT_MAX || lex_scratch_size(n) > sizeof scratch)
        VBUG("a builtin body is too long");
    memset(src, 0, sizeof src);
    memcpy(src, text, n);
    count = lex_tokenize(src, n, toks, scratch);
    *first = c->nlzt;
    for (uint32_t i = 0; i < count; i++) {
        ptok t;

        memset(&t, 0, sizeof t);
        t.s = src + toks[i].off;
        t.len = toks[i].len;
        t.id = toks[i].id;
        t.loc = loc;
        t.kind = toks[i].kind;
        t.flags = toks[i].flags & (uint8_t)~LEX_TOKEN_FLAG_SPLICED;
        cc_lazy_body_token_append(c, &t);
    }
}

/* the body's expression, parsed in the scope the caller made */
static uint32_t cc_body_parse(cc *c, const char *text, uint32_t loc)
{
    ptok saved[4];
    int ntk = c->ntk;
    uint32_t prevloc = c->prevloc, first, n;
    uint8_t prevflags = c->prevflags;
    bool lz_on = c->lz_on, lz_hc = c->lz_hc;
    uint32_t lzc = c->lzc, lze = c->lze;
    pp_lazy lzz = c->lzz;
    struct cc_recovery_point *rec = c->rec;

    uint32_t fn = c->fn;

    cc_body_tokens_append(c, text, loc, &first);
    memcpy(saved, c->tk, sizeof saved);
    if (!fn) {
        /* the C++ front end lowers with no function being parsed; a statement
           expression wants one */
        c->fn = cc_symbol_new(c, AST_SYMBOL_FUNCTION, 0,
                              cc_type_basic(AST_TYPE_VOID), loc);
    }
    c->in_body++;
    c->lz_on = true;
    c->lz_hc = false;
    c->lzc = first;
    c->lze = c->nlzt;
    c->rec = NULL; /* another token stream: no recovery outside the body */
    cc_token_fetch(c, &c->tk[0]);
    c->ntk = 1;
    n = expr(c);
    c->in_body--;
    c->fn = fn;
    if (!cc_token_is(c, LEX_TOKEN_EOF))
        cc_report_error(
            c, loc, "internal error: builtin body '%s' is not one expression",
            text);
    c->rec = rec;
    c->lz_on = lz_on;
    c->lz_hc = lz_hc;
    c->lzc = lzc;
    c->lze = lze;
    c->lzz = lzz;
    memcpy(c->tk, saved, sizeof saved);
    c->ntk = ntk;
    c->prevloc = prevloc;
    c->prevflags = prevflags;
    return n;
}

static bool cc_body_is_constant(cc *c, uint32_t n)
{
    const node *x = AST_NODE(n);

    if (x->op == AST_NODE_NUMBER || x->op == AST_NODE_FLOAT_NUMBER)
        return true;
    return x->op == AST_NODE_CAST && x->a && cc_body_is_constant(c, x->a);
}

CC_PRIVATE uint32_t cc_builtin_body_apply(cc *c, const cc_body_binding *k,
                                          const cc_builtin *b,
                                          const uint32_t *args, uint32_t n,
                                          uint32_t loc)
{
    ast_qualified_type result = cc_builtin_signature_type(c, b->sig[0]), t;
    uint32_t value[BODY_PARAMS_MAX], pre[BODY_PARAMS_MAX], e;
    unsigned np = cc_builtin_body_arity(k);
    const char *name = k->names;

    if (n != np || n > BODY_PARAMS_MAX) {
        cc_report_delayed_bug(c, loc, "builtin body: argument count");
        return cc_node_error_new(c, loc);
    }
    for (uint32_t i = 0; i < n; i++)
        if (AST_NODE_IS_ERROR(args[i]))
            return cc_node_error_new(c, loc);
    t = result;
    if (k->type == BODY_T_ARGUMENT)
        t = AST_QUALIFIED_TYPE_UNQUALIFIED(
            AST_NODE(cc_conversion_decay(c, args[0]))->ty);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t a = cc_conversion_decay(c, args[i]);

        switch (k->params[i]) {
        case 'F':
            if (!cc_type_is_float(c, AST_NODE(a)->ty)) {
                cc_report_error(c, AST_NODE(a)->loc,
                                "floating point classification requires "
                                "argument of floating point type (passed in "
                                "'%s')",
                                tyname(c, AST_NODE(a)->ty));
                return cc_node_error_new(c, loc);
            }
            break;
        case 'U':
            if (!cc_type_is_integer(c, AST_NODE(a)->ty) ||
                cc_type_is_signed(c, AST_NODE(a)->ty) ||
                AST_TYPE_KIND(c, AST_NODE(a)->ty) == AST_TYPE_BOOL ||
                AST_TYPE_KIND(c, AST_NODE(a)->ty) == AST_TYPE_ENUM) {
                cc_report_error(
                    c, AST_NODE(a)->loc,
                    "1st argument must be a scalar unsigned integer type");
                return cc_node_error_new(c, loc);
            }
            break;
        case 'P':
            if (!cc_type_is_pointer(c, AST_NODE(a)->ty)) {
                cc_report_error(
                    c, loc, "carry-out argument to builtin must be a pointer");
                return cc_node_error_new(c, loc);
            }
            break;
        default:
            a = cc_conversion_apply(c, a, t);
            break;
        }
        /* once, in order: a constant as it is, a name that cannot change
           (no store in the body) as it is, anything else in a temporary */
        pre[i] = 0;
        value[i] = a;
        if (!cc_body_is_constant(c, a) &&
            (k->stores || !cc_lowering_is_pure(c, a, 0)))
            value[i] = cc_lowering_save(c, a, &pre[i]);
    }
    cc_scope_open(c);
    {
        uint32_t s =
            cc_symbol_new(c, AST_SYMBOL_TYPEDEF, vintern_cstr("__T"), t, loc);

        c->syms[s].sc = AST_STORAGE_TYPEDEF;
        c->syms[s].flags |=
            AST_SYMBOL_FLAG_REFERENCED; /* (no -Wunused-local-typedef) */
        cc_symbol_bind(c, s);
    }
    if (k->type == BODY_T_ARGUMENT && cc_type_is_integer(c, t)) {
        uint32_t s =
            cc_symbol_new(c, AST_SYMBOL_BODY_ARGUMENT, vintern_cstr("__N"),
                          cc_type_basic(AST_TYPE_INT), loc);

        c->syms[s].body = cc_node_integer_new(c, cc_type_integer_bits(c, t),
                                              cc_type_basic(AST_TYPE_INT), loc);
        cc_symbol_bind(c, s);
    }
    for (uint32_t i = 0; i < n; i++) {
        char word[16];
        size_t len = 0;
        uint32_t s;

        while (name[len] && name[len] != ' ')
            len++;
        if (len >= sizeof word)
            VBUG("a builtin body parameter name is too long");
        memcpy(word, name, len);
        word[len] = 0;
        name += len + (name[len] == ' ');
        s = cc_symbol_new(c, AST_SYMBOL_BODY_ARGUMENT, vintern_cstr(word),
                          AST_NODE(value[i])->ty, loc);
        c->syms[s].body = value[i];
        cc_symbol_bind(c, s);
    }
    e = cc_body_parse(c, k->text, loc);
    cc_scope_close_prototype(c);
    if (!AST_NODE_IS_ERROR(e))
        e = cc_conversion_apply(c, cc_node_rvalue(c, e), result);
    for (int i = (int)n - 1; i >= 0; i--)
        e = cc_lowering_sequence(c, pre[i], e);
    return e;
}
