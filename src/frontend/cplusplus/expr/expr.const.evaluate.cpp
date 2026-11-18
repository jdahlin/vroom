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

bool is_value(const constant_value &v)
{
    return v.kind == constant_value_kind::integer ||
           v.kind == constant_value_kind::floating;
}

/* the end of a scope ([basic.stc.auto]/1): its objects and their elements
   go */
/* evaluate_initialization's init for an element no initializer list
   gives: zero-initialized */
constexpr uint32_t zero_initializer = UINT32_MAX;

/* a read of an indeterminate value ([basic.indet]/2) or of a volatile
   object ([expr.const]/5.8): not constant, but the check of a body for a
   potential constant expression does not count it, as clang does not */
template <class state_type> constness undefined_value(const state_type &state)
{
    if (state.potential)
        return constness::unknown;
    return constness::not_constant;
}

template <class state_type>
void restore(state_type &state, size_t objects, size_t elements)
{
    state.objects.resize(objects);
    state.elements.resize(elements);
    state.element_read_only.resize(elements);
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
    constant_value r;
    if (fundamental_type_is_floating(t)) {
        r.kind = constant_value_kind::floating;
        r.floating = floating_value(kept);
        return r;
    }
    r.kind = constant_value_kind::integer;
    r.integer = static_cast<unsigned __int128>(
        integer_value(tokens.files.behavior, t, kept));
    return r;
}

void semantic_analysis::evaluated_meaning(expression_meaning &m,
                                          const constant_value &v)
{
    m.evaluation = constness::constant;
    m.value = kept_of(evaluated_type(m.type), v);
}

/* [conv.integral], [conv.double], [conv.fpint], [conv.bool]: the rules
   analysis folds with, on a value */
constness semantic_analysis::evaluate_conversion(constant_value &v,
                                                 fundamental_type from,
                                                 fundamental_type to)
{
    if (from == fundamental_type::none || to == fundamental_type::none)
        return constness::unknown;
    expression_meaning m;
    m.type = qualified_type::make(type_table.fundamental(from), cv_none);
    m.evaluation = constness::constant;
    m.value = kept_of(from, v);
    arithmetic_value(m, to);
    if (m.evaluation == constness::constant)
        v = value_of(to, m.value);
    return m.evaluation;
}

/*
 * A binary operator on values: folding.cpp's rules, analysis's.  t is the
 * operands' type (a shift's right operand has right), result the
 * operation's (bool for a comparison).
 */
constness semantic_analysis::evaluate_binary(uint8_t op, fundamental_type t,
                                             fundamental_type right,
                                             fundamental_type result,
                                             const constant_value &x,
                                             const constant_value &y,
                                             constant_value &out)
{
    uint64_t value = 0;
    constness c =
        fold_binary(tokens.files.behavior, tokens.files.floating_point_pragma,
                    static_cast<preprocessing_op_or_punc>(op), t, right,
                    kept_of(t, x), kept_of(right, y), value);
    if (c == constness::constant)
        out = value_of(result, value);
    return c;
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
        /* a function defined after the expression is undefined at its
           point ([expr.const]/5.3) */
        if (x.at.is_token())
            state.defined_before = x.at.index();
        else
            state.defined_before = tree[x.at.index()].first_token;
        constant_value v;
        if (evaluate_node(state, m.node, v) == constness::constant &&
            is_value(v))
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
    constness c = evaluate_node(state, node, v);
    if (c == constness::constant && !is_value(v))
        c = constness::unknown;
    if (c == constness::unknown)
        return not_supported(at, "expr.const");
    constant = c == constness::constant;
    return analysis_result::well_formed;
}

/* the object of entity e in the invocation being evaluated, or none */
uint32_t semantic_analysis::frame_object(const constant_evaluation &state,
                                         entity_index e) const
{
    if (!state.frame)
        return no_frame_object;
    for (size_t i = state.objects.size(); i-- > 0;)
        if (state.objects[i].entity == e &&
            state.objects[i].frame == state.frame)
            return static_cast<uint32_t>(i);
    return no_frame_object;
}

constant_value &semantic_analysis::place_value(constant_evaluation &state,
                                               uint32_t place)
{
    if (place & element_place)
        return state.elements[place & ~element_place];
    return state.objects[place].value;
}

bool semantic_analysis::place_read_only(const constant_evaluation &state,
                                        uint32_t place) const
{
    if (place & element_place)
        return state.element_read_only[place & ~element_place] != 0;
    return state.objects[place].read_only;
}

