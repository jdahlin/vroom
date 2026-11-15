/* C++ templates: template argument deduction, constraints */
#include "instantiate.h"

/* P against the base classes of the class A (cv as A's): true if one matches (bind then holds it) */
VPRIV bool cxx_match_bases(cxx *c, cxx_partial *p, ast_qualified_type P,
                           ast_qualified_type A, cxx_targ *bind, int depth)
{
    cxx_scope *cls =
        depth < 8 ? cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(A))
                  : NULL;
    cxx_targ save[64];

    if (!cls || p->params->ntparams > 64)
        return false;
    memcpy(save, bind, p->params->ntparams * sizeof *bind);
    for (uint32_t i = 0; i < cls->nused; i++) {
        cxx_scope *b = cls->used[i];
        ast_qualified_type bt;

        if (!b || b->kind != CXX_SCOPE_CLASS || !b->owner ||
            b->owner->ty == CXX_TY_UNKNOWN ||
            !cc_type_is_record(c->ast, b->owner->ty))
            continue;
        bt = AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(b->owner->ty),
                                AST_QUALIFIED_TYPE_QUALIFIERS(A));
        if (cxx_match_type(c, p, P, bt, bind, 0) ||
            cxx_match_bases(c, p, P, bt, bind, depth + 1))
            return true;
        memcpy(bind, save,
               p->params->ntparams *
                   sizeof *bind); /* (a partial match binds nothing) */
    }
    return false;
}

/*
 * After deduction: the parameters left take their default arguments, and each value parameter's type
 * is formed with the earlier parameters bound (template<class T, enable_if_t<cond<T>, int> = 0>).
 * 1 all bound, 0 a substitution failure (typename X::type with no such member: not viable), -1 not
 * known.
 */
VPRIV int cxx_ftemplate_complete(cxx *c, cxx_ftemplate *f, cxx_targ *bind)
{
    cxx_partial *p = &f->p;

    for (uint32_t i = 0; i < p->params->ntparams; i++) {
        const cxx_tparam *tp = &p->params->tparams[i];
        bool check = tp->kind == 2 && tp->type_b &&
                     !tp->name; /* (an unnamed value: there for its type) */
        cxx_scope *binding;
        cxx_outer o;
        jmp_buf jb;
        volatile bool failed = false;

        if (!bind[i].kind && !tp->def_b) {
            if (getenv("VCC_CXX_SIG"))
                fprintf(stderr,
                        "complete: parameter %u of %u: not bound, no default\n",
                        i, p->params->ntparams);
            return -1;
        }
        if (bind[i].kind && !check)
            continue;
        binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
        for (uint32_t j = 0; j < i; j++)
            cxx_bind(c, binding, &p->params->tparams[j], &bind[j]);
        if (check) {
            cxx_outer_save(c, &o);
            c->instantiating++;
            c->fail = &jb;
            c->pos = tp->type_b;
            c->scope = binding;
            c->template_depth = 0, c->in_template_args = 0,
            c->explicit_specialization = 0, c->class_depth = 0,
            c->implicit_typename = 0;
            c->member_missing = 0;
            volatile ast_qualified_type got = CXX_TY_UNKNOWN;

            if (!setjmp(jb)) {
                cxx_type_specifiers_parse(c);
                got = c->ds_type;
            } else
                failed = true;
            c->instantiating--;
            cxx_outer_restore(c, &o);
            if (c->member_missing)
                return 0;
            if ((failed || got == CXX_TY_UNKNOWN) && getenv("VCC_CXX_SIG")) {
                fprintf(stderr,
                        "complete: parameter %u: its type is not known (failed "
                        "%d):",
                        i, failed);
                for (uint32_t k = tp->type_b;
                     k < tp->type_b + 14 && k < c->ntok; k++)
                    fprintf(stderr, " %.*s", (int)c->tok[k].len, c->tok[k].s);
                fputc('\n', stderr);
            }
            if (failed || got == CXX_TY_UNKNOWN)
                return -1; /* (enable_if_t<cond, int> with a cond vcc cannot evaluate: not known, not fine) */
        }
        if (!bind[i].kind) {
            c->member_missing = 0;
            if (!cxx_default_argument(c, tp, binding, &bind[i])) {
                if (!c->member_missing && getenv("VCC_CXX_SIG"))
                    fprintf(
                        stderr,
                        "complete: parameter %u: its default is not known\n",
                        i);
                return c->member_missing ? 0 : -1;
            }
            if (c->member_missing)
                return 0;
        }
    }
    return 1;
}

