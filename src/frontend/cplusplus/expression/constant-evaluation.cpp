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
 * ++, [], *), pointers to single objects (a one-element array,
 * [basic.compound]/3.3), to variables of static storage duration and to
 * string literals (one object each, which stays: [lex.string]/9), pointer
 * assignment and `p += n`.  Every other kind of node, type or statement
 * answers unsupported ([expr.const]): a reference to a temporary, long
 * double.
 *
 * Inside an evaluation analysis's not_constant does not decide: it says an
 * assignment or a read of a local is not a constant expression on its own,
 * while [expr.const]/5.8 and /5.16 allow both for an object "whose
 * lifetime began within the evaluation of E".  Only the outermost node's
 * not_constant is taken as it is.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/builtin-registry.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

enum : uint8_t { jump_none, jump_break, jump_continue };

/* the line of the last answer "no rules" (the one nearest the node the
   evaluation started from): the detail of the refusal that follows, read
   only when an evaluation answers unknown */
thread_local uint32_t last_unknown_line = 0;

/* the line of the last answer "not a constant expression" (a rule of
   [expr.const]/5), for the detail of the refusal that follows when the
   analysis could not decide */
thread_local uint32_t last_not_constant_line = 0;
thread_local bool last_not_constant_certain = false;
#define CONST_NOT_CONSTANT                                                     \
    (last_not_constant_line = __LINE__, last_not_constant_certain = false,     \
     constness::not_constant)
/* the same for a rule of [expr.const]/5 that clang applies as it is: the
   declaration that needs a constant expression is ill-formed (not one the
   analysis merely cannot follow) */
#define CONST_NOT_CONSTANT_RULE                                                \
    (last_not_constant_line = __LINE__, last_not_constant_certain = true,      \
     constness::not_constant)

/* The answer "the evaluator has no rules for this": normally just
   constness::unknown.  Compiled with -DVCC_TRACE_UNKNOWN each such answer
   prints "CXXUNK line" to stderr, which tells which refusal an input
   meets (count them: `... 2>&1 | grep CXXUNK | sort | uniq -c`). */
#ifdef VCC_TRACE_UNKNOWN
constness trace_unknown(int line)
{
    fprintf(stderr, "CXXUNK %d\n", line);
    last_unknown_line = static_cast<uint32_t>(line);
    return constness::unknown;
}
#define CONST_UNKNOWN trace_unknown(__LINE__)
#else
#define CONST_UNKNOWN (last_unknown_line = __LINE__, constness::unknown)
#endif

/* a class or union object (not an array): the values whose members are
   elements of the evaluation */
bool is_object_value(const constant_value &v)
{
    return v.kind == constant_value_kind::class_object ||
           v.kind == constant_value_kind::union_object;
}

/* a read or a write of an object whose value the evaluation does not have:
   [expr.const]/5.8, /5.16 make it not constant when the object began its
   life outside the evaluation, unknown otherwise (a parameter of the body a
   potential-constant check looks at) */
template <class State, class Object>
constness opaque_access(const State &state, const Object &o)
{
    /* (the check of a body for a potential constant expression leaves a
       variable it cannot know unusable to the call that needs a value,
       as clang does) */
    if (o.outside && o.static_unusable && !state.potential)
        return CONST_NOT_CONSTANT_RULE;
    if (o.outside)
        return CONST_NOT_CONSTANT;
    return CONST_UNKNOWN;
}

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

/* the same for an object that is zero-initialized whatever its
   constructors ([dcl.init]/6: each member, not a constructor's call),
   before a constructor runs on it ([dcl.init]/8.2, construct.op) */
constexpr uint32_t zero_fill = UINT32_MAX - 1;

/* constant_value::base of a pointer to a single object moved by one (its
   one past the end): there is no array whose elements to index */
constexpr uint32_t single_object_base = UINT32_MAX;

/* a read of an indeterminate value ([basic.indet]/2) or of a volatile
   object ([expr.const]/5.8): not constant, but the check of a body for a
   potential constant expression does not count it, [impl.clang] as clang does
   not */
template <class state_type> constness undefined_value(const state_type &state)
{
    if (state.potential)
        return CONST_UNKNOWN;
    return CONST_NOT_CONSTANT_RULE;
}

/* an object of the evaluation, with the serial that tells it from the
   object a later one reuses the place of once its scope has ended
   ([basic.stc.auto]/1): a pointer to it carries the serial */
template <class state_type, class object_type>
void append_object(state_type &state, object_type &o)
{
    o.serial = ++state.serials;
    state.objects.append(o);
}

/* the elements to keep at the end of a scope: all of them when a return
   statement's class object may be among them */
template <class state_type>
size_t kept_elements(const state_type &state, size_t saved, bool returned,
                     const constant_value &result)
{
    if (returned && is_object_value(result))
        return state.elements.size();
    return saved;
}

/* the end of a scope; what a string literal's object lies above stays
   ([lex.string]/9: static storage duration) */