/*
 * [dcl.init]/7: an object of type t initialized by init: a scalar by an
 * expression or zero ([dcl.init]/6), an array element by element from an
 * initializer list, the elements it does not give zero ([dcl.init.aggr]/5).
 * No initializer (0) leaves the value indeterminate ([basic.indet]/1);
 * zero_initializer stands for an element no list gives.
 */
constness semantic_analysis::evaluate_initialization(constant_evaluation &state,
                                                     qualified_type t,
                                                     uint32_t init,
                                                     bool read_only,
                                                     constant_value &v)
{
    v = constant_value();
    bool zero = init == zero_initializer ||
                (init && semantics[init].kind == semantic_kind::zero);
    const struct type record = type_table[t.unqualified()];
    if (record.kind != type_kind::array) {
        fundamental_type f = evaluated_type(t);
        if (f == fundamental_type::none)
            return constness::unknown;
        if (zero)
            v = value_of(f, 0);
        else if (init)
            return evaluate_node(state, init, v);
        return constness::constant;
    }
    if (!record.bound_known || record.bound > constant_full_expression_limit)
        return constness::unknown;
    uint32_t given = 0;
    const uint32_t *items = nullptr;
    if (init && !zero) {
        /* a string literal, a copy: not modelled */
        if (semantics[init].kind != semantic_kind::initializer_list)
            return constness::unknown;
        given = semantics[init].count;
        items = semantics.items(semantics[init].first);
    }
    if (given > record.bound)
        return constness::unknown;
    uint32_t first = static_cast<uint32_t>(state.elements.size());
    uint32_t count = static_cast<uint32_t>(record.bound);
    for (uint32_t i = 0; i < count; i++) {
        state.elements.append(constant_value());
        state.element_read_only.append(read_only);
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t element = 0;
        if (i < given)
            element = items[i];
        else if (init)
            element = zero_initializer;
        constant_value e;
        constness c = evaluate_initialization(state, record.operand, element,
                                              read_only, e);
        if (c != constness::constant)
            return c;
        state.elements[first + i] = e;
    }
    v.kind = constant_value_kind::array;
    v.first = first;
    v.count = count;
    return constness::constant;
}

/*
 * The object a glvalue designates: [expr.const]/5.8's "object whose
 * lifetime began within the evaluation of E", a local variable or
 * parameter of the invocation being evaluated.
 */
