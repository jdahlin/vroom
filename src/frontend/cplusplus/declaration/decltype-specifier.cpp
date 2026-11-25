/*
 * decltype-specifier.cpp: [dcl.type.decltype] Decltype specifiers: the type
 * a decltype-specifier denotes (/1).  The grammar is in decl-specifier.cpp.
 *
 * Not analysed: an operand naming a non-type template-parameter (/1's
 * second case, which has no rules yet), and
 * /2's prvalue of incomplete or abstract class type.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* whether operand is an unparenthesized id-expression or class member
   access ([dcl.type.decltype]/1's third case) */
bool semantic_analysis::names_entity(SyntaxElement operand) const
{
    if (operand.is_token())
        return operand.is_identifier();
    if (operand.is(syntax_kind::qualified_id))
        return true;
    if (!operand.is(syntax_kind::postfix_expression) || operand.size() != 3)
        return false;
    SyntaxElement op = operand[1];
    return op.is(preprocessing_op_or_punc::DOT,
                 preprocessing_op_or_punc::ARROW);
}

/*
 * [dcl.type.decltype]/1: "otherwise, if E is an unparenthesized
 * id-expression or an unparenthesized class member access, decltype(E) is
 * the type of the entity named by E. ... otherwise, if E is an xvalue,
 * decltype(E) is T&&, where T is the type of E; otherwise, if E is an
 * lvalue, decltype(E) is T&, where T is the type of E; otherwise,
 * decltype(E) is the type of E."
 */
bool semantic_analysis::decltype_of(const expression_meaning &m, bool named,
                                    qualified_type &type)
{
    if (named) {
        if (!m.node)
            return false;
        /* "if E is an unparenthesized id-expression naming a structured
           binding ([dcl.struct.bind]), decltype(E) is the referenced type
           as given in the specification of the structured binding
           declaration" */
        if (m.binding) {
            type = entity_table[m.binding].type;
            return true;
        }
        const semantic_node &n = semantics[m.node];
        if (n.kind == semantic_kind::entity && n.a) {
            type = entity_table[n.a].type;
            return true;
        }
        if (n.kind == semantic_kind::member && n.b) {
            type = entity_table[n.b].type;
            return true;
        }
        /* an enumerator: a prvalue of its enumeration's type */
        if (n.kind == semantic_kind::constant &&
            m.category == value_category::prvalue) {
            type = m.type;
            return true;
        }
        return false;
    }
    const struct type record = type_table[m.type.unqualified()];
    if (record.kind == type_kind::none ||
        record.kind == type_kind::placeholder ||
        record.kind == type_kind::dependent)
        return false;
    type_index reference = 0;
    if (m.category == value_category::xvalue)
        reference = type_table.rvalue_reference_to(m.type);
    if (m.category == value_category::lvalue)
        reference = type_table.lvalue_reference_to(m.type);
    type = m.type;
    if (reference)
        type = qualified_type::make(reference, cv_none);
    return true;
}

/*
 * [dcl.type.decltype]:
 *
 *     decltype-specifier:
 *         decltype ( expression )
 *
 * "The operand of the decltype specifier is an unevaluated operand".
 */
