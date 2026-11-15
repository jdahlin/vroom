/* C++ code generation: temporaries, class arguments, binding values to objects, parameters and references; conversions */
#include "codegen.h"

/* T(args), T{args}: a temporary of class type, constructed (cxx_ctor_use was called for it) */
static void cxx_gen_construct_temp1(cxx *c, ast_qualified_type t,
                                    const uint32_t *args, uint32_t nargs,
                                    uint32_t loc);

/* (a temporary whose construction needs another of its kind for an argument, and so on: a constructor
   chosen through a conversion vcc got wrong; past 64, not built rather than a stack overflow) */
static __thread uint32_t cxx_gen_temp_depth;

VPRIV void cxx_gen_construct_temp(cxx *c, ast_qualified_type t,
                                  const uint32_t *args, uint32_t nargs,
                                  uint32_t loc)
{
    if (cxx_gen_temp_depth > 64) {
        c->enode = 0;
        cxx_gen_fail(c);
        return;
    }
    cxx_gen_temp_depth++;
    cxx_gen_construct_temp1(c, t, args, nargs, loc);
    cxx_gen_temp_depth--;
}

static void cxx_gen_construct_temp1(cxx *c, ast_qualified_type t,
                                    const uint32_t *args, uint32_t nargs,
                                    uint32_t loc)
{
    cc *a = c->ast;
    cxx_entity *picked = c->ctor_picked;
    bool found = c->ctor_found, none = c->ctor_none;
    cc_id_list l;
    uint32_t tmp, node, expr, call = 0, zero = 0;

    if (!cxx_gen_on(c))
        return;
    c->enode = 0;
    t = AST_QUALIFIED_TYPE_UNQUALIFIED(t);
    if (nargs == UINT32_MAX || nargs > CXX_MAX_ARGS ||
        !cxx_gen_class_ok2(c, t, true))
        return;
    for (uint32_t i = 0; i < nargs; i++)
        if (!args[i] && !(c->gen_agg_brace && c->gen_agg_brace[i]) &&
            !(i < CXX_MAX_ARGS && c->gen_call_brace[i]))
            return; /* (an aggregate's element that is a braced list has no node: cxx_gen_aggregate_init parses it;
                       T({...}): a constructor's braced argument, a temporary of its parameter's class) */
    uint32_t call_brace[CXX_MAX_ARGS];

    memcpy(
        call_brace, c->gen_call_brace,
        sizeof call_brace); /* (the temporary's destructor, made now, may make calls of its own) */
    if (nargs == 1 && args[0] && cxx_gen_prv_find(c, args[0]) &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, args[0])->ty) == t) {
        c->enode = args[0]; /* T(a prvalue of T): that object */
        return;
    }
    const uint32_t *agg_brace =
        c->gen_agg_brace; /* (the temporary's destructor, made now, may build lists of its own) */
    uint32_t into = 0, live = 0;

    if (c->gen_ret_into && c->gen_sret &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(c->gen_ret_into) == t) {
        /* return T{...};: T built in the caller's object, no temporary ([stmt.return], [class.temporary]/2) */
        into = 1;
        c->gen_ret_into = 0;
        tmp = 0;
    } else {
        c->gen_cond_ok =
            1; /* (in an operand that may not run: a flagged temporary, live set below) */
        tmp = cxx_gen_class_temp(c, t, loc);
        live = c->gen_cond_live, c->gen_cond_live = 0;
        if (!tmp)
            return;
    }
    c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
    c->gen_agg_brace = agg_brace;
    memcpy(c->gen_call_brace, call_brace, sizeof call_brace);
    cc_id_list_init(&l);
    if (!nargs &&
        (!(found && picked && picked->member_of == cxx_tag_scope_get(c, t)) ||
         picked->fn_defaulted)) {
        /* T() of a class without a user-provided default constructor: zeroes (then constructed, below, if
		   anything needs it) */
        uint32_t zargs[3], saved = c->enode;

        zargs[0] = cc_address_of_build(
            a,
            (into
                 ? cc_dereference_build(
                       a,
                       cc_node_rvalue(a, cxx_gen_var_node(c, c->gen_sret, loc)),
                       loc)
                 : cxx_gen_var_node(c, tmp, loc)),
            loc);
        zargs[1] = cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_INT), loc);
        zargs[2] =
            cc_node_integer_new(a, (i128)AST_TYPE(a, t)->size,
                                cc_type_basic(AST_TYPE_UNSIGNED_LONG), loc);
        c->enode = 0;
        cxx_gen_builtin(c, "__builtin_memset", 16, zargs, 3, loc);
        zero = c->enode;
        c->enode = saved;
        if (!zero)
            return;
        if (cxx_gen_needs_construction(c, t, 0)) {
            c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
            cc_id_list_append(
                a, &l,
                cc_statement_new(a, AST_NODE_EXPRESSION, loc, zero, 0, 0));
            zero = 0;
            if (!cxx_gen_construct_at(
                    c,
                    (into ? cc_dereference_build(
                                a,
                                cc_node_rvalue(
                                    a, cxx_gen_var_node(c, c->gen_sret, loc)),
                                loc)
                          : cxx_gen_var_node(c, tmp, loc)),
                    t, args, nargs, &l, loc)) {
                cxx_gen_fail(c);
                return;
            }
        }
    } else if (!cxx_gen_construct_at(
                   c,
                   (into ? cc_dereference_build(
                               a,
                               cc_node_rvalue(
                                   a, cxx_gen_var_node(c, c->gen_sret, loc)),
                               loc)
                         : cxx_gen_var_node(c, tmp, loc)),
                   t, args, nargs, &l, loc)) {
        cxx_gen_fail(
            c); /* (the temporary is declared: nothing of this function is kept) */
        return;
    }
    if (live &&
        zero) { /* (the zeroing is all the construction: before live is set) */
        cc_id_list_append(
            a, &l, cc_statement_new(a, AST_NODE_EXPRESSION, loc, zero, 0, 0));
        zero = 0;
    }
    if (live) /* (built: its cleanup destroys it now) */
        cc_id_list_append(
            a, &l,
            cc_statement_new(
                a, AST_NODE_EXPRESSION, loc,
                cc_assignment_build(
                    a, 0, cxx_gen_var_node(c, live, loc),
                    cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc),
                    loc),
                0, 0));
    expr = zero && !live ? zero : cxx_gen_chain(c, &l, 0, loc);
    if (!zero && l.n == 1 && AST_NODE_AT(a, expr)->op == AST_NODE_CALL &&
        AST_NODE_AT(a, expr)->c)
        call = expr;
    node = cc_address_of_build(
        a,
        (into ? cc_dereference_build(
                    a, cc_node_rvalue(a, cxx_gen_var_node(c, c->gen_sret, loc)),
                    loc)
              : cxx_gen_var_node(c, tmp, loc)),
        loc);
    if (expr)
        node = cc_node_new_operands(a, AST_NODE_COMMA, AST_NODE_AT(a, node)->ty,
                                    loc, expr, node);
    node = cc_dereference_build(a, node, loc);
    if (into) {
        c->gen_ret_done =
            node; /* (not a temporary: the return has nothing to copy) */
        c->enode = node;
        return;
    }
    if (call)
        cxx_gen_prv_add(c, node, expr, call, tmp);
    else
        cxx_gen_prv_add(c, node, 0, 0, tmp);
    if (live) {
        cxx_prv *p = cxx_gen_prv_find(c, node);

        if (p)
            p->flagged = 1;
    }
    c->enode = node;
}