template <class state_type>
void restore(state_type &state, size_t objects, size_t elements)
{
    if (objects < state.object_floor)
        objects = state.object_floor;
    if (elements < state.element_floor)
        elements = state.element_floor;
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
        return CONST_UNKNOWN;
    if (wide_integer(from) || wide_integer(to)) {
        /* [conv.integral]/2, /3: the value modulo 2^N; /1 for bool (a
           floating operand is not evaluated) */
        if (fundamental_type_is_floating(from) ||
            fundamental_type_is_floating(to) ||
            v.kind != constant_value_kind::integer)
            return CONST_UNKNOWN;
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
        return CONST_UNKNOWN;
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
            return CONST_NOT_CONSTANT;
        break;
    }
    case preprocessing_op_or_punc::SLASH:
    case preprocessing_op_or_punc::PERCENT: {
        /* [expr.mul]/4: a zero divisor, or a quotient not representable,
           is undefined */
        if (b == 0)
            return CONST_NOT_CONSTANT;
        if (is_signed) {
            __int128 least =
                static_cast<__int128>(static_cast<unsigned __int128>(1) << 127);
            if (sa == least && sb == -1)
                return CONST_NOT_CONSTANT;
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
            return CONST_UNKNOWN;
        __int128 n =
            integer_value(tokens.files.behavior, right, kept_of(right, y));
        if (n < 0 || n >= 128)
            return CONST_NOT_CONSTANT;
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
            return CONST_UNKNOWN;
        r = a << static_cast<int>(n);
        break;
    }
    default:
        return CONST_UNKNOWN;
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

/* the last refusal recorded: where the evaluator last answered "no
   rules" (its source line), for a constant expression refused outside
   evaluate_constant */
void semantic_analysis::evaluator_stop_detail()
{
    char text[64] = "evaluator answered no value";
    if (constant_stop == 1)
        snprintf(text, sizeof text, "analysis folded it as not constant");
    else if (constant_stop == 2)
        snprintf(text, sizeof text, "no semantic node for the evaluator");
    else if (last_unknown_line)
        snprintf(text, sizeof text, "evaluator met L%u", last_unknown_line);
    else if (last_not_constant_line)
        snprintf(text, sizeof text, "evaluator not_constant L%u",
                 last_not_constant_line);
    last_unsupported_detail(text, strlen(text));
}

void semantic_analysis::evaluation_detail(uint32_t node, bool gave_up,
                                          const constant_value &v)
{
    char text[128];
    if (!gave_up) {
        /* the evaluation was constant but its value is not one the
           analysis keeps (or an allocation was left) */
        snprintf(text, sizeof text, "evaluator kept no value (%s, value %d)",
                 semantic_kind_name(semantics[node].kind),
                 static_cast<int>(v.kind));
    } else {
        snprintf(text, sizeof text, "evaluator met L%u (%s)", last_unknown_line,
                 semantic_kind_name(semantics[node].kind));
    }
    last_unsupported_detail(text, strlen(text));
}

bool semantic_analysis::copy_reads_nothing(entity_index c) const
{
    const entity &x = entity_table[c];
    if (x.union_ || x.dynamic || x.virtual_base_count)
        return false;
    for (uint32_t i = 0; i < x.member_count; i++) {
        const entity &m = entity_table[class_members[x.first_member + i]];
        if (m.base_subobject || m.kind != entity_kind::variable ||
            !m.nonstatic_member)
            continue;
        const struct type &t = type_table[m.type.unqualified()];
        if (t.kind != type_kind::class_type || !copy_reads_nothing(t.entity))
            return false;
    }
    for (uint32_t i = 0; i < x.base_count; i++)
        if (!copy_reads_nothing(
                type_table[entity_table[class_bases[x.first_base + i]]
                               .type.unqualified()]
                    .entity))
            return false;
    return true;
}

bool semantic_analysis::evaluation_rule_applied() const
{
    return last_not_constant_certain;
}

constness semantic_analysis::evaluate_value(uint32_t node, constant_value &v)
{
    if (semantics[node].evaluation == constness::not_constant)
        return CONST_NOT_CONSTANT;
    constant_evaluation state;
    last_not_constant_line = 0;
    last_not_constant_certain = false;
    constness c = evaluate_node(state, node, v);
    /* [expr.const]/5.18: an allocation not deallocated in the evaluation:
       whether this expression is the whole of the constant expression is
       not known here (`f(alloc())` has f delete what the consteval alloc
       made), so it is not decided */
    if (c == constness::constant && state.heap_live)
        c = constness::unknown;
    if (c == constness::constant && !is_value(v))
        c = constness::unknown;
    /* a 128-bit value that expression_meaning's 64 bits do not hold */
    if (c == constness::constant &&
        !value_kept_exactly(semantics[node].type, v))
        c = constness::unknown;
    return c;
}

analysis_result semantic_analysis::evaluate_constant(SyntaxRef at,
                                                     uint32_t node,
                                                     constant_value &v,
                                                     bool &constant)
{
    constant = false;
    last_unknown_line = 0;
    constness c = evaluate_value(node, v);
    if (c == constness::unknown) {
        /* (the reason of a refused function the evaluation called is the
           subclause; its detail is where the evaluator stopped: a "no
           rules" answer, or a value the analysis does not keep) */
        analysis_result refused = not_supported(at, expr_const_reason());
        evaluation_detail(node, last_unknown_line != 0, v);
        return refused;
    }
    constant = c == constness::constant;
    if (c == constness::not_constant && last_not_constant_certain)
        constant_stop = 3;
    return analysis_result::well_formed;
}

/* [class.mem]/9: whether the class (or an array of it) has a mutable
   non-static data member, in a member or a base subobject */
bool semantic_analysis::has_mutable_subobject(qualified_type type) const
{
    qualified_type object = type;
    while (type_table[object.unqualified()].kind == type_kind::array)
        object = type_table[object.unqualified()].operand;
    if (type_table[object.unqualified()].kind != type_kind::class_type)
        return false;
    const entity &k = entity_table[type_table[object.unqualified()].entity];
    for (uint32_t i = 0; i < k.member_count; i++) {
        const entity &m = entity_table[class_members[k.first_member + i]];
        if (m.mutable_member || has_mutable_subobject(m.type))
            return true;
    }
    return false;
}

/* [dcl.init]/8: whether value-initializing an object of class c is more
   than zero-initializing it: a member or base with a default member
   initializer is initialized by it after the zeros ([class.mem]/10) */
bool semantic_analysis::has_default_member_initializer(entity_index c) const
{
    const entity &k = entity_table[c];
    for (uint32_t i = 0; i < k.member_count; i++) {
        const entity &m = entity_table[class_members[k.first_member + i]];
        if (m.default_initializer)
            return true;
        const struct type *t = &type_table[m.type.unqualified()];
        while (t->kind == type_kind::array)
            t = &type_table[t->operand.unqualified()];
        if (t->kind == type_kind::class_type && t->entity != c &&
            has_default_member_initializer(t->entity))
            return true;
    }
    return false;
}

/* [dcl.init.aggr]/4: whether an element of an initializer list is the
   member's: all but an unnamed bit-field ([class.bit]/2) */
bool semantic_analysis::initializer_member(entity_index member) const
{
    const entity &m = entity_table[member];
    return !(m.bit_field && !m.name);
}

entity_index semantic_analysis::element_destructor(qualified_type type,
                                                   qualified_type &object) const
{
    object = type;
    while (type_table[object.unqualified()].kind == type_kind::array)
        object = type_table[object.unqualified()].operand;
    if (type_table[object.unqualified()].kind != type_kind::class_type)
        return 0;
    return entity_table[type_table[object.unqualified()].entity].destructor;
}

/* [stmt.dcl]/2, [expr.call]/8: the object just made (the last of the
   evaluation's objects) of this type is destroyed when its scope ends */
void semantic_analysis::add_destroyed(constant_evaluation &state,
                                      qualified_type type)
{
    constant_evaluation::destruction d;
    d.object = static_cast<uint32_t>(state.objects.size() - 1);
    d.type = type;
    state.destructions.append(d);
}

/* [class.dtor]: the destructor of the object of the type at place, of each
   element of an array from the last ([dcl.init.aggr]/?, [class.dtor]/13:
   reverse order of the construction) */
constness semantic_analysis::destroy_object(constant_evaluation &state,
                                            uint32_t place, qualified_type type)
{
    qualified_type element;
    entity_index destructor = element_destructor(type, element);
    if (!destructor)
        return constness::constant;
    const struct type record = type_table[type.unqualified()];
    if (record.kind == type_kind::array) {
        const constant_value array = place_value(state, place);
        if (array.kind != constant_value_kind::array)
            return CONST_UNKNOWN;
        for (uint32_t i = array.count; i-- > 0;) {
            constness c = destroy_object(
                state, element_place | (array.first + i), record.operand);
            if (c != constness::constant)
                return c;
        }
        return constness::constant;
    }
    constant_value self;
    self.kind = constant_value_kind::pointer;
    self.first = place;
    self.count = 1;
    if (!(place & element_place))
        self.stamp = state.objects[place].serial;
    bool returned = false;
    constant_value result;
    return evaluate_invocation(state, destructor, self, 0, 0, returned, result);
}

/* [intro.execution]/5: the end of a full-expression: the temporaries it
   made are destroyed, last first (the destructions past keep) */
constness semantic_analysis::end_full_expression(constant_evaluation &state,
                                                 size_t keep, constness c,
                                                 constant_value &result)
{
    while (state.destructions.size() > keep) {
        constant_evaluation::destruction d =
            state.destructions[state.destructions.size() - 1];
        state.destructions.resize(state.destructions.size() - 1);
        if (c != constness::constant)
            continue;
        constant_value held = result;
        uint32_t jump = state.jump;
        state.jump = jump_none;
        c = destroy_object(state, d.object, d.type);
        state.jump = static_cast<uint8_t>(jump);
        result = held;
    }
    return c;
}

/* the end of a scope ([basic.stc.auto]/1): the objects declared since
   objects (and their elements) are destroyed, last first, then go.  Only
   an evaluation that is going on has destructors to run. */
constness semantic_analysis::leave_scope(constant_evaluation &state,
                                         size_t objects, size_t elements,
                                         bool &returned, constant_value &result,
                                         constness c)
{
    while (!state.destructions.empty() &&
           state.destructions[state.destructions.size() - 1].object >=
               objects) {
        constant_evaluation::destruction d =
            state.destructions[state.destructions.size() - 1];
        state.destructions.resize(state.destructions.size() - 1);
        if (c != constness::constant)
            continue;
        /* the value a return statement gave stays while they run */
        bool destroyed_returned = false;
        constant_value held = result;
        uint32_t jump = state.jump;
        state.jump = jump_none;
        c = destroy_object(state, d.object, d.type);
        state.jump = static_cast<uint8_t>(jump);
        result = held;
        (void)destroyed_returned;
    }
    restore(state, objects, kept_elements(state, elements, returned, result));
    return c;
}

/* the objects and elements made so far stay to the end of the evaluation:
   a variable of static storage duration made on its first use inside an
   invocation outlives it (a pointer to it may be the invocation's result) */
void semantic_analysis::pin_objects(constant_evaluation &state) const
{
    state.object_floor = static_cast<uint32_t>(state.objects.size());
    state.element_floor = static_cast<uint32_t>(state.elements.size());
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

const constant_value &
semantic_analysis::place_value(const constant_evaluation &state,
                               uint32_t place) const
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
    bool zero = init == zero_initializer || init == zero_fill ||
                (init && semantics[init].kind == semantic_kind::zero);
    const struct type record = type_table[t.unqualified()];
    if (record.kind == type_kind::lvalue_reference ||
        record.kind == type_kind::rvalue_reference) {
        /* [dcl.init.ref]: the reference is the pointer to the object it
           is bound to */
        if (!init || zero)
            return CONST_UNKNOWN;
        constant_value v;
        constness c = evaluate_node(state, init, v);
        if (c != constness::constant)
            return c;
        if (v.kind != constant_value_kind::pointer || !v.count)
            return CONST_UNKNOWN;
        place_value(state, place) = v;
        return c;
    }
    if (record.kind == type_kind::complex) {
        constant_value v;
        if (zero) {
            fundamental_type f = evaluated_type(record.operand);
            if (f == fundamental_type::none)
                return CONST_UNKNOWN;
            constant_value z;
            z.kind = constant_value_kind::floating;
            z.floating = 0.0;
            v.kind = constant_value_kind::array;
            v.first = static_cast<uint32_t>(state.elements.size());
            v.count = 2;
            state.elements.append(z);
            state.elements.append(z);
            state.element_read_only.append(read_only);
            state.element_read_only.append(read_only);
        } else {
            constness c = evaluate_node(state, init, v);
            if (c != constness::constant)
                return c;
            if (v.kind != constant_value_kind::array || v.count != 2)
                return CONST_UNKNOWN;
        }
        place_value(state, place) = v;
        return constness::constant;
    }
    if (record.kind != type_kind::array &&
        record.kind != type_kind::class_type) {
        constant_value v;
        constness c = constness::constant;
        bool pointer = record.kind == type_kind::pointer;
        fundamental_type f = evaluated_type(t);
        if (f == fundamental_type::none && !pointer)
            return CONST_UNKNOWN;
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
            return CONST_UNKNOWN;
        count = static_cast<uint32_t>(record.bound);
    } else {
        c_entity = evaluated_class(t);
        if (!c_entity)
            return CONST_UNKNOWN;
        const entity &c = entity_table[c_entity];
        count = c.member_count;
        /* [dcl.init]/6: to zero-initialize an object of class type is to
           zero-initialize each non-static data member and each base class
           subobject, whatever its constructors (a base with data is a
           member of the list: base-clause.cpp; an empty one has no
           element).  A `zero` that stands for value-initialization of a
           class with a constructor ([dcl.init]/8) runs that constructor,
           which is not this */
        if (zero && init != zero_fill &&
            ((c.first_constructor &&
              !trivial_defaulted_default_constructor(c_entity)) ||
             has_default_member_initializer(c_entity)))
            return CONST_UNKNOWN;
    }
    if (init && !zero) {
        semantic_kind k = semantics[init].kind;
        if (k == semantic_kind::construct && c_entity && !semantics[init].a) {
            /* the object is made first, its members indeterminate (zero
               when the construct says so, [dcl.init]/8.2), then the
               constructor runs on it */
            uint32_t first = 0;
            if (semantics[init].op || state.zeroed)
                first = zero_fill;
            constness c =
                evaluate_initialization(state, t, first, read_only, place);
            if (c != constness::constant)
                return c;
            return evaluate_construct(state, init, place);
        }
        if (k != semantic_kind::initializer_list) {
            /* a copy of a class object ([class.copy.ctor]/14) or a value
               a call returned; a string literal: not modelled */
            if (!c_entity)
                return CONST_UNKNOWN;
            constant_value v;
            constness c = evaluate_node(state, init, v);
            if (c != constness::constant)
                return c;
            if (!is_object_value(v))
                return CONST_UNKNOWN;
            place_value(state, place) = copy_value(state, v, read_only);
            return constness::constant;
        }
        given = semantics[init].count;
        items = semantics.items(semantics[init].first);
    }
    /* [dcl.init.aggr]/4: the elements the list covers: an unnamed
       bit-field has no initializer and is zero */
    if (c_entity && entity_table[c_entity].union_)
        return evaluate_union_initialization(state, init, zero, given, items,
                                             read_only, place, c_entity);
    uint32_t listed = count;
    if (c_entity) {
        listed = 0;
        const entity &x = entity_table[c_entity];
        for (uint32_t i = 0; i < x.member_count; i++)
            if (initializer_member(class_members[x.first_member + i]))
                listed++;
    }
    /* a list that has an item for each member, the unnamed bit-fields
       too (a constructor's, one per member in order) */
    bool all_members = c_entity && init && listed != count && given == count;
    if (!all_members &&
        (given > listed || (c_entity && init && !zero && given != listed)))
        return CONST_UNKNOWN;
    /* an object made again at its place (a constructor's initialization of
       a member the object's own allocation made) keeps the elements it
       has: those made now would be above the invocation's and go with it
       ([basic.stc.auto]/1) while the object holds them */
    const constant_value held = place_value(state, place);
    uint32_t first = static_cast<uint32_t>(state.elements.size());
    constant_value_kind shape = constant_value_kind::array;
    if (c_entity)
        shape = constant_value_kind::class_object;
    if (held.kind == shape && held.count == count &&
        (!c_entity || held.designator == c_entity) &&
        held.first + count <= state.elements.size()) {
        first = held.first;
        for (uint32_t i = 0; i < count; i++) {
            state.elements[first + i] = constant_value();
            state.element_read_only[first + i] = read_only;
        }
    } else {
        for (uint32_t i = 0; i < count; i++) {
            state.elements.append(constant_value());
            state.element_read_only.append(read_only);
        }
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
    uint32_t item = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t element = 0;
        bool listed_member = true;
        if (c_entity)
            listed_member = initializer_member(
                class_members[entity_table[c_entity].first_member + i]);
        if (all_members)
            element = items[i];
        else if (!listed_member)
            element = zero_initializer;
        else if (item < given)
            element = items[item++];
        else if (init)
            element = zero_initializer;
        if (init == zero_fill)
            element = zero_fill;
        qualified_type et = record.operand;
        if (c_entity) {
            const entity &x = entity_table[c_entity];
            et = entity_table[class_members[x.first_member + i]].type;
        }
        constness c;
        if (c_entity && element != zero_initializer && element != zero_fill &&
            semantics[element].kind ==
                semantic_kind::default_member_initializer) {
            /* [dcl.init.aggr]/5.1, [class.mem]/6: the default member
               initializer's `this` is the aggregate, whose earlier elements
               are stored; evaluated in a frame of its own */
            const semantic_node wrapper = semantics[element];
            constant_evaluation::object self;
            self.entity = static_cast<entity_index>(wrapper.b);
            self.frame = state.frames + 1;
            self.value.kind = constant_value_kind::pointer;
            self.value.first = place;
            self.value.count = 1;
            if (!(place & element_place))
                self.value.stamp = state.objects[place].serial;
            size_t saved_objects = state.objects.size();
            uint32_t caller = state.frame, caller_frames = state.frames;
            state.frames = state.frames + 1;
            state.frame = state.frames;
            append_object(state, self);
            c = evaluate_initialization(state, et, wrapper.a, read_only,
                                        element_place | (first + i));
            if (state.objects.size() > saved_objects)
                state.objects.resize(saved_objects);
            state.frame = caller;
            state.frames = caller_frames;
        } else {
            c = evaluate_initialization(state, et, element, read_only,
                                        element_place | (first + i));
        }
        if (c != constness::constant)
            return c;
        /* [class.bit]/1: a bit-field initialized holds the value modulo
           2^width as one assigned does */
        if (c_entity) {
            entity_index member =
                class_members[entity_table[c_entity].first_member + i];
            if (entity_table[member].bit_field)
                place_value(state, element_place | (first + i)) =
                    bit_field_member_stored(
                        member,
                        place_value(state, element_place | (first + i)));
        }
    }
    return constness::constant;
}

/*
 * [class.union]/2, [dcl.init.aggr]/5.2, [dcl.init]/6: the union object at
 * place has one slot, its active member's value: none while no member is
 * active (the object made, a constructor to follow); zero-initialized, the
 * first member is; a list gives the first member or the designated one
 * (the list node's a) its one initializer.
 */
constness semantic_analysis::evaluate_union_initialization(
    constant_evaluation &state, uint32_t init, bool zero, uint32_t given,
    const uint32_t *items, bool read_only, uint32_t place,
    entity_index c_entity)
{
    const entity &u = entity_table[c_entity];
    const constant_value held = place_value(state, place);
    uint32_t first = static_cast<uint32_t>(state.elements.size());
    if (held.kind == constant_value_kind::union_object &&
        held.designator == c_entity && held.first < state.elements.size()) {
        first = held.first;
        state.elements[first] = constant_value();
        state.element_read_only[first] = read_only;
    } else {
        state.elements.append(constant_value());
        state.element_read_only.append(read_only);
    }
    constant_value v;
    v.kind = constant_value_kind::union_object;
    v.designator = c_entity;
    v.first = first;
    v.count = 1;
    place_value(state, place) = v;
    if (!init)
        return constness::constant;
    entity_index member = 0;
    uint32_t value = zero_initializer;
    if (!zero) {
        if (semantics[init].kind != semantic_kind::initializer_list ||
            given > 1)
            return CONST_UNKNOWN;
        member = static_cast<entity_index>(semantics[init].a);
        if (given)
            value = items[0];
    }
    /* [dcl.init.aggr]/5.2, [dcl.init]/6: the first member that is not an
       unnamed bit-field */
    for (uint32_t i = 0; !member && i < u.member_count; i++)
        if (initializer_member(class_members[u.first_member + i]))
            member = class_members[u.first_member + i];
    if (!member)
        return CONST_UNKNOWN;
    place_value(state, place).member = member;
    return evaluate_initialization(state, entity_table[member].type, value,
                                   read_only, element_place | first);
}

entity_index semantic_analysis::evaluated_class(qualified_type t) const
{
    const struct type &record = type_table[t.unqualified()];
    if (record.kind != type_kind::class_type)
        return 0;
    const entity &x = entity_table[record.entity];
    if (!x.defined || x.virtual_base_count)
        return 0;
    return record.entity;
}

/* the value v of a class or array stored in the object at place: the
   elements one by one into those the object has, when it has them (a
   store is not a new object, whose elements would be above the
   invocation's and go with it), else v as it is */
void semantic_analysis::assign_value(constant_evaluation &state, uint32_t place,
                                     const constant_value &v)
{
    const constant_value held = place_value(state, place);
    if (v.kind == constant_value_kind::union_object &&
        held.kind == constant_value_kind::union_object &&
        held.designator == v.designator && held.first != v.first &&
        held.count == v.count) {
        /* [class.copy.assign]/10: a union is copied as its object
           representation: the active member of the source becomes the
           target's */
        place_value(state, place).member = v.member;
        const constant_value e = state.elements[v.first];
        state.elements[held.first] = constant_value();
        if (is_object_value(e) || e.kind == constant_value_kind::array)
            state.elements[held.first] = copy_value(state, e, false);
        else
            state.elements[held.first] = e;
        return;
    }
    if ((v.kind != constant_value_kind::array &&
         v.kind != constant_value_kind::class_object) ||
        held.kind != v.kind || held.count != v.count ||
        held.designator != v.designator || held.first == v.first) {
        place_value(state, place) = v;
        return;
    }
    for (uint32_t i = 0; i < v.count; i++) {
        const constant_value e = state.elements[v.first + i];
        assign_value(state, element_place | (held.first + i), e);
    }
}

constant_value semantic_analysis::copy_value(constant_evaluation &state,
                                             const constant_value &v,
                                             bool read_only)
{
    if (v.kind != constant_value_kind::array &&
        v.kind != constant_value_kind::class_object &&
        v.kind != constant_value_kind::union_object)
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
    case semantic_kind::complex_part: {
        if (x.op != 0 && x.op != 1)
            return CONST_UNKNOWN;
        uint32_t base = 0;
        c = evaluate_object(state, x.a, base);
        if (c != constness::constant)
            return c;
        const constant_value &cv = place_value(state, base);
        if (cv.kind != constant_value_kind::array || cv.count != 2)
            return CONST_UNKNOWN;
        object = (cv.first + x.op) | element_place;
        return constness::constant;
    }
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
                return CONST_UNKNOWN;
            return pointed_place(state, r.value, object);
        }
        const entity &e = entity_table[x.a];
        type_kind tk = type_table[e.type.unqualified()].kind;
        bool scalar_variable = tk == type_kind::pointer ||
                               evaluated_type(e.type) != fundamental_type::none;
        /* a variable of static storage duration of scalar type: its address
           is a permitted result ([expr.const]/11), its value is read only
           when the variable is usable in constant expressions
           ([expr.const]/5.8) */
        /* a weak variable's address may be null, a thread-local one's is
           not a constant */
        if (e.kind == entity_kind::variable &&
            ((e.gnu_flags & gnu_weak) || e.c11_thread_local))
            return CONST_UNKNOWN;
        if (x.a && e.kind == entity_kind::variable && e.name && !e.automatic &&
            scalar_variable && !(e.constexpr_ && e.definition)) {
            if (e.usable == constness::unknown)
                return CONST_UNKNOWN;
            for (size_t i = 0; i < state.objects.size(); i++)
                if (state.objects[i].entity == x.a && !state.objects[i].frame) {
                    object = static_cast<uint32_t>(i);
                    return constness::constant;
                }
            constant_evaluation::object o;
            o.entity = x.a;
            o.read_only = true;
            if (e.usable == constness::constant && tk != type_kind::pointer &&
                !(e.type.cv() & cv_volatile))
                o.value = value_of(evaluated_type(e.type), e.value);
            else
                o.unusable = true;
            object = static_cast<uint32_t>(state.objects.size());
            append_object(state, o);
            pin_objects(state);
            return constness::constant;
        }
        if (x.a && e.kind == entity_kind::variable &&
            (!e.automatic || e.constexpr_) &&
            (tk == type_kind::array || tk == type_kind::class_type ||
             tk == type_kind::complex || scalar_variable)) {
            /* [expr.const]/4: a constexpr array (a local one too, read
               outside its function's evaluation) is usable in constant
               expressions, its elements read as the evaluation needs
               them; another is not ([expr.const]/5.8) */
            for (size_t i = 0; i < state.objects.size(); i++)
                if (state.objects[i].entity == x.a && !state.objects[i].frame) {
                    object = static_cast<uint32_t>(i);
                    return constness::constant;
                }
            constant_evaluation::object o;
            o.entity = x.a;
            o.read_only = true;
            /* [expr.const]/5.8: a volatile object is never read */
            o.unusable = (e.type.cv() & cv_volatile) != 0;
            object = static_cast<uint32_t>(state.objects.size());
            /* [expr.const]/4: another object of static storage duration
               (declared extern, or not constexpr) is not usable in
               constant expressions: its address is a constant
               ([expr.const]/13) and its value is not known (opaque) */
            if (!e.constexpr_ || !e.definition) {
                o.opaque = true;
                /* [expr.const]/5.8: a class or array object that is not
                   constexpr is not usable in constant expressions, and
                   began its life outside the evaluation */
                /* (an empty class's copy reads no value,
                   [class.copy.ctor]/14) */
                o.static_unusable =
                    !e.constexpr_ && e.usable == constness::not_constant &&
                    (tk == type_kind::array ||
                     !copy_reads_nothing(
                         type_table[e.type.unqualified()].entity));
                o.outside = o.static_unusable;
                append_object(state, o);
                return constness::constant;
            }
            append_object(state, o);
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
                return CONST_UNKNOWN;
            if (c == constness::constant)
                pin_objects(state);
            return c;
        }
        /* [expr.const]/5.12 (before P2280R4): "an id-expression that
           refers to a variable or data member of reference type unless the
           reference has a preceding initialization and either it is usable
           in constant expressions or its lifetime began within the
           evaluation of E": a reference parameter of the function whose
           body holds the expression has neither (its value is the
           caller's) */
        if (!tokens.files.behavior.has(
                language_feature::unknown_references_in_constant_expressions) &&
            e.kind == entity_kind::variable && e.automatic &&
            entity_table[e.enclosing].parameter_scope &&
            (tk == type_kind::lvalue_reference ||
             tk == type_kind::rvalue_reference))
            return CONST_NOT_CONSTANT;
        /* an automatic variable whose lifetime did not begin within the
           evaluation: [expr.const]/5.8 forbids an lvalue-to-rvalue
           conversion of it, not designating it (the object expression of
           a member call that reads nothing, [impl.clang] as clang accepts), which the
           evaluation has no object for; a variable of static storage
           duration may not be modified (/5.16): the object of a class or
           array type is designated, opaque and outside */
        if (x.a && e.kind == entity_kind::variable && e.automatic &&
            !e.constexpr_ &&
            (tk == type_kind::array ||
             (tk == type_kind::class_type &&
              !empty_class(type_table[e.type.unqualified()].entity)))) {
            /* (an empty class's copy reads no value, [class.copy.ctor]/14:
               its object is not designated here, the answer stays unknown) */
            constant_evaluation::object o;
            o.entity = x.a;
            o.opaque = true;
            o.outside = true;
            object = static_cast<uint32_t>(state.objects.size());
            append_object(state, o);
            return constness::constant;
        }
        return CONST_UNKNOWN;
    }
    /* [expr.unary.op]/1: "the result is an lvalue referring to the object
       ... to which the expression points" */
    case semantic_kind::indirection: {
        constant_value p;
        c = evaluate_node(state, x.a, p);
        if (c != constness::constant)
            return c;
        if (p.kind != constant_value_kind::pointer)
            return CONST_UNKNOWN;
        return pointed_place(state, p, object);
    }
    /* [expr.cond]/5: a glvalue conditional designates the object of the
       operand it selects ([ext:gnu_conditional_omitted]: d, the first
       operand, when b is none) */
    case semantic_kind::conditional: {
        constant_value condition;
        c = evaluate_node(state, x.a, condition);
        if (c != constness::constant)
            return c;
        uint32_t second = x.b;
        if (!second)
            second = x.d;
        if (condition.integer != 0)
            return evaluate_object(state, second, object);
        return evaluate_object(state, x.c, object);
    }
    /* [expr.ref]/6.2: "If E2 is a non-static data member ... E1.E2
       designates the corresponding member subobject" */
    case semantic_kind::member: {
        uint32_t whole = 0;
        c = evaluate_object(state, x.a, whole);
        if (c != constness::constant)
            return c;
        if (!(whole & element_place) && state.objects[whole].opaque) {
            /* a member of an automatic variable of the enclosing function:
               its address is no constant ([expr.const]/13) nor its value
               ([expr.const]/5.8) */
            const auto &w = state.objects[whole];
            if (w.outside && entity_table[w.entity].automatic)
                return CONST_NOT_CONSTANT;
            /* [expr.const]/5.8: the read of a member of a static object
               that is not usable in constant expressions */
            if (w.static_unusable && state.reading && !state.potential)
                return CONST_NOT_CONSTANT_RULE;
            return constness::unknown;
        }
        constant_value o = place_value(state, whole);
        if (o.kind == constant_value_kind::union_object) {
            /* [class.union]/2: only the active member holds a value; an
               assignment to another one makes it active ([class.union]/6) */
            const entity &m = entity_table[x.b];
            type_kind mk = type_table[m.type.unqualified()].kind;
            if (m.mutable_member || mk == type_kind::lvalue_reference ||
                mk == type_kind::rvalue_reference)
                return CONST_UNKNOWN;
            if (o.member != x.b) {
                if (!state.writing) {
                    if (state.reading)
                        return CONST_NOT_CONSTANT_RULE;
                    if (state.designating) {
                        object = element_place | o.first;
                        return constness::constant;
                    }
                    return CONST_UNKNOWN;
                }
                place_value(state, whole).member = x.b;
                state.elements[o.first] = constant_value();
                if (mk == type_kind::class_type || mk == type_kind::array) {
                    qualified_type member_type = m.type;
                    c = evaluate_initialization(state, member_type, 0, false,
                                                element_place | o.first);
                    if (c != constness::constant)
                        return c;
                    /* the member's elements are the union's, which may be
                       older than the scope that makes them active: they
                       stay (as a string literal's do) */
                    state.element_floor =
                        static_cast<uint32_t>(state.elements.size());
                }
            }
            object = element_place | o.first;
            return constness::constant;
        }
        if (o.kind != constant_value_kind::class_object)
            return CONST_UNKNOWN;
        const entity &k = entity_table[o.designator];
        /* [expr.const]/5.9: reading a mutable subobject of an object that
           is usable in constant expressions is not constant: which object
           this is is not modelled */
        if (entity_table[x.b].mutable_member)
            return CONST_UNKNOWN;
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
                    return CONST_UNKNOWN;
                return pointed_place(state, r, object);
            }
        return CONST_UNKNOWN;
    }
    /* [conv.ptr]/3's and [expr.static.cast]/2's glvalue forms: the base
       class subobject of the object, or the object it is the base of */
    case semantic_kind::conversion: {
        conversion_kind k = static_cast<conversion_kind>(x.op);
        if (k != conversion_kind::derived_to_base &&
            k != conversion_kind::base_to_derived)
            break;
        uint32_t from = 0;
        c = evaluate_object(state, x.a, from);
        if (c != constness::constant)
            return c;
        if (k == conversion_kind::derived_to_base)
            return base_subobject_place(state, from, x.b, object);
        return derived_object_place(state, from, x.b, object);
    }
    case semantic_kind::construct: {
        if (!x.a)
            break;
        state.writing++;
        c = evaluate_object(state, x.a, object);
        state.writing--;
        if (c != constness::constant)
            return c;
        uint32_t first = 0;
        if (state.zeroed)
            first = zero_fill;
        c = evaluate_initialization(state, semantics[x.a].type, first, false,
                                    object);
        if (c != constness::constant)
            return c;
        return evaluate_construct(state, node, object);
    }
    /* [expr.sub]/1: "The expression E1[E2] is identical (by definition) to
       *((E1)+(E2))": the pointer operand (an array converted) moved by
       the index */
    case semantic_kind::subscript: {
        constant_value moved;
        c = evaluate_subscript_pointer(state, node, moved);
        if (c != constness::constant)
            return c;
        return pointed_place(state, moved, object);
    }
    case semantic_kind::assignment: {
        /* [class.union]/6: `u.m = v` makes m active */
        if (static_cast<preprocessing_op_or_punc>(x.op) ==
            preprocessing_op_or_punc::EQUAL)
            state.writing++;
        c = evaluate_object(state, x.a, object);
        if (static_cast<preprocessing_op_or_punc>(x.op) ==
            preprocessing_op_or_punc::EQUAL)
            state.writing--;
        if (c != constness::constant)
            return c;
        uint32_t target = object;
        /* [expr.const]/5.16: the modification of an object whose lifetime
           did not begin within the evaluation */
        if (place_read_only(state, target))
            return CONST_NOT_CONSTANT;
        constant_value y;
        c = evaluate_node(state, x.b, y);
        if (c != constness::constant)
            return c;
        fundamental_type t = evaluated_type(semantics[x.a].type);
        if (t == fundamental_type::none &&
            type_table[semantics[x.a].type.unqualified()].kind ==
                type_kind::pointer) {
            /* [expr.ass]/2 on a pointer: the pointer assigned; [expr.add]/4
               for `p += n` and `p -= n` ("E1 = E1 op E2") */
            preprocessing_op_or_punc op =
                static_cast<preprocessing_op_or_punc>(x.op);
            if (y.kind == constant_value_kind::pointer &&
                op == preprocessing_op_or_punc::EQUAL) {
                place_value(state, target) = y;
                object = target;
                return constness::constant;
            }
            if (y.kind != constant_value_kind::integer ||
                (op != preprocessing_op_or_punc::PLUS_EQUAL &&
                 op != preprocessing_op_or_punc::MINUS_EQUAL))
                return CONST_UNKNOWN;
            if (!(target & element_place) && state.objects[target].opaque)
                return CONST_UNKNOWN;
            const constant_value old = place_value(state, target);
            if (old.kind != constant_value_kind::pointer)
                return undefined_value(state);
            __int128 n;
            if (!pointer_offset_count(semantics[x.b].type, y, n))
                return CONST_NOT_CONSTANT;
            if (op == preprocessing_op_or_punc::MINUS_EQUAL)
                n = -n;
            constant_value moved;
            c = offset_pointer(old, n, moved);
            if (c != constness::constant)
                return c;
            place_value(state, target) = moved;
            object = target;
            return constness::constant;
        }
        if (t == fundamental_type::none) {
            /* [class.copy.assign]/10: the implicit copy and move
               assignment of a class assigns its members, an array's
               elements, one by one */
            type_kind tk = type_table[semantics[x.a].type.unqualified()].kind;
            if (static_cast<preprocessing_op_or_punc>(x.op) !=
                    preprocessing_op_or_punc::EQUAL ||
                (tk != type_kind::class_type && tk != type_kind::array) ||
                (!is_object_value(y) && y.kind != constant_value_kind::array))
                return CONST_UNKNOWN;
            if (!(target & element_place) && state.objects[target].opaque)
                return CONST_UNKNOWN;
            assign_value(state, target, y);
            object = target;
            return constness::constant;
        }
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
                return CONST_UNKNOWN;
            if (!(target & element_place) && state.objects[target].opaque)
                return opaque_access(state, state.objects[target]);
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
        place_value(state, target) = bit_field_stored(x.a, y);
        object = target;
        return constness::constant;
    }
    case semantic_kind::increment: {
        increment_kind k = static_cast<increment_kind>(x.op);
        if (k != increment_kind::pre_increment &&
            k != increment_kind::pre_decrement)
            return CONST_UNKNOWN;
        /* the operand evaluated again for its object: only one whose
           evaluation has no side effect */
        if (semantics[x.a].kind != semantic_kind::entity)
            return CONST_UNKNOWN;
        constant_value updated;
        c = evaluate_node(state, node, updated);
        if (c != constness::constant)
            return c;
        return evaluate_object(state, x.a, object);
    }
    case semantic_kind::comma: {
        /* [expr.ref]/6.1: the object expression of a static data member's
           name is evaluated, not read ([expr.context]/2: a discarded-value
           expression applies no lvalue-to-rvalue conversion to a glvalue),
           so a member of an inactive union member breaks no rule
           ([class.union]/2, [expr.const]/5.9) */
        if (semantics[x.a].kind == semantic_kind::member) {
            uint32_t outer_reading = state.reading;
            uint32_t discarded_object = 0;
            state.reading = 0;
            state.designating++;
            c = evaluate_object(state, x.a, discarded_object);
            state.designating--;
            state.reading = outer_reading;
        } else {
            constant_value discarded;
            c = evaluate_node(state, x.a, discarded);
        }
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
        /* the same literal evaluated again is the same object */
        for (size_t i = 0; i < state.literals.size(); i++)
            if (state.literals[i].at == x.at) {
                object = state.literals[i].object;
                return constness::constant;
            }
        const struct type &array = type_table[x.type.unqualified()];
        fundamental_type f = evaluated_type(array.operand);
        if (array.kind != type_kind::array || !array.bound_known ||
            array.bound > constant_full_expression_limit ||
            f == fundamental_type::none)
            return CONST_UNKNOWN;
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
        o.literal = true;
        o.value = a;
        object = static_cast<uint32_t>(state.objects.size());
        append_object(state, o);
        constant_evaluation::literal_object entry;
        entry.at = x.at;
        entry.object = object;
        state.literals.append(entry);
        pin_objects(state);
        return constness::constant;
    }
    default:
        break;
    }
    /* [conv.rval]: a prvalue of class type where an object is needed
       (the object expression of a member access or call) materializes a
       temporary */
    if (x.category != value_category::prvalue || !evaluated_class(x.type))
        return CONST_UNKNOWN;
    /* [class.temporary]/4: the temporary's destructor runs at the end of
       the full-expression, which a statement that makes its own ends
       (temporaries_ok) does */
    qualified_type destroyed;
    bool destructor = element_destructor(x.type, destroyed) != 0;
    if (destructor && !state.temporaries_ok)
        return CONST_UNKNOWN;
    constant_value v;
    c = evaluate_node(state, node, v);
    if (c != constness::constant)
        return c;
    if (!is_object_value(v))
        return CONST_UNKNOWN;
    constant_evaluation::object o;
    o.frame = state.frame;
    o.value = v;
    object = static_cast<uint32_t>(state.objects.size());
    append_object(state, o);
    if (destructor)
        add_destroyed(state, x.type);
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
        return CONST_UNKNOWN;
    bool lazy_pointer =
        tk == type_kind::pointer && x.kind == semantic_kind::entity && x.a &&
        entity_table[x.a].constexpr_ && entity_table[x.a].definition &&
        frame_object(state, x.a) == no_frame_object;
    if ((scalar || tk == type_kind::pointer) && !lazy_pointer &&
        x.kind == semantic_kind::entity &&
        frame_object(state, x.a) == no_frame_object) {
        /* [expr.const]/5.8.1: "a non-volatile glvalue that refers to an
           object that is usable in constant expressions" (a pointer
           variable of the enclosing function, a parameter or a local, is
           one only when constexpr, whose value analysis does not keep:
           unknown) */
        const entity &e = entity_table[x.a];
        /* ([expr.prim.this]'s `this` is the one variable of no name: its
           rules are not these) */
        if (e.kind != entity_kind::variable || e.usable == constness::unknown ||
            !e.name)
            return CONST_UNKNOWN;
        if (e.usable == constness::not_constant) {
            /* [expr.const]/4: only a const or constexpr object may be
               usable; another variable (a reference, a const one) may be
               one analysis could not classify (a later definition, a
               template's) */
            if (!e.automatic && !e.constexpr_ && !(e.type.cv() & cv_const) &&
                evaluated_type(e.type) != fundamental_type::none) {
                if (state.potential)
                    return CONST_NOT_CONSTANT;
                return CONST_NOT_CONSTANT_RULE;
            }
            if (e.automatic)
                return CONST_NOT_CONSTANT;
            return CONST_UNKNOWN;
        }
        fundamental_type t = evaluated_type(e.type);
        if (t == fundamental_type::none)
            return CONST_UNKNOWN;
        v = value_of(t, e.value);
        return constness::constant;
    }
    /* [expr.const]/5.8: a scalar member of an automatic class object
       whose lifetime did not begin within the evaluation (the enclosing
       function's local or parameter): only a subobject of a constexpr
       object is usable in constant expressions */
    if (scalar && x.kind == semantic_kind::member) {
        uint32_t root = x.a;
        while (semantics[root].kind == semantic_kind::member)
            root = semantics[root].a;
        if (semantics[root].kind == semantic_kind::entity &&
            frame_object(state, semantics[root].a) == no_frame_object) {
            const entity &e = entity_table[semantics[root].a];
            if (e.kind == entity_kind::variable && e.automatic &&
                !e.constexpr_ &&
                type_table[e.type.unqualified()].kind == type_kind::class_type)
                return CONST_NOT_CONSTANT;
        }
    }
    uint32_t object = 0;
    /* [class.union]/2: a read of a member that is not active; inside the
       read nothing is written (the subscripts' operands) */
    uint32_t outer_writing = state.writing;
    state.writing = 0;
    state.reading++;
    constness c = evaluate_object(state, node, object);
    state.reading--;
    state.writing = outer_writing;
    if (c != constness::constant)
        return c;
    if (!(object & element_place) && state.objects[object].opaque)
        return opaque_access(state, state.objects[object]);
    /* [expr.const]/5.8: a variable that is not usable in constant
       expressions, read through a pointer or a reference */
    if (!(object & element_place) && state.objects[object].unusable)
        return CONST_NOT_CONSTANT;
    /* [basic.life]: the storage of a deleted object */
    if (!(object & element_place) && state.objects[object].freed)
        return CONST_NOT_CONSTANT;
    v = place_value(state, object);
    /* [expr.const]/5.8: the object's value must be one it was given; an
       indeterminate value is undefined behavior ([basic.indet]/2) */
    if (v.kind == constant_value_kind::none)
        return undefined_value(state);
    /* [class.copy.ctor]/14: the implicit copy of a class object, member
       by member */
    if (is_object_value(v)) {
        /* [expr.const]/5.9 (5.10 in C++23): the copy reads the mutable
           members of an object usable in constant expressions */
        if (!(object & element_place) && state.objects[object].read_only &&
            has_mutable_subobject(x.type))
            return CONST_NOT_CONSTANT;
        v = copy_value(state, v, false);
    }
    return constness::constant;
}

