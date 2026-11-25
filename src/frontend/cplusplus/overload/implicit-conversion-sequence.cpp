/*
 * implicit-conversion-sequence.cpp: [over.best.ics] Implicit conversion
 * sequences, with [over.ics.scs] Standard conversion sequences, [over.ics.user]
 * User-defined conversion sequences and [over.ics.ref] Reference binding.
 *
 * [over.best.ics]/1: "An implicit conversion sequence is a sequence of
 * conversions used to convert an argument in a function call to the type of
 * the corresponding parameter of the function being called.  The sequence of
 * conversions is an implicit conversion as defined in [conv], which means it
 * is governed by the rules for initialization of an object or reference by a
 * single expression ([dcl.init], [dcl.init.ref])."  /3: "The implicit
 * conversion sequence is the one required to convert the argument expression
 * to the type of the parameter."  /9: "If no conversions are required to
 * match an argument to a parameter type, the implicit conversion sequence is
 * the standard conversion sequence consisting of the identity conversion".
 *
 * These only classify: the conversion of the chosen function's arguments is
 * initialize_with's, as for one candidate.  A conversion function makes
 * a sequence to a type that is no class (conversion-function.cpp); to a class
 * or a reference to one it is not analysed (form::unknown), nor is a
 * class analysis did not follow.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

using sequence = semantic_analysis::conversion_sequence;

sequence of_form(sequence::form kind)
{
    sequence s;
    s.kind = kind;
    return s;
}

bool arithmetic_type(fundamental_type t)
{
    return fundamental_type_is_integral(t) || fundamental_type_is_floating(t);
}

} // namespace

/*
 * [over.ics.scs]/1: "Table 17 summarizes the conversions defined in [conv]
 * and partitions them into four disjoint categories: Lvalue Transformation,
 * Qualification Adjustment, Promotion, and Conversion."  /3: "Each
 * conversion in Table 17 also has an associated rank (Exact Match,
 * Promotion, or Conversion). ... If that sequence contains a Conversion, the
 * sequence has Conversion rank; otherwise, if it contains a Promotion, the
 * sequence has Promotion rank; otherwise, the sequence has Exact Match
 * rank."
 *
 * The argument a, of a type that is no class, converted to to, which is
 * neither a class nor a reference.
 */
