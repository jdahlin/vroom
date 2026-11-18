/*
 * expr.unary.cpp: [expr.unary] Unary expressions, with [expr.unary.op],
 * [expr.pre.incr], [expr.sizeof], [expr.alignof] and [expr.unary.noexcept],
 * which are alternatives of the one production or a production of one line.
 *
 *     unary-expression:
 *         postfix-expression
 *         unary-operator cast-expression
 *         ++ cast-expression
 *         -- cast-expression
 *         await-expression
 *         sizeof unary-expression
 *         sizeof ( type-id )
 *         sizeof ... ( identifier )
 *         alignof ( type-id )
 *         noexcept-expression
 *         new-expression
 *         delete-expression
 *
 *     unary-operator: one of
 *         * & + - ! ~
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

bool is_unary_operator(const token &t)
{
    if (t.kind != token_kind::operator_or_punctuator)
        return false;
    switch (preprocessing_op_or_punc_primary(t.op_or_punc)) {
    case preprocessing_op_or_punc::STAR:
    case preprocessing_op_or_punc::AMPERSAND:
    case preprocessing_op_or_punc::PLUS:
    case preprocessing_op_or_punc::MINUS:
    case preprocessing_op_or_punc::EXCLAMATION:
    case preprocessing_op_or_punc::TILDE:
        return true;
    default:
        return false;
    }
}

} // namespace

/*
 * [expr.unary]/1: "Expressions with unary operators group right-to-left":
 * the operand of an operator is a cast-expression.
 *
 * [expr.unary.op]/10: "There is an ambiguity in the grammar when ~ is
 * followed by a type-name or decltype-specifier. The ambiguity is resolved
 * by treating ~ as the unary complement operator rather than as the start
 * of an unqualified-id naming a destructor."  So '~' here is always the
 * operator.
 *
 * [expr.sizeof]/1: "The operand is either an expression, which is an
 * unevaluated operand, or a parenthesized type-id"; which of the two a '('
 * begins is [dcl.ambig.res]/2's rule.  [expr.alignof]/1: "The operand shall
 * be a type-id".
 *
 * await-expression is expr.await.cpp's and sizeof ... ( identifier ) P7's.  The first
 * alternative passes its postfix-expression on.
 */
node_index parser::unary_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    if (at(extension_keyword::extension)) /* [ext.gnu] */
        return gnu_extension_expression();
    /* [ext:gnu_labels_as_values] */
    if (at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND) && at_identifier(1) &&
        tokens.files.behavior.has(extension::gnu_labels_as_values))
        return gnu_label_address();
    if (is_unary_operator(peek()) || at(preprocessing_op_or_punc::PLUS_PLUS) ||
        at(preprocessing_op_or_punc::MINUS_MINUS)) {
        add_token();
        cast_expression();
        if (failed())
            return 0;
        return finish_node(syntax_kind::unary_expression, mark, first);
    }
    if (at(standard_keyword::sizeof_)) {
        add_token();
        if (at(preprocessing_op_or_punc::ELLIPSIS)) {
            /* [expr.sizeof]/5: sizeof ... ( identifier ) (C++11) */
            if (!tokens.files.behavior.has(
                    language_feature::variadic_templates))
                return beyond_this_step();
            add_token();
            if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
                return expected("'(' after sizeof...", "expr.sizeof");
            if (!at_identifier())
                return expected("an identifier in sizeof...", "expr.sizeof");
            add_token();
            if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return expected("')' after the identifier", "expr.sizeof");
            return finish_node(syntax_kind::unary_expression, mark, first);
        }
        bool type = false;
        if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
            type = parenthesized_type_id(false);
            if (failed())
                return 0;
        }
        if (!type) {
            /* sizeof sizeof ...: nesting without a cast-expression */
            nesting_level level(*this);
            if (!nesting_within_limit("an expression nested no deeper than "
                                      "this implementation's limit"))
                return 0;
            unary_expression();
            if (failed())
                return 0;
        }
        return finish_node(syntax_kind::unary_expression, mark, first);
    }
    if (at(standard_keyword::alignof_)) {
        add_token();
        if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return expected("'(' after alignof", "expr.alignof");
        if (!parenthesized_type_id(false)) {
            if (failed())
                return 0;
            /* [ext.gnu] alignof of an expression: P9 */
            return beyond_this_step();
        }
        return finish_node(syntax_kind::unary_expression, mark, first);
    }
    if (at(standard_keyword::noexcept_))
        return noexcept_expression();
    if (at(standard_keyword::new_) ||
        (at(preprocessing_op_or_punc::COLON_COLON) &&
         at(standard_keyword::new_, 1)))
        return new_expression();
    if (at(standard_keyword::delete_) ||
        (at(preprocessing_op_or_punc::COLON_COLON) &&
         at(standard_keyword::delete_, 1)))
        return delete_expression();
    if (at(standard_keyword::co_await_))
        return await_expression();
    return postfix_expression();
}

