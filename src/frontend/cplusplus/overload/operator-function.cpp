/*
 * operator-function.cpp: [over.oper] Overloaded operators and [over.literal]
 * User-defined literals: the unqualified-ids that begin with operator
 * ([expr.prim.id.unqual]), with [class.conv.fct]'s conversion-function-id
 * (conversion-function.cpp).
 *
 *     operator-function-id:
 *         operator operator
 *
 *     operator: one of
 *         new delete new[] delete[] co_await () [] -> ->*
 *         ~ ! + - * / % ^ &
 *         | = += -= *= /= %= ^= &=
 *         |= == != < > <= >= <=> &&
 *         || << >> <<= >>= ++ -- ,
 *
 *     literal-operator-id:
 *         operator string-literal identifier
 *         operator user-defined-string-literal
 *
 * [over.oper]/1: "Both the unary and binary forms of + - * & can be
 * overloaded"; "The following operators cannot be overloaded: . .* :: ?:".
 * A template-id of an operator-function-id or literal-operator-id is P7's.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

/* [over.oper]/1's operator, the tokens that are one */
bool is_overloadable_operator(const token &t)
{
    if (t.kind != token_kind::operator_or_punctuator)
        return false;
    switch (preprocessing_op_or_punc_primary(t.op_or_punc)) {
    case preprocessing_op_or_punc::ARROW:
    case preprocessing_op_or_punc::ARROW_STAR:
    case preprocessing_op_or_punc::TILDE:
    case preprocessing_op_or_punc::EXCLAMATION:
    case preprocessing_op_or_punc::PLUS:
    case preprocessing_op_or_punc::MINUS:
    case preprocessing_op_or_punc::STAR:
    case preprocessing_op_or_punc::SLASH:
    case preprocessing_op_or_punc::PERCENT:
    case preprocessing_op_or_punc::CARET:
    case preprocessing_op_or_punc::AMPERSAND:
    case preprocessing_op_or_punc::BAR:
    case preprocessing_op_or_punc::EQUAL:
    case preprocessing_op_or_punc::PLUS_EQUAL:
    case preprocessing_op_or_punc::MINUS_EQUAL:
    case preprocessing_op_or_punc::STAR_EQUAL:
    case preprocessing_op_or_punc::SLASH_EQUAL:
    case preprocessing_op_or_punc::PERCENT_EQUAL:
    case preprocessing_op_or_punc::CARET_EQUAL:
    case preprocessing_op_or_punc::AMPERSAND_EQUAL:
    case preprocessing_op_or_punc::BAR_EQUAL:
    case preprocessing_op_or_punc::EQUAL_EQUAL:
    case preprocessing_op_or_punc::EXCLAMATION_EQUAL:
    case preprocessing_op_or_punc::LESS:
    case preprocessing_op_or_punc::GREATER:
    case preprocessing_op_or_punc::LESS_EQUAL:
    case preprocessing_op_or_punc::GREATER_EQUAL:
    case preprocessing_op_or_punc::SPACESHIP:
    case preprocessing_op_or_punc::AMPERSAND_AMPERSAND:
    case preprocessing_op_or_punc::BAR_BAR:
    case preprocessing_op_or_punc::LESS_LESS:
    case preprocessing_op_or_punc::GREATER_GREATER:
    case preprocessing_op_or_punc::LESS_LESS_EQUAL:
    case preprocessing_op_or_punc::GREATER_GREATER_EQUAL:
    case preprocessing_op_or_punc::PLUS_PLUS:
    case preprocessing_op_or_punc::MINUS_MINUS:
    case preprocessing_op_or_punc::COMMA:
        return true;
    default:
        return false;
    }
}

} // namespace

/*
 * The unqualified-id at operator: which of the three it is follows from the
 * token after operator.  [over.literal]/1: "The string-literal or
 * user-defined-string-literal in a literal-operator-id shall have no
 * encoding-prefix and shall contain no characters other than the implicit
 * terminating '\0'."  User-defined literals are C++11's (N2765).
 */
