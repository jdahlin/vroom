/* C++ code generation: which classes need construction or destruction; constructing an object (construct_at), locals */
#include "codegen.h"

/* does the class declare a constructor, or do its bases or members of class type need one called? */
VPRIV bool cxx_gen_needs_construction(cxx *c, ast_qualified_type t, int depth)
{
    cc *a = c->ast;
    cxx_scope *cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
    ast_qualified_type bases[16];
    uint32_t nb;
    bool pod, empty;

    if (cxx_class_layout_info(c, t, &pod, &empty) && pod)
        return false;
    if (!cls || depth > 8 || cxx_class_polymorphic(c, t) ||
        cxx_class_has_member_initializer(c, t))
        return true; /* (a vptr to set, members' default initializers) */
    for (cxx_entity *e = cxx_lookup_in(c, cls, cxx_ctor_id(), 0); e;
         e = e->next)
        if (e->member_of == cls)
            return true;
    nb = cxx_class_bases(c, t, bases, 16);
    for (uint32_t i = 0; i < nb; i++)
        if (cxx_gen_needs_construction(c, bases[i], depth + 1))
            return true;
    for (uint32_t i = 0; i < cls->nfield_types; i++) {
        ast_qualified_type ft = cls->field_types[i];

        while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == AST_TYPE_ARRAY)
            ft = AST_TYPE(a, ft)->base;
        if (ft != CXX_TY_UNKNOWN && cc_type_is_record(a, ft) &&
            cxx_gen_needs_construction(c, ft, depth + 1))
            return true;
    }
    return false;
}

/* a destructor about to be parsed: the destructors its end runs (its members' and bases' classes) have
   their bodies parsed first, not from inside its code generation */
VPRIV void cxx_gen_destructor_parts_use(cxx *c, cxx_entity *d)
{
    cxx_scope *cls = d ? d->member_of : NULL;
    ast_qualified_type bases[16];
    uint32_t nb;

    if (!cls || !cls->owner || cls->owner->ty == CXX_TY_UNKNOWN)
        return;
    for (uint32_t i = 0; i < cls->nfield_types; i++) {
        ast_qualified_type ft = cls->field_types[i];

        while (ft != CXX_TY_UNKNOWN &&
               AST_TYPE_KIND(c->ast, ft) == AST_TYPE_ARRAY)
            ft = AST_TYPE(c->ast, ft)->base;
        if (ft != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, ft)) {
            cxx_entity *md = cxx_gen_declared_destructor(c, ft);

            if (md && md != d)
                cxx_member_body_use(c, md);
        }
    }
    nb = cxx_class_bases(c, cls->owner->ty, bases, 16);
    for (uint32_t i = 0; i < nb; i++) {
        cxx_entity *bd = cxx_gen_declared_destructor(c, bases[i]);

        if (bd && bd != d)
            cxx_member_body_use(c, bd);
    }
}

/* X::~X() = default; outside its class: its body made now, from the class's members and bases */
VPRIV void cxx_gen_defaulted_destructor(cxx *c, cxx_entity *d)
{
    if (d && d->member_of && d->member_of->owner &&
        d->member_of->owner->ty != CXX_TY_UNKNOWN) {
        d->fn_ooc_default = 1, d->fn_inline = 0;
        cxx_gen_destructor_body(c, d->member_of->owner->ty, d);
    }
}

/*
 * X::X() = default;, X::X(const X&) = default; or X::X(X&&) = default; outside its class: the function, built
 * here as clang does (not inline: other TUs call it).  A copy or move does the class's bases, vptr and members
 * from the parameter's (cxx_gen_copy_construct_parts; a move moves its class members), nothing else.
 */
VPRIV void cxx_gen_defaulted_constructor(cxx *c, cxx_entity *d)
{
    cc *a = c->ast;
    ast_qualified_type t, pt;
    cxx_scope *fs;
    cxx_param p[1];
    uint32_t loc, src, body;
    cc_id_list l;
    bool ok, move;

    if (!d || !d->member_of || !d->member_of->owner || d->fn_deleted ||
        (t = d->member_of->owner->ty) == CXX_TY_UNKNOWN ||
        d->ty == CXX_TY_UNKNOWN ||
        AST_TYPE_KIND(a, d->ty) != AST_TYPE_FUNCTION ||
        AST_TYPE(a, d->ty)->n > 1 || !a->o->emit || c->tentative ||
        c->unevaluated || d->member_of->dependent)
        return;
    d->fn_ooc_default = 1, d->fn_inline = 0;
    if (!AST_TYPE(a, d->ty)->n) {
        cxx_gen_constructor_body(c, t, d);
        return;
    }
    pt = a->params[AST_TYPE(a, d->ty)->aux];
    if (!cxx_gen_is_reference(c, pt) || d->gen_tried)
        return;
    move = !(AST_QUALIFIED_TYPE_QUALIFIERS(AST_TYPE(a, pt)->base) &
             AST_QUALIFIER_CONST) &&
           AST_TYPE(a, pt)->ext >= 2;
    p[0].name = 0, p[0].ty = pt;
    d->gen_tried = 2;
    fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, d->member_of, NULL);
    if (!cxx_gen_function_begin(c, d, fs, p, 1)) {
        d->gen_tried = 1;
        return;
    }
    loc = a->syms[d->sym].loc;
    src = cc_dereference_build(
        a,
        cc_node_rvalue(
            a, cxx_gen_var_node(c, a->lists[c->gen_psyms + c->gen_npsyms - 1],
                                loc)),
        loc);
    cc_id_list_init(&l);
    c->gen_parts_move = move;
    ok = src && cxx_gen_copy_construct_parts(c, cxx_gen_this_object(c, loc), t,
                                             src, &l, loc, 0);
    c->gen_parts_move = 0;
    c->gen_delegates =
        1; /* (the bases and members: done above, not default-constructed after) */
    if (!ok)
        c->gen_failed = 1;
    body = cc_statement_new(a, AST_NODE_BLOCK, loc, 0, cc_id_list_freeze(a, &l),
                            l.n);
    cxx_gen_function_end(c, d, body);
    d->gen_tried = 1;
}

/* the destructor the class declares (NULL: none) */
VPRIV cxx_entity *cxx_gen_declared_destructor(cxx *c, ast_qualified_type t)
{
    cxx_scope *cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
    cxx_entity *d = cls ? cxx_lookup_in(c, cls, cxx_dtor_id(), 0) : NULL;

    return d && d->member_of == cls && d->kind == CXX_ENTITY_VALUE ? d : NULL;
}

