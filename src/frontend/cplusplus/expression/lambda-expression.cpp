/*
 * lambda-expression.cpp: [expr.prim.lambda] Lambda expressions, with
 * [expr.prim.lambda.capture].
 *
 *     lambda-expression:
 *         lambda-introducer lambda-declarator_opt compound-statement
 *         lambda-introducer < template-parameter-list > requires-clause_opt lambda-declarator_opt compound-statement
 *
 *     lambda-introducer:
 *         [ lambda-capture_opt ]
 *
 *     lambda-declarator:
 *         ( parameter-declaration-clause ) decl-specifier-seq_opt
 *             noexcept-specifier_opt attribute-specifier-seq_opt trailing-return-type_opt requires-clause_opt
 *
 *     lambda-capture:
 *         capture-default
 *         capture-list
 *         capture-default , capture-list
 *
 *     capture-default:
 *         &
 *         =
 *
 *     capture-list:
 *         capture
 *         capture-list , capture
 *
 *     capture:
 *         simple-capture
 *         init-capture
 *
 *     simple-capture:
 *         identifier ..._opt
 *         & identifier ..._opt
 *         this
 *         * this
 *
 *     init-capture:
 *         ..._opt identifier initializer
 *         & ..._opt identifier initializer
 *
 * The template-parameter-list, the requires-clause and the packs (...) are
 * P7's.  Lambdas are C++11's (N2927), init-captures C++14's (N3648),
 * * this C++17's (P0018R3) and constexpr in the lambda-declarator C++17's
 * (P0170R1); in an earlier edition each is clang's extension, P9's, as is
 * C++23's lambda-declarator without its parameters (P1102R2: [] mutable
 * { }).
 *
 * Scopes: [basic.scope.param]/1, "A function parameter (including one
 * appearing in a lambda-declarator) ... has function parameter scope", so
 * the parameters are seen in the compound-statement as a function's; and
 * [expr.prim.lambda.capture]/6: an init-capture "behaves as if it declares
 * and explicitly captures a variable of the form "auto init-capture ;"
 * whose declarative region is the lambda-expression's compound-statement",
 * so the init-captures are declared in a scope around the parameters and
 * the compound-statement.  An init-capture's initializer is the enclosing
 * context's ([expr.prim.lambda.capture]/6: "the initializer ... is
 * evaluated in the context where the lambda-expression appears"), so the
 * name is declared after it.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::lambda_expression()
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    const implementation_defined_behavior &b = tokens.files.behavior;
    if (!b.has(language_feature::lambda_expressions))
        return beyond_this_step();
    size_t mark = begin_node();
    token_index first = position;
    declarative_region captures(names, scope_kind::block);
    lambda_introducer();
    if (failed())
        return 0;
    /* lambda-introducer < template-parameter-list > requires-clause_opt
       lambda-declarator compound-statement (P0428R2, C++20) */
    declarative_region template_parameters(names,
                                           scope_kind::template_parameter);
    if (at(preprocessing_op_or_punc::LESS)) {
        if (!b.has(language_feature::lambda_template_parameters))
            return beyond_this_step();
        add_token();
        {
            flag_set closing(greater_is_operator, false);
            if (at_closing_angle())
                return expected("a template-parameter", "expr.prim.lambda");
            template_parameter_list();
            if (failed())
                return 0;
        }
        if (!accept_closing_angle())
            return expected("'>' to end the template-parameter-list",
                            "expr.prim.lambda");
        if (at(standard_keyword::requires_)) {
            requires_clause();
            if (failed())
                return 0;
        }
    }
    scope_index enclosing = names.current();
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        scope_index parameters = 0;
        lambda_declarator(&parameters);
        if (failed())
            return 0;
        names.reenter(parameters);
    } else if (!at(preprocessing_op_or_punc::LEFT_BRACE)) {
        /* P1102R2 (C++23): decl-specifiers, a noexcept-specifier,
           attributes or a trailing-return-type with no parameters */
        return beyond_this_step();
    }
    if (!at(preprocessing_op_or_punc::LEFT_BRACE)) {
        names.reenter(enclosing);
        return expected("'{' to begin the lambda's compound-statement",
                        "expr.prim.lambda");
    }
    compound_statement();
    names.reenter(enclosing);
    if (failed())
        return 0;
    return finish_node(syntax_kind::lambda_expression, mark, first);
}

node_index parser::lambda_introducer()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* [ */
    if (!at(preprocessing_op_or_punc::RIGHT_BRACKET)) {
        lambda_capture();
        if (failed())
            return 0;
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET))
        return expected("']' to end the lambda-introducer", "expr.prim.lambda");
    return finish_node(syntax_kind::lambda_introducer, mark, first);
}

