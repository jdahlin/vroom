/*
 * unary-expression.cpp: [expr.unary] Unary expressions, with [expr.unary.op],
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
 * [ext:gnu_alignof_expression] (GCC manual 6.46):
 *
 *     unary-expression:
 *         alignof unary-expression
 */

/*
 * [ext:clang_datasizeof] (clang's __datasizeof):
 *
 *     unary-expression:
 *         __datasizeof ( type-id )
 *         __datasizeof unary-expression
 */

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
 * await-expression is await-expression.cpp's and sizeof ... ( identifier )
 * P7's.  The first alternative passes its postfix-expression on.
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
        at(preprocessing_op_or_punc::MINUS_MINUS) ||
        at(extension_keyword::real_) || at(extension_keyword::imag_)) {
        add_token();
        cast_expression();
        if (failed())
            return 0;
        return finish_node(syntax_kind::unary_expression, mark, first);
    }
    /* [ext:clang_datasizeof] __datasizeof has sizeof's operands, but
       sizeof... */
    bool data_size = at(extension_keyword::datasizeof_);
    if (at(standard_keyword::sizeof_) || data_size) {
        add_token();
        if (!data_size && at(preprocessing_op_or_punc::ELLIPSIS)) {
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
            /* [ext:gnu_alignof_expression] the alignment of the
               expression's type (clang's -Wgnu-alignof-expression) */
            if (!tokens.files.behavior.has(extension::gnu_alignof_expression))
                return expected("a type-id after alignof (", "expr.alignof");
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
    const_cast<semantic_analysis *>(this)->complete_class_type(t);
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
            /* binary128 (aarch64), and x87's 80 bits padded to 16 bytes
               (x86-64); 8 where it is a double (Darwin) */
            if (b.long_double_fraction_bits == 52)
                return 8;
            return 16;
        case fundamental_type::float128:
            return 16;
        case fundamental_type::float16:
        case fundamental_type::bfloat16:
            return 2;
        case fundamental_type::std_nullptr_t:
            return b.long_width / 8;
        case fundamental_type::builtin_va_list:
            return 32;
        case fundamental_type::builtin_va_list_tag:
            return 24;
        default:
            if (fundamental_type_is_integral(record.fundamental))
                return fundamental_type_width(b, record.fundamental) / 8;
            return 0;
        }
    case type_kind::pointer:
    case type_kind::block_pointer:
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
    case type_kind::atomic:
        return atomic_layout(record.operand, false);
    case type_kind::complex:
        /* [ext:gnu_complex] struct { T re, im; } */
        return 2 * size_of_type(record.operand);
    case type_kind::bit_int:
        return bit_int_layout(record.bound, false);
    case type_kind::ext_vector:
        return ext_vector_layout(record, false);
    case type_kind::vector:
        return vector_layout(record, false);
    case type_kind::array:
        if (!record.bound_known || record.variable_bound)
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

/* [ext:clang_vla] */
bool semantic_analysis::variable_length_array(qualified_type t) const
{
    const struct type &r = type_table[t.unqualified()];
    return r.kind == type_kind::array && r.variable_bound;
}

bool semantic_analysis::has_variable_bound(qualified_type t) const
{
    for (;;) {
        const struct type &r = type_table[t.unqualified()];
        switch (r.kind) {
        case type_kind::array:
            if (r.variable_bound)
                return true;
            t = r.operand;
            continue;
        case type_kind::pointer:
        case type_kind::lvalue_reference:
        case type_kind::rvalue_reference:
        case type_kind::block_pointer:
        case type_kind::pointer_to_member:
        case type_kind::atomic:
        case type_kind::complex:
            t = r.operand;
            continue;
        case type_kind::function: {
            const qualified_type *p = type_table.parameters(t.unqualified());
            for (uint32_t i = 0; i < r.parameter_count; i++)
                if (has_variable_bound(p[i]))
                    return true;
            t = r.operand;
            continue;
        }
        default:
            return false;
        }
    }
}

uint64_t power_of_two_at_least(uint64_t n)
{
    uint64_t p = 1;
    while (p < n)
        p <<= 1;
    return p;
}

/* [ext:clang_atomic] [impl.clang] _Atomic(T)'s size, or alignment: a T
   of up to 16 bytes is widened to the next power of two, aligned to it
   (C11's atomic types as clang lays them out) */
uint64_t semantic_analysis::atomic_layout(qualified_type value,
                                          bool alignment) const
{
    uint64_t size = size_of_type(value);
    if (!size || size > 16) {
        if (alignment)
            return alignment_of_type(value);
        return size;
    }
    return power_of_two_at_least(size);
}

uint64_t semantic_analysis::alignment_of_type(qualified_type t) const
{
    const struct type record = type_table[t.unqualified()];
    switch (record.kind) {
    case type_kind::atomic:
        return atomic_layout(record.operand, true);
    case type_kind::complex:
        return alignment_of_type(record.operand);
    case type_kind::bit_int:
        return bit_int_layout(record.bound, true);
    case type_kind::ext_vector:
        return ext_vector_layout(record, true);
    case type_kind::vector:
        return vector_layout(record, true);
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
        if (record.fundamental == fundamental_type::builtin_va_list ||
            record.fundamental == fundamental_type::builtin_va_list_tag)
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
analysis_result semantic_analysis::size_of(SyntaxRef n, bool alignment,
                                           expression_meaning &m,
                                           bool data_size)
{
    qualified_type operand;
    uint32_t typedef_alignment = 0;
    if (n.size() == 4 && n[2].is(syntax_kind::type_id)) {
        VTRY(r, type_id(n[2], operand, &typedef_alignment));
    } else if (n.size() == 2) {
        /* [expr.sizeof]/3: "The lvalue-to-rvalue, array-to-pointer, and
           function-to-pointer standard conversions are not applied to the
           operand of sizeof."  [ext:gnu_alignof_expression] alignof of an
           expression is the alignment of its type */
        expression_meaning e;
        /* [expr.sizeof]/1: "The operand is either an expression, which is
           an unevaluated operand ([expr.prop])" */
        unevaluated++;
        atomic_object_operand = true;
        analysis_result r = expression(n[1], e);
        unevaluated--;
        if (r != analysis_result::well_formed)
            return r;
        /* [expr.sizeof]/1: "The sizeof operator shall not be applied to
           ... a glvalue that designates a bit-field." */
        if (e.bit_field_width) {
            if (alignment)
                return ill_formed(n,
                                  "the operand of alignof shall not be a "
                                  "glvalue that designates a bit-field",
                                  "expr.alignof");
            return ill_formed(n,
                              "the sizeof operator shall not be applied to a "
                              "glvalue that designates a bit-field",
                              "expr.sizeof");
        }
        operand = e.type;
    } else if (!alignment && n.size() == 5 &&
               n[1].is(preprocessing_op_or_punc::ELLIPSIS) && n[3].is_token() &&
               n[3].is_identifier()) {
        return pack_size(n, m);
    } else {
        VBUG("sizeof or alignof node of an unknown shape");
    }
    analysis_result r = sized_operand(n, alignment, operand);
    if (r != analysis_result::well_formed)
        return r;
    /* [ext:clang_vla] sizeof a variable length array: the bytes its bound
       made, read at run time (the bound was evaluated where the array was
       declared) */
    if (!alignment && !data_size) {
        qualified_type t = operand;
        while (type_table[t.unqualified()].kind ==
                   type_kind::lvalue_reference ||
               type_table[t.unqualified()].kind == type_kind::rvalue_reference)
            t = type_table[t.unqualified()].operand;
        if (variable_length_array(t)) {
            m.type = qualified_type::make(
                type_table.fundamental(fundamental_type::unsigned_long_int),
                cv_none);
            m.category = value_category::prvalue;
            m.evaluation = constness::not_constant;
            m.value = 0;
            m.beyond_integral_constant = true;
            m.node = expression_node(semantic_kind::variable_size, m,
                                     n.first_token());
            semantics[m.node].computation = t;
            return analysis_result::well_formed;
        }
    }
    uint64_t size = size_of_type(operand);
    if (data_size) {
        /* [ext:clang_datasizeof] a class's data size ([abi:layout]), the
           size of the rest */
        qualified_type t = operand;
        while (type_table[t.unqualified()].kind ==
                   type_kind::lvalue_reference ||
               type_table[t.unqualified()].kind == type_kind::rvalue_reference)
            t = type_table[t.unqualified()].operand;
        const struct type record = type_table[t.unqualified()];
        if (record.kind == type_kind::class_type)
            size = entity_table[record.entity].data_size;
    }
    if (alignment) {
        size = alignment_of_type(operand);
        /* [ext:gnu_attributes] alignof of a typedef-name declared with
           aligned */
        if (typedef_alignment > size)
            size = typedef_alignment;
    }
    m.type = qualified_type::make(
        type_table.fundamental(fundamental_type::unsigned_long_int), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::constant;
    m.value = size;
    /* the operand is unevaluated ([expr.sizeof]/1): the constant alone */
    m.node = expression_node(semantic_kind::constant, m, n.first_token());
    return analysis_result::well_formed;
}

/*
 * [expr.sizeof]/4: "The identifier in a sizeof... expression shall name a
 * pack.  The sizeof... operator yields the number of elements in the pack
 * ([temp.variadic]).  A sizeof... expression is a pack expansion
 * ([temp.variadic])."  An instantiation's pack; a template's own is not
 * analysed.
 */
analysis_result semantic_analysis::pack_size(SyntaxRef n, expression_meaning &m)
{
    uint32_t name = tokens.identifier(n[3].token());
    entity_index pack = find_pack(name);
    if (!pack) {
        /* a name that is certainly no pack is the error; one that may be a
           pack the analysis does not bind (a function parameter pack of
           reference type, a template's own) is not */
        lookup_result found = lookup_unqualified(name, lookup_filter::any);
        if (found.entity && !own_pack_parameter(name)) {
            const entity &x = entity_table[found.entity];
            bool no_pack =
                x.kind == entity_kind::class_type ||
                x.kind == entity_kind::enumeration ||
                x.kind == entity_kind::enumerator ||
                x.kind == entity_kind::function ||
                x.kind == entity_kind::namespace_entity ||
                (x.kind == entity_kind::variable && is_namespace(x.enclosing));
            if (no_pack)
                return ill_formed(n,
                                  "the identifier in a sizeof... expression "
                                  "shall name a pack",
                                  "expr.sizeof");
        }
        return not_supported(n, "temp.variadic");
    }
    m.type = qualified_type::make(
        type_table.fundamental(fundamental_type::unsigned_long_int), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::constant;
    m.value = entity_table[pack].member_count;
    m.node = expression_node(semantic_kind::constant, m, n.first_token());
    return analysis_result::well_formed;
}

/*
 * [expr.sizeof]/1: "The sizeof operator shall not be applied to an
 * expression that has function or incomplete type, to the parenthesized
 * name of such types, or to a glvalue that designates a bit-field."
 * [expr.alignof]/1: "The operand shall be a type-id representing a complete
 * object type, or an array thereof, or a reference to one of those types."
 * [expr.sizeof]/2 and [expr.alignof]/3: a reference's operand is the
 * referenced type.  The one place that says whether a type has a size and an
 * alignment: size_of_type and alignment_of_type give 0 for the rest.
 */
analysis_result semantic_analysis::sized_operand(SyntaxRef n, bool alignment,
                                                 qualified_type operand)
{
    const char *subclause = "expr.sizeof";
    const char *incomplete =
        "the sizeof operator shall not be applied to an expression that has "
        "function or incomplete type, or to the parenthesized name of such "
        "types";
    if (alignment) {
        subclause = "expr.alignof";
        incomplete = "the operand of alignof shall represent a complete "
                     "object type, or an array thereof, or a reference to "
                     "one of those types";
    }
    qualified_type t = operand;
    /* a reference's operand is the referenced type's */
    while (type_table[t.unqualified()].kind == type_kind::lvalue_reference ||
           type_table[t.unqualified()].kind == type_kind::rvalue_reference)
        t = type_table[t.unqualified()].operand;
    /* an array: its element's, an unknown bound only for alignof ("or an
       array thereof") */
    while (type_table[t.unqualified()].kind == type_kind::array) {
        if (!type_table[t.unqualified()].bound_known && !alignment)
            return ill_formed(n, incomplete, subclause);
        t = type_table[t.unqualified()].operand;
    }
    const struct type record = type_table[t.unqualified()];
    switch (record.kind) {
    case type_kind::function:
        return ill_formed(n, incomplete, subclause);
    case type_kind::fundamental:
        if (record.fundamental == fundamental_type::void_)
            return ill_formed(n, incomplete, subclause);
        return analysis_result::well_formed;
    case type_kind::class_type: {
        const entity &c = entity_table[record.entity];
        if (c.kind != entity_kind::class_type)
            /* a class whose definition was not analysed */
            return not_supported(n, "class.pre");
        /* [class.mem]/6: a class is incomplete until its closing brace
           (the size is 0 until then; an empty class's is 1) */
        if (!c.size)
            return ill_formed(n, incomplete, subclause);
        return analysis_result::well_formed;
    }
    case type_kind::enumeration:
        /* [dcl.enum]/6: an enumeration is incomplete until its underlying
           type is fixed or its enumerators are defined */
        if (entity_table[record.entity].underlying == fundamental_type::none)
            return ill_formed(n, incomplete, subclause);
        return analysis_result::well_formed;
    case type_kind::template_parameter:
    case type_kind::dependent:
    case type_kind::pack_expansion:
    case type_kind::placeholder:
    case type_kind::none:
        /* [temp.dep.constexpr]/2: value-dependent (or, a placeholder, not
           yet a type): the operand of a template's own declaration */
        return not_supported(n, "temp.dep.constexpr");
    default:
        if (!size_of_type(t))
            return not_supported(n, "basic.compound");
        return analysis_result::well_formed;
    }
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
analysis_result semantic_analysis::increment(SyntaxRef n, SyntaxElement operand,
                                             bool decrement, bool postfix,
                                             expression_meaning &m)
{
    /* [ext:clang_atomic] ++ and -- of an atomic object: one atomic
       read-modify-write (C's code generator) */
    atomic_object_operand = true;
    VTRY(r, expression(operand, m));
    return increment_of(n, n.first_token(), operand, decrement, postfix, m);
}

/* ... of the operand analysed as m (its syntax operand, none for an
   operand [stmt.ranged]/1 makes) */
analysis_result semantic_analysis::increment_of(SyntaxRef n, token_index at,
                                                SyntaxElement operand,
                                                bool decrement, bool postfix,
                                                expression_meaning &m)
{
    analysis_result r = analysis_result::well_formed;
    {
        /* [over.inc]: an operand of class or enumeration type
           ([over.match.oper]/2) */
        preprocessing_op_or_punc op = preprocessing_op_or_punc::PLUS_PLUS;
        if (decrement)
            op = preprocessing_op_or_punc::MINUS_MINUS;
        bool handled = false;
        r = unary_operator_function(n, at, op, postfix, operand, m, handled);
        if (r != analysis_result::well_formed || handled)
            return r;
    }
    const char *subclause = "expr.pre.incr";
    if (postfix)
        subclause = "expr.post.incr";
    /* [ext:clang_atomic] an atomic object's value type has the rules; the
       prefix forms' result is the atomic object, the postfix forms' a
       value of the value type (clang's) */
    qualified_type atomic_type;
    if (type_index value = atomic_value_type(m.type)) {
        atomic_type = m.type;
        m.type = qualified_type::make(value, m.type.cv());
    }
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
    if (atomic_type.unqualified() && !postfix)
        m.type = atomic_type;
    m.node = expression_node(semantic_kind::increment, m, at, m.node, 0,
                             static_cast<uint8_t>(kind));
    return analysis_result::well_formed;
}

/*
 * [expr.unary.op]: the unary operators *, &, +, -, ! and ~; [expr.pre.incr]
 * prefix ++ and --; [expr.sizeof] and [expr.alignof].
 */
analysis_result semantic_analysis::unary(SyntaxRef n, expression_meaning &m)
{
    if (n.size() < 2 || !n[0].is_token())
        return not_supported(n, "expr.unary");
    const token &op = tokens[n[0].token()];
    if (op.kind == token_kind::keyword) {
        standard_keyword kw = p.keyword_of(op);
        if (kw == standard_keyword::sizeof_)
            return size_of(n, false, m);
        if (kw == standard_keyword::alignof_)
            return size_of(n, true, m);
        if (p.extension_keyword_of(op) == extension_keyword::datasizeof_)
            return size_of(n, false, m, true);
        extension_keyword ek = p.extension_keyword_of(op);
        if (ek == extension_keyword::real_ || ek == extension_keyword::imag_) {
            expression_meaning operand;
            VTRY(r, expression(n[1], operand));
            const struct type ot = type_table[operand.type.unqualified()];
            if (ot.kind != type_kind::complex)
                return not_supported(n, "ext.gnu");
            uint8_t part = 0;
            if (ek == extension_keyword::imag_)
                part = 1;
            m = expression_meaning();
            m.type = qualified_type::make(ot.operand.unqualified(),
                                          operand.type.cv());
            m.category = operand.category;
            m.evaluation = operand.evaluation;
            m.beyond_integral_constant = true;
            m.node = expression_node(semantic_kind::complex_part, m,
                                     n.first_token(), operand.node, 0, part);
            return analysis_result::well_formed;
        }
        return not_supported(n, "expr.unary");
    }
    if (op.kind != token_kind::operator_or_punctuator || n.size() != 2)
        return not_supported(n, "expr.unary");
    preprocessing_op_or_punc o =
        preprocessing_op_or_punc_primary(op.op_or_punc);
    if (o == preprocessing_op_or_punc::PLUS_PLUS ||
        o == preprocessing_op_or_punc::MINUS_MINUS)
        return increment(n, n[1], o == preprocessing_op_or_punc::MINUS_MINUS,
                         false, m);
    /* [expr.unary.op]/4: "A pointer to member is only formed when an
       explicit & is used and its operand is a qualified-id not enclosed in
       parentheses." */
    SyntaxRef outer_operand = member_pointer_operand;
    member_pointer_operand = 0;
    if (o == preprocessing_op_or_punc::AMPERSAND &&
        n[1].is(syntax_kind::qualified_id))
        member_pointer_operand = n[1].index();
    /* [ext:clang_atomic] &a of an atomic object a: its address */
    atomic_object_operand = o == preprocessing_op_or_punc::AMPERSAND;
    analysis_result r = expression(n[1], m);
    member_pointer_operand = outer_operand;
    if (r != analysis_result::well_formed)
        return r;
    if (m.node && semantics[m.node].kind == semantic_kind::member_pointer)
        return analysis_result::well_formed;
    const implementation_defined_behavior &b = tokens.files.behavior;
    token_index at = n[0].token();
    if (o == preprocessing_op_or_punc::STAR)
        return indirection_of(n, at, n[1], m);
    /* [over.unary]: an operand of class or enumeration type
       ([over.match.oper]/2) */
    bool handled = false;
    r = unary_operator_function(n, at, o, false, n[1], m, handled);
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
            return address_of_bit_field(n.first_token());
        if (m.node && semantics[m.node].kind == semantic_kind::subscript &&
            type_table[semantics[semantics[m.node].a].type.unqualified()]
                    .kind == type_kind::vector)
            /* [ext:gnu_vector_size] clang: "address of vector element
               requested" */
            return ill_formed(n, "the address of a vector element", "ext.gnu");
        address_of_glvalue(at, m);
        return analysis_result::well_formed;
    }
    case preprocessing_op_or_punc::PLUS:
    case preprocessing_op_or_punc::MINUS:
    case preprocessing_op_or_punc::TILDE: {
        VTRY_SET(r, operand_value(n, m));
        /* [expr.unary.op]/7, /8, /10: "arithmetic or unscoped
           enumeration type", "integral or unscoped enumeration type" */
        promote_enumeration(m);
        if (type_table[m.type.unqualified()].kind == type_kind::vector) {
            /* [ext:gnu_vector_size] +, - and ~ lane by lane: ~ of integer
               elements, a vector of the operand's type */
            if (o == preprocessing_op_or_punc::TILDE &&
                !is_integral(type_table[m.type.unqualified()].operand))
                return ill_formed(n,
                                  "the operand of ~ shall be a vector of "
                                  "integer elements",
                                  "ext.gnu");
            m.type = qualified_type::make(m.type.unqualified(), cv_none);
            m.evaluation = constness::not_constant;
            m.beyond_integral_constant = true;
            break;
        }
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
        VTRY_SET(r, contextual_bool(n, m));
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

/* [expr.unary.op]/1: *m of the operand analysed as m (its syntax operand,
   none for an operand [stmt.ranged]/1 makes) */
analysis_result semantic_analysis::indirection_of(SyntaxRef n, token_index at,
                                                  SyntaxElement operand,
                                                  expression_meaning &m)
{
    {
        /* [over.unary]: an operand of class or enumeration type
           ([over.match.oper]/2) */
        bool handled = false;
        analysis_result r = unary_operator_function(
            n, at, preprocessing_op_or_punc::STAR, false, operand, m, handled);
        if (r != analysis_result::well_formed || handled)
            return r;
    }
    /* [expr.unary.op]/1: "the expression to which it is applied shall
       be a pointer to an object type, or a pointer to a function type
       and the result is an lvalue referring to the object or function
       to which the expression points.  If the type of the expression
       is "pointer to T", the type of the result is "T"." */
    VTRY(r, operand_value(n, m));
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
    /* [dcl.fct]/1: the function type of a member function with a
       cv-qualifier-seq has it (the entity keeps it as member_cv) */
    qualified_type pointee = x.type;
    if (x.kind == entity_kind::function && x.member_cv) {
        uint8_t ref = 0;
        if (x.member_cv & cv_ref_lvalue)
            ref = 1;
        if (x.member_cv & cv_ref_rvalue)
            ref = 2;
        pointee = qualified_type::make(
            type_table.function_qualified(x.type.unqualified(),
                                          object_cv(x.member_cv), ref),
            cv_none);
    }
    m = expression_meaning();
    m.type =
        qualified_type::make(type_table.pointer_to_member(c, pointee), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::unknown;
    m.node = expression_node(semantic_kind::member_pointer, m, t, e);
    return analysis_result::well_formed;
}

/*
 * [expr.unary.noexcept]/2: "The result of the noexcept operator is a
 * prvalue of type bool.  The result is false if the full-expression of
 * the operand is potentially-throwing ([except.spec]), and true
 * otherwise."  [except.spec]/6: "An expression E is potentially-throwing
 * if (6.1) E is a function call whose postfix-expression has a function
 * type, or a pointer-to-function type, with a potentially-throwing
 * exception specification, or (6.2) E implicitly invokes a function (such
 * as an overloaded operator, an allocation function in a new-expression,
 * a constructor for a function argument, or a destructor if E is a
 * full-expression) that has a potentially-throwing exception
 * specification, or (6.3) E is a throw-expression, ... or (6.6) any of
 * the immediate subexpressions of E is potentially-throwing."  The
 * operand's semantic tree is walked; a node analysis has no rule for here
 * refuses.
 */
analysis_result semantic_analysis::noexcept_operator(SyntaxRef n,
                                                     expression_meaning &m)
{
    /* before C++17 a function's exception specification is not part of
       its type, and not kept (parameter-declaration-clause.cpp) */
    if (n.size() != 4 || !tokens.files.behavior.has(
                             language_feature::exception_specification_in_type))
        return not_supported(n, "expr.unary.noexcept");
    expression_meaning e;
    unevaluated++;
    analysis_result r = expression(n[2], e);
    unevaluated--;
    if (r != analysis_result::well_formed)
        return r;
    bool throwing = false;
    VTRY_SET(r, potentially_throwing(n, e.node, throwing));
    m = integer_constant(fundamental_type::bool_, !throwing, n.first_token());
    return analysis_result::well_formed;
}

/* [except.spec]/6 of the semantic node x (0: none) */
analysis_result semantic_analysis::potentially_throwing(SyntaxRef n, uint32_t x,
                                                        bool &throwing)
{
    owned_array<uint32_t> work;
    if (x)
        work.append(x);
    while (!work.empty() && !throwing) {
        uint32_t k = work.back();
        work.resize(work.size() - 1);
        /* an operand with no node of its own (a declval<T>() of the
           traits) cannot throw */
        if (!k)
            continue;
        const semantic_node s = semantics[k];
        switch (s.kind) {
        case semantic_kind::constant:
        case semantic_kind::string_literal:
        case semantic_kind::entity:
        case semantic_kind::zero:
            break;
        case semantic_kind::conversion:
        case semantic_kind::unary:
        case semantic_kind::address:
        case semantic_kind::indirection:
        case semantic_kind::increment:
        case semantic_kind::bind_reference:
        case semantic_kind::member:
        case semantic_kind::bound_member_function:
            if (s.a)
                work.append(s.a);
            break;
        case semantic_kind::binary:
        case semantic_kind::logical_and:
        case semantic_kind::logical_or:
        case semantic_kind::assignment:
        case semantic_kind::comma:
        case semantic_kind::subscript:
            work.append(s.a);
            work.append(s.b);
            break;
        case semantic_kind::conditional:
            work.append(s.a);
            work.append(s.b);
            work.append(s.c);
            break;
        case semantic_kind::initializer_list:
            for (uint32_t i = 0; i < s.count; i++)
                work.append(semantics.items(s.first)[i]);
            break;
        case semantic_kind::call: {
            /* /6.1: the callee's function type */
            uint32_t callee_node = s.a;
            qualified_type t = semantics[callee_node].type;
            if (type_table[t.unqualified()].kind == type_kind::pointer)
                t = type_table[t.unqualified()].operand;
            const struct type &f = type_table[t.unqualified()];
            if (f.kind != type_kind::function)
                return not_supported(n, "expr.unary.noexcept");
            /* clang: a function with the nothrow attribute has a
               non-throwing type */
            const semantic_node &callee = semantics[callee_node];
            bool nothrow = callee.kind == semantic_kind::entity && callee.a &&
                           (entity_table[callee.a].gnu_flags & gnu_nothrow);
            /* [impl.clang] the attribute is ignored with a
               noexcept(false): whether there is one is not kept */
            if (nothrow && !f.non_throwing)
                return not_supported(n, "expr.unary.noexcept");
            if (!f.non_throwing) {
                throwing = true;
                break;
            }
            work.append(s.a);
            if (s.b)
                work.append(s.b);
            for (uint32_t i = 0; i < s.count; i++)
                work.append(semantics.items(s.first)[i]);
            break;
        }
        case semantic_kind::construct: {
            /* /6.2: the constructor; an implicit one's exception
               specification is its subobjects' ([except.spec]/8-9) */
            if (s.b) {
                const entity &c = entity_table[s.b];
                if (c.implicit) {
                    bool known = true;
                    if (implicit_constructor_throws(s.b, known)) {
                        throwing = true;
                        break;
                    }
                    if (!known)
                        return not_supported(n, "except.spec");
                } else if (!type_table[c.type.unqualified()].non_throwing) {
                    throwing = true;
                    break;
                }
            }
            if (s.a)
                work.append(s.a);
            for (uint32_t i = 0; i < s.count; i++)
                work.append(semantics.items(s.first)[i]);
            break;
        }
        default:
            /* a temporary's destruction, a new- or delete-expression, a
               member pointer: no rule here */
            return not_supported(n, "expr.unary.noexcept");
        }
    }
    return analysis_result::well_formed;
}

} // namespace cplusplus
