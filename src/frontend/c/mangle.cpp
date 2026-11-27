/* vcc front end: overloadable: Itanium mangling as clang does it for C */
#include "common/literal.h"
#include "cc-internal.h"
#include "frontend/itanium/writer.h"

/* ---------------- overloadable: Itanium mangling as clang does it for C ---------------- */

/* the writer of the C++ front end's mangler (frontend/itanium/writer.h),
   with the substitution candidates of a C function's parameter types:
   canonical (qualified) types, in order ([abi:mangling-compression]) */
struct cc_mangler {
    itanium::writer out;
    ast_qualified_type sub[64];
    uint32_t nsub = 0;

    explicit cc_mangler(vbuffer *b) : out(b)
    {
    }
};

static void cc_mangle_source_name(cc_mangler *m, uint32_t id)
{
    int l;
    const char *s = cc_identifier_name(id, &l);

    m->out.source_name(s, (size_t)l);
}

/* <substitution> for a type seen before; false if t is new */
static bool cc_mangle_substitution_find(cc_mangler *m, ast_qualified_type t)
{
    for (uint32_t i = 0; i < m->nsub; i++)
        if (m->sub[i] == t) {
            m->out.substitution(i);
            return true;
        }
    return false;
}

static void cc_mangle_substitution_add(cc_mangler *m, ast_qualified_type t)
{
    if (m->nsub < 64)
        m->sub[m->nsub++] = t;
}

/* [abi:mangling-builtin]'s row of a basic type; none for another */
static itanium::builtin cc_mangle_builtin(int k)
{
    switch (k) {
    case AST_TYPE_VOID:
        return itanium::builtin::void_;
    case AST_TYPE_BOOL:
        return itanium::builtin::bool_;
    case AST_TYPE_CHAR:
        return itanium::builtin::char_;
    case AST_TYPE_SIGNED_CHAR:
        return itanium::builtin::signed_char;
    case AST_TYPE_UNSIGNED_CHAR:
        return itanium::builtin::unsigned_char;
    case AST_TYPE_SHORT:
        return itanium::builtin::short_int;
    case AST_TYPE_UNSIGNED_SHORT:
        return itanium::builtin::unsigned_short_int;
    case AST_TYPE_INT:
        return itanium::builtin::int_;
    case AST_TYPE_UNSIGNED_INT:
        return itanium::builtin::unsigned_int;
    case AST_TYPE_LONG:
        return itanium::builtin::long_int;
    case AST_TYPE_UNSIGNED_LONG:
        return itanium::builtin::unsigned_long_int;
    case AST_TYPE_LONG_LONG:
        return itanium::builtin::long_long_int;
    case AST_TYPE_UNSIGNED_LONG_LONG:
        return itanium::builtin::unsigned_long_long_int;
    case AST_TYPE_INT128:
        return itanium::builtin::int128;
    case AST_TYPE_UINT128:
        return itanium::builtin::unsigned_int128;
    case AST_TYPE_BFLOAT16:
        return itanium::builtin::bfloat16;
    case AST_TYPE_FLOAT16:
        return itanium::builtin::float16;
    case AST_TYPE_FLOAT:
        return itanium::builtin::float_;
    case AST_TYPE_DOUBLE:
        return itanium::builtin::double_;
    case AST_TYPE_LONG_DOUBLE:
        return itanium::builtin::long_double;
    case AST_TYPE_FLOAT128:
        return itanium::builtin::float128;
    default:
        return itanium::builtin::none;
    }
}

