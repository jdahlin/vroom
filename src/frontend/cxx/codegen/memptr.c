/* C++ code generation: pointers to members (frontend/cxx/memptr.c has their types and layout) */
#include "codegen.h"

/* a value of pointer to member type t from its parts' nodes (a long each; v1 only for a member function's):
   C's compound literal, an lvalue (its initializer list is a constant one's when the parts are constants) */
VPRIV uint32_t cxx_gen_memptr_value(cxx *c, ast_qualified_type t, uint32_t v0,
                                    uint32_t v1, uint32_t loc)
{
    cc *a = c->ast;
    const cxx_memptr *mp = cxx_memptr_of(c, t);
    cc_id_list l;
    uint32_t init, s, n;

    if (!cxx_gen_on(c) || !mp || !v0 || (mp->fn && !v1))
        return 0;
    t = AST_QUALIFIED_TYPE_UNQUALIFIED(t);
    cc_id_list_init(&l);
    cc_id_list_append(a, &l,
                      cc_conversion_apply(a, cc_conversion_decay(a, v0),
                                          cc_type_basic(AST_TYPE_LONG)));
    if (mp->fn)
        cc_id_list_append(a, &l,
                          cc_conversion_apply(a, cc_conversion_decay(a, v1),
                                              cc_type_basic(AST_TYPE_LONG)));
    init = cc_node_new(a, AST_NODE_INITIALIZER, t, loc);
    AST_NODE_AT(a, init)->aux = 0;
    AST_NODE_AT(a, init)->c = l.n;
    AST_NODE_AT(a, init)->b = cc_id_list_freeze(a, &l);
    s = cc_symbol_new(a, AST_SYMBOL_VARIABLE, 0, t, loc);
    a->syms[s].body = init;
    a->syms[s].sc = AST_STORAGE_AUTO;
    n = cc_node_new_operands(a, AST_NODE_COMPOUND_LITERAL, t, loc, init, s);
    AST_NODE_AT(a, n)->flags = AST_NODE_FLAG_LVALUE;
    return n;
}

/* the null pointer to member of type t: a data member's offset -1, a member function's ptr 0 */
VPRIV uint32_t cxx_gen_memptr_null(cxx *c, ast_qualified_type t, uint32_t loc)
{
    const cxx_memptr *mp = cxx_memptr_of(c, t);
    cc *a = c->ast;

    if (!mp)
        return 0;
    return mp->fn ? cxx_gen_memptr_value(
                        c, t,
                        cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_LONG),
                                            loc),
                        cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_LONG),
                                            loc),
                        loc)
                  : cxx_gen_memptr_value(
                        c, t,
                        cc_node_integer_new(a, -1, cc_type_basic(AST_TYPE_LONG),
                                            loc),
                        0, loc);
}

/* the offset of the subobject or member a node reached from *(C *)0 is at: C's constant (0: none) */
static bool cxx_gen_null_offset(cxx *c, uint32_t lv, int64_t *off)
{
    cc *a = c->ast;
    cc_value v;
    uint32_t n;

    if (!lv)
        return false;
    n = cc_cast_insert(a, cc_address_of_build(a, lv, 0),
                       cc_type_basic(AST_TYPE_LONG),
                       AST_CAST_POINTER_TO_INTEGER);
    if (!cc_eval(a, n, &v) ||
        (v.kind != CC_VALUE_INTEGER &&
         !(v.kind == CC_VALUE_ADDRESS && !v.sym && !v.str)))
        return false;
    *off = (int64_t)v.i;
    return true;
}

/* *(C *)0: an lvalue of class cls to reach members and bases from, at offset 0 */
static uint32_t cxx_gen_null_object(cxx *c, ast_qualified_type cls,
                                    uint32_t loc)
{
    cc *a = c->ast;

    return cc_dereference_build(
        a,
        cc_cast_insert(
            a, cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_LONG), loc),
            cc_type_pointer(a, AST_QUALIFIED_TYPE_UNQUALIFIED(cls)),
            AST_CAST_INTEGER_TO_POINTER),
        loc);
}