/*
 * [expr.unary.noexcept]:
 *
 *     noexcept-expression:
 *         noexcept ( expression )
 *
 * [expr.unary.noexcept]/1: "The noexcept operator determines whether the
 * evaluation of its operand, which is an unevaluated operand, can throw an
 * exception".
 */
node_index parser::noexcept_expression()
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after noexcept", "expr.unary.noexcept");
    expression();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to close the operand of noexcept",
                        "expr.unary.noexcept");
    return finish_node(syntax_kind::noexcept_expression, mark, first);
}

/* ---- semantic analysis ---------------------------------------------- */

uint64_t semantic_analysis::pointer_size() const
{
    return tokens.files.behavior.long_width / 8;
}

/* the size in bytes of an object of type t on the target (aarch64 Linux,
   LP64), 0 if t is not a complete object type analysis knows the size of */
uint64_t semantic_analysis::size_of_type(qualified_type t) const
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    const struct type record = type_table[t.unqualified()];
    switch (record.kind) {
    case type_kind::fundamental:
        switch (record.fundamental) {
        case fundamental_type::bool_:
            return 1;
        case fundamental_type::float_:
            return 4;
        case fundamental_type::double_:
            return 8;
        case fundamental_type::long_double:
            if (b.long_double_fraction_bits == 112)
                return 16;
            return 8;
        case fundamental_type::std_nullptr_t:
            return b.long_width / 8;
        case fundamental_type::builtin_va_list:
            return 32;
        default:
            if (fundamental_type_is_integral(record.fundamental))
                return fundamental_type_width(b, record.fundamental) / 8;
            return 0;
        }
    case type_kind::pointer:
        return b.long_width / 8;
    case type_kind::pointer_to_member:
        /* [abi:data-member-pointers]: "The basic ABI properties of data
           member pointer types are those of ptrdiff_t."
           [abi:member-function-pointers]: "the basic ABI properties of
           member function pointer types are those of the following class
           ... struct { fnptr_t ptr; ptrdiff_t adj; };" */
        if (type_table[record.operand.unqualified()].kind ==
            type_kind::function)
            return 2 * (b.long_width / 8);
        return b.long_width / 8;
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
        return size_of_type(record.operand);
    case type_kind::array:
        if (!record.bound_known)
            return 0;
        return record.bound * size_of_type(record.operand);
    case type_kind::class_type:
        /* [class.mem]/6: 0 until the class is complete */
        return entity_table[record.entity].size;
    case type_kind::enumeration: {
        /* [dcl.enum]/8 and [abi:layout]: the underlying type's */
        fundamental_type u = entity_table[record.entity].underlying;
        if (u == fundamental_type::none)
            return 0;
        return fundamental_type_width(b, u) / 8;
    }
    default:
        return 0;
    }
}

uint64_t semantic_analysis::alignment_of_type(qualified_type t) const
{
    const struct type record = type_table[t.unqualified()];
    switch (record.kind) {
    case type_kind::lvalue_reference:
    case type_kind::rvalue_reference:
    case type_kind::array:
        return alignment_of_type(record.operand);
    case type_kind::class_type:
        return entity_table[record.entity].alignment;
    case type_kind::pointer_to_member:
        /* (ptrdiff_t's, and the pair's: its members') */
        return tokens.files.behavior.long_width / 8;
    case type_kind::fundamental:
        if (record.fundamental == fundamental_type::builtin_va_list)
            return 8;
        return size_of_type(t);
    default:
        /* the fundamental types and pointers are aligned to their size on
           aarch64 (AAPCS64 5.1) */
        return size_of_type(t);
    }
}