/* [abi:mangling-type] of a C type */
static void cc_mangle_type(cc *c, cc_mangler *m, ast_qualified_type t)
{
    using itanium::terminal;
    int q = AST_QUALIFIED_TYPE_QUALIFIERS(t), k;
    const ctype *x;

    t = canon(c, t); /* keeps the qualifiers */
    k = AST_TYPE_KIND(c, t);
    if (!q && k < AST_TYPE_POINTER) {
        m->out.write(cc_mangle_builtin(k));
        return;
    }
    if (cc_mangle_substitution_find(m, t))
        return;
    if (q) {
        /* <qualified-type>, <CV-qualifiers> ::= [r] [V] [K] */
        if (q & AST_QUALIFIER_RESTRICT)
            m->out.write(terminal::restrict_qualifier);
        if (q & AST_QUALIFIER_VOLATILE)
            m->out.write(terminal::volatile_qualifier);
        if (q & AST_QUALIFIER_CONST)
            m->out.write(terminal::const_qualifier);
        cc_mangle_type(c, m, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
        cc_mangle_substitution_add(m, t);
        return;
    }
    x = AST_TYPE(c, t);
    switch (k) {
    case AST_TYPE_POINTER:
        m->out.write(terminal::pointer);
        cc_mangle_type(c, m, x->base);
        break;
    case AST_TYPE_ARRAY:
        m->out.write(terminal::array_type);
        if (!(x->flags & (AST_TYPE_FLAG_INCOMPLETE | AST_TYPE_FLAG_VLA)))
            m->out.number(x->n);
        m->out.write(terminal::underscore);
        cc_mangle_type(c, m, x->base);
        break;
    case AST_TYPE_FUNCTION:
        m->out.write(terminal::function_type);
        cc_mangle_type(c, m, x->base);
        if (!x->n && !(x->flags & AST_TYPE_FLAG_VARIADIC))
            m->out.write(itanium::builtin::void_);
        for (uint32_t i = 0; i < x->n; i++)
            cc_mangle_type(
                c, m, AST_QUALIFIED_TYPE_UNQUALIFIED(c->params[x->aux + i]));
        if (x->flags & AST_TYPE_FLAG_VARIADIC)
            m->out.write(terminal::ellipsis);
        m->out.write(terminal::end);
        break;
    default: { /* struct, union, enum */
        const tag *r = &c->tags[x->aux];
        uint32_t id = r->name ? r->name : r->typedef_name;

        if (id) {
            cc_mangle_source_name(m, id);
        } else {
            m->out.write(terminal::unnamed_type);
            m->out.write(terminal::underscore);
        }
        break;
    }
    }
    cc_mangle_substitution_add(m, t);
}

/* _Z[L]<name><params> as clang mangles an overloadable C function; 0 if it does not fit */
static uint32_t cc_mangle_overload(cc *c, uint32_t s)
{
    using itanium::terminal;
    const sym *y = &c->syms[s];
    const ctype *f = AST_TYPE(c, y->ty);
    char storage[512];
    vbuffer b;
    uint32_t si;
    char *keep;

    vbuffer_fixed(&b, storage, sizeof(storage));
    b.flags |= VBUFFER_STICKY;
    cc_mangler m(&b);
    m.out.write(terminal::mangled_name);
    if (y->sc == AST_STORAGE_STATIC)
        m.out.write(terminal::internal_linkage);
    cc_mangle_source_name(&m, y->name);
    if (!f->n && !(f->flags & AST_TYPE_FLAG_VARIADIC))
        m.out.write(itanium::builtin::void_);
    for (uint32_t i = 0; i < f->n; i++)
        cc_mangle_type(c, &m,
                       AST_QUALIFIED_TYPE_UNQUALIFIED(c->params[f->aux + i]));
    if (f->flags & AST_TYPE_FLAG_VARIADIC)
        m.out.write(terminal::ellipsis);
    if (b.failed)
        return 0;
    keep = (char *)cc_heap_allocate(c, b.n + 1);
    memcpy(keep, b.p, b.n);
    keep[b.n] = 0;
    si = c->nstr++;
    c->strs[si] =
        VCC_LITERAL(cstr, keep, (uint32_t)b.n + 1, 1, LEX_TOKEN_STRING);
    return si + 1;
}

/* give an overloadable function its mangled name (unless it has an asm label) */
CC_PRIVATE void cc_mangle_overload_name_set(cc *c, uint32_t s)
{
    sym *y = &c->syms[s];
    attrs a;

    if (y->attrs && c->attr[y->attrs].asmname)
        return;
    memset(&a, 0, sizeof(a));
    a.asmname = cc_mangle_overload(c, s);
    if (a.asmname)
        cc_attribute_apply_symbol(c, y, &a);
}