constness semantic_analysis::evaluate_object(constant_evaluation &state,
                                             uint32_t node, uint32_t &object)
{
    const semantic_node &x = semantics[node];
    constness c;
    switch (x.kind) {
    case semantic_kind::entity: {
        object = frame_object(state, x.a);
        if (object != no_frame_object)
            return constness::constant;
        const entity &e = entity_table[x.a];
        if (x.a && e.kind == entity_kind::variable &&
            (!e.automatic || e.constexpr_) &&
            type_table[e.type.unqualified()].kind == type_kind::array) {
            /* [expr.const]/4: a constexpr array (a local one too, read
               outside its function's evaluation) is usable in constant
               expressions, its elements read as the evaluation needs
               them; another is not ([expr.const]/5.8) */
            if (!e.constexpr_ || !e.definition)
                return constness::unknown;
            for (size_t i = 0; i < state.objects.size(); i++)
                if (state.objects[i].entity == x.a && !state.objects[i].frame) {
                    object = static_cast<uint32_t>(i);
                    return constness::constant;
                }
            constant_evaluation::object o;
            o.entity = x.a;
            o.read_only = true;
            c = evaluate_initialization(state, e.type, e.definition, true,
                                        o.value);
            if (c != constness::constant)
                return c;
            object = static_cast<uint32_t>(state.objects.size());
            state.objects.append(o);
            return constness::constant;
        }
        /* an automatic variable whose lifetime did not begin within the
           evaluation: [expr.const]/5.8; a variable of static storage
           duration may not be modified (/5.16) */
        if (entity_table[x.a].automatic && x.a)
            return constness::not_constant;
        return constness::unknown;
    }
    /* [expr.sub]/1: "The expression E1[E2] is identical (by definition) to
       *((E1)+(E2))", an element of the array E1 converted from */
    case semantic_kind::subscript: {
        const semantic_node &pointer = semantics[x.a];
        if (pointer.kind != semantic_kind::conversion ||
            static_cast<conversion_kind>(pointer.op) !=
                conversion_kind::array_to_pointer)
            return constness::unknown;
        uint32_t array = 0;
        c = evaluate_object(state, pointer.a, array);
        if (c != constness::constant)
            return c;
        constant_value index;
        c = evaluate_node(state, x.b, index);
        if (c != constness::constant)
            return c;
        constant_value a = place_value(state, array);
        if (a.kind != constant_value_kind::array ||
            index.kind != constant_value_kind::integer)
            return constness::unknown;
        /* [expr.add]/4: an index outside the array is undefined, and the
           one past its end designates no element ([expr.unary.op]/1) */
        __int128 i = static_cast<__int128>(index.integer);
        if (i < 0 || i >= a.count)
            return constness::not_constant;
        object = element_place | (a.first + static_cast<uint32_t>(i));
        return constness::constant;
    }
    case semantic_kind::assignment: {
        c = evaluate_object(state, x.a, object);
        if (c != constness::constant)
            return c;
        uint32_t target = object;
        /* [expr.const]/5.16: the modification of an object whose lifetime
           did not begin within the evaluation */
        if (place_read_only(state, target))
            return constness::not_constant;
        constant_value y;
        c = evaluate_node(state, x.b, y);
        if (c != constness::constant)
            return c;
        fundamental_type t = evaluated_type(semantics[x.a].type);
        if (t == fundamental_type::none)
            return constness::unknown;
        preprocessing_op_or_punc o =
            static_cast<preprocessing_op_or_punc>(x.op);
        if (o != preprocessing_op_or_punc::EQUAL) {
            /* [expr.ass]/6: "The behavior of an expression of the form E1 op=
               E2 is equivalent to E1 = E1 op E2 except that E1 is evaluated
               only once" */
            preprocessing_op_or_punc binary = compound_assignment_operator(o);
            fundamental_type computation = evaluated_type(x.computation);
            fundamental_type right = evaluated_type(semantics[x.b].type);
            if (binary == preprocessing_op_or_punc::count ||
                computation == fundamental_type::none ||
                right == fundamental_type::none)
                return constness::unknown;
            if (!(target & element_place) && state.objects[target].opaque)
                return constness::unknown;
            constant_value old = place_value(state, target);
            if (old.kind == constant_value_kind::none)
                return undefined_value(state);
            c = evaluate_conversion(old, t, computation);
            if (c != constness::constant)
                return c;
            if (binary != preprocessing_op_or_punc::LESS_LESS &&
                binary != preprocessing_op_or_punc::GREATER_GREATER &&
                right != computation) {
                c = evaluate_conversion(y, right, computation);
                if (c != constness::constant)
                    return c;
                right = computation;
            }
            constant_value result;
            c = evaluate_binary(static_cast<uint8_t>(binary), computation,
                                right, computation, old, y, result);
            if (c != constness::constant)
                return c;
            c = evaluate_conversion(result, computation, t);
            if (c != constness::constant)
                return c;
            y = result;
        }
        place_value(state, target) = y;
        object = target;
        return constness::constant;
    }
    case semantic_kind::increment: {
        increment_kind k = static_cast<increment_kind>(x.op);
        if (k != increment_kind::pre_increment &&
            k != increment_kind::pre_decrement)
            return constness::unknown;
        /* the operand evaluated again for its object: only one whose
           evaluation has no side effect */
        if (semantics[x.a].kind != semantic_kind::entity)
            return constness::unknown;
        constant_value updated;
        c = evaluate_node(state, node, updated);
        if (c != constness::constant)
            return c;
        return evaluate_object(state, x.a, object);
    }
    case semantic_kind::comma: {
        constant_value discarded;
        c = evaluate_node(state, x.a, discarded);
        if (c != constness::constant)
            return c;
        return evaluate_object(state, x.b, object);
    }
    default:
        return constness::unknown;
    }
}