/* an argument for a parameter of a class passed by address: the address of the temporary the argument is,
   or of a temporary copied from the argument (0: not built) */
VPRIV uint32_t cxx_gen_class_argument(cxx *c, uint32_t x, ast_qualified_type pt,
                                      uint32_t loc)
{
    cc *a = c->ast;
    ast_qualified_type t = AST_QUALIFIED_TYPE_UNQUALIFIED(pt), at[1];
    uint8_t lv[1];
    cxx_entity *call_picked = c->call_picked;
    uint8_t call_resolved = c->call_resolved, none_viable = c->none_viable,
            elval = c->elval, econst = c->econst, einvalid = c->einvalid;
    ast_qualified_type etype = c->etype;
    int64_t evalue = c->evalue;
    cxx_entity *picked;
    bool found, none;
    cc_id_list l;
    uint32_t tmp;

    if (!x)
        return 0;
    if (AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty) == t &&
        cxx_gen_prv_find(c, x))
        return cc_address_of_build(a, x, loc);
    if (AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty) == t &&
        !(AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE))
        return 0; /* (a prvalue of the class that is not a call's: not built yet) */
    {
        /* an object of a class derived from the parameter's: its base subobject, copied (priority_tag<4>{} for
		   priority_tag<1>) */
        ast_qualified_type xt =
            AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty);
        uint64_t off;

        if (xt != t && cc_type_is_record(a, xt) &&
            AST_TYPE_KIND(a, xt) != AST_TYPE_ARRAY &&
            (AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) &&
            cxx_class_base_offset(c, xt, t, &off, 0) &&
            !(x = cxx_gen_base_object(c, x, t, loc)))
            return 0;
    }
    /* the parameter copy-initialized from the argument: a copy, or a converting constructor from a scalar,
	   pointer or array (not an explicit one: json from 3 or "text" for operator=(basic_json)) */
    /* (a call's result is a prvalue though its node is an object here: CompatibleType&& must not deduce T&) */
    at[0] = AST_NODE_AT(a, x)->ty,
    lv[0] = (AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) != 0 &&
            !cxx_gen_prv_find(c, x);
    {
        /* nullptr (vcc's node: 0 as a void *): a std::nullptr_t prvalue (std::function cb = nullptr) */
        if (cxx_gen_is_nullptr_node(c, x)) {
            at[0] = c->nullptr_ty, lv[0] = 0;
            if (!(x = cxx_gen_nullptr_object(c, x, loc)))
                return 0;
        }
    }
    c->skip_explicit = AST_QUALIFIED_TYPE_UNQUALIFIED(at[0]) != t ? 2 : 0;
    cxx_ctor_use(c, t, at, lv, 1, false);
    c->skip_explicit = 0;
    picked = c->ctor_picked, found = c->ctor_found, none = c->ctor_none;
    c->call_picked = call_picked, c->call_resolved = call_resolved,
    c->none_viable = none_viable;
    c->etype = etype, c->elval = elval, c->econst = econst, c->evalue = evalue,
    c->einvalid = einvalid;
    c->gen_cond_ok =
        1; /* (an argument in an operand that may not run, a && f(lambda): a flagged temporary) */
    tmp = cxx_gen_class_temp(c, t, loc);
    uint32_t live = c->gen_cond_live;

    c->gen_cond_live = 0;
    if (!tmp)
        return 0;
    c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
    cc_id_list_init(&l);
    if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, tmp, loc), t, &x, 1, &l,
                              loc)) {
        cxx_gen_fail(c);
        return 0;
    }
    if (live) /* (built: its cleanup destroys it now) */
        cc_id_list_append(
            a, &l,
            cc_statement_new(
                a, AST_NODE_EXPRESSION, loc,
                cc_assignment_build(
                    a, 0, cxx_gen_var_node(c, live, loc),
                    cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc),
                    loc),
                0, 0));
    return cxx_gen_chain(
        c, &l, cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc), loc);
}

