/*
 * lambda-capture.cpp: [expr.prim.lambda.capture] Captures: the
 * analysis of a lambda-introducer's captures (the grammar is
 * lambda-expression.cpp's) and of the names in a lambda's body that capture
 * an entity.
 *
 * Each entity captured is a non-static data member of the closure type
 * (/10, /12: one for a capture by reference too, [impl.clang] as clang has),
 * made when the capture is: the explicit ones in the order of the
 * lambda-capture, then the implicit ones in the order of their first use in the
 * body (clang's order).  In the body a name of a captured entity, and this,
 * designate the member through the function call operator's this (/11); the
 * closure object's members are initialized where the lambda-expression is (/15,
 * closure_object), where a capture of an enclosing lambda's entity captures it
 * in that lambda too (/14).
 *
 * A variable usable in constant expressions is captured only by an odr-use
 * ([basic.def.odr]/4): the names of such variables in a body are kept as
 * entity nodes until the body is analysed (pending_uses), and a read is
 * none.  A generic lambda's closure type is complete where the
 * lambda-expression is, its body being analysed once per specialization
 * of the call operator template: its implicit captures are the names the
 * body has (implicit_captures) and a specialization that needs another is
 * not analysed.  A capture pack ([xs...], [...ys = xs], /17) is one capture
 * per element of the pack.
 *
 * Not analysed: a function captured by copy, a reference to a function
 * captured, an entity of a function enclosing a local class whose member
 * function the lambda is in, a lambda in a default argument of a function
 * that is not a member, and a variable usable in constant expressions
 * odr-used in a generic lambda's body.
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
            VTRY(r, lambda_captures_of(c, out));
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
    if (o.kind == type_kind::function)
        return not_supported_because(at, "expr.prim.lambda.capture",
                                     "a capture of a function");
    /* an array captured by copy is copied element by element (/15) */
    if (o.kind == type_kind::array && !by_reference &&
        (!o.bound_known || !size_of_type(object)))
        return not_supported_because(
            at, "expr.prim.lambda.capture",
            "an array of unknown bound or size captured by copy");
    if (by_reference) {
        type = reference_to(object, false);
        return analysis_result::well_formed;
    }
    if (!size_of_type(object))
        return not_supported_because(
            at, "expr.prim.lambda.capture",
            "a capture by copy of a type with no known size");
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
    VTRY(r, lambda_captures_of(n, captures));
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
                if (!l.enclosing_function && !l.enclosing_class)
                    return ill_formed_at_token(
                        c.token(),
                        "a lambda-expression shall not have a "
                        "capture-default unless its innermost enclosing "
                        "scope is a block scope",
                        "expr.prim.lambda.capture");
                continue;
            }
            if (t.kind == token_kind::keyword &&
                syntax_parser.keyword_of(t) == standard_keyword::this_) {
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
                return not_supported_because(
                    n, "expr.prim.lambda.capture",
                    "a simple-capture of an unhandled form");
            }
        } else if (c.is(syntax_kind::init_capture)) {
            VTRY_SET(r, init_capture(c, l, record));
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
                /* a pack expansion (/17): the capture of each element */
                bool reference = c.size() == 3 &&
                                 c[0].is(preprocessing_op_or_punc::AMPERSAND);
                size_t at = reference;
                if (c.size() != at + 2 ||
                    !c[static_cast<uint32_t>(at)].is_token() ||
                    !c[static_cast<uint32_t>(at)].is_identifier() ||
                    !c[static_cast<uint32_t>(at + 1)].is(
                        preprocessing_op_or_punc::ELLIPSIS))
                    return not_supported_because(
                        c.index(), "temp.variadic",
                        "a capture pack expansion that is not identifier...");
                VTRY(packed,
                     pack_capture(n, l, c[static_cast<uint32_t>(at)].token(),
                                  reference));
                continue;
            }
        } else {
            return not_supported_because(n, "expr.prim.lambda.capture",
                                         "a capture of an unhandled form");
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
                    return not_supported_because(
                        n, "expr.prim.lambda.capture",
                        "a *this capture of a class with no known size");
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
            return not_supported_because(
                n, "basic.lookup.unqual",
                "a capture of a name that lookup cannot resolve");
        VTRY_SET(r, variable_capture(n, l, e, name, record.by_reference));
    }
    return analysis_result::well_formed;
}

