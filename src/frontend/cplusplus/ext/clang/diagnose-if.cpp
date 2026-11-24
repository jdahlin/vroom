/*
 * diagnose-if.cpp: [ext:clang_diagnose_if] (clang Attributes,
 * diagnose_if): "diagnose_if(condition, "message", "error" | "warning")":
 * a function so declared is diagnosed at a call, or at the construction by a
 * constructor so declared, when the condition holds for the call's
 * arguments.  [impl.clang] The condition is analysed where the attribute is
 * written, in a scope where the function's parameters are (as a
 * noexcept-specifier's), and evaluated at each call with the parameters
 * bound to the arguments ([expr.call]/8): a condition that is not a constant
 * expression, or whose analysis fails, diagnoses nothing.  "error" makes
 * the call ill-formed with the message, "warning" is a warning
 * (-Wuser-defined-warnings).
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* the string-literal token t is "text" (an ordinary one): its text */
bool semantic_analysis::plain_string_token(token_index t,
                                           owned_buffer &text) const
{
    if (tokens[t].literal != literal_kind::string_literal)
        return false;
    tokens.spelling(t, text.get());
    return text.size() >= 2 && text.data()[0] == '"';
}

analysis_result semantic_analysis::diagnose_if_attribute(SyntaxRef at,
                                                         SyntaxElement a,
                                                         uint32_t count)
{
    /* name ( expression-list ): condition , message , type */
    if (count != 4 || a[2].is_token() || a[2].size() != 5 ||
        !a[2][2].is_token() || !a[2][4].is_token())
        return analysis_result::well_formed;
    SyntaxElement condition = a[2][0];
    owned_buffer message, type;
    if (!plain_string_token(a[2][2].token(), message) ||
        !plain_string_token(a[2][4].token(), type))
        return analysis_result::well_formed;
    diagnose_if_condition d;
    if (type.size() == 7 && !memcmp(type.data(), "\"error\"", 7))
        d.error = true;
    else if (!(type.size() == 9 && !memcmp(type.data(), "\"warning\"", 9)))
        return analysis_result::well_formed;
    d.at = at.first_token();
    d.source = condition;
    d.message = a[2][2].token();
    pending_diagnose_ifs.append(d);
    return analysis_result::well_formed;
}

/* the conditions written in the declaration of the function e, analysed as a
   trial: one analysis cannot follow (a member of the class being defined,
   say) leaves the function undiagnosed, as one that is not a constant
   expression does */
void semantic_analysis::diagnose_if_declared(SyntaxRef declaration,
                                             entity_index e)
{
    if (!(entity_table[e].gnu_flags & gnu_diagnose_if))
        return;
    owned_array<diagnose_if_condition> kept;
    for (diagnose_if_condition d : pending_diagnose_ifs) {
        if (d.at < declaration.first_token() ||
            d.at >= declaration.end_token()) {
            kept.append(d);
            continue;
        }
        if (names_dependent_entity(d.source.first_token(),
                                   d.source.end_token()))
            continue;
        uint32_t independent = independent_errors;
        uint32_t dependent = dependent_errors;
        size_t unsupported_before = unsupported.size();
        entity_index outer = current_scope;
        analysis_result r = analysis_result::well_formed;
        {
            diagnostics_suppressed hush(diagnostics);
            open_parameter_scope();
            entity_index scope = current_scope;
            expression_meaning m;
            r = expression(d.source, m);
            if (r == analysis_result::well_formed)
                r = contextual_bool(declaration, m);
            if (r == analysis_result::well_formed) {
                d.condition = m.node;
                d.function = e;
                d.first_parameter =
                    static_cast<uint32_t>(diagnose_if_parameters.size());
                for (const parameter_meaning &p : function_parameters) {
                    entity_index named = 0;
                    if (p.id)
                        named =
                            entity_table.find(scope, tokens.identifier(p.id));
                    diagnose_if_parameters.append(named);
                    d.parameter_count++;
                }
            }
        }
        current_scope = outer;
        independent_errors = independent;
        dependent_errors = dependent;
        unsupported.resize(unsupported_before);
        if (r == analysis_result::well_formed)
            diagnose_ifs.append(d);
    }
    pending_diagnose_ifs.clear();
    for (const diagnose_if_condition &d : kept)
        pending_diagnose_ifs.append(d);
}

analysis_result semantic_analysis::diagnose_if_call(token_index at,
                                                    entity_index f,
                                                    const uint32_t *arguments,
                                                    size_t count)
{
    if (!(entity_table[f].gnu_flags & gnu_diagnose_if))
        return analysis_result::well_formed;
    for (size_t i = 0; i < diagnose_ifs.size(); i++) {
        const diagnose_if_condition d = diagnose_ifs[i];
        if (d.function != f || d.parameter_count > count ||
            !diagnose_if_holds(d.condition,
                               &diagnose_if_parameters[d.first_parameter],
                               d.parameter_count, arguments))
            continue;
        owned_buffer text;
        tokens.spelling(d.message, text.get());
        owned_string &message =
            detail_texts.make(text.data() + 1, text.size() - 2);
        if (d.error)
            return ill_formed_at_token(at, message.c_str(), "impl.clang");
        diagnostics.issue(diagnostic_id::sema_user_warning, tokens.location(at))
            << message.c_str();
    }
    return analysis_result::well_formed;
}

} // namespace cplusplus
