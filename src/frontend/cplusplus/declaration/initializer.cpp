/*
 * initializer.cpp: [dcl.init] Initializers.
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
    bool expansion = false;
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
        /* [temp.variadic]/5: a pack expansion, the ... the list's child
           after its clause */
        if (accept(preprocessing_op_or_punc::ELLIPSIS))
            expansion = true;
        count++;
        if (!at(preprocessing_op_or_punc::COMMA))
            break;
        if (in_braces && at(preprocessing_op_or_punc::RIGHT_BRACE, 1))
            break;
        add_token();
    }
    if (count < 2 && !expansion)
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
bool is_unparenthesized_string_literal(token_stream &tokens, syntax_child c)
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
    SyntaxRef at, const expression_meaning &string, qualified_type &type)
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
            return not_supported_because(
                at, "dcl.init.string",
                "a char8_t string literal for a char array");
        return ill_formed(at,
                          "an array of ordinary character type, char8_t, "
                          "char16_t, char32_t, or wchar_t can be initialized "
                          "by an ordinary, UTF-8, UTF-16, UTF-32, or wide "
                          "string literal, respectively",
                          "dcl.init.string");
    }
    uint64_t size = uint64_t(tokens.files.value_of(literal).string_length) + 1;
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
analysis_result semantic_analysis::narrowing(SyntaxRef at,
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
    if (m.evaluation == constness::constant &&
        fundamental_type_is_integral(f) && t == fundamental_type::long_double) {
        /* [dcl.init.list]/7.3: the integer's value is kept when the
           significant bits (from the highest set bit to the lowest) fit
           long double's significand, the fraction bits and the leading one */
        __int128 v = integer_value(b, f, m.value);
        unsigned __int128 magnitude = static_cast<unsigned __int128>(v);
        if (fundamental_type_is_signed(b, f) && v < 0)
            magnitude = -magnitude;
        unsigned significant = 0;
        if (magnitude) {
            unsigned high_bit = 127, low_bit = 0;
            while (!((magnitude >> high_bit) & 1))
                high_bit--;
            while (!((magnitude >> low_bit) & 1))
                low_bit++;
            significant = high_bit - low_bit + 1;
        }
        narrows = significant > b.long_double_fraction_bits + 1u;
        return analysis_result::well_formed;
    }
    if (m.evaluation == constness::unknown ||
        f == fundamental_type::long_double ||
        t == fundamental_type::long_double)
        return not_supported_because(
            at, "dcl.init.list",
            "a narrowing check of an unknown or long double value");
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
 * [dcl.init.ref]/5.3.2 and [class.temporary]/6: a prvalue of class type
 * with no destructor (a lambda's closure object, built from its captures)
 * bound to a reference of its own class: the lowering holds it in a
 * temporary of the enclosing block as C's compound literal is.  A
 * destructor ([class.temporary]/4) and a base class's reference (the
 * conversion to the base, [class.temporary]/2) stay refused.
 */
bool semantic_analysis::closure_prvalue_binding(
    const expression_meaning &m) const
{
    const struct type y = type_table[m.type.unqualified()];
    if (y.kind != type_kind::class_type)
        return false;
    const entity &c = entity_table[y.entity];
    return c.lambda && !c.destructor;
}

/*
 * [dcl.init.ref]/4: "Given types "cv1 T1" and "cv2 T2", "cv1 T1" is
 * reference-related to "cv2 T2" if T1 is similar ([conv.qual]) to T2, or T1
 * is a base class of T2."  ambiguous is set when T1 is an ambiguous base
 * (base_step).
 */
bool semantic_analysis::reference_related(qualified_type t1, qualified_type t2,
                                          bool &ambiguous) const
{
    if (similar(qualified_type::make(t1.unqualified(), cv_none),
                qualified_type::make(t2.unqualified(), cv_none)))
        return true;
    const struct type &x = type_table[t1.unqualified()];
    const struct type &y = type_table[t2.unqualified()];
    return x.kind == type_kind::class_type && y.kind == type_kind::class_type &&
           entity_table[x.entity].kind == entity_kind::class_type &&
           entity_table[y.entity].kind == entity_kind::class_type &&
           base_step(y.entity, x.entity, ambiguous) != 0;
}

/* [dcl.init.ref]/4: ""cv1 T1" is reference-compatible with "cv2 T2" if a
   prvalue of type "pointer to cv2 T2" can be converted to the type "pointer
   to cv1 T1" via a standard conversion sequence": the same pointer type, a
   qualification conversion or a function pointer conversion (the
   derived-to-base pointer conversion is the class rules') */
uint32_t semantic_analysis::qualifiers_of(qualified_type t) const
{
    while (type_table[t.unqualified()].kind == type_kind::array)
        t = type_table[t.unqualified()].operand;
    return t.cv();
}

bool semantic_analysis::reference_compatible(qualified_type t1,
                                             qualified_type t2)
{
    qualified_type p1 =
        qualified_type::make(type_table.pointer_to(t1), cv_none);
    qualified_type p2 =
        qualified_type::make(type_table.pointer_to(t2), cv_none);
    return p1.unqualified() == p2.unqualified() ||
           qualification_convertible(p2, p1) ||
           function_pointer_convertible(p2, p1);
}

/*
 * [dcl.init.ref]/5: "A reference to type "cv1 T1" is initialized by an
 * expression of type "cv2 T2" as follows: ..."  [dcl.init.ref]/4: ""cv1
 * T1" is reference-related to "cv2 T2" if T1 is similar to T2, or T1 is a
 * base class of T2.  "cv1 T1" is reference-compatible with "cv2 T2" if a
 * prvalue of type "pointer to cv2 T2" can be converted to the type "pointer
 * to cv1 T1" via a standard conversion sequence".
 */
analysis_result semantic_analysis::reference_binding(SyntaxRef at,
                                                     expression_meaning &m,
                                                     qualified_type reference)
{
    const struct type r = type_table[reference.unqualified()];
    bool rvalue_reference = r.kind == type_kind::rvalue_reference;
    /* [dcl.init.ref]/5.3.2 (or /5.4): an aggregate's prvalue T{...} or T()
       of a class without a destructor bound to a reference to its own
       class is held in a temporary of the enclosing block (bind_reference);
       one bound to a base class's reference needs the conversion to the
       base subobject, and at namespace scope the temporary has static
       storage ([class.temporary]/6): _ZGR<name>_ (lowering) */
    /* (a scalar's or array's list temporary in an unevaluated operand,
       [expr.context]/1, has no storage to give) */
    bool scalar_in_unevaluated =
        unevaluated &&
        type_table[m.type.unqualified()].kind != type_kind::class_type;
    if (aggregate_prvalue(m) && !closure_prvalue_binding(m) &&
        !scalar_in_unevaluated &&
        (type_table[m.type.unqualified()].kind != type_kind::class_type ||
         (!at_block_scope() && !binding_extends)))
        return not_supported_because(
            at, "class.temporary",
            "an array's or scalar's list temporary, or a temporary of a call's "
            "argument at namespace scope");
    /* [dcl.init.ref]/5: a non-const lvalue reference "shall not be bound
       to a bit-field"; a const lvalue or rvalue reference is bound to a
       temporary holding its value ([conv.lval]) */
    if (m.bit_field_width && m.category != value_category::prvalue) {
        if (r.kind == type_kind::lvalue_reference &&
            (!(r.operand.cv() & cv_const) || (r.operand.cv() & cv_volatile)))
            return ill_formed(at,
                              "a non-const lvalue reference shall not be "
                              "bound to a bit-field",
                              "dcl.init.ref");
        m.category = value_category::prvalue;
        m.bit_field_width = 0;
        m.node = expression_node(semantic_kind::conversion, m, at.first_token(),
                                 m.node);
    }
    qualified_type t1 = r.operand, t2 = m.type;
    const struct type x = type_table[t1.unqualified()];
    const struct type y = type_table[t2.unqualified()];
    if (x.kind == type_kind::class_type && y.kind == type_kind::class_type) {
        entity_index c1 = x.entity, c2 = y.entity;
        bool ambiguous = false;
        /* ([dcl.init.ref]/5.1: the same class binds directly) */
        if (c1 != c2)
            require_complete(c2);
        if (entity_table[c1].kind == entity_kind::class_type &&
            entity_table[c2].kind == entity_kind::class_type &&
            entity_table[c2].size && c1 != c2 && !base_step(c2, c1, ambiguous))
            return converted_temporary_binding(at, m, reference);
        return class_reference_binding(at, m, reference);
    }
    if (x.kind == type_kind::class_type && y.kind != type_kind::class_type &&
        y.kind != type_kind::none)
        return converted_temporary_binding(at, m, reference);
    if (y.kind == type_kind::class_type &&
        (x.kind == type_kind::fundamental || x.kind == type_kind::pointer)) {
        /* [dcl.init.ref]/5.1.2, /5.3.2, /5.4.1: "has a class type (i.e., T2
           is a class type), where T1 is not reference-related to T2, and can
           be converted to an lvalue [or rvalue] of type "cv3 T3", where "cv1
           T1" is reference-compatible with "cv3 T3" (this conversion is
           selected by enumerating the applicable conversion functions
           ([over.match.ref]) and choosing the best one through overload
           resolution)": the conversion function that converts to T1 (a
           candidate set of [over.match.conv]'s), then the binding to its
           result.  [over.match.ref]/1 has other candidates than
           [over.match.conv] for a reference that is no const lvalue
           reference: analysed when the class has one conversion function,
           the candidate of both */
        bool const_lvalue = !rvalue_reference && (t1.cv() & cv_const) &&
                            !(t1.cv() & cv_volatile);
        if (!const_lvalue) {
            entity_index f = own_conversion_functions(y.entity);
            if (f && entity_table[f].next_overload)
                return not_supported_because(
                    at, "over.match.ref",
                    "a non-const reference with several conversion functions");
        }
        bool converted = false;
        VTRY(c, conversion_function_call(
                    at, m, qualified_type::make(t1.unqualified(), cv_none),
                    false, converted));
        require_complete(y.entity);
        if (!converted && entity_table[y.entity].size &&
            !has_conversion_functions(y.entity))
            /* [dcl.init.ref]/5.4: "Otherwise, ... the initializer expression
               is implicitly converted to a prvalue of type "cv1 T1"": a
               complete class without a conversion function has none */
            return ill_formed(at,
                              "a class without a conversion function to the "
                              "referenced type does not initialize a "
                              "reference to it",
                              "dcl.init.ref");
        if (!converted)
            return not_supported_because(
                at, "dcl.init.ref",
                "no conversion function found for the reference");
        return reference_binding(at, m, reference);
    }
    /* an enumeration has no conversion functions: its rules are a
       fundamental type's (libc++'s byte& operator|=(byte&, byte) returns
       the assignment's lvalue) */
    if (x.kind == type_kind::class_type || y.kind == type_kind::class_type ||
        x.kind == type_kind::none || y.kind == type_kind::none)
        return not_supported_because(
            at, "dcl.init.ref",
            "a reference binding with a class or unknown type");
    bool compatible = reference_compatible(t1, t2);
    bool ambiguous = false;
    bool related = reference_related(t1, t2, ambiguous);
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
    if (!rvalue_reference &&
        (!(qualifiers_of(t1) & cv_const) || (qualifiers_of(t1) & cv_volatile)))
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
    /* (a temporary of or from an enumeration: not yet) */
    if (x.kind == type_kind::enumeration || y.kind == type_kind::enumeration)
        return not_supported_because(
            at, "dcl.init.ref",
            "a reference temporary of or from an enumeration");
    VTRY(result, operand_value(at, m));
    return implicit_conversion(at, m, t1, false);
}

/*
 * [dcl.init.ref]/5.4.1 and /5.4.4: the initializer is not reference-related
 * to the class T1, so it is "implicitly converted to a prvalue of type "cv1
 * T1"" (a user-defined conversion: a converting constructor of T1
 * ([class.conv.ctor], [over.match.copy]/1.1)), "the temporary
 * materialization conversion is applied and the reference is bound to the
 * result" ([class.temporary]/6: the temporary lives as long as the reference
 * or to the end of the full-expression).  A temporary made by a constructor
 * that binds one again nests: bounded, a clean refusal at 16.
 */
analysis_result semantic_analysis::converted_temporary_binding(
    SyntaxRef at, expression_meaning &m, qualified_type reference)
{
    const struct type r = type_table[reference.unqualified()];
    bool rvalue_reference = r.kind == type_kind::rvalue_reference;
    qualified_type t1 = r.operand;
    /* /5.2: an lvalue reference to a non-const (or volatile) type binds to
       nothing converted, but to the lvalue a conversion function returns
       (/5.1.2) */
    bool lvalue_only =
        !rvalue_reference && (!(t1.cv() & cv_const) || (t1.cv() & cv_volatile));
    if (converting_binds >= 16)
        return not_supported_because(
            at, "dcl.init.ref",
            "too deeply nested converting reference binding");
    qualified_type made_type = qualified_type::make(t1.unqualified(), cv_none);
    const struct type &source = type_table[m.type.unqualified()];
    if (source.kind == type_kind::class_type &&
        has_conversion_functions(source.entity)) {
        /* /5.1.2, /5.3.2: "can be converted to an lvalue [or rvalue] of
           type "cv3 T3"" by a conversion function ([over.match.ref]) */
        bool converted = false;
        VTRY(c, conversion_function_call(at, m, made_type, false, converted));
        if (converted && (!lvalue_only || m.category == value_category::lvalue))
            return analysis_result::well_formed;
        if (converted)
            return not_supported_because(
                at, "dcl.init.ref",
                "a conversion function result that is no lvalue");
    }
    if (lvalue_only)
        return ill_formed(at,
                          "an lvalue reference to a type that is not "
                          "const-qualified or is volatile-qualified binds "
                          "only to a reference-compatible lvalue",
                          "dcl.init.ref");
    converting_binds++;
    initialized_value value;
    analysis_result a = initialize_with(at, m, made_type, false, false, value);
    converting_binds--;
    if (a != analysis_result::well_formed)
        return a;
    expression_meaning made;
    made.type = made_type;
    made.category = value_category::prvalue;
    made.evaluation = constness::not_constant;
    made.node = value.node;
    m = made;
    return analysis_result::well_formed;
}

/*
 * [dcl.init.ref]/5 for a class: "(5.1) If the reference is an lvalue
 * reference and the initializer expression (5.1.1) is an lvalue (but is
 * not a bit-field), and "cv1 T1" is reference-compatible with "cv2 T2"
 * ... then the reference binds to the initializer expression lvalue ...
 * (5.3) Otherwise, if the initializer expression (5.3.1) is an rvalue (but
 * not a bit-field) ... and "cv1 T1" is reference-compatible with "cv2
 * T2" ... the reference is bound to the result of the conversion"; /5.2
 * an lvalue reference to a non-const type binds to nothing else.  A
 * reference to a base binds to the base class subobject ([conv.ptr]/3's
 * pointer conversion makes T1 reference-compatible); the conversions by a
 * conversion function or constructor (/5.1.2, /5.3.2, /5.4.1) are not
 * analysed, nor a temporary with a destructor.
 */
analysis_result
semantic_analysis::class_reference_binding(SyntaxRef at, expression_meaning &m,
                                           qualified_type reference)
{
    const struct type r = type_table[reference.unqualified()];
    bool rvalue_reference = r.kind == type_kind::rvalue_reference;
    qualified_type t1 = r.operand, t2 = m.type;
    entity_index c1 = type_table[t1.unqualified()].entity;
    entity_index c2 = type_table[t2.unqualified()].entity;
    if (entity_table[c1].kind != entity_kind::class_type ||
        entity_table[c2].kind != entity_kind::class_type)
        return not_supported_because(
            at, "dcl.init.ref",
            "a base reference binding of non-analysed classes");
    /* the same class binds even when incomplete (an lvalue of it); a base
       needs the complete derived class */
    if (c1 != c2)
        require_complete(c2);
    if (c1 != c2 && !entity_table[c2].size)
        return not_supported_because(
            at, "dcl.init.ref",
            "a reference binding of an incomplete derived class");
    bool ambiguous = false;
    bool related = reference_related(t1, t2, ambiguous);
    if (!related)
        return not_supported_because(
            at, "dcl.init.ref",
            "a reference binding between unrelated classes");
    if (t2.cv() & ~t1.cv()) {
        if (!rvalue_reference && !(t1.cv() & cv_const))
            return ill_formed(at,
                              "an lvalue reference to a type that is not "
                              "const-qualified or is volatile-qualified binds "
                              "only to a reference-compatible lvalue",
                              "dcl.init.ref");
        return ill_formed(at,
                          "cv1 shall be the same cv-qualification as, or "
                          "greater cv-qualification than, cv2",
                          "dcl.init.ref");
    }
    bool lvalue = m.category == value_category::lvalue;
    if (!rvalue_reference && !lvalue &&
        (!(t1.cv() & cv_const) || (t1.cv() & cv_volatile)))
        return ill_formed(at,
                          "an lvalue reference to a type that is not "
                          "const-qualified or is volatile-qualified binds "
                          "only to a reference-compatible lvalue",
                          "dcl.init.ref");
    if (rvalue_reference && lvalue)
        return ill_formed(at,
                          "if the reference is an rvalue reference, the "
                          "initializer expression shall not be an lvalue",
                          "dcl.init.ref");
    if (c1 == c2)
        return analysis_result::well_formed;
    return to_base(at.first_token(), c1, m);
}

/*
 * [dcl.init]/17: the initialization of an object or reference of type type
 * by one initializer-clause that is an expression: copy-initialization
 * (= E, an element of a list), or direct-initialization (( E )); list when
 * the clause is the element of a list-initialization, whose narrowing
 * conversions are ill-formed ([dcl.init.list]/3.9).
 */
analysis_result semantic_analysis::initialize_from(SyntaxRef at,
                                                   SyntaxElement clause,
                                                   qualified_type &type,
                                                   bool direct, bool list,
                                                   initialized_value &result)
{
    if (clause.is(syntax_kind::braced_init_list))
        return list_initialization(clause, type, direct, result);
    const struct type target = type_table[type.unqualified()];
    expression_meaning m;
    if (target.kind == type_kind::array) {
        /* [dcl.init]/17.3: "If the destination type is an array of
           characters, ... and the initializer is a string-literal, see
           [dcl.init.string]." */
        if (is_unparenthesized_string_literal(tokens, clause) &&
            character_type(fundamental_of(target.operand))) {
            VTRY(r, expression(clause, m));
            result.evaluation = constness::constant;
            result.node = m.node;
            return string_initialization(at, m, type);
        }
        if (direct)
            /* C++20's aggregate initialization from a parenthesized
               expression-list ([dcl.init]/17.5) */
            return not_supported_because(
                at, "dcl.init", "a parenthesized aggregate initialization");
        VTRY(r, expression(clause, m));
        /* a string-literal in parentheses: GCC's and clang's extension */
        if (m.string_literal)
            return not_supported_because(
                at, "dcl.init.string",
                "a parenthesized string literal for an array");
        return ill_formed(at,
                          "an array is initialized by a braced-init-list, or "
                          "by a string-literal if it is an array of "
                          "characters",
                          "dcl.init");
    }
    /* [over.over]/1: "an object or reference being initialized" */
    bool extends = declared_initializer;
    declared_initializer = false;
    VTRY(r, target_expression(clause, type, m));
    binding_extends = extends;
    analysis_result done = initialize_with(at, m, type, direct, list, result);
    binding_extends = false;
    return done;
}

/*
 * [dcl.init]/17, from its second bullet: the initialization of an object or
 * reference of type type by the analysed expression m (the rest of
 * initialize_from, which has the braced-init-list, string-literal and
 * array bullets).  One function per bullet:
 * - "If the destination type is a reference type, see [dcl.init.ref]":
 *   reference_initialization;
 * - "Otherwise, if the destination type is a (possibly cv-qualified) class
 *   type": class_initialization;
 * - "Otherwise, if the source type is a (possibly cv-qualified) class type,
 *   conversion functions are considered", and "Otherwise, the initial value
 *   of the object being initialized is the (possibly converted) value of
 *   the initializer expression": scalar_initialization.
 */
analysis_result semantic_analysis::initialize_with(SyntaxRef at,
                                                   expression_meaning &m,
                                                   qualified_type type,
                                                   bool direct, bool list,
                                                   initialized_value &result)
{
    /* [ext:clang_atomic] an atomic object is initialized as an object of
       its value type (C23 6.7.11/11: "the initial value of the object
       ... is that of the expression (after conversion)"; not atomically) */
    if (type_index value = atomic_value_type(type))
        return initialize_with(at, m, qualified_type::make(value, type.cv()),
                               direct, list, result);
    switch (type_table[type.unqualified()].kind) {
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
        return reference_initialization(at, m, type, result);
    case type_kind::class_type:
        return class_initialization(at, m, type, direct, result);
    case type_kind::vector:
        /* [ext:gnu_vector_size] a vector is initialized from a vector of
           its type, a copy of its bytes (other conversions not analysed) */
        return same_type_copy(at, m, type, constness::not_constant, result);
    case type_kind::complex:
        /* [ext:gnu_complex] a complex value initialized from the same
           complex type */
        return same_type_copy(at, m, type, m.evaluation, result);
    default:
        return scalar_initialization(at, m, type, direct, list, result);
    }
}

/* an extension type initialized from an object of its own type, which is
   copied; another source type is not analysed */
analysis_result semantic_analysis::same_type_copy(SyntaxRef at,
                                                  expression_meaning &m,
                                                  qualified_type type,
                                                  constness evaluation,
                                                  initialized_value &result)
{
    if (m.type.unqualified() != type.unqualified())
        return not_supported_because(
            at, "dcl.init", "an extension type initialized from another type");
    VTRY(r, operand_value(at, m));
    result.evaluation = evaluation;
    result.value = 0;
    result.node = m.node;
    return r;
}

/* [dcl.init]/17.2: "If the destination type is a reference type, see
   [dcl.init.ref]." */
analysis_result
semantic_analysis::reference_initialization(SyntaxRef at, expression_meaning &m,
                                            qualified_type type,
                                            initialized_value &result)
{
    result.evaluation = constness::unknown;
    VTRY(r, reference_binding(at, m, type));
    /* the reference's own type: what lowering makes an address of */
    result.node = expression_node(semantic_kind::bind_reference, m,
                                  at.first_token(), m.node);
    semantics[result.node].type = type;
    return r;
}

/*
 * [dcl.init]/17.6: "Otherwise, if the destination type is a (possibly
 * cv-qualified) class type:
 * - (17.6.1) If the initializer expression is a prvalue and the
 *   cv-unqualified version of the source type is the same class as the
 *   class of the destination, the initializer expression is used to
 *   initialize the destination object.
 * - (17.6.2) Otherwise, if the initialization is direct-initialization, or
 *   if it is copy-initialization where the cv-unqualified version of the
 *   source type is the same class as, or a derived class of, the class of
 *   the destination, constructors are considered.
 * - (17.6.3) Otherwise (i.e., for the remaining copy-initialization
 *   cases), user-defined conversions that can convert from the source type
 *   to the destination type or (when a conversion function is used) to a
 *   derived class thereof are enumerated as described in [over.match.copy]".
 * Constructors are considered by construct when the class has any, or a
 * deleted implicit copy constructor; else the implicitly-declared copy
 * constructor ([class.copy.ctor]/6) is the one, which is trivial for those
 * classes ([class.copy.ctor]/11): it copies the object representation.
 */
analysis_result
semantic_analysis::class_initialization(SyntaxRef at, expression_meaning &m,
                                        qualified_type type, bool direct,
                                        initialized_value &result)
{
    /* [temp.inst]/2: the constructors initialization uses: a specialization
       named only is defined here */
    require_complete(type);
    require_complete(m.type);
    const struct type target = type_table[type.unqualified()];
    analysis_result r = analysis_result::well_formed;
    if (entity_table[target.entity].kind != entity_kind::class_type)
        return not_supported_because(
            at, "dcl.init", "initialization of a class that was not analysed");
    bool same = m.type.unqualified() == type.unqualified();
    bool related = false;
    if (type_table[m.type.unqualified()].kind == type_kind::class_type &&
        !same) {
        bool ambiguous = false;
        related = base_step(type_table[m.type.unqualified()].entity,
                            target.entity, ambiguous) != 0;
    }
    if (type_table[m.type.unqualified()].kind == type_kind::class_type &&
        !same && !related) {
        /* [dcl.init]/17.6.3, [over.match.copy]/1.2: the conversion
           functions of the source class are candidates too, and in a
           direct-initialization ([over.match.ctor]) a constructor's
           parameter may be initialized by one.  The one conversion
           function that yields the class, no converting constructor
           viable beside it, is called ([over.match.conv]), its result
           initializing the object; else not analysed */
        qualified_type unqualified =
            qualified_type::make(type.unqualified(), cv_none);
        conversion_sequence found;
        bool cannot_tell = false;
        if (conversion_function_to_class(m, unqualified, found, cannot_tell)) {
            conversion_sequence s = class_sequence(m, unqualified, true);
            /* before C++17 the result initializes the object by a copy
               constructor, which may be elided but shall be viable
               ([dcl.init]/17.6.3, [class.copy.elision]/1): not
               analysed */
            if (tokens.files.behavior.edition <
                    standard_edition::cplusplus_17 ||
                s.kind != conversion_sequence::form::user_defined ||
                s.ambiguous || s.function != found.function)
                return not_supported_because(
                    at, "over.match.copy",
                    "a conversion function in this copy-initialization");
            bool converted = false;
            VTRY_SET(r, conversion_function_call(at, m, unqualified, direct,
                                                 converted));
            if (!converted || m.type.unqualified() != type.unqualified())
                return not_supported_because(
                    at, "over.match.copy",
                    "a conversion function that does not yield the class");
            same = true;
        } else if (cannot_tell) {
            return not_supported_because(
                at, "over.match.copy",
                "conversion functions that cannot be decided");
        }
    }
    if (!(same && m.category == value_category::prvalue) &&
        (entity_table[target.entity].first_constructor ||
         /* its deleted implicit copy constructor (construct) */
         entity_table[target.entity].implicit_copy_deleted)) {
        /* /17.6.2 by [over.match.ctor]; /17.6.3: a user-defined
           conversion by a converting constructor ([class.conv.ctor]),
           whose result initializes the object (the copy elided) */
        owned_array<expression_meaning> arguments;
        arguments.append(m);
        return construct(at, type, arguments, 0, result, !direct);
    }
    bool ambiguous = false;
    if (!same &&
        type_table[m.type.unqualified()].kind == type_kind::class_type &&
        base_step(type_table[m.type.unqualified()].entity, target.entity,
                  ambiguous)) {
        /* the implicit copy constructor's const T& bound to the base
           class subobject of the derived object */
        if (!copies_bytes(target.entity))
            return not_supported_because(
                at, "class.copy.ctor",
                "a derived-to-base copy that is not a byte copy");
        VTRY_SET(r, to_base(at.first_token(), target.entity, m));
        same = true;
    }
    /* /17.6.3: "If the conversion cannot be done or is ambiguous, the
       initialization is ill-formed": a copy-initialization of a class
       with no constructor and no base class from an unrelated class none
       of whose conversion functions yields it (above), where a
       substitution is tried
       (is_assignable<S&, const U> of libc++ and json: false; elsewhere an
       overload candidate the analysis takes for this one may be the
       cause); other forms not analysed */
    if (!same && !direct && substituting &&
        type_table[m.type.unqualified()].kind == type_kind::class_type &&
        !entity_table[target.entity].base_count)
        return ill_formed(at,
                          "the initialization is ill-formed if the "
                          "conversion cannot be done",
                          "dcl.init");
    if (!same)
        return not_supported_because(
            at, "dcl.init", "a class initialized from this other type");
    VTRY_SET(r, operand_value(at, m));
    result.evaluation = constness::unknown;
    result.value = 0;
    result.node = m.node;
    return analysis_result::well_formed;
}

/*
 * [dcl.init]/17.7: "Otherwise, if the source type is a (possibly
 * cv-qualified) class type, conversion functions are considered"; /17.9:
 * "Otherwise, the initial value of the object being initialized is the
 * (possibly converted) value of the initializer expression.  A standard
 * conversion sequence ([conv]) will be used, if necessary, to convert the
 * initializer expression to the cv-unqualified version of the destination
 * type"; a narrowing conversion in a list-initialization is ill-formed
 * ([dcl.init.list]/3.9).  /17.8 (nullptr to bool) is implicit_conversion's.
 */
analysis_result
semantic_analysis::scalar_initialization(SyntaxRef at, expression_meaning &m,
                                         qualified_type type, bool direct,
                                         bool list, initialized_value &result)
{
    const struct type target = type_table[type.unqualified()];
    analysis_result r = analysis_result::well_formed;
    if (target.kind != type_kind::fundamental &&
        target.kind != type_kind::pointer &&
        target.kind != type_kind::pointer_to_member && !enumeration_of(type))
        return not_supported_because(
            at, "dcl.init", "a scalar initialization of this target type");
    if (type_table[m.type.unqualified()].kind == type_kind::class_type) {
        /* [dcl.init]/17.7: by a conversion function */
        bool converted = false;
        VTRY_SET(r, conversion_function_call(at, m, type, direct, converted));
    }
    if (target.kind == type_kind::fundamental &&
        target.fundamental == fundamental_type::void_)
        return not_supported_because(at, "dcl.init", "initialization of void");
    if (target.kind == type_kind::fundamental &&
        target.fundamental == fundamental_type::builtin_va_list) {
        /* [impl.clang] a va_list is initialized from a va_list only (a
           copy, as va_copy's) */
        return same_type_copy(at, m, type, constness::not_constant, result);
    }
    VTRY_SET(r, operand_value(at, m));
    if (list &&
        tokens.files.behavior.has(language_feature::list_initialization)) {
        bool narrows = false;
        VTRY_SET(r, narrowing(at, m, type, narrows));
        /* Microsoft's extensions: [ext.microsoft] clang accepts a narrowing
           conversion by the -fms-compatibility-version it emulates */
        if (narrows && tokens.files.behavior.microsoft_extensions)
            return not_supported_because(
                at, "dcl.init.list",
                "a narrowing conversion under Microsoft extensions");
        if (narrows) {
            VTRY_SET(r, extension_used(diagnostic_id::sema_narrowing,
                                       at.first_token(), nullptr));
        }
    }
    VTRY_SET(r, implicit_conversion(at, m, type, direct));
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
semantic_analysis::list_initialization(SyntaxRef list, qualified_type &type,
                                       bool direct, initialized_value &result)
{
    owned_array<SyntaxElement> clauses;
    owned_array<uint32_t> packs;
    designation_set designations;
    VTRY(gathered, list_clauses(list, clauses, &packs, &designations));
    /* the expansions are known to the array path only: another type's
       clauses are analysed without them */
    bool expanded = false;
    for (size_t i = 0; i < packs.size(); i++)
        if (packs[i] != no_expansion)
            expanded = true;
    /* (std::initializer_list<E> is initialized from its backing array,
       const E[N], by this list: the array path, [dcl.init.list]/5) */
    const struct type &target = type_table[type.unqualified()];
    bool initializer_list_target =
        target.kind == type_kind::class_type && target.entity &&
        named(target.entity, "initializer_list") &&
        std_namespace(entity_table[target.entity].enclosing);
    if (expanded && (designations.any || (target.kind != type_kind::array &&
                                          !initializer_list_target)))
        return not_supported_because(
            list, "temp.variadic",
            "a designated or non-array list with a pack expansion");
    const owned_array<uint32_t> *saved = clause_packs;
    clause_packs = nullptr;
    if (expanded)
        clause_packs = &packs;
    analysis_result r;
    if (designations.any)
        r = designated_list_initialization(list, clauses, designations, type,
                                           result);
    else
        r = list_initialization_of(list, clauses, type, direct, result);
    clause_packs = saved;
    return r;
}

void semantic_analysis::append_clause(
    SyntaxElement clause, const SyntaxElement *chain, uint32_t count,
    owned_array<SyntaxElement> &clauses, owned_array<uint32_t> *packs,
    uint32_t pack, designation_set *designated)
{
    clauses.append(clause);
    if (packs)
        packs->append(pack);
    if (!designated)
        return;
    clause_designation d;
    d.first = static_cast<uint32_t>(designated->pool.size());
    d.count = count;
    for (uint32_t i = 0; i < count; i++)
        designated->pool.append(chain[i]);
    designated->of.append(d);
    if (count)
        designated->any = true;
}

/*
 * The initializer-clauses among the children of node, in order.  A clause
 * followed by an ellipsis is [temp.variadic]/7 a pack expansion: its
 * pattern once for each element of the pack(s) it expands ([dcl.init.aggr]/
 * 4.2: the clauses of the list are those after expansion).  A designated
 * clause ([dcl.init.aggr]/3.1: ". identifier" and its initializer, or
 * [ext:gnu_designated_initializers]'s designation and clause) is the
 * clause of its initializer with its designators kept in designated; a
 * caller that gives none does not analyse designators.
 */
analysis_result semantic_analysis::gather_clauses(
    SyntaxRef list, SyntaxRef node, owned_array<SyntaxElement> &clauses,
    owned_array<uint32_t> *packs, designation_set *designated)
{
    size_t n = node.size();
    /* [ext:gnu_designated_initializers]: the designation before the clause
       that follows it */
    owned_array<SyntaxElement> pending;
    for (size_t k = 0; k < n; k++) {
        SyntaxElement c = node[static_cast<uint32_t>(k)];
        if (c.is(preprocessing_op_or_punc::LEFT_BRACE,
                 preprocessing_op_or_punc::RIGHT_BRACE,
                 preprocessing_op_or_punc::COMMA,
                 preprocessing_op_or_punc::ELLIPSIS))
            continue;
        if (c.is(syntax_kind::initializer_list,
                 syntax_kind::designated_initializer_list,
                 syntax_kind::gnu_initializer_list)) {
            VTRY(r,
                 gather_clauses(list, c.index(), clauses, packs, designated));
            continue;
        }
        if (c.is(syntax_kind::designated_initializer_clause)) {
            /* designator brace-or-equal-initializer: ". identifier" and
               "= initializer-clause", or a braced-init-list */
            if (!designated)
                return not_supported_because(
                    list, "dcl.init.aggr",
                    "a designator where none is allowed");
            SyntaxElement initializer = c[1];
            if (initializer.is(syntax_kind::brace_or_equal_initializer))
                initializer = initializer[1];
            SyntaxElement chain = c[0];
            append_clause(initializer, &chain, 1, clauses, packs, no_expansion,
                          designated);
            continue;
        }
        if (c.is(syntax_kind::gnu_designation)) {
            if (!designated)
                return not_supported_because(
                    list, "dcl.init.aggr",
                    "a GNU designator where none is allowed");
            pending.clear();
            for (uint32_t i = 0; i < c.size(); i++) {
                SyntaxElement d = c[i];
                if (d.is(syntax_kind::designator) || d.is_identifier())
                    pending.append(d);
            }
            continue;
        }
        /* (a lambda-expression is a primary-expression, [expr.prim.lambda];
           a simple-template-id an id-expression, [expr.prim.id]/1: a
           variable template's specialization, libc++'s
           {__trait<_Types, ...>...} in <variant>) */
        if (c.is_node() && !syntax_kind_is_expression(c.kind()) &&
            !c.is(syntax_kind::braced_init_list) &&
            !c.is(syntax_kind::lambda_expression) &&
            !c.is(syntax_kind::simple_template_id))
            return not_supported_because(
                list, "dcl.init.aggr",
                "an unexpected node in an initializer list");
        uint32_t count = 1;
        bool expansion = k + 1 < n && node[static_cast<uint32_t>(k + 1)].is(
                                          preprocessing_op_or_punc::ELLIPSIS);
        if (expansion) {
            if (!packs || !pending.empty() || c.is_token() ||
                !expansion_count(c.first_token(), c.end_token(), count))
                return not_supported_because(
                    list, "temp.variadic",
                    "a pack expansion in this initializer list");
        }
        for (uint32_t i = 0; i < count; i++) {
            uint32_t pack = no_expansion;
            if (expansion)
                pack = i;
            append_clause(c, pending.data(),
                          static_cast<uint32_t>(pending.size()), clauses, packs,
                          pack, designated);
        }
        pending.clear();
    }
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::list_clauses(
    SyntaxRef list, owned_array<SyntaxElement> &clauses,
    owned_array<uint32_t> *packs, designation_set *designated)
{
    return gather_clauses(list, list, clauses, packs, designated);
}

/*
 * [dcl.init.list]/3: "List-initialization of an object or reference of type
 * T is defined as follows:" (the designated-initializer-list of (3.1) is
 * designated_list_initialization's).  By T's kind, one function each:
 * - an array: (3.3) a string-literal, (3.4) aggregate initialization,
 *   array_list_initialization;
 * - a class: (3.2), (3.4) to (3.7), class_list_initialization;
 * - a reference: (3.9), (3.10), reference_list_initialization;
 * - a scalar: (3.8), (3.9), (3.11), (3.12), scalar_list_initialization.
 */
analysis_result semantic_analysis::list_initialization_of(
    SyntaxRef list, const owned_array<SyntaxElement> &clauses,
    qualified_type &type, bool direct, initialized_value &result)
{
    switch (type_table[type.unqualified()].kind) {
    case type_kind::array:
        return array_list_initialization(list, clauses, type, direct, result);
    case type_kind::vector:
        /* [ext:gnu_vector_size] the elements in order, the rest zero */
        return element_list_initialization(list, clauses, type, result);
    case type_kind::class_type:
        return class_list_initialization(list, clauses, type, direct, result);
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
        return reference_list_initialization(list, clauses, type, direct,
                                             result);
    default:
        return scalar_list_initialization(list, clauses, type, direct, result);
    }
}

/* [dcl.init.aggr]/4: the elements of an array (or a vector) initialized
   in order by the clauses, the rest as /5 says; too many clauses are
   ill-formed (/12) */
analysis_result semantic_analysis::element_list_initialization(
    SyntaxRef list, const owned_array<SyntaxElement> &clauses,
    qualified_type type, initialized_value &result)
{
    size_t next = 0;
    VTRY(r, aggregate_elements(list, clauses, next, type, result));
    semantics[result.node].type = type;
    return too_many_clauses(list, clauses, next);
}

/* [dcl.init.list]/3.3 and /3.4 for an array T */
analysis_result semantic_analysis::array_list_initialization(
    SyntaxRef list, const owned_array<SyntaxElement> &clauses,
    qualified_type &type, bool direct, initialized_value &result)
{
    const struct type target = type_table[type.unqualified()];
    /* [dcl.init.list]/3.3: "if T is a character array and the
       initializer list has a single element that is an
       appropriately-typed string-literal, initialization is performed
       as described in that subclause" */
    if (clauses.size() == 1 &&
        is_unparenthesized_string_literal(tokens, clauses[0]) &&
        character_type(fundamental_of(target.operand)))
        return initialize_from(list, clauses[0], type, direct, true, result);
    /* ... in parentheses: an extension */
    if (clauses.size() == 1 && !clauses[0].is_token() &&
        character_type(fundamental_of(target.operand))) {
        SyntaxElement inner = clauses[0];
        while (inner.is(syntax_kind::primary_expression) && inner.size() == 3)
            inner = inner[1];
        if (is_unparenthesized_string_literal(tokens, inner))
            return not_supported_because(
                list, "dcl.init.string",
                "a parenthesized string literal in braces");
    }
    /* [dcl.init.aggr]/9: "An array of unknown bound initialized with a
       brace-enclosed initializer-list containing n initializer-clauses
       is defined as having n elements" */
    uint64_t bound = target.bound;
    qualified_type element = target.operand;
    if (!target.bound_known) {
        bound = clauses.size();
        if (!bound) {
            VTRY(r, extension_used(diagnostic_id::sema_zero_length_array,
                                   list.first_token(), nullptr));
        }
        type = qualified_type::make(type_table.array_of(element, bound),
                                    type.cv());
    }
    if (target.bound_known || !clauses.size())
        return element_list_initialization(list, clauses, type, result);
    size_t next = 0;
    VTRY(r, aggregate_elements(list, clauses, next, type, result));
    /* the elements the clauses initialized, with brace elision */
    type = qualified_type::make(
        type_table.array_of(element, semantics[result.node].count), type.cv());
    semantics[result.node].type = type;
    return too_many_clauses(list, clauses, next);
}

/* [dcl.init.list]/3 for a scalar T: "(3.8) Otherwise, if T is an
   enumeration with a fixed underlying type ..."; "(3.9) Otherwise, if the
   initializer list has a single element of type E ..., the object ... is
   initialized from that element"; "(3.11) Otherwise, if the initializer
   list has no elements, the object is value-initialized"; "(3.12)
   Otherwise, the program is ill-formed." */
analysis_result semantic_analysis::scalar_list_initialization(
    SyntaxRef list, const owned_array<SyntaxElement> &clauses,
    qualified_type type, bool direct, initialized_value &result)
{
    const struct type target = type_table[type.unqualified()];
    /* (3.8), an enumeration from one element, is not analysed */
    bool empty_enumeration =
        target.kind == type_kind::enumeration && clauses.empty();
    if (target.kind != type_kind::fundamental &&
        target.kind != type_kind::pointer &&
        target.kind != type_kind::pointer_to_member && !empty_enumeration)
        return not_supported_because(list, "dcl.init.list",
                                     "list-initialization of this type");
    if (clauses.empty()) {
        /* (3.11) value-initialization; [dcl.init]/8: to value-initialize a scalar is to zero-initialize
           it: the value 0, a null pointer, a null member pointer (whose
           value analysis does not keep) */
        result.evaluation = constness::constant;
        if (target.kind == type_kind::pointer_to_member)
            result.evaluation = constness::unknown;
        result.value = 0;
        expression_meaning zero;
        zero.type = qualified_type::make(type.unqualified(), cv_none);
        zero.evaluation = result.evaluation;
        result.node =
            expression_node(semantic_kind::zero, zero, list.first_token());
        return analysis_result::well_formed;
    }
    /* (3.12) */
    if (clauses.size() > 1)
        return ill_formed(list,
                          "a scalar is list-initialized from a single "
                          "element, or from no element; otherwise, the "
                          "program is ill-formed",
                          "dcl.init.list");
    if (clauses[0].is(syntax_kind::braced_init_list))
        /* braces around a scalar's initializer: clang's warning */
        return not_supported_because(
            list, "dcl.init.list", "extra braces around a scalar initializer");
    /* (3.9) */
    return initialize_from(list, clauses[0], type, direct, true, result);
}

/*
 * [dcl.init.list]/3 for a reference: "(3.9) Otherwise, if the initializer
 * list has a single element of type E and either T is not a reference type
 * or its referenced type is reference-related to E, the object or reference
 * is initialized from that element (by copy-initialization for copy-list-
 * initialization, or by direct-initialization for direct-list-
 * initialization)"; "(3.10) Otherwise, if T is a reference type, a prvalue
 * is generated.  The prvalue initializes its result object by copy-list-
 * initialization.  The prvalue is then used to direct-initialize the
 * reference.  The type of the temporary is the type referenced by T".
 * A reference to an array of unknown bound is not analysed.
 */
analysis_result semantic_analysis::reference_list_initialization(
    SyntaxRef list, const owned_array<SyntaxElement> &clauses,
    qualified_type type, bool direct, initialized_value &result)
{
    qualified_type referenced = type_table[type.unqualified()].operand;
    const struct type x = type_table[referenced.unqualified()];
    if (x.kind == type_kind::array && !x.bound_known)
        return not_supported_because(
            list, "dcl.init.list",
            "a reference to an array of unknown bound from a list");
    qualified_type prvalue_type =
        qualified_type::make(referenced.unqualified(), cv_none);
    expression_meaning m;
    m.type = prvalue_type;
    m.category = value_category::prvalue;
    initialized_value made;
    if (clauses.size() == 1 && !clauses[0].is(syntax_kind::braced_init_list)) {
        expression_meaning e;
        VTRY(r, target_expression(clauses[0], type, e));
        bool ambiguous = false;
        bool related = reference_related(referenced, e.type, ambiguous);
        if (related)
            /* (3.9): initialized from the element */
            return initialize_with(list, e, type, direct, true, result);
        if (x.kind != type_kind::class_type && x.kind != type_kind::array) {
            /* (3.10): the prvalue initialized from the element, analysed
               already (narrowing checked) */
            VTRY(r2, initialize_with(list, e, prvalue_type, false, true, made));
            m.evaluation = made.evaluation;
            m.value = made.value;
            m.node = made.node;
            return initialize_with(list, m, type, true, false, result);
        }
        /* a class or an array initialized from it: the clause is analysed
           again by the list-initialization of the prvalue */
    }
    /* [dcl.init.list]/3.10 [impl.clang] as clang has it (and as C++11 to C++17
       worded it): by copy-list-initialization or direct-list-initialization,
       depending on the kind of initialization for the reference; the text since
       CWG 2267 always copies */
    VTRY(r, list_initialization_of(list, clauses, prvalue_type, direct, made));
    m.evaluation = made.evaluation;
    m.value = made.value;
    m.node = made.node;
    return initialize_with(list, m, type, true, false, result);
}

/*
 * The initializer of a declarator: [dcl.init]/17.1: "If the initializer is
 * a (non-parenthesized) braced-init-list or is = braced-init-list, the
 * object or reference is list-initialized"; [dcl.init]/15: "The
 * initialization that occurs in the = form of a brace-or-equal-initializer
 * ... is called copy-initialization"; /16: "The initialization that occurs
 * ... in the forms T x(a); T x{a}; ... is called direct-initialization."
 */
analysis_result semantic_analysis::initializer(SyntaxElement init,
                                               qualified_type &type,
                                               initialized_value &result)
{
    if (init.is_token())
        return not_supported_because(current_declaration, "dcl.init",
                                     "an initializer that is a single token");
    /* [temp.spec]/6: a static data member or variable template
       initializer is checked */
    flag_set checked(explicit_declaration_names, false);
    InitializerParts parts(init);
    switch (parts.form) {
    case InitializerParts::syntax_form::braced:
        return list_initialization(parts.clause, type, true, result);
    case InitializerParts::syntax_form::equals:
        if (SyntaxElement list = parts.braced_list())
            return list_initialization(list, type, false, result);
        return initialize_from(init, parts.clause, type, false, false, result);
    case InitializerParts::syntax_form::parenthesized:
        if (!parts.list)
            return not_supported_because(
                init, "dcl.init",
                "a parenthesized initializer with no expressions");
        /* a braced-init-list argument of a constructor ([over.best.ics]/1:
           its sequence is [over.ics.list]'s), kept as written.  (Not an
           empty one: the ambiguity of B b({}) with the copy constructor,
           CWG1902, is not told) */
        if (!parts.list_of_several() &&
            parts.list.is(syntax_kind::braced_init_list) &&
            parts.list.end_token() > parts.list.first_token() + 2 &&
            type_table[type.unqualified()].kind == type_kind::class_type) {
            entity_index c = type_table[type.unqualified()].entity;
            require_complete(c);
            if (entity_table[c].kind == entity_kind::class_type &&
                entity_table[c].first_constructor) {
                owned_array<expression_meaning> arguments;
                owned_array<SyntaxElement> written;
                VTRY(r,
                     expression_list_meanings(parts.list, arguments, &written));
                return construct(init, type, arguments, 0, result, false,
                                 &written);
            }
        }
        if (parts.list_of_several()) {
            if (has_pack_expansion(parts.list))
                return not_supported_because(
                    init, "temp.variadic",
                    "a pack expansion in a parenthesized initializer");
            const struct type target = type_table[type.unqualified()];
            if (target.kind == type_kind::array)
                return not_supported_because(
                    init, "dcl.init",
                    "an array initialized from several parenthesized values");
            if (target.kind == type_kind::class_type) {
                /* [dcl.init]/17.6.2: the constructor called with the
                   expression-list as its arguments */
                if (entity_table[target.entity].kind !=
                        entity_kind::class_type ||
                    !entity_table[target.entity].first_constructor)
                    return not_supported_because(
                        init, "dcl.init",
                        "several arguments for a class without constructors");
                owned_array<expression_meaning> arguments;
                owned_array<SyntaxElement> written;
                VTRY(r,
                     expression_list_meanings(parts.list, arguments, &written));
                return construct(init, type, arguments, 0, result, false,
                                 &written);
            }
            return ill_formed(init,
                              "if the entity being initialized does not have "
                              "class type, the expression-list in a "
                              "parenthesized initializer shall be a single "
                              "expression",
                              "dcl.init");
        }
        return initialize_from(init, parts.list, type, true, false, result);
    case InitializerParts::syntax_form::none:
        break;
    }
    return not_supported_because(init, "dcl.init",
                                 "an initializer of no known form");
}

bool semantic_analysis::has_user_constructor_template(entity_index c) const
{
    entity_index held = entity_table[c].constructor_templates;
    if (!held)
        return false;
    for (const function_template &t : function_templates)
        if (t.name == held && !entity_table[t.entity].deleted &&
            !(t.definition && t.definition.size() &&
              deleted_body(t.definition[t.definition.size() - 1])))
            return true;
    return false;
}

/*
 * [dcl.init.list]/3.5: "if the initializer list has no elements and T is a
 * class type with a default constructor, the object is value-initialized";
 * without one, /3.7 and [over.match.list]/1.1: the constructors taking a
 * std::initializer_list are candidates with the empty list.
 */
bool semantic_analysis::empty_list_uses_constructors(entity_index c) const
{
    /* std::initializer_list<E> itself: [dcl.init.list]/3.6 */
    if (named(c, "initializer_list") &&
        std_namespace(entity_table[c].enclosing))
        return true;
    bool initializer_list_constructor = false;
    for (entity_index k = entity_table[c].first_constructor; k;
         k = entity_table[k].next_constructor) {
        const entity &x = entity_table[k];
        if (x.deleted)
            continue;
        const struct type &ft = type_table[x.type.unqualified()];
        if (ft.parameter_count == x.default_arguments)
            return false;
        qualified_type first = type_table.parameters(x.type.unqualified())[0];
        const struct type *p = &type_table[first.unqualified()];
        if (p->kind == type_kind::lvalue_reference ||
            p->kind == type_kind::rvalue_reference)
            p = &type_table[p->operand.unqualified()];
        if (p->kind == type_kind::class_type &&
            named(p->entity, "initializer_list") &&
            std_namespace(entity_table[p->entity].enclosing))
            initializer_list_constructor = true;
    }
    return initializer_list_constructor;
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
    SyntaxRef list, const owned_array<SyntaxElement> &clauses,
    qualified_type type, bool direct, initialized_value &result)
{
    entity_index c = type_table[type.unqualified()].entity;
    /* [temp.inst]/2: a specialization named where no complete type was
       needed is defined by the initialization of an object of it */
    require_complete(c);
    if (entity_table[c].kind != entity_kind::class_type)
        return not_supported_because(
            list, "class.pre",
            "list-initialization of a class that was not analysed");
    /* [dcl.init.list]/3.6: "Otherwise, if T is a specialization of
       std::initializer_list<E>, the object is constructed as described
       below" */
    if (named(c, "initializer_list") &&
        std_namespace(entity_table[c].enclosing)) {
        if (!entity_table[c].instantiation)
            return not_supported_because(
                list, "dcl.init.list",
                "an initializer_list that is no specialization");
        const instantiation_record &inst =
            instantiations[entity_table[c].instantiation - 1];
        if (inst.argument_count < 1 ||
            template_arguments[inst.first_argument].kind !=
                template_argument::kind_t::type)
            return not_supported_because(
                list, "dcl.init.list",
                "an initializer_list with no element type argument");
        qualified_type elem = template_arguments[inst.first_argument].type;
        owned_array<entity_index> fields;
        for (uint32_t i = 0; i < entity_table[c].member_count; i++) {
            entity_index m = class_members[entity_table[c].first_member + i];
            if (entity_table[m].kind == entity_kind::variable &&
                entity_table[m].nonstatic_member)
                fields.append(m);
        }
        if (fields.size() != 2)
            return not_supported_because(
                list, "dcl.init.list",
                "an initializer_list without two data members");
        qualified_type f0 = entity_table[fields[0]].type;
        qualified_type f1 = entity_table[fields[1]].type;
        if (type_table[f0.unqualified()].kind != type_kind::pointer)
            return not_supported_because(
                list, "dcl.init.list",
                "an initializer_list whose first member is no pointer");
        bool second_is_int = is_integral(f1);
        if (!second_is_int &&
            type_table[f1.unqualified()].kind != type_kind::pointer)
            return not_supported_because(
                list, "dcl.init.list",
                "an initializer_list with an unknown second member");
        token_index at = list.first_token();
        uint32_t first_node = 0;
        uint32_t second_node = 0;
        if (clauses.empty()) {
            expression_meaning nullp;
            nullp.type = f0;
            nullp.category = value_category::prvalue;
            nullp.evaluation = constness::constant;
            nullp.value = 0;
            first_node = expression_node(semantic_kind::constant, nullp, at);
            expression_meaning zero;
            zero.type = f1;
            zero.category = value_category::prvalue;
            zero.evaluation = constness::constant;
            zero.value = 0;
            second_node = expression_node(semantic_kind::constant, zero, at);
        } else {
            qualified_type elem_const =
                qualified_type::make(elem.unqualified(), elem.cv() | cv_const);
            qualified_type arr_type = qualified_type::make(
                type_table.array_of(elem_const, clauses.size()), cv_none);
            initialized_value arr_init;
            VTRY(r, list_initialization(list, arr_type, false, arr_init));
            expression_meaning arr_obj;
            arr_obj.type = arr_type;
            arr_obj.category = value_category::lvalue;
            arr_obj.evaluation = constness::not_constant;
            uint32_t cl = expression_node(semantic_kind::compound_literal,
                                          arr_obj, at, arr_init.node);
            expression_meaning decayed;
            decayed.type = f0;
            decayed.category = value_category::prvalue;
            decayed.evaluation = constness::not_constant;
            first_node =
                expression_node(semantic_kind::conversion, decayed, at, cl);
            if (second_is_int) {
                expression_meaning sz;
                sz.type = f1;
                sz.category = value_category::prvalue;
                sz.evaluation = constness::constant;
                sz.value =
                    static_cast<uint64_t>(static_cast<int64_t>(clauses.size()));
                second_node = expression_node(semantic_kind::constant, sz, at);
            } else {
                return not_supported_because(
                    list, "dcl.init.list",
                    "an initializer_list with a pointer end member");
            }
        }
        owned_array<uint32_t> items;
        items.append(first_node);
        items.append(second_node);
        expression_meaning out;
        out.type = type;
        out.category = value_category::prvalue;
        out.evaluation = constness::not_constant;
        result.node = expression_node(semantic_kind::initializer_list, out, at);
        semantics.set_list(result.node, items.data(), items.size());
        result.evaluation = constness::not_constant;
        return analysis_result::well_formed;
    }
    require_complete(c);
    if (!entity_table[c].size)
        /* [dcl.init.list]/3: list-initialization of an incomplete class:
           clang's "incomplete type" error */
        return not_supported_because(
            list, "dcl.init.list",
            "list-initialization of an incomplete class");
    if (!is_aggregate(c)) {
        /* (3.5): "Otherwise, if the initializer list has no elements and T
           is a class type with a default constructor, the object is
           value-initialized"; [dcl.init]/8.1: "if T is a (possibly
           cv-qualified) class type with ... a user-provided ... default
           constructor, then the object is default-initialized" */
        if (clauses.empty() && entity_table[c].first_constructor &&
            empty_list_uses_constructors(c))
            return constructor_list_initialization(list, clauses, type, direct,
                                                   result);
        if (clauses.empty() && entity_table[c].first_constructor) {
            owned_array<expression_meaning> none;
            analysis_result r = construct(list, type, none, 0, result);
            if (r == analysis_result::well_formed && !direct &&
                semantics[result.node].kind == semantic_kind::construct &&
                entity_table[semantics[result.node].b].explicit_)
                /* [over.match.list]/1 */
                return ill_formed(list,
                                  "In copy-list-initialization, if an explicit "
                                  "constructor is chosen, the initialization "
                                  "is ill-formed",
                                  "over.match.list");
            return r;
        }
        /* (3.5) without a user-provided default constructor (X() =
           default, or an empty base class or a private member making it
           no aggregate): [dcl.init]/8.2: "the object is zero-initialized
           and ... if T has a non-trivial default constructor, the object
           is default-initialized", the elements as an empty list
           initializes an aggregate's ([dcl.init.aggr]/5); an empty base
           class subobject ([class.derived]/4), no element, only when
           trivial */
        const entity &x = entity_table[c];
        bool trivial_empty_bases = true;
        for (uint32_t i = 0; i < x.base_count; i++) {
            const entity &b = entity_table[class_bases[x.first_base + i]];
            if (b.field == UINT32_MAX &&
                (b.virtual_base ||
                 !trivial_special_members(
                     type_table[b.type.unqualified()].entity)))
                trivial_empty_bases = false;
        }
        if (clauses.empty() && !x.first_constructor && !x.dynamic &&
            trivial_empty_bases && !x.constructor_template &&
            !x.constructor_templates) {
            if (!direct && x.explicit_default_constructor)
                /* [over.match.list]/1 */
                return ill_formed(list,
                                  "In copy-list-initialization, if an explicit "
                                  "constructor is chosen, the initialization "
                                  "is ill-formed",
                                  "over.match.list");
            size_t next = 0;
            return aggregate_elements(list, clauses, next, type, result);
        }
        if (clauses.empty() || !entity_table[c].first_constructor)
            return not_supported_because(
                list, "over.match.list",
                "list-initialization of a non-aggregate with no constructor "
                "match");
        return constructor_list_initialization(list, clauses, type, direct,
                                               result);
    }
    if (clauses.size() == 1 && !clauses[0].is(syntax_kind::braced_init_list)) {
        /* (3.2): an element of the class's own type or of a class derived
           from it (CWG 1467) */
        expression_meaning e;
        VTRY(r, expression(clauses[0], e));
        if (e.type.unqualified() == type.unqualified() ||
            same_or_derived_class(e, c))
            return initialize_with(list, e, type, false, true, result);
        /* an element initialized from it: analysed again below */
    }
    size_t next = 0;
    VTRY(r, aggregate_elements(list, clauses, next, type, result));
    return too_many_clauses(list, clauses, next);
}

/*
 * [dcl.init.list]/3.7: "Otherwise, if T is a class type, constructors are
 * considered.  The applicable constructors are enumerated and the best one
 * is chosen through overload resolution ([over.match], [over.match.list]).
 * If a narrowing conversion (see below) is required to convert any of the
 * arguments, the program is ill-formed."  [over.match.list]/1.2: without
 * an initializer-list constructor, "the candidate functions are all the
 * constructors of the class T and the argument list consists of the
 * elements of the initializer list".  A constructor taking a class named
 * initializer_list (3.6), and braced elements, are not analysed; explicit
 * constructors are candidates, ill-formed when chosen in copy-list-
 * initialization ([over.match.list]/1).
 */
analysis_result semantic_analysis::constructor_list_initialization(
    SyntaxRef list, const owned_array<SyntaxElement> &clauses,
    qualified_type type, bool direct, initialized_value &result)
{
    entity_index c = type_table[type.unqualified()].entity;
    /* [over.match.list]/1.1: first phase: initializer-list constructors */
    owned_array<entity_index> ilist_ctors;
    owned_array<qualified_type> ilist_types;
    for (entity_index k = entity_table[c].first_constructor; k;
         k = entity_table[k].next_constructor) {
        const struct type &f = type_table[entity_table[k].type.unqualified()];
        if (!f.parameter_count)
            continue;
        if (f.parameter_count > 1 &&
            entity_table[k].default_arguments < f.parameter_count - 1)
            continue;
        qualified_type first =
            type_table.parameters(entity_table[k].type.unqualified())[0];
        const struct type &p = type_table[first.unqualified()];
        if (p.kind == type_kind::lvalue_reference ||
            p.kind == type_kind::rvalue_reference)
            first = p.operand;
        const struct type &p0 = type_table[first.unqualified()];
        if (p0.kind == type_kind::class_type && p0.entity != c &&
            named(p0.entity, "initializer_list") &&
            std_namespace(entity_table[p0.entity].enclosing)) {
            ilist_ctors.append(k);
            ilist_types.append(
                qualified_type::make(first.unqualified(), cv_none));
        }
    }
    if (ilist_ctors.size() == 1) {
        qualified_type ilist_type = ilist_types[0];
        initialized_value ilist_val;
        substitution_mark mark = mark_substitution();
        substituting++;
        analysis_result ir = class_list_initialization(
            list, clauses, ilist_type, false, ilist_val);
        substituting--;
        if (ir == analysis_result::well_formed) {
            expression_meaning arg;
            arg.type = ilist_type;
            arg.category = value_category::prvalue;
            arg.evaluation = ilist_val.evaluation;
            arg.node = ilist_val.node;
            owned_array<expression_meaning> one;
            one.append(arg);
            return construct(list, type, one, 0, result, !direct);
        }
        if (ir != analysis_result::ill_formed || !undo_substitution(mark))
            return ir;
    } else if (ilist_ctors.size() > 1) {
        return not_supported_because(list, "over.match.list",
                                     "several initializer_list constructors");
    }
    owned_array<expression_meaning> arguments;
    for (SyntaxElement clause : clauses) {
        expression_meaning e;
        if (!clause.is(syntax_kind::braced_init_list)) {
            VTRY(r, expression(clause, e));
        }
        arguments.append(e);
    }
    if (arguments.size() == 1 &&
        !clauses[0].is(syntax_kind::braced_init_list) &&
        arguments[0].category == value_category::prvalue &&
        arguments[0].type.unqualified() == type.unqualified())
        /* one prvalue of the class itself: clang initializes the object
           from it with no constructor (CWG2311), the copy or move one
           deleted or not */
        return not_supported_because(list, "dcl.init.list",
                                     "a list of one prvalue of the same class");
    /* the arguments before the conversions overload resolution adds */
    owned_array<expression_meaning> before;
    for (const expression_meaning &e : arguments)
        before.append(e);
    VTRY(r, construct(list, type, arguments, 0, result, false, &clauses));
    entity_index chosen = semantics[result.node].b;
    if (!chosen || semantics[result.node].kind != semantic_kind::construct)
        return not_supported_because(list, "over.match.list",
                                     "no constructor chosen for the list");
    if (!direct && entity_table[chosen].explicit_)
        /* [over.match.list]/1 */
        return ill_formed(
            list,
            "In copy-list-initialization, if an explicit constructor is "
            "chosen, the initialization is ill-formed",
            "over.match.list");
    const qualified_type *parameters =
        type_table.parameters(entity_table[chosen].type.unqualified());
    uint32_t count =
        type_table[entity_table[chosen].type.unqualified()].parameter_count;
    for (size_t i = 0; i < before.size() && i < count; i++) {
        if (clauses[i].is(syntax_kind::braced_init_list))
            continue;
        qualified_type to = parameters[i];
        const struct type &p = type_table[to.unqualified()];
        bool reference = p.kind == type_kind::lvalue_reference ||
                         p.kind == type_kind::rvalue_reference;
        if (reference)
            to = p.operand;
        if (type_table[before[i].type.unqualified()].kind ==
                type_kind::class_type ||
            type_table[to.unqualified()].kind == type_kind::class_type)
            continue;
        SyntaxRef at = list;
        if (!clauses[i].is_token())
            at = clauses[i].index();
        expression_meaning value = before[i];
        VTRY_SET(r, operand_value(at, value));
        bool narrows = false;
        VTRY_SET(r, narrowing(at, value, to, narrows));
        /* [impl.clang] into a reference's temporary: its own warning
           group (-Wc++11-narrowing-const-reference) */
        if (narrows &&
            (reference || tokens.files.behavior.microsoft_extensions))
            return not_supported_because(
                at, "dcl.init.list",
                "narrowing into a reference or under Microsoft extensions");
        if (narrows) {
            VTRY_SET(r, extension_used(diagnostic_id::sema_narrowing,
                                       at.first_token(), nullptr));
        }
    }
    return analysis_result::well_formed;
}

/* [dcl.init.aggr]/11: "An initializer-list is ill-formed if the number of
   initializer-clauses exceeds the number of elements of the aggregate." */
analysis_result semantic_analysis::too_many_clauses(
    SyntaxRef list, const owned_array<SyntaxElement> &clauses, size_t next)
{
    if (next < clauses.size())
        return ill_formed(list,
                          "an initializer-list is ill-formed if the number of "
                          "initializer-clauses exceeds the number of elements "
                          "of the aggregate",
                          "dcl.init.aggr");
    return analysis_result::well_formed;
}

/* whether the initializer init has only literals (true, false, nullptr; not string or
   user-defined: [lex.string], [over.literal]) and operators or
   punctuators among its tokens: it looks up no name and has no this */
bool semantic_analysis::initializer_names_nothing(SyntaxRef init) const
{
    for (token_index t = init.first_token(); t < init.end_token(); t++) {
        const struct token &k = tokens[t];
        if (k.kind == token_kind::operator_or_punctuator)
            continue;
        if (k.kind == token_kind::literal &&
            k.literal != literal_kind::string_literal &&
            k.literal != literal_kind::user_defined_literal)
            continue;
        if (k.kind == token_kind::keyword) {
            standard_keyword w = this->syntax_parser.keyword_of(k);
            if (w == standard_keyword::true_ || w == standard_keyword::false_ ||
                w == standard_keyword::nullptr_)
                continue;
        }
        return false;
    }
    return true;
}

/*
 * [dcl.init.aggr]/1: "The elements of an aggregate are: ... for a class,
 * the direct base classes in declaration order, followed by the direct
 * non-static data members ([class.mem]) that are not members of an
 * anonymous union, in declaration order"; [class.bit]/2: "Unnamed
 * bit-fields are not members".  A base class that is no field (an empty
 * one, [class.derived]/4) is an element with no place in the object's
 * fields (empty_base); a non-empty one is the field it is.
 */
void semantic_analysis::aggregate_class_elements(
    entity_index c, owned_array<aggregate_class_element> &out) const
{
    const entity &x = entity_table[c];
    for (uint32_t i = 0; i < x.base_count; i++) {
        const entity &b = entity_table[class_bases[x.first_base + i]];
        aggregate_class_element e;
        e.type = b.type;
        e.empty_base = b.field == UINT32_MAX;
        if (!e.empty_base) {
            e.member = class_members[x.first_member + b.field];
            e.type = entity_table[e.member].type;
        }
        out.append(e);
    }
    for (uint32_t i = 0; i < x.member_count; i++) {
        entity_index k = class_members[x.first_member + i];
        const entity &m = entity_table[k];
        if (m.base_subobject || (m.bit_field && !m.name))
            continue;
        aggregate_class_element e;
        e.type = m.type;
        e.default_initializer = m.default_initializer;
        e.member = k;
        out.append(e);
    }
}

/*
 * The elements of the aggregate type (an array or an aggregate class),
 * initialized from clauses[next ..], next moved past the clauses used.
 * [dcl.init.aggr]/1: "The elements of an aggregate are: for an array, the
 * array elements in increasing subscript order", for a class
 * aggregate_class_elements.  /3.2: "the explicitly initialized elements of the aggregate are the first
 * n elements"; /5: the others are value-initialized (zero here: the
 * elements analysis has).  /16: "When a union is initialized with an
 * initializer list, there shall not be more than one explicitly initialized
 * element": without designators, the first.
 */
analysis_result semantic_analysis::aggregate_elements(
    SyntaxRef list, const owned_array<SyntaxElement> &clauses, size_t &next,
    qualified_type type, initialized_value &result)
{
    const struct type target = type_table[type.unqualified()];
    /* [ext:gnu_vector_size] a vector's elements are listed as an array's */
    bool arraylike =
        target.kind == type_kind::array || target.kind == type_kind::vector;
    owned_array<aggregate_class_element> members;
    uint64_t count = 0;
    size_t union_default = 0, first_element = 0;
    bool unknown_bound = false;
    if (arraylike) {
        count = target.bound;
        unknown_bound = target.kind == type_kind::array && !target.bound_known;
    } else {
        const entity &x = entity_table[target.entity];
        aggregate_class_elements(target.entity, members);
        count = members.size();
        if (x.union_ && count > 1)
            count = 1;
        /* [dcl.init.aggr]/5.4.2 (C++20 /5.4): an empty list initializes a
           union's member that has a default member initializer, not its
           first member: that member is the active one, as a designator
           makes it */
        if (x.union_ && next >= clauses.size())
            for (size_t i = 1; i < members.size() && !union_default; i++)
                if (members[i].default_initializer &&
                    !members[0].default_initializer)
                    union_default = i;
        if (union_default) {
            first_element = union_default;
            count = union_default + 1;
        }
    }
    owned_array<uint32_t> elements;
    constness all = constness::constant;
    for (uint64_t i = first_element; i < count || unknown_bound; i++) {
        if (unknown_bound && next == clauses.size())
            break;
        qualified_type element = target.operand;
        if (!arraylike)
            element = members[i].type;
        initialized_value one;
        bool has_default = !arraylike && members[i].default_initializer;
        if (!arraylike && members[i].empty_base) {
            VTRY(r, empty_base_element(list, clauses, next, element));
            continue;
        }
        if (next >= clauses.size()) {
            VTRY(r, omitted_element_checks(list, element, has_default));
        }
        if (next < clauses.size()) {
            VTRY(r, aggregate_element(list, clauses, next, element, one));
        } else if (arraylike &&
                   !(type_table[element.unqualified()].kind ==
                         type_kind::class_type &&
                     entity_table[type_table[element.unqualified()].entity]
                             .kind == entity_kind::class_type &&
                     (entity_table[type_table[element.unqualified()].entity]
                          .first_constructor ||
                      destroys(type_table[element.unqualified()].entity)))) {
            /* the rest of an array: zero, as the lowering fills it */
            break;
        } else {
            SyntaxRef default_initializer;
            if (!arraylike)
                default_initializer = members[i].default_initializer;
            VTRY(r, omitted_element_value(list, element, default_initializer,
                                          target.entity, one));
        }
        all = combined_constness(all, one.evaluation);
        elements.append(one.node);
        /* [dcl.init.aggr]/8: "The destructor for each element of class type
           is potentially invoked ([class.dtor]) from the context where the
           aggregate initialization occurs": a deleted or inaccessible one
           is ill-formed ([dcl.fct.def.delete]/2, [class.access]) */
        qualified_type destroyed = element;
        while (type_table[destroyed.unqualified()].kind == type_kind::array)
            destroyed = type_table[destroyed.unqualified()].operand;
        if (type_table[destroyed.unqualified()].kind == type_kind::class_type) {
            entity_index sub = type_table[destroyed.unqualified()].entity;
            if (entity_table[sub].kind == entity_kind::class_type &&
                (destructor_deleted(sub) ||
                 (entity_table[sub].destructor &&
                  !accessible(entity_table[sub].destructor))))
                return ill_formed(list,
                                  "the destructor of an element of an "
                                  "aggregate is potentially invoked by its "
                                  "initialization and it is deleted or "
                                  "inaccessible",
                                  "dcl.init.aggr");
        }
    }
    aggregate_value(list, type, all, elements, result);
    if (union_default)
        semantics[result.node].a = members[union_default].member;
    return analysis_result::well_formed;
}

/*
 * [dcl.init.aggr]/2: the direct base classes are elements of an aggregate;
 * [class.derived]/4 an empty one has no storage, so no field holds its
 * value: the element is initialized as any ([dcl.init.aggr]/4.2, /5), and
 * the object it makes is nothing the program can tell from any other of the
 * class, as long as making it does nothing: a closure without captures, a
 * class with no constructor of its own and no destructor, from a braced
 * list, a conversion or an object (a clause that calls a function to make
 * it is not analysed).
 */
analysis_result
semantic_analysis::empty_base_element(SyntaxRef list,
                                      const owned_array<SyntaxElement> &clauses,
                                      size_t &next, qualified_type element)
{
    entity_index base = type_table[element.unqualified()].entity;
    if (!trivial_special_members(base))
        return not_supported_because(
            list, "dcl.init.aggr",
            "an empty base with non-trivial special members");
    if (next >= clauses.size()) {
        /* /5.2: from an empty initializer list */
        return omitted_element_checks(list, element, false);
    }
    initialized_value one;
    VTRY(r, aggregate_element(list, clauses, next, element, one));
    if (!makes_no_effect(one.node))
        return not_supported_because(
            list, "dcl.init.aggr",
            "an empty base initializer that has an effect");
    return analysis_result::well_formed;
}

/* whether evaluating the expression of an empty class does nothing: the
   shapes empty_base_element accepts */
bool semantic_analysis::makes_no_effect(uint32_t n) const
{
    const semantic_node &x = semantics[n];
    switch (x.kind) {
    case semantic_kind::constant:
    case semantic_kind::zero:
    case semantic_kind::entity:
        return true;
    case semantic_kind::initializer_list:
        for (uint32_t i = 0; i < x.count; i++)
            if (!makes_no_effect(semantics.items(x.first)[i]))
                return false;
        return true;
    case semantic_kind::conversion:
        return makes_no_effect(x.a);
    default:
        return false;
    }
}

/* [dcl.init.aggr]/5.2 and 5.3: an element no clause initializes, which is
   left out of the list: of a class with constructors, or of reference type
   without a default member initializer, analysis has no rules (or the
   program is ill-formed) */
analysis_result semantic_analysis::omitted_element_checks(
    SyntaxRef list, qualified_type element, bool has_default)
{
    const struct type of_element = type_table[element.unqualified()];
    if ((of_element.kind == type_kind::lvalue_reference ||
         of_element.kind == type_kind::rvalue_reference) &&
        !has_default)
        /* (5.2): "If the element is not a reference, the element is
           copy-initialized from an empty initializer list"; (5.3):
           "Otherwise, the program is ill-formed" */
        return ill_formed(list,
                          "an element of reference type that is not "
                          "explicitly initialized and has no default "
                          "member initializer makes the program "
                          "ill-formed",
                          "dcl.init.aggr");
    return analysis_result::well_formed;
}

/* [dcl.init.aggr]/5.1, [class.mem]/6: a default member initializer that
   names members is a complete-class context whose `this` is the aggregate
   being initialized: analysed in the class's scope with a `this` of its
   type (a variable of the class, which the lowering binds to the
   aggregate), as a constructor analyses it ([class.base.init]/9.1,
   default_member_initializer), and the value is not a constant, which the
   evaluator has no object for */
analysis_result semantic_analysis::member_default_in_aggregate(
    SyntaxRef list, entity_index owner, SyntaxRef default_initializer,
    qualified_type member_type, initialized_value &one)
{
    if (!owner || entity_table[owner].kind != entity_kind::class_type)
        return not_supported_because(
            list, "dcl.init.aggr", "a default member initializer of no class");
    /* (an initializer whose value needs its own: clang's "default member
       initializer needed within definition of enclosing class" when the
       class is not complete yet) */
    for (SyntaxRef d : member_defaults_analysed)
        if (d == default_initializer)
            return not_supported_because(
                list, "dcl.init.aggr",
                "a default member initializer that needs itself");
    /* (in a function body: the lowering has no rule for the
       default_member_initializer node, whose `this` is the aggregate) */
    /* (in a default argument ([dcl.fct.default]/1): the lowering of the
       call that uses it has no object for its `this`) */
    /* (in a class being defined: the default member initializer of one of
       its members, which a constructor of the class uses) */
    bool this_unsupported = function_state.active || at_block_scope() ||
                            defining_class || call_site_depth;
    /* the class's scope with a `this` of its own: no function's */
    function_context outer = static_cast<function_context &&>(function_state);
    entity_index outer_scope = current_scope;
    function_state = function_context();
    entity_index this_object_entity =
        entity_table.make(entity_kind::variable, 0, owner);
    entity_table[this_object_entity].type = qualified_type::make(
        type_table.pointer_to(qualified_type::make(
            entity_table[owner].type.unqualified(), cv_none)),
        cv_none);
    entity_table[this_object_entity].automatic = true;
    function_state.this_entity = this_object_entity;
    function_state.this_class = owner;
    current_scope = owner;
    uint32_t locations_before = source_location_uses;
    uint32_t nodes_before = static_cast<uint32_t>(semantics.size());
    member_defaults_analysed.append(default_initializer);
    analysis_result r = initializer(default_initializer, member_type, one);
    member_defaults_analysed.resize(member_defaults_analysed.size() - 1);
    function_state = static_cast<function_context &&>(outer);
    current_scope = outer_scope;
    if (r != analysis_result::well_formed)
        return r;
    /* [ext:clang_source_location] a builtin in it is the place of the
       aggregate's initialization, which is not the initializer's */
    if (source_location_uses != locations_before)
        return not_supported_because(
            list, "dcl.init.aggr",
            "a source location builtin in a default member initializer");
    if (!one.node)
        return not_supported_because(
            list, "dcl.init.aggr",
            "a default member initializer with no value");
    /* [class.mem]/6: names looked up in the class that use no `this`
       (a static member, a type, a function of another class): the value is
       the initializer's own, as anywhere else, with no object to bind */
    bool uses_this = false;
    for (uint32_t n = nodes_before;
         n < static_cast<uint32_t>(semantics.size()) && !uses_this; n++)
        uses_this = semantics[n].kind == semantic_kind::entity &&
                    semantics[n].a == this_object_entity;
    if (!uses_this)
        return analysis_result::well_formed;
    if (this_unsupported)
        return not_supported_because(
            list, "dcl.init.aggr",
            "a default member initializer using this in this context");
    /* the evaluator binds `this` to the aggregate under construction, whose
       earlier elements it has stored; its value is not known here */
    expression_meaning wrapped;
    wrapped.type = member_type;
    wrapped.evaluation = constness::unknown;
    uint32_t node = expression_node(semantic_kind::default_member_initializer,
                                    wrapped, list.first_token());
    semantics[node].a = one.node;
    semantics[node].b = this_object_entity;
    one.node = node;
    one.evaluation = constness::unknown;
    return analysis_result::well_formed;
}

/* the value of an element no clause initializes ([dcl.init.aggr]/5) */
analysis_result semantic_analysis::omitted_element_value(
    SyntaxRef list, qualified_type element, SyntaxRef default_initializer,
    entity_index owner, initialized_value &one)
{
    if (default_initializer) {
        /* (5.1): "If the element has a default member initializer
           ([class.mem]), the element is initialized from that
           initializer", whose this is the aggregate being initialized
           and whose names are looked up in the class: analysed here
           only when it names nothing (literals and operators) */
        qualified_type member_type = element;
        const struct type member = type_table[element.unqualified()];
        if (initializer_names_nothing(default_initializer)) {
            VTRY(r, initializer(default_initializer, member_type, one));
        } else {
            VTRY(r, member_default_in_aggregate(
                        list, owner, default_initializer, member_type, one));
        }
        if (member.kind == type_kind::class_type &&
            entity_table[member.entity].kind == entity_kind::class_type &&
            (entity_table[member.entity].first_constructor ||
             destroys(member.entity)) &&
            !aggregate_value_stored(member.entity, one.node))
            /* a class with constructors initialized in place by a
               constructor call: not analysed */
            return not_supported_because(
                list, "dcl.init.aggr",
                "a class member default that needs construction in place");
        return analysis_result::well_formed;
    }
    /* (5.2): from an empty initializer list: value-initialization
       ([dcl.init.list]/3.11 for a scalar, a default constructor for a
       class with constructors, all zero otherwise) */
    const struct type of_element = type_table[element.unqualified()];
    if (of_element.kind == type_kind::class_type &&
        entity_table[of_element.entity].kind == entity_kind::class_type &&
        entity_table[of_element.entity].first_constructor) {
        owned_array<expression_meaning> none;
        if (empty_list_uses_constructors(of_element.entity)) {
            owned_array<SyntaxElement> no_clauses;
            return class_list_initialization(list, no_clauses, element, false,
                                             one);
        }
        return construct(list, element, none, 0, one);
    }
    expression_meaning zero;
    zero.type = qualified_type::make(element.unqualified(), cv_none);
    zero.evaluation = constness::constant;
    one.evaluation = constness::constant;
    one.node = expression_node(semantic_kind::zero, zero, list.first_token());
    return analysis_result::well_formed;
}

/* the value of the aggregate of type: its elements in order */
void semantic_analysis::aggregate_value(SyntaxRef list, qualified_type type,
                                        constness all,
                                        const owned_array<uint32_t> &elements,
                                        initialized_value &result)
{
    result.evaluation = all;
    result.value = 0;
    expression_meaning list_meaning;
    list_meaning.type = qualified_type::make(type.unqualified(), cv_none);
    if (type_table[type.unqualified()].kind == type_kind::array ||
        type_table[type.unqualified()].kind == type_kind::vector)
        list_meaning.type = type;
    list_meaning.evaluation = all;
    result.node = expression_node(semantic_kind::initializer_list, list_meaning,
                                  list.first_token());
    semantics.set_list(result.node, elements.data(), elements.size());
}

/*
 * One element of an aggregate from clauses[next], or with its braces elided
 * from as many clauses as it has elements.  [dcl.init.aggr]/4.2: "the
 * element is copy-initialized from the corresponding initializer-clause ...
 * If the initializer-clause is an expression and a narrowing conversion is
 * required to convert the expression, the program is ill-formed."  /16:
 * "If the initializer-clause is an expression and an implicit conversion
 * sequence can be formed that converts the expression to the type of the
 * element, the element is initialized"; "If the initializer-list of a
 * subaggregate does not begin with a left brace, then only enough
 * initializer-clauses from the list are taken to initialize the elements
 * of the subaggregate".
 */
analysis_result semantic_analysis::aggregate_element(
    SyntaxRef list, const owned_array<SyntaxElement> &clauses, size_t &next,
    qualified_type element, initialized_value &one)
{
    SyntaxElement clause = clauses[next];
    SyntaxRef at = list;
    if (!clause.is_token())
        at = clause.index();
    /* [temp.variadic]/7: the clause of a pack expansion is its pattern for
       one element */
    bool has_pack = clause_packs && next < clause_packs->size() &&
                    (*clause_packs)[next] != no_expansion;
    uint32_t pack_idx = 0;
    if (has_pack)
        pack_idx = (*clause_packs)[next];
    expansion_scope scope(*this, has_pack, pack_idx);
    return aggregate_element_clause(list, clauses, next, element, one, clause,
                                    at);
}

analysis_result semantic_analysis::aggregate_element_clause(
    SyntaxRef list, const owned_array<SyntaxElement> &clauses, size_t &next,
    qualified_type element, initialized_value &one, SyntaxElement clause,
    SyntaxRef at)
{
    /* a copy: analysing the clause adds types, which may move
       type_table's */
    const struct type record = type_table[element.unqualified()];
    bool braced = clause.is(syntax_kind::braced_init_list);
    bool subaggregate =
        record.kind == type_kind::array || record.kind == type_kind::vector ||
        (record.kind == type_kind::class_type &&
         entity_table[record.entity].kind == entity_kind::class_type &&
         is_aggregate(record.entity));
    if (braced || (record.kind == type_kind::array &&
                   is_unparenthesized_string_literal(tokens, clause) &&
                   character_type(fundamental_of(record.operand)))) {
        next++;
        return initialize_from(at, clause, element, false, true, one);
    }
    if (record.kind == type_kind::class_type) {
        expression_meaning e;
        VTRY(r, expression(clause, e));
        if (e.type.unqualified() == element.unqualified()) {
            next++;
            return initialize_with(at, e, element, false, true, one);
        }
        if (!subaggregate) {
            next++;
            return initialize_with(at, e, element, false, true, one);
        }
        /* a conversion from another class (a constructor or conversion
           function) into a subaggregate */
        if (type_table[e.type.unqualified()].kind == type_kind::class_type)
            return not_supported_because(
                list, "dcl.init.aggr",
                "a subaggregate converted from another class");
    }
    if (!subaggregate) {
        next++;
        return initialize_from(at, clause, element, false, true, one);
    }
    if (record.kind == type_kind::array && !record.bound_known)
        /* a flexible array member */
        return not_supported_because(list, "dcl.init.aggr",
                                     "a flexible array member initializer");
    if ((record.kind == type_kind::array && !record.bound) ||
        (record.kind == type_kind::class_type &&
         !entity_table[record.entity].member_count))
        /* [impl.clang] a subaggregate with no elements takes no clause:
           clang's "initializer for aggregate with no elements requires
           explicit braces" error */
        return not_supported_because(list, "dcl.init.aggr",
                                     "a subaggregate with no elements");
    /* the braces elided: the subaggregate's elements from the clauses */
    return aggregate_elements(list, clauses, next, element, one);
}

/*
 * [dcl.init.aggr]/3.1: "If the initializer list is a designated-
 * initializer-list, the aggregate shall be of class type, the identifier
 * in each designator shall name a direct non-static data member of the
 * class, and the explicitly initialized elements of the aggregate are the
 * elements that are, or contain, those members."  [ext:gnu_designated_
 * initializers] (GCC manual 6.29, as clang takes it in C++): "[ index ] ="
 * and "[ first ... last ] =" for an array, designators in any order (clang
 * warns: -Wreorder-init-list), nested ".a.b =" and ".a[i] =", clauses with
 * and without designators in one list.
 */
analysis_result semantic_analysis::designated_list_initialization(
    SyntaxRef list, const owned_array<SyntaxElement> &clauses,
    const designation_set &designations, qualified_type &type,
    initialized_value &result)
{
    owned_array<designated_item> items;
    for (size_t i = 0; i < clauses.size(); i++) {
        designated_item item;
        item.clause = static_cast<uint32_t>(i);
        item.chain_first = designations.of[i].first;
        item.chain_count = designations.of[i].count;
        items.append(item);
    }
    return designated_elements(list, clauses, designations, items, type,
                               result);
}

/*
 * The elements of one aggregate (an array or an aggregate class) from the
 * clauses in items, some with designators.  A designator selects the
 * element(s) the clause initializes, and the clauses after it (without
 * designators) continue from the element after ([dcl.init.aggr]/4.1:
 * "the explicitly initialized elements ... are the first n"; C99 6.7.9/17).
 * A clause with more designators initializes a subobject of the element: the
 * clauses designating the element so are its own list (the recursion).
 * A name that is a member of an anonymous union or structure of the class
 * selects that member of the class and is a designator of it, the same
 * clause one level down ([class.union.anon]/1: its members are members of
 * the enclosing scope; clang designates through the anonymous member).
 * A union holds the member its designators name; the node's a is that
 * member's entity unless it is the first (0).
 * Left to refuse: a clause without designator after a nested one (C's
 * current object is then inside the element), the same element designated
 * in part and as a whole.
 */
analysis_result semantic_analysis::designated_elements(
    SyntaxRef list, const owned_array<SyntaxElement> &clauses,
    const designation_set &designations,
    const owned_array<designated_item> &items, qualified_type &type,
    initialized_value &result)
{
    const struct type target = type_table[type.unqualified()];
    bool is_array = target.kind == type_kind::array;
    owned_array<qualified_type> types;
    owned_array<SyntaxRef> defaults;
    owned_array<entity_index> members;
    uint64_t bound = 0;
    bool bound_known = true, is_union = false;
    /* an empty base precedes the members (and the non-empty bases) as an
       element, but is not in the element list here */
    bool empty_base = false;
    if (is_array) {
        bound = target.bound;
        bound_known = target.bound_known;
    } else if (target.kind == type_kind::class_type &&
               entity_table[target.entity].kind == entity_kind::class_type) {
        require_complete(target.entity);
        if (!entity_table[target.entity].size)
            return not_supported_because(
                list, "dcl.init.list",
                "designated initialization of an incomplete class");
        if (!is_aggregate(target.entity))
            return ill_formed(list,
                              "a designator selects an element of an "
                              "aggregate",
                              "dcl.init.aggr");
        const entity &x = entity_table[target.entity];
        is_union = x.union_;
        owned_array<aggregate_class_element> elements;
        aggregate_class_elements(target.entity, elements);
        for (const aggregate_class_element &e : elements) {
            /* [dcl.init.aggr]/5.2: an empty base class is an element too,
               which no designator initializes: value-initialized (nothing
               to do when its special members are trivial) */
            if (e.empty_base) {
                empty_base = true;
                if (!trivial_special_members(
                        type_table[e.type.unqualified()].entity))
                    return not_supported_because(
                        list, "dcl.init.aggr",
                        "an empty base with non-trivial members before "
                        "designators");
                continue;
            }
            types.append(e.type);
            defaults.append(e.default_initializer);
            members.append(e.member);
        }
        bound = members.size();
    } else if (target.kind == type_kind::lvalue_reference ||
               target.kind == type_kind::rvalue_reference) {
        return not_supported_because(list, "dcl.init.list",
                                     "a designated list for a reference");
    } else {
        return ill_formed(list,
                          "a designator selects an element of an aggregate",
                          "dcl.init.aggr");
    }
    /* the elements that can be initialized: a union's first */
    uint64_t room = bound;
    if (is_union && room > 1)
        room = 1;
    if (is_array && !bound_known)
        room = UINT64_MAX;
    /* the largest array a designated list is expanded for (its gaps are
       zero nodes) */
    const uint64_t limit = 1u << 20;

    owned_array<uint32_t> slots;
    owned_array<uint8_t> consumed;
    if (!items.empty())
        memset(consumed.extend(items.size()), 0, items.size());
    constness all = constness::constant;
    uint64_t pos = 0, extent = 0;
    bool after_nested = false;
    /* a union's member the clauses so far initialize */
    uint64_t active = 0;
    bool active_set = false;
    for (size_t i = 0; i < items.size(); i++) {
        if (consumed[i])
            continue;
        const designated_item item = items[i];
        uint64_t first = pos, last = pos;
        bool through = false;
        if (item.chain_count) {
            VTRY(r, designator_elements(
                        list, designations.pool[item.chain_first], target,
                        members, bound, bound_known, first, last, through));
            if (is_union && active_set && first != active)
                /* another member than the one given: clang's "initializer
                   partially overrides prior initialization of this
                   subobject" error, not modelled */
                return not_supported_because(
                    list, "dcl.init.aggr",
                    "a designator overriding another union member");
        } else {
            if (after_nested)
                return not_supported_because(
                    list, "dcl.init.aggr",
                    "an undesignated clause after a nested designator");
            if (empty_base)
                /* a clause without a designator mixed with designated
                   ones (clang's -Wc99-designator) counts the empty
                   bases' positions, which this element list leaves out:
                   not modelled */
                return not_supported_because(
                    list, "dcl.init.aggr",
                    "mixed designators with an empty base");
            if (pos >= room)
                return ill_formed(list,
                                  "an initializer-list is ill-formed if the "
                                  "number of initializer-clauses exceeds the "
                                  "number of elements of the aggregate",
                                  "dcl.init.aggr");
        }
        if (last >= limit)
            return not_supported_because(list, "dcl.init.aggr",
                                         "an array designator index too large");
        qualified_type element = target.operand;
        if (!is_array)
            element = types[first];
        initialized_value one;
        if (item.chain_count > 1 || through) {
            /* a subobject of the element: the clauses designating the
               element so, as a list of its own */
            if (first != last || (first < slots.size() && slots[first]))
                return not_supported_because(
                    list, "dcl.init.aggr",
                    "a nested designator over a range or initialized element");
            owned_array<designated_item> inner;
            for (size_t j = i; j < items.size(); j++) {
                if (consumed[j] || !items[j].chain_count)
                    continue;
                uint64_t a = 0, b = 0;
                bool through_j = through;
                if (j != i) {
                    VTRY(r, designator_elements(
                                list, designations.pool[items[j].chain_first],
                                target, members, bound, bound_known, a, b,
                                through_j));
                    if (is_union && a != first)
                        /* another member: refused when it is reached */
                        continue;
                    if (a != first || b != last)
                        continue;
                    if (items[j].chain_count == 1 && !through_j)
                        /* the element designated in part and as a whole */
                        return not_supported_because(
                            list, "dcl.init.aggr",
                            "an element designated both in part and whole");
                }
                designated_item d = items[j];
                if (!through_j) {
                    d.chain_first++;
                    d.chain_count--;
                }
                inner.append(d);
                consumed[j] = 1;
            }
            VTRY(r, designated_elements(list, clauses, designations, inner,
                                        element, one));
            after_nested = true;
        } else {
            /* the clause, and the clauses after it without designators it
               may take with its braces elided */
            owned_array<SyntaxElement> run;
            for (size_t j = i; j < items.size(); j++) {
                if (j != i && (consumed[j] || items[j].chain_count))
                    break;
                run.append(clauses[items[j].clause]);
            }
            size_t next = 0;
            VTRY(r, aggregate_element(list, run, next, element, one));
            if (!next)
                return not_supported_because(
                    list, "dcl.init.aggr",
                    "a designated element that takes no clause");
            i += next - 1;
            after_nested = false;
        }
        if (last > first && one.evaluation != constness::constant)
            /* [ext:gnu_designated_initializers]: a range's initializer is
               evaluated once; the list holds its node once per element, so
               only a constant (no side effect) is the same for each */
            return not_supported_because(
                list, "dcl.init.aggr",
                "a range designator with a non-constant value");
        if (slots.size() <= last) {
            size_t was = slots.size();
            uint32_t *grown = slots.extend(last + 1 - was);
            memset(grown, 0, (last + 1 - was) * sizeof(uint32_t));
        }
        for (uint64_t k = first; k <= last; k++)
            slots[k] = one.node;
        if (item.chain_count) {
            active = first;
            active_set = true;
        }
        all = combined_constness(all, one.evaluation);
        pos = last + 1;
        if (pos > extent)
            extent = pos;
    }
    /* the elements: those no clause initializes by [dcl.init.aggr]/5 */
    uint64_t count = extent;
    if (!is_array)
        count = bound;
    if (is_union && count > 1)
        count = 1;
    if (is_array && bound_known && count > target.bound)
        count = target.bound;
    owned_array<uint32_t> elements;
    entity_index active_member = 0;
    if (is_union && active_set && active != 0 && active < slots.size() &&
        slots[active]) {
        /* a union member but the first: the list holds its value alone and
           the node names the member (the lowering's initialized field) */
        elements.append(slots[active]);
        active_member = members[active];
        count = 0;
    }
    for (uint64_t i = 0; i < count; i++) {
        if (i < slots.size() && slots[i]) {
            elements.append(slots[i]);
            continue;
        }
        qualified_type element = target.operand;
        SyntaxRef default_initializer;
        if (!is_array) {
            element = types[i];
            default_initializer = defaults[i];
        }
        initialized_value one;
        VTRY(r, omitted_element_checks(list, element,
                                       static_cast<bool>(default_initializer)));
        VTRY_SET(r, omitted_element_value(list, element, default_initializer,
                                          target.entity, one));
        all = combined_constness(all, one.evaluation);
        elements.append(one.node);
    }
    if (is_array && !bound_known)
        /* [dcl.init.aggr]/4.2 (unknown bound): the greatest index and one */
        type = qualified_type::make(type_table.array_of(target.operand, extent),
                                    type.cv());
    aggregate_value(list, type, all, elements, result);
    semantics[result.node].a = active_member;
    return analysis_result::well_formed;
}

/* Whether the class declares a member named name, or has an anonymous union
   or structure member that does ([class.union.anon]/1) */
bool semantic_analysis::anonymous_member_declares(entity_index cls,
                                                  uint32_t name)
{
    const entity &x = entity_table[cls];
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.name == name)
            return true;
        if (m.name || m.bit_field || m.base_subobject)
            continue;
        const struct type mt = type_table[m.type.unqualified()];
        if (mt.kind == type_kind::class_type &&
            anonymous_member_declares(mt.entity, name))
            return true;
    }
    return false;
}

/*
 * The elements a designator selects of the aggregate target, first to last
 * (more than one for GNU's "[ first ... last ]").  [dcl.init.aggr]/3.1:
 * ". identifier" names a direct non-static data member of a class; C99's
 * "[ constant-expression ]" ([ext:gnu_designated_initializers]) an element
 * of an array, "the constant-expression shall be an integer constant
 * expression ... within the array bounds" (C17 6.7.9/6); the obsolete
 * "identifier :" is a field designator.
 */
analysis_result semantic_analysis::designator_elements(
    SyntaxRef list, SyntaxElement designator, const struct type &target,
    const owned_array<entity_index> &members, uint64_t bound, bool bound_known,
    uint64_t &first, uint64_t &last, bool &through)
{
    through = false;
    bool is_array = target.kind == type_kind::array;
    SyntaxRef at = list;
    if (designator.is_node())
        at = designator;
    token_index name_token = 0;
    if (designator.is_token())
        name_token = designator.token();
    else if (designator[0].is(preprocessing_op_or_punc::DOT))
        name_token = designator[1].token();
    if (name_token) {
        if (is_array)
            return ill_formed(at,
                              "a field designator cannot initialize an "
                              "array",
                              "dcl.init.aggr");
        uint32_t name = tokens.identifier(name_token);
        for (size_t i = 0; i < members.size(); i++) {
            if (entity_table[members[i]].name == name) {
                first = last = i;
                return analysis_result::well_formed;
            }
        }
        /* a member of an anonymous union or structure is bound through it:
           the designator selects the anonymous member and the same
           designator then names the member in it */
        for (size_t i = 0; i < members.size(); i++) {
            const entity &m = entity_table[members[i]];
            if (m.name || m.base_subobject)
                continue;
            const struct type mt = type_table[m.type.unqualified()];
            if (mt.kind == type_kind::class_type &&
                anonymous_member_declares(mt.entity, name)) {
                first = last = i;
                through = true;
                return analysis_result::well_formed;
            }
        }
        return ill_formed(at,
                          "the identifier in each designator shall name a "
                          "direct non-static data member of the class",
                          "dcl.init.aggr");
    }
    if (!is_array)
        return ill_formed(at,
                          "an array designator cannot initialize an "
                          "object of class type",
                          "dcl.init.aggr");
    const implementation_defined_behavior &behavior = tokens.files.behavior;
    uint64_t bounds[2] = {0, 0};
    uint32_t operands = 1;
    uint32_t index = 1;
    if (designator.size() > 4)
        operands = 2;
    for (uint32_t k = 0; k < operands; k++) {
        expression_meaning e;
        VTRY(r, expression(designator[index], e));
        VTRY_SET(r, operand_value(at, e));
        /* an enumerator: its value as an integer */
        if (!promote_enumeration(e))
            enumeration_as_underlying(e);
        if (!is_integral(e.type))
            return ill_formed(at,
                              "an array designator's index shall be an "
                              "integer constant expression",
                              "dcl.init.aggr");
        bool constant = false;
        VTRY_SET(r, integral_constant_expression(at, e, constant));
        if (!constant)
            return not_supported_because(
                at, "expr.const",
                "an array designator index not evaluated as a constant");
        __int128 value =
            integer_value(behavior, fundamental_of(e.type), e.value);
        if (value < 0)
            return ill_formed(at,
                              "an array designator's index shall not be "
                              "negative",
                              "dcl.init.aggr");
        if (value > static_cast<__int128>(UINT32_MAX))
            return not_supported_because(at, "dcl.init.aggr",
                                         "an array designator index too large");
        bounds[k] = static_cast<uint64_t>(value);
        index = 3;
    }
    first = bounds[0];
    last = bounds[0];
    if (operands == 2)
        last = bounds[1];
    if (last < first)
        return ill_formed(at, "an array designator's range is empty",
                          "dcl.init.aggr");
    if (bound_known && last >= bound)
        return ill_formed(at,
                          "an array designator's index shall be within the "
                          "array bounds",
                          "dcl.init.aggr");
    return analysis_result::well_formed;
}

} // namespace cplusplus