semantic_analysis::conversion_sequence
semantic_analysis::standard_conversion_sequence(const expression_meaning &a,
                                                qualified_type to)
{
    sequence s = of_form(sequence::form::standard);
    to = qualified_type::make(to.unqualified(), cv_none);
    s.target = to;
    /* the Lvalue Transformations ([conv.array], [conv.func]); [conv.lval]
       drops the cv-qualifiers of a non-class type */
    qualified_type from = qualified_type::make(a.type.unqualified(), cv_none);
    const struct type source = type_table[from.unqualified()];
    if (source.kind == type_kind::array)
        from = qualified_type::make(type_table.pointer_to(source.operand),
                                    cv_none);
    else if (source.kind == type_kind::function)
        from = qualified_type::make(type_table.pointer_to(a.type), cv_none);
    const struct type f = type_table[from.unqualified()];
    const struct type t = type_table[to.unqualified()];
    if (f.kind == type_kind::none || t.kind == type_kind::none)
        return of_form(sequence::form::unknown);
    if (from.unqualified() == to.unqualified())
        return s;
    s.identity = false;
    s.rank = 2;
    fundamental_type ff = fundamental_of(from), tf = fundamental_of(to);
    if (f.kind == type_kind::enumeration) {
        /* [conv.prom]/4: "A prvalue of an unscoped enumeration type whose
           underlying type is not fixed can be converted to a prvalue of the
           first of the following types that can represent all the values of
           the enumeration ... A prvalue of an unscoped enumeration type
           whose underlying type is fixed can be converted to a prvalue of
           its underlying type.  Moreover, if integral promotion can be
           applied to its underlying type, a prvalue of an unscoped
           enumeration type whose underlying type is fixed can also be
           converted to a prvalue of the promoted underlying type."
           [conv.integral]/1, [conv.fpint]/2, [conv.bool]: the rest are
           Conversions; a scoped enumeration converts implicitly to nothing
           ([dcl.enum]/10) */
        const entity &e = entity_table[f.entity];
        if (e.underlying == fundamental_type::none)
            return of_form(sequence::form::unknown);
        if (e.scoped || !arithmetic_type(tf))
            return of_form(sequence::form::bad);
        if (e.fixed_underlying && tf == e.underlying) {
            s.rank = 1;
            s.to_underlying = true;
        } else if (e.fixed_underlying && tf == promotion_of(e.underlying)) {
            s.rank = 1;
        } else if (!e.fixed_underlying && tf == e.promoted) {
            s.rank = 1;
        }
        return s;
    }
    /* no standard conversion makes an enumeration */
    if (t.kind == type_kind::enumeration)
        return of_form(sequence::form::bad);
    if (arithmetic_type(ff) && arithmetic_type(tf)) {
        /* [conv.prom]: the integral promotions; [conv.fpprom]/1: "A prvalue
           of type float can be converted to a prvalue of type double."
           Every other arithmetic conversion is a Conversion
           ([conv.integral], [conv.double], [conv.fpint], [conv.bool]) */
        if (fundamental_type_is_integral(ff) && integral_promotion(a) == tf)
            s.rank = 1;
        if (ff == fundamental_type::float_ && tf == fundamental_type::double_)
            s.rank = 1;
        return s;
    }
    if (tf == fundamental_type::bool_) {
        /* [conv.bool]/1: "A prvalue of arithmetic, unscoped enumeration,
           pointer, or pointer-to-member type can be converted to a prvalue
           of type bool."  std::nullptr_t converts to bool only in a
           direct-initialization ([dcl.init]/17.8), and a parameter is
           copy-initialized */
        if (f.kind == type_kind::pointer ||
            f.kind == type_kind::pointer_to_member) {
            s.pointer_to_bool = true;
            return s;
        }
        return of_form(sequence::form::bad);
    }
    bool null_pointer_constant =
        a.zero_literal || ff == fundamental_type::std_nullptr_t;
    /* an integral constant expression of value zero that is no literal:
       C++03's null pointer constant ([diff.cpp03.expr]), which
       implicit_conversion does not analyse */
    bool maybe_null =
        fundamental_type_is_integral(ff) &&
        !tokens.files.behavior.has(language_feature::list_initialization) &&
        a.evaluation != constness::not_constant &&
        (a.evaluation != constness::constant || a.value == 0);
    if (tf == fundamental_type::std_nullptr_t) {
        /* [conv.ptr]/1: "A null pointer constant of integral type can be
           converted to a prvalue of type std::nullptr_t." */
        if (null_pointer_constant)
            return s;
        if (maybe_null)
            return of_form(sequence::form::unknown);
        return of_form(sequence::form::bad);
    }
    if (t.kind == type_kind::pointer_to_member) {
        /* [conv.mem]/1: "A null pointer constant ([conv.ptr]) can be
           converted to a pointer-to-member type"; /2, a pointer to member
           of a base, is not ranked here ([over.ics.rank]/4.4.3) */
        if (null_pointer_constant)
            return s;
        if (f.kind != type_kind::pointer_to_member) {
            if (maybe_null || f.kind == type_kind::class_type)
                return of_form(sequence::form::unknown);
            return of_form(sequence::form::bad);
        }
        /* [conv.qual]/3: Qualification Adjustment, of Exact Match rank */
        if (qualification_convertible(from, to)) {
            s.rank = 0;
            return s;
        }
        if (f.operand.unqualified() != t.operand.unqualified())
            return of_form(sequence::form::bad);
        return of_form(sequence::form::unknown);
    }
    if (t.kind != type_kind::pointer) {
        if (t.kind == type_kind::fundamental && f.kind != type_kind::class_type)
            return of_form(sequence::form::bad);
        return of_form(sequence::form::unknown);
    }
    /* [conv.ptr]/1: "A null pointer constant can be converted to a pointer
       type; the result is the null pointer value of that type" */
    if (null_pointer_constant)
        return s;
    if (f.kind != type_kind::pointer) {
        if (maybe_null)
            return of_form(sequence::form::unknown);
        if (f.kind == type_kind::fundamental)
            return of_form(sequence::form::bad);
        return of_form(sequence::form::unknown);
    }
    /* [conv.qual]/3: a qualification conversion: Qualification Adjustment,
       of Exact Match rank */
    if (qualification_convertible(from, to)) {
        s.rank = 0;
        return s;
    }
    /* [conv.fctptr]: a function pointer conversion, of Exact Match rank
       ([over.ics.scs] Table 19) */
    if (function_pointer_convertible(from, to)) {
        s.rank = 0;
        s.identity = false;
        return s;
    }
    qualified_type pointee = f.operand, target_pointee = t.operand;
    const struct type fp = type_table[pointee.unqualified()];
    const struct type tp = type_table[target_pointee.unqualified()];
    /* The supported function pointer conversion was handled above.
       [conv.fctptr]/1 has no conversion in the reverse direction. */
    if (fp.kind == type_kind::function && tp.kind == type_kind::function)
        return of_form(sequence::form::bad);
    bool cv_kept = !(pointee.cv() & ~target_pointee.cv());
    /* [conv.ptr]/2: "A prvalue of type "pointer to cv T", where T is an
       object type, can be converted to a prvalue of type "pointer to cv
       void"" (then a qualification conversion) */
    if (tp.kind == type_kind::fundamental &&
        tp.fundamental == fundamental_type::void_ && is_object_pointer(from)) {
        if (!cv_kept)
            return of_form(sequence::form::bad);
        s.to_void_pointer = true;
        if (fp.kind == type_kind::class_type)
            s.base_from = fp.entity;
        return s;
    }
    /* [conv.ptr]/3: "A prvalue of type "pointer to cv D", where D is a
       complete class type, can be converted to a prvalue of type "pointer
       to cv B", where B is a base class of D." */
    if (fp.kind == type_kind::class_type && tp.kind == type_kind::class_type) {
        if (entity_table[fp.entity].kind != entity_kind::class_type ||
            entity_table[tp.entity].kind != entity_kind::class_type)
            return of_form(sequence::form::unknown);
        bool ambiguous = false;
        require_complete(fp.entity);
        if (!entity_table[fp.entity].size || !cv_kept ||
            !base_step(fp.entity, tp.entity, ambiguous))
            return of_form(sequence::form::bad);
        if (ambiguous)
            return of_form(sequence::form::unknown);
        s.base_from = fp.entity;
        s.base_to = tp.entity;
        return s;
    }
    /* [diff.lex]/3: a string literal to char*, wchar_t* and so on (the
       target's pointee the element type, not const): C++03's deprecated
       conversion, clang's extension since C++11 (-Wwritable-strings).
       [impl.clang] since C++11 a function that needs it is worse than any
       viable function that does not (even one called through an ellipsis
       or by a user-defined conversion), whatever the other arguments
       (better in overload-resolution.cpp); it is of Conversion rank
       between functions that all need it.  C++03 ranks it by other rules
       (clang's Writing Conversion): not analysed.  The conversion itself
       is standard_conversion's (standard-conversions.cpp). */
    if (a.string_literal &&
        pointee.unqualified() == target_pointee.unqualified() &&
        !target_pointee.cv()) {
        if (!tokens.files.behavior.has(language_feature::list_initialization))
            return of_form(sequence::form::unknown);
        s.string_literal_to_char_pointer = true;
        return s;
    }
    /* [conv.fctptr]: a function pointer conversion */
    if (fp.kind == type_kind::function || tp.kind == type_kind::function)
        return of_form(sequence::form::unknown);
    if (fp.kind == type_kind::none || tp.kind == type_kind::none)
        return of_form(sequence::form::unknown);
    return of_form(sequence::form::bad);
}