/* [conv.lval]/3.4: "the object indicated by the glvalue is read" */
constness semantic_analysis::evaluate_read(constant_evaluation &state,
                                           uint32_t node, constant_value &v)
{
    const semantic_node &x = semantics[node];
    if (x.type.cv() & cv_volatile)
        /* [expr.const]/5.8: "a non-volatile glvalue" */
        return undefined_value(state);
    if (evaluated_type(x.type) == fundamental_type::none)
        return constness::unknown;
    if (x.kind == semantic_kind::entity &&
        frame_object(state, x.a) == no_frame_object) {
        /* [expr.const]/5.8.1: "a non-volatile glvalue that refers to an
           object that is usable in constant expressions" */
        const entity &e = entity_table[x.a];
        if (e.kind != entity_kind::variable || e.usable == constness::unknown)
            return constness::unknown;
        if (e.usable == constness::not_constant) {
            /* [expr.const]/4: only a const or constexpr object may be
               usable; another variable (a reference, a const one) may be
               one analysis could not classify (a later definition, a
               template's) */
            if (e.automatic ||
                (!e.constexpr_ && !(e.type.cv() & cv_const) &&
                 evaluated_type(e.type) != fundamental_type::none))
                return constness::not_constant;
            return constness::unknown;
        }
        fundamental_type t = evaluated_type(e.type);
        if (t == fundamental_type::none)
            return constness::unknown;
        v = value_of(t, e.value);
        return constness::constant;
    }
    uint32_t object = 0;
    constness c = evaluate_object(state, node, object);
    if (c != constness::constant)
        return c;
    if (!(object & element_place) && state.objects[object].opaque)
        return constness::unknown;
    v = place_value(state, object);
    /* [expr.const]/5.8: the object's value must be one it was given; an
       indeterminate value is undefined behavior ([basic.indet]/2) */
    if (v.kind == constant_value_kind::none)
        return undefined_value(state);
    return constness::constant;
}

