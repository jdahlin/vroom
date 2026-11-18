/*
 * analysis.cpp: the developer outputs of semantic analysis (dump.h):
 * analysis, unsupported, declaration-claims, type-claims, lookup-claims,
 * check-tree, tree, symbols, layout-claims and stats, which
 * bench/cxx-sema.py checks with clang; and the selftest of the tables
 * analysis builds on.
 */
#include <math.h>
#include <string.h>

#include "../cplusplus-internal.h"
#include "dump.h"

namespace cplusplus
{

#include "type-claims.h"
#include "tree-check.h"

static bool analysis_check(vbuffer *out, bool condition, const char *what)
{
    if (!condition)
        vbuffer_format_append(out, "selftest: FAILED: %s\n", what);
    return condition;
}

/* the type and entity tables have no rule that uses them before step S1:
   what they promise is checked here */
bool dump_analysis_selftest(vbuffer *out)
{
    bool ok = true;
    {
        types t;
        type_index i = t.fundamental(fundamental_type::int_);
        type_index c = t.fundamental(fundamental_type::char_);
        ok &=
            analysis_check(out, i && c && i != c, "distinct fundamental types");
        ok &= analysis_check(out, t.fundamental(fundamental_type::int_) == i,
                             "a type made again is the same type");
        qualified_type plain = qualified_type::make(i, cv_none);
        qualified_type constant = qualified_type::make(i, cv_const);
        ok &= analysis_check(
            out,
            plain.unqualified() == constant.unqualified() &&
                !(plain == constant) && constant.cv() == cv_const,
            "[basic.type.qualifier]/1: cv-qualified versions are "
            "distinct types of one record");
        type_index p = t.pointer_to(plain), pc = t.pointer_to(constant);
        ok &= analysis_check(out, p != pc && t.pointer_to(plain) == p,
                             "int* and const int* differ");
        ok &= analysis_check(
            out, t.lvalue_reference_to(plain) != t.rvalue_reference_to(plain),
            "[dcl.ref]/1: the two kinds of reference");
        ok &= analysis_check(out,
                             t.array_of(plain, 3) != t.array_of(plain, 4) &&
                                 t.array_of(plain, 3) == t.array_of(plain, 3) &&
                                 t.array_of_unknown_bound_of(plain) !=
                                     t.array_of(plain, 0),
                             "[dcl.array]/1: arrays by bound");
        qualified_type two[] = {plain, qualified_type::make(c, cv_none)};
        type_index f = t.function(plain, two, 2, false);
        ok &= analysis_check(
            out,
            t.function(plain, two, 2, false) == f &&
                t.function(plain, two, 1, false) != f &&
                t.function(plain, two, 2, true) != f &&
                t.function(constant, two, 2, false) != f,
            "[dcl.fct]/1: functions by parameters and return type");
        ok &= analysis_check(
            out, t[f].parameter_count == 2 && t.parameters(f)[1] == two[1],
            "a function type's parameters");
        ok &= analysis_check(out,
                             t.of_entity(type_kind::class_type, 1) !=
                                     t.of_entity(type_kind::class_type, 2) &&
                                 t.of_entity(type_kind::class_type, 1) !=
                                     t.of_entity(type_kind::enumeration, 1),
                             "one type per class and enumeration");
        ok &= analysis_check(
            out, t.pointer_to_member(1, plain) != t.pointer_to_member(2, plain),
            "[dcl.mptr]/1: pointers to members by class");
        /* enough types for the table to grow several times */
        size_t before = t.size();
        qualified_type chain = plain;
        for (int k = 0; k < 1000; k++)
            chain = qualified_type::make(t.pointer_to(chain), cv_none);
        ok &= analysis_check(out, t.size() == before + 999,
                             "p was the first of the chain");
        qualified_type again = plain;
        for (int k = 0; k < 1000; k++)
            again = qualified_type::make(t.pointer_to(again), cv_none);
        ok &= analysis_check(out, again == chain && t.size() == before + 999,
                             "types are found again after the table grew");
    }
    {
        entities e;
        entity_index n = e.make(entity_kind::namespace_entity, 7, 0);
        entity_index x = e.make(entity_kind::variable, 8, n);
        ok &=
            analysis_check(out, n && x && e[x].enclosing == n && e[x].name == 8,
                           "an entity and its enclosing entity");
        e.add_declaration(x, 11);
        e.add_declaration(n, 5);
        e.add_declaration(x, 12);
        uint32_t first = e[x].first_declaration;
        ok &= analysis_check(
            out,
            e.declaration(first).node == 11 &&
                e.declaration(e.declaration(first).next).node == 12 &&
                e[x].last_declaration == e.declaration(first).next &&
                !e.declaration(e[x].last_declaration).next &&
                e.declaration(e[n].first_declaration).node == 5,
            "[basic.link]/1: an entity's declarations in order");
    }
    {
        /* the claims' encoding: what bench/cxx-sema.py has clang check */
        types t;
        qualified_type i = qualified_type::make(
            t.fundamental(fundamental_type::int_), cv_none);
        qualified_type c = qualified_type::make(
            t.fundamental(fundamental_type::char_), cv_none);
        qualified_type ci = qualified_type::make(i.unqualified(), cv_const);
        qualified_type pci =
            qualified_type::make(t.pointer_to(ci), cv_const | cv_volatile);
        qualified_type a3 = qualified_type::make(t.array_of(pci, 3), cv_none);
        qualified_type parameters[] = {c, i};
        qualified_type f = qualified_type::make(
            t.function(qualified_type::make(t.pointer_to(a3), cv_none),
                       parameters, 2, true),
            cv_none);
        qualified_type g =
            qualified_type::make(t.function(i, nullptr, 0, true), cv_none);
        qualified_type s = qualified_type::make(
            t.of_entity(type_kind::class_type, 1), cv_none);
        owned_buffer encoded;
        auto claim = [&](qualified_type type, value_category category,
                         const char *expected) {
            encoded.clear();
            bool written =
                write_expression_claim(t, type, category, encoded.get());
            if (!expected)
                return analysis_check(
                    out, !written && !encoded.size(),
                    "a type without an encoding writes nothing");
            return analysis_check(
                out,
                written && encoded.size() == strlen(expected) &&
                    !memcmp(encoded.data(), expected, encoded.size()),
                expected);
        };
        ok &= claim(i, value_category::prvalue, "int");
        ok &= claim(i, value_category::lvalue,
                    "::vcc_claim::lvalue_reference_to<int>");
        ok &= claim(ci, value_category::xvalue,
                    "::vcc_claim::rvalue_reference_to<::vcc_claim::const_<"
                    "int>>");
        ok &= claim(f, value_category::lvalue,
                    "::vcc_claim::lvalue_reference_to<::vcc_claim::function<"
                    "::vcc_claim::pointer_to<::vcc_claim::array_of<::vcc_"
                    "claim::const_<::vcc_claim::volatile_<::vcc_claim::"
                    "pointer_to<::vcc_claim::const_<int>>>>, (3)>>(char, int, "
                    "...)>>");
        ok &= claim(g, value_category::prvalue,
                    "::vcc_claim::function<int(...)>");
        ok &= claim(qualified_type::make(t.pointer_to(s), cv_none),
                    value_category::prvalue, nullptr);
    }
    return ok;
}

/*
 * declaration-claims: per declared name
 *
 *     FILE <tab> LINE-LINE <tab> LINE:COLUMN <tab> KIND <tab> NAME <tab> CLAIM
 *
 * as declarator-claims writes it (declarator-claims.h), so that
 * bench/cxx-parse.py's check runs on it: LINE-LINE the declaration's lines
 * (a function-definition's up to its body), LINE:COLUMN just after the
 * declaration, KIND type, function or object ([] appended for an array of
 * unknown bound).  A declaration with a token a macro made is left out: its
 * tokens are not at one place in the file.
 */
static void dump_declaration_claims(const semantic_analysis &analysis,
                                    const syntax_tree &tree,
                                    token_stream &tokens, vbuffer *out)
{
    owned_buffer claim;
    for (const declared_name &d : analysis.declared) {
        if (!d.claimable)
            continue;
        /* [ext:gnu_attributes] noreturn is part of the function's type for
           clang (void () __attribute__((noreturn))), which the types here
           do not say */
        if (d.entity && (analysis.entity_table[d.entity].gnu_flags &
                         (gnu_noreturn | gnu_nothrow)))
            continue;
        const syntax_node &node = tree[d.declaration];
        bool from_macro = false;
        for (token_index t = node.first_token; t < node.end_token; t++)
            if (tokens[t].preprocessing.expansion)
                from_macro = true;
        claim.clear();
        if (from_macro ||
            !write_type_claim(analysis.type_table, d.type, claim.get()))
            continue;
        token_index last = node.end_token - 1;
        for (uint32_t i = 0; i < node.child_count; i++) {
            syntax_child c = tree.children(d.declaration)[i];
            if (syntax_is(tree, c, syntax_kind::function_body))
                last = tree[c.index()].first_token - 1;
        }
        source_position end = tokens.position(node.end_token - 1);
        uint32_t extent = tokens[node.end_token - 1].preprocessing.extent;
        vbuffer_format_append(out, "%s\t%u-%u\t%u:%u\t", end.file_name,
                              tokens.position(node.first_token).line,
                              tokens.position(last).line, end.line,
                              end.column + extent);
        const type &record = analysis.type_table[d.type.unqualified()];
        if (d.kind == entity_kind::typedef_name)
            vbuffer_string_append(out, "type");
        else if (d.kind == entity_kind::function)
            vbuffer_string_append(out, "function");
        else
            vbuffer_string_append(out, "object");
        if (record.kind == type_kind::array && !record.bound_known)
            vbuffer_string_append(out, "[]");
        vbuffer_string_append(out, "\t");
        tokens.spelling(d.id, out);
        vbuffer_string_append(out, "\t");
        vbuffer_append(out, claim.data(), claim.size());
        vbuffer_string_append(out, "\n");
    }
}

/*
 * The value of a constant prvalue as a C++ literal clang compares it with:
 * an integer as unsigned long long or long long, a float or double as a
 * hexadecimal floating literal; false when there is none (a long double, an
 * infinity or a NaN, a pointer).
 */
static bool dump_value_claim(const semantic_analysis &analysis,
                             const implementation_defined_behavior &b,
                             const expression_meaning &m, vbuffer *out)
{
    const type &record = analysis.type_table[m.type.unqualified()];
    if (m.category != value_category::prvalue ||
        m.evaluation != constness::constant ||
        record.kind != type_kind::fundamental)
        return false;
    fundamental_type t = record.fundamental;
    if (fundamental_type_is_integral(t)) {
        __int128 v = integer_value(b, t, m.value);
        if (v < 0 && v == -(__int128(1) << 63))
            vbuffer_string_append(out, "(-9223372036854775807LL - 1)");
        else if (v < 0)
            vbuffer_format_append(out, "(-%lluLL)",
                                  static_cast<unsigned long long>(-v));
        else
            vbuffer_format_append(out, "%lluULL",
                                  static_cast<unsigned long long>(v));
        return true;
    }
    if (t != fundamental_type::float_ && t != fundamental_type::double_)
        return false;
    double d = floating_value(m.value);
    if (!isfinite(d))
        return false;
    vbuffer_format_append(out, "%a", d);
    return true;
}

/*
 * type-claims: a line per expression analysis gave a type, in a
 * declaration that is well-formed or unsupported (type-claims.h); from
 * C++11 on, a fifth field with the value of a constant prvalue, which
 * bench/cxx-sema.py checks as static_assert((E) == VALUE).  An expression
 * with a token a macro made, or one spelled over lines, is left out.
 */
static void dump_type_claims(const semantic_analysis &analysis,
                             const syntax_tree &tree, token_stream &tokens,
                             vbuffer *out)
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    bool values = b.has(language_feature::list_initialization);
    owned_buffer claim, spelling;
    size_t j = 0;
    for (const analysed_expression &x : analysis.expressions) {
        while (j < analysis.declarations.size() &&
               analysis.declarations[j].node != x.declaration)
            j++;
        if (j == analysis.declarations.size())
            break;
        analysis_result result = analysis.declarations[j].result;
        if (result != analysis_result::well_formed &&
            result != analysis_result::unsupported)
            continue;
        token_index first = x.at.index(), end = x.at.index() + 1;
        if (!x.at.is_token()) {
            first = tree[x.at.index()].first_token;
            end = tree[x.at.index()].end_token;
        }
        bool from_macro = false;
        spelling.clear();
        for (token_index t = first; t < end; t++) {
            if (tokens[t].preprocessing.expansion)
                from_macro = true;
            if (t > first)
                vbuffer_string_append(spelling.get(), " ");
            tokens.spelling(t, spelling.get());
        }
        if (from_macro || memchr(spelling.data(), '\n', spelling.size()) ||
            memchr(spelling.data(), '\t', spelling.size()))
            continue;
        /* a noreturn function's type (write_declaration_claims) */
        if (x.meaning.entity &&
            (analysis.entity_table[x.meaning.entity].gnu_flags &
             (gnu_noreturn | gnu_nothrow)))
            continue;
        claim.clear();
        if (!write_expression_claim(analysis.type_table, x.meaning.type,
                                    x.meaning.category, claim.get()))
            continue;
        if (!x.placeable)
            continue;
        /* just after the statement of a block the expression is in, or
           after its declaration */
        token_index after = tree[x.declaration].end_token - 1;
        if (x.place)
            after = x.place;
        if (tokens[after].preprocessing.expansion)
            continue;
        source_position at = tokens.position(after);
        uint32_t extent = tokens[after].preprocessing.extent;
        vbuffer_format_append(out, "%s\t%u:%u\t", at.file_name, at.line,
                              at.column + extent);
        vbuffer_append(out, spelling.data(), spelling.size());
        vbuffer_string_append(out, "\t");
        vbuffer_append(out, claim.data(), claim.size());
        if (values) {
            claim.clear();
            if (dump_value_claim(analysis, b, x.meaning, claim.get())) {
                vbuffer_string_append(out, "\t");
                vbuffer_append(out, claim.data(), claim.size());
            }
        }
        vbuffer_string_append(out, "\n");
    }
}