/*
 * [expr.sizeof]/1: "The sizeof operator yields the number of bytes occupied
 * by a non-potentially-overlapping object of the type of its operand.  The
 * operand is either an expression, which is an unevaluated operand, or a
 * parenthesized type-id."  [expr.alignof]/1: "An alignof expression yields
 * the alignment requirement of its operand type."  [expr.sizeof]/5 and
 * [expr.alignof]/2: "The result ... is a prvalue of type std::size_t."
 */
analysis_result semantic_analysis::size_of(node_index n, bool alignment,
                                           expression_meaning &m)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    qualified_type operand;
    if (node.child_count == 4 &&
        syntax_is(tree, children[2], syntax_kind::type_id)) {
        analysis_result r = type_id(children[2].index(), operand);
        if (r != analysis_result::well_formed)
            return r;
    } else if (node.child_count == 2 && !alignment) {
        /* [expr.sizeof]/3: "The lvalue-to-rvalue, array-to-pointer, and
           function-to-pointer standard conversions are not applied to the
           operand of sizeof." */
        expression_meaning e;
        /* [expr.sizeof]/1: "The operand is either an expression, which is
           an unevaluated operand ([expr.prop])" */
        unevaluated++;
        analysis_result r = expression(children[1], e);
        unevaluated--;
        if (r != analysis_result::well_formed)
            return r;
        /* [expr.sizeof]/1: "The sizeof operator shall not be applied to
           ... an lvalue that designates a bit-field." */
        if (e.bit_field_width)
            return not_supported(n, "class.bit");
        operand = e.type;
    } else if (!alignment && node.child_count == 5 &&
               syntax_is_punctuator(tokens, children[1],
                                    preprocessing_op_or_punc::ELLIPSIS) &&
               children[3].is_token() &&
               tokens[children[3].index()].kind == token_kind::identifier) {
        /* [expr.sizeof]/4: "The identifier in a sizeof... expression shall
           name a pack.  The sizeof... operator yields the number of
           elements in the pack ([temp.variadic]).  A sizeof... expression
           is a pack expansion ([temp.variadic])."  An instantiation's pack;
           a template's own is not analysed */
        entity_index pack = find_pack(tokens.identifier(children[3].index()));
        if (!pack)
            return not_supported(n, "expr.sizeof");
        m.type = qualified_type::make(
            type_table.fundamental(fundamental_type::unsigned_long_int),
            cv_none);
        m.category = value_category::prvalue;
        m.evaluation = constness::constant;
        m.value = entity_table[pack].member_count;
        m.node = expression_node(semantic_kind::constant, m, node.first_token);
        return analysis_result::well_formed;
    } else {
        /* alignof of an expression (GCC's) */
        return not_supported(n, "expr.sizeof");
    }
    const struct type record = type_table[operand.unqualified()];
    if (record.kind == type_kind::function ||
        (record.kind == type_kind::fundamental &&
         record.fundamental == fundamental_type::void_))
        /* GCC's sizeof(void) and sizeof of a function type, extensions
           clang has too */
        return not_supported(n, "expr.sizeof");
    if (record.kind == type_kind::array && !record.bound_known) {
        if (alignment)
            /* "or an array thereof": alignof(T[]) */
            operand = record.operand;
        else
            return ill_formed(n,
                              "the sizeof operator shall not be applied to an "
                              "expression that has function or incomplete "
                              "type, or to the parenthesized name of such "
                              "types",
                              "expr.sizeof");
    }
    uint64_t size = size_of_type(operand);
    if (alignment)
        size = alignment_of_type(operand);
    if (!size)
        return not_supported(n, "expr.sizeof");
    m.type = qualified_type::make(
        type_table.fundamental(fundamental_type::unsigned_long_int), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::constant;
    m.value = size;
    /* the operand is unevaluated ([expr.sizeof]/1): the constant alone */
    m.node = expression_node(semantic_kind::constant, m, node.first_token);
    return analysis_result::well_formed;
}

/*
 * [expr.pre.incr]/1: "The operand shall be a modifiable lvalue.  The type
 * of the operand shall be an arithmetic type other than cv bool, or a
 * pointer to a completely-defined object type. ... The result is the
 * updated operand; it is an lvalue".  [expr.post.incr]/1: "The result is
 * a prvalue.  The type of the result is the cv-unqualified version of the
 * type of the operand."  The operand is modified: never a constant
 * expression here ([expr.const]/5.16).
 */
