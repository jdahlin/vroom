/*
 * expr.const.evaluate.cpp: [expr.const] the evaluation of a core constant
 * expression over the semantic tree.
 *
 * [expr.const]/5: "An expression E is a core constant expression unless
 * the evaluation of E, following the rules of the abstract machine
 * ([intro.execution]), would evaluate one of the following".  Analysis
 * folds the operators of the fundamental types as it builds the tree
 * (expression_meaning::evaluation); this is the evaluator for what it does
 * not fold: invocations of constexpr functions, their statements and
 * objects.
 *
 * What it models: values of the integer types up to 64 bits, bool, the
 * unscoped and scoped enumerations, float and double; objects of those
 * types that are local variables and parameters of the invocations
 * (state.objects, one frame per invocation), and variables usable in
 * constant expressions (entity::usable).  Every other kind of node, type
 * or statement answers unsupported ([expr.const]): pointers, references,
 * arrays, classes, member functions, switch statements, long double.
 *
 * Inside an evaluation analysis's not_constant does not decide: it says an
 * assignment or a read of a local is not a constant expression on its own,
 * while [expr.const]/5.8 and /5.16 allow both for an object "whose
 * lifetime began within the evaluation of E".  Only the outermost node's
 * not_constant is taken as it is.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

enum : uint8_t { jump_none, jump_break, jump_continue };

constant_value evaluated_integer(__int128 v)
{
    constant_value r;
    r.kind = constant_value_kind::integer;
    r.integer = static_cast<unsigned __int128>(v);
    return r;
}

constant_value evaluated_floating(double v)
{
    constant_value r;
    r.kind = constant_value_kind::floating;
    r.floating = v;
    return r;
}

/* the binary operator a compound assignment applies ([expr.ass]/6) */
preprocessing_op_or_punc compound_operator(preprocessing_op_or_punc o)
{
    switch (o) {
    case preprocessing_op_or_punc::PLUS_EQUAL:
        return preprocessing_op_or_punc::PLUS;
    case preprocessing_op_or_punc::MINUS_EQUAL:
        return preprocessing_op_or_punc::MINUS;
    case preprocessing_op_or_punc::STAR_EQUAL:
        return preprocessing_op_or_punc::STAR;
    case preprocessing_op_or_punc::SLASH_EQUAL:
        return preprocessing_op_or_punc::SLASH;
    case preprocessing_op_or_punc::PERCENT_EQUAL:
        return preprocessing_op_or_punc::PERCENT;
    case preprocessing_op_or_punc::LESS_LESS_EQUAL:
        return preprocessing_op_or_punc::LESS_LESS;
    case preprocessing_op_or_punc::GREATER_GREATER_EQUAL:
        return preprocessing_op_or_punc::GREATER_GREATER;
    case preprocessing_op_or_punc::AMPERSAND_EQUAL:
        return preprocessing_op_or_punc::AMPERSAND;
    case preprocessing_op_or_punc::BAR_EQUAL:
        return preprocessing_op_or_punc::BAR;
    case preprocessing_op_or_punc::CARET_EQUAL:
        return preprocessing_op_or_punc::CARET;
    default:
        return preprocessing_op_or_punc::count;
    }
}

} // namespace

fundamental_type semantic_analysis::evaluated_type(qualified_type t) const
{
    fundamental_type f = fundamental_of(t);
    if (f == fundamental_type::none) {
        entity_index e = enumeration_of(t);
        if (!e)
            return fundamental_type::none;
        f = entity_table[e].underlying;
    }
    if (f == fundamental_type::float_ || f == fundamental_type::double_)
        return f;
    if (!fundamental_type_is_integral(f) || f == fundamental_type::int128 ||
        f == fundamental_type::unsigned_int128 ||
        f == fundamental_type::bit_precise_int ||
        f == fundamental_type::unsigned_bit_precise_int)
        return fundamental_type::none;
    return f;
}

