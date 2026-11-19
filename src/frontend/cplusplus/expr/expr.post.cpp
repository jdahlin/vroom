/*
 * expr.post.cpp: [expr.post] Postfix expressions, with the subclauses
 * that give its alternatives their meaning: [expr.sub], [expr.call],
 * [expr.type.conv], [expr.ref], [expr.post.incr], [expr.dynamic.cast],
 * [expr.typeid], [expr.static.cast], [expr.reinterpret.cast],
 * [expr.const.cast].
 *
 *     postfix-expression:
 *         primary-expression
 *         postfix-expression [ expr-or-braced-init-list ]
 *         postfix-expression ( expression-list_opt )
 *         simple-type-specifier ( expression-list_opt )
 *         typename-specifier ( expression-list_opt )
 *         simple-type-specifier braced-init-list
 *         typename-specifier braced-init-list
 *         postfix-expression . template_opt id-expression
 *         postfix-expression -> template_opt id-expression
 *         postfix-expression ++
 *         postfix-expression --
 *         dynamic_cast < type-id > ( expression )
 *         static_cast < type-id > ( expression )
 *         reinterpret_cast < type-id > ( expression )
 *         const_cast < type-id > ( expression )
 *         typeid ( expression )
 *         typeid ( type-id )
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [expr.post]/1: "Postfix expressions group left-to-right": what begins
 * one (a primary-expression, a type with its initializer, a named cast,
 * typeid) is the operand of the first suffix, and each suffix makes the
 * operand of the next.  With no suffix, the primary-expression passes
 * through.
 *
 * [expr.type.conv]/1: "A simple-type-specifier or typename-specifier
 * followed by a parenthesized optional expression-list or by a
 * braced-init-list (the initializer) constructs a value of the specified
 * type given the initializer."  A name begins this alternative when lookup
 * finds a type; otherwise it is a primary-expression.
 *
 * typename-specifier is P7's, as are the template after . and -> and a
 * member's template-id; a placeholder for a deduced type (auto(x),
 * P0849R8, C++23) is not n4861's.
 */
node_index parser::postfix_expression()
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    size_t mark = begin_node();
    token_index first = position;
    node_index n = 0;
    standard_keyword k = keyword_of(peek());
    bool type = false;
    switch (k) {
    case standard_keyword::dynamic_cast_:
    case standard_keyword::static_cast_:
    case standard_keyword::reinterpret_cast_:
    case standard_keyword::const_cast_:
        add_token();
        if (!accept(preprocessing_op_or_punc::LESS))
            return expected("'<' after the cast's keyword", "expr.post");
        {
            flag_set closing(greater_is_operator, false);
            /* [temp.res]/5: the type-id of a static_cast ... */
            flag_set only(type_id_only, true);
            type_id();
        }
        if (failed())
            return 0;
        if (!accept_closing_angle())
            return expected("'>' after the type-id", "expr.post");
        if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return expected("'(' before the cast's operand", "expr.post");
        expression();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' after the cast's operand", "expr.post");
        n = finish_node(syntax_kind::postfix_expression, mark, first);
        break;
    case standard_keyword::typeid_:
        add_token();
        if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return expected("'(' after typeid", "expr.typeid");
        /* typeid ( type-id ) when the operand can be one
           ([dcl.ambig.res]/2) */
        if (!parenthesized_type_id(false)) {
            if (failed())
                return 0;
            add_token();
            expression();
            if (failed())
                return 0;
            if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return expected("')' after the operand of typeid",
                                "expr.typeid");
        }
        n = finish_node(syntax_kind::postfix_expression, mark, first);
        break;
    case standard_keyword::typename_:
        /* [expr.type.conv]: typename-specifier ( expression-list_opt ) or
           braced-init-list */
        typename_specifier();
        type = true;
        break;
    case standard_keyword::auto_:
        return beyond_this_step();
    case standard_keyword::decltype_:
        /* decltype-specifier :: begins a qualified-id */
        if (!at(preprocessing_op_or_punc::COLON_COLON, group_end(1))) {
            if (at(standard_keyword::auto_, 2))
                return beyond_this_step(); /* decltype(auto)(x): P0849R8 */
            decltype_specifier();
            type = true;
        }
        break;
    default:
        if (is_simple_type_keyword(k)) {
            add_token();
            type = true;
        } else if (at_identifier() ||
                   at(preprocessing_op_or_punc::COLON_COLON)) {
            unsigned length = 0;
            if (classify_name(0, &length, false) == name_class::type) {
                if (at(preprocessing_op_or_punc::LESS, length))
                    return beyond_this_step(); /* a template-id: P7 */
                simple_type_specifier_with_name();
                type = true;
            }
        }
        break;
    }
    if (failed())
        return 0;
    if (type) {
        if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
            if (!parenthesized_expression_list(
                    "')' to close the expression-list", "expr.type.conv"))
                return 0;
        } else if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
            if (list_initialization_missing())
                return 0;
            braced_init_list();
            if (failed())
                return 0;
        } else {
            return expected("'(' or a braced-init-list after the "
                            "simple-type-specifier",
                            "expr.type.conv");
        }
        n = finish_node(syntax_kind::postfix_expression, mark, first);
    } else if (!n) {
        n = primary_expression();
        if (failed())
            return 0;
    }
    return postfix_expression_suffixes(mark, first, n);
}

/* the suffixes after what begins a postfix-expression n, which starts at
   mark and first */
node_index parser::postfix_expression_suffixes(size_t mark, token_index first,
                                               node_index n)
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    for (;;) {
        if (at(preprocessing_op_or_punc::LEFT_BRACKET)) {
            /* [dcl.attr.grammar]/7: "Two consecutive left square bracket
               tokens shall appear only when introducing an
               attribute-specifier": a[[]{ return 0; }()] is ill-formed */
            if (at(preprocessing_op_or_punc::LEFT_BRACKET, 1))
                return misplaced_attribute_brackets();
            add_token();
            if (b.has(language_feature::multidimensional_subscript)) {
                /* P2128R6 (C++23):
                       postfix-expression [ expression-list_opt ] */
                if (!at(preprocessing_op_or_punc::RIGHT_BRACKET))
                    expression_list();
            } else if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
                if (!list_initialization_missing())
                    braced_init_list();
            } else {
                expression();
            }
            if (failed())
                return 0;
            if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET))
                return expected("']' to close the subscript", "expr.sub");
        } else if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
            if (!parenthesized_expression_list(
                    "')' to close the expression-list", "expr.call"))
                return 0;
        } else if (at(preprocessing_op_or_punc::DOT) ||
                   at(preprocessing_op_or_punc::ARROW)) {
            add_token();
            id_expression(true);
            if (failed())
                return 0;
        } else if (at(preprocessing_op_or_punc::PLUS_PLUS) ||
                   at(preprocessing_op_or_punc::MINUS_MINUS)) {
            add_token();
        } else {
            break;
        }
        n = finish_node(syntax_kind::postfix_expression, mark, first);
    }
    return n;
}

/*
 *     expression-list:
 *         initializer-list
 */
node_index parser::expression_list()
{
    return initializer_list(false);
}

bool parser::parenthesized_expression_list(const char *what,
                                           const char *subclause)
{
    add_token(); /* ( */
    if (!at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        expression_list();
        if (failed())
            return false;
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        expected(what, subclause);
        return false;
    }
    return true;
}

