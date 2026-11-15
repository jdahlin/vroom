/*
 * statement.c: C++ statements (cxx-internal.h).
 *
 * A statement that may be a declaration is tried as one first (the
 * language's rule: what can be a declaration is one), then as an
 * expression.
 */
#include "cxx-internal.h"

static bool cxx_try(cxx *c, void (*fn)(cxx *))
{
    cxx_mark m;
    jmp_buf jb;
    volatile bool ok = false;

    cxx_mark_set(c, &m);
    c->tentative++;
    c->fail = &jb;
    if (!setjmp(jb)) {
        fn(c);
        ok = true;
    }
    c->tentative--;
    cxx_mark_rewind(c, &m);
    return ok;
}

static bool cxx_kw_is_asm_qualifier(cxx *c)
{
    int kw = cxx_kw(c);

    return kw == CXX_KW_volatile || kw == CXX_KW___volatile__ ||
           kw == CXX_KW___volatile || kw == CXX_KW_inline || kw == CXX_KW_goto;
}

static void cxx_block_declaration(cxx *c)
{
    cxx_declaration_parse(c, CXX_CONTEXT_BLOCK);
}

/* a declaration of a block scope (tried before an expression) */
static void cxx_substatement_parse(cxx *c);

/* a handler's exception-declaration, up to its ): a type and perhaps a name */
static void cxx_handler_declaration(cxx *c)
{
    cxx_handler_declaration_parse(c);
}

static bool cxx_declaration_statement_try(cxx *c)
{
    cxx_token *t = cxx_peek(c, 0);

    if (cxx_starts_declaration(c) && t->kw != CXX_KW_typename)
        return true;
    if (t->kind != LEX_TOKEN_IDENTIFIER && t->kind != LEX_TOKEN_COLON_COLON)
        return false;
    if (t->kw && t->kw != CXX_KW_typename && t->kw != CXX_KW_decltype)
        return false;
    if (t->kind == LEX_TOKEN_COLON_COLON &&
        cxx_peek(c, 1)->kw == CXX_KW_operator)
        return false; /* ::operator delete(p);: a call (a block declares no operator function) */
    /* x = 1; f(a); a.b(): not declarations, and quick to see */
    if (t->kind == LEX_TOKEN_IDENTIFIER && !t->kw) {
        int n = cxx_peek(c, 1)->kind;
        cxx_entity *e = cxx_lookup(c, t->id);

        if (n == LEX_TOKEN_PERIOD || n == LEX_TOKEN_ARROW ||
            n == LEX_TOKEN_EQUAL || n == LEX_TOKEN_PLUS_PLUS ||
            n == LEX_TOKEN_MINUS_MINUS || n == LEX_TOKEN_LEFT_SQUARE ||
            n == LEX_TOKEN_PLUS_EQUAL || n == LEX_TOKEN_SEMICOLON)
            return false;
        if (e) { /* functions, variables, function templates: an expression */
            bool values = true;

            for (cxx_entity *x = e; x && values; x = x->next)
                values = x->kind == CXX_ENTITY_VALUE ||
                         x->kind == CXX_ENTITY_FUNCTION_TEMPLATE;
            if (values)
                return false;
        }
        if (!e && n != LEX_TOKEN_COLON_COLON && n != LEX_TOKEN_LESS)
            return false;
        /* A::B::f(...): a qualified name of functions or variables is an expression (Base::f(x); calls) */
        if (e && n == LEX_TOKEN_COLON_COLON) {
            cxx_entity *x = e;
            uint32_t i = 0;

            while (x && cxx_peek(c, i + 1)->kind == LEX_TOKEN_COLON_COLON &&
                   cxx_peek(c, i + 2)->kind == LEX_TOKEN_IDENTIFIER &&
                   !cxx_peek(c, i + 2)->kw && x->members &&
                   !x->members->dependent && !x->next) {
                x = cxx_lookup_in(c, x->members, cxx_peek(c, i + 2)->id, 0);
                i += 2;
            }
            if (x && i && cxx_peek(c, i + 1)->kind != LEX_TOKEN_COLON_COLON &&
                cxx_peek(c, i + 1)->kind != LEX_TOKEN_LESS) {
                bool values = true;

                for (cxx_entity *y = x; y && values; y = y->next)
                    values = y->kind == CXX_ENTITY_VALUE ||
                             y->kind == CXX_ENTITY_FUNCTION_TEMPLATE;
                if (values)
                    return false;
            }
        }
    }
    return cxx_try(c, cxx_block_declaration);
}

/*
 * for (decl : r) over a class with begin() and end(): { auto &&__r = r; auto __b = __r.begin(), __e = __r.end();
 * for (; __b != __e; ++__b) { decl = *__b; body } } -- each call resolved and built as the expression would
 * be (a pointer as the iterator: the built-in operators).  Sets gr's loop parts; false: not built.
 */