VPRIV void cxx_gen_call(cxx *c, cxx_entity *f, uint32_t object, bool arrow,
                        const uint32_t *args, uint32_t nargs, uint32_t loc);

/* an object of class type as a value of the scalar type `to`: its conversion function to exactly that type
   (operator bool for a condition); 0: none, or not built */
VPRIV uint32_t cxx_gen_convert_class(cxx *c, uint32_t x, ast_qualified_type to,
                                     uint32_t loc)
{
    cc *a = c->ast;
    cxx_scope *cls;
    cxx_entity *f;
    char nm[64] = {0};
    uint32_t saved = c->enode, n;

    if (x && cc_type_is_record(a, AST_NODE_AT(a, x)->ty) &&
        AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != AST_TYPE_ARRAY &&
        !(AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) &&
        cxx_gen_trivial_abi(c, AST_NODE_AT(a, x)->ty, 0) == 1 && c->gen_block) {
        /* a prvalue of a trivially copyable class (a <=> b's std::strong_ordering): held in a temporary,
           its conversion function called on that */
        ast_qualified_type xt =
            AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty);
        uint32_t tmp = cxx_gen_temporary(c, xt, loc);

        if (!tmp)
            return 0;
        x = cc_dereference_build(
            a,
            cc_node_new_operands(
                a, AST_NODE_COMMA, cc_type_pointer(a, xt), loc,
                cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc), x,
                                    loc),
                cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc)),
            loc);
    }
    if (!x || !cc_type_is_record(a, AST_NODE_AT(a, x)->ty) ||
        AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == AST_TYPE_ARRAY ||
        !(AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) ||
        !(cls = cxx_tag_scope_get(
              c, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty))))
        return 0;
    snprintf(nm, 32, "operator #%u",
             (unsigned)AST_QUALIFIED_TYPE_UNQUALIFIED(to));
    f = cxx_lookup_in(c, cls, vintern_cstr(nm), 0);
    if (!f && cc_type_is_arithmetic(a, to) && !cls->conv_unknown &&
        !cls->nused) {
        /* none to that type: the one conversion function to an arithmetic type, then a standard conversion
		   (fpos's operator streamoff() for an off_t; [over.match.conv]) */
        ast_qualified_type via = CXX_TY_UNKNOWN;
        uint32_t n1 = 0;

        for (uint32_t i = 0; i < cls->nconv; i++)
            if (cc_type_is_arithmetic(a, cls->conv_types[i]) &&
                AST_TYPE_KIND(a, cls->conv_types[i]) != AST_TYPE_POINTER)
                via = AST_QUALIFIED_TYPE_UNQUALIFIED(cls->conv_types[i]), n1++;
        if (n1 == 1 && via != AST_QUALIFIED_TYPE_UNQUALIFIED(to)) {
            uint32_t v = cxx_gen_convert_class(c, x, via, loc);

            return v ? cc_conversion_apply(a, cc_node_rvalue(a, v),
                                           AST_QUALIFIED_TYPE_UNQUALIFIED(to))
                     : 0;
        }
    }
    if (!f && cls->has_conversion) {
        /* a conversion function template (json's template <typename ValueType> operator ValueType() const):
           its instance for the target type, T deduced as it ([temp.deduct.conv]: here, the pattern a bare
           T, given as the explicit argument), when its return type is that */
        for (uint32_t i = 0; i < cls->nslots && !f; i++)
            for (cxx_entity *e = cls->slots[i]; e && !f; e = e->next) {
                const vident *v = &vintern_ents[e->name];
                cxx_targ xa =
                    VCC_LITERAL(cxx_targ, .kind = 1,
                                .t = AST_QUALIFIED_TYPE_UNQUALIFIED(to));
                ast_qualified_type r = CXX_TY_UNKNOWN;
                cxx_entity *ci = c->call_instance;

                if (e->kind != CXX_ENTITY_FUNCTION_TEMPLATE || v->len != 11 ||
                    memcmp(v->s, "operator #t", 11))
                    continue;
                c->call_instance = NULL;
                int dd =
                    cxx_ftemplate_deduce(c, e, &xa, 1, NULL, NULL, 0, &r, true);

                if (dd == 1 && c->call_instance &&
                    c->call_instance->inst_ftmpl == e->ftmpl &&
                    c->call_instance->ty == CXX_TY_UNKNOWN)
                    /* (the pattern's return type, T, not known as one: the instance's type is T's, () ) */
                    c->call_instance->ty = cxx_type_function(
                        c, AST_QUALIFIED_TYPE_UNQUALIFIED(to), NULL, 0, false);
                if (dd == 1 && c->call_instance &&
                    c->call_instance->inst_ftmpl == e->ftmpl &&
                    c->call_instance->ty != CXX_TY_UNKNOWN &&
                    AST_QUALIFIED_TYPE_UNQUALIFIED(
                        AST_TYPE(a, c->call_instance->ty)->base) ==
                        AST_QUALIFIED_TYPE_UNQUALIFIED(to))
                    f = c->call_instance;
                c->call_instance = ci;
                if (f && !f->sym)
                    cxx_member_body_use(
                        c,
                        f); /* (its body: instantiated for code, as a call's) */
            }
    }
    if (f && f->next) {
        /* overloads that differ in cv (operator T() const and const volatile: std::atomic): the one with
		   the fewest qualifiers the object's binds to */
        uint8_t oq = AST_QUALIFIED_TYPE_QUALIFIERS(AST_NODE_AT(a, x)->ty) &
                     (AST_QUALIFIER_CONST | AST_QUALIFIER_VOLATILE);
        cxx_entity *best = NULL;

        for (cxx_entity *e = f; e; e = e->next) {
            if (e->kind != CXX_ENTITY_VALUE || !e->member_of ||
                (e->fn_const & oq) != oq)
                continue;
            if (best && (best->fn_const & e->fn_const) != best->fn_const &&
                (best->fn_const & e->fn_const) != e->fn_const)
                return 0; /* (neither has fewer: not chosen here) */
            if (!best || (best->fn_const & e->fn_const) == e->fn_const)
                best = e;
        }
        f = best;
        if (!f || f->fn_deleted)
            return 0;
    } else if (!f || f->kind != CXX_ENTITY_VALUE || f->fn_deleted)
        return 0;
    if (f->late && !f->late_done)
        cxx_member_body_use(c, f);
    c->enode = 0;
    cxx_gen_call(c, f, x, false, NULL, 0, loc);
    n = c->enode;
    c->enode = saved;
    return n;
}

