/* C++ code generation: destructors' bodies, the destructor a class has in effect, destroying objects */
#include "codegen.h"

/*
 * A destructor with no body of its own gets one: one defaulted in its class (~T() = default), or the
 * implicit one of a class that declares none though its members and bases need destruction (made here: an
 * entity in the class like a declared one).  Its body is empty; cxx_gen_function_end adds the members'
 * and bases' destruction.  NULL: not built.
 */
VPRIV cxx_entity *cxx_gen_destructor_body(cxx *c, ast_qualified_type t,
                                          cxx_entity *d)
{
    cc *a = c->ast;
    cxx_scope *cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t)),
              *fs;
    uint32_t body;

    if (!cls || !cls->owner || cls->dependent || !a->o->emit || c->tentative ||
        c->unevaluated)
        return NULL;
    if (!d) {
        d = cxx_declare(c, cls, cxx_dtor_id(), CXX_ENTITY_VALUE);
        if (!d)
            return NULL;
        d->ty =
            cxx_type_function(c, cc_type_basic(AST_TYPE_VOID), NULL, 0, false);
        d->member_of = cls;
        d->fn_defaulted = 1, d->fn_implicit = 1, d->fn_inline = 1;
        cxx_vtable_declare(
            c, cls, d, false); /* (it overrides a base's virtual destructor) */
    }
    if (d->gen_tried || d->fn_deleted)
        return d->sym && (a->syms[d->sym].flags & AST_SYMBOL_FLAG_DEFINED) ? d
               : d->gen_tried == 2 ? d
                                   : NULL;
    d->gen_tried =
        2; /* (being built: a member of its own type further in asks again) */
    if (!d->fn_ooc_default)
        d->fn_inline =
            1; /* (X::~X() = default; outside the class: not inline, the TU's own definition) */
    fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, cls, NULL);
    if (!cxx_gen_function_begin(c, d, fs, NULL, 0)) {
        d->gen_tried = 1;
        return NULL;
    }
    body = cc_statement_new(a, AST_NODE_BLOCK, 0, 0, 0, 0);
    cxx_gen_function_end(c, d, body);
    d->gen_tried = 1;
    return d->sym && (a->syms[d->sym].flags & AST_SYMBOL_FLAG_DEFINED) ? d
                                                                       : NULL;
}

/*
 * The destructor that destroying an object of the class runs: the one it declares; with none declared,
 * the one of its only base that needs destruction, when that base is at offset 0 and no member needs
 * destruction (the implicit destructor does just that).  NULL: none, or an implicit one not built yet.
 */
VPRIV cxx_entity *cxx_gen_effective_destructor(cxx *c, ast_qualified_type t,
                                               int depth)
{
    cc *a = c->ast;
    cxx_entity *d = cxx_gen_declared_destructor(c, t);
    cxx_scope *cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
    ast_qualified_type bases[16], only = CXX_TY_UNKNOWN;
    uint32_t nb;
    uint64_t off;
    bool members = false;

    if (d && d->fn_defaulted && !d->fn_deleted &&
        !(d->sym && (a->syms[d->sym].flags & AST_SYMBOL_FLAG_DEFINED)))
        return cxx_gen_destructor_body(
            c, t,
            d); /* ~T() = default: its body is made where it is first used */
    if (d || !cls || depth > 8)
        return d;
    if (!cxx_gen_needs_destruction(c, t, 0))
        return NULL;
    for (uint32_t i = 0; i < cls->nfield_types; i++) {
        ast_qualified_type ft = cls->field_types[i];

        while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == AST_TYPE_ARRAY)
            ft = AST_TYPE(a, ft)->base;
        if (ft == CXX_TY_UNKNOWN)
            return NULL;
        members |=
            cc_type_is_record(a, ft) && cxx_gen_needs_destruction(c, ft, 0);
    }
    nb = cxx_class_bases(c, t, bases, 16);
    for (uint32_t i = 0; i < nb && !members; i++)
        if (cxx_gen_needs_destruction(c, bases[i], 0)) {
            if (only != CXX_TY_UNKNOWN)
                members = true;
            only = bases[i];
        }
    if (!members && only != CXX_TY_UNKNOWN &&
        cxx_class_base_offset(c, t, only, &off, 0) && !off &&
        !cxx_class_polymorphic(c, t))
        return cxx_gen_effective_destructor(
            c, only, depth + 1); /* (the base's does all there is to do) */
    return cxx_gen_destructor_body(c, t,
                                   NULL); /* the implicit destructor, made */
}

