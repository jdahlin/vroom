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
        if (accept(preprocessing_op_or_punc::ELLIPSIS)) {
            /* [ext.gnu] a case range, case lo ... hi: */
            constant_expression();
            if (failed())
                return 0;
        }
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
    uint64_t lo = 0, hi = 0;
    bool is_range = false;
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
        /* case constant-expression : statement, or GNU case lo ... hi :
           statement */
        SyntaxElement high;
        if (n.size() == 6 && n[2].is(preprocessing_op_or_punc::ELLIPSIS)) {
            high = n[3];
            is_range = true;
        } else if (n.size() != 4) {
            return not_supported(n, "stmt.switch");
        }
        result = case_label(n, n[1], high, innermost, lo, hi);
        kind = semantic_kind::case_label;
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
    uint32_t hi_node = 0;
    if (is_range && result == analysis_result::well_formed) {
        expression_meaning hi_m = integer_constant(
            function_state.switches[innermost].type, hi, first);
        hi_node = hi_m.node;
    }
    built = statement_node(kind, first, labelled, hi_node);
    semantics[built].value = lo;
    return result;
}

/*
 * [stmt.switch]/2: "case constant-expression : where the
 * constant-expression shall be a converted constant expression of the
 * adjusted type of the switch condition."
 */
analysis_result semantic_analysis::case_value(SyntaxRef n, SyntaxElement value,
                                              size_t innermost, uint64_t &out)
{
    expression_meaning m;
    VTRY(r, expression(value, m));
    fundamental_type adjusted = function_state.switches[innermost].type;
    if (adjusted == fundamental_type::none)
        return not_supported(n, "stmt.switch");
    VTRY_SET(r, operand_value(n, m));
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
    VTRY_SET(r, integral_constant_expression(n, m, constant));
    if (!constant)
        return ill_formed(n, rule, "stmt.switch");
    VTRY_SET(r, converted_constant_expression(n, m, adjusted, constant));
    if (!constant)
        return not_supported(n, "expr.const");
    out = m.value;
    return analysis_result::well_formed;
}

/*
 * [stmt.switch]/2: "No two of the case constants in the same switch shall
 * have the same value after conversion."  For a GNU case range lo ... hi,
 * neither endpoint may overlap an earlier case value or range.
 */
analysis_result semantic_analysis::case_label(SyntaxRef n, SyntaxElement low,
                                              SyntaxElement high,
                                              size_t innermost, uint64_t &lo,
                                              uint64_t &hi)
{
    VTRY(r, case_value(n, low, innermost, lo));
    hi = lo;
    if (high)
        VTRY_SET(r, case_value(n, high, innermost, hi));
    fundamental_type adjusted = function_state.switches[innermost].type;
    bool is_signed =
        fundamental_type_is_signed(tokens.files.behavior, adjusted);
    auto less = [is_signed](uint64_t a, uint64_t b) {
        if (is_signed)
            return static_cast<int64_t>(a) < static_cast<int64_t>(b);
        return a < b;
    };
    if (high && less(hi, lo))
        return analysis_result::well_formed;
    for (size_t i = function_state.switches[innermost].first_value;
         i < function_state.case_values.size(); i++) {
        const auto &prev = function_state.case_values[i];
        if (!less(hi, prev.lo) && !less(prev.hi, lo))
            return ill_formed(n,
                              "no two of the case constants in the same switch "
                              "shall have the same value after conversion",
                              "stmt.switch");
    }
    function_state.case_values.append({lo, hi});
    return analysis_result::well_formed;
}

} // namespace cplusplus