/*
 * The value x for an object, parameter or result of type to: converted for a scalar, as it is for a class,
 * and for a reference the address of the lvalue it binds to (for a const T& or T&& bound to a value: of a
 * temporary holding it).  0: not built.
 */
/* nullptr (vcc's node: 0 cast to void *) */
VPRIV bool cxx_gen_is_nullptr_node(cxx *c, uint32_t x)
{
    cc *a = c->ast;
    const struct node *nx = AST_NODE_AT(a, x);

    return c->nullptr_ty && nx->op == AST_NODE_CAST &&
           AST_TYPE_KIND(a, nx->ty) == AST_TYPE_POINTER &&
           AST_TYPE(a, nx->ty)->base == cc_type_basic(AST_TYPE_VOID) && nx->a &&
           AST_NODE_AT(a, nx->a)->op == AST_NODE_NUMBER &&
           a->ipool[AST_NODE_AT(a, nx->a)->a] == 0;
}

/* a nullptr_t object holding pointer value x (a temporary, an lvalue): *(void **)&tmp = x, tmp */
VPRIV uint32_t cxx_gen_nullptr_object(cxx *c, uint32_t x, uint32_t loc)
{
    cc *a = c->ast;
    ast_qualified_type vp = cc_type_pointer(a, cc_type_basic(AST_TYPE_VOID));
    uint32_t tmp = cxx_gen_temporary(c, c->nullptr_ty, loc), tv, slot, asg;

    if (!tmp)
        return 0;
    tv = cc_node_new(a, AST_NODE_VARIABLE, a->syms[tmp].ty, loc);
    AST_NODE_AT(a, tv)->a = tmp,
                   AST_NODE_AT(a, tv)->flags = AST_NODE_FLAG_LVALUE;
    slot = cc_dereference_build(
        a,
        cc_cast_insert(a, cc_address_of_build(a, tv, loc),
                       cc_type_pointer(a, vp), AST_CAST_NOP),
        loc);
    asg = cc_assignment_build(
        a, 0, slot, cc_conversion_apply(a, cc_node_rvalue(a, x), vp), loc);
    tv = cc_node_new(a, AST_NODE_VARIABLE, a->syms[tmp].ty, loc);
    AST_NODE_AT(a, tv)->a = tmp,
                   AST_NODE_AT(a, tv)->flags = AST_NODE_FLAG_LVALUE;
    x = cc_node_new_operands(a, AST_NODE_COMMA, a->syms[tmp].ty, loc, asg, tv);
    AST_NODE_AT(a, x)->flags |= AST_NODE_FLAG_LVALUE;
    return x;
}