node_index parser::operator_unqualified_id()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* operator */
    const token &t = peek();
    if (t.literal == literal_kind::string_literal ||
        (t.literal == literal_kind::user_defined_literal &&
         t.preprocessing.kind ==
             preprocessing_token_kind::user_defined_string_literal)) {
        if (!tokens.files.behavior.has(language_feature::user_defined_literals))
            return beyond_this_step();
        /* [lex.phases]/1.6: the token is the concatenation of adjacent
           string-literals; each of them has to be empty and without
           encoding-prefix (R is none, [lex.string]/1), the last may be the
           user-defined one */
        bool suffixed = t.literal == literal_kind::user_defined_literal;
        size_t count = 0;
        const preprocessing_token *pieces = tokens.pieces(position, &count);
        for (size_t i = 0; i < count; i++) {
            owned_buffer spelling;
            tokens.files.spelling(pieces[i], spelling.get());
            const char *text = spelling.data();
            bool prefixed =
                spelling.size() < 2 ||
                (text[0] != '"' && !(text[0] == 'R' && text[1] == '"'));
            if (prefixed)
                return violates("an empty string-literal with no "
                                "encoding-prefix after operator",
                                "over.literal", position);
        }
        if (tokens.files.value_of(t).string_length != 0)
            return violates("an empty string-literal with no "
                            "encoding-prefix after operator",
                            "over.literal", position);
        add_token();
        if (!suffixed) {
            if (!at_identifier())
                return expected("an identifier after the string-literal",
                                "over.literal");
            add_token();
        }
        node_index n =
            finish_node(syntax_kind::literal_operator_id, mark, first);
        /* template-id: literal-operator-id < template-argument-list_opt > */
        if (at(preprocessing_op_or_punc::LESS))
            return template_id_rest(mark, first, syntax_kind::template_id);
        return n;
    }
    if (at(standard_keyword::new_) || at(standard_keyword::delete_)) {
        add_token();
        if (at(preprocessing_op_or_punc::LEFT_BRACKET) &&
            at(preprocessing_op_or_punc::RIGHT_BRACKET, 1)) {
            add_token();
            add_token();
        }
    } else if (at(standard_keyword::co_await_)) {
        add_token();
    } else if ((at(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
                at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 1)) ||
               (at(preprocessing_op_or_punc::LEFT_BRACKET) &&
                at(preprocessing_op_or_punc::RIGHT_BRACKET, 1))) {
        add_token();
        add_token();
    } else if (is_overloadable_operator(t)) {
        add_token();
    } else {
        /* conversion-function-id: operator conversion-type-id */
        conversion_type_id();
        if (failed())
            return 0;
        return finish_node(syntax_kind::conversion_function_id, mark, first);
    }
    node_index n = finish_node(syntax_kind::operator_function_id, mark, first);
    /* template-id: operator-function-id < template-argument-list_opt > */
    if (at(preprocessing_op_or_punc::LESS))
        return template_id_rest(mark, first, syntax_kind::template_id);
    return n;
}

/* ---- semantic analysis ---- */

uint32_t semantic_analysis::operator_name(preprocessing_op_or_punc op)
{
    const char *rest = preprocessing_op_or_punc_spelling(op);
    if (op == preprocessing_op_or_punc::LEFT_PARENTHESIS)
        rest = "()";
    else if (op == preprocessing_op_or_punc::LEFT_BRACKET)
        rest = "[]";
    return operator_identifier(rest, strlen(rest));
}

uint32_t semantic_analysis::operator_identifier(const char *rest,
                                                size_t length) const
{
    /* the spelling operator@, which no identifier has */
    owned_buffer spelling;
    vbuffer_string_append(spelling.get(), "operator");
    vbuffer_append(spelling.get(), rest, length);
    return tokens.pp.identifier(spelling.data(), spelling.size()) + 1;
}

uint32_t semantic_analysis::allocation_function_name(allocation_function kind)
{
    const char *rest = " new";
    switch (kind) {
    case allocation_function::new_array:
        rest = " new[]";
        break;
    case allocation_function::delete_:
        rest = " delete";
        break;
    case allocation_function::delete_array:
        rest = " delete[]";
        break;
    default:
        break;
    }
    return operator_identifier(rest, strlen(rest));
}

/*
 * The operator-function-id of a declarator: operator @, operator (),
 * operator [], or an allocation or deallocation function's name
 * ([basic.stc.dynamic]/1).  co_await is not analysed.
 */