/* does destroying an object of the class do anything: a declared destructor, here or in a base or member? */
VPRIV bool cxx_gen_needs_destruction(cxx *c, ast_qualified_type t, int depth)
{
    cc *a = c->ast;
    cxx_scope *cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
    ast_qualified_type bases[16];
    uint32_t nb;
    bool pod, empty;

    if (cxx_class_layout_info(c, t, &pod, &empty) && pod)
        return false;
    if (!cls || depth > 8)
        return true;
    {
        cxx_entity *d = cxx_gen_declared_destructor(c, t);

        /* ~T() = default; in the class, not virtual: trivial when its bases' and members' are (json's iter_impl) */
        if (d && !(d->fn_defaulted && !d->fn_ooc_default && !d->fn_virtual &&
                   !d->fn_deleted))
            return true;
    }
    nb = cxx_class_bases(c, t, bases, 16);
    for (uint32_t i = 0; i < nb; i++)
        if (cxx_gen_needs_destruction(c, bases[i], depth + 1))
            return true;
    for (uint32_t i = 0; i < cls->nfield_types; i++) {
        ast_qualified_type ft = cls->field_types[i];

        while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == AST_TYPE_ARRAY)
            ft = AST_TYPE(a, ft)->base;
        if (ft != CXX_TY_UNKNOWN && cc_type_is_record(a, ft) &&
            cxx_gen_needs_destruction(c, ft, depth + 1))
            return true;
    }
    return false;
}

/*
 * An object of class type t at the lvalue `target` is initialized from args: the call of the constructor
 * cxx_ctor_use chose just before, or a copy from an object of the class where none is declared for that,
 * or nothing for a class that needs no construction.  The statements go to `out`.  False: not built yet.
 */
VPRIV bool cxx_gen_default_construct(cxx *c, uint32_t target,
                                     ast_qualified_type t, cc_id_list *out,
                                     uint32_t loc, int depth);
VPRIV bool cxx_gen_default_construct_parts(cxx *c, uint32_t target,
                                           ast_qualified_type t,
                                           cc_id_list *out, uint32_t loc,
                                           int depth);
/*
 * T(const T&) or T(T&&) that is implicit (or defaulted) and not trivial, done in place: the bases from the
 * source's base subobjects, the vptr, then each member (a class one by its constructor, the others copied).
 * src: the source object (an lvalue).
 */
VPRIV bool cxx_gen_copy_construct_parts(cxx *c, uint32_t target,
                                        ast_qualified_type t, uint32_t src,
                                        cc_id_list *out, uint32_t loc,
                                        int depth)
{
    cc *a = c->ast;
    cxx_scope *cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
    ast_qualified_type bases[16];
    uint32_t nb;
    const tag *r;

    if (!target || !src || !cls || depth > 8 ||
        AST_TYPE_KIND(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t)) == AST_TYPE_UNION ||
        0)
        return false;
    nb = cxx_class_bases(c, t, bases, 16);
    for (uint32_t i = 0; i < nb; i++) {
        uint32_t tb = cxx_gen_base_object(c, target, bases[i], loc),
                 sb = cxx_gen_base_object(c, src, bases[i], loc);
        ast_qualified_type at[1] = {AST_QUALIFIED_TYPE(
            AST_QUALIFIED_TYPE_INDEX(AST_QUALIFIED_TYPE_UNQUALIFIED(bases[i])),
            AST_QUALIFIER_CONST)};
        uint8_t lv[1] = {1};
        bool ok;

        if (c->gen_parts_move) /* (T(T&&) = default: the base moved) */
            at[0] = AST_QUALIFIED_TYPE_UNQUALIFIED(bases[i]), lv[0] = 0;

        if (!tb || !sb)
            return false;
        cxx_ctor_use(c, AST_QUALIFIED_TYPE_UNQUALIFIED(bases[i]), at, lv, 1,
                     false);
        c->gen_base_type = AST_QUALIFIED_TYPE_UNQUALIFIED(
            bases[i]); /* (a base subobject: C2) */
        ok = cxx_gen_construct_at(
            c, tb, AST_QUALIFIED_TYPE_UNQUALIFIED(bases[i]), &sb, 1, out, loc);
        c->gen_base_type = 0;
        if (!ok)
            return false;
    }
    if (cxx_class_polymorphic(c, t)) {
        uint32_t st = cxx_gen_vptr_store_at(
            c, cls, cc_address_of_build(a, target, loc), loc);

        if (!st)
            return false;
        cc_id_list_append(a, out, st);
    }
    r = &a->tags[AST_TYPE(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t))->aux];
    for (uint32_t i = 0; i < r->nfield; i++) {
        ast_qualified_type ft = r->f[i].ty, et = ft;
        uint32_t tm, sm;

        if (!r->f[i].name)
            continue; /* (the bases' bytes, done above) */
        while (AST_TYPE_KIND(a, et) == AST_TYPE_ARRAY)
            et = AST_TYPE(a, et)->base;
        tm = member(a, target, r->f[i].name, loc),
        sm = member(a, src, r->f[i].name, loc);
        if (!tm || !sm)
            return false;
        if (cc_type_is_record(a, et) && cxx_gen_trivial_abi(c, et, 0) != 1) {
            ast_qualified_type at[1] = {AST_QUALIFIED_TYPE(
                AST_QUALIFIED_TYPE_INDEX(AST_QUALIFIED_TYPE_UNQUALIFIED(ft)),
                AST_QUALIFIER_CONST)};
            uint8_t lv[1] = {1};

            if (et != ft && AST_TYPE(a, ft)->base == et && AST_TYPE(a, ft)->n &&
                AST_TYPE(a, ft)->n <= 256) {
                /* an array of such a class (std::array<pair<char, string>, 12>): each element copied */
                ast_qualified_type eat[1] = {
                    AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(
                                           AST_QUALIFIED_TYPE_UNQUALIFIED(et)),
                                       AST_QUALIFIER_CONST)};

                for (uint64_t j = 0; j < AST_TYPE(a, ft)->n; j++) {
                    uint32_t te = cc_dereference_build(
                        a,
                        cc_binary_build(
                            a, LEX_TOKEN_PLUS,
                            cc_conversion_decay(
                                a, member(a, target, r->f[i].name, loc)),
                            cc_node_integer_new(
                                a, (i128)j, cc_type_basic(AST_TYPE_LONG), loc),
                            loc),
                        loc);
                    uint32_t se = cc_dereference_build(
                        a,
                        cc_binary_build(
                            a, LEX_TOKEN_PLUS,
                            cc_conversion_decay(
                                a, member(a, src, r->f[i].name, loc)),
                            cc_node_integer_new(
                                a, (i128)j, cc_type_basic(AST_TYPE_LONG), loc),
                            loc),
                        loc);
                    uint8_t elv[1] = {1};

                    cxx_ctor_use(c, AST_QUALIFIED_TYPE_UNQUALIFIED(et), eat,
                                 elv, 1, false);
                    if (!cxx_gen_construct_at(
                            c, te, AST_QUALIFIED_TYPE_UNQUALIFIED(et), &se, 1,
                            out, loc))
                        return false;
                }
                continue;
            }
            if (et != ft)
                return false; /* (an array of arrays of such a class: not built yet) */
            if (c->gen_parts_move) /* (T(T&&) = default: the member moved) */
                at[0] = AST_QUALIFIED_TYPE_UNQUALIFIED(ft), lv[0] = 0;
            cxx_ctor_use(c, AST_QUALIFIED_TYPE_UNQUALIFIED(ft), at, lv, 1,
                         false);
            if (c->ctor_picked && c->ctor_picked->fn_deleted) {
                lv[0] = 0, at[0] = AST_QUALIFIED_TYPE_UNQUALIFIED(
                               ft); /* (a move-only member: moved) */
                cxx_ctor_use(c, AST_QUALIFIED_TYPE_UNQUALIFIED(ft), at, lv, 1,
                             false);
            }
            if (!cxx_gen_construct_at(c, tm, AST_QUALIFIED_TYPE_UNQUALIFIED(ft),
                                      &sm, 1, out, loc))
                return false;
        } else {
            if (cxx_gen_is_reference(c, ft))
                return false;
            if (AST_QUALIFIED_TYPE_QUALIFIERS(AST_NODE_AT(a, tm)->ty) &
                (AST_QUALIFIER_CONST | AST_QUALIFIER_VOLATILE)) {
                /* (a const member: initialized, through its address as the unqualified type) */
                ast_qualified_type pt = cc_type_pointer(
                    a, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, tm)->ty));
                uint32_t addr = cc_address_of_build(a, tm, loc);

                tm = cc_dereference_build(
                    a,
                    cc_cast_insert(a, addr, pt,
                                   cc_conversion_cast_kind(
                                       a, AST_NODE_AT(a, addr)->ty, pt, addr)),
                    loc);
            }
            if (AST_TYPE_KIND(a, ft) == AST_TYPE_ARRAY) {
                /* (an array of scalars: its bytes, as a struct of the same size would be) */
                uint32_t zargs[3], saved = c->enode;

                zargs[0] = cc_address_of_build(a, tm, loc),
                zargs[1] = cc_address_of_build(a, sm, loc);
                zargs[2] = cc_node_integer_new(
                    a, (i128)AST_TYPE(a, ft)->size,
                    cc_type_basic(AST_TYPE_UNSIGNED_LONG), loc);
                c->enode = 0;
                cxx_gen_builtin(c, "__builtin_memcpy", 16, zargs, 3, loc);
                if (!c->enode) {
                    c->enode = saved;
                    return false;
                }
                cc_id_list_append(a, out,
                                  cc_statement_new(a, AST_NODE_EXPRESSION, loc,
                                                   c->enode, 0, 0));
                c->enode = saved;
                continue;
            }
            cc_id_list_append(
                a, out,
                cc_statement_new(
                    a, AST_NODE_EXPRESSION, loc,
                    cc_assignment_build(a, 0, tm, cc_node_rvalue(a, sm), loc),
                    0, 0));
        }
    }
    return true;
}