/*
 * The explicit capture of local entity e, named at name, by reference or
 * by copy: the member of the closure type ([expr.prim.lambda.capture]/10,
 * /12).
 */
analysis_result semantic_analysis::variable_capture(SyntaxRef n,
                                                    lambda_context &l,
                                                    entity_index e,
                                                    token_index name,
                                                    bool by_reference)
{
    capture_record record;
    record.by_reference = by_reference;
    const entity x = entity_table[e];
    /* a structured binding is a local entity too (C++20,
       [basic.pre]/7) */
    bool binding = x.kind == entity_kind::structured_binding &&
                   x.binding_object && entity_table[x.binding_object].automatic;
    if (!binding && (x.kind != entity_kind::variable || !x.automatic))
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
    bool reachable = local_reachable(owner);
    if (!reachable || (owner != function_state.function && !current_lambda()))
        return not_supported_because(
            n, "basic.def.odr",
            "a capture of a local that is not reachable from this lambda");
    record.variable = e;
    record.at = name;
    qualified_type type;
    qualified_type captured = x.type;
    if (record.by_reference)
        captured = enclosing_capture_type(e, lambdas.size(), l.first_capture);
    VTRY(r, capture_type(n, captured, record.by_reference, type));
    record.member = capture_member_of(l.closure, type);
    lambda_captures.append(record);
    return analysis_result::well_formed;
}

/*
 * [expr.prim.lambda.capture]/17 and [temp.variadic]: a simple-capture
 * with an ellipsis captures each element of the pack its identifier
 * names, as if each were listed.  The pack is the function parameter
 * pack of the instantiation being analysed; a body that expands it names
 * the elements, which are captured.
 */