uint64_t semantic_analysis::kept_of(fundamental_type t,
                                    const constant_value &v) const
{
    if (v.kind == constant_value_kind::floating)
        return floating_kept(v.floating);
    return integer_wrap(tokens.files.behavior, t, v.integer);
}

constant_value semantic_analysis::value_of(fundamental_type t,
                                           uint64_t kept) const
{
    if (fundamental_type_is_floating(t))
        return evaluated_floating(floating_value(kept));
    return evaluated_integer(integer_value(tokens.files.behavior, t, kept));
}

void semantic_analysis::evaluated_meaning(expression_meaning &m,
                                          const constant_value &v)
{
    m.evaluation = constness::constant;
    m.value = kept_of(evaluated_type(m.type), v);
}

/* [conv.integral], [conv.double], [conv.fpint], [conv.bool]: the rules
   analysis folds with, on a value */
analysis_result semantic_analysis::evaluate_conversion(constant_value &v,
                                                       fundamental_type from,
                                                       fundamental_type to,
                                                       bool &constant)
{
    constant = false;
    if (from == fundamental_type::none || to == fundamental_type::none)
        return analysis_result::unsupported;
    expression_meaning m;
    m.type = qualified_type::make(type_table.fundamental(from), cv_none);
    m.evaluation = constness::constant;
    m.value = kept_of(from, v);
    arithmetic_value(m, to);
    if (m.evaluation == constness::unknown)
        return analysis_result::unsupported;
    if (m.evaluation == constness::not_constant)
        return analysis_result::well_formed;
    v = value_of(to, m.value);
    constant = true;
    return analysis_result::well_formed;
}

/*
 * A binary operator on values: folding.cpp's rules, analysis's.  t is the
 * operands' type (a shift's right operand has right), result the
 * operation's (bool for a comparison).
 */
analysis_result semantic_analysis::evaluate_binary(
    uint8_t op, fundamental_type t, fundamental_type right,
    fundamental_type result, const constant_value &x, const constant_value &y,
    constant_value &out, bool &constant)
{
    constant = false;
    uint64_t value = 0;
    constness c =
        fold_binary(tokens.files.behavior, tokens.files.floating_point_pragma,
                    static_cast<preprocessing_op_or_punc>(op), t, right,
                    kept_of(t, x), kept_of(right, y), value);
    if (c == constness::unknown)
        return analysis_result::unsupported;
    if (c == constness::constant) {
        out = value_of(result, value);
        constant = true;
    }
    return analysis_result::well_formed;
}

void semantic_analysis::evaluate_analysed_expressions()
{
    /* (expressions are recorded in the order of their declarations, as
       dump_type_claims walks them) */
    size_t j = 0;
    for (analysed_expression &x : expressions) {
        while (j < declarations.size() && declarations[j].node != x.declaration)
            j++;
        if (j == declarations.size())
            break;
        expression_meaning &m = x.meaning;
        if (m.category != value_category::prvalue ||
            m.evaluation != constness::unknown || !m.node ||
            evaluated_type(m.type) == fundamental_type::none)
            continue;
        /* the evaluation of an expression of a declaration analysis did
           not finish may run a tree in part */
        if (declarations[j].result != analysis_result::well_formed)
            continue;
        constant_evaluation state;
        constant_value v;
        bool constant = false;
        if (evaluate_node(state, m.node, v, constant) !=
                analysis_result::well_formed ||
            !constant ||
            (v.kind != constant_value_kind::integer &&
             v.kind != constant_value_kind::floating))
            continue;
        evaluated_meaning(m, v);
    }
}

analysis_result semantic_analysis::evaluate_constant(node_index at,
                                                     uint32_t node,
                                                     constant_value &v,
                                                     bool &constant)
{
    constant = false;
    if (semantics[node].evaluation == constness::not_constant)
        return analysis_result::well_formed;
    constant_evaluation state;
    analysis_result r = evaluate_node(state, node, v, constant);
    if (r == analysis_result::well_formed && constant &&
        v.kind != constant_value_kind::integer &&
        v.kind != constant_value_kind::floating)
        r = analysis_result::unsupported;
    if (r == analysis_result::unsupported)
        return not_supported(at, "expr.const");
    return r;
}