/* ---- semantic analysis ---------------------------------------------- */

/*
 * The postfix expressions analysis has rules for: [expr.sub] subscripting,
 * [expr.post.incr] postfix ++ and --, and [expr.type.conv] the functional
 * notation T(e), T() and T{e} of a fundamental type.  A function call
 * ([expr.call]) needs overload resolution and is not analysed yet.
 */
/*
 * The named casts: [expr.static.cast]/1: "The result of the expression
 * static_cast<T>(v) is the result of converting the expression v to type
 * T."; [expr.const.cast]/1: "The result of the expression const_cast<T>(v)
 * is of type T."; [expr.reinterpret.cast]/1: "The result of the expression
 * reinterpret_cast<T>(v) is the result of converting the expression v to
 * type T."  Each the conversions of its own subclause, explicit_conversion
 * (expr.cast.cpp) with that cast alone.  dynamic_cast needs the run-time
 * type information, not analysed.
 */
analysis_result semantic_analysis::named_cast(node_index n,
                                              standard_keyword keyword,
                                              expression_meaning &m)
{
    uint8_t kinds = cast_static;
    if (keyword == standard_keyword::const_cast_)
        kinds = cast_const;
    else if (keyword == standard_keyword::reinterpret_cast_)
        kinds = cast_reinterpret;
    else if (keyword != standard_keyword::static_cast_)
        return not_supported(n, "expr.dynamic.cast");
    auto children = syntax(n);
    qualified_type to;
    analysis_result r = type_id(children[2].index(), to);
    /* [over.over]/1: "an explicit type conversion" */
    if (r == analysis_result::well_formed)
        r = target_expression(children[5], to, m);
    if (r != analysis_result::well_formed)
        return r;
    return explicit_conversion(n, to, m, kinds);
}