/* a constraint parsed with the parameters bound: fn at pos gives 1, 0 or -1 */
VPRIV int cxx_constraint_parse(cxx *c, cxx_partial *p, const cxx_targ *bind,
                               uint32_t pos, const cxx_targ *first)
{
    cxx_scope *binding = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
    cxx_outer o;
    jmp_buf jb;
    volatile int v = -1;

    if (c->body_depth >= 64)
        return -1;
    for (uint32_t j = 0; j < p->params->ntparams; j++)
        if (bind[j].kind == 6)
            cxx_bind_pack(c, binding, &p->params->tparams[j],
                          bind[j].list + bind[j].t, (uint32_t)bind[j].v);
        else if (bind[j].kind)
            cxx_bind(c, binding, &p->params->tparams[j], &bind[j]);
    cxx_outer_save(c, &o);
    c->instantiating++, c->body_depth++;
    c->fail = &jb;
    c->pos = pos;
    c->scope = binding;
    c->template_depth = 0, c->in_template_args = 0,
    c->explicit_specialization = 0, c->class_depth = 0,
    c->implicit_typename = 0;
    if (!setjmp(jb)) {
        if (first) {
            /* template<Concept<args> T>: Concept<T, args> */
            cxx_entity *ce = cxx_constraint_name_parse(c);
            cxx_targ a[32];

            if (ce && c->ntargs < 31) {
                a[0] = *first;
                memcpy(a + 1, c->targs, c->ntargs * sizeof *a);
                v = cxx_concept_value(c, ce, a, c->ntargs + 1);
            }
        } else
            v = cxx_requires_clause_value(c);
    }
    c->instantiating--, c->body_depth--;
    cxx_outer_restore(c, &o);
    return v;
}

/* the template's constraints with its parameters bound: constrained parameters, the header's requires-clause */
VPRIV int cxx_ftemplate_constraints(cxx *c, cxx_ftemplate *f,
                                    const cxx_targ *bind)
{
    cxx_partial *p = &f->p;

    for (uint32_t i = 0; i < p->params->ntparams; i++) {
        const cxx_tparam *tp = &p->params->tparams[i];

        if (tp->constraint_b && bind[i].kind == 1) {
            int v =
                cxx_constraint_parse(c, p, bind, tp->constraint_b, &bind[i]);

            if (v <= 0)
                return v;
        }
    }
    return f->req_pos ? cxx_constraint_parse(c, p, bind, f->req_pos, NULL) : 1;
}

/* is the class a an instance of the template t, or derived from one? */
VPRIV bool cxx_instance_of(cxx *c, ast_qualified_type a, const cxx_template *t,
                           int depth)
{
    cxx_instance *ia = cxx_tag_info(c, a);
    cxx_scope *cls;

    if (ia && ia->t == t)
        return true;
    cls = depth < 8 ? cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(a))
                    : NULL;
    if (!cls || cls->has_dependent_base)
        return cls != NULL; /* (bases not known: it may be) */
    for (uint32_t i = 0; i < cls->nused; i++) {
        cxx_scope *b = cls->used[i];

        if (b && b->kind == CXX_SCOPE_CLASS && b->owner &&
            b->owner->ty != CXX_TY_UNKNOWN &&
            cc_type_is_record(c->ast, b->owner->ty) &&
            cxx_instance_of(c, b->owner->ty, t, depth + 1))
            return true;
    }
    return false;
}

/* can no argument of type A match the pattern P, whatever its parameters are?  X<...> against what is not
   an X (or derived from one), a pointer or array pattern against what is not one */