/* target = src of class type t (move: src an xvalue), appended to out: its operator= when it declares one,
   else the implicit one's parts (cxx_gen_assign_parts), else (trivially copyable) its bytes */
static bool cxx_gen_assign_object(cxx *c, uint32_t target, ast_qualified_type t,
                                  uint32_t src, bool move, cc_id_list *out,
                                  uint32_t loc, int depth)
{
    cc *a = c->ast;
    cxx_scope *cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
    cxx_entity *own = NULL, *set;
    bool pod, empty;

    if (!cls)
        return false;
    if (cxx_class_layout_info(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t), &pod,
                              &empty) &&
        empty && cxx_gen_trivial_abi(c, t, 0) == 1)
        return true; /* (an empty class: nothing to assign; its byte may be another member's, an empty base's
                        or [[no_unique_address]]) */
    set = cxx_lookup_in(c, cls, vintern_cstr("operator="), 0);
    for (cxx_entity *e = set; e && !own; e = e->next)
        if (e->member_of == cls || e->kind == CXX_ENTITY_FUNCTION_TEMPLATE)
            own = set;
    if (own) {
        ast_qualified_type at =
            move ? AST_QUALIFIED_TYPE_UNQUALIFIED(t)
                 : AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(
                                          AST_QUALIFIED_TYPE_UNQUALIFIED(t)),
                                      AST_QUALIFIER_CONST);
        uint8_t lv = !move;
        ast_qualified_type et = c->etype;
        bool el = c->elval;

        c->call_obj = 1;
        cxx_call_type(c, own, CXX_TY_UNKNOWN, &at, &lv, 1);
        c->etype = et, c->elval = el;
        if (!c->call_resolved || !c->call_picked ||
            c->call_picked->kind != CXX_ENTITY_VALUE ||
            c->call_picked->fn_deleted)
            return false;
        cxx_member_body_use(c, c->call_picked);
        c->enode = 0;
        cxx_gen_call(c, c->call_picked, target, false, &src, 1, loc);
        if (!c->enode)
            return false;
        cc_id_list_append(
            a, out,
            cc_statement_new(a, AST_NODE_EXPRESSION, loc, c->enode, 0, 0));
        c->enode = 0;
        return true;
    }
    if (cxx_gen_trivial_abi(c, t, 0) == 1) {
        cc_id_list_append(
            a, out,
            cc_statement_new(
                a, AST_NODE_EXPRESSION, loc,
                cc_assignment_build(a, 0, target, cc_node_rvalue(a, src), loc),
                0, 0));
        return true;
    }
    return cxx_gen_assign_parts(c, target, t, src, move, out, loc, depth + 1);
}

/* T& operator=(const T&) = default, T& operator=(T&&) = default: each base, then each member assigned from
   src's, in order ([class.copy.assign]/12; move: each moved) */
