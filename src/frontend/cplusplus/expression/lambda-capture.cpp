/*
 * lambda-capture.cpp: [expr.prim.lambda.capture] Captures: the
 * analysis of a lambda-introducer's captures (the grammar is
 * lambda-expression.cpp's) and of the names in a lambda's body that capture
 * an entity.
 *
 * Each entity captured is a non-static data member of the closure type
 * (/10, /12: one for a capture by reference too, as clang has), made when
 * the capture is: the explicit ones in the order of the lambda-capture,
 * then the implicit ones in the order of their first use in the body
 * (clang's order).  In the body a name of a captured entity, and this,
 * designate the member through the function call operator's this (/11);
 * the closure object's members are initialized where the
 * lambda-expression is (/15, closure_object), where a capture of an
 * enclosing lambda's entity captures it in that lambda too (/14).
 *
 * Not analysed: a pack expansion in a capture (/17), an implicit capture
 * of a variable usable in constant expressions or in an unevaluated
 * operand (whether it is an odr-use, [basic.def.odr]/4, decides whether it
 * is captured), an array or a function captured by copy, a reference to a
 * function captured.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* the captures of a lambda-introducer (its lambda-capture, capture-list or
   capture passed through), in order, the capture-default first */
analysis_result
semantic_analysis::lambda_captures_of(SyntaxRef n,
                                      owned_array<SyntaxElement> &out)
{
    for (auto c : n.elements()) {
        if (c.is(preprocessing_op_or_punc::COMMA,
                 preprocessing_op_or_punc::LEFT_BRACKET,
                 preprocessing_op_or_punc::RIGHT_BRACKET))
            continue;
        if (c.is(syntax_kind::lambda_capture, syntax_kind::capture_list)) {
            analysis_result r = lambda_captures_of(c, out);
            if (r != analysis_result::well_formed)
                return r;
            continue;
        }
        out.append(c);
    }
    return analysis_result::well_formed;
}

entity_index semantic_analysis::capture_member_of(entity_index closure,
                                                  qualified_type type)
{
    /* (a class defined in the body since added its own members) */
    make_members_contiguous(closure);
    entity_index e = entity_table.make(entity_kind::variable, 0, closure);
    entity_table[e].type = type;
    entity_table[e].nonstatic_member = true;
    entity_table[e].defined = true;
    entity_table[e].field = entity_table[closure].member_count;
    class_members.append(e);
    entity_table[closure].member_count++;
    return e;
}

/*
 * [expr.prim.lambda.capture]/10: "The type of such a data member is the
 * referenced type if the entity is a reference to an object, an lvalue
 * reference to the referenced function type if the entity is a reference
 * to a function, or the type of the corresponding captured entity
 * otherwise."  By reference (/12), a reference to the object.
 */
analysis_result semantic_analysis::capture_type(SyntaxRef at, qualified_type t,
                                                bool by_reference,
                                                qualified_type &type)
{
    const struct type r = type_table[t.unqualified()];
    qualified_type object = t;
    if (r.kind == type_kind::lvalue_reference ||
        r.kind == type_kind::rvalue_reference)
        object = r.operand;
    const struct type o = type_table[object.unqualified()];
    /* a function, or an array copied element by element (/15) */
    if (o.kind == type_kind::function ||
        (o.kind == type_kind::array && !by_reference))
        return not_supported(at, "expr.prim.lambda.capture");
    if (by_reference) {
        type = reference_to(object, false);
        return analysis_result::well_formed;
    }
    if (!size_of_type(object))
        return not_supported(at, "expr.prim.lambda.capture");
    type = object;
    return analysis_result::well_formed;
}