analysis_result semantic_analysis::increment(node_index n, syntax_child operand,
                                             bool decrement, bool postfix,
                                             expression_meaning &m)
{
    analysis_result r = expression(operand, m);
    if (r != analysis_result::well_formed)
        return r;
    {
        /* [over.inc]: an operand of class or enumeration type
           ([over.match.oper]/2) */
        preprocessing_op_or_punc op = preprocessing_op_or_punc::PLUS_PLUS;
        if (decrement)
            op = preprocessing_op_or_punc::MINUS_MINUS;
        bool handled = false;
        r = unary_operator_function(n, tree[n].first_token, op, postfix,
                                    operand, m, handled);
        if (r != analysis_result::well_formed || handled)
            return r;
    }
    const char *subclause = "expr.pre.incr";
    if (postfix)
        subclause = "expr.post.incr";
    /* [basic.lval]/10: "An lvalue is modifiable unless its type is
       const-qualified or is a function type." */
    if (m.category != value_category::lvalue || (m.type.cv() & cv_const))
        return ill_formed(n, "the operand shall be a modifiable lvalue",
                          subclause);
    /* [over.match.oper]/1: "If no operand of an operator in an expression
       has a type that is a class or an enumeration, the operator is assumed
       to be a built-in operator": for one that has, an overloaded operator
       function may be the meaning */
    {
        type_kind operand_kind = type_table[m.type.unqualified()].kind;
        if (operand_kind == type_kind::class_type ||
            operand_kind == type_kind::enumeration)
            return not_supported(n, "over.match.oper");
    }
    fundamental_type t = fundamental_of(m.type);
    /* [diff.cpp14.expr]/1: ++ of a bool operand, deprecated, removed in
       C++17 */
    bool bool_allowed = !decrement && tokens.files.behavior.has(
                                          language_feature::bool_increment);
    if ((t == fundamental_type::bool_ && !bool_allowed) ||
        (!is_arithmetic(m.type) && !is_pointer(m.type)))
        return ill_formed(n,
                          "the type of the operand shall be an arithmetic "
                          "type other than cv bool, or a pointer to a "
                          "completely-defined object type",
                          subclause);
    if (is_pointer(m.type) &&
        (!is_object_pointer(m.type) ||
         !size_of_type(type_table[m.type.unqualified()].operand)))
        return not_supported(n, subclause);
    /* [expr.const]/5.16: the modification of a variable whose lifetime
       did not begin within the evaluation (the evaluator's, inside a
       constexpr function's); another glvalue (a call's, *p) may designate
       an object whose lifetime did (a temporary bound to a reference
       parameter that the function returns) */
    if (m.node && semantics[m.node].kind == semantic_kind::entity)
        m.evaluation = constness::not_constant;
    else if (m.evaluation != constness::not_constant)
        m.evaluation = constness::unknown;
    m.entity = 0;
    increment_kind kind = increment_kind::pre_increment;
    if (decrement)
        kind = increment_kind::pre_decrement;
    if (postfix) {
        m.category = value_category::prvalue;
        m.type = qualified_type::make(m.type.unqualified(), cv_none);
        kind = increment_kind::post_increment;
        if (decrement)
            kind = increment_kind::post_decrement;
    }
    m.node = expression_node(semantic_kind::increment, m, tree[n].first_token,
                             m.node, 0, static_cast<uint8_t>(kind));
    return analysis_result::well_formed;
}

/*
 * [expr.unary.op]: the unary operators *, &, +, -, ! and ~; [expr.pre.incr]
 * prefix ++ and --; [expr.sizeof] and [expr.alignof].
 */