VPRIV bool cxx_gen_assign_parts(cxx *c, uint32_t target, ast_qualified_type t,
                                uint32_t src, bool move, cc_id_list *out,
                                uint32_t loc, int depth)
{
    cc *a = c->ast;
    cxx_scope *cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
    ast_qualified_type bases[16];
    uint32_t nb;
    const tag *r;

    if (!target || !src || !cls || depth > 8 ||
        AST_TYPE_KIND(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t)) == AST_TYPE_UNION ||
        cxx_class_vbases(c, t, NULL, NULL, 0))
        return false; /* (a virtual base: assigned once, not done) */
    nb = cxx_class_bases(c, t, bases, 16);
    for (uint32_t i = 0; i < nb; i++) {
        uint32_t tb = cxx_gen_base_object(c, target, bases[i], loc),
                 sb = cxx_gen_base_object(c, src, bases[i], loc);

        if (!tb || !sb ||
            !cxx_gen_assign_object(c, tb,
                                   AST_QUALIFIED_TYPE_UNQUALIFIED(bases[i]), sb,
                                   move, out, loc, depth))
            return false;
    }
    r = &a->tags[AST_TYPE(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t))->aux];
    for (uint32_t i = 0; i < r->nfield; i++) {
        ast_qualified_type ft = r->f[i].ty;
        uint32_t tm, sm;

        if (!r->f[i].name)
            continue; /* (the bases' bytes, done above) */
        if (cxx_gen_is_reference(c, ft) ||
            (AST_QUALIFIED_TYPE_QUALIFIERS(ft) & AST_QUALIFIER_CONST))
            return false; /* (a reference or const member: the operator is deleted) */
        tm = member(a, target, r->f[i].name, loc),
        sm = member(a, src, r->f[i].name, loc);
        if (!tm || !sm)
            return false;
        if (AST_TYPE_KIND(a, ft) == AST_TYPE_ARRAY) {
            ast_qualified_type et = ft;
            uint32_t zargs[3], saved = c->enode;

            while (AST_TYPE_KIND(a, et) == AST_TYPE_ARRAY)
                et = AST_TYPE(a, et)->base;
            if (cc_type_is_record(a, et) && cxx_gen_trivial_abi(c, et, 0) != 1)
                return false; /* (an array of such a class: not built yet) */
            zargs[0] = cc_address_of_build(a, tm, loc),
            zargs[1] = cc_address_of_build(a, sm, loc);
            zargs[2] =
                cc_node_integer_new(a, (i128)AST_TYPE(a, ft)->size,
                                    cc_type_basic(AST_TYPE_UNSIGNED_LONG), loc);
            c->enode = 0;
            cxx_gen_builtin(c, "__builtin_memmove", 17, zargs, 3, loc);
            if (!c->enode) {
                c->enode = saved;
                return false;
            }
            cc_id_list_append(
                a, out,
                cc_statement_new(a, AST_NODE_EXPRESSION, loc, c->enode, 0, 0));
            c->enode = saved;
            continue;
        }
        if (cc_type_is_record(a, ft)) {
            if (!cxx_gen_assign_object(c, tm,
                                       AST_QUALIFIED_TYPE_UNQUALIFIED(ft), sm,
                                       move, out, loc, depth))
                return false;
            continue;
        }
        cc_id_list_append(
            a, out,
            cc_statement_new(
                a, AST_NODE_EXPRESSION, loc,
                cc_assignment_build(a, 0, tm, cc_node_rvalue(a, sm), loc), 0,
                0));
    }
    return true;
}

/* target = src through t's implicit copy or move assignment (no operator= declared): src converted to t first
   when of another type (alt_string = "text": a temporary by t's converting constructor); the parts assigned
   (cxx_gen_assign_parts), the bytes for a trivially copyable class; target, an lvalue (0: not built) */
VPRIV uint32_t cxx_gen_implicit_assign(cxx *c, uint32_t target,
                                       ast_qualified_type t, uint32_t src,
                                       uint32_t loc)
{
    cc *a = c->ast;
    cc_id_list parts;
    uint32_t blk, se;
    bool move;

    t = AST_QUALIFIED_TYPE_UNQUALIFIED(t);
    if (!target || !src || !cxx_gen_on(c))
        return 0;
    if (AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, src)->ty) != t) {
        uint32_t p = cxx_gen_class_argument(c, src, t, loc);

        if (!p)
            return 0;
        src = cc_dereference_build(a, p, loc), move = true;
    } else
        move = !(AST_NODE_AT(a, src)->flags & AST_NODE_FLAG_LVALUE) ||
               cxx_gen_prv_find(c, src);
    {
        bool pod, empty;

        if (cxx_class_layout_info(c, t, &pod, &empty) && empty &&
            cxx_gen_trivial_abi(c, t, 0) == 1)
            /* (an empty class: nothing assigned, src evaluated; its byte may be another member's,
               [[no_unique_address]] __unique_ptr_array_bounds_stateless) */
            return cc_dereference_build(
                a,
                cc_node_new_operands(
                    a, AST_NODE_COMMA,
                    cc_type_pointer(a, AST_NODE_AT(a, target)->ty), loc,
                    cc_cast_insert(a, cc_node_rvalue(a, src),
                                   cc_type_basic(AST_TYPE_VOID), AST_CAST_VOID),
                    cc_address_of_build(a, target, loc)),
                loc);
    }
    if (cxx_gen_trivial_abi(c, t, 0) == 1)
        return cc_assignment_build(
            a, 0, target, cc_node_rvalue(a, src),
            loc); /* (its bytes, from any value: ({ ...; s; })) */
    if (!(AST_NODE_AT(a, src)->flags & AST_NODE_FLAG_LVALUE))
        return 0;
    cc_id_list_init(&parts);
    if (!cxx_gen_assign_parts(c, target, t, src, move, &parts, loc, 0))
        return 0;
    cc_id_list_append(a, &parts,
                      cc_statement_new(a, AST_NODE_EXPRESSION, loc,
                                       cc_address_of_build(a, target, loc), 0,
                                       0));
    blk = cc_statement_new(a, AST_NODE_BLOCK, loc, 0,
                           cc_id_list_freeze(a, &parts), parts.n);
    se = cc_node_new(a, AST_NODE_STATEMENT_EXPRESSION,
                     cc_type_pointer(a, AST_NODE_AT(a, target)->ty), loc);
    AST_NODE_AT(a, se)->a = blk;
    return cc_dereference_build(a, se, loc);
}

