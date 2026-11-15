/* C++ expressions: primary expressions, parentheses, folds, __builtin_invoke */
#include "expression.h"

VPRIV bool cxx_is_fold_operator(int k)
{
    switch (k) {
    case LEX_TOKEN_PLUS:
    case LEX_TOKEN_MINUS:
    case LEX_TOKEN_STAR:
    case LEX_TOKEN_SLASH:
    case LEX_TOKEN_PERCENT:
    case LEX_TOKEN_CARET:
    case LEX_TOKEN_AMPERSAND:
    case LEX_TOKEN_PIPE:
    case LEX_TOKEN_LESS_LESS:
    case LEX_TOKEN_GREATER_GREATER:
    case LEX_TOKEN_PLUS_EQUAL:
    case LEX_TOKEN_MINUS_EQUAL:
    case LEX_TOKEN_STAR_EQUAL:
    case LEX_TOKEN_SLASH_EQUAL:
    case LEX_TOKEN_PERCENT_EQUAL:
    case LEX_TOKEN_CARET_EQUAL:
    case LEX_TOKEN_AMPERSAND_EQUAL:
    case LEX_TOKEN_PIPE_EQUAL:
    case LEX_TOKEN_LESS_LESS_EQUAL:
    case LEX_TOKEN_GREATER_GREATER_EQUAL:
    case LEX_TOKEN_EQUAL:
    case LEX_TOKEN_EQUAL_EQUAL:
    case LEX_TOKEN_EXCLAMATION_EQUAL:
    case LEX_TOKEN_LESS:
    case LEX_TOKEN_GREATER:
    case LEX_TOKEN_LESS_EQUAL:
    case LEX_TOKEN_GREATER_EQUAL:
    case LEX_TOKEN_AMPERSAND_AMPERSAND:
    case LEX_TOKEN_PIPE_PIPE:
    case LEX_TOKEN_COMMA:
    case CXX_TOKEN_PERIOD_STAR:
    case CXX_TOKEN_ARROW_STAR:
        return true;
    }
    return false;
}

/* ( ... ) : a parenthesized expression or a fold expression */
VPRIV void cxx_paren_expression_parse(cxx *c)
{
    uint32_t targs = c->in_template_args;

    uint32_t implicit = c->implicit_typename;

    cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
    c->in_template_args = 0;
    c->implicit_typename = 0;
    if (cxx_is(c, LEX_TOKEN_ELLIPSIS)) { /* ( ... op e ) */
        uint32_t n;

        cxx_advance(c);
        if (!cxx_is_fold_operator(cxx_operator_at(c, &n)))
            cxx_fail(c, "expected a fold operator");
        while (n--)
            cxx_advance(c);
        cxx_cast_expression_parse(c);
        cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
        c->in_template_args = targs;
        c->implicit_typename = implicit;
        return;
    }
    if (cxx_is(c,
               LEX_TOKEN_LEFT_BRACE)) { /* GNU statement expression ({ ... }) */
        uint32_t sloc = cxx_peek(c, 0)->loc, outer_snode = c->snode;
        bool gen = cxx_gen_on(c);

        cxx_compound_statement_parse(c);
        if (gen) {
            /* code generation: the block, its value its last statement's when that is an expression of a
			   scalar type, void or a class of plain data; not built otherwise: another class's value is copied out before the block's
			   objects are destroyed, which the C node does not do */
            uint32_t blk = cxx_gen_on(c) ? c->snode : 0, n = 0;
            const node *bx = blk ? AST_NODE_AT(c->ast, blk) : NULL;

            if (bx && bx->op == AST_NODE_BLOCK && bx->c) {
                const node *last =
                    AST_NODE_AT(c->ast, c->ast->lists[bx->b + bx->c - 1]);
                ast_qualified_type t =
                    last->op == AST_NODE_EXPRESSION && last->a
                        ? AST_NODE_AT(c->ast, last->a)->ty
                        : cc_type_basic(AST_TYPE_VOID);

                /* (a class of plain data, copied as C copies it, too) */
                bool plain =
                    !cc_type_is_record(c->ast, t) ||
                    (AST_TYPE_KIND(c->ast, t) != AST_TYPE_ARRAY &&
                     cxx_gen_type_ok(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t)) &&
                     !cxx_gen_needs_construction_public(
                         c, AST_QUALIFIED_TYPE_UNQUALIFIED(t)));

                if (plain && c->etype != CXX_TY_UNKNOWN) {
                    n = cc_node_new(c->ast, AST_NODE_STATEMENT_EXPRESSION,
                                    AST_QUALIFIED_TYPE_UNQUALIFIED(t), sloc);
                    AST_NODE_AT(c->ast, n)->a = blk;
                }
            } else if (bx && bx->op == AST_NODE_BLOCK &&
                       c->etype != CXX_TY_UNKNOWN &&
                       AST_TYPE_KIND(c->ast, c->etype) == AST_TYPE_VOID) {
                n = cc_node_new(c->ast, AST_NODE_STATEMENT_EXPRESSION,
                                cc_type_basic(AST_TYPE_VOID), sloc);
                AST_NODE_AT(c->ast, n)->a = blk;
            }
            c->snode = outer_snode;
            if (!n)
                cxx_gen_fail(c);
            c->enode = n;
        }
        cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
        c->in_template_args = targs;
        c->implicit_typename = implicit;
        return;
    }
    cxx_expression_parse(c);
    uint32_t n;

    if (cxx_is_fold_operator(cxx_operator_at(c, &n)) &&
        cxx_peek(c, n)->kind == LEX_TOKEN_ELLIPSIS) { /* ( e op ... [op e] ) */
        while (n--)
            cxx_advance(c);
        cxx_advance(c);
        if (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) {
            cxx_operator_at(c, &n);
            while (n--)
                cxx_advance(c);
            cxx_cast_expression_parse(c);
        }
    }
    cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
    c->in_template_args = targs;
    c->implicit_typename = implicit;
}

/*
 * __builtin_invoke(f, args...) (libc++'s std::__invoke, __invoke_result, is_invocable): the call f(args...)
 * for a function, a pointer to one or an object with operator() (pointers to members: not known); not
 * callable with these arguments: invalid, a substitution failure where one counts.
 */