/* &C::m: the pointer to member of type t to e, a non-static data member or member function (0: not built) */
VPRIV uint32_t cxx_gen_memptr_of(cxx *c, cxx_entity *e, ast_qualified_type t,
                                 uint32_t loc)
{
    cc *a = c->ast;
    const cxx_memptr *mp = cxx_memptr_of(c, t);
    int64_t off;

    if (!cxx_gen_on(c) || !mp || !e)
        return 0;
    if (!mp->fn) {
        uint32_t m = cxx_gen_member_of(c, cxx_gen_null_object(c, mp->cls, loc),
                                       e->name, loc, 0);

        if (!cxx_gen_null_offset(c, m, &off))
            return 0;
        return cxx_gen_memptr_value(
            c, t,
            cc_node_integer_new(a, off, cc_type_basic(AST_TYPE_LONG), loc), 0,
            loc);
    }
    if (e->fn_virtual &&
        e->vslot) /* (virtual: 1 + its slot's offset in the vtable, which the call reads) */
        return cxx_gen_memptr_value(
            c, t,
            cc_node_integer_new(a, 1 + 8 * (int64_t)(e->vslot - 1),
                                cc_type_basic(AST_TYPE_LONG), loc),
            cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_LONG), loc), loc);
    uint32_t en = c->enode, fn;

    cxx_gen_function_value(c, e, loc);
    fn = c->enode, c->enode = en;
    if (!fn)
        return 0;
    fn = cc_cast_insert(a, cc_conversion_decay(a, fn),
                        cc_type_basic(AST_TYPE_LONG),
                        AST_CAST_POINTER_TO_INTEGER);
    return cxx_gen_memptr_value(
        c, t, fn, cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_LONG), loc),
        loc);
}

/* the offset of the base subobject of type to in an object of type from (a pointer to member of from's
   base, converted to one of from: what is added); false: not a base known here */
VPRIV bool cxx_gen_base_offset(cxx *c, ast_qualified_type from,
                               ast_qualified_type to, int64_t *off)
{
    uint32_t o, b;

    if (AST_QUALIFIED_TYPE_UNQUALIFIED(from) ==
        AST_QUALIFIED_TYPE_UNQUALIFIED(to)) {
        *off = 0;
        return true;
    }
    o = cxx_gen_null_object(c, from, 0);
    b = cxx_gen_base_object(c, o, AST_QUALIFIED_TYPE_UNQUALIFIED(to), 0);
    return b && cxx_gen_null_offset(c, b, off);
}

/*
 * x.*mp, p->*mp (obj the object, arrow: obj a pointer to it).  Of a data member: the member, an lvalue,
 * *(T *)((char *)&x + mp.off).  Of a member function: the function to call, *(R (*)(C *, P...))fn with fn
 * mp.ptr or, odd (virtual), the vtable's entry at mp.ptr - 1; its this, (char *)&x + mp.adj, is in
 * c->bmf_this for the call (cxx_gen_indirect_call of c->bmf_callee passes it first).  0: not built.
 */
static uint32_t mp_cast(cc *a, uint32_t n, ast_qualified_type t)
{
    return cc_cast_insert(
        a, n, t, cc_conversion_cast_kind(a, AST_NODE_AT(a, n)->ty, t, n));
}