/* [class.derived]/2: the place of the base class subobject base inside
   the class object at place (the subobjects with data are members of
   the class, base-clause.cpp), or unknown: an empty base has none */
constness
semantic_analysis::base_subobject_place(const constant_evaluation &state,
                                        uint32_t place, entity_index base,
                                        uint32_t &out) const
{
    if (!base)
        return CONST_UNKNOWN;
    const constant_value &o = place_value(state, place);
    if (o.kind != constant_value_kind::class_object || !o.designator)
        return CONST_UNKNOWN;
    const entity &k = entity_table[o.designator];
    for (uint32_t i = 0; i < k.member_count; i++)
        if (class_members[k.first_member + i] == base) {
            out = element_place | (o.first + i);
            return constness::constant;
        }
    return CONST_UNKNOWN;
}

/* [expr.static.cast]/11: the class object that holds the base subobject at
   place: found among the objects and elements of the evaluation */
constness
semantic_analysis::derived_object_place(const constant_evaluation &state,
                                        uint32_t place, entity_index base,
                                        uint32_t &out) const
{
    if (!base || !(place & element_place))
        return CONST_UNKNOWN;
    uint32_t index = place & ~element_place;
    for (size_t pass = 0; pass < 2; pass++) {
        size_t n = state.objects.size();
        if (pass)
            n = state.elements.size();
        for (size_t i = 0; i < n; i++) {
            const constant_value *v = &state.elements[0];
            if (pass)
                v = &state.elements[i];
            else
                v = &state.objects[i].value;
            if (v->kind != constant_value_kind::class_object ||
                !v->designator || index < v->first ||
                index >= v->first + v->count)
                continue;
            const entity &k = entity_table[v->designator];
            if (class_members[k.first_member + (index - v->first)] != base)
                continue;
            out = static_cast<uint32_t>(i);
            if (pass)
                out |= element_place;
            return constness::constant;
        }
    }
    return CONST_UNKNOWN;
}