VPRIV void cxx_builtin_invoke_parse(cxx *c)
{
    uint32_t loc = cxx_peek(c, 0)->loc, n, nodes[CXX_MAX_ARGS];
    ast_qualified_type at[CXX_MAX_ARGS];
    uint8_t lv[CXX_MAX_ARGS];
    ast_qualified_type f;
    bool known = true;

    cxx_advance(c);
    cxx_call_arguments_parse(c);
    n = c->ncargs;
    cxx_et(c, CXX_TY_UNKNOWN, false);
    if (n == UINT32_MAX || n == 0 || n > CXX_MAX_ARGS || c->tentative)
        return;
    memcpy(nodes, c->carg_nodes, n * sizeof *nodes),
        memcpy(at, c->cargs, n * sizeof *at), memcpy(lv, c->cargs_lval, n);
    for (uint32_t i = 0; i < n; i++)
        known &= at[i] != CXX_TY_UNKNOWN && !cxx_type_has_placeholder(c, at[i]);
    f = at[0];
    if (f == CXX_TY_UNKNOWN)
        return;
    if (AST_TYPE_KIND(c->ast, f) == AST_TYPE_POINTER &&
        AST_TYPE(c->ast, f)->ext)
        f = AST_TYPE(c->ast, f)->base;
    if (cc_type_is_record(c->ast, f) &&
        AST_TYPE_KIND(c->ast, f) != AST_TYPE_ARRAY) {
        c->op_lv_set = 1, c->op_lv[0] = lv[0];
        memcpy(c->op_lv + 1, lv + 1, n - 1);
        if (cxx_operator_call_type(c, vintern_cstr("operator()"), f, at + 1,
                                   n - 1)) {
            uint32_t obj = nodes[0];

            if (!obj && c->gen)
                obj = cxx_gen_empty_object(c, f, loc);
            cxx_gen_operator_call(c, obj, nodes + 1, n - 1, loc);
        } else if (known && !c->template_depth)
            CXX_INVALID(c); /* (no operator() takes these) */
        return;
    }
    if (AST_TYPE_KIND(c->ast, f) == AST_TYPE_FUNCTION ||
        (AST_TYPE_KIND(c->ast, f) == AST_TYPE_POINTER &&
         !AST_TYPE(c->ast, f)->ext &&
         AST_TYPE_KIND(c->ast, AST_TYPE(c->ast, f)->base) ==
             AST_TYPE_FUNCTION)) {
        ast_qualified_type ft = AST_TYPE_KIND(c->ast, f) == AST_TYPE_FUNCTION
                                    ? f
                                    : AST_TYPE(c->ast, f)->base;
        const ctype *fx = AST_TYPE(c->ast, ft);

        if ((fx->flags & AST_TYPE_FLAG_VARIADIC) ? n - 1 < fx->n
                                                 : n - 1 != fx->n) {
            if (known && !c->template_depth)
                CXX_INVALID(
                    c); /* (a function of another arity: a pointer to one has no default arguments) */
            return;
        }
        cxx_call_type(c, NULL, ft, at + 1, lv + 1, n - 1);
        if (c->etype != CXX_TY_UNKNOWN && nodes[0])
            cxx_gen_indirect_call(c, nodes[0], nodes + 1, n - 1, loc);
        else if (c->etype == CXX_TY_UNKNOWN && known && !c->template_depth &&
                 AST_TYPE(c->ast, ft)->n != n - 1 &&
                 !(AST_TYPE(c->ast, ft)->flags & AST_TYPE_FLAG_VARIADIC))
            CXX_INVALID(c); /* (a function of another arity) */
        return;
    }
    if (known && !c->template_depth &&
        ((AST_TYPE_KIND(c->ast, f) >= AST_TYPE_BOOL &&
          AST_TYPE_KIND(c->ast, f) <= AST_TYPE_FLOAT128) ||
         AST_TYPE_KIND(c->ast, f) == AST_TYPE_ENUM))
        CXX_INVALID(
            c); /* (an arithmetic value: nothing to call; pointers to members: not decided) */
}

/* the ud-suffix of a string literal's piece: what follows its closing quote (an identifier, _json), or 0 */
static uint32_t cxx_string_suffix_length(const cxx_token *x)
{
    uint32_t q = x->len;

    while (q > 0 && x->s[q - 1] != '"')
        q--;
    if (!q || q == x->len ||
        !(x->s[q] == '_' || isalpha((unsigned char)x->s[q])))
        return 0;
    return x->len - q;
}

/*
 * "text"_suffix (a user-defined string literal, [lex.ext]/5): operator""_suffix("text", length), the
 * literal as without its suffix (the pieces' suffixes left out while it is parsed); false: no suffix here.
 */
static bool cxx_user_defined_string_parse(cxx *c)
{
    uint32_t b = c->pos, e = b, sl = 0, loc = cxx_peek(c, 0)->loc;
    const cxx_token *sx = NULL;

    while (e < c->ntok && c->tok[e].kind >= LEX_TOKEN_STRING &&
           c->tok[e].kind <= LEX_TOKEN_UTF32_STRING) {
        uint32_t k = cxx_string_suffix_length(&c->tok[e]);

        if (k)
            sl = k, sx = &c->tok[e];
        e++;
    }
    if (!sx)
        return false;
    char name[256];
    uint32_t keep[64], n = e - b;

    if (n > 64 || 10 + sl >= sizeof name)
        return false;
    memcpy(name, "operator\"\"", 10);
    memcpy(name + 10, sx->s + sx->len - sl, sl);
    uint32_t id = vintern_copy(name, 10 + sl);

    for (uint32_t i = 0; i < n; i++) {
        keep[i] = c->tok[b + i].len;
        c->tok[b + i].len -= cxx_string_suffix_length(&c->tok[b + i]);
    }
    cxx_primary_expression_parse(c); /* (the string, its suffix not seen) */
    for (uint32_t i = 0; i < n; i++)
        c->tok[b + i].len = keep[i];

    ast_qualified_type st = c->etype;
    uint32_t sn = c->enode;
    cxx_entity *set = cxx_lookup(c, id);

    if (!set || st == CXX_TY_UNKNOWN ||
        AST_TYPE_KIND(c->ast, st) != AST_TYPE_ARRAY || c->tentative) {
        cxx_et(c, CXX_TY_UNKNOWN, false);
        return true;
    }
    uint64_t len = AST_TYPE(c->ast, st)->n ? AST_TYPE(c->ast, st)->n - 1 : 0;
    ast_qualified_type at[2] = {
        cc_type_pointer(c->ast, AST_TYPE(c->ast, st)->base),
        cc_type_basic(AST_TYPE_UNSIGNED_LONG)};
    uint8_t lv[2] = {0, 0};

    cxx_call_type(c, set, CXX_TY_UNKNOWN, at, lv, 2);
    if (c->call_resolved && c->call_picked && c->etype != CXX_TY_UNKNOWN &&
        cxx_gen_on(c)) {
        uint32_t an[2] = {
            sn ? cc_conversion_decay(c->ast, sn) : 0,
            cc_node_integer_new(c->ast, (i128)len,
                                cc_type_basic(AST_TYPE_UNSIGNED_LONG), loc)};

        if (an[0])
            cxx_gen_call(c, c->call_picked, 0, false, an, 2, loc);
        else
            cxx_gen_fail(c);
    }
    return true;
}