VPRIV bool cxx_shape_mismatch(cxx *c, ast_qualified_type P,
                              ast_qualified_type A)
{
    const ctype *pt, *at;
    void *owner;

    if (P == CXX_TY_UNKNOWN || A == CXX_TY_UNKNOWN ||
        !cxx_mentions_placeholder(c, P, 0))
        return false;
    pt = AST_TYPE(c->ast, P);
    if (pt->kind == AST_TYPE_POINTER && pt->ext)
        P = pt->base, pt = AST_TYPE(c->ast, P);
    if (cxx_placeholder_of(c, P, &owner) >= 0)
        return false; /* a bare T: anything */
    at = AST_TYPE(c->ast, A);
    if (pt->kind == AST_TYPE_STRUCT || pt->kind == AST_TYPE_UNION) {
        cxx_instance *ip = cxx_tag_info(c, P);

        if (!ip)
            return false;
        if (at->kind != AST_TYPE_STRUCT && at->kind != AST_TYPE_UNION)
            return true;
        if (!cxx_instance_of(c, A, ip->t, 0))
            return true;
        /* X<P1, ...> against an X<A1, ...> itself: an argument whose own shape cannot match
		   (identity_tag<tuple<Ts...>> against identity_tag<int>) */
        {
            cxx_instance *ia = cxx_tag_info(c, A);

            if (ia && ia->t == ip->t)
                for (uint32_t i = 0; i < ip->nargs && i < ia->nargs; i++) {
                    if (ip->args[i].kind != 1 || ia->args[i].kind != 1)
                        break; /* (a pack's expansion, a value: not compared) */
                    if (cxx_shape_mismatch(c, ip->args[i].t, ia->args[i].t))
                        return true;
                }
        }
        return false;
    }
    if (pt->kind == AST_TYPE_POINTER)
        return !((at->kind == AST_TYPE_POINTER && !at->ext) ||
                 at->kind == AST_TYPE_ARRAY || at->kind == AST_TYPE_FUNCTION);
    if (pt->kind == AST_TYPE_ARRAY)
        return at->kind != AST_TYPE_ARRAY;
    return false;
}

/* one argument (type A, an lvalue or not) against a parameter's pattern P: 1 deduced or nothing to deduce,
   0 no match, -1 not known */