/* [conv.ptr]/3, [expr.static.cast]/11: the pointer p converted to the
   base class subobject (to_base) or back to the class object holding it */
constness semantic_analysis::convert_base_pointer(
    const constant_evaluation &state, bool to_base, entity_index base,
    const constant_value &p, constant_value &v) const
{
    if (p.kind != constant_value_kind::pointer)
        return CONST_UNKNOWN;
    v = p;
    if (!p.count)
        return constness::constant;
    uint32_t place = 0;
    constness c = pointed_place(state, p, place);
    if (c != constness::constant)
        return c;
    uint32_t moved = 0;
    if (to_base)
        c = base_subobject_place(state, place, base, moved);
    else
        c = derived_object_place(state, place, base, moved);
    if (c != constness::constant)
        return c;
    v = constant_value();
    v.kind = constant_value_kind::pointer;
    v.count = 1;
    v.first = moved;
    if (!(moved & element_place))
        v.stamp = state.objects[moved].serial;
    return constness::constant;
}

/* the most derived class object the subobject at place is part of: the
   enclosing object while the place is a base class subobject of one */
uint32_t semantic_analysis::most_derived_place(const constant_evaluation &state,
                                               uint32_t place) const
{
    for (size_t guard = 0; guard < 64; guard++) {
        if (!(place & element_place))
            return place;
        uint32_t index = place & ~element_place;
        bool climbed = false;
        for (size_t pass = 0; pass < 2 && !climbed; pass++) {
            size_t n = state.objects.size();
            if (pass)
                n = state.elements.size();
            for (size_t i = 0; i < n; i++) {
                const constant_value *candidate = &state.objects[i].value;
                if (pass)
                    candidate = &state.elements[i];
                const constant_value &o = *candidate;
                if (o.kind != constant_value_kind::class_object ||
                    !o.designator || index < o.first ||
                    index >= o.first + o.count)
                    continue;
                const entity &k = entity_table[o.designator];
                entity_index member =
                    class_members[k.first_member + (index - o.first)];
                if (!entity_table[member].base_subobject)
                    return place;
                place = static_cast<uint32_t>(i);
                if (pass)
                    place |= element_place;
                climbed = true;
                break;
            }
        }
        if (!climbed)
            return place;
    }
    return place;
}