/*
 * A string literal (its pieces concatenated): an lvalue of const E[N], E the C++ type of its kind
 * (char, wchar_t, char8_t in C++20, char16_t, char32_t: each its own type here); the bytes decoded
 * by C's cc_literal_string_build (escapes, \\u, UTF-8 to wide, raw strings).
 */
static void cxx_string_literal_parse(cxx *c)
{
    ptok local[16], *pt = local;
    uint32_t n = 0, cap = 16, loc = cxx_peek(c, 0)->loc;
    int kind = LEX_TOKEN_STRING;

    while (cxx_kind(c) >= LEX_TOKEN_STRING &&
           cxx_kind(c) <= LEX_TOKEN_UTF32_STRING) {
        const cxx_token *x = cxx_peek(c, 0);

        if (n == cap) {
            ptok *more = (ptok *)malloc((cap *= 2) * sizeof *more);

            memcpy(more, pt, n * sizeof *more);
            if (pt != local)
                free(pt);
            pt = more;
        }
        memset(&pt[n], 0, sizeof pt[n]);
        pt[n].s = x->s, pt[n].len = x->len, pt[n].loc = x->loc,
        pt[n].kind = (uint8_t)x->kind;
        n++;
        if (x->kind != LEX_TOKEN_STRING)
            kind = x->kind;
        cxx_advance(c);
    }
    /* the element type: C++'s distinct character types (ext 1), const */
    int ck = kind == LEX_TOKEN_WIDE_STRING    ? AST_TYPE_INT
             : kind == LEX_TOKEN_UTF16_STRING ? AST_TYPE_UNSIGNED_SHORT
             : kind == LEX_TOKEN_UTF32_STRING ? AST_TYPE_UNSIGNED_INT
             : kind == LEX_TOKEN_UTF8_STRING && c->o->standard >= 20 &&
                     c->o->standard != 98
                 ? AST_TYPE_UNSIGNED_CHAR
                 : AST_TYPE_CHAR;
    ast_qualified_type et = cc_type_basic(ck);

    if (ck != AST_TYPE_CHAR) {
        ctype t = c->ast->ty[ck];

        t.ext = 1;
        et = AST_QUALIFIED_TYPE(cc_type_intern(c->ast, &t), 0);
    }
    et = cc_type_qualify(c->ast, et, AST_QUALIFIER_CONST);
    if (c->tentative) {
        cxx_et(c, CXX_TY_UNKNOWN, false);
        if (pt != local)
            free(pt);
        return;
    }
    uint32_t sn = cc_literal_string_build(c->ast, pt, n, loc);
    ast_qualified_type at = cc_type_array(
        c->ast, et, AST_TYPE(c->ast, AST_NODE_AT(c->ast, sn)->ty)->n, 0);

    if (pt != local)
        free(pt);
    AST_NODE_AT(c->ast, sn)->ty = at;
    cxx_et(c, at, true);
    if (cxx_gen_on(c))
        c->enode = sn;
}