analysis_result semantic_analysis::operator_function_id(OperatorFunctionId id,
                                                        declarator_meaning &m)
{
    operators_held = true;
    if ((id.size() == 2 || id.size() == 4) && id[1].is_token() &&
        tokens[id[1].token()].kind == token_kind::keyword &&
        (id[1].keyword() == standard_keyword::new_ ||
         id[1].keyword() == standard_keyword::delete_)) {
        bool is_new = id[1].keyword() == standard_keyword::new_;
        bool array = id.size() == 4;
        allocation_function kind = allocation_function::delete_;
        if (is_new && array)
            kind = allocation_function::new_array;
        else if (is_new)
            kind = allocation_function::new_;
        else if (array)
            kind = allocation_function::delete_array;
        /* a declaration of a global one redeclares the implicit
           declaration ([basic.stc.dynamic]/2) */
        implicit_allocation_functions();
        operators_held = false;
        m.has_id = true;
        m.id = id[0].token();
        m.operator_function = true;
        m.allocation = kind;
        tokens.name_token(m.id, allocation_function_name(kind));
        return analysis_result::well_formed;
    }
    if (id.size() < 2 || id.size() > 3 || !id[0].is_token() ||
        !id[1].is_token() ||
        tokens[id[1].token()].kind != token_kind::operator_or_punctuator)
        /* co_await */
        return not_supported(id, "over.oper");
    preprocessing_op_or_punc op =
        preprocessing_op_or_punc_primary(tokens[id[1].token()].op_or_punc);
    if (id.size() == 3 && op != preprocessing_op_or_punc::LEFT_PARENTHESIS &&
        op != preprocessing_op_or_punc::LEFT_BRACKET)
        return not_supported(id, "over.oper");
    switch (op) {
    case preprocessing_op_or_punc::SPACESHIP:
        if (!tokens.files.behavior.has(
                language_feature::three_way_comparison_operator))
            return not_supported(id, "over.oper");
        break;
    default:
        break;
    }
    operators_held = false;
    m.has_id = true;
    m.id = id[0].token();
    m.operator_function = true;
    m.implemented = op;
    tokens.name_token(m.id, operator_name(op));
    return analysis_result::well_formed;
}

namespace
{

/* the operators that have a unary form ([expr.unary.op]/1, [over.oper]/2)
   and those that have only one */
bool has_unary_form(preprocessing_op_or_punc op)
{
    switch (op) {
    case preprocessing_op_or_punc::PLUS:
    case preprocessing_op_or_punc::MINUS:
    case preprocessing_op_or_punc::STAR:
    case preprocessing_op_or_punc::AMPERSAND:
    case preprocessing_op_or_punc::TILDE:
    case preprocessing_op_or_punc::EXCLAMATION:
        return true;
    default:
        return false;
    }
}

bool only_unary(preprocessing_op_or_punc op)
{
    return op == preprocessing_op_or_punc::TILDE ||
           op == preprocessing_op_or_punc::EXCLAMATION;
}

} // namespace

