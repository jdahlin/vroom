/*
 * dcl.init.cpp: [dcl.init] Initializers.
 *
 *     initializer:
 *         brace-or-equal-initializer
 *         ( expression-list )
 *
 *     brace-or-equal-initializer:
 *         = initializer-clause
 *         braced-init-list
 *
 * [dcl.init]/1: "The process of initialization described in this subclause
 * applies to all initializations regardless of syntactic context,
 * including the initialization of a function parameter, the initialization
 * of a return value, or when an initializer follows a declarator."
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * initializer_opt: nothing (0, no failure) when no initializer starts
 * here.  A braced-init-list passes through as its own node.
 */
node_index parser::initializer()
{
    size_t mark = begin_node();
    token_index first = position;
    if (at(preprocessing_op_or_punc::EQUAL)) {
        add_token();
        initializer_clause();
        if (failed())
            return 0;
        return finish_node(syntax_kind::brace_or_equal_initializer, mark,
                           first);
    }
    if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
        if (list_initialization_missing())
            return 0;
        return braced_init_list();
    }
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        add_token();
        if (at(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("an expression-list in the initializer",
                            "dcl.init");
        expression_list();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' to close the initializer", "dcl.init");
        return finish_node(syntax_kind::initializer, mark, first);
    }
    return 0;
}

/*
 * N2672: list-initialization is C++11's.  Before it
 * a braced list is only the initializer-clause of an aggregate's
 * initializer (= { ... }, and the lists nested in one); anywhere else
 * (T x{...}, T{...}, new T{...}, an argument, a default argument, the
 * right operand of an assignment) it is [ext.clang] clang's extension of
 * C++03, which is P9's.
 */
bool parser::list_initialization_missing()
{
    if (!at(preprocessing_op_or_punc::LEFT_BRACE) ||
        tokens.files.behavior.has(language_feature::list_initialization))
        return false;
    beyond_this_step();
    return true;
}

/*
 *     initializer-clause:
 *         assignment-expression
 *         braced-init-list
 *
 *     expr-or-braced-init-list:
 *         expression
 *         braced-init-list
 *
 * Both alternatives pass their operand on.
 */
node_index parser::initializer_clause()
{
    if (at(preprocessing_op_or_punc::LEFT_BRACE))
        return braced_init_list();
    return assignment_expression();
}

/*
 *     braced-init-list:
 *         { initializer-list ,_opt }
 *         { designated-initializer-list ,_opt }
 *         { }
 *
 * [dcl.init.list]/1: "List-initialization is initialization of an object
 * or reference from a braced-init-list. ... An initializer list may be
 * empty."  A '.' begins a designator, and nothing else in an
 * initializer-clause does, so the first token says which list it is.
 */
node_index parser::braced_init_list()
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    nesting_level level(*this);
    if (!nesting_within_limit("an initializer nested no deeper than this "
                              "implementation's limit"))
        return 0;
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* { */
    if (!at(preprocessing_op_or_punc::RIGHT_BRACE)) {
        if (gnu_initializer_list_needed()) {
            /* [ext:gnu_designated_initializers] */
            gnu_initializer_list();
        } else if (at(preprocessing_op_or_punc::DOT)) {
            /* P0329R4 (C++20); before it [ext.clang] C99's designators,
               clang's extension: P9 */
            if (!tokens.files.behavior.has(
                    language_feature::designated_initializers))
                return beyond_this_step();
            designated_initializer_list();
        } else {
            initializer_list(true);
        }
        if (failed())
            return 0;
        accept(preprocessing_op_or_punc::COMMA);
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_BRACE))
        return expected("'}' to close the braced-init-list", "dcl.init");
    return finish_node(syntax_kind::braced_init_list, mark, first);
}

/*
 *     initializer-list:
 *         initializer-clause ..._opt
 *         initializer-list , initializer-clause ..._opt
 *
 * The clauses of the list are one node's children; a list of one clause
 * passes it on.  In braces, a ',' before the '}' is the braced-init-list's.
 * The ... of a pack expansion ([temp.variadic]) is P7's.
 */
node_index parser::initializer_list(bool in_braces)
{
    size_t mark = begin_node();
    token_index first = position;
    unsigned count = 0;
    node_index n = 0;
    for (;;) {
        /* [ext.clang] C99's designators in C++: an initializer-list that
           mixes clauses with designators, the array designator [n] = (a
           '[' whose group no '=' follows begins a lambda-expression): P9 */
        if (in_braces && (at(preprocessing_op_or_punc::DOT) ||
                          (at(preprocessing_op_or_punc::LEFT_BRACKET) &&
                           at(preprocessing_op_or_punc::EQUAL, group_end(0)))))
            return beyond_this_step();
        if (!in_braces && list_initialization_missing())
            return 0;
        n = initializer_clause();
        if (failed())
            return 0;
        if (at(preprocessing_op_or_punc::ELLIPSIS))
            return beyond_this_step();
        count++;
        if (!at(preprocessing_op_or_punc::COMMA))
            break;
        if (in_braces && at(preprocessing_op_or_punc::RIGHT_BRACE, 1))
            break;
        add_token();
    }
    if (count < 2)
        return n;
    return finish_node(syntax_kind::initializer_list, mark, first);
}