/*
 * A capture-default is & or = alone: & before an identifier is a
 * simple-capture's or init-capture's.  One capture alone passes through,
 * and so does a capture-list alone.
 */
node_index parser::lambda_capture()
{
    size_t mark = begin_node();
    token_index first = position;
    bool capture_default = false;
    if (at(preprocessing_op_or_punc::EQUAL) ||
        (at(preprocessing_op_or_punc::AMPERSAND) &&
         (at(preprocessing_op_or_punc::COMMA, 1) ||
          at(preprocessing_op_or_punc::RIGHT_BRACKET, 1)))) {
        add_token();
        capture_default = true;
        if (!accept(preprocessing_op_or_punc::COMMA))
            return 0;
    }
    size_t list_mark = begin_node();
    token_index list_first = position;
    unsigned count = 0;
    for (;;) {
        capture();
        if (failed())
            return 0;
        count++;
        if (!accept(preprocessing_op_or_punc::COMMA))
            break;
    }
    if (count > 1)
        finish_node(syntax_kind::capture_list, list_mark, list_first);
    if (!capture_default)
        return 0;
    return finish_node(syntax_kind::lambda_capture, mark, first);
}

/*
 * Which capture it is follows from its first tokens: this, * this, & or an
 * identifier, then an initializer for an init-capture (=, ( or {).  An
 * identifier alone passes through.
 */
node_index parser::capture()
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    size_t mark = begin_node();
    token_index first = position;
    if (at(standard_keyword::this_)) {
        add_token();
        return 0;
    }
    if (at(preprocessing_op_or_punc::STAR) && at(standard_keyword::this_, 1)) {
        if (!b.has(language_feature::star_this_capture))
            return beyond_this_step();
        add_token();
        add_token();
        return finish_node(syntax_kind::simple_capture, mark, first);
    }
    bool reference = accept(preprocessing_op_or_punc::AMPERSAND);
    /* init-capture: &_opt ... identifier initializer (P0780R2, C++20) */
    bool init_pack = false;
    if (at(preprocessing_op_or_punc::ELLIPSIS)) {
        if (!b.has(language_feature::init_capture_packs))
            return beyond_this_step();
        add_token();
        init_pack = true;
    }
    if (!at_identifier())
        return expected("a capture", "expr.prim.lambda.capture");
    uint32_t name = tokens.identifier(position);
    add_token();
    /* simple-capture ... : a pack expansion ([temp.variadic]/5) */
    if (!init_pack && at(preprocessing_op_or_punc::ELLIPSIS)) {
        add_token();
        return finish_node(syntax_kind::simple_capture, mark, first);
    }
    if (init_pack && !at(preprocessing_op_or_punc::EQUAL) &&
        !at(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
        !at(preprocessing_op_or_punc::LEFT_BRACE))
        return expected("an initializer after the init-capture pack",
                        "expr.prim.lambda.capture");
    if (at(preprocessing_op_or_punc::EQUAL) ||
        at(preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
        at(preprocessing_op_or_punc::LEFT_BRACE)) {
        if (!b.has(language_feature::init_captures))
            return beyond_this_step();
        initializer();
        if (failed())
            return 0;
        names.declare(name, name_kind::other);
        return finish_node(syntax_kind::init_capture, mark, first);
    }
    if (!reference)
        return 0;
    return finish_node(syntax_kind::simple_capture, mark, first);
}

/*
 * [expr.prim.lambda]/3: "In the decl-specifier-seq of the
 * lambda-declarator, each decl-specifier shall be one of mutable,
 * constexpr, or consteval."  A C++11 or C++14 lambda-declarator has an
 * exception-specification where C++20's has the noexcept-specifier
 * (noexcept-specifier.cpp reads both).  *parameters: the function parameter
 * scope, which the compound-statement continues.
 */
node_index parser::lambda_declarator(scope_index *parameters)
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* ( */
    scope_index enclosing = names.current();
    *parameters = names.enter(scope_kind::function_prototype);
    {
        /* [temp.res]/5: a parameter-declaration in a lambda-declarator */
        flag_set implicit(parameters_implicit_typename, true);
        parameter_declaration_clause();
    }
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to close the lambda's parameters",
                        "expr.prim.lambda");
    /* [ext:gnu_attributes] right after the parameters, as clang */
    gnu_attributes_opt();
    if (failed())
        return 0;
    size_t specifiers_mark = begin_node();
    token_index specifiers_first = position;
    unsigned specifiers = 0;
    /* [dcl.spec]/2: "each decl-specifier shall appear at most once in a
       complete decl-specifier-seq" */
    bool seen_mutable = false, seen_constexpr = false, seen_consteval = false;
    for (;;) {
        bool *seen = nullptr;
        if (at(standard_keyword::mutable_)) {
            seen = &seen_mutable;
        } else if (at(standard_keyword::consteval_)) {
            seen = &seen_consteval;
        } else if (at(standard_keyword::constexpr_)) {
            if (!b.has(language_feature::constexpr_lambdas))
                return beyond_this_step();
            seen = &seen_constexpr;
        } else if (at(standard_keyword::static_)) {
            return beyond_this_step(); /* P1169R4 (C++23): static */
        } else {
            break;
        }
        if (*seen)
            return violates("each decl-specifier at most once", "dcl.spec",
                            position);
        *seen = true;
        add_token();
        specifiers++;
    }
    if (specifiers > 1)
        finish_node(syntax_kind::decl_specifier_seq, specifiers_mark,
                    specifiers_first);
    if (at(standard_keyword::noexcept_))
        noexcept_specifier();
    else if (at(standard_keyword::throw_))
        dynamic_exception_specification();
    if (failed())
        return 0;
    attribute_specifier_seq(true);
    if (failed())
        return 0;
    if (at(preprocessing_op_or_punc::ARROW)) {
        trailing_return_type();
        if (failed())
            return 0;
    }
    if (at(standard_keyword::requires_)) {
        requires_clause();
        if (failed())
            return 0;
    }
    names.reenter(enclosing);
    return finish_node(syntax_kind::lambda_declarator, mark, first);
}