/*
 * [over.best.ics]/6: "When the parameter has a class type and the argument
 * expression has the same type, the implicit conversion sequence is an
 * identity conversion.  When the parameter has a class type and the
 * argument expression has a derived class type, the implicit conversion
 * sequence is a derived-to-base conversion from the derived class to the
 * base class. ... A derived-to-base conversion has Conversion rank".
 * Otherwise [over.ics.user]/1: "A user-defined conversion sequence consists
 * of an initial standard conversion sequence followed by a user-defined
 * conversion ([class.conv]) followed by a second standard conversion
 * sequence.  If the user-defined conversion is specified by a constructor
 * ([class.conv.ctor]), the initial standard conversion sequence converts
 * the source type to the type of the first parameter of that constructor."
 * The constructors are those of [over.match.copy]/1.1, "the converting
 * constructors of T"; [over.best.ics]/4: in that overload resolution, of
 * a constructor's first parameter, "user-defined conversion sequences are
 * not considered".  [over.best.ics]/10: "If several different sequences of
 * conversions exist that each convert the argument to the parameter type,
 * the implicit conversion sequence associated with the parameter is
 * defined to be the unique conversion sequence designated the ambiguous
 * conversion sequence."
 */
/*
 * [over.match.copy]/1.2: "When the type of the initializer expression is a
 * class type "cv S", the non-explicit conversion functions of S and its
 * base classes are considered.  When initializing a temporary object to be
 * bound to the first parameter of a constructor ... Those that are not
 * hidden within S and yield a type whose cv-unqualified version is the same
 * type as T or is a derived class thereof are candidate functions."
 * Whether S has one that may be (a class or function template of
 * conversion functions it does not follow is one); the explicit ones
 * count when asked: the first parameter of a copy or move constructor in
 * a direct-initialization takes them too ([over.match.copy]/1.2's last
 * sentence), which the sequence of an argument is not told apart from.
 */