VPRIV bool cxx_gen_construct_at(cxx *c, uint32_t target, ast_qualified_type t,
                                const uint32_t *args, uint32_t nargs,
                                cc_id_list *out, uint32_t loc)
{
    cc *a = c->ast;
    cxx_entity *ctor = c->ctor_picked;
    bool found = c->ctor_found;
    cxx_scope *cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));

    bool value_init = c->gen_value_init;

    c->gen_value_init = 0;
    if (!target || !cls || nargs == UINT32_MAX || nargs > CXX_MAX_ARGS)
        return false;
    if (value_init && !nargs &&
        !(found && ctor && ctor->member_of == cls && !ctor->fn_defaulted)) {
        /* T{} or f({}) of a class without a user-provided default constructor: zeroed, then default-constructed
           ([dcl.init.general]/9.1.2) */
        uint32_t zargs[3], saved = c->enode, zero;

        zargs[0] = cc_address_of_build(a, target, loc);
        zargs[1] = cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_INT), loc);
        zargs[2] = cc_node_integer_new(
            a, (i128)AST_TYPE(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t))->size,
            cc_type_basic(AST_TYPE_UNSIGNED_LONG), loc);
        c->enode = 0;
        cxx_gen_builtin(c, "__builtin_memset", 16, zargs, 3, loc);
        zero = c->enode;
        c->enode = saved;
        if (!zero)
            return false;
        cc_id_list_append(
            a, out, cc_statement_new(a, AST_NODE_EXPRESSION, loc, zero, 0, 0));
        c->ctor_picked = ctor, c->ctor_found = found;
        if (!cxx_gen_needs_construction(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t),
                                        0))
            return true;
    }
    uint32_t sliced;
    if (nargs == 1 && args[0] &&
        (AST_NODE_AT(a, args[0])->flags & AST_NODE_FLAG_LVALUE) &&
        cc_type_is_record(a, AST_NODE_AT(a, args[0])->ty) &&
        AST_TYPE_KIND(a, AST_NODE_AT(a, args[0])->ty) != AST_TYPE_ARRAY &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, args[0])->ty) !=
            AST_QUALIFIED_TYPE_UNQUALIFIED(t)) {
        /* from an object of a class derived from this one (B(d) in D's copy constructor, json_base_class_t(other)):
		   its base subobject, copied or moved as this class's own */
        uint64_t off;

        if (cxx_class_base_offset(
                c, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, args[0])->ty),
                AST_QUALIFIED_TYPE_UNQUALIFIED(t), &off, 0)) {
            if (!(sliced = cxx_gen_base_object(
                      c, args[0], AST_QUALIFIED_TYPE_UNQUALIFIED(t), loc)))
                return false;
            args = &sliced;
        }
    }
    if (c->ctor_ilist && c->ctor_ilist->member_of == cls &&
        c->ctor_ilist->ty != CXX_TY_UNKNOWN) {
        /* T{a, b} with an initializer_list constructor: it, from the list (its other parameters defaulted) */
        cxx_entity *ilc = c->ctor_ilist;
        uint32_t il, arr = 0;
        ast_qualified_type et;

        c->ctor_ilist = NULL;
        il = cxx_gen_initializer_list(c, a->params[AST_TYPE(a, ilc->ty)->aux],
                                      args, nargs, out, loc, &arr);
        if (!il)
            return false;
        cxx_gen_call(c, ilc, target, false, &il, 1, loc);
        if (!c->enode)
            return false;
        cc_id_list_append(
            a, out,
            cc_statement_new(a, AST_NODE_EXPRESSION, loc, c->enode, 0, 0));
        c->enode = 0;
        /* the array's elements live to the end of the full-expression: here, the declaration's (not for an
		   exception thrown by the constructor) */
        et = arr ? AST_TYPE(a, a->syms[arr].ty)->base : CXX_TY_UNKNOWN;
        if (arr && cc_type_is_record(a, et) &&
            cxx_gen_needs_destruction(c, et, 0))
            for (uint32_t i = nargs; i-- > 0;) {
                uint32_t slot = cc_dereference_build(
                    a,
                    cc_binary_build(
                        a, LEX_TOKEN_PLUS,
                        cc_conversion_decay(a, cxx_gen_var_node(c, arr, loc)),
                        cc_node_integer_new(a, i, cc_type_basic(AST_TYPE_LONG),
                                            loc),
                        loc),
                    loc);

                if (!cxx_gen_destroy(c, slot, et, out, loc))
                    return false;
            }
        return true;
    }
    if (ctor && ctor->member_of != cls)
        ctor = NULL,
        found =
            false; /* (a base's constructor, found by name: not this class's) */
    uint32_t conv[1];

    if (nargs == 1 && args[0] &&
        cc_type_is_record(a, AST_NODE_AT(a, args[0])->ty) &&
        AST_TYPE_KIND(a, AST_NODE_AT(a, args[0])->ty) != AST_TYPE_ARRAY &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, args[0])->ty) !=
            AST_QUALIFIED_TYPE_UNQUALIFIED(t) &&
        (!ctor || /* (no constructor chosen: the source's conversion function to T, if any) */
         ctor->ty == CXX_TY_UNKNOWN || AST_TYPE(a, ctor->ty)->n < 1 ||
         (!cc_type_is_record(a, a->params[AST_TYPE(a, ctor->ty)->aux]) &&
          !(cxx_gen_is_reference(c, a->params[AST_TYPE(a, ctor->ty)->aux]) &&
            cc_type_is_record(
                a,
                AST_TYPE(a, a->params[AST_TYPE(a, ctor->ty)->aux])->base))) ||
         /* (a constructor template's instance for the source class itself, basic_string(const T&) with T
            json: constrained on is_convertible<json, string_view>, which a conversion template decides and
            vcc does not; the source's conversion function, clang's choice there) */
         (ctor->is_instance &&
          cxx_gen_is_reference(c, a->params[AST_TYPE(a, ctor->ty)->aux]) &&
          AST_QUALIFIED_TYPE_UNQUALIFIED(
              AST_TYPE(a, a->params[AST_TYPE(a, ctor->ty)->aux])->base) ==
              AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, args[0])->ty)))) {
        /* T x = y, y of another class, through a constructor of T taking a scalar: that would convert y
           to the scalar first, a second user-defined conversion ([over.best.ics]/4); y's own conversion to T
           when it has one (json's template operator ValueType() to std::string, not to char const *) */
        uint32_t v = cxx_gen_convert_class(
            c, args[0], AST_QUALIFIED_TYPE_UNQUALIFIED(t), loc);

        if (v && AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, v)->ty) ==
                     AST_QUALIFIED_TYPE_UNQUALIFIED(t)) {
            conv[0] = v, args = conv;
            ctor = NULL,
            found = false; /* (from the prvalue: constructed in place below) */
        }
    }
    if (ctor && nargs == 1 && args[0] &&
        cc_type_is_record(a, AST_NODE_AT(a, args[0])->ty) &&
        AST_TYPE_KIND(a, AST_NODE_AT(a, args[0])->ty) != AST_TYPE_ARRAY &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, args[0])->ty) !=
            AST_QUALIFIED_TYPE_UNQUALIFIED(t) &&
        ctor->ty != CXX_TY_UNKNOWN && AST_TYPE(a, ctor->ty)->n >= 1 &&
        cxx_gen_is_reference(c, a->params[AST_TYPE(a, ctor->ty)->aux]) &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(
            AST_TYPE(a, a->params[AST_TYPE(a, ctor->ty)->aux])->base) ==
            AST_QUALIFIED_TYPE_UNQUALIFIED(t)) {
        /* T(x) through T's copy or move constructor from an object of another class that converts to T: the
		   object is the conversion function's result itself ([dcl.init.general]/16.6.1, C++17); no copy */
        uint32_t v = cxx_gen_convert_class(
            c, args[0], AST_QUALIFIED_TYPE_UNQUALIFIED(t), loc);

        if (v && AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, v)->ty) ==
                     AST_QUALIFIED_TYPE_UNQUALIFIED(t))
            conv[0] = v, args = conv;
    }
    if (ctor && nargs == 1 && args[0] &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, args[0])->ty) ==
            AST_QUALIFIED_TYPE_UNQUALIFIED(t)) {
        /* from an object of the class: only a copy or move constructor takes it (not one found through a
		   conversion of the object to something else) */
        const ctype *cf =
            ctor->ty != CXX_TY_UNKNOWN &&
                    AST_TYPE_KIND(a, ctor->ty) == AST_TYPE_FUNCTION
                ? AST_TYPE(a, ctor->ty)
                : NULL;
        ast_qualified_type p0 =
            cf && cf->n ? a->params[cf->aux] : CXX_TY_UNKNOWN;

        if (p0 == CXX_TY_UNKNOWN ||
            !(AST_QUALIFIED_TYPE_UNQUALIFIED(p0) ==
                  AST_QUALIFIED_TYPE_UNQUALIFIED(t) ||
              (cxx_gen_is_reference(c, p0) &&
               AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(a, p0)->base) ==
                   AST_QUALIFIED_TYPE_UNQUALIFIED(t))))
            ctor = NULL, found = false;
    }
    if (nargs == 1 && args[0] &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, args[0])->ty) ==
            AST_QUALIFIED_TYPE_UNQUALIFIED(t)) {
        /* from a prvalue of the class: constructed here in the first place */
        cxx_prv *p = cxx_gen_prv_find(c, args[0]);

        if (p && p->call) {
            cc_id_list_append(
                a, out,
                cc_statement_new(
                    a, AST_NODE_EXPRESSION, loc,
                    cxx_gen_prv_aim(c, p, cc_address_of_build(a, target, loc)),
                    0, 0));
            return true;
        }
    }
    if (found && ctor && ctor->fn_defaulted && nargs == 1 && args[0] &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, args[0])->ty) ==
            AST_QUALIFIED_TYPE_UNQUALIFIED(t) &&
        cxx_gen_defaulted_copy_trivial(c, t)) {
        /* T(const T&) = default, T(T&&) = default of a trivially copyable class: its bytes, as clang does (it
		   calls no function, and defines none) */
        ctor = NULL, found = false;
        goto bitwise;
    }
    if (found && ctor && ctor->fn_defaulted && !ctor->fn_deleted &&
        !ctor->fn_ooc_default && /* (defined outside its class: called) */
        nargs == 1 && args[0] &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, args[0])->ty) ==
            AST_QUALIFIED_TYPE_UNQUALIFIED(t) &&
        (AST_NODE_AT(a, args[0])->flags & AST_NODE_FLAG_LVALUE) &&
        ctor->ty != CXX_TY_UNKNOWN && AST_TYPE(a, ctor->ty)->n == 1 &&
        cxx_gen_is_reference(c, a->params[AST_TYPE(a, ctor->ty)->aux]) &&
        AST_TYPE(a, a->params[AST_TYPE(a, ctor->ty)->aux])->ext >= 1) {
        /* T(const T&) = default (or T(T&&) = default: pair(pair&&) with a string in it) of a class that is not
		   trivially copyable (a polymorphic one: std::exception): what it does, in place (bases, the vptr,
		   members), no function called; a move's members copied (the source stays valid, as a moved-from
		   object must) */
        uint32_t src = args[0];

        if (AST_NODE_AT(a, target)->ty != AST_QUALIFIED_TYPE_UNQUALIFIED(t)) {
            ast_qualified_type pt =
                cc_type_pointer(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
            uint32_t addr = cc_address_of_build(a, target, loc);

            target = cc_dereference_build(
                a,
                cc_cast_insert(a, addr, pt,
                               cc_conversion_cast_kind(
                                   a, AST_NODE_AT(a, addr)->ty, pt, addr)),
                loc);
        }
        if (AST_NODE_AT(a, src)->ty != AST_QUALIFIED_TYPE_UNQUALIFIED(t)) {
            ast_qualified_type pt =
                cc_type_pointer(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
            uint32_t addr = cc_address_of_build(a, src, loc);

            src = cc_dereference_build(
                a,
                cc_cast_insert(a, addr, pt,
                               cc_conversion_cast_kind(
                                   a, AST_NODE_AT(a, addr)->ty, pt, addr)),
                loc);
        }
        return cxx_gen_copy_construct_parts(c, target, t, src, out, loc, 0);
    }
    if (found && ctor && ctor->fn_defaulted &&
        !nargs) { /* T() = default: what the implicit one does, in place */
        if (cxx_class_has_member_initializer(c, t))
            return cxx_gen_construct_by_body(c, target, t, ctor, out, loc);
        return cxx_gen_default_construct_parts(c, target, t, out, loc, 0);
    }
    if (found && ctor) {
        cxx_gen_call(c, ctor, target, false, args, nargs, loc);
        if (!c->enode)
            return false;
        cc_id_list_append(
            a, out,
            cc_statement_new(a, AST_NODE_EXPRESSION, loc, c->enode, 0, 0));
        c->enode = 0;
        return true;
    }
    if (nargs == 0)
        return cxx_gen_default_construct(
            c, target, t, out, loc,
            0); /* (the implicit default constructor, in place) */
bitwise:
    if (nargs == 1 && args[0] &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, args[0])->ty) ==
            AST_QUALIFIED_TYPE_UNQUALIFIED(t) &&
        (cxx_gen_type_ok(c, t) || cxx_gen_trivial_abi(c, t, 0) == 1)) {
        /* from an object of a class C copies as C++ does (a const object is initialized all the same) */
        bool pod, empty;

        if (cxx_class_layout_info(c, t, &pod, &empty) && empty) {
            /* an empty class: nothing to copy (its byte may be another member's, [[no_unique_address]]
			   __value_comp_ at __size_'s offset in std::__tree) */
            if (!(AST_NODE_AT(a, args[0])->flags & AST_NODE_FLAG_LVALUE))
                cc_id_list_append(a, out,
                                  cc_statement_new(a, AST_NODE_EXPRESSION, loc,
                                                   args[0], 0, 0));
            return true;
        }
        if (AST_NODE_AT(a, target)->ty !=
            AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, target)->ty)) {
            ast_qualified_type pt =
                cc_type_pointer(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
            uint32_t addr = cc_address_of_build(a, target, loc);

            target = cc_dereference_build(
                a,
                cc_cast_insert(a, addr, pt,
                               cc_conversion_cast_kind(
                                   a, AST_NODE_AT(a, addr)->ty, pt, addr)),
                loc);
        }
        cc_id_list_append(
            a, out,
            cc_statement_new(a, AST_NODE_EXPRESSION, loc,
                             cc_assignment_build(
                                 a, 0, target, cc_node_rvalue(a, args[0]), loc),
                             0, 0));
        return true;
    }
    if (!found &&
        !(nargs == 1 && args[0] &&
          AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, args[0])->ty) ==
              AST_QUALIFIED_TYPE_UNQUALIFIED(t)))
        return cxx_gen_aggregate_init(c, target, t, args, nargs, out, loc);
    if (nargs == 1 && args[0] &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, args[0])->ty) ==
            AST_QUALIFIED_TYPE_UNQUALIFIED(t) &&
        (AST_NODE_AT(a, args[0])->flags & AST_NODE_FLAG_LVALUE) &&
        (!found || (ctor && ctor->fn_defaulted))) {
        /* the implicit (or defaulted) copy or move constructor of a class with constructors: member by member */
        uint32_t src = args[0];

        if (AST_NODE_AT(a, src)->ty != AST_QUALIFIED_TYPE_UNQUALIFIED(t)) {
            ast_qualified_type pt =
                cc_type_pointer(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
            uint32_t addr = cc_address_of_build(a, src, loc);

            src = cc_dereference_build(
                a,
                cc_cast_insert(a, addr, pt,
                               cc_conversion_cast_kind(
                                   a, AST_NODE_AT(a, addr)->ty, pt, addr)),
                loc);
        }
        if (AST_NODE_AT(a, target)->ty != AST_QUALIFIED_TYPE_UNQUALIFIED(t)) {
            ast_qualified_type pt =
                cc_type_pointer(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
            uint32_t addr = cc_address_of_build(a, target, loc);

            target = cc_dereference_build(
                a,
                cc_cast_insert(a, addr, pt,
                               cc_conversion_cast_kind(
                                   a, AST_NODE_AT(a, addr)->ty, pt, addr)),
                loc);
        }
        return cxx_gen_copy_construct_parts(c, target, t, src, out, loc, 0);
    }
    return false; /* (an implicit copy of a class with constructors: not built yet) */
}

/* this->... : the object being constructed or destroyed, as an lvalue */
VPRIV uint32_t cxx_gen_this_object(cxx *c, uint32_t loc)
{
    uint32_t p;

    cxx_gen_this(c, loc);
    p = c->enode;
    c->enode = 0;
    return p ? cc_dereference_build(c->ast, p, loc) : 0;
}

/* a runtime function's symbol, declared once: int name(void *, ...) as the Itanium ABI's */
static uint32_t cxx_gen_abi_function(cxx *c, uint32_t *slot, const char *name,
                                     ast_qualified_type ret, uint32_t np)
{
    cc *a = c->ast;
    ast_qualified_type vp = cc_type_pointer(a, cc_type_basic(AST_TYPE_VOID)),
                       ps[3] = {vp, vp, vp};

    if (!*slot) {
        *slot = cc_symbol_new(a, AST_SYMBOL_FUNCTION, vintern_cstr(name),
                              cxx_type_function(c, ret, ps, np, false), 0);
        a->syms[*slot].flags |= AST_SYMBOL_FLAG_GLOBAL;
        a->syms[*slot].depth = 0;
    }
    return *slot;
}

/*
 * static T x(args); in a function: constructed the first time its declaration runs, once
 * ([stmt.dcl]/3), under the Itanium ABI's guard: if (guard == 0 && __cxa_guard_acquire(&guard))
 * { construct; __cxa_atexit(destructor, &x, &__dso_handle); __cxa_guard_release(&guard); }.  (A
 * constructor that throws does not call __cxa_guard_abort: not done; the guard's first byte is read
 * without acquire ordering, as x86-64's loads are.)
 */
static void cxx_gen_construct_static_local(cxx *c, uint32_t s,
                                           ast_qualified_type t,
                                           const uint32_t *args, uint32_t nargs,
                                           uint32_t loc)
{
    cc *a = c->ast;
    cc_id_list *block = (cc_id_list *)c->gen_block, in;
    cxx_entity *picked = c->ctor_picked, *il = c->ctor_ilist, *dtor = NULL;
    bool found = c->ctor_found, none = c->ctor_none;
    ast_qualified_type ll = cc_type_basic(AST_TYPE_LONG_LONG),
                       vp = cc_type_pointer(a, cc_type_basic(AST_TYPE_VOID));
    uint32_t g, ga, cond, ar[3];

    if (cxx_gen_needs_destruction(c, t, 0)) {
        dtor = cxx_gen_effective_destructor(c, t, 0);
        if (!dtor || dtor->fn_deleted || !cxx_gen_name(c, dtor) ||
            !dtor->sym1 || (cxx_member_body_use(c, dtor), 0)) {
            cxx_gen_fail(c);
            return;
        }
    }
    g = cxx_gen_local(c, vintern_cstr("__vcc_guard"), ll, loc);
    if (!g) {
        cxx_gen_fail(c);
        return;
    }
    a->syms[g].sc = AST_STORAGE_STATIC;
    cc_id_list_append(a, block,
                      cc_node_new_operands(a, AST_NODE_DECLARATION,
                                           cc_type_basic(AST_TYPE_VOID), loc, s,
                                           0));
    cc_id_list_append(a, block,
                      cc_node_new_operands(a, AST_NODE_DECLARATION,
                                           cc_type_basic(AST_TYPE_VOID), loc, g,
                                           0));
    cc_id_list_init(&in);
    c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none,
    c->ctor_ilist = il;
    if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, s, loc), t, args, nargs,
                              &in, loc)) {
        cxx_gen_fail(c);
        return;
    }
    ga = cxx_gen_to_void_pointer(
        c, cc_address_of_build(a, cxx_gen_var_node(c, g, loc), loc));
    if (dtor) {
        cxx_gen_abi_function(c, &c->gen_atexit_sym, "__cxa_atexit",
                             cc_type_basic(AST_TYPE_INT), 3);
        if (!c->gen_dso_sym)
            c->gen_dso_sym = cxx_gen_data_symbol(c, "__dso_handle", 0);
        ar[0] = cxx_gen_to_void_pointer(
            c, cxx_gen_function_node(c, cxx_gen_dtor_sym(dtor), loc));
        ar[1] = cxx_gen_to_void_pointer(
            c, cc_address_of_build(a, cxx_gen_var_node(c, s, loc), loc));
        ar[2] = cxx_gen_data_address(c, c->gen_dso_sym, 0);
        cc_id_list_append(
            a, &in,
            cc_statement_new(
                a, AST_NODE_EXPRESSION, loc,
                cxx_gen_runtime_call(c, c->gen_atexit_sym, ar, 3, loc), 0, 0));
    }
    ar[0] = ga;
    cc_id_list_append(a, &in,
                      cc_statement_new(a, AST_NODE_EXPRESSION, loc,
                                       cxx_gen_runtime_call(
                                           c,
                                           cxx_gen_abi_function(
                                               c, &c->gen_guard_release_sym,
                                               "__cxa_guard_release",
                                               cc_type_basic(AST_TYPE_VOID), 1),
                                           ar, 1, loc),
                                       0, 0));
    ar[0] = cxx_gen_to_void_pointer(
        c, cc_address_of_build(a, cxx_gen_var_node(c, g, loc), loc));
    cond = cc_binary_build(
        a, LEX_TOKEN_AMPERSAND_AMPERSAND,
        cc_binary_build(
            a, LEX_TOKEN_EQUAL_EQUAL,
            cc_node_rvalue(
                a, cc_dereference_build(
                       a,
                       cc_cast_insert(
                           a,
                           cc_address_of_build(a, cxx_gen_var_node(c, g, loc),
                                               loc),
                           cc_type_pointer(a, cc_type_basic(AST_TYPE_CHAR)),
                           AST_CAST_POINTER_TO_POINTER),
                       loc)),
            cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_INT), loc), loc),
        cc_binary_build(
            a, LEX_TOKEN_EXCLAMATION_EQUAL,
            cxx_gen_runtime_call(
                c,
                cxx_gen_abi_function(c, &c->gen_guard_acquire_sym,
                                     "__cxa_guard_acquire",
                                     cc_type_basic(AST_TYPE_INT), 1),
                ar, 1, loc),
            cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_INT), loc), loc),
        loc);
    (void)vp;
    cc_id_list_append(
        a, block,
        cc_statement_new(a, AST_NODE_IF, loc,
                         cc_condition_scalar(a, cond, "statement"),
                         cc_statement_new(a, AST_NODE_BLOCK, loc, 0,
                                          cc_id_list_freeze(a, &in), in.n),
                         0));
}

