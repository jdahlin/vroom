/* C++ expressions: postfix expressions (calls, members, subscripts) */
#include "expression.h"
#include "../declaration/declaration.h"

/* did the call just resolved convert an argument by a user-defined conversion (its rank 3 or more)? */
static bool cxx_call_ranks_converted(const cxx *c)
{
    for (uint32_t r = 0; r < c->call_nranks && r < 16; r++)
        if (c->call_ranks[r] >= 3)
            return true;
    return false;
}

/* x.m / p->m: m's type as cxx_name_type gives it, but not const for being this's member in a const
   member function: x is not this (Skia's SkDCubic::chopAt() const writing dst.pts[0] of a local dst);
   x's own constness is applied by the caller */
static void cxx_member_name_type(cxx *c, cxx_entity *m)
{
    uint8_t this_const = c->this_const;

    c->this_const = 0;
    cxx_name_type(c, m);
    c->this_const = this_const;
}

/* VCC_CXX_GEN=2: the innermost postfix expression of each function that code generation did not build */
VPRIV void cxx_postfix_expression_parse(cxx *c)
{
    static __thread int trace = -1;
    uint32_t start = c->pos;

    cxx_postfix_expression_parse1(c);
    if (trace < 0)
        trace = (getenv("VCC_CXX_GEN") && atoi(getenv("VCC_CXX_GEN")) >= 2) ? 1
                : getenv("VCC_CXX_MISSING")                                 ? 2
                                                                            : 0;
    if (trace && !c->enode && cxx_gen_on(c))
        cxx_gen_note_no_node(
            c, start); /* (the function's reason, unless one is known) */
    if (trace == 1 && !c->enode && cxx_gen_on(c) && !c->gen_why) {
        c->gen_why = 1;
        fprintf(stderr, "no node:");
        for (uint32_t i = start; i < c->pos && i < start + 12; i++)
            fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
        fprintf(stderr, " -> %s\n",
                c->etype == CXX_TY_UNKNOWN            ? "untyped"
                : cc_type_is_record(c->ast, c->etype) ? "class"
                                                      : "typed");
    }
}