/*
 *     designated-initializer-list:
 *         designated-initializer-clause
 *         designated-initializer-list , designated-initializer-clause
 *
 *     designated-initializer-clause:
 *         designator brace-or-equal-initializer
 *
 *     designator:
 *         . identifier
 *
 * As for initializer-list, a list of one clause passes it on and the ','
 * before the '}' is the braced-init-list's.
 */
node_index parser::designated_initializer_list()
{
    size_t mark = begin_node();
    token_index first = position;
    unsigned count = 0;
    node_index n = 0;
    for (;;) {
        size_t clause_mark = begin_node();
        token_index clause_first = position;
        if (!at(preprocessing_op_or_punc::DOT))
            /* [ext.clang] a clause without a designator after one with
               (C99's mixture, which clang accepts with a warning), or its
               array designator [n] =: the grammar has no such list.  P9 */
            return beyond_this_step();
        add_token();
        if (!at_identifier())
            return expected("an identifier after '.'", "dcl.init");
        add_token();
        finish_node(syntax_kind::designator, clause_mark, clause_first);
        if (at(preprocessing_op_or_punc::DOT) ||
            at(preprocessing_op_or_punc::LEFT_BRACKET))
            /* [ext.clang] C99's nested designator .a.b = and .a[n] =: P9 */
            return beyond_this_step();
        if (at(preprocessing_op_or_punc::EQUAL)) {
            size_t initializer_mark = begin_node();
            token_index initializer_first = position;
            add_token();
            initializer_clause();
            if (failed())
                return 0;
            finish_node(syntax_kind::brace_or_equal_initializer,
                        initializer_mark, initializer_first);
        } else if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
            braced_init_list();
            if (failed())
                return 0;
        } else {
            return expected("a brace-or-equal-initializer after the "
                            "designator",
                            "dcl.init");
        }
        n = finish_node(syntax_kind::designated_initializer_clause, clause_mark,
                        clause_first);
        count++;
        if (!at(preprocessing_op_or_punc::COMMA) ||
            at(preprocessing_op_or_punc::RIGHT_BRACE, 1))
            break;
        add_token();
    }
    if (count < 2)
        return n;
    return finish_node(syntax_kind::designated_initializer_list, mark, first);
}

/* ---- semantic analysis ---------------------------------------------- */

namespace
{

bool character_type(fundamental_type t)
{
    switch (t) {
    case fundamental_type::char_:
    case fundamental_type::signed_char:
    case fundamental_type::unsigned_char:
    case fundamental_type::char8_t_:
    case fundamental_type::char16_t_:
    case fundamental_type::char32_t_:
    case fundamental_type::wchar_t_:
        return true;
    default:
        return false;
    }
}

/* a string-literal, not in parentheses */
bool is_string_literal(token_stream &tokens, syntax_child c)
{
    return c.is_token() && tokens[c.index()].kind == token_kind::literal &&
           tokens[c.index()].literal == literal_kind::string_literal;
}

} // namespace

/*
 * [dcl.init.string]/1: "An array of ordinary character type, char8_t
 * array, char16_t array, char32_t array, or wchar_t array can be
 * initialized by an ordinary string literal, UTF-8 string literal, UTF-16
 * string literal, UTF-32 string literal, or wide string literal,
 * respectively, or by an appropriately-typed string-literal enclosed in
 * braces."  /2: "There shall not be more initializers than there are array
 * elements."
 */
analysis_result semantic_analysis::string_initialization(
    node_index at, const expression_meaning &string, qualified_type &type)
{
    const struct type array = type_table[type.unqualified()];
    fundamental_type element = fundamental_of(array.operand);
    const token &literal = tokens[string.string_literal];
    fundamental_type of_literal = literal.type;
    bool ordinary = element == fundamental_type::char_ ||
                    element == fundamental_type::signed_char ||
                    element == fundamental_type::unsigned_char;
    bool matches = element == of_literal ||
                   (ordinary && of_literal == fundamental_type::char_);
    if (!matches) {
        /* an array of ordinary characters and a UTF-8 string literal of
           char8_t: P2513R4 allows it, clang as a defect report */
        if (ordinary && of_literal == fundamental_type::char8_t_)
            return not_supported(at, "dcl.init.string");
        return ill_formed(at,
                          "an array of ordinary character type, char8_t, "
                          "char16_t, char32_t, or wchar_t can be initialized "
                          "by an ordinary, UTF-8, UTF-16, UTF-32, or wide "
                          "string literal, respectively",
                          "dcl.init.string");
    }
    uint64_t size = uint64_t(literal.string_length) + 1;
    if (!array.bound_known) {
        type = qualified_type::make(type_table.array_of(array.operand, size),
                                    type.cv());
        return analysis_result::well_formed;
    }
    if (size > array.bound)
        return ill_formed(at,
                          "there shall not be more initializers than there "
                          "are array elements",
                          "dcl.init.string");
    return analysis_result::well_formed;
}