static bool cxx_range_for_class(cxx *c, cxx_gen_range *gr, uint32_t range,
                                ast_qualified_type rt, uint32_t loc)
{
    cc *a = c->ast;
    cxx_scope *cls =
        cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(rt));
    cxx_entity *bs, *es;
    uint32_t rp, rnode, bnode, enode, bsym, esym;
    ast_qualified_type it, it2;

    if (!cls || !range ||
        !(AST_NODE_AT(a, range)->flags & AST_NODE_FLAG_LVALUE) || !c->gen_block)
        return false;
    bs = cxx_lookup_in(c, cls, vintern_cstr("begin"), 0);
    es = cxx_lookup_in(c, cls, vintern_cstr("end"), 0);
    if (!bs || !es || bs->kind == CXX_ENTITY_DEPENDENT ||
        es->kind == CXX_ENTITY_DEPENDENT)
        return false;
    memset(gr, 0, sizeof *gr);
    cc_id_list_init(&gr->pre);
    gr->outer = c->gen_block;
    c->gen_block = &gr->pre;
    /* the range, once: a pointer to it */
    rp = cxx_gen_local(c, 0,
                       cc_type_pointer(a, AST_QUALIFIED_TYPE_UNQUALIFIED(
                                              AST_NODE_AT(a, range)->ty)),
                       loc);
    if (!rp)
        goto fail;
    cxx_gen_local_declare(c, rp, cc_address_of_build(a, range, loc), true, loc);
    rnode = cc_dereference_build(
        a, cc_node_rvalue(a, cxx_gen_var_node_public(c, rp, loc)), loc);
    /* begin(), end() */
    c->call_obj =
        (AST_QUALIFIED_TYPE_QUALIFIERS(rt) & AST_QUALIFIER_CONST) ? 2 : 1;
    cxx_call_type(c, bs, CXX_TY_UNKNOWN, NULL, NULL, 0);
    if (!c->call_resolved || !c->call_picked ||
        (it = c->etype) == CXX_TY_UNKNOWN)
        goto fail;
    c->enode = 0;
    cxx_gen_call(c, c->call_picked, rnode, false, NULL, 0, loc);
    bnode = c->enode;
    c->call_obj =
        (AST_QUALIFIED_TYPE_QUALIFIERS(rt) & AST_QUALIFIER_CONST) ? 2 : 1;
    cxx_call_type(c, es, CXX_TY_UNKNOWN, NULL, NULL, 0);
    if (!c->call_resolved || !c->call_picked ||
        (it2 = c->etype) == CXX_TY_UNKNOWN ||
        AST_QUALIFIED_TYPE_UNQUALIFIED(it2) !=
            AST_QUALIFIED_TYPE_UNQUALIFIED(it) ||
        !bnode)
        goto fail;
    c->enode = 0;
    cxx_gen_call(
        c, c->call_picked,
        cc_dereference_build(
            a, cc_node_rvalue(a, cxx_gen_var_node_public(c, rp, loc)), loc),
        false, NULL, 0, loc);
    enode = c->enode;
    if (!enode)
        goto fail;
    it = AST_QUALIFIED_TYPE_UNQUALIFIED(it);
    bsym = cxx_gen_local(c, 0, it, loc), esym = cxx_gen_local(c, 0, it, loc);
    if (!bsym || !esym)
        goto fail;
    if (cc_type_is_record(a, it)) {
        uint8_t lv = 0;

        cxx_ctor_use(c, it, &it, &lv, 1, false);
        cxx_gen_construct_local(c, bsym, it, &bnode, 1, loc);
        cxx_ctor_use(c, it, &it, &lv, 1, false);
        cxx_gen_construct_local(c, esym, it, &enode, 1, loc);
    } else {
        cxx_gen_local_declare(c, bsym, bnode, true, loc);
        cxx_gen_local_declare(c, esym, enode, true, loc);
    }
    if (!cxx_gen_on(c))
        goto fail;
    {
        uint32_t bv = cxx_gen_var_node_public(c, bsym, loc),
                 ev = cxx_gen_var_node_public(c, esym, loc);

        if (!cc_type_is_record(a, it)) {
            if (AST_TYPE_KIND(a, it) != AST_TYPE_POINTER)
                goto fail;
            gr->cond = cc_binary_build(a, LEX_TOKEN_EXCLAMATION_EQUAL,
                                       cc_node_rvalue(a, bv),
                                       cc_node_rvalue(a, ev), loc);
            gr->step = cc_assignment_build(
                a, LEX_TOKEN_PLUS, cxx_gen_var_node_public(c, bsym, loc),
                cc_node_integer_new(a, 1, cc_type_basic(AST_TYPE_LONG), loc),
                loc);
            gr->elem = cc_dereference_build(
                a, cc_node_rvalue(a, cxx_gen_var_node_public(c, bsym, loc)),
                loc);
        } else {
            ast_qualified_type arg = it;

            /* __b != __e (or !(__b == __e)) */
            c->op_lv_set = 1, c->op_lv[0] = 1;
            if (cxx_operator_call_type(c, vintern_cstr("operator!="), it, &arg,
                                       1) &&
                c->call_resolved && c->call_picked) {
                c->enode = 0;
                cxx_gen_operator_call(c, bv, &ev, 1, loc);
                gr->cond = c->enode;
            } else if ((arg = it, c->op_lv_set = 1, c->op_lv[0] = 1,
                        cxx_operator_call_type(c, vintern_cstr("operator=="),
                                               it, &arg, 1)) &&
                       c->call_resolved && c->call_picked) {
                c->enode = 0;
                cxx_gen_operator_call(c, bv, &ev, 1, loc);
                gr->cond = c->enode
                               ? cc_unary_build_public(c, LEX_TOKEN_EXCLAMATION,
                                                       c->enode, loc)
                               : 0;
            }
            c->op_lv_set = 0;
            /* ++__b */
            c->op_lv_set = 1, c->op_lv[0] = 1;
            if (cxx_operator_call_type(c, vintern_cstr("operator++"), it, NULL,
                                       0) &&
                c->call_resolved && c->call_picked) {
                c->enode = 0;
                cxx_gen_operator_call(c, cxx_gen_var_node_public(c, bsym, loc),
                                      NULL, 0, loc);
                gr->step = c->enode;
            }
            c->op_lv_set = 0;
            /* *__b */
            c->op_lv_set = 1, c->op_lv[0] = 1;
            if (cxx_operator_call_type(c, vintern_cstr("operator*"), it, NULL,
                                       0) &&
                c->call_resolved && c->call_picked) {
                c->enode = 0;
                cxx_gen_operator_call(c, cxx_gen_var_node_public(c, bsym, loc),
                                      NULL, 0, loc);
                gr->elem = c->enode;
            }
            c->op_lv_set = 0;
        }
    }
    if (!gr->cond || !gr->step || !gr->elem)
        goto fail;
    gr->cond = cxx_gen_condition(c, gr->cond);
    if (!gr->cond)
        goto fail;
    gr->loc = loc, gr->ok = true, gr->cls = true;
    cc_id_list_init(&gr->body);
    c->gen_block = &gr->body;
    return true;
fail:
    c->gen_block = gr->outer;
    cxx_gen_fail(c);
    return false;
}

