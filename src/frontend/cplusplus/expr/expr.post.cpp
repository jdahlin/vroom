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
 * [expr.static.cast]/1: "The result of the expression static_cast<T>(v)
 * is the result of converting the expression v to type T."  What analysis
 * has rules for: T cv void (/6) and the conversions between arithmetic and
 * enumeration types (/4 the implicit ones, /9, /10, [conv.fpint],
 * [conv.integral]); the rest (references, pointers, classes) not yet.
 */
analysis_result semantic_analysis::static_cast_expression(node_index n,
                                                          expression_meaning &m)
{
    const syntax_child *children = tree.children(n);
    qualified_type to;
    analysis_result r = type_id(children[2].index(), to);
    if (r == analysis_result::well_formed)
        r = expression(children[5], m);
    if (r != analysis_result::well_formed)
        return r;
    const struct type target = type_table[to.unqualified()];
    bool to_void = target.kind == type_kind::fundamental &&
                   target.fundamental == fundamental_type::void_;
    bool to_value = is_arithmetic(to) || enumeration_of(to);
    bool from_value = is_arithmetic(m.type) || enumeration_of(m.type);
    if (to_void || (to_value && from_value))
        return explicit_conversion(n, to, m);
    if ((target.kind == type_kind::rvalue_reference ||
         target.kind == type_kind::lvalue_reference) &&
        m.category != value_category::prvalue &&
        target.operand.unqualified() == m.type.unqualified() &&
        (m.type.cv() & ~target.operand.cv()) == 0) {
        /* [expr.static.cast]/3: "A glvalue of type cv1 T1 can be cast to
           type rvalue reference to cv2 T2 if cv2 T2 is reference-compatible
           with cv1 T1 ([dcl.init.ref]).  If the value is not a bit-field,
           the result refers to the object or the specified base class
           subobject thereof"; /1: "If T is an lvalue reference type ... the
           result is an lvalue; if T is an rvalue reference to object type,
           the result is an xvalue".  The same type only, not a bit-field
           (a base class is not analysed) */
        if (m.bit_field_width)
            return not_supported(n, "expr.static.cast");
        m.type = target.operand;
        m.category = value_category::xvalue;
        if (target.kind == type_kind::lvalue_reference)
            m.category = value_category::lvalue;
        return analysis_result::well_formed;
    }
    return not_supported(n, "expr.static.cast");
}