analysis_result semantic_analysis::postfix(node_index n, expression_meaning &m)
{
    auto node = syntax(n);
    if (node.size() == 2 && node[1].is_token()) {
        preprocessing_op_or_punc op = preprocessing_op_or_punc_primary(
            tokens[node[1].token()].op_or_punc);
        if (op == preprocessing_op_or_punc::PLUS_PLUS ||
            op == preprocessing_op_or_punc::MINUS_MINUS)
            return increment(n, node[0],
                             op == preprocessing_op_or_punc::MINUS_MINUS, true,
                             m);
        return not_supported(n, "expr.post");
    }
    if (node.size() < 2)
        return not_supported(n, "expr.post");
    if (node.size() == 7 && node[0].is_token() &&
        node[2].is(syntax_kind::type_id)) {
        standard_keyword k = p.keyword_of(tokens[node[0].token()]);
        if (k == standard_keyword::static_cast_ ||
            k == standard_keyword::const_cast_ ||
            k == standard_keyword::reinterpret_cast_ ||
            k == standard_keyword::dynamic_cast_)
            return named_cast(n, k, m);
    }
    if (node.size() == 3 && (node[1].is(preprocessing_op_or_punc::DOT) ||
                             node[1].is(preprocessing_op_or_punc::ARROW)))
        return member_reference(n, m);
    /* [expr.type.conv]/1: "A simple-type-specifier or typename-specifier
       followed by a parenthesized optional expression-list or by a
       braced-init-list (the initializer) constructs a value of the
       specified type given the initializer." */
    bool parenthesized = node[1].is(preprocessing_op_or_punc::LEFT_PARENTHESIS);
    bool braced = node[1].is(syntax_kind::braced_init_list);
    type_index named = 0;
    /* [dcl.type.simple]/1: nested-name-specifier(opt) type-name */
    entity_index class_named = 0;
    uint32_t named_cv = 0;
    if ((parenthesized || braced) &&
        node[0].is(syntax_kind::simple_type_specifier)) {
        analysis_result r =
            qualified_type_name(node[0].index(), named, named_cv);
        if (r != analysis_result::well_formed)
            return r;
        if (type_table[named].kind == type_kind::class_type &&
            entity_table[type_table[named].entity].kind ==
                entity_kind::class_type)
            class_named = type_table[named].entity;
    }
    if ((parenthesized || braced) && node[0].is_token()) {
        const token &t = tokens[node[0].token()];
        if (t.kind == token_kind::identifier) {
            entity_index e = lookup(tokens.identifier(node[0].token()));
            if (e && entity_table[e].kind == entity_kind::class_type)
                class_named = e;
            else if (e && (entity_table[e].kind == entity_kind::typedef_name ||
                           entity_table[e].kind == entity_kind::enumeration)) {
                named = entity_table[e].type.unqualified();
                named_cv = entity_table[e].type.cv();
            }
        } else if (t.kind == token_kind::keyword) {
            simple_type_keywords k;
            switch (p.keyword_of(t)) {
            case standard_keyword::signed_:
                k.signed_ = 1;
                break;
            case standard_keyword::unsigned_:
                k.unsigned_ = 1;
                break;
            case standard_keyword::short_:
                k.short_ = 1;
                break;
            case standard_keyword::long_:
                k.long_ = 1;
                break;
            case standard_keyword::char_:
                k.char_ = 1;
                break;
            case standard_keyword::int_:
                k.int_ = 1;
                break;
            case standard_keyword::double_:
                k.double_ = 1;
                break;
            case standard_keyword::bool_:
                k.alone = 1;
                k.alone_type = fundamental_type::bool_;
                break;
            case standard_keyword::float_:
                k.alone = 1;
                k.alone_type = fundamental_type::float_;
                break;
            case standard_keyword::void_:
                k.alone = 1;
                k.alone_type = fundamental_type::void_;
                break;
            case standard_keyword::wchar_t_:
                k.alone = 1;
                k.alone_type = fundamental_type::wchar_t_;
                break;
            case standard_keyword::char8_t_:
                k.alone = 1;
                k.alone_type = fundamental_type::char8_t_;
                break;
            case standard_keyword::char16_t_:
                k.alone = 1;
                k.alone_type = fundamental_type::char16_t_;
                break;
            case standard_keyword::char32_t_:
                k.alone = 1;
                k.alone_type = fundamental_type::char32_t_;
                break;
            default:
                return not_supported(n, "expr.type.conv");
            }
            analysis_result r = simple_type(n, k, named);
            if (r != analysis_result::well_formed)
                return r;
        }
    }
    if (named_cv)
        return not_supported(n, "expr.type.conv");
    if (class_named)
        return class_conversion(n, class_named, m);
    if ((parenthesized || braced) &&
        node[0].is(syntax_kind::simple_template_id)) {
        /* [temp.names]/1 with [expr.type.conv]/1: a simple-template-id
           naming a class template is a simple-type-specifier
           ([dcl.type.simple]/1), the specialization's class */
        auto id = node[0];
        if (node[0].size() >= 3 && id[0].is_token() &&
            tokens[id[0].token()].kind == token_kind::identifier) {
            lookup_result x = lookup_unqualified(
                tokens.identifier(id[0].token()), lookup_filter::any);
            entity_index found = x.entity;
            if (!found)
                found = x.unknown_entity;
            if (class_template_of(found)) {
                type_index t = 0;
                analysis_result r = template_type(node[0].index(), 0, false, t);
                if (r != analysis_result::well_formed)
                    return r;
                if (type_table[t].kind != type_kind::class_type)
                    return not_supported(n, "temp.res");
                return class_conversion(n, type_table[t].entity, m);
            }
        }
    }
    if (named) {
        qualified_type to = qualified_type::make(named, cv_none);
        const struct type record = type_table[named];
        if (record.kind == type_kind::class_type &&
            entity_table[record.entity].kind == entity_kind::class_type)
            /* a typedef-name of a class */
            return class_conversion(n, record.entity, m);
        if (record.kind != type_kind::fundamental &&
            record.kind != type_kind::pointer)
            return not_supported(n, "expr.type.conv");
        bool is_void = record.kind == type_kind::fundamental &&
                       record.fundamental == fundamental_type::void_;
        if (parenthesized && node.size() == 3) {
            /* [expr.type.conv]/2: "Otherwise, if the type is cv void and the
               initializer is () or {}, the expression is a prvalue of the
               specified type that performs no initialization.  Otherwise,
               the expression is a prvalue of the specified type whose result
               object is direct-initialized with the initializer."  T() of a
               scalar: value-initialized ([dcl.init]/11), zero; void(): a
               constant of type void, which has no value */
            m = expression_meaning();
            m.type = to;
            m.evaluation = constness::constant;
            m.value = 0;
            m.node =
                expression_node(semantic_kind::constant, m, node.first_token());
            return analysis_result::well_formed;
        }
        if (parenthesized && node.size() == 4) {
            if (node[2].is(syntax_kind::initializer_list)) {
                if (has_pack_expansion(node[2]))
                    return not_supported(n, "temp.variadic");
                /* [dcl.init]/14: "If the entity being initialized does not
                   have class type, the expression-list in a parenthesized
                   initializer shall be a single expression." */
                return ill_formed(n,
                                  "if the entity being initialized does not "
                                  "have class type, the expression-list in a "
                                  "parenthesized initializer shall be a "
                                  "single expression",
                                  "dcl.init");
            }
            /* "If the initializer is a parenthesized single expression, the
               type conversion expression is equivalent to the corresponding
               cast expression." */
            analysis_result r = expression(node[2], m);
            if (r != analysis_result::well_formed)
                return r;
            return explicit_conversion(n, to, m);
        }
        if (braced && node.size() == 2) {
            if (is_void)
                return not_supported(n, "expr.type.conv");
            initialized_value result;
            analysis_result r =
                list_initialization(node[1].index(), to, true, result);
            if (r != analysis_result::well_formed)
                return r;
            m = expression_meaning();
            m.type = to;
            m.evaluation = result.evaluation;
            m.value = result.value;
            m.node = result.node;
            return analysis_result::well_formed;
        }
        return not_supported(n, "expr.type.conv");
    }
    if (parenthesized)
        return call(n, m);
    if (node.size() == 4 &&
        node[1].is(preprocessing_op_or_punc::LEFT_BRACKET)) {
        /* [expr.sub]/1: "One of the expressions shall be a glvalue of type
           "array of T" or a prvalue of type "pointer to T" and the other
           shall be a prvalue of unscoped enumeration or integral type.  The
           result is of type "T". ... The expression E1[E2] is identical (by
           definition) to *((E1)+(E2)), except that in the case of an array
           operand, the result is an lvalue if that operand is an lvalue and
           an xvalue otherwise." */
        if (node[2].is(syntax_kind::braced_init_list) ||
            node[2].is(syntax_kind::initializer_list))
            return not_supported(n, "expr.sub");
        expression_meaning a, i;
        analysis_result r = expression(node[0], a);
        if (r == analysis_result::well_formed)
            r = expression(node[2], i);
        if (r != analysis_result::well_formed)
            return r;
        /* [over.sub]/1: "the operator function is selected by overload
           resolution ([over.match.oper])" */
        bool handled = false;
        r = binary_operator_function(n, node[1].token(),
                                     preprocessing_op_or_punc::LEFT_BRACKET,
                                     node[0], a, node[2], i, handled, m);
        if (r != analysis_result::well_formed || handled)
            return r;
        expression_meaning *base = &a, *index = &i;
        auto is_array = [&](const expression_meaning &x) {
            return type_table[x.type.unqualified()].kind == type_kind::array;
        };
        if (!is_array(a) && !is_pointer(a.type) &&
            (is_array(i) || is_pointer(i.type))) {
            base = &i;
            index = &a;
        }
        if (is_array(*base) && base->category != value_category::lvalue)
            return not_supported(n, "expr.sub");
        r = operand_value(n, *base);
        if (r == analysis_result::well_formed)
            r = operand_value(n, *index);
        if (r != analysis_result::well_formed)
            return r;
        /* [expr.sub]/1: "The other shall be a prvalue of unscoped
           enumeration or integral type" */
        promote_enumeration(*index);
        if (!is_pointer(base->type) || !is_integral(index->type)) {
            if ((fundamental_of(base->type) == fundamental_type::none &&
                 !is_pointer(base->type)) ||
                (fundamental_of(index->type) == fundamental_type::none &&
                 !is_pointer(index->type)))
                return not_supported(n, "expr.sub");
            return ill_formed(n,
                              "one of the expressions shall be a glvalue of "
                              "type array of T or a prvalue of type pointer "
                              "to T and the other shall be a prvalue of "
                              "unscoped enumeration or integral type",
                              "expr.sub");
        }
        qualified_type element = type_table[base->type.unqualified()].operand;
        if (!is_object_pointer(base->type) || !size_of_type(element))
            return not_supported(n, "expr.sub");
        constness evaluation =
            combined_constness(base->evaluation, index->evaluation);
        m = expression_meaning();
        m.type = element;
        m.category = value_category::lvalue;
        m.evaluation = evaluation;
        if (m.evaluation == constness::constant)
            m.evaluation = constness::unknown;
        m.node = expression_node(semantic_kind::subscript, m,
                                 node.first_token(), base->node, index->node);
        return analysis_result::well_formed;
    }
    return not_supported(n, "expr.post");
}

/*
 * [expr.call]/1: "A function call is a postfix expression followed by
 * parentheses containing a possibly empty, comma-separated list of
 * initializer-clauses which constitute the arguments to the function.  ...
 * For a call to a non-member function or to a static member function, the
 * postfix expression shall be either an lvalue that refers to a function
 * (in which case the function-to-pointer standard conversion is suppressed
 * on the postfix expression), or a prvalue of function pointer type."
 *
 * The function a name finds is one, not an overload set (analysis holds
 * the name of a function declared twice with different parameters), so
 * overload resolution ([over.match]) has one candidate: viable or not.
 */