bool semantic_analysis::conversion_functions_yield_class(
    entity_index s, entity_index t, bool explicit_counts) const
{
    if (entity_table[s].kind != entity_kind::class_type ||
        base_has_conversion_functions(s))
        return true;
    for (entity_index f = own_conversion_functions(s); f;
         f = entity_table[f].next_overload) {
        if (!entity_table[f].conversion_function)
            return true;
        if (entity_table[f].explicit_ && !explicit_counts)
            continue;
        const struct type &r =
            type_table[conversion_function_result(f).type.unqualified()];
        if (r.kind != type_kind::class_type)
            continue;
        bool ambiguous = false;
        if (entity_table[r.entity].kind != entity_kind::class_type ||
            r.entity == t || base_step(r.entity, t, ambiguous) || ambiguous)
            return true;
    }
    return false;
}

/* whether the class has a conversion function that is no explicit one
   (or conversion functions this does not follow): one a copy-initialization
   may use ([class.conv.fct]/2) */
bool semantic_analysis::has_implicit_conversion_functions(entity_index c) const
{
    if (entity_table[c].kind != entity_kind::class_type ||
        base_has_conversion_functions(c))
        return true;
    for (entity_index f = own_conversion_functions(c); f;
         f = entity_table[f].next_overload)
        if (!entity_table[f].conversion_function || !entity_table[f].explicit_)
            return true;
    return false;
}

/*
 * [over.match.copy]/1.2: "When the type of the initializer expression is a
 * class type "cv S", the non-explicit conversion functions of S and its
 * base classes are considered.  ...  Those that are not hidden within S and
 * yield a type whose cv-unqualified version is the same type as T or is a
 * derived class thereof are candidate functions."  The one conversion
 * function that does, of an argument of class a's type to the class type
 * `to`, as the user-defined conversion sequence [over.ics.user]/1: its
 * second standard conversion sequence the result's (identity, or
 * derived-to-base) to T.  False without a candidate; with unknown set when
 * the analysis cannot tell (a conversion function template, an explicit
 * one ([over.match.copy]/1.2's last sentence: the first parameter of a copy
 * or move constructor in a direct-initialization), a ref-qualified one,
 * two candidates, which [over.match.best] would rank).
 */
bool semantic_analysis::conversion_function_to_class(
    const expression_meaning &a, qualified_type to, sequence &found,
    bool &unknown)
{
    unknown = false;
    found = of_form(sequence::form::bad);
    entity_index c1 = type_table[to.unqualified()].entity;
    owned_array<entity_index> candidates;
    owned_array<entity_index> conv_templates;
    if (!visible_conversion_functions(type_table[a.type.unqualified()].entity,
                                      candidates, &conv_templates) ||
        !deduce_conversion_templates(conv_templates, to, candidates)) {
        unknown = true;
        return false;
    }
    entity_index chosen = 0;
    for (entity_index f : candidates) {
        expression_meaning value = conversion_function_result(f);
        const struct type &r = type_table[value.type.unqualified()];
        if (r.kind != type_kind::class_type)
            continue;
        bool ambiguous = false;
        if (entity_table[r.entity].kind != entity_kind::class_type) {
            unknown = true;
            return false;
        }
        if (r.entity != c1 && !base_step(r.entity, c1, ambiguous) && !ambiguous)
            continue;
        /* a ref-qualified one: [class.copy.elision]/3's first overload
           resolution takes the returned lvalue as an rvalue, which the
           sequence of an argument is not told apart from */
        if (ambiguous || entity_table[f].explicit_ || chosen ||
            (entity_table[f].member_cv & (cv_ref_lvalue | cv_ref_rvalue))) {
            unknown = true;
            return false;
        }
        /* the implicit object parameter ([over.match.funcs]/4) */
        if (object_sequence(a, f).kind == sequence::form::bad)
            continue;
        chosen = f;
    }
    if (!chosen)
        return false;
    sequence second =
        class_sequence(conversion_function_result(chosen), to, false);
    if (second.kind != sequence::form::standard) {
        unknown = true;
        return false;
    }
    found = second;
    found.kind = sequence::form::user_defined;
    found.function = chosen;
    return true;
}