/* for (init; cond; inc) or for (decl : range); after the ( */
static void cxx_for_parse(cxx *c)
{
    cxx_scope *fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
    cxx_scope *outer = c->scope;

    cxx_scope_push(c, fs);
    cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
    /* range-for: a declaration followed by : (find it at depth 0 before the first ;) */
    {
        int depth = 0;
        uint32_t i = c->pos;
        bool range = false;

        for (; i < c->ntok; i++) {
            int k = c->tok[i].kind;

            if (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_LEFT_SQUARE ||
                k == LEX_TOKEN_LEFT_BRACE)
                depth++;
            else if (k == LEX_TOKEN_RIGHT_PAREN ||
                     k == LEX_TOKEN_RIGHT_SQUARE ||
                     k == LEX_TOKEN_RIGHT_BRACE) {
                if (!depth--)
                    break;
            } else if (depth == 0 && k == LEX_TOKEN_SEMICOLON)
                break;
            else if (depth == 0 && k == LEX_TOKEN_COLON &&
                     c->tok[i + 1].kind != LEX_TOKEN_COLON) {
                range = true;
                break;
            }
        }
        if (range) {
            /* decl-specifiers declarator (or a structured binding [a, b]) : range */
            uint32_t colon = i, last_id = 0;
            cxx_entity *var = NULL;
            ast_qualified_type var_type = CXX_TY_UNKNOWN;
            bool var_auto = false;

            /* the declaration, when it is a plain one outside templates: its name and type */
            if (!c->template_depth && !c->tentative &&
                !cxx_is(c, LEX_TOKEN_SEMICOLON) &&
                cxx_lookahead_fn(c, cxx_for_range_declaration_parse)) {
                cxx_for_range_declaration_parse(c);
                if (c->range_name > 1 && c->pos == colon) {
                    var = cxx_declare(c, fs, c->range_name, CXX_ENTITY_VALUE);
                    var_type = c->range_type, var_auto = c->range_auto;
                }
                c->pos = colon;
            }
            for (uint32_t k = c->pos; k < colon; k++)
                if (c->tok[k].kind == LEX_TOKEN_IDENTIFIER && !c->tok[k].kw)
                    last_id = c->tok[k].id;
            if (cxx_is(c, LEX_TOKEN_SEMICOLON) == false && c->pos < colon) {
                /* (a C++20 init-statement before the declaration is not handled) */
                c->pos = colon;
            }
            if (last_id && !var)
                cxx_declare(c, fs, last_id, CXX_ENTITY_VALUE);
            for (uint32_t k = colon - 1;
                 k > 0 && c->tok[k].kind != LEX_TOKEN_LEFT_PAREN; k--)
                if (c->tok[k].kind == LEX_TOKEN_IDENTIFIER && !c->tok[k].kw &&
                    c->tok[k - 1].kind == LEX_TOKEN_COMMA)
                    cxx_declare(c, fs, c->tok[k].id,
                                CXX_ENTITY_VALUE); /* structured bindings */
            cxx_expect(c, LEX_TOKEN_COLON);
            uint32_t rloc = cxx_peek(c, 0)->loc, rnode = 0, elems[64],
                     nelems = 0;
            ast_qualified_type brace_elem = CXX_TY_UNKNOWN,
                               rrt = CXX_TY_UNKNOWN;
            bool braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE), list_ok = braced;

            if (braced) {
                /* { e, ... }: a std::initializer_list of the elements' one type (decayed), here its elements */
                cxx_advance(c);
                while (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE)) {
                    ast_qualified_type et;

                    cxx_assignment_expression_parse(c);
                    et = c->etype;
                    if (et != CXX_TY_UNKNOWN &&
                        AST_TYPE_KIND(c->ast, et) == AST_TYPE_ARRAY)
                        et =
                            cc_type_pointer(c->ast, AST_TYPE(c->ast, et)->base);
                    else if (et != CXX_TY_UNKNOWN)
                        et = AST_QUALIFIED_TYPE_UNQUALIFIED(et);
                    if (et == CXX_TY_UNKNOWN ||
                        (brace_elem != CXX_TY_UNKNOWN && brace_elem != et))
                        list_ok = false;
                    brace_elem = et;
                    if (nelems < 64)
                        elems[nelems] = c->enode;
                    nelems++;
                    if (!cxx_accept(c, LEX_TOKEN_COMMA))
                        break;
                }
                cxx_expect(c, LEX_TOKEN_RIGHT_BRACE);
                if (nelems > 64)
                    list_ok = false;
            } else {
                cxx_initializer_parse(c);
                rnode = c->enode, rrt = c->etype;
            }
            if (var) {
                /* auto: deduced from *begin(range), an lvalue; else as declared */
                ast_qualified_type elem;

                if (!var_auto)
                    var->ty = var_type;
                else if (braced ? list_ok && (elem = brace_elem, true)
                                : cxx_range_element_type(c, c->etype, &elem))
                    var->ty = cxx_auto_deduce(
                        c, var_type,
                        braced
                            ? AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(elem),
                                                 AST_QUALIFIER_CONST)
                            : elem,
                        true);
            }
            cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
            {
                /* code generation: arrays and braced lists (cxx_gen_range_for_begin) */
                cxx_gen_range gr;
                bool gen = cxx_gen_on(c) && var && var->ty != CXX_TY_UNKNOWN &&
                           !c->tentative && !c->template_depth &&
                           (!braced || list_ok);

                bool cls_range = gen && !braced && rnode &&
                                 c->etype != CXX_TY_UNKNOWN &&
                                 cc_type_is_record(c->ast, rrt) &&
                                 AST_TYPE_KIND(c->ast, rrt) != AST_TYPE_ARRAY;

                if (gen &&
                    (cls_range ? cxx_range_for_class(c, &gr, rnode, rrt, rloc)
                               : cxx_gen_range_for_begin(
                                     c, &gr, rnode, braced ? elems : NULL,
                                     nelems, brace_elem, rloc))) {
                    uint32_t vs = cxx_gen_local(c, var->name, var->ty, rloc);

                    if (vs) {
                        var->sym = vs;
                        cxx_gen_local_declare(c, vs,
                                              cxx_gen_range_for_element(c, &gr),
                                              true, rloc);
                    }
                    c->snode = 0;
                    cxx_statement_parse(c);
                    if (!vs)
                        cxx_gen_fail(c);
                    cxx_gen_range_for_end(c, &gr);
                } else {
                    if (cxx_gen_on(c) && !c->tentative && !c->template_depth)
                        cxx_gen_fail(
                            c); /* (begin/end of a class: not built yet) */
                    cxx_statement_parse(c);
                }
            }
            c->scope = outer;
            return;
        }
    }
    /* code generation: { init; for (; cond; step) body } with the init's declarations in the block */
    bool gen = cxx_gen_on(c);
    cc_id_list pre;
    void *outer_block = c->gen_block;
    uint32_t floc = cxx_peek(c, 0)->loc, gcond = 0, gstep = 0, gbody;
    bool has_step = false;

    if (gen) {
        cc_id_list_init(&pre);
        c->gen_block = &pre;
    }
    if (!cxx_accept(c, LEX_TOKEN_SEMICOLON)) {
        if (cxx_declaration_statement_try(c))
            cxx_declaration_parse(c, CXX_CONTEXT_BLOCK);
        else {
            cxx_expression_parse(c);
            if (gen && cxx_gen_on(c)) {
                if (c->enode)
                    cc_id_list_append(
                        c->ast, &pre,
                        cxx_gen_new_statement(c, AST_NODE_EXPRESSION, floc,
                                              c->enode, 0, 0));
                else
                    cxx_gen_fail(c);
            }
            cxx_expect(c, LEX_TOKEN_SEMICOLON);
        }
    }
    if (gen)
        c->gen_block = outer_block;
    c->gen_notemp++;
    if (!cxx_is(c, LEX_TOKEN_SEMICOLON)) {
        cxx_expression_parse(c);
        if (gen)
            gcond = cxx_gen_condition(c, c->enode);
    }
    cxx_expect(c, LEX_TOKEN_SEMICOLON);
    if (!cxx_is(c, LEX_TOKEN_RIGHT_PAREN)) {
        cxx_expression_parse(c);
        has_step = true, gstep = c->enode;
    }
    c->gen_notemp--;
    cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
    cxx_substatement_parse(c);
    c->scope = outer;
    if (gen && cxx_gen_on(c)) {
        cc_id_list sb;

        gbody = c->snode
                    ? c->snode
                    : cxx_gen_new_statement(c, AST_NODE_BLOCK, floc, 0, 0, 0);
        if (has_step && !gstep) {
            cxx_gen_fail(c);
            return;
        }
        cc_id_list_init(&sb);
        cc_id_list_append(c->ast, &sb, gstep);
        cc_id_list_append(c->ast, &sb, gbody);
        cc_id_list_append(
            c->ast, &pre,
            cxx_gen_new_statement(c, AST_NODE_FOR, floc, 0, gcond,
                                  cc_id_list_freeze(c->ast, &sb)));
        cxx_gen_statement(
            c, cxx_gen_new_statement(c, AST_NODE_BLOCK, floc, 0,
                                     cc_id_list_freeze(c->ast, &pre), pre.n));
    }
}