/*
 * The object a glvalue designates: [expr.const]/5.8's "object whose
 * lifetime began within the evaluation of E", a local variable or
 * parameter of the invocation being evaluated.
 */
analysis_result semantic_analysis::evaluate_object(constant_evaluation &state,
                                                   uint32_t node,
                                                   uint32_t &object,
                                                   bool &constant)
{
    constant = false;
    const semantic_node &x = semantics[node];
    analysis_result r;
    switch (x.kind) {
    case semantic_kind::entity: {
        for (size_t i = state.objects.size(); i-- > 0;)
            if (state.objects[i].entity == x.a &&
                state.objects[i].frame == state.frame && state.frame) {
                object = static_cast<uint32_t>(i);
                constant = true;
                return analysis_result::well_formed;
            }
        /* an automatic variable whose lifetime did not begin within the
           evaluation: [expr.const]/5.8; a variable of static storage
           duration may not be modified (/5.16) */
        if (entity_table[x.a].automatic && x.a)
            return analysis_result::well_formed;
        return analysis_result::unsupported;
    }
    case semantic_kind::assignment: {
        r = evaluate_object(state, x.a, object, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        uint32_t target = object;
        constant_value y;
        r = evaluate_node(state, x.b, y, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        fundamental_type t = evaluated_type(semantics[x.a].type);
        if (t == fundamental_type::none)
            return analysis_result::unsupported;
        preprocessing_op_or_punc o =
            static_cast<preprocessing_op_or_punc>(x.op);
        if (o != preprocessing_op_or_punc::EQUAL) {
            /* [expr.ass]/6: "The behavior of an expression of the form E1 op=
               E2 is equivalent to E1 = E1 op E2 except that E1 is evaluated
               only once" */
            preprocessing_op_or_punc binary = compound_operator(o);
            fundamental_type c = evaluated_type(x.computation);
            fundamental_type right = evaluated_type(semantics[x.b].type);
            if (binary == preprocessing_op_or_punc::count ||
                c == fundamental_type::none || right == fundamental_type::none)
                return analysis_result::unsupported;
            constant_value old = state.objects[target].value;
            if (old.kind == constant_value_kind::none)
                return analysis_result::well_formed;
            r = evaluate_conversion(old, t, c, constant);
            if (r != analysis_result::well_formed || !constant)
                return r;
            if (binary != preprocessing_op_or_punc::LESS_LESS &&
                binary != preprocessing_op_or_punc::GREATER_GREATER &&
                right != c) {
                r = evaluate_conversion(y, right, c, constant);
                if (r != analysis_result::well_formed || !constant)
                    return r;
                right = c;
            }
            constant_value result;
            r = evaluate_binary(static_cast<uint8_t>(binary), c, right, c, old,
                                y, result, constant);
            if (r != analysis_result::well_formed || !constant)
                return r;
            r = evaluate_conversion(result, c, t, constant);
            if (r != analysis_result::well_formed || !constant)
                return r;
            y = result;
        }
        state.objects[target].value = y;
        object = target;
        constant = true;
        return analysis_result::well_formed;
    }
    case semantic_kind::increment: {
        increment_kind k = static_cast<increment_kind>(x.op);
        if (k != increment_kind::pre_increment &&
            k != increment_kind::pre_decrement)
            return analysis_result::unsupported;
        /* the operand evaluated again for its object: only one whose
           evaluation has no side effect */
        if (semantics[x.a].kind != semantic_kind::entity)
            return analysis_result::unsupported;
        constant_value updated;
        r = evaluate_node(state, node, updated, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        return evaluate_object(state, x.a, object, constant);
    }
    case semantic_kind::comma: {
        constant_value discarded;
        r = evaluate_node(state, x.a, discarded, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        return evaluate_object(state, x.b, object, constant);
    }
    default:
        return analysis_result::unsupported;
    }
}

/* [conv.lval]/3.4: "the object indicated by the glvalue is read" */
analysis_result semantic_analysis::evaluate_read(constant_evaluation &state,
                                                 uint32_t node,
                                                 constant_value &v,
                                                 bool &constant)
{
    constant = false;
    const semantic_node &x = semantics[node];
    if (x.type.cv() & cv_volatile)
        /* [expr.const]/5.8: "a non-volatile glvalue" */
        return analysis_result::well_formed;
    if (evaluated_type(x.type) == fundamental_type::none)
        return analysis_result::unsupported;
    if (x.kind == semantic_kind::entity) {
        bool in_frame = false;
        for (size_t i = state.objects.size(); i-- > 0 && state.frame;)
            if (state.objects[i].entity == x.a &&
                state.objects[i].frame == state.frame) {
                in_frame = true;
                break;
            }
        if (!in_frame) {
            /* [expr.const]/5.8.1: "a non-volatile glvalue that refers to an
               object that is usable in constant expressions" */
            const entity &e = entity_table[x.a];
            if (e.kind != entity_kind::variable ||
                e.usable == constness::unknown)
                return analysis_result::unsupported;
            if (e.usable == constness::not_constant) {
                if (e.automatic)
                    return analysis_result::well_formed;
                /* a variable not usable may be one analysis could not
                   classify (a later definition, a template's) */
                return analysis_result::unsupported;
            }
            fundamental_type t = evaluated_type(e.type);
            if (t == fundamental_type::none)
                return analysis_result::unsupported;
            if (fundamental_type_is_floating(t))
                v = evaluated_floating(floating_value(e.value));
            else
                v = evaluated_integer(
                    integer_value(tokens.files.behavior, t, e.value));
            constant = true;
            return analysis_result::well_formed;
        }
    }
    uint32_t object = 0;
    analysis_result r = evaluate_object(state, node, object, constant);
    if (r != analysis_result::well_formed || !constant)
        return r;
    v = state.objects[object].value;
    /* [expr.const]/5.8: the object's value must be one it was given; an
       indeterminate value is undefined behavior ([basic.indet]/2) */
    constant = v.kind != constant_value_kind::none;
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::evaluate_node(constant_evaluation &state,
                                                 uint32_t node,
                                                 constant_value &v,
                                                 bool &constant)
{
    constant = false;
    const semantic_node &x = semantics[node];
    analysis_result r;
    if (x.evaluation == constness::constant &&
        x.category == value_category::prvalue) {
        /* [basic.fundamental]: an integer or a double analysis folded */
        fundamental_type f = evaluated_type(x.type);
        if (f == fundamental_type::none)
            return analysis_result::unsupported;
        if (fundamental_type_is_integral(f))
            v = evaluated_integer(
                integer_value(tokens.files.behavior, f, x.value));
        else
            v = evaluated_floating(floating_value(x.value));
        constant = true;
        return analysis_result::well_formed;
    }
    if (x.category != value_category::prvalue &&
        x.kind != semantic_kind::increment) {
        /* a glvalue whose value is discarded: its evaluation */
        uint32_t object = 0;
        r = evaluate_object(state, node, object, constant);
        v = constant_value();
        v.kind = constant_value_kind::lvalue;
        return r;
    }
    switch (x.kind) {
    case semantic_kind::call:
        return evaluate_call(state, node, v, constant);
    case semantic_kind::conversion: {
        conversion_kind k = static_cast<conversion_kind>(x.op);
        if (k == conversion_kind::lvalue_to_rvalue)
            return evaluate_read(state, x.a, v, constant);
        if (k == conversion_kind::to_void) {
            r = evaluate_node(state, x.a, v, constant);
            v = constant_value();
            return r;
        }
        if (k != conversion_kind::integral && k != conversion_kind::floating &&
            k != conversion_kind::floating_integral &&
            k != conversion_kind::boolean)
            return analysis_result::unsupported;
        r = evaluate_node(state, x.a, v, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        fundamental_type to = evaluated_type(x.type);
        /* an integral conversion to bool, a cast's to an enumeration whose
           underlying type is bool (expr.cast.cpp): the truncated value of
           [conv.fpint]/1 "cannot be represented in the destination type"
           unless it is 0 or 1 */
        if (k == conversion_kind::integral && to == fundamental_type::bool_ &&
            v.kind == constant_value_kind::integer && v.integer > 1) {
            constant = false;
            return analysis_result::well_formed;
        }
        return evaluate_conversion(v, evaluated_type(semantics[x.a].type), to,
                                   constant);
    }
    case semantic_kind::unary: {
        fundamental_type t = evaluated_type(x.type);
        if (t == fundamental_type::none)
            return analysis_result::unsupported;
        r = evaluate_node(state, x.a, v, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        /* the operand is promoted to t, or a bool for ! */
        fundamental_type operand = evaluated_type(semantics[x.a].type);
        uint64_t value = 0;
        constness c = fold_unary(tokens.files.behavior,
                                 static_cast<preprocessing_op_or_punc>(x.op),
                                 operand, kept_of(operand, v), value);
        constant = false;
        if (c == constness::unknown)
            return analysis_result::unsupported;
        if (c == constness::constant) {
            v = value_of(t, value);
            constant = true;
        }
        return analysis_result::well_formed;
    }
    case semantic_kind::binary: {
        fundamental_type t = evaluated_type(semantics[x.a].type);
        fundamental_type right = evaluated_type(semantics[x.b].type);
        if (t == fundamental_type::none || right == fundamental_type::none ||
            evaluated_type(x.type) == fundamental_type::none)
            return analysis_result::unsupported;
        constant_value p, q;
        r = evaluate_node(state, x.a, p, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        r = evaluate_node(state, x.b, q, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        return evaluate_binary(x.op, t, right, evaluated_type(x.type), p, q, v,
                               constant);
    }
    /* [expr.log.and]/1: "the second operand is not evaluated if the first
       operand is false"; [expr.log.or]/1: "if the first operand evaluates
       to true" */
    case semantic_kind::logical_and:
    case semantic_kind::logical_or: {
        r = evaluate_node(state, x.a, v, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        bool first = v.integer != 0;
        if (first == (x.kind == semantic_kind::logical_or))
            return analysis_result::well_formed;
        return evaluate_node(state, x.b, v, constant);
    }
    /* [expr.cond]/1: "only one of the second and third expressions is
       evaluated" */
    case semantic_kind::conditional: {
        r = evaluate_node(state, x.a, v, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        if (v.integer != 0)
            return evaluate_node(state, x.b, v, constant);
        return evaluate_node(state, x.c, v, constant);
    }
    case semantic_kind::comma: {
        r = evaluate_node(state, x.a, v, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        return evaluate_node(state, x.b, v, constant);
    }
    /* [expr.post.incr]/1: "The value of the operand object is modified
       ... by adding 1 to it"; a postfix one's value is the old one
       ([expr.pre.incr] the same for the prefix ones).  The prefix ones are lvalues, read as evaluate_object's */
    case semantic_kind::increment: {
        const semantic_node &operand = semantics[x.a];
        fundamental_type t = evaluated_type(operand.type);
        if (t == fundamental_type::none || t == fundamental_type::bool_)
            return analysis_result::unsupported;
        uint32_t object = 0;
        r = evaluate_object(state, x.a, object, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        constant = false;
        constant_value old = state.objects[object].value;
        if (old.kind == constant_value_kind::none)
            return analysis_result::well_formed;
        increment_kind k = static_cast<increment_kind>(x.op);
        bool up = k == increment_kind::pre_increment ||
                  k == increment_kind::post_increment;
        /* [expr.pre.incr]/1: "The expression ++x is equivalent to x+=1":
           computed in the promoted type, converted back */
        fundamental_type promoted = promotion_of(t);
        constant_value updated = old;
        r = evaluate_conversion(updated, t, promoted, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        preprocessing_op_or_punc o = preprocessing_op_or_punc::MINUS;
        if (up)
            o = preprocessing_op_or_punc::PLUS;
        constant_value one = evaluated_integer(1);
        if (fundamental_type_is_floating(promoted))
            one = evaluated_floating(1);
        r = evaluate_binary(static_cast<uint8_t>(o), promoted, promoted,
                            promoted, updated, one, updated, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        r = evaluate_conversion(updated, promoted, t, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        state.objects[object].value = updated;
        v = old;
        if (k == increment_kind::pre_increment ||
            k == increment_kind::pre_decrement)
            v = updated;
        constant = true;
        return analysis_result::well_formed;
    }
    case semantic_kind::zero: {
        /* [dcl.init]/6: to zero-initialize a scalar, "the object is
           initialized to the value obtained by converting the integer
           literal 0 (zero) to T" */
        fundamental_type t = evaluated_type(x.type);
        if (t == fundamental_type::none)
            return analysis_result::unsupported;
        if (fundamental_type_is_floating(t))
            v = evaluated_floating(0);
        else
            v = evaluated_integer(0);
        constant = true;
        return analysis_result::well_formed;
    }
    /* pointers and objects: [expr.const]/11's permitted results */
    case semantic_kind::entity:
    case semantic_kind::address:
    case semantic_kind::indirection:
    case semantic_kind::subscript:
    case semantic_kind::member:
    case semantic_kind::string_literal:
    case semantic_kind::bound_member_function:
    case semantic_kind::assignment:
    /* initialization: [dcl.init], [class.ctor], [class.dtor] */
    case semantic_kind::initializer_list:
    case semantic_kind::bind_reference:
    case semantic_kind::construct:
    case semantic_kind::destroy:
    default:
        return analysis_result::unsupported;
    }
}

/*
 * [expr.call]/7 with [dcl.constexpr]: the invocation of a constexpr
 * function.  [expr.const]/5.2: "an invocation of a non-constexpr function"
 * is not a core constant expression; /5.3 "an invocation of an undefined
 * constexpr function" neither (one analysis has no body for is not known
 * to be undefined: unsupported).  The evaluation binds the parameters to
 * the arguments' values in a frame of state.objects and runs the body's
 * statements; [implimits]/2 bounds the depth.
 */
analysis_result semantic_analysis::evaluate_call(constant_evaluation &state,
                                                 uint32_t node,
                                                 constant_value &v,
                                                 bool &constant)
{
    constant = false;
    if (state.depth >= constexpr_invocation_limit)
        return analysis_result::unsupported;
    const semantic_node x = semantics[node];
    /* a member function's object ([class.this]): not modelled */
    if (x.b)
        return analysis_result::unsupported;
    uint32_t callee = x.a;
    if (semantics[callee].kind == semantic_kind::conversion &&
        static_cast<conversion_kind>(semantics[callee].op) ==
            conversion_kind::function_to_pointer)
        callee = semantics[callee].a;
    if (semantics[callee].kind != semantic_kind::entity)
        return analysis_result::unsupported;
    entity_index f = semantics[callee].a;
    const entity &e = entity_table[f];
    if (e.kind != entity_kind::function)
        return analysis_result::unsupported;
    if (!e.constexpr_)
        return analysis_result::well_formed;
    uint32_t body = e.definition;
    if (!body || semantics[body].kind != semantic_kind::function_body ||
        semantics[body].evaluation == constness::not_constant ||
        semantics[body].b || semantics[body].c ||
        semantics[body].count != x.count)
        return analysis_result::unsupported;
    /* [expr.call]/8: "The initialization of a parameter ... is
       indeterminately sequenced with respect to that of any other
       parameter": in order, as none of them can see another */
    owned_array<constant_value> arguments;
    const uint32_t *items = semantics.items(x.first);
    for (uint32_t i = 0; i < x.count; i++) {
        constant_value a;
        analysis_result r = evaluate_node(state, items[i], a, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        arguments.append(a);
    }
    size_t saved = state.objects.size();
    uint32_t caller = state.frame;
    state.frame = ++state.frames;
    state.depth++;
    const uint32_t *parameters = semantics.items(semantics[body].first);
    for (uint32_t i = 0; i < x.count; i++) {
        constant_evaluation::object o;
        o.entity = parameters[i];
        o.frame = state.frame;
        o.value = arguments[i];
        state.objects.append(o);
    }
    bool returned = false;
    constant_value result;
    analysis_result r = evaluate_statement(state, semantics[body].a, constant,
                                           returned, result);
    state.depth--;
    state.frame = caller;
    state.objects.resize(saved);
    if (r != analysis_result::well_formed || !constant)
        return r;
    /* [stmt.return]/2: "flowing off the end of a function other than main
       ... results in undefined behavior" for one that returns a value */
    if (!returned && fundamental_of(x.type) != fundamental_type::void_) {
        constant = false;
        return analysis_result::well_formed;
    }
    if (fundamental_of(x.type) != fundamental_type::void_ &&
        result.kind != constant_value_kind::integer &&
        result.kind != constant_value_kind::floating)
        return analysis_result::unsupported;
    v = result;
    constant = true;
    return analysis_result::well_formed;
}

/* a condition ([stmt.pre]/1): an expression or a declaration whose
   variable's value is converted to bool */
analysis_result
semantic_analysis::evaluate_condition(constant_evaluation &state, uint32_t node,
                                      bool &value, bool &constant)
{
    constant_value v;
    analysis_result r;
    if (semantics[node].kind == semantic_kind::condition_declaration) {
        bool returned = false;
        r = evaluate_statement(state, semantics[node].a, constant, returned, v);
        if (r != analysis_result::well_formed || !constant)
            return r;
        node = semantics[node].b;
    }
    r = evaluate_node(state, node, v, constant);
    if (r != analysis_result::well_formed || !constant)
        return r;
    if (v.kind != constant_value_kind::integer)
        return analysis_result::unsupported;
    value = v.integer != 0;
    return analysis_result::well_formed;
}

/*
 * [stmt.stmt] as the evaluation runs a constexpr function's body:
 * returned is set by a return statement ([stmt.return]/2), with its value
 * in result.  [dcl.constexpr]/3.4 lists what the body may not contain; the
 * rest runs as the abstract machine does.  Objects declared in a block or
 * a statement's condition end with it ([basic.stc.auto]/1).
 */
analysis_result
semantic_analysis::evaluate_statement(constant_evaluation &state, uint32_t node,
                                      bool &constant, bool &returned,
                                      constant_value &result)
{
    constant = false;
    returned = false;
    if (++state.full_expressions > constant_full_expression_limit)
        return analysis_result::unsupported;
    const semantic_node x = semantics[node];
    analysis_result r = analysis_result::well_formed;
    size_t saved = state.objects.size();
    switch (x.kind) {
    case semantic_kind::null_statement:
        constant = true;
        return r;
    case semantic_kind::block:
    case semantic_kind::declarations: {
        const uint32_t *items = semantics.items(x.first);
        constant = true;
        for (uint32_t i = 0; i < x.count; i++) {
            r = evaluate_statement(state, items[i], constant, returned, result);
            if (r != analysis_result::well_formed || !constant || returned ||
                state.jump != jump_none)
                break;
        }
        /* a declarations node's variables are in the enclosing block */
        if (x.kind == semantic_kind::block)
            state.objects.resize(saved);
        return r;
    }
    case semantic_kind::expression_statement: {
        constant_value discarded;
        return evaluate_node(state, x.a, discarded, constant);
    }
    case semantic_kind::declaration_statement: {
        const entity &e = entity_table[x.a];
        /* [dcl.constexpr]/3.4: "a definition of a variable of non-literal
           type or of static or thread storage duration" may not be in the
           body; references and objects of other types: not modelled */
        if (!e.automatic || evaluated_type(e.type) == fundamental_type::none)
            return analysis_result::unsupported;
        /* [basic.scope.pdecl]/1: the variable is in scope in its own
           initializer */
        constant_evaluation::object o;
        o.entity = x.a;
        o.frame = state.frame;
        uint32_t index = static_cast<uint32_t>(state.objects.size());
        state.objects.append(o);
        constant = true;
        if (!x.b)
            return r;
        constant_value v;
        r = evaluate_node(state, x.b, v, constant);
        if (r != analysis_result::well_formed || !constant)
            return r;
        state.objects[index].value = v;
        return r;
    }
    case semantic_kind::if_statement: {
        if (x.d) {
            r = evaluate_statement(state, x.d, constant, returned, result);
            if (r != analysis_result::well_formed || !constant)
                return r;
        }
        bool value = false;
        r = evaluate_condition(state, x.a, value, constant);
        if (r == analysis_result::well_formed && constant) {
            if (value)
                r = evaluate_statement(state, x.b, constant, returned, result);
            else if (x.c)
                r = evaluate_statement(state, x.c, constant, returned, result);
        }
        state.objects.resize(saved);
        return r;
    }
    case semantic_kind::while_statement:
    case semantic_kind::do_statement:
    case semantic_kind::for_statement: {
        uint32_t condition = x.a, body = x.b, expression = 0;
        /* [stmt.do]/2: "The test takes place after each execution of the
           statement." */
        bool skip_test = false;
        if (x.kind == semantic_kind::do_statement) {
            condition = x.b;
            body = x.a;
            skip_test = true;
        } else if (x.kind == semantic_kind::for_statement) {
            condition = x.b;
            body = x.d;
            expression = x.c;
            if (x.a) {
                r = evaluate_statement(state, x.a, constant, returned, result);
                if (r != analysis_result::well_formed || !constant)
                    return r;
            }
        }
        size_t iteration = state.objects.size();
        constant = true;
        for (;;) {
            if (condition && !skip_test) {
                bool value = false;
                r = evaluate_condition(state, condition, value, constant);
                if (r != analysis_result::well_formed || !constant || !value)
                    break;
            }
            skip_test = false;
            r = evaluate_statement(state, body, constant, returned, result);
            state.objects.resize(iteration);
            if (r != analysis_result::well_formed || !constant || returned)
                break;
            uint8_t jump = state.jump;
            state.jump = jump_none;
            if (jump == jump_break)
                break;
            if (expression) {
                constant_value discarded;
                r = evaluate_node(state, expression, discarded, constant);
                if (r != analysis_result::well_formed || !constant)
                    break;
            }
            if (++state.full_expressions > constant_full_expression_limit) {
                r = analysis_result::unsupported;
                break;
            }
        }
        state.objects.resize(saved);
        return r;
    }
    case semantic_kind::break_statement:
        state.jump = jump_break;
        constant = true;
        return r;
    case semantic_kind::continue_statement:
        state.jump = jump_continue;
        constant = true;
        return r;
    case semantic_kind::return_statement:
        result = constant_value();
        constant = true;
        if (x.a)
            r = evaluate_node(state, x.a, result, constant);
        returned = true;
        return r;
    case semantic_kind::identifier_label:
        return evaluate_statement(state, x.a, constant, returned, result);
    /* case labels: a jump into the substatement, not modelled */
    case semantic_kind::switch_statement:
    case semantic_kind::case_label:
    case semantic_kind::default_label:
    /* [dcl.constexpr]/3.4: "a goto statement" may not be in the body */
    case semantic_kind::goto_statement:
    default:
        return analysis_result::unsupported;
    }
}

} // namespace cplusplus