semantic_analysis::conversion_sequence
semantic_analysis::class_sequence(const expression_meaning &a,
                                  qualified_type to, bool user_defined)
{
    const struct type t = type_table[to.unqualified()];
    entity_index c1 = t.entity;
    if (entity_table[c1].kind != entity_kind::class_type)
        return of_form(sequence::form::unknown);
    const struct type y = type_table[a.type.unqualified()];
    if (y.kind == type_kind::class_type) {
        entity_index c2 = y.entity;
        if (entity_table[c2].kind != entity_kind::class_type)
            return of_form(sequence::form::unknown);
        sequence s = of_form(sequence::form::standard);
        s.target = qualified_type::make(to.unqualified(), cv_none);
        if (c1 == c2)
            return s;
        bool ambiguous = false;
        require_complete(c2);
        if (entity_table[c2].size && base_step(c2, c1, ambiguous)) {
            if (ambiguous)
                return of_form(sequence::form::unknown);
            s.identity = false;
            s.rank = 2;
            s.base_from = c2;
            s.base_to = c1;
            return s;
        }
    } else if (y.kind == type_kind::none) {
        return of_form(sequence::form::unknown);
    }
    /* [over.match.copy]/1.2: the conversion functions of the argument's
       class are candidates too */
    sequence by_function = of_form(sequence::form::bad);
    if (y.kind == type_kind::class_type && user_defined) {
        bool unknown = false;
        if (!conversion_function_to_class(a, to, by_function, unknown) &&
            unknown) {
            /* (a class unrelated to c1: only a user-defined conversion
               converts it) */
            sequence cannot_tell = of_form(sequence::form::unknown);
            cannot_tell.user_defined_if_viable = true;
            return cannot_tell;
        }
    }
    if (!user_defined)
        return of_form(sequence::form::bad);
    /* [temp.mem]/1: a constructor template of c1 only declared is a
       candidate too: not analysed */
    if (entity_table[c1].constructor_template)
        return of_form(sequence::form::unknown);
    /* the converting constructors of c1, and the specializations of its
       converting constructor templates, with one parameter to take the
       argument */
    owned_array<entity_index> constructors;
    for (entity_index k = entity_table[c1].first_constructor; k;
         k = entity_table[k].next_constructor)
        constructors.append(k);
    if (entity_table[c1].constructor_templates) {
        owned_array<overload_candidate> specializations;
        if (constructor_template_candidates(SyntaxRef(), c1, &a, 1, true,
                                            specializations) !=
            analysis_result::well_formed)
            return of_form(sequence::form::unknown);
        for (const overload_candidate &k : specializations)
            constructors.append(k.function);
    }
    sequence best = of_form(sequence::form::bad);
    bool unique = true, unknown = false;
    for (entity_index k : constructors) {
        const struct type &f = type_table[entity_table[k].type.unqualified()];
        /* [over.match.copy]/1.1: "the converting constructors of T" */
        if (entity_table[k].explicit_)
            continue;
        if (f.parameter_count == 0 ||
            f.parameter_count - entity_table[k].default_arguments > 1)
            continue;
        qualified_type p =
            type_table.parameters(entity_table[k].type.unqualified())[0];
        sequence first = implicit_conversion_sequence(a, p, false);
        if (first.kind == sequence::form::unknown)
            unknown = true;
        if (first.kind != sequence::form::standard)
            continue;
        if (best.kind == sequence::form::bad) {
            best = first;
            best.function = k;
            continue;
        }
        int c = compare_conversions(first, best);
        if (c < 0) {
            best = first;
            best.function = k;
            unique = true;
        } else if (c == 0) {
            unique = false;
        }
    }
    if (unknown)
        return of_form(sequence::form::unknown);
    if (by_function.kind == sequence::form::user_defined) {
        /* [over.match.copy]/1: a conversion function and a converting
           constructor both viable are candidates of one overload
           resolution: not ranked */
        if (best.kind != sequence::form::bad) {
            sequence cannot_tell = of_form(sequence::form::unknown);
            cannot_tell.user_defined_if_viable = true;
            return cannot_tell;
        }
        return by_function;
    }
    if (best.kind == sequence::form::bad)
        return best;
    /* the second standard conversion sequence: the constructor's prvalue
       of c1 to the parameter, an identity */
    sequence s = of_form(sequence::form::user_defined);
    s.target = qualified_type::make(to.unqualified(), cv_none);
    s.function = best.function;
    s.ambiguous = !unique;
    if (!unique)
        s.function = 0;
    return s;
}