VPRIV uint32_t cxx_gen_bind(cxx *c, uint32_t x, ast_qualified_type to,
                            uint32_t loc)
{
    cc *a = c->ast;

    if (!x)
        return 0;
    if (cxx_memptr_of(c, to))
        return cxx_gen_memptr_convert(
            c, x, to,
            loc); /* (a pointer to member: null, a base's to a derived class's) */
    if (c->nullptr_ty &&
        AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == AST_TYPE_POINTER &&
        !AST_TYPE(a, AST_NODE_AT(a, x)->ty)->ext &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(cxx_gen_is_reference(c, to)
                                           ? AST_TYPE(a, to)->base
                                           : to) == c->nullptr_ty) {
        /* nullptr (a void * here) to a std::nullptr_t parameter: an object of that type */
        x = cxx_gen_nullptr_object(c, x, loc);
        if (!x)
            return 0;
        if (!cxx_gen_is_reference(c, to))
            return cc_node_rvalue(a, x);
        return cc_cast_insert(a, cc_address_of_build(a, x, loc),
                              AST_QUALIFIED_TYPE_UNQUALIFIED(to), AST_CAST_NOP);
    }
    if (c->nullptr_ty &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty) ==
            c->nullptr_ty &&
        (AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) &&
        AST_TYPE_KIND(a, to) == AST_TYPE_POINTER &&
        !cxx_gen_is_reference(c, to)) {
        /* a std::nullptr_t object to a pointer: the void * it holds */
        ast_qualified_type vp =
            cc_type_pointer(a, cc_type_basic(AST_TYPE_VOID));

        x = cc_dereference_build(
            a,
            cc_cast_insert(a, cc_address_of_build(a, x, loc),
                           cc_type_pointer(a, vp), AST_CAST_NOP),
            loc);
        return cc_conversion_apply(a, cc_node_rvalue(a, x),
                                   AST_QUALIFIED_TYPE_UNQUALIFIED(to));
    }
    if (c->nullptr_ty &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty) ==
            c->nullptr_ty &&
        AST_TYPE_KIND(a, to) == AST_TYPE_POINTER &&
        !cxx_gen_is_reference(c, to)) {
        /* ... one that is no lvalue (std::forward<nullptr_t>(v)): evaluated, and a null pointer (its only value) */
        ast_qualified_type pt = AST_QUALIFIED_TYPE_UNQUALIFIED(to);
        uint32_t z = cc_cast_insert(
            a, cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_LONG), loc), pt,
            AST_CAST_INTEGER_TO_POINTER);

        return cc_node_new_operands(a, AST_NODE_COMMA, pt, loc, x, z);
    }
    if (cxx_gen_is_reference(c, to)) {
        ast_qualified_type rt = AST_TYPE(a, to)->base;

        if (AST_NODE_AT(a, x)->op == AST_NODE_ASSIGN &&
            !cc_type_is_record(a, rt) && AST_NODE_AT(a, x)->a &&
            (AST_NODE_AT(a, AST_NODE_AT(a, x)->a)->flags &
             AST_NODE_FLAG_LVALUE) &&
            AST_QUALIFIED_TYPE_UNQUALIFIED(
                AST_NODE_AT(a, AST_NODE_AT(a, x)->a)->ty) ==
                AST_QUALIFIED_TYPE_UNQUALIFIED(rt)) {
            /* l = r (or l op= r) bound to a reference: C++'s assignment is l itself, an lvalue (return l = l | r):
			   (p = &l, *p op= r, p), l evaluated once */
            const node *an = AST_NODE_AT(a, x);
            ast_qualified_type pt =
                cc_type_pointer(a, AST_NODE_AT(a, an->a)->ty);
            uint32_t p = cxx_gen_temporary(c, pt, loc), set, asg;

            if (!p)
                return 0;
            set = cc_assignment_build(a, 0, cxx_gen_var_node(c, p, loc),
                                      cc_address_of_build(a, an->a, loc), loc);
            asg = cc_assignment_build(
                a, an->aux,
                cc_dereference_build(
                    a, cc_node_rvalue(a, cxx_gen_var_node(c, p, loc)), loc),
                an->b, loc);
            x = cc_node_new_operands(
                a, AST_NODE_COMMA, pt, loc, set,
                cc_node_new_operands(
                    a, AST_NODE_COMMA, pt, loc, asg,
                    cc_node_rvalue(a, cxx_gen_var_node(c, p, loc))));
            return cc_cast_insert(a, x, AST_QUALIFIED_TYPE_UNQUALIFIED(to),
                                  AST_CAST_NOP);
        }

        bool by_ctor = false;
    again:
        if (cc_type_is_record(a, rt) &&
            AST_TYPE_KIND(a, rt) != AST_TYPE_ARRAY &&
            (by_ctor || !cc_type_is_record(a, AST_NODE_AT(a, x)->ty) ||
             AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == AST_TYPE_ARRAY)) {
            /* a reference to a class bound to a value of another type ("text" for a const std::string &): a
			   temporary made by the class's converting constructor */
            ast_qualified_type at[1] = {AST_NODE_AT(a, x)->ty};
            uint8_t lv[1] = {
                (AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) != 0 &&
                !cxx_gen_prv_find(c, x)};
            uint32_t saved = c->enode, tmp;

            if (AST_TYPE(a, to)->ext == 1 &&
                !(AST_QUALIFIED_TYPE_QUALIFIERS(rt) & AST_QUALIFIER_CONST))
                return 0; /* (T& does not bind a temporary) */
            if (AST_TYPE_KIND(a, at[0]) ==
                AST_TYPE_ARRAY) /* ("text": a pointer, as for a call) */
                at[0] = cc_type_pointer(a, AST_TYPE(a, at[0])->base), lv[0] = 0;
            if (cxx_gen_is_nullptr_node(
                    c, x)) { /* (nullptr: a std::nullptr_t, not a void *) */
                at[0] = c->nullptr_ty, lv[0] = 0;
                if (!(x = cxx_gen_nullptr_object(c, x, loc)))
                    return 0;
            }
            c->skip_explicit = 2;
            cxx_ctor_use(c, AST_QUALIFIED_TYPE_UNQUALIFIED(rt), at, lv, 1,
                         false);
            c->skip_explicit = 0;
            if (!c->ctor_found || !c->ctor_picked ||
                c->ctor_picked->fn_explicit == 1)
                return 0;
            c->enode = 0;
            cxx_gen_construct_temp(c, AST_QUALIFIED_TYPE_UNQUALIFIED(rt), &x, 1,
                                   loc);
            tmp = c->enode;
            c->enode = saved;
            if (!tmp || !(AST_NODE_AT(a, tmp)->flags & AST_NODE_FLAG_LVALUE))
                return 0;
            return cc_cast_insert(a, cc_address_of_build(a, tmp, loc),
                                  AST_QUALIFIED_TYPE_UNQUALIFIED(to),
                                  AST_CAST_NOP);
        }
        if ((AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) &&
            cc_type_is_record(a, rt) &&
            cc_type_is_record(a, AST_NODE_AT(a, x)->ty) &&
            AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty) !=
                AST_QUALIFIED_TYPE_UNQUALIFIED(rt)) {
            /* a reference to a base class bound to a derived object: its base subobject; to another class:
			   the object its conversion function to that class gives (operator Result()) */
            ast_qualified_type from =
                AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty);
            uint64_t off;

            uint32_t vk;

            if (cxx_class_base_offset(
                    c, from, AST_QUALIFIED_TYPE_UNQUALIFIED(rt), &off, 0) ||
                cxx_class_vbase_path(
                    c, from, AST_QUALIFIED_TYPE_UNQUALIFIED(rt), &vk, &off))
                x = cxx_gen_base_object(
                    c, x, AST_QUALIFIED_TYPE_UNQUALIFIED(rt), loc);
            else {
                uint32_t y = cxx_gen_convert_class(
                    c, x, AST_QUALIFIED_TYPE_UNQUALIFIED(rt), loc);

                if (!y && !by_ctor) {
                    /* (no conversion function: the class's converting constructor, a temporary;
					   unique_ptr<T, D>(p, alloc) with D(Alloc&)) */
                    by_ctor = true;
                    goto again;
                }
                x = y;
            }
            if (!x)
                return 0;
        }
        if (!(AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) ||
            (cxx_gen_type_ok(c, rt) && !cc_type_is_record(a, rt) &&
             AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty) !=
                 AST_QUALIFIED_TYPE_UNQUALIFIED(rt))) {
            /* a value (or an lvalue of another type, converted): a temporary */
            uint32_t tmp, tv, asg;

            if (AST_TYPE(a, to)->ext == 1 &&
                !(AST_QUALIFIED_TYPE_QUALIFIERS(rt) & AST_QUALIFIER_CONST))
                return 0; /* (T& does not bind a value) */
            if (!cxx_gen_object_type_ok(c, rt) ||
                AST_TYPE_KIND(a, rt) == AST_TYPE_ARRAY)
                return 0;
            if (cc_type_is_record(a, rt) !=
                    cc_type_is_record(a, AST_NODE_AT(a, x)->ty) ||
                (cc_type_is_record(a, rt) &&
                 AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty) !=
                     AST_QUALIFIED_TYPE_UNQUALIFIED(rt)))
                return 0; /* (a class from another type: a converting constructor or conversion function, not built yet) */
            tmp = cxx_gen_temporary(c, rt, loc);
            if (!tmp)
                return 0;
            tv = cc_node_new(a, AST_NODE_VARIABLE, a->syms[tmp].ty, loc);
            AST_NODE_AT(a, tv)->a = tmp;
            AST_NODE_AT(a, tv)->flags = AST_NODE_FLAG_LVALUE;
            asg = cc_assignment_build(
                a, 0, tv,
                cc_type_is_record(a, rt)
                    ? x
                    : cc_conversion_apply(a, cc_conversion_decay(a, x),
                                          AST_QUALIFIED_TYPE_UNQUALIFIED(rt)),
                loc);
            tv = cc_node_new(a, AST_NODE_VARIABLE, a->syms[tmp].ty, loc);
            AST_NODE_AT(a, tv)->a = tmp;
            AST_NODE_AT(a, tv)->flags = AST_NODE_FLAG_LVALUE;
            x = cc_node_new_operands(a, AST_NODE_COMMA,
                                     cc_type_pointer(a, a->syms[tmp].ty), loc,
                                     asg, cc_address_of_build(a, tv, loc));
        } else
            x = cc_address_of_build(a, x, loc);
        return cc_cast_insert(a, x, AST_QUALIFIED_TYPE_UNQUALIFIED(to),
                              AST_CAST_NOP);
    }
    if (cc_type_is_record(a, to)) {
        if (AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty) ==
            AST_QUALIFIED_TYPE_UNQUALIFIED(to))
            return cc_node_rvalue(a, x);
        if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) &&
            AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != AST_TYPE_ARRAY) {
            /* another class's object: its conversion function to this class (strong_ordering to partial_ordering) */
            uint32_t v = cxx_gen_convert_class(
                c, x, AST_QUALIFIED_TYPE_UNQUALIFIED(to), loc);

            if (v && AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, v)->ty) ==
                         AST_QUALIFIED_TYPE_UNQUALIFIED(to))
                return cc_node_rvalue(a, v);
            /* (else: this class's converting constructor from it, below; __map_iterator(__tree_iterator)) */
        }
        {
            /* a value of another type: the class's converting constructor (0 for _CmpUnspecifiedParam) */
            ast_qualified_type at[1] = {AST_NODE_AT(a, x)->ty};
            uint8_t lv[1] = {
                (AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE) != 0 &&
                !cxx_gen_prv_find(c, x)};
            uint32_t saved = c->enode, tmp;

            if (AST_TYPE_KIND(a, at[0]) == AST_TYPE_ARRAY)
                at[0] = cc_type_pointer(a, AST_TYPE(a, at[0])->base), lv[0] = 0;
            if (cxx_gen_is_nullptr_node(
                    c, x)) { /* (nullptr: a std::nullptr_t, not a void *) */
                at[0] = c->nullptr_ty, lv[0] = 0;
                if (!(x = cxx_gen_nullptr_object(c, x, loc)))
                    return 0;
            }
            c->skip_explicit = 2;
            cxx_ctor_use(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to), at, lv, 1,
                         false);
            c->skip_explicit = 0;
            if (!c->ctor_found || !c->ctor_picked ||
                c->ctor_picked->fn_explicit == 1)
                return 0;
            c->enode = 0;
            cxx_gen_construct_temp(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to), &x, 1,
                                   loc);
            tmp = c->enode;
            c->enode = saved;
            return tmp ? cc_node_rvalue(a, tmp) : 0;
        }
    }
    if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) &&
        AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != AST_TYPE_ARRAY) {
        x = cxx_gen_convert_class(
            c, x, to,
            loc); /* a class to a scalar: its conversion function to that type */
        if (!x)
            return 0;
    }
    x = cc_conversion_decay(a, x);
    if (AST_TYPE_KIND(a, to) == AST_TYPE_POINTER &&
        AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == AST_TYPE_POINTER) {
        /* Derived* to Base*: the pointer moved to the base subobject */
        ast_qualified_type tb = AST_TYPE(a, to)->base,
                           fb = AST_TYPE(a, AST_NODE_AT(a, x)->ty)->base;

        if (cc_type_is_record(a, tb) && cc_type_is_record(a, fb) &&
            AST_QUALIFIED_TYPE_UNQUALIFIED(tb) !=
                AST_QUALIFIED_TYPE_UNQUALIFIED(fb)) {
            x = cxx_gen_base_pointer(c, x, tb, loc);
            return x ? cc_cast_insert(a, x, AST_QUALIFIED_TYPE_UNQUALIFIED(to),
                                      AST_CAST_NOP)
                     : 0;
        }
    }
    return cc_conversion_apply(a, x, AST_QUALIFIED_TYPE_UNQUALIFIED(to));
}