/* if/switch/while ( [init;] condition ) */
/*
 * An if's or a switch's condition that is a declaration (T x = e, T x{e}): parsed as a declaration like any
 * other, up to the ) that ends the condition; its value is the variable's.  Code generation declares the
 * variable in the block around the statement.  False: not a declaration.
 */
static bool cxx_condition_declaration_parse(cxx *c)
{
    uint32_t start = c->pos, close = c->pos, name = 0, at = 0;
    int d = 0;

    if (!c->cond_decl || c->tentative || c->template_depth)
        return false;
    for (; close < c->ntok; close++) {
        int k = c->tok[close].kind;

        if (k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_LEFT_SQUARE ||
            k == LEX_TOKEN_LEFT_BRACE)
            d++;
        else if (k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_RIGHT_SQUARE ||
                 k == LEX_TOKEN_RIGHT_BRACE) {
            if (!d--)
                break;
        } else if (!d && k == LEX_TOKEN_SEMICOLON)
            return false; /* (an init-statement first) */
        else if (!d && !name && k == LEX_TOKEN_IDENTIFIER &&
                 !c->tok[close].kw) {
            /* the name: before = or {, perhaps with __attribute__((...)) in between */
            uint32_t after = close + 1;

            while (after < c->ntok &&
                   c->tok[after].kw == CXX_KW___attribute__ &&
                   c->tok[after + 1].kind == LEX_TOKEN_LEFT_PAREN) {
                int ad = 0;

                for (after++; after < c->ntok; after++) {
                    if (c->tok[after].kind == LEX_TOKEN_LEFT_PAREN)
                        ad++;
                    else if (c->tok[after].kind == LEX_TOKEN_RIGHT_PAREN &&
                             !--ad) {
                        after++;
                        break;
                    }
                }
            }
            if (after < c->ntok && (c->tok[after].kind == LEX_TOKEN_EQUAL ||
                                    c->tok[after].kind == LEX_TOKEN_LEFT_BRACE))
                name = c->tok[close].id, at = close;
        }
    }
    if (!name || at == start || close >= c->ntok ||
        c->tok[close].kind != LEX_TOKEN_RIGHT_PAREN)
        return false;
    {
        /* (what comes before the name ends a type: a name, >, *, &) */
        int pk = c->tok[at - 1].kind;

        if (pk != LEX_TOKEN_IDENTIFIER && pk != LEX_TOKEN_GREATER &&
            pk != LEX_TOKEN_STAR && pk != LEX_TOKEN_AMPERSAND &&
            pk != LEX_TOKEN_AMPERSAND_AMPERSAND)
            return false;
    }
    {
        jmp_buf djb, *outer_fail = c->fail;
        volatile bool parsed = false;
        cxx_entity *e;

        c->tok[close].kind = LEX_TOKEN_SEMICOLON;
        c->fail = &djb;
        if (!setjmp(djb)) {
            cxx_declaration_parse(c, CXX_CONTEXT_BLOCK);
            parsed = true;
        }
        c->fail = outer_fail;
        c->tok[close].kind = LEX_TOKEN_RIGHT_PAREN;
        if (!parsed)
            longjmp(*outer_fail, 1);
        c->pos = close;
        e = cxx_lookup(c, name);
        if (e && e->kind == CXX_ENTITY_VALUE)
            cxx_name_type(c, e);
        else
            cxx_et(c, CXX_TY_UNKNOWN, false);
    }
    return true;
}

static void cxx_condition_parse(cxx *c)
{
    cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
    for (;;) {
        if (cxx_condition_declaration_parse(c))
            break;
        if (cxx_declaration_statement_try(c)) {
            /* a declaration: init-statement ending in ; or a condition declaration */
            uint32_t save = c->pos;
            cxx_mark m;
            jmp_buf jb;
            volatile bool ok = false;

            cxx_mark_set(c, &m);
            c->tentative++;
            c->fail = &jb;
            if (!setjmp(jb)) {
                cxx_declaration_parse(c, CXX_CONTEXT_BLOCK);
                ok = true;
            }
            c->tentative--;
            cxx_mark_rewind(c, &m);
            (void)save;
            if (ok) {
                cxx_declaration_parse(c, CXX_CONTEXT_BLOCK); /* (ends with ;) */
                continue;
            }
            /* a condition declaration: T x = e  or  T x{e} */
            {
                int depth = 0;

                while (!(depth == 0 && cxx_is(c, LEX_TOKEN_RIGHT_PAREN))) {
                    if (cxx_is(c, LEX_TOKEN_EOF))
                        cxx_fail(c, "unterminated condition");
                    if (cxx_is(c, LEX_TOKEN_LEFT_PAREN))
                        depth++;
                    else if (cxx_is(c, LEX_TOKEN_RIGHT_PAREN))
                        depth--;
                    if (depth == 0 && cxx_is(c, LEX_TOKEN_IDENTIFIER) &&
                        !cxx_kw(c) &&
                        (cxx_peek(c, 1)->kind == LEX_TOKEN_EQUAL ||
                         cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_BRACE))
                        cxx_declare(c, c->scope, cxx_peek(c, 0)->id,
                                    CXX_ENTITY_VALUE);
                    if (depth == 0 && cxx_is(c, LEX_TOKEN_EQUAL)) {
                        cxx_advance(c);
                        cxx_initializer_parse(c);
                        continue;
                    }
                    if (depth == 0 && cxx_is(c, LEX_TOKEN_LEFT_BRACE)) {
                        cxx_braced_init_list_parse(c);
                        continue;
                    }
                    cxx_advance(c);
                }
                cxx_et(c, CXX_TY_UNKNOWN,
                       false); /* (a condition declaration: no node yet) */
                break;
            }
        }
        cxx_expression_parse(c);
        if (cxx_accept(c, LEX_TOKEN_SEMICOLON))
            continue; /* an expression init-statement */
        break;
    }
    cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
}

VPRIV void cxx_compound_statement_parse(cxx *c)
{
    cxx_scope *bs = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);
    cxx_scope *outer = c->scope;
    /* code generation: the block's statements, in a list of its own */
    cc_id_list list;
    void *outer_block = c->gen_block;
    bool gen = cxx_gen_on(c);
    uint32_t loc = cxx_peek(c, 0)->loc, outer_temps = c->gen_block_temps;

    if (gen) {
        cc_id_list_init(&list);
        c->gen_block = &list;
        c->gen_block_temps = 0;
    }
    cxx_expect(c, LEX_TOKEN_LEFT_BRACE);
    cxx_scope_push(c, bs);
    while (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE)) {
        if (cxx_is(c, LEX_TOKEN_EOF))
            cxx_fail(c, "expected '}'");
        bool noted = cxx_gen_noted();

        cxx_statement_parse(c);
        if (gen && c->snode && cxx_gen_on(c))
            cc_id_list_append(c->ast, &list, c->snode);
        if (gen && cxx_gen_on(c) && !noted)
            cxx_gen_note_clear(); /* (a statement built after all: its expressions' notes are no reason) */
    }
    cxx_advance(c);
    c->scope = outer;
    if (gen) {
        c->gen_block = outer_block;
        c->gen_block_temps = outer_temps;
        cxx_gen_statement(c, cxx_gen_on(c)
                                 ? cxx_gen_new_statement(
                                       c, AST_NODE_BLOCK, loc, 0,
                                       cc_id_list_freeze(c->ast, &list), list.n)
                                 : 0);
    }
}