/* the place of the subobject of class target in the class object at place
   (itself, or one of its base class subobjects, depth first) */
bool semantic_analysis::subobject_of_class(const constant_evaluation &state,
                                           uint32_t place, entity_index target,
                                           uint32_t &out) const
{
    const constant_value &o = place_value(state, place);
    if (o.kind != constant_value_kind::class_object)
        return false;
    if (o.designator == target) {
        out = place;
        return true;
    }
    const entity &k = entity_table[o.designator];
    for (uint32_t i = 0; i < k.member_count; i++) {
        if (!entity_table[class_members[k.first_member + i]].base_subobject)
            continue;
        if (subobject_of_class(state, element_place | (o.first + i), target,
                               out))
            return true;
    }
    return false;
}

/* [class.virtual]/16: for the virtual function f called on the object self
   points to: the final overrider g in the dynamic class of the object, and
   a pointer to the subobject of g's class that is its `this` */
constness semantic_analysis::virtual_target(const constant_evaluation &state,
                                            const constant_value &self,
                                            entity_index f, entity_index &g,
                                            constant_value &adjusted) const
{
    uint32_t place = 0;
    constness c = pointed_place(state, self, place);
    if (c != constness::constant)
        return c;
    uint32_t top = most_derived_place(state, place);
    const constant_value &o = place_value(state, top);
    if (o.kind != constant_value_kind::class_object || !o.designator)
        return CONST_UNKNOWN;
    const dynamic_class *d = dynamic_of(o.designator);
    if (!d || !d->group_count)
        return CONST_UNKNOWN;
    const vtable_group &first = vtable_groups[d->first_group];
    const vtable_group &last =
        vtable_groups[d->first_group + d->group_count - 1];
    entity_index found = 0;
    for (uint32_t k = first.first_slot; k < last.first_slot + last.slot_count;
         k++) {
        entity_index slot = vtable_slots[k];
        if (slot == f || overrides_function(slot, f)) {
            found = slot;
            break;
        }
    }
    if (!found || entity_table[found].pure ||
        entity_table[found].kind != entity_kind::function)
        return CONST_UNKNOWN;
    uint32_t sub = 0;
    if (!subobject_of_class(state, top, entity_table[found].enclosing, sub))
        return CONST_UNKNOWN;
    adjusted = constant_value();
    adjusted.kind = constant_value_kind::pointer;
    adjusted.count = 1;
    adjusted.first = sub;
    if (!(sub & element_place))
        adjusted.stamp = state.objects[sub].serial;
    g = found;
    return constness::constant;
}

/* [expr.sub]/1: the pointer E1[E2] designates, "*((E1)+(E2))": the pointer
   operand (an array converted) moved by the index */
constness semantic_analysis::evaluate_subscript_pointer(
    constant_evaluation &state, uint32_t node, constant_value &moved)
{
    const semantic_node x = semantics[node];
    uint32_t pointer_node = x.a, index_node = x.b;
    if (type_table[semantics[pointer_node].type.unqualified()].kind !=
        type_kind::pointer) {
        uint32_t held = pointer_node;
        pointer_node = index_node;
        index_node = held;
    }
    constant_value p, index;
    constness c = evaluate_node(state, pointer_node, p);
    if (c != constness::constant)
        return c;
    c = evaluate_node(state, index_node, index);
    if (c != constness::constant)
        return c;
    if (p.kind != constant_value_kind::pointer ||
        index.kind != constant_value_kind::integer)
        return CONST_UNKNOWN;
    __int128 n;
    if (!pointer_offset_count(semantics[index_node].type, index, n))
        return CONST_NOT_CONSTANT;
    return offset_pointer(p, n, moved);
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
        return CONST_UNKNOWN;
    /* [expr.unary.op]/1, [expr.const]/5.7: the operand of the indirection
       is null: undefined behavior (past the end: not decided, a member
       that is static does not read the object) */
    if (!p.count)
        return CONST_NOT_CONSTANT_RULE;
    if (p.extent && p.offset >= p.extent)
        return CONST_NOT_CONSTANT;
    uint32_t index = p.first;
    if (index & element_place) {
        if ((index & ~element_place) >= state.elements.size())
            return CONST_UNKNOWN;
    } else if (index >= state.objects.size() ||
               (p.stamp && state.objects[index].serial != p.stamp)) {
        /* (a place that is gone, or another object's now) */
        return CONST_UNKNOWN;
    }
    place = index;
    return constness::constant;
}

/* [class.bit]/1: a bit-field holds the value modulo 2^width ([conv.integral],
   as the type of the field when it is signed: the width's sign bit); the
   value v stored to the member the node target names */
constant_value semantic_analysis::bit_field_stored(uint32_t target,
                                                   constant_value v) const
{
    if (semantics[target].kind != semantic_kind::member)
        return v;
    return bit_field_member_stored(semantics[target].b, v);
}

constant_value
semantic_analysis::bit_field_member_stored(entity_index member,
                                           constant_value v) const
{
    const entity &m = entity_table[member];
    if (!m.bit_field || !m.bit_width || m.bit_width >= 64 ||
        v.kind != constant_value_kind::integer)
        return v;
    fundamental_type t = evaluated_type(m.type);
    if (t == fundamental_type::none || fundamental_type_is_floating(t))
        return v;
    unsigned __int128 mask =
        (static_cast<unsigned __int128>(1) << m.bit_width) - 1;
    unsigned __int128 kept = v.integer & mask;
    if (t != fundamental_type::bool_ &&
        fundamental_type_is_signed(tokens.files.behavior, t) &&
        (kept >> (m.bit_width - 1)) & 1)
        kept |= ~mask;
    v.integer = kept;
    return v;
}

/* the integer operand of pointer arithmetic as a signed count; a value an
   unsigned type holds above what a signed count does is no index of any
   array ([expr.add]/4) */
bool semantic_analysis::pointer_offset_count(qualified_type type,
                                             const constant_value &i,
                                             __int128 &n) const
{
    fundamental_type t = evaluated_type(type);
    n = static_cast<__int128>(i.integer);
    if (t == fundamental_type::none ||
        fundamental_type_is_signed(tokens.files.behavior, t))
        return true;
    return i.integer <= static_cast<unsigned __int128>(INT64_MAX);
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
        return CONST_UNKNOWN;
    moved = p;
    if (n == 0)
        return constness::constant;
    if (!p.count)
        return CONST_NOT_CONSTANT;
    if (!p.extent || p.base == single_object_base) {
        /* [expr.add]/4.2 ([basic.compound]/3.3): "an object that is not an
           array element is considered to belong to an array with one
           element of that type": the pointer to it moved by 0 or 1 */
        __int128 j = n;
        if (p.extent)
            j += static_cast<__int128>(p.offset);
        if (j < 0 || j > 1)
            return CONST_NOT_CONSTANT;
        moved.offset = static_cast<uint64_t>(j);
        moved.extent = 0;
        moved.base = 0;
        if (j) {
            moved.extent = 1;
            moved.base = single_object_base;
        }
        return constness::constant;
    }
    __int128 i = static_cast<__int128>(p.offset) + n;
    /* [expr.add]/4.2, [expr.const]/5.7: undefined behavior */
    if (i < 0 || i > static_cast<__int128>(p.extent))
        return CONST_NOT_CONSTANT_RULE;
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
    /* (the decay of an array the evaluation has no value for: its address
       is not read, [expr.const]/5.8 does not apply) */
    if (!(array & element_place) && state.objects[array].opaque)
        return CONST_UNKNOWN;
    const constant_value a = place_value(state, array);
    if (a.kind != constant_value_kind::array || !a.count)
        return CONST_UNKNOWN;
    v = constant_value();
    v.kind = constant_value_kind::pointer;
    v.count = 1;
    v.base = a.first;
    v.extent = a.count;
    v.offset = 0;
    v.first = element_place | a.first;
    v.literal = !(array & element_place) && state.objects[array].literal;
    /* each evaluation of a string literal is its own for a comparison
       ([lex.string]/9: whether they are one object is unspecified): the
       pointers made from it share the version */
    if (v.literal)
        v.stamp = ++state.serials;
    return constness::constant;
}

/* [dcl.init.ref]/5.1: a reference bound to a glvalue refers to its object;
   to a prvalue ([dcl.init.ref]/5.2, a temporary) is not modelled */