/*
 * [over.ics.ref]/1: "When a parameter of reference type binds directly
 * ([dcl.init.ref]) to an argument expression, the implicit conversion
 * sequence is the identity conversion, unless the argument expression has
 * a type that is a derived class of the parameter type, in which case the
 * implicit conversion sequence is a derived-to-base Conversion".  /2: "When
 * a parameter of reference type is not bound directly to an argument
 * expression, the conversion sequence is the one required to convert the
 * argument expression to the referenced type according to
 * [over.best.ics]."  /3: "Except for an implicit object parameter ... a
 * standard conversion sequence cannot be formed if it requires binding an
 * lvalue reference other than a reference to a non-volatile const type to
 * an rvalue or binding an rvalue reference to an lvalue other than a
 * function lvalue."  The cases are [dcl.init.ref]/5's, as reference_binding
 * has them.
 */
semantic_analysis::conversion_sequence semantic_analysis::reference_sequence(
    const expression_meaning &a, qualified_type reference, bool user_defined)
{
    const struct type r = type_table[reference.unqualified()];
    bool rvalue_reference = r.kind == type_kind::rvalue_reference;
    qualified_type t1 = r.operand, t2 = a.type;
    const struct type x = type_table[t1.unqualified()];
    const struct type y = type_table[t2.unqualified()];
    bool lvalue = a.category == value_category::lvalue;
    /* a const lvalue reference or an rvalue reference: one that binds to
       an rvalue (or a temporary) */
    bool binds_rvalues =
        rvalue_reference || ((t1.cv() & cv_const) && !(t1.cv() & cv_volatile));
    if (a.bit_field_width && lvalue)
        /* [dcl.init.ref]/5: a non-const lvalue reference "shall not be bound
           to a bit-field"; a const one is bound to a temporary (not
           analysed) */
        return of_form(sequence::form::unknown);
    sequence s;
    if (x.kind == type_kind::class_type || y.kind == type_kind::class_type) {
        entity_index c1 = 0, c2 = 0;
        if (x.kind == type_kind::class_type)
            c1 = x.entity;
        if (y.kind == type_kind::class_type)
            c2 = y.entity;
        if ((c1 && entity_table[c1].kind != entity_kind::class_type) ||
            (c2 && entity_table[c2].kind != entity_kind::class_type) ||
            x.kind == type_kind::none || y.kind == type_kind::none)
            return of_form(sequence::form::unknown);
        bool ambiguous = false;
        /* a base needs the complete derived class; the same class is
           reference-related to itself as it is ([dcl.init.ref]/5.1: a
           direct binding, no constructor: libc++'s concepts name
           pair<const string, J>& while J is being defined) */
        if (c1 != c2)
            require_complete(c2);
        bool related = c1 && c2 && (c1 == c2 || entity_table[c2].size) &&
                       reference_related(t1, t2, ambiguous);
        if (ambiguous)
            return of_form(sequence::form::unknown);
        if (related) {
            /* [dcl.init.ref]/5.1 and /5.3: bound directly when
               reference-compatible; /5.4.4: "cv1 shall be the same
               cv-qualification as, or greater cv-qualification than, cv2;
               and if the reference is an rvalue reference, the initializer
               expression shall not be an lvalue" */
            if (t2.cv() & ~t1.cv())
                return of_form(sequence::form::bad);
            if ((!lvalue && !binds_rvalues) || (rvalue_reference && lvalue))
                return of_form(sequence::form::bad);
            s = of_form(sequence::form::standard);
            if (c1 != c2) {
                s.identity = false;
                s.rank = 2;
                s.base_from = c2;
                s.base_to = c1;
            }
        } else {
            /* [dcl.init.ref]/5.4.1: "If T1 or T2 is a class type and T1 is
               not reference-related to T2, user-defined conversions are
               considered using the rules for copy-initialization of an
               object of type "cv1 T1" by user-defined conversion": the
               converting constructors of T1, a temporary (no conversion
               functions: see above) */
            /* [dcl.init.ref]/5.1.2, /5.3.2 ([over.match.ref]): a
               conversion function of T2's: not analysed */
            if (c2 && user_defined) {
                /* (those that yield T1 or a class derived from it) */
                bool functions = has_conversion_functions(c2);
                if (c1) {
                    conversion_sequence found;
                    bool cannot_tell = false;
                    functions =
                        conversion_function_to_class(
                            a, qualified_type::make(t1.unqualified(), cv_none),
                            found, cannot_tell) ||
                        cannot_tell;
                    /* [dcl.init.ref]/5.1.2, [over.match.ref]: the one
                       conversion function yielding an lvalue reference to
                       a type cv1 T1 reference-compatible with (here the
                       same class as) T1 binds the lvalue reference
                       directly: the user-defined conversion sequence of
                       that function, its second conversion an identity
                       ([over.ics.ref]/2) */
                    if (functions && !cannot_tell && !rvalue_reference &&
                        found.kind == sequence::form::user_defined &&
                        found.function && found.rank == 0) {
                        const struct type &fr =
                            type_table[conversion_function_result(
                                           found.function)
                                           .type.unqualified()];
                        qualified_type referent =
                            conversion_function_result(found.function).type;
                        bool yields_lvalue =
                            conversion_function_result(found.function)
                                .category == value_category::lvalue;
                        if (yields_lvalue && fr.kind == type_kind::class_type &&
                            fr.entity == c1 && !(referent.cv() & ~t1.cv()))
                            return found;
                    }
                }
                if (functions) {
                    sequence cannot_tell = of_form(sequence::form::unknown);
                    /* [over.ics.rank]/2: once the source class is complete
                       and known not to be reference-related, any viable
                       conversion here is user-defined. */
                    cannot_tell.user_defined_if_viable = entity_table[c2].size;
                    return cannot_tell;
                }
            }
            if (!c1 || !binds_rvalues || !user_defined)
                return of_form(sequence::form::bad);
            s = class_sequence(
                a, qualified_type::make(t1.unqualified(), cv_none), true);
            if (s.kind != sequence::form::user_defined)
                return s;
            lvalue = false;
        }
    } else {
        if (x.kind == type_kind::none || y.kind == type_kind::none ||
            x.kind == type_kind::pointer_to_member ||
            y.kind == type_kind::pointer_to_member)
            return of_form(sequence::form::unknown);
        bool compatible = reference_compatible(t1, t2);
        bool ambiguous = false;
        bool related = reference_related(t1, t2, ambiguous);
        bool function_lvalue = y.kind == type_kind::function && lvalue;
        if (compatible && ((!rvalue_reference && lvalue) ||
                           (binds_rvalues && (!lvalue || function_lvalue)))) {
            /* /5.1, /5.3: bound directly */
            s = of_form(sequence::form::standard);
            s.function_lvalue = function_lvalue;
        } else {
            if (!binds_rvalues || x.kind == type_kind::function)
                return of_form(sequence::form::bad);
            if (related &&
                ((t2.cv() & ~t1.cv()) || (rvalue_reference && lvalue)))
                return of_form(sequence::form::bad);
            /* /5.4.2: "the initializer expression is implicitly converted
               to a prvalue of type "cv1 T1".  The temporary materialization
               conversion is applied and the reference is bound to the
               result." */
            s = standard_conversion_sequence(a, t1);
            if (s.kind != sequence::form::standard)
                return s;
            lvalue = false;
        }
    }
    s.reference = true;
    s.rvalue_reference = rvalue_reference;
    s.binds_rvalue = !lvalue;
    s.target = t1;
    return s;
}