analysis_result semantic_analysis::operator_function_declaration(SyntaxRef n,
                                                                 entity_index f)
{
    if (entity_table[f].allocation != allocation_function::none)
        return allocation_function_declaration(n, f);
    const entity &x = entity_table[f];
    preprocessing_op_or_punc op = x.implemented;
    const struct type &function = type_table[x.type.unqualified()];
    const qualified_type *parameters =
        type_table.parameters(x.type.unqualified());
    bool member = x.enclosing &&
                  entity_table[x.enclosing].kind == entity_kind::class_type;
    bool call = op == preprocessing_op_or_punc::LEFT_PARENTHESIS;
    bool subscript = op == preprocessing_op_or_punc::LEFT_BRACKET;
    if ((call || subscript) && member && !x.nonstatic_member)
        /* C++23's static operator() and operator[] (P1169, P2589), an
           extension of clang's before */
        return not_supported(n, "over.call");
    /* [over.oper]/7: "An operator function shall either be a non-static
       member function or be a non-member function that has at least one
       parameter whose type is a class, a reference to a class, an
       enumeration, or a reference to an enumeration." */
    if (member && !x.nonstatic_member)
        return ill_formed(n,
                          "an operator function shall either be a "
                          "non-static member function or be a non-member "
                          "function",
                          "over.oper");
    if (!member) {
        bool of_class = false;
        for (uint32_t i = 0; i < function.parameter_count; i++) {
            qualified_type t = parameters[i];
            const struct type &r = type_table[t.unqualified()];
            if (r.kind == type_kind::lvalue_reference ||
                r.kind == type_kind::rvalue_reference)
                t = r.operand;
            type_kind k = type_table[t.unqualified()].kind;
            if (k == type_kind::class_type || k == type_kind::enumeration)
                of_class = true;
        }
        if (!of_class)
            return ill_formed(n,
                              "a non-member operator function shall have at "
                              "least one parameter whose type is a class, a "
                              "reference to a class, an enumeration, or a "
                              "reference to an enumeration",
                              "over.oper");
    }
    /* [over.ass]/1: "A simple assignment operator function shall be a
       non-static member function."  [over.call]/1: "A function call
       operator function is a function named operator() that is a
       non-static member function with an arbitrary number of parameters.
       It may have default arguments."  [over.sub]/1: "A subscripting
       operator function is a function named operator[] that is a
       non-static member function with exactly one parameter." */
    if ((call || subscript || op == preprocessing_op_or_punc::EQUAL) &&
        !x.nonstatic_member)
        return ill_formed(n,
                          "a simple assignment, function call or "
                          "subscripting operator function shall be a "
                          "non-static member function",
                          "over.oper");
    if (call)
        return analysis_result::well_formed;
    if (op == preprocessing_op_or_punc::ARROW) {
        /* [over.ref]/1: "A class member access operator function is a
           function named operator-> that is a non-static member function
           taking no parameters." */
        if (!x.nonstatic_member || function.parameter_count ||
            function.variadic)
            return ill_formed(n,
                              "a class member access operator function "
                              "shall be a non-static member function taking "
                              "no parameters",
                              "over.ref");
        return analysis_result::well_formed;
    }
    if (subscript && (function.parameter_count != 1 || function.variadic)) {
        if (tokens.files.behavior.has(
                language_feature::multidimensional_subscript))
            /* C++23's operator[] of any number of parameters */
            return not_supported(n, "over.sub");
        return ill_formed(n,
                          "a subscripting operator function shall have "
                          "exactly one parameter",
                          "over.sub");
    }
    /* [over.oper]/10: "An operator function cannot have default arguments
       ([dcl.fct.default]), except where explicitly stated below.  Operator
       functions cannot have more or fewer parameters than the number
       required for the corresponding operator" */
    if (x.default_arguments)
        return ill_formed(n,
                          "an operator function cannot have default "
                          "arguments",
                          "over.oper");
    uint32_t operands = function.parameter_count;
    if (member)
        operands++;
    bool count_ok = operands == 2 && !only_unary(op);
    if (operands == 1)
        count_ok = has_unary_form(op) ||
                   op == preprocessing_op_or_punc::PLUS_PLUS ||
                   op == preprocessing_op_or_punc::MINUS_MINUS;
    if (!count_ok || function.variadic)
        return ill_formed(n,
                          "operator functions cannot have more or fewer "
                          "parameters than the number required for the "
                          "corresponding operator",
                          "over.oper");
    if (op == preprocessing_op_or_punc::PLUS_PLUS ||
        op == preprocessing_op_or_punc::MINUS_MINUS) {
        /* [over.inc]/1: "a non-static member function with one parameter
           (which shall be of type int) or a non-member function with two
           parameters (the second of which shall be of type int)" */
        if (operands == 2 &&
            fundamental_of(parameters[function.parameter_count - 1]) !=
                fundamental_type::int_)
            return ill_formed(n,
                              "the parameter of a postfix increment or "
                              "decrement operator function shall be of type "
                              "int",
                              "over.inc");
    }
    return analysis_result::well_formed;
}

/*
 * [class.copy.assign]/1: "A user-declared copy assignment operator
 * X::operator= is a non-static non-template member function of class X
 * with exactly one parameter of type X, X&, const X&, volatile X&, or const
 * volatile X&."
 */
bool semantic_analysis::copy_assignment_operator(entity_index f,
                                                 entity_index c) const
{
    const entity &x = entity_table[f];
    if (!x.nonstatic_member || x.enclosing != c)
        return false;
    /* an operator+ or operator== of that parameter is none */
    if (!x.operator_function ||
        x.implemented != preprocessing_op_or_punc::EQUAL)
        return false;
    const struct type &function = type_table[x.type.unqualified()];
    if (function.parameter_count != 1 || function.variadic)
        return false;
    qualified_type p = type_table.parameters(x.type.unqualified())[0];
    const struct type &r = type_table[p.unqualified()];
    if (r.kind == type_kind::lvalue_reference)
        p = r.operand;
    const struct type &t = type_table[p.unqualified()];
    return t.kind == type_kind::class_type && t.entity == c;
}

bool semantic_analysis::assigned_bitwise(entity_index c)
{
    /* [class.copy.assign]/9.1: a class with virtual functions: its
       implicit copy assignment is not trivial (it keeps the virtual table
       pointer): not analysed */
    /* [class.copy.assign]/2: a declared move constructor or move
       assignment operator makes the implicit copy assignment deleted:
       not analysed */
    const entity &x = entity_table[c];
    if (x.dynamic ||
        ((x.defaulted_move_constructor || x.defaulted_move_assignment) &&
         !x.defaulted_copy_assignment))
        return false;
    lookup_result found = lookup_qualified(
        c, operator_name(preprocessing_op_or_punc::EQUAL), lookup_filter::any);
    if (found.entity || found.unknown || found.ambiguous)
        return false;
    owned_array<entity_index> classes;
    subobject_classes(c, classes);
    for (entity_index k : classes)
        if (entity_table[k].kind == entity_kind::class_type &&
            !assigned_bitwise(k))
            return false;
    return true;
}

} // namespace cplusplus