/*
 * [dcl.init.list]/7: "A narrowing conversion is an implicit conversion
 *   - from a floating-point type to an integer type, or
 *   - from long double to double or float, or from double to float, except
 *     where the source is a constant expression and the actual value after
 *     conversion is within the range of values that can be represented
 *     (even if it cannot be represented exactly), or
 *   - from an integer type or unscoped enumeration type to a floating-point
 *     type, except where the source is a constant expression and the actual
 *     value after conversion will fit into the target type and will produce
 *     the original value when converted back to the original type, or
 *   - from an integer type or unscoped enumeration type to an integer type
 *     that cannot represent all the values of the original type, except
 *     where the source is a constant expression whose value after integral
 *     promotions will fit into the target type, or
 *   - from a pointer type or a pointer-to-member type to bool."
 * m is the prvalue before the conversion; unsupported where whether the
 * source is a constant expression decides and analysis cannot tell.
 */
analysis_result semantic_analysis::narrowing(node_index at,
                                             const expression_meaning &m,
                                             qualified_type to, bool &narrows)
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    narrows = false;
    fundamental_type f = fundamental_of(m.type), t = fundamental_of(to);
    if (is_pointer(m.type)) {
        narrows = t == fundamental_type::bool_;
        return analysis_result::well_formed;
    }
    if (!fundamental_type_is_integral(t) && !fundamental_type_is_floating(t))
        return analysis_result::well_formed;
    if (fundamental_type_is_floating(f) && fundamental_type_is_integral(t)) {
        narrows = true;
        return analysis_result::well_formed;
    }
    bool may_narrow = false;
    if (fundamental_type_is_floating(f) && fundamental_type_is_floating(t)) {
        may_narrow =
            (f == fundamental_type::long_double &&
             t != fundamental_type::long_double) ||
            (f == fundamental_type::double_ && t == fundamental_type::float_);
    } else if (fundamental_type_is_integral(f) &&
               fundamental_type_is_floating(t)) {
        may_narrow = true;
    } else if (fundamental_type_is_integral(f) &&
               fundamental_type_is_integral(t)) {
        unsigned wf = fundamental_type_width(b, f),
                 wt = fundamental_type_width(b, t);
        bool sf = fundamental_type_is_signed(b, f),
             st = fundamental_type_is_signed(b, t);
        bool all = false;
        if (f == fundamental_type::bool_)
            all = true;
        else if (t == fundamental_type::bool_)
            all = false;
        else if (sf == st)
            all = wt >= wf;
        else if (!sf && st)
            all = wt > wf;
        may_narrow = !all;
    }
    if (!may_narrow)
        return analysis_result::well_formed;
    if (m.evaluation == constness::not_constant) {
        narrows = true;
        return analysis_result::well_formed;
    }
    if (m.evaluation == constness::unknown ||
        f == fundamental_type::long_double ||
        t == fundamental_type::long_double)
        return not_supported(at, "dcl.init.list");
    expression_meaning converted = m;
    convert_arithmetic(converted, t);
    if (fundamental_type_is_floating(f) && fundamental_type_is_floating(t)) {
        /* within the range: the conversion did not overflow */
        narrows = converted.evaluation != constness::constant;
        return analysis_result::well_formed;
    }
    if (fundamental_type_is_floating(t)) {
        expression_meaning back = converted;
        convert_arithmetic(back, f);
        narrows =
            back.evaluation != constness::constant || back.value != m.value;
        return analysis_result::well_formed;
    }
    narrows = !integer_holds(b, t, integer_value(b, f, m.value));
    return analysis_result::well_formed;
}

/*
 * [dcl.init.ref]/5: "A reference to type "cv1 T1" is initialized by an
 * expression of type "cv2 T2" as follows: ..."  [dcl.init.ref]/4: ""cv1
 * T1" is reference-related to "cv2 T2" if T1 is similar to T2, or T1 is a
 * base class of T2.  "cv1 T1" is reference-compatible with "cv2 T2" if a
 * prvalue of type "pointer to cv2 T2" can be converted to the type "pointer
 * to cv1 T1" via a standard conversion sequence".
 */