analysis_result semantic_analysis::unary(node_index n, expression_meaning &m)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (node.child_count < 2 || !children[0].is_token())
        return not_supported(n, "expr.unary");
    const token &op = tokens[children[0].index()];
    if (op.kind == token_kind::keyword) {
        standard_keyword kw = p.keyword_of(op);
        if (kw == standard_keyword::sizeof_)
            return size_of(n, false, m);
        if (kw == standard_keyword::alignof_)
            return size_of(n, true, m);
        return not_supported(n, "expr.unary");
    }
    if (op.kind != token_kind::operator_or_punctuator || node.child_count != 2)
        return not_supported(n, "expr.unary");
    preprocessing_op_or_punc o =
        preprocessing_op_or_punc_primary(op.op_or_punc);
    if (o == preprocessing_op_or_punc::PLUS_PLUS ||
        o == preprocessing_op_or_punc::MINUS_MINUS)
        return increment(n, children[1],
                         o == preprocessing_op_or_punc::MINUS_MINUS, false, m);
    /* [expr.unary.op]/4: "A pointer to member is only formed when an
       explicit & is used and its operand is a qualified-id not enclosed in
       parentheses." */
    node_index outer_operand = member_pointer_operand;
    member_pointer_operand = 0;
    if (o == preprocessing_op_or_punc::AMPERSAND &&
        syntax_is(tree, children[1], syntax_kind::qualified_id))
        member_pointer_operand = children[1].index();
    analysis_result r = expression(children[1], m);
    member_pointer_operand = outer_operand;
    if (r != analysis_result::well_formed)
        return r;
    if (m.node && semantics[m.node].kind == semantic_kind::member_pointer)
        return analysis_result::well_formed;
    const implementation_defined_behavior &b = tokens.files.behavior;
    token_index at = children[0].index();
    /* [over.unary]: an operand of class or enumeration type
       ([over.match.oper]/2) */
    bool handled = false;
    r = unary_operator_function(n, at, o, false, children[1], m, handled);
    if (r != analysis_result::well_formed || handled)
        return r;
    switch (o) {
    case preprocessing_op_or_punc::AMPERSAND: {
        /* [expr.unary.op]/3: "The operand shall be an lvalue or a
           qualified-id. ... if the type of the expression is T, the result
           has type "pointer to T" and is a prvalue" */
        if (m.category != value_category::lvalue &&
            type_table[m.type.unqualified()].kind == type_kind::class_type)
            /* [impl.clang] the address of a class temporary: an error
               clang makes a warning (-Waddress-of-temporary) */
            return not_supported(n, "expr.unary.op");
        /* (/3.1, a pointer to member, is member_pointer_constant's) */
        if (m.category != value_category::lvalue)
            return ill_formed(n,
                              "the operand of the unary & operator shall "
                              "be an lvalue",
                              "expr.unary.op");
        if (m.bit_field_width)
            return address_of_bit_field(tree[n].first_token);
        address_of_glvalue(at, m);
        return analysis_result::well_formed;
    }
    case preprocessing_op_or_punc::STAR: {
        /* [expr.unary.op]/1: "the expression to which it is applied shall
           be a pointer to an object type, or a pointer to a function type
           and the result is an lvalue referring to the object or function
           to which the expression points.  If the type of the expression
           is "pointer to T", the type of the result is "T"." */
        r = operand_value(n, m);
        if (r != analysis_result::well_formed)
            return r;
        if (!is_pointer(m.type)) {
            if (fundamental_of(m.type) == fundamental_type::none)
                return not_supported(n, "expr.unary.op");
            return ill_formed(n,
                              "the operand of unary * shall be a pointer to an "
                              "object type, or a pointer to a function type",
                              "expr.unary.op");
        }
        qualified_type pointee = type_table[m.type.unqualified()].operand;
        if (fundamental_of(pointee) == fundamental_type::void_)
            /* clang's extension, a warning */
            return not_supported(n, "expr.unary.op");
        m.type = pointee;
        m.category = value_category::lvalue;
        if (m.evaluation != constness::not_constant)
            m.evaluation = constness::unknown;
        m.node = expression_node(semantic_kind::indirection, m, at, m.node);
        return analysis_result::well_formed;
    }
    case preprocessing_op_or_punc::PLUS:
    case preprocessing_op_or_punc::MINUS:
    case preprocessing_op_or_punc::TILDE: {
        r = operand_value(n, m);
        if (r != analysis_result::well_formed)
            return r;
        /* [expr.unary.op]/7, /8, /10: "arithmetic or unscoped
           enumeration type", "integral or unscoped enumeration type" */
        promote_enumeration(m);
        if (o == preprocessing_op_or_punc::PLUS && is_pointer(m.type)) {
            /* [expr.unary.op]/7: "or pointer type" */
            break;
        }
        bool allowed = is_arithmetic(m.type);
        if (o == preprocessing_op_or_punc::TILDE)
            allowed = is_integral(m.type);
        if (!allowed) {
            if (fundamental_of(m.type) == fundamental_type::none &&
                !is_pointer(m.type))
                return not_supported(n, "expr.unary.op");
            if (o == preprocessing_op_or_punc::PLUS)
                return ill_formed(n,
                                  "the operand of the unary + operator shall "
                                  "have arithmetic, unscoped enumeration, or "
                                  "pointer type",
                                  "expr.unary.op");
            if (o == preprocessing_op_or_punc::MINUS)
                return ill_formed(n,
                                  "the operand of the unary - operator shall "
                                  "have arithmetic or unscoped enumeration "
                                  "type",
                                  "expr.unary.op");
            return ill_formed(n,
                              "the operand of ~ shall have integral or "
                              "unscoped enumeration type",
                              "expr.unary.op");
        }
        /* "Integral promotion is performed on integral or enumeration
           operands." */
        promote(m);
        fundamental_type t = fundamental_of(m.type);
        if (m.evaluation != constness::constant ||
            o == preprocessing_op_or_punc::PLUS)
            break;
        m.evaluation = fold_unary(b, o, t, m.value, m.value);
        break;
    }
    case preprocessing_op_or_punc::EXCLAMATION: {
        /* [expr.unary.op]/9: "The operand of the logical negation operator
           ! is contextually converted to bool; its value is true if the
           converted operand is false and false otherwise.  The type of the
           result is bool." */
        r = contextual_bool(n, m);
        if (r != analysis_result::well_formed)
            return r;
        m.value = !m.value;
        break;
    }
    default:
        return not_supported(n, "expr.unary");
    }
    m.node = expression_node(semantic_kind::unary, m, at, m.node, 0,
                             static_cast<uint8_t>(o));
    return analysis_result::well_formed;
}