/*
 * symbols: a line per function or variable a well-formed declaration
 * declared that has a symbol, once per entity:
 *
 *     SYMBOL <tab> defined|inline|declared <tab> external|internal|none
 *
 * (itanium_symbol_name); bench/cxx-sema.py has clang -O0 compile a file
 * it accepts and checks each defined one against its object's symbols.
 * An entity whose symbol needs rules not written yet is "unsupported".
 */
static void dump_symbols(const semantic_analysis &analysis,
                         token_stream &tokens, vbuffer *out)
{
    owned_array<entity_index> seen;
    owned_buffer symbol;
    for (const declared_name &d : analysis.declared) {
        if (!d.entity)
            continue;
        const entity &e = analysis.entity_table[d.entity];
        if (e.kind != entity_kind::function && e.kind != entity_kind::variable)
            continue;
        if (e.kind == entity_kind::variable && e.automatic)
            continue;
        bool well_formed = false;
        for (const analysed_declaration &a : analysis.declarations)
            if (a.node == d.outermost)
                well_formed = a.result == analysis_result::well_formed;
        bool again = false;
        for (entity_index s : seen)
            if (s == d.entity)
                again = true;
        if (!well_formed || again)
            continue;
        seen.append(d.entity);
        symbol.clear();
        if (!itanium_symbol_name(analysis, tokens.pp, d.entity, symbol.get())) {
            vbuffer_string_append(out, "unsupported\n");
            continue;
        }
        vbuffer_append(out, symbol.data(), symbol.size());
        const char *linkage = "none";
        if (e.linkage_of_name == linkage::external)
            linkage = "external";
        else if (e.linkage_of_name == linkage::internal)
            linkage = "internal";
        const char *defined = "declared";
        if (e.defined)
            defined = "defined";
        /* emitted where it is odr-used ([basic.def.odr]/11) */
        if (e.defined && e.inline_)
            defined = "inline";
        /* [temp.explicit]/10 and /11: emitted, or not here */
        instantiation_kind explicit_ =
            analysis.explicitly_instantiated(d.entity);
        if (e.defined && explicit_ == instantiation_kind::definition)
            defined = "defined";
        if (explicit_ == instantiation_kind::declaration)
            defined = "declared";
        vbuffer_format_append(out, "\t%s\t%s\n", defined, linkage);
    }
}