analysis_result semantic_analysis::reference_binding(node_index at,
                                                     expression_meaning &m,
                                                     qualified_type reference)
{
    const struct type r = type_table[reference.unqualified()];
    bool rvalue_reference = r.kind == type_kind::rvalue_reference;
    qualified_type t1 = r.operand, t2 = m.type;
    const struct type x = type_table[t1.unqualified()];
    const struct type y = type_table[t2.unqualified()];
    if (x.kind == type_kind::class_type || x.kind == type_kind::enumeration ||
        y.kind == type_kind::class_type || y.kind == type_kind::enumeration ||
        x.kind == type_kind::none || y.kind == type_kind::none)
        return not_supported(at, "dcl.init.ref");
    qualified_type p1 =
        qualified_type::make(type_table.pointer_to(t1), cv_none);
    qualified_type p2 =
        qualified_type::make(type_table.pointer_to(t2), cv_none);
    bool compatible = p1.unqualified() == p2.unqualified() ||
                      qualification_convertible(p2, p1);
    bool related = similar(qualified_type::make(t1.unqualified(), cv_none),
                           qualified_type::make(t2.unqualified(), cv_none));
    bool function_lvalue =
        y.kind == type_kind::function && m.category == value_category::lvalue;
    /* "If the reference is an lvalue reference and the initializer
       expression is an lvalue (but is not a bit-field), and "cv1 T1" is
       reference-compatible with "cv2 T2"": it binds to the lvalue */
    if (!rvalue_reference && m.category == value_category::lvalue && compatible)
        return analysis_result::well_formed;
    /* "Otherwise, if the reference is an lvalue reference to a type that is
       not const-qualified or is volatile-qualified, the program is
       ill-formed." */
    if (!rvalue_reference && (!(t1.cv() & cv_const) || (t1.cv() & cv_volatile)))
        return ill_formed(at,
                          "an lvalue reference to a type that is not "
                          "const-qualified or is volatile-qualified binds "
                          "only to a reference-compatible lvalue",
                          "dcl.init.ref");
    /* "Otherwise, if the initializer expression is an rvalue (but not a
       bit-field) or function lvalue and "cv1 T1" is reference-compatible
       with "cv2 T2"": it binds to the rvalue (a materialized temporary) or
       the function */
    if ((m.category != value_category::lvalue || function_lvalue) && compatible)
        return analysis_result::well_formed;
    if (x.kind == type_kind::function)
        return ill_formed(at,
                          "a reference to function binds to a function lvalue "
                          "of a reference-compatible type",
                          "dcl.init.ref");
    /* "Otherwise, the initializer expression is implicitly converted to a
       prvalue of type "cv1 T1".  The temporary materialization conversion
       is applied and the reference is bound to the result."  /5.4.4: "If T1
       is reference-related to T2: cv1 shall be the same cv-qualification
       as, or greater cv-qualification than, cv2; and if the reference is an
       rvalue reference, the initializer expression shall not be an
       lvalue." */
    if (related) {
        if (t2.cv() & ~t1.cv())
            return ill_formed(at,
                              "cv1 shall be the same cv-qualification as, or "
                              "greater cv-qualification than, cv2",
                              "dcl.init.ref");
        if (rvalue_reference && m.category == value_category::lvalue)
            return ill_formed(at,
                              "if the reference is an rvalue reference, the "
                              "initializer expression shall not be an lvalue",
                              "dcl.init.ref");
    }
    analysis_result result = operand_value(at, m);
    if (result != analysis_result::well_formed)
        return result;
    return implicit_conversion(at, m, t1, false);
}

/*
 * [dcl.init]/17: the initialization of an object or reference of type type
 * by one initializer-clause that is an expression: copy-initialization
 * (= E, an element of a list), or direct-initialization (( E )); list when
 * the clause is the element of a list-initialization, whose narrowing
 * conversions are ill-formed ([dcl.init.list]/3.9).
 */
analysis_result semantic_analysis::initialize_from(node_index at,
                                                   syntax_child clause,
                                                   qualified_type &type,
                                                   bool direct, bool list,
                                                   initialized_value &result)
{
    if (syntax_is(tree, clause, syntax_kind::braced_init_list))
        return list_initialization(clause.index(), type, direct, result);
    const struct type target = type_table[type.unqualified()];
    expression_meaning m;
    if (target.kind == type_kind::array) {
        /* [dcl.init]/17.3: "If the destination type is an array of
           characters, ... and the initializer is a string-literal, see
           [dcl.init.string]." */
        if (is_string_literal(tokens, clause) &&
            character_type(fundamental_of(target.operand))) {
            analysis_result r = expression(clause, m);
            if (r != analysis_result::well_formed)
                return r;
            result.evaluation = constness::constant;
            result.node = m.node;
            return string_initialization(at, m, type);
        }
        if (direct)
            /* C++20's aggregate initialization from a parenthesized
               expression-list ([dcl.init]/17.5) */
            return not_supported(at, "dcl.init");
        analysis_result r = expression(clause, m);
        if (r != analysis_result::well_formed)
            return r;
        return ill_formed(at,
                          "an array is initialized by a braced-init-list, or "
                          "by a string-literal if it is an array of "
                          "characters",
                          "dcl.init");
    }
    analysis_result r = expression(clause, m);
    if (r != analysis_result::well_formed)
        return r;
    return initialize_with(at, m, type, direct, list, result);
}

