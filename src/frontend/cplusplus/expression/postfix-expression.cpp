/*
 * postfix-expression.cpp: [expr.post] Postfix expressions, with the subclauses
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
#include "frontend/cplusplus/builtin-registry.h"
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
            if (classify_name(0, &length, false) == name_class::type &&
                !at(preprocessing_op_or_punc::COLON_COLON, length)) {
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
 * (cast-expression.cpp) with that cast alone.  dynamic_cast needs the run-time
 * type information, not analysed.
 */
analysis_result semantic_analysis::named_cast(PostfixOperationNamedCast cast,
                                              expression_meaning &m)
{
    SyntaxRef n = cast.node;
    standard_keyword keyword = cast.keyword().keyword();
    uint8_t kinds = cast_static;
    if (keyword == standard_keyword::const_cast_)
        kinds = cast_const;
    else if (keyword == standard_keyword::reinterpret_cast_)
        kinds = cast_reinterpret;
    else if (keyword != standard_keyword::static_cast_)
        return not_supported(n, "expr.dynamic.cast");
    qualified_type to;
    analysis_result r = type_id(cast.type_id(), to);
    /* [over.over]/1: "an explicit type conversion" */
    if (r == analysis_result::well_formed)
        VTRY_SET(r, target_expression(cast.operand(), to, m));
    return explicit_conversion(n, to, m, kinds);
}

/*
 * [expr.typeid]/1: "The result of a typeid expression is an lvalue of
 * static type const std::type_info".  /6: "If the header <typeinfo> is not
 * imported or included prior to a use of typeid, the program is
 * ill-formed."  A type-id operand (/4) and an unevaluated expression
 * operand that is not a glvalue of polymorphic class type (/3) name the
 * static type, which std::type_info object (a variable of the ABI's name,
 * typeid_object) is the glvalue; /2's dynamic type has no rules.  The
 * object's address is a constant; the run-time library defines the objects
 * of the fundamental types, and a class's has no lowering yet.
 */
analysis_result
semantic_analysis::type_identification(PostfixOperationTypeidOperation syntax,
                                       expression_meaning &m)
{
    SyntaxRef n = syntax.node;
    SyntaxElement written = syntax.operand();
    qualified_type operand;
    if (written.is(syntax_kind::type_id)) {
        VTRY(r, type_id(written, operand));
    } else {
        expression_meaning e;
        unevaluated++;
        analysis_result r = expression(written, e);
        unevaluated--;
        if (r != analysis_result::well_formed)
            return r;
        operand = e.type;
        if (e.category != value_category::prvalue &&
            type_table[operand.unqualified()].kind == type_kind::class_type &&
            entity_table[type_table[operand.unqualified()].entity].dynamic)
            return not_supported(n, "expr.typeid");
    }
    if (depends_on_template_parameter(operand))
        return not_supported(n, "expr.typeid");
    const struct type record = type_table[operand.unqualified()];
    if (record.kind == type_kind::lvalue_reference ||
        record.kind == type_kind::rvalue_reference)
        operand = record.operand;
    const struct type shape = type_table[operand.unqualified()];
    require_complete(shape.entity);
    if (shape.kind == type_kind::class_type && !entity_table[shape.entity].size)
        return not_supported(n, "expr.typeid");
    entity_index std_ =
        entity_table.find(0, tokens.pp.identifier("std", 3) + 1);
    entity_index info = 0;
    if (std_ && is_namespace(std_) && std_namespace(std_)) {
        lookup_result found = lookup_in_namespace(
            std_, tokens.pp.identifier("type_info", 9) + 1, lookup_filter::any);
        if (found.ambiguous)
            return not_supported(n, "namespace.qual");
        info = found.entity;
    }
    /* [expr.typeid]/1: an lvalue of type const std::type_info, which may be
       incomplete here (clang asks only for its declaration; a use of a
       member needs the class complete, [expr.ref]) */
    if (!info || entity_table[info].kind != entity_kind::class_type)
        return ill_formed(n,
                          "the header <typeinfo> shall be included before a "
                          "use of typeid",
                          "expr.typeid");
    /* /5: "the top-level cv-qualifiers of the glvalue ... are ignored" */
    entity_index object = typeid_object(operand, info);
    m = expression_meaning();
    m.type = entity_table[object].type;
    m.category = value_category::lvalue;
    m.entity = object;
    m.evaluation = constness::constant;
    m.node = expression_node(semantic_kind::entity, m, n.first_token(), object);
    return analysis_result::well_formed;
}

entity_index semantic_analysis::typeid_object(qualified_type type,
                                              entity_index info)
{
    type_index named = type.unqualified();
    for (entity_index e : typeid_objects)
        if (entity_table[e].typeinfo_of == named)
            return e;
    entity_index e = entity_table.make(entity_kind::variable, 0, 0);
    entity_table[e].type =
        qualified_type::make(entity_table[info].type.unqualified(), cv_const);
    entity_table[e].typeinfo_of = named;
    entity_table[e].linkage_of_name = linkage::external;
    typeid_objects.append(e);
    return e;
}