VPRIV void cxx_postfix_expression_parse1(cxx *c)
{
    cxx_primary_expression_parse(c);
    for (;;) {
        switch (cxx_kind(c)) {
        case LEX_TOKEN_LEFT_SQUARE:
            if (cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE)
                return; /* [[ attribute ]] */
            cxx_advance(c);
            {
                uint32_t targs = c->in_template_args;
                ast_qualified_type base = c->etype;
                uint32_t bn = c->enode, in, iloc = cxx_peek(c, 0)->loc;

                c->in_template_args = 0;
                if (cxx_is(c, LEX_TOKEN_LEFT_BRACE))
                    cxx_braced_init_list_parse(c);
                else
                    cxx_expression_parse(c);
                c->in_template_args = targs;
                in = c->enode;
                /* a[i]: an element of an array or pointer, or a class's operator[] */
                if (!c->tentative &&
                    cxx_operator_call_type(c, vintern_cstr("operator[]"), base,
                                           &c->etype, 1))
                    cxx_gen_operator_call(c, bn, &in, 1, iloc);
                else {
                    cxx_et(c,
                           base != CXX_TY_UNKNOWN &&
                                   (AST_TYPE_KIND(c->ast, base) ==
                                        AST_TYPE_POINTER ||
                                    AST_TYPE_KIND(c->ast, base) ==
                                        AST_TYPE_ARRAY) &&
                                   !AST_TYPE(c->ast, base)->ext
                               ? AST_TYPE(c->ast, base)->base
                               : CXX_TY_UNKNOWN,
                           true);
                    if (c->etype != CXX_TY_UNKNOWN)
                        cxx_gen_index(c, bn, in, iloc);
                }
            }
            cxx_expect(c, LEX_TOKEN_RIGHT_SQUARE);
            continue;
        case LEX_TOKEN_LEFT_PAREN: {
            uint32_t targs = c->in_template_args;
            cxx_entity *set = c->ecallee;
            ast_qualified_type callee = c->etype;
            uint8_t callee_lval = c->elval;
            uint32_t obj_node = c->eobj_node, callee_node = c->enode;
            bool obj_arrow = c->eobj_arrow;
            cxx_targ xa[8];
            uint32_t nx =
                c->xargs_set && c->nxargs <= 8 ? c->nxargs : UINT32_MAX;
            uint8_t call_obj = c->call_obj, call_obj_rv = c->call_obj_rv;
            uint32_t ename = c->ename;
            bool missing = c->ename_missing;
            uint32_t on_args = 0, on_nodes[CXX_MAX_ARGS];

            if (c->xargs_set && nx == UINT32_MAX)
                set = NULL,
                callee = CXX_TY_UNKNOWN; /* (too many explicit arguments) */
            if (nx != UINT32_MAX)
                memcpy(xa, c->xargs, nx * sizeof *xa);

            /* (a virtual function named with a qualifier is called directly; a callee that is not a plain name: not known) */
            uint8_t vmode =
                c->pos >= 2 && c->tok[c->pos - 1].kind == LEX_TOKEN_IDENTIFIER
                    ? (c->tok[c->pos - 2].kind == LEX_TOKEN_COLON_COLON ? 1 : 0)
                    : 2;

            c->in_template_args = 0;
            cxx_call_arguments_parse(c);
            c->in_template_args = targs;
            if (c->ncargs == UINT32_MAX || c->tentative)
                cxx_et(c, CXX_TY_UNKNOWN, false);
            else if (nx != UINT32_MAX && set) {
                uint32_t anodes[CXX_MAX_ARGS],
                    an = c->ncargs, cloc = c->tok[c->pos ? c->pos - 1 : 0].loc;

                memcpy(anodes, c->carg_nodes, sizeof anodes);
                uint32_t abrace[CXX_MAX_ARGS];
                cxx_entity *afns[16];

                uint8_t xkmask = c->carg_kmask;
                int64_t xkval[8];

                memcpy(xkval, c->carg_kval, sizeof xkval);
                memcpy(abrace, c->carg_brace, sizeof abrace);
                memcpy(afns, c->carg_fnset, sizeof afns);
                c->call_obj = call_obj, c->call_obj_rv = call_obj_rv,
                cxx_call_type_x(c, set, xa, nx, c->cargs, c->cargs_lval,
                                c->ncargs);
                if (c->want_constant && an && an <= 8 &&
                    xkmask == (uint8_t)((1u << an) - 1) && !obj_node &&
                    c->call_resolved && c->call_picked && !c->econst &&
                    !c->template_depth && c->etype != CXX_TY_UNKNOWN &&
                    ((AST_TYPE_KIND(c->ast, c->etype) >= AST_TYPE_BOOL &&
                      AST_TYPE_KIND(c->ast, c->etype) <=
                          AST_TYPE_UNSIGNED_LONG_LONG) ||
                     AST_TYPE_KIND(c->ast, c->etype) == AST_TYPE_ENUM)) {
                    /* f<args>(constants) of a constexpr function template (SkAlignedSTStorage<RoundUp<T>(N), T>) */
                    ast_qualified_type rt =
                        AST_QUALIFIED_TYPE_UNQUALIFIED(c->etype);
                    int64_t v;

                    if (cxx_const_call(c, c->call_picked, xkval, an, &v)) {
                        cxx_ec(c, rt,
                               rt == cc_type_basic(AST_TYPE_BOOL) ? v != 0 : v);
                        continue;
                    }
                }
                if (!c->ncargs && !c->econst && !c->template_depth &&
                    c->etype != CXX_TY_UNKNOWN &&
                    AST_TYPE_KIND(c->ast, c->etype) >= AST_TYPE_BOOL &&
                    AST_TYPE_KIND(c->ast, c->etype) <=
                        AST_TYPE_UNSIGNED_LONG_LONG) {
                    /* f<args>() of a constexpr function template: its value, a constant */
                    cxx_entity *te =
                        cxx_entity_kind_find(set, CXX_ENTITY_FUNCTION_TEMPLATE);
                    ast_qualified_type rt =
                        AST_QUALIFIED_TYPE_UNQUALIFIED(c->etype);
                    int64_t v;

                    if (te && cxx_ftemplate_const_call(c, te, xa, nx, &v)) {
                        cxx_ec(c, rt,
                               rt == cc_type_basic(AST_TYPE_BOOL) ? v != 0 : v);
                        continue;
                    }
                }
                if (c->call_resolved && c->call_picked &&
                    c->etype != CXX_TY_UNKNOWN && (!call_obj || obj_node))
                    c->gen_vmode = vmode,
                    memcpy(c->gen_call_brace, abrace, sizeof abrace),
                    cxx_fnset_fill(c, c->call_picked, afns, anodes, an, cloc),
                    cxx_gen_call(c, c->call_picked, obj_node, obj_arrow, anodes,
                                 an, cloc),
                    c->gen_vmode = 0;
            } else if (!set && callee != CXX_TY_UNKNOWN &&
                       cc_type_is_record(
                           c->ast, callee) && /* (operator(): a member only) */
                       (on_args = c->ncargs,
                        memcpy(on_nodes, c->carg_nodes, sizeof on_nodes),
                        c->op_lv_set = c->ncargs <= CXX_MAX_ARGS,
                        c->op_lv[0] = callee_lval,
                        memcpy(c->op_lv + 1, c->cargs_lval,
                               c->ncargs <= CXX_MAX_ARGS ? c->ncargs : 0),
                        cxx_operator_call_type(c, vintern_cstr("operator()"),
                                               callee, c->cargs, c->ncargs))) {
                /* an object of class type: its operator() (the arguments as parsed: typing the call may
					   instantiate its body, whose own calls reuse c->ncargs and c->carg_nodes) */
                if (!callee_node && c->gen)
                    callee_node = cxx_gen_empty_object(
                        c, callee, c->tok[c->pos ? c->pos - 1 : 0].loc);
                cxx_gen_operator_call(c, callee_node, on_nodes, on_args,
                                      c->tok[c->pos ? c->pos - 1 : 0].loc);
            } else {
                uint8_t was =
                    c->einvalid; /* (an earlier part of the expression may be ill-formed already) */
                uint32_t anodes[CXX_MAX_ARGS],
                    an = c->ncargs, cloc = c->tok[c->pos ? c->pos - 1 : 0].loc;

                memcpy(anodes, c->carg_nodes, sizeof anodes);
                uint32_t abrace[CXX_MAX_ARGS];
                cxx_entity *afns[16];
                uint8_t akmask = c->carg_kmask;
                int64_t akval[8];

                memcpy(akval, c->carg_kval, sizeof akval);

                memcpy(abrace, c->carg_brace, sizeof abrace);
                memcpy(afns, c->carg_fnset, sizeof afns);
                c->call_obj = call_obj, c->call_obj_rv = call_obj_rv,
                cxx_call_type(c, set, callee, c->cargs, c->cargs_lval,
                              c->ncargs);
                if (set && ename && !call_obj && c->call_resolved &&
                    !c->unevaluated && /* (evaluated code: an unevaluated operand's verdicts stay ordinary
                                          lookup's, which the SFINAE detection of json's traits reads) */
                    c->call_picked &&
                    (c->call_picked->kind == CXX_ENTITY_FUNCTION_TEMPLATE ||
                     c->call_picked->inst_ftmpl ||
                     cxx_call_ranks_converted(c)) &&
                    !c->call_picked->member_of && !c->template_depth &&
                    !c->tentative && c->ncargs && c->ncargs <= 16) {
                    /* f(x) whose ordinary lookup picked a template: the functions argument-dependent
                       lookup finds are candidates too, and a non-template that fits as well wins
                       ([over.match.best]: json's int_to_string(alt_string&, size_t) beside its
                       template); one candidate set in C++, the two compared here */
                    ast_qualified_type o_type = c->etype;
                    bool o_lval = c->elval;
                    cxx_entity *o_pick = c->call_picked, *o_callee = c->ecallee,
                               *o_inst = c->call_instance;
                    uint8_t o_ranks[16],
                        o_n = c->call_nranks, o_unsure = c->eunsure,
                        o_inv = c->einvalid, o_inv_u = c->einvalid_unsure;
                    bool take = false;

                    memcpy(o_ranks, c->call_ranks, sizeof o_ranks);
                    /* (the argument list as parsed: what the lookup instantiates parses lists of its own) */
                    ast_qualified_type o_args[16];
                    uint8_t o_lv[16];
                    uint32_t o_nargs = c->ncargs, o_nodes[CXX_MAX_ARGS],
                             o_brace[CXX_MAX_ARGS];

                    memcpy(o_args, c->cargs, o_nargs * sizeof *o_args);
                    memcpy(o_lv, c->cargs_lval, o_nargs);
                    memcpy(o_nodes, c->carg_nodes, sizeof o_nodes);
                    memcpy(o_brace, c->carg_brace, sizeof o_brace);
                    cxx_adl_call_type(c, ename, o_args, o_lv, o_nargs);
                    c->ncargs = o_nargs;
                    memcpy(c->cargs, o_args, o_nargs * sizeof *o_args);
                    memcpy(c->cargs_lval, o_lv, o_nargs);
                    memcpy(c->carg_nodes, o_nodes, sizeof o_nodes);
                    memcpy(c->carg_brace, o_brace, sizeof o_brace);
                    if (c->etype != CXX_TY_UNKNOWN && c->call_resolved &&
                        c->call_picked && c->call_picked != o_pick &&
                        c->call_picked->kind == CXX_ENTITY_VALUE &&
                        !c->call_picked->is_instance &&
                        !c->call_picked->inst_ftmpl &&
                        (!c->call_picked->member_of ||
                         c->call_picked->member_of->kind != CXX_SCOPE_CLASS) &&
                        c->call_nranks == o_n) {
                        take = true;
                        /* (a parameter of class type: the argument's class or a base of it, no conversion
                           through a conversion function, which vcc's ranks do not always tell) */
                        const ctype *pft =
                            c->call_picked->ty != CXX_TY_UNKNOWN &&
                                    AST_TYPE_KIND(c->ast, c->call_picked->ty) ==
                                        AST_TYPE_FUNCTION
                                ? AST_TYPE(c->ast, c->call_picked->ty)
                                : NULL;

                        take = pft != NULL;
                        for (uint32_t r = 0;
                             take && r < c->ncargs && r < pft->n; r++) {
                            ast_qualified_type pt =
                                c->ast->params[pft->aux + r];
                            ast_qualified_type at = c->cargs[r];
                            uint64_t off;

                            if (pt != CXX_TY_UNKNOWN &&
                                AST_TYPE_KIND(c->ast, pt) == AST_TYPE_POINTER &&
                                AST_TYPE(c->ast, pt)->ext)
                                pt = AST_TYPE(c->ast, pt)->base;
                            if (at != CXX_TY_UNKNOWN &&
                                AST_TYPE_KIND(c->ast, at) == AST_TYPE_POINTER &&
                                AST_TYPE(c->ast, at)->ext)
                                at = AST_TYPE(c->ast, at)->base;
                            if (pt == CXX_TY_UNKNOWN || at == CXX_TY_UNKNOWN)
                                take = false;
                            else if (cc_type_is_record(c->ast, pt) &&
                                     AST_TYPE_KIND(c->ast, pt) !=
                                         AST_TYPE_ARRAY)
                                take =
                                    AST_QUALIFIED_TYPE_UNQUALIFIED(pt) ==
                                        AST_QUALIFIED_TYPE_UNQUALIFIED(at) ||
                                    (cc_type_is_record(c->ast, at) &&
                                     AST_TYPE_KIND(c->ast, at) !=
                                         AST_TYPE_ARRAY &&
                                     cxx_class_base_offset(
                                         c, AST_QUALIFIED_TYPE_UNQUALIFIED(at),
                                         AST_QUALIFIED_TYPE_UNQUALIFIED(pt),
                                         &off, 0));
                        }
                        for (uint32_t r = 0; take && r < o_n && r < 16; r++)
                            take &=
                                c->call_ranks[r] < 3 &&
                                c->call_ranks[r] <=
                                    o_ranks
                                        [r]; /* (no worse, no user-defined conversion: as good, a
                                                       non-template) */
                    }
                    if (!take) {
                        c->etype = o_type, c->elval = o_lval,
                        c->call_picked = o_pick, c->ecallee = o_callee,
                        c->call_instance = o_inst, c->call_resolved = 1,
                        c->call_nranks = o_n, c->eunsure = o_unsure,
                        c->einvalid = o_inv, c->einvalid_unsure = o_inv_u;
                        memcpy(c->call_ranks, o_ranks, sizeof o_ranks);
                    }
                }
                cxx_entity *cpick = c->call_resolved ? c->call_picked : NULL;
                ast_qualified_type cty = c->etype;
                if (set && c->call_resolved && c->call_picked &&
                    c->etype != CXX_TY_UNKNOWN && (!call_obj || obj_node))
                    c->gen_vmode = vmode,
                    memcpy(c->gen_call_brace, abrace, sizeof abrace),
                    cxx_fnset_fill(c, c->call_picked, afns, anodes, an, cloc),
                    cxx_gen_call(c, c->call_picked, obj_node, obj_arrow, anodes,
                                 an, cloc),
                    c->gen_vmode = 0;
                if (c->want_constant && cpick && an <= 8 &&
                    akmask == (uint8_t)((1u << an) - 1) && !obj_node &&
                    !c->econst && !c->template_depth && cty != CXX_TY_UNKNOWN &&
                    c->etype == cty &&
                    ((AST_TYPE_KIND(c->ast, cty) >= AST_TYPE_BOOL &&
                      AST_TYPE_KIND(c->ast, cty) <=
                          AST_TYPE_UNSIGNED_LONG_LONG) ||
                     AST_TYPE_KIND(c->ast, cty) == AST_TYPE_ENUM)) {
                    /* f() of a constexpr function: a constant too (case char_traits<char>::eof():), the call kept */
                    int64_t cv;
                    uint32_t keep = c->enode;

                    if (cxx_const_call(c, cpick, akval, an, &cv)) {
                        c->econst = 1,
                        c->evalue =
                            cty == cc_type_basic(AST_TYPE_BOOL) ? cv != 0 : cv,
                        c->enode = keep;
                        if (!keep &&
                            c->gen) /* (no call built: its value, a constant) */
                            cxx_gen_integer(c, c->evalue,
                                            AST_QUALIFIED_TYPE_UNQUALIFIED(cty),
                                            cloc);
                    }
                } else if (!set && callee_node &&
                           callee_node == c->bmf_callee && c->bmf_this &&
                           c->etype != CXX_TY_UNKNOWN && an != UINT32_MAX &&
                           an < CXX_MAX_ARGS) {
                    /* (x.*pmf)(args): the function the pointer to member gives, this first */
                    uint32_t th = c->bmf_this, pre = c->bmf_pre;

                    memmove(anodes + 1, anodes, an * sizeof *anodes);
                    anodes[0] = th;
                    c->bmf_callee = c->bmf_this = c->bmf_pre = 0;
                    cxx_gen_indirect_call(c, callee_node, anodes, an + 1, cloc);
                    if (c->enode && pre)
                        c->enode = cc_node_new_operands(
                            c->ast, AST_NODE_COMMA,
                            AST_NODE_AT(c->ast, c->enode)->ty, cloc, pre,
                            c->enode);
                } else if (!set && callee_node && c->etype != CXX_TY_UNKNOWN &&
                           an != UINT32_MAX)
                    cxx_gen_indirect_call(
                        c, callee_node, anodes, an,
                        cloc); /* through a pointer to a function */
                /* f(x) with no f that fits: the functions named f in the namespaces of the
					   arguments' classes (argument-dependent lookup) */
                if ((c->etype == CXX_TY_UNKNOWN ||
                     (set && !c->call_resolved && !c->template_depth &&
                      !c->tentative)) &&
                    ename &&
                    (!set || c->none_viable ||
                     (!c->call_resolved && !c->template_depth &&
                      !c->tentative))) { /* (or several fit as well: one ADL finds may fit better) */
                    bool none = c->none_viable || (!set && missing),
                         known = true;

                    for (uint32_t i = 0; i < c->ncargs && i < 16; i++)
                        known &= c->cargs[i] != CXX_TY_UNKNOWN;
                    cxx_adl_call_type(c, ename, c->cargs, c->cargs_lval,
                                      c->ncargs);
                    /* code generation: the function found there, called (no object: a namespace's
						   function, or a friend the class declares) */
                    if (c->etype != CXX_TY_UNKNOWN && c->call_resolved &&
                        c->call_picked && an != UINT32_MAX &&
                        (!c->call_picked->member_of ||
                         c->call_picked->fn_static ||
                         c->call_picked->member_of->kind != CXX_SCOPE_CLASS))
                        c->gen_vmode = vmode,
                        memcpy(c->gen_call_brace, abrace, sizeof abrace),
                        cxx_fnset_fill(c, c->call_picked, afns, anodes, an,
                                       cloc),
                        cxx_gen_call(c, c->call_picked, 0, false, anodes, an,
                                     cloc),
                        c->gen_vmode = 0;
                    /* nothing by that name fits, here or in the arguments' namespaces */
                    c->einvalid = was || (c->etype == CXX_TY_UNKNOWN && none &&
                                          known && !c->einvalid_unsure);
                    if (c->einvalid && !was)
                        c->einvalid_at = c->pos;
                }
            }
        }
            continue;
        case LEX_TOKEN_PERIOD:
        case LEX_TOKEN_ARROW: {
            /* the object's class: its scope, for the member's lookup */
            ast_qualified_type obj = c->etype;
            cxx_scope *cls = NULL;
            uint32_t obj_node = c->enode, mloc = cxx_peek(c, 0)->loc;
            bool arrow = cxx_is(c, LEX_TOKEN_ARROW);
            uint8_t obj_rv =
                !arrow &&
                (!c->elval ||
                 (c->edecl != CXX_TY_UNKNOWN &&
                  AST_TYPE_KIND(c->ast, c->edecl) == AST_TYPE_POINTER &&
                  AST_TYPE(c->ast, c->edecl)->ext ==
                      2)); /* (x.f() of an rvalue x, std::move(x).f(): f() && only) */

            /* x->m on an object of class type: (x.operator->())->m, again while the result is a class
			   ([over.ref]: p->m is interpreted as (p.operator->())->m) */
            for (uint32_t hop = 0;
                 arrow && hop < 8 && !c->tentative && obj != CXX_TY_UNKNOWN &&
                 cc_type_is_record(c->ast, obj) &&
                 AST_TYPE_KIND(c->ast, obj) != AST_TYPE_ARRAY;
                 hop++) {
                if (!cxx_operator_call_type(c, vintern_cstr("operator->"), obj,
                                            NULL, 0) ||
                    c->etype == CXX_TY_UNKNOWN) {
                    obj = CXX_TY_UNKNOWN;
                    break;
                }
                if (c->call_resolved && c->call_picked)
                    cxx_gen_operator_call(c, obj_node, NULL, 0, mloc);
                obj = c->etype,
                obj_node = c->call_resolved && c->call_picked ? c->enode : 0;
            }
            if (cxx_is(c, LEX_TOKEN_ARROW))
                obj = obj != CXX_TY_UNKNOWN &&
                              AST_TYPE_KIND(c->ast, obj) == AST_TYPE_POINTER &&
                              !AST_TYPE(c->ast, obj)->ext
                          ? AST_TYPE(c->ast, obj)->base
                          : CXX_TY_UNKNOWN;
            if (obj != CXX_TY_UNKNOWN && !c->tentative)
                cls = cxx_type_class_scope(c,
                                           AST_QUALIFIED_TYPE_UNQUALIFIED(obj));
            if (obj != CXX_TY_UNKNOWN && !c->tentative &&
                !cc_type_is_record(c->ast, obj) && !cxx_is(c, LEX_TOKEN_TILDE))
                CXX_INVALID(
                    c); /* x.m on what is not a class (a requirement fails) */
            cxx_et(c, CXX_TY_UNKNOWN, false);
            cxx_advance(c);
            {
                /* a member: names are looked up in the object's class, which the parser does not know;
				   template-ids need 'template' when dependent, else lookup by name finds member templates */
                cxx_scope *q;
                bool dep, template_kw;
                uint32_t id;

                template_kw = cxx_accept_kw(c, CXX_KW_template);
                cxx_nested_name_parse(c, &q, &dep);
                template_kw |= cxx_accept_kw(c, CXX_KW_template);
                if (cxx_is(c, LEX_TOKEN_TILDE) && !q && !dep &&
                    cxx_peek(c, 1)->kind == LEX_TOKEN_IDENTIFIER &&
                    cxx_peek(c, 2)->kind == LEX_TOKEN_LEFT_PAREN &&
                    cxx_peek(c, 3)->kind == LEX_TOKEN_RIGHT_PAREN) {
                    /* x.~T(), p->~T(): the destructor called (of a scalar: nothing) */
                    uint32_t dloc = cxx_peek(c, 0)->loc;

                    c->pos += 4;
                    cxx_et(c, cc_type_basic(AST_TYPE_VOID), false);
                    if (!c->tentative && !c->template_depth && obj_node &&
                        obj != CXX_TY_UNKNOWN)
                        cxx_gen_destroy_object(c, obj_node, arrow, dloc);
                    continue;
                }
                if (cxx_is(c, LEX_TOKEN_TILDE) ||
                    cxx_is_kw(c, CXX_KW_operator)) {
                    /* x.operator->(), x.operator+=(y): the member operator by its name */
                    uint32_t oid = 0;

                    if (cxx_is_kw(c, CXX_KW_operator) && cls && !q && !dep) {
                        uint32_t save = c->pos, on;

                        c->pos++;
                        if (cxx_is(c, LEX_TOKEN_LEFT_PAREN) &&
                            cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_PAREN)
                            oid = vintern_cstr("operator()");
                        else if (cxx_is(c, LEX_TOKEN_LEFT_SQUARE) &&
                                 cxx_peek(c, 1)->kind == LEX_TOKEN_RIGHT_SQUARE)
                            oid = vintern_cstr("operator[]");
                        else if ((cxx_kind(c) >= LEX_TOKEN_LEFT_SQUARE &&
                                  cxx_kind(c) < LEX_TOKEN_HASH) ||
                                 cxx_is(c, CXX_TOKEN_SPACESHIP) ||
                                 cxx_is(c, CXX_TOKEN_ARROW_STAR)) {
                            cxx_operator_at(c, &on);
                            oid = cxx_operator_id_at(c, on);
                        }
                        c->pos = save;
                    }
                    cxx_unqualified_id_rest_parse(c);
                    cxx_et(c, CXX_TY_UNKNOWN, false);
                    if (oid > 1) {
                        cxx_entity *m = cxx_lookup_in(c, cls, oid, 0);

                        if (m && m->kind != CXX_ENTITY_DEPENDENT) {
                            cxx_member_name_type(c, m);
                            c->call_obj = (AST_QUALIFIED_TYPE_QUALIFIERS(obj) &
                                           AST_QUALIFIER_CONST)
                                              ? 2
                                              : 1;
                            c->call_obj_rv = obj_rv;
                            c->eobj_node = obj_node,
                            c->eobj_arrow =
                                arrow; /* (code generation: the call's object) */
                        } else if (!m && !cls->dependent &&
                                   !cls->has_dependent_base)
                            CXX_INVALID(c);
                    }
                    continue;
                }
                if (!cxx_is(c, LEX_TOKEN_IDENTIFIER))
                    cxx_fail(c, "expected a member name");
                id = cxx_peek(c, 0)->id;
                cxx_advance(c);
                /* the object's type is not known here: name<...>( is a member template call if it parses */
                /* (a known class whose member of that name is no template: < is less-than, however
				   what follows parses: a.n < 1 || a.n > (int)4, skcms) */
                bool not_template = false;

                if (!template_kw && cls && !q && !dep && !cls->dependent &&
                    !cls->has_dependent_base && cxx_is(c, LEX_TOKEN_LESS)) {
                    cxx_entity *m = cxx_lookup_in(c, cls, id, 0);

                    not_template = m && m->kind != CXX_ENTITY_DEPENDENT;
                    for (cxx_entity *x = m; x && not_template; x = x->next)
                        not_template =
                            x->kind != CXX_ENTITY_TEMPLATE &&
                            x->kind != CXX_ENTITY_FUNCTION_TEMPLATE &&
                            !cxx_entity_template(x);
                }
                if (cxx_is(c, LEX_TOKEN_LESS) && !not_template &&
                    (template_kw ||
                     cxx_lookahead_fn(c, cxx_template_arguments_then_call))) {
                    cxx_entity *m =
                        cls && !q && !dep ? cxx_lookup_in(c, cls, id, 0) : NULL;

                    cxx_template_arguments_parse(c);
                    cxx_et(c, CXX_TY_UNKNOWN, false);
                    /* x.f<args>(...): the member function templates named f, with the explicit arguments */
                    if (m && m->kind != CXX_ENTITY_DEPENDENT &&
                        cxx_entity_kind_find(m, CXX_ENTITY_FUNCTION_TEMPLATE) &&
                        !c->tentative) {
                        if (c->ntargs > c->cxargs)
                            c->xargs = (cxx_targ *)(realloc(
                                c->xargs, (c->cxargs = c->ntargs + 8) *
                                              sizeof *c->xargs));
                        memcpy(c->xargs, c->targs,
                               c->ntargs * sizeof *c->xargs);
                        c->nxargs = c->ntargs, c->xargs_set = 1, c->ecallee = m;
                        c->call_obj = (AST_QUALIFIED_TYPE_QUALIFIERS(obj) &
                                       AST_QUALIFIER_CONST)
                                          ? 2
                                          : 1;
                        c->call_obj_rv = obj_rv;
                        c->eobj_node = obj_node,
                        c->eobj_arrow =
                            arrow; /* (code generation: the call's object) */
                    } else if (cls && !q && !dep && !m && !cls->dependent &&
                               !cls->has_dependent_base && !c->tentative)
                        CXX_INVALID(c); /* no such member template */
                    continue;
                }
                /* the member: a data member's type, or the member functions for the call */
                if (cls && !q && !dep) {
                    cxx_entity *m = cxx_lookup_in(c, cls, id, 0);

                    if (m && m->kind != CXX_ENTITY_DEPENDENT) {
                        cxx_member_name_type(c, m);
                        c->call_obj = (AST_QUALIFIED_TYPE_QUALIFIERS(obj) &
                                       AST_QUALIFIER_CONST)
                                          ? 2
                                          : 1;
                        c->call_obj_rv = obj_rv;
                        /* a data member of a const object is const (not a mutable one, not a reference):
						   x.layout.begin_ptr() on a const x calls the const overload */
                        if (c->etype != CXX_TY_UNKNOWN && c->edecl_name &&
                            !c->ecallee &&
                            (AST_QUALIFIED_TYPE_QUALIFIERS(obj) &
                             AST_QUALIFIER_CONST)) {
                            cxx_entity *fe = m;

                            while (fe && !(fe->kind == CXX_ENTITY_VALUE &&
                                           fe->field_of))
                                fe = fe->next;
                            if (fe && !fe->is_mutable &&
                                !(AST_TYPE_KIND(c->ast, c->etype) ==
                                      AST_TYPE_POINTER &&
                                  AST_TYPE(c->ast, c->etype)->ext))
                                c->etype = AST_QUALIFIED_TYPE(
                                    AST_QUALIFIED_TYPE_INDEX(c->etype),
                                    AST_QUALIFIED_TYPE_QUALIFIERS(c->etype) |
                                        AST_QUALIFIER_CONST);
                        }
                        if (c->etype != CXX_TY_UNKNOWN && c->edecl_name &&
                            !c->ecallee)
                            cxx_gen_member(c, obj_node, arrow, id,
                                           mloc); /* a data member */
                        else
                            c->eobj_node = obj_node,
                            c->eobj_arrow =
                                arrow; /* member functions: the call's object */
                    } else if (!m && !cls->dependent &&
                               !cls->has_dependent_base)
                        CXX_INVALID(
                            c); /* no such member (a requirement fails) */
                }
            }
            continue;
        }
        case LEX_TOKEN_PLUS_PLUS:
        case LEX_TOKEN_MINUS_MINUS: {
            int op = cxx_kind(c);
            uint32_t xn = c->enode, oploc = cxx_peek(c, 0)->loc;
            bool built_in =
                c->etype != CXX_TY_UNKNOWN &&
                (cc_type_is_arithmetic(c->ast, c->etype) ||
                 AST_TYPE_KIND(c->ast, c->etype) == AST_TYPE_POINTER);

            ast_qualified_type xt = c->etype;
            uint8_t xl = c->elval;

            cxx_advance(c);
            if (!built_in && xt != CXX_TY_UNKNOWN &&
                cc_type_is_record(c->ast, xt) &&
                AST_TYPE_KIND(c->ast, xt) != AST_TYPE_ARRAY && !c->tentative) {
                /* x++ of a class: x.operator++(0), operator++(int) ([over.inc]): out++ of an iterator */
                ast_qualified_type it = cc_type_basic(AST_TYPE_INT);

                c->op_lv_set = 1, c->op_lv[0] = xl, c->op_lv[1] = 0;
                if (cxx_operator_call_type(
                        c,
                        vintern_cstr(op == LEX_TOKEN_PLUS_PLUS ? "operator++"
                                                               : "operator--"),
                        xt, &it, 1) &&
                    c->call_resolved && c->call_picked) {
                    uint32_t zero =
                        cxx_gen_on(c)
                            ? cc_node_integer_new(c->ast, 0, it, oploc)
                            : 0;

                    cxx_gen_operator_call(c, xn, &zero, 1, oploc);
                } else
                    cxx_et(c, CXX_TY_UNKNOWN, false);
                c->op_lv_set = 0;
                continue;
            }
            cxx_et(c,
                   built_in ? AST_QUALIFIED_TYPE_UNQUALIFIED(c->etype)
                            : CXX_TY_UNKNOWN,
                   false);
            if (built_in)
                cxx_gen_postfix(c, op, xn, oploc);
            continue;
        }
        }
        return;
    }
}

/* ---- unary, cast, binary ----------------------------------------------------------------- */