/* ~T(): the destructor's call on the lvalue x (the class declares it); the statement goes to `out` */
VPRIV bool cxx_gen_destroy(cxx *c, uint32_t x, ast_qualified_type t,
                           cc_id_list *out, uint32_t loc)
{
    cc *a = c->ast;
    cxx_entity *d = cxx_gen_effective_destructor(c, t, 0);

    if (!x)
        return false;
    if (!d)
        return !cxx_gen_needs_destruction(
            c, t,
            0); /* (an implicit destructor that does something: not built yet) */
    if (d->fn_deleted)
        return false;
    cxx_gen_call(c, d, x, false, NULL, 0, loc);
    if (!c->enode)
        return false;
    cc_id_list_append(
        a, out, cc_statement_new(a, AST_NODE_EXPRESSION, loc, c->enode, 0, 0));
    c->enode = 0;
    return true;
}

/* a destructor's body is done: then its members of class type, last first, and its bases (into `after`) */
VPRIV bool cxx_gen_destructor_rest(cxx *c, cc_id_list *after, uint32_t loc)
{
    cc *a = c->ast;
    uint32_t self = cxx_gen_this_object(c, loc);
    ast_qualified_type own, bases[16];
    uint32_t nb;
    const tag *r;

    if (!self)
        return false;
    own = AST_NODE_AT(a, self)->ty;
    r = &a->tags[AST_TYPE(a, own)->aux];
    for (uint32_t i = r->nfield; i-- > 0;) {
        ast_qualified_type ft = r->f[i].ty, et = ft;

        while (AST_TYPE_KIND(a, et) == AST_TYPE_ARRAY)
            et = AST_TYPE(a, et)->base;
        if (!cc_type_is_record(a, et) || !cxx_gen_needs_destruction(c, et, 0))
            continue;
        if (r->f[i].name && et != ft && AST_TYPE(a, ft)->base == et &&
            AST_TYPE(a, ft)->n && AST_TYPE(a, ft)->n <= 256) {
            /* an array of them (std::array<pair<char, string>, 12>): each element, the last first */
            for (uint64_t j = AST_TYPE(a, ft)->n; j-- > 0;) {
                uint32_t el = cc_dereference_build(
                    a,
                    cc_binary_build(
                        a, LEX_TOKEN_PLUS,
                        cc_conversion_decay(
                            a, member(a, cxx_gen_this_object(c, loc),
                                      r->f[i].name, loc)),
                        cc_node_integer_new(a, (i128)j,
                                            cc_type_basic(AST_TYPE_LONG), loc),
                        loc),
                    loc);

                if (!cxx_gen_destroy(c, el, AST_QUALIFIED_TYPE_UNQUALIFIED(et),
                                     after, loc))
                    return false;
            }
            continue;
        }
        if (!r->f[i].name || et != ft)
            return false; /* (anonymous members, arrays of arrays of classes with destructors: not built yet) */
        if (!cxx_gen_destroy(
                c, member(a, cxx_gen_this_object(c, loc), r->f[i].name, loc),
                AST_QUALIFIED_TYPE_UNQUALIFIED(ft), after, loc))
            return false;
    }
    nb = cxx_class_bases(c, own, bases, 16);
    for (uint32_t i = nb; i-- > 0;) {
        if (!cxx_gen_needs_destruction(c, bases[i], 0))
            continue;
        c->gen_base_type = AST_QUALIFIED_TYPE_UNQUALIFIED(bases[i]);
        if (!cxx_gen_destroy(c,
                             cxx_gen_base_object(c, cxx_gen_this_object(c, loc),
                                                 bases[i], loc),
                             bases[i], after, loc)) {
            c->gen_base_type = 0;
            return false;
        }
        c->gen_base_type = 0;
    }
    return true;
}