static void cxx_statement_parse1(cxx *c);

/* (code generation: a statement whose own case did not give its node is one not built yet) */
VPRIV void cxx_statement_parse(cxx *c)
{
    uint32_t depth = ++c->sdepth;

    c->snode = 0;
    c->sdone =
        0; /* (the statement before this one, done at the same depth, does not vouch for this one) */
    cxx_statement_parse1(c);
    if (c->gen && c->sdone != depth) {
        cxx_gen_fail(c);
        c->snode = 0;
    }
    c->sdepth = depth - 1;
}

/*
 * A statement that is a part of another (an if's branches, a loop's body, a label's statement).  Code
 * generation: the temporaries it declares are its own, in a block around it (it may run many times or not
 * at all for one entry of the enclosing block).
 */
static void cxx_substatement_parse(cxx *c)
{
    cc_id_list list;
    void *outer_block = c->gen_block;
    uint32_t outer_temps = c->gen_block_temps, loc = cxx_peek(c, 0)->loc;
    bool gen =
        cxx_gen_on(c) && c->gen_block && !cxx_is(c, LEX_TOKEN_LEFT_BRACE);

    if (gen) {
        cc_id_list_init(&list);
        c->gen_block = &list;
        c->gen_block_temps = 0;
    }
    cxx_statement_parse(c);
    if (gen) {
        c->gen_block = outer_block;
        c->gen_block_temps = outer_temps;
        if (list.n && c->snode && cxx_gen_on(c)) {
            cc_id_list_append(c->ast, &list, c->snode);
            c->snode =
                cxx_gen_new_statement(c, AST_NODE_BLOCK, loc, 0,
                                      cc_id_list_freeze(c->ast, &list), list.n);
        } else if (list.n)
            cxx_gen_fail(c); /* (declarations with no statement to go with) */
    }
}

/* a nested statement's node, or an empty block for an empty statement */
static uint32_t cxx_gen_nested(cxx *c, uint32_t loc)
{
    if (!cxx_gen_on(c))
        return 0;
    return c->snode ? c->snode
                    : cxx_gen_new_statement(c, AST_NODE_BLOCK, loc, 0, 0, 0);
}