analysis_result semantic_analysis::call(node_index n, expression_meaning &m)
{
    auto node = syntax(n);
    if (builtin_addressof_call(n))
        return builtin_addressof(n, m);
    owned_array<syntax_child> arguments;
    /* [temp.variadic]/5: the arguments, pack expansions instantiated: each
       analysed now, one per element of the packs its pattern names */
    owned_array<expression_meaning> expanded;
    if (node.size() == 4) {
        syntax_child c = node[2];
        if (syntax(c).is(syntax_kind::initializer_list)) {
            owned_array<syntax_child> written;
            owned_array<bool> expansion;
            if (!expansion_list(c.index(), written, expansion))
                return not_supported(n, "temp.variadic");
            bool any = false;
            for (bool x : expansion)
                any = any || x;
            for (size_t i = 0; i < written.size(); i++) {
                syntax_child a = written[i];
                if (!any) {
                    arguments.append(a);
                    continue;
                }
                if (syntax(a).is(syntax_kind::braced_init_list))
                    return not_supported(n, "temp.variadic");
                if (!expansion[i]) {
                    expression_meaning e;
                    analysis_result r = expression(a, e);
                    if (r != analysis_result::well_formed)
                        return r;
                    arguments.append(a);
                    expanded.append(e);
                    continue;
                }
                uint32_t count = 0;
                token_index end = first_token(a) + 1;
                if (!a.is_token())
                    end = syntax(a).end_token();
                if (!expansion_count(first_token(a), end, count))
                    return not_supported(n, "temp.variadic");
                for (uint32_t k = 0; k < count; k++) {
                    expression_meaning e;
                    analysis_result r = expansion_element_expression(a, k, e);
                    if (r != analysis_result::well_formed)
                        return r;
                    arguments.append(a);
                    expanded.append(e);
                }
            }
        } else {
            arguments.append(c);
        }
    } else if (node.size() != 3) {
        return not_supported(n, "expr.call");
    }
    expression_meaning f;
    analysis_result r = analysis_result::well_formed;
    {
        /* [over.match.funcs]/8: "In each case where a candidate is a
           function template, candidate function template specializations
           are generated using template argument deduction"; the name is a
           function template's alone */
        token_index name = 0;
        uint32_t ft = 0;
        bool qualified = false;
        owned_array<template_argument> specified;
        /* [class.mfct.non-static]/2: a member function template's name
           in a member function of its class (or a class derived from
           it), called on (*this) */
        if (node[0].is_token() &&
            tokens[node[0].token()].kind == token_kind::identifier) {
            lookup_result x = lookup_unqualified(
                tokens.identifier(node[0].token()), lookup_filter::any);
            if (x.unknown && !x.entity && !x.ambiguous &&
                member_function_template_of(x.unknown_entity)) {
                bool ambiguous = false;
                entity_index of = entity_table[x.unknown_entity].enclosing;
                if (!function_state.this_entity ||
                    (of != current_class() &&
                     !base_step(current_class(), of, ambiguous)) ||
                    ambiguous)
                    return not_supported(n, "temp.mem");
                expression_meaning object;
                r = this_object(node[0].token(), object);
                if (r != analysis_result::well_formed)
                    return r;
                owned_array<expression_meaning> early;
                for (const expression_meaning &e : expanded)
                    early.append(e);
                return member_template_call(n, x.unknown_entity, object,
                                            arguments, early, m);
            }
        }
        r = template_callee(node[0], name, ft, specified, qualified);
        if (r != analysis_result::well_formed)
            return r;
        if (ft) {
            owned_array<expression_meaning> early;
            for (const expression_meaning &e : expanded)
                early.append(e);
            r = template_call(n, name, ft, specified, qualified, arguments,
                              early, f);
            if (r != analysis_result::well_formed)
                return r;
            return call_with(n, f, f.type, 0, 0, arguments, early, m);
        }
    }
    /* the arguments analysed before the function is known, for
       argument-dependent lookup or overload resolution: each one's
       meaning, or none for a braced-init-list (which has no type) */
    owned_array<expression_meaning> early;
    /* the functions argument-dependent lookup found, with unqualified
       lookup's */
    owned_array<entity_index> found_by_adl;
    bool looked_up = false;
    if (node[0].is_token() &&
        tokens[node[0].token()].kind == token_kind::identifier) {
        /* [basic.lookup.argdep]/3: "Let X be the lookup set produced by
           unqualified lookup and let Y be the lookup set produced by argument
           dependent lookup (defined as follows).  If X contains (3.1) a
           declaration of a class member, or (3.2) a block-scope function
           declaration that is not a using-declaration, or (3.3) a declaration
           that is neither a function nor a function template then Y is empty.
           Otherwise Y is the set of declarations found in the namespaces
           associated with the argument types as described below.  The set of
           declarations found by the lookup of the name is the union of X and
           Y." */
        token_index t = node[0].token();
        uint32_t name = tokens.identifier(t);
        lookup_result x = lookup_unqualified(name, lookup_filter::any);
        bool dependent = !x.ambiguous && !x.unknown;
        if (x.entity) {
            const entity &fx = entity_table[x.entity];
            dependent = fx.kind == entity_kind::function &&
                        is_namespace(fx.enclosing) &&
                        (is_namespace(x.found_in) ||
                         entity_table.bound_by_using(x.found_in, name));
        }
        if (dependent && !expanded.empty()) {
            owned_array<qualified_type> types;
            for (const expression_meaning &e : expanded) {
                types.append(e.type);
                early.append(e);
            }
            owned_array<entity_index> set;
            if (x.entity)
                set.append(x.entity);
            if (!argument_dependent_lookup(name, types, set) || !x.entity)
                return not_supported(n, "basic.lookup.argdep");
            for (entity_index e : set)
                overload_set(e, found_by_adl);
        } else if (dependent) {
            owned_array<qualified_type> types;
            for (syntax_child a : arguments) {
                expression_meaning e;
                token_index name = 0;
                if (entity_index named = named_overload_set(a, name)) {
                    /* [basic.lookup.argdep]/2: "If an argument is the name
                       or address of an overload set, its associated
                       entities and namespaces are the union of those
                       associated with each of the members of the set";
                       the argument is analysed for its parameter
                       ([over.over], call_with) */
                    owned_array<entity_index> members;
                    overload_set(named, members);
                    for (entity_index f : members)
                        types.append(entity_table[f].type);
                } else if (!syntax(a).is(syntax_kind::braced_init_list)) {
                    r = expression(a, e);
                    if (r != analysis_result::well_formed)
                        return r;
                    types.append(e.type);
                }
                early.append(e);
            }
            owned_array<entity_index> set;
            if (x.entity)
                set.append(x.entity);
            if (!argument_dependent_lookup(name, types, set))
                return not_supported(n, "basic.lookup.argdep");
            /* the functions of the sets found: [over.call.func]/3's
               candidate functions */
            for (entity_index e : set)
                overload_set(e, found_by_adl);
            if (!x.entity && !set.empty()) {
                /* found by argument-dependent lookup alone: no claim is
                   made about the name, which names nothing elsewhere */
                overloaded_callee = t;
                r = named_entity(t, set[0], f);
                overloaded_callee = 0;
                if (r != analysis_result::well_formed)
                    return r;
                looked_up = true;
            }
        }
    }
    if (!expanded.empty() && early.empty())
        for (const expression_meaning &e : expanded)
            early.append(e);
    if (!looked_up) {
        overloaded_callee = callee_name(node[0]);
        member_template_name = 0;
        r = expression(node[0], f);
        overloaded_callee = 0;
    }
    if (entity_index held = member_template_name) {
        member_template_name = 0;
        if (r != analysis_result::well_formed)
            return r;
        expression_meaning object = member_template_object;
        return member_template_call(n, held, object, arguments, early, m);
    }
    if (r != analysis_result::well_formed)
        return r;
    if (type_table[f.type.unqualified()].kind == type_kind::class_type) {
        /* [over.call]/1: "where the postfix-expression is of class type,
           the operator function is selected by overload resolution
           ([over.call.object])" and the expression "interpreted as
           postfix-expression . operator () (expression-list_opt)" */
        if (!expanded.empty())
            return not_supported(n, "temp.variadic");
        owned_array<syntax_child> operand_syntax;
        owned_array<expression_meaning> operands;
        operand_syntax.append(node[0]);
        operands.append(f);
        for (syntax_child a : arguments) {
            if (syntax(a).is(syntax_kind::braced_init_list))
                return not_supported(n, "over.ics.list");
            expression_meaning e;
            r = expression(a, e);
            if (r != analysis_result::well_formed)
                return r;
            operand_syntax.append(a);
            operands.append(e);
        }
        bool handled = false;
        r = overloaded_operator(n, node[1].token(),
                                preprocessing_op_or_punc::LEFT_PARENTHESIS,
                                false, operand_syntax, operands, handled, m);
        if (r != analysis_result::well_formed || handled)
            return r;
        /* [over.call.object]/2: no surrogate call functions (no conversion
           functions are analysed) */
        return not_supported(n, "over.call.object");
    }
    qualified_type function_type = f.type;
    /* [expr.call]/2: "For a call to a non-static member function, the
       postfix expression shall be an implicit ([class.mfct.non-static],
       [class.static]) or explicit class member access whose id-expression
       is a function member name" */
    uint32_t object = 0;
    entity_index member = 0;
    if (f.node &&
        semantics[f.node].kind == semantic_kind::bound_member_function) {
        object = semantics[f.node].a;
        member = semantics[f.node].b;
    } else if (f.node &&
               semantics[f.node].kind == semantic_kind::member_pointer_access &&
               type_table[f.type.unqualified()].kind == type_kind::function) {
        /* [expr.mptr.oper]/6: "If the result of .* or ->* is a function,
           then that result can be used only as the operand for the
           function call operator ()": the call's object is the access's
           (lowering adjusts it and finds the function) */
    } else if (type_table[f.type.unqualified()].kind != type_kind::function ||
               f.category != value_category::lvalue) {
        r = operand_value(n, f);
        if (r != analysis_result::well_formed)
            return r;
        const struct type pointer = type_table[f.type.unqualified()];
        if (pointer.kind == type_kind::pointer &&
            type_table[pointer.operand.unqualified()].kind ==
                type_kind::function) {
            function_type = pointer.operand;
        } else {
            if (fundamental_of(f.type) == fundamental_type::none &&
                !is_pointer(f.type))
                return not_supported(n, "expr.call");
            return ill_formed(n,
                              "the postfix expression shall be either an "
                              "lvalue that refers to a function or a prvalue "
                              "of function pointer type",
                              "expr.call");
        }
    }
    /* [over.call.func]/3: the candidate functions of the name's overload
       set, one or more */
    entity_index named = member;
    if (!named && f.node && semantics[f.node].kind == semantic_kind::entity &&
        f.entity && entity_table[f.entity].kind == entity_kind::function)
        named = f.entity;
    owned_array<entity_index> set;
    if (named)
        overload_set(named, set);
    for (entity_index e : found_by_adl)
        overload_set(e, set);
    if (set.size() > 1) {
        r = resolve_call(n, arguments, early, set, object, f);
        if (r != analysis_result::well_formed)
            return r;
        function_type = f.type;
        if (member)
            member = semantics[f.node].b;
    }
    return call_with(n, f, function_type, object, member, arguments, early, m);
}