VPRIV uint32_t cxx_gen_memptr_apply(cxx *c, uint32_t obj, bool arrow,
                                    uint32_t mpn, ast_qualified_type mpt,
                                    uint32_t loc)
{
    cc *a = c->ast;
    const cxx_memptr *mp = cxx_memptr_of(c, mpt);
    ast_qualified_type cp = cc_type_pointer(a, cc_type_basic(AST_TYPE_CHAR)),
                       ot;
    uint32_t p, tm, pre;

    if (!cxx_gen_on(c) || !mp || !obj || !mpn)
        return 0;
    p = arrow ? cc_node_rvalue(a, cc_conversion_decay(a, obj))
              : cc_address_of_build(a, obj, loc);
    ot = AST_TYPE_KIND(a, AST_NODE_AT(a, p)->ty) == AST_TYPE_POINTER
             ? AST_TYPE(a, AST_NODE_AT(a, p)->ty)->base
             : CXX_TY_UNKNOWN;
    if (ot == CXX_TY_UNKNOWN || !cc_type_is_record(a, ot))
        return 0;
    uint8_t oq = AST_QUALIFIED_TYPE_QUALIFIERS(ot);

    if (AST_QUALIFIED_TYPE_UNQUALIFIED(ot) != mp->cls) {
        p = cxx_gen_base_pointer(
            c, p, AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(mp->cls), oq),
            loc); /* (a derived object: its base's address) */
        if (!p)
            return 0;
    }
    /* (the pointer to member read once: a temporary) */
    if (!(tm = cxx_gen_temporary(c, AST_QUALIFIED_TYPE_UNQUALIFIED(mpt), loc)))
        return 0;
    pre = cc_assignment_build(a, 0, cxx_gen_var_node(c, tm, loc),
                              cc_node_rvalue(a, mpn), loc);
    if (!mp->fn) {
        ast_qualified_type mt = AST_QUALIFIED_TYPE(
            AST_QUALIFIED_TYPE_INDEX(mp->mem),
            AST_QUALIFIED_TYPE_QUALIFIERS(mp->mem) |
                (oq & (AST_QUALIFIER_CONST | AST_QUALIFIER_VOLATILE)));
        uint32_t at = cc_binary_build(
            a, LEX_TOKEN_PLUS, mp_cast(a, p, cp),
            cc_node_rvalue(a, member(a, cxx_gen_var_node(c, tm, loc),
                                     vintern_cstr("__off"), loc)),
            loc);

        at = mp_cast(a, at, cc_type_pointer(a, mt));
        return cc_dereference_build(a,
                                    cc_node_new_operands(a, AST_NODE_COMMA,
                                                         AST_NODE_AT(a, at)->ty,
                                                         loc, pre, at),
                                    loc);
    }
    /* a member function: this, then the function's address */
    const ctype *ft = AST_TYPE(a, mp->mem);
    ast_qualified_type ps[33],
        tp = cc_type_pointer(
            a, AST_QUALIFIED_TYPE(
                   AST_QUALIFIED_TYPE_INDEX(mp->cls),
                   (mp->quals & 1 ? AST_QUALIFIER_CONST : 0) |
                       (mp->quals & 2 ? AST_QUALIFIER_VOLATILE : 0)));
    uint32_t th, set, ptr, odd, virt, fnv, fp;

    if (ft->n > 32 || !(th = cxx_gen_temporary(c, cp, loc)))
        return 0;
    ps[0] = tp;
    for (uint32_t i = 0; i < ft->n; i++)
        ps[i + 1] = a->params[ft->aux + i];
    ast_qualified_type fpt = cc_type_pointer(
        a, cxx_type_function(c, ft->base, ps, ft->n + 1,
                             (ft->flags & AST_TYPE_FLAG_VARIADIC) != 0));

    set = cc_assignment_build(
        a, 0, cxx_gen_var_node(c, th, loc),
        cc_binary_build(
            a, LEX_TOKEN_PLUS, mp_cast(a, p, cp),
            cc_node_rvalue(a, member(a, cxx_gen_var_node(c, tm, loc),
                                     vintern_cstr("__adj"), loc)),
            loc),
        loc);
    pre = cc_node_new_operands(a, AST_NODE_COMMA, AST_NODE_AT(a, set)->ty, loc,
                               pre, set);
    ptr = cc_node_rvalue(
        a, member(a, cxx_gen_var_node(c, tm, loc), vintern_cstr("__ptr"), loc));
    odd = cc_binary_build(
        a, LEX_TOKEN_AMPERSAND, ptr,
        cc_node_integer_new(a, 1, cc_type_basic(AST_TYPE_LONG), loc), loc);
    /* *(long *)(*(char **)this + ptr - 1): the vtable's entry */
    virt = cc_dereference_build(
        a,
        mp_cast(
            a,
            cc_node_rvalue(
                a, cc_dereference_build(
                       a,
                       cc_cast_insert(
                           a, cc_node_rvalue(a, cxx_gen_var_node(c, th, loc)),
                           cc_type_pointer(a, cp), AST_CAST_NOP),
                       loc)),
            cp),
        loc);
    virt = cc_binary_build(
        a, LEX_TOKEN_MINUS,
        cc_binary_build(
            a, LEX_TOKEN_PLUS, cc_address_of_build(a, virt, loc),
            cc_node_rvalue(a, member(a, cxx_gen_var_node(c, tm, loc),
                                     vintern_cstr("__ptr"), loc)),
            loc),
        cc_node_integer_new(a, 1, cc_type_basic(AST_TYPE_LONG), loc), loc);
    virt = cc_node_rvalue(
        a,
        cc_dereference_build(
            a,
            mp_cast(a, virt, cc_type_pointer(a, cc_type_basic(AST_TYPE_LONG))),
            loc));
    fnv = cc_conditional_build(
        a, cc_condition_scalar(a, odd, "'->*'"), virt,
        cc_node_rvalue(a, member(a, cxx_gen_var_node(c, tm, loc),
                                 vintern_cstr("__ptr"), loc)),
        loc);
    fp = cc_cast_insert(a, fnv, fpt, AST_CAST_INTEGER_TO_POINTER);
    c->bmf_pre =
        pre; /* (run before the call: C orders neither the callee nor the arguments) */
    c->bmf_this =
        mp_cast(a, cc_node_rvalue(a, cxx_gen_var_node(c, th, loc)), tp);
    c->bmf_callee = cc_dereference_build(a, fp, loc);
    return c->bmf_callee;
}