/* a reference as an expression: the lvalue it refers to */
VPRIV uint32_t cxx_gen_referent(cxx *c, uint32_t x, uint32_t loc)
{
    cc *a = c->ast;
    ast_qualified_type t = AST_NODE_AT(a, x)->ty;

    x = cc_node_rvalue(a, x);
    x = cc_cast_insert(a, x, cc_type_pointer(a, AST_TYPE(a, t)->base),
                       AST_CAST_NOP); /* (a plain pointer to the referent) */
    return cc_dereference_build(a, x, loc);
}

/*
 * A pointer to an object of class type as a pointer to its base class subobject (to: the base class, with
 * the cv of the pointee): the pointer moved by the base's offset.  0: not a base of it.
 */
VPRIV uint32_t cxx_gen_base_pointer(cxx *c, uint32_t p, ast_qualified_type to,
                                    uint32_t loc)
{
    cc *a = c->ast;
    ast_qualified_type pt = AST_NODE_AT(a, p)->ty, from;
    uint64_t off;

    if (AST_TYPE_KIND(a, pt) != AST_TYPE_POINTER)
        return 0;
    from = AST_TYPE(a, pt)->base;
    if (AST_QUALIFIED_TYPE_UNQUALIFIED(from) ==
        AST_QUALIFIED_TYPE_UNQUALIFIED(to))
        return cc_cast_insert(a, p, cc_type_pointer(a, to), AST_CAST_NOP);
    if (!cxx_class_base_offset(c, AST_QUALIFIED_TYPE_UNQUALIFIED(from),
                               AST_QUALIFIED_TYPE_UNQUALIFIED(to), &off, 0)) {
        /* through a virtual base: its offset from the object's vtable (p + ((long *)*(char **)p)[-3 - k]), then
		   the base's in it */
        uint32_t k, tmp, tv, asg, vptr, slot;
        ast_qualified_type cp =
                               cc_type_pointer(a, cc_type_basic(AST_TYPE_CHAR)),
                           lp =
                               cc_type_pointer(a, cc_type_basic(AST_TYPE_LONG));

        if (!cxx_class_vbase_path(c, AST_QUALIFIED_TYPE_UNQUALIFIED(from),
                                  AST_QUALIFIED_TYPE_UNQUALIFIED(to), &k,
                                  &off) ||
            !(tmp = cxx_gen_temporary(c, cp, loc)))
            return 0;
        tv = cxx_gen_var_node(c, tmp, loc);
        asg = cc_assignment_build(a, 0, tv,
                                  cc_cast_insert(a, p, cp, AST_CAST_NOP), loc);
        vptr = cc_dereference_build(
            a,
            cc_cast_insert(a, cxx_gen_var_node(c, tmp, loc),
                           cc_type_pointer(a, cp), AST_CAST_NOP),
            loc);
        slot = cc_binary_build(
            a, LEX_TOKEN_PLUS,
            cc_cast_insert(a, cc_node_rvalue(a, vptr), lp, AST_CAST_NOP),
            cc_node_integer_new(a, -3 - (i128)k, cc_type_basic(AST_TYPE_LONG),
                                loc),
            loc);
        p = cc_binary_build(
            a, LEX_TOKEN_PLUS, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)),
            cc_node_rvalue(a, cc_dereference_build(a, slot, loc)), loc);
        if (off)
            p = cc_binary_build(
                a, LEX_TOKEN_PLUS, p,
                cc_node_integer_new(a, (i128)off, cc_type_basic(AST_TYPE_LONG),
                                    loc),
                loc);
        p = cc_node_new_operands(a, AST_NODE_COMMA, cp, loc, asg, p);
        return cc_cast_insert(a, p, cc_type_pointer(a, to), AST_CAST_NOP);
    }
    if (off) {
        p = cc_cast_insert(a, p,
                           cc_type_pointer(a, cc_type_basic(AST_TYPE_CHAR)),
                           AST_CAST_NOP);
        p = cc_binary_build(a, LEX_TOKEN_PLUS, p,
                            cc_node_integer_new(a, (i128)off,
                                                cc_type_basic(AST_TYPE_LONG),
                                                loc),
                            loc);
    }
    return cc_cast_insert(a, p, cc_type_pointer(a, to), AST_CAST_NOP);
}