/* [expr.unary.op]/3.2: "if the operand is an lvalue of type T, the
   resulting expression is a prvalue of type "pointer to T" whose result is
   a pointer to the designated object": m becomes it */
void semantic_analysis::address_of_glvalue(token_index at,
                                           expression_meaning &m)
{
    m.type = qualified_type::make(type_table.pointer_to(m.type), cv_none);
    m.category = value_category::prvalue;
    if (m.evaluation == constness::constant)
        m.evaluation = constness::unknown;
    m.entity = 0;
    m.string_literal = 0;
    m.node = expression_node(semantic_kind::address, m, at, m.node);
}

/* [class.bit]/3: "The address-of operator & shall not be applied to a
   bit-field, so there are no pointers to bit-fields." */
analysis_result semantic_analysis::address_of_bit_field(token_index t)
{
    return ill_formed_at_token(t,
                               "the address-of operator & shall not be "
                               "applied to a bit-field",
                               "class.bit");
}

/*
 * [expr.unary.op]/3.1: "If the operand is a qualified-id naming a
 * non-static or variant member m of some class C with type T, the result
 * has type "pointer to member of class C of type T" and is a prvalue
 * designating C::m."  C is the class m is declared in, whatever class the
 * nested-name-specifier names.
 */
analysis_result
semantic_analysis::member_pointer_constant(token_index t, entity_index e,
                                           expression_meaning &m)
{
    const entity &x = entity_table[e];
    entity_index c = x.enclosing;
    if (x.kind != entity_kind::variable && x.kind != entity_kind::function)
        return not_supported(current_declaration, "expr.unary.op");
    if (!c || entity_table[c].kind != entity_kind::class_type ||
        !entity_table[c].name)
        /* a variant member of an anonymous union: its offset is the
           union's plus its own */
        return not_supported(current_declaration, "expr.unary.op");
    if (!accessible(e))
        return ill_formed_at_token(t,
                                   "a private or protected member's name can "
                                   "be used only by members and friends of "
                                   "the class in which it is declared",
                                   "class.access");
    if (x.bit_field)
        return address_of_bit_field(t);
    const struct type &member = type_table[x.type.unqualified()];
    if (member.kind == type_kind::lvalue_reference ||
        member.kind == type_kind::rvalue_reference)
        /* [dcl.mptr]/3: "A pointer to member shall not point to ... a
           member with reference type" */
        return ill_formed_at_token(t,
                                   "a pointer to member shall not point to a "
                                   "member with reference type",
                                   "dcl.mptr");
    if (x.kind == entity_kind::function && x.member_cv)
        /* the function types analysis makes have no cv-qualifier-seq
           ([dcl.fct]/1) */
        return not_supported(current_declaration, "dcl.fct");
    m = expression_meaning();
    m.type =
        qualified_type::make(type_table.pointer_to_member(c, x.type), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::unknown;
    m.node = expression_node(semantic_kind::member_pointer, m, t, e);
    return analysis_result::well_formed;
}

} // namespace cplusplus