/* the long constant of a pointer to member value's part k, when the value is a literal of constants */
static bool cxx_gen_memptr_part_const(cxx *c, uint32_t x, uint32_t k,
                                      int64_t *v)
{
    cc *a = c->ast;
    const node *n = AST_NODE_AT(a, x);
    cc_value cv;

    if (n->op != AST_NODE_COMPOUND_LITERAL || !n->a ||
        AST_NODE_AT(a, n->a)->op != AST_NODE_INITIALIZER ||
        k >= AST_NODE_AT(a, n->a)->c)
        return false;
    if (!cc_eval(a, a->lists[AST_NODE_AT(a, n->a)->b + k], &cv) ||
        cv.kind != CC_VALUE_INTEGER)
        return false;
    *v = (int64_t)cv.i;
    return true;
}

/*
 * x converted to the pointer to member type to: itself; the null one from 0 or nullptr; one of a base B's
 * member to one of a class D derived from B ([conv.mem]): the base's offset in D added (a data member's
 * offset, -1 kept; a member function's adj).  0: not a conversion built here.
 */
VPRIV uint32_t cxx_gen_memptr_convert(cxx *c, uint32_t x, ast_qualified_type to,
                                      uint32_t loc)
{
    cc *a = c->ast;
    const cxx_memptr *mt = cxx_memptr_of(c, to), *mf;
    ast_qualified_type ft;
    cc_value cv;
    int64_t off, v0, v1;

    if (!cxx_gen_on(c) || !x || !mt)
        return 0;
    ft = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty);
    if (ft == AST_QUALIFIED_TYPE_UNQUALIFIED(to))
        return x;
    if ((c->nullptr_ty && ft == c->nullptr_ty) ||
        (AST_TYPE_KIND(a, ft) == AST_TYPE_POINTER && cc_eval(a, x, &cv) &&
         cv.kind == CC_VALUE_INTEGER && !cv.i) ||
        (cc_type_is_integer(a, ft) && cc_eval(a, x, &cv) &&
         cv.kind == CC_VALUE_INTEGER && !cv.i))
        return cxx_gen_memptr_null(c, to, loc);
    if (!(mf = cxx_memptr_of(c, ft)) || mf->fn != mt->fn ||
        mf->mem != mt->mem || mf->quals != mt->quals ||
        !cxx_gen_base_offset(c, mt->cls, mf->cls, &off))
        return 0;
    if (!mt->fn && cxx_gen_memptr_part_const(c, x, 0, &v0))
        return cxx_gen_memptr_value(
            c, to,
            cc_node_integer_new(a, v0 == -1 ? -1 : v0 + off,
                                cc_type_basic(AST_TYPE_LONG), loc),
            0, loc);
    if (mt->fn && cxx_gen_memptr_part_const(c, x, 1, &v1)) {
        /* (ptr as it is: the function's address is a constant, not an integer one) */
        const node *n = AST_NODE_AT(a, x);
        uint32_t p0 = a->lists[AST_NODE_AT(a, n->a)->b];

        return cxx_gen_memptr_value(
            c, to, p0,
            cc_node_integer_new(a, v1 + off, cc_type_basic(AST_TYPE_LONG), loc),
            loc);
    }
    /* (not constants: a temporary, its parts adjusted) */
    uint32_t tm = cxx_gen_temporary(c, AST_QUALIFIED_TYPE_UNQUALIFIED(ft), loc);

    if (!tm)
        return 0;
    uint32_t set = cc_assignment_build(a, 0, cxx_gen_var_node(c, tm, loc),
                                       cc_node_rvalue(a, x), loc);
    uint32_t p0 = cc_node_rvalue(
                 a, member(a, cxx_gen_var_node(c, tm, loc),
                           vintern_cstr(mt->fn ? "__ptr" : "__off"), loc)),
             r;

    if (!mt->fn) {
        uint32_t isnull = cc_binary_build(
            a, LEX_TOKEN_EQUAL_EQUAL, p0,
            cc_node_integer_new(a, -1, cc_type_basic(AST_TYPE_LONG), loc), loc);

        r = cxx_gen_memptr_value(
            c, to,
            cc_conditional_build(
                a, isnull,
                cc_node_integer_new(a, -1, cc_type_basic(AST_TYPE_LONG), loc),
                cc_binary_build(
                    a, LEX_TOKEN_PLUS,
                    cc_node_rvalue(a, member(a, cxx_gen_var_node(c, tm, loc),
                                             vintern_cstr("__off"), loc)),
                    cc_node_integer_new(a, off, cc_type_basic(AST_TYPE_LONG),
                                        loc),
                    loc),
                loc),
            0, loc);
    } else
        r = cxx_gen_memptr_value(
            c, to, p0,
            cc_binary_build(
                a, LEX_TOKEN_PLUS,
                cc_node_rvalue(a, member(a, cxx_gen_var_node(c, tm, loc),
                                         vintern_cstr("__adj"), loc)),
                cc_node_integer_new(a, off, cc_type_basic(AST_TYPE_LONG), loc),
                loc),
            loc);
    return r ? cc_node_new_operands(a, AST_NODE_COMMA,
                                    AST_QUALIFIED_TYPE_UNQUALIFIED(to), loc,
                                    set, cc_node_rvalue(a, r))
             : 0;
}