/* x.~T(), p->~T(): the destructor's call (a class with none to run, a scalar: nothing), as an expression */
VPRIV void cxx_gen_destroy_object(cxx *c, uint32_t x, bool arrow, uint32_t loc)
{
    cc *a = c->ast;
    ast_qualified_type t;
    cxx_entity *d;

    c->enode = 0;
    if (!cxx_gen_on(c) || !x)
        return;
    if (arrow) {
        x = cc_node_rvalue(a, x);
        if (AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != AST_TYPE_POINTER)
            return;
        x = cc_dereference_build(a, x, loc);
    }
    t = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty);
    if (!cc_type_is_record(a, t) || !cxx_gen_needs_destruction(c, t, 0)) {
        /* nothing destroyed, the object's expression evaluated all the same ((--p)->~T() in valarray's
           __clear: dropping it looped forever) */
        c->enode =
            cc_cast_insert(a,
                           (AST_NODE_AT(a, x)->flags & AST_NODE_FLAG_LVALUE)
                               ? cc_address_of_build(a, x, loc)
                               : x,
                           cc_type_basic(AST_TYPE_VOID), AST_CAST_VOID);
        return;
    }
    if (!(d = cxx_gen_effective_destructor(c, t, 0)))
        return;
    cxx_member_body_use(
        c,
        d); /* (called: an instance's body is instantiated, as at the end of a scope) */
    if (d->fn_virtual && d->vslot &&
        !(d->member_of && d->member_of->is_final)) {
        /* p->~T() of a virtual destructor: the complete-object one (D1) through the vtable, tmp = &x;
		   ((void (**)(T *))*(void ***)tmp)[slot](tmp) */
        uint32_t s1 = cxx_gen_name(c, d) ? d->sym1 : 0, tmp, pre, vptr, fn,
                 call;
        ast_qualified_type pt = cc_type_pointer(a, t), ft, pf, ppf, pppf;
        cc_id_list args;

        if (!s1 || !(tmp = cxx_gen_temporary(c, pt, loc)))
            return;
        ft = a->syms[s1].ty, pf = cc_type_pointer(a, ft),
        ppf = cc_type_pointer(a, pf), pppf = cc_type_pointer(a, ppf);
        pre = cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc),
                                  cc_address_of_build(a, x, loc), loc);
        vptr = cc_node_rvalue(
            a, cc_dereference_build(
                   a,
                   cc_cast_insert(
                       a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)),
                       pppf, AST_CAST_NOP),
                   loc));
        fn = cc_node_rvalue(
            a, cc_dereference_build(
                   a,
                   cc_binary_build(
                       a, LEX_TOKEN_PLUS, vptr,
                       cc_node_integer_new(a, d->vslot - 1,
                                           cc_type_basic(AST_TYPE_LONG), loc),
                       loc),
                   loc));
        cc_id_list_init(&args);
        cc_id_list_append(
            a, &args,
            cc_cast_insert(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)),
                           a->params[AST_TYPE(a, ft)->aux], AST_CAST_NOP));
        call = cc_node_new(a, AST_NODE_CALL, cc_type_basic(AST_TYPE_VOID), loc);
        AST_NODE_AT(a, call)->a = fn;
        AST_NODE_AT(a, call)->b = cc_id_list_freeze(a, &args);
        AST_NODE_AT(a, call)->c = 1;
        c->enode = cc_node_new_operands(
            a, AST_NODE_COMMA, cc_type_basic(AST_TYPE_VOID), loc, pre, call);
        return;
    }
    if (cxx_class_polymorphic(c, t) && !d->fn_virtual)
        return; /* (a non-virtual destructor of a class with virtual functions: the static type's, as written) */
    cxx_gen_call(c, d, x, false, NULL, 0, loc);
}

/* ---- virtual functions ----------------------------------------------------------------------- */