/*
 * The type of local entity e named where a lambda-expression is whose
 * enclosing lambdas are lambdas[0 .. count) (the innermost one's captures
 * end at end): e's own, or, if the nearest of them that does not capture
 * it by reference captures it by copy, that lambda's member, const unless
 * the lambda is mutable ([expr.prim.lambda.capture]/11: the name is
 * "transformed into an access to the corresponding unnamed data member of
 * the closure type"; [expr.prim.lambda.closure]/5: the function call
 * operator "is declared const if and only if the lambda-expression's
 * parameter-declaration-clause is not followed by mutable").  An
 * enclosing lambda with a capture-default = that does not capture it yet
 * will (/7).  A reference captured by reference then refers to that
 * member.
 */
qualified_type semantic_analysis::enclosing_capture_type(entity_index e,
                                                         size_t count,
                                                         size_t end) const
{
    qualified_type t = entity_table[e].type;
    entity_index owner = enclosing_function(entity_table, e);
    for (size_t k = count; k-- > 0;) {
        const lambda_context &l = lambdas[k];
        size_t last = end;
        if (k + 1 < count)
            last = lambdas[k + 1].first_capture;
        const capture_record *found = nullptr;
        for (size_t i = l.first_capture; i < last; i++)
            if (lambda_captures[i].variable == e &&
                !lambda_captures[i].init_capture)
                found = &lambda_captures[i];
        bool by_reference = l.default_capture == capture_default::reference;
        if (found)
            by_reference = found->by_reference;
        else if (l.default_capture == capture_default::none)
            break;
        if (!by_reference) {
            if (found)
                t = entity_table[found->member].type;
            else {
                type_kind kind = type_table[t.unqualified()].kind;
                if (kind == type_kind::lvalue_reference ||
                    kind == type_kind::rvalue_reference)
                    t = type_table[t.unqualified()].operand;
            }
            uint32_t cv = t.cv();
            if (!l.mutable_)
                cv |= cv_const;
            return qualified_type::make(t.unqualified(), cv);
        }
        if (l.enclosing_function == owner)
            break;
    }
    return t;
}

/*
 * [expr.prim.lambda.capture]/2: "If a lambda-capture includes a
 * capture-default that is &, no identifier in a simple-capture of that
 * lambda-capture shall be preceded by &.  If a lambda-capture includes a
 * capture-default that is =, each simple-capture of that lambda-capture
 * shall be of the form "& identifier ..._opt", "this", or "* this". ...
 * Ignoring appearances in initializers of init-captures, an identifier or
 * this shall not appear more than once in a lambda-capture."  /4: "The
 * identifier in a simple-capture is looked up using the usual rules for
 * unqualified name lookup ([basic.lookup.unqual]); each such lookup shall
 * find a local entity.  The simple-captures this and * this denote the
 * local entity *this."
 */