/* the initialization of an object or reference of type type by the
   analysed expression m (the rest of initialize_from) */
analysis_result semantic_analysis::initialize_with(node_index at,
                                                   expression_meaning &m,
                                                   qualified_type type,
                                                   bool direct, bool list,
                                                   initialized_value &result)
{
    const struct type target = type_table[type.unqualified()];
    analysis_result r = analysis_result::well_formed;
    if (target.kind == type_kind::lvalue_reference ||
        target.kind == type_kind::rvalue_reference) {
        /* [dcl.init]/17.2: "If the destination type is a reference type,
           see [dcl.init.ref]." */
        result.evaluation = constness::unknown;
        r = reference_binding(at, m, type);
        if (r != analysis_result::well_formed)
            return r;
        /* the reference's own type: what lowering makes an address of */
        result.node = expression_node(semantic_kind::bind_reference, m,
                                      tree[at].first_token, m.node);
        semantics[result.node].type = type;
        return r;
    }
    if (target.kind == type_kind::class_type) {
        /* [dcl.init]/17.6.1: "If the initializer is a prvalue and the
           cv-unqualified version of the source type is the same class as
           the class of the destination, the initializer expression is used
           to initialize the destination object"; /17.6.2: "Otherwise, if
           the initialization is direct-initialization, or if it is
           copy-initialization where the cv-unqualified version of the
           source type is the same class as, or a derived class of, the
           class of the destination, constructors are considered": the
           implicitly-declared copy constructor ([class.copy.ctor]/6), which
           is trivial for the classes analysis has ([class.copy.ctor]/11):
           it copies the object representation */
        if (entity_table[target.entity].kind != entity_kind::class_type)
            return not_supported(at, "dcl.init");
        bool same = m.type.unqualified() == type.unqualified();
        if (!(same && m.category == value_category::prvalue) &&
            entity_table[target.entity].first_constructor) {
            /* /17.6.2 by [over.match.ctor]; /17.6.3: a user-defined
               conversion by a converting constructor ([class.conv.ctor]),
               whose result initializes the object (the copy elided) */
            owned_array<expression_meaning> arguments;
            arguments.append(m);
            return construct(at, type, arguments, 0, result);
        }
        if (!same)
            return not_supported(at, "dcl.init");
        r = operand_value(at, m);
        if (r != analysis_result::well_formed)
            return r;
        result.evaluation = constness::unknown;
        result.value = 0;
        result.node = m.node;
        return analysis_result::well_formed;
    }
    if (target.kind != type_kind::fundamental &&
        target.kind != type_kind::pointer)
        return not_supported(at, "dcl.init");
    if (target.kind == type_kind::fundamental &&
        target.fundamental == fundamental_type::void_)
        return not_supported(at, "dcl.init");
    r = operand_value(at, m);
    if (r != analysis_result::well_formed)
        return r;
    if (list &&
        tokens.files.behavior.has(language_feature::list_initialization)) {
        bool narrows = false;
        r = narrowing(at, m, type, narrows);
        if (r != analysis_result::well_formed)
            return r;
        /* Microsoft's extensions: clang accepts a narrowing conversion
           by the -fms-compatibility-version it emulates */
        if (narrows && tokens.files.behavior.microsoft_extensions)
            return not_supported(at, "dcl.init.list");
        if (narrows) {
            r = extension_used(diagnostic_id::sema_narrowing,
                               tree[at].first_token, nullptr);
            if (r != analysis_result::well_formed)
                return r;
        }
    }
    r = implicit_conversion(at, m, type, direct);
    if (r != analysis_result::well_formed)
        return r;
    result.evaluation = m.evaluation;
    result.value = m.value;
    result.node = m.node;
    return analysis_result::well_formed;
}

/*
 * [dcl.init.list]/3: "List-initialization of an object or reference of type
 * T is defined as follows: ..."  For the types analysis has: an array
 * (aggregate initialization, [dcl.init.aggr]), or a scalar ("if the
 * initializer list has a single element of type E ... the object or
 * reference is initialized from that element"; "Otherwise, if the
 * initializer list has no elements, the object is value-initialized";
 * "Otherwise, the program is ill-formed").
 */