/*
 * lookup-claims: a line per name whose lookup analysis resolved
 * (semantic_analysis::name_uses), with the declaration it found:
 *
 *     FILE <tab> LINE:COLUMN <tab> NAME <tab> KIND <tab> FILE:LINE:COLUMN
 *
 * the place of the name, its spelling, the kind of entity found, and the
 * place of the name in that entity's first declaration.  bench/cxx-sema.py
 * compares each with the declaration clang's AST says the name at that
 * place refers to (the first of its redeclarations).  A use or a
 * declaration with a token a macro made is left out, and so is an entity
 * without a declared name (a namespace, a built-in function).
 */
static void dump_lookup_claims(const semantic_analysis &analysis,
                               token_stream &tokens, vbuffer *out)
{
    /* each entity's first declared name, in the order they were declared */
    owned_array<token_index> first;
    first.resize(analysis.entity_table.size());
    for (size_t e = 0; e < analysis.entity_table.size(); e++)
        first[e] = 0;
    /* only a name of the entity's first declaration: one declaration
       analysis records no declared_name for (a member's, in its class)
       leaves the entity's claims out rather than naming a later one */
    for (const declared_name &d : analysis.declared) {
        if (!d.entity || !d.id || first[d.entity])
            continue;
        uint32_t f = analysis.entity_table[d.entity].first_declaration;
        if (f && analysis.entity_table.declaration(f).node != d.declaration)
            continue;
        first[d.entity] = d.id;
    }
    owned_buffer spelling;
    for (const semantic_analysis::name_use &u : analysis.name_uses) {
        if (!u.entity || u.entity >= analysis.entity_table.size())
            continue;
        token_index declared_at = first[u.entity];
        if (!declared_at || tokens[u.at].preprocessing.expansion ||
            tokens[declared_at].preprocessing.expansion)
            continue;
        const char *kind = "other";
        switch (analysis.entity_table[u.entity].kind) {
        case entity_kind::variable:
            kind = "variable";
            break;
        case entity_kind::function:
            kind = "function";
            break;
        case entity_kind::enumerator:
            kind = "enumerator";
            break;
        case entity_kind::class_type:
            kind = "class";
            break;
        case entity_kind::enumeration:
            kind = "enumeration";
            break;
        case entity_kind::typedef_name:
            kind = "typedef";
            break;
        default:
            break;
        }
        source_position at = tokens.position(u.at);
        source_position declared = tokens.position(declared_at);
        spelling.clear();
        tokens.spelling(u.at, spelling.get());
        vbuffer_format_append(out, "%s\t%u:%u\t", at.file_name, at.line,
                              at.column);
        vbuffer_append(out, spelling.data(), spelling.size());
        vbuffer_format_append(out, "\t%s\t%s:%u:%u\n", kind, declared.file_name,
                              declared.line, declared.column);
    }
}

