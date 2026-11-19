/*
 * type-claims.h: -fcxx-dump=type-claims (doc/PLAN-cxx-sema.md, C4): for every
 * expression analysis gave a type, that type and value category, in the
 * encoding of the declarator claims (declarator-claims.h), which
 * bench/cxx-parse.py's PRELUDE defines:
 *
 *     ::vcc_claim::pointer_to<::vcc_claim::const_<int>>
 *
 * bench/cxx-sema.py has clang compile, just after the expression's
 * declaration,
 *
 *     static_assert(__is_same(decltype((E)), CLAIM));
 *
 * where [dcl.type.decltype]/1 makes decltype((E)) the type T of E for a
 * prvalue, T& for an lvalue and T&& for an xvalue: the claim says both.
 *
 * A line per expression:
 *
 *     FILE <tab> LINE:COLUMN <tab> EXPRESSION <tab> CLAIM [<tab> VALUE]
 *
 * with LINE:COLUMN just after the declaration, as
 * -fcxx-dump=expression-claims writes it, and from C++11 on the value of a constant
 * prvalue of integral, float or double type (an integer literal, or a
 * hexadecimal floating literal), which bench/cxx-sema.py checks as
 *
 *     static_assert((E) == VALUE);
 *
 * The encoding is checked by -fcxx-dump=selftest.  Included by
 * analysis.cpp inside namespace cplusplus.
 */
#ifndef VCC_CPLUSPLUS_DUMP_TYPE_CLAIMS_H
#define VCC_CPLUSPLUS_DUMP_TYPE_CLAIMS_H

/*
 * Appends the encoding of type t to out; false (and out unchanged) when
 * the type has no encoding yet: a class or an enumeration (its name needs
 * the scope it is declared in, S1), a bit-precise integer (its width is in
 * the token, not the type).
 */
static bool write_type_claim(const types &table, qualified_type t,
                             vbuffer *out);

static bool write_type_claim_unqualified(const types &table, type_index t,
                                         vbuffer *out)
{
    const type &record = table[t];
    switch (record.kind) {
    case type_kind::fundamental:
        if (record.fundamental == fundamental_type::bit_precise_int ||
            record.fundamental == fundamental_type::unsigned_bit_precise_int ||
            record.fundamental == fundamental_type::none)
            return false;
        vbuffer_string_append(out, fundamental_type_name(record.fundamental));
        return true;
    case type_kind::pointer:
        vbuffer_string_append(out, "::vcc_claim::pointer_to<");
        break;
    case type_kind::lvalue_reference:
        vbuffer_string_append(out, "::vcc_claim::lvalue_reference_to<");
        break;
    case type_kind::rvalue_reference:
        vbuffer_string_append(out, "::vcc_claim::rvalue_reference_to<");
        break;
    case type_kind::array:
        if (record.bound_known)
            vbuffer_string_append(out, "::vcc_claim::array_of<");
        else
            vbuffer_string_append(out, "::vcc_claim::array_of_unknown_bound<");
        break;
    case type_kind::function:
        vbuffer_string_append(out, "::vcc_claim::function<");
        break;
    case type_kind::none:
    case type_kind::class_type:
    case type_kind::enumeration:
    case type_kind::pointer_to_member:
    case type_kind::template_parameter:
    case type_kind::dependent:
    case type_kind::placeholder:
    case type_kind::pack_expansion:
        return false;
    }
    if (!write_type_claim(table, record.operand, out))
        return false;
    if (record.kind == type_kind::array && record.bound_known)
        vbuffer_format_append(out, ", (%llu)",
                              (unsigned long long)record.bound);
    if (record.kind == type_kind::function) {
        vbuffer_string_append(out, "(");
        for (uint32_t i = 0; i < record.parameter_count; i++) {
            if (i)
                vbuffer_string_append(out, ", ");
            if (!write_type_claim(table, table.parameters(t)[i], out))
                return false;
        }
        if (record.variadic && record.parameter_count)
            vbuffer_string_append(out, ", ...");
        else if (record.variadic)
            vbuffer_string_append(out, "...");
        vbuffer_string_append(out, ")");
        if (record.non_throwing)
            vbuffer_string_append(out, " noexcept");
    }
    vbuffer_string_append(out, ">");
    return true;
}

static bool write_type_claim(const types &table, qualified_type t, vbuffer *out)
{
    size_t size = out->n;
    unsigned wrappers = 0;
    if (t.cv() & cv_const) {
        vbuffer_string_append(out, "::vcc_claim::const_<");
        wrappers++;
    }
    if (t.cv() & cv_volatile) {
        vbuffer_string_append(out, "::vcc_claim::volatile_<");
        wrappers++;
    }
    if (t.cv() & cv_restrict) {
        vbuffer_string_append(out, "::vcc_claim::restrict_<");
        wrappers++;
    }
    if (!write_type_claim_unqualified(table, t.unqualified(), out)) {
        out->n = size;
        return false;
    }
    for (unsigned i = 0; i < wrappers; i++)
        vbuffer_string_append(out, ">");
    return true;
}

/* the claim for an expression of type t and category c: what
   decltype((E)) is ([dcl.type.decltype]/1.5) */
static bool write_expression_claim(const types &table, qualified_type t,
                                   value_category c, vbuffer *out)
{
    size_t size = out->n;
    if (c == value_category::lvalue)
        vbuffer_string_append(out, "::vcc_claim::lvalue_reference_to<");
    else if (c == value_category::xvalue)
        vbuffer_string_append(out, "::vcc_claim::rvalue_reference_to<");
    if (!write_type_claim(table, t, out)) {
        out->n = size;
        return false;
    }
    if (c != value_category::prvalue)
        vbuffer_string_append(out, ">");
    return true;
}

#endif