analysis_result semantic_analysis::lambda_introducer(LambdaIntroducer n,
                                                     lambda_context &l)
{
    owned_array<SyntaxElement> captures;
    analysis_result r = lambda_captures_of(n, captures);
    if (r != analysis_result::well_formed)
        return r;
    for (size_t i = 0; i < captures.size(); i++) {
        SyntaxElement c = captures[i];
        capture_record record;
        token_index name = 0;
        if (c.is_token()) {
            const token &t = tokens[c.token()];
            if (i == 0 && (c.is(preprocessing_op_or_punc::EQUAL,
                                preprocessing_op_or_punc::AMPERSAND))) {
                l.default_capture = capture_default::copy;
                if (c.is(preprocessing_op_or_punc::AMPERSAND))
                    l.default_capture = capture_default::reference;
                /* [expr.prim.lambda.capture]/3: "A lambda-expression shall
                   not have a capture-default or simple-capture in its
                   lambda-introducer unless its innermost enclosing scope
                   is a block scope ([basic.scope.block]) or it appears
                   within a default member initializer" */
                if (!l.enclosing_function)
                    return ill_formed_at_token(
                        c.token(),
                        "a lambda-expression shall not have a "
                        "capture-default unless its innermost enclosing "
                        "scope is a block scope",
                        "expr.prim.lambda.capture");
                continue;
            }
            if (t.kind == token_kind::keyword &&
                p.keyword_of(t) == standard_keyword::this_) {
                record.at = c.token();
            } else if (t.kind == token_kind::identifier) {
                name = c.token();
                /* "shall be of the form & identifier ..." with = */
                if (l.default_capture == capture_default::copy)
                    return ill_formed_at_token(name,
                                               "with the capture-default =, "
                                               "a simple-capture shall be of "
                                               "the form & identifier, this "
                                               "or * this",
                                               "expr.prim.lambda.capture");
            } else {
                return not_supported(n, "expr.prim.lambda.capture");
            }
        } else if (c.is(syntax_kind::init_capture)) {
            r = init_capture(c, l, record);
            if (r != analysis_result::well_formed)
                return r;
            lambda_captures.append(record);
            continue;
        } else if (c.is(syntax_kind::simple_capture)) {
            if (c.size() == 2 && c[0].is(preprocessing_op_or_punc::STAR)) {
                /* * this: *this captured by copy (/10) */
                record.at = c[1].token();
                record.star_this = true;
            } else if (c.size() == 2 &&
                       c[0].is(preprocessing_op_or_punc::AMPERSAND) &&
                       c[1].is_token() && c[1].is_identifier()) {
                name = c[1].token();
                record.by_reference = true;
                if (l.default_capture == capture_default::reference)
                    return ill_formed_at_token(name,
                                               "with the capture-default &, "
                                               "no identifier in a "
                                               "simple-capture shall be "
                                               "preceded by &",
                                               "expr.prim.lambda.capture");
            } else {
                /* a pack expansion (/17) */
                return not_supported(c.index(), "temp.variadic");
            }
        } else {
            return not_supported(n, "expr.prim.lambda.capture");
        }
        if (!name) {
            /* this or * this */
            if (!l.enclosing_this.unqualified())
                /* [basic.def.odr]/9: "either the local entity is not *this,
                   or an enclosing class or non-lambda function parameter
                   scope exists" */
                return ill_formed_at_token(record.at,
                                           "*this is a local entity only in "
                                           "a non-static member function",
                                           "basic.def.odr");
            for (size_t k = l.first_capture; k < lambda_captures.size(); k++)
                if (!lambda_captures[k].variable &&
                    !lambda_captures[k].init_capture)
                    return ill_formed_at_token(record.at,
                                               "this shall not appear more "
                                               "than once in a "
                                               "lambda-capture",
                                               "expr.prim.lambda.capture");
            qualified_type type = l.enclosing_this;
            if (record.star_this) {
                type = type_table[type.unqualified()].operand;
                if (!size_of_type(type))
                    return not_supported(n, "expr.prim.lambda.capture");
            } else {
                record.by_reference = true;
            }
            record.member = capture_member_of(l.closure, type);
            lambda_captures.append(record);
            continue;
        }
        lookup_result found =
            lookup_unqualified(tokens.identifier(name), lookup_filter::any);
        entity_index e = found.entity;
        if (found.ambiguous || found.unknown || !e ||
            entity_table[e].kind == entity_kind::none)
            return not_supported(n, "basic.lookup.unqual");
        /* a structured binding (C++20's local entity, [basic.pre]/7):
           its capture is not analysed */
        if (entity_table[e].kind == entity_kind::structured_binding)
            return not_supported(n, "expr.prim.lambda.capture");
        const entity &x = entity_table[e];
        if (x.kind != entity_kind::variable || !x.automatic)
            return ill_formed_at_token(name,
                                       "the identifier in a simple-capture "
                                       "shall find a local entity",
                                       "expr.prim.lambda.capture");
        for (size_t k = l.first_capture; k < lambda_captures.size(); k++)
            if (lambda_captures[k].variable == e &&
                !lambda_captures[k].init_capture)
                return ill_formed_at_token(name,
                                           "an identifier shall not appear "
                                           "more than once in a "
                                           "lambda-capture",
                                           "expr.prim.lambda.capture");
        /* a local entity of a function the lambda is not in: of a local
           class's member function's enclosing function */
        entity_index owner = enclosing_function(entity_table, e);
        bool reachable = owner == function_state.function;
        for (size_t k = lambdas.size(); k-- > 0 && !reachable;)
            if (lambdas[k].enclosing_function == owner)
                reachable = true;
        if (!reachable ||
            (owner != function_state.function && !current_lambda()))
            return not_supported(n, "basic.def.odr");
        record.variable = e;
        record.at = name;
        qualified_type type;
        qualified_type captured = x.type;
        if (record.by_reference)
            captured =
                enclosing_capture_type(e, lambdas.size(), l.first_capture);
        r = capture_type(n, captured, record.by_reference, type);
        if (r != analysis_result::well_formed)
            return r;
        record.member = capture_member_of(l.closure, type);
        lambda_captures.append(record);
    }
    return analysis_result::well_formed;
}