/*
 * A local object of class type: its declaration, its construction from args (cxx_ctor_use was called for
 * it), and its destructor at the end of its scope (the emitter's scope cleanup, as C's
 * __attribute__((cleanup))).
 */
VPRIV void cxx_gen_construct_local(cxx *c, uint32_t s, ast_qualified_type t,
                                   const uint32_t *args, uint32_t nargs,
                                   uint32_t loc)
{
    if (s && cxx_gen_on(c) && c->gen_block && nargs != UINT32_MAX &&
        c->ast->syms[s].sc == AST_STORAGE_STATIC) {
        cxx_gen_construct_static_local(c, s, t, args, nargs, loc);
        return;
    }
    cc *a = c->ast;
    cxx_entity *dtor;
    cc_id_list *block = (cc_id_list *)c->gen_block;
    /* (what cxx_ctor_use chose: naming the destructor below resolves nothing, but keep it apart) */
    cxx_entity *picked = c->ctor_picked, *il = c->ctor_ilist;
    bool found = c->ctor_found, none = c->ctor_none;
    ast_qualified_type il_types[CXX_MAX_ARGS];
    uint8_t il_lval[CXX_MAX_ARGS];
    uint32_t il_n = c->ctor_ilist_n, il_brace[CXX_MAX_ARGS];

    memcpy(il_types, c->ctor_ilist_types, sizeof il_types),
        memcpy(il_lval, c->ctor_ilist_lval, sizeof il_lval);
    memcpy(il_brace, c->ctor_ilist_brace, sizeof il_brace);

    if (!cxx_gen_on(c) || !s)
        return;
    if (!block || nargs == UINT32_MAX) {
        cxx_gen_fail(c);
        return;
    }
    dtor = cxx_gen_effective_destructor(c, t, 0);
    if (dtor) {
        attrs at;

        if (dtor->fn_deleted || !cxx_gen_name(c, dtor) || !dtor->sym1 ||
            (cxx_member_body_use(c, dtor), 0)) {
            cxx_gen_fail(c);
            return;
        }
        memset(&at, 0, sizeof at);
        at.cleanup = cxx_gen_dtor_sym(dtor);
        cxx_gen_eh_ready(
            c); /* (with exceptions: run when one unwinds the scope too) */
        cc_attribute_apply_symbol(a, &a->syms[s], &at);
    } else if (cxx_gen_needs_destruction(c, t, 0)) {
        cxx_gen_fail(
            c); /* (an implicit destructor that destroys members: not built yet) */
        return;
    }
    /* (with a destructor the declaration follows the construction: its cleanup is live from there on, so an
	   exception the constructor throws does not destroy the object it did not make; the emitter gives the
	   variable its slot at its first use) */
    uint32_t decl = cc_node_new_operands(
        a, AST_NODE_DECLARATION, cc_type_basic(AST_TYPE_VOID), loc, s, 0);

    if (!dtor)
        cc_id_list_append(a, block, decl);
    c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none,
    c->ctor_ilist = il, c->ctor_ilist_n = il_n;
    memcpy(c->ctor_ilist_types, il_types, sizeof il_types),
        memcpy(c->ctor_ilist_lval, il_lval, sizeof il_lval);
    memcpy(c->ctor_ilist_brace, il_brace, sizeof il_brace);
    if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, s, loc), t, args, nargs,
                              block, loc))
        cxx_gen_fail(c);
    if (dtor)
        cc_id_list_append(a, block, decl);
}

/* ---- new and delete ------------------------------------------------------------------------- */

/* a call of one of the builtin table's library functions (0: not built) */
VPRIV uint32_t cxx_gen_library_call(cxx *c, const char *name,
                                    const uint32_t *args, uint32_t nargs,
                                    uint32_t loc)
{
    uint32_t saved = c->enode, n;

    c->enode = 0;
    cxx_gen_builtin(c, name, strlen(name), args, nargs, loc);
    n = c->enode;
    c->enode = saved;
    return n;
}
