/*
 * labeled-statement.cpp: [stmt.label] Labeled statement.
 *
 *     labeled-statement:
 *         attribute-specifier-seq_opt identifier : statement
 *         attribute-specifier-seq_opt case constant-expression : statement
 *         attribute-specifier-seq_opt default : statement
 *
 * [stmt.label]/1: "The optional attribute-specifier-seq appertains to the
 * label."  P2324R2 (C++23) lets a label end a compound-statement; before
 * it that is [ext.clang] clang's extension, P9's.
 */
#include "frontend/cplusplus/sema.h"
#include "ambiguity-resolution.h"

namespace cplusplus
{

node_index parser::labeled_statement()
{
    size_t mark = begin_node();
    token_index first = position;
    attribute_specifier_seq();
    if (failed())
        return 0;
    if (accept(standard_keyword::case_)) {
        constant_expression();
        if (failed())
            return 0;
        if (at(preprocessing_op_or_punc::ELLIPSIS))
            /* [ext.gnu] a case range, case 1 ... 3: P9 */
            return beyond_this_step();
    } else {
        add_token(); /* default, or the identifier */
    }
    if (!accept(preprocessing_op_or_punc::COLON))
        return expected("':' after the label", "stmt.label");
    if (at(preprocessing_op_or_punc::RIGHT_BRACE)) {
        if (!tokens.files.behavior.has(language_feature::labels_anywhere))
            return beyond_this_step();
    } else {
        statement();
        if (failed())
            return 0;
    }
    return finish_node(syntax_kind::labeled_statement, mark, first);
}

/* ---- semantic analysis ---------------------------------------------- */

/* the identifier labels of a function body, before its statements are
   analysed: a goto can name a label that follows it ([stmt.label]/1); a
   lambda's or a local class's body is another function's */
void semantic_analysis::collect_labels(SyntaxElement s)
{
    if (s.is_token())
        return;
    if (s.is(syntax_kind::lambda_expression, syntax_kind::class_specifier))
        return;
    if (s.is(syntax_kind::labeled_statement) && s.size() && s[0].is_token() &&
        s[0].is_identifier()) {
        label_record l;
        l.name = tokens.identifier(s[0].token());
        l.point.at = s[0].token();
        function_state.labels.append(l);
    }
    for (auto child : s.elements())
        collect_labels(child);
}

/*
 * [stmt.label]/1:
 *
 *     labeled-statement:
 *         attribute-specifier-seq_opt identifier : statement
 *         attribute-specifier-seq_opt case constant-expression : statement
 *         attribute-specifier-seq_opt default : statement
 */
analysis_result semantic_analysis::labeled_statement(LabeledStatement n,
                                                     uint32_t &built)
{
    if (!n.size() || !n[0].is_token())
        return not_supported(n, "dcl.attr");
    token_index first = n[0].token();
    const token &t = tokens[first];
    SyntaxElement last = n[n.size() - 1];
    if (t.kind == token_kind::identifier) {
        if (n.size() != 3)
            return not_supported(n, "stmt.label");
        for (label_record &l : function_state.labels)
            if (l.point.at == first) {
                l.point.scope = current_scope;
                l.analysed = true;
            }
        uint32_t labelled = 0;
        analysis_result r = statement(last, labelled);
        built =
            statement_node(semantic_kind::identifier_label, first, labelled);
        semantics[built].value = tokens.identifier(first);
        return r;
    }
    standard_keyword k = p.keyword_of(t);
    if (k != standard_keyword::case_ && k != standard_keyword::default_)
        return not_supported(n, "stmt.label");
    jump_point here;
    here.scope = current_scope;
    here.at = first;
    analysis_result result = analysis_result::well_formed;
    /* [stmt.label]/2: "Case labels and default labels shall occur only in
       switch statements." */
    if (function_state.switches.empty()) {
        result = ill_formed(n,
                            "case labels and default labels shall occur only "
                            "in switch statements",
                            "stmt.label");
        uint32_t ignored = 0;
        return worse(result, statement(last, ignored));
    }
    size_t innermost = function_state.switches.size() - 1;
    semantic_kind kind = semantic_kind::default_label;
    uint64_t value = 0;
    if (k == standard_keyword::default_) {
        if (n.size() != 3)
            return not_supported(n, "stmt.label");
        /* [stmt.switch]/3: "There shall be at most one label of the form
           default : within a switch statement." */
        if (function_state.switches[innermost].has_default)
            result = ill_formed(n,
                                "there shall be at most one label of the form "
                                "default : within a switch statement",
                                "stmt.switch");
        function_state.switches[innermost].has_default = true;
    } else {
        /* case constant-expression : statement; GNU's case ranges
           (case a ... b) are not analysed */
        if (n.size() != 4)
            return not_supported(n, "stmt.switch");
        result = case_label(n, n[1], innermost);
        kind = semantic_kind::case_label;
        if (result == analysis_result::well_formed)
            value = function_state.case_values.back();
    }
    if (result != analysis_result::ill_formed &&
        jump_bypasses(function_state.switches[innermost].point, here))
        result = ill_formed(n,
                            "it is possible to transfer into a block, but not "
                            "in a way that bypasses declarations with "
                            "initialization",
                            "stmt.dcl");
    uint32_t labelled = 0;
    result = worse(result, statement(last, labelled));
    built = statement_node(kind, first, labelled);
    semantics[built].value = value;
    return result;
}

/*
 * [stmt.switch]/2: "case constant-expression : where the
 * constant-expression shall be a converted constant expression of the
 * adjusted type of the switch condition.  No two of the case constants in
 * the same switch shall have the same value after conversion."
 */
analysis_result semantic_analysis::case_label(SyntaxRef n, SyntaxElement value,
                                              size_t innermost)
{
    expression_meaning m;
    analysis_result r = expression(value, m);
    if (r != analysis_result::well_formed)
        return r;
    fundamental_type adjusted = function_state.switches[innermost].type;
    if (adjusted == fundamental_type::none)
        return not_supported(n, "stmt.switch");
    r = operand_value(n, m);
    if (r != analysis_result::well_formed)
        return r;
    const char *rule = "the constant-expression shall be a converted "
                       "constant expression of the adjusted type of the "
                       "switch condition";
    /* (a scoped enumeration's switch is on its underlying type: the
       condition's) */
    if (!promote_enumeration(m))
        enumeration_as_underlying(m);
    if (!is_integral(m.type)) {
        if (fundamental_of(m.type) == fundamental_type::none &&
            !is_pointer(m.type))
            return not_supported(n, "stmt.switch");
        return ill_formed(n, rule, "stmt.switch");
    }
    bool constant = false;
    r = integral_constant_expression(n, m, constant);
    if (r != analysis_result::well_formed)
        return r;
    if (!constant)
        return ill_formed(n, rule, "stmt.switch");
    r = converted_constant_expression(n, m, adjusted, constant);
    if (r != analysis_result::well_formed)
        return r;
    if (!constant)
        return not_supported(n, "expr.const");
    for (size_t i = function_state.switches[innermost].first_value;
         i < function_state.case_values.size(); i++)
        if (function_state.case_values[i] == m.value)
            return ill_formed(n,
                              "no two of the case constants in the same switch "
                              "shall have the same value after conversion",
                              "stmt.switch");
    function_state.case_values.append(m.value);
    return analysis_result::well_formed;
}

} // namespace cplusplus