analysis_result semantic_analysis::postfix(node_index n, expression_meaning &m)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (node.child_count == 2 && children[1].is_token()) {
        preprocessing_op_or_punc op = preprocessing_op_or_punc_primary(
            tokens[children[1].index()].op_or_punc);
        if (op == preprocessing_op_or_punc::PLUS_PLUS ||
            op == preprocessing_op_or_punc::MINUS_MINUS)
            return increment(n, children[0],
                             op == preprocessing_op_or_punc::MINUS_MINUS, true,
                             m);
        return not_supported(n, "expr.post");
    }
    if (node.child_count < 2)
        return not_supported(n, "expr.post");
    if (node.child_count == 7 && children[0].is_token() &&
        p.keyword_of(tokens[children[0].index()]) ==
            standard_keyword::static_cast_ &&
        syntax_is(tree, children[2], syntax_kind::type_id))
        return static_cast_expression(n, m);
    if (node.child_count == 3 &&
        (syntax_is_punctuator(tokens, children[1],
                              preprocessing_op_or_punc::DOT) ||
         syntax_is_punctuator(tokens, children[1],
                              preprocessing_op_or_punc::ARROW)))
        return member_reference(n, m);
    /* [expr.type.conv]/1: "A simple-type-specifier or typename-specifier
       followed by a parenthesized optional expression-list or by a
       braced-init-list (the initializer) constructs a value of the
       specified type given the initializer." */
    bool parenthesized = syntax_is_punctuator(
        tokens, children[1], preprocessing_op_or_punc::LEFT_PARENTHESIS);
    bool braced = syntax_is(tree, children[1], syntax_kind::braced_init_list);
    type_index named = 0;
    if ((parenthesized || braced) && children[0].is_token()) {
        const token &t = tokens[children[0].index()];
        if (t.kind == token_kind::identifier) {
            entity_index e = lookup(tokens.identifier(children[0].index()));
            if (e && entity_table[e].kind == entity_kind::class_type)
                return class_conversion(n, e, m);
            if (e && (entity_table[e].kind == entity_kind::typedef_name ||
                      entity_table[e].kind == entity_kind::enumeration))
                named = entity_table[e].type.unqualified();
            if (e && entity_table[e].kind == entity_kind::typedef_name &&
                entity_table[e].type.cv())
                return not_supported(n, "expr.type.conv");
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
    if (named) {
        qualified_type to = qualified_type::make(named, cv_none);
        const struct type record = type_table[named];
        if (record.kind != type_kind::fundamental &&
            record.kind != type_kind::pointer)
            return not_supported(n, "expr.type.conv");
        bool is_void = record.kind == type_kind::fundamental &&
                       record.fundamental == fundamental_type::void_;
        if (parenthesized && node.child_count == 3) {
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
                expression_node(semantic_kind::constant, m, node.first_token);
            return analysis_result::well_formed;
        }
        if (parenthesized && node.child_count == 4) {
            if (syntax_is(tree, children[2], syntax_kind::initializer_list)) {
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
            analysis_result r = expression(children[2], m);
            if (r != analysis_result::well_formed)
                return r;
            return explicit_conversion(n, to, m);
        }
        if (braced && node.child_count == 2) {
            if (is_void)
                return not_supported(n, "expr.type.conv");
            initialized_value result;
            analysis_result r =
                list_initialization(children[1].index(), to, true, result);
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
    if (node.child_count == 4 &&
        syntax_is_punctuator(tokens, children[1],
                             preprocessing_op_or_punc::LEFT_BRACKET)) {
        /* [expr.sub]/1: "One of the expressions shall be a glvalue of type
           "array of T" or a prvalue of type "pointer to T" and the other
           shall be a prvalue of unscoped enumeration or integral type.  The
           result is of type "T". ... The expression E1[E2] is identical (by
           definition) to *((E1)+(E2)), except that in the case of an array
           operand, the result is an lvalue if that operand is an lvalue and
           an xvalue otherwise." */
        if (syntax_is(tree, children[2], syntax_kind::braced_init_list) ||
            syntax_is(tree, children[2], syntax_kind::initializer_list))
            return not_supported(n, "expr.sub");
        expression_meaning a, i;
        analysis_result r = expression(children[0], a);
        if (r == analysis_result::well_formed)
            r = expression(children[2], i);
        if (r != analysis_result::well_formed)
            return r;
        /* [over.sub]/1: "the operator function is selected by overload
           resolution ([over.match.oper])" */
        bool handled = false;
        r = binary_operator_function(
            n, children[1].index(), preprocessing_op_or_punc::LEFT_BRACKET,
            children[0], a, children[2], i, handled, m);
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
        m.node = expression_node(semantic_kind::subscript, m, node.first_token,
                                 base->node, index->node);
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
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    owned_array<syntax_child> arguments;
    if (node.child_count == 4) {
        syntax_child c = children[2];
        if (syntax_is(tree, c, syntax_kind::initializer_list)) {
            for (uint32_t i = 0; i < tree[c.index()].child_count; i++) {
                syntax_child a = tree.children(c.index())[i];
                if (syntax_is_punctuator(tokens, a,
                                         preprocessing_op_or_punc::COMMA))
                    continue;
                if (syntax_is_punctuator(tokens, a,
                                         preprocessing_op_or_punc::ELLIPSIS))
                    return not_supported(n, "temp.variadic");
                arguments.append(a);
            }
        } else {
            arguments.append(c);
        }
    } else if (node.child_count != 3) {
        return not_supported(n, "expr.call");
    }
    expression_meaning f;
    analysis_result r = analysis_result::well_formed;
    /* the arguments analysed before the function is known, for
       argument-dependent lookup or overload resolution: each one's
       meaning, or none for a braced-init-list (which has no type) */
    owned_array<expression_meaning> early;
    /* the functions argument-dependent lookup found, with unqualified
       lookup's */
    owned_array<entity_index> found_by_adl;
    bool looked_up = false;
    if (children[0].is_token() &&
        tokens[children[0].index()].kind == token_kind::identifier) {
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
        token_index t = children[0].index();
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
        if (dependent) {
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
                } else if (!syntax_is(tree, a, syntax_kind::braced_init_list)) {
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
    if (!looked_up) {
        overloaded_callee = callee_name(children[0]);
        r = expression(children[0], f);
        overloaded_callee = 0;
    }
    if (r != analysis_result::well_formed)
        return r;
    if (type_table[f.type.unqualified()].kind == type_kind::class_type) {
        /* [over.call]/1: "where the postfix-expression is of class type,
           the operator function is selected by overload resolution
           ([over.call.object])" and the expression "interpreted as
           postfix-expression . operator () (expression-list_opt)" */
        owned_array<syntax_child> syntax;
        owned_array<expression_meaning> operands;
        syntax.append(children[0]);
        operands.append(f);
        for (syntax_child a : arguments) {
            if (syntax_is(tree, a, syntax_kind::braced_init_list))
                return not_supported(n, "over.ics.list");
            expression_meaning e;
            r = expression(a, e);
            if (r != analysis_result::well_formed)
                return r;
            syntax.append(a);
            operands.append(e);
        }
        bool handled = false;
        r = overloaded_operator(n, children[1].index(),
                                preprocessing_op_or_punc::LEFT_PARENTHESIS,
                                false, syntax, operands, handled, m);
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
    const syntax_node &node = tree[n];
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
            if (syntax_is(tree, a, syntax_kind::braced_init_list)) {
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
        if (syntax_is(tree, a, syntax_kind::braced_init_list))
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
    qualified_type result = function.operand;
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
    /* [expr.const]/5.2: "an invocation of a non-constexpr function": no
       function analysis has a declaration of is constexpr, but a pointer
       or reference may be to one it has none of */
    m.evaluation = constness::unknown;
    if (member ||
        (f.entity && entity_table[f.entity].kind == entity_kind::function))
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
        callee = expression_node(semantic_kind::entity, named, node.first_token,
                                 member);
    }
    m.node = expression_node(semantic_kind::call, m, node.first_token, callee,
                             object);
    semantics[m.node].first =
        semantics.list(argument_nodes.data(), argument_nodes.size());
    semantics[m.node].count = static_cast<uint32_t>(argument_nodes.size());
    return analysis_result::well_formed;
}

} // namespace cplusplus