/*
 * A class's name as clang -fdump-record-layouts writes it: its enclosing
 * namespaces and classes, an unnamed namespace as (anonymous namespace),
 * an inline namespace left out, an unnamed class as (unnamed at
 * LINE:COLUMN), the place of its class-key (clang has the file there too,
 * which bench/cxx-sema.py takes out).  A class of block scope is its name
 * alone; false for a class nested in one (clang names the function).
 */
static bool layout_class_name(const semantic_analysis &analysis,
                              token_stream &tokens, entity_index c, bool nested,
                              vbuffer *out)
{
    const entity &x = analysis.entity_table[c];
    entity_index scope = x.enclosing;
    if (scope) {
        const entity &s = analysis.entity_table[scope];
        if (s.kind == entity_kind::block_scope ||
            s.kind == entity_kind::function) {
            if (nested)
                return false;
        } else if (s.kind == entity_kind::namespace_entity) {
            if (!layout_class_name(analysis, tokens, scope, true, out))
                return false;
        } else if (!layout_class_name(analysis, tokens, scope, true, out)) {
            return false;
        }
    }
    if (x.kind == entity_kind::namespace_entity) {
        if (x.inline_ && x.name)
            return true;
        if (!x.name) {
            vbuffer_string_append(out, "(anonymous namespace)::");
            return true;
        }
    }
    if (x.name) {
        size_t length = 0;
        const char *spelled =
            tokens.pp.identifier_spelling(x.name - 1, &length);
        vbuffer_append(out, spelled, length);
    } else if (x.kind == entity_kind::class_type && x.defined) {
        source_position at = tokens.position(x.class_key);
        vbuffer_format_append(out, "(unnamed at %u:%u)", at.line, at.column);
    } else {
        return false;
    }
    if (nested)
        vbuffer_string_append(out, "::");
    return true;
}

