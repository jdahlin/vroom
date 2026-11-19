/*
 * cast-expression.cpp: [expr.cast] Explicit type conversion (cast notation).
 *
 *     cast-expression:
 *         unary-expression
 *         ( type-id ) cast-expression
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [expr.cast]/1: "The result of the expression (T) cast-expression is of
 * type T."  Whether a '(' begins the second alternative or a
 * unary-expression (a parenthesized expression, [expr.prim.paren]) is
 * [dcl.ambig.res]/2's rule, ambiguity-resolution.cpp.  The first alternative
 * passes its unary-expression on.
 */
node_index parser::cast_expression()
{
    /* as for assignment-expression: here for the casts and unary
       operators, which nest without one */
    nesting_level level(*this);
    if (!nesting_within_limit("an expression nested no deeper than this "
                              "implementation's limit"))
        return 0;
    if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return unary_expression();
    size_t mark = begin_node();
    token_index first = position;
    if (!parenthesized_type_id(true)) {
        if (failed())
            return 0;
        return unary_expression();
    }
    /* (parenthesized_type_id leaves a '{' only for
       [ext:gnu_compound_literals]) */
    if (at(preprocessing_op_or_punc::LEFT_BRACE))
        return gnu_compound_literal(mark, first);
    cast_expression();
    if (failed())
        return 0;
    return finish_node(syntax_kind::cast_expression, mark, first);
}

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [expr.cast]/4: "The conversions performed by
 *
 *   - a const_cast ([expr.const.cast]),
 *   - a static_cast ([expr.static.cast]),
 *   - a static_cast followed by a const_cast,
 *   - a reinterpret_cast ([expr.reinterpret.cast]), or
 *   - a reinterpret_cast followed by a const_cast,
 *
 * can be performed using the cast notation of explicit type conversion."
 * "If a conversion can be interpreted in more than one of the ways listed
 * above, the interpretation that appears first in the list is used".  One
 * function has the rules of all of them, kinds (cast_kinds) the casts it
 * may use: the cast notation's, or one named cast's.  Analysis has:
 *
 *   - to cv void, [expr.static.cast]/6;
 *   - to a class, [expr.static.cast]/4: "T t(E);", a direct-initialization
 *     (class_direct_initialization, as T(E));
 *   - from a class to another type, [expr.static.cast]/4 by a conversion
 *     function ([dcl.init]/17.7, conversion_function_call);
 *   - between arithmetic types, [expr.static.cast]/4 (an implicit
 *     conversion) and /10;
 *   - the other implicit conversions ([expr.static.cast]/4: "An expression
 *     E can be explicitly converted to a type T if there is an implicit
 *     conversion sequence from E to T"): a null pointer constant to a
 *     pointer, a pointer to bool, a pointer to a pointer by [conv.ptr]/2,
 *     [conv.qual] or [conv.fctptr] (a pointer to a noexcept function to
 *     a pointer to function);
 *   - between pointers otherwise, pointer_conversion;
 *   - to a reference, reference_cast;
 *   - a pointer to an integral type, [expr.reinterpret.cast]/4: "A pointer
 *     can be explicitly converted to any integral type large enough to
 *     hold all values of its type";
 *   - an integral type to a pointer, [expr.reinterpret.cast]/5: "A value
 *     of integral type or enumeration type can be explicitly converted to
 *     a pointer."
 *
 * No cast converts between a pointer and a floating type: ill-formed.
 * The address is the same in each of them, the integers' mapping being
 * the implementation's ([expr.reinterpret.cast]/4 "The mapping function is
 * implementation-defined"): the target's pointer's bits.
 */
static const char cast_rule[] =
    "the cast notation performs the conversions of a const_cast, a "
    "static_cast, or a reinterpret_cast, and those followed by a "
    "const_cast";

/* the rule a conversion none of the casts in kinds performs breaks: a
   named cast's "No other conversion" sentence ([expr.static.cast]/5,
   [expr.const.cast]/1, [expr.reinterpret.cast]/1) */
static const char *cast_rule_of(uint8_t kinds, const char *&label)
{
    switch (kinds) {
    case cast_static:
        label = "expr.static.cast";
        return "no other conversion shall be performed explicitly using a "
               "static_cast";
    case cast_const:
        label = "expr.const.cast";
        return "no other conversion shall be performed explicitly using "
               "const_cast";
    case cast_reinterpret:
        label = "expr.reinterpret.cast";
        return "no other conversion can be performed explicitly using "
               "reinterpret_cast";
    default:
        label = "expr.cast";
        return cast_rule;
    }
}

analysis_result semantic_analysis::no_cast(SyntaxRef at, uint8_t kinds)
{
    const char *label = nullptr;
    const char *rule = cast_rule_of(kinds, label);
    /* [impl.clang] Microsoft's mode has casts of its own (a static_cast
       between pointers to functions and to objects) */
    if (tokens.files.behavior.microsoft_extensions)
        return not_supported(at, label);
    return ill_formed(at, rule, label);
}

analysis_result semantic_analysis::explicit_conversion(SyntaxRef at,
                                                       qualified_type to,
                                                       expression_meaning &m,
                                                       uint8_t kinds)
{
    const struct type target = type_table[to.unqualified()];
    bool by_static = kinds & cast_static;
    if (target.kind == type_kind::lvalue_reference ||
        target.kind == type_kind::rvalue_reference)
        return reference_cast(at, to, m, kinds);
    if (target.kind == type_kind::fundamental &&
        target.fundamental == fundamental_type::void_) {
        if (!by_static)
            return no_cast(at, kinds);
        /* [expr.static.cast]/6: "Any expression can be explicitly converted
           to type cv void, in which case it becomes a discarded-value
           expression." */
        constness evaluation = m.evaluation;
        bool beyond = m.beyond_integral_constant;
        uint32_t operand = m.node;
        m = expression_meaning();
        m.type = qualified_type::make(to.unqualified(), cv_none);
        m.evaluation = evaluation;
        m.beyond_integral_constant = beyond;
        m.node = operand;
        converted(m, conversion_kind::to_void);
        return analysis_result::well_formed;
    }
    if (target.kind == type_kind::class_type) {
        if (!by_static)
            return no_cast(at, kinds);
        /* [expr.static.cast]/4: "if overload resolution for a
           direct-initialization of an object or reference of type T from
           E would find at least one viable function, T t(E); for some
           invented temporary variable t and then using the temporary
           variable as the result of the conversion" */
        owned_array<expression_meaning> arguments;
        arguments.append(m);
        return class_direct_initialization(at, target.entity, arguments, m);
    }
    if (type_table[m.type.unqualified()].kind == type_kind::class_type) {
        if (!by_static)
            return no_cast(at, kinds);
        if (target.kind != type_kind::fundamental &&
            target.kind != type_kind::pointer && !enumeration_of(to))
            return not_supported(at, "expr.static.cast");
        /* [expr.static.cast]/4 with [dcl.init]/17.7: "the source type is a
           (possibly cv-qualified) class type, conversion functions are
           considered", then the second standard conversion sequence */
        bool converted = false;
        analysis_result r =
            conversion_function_call(at, m, to, true, converted);
        if (r != analysis_result::well_formed)
            return r;
        if (!converted)
            return no_cast(at, kinds);
        r = operand_value(at, m);
        if (r != analysis_result::well_formed)
            return r;
        return implicit_conversion(at, m, to, true);
    }
    if (kinds == cast_reinterpret && m.type.unqualified() == to.unqualified() &&
        (is_integral(to) || enumeration_of(to) || is_pointer(to))) {
        /* [expr.reinterpret.cast]/2: "An expression of integral,
           enumeration, pointer, or pointer-to-member type can be explicitly
           converted to its own type; such a cast yields the value of its
           operand."  (A bit-field's value of its own type is not
           analysed.) */
        if (m.bit_field_width)
            return not_supported(at, "expr.reinterpret.cast");
        analysis_result r = operand_value(at, m);
        if (r == analysis_result::well_formed)
            m.type = qualified_type::make(to.unqualified(), cv_none);
        return r;
    }
    if (target.kind == type_kind::enumeration) {
        /* [expr.static.cast]/10: "A value of integral or enumeration type
           can be explicitly converted to a complete enumeration type. ...
           If the enumeration type does not have a fixed underlying type,
           the value is unchanged if the original value is within the
           range of the enumeration values ([dcl.enum]), and otherwise,
           the behavior is undefined.  A value of floating-point type can
           also be explicitly converted to an enumeration type.  The
           resulting value is the same as converting the original value to
           the underlying type of the enumeration ([conv.fpint]), and
           subsequently to the enumeration type." */
        entity_index e = enumeration_of(to);
        if (!e)
            return not_supported(at, "dcl.enum");
        analysis_result r = operand_value(at, m);
        if (r != analysis_result::well_formed)
            return r;
        if (m.type.unqualified() == to.unqualified())
            return analysis_result::well_formed;
        if (type_table[m.type.unqualified()].kind == type_kind::enumeration &&
            !enumeration_as_underlying(m))
            return not_supported(at, "dcl.enum");
        if (!is_arithmetic(m.type)) {
            if (fundamental_of(m.type) == fundamental_type::none &&
                !is_pointer(m.type))
                return not_supported(at, "expr.cast");
            return no_cast(at, kinds);
        }
        if (!by_static)
            return no_cast(at, kinds);
        /* [conv.fpint]/1: "The conversion truncates; that is, the
           fractional part is discarded.  The behavior is undefined if the
           truncated value cannot be represented in the destination type."
           bool is an integer type here, not [conv.bool]'s: 0.5 is false,
           2.5 has no value (clang 23; CWG1094 changes it later) */
        if (entity_table[e].underlying == fundamental_type::bool_ &&
            is_floating(m.type)) {
            convert_arithmetic(m, fundamental_type::long_long_int);
            if (m.evaluation == constness::constant && m.value > 1)
                m.evaluation = constness::not_constant;
            convert_arithmetic(m, fundamental_type::bool_);
            /* in the tree as [conv.integral]'s, not [conv.bool]'s: the
               evaluator gives a value other than 0 or 1 none */
            if (m.node)
                semantics[m.node].op =
                    static_cast<uint8_t>(conversion_kind::integral);
        }
        convert_arithmetic(m, entity_table[e].underlying);
        m.type = qualified_type::make(to.unqualified(), cv_none);
        converted(m, conversion_kind::integral);
        return analysis_result::well_formed;
    }
    if (target.kind == type_kind::pointer_to_member)
        return member_pointer_cast(at, to, m, kinds);
    if (target.kind != type_kind::fundamental &&
        target.kind != type_kind::pointer)
        return not_supported(at, "expr.cast");
    analysis_result r = operand_value(at, m);
    if (r != analysis_result::well_formed)
        return r;
    if (is_member_pointer(m.type)) {
        /* [conv.bool]/1: "A prvalue of ... pointer-to-member type can be
           converted to a prvalue of type bool" (a static_cast,
           [expr.static.cast]/4); [expr.reinterpret.cast]/10's casts are
           not analysed */
        if (target.kind == type_kind::fundamental &&
            target.fundamental == fundamental_type::bool_ && by_static)
            return implicit_conversion(at, m, to, true);
        return not_supported(at, "expr.cast");
    }
    /* [expr.static.cast]/9: "A value of a scoped enumeration type can be
       explicitly converted to an integral type"; and to a floating-point
       type; an unscoped one converts so implicitly */
    if (target.kind == type_kind::fundamental &&
        type_table[m.type.unqualified()].kind == type_kind::enumeration) {
        if (!by_static)
            return no_cast(at, kinds);
        if (!enumeration_as_underlying(m))
            return not_supported(at, "dcl.enum");
    }
    if (is_arithmetic(to) && is_arithmetic(m.type)) {
        if (!by_static)
            return no_cast(at, kinds);
        convert_arithmetic(m, target.fundamental);
        return analysis_result::well_formed;
    }
    fundamental_type from = fundamental_of(m.type);
    qualified_type result = qualified_type::make(to.unqualified(), cv_none);
    if (target.kind == type_kind::pointer) {
        if (m.zero_literal && by_static)
            return implicit_conversion(at, m, to, true);
        if (from == fundamental_type::std_nullptr_t) {
            /* [expr.reinterpret.cast]/9: "A null pointer constant of type
               std::nullptr_t cannot be converted to a pointer type" */
            if (!by_static)
                return no_cast(at, kinds);
            return implicit_conversion(at, m, to, true);
        }
        if (is_pointer(m.type))
            return pointer_conversion(at, to, m, kinds);
        if (is_integral(m.type)) {
            if (!(kinds & cast_reinterpret))
                return no_cast(at, kinds);
            /* [expr.const]/5: "a reinterpret_cast" is not a core constant
               expression */
            m.type = result;
            m.evaluation = constness::not_constant;
            converted(m, conversion_kind::integral_to_pointer);
            return analysis_result::well_formed;
        }
        if (from == fundamental_type::none)
            return not_supported(at, "expr.cast");
        return no_cast(at, kinds);
    }
    if (from == fundamental_type::std_nullptr_t &&
        target.fundamental == fundamental_type::bool_ && by_static)
        /* [conv.bool]/1: "For direct-initialization, a prvalue of type
           std::nullptr_t can be converted to a prvalue of type bool; the
           resulting value is false." */
        return implicit_conversion(at, m, to, true);
    if (from == fundamental_type::std_nullptr_t &&
        fundamental_type_is_integral(target.fundamental) &&
        (kinds & cast_reinterpret)) {
        /* [expr.reinterpret.cast]/4: "A value of type std::nullptr_t can be
           converted to an integral type; the conversion has the same
           meaning and validity as a conversion of (void*)0 to the integral
           type." */
        qualified_type void_pointer = qualified_type::make(
            type_table.pointer_to(qualified_type::make(
                type_table.fundamental(fundamental_type::void_), cv_none)),
            cv_none);
        r = implicit_conversion(at, m, void_pointer, true);
        if (r != analysis_result::well_formed)
            return r;
    }
    if (is_pointer(m.type)) {
        fundamental_type t = target.fundamental;
        if (t == fundamental_type::bool_ && by_static)
            return implicit_conversion(at, m, to, true);
        if (!fundamental_type_is_integral(t) || !(kinds & cast_reinterpret))
            return no_cast(at, kinds);
        /* (the targets' pointers have 64 bits) */
        if (fundamental_type_width(tokens.files.behavior, t) < 64)
            return ill_formed(at,
                              "a pointer can be explicitly converted to any "
                              "integral type large enough to hold all values "
                              "of its type",
                              "expr.reinterpret.cast");
        m.type = result;
        m.evaluation = constness::not_constant;
        converted(m, conversion_kind::pointer_to_integral);
        return analysis_result::well_formed;
    }
    /* std::nullptr_t, the operands of other types: the implicit
       conversions, the rest not analysed yet */
    if (from == fundamental_type::none)
        return not_supported(at, "expr.cast");
    if (target.fundamental == fundamental_type::std_nullptr_t && by_static)
        return implicit_conversion(at, m, to, true);
    return not_supported(at, "expr.cast");
}

/*
 * To a pointer to member: the implicit conversions ([conv.mem], a static_cast
 * by [expr.static.cast]/4), and [expr.static.cast]/12: "A prvalue of type
 * "pointer to member of D of type cv1 T" can be converted to a prvalue of
 * type "pointer to member of B of type cv2 T", where D is a complete class
 * type and B is a base class ([class.derived]) of D, if cv2 is the same
 * cv-qualification as, or greater cv-qualification than, cv1. ... If no
 * valid standard conversion from "pointer to member of B of type T" to
 * "pointer to member of D of type T" exists ([conv.mem]), the program is
 * ill-formed.  The null member pointer value ([conv.mem]) is converted to
 * the null member pointer value of the destination type."  The const_casts
 * and reinterpret_casts of pointers to members are not analysed.
 */
analysis_result semantic_analysis::member_pointer_cast(SyntaxRef at,
                                                       qualified_type to,
                                                       expression_meaning &m,
                                                       uint8_t kinds)
{
    analysis_result r = operand_value(at, m);
    if (r != analysis_result::well_formed)
        return r;
    if (!(kinds & cast_static))
        return not_supported(at, "expr.cast");
    const struct type source = type_table[m.type.unqualified()];
    const struct type target = type_table[to.unqualified()];
    if (source.kind != type_kind::pointer_to_member ||
        m.type.unqualified() == to.unqualified() ||
        qualification_convertible(m.type, to))
        return implicit_conversion(at, m, to, true);
    entity_index d = source.entity, b = target.entity;
    if (source.operand.unqualified() != target.operand.unqualified() ||
        (source.operand.cv() & ~target.operand.cv()) || d == b ||
        entity_table[d].kind != entity_kind::class_type ||
        entity_table[b].kind != entity_kind::class_type)
        return not_supported(at, "expr.static.cast");
    if (entity_table[d].size && base_subobjects(d, b) == 0 &&
        entity_table[b].size && base_subobjects(b, d)) {
        /* [conv.mem]/2; [expr.cast]/4: "a pointer to member of an
           unambiguous non-virtual base class type may be explicitly
           converted to ... a pointer to member of a derived class type"
           even if the base class is inaccessible */
        if (kinds == cast_static)
            return implicit_conversion(at, m, to, true);
        if (base_subobjects(b, d) != 1 || !nonvirtual_subobjects(b, d))
            return ill_formed(at,
                              "if B is an ambiguous or virtual base class of "
                              "D, or a base class of a virtual base class of "
                              "D, a program that necessitates this "
                              "conversion is ill-formed",
                              "conv.mem");
        r = member_pointer_along_path(
            at.first_token(), b, d,
            qualified_type::make(
                type_table.pointer_to_member(b, source.operand), cv_none),
            false, false, m);
        if (r == analysis_result::well_formed)
            member_pointer_qualification(m, to);
        return r;
    }
    if (!entity_table[d].size || !base_subobjects(d, b))
        return not_supported(at, "expr.static.cast");
    if (base_subobjects(d, b) != 1 || !nonvirtual_subobjects(d, b))
        return ill_formed(at,
                          "if no valid standard conversion from \"pointer to "
                          "member of B of type T\" to \"pointer to member of "
                          "D of type T\" exists, the program is ill-formed",
                          "expr.static.cast");
    /* [expr.cast]/4: "in performing a static_cast in the following
       situations the conversion is valid even if the base class is
       inaccessible" */
    return member_pointer_along_path(at.first_token(), d, b, to, true,
                                     kinds == cast_static, m);
}

/*
 * A pointer to a pointer by the casts in kinds ([expr.cast]/4's list, in
 * order):
 *
 *   - the static_casts that are implicit conversions: the same type,
 *     [conv.qual], [conv.ptr]/2's "pointer to cv void";
 *   - a const_cast, [expr.const.cast]/3: "For two similar types T1 and T2
 *     ([conv.qual]), a prvalue of type T1 may be explicitly converted to
 *     the type T2 using a const_cast";
 *   - between pointers to classes one derived from the other,
 *     class_pointer_cast;
 *   - [expr.static.cast]/13: "A prvalue of type "pointer to cv1 void" can
 *     be converted to a prvalue of type "pointer to cv2 T", where T is an
 *     object type and cv2 is the same cv-qualification as, or greater
 *     cv-qualification than, cv1", then a const_cast if cv2 is less;
 *   - a reinterpret_cast, then a const_cast: [expr.reinterpret.cast]/7
 *     "An object pointer can be explicitly converted to an object pointer
 *     of a different type", /6 "A function pointer can be explicitly
 *     converted to a function pointer of a different type", /8
 *     "Converting a function pointer to an object pointer type or vice
 *     versa is conditionally-supported" ([impl.clang]: supported, the
 *     same address).
 *
 * [expr.static.cast]/1: "The static_cast operator shall not cast away
 * constness", nor a reinterpret_cast ([expr.reinterpret.cast]/2): of a
 * pointer to a pointer, [expr.const.cast]/7, a pointee's cv-qualifier the
 * target's lacks; analysis has it for pointers to what is no pointer.
 * The last two are not core constant expressions ([expr.const]/5: "a
 * conversion from type cv void* to a pointer-to-object type", "a
 * reinterpret_cast").
 */
analysis_result semantic_analysis::pointer_conversion(SyntaxRef at,
                                                      qualified_type to,
                                                      expression_meaning &m,
                                                      uint8_t kinds)
{
    qualified_type result = qualified_type::make(to.unqualified(), cv_none);
    qualified_type pointee = type_table[m.type.unqualified()].operand;
    qualified_type target_pointee = type_table[to.unqualified()].operand;
    const struct type fp = type_table[pointee.unqualified()];
    const struct type tp = type_table[target_pointee.unqualified()];
    bool by_static = kinds & cast_static;
    bool by_const = kinds & cast_const;
    bool to_void = tp.kind == type_kind::fundamental &&
                   tp.fundamental == fundamental_type::void_;
    bool from_void = fp.kind == type_kind::fundamental &&
                     fp.fundamental == fundamental_type::void_;
    /* [basic.type.qualifier]/3: "An array type whose elements are
       cv-qualified is also considered to have the same cv-qualifications
       as its elements." */
    auto qualifiers = [&](qualified_type t) {
        uint32_t cv = t.cv();
        while (type_table[t.unqualified()].kind == type_kind::array) {
            t = type_table[t.unqualified()].operand;
            cv |= t.cv();
        }
        return cv;
    };
    bool lesser = qualifiers(pointee) & ~qualifiers(target_pointee);
    if (by_static && (m.type.unqualified() == to.unqualified() ||
                      qualification_convertible(m.type, to) ||
                      function_pointer_convertible(m.type, to) ||
                      (to_void && is_object_pointer(m.type) && !lesser)))
        return implicit_conversion(at, m, to, true);
    /* [expr.const.cast]/9: "conversions between pointers to functions
       are not covered" */
    if (by_const && tp.kind != type_kind::function && similar(m.type, to)) {
        if (m.type.unqualified() == to.unqualified())
            return analysis_result::well_formed;
        m.type = result;
        converted(m, conversion_kind::qualification_cast);
        return analysis_result::well_formed;
    }
    bool handled = false;
    analysis_result r = class_pointer_cast(at, to, m, kinds, handled);
    if (handled || r != analysis_result::well_formed)
        return r;
    if (by_static && by_const && to_void && is_object_pointer(m.type)) {
        /* [expr.cast]/4: "a static_cast followed by a const_cast": to
           "pointer to cv void" with the pointee's cv-qualifiers
           ([conv.ptr]/2), then the qualification cast */
        qualified_type via = qualified_type::make(
            type_table.pointer_to(qualified_type::make(
                target_pointee.unqualified(), pointee.cv())),
            cv_none);
        r = implicit_conversion(at, m, via, true);
        if (r != analysis_result::well_formed)
            return r;
        m.type = result;
        converted(m, conversion_kind::qualification_cast);
        return analysis_result::well_formed;
    }
    bool to_object = tp.kind != type_kind::function && !to_void;
    bool by_reinterpret = kinds & cast_reinterpret;
    if (by_static && from_void && to_object && (!lesser || by_const))
        by_reinterpret = true;
    if (!by_reinterpret)
        return no_cast(at, kinds);
    if (lesser && !by_const)
        return ill_formed(at,
                          "the reinterpret_cast operator shall not cast away "
                          "constness",
                          "expr.reinterpret.cast");
    if (!lesser && !by_const &&
        (fp.kind == type_kind::pointer || tp.kind == type_kind::pointer))
        /* [expr.const.cast]/7 at the levels below the first */
        return not_supported(at, "expr.const.cast");
    m.type = result;
    /* [expr.const]/5: "a reinterpret_cast", "a conversion from type cv
       void* to a pointer-to-object type" (which a later revision allows:
       not claimed) */
    if (from_void && by_static) {
        if (m.evaluation == constness::constant)
            m.evaluation = constness::unknown;
    } else {
        m.evaluation = constness::not_constant;
    }
    converted(m, conversion_kind::pointer_cast);
    return analysis_result::well_formed;
}

/*
 * Between pointers to classes, one a base of the other, the static_casts:
 * [conv.ptr]/3 (an implicit conversion, [expr.static.cast]/4) to a base,
 * [expr.static.cast]/11 to a derived class; each then a const_cast if the
 * target's pointee is less cv-qualified.  [expr.cast]/4: "in performing a
 * static_cast in the following situations the conversion is valid even if
 * the base class is inaccessible": the cast notation's here only on one
 * path of non-virtual bases.  handled is false for the other pointers.
 */
analysis_result semantic_analysis::class_pointer_cast(SyntaxRef at,
                                                      qualified_type to,
                                                      expression_meaning &m,
                                                      uint8_t kinds,
                                                      bool &handled)
{
    handled = false;
    qualified_type pointee = type_table[m.type.unqualified()].operand;
    qualified_type target_pointee = type_table[to.unqualified()].operand;
    const struct type fp = type_table[pointee.unqualified()];
    const struct type tp = type_table[target_pointee.unqualified()];
    if (fp.kind != type_kind::class_type || tp.kind != type_kind::class_type ||
        fp.entity == tp.entity || !(kinds & cast_static))
        return analysis_result::well_formed;
    /* [expr.cast]/5: "If both the operand and destination types are class
       types and one or both are incomplete, it is unspecified whether the
       static_cast or the reinterpret_cast interpretation is used"
       ([impl.clang]: the reinterpret_cast's); a static_cast alone needs
       complete classes ([expr.static.cast]/11, [conv.ptr]/3) */
    if (!entity_table[fp.entity].size || !entity_table[tp.entity].size)
        return analysis_result::well_formed;
    bool ambiguous = false;
    bool upward = base_step(fp.entity, tp.entity, ambiguous) != 0;
    bool to_derived =
        !upward && base_step(tp.entity, fp.entity, ambiguous) != 0;
    if (!upward && !to_derived)
        return analysis_result::well_formed;
    handled = true;
    entity_index derived = fp.entity;
    entity_index base = tp.entity;
    if (to_derived) {
        derived = tp.entity;
        base = fp.entity;
    }
    if (kinds != cast_static && !single_nonvirtual_path(derived, base))
        return not_supported(at, "expr.cast");
    bool lesser = pointee.cv() & ~target_pointee.cv();
    if (lesser && !(kinds & cast_const))
        return ill_formed(at,
                          "the static_cast operator shall not cast away "
                          "constness",
                          "expr.static.cast");
    /* the static_cast keeps the operand's cv-qualifiers where the target
       has fewer, the const_cast then drops them */
    qualified_type via = qualified_type::make(
        type_table.pointer_to(qualified_type::make(
            target_pointee.unqualified(), target_pointee.cv() | pointee.cv())),
        cv_none);
    token_index t = at.first_token();
    analysis_result r = analysis_result::well_formed;
    if (kinds != cast_static)
        /* [expr.cast]/4: "in performing a static_cast in the following
           situations the conversion is valid even if the base class is
           inaccessible" */
        r = pointer_along_path(t, derived, base, via, to_derived, false, m);
    else if (upward)
        r = pointer_to_base(t, fp.entity, tp.entity, via, m);
    else
        r = pointer_to_derived(t, fp.entity, tp.entity, via, m);
    if (r != analysis_result::well_formed || !lesser)
        return r;
    m.type = qualified_type::make(to.unqualified(), cv_none);
    converted(m, conversion_kind::qualification_cast);
    return analysis_result::well_formed;
}

/*
 * A cast to a reference ([expr.cast]/1: "The result is an lvalue if T is an
 * lvalue reference type or an rvalue reference to function type and an
 * xvalue if T is an rvalue reference to object type"):
 *
 *   - [expr.static.cast]/3: "A glvalue of type cv1 T1 can be cast to type
 *     rvalue reference to cv2 T2 if cv2 T2 is reference-compatible with
 *     cv1 T1"; an lvalue to an lvalue reference to the same type
 *     likewise ([expr.static.cast]/4, a reference binding);
 *   - [expr.const.cast]/4: "an lvalue of type T1 can be explicitly
 *     converted to an lvalue of type T2 using the cast const_cast<T2&>" if
 *     "a pointer to T1 can be explicitly converted to the type "pointer to
 *     T2" using a const_cast";
 *   - [expr.static.cast]/2: "An lvalue of type "cv1 B", where B is a class
 *     type, can be cast to type "reference to cv2 D", where D is a class
 *     derived from B", and to a base class by the reference binding of
 *     /4: as the pointers;
 *   - [expr.reinterpret.cast]/11: "The result is that of
 *     *reinterpret_cast<T2 *>(p) where p is a pointer to x of type
 *     "pointer to T1"."
 *
 * Each of the last three an lvalue's address converted as a pointer, then
 * the object it points to; a temporary bound to the reference
 * ([expr.static.cast]/4) the same, the temporary's address.  A temporary
 * of a class, a prvalue operand otherwise, an xvalue of another type and
 * a function are not analysed.
 */
analysis_result semantic_analysis::reference_cast(SyntaxRef at,
                                                  qualified_type to,
                                                  expression_meaning &m,
                                                  uint8_t kinds)
{
    const struct type target = type_table[to.unqualified()];
    qualified_type referenced = target.operand;
    bool lvalue_reference = target.kind == type_kind::lvalue_reference;
    value_category category = value_category::xvalue;
    if (lvalue_reference)
        category = value_category::lvalue;
    if (type_table[referenced.unqualified()].kind == type_kind::function ||
        m.bit_field_width)
        return not_supported(at, "expr.cast");
    bool same = referenced.unqualified() == m.type.unqualified();
    bool scalars =
        type_table[referenced.unqualified()].kind != type_kind::class_type &&
        type_table[m.type.unqualified()].kind != type_kind::class_type;
    bool may_bind_temporary = !lvalue_reference || referenced.cv() == cv_const;
    /* [expr.static.cast]/4: "T t(E); for some invented temporary variable
       t and then using the temporary variable as the result of the
       conversion": a reference to const or an rvalue reference, neither to
       a class, bound by [dcl.init.ref]/5 (reference_binding) to a prvalue
       or to a glvalue of another type, the temporary its prvalue
       materializes ([conv.rval]) */
    bool temporary = (kinds & cast_static) && scalars && may_bind_temporary &&
                     (m.category == value_category::prvalue || !same);
    if (temporary) {
        analysis_result r = reference_binding(at, m, to);
        if (r != analysis_result::well_formed)
            return r;
    } else {
        if (m.category == value_category::prvalue)
            return not_supported(at, "expr.cast");
        if (lvalue_reference && m.category != value_category::lvalue)
            return not_supported(at, "expr.cast");
        if (same && m.type.cv() == referenced.cv() && m.category == category &&
            (kinds & (cast_static | cast_const)))
            /* the glvalue itself */
            return analysis_result::well_formed;
        if (m.category != value_category::lvalue)
            return not_supported(at, "expr.cast");
        const struct type &from = type_table[m.type.unqualified()];
        const struct type &into = type_table[referenced.unqualified()];
        bool related = false;
        if (from.kind == type_kind::class_type &&
            into.kind == type_kind::class_type && !same) {
            bool ambiguous = false;
            related = base_step(from.entity, into.entity, ambiguous) ||
                      base_step(into.entity, from.entity, ambiguous);
        }
        if (!related && !same && (kinds & cast_static) &&
            (kinds == cast_static || may_bind_temporary))
            /* [expr.static.cast]/4: "T t(E);" may bind the reference to a
               temporary of the converted value */
            return not_supported(at, "expr.static.cast");
    }
    token_index t = at.first_token();
    expression_meaning address;
    address.type = qualified_type::make(type_table.pointer_to(m.type), cv_none);
    address.category = value_category::prvalue;
    address.evaluation = constness::unknown;
    if (m.evaluation == constness::not_constant)
        address.evaluation = constness::not_constant;
    qualified_type pointer =
        qualified_type::make(type_table.pointer_to(referenced), cv_none);
    if (temporary)
        address.type = pointer;
    address.node = expression_node(semantic_kind::address, address, t, m.node);
    if (!temporary) {
        analysis_result r = pointer_conversion(at, pointer, address, kinds);
        if (r != analysis_result::well_formed)
            return r;
    }
    m = expression_meaning();
    m.type = referenced;
    m.category = category;
    m.evaluation = address.evaluation;
    m.node = expression_node(semantic_kind::indirection, m, t, address.node);
    return analysis_result::well_formed;
}

/*
 * [expr.cast]/1: "The result of the expression (T) cast-expression is of
 * type T.  The result is an lvalue if T is an lvalue reference type or an
 * rvalue reference to function type and an xvalue if T is an rvalue
 * reference to object type; otherwise the result is a prvalue."
 */
analysis_result semantic_analysis::cast(SyntaxRef n, expression_meaning &m)
{
    if (n.size() != 4 || !n[1].is(syntax_kind::type_id))
        return not_supported(n, "expr.cast");
    qualified_type to;
    analysis_result r = type_id(n[1], to);
    /* [over.over]/1: "an explicit type conversion" */
    if (r == analysis_result::well_formed)
        r = target_expression(n[3], to, m);
    if (r != analysis_result::well_formed)
        return r;
    /* C++03 [expr.const]/1: "Floating literals can appear only if they are
       cast to integral or enumeration types." */
    bool floating_literal_to_integral =
        n[3].is_token() &&
        tokens[n[3].token()].literal == literal_kind::floating_point_literal &&
        is_integral(to);
    r = explicit_conversion(n, to, m);
    if (r == analysis_result::well_formed && floating_literal_to_integral)
        m.beyond_integral_constant = false;
    return r;
}

} // namespace cplusplus