/*
 * A call whose name is a function template's ([temp.fct]): the arguments
 * analysed, the template arguments deduced from them
 * ([temp.deduct.call]), the specialization instantiated ([temp.inst]) and
 * named.  [basic.lookup.argdep]/3's lookup set Y may add functions of the
 * arguments' associated namespaces, which overload resolution would weigh
 * against the specialization: not analysed.
 */
analysis_result semantic_analysis::template_callee(
    syntax_child callee, token_index &name, uint32_t &function_template,
    owned_array<template_argument> &specified, bool &qualified)
{
    name = 0;
    function_template = 0;
    qualified = false;
    if (callee.is_token()) {
        if (tokens[callee.index()].kind != token_kind::identifier)
            return analysis_result::well_formed;
        name = callee.index();
        lookup_result x =
            lookup_unqualified(tokens.identifier(name), lookup_filter::any);
        function_template = function_template_of(x.unknown_entity);
        return analysis_result::well_formed;
    }
    /* (the uses a callee that is no template's records are recorded
       again by its analysis as an expression) */
    size_t uses_before = name_uses.size();
    analysis_result r = template_callee_parts(callee, name, function_template,
                                              specified, qualified);
    if (!function_template)
        name_uses.resize(uses_before);
    return r;
}

analysis_result semantic_analysis::template_callee_parts(
    syntax_child callee, token_index &name, uint32_t &function_template,
    owned_array<template_argument> &specified, bool &qualified)
{
    syntax_child id = callee;
    nominated_scope scope;
    if (syntax(callee).is(syntax_kind::qualified_id)) {
        /* [namespace.qual]: a function template of a namespace */
        auto q = syntax(callee);
        if (q.size() != 2 || !q[0].is(syntax_kind::nested_name_specifier))
            return analysis_result::well_formed;
        id = q[1];
        analysis_result r = nested_name_specifier(q[0].index(), scope);
        if (r != analysis_result::well_formed)
            return r;
        if (!is_namespace(scope.entity))
            return analysis_result::well_formed;
        qualified = true;
    }
    owned_array<syntax_child> written;
    if (id.is_token()) {
        if (!qualified || tokens[id.index()].kind != token_kind::identifier)
            return analysis_result::well_formed;
        name = id.index();
    } else if (syntax(id).is(syntax_kind::simple_template_id)) {
        auto t = syntax(id);
        if (t.size() < 3 || !t[0].is_identifier())
            return analysis_result::well_formed;
        name = t[0].token();
        for (uint32_t i = 2; i + 1 < t.size(); i++) {
            if (t[i].is(syntax_kind::template_argument_list)) {
                for (uint32_t k = 0; k < t[i].size(); k++) {
                    syntax_child a = t[i][k];
                    if (!syntax(a).is(preprocessing_op_or_punc::COMMA))
                        written.append(a);
                }
            } else {
                written.append(t[i]);
            }
        }
    } else {
        return analysis_result::well_formed;
    }
    lookup_result x;
    if (qualified)
        x = lookup_qualified(scope.entity, tokens.identifier(name),
                             lookup_filter::any);
    else
        x = lookup_unqualified(tokens.identifier(name), lookup_filter::any);
    function_template = function_template_of(x.unknown_entity);
    if (!function_template) {
        /* a template-id of no function template analysis knows: what it
           was before */
        if (!written.empty() || !qualified)
            return not_supported(id.index(), "temp.names");
        return analysis_result::well_formed;
    }
    if (!written.empty()) {
        /* explicit template arguments for each of several templates
           ([temp.over]/1's "checking of any explicit template
           arguments"): not analysed */
        owned_array<uint32_t> group;
        function_template_overloads(
            function_templates[function_template - 1].name, group);
        if (group.size() > 1) {
            overloaded_written.clear();
            for (syntax_child a : written)
                overloaded_written.append(a);
            overloaded_written_at = id.index();
            return analysis_result::well_formed;
        }
    }
    /* [temp.arg.explicit]/6: "The template argument list shall not
       specify more template-arguments than there are corresponding
       template-parameters" (clang: no candidate) */
    for (syntax_child a : written) {
        uint32_t position = explicit_argument_position(
            function_template, static_cast<uint32_t>(specified.size()));
        if (position == UINT32_MAX)
            return not_supported(id.index(), "temp.arg.explicit");
        template_argument x;
        analysis_result r = template_argument_for(
            id.index(), function_template, position, a, specified.data(), x);
        if (r != analysis_result::well_formed)
            return r;
        specified.append(x);
    }
    return analysis_result::well_formed;
}