/* x as a truth value: a data member's offset not -1, a member function's ptr not 0 (an int, 0: not built) */
VPRIV uint32_t cxx_gen_memptr_test(cxx *c, uint32_t x, uint32_t loc)
{
    cc *a = c->ast;
    const cxx_memptr *mp = x ? cxx_memptr_of(c, AST_NODE_AT(a, x)->ty) : NULL;
    uint32_t tm;

    if (!cxx_gen_on(c) || !mp ||
        !(tm = cxx_gen_temporary(
              c, AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty), loc)))
        return 0;
    uint32_t set = cc_assignment_build(a, 0, cxx_gen_var_node(c, tm, loc),
                                       cc_node_rvalue(a, x), loc);
    uint32_t t = cc_binary_build(
        a, LEX_TOKEN_EXCLAMATION_EQUAL,
        cc_node_rvalue(a,
                       member(a, cxx_gen_var_node(c, tm, loc),
                              vintern_cstr(mp->fn ? "__ptr" : "__off"), loc)),
        cc_node_integer_new(a, mp->fn ? 0 : -1, cc_type_basic(AST_TYPE_LONG),
                            loc),
        loc);

    return cc_node_new_operands(a, AST_NODE_COMMA, AST_NODE_AT(a, t)->ty, loc,
                                set, t);
}