/* ---- semantic analysis ---------------------------------------------- */

semantic_analysis::lambda_context *semantic_analysis::current_lambda()
{
    if (lambdas.empty() || !function_state.active ||
        function_state.function != lambdas.back().call)
        return nullptr;
    return &lambdas.back();
}

/*
 * [expr.prim.lambda]/2: "A lambda-expression is a prvalue whose result
 * object is called the closure object."  [expr.prim.lambda.closure]/1:
 * "The type of a lambda-expression (which is also the type of the closure
 * object) is a unique, unnamed non-union class type, called the closure
 * type"; /2: "The closure type is declared in the smallest block scope,
 * class scope, or namespace scope that contains the corresponding
 * lambda-expression."
 */
analysis_result semantic_analysis::lambda_expression(LambdaExpression n,
                                                     expression_meaning &m)
{
    /* [expr.prim.lambda.closure]/1 (C++20): a lambda-expression may be in
       an unevaluated operand; its compound-statement is no unevaluated
       operand, the function call operator being a function of its own */
    unsigned outer_unevaluated = unevaluated;
    unevaluated = 0;
    analysis_result r = lambda_expression_analysed(n, m);
    unevaluated = outer_unevaluated;
    return r;
}

analysis_result
semantic_analysis::lambda_expression_analysed(LambdaExpression n,
                                              expression_meaning &m)
{
    /* a lambda in a default argument or member initializer or while a
       candidate template is substituted; outside a function body only at
       namespace scope */
    /* ([temp.deduct]/8: a function body analysed to deduce a return type is
       no immediate context: its lambdas are made) */
    /* a default argument of an inline function (a member defined in its
       class): the closure type is named in the function's scope, which
       the other kinds are not analysed for */
    bool in_default =
        !function_state.active && default_argument_function &&
        entity_table[default_argument_function].inline_ &&
        entity_table[entity_table[default_argument_function].enclosing].kind ==
            entity_kind::class_type;
    if ((!function_state.active && !is_namespace(current_scope) &&
         !in_default) ||
        (substituting && !function_state.active) || n.size() < 2)
        return not_supported(n, "expr.prim.lambda");
    /* [expr.prim.lambda]/5: a template-parameter-list makes a generic
       lambda, whose call operator is a template
       ([expr.prim.lambda.closure]/3); its requires-clause is the
       template's (lambda_constraint) */
    SyntaxElement declarator, template_list;
    bool has_placeholder = false;
    for (uint32_t i = 1; i + 1 < n.size(); i++) {
        SyntaxElement x = n[i];
        if (x.is(syntax_kind::lambda_declarator) && !declarator)
            declarator = x;
        else if (x.is(syntax_kind::template_parameter_list,
                      syntax_kind::type_parameter,
                      syntax_kind::parameter_declaration))
            /* (a list of one template-parameter is that parameter) */
            template_list = x;
        else if (!x.is_token() && !x.is(syntax_kind::requires_clause))
            return not_supported(n, "expr.prim.lambda");
    }
    SyntaxElement body = n.compound_statement();
    if (!n.lambda_introducer() || !body)
        return not_supported(n, "expr.prim.lambda");
    if (declarator) {
        /* [expr.prim.lambda]/5: "A lambda is a generic lambda if the
           lambda-expression has any generic parameter type placeholders"
           (auto in a parameter's decl-specifier-seq) */
        owned_array<SyntaxRef> placeholders;
        if (!abbreviated_clause(declarator, placeholders))
            return not_supported(n, "expr.prim.lambda.closure");
        has_placeholder = !placeholders.empty();
        /* a requires-clause after the parameters: a generic lambda's,
           (lambda_constraint) */
        if (!template_list && !has_placeholder &&
            declarator[declarator.size() - 1].is(syntax_kind::requires_clause))
            return not_supported(n, "temp.constr");
    }

    lambda_context l;
    l.at = n;
    l.enclosing_function = function_state.function;
    /* the type of this where the lambda-expression is: in an enclosing
       lambda's body, the one it captured (a copy of *this is that
       lambda's member, const unless it is mutable) */
    if (lambda_context *outer = current_lambda()) {
        l.enclosing_this = outer->enclosing_this;
        for (size_t i = outer->first_capture; i < lambda_captures.size(); i++)
            if (lambda_captures[i].star_this) {
                qualified_type object =
                    type_table[outer->enclosing_this.unqualified()].operand;
                uint32_t cv = object.cv();
                if (!outer->mutable_)
                    cv |= cv_const;
                l.enclosing_this = qualified_type::make(
                    type_table.pointer_to(
                        qualified_type::make(object.unqualified(), cv)),
                    cv_none);
            }
    } else if (function_state.this_entity) {
        l.enclosing_this = entity_table[function_state.this_entity].type;
    }

    /* the closure type, a local class with no name */
    entity_index scope = current_scope;
    entity_index member = 0;
    /* [abi:closure-types] a closure type in a default member initializer
       belongs to the data member, whose class is its scope */
    uint32_t default_parameter = 0;
    if (in_default && !current_lambda()) {
        scope = default_argument_function;
        /* the number of the closure among those of the function's default
           arguments ([abi:closure-types]: d [<number>] _, number - 2 for
           the second) */
        default_parameter = 1;
        for (entity_index k = 1; k < entity_table.size(); k++)
            if (entity_table[k].kind == entity_kind::class_type &&
                entity_table[k].enclosing == scope &&
                entity_table[k].default_argument_parameter)
                default_parameter++;
    } else if (initialized_member && !current_lambda()) {
        member = initialized_member;
        scope = entity_table[member].enclosing;
    }
    if (member)
        for (const member_closure &k : member_closures)
            if (k.lambda == n.index() && k.member == member) {
                l.closure = k.closure;
                owned_array<capture_record> again;
                for (uint32_t i = 0; i < k.capture_count; i++)
                    again.append(member_closure_captures[k.first_capture + i]);
                return closure_object(l, again, m);
            }
    entity_index c = entity_table.make(entity_kind::class_type, 0, scope);
    entity_table[c].closure_variable = member;
    entity_table[c].default_argument_parameter = default_parameter;
    entity_table[c].type = qualified_type::make(
        type_table.of_entity(type_kind::class_type, c), cv_none);
    entity_table[c].lambda = n;
    if (!function_state.active && deducing_variable &&
        entity_table[deducing_variable].inline_)
        entity_table[c].closure_variable = deducing_variable;
    entity_table[c].defined = true;
    entity_table[c].first_member = static_cast<uint32_t>(class_members.size());
    entity_table[c].first_base = static_cast<uint32_t>(class_bases.size());
    entity_table.add_declaration(c, n);
    l.closure = c;
    l.first_capture = static_cast<uint32_t>(lambda_captures.size());
    analysis_result r = lambda_introducer(n.lambda_introducer(), l);
    if (r != analysis_result::well_formed) {
        lambda_captures.resize(l.first_capture);
        entity_table[c].kind = entity_kind::none;
        return r;
    }
    bool has_capture = n.lambda_introducer().size() > 2;

    qualified_type type;
    owned_array<parameter_meaning> parameters;
    bool generic = template_list || has_placeholder;
    if (generic)
        r = generic_lambda(n, template_list, declarator, l, has_capture, m);
    else
        r = lambda_call_type(n, declarator, l, type, parameters);
    if (r != analysis_result::well_formed || generic) {
        lambda_captures.resize(l.first_capture);
        if (r != analysis_result::well_formed)
            entity_table[c].kind = entity_kind::none;
        return r;
    }

    /* [expr.prim.lambda.closure]/3: "The closure type for a
       lambda-expression has a public inline function call operator (for a
       non-generic lambda) ... whose parameters and return type are
       described by the lambda-expression's parameter-declaration-clause
       and trailing-return-type respectively"; /4: "The function call
       operator or operator template is declared const
       ([class.mfct.non-static]) if and only if the lambda-expression's
       parameter-declaration-clause is not followed by mutable." */
    uint32_t call_name =
        operator_name(preprocessing_op_or_punc::LEFT_PARENTHESIS);
    entity_index f = entity_table.make(entity_kind::function, call_name, c);
    entity_table[f].type = type;
    entity_table[f].nonstatic_member = true;
    entity_table[f].member_cv = cv_const;
    if (l.mutable_)
        entity_table[f].member_cv = cv_none;
    entity_table[f].inline_ = true;
    entity_table[f].constexpr_ = l.constexpr_;
    entity_table[f].immediate = l.immediate;
    entity_table[f].defined = true;
    entity_table[f].operator_function = true;
    entity_table[f].implemented = preprocessing_op_or_punc::LEFT_PARENTHESIS;
    entity_table[f].placeholder_return =
        contains_placeholder(type_table[type.unqualified()].operand);
    entity_table.add_declaration(f, n);
    entity_table.bind(c, call_name, f);
    entity_table[c].call_operator = f;
    l.call = f;

    owned_array<capture_record> captures;
    r = lambda_body(n, body, l, parameters, captures);
    if (r == analysis_result::well_formed)
        r = closure_class(c, n, has_capture);
    if (r != analysis_result::well_formed) {
        entity_table[c].kind = entity_kind::none;
        entity_table[c].size = 0;
        return r;
    }
    if (member) {
        member_closure k;
        k.lambda = n.index();
        k.closure = c;
        k.member = member;
        k.first_capture = static_cast<uint32_t>(member_closure_captures.size());
        k.capture_count = static_cast<uint32_t>(captures.size());
        for (const capture_record &x : captures)
            member_closure_captures.append(x);
        member_closures.append(k);
    }
    return closure_object(l, captures, m);
}

