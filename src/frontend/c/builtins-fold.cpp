/* vcc front end: constant folding of builtins (cc_builtin_classify_type, cc_builtin_fold); see builtin-table.c for where a builtin's other facts live */
#include "cc-internal.h"
#include "frontend/builtin-fold.h"

static int cc_builtin_classify_type(cc *c, ast_qualified_type t)
{
    switch (AST_TYPE_KIND(c, t)) {
    case AST_TYPE_VOID:
        return 0;
    case AST_TYPE_BOOL:
        return 4;
    case AST_TYPE_ENUM:
        return 3;
    case AST_TYPE_POINTER:
        return 5;
    case AST_TYPE_STRUCT:
        return 12;
    case AST_TYPE_UNION:
        return 13;
    case AST_TYPE_ARRAY:
        return 5;
    case AST_TYPE_FUNCTION:
        return 5;
    default:
        if (cc_type_is_float(c, t))
            return 8;
        return 1;
    }
}

/* the operation of frontend/builtin-fold.h an integer builtin node is, or
   vfold_none */
static int cc_builtin_integer_operation(unsigned id)
{
    switch (id) {
    case CC_BUILTIN_CLZ:
    case CC_BUILTIN_CLZL:
    case CC_BUILTIN_CLZLL:
        return vfold_clz;
    case CC_BUILTIN_CTZ:
    case CC_BUILTIN_CTZL:
    case CC_BUILTIN_CTZLL:
        return vfold_ctz;
    case CC_BUILTIN_FFS:
    case CC_BUILTIN_FFSL:
    case CC_BUILTIN_FFSLL:
        return vfold_ffs;
    case CC_BUILTIN_POPCOUNT:
    case CC_BUILTIN_POPCOUNTL:
    case CC_BUILTIN_POPCOUNTLL:
        return vfold_popcount;
    case CC_BUILTIN_PARITY:
    case CC_BUILTIN_PARITYL:
    case CC_BUILTIN_PARITYLL:
        return vfold_parity;
    case CC_BUILTIN_CLRSB:
    case CC_BUILTIN_CLRSBL:
    case CC_BUILTIN_CLRSBLL:
        return vfold_clrsb;
    case CC_BUILTIN_BSWAP16:
    case CC_BUILTIN_BSWAP32:
    case CC_BUILTIN_BSWAP64:
        return vfold_bswap;
    case CC_BUILTIN_BITREVERSE8:
    case CC_BUILTIN_BITREVERSE16:
    case CC_BUILTIN_BITREVERSE32:
    case CC_BUILTIN_BITREVERSE64:
        return vfold_bitreverse;
    case CC_BUILTIN_ROTL8:
    case CC_BUILTIN_ROTL16:
    case CC_BUILTIN_ROTL32:
    case CC_BUILTIN_ROTL64:
        return vfold_rotate_left;
    case CC_BUILTIN_ROTR8:
    case CC_BUILTIN_ROTR16:
    case CC_BUILTIN_ROTR32:
    case CC_BUILTIN_ROTR64:
        return vfold_rotate_right;
    default:
        return vfold_none;
    }
}

/*
 * Whether a builtin node of this id changes nothing by itself: its value
 * depends on its arguments alone (what cc_builtin_fold computes), so the
 * effects of the node are those of its arguments.  An array bound of such a
 * call that folds is a constant bound, as in clang (which folds a bound
 * that is not an integer constant expression and says it is an extension).
 */
CC_PRIVATE bool cc_builtin_is_pure(unsigned id)
{
    if (cc_builtin_integer_operation(id) != vfold_none)
        return true;
    switch (id) {
    case CC_BUILTIN_CLASSIFY_TYPE:
    case CC_BUILTIN_CONSTANT_P:
    case CC_BUILTIN_EXPECT:
    case CC_BUILTIN_EXPECT_PROB:
    case CC_BUILTIN_ISNAN:
    case CC_BUILTIN_ISINF:
    case CC_BUILTIN_ISINF_SIGN:
    case CC_BUILTIN_ISFINITE:
    case CC_BUILTIN_ISNORMAL:
    case CC_BUILTIN_SIGNBIT:
    case CC_BUILTIN_ISSIGNALING:
    case CC_BUILTIN_ISGREATER:
    case CC_BUILTIN_ISGREATEREQUAL:
    case CC_BUILTIN_ISLESS:
    case CC_BUILTIN_ISLESSEQUAL:
    case CC_BUILTIN_ISLESSGREATER:
    case CC_BUILTIN_ISUNORDERED:
    case CC_BUILTIN_OBJECT_SIZE:
    case CC_BUILTIN_DYNAMIC_OBJECT_SIZE:
        return true;
    default:
        return false;
    }
}