semantic_analysis::conversion_sequence
semantic_analysis::implicit_conversion_sequence(const expression_meaning &a,
                                                qualified_type parameter,
                                                bool user_defined)
{
    /* [temp.inst]/2: the constructors and conversion functions a
       conversion uses: a specialization named only is defined here */
    const struct type p = type_table[parameter.unqualified()];
    /* ([dcl.init.ref]/5.1: a reference bound to the same class needs no
       complete class, reference_sequence) */
    bool direct = false;
    if (p.kind == type_kind::lvalue_reference ||
        p.kind == type_kind::rvalue_reference) {
        const struct type &r =
            type_table[referenced_type(parameter).unqualified()];
        const struct type &y =
            type_table[referenced_type(a.type).unqualified()];
        direct = r.kind == type_kind::class_type &&
                 y.kind == type_kind::class_type && y.entity == r.entity;
    }
    if (!direct) {
        require_complete(parameter);
        require_complete(a.type);
    }
    if (p.kind == type_kind::lvalue_reference ||
        p.kind == type_kind::rvalue_reference)
        return reference_sequence(a, parameter, user_defined);
    if (p.kind == type_kind::class_type)
        return class_sequence(a, parameter, user_defined);
    const struct type y = type_table[a.type.unqualified()];
    if (y.kind == type_kind::class_type) {
        /* to a type that is no class: by a conversion function of the
           argument's class ([over.ics.user]/2: "The second standard
           conversion sequence converts the result of the user-defined
           conversion to the target type") */
        if (!user_defined)
            return of_form(sequence::form::bad);
        bool ambiguous = false, unknown = false;
        entity_index f =
            conversion_function_for(a, parameter, false, ambiguous, unknown);
        if (unknown) {
            sequence s = of_form(sequence::form::unknown);
            s.user_defined_if_viable = true;
            return s;
        }
        if (ambiguous) {
            sequence s = of_form(sequence::form::user_defined);
            s.ambiguous = true;
            s.target = qualified_type::make(parameter.unqualified(), cv_none);
            return s;
        }
        if (!f)
            return of_form(sequence::form::bad);
        /* the second standard conversion sequence, kept in the sequence
           for [over.ics.rank]/3.3 */
        expression_meaning value = conversion_function_result(f);
        sequence s = standard_conversion_sequence(value, parameter);
        if (s.kind != sequence::form::standard) {
            sequence u = of_form(sequence::form::unknown);
            u.user_defined_if_viable = true;
            return u;
        }
        s.kind = sequence::form::user_defined;
        s.function = f;
        return s;
    }
    return standard_conversion_sequence(a, parameter);
}