constness semantic_analysis::evaluate_node(constant_evaluation &state,
                                           uint32_t node, constant_value &v)
{
    const semantic_node &x = semantics[node];
    constness c;
    if (x.evaluation == constness::constant &&
        x.category == value_category::prvalue) {
        /* [basic.fundamental]: an integer or a double analysis folded */
        fundamental_type f = evaluated_type(x.type);
        if (f == fundamental_type::none)
            return constness::unknown;
        v = value_of(f, x.value);
        return constness::constant;
    }
    if (x.category != value_category::prvalue &&
        x.kind != semantic_kind::increment) {
        /* a glvalue whose value is discarded: its evaluation */
        uint32_t object = 0;
        c = evaluate_object(state, node, object);
        v = constant_value();
        v.kind = constant_value_kind::lvalue;
        return c;
    }
    switch (x.kind) {
    case semantic_kind::call:
        return evaluate_call(state, node, v);
    case semantic_kind::conversion: {
        conversion_kind k = static_cast<conversion_kind>(x.op);
        if (k == conversion_kind::lvalue_to_rvalue)
            return evaluate_read(state, x.a, v);
        if (k == conversion_kind::to_void) {
            c = evaluate_node(state, x.a, v);
            v = constant_value();
            return c;
        }
        if (k != conversion_kind::integral && k != conversion_kind::floating &&
            k != conversion_kind::floating_integral &&
            k != conversion_kind::boolean)
            return constness::unknown;
        c = evaluate_node(state, x.a, v);
        if (c != constness::constant)
            return c;
        fundamental_type to = evaluated_type(x.type);
        /* an integral conversion to bool, a cast's to an enumeration whose
           underlying type is bool (expr.cast.cpp): the truncated value of
           [conv.fpint]/1 "cannot be represented in the destination type"
           unless it is 0 or 1 */
        if (k == conversion_kind::integral && to == fundamental_type::bool_ &&
            v.kind == constant_value_kind::integer && v.integer > 1)
            return constness::not_constant;
        return evaluate_conversion(v, evaluated_type(semantics[x.a].type), to);
    }
    case semantic_kind::unary: {
        fundamental_type t = evaluated_type(x.type);
        if (t == fundamental_type::none)
            return constness::unknown;
        c = evaluate_node(state, x.a, v);
        if (c != constness::constant)
            return c;
        /* the operand is promoted to t, or a bool for ! */
        fundamental_type operand = evaluated_type(semantics[x.a].type);
        uint64_t value = 0;
        c = fold_unary(tokens.files.behavior,
                       static_cast<preprocessing_op_or_punc>(x.op), operand,
                       kept_of(operand, v), value);
        if (c == constness::constant)
            v = value_of(t, value);
        return c;
    }
    case semantic_kind::binary: {
        fundamental_type t = evaluated_type(semantics[x.a].type);
        fundamental_type right = evaluated_type(semantics[x.b].type);
        if (t == fundamental_type::none || right == fundamental_type::none ||
            evaluated_type(x.type) == fundamental_type::none)
            return constness::unknown;
        constant_value p, q;
        c = evaluate_node(state, x.a, p);
        if (c == constness::unknown && state.potential &&
            evaluate_node(state, x.b, q) == constness::not_constant)
            return constness::not_constant;
        if (c != constness::constant)
            return c;
        c = evaluate_node(state, x.b, q);
        if (c != constness::constant)
            return c;
        return evaluate_binary(x.op, t, right, evaluated_type(x.type), p, q, v);
    }
    /* [expr.log.and]/1: "the second operand is not evaluated if the first
       operand is false"; [expr.log.or]/1: "if the first operand evaluates
       to true" */
    case semantic_kind::logical_and:
    case semantic_kind::logical_or: {
        c = evaluate_node(state, x.a, v);
        if (c != constness::constant)
            return c;
        bool first = v.integer != 0;
        if (first == (x.kind == semantic_kind::logical_or))
            return constness::constant;
        return evaluate_node(state, x.b, v);
    }
    /* [expr.cond]/1: "only one of the second and third expressions is
       evaluated" */
    case semantic_kind::conditional: {
        c = evaluate_node(state, x.a, v);
        if (c == constness::unknown && state.potential) {
            constant_value arm;
            if (evaluate_node(state, x.b, arm) == constness::not_constant &&
                evaluate_node(state, x.c, arm) == constness::not_constant)
                return constness::not_constant;
            return c;
        }
        if (c != constness::constant)
            return c;
        if (v.integer != 0)
            return evaluate_node(state, x.b, v);
        return evaluate_node(state, x.c, v);
    }
    case semantic_kind::comma: {
        c = evaluate_node(state, x.a, v);
        if (c != constness::constant)
            return c;
        return evaluate_node(state, x.b, v);
    }
    /* [expr.post.incr]/1: "The value of the operand object is modified
       ... by adding 1 to it"; a postfix one's value is the old one
       ([expr.pre.incr] the same for the prefix ones).  The prefix ones are
       lvalues, read as evaluate_object's */
    case semantic_kind::increment: {
        const semantic_node &operand = semantics[x.a];
        fundamental_type t = evaluated_type(operand.type);
        if (t == fundamental_type::none || t == fundamental_type::bool_)
            return constness::unknown;
        uint32_t object = 0;
        c = evaluate_object(state, x.a, object);
        if (c != constness::constant)
            return c;
        if (place_read_only(state, object))
            return constness::not_constant;
        if (!(object & element_place) && state.objects[object].opaque)
            return constness::unknown;
        constant_value old = place_value(state, object);
        if (old.kind == constant_value_kind::none)
            return undefined_value(state);
        increment_kind k = static_cast<increment_kind>(x.op);
        bool up = k == increment_kind::pre_increment ||
                  k == increment_kind::post_increment;
        /* [expr.pre.incr]/1: "The expression ++x is equivalent to x+=1":
           computed in the promoted type, converted back */
        fundamental_type promoted = promotion_of(t);
        constant_value updated = old;
        c = evaluate_conversion(updated, t, promoted);
        if (c != constness::constant)
            return c;
        preprocessing_op_or_punc o = preprocessing_op_or_punc::MINUS;
        if (up)
            o = preprocessing_op_or_punc::PLUS;
        uint64_t one = 1;
        if (fundamental_type_is_floating(promoted))
            one = floating_kept(1);
        c = evaluate_binary(static_cast<uint8_t>(o), promoted, promoted,
                            promoted, updated, value_of(promoted, one),
                            updated);
        if (c != constness::constant)
            return c;
        c = evaluate_conversion(updated, promoted, t);
        if (c != constness::constant)
            return c;
        place_value(state, object) = updated;
        v = old;
        if (k == increment_kind::pre_increment ||
            k == increment_kind::pre_decrement)
            v = updated;
        return constness::constant;
    }
    case semantic_kind::zero: {
        /* [dcl.init]/6: to zero-initialize a scalar, "the object is
           initialized to the value obtained by converting the integer
           literal 0 (zero) to T" (0 kept is 0 in every type) */
        fundamental_type t = evaluated_type(x.type);
        if (t == fundamental_type::none)
            return constness::unknown;
        v = value_of(t, 0);
        return constness::constant;
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
        return constness::unknown;
    }
}

/*
 * [expr.call]/7 with [dcl.constexpr]: the invocation of a constexpr
 * function.  [expr.const]/5.2: "an invocation of a non-constexpr function"
 * is not a core constant expression; /5.3 "an invocation of an undefined
 * constexpr function" neither (one analysis has no body for, or whose body
 * comes after the expression evaluated, is not known to be undefined:
 * unknown).  The evaluation binds the parameters to the arguments' values
 * in a frame of state.objects and runs the body's statements;
 * [implimits]/2 bounds the depth.
 */
constness semantic_analysis::evaluate_call(constant_evaluation &state,
                                           uint32_t node, constant_value &v)
{
    if (state.depth >= constexpr_invocation_limit)
        return constness::unknown;
    const semantic_node x = semantics[node];
    /* a member function's object ([class.this]): not modelled */
    if (x.b)
        return constness::unknown;
    uint32_t callee = x.a;
    if (semantics[callee].kind == semantic_kind::conversion &&
        static_cast<conversion_kind>(semantics[callee].op) ==
            conversion_kind::function_to_pointer)
        callee = semantics[callee].a;
    if (semantics[callee].kind != semantic_kind::entity)
        return constness::unknown;
    entity_index f = semantics[callee].a;
    const entity &e = entity_table[f];
    if (e.kind != entity_kind::function)
        return constness::unknown;
    if (!e.constexpr_) {
        /* (a library builtin, declared with C language linkage, may be
           one the evaluator does not know clang evaluates) */
        if (state.potential && e.c_language_linkage)
            return constness::unknown;
        return constness::not_constant;
    }
    uint32_t body = e.definition;
    if (!body || semantics[body].kind != semantic_kind::function_body ||
        semantics[body].evaluation == constness::not_constant ||
        semantics[body].b || semantics[body].c ||
        semantics[body].count != x.count ||
        (state.defined_before && semantics[body].at > state.defined_before))
        return constness::unknown;
    /* [expr.call]/8: "The initialization of a parameter ... is
       indeterminately sequenced with respect to that of any other
       parameter": in order, as none of them can see another.  The
       arguments are evaluated in the caller's frame and kept as the
       parameters of the new one. */
    size_t saved = state.objects.size();
    size_t saved_elements = state.elements.size();
    uint32_t frame = state.frames + 1;
    const uint32_t *items = semantics.items(x.first);
    const uint32_t *parameters = semantics.items(semantics[body].first);
    constness arguments = constness::constant;
    for (uint32_t i = 0; i < x.count; i++) {
        constant_evaluation::object o;
        constness c = evaluate_node(state, items[i], o.value);
        if (c == constness::unknown && state.potential) {
            arguments = c;
            continue;
        }
        if (c != constness::constant) {
            restore(state, saved, saved_elements);
            return c;
        }
        o.entity = parameters[i];
        o.frame = frame;
        state.objects.append(o);
    }
    if (arguments != constness::constant) {
        restore(state, saved, saved_elements);
        return arguments;
    }
    uint32_t caller = state.frame;
    state.frames = frame;
    state.frame = frame;
    state.depth++;
    bool returned = false;
    constant_value result;
    constness c =
        evaluate_statement(state, semantics[body].a, returned, result);
    state.depth--;
    state.frame = caller;
    restore(state, saved, saved_elements);
    if (c != constness::constant)
        return c;
    if (fundamental_of(x.type) == fundamental_type::void_) {
        v = result;
        return constness::constant;
    }
    /* [stmt.return]/2: "flowing off the end of a function other than main
       ... results in undefined behavior" for one that returns a value */
    if (!returned)
        return constness::not_constant;
    if (!is_value(result))
        return constness::unknown;
    v = result;
    return constness::constant;
}

/* a condition ([stmt.pre]/1): an expression or a declaration whose
   variable's value is converted to bool, or a switch's promoted type */
constness semantic_analysis::evaluate_condition(constant_evaluation &state,
                                                uint32_t node,
                                                constant_value &v)
{
    constness c;
    if (semantics[node].kind == semantic_kind::condition_declaration) {
        bool returned = false;
        c = evaluate_statement(state, semantics[node].a, returned, v);
        if (c != constness::constant)
            return c;
        node = semantics[node].b;
    }
    c = evaluate_node(state, node, v);
    if (c != constness::constant)
        return c;
    if (v.kind != constant_value_kind::integer)
        return constness::unknown;
    return constness::constant;
}

uint32_t semantic_analysis::switch_target(uint32_t node, uint64_t value,
                                          bool &found) const
{
    if (!node)
        return 0;
    const semantic_node &x = semantics[node];
    uint32_t target = 0, other = 0;
    switch (x.kind) {
    case semantic_kind::block:
    case semantic_kind::declarations: {
        const uint32_t *items = semantics.items(x.first);
        for (uint32_t i = 0; i < x.count && !found; i++) {
            other = switch_target(items[i], value, found);
            if (other && (found || !target))
                target = other;
        }
        return target;
    }
    case semantic_kind::case_label:
        if (x.value == value) {
            found = true;
            return node;
        }
        return switch_target(x.a, value, found);
    case semantic_kind::default_label:
        target = switch_target(x.a, value, found);
        if (found)
            return target;
        return node;
    case semantic_kind::identifier_label:
    case semantic_kind::do_statement:
        return switch_target(x.a, value, found);
    case semantic_kind::while_statement:
        return switch_target(x.b, value, found);
    case semantic_kind::for_statement:
        return switch_target(x.d, value, found);
    case semantic_kind::if_statement:
        target = switch_target(x.b, value, found);
        if (found)
            return target;
        other = switch_target(x.c, value, found);
        if (other && (found || !target))
            target = other;
        return target;
    /* a nested switch statement's labels are its own */
    default:
        return 0;
    }
}

/*
 * [stmt.stmt] as the evaluation runs a constexpr function's body:
 * returned is set by a return statement ([stmt.return]/2), with its value
 * in result.  [dcl.constexpr]/3.5 lists what the body may not contain; the
 * rest runs as the abstract machine does.  Objects declared in a block or
 * a statement's condition end with it ([basic.stc.auto]/1).
 */
constness semantic_analysis::evaluate_statement(constant_evaluation &state,
                                                uint32_t node, bool &returned,
                                                constant_value &result)
{
    returned = false;
    if (++state.full_expressions > constant_full_expression_limit)
        return constness::unknown;
    const semantic_node x = semantics[node];
    constness c = constness::constant;
    size_t saved = state.objects.size();
    size_t saved_elements = state.elements.size();
    /* [stmt.switch]/5: control passes to the label: the statements before
       it are not executed; the declarations among them still declare
       their variables ([stmt.dcl]/2 rules out jumping past an
       initialization) */
    if (state.seek) {
        switch (x.kind) {
        case semantic_kind::block:
        case semantic_kind::declarations:
        case semantic_kind::declaration_statement:
            break;
        case semantic_kind::case_label:
        case semantic_kind::default_label:
        case semantic_kind::identifier_label:
            if (node == state.seek)
                state.seek = 0;
            return evaluate_statement(state, x.a, returned, result);
        /* a label inside another statement: switch_statement answers
           unknown when the seek is not over */
        default:
            return c;
        }
    }
    switch (x.kind) {
    case semantic_kind::null_statement:
        return c;
    case semantic_kind::block:
    case semantic_kind::declarations: {
        const uint32_t *items = semantics.items(x.first);
        for (uint32_t i = 0; i < x.count; i++) {
            c = evaluate_statement(state, items[i], returned, result);
            if (c != constness::constant || returned || state.jump != jump_none)
                break;
        }
        /* a declarations node's variables are in the enclosing block */
        if (x.kind == semantic_kind::block)
            restore(state, saved, saved_elements);
        return c;
    }
    case semantic_kind::expression_statement: {
        constant_value discarded;
        return evaluate_node(state, x.a, discarded);
    }
    case semantic_kind::declaration_statement: {
        const entity &e = entity_table[x.a];
        /* [dcl.constexpr]/3.5.3: "a definition of a variable of non-literal
           type or of static or thread storage duration" may not be in the
           body; references and objects of other types: not modelled */
        /* [expr.const]/5.2 (C++23): "control flow that passes through a
           declaration of a block variable with static or thread storage
           duration, unless that variable is usable in constant
           expressions" */
        if (!e.automatic) {
            if (!e.constexpr_ && !(e.type.cv() & cv_const))
                return constness::not_constant;
            return constness::unknown;
        }
        /* [basic.scope.pdecl]/1: the variable is in scope in its own
           initializer */
        constant_evaluation::object o;
        o.entity = x.a;
        o.frame = state.frame;
        uint32_t index = static_cast<uint32_t>(state.objects.size());
        state.objects.append(o);
        uint32_t init = x.b;
        if (state.seek)
            init = 0;
        constant_value v;
        c = evaluate_initialization(state, e.type, init, false, v);
        if (c == constness::constant)
            state.objects[index].value = v;
        return c;
    }
    case semantic_kind::if_statement: {
        if (x.d) {
            c = evaluate_statement(state, x.d, returned, result);
            if (c != constness::constant)
                return c;
        }
        constant_value value;
        c = evaluate_condition(state, x.a, value);
        if (c == constness::constant) {
            if (value.integer != 0)
                c = evaluate_statement(state, x.b, returned, result);
            else if (x.c)
                c = evaluate_statement(state, x.c, returned, result);
        }
        restore(state, saved, saved_elements);
        return c;
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
                c = evaluate_statement(state, x.a, returned, result);
                if (c != constness::constant)
                    return c;
            }
        }
        size_t iteration = state.objects.size();
        size_t iteration_elements = state.elements.size();
        for (;;) {
            if (condition && !skip_test) {
                constant_value value;
                c = evaluate_condition(state, condition, value);
                if (c != constness::constant || value.integer == 0)
                    break;
            }
            skip_test = false;
            c = evaluate_statement(state, body, returned, result);
            restore(state, iteration, iteration_elements);
            if (c != constness::constant || returned)
                break;
            uint8_t jump = state.jump;
            state.jump = jump_none;
            if (jump == jump_break)
                break;
            if (expression) {
                constant_value discarded;
                c = evaluate_node(state, expression, discarded);
                if (c != constness::constant)
                    break;
            }
            if (++state.full_expressions > constant_full_expression_limit) {
                c = constness::unknown;
                break;
            }
        }
        restore(state, saved, saved_elements);
        return c;
    }
    case semantic_kind::break_statement:
        state.jump = jump_break;
        return c;
    case semantic_kind::continue_statement:
        state.jump = jump_continue;
        return c;
    case semantic_kind::return_statement:
        result = constant_value();
        if (x.a)
            c = evaluate_node(state, x.a, result);
        returned = true;
        return c;
    case semantic_kind::identifier_label:
    case semantic_kind::case_label:
    case semantic_kind::default_label:
        return evaluate_statement(state, x.a, returned, result);
    case semantic_kind::switch_statement: {
        if (x.d) {
            c = evaluate_statement(state, x.d, returned, result);
            if (c != constness::constant)
                return c;
        }
        constant_value value;
        c = evaluate_condition(state, x.a, value);
        uint32_t condition = x.a;
        if (semantics[condition].kind == semantic_kind::condition_declaration)
            condition = semantics[condition].b;
        fundamental_type t = evaluated_type(semantics[condition].type);
        if (c == constness::constant && t == fundamental_type::none)
            c = constness::unknown;
        if (c == constness::constant) {
            /* [stmt.switch]/5: "If one of the case constants has the same
               value as the condition, control is passed to the statement
               following the matched case label.  If no case constant
               matches the condition, and if there is a default label,
               control passes to the statement labeled by the default
               label.  If no case matches and if there is no default then
               none of the statements in the switch is executed." */
            bool found = false;
            state.seek = switch_target(x.b, kept_of(t, value), found);
            if (state.seek)
                c = evaluate_statement(state, x.b, returned, result);
            /* a label inside a statement the seek passed over */
            if (state.seek)
                c = constness::unknown;
            state.seek = 0;
            /* [stmt.break]/1: the break ends the switch */
            if (state.jump == jump_break)
                state.jump = jump_none;
        }
        restore(state, saved, saved_elements);
        return c;
    }
    /* [dcl.constexpr]/3.5.1: "a goto statement" may not be in the body */
    case semantic_kind::goto_statement:
    default:
        return constness::unknown;
    }
}

constness semantic_analysis::potential_constant(uint32_t body)
{
    const semantic_node &b = semantics[body];
    if (b.kind != semantic_kind::function_body || b.b || b.c)
        return constness::unknown;
    constant_evaluation state;
    state.potential = true;
    state.frame = state.frames = 1;
    state.depth = 1;
    const uint32_t *parameters = semantics.items(b.first);
    for (uint32_t i = 0; i < b.count; i++) {
        constant_evaluation::object o;
        o.entity = parameters[i];
        o.frame = state.frame;
        o.opaque = true;
        state.objects.append(o);
    }
    bool returned = false;
    constant_value result;
    return evaluate_statement(state, b.a, returned, result);
}

} // namespace cplusplus