CC_PRIVATE bool cc_builtin_fold(cc *c, const node *x, cc_value *v)
{
    const uint32_t *arg = &c->lists[x->b];
    cc_value a;

    v->kind = CC_VALUE_INTEGER;
    v->ice = true;
    switch (x->aux) {
    case CC_BUILTIN_OBJECT_SIZE:
    case CC_BUILTIN_DYNAMIC_OBJECT_SIZE: {
        cc_object_bound b;
        if (!cc_constant_eval(c, arg[1], &a) || a.kind != CC_VALUE_INTEGER)
            return false;
        if (cc_builtin_object_size_pointer_bound(c, arg[0], &b, 0,
                                                 (unsigned)a.i)) {
            const i128 end =
                (a.i & 1) && (b.whole < 0 || b.sub < b.whole) ? b.sub : b.whole;
            v->i = end < 0 ? (a.i & 2 ? 0 : (i128)(uint64_t)-1)
                   : b.offset < 0 || b.offset > end ? 0
                                                    : end - b.offset;
        } else
            v->i = a.i & 2 ? 0 : (i128)(uint64_t)-1;
        v->ice = false;
        return true;
    }
    case CC_BUILTIN_CONSTANT_P:
        if (cc_constant_eval(c, arg[0], &a) &&
            (a.kind == CC_VALUE_INTEGER || a.kind == CC_VALUE_FLOAT ||
             (a.kind == CC_VALUE_ADDRESS && a.str && a.i == 0)))
            v->i = 1;
        else
            v->i = 0;
        return true;
    case CC_BUILTIN_EXPECT:
    case CC_BUILTIN_EXPECT_PROB:
        if (!cc_constant_eval(c, arg[0], v))
            return false;
        v->ice = v->ice && v->kind == CC_VALUE_INTEGER;
        return true;
    case CC_BUILTIN_CLASSIFY_TYPE:
        v->i = cc_builtin_classify_type(c, AST_NODE(arg[0])->ty);
        return true;
    case CC_BUILTIN_STRLEN: {
        uint32_t s = arg[0];

        while (AST_NODE(s)->op == AST_NODE_CAST)
            s = AST_NODE(s)->a;
        if (AST_NODE(s)->op != AST_NODE_STRING)
            return false;
        v->i = (i128)strlen(c->strs[AST_NODE(s)->a].s);
        v->ice =
            true; /* clang: an integer constant expression (the kernel's __is_constexpr
				   picks __builtin_strlen over __fortify_strlen in static initializers);
				   the library strlen("...") only folds (const.c) */
        return true;
    }
    case CC_BUILTIN_HUGE_VAL:
    case CC_BUILTIN_HUGE_VALF:
    case CC_BUILTIN_INF:
    case CC_BUILTIN_INFF:
        v->kind = CC_VALUE_FLOAT, v->f = __builtin_inf(), v->ice = false;
        return true;
    case CC_BUILTIN_NAN:
    case CC_BUILTIN_NANF:
    case CC_BUILTIN_NANS: {
        uint32_t n = arg[0];
        while (AST_NODE(n)->op == AST_NODE_CAST)
            n = AST_NODE(n)->a;
        if (AST_NODE(n)->op != AST_NODE_STRING)
            return false;
        const char *text = c->strs[AST_NODE(n)->a].s;
        int p = AST_TYPE_KIND(c, x->ty) == AST_TYPE_FLOAT16  ? 10
                : AST_TYPE_KIND(c, x->ty) == AST_TYPE_FLOAT  ? 23
                : AST_TYPE_KIND(c, x->ty) == AST_TYPE_DOUBLE ? 52
                : c->o->target == PP_DARWIN                  ? 52
                : CC_X86_64(c)                               ? 63
                                                             : 112;
        if (!vsoftfloat_nan_payload(text, p, x->aux == CC_BUILTIN_NANS, &v->q))
            return false;
        v->kind = CC_VALUE_FLOAT, v->ld = true,
        v->f = vsoftfloat_to_double(v->q), v->ice = false;
        return true;
    }
    case CC_BUILTIN_ISNAN:
    case CC_BUILTIN_ISINF:
    case CC_BUILTIN_ISINF_SIGN:
    case CC_BUILTIN_ISFINITE:
    case CC_BUILTIN_ISNORMAL:
    case CC_BUILTIN_SIGNBIT: {
        if (cc_builtin_has_effects(c, arg[0], true) ||
            !cc_constant_eval(c, arg[0], &a) || a.kind != CC_VALUE_FLOAT)
            return false;
        u128 q = a.ld ? a.q : vsoftfloat_of_double(a.f);
        unsigned exponent = (unsigned)(q >> 112) & 0x7fff;
        int kind = AST_TYPE_KIND(c, AST_NODE(arg[0])->ty);
        unsigned minimum =
            kind == AST_TYPE_FLOAT16                              ? 16383 - 14
            : kind == AST_TYPE_FLOAT || kind == AST_TYPE_BFLOAT16 ? 16383 - 126
            : kind == AST_TYPE_DOUBLE || c->o->target == PP_DARWIN
                ? 16383 - 1022
                : 1;
        switch (x->aux) {
        case CC_BUILTIN_ISNAN:
            v->i = vsoftfloat_is_nan(q);
            break;
        case CC_BUILTIN_ISINF:
            v->i = vsoftfloat_is_infinity(q);
            break;
        case CC_BUILTIN_ISINF_SIGN:
            v->i = vsoftfloat_is_infinity(q) ? (q >> 127 ? -1 : 1) : 0;
            break;
        case CC_BUILTIN_ISFINITE:
            v->i = exponent != 0x7fff;
            break;
        case CC_BUILTIN_ISNORMAL:
            v->i = exponent >= minimum && exponent != 0x7fff;
            break;
        default:
            v->i = q >> 127;
            break;
        }
        v->ice = false;
        return true;
    }
    case CC_BUILTIN_ISGREATER:
    case CC_BUILTIN_ISGREATEREQUAL:
    case CC_BUILTIN_ISLESS:
    case CC_BUILTIN_ISLESSEQUAL:
    case CC_BUILTIN_ISLESSGREATER:
    case CC_BUILTIN_ISUNORDERED: {
        cc_value b;
        if (x->c != 2 || cc_builtin_has_effects(c, arg[0], true) ||
            cc_builtin_has_effects(c, arg[1], true) ||
            !cc_constant_eval(c, arg[0], &a) ||
            !cc_constant_eval(c, arg[1], &b) ||
            (a.kind != CC_VALUE_FLOAT && a.kind != CC_VALUE_INTEGER) ||
            (b.kind != CC_VALUE_FLOAT && b.kind != CC_VALUE_INTEGER))
            return false;
        int p = 0;
        for (unsigned i = 0; i < 2; i++)
            if (cc_type_is_float(c, AST_NODE(arg[i])->ty)) {
                int kind = AST_TYPE_KIND(c, AST_NODE(arg[i])->ty);
                int bits =
                    kind == AST_TYPE_BFLOAT16  ? 7
                    : kind == AST_TYPE_FLOAT16 ? 10
                    : kind == AST_TYPE_FLOAT   ? 23
                    : kind == AST_TYPE_DOUBLE || c->o->target == PP_DARWIN
                        ? 52
                        : cc_float_precision_bits(c, AST_NODE(arg[i])->ty);
                if (bits > p)
                    p = bits;
            }
        if (!p)
            return false;
        u128 l = a.kind == CC_VALUE_FLOAT
                     ? (a.ld ? a.q : vsoftfloat_of_double(a.f))
                     : vsoftfloat_of_integer_with_precision(
                           a.i, cc_type_is_signed(c, AST_NODE(arg[0])->ty), p);
        u128 r = b.kind == CC_VALUE_FLOAT
                     ? (b.ld ? b.q : vsoftfloat_of_double(b.f))
                     : vsoftfloat_of_integer_with_precision(
                           b.i, cc_type_is_signed(c, AST_NODE(arg[1])->ty), p);
        int order = vsoftfloat_compare(l, r);
        switch (x->aux) {
        case CC_BUILTIN_ISGREATER:
            v->i = order == 1;
            break;
        case CC_BUILTIN_ISGREATEREQUAL:
            v->i = order == 1 || order == 0;
            break;
        case CC_BUILTIN_ISLESS:
            v->i = order == -1;
            break;
        case CC_BUILTIN_ISLESSEQUAL:
            v->i = order == -1 || order == 0;
            break;
        case CC_BUILTIN_ISLESSGREATER:
            v->i = order == -1 || order == 1;
            break;
        default:
            v->i = order == 2;
            break;
        }
        v->ice = false;
        return true;
    }
    case CC_BUILTIN_ISSIGNALING:
        if (!cc_constant_eval(c, arg[0], &a) || a.kind != CC_VALUE_FLOAT)
            return false;
        v->i = a.ld ? vsoftfloat_is_nan(a.q) && !(a.q & ((u128)1 << 111)) : 0;
        v->ice = false;
        return true;
    case CC_BUILTIN_CLZ:
    case CC_BUILTIN_CLZL:
    case CC_BUILTIN_CLZLL:
    case CC_BUILTIN_CTZ:
    case CC_BUILTIN_CTZL:
    case CC_BUILTIN_CTZLL:
    case CC_BUILTIN_FFS:
    case CC_BUILTIN_FFSL:
    case CC_BUILTIN_FFSLL:
    case CC_BUILTIN_POPCOUNT:
    case CC_BUILTIN_POPCOUNTL:
    case CC_BUILTIN_POPCOUNTLL:
    case CC_BUILTIN_PARITY:
    case CC_BUILTIN_PARITYL:
    case CC_BUILTIN_PARITYLL:
    case CC_BUILTIN_BSWAP16:
    case CC_BUILTIN_BSWAP32:
    case CC_BUILTIN_BSWAP64:
    case CC_BUILTIN_BITREVERSE8:
    case CC_BUILTIN_BITREVERSE16:
    case CC_BUILTIN_BITREVERSE32:
    case CC_BUILTIN_BITREVERSE64:
    case CC_BUILTIN_ROTL8:
    case CC_BUILTIN_ROTL16:
    case CC_BUILTIN_ROTL32:
    case CC_BUILTIN_ROTL64:
    case CC_BUILTIN_ROTR8:
    case CC_BUILTIN_ROTR16:
    case CC_BUILTIN_ROTR32:
    case CC_BUILTIN_ROTR64:
    case CC_BUILTIN_CLRSB:
    case CC_BUILTIN_CLRSBL:
    case CC_BUILTIN_CLRSBLL: {
        ast_qualified_type at;
        unsigned bits;
        u128 u, r = 0;
        int operation = cc_builtin_integer_operation(x->aux);

        if (!cc_constant_eval(c, arg[0], &a) || a.kind != CC_VALUE_INTEGER)
            return false;
        at = AST_NODE(arg[0])->ty;
        bits = (unsigned)cc_type_size(c, at) * 8;
        u = (u128)cc_constant_truncate(c, a.i, cc_type_unsigned(c, at));
        v->ice = a.ice;
        if (operation == vfold_rotate_left || operation == vfold_rotate_right) {
            cc_value count;

            if (x->c != 2 || !cc_constant_eval(c, arg[1], &count) ||
                count.kind != CC_VALUE_INTEGER)
                return false;
            r = vfold_rotate(u, bits, (u128)count.i,
                             operation == vfold_rotate_right);
            v->ice = v->ice && count.ice;
        } else if (!vfold_integer(operation, u, bits, &r)) {
            return false; /* clz and ctz of 0 are undefined */
        }
        v->i = cc_constant_truncate(c, (i128)r, x->ty);
        return true;
    }
    default:
        return false;
    }
}