analysis_result
semantic_analysis::list_initialization(node_index list, qualified_type &type,
                                       bool direct, initialized_value &result)
{
    const syntax_node &node = tree[list];
    const syntax_child *children = tree.children(list);
    owned_array<syntax_child> clauses;
    for (uint32_t i = 0; i < node.child_count; i++) {
        syntax_child c = children[i];
        if (syntax_is_punctuator(tokens, c,
                                 preprocessing_op_or_punc::LEFT_BRACE) ||
            syntax_is_punctuator(tokens, c,
                                 preprocessing_op_or_punc::RIGHT_BRACE) ||
            syntax_is_punctuator(tokens, c, preprocessing_op_or_punc::COMMA))
            continue;
        if (syntax_is(tree, c, syntax_kind::initializer_list)) {
            const syntax_node &items = tree[c.index()];
            for (uint32_t k = 0; k < items.child_count; k++) {
                syntax_child item = tree.children(c.index())[k];
                if (syntax_is_punctuator(tokens, item,
                                         preprocessing_op_or_punc::COMMA))
                    continue;
                if (syntax_is_punctuator(tokens, item,
                                         preprocessing_op_or_punc::ELLIPSIS))
                    return not_supported(list, "temp.variadic");
                clauses.append(item);
            }
            continue;
        }
        if (!c.is_token() && !syntax_kind_is_expression(tree[c.index()].kind) &&
            !syntax_is(tree, c, syntax_kind::braced_init_list))
            /* a designated-initializer-list */
            return not_supported(list, "dcl.init.aggr");
        clauses.append(c);
    }
    const struct type target = type_table[type.unqualified()];
    if (target.kind == type_kind::array) {
        /* [dcl.init.list]/3.3: "if T is a character array and the
           initializer list has a single element that is an
           appropriately-typed string-literal, initialization is performed
           as described in that subclause" */
        if (clauses.size() == 1 && is_string_literal(tokens, clauses[0]) &&
            character_type(fundamental_of(target.operand)))
            return initialize_from(list, clauses[0], type, direct, true,
                                   result);
        /* [dcl.init.aggr]/9: "An array of unknown bound initialized with a
           brace-enclosed initializer-list containing n initializer-clauses
           is defined as having n elements" */
        uint64_t bound = target.bound;
        qualified_type element = target.operand;
        if (!target.bound_known) {
            bound = clauses.size();
            if (!bound) {
                analysis_result r =
                    extension_used(diagnostic_id::sema_zero_length_array,
                                   node.first_token, nullptr);
                if (r != analysis_result::well_formed)
                    return r;
            }
            type = qualified_type::make(type_table.array_of(element, bound),
                                        type.cv());
        }
        /* brace elision ([dcl.init.aggr]/15): an element that is an
           aggregate initialized by a clause that is no braced-init-list
           (nor a string-literal for an array of characters) */
        const struct type of_element = type_table[element.unqualified()];
        if (of_element.kind != type_kind::fundamental &&
            of_element.kind != type_kind::pointer)
            for (syntax_child clause : clauses)
                if (!syntax_is(tree, clause, syntax_kind::braced_init_list) &&
                    !is_string_literal(tokens, clause))
                    return not_supported(list, "dcl.init.aggr");
        /* [dcl.init.aggr]/11: "An initializer-list is ill-formed if the
           number of initializer-clauses exceeds the number of elements of
           the aggregate." */
        if (clauses.size() > bound)
            return ill_formed(list,
                              "an initializer-list is ill-formed if the "
                              "number of initializer-clauses exceeds the "
                              "number of elements of the aggregate",
                              "dcl.init.aggr");
        /* [dcl.init.aggr]/4.2: "Otherwise, the element is copy-initialized
           from the corresponding initializer-clause ... If the
           initializer-clause is an expression and a narrowing conversion is
           required to convert the expression, the program is ill-formed." */
        constness all = constness::constant;
        owned_array<uint32_t> elements;
        for (syntax_child clause : clauses) {
            qualified_type e = element;
            initialized_value one;
            node_index at = list;
            if (!clause.is_token())
                at = clause.index();
            analysis_result r =
                initialize_from(at, clause, e, false, true, one);
            if (r != analysis_result::well_formed)
                return r;
            all = combined_constness(all, one.evaluation);
            elements.append(one.node);
        }
        result.evaluation = all;
        result.value = 0;
        expression_meaning list_meaning;
        list_meaning.type = type;
        list_meaning.evaluation = all;
        result.node = expression_node(semantic_kind::initializer_list,
                                      list_meaning, node.first_token);
        semantics[result.node].first =
            semantics.list(elements.data(), elements.size());
        semantics[result.node].count = static_cast<uint32_t>(elements.size());
        return analysis_result::well_formed;
    }
    if (target.kind == type_kind::class_type)
        return class_list_initialization(list, clauses, type, result);
    if (target.kind == type_kind::lvalue_reference ||
        target.kind == type_kind::rvalue_reference)
        return not_supported(list, "dcl.init.list");
    if (target.kind != type_kind::fundamental &&
        target.kind != type_kind::pointer)
        return not_supported(list, "dcl.init.list");
    if (clauses.empty()) {
        /* [dcl.init]/8: to value-initialize a scalar is to zero-initialize
           it: the value 0, a null pointer */
        result.evaluation = constness::constant;
        result.value = 0;
        expression_meaning zero;
        zero.type = qualified_type::make(type.unqualified(), cv_none);
        zero.evaluation = constness::constant;
        result.node =
            expression_node(semantic_kind::zero, zero, node.first_token);
        return analysis_result::well_formed;
    }
    if (clauses.size() > 1)
        return ill_formed(list,
                          "a scalar is list-initialized from a single "
                          "element, or from no element; otherwise, the "
                          "program is ill-formed",
                          "dcl.init.list");
    if (syntax_is(tree, clauses[0], syntax_kind::braced_init_list))
        /* braces around a scalar's initializer: clang's warning */
        return not_supported(list, "dcl.init.list");
    return initialize_from(list, clauses[0], type, direct, true, result);
}