/*
 * layout-claims: per class analysis completed ([abi:class-types],
 * abi/itanium/layout.cpp), a line
 *
 *     record <tab> FILE:LINE:COLUMN <tab> NAME <tab> SIZE <tab> DSIZE <tab> ALIGN
 *
 * (the place of its class-key; sizeof, the data size and the alignment in
 * bytes), then a line per direct base class subobject and per non-static
 * data member or unnamed bit-field, in the layout's order, after a dynamic
 * class's own virtual table pointer:
 *
 *     vptr <tab> 0
 *     base <tab> OFFSET <tab> NAME
 *     field <tab> OFFSET <tab> NAME
 *     bits <tab> BIT-OFFSET <tab> WIDTH <tab> NAME
 *
 * bench/cxx-sema.py compares each record with the one of that name in
 * clang -fdump-record-layouts-complete.  A class whose class-key a macro
 * made, or whose name clang would write another way, is left out.
 */
static void dump_layout_claims(const semantic_analysis &analysis,
                               token_stream &tokens, vbuffer *out)
{
    owned_buffer name, lines;
    for (entity_index c = 1; c < analysis.entity_table.size(); c++) {
        const entity &x = analysis.entity_table[c];
        if (x.kind != entity_kind::class_type || !x.defined || !x.size ||
            tokens[x.class_key].preprocessing.expansion)
            continue;
        name.clear();
        if (!layout_class_name(analysis, tokens, c, false, name.get()))
            continue;
        source_position at = tokens.position(x.class_key);
        lines.clear();
        vbuffer_format_append(lines.get(), "record\t%s:%u:%u\t", at.file_name,
                              at.line, at.column);
        vbuffer_append(lines.get(), name.data(), name.size());
        vbuffer_format_append(lines.get(), "\t%llu\t%llu\t%u\n",
                              static_cast<unsigned long long>(x.size),
                              static_cast<unsigned long long>(x.data_size),
                              x.alignment);
        bool nameable = true;
        const dynamic_class *d = analysis.dynamic_of(c);
        if (d && !d->primary_base)
            /* [abi:class-types] I: the virtual table pointer */
            vbuffer_string_append(lines.get(), "vptr\t0\n");
        for (uint32_t i = 0; i < x.base_count; i++) {
            const entity &b =
                analysis.entity_table[analysis.class_bases[x.first_base + i]];
            if (b.virtual_base)
                continue;
            name.clear();
            if (!layout_class_name(
                    analysis, tokens,
                    analysis.type_table[b.type.unqualified()].entity, false,
                    name.get()))
                nameable = false;
            vbuffer_format_append(lines.get(), "base\t%llu\t",
                                  static_cast<unsigned long long>(b.offset));
            vbuffer_append(lines.get(), name.data(), name.size());
            vbuffer_string_append(lines.get(), "\n");
        }
        for (uint32_t i = 0; i < x.member_count; i++) {
            const entity &m =
                analysis
                    .entity_table[analysis.class_members[x.first_member + i]];
            if (m.base_subobject)
                continue;
            if (m.bit_field)
                vbuffer_format_append(
                    lines.get(), "bits\t%llu\t%u\t",
                    static_cast<unsigned long long>(m.bit_offset),
                    static_cast<unsigned>(m.bit_width));
            else
                vbuffer_format_append(
                    lines.get(), "field\t%llu\t",
                    static_cast<unsigned long long>(m.offset));
            if (m.name) {
                size_t length = 0;
                const char *spelled =
                    tokens.pp.identifier_spelling(m.name - 1, &length);
                vbuffer_append(lines.get(), spelled, length);
            }
            vbuffer_string_append(lines.get(), "\n");
        }
        /* [abi:class-types] III: the virtual bases, after the members, in
           the order of clang's dump (the order of their construction) */
        for (uint32_t i = 0; i < x.virtual_base_count; i++) {
            const entity &b =
                analysis.entity_table
                    [analysis.class_virtual_bases[x.first_virtual_base + i]];
            name.clear();
            if (!layout_class_name(
                    analysis, tokens,
                    analysis.type_table[b.type.unqualified()].entity, false,
                    name.get()))
                nameable = false;
            vbuffer_format_append(lines.get(), "base\t%llu\t",
                                  static_cast<unsigned long long>(b.offset));
            vbuffer_append(lines.get(), name.data(), name.size());
            vbuffer_string_append(lines.get(), "\n");
        }
        if (nameable)
            vbuffer_append(out, lines.data(), lines.size());
    }
}