analysis_result semantic_analysis::decltype_type(SyntaxRef n,
                                                 qualified_type &type)
{
    if (n.size() != 4)
        return not_supported(n, "dcl.type.decltype");
    SyntaxElement operand = n[2];
    if (operand.is_token() && operand.is_identifier()) {
        lookup_result found = lookup_unqualified(
            tokens.identifier(operand.token()), lookup_filter::any);
        entity_index named = found.entity;
        if (!named && !found.ambiguous)
            named = found.unknown_entity;
        if (named && entity_table[named].kind == entity_kind::none &&
            !entity_table[named].held_by_template &&
            entity_table[named].type.unqualified() &&
            !contains_placeholder(entity_table[named].type) &&
            !depends_on_template_parameter(entity_table[named].type)) {
            type = entity_table[named].type;
            return analysis_result::well_formed;
        }
    }
    /* an operand that names a template parameter (or a type that depends
       on one) is type-dependent ([temp.dep.expr]): not analysed */
    if (names_dependent_entity(n.first_token(), n.end_token())) {
        /* [temp.dep.expr]/4: "Expressions of the following forms are never
           type-dependent (because the type of the expression cannot be
           dependent): ... sizeof unary-expression, sizeof ( type-id ),
           sizeof ... ( identifier ), alignof ( type-id )": decltype of one
           is std::size_t, whatever the operand names (sizeof or alignof
           first) */
        SyntaxElement inner = operand;
        while (inner.is_node() && inner.size() == 1)
            inner = inner[0];
        if (inner.is_node() && inner.is(syntax_kind::unary_expression) &&
            inner.size() > 1 &&
            (inner[0].is(standard_keyword::sizeof_) ||
             inner[0].is(standard_keyword::alignof_))) {
            type = qualified_type::make(
                type_table.fundamental(fundamental_type::unsigned_long_int),
                cv_none);
            return analysis_result::well_formed;
        }
        /* [temp.dep.type]/10.3: "denoted by decltype(expression), where
           expression is type-dependent" a dependent type, in a template's
           own declaration (an instantiation's names are bound to
           arguments) */
        if (instantiating || class_instantiating || substituting)
            return not_supported(n, "temp.dep.expr");
        type = qualified_type::make(dependent_written_type(n), cv_none);
        return analysis_result::well_formed;
    }
    expression_meaning m;
    unevaluated++;
    analysis_result r = expression(operand, m);
    unevaluated--;
    if (r != analysis_result::well_formed)
        return r;
    if (m.bit_field_width && !names_entity(operand))
        return not_supported(n, "class.bit");
    bool named = names_entity(operand);
    if (!decltype_of(m, named, type))
        return not_supported(n, "dcl.type.decltype");
    /* [ext:clang_vla] a variable length array type is the declaration's
       own: clang refuses it as a template argument and in the type traits */
    if (has_variable_bound(type))
        return not_supported(n, "dcl.array");
    return analysis_result::well_formed;
}

/* whether one of the arguments a pack is bound to is a type that depends
   on a template parameter ([temp.dep.type]/10.3): an alias template used
   in another template's declaration */
bool semantic_analysis::pack_depends(entity_index pack) const
{
    for (uint32_t i = 0; i < entity_table[pack].member_count; i++) {
        entity_index a = pack_elements[entity_table[pack].value + i];
        if (a && entity_table[a].kind == entity_kind::typedef_name &&
            depends_on_template_parameter(entity_table[a].type))
            return true;
    }
    return false;
}

/* whether a name in [first, end) denotes a template parameter, or a
   variable or type whose type depends on one ([temp.dep.expr]); a
   function template's type depends on its own parameters, not the
   call's: only parameters, packs, variables and type names count */
bool semantic_analysis::names_dependent_entity(token_index first,
                                               token_index end) const
{
    for (token_index t = first; t < end; t++) {
        if (tokens[t].kind != token_kind::identifier)
            continue;
        lookup_result found =
            lookup_unqualified(tokens.identifier(t), lookup_filter::any);
        entity_index e = found.entity;
        if (!e && !found.ambiguous && found.unknown_entity &&
            !entity_table[found.unknown_entity].held_by_template)
            e = found.unknown_entity;
        if (!e) {
            /* (lookup leaves a pack's name unknown outside a pattern) */
            entity_index pack = find_pack(tokens.identifier(t));
            if (pack && pack_depends(pack))
                return true;
            continue;
        }
        entity_kind k = entity_table[e].kind;
        if (k == entity_kind::none || is_template_template_parameter(e))
            return true;
        if ((k == entity_kind::variable || k == entity_kind::typedef_name) &&
            depends_on_template_parameter(entity_table[e].type))
            return true;
    }
    return false;
}

/* the types of the casts written in x, analysed with the alias template's
   names bound ([expr.cast]: ( type-id ) cast-expression); a type that does
   not analyse is left out (the mangler then has no encoding) */