semantic_analysis::substitution_mark
semantic_analysis::mark_substitution() const
{
    substitution_mark m;
    m.messages = diagnostics.messages.size();
    m.errors = diagnostics.error_count;
    m.warnings = diagnostics.warning_count;
    m.fatals = diagnostics.fatal_count;
    m.expressions = expressions.size();
    m.uses = name_uses.size();
    m.failures = definition_failures;
    return m;
}

bool semantic_analysis::undo_substitution(const substitution_mark &m)
{
    /* (a definition instantiated on the way failed, or a fatal error:
       not the substitution's own failure) */
    if (m.failures != definition_failures ||
        diagnostics.fatal_count != m.fatals)
        return false;
    diagnostics.messages.resize(m.messages);
    diagnostics.error_count = m.errors;
    diagnostics.warning_count = m.warnings;
    expressions.resize(m.expressions);
    name_uses.resize(m.uses);
    return true;
}

/* [temp.arg.explicit]/9: the template parameter explicit template
   argument i of function template ft is for, those from a trailing
   pack's position on that pack's; UINT32_MAX for one too many (/6) */
uint32_t semantic_analysis::explicit_argument_position(uint32_t ft,
                                                       uint32_t i) const
{
    const function_template &f = function_templates[ft - 1];
    if (i >= f.parameter_count && f.parameter_count &&
        template_parameter_is_pack(f.first_parameter + f.parameter_count - 1))
        return f.parameter_count - 1;
    if (i >= f.parameter_count)
        return UINT32_MAX;
    return i;
}

/*
 * [temp.over]/1: "For each function template, if the argument deduction
 * and checking succeeds, the template-arguments (deduced and/or explicit)
 * are used to synthesize the declaration of a single function template
 * specialization which is added to the candidate functions set to be used
 * in overload resolution."  Each candidate's declaration is instantiated
 * ([temp.inst]/5: not its definition, which only the one chosen needs);
 * with first_arguments, where each one's arguments start in all.  A call
 * of a template's name and an operator expression ([over.match.oper]/3)
 * both form them here.
 */