void dump_analysis(const dump_request &r, const semantic_analysis &analysis,
                   const syntax_tree &tree, token_stream &tokens, vbuffer *out)
{
    size_t by_result[4] = {};
    for (const analysed_declaration &d : analysis.declarations) {
        by_result[static_cast<size_t>(d.result)]++;
        if (!r.has(dump_output::analysis))
            continue;
        const syntax_node &s = tree[d.node];
        source_position first = tokens.position(s.first_token);
        token_index last = s.first_token;
        if (s.end_token > s.first_token)
            last = s.end_token - 1;
        source_position end = tokens.position(last);
        vbuffer_format_append(out, "%s %s:%u:%u %s:%u:%u %s",
                              syntax_kind_name(s.kind), first.file_name,
                              first.line, first.column, end.file_name, end.line,
                              end.column, analysis_result_name(d.result));
        if (d.result == analysis_result::unsupported)
            vbuffer_format_append(
                out, "=[%s]",
                analysis.unsupported[d.first_unsupported].subclause);
        vbuffer_string_append(out, "\n");
    }
    if (r.has(dump_output::unsupported)) {
        for (size_t i = 1; i < analysis.unsupported.size(); i++) {
            const unsupported_construct &u = analysis.unsupported[i];
            source_position at = tokens.position(tree[u.node].first_token);
            vbuffer_format_append(out, "[%s] %s %s:%u:%u\n", u.subclause,
                                  syntax_kind_name(tree[u.node].kind),
                                  at.file_name, at.line, at.column);
        }
    }
    if (r.has(dump_output::declaration_claims))
        dump_declaration_claims(analysis, tree, tokens, out);
    if (r.has(dump_output::type_claims))
        dump_type_claims(analysis, tree, tokens, out);
    if (r.has(dump_output::lookup_claims))
        dump_lookup_claims(analysis, tokens, out);
    if (r.has(dump_output::check_tree))
        check_tree(analysis, tree, tokens, out);
    if (r.has(dump_output::tree))
        dump_tree(analysis, tokens, out);
    if (r.has(dump_output::symbols))
        dump_symbols(analysis, tokens, out);
    if (r.has(dump_output::layout_claims))
        dump_layout_claims(analysis, tokens, out);
    if (r.has(dump_output::stats)) {
        vbuffer_format_append(
            out,
            "declarations %zu\nwell-formed %zu\nill-formed %zu\n"
            "unsupported %zu\nnot-parsed %zu\ntypes %zu\nentities %zu\n",
            analysis.declarations.size(),
            by_result[static_cast<size_t>(analysis_result::well_formed)],
            by_result[static_cast<size_t>(analysis_result::ill_formed)],
            by_result[static_cast<size_t>(analysis_result::unsupported)],
            by_result[static_cast<size_t>(analysis_result::not_parsed)],
            analysis.type_table.size() - 1, analysis.entity_table.size() - 1);
    }
}

} // namespace cplusplus