VPRIV int cxx_deduce_argument(cxx *c, cxx_partial *p, ast_qualified_type P,
                              ast_qualified_type A, bool lvalue, cxx_targ *bind)
{
    if (c->memptr_ty && P != CXX_TY_UNKNOWN &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(P) ==
            AST_QUALIFIED_TYPE_UNQUALIFIED(c->memptr_ty) &&
        A != CXX_TY_UNKNOWN &&
        AST_QUALIFIED_TYPE_UNQUALIFIED(A) !=
            AST_QUALIFIED_TYPE_UNQUALIFIED(c->memptr_ty) &&
        !cc_type_is_record(c->ast, A))
        return 0; /* (R C::* from something that is no pointer to member: no deduction) */
    if (P == CXX_TY_UNKNOWN || !cxx_mentions_placeholder(c, P, 0))
        return 1; /* (non-deduced: its conversion is not checked here) */
    if (A == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, A) == AST_TYPE_VOID) {
        c->why = CXX_WHY_ARG;
        return -1; /* (void: a type vcc got wrong; no argument has it) */
    }
    if (AST_TYPE_KIND(c->ast, P) == AST_TYPE_POINTER &&
        AST_TYPE(c->ast, P)->ext) {
        int ext = AST_TYPE(c->ast, P)->ext;
        void *owner;

        P = AST_TYPE(c->ast, P)->base;
        /* T&& with an lvalue: a forwarding reference deduces T as A& */
        if (ext == 2 && !AST_QUALIFIED_TYPE_QUALIFIERS(P) && lvalue &&
            cxx_placeholder_of(c, P, &owner) >= 0 && owner == p)
            A = cxx_type_reference(c, A, false);
        else
            A = AST_QUALIFIED_TYPE(
                AST_QUALIFIED_TYPE_INDEX(A),
                AST_QUALIFIED_TYPE_QUALIFIERS(A) |
                    AST_QUALIFIED_TYPE_QUALIFIERS(
                        P)); /* (const T& binds what is not const too) */
    } else {
        /* by value: arrays and functions decay, top-level cv goes */
        const ctype *at = AST_TYPE(c->ast, A);

        if (at->kind == AST_TYPE_ARRAY)
            A = cc_type_pointer(c->ast, at->base);
        else if (at->kind == AST_TYPE_FUNCTION)
            A = cc_type_pointer(c->ast, A);
        A = AST_QUALIFIED_TYPE_UNQUALIFIED(A);
        P = AST_QUALIFIED_TYPE_UNQUALIFIED(
            P); /* (a by-value parameter's own const is not part of the function's type) */
        /* const T * from int *: a qualification conversion, the pointee as const as P's ([temp.deduct.call]/4.2) */
        if (AST_TYPE_KIND(c->ast, P) == AST_TYPE_POINTER &&
            !AST_TYPE(c->ast, P)->ext &&
            AST_TYPE_KIND(c->ast, A) == AST_TYPE_POINTER &&
            !AST_TYPE(c->ast, A)->ext &&
            AST_TYPE(c->ast, P)->base != CXX_TY_UNKNOWN &&
            AST_TYPE(c->ast, A)->base != CXX_TY_UNKNOWN) {
            ast_qualified_type pb = AST_TYPE(c->ast, P)->base,
                               ab = AST_TYPE(c->ast, A)->base;

            if ((AST_QUALIFIED_TYPE_QUALIFIERS(pb) &
                 AST_QUALIFIED_TYPE_QUALIFIERS(ab)) ==
                    AST_QUALIFIED_TYPE_QUALIFIERS(ab) &&
                AST_QUALIFIED_TYPE_QUALIFIERS(pb) !=
                    AST_QUALIFIED_TYPE_QUALIFIERS(ab))
                A = cc_type_pointer(
                    c->ast,
                    AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(ab),
                                       AST_QUALIFIED_TYPE_QUALIFIERS(ab) |
                                           AST_QUALIFIED_TYPE_QUALIFIERS(pb)));
        }
    }
    if (!cxx_match_type(c, p, P, A, bind, 0)) {
        /* X<T> against a class derived from X<int>: its bases, nearest first */
        if (AST_TYPE_KIND(c->ast, AST_QUALIFIED_TYPE_UNQUALIFIED(A)) ==
                AST_TYPE_STRUCT &&
            cxx_match_bases(c, p, P, A, bind, 0))
            return 1;
        /* ... X<T> * against a pointer to one ([temp.deduct.call]/4.3): the pointee's bases, as const and
		   volatile as P's pointee (__cxx_atomic_load(const volatile __cxx_atomic_base_impl<T> *) for a
		   __cxx_atomic_impl<bool> *) */
        if (AST_TYPE_KIND(c->ast, P) == AST_TYPE_POINTER &&
            !AST_TYPE(c->ast, P)->ext &&
            AST_TYPE_KIND(c->ast, A) == AST_TYPE_POINTER &&
            !AST_TYPE(c->ast, A)->ext &&
            AST_TYPE(c->ast, P)->base != CXX_TY_UNKNOWN &&
            AST_TYPE(c->ast, A)->base != CXX_TY_UNKNOWN &&
            AST_TYPE_KIND(c->ast, AST_TYPE(c->ast, A)->base) ==
                AST_TYPE_STRUCT) {
            ast_qualified_type pb = AST_TYPE(c->ast, P)->base,
                               ab = AST_TYPE(c->ast, A)->base;

            if ((AST_QUALIFIED_TYPE_QUALIFIERS(pb) &
                 AST_QUALIFIED_TYPE_QUALIFIERS(ab)) ==
                    AST_QUALIFIED_TYPE_QUALIFIERS(ab) &&
                cxx_match_bases(
                    c, p, pb,
                    AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(ab),
                                       AST_QUALIFIED_TYPE_QUALIFIERS(pb)),
                    bind, 0))
                return 1;
        }
        /* (deduction does not convert: a pattern that matches neither the argument nor a base of it fails) */
        c->why = CXX_WHY_CLASS_MATCH;
        return 0;
    }
    return 1;
}