static void cxx_statement_parse1(cxx *c)
{
    uint32_t sloc = cxx_peek(c, 0)->loc;

    cxx_attributes_skip(c);
    switch (cxx_kind(c)) {
    case LEX_TOKEN_LEFT_BRACE:
        cxx_compound_statement_parse(c);
        return;
    case LEX_TOKEN_SEMICOLON:
        cxx_advance(c);
        cxx_gen_statement(c, 0);
        return;
    case LEX_TOKEN_IDENTIFIER:
        if (!cxx_kw(c) && cxx_peek(c, 1)->kind == LEX_TOKEN_COLON) { /* label */
            uint32_t name = cxx_peek(c, 0)->id, loc = cxx_peek(c, 0)->loc,
                     ln = 0, st = 0;
            bool gen = cxx_gen_on(c) && !c->tentative && !c->template_depth;

            cxx_advance(c);
            cxx_advance(c);
            if (gen) {
                /* name: statement, as a block of the label and the statement (C's AST_NODE_LABEL) */
                uint32_t l = cxx_gen_label(c, name, loc);

                if (c->ast->labels[l].defined)
                    cxx_gen_fail(c);
                ln = cxx_gen_new_statement(c, AST_NODE_LABEL, loc, l, 0, 0);
                c->ast->labels[l].defined = ln;
            }
            if (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE)) {
                cxx_statement_parse(c);
                st = c->snode;
            }
            if (gen && cxx_gen_on(c)) {
                cc_id_list l2;

                cc_id_list_init(&l2);
                cc_id_list_append(c->ast, &l2, ln);
                if (st)
                    cc_id_list_append(c->ast, &l2, st);
                cxx_gen_statement(c, cxx_gen_new_statement(
                                         c, AST_NODE_BLOCK, loc, 0,
                                         cc_id_list_freeze(c->ast, &l2), l2.n));
            }
            return;
        }
        break;
    }
    switch (cxx_kw(c)) {
    case CXX_KW_if: {
        cxx_advance(c);
        uint8_t is_constexpr = cxx_accept_kw(c, CXX_KW_constexpr);
        int taken =
            -1; /* if constexpr with a known condition: 1 the first statement, 0 the else */
        uint32_t gen_cond = 0, gen_then = 0, gen_else = 0;
        /* code generation: what the condition declares (a variable, temporaries) lives in a block around the if */
        cc_id_list cpre;
        void *cond_outer = c->gen_block;
        uint32_t cond_temps = c->gen_block_temps;
        bool cond_gen = cxx_gen_on(c) && c->gen_block;

        cpre.n = 0;
        cxx_scope *if_outer = NULL;

        if (cxx_is(c, LEX_TOKEN_EXCLAMATION) ||
            cxx_is_kw(c, CXX_KW_consteval)) { /* if !consteval / if consteval */
            cxx_accept(c, LEX_TOKEN_EXCLAMATION);
            cxx_expect_kw(c, CXX_KW_consteval);
            cxx_compound_statement_parse(c);
        } else {
            cxx_scope *s = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL);

            if_outer = c->scope;
            cxx_scope_push(c, s);
            if (cond_gen) {
                cc_id_list_init(&cpre);
                c->gen_block = &cpre;
                c->gen_block_temps = 0;
            }
            c->cond_decl = 1;
            cxx_condition_parse(c);
            c->cond_decl = 0;
            gen_cond = is_constexpr ? 0 : cxx_gen_condition(c, c->enode);
            if (cond_gen) {
                c->gen_block = cond_outer;
                c->gen_block_temps = cond_temps;
            }
            if (is_constexpr && c->econst && !c->tentative &&
                !c->template_depth)
                taken = c->evalue != 0;
            /* the discarded statement is parsed, not instantiated: what it calls is not used */
            c->unevaluated += taken == 0;
            cxx_substatement_parse(c);
            c->unevaluated -= taken == 0;
            gen_then = cxx_gen_nested(c, sloc);
        }
        if (cxx_accept_kw(c, CXX_KW_else)) {
            c->unevaluated += taken == 1;
            cxx_substatement_parse(c);
            c->unevaluated -= taken == 1;
            gen_else = cxx_gen_nested(c, sloc);
        }
        if (if_outer)
            c->scope =
                if_outer; /* (the condition's names are in scope in the else too, [stmt.if]/3) */
        if (is_constexpr && cxx_gen_on(c)) {
            /* if constexpr: the branch taken is the statement (the other one was not built); a condition
			   not known here: not built */
            uint32_t st =
                taken == 1 ? gen_then
                : taken == 0
                    ? (gen_else ? gen_else
                                : cxx_gen_new_statement(c, AST_NODE_BLOCK, sloc,
                                                        0, 0, 0))
                    : 0;

            if (st && !(cond_gen && cpre.n))
                cxx_gen_statement(c, st);
            else
                cxx_gen_fail(c);
        } else if (gen_cond && gen_then && cxx_gen_on(c)) {
            uint32_t st = cxx_gen_new_statement(c, AST_NODE_IF, sloc, gen_cond,
                                                gen_then, gen_else);

            if (cond_gen && cpre.n) {
                cc_id_list_append(c->ast, &cpre, st);
                st = cxx_gen_new_statement(c, AST_NODE_BLOCK, sloc, 0,
                                           cc_id_list_freeze(c->ast, &cpre),
                                           cpre.n);
            }
            cxx_gen_statement(c, st);
        } else if (cond_gen && cpre.n)
            cxx_gen_fail(c);
        return;
    }
    case CXX_KW_switch:
    case CXX_KW_while: {
        cxx_scope *s = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL),
                  *outer = c->scope;
        bool is_while = cxx_is_kw(c, CXX_KW_while);
        uint32_t gen_cond, gen_body;

        /* while (T x = e): the declaration runs each iteration, for (;;) { T x = e; if (!x) break; body };
		   switch (T x = e): { T x = e; switch (x) } */
        cc_id_list wpre;
        void *w_outer = c->gen_block;
        uint32_t w_temps = c->gen_block_temps;
        bool w_gen = cxx_gen_on(c) && c->gen_block;

        wpre.n = 0;
        cxx_advance(c);
        cxx_scope_push(c, s);
        if (w_gen) {
            cc_id_list_init(&wpre);
            c->gen_block = &wpre;
            c->gen_block_temps = 0;
        }
        c->gen_notemp +=
            is_while; /* (evaluated once per iteration: no temporaries with destructors) */
        c->cond_decl = 1;
        cxx_condition_parse(c);
        c->cond_decl = 0;
        c->gen_notemp -= is_while;
        gen_cond = is_while ? cxx_gen_condition(c, c->enode)
                            : cxx_gen_switch_value(c, c->enode);
        if (w_gen)
            c->gen_block = w_outer, c->gen_block_temps = w_temps;
        cxx_substatement_parse(c);
        gen_body = cxx_gen_nested(c, sloc);
        c->scope = outer;
        if (gen_cond && gen_body && cxx_gen_on(c) && wpre.n && is_while) {
            cc_id_list sb;
            uint32_t brk =
                cxx_gen_new_statement(c, AST_NODE_BREAK, sloc, 0, 0, 0);

            cc_id_list_append(
                c->ast, &wpre,
                cxx_gen_new_statement(
                    c, AST_NODE_IF, sloc, gen_cond,
                    cxx_gen_new_statement(c, AST_NODE_BLOCK, sloc, 0, 0, 0),
                    brk));
            cc_id_list_append(c->ast, &wpre, gen_body);
            cc_id_list_init(&sb);
            cc_id_list_append(c->ast, &sb, 0);
            cc_id_list_append(c->ast, &sb,
                              cxx_gen_new_statement(
                                  c, AST_NODE_BLOCK, sloc, 0,
                                  cc_id_list_freeze(c->ast, &wpre), wpre.n));
            cxx_gen_statement(
                c, cxx_gen_new_statement(c, AST_NODE_FOR, sloc, 0, 0,
                                         cc_id_list_freeze(c->ast, &sb)));
        } else if (gen_cond && gen_body && cxx_gen_on(c)) {
            uint32_t st = cxx_gen_new_statement(
                c, is_while ? AST_NODE_WHILE : AST_NODE_SWITCH, sloc, gen_cond,
                gen_body, 0);

            if (wpre.n) {
                cc_id_list_append(c->ast, &wpre, st);
                st = cxx_gen_new_statement(c, AST_NODE_BLOCK, sloc, 0,
                                           cc_id_list_freeze(c->ast, &wpre),
                                           wpre.n);
            }
            cxx_gen_statement(c, st);
        } else if (wpre.n)
            cxx_gen_fail(c);
        return;
    }
    case CXX_KW_do: {
        uint32_t gen_body, gen_cond;

        cxx_advance(c);
        cxx_substatement_parse(c);
        gen_body = cxx_gen_nested(c, sloc);
        cxx_expect_kw(c, CXX_KW_while);
        cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
        c->gen_notemp++;
        cxx_expression_parse(c);
        c->gen_notemp--;
        gen_cond = cxx_gen_condition(c, c->enode);
        cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
        cxx_expect(c, LEX_TOKEN_SEMICOLON);
        if (gen_body && gen_cond && cxx_gen_on(c))
            cxx_gen_statement(c, cxx_gen_new_statement(c, AST_NODE_DO, sloc,
                                                       gen_cond, gen_body, 0));
        return;
    }
    case CXX_KW_for:
        cxx_advance(c);
        cxx_for_parse(c);
        return;
    case CXX_KW_return:
    case CXX_KW_co_return: {
        bool is_return = cxx_is_kw(c, CXX_KW_return), has_value = false;
        uint32_t value = 0;

        cxx_advance(c);
        if (cxx_is(c, LEX_TOKEN_LEFT_BRACE) && is_return && cxx_gen_on(c) &&
            !c->tentative && !c->template_depth &&
            c->gen_ret != CXX_TY_UNKNOWN &&
            cc_type_is_record(c->ast, c->gen_ret) &&
            AST_TYPE_KIND(c->ast, c->gen_ret) != AST_TYPE_ARRAY) {
            /* return {args}: the result constructed from them (a temporary the return elides) */
            uint32_t an[CXX_MAX_ARGS], n;
            ast_qualified_type rt = AST_QUALIFIED_TYPE_UNQUALIFIED(c->gen_ret);

            cxx_braced_arguments_parse(c);
            n = c->ncargs;
            has_value = true;
            if (n != UINT32_MAX && n <= CXX_MAX_ARGS) {
                memcpy(an, c->carg_nodes, n * sizeof *an);
                cxx_ctor_use(c, rt, c->cargs, c->cargs_lval, n, true);
                c->gen_ret_into = c->gen_sret ? rt : CXX_TY_UNKNOWN,
                c->gen_ret_done = 0; /* (built in the result object) */
                cxx_gen_construct_temp(c, rt, an, n, sloc);
                c->gen_ret_into = CXX_TY_UNKNOWN;
                value = c->enode;
            }
            if (!value)
                cxx_gen_fail(c);
        } else if (cxx_is(c, LEX_TOKEN_LEFT_BRACE))
            cxx_braced_init_list_parse(c), has_value = true;
        else if (!cxx_is(c, LEX_TOKEN_SEMICOLON)) {
            /* return T{...}; or return T(...);, the whole expression one class prvalue of the result's class:
			   built in the result object ([stmt.return]: no temporary to copy) */
            bool into = false;

            if (is_return && cxx_gen_on(c) && c->gen_sret && !c->tentative &&
                !c->template_depth && c->gen_ret != CXX_TY_UNKNOWN) {
                uint32_t i = c->pos, d = 0;

                if (c->tok[i].kind == LEX_TOKEN_COLON_COLON)
                    i++;
                while (i < c->ntok && c->tok[i].kind == LEX_TOKEN_IDENTIFIER) {
                    i++;
                    if (c->tok[i].kind ==
                        LEX_TOKEN_LESS) { /* (template arguments: to the matching >) */
                        for (d = 0; i < c->ntok; i++) {
                            int k = c->tok[i].kind;

                            if (k == LEX_TOKEN_LESS ||
                                k == LEX_TOKEN_LEFT_PAREN)
                                d++;
                            else if (k == LEX_TOKEN_GREATER ||
                                     k == LEX_TOKEN_RIGHT_PAREN)
                                d--;
                            else if (k == LEX_TOKEN_GREATER_GREATER)
                                d -= 2;
                            else if (k == LEX_TOKEN_SEMICOLON ||
                                     k == LEX_TOKEN_LEFT_BRACE)
                                break;
                            if ((int)d <= 0) {
                                i++;
                                break;
                            }
                        }
                        if ((int)d < 0)
                            break;
                    }
                    if (c->tok[i].kind != LEX_TOKEN_COLON_COLON)
                        break;
                    i++;
                }
                if (i > c->pos && i < c->ntok &&
                    (c->tok[i].kind == LEX_TOKEN_LEFT_BRACE ||
                     c->tok[i].kind == LEX_TOKEN_LEFT_PAREN)) {
                    int open = c->tok[i].kind,
                        close = open == LEX_TOKEN_LEFT_BRACE
                                    ? LEX_TOKEN_RIGHT_BRACE
                                    : LEX_TOKEN_RIGHT_PAREN;

                    for (d = 0; i < c->ntok; i++) {
                        if (c->tok[i].kind == open)
                            d++;
                        else if (c->tok[i].kind == close && !--d)
                            break;
                    }
                    into = i + 1 < c->ntok &&
                           c->tok[i + 1].kind == LEX_TOKEN_SEMICOLON;
                }
            }
            if (into)
                c->gen_ret_into = AST_QUALIFIED_TYPE_UNQUALIFIED(c->gen_ret),
                c->gen_ret_done = 0;
            cxx_expression_parse(c);
            c->gen_ret_into = CXX_TY_UNKNOWN;
            has_value = true, value = c->enode;
            if (c->ret_capture)
                c->ret_seen = 1;
            /* an auto return type: the first return's (the others must agree) */
            if (c->ret_capture && c->ret_type == CXX_TY_UNKNOWN &&
                !c->tentative)
                c->ret_type = c->etype, c->ret_lval = c->elval;
        } else if (c->ret_capture && c->ret_type == CXX_TY_UNKNOWN &&
                   !c->tentative)
            c->ret_type = cc_type_basic(AST_TYPE_VOID), c->ret_lval = 0;
        cxx_expect(c, LEX_TOKEN_SEMICOLON);
        if (is_return && cxx_gen_on(c)) {
            uint32_t r = cxx_gen_return(c, value, has_value, sloc);

            c->gen_returns = 1;
            if (r)
                cxx_gen_statement(c, r);
        }
        return;
    }
    case CXX_KW_break:
    case CXX_KW_continue: {
        int op =
            cxx_is_kw(c, CXX_KW_break) ? AST_NODE_BREAK : AST_NODE_CONTINUE;

        cxx_advance(c);
        cxx_expect(c, LEX_TOKEN_SEMICOLON);
        if (cxx_gen_on(c))
            cxx_gen_statement(c, cxx_gen_new_statement(c, op, sloc, 0, 0, 0));
        return;
    }
    case CXX_KW_goto: {
        uint32_t gloc = cxx_peek(c, 0)->loc;

        cxx_advance(c);
        if (cxx_accept(c, LEX_TOKEN_STAR))
            cxx_expression_parse(
                c); /* (goto *p: not built, the function gets no code) */
        else {
            uint32_t name = cxx_peek(c, 0)->id;

            cxx_expect(c, LEX_TOKEN_IDENTIFIER);
            if (cxx_gen_on(c) && !c->tentative && !c->template_depth)
                cxx_gen_statement(c, cxx_gen_new_statement(
                                         c, AST_NODE_GOTO, gloc,
                                         cxx_gen_label(c, name, gloc), 0, 0));
        }
        cxx_expect(c, LEX_TOKEN_SEMICOLON);
        return;
    }
    case CXX_KW_case: {
        bool known;
        int64_t lo, hi;

        cxx_advance(c);
        cxx_constant_expression_parse(c);
        known = c->econst, lo = hi = c->evalue;
        if (cxx_accept(c, LEX_TOKEN_ELLIPSIS)) { /* GNU case ranges */
            cxx_constant_expression_parse(c);
            known &= c->econst, hi = c->evalue;
        }
        cxx_expect(c, LEX_TOKEN_COLON);
        if (c->gen_block_temps)
            cxx_gen_fail(
                c); /* (a jump past a temporary that is destroyed at the block's end) */
        c->snode = 0;
        if (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE))
            cxx_substatement_parse(c);
        if (known && cxx_gen_on(c))
            cxx_gen_statement(
                c, cxx_gen_case(c, lo, hi, cxx_gen_nested(c, sloc), sloc));
        return;
    }
    case CXX_KW_default:
        cxx_advance(c);
        cxx_expect(c, LEX_TOKEN_COLON);
        if (c->gen_block_temps)
            cxx_gen_fail(
                c); /* (a jump past a temporary that is destroyed at the block's end) */
        c->snode = 0;
        if (!cxx_is(c, LEX_TOKEN_RIGHT_BRACE))
            cxx_substatement_parse(c);
        if (cxx_gen_on(c))
            cxx_gen_statement(
                c, cxx_gen_new_statement(c, AST_NODE_DEFAULT, sloc, 0, 0,
                                         cxx_gen_nested(c, sloc)));
        return;
    case CXX_KW_try: {
        /* code generation: the body as a function of its own, called through the function that has the
		   exception table; then the handlers, each entered when that call says so */
        bool gen = cxx_gen_on(c) && c->gen_block && !c->tentative &&
                   !c->template_depth;
        uint32_t closure = 0, exc = 0, ti[8], hnode[8], nh = 0;
        cxx_entity *call = NULL;
        bool ok = gen;

        cxx_advance(c);
        if (gen) {
            exc = cxx_gen_scratch(
                c, cc_type_pointer(c->ast, cc_type_basic(AST_TYPE_VOID)), sloc);
            ok = cxx_try_block_parse(c, &closure, &call) && exc;
        } else
            cxx_compound_statement_parse(c);
        while (cxx_is_kw(c, CXX_KW_catch)) {
            cxx_scope *s = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->scope, NULL),
                      *outer = c->scope;
            ast_qualified_type dt = CXX_TY_UNKNOWN;
            uint32_t name = 0, var = 0, hloc = cxx_peek(c, 0)->loc;
            bool all = false;
            cc_id_list hl;
            cxx_entity *e;

            cxx_advance(c);
            cxx_expect(c, LEX_TOKEN_LEFT_PAREN);
            if (cxx_accept(c, LEX_TOKEN_ELLIPSIS))
                all = true;
            else if (cxx_try(c, cxx_handler_declaration)) {
                name = cxx_handler_declaration_parse(c);
                dt = c->type_out;
                if (name <= 1)
                    name = 0;
            } else {
                int depth = 0;

                while (!(depth == 0 && cxx_is(c, LEX_TOKEN_RIGHT_PAREN))) {
                    if (cxx_is(c, LEX_TOKEN_EOF))
                        cxx_fail(c, "unterminated catch");
                    if (cxx_is(c, LEX_TOKEN_LEFT_PAREN))
                        depth++;
                    else if (cxx_is(c, LEX_TOKEN_RIGHT_PAREN))
                        depth--;
                    if (cxx_is(c, LEX_TOKEN_IDENTIFIER) && !cxx_kw(c))
                        name = cxx_peek(c, 0)->id;
                    cxx_advance(c);
                }
            }
            cxx_expect(c, LEX_TOKEN_RIGHT_PAREN);
            cxx_scope_push(c, s);
            e = name ? cxx_declare(c, s, name, CXX_ENTITY_VALUE) : NULL;
            if (e)
                e->ty = dt;
            if (ok && cxx_gen_on(c) && nh < 8) {
                /* its type info (the type caught: without a reference or qualifiers), and its start */
                ast_qualified_type ct = dt;

                if (!all && ct != CXX_TY_UNKNOWN &&
                    AST_TYPE_KIND(c->ast, ct) == AST_TYPE_POINTER &&
                    AST_TYPE(c->ast, ct)->ext)
                    ct = AST_TYPE(c->ast, ct)->base;
                ti[nh] = all                    ? 0
                         : ct == CXX_TY_UNKNOWN ? 0
                                                : cxx_gen_typeinfo(c, ct);
                ok = all || ti[nh];
                cc_id_list_init(&hl);
                ok = ok && cxx_gen_catch_begin(
                               c, exc, name && !all ? dt : CXX_TY_UNKNOWN, &var,
                               &hl, hloc);
                if (e && var)
                    e->sym = var;
            } else
                ok = false;
            cxx_compound_statement_parse(c);
            if (ok && c->snode && cxx_gen_on(c)) {
                cc_id_list_append(c->ast, &hl, c->snode);
                hnode[nh++] =
                    cxx_gen_new_statement(c, AST_NODE_BLOCK, hloc, 0,
                                          cc_id_list_freeze(c->ast, &hl), hl.n);
            } else
                ok = false;
            c->scope = outer;
        }
        if (gen) {
            uint32_t st =
                ok && nh
                    ? cxx_gen_try_statement(
                          c, cxx_gen_try(c, closure, call, ti, nh, exc, sloc),
                          hnode, nh, sloc)
                    : 0;

            if (st && cxx_gen_on(c))
                cxx_gen_statement(c, st);
            else
                cxx_gen_fail(c);
        }
        return;
    }
    case CXX_KW_asm:
    case CXX_KW___asm__:
    case CXX_KW___asm: {
        /* asm("text") or asm volatile("text" : : : ) with no operands: the C front end's asm record (a
		   statement with operands: not built yet) */
        uint32_t aloc = cxx_peek(c, 0)->loc;
        bool vol = false, simple = true;
        uint32_t tmpl = 0;

        cxx_advance(c);
        while (cxx_kw_is_asm_qualifier(c)) {
            vol |= cxx_is_kw(c, CXX_KW_volatile) ||
                   cxx_is_kw(c, CXX_KW___volatile__) ||
                   cxx_is_kw(c, CXX_KW___volatile);
            cxx_advance(c);
        }
        if (cxx_gen_on(c) && cxx_is(c, LEX_TOKEN_LEFT_PAREN) &&
            cxx_peek(c, 1)->kind >= LEX_TOKEN_STRING &&
            cxx_peek(c, 1)->kind <= LEX_TOKEN_UTF32_STRING) {
            uint32_t save = c->pos;

            cxx_advance(c);
            cxx_assignment_expression_parse(c);
            {
                uint32_t n = c->enode;

                while (n && AST_NODE_AT(c->ast, n)->op != AST_NODE_STRING &&
                       AST_NODE_AT(c->ast, n)->a &&
                       (AST_NODE_AT(c->ast, n)->op == AST_NODE_CAST ||
                        AST_NODE_AT(c->ast, n)->op == AST_NODE_ADDRESS))
                    n = AST_NODE_AT(c->ast, n)->a;
                tmpl = n && AST_NODE_AT(c->ast, n)->op == AST_NODE_STRING
                           ? AST_NODE_AT(c->ast, n)->a + 1
                           : 0;
            }
            while (cxx_is(c, LEX_TOKEN_COLON) ||
                   cxx_is(c, LEX_TOKEN_COLON_COLON))
                cxx_advance(c);
            simple = tmpl && cxx_is(c, LEX_TOKEN_RIGHT_PAREN);
            c->pos = save;
        } else
            simple = false;
        cxx_skip_balanced(c);
        cxx_expect(c, LEX_TOKEN_SEMICOLON);
        if (cxx_gen_on(c)) {
            if (!simple) {
                cxx_gen_fail(c);
                return;
            }
            cc *a = c->ast;
            uint32_t ai = a->nasm++;
            ast_asm_statement *r = &a->asms[ai];

            memset(r, 0, sizeof *r);
            r->tmpl = tmpl, r->vol = vol, r->extended = 0;
            r->ops = (ast_asm_operand *)(cc_heap_allocate(
                a, sizeof(ast_asm_operand)));
            r->clob = (uint32_t *)(cc_heap_allocate(a, sizeof(uint32_t)));
            r->labels = (uint32_t *)(cc_heap_allocate(a, sizeof(uint32_t)));
            cxx_gen_statement(
                c, cxx_gen_new_statement(c, AST_NODE_ASM, aloc, ai, 0, 0));
        }
        return;
    }
    case CXX_KW___label__:
        while (!cxx_accept(c, LEX_TOKEN_SEMICOLON))
            cxx_advance(c);
        return;
    }
    if (cxx_declaration_statement_try(c)) {
        cxx_declaration_parse(c, CXX_CONTEXT_BLOCK);
        cxx_gen_statement(
            c,
            0); /* (its variables' declarations were appended to the block) */
        return;
    }
    uint32_t start = c->pos;

    cxx_expression_parse(c);
    if (cxx_gen_on(c) && c->enode)
        cxx_gen_statement(c, cxx_gen_new_statement(c, AST_NODE_EXPRESSION, sloc,
                                                   c->enode, 0, 0));
    if (!c->tentative && !c->template_depth && !c->instantiating) {
        static int dump = -1;

        c->stats
            .expressions++; /* (an expression statement outside templates: its type known?) */
        c->stats.expressions_typed += c->etype != CXX_TY_UNKNOWN;
        if (dump < 0)
            dump = getenv("VCC_CXX_UNTYPED") != NULL;
        if (dump &&
            c->etype ==
                CXX_TY_UNKNOWN) { /* the untyped statements, for the next step */
            for (uint32_t i = start; i < c->pos && i < start + 12; i++)
                fprintf(stderr, "%.*s ", (int)c->tok[i].len, c->tok[i].s);
            fputc('\n', stderr);
        }
    }
    cxx_expect(c, LEX_TOKEN_SEMICOLON);
}