/*
 * The initializer of a declarator: [dcl.init]/17.1: "If the initializer is
 * a (non-parenthesized) braced-init-list or is = braced-init-list, the
 * object or reference is list-initialized"; [dcl.init]/15: "The
 * initialization that occurs in the = form of a brace-or-equal-initializer
 * ... is called copy-initialization"; /16: "The initialization that occurs
 * ... in the forms T x(a); T x{a}; ... is called direct-initialization."
 */
analysis_result semantic_analysis::initializer(syntax_child init,
                                               qualified_type &type,
                                               initialized_value &result)
{
    if (init.is_token())
        return not_supported(current_declaration, "dcl.init");
    node_index n = init.index();
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    switch (node.kind) {
    case syntax_kind::braced_init_list:
        return list_initialization(n, type, true, result);
    case syntax_kind::brace_or_equal_initializer:
        if (node.child_count != 2)
            return not_supported(n, "dcl.init");
        if (syntax_is(tree, children[1], syntax_kind::braced_init_list))
            return list_initialization(children[1].index(), type, false,
                                       result);
        return initialize_from(n, children[1], type, false, false, result);
    case syntax_kind::initializer:
        if (node.child_count != 3)
            return not_supported(n, "dcl.init");
        if (syntax_is(tree, children[1], syntax_kind::initializer_list)) {
            const struct type target = type_table[type.unqualified()];
            if (target.kind == type_kind::array)
                return not_supported(n, "dcl.init");
            if (target.kind == type_kind::class_type) {
                /* [dcl.init]/17.6.2: the constructor called with the
                   expression-list as its arguments */
                if (entity_table[target.entity].kind !=
                        entity_kind::class_type ||
                    !entity_table[target.entity].first_constructor)
                    return not_supported(n, "dcl.init");
                owned_array<expression_meaning> arguments;
                analysis_result r =
                    expression_list_meanings(children[1], arguments);
                if (r != analysis_result::well_formed)
                    return r;
                return construct(n, type, arguments, 0, result);
            }
            return ill_formed(n,
                              "if the entity being initialized does not have "
                              "class type, the expression-list in a "
                              "parenthesized initializer shall be a single "
                              "expression",
                              "dcl.init");
        }
        return initialize_from(n, children[1], type, true, false, result);
    default:
        return not_supported(n, "dcl.init");
    }
}

/*
 * [dcl.init.list]/3 for a class T: "(3.4) Otherwise, if T is an aggregate,
 * aggregate initialization is performed ([dcl.init.aggr])"; "(3.5)
 * Otherwise, if the initializer list has no elements and T is a class type
 * with a default constructor, the object is value-initialized"; (3.7) a
 * constructor otherwise.  (3.2) first: "If T is a class type and the
 * initializer list has a single element of type cv U, where U is T or a
 * class derived from T, the object is initialized from that element".
 *
 * [dcl.init.aggr]/4: "For each explicitly initialized element: ... (4.2)
 * Otherwise, the element is copy-initialized from the corresponding
 * initializer-clause"; /5: "For a non-union aggregate, each element that
 * is not an explicitly initialized element is initialized as follows:
 * (5.1) If the element has a default member initializer, ... (5.2)
 * Otherwise, if the element is not a reference, the element is
 * copy-initialized from an empty initializer list ([dcl.init.list])."
 * Without brace elision ([dcl.init.aggr]/15): an element that is an
 * aggregate takes a braced-init-list.
 */