analysis_result semantic_analysis::postfix(SyntaxRef n, expression_meaning &m)
{
    PostfixOperation post = n.postfix_operation();
    if (PostfixOperationTypeidOperation id = post.typeid_operation())
        return type_identification(id, m);
    if (PostfixOperationIncrement inc = post.increment())
        return increment(n, inc.operand(),
                         inc.op().is(preprocessing_op_or_punc::MINUS_MINUS),
                         true, m);
    if (n.size() < 2)
        return not_supported(n, "expr.post");
    if (PostfixOperationNamedCast cast = post.named_cast())
        return named_cast(cast, m);
    if (post.member_access())
        return member_reference(n, m);
    /* [expr.type.conv]/1: "A simple-type-specifier or typename-specifier
       followed by a parenthesized optional expression-list or by a
       braced-init-list (the initializer) constructs a value of the
       specified type given the initializer." */
    InitializerParts written = InitializerParts::after(n, 1);
    bool parenthesized =
        written.form == InitializerParts::syntax_form::parenthesized;
    bool braced = written.form == InitializerParts::syntax_form::braced;
    type_index named = 0;
    /* [dcl.type.simple]/1: nested-name-specifier(opt) type-name */
    entity_index class_named = 0;
    uint32_t named_cv = 0;
    if ((parenthesized || braced) &&
        n[0].is(syntax_kind::simple_type_specifier)) {
        VTRY(r, qualified_type_name(n[0], named, named_cv));
        if (type_table[named].kind == type_kind::class_type &&
            entity_table[type_table[named].entity].kind ==
                entity_kind::class_type)
            class_named = type_table[named].entity;
    }
    if ((parenthesized || braced) && n[0].is(syntax_kind::typename_specifier)) {
        /* [temp.res]/3 with [expr.type.conv]/1: typename Q::name(args), the
           type the typename-specifier denotes */
        VTRY(r, typename_type(n[0], named, named_cv));
        if (type_table[named].kind == type_kind::class_type &&
            entity_table[type_table[named].entity].kind ==
                entity_kind::class_type)
            class_named = type_table[named].entity;
    }
    if ((parenthesized || braced) && n[0].is_token()) {
        const token &t = tokens[n[0].token()];
        if (t.kind == token_kind::identifier) {
            entity_index e = lookup(tokens.identifier(n[0].token()));
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
            VTRY(r, simple_type(n, k, named));
        }
    }
    if ((parenthesized || braced) && n[0].is_token() &&
        tokens[n[0].token()].kind == token_kind::identifier && !class_named &&
        !named) {
        /* [dcl.type.class.deduct]/1: a template-name of a class template
           alone is a placeholder for a deduced class type, which
           [expr.type.conv]/1 allows as the simple-type-specifier */
        lookup_result x = lookup_unqualified(tokens.identifier(n[0].token()),
                                             lookup_filter::any);
        uint32_t ct = class_template_of(x.unknown_entity);
        if (ct && n.size() <= 4) {
            entity_index deduced = 0;
            /* a lambda-expression among the arguments is one closure for
               the deduction and the initialization */
            uint32_t saved_reuse = closure_reuse;
            closure_reuse = n.index().index() | 0x80000000u;
            analysis_result r = deduced_class(n, ct, deduced);
            if (r == analysis_result::well_formed)
                r = class_conversion(n, deduced, m);
            size_t kept = 0;
            for (size_t i = 0; i < member_closures.size(); i++)
                if (member_closures[i].member != closure_reuse)
                    member_closures[kept++] = member_closures[i];
            member_closures.resize(kept);
            closure_reuse = saved_reuse;
            return r;
        }
    }
    if (named_cv)
        return not_supported(n, "expr.type.conv");
    if (class_named)
        return class_conversion(n, class_named, m);
    if ((parenthesized || braced) && n[0].is(syntax_kind::simple_template_id)) {
        /* [temp.names]/1 with [expr.type.conv]/1: a simple-template-id
           naming a class template is a simple-type-specifier
           ([dcl.type.simple]/1), the specialization's class */
        auto id = n[0];
        if (n[0].size() >= 3 && id[0].is_token() && id[0].is_identifier()) {
            lookup_result x = lookup_unqualified(
                tokens.identifier(id[0].token()), lookup_filter::any);
            entity_index found = x.entity;
            if (!found)
                found = x.unknown_entity;
            if (class_template_of(found)) {
                type_index t = 0;
                VTRY(r, template_type(n[0], 0, false, t));
                if (type_table[t].kind != type_kind::class_type)
                    return not_supported(n, "temp.res");
                return class_conversion(n, type_table[t].entity, m);
            }
            if (alias_template_of(found)) {
                /* [temp.alias]/2: the alias template-id "is equivalent to
                   the substitution of its template-arguments" in its
                   type: the simple-type-specifier names that type */
                type_index t = 0;
                uint32_t cv = 0;
                VTRY(r, template_type(n[0], 0, false, t, &cv));
                if (cv)
                    return not_supported(n, "expr.type.conv");
                if (type_table[t].kind == type_kind::class_type &&
                    entity_table[type_table[t].entity].kind ==
                        entity_kind::class_type)
                    return class_conversion(n, type_table[t].entity, m);
                named = t;
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
            record.kind != type_kind::pointer &&
            record.kind != type_kind::enumeration)
            return not_supported(n, "expr.type.conv");
        bool is_void = record.kind == type_kind::fundamental &&
                       record.fundamental == fundamental_type::void_;
        if (parenthesized && !written.list) {
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
                expression_node(semantic_kind::constant, m, n.first_token());
            return analysis_result::well_formed;
        }
        if (parenthesized) {
            if (written.list_of_several()) {
                if (has_pack_expansion(written.list)) {
                    owned_array<expression_meaning> expanded;
                    VTRY(r, expression_list_meanings(written.list, expanded));
                    if (expanded.empty()) {
                        m = expression_meaning();
                        m.type = to;
                        m.evaluation = constness::constant;
                        m.value = 0;
                        m.node = expression_node(semantic_kind::constant, m,
                                                 n.first_token());
                        return analysis_result::well_formed;
                    }
                    if (expanded.size() == 1) {
                        m = expanded[0];
                        return explicit_conversion(n, to, m);
                    }
                }
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
            VTRY(r, expression(written.list, m));
            return explicit_conversion(n, to, m);
        }
        if (braced) {
            if (is_void)
                return not_supported(n, "expr.type.conv");
            initialized_value result;
            VTRY(r, list_initialization(written.clause, to, true, result));
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
    if (PostfixOperationSubscript sub = post.subscript()) {
        /* [expr.sub]/1: "One of the expressions shall be a glvalue of type
           "array of T" or a prvalue of type "pointer to T" and the other
           shall be a prvalue of unscoped enumeration or integral type.  The
           result is of type "T". ... The expression E1[E2] is identical (by
           definition) to *((E1)+(E2)), except that in the case of an array
           operand, the result is an lvalue if that operand is an lvalue and
           an xvalue otherwise." */
        if (!sub.index() || sub.index().is(syntax_kind::braced_init_list,
                                           syntax_kind::initializer_list))
            return not_supported(n, "expr.sub");
        expression_meaning a, i;
        analysis_result r = expression(sub.operand(), a);
        if (r == analysis_result::well_formed)
            VTRY_SET(r, expression(sub.index(), i));
        /* [over.sub]/1: "the operator function is selected by overload
           resolution ([over.match.oper])" */
        bool handled = false;
        r = binary_operator_function(
            n, sub.bracket().token(), preprocessing_op_or_punc::LEFT_BRACKET,
            sub.operand(), a, sub.index(), i, handled, m);
        if (r != analysis_result::well_formed || handled)
            return r;
        expression_meaning *base = &a, *index = &i;
        auto is_vector = [&](const expression_meaning &x) {
            return type_table[x.type.unqualified()].kind == type_kind::vector;
        };
        if (is_vector(a) || is_vector(i)) {
            /* [ext:gnu_vector_size] v[i]: lane i of the vector, an lvalue
               where the vector is one (clang: of the vector's cv) */
            if (!is_vector(a)) {
                base = &i;
                index = &a;
            }
            bool object = base->category == value_category::lvalue;
            if (!object)
                VTRY_SET(r, operand_value(n, *base));
            VTRY_SET(r, operand_value(n, *index));
            promote_enumeration(*index);
            if (!is_integral(index->type))
                return not_supported(n, "ext.gnu");
            qualified_type lane = type_table[base->type.unqualified()].operand;
            lane = qualified_type::make(lane.unqualified(), base->type.cv());
            constness evaluation = constness::not_constant;
            m = expression_meaning();
            m.type = lane;
            m.category = value_category::prvalue;
            if (object)
                m.category = value_category::lvalue;
            m.evaluation = evaluation;
            m.beyond_integral_constant = true;
            m.node = expression_node(semantic_kind::subscript, m,
                                     n.first_token(), base->node, index->node);
            return analysis_result::well_formed;
        }
        auto is_array = [&](const expression_meaning &x) {
            return type_table[x.type.unqualified()].kind == type_kind::array;
        };
        if (!is_array(a) && !is_pointer(a.type) &&
            (is_array(i) || is_pointer(i.type))) {
            base = &i;
            index = &a;
        }
        if (is_array(*base) && base->category == value_category::prvalue)
            return not_supported(n, "expr.sub");
        r = operand_value(n, *base);
        if (r == analysis_result::well_formed)
            VTRY_SET(r, operand_value(n, *index));
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
        m.node = expression_node(semantic_kind::subscript, m, n.first_token(),
                                 base->node, index->node);
        return analysis_result::well_formed;
    }
    return not_supported(n, "expr.post");
}

/*
 * [over.match.class.deduct]/1: "When resolving a placeholder for a deduced
 * class type ([dcl.type.simple]) where the template-name names a primary
 * class template C, a set of functions and function templates, called the
 * guides of C, is formed", /3: "Initialization and overload resolution are
 * performed as described in [dcl.init] and [over.match.ctor],
 * [over.match.copy], or [over.match.list] ... If overload resolution
 * fails, the program is ill-formed.  Otherwise, the return type of the
 * selected function template specialization becomes the deduced class
 * template specialization."  Of the guides, only the deduction-guides
 * ([over.match.class.deduct]/1.3) are candidates, as function templates
 * ([temp.deduct.guide]); the implicit guides of C's constructors (/1.1) and
 * the copy deduction candidate (/1.2) are not, and a class template whose
 * deduction-guides none is viable for the arguments is not analysed.  The
 * arguments are the initializer's expression-list or braced elements
 * (no braced-init-list element, no pack expansion).
 */
analysis_result semantic_analysis::deduced_class(SyntaxRef n, uint32_t ct,
                                                 entity_index &c)
{
    c = 0;
    owned_array<SyntaxElement> clauses;
    /* a declarator's initializer, or a functional cast's T ( ... ) or
       T { ... } */
    InitializerParts parts(n);
    if (!parts)
        parts = InitializerParts::after(n, 1);
    if (SyntaxElement list = parts.braced_list()) {
        VTRY(gathered, list_clauses(list, clauses, nullptr));
    } else if (parts.form == InitializerParts::syntax_form::equals) {
        /* [dcl.type.class.deduct]/1: C x = e; the guides are resolved
           for the argument e ([over.match.class.deduct]/3) */
        clauses.append(parts.clause);
    } else if (parts.form == InitializerParts::syntax_form::parenthesized &&
               parts.list) {
        for (SyntaxElement a : parts.list.items(syntax_kind::initializer_list))
            clauses.append(a);
    }
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    owned_array<expression_meaning> arguments;
    for (SyntaxElement a : clauses) {
        if (a.is(syntax_kind::braced_init_list) ||
            a.is(preprocessing_op_or_punc::ELLIPSIS))
            return not_supported(n, "over.match.class.deduct");
        expression_meaning e;
        VTRY(r, passed_expression(a, e));
        if (e.entity && entity_table[e.entity].kind == entity_kind::function)
            return not_supported(n, "over.match.class.deduct");
        arguments.append(e);
    }
    /* [over.match.class.deduct]/1.2: a lone argument of the template's own
       specialization selects the copy deduction candidate (not a guide
       here) */
    if (arguments.size() == 1) {
        const struct type &argument =
            type_table[arguments[0].type.unqualified()];
        if (argument.kind == type_kind::class_type &&
            class_template_of(argument.entity) == ct)
            return not_supported(n, "over.match.class.deduct");
    }
    implicit_deduction_guides(ct);
    owned_array<uint32_t> group;
    for (size_t i = 0; i < function_templates.size(); i++)
        if (function_templates[i].guide_of == ct)
            group.append(static_cast<uint32_t>(i + 1));
    if (!group.empty()) {
        owned_array<overload_candidate> candidates;
        owned_array<uint32_t> first_arguments;
        owned_array<template_argument> all;
        VTRY(added, template_candidates(n, group, arguments.data(),
                                        static_cast<uint32_t>(arguments.size()),
                                        candidates, &first_arguments, &all,
                                        nullptr, 0));
        if (!candidates.empty()) {
            size_t chosen = 0;
            VTRY(best, best_viable_function(n, candidates, nullptr, arguments,
                                            chosen));
            /* (the arguments are analysed again by the initialization) */
            expressions.resize(expressions_before);
            name_uses.resize(uses_before);
            /* [over.match.copy]/1, [over.match.list]/1: "In copy-list-
               initialization, if an explicit constructor is chosen, the
               initialization is ill-formed", and an explicit guide is no
               candidate of copy-initialization: not analysed here */
            if (parts.form == InitializerParts::syntax_form::equals &&
                candidates[chosen].template_index) {
                const function_template &guide =
                    function_templates[candidates[chosen].template_index - 1];
                if (guide.explicit_guide)
                    return not_supported(n, "over.match.class.deduct");
                SyntaxElement first = guide.declarator[0];
                if (first.is_token() &&
                    first.keyword() == standard_keyword::explicit_)
                    return not_supported(n, "over.match.class.deduct");
            }
            qualified_type function =
                entity_table[candidates[chosen].function].type;
            const struct type &returned =
                type_table[type_table[function.unqualified()]
                               .operand.unqualified()];
            if (returned.kind != type_kind::class_type)
                return not_supported(n, "over.match.class.deduct");
            c = returned.entity;
            return analysis_result::well_formed;
        }
    }
    /* [over.match.class.deduct]/1.1: implicit deduction guides synthesized
       from the primary class template's non-template constructors */
    const function_template ct_rec = function_templates[ct - 1];
    if (ct_rec.definition &&
        ct_rec.definition.is(syntax_kind::class_specifier)) {
        entity_index outer_scope = entity_table[ct_rec.entity].enclosing;
        uint32_t class_name = entity_table[ct_rec.entity].name;
        entity_index saved_scope = current_scope;
        entity_index saved_class = defining_class;
        uint32_t saved_cv = function_cv;
        owned_array<parameter_meaning> saved_params =
            static_cast<owned_array<parameter_meaning> &&>(function_parameters);
        entity_index param_scope =
            entity_table.make(entity_kind::block_scope, 0, outer_scope);
        for (uint32_t i = 0; i < ct_rec.parameter_count; i++) {
            entity_index pe =
                template_parameter_entities[ct_rec.first_parameter + i];
            if (entity_table[pe].name)
                entity_table.bind(param_scope, entity_table[pe].name, pe);
        }
        auto members =
            ct_rec.definition.class_specifier().member_specification();
        for (SyntaxElement mem : members.elements()) {
            if (!mem.is(syntax_kind::function_definition,
                        syntax_kind::member_declaration))
                continue;
            uint32_t at = leading_gnu_attributes(mem.index());
            if (at < mem.size() && mem[at].is(syntax_kind::decl_specifier_seq))
                at++;
            DeclaratorParts ctor(mem[at]);
            if (!ctor.direct || !ctor.id.is_identifier() ||
                tokens.identifier(ctor.id.token()) != class_name ||
                !ctor.parameters())
                continue;
            current_scope = param_scope;
            defining_class = 0;
            function_cv = 0;
            function_parameters.clear();
            qualified_type ctor_type = qualified_type::make(
                type_table.fundamental(fundamental_type::void_), cv_none);
            analysis_result fr = function(ctor.parameters().index(), ctor_type);
            current_scope = saved_scope;
            defining_class = saved_class;
            function_cv = saved_cv;
            function_parameters =
                static_cast<owned_array<parameter_meaning> &&>(saved_params);
            if (fr != analysis_result::well_formed)
                continue;
            const struct type ftype = type_table[ctor_type.unqualified()];
            if (ftype.kind != type_kind::function || ftype.variadic ||
                ftype.parameter_count != arguments.size())
                continue;
            const qualified_type *params =
                type_table.parameters(ctor_type.unqualified());
            owned_array<template_argument> values;
            owned_array<bool> set;
            for (uint32_t i = 0; i < ct_rec.parameter_count; i++) {
                values.append(template_argument());
                set.append(false);
            }
            bool ok = true;
            for (uint32_t i = 0; i < ftype.parameter_count && ok; i++) {
                qualified_type atype = arguments[i].type;
                if (type_table[params[i].unqualified()].kind !=
                        type_kind::lvalue_reference &&
                    type_table[params[i].unqualified()].kind !=
                        type_kind::rvalue_reference)
                    atype = qualified_type::make(atype.unqualified(), cv_none);
                ok = deduce_type(params[i], atype, values, set, false);
            }
            if (!ok)
                continue;
            if (default_template_arguments(n, ct, values, set) !=
                analysis_result::well_formed)
                continue;
            expressions.resize(expressions_before);
            name_uses.resize(uses_before);
            return instantiate_class(n, ct, values.data(),
                                     static_cast<uint32_t>(values.size()), c);
        }
        current_scope = saved_scope;
        defining_class = saved_class;
        function_cv = saved_cv;
        function_parameters =
            static_cast<owned_array<parameter_meaning> &&>(saved_params);
    }
    return not_supported(n, "over.match.class.deduct");
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
analysis_result semantic_analysis::call(SyntaxRef n, expression_meaning &m)
{
    /* postfix-expression ( expression-list_opt ); a functional cast's
       simple-type-specifier ( expression-list_opt ) is none */
    PostfixOperationCall syntax = n.postfix_operation().call();
    SyntaxElement callee = syntax.callee();
    SyntaxElement arguments_written = syntax.arguments();
    SyntaxElement parenthesis = syntax.parenthesis();
    if (!syntax) {
        PostfixOperationTypeConversion conversion =
            n.postfix_operation().type_conversion();
        callee = conversion.type();
        arguments_written = conversion.arguments();
        parenthesis = conversion.parenthesis();
        if (!parenthesis)
            return not_supported(n, "expr.call");
    }
    if (builtin_addressof_call(n))
        return builtin_addressof(n, m);
    if (builtin_shufflevector_call(n))
        return builtin_shufflevector(n, m);
    if (builtin_complex_call(n))
        return builtin_complex(n, m);
    /* [expr.prim.id.dtor]/2: a destructor's name is a call's callee alone */
    PostfixOperationMemberAccess access =
        callee.postfix_operation().member_access();
    if (access && !access.template_keyword() && destructor_id(access.member()))
        return destructor_call(n, m);
    /* [class.copy.assign]/12: B::operator=(x) on an empty base class with an
       implicit trivial copy/move assignment operator writes no bytes */
    if (arguments_written && callee.is(syntax_kind::qualified_id) &&
        callee.size() == 2 &&
        callee[0].is(syntax_kind::nested_name_specifier) &&
        callee[1].operator_function_id() && callee[1].size() == 2 &&
        callee[1][1].is(preprocessing_op_or_punc::EQUAL) && current_class()) {
        nominated_scope qscope;
        if (nested_name_specifier(callee[0], qscope, false) ==
                analysis_result::well_formed &&
            !qscope.global && !qscope.dependent && qscope.entity &&
            entity_table[qscope.entity].kind == entity_kind::class_type &&
            entity_table[qscope.entity].member_count == 0 &&
            entity_table[qscope.entity].base_count == 0 &&
            !entity_table[qscope.entity].dynamic &&
            !entity_table.find(
                qscope.entity,
                operator_name(preprocessing_op_or_punc::EQUAL))) {
            expression_meaning arg;
            VTRY(ar, expression(arguments_written, arg));
            expression_meaning ptr;
            VTRY(tr, this_pointer(n.first_token(), ptr));
            expression_meaning self;
            self.type = entity_table[current_class()].type;
            self.category = value_category::lvalue;
            self.evaluation = constness::not_constant;
            self.node = expression_node(semantic_kind::indirection, self,
                                        n.first_token(), ptr.node);
            VTRY(br, to_base(n.first_token(), qscope.entity, self));
            m = self;
            m.node = expression_node(semantic_kind::comma, m, n.first_token(),
                                     arg.node, self.node);
            return analysis_result::well_formed;
        }
    }
    owned_array<SyntaxElement> arguments;
    /* [temp.variadic]/5: the arguments, pack expansions instantiated: each
       analysed now, one per element of the packs its pattern names */
    owned_array<expression_meaning> expanded;
    if (SyntaxElement c = arguments_written) {
        if (c.is(syntax_kind::initializer_list)) {
            owned_array<SyntaxElement> written;
            owned_array<bool> expansion;
            if (!expansion_list(c, written, expansion))
                return not_supported(n, "temp.variadic");
            bool any = false;
            for (bool x : expansion)
                any = any || x;
            for (size_t i = 0; i < written.size(); i++) {
                SyntaxElement a = written[i];
                if (!any) {
                    arguments.append(a);
                    continue;
                }
                if (a.is(syntax_kind::braced_init_list))
                    return not_supported(n, "temp.variadic");
                if (!expansion[i]) {
                    expression_meaning e;
                    VTRY(r, expression(a, e));
                    arguments.append(a);
                    expanded.append(e);
                    continue;
                }
                uint32_t count = 0;
                token_index end = first_token(a) + 1;
                if (!a.is_token())
                    end = a.end_token();
                if (!expansion_count(first_token(a), end, count))
                    return not_supported(n, "temp.variadic");
                for (uint32_t k = 0; k < count; k++) {
                    expression_meaning e;
                    VTRY(r, expansion_element_expression(a, k, e));
                    arguments.append(a);
                    expanded.append(e);
                }
            }
        } else {
            arguments.append(c);
        }
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
           it), called on (*this); [expr.prim.id.qual]/2: through a class's
           name, [class.mfct.non-static]/3 */
        entity_index held = 0;
        entity_index named_in = 0;
        owned_array<SyntaxElement> written;
        SyntaxRef written_at = 0;
        VTRY_SET(r, member_callee(callee, held, named_in, written, written_at));
        if (held) {
            bool ambiguous = false;
            entity_index from = current_class();
            if (!named_in)
                named_in = entity_table[held].enclosing;
            /* (*this) is the object where this class is, or is derived
               from, the class the name is of; elsewhere (a static member
               function, a nested class's member, a default argument) there
               is no object, and only a static member function can be
               called ([class.static.mfct]/2) */
            bool related = from && (named_in == from ||
                                    base_step(from, named_in, ambiguous));
            if (ambiguous)
                return not_supported(n, "temp.mem");
            expression_meaning object;
            bool has_object = related && has_this_object();
            if (has_object)
                VTRY_SET(r, this_object(n.first_token(), object));
            owned_array<expression_meaning> early;
            for (const expression_meaning &e : expanded)
                early.append(e);
            const expression_meaning *implied = nullptr;
            if (has_object)
                implied = &object;
            const owned_array<SyntaxElement> *as_written = nullptr;
            if (written_at)
                as_written = &written;
            return member_template_call(n, held, implied, arguments, early,
                                        as_written, written_at, m);
        }
        VTRY_SET(r, template_callee(callee, name, ft, specified, qualified));
        if (ft) {
            owned_array<expression_meaning> early;
            for (const expression_meaning &e : expanded)
                early.append(e);
            {
                resolving_call scope(resolving_calls);
                VTRY_SET(r, template_call(n, name, ft, specified, qualified,
                                          arguments, early, f));
            }
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
    token_index adl_token = 0;
    uint32_t adl_name = 0;
    if (callee.is_token() && callee.is_identifier()) {
        adl_token = callee.token();
        adl_name = tokens.identifier(adl_token);
    } else if (auto op = callee.operator_function_id()) {
        declarator_meaning dm;
        VTRY_SET(r, operator_function_id(op, dm));
        adl_token = dm.id;
        adl_name = tokens.identifier(adl_token);
    }
    if (adl_token) {
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
        token_index t = adl_token;
        uint32_t name = adl_name;
        lookup_result x = lookup_unqualified(name, lookup_filter::any);
        /* ([lex.name]/3 reserves __builtin_x to the implementation: a
           builtin function, found by the call's analysis of the name) */
        bool dependent = !x.ambiguous && !x.unknown && !builtin_spelled(name);
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
            for (SyntaxElement a : arguments) {
                expression_meaning e;
                function_set named_set;
                if (named_function_set(a, named_set)) {
                    /* [basic.lookup.argdep]/2: "If an argument is the name
                       or address of an overload set, its associated
                       entities and namespaces are the union of those
                       associated with each of the members of the set";
                       the argument is analysed for its parameter
                       ([over.over], call_with) */
                    e.overload_name = named_set.at;
                    for (entity_index f : named_set.functions)
                        types.append(entity_table[f].type);
                } else if (!a.is(syntax_kind::braced_init_list)) {
                    VTRY_SET(r, passed_expression(a, e));
                    types.append(e.type);
                }
                early.append(e);
            }
            owned_array<entity_index> set;
            if (x.entity)
                set.append(x.entity);
            owned_array<entity_index> held;
            if (!argument_dependent_lookup(name, types, set, 0, &held))
                return not_supported(n, "basic.lookup.argdep");
            if (!held.empty()) {
                /* (the other templates and non-template functions ADL found
                   join the first's, the candidates of template_call) */
                uint32_t found_template = function_template_of(held[0]);
                if (!found_template)
                    return not_supported(n, "basic.lookup.argdep");
                owned_array<template_argument> specified;
                {
                    resolving_call scope(resolving_calls);
                    VTRY_SET(r, template_call(n, t, found_template, specified,
                                              false, arguments, early, f));
                }
                return call_with(n, f, f.type, 0, 0, arguments, early, m);
            }
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
        overloaded_callee = callee_name(callee);
        callee_has_arguments = !arguments.empty();
        member_template_name = 0;
        r = expression(callee, f);
        overloaded_callee = 0;
        callee_has_arguments = false;
    }
    if (entity_index held = member_template_name) {
        member_template_name = 0;
        if (r != analysis_result::well_formed)
            return r;
        expression_meaning object = member_template_object;
        owned_array<SyntaxElement> written;
        for (SyntaxElement w : member_template_written)
            written.append(w);
        SyntaxRef written_at = member_template_written_at;
        member_template_written.clear();
        member_template_written_at = 0;
        const owned_array<SyntaxElement> *as_written = nullptr;
        if (written_at)
            as_written = &written;
        return member_template_call(n, held, &object, arguments, early,
                                    as_written, written_at, m);
    }
    if (r != analysis_result::well_formed)
        return r;
    /* [ext:clang_builtin_functions] __builtin_invoke is the call of its
       first argument with the others (builtin-invoke.cpp) */
    if (f.entity && entity_table[f.entity].gnu_builtin_row &&
        gnu_builtin_at(entity_table[f.entity].gnu_builtin_row).form ==
            builtin_form::invoke) {
        owned_array<expression_meaning> ops;
        if (early.size() == arguments.size()) {
            for (const expression_meaning &e : early)
                ops.append(e);
        } else {
            for (SyntaxElement a : arguments) {
                if (a.is(syntax_kind::braced_init_list))
                    return not_supported(n, "expr.call");
                expression_meaning e;
                VTRY_SET(r, expression(a, e));
                ops.append(e);
            }
        }
        return builtin_invoke(n, f.entity, arguments, ops, m);
    }
    if (type_table[f.type.unqualified()].kind == type_kind::class_type) {
        /* [over.call]/1: "where the postfix-expression is of class type,
           the operator function is selected by overload resolution
           ([over.call.object])" and the expression "interpreted as
           postfix-expression . operator () (expression-list_opt)" */
        owned_array<SyntaxElement> operand_syntax;
        owned_array<expression_meaning> operands;
        operand_syntax.append(callee);
        operands.append(f);
        for (size_t i = 0; i < arguments.size(); i++) {
            SyntaxElement a = arguments[i];
            if (a.is(syntax_kind::braced_init_list))
                return not_supported(n, "over.ics.list");
            expression_meaning e;
            if (i < expanded.size() && expanded.size() == arguments.size()) {
                /* [temp.variadic]/5: an element of a pack expansion,
                   analysed above */
                e = expanded[i];
            } else {
                VTRY_SET(r, expression(a, e));
            }
            operand_syntax.append(a);
            operands.append(e);
        }
        bool handled = false;
        r = overloaded_operator(n, parenthesis.token(),
                                preprocessing_op_or_punc::LEFT_PARENTHESIS,
                                false, operand_syntax, operands, handled, m);
        if (r != analysis_result::well_formed || handled)
            return r;
        /* [over.call.object]/2: if the class has no viable operator() and no
           conversion functions (no surrogate call functions), calling it is
           ill-formed in a substitution context */
        if (substituting && !has_implicit_conversion_functions(
                                type_table[f.type.unqualified()].entity))
            return ill_formed(n,
                              "called object type is not a function or "
                              "function pointer",
                              "over.call.object");
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
        VTRY_SET(r, operand_value(n, f));
        const struct type pointer = type_table[f.type.unqualified()];
        if (pointer.kind == type_kind::pointer &&
            type_table[pointer.operand.unqualified()].kind ==
                type_kind::function) {
            function_type = pointer.operand;
        } else {
            if (fundamental_of(f.type) == fundamental_type::none &&
                !is_pointer(f.type) &&
                type_table[f.type.unqualified()].kind != type_kind::enumeration)
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
        {
            resolving_call scope(resolving_calls);
            VTRY_SET(r, resolve_call(n, arguments, early, set, object, f));
        }
        function_type = f.type;
        if (member)
            member = semantics[f.node].b;
    } else if (set.size() == 1 && f.node) {
        /* the one function the name names is the function called (its use
           is not the name's, a callee's); [dcl.fct.def.delete]/2's deleted
           calls of it are not diagnosed here yet (a template argument's
           substitution error is attributed to the use) */
        VTRY(du, unavailable_use(semantics[f.node].at, set[0]));
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
    SyntaxElement callee, token_index &name, uint32_t &function_template,
    owned_array<template_argument> &specified, bool &qualified)
{
    name = 0;
    function_template = 0;
    qualified = false;
    if (auto op = callee.operator_function_id()) {
        /* [over.call.func]/3: operator<(a, b) names the function templates
           of that operator found by unqualified lookup too */
        declarator_meaning function_name;
        VTRY(r, operator_function_id(op, function_name));
        name = function_name.id;
        lookup_result x =
            lookup_unqualified(tokens.identifier(name), lookup_filter::any);
        if (!x.entity)
            function_template = function_template_of(x.unknown_entity);
        return analysis_result::well_formed;
    }
    if (callee.is_token()) {
        if (tokens[callee.token()].kind != token_kind::identifier)
            return analysis_result::well_formed;
        name = callee.token();
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

/*
 * [over.call.func]/3: the callee of a call, named by a member function
 * template of a class: an unqualified name or template-id
 * (f, f<T>), or a class-qualified one (X::f, X::template f<T>); held is
 * the held name of the member function templates (and the functions of
 * their chain) it found, named_in the class of a qualified name, written
 * the explicit template arguments of a template-id (written_at its
 * syntax).  held is 0 for anything else.
 */
analysis_result semantic_analysis::member_callee(
    SyntaxElement callee, entity_index &held, entity_index &named_in,
    owned_array<SyntaxElement> &written, SyntaxRef &written_at)
{
    held = 0;
    named_in = 0;
    written_at = 0;
    SyntaxElement id = callee;
    nominated_scope scope;
    bool qualified = false;
    if (callee.is(syntax_kind::qualified_id)) {
        bool keyword =
            callee.size() == 3 && callee[1].is(standard_keyword::template_);
        if ((callee.size() != 2 && !keyword) ||
            !callee[0].is(syntax_kind::nested_name_specifier))
            return analysis_result::well_formed;
        id = callee[callee.size() - 1];
        qualified = true;
    }
    token_index name = 0;
    if (id.is_token()) {
        if (!id.is_identifier())
            return analysis_result::well_formed;
        name = id.token();
    } else if (SyntaxElement t = id.simple_template_id().template_name()) {
        name = t.token();
    } else if (auto function = id.operator_function_id()) {
        /* [over.call]: operator@(args), an operator function's name
           called as a function (json's operator[](T* key) calling
           operator[](key_type(key))) */
        declarator_meaning function_name;
        VTRY(r, operator_function_id(function, function_name));
        name = function_name.id;
    } else {
        return analysis_result::well_formed;
    }
    lookup_result x;
    /* (the uses a callee that is no member template's records are recorded
       again by its analysis) */
    size_t uses_before = name_uses.size();
    if (qualified) {
        VTRY(r, nested_name_specifier(callee[0], scope));
        if (is_namespace(scope.entity) ||
            entity_table[scope.entity].kind != entity_kind::class_type) {
            name_uses.resize(uses_before);
            return analysis_result::well_formed;
        }
        x = lookup_qualified(scope.entity, tokens.identifier(name),
                             lookup_filter::any);
        named_in = scope.entity;
    } else {
        x = lookup_unqualified(tokens.identifier(name), lookup_filter::any);
    }
    if (x.unknown && !x.entity && !x.ambiguous &&
        member_function_template_of(x.unknown_entity))
        held = x.unknown_entity;
    else if (x.entity && !x.ambiguous &&
             entity_table[x.entity].member_templates)
        held = x.entity;
    if (!held) {
        named_in = 0;
        name_uses.resize(uses_before);
        return analysis_result::well_formed;
    }
    if (id.is(syntax_kind::simple_template_id)) {
        owned_array<bool> expansion;
        VTRY(r, written_arguments_of(id, written, expansion));
        for (bool e : expansion)
            if (e)
                return not_supported(id.index(), "temp.variadic");
        written_at = id.index();
    }
    return analysis_result::well_formed;
}

/*
 * [temp.names]/1: the template-arguments of the simple-template-id id,
 * one element each; [temp.variadic]/5: an item may be followed by an
 * ellipsis (a lone argument is the template-id's own child), marked in
 * expansion.
 */
analysis_result
semantic_analysis::written_arguments_of(SyntaxElement id,
                                        owned_array<SyntaxElement> &written,
                                        owned_array<bool> &expansion)
{
    for (uint32_t i = 2; i + 1 < id.size(); i++) {
        /* [temp.variadic]/5: an item may be followed by an ellipsis
           (a lone argument is the template-id's own child) */
        if (id[i].is(preprocessing_op_or_punc::ELLIPSIS)) {
            if (written.empty() || expansion.back())
                return not_supported(id.index(), "temp.variadic");
            expansion.back() = true;
        } else if (id[i].is(syntax_kind::template_argument_list)) {
            if (!expansion_list(id[i], written, expansion))
                return not_supported(id.index(), "temp.variadic");
        } else {
            written.append(id[i]);
            expansion.append(false);
        }
    }
    /* a type-id that ends in the ellipsis: its pattern the type-id
       before it */
    for (size_t w = 0; w < written.size(); w++)
        if (written[w].is(syntax_kind::type_id) && written[w].size() == 2 &&
            written[w][1].is(preprocessing_op_or_punc::ELLIPSIS)) {
            if (expansion[w])
                return not_supported(id.index(), "temp.variadic");
            expansion[w] = true;
        }
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::template_callee_parts(
    SyntaxElement callee, token_index &name, uint32_t &function_template,
    owned_array<template_argument> &specified, bool &qualified)
{
    SyntaxElement id = callee;
    nominated_scope scope;
    if (callee.is(syntax_kind::qualified_id)) {
        /* [namespace.qual]: a function template of a namespace;
           [expr.prim.id.qual]/2: a member function template of a class,
           with the keyword template ([temp.names]/4) or without */
        bool keyword =
            callee.size() == 3 && callee[1].is(standard_keyword::template_);
        if ((callee.size() != 2 && !keyword) ||
            !callee[0].is(syntax_kind::nested_name_specifier))
            return analysis_result::well_formed;
        id = callee[callee.size() - 1];
        VTRY(r, nested_name_specifier(callee[0], scope));
        qualified = true;
    }
    owned_array<SyntaxElement> written;
    owned_array<bool> expansion;
    if (id.is_token()) {
        if (!qualified || tokens[id.token()].kind != token_kind::identifier)
            return analysis_result::well_formed;
        name = id.token();
    } else if (id.is(syntax_kind::simple_template_id)) {
        if (!id.simple_template_id().template_name())
            return analysis_result::well_formed;
        name = id[0].token();
        VTRY(r, written_arguments_of(id, written, expansion));
    } else if (id.is(syntax_kind::template_id) && id.size() >= 3 &&
               id[0].operator_function_id() && !qualified) {
        /* [temp.names]/1: operator-function-id < template-argument-list >;
           its name token was named by the operator-function-id's analysis */
        name = id[0].operator_function_id().operator_keyword().token();
        VTRY(r, written_arguments_of(id, written, expansion));
    } else {
        return analysis_result::well_formed;
    }
    lookup_result x;
    bool member = qualified && !is_namespace(scope.entity);
    if (qualified)
        x = lookup_qualified(scope.entity, tokens.identifier(name),
                             lookup_filter::any);
    else
        x = lookup_unqualified(tokens.identifier(name), lookup_filter::any);
    function_template = function_template_of(x.unknown_entity, member);
    if (member && function_template &&
        entity_table[function_templates[function_template - 1].entity]
            .nonstatic_member)
        /* [class.mfct.non-static]/3: C::f(args) names a non-static member
           function, called on (*this) (or an error outside one) */
        return not_supported(id.index(), "class.mfct.non-static");
    if (!function_template) {
        /* a template-id of no function template analysis knows: what it
           was before */
        if (!written.empty() || !qualified) {
            analysis_result refused = not_supported(id.index(), "temp.names");
            template_name_detail(name, x, x.entity);
            return refused;
        }
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
            overloaded_written_expansion.clear();
            for (SyntaxElement a : written)
                overloaded_written.append(a);
            for (bool x : expansion)
                overloaded_written_expansion.append(x);
            overloaded_written_at = id.index();
            return analysis_result::well_formed;
        }
    }
    /* [temp.arg.explicit]/6: "The template argument list shall not
       specify more template-arguments than there are corresponding
       template-parameters" (clang: no candidate) */
    for (size_t w = 0; w < written.size(); w++) {
        SyntaxElement a = written[w];
        /* [temp.variadic]/5: a pack expansion's pattern, once per element
           of the packs it names ([temp.arg.explicit]/9: the elements
           specify the trailing pack's first ones); a template's own pack
           expansion is not analysed */
        uint32_t times = 1;
        if (expansion[w]) {
            token_index first = first_token(a);
            token_index end = first + 1;
            if (!a.is_token())
                end = a.end_token();
            if (end > first + 1 && SyntaxElement::from_token(end - 1).is(
                                       preprocessing_op_or_punc::ELLIPSIS))
                end--;
            if (!expansion_count(first, end, times))
                return not_supported(id.index(), "temp.variadic");
        }
        for (uint32_t k = 0; k < times; k++) {
            uint32_t position = explicit_argument_position(
                function_template, static_cast<uint32_t>(specified.size()));
            if (position == UINT32_MAX)
                return not_supported(id.index(), "temp.arg.explicit");
            template_argument x;
            analysis_result r = analysis_result::well_formed;
            {
                expansion_scope scope(*this, expansion[w], k, true);
                r = template_argument_for(id.index(), function_template,
                                          position, a, specified.data(), x);
            }
            if (r != analysis_result::well_formed)
                return r;
            specified.append(x);
        }
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
    /* the template parameter pack, wherever it is: explicit arguments from
       its position on are its elements */
    for (uint32_t k = f.parameter_count; k-- > 0;)
        if (template_parameter_is_pack(f.first_parameter + k)) {
            if (i >= k)
                return k;
            return i;
        }
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
    SyntaxRef n, const owned_array<uint32_t> &group,
    const expression_meaning *meanings, uint32_t count,
    owned_array<overload_candidate> &candidates,
    owned_array<uint32_t> *first_arguments, owned_array<template_argument> *all,
    const owned_array<SyntaxElement> *written, SyntaxRef written_at,
    const owned_array<template_argument> *specified,
    const owned_array<bool> *written_expansion,
    const qualified_type *target_function)
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
                SyntaxElement a = (*written)[i];
                bool is_exp = written_expansion && (*written_expansion)[i];
                uint32_t times = 1;
                if (is_exp) {
                    token_index first = first_token(a);
                    token_index end = first + 1;
                    if (!a.is_token())
                        end = a.end_token();
                    if (end > first + 1 &&
                        SyntaxElement::from_token(end - 1).is(
                            preprocessing_op_or_punc::ELLIPSIS))
                        end--;
                    if (!expansion_count(first, end, times)) {
                        r = not_supported(written_at, "temp.variadic");
                        break;
                    }
                }
                for (uint32_t k = 0; !failed && k < times; k++) {
                    uint32_t position = explicit_argument_position(
                        g, static_cast<uint32_t>(d.size()));
                    if (position == UINT32_MAX) {
                        failed = true;
                        break;
                    }
                    entity_index p = template_parameter_entities
                        [function_templates[g - 1].first_parameter + position];
                    bool type_parameter =
                        entity_table[p].kind == entity_kind::typedef_name;
                    if (entity_table[p].kind != entity_kind::template_entity &&
                        type_parameter != a.is(syntax_kind::type_id)) {
                        failed = true;
                        break;
                    }
                    template_argument x;
                    {
                        expansion_scope scope(*this, is_exp, k, true);
                        r = template_argument_for(written_at, g, position, a,
                                                  d.data(), x);
                    }
                    if (r != analysis_result::well_formed)
                        break;
                    d.append(x);
                }
                if (r != analysis_result::well_formed)
                    break;
            }
        }
        if (r == analysis_result::well_formed && !failed) {
            if (target_function) {
                /* [temp.deduct.funcaddr]/1 */
                owned_array<template_argument> given;
                for (const template_argument &a : d)
                    given.append(a);
                r = deduce_from_function_type(n, g, *target_function, d, failed,
                                              &given);
            } else {
                r = deduce_from_call(n, g, meanings, count, d, &failed);
            }
        }
        entity_index e = 0;
        const struct function_template ft = function_templates[g - 1];
        if (r == analysis_result::well_formed && !failed) {
            unevaluated++;
            r = instantiate(n, ft.entity, d.data(),
                            static_cast<uint32_t>(d.size()), e);
            unevaluated--;
        }
        if (r == analysis_result::well_formed && !failed && target_function) {
            /* [over.over]/3: selected when the specialization's type is
               the target's, after the [conv.fctptr] conversion */
            qualified_type t = entity_table[e].type;
            bool compatible = type_table[t.unqualified()].non_throwing &&
                              differ_in_exceptions(t, *target_function);
            if (t.unqualified() != target_function->unqualified() &&
                !compatible)
                failed = true;
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

analysis_result
semantic_analysis::braced_deduction_elements(SyntaxElement list,
                                             expression_meaning &m)
{
    owned_array<SyntaxElement> clauses;
    owned_array<uint32_t> packs;
    designation_set designations;
    VTRY(gathered, list_clauses(list, clauses, &packs, &designations));
    if (designations.any)
        return analysis_result::well_formed;
    for (uint32_t x : packs)
        if (x != no_expansion)
            return analysis_result::well_formed;
    owned_array<expression_meaning> found;
    for (SyntaxElement c : clauses) {
        if (c.is(syntax_kind::braced_init_list))
            return analysis_result::well_formed;
        expression_meaning e;
        size_t uses_before = name_uses.size();
        VTRY(r, passed_expression(c, e));
        if (!e.node || e.overload_name) {
            name_uses.resize(uses_before);
            return analysis_result::well_formed;
        }
        found.append(e);
    }
    m.braced_known = true;
    m.braced_first = static_cast<uint32_t>(braced_elements.size());
    m.braced_count = static_cast<uint32_t>(found.size());
    for (const expression_meaning &e : found)
        braced_elements.append(e);
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::template_call(
    SyntaxRef n, token_index name, uint32_t function_template,
    owned_array<template_argument> &specified, bool qualified,
    const owned_array<SyntaxElement> &arguments,
    owned_array<expression_meaning> &early, expression_meaning &f)
{
    owned_array<SyntaxElement> written;
    owned_array<bool> written_expansion;
    SyntaxRef written_at = overloaded_written_at;
    for (SyntaxElement a : overloaded_written)
        written.append(a);
    for (bool x : overloaded_written_expansion)
        written_expansion.append(x);
    overloaded_written.clear();
    overloaded_written_expansion.clear();
    overloaded_written_at = 0;
    owned_array<qualified_type> types;
    /* (arguments analysed already: a pack expansion's, call) */
    bool given = !early.empty();
    for (const expression_meaning &e : early) {
        if (e.entity && entity_table[e.entity].kind == entity_kind::function)
            return not_supported_because(n, "temp.deduct.call",
                                         "a function as an argument analysed "
                                         "before the call (pack expansion)");
        types.append(e.type);
    }
    for (SyntaxElement a : arguments) {
        if (given)
            break;
        expression_meaning e;
        if (a.is(syntax_kind::braced_init_list)) {
            /* [temp.deduct.call]/1: a braced-init-list is a non-deduced
               context for a parameter that is not std::initializer_list
               or an array: no type, no node (deduce_from_call skips it);
               the list initializes the parameter after deduction */
            VTRY(r, braced_deduction_elements(a, e));
            types.append(e.type);
            early.append(e);
            continue;
        }
        /* [temp.deduct.call]/6: "When P is a function type, function
           pointer type, or pointer-to-member-function type: If the argument
           is an overload set containing one or more function templates, the
           parameter is treated as a non-deduced context.  If the argument
           is an overload set (not containing function templates), trial
           argument deduction is attempted using each of the members of the
           set": not analysed */
        if (overload_set_operand(a, e)) {
            types.append(e.type);
            early.append(e);
            continue;
        }
        size_t uses_before = name_uses.size();
        VTRY(r, passed_expression(a, e));
        /* (a function's name whose other declarations analysis did not
           follow may be such a set too) */
        if (e.entity && entity_table[e.entity].kind == entity_kind::function &&
            entity_table[e.entity].next_overload) {
            name_uses.resize(uses_before);
            return not_supported_because(
                n, "temp.deduct.call", "an overloaded function as an argument");
        }
        types.append(e.type);
        early.append(e);
    }
    /* [basic.lookup.argdep]/1: for an unqualified name only */
    owned_array<entity_index> others;
    owned_array<entity_index> adl_templates;
    if (!qualified &&
        (!argument_dependent_lookup(
            tokens.identifier(name), types, others,
            function_templates[function_template - 1].name, &adl_templates)))
        return not_supported(n, "basic.lookup.argdep");
    owned_array<template_argument> &deduced = specified;
    const expression_meaning *meanings = nullptr;
    if (!early.empty())
        meanings = &early[0];
    owned_array<uint32_t> group;
    function_template_overloads(function_templates[function_template - 1].name,
                                group);
    for (entity_index held : adl_templates) {
        owned_array<uint32_t> extra;
        function_template_overloads(held, extra);
        for (uint32_t g : extra) {
            bool present = false;
            for (uint32_t existing : group)
                if (function_templates[existing - 1].entity ==
                    function_templates[g - 1].entity)
                    present = true;
            if (!present)
                group.append(g);
        }
    }
    owned_array<entity_index> associated_classes, associated_spaces;
    if (!qualified &&
        !associated_namespaces(types, associated_classes, associated_spaces))
        return not_supported(n, "basic.lookup.argdep");
    owned_array<uint32_t> visible;
    for (uint32_t candidate : group) {
        const struct function_template &entry =
            function_templates[candidate - 1];
        bool found = !entry.hidden_friend;
        if (!qualified && entry.hidden_friend)
            for (entity_index cls : associated_classes)
                if (cls == entry.friend_class)
                    found = true;
        if (found)
            visible.append(candidate);
    }
    group = static_cast<owned_array<uint32_t> &&>(visible);
    /* the name's non-template functions, candidates unless the name is a
       template-id's ([temp.arg.explicit]: only templates take template
       arguments) */
    owned_array<entity_index> functions;
    bool template_id =
        tokens[name + 1].kind == token_kind::operator_or_punctuator &&
        preprocessing_op_or_punc_primary(tokens[name + 1].op_or_punc) ==
            preprocessing_op_or_punc::LESS;
    /* (the token after operator is the operator's own, operator<: a
       template-id only where written arguments follow it) */
    {
        size_t length = 0;
        const char *spelled = identifier_text(tokens.identifier(name), &length);
        if (length > 8 && memcmp(spelled, "operator", 8) == 0) {
            template_id = static_cast<bool>(written_at);
            /* [over.oper]/6: an operator function shall have a parameter of
               class or enumeration type; of a specialization that has none
               (clang: substitution failure at the use) not analysed */
            bool user_defined = false;
            for (const expression_meaning &e : early) {
                type_index t = referenced_type(e.type).unqualified();
                user_defined |= type_table[t].kind == type_kind::class_type ||
                                type_table[t].kind == type_kind::enumeration;
            }
            if (!user_defined)
                return not_supported(n, "over.oper");
        }
    }
    if (!template_id)
        inline_set_functions(function_templates[function_template - 1].name,
                             functions);
    /* [basic.lookup.argdep]/3: the non-template functions argument-dependent
       lookup found are candidates too */
    if (!template_id)
        for (entity_index e : others) {
            bool present = false;
            for (entity_index g : functions)
                if (g == e)
                    present = true;
            if (!present)
                functions.append(e);
        }
    entity_index specialization = 0;
    if (group.empty() && functions.empty())
        return ill_formed_at_token(name, "use of an undeclared identifier",
                                   "basic.lookup.unqual");
    if (!group.empty())
        function_template = group[0];
    if (group.size() > 1 || !functions.empty()) {
        /* [temp.over]/1: template_candidates */
        owned_array<overload_candidate> candidates;
        owned_array<uint32_t> first_arguments;
        owned_array<template_argument> all;
        function_candidates(functions, false, candidates, &first_arguments);
        const owned_array<SyntaxElement> *given_written = nullptr;
        const owned_array<bool> *given_expansion = nullptr;
        if (written_at) {
            given_written = &written;
            given_expansion = &written_expansion;
        }
        VTRY(added, template_candidates(
                        n, group, meanings, static_cast<uint32_t>(early.size()),
                        candidates, &first_arguments, &all, given_written,
                        written_at, nullptr, given_expansion));
        if (candidates.empty()) {
            /* [temp.deduct]/8: no viable candidate in an immediate context
               is a substitution failure */
            if (substituting)
                return analysis_result::ill_formed;
            /* (deduction failed for each: no viable function, which
               [impl.clang] clang reports with each template's reason) */
            return not_supported(n, "temp.over");
        }
        size_t chosen = 0;
        VTRY(r, best_viable_function(n, candidates, nullptr, early, chosen));
        if (!candidates[chosen].template_index) {
            /* a non-template function: [dcl.fct.def.delete]/2 "If a
               function is overloaded, it is referenced only if the
               function is selected by overload resolution." */
            entity_index c = candidates[chosen].function;
            VTRY_SET(r, deleted_use(name, c));
            overloaded_callee = name;
            r = named_entity(name, c, f);
            overloaded_callee = 0;
            return r;
        }
        const struct function_template ft =
            function_templates[candidates[chosen].template_index - 1];
        VTRY_SET(r, instantiate(n, ft.entity, &all[first_arguments[chosen]],
                                ft.parameter_count, specialization));
    } else {
        VTRY(r, deduce_from_call(n, function_template, meanings,
                                 static_cast<uint32_t>(early.size()), deduced));
        VTRY_SET(
            r, instantiate(n, function_templates[function_template - 1].entity,
                           &deduced[0], static_cast<uint32_t>(deduced.size()),
                           specialization));
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
    SyntaxRef n, token_index at, entity_index f,
    const owned_array<expression_meaning> &arguments, expression_meaning &m)
{
    VTRY(r, deleted_use(at, f));
    const entity &x = entity_table[f];
    expression_meaning callee;
    callee.type = x.type;
    callee.category = value_category::lvalue;
    callee.evaluation = constness::constant;
    callee.entity = f;
    callee.node = expression_node(semantic_kind::entity, callee, at, f);
    owned_array<SyntaxElement> syntax;
    for (size_t i = 0; i < arguments.size(); i++)
        syntax.append(SyntaxElement{});
    return call_with(n, callee, x.type, 0, 0, syntax, arguments, m);
}

analysis_result
semantic_analysis::call_with(SyntaxRef n, expression_meaning &f,
                             qualified_type function_type, uint32_t object,
                             entity_index member,
                             const owned_array<SyntaxElement> &arguments,
                             const owned_array<expression_meaning> &early,
                             expression_meaning &m, bool operator_expression)
{
    analysis_result r = analysis_result::well_formed;
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
        SyntaxElement a = arguments[i];
        initialized_value value;
        if (i < count) {
            /* [expr.call]/7: "When a function is called, each parameter is
               initialized with its corresponding argument." ([dcl.init]/15:
               copy-initialization) */
            qualified_type parameter =
                type_table.parameters(function_type.unqualified())[i];
            if (a.is(syntax_kind::braced_init_list)) {
                r = list_initialization(a, parameter, false, value);
            } else {
                expression_meaning e;
                if (i < early.size() && early[i].node)
                    e = early[i];
                else
                    /* [over.over]/1: "a parameter of a function" */
                    r = passed_target_expression(a, parameter, e);
                /* an argument that is one token is reported at the call */
                SyntaxRef at = n;
                if (a.is_node())
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
        if (a.is(syntax_kind::braced_init_list))
            return not_supported(n, "expr.call");
        expression_meaning e;
        if (i < early.size() && early[i].node)
            e = early[i];
        else
            r = passed_expression(a, e);
        if (r == analysis_result::well_formed)
            VTRY_SET(r, operand_value(node_of(a), e));
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
    for (size_t i = arguments.size(); i < count; i++) {
        uint32_t used =
            default_argument_nodes[entity_table[called].first_default + i];
        default_argument_used(used);
        argument_nodes.append(bind_call_site(used, n.first_token()));
    }
    if (called && bit_count_builtin(called)) {
        VTRY_SET(r, bit_count_arguments(n, called, argument_nodes));
    }
    /* [expr.call]/14: "A function call is an lvalue if the result type is
       an lvalue reference type or an rvalue reference to function type, an
       xvalue if the result type is an rvalue reference to object type, and
       a prvalue otherwise." */
    m = expression_meaning();
    qualified_type result_type = function.operand;
    /* [ext:gnu_library_builtins] the call's type is by its arguments */
    if (called && entity_table[called].gnu_builtin_row &&
        gnu_builtin_untyped(entity_table[called].gnu_builtin_row))
        VTRY_SET(r,
                 atomic_builtin_result(n, called, argument_nodes, result_type));
    call_result(result_type, m);
    /* [expr.const]/5.2: "an invocation of a non-constexpr function"; a
       constexpr function's invocation is the evaluator's
       (evaluate_constant), and a pointer or reference may be to a function
       analysis has no declaration of */
    m.evaluation = constness::unknown;
    /* (a closure's function call operator may be constexpr without the
       specifier: [expr.prim.lambda.closure]/4, evaluate_invocation) */
    if (called && !entity_table[called].constexpr_ &&
        !(entity_table[called].enclosing &&
          entity_table[entity_table[called].enclosing].lambda) &&
        !(entity_table[called].gnu_builtin_row &&
          gnu_builtin_at(entity_table[called].gnu_builtin_row).fold !=
              builtin_fold::none) &&
        /* (the evaluator answers __builtin_is_constant_evaluated) */
        !(entity_table[called].gnu_flags & gnu_is_constant_evaluated))
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
        callee = expression_node(semantic_kind::entity, named, n.first_token(),
                                 member);
    }
    /* [class.virtual]/16: "Explicit qualification with the scope operator
       ([expr.prim.id.qual]) suppresses the virtual call mechanism" */
    bool dispatched = false;
    if (member && entity_table[member].virtual_) {
        if (!n.size())
            return not_supported(n, "class.virtual");
        SyntaxElement name = n[0];
        if (!name.is_token() && name.is(syntax_kind::postfix_expression) &&
            name.size() == 3 &&
            (name[1].is(preprocessing_op_or_punc::DOT,
                        preprocessing_op_or_punc::ARROW)))
            name = name[2];
        if (operator_expression)
            /* [over.match.oper]/2: the expression is interpreted as the
               call of the member x.operator@(y), a call of an
               unqualified name */
            dispatched = true;
        else if (name.is(syntax_kind::operator_function_id))
            dispatched = true;
        else if (name.is_token() && name.is_identifier())
            dispatched = true;
        else if (!name.is(syntax_kind::qualified_id))
            return not_supported(n, "class.virtual");
    }
    if (called) {
        VTRY(dq,
             diagnose_if_call(n.first_token(), called, argument_nodes.data(),
                              argument_nodes.size()));
    }
    m.node = expression_node(semantic_kind::call, m, n.first_token(), callee,
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
analysis_result semantic_analysis::immediate_invocation(SyntaxRef n,
                                                        expression_meaning &m)
{
    if (unevaluated || (function_state.function &&
                        entity_table[function_state.function].immediate))
        return analysis_result::well_formed;
    constant_value v;
    bool constant = false;
    VTRY(r, evaluate_constant(n, m.node, v, constant));
    if (!constant) {
        /* a lambda's function call operator is immediate-escalating
           (P2564R3, a defect report clang applies to every edition): not
           analysed */
        entity_index owner = function_state.function;
        if (owner && entity_table[owner].enclosing &&
            entity_table[entity_table[owner].enclosing].lambda)
            return not_supported(n, "expr.const");
        return ill_formed(n,
                          "an immediate invocation shall be a constant "
                          "expression",
                          "expr.const");
    }
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