/*
 * [expr.prim.lambda.capture]/6: "An init-capture without ellipsis behaves
 * as if it declares and explicitly captures a variable of the form "auto
 * init-capture ;" whose declarative region is the lambda-expression's
 * compound-statement"; its initializer is analysed here, in the context
 * of the lambda-expression, and the variable is bound in the body
 * (bind_init_captures).  The member is initialized as the variable would
 * be; a name of the variable designates the member (named_entity).
 */
analysis_result semantic_analysis::init_capture(InitCapture n,
                                                lambda_context &l,
                                                capture_record &record)
{
    uint32_t i = 0;
    bool by_reference = n[0].is(preprocessing_op_or_punc::AMPERSAND);
    if (by_reference)
        i = 1;
    if (n.size() != i + 2 || !n[i].is_identifier())
        return not_supported(n, "temp.variadic");
    token_index name = n[i].token();
    for (size_t k = l.first_capture; k < lambda_captures.size(); k++)
        if (lambda_captures[k].variable &&
            entity_table[lambda_captures[k].variable].name ==
                tokens.identifier(name))
            return ill_formed_at_token(name,
                                       "an identifier shall not appear more "
                                       "than once in a lambda-capture",
                                       "expr.prim.lambda.capture");
    entity_index v =
        entity_table.make(entity_kind::variable, tokens.identifier(name), 0);
    entity_table[v].automatic = true;
    entity_table[v].defined = true;
    entity_table.add_declaration(v, n);
    qualified_type type = placeholder_type(false);
    if (by_reference)
        type = reference_to(type, false);
    entity_table[v].type = type;
    qualified_type outer_deduced = declaration_deduced;
    entity_index outer_variable = deducing_variable;
    declaration_deduced = qualified_type();
    deducing_variable = v;
    initialized_value value;
    analysis_result r = deduced_initializer(n[i + 1], v, type, value);
    declaration_deduced = outer_deduced;
    deducing_variable = outer_variable;
    if (r != analysis_result::well_formed)
        return r;
    qualified_type member = type;
    const struct type t = type_table[type.unqualified()];
    if (t.kind == type_kind::function ||
        (t.kind == type_kind::array && !by_reference) ||
        (!by_reference && !size_of_type(type)))
        return not_supported(n, "expr.prim.lambda.capture");
    record.variable = v;
    record.init_capture = true;
    record.by_reference = by_reference;
    record.at = name;
    record.initialization = value.node;
    record.member = capture_member_of(l.closure, member);
    entity_table[v].capture_member = record.member;
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::bind_init_captures(entity_index scope)
{
    lambda_context *l = current_lambda();
    for (size_t i = l->first_capture; i < lambda_captures.size(); i++) {
        const capture_record &c = lambda_captures[i];
        if (!c.init_capture)
            continue;
        uint32_t name = entity_table[c.variable].name;
        if (entity_table.find(scope, name))
            /* (the example of /6: "parameter and local variable have the
               same name") */
            return ill_formed_at_token(c.at,
                                       "an init-capture's variable and a "
                                       "parameter of the lambda-declarator "
                                       "shall not have the same name",
                                       "expr.prim.lambda.capture");
        entity_table[c.variable].enclosing = scope;
        entity_table.bind(scope, name, c.variable);
    }
    return analysis_result::well_formed;
}

/*
 * [expr.prim.lambda.capture]/11: "Every id-expression within the
 * compound-statement of a lambda-expression that is an odr-use
 * ([basic.def.odr]) of an entity captured by copy is transformed into an
 * access to the corresponding unnamed data member of the closure type."
 * A reference captured by reference designates what it refers to (/13),
 * the member being a reference to that.
 */
analysis_result semantic_analysis::closure_member(token_index t,
                                                  entity_index member,
                                                  expression_meaning &m)
{
    expression_meaning object;
    analysis_result r = this_object(t, object, true);
    if (r != analysis_result::well_formed)
        return r;
    r = member_access(t, member, object);
    if (r != analysis_result::well_formed)
        return r;
    m = object;
    m.evaluation = constness::not_constant;
    return analysis_result::well_formed;
}

/*
 * A name in the body of the current lambda of a local entity of an
 * enclosing function.  [expr.prim.lambda.capture]/7: "If an expression
 * potentially references a local entity within a declarative region in
 * which it is odr-usable, and the expression would be potentially
 * evaluated if the effect of any enclosing typeid expressions were
 * ignored, the entity is said to be implicitly captured by each
 * intervening lambda-expression with an associated capture-default that
 * does not explicitly capture it."  [basic.def.odr]/9: "If a local entity
 * is odr-used in a declarative region in which it is not odr-usable, the
 * program is ill-formed."
 */
analysis_result semantic_analysis::captured_entity(token_index t,
                                                   entity_index e,
                                                   expression_meaning &m)
{
    lambda_context &l = *current_lambda();
    for (size_t i = l.first_capture; i < lambda_captures.size(); i++)
        if (lambda_captures[i].variable == e)
            return closure_member(t, lambda_captures[i].member, m);
    entity_index owner = enclosing_function(entity_table, e);
    bool reachable = false;
    for (size_t k = lambdas.size(); k-- > 0 && !reachable;)
        if (lambdas[k].enclosing_function == owner)
            reachable = true;
    if (!reachable)
        /* of a function enclosing a local class whose member function
           the lambda is in */
        return not_supported(current_declaration, "basic.def.odr");
    /* in an unevaluated operand, or a variable whose reading may be no
       odr-use ([basic.def.odr]/4): whether it is captured is not
       analysed */
    if (unevaluated || entity_table[e].usable != constness::not_constant)
        return not_supported(current_declaration, "basic.def.odr");
    if (l.default_capture == capture_default::none)
        return ill_formed_at_token(t,
                                   "a local entity odr-used in a declarative "
                                   "region in which it is not odr-usable (a "
                                   "lambda that does not capture it) makes "
                                   "the program ill-formed",
                                   "basic.def.odr");
    capture_record record;
    record.variable = e;
    record.at = t;
    /* [expr.prim.lambda.capture]/10: "An entity is captured by copy if it
       is implicitly captured, the capture-default is =, and the captured
       entity is not *this" */
    record.by_reference = l.default_capture == capture_default::reference;
    qualified_type type;
    qualified_type captured = entity_table[e].type;
    if (record.by_reference)
        captured =
            enclosing_capture_type(e, lambdas.size() - 1, l.first_capture);
    analysis_result r =
        capture_type(current_declaration, captured, record.by_reference, type);
    if (r != analysis_result::well_formed)
        return r;
    record.member = capture_member_of(l.closure, type);
    lambda_captures.append(record);
    return closure_member(t, record.member, m);
}

/*
 * [expr.prim.lambda.closure]/12: in the compound-statement this is the
 * enclosing context's: the member capturing it (a pointer), or the address
 * of the member that is a copy of *this ([expr.prim.lambda.capture]/11:
 * "If *this is captured by copy, each expression that odr-uses *this is
 * transformed to instead refer to the corresponding unnamed data member of
 * the closure type").  Implicitly captured by a capture-default (/7).
 */
analysis_result semantic_analysis::captured_this(token_index t,
                                                 expression_meaning &m)
{
    lambda_context &l = *current_lambda();
    entity_index member = 0;
    bool star = false;
    for (size_t i = l.first_capture; i < lambda_captures.size(); i++)
        if (!lambda_captures[i].variable && !lambda_captures[i].init_capture) {
            member = lambda_captures[i].member;
            star = lambda_captures[i].star_this;
        }
    if (!member) {
        if (!l.enclosing_this.unqualified())
            return ill_formed_at_token(t,
                                       "the expression this shall appear only "
                                       "in a member function or a default "
                                       "member initializer",
                                       "expr.prim.this");
        if (unevaluated)
            return not_supported(current_declaration,
                                 "expr.prim.lambda.capture");
        if (l.default_capture == capture_default::none)
            return ill_formed_at_token(t,
                                       "*this odr-used in a declarative region "
                                       "in which it is not odr-usable (a "
                                       "lambda that does not capture it) "
                                       "makes the program ill-formed",
                                       "basic.def.odr");
        capture_record record;
        record.by_reference = true;
        record.at = t;
        record.member = capture_member_of(l.closure, l.enclosing_this);
        lambda_captures.append(record);
        member = record.member;
    }
    analysis_result r = closure_member(t, member, m);
    if (r != analysis_result::well_formed)
        return r;
    if (!star)
        return operand_value(current_declaration, m);
    m.type = qualified_type::make(type_table.pointer_to(m.type), cv_none);
    m.category = value_category::prvalue;
    m.node = expression_node(semantic_kind::address, m, t, m.node);
    return analysis_result::well_formed;
}

/*
 * [expr.prim.lambda.capture]/15: "When the lambda-expression is evaluated,
 * the entities that are captured by copy are used to direct-initialize
 * each corresponding non-static data member of the resulting closure
 * object, and the non-static data members corresponding to the
 * init-captures are initialized as indicated by the corresponding
 * initializer (which may be copy- or direct-initialization). ... These
 * initializations are performed in the (unspecified) order in which the
 * non-static data members are declared."  The entities are named in the
 * context of the lambda-expression: an enclosing lambda's body captures
 * them in turn (/14).
 */
analysis_result
semantic_analysis::closure_object(lambda_context &l,
                                  const owned_array<capture_record> &captures,
                                  expression_meaning &m)
{
    token_index at = l.at.first_token();
    owned_array<uint32_t> elements;
    for (const capture_record &c : captures) {
        if (c.init_capture) {
            elements.append(c.initialization);
            continue;
        }
        expression_meaning e;
        analysis_result r = analysis_result::well_formed;
        if (c.variable)
            r = named_entity(c.at, c.variable, e, false);
        else if (c.star_this)
            r = this_object(c.at, e);
        else
            r = this_pointer(c.at, e);
        initialized_value value;
        if (r == analysis_result::well_formed)
            r = initialize_with(l.at, e, entity_table[c.member].type, true,
                                false, value);
        if (r != analysis_result::well_formed)
            return r;
        elements.append(value.node);
    }
    m = expression_meaning();
    m.type = entity_table[l.closure].type;
    m.category = value_category::prvalue;
    m.evaluation = constness::not_constant;
    m.node = expression_node(semantic_kind::initializer_list, m, at);
    semantics.set_list(m.node, elements.data(), elements.size());
    return analysis_result::well_formed;
}

} // namespace cplusplus