constness semantic_analysis::evaluate_binding(constant_evaluation &state,
                                              uint32_t node, constant_value &v)
{
    uint32_t bound = semantics[node].a;
    if (semantics[bound].category == value_category::prvalue)
        return CONST_UNKNOWN;
    uint32_t object = 0;
    constness c = evaluate_object(state, bound, object);
    if (c != constness::constant)
        return c;
    v = constant_value();
    v.kind = constant_value_kind::pointer;
    v.count = 1;
    v.first = object;
    if (!(object & element_place))
        v.stamp = state.objects[object].serial;
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
                return CONST_UNKNOWN;
            if (!p.count && !q.count) {
                v = value_of(result, 0);
                return constness::constant;
            }
            /* [lex.string]/9: two evaluations of a literal may be two
               objects: the difference of their addresses is unspecified */
            if (p.count && q.count && p.literal && q.literal &&
                p.stamp != q.stamp) {
                /* (a function that does it is not "never constant": the
                   objects depend on the evaluation, [impl.clang] as clang's
                   check) */
                if (state.potential)
                    return CONST_UNKNOWN;
                return CONST_NOT_CONSTANT;
            }
            /* [expr.add]/5.2: both point to elements of the same array */
            if (!p.count || !q.count || !p.extent || p.extent != q.extent ||
                p.base != q.base ||
                (p.base == single_object_base && p.first != q.first))
                return CONST_UNKNOWN;
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
            return CONST_UNKNOWN;
        uint32_t integer_node = x.a;
        if (left_pointer)
            integer_node = x.b;
        __int128 n;
        if (!pointer_offset_count(semantics[integer_node].type, integer, n))
            return CONST_NOT_CONSTANT;
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
        return CONST_UNKNOWN;
    int order = 0;
    /* [expr.eq]/3.1: whether two distinct string literals share an object
       is unspecified: [impl.clang] clang does not take the comparison of the
       address of a literal with one of an object as a constant */
    if (p.count && q.count && (p.literal || q.literal) &&
        !(p.literal && q.literal && p.stamp == q.stamp && p.base == q.base)) {
        if (state.potential)
            return CONST_UNKNOWN;
        return CONST_NOT_CONSTANT;
    }
    if (!p.count || !q.count) {
        /* [expr.eq]/3: null pointers compare equal; a pointer to an object
           is not null */
        if (relational)
            return CONST_UNKNOWN;
        order = 0;
        if (p.count != q.count)
            order = 1;
    } else if ((!p.extent || p.base == single_object_base) &&
               (!q.extent || q.base == single_object_base) &&
               p.first == q.first) {
        /* the same place; of the same object unless one of them is gone */
        if (p.stamp != q.stamp)
            return CONST_UNKNOWN;
        /* the same object, as a one-element array: [expr.add]/4.2 */
        uint64_t i = 0, j = 0;
        if (p.extent)
            i = p.offset;
        if (q.extent)
            j = q.offset;
        if (i < j)
            order = -1;
        else if (i > j)
            order = 1;
    } else if (p.extent && p.extent == q.extent && p.base == q.base &&
               p.base != single_object_base) {
        if (p.offset < q.offset)
            order = -1;
        else if (p.offset > q.offset)
            order = 1;
    } else if (!p.extent && !q.extent && equality) {
        if (p.first != q.first)
            order = 1;
    } else {
        return CONST_UNKNOWN;
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
        /* (a null pointer or void conversion is made by the conversion below) */
        type_kind k = type_table[x.type.unqualified()].kind;
        if (k != type_kind::class_type && k != type_kind::pointer &&
            k != type_kind::complex &&
            fundamental_of(x.type) != fundamental_type::void_)
            return CONST_UNKNOWN;
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
                return CONST_UNKNOWN;
            v = constant_value();
            v.kind = constant_value_kind::pointer;
            return constness::constant;
        }
        /* [expr.const]/5.15 (before C++26): "a conversion from type
           cv void* to a pointer-to-object type" is not a constant
           expression", except the result of an allocation function called
           by std::allocator<T>::allocate */
        if (k == conversion_kind::pointer_cast &&
            !tokens.files.behavior.has(
                language_feature::constant_cast_from_void_pointer)) {
            const struct type from =
                type_table[semantics[x.a].type.unqualified()];
            const struct type to = type_table[x.type.unqualified()];
            if (from.kind == type_kind::pointer &&
                to.kind == type_kind::pointer &&
                fundamental_of(from.operand) == fundamental_type::void_ &&
                type_table[to.operand.unqualified()].kind !=
                    type_kind::function &&
                fundamental_of(to.operand) != fundamental_type::void_) {
                /* (the operand is evaluated first: an allocation function's
                   result, which the evaluator has no rules for, is the
                   exception) */
                c = evaluate_node(state, x.a, v);
                if (c != constness::constant)
                    return c;
                return CONST_NOT_CONSTANT_RULE;
            }
        }
        /* [conv.qual]: the same address, the pointed-to type differing
           in cv-qualification only */
        if (k == conversion_kind::pointer ||
            k == conversion_kind::qualification_cast) {
            const struct type from =
                type_table[semantics[x.a].type.unqualified()];
            const struct type to = type_table[x.type.unqualified()];
            /* ([conv.ptr]/2: to cv void *, the same address, which nothing
               here reads through) */
            if (from.kind != type_kind::pointer ||
                to.kind != type_kind::pointer ||
                (from.operand.unqualified() != to.operand.unqualified() &&
                 fundamental_of(to.operand) != fundamental_type::void_))
                return CONST_UNKNOWN;
            c = evaluate_node(state, x.a, v);
            if (c == constness::constant &&
                v.kind != constant_value_kind::pointer)
                return CONST_UNKNOWN;
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
                return CONST_UNKNOWN;
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
                return CONST_UNKNOWN;
            if (!bit_cast_kept(tokens.files.behavior, from, to,
                               kept_of(from, v), value))
                return CONST_UNKNOWN;
            v = value_of(to, value);
            return c;
        }
        if (k == conversion_kind::derived_to_base ||
            k == conversion_kind::base_to_derived) {
            constant_value p;
            c = evaluate_node(state, x.a, p);
            if (c != constness::constant)
                return c;
            return convert_base_pointer(
                state, k == conversion_kind::derived_to_base, x.b, p, v);
        }
        if (k != conversion_kind::integral && k != conversion_kind::floating &&
            k != conversion_kind::floating_integral &&
            k != conversion_kind::boolean)
            return CONST_UNKNOWN;
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
            return CONST_NOT_CONSTANT;
        /* [expr.static.cast]/10: a cast's integral value outside the
           range of an enumeration without a fixed underlying type
           ([dcl.enum]/8) is undefined behavior, no core constant
           expression ([expr.const]/5); a floating operand converted to the
           underlying type first is not checked (cast-expression.cpp) */
        if (k == conversion_kind::integral &&
            v.kind == constant_value_kind::integer &&
            type_table[x.type.unqualified()].kind == type_kind::enumeration) {
            entity_index e = enumeration_of(x.type);
            fundamental_type from = evaluated_type(semantics[x.a].type);
            const semantic_node &operand = semantics[x.a];
            bool from_floating = operand.kind == semantic_kind::conversion &&
                                 static_cast<conversion_kind>(operand.op) ==
                                     conversion_kind::floating_integral;
            if (e && !entity_table[e].fixed_underlying &&
                entity_table[e].defined && !from_floating &&
                fundamental_type_is_integral(from) &&
                !enumeration_holds(entity_table[e],
                                   integer_value(tokens.files.behavior, from,
                                                 kept_of(from, v))))
                return CONST_NOT_CONSTANT;
        }
        return evaluate_conversion(v, evaluated_type(semantics[x.a].type), to);
    }
    case semantic_kind::unary: {
        fundamental_type t = evaluated_type(x.type);
        if (t == fundamental_type::none)
            return CONST_UNKNOWN;
        c = evaluate_node(state, x.a, v);
        if (c != constness::constant)
            return c;
        /* the operand is promoted to t, or a bool for ! */
        fundamental_type operand = evaluated_type(semantics[x.a].type);
        if (wide_integer(operand)) {
            auto op = static_cast<preprocessing_op_or_punc>(x.op);
            if (v.kind != constant_value_kind::integer)
                return CONST_UNKNOWN;
            if (op == preprocessing_op_or_punc::EXCLAMATION) {
                v = value_of(t, v.integer == 0);
                return constness::constant;
            }
            if (op == preprocessing_op_or_punc::PLUS)
                return constness::constant;
            if (op == preprocessing_op_or_punc::TILDE) {
                v.integer = ~v.integer;
                return constness::constant;
            }
            if (op == preprocessing_op_or_punc::MINUS) {
                if (t == fundamental_type::int128 &&
                    v.integer == (static_cast<unsigned __int128>(1) << 127))
                    return CONST_NOT_CONSTANT;
                v.integer = 0 - v.integer;
                return constness::constant;
            }
            return CONST_UNKNOWN;
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
        if (x.op == static_cast<uint8_t>(preprocessing_op_or_punc::SPACESHIP) &&
            type_table[x.type.unqualified()].kind == type_kind::class_type) {
            fundamental_type operands = evaluated_type(semantics[x.a].type);
            if (operands == fundamental_type::none ||
                evaluated_type(semantics[x.b].type) != operands)
                return CONST_UNKNOWN;
            constant_value p, q;
            c = evaluate_node(state, x.a, p);
            if (c != constness::constant)
                return c;
            c = evaluate_node(state, x.b, q);
            if (c != constness::constant)
                return c;
            return evaluate_spaceship(state, x, operands, p, q, v);
        }
        if (type_table[semantics[x.a].type.unqualified()].kind ==
                type_kind::pointer ||
            type_table[semantics[x.b].type.unqualified()].kind ==
                type_kind::pointer)
            return evaluate_pointer_binary(state, node, v);
        fundamental_type t = evaluated_type(semantics[x.a].type);
        fundamental_type right = evaluated_type(semantics[x.b].type);
        if (t == fundamental_type::none || right == fundamental_type::none ||
            evaluated_type(x.type) == fundamental_type::none)
            return CONST_UNKNOWN;
        constant_value p, q;
        c = evaluate_node(state, x.a, p);
        if (c == constness::unknown && state.potential &&
            evaluate_node(state, x.b, q) == constness::not_constant)
            return CONST_NOT_CONSTANT;
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
        /* [ext:gnu_conditional_omitted] a ? : c has no b: the first
           operand's value (d) is the result when a is true */
        uint32_t second = x.b;
        if (!second)
            second = x.d;
        c = evaluate_node(state, x.a, v);
        if (c == constness::unknown && state.potential) {
            constant_value arm;
            if (evaluate_node(state, second, arm) == constness::not_constant &&
                evaluate_node(state, x.c, arm) == constness::not_constant)
                return CONST_NOT_CONSTANT;
            return c;
        }
        if (c != constness::constant)
            return c;
        if (v.integer != 0)
            return evaluate_node(state, second, v);
        return evaluate_node(state, x.c, v);
    }
    case semantic_kind::comma: {
        c = evaluate_node(state, x.a, v);
        if (c != constness::constant)
            return c;
        return evaluate_node(state, x.b, v);
    }
    case semantic_kind::complex_value: {
        constant_value re, im;
        c = evaluate_node(state, x.a, re);
        if (c != constness::constant)
            return c;
        c = evaluate_node(state, x.b, im);
        if (c != constness::constant)
            return c;
        v = constant_value();
        v.kind = constant_value_kind::array;
        v.first = static_cast<uint32_t>(state.elements.size());
        v.count = 2;
        state.elements.append(re);
        state.elements.append(im);
        state.element_read_only.append(0);
        state.element_read_only.append(0);
        return constness::constant;
    }
    case semantic_kind::complex_part: {
        constant_value cv;
        if (semantics[x.a].category != value_category::prvalue) {
            uint32_t obj = 0;
            c = evaluate_object(state, x.a, obj);
            if (c != constness::constant)
                return c;
            cv = place_value(state, obj);
        } else {
            c = evaluate_node(state, x.a, cv);
            if (c != constness::constant)
                return c;
        }
        if (cv.kind != constant_value_kind::array || cv.count != 2)
            return CONST_UNKNOWN;
        if (x.op == 0) {
            v = state.elements[cv.first];
            return constness::constant;
        }
        if (x.op == 1) {
            v = state.elements[cv.first + 1];
            return constness::constant;
        }
        return CONST_UNKNOWN;
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
            return CONST_UNKNOWN;
        uint32_t object = 0;
        c = evaluate_object(state, x.a, object);
        if (c != constness::constant)
            return c;
        if (place_read_only(state, object))
            return CONST_NOT_CONSTANT;
        if (!(object & element_place) && state.objects[object].opaque)
            return opaque_access(state, state.objects[object]);
        constant_value old = place_value(state, object);
        if (old.kind == constant_value_kind::none)
            return undefined_value(state);
        increment_kind k = static_cast<increment_kind>(x.op);
        if (pointer) {
            /* [expr.pre.incr]/1: the pointer moved by one element
               ([expr.add]/4) */
            if (old.kind != constant_value_kind::pointer)
                return CONST_UNKNOWN;
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
        place_value(state, object) = bit_field_stored(x.a, updated);
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
        append_object(state, o);
        c = evaluate_initialization(state, x.type, node, false, object);
        if (c == constness::constant)
            v = state.objects[object].value;
        return c;
    }
    /* [expr.unary.op]/3: "The result of the unary & operator is a pointer
       to its operand" */
    case semantic_kind::address: {
        uint32_t object = 0;
        /* the address of an array element is a pointer into its array */
        if (semantics[x.a].kind == semantic_kind::subscript) {
            c = evaluate_subscript_pointer(state, x.a, v);
            if (c != constness::constant)
                return c;
            /* (one past the end is a pointer, not an object) */
            uint32_t place = 0;
            if (v.count && !(v.extent && v.offset >= v.extent))
                c = pointed_place(state, v, place);
            return c;
        }
        c = evaluate_object(state, x.a, object);
        if (c != constness::constant)
            return c;
        v = constant_value();
        v.kind = constant_value_kind::pointer;
        v.first = object;
        v.count = 1;
        if (!(object & element_place))
            v.stamp = state.objects[object].serial;
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
    /* [expr.new]/10, [expr.const]/5.18 (C++20): a new-expression is
       constant when its storage is deallocated within the evaluation: the
       allocation function is not called, the object is a heap one that
       stays until a delete frees it (the allocated nodes of the
       initialization are its place) */
    case semantic_kind::new_object: {
        const struct type pointer = type_table[x.type.unqualified()];
        if (pointer.kind != type_kind::pointer || x.d)
            return CONST_UNKNOWN;
        /* [expr.new]/10: the placement arguments are evaluated (the
           size, the first argument, is the allocation function's own) */
        if (x.a && semantics[x.a].kind == semantic_kind::call) {
            const semantic_node call = semantics[x.a];
            const uint32_t *arguments = semantics.items(call.first);
            for (uint32_t i = 1; i < call.count; i++) {
                constant_value placed;
                c = evaluate_node(state, arguments[i], placed);
                if (c != constness::constant)
                    return c;
            }
        }
        /* [new.delete.placement]/2: the reserved placement form makes no
           call, the pointer is the allocation and the object constructed
           is the one it designates: not followed yet (a new object here
           would leave the designated one indeterminate) */
        if (x.a && semantics[x.a].kind != semantic_kind::call)
            return CONST_UNKNOWN;
        constant_evaluation::object o;
        o.heap = true;
        uint32_t place = static_cast<uint32_t>(state.objects.size());
        append_object(state, o);
        pin_objects(state);
        state.heap_live++;
        c = evaluate_initialization(state, pointer.operand, 0, false, place);
        if (c != constness::constant)
            return c;
        if (x.b) {
            state.allocations.append(place);
            if (semantics[x.b].kind == semantic_kind::construct) {
                /* the constructor, called on the object */
                constant_value stored;
                c = evaluate_node(state, x.b, stored);
            } else {
                /* the value the object is initialized with */
                c = evaluate_initialization(state, pointer.operand, x.b, false,
                                            place);
            }
            state.allocations.resize(state.allocations.size() - 1);
            if (c != constness::constant)
                return c;
        }
        v = constant_value();
        v.kind = constant_value_kind::pointer;
        v.count = 1;
        v.first = place;
        v.stamp = state.objects[place].serial;
        return constness::constant;
    }
    case semantic_kind::allocated: {
        if (state.allocations.empty())
            return CONST_UNKNOWN;
        uint32_t place = state.allocations[state.allocations.size() - 1];
        v = constant_value();
        v.kind = constant_value_kind::pointer;
        v.count = 1;
        v.first = place;
        v.stamp = state.objects[place].serial;
        return constness::constant;
    }
    /* [expr.delete]/9: the destructor, then the storage is gone; the
       operand shall point to an object a new-expression made */
    case semantic_kind::delete_object: {
        constant_value p;
        c = evaluate_node(state, x.a, p);
        if (c != constness::constant)
            return c;
        v = constant_value();
        if (p.kind != constant_value_kind::pointer)
            return CONST_UNKNOWN;
        if (!p.count)
            return constness::constant;
        if (x.d || x.value)
            return CONST_UNKNOWN;
        uint32_t place = 0;
        c = pointed_place(state, p, place);
        if (c != constness::constant)
            return c;
        if ((place & element_place) || !state.objects[place].heap ||
            state.objects[place].freed)
            return CONST_NOT_CONSTANT;
        if (x.b) {
            state.allocations.append(place);
            constant_value discarded;
            c = evaluate_node(state, x.b, discarded);
            state.allocations.resize(state.allocations.size() - 1);
            if (c != constness::constant)
                return c;
        }
        state.objects[place].freed = true;
        state.heap_live--;
        return constness::constant;
    }
    /* [class.cdtor]/4: the virtual table pointer a constructor stores is
       the object's dynamic type, which the evaluator finds from the class
       of the object itself */
    case semantic_kind::vtable_pointer_store:
        v = constant_value();
        return constness::constant;
    /* [class.dtor]/13: the destructor called on the object (a
       subobject's, after the body of a destructor; an explicit call) */
    case semantic_kind::destroy: {
        uint32_t place = 0;
        c = evaluate_object(state, x.a, place);
        if (c != constness::constant)
            return c;
        v = constant_value();
        return destroy_object(state, place, semantics[x.a].type);
    }
    /* initialization: [dcl.init], [class.dtor] */
    case semantic_kind::bind_reference:
    default:
        return CONST_UNKNOWN;
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
        return CONST_UNKNOWN;
    /* [meta.const.eval]/1: true within a manifestly constant-evaluated
       expression ([expr.const]/14), which this evaluation is unless it is
       potential_constant's or the claims' */
    if (entity_table[semantics[callee].a].gnu_flags &
        gnu_is_constant_evaluated) {
        if (state.potential || state.any_expression)
            return CONST_UNKNOWN;
        v = constant_value();
        v.kind = constant_value_kind::integer;
        v.integer = 1;
        return constness::constant;
    }
    /* [ext:gnu_library_builtins] clang evaluates the bit counts */
    if (uint16_t row = entity_table[semantics[callee].a].gnu_builtin_row) {
        const gnu_builtin &b = gnu_builtin_at(row);
        /* the fold table says whether it is evaluated at all */
        if (b.fold != builtin_fold::none && !gnu_builtin_folds(b))
            return CONST_NOT_CONSTANT;
        switch (b.fold) {
        case builtin_fold::bit_count:
            return evaluate_bit_count(state, b.which, x, v);
        case builtin_fold::constant_p:
        case builtin_fold::lock_free:
            return evaluate_query_builtin(state, b.which, x, v);
        case builtin_fold::floating_constant:
            return evaluate_floating_builtin(state, semantics[callee].a, x, v);
        case builtin_fold::identity:
            return evaluate_node(state, semantics.items(x.first)[0], v);
        case builtin_fold::bswap:
            return evaluate_bswap(state, x, v);
        case builtin_fold::stdc_bits:
            return evaluate_stdc_bits(state, b.which, x, v);
        case builtin_fold::align:
            return evaluate_align(state, b.which, x, v);
        default:
            return evaluate_builtin_fold(state, b, x, v);
        }
    }
    /* __builtin_assume_aligned (p, alignment [, offset]): p, which clang
       also checks against the asserted alignment (not done here: the call
       is a constant expression, whose pointer is the first argument) */
    if (!x.b && x.count >= 2) {
        static const char assume_aligned[] = "__builtin_assume_aligned";
        const entity &callee_entity = entity_table[semantics[callee].a];
        if (callee_entity.name &&
            callee_entity.name ==
                tokens.pp.identifier(assume_aligned,
                                     sizeof assume_aligned - 1) +
                    1)
            return evaluate_node(state, semantics.items(x.first)[0], v);
    }
    /* [class.this]/1: a non-static member function's this, the address
       of the object expression */
    constant_value self;
    if (x.b) {
        constness c = evaluate_node(state, x.b, self);
        if (c != constness::constant)
            return c;
        if (self.kind != constant_value_kind::pointer)
            return CONST_UNKNOWN;
        /* [expr.ref]/2: the object expression is `*(this)`: a null pointer
           or one past the end designates no object */
        if (!self.count || (self.extent && self.offset >= self.extent))
            return CONST_NOT_CONSTANT;
    }
    entity_index function = semantics[callee].a;
    /* [expr.const]/5.4 (C++20 allowed it): an invocation of a virtual
       function, called through its dynamic type or not */
    if (entity_table[function].virtual_ &&
        tokens.files.behavior.edition < standard_edition::cplusplus_20)
        return CONST_NOT_CONSTANT;
    if (x.c == 1 && x.b) {
        /* [class.virtual]/16: the final overrider in the object's dynamic
           class, called on its own subobject */
        constness c = virtual_target(state, self, function, function, self);
        if (c != constness::constant)
            return c;
    }
    bool returned = false;
    constant_value result;
    constness c = evaluate_invocation(state, function, self, x.first, x.count,
                                      returned, result);
    if (c != constness::constant)
        return c;
    if (fundamental_of(x.type) == fundamental_type::void_) {
        v = result;
        return constness::constant;
    }
    /* [stmt.return]/2: "flowing off the end of a function other than main
       ... results in undefined behavior" for one that returns a value */
    if (!returned)
        return CONST_NOT_CONSTANT;
    /* a pointer to what the call made is gone with it: its stamp tells */
    if (!is_value(result) && !is_object_value(result) &&
        result.kind != constant_value_kind::pointer)
        return CONST_UNKNOWN;
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
        if (taken.function)
            entity_table[taken.function].member_body_deferred = false;
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
        return CONST_UNKNOWN;
    const entity &e = entity_table[f];
    if (e.kind != entity_kind::function)
        return CONST_UNKNOWN;
    /* [expr.prim.lambda.closure]/4: a closure's function call operator is
       a constexpr function "if it satisfies the requirements for a
       constexpr function": evaluated, and not a constant expression where
       its body is not one */
    bool implicit_constexpr =
        (e.enclosing && entity_table[e.enclosing].lambda &&
         entity_table[e.enclosing].call_operator == f) ||
        (e.implicit && e.definition);
    if (!e.constexpr_ && !implicit_constexpr) {
        /* (a library builtin, declared with C language linkage, may be
           one the evaluator does not know clang evaluates) */
        if (state.potential && e.c_language_linkage)
            return CONST_UNKNOWN;
        /* an implicitly declared special member is constexpr when its
           definition would satisfy [dcl.constexpr]'s rules
           ([class.copy.ctor]/12): its synthesized definition is run, and
           one that calls what is not constexpr is no constant expression
           itself, the same answer; one with no definition (not analysed)
           is not known */
        if (e.implicit)
            return CONST_UNKNOWN;
        /* [expr.prim.lambda.closure]/4: a closure's function call operator
           is constexpr if it satisfies those rules, not decided yet; its
           conversion function and the function it returns (/11) too */
        if (e.enclosing && entity_table[e.enclosing].lambda)
            return CONST_UNKNOWN;
        /* [expr.const]/5.2: a call of a function that is not constexpr */
        return CONST_NOT_CONSTANT_RULE;
    }
    /* a defaulted constructor's default member initializers with a
       location builtin were folded at the wrong place */
    if (e.implicit && e.enclosing) {
        const entity &k = entity_table[e.enclosing];
        for (uint32_t i = 0; i < k.member_count; i++)
            if (entity_table[class_members[k.first_member + i]]
                    .initializer_located)
                return CONST_UNKNOWN;
    }
    analyse_postponed_body(f);
    uint32_t body = entity_table[f].definition;
    uint32_t with_this = 0;
    if (self.kind == constant_value_kind::pointer)
        with_this = 1;
    if (!body || semantics[body].kind != semantic_kind::function_body ||
        semantics[body].evaluation == constness::not_constant ||
        semantics[body].count != count + with_this ||
        (state.defined_before && semantics[body].at > state.defined_before)) {
        if (entity_table[f].held_because)
            evaluation_cause = entity_table[f].held_because;
        /* [expr.const]/5.3: "an invocation of an undefined constexpr
           function": declared, with no definition anywhere yet (not a
           template's specialization, not one whose body failed analysis) */
        if (!body && !state.potential && !entity_table[f].defined &&
            !entity_table[f].definition && !entity_table[f].implicit &&
            !entity_table[f].instantiation && !entity_table[f].held_because &&
            !entity_table[f].immediate && !entity_table[f].gnu_flags)
            return CONST_NOT_CONSTANT;
        return CONST_UNKNOWN;
    }
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
        append_object(state, o);
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
        /* clang: "null passed to a callee that requires a non-null
           argument" is not a constant expression; which argument the
           attribute names is not modelled */
        if ((e.gnu_flags & gnu_nonnull) &&
            o.value.kind == constant_value_kind::pointer && !o.value.count) {
            restore(state, saved,
                    kept_elements(state, saved_elements, returned, result));
            return CONST_UNKNOWN;
        }
        o.entity = parameters[i + with_this];
        o.frame = frame;
        append_object(state, o);
        /* [expr.call]/8: a parameter of a class type with a destructor is
           destroyed when the function returns (the order of the
           parameters' destruction is reverse of their making) */
        qualified_type destroyed;
        qualified_type declared =
            type_table.parameters(entity_table[f].type.unqualified())[i];
        if (element_destructor(declared, destroyed))
            add_destroyed(state, declared);
    }
    if (arguments != constness::constant) {
        restore(state, saved,
                kept_elements(state, saved_elements, returned, result));
        return arguments;
    }
    uint32_t caller = state.frame;
    /* the callee's statements end their own full-expressions */
    uint32_t caller_temporaries = state.temporaries_ok;
    uint32_t caller_writing = state.writing, caller_reading = state.reading;
    state.temporaries_ok = 0;
    state.writing = 0;
    state.reading = 0;
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
    /* [class.dtor]/9: after a destructor's body, the destructors of its
       members and bases (the body's list of them) */
    if (c == constness::constant && semantics[body].c) {
        bool destructions_returned = false;
        constant_value discarded;
        c = evaluate_statement(state, semantics[body].c, destructions_returned,
                               discarded);
    }
    state.depth--;
    state.frame = caller;
    state.temporaries_ok = caller_temporaries;
    state.writing = caller_writing;
    state.reading = caller_reading;
    /* a class object returned keeps its elements */
    if (is_object_value(result))
        saved_elements = state.elements.size();
    return leave_scope(state, saved, saved_elements, returned, result, c);
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
           class with no members and no bases does nothing; one with a
           synthesized definition (default member initializers) is evaluated
           below ([class.default.ctor]/4) */
        if (!x.count) {
            if (c_entity && !entity_table[c_entity].member_count)
                return constness::constant;
            if (!k.definition || !c_entity)
                return CONST_UNKNOWN;
        } else {
            if (x.count != 1 || !c_entity ||
                !copy_or_move_constructor(c_entity, x.b))
                return CONST_UNKNOWN;
            uint32_t argument = semantics.items(x.first)[0];
            if (semantics[argument].kind != semantic_kind::bind_reference)
                return CONST_UNKNOWN;
            uint32_t from = 0;
            constness c = evaluate_object(state, semantics[argument].a, from);
            if (c != constness::constant)
                return c;
            constant_value v = place_value(state, from);
            if (!is_object_value(v))
                return CONST_UNKNOWN;
            if (!(from & element_place) && state.objects[from].read_only &&
                has_mutable_subobject(semantics[argument].type))
                return CONST_NOT_CONSTANT;
            constant_value copied = copy_value(state, v, false);
            place_value(state, place) = copied;
            return constness::constant;
        }
    }
    constant_value self;
    self.kind = constant_value_kind::pointer;
    self.first = place;
    self.count = 1;
    bool returned = false;
    constant_value result;
    /* [dcl.init]/8.2: the zeros of a value-initialization are kept by the
       implicit constructor's base and member constructions, not by a
       user-provided constructor's */
    bool saved = state.zeroed;
    if (x.op)
        state.zeroed = true;
    else if (!k.implicit)
        state.zeroed = false;
    constness c = evaluate_invocation(state, x.b, self, x.first, x.count,
                                      returned, result);
    state.zeroed = saved;
    return c;
}