/*
 * The function call operator's type and parameters from the
 * lambda-declarator d of lambda-expression n (0: none), in the current
 * scope: a generic lambda's template-parameters' or a specialization's.
 */
analysis_result
semantic_analysis::lambda_call_type(SyntaxRef n, SyntaxRef d, lambda_context &l,
                                    qualified_type &type,
                                    owned_array<parameter_meaning> &parameters)
{
    /* [expr.prim.lambda]/4: "If a lambda-expression does not include a
       lambda-declarator, it is as if the lambda-declarator were ().  The
       lambda return type is auto, which is replaced by the type specified
       by the trailing-return-type if provided and/or deduced from return
       statements as described in [dcl.spec.auto]." */
    lambda_enclosing_state saved;
    saved.parameters =
        static_cast<owned_array<parameter_meaning> &&>(function_parameters);
    function_parameters = owned_array<parameter_meaning>();
    saved.function_empty_pack = function_empty_pack;
    saved.function_cv = function_cv;
    function_empty_pack = 0;
    analysis_result r = analysis_result::well_formed;
    qualified_type returns = placeholder_type(false);
    type = returns;
    if (d) {
        bool closed = false;
        for (uint32_t i = 0; i < d.size() && r == analysis_result::well_formed;
             i++) {
            SyntaxElement part = d[i];
            if (part.is(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
                closed = true;
                continue;
            }
            if (!closed)
                continue;
            owned_array<SyntaxElement> specifiers;
            if (part.is(syntax_kind::decl_specifier_seq)) {
                for (auto child : part.elements())
                    specifiers.append(child);
            } else if (part.is(syntax_kind::gnu_attribute_specifier)) {
                /* [ext:gnu_attributes] always_inline and the like: no
                   effect on the closure type */
                gnu_attributes g;
                analysis_result a = gnu_attribute_specifier(part, g);
                if (a != analysis_result::well_formed)
                    r = a;
            } else if (part.is_token()) {
                specifiers.append(part);
            }
            for (SyntaxElement sp : specifiers) {
                standard_keyword kw = standard_keyword::none;
                if (sp.is_token())
                    kw = sp.keyword();
                if (kw == standard_keyword::mutable_)
                    l.mutable_ = true;
                else if (kw == standard_keyword::constexpr_)
                    /* [expr.prim.lambda.closure]/4: "The function call
                       operator or any given operator template
                       specialization is a constexpr function if either the
                       corresponding lambda-expression's
                       parameter-declaration-clause is followed by
                       constexpr or consteval, or it satisfies the
                       requirements for a constexpr function" */
                    l.constexpr_ = true;
                else if (kw == standard_keyword::consteval_) {
                    /* [dcl.constexpr]/2: consteval implies constexpr; an
                       immediate function ([expr.const]/13) */
                    l.constexpr_ = true;
                    l.immediate = true;
                } else {
                    r = not_supported(n, "dcl.constexpr");
                }
            }
            if (part.is(syntax_kind::trailing_return_type)) {
                if (part.size() != 2 || !part[1].is(syntax_kind::type_id)) {
                    r = not_supported(n, "dcl.fct");
                    continue;
                }
                /* an init-capture's name in the trailing-return-type:
                   [expr.prim.lambda.capture]/6 declares it in the
                   compound-statement, clang finds it in the declarator
                   too */
                for (size_t i = l.first_capture; i < lambda_captures.size();
                     i++)
                    if (lambda_captures[i].init_capture)
                        r = not_supported(n, "expr.prim.lambda.capture");
                if (r != analysis_result::well_formed)
                    continue;
                r = type_id(part[1], returns);
                if (r == analysis_result::well_formed &&
                    abstract_class(returns))
                    r = not_supported(n, "class.abstract");
            }
        }
        type = returns;
        in_member_declarator = false;
        if (r == analysis_result::well_formed)
            r = function(d, type);
    } else {
        type = qualified_type::make(
            type_table.function(returns, nullptr, 0, false, false), cv_none);
    }
    parameters =
        static_cast<owned_array<parameter_meaning> &&>(function_parameters);
    function_parameters =
        static_cast<owned_array<parameter_meaning> &&>(saved.parameters);
    function_empty_pack = saved.function_empty_pack;
    function_cv = saved.function_cv;
    for (const parameter_meaning &parameter : parameters) {
        if (r != analysis_result::well_formed)
            break;
        /* a default argument ([dcl.fct.default]) */
        if (parameter.default_argument)
            r = not_supported(n, "dcl.fct.default");
        /* [expr.prim.lambda.capture]/5: "If an identifier in a
           simple-capture appears as the declarator-id of a parameter of the
           lambda-declarator's parameter-declaration-clause, the program is
           ill-formed." */
        for (size_t i = l.first_capture; i < lambda_captures.size(); i++)
            if (parameter.id && lambda_captures[i].variable &&
                !lambda_captures[i].init_capture &&
                entity_table[lambda_captures[i].variable].name ==
                    tokens.identifier(parameter.id))
                r = ill_formed_at_token(parameter.id,
                                        "an identifier in a simple-capture "
                                        "shall not appear as the "
                                        "declarator-id of a parameter of the "
                                        "lambda-declarator",
                                        "expr.prim.lambda.capture");
    }
    return r;
}

/*
 * The compound-statement body of lambda-expression n, the function call
 * operator l.call's body ([expr.prim.lambda.closure]/12), with its
 * parameters; the captures it made appended to captures.
 */
analysis_result
semantic_analysis::lambda_body(SyntaxRef n, SyntaxElement body,
                               lambda_context &l,
                               owned_array<parameter_meaning> &parameters,
                               owned_array<capture_record> &captures)
{
    /* the compound-statement, the function call operator's body
       ([expr.prim.lambda.closure]/12) */
    lambda_enclosing_state saved;
    saved.function = static_cast<function_context &&>(function_state);
    function_state = function_context();
    saved.block_variables =
        static_cast<owned_array<entity_index> &&>(block_variables);
    block_variables = owned_array<entity_index>();
    saved.scope = current_scope;
    saved.last_declared = last_declared;
    saved.declaration = current_declaration;
    saved.claim_place = claim_place;
    saved.claim_scope = claim_scope;
    saved.declaration_deduced = declaration_deduced;
    saved.placeholder_group = placeholder_group;
    saved.deducing_variable = deducing_variable;
    saved.in_condition = in_condition;
    declaration_deduced = qualified_type();
    placeholder_group = false;
    deducing_variable = 0;
    in_condition = false;
    saved.parameters =
        static_cast<owned_array<parameter_meaning> &&>(function_parameters);
    function_parameters =
        static_cast<owned_array<parameter_meaning> &&>(parameters);
    saved.function_cv = function_cv;
    saved.function_empty_pack = function_empty_pack;
    function_empty_pack = 0;
    last_declared = l.call;
    lambdas.append(l);
    analysis_result r = analyse_function_body(n, body);
    if (r == analysis_result::well_formed)
        r = resolve_pending_uses(lambdas.back());
    else
        pending_uses.resize(0);
    /* the captures the body made, and the lambda off the stack (an
       enclosing lambda's captures go on after its own) */
    l = lambdas.back();
    lambdas.resize(lambdas.size() - 1);
    for (size_t i = l.first_capture; i < lambda_captures.size(); i++)
        captures.append(lambda_captures[i]);
    lambda_captures.resize(l.first_capture);
    function_parameters =
        static_cast<owned_array<parameter_meaning> &&>(saved.parameters);
    function_state = static_cast<function_context &&>(saved.function);
    block_variables =
        static_cast<owned_array<entity_index> &&>(saved.block_variables);
    current_scope = saved.scope;
    last_declared = saved.last_declared;
    current_declaration = saved.declaration;
    claim_place = saved.claim_place;
    claim_scope = saved.claim_scope;
    declaration_deduced = saved.declaration_deduced;
    placeholder_group = saved.placeholder_group;
    deducing_variable = saved.deducing_variable;
    in_condition = saved.in_condition;
    function_cv = saved.function_cv;
    function_empty_pack = saved.function_empty_pack;
    return r;
}

/*
 * [expr.prim.lambda.closure]/3: "For a generic lambda, the closure type
 * has a public inline function call operator member template
 * ([temp.mem]) whose template-parameter-list consists of the specified
 * template-parameter-list, if any, to which is appended one invented type
 * template-parameter for each occurrence of a generic parameter type
 * placeholder in the lambda's parameter-declaration-clause, in order of
 * appearance."  Here with a template-parameter-list and no placeholder:
 * the template's record is a member function template's (member-template.cpp),
 * whose definition is the lambda-expression; a specialization's
 * declaration and body are the lambda-declarator and compound-statement
 * analysed again (instantiate, instantiate_definition).  Without a
 * lambda-capture only: the captures an instantiation's body makes would
 * change the closure type after its use.
 */
analysis_result semantic_analysis::generic_lambda(SyntaxRef n, SyntaxRef list,
                                                  SyntaxRef declarator,
                                                  lambda_context &l,
                                                  bool has_capture,
                                                  expression_meaning &m)
{
    entity_index c = l.closure;
    owned_array<SyntaxElement> list_items;
    if (!list)
        ;
    else if (list.is(syntax_kind::template_parameter_list))
        for (uint32_t i = 0; i < list.size(); i++)
            list_items.append(list[i]);
    else
        list_items.append(list);
    owned_array<SyntaxRef> parameters;
    for (SyntaxElement p : list_items) {
        if (p.is_token())
            continue;
        parameters.append(p);
    }
    /* [expr.prim.lambda.closure]/3: "to which is appended one invented
       type template-parameter for each occurrence of a generic parameter
       type placeholder in the lambda's parameter-declaration-clause, in
       order of appearance" */
    if (declarator && !abbreviated_clause(declarator, parameters))
        return not_supported(n, "temp.variadic");
    VTRY(valid, validate_template_parameters(n, parameters, true, true, true));
    uint32_t clauses = 0;
    for (uint32_t i = 1; i + 1 < n.size(); i++)
        clauses += n[i].is(syntax_kind::requires_clause);
    if (clauses && declarator &&
        declarator[declarator.size() - 1].is(syntax_kind::requires_clause))
        return not_supported(n, "temp.constr");
    uint32_t index = 0, first = 0;
    entity_index scope = 0;
    VTRY(r, template_parameter_scope(n, parameters, c, 0, index, scope, first));
    /* the template's declarator, in the template-parameters' scope; what
       it records for claims is the template's, which clang has none of */
    size_t expressions_before = expressions.size();
    size_t uses_before = name_uses.size();
    entity_index outer = current_scope;
    current_scope = scope;
    qualified_type type;
    owned_array<parameter_meaning> call_parameters;
    r = lambda_call_type(n, declarator, l, type, call_parameters);
    current_scope = outer;
    expressions.resize(expressions_before);
    name_uses.resize(uses_before);
    if (r != analysis_result::well_formed)
        return r;
    /* the body is analysed once per specialization, after the closure
       type is complete: the captures a capture-default makes are those
       of the names the body has (implicit_captures) */
    if (r == analysis_result::well_formed)
        r = implicit_captures(n, l, call_parameters);
    if (r != analysis_result::well_formed)
        return r;
    uint32_t call_name =
        operator_name(preprocessing_op_or_punc::LEFT_PARENTHESIS);
    function_template record =
        new_template(call_name, c, n, index, first,
                     static_cast<uint32_t>(parameters.size()));
    record.definition = n;
    record.declarator = n;
    entity_index t = record.entity;
    entity_table[t].type = type;
    entity_table[t].defined = true;
    entity_table[t].linkage_of_name = entity_table[c].linkage_of_name;
    entity_table[t].nonstatic_member = true;
    entity_table[t].member_cv = cv_const;
    if (l.mutable_)
        entity_table[t].member_cv = cv_none;
    entity_table[t].inline_ = true;
    entity_table[t].constexpr_ = l.constexpr_;
    entity_table[t].immediate = l.immediate;
    entity_table[t].operator_function = true;
    entity_table[t].implemented = preprocessing_op_or_punc::LEFT_PARENTHESIS;
    hold_template_name(record, call_name, c);
    entity_table[c].call_operator = t;
    owned_array<capture_record> captures;
    for (size_t i = l.first_capture; i < lambda_captures.size(); i++)
        captures.append(lambda_captures[i]);
    generic_closure g;
    g.closure = c;
    g.default_capture = l.default_capture;
    g.mutable_ = l.mutable_;
    g.enclosing_this = l.enclosing_this;
    g.enclosing_function = l.enclosing_function;
    g.first_capture = static_cast<uint32_t>(generic_closure_captures.size());
    g.capture_count = static_cast<uint32_t>(captures.size());
    for (const capture_record &x : captures)
        generic_closure_captures.append(x);
    generic_closures.append(g);
    VTRY_SET(r, closure_class(c, n, has_capture));
    return closure_object(l, captures, m);
}

/*
 * [expr.prim.lambda]/1: a generic lambda's requires-clause, after its
 * template-parameter-list or its lambda-declarator (generic_lambda
 * refuses both), or none
 */
SyntaxElement semantic_analysis::lambda_constraint(SyntaxRef n,
                                                   bool *trailing) const
{
    for (uint32_t i = 1; i + 1 < n.size(); i++) {
        SyntaxElement c = n[i];
        bool in_declarator = c.is(syntax_kind::lambda_declarator);
        if (in_declarator)
            c = c[c.size() - 1];
        if (!c.is(syntax_kind::requires_clause) || c.size() != 2)
            continue;
        if (trailing)
            *trailing = in_declarator;
        return c[1];
    }
    return SyntaxElement();
}

/* the invented template-parameters ([expr.prim.lambda.closure]/3) of
   lambda-expression n: one for each generic parameter type placeholder */
uint32_t semantic_analysis::invented_parameter_count(SyntaxRef n) const
{
    owned_array<SyntaxRef> placeholders;
    for (uint32_t i = 1; i + 1 < n.size(); i++)
        if (n[i].is(syntax_kind::lambda_declarator))
            abbreviated_clause(n[i], placeholders);
    return static_cast<uint32_t>(placeholders.size());
}

/*
 * A generic lambda's specialization (instantiate, instantiate_definition):
 * its declaration, the lambda-declarator in the current scope, where the
 * template-parameters' names are the arguments; with body, its
 * compound-statement too, as specialization's body.
 */
analysis_result semantic_analysis::lambda_specialization(
    SyntaxRef n, bool body, qualified_type &type, entity_index specialization)
{
    SyntaxRef declarator = 0;
    for (uint32_t i = 1; i + 1 < n.size(); i++)
        if (n[i].is(syntax_kind::lambda_declarator))
            declarator = n[i].index();
    lambda_context l;
    l.at = n;
    l.closure = entity_table[current_scope].enclosing;
    while (entity_table[l.closure].kind != entity_kind::class_type)
        l.closure = entity_table[l.closure].enclosing;
    l.call = specialization;
    l.first_capture = static_cast<uint32_t>(lambda_captures.size());
    /* the closure's captures, made where the lambda-expression is */
    uint32_t made = 0;
    for (const generic_closure &g : generic_closures)
        if (g.closure == l.closure) {
            l.default_capture = g.default_capture;
            l.mutable_ = g.mutable_;
            l.enclosing_this = g.enclosing_this;
            l.enclosing_function = g.enclosing_function;
            made = g.capture_count;
            for (uint32_t i = 0; i < g.capture_count; i++)
                lambda_captures.append(
                    generic_closure_captures[g.first_capture + i]);
        }
    l.sealed = true;
    owned_array<parameter_meaning> parameters;
    analysis_result r = lambda_call_type(n, declarator, l, type, parameters);
    if (r != analysis_result::well_formed || !body) {
        lambda_captures.resize(l.first_capture);
        return r;
    }
    owned_array<capture_record> captures;
    r = lambda_body(n, n[n.size() - 1], l, parameters, captures);
    /* (lambda_body took the captures off lambda_captures) */
    if (r == analysis_result::well_formed && captures.size() != made)
        return not_supported(n, "expr.prim.lambda.capture");
    return r;
}

} // namespace cplusplus