analysis_result semantic_analysis::template_candidates(
    node_index n, const owned_array<uint32_t> &group,
    const expression_meaning *meanings, uint32_t count,
    owned_array<overload_candidate> &candidates,
    owned_array<uint32_t> *first_arguments, owned_array<template_argument> *all,
    const owned_array<syntax_child> *written, node_index written_at,
    const owned_array<template_argument> *specified)
{
    for (uint32_t g : group) {
        /* the explicit template arguments ([temp.arg.explicit]), if given */
        owned_array<template_argument> d;
        if (specified)
            for (const template_argument &a : *specified)
                d.append(a);
        bool failed = false;
        /* [temp.deduct]/8: an invalid type or expression in the
           immediate context of the declaration (or of a default
           template-argument) substituted is no candidate, and no
           diagnostic: what substitution issued is taken back */
        substitution_mark mark = mark_substitution();
        substituting++;
        analysis_result r = analysis_result::well_formed;
        /* [temp.arg.explicit]/2-6: the explicit template-arguments,
           checked against this template's parameters; more than it has,
           or one of another kind, is no candidate ([temp.deduct]/2) */
        if (written) {
            for (size_t i = 0; !failed && i < written->size(); i++) {
                uint32_t position =
                    explicit_argument_position(g, static_cast<uint32_t>(i));
                if (position == UINT32_MAX) {
                    failed = true;
                    break;
                }
                entity_index p = template_parameter_entities
                    [function_templates[g - 1].first_parameter + position];
                bool type_parameter =
                    entity_table[p].kind == entity_kind::typedef_name;
                if (entity_table[p].kind != entity_kind::template_entity &&
                    type_parameter !=
                        syntax((*written)[i]).is(syntax_kind::type_id)) {
                    failed = true;
                    break;
                }
                template_argument x;
                r = template_argument_for(written_at, g, position,
                                          (*written)[i], d.data(), x);
                if (r != analysis_result::well_formed)
                    break;
                d.append(x);
            }
        }
        if (r == analysis_result::well_formed && !failed)
            r = deduce_from_call(n, g, meanings, count, d, &failed);
        entity_index e = 0;
        const struct function_template ft = function_templates[g - 1];
        if (r == analysis_result::well_formed && !failed) {
            unevaluated++;
            r = instantiate(n, ft.entity, d.data(),
                            static_cast<uint32_t>(d.size()), e);
            unevaluated--;
        }
        substituting--;
        if (r == analysis_result::ill_formed && undo_substitution(mark))
            continue;
        if (r != analysis_result::well_formed)
            return r;
        if (failed)
            continue;
        overload_candidate c;
        c.function = e;
        c.type = entity_table[e].type;
        c.template_index = g;
        candidates.append(c);
        if (first_arguments) {
            first_arguments->append(static_cast<uint32_t>(all->size()));
            for (const template_argument &x : d)
                all->append(x);
        }
    }
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::template_call(
    node_index n, token_index name, uint32_t function_template,
    owned_array<template_argument> &specified, bool qualified,
    const owned_array<syntax_child> &arguments,
    owned_array<expression_meaning> &early, expression_meaning &f)
{
    owned_array<syntax_child> written;
    node_index written_at = overloaded_written_at;
    for (syntax_child a : overloaded_written)
        written.append(a);
    overloaded_written.clear();
    overloaded_written_at = 0;
    owned_array<qualified_type> types;
    /* (arguments analysed already: a pack expansion's, call) */
    bool given = !early.empty();
    for (const expression_meaning &e : early) {
        if (e.entity && entity_table[e.entity].kind == entity_kind::function)
            return not_supported(n, "temp.deduct.call");
        types.append(e.type);
    }
    for (syntax_child a : arguments) {
        if (given)
            break;
        expression_meaning e;
        if (syntax(a).is(syntax_kind::braced_init_list))
            return not_supported(n, "temp.deduct.call");
        /* [temp.deduct.call]/6: "When P is a function type, function
           pointer type, or pointer-to-member-function type: If the argument
           is an overload set containing one or more function templates, the
           parameter is treated as a non-deduced context.  If the argument
           is an overload set (not containing function templates), trial
           argument deduction is attempted using each of the members of the
           set": not analysed */
        token_index set_at = 0;
        if (named_overload_set(a, set_at))
            return not_supported(n, "temp.deduct.call");
        size_t uses_before = name_uses.size();
        analysis_result r = expression(a, e);
        if (r != analysis_result::well_formed)
            return r;
        /* (a function's name whose other declarations analysis did not
           follow may be such a set too) */
        if (e.entity && entity_table[e.entity].kind == entity_kind::function) {
            name_uses.resize(uses_before);
            return not_supported(n, "temp.deduct.call");
        }
        types.append(e.type);
        early.append(e);
    }
    /* [basic.lookup.argdep]/1: for an unqualified name only */
    owned_array<entity_index> others;
    if (!qualified && (!argument_dependent_lookup(
                           tokens.identifier(name), types, others,
                           function_templates[function_template - 1].name) ||
                       !others.empty()))
        return not_supported(n, "basic.lookup.argdep");
    owned_array<template_argument> &deduced = specified;
    const expression_meaning *meanings = nullptr;
    if (!early.empty())
        meanings = &early[0];
    owned_array<uint32_t> group;
    function_template_overloads(function_templates[function_template - 1].name,
                                group);
    /* the name's non-template functions, candidates unless the name is a
       template-id's ([temp.arg.explicit]: only templates take template
       arguments) */
    owned_array<entity_index> functions;
    bool template_id =
        tokens[name + 1].kind == token_kind::operator_or_punctuator &&
        preprocessing_op_or_punc_primary(tokens[name + 1].op_or_punc) ==
            preprocessing_op_or_punc::LESS;
    for (const template_name_function &x : template_name_functions)
        if (x.name == function_templates[function_template - 1].name &&
            !template_id)
            functions.append(x.function);
    entity_index specialization = 0;
    if (group.size() > 1 || !functions.empty()) {
        /* [temp.over]/1: template_candidates */
        owned_array<overload_candidate> candidates;
        owned_array<uint32_t> first_arguments;
        owned_array<template_argument> all;
        for (entity_index e : functions) {
            overload_candidate c;
            c.function = e;
            c.type = entity_table[e].type;
            candidates.append(c);
            first_arguments.append(0);
        }
        const owned_array<syntax_child> *given_written = nullptr;
        if (written_at)
            given_written = &written;
        analysis_result added = template_candidates(
            n, group, meanings, static_cast<uint32_t>(early.size()), candidates,
            &first_arguments, &all, given_written, written_at);
        if (added != analysis_result::well_formed)
            return added;
        if (candidates.empty())
            /* (deduction failed for each: no viable function, which
               clang reports with each template's reason) */
            return not_supported(n, "temp.over");
        size_t chosen = 0;
        analysis_result r =
            best_viable_function(n, candidates, nullptr, early, chosen);
        if (r != analysis_result::well_formed)
            return r;
        if (!candidates[chosen].template_index) {
            /* a non-template function: [dcl.fct.def.delete]/2 "If a
               function is overloaded, it is referenced only if the
               function is selected by overload resolution." */
            entity_index c = candidates[chosen].function;
            r = deleted_use(name, c);
            if (r != analysis_result::well_formed)
                return r;
            overloaded_callee = name;
            r = named_entity(name, c, f);
            overloaded_callee = 0;
            return r;
        }
        const struct function_template ft =
            function_templates[candidates[chosen].template_index - 1];
        r = instantiate(n, ft.entity, &all[first_arguments[chosen]],
                        ft.parameter_count, specialization);
        if (r != analysis_result::well_formed)
            return r;
    } else {
        analysis_result r =
            deduce_from_call(n, function_template, meanings,
                             static_cast<uint32_t>(early.size()), deduced);
        if (r != analysis_result::well_formed)
            return r;
        r = instantiate(n, function_templates[function_template - 1].entity,
                        &deduced[0], static_cast<uint32_t>(deduced.size()),
                        specialization);
        if (r != analysis_result::well_formed)
            return r;
    }
    analysis_result r = analysis_result::well_formed;
    /* no claim is made about the name, whose declaration clang finds is
       the template's */
    overloaded_callee = name;
    r = named_entity(name, specialization, f);
    overloaded_callee = 0;
    return r;
}

/*
 * [expr.call]/7: "When a function is called, each parameter is initialized
 * with its corresponding argument."  The call of f, its function chosen.
 */
analysis_result semantic_analysis::call_function(
    node_index n, token_index at, entity_index f,
    const owned_array<expression_meaning> &arguments, expression_meaning &m)
{
    analysis_result r = deleted_use(at, f);
    if (r != analysis_result::well_formed)
        return r;
    const entity &x = entity_table[f];
    expression_meaning callee;
    callee.type = x.type;
    callee.category = value_category::lvalue;
    callee.evaluation = constness::constant;
    callee.entity = f;
    callee.node = expression_node(semantic_kind::entity, callee, at, f);
    owned_array<syntax_child> syntax;
    for (size_t i = 0; i < arguments.size(); i++)
        syntax.append(syntax_child{});
    return call_with(n, callee, x.type, 0, 0, syntax, arguments, m);
}

analysis_result semantic_analysis::call_with(
    node_index n, expression_meaning &f, qualified_type function_type,
    uint32_t object, entity_index member,
    const owned_array<syntax_child> &arguments,
    const owned_array<expression_meaning> &early, expression_meaning &m)
{
    analysis_result r = analysis_result::well_formed;
    auto node = syntax(n);
    const struct type function = type_table[function_type.unqualified()];
    size_t count = function.parameter_count;
    owned_array<uint32_t> argument_nodes;
    /* [over.match.viable]/2: "First, to be a viable function, a candidate
       function shall have enough parameters to agree in number with the
       arguments in the list."  [over.match]/3: "If a best viable
       function exists and is unique, overload resolution succeeds ...
       Otherwise overload resolution fails and the invocation is
       ill-formed." */
    entity_index called = member;
    if (!called && f.entity &&
        entity_table[f.entity].kind == entity_kind::function)
        called = f.entity;
    size_t defaults = 0;
    if (called)
        defaults = entity_table[called].default_arguments;
    if (called && entity_table[called].unknown_defaults &&
        arguments.size() < count)
        return not_supported(n, "dcl.fct.default");
    if (arguments.size() + defaults < count ||
        (arguments.size() > count && !function.variadic))
        return ill_formed(n,
                          "to be a viable function, a candidate function "
                          "shall have enough parameters to agree in number "
                          "with the arguments in the list",
                          "over.match.viable");
    for (size_t i = 0; i < arguments.size(); i++) {
        syntax_child a = arguments[i];
        initialized_value value;
        if (i < count) {
            /* [expr.call]/7: "When a function is called, each parameter is
               initialized with its corresponding argument." ([dcl.init]/15:
               copy-initialization) */
            qualified_type parameter =
                type_table.parameters(function_type.unqualified())[i];
            if (syntax(a).is(syntax_kind::braced_init_list)) {
                r = list_initialization(a.index(), parameter, false, value);
            } else {
                expression_meaning e;
                if (i < early.size() && early[i].node)
                    e = early[i];
                else
                    /* [over.over]/1: "a parameter of a function" */
                    r = target_expression(a, parameter, e);
                /* an argument that is one token is reported at the call */
                node_index at = n;
                if (!a.is_token() && a.index())
                    at = a.index();
                if (r == analysis_result::well_formed)
                    r = initialize_with(at, e, parameter, false, false, value);
            }
            if (r != analysis_result::well_formed)
                return r;
            argument_nodes.append(value.node);
            continue;
        }
        /* [expr.call]/12: "The lvalue-to-rvalue, array-to-pointer, and
           function-to-pointer standard conversions are performed on the
           argument expression. ... After these conversions, if the argument
           does not have arithmetic, enumeration, pointer, pointer-to-member,
           or class type, the program is ill-formed." */
        if (syntax(a).is(syntax_kind::braced_init_list))
            return not_supported(n, "expr.call");
        expression_meaning e;
        if (i < early.size() && early[i].node)
            e = early[i];
        else
            r = expression(a, e);
        if (r == analysis_result::well_formed)
            r = operand_value(node_of(a), e);
        if (r != analysis_result::well_formed)
            return r;
        /* [ext:gnu_library_builtins] a type-generic builtin of the C
           front end (__builtin_isfinite, ...) takes its argument as it
           is, as clang's custom type checking does: no promotion */
        if (called && (entity_table[called].gnu_flags & gnu_compiler_builtin)) {
            argument_nodes.append(e.node);
            continue;
        }
        promote_enumeration(e);
        fundamental_type passed = fundamental_of(e.type);
        if (passed == fundamental_type::void_)
            return ill_formed(n,
                              "an argument passed for an ellipsis shall have "
                              "arithmetic, enumeration, pointer, "
                              "pointer-to-member, or class type",
                              "expr.call");
        if (passed == fundamental_type::none && !is_pointer(e.type))
            return not_supported(n, "expr.call");
        if (passed == fundamental_type::std_nullptr_t) {
            /* "An argument that has type cv std::nullptr_t is converted to
               type void*" */
            qualified_type v = qualified_type::make(
                type_table.fundamental(fundamental_type::void_), cv_none);
            e.type = qualified_type::make(type_table.pointer_to(v), cv_none);
            converted(e, conversion_kind::null_pointer);
        } else if (passed == fundamental_type::float_) {
            /* "If the argument has integral or enumeration type that is
               subject to the integral promotions, or a floating-point type
               that is subject to the floating-point promotion, the value of
               the argument is converted to the promoted type before the
               call." ([conv.fpprom]: float to double) */
            convert_arithmetic(e, fundamental_type::double_);
        } else {
            promote(e);
        }
        argument_nodes.append(e.node);
    }
    /* [expr.call]/7: "If there is no corresponding argument, the default
       argument for the parameter is used." ([dcl.fct.default]/9: evaluated
       at each call) */
    for (size_t i = arguments.size(); i < count; i++)
        argument_nodes.append(
            default_argument_nodes[entity_table[called].first_default + i]);
    /* [expr.call]/14: "A function call is an lvalue if the result type is
       an lvalue reference type or an rvalue reference to function type, an
       xvalue if the result type is an rvalue reference to object type, and
       a prvalue otherwise." */
    m = expression_meaning();
    call_result(function.operand, m);
    /* [expr.const]/5.2: "an invocation of a non-constexpr function"; a
       constexpr function's invocation is the evaluator's
       (evaluate_constant), and a pointer or reference may be to a function
       analysis has no declaration of */
    m.evaluation = constness::unknown;
    /* (a closure's function call operator may be constexpr without the
       specifier: [expr.prim.lambda.closure]/4, evaluate_invocation) */
    if (called && !entity_table[called].constexpr_ &&
        !(entity_table[called].enclosing &&
          entity_table[entity_table[called].enclosing].lambda))
        m.evaluation = constness::not_constant;
    /* a constexpr member function's invocation is the evaluator's */
    if (called && entity_table[called].constexpr_) {
        m.evaluation = constness::unknown;
        constexpr_calls++;
    }
    uint32_t callee = f.node;
    if (member) {
        /* the member function, as an lvalue of its type; the object's
           address is its implicit object argument ([over.match.funcs]/4) */
        expression_meaning named;
        named.type = function_type;
        named.category = value_category::lvalue;
        named.evaluation = constness::constant;
        named.entity = member;
        callee = expression_node(semantic_kind::entity, named,
                                 node.first_token(), member);
    }
    /* [class.virtual]/16: "Explicit qualification with the scope operator
       ([expr.prim.id.qual]) suppresses the virtual call mechanism" */
    bool dispatched = false;
    if (member && entity_table[member].virtual_) {
        if (!node.size())
            return not_supported(n, "class.virtual");
        syntax_child name = syntax(n)[0];
        if (!name.is_token() &&
            syntax(name).is(syntax_kind::postfix_expression) &&
            syntax(name).size() == 3 &&
            (syntax(name)[1].is(preprocessing_op_or_punc::DOT) ||
             syntax(name)[1].is(preprocessing_op_or_punc::ARROW)))
            name = syntax(name)[2];
        if (name.is_token() &&
            tokens[name.index()].kind == token_kind::identifier)
            dispatched = true;
        else if (!syntax(name).is(syntax_kind::qualified_id))
            return not_supported(n, "class.virtual");
    }
    m.node = expression_node(semantic_kind::call, m, node.first_token(), callee,
                             object);
    if (dispatched)
        semantics[m.node].c = 1;
    semantics.set_list(m.node, argument_nodes.data(), argument_nodes.size());
    if (called && entity_table[called].immediate)
        return immediate_invocation(n, m);
    return analysis_result::well_formed;
}

/*
 * [expr.const]/13: "An expression or conversion is in an immediate
 * function context if it is potentially evaluated and its innermost
 * non-block scope is a function parameter scope of an immediate function.
 * An expression or conversion is an immediate invocation if it is a
 * potentially-evaluated explicit or implicit invocation of an immediate
 * function and is not in an immediate function context.  An immediate
 * invocation shall be a constant expression."  Its value replaces the
 * call (lowering emits no call, so no definition of the function).
 */
analysis_result semantic_analysis::immediate_invocation(node_index n,
                                                        expression_meaning &m)
{
    if (unevaluated || (function_state.function &&
                        entity_table[function_state.function].immediate))
        return analysis_result::well_formed;
    constant_value v;
    bool constant = false;
    analysis_result r = evaluate_constant(n, m.node, v, constant);
    if (r != analysis_result::well_formed)
        return r;
    if (!constant)
        return ill_formed(n,
                          "an immediate invocation shall be a constant "
                          "expression",
                          "expr.const");
    /* a value of class type is not one an expression_meaning keeps */
    if (m.category != value_category::prvalue ||
        evaluated_type(m.type) == fundamental_type::none)
        return not_supported(n, "expr.const");
    evaluated_meaning(m, v);
    semantics[m.node].evaluation = m.evaluation;
    semantics[m.node].value = m.value;
    return analysis_result::well_formed;
}

/* [expr.call]/14: the type and value category of a call of a function
   returning result */
void semantic_analysis::call_result(qualified_type result,
                                    expression_meaning &m) const
{
    const struct type record = type_table[result.unqualified()];
    m.type = result;
    m.category = value_category::prvalue;
    if (record.kind == type_kind::lvalue_reference ||
        record.kind == type_kind::rvalue_reference) {
        m.type = record.operand;
        m.category = value_category::lvalue;
        if (record.kind == type_kind::rvalue_reference &&
            type_table[record.operand.unqualified()].kind !=
                type_kind::function)
            m.category = value_category::xvalue;
    }
}

} // namespace cplusplus