VPRIV void cxx_primary_expression_parse(cxx *c)
{
    cxx_token *t = cxx_peek(c, 0);

    if (t->kind >= LEX_TOKEN_STRING && t->kind <= LEX_TOKEN_UTF32_STRING &&
        cxx_user_defined_string_parse(c))
        return;

    if (t->kind == LEX_TOKEN_IDENTIFIER && t->len == 16 &&
        !memcmp(t->s, "__builtin_invoke", 16) &&
        cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN) {
        cxx_builtin_invoke_parse(c);
        return;
    }
    if (t->kind == LEX_TOKEN_COLON_COLON &&
        cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER &&
        cxx_peek(c, 1)->len > 10 &&
        !memcmp(cxx_peek(c, 1)->s, "__builtin_", 10) &&
        cxx_peek(c, 2)->kind == LEX_TOKEN_LEFT_PAREN &&
        !cxx_lookup_in(c, c->global, cxx_peek(c, 1)->id, 0)) {
        cxx_advance(
            c); /* ::__builtin_memmove(...): the builtin, as unqualified (no declaration in the global namespace) */
        t = cxx_peek(c, 0);
    }
    cxx_et(c, CXX_TY_UNKNOWN, false);
    switch (t->kind) {
    case LEX_TOKEN_NUMBER:
        cxx_advance(c);
        {
            int64_t v;
            /* GNU's imaginary suffix (2i, 1.5if): the number without it, then 0 + v i, C's complex node */
            char ib[128];
            cxx_token it = *t;
            bool imag = false;

            for (uint32_t k = t->len; k > 0 && t->len < sizeof ib &&
                                      strchr("fFlLuUiIjJ", t->s[k - 1]);
                 k--)
                if (strchr("iIjJ", t->s[k - 1]) &&
                    !(t->len > 1 && (t->s[1] | 32) == 'x' &&
                      strchr("fF", t->s[k - 1]))) {
                    memcpy(ib, t->s, k - 1),
                        memcpy(ib + k - 1, t->s + k, t->len - k);
                    memset(ib + t->len - 1, 0, 16);
                    it.s = ib, it.len = t->len - 1, imag = true;
                    break;
                }
            if (imag) {
                ast_qualified_type et = cxx_number_type(c, &it),
                                   ct = cc_type_complex(
                                       c->ast,
                                       AST_QUALIFIED_TYPE_UNQUALIFIED(et));

                if (cxx_number_value(&it, &v))
                    cxx_ec(c, et, v);
                else if (!c->tentative)
                    cxx_et(c, et, false),
                        cxx_gen_floating(c, it.s, it.len, et, t->loc);
                uint32_t vn = c->enode;

                cxx_et(c, ct, false);
                if (vn && cxx_gen_on(c))
                    c->enode = cc_complex_node_new(
                        c->ast, ct,
                        cc_lowering_zero(
                            c->ast, AST_QUALIFIED_TYPE_UNQUALIFIED(et), t->loc),
                        vn, t->loc);
                return;
            }
            ast_qualified_type nt = cxx_number_type(c, t);

            if (cxx_number_value(t, &v))
                cxx_ec(c, nt, v);
            else if (!c->tentative) {
                cxx_et(c, nt, false);
                cxx_gen_floating(c, t->s, t->len, nt, t->loc);
            }
        }
        return;
    case LEX_TOKEN_CHARACTER:
    case LEX_TOKEN_WIDE_CHARACTER:
    case LEX_TOKEN_UTF8_CHARACTER:
    case LEX_TOKEN_UTF16_CHARACTER:
    case LEX_TOKEN_UTF32_CHARACTER:
        cxx_advance(c);
        if (t->s[t->len - 1] == '\'' && !c->tentative &&
            t->kind != LEX_TOKEN_CHARACTER) {
            /* (no user-defined suffix) a prefixed one: the value as C decodes it, of C++'s character type:
               wchar_t, char8_t (C++20), char16_t, char32_t ('x' is a char below, not C's int) */
            ptok pt;
            ast_qualified_type ct;

            memset(&pt, 0, sizeof pt);
            pt.s = t->s, pt.len = t->len, pt.loc = t->loc,
            pt.kind = (uint8_t)t->kind;
            int64_t v = (int64_t)cc_literal_character_value(c->ast, &pt, &ct);
            int ck =
                t->kind == LEX_TOKEN_WIDE_CHARACTER    ? AST_TYPE_INT
                : t->kind == LEX_TOKEN_UTF16_CHARACTER ? AST_TYPE_UNSIGNED_SHORT
                : t->kind == LEX_TOKEN_UTF32_CHARACTER ? AST_TYPE_UNSIGNED_INT
                : t->kind == LEX_TOKEN_UTF8_CHARACTER
                    ? (c->o->standard >= 20 && c->o->standard != 98
                           ? AST_TYPE_UNSIGNED_CHAR
                           : AST_TYPE_CHAR)
                    : (int)AST_TYPE_KIND(c->ast, ct);

            ast_qualified_type et = cc_type_basic(ck);

            if (t->kind != LEX_TOKEN_CHARACTER &&
                !(t->kind == LEX_TOKEN_UTF8_CHARACTER && ck == AST_TYPE_CHAR)) {
                ctype x = c->ast->ty[ck];

                x.ext = 1;
                et = AST_QUALIFIED_TYPE(cc_type_intern(c->ast, &x), 0);
            }
            cxx_ec(c, et, v);
            return;
        }
        if (t->kind == LEX_TOKEN_CHARACTER &&
            t->s[t->len - 1] == '\'') { /* (no user-defined suffix) */
            int64_t cv = 0;
            bool known = false;

            /* 'x', the simple escapes, and the hex and octal ones ('\x1F', '\101': one byte, a char's value, signed
			   as clang's): a constant (the others: a char of a value not computed here) */
            if (t->len == 3 && t->s[1] != '\\')
                cv = (signed char)t->s[1], known = true;
            else if (t->len == 4 && t->s[1] == '\\' &&
                     !(t->s[2] >= '0' && t->s[2] <= '7')) {
                static const char from[] = "ntr\\'\"abfv?",
                                  to[] = "\n\t\r\\'\"\a\b\f\v?";
                const char *p =
                    (const char *)memchr(from, t->s[2], sizeof from - 1);

                if (p)
                    cv = to[p - from], known = true;
            }
            if (!known && t->len >= 4 && t->s[1] == '\\' &&
                (t->s[2] == 'x' || (t->s[2] >= '0' && t->s[2] <= '7'))) {
                bool hex = t->s[2] == 'x';
                uint32_t v = 0, i = hex ? 3 : 2, n = 0;

                for (; i + 1 < t->len &&
                       (hex ? isxdigit((unsigned char)t->s[i]) != 0
                            : (t->s[i] >= '0' && t->s[i] <= '7' && n < 3));
                     i++, n++) {
                    unsigned char d = (unsigned char)t->s[i];

                    v = hex ? v * 16 +
                                  (d <= '9' ? d - '0' : (d | 32) - 'a' + 10)
                            : v * 8 + (d - '0');
                    if (v > 255)
                        break;
                }
                if (n && i + 1 == t->len && v <= 255)
                    cv = (signed char)v, known = true;
            }
            if (known)
                cxx_ec(c, cc_type_basic(AST_TYPE_CHAR), cv);
            else
                cxx_et(c, cc_type_basic(AST_TYPE_CHAR), false);
        }
        return;
    case LEX_TOKEN_STRING:
    case LEX_TOKEN_WIDE_STRING:
    case LEX_TOKEN_UTF8_STRING:
    case LEX_TOKEN_UTF16_STRING:
    case LEX_TOKEN_UTF32_STRING:
        cxx_string_literal_parse(c);
        return;
    case LEX_TOKEN_LEFT_PAREN:
        cxx_paren_expression_parse(c);
        c->edecl_name = 0; /* decltype((x)) is a reference */
        return;
    case LEX_TOKEN_LEFT_SQUARE:
        cxx_lambda_parse(
            c); /* (its returns are its own: the enclosing function's capture is kept) */
        {
            uint32_t ln = c->lambda_node;

            cxx_et(
                c, c->lambda_type,
                false); /* its closure type, when built (not its body's last expression's) */
            c->enode = ln;
        }
        return;
    case LEX_TOKEN_LEFT_BRACE:
        cxx_braced_init_list_parse(c);
        cxx_et(c, CXX_TY_UNKNOWN, false);
        return;
    case LEX_TOKEN_IDENTIFIER:
    case LEX_TOKEN_COLON_COLON:
    case LEX_TOKEN_TILDE:
        break;
    default:
        cxx_fail(c, "expected an expression");
    }
    switch (t->kw) {
    case CXX_KW_true:
    case CXX_KW_false:
    case CXX_KW_nullptr:
    case CXX_KW___null:
    case CXX_KW_this:
    case CXX_KW___nullptr:
    case CXX_KW___builtin_LINE:
    case CXX_KW___builtin_FILE:
    case CXX_KW___builtin_FUNCTION:
    case CXX_KW___builtin_COLUMN:
    case CXX_KW___builtin_FILE_NAME:
        cxx_advance(c);
        if (t->kw >= CXX_KW___builtin_LINE &&
            t->kw <= CXX_KW___builtin_FILE_NAME &&
            cxx_is(c, LEX_TOKEN_LEFT_PAREN))
            cxx_skip_balanced(c);
        if (t->kw == CXX_KW_true || t->kw == CXX_KW_false)
            cxx_ec(c, cc_type_basic(AST_TYPE_BOOL), t->kw == CXX_KW_true);
        else if (t->kw == CXX_KW___null) {
            /* NULL: the integer constant 0 of type long (a null pointer constant, as GCC's) */
            cxx_ec(c, cc_type_basic(AST_TYPE_LONG), 0);
            if (!c->tentative)
                cxx_gen_integer(c, 0, cc_type_basic(AST_TYPE_LONG), t->loc);
        } else if ((t->kw == CXX_KW_nullptr || t->kw == CXX_KW___nullptr) &&
                   !c->tentative) {
            cxx_et(c, cxx_nullptr_type(c), false);
            cxx_gen_nullptr(c, t->loc);
        } else if (t->kw == CXX_KW_this && c->this_type != CXX_TY_UNKNOWN &&
                   c->this_type && !c->tentative) {
            /* (a const member function's: a const T *) */
            cxx_et(c,
                   cc_type_pointer(
                       c->ast,
                       c->this_const
                           ? AST_QUALIFIED_TYPE(
                                 AST_QUALIFIED_TYPE_INDEX(c->this_type),
                                 AST_QUALIFIED_TYPE_QUALIFIERS(c->this_type) |
                                     AST_QUALIFIER_CONST)
                           : c->this_type),
                   false);
            cxx_gen_this(c, t->loc);
        } else if (t->kw >= CXX_KW___builtin_LINE &&
                   t->kw <= CXX_KW___builtin_FILE_NAME && !c->tentative) {
            /* the builtin's own location (clang's: the caller's in a default argument; only messages differ) */
            const char *name = "";
            uint32_t nl = 0, line = 0, col = 0;

            pp_location_describe(c->p, t->loc, &name, &nl, &line, &col);
            if (t->kw == CXX_KW___builtin_LINE ||
                t->kw == CXX_KW___builtin_COLUMN)
                cxx_ec(c, cc_type_basic(AST_TYPE_UNSIGNED_INT),
                       t->kw == CXX_KW___builtin_LINE ? line : col);
            else {
                const char *s = name;
                size_t n = nl;

                if (t->kw == CXX_KW___builtin_FILE_NAME)
                    for (size_t i = 0; i < nl; i++)
                        if (name[i] == '/')
                            s = name + i + 1, n = nl - i - 1;
                if (t->kw == CXX_KW___builtin_FUNCTION)
                    s = "",
                    n = 0; /* (the enclosing function's name: not kept here) */
                cxx_et(c,
                       cc_type_pointer(
                           c->ast,
                           cc_type_qualify(c->ast, cc_type_basic(AST_TYPE_CHAR),
                                           AST_QUALIFIER_CONST)),
                       false);
                cxx_gen_string(c, s, n, t->loc);
                if (c->enode)
                    c->enode = cc_conversion_decay(c->ast, c->enode);
            }
        } else if (t->kw == CXX_KW___builtin_LINE ||
                   t->kw == CXX_KW___builtin_COLUMN)
            cxx_et(c, cc_type_basic(AST_TYPE_UNSIGNED_INT), false);
        return;
    case CXX_KW___builtin_source_location:
        cxx_advance(c);
        cxx_skip_balanced(c);
        return;
    case CXX_KW_static_cast:
    case CXX_KW_dynamic_cast:
    case CXX_KW_const_cast:
    case CXX_KW_reinterpret_cast:
        cxx_advance(c);
        cxx_expect(c, LEX_TOKEN_LESS);
        ast_qualified_type cast_to;
        {
            uint32_t targs = c->in_template_args;

            c->in_template_args = 0;
            cxx_type_id_parse(c);
            cast_to = c->type_out;
            c->in_template_args = targs;
        }
        if (!cxx_accept_greater(c))
            cxx_fail(c, "expected '>'");
        c->fn_set_seen = NULL;
        {
            uint32_t at = c->pos;

            cxx_paren_expression_parse(c);
            /* static_cast<R (*)(P...)>(&f), (f): the overload of f whose type that is */
            if (c->etype == CXX_TY_UNKNOWN && c->fn_set_seen &&
                cast_to != CXX_TY_UNKNOWN && !c->tentative &&
                !c->template_depth &&
                AST_TYPE_KIND(c->ast, cast_to) == AST_TYPE_POINTER &&
                !AST_TYPE(c->ast, cast_to)->ext &&
                AST_TYPE_KIND(c->ast, AST_TYPE(c->ast, cast_to)->base) ==
                    AST_TYPE_FUNCTION &&
                c->pos - at <= 6) {
                ast_qualified_type want = AST_QUALIFIED_TYPE_UNQUALIFIED(
                    AST_TYPE(c->ast, cast_to)->base);

                for (cxx_entity *e = c->fn_set_seen; e; e = e->next)
                    if (e->kind == CXX_ENTITY_VALUE &&
                        e->ty != CXX_TY_UNKNOWN &&
                        AST_QUALIFIED_TYPE_UNQUALIFIED(e->ty) == want &&
                        (!e->member_of || e->fn_static)) {
                        uint32_t loc = c->tok[at].loc;

                        cxx_et(c, cast_to, false);
                        cxx_gen_function_value(c, e, loc);
                        if (c->enode)
                            c->enode = cc_conversion_decay(
                                c->ast,
                                c->enode); /* (the function's address) */
                        break;
                    }
            }
            c->fn_set_seen = NULL;
        }
        {
            bool k =
                c->econst && cxx_const_widens(c, c->etype, cast_to, c->evalue);
            int64_t v = c->evalue;

            uint32_t xn = c->enode;

            if (c->etype == CXX_TY_UNKNOWN && !c->tentative)
                c->eunsure = 1;
            cxx_et(c, cast_to,
                   false); /* (a reference type: an lvalue of its referent) */
            if (k && cast_to != CXX_TY_UNKNOWN &&
                AST_TYPE_KIND(c->ast, cast_to) >= AST_TYPE_BOOL &&
                AST_TYPE_KIND(c->ast, cast_to) <= AST_TYPE_UINT128)
                cxx_ec(c, cast_to, v);
            else if (t->kw == CXX_KW_static_cast && cast_to != CXX_TY_UNKNOWN &&
                     cc_type_is_record(c->ast, cast_to) &&
                     AST_TYPE_KIND(c->ast, cast_to) != AST_TYPE_ARRAY)
                cxx_gen_static_cast_class(c, xn, cast_to, t->loc);
            else if (
                t->kw == CXX_KW_static_cast ||
                t->kw ==
                    CXX_KW_const_cast) /* (const_cast: the same object or value, cv dropped) */
                cxx_gen_cast(c, xn, cast_to, t->loc);
            else if (t->kw == CXX_KW_reinterpret_cast)
                cxx_gen_reinterpret(c, xn, cast_to, t->loc);
        }
        return;
    case CXX_KW_typeid: {
        /* typeid(T), typeid(e): a const std::type_info lvalue; its object for a type that is not a
		   polymorphic class (a polymorphic object's dynamic type: not built) */
        ast_qualified_type t = CXX_TY_UNKNOWN,
                           ti = cxx_std_class(c, "type_info");
        uint32_t tloc = cxx_peek(c, 0)->loc;

        cxx_advance(c);
        cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
        if (cxx_try_type_id(c))
            cxx_type_id_parse(c), t = c->type_out;
        else {
            c->unevaluated++;
            cxx_expression_parse(c);
            c->unevaluated--;
            t = c->etype;
            if (t != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, t) &&
                cxx_class_polymorphic(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t)))
                t = CXX_TY_UNKNOWN; /* (its dynamic type: from its vtable, not built) */
        }
        cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
        cxx_et(c,
               ti != CXX_TY_UNKNOWN
                   ? AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(ti),
                                        AST_QUALIFIER_CONST)
                   : CXX_TY_UNKNOWN,
               true);
        if (!c->tentative && !c->template_depth && ti != CXX_TY_UNKNOWN)
            cxx_gen_typeid(c, t, ti, tloc);
        return;
    }
    case CXX_KW_requires:
        c->unevaluated++;
        cxx_requires_expression_parse(c);
        c->unevaluated--;
        if (c->req_value >= 0)
            cxx_ec(c, cc_type_basic(AST_TYPE_BOOL), c->req_value);
        else
            cxx_et(c, cc_type_basic(AST_TYPE_BOOL), false);
        return;
    case CXX_KW___builtin_offsetof: { /* (type, member): a constant when the class is laid out */
        ast_qualified_type ot;
        uint32_t member = 0;
        uint64_t bits;
        bool simple;

        cxx_advance(c);
        cxx_advance(c);
        cxx_type_id_parse(c);
        ot = c->type_out;
        cxx_expect(c, LEX_TOKEN_COMMA);
        if (cxx_is(c, LEX_TOKEN_IDENTIFIER))
            member = cxx_peek(c, 0)->id;
        simple = member && cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_PAREN;
        while (
            !cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) { /* (a.b[i]: not evaluated) */
            if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE))
                cxx_skip_balanced(c);
            else
                cxx_advance(c);
        }
        cxx_advance(c);
        if (simple && !c->template_depth && !c->tentative &&
            cxx_class_field_offset(c, ot, member, &bits, 0) && !(bits & 7))
            cxx_ec(c, cc_type_basic(AST_TYPE_UNSIGNED_LONG),
                   (int64_t)(bits / 8));
        else
            cxx_et(c, cc_type_basic(AST_TYPE_UNSIGNED_LONG), false);
        return;
    }
    case CXX_KW___builtin_va_arg:
    case CXX_KW___builtin_bit_cast:
    case CXX_KW___builtin_convertvector:
    case CXX_KW___builtin_types_compatible_p:
    case CXX_KW___builtin_choose_expr:
    case CXX_KW___builtin_shufflevector:
    case CXX_KW___builtin_addressof:
    case CXX_KW___is_identifier:
        cxx_builtin_call_parse(c);
        cxx_et(c, CXX_TY_UNKNOWN, false);
        {
            ast_qualified_type rt;
            uint32_t bn[8], nb = c->nbnodes;

            memcpy(bn, c->bnodes, sizeof bn);
            if (nb != UINT32_MAX && !c->tentative &&
                cxx_builtin_function_type(c, t->s, t->len, nb, c->bfirst,
                                          &rt)) {
                cxx_et(c, rt, false);
                cxx_gen_builtin(c, t->s, t->len, bn, nb, t->loc);
            }
        }
        return;
    case CXX_KW_operator:
        cxx_id_expression_parse(c);
        return;
    case CXX_KW___real__:
    case CXX_KW___imag__:
    case CXX_KW___extension__: {
        bool ext = t->kw == CXX_KW___extension__;

        cxx_advance(c);
        cxx_cast_expression_parse(c);
        if (ext)
            return;
        {
            /* __real__ x, __imag__ x: a complex's part (an lvalue of an lvalue's), of a real number itself or 0 */
            ast_qualified_type ot = c->etype;
            bool im = t->kw == CXX_KW___imag__, lv = c->elval;
            uint32_t n = c->enode;

            if (ot == CXX_TY_UNKNOWN || (!cxx_type_is_complex(c, ot) &&
                                         !cc_type_is_arithmetic(c->ast, ot))) {
                cxx_et(c, CXX_TY_UNKNOWN, false);
                return;
            }
            ast_qualified_type pt =
                cxx_type_is_complex(c, ot)
                    ? AST_QUALIFIED_TYPE_UNQUALIFIED(
                          c->ast->tags[AST_TYPE(c->ast, ot)->aux].f[0].ty)
                    : AST_QUALIFIED_TYPE_UNQUALIFIED(ot);

            cxx_et(c, pt, cxx_type_is_complex(c, ot) && lv);
            if (!n || !cxx_gen_on(c))
                return;
            if (cxx_type_is_complex(c, ot))
                c->enode = cc_complex_part(c->ast, n, im);
            else {
                n = cc_node_rvalue(c->ast, n);
                c->enode = im ? cc_node_new_operands(
                                    c->ast, AST_NODE_COMMA, pt, t->loc, n,
                                    cc_lowering_zero(c->ast, pt, t->loc))
                              : n;
            }
        }
        return;
    }
    case CXX_KW_typename:
    case CXX_KW_decltype:
    case CXX_KW___decltype:
        if (cxx_lookahead_fn(c, cxx_type_then_init)) {
            cxx_type_specifiers_parse(c);
            ast_qualified_type cast_to = c->ds_type;

            bool gen_class = false;
            uint32_t gen_node = 0;

            {
                bool braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE);

                if (braced)
                    cxx_braced_arguments_parse(c);
                else
                    cxx_call_arguments_parse(c);
                if (!c->tentative && !c->template_depth &&
                    cast_to != CXX_TY_UNKNOWN &&
                    cc_type_is_record(c->ast, cast_to)) {
                    uint32_t an[CXX_MAX_ARGS], n = c->ncargs;

                    memcpy(an, c->carg_nodes, sizeof an);
                    uint32_t abr_[CXX_MAX_ARGS];

                    memcpy(abr_, c->carg_brace, sizeof abr_);
                    cxx_ctor_use(c, cast_to, c->cargs, c->cargs_lval, c->ncargs,
                                 braced); /* T(args): a temporary */
                    gen_class =
                        true; /* (a list for an aggregate: its members, cxx_gen_aggregate_init) */
                    memcpy(
                        c->gen_call_brace, abr_,
                        sizeof abr_); /* T({...}): the list a temporary of the parameter's class */
                    if (gen_class) {
                        c->gen_agg_brace =
                            braced && n <= CXX_MAX_ARGS
                                ? abr_
                                : NULL; /* (an aggregate's elements that are braced lists: H{{S(1), S(2)}}) */
                        cxx_gen_construct_temp(c, cast_to, an, n, t->loc);
                        c->gen_agg_brace = NULL;
                    }
                    memset(
                        c->gen_call_brace, 0,
                        sizeof c
                            ->gen_call_brace); /* (not taken: an aggregate's) */
                    gen_node = c->enode;
                }
            }
            cxx_functional_cast_type(c, cast_to);
            if (gen_class)
                c->enode = gen_node;
            return;
        }
        if (t->kw == CXX_KW_decltype ||
            t->kw == CXX_KW___decltype) { /* decltype(x)::member */
            cxx_id_expression_parse(c);
            return;
        }
        cxx_fail(c, "expected a type after 'typename'");
    case 0:
    case CXX_KW_template:
        break;
    default:
        /* a simple type keyword: int(x), unsigned{x} */
        if (cxx_starts_type(c) && cxx_lookahead_fn(c, cxx_type_then_init)) {
            cxx_type_specifiers_parse(c);
            ast_qualified_type cast_to = c->ds_type;

            bool gen_class = false;
            uint32_t gen_node = 0;

            {
                bool braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE);

                if (braced)
                    cxx_braced_arguments_parse(c);
                else
                    cxx_call_arguments_parse(c);
                if (!c->tentative && !c->template_depth &&
                    cast_to != CXX_TY_UNKNOWN &&
                    cc_type_is_record(c->ast, cast_to)) {
                    uint32_t an[CXX_MAX_ARGS], n = c->ncargs;

                    memcpy(an, c->carg_nodes, sizeof an);
                    uint32_t abr_[CXX_MAX_ARGS];

                    memcpy(abr_, c->carg_brace, sizeof abr_);
                    cxx_ctor_use(c, cast_to, c->cargs, c->cargs_lval, c->ncargs,
                                 braced); /* T(args): a temporary */
                    gen_class =
                        true; /* (a list for an aggregate: its members, cxx_gen_aggregate_init) */
                    memcpy(
                        c->gen_call_brace, abr_,
                        sizeof abr_); /* T({...}): the list a temporary of the parameter's class */
                    if (gen_class) {
                        c->gen_agg_brace =
                            braced && n <= CXX_MAX_ARGS
                                ? abr_
                                : NULL; /* (an aggregate's elements that are braced lists: H{{S(1), S(2)}}) */
                        cxx_gen_construct_temp(c, cast_to, an, n, t->loc);
                        c->gen_agg_brace = NULL;
                    }
                    memset(
                        c->gen_call_brace, 0,
                        sizeof c
                            ->gen_call_brace); /* (not taken: an aggregate's) */
                    gen_node = c->enode;
                }
            }
            cxx_functional_cast_type(c, cast_to);
            if (gen_class)
                c->enode = gen_node;
            return;
        }
        cxx_fail(c, "unexpected '%.*s' in an expression", (int)t->len, t->s);
    }
    /* identifiers: builtin traits, types used as functional casts, id-expressions */
    if (t->kind == LEX_TOKEN_IDENTIFIER && !t->kw &&
        cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN &&
        cxx_builtin_kind(t->id) == CXX_BUILTIN_TRAIT && !cxx_lookup(c, t->id)) {
        cxx_builtin_call_parse(c);
        cxx_et(
            c,
            t->len > 5 &&
                    (!memcmp(t->s, "__is_", 5) || !memcmp(t->s, "__has_", 6) ||
                     (t->len == 39 &&
                      !memcmp(t->s, "__builtin_lt_synthesizes_from_spaceship",
                              39)))
                ? cc_type_basic(AST_TYPE_BOOL)
                : CXX_TY_UNKNOWN,
            false);
        {
            /* a builtin that is a library function or a simple operation: its type, and its code */
            ast_qualified_type rt;
            uint32_t bn[8], nb = c->nbnodes;

            memcpy(bn, c->bnodes, sizeof bn);
            if (nb == 1 && c->bconst && c->bfirst != CXX_TY_UNKNOWN &&
                AST_TYPE_KIND(c->ast, c->bfirst) >= AST_TYPE_BOOL &&
                AST_TYPE_KIND(c->ast, c->bfirst) <= AST_TYPE_UINT128 &&
                ((t->len == 19 && !memcmp(t->s, "__builtin_popcountg", 19)) ||
                 (t->len == 14 && (!memcmp(t->s, "__builtin_clzg", 14) ||
                                   !memcmp(t->s, "__builtin_ctzg", 14))))) {
                /* the generic bit counts of a constant (the value is sign-extended to its type's width) */
                uint32_t w =
                    (uint32_t)AST_TYPE(
                        c->ast, AST_QUALIFIED_TYPE_UNQUALIFIED(c->bfirst))
                        ->size *
                    8;
                uint64_t lo = (uint64_t)c->bvalue,
                         hi = c->bvalue < 0 ? ~0ull : 0;
                int64_t r = -1;

                if (w < 64)
                    lo &= (1ull << w) - 1, hi = 0;
                if (w <= 64)
                    hi = 0;
                if (t->s[11] == 'o')
                    r = __builtin_popcountll(lo) +
                        (w > 64 ? __builtin_popcountll(hi) : 0);
                else if (t->s[10] == 'c' && t->s[11] == 'l' && (lo || hi))
                    r = w > 64 ? (hi ? __builtin_clzll(hi)
                                     : 64 + __builtin_clzll(lo)) -
                                     (int)(128 - w)
                               : __builtin_clzll(lo) - (int)(64 - w);
                else if (t->s[11] == 't' && (lo || hi))
                    r = lo ? __builtin_ctzll(lo) : 64 + __builtin_ctzll(hi);
                if (r >= 0) {
                    cxx_ec(c, cc_type_basic(AST_TYPE_INT), r);
                    return;
                }
            }
            if (nb != UINT32_MAX && !c->tentative &&
                cxx_builtin_function_type(c, t->s, t->len, nb, c->bfirst,
                                          &rt)) {
                cxx_et(c, rt, false);
                cxx_gen_builtin(c, t->s, t->len, bn, nb, t->loc);
                return;
            }
        }
        {
            int64_t v;

            /* a trait of known types (not a template's, not a pattern's placeholders): its value */
            if (!c->template_depth && !c->tentative && c->nbtypes &&
                c->nbtypes <= 2 && !cxx_type_has_placeholder(c, c->btypes[0]) &&
                !(c->nbtypes == 2 &&
                  cxx_type_has_placeholder(c, c->btypes[1])) &&
                cxx_trait_value(c, t->s, t->len, c->btypes, c->nbtypes, &v))
                cxx_ec(c,
                       t->len == 12 && !memcmp(t->s, "__datasizeof", 12)
                           ? cc_type_basic(AST_TYPE_UNSIGNED_LONG)
                           : cc_type_basic(AST_TYPE_BOOL),
                       v);
            else if (t->len == 12 && !memcmp(t->s, "__datasizeof", 12))
                cxx_et(c, cc_type_basic(AST_TYPE_UNSIGNED_LONG), false);
        }
        return;
    }
    {
        /* a type followed by ( or { */
        cxx_mark m;
        bool is_type;
        cxx_entity *ctad_te =
            NULL; /* a class template named without its arguments: Name{args} deduces them */

        cxx_mark_set(c, &m);
        is_type = cxx_lookahead_fn(c, cxx_type_then_init);
        if (is_type) {
            /* only if the name is a type (a value followed by ( is a call) */
            cxx_scope *q;
            bool dep;
            cxx_entity *e;

            cxx_nested_name_parse(c, &q, &dep);
            cxx_accept_kw(c, CXX_KW_template);
            e = cxx_is(c, LEX_TOKEN_IDENTIFIER)
                    ? (dep ? cxx_entity_dependent()
                       : q ? cxx_lookup_in(c, q, cxx_peek(c, 0)->id, 0)
                           : cxx_lookup(c, cxx_peek(c, 0)->id))
                    : NULL;
            if (e && e->kind != CXX_ENTITY_TYPE &&
                e->kind != CXX_ENTITY_TEMPLATE &&
                !(cxx_builtin_kind(cxx_peek(c, 0)->id) ==
                  CXX_BUILTIN_TYPE_TRANSFORM))
                is_type = cxx_entity_kind_find(e, CXX_ENTITY_TYPE) &&
                          !cxx_entity_kind_find(e, CXX_ENTITY_VALUE);
            if (e && e->kind == CXX_ENTITY_TEMPLATE && e->next)
                is_type = true;
            if (e && is_type) {
                cxx_entity *te = cxx_entity_prefer(e, CXX_ENTITY_TEMPLATE);

                if (te && te->kind == CXX_ENTITY_TEMPLATE && te->nguides &&
                    (cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_BRACE ||
                     cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_PAREN))
                    ctad_te = te;
            }
            cxx_mark_rewind(c, &m);
        }
        if (is_type) {
            cxx_type_specifiers_parse(c);
            ast_qualified_type cast_to = c->ds_type;

            bool gen_class = false;
            uint32_t gen_node = 0;

            {
                bool braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE);

                if (braced)
                    cxx_braced_arguments_parse(c);
                else
                    cxx_call_arguments_parse(c);
                if (cast_to == CXX_TY_UNKNOWN && ctad_te &&
                    c->ncargs != UINT32_MAX && c->ncargs <= 8) {
                    /* (deducing instantiates the class, whose members parse argument lists of their own: this
					   list's, kept) */
                    uint32_t sn = c->ncargs, snodes[8], sbr[CXX_MAX_ARGS],
                             szero = c->carg_zero;
                    ast_qualified_type st[8];
                    uint8_t sl[8];

                    memcpy(snodes, c->carg_nodes, sn * sizeof *snodes),
                        memcpy(st, c->cargs, sn * sizeof *st);
                    memcpy(sl, c->cargs_lval, sn),
                        memcpy(sbr, c->carg_brace, sizeof sbr);
                    cast_to = cxx_ctad(c, ctad_te, st, sl, sn);
                    if (c->ccargs < sn)
                        c->cargs = (ast_qualified_type *)(realloc(
                            c->cargs,
                            (c->ccargs = sn + 16) * sizeof *c->cargs)),
                        c->cargs_lval =
                            (uint8_t *)realloc(c->cargs_lval, c->ccargs);
                    c->ncargs = sn, c->carg_zero = szero;
                    memcpy(c->carg_nodes, snodes, sn * sizeof *snodes),
                        memcpy(c->cargs, st, sn * sizeof *st);
                    memcpy(c->cargs_lval, sl, sn),
                        memcpy(c->carg_brace, sbr, sizeof sbr);
                }
                if (!c->tentative && !c->template_depth &&
                    cast_to != CXX_TY_UNKNOWN &&
                    cc_type_is_record(c->ast, cast_to)) {
                    uint32_t an[CXX_MAX_ARGS], n = c->ncargs;

                    memcpy(an, c->carg_nodes, sizeof an);
                    uint32_t abr_[CXX_MAX_ARGS];

                    memcpy(abr_, c->carg_brace, sizeof abr_);
                    cxx_ctor_use(c, cast_to, c->cargs, c->cargs_lval, c->ncargs,
                                 braced); /* T(args): a temporary */
                    gen_class =
                        true; /* (a list for an aggregate: its members, cxx_gen_aggregate_init) */
                    memcpy(
                        c->gen_call_brace, abr_,
                        sizeof abr_); /* T({...}): the list a temporary of the parameter's class */
                    if (gen_class) {
                        c->gen_agg_brace =
                            braced && n <= CXX_MAX_ARGS
                                ? abr_
                                : NULL; /* (an aggregate's elements that are braced lists: H{{S(1), S(2)}}) */
                        cxx_gen_construct_temp(c, cast_to, an, n, t->loc);
                        c->gen_agg_brace = NULL;
                    }
                    memset(
                        c->gen_call_brace, 0,
                        sizeof c
                            ->gen_call_brace); /* (not taken: an aggregate's) */
                    gen_node = c->enode;
                }
            }
            cxx_functional_cast_type(c, cast_to);
            if (gen_class)
                c->enode = gen_node;
            return;
        }
    }
    cxx_id_expression_parse(c);
}

VPRIV void cxx_postfix_expression_parse1(cxx *c);