const semantic_analysis::ordering_category *
semantic_analysis::ordering_category_values(entity_index category) const
{
    for (uint32_t i = 0; i < ordering_categories.size(); i++)
        if (ordering_categories[i].category == category)
            return &ordering_categories[i];
    return nullptr;
}

const semantic_analysis::ordering_category &
semantic_analysis::ordering_values(entity_index category)
{
    const ordering_category *known = ordering_category_values(category);
    if (known)
        return *known;
    ordering_category record;
    record.category = category;
    record.known = ordering_values_evaluated(category, record.values);
    ordering_categories.append(record);
    return ordering_categories[ordering_categories.size() - 1];
}

bool semantic_analysis::ordering_values_evaluated(entity_index category,
                                                  int8_t (&values)[4])
{
    static const char *const names[4] = {"less", "equivalent", "greater",
                                         "unordered"};
    const entity &c = entity_table[category];
    if (c.member_count != 1 ||
        evaluated_type(entity_table[class_members[c.first_member]].type) ==
            fundamental_type::none)
        return false;
    for (int i = 0; i < 4; i++) {
        lookup_result found = lookup_qualified(
            category,
            tokens.pp.identifier(names[i],
                                 static_cast<uint32_t>(strlen(names[i]))) +
                1,
            lookup_filter::any);
        /* (a strong ordering has no unordered) */
        if (!found.entity && i == 3)
            continue;
        if (!found.entity || found.ambiguous)
            return false;
        const entity &e = entity_table[found.entity];
        if (e.kind != entity_kind::variable || !e.constexpr_ || !e.definition)
            return false;
        constant_evaluation state;
        constant_evaluation::object o;
        append_object(state, o);
        constness k =
            evaluate_initialization(state, e.type, e.definition, false, 0);
        const constant_value &v = state.objects[0].value;
        if (k != constness::constant ||
            v.kind != constant_value_kind::class_object || v.count != 1 ||
            state.elements[v.first].kind != constant_value_kind::integer)
            return false;
        __int128 value = static_cast<__int128>(state.elements[v.first].integer);
        if (value < -128 || value > 127)
            return false;
        values[i] = static_cast<int8_t>(value);
    }
    return true;
}