analysis_result semantic_analysis::class_list_initialization(
    node_index list, const owned_array<syntax_child> &clauses,
    qualified_type type, initialized_value &result)
{
    entity_index c = type_table[type.unqualified()].entity;
    if (entity_table[c].kind != entity_kind::class_type)
        return not_supported(list, "class.pre");
    if (!entity_table[c].size)
        /* [dcl.init.list]/3: list-initialization of an incomplete class:
           clang's "incomplete type" error */
        return not_supported(list, "dcl.init.list");
    if (!is_aggregate(c)) {
        /* (3.5): "Otherwise, if the initializer list has no elements and T
           is a class type with a default constructor, the object is
           value-initialized"; [dcl.init]/8.1: "if T is a (possibly
           cv-qualified) class type with ... a user-provided ... default
           constructor, then the object is default-initialized" */
        if (!clauses.empty() || !entity_table[c].first_constructor)
            /* (3.7) with narrowing checked ([over.match.list]) */
            return not_supported(list, "over.match.list");
        owned_array<expression_meaning> none;
        return construct(list, type, none, 0, result);
    }
    if (clauses.size() == 1 &&
        !syntax_is(tree, clauses[0], syntax_kind::braced_init_list)) {
        /* (3.2): an element of the class's own type */
        expression_meaning e;
        analysis_result r = expression(clauses[0], e);
        if (r != analysis_result::well_formed)
            return r;
        if (e.type.unqualified() == type.unqualified())
            return initialize_with(list, e, type, false, true, result);
        /* an element initialized from it: analysed again below */
    }
    const entity &x = entity_table[c];
    uint32_t count = x.member_count;
    uint32_t first = x.first_member;
    /* [dcl.init.aggr]/11: "An initializer-list is ill-formed if the number
       of initializer-clauses exceeds the number of elements of the
       aggregate." */
    bool elidable = false;
    for (uint32_t i = 0; i < count; i++) {
        type_kind k = type_table[entity_table[class_members[first + i]]
                                     .type.unqualified()]
                          .kind;
        if (k == type_kind::class_type || k == type_kind::array)
            elidable = true;
    }
    if (clauses.size() > count && elidable)
        /* brace elision ([dcl.init.aggr]/15) */
        return not_supported(list, "dcl.init.aggr");
    if (clauses.size() > count)
        return ill_formed(list,
                          "an initializer-list is ill-formed if the number of "
                          "initializer-clauses exceeds the number of elements "
                          "of the aggregate",
                          "dcl.init.aggr");
    owned_array<uint32_t> elements;
    constness all = constness::constant;
    for (uint32_t i = 0; i < count; i++) {
        entity_index member = class_members[first + i];
        qualified_type element = entity_table[member].type;
        const struct type &record = type_table[element.unqualified()];
        initialized_value one;
        if (i < clauses.size()) {
            syntax_child clause = clauses[i];
            node_index at = list;
            if (!clause.is_token())
                at = clause.index();
            bool braced =
                syntax_is(tree, clause, syntax_kind::braced_init_list);
            analysis_result r;
            if (record.kind == type_kind::class_type && !braced) {
                /* [dcl.init.aggr]/15: "If the initializer-clause is an
                   expression and an implicit conversion sequence can be
                   formed that converts the expression to the type of the
                   element, the element is initialized"; else the braces
                   are elided, not analysed yet */
                expression_meaning e;
                r = expression(clause, e);
                if (r != analysis_result::well_formed)
                    return r;
                if (e.type.unqualified() != element.unqualified())
                    return not_supported(list, "dcl.init.aggr");
                r = initialize_with(at, e, element, false, true, one);
            } else if (record.kind == type_kind::array && !braced &&
                       !is_string_literal(tokens, clause)) {
                /* brace elision */
                return not_supported(list, "dcl.init.aggr");
            } else {
                r = initialize_from(at, clause, element, false, true, one);
            }
            if (r != analysis_result::well_formed)
                return r;
        } else {
            /* (5.2): from an empty initializer list: value-initialization
               ([dcl.init.list]/3.11 for a scalar, an aggregate's own
               elements for an aggregate), all zero for these members */
            expression_meaning zero;
            zero.type = qualified_type::make(element.unqualified(), cv_none);
            zero.evaluation = constness::constant;
            one.evaluation = constness::constant;
            one.node = expression_node(semantic_kind::zero, zero,
                                       tree[list].first_token);
        }
        all = combined_constness(all, one.evaluation);
        elements.append(one.node);
    }
    result.evaluation = all;
    result.value = 0;
    expression_meaning list_meaning;
    list_meaning.type = qualified_type::make(type.unqualified(), cv_none);
    list_meaning.evaluation = all;
    result.node = expression_node(semantic_kind::initializer_list, list_meaning,
                                  tree[list].first_token);
    semantics[result.node].first =
        semantics.list(elements.data(), elements.size());
    semantics[result.node].count = static_cast<uint32_t>(elements.size());
    return analysis_result::well_formed;
}

} // namespace cplusplus