/* pat with p's placeholder (the pack's) replaced by t: T, const T, T&, T&&, T* */
VPRIV ast_qualified_type cxx_placeholder_subst(cxx *c, cxx_partial *p,
                                               ast_qualified_type pat,
                                               ast_qualified_type t, int depth)
{
    const ctype *x;
    void *owner;

    if (pat == CXX_TY_UNKNOWN || depth > 16)
        return CXX_TY_UNKNOWN;
    if (cxx_placeholder_of(c, pat, &owner) >= 0)
        return owner == p
                   ? (AST_TYPE_KIND(c->ast, t) == AST_TYPE_POINTER &&
                              AST_TYPE(c->ast, t)->ext
                          ? t
                          : cc_type_qualify(c->ast, t,
                                            AST_QUALIFIED_TYPE_QUALIFIERS(pat)))
                   : CXX_TY_UNKNOWN;
    x = AST_TYPE(c->ast, pat);
    if (x->kind == AST_TYPE_POINTER) {
        ast_qualified_type b =
            cxx_placeholder_subst(c, p, x->base, t, depth + 1);

        if (b == CXX_TY_UNKNOWN)
            return b;
        if (x->ext)
            return cxx_type_reference(c, b, x->ext == 2); /* (collapses) */
        return cc_type_qualify(c->ast, cc_type_pointer(c->ast, b),
                               AST_QUALIFIED_TYPE_QUALIFIERS(pat));
    }
    return cxx_mentions_placeholder(c, pat, 0) ? CXX_TY_UNKNOWN : pat;
}

/*
 * Deduce e's template arguments from a call: 0 not viable (deduction failed), 1 viable, *ret its
 * return type (CXX_TY_UNKNOWN if it cannot be formed), -1 not known (vcc cannot deduce it).
 */
VPRIV int cxx_ftemplate_deduce1(cxx *c, cxx_entity *e, const cxx_targ *xargs,
                                uint32_t nxargs, const ast_qualified_type *args,
                                const uint8_t *lval, uint32_t nargs,
                                ast_qualified_type *ret, bool body);

/* (a candidate that does not deduce, or whose default arguments or constraints fail, is not an error
   in the expression that calls: what its evaluation left in the validity flags goes with it) */
VPRIV int cxx_ftemplate_deduce(cxx *c, cxx_entity *e, const cxx_targ *xargs,
                               uint32_t nxargs, const ast_qualified_type *args,
                               const uint8_t *lval, uint32_t nargs,
                               ast_qualified_type *ret, bool body)
{
    uint8_t invalid = c->einvalid, unsure = c->eunsure,
            missing = c->member_missing, adl = c->einvalid_unsure;
    cxx_entity *outer_te = c->ftmpl_te;
    int r;

    c->ftmpl_te = e;
    if (body)
        c->call_instance = NULL;
    r = cxx_ftemplate_deduce1(c, e, xargs, nxargs, args, lval, nargs, ret,
                              body);
    c->ftmpl_te = outer_te;
    c->einvalid = invalid, c->eunsure = unsure, c->member_missing = missing,
    c->einvalid_unsure = adl;
    return r;
}