/*
 * [expr.spaceship]/4-7: "the result is of type std::strong_ordering ...
 * partial_ordering": p <=> q of the converted arithmetic operands is the
 * category's less, equivalent or greater by p < q, p == q, p > q, and for
 * a floating type with a NaN, partial_ordering::unordered ([cmp.partialord]).
 */
constness semantic_analysis::evaluate_spaceship(
    constant_evaluation &state, const semantic_node &x, fundamental_type t,
    const constant_value &p, const constant_value &q, constant_value &v)
{
    if (type_table[x.type.unqualified()].kind != type_kind::class_type)
        return CONST_UNKNOWN;
    entity_index category = type_table[x.type.unqualified()].entity;
    const ordering_category &known = ordering_values(category);
    if (!known.known)
        return CONST_UNKNOWN;
    int64_t values[4];
    for (int i = 0; i < 4; i++)
        values[i] = known.values[i];
    static const preprocessing_op_or_punc ops[3] = {
        preprocessing_op_or_punc::LESS, preprocessing_op_or_punc::EQUAL_EQUAL,
        preprocessing_op_or_punc::GREATER};
    int64_t chosen = values[3];
    for (int i = 2; i >= 0; i--) {
        constant_value r;
        constness c = evaluate_binary(static_cast<uint8_t>(ops[i]), t, t,
                                      fundamental_type::int_, p, q, r);
        if (c != constness::constant)
            return c;
        if (r.integer != 0)
            chosen = values[i];
    }
    const entity &k = entity_table[category];
    fundamental_type member =
        evaluated_type(entity_table[class_members[k.first_member]].type);
    v = constant_value();
    v.kind = constant_value_kind::class_object;
    v.designator = category;
    v.first = static_cast<uint32_t>(state.elements.size());
    v.count = 1;
    state.elements.append(value_of(member, static_cast<uint64_t>(chosen)));
    state.element_read_only.append(0);
    return constness::constant;
}

constness semantic_analysis::evaluate_variable(qualified_type t, uint32_t init,
                                               token_index at, uint32_t &value)
{
    value = 0;
    constant_evaluation state;
    constant_evaluation::object o;
    append_object(state, o);
    constness c = evaluate_initialization(state, t, init, false, 0);
    if (c == constness::constant && state.heap_live)
        c = constness::not_constant;
    if (c == constness::constant)
        c = permitted_result(state, state.objects[0].value);
    if (c == constness::constant)
        value = value_tree(state, t, state.objects[0].value, at);
    return c;
}

/* [expr.const]/13 (/11 before C++23): "A constant expression is ... an
   expression whose result ... if it is a pointer, it points to an object
   with static storage duration, one past it, a function or is null": a
   pointer into this evaluation's own temporaries and locals is not one.
   Every other value is. */
constness semantic_analysis::permitted_result(const constant_evaluation &state,
                                              const constant_value &v) const
{
    if (v.kind == constant_value_kind::array || is_object_value(v)) {
        for (uint32_t i = 0; i < v.count; i++) {
            constness c = permitted_result(state, state.elements[v.first + i]);
            if (c != constness::constant)
                return c;
        }
        return constness::constant;
    }
    if (v.kind != constant_value_kind::pointer || !v.count)
        return constness::constant;
    uint32_t place = v.first;
    if (place & element_place) {
        /* an element: of an array that is a variable or a string literal
           of this evaluation, none of which is a local (the invocations
           are over: frame 0) */
        uint32_t index = place & ~element_place;
        /* (one past the last element is a permitted result too) */
        bool one_past =
            v.extent && v.offset == v.extent && v.base != UINT32_MAX;
        if (index >= state.elements.size() && !one_past)
            return CONST_UNKNOWN;
        /* an element of a local constexpr array ([expr.const]/13.1: "the
           address of an object with ... automatic storage duration") */
        for (size_t i = 0; i < state.objects.size(); i++) {
            const constant_evaluation::object &o = state.objects[i];
            if (o.entity && entity_table[o.entity].automatic &&
                o.value.kind == constant_value_kind::array &&
                index >= o.value.first && index < o.value.first + o.value.count)
                return CONST_NOT_CONSTANT;
        }
        return constness::constant;
    }
    if (place >= state.objects.size() || state.objects[place].frame)
        return CONST_UNKNOWN;
    /* [expr.const]/13.? : storage a new-expression made has to be freed */
    if (state.objects[place].heap)
        return CONST_NOT_CONSTANT;
    /* a local constexpr variable: its address differs per invocation */
    if (state.objects[place].entity &&
        entity_table[state.objects[place].entity].automatic)
        return CONST_NOT_CONSTANT;
    return constness::constant;
}

/*
 * [expr.const]/13: a constant pointer as a tree: the address of an object
 * of static storage duration as the analysis writes it (`&x`, `a + i`, a
 * string literal's array decayed, null), which lowering emits as data.
 * 0: a pointer no such tree names (into a local, past the end, of another
 * type than the object's).
 */
uint32_t semantic_analysis::pointer_tree(const constant_evaluation &state,
                                         qualified_type t,
                                         const constant_value &v,
                                         token_index at)
{
    expression_meaning m;
    m.type = qualified_type::make(t.unqualified(), cv_none);
    m.category = value_category::prvalue;
    m.evaluation = constness::unknown;
    const struct type pointer = type_table[t.unqualified()];
    if (pointer.kind != type_kind::pointer)
        return 0;
    if (!v.count)
        return expression_node(semantic_kind::zero, m, at);
    if (v.extent && v.base == UINT32_MAX)
        return 0;
    qualified_type pointee = pointer.operand;
    auto entity_object = [&](entity_index e, qualified_type &type) -> uint32_t {
        const entity &x = entity_table[e];
        if (x.kind != entity_kind::variable || x.automatic ||
            x.nonstatic_member || (x.gnu_flags & gnu_weak) ||
            x.c11_thread_local)
            return 0;
        type = x.type;
        expression_meaning em;
        em.type = x.type;
        em.category = value_category::lvalue;
        em.entity = e;
        return expression_node(semantic_kind::entity, em, at, e);
    };
    uint32_t base_node = 0;
    qualified_type base_type;
    uint32_t index = 0;
    if (!(v.first & element_place)) {
        /* a single object: the variable itself */
        if (v.first >= state.objects.size() || v.extent)
            return 0;
        const constant_evaluation::object &o = state.objects[v.first];
        if (!o.entity || o.frame)
            return 0;
        base_node = entity_object(o.entity, base_type);
        if (!base_node || base_type.unqualified() != pointee.unqualified())
            return 0;
        expression_meaning am = m;
        return expression_node(semantic_kind::address, am, at, base_node);
    }
    /* an element of the array a variable is or a string literal makes */
    if (!v.extent)
        return 0;
    index = static_cast<uint32_t>(v.offset);
    for (size_t i = 0; i < state.objects.size(); i++) {
        const constant_evaluation::object &o = state.objects[i];
        if (o.frame || o.value.kind != constant_value_kind::array ||
            o.value.first != v.base || o.value.count != v.extent)
            continue;
        qualified_type array_type;
        if (o.literal) {
            token_index literal_at = 0;
            for (size_t j = 0; j < state.literals.size(); j++)
                if (state.literals[j].object == i)
                    literal_at = state.literals[j].at;
            if (!literal_at)
                return 0;
            array_type = qualified_type::make(
                type_table.array_of(pointee, v.extent), cv_none);
            expression_meaning lm;
            lm.type = array_type;
            lm.category = value_category::lvalue;
            base_node =
                expression_node(semantic_kind::string_literal, lm, literal_at);
        } else if (o.entity) {
            base_node = entity_object(o.entity, array_type);
            if (!base_node ||
                type_table[array_type.unqualified()].kind != type_kind::array ||
                type_table[array_type.unqualified()].operand.unqualified() !=
                    pointee.unqualified())
                return 0;
        } else {
            return 0;
        }
        expression_meaning dm = m;
        uint32_t decayed = expression_node(
            semantic_kind::conversion, dm, at, base_node, 0,
            static_cast<uint8_t>(conversion_kind::array_to_pointer));
        if (!index)
            return decayed;
        expression_meaning cm;
        cm.type = qualified_type::make(
            type_table.fundamental(fundamental_type::long_int), cv_none);
        cm.category = value_category::prvalue;
        cm.evaluation = constness::constant;
        cm.value = index;
        uint32_t offset = expression_node(semantic_kind::constant, cm, at);
        return expression_node(
            semantic_kind::binary, m, at, decayed, offset,
            static_cast<uint8_t>(preprocessing_op_or_punc::PLUS));
    }
    return 0;
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
    if (v.kind == constant_value_kind::pointer)
        return pointer_tree(state, t, v, at);
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
    if (record.kind == type_kind::complex && elements.size() == 2)
        return expression_node(semantic_kind::complex_value, m, at, elements[0],
                               elements[1]);
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
        return CONST_UNKNOWN;
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
        return CONST_UNKNOWN;
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
            return leave_scope(state, saved, saved_elements, returned, result,
                               c);
        return c;
    }
    case semantic_kind::expression_statement: {
        constant_value discarded;
        /* [class.temporary]/4: a discarded prvalue of a class type with a
           destructor is a temporary destroyed at once: not run yet */
        qualified_type destroyed;
        bool in_place =
            semantics[x.a].kind == semantic_kind::construct && semantics[x.a].a;
        size_t mark = state.destructions.size();
        state.temporaries_ok++;
        if (!in_place && semantics[x.a].category == value_category::prvalue &&
            element_destructor(semantics[x.a].type, destroyed)) {
            /* the temporary is made, then destroyed at the semicolon */
            c = evaluate_node(state, x.a, discarded);
            if (c == constness::constant && is_object_value(discarded)) {
                constant_evaluation::object o;
                o.frame = state.frame;
                o.value = discarded;
                append_object(state, o);
                add_destroyed(state, semantics[x.a].type);
            } else if (c == constness::constant) {
                c = CONST_UNKNOWN;
            }
        } else {
            c = evaluate_node(state, x.a, discarded);
        }
        state.temporaries_ok--;
        return end_full_expression(state, mark, c, result);
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
                return CONST_NOT_CONSTANT;
            return CONST_UNKNOWN;
        }
        /* [basic.scope.pdecl]/1: the variable is in scope in its own
           initializer */
        constant_evaluation::object o;
        o.entity = x.a;
        o.frame = state.frame;
        uint32_t index = static_cast<uint32_t>(state.objects.size());
        append_object(state, o);
        uint32_t init = x.b;
        if (state.seek)
            init = 0;
        size_t mark = state.destructions.size();
        state.temporaries_ok++;
        c = evaluate_initialization(state, e.type, init, false, index);
        state.temporaries_ok--;
        c = end_full_expression(state, mark, c, result);
        /* [stmt.dcl]/2: "Variables with automatic storage duration are
           destroyed when the block is left, in reverse order of their
           declaration" */
        qualified_type destroyed;
        if (c == constness::constant && !state.seek &&
            element_destructor(e.type, destroyed))
            add_destroyed(state, e.type);
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
        return leave_scope(state, saved, saved_elements, returned, result, c);
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
            c = leave_scope(state, iteration, iteration_elements, returned,
                            result, c);
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
        return leave_scope(state, saved, saved_elements, returned, result, c);
    }
    case semantic_kind::break_statement:
        state.jump = jump_break;
        return c;
    case semantic_kind::continue_statement:
        state.jump = jump_continue;
        return c;
    case semantic_kind::return_statement:
        result = constant_value();
        if (x.a) {
            size_t mark = state.destructions.size();
            state.temporaries_ok++;
            c = evaluate_node(state, x.a, result);
            state.temporaries_ok--;
            c = end_full_expression(state, mark, c, result);
        }
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
        return leave_scope(state, saved, saved_elements, returned, result, c);
    }
    /* [stmt.goto]: clang's evaluator has no goto: "subexpression not valid
       in a constant expression", whatever the language version; the check
       of a body for a potential constant expression does not count it */
    case semantic_kind::goto_statement:
        if (state.potential)
            return CONST_UNKNOWN;
        return CONST_NOT_CONSTANT;
    default:
        return CONST_UNKNOWN;
    }
}

constness semantic_analysis::potential_constant(uint32_t body)
{
    const semantic_node b = semantics[body];
    if (b.kind != semantic_kind::function_body || b.b || b.c)
        return CONST_UNKNOWN;
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
        append_object(state, o);
    }
    bool returned = false;
    constant_value result;
    return evaluate_statement(state, b.a, returned, result);
}

} // namespace cplusplus