static uint32_t mp_logical(cc *a, int op, uint32_t l, uint32_t r, uint32_t loc)
{
    return cc_node_new_operands(a, op, cc_type_basic(AST_TYPE_INT), loc,
                                cc_condition_scalar(a, l, "'=='"),
                                cc_condition_scalar(a, r, "'=='"));
}

/* x == y (eq) or x != y of one pointer to member type: the offsets equal; ptr equal and, unless both are null,
   adj too ([expr.eq]/4, the Itanium ABI's) */
VPRIV uint32_t cxx_gen_memptr_compare(cxx *c, uint32_t x, uint32_t y, bool eq,
                                      uint32_t loc)
{
    cc *a = c->ast;
    const cxx_memptr *mp = x ? cxx_memptr_of(c, AST_NODE_AT(a, x)->ty) : NULL;
    ast_qualified_type t;
    uint32_t tx, ty, r;

    if (!cxx_gen_on(c) || !mp || !y)
        return 0;
    t = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, x)->ty);
    if (AST_QUALIFIED_TYPE_UNQUALIFIED(AST_NODE_AT(a, y)->ty) != t &&
        !(y = cxx_gen_memptr_convert(c, y, t, loc)))
        return 0;
    if (!(tx = cxx_gen_temporary(c, t, loc)) ||
        !(ty = cxx_gen_temporary(c, t, loc)))
        return 0;
    uint32_t pre = cc_node_new_operands(
        a, AST_NODE_COMMA, t, loc,
        cc_assignment_build(a, 0, cxx_gen_var_node(c, tx, loc),
                            cc_node_rvalue(a, x), loc),
        cc_assignment_build(a, 0, cxx_gen_var_node(c, ty, loc),
                            cc_node_rvalue(a, y), loc));
#define MP_PART(v, f)                                                          \
    cc_node_rvalue(                                                            \
        a, member(a, cxx_gen_var_node(c, (v), loc), vintern_cstr(f), loc))
    r = cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL,
                        MP_PART(tx, mp->fn ? "__ptr" : "__off"),
                        MP_PART(ty, mp->fn ? "__ptr" : "__off"), loc);
    if (mp->fn)
        r = mp_logical(
            a, AST_NODE_LOGICAL_AND, r,
            mp_logical(
                a, AST_NODE_LOGICAL_OR,
                cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, MP_PART(tx, "__ptr"),
                                cc_node_integer_new(
                                    a, 0, cc_type_basic(AST_TYPE_LONG), loc),
                                loc),
                cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, MP_PART(tx, "__adj"),
                                MP_PART(ty, "__adj"), loc),
                loc),
            loc);
#undef MP_PART
    if (!eq)
        r = cc_binary_build(
            a, LEX_TOKEN_EQUAL_EQUAL, r,
            cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_INT), loc), loc);
    return cc_node_new_operands(a, AST_NODE_COMMA, AST_NODE_AT(a, r)->ty, loc,
                                pre, r);
}