/*
 * [over.match.funcs]/4: "For non-static member functions, the type of the
 * implicit object parameter is "lvalue reference to cv X" for functions
 * declared without a ref-qualifier ... where X is the class of which the
 * function is a member and cv is the cv-qualification on the member
 * function declaration. ... For static member functions, the implicit
 * object parameter is considered to match any object (since if the function
 * is selected, the object is discarded)."  /5: "even if the implicit object
 * parameter is not const-qualified, an rvalue can be bound to the
 * parameter as long as in all other respects the argument can be converted
 * to the type of the implicit object parameter."
 */
semantic_analysis::conversion_sequence
semantic_analysis::object_sequence(const expression_meaning &object,
                                   entity_index f) const
{
    const entity &x = entity_table[f];
    sequence s = of_form(sequence::form::standard);
    if (!x.nonstatic_member) {
        s.any_object = true;
        return s;
    }
    if (object.type.cv() & ~x.member_cv)
        return of_form(sequence::form::bad);
    s.reference = true;
    s.implicit_object = true;
    s.binds_rvalue = object.category != value_category::lvalue;
    /* [over.match.funcs]/4: the type of the implicit object parameter of a
       function with a ref-qualifier is "lvalue reference to cv X" for &
       and "rvalue reference to cv X" for &&; /5: only a function declared
       without a ref-qualifier binds an rvalue to a non-const parameter */
    if (x.member_cv & cv_ref_lvalue) {
        if (s.binds_rvalue &&
            (!(x.member_cv & cv_const) || (x.member_cv & cv_volatile)))
            return of_form(sequence::form::bad);
        /* [over.ics.rank]/3.2.3 applies to it */
        s.implicit_object = false;
    }
    if (x.member_cv & cv_ref_rvalue) {
        if (!s.binds_rvalue)
            return of_form(sequence::form::bad);
        s.rvalue_reference = true;
        s.implicit_object = false;
    }
    s.target =
        qualified_type::make(object.type.unqualified(), object_cv(x.member_cv));
    entity_index c = type_table[object.type.unqualified()].entity;
    /* [over.match.funcs]/4: "For non-conversion functions introduced by a
       using-declaration into a derived class, the function is considered
       to be a member of the derived class for the purpose of defining the
       type of the implicit object parameter": the class the name is found
       in */
    entity_index owner = x.enclosing;
    /* [over.match.funcs]/4: "For conversion functions, the function is
       considered to be a member of the class of the implied object
       argument for the purpose of defining the type of the implicit object
       parameter": a base's one takes the derived class's object as it
       is, no derived-to-base conversion ranks it below the derived
       class's own */
    if (x.conversion_function)
        owner = c;
    if (c != owner && x.name && !x.conversion_function &&
        is_class_entity(owner) && is_class_entity(c)) {
        lookup_result found = lookup_qualified(c, x.name, lookup_filter::any);
        if (found.found_in != owner && is_class_entity(found.found_in))
            owner = found.found_in;
    }
    if (c != owner) {
        s.identity = false;
        s.rank = 2;
        s.base_from = c;
        s.base_to = owner;
    }
    return s;
}

} // namespace cplusplus