void semantic_analysis::collect_written_casts(
    SyntaxElement x, owned_array<written_binding> &found)
{
    if (!x.is_node())
        return;
    if (x.is(syntax_kind::cast_expression) && x.size() == 4 &&
        x[1].is(syntax_kind::type_id)) {
        size_t unsupported_before = unsupported.size();
        qualified_type t;
        analysis_result r = type_id(x[1], t);
        unsupported.resize(unsupported_before);
        if (r == analysis_result::well_formed) {
            written_binding b;
            SyntaxRef r2 = x;
            b.cast = r2.index().index();
            b.type = t;
            found.append(b);
        }
    }
    /* typename Q::template X<args> ([temp.dep.type]/9): the qualifier's
       type (element 0) and the type arguments, for the mangler */
    if (x.is(syntax_kind::typename_specifier) && x.size() >= 3 &&
        x[1].is(syntax_kind::nested_name_specifier) &&
        x[x.size() - 1].is(syntax_kind::simple_template_id)) {
        size_t before = found.size();
        size_t unsupported_before = unsupported.size();
        SyntaxRef r = x;
        nominated_scope scope;
        bool ok = nested_name_specifier(x[1], scope, true) ==
                      analysis_result::well_formed &&
                  scope.dependent;
        if (ok) {
            written_binding b;
            b.cast = r.index().index();
            b.element = 0;
            b.type = qualified_type::make(scope.dependent, cv_none);
            found.append(b);
            SyntaxElement id = x[x.size() - 1];
            uint32_t count = 0;
            for (uint32_t i = 2; i + 1 < id.size() && ok; i++) {
                if (id[i].is(preprocessing_op_or_punc::COMMA))
                    continue;
                if (id[i].is(syntax_kind::template_argument_list)) {
                    ok = false;
                    break;
                }
                qualified_type t;
                ok = type_id(id[i], t) == analysis_result::well_formed;
                if (ok) {
                    written_binding a;
                    a.cast = r.index().index();
                    a.element = ++count;
                    a.type = t;
                    found.append(a);
                }
            }
        }
        unsupported.resize(unsupported_before);
        if (!ok)
            found.resize(before);
    }
    for (SyntaxElement c : x.elements())
        collect_written_casts(c, found);
}

/* [temp.dep.type]/10.3: the names in n that an alias template's
   instantiation bound to a type that depends on a template parameter (an
   alias template used in another template's declaration), or to a pack of
   such types */
void semantic_analysis::collect_written_bindings(
    SyntaxRef n, owned_array<written_binding> &found)
{
    collect_written_casts(n, found);
    for (token_index t = n.first_token(); t < n.end_token(); t++) {
        if (tokens[t].kind != token_kind::identifier)
            continue;
        uint32_t id = tokens.identifier(t);
        bool seen = false;
        for (const written_binding &b : found)
            seen = seen || b.identifier == id;
        if (seen)
            continue;
        lookup_result x = lookup_unqualified(id, lookup_filter::any);
        entity_index e = x.entity;
        if (e) {
            /* (a template parameter's own name is no binding) */
            const struct type &r =
                type_table[entity_table[e].type.unqualified()];
            bool own = r.kind == type_kind::template_parameter &&
                       entity_table[r.entity].name == id &&
                       !entity_table[e].type.cv();
            if (entity_table[e].kind == entity_kind::typedef_name && !own &&
                depends_on_template_parameter(entity_table[e].type)) {
                written_binding b;
                b.identifier = id;
                b.type = entity_table[e].type;
                found.append(b);
            }
            continue;
        }
        entity_index pack = find_pack(id);
        if (!pack || !pack_depends(pack))
            continue;
        for (uint32_t i = 0; i < entity_table[pack].member_count; i++) {
            entity_index a = pack_elements[entity_table[pack].value + i];
            written_binding b;
            b.identifier = id;
            b.element = i;
            if (a)
                b.type = entity_table[a].type;
            found.append(b);
        }
    }
}

/* the dependent type of decltype-specifier n: one for the operands of the
   same tokens and the same bindings of its names */
type_index semantic_analysis::dependent_written_type(SyntaxRef n)
{
    owned_array<written_binding> bindings;
    collect_written_bindings(n, bindings);
    for (entity_index e : dependent_written_types) {
        auto y =
            SyntaxRef(node_index(static_cast<uint32_t>(entity_table[e].value)));
        bool same =
            y.end_token() - y.first_token() == n.end_token() - n.first_token();
        for (token_index i = 0; same && i < n.end_token() - n.first_token();
             i++)
            same =
                same_token_spelling(n.first_token() + i, y.first_token() + i);
        same = same && entity_table[e].member_count == bindings.size();
        for (uint32_t i = 0; same && i < bindings.size(); i++) {
            const written_binding &a =
                written_bindings[entity_table[e].first_member + i];
            same = a.identifier == bindings[i].identifier &&
                   a.cast == bindings[i].cast &&
                   a.element == bindings[i].element &&
                   a.type == bindings[i].type;
        }
        if (same)
            return entity_table[e].type.unqualified();
    }
    entity_index e = entity_table.make(entity_kind::dependent_written, 0, 0);
    entity_table[e].value = n.index().index();
    entity_table[e].first_member =
        static_cast<uint32_t>(written_bindings.size());
    entity_table[e].member_count = static_cast<uint32_t>(bindings.size());
    for (const written_binding &b : bindings)
        written_bindings.append(b);
    entity_table[e].type = qualified_type::make(
        type_table.of_entity(type_kind::dependent, e), cv_none);
    dependent_written_types.append(e);
    return entity_table[e].type.unqualified();
}

} // namespace cplusplus