VPRIV int cxx_ftemplate_deduce1(cxx *c, cxx_entity *e, const cxx_targ *xargs,
                                uint32_t nxargs, const ast_qualified_type *args,
                                const uint8_t *lval, uint32_t nargs,
                                ast_qualified_type *ret, bool body)
{
    cxx_ftemplate *f = e->ftmpl;
    cxx_partial *p;
    const ctype *ft;
    cxx_targ bind[64];

    *ret = CXX_TY_UNKNOWN;
    if (!f) {
        c->why = CXX_WHY_NO_FTMPL;
        return -1;
    }
    cxx_ftemplate_evaluate(c, f);
    p = &f->p;
    /* (make<T>(args...) with a deducible pack goes below: T explicit, the pack from the arguments) */
    bool nothing_deduced =
        f->packs; /* (__and_helper<_Pred...>(0): its parameters name no template parameter) */

    if (!nothing_deduced && f->pack_tp && !f->pack_param && f->has_pt) {
        nothing_deduced = true;
        for (uint32_t i = 0; i < f->np && nothing_deduced; i++)
            nothing_deduced = f->pt[i] != CXX_TY_UNKNOWN &&
                              !cxx_mentions_placeholder(c, f->pt[i], 0);
    }
    bool none_given = !xargs && (!nxargs || nxargs == UINT32_MAX) &&
                      p->params->ntparams == 1; /* (UINT32_MAX: none written) */

    if (nothing_deduced && (xargs || none_given) && p->params->ntparams &&
        p->params->ntparams <= 32) {
        /* (no explicit argument and the one parameter a pack: it is empty, template <int&...> f()) */
        if (none_given)
            nxargs = 0;
        /* f<A, Ps...>(args) with the last parameter a pack and every argument explicit (libc++'s
		   __and_helper<_Pred...>(0)): the signature with them bound */
        uint32_t np = p->params->ntparams;
        bool simple = p->params->tparams[np - 1].pack &&
                      p->params->tparams[np - 1].kind != 3 && nxargs + 1 >= np;

        for (uint32_t i = 0; i + 1 < np && simple; i++)
            simple = !p->params->tparams[i].pack &&
                     p->params->tparams[i].kind != 3 && xargs[i].kind &&
                     (xargs[i].kind == 1) == (p->params->tparams[i].kind == 1);
        for (uint32_t i = np - 1; i < nxargs && simple; i++)
            simple = xargs[i].kind == 1 || xargs[i].kind == 2;
        if (simple) {
            *ret = cxx_ftemplate_return_pack(c, f, xargs, nxargs, false, NULL,
                                             UINT32_MAX);
            return c->sig_requires == 1 ? 1 : c->sig_requires;
        }
    }
    /* an explicit argument of the other kind (a value for a type parameter: get<1> against get<T>(tuple&)): not
	   viable, whatever is left to deduce */
    for (uint32_t i = 0; xargs && i < nxargs && i < p->params->ntparams &&
                         !p->params->tparams[i].pack;
         i++)
        if ((xargs[i].kind == 1 || xargs[i].kind == 2) &&
            p->params->tparams[i].kind != 3 &&
            (xargs[i].kind == 1) != (p->params->tparams[i].kind == 1))
            return 0;
    if (f->packs && f->has_pt && !f->variadic && !f->pack_param &&
        (nargs > f->np || nargs + e->ndefaults < f->np))
        return 0; /* (too many or too few arguments: not viable, packs or not; pair's piecewise constructor for pair()) */
    if (f->packs || p->params->ntparams > 64) {
        /* not deduced, but a parameter whose shape the argument cannot have rules it out: swap(tuple<Ts...>&,
		   tuple<Ts...>&) for two ints */
        if (f->has_pt && f->packs && !f->variadic)
            for (uint32_t i = 0; i < nargs && i < f->np; i++)
                if (cxx_shape_mismatch(c, f->pt[i], args[i]))
                    return 0;
        c->why = CXX_WHY_PACKS;
        return -1;
    }
    memset(bind, 0, p->params->ntparams * sizeof *bind);
    /* every parameter given explicitly: the placeholder signature is not needed (its parameter types may
	   not parse with placeholders: typename remove_reference<T>::type&) */
    if (xargs && nxargs == p->params->ntparams) {
        for (uint32_t i = 0; i < nxargs; i++) {
            if (!xargs[i].kind || xargs[i].kind > 2 ||
                (xargs[i].kind == 1) != (p->params->tparams[i].kind == 1))
                return xargs[i].kind ? 0 : -1;
            bind[i] = xargs[i];
        }
        {
            int k = cxx_ftemplate_constraints(c, f, bind);

            if (k <= 0)
                return k;
        }
        *ret = cxx_ftemplate_return(c, f, bind, body);
        if (c->sig_requires != 1)
            return c->sig_requires;
        return 1; /* (its arity is not checked: the signature with the arguments bound would tell) */
    }
    if (!f->has_pt) {
        c->why = CXX_WHY_SIG;
        return -1;
    }
    (void)ft;
    if (!f->pack_tp ||
        (!f->pack_param &&
         !f->variadic)) { /* (a pack takes any number of arguments; one only inside the
								   parameters' types does not: pair's (piecewise_construct_t, tuple&, tuple&, __tuple_indices...)) */
        if (nargs > f->np && !f->variadic)
            return 0;
        if (nargs + e->ndefaults < f->np)
            return 0;
    }
    /* explicit arguments: the first parameters (all of them: nothing left to deduce) */
    if (xargs) {
        if (nxargs > p->params->ntparams)
            return 0;
        for (uint32_t i = 0; i < nxargs; i++) {
            if (!xargs[i].kind || xargs[i].kind > 2 ||
                (xargs[i].kind == 1) != (p->params->tparams[i].kind == 1))
                return xargs[i].kind ? 0 : -1;
            bind[i] = xargs[i];
        }
    }
    uint32_t npk = 0;

    for (uint32_t i = 0; i < p->params->ntparams; i++)
        npk += p->params->tparams[i].pack;
    if (f->pack_tp && (f->pack_param || npk <= 1)) {
        /* f(A a, Ts&&... ts): the fixed parameters, then each further argument against the pack's pattern; or
		   the pack only inside the parameters' types (get(tuple<Ts...>&)): deduced there, as a list.  (Several
		   packs, each inside a parameter's type, pair(piecewise_construct_t, tuple<_Args1...>, tuple<_Args2...>):
		   the general path below, each deduced as a list.) */
        bool inside = !f->pack_param;
        uint32_t ntp = p->params->ntparams, fixed = inside ? f->np : f->np - 1,
                 pk = f->pack_tp - 1, ne = 0;
        cxx_targ flat[32];
        ast_qualified_type etypes[16];

        if (nargs < fixed && !(inside && nargs + e->ndefaults >= fixed))
            return 0;
        if (inside && nargs > fixed && !f->variadic)
            return 0;
        /* (a parameter whose shape the argument cannot have rules it out before the give-ups below:
		   operator<=>(const variant<Ts...>&, ...) requires (...) for two strings) */
        for (uint32_t i = 0; i < nargs && i < f->np; i++)
            if (cxx_shape_mismatch(c, f->pt[i], args[i]))
                return 0;
        if (nargs - fixed > 16 || ntp > 16 || f->req_pos ||
            (xargs &&
             nxargs > pk)) { /* (explicit elements of the pack: not mixed) */
            c->why = CXX_WHY_PACKS;
            return -1;
        }
        for (uint32_t i = 0; i < ntp; i++)
            if (i < pk &&
                ((!p->params->tparams[i].name && !p->params->tparams[i].def_b &&
                  !(xargs && i < nxargs)) ||
                 p->params->tparams[i]
                     .constraint_b)) { /* (an unnamed one given explicitly: bound) */
                c->why = CXX_WHY_PACKS;
                return -1; /* (constraints before a pack: not evaluated here; a default is, when not deduced) */
            }
        for (uint32_t i = 0; i < fixed && i < nargs; i++) {
            int r = cxx_deduce_argument(c, p, f->pt[i], args[i], lval[i], bind);

            if (r <= 0)
                return r;
        }
        if (inside) {
            /* the pack's elements, as the pattern deduced them (tuple<int, long>: int, long) */
            if (bind[pk].kind != 6 || bind[pk].v < 0 ||
                ntp - 1 + (uint32_t)bind[pk].v > 32 || bind[pk].v > 16) {
                c->why = CXX_WHY_PACKS;
                return -1;
            }
            for (uint32_t i = 0; i < (uint32_t)bind[pk].v; i++)
                flat[ntp - 1 + ne++] = bind[pk].list[bind[pk].t + i];
        }
        for (uint32_t i = inside ? nargs : fixed; i < nargs; i++) {
            int r;

            bind[pk].kind = 0;
            r = cxx_deduce_argument(c, p, f->pt[fixed], args[i], lval[i], bind);
            if (r <= 0)
                return r;
            if (bind[pk].kind != 1) {
                c->why = CXX_WHY_PACKS;
                return -1;
            }
            /* the element's parameter type: the pattern with this element (Ts&& with T = A&: A&) */
            etypes[ne] =
                cxx_placeholder_subst(c, p, f->pt[fixed], bind[pk].t, 0);
            if (etypes[ne] == CXX_TY_UNKNOWN) {
                c->why = CXX_WHY_PACKS;
                return -1;
            }
            flat[ntp - 1 + ne++] = bind[pk];
        }
        bind[pk].kind = 0;
        {
            /* the parameters other than the pack, in order (those after it: their defaults, with the pack bound),
			   then the pack's elements */
            cxx_scope *b = cxx_scope_new(c, CXX_SCOPE_BLOCK, p->home, NULL);
            uint32_t k = 0;

            for (uint32_t i = 0; i < pk; i++) {
                if (!bind[i].kind && p->params->tparams[i].def_b &&
                    (!cxx_default_argument(c, &p->params->tparams[i], b,
                                           &bind[i]) ||
                     !bind[i].kind)) {
                    c->why = CXX_WHY_UNBOUND;
                    return -1; /* (not deduced, and its default not known) */
                }
                if (!bind[i].kind) {
                    c->why = CXX_WHY_UNBOUND;
                    return -1;
                }
                flat[k++] = bind[i];
                cxx_bind(c, b, &p->params->tparams[i], &bind[i]);
            }
            cxx_bind_pack(c, b, &p->params->tparams[pk], flat + (ntp - 1), ne);
            for (uint32_t i = pk + 1; i < ntp; i++) {
                const cxx_tparam *tp = &p->params->tparams[i];

                if (tp->kind == 2 && tp->type_b && !tp->name) {
                    /* an unnamed value there for its type (enable_if_t<cond, int> = 0): the type must form */
                    cxx_outer o;
                    jmp_buf jb;
                    volatile bool failed = false;
                    volatile ast_qualified_type got = CXX_TY_UNKNOWN;

                    cxx_outer_save(c, &o);
                    c->instantiating++;
                    c->fail = &jb;
                    c->pos = tp->type_b;
                    c->scope = b;
                    c->template_depth = 0, c->in_template_args = 0,
                    c->explicit_specialization = 0, c->class_depth = 0;
                    c->implicit_typename = 0, c->member_missing = 0;
                    if (!setjmp(jb)) {
                        cxx_type_specifiers_parse(c);
                        got = c->ds_type;
                    } else
                        failed = true;
                    c->instantiating--;
                    cxx_outer_restore(c, &o);
                    if (c->member_missing) {
                        c->why = CXX_WHY_UNBOUND;
                        return 0; /* (no such type: a substitution failure, not viable) */
                    }
                    if (failed || got == CXX_TY_UNKNOWN) {
                        c->why = CXX_WHY_PACKS;
                        return -1;
                    }
                }
                if (!cxx_default_argument(c, &p->params->tparams[i], b,
                                          &flat[k])) {
                    c->why = CXX_WHY_PACKS;
                    return -1;
                }
                if (flat[k].kind == 0) {
                    c->why = CXX_WHY_UNBOUND;
                    return 0; /* (enable_if_t<false, ...>: no type, a substitution failure) */
                }
                cxx_bind(c, b, &p->params->tparams[i], &flat[k]);
                k++;
            }
        }
        *ret = cxx_ftemplate_return_pack(c, f, flat, ntp - 1 + ne, body,
                                         inside ? NULL : etypes,
                                         inside ? UINT32_MAX : ne);
        return c->sig_requires == 1 ? 1 : c->sig_requires;
    }
    for (uint32_t i = 0; i < nargs && i < f->np; i++) {
        int r = cxx_deduce_argument(c, p, f->pt[i], args[i], lval[i], bind);

        if (r <= 0 && getenv("VCC_CXX_SIG"))
            fprintf(stderr, "deduce: argument %u: %d\n", i, r);
        if (r <= 0)
            return r;
    }
    {
        int k = cxx_ftemplate_complete(c, f, bind);

        if (k <= 0 && getenv("VCC_CXX_SIG"))
            fprintf(stderr, "deduce: complete: %d\n", k);
        if (k > 0) {
            k = cxx_ftemplate_constraints(c, f, bind);
            if (k <= 0 && getenv("VCC_CXX_SIG"))
                fprintf(stderr, "deduce: constraints: %d %.*s at %u\n", k,
                        (int)vintern_ents[e->name].len, vintern_ents[e->name].s,
                        f->req_pos);
        }
        if (k <= 0) {
            c->why = CXX_WHY_UNBOUND;
            return k;
        }
    }
    *ret = cxx_ftemplate_return(c, f, bind, body);
    if (c->sig_requires != 1 && getenv("VCC_CXX_SIG"))
        fprintf(stderr, "deduce: signature: %d\n", c->sig_requires);
    if (c->sig_requires != 1) {
        c->why = CXX_WHY_UNBOUND;
        return c
            ->sig_requires; /* its trailing requires-clause: false, or not known */
    }
    return 1;
}
