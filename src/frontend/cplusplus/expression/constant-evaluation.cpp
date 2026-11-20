/*
 * constant-evaluation.cpp: [expr.const] the evaluation of a core constant
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
 * constant expressions (entity::usable); arrays, classes, references
 * (the pointer to the object they are bound to) and pointers into one
 * array (null, an element or one past the last: P + J, P - Q, comparisons,
 * ++, [], *).  Every other kind of node, type or statement answers
 * unsupported ([expr.const]): a pointer to a single object moved, a
 * reference to a temporary, long double.
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

/* the elements to keep at the end of a scope: all of them when a return
   statement's class object may be among them */
template <class state_type>
size_t kept_elements(const state_type &state, size_t saved, bool returned,
                     const constant_value &result)
{
    if (returned && result.kind == constant_value_kind::class_object)
        return state.elements.size();
    return saved;
}

template <class state_type>
void restore(state_type &state, size_t objects, size_t elements)
{
    state.objects.resize(objects);
    state.elements.resize(elements);
    state.element_read_only.resize(elements);
}

/* an integer type of 128 bits: expression_meaning keeps 64 bits, the
   evaluator's value all of them */
bool wide_integer(fundamental_type t)
{
    return t == fundamental_type::int128 ||
           t == fundamental_type::unsigned_int128;
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
    if (!fundamental_type_is_integral(f) ||
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

bool semantic_analysis::value_kept_exactly(qualified_type t,
                                           const constant_value &v) const
{
    fundamental_type f = evaluated_type(t);
    if (!wide_integer(f))
        return true;
    return v.kind == constant_value_kind::integer &&
           integer_kept_exactly(tokens.files.behavior, f, v.integer);
}

/* [conv.integral], [conv.double], [conv.fpint], [conv.bool]: the rules
   analysis folds with, on a value */
constness semantic_analysis::evaluate_conversion(constant_value &v,
                                                 fundamental_type from,
                                                 fundamental_type to)
{
    if (from == fundamental_type::none || to == fundamental_type::none)
        return constness::unknown;
    if (wide_integer(from) || wide_integer(to)) {
        /* [conv.integral]/2, /3: the value modulo 2^N; /1 for bool (a
           floating operand is not evaluated) */
        if (fundamental_type_is_floating(from) ||
            fundamental_type_is_floating(to) ||
            v.kind != constant_value_kind::integer)
            return constness::unknown;
        if (!wide_integer(to))
            v = value_of(to,
                         integer_wrap(tokens.files.behavior, to, v.integer));
        return constness::constant;
    }
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
    if (wide_integer(t))
        return evaluate_wide_binary(op, t, right, result, x, y, out);
    uint64_t value = 0;
    constness c =
        fold_binary(tokens.files.behavior, tokens.files.floating_point_pragma,
                    static_cast<preprocessing_op_or_punc>(op), t, right,
                    kept_of(t, x), kept_of(right, y), value);
    if (c == constness::constant)
        out = value_of(result, value);
    return c;
}

/*
 * [expr.mul], [expr.add], [expr.shift], [expr.rel], [expr.eq], [expr.bit.and],
 * [expr.xor] and [expr.or] on operands of a 128-bit integer type (folding.cpp's
 * rules at 128 bits, on the evaluator's values).
 */
constness semantic_analysis::evaluate_wide_binary(
    uint8_t op, fundamental_type t, fundamental_type right,
    fundamental_type result, const constant_value &x, const constant_value &y,
    constant_value &out)
{
    if (x.kind != constant_value_kind::integer ||
        y.kind != constant_value_kind::integer)
        return constness::unknown;
    preprocessing_op_or_punc o = static_cast<preprocessing_op_or_punc>(op);
    bool is_signed = t == fundamental_type::int128;
    unsigned __int128 a = x.integer, b = y.integer;
    __int128 sa = static_cast<__int128>(a), sb = static_cast<__int128>(b);
    unsigned __int128 r = 0;
    bool less = a < b;
    if (is_signed)
        less = sa < sb;
    switch (o) {
    case preprocessing_op_or_punc::LESS:
    case preprocessing_op_or_punc::GREATER:
    case preprocessing_op_or_punc::LESS_EQUAL:
    case preprocessing_op_or_punc::GREATER_EQUAL:
    case preprocessing_op_or_punc::EQUAL_EQUAL:
    case preprocessing_op_or_punc::EXCLAMATION_EQUAL: {
        /* [expr.rel]/6, [expr.eq] */
        bool equal = a == b;
        bool value = !equal;
        if (o == preprocessing_op_or_punc::LESS)
            value = less;
        else if (o == preprocessing_op_or_punc::GREATER)
            value = !less && !equal;
        else if (o == preprocessing_op_or_punc::LESS_EQUAL)
            value = less || equal;
        else if (o == preprocessing_op_or_punc::GREATER_EQUAL)
            value = !less;
        else if (o == preprocessing_op_or_punc::EQUAL_EQUAL)
            value = equal;
        out = value_of(result, value);
        return constness::constant;
    }
    case preprocessing_op_or_punc::PLUS:
    case preprocessing_op_or_punc::MINUS:
    case preprocessing_op_or_punc::STAR: {
        /* [expr.pre]/4: a signed result not in range is undefined;
           [basic.fundamental]/4: an unsigned one wraps */
        __int128 ignored = 0;
        bool overflow = false;
        if (o == preprocessing_op_or_punc::PLUS) {
            overflow = __builtin_add_overflow(sa, sb, &ignored);
            r = a + b;
        } else if (o == preprocessing_op_or_punc::MINUS) {
            overflow = __builtin_sub_overflow(sa, sb, &ignored);
            r = a - b;
        } else {
            overflow = __builtin_mul_overflow(sa, sb, &ignored);
            r = a * b;
        }
        if (is_signed && overflow)
            return constness::not_constant;
        break;
    }
    case preprocessing_op_or_punc::SLASH:
    case preprocessing_op_or_punc::PERCENT: {
        /* [expr.mul]/4: a zero divisor, or a quotient not representable,
           is undefined */
        if (b == 0)
            return constness::not_constant;
        if (is_signed) {
            __int128 least =
                static_cast<__int128>(static_cast<unsigned __int128>(1) << 127);
            if (sa == least && sb == -1)
                return constness::not_constant;
            if (o == preprocessing_op_or_punc::SLASH)
                r = static_cast<unsigned __int128>(sa / sb);
            else
                r = static_cast<unsigned __int128>(sa % sb);
        } else if (o == preprocessing_op_or_punc::SLASH) {
            r = a / b;
        } else {
            r = a % b;
        }
        break;
    }
    case preprocessing_op_or_punc::AMPERSAND:
        r = a & b;
        break;
    case preprocessing_op_or_punc::CARET:
        r = a ^ b;
        break;
    case preprocessing_op_or_punc::BAR:
        r = a | b;
        break;
    case preprocessing_op_or_punc::LESS_LESS:
    case preprocessing_op_or_punc::GREATER_GREATER: {
        /* [expr.shift]/1: undefined if the right operand is negative or
           not less than the width of the promoted left operand */
        if (wide_integer(right) || fundamental_type_is_floating(right))
            return constness::unknown;
        __int128 n =
            integer_value(tokens.files.behavior, right, kept_of(right, y));
        if (n < 0 || n >= 128)
            return constness::not_constant;
        if (o == preprocessing_op_or_punc::GREATER_GREATER) {
            /* [expr.shift]/3: E1/2^E2, rounded down */
            if (is_signed)
                r = static_cast<unsigned __int128>(sa >> static_cast<int>(n));
            else
                r = a >> static_cast<int>(n);
            break;
        }
        /* [expr.shift]/2: congruent to E1 x 2^E2 modulo 2^N (C++20) */
        if (is_signed &&
            !tokens.files.behavior.has(language_feature::modular_left_shift))
            return constness::unknown;
        r = a << static_cast<int>(n);
        break;
    }
    default:
        return constness::unknown;
    }
    out = constant_value();
    out.kind = constant_value_kind::integer;
    out.integer = r;
    return constness::constant;
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
        state.any_expression = true;
        /* a function defined after the expression is undefined at its
           point ([expr.const]/5.3) */
        if (x.at.is_token())
            state.defined_before = x.at.token();
        else
            state.defined_before = x.at.first_token();
        constant_value v;
        if (evaluate_node(state, m.node, v) == constness::constant &&
            is_value(v) && value_kept_exactly(m.type, v))
            evaluated_meaning(m, v);
    }
}

analysis_result semantic_analysis::evaluate_constant(SyntaxRef at,
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
    /* a 128-bit value that expression_meaning's 64 bits do not hold */
    if (c == constness::constant &&
        !value_kept_exactly(semantics[node].type, v))
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
 * [dcl.init]/7: the object of type t at place initialized by init: a
 * scalar by an expression or zero ([dcl.init]/6); an array or an
 * aggregate class element by element from an initializer list, the
 * elements of an array it does not give zero ([dcl.init.aggr]/5); a class
 * by a constructor ([class.ctor]) or a copy.  No initializer (0) leaves
 * the value indeterminate ([basic.indet]/1); zero_initializer stands for
 * an element no list gives.
 */
constness semantic_analysis::evaluate_initialization(constant_evaluation &state,
                                                     qualified_type t,
                                                     uint32_t init,
                                                     bool read_only,
                                                     uint32_t place)
{
    bool zero = init == zero_initializer ||
                (init && semantics[init].kind == semantic_kind::zero);
    const struct type record = type_table[t.unqualified()];
    if (record.kind == type_kind::lvalue_reference ||
        record.kind == type_kind::rvalue_reference) {
        /* [dcl.init.ref]: the reference is the pointer to the object it
           is bound to */
        if (!init || zero)
            return constness::unknown;
        constant_value v;
        constness c = evaluate_node(state, init, v);
        if (c != constness::constant)
            return c;
        if (v.kind != constant_value_kind::pointer || !v.count)
            return constness::unknown;
        place_value(state, place) = v;
        return c;
    }
    if (record.kind != type_kind::array &&
        record.kind != type_kind::class_type) {
        constant_value v;
        constness c = constness::constant;
        bool pointer = record.kind == type_kind::pointer;
        fundamental_type f = evaluated_type(t);
        if (f == fundamental_type::none && !pointer)
            return constness::unknown;
        /* a null pointer's zero ([conv.ptr]/1) */
        if (zero && pointer)
            v.kind = constant_value_kind::pointer;
        else if (zero)
            v = value_of(f, 0);
        else if (init)
            c = evaluate_node(state, init, v);
        if (c == constness::constant)
            place_value(state, place) = v;
        return c;
    }
    uint32_t given = 0;
    const uint32_t *items = nullptr;
    uint32_t count = 0;
    entity_index c_entity = 0;
    if (record.kind == type_kind::array) {
        if (!record.bound_known ||
            record.bound > constant_full_expression_limit)
            return constness::unknown;
        count = static_cast<uint32_t>(record.bound);
    } else {
        c_entity = evaluated_class(t);
        if (!c_entity)
            return constness::unknown;
        count = entity_table[c_entity].member_count;
        /* value-initialization of a class ([dcl.init]/8) calls a default
           constructor, and a member's default member initializer
           ([class.mem]/10) is not in the tree: neither is modelled */
        if (zero && count)
            return constness::unknown;
    }
    if (init && !zero) {
        semantic_kind k = semantics[init].kind;
        if (k == semantic_kind::construct && c_entity && !semantics[init].a) {
            /* the object is made first, its members indeterminate, then
               the constructor runs on it */
            constness c =
                evaluate_initialization(state, t, 0, read_only, place);
            if (c != constness::constant)
                return c;
            return evaluate_construct(state, init, place);
        }
        if (k != semantic_kind::initializer_list) {
            /* a copy of a class object ([class.copy.ctor]/14) or a value
               a call returned; a string literal: not modelled */
            if (!c_entity)
                return constness::unknown;
            constant_value v;
            constness c = evaluate_node(state, init, v);
            if (c != constness::constant)
                return c;
            if (v.kind != constant_value_kind::class_object)
                return constness::unknown;
            place_value(state, place) = copy_value(state, v, read_only);
            return constness::constant;
        }
        given = semantics[init].count;
        items = semantics.items(semantics[init].first);
    }
    /* (a member a list does not give may have a default member
       initializer, which analysis does not keep in the tree) */
    if (given > count || (c_entity && init && !zero && given != count))
        return constness::unknown;
    uint32_t first = static_cast<uint32_t>(state.elements.size());
    for (uint32_t i = 0; i < count; i++) {
        state.elements.append(constant_value());
        state.element_read_only.append(read_only);
    }
    constant_value v;
    v.kind = constant_value_kind::array;
    if (c_entity) {
        v.kind = constant_value_kind::class_object;
        v.designator = c_entity;
    }
    v.first = first;
    v.count = count;
    place_value(state, place) = v;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t element = 0;
        if (i < given)
            element = items[i];
        else if (init)
            element = zero_initializer;
        qualified_type et = record.operand;
        if (c_entity) {
            const entity &x = entity_table[c_entity];
            et = entity_table[class_members[x.first_member + i]].type;
        }
        constness c = evaluate_initialization(state, et, element, read_only,
                                              element_place | (first + i));
        if (c != constness::constant)
            return c;
    }
    return constness::constant;
}

entity_index semantic_analysis::evaluated_class(qualified_type t) const
{
    const struct type &record = type_table[t.unqualified()];
    if (record.kind != type_kind::class_type)
        return 0;
    const entity &x = entity_table[record.entity];
    if (!x.defined || x.union_ || x.dynamic || x.virtual_base_count)
        return 0;
    return record.entity;
}

constant_value semantic_analysis::copy_value(constant_evaluation &state,
                                             const constant_value &v,
                                             bool read_only)
{
    if (v.kind != constant_value_kind::array &&
        v.kind != constant_value_kind::class_object)
        return v;
    constant_value r = v;
    r.first = static_cast<uint32_t>(state.elements.size());
    for (uint32_t i = 0; i < v.count; i++) {
        state.elements.append(constant_value());
        state.element_read_only.append(read_only);
    }
    for (uint32_t i = 0; i < v.count; i++) {
        constant_value e = state.elements[v.first + i];
        constant_value copied = copy_value(state, e, read_only);
        state.elements[r.first + i] = copied;
    }
    return r;
}

/*
 * The object a glvalue designates: [expr.const]/5.8's "object whose
 * lifetime began within the evaluation of E", a local variable or
 * parameter of the invocation being evaluated.
 */
constness semantic_analysis::evaluate_object(constant_evaluation &state,
                                             uint32_t node, uint32_t &object)
{
    /* a copy: evaluating can analyse more code, which appends to the tree */
    const semantic_node x = semantics[node];
    constness c;
    switch (x.kind) {
    case semantic_kind::entity: {
        object = frame_object(state, x.a);
        if (object != no_frame_object) {
            type_kind rk =
                type_table[entity_table[x.a].type.unqualified()].kind;
            if (rk != type_kind::lvalue_reference &&
                rk != type_kind::rvalue_reference)
                return constness::constant;
            /* [expr.type]/1: a reference designates the object
               it is bound to */
            const constant_evaluation::object &r = state.objects[object];
            if (r.opaque || r.value.kind != constant_value_kind::pointer ||
                !r.value.count)
                return constness::unknown;
            return pointed_place(state, r.value, object);
        }
        const entity &e = entity_table[x.a];
        type_kind tk = type_table[e.type.unqualified()].kind;
        if (x.a && e.kind == entity_kind::variable &&
            (!e.automatic || e.constexpr_) &&
            (tk == type_kind::array || tk == type_kind::class_type)) {
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
            object = static_cast<uint32_t>(state.objects.size());
            state.objects.append(o);
            /* (its constructor runs on it as on a temporary: a constexpr
               object's members are modified only during its
               construction, [expr.const]/5.16) */
            state.objects[object].read_only = false;
            size_t elements = state.elements.size();
            c = evaluate_initialization(state, e.type, e.definition, false,
                                        object);
            state.objects[object].read_only = true;
            for (size_t i = elements; i < state.elements.size(); i++)
                state.element_read_only[i] = 1;
            /* (an initializer that is not a constant expression made the
               constexpr variable's declaration ill-formed, diagnosed
               there; one the analysis left unsupported is not known) */
            if (c == constness::not_constant)
                return constness::unknown;
            return c;
        }
        /* an automatic variable whose lifetime did not begin within the
           evaluation: [expr.const]/5.8 forbids an lvalue-to-rvalue
           conversion of it, not designating it (the object expression of
           a member call that reads nothing, as clang accepts), which the
           evaluation has no object for; a variable of static storage
           duration may not be modified (/5.16) */
        return constness::unknown;
    }
    /* [expr.unary.op]/1: "the result is an lvalue referring to the object
       ... to which the expression points" */
    case semantic_kind::indirection: {
        constant_value p;
        c = evaluate_node(state, x.a, p);
        if (c != constness::constant)
            return c;
        if (p.kind != constant_value_kind::pointer)
            return constness::unknown;
        return pointed_place(state, p, object);
    }
    /* [expr.ref]/6.2: "If E2 is a non-static data member ... E1.E2
       designates the corresponding member subobject" */
    case semantic_kind::member: {
        uint32_t whole = 0;
        c = evaluate_object(state, x.a, whole);
        if (c != constness::constant)
            return c;
        constant_value o = place_value(state, whole);
        if (o.kind != constant_value_kind::class_object)
            return constness::unknown;
        const entity &k = entity_table[o.designator];
        /* a bit-field's value is its width's ([class.bit]): not modelled */
        if (entity_table[x.b].bit_field)
            return constness::unknown;
        /* [expr.const]/5.9: reading a mutable subobject of an object that
           is usable in constant expressions is not constant: which object
           this is is not modelled */
        if (entity_table[x.b].mutable_member)
            return constness::unknown;
        for (uint32_t i = 0; i < k.member_count; i++)
            if (class_members[k.first_member + i] == x.b) {
                object = element_place | (o.first + i);
                type_kind mk =
                    type_table[entity_table[x.b].type.unqualified()].kind;
                if (mk != type_kind::lvalue_reference &&
                    mk != type_kind::rvalue_reference)
                    return constness::constant;
                /* [expr.type]/1: a member of reference type designates
                   the object it is bound to */
                const constant_value r = place_value(state, object);
                if (r.kind != constant_value_kind::pointer || !r.count)
                    return constness::unknown;
                return pointed_place(state, r, object);
            }
        return constness::unknown;
    }
    case semantic_kind::construct: {
        if (!x.a)
            break;
        c = evaluate_object(state, x.a, object);
        if (c != constness::constant)
            return c;
        c = evaluate_initialization(state, semantics[x.a].type, 0, false,
                                    object);
        if (c != constness::constant)
            return c;
        return evaluate_construct(state, node, object);
    }
    /* [expr.sub]/1: "The expression E1[E2] is identical (by definition) to
       *((E1)+(E2))": the pointer operand (an array converted) moved by
       the index */
    case semantic_kind::subscript: {
        uint32_t pointer_node = x.a, index_node = x.b;
        if (type_table[semantics[pointer_node].type.unqualified()].kind !=
            type_kind::pointer) {
            uint32_t held = pointer_node;
            pointer_node = index_node;
            index_node = held;
        }
        constant_value p, index, moved;
        c = evaluate_node(state, pointer_node, p);
        if (c != constness::constant)
            return c;
        c = evaluate_node(state, index_node, index);
        if (c != constness::constant)
            return c;
        if (p.kind != constant_value_kind::pointer ||
            index.kind != constant_value_kind::integer)
            return constness::unknown;
        c = offset_pointer(p, static_cast<__int128>(index.integer), moved);
        if (c != constness::constant)
            return c;
        return pointed_place(state, moved, object);
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
    /* [lex.string]/9: "Evaluating a string-literal results in a string
       literal object with static storage duration, initialized from the
       given characters", the code units and the terminating zero; "it is
       unspecified whether distinct string-literals are distinct objects"
       (each evaluation makes one) */
    case semantic_kind::string_literal: {
        const struct type &array = type_table[x.type.unqualified()];
        fundamental_type f = evaluated_type(array.operand);
        if (array.kind != type_kind::array || !array.bound_known ||
            array.bound > constant_full_expression_limit ||
            f == fundamental_type::none)
            return constness::unknown;
        const uint32_t *units = tokens.files.phase7().string_code_units();
        const literal_value &lit = tokens.files.value_of(tokens[x.at]);
        uint32_t count = static_cast<uint32_t>(array.bound);
        constant_value a;
        a.kind = constant_value_kind::array;
        a.first = static_cast<uint32_t>(state.elements.size());
        a.count = count;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t u = 0;
            if (i < lit.string_length)
                u = units[lit.string_offset + i];
            state.elements.append(value_of(f, u));
            state.element_read_only.append(1);
        }
        constant_evaluation::object o;
        o.read_only = true;
        o.value = a;
        object = static_cast<uint32_t>(state.objects.size());
        state.objects.append(o);
        return constness::constant;
    }
    default:
        break;
    }
    /* [conv.rval]: a prvalue of class type where an object is needed
       (the object expression of a member access or call) materializes a
       temporary */
    if (x.category != value_category::prvalue || !evaluated_class(x.type))
        return constness::unknown;
    constant_value v;
    c = evaluate_node(state, node, v);
    if (c != constness::constant)
        return c;
    if (v.kind != constant_value_kind::class_object)
        return constness::unknown;
    constant_evaluation::object o;
    o.frame = state.frame;
    o.value = v;
    object = static_cast<uint32_t>(state.objects.size());
    state.objects.append(o);
    return constness::constant;
}

/* [conv.lval]/3.4: "the object indicated by the glvalue is read" */
constness semantic_analysis::evaluate_read(constant_evaluation &state,
                                           uint32_t node, constant_value &v)
{
    /* a copy: evaluating can analyse more code, which appends to the tree */
    const semantic_node x = semantics[node];
    if (x.type.cv() & cv_volatile)
        /* [expr.const]/5.8: "a non-volatile glvalue" */
        return undefined_value(state);
    type_kind tk = type_table[x.type.unqualified()].kind;
    bool scalar = evaluated_type(x.type) != fundamental_type::none;
    if (!scalar && tk != type_kind::pointer && tk != type_kind::class_type)
        return constness::unknown;
    if (scalar && x.kind == semantic_kind::entity &&
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
    /* [class.copy.ctor]/14: the implicit copy of a class object, member
       by member */
    if (v.kind == constant_value_kind::class_object)
        v = copy_value(state, v, false);
    return constness::constant;
}

/* [expr.unary.op]/1: "the result is an lvalue referring to the object to
   which the expression points": the place of that object.  The null
   pointer designates none ([basic.compound]/3), nor does one past the end
   of an array ([expr.add]/4); a place that is gone (an object of a block
   that ended) is not known. */
constness semantic_analysis::pointed_place(const constant_evaluation &state,
                                           const constant_value &p,
                                           uint32_t &place) const
{
    if (p.kind != constant_value_kind::pointer)
        return constness::unknown;
    if (!p.count)
        return constness::not_constant;
    if (p.extent && p.offset >= p.extent)
        return constness::not_constant;
    uint32_t index = p.first;
    if (index & element_place) {
        if ((index & ~element_place) >= state.elements.size())
            return constness::unknown;
    } else if (index >= state.objects.size()) {
        return constness::unknown;
    }
    place = index;
    return constness::constant;
}

/* [expr.add]/4: "If P points to an element x[i] of an array object x with n
   elements, the expressions P + J and J + P (where J has the value j) point
   to the (possibly hypothetical) element x[i + j] if 0 <= i + j <= n;
   otherwise, the behavior is undefined."  The null pointer plus 0 is null
   (/4.1).  A pointer to an object that is not an element is not moved
   (unknown) except by 0. */
constness semantic_analysis::offset_pointer(const constant_value &p, __int128 n,
                                            constant_value &moved) const
{
    if (p.kind != constant_value_kind::pointer)
        return constness::unknown;
    moved = p;
    if (n == 0)
        return constness::constant;
    if (!p.count)
        return constness::not_constant;
    if (!p.extent)
        return constness::unknown;
    __int128 i = static_cast<__int128>(p.offset) + n;
    if (i < 0 || i > static_cast<__int128>(p.extent))
        return constness::not_constant;
    moved.offset = static_cast<uint64_t>(i);
    moved.first = element_place | (p.base + static_cast<uint32_t>(i));
    return constness::constant;
}

/* [conv.array]/1: "An lvalue or rvalue of type 'array of N T' ... can be
   converted to a prvalue of type 'pointer to T'.  The result is a pointer
   to the first element of the array." */
constness semantic_analysis::evaluate_array_pointer(constant_evaluation &state,
                                                    uint32_t node,
                                                    constant_value &v)
{
    uint32_t array = 0;
    constness c = evaluate_object(state, node, array);
    if (c != constness::constant)
        return c;
    if (!(array & element_place) && state.objects[array].opaque)
        return constness::unknown;
    const constant_value a = place_value(state, array);
    if (a.kind != constant_value_kind::array || !a.count)
        return constness::unknown;
    v = constant_value();
    v.kind = constant_value_kind::pointer;
    v.count = 1;
    v.base = a.first;
    v.extent = a.count;
    v.offset = 0;
    v.first = element_place | a.first;
    return constness::constant;
}

/* [dcl.init.ref]/5.1: a reference bound to a glvalue refers to its object;
   to a prvalue ([dcl.init.ref]/5.2, a temporary) is not modelled */
constness semantic_analysis::evaluate_binding(constant_evaluation &state,
                                              uint32_t node, constant_value &v)
{
    uint32_t bound = semantics[node].a;
    if (semantics[bound].category == value_category::prvalue)
        return constness::unknown;
    uint32_t object = 0;
    constness c = evaluate_object(state, bound, object);
    if (c != constness::constant)
        return c;
    v = constant_value();
    v.kind = constant_value_kind::pointer;
    v.count = 1;
    v.first = object;
    return constness::constant;
}

/*
 * [expr.add]/4, /5, [expr.rel]/4 and [expr.eq]/3 on pointers into one
 * array (or null): P + J, J + P, P - J, P - Q and the comparisons.  Pointers
 * to different objects compare unequal only where neither can be one past
 * the end of the other's array, and the relational operators of unrelated
 * pointers are unspecified: unknown.
 */
constness semantic_analysis::evaluate_pointer_binary(constant_evaluation &state,
                                                     uint32_t node,
                                                     constant_value &v)
{
    const semantic_node x = semantics[node];
    preprocessing_op_or_punc op = static_cast<preprocessing_op_or_punc>(x.op);
    bool left_pointer = type_table[semantics[x.a].type.unqualified()].kind ==
                        type_kind::pointer;
    bool right_pointer = type_table[semantics[x.b].type.unqualified()].kind ==
                         type_kind::pointer;
    constant_value p, q;
    constness c = evaluate_node(state, x.a, p);
    if (c != constness::constant)
        return c;
    c = evaluate_node(state, x.b, q);
    if (c != constness::constant)
        return c;
    fundamental_type result = evaluated_type(x.type);
    if (op == preprocessing_op_or_punc::PLUS ||
        op == preprocessing_op_or_punc::MINUS) {
        if (left_pointer && right_pointer) {
            if (op != preprocessing_op_or_punc::MINUS ||
                p.kind != constant_value_kind::pointer ||
                q.kind != constant_value_kind::pointer ||
                result == fundamental_type::none)
                return constness::unknown;
            if (!p.count && !q.count) {
                v = value_of(result, 0);
                return constness::constant;
            }
            /* [expr.add]/5.2: both point to elements of the same array */
            if (!p.count || !q.count || !p.extent || p.extent != q.extent ||
                p.base != q.base)
                return constness::unknown;
            __int128 d = static_cast<__int128>(p.offset) -
                         static_cast<__int128>(q.offset);
            v = constant_value();
            v.kind = constant_value_kind::integer;
            v.integer = static_cast<unsigned __int128>(d);
            return constness::constant;
        }
        const constant_value *pointer_operand = &q, *integer_operand = &p;
        if (left_pointer) {
            pointer_operand = &p;
            integer_operand = &q;
        }
        const constant_value &pointer = *pointer_operand;
        const constant_value &integer = *integer_operand;
        if (integer.kind != constant_value_kind::integer ||
            (!left_pointer && op == preprocessing_op_or_punc::MINUS))
            return constness::unknown;
        __int128 n = static_cast<__int128>(integer.integer);
        if (op == preprocessing_op_or_punc::MINUS)
            n = -n;
        return offset_pointer(pointer, n, v);
    }
    bool equality = op == preprocessing_op_or_punc::EQUAL_EQUAL ||
                    op == preprocessing_op_or_punc::EXCLAMATION_EQUAL;
    bool relational = op == preprocessing_op_or_punc::LESS ||
                      op == preprocessing_op_or_punc::GREATER ||
                      op == preprocessing_op_or_punc::LESS_EQUAL ||
                      op == preprocessing_op_or_punc::GREATER_EQUAL;
    if ((!equality && !relational) || !left_pointer || !right_pointer ||
        p.kind != constant_value_kind::pointer ||
        q.kind != constant_value_kind::pointer ||
        result == fundamental_type::none)
        return constness::unknown;
    int order = 0;
    if (!p.count || !q.count) {
        /* [expr.eq]/3: null pointers compare equal; a pointer to an object
           is not null */
        if (relational)
            return constness::unknown;
        order = 0;
        if (p.count != q.count)
            order = 1;
    } else if (p.extent && p.extent == q.extent && p.base == q.base) {
        if (p.offset < q.offset)
            order = -1;
        else if (p.offset > q.offset)
            order = 1;
    } else if (!p.extent && !q.extent && equality) {
        if (p.first != q.first)
            order = 1;
    } else {
        return constness::unknown;
    }
    bool truth = false;
    switch (op) {
    case preprocessing_op_or_punc::EQUAL_EQUAL:
        truth = order == 0;
        break;
    case preprocessing_op_or_punc::EXCLAMATION_EQUAL:
        truth = order != 0;
        break;
    case preprocessing_op_or_punc::LESS:
        truth = order < 0;
        break;
    case preprocessing_op_or_punc::GREATER:
        truth = order > 0;
        break;
    case preprocessing_op_or_punc::LESS_EQUAL:
        truth = order <= 0;
        break;
    default:
        truth = order >= 0;
        break;
    }
    v = value_of(result, truth);
    return constness::constant;
}

constness semantic_analysis::evaluate_node(constant_evaluation &state,
                                           uint32_t node, constant_value &v)
{
    /* a copy: evaluating can analyse more code, which appends to the tree */
    const semantic_node x = semantics[node];
    constness c;
    if (x.kind == semantic_kind::bind_reference)
        return evaluate_binding(state, node, v);
    if (x.evaluation == constness::constant &&
        x.category == value_category::prvalue) {
        /* [basic.fundamental]: an integer or a double analysis folded;
           an object of class type (`E{}`) is made by the cases below */
        fundamental_type f = evaluated_type(x.type);
        if (f != fundamental_type::none) {
            v = value_of(f, x.value);
            return constness::constant;
        }
        /* (a null pointer is made by the conversion below) */
        type_kind k = type_table[x.type.unqualified()].kind;
        if (k != type_kind::class_type && k != type_kind::pointer)
            return constness::unknown;
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
        if (k == conversion_kind::array_to_pointer)
            return evaluate_array_pointer(state, x.a, v);
        /* [conv.ptr]/1: "A null pointer constant of integral type can be
           converted to a pointer type" */
        if (k == conversion_kind::null_pointer) {
            /* (a null pointer constant has no side effects, and it is a
               constant: analysis folded it) */
            if (semantics[x.a].evaluation != constness::constant ||
                semantics[x.a].category != value_category::prvalue)
                return constness::unknown;
            v = constant_value();
            v.kind = constant_value_kind::pointer;
            return constness::constant;
        }
        /* [conv.qual]: the same address, the pointed-to type differing
           in cv-qualification only */
        if (k == conversion_kind::pointer ||
            k == conversion_kind::qualification_cast) {
            const struct type from =
                type_table[semantics[x.a].type.unqualified()];
            const struct type to = type_table[x.type.unqualified()];
            if (from.kind != type_kind::pointer ||
                to.kind != type_kind::pointer ||
                from.operand.unqualified() != to.operand.unqualified())
                return constness::unknown;
            c = evaluate_node(state, x.a, v);
            if (c == constness::constant &&
                v.kind != constant_value_kind::pointer)
                return constness::unknown;
            return c;
        }
        /* [conv.bool]/1: "A prvalue of arithmetic, unscoped enumeration,
           pointer, or pointer-to-member type can be converted to a prvalue
           of type bool": a pointer is false when null */
        if (k == conversion_kind::boolean &&
            type_table[semantics[x.a].type.unqualified()].kind ==
                type_kind::pointer) {
            c = evaluate_node(state, x.a, v);
            if (c != constness::constant)
                return c;
            if (v.kind != constant_value_kind::pointer ||
                fundamental_of(x.type) != fundamental_type::bool_)
                return constness::unknown;
            bool truth = v.count != 0;
            v = value_of(fundamental_type::bool_, truth);
            return constness::constant;
        }
        if (k == conversion_kind::bit_cast) {
            c = evaluate_node(state, x.a, v);
            if (c != constness::constant)
                return c;
            fundamental_type from = evaluated_type(semantics[x.a].type);
            fundamental_type to = evaluated_type(x.type);
            uint64_t value = 0;
            if (wide_integer(from) || wide_integer(to))
                return constness::unknown;
            if (!bit_cast_kept(tokens.files.behavior, from, to,
                               kept_of(from, v), value))
                return constness::unknown;
            v = value_of(to, value);
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
           underlying type is bool (cast-expression.cpp): the truncated value of
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
        if (wide_integer(operand)) {
            /* unary + and !; - and ~ at 128 bits are not evaluated */
            auto op = static_cast<preprocessing_op_or_punc>(x.op);
            if (v.kind != constant_value_kind::integer)
                return constness::unknown;
            if (op == preprocessing_op_or_punc::EXCLAMATION) {
                v = value_of(t, v.integer == 0);
                return constness::constant;
            }
            if (op == preprocessing_op_or_punc::PLUS)
                return constness::constant;
            return constness::unknown;
        }
        uint64_t value = 0;
        c = fold_unary(tokens.files.behavior,
                       static_cast<preprocessing_op_or_punc>(x.op), operand,
                       kept_of(operand, v), value);
        if (c == constness::constant)
            v = value_of(t, value);
        return c;
    }
    case semantic_kind::binary: {
        if (type_table[semantics[x.a].type.unqualified()].kind ==
                type_kind::pointer ||
            type_table[semantics[x.b].type.unqualified()].kind ==
                type_kind::pointer)
            return evaluate_pointer_binary(state, node, v);
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
        const semantic_node operand = semantics[x.a];
        fundamental_type t = evaluated_type(operand.type);
        bool pointer =
            type_table[operand.type.unqualified()].kind == type_kind::pointer;
        if ((t == fundamental_type::none && !pointer) ||
            t == fundamental_type::bool_)
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
        if (pointer) {
            /* [expr.pre.incr]/1: the pointer moved by one element
               ([expr.add]/4) */
            if (old.kind != constant_value_kind::pointer)
                return constness::unknown;
            constant_value moved;
            __int128 step = 1;
            if (k == increment_kind::pre_decrement ||
                k == increment_kind::post_decrement)
                step = -1;
            c = offset_pointer(old, step, moved);
            if (c != constness::constant)
                return c;
            place_value(state, object) = moved;
            v = old;
            if (k == increment_kind::pre_increment ||
                k == increment_kind::pre_decrement)
                v = moved;
            return constness::constant;
        }
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
        if (t != fundamental_type::none) {
            v = value_of(t, 0);
            return constness::constant;
        }
    }
        /* fall through: a class's or an array's, a temporary */
        [[fallthrough]];
    /* [class.temporary]/2: a prvalue of class type initializes a
       temporary object (or the object a's for a constructor called on
       one) */
    case semantic_kind::initializer_list:
    case semantic_kind::construct: {
        uint32_t object = 0;
        if (x.kind == semantic_kind::construct && x.a) {
            c = evaluate_object(state, node, object);
            v = constant_value();
            v.kind = constant_value_kind::lvalue;
            return c;
        }
        constant_evaluation::object o;
        o.frame = state.frame;
        object = static_cast<uint32_t>(state.objects.size());
        state.objects.append(o);
        c = evaluate_initialization(state, x.type, node, false, object);
        if (c == constness::constant)
            v = state.objects[object].value;
        return c;
    }
    /* [expr.unary.op]/3: "The result of the unary & operator is a pointer
       to its operand" */
    case semantic_kind::address: {
        uint32_t object = 0;
        c = evaluate_object(state, x.a, object);
        if (c != constness::constant)
            return c;
        v = constant_value();
        v.kind = constant_value_kind::pointer;
        v.first = object;
        v.count = 1;
        return constness::constant;
    }
    /* pointers and objects: [expr.const]/11's permitted results */
    case semantic_kind::entity:
    case semantic_kind::indirection:
    case semantic_kind::subscript:
    case semantic_kind::member:
    case semantic_kind::string_literal:
    case semantic_kind::bound_member_function:
    case semantic_kind::assignment:
    /* initialization: [dcl.init], [class.dtor] */
    case semantic_kind::bind_reference:
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
    const semantic_node x = semantics[node];
    uint32_t callee = x.a;
    if (semantics[callee].kind == semantic_kind::conversion &&
        static_cast<conversion_kind>(semantics[callee].op) ==
            conversion_kind::function_to_pointer)
        callee = semantics[callee].a;
    if (semantics[callee].kind != semantic_kind::entity)
        return constness::unknown;
    /* [meta.const.eval]/1: true within a manifestly constant-evaluated
       expression ([expr.const]/14), which this evaluation is unless it is
       potential_constant's or the claims' */
    if (entity_table[semantics[callee].a].gnu_flags &
        gnu_is_constant_evaluated) {
        if (state.potential || state.any_expression)
            return constness::unknown;
        v = constant_value();
        v.kind = constant_value_kind::integer;
        v.integer = 1;
        return constness::constant;
    }
    /* [ext:gnu_library_builtins] clang evaluates the bit counts */
    if (int which = bit_count_builtin(semantics[callee].a))
        return evaluate_bit_count(state, which, x, v);
    if (int which = query_builtin(semantics[callee].a))
        return evaluate_query_builtin(state, which, x, v);
    if (entity_table[semantics[callee].a].gnu_flags & gnu_floating_constant)
        return evaluate_floating_builtin(state, semantics[callee].a, x, v);
    /* [expr.prim.lambda.closure]/4: whether a closure's function call
       operator is constexpr is not decided yet (evaluate_invocation),
       unless its lambda-declarator says constexpr */
    entity_index enclosing = entity_table[semantics[callee].a].enclosing;
    if (enclosing && entity_table[enclosing].lambda &&
        !entity_table[semantics[callee].a].constexpr_)
        return constness::unknown;
    /* [class.this]/1: a non-static member function's this, the address
       of the object expression */
    constant_value self;
    if (x.b) {
        constness c = evaluate_node(state, x.b, self);
        if (c != constness::constant)
            return c;
        if (self.kind != constant_value_kind::pointer)
            return constness::unknown;
    }
    bool returned = false;
    constant_value result;
    constness c = evaluate_invocation(state, semantics[callee].a, self, x.first,
                                      x.count, returned, result);
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
    if (!is_value(result) && result.kind != constant_value_kind::class_object)
        return constness::unknown;
    v = result;
    return constness::constant;
}

/*
 * [temp.inst]/4, [temp.point]/7: the body of a member function of a class
 * template's specialization that waits for the end of the translation
 * unit (postponed_bodies) is analysed now, where a constant evaluation
 * needs it ([expr.const]/11: the function is "needed for constant
 * evaluation"), unless a class is being defined, whose completeness the
 * wait was for.
 */
void semantic_analysis::analyse_postponed_body(entity_index f)
{
    if (entity_table[f].definition)
        return;
    for (entity_index d = defining_class; d; d = entity_table[d].enclosing)
        if (d == entity_table[f].enclosing)
            return;
    /* a function template's specialization whose definition was not
       needed where it was named (an unevaluated operand, instantiate): a
       constant evaluation needs it ([temp.inst]/5, [expr.const]/11) */
    for (size_t i = 0; i < instantiations.size(); i++) {
        const instantiation_record &x = instantiations[i];
        if (x.specialization != f || x.defined || x.defining)
            continue;
        const function_template &ft =
            function_templates[entity_table[x.template_entity].instantiation -
                               1];
        if (ft.definition)
            instantiate_definition(ft.declaration, static_cast<uint32_t>(i));
        return;
    }
    for (size_t i = 0; i < postponed_bodies.size(); i++) {
        postponed_body &b = postponed_bodies[i];
        if (!b.c || b.c != entity_table[f].enclosing ||
            !declared_by(f, b.body.function_definition()))
            continue;
        const postponed_body taken = b;
        b.c = 0;
        instantiation_context context;
        enter_instantiation(context, taken.scope, taken.declaration);
        instantiated_member_body(taken.c, taken.body);
        leave_instantiation(context);
        return;
    }
}

constness semantic_analysis::evaluate_invocation(
    constant_evaluation &state, entity_index f, const constant_value &self,
    uint32_t first, uint32_t count, bool &returned, constant_value &result)
{
    if (state.depth >= constexpr_invocation_limit)
        return constness::unknown;
    const entity &e = entity_table[f];
    if (e.kind != entity_kind::function)
        return constness::unknown;
    if (!e.constexpr_) {
        /* (a library builtin, declared with C language linkage, may be
           one the evaluator does not know clang evaluates) */
        if (state.potential && e.c_language_linkage)
            return constness::unknown;
        /* an implicitly declared special member is constexpr when its
           definition would satisfy [dcl.constexpr]'s rules
           ([class.copy.ctor]/12), which is not decided yet */
        if (e.implicit)
            return constness::unknown;
        /* [expr.prim.lambda.closure]/4: a closure's function call operator
           is constexpr if it satisfies those rules, not decided yet; its
           conversion function and the function it returns (/11) too */
        if (e.enclosing && entity_table[e.enclosing].lambda)
            return constness::unknown;
        return constness::not_constant;
    }
    analyse_postponed_body(f);
    uint32_t body = entity_table[f].definition;
    uint32_t with_this = 0;
    if (self.kind == constant_value_kind::pointer)
        with_this = 1;
    if (!body || semantics[body].kind != semantic_kind::function_body ||
        semantics[body].evaluation == constness::not_constant ||
        semantics[body].c || semantics[body].count != count + with_this ||
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
    const uint32_t *items = semantics.items(first);
    const uint32_t *parameters = semantics.items(semantics[body].first);
    if (with_this) {
        constant_evaluation::object o;
        o.entity = parameters[0];
        o.frame = frame;
        o.value = self;
        state.objects.append(o);
    }
    constness arguments = constness::constant;
    for (uint32_t i = 0; i < count; i++) {
        constant_evaluation::object o;
        constness c = evaluate_node(state, items[i], o.value);
        if (c == constness::unknown && state.potential) {
            arguments = c;
            continue;
        }
        if (c != constness::constant) {
            restore(state, saved,
                    kept_elements(state, saved_elements, returned, result));
            return c;
        }
        o.entity = parameters[i + with_this];
        o.frame = frame;
        state.objects.append(o);
    }
    if (arguments != constness::constant) {
        restore(state, saved,
                kept_elements(state, saved_elements, returned, result));
        return arguments;
    }
    uint32_t caller = state.frame;
    state.frames = frame;
    state.frame = frame;
    state.depth++;
    /* [class.base.init]/13: a constructor's member initializations, then
       its compound-statement */
    constness c = constness::constant;
    if (semantics[body].b)
        c = evaluate_statement(state, semantics[body].b, returned, result);
    if (c == constness::constant)
        c = evaluate_statement(state, semantics[body].a, returned, result);
    state.depth--;
    state.frame = caller;
    /* a class object returned keeps its elements */
    if (result.kind == constant_value_kind::class_object)
        saved_elements = state.elements.size();
    restore(state, saved,
            kept_elements(state, saved_elements, returned, result));
    return c;
}

/*
 * [class.ctor]/2 for the construct node on the object at place: the
 * implicit default constructor of a class with no member initializers
 * leaves the members indeterminate; the implicit copy and move
 * constructors copy each member ([class.copy.ctor]/14); another
 * constructor is invoked with this pointing to the object.
 */
constness semantic_analysis::evaluate_construct(constant_evaluation &state,
                                                uint32_t node, uint32_t place)
{
    const semantic_node x = semantics[node];
    const entity &k = entity_table[x.b];
    entity_index c_entity = evaluated_class(entity_table[k.enclosing].type);
    if (k.implicit) {
        /* [class.default.ctor]/1: the implicit default constructor of a
           class with no members and no bases does nothing */
        if (!x.count) {
            if (c_entity && !entity_table[c_entity].member_count)
                return constness::constant;
            return constness::unknown;
        }
        if (x.count != 1 || !c_entity || !is_copy_constructor(c_entity, x.b))
            return constness::unknown;
        uint32_t argument = semantics.items(x.first)[0];
        if (semantics[argument].kind != semantic_kind::bind_reference)
            return constness::unknown;
        uint32_t from = 0;
        constness c = evaluate_object(state, semantics[argument].a, from);
        if (c != constness::constant)
            return c;
        constant_value v = place_value(state, from);
        if (v.kind != constant_value_kind::class_object)
            return constness::unknown;
        constant_value copied = copy_value(state, v, false);
        place_value(state, place) = copied;
        return constness::constant;
    }
    constant_value self;
    self.kind = constant_value_kind::pointer;
    self.first = place;
    self.count = 1;
    bool returned = false;
    constant_value result;
    return evaluate_invocation(state, x.b, self, x.first, x.count, returned,
                               result);
}

constness semantic_analysis::evaluate_variable(qualified_type t, uint32_t init,
                                               token_index at, uint32_t &value)
{
    value = 0;
    constant_evaluation state;
    constant_evaluation::object o;
    state.objects.append(o);
    constness c = evaluate_initialization(state, t, init, false, 0);
    if (c == constness::constant)
        value = value_tree(state, t, state.objects[0].value, at);
    return c;
}

uint32_t semantic_analysis::value_tree(const constant_evaluation &state,
                                       qualified_type t,
                                       const constant_value &v, token_index at)
{
    expression_meaning m;
    m.type = qualified_type::make(t.unqualified(), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::constant;
    if (is_value(v)) {
        fundamental_type f = evaluated_type(t);
        if (f == fundamental_type::none)
            return 0;
        /* a 128-bit value that 64 bits do not hold: the initializer as
           written is lowered */
        if (!value_kept_exactly(t, v))
            return 0;
        m.value = kept_of(f, v);
        return expression_node(semantic_kind::constant, m, at);
    }
    if (v.kind != constant_value_kind::array &&
        v.kind != constant_value_kind::class_object)
        return 0;
    const struct type record = type_table[t.unqualified()];
    owned_array<uint32_t> elements;
    for (uint32_t i = 0; i < v.count; i++) {
        qualified_type et = record.operand;
        if (v.kind == constant_value_kind::class_object) {
            const entity &x = entity_table[v.designator];
            et = entity_table[class_members[x.first_member + i]].type;
        }
        uint32_t e = value_tree(state, et, state.elements[v.first + i], at);
        if (!e)
            return 0;
        elements.append(e);
    }
    uint32_t node = expression_node(semantic_kind::initializer_list, m, at);
    semantics.set_list(node, elements.data(), elements.size());
    return node;
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
    /* a copy: evaluating can analyse more code, which appends to the tree */
    const semantic_node x = semantics[node];
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
            restore(state, saved,
                    kept_elements(state, saved_elements, returned, result));
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
        return evaluate_initialization(state, e.type, init, false, index);
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
        restore(state, saved,
                kept_elements(state, saved_elements, returned, result));
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
            restore(state, iteration,
                    kept_elements(state, iteration_elements, returned, result));
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
        restore(state, saved,
                kept_elements(state, saved_elements, returned, result));
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
        if (c == constness::constant &&
            (t == fundamental_type::none || wide_integer(t)))
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
        restore(state, saved,
                kept_elements(state, saved_elements, returned, result));
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
    const semantic_node b = semantics[body];
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