analysis_result semantic_analysis::pack_capture(SyntaxRef n, lambda_context &l,
                                                token_index name,
                                                bool by_reference)
{
    entity_index pack = find_pack(tokens.identifier(name));
    if (!pack)
        return not_supported_because(
            n, "temp.variadic",
            "a capture pack expansion of a name that is not a pack");
    uint32_t first = static_cast<uint32_t>(entity_table[pack].value);
    uint32_t count = entity_table[pack].member_count;
    for (uint32_t i = 0; i < count; i++) {
        entity_index e = pack_elements[first + i];
        VTRY(r, variable_capture(n, l, e, name, by_reference));
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
    /* [expr.prim.lambda.capture]/17: an init-capture pack, ... identifier
       initializer */
    bool pack = n.size() > i && n[i].is(preprocessing_op_or_punc::ELLIPSIS);
    if (pack)
        i++;
    if (n.size() != i + 2 || !n[i].is_identifier())
        return not_supported_because(
            n, "temp.variadic", "an init-capture pack of an unhandled form");
    token_index name = n[i].token();
    for (size_t k = l.first_capture; k < lambda_captures.size(); k++)
        if (lambda_captures[k].variable &&
            entity_table[lambda_captures[k].variable].name ==
                tokens.identifier(name))
            return ill_formed_at_token(name,
                                       "an identifier shall not appear more "
                                       "than once in a lambda-capture",
                                       "expr.prim.lambda.capture");
    /* /17: "the initializer shall contain an unexpanded pack": the
       elements of the packs it names, one capture each */
    uint32_t count = 1;
    if (pack) {
        SyntaxElement init = n[i + 1];
        if (!expansion_count(init.first_token(), init.end_token(), count))
            return ill_formed_at_token(name,
                                       "the initializer of an init-capture "
                                       "pack shall name a parameter pack "
                                       "and no pack expansion",
                                       "expr.prim.lambda.capture");
    }
    for (uint32_t k = 0; k < count; k++) {
        entity_index v = entity_table.make(entity_kind::variable,
                                           tokens.identifier(name), 0);
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
        analysis_result r = analysis_result::well_formed;
        {
            expansion_scope scope(*this, pack, k);
            r = deduced_initializer(n[i + 1], v, type, value);
        }
        declaration_deduced = outer_deduced;
        deducing_variable = outer_variable;
        if (r != analysis_result::well_formed)
            return r;
        qualified_type member = type;
        const struct type t = type_table[type.unqualified()];
        if (t.kind == type_kind::function ||
            (t.kind == type_kind::array && !by_reference) ||
            (!by_reference && !size_of_type(type)))
            return not_supported_because(n, "expr.prim.lambda.capture",
                                         "an init-capture of a function, array "
                                         "or type with no known size");
        capture_record each;
        each.variable = v;
        each.init_capture = true;
        each.by_reference = by_reference;
        each.at = name;
        each.initialization = value.node;
        each.member = capture_member_of(l.closure, member);
        if (pack)
            each.pack_name = tokens.identifier(name);
        entity_table[v].capture_member = each.member;
        lambda_captures.append(each);
    }
    /* (an empty pack has no member: its name is bound all the same) */
    if (pack && count == 0) {
        capture_record none;
        none.init_capture = true;
        none.pack_name = tokens.identifier(name);
        none.at = name;
        lambda_captures.append(none);
    }
    record.at = 0;
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::bind_init_captures(entity_index scope)
{
    lambda_context *l = current_lambda();
    for (size_t i = l->first_capture; i < lambda_captures.size(); i++) {
        const capture_record c = lambda_captures[i];
        if (!c.init_capture)
            continue;
        uint32_t name = c.pack_name;
        if (c.variable)
            name = entity_table[c.variable].name;
        if (entity_table.find(scope, name))
            /* (the example of /6: "parameter and local variable have the
               same name") */
            return ill_formed_at_token(c.at,
                                       "an init-capture's variable and a "
                                       "parameter of the lambda-declarator "
                                       "shall not have the same name",
                                       "expr.prim.lambda.capture");
        if (c.pack_name) {
            /* an init-capture pack's name is the pack of its variables */
            owned_array<entity_index> elements;
            size_t k = i;
            for (; k < lambda_captures.size() &&
                   lambda_captures[k].pack_name == c.pack_name;
                 k++)
                if (lambda_captures[k].variable) {
                    entity_table[lambda_captures[k].variable].enclosing = scope;
                    elements.append(lambda_captures[k].variable);
                }
            make_bound_pack(scope, name, elements.data(),
                            static_cast<uint32_t>(elements.size()));
            i = k - 1;
            continue;
        }
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
    VTRY(r, this_object(t, object, true));
    VTRY_SET(r, member_access(t, member, object));
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
/* whether the current lambda has a capture of e already (an explicit
   capture, or an implicit one made by an earlier use) */
bool semantic_analysis::captured_by_name(entity_index e)
{
    lambda_context &l = *current_lambda();
    for (size_t i = l.first_capture; i < lambda_captures.size(); i++)
        if (lambda_captures[i].variable == e)
            return true;
    return false;
}

analysis_result semantic_analysis::captured_entity(token_index t,
                                                   entity_index e,
                                                   expression_meaning &m,
                                                   bool &deferred, bool &plain)
{
    lambda_context &l = *current_lambda();
    for (size_t i = l.first_capture; i < lambda_captures.size(); i++)
        if (lambda_captures[i].variable == e)
            return closure_member(t, lambda_captures[i].member, m);
    /* [basic.def.odr]/4: an unevaluated operand odr-uses nothing */
    if (unevaluated) {
        plain = true;
        return analysis_result::well_formed;
    }
    entity_index owner = enclosing_function(entity_table, e);
    bool reachable = local_reachable(owner);
    if (!reachable)
        /* of a function enclosing a local class whose member function
           the lambda is in */
        return not_supported_because(
            current_declaration, "basic.def.odr",
            "a use of a local that is not reachable from this lambda");
    /* a variable usable in constant expressions is odr-used unless its
       value is read (/4): decided when the body is analysed
       (resolve_pending_uses) */
    const struct type &variable_type =
        type_table[entity_table[e].type.unqualified()];
    if (entity_table[e].usable != constness::not_constant && !forced_odr_use) {
        if (entity_table[e].usable != constness::constant ||
            (variable_type.kind != type_kind::fundamental &&
             variable_type.kind != type_kind::enumeration)) {
            if (l.default_capture != capture_default::none)
                return capture_variable(l, t, e, m);
            /* (a pointer's or class object's reads, and a variable whose
               value is not known, are not tracked: whether it is odr-used
               is not analysed) */
            return not_supported_because(current_declaration, "basic.def.odr",
                                         "a possible odr-use of a constant "
                                         "variable that is not tracked");
        }
        plain = true;
        deferred = true;
        return analysis_result::well_formed;
    }
    return capture_variable(l, t, e, m);
}

/*
 * Whether a local entity of function owner is reachable from the function
 * analysed now: it is that function, or one a lambda on the stack is in, or
 * the function a generic lambda's closure was made in, which a
 * specialization's body (analysed from wherever it is called) has no
 * lambda on the stack for.
 */
bool semantic_analysis::local_reachable(entity_index owner) const
{
    entity_index f = function_state.function;
    for (unsigned guard = 0; f && guard < 64; guard++) {
        if (f == owner)
            return true;
        for (size_t k = lambdas.size(); k-- > 0;)
            if (lambdas[k].enclosing_function == owner)
                return true;
        entity_index c = entity_table[f].enclosing;
        while (c && entity_table[c].kind != entity_kind::class_type)
            c = entity_table[c].enclosing;
        entity_index outer = 0;
        for (const generic_closure &g : generic_closures)
            if (g.closure == c)
                outer = g.enclosing_function;
        f = outer;
    }
    return false;
}

/*
 * The implicit capture of local variable e, odr-used at t in the body of
 * the current lambda: a member of the closure type, and the access to it.
 * [basic.def.odr]/9 (the lambda has no capture-default that captures it):
 * ill-formed.
 */
analysis_result semantic_analysis::capture_variable(lambda_context &l,
                                                    token_index t,
                                                    entity_index e,
                                                    expression_meaning &m)
{
    /* a generic lambda's closure is complete: the names its body had
       when the lambda-expression was analysed are captured
       (implicit_captures) */
    if (l.sealed && l.default_capture != capture_default::none)
        return not_supported_because(
            current_declaration, "expr.prim.lambda.capture",
            "a new implicit capture after the lambda's captures were fixed");
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
    VTRY(r, capture_type(current_declaration, captured, record.by_reference,
                         type));
    record.member = capture_member_of(l.closure, type);
    lambda_captures.append(record);
    return closure_member(t, record.member, m);
}

/*
 * The names of variables usable in constant expressions in the body just
 * analysed of lambda l (captured_entity): one whose value is read
 * ([basic.def.odr]/4) is no odr-use and names the variable; any other use
 * captures it, and its node becomes the access to the member.
 */
analysis_result semantic_analysis::resolve_pending_uses(lambda_context &l)
{
    size_t depth = lambdas.size();
    size_t keep = 0;
    analysis_result result = analysis_result::well_formed;
    for (size_t i = 0; i < pending_uses.size(); i++) {
        pending_use u = pending_uses[i];
        if (u.lambda_depth != depth) {
            pending_uses[keep++] = u;
            continue;
        }
        if (u.read) {
            /* lowering folds the read (c_lowering::folded_read) */
            semantics[u.node].evaluation = constness::constant;
            continue;
        }
        if (result != analysis_result::well_formed)
            continue;
        expression_meaning access;
        entity_index outer_this = function_state.this_entity;
        function_state.this_entity = u.this_entity;
        entity_index member = 0;
        for (size_t k = l.first_capture; k < lambda_captures.size(); k++)
            if (lambda_captures[k].variable == u.variable)
                member = lambda_captures[k].member;
        analysis_result r = analysis_result::well_formed;
        if (member)
            r = closure_member(u.at, member, access);
        else
            r = capture_variable(l, u.at, u.variable, access);
        function_state.this_entity = outer_this;
        if (r != analysis_result::well_formed)
            result = r;
        else
            semantics[u.node] = semantics[access.node];
    }
    pending_uses.resize(keep);
    return result;
}

/*
 * Whether the name of a variable usable in constant expressions at token
 * t of a generic lambda's body (in [first, end)) is an odr-use, as far as
 * its statement shows: [expr.prim.lambda.capture]/7 captures it when the
 * full-expression depends on a generic lambda parameter, and otherwise it
 * is an odr-use unless it is only read ([basic.def.odr]/4), which a
 * reference bound to it or its address is not.
 */
bool semantic_analysis::odr_use_in_statement(
    token_index t, token_index first, token_index end,
    const owned_array<parameter_meaning> &parameters) const
{
    auto boundary = [&](token_index k) {
        return is_operator(tokens[k], preprocessing_op_or_punc::SEMICOLON) ||
               is_operator(tokens[k], preprocessing_op_or_punc::LEFT_BRACE) ||
               is_operator(tokens[k], preprocessing_op_or_punc::RIGHT_BRACE);
    };
    token_index from = t, to = t;
    while (from > first && !boundary(from - 1))
        from--;
    while (to + 1 < end && !boundary(to + 1))
        to++;
    bool before_equal = true;
    for (token_index k = from; k <= to; k++) {
        if (is_operator(tokens[k], preprocessing_op_or_punc::EQUAL))
            before_equal = false;
        /* a reference declared before the =, its address taken */
        if (k < t && before_equal &&
            is_operator(tokens[k], preprocessing_op_or_punc::AMPERSAND) &&
            k + 1 < t)
            return true;
        if (k + 1 == t &&
            is_operator(tokens[k], preprocessing_op_or_punc::AMPERSAND))
            return true;
        if (tokens[k].kind != token_kind::identifier)
            continue;
        for (const parameter_meaning &p : parameters)
            if (p.id && tokens.identifier(p.id) == tokens.identifier(k))
                return true;
    }
    return false;
}

/*
 * [expr.prim.lambda.capture]/7: the entities a generic lambda's body
 * implicitly captures, which its capture-default captures.  The body is
 * analysed once per specialization of the function call operator
 * template, but the closure type is complete where the lambda-expression
 * is; so the body's names decide here, [impl.clang] as clang does for a
 * template's: every name of a local entity of the enclosing function, outside
 * an unevaluated operand, that is not a parameter or an init-capture, and no
 * variable usable in constant expressions (whose reads are no odr-use), and
 * this when the body names it or a non-static member.  /7 captures a name "in a
 * potentially-evaluated expression where the enclosing full-expression depends
 * on a generic lambda parameter"; whether an expression does is not known
 * before a specialization, so a name is taken to.  A name that a declaration in
 * the body hides is captured too, which the closure type's size shows.
 */
analysis_result semantic_analysis::implicit_captures(
    SyntaxRef n, lambda_context &l,
    const owned_array<parameter_meaning> &parameters)
{
    if (l.default_capture == capture_default::none)
        return analysis_result::well_formed;
    SyntaxElement body = n[n.size() - 1];
    token_index first = body.first_token(), end = body.end_token();
    owned_array<uint32_t> own;
    for (const parameter_meaning &p : parameters)
        if (p.id)
            own.append(tokens.identifier(p.id));
    for (size_t i = l.first_capture; i < lambda_captures.size(); i++)
        if (lambda_captures[i].init_capture && lambda_captures[i].variable)
            own.append(entity_table[lambda_captures[i].variable].name);
        else if (lambda_captures[i].pack_name)
            own.append(lambda_captures[i].pack_name);
    int depth = 0, unevaluated_depth = -1;
    for (token_index t = first; t < end; t++) {
        if (is_operator(tokens[t],
                        preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
            if (unevaluated_depth < 0 && t > first &&
                tokens[t - 1].kind == token_kind::keyword) {
                standard_keyword kw = syntax_parser.keyword_of(tokens[t - 1]);
                if (kw == standard_keyword::sizeof_ ||
                    kw == standard_keyword::alignof_ ||
                    kw == standard_keyword::decltype_ ||
                    kw == standard_keyword::noexcept_)
                    unevaluated_depth = depth;
            }
            depth++;
            continue;
        }
        if (is_operator(tokens[t],
                        preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
            depth--;
            if (depth == unevaluated_depth)
                unevaluated_depth = -1;
            continue;
        }
        if (unevaluated_depth >= 0)
            continue;
        bool names_this =
            tokens[t].kind == token_kind::keyword &&
            syntax_parser.keyword_of(tokens[t]) == standard_keyword::this_;
        capture_record record;
        if (!names_this) {
            if (tokens[t].kind != token_kind::identifier)
                continue;
            if (t > first &&
                (is_operator(tokens[t - 1], preprocessing_op_or_punc::DOT) ||
                 is_operator(tokens[t - 1], preprocessing_op_or_punc::ARROW) ||
                 is_operator(tokens[t - 1],
                             preprocessing_op_or_punc::COLON_COLON)))
                continue;
            uint32_t name = tokens.identifier(t);
            bool skip = false;
            for (uint32_t k : own)
                skip = skip || k == name;
            if (skip)
                continue;
            lookup_result found = lookup_unqualified(name, lookup_filter::any);
            entity_index e = found.entity;
            if (found.ambiguous || found.unknown || !e)
                continue;
            const entity &x = entity_table[e];
            if (x.nonstatic_member) {
                names_this = true;
            } else {
                bool binding = x.kind == entity_kind::structured_binding &&
                               x.binding_object &&
                               entity_table[x.binding_object].automatic;
                if (!binding &&
                    (x.kind != entity_kind::variable || !x.automatic))
                    continue;
                bool captured = false;
                for (size_t k = l.first_capture; k < lambda_captures.size();
                     k++)
                    captured = captured || (lambda_captures[k].variable == e &&
                                            !lambda_captures[k].init_capture);
                if (captured)
                    continue;
                entity_index owner = enclosing_function(entity_table, e);
                bool reachable = local_reachable(owner);
                if (!reachable)
                    continue;
                const struct type kind = type_table[x.type.unqualified()];
                if (x.usable == constness::constant &&
                    (kind.kind == type_kind::fundamental ||
                     kind.kind == type_kind::enumeration) &&
                    !odr_use_in_statement(t, first, end, parameters))
                    continue;
                record.variable = e;
                record.at = t;
                record.by_reference =
                    l.default_capture == capture_default::reference;
                qualified_type captured_type = x.type;
                if (record.by_reference)
                    captured_type = enclosing_capture_type(e, lambdas.size(),
                                                           l.first_capture);
                qualified_type member_type;
                VTRY(r, capture_type(n, captured_type, record.by_reference,
                                     member_type));
                record.member = capture_member_of(l.closure, member_type);
                lambda_captures.append(record);
                continue;
            }
        }
        if (!names_this || !l.enclosing_this.unqualified())
            continue;
        bool captured = false;
        for (size_t k = l.first_capture; k < lambda_captures.size(); k++)
            captured = captured || (!lambda_captures[k].variable &&
                                    !lambda_captures[k].init_capture);
        if (captured)
            continue;
        record.at = t;
        record.by_reference = true;
        record.member = capture_member_of(l.closure, l.enclosing_this);
        lambda_captures.append(record);
    }
    return analysis_result::well_formed;
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
        if (unevaluated || l.sealed)
            return not_supported_because(
                current_declaration, "expr.prim.lambda.capture",
                "a use of this in an unevaluated or already analysed lambda");
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
    VTRY(r, closure_member(t, member, m));
    if (!star)
        return operand_value(current_declaration, m);
    m.type = qualified_type::make(type_table.pointer_to(m.type), cv_none);
    m.category = value_category::prvalue;
    m.node = expression_node(semantic_kind::address, m, t, m.node);
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::copy_array(SyntaxRef at, token_index t,
                                              expression_meaning array,
                                              qualified_type type,
                                              uint32_t &node)
{
    const struct type a = type_table[type.unqualified()];
    qualified_type element = a.operand;
    VTRY(r, operand_value(at, array));
    owned_array<uint32_t> items;
    for (uint64_t i = 0; i < a.bound; i++) {
        expression_meaning index =
            integer_constant(fundamental_type::int_, i, t);
        expression_meaning object;
        object.type = element;
        object.category = value_category::lvalue;
        object.node = expression_node(semantic_kind::subscript, object, t,
                                      array.node, index.node);
        if (type_table[element.unqualified()].kind == type_kind::array) {
            uint32_t inner = 0;
            VTRY_SET(r, copy_array(at, t, object, element, inner));
            items.append(inner);
            continue;
        }
        initialized_value value;
        VTRY_SET(r, initialize_with(at, object, element, true, false, value));
        items.append(value.node);
    }
    expression_meaning list;
    list.type = type;
    list.category = value_category::prvalue;
    list.evaluation = constness::not_constant;
    list.node = expression_node(semantic_kind::initializer_list, list, t);
    semantics.set_list(list.node, items.data(), items.size());
    node = list.node;
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
            if (c.member)
                elements.append(c.initialization);
            continue;
        }
        expression_meaning e;
        analysis_result r = analysis_result::well_formed;
        if (c.variable) {
            /* the capture odr-uses the entity (/15) */
            forced_odr_use = true;
            r = named_entity(c.at, c.variable, e, false);
            forced_odr_use = false;
        } else if (c.star_this)
            r = this_object(c.at, e);
        else
            r = this_pointer(c.at, e);
        initialized_value value;
        qualified_type member_type = entity_table[c.member].type;
        if (r == analysis_result::well_formed &&
            type_table[member_type.unqualified()].kind == type_kind::array) {
            VTRY_SET(r, copy_array(l.at, c.at, e, member_type, value.node));
        } else if (r == analysis_result::well_formed) {
            VTRY_SET(r,
                     initialize_with(l.at, e, member_type, true, false, value));
        }
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