/* an lvalue of class type as its base class subobject (0: not a base of it) */
VPRIV uint32_t cxx_gen_base_object(cxx *c, uint32_t x, ast_qualified_type to,
                                   uint32_t loc)
{
    cc *a = c->ast;
    ast_qualified_type xt = AST_NODE_AT(a, x)->ty;
    uint32_t p;

    if (!(AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE))
        return 0;
    p = cxx_gen_base_pointer(
        c, cc_address_of_build(a, x, loc),
        AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(to),
                           AST_QUALIFIED_TYPE_QUALIFIERS(to) |
                               AST_QUALIFIED_TYPE_QUALIFIERS(xt)),
        loc);
    return p ? cc_dereference_build(a, p, loc) : 0;
}

/* the member `name` of the object x: a field of its class, or of a base class (the subobject first) */
VPRIV uint32_t cxx_gen_member_of(cxx *c, uint32_t x, uint32_t name,
                                 uint32_t loc, int depth)
{
    cc *a = c->ast;
    ast_qualified_type bt = AST_NODE_AT(a, x)->ty, bases[16];
    uint64_t bits;
    uint32_t nb;

    if (!cc_type_is_record(a, bt) ||
        !(a->tags[AST_TYPE(a, bt)->aux].flags & AST_RECORD_FLAG_COMPLETE) ||
        depth > 8)
        return 0;
    if (cxx_class_field_offset(c, bt, name, &bits, 0))
        return member(a, x, name, loc);
    nb = cxx_class_bases(c, bt, bases, 16);
    for (uint32_t i = 0; i < nb; i++) {
        uint32_t sub = cxx_gen_base_object(c, x, bases[i], loc), m;

        if (sub && (m = cxx_gen_member_of(c, sub, name, loc, depth + 1)))
            return m;
    }
    nb = cxx_class_vbases(c, bt, bases, NULL, 16);
    for (uint32_t i = 0; i < nb && i < 16; i++) {
        uint32_t sub, m;

        if (!cxx_class_field_offset(c, bases[i], name, &bits, 0))
            continue; /* (a virtual base's own bases are virtual bases of this class too) */
        if ((sub = cxx_gen_base_object(c, x, bases[i], loc)) &&
            (m = member(a, sub, name, loc)))
            return m;
    }
    return 0;
}
